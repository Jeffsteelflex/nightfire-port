// O teste de conformidade do renderizador (só do port; docs/port.md, "macOS: o renderizador Metal"): cenas sintéticas
// feitas aqui (malhas e texturas procedurais, nada do jogo) desenhadas pela fachada Renderer (render/renderer.h) fora da
// tela, cada uma gravada num PNG. No Windows o backend é o Direct3D 11, a referência: as imagens dele ficam em
// tests/data/render (geradas com --out tests/data/render). No macOS o Metal desenha as mesmas cenas e --compare mede a
// diferença de cada uma contra a referência: o erro máximo e o médio por canal, o PSNR e a fração de pixels com um canal
// diferente em mais de 8 e de 32. Cada cena tem os seus limites (as diferenças esperadas entre duas GPUs: as regras de
// rasterização nas bordas, o arredondamento das misturas e dos filtros, o padrão do MSAA e do alpha-to-coverage, a
// filtragem anisotrópica de cada fabricante); passar deles faz a saída ser 1.
//
// Cobre: malhas rígidas e Z; a rotina de luz da VU1; o mapa de ambiente (0x2e0); o tweak; o skinning (com luz); os morphs
// dos rostos (com um índice que passa do fim dos deltas); as misturas do GS (ALPHA_1 com As, Ad e FIX, ABE desligado); o
// teste de alfa (os 8 ATST e a regra antiga); o teste e a escrita de Z (ZTE, ZTST, ZMSK); a névoa (com e sem MSAA); a
// limpeza do Z antes da lista 2; as partículas (aditiva, pré-multiplicada, subtrativa); os estilhaços; os quadrados 2D
// (ponto e linear, espelho, branca, sem textura, recorte, as imagens do port com mipmaps) e o 9-slice; o esmaecimento; o
// rastro da tela; o desfoque dos menus Modern; o filme de fundo (Catmull-Rom); o MSAA 4x com alpha-to-coverage; a escala
// de 200% e a de 150% com MSAA; os filtros de textura; a tela de carga (o snapshot); as texturas DDS (RGBA8, BC1 com e
// sem mipmaps, BC3); a troca do conteúdo de uma textura (update_texture: fora e dentro de um quadro, com a substituta
// HD saindo e com outro tamanho). Fora as imagens: read_texture devolve o que foi enviado, e as contagens das partes do
// mapa de ambiente e da cobertura (contagens.txt) batem com as da referência. E o desenho vetorial pela fachada (etapa 3d-B;
// docs/port.md, "macOS: o desenho vetorial"), num renderizador de 960 x 540 para os textos terem tamanho: a pausa Modern
// sobre a cena desfocada (draw_modern: a imagem da UiVectorCanvas composta no alvo) e um lote da camada HD com textos,
// teclas e substitutas SVG intercalados (o asset de cada uma vira textura); os casos são os de tests/vector_cases.h. As
// duas com a Rajdhani do repositório (na camada HD, no lugar das fontes do map.json), a mesma fonte nos dois sistemas:
// a diferença que fica é a do desenho (o antisserrilhado do texto), não a de uma fonte trocada.
//
// Uso: nf_render_check --out <pasta> [--compare <referências>] [--scene <nome>] [--adapter <pref>] [--janela]
//                      [--hd-ui <pasta>]
//   --hd-ui: a pasta hd_ui do repositório (as fontes Rajdhani, os glifos, o map.json e as SVG; padrão "hd_ui").
//   --adapter: o adaptador de vídeo, como o [video] adapter (no Windows, para medir a diferença entre duas GPUs).
//   --janela: também abre uma janela de verdade, desenha nela (o drawable, a cadeia de troca) e confere que o quadro é o
//   mesmo da captura fora da tela (sem a sessão gráfica, pulado).

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/map_item.h"
#include "platform/arena.h"
#include "platform/image_io.h"
#include "platform/os.h"
#include "platform/psi_2d.h"
#include "platform/video_settings.h"
#if NF_RENDER_CHECK_WINDOW
#include "platform/window.h"
#endif
#include "render/gpu_adapters.h"
#include "render/remix_d3d9.h"
#include "render/renderer.h"
#if NF_RENDER_CHECK_APP
#include "platform/entry.h"
#endif
#include "vector_cases.h"

namespace {

constexpr int kW = 256, kH = 192;
constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------- matemática (vetor-linha, como o motor)

using M4 = std::array<float, 16>;
struct V3 {
    float x, y, z;
};

V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0.0f ? mul(a, 1.0f / l) : a;
}

M4 ident() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
            r[size_t(i * 4 + j)] = s;
        }
    }
    return r;
}
M4 translate(float x, float y, float z) {
    M4 m = ident();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}
M4 rot_y(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}
M4 rot_x(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}
M4 rot_z(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// A câmera: olhar de eye para at (mão direita, y para cima) e a projeção com a profundidade invertida
// (XMMatrixPerspectiveFovRH(fov, aspecto, longe, perto), como o laço do nightfire).
struct Camera {
    M4 view_proj{};
    V3 right{}, up{}, eye{};
};
Camera camera(V3 eye, V3 at, float fov_deg = 60.0f, float zn = 0.05f, float zf = 20000.0f,
              float aspect = float(kW) / float(kH)) {
    const V3 z = norm(sub(eye, at));
    const V3 x = norm(cross(V3{0, 1, 0}, z));
    const V3 y = cross(z, x);
    const M4 view = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1};
    const float h = 1.0f / std::tan(fov_deg * kPi / 360.0f);
    const float w = h / aspect;
    const float range = zn / (zf - zn);
    const M4 proj = {w, 0, 0, 0, 0, h, 0, 0, 0, 0, range, -1, 0, 0, range * zf, 0};
    Camera c;
    c.view_proj = mul(view, proj);
    c.right = x;
    c.up = y;
    c.eye = eye;
    return c;
}

// Um gerador determinístico (os mesmos números em toda plataforma).
struct Lcg {
    uint32_t s = 12345;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / 16777216.0f;
    }
};

// ---------------------------------------------------------------- texturas (alfa na escala do GS: 0x80 = opaco)

enum Tex : int { T_CHECK = 0, T_RAMP, T_FINE, T_LEAF, T_GLYPH, T_FRAME, T_SOFT, T_ENV, T_VRAMP, kTexCount };
constexpr uint32_t kPortImage = 0x7e570001u;

nf::Texture make_tex(int w, int h, const std::function<std::array<uint8_t, 4>(int, int)>& px) {
    nf::Texture t;
    t.width = w;
    t.height = h;
    t.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto c = px(x, y);
            std::memcpy(&t.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4], c.data(), 4);
            if (c[3] != 0x80) t.has_alpha = true;
        }
    }
    return t;
}

uint8_t u8(float v) { return uint8_t(std::clamp(int(std::lround(v)), 0, 255)); }

std::vector<nf::Texture> textures() {
    std::vector<nf::Texture> t(kTexCount);
    const uint8_t pal[4][3] = {{200, 60, 40}, {230, 200, 80}, {60, 140, 200}, {90, 200, 110}};
    t[T_CHECK] = make_tex(64, 64, [&](int x, int y) {
        const int i = x / 8, j = y / 8;
        const uint8_t* p = pal[(i + 2 * j) % 4];
        const float k = ((i + j) & 1) ? 0.6f : 1.0f;
        return std::array<uint8_t, 4>{u8(p[0] * k), u8(p[1] * k), u8(p[2] * k), 0x80};
    });
    t[T_RAMP] = make_tex(64, 64, [](int x, int y) {
        return std::array<uint8_t, 4>{u8(255.0f - x * 3.0f), u8(80.0f + y * 2.0f), u8(40.0f + x * 3.0f), u8(x * 255.0f / 63.0f)};
    });
    t[T_FINE] = make_tex(256, 256, [](int x, int y) {
        uint8_t v = ((x / 2 + y / 2) & 1) ? 230 : 20;
        if (x % 32 == 0 || y % 32 == 0) return std::array<uint8_t, 4>{220, 40, 40, 0x80};
        if ((x + y) % 64 == 0) return std::array<uint8_t, 4>{40, 60, 230, 0x80};
        return std::array<uint8_t, 4>{v, v, v, 0x80};
    });
    t[T_LEAF] = make_tex(64, 64, [](int x, int y) {
        const auto in = [&](float cx, float cy, float rx, float ry) {
            const float dx = (float(x) + 0.5f - cx) / rx, dy = (float(y) + 0.5f - cy) / ry;
            return dx * dx + dy * dy <= 1.0f;
        };
        const bool leaf = in(20, 22, 14, 9) || in(42, 30, 10, 18) || in(28, 48, 18, 7);
        return std::array<uint8_t, 4>{u8(40.0f + x), u8(120.0f + y), 50, uint8_t(leaf ? 0x80 : 0)};
    });
    static const uint8_t glyph_rows[8] = {0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0x00};
    t[T_GLYPH] = make_tex(32, 32, [](int x, int y) {
        const bool on = (glyph_rows[(y / 4) % 8] >> (7 - (x / 4) % 8)) & 1;
        return std::array<uint8_t, 4>{255, 255, 255, uint8_t(on ? 0x80 : 0)};
    });
    t[T_FRAME] = make_tex(64, 64, [](int x, int y) {
        const int e = std::min(std::min(x, 63 - x), std::min(y, 63 - y));
        if ((x < 6 || x > 57) && (y < 6 || y > 57)) return std::array<uint8_t, 4>{220, 50, 40, 0x80};
        if (e < 2) return std::array<uint8_t, 4>{60, 40, 20, 0x80};
        if (e < 10) return std::array<uint8_t, 4>{u8(150.0f + e * 6.0f), u8(110.0f + e * 4.0f), 60, 0x80};
        if (e < 12) return std::array<uint8_t, 4>{240, 210, 140, 0x80};
        return std::array<uint8_t, 4>{30, 20, u8(20.0f + y), 0x60};
    });
    t[T_SOFT] = make_tex(32, 32, [](int x, int y) {
        const float dx = float(x) + 0.5f - 16.0f, dy = float(y) + 0.5f - 16.0f;
        const float k = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy) / 16.0f);
        return std::array<uint8_t, 4>{255, 220, 160, u8(k * k * 128.0f)};
    });
    t[T_ENV] = make_tex(64, 64, [](int x, int y) {
        if (y % 16 < 2) return std::array<uint8_t, 4>{250, 250, 230, 0x80};
        if (x % 32 < 2) return std::array<uint8_t, 4>{250, 120, 40, 0x80};
        return std::array<uint8_t, 4>{u8(40.0f + y * 2.0f), u8(80.0f + y * 2.0f), 200, 0x80};
    });
    t[T_VRAMP] = make_tex(32, 32, [](int x, int y) {
        return std::array<uint8_t, 4>{u8(255.0f - x * 2.0f), u8(64.0f + x * 4.0f), 32, u8(y * 128.0f / 31.0f)};
    });
    return t;
}

