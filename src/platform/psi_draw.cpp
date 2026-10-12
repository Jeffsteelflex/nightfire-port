// Desenho de cada quadro para PC: registra o que o Game_Draw pede. Ver psi_draw.h.
//
// Divergências em psiDrawSkinObjectMatrix (ver a função): o recorte usa os lados do frustum da câmera
// em vez das matrizes de recorte da VU0; ponteiros nulos que o original leria são testados antes; o cache de
// skinning não tem o limite de 128 entradas. Os morphs (bloco 0x2b) são somados na GPU, não na VU0.
// Divergências em psiDrawParticleList (ver a função): o tamanho do sprite vira um tamanho no mundo
// (o do PS2 a 60 graus) e o sprite fica quadrado na tela (no PS2, quadrado em pixels do GS, que não
// são quadrados); sem textura (pstex_tag nulo) o PC não desenha com a textura anterior do GS, e sim
// sem textura; o recorte pelo centro do sprite é feito pelo renderizador.
// Divergência em psiDrawObjectMatrix (AlphaFogObjects): o recorte da caixa 0 usa o volume da câmera
// (lados pelo campo de visão do viewer_tag, perto em Ps2Near2) em vez das matrizes de recorte da VU0, e
// vale para o objeto inteiro (no PS2, a bandeira de recorte é a da primeira folha desenhada).

#include "platform/psi_draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/game.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/object.h"
#include "engine/view.h"
#include "platform/psi_loading.h"
#include "platform/psi_movie.h"
#include "platform/vu1.h"
#include "platform/fmath.h"

PsiFrame g_psi_frame;
static_assert(kPsiMorphs == kMaxMorphs, "os pesos registrados por desenho são os Morph_Weight do motor");

uint8_t FogOn = 0;
uint8_t AlphaFogObjects = 0;
uint32_t ScreenBlur = 0x80;
uint8_t TweakRGBA = 0;
uint8_t olda = 0xff, oldr = 0xff, oldg = 0xff, oldb = 0xff;
float TAmbient[4] = {1.0f, 1.0f, 1.0f, 1.0f};
uint8_t WipeZBuffer = 1;
uint32_t PS2NumLights = 0;
float TLight[2][4] = {};
float TColor0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
float TColor1[4] = {0.0f, 0.0f, 0.0f, 1.0f};
uint8_t PS2FogR = 0, PS2FogG = 0, PS2FogB = 0;
float Ps2Far1 = 4194240.0f;
float PS2SnowSize = 10000.0f;
float PS2ParticleSize = 4194240.0f * 0.002386635f;

namespace {
const viewer_tag* current_camera = nullptr;
uint8_t current_blend = 2;
uint8_t after_solid = 0;
uint8_t alpha_phase = 0;  // só no PC: entre psiStartAlpha e psiEndAlpha, com ou sem névoa
// TEST_1 e ZBUF_1 escritos por psiSwitchZBufferRead/Write neste quadro (bit 1 e bit 2 de gs_has).
uint8_t gs_has = 0;
uint64_t gs_test = 0;
uint64_t gs_zbuf = 0;
// Só do port: o dono dos desenhos (psi_draw_set_owner) e a contagem dentro dele.
const void* owner_ptr = nullptr;
PsiOwnerKind owner_kind = PsiOwnerKind::None;
int8_t owner_list = -1;
uint16_t owner_seq = 0;
constexpr uint64_t ZBufBase = 0;  // endereço do buffer de Z no GS (só a máscara de escrita importa aqui)

// Constantes do PS2 (.sdata) para o tamanho dos sprites (as da projeção estão em vu1.h). psiPreGameRun
// troca Ps2Far1 e PS2SnowSize por fase; no caminho com cores Ps2Far1 se cancela (PS2ParticleSize =
// Ps2Far1 * 0.002386635), no sem cores o tamanho é o da neve padrão.
constexpr float Ps2Near1 = vu1::kPs2Near1;
constexpr float Ps2Near2 = vu1::kPs2Near2;
constexpr float Ps2Far2 = vu1::kPs2Far2;
constexpr float kPs2HalfHeight = 224.0f;      // metade dos 448 pixels da tela do PS2
constexpr float kPs2CotHalfFov = 1.7320508f;  // Camera_CalcViewAngles: 60 graus na vertical

// SkinCache (0x00246350): no PS2, 128 entradas de 0x18 bytes {cadeia de DMA da caixa, cópia dela,
// entrada do SkinIt, buffer de saída, objeto, estado}. O CacheSkin cria a entrada com estado 3 e o
// AgeSkinCache (chamado por psiPreDraw) desconta 1 a cada quadro e libera ao chegar a 0;
// psiDrawSkinObjectMatrix só reaproveita entradas de estado 3, ou seja, do próprio quadro. No PC a
// "cadeia já transformada" é o desenho registrado: o modelo e a cópia da paleta.
struct SkinCacheEntry {
    const obj_tag* obj;
    const celglist_tag* glist;
    int32_t palette;
    uint16_t bones;
    int32_t morph;  // pesos em g_psi_frame.morphs (-1: sem morph)
};
std::vector<SkinCacheEntry> skin_cache;

template <typename T>
T* ptr_at(const void* base, size_t offset) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, 4);
    return nf::p32_ptr<T>(v);
}

