#include "render/runtime_gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "data/map_item.h"
#include "engine/anim.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi_2d.h"
#include "platform/psi_draw.h"
#include "platform/psi_gfx.h"
#include "platform/vu1.h"
#include "render/frame_interp.h"
#include "render/frame_markers.h"
#include "render/ui_hd.h"
#include "render/ui_hd_game.h"
#include "platform/fmath.h"

namespace nf {

namespace {

// As matrizes do motor não escrevem a coluna w (View_RotTransMatrix); o renderizador precisa dela.
std::array<float, 16> to_array(const MATRIX& m) {
    std::array<float, 16> out;
    std::memcpy(out.data(), m.m, sizeof(out));
    out[3] = out[7] = out[11] = 0.0f;
    out[15] = 1.0f;
    return out;
}

// RotMatrix(rot) com translação pos: a mesma transformação de parseentity_transform_bounding_box.
std::array<float, 16> cel_matrix(const cel_tag& cel) {
    MATRIX m;
    RotMatrix(&cel.rot, &m);
    for (int r = 0; r < 3; ++r) {
        m.m[r][3] = 0.0f;
    }
    m.m[3][0] = cel.pos.x;
    m.m[3][1] = cel.pos.y;
    m.m[3][2] = cel.pos.z;
    m.m[3][3] = 1.0f;
    return to_array(m);
}

// Registradores do GS em vigor num desenho registrado: o ALPHA_1 de psiSetUpColourBlend e, se
// psiSwitchZBufferRead/Write já foram chamados no quadro, TEST_1 e ZBUF_1.
GsState gs_of(const PsiDrawCall& call) {
    GsState g;
    g.alpha = gs_alpha_of_blend(Blend(call.blend & 3));
    g.has = GsState::kAlpha;
    if (call.gs_has & 2) {
        g.test = call.gs_test;
        g.has |= GsState::kTest;
    }
    if (call.gs_has & 4) {
        g.zbuf = call.gs_zbuf;
        g.has |= GsState::kZbuf;
    }
    return g;
}

// Quadrados virados para a câmera de uma lista de psiDrawParticleList. Como o microprograma da VU1,
// o sprite some quando o centro sai do volume de recorte (o dobro da tela em x e y, a partir de
// Ps2Near2 = 0.1 m na frente); a textura fica girada 180 graus (ST (1, 1) em cima à esquerda).
// Direita e cima da tela como em XMMatrixLookToRH (main.cpp): R = F x U, U' = -F x R.
void build_sprites(const PsiParticleList& list, const viewer_tag* camera, DrawItem& item) {
    const MATRIX& cam = camera->camera;
    float f[3] = {cam.m[2][0], cam.m[2][1], cam.m[2][2]};
    const float up0[3] = {cam.m[1][0], cam.m[1][1], cam.m[1][2]};
    const float eye[3] = {cam.m[3][0], cam.m[3][1], cam.m[3][2]};
    const auto normalize = [](float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0.0f) {
            for (int k = 0; k < 3; ++k) v[k] /= l;
        }
    };
    normalize(f);
    float r[3] = {f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0]};
    normalize(r);
    const float u[3] = {-(f[1] * r[2] - f[2] * r[1]), -(f[2] * r[0] - f[0] * r[2]), -(f[0] * r[1] - f[1] * r[0])};
    const float cot = camera->fov > 0.0f ? 1.0f / nf::fm::tan(camera->fov * 0.5f) : 1.0f;
    const float aspect = camera->aspect > 0.0f ? camera->aspect : 1.0f;
    const float s = list.half_size;
    item.sprites.reserve(size_t(list.count) * 4);
    for (uint32_t i = 0; i < list.count; ++i) {
        const PsiParticle& p = g_psi_frame.particles[list.first + i];
        const float d[3] = {p.pos[0] - eye[0], p.pos[1] - eye[1], p.pos[2] - eye[2]};
        const float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        if (z < 0.1f) continue;
        const float x = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * cot / (aspect * z);
        const float y = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) * cot / z;
        if (std::fabs(x) > 2.0f || std::fabs(y) > 2.0f) continue;
        const float corner[4][2] = {{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}};
        const float uv[4][2] = {{1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}};
        for (int k = 0; k < 4; ++k) {
            Vertex v{};
            for (int c = 0; c < 3; ++c) {
                v.pos[c] = p.pos[c] + s * (corner[k][0] * r[c] + corner[k][1] * u[c]);
                v.normal[c] = -f[c];
            }
            v.uv[0] = uv[k][0];
            v.uv[1] = uv[k][1];
            std::memcpy(v.color, p.rgba, 4);
            item.sprites.push_back(v);
        }
    }
}