std::vector<uint8_t> port_image() {
    std::vector<uint8_t> px(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            uint8_t* p = &px[size_t(y * 64 + x) * 4];
            const bool ring = std::abs(std::hypot(float(x) - 31.5f, float(y) - 31.5f) - 22.0f) < 4.0f;
            p[0] = ring ? 250 : u8(x * 4.0f);
            p[1] = ring ? 200 : u8(y * 4.0f);
            p[2] = ring ? 60 : 120;
            p[3] = ((x / 4 + y / 4) & 1) || ring ? 0x80 : 0x30;
        }
    }
    return px;
}

// ---------------------------------------------------------------- malhas

using Rgba = std::array<uint8_t, 4>;
constexpr Rgba kGsWhite = {0x80, 0x80, 0x80, 0x80};

struct MeshBuilder {
    nf::EntityMesh m;
    void begin(int texture, const nf::GsState& gs = nf::GsState{}, bool env = false) {
        nf::SubMesh sm;
        sm.texture = texture;
        sm.first_index = uint32_t(m.indices.size());
        sm.gs = gs;
        sm.env = env;
        m.submeshes.push_back(sm);
    }
    void end() { m.submeshes.back().index_count = uint32_t(m.indices.size()) - m.submeshes.back().first_index; }
    // Uma grade de nx x ny quadrados de o a o + ax + ay, com a normal (unitária; a do disco é o inteiro / 128: x 127/128).
    void grid(V3 o, V3 ax, V3 ay, int nx, int ny, float us, float vs, V3 n,
              const std::function<Rgba(float, float)>& colour = nullptr) {
        const uint32_t base = uint32_t(m.vertices.size());
        const V3 nn = mul(norm(n), 127.0f / 128.0f);
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                const float s = float(i) / float(nx), t = float(j) / float(ny);
                const V3 p = add(o, add(mul(ax, s), mul(ay, t)));
                nf::Vertex v{};
                v.pos[0] = p.x;
                v.pos[1] = p.y;
                v.pos[2] = p.z;
                v.normal[0] = nn.x;
                v.normal[1] = nn.y;
                v.normal[2] = nn.z;
                v.uv[0] = s * us;
                v.uv[1] = t * vs;
                const Rgba c = colour ? colour(s, t) : kGsWhite;
                std::memcpy(v.color, c.data(), 4);
                m.vertices.push_back(v);
            }
        }
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const uint32_t a = base + uint32_t(j * (nx + 1) + i), b = a + 1, c = a + uint32_t(nx + 1), d = c + 1;
                m.indices.insert(m.indices.end(), {a, b, d, a, d, c});
            }
        }
    }
    // Uma esfera (normais radiais).
    void sphere(V3 c, float r, int nu, int nv, float us, float vs, const std::function<Rgba(float, float)>& colour = nullptr) {
        const uint32_t base = uint32_t(m.vertices.size());
        for (int j = 0; j <= nv; ++j) {
            for (int i = 0; i <= nu; ++i) {
                const float s = float(i) / float(nu), t = float(j) / float(nv);
                const float th = s * 2.0f * kPi, ph = t * kPi;
                const V3 n = {std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th)};
                nf::Vertex v{};
                v.pos[0] = c.x + n.x * r;
                v.pos[1] = c.y + n.y * r;
                v.pos[2] = c.z + n.z * r;
                v.normal[0] = n.x * 127.0f / 128.0f;
                v.normal[1] = n.y * 127.0f / 128.0f;
                v.normal[2] = n.z * 127.0f / 128.0f;
                v.uv[0] = s * us;
                v.uv[1] = t * vs;
                const Rgba col = colour ? colour(s, t) : kGsWhite;
                std::memcpy(v.color, col.data(), 4);
                m.vertices.push_back(v);
            }
        }
        for (int j = 0; j < nv; ++j) {
            for (int i = 0; i < nu; ++i) {
                const uint32_t a = base + uint32_t(j * (nu + 1) + i), b = a + 1, cc = a + uint32_t(nu + 1), d = cc + 1;
                m.indices.insert(m.indices.end(), {a, cc, d, a, d, b});
            }
        }
    }
    // Uma caixa centrada na origem, cada face uma grade 2 x 2.
    void box(float hx, float hy, float hz, const std::function<Rgba(float, float)>& colour = nullptr) {
        grid({-hx, -hy, hz}, {2 * hx, 0, 0}, {0, 2 * hy, 0}, 2, 2, 1, 1, {0, 0, 1}, colour);
        grid({hx, -hy, -hz}, {-2 * hx, 0, 0}, {0, 2 * hy, 0}, 2, 2, 1, 1, {0, 0, -1}, colour);
        grid({hx, -hy, hz}, {0, 0, -2 * hz}, {0, 2 * hy, 0}, 2, 2, 1, 1, {1, 0, 0}, colour);
        grid({-hx, -hy, -hz}, {0, 0, 2 * hz}, {0, 2 * hy, 0}, 2, 2, 1, 1, {-1, 0, 0}, colour);
        grid({-hx, hy, hz}, {2 * hx, 0, 0}, {0, 0, -2 * hz}, 2, 2, 1, 1, {0, 1, 0}, colour);
        grid({-hx, -hy, -hz}, {2 * hx, 0, 0}, {0, 0, 2 * hz}, 2, 2, 1, 1, {0, -1, 0}, colour);
    }
};

// Um quadrado na vertical (de frente para +z) de (x0, y0) a (x1, y1) em z.
nf::EntityMesh quad_mesh(float x0, float y0, float x1, float y1, float z, int texture, const nf::GsState& gs = nf::GsState{},
                         Rgba colour = kGsWhite, float us = 1.0f, float vs = 1.0f) {
    MeshBuilder b;
    b.begin(texture, gs);
    b.grid({x0, y1, z}, {x1 - x0, 0, 0}, {0, y0 - y1, 0}, 1, 1, us, vs, {0, 0, 1}, [colour](float, float) { return colour; });
    b.end();
    return b.m;
}

nf::DrawItem item(int mesh, const M4& world = ident(), nf::Blend blend = nf::Blend::Opaque) {
    nf::DrawItem d;
    d.mesh = mesh;
    d.world = world;
    d.blend = blend;
    return d;
}

nf::GsState gs_of(uint64_t alpha, uint64_t test, bool zmsk, uint8_t abe = 1) {
    nf::GsState g;
    g.alpha = alpha;
    g.test = test;
    g.zbuf = zmsk ? (uint64_t(1) << 32) : 0;
    g.has = nf::GsState::kAlpha | nf::GsState::kTest | nf::GsState::kZbuf;
    g.abe = abe;
    return g;
}

constexpr uint64_t kZGequal = 0x50000;  // ZTE, ZTST = GEQUAL, sem teste de alfa

// ---------------------------------------------------------------- as cenas

struct Ctx {
    nf::Renderer& r;
    std::wstring out;
    std::vector<nf::Texture> tex;
    std::string report;  // linhas de fora das imagens (read_texture)
    const nf::vcase::Assets* assets = nullptr;  // o map.json com as SVG (as cenas do desenho vetorial)
};

nf::FrameParams frame_of(const Camera& c, std::array<float, 4> clear = {0.125f, 0.145f, 0.19f, 1.0f}) {
    nf::FrameParams f;
    f.view_proj = c.view_proj;
    f.clear_color = clear;
    return f;
}

const Camera& default_camera() {
    static const Camera c = camera({0.0f, 1.6f, 5.5f}, {0.0f, 0.9f, 0.0f});
    return c;
}

// O chão quadriculado e duas caixas que se cruzam (a base de várias cenas).
void rigid_scene(Ctx& c, std::vector<nf::DrawItem>& items, float box_x = -1.0f) {
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 8, 8, 4, 4, {0, 1, 0},
               [](float s, float t) { return Rgba{u8(96 + s * 64), 0x80, u8(112 + t * 32), 0x80}; });
    floor.end();
    MeshBuilder box1;
    box1.begin(T_CHECK);
    box1.box(0.5f, 0.5f, 0.5f, [](float s, float t) { return Rgba{0x80, u8(96 + s * 64), u8(96 + t * 64), 0x80}; });
    box1.end();
    MeshBuilder box2;
    box2.begin(T_CHECK);
    box2.box(0.75f, 0.75f, 0.75f, [](float s, float) { return Rgba{u8(160 + s * 95), u8(160 + s * 95), 0xc0, 0x80}; });
    box2.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    items.push_back(item(c.r.add_mesh(box1.m), mul(rot_y(0.5f), translate(box_x, 0.5f, 0.0f))));
    items.push_back(item(c.r.add_mesh(box2.m), mul(rot_y(-0.35f), translate(0.7f, 0.75f, -0.4f))));
}

void scene_rigido(Ctx& c) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    // Cor de vértice acima de 0x80 (até 2.0 no GS): satura.
    items.push_back(item(c.r.add_mesh(quad_mesh(-3.2f, 0.2f, -2.0f, 1.4f, -1.0f, T_CHECK, nf::GsState{}, Rgba{0xff, 0xff, 0xff, 0x80}))));
    c.r.render(items, frame_of(default_camera()));
    int w = 0, h = 0;
    std::vector<uint8_t> back;
    const bool ok = c.r.read_texture(T_CHECK, w, h, back) && w == c.tex[T_CHECK].width && h == c.tex[T_CHECK].height &&
                    back == c.tex[T_CHECK].rgba;
    c.report += std::string("read_texture ") + (ok ? "igual" : "DIFERENTE") + "\n";
}

void scene_luzes(Ctx& c) {
    MeshBuilder plane;
    plane.begin(-1);
    plane.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 16, 16, 1, 1, {0, 1, 0});
    plane.end();
    MeshBuilder ball;
    ball.begin(T_CHECK);
    ball.sphere({0, 0, 0}, 0.9f, 24, 16, 4, 2);
    ball.end();
    const float lp[2][4] = {{-1.6f, 0.8f, 0.6f, 1.0f}, {1.7f, 0.6f, -0.8f, 1.0f}};
    const float lc[2][4] = {{380.0f, 230.0f, 120.0f, 1.0f / 16.0f}, {90.0f, 160.0f, 420.0f, 1.0f / 9.0f}};
    std::vector<nf::DrawItem> items;
    nf::DrawItem p = item(c.r.add_mesh(plane.m));
    nf::DrawItem s = item(c.r.add_mesh(ball.m), translate(0.0f, 1.0f, 0.0f));
    for (nf::DrawItem* d : {&p, &s}) {
        d->tint = {0.3f, 0.3f, 0.35f, 1.0f};
        d->num_lights = 2;
        std::memcpy(d->light_color, lc, sizeof(lc));
    }
    std::memcpy(p.light_pos, lp, sizeof(lp));
    // A esfera: as luzes no espaço dos vértices (a do mundo pela inversa da matriz).
    for (int k = 0; k < 2; ++k) {
        s.light_pos[k][0] = lp[k][0];
        s.light_pos[k][1] = lp[k][1] - 1.0f;
        s.light_pos[k][2] = lp[k][2];
        s.light_pos[k][3] = 1.0f;
    }
    items.push_back(p);
    items.push_back(s);
    c.r.render(items, frame_of(default_camera()));
}