// Recorte de psiDrawSkinObjectMatrix. O PS2 (CheckObjectClip, 0x00121a60) leva os 8 cantos do cubo
// centro ± raio pelas matrizes de recorte da câmera (0x00244540 e 0x00244580, montadas pelo código de
// câmera do PS2, ainda não portado) e usa o VCLIP: -1 se todos os cantos estão fora de um mesmo
// plano, 1 se algum canto sai do volume de desenho, 0 se o cubo está inteiro dentro. No PC: o mesmo
// cubo contra os quatro lados do frustum da câmera em uso (viewer_tag::planes, os de Vision_InView),
// sem os planos de perto e de longe; 0 e 1 não mudam nada aqui (no PS2 escolhem o microprograma com
// ou sem recorte). Sem câmera (testes), nada é recortado.
int32_t skin_clip(const _VECTOR& s, const viewer_tag* camera) {
    if (camera == nullptr) return 1;
    for (const plane_equ_tag& p : camera->planes) {
        const float d = s.x * p.a + s.y * p.b + s.z * p.c + p.d;
        const float extent = s.w * (std::fabs(p.a) + std::fabs(p.b) + std::fabs(p.c));
        if (d < -extent) return -1;
    }
    return 1;
}

PsiDrawCall make_call(const celglist_tag* glist, const MATRIX& world, uint32_t created) {
    PsiDrawCall call;
    call.world = world;
    call.glist = glist;
    call.camera = current_camera;
    call.created = created;
    call.blend = current_blend;
    call.alpha_pass = alpha_phase;
    call.after_solid = after_solid;
    call.tweak[0] = olda;
    call.tweak[1] = oldr;
    call.tweak[2] = oldg;
    call.tweak[3] = oldb;
    call.num_lights = uint8_t(PS2NumLights);
    std::memcpy(call.light_pos, TLight, sizeof(call.light_pos));
    std::memcpy(call.light_color[0], TColor0, sizeof(TColor0));
    std::memcpy(call.light_color[1], TColor1, sizeof(TColor1));
    call.gs_has = gs_has;
    call.gs_test = gs_test;
    call.gs_zbuf = gs_zbuf;
    call.owner = owner_ptr;
    call.owner_kind = owner_kind;
    call.list = owner_list;
    call.owner_seq = owner_seq;
    if (owner_ptr != nullptr) ++owner_seq;
    return call;
}
}  // namespace

void psi_draw_debug_power_on() {
    g_psi_frame = PsiFrame{};
    skin_cache.clear();
    FogOn = 0;
    AlphaFogObjects = 0;
    ScreenBlur = 0x80;
    TweakRGBA = 0;
    olda = oldr = oldg = oldb = 0xff;
    for (float& c : TAmbient) c = 1.0f;
    WipeZBuffer = 1;
    current_camera = nullptr;
    current_blend = 2;
    after_solid = 0;
    alpha_phase = 0;
    gs_has = 0;
    gs_test = gs_zbuf = 0;
    owner_ptr = nullptr;
    owner_kind = PsiOwnerKind::None;
    owner_list = -1;
    owner_seq = 0;
}

void psi_draw_set_owner(const void* owner, PsiOwnerKind kind, int8_t list) {
    if (owner != owner_ptr || kind != owner_kind) owner_seq = 0;
    owner_ptr = owner;
    owner_kind = owner == nullptr ? PsiOwnerKind::None : kind;
    owner_list = list;
}

const void* psi_draw_owner() { return owner_ptr; }
PsiOwnerKind psi_draw_owner_kind() { return owner_kind; }
int8_t psi_draw_list() { return owner_list; }