// O que FillMatrixChainRot/Skin mandariam à VU1 para um desenho: as luzes (no espaço dos vértices da
// malha rígida, pela inversa da matriz do objeto; no mundo com skinning, cuja matriz é a identidade) e
// as linhas da câmera do mapa de ambiente.
void set_vu1_state(const PsiDrawCall& call, bool skinned, DrawItem& item) {
    item.num_lights = call.num_lights;
    std::memcpy(item.light_color, call.light_color, sizeof(item.light_color));
    if (skinned) {
        std::memcpy(item.light_pos, call.light_pos, sizeof(item.light_pos));
    } else {
        vu1::lights_to_local(call.world, call.light_pos, item.light_pos);
    }
    if (call.camera != nullptr) {
        for (int k = 0; k < 3; ++k) {
            item.env_s[k] = call.camera->camera.m[0][k];
            item.env_t[k] = call.camera->camera.m[1][k];
        }
    }
}

// Os sprites 2D de uma chamada de psiDrawSprites, levados da tela do PS2 (512 x 448) para a janela por
// psi_2d_viewport e psi_2d_map_rect; elementos seguidos com o mesmo recorte (a área 4:3, ou a janela
// inteira para os de tela cheia esticados) viram um item só, também entre chamadas. Aqui é onde uma
// camada HD trocaria o desenho de cada elemento pela origem dele (Psi2DQuad::origin). Só no PC: uma moldura
// (psi_2d_find_frame) fora da proporção vira nove pedaços (psi_2d_nine_slice); na proporção, o quadrado
// original.
void add_2d(const Psi2DQuad* quads, uint32_t count, int width, int height, std::vector<DrawItem>& out) {
    const Psi2DViewport vp = psi_2d_viewport(width, height);
    const float area[4] = {vp.left, vp.top, vp.left + vp.width, vp.top + vp.height};
    const float window[4] = {0.0f, 0.0f, float(width), float(height)};
    for (uint32_t i = 0; i < count; ++i) {
        const Psi2DQuad& src = quads[i];
        Psi2DQuad pieces[9];
        int n = 1;
        pieces[0] = src;
        if (const Psi2DFrame* f = psi_2d_find_frame(src.origin.texture_hash)) {
            const float in[4] = {src.x0, src.y0, src.x1, src.y1};
            float r[4];
            if (!psi_2d_map_rect(vp, width, height, in, r)) n = psi_2d_nine_slice(src, *f, vp.scale_x / vp.scale_y, pieces);
        }
        for (int k = 0; k < n; ++k) {
            const Psi2DQuad& q = pieces[k];
            const float in[4] = {q.x0, q.y0, q.x1, q.y1};
            float r[4];
            const float* clip = psi_2d_map_rect(vp, width, height, in, r) ? window : area;
            if (out.empty() || out.back().quads2d.empty() || out.back().fade >= 0 ||
                std::memcmp(out.back().clip2d, clip, sizeof(float) * 4) != 0) {
                DrawItem item;
                item.blend = Blend::Alpha;  // misturado: os provisórios entram antes
                std::memcpy(item.clip2d, clip, sizeof(item.clip2d));
                out.push_back(std::move(item));
            }
            std::vector<Quad2D>& quads = out.back().quads2d;
            Quad2D o;
            o.x0 = r[0];
            o.y0 = r[1];
            o.x1 = r[2];
            o.y1 = r[3];
            o.u0 = q.u0;
            o.v0 = q.v0;
            o.u1 = q.u1;
            o.v1 = q.v1;
            std::memcpy(o.rgba, q.rgba, 4);
            o.texture = q.texture;
            o.alpha = q.alpha;
            o.linear = q.linear;
            quads.push_back(o);
        }
    }
}