void scene_ambiente(Ctx& c) {
    std::vector<nf::DrawItem> items;
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 4, 4, 4, 4, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    MeshBuilder ball;
    ball.begin(T_ENV, nf::GsState{}, true);
    ball.sphere({0, 0, 0}, 0.85f, 32, 20, 1, 1);
    ball.end();
    MeshBuilder cube;
    cube.begin(T_ENV, nf::GsState{}, true);
    cube.box(0.6f, 0.6f, 0.6f);
    cube.end();
    const Camera& cam = default_camera();
    nf::DrawItem a = item(c.r.add_mesh(ball.m), translate(-1.1f, 1.0f, 0.0f));
    nf::DrawItem b = item(c.r.add_mesh(cube.m), mul(mul(rot_x(0.4f), rot_y(0.6f)), translate(1.2f, 0.9f, 0.0f)));
    for (nf::DrawItem* d : {&a, &b}) {
        d->env_s[0] = cam.right.x;
        d->env_s[1] = cam.right.y;
        d->env_s[2] = cam.right.z;
        d->env_t[0] = cam.up.x;
        d->env_t[1] = cam.up.y;
        d->env_t[2] = cam.up.z;
    }
    items.push_back(a);
    items.push_back(b);
    c.r.render(items, frame_of(cam));
}

void scene_tweak(Ctx& c) {
    std::vector<nf::DrawItem> items;
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 4, 4, 4, 4, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    const std::array<float, 4> tints[3] = {{1.0f, 0.5f, 0.25f, 1.0f}, {0.4f, 1.0f, 1.0f, 0.5f}, {1.0f, 1.0f, 1.0f, 0.35f}};
    const nf::Blend blends[3] = {nf::Blend::Opaque, nf::Blend::Alpha, nf::Blend::Additive};
    for (int i = 0; i < 3; ++i) {
        const float x = -2.1f + float(i) * 1.5f;
        nf::DrawItem d = item(c.r.add_mesh(quad_mesh(x, 0.3f, x + 1.2f, 1.7f, 0.5f, T_CHECK)), ident(), blends[i]);
        d.tint = tints[i];
        items.push_back(d);
    }
    c.r.render(items, frame_of(default_camera()));
}

// Uma fita com skinning: dois ossos, o peso do primeiro de 1 (embaixo) a 0 (em cima).
nf::EntityMesh ribbon() {
    nf::EntityMesh m;
    m.skinned = true;
    m.bone_count = 2;
    const int nx = 4, ny = 20;
    for (int j = 0; j <= ny; ++j) {
        for (int i = 0; i <= nx; ++i) {
            nf::SkinnedVertex v{};
            const float x = -0.3f + 0.6f * float(i) / float(nx), y = 3.0f * float(j) / float(ny);
            v.pos[0] = x;
            v.pos[1] = y;
            v.normal[2] = 1.0f;
            v.uv[0] = float(i) / float(nx);
            v.uv[1] = 4.0f * float(j) / float(ny);
            std::memcpy(v.color, kGsWhite.data(), 4);
            v.bones[0] = 0;
            v.bones[1] = 1;
            v.weight = std::clamp(2.0f - y, 0.0f, 1.0f);
            m.skin_vertices.push_back(v);
        }
    }
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const uint32_t a = uint32_t(j * (nx + 1) + i), b = a + 1, cc = a + uint32_t(nx + 1), d = cc + 1;
            m.indices.insert(m.indices.end(), {a, b, d, a, d, cc});
        }
    }
    nf::SubMesh sm;
    sm.texture = T_CHECK;
    sm.index_count = uint32_t(m.indices.size());
    m.submeshes.push_back(sm);
    return m;
}

void scene_skinning(Ctx& c) {
    std::vector<nf::DrawItem> items;
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 4, 4, 4, 4, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    const int mesh = c.r.add_mesh(ribbon());
    const auto bend = [](float x, float angle) {
        const M4 place = translate(x, 0.0f, 0.0f);
        return std::vector<std::array<float, 16>>{place, mul(mul(mul(translate(0, -1.5f, 0), rot_z(angle)), translate(0, 1.5f, 0)), place)};
    };
    nf::DrawItem a = item(mesh);
    a.palette = bend(-1.2f, 0.8f);
    items.push_back(a);
    nf::DrawItem b = item(mesh);
    b.palette = bend(1.2f, -0.6f);
    b.tint = {0.4f, 0.4f, 0.4f, 1.0f};
    b.num_lights = 1;
    const float lp[4] = {1.8f, 1.2f, 1.0f, 1.0f}, lc[4] = {300.0f, 300.0f, 260.0f, 1.0f / 9.0f};
    std::memcpy(b.light_pos[0], lp, sizeof(lp));
    std::memcpy(b.light_color[0], lc, sizeof(lc));
    items.push_back(b);
    c.r.render(items, frame_of(default_camera()));
}

// Um "rosto": uma grade com skinning e três alvos de morph (um volume, uma onda em x, uma curva em y); a última linha
// aponta para o último elemento dos deltas (os alvos 1 e 2 caem fora: valem 0) e os cantos não têm morph.
nf::EntityMesh face() {
    nf::EntityMesh m;
    m.skinned = true;
    m.bone_count = 1;
    const int n = 16;
    const uint32_t targets = 3, verts = uint32_t((n + 1) * (n + 1));
    m.morph.vertices = verts;
    m.morph.targets = targets;
    m.morph.first = 3;
    m.morph.deltas.assign(size_t(3 + verts * targets) * 3, 0.0f);
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            nf::SkinnedVertex v{};
            const float x = -1.0f + 2.0f * float(i) / float(n), y = -1.0f + 2.0f * float(j) / float(n);
            v.pos[0] = x;
            v.pos[1] = y;
            v.normal[2] = 1.0f;
            v.uv[0] = float(i) / float(n) * 2.0f;
            v.uv[1] = float(j) / float(n) * 2.0f;
            std::memcpy(v.color, kGsWhite.data(), 4);
            v.weight = 1.0f;
            const uint32_t idx = uint32_t(j * (n + 1) + i);
            const uint32_t e = 3 + idx * targets;
            float* d = &m.morph.deltas[size_t(e) * 3];
            d[2] = 0.6f * std::exp(-(x * x + y * y) * 3.0f);  // alvo 0
            d[3] = 0.15f * std::sin(3.0f * y);               // alvo 1 (x)
            d[7] = 0.25f * x * x;                            // alvo 2 (y)
            v.morph = int32_t(e);
            if (j == n) v.morph = int32_t(m.morph.elements() - 1);
            if ((i == 0 || i == n) && (j == 0 || j == n)) v.morph = -1;
            m.skin_vertices.push_back(v);
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const uint32_t a = uint32_t(j * (n + 1) + i), b = a + 1, cc = a + uint32_t(n + 1), d = cc + 1;
            m.indices.insert(m.indices.end(), {a, b, d, a, d, cc});
        }
    }
    nf::SubMesh sm;
    sm.texture = T_CHECK;
    sm.index_count = uint32_t(m.indices.size());
    m.submeshes.push_back(sm);
    return m;
}

void scene_morph(Ctx& c) {
    std::vector<nf::DrawItem> items;
    const int mesh = c.r.add_mesh(face());
    nf::DrawItem a = item(mesh);
    a.palette = {mul(rot_y(0.5f), translate(-1.1f, 1.1f, 0.0f))};
    a.morph.assign(nf::kMaxMorphTargets, 0.0f);
    a.morph[0] = 1.0f;
    a.morph[1] = 0.6f;
    a.morph[2] = -0.8f;
    items.push_back(a);
    nf::DrawItem b = item(mesh);  // sem pesos: o skinning comum
    b.palette = {mul(rot_y(-0.5f), translate(1.1f, 1.1f, 0.0f))};
    b.morph.assign(nf::kMaxMorphTargets, 0.0f);
    items.push_back(b);
    c.r.render(items, frame_of(default_camera()));
}

void scene_misturas(Ctx& c) {
    std::vector<nf::DrawItem> items;
    // O fundo grava o alfa da rampa no alvo (o Ad das misturas com C = Ad).
    items.push_back(item(c.r.add_mesh(quad_mesh(-4.5f, -1.5f, 4.5f, 3.5f, -1.5f, T_RAMP, gs_of(0x2a, kZGequal, false), kGsWhite, 3, 3))));
    struct Mix {
        uint64_t alpha;
        uint8_t abe;
    };
    const Mix mixes[10] = {{0x44, 1}, {0x48, 1}, {0x42, 1}, {0x01, 1}, {0x4000000064ull, 1},
                           {0x54, 1}, {0x58, 1}, {0x46, 1}, {0x8000000068ull, 1}, {0x44, 0}};
    const Rgba colours[10] = {{0x80, 0x80, 0x80, 0x80}, {0x60, 0x80, 0xa0, 0x80}, {0x80, 0x40, 0x40, 0x80},
                              {0x40, 0x80, 0x40, 0x80}, {0xa0, 0x80, 0x60, 0x80}, {0x80, 0x80, 0xff, 0x80},
                              {0xff, 0x80, 0x80, 0x80}, {0x80, 0x80, 0x80, 0x80}, {0x40, 0x60, 0x80, 0x80},
                              {0x80, 0x80, 0x80, 0x80}};
    for (int i = 0; i < 10; ++i) {
        const float x = -3.3f + float(i % 5) * 1.35f, y = i < 5 ? 1.7f : 0.25f;
        items.push_back(item(c.r.add_mesh(quad_mesh(x, y, x + 1.2f, y + 1.25f, -0.5f, T_VRAMP,
                                                    gs_of(mixes[i].alpha, kZGequal, true, mixes[i].abe), colours[i])),
                             ident(), nf::Blend::Alpha));
    }
    c.r.render(items, frame_of(camera({0.0f, 1.0f, 5.0f}, {0.0f, 1.0f, 0.0f})));
}

void scene_teste_alfa(Ctx& c) {
    std::vector<nf::DrawItem> items;
    items.push_back(item(c.r.add_mesh(quad_mesh(-4.5f, -1.5f, 4.5f, 3.5f, -1.5f, T_CHECK, nf::GsState{}, kGsWhite, 4, 3))));
    for (int i = 0; i < 10; ++i) {
        const float x = -3.3f + float(i % 5) * 1.35f, y = i < 5 ? 1.7f : 0.25f;
        nf::DrawItem d;
        if (i < 8) {
            const uint64_t test = 1 | (uint64_t(i) << 1) | (uint64_t(0x40) << 4) | kZGequal;
            d = item(c.r.add_mesh(quad_mesh(x, y, x + 1.2f, y + 1.25f, -0.5f, T_RAMP, gs_of(0x44, test, false))), ident(),
                     nf::Blend::Alpha);
        } else {
            // A regra antiga (sem TEST_1): opaco testa em 50% e grava Z; misturado descarta só alfa < 0.004.
            d = item(c.r.add_mesh(quad_mesh(x, y, x + 1.2f, y + 1.25f, -0.5f, T_RAMP)), ident(),
                     i == 8 ? nf::Blend::Opaque : nf::Blend::Alpha);
        }
        items.push_back(d);
    }
    c.r.render(items, frame_of(camera({0.0f, 1.0f, 5.0f}, {0.0f, 1.0f, 0.0f})));
}