// [decomp] 0x00121590 psiPreDraw
// No PS2: zera as estatísticas, envelhece o cache de skinning (AgeSkinCache), abre o pacote de DMA
// do quadro, limpa o Z e manda os registradores iniciais; com PS2CacheLoadingBlobs, guarda a textura das bolinhas
// da tela de carga (CacheLoadingBlobs). No PC: começa um registro novo (as entradas
// do cache deixam de valer, como as de estado 3 que viram 2).
void psiPreDraw() {
    g_psi_frame.draws.clear();
    g_psi_frame.palettes.clear();
    g_psi_frame.morphs.clear();
    g_psi_frame.particles.clear();
    g_psi_frame.particle_lists.clear();
    g_psi_frame.tri_vertices.clear();
    g_psi_frame.tri_lists.clear();
    g_psi_frame.quads2d.clear();
    g_psi_frame.lists2d.clear();
    skin_cache.clear();
    g_psi_frame.clear_z = 0;
    g_psi_frame.clear_z_at.clear();
    g_psi_frame.cameras = 0;
    g_psi_frame.fade_camera = nullptr;
    std::memset(g_psi_frame.fade, 0, sizeof(g_psi_frame.fade));
    g_psi_frame.fades = 0;
    g_psi_frame.screen_blur = ScreenBlur;  // CreateSqueezeFrame lê ScreenBlur aqui
    g_psi_frame.finished = false;
    g_psi_frame.fog_at = -1;
    current_camera = nullptr;
    current_blend = 2;
    after_solid = 0;
    alpha_phase = 0;
    gs_has = 0;
    gs_test = gs_zbuf = 0;
    owner_ptr = nullptr;
    owner_kind = PsiOwnerKind::None;
    owner_list = -1;
    owner_seq = 0;
    psiClearZ();
    // A textura das bolinhas da tela de carga (ResetMap_GenLoadScreen pede no primeiro dos seus quadros).
    if (PS2CacheLoadingBlobs != 0) {
        CacheLoadingBlobs();
        PS2CacheLoadingBlobs = 0;
    }
}

// No PS2: fecha o pacote de DMA e atualiza o som (SFXUpdate).
void psiPostDraw() { g_psi_frame.finished = true; }

// [decomp] 0x0010b960 psiDrawFogPass
// No PS2: com a névoa ligada (e sem filme de fundo), os pacotes RGB2APacket (CreateMoveRG2BA: os bits
// 8-15 do Z de cada pixel vão para o alfa do quadro) e FogOverlayPacket (CreateFogSprite: sprites com a
// cor da névoa, Z 0x7fff e teste GREATER, misturados por (Cd - Cs) * Ad + Cs). No PC: o registro marca
// onde a passada acontece, com a cor e o Ps2Far1 em vigor; o renderizador a faz com o Z de cada pixel
// (vu1::fog_keep). Com um filme tocando (BGFMVPlaying) não há névoa: a imagem dele é o fundo do quadro.
void psiDrawFogPass() {
    if (FogOn == 0 || BGFMVPlaying != 0) return;
    g_psi_frame.fog_at = int32_t(g_psi_frame.draws.size());
    g_psi_frame.fog_rgb[0] = PS2FogR;
    g_psi_frame.fog_rgb[1] = PS2FogG;
    g_psi_frame.fog_rgb[2] = PS2FogB;
    g_psi_frame.fog_far1 = Ps2Far1;
}

// [decomp] 0x00121630 psiPostSolid
// A névoa sobre o que já foi desenhado; no PC também marca os desenhos seguintes (after_solid).
void psiPostSolid() {
    psiDrawFogPass();
    after_solid = 1;
}

// No PS2: manda a matriz de visão da câmera para a VU1 (PS2WorldViewVU1Kick2) e o recorte da tela
// (viewport). No PC: as próximas chamadas são desta câmera.
void psiUseCamera(viewer_tag* viewer, uint16_t) {
    current_camera = viewer;
    ++g_psi_frame.cameras;
}

// [decomp] 0x0010b958 psiEndUseCamera
void psiEndUseCamera(viewer_tag*) {}