// O esmaecimento de psiFadeView: o sprite da tela inteira do PS2 (CreateTotalScreenSprite: 512 x a altura da tela)
// vira um quadrado sem textura (kQuadNoTexture) na janela inteira, também fora da área 4:3, com a cor e o alfa do
// jogo e o ALPHA_1 0x44 do sprite. A câmera vai em DrawItem::fade (a identidade entre ticks).
void add_fade(const PsiDrawCall& call, int width, int height, std::vector<DrawItem>& out) {
    DrawItem item;
    item.blend = Blend::Alpha;
    item.clip2d[2] = float(width);
    item.clip2d[3] = float(height);
    for (int i = 0; i < kViewerCount; ++i) {
        if (glb_viewer[i] != nullptr && glb_viewer[i] == call.camera) item.fade = int8_t(i);
    }
    if (item.fade < 0) item.fade = int8_t(kViewerCount);  // sem câmera do jogo (testes): uma identidade própria
    Quad2D q;
    q.x1 = float(width);
    q.y1 = float(height);
    q.u1 = q.v1 = 1.0f;
    std::memcpy(q.rgba, call.fade_rgba, 4);
    q.texture = kQuadNoTexture;
    q.alpha = 0x44;
    q.linear = true;
    item.quads2d.push_back(q);
    out.push_back(std::move(item));
}

// O lote da camada HD de uma chamada de psiDrawSprites (vazio: nada a desenhar nela).
void add_ui_hd(const UiHdFrame& hd, uint32_t list, std::vector<DrawItem>& out) {
    const auto it = hd.batches.find(list);
    if (it == hd.batches.end() || it->second == nullptr || it->second->empty()) return;
    DrawItem item;
    item.blend = Blend::Alpha;  // misturado: os provisórios entram antes
    item.ui = it->second;
    out.push_back(std::move(item));
}

// Só do port: a identidade do desenho entre ticks (render/frame_interp.h), pelo dono que psi_draw gravou.
DrawItem::Interp interp_of(const PsiDrawCall& call, bool skinned) {
    DrawItem::Interp i;
    if (call.owner == nullptr) return i;
    i.id = draw_identity(call.owner, uint8_t(call.owner_kind), call.glist, call.created, call.owner_seq, skinned);
    i.owner = call.owner;
    i.owner_kind = uint8_t(call.owner_kind);
    i.list = call.list;
    i.created = call.created;
    return i;
}

}  // namespace

RuntimeGfx::RuntimeGfx(Renderer& renderer) : renderer_(renderer) {
    g_psi_gfx_backend.user = this;
    g_psi_gfx_backend.create_texture = &RuntimeGfx::on_texture;
    g_psi_gfx_backend.create_entity = &RuntimeGfx::on_entity;
    g_psi_gfx_backend.reset = &RuntimeGfx::on_reset;
}

RuntimeGfx::~RuntimeGfx() { g_psi_gfx_backend = {}; }