void scene_z(Ctx& c) {
    std::vector<nf::DrawItem> items;
    items.push_back(item(c.r.add_mesh(quad_mesh(-4.5f, -1.5f, 4.5f, 3.5f, -3.0f, T_CHECK, nf::GsState{}, kGsWhite, 4, 3))));
    // A parede: a faixa de cima, na frente.
    items.push_back(item(c.r.add_mesh(quad_mesh(-3.2f, 1.0f, 3.2f, 2.0f, 0.0f, -1, nf::GsState{}, Rgba{0xff, 0x40, 0x40, 0x80}))));
    // Atrás dela, cinco faixas: ZTST NEVER, ALWAYS, GEQUAL, GREATER e o ZTE desligado.
    const uint64_t tests[5] = {0x10000, 0x10000 | (1ull << 17), 0x10000 | (2ull << 17), 0x10000 | (3ull << 17), 0};
    const Rgba cols[5] = {{0x40, 0xff, 0x40, 0x80}, {0x40, 0x80, 0xff, 0x80}, {0xff, 0xff, 0x40, 0x80},
                          {0xff, 0x40, 0xff, 0x80}, {0x40, 0xff, 0xff, 0x80}};
    for (int i = 0; i < 5; ++i) {
        const float x = -3.0f + float(i) * 1.25f;
        items.push_back(item(c.r.add_mesh(quad_mesh(x, 0.6f, x + 1.0f, 2.4f, -1.0f, -1, gs_of(0x2a, tests[i], false), cols[i]))));
    }
    // ZMSK: uma faixa na frente que não grava Z, e outra atrás dela desenhada depois, que aparece por cima.
    items.push_back(item(c.r.add_mesh(quad_mesh(-2.0f, -0.3f, 2.0f, 0.45f, 0.6f, -1, gs_of(0x2a, kZGequal, true), Rgba{0x40, 0x40, 0xff, 0x80}))));
    items.push_back(item(c.r.add_mesh(quad_mesh(-1.0f, -0.15f, 1.0f, 0.3f, 0.3f, -1, gs_of(0x2a, kZGequal, false), Rgba{0xff, 0xff, 0xff, 0x80}))));
    items.push_back(item(c.r.add_mesh(quad_mesh(-2.6f, -0.1f, -1.6f, 0.2f, 0.2f, -1, gs_of(0x2a, kZGequal, false), Rgba{0xff, 0xa0, 0x20, 0x80}))));
    c.r.render(items, frame_of(camera({0.0f, 1.0f, 5.0f}, {0.0f, 1.0f, 0.0f})));
}

void fog_items(Ctx& c, std::vector<nf::DrawItem>& items) {
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-6, 0, 0}, {12, 0, 0}, {0, 0, -160}, 4, 40, 3, 40, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    MeshBuilder pillar;
    pillar.begin(T_CHECK);
    pillar.box(0.6f, 2.0f, 0.6f, [](float s, float t) { return Rgba{u8(128 + s * 100), 0x80, u8(128 + t * 60), 0x80}; });
    pillar.end();
    const int pm = c.r.add_mesh(pillar.m);
    const float zs[6] = {-8.0f, -16.0f, -30.0f, -55.0f, -90.0f, -140.0f};
    for (int i = 0; i < 6; ++i) items.push_back(item(pm, translate(i % 2 ? 3.0f : -3.0f, 2.0f, zs[i])));
    nf::DrawItem fog;
    fog.fog_pass = true;
    fog.fog_rgb[0] = 90;
    fog.fog_rgb[1] = 100;
    fog.fog_rgb[2] = 120;
    fog.fog_far1 = 4194240.0f;
    items.push_back(fog);
    // Depois da névoa (a lista transparente): sem névoa.
    nf::DrawItem glass = item(c.r.add_mesh(quad_mesh(-1.5f, 0.5f, 1.5f, 2.5f, -12.0f, T_CHECK)), ident(), nf::Blend::Alpha);
    glass.tint = {1.0f, 1.0f, 1.0f, 0.6f};
    items.push_back(glass);
}

const Camera& fog_camera() {
    static const Camera c = camera({0.0f, 2.0f, 2.0f}, {0.0f, 1.4f, -40.0f});
    return c;
}

void scene_neblina(Ctx& c) {
    std::vector<nf::DrawItem> items;
    fog_items(c, items);
    c.r.render(items, frame_of(fog_camera(), {0.1f, 0.1f, 0.1f, 1.0f}));
}

void scene_limpa_z(Ctx& c) {
    std::vector<nf::DrawItem> items;
    items.push_back(item(c.r.add_mesh(quad_mesh(-4.5f, -1.5f, 4.5f, 3.5f, -6.0f, T_CHECK, nf::GsState{}, kGsWhite, 4, 3))));
    items.push_back(item(c.r.add_mesh(quad_mesh(-3.0f, 0.0f, 3.0f, 2.5f, -1.0f, -1, nf::GsState{}, Rgba{0x70, 0x70, 0x78, 0x80}))));
    // Antes da limpeza: atrás da parede, escondido.
    items.push_back(item(c.r.add_mesh(quad_mesh(0.8f, 0.4f, 2.2f, 1.8f, -3.0f, -1, nf::GsState{}, Rgba{0xff, 0x30, 0x30, 0x80}))));
    nf::DrawItem clear;
    clear.clear_depth = true;
    items.push_back(clear);
    // Depois: por cima da parede, e testando-se entre si.
    items.push_back(item(c.r.add_mesh(quad_mesh(-1.0f, 0.3f, 0.5f, 1.9f, -3.0f, T_CHECK, nf::GsState{}, Rgba{0x40, 0xff, 0x40, 0x80}))));
    items.push_back(item(c.r.add_mesh(quad_mesh(-0.2f, 0.8f, 1.6f, 2.3f, -3.5f, -1, nf::GsState{}, Rgba{0x40, 0x40, 0xff, 0x80}))));
    c.r.render(items, frame_of(camera({0.0f, 1.2f, 4.0f}, {0.0f, 1.2f, 0.0f})));
}

void add_sprite(nf::DrawItem& d, const Camera& cam, V3 p, float hs, Rgba col) {
    const V3 r = mul(cam.right, hs), u = mul(cam.up, hs);
    const V3 corners[4] = {add(sub(p, r), u), add(add(p, r), u), sub(add(p, r), u), sub(sub(p, r), u)};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int k = 0; k < 4; ++k) {
        nf::Vertex v{};
        v.pos[0] = corners[k].x;
        v.pos[1] = corners[k].y;
        v.pos[2] = corners[k].z;
        v.uv[0] = uv[k][0];
        v.uv[1] = uv[k][1];
        std::memcpy(v.color, col.data(), 4);
        d.sprites.push_back(v);
    }
}

void scene_particulas(Ctx& c) {
    std::vector<nf::DrawItem> items;
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 4, 4, 4, 4, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    const Camera& cam = default_camera();
    nf::DrawItem add_d, alpha_d, sub_d;
    add_d.gs.alpha = 0x48;
    alpha_d.gs.alpha = 0x44;
    sub_d.gs.alpha = 0x42;
    for (nf::DrawItem* d : {&add_d, &alpha_d, &sub_d}) {
        d->gs.has = nf::GsState::kAlpha;
        d->blend = nf::Blend::Alpha;
        d->sprite_texture = T_SOFT;
        d->world = ident();  // os vértices já estão no mundo (como no RuntimeGfx)
    }
    for (int i = 0; i < 24; ++i) {
        const float a = float(i) * 2.0f * kPi / 24.0f;
        add_sprite(add_d, cam, {-1.3f + std::cos(a) * 0.9f, 1.3f + std::sin(a) * 0.9f, 0.0f}, 0.25f + 0.1f * float(i % 3),
                   Rgba{0xff, u8(float(0x40 + i * 6)), 0x30, u8(float(0x60 + i * 6))});
    }
    for (int i = 0; i < 16; ++i) {
        add_sprite(alpha_d, cam, {0.8f + float(i % 4) * 0.45f, 0.6f + float(i / 4) * 0.45f, 0.2f}, 0.3f,
                   Rgba{u8(float(0x40 + i * 12)), 0x80, 0xff, u8(float(0x40 + i * 12))});
    }
    for (int i = 0; i < 6; ++i) {
        add_sprite(sub_d, cam, {-2.4f + float(i) * 0.9f, 0.25f, 1.0f}, 0.35f, Rgba{0x80, 0x80, 0x80, 0x80});
    }
    items.push_back(add_d);
    items.push_back(alpha_d);
    items.push_back(sub_d);
    c.r.render(items, frame_of(cam));
}

void scene_cacos(Ctx& c) {
    std::vector<nf::DrawItem> items;
    MeshBuilder floor;
    floor.begin(T_CHECK);
    floor.grid({-4, 0, 4}, {8, 0, 0}, {0, 0, -8}, 4, 4, 4, 4, {0, 1, 0});
    floor.end();
    items.push_back(item(c.r.add_mesh(floor.m)));
    items.push_back(item(c.r.add_mesh(quad_mesh(-3.0f, 0.0f, 3.0f, 2.5f, -0.6f, -1, nf::GsState{}, Rgba{0x60, 0x68, 0x70, 0x80}))));
    nf::DrawItem shards;
    shards.gs = gs_of(0x44, kZGequal, true);
    shards.blend = nf::Blend::Alpha;
    shards.triangle_texture = T_CHECK;
    shards.world = ident();
    Lcg rng;
    for (int t = 0; t < 40; ++t) {
        const V3 centre = {-2.0f + rng.next() * 4.0f, 0.4f + rng.next() * 1.8f, -1.2f + rng.next() * 1.6f};
        const Rgba col = {u8(0x60 + rng.next() * 0x9f), u8(0x60 + rng.next() * 0x9f), u8(0x60 + rng.next() * 0x9f),
                          u8(0x50 + rng.next() * 0x30)};
        for (int k = 0; k < 3; ++k) {
            nf::Vertex v{};
            v.pos[0] = centre.x + (rng.next() - 0.5f) * 0.7f;
            v.pos[1] = centre.y + (rng.next() - 0.5f) * 0.7f;
            v.pos[2] = centre.z + (rng.next() - 0.5f) * 0.7f;
            v.uv[0] = rng.next();
            v.uv[1] = rng.next();
            std::memcpy(v.color, col.data(), 4);
            shards.triangles.push_back(v);
        }
    }
    items.push_back(shards);
    c.r.render(items, frame_of(default_camera()));
}

nf::Quad2D quad2d(float x0, float y0, float x1, float y1, int texture, Rgba col = kGsWhite, uint64_t alpha = 0x44,
                  bool linear = true, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    nf::Quad2D q;
    q.x0 = x0;
    q.y0 = y0;
    q.x1 = x1;
    q.y1 = y1;
    q.u0 = u0;
    q.v0 = v0;
    q.u1 = u1;
    q.v1 = v1;
    std::memcpy(q.rgba, col.data(), 4);
    q.texture = texture;
    q.alpha = alpha;
    q.linear = linear;
    return q;
}