namespace {
// CheckObjectClipBox da caixa 0 (mínimo, máximo no espaço do objeto) com a câmera em uso: -1 se os 8
// cantos ficam fora de um mesmo lado do volume da tela, 1 se algum canto sai do volume dobrado em x e y
// (ou fica antes do plano de perto), 0 se a caixa está inteira dentro. No PS2 os volumes vêm das
// matrizes de recorte de PS2WorldViewVU1Kick2 (0x244540 e 0x244580); aqui, do campo de visão e da
// proporção do viewer_tag, no espaço da câmera.
int32_t box_clip(const float* box, const MATRIX& m, const viewer_tag& cam) {
    const float ty = nf::fm::tan(cam.fov * 0.5f);
    const float tx = ty * cam.aspect;
    uint32_t all_out = 0x1f, any_out2 = 0;
    for (int k = 0; k < 8; ++k) {
        const float c[3] = {box[(k & 1) ? 3 : 0], box[(k & 2) ? 4 : 1], box[(k & 4) ? 5 : 2]};
        float w[3];  // canto no mundo, relativo à câmera
        for (int j = 0; j < 3; ++j) {
            w[j] = c[0] * m.m[0][j] + c[1] * m.m[1][j] + c[2] * m.m[2][j] + m.m[3][j] - cam.camera.m[3][j];
        }
        float v[3];  // no espaço da câmera: lado, cima, frente
        for (int r = 0; r < 3; ++r) {
            v[r] = w[0] * cam.camera.m[r][0] + w[1] * cam.camera.m[r][1] + w[2] * cam.camera.m[r][2];
        }
        uint32_t out = 0;
        if (v[0] > v[2] * tx) out |= 1;
        if (v[0] < -v[2] * tx) out |= 2;
        if (v[1] > v[2] * ty) out |= 4;
        if (v[1] < -v[2] * ty) out |= 8;
        if (v[2] < vu1::kPs2Near2) out |= 0x10;
        all_out &= out;
        if (std::fabs(v[0]) > 2.0f * v[2] * tx || std::fabs(v[1]) > 2.0f * v[2] * ty || v[2] < vu1::kPs2Near2) {
            any_out2 = 1;
        }
    }
    if (all_out != 0) return -1;
    return int32_t(any_out2);
}

// O trecho de FillMatrixChainRot com AlphaFogObjects (lista transparente com a névoa ligada): o alfa do
// tweak cai com a distância do centro da caixa 0, como a névoa faria (vu1::alpha_fog); caixas que
// precisam de recorte ficam com o alfa que tinham. Como no PS2, psiSetTweakA muda olda, e o próximo
// desenho parte do valor já reduzido se ninguém trocar o tweak.
void alpha_fog_objects(const celglist_tag* glist, const MATRIX& m) {
    if (AlphaFogObjects == 0 || current_camera == nullptr) return;
    const float* box = ptr_at<const float>(glist->gfx->ps2_gfx, 4);
    if (box == nullptr) return;
    const int32_t clip = box_clip(box, m, *current_camera);
    if (clip < 0) return;  // RecurseAndDrawBoxes não desenha nada: FillMatrixChainRot não roda
    if (clip == 1) {
        psiSetTweakA(olda);
        return;
    }
    const float c[3] = {(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f};
    float z = 0.0f;
    for (int j = 0; j < 3; ++j) {
        const float w = c[0] * m.m[0][j] + c[1] * m.m[1][j] + c[2] * m.m[2][j] + m.m[3][j];
        z += (w - current_camera->camera.m[3][j]) * current_camera->camera.m[2][j];
    }
    psiSetTweakA(vu1::alpha_fog(vu1::screen_z(z, Ps2Far1), olda));
}
}  // namespace

// [decomp] 0x00124708 psiDrawObjectMatrix
// Como no PS2, só desenha entidades com geometria (bloco 0x2d) e força a coluna w da matriz de quem
// chamou para (0, 0, 0, 1). O desenho em si (RecurseAndDrawBoxes) vira um item do registro, com o
// estado que FillMatrixChainRot mandaria à VU1 (o alfa da névoa dos transparentes muda o tweak antes).
void psiDrawObjectMatrix(celglist_tag* glist, MATRIX* m, uint32_t created) {
    if (glist->gfx == nullptr || glist->gfx->ps2_gfx == nullptr) return;
    m->m[0][3] = 0.0f;
    m->m[3][3] = 1.0f;
    m->m[1][3] = 0.0f;
    m->m[2][3] = 0.0f;
    alpha_fog_objects(glist, *m);
    g_psi_frame.draws.push_back(make_call(glist, *m, created));
}

// [decomp] 0x00111828 psiDrawSkinObjectMatrix
// No PS2: recorta pela esfera do objeto; se o objeto já foi transformado neste quadro (SkinCache),
// desenha de novo a cadeia guardada; senão copia a cadeia da primeira caixa (CacheSkin), leva à VU0
// as matrizes dos ossos da lista da malha (slot k = palette[lista[k]], linhas espaçadas de 64
// quadwords em 0x11004000), roda o SkinIt e desenha (psiDrawClonedObject). A matriz recebida é
// trocada pela identidade: a paleta já está no mundo.
// No PC: o skinning é feito na GPU; o desenho registrado guarda o modelo e uma cópia de
// palette[0 .. maior osso da lista] em g_psi_frame.palettes.
// Comportamento original preservado: a busca no cache é só pelo objeto, então um segundo modelo com
// skinning do mesmo objeto no mesmo quadro desenharia de novo o primeiro, com a paleta da primeira
// chamada (nenhum skin do disco lista mais de uma malha; uma segunda câmera reaproveita o mesmo).
// Divergências: recorte pelos lados do frustum (skin_clip); obj nulo, bloco 0x2d ausente ou modelo
// sem skinning saem antes (o original leria endereços baixos); o cache não tem o limite de 128 entradas (o
// CacheSkin devolveria -1 e o original leria fora da tabela).
// Morphs: como no PS2, valem com a malha com bloco 0x2b (entgfx_tag::morph), Morph_Count != 0 e, fora de cena
// (ScriptCam 0), o menor byte de sphere_src + 0x110..0x113 até 2; o SkinIt os soma antes do skinning. No PC os
// pesos (Morph_Weight[k] no alvo Morph_Index[k]) vão para g_psi_frame.morphs e o vertex shader faz a soma; uma
// entrada do cache leva os pesos da primeira chamada, como a cadeia já transformada.
void psiDrawSkinObjectMatrix(celglist_tag* glist, MATRIX*, obj_tag* obj, MATRIX* palette) {
    if (obj == nullptr) return;
    const int32_t clip = skin_clip(obj->sphere, current_camera);
    if (clip == -1) return;
    if (obj->sphere_src == nullptr || glist == nullptr) return;
    const entgfx_tag* gfx = glist->gfx;
    if (gfx == nullptr || gfx->ps2_gfx == nullptr) return;
    const uint8_t* hdr = gfx->ps2_gfx;  // {quantidade, caixas, extra}, já com ponteiros
    const uint8_t* boxes = ptr_at<const uint8_t>(hdr, 4);
    if (boxes == nullptr || ptr_at<const uint8_t>(boxes, 0x20) == nullptr) return;

    const SkinCacheEntry* cached = nullptr;
    for (const SkinCacheEntry& e : skin_cache) {
        if (e.obj == obj) {
            cached = &e;
            break;
        }
    }
    // CacheAndFillNewGlistTextures(lista de texturas da caixa): no PC as texturas já estão na GPU.
    if (ptr_at<const uint8_t>(boxes, 0x24) == nullptr) return;

    MATRIX identity{};
    for (int k = 0; k < 4; ++k) identity.m[k][k] = 1.0f;
    PsiDrawCall call = make_call(glist, identity, obj->created);
    call.obj = obj;
    if (cached != nullptr) {
        call.glist = cached->glist;
        call.palette = cached->palette;
        call.bones = cached->bones;
        call.morph = cached->morph;
        g_psi_frame.draws.push_back(call);
        return;
    }

    const uint8_t* info = ptr_at<const uint8_t>(hdr, 8);  // SkinInfo
    if (info == nullptr || palette == nullptr) return;
    const uint8_t* list = ptr_at<const uint8_t>(info, 0xc);
    if (list == nullptr) return;
    uint16_t bones = 0;
    for (const uint8_t* p = list; *p != 0xff; ++p) bones = std::max<uint16_t>(bones, uint16_t(*p + 1));
    if (bones == 0) return;
    const int32_t first = int32_t(g_psi_frame.palettes.size());
    g_psi_frame.palettes.insert(g_psi_frame.palettes.end(), palette, palette + bones);
    // Os morphs: fora de cena, o menor byte de closeness (as quatro câmeras) não pode passar de 2.
    float closeness = 0.0f;
    if (ScriptCam == 0) {
        const uint8_t* c = obj->sphere_src + 0x110;
        uint8_t m = c[0];
        for (int i = 1; i < 4; ++i) m = c[i] < m ? c[i] : m;
        closeness = float(m);
    }
    int32_t morph = -1;
    if (gfx->morph != nullptr && Morph_Count != 0 && !(2.0f < closeness)) {
        morph = int32_t(g_psi_frame.morphs.size());
        g_psi_frame.morphs.resize(g_psi_frame.morphs.size() + kPsiMorphs, 0.0f);
        for (uint32_t k = 0; k < Morph_Count && k < uint32_t(kPsiMorphs); ++k) {
            if (Morph_Index[k] < kPsiMorphs) g_psi_frame.morphs[size_t(morph) + Morph_Index[k]] = Morph_Weight[k];
        }
    }
    skin_cache.push_back({obj, glist, first, bones, morph});
    call.palette = first;
    call.bones = bones;
    call.morph = morph;
    g_psi_frame.draws.push_back(call);
}

// [decomp] 0x00124770 psiSetTweakARGB
void psiSetTweakARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    oldg = g;
    oldr = r;
    oldb = b;
    olda = a;
    TweakRGBA = (a == 0xff && r == 0xff && g == 0xff && b == 0xff) ? 0 : 1;
    TAmbient[3] = float(a) * 0.003921569f;
    TAmbient[0] = float(r) * 0.003921569f;
    TAmbient[1] = float(g) * 0.003921569f;
    TAmbient[2] = float(b) * 0.003921569f;
}