void RuntimeGfx::on_texture(void* user, int index, const bmpheader_tag& header, const uint8_t* pixels,
                            uint32_t pixels_bytes, const uint8_t* palette, uint32_t palette_bytes) {
    auto* self = static_cast<RuntimeGfx*>(user);
    Texture t;
    const int frames = std::max(1, int(header.frames & 0xff));
    if (decode_texture(header.width1 + 1, header.height1 + 1, frames, (header.flags & 1) != 0,
                       ByteView(pixels, pixels_bytes), ByteView(palette, palette_bytes), t)) {
        self->renderer_.set_texture(index, t);
        if (self->mirror_ != nullptr) self->mirror_->mirror_texture(index, t);
        ++self->textures_;
        // Substituta HD: mesmos texels (primeiro quadro) e mesma paleta, como o PCSX2 os vê.
        const bool bpp8 = (header.flags & 1) != 0;
        const size_t frame_bytes = size_t(t.width) * size_t(t.height) / (bpp8 ? 1 : 2);
        if (self->pack_ != nullptr && pixels_bytes >= frame_bytes && palette_bytes >= (bpp8 ? 1024u : 64u)) {
            const std::wstring file = self->pack_->find(t.width, t.height, bpp8, pixels, palette);
            if (!file.empty() && self->renderer_.set_texture_dds(index, file)) ++self->hd_;
        }
    }
}

int RuntimeGfx::on_entity(void* user, const celglist_tag* glist, const map_tag* map) {
    auto* self = static_cast<RuntimeGfx*>(user);
    const uint8_t* blk = glist->gfx->ps2_gfx;
    uint32_t header;
    std::memcpy(&header, blk, 4);
    const auto frames_of = [map](int tex) {
        if (map == nullptr || tex < 0 || uint32_t(tex) >= map->bmp_count) return 1;
        return std::max(1, int(map->bmp[tex].frames & 0xff));
    };
    EntityMesh mesh;
    const ByteView block(blk, header & 0xffffff);
    try {
        mesh = decode_gfx(block, frames_of);
    } catch (const FormatError&) {
        if (gfx_is_skinned(block)) ++self->skinned_failed_;
        return -1;
    }
    if (mesh.skinned) {
        if (mesh.empty() || mesh.bad_batches != 0) {
            ++self->skinned_failed_;
        } else {
            ++self->skinned_;
        }
    }
    if (mesh.empty()) return -1;
    // Os alvos de morph do rosto (bloco 0x2B, entgfx_tag::morph aponta para o conteúdo; o tamanho está no cabeçalho
    // do bloco, 4 bytes antes). Um bloco que não se decodifica, ou um vértice cujos deltas passariam do fim, fica
    // sem morphs (contado em morph_failed).
    if (mesh.skinned && glist->gfx->morph != nullptr) {
        uint32_t mh;
        std::memcpy(&mh, glist->gfx->morph - 4, 4);
        const uint32_t size = mh & 0xffffff;
        try {
            if ((mh >> 24) != 0x2b || size < 4) throw FormatError("cabeçalho do bloco de morph");
            mesh.morph = decode_morph(ByteView(nf::raw(glist->gfx->morph), size - 4));
            for (const SkinnedVertex& v : mesh.skin_vertices) {
                if (v.morph >= 0 && size_t(v.morph) + mesh.morph.targets > mesh.morph.elements()) {
                    throw FormatError("vértice com morph fora do bloco");
                }
            }
            ++self->morph_meshes_;
        } catch (const FormatError&) {
            mesh.morph = MorphTargets{};
            ++self->morph_failed_;
        }
    }
    for (SubMesh& sm : mesh.submeshes) {
        if (sm.texture >= 0) sm.texture = psi_gfx_map_texture(map, sm.texture);
    }
    MeshInfo info{};
    for (int k = 0; k < 3; ++k) {
        info.bmin[k] = 1e30f;
        info.bmax[k] = -1e30f;
    }
    const auto grow = [&info](const float* p) {
        for (int k = 0; k < 3; ++k) {
            info.bmin[k] = std::min(info.bmin[k], p[k]);
            info.bmax[k] = std::max(info.bmax[k], p[k]);
        }
    };
    for (const Vertex& v : mesh.vertices) grow(v.pos);
    for (const SkinnedVertex& v : mesh.skin_vertices) grow(v.pos);  // pose de repouso
    info.triangles = mesh.indices.size() / 3;
    info.skinned = mesh.skinned;
    info.morph_targets = mesh.morph.empty() ? 0 : int(mesh.morph.targets);
    const int id = self->renderer_.add_mesh(mesh);
    if (self->mirror_ != nullptr) self->mirror_->mirror_mesh(id, mesh);
    if (id >= int(self->meshes_.size())) self->meshes_.resize(size_t(id) + 1);
    self->meshes_[size_t(id)] = info;
    return id;
}