nf::DrawItem item2d(std::vector<nf::Quad2D> quads, float x0 = 0, float y0 = 0, float x1 = float(kW), float y1 = float(kH)) {
    nf::DrawItem d;
    d.blend = nf::Blend::Alpha;
    d.quads2d = std::move(quads);
    d.clip2d[0] = x0;
    d.clip2d[1] = y0;
    d.clip2d[2] = x1;
    d.clip2d[3] = y1;
    return d;
}

void scene_quads_2d(Ctx& c) {
    std::vector<nf::Quad2D> q;
    q.push_back(quad2d(8, 8, 104, 104, T_GLYPH, kGsWhite, 0x44, false));
    q.push_back(quad2d(112, 8, 208, 104, T_GLYPH, kGsWhite, 0x44, true));
    q.push_back(quad2d(8, 112, 72, 176, T_CHECK, kGsWhite, 0x44, true, 1, 0, 0, 1));  // espelhada
    q.push_back(quad2d(80, 112, 140, 140, -1, Rgba{0x80, 0x40, 0x20, 0x80}));
    q.push_back(quad2d(80, 144, 140, 176, nf::kQuadNoTexture, Rgba{0x20, 0x90, 0xff, 0x40}));
    q.push_back(quad2d(60, 40, 150, 130, T_SOFT, Rgba{0x80, 0x60, 0x20, 0x80}, 0x48));
    q.push_back(quad2d(150, 60, 230, 140, T_SOFT, Rgba{0x80, 0x80, 0x80, 0x80}, 0x42));
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 4; ++i) {
            q.push_back(quad2d(150.0f + float(i) * 24.0f, 150.0f + float(j) * 20.0f, 174.0f + float(i) * 24.0f,
                               170.0f + float(j) * 20.0f, T_CHECK, kGsWhite, 0x44, true, 0, 0, 0.25f, 0.25f));
        }
    }
    q.push_back(quad2d(212, 112, 252, 144, T_GLYPH, Rgba{0x80, 0x80, 0x80, 0x00}));  // alfa 0: nada
    nf::Quad2D port = quad2d(216, 8, 248, 40, -1);
    port.image = c.r.ui_image(kPortImage);
    port.smooth = true;
    q.push_back(port);
    std::vector<nf::DrawItem> items;
    items.push_back(item2d(std::move(q)));
    items.push_back(item2d({quad2d(0, 0, float(kW), float(kH), T_RAMP)}, 20, 20, 120, 90));  // o recorte
    c.r.render(items, frame_of(default_camera(), {0.18f, 0.2f, 0.25f, 1.0f}));
}

void scene_nove_pedacos(Ctx& c) {
    std::vector<nf::Quad2D> q;
    q.push_back(quad2d(10, 10, 246, 60, T_FRAME));  // a textura inteira esticada, num quadrado só
    Psi2DQuad p{};
    p.x0 = 20;
    p.y0 = 170;
    p.x1 = 490;
    p.y1 = 420;
    p.u0 = 0;
    p.v0 = 0;
    p.u1 = 1;
    p.v1 = 1;
    std::memcpy(p.rgba, kGsWhite.data(), 4);
    p.texture = T_FRAME;
    p.alpha = 0x44;
    p.linear = true;
    Psi2DFrame f;
    f.tex_w = 64;
    f.tex_h = 64;
    f.inset[0] = f.inset[1] = f.inset[2] = f.inset[3] = 12.0f;
    f.aspect = 1.0f;
    const float sx = float(kW) / kPs2ScreenWidth, sy = float(kH) / kPs2ScreenHeight;
    Psi2DQuad pieces[9];
    const int n = psi_2d_nine_slice(p, f, sx / sy, pieces);
    for (int i = 0; i < n; ++i) {
        const Psi2DQuad& s = pieces[i];
        q.push_back(quad2d(s.x0 * sx, s.y0 * sy, s.x1 * sx, s.y1 * sy, T_FRAME, kGsWhite, 0x44, true, s.u0, s.v0, s.u1, s.v1));
    }
    c.report += "nove_pedacos " + std::to_string(n) + " pedacos\n";
    c.r.render({item2d(std::move(q))}, frame_of(default_camera(), {0.05f, 0.05f, 0.08f, 1.0f}));
}

void scene_esmaecimento(Ctx& c) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    nf::DrawItem fade = item2d({quad2d(0, 0, float(kW), float(kH), nf::kQuadNoTexture, Rgba{0x10, 0x20, 0x40, 0x50})});
    fade.fade = 0;
    items.push_back(fade);
    c.r.render(items, frame_of(default_camera()));
}

void scene_rastro(Ctx& c) {
    std::vector<nf::DrawItem> a, b;
    rigid_scene(c, a, -2.0f);
    c.r.render(a, frame_of(default_camera()));
    c.r.screen_blur(0.55f);  // sem o anterior: só guarda
    rigid_scene(c, b, 1.5f);
    c.r.render(b, frame_of(default_camera()));
    c.r.screen_blur(0.55f);  // o anterior x 0,55 + o atual x 0,45
}

void blur_scene(Ctx& c, float amount) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    items.push_back(item2d({quad2d(20, 20, 116, 116, T_GLYPH, kGsWhite, 0x44, false)}));
    c.r.render(items, frame_of(default_camera()));
    c.r.blur_background(amount);
}
void scene_desfoque(Ctx& c) { blur_scene(c, 1.0f); }
void scene_desfoque_meio(Ctx& c) { blur_scene(c, 0.5f); }

void scene_filme(Ctx& c) {
    std::vector<uint8_t> frame(512 * 448 * 4);
    for (int y = 0; y < 448; ++y) {
        for (int x = 0; x < 512; ++x) {
            uint8_t* p = &frame[size_t(y * 512 + x) * 4];
            uint8_t b = u8(x / 2.0f), g = u8(y / 1.8f), r = u8(128.0f + 100.0f * std::sin(float(x + y) * 0.02f));
            if (x % 64 < 4) b = g = r = (x / 64) % 2 ? 250 : 10;
            if (x < 96 && y < 96 && ((x / 8 + y / 8) & 1)) b = g = r = 255;
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = 0x80;
        }
    }
    c.r.set_movie_frame(frame.data(), 512, 448, 1);
    const float rect[4] = {24.0f, 12.0f, 232.0f, 180.0f};
    c.r.set_movie_background(true, rect);
    c.r.render({item2d({quad2d(40, 150, 216, 172, T_GLYPH, Rgba{0x80, 0x80, 0x40, 0x80}, 0x44, true, 0, 0, 6, 1)})},
               frame_of(default_camera(), {0.3f, 0.0f, 0.0f, 1.0f}));
    const float none[4] = {0, 0, 0, 0};
    c.r.set_movie_background(false, none);
}

void a2c_scene(Ctx& c) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    const uint64_t leaf_test = 0x507fd;  // GREATER 0x7f com Z GEQUAL (as folhas e grades do jogo)
    for (int i = 0; i < 6; ++i) {
        const M4 w = mul(mul(rot_x(0.3f * float(i % 3) - 0.3f), rot_y(0.5f * float(i) - 1.2f)),
                         translate(-2.2f + float(i) * 0.9f, 1.4f + 0.3f * float(i % 2), 0.8f));
        items.push_back(item(c.r.add_mesh(quad_mesh(-0.6f, -0.6f, 0.6f, 0.6f, 0.0f, T_LEAF, gs_of(0x44, leaf_test, false))), w,
                             nf::Blend::Alpha));
    }
    const uint64_t soft_test = 1 | (5u << 1) | (0x40u << 4) | kZGequal;  // GEQUAL 0x40, sem mistura
    for (int i = 0; i < 3; ++i) {
        items.push_back(item(c.r.add_mesh(quad_mesh(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f, T_SOFT, gs_of(0x2a, soft_test, false))),
                             mul(rot_y(0.4f * float(i)), translate(-1.5f + float(i) * 1.5f, 0.6f, 1.6f))));
    }
    c.r.render(items, frame_of(default_camera()));
}
void scene_msaa_a2c(Ctx& c) {
    c.r.set_scene_quality(100, 4);
    c.r.set_alpha_to_coverage(true);
    a2c_scene(c);
}
void scene_msaa_neblina(Ctx& c) {
    c.r.set_scene_quality(100, 4);
    std::vector<nf::DrawItem> items;
    fog_items(c, items);
    c.r.render(items, frame_of(fog_camera(), {0.1f, 0.1f, 0.1f, 1.0f}));
}
void scene_escala_200(Ctx& c) {
    c.r.set_scene_quality(200, 1);
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    items.push_back(item2d({quad2d(150, 10, 246, 106, T_GLYPH, kGsWhite, 0x44, false)}));
    c.r.render(items, frame_of(default_camera()));
}
void scene_escala_msaa(Ctx& c) {
    c.r.set_scene_quality(150, 4);
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    c.r.render(items, frame_of(default_camera()));
}

void filter_scene(Ctx& c, nf::TextureFilter f) {
    c.r.set_texture_filter(f);
    MeshBuilder floor;
    floor.begin(T_FINE);
    floor.grid({-6, 0, 2}, {12, 0, 0}, {0, 0, -62}, 2, 16, 6, 31, {0, 1, 0});
    floor.end();
    c.r.render({item(c.r.add_mesh(floor.m))}, frame_of(camera({0.0f, 0.8f, 2.0f}, {0.0f, 0.3f, -10.0f})));
}
void scene_filtro_bilinear(Ctx& c) { filter_scene(c, nf::TextureFilter::Bilinear); }
void scene_filtro_trilinear(Ctx& c) { filter_scene(c, nf::TextureFilter::Trilinear); }
void scene_filtro_aniso16(Ctx& c) { filter_scene(c, nf::TextureFilter::Aniso16); }

void scene_tela_carga(Ctx& c) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const nf::FrameParams fp = frame_of(default_camera(), {0.0f, 0.0f, 0.0f, 1.0f});
    c.r.clear_target(black);
    c.r.render_2d({item2d({quad2d(16, 16, 240, 150, T_ENV), quad2d(30, 120, 70, 145, T_GLYPH, kGsWhite, 0x44, false),
                           quad2d(70, 120, 110, 145, T_GLYPH, kGsWhite, 0x44, false)})},
                  fp);
    c.r.save_loading_snapshot();
    c.r.render_2d({item2d({quad2d(20, 160, 60, 184, T_SOFT, Rgba{0x80, 0x20, 0x20, 0x80})})}, fp);  // some na volta
    const bool restored = c.r.restore_loading_snapshot();
    const nf::GpuImageRef blob = c.r.keep_texture(T_SOFT);
    nf::Quad2D b = quad2d(80, 160, 120, 184, -1, Rgba{0x20, 0x80, 0x20, 0x80});
    b.image = blob.get();
    c.r.render_2d({item2d({b})}, fp);
    c.report += std::string("tela_carga snapshot ") + (restored ? "restaurado" : "FALHOU") + (blob ? " textura guardada\n" : " SEM TEXTURA\n");
}