// [decomp] 0x00124878 psiSetTweakA
// Só o alfa do tweak (TAmbient[3]); olda passa a ser o novo valor.
void psiSetTweakA(uint8_t a) {
    olda = a;
    TweakRGBA = (a == 0xff && oldr == 0xff && oldg == 0xff && oldb == 0xff) ? 0 : 1;
    TAmbient[3] = float(a) * 0.003921569f;
}

// No PS2: escreve o registrador ALPHA do GS (0x48, 0x44, 0x2a ou 0x42); outros valores não mudam
// nada.
void psiSetUpColourBlend(int32_t mode) {
    if (mode >= 0 && mode <= 3) current_blend = uint8_t(mode);
}

// [decomp] 0x00124900 psiStartAlpha
void psiStartAlpha() {
    if (FogOn != 0) AlphaFogObjects = 1;
    alpha_phase = 1;
}

// [decomp] 0x00124920 psiEndAlpha
void psiEndAlpha() {
    AlphaFogObjects = 0;
    alpha_phase = 0;
}

// [decomp] 0x0010e788 psiLight_Create
uint32_t psiLight_Create(light_tag*) { return 0; }

// [decomp] 0x0010e790 psiLight_Delete
void psiLight_Delete(uint32_t) {}

// [decomp] 0x0010e588 psiLight_SetLights
// As (no máximo) duas primeiras luzes da lista vão para a VU1: posição (w = 1) em TLight e cor em
// TColor0/TColor1 (cor da luz x intensidade x o multiplicador do canal; w = 1 / raio²). As cores
// começam zeradas (w 1) e PS2NumLights conta as luzes carregadas. Cada desenho registrado guarda a
// cópia em vigor.
void psiLight_SetLights(P32<light_tag>* lights, uint16_t count, float r, float g, float b) {
    PS2NumLights = 0;
    for (int i = 0; i < 3; ++i) {
        TColor0[i] = 0.0f;
        TColor1[i] = 0.0f;
    }
    TColor0[3] = 1.0f;
    TColor1[3] = 1.0f;
    if (lights == nullptr || count == 0) return;
    for (int i = 0; i < 2 && i < int(count); ++i) {
        const light_tag* l = lights[i];
        TLight[i][0] = l->pos.x;
        TLight[i][1] = l->pos.y;
        TLight[i][2] = l->pos.z;
        TLight[i][3] = 1.0f;
        float* c = i == 0 ? TColor0 : TColor1;
        c[0] = float(l->r) * l->intensity * r;
        c[1] = float(l->g) * l->intensity * g;
        c[2] = float(l->b) * l->intensity * b;
        c[3] = 1.0f / (l->radius * l->radius);
        ++PS2NumLights;
    }
}