void RuntimeGfx::on_reset(void* user) {
    auto* self = static_cast<RuntimeGfx*>(user);
    self->renderer_.reset_resources();
    if (self->mirror_ != nullptr) self->mirror_->mirror_reset();
    self->meshes_.clear();
    self->textures_ = 0;
    self->hd_ = 0;
    self->skinned_ = 0;
    self->skinned_failed_ = 0;
    self->morph_meshes_ = 0;
    self->morph_failed_ = 0;
}

int RuntimeGfx::rigid_mesh(const celglist_tag* glist) const {
    const int mesh = psi_gfx_entity_mesh(glist);
    if (mesh < 0 || mesh >= int(meshes_.size()) || meshes_[size_t(mesh)].skinned) return -1;
    return mesh;
}

void RuntimeGfx::collect(std::vector<DrawItem>& out) const {
    out.clear();
    if (glb_world != nullptr) {
        for (const cel_tag* cel = glb_world->first; cel != nullptr; cel = cel->next) {
            const int mesh = rigid_mesh(cel->glist);
            if (mesh < 0) continue;
            out.push_back({mesh, cel_matrix(*cel), classify_object(cel->type)});
        }
    }
    // Objetos de verdade (DynamicObjList): modelo e matriz do próprio objeto.
    for (const obj_tag* obj = DynamicObjList.next; obj != nullptr; obj = obj->next) {
        const int mesh = rigid_mesh(obj->glist);
        if (mesh < 0) continue;
        out.push_back({mesh, to_array(obj->matrix), ObjectKind::Visible});
    }
    for (const ProvisionalObject& obj : g_provisional_objects) {
        const int mesh = rigid_mesh(obj.glist);
        if (mesh < 0) continue;
        out.push_back({mesh, to_array(obj.world), classify_object(obj.flags)});
    }
}