// ---------------------------------------------------------------- DDS sintéticos

void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) { std::memcpy(&f[at], &v, 4); }

std::vector<uint8_t> dds_header(uint32_t w, uint32_t h, uint32_t mips, uint32_t fourcc, bool rgba8) {
    std::vector<uint8_t> f(128, 0);
    std::memcpy(f.data(), "DDS ", 4);
    put32(f, 4, 124);
    put32(f, 8, 0x1007 | (mips > 1 ? 0x20000u : 0u));
    put32(f, 12, h);
    put32(f, 16, w);
    put32(f, 28, mips);
    put32(f, 76, 32);
    if (rgba8) {
        put32(f, 80, 0x41);  // RGB | ALPHAPIXELS
        put32(f, 88, 32);
        put32(f, 92, 0xff);
        put32(f, 96, 0xff00);
        put32(f, 100, 0xff0000);
        put32(f, 104, 0xff000000u);
    } else {
        put32(f, 80, 4);  // FOURCC
        put32(f, 84, fourcc);
    }
    put32(f, 108, 0x1000 | (mips > 1 ? 0x400008u : 0u));
    return f;
}

uint16_t rgb565(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

// Um bloco BC1 de quatro cores (c0 > c1) com um degradê na diagonal.
void bc1_block(std::vector<uint8_t>& out, uint16_t c0, uint16_t c1, int phase) {
    if (c0 <= c1) std::swap(c0, c1);
    if (c0 == c1) c0 = uint16_t(c0 + 1);
    uint32_t idx = 0;
    static const uint32_t order[4] = {0, 2, 3, 1};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) idx |= order[(x + y + phase) % 4] << (2 * (y * 4 + x));
    }
    out.push_back(uint8_t(c0));
    out.push_back(uint8_t(c0 >> 8));
    out.push_back(uint8_t(c1));
    out.push_back(uint8_t(c1 >> 8));
    for (int k = 0; k < 4; ++k) out.push_back(uint8_t(idx >> (8 * k)));
}

std::vector<uint8_t> bc1_level(uint32_t w, uint32_t h, int level) {
    std::vector<uint8_t> out;
    const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const int r = int(bx * 255 / std::max(1u, bw)), g = int(by * 255 / std::max(1u, bh));
            bc1_block(out, rgb565(r, g, 64 + level * 30), rgb565(255 - r, 40, 255 - g), int(bx + by));
        }
    }
    return out;
}

std::vector<uint8_t> bc3_level(uint32_t w, uint32_t h) {
    std::vector<uint8_t> out;
    const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            out.push_back(255);  // a0 > a1: oito alfas
            out.push_back(0);
            uint64_t bits = 0;
            for (int i = 0; i < 16; ++i) bits |= uint64_t((i + int(bx)) % 8) << (3 * i);
            for (int k = 0; k < 6; ++k) out.push_back(uint8_t(bits >> (8 * k)));
            bc1_block(out, rgb565(255, int(by * 255 / bh), 40), rgb565(30, 60, int(bx * 255 / bw)), 0);
        }
    }
    return out;
}

bool write_file(const std::wstring& path, const std::vector<uint8_t>& data) {
    FILE* f = nf::os::open_file(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

uint32_t fourcc(const char* s) { return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24; }

void scene_dds(Ctx& c) {
    // RGBA8 de um nível (copiado e com os mipmaps gerados), BC1 com todos os níveis, BC1 de um nível (descomprimido na
    // GPU e com os mipmaps gerados) e BC3 com todos.
    std::vector<uint8_t> rgba = dds_header(64, 64, 1, 0, true);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            rgba.insert(rgba.end(), {uint8_t(on ? 240 : 30), uint8_t(x * 4), uint8_t(y * 4), uint8_t(0x80)});
        }
    }
    std::vector<uint8_t> bc1m = dds_header(128, 128, 8, fourcc("DXT1"), false);
    for (uint32_t m = 0, s = 128; m < 8; ++m, s = std::max(1u, s / 2)) {
        const std::vector<uint8_t> l = bc1_level(s, s, int(m));
        bc1m.insert(bc1m.end(), l.begin(), l.end());
    }
    std::vector<uint8_t> bc1 = dds_header(128, 128, 1, fourcc("DXT1"), false);
    {
        const std::vector<uint8_t> l = bc1_level(128, 128, 0);
        bc1.insert(bc1.end(), l.begin(), l.end());
    }
    std::vector<uint8_t> bc3 = dds_header(64, 64, 7, fourcc("DXT5"), false);
    for (uint32_t m = 0, s = 64; m < 7; ++m, s = std::max(1u, s / 2)) {
        const std::vector<uint8_t> l = bc3_level(s, s);
        bc3.insert(bc3.end(), l.begin(), l.end());
    }
    const std::vector<uint8_t>* files[4] = {&rgba, &bc1m, &bc1, &bc3};
    const char* names[4] = {"rgba8_1nivel.dds", "bc1_niveis.dds", "bc1_1nivel.dds", "bc3_niveis.dds"};
    std::string loaded;
    std::vector<nf::DrawItem> items;
    for (int i = 0; i < 4; ++i) {
        const std::wstring path = nf::os::join(c.out, std::wstring(names[i], names[i] + std::strlen(names[i])));
        const bool ok = write_file(path, *files[i]) && c.r.set_texture_dds(20 + i, path);
        loaded += ok ? "1" : "0";
        nf::os::remove_file(path);
        // No chão (diminuídas: os mipmaps) e na tela (aumentadas).
        MeshBuilder strip;
        strip.begin(20 + i);
        strip.grid({-4.0f + float(i) * 2.0f, 0, 3}, {1.9f, 0, 0}, {0, 0, -40}, 1, 8, 1, 20, {0, 1, 0});
        strip.end();
        items.push_back(item(c.r.add_mesh(strip.m)));
        items.push_back(item2d({quad2d(8.0f + float(i) * 62.0f, 8, 64.0f + float(i) * 62.0f, 64, 20 + i)}));
    }
    c.report += "dds carregados " + loaded + "\n";
    c.r.render(items, frame_of(camera({0.0f, 1.2f, 4.0f}, {0.0f, 0.2f, -6.0f})));
}

// Renderer::update_texture (o fundo animado da pausa, platform/psi_woman.h): o slot 30 nasce com uma textura e uma
// substituta HD (DDS RGBA8 magenta), o 31 com 32 x 32. Um quadro com os dois; depois o 30 recebe outro conteúdo (a
// substituta sai e os mipmaps do chão são refeitos) e o 31 outro tamanho (vira uma textura nova); o segundo quadro os
// desenha, e com ele ainda aberto o 30 muda de novo e um 2D por cima o mostra: os desenhos de antes ficam com o conteúdo
// de antes (o contexto imediato do D3D11; no Metal, a cópia no command buffer do quadro).
void scene_textura_trocada(Ctx& c) {
    const auto stripes = [](int phase, uint8_t r, uint8_t g, uint8_t b) {
        return make_tex(64, 64, [=](int x, int y) {
            const bool on = ((x + y + phase) / 16) & 1;
            const float k = on ? 1.0f : 0.35f;
            return std::array<uint8_t, 4>{u8(r * k), u8(g * k + float(y)), u8(b * k), 0x80};
        });
    };
    c.r.set_texture(30, make_tex(64, 64, [](int x, int y) {
        return std::array<uint8_t, 4>{uint8_t(((x / 8 + y / 8) & 1) ? 60 : 200), 90, 230, 0x80};
    }));
    std::vector<uint8_t> dds = dds_header(64, 64, 1, 0, true);
    for (int i = 0; i < 64 * 64; ++i) dds.insert(dds.end(), {250, 0, 250, 0x80});
    const std::wstring path = nf::os::join(c.out, L"trocada_hd.dds");
    const bool hd = write_file(path, dds) && c.r.set_texture_dds(30, path);
    nf::os::remove_file(path);
    c.r.set_texture(31, make_tex(32, 32, [](int x, int y) {
        return std::array<uint8_t, 4>{uint8_t(x * 8), uint8_t(y * 8), 40, 0x80};
    }));
    MeshBuilder strip;
    strip.begin(30);
    strip.grid({-3.0f, 0, 3}, {6.0f, 0, 0}, {0, 0, -40}, 1, 8, 2, 6, {0, 1, 0});
    strip.end();
    const int floor = c.r.add_mesh(strip.m);
    const auto items = [&](int mesh) {
        std::vector<nf::DrawItem> v;
        v.push_back(item(mesh));
        v.push_back(item2d({quad2d(8, 8, 72, 72, 30), quad2d(80, 8, 144, 72, 31)}));
        return v;
    };
    const Camera cam = camera({0.0f, 1.2f, 4.0f}, {0.0f, 0.2f, -6.0f});
    c.r.render(items(floor), frame_of(cam));
    c.r.present();
    c.r.update_texture(30, stripes(0, 240, 120, 40));
    c.r.update_texture(31, stripes(3, 40, 200, 120));  // outro tamanho: uma textura nova no slot
    c.r.render(items(floor), frame_of(cam));
    c.r.update_texture(30, stripes(5, 90, 60, 250));
    c.r.render_2d({item2d({quad2d(152, 8, 216, 72, 30)})}, frame_of(cam));
    c.report += std::string("textura_trocada substituta ") + (hd ? "1" : "0") + "\n";
}

// ---------------------------------------------------------------- o desenho vetorial pela fachada (etapa 3d-B)

// As cenas do desenho vetorial: um renderizador próprio, maior (os textos dos menus Modern na tela virtual de 1920 x 1080
// ficam com um terço do tamanho, a camada HD no tamanho da janela).
constexpr int kVW = 960, kVH = 540;

const Camera& wide_camera() {
    static const Camera c = camera({0.0f, 1.6f, 5.5f}, {0.0f, 0.9f, 0.0f}, 60.0f, 0.05f, 20000.0f, float(kVW) / float(kVH));
    return c;
}

// A pausa Modern como o jogo a desenha: a cena, o desfoque do fundo e a lista por cima (a do caso tela_pausa da
// comparação do desenho vetorial, medida pelo próprio Renderer).
void scene_modern_pausa(Ctx& c) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    c.r.render(items, frame_of(wide_camera()));
    c.r.blur_background(1.0f);
    const nf::ui::Canvas cv = nf::ui::make_canvas(kVW, kVH);
    const nf::ui::Measure m = [&c](const std::wstring& t, const nf::ui::TextStyle& st) { return c.r.modern_measure(t, st); };
    c.r.draw_modern(nf::vcase::pause_list(m, cv), cv);
}