// [decomp] 0x0011e2e8 psiPreGameRun
// A névoa e o plano de longe de cada fase (as de neve também mudam o tamanho dos flocos). No PS2 ela
// também inicia os sons, limpa o cache de skinning, monta os sprites de névoa e de limpar o Z e
// empacota as texturas na VRAM (CreateSpaceAndPackTextures, que no fim acha as texturas de fonte); no
// PC só o cache de skinning e as fontes (o renderizador cuida do resto).
void psiPreGameRun() {
    skin_cache.clear();
    psi_2d_find_font_textures();
    bool fog = true;
    switch (GameState.level_file) {
        case 0x7000002:
            Ps2Far1 = 9987520.0f;
            PS2FogR = 0x24, PS2FogG = 0x2e, PS2FogB = 0x4c;
            break;
        case 0x7000004:
            Ps2Far1 = 9379776.0f;
            PS2FogR = 0x11, PS2FogG = 0x15, PS2FogB = 0x21;
            break;
        case 0x7000005:
            Ps2Far1 = 19118528.0f;
            PS2SnowSize = 28990.0f;
            PS2FogR = 0x3a, PS2FogG = 0x4d, PS2FogB = 0x68;
            break;
        case 0x7000006:
            Ps2Far1 = 8059232.0f;
            PS2SnowSize = 12826.0f;
            PS2FogR = 0x2e, PS2FogG = 0x3c, PS2FogB = 0x52;
            break;
        case 0x7000008:
            Ps2Far1 = 13191350.0f;
            PS2SnowSize = 19053.0f;
            PS2FogR = 0x3b, PS2FogG = 0x50, PS2FogB = 0x69;
            break;
        case 0x700000c:
            Ps2Far1 = 20475840.0f;
            PS2FogR = 0xd1, PS2FogG = 0xd5, PS2FogB = 0xd6;
            break;
        case 0x700000d:
            Ps2Far1 = 19707840.0f;
            PS2FogR = 0x92, PS2FogG = 0x85, PS2FogB = 0x69;
            break;
        default:
            Ps2Far1 = 4194240.0f;
            fog = false;
            break;
    }
    FogOn = fog ? 1 : 0;
    PS2ParticleSize = Ps2Far1 * 0.002386635f;
}

// [decomp] 0x00121578 psiFog
// Vazia no PS2 (a névoa é configurada em psiPreGameRun).
void psiFog(int8_t, float, float, uint8_t, uint8_t, uint8_t) {}

// [decomp] 0x0011ca98 psiSetScreenBlur
// O rastro da tela: ScreenBlur = 0x80 - blur / 4, o FIX do ALPHA_1 com que o CreateSqueezeFrame do próximo psiPreDraw
// copia o quadro desenhado sobre o que está na tela ((novo - tela) x ScreenBlur / 128 + tela). No PC o psiPreDraw
// guarda o valor em g_psi_frame.screen_blur e o main faz a mistura (Renderer::screen_blur).
void psiSetScreenBlur(uint8_t blur) { ScreenBlur = 0x80u - (uint32_t(blur) >> 2); }