void RuntimeGfx::collect_frame(const viewer_tag* camera, std::vector<DrawItem>& out) const {
    out.clear();
    // Camada 2D em alta definição: o 2D do quadro inteiro refeito de uma vez (um texto pode atravessar
    // chamadas de psiDrawSprites), um lote por chamada.
    UiHdFrame hd;
    if (ui_hd_ != nullptr) {
        UiHdEnv env = ui_hd_game_env(renderer_.width(), renderer_.height());
        const Renderer* r = &renderer_;
        env.measure = [r](const std::wstring& label, float cap_px) { return r->ui_hd_measure(label, cap_px); };
        hd = ui_hd_build(g_psi_frame.quads2d.data(), g_psi_frame.quads2d.size(), g_psi_frame.lists2d.data(),
                         g_psi_frame.lists2d.size(), *ui_hd_, env);
    }
    // A passada de névoa (psiDrawFogPass) e as limpezas do Z (psiClearZ, antes da lista 2) valem para o quadro
    // inteiro, na posição em que o jogo as pediu (render/frame_markers.h).
    for (size_t index = 0; index < g_psi_frame.draws.size(); ++index) {
        const PsiDrawCall& call = g_psi_frame.draws[index];
        add_frame_markers(g_psi_frame, index, out);
        if (call.fade != 0) {
            // psiFadeView: de todas as câmeras, na ordem do Game_Draw (por cima do 3D e do 2D da câmera que o pediu,
            // por baixo do 2D das câmeras seguintes), na janela inteira. Não some com o 2D escondido pelos menus
            // Modern: é a cena escurecendo, não um menu.
            add_fade(call, renderer_.width(), renderer_.height(), out);
            continue;
        }
        if (call.sprites2d >= 0) {
            // 2D: de todas as câmeras, como a tela do PS2, que junta o HUD e as legendas (câmeras 0, 4 e 5)
            // por cima da cena. Só do PC: menos as escondidas pelos menus Modern (set_hidden_2d).
            if (hidden_2d_ != 0) {
                int cam = -1;
                for (int i = 0; i < 10; ++i) {
                    if (glb_viewer[i] != nullptr && glb_viewer[i] == call.camera) cam = i;
                }
                if (hidden_2d_ == ~0u || (cam >= 0 && ((hidden_2d_ >> cam) & 1u) != 0)) continue;
            }
            if (ui_hd_ != nullptr) {
                add_ui_hd(hd, uint32_t(call.sprites2d), out);
                continue;
            }
            const Psi2DList& list = g_psi_frame.lists2d[size_t(call.sprites2d)];
            add_2d(g_psi_frame.quads2d.data() + list.first, list.count, renderer_.width(), renderer_.height(), out);
            continue;
        }
        if (call.camera != camera) continue;
        if (call.particles >= 0) {
            // Sprites: o microprograma de partículas não usa o tweak.
            DrawItem item;
            item.blend = Blend(call.blend & 3);
            item.gs = gs_of(call);
            const PsiParticleList& list = g_psi_frame.particle_lists[size_t(call.particles)];
            item.sprite_texture = list.texture;
            item.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // os vértices já estão no mundo
            if (camera != nullptr) build_sprites(list, camera, item);
            if (!item.sprites.empty()) out.push_back(std::move(item));
            continue;
        }
        if (call.triangles >= 0) {
            // Estilhaços (psiDrawShard): triângulos já no mundo, cor na escala do GS sem o tweak (a
            // rotina 0 da VU1). Chamadas seguidas com a mesma textura e o mesmo estado viram um item só.
            const PsiTriList& list = g_psi_frame.tri_lists[size_t(call.triangles)];
            const GsState gs = gs_of(call);
            const Blend blend = Blend(call.blend & 3);
            if (out.empty() || out.back().triangles.empty() || out.back().triangle_texture != list.texture ||
                out.back().blend != blend || out.back().gs != gs) {
                DrawItem item;
                item.blend = blend;
                item.gs = gs;
                item.triangle_texture = list.texture;
                item.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                out.push_back(std::move(item));
            }
            std::vector<Vertex>& tris = out.back().triangles;
            for (uint32_t i = 0; i < list.count; ++i) {
                const PsiTriVertex& t = g_psi_frame.tri_vertices[list.first + i];
                Vertex v{};
                std::memcpy(v.pos, t.pos, sizeof(v.pos));
                std::memcpy(v.uv, t.uv, sizeof(v.uv));
                std::memcpy(v.color, t.rgba, sizeof(v.color));
                tris.push_back(v);
            }
            continue;
        }
        const int mesh = psi_gfx_entity_mesh(call.glist);
        if (mesh < 0 || mesh >= int(meshes_.size())) continue;
        // Um modelo com skinning pedido por psiDrawObjectMatrix (no PS2 a cadeia leria um buffer de
        // saída que não existe) ou o contrário não é desenhado.
        const bool skinned = call.palette >= 0;
        if (skinned != meshes_[size_t(mesh)].skinned) continue;
        out.push_back({mesh, to_array(call.world), ObjectKind::Visible, Blend(call.blend & 3)});
        // psiSetTweakARGB: tweak = a, r, g, b; 255 = 1.0 (TAmbient do PS2).
        out.back().tint = {call.tweak[1] / 255.0f, call.tweak[2] / 255.0f, call.tweak[3] / 255.0f,
                           call.tweak[0] / 255.0f};
        out.back().gs = gs_of(call);
        out.back().interp = interp_of(call, skinned);
        set_vu1_state(call, skinned, out.back());
        if (skinned) {
            const size_t first = size_t(call.palette);
            const size_t n = std::min<size_t>(call.bones, g_psi_frame.palettes.size() - std::min(first, g_psi_frame.palettes.size()));
            std::vector<std::array<float, 16>>& palette = out.back().palette;
            palette.resize(n);
            for (size_t b = 0; b < n; ++b) std::memcpy(palette[b].data(), g_psi_frame.palettes[first + b].m, 64);
            if (call.morph >= 0 && size_t(call.morph) + kPsiMorphs <= g_psi_frame.morphs.size() &&
                meshes_[size_t(mesh)].morph_targets > 0) {
                const float* w = g_psi_frame.morphs.data() + call.morph;
                out.back().morph.assign(w, w + std::min<int>(kPsiMorphs, meshes_[size_t(mesh)].morph_targets));
            }
        }
    }
    add_frame_markers(g_psi_frame, g_psi_frame.draws.size(), out);
    // Os provisórios são opacos: entram antes do primeiro item misturado (e antes da névoa e da limpeza do Z), para
    // que os transparentes e a arma em primeira pessoa fiquem por cima deles. Sem luzes nem tweak.
    std::vector<DrawItem> provisional;
    for (const ProvisionalObject& obj : g_provisional_objects) {
        const int mesh = rigid_mesh(obj.glist);
        if (mesh < 0) continue;
        provisional.push_back({mesh, to_array(obj.world), classify_object(obj.flags)});
        if (camera != nullptr) {
            for (int k = 0; k < 3; ++k) {
                provisional.back().env_s[k] = camera->camera.m[0][k];
                provisional.back().env_t[k] = camera->camera.m[1][k];
            }
        }
    }
    out.insert(provisional_insert_point(out), provisional.begin(), provisional.end());
}