// Uma substituta SVG (as de hd_ui/svg, que vector_cases::load_assets acrescenta ao map.json) num retângulo de altura h
// com a proporção do viewBox; sem ela, a textura T_CHECK no lugar (como a original do jogo).
nf::UiHdImage hd_image(const nf::vcase::Assets& a, const char* file, float x0, float y0, float h) {
    nf::UiHdImage im;
    float aspect = 1.0f;
    for (size_t i = 0; i < a.files.size(); ++i) {
        if (a.files[i] != std::string("svg/") + file) continue;
        im.asset = a.first_svg + int(i);
        const nf::UiHdImageEntry& e = a.config.images[size_t(im.asset)];
        if (e.key.has_rect && e.key.rect[3] > 0.0f) aspect = e.key.rect[2] / e.key.rect[3];
    }
    im.x0 = x0;
    im.y0 = y0;
    im.x1 = x0 + std::round(h * aspect);
    im.y1 = y0 + h;
    im.u1 = im.v1 = 1.0f;
    im.orig_uv[2] = im.orig_uv[3] = 1.0f;
    im.rgba[0] = im.rgba[1] = im.rgba[2] = im.rgba[3] = 0x80;
    im.texture = T_CHECK;
    im.asset_w = int(im.x1 - im.x0);
    im.asset_h = int(h);
    im.clip[2] = float(kVW);
    im.clip[3] = float(kVH);
    return im;
}

// Um lote da camada HD (um UiHdBatch, como o ui_hd_build monta de uma chamada de psiDrawSprites) por cima da cena: os
// textos do caso hd_textos (contorno, sombra, trechos de cor, alinhamentos, compressão, recorte), as oito teclas do caso
// hd_teclas na faixa de baixo e quatro substitutas SVG na coluna da direita, intercaladas (o renderizador junta as
// imagens seguidas num item 2D e os textos e teclas seguidos numa imagem da UiVectorCanvas).
void scene_hd_lote(Ctx& c) {
    std::vector<nf::DrawItem> items;
    rigid_scene(c, items);
    auto b = std::make_shared<nf::UiHdBatch>();
    const nf::vcase::Case t = nf::vcase::hd_case(false), k = nf::vcase::hd_case(true);
    b->images.push_back(hd_image(*c.assets, "health_ring.svg", 812, 24, 120));
    b->images.push_back(hd_image(*c.assets, "crosshair.svg", 832, 168, 88));
    b->images.push_back(hd_image(*c.assets, "ps2_triangle.svg", 852, 284, 48));
    b->images.push_back(hd_image(*c.assets, "results_007.svg", 808, 360, 56));
    b->texts = t.texts;
    b->ops.push_back(nf::UiHdOp{nf::UiHdOp::Image, 0});
    for (const nf::UiHdOp& op : t.order) b->ops.push_back(op);  // os índices já são posições em texts
    b->ops.push_back(nf::UiHdOp{nf::UiHdOp::Image, 1});
    for (size_t i = 0; i < k.keys.size() && i < 8; ++i) {
        nf::UiHdKey key = k.keys[i];
        key.y0 += 420.0f;
        key.y1 += 420.0f;
        key.clip[0] = key.clip[1] = 0.0f;
        key.clip[2] = float(kVW);
        key.clip[3] = float(kVH);
        b->ops.push_back(nf::UiHdOp{nf::UiHdOp::Key, uint32_t(b->keys.size())});
        b->keys.push_back(key);
    }
    b->ops.push_back(nf::UiHdOp{nf::UiHdOp::Image, 2});
    b->ops.push_back(nf::UiHdOp{nf::UiHdOp::Image, 3});
    nf::DrawItem d;
    d.blend = nf::Blend::Alpha;
    d.ui = b;
    d.clip2d[2] = float(kVW);
    d.clip2d[3] = float(kVH);
    items.push_back(std::move(d));
    c.r.render(items, frame_of(wide_camera()));
}

// A pasta da camada HD do teste: a Rajdhani do repositório como as "fontes do usuário" (fonts/, que os dois sistemas usam
// antes das suas) e as SVG (svg/), montada na pasta temporária do sistema. Com ela as três fontes do map.json viram a
// Rajdhani: no macOS a fonte do map.json é outra (DIN Alternate no lugar da Bahnschrift) e a diferença de uma fonte
// trocada esconderia a do desenho (um lote sem os textos daria quase o mesmo PSNR); a troca de fonte a comparação do
// desenho vetorial já mede (nf_vector_check, hd_textos). Vazia se falhar.
std::wstring hd_test_dir(const std::wstring& hd_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src(hd_dir);
    const fs::path dst = fs::temp_directory_path(ec) / "nf_render_check_hd";
    if (ec) return {};
    fs::create_directories(dst / "fonts", ec);
    fs::create_directories(dst / "svg", ec);
    int fonts = 0, svgs = 0;
    const auto copy_all = [&](const fs::path& from, const fs::path& to, const char* ext, int& count) {
        std::error_code e;
        for (fs::directory_iterator it(from, e), end; !e && it != end; it.increment(e)) {
            std::error_code c;
            if (it->path().extension() == ext &&
                fs::copy_file(it->path(), to / it->path().filename(), fs::copy_options::overwrite_existing, c)) {
                ++count;
            }
        }
    };
    copy_all(src / "fonts_ofl" / "rajdhani", dst / "fonts", ".ttf", fonts);
    copy_all(src / "svg", dst / "svg", ".svg", svgs);
    return fonts > 0 && svgs > 0 ? dst.wstring() : std::wstring();
}

struct Scene {
    const char* name;
    void (*run)(Ctx&);
    float psnr_min;   // dB
    float pct32_max;  // % dos pixels com um canal a mais de 32 da referência
    int width = kW, height = kH;  // kVW x kVH: no renderizador das cenas do desenho vetorial
};

// Os limites (13/10/2026): 6 dB abaixo do menor PSNR medido entre o Direct3D 11 do NVIDIA (as referências) e duas outras
// GPUs, o Direct3D 11 do Radeon integrado desta máquina e o Metal da GPU virtual do macOS da CI (um erro quadrático médio
// quatro vezes maior), e a fração de pixels com mais de 32 de diferença em 0,5% (1% a 1,5% onde o fabricante decide o
// desenho: o alpha-to-coverage, o filtro bilinear das bordas dos mipmaps, o anisotrópico). As medidas estão em
// docs/port.md, "macOS: o renderizador Metal".
const Scene kScenes[] = {
    {"rigido", scene_rigido, 39.0f, 0.5f},
    {"luzes", scene_luzes, 52.0f, 0.5f},
    {"ambiente", scene_ambiente, 40.0f, 0.5f},
    {"tweak", scene_tweak, 39.0f, 0.5f},
    {"skinning", scene_skinning, 38.0f, 0.5f},
    {"morph", scene_morph, 43.0f, 0.5f},
    {"misturas", scene_misturas, 44.0f, 0.5f},
    {"teste_alfa", scene_teste_alfa, 37.0f, 0.5f},
    {"z", scene_z, 45.0f, 0.5f},
    {"neblina", scene_neblina, 38.0f, 0.5f},
    {"limpa_z", scene_limpa_z, 45.0f, 0.5f},
    {"particulas", scene_particulas, 39.0f, 0.5f},
    {"cacos", scene_cacos, 41.0f, 0.5f},
    {"quads_2d", scene_quads_2d, 53.0f, 0.5f},
    {"nove_pedacos", scene_nove_pedacos, 50.0f, 0.5f},
    {"esmaecimento", scene_esmaecimento, 47.0f, 0.5f},
    {"rastro", scene_rastro, 39.0f, 0.5f},
    {"desfoque", scene_desfoque, 49.0f, 0.5f},
    {"desfoque_meio", scene_desfoque_meio, 44.0f, 0.5f},
    {"filme", scene_filme, 58.0f, 0.5f},
    {"msaa_a2c", scene_msaa_a2c, 33.0f, 1.5f},
    {"msaa_neblina", scene_msaa_neblina, 38.0f, 0.5f},
    {"escala_200", scene_escala_200, 47.0f, 0.5f},
    {"escala_msaa", scene_escala_msaa, 46.0f, 0.5f},
    {"filtro_bilinear", scene_filtro_bilinear, 32.0f, 1.5f},
    {"filtro_trilinear", scene_filtro_trilinear, 42.0f, 0.5f},
    {"filtro_aniso16", scene_filtro_aniso16, 29.0f, 1.0f},
    {"tela_carga", scene_tela_carga, 41.0f, 0.5f},
    {"dds", scene_dds, 33.0f, 0.5f},
    {"textura_trocada", scene_textura_trocada, 47.0f, 0.5f},
    // O desenho vetorial pela fachada: no macOS o CoreGraphics e o CoreText (render/ui_vector_cg.mm) compostos pelo
    // Metal, contra o Direct2D sobre o Direct3D 11. A diferença é a do antisserrilhado do texto (a mesma fonte), igual em
    // toda rodada; o Direct2D no Radeon dá 53,6 e 54,2 dB. Sem a camada a imagem muda muito mais: a pausa sem a lista dá
    // 12,7 dB e 95,8% dos pixels a mais de 32; o lote sem os textos (as SVG e as teclas ficam), 19,8 dB e 3,6%.
    // Os limites são provisórios até a medida do Metal com a Rajdhani na camada HD.
    {"modern_pausa", scene_modern_pausa, 30.0f, 1.5f, kVW, kVH},
    {"hd_lote", scene_hd_lote, 24.0f, 2.0f, kVW, kVH},
};

void reset(nf::Renderer& r) {
    r.reset_resources();
    r.set_scene_quality(100, 1);
    r.set_texture_filter(nf::TextureFilter::Aniso8);
    r.set_alpha_to_coverage(true);
    r.use_hd(true);
    r.screen_blur(0.0f);
    const float none[4] = {0, 0, 0, 0};
    r.set_movie_background(false, none);
}

void load_textures(Ctx& c) {
    for (int i = 0; i < kTexCount; ++i) c.r.set_texture(i, c.tex[size_t(i)]);
}

// ---------------------------------------------------------------- a comparação

struct Diff {
    bool ok = false;
    int max = 0;
    double mean = 0.0, psnr = 0.0, pct8 = 0.0, pct32 = 0.0;
};

bool read_png(const std::wstring& path, int& w, int& h, std::vector<uint8_t>& bgra) {
    return nf::read_image_bgra(path, 16384, &w, &h, &bgra);
}

Diff compare(const std::wstring& a_path, const std::wstring& b_path, const std::wstring& diff_path) {
    Diff d;
    int aw = 0, ah = 0, bw = 0, bh = 0;
    std::vector<uint8_t> a, b;
    if (!read_png(a_path, aw, ah, a) || !read_png(b_path, bw, bh, b) || aw != bw || ah != bh) return d;
    std::vector<uint8_t> vis(a.size());
    double sq = 0.0, sum = 0.0;
    size_t over8 = 0, over32 = 0;
    const size_t pixels = size_t(aw) * size_t(ah);
    for (size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int k = 0; k < 3; ++k) {
            const int e = std::abs(int(a[p * 4 + size_t(k)]) - int(b[p * 4 + size_t(k)]));
            worst = std::max(worst, e);
            sq += double(e) * double(e);
            sum += e;
            vis[p * 4 + size_t(k)] = uint8_t(std::min(255, e * 4));
        }
        vis[p * 4 + 3] = 255;
        d.max = std::max(d.max, worst);
        if (worst > 8) ++over8;
        if (worst > 32) ++over32;
    }
    const double n = double(pixels) * 3.0;
    d.mean = sum / n;
    const double mse = sq / n;
    d.psnr = mse > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
    d.pct8 = 100.0 * double(over8) / double(pixels);
    d.pct32 = 100.0 * double(over32) / double(pixels);
    nf::write_png_bgra(diff_path, aw, ah, vis.data());
    d.ok = true;
    return d;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::map<std::string, std::string> read_counts(const std::wstring& path) {
    std::map<std::string, std::string> out;
    FILE* f = nf::os::open_file(path, "r");
    if (f == nullptr) return out;
    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        const size_t sp = s.find(' ');
        if (sp != std::string::npos) out[s.substr(0, sp)] = s.substr(sp + 1);
    }
    std::fclose(f);
    return out;
}