// [decomp] 0x0010b9a0 psiClearZ
// No PS2: com WipeZBuffer ligado (o valor inicial; nada no ELF o muda), manda o ZClearPacket de
// CreateZClearSprite: TEST_1 = 0x32001 (alfa NEVER com AFAIL = só Z, ZTST ALWAYS), ZBUF_1 com escrita e um
// SPRITE da tela inteira (PS2OffX/Y, 512 x PS2ScreenHeight) com Z = 1, o mais longe para o teste GEQUAL: o Z
// fica limpo e a cor intacta. No PC: o registro marca onde a limpeza cai (clear_z_at), e o renderizador limpa a
// profundidade ali (render/frame_markers.h).
void psiClearZ() {
    if (WipeZBuffer == 0) return;
    ++g_psi_frame.clear_z;
    g_psi_frame.clear_z_at.push_back(int32_t(g_psi_frame.draws.size()));
}

// [decomp] 0x00124608 psiDrawParticles
// Vazia no PS2.
void psiDrawParticles(uint32_t) {}

// [decomp] 0x00111288 psiCreateParticles
// No PS2 a função é vazia (jr ra): quem chama recebe o que estava em v0. No PC, 0.
uint32_t psiCreateParticles(uint32_t, _VECTOR*, plane_equ_tag*) { return 0; }

// [decomp] 0x00111290 psiParticleUpdate
// Vazia no PS2 (v0 do chamador); no PC, 0: o objeto de Particles_Update some.
uint32_t psiParticleUpdate(uint32_t) { return 0; }

// [decomp] 0x00121428 psiSwitchZBufferRead
// TEST_1 (0x47): 0 liga o teste de Z (GEQUAL) com alfa > 0 (0x5000d); 1 desliga o teste de Z (0xd).
void psiSwitchZBufferRead(int8_t mode) {
    if (mode == 0) {
        gs_test = 0x5000d;
    } else if (mode == 1) {
        gs_test = 0xd;
    } else {
        return;
    }
    gs_has |= 2;
}

// [decomp] 0x00121480 psiSwitchZBufferWrite
// ZBUF_1 (0x4e): 0 escreve no Z; 1 liga ZMSK (não escreve).
void psiSwitchZBufferWrite(int8_t mode) {
    if (mode == 0) {
        gs_zbuf = ZBufBase | 0x31000000;
    } else if (mode == 1) {
        gs_zbuf = ZBufBase | 0x131000000ull;
    } else {
        return;
    }
    gs_has |= 4;
}

// No PS2 o microprograma de partículas leva o centro à tela (Z do GS = A / z + B, com
// A = Ps2Near2 * (Ps2Far1 - Ps2Near1) * Ps2Far2 / (Ps2Far2 - Ps2Near2)) e soma +- Z * q ao x e ao y
// do sprite, em pixels; q = 4 * size / (PS2ParticleSize * PS2SnowSizeMult) com cores e
// 1 / (PS2SnowSize * PS2SnowSizeMult) sem (PS2SnowSizeMult desfaz o zoom e a altura da janela). Um
// objeto de meia altura h a z metros mede 224 * cot(30 graus) * h / z pixels na tela de 448 linhas;
// igualando, h = A * q / (224 * cot(30 graus)) (cerca de 0.432 * size com cores). O termo B * q
// (menos de 0.01 pixel) fica de fora.
float psi_particle_half_size(float size, bool with_colours) {
    const float A = Ps2Near2 * (Ps2Far1 - Ps2Near1) * Ps2Far2 / (Ps2Far2 - Ps2Near2);
    const float q = with_colours ? 4.0f * size / PS2ParticleSize : 1.0f / PS2SnowSize;
    return A * q / (kPs2HalfHeight * kPs2CotHalfFov);
}