void RuntimeGfx::collect_2d(const Psi2DQuad* quads, uint32_t count, std::vector<DrawItem>& out) const {
    add_2d(quads, count, renderer_.width(), renderer_.height(), out);
}

void RuntimeGfx::suggested_view(const std::vector<DrawItem>& items, float eye[3], float target[3]) const {
    std::vector<WorldBox> boxes;
    for (const DrawItem& it : items) {
        if (it.kind != ObjectKind::Visible || it.mesh < 0 || it.mesh >= int(meshes_.size())) continue;
        const MeshInfo& m = meshes_[size_t(it.mesh)];
        boxes.push_back(transform_box(m.bmin, m.bmax, it.world));
    }
    suggest_view(boxes, eye, target);
}

size_t RuntimeGfx::triangles(const std::vector<DrawItem>& items) const {
    size_t n = 0;
    for (const DrawItem& it : items) {
        if (it.kind != ObjectKind::Volume && it.mesh >= 0 && it.mesh < int(meshes_.size())) {
            n += meshes_[size_t(it.mesh)].triangles;
        }
    }
    return n;
}

void describe_faces(const std::vector<DrawItem>& items, const float eye[3], std::string* line, std::string* columns) {
    struct Face {
        const DrawItem* item;
        const obj_tag* obj;
        float dist;
        bool weighted;
    };
    std::vector<Face> faces;
    for (const DrawItem& d : items) {
        if (d.mesh < 0 || d.palette.empty() || d.interp.owner == nullptr ||
            d.interp.owner_kind != uint8_t(PsiOwnerKind::Object)) {
            continue;
        }
        const auto* o = static_cast<const obj_tag*>(d.interp.owner);
        const auto* a = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(o->sphere_src));
        if (a == nullptr || a->skin == nullptr || a->skin->n_morphs == 0) continue;
        bool any = false;
        for (const float w : d.morph) any = any || w != 0.0f;
        const float dx = o->pos.x - eye[0], dy = o->pos.y - eye[1], dz = o->pos.z - eye[2];
        faces.push_back({&d, o, std::sqrt(dx * dx + dy * dy + dz * dz), any});
    }
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.dist < b.dist; });
    int weighted = 0;
    for (const Face& f : faces) weighted += f.weighted ? 1 : 0;
    char buf[2048];
    int n = std::snprintf(buf, sizeof(buf), "rosto desenhados %zu com_pesos %d", faces.size(), weighted);
    for (size_t i = 0; i < faces.size() && i < 4 && n < int(sizeof(buf)) - 256; ++i) {
        const Face& f = faces[i];
        const auto* a = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(f.obj->sphere_src));
        int tags[3] = {0, 0, 0};
        for (const ANIMSCRIPT_tag* s = a->scripts; s != nullptr; s = s->next) {
            if (s->tag >= 0xe0000000u && s->tag <= 0xe0000002u) ++tags[s->tag - 0xe0000000u];
        }
        n += std::snprintf(buf + n, sizeof(buf) - size_t(n),
                           " | dist %.2f pos %.2f %.2f %.2f giro %.2f tipo %u pele %08x estado %u timer %d olhos %u %u "
                           "cabeca %x %.3f %.3f scripts %d/%d/%d pesos",
                           double(f.dist), double(f.obj->pos.x), double(f.obj->pos.y), double(f.obj->pos.z),
                           double(f.obj->rot.y), unsigned(f.obj->type), unsigned(a->skin->hash),
                           unsigned(a->face_state), int(a->face_timer), unsigned(a->eye_x), unsigned(a->eye_y),
                           unsigned((a->anim_flags >> 4) & 7), double(a->head_yaw), double(a->head_pitch), tags[0],
                           tags[1], tags[2]);
        for (size_t k = 0; k < f.item->morph.size() && n < int(sizeof(buf)) - 16; ++k) {
            if (std::fabs(f.item->morph[k]) > 0.005f) {
                n += std::snprintf(buf + n, sizeof(buf) - size_t(n), " %zu:%.3f", k, double(f.item->morph[k]));
            }
        }
    }
    if (line != nullptr) *line = buf;
    if (columns == nullptr) return;
    // As colunas: o rosto com pesos mais perto (o que fala, numa cena), ou o mais perto; com NF_FACE_SKIN=<pele em
    // hexa>, o mais perto dessa pele (para acompanhar um personagem só).
    const Face* best = nullptr;
    const char* want = std::getenv("NF_FACE_SKIN");
    const uint32_t want_skin = want != nullptr ? uint32_t(std::strtoul(want, nullptr, 16)) : 0;
    for (const Face& f : faces) {
        const auto* a = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(f.obj->sphere_src));
        if (want_skin != 0 ? a->skin->hash == want_skin : f.weighted) {
            best = &f;
            break;
        }
    }
    if (best == nullptr && !faces.empty() && want_skin == 0) best = &faces[0];
    if (best == nullptr) {
        *columns = "\t\t\t";
        return;
    }
    const auto* a = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(best->obj->sphere_src));
    char col[512];
    int m = std::snprintf(col, sizeof(col), "%.3f\t%08x\t%u\t", double(best->dist), unsigned(a->skin->hash),
                          unsigned(a->face_state));
    for (int k = 0; k < kMaxMorphs && m < int(sizeof(col)) - 12; ++k) {
        const float w = size_t(k) < best->item->morph.size() ? best->item->morph[size_t(k)] : 0.0f;
        m += std::snprintf(col + m, sizeof(col) - size_t(m), "%s%.4f", k != 0 ? "," : "", double(w));
    }
    *columns = col;
}

}  // namespace nf