#if NF_RENDER_CHECK_WINDOW
// --janela: a mesma cena numa janela de verdade (o drawable da CAMetalLayer, a cadeia de troca do DXGI) e fora da tela, no
// mesmo tamanho: o quadro tem de ser o mesmo.
int window_check(const std::wstring& out, const std::vector<nf::Texture>& tex, std::string& report) {
    try {
        nf::Window w(L"nf_render_check", 320, 240, false);
        for (int i = 0; i < 10; ++i) w.pump();
        const int ww = w.width(), wh = w.height();
        nf::Renderer wr(w.native_handle(), ww, wh, true, false);
        Ctx c{wr, out, tex, {}};
        load_textures(c);
        std::vector<nf::DrawItem> items;
        rigid_scene(c, items);
        const auto t0 = std::chrono::steady_clock::now();
        const int frames = 30;
        for (int i = 0; i < frames; ++i) {
            if (!w.pump()) break;
            wr.render(items, frame_of(default_camera()));
            wr.present();
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        wr.render(items, frame_of(default_camera()));
        const std::wstring shot = nf::os::join(out, L"janela.png");
        wr.save_png(shot);
        wr.present();
        void* hidden = nf::create_remix_hidden_window(ww, wh);
        nf::Renderer off(hidden, ww, wh, false, true);
        Ctx o{off, out, tex, {}};
        load_textures(o);
        std::vector<nf::DrawItem> items2;
        rigid_scene(o, items2);
        off.render(items2, frame_of(default_camera()));
        const std::wstring ref = nf::os::join(out, L"janela_fora.png");
        off.save_png(ref);
        const Diff d = compare(shot, ref, nf::os::join(out, L"janela_dif.png"));
        char buf[256];
        std::snprintf(buf, sizeof(buf), "janela %dx%d %d quadros em %.2f s (%.1f por segundo) ocultos %llu | contra fora da tela: max %d psnr %.1f\n",
                      ww, wh, frames, secs, double(frames) / std::max(secs, 1e-6),
                      static_cast<unsigned long long>(wr.counters().occluded), d.max, d.psnr);
        report += buf;
        std::printf("%s", buf);
        return d.ok && d.max <= 2 ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("janela: pulada (%s)\n", e.what());
        report += std::string("janela pulada: ") + e.what() + "\n";
        return 0;
    }
}
#else
// Sem janela nesta build (a headless do Linux, só compilada).
int window_check(const std::wstring&, const std::vector<nf::Texture>&, std::string& report) {
    report += "janela pulada: sem janela nesta build\n";
    return 0;
}
#endif

int run(const std::vector<std::string>& args) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::wstring out, ref;
    std::string only;
    bool window = false;
    std::string adapter;
    std::wstring hd_dir = L"hd_ui";
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) {
            out = widen(args[++i]);
        } else if (args[i] == "--compare" && i + 1 < args.size()) {
            ref = widen(args[++i]);
        } else if (args[i] == "--scene" && i + 1 < args.size()) {
            only = args[++i];
        } else if (args[i] == "--adapter" && i + 1 < args.size()) {
            adapter = args[++i];
        } else if (args[i] == "--janela") {
            window = true;
        } else if (args[i] == "--hd-ui" && i + 1 < args.size()) {
            hd_dir = widen(args[++i]);
        } else {
            std::printf("uso: nf_render_check --out <pasta> [--compare <referências>] [--scene <nome>] [--janela] [--hd-ui <pasta>]\n");
            return 2;
        }
    }
    if (out.empty()) {
        std::printf("uso: nf_render_check --out <pasta> [--compare <referências>] [--scene <nome>] [--janela] [--hd-ui <pasta>]\n");
        return 2;
    }
    if (!nf::os::is_dir(out)) nf::os::make_dir(out);
    const std::vector<nf::Texture> tex = textures();
    void* hidden = nf::create_remix_hidden_window(kW, kH);  // o Direct3D 11 precisa de uma janela, mesmo fora da tela
    // --adapter: o adaptador como no [video] adapter (integrated, discrete, um índice, um nome; platform/gpu_adapter.h).
    std::vector<nf::AdapterInfo> adapters;
    const nf::AdapterInfo* chosen = nullptr;
    nf::AdapterPreference pref;
    if (!adapter.empty() && nf::parse_adapter_preference(adapter, &pref)) {
        adapters = nf::enumerate_adapters(true);
        const int k = nf::choose_adapter(adapters, pref);
        if (k >= 0) chosen = &adapters[size_t(k)];
    }
    nf::Renderer r(hidden, kW, kH, false, true, chosen);
    std::printf("adaptador %s (%s)\n", r.adapter().name.c_str(), nf::adapter_kind_name(r.adapter()));
    const std::vector<uint8_t> port = port_image();
    r.set_ui_image(kPortImage, port.data(), 64, 64);
    Ctx c{r, out, tex, {}};
    std::string counts;
    int failures = 0;
    std::string report = "adaptador " + r.adapter().name + "\n";
    const std::map<std::string, std::string> ref_counts = ref.empty() ? std::map<std::string, std::string>{}
                                                                      : read_counts(nf::os::join(ref, L"contagens.txt"));
    // As cenas do desenho vetorial: o renderizador delas (kVW x kVH), criado na primeira, com o desenho dos menus Modern e
    // a camada HD ligados (a pasta hd_ui; as SVG acrescentadas ao map.json como substitutas).
    const nf::vcase::Assets assets = nf::vcase::load_assets(hd_dir);
    nf::UiHdConfig hd_config = assets.config;
    for (nf::UiHdFont& f : hd_config.fonts) {
        f.family = L"Rajdhani";
        f.family_macos = {L"Rajdhani"};
    }
    std::unique_ptr<nf::Renderer> vr;
    std::unique_ptr<Ctx> vc;
    bool vector_ok = false;
    for (const Scene& s : kScenes) {
        if (!only.empty() && only != s.name) continue;
        nf::Renderer* rr = &r;
        Ctx* cc = &c;
        if (s.width == kVW && s.height == kVH) {
            if (vr == nullptr) {
                void* vh = nf::create_remix_hidden_window(kVW, kVH);
                vr = std::make_unique<nf::Renderer>(vh, kVW, kVH, false, true, chosen);
                const bool modern = vr->set_modern_ui(hd_dir);
                const std::wstring hd_test = hd_test_dir(hd_dir);
                if (assets.error.empty() && !hd_test.empty()) vr->set_ui_hd(&hd_config, hd_test);
                vector_ok = modern && vr->ui_hd_ok() && !assets.files.empty();
                vc.reset(new Ctx{*vr, out, tex, {}, &assets});
                std::printf("desenho vetorial: modern %s | hd %s | svg %zu %s\n", modern ? "ok" : "FALHOU",
                            vr->ui_hd_describe().c_str(), assets.files.size(), assets.error.c_str());
            }
            if (!vector_ok) {
                std::printf("%-16s SEM O DESENHO VETORIAL\n", s.name);
                report += std::string(s.name) + " sem o desenho vetorial\n";
                ++failures;
                continue;
            }
            rr = vr.get();
            cc = vc.get();
        }
        reset(*rr);
        load_textures(*cc);
        s.run(*cc);
        const std::wstring png = nf::os::join(out, widen(std::string(s.name) + ".png"));
        rr->save_png(png);
        rr->present();
        char cbuf[128];
        std::snprintf(cbuf, sizeof(cbuf), "env %u a2c %u", rr->env_parts(), rr->alpha_coverage_parts());
        counts += std::string(s.name) + " " + cbuf + "\n";
        if (ref.empty()) {
            std::printf("%-16s gravada (%s)\n", s.name, cbuf);
            continue;
        }
        const Diff d = compare(png, nf::os::join(ref, widen(std::string(s.name) + ".png")),
                               nf::os::join(out, widen(std::string(s.name) + "_dif.png")));
        const auto rc = ref_counts.find(s.name);
        const bool counts_ok = rc == ref_counts.end() || rc->second == cbuf;
        const bool ok = d.ok && d.psnr >= s.psnr_min && d.pct32 <= s.pct32_max && counts_ok;
        if (!ok) ++failures;
        char line[320];
        std::snprintf(line, sizeof(line),
                      "%-16s psnr %6.2f dB  max %3d  media %6.3f  acima8 %6.2f%%  acima32 %6.2f%%  limite psnr>=%.0f acima32<=%.1f%%  %s%s %s\n",
                      s.name, d.psnr, d.max, d.mean, d.pct8, d.pct32, double(s.psnr_min), double(s.pct32_max), cbuf,
                      counts_ok ? "" : " (DIFERE da referencia)", ok ? "ok" : (d.ok ? "FALHOU" : "SEM REFERENCIA"));
        std::printf("%s", line);
        report += line;
    }
    report += c.report;
    std::printf("%s", c.report.c_str());
    if (vc != nullptr) {
        report += vc->report;
        report += "desenho vetorial: " + vr->modern_describe() + "\n";
        std::printf("desenho vetorial: %s\n", vr->modern_describe().c_str());
    }
    if (window) failures += window_check(out, tex, report);
    FILE* f = nf::os::open_file(nf::os::join(out, L"contagens.txt"), "w");
    if (f != nullptr) {
        std::fputs(counts.c_str(), f);
        std::fclose(f);
    }
    if (!ref.empty()) {
        FILE* rf = nf::os::open_file(nf::os::join(out, L"relatorio.txt"), "w");
        if (rf != nullptr) {
            std::fputs(report.c_str(), rf);
            std::fclose(rf);
        }
        std::printf("%s\n", failures == 0 ? "conformidade: tudo dentro dos limites" : "conformidade: FALHAS");
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace

#if NF_RENDER_CHECK_APP
int nf_app_main(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        std::string s;
        for (const wchar_t* p = argv[i]; *p != 0; ++p) s.push_back(*p < 128 ? char(*p) : '?');
        args.push_back(s);
    }
    try {
        return run(args);
    } catch (const std::exception& e) {
        std::printf("erro: %s\n", e.what());
        return 3;
    }
}
#else
int main(int argc, char** argv) {
    return nf::run_main([&] {
        std::vector<std::string> args(argv, argv + argc);
        try {
            return run(args);
        } catch (const std::exception& e) {
            std::printf("erro: %s\n", e.what());
            return 3;
        }
    });
}
#endif