// [decomp] 0x00122d50 psiDrawParticleList
// No PS2: TEST com Z e alfa > 0, Z sem escrita, mistura aditiva (additive != 0) ou transparente; a
// cadeia (CreatePointerSpriteList[Colour]) põe a textura no GS (psiFont_SetTexture), as constantes
// do sprite na VU1 e as partículas em lotes de até 28 (posições V4-32, cores V4-8); o microprograma
// (0x46e com cores, 0x42b sem) projeta cada centro, descarta o sprite se o centro sai do volume de
// recorte 0x244640 (o dobro da tela em x e y) e desenha um SPRITE de (centro - Z * q) a
// (centro + Z * q), com ST (1, 1) no canto de cima à esquerda e (0, 0) no de baixo à direita (a
// textura gira 180 graus). count é arredondado para baixo a múltiplo de 4 (param_2 & ~3).
// No PC: o registro copia posições e cores (ou a cor fixa) e o tamanho no mundo
// (psi_particle_half_size); o renderizador monta os quadrados e faz o recorte pelo centro.
void psiDrawParticleList(_VECTOR4* positions, int32_t count, uint32_t, uint32_t texture, uint32_t* colours,
                         int8_t additive, float size, void*) {
    psiSwitchZBufferRead(0);
    psiSwitchZBufferWrite(1);
    psiSetUpColourBlend(additive == 0 ? 1 : 0);
    const int32_t n = count & int32_t(0xfffffffc);
    PsiParticleList list;
    list.first = uint32_t(g_psi_frame.particles.size());
    list.count = n > 0 ? uint32_t(n) : 0u;
    const auto* tex = nf::p32_ptr<const pstex_tag>(texture);
    list.texture = tex != nullptr ? int32_t(tex->tex_index) : -1;
    list.half_size = psi_particle_half_size(size, colours != nullptr);
    const auto* rgba = reinterpret_cast<const uint8_t*>(colours);
    for (uint32_t i = 0; i < list.count; ++i) {
        PsiParticle p;
        p.pos[0] = positions[i].x;
        p.pos[1] = positions[i].y;
        p.pos[2] = positions[i].z;
        if (rgba != nullptr) {
            std::memcpy(p.rgba, rgba + i * 4, 4);
        } else {
            p.rgba[0] = p.rgba[1] = p.rgba[2] = 0x3f;
            p.rgba[3] = 0x7f;
        }
        g_psi_frame.particles.push_back(p);
    }
    MATRIX identity{};
    for (int k = 0; k < 4; ++k) identity.m[k][k] = 1.0f;
    PsiDrawCall call = make_call(nullptr, identity, 0);
    call.particles = int32_t(g_psi_frame.particle_lists.size());
    g_psi_frame.particle_lists.push_back(list);
    g_psi_frame.draws.push_back(call);
}

// Os estilhaços (psiDrawShard): um desenho sem modelo com os vértices copiados, no estado em vigor
// (psiSetShardRenderStates).
void psi_draw_triangles(const PsiTriVertex* vertices, uint32_t count, int32_t texture) {
    PsiTriList list;
    list.first = uint32_t(g_psi_frame.tri_vertices.size());
    list.count = count - count % 3;
    list.texture = texture;
    g_psi_frame.tri_vertices.insert(g_psi_frame.tri_vertices.end(), vertices, vertices + list.count);
    MATRIX identity{};
    for (int k = 0; k < 4; ++k) identity.m[k][k] = 1.0f;
    PsiDrawCall call = make_call(nullptr, identity, 0);
    call.triangles = int32_t(g_psi_frame.tri_lists.size());
    g_psi_frame.tri_lists.push_back(list);
    g_psi_frame.draws.push_back(call);
}

// Os sprites 2D (psiDrawSprites): um desenho sem modelo com os sprites copiados, no estado em vigor
// (psiStartSprites: sem teste nem escrita de Z).
void psi_draw_2d(const Psi2DQuad* quads, uint32_t count) {
    Psi2DList list;
    list.first = uint32_t(g_psi_frame.quads2d.size());
    list.count = count;
    g_psi_frame.quads2d.insert(g_psi_frame.quads2d.end(), quads, quads + count);
    MATRIX identity{};
    for (int k = 0; k < 4; ++k) identity.m[k][k] = 1.0f;
    PsiDrawCall call = make_call(nullptr, identity, 0);
    call.sprites2d = int32_t(g_psi_frame.lists2d.size());
    g_psi_frame.lists2d.push_back(list);
    g_psi_frame.draws.push_back(call);
}

// [decomp] 0x0010b9c8 psiFadeView
// No PS2: AddDMAChainSprite(CreateTotalScreenSprite(r, g, b, a)), um sprite sem textura do tamanho da tela
// (PS2OffX/PS2OffY até 512 x PS2ScreenHeight) com RGBAQ = (r, g, b, a), ALPHA_1 0x44 e TEST_1 0x20002 (sem teste
// de Z: ZTE 0), na cadeia do quadro depois do que já foi pedido. No PC: um desenho sem modelo na mesma posição do
// registro, com a câmera em uso (psi_draw.h).
void psiFadeView(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    g_psi_frame.fade[0] = r;
    g_psi_frame.fade[1] = g;
    g_psi_frame.fade[2] = b;
    g_psi_frame.fade[3] = a;
    g_psi_frame.fade_camera = current_camera;
    ++g_psi_frame.fades;
    MATRIX identity{};
    for (int k = 0; k < 4; ++k) identity.m[k][k] = 1.0f;
    const void* keep_owner = owner_ptr;  // o esmaecimento não tem dono nem conta na ordem dos desenhos
    owner_ptr = nullptr;
    PsiDrawCall call = make_call(nullptr, identity, 0);
    owner_ptr = keep_owner;
    call.list = -1;
    call.owner_kind = PsiOwnerKind::None;
    call.fade = 1;
    call.fade_rgba[0] = r;
    call.fade_rgba[1] = g;
    call.fade_rgba[2] = b;
    call.fade_rgba[3] = a;
    g_psi_frame.draws.push_back(call);
}
