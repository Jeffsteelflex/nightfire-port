// Testes dos modelos com skinning: a conta do skinning (nf::skin_vertex, a mesma do shader), a
// decodificação do bloco 0x2d com a estrutura de skinning (nf::decode_gfx) sobre um bloco sintético
// montado como os do disco, e o registro de psiDrawSkinObjectMatrix (src/platform/psi_draw.cpp).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "data/map_item.h"
#include "engine/anim.h"
#include "engine/game.h"
#include "engine/object.h"
#include "engine/view.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

using nf::SkinnedVertex;

void set_translation(float* m, float x, float y, float z) {
    std::memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

// ---------------------------------------------------------------- bloco 0x2d sintético

struct Writer {
    std::vector<uint8_t> b;
    size_t pos() const { return b.size(); }
    void u32(uint32_t v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + 4);
    }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void f32(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void put32(size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
    void pad_to(size_t at) {
        if (b.size() < at) b.resize(at, 0);
    }
};

// Etiqueta DMA: qwc, id (1 CNT, 3 REF, 6 RET), endereço de 31 bits, dois códigos VIF.
void dma_tag(Writer& w, uint32_t qwc, uint32_t id, uint32_t addr, uint32_t vif0, uint32_t vif1) {
    w.u64(uint64_t(qwc) | uint64_t(id) << 28 | uint64_t(addr & 0x7fffffff) << 32);
    w.u32(vif0);
    w.u32(vif1);
}

// Pacote CNT: os códigos e dados em "words" (a partir dos 8 bytes altos da etiqueta), completados com
// NOPs até o fim do último quadword.
void cnt(Writer& w, std::vector<uint32_t> words) {
    while ((words.size() + 2) % 4 != 0) words.push_back(0);
    const uint32_t qwc = uint32_t((words.size() - 2) / 4);
    dma_tag(w, qwc, 1, 0, words[0], words[1]);
    for (size_t i = 2; i < words.size(); ++i) w.u32(words[i]);
}

uint32_t f2u(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

constexpr uint32_t kStcycl44 = 0x01000404, kStcycl31 = 0x01000103, kStcycl32 = 0x01000203, kMscnt = 0x17000000;
constexpr uint32_t kGifAddr = 0x2e1;  // como nos modelos do disco
constexpr uint32_t kUvAddr = 0x20;

// GIF tag de tira de triângulos com nloop vértices; tme liga a textura (bit 4 do PRIM).
std::vector<uint32_t> gif_unpack(uint32_t nloop, bool tme) {
    const uint64_t prim = 0x4c | (tme ? 0x10 : 0);
    const uint64_t lo = uint64_t(nloop) | 1ull << 15 | 1ull << 46 | prim << 47 | 3ull << 60;
    return {kStcycl44, 0x6c010000u | kGifAddr, uint32_t(lo), uint32_t(lo >> 32), 0x412, 0};
}

struct PoolVertex {
    float pos[3];
    float weight;
    int16_t normal[3];  // normal * 64
    uint16_t slot0, slot1;
    bool strip_start;
    int16_t morph = -1;  // índice de morph (16 bits altos do w)
};

// Bloco com uma caixa, a estrutura de skinning, a lista de ossos {5, 9, 2} e uma cadeia como a dos
// modelos do disco: a chamada inicial do microprograma (GIF tag sem vértices), a referência de
// textura 1, a tag REF que traz posição e normal do buffer de saída do SkinIt (começando um quadword
// antes, endereço -16, com STCYCL 3,2) e o pacote com GIF tag, UVs e cores que dispara o desenho.
// pool_offset_bytes troca o -16 (para testar um encaixe errado).
std::vector<uint8_t> make_skinned_block(const std::vector<PoolVertex>& pool, int32_t pool_offset_bytes = -16) {
    Writer w;
    w.u32(0);              // cabeçalho do bloco (id 0x2d, tamanho no fim)
    w.u32(0x20);           // gfx_header_offset
    w.pad_to(0x20);
    w.u32(1);              // box_count
    w.u32(0x40);           // boxes_offset
    w.u32(0x80);           // extra_offset (SkinInfo)
    w.pad_to(0x40);
    w.pad_to(0x40 + 0x38);
    w.put32(0x40 + 0x20, 0x100);  // dma_offset
    w.put32(0x40 + 0x24, 0x0f0);  // texref_offset (não usado pela decodificação)
    w.pad_to(0x80);
    w.u32(0x300);                          // input_offset
    w.u32(uint32_t(pool.size()) * 32);     // output_size
    w.u32(0);                              // patch_offset
    w.u32(0xc0);                           // bones_offset
    w.pad_to(0xc0);
    for (const int b : {5, 9, 2, 0xff}) w.b.push_back(uint8_t(b));
    w.pad_to(0x100);

    // Chamada inicial: GIF tag e MSCNT sem nenhum vértice na memória.
    std::vector<uint32_t> prime = gif_unpack(5, true);
    prime.push_back(kMscnt);
    cnt(w, prime);
    dma_tag(w, 4, 3, 1, 0, 0);  // textura 1 do item
    const uint32_t qwords = uint32_t(pool.size()) * 2 + 1;
    dma_tag(w, qwords, 3, uint32_t(pool_offset_bytes), kStcycl32, 0x6c000000u | qwords << 16 | kGifAddr);
    std::vector<uint32_t> draw = gif_unpack(uint32_t(pool.size()), true);
    draw.push_back(0x64000000u | uint32_t(pool.size()) << 16 | kUvAddr);  // UNPACK V2-32: UV
    for (size_t i = 0; i < pool.size(); ++i) {
        draw.push_back(f2u(0.25f * float(i)));
        draw.push_back(f2u(0.5f));
    }
    draw.push_back(kStcycl31);
    draw.push_back(0x6e000000u | uint32_t(pool.size()) << 16 | 0x4000 | (kGifAddr + 2));  // V4-8u: cor
    for (size_t i = 0; i < pool.size(); ++i) draw.push_back(0x80402010u + uint32_t(i));
    draw.push_back(kMscnt);
    cnt(w, draw);
    dma_tag(w, 0, 6, 0, 0, 0);  // RET

    w.pad_to(0x300);
    for (const PoolVertex& v : pool) {
        for (const float c : v.pos) w.f32(c);
        w.f32(v.weight);
        w.u32(uint32_t(uint16_t(v.normal[0])) << 16 | v.slot0);
        w.u32(uint32_t(uint16_t(v.normal[1])) << 16 | v.slot1);
        w.u32(uint32_t(uint16_t(v.normal[2])) << 16);
        w.u32(uint32_t(uint16_t(v.morph)) << 16 | (v.strip_start ? 0x8000u : 0u));  // -1: sem morph
    }
    w.put32(0, 0x2du << 24 | uint32_t(w.b.size()));
    return w.b;
}

const std::vector<PoolVertex> kQuad = {
    {{0, 0, 0}, 1.0f, {0, 0, 64}, 0, 1, true},
    {{1, 0, 0}, 0.5f, {0, 64, 0}, 1, 2, true},
    {{0, 1, 0}, 0.25f, {64, 0, 0}, 2, 0, false},
    {{1, 1, 0}, 0.0f, {0, 0, -64}, 0x8000 | 0, 2, false},  // bit 15 do slot é ignorado
};

// ---------------------------------------------------------------- psiDrawSkinObjectMatrix

// Estruturas como ficam depois do psiCreateEntityGfx: cabeçalho {quantidade, caixas, extra} com
// ponteiros, uma caixa com cadeia e lista de texturas, SkinInfo com a lista de ossos.
struct SkinModel {
    alignas(16) uint8_t box[0x38]{};
    uint32_t dma[4]{};
    uint32_t texlist[2] = {0, 0xffffffff};
    uint8_t bones[4];
    uint32_t info[4]{};
    uint32_t hdr[3]{};
    entgfx_tag gfx{};
    celglist_tag glist{};

    explicit SkinModel(std::initializer_list<uint8_t> list) {
        std::memset(bones, 0xff, sizeof(bones));
        std::copy(list.begin(), list.end(), bones);
        const uint32_t d = nf::p32_u(dma);
        const uint32_t t = nf::p32_u(texlist);
        std::memcpy(box + 0x20, &d, 4);
        std::memcpy(box + 0x24, &t, 4);
        info[3] = nf::p32_u(bones);
        hdr[0] = 1;
        hdr[1] = nf::p32_u(box);
        hdr[2] = nf::p32_u(info);
        gfx.ps2_gfx = reinterpret_cast<uint8_t*>(hdr);
        glist.gfx = &gfx;
    }
};

struct SkinScene {
    alignas(16) obj_tag obj{};
    uint8_t anim[0x120]{};
    MATRIX palette[8];
    MATRIX unused{};

    SkinScene() {
        obj.sphere = {0.0f, 0.0f, 0.0f, 1.0f};
        obj.sphere_src = anim;
        obj.created = 77;
        for (int b = 0; b < 8; ++b) set_translation(&palette[b].m[0][0], float(b), 10.0f * float(b), 0.0f);
        psi_draw_debug_power_on();
        psiPreDraw();
    }
};

}  // namespace

TEST(skin_vertex_mistura_dois_ossos) {
    // Peso 0,25 no osso 3 (translação +10 em x) e 0,75 no osso 7 (+20 em y): o ponto vai à média
    // ponderada dos dois destinos.
    std::vector<float> palette(16 * 8);
    for (int b = 0; b < 8; ++b) set_translation(&palette[16 * b], 0, 0, 0);
    set_translation(&palette[16 * 3], 10, 0, 0);
    set_translation(&palette[16 * 7], 0, 20, 0);
    SkinnedVertex v{};
    v.pos[0] = 1;
    v.pos[1] = 2;
    v.pos[2] = 3;
    v.normal[2] = 1;
    v.bones[0] = 3;
    v.bones[1] = 7;
    v.weight = 0.25f;
    float p[3], n[3];
    nf::skin_vertex(v, palette.data(), p, n);
    CHECK_NEAR(p[0], 0.25 * 11 + 0.75 * 1, 1e-6);
    CHECK_NEAR(p[1], 0.25 * 2 + 0.75 * 22, 1e-6);
    CHECK_NEAR(p[2], 3, 1e-6);
    // Translações não mexem na normal.
    CHECK_NEAR(n[0], 0, 1e-6);
    CHECK_NEAR(n[1], 0, 1e-6);
    CHECK_NEAR(n[2], 1, 1e-6);
    // Peso 1: só o primeiro osso conta.
    v.weight = 1.0f;
    nf::skin_vertex(v, palette.data(), p, n);
    CHECK_NEAR(p[0], 11, 1e-6);
    CHECK_NEAR(p[1], 2, 1e-6);
}

TEST(skin_vertex_gira_a_normal) {
    // Vetor-linha: v' = v * M. Linhas (0,0,-1), (0,1,0), (1,0,0) giram 90 graus em torno de y: z vira x.
    float palette[32];
    set_translation(palette, 0, 0, 0);
    set_translation(palette + 16, 5, 6, 7);
    palette[16 + 0] = 0;
    palette[16 + 2] = -1;
    palette[16 + 8] = 1;
    palette[16 + 10] = 0;
    SkinnedVertex v{};
    v.pos[0] = 1;
    v.pos[1] = 2;
    v.pos[2] = 3;
    v.normal[2] = 1;
    v.bones[0] = 1;
    v.bones[1] = 0;
    v.weight = 1.0f;
    float p[3], n[3];
    nf::skin_vertex(v, palette, p, n);
    CHECK_NEAR(n[0], 1, 1e-6);
    CHECK_NEAR(n[1], 0, 1e-6);
    CHECK_NEAR(n[2], 0, 1e-6);
    CHECK_NEAR(p[0], 3 + 5, 1e-6);  // 1 * (0,0,-1) + 2 * (0,1,0) + 3 * (1,0,0) + t
    CHECK_NEAR(p[1], 2 + 6, 1e-6);
    CHECK_NEAR(p[2], -1 + 7, 1e-6);
}

TEST(decode_gfx_skinning_bloco_sintetico) {
    const std::vector<uint8_t> blk = make_skinned_block(kQuad);
    CHECK(nf::gfx_is_skinned(blk));
    const nf::EntityMesh mesh = nf::decode_gfx(blk, [](int tex) { return tex == 1 ? 2 : 1; });
    CHECK(mesh.skinned);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.bad_batches == 0);  // a chamada inicial sem vértices não conta como erro
    CHECK(mesh.bone_count == 10);  // maior osso da lista {5, 9, 2} + 1
    CHECK(mesh.skin_vertices.size() == 4);
    if (mesh.skin_vertices.size() != 4) return;
    // Tira: o terceiro e o quarto vértices fecham (0,1,2) e (2,1,3), com a ordem alternada.
    const std::vector<uint32_t> want = {0, 1, 2, 2, 1, 3};
    CHECK(mesh.indices == want);
    CHECK(mesh.submeshes.size() == 1 && mesh.submeshes[0].texture == 1 && mesh.submeshes[0].index_count == 6);
    // Ossos pela lista: slot 0 -> 5, 1 -> 9, 2 -> 2; o bit 15 do slot não conta.
    const uint8_t bones[4][2] = {{5, 9}, {9, 2}, {2, 5}, {5, 2}};
    for (size_t i = 0; i < 4; ++i) {
        const SkinnedVertex& v = mesh.skin_vertices[i];
        for (int k = 0; k < 3; ++k) CHECK_NEAR(v.pos[k], kQuad[i].pos[k], 0);
        CHECK(v.bones[0] == bones[i][0] && v.bones[1] == bones[i][1]);
        CHECK_NEAR(v.weight, kQuad[i].weight, 0);
        for (int k = 0; k < 3; ++k) CHECK_NEAR(v.normal[k], kQuad[i].normal[k] / 64.0, 1e-6);
        CHECK_NEAR(v.uv[0], 0.25 * double(i), 0);
        CHECK_NEAR(v.uv[1], 0.25, 0);  // textura de 2 quadros: V pela metade, como nas malhas rígidas
        // Cor como no disco (escala do GS, 0x80 = 1.0): a VU1 a usa assim.
        CHECK(v.color[0] == 0x10 + i && v.color[1] == 0x20 && v.color[2] == 0x40 && v.color[3] == 0x80);
    }
}

TEST(decode_gfx_skinning_desalinhado_descarta_o_lote) {
    // Começando no quadword 0 em vez de -1, as posições caem nos quadwords ímpares (normais): o lote
    // é descartado e contado.
    const std::vector<uint8_t> blk = make_skinned_block(kQuad, 0);
    const nf::EntityMesh mesh = nf::decode_gfx(blk, [](int) { return 1; });
    CHECK(mesh.skinned);
    CHECK(mesh.bad_batches == 1);
    CHECK(mesh.empty());
}

TEST(psiDrawSkinObjectMatrix_copia_a_paleta) {
    SkinScene s;
    SkinModel model({4, 1, 6});
    psiSetUpColourBlend(1);
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1);
    if (g_psi_frame.draws.size() != 1) return;
    const PsiDrawCall& d = g_psi_frame.draws[0];
    CHECK(d.glist == &model.glist && d.obj == &s.obj && d.created == 77 && d.blend == 1);
    CHECK(d.palette == 0 && d.bones == 7);  // maior osso da lista + 1
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) CHECK(d.world.m[r][c] == (r == c ? 1.0f : 0.0f));
    }
    // A cópia não depende mais da paleta do motor.
    s.palette[6].m[3][0] = 1234.0f;
    CHECK(g_psi_frame.palettes.size() == 7);
    CHECK(g_psi_frame.palettes[6].m[3][0] == 6.0f && g_psi_frame.palettes[6].m[3][1] == 60.0f);
    CHECK(g_psi_frame.palettes[2].m[3][1] == 20.0f);
}

TEST(psiDrawSkinObjectMatrix_sem_animacao_nao_desenha) {
    SkinScene s;
    SkinModel model({0});
    s.obj.sphere_src = nullptr;
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.empty());
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, nullptr, s.palette);
    CHECK(g_psi_frame.draws.empty());
}

TEST(psiDrawSkinObjectMatrix_cache_por_objeto) {
    // O SkinCache procura só pelo objeto: no mesmo quadro, um segundo modelo do mesmo objeto desenha
    // de novo o primeiro, com a paleta da primeira chamada. No quadro seguinte o cache já não vale.
    SkinScene s;
    SkinModel first({1, 2});
    SkinModel second({7});
    psiDrawSkinObjectMatrix(&first.glist, &s.unused, &s.obj, s.palette);
    s.palette[1].m[3][0] = 99.0f;
    psiDrawSkinObjectMatrix(&second.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 2);
    if (g_psi_frame.draws.size() != 2) return;
    CHECK(g_psi_frame.draws[1].glist == &first.glist);
    CHECK(g_psi_frame.draws[1].palette == g_psi_frame.draws[0].palette && g_psi_frame.draws[1].bones == 3);
    CHECK(g_psi_frame.palettes.size() == 3 && g_psi_frame.palettes[1].m[3][0] == 1.0f);

    psiPreDraw();
    psiDrawSkinObjectMatrix(&second.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].glist == &second.glist);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].bones == 8);
}

TEST(psiDrawSkinObjectMatrix_recorta_pelo_cubo_da_esfera) {
    SkinScene s;
    SkinModel model({0});
    alignas(16) static viewer_tag cam;
    std::memset(&cam, 0, sizeof(cam));
    // Um lado inclinado (dentro: x + y >= 0); os outros três deixam tudo dentro.
    const float h = 0.70710678f;
    cam.planes[0] = {h, h, 0.0f, 0.0f};
    for (int i = 1; i < 4; ++i) cam.planes[i] = {0.0f, 0.0f, 0.0f, 1.0f};
    psiUseCamera(&cam, 0);
    // Esfera inteira fora (distância -1,13 com raio 1), mas o canto (0,2, 0,2) do cubo em volta dela
    // está dentro: como no CheckObjectClip do PS2, desenha.
    s.obj.sphere = {-0.8f, -0.8f, 0.0f, 1.0f};
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].camera == &cam);
    // Cubo inteiro fora: nada.
    psiPreDraw();
    psiUseCamera(&cam, 0);
    s.obj.sphere = {-2.0f, -2.0f, 0.0f, 1.0f};
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.empty());
}

TEST(decode_gfx_skinning_le_o_indice_de_morph) {
    // Os 16 bits altos do w da entrada do SkinIt: o elemento do bloco 0x2B onde começam os deltas do vértice
    // (-1 = sem morph). Vai para SkinnedVertex::morph pelo mesmo caminho da posição (quadword 2k do buffer).
    std::vector<PoolVertex> quad = kQuad;
    quad[0].morph = 80;
    quad[2].morph = 102;
    quad[3].morph = 0;
    const nf::EntityMesh mesh = nf::decode_gfx(make_skinned_block(quad), [](int) { return 1; });
    CHECK(mesh.skin_vertices.size() == 4);
    if (mesh.skin_vertices.size() != 4) return;
    CHECK(mesh.skin_vertices[0].morph == 80 && mesh.skin_vertices[1].morph == -1);
    CHECK(mesh.skin_vertices[2].morph == 102 && mesh.skin_vertices[3].morph == 0);
    CHECK(mesh.morph.empty());  // os alvos vêm de outro bloco (decode_morph)
}

namespace {
// Estado global dos morphs de AnimObjectDraw (Morph_Count/Index/Weight) para um desenho.
void set_morphs(std::initializer_list<std::pair<uint8_t, float>> list) {
    Morph_Count = 0;
    for (const auto& [k, w] : list) {
        Morph_Index[Morph_Count] = k;
        Morph_Weight[Morph_Count] = w;
        ++Morph_Count;
    }
}
}  // namespace

TEST(psiDrawSkinObjectMatrix_registra_os_pesos_dos_morphs) {
    // Com o bloco 0x2B na malha (entgfx_tag::morph) e Morph_Count != 0, o desenho leva os pesos (o do alvo
    // Morph_Index[k] na posição k); fora de cena, só com o menor byte de closeness até 2, como o SkinIt.
    uint8_t block[16] = {};
    SkinScene s;
    SkinModel model({0, 1});
    model.gfx.morph = block;
    ScriptCam = 0;
    set_morphs({{3, 0.25f}, {10, 1.0f}, {21, -0.5f}});
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].morph == 0);
    CHECK(g_psi_frame.morphs.size() == size_t(kPsiMorphs));
    if (g_psi_frame.morphs.size() == size_t(kPsiMorphs)) {
        CHECK(g_psi_frame.morphs[3] == 0.25f && g_psi_frame.morphs[10] == 1.0f && g_psi_frame.morphs[21] == -0.5f);
        float rest = 0.0f;
        for (int k = 0; k < kPsiMorphs; ++k) rest += (k == 3 || k == 10 || k == 21) ? 0.0f : std::fabs(g_psi_frame.morphs[size_t(k)]);
        CHECK(rest == 0.0f);
    }
    // O mesmo objeto de novo no quadro (o SkinCache): os pesos da primeira chamada.
    set_morphs({{0, 1.0f}});
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 2 && g_psi_frame.draws[1].morph == 0 && g_psi_frame.morphs.size() == 22u);

    // Longe (closeness 3 nas quatro câmeras): sem morph. Numa cena (ScriptCam), com morph de novo.
    psiPreDraw();
    std::memset(s.anim + 0x110, 3, 4);
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].morph == -1 && g_psi_frame.morphs.empty());
    psiPreDraw();
    ScriptCam = 0x6000123;
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].morph == 0);
    ScriptCam = 0;
    // Sem pesos (Morph_Count 0) ou sem o bloco: sem morph.
    psiPreDraw();
    std::memset(s.anim + 0x110, 0, 4);
    Morph_Count = 0;
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].morph == -1);
    psiPreDraw();
    set_morphs({{1, 1.0f}});
    model.gfx.morph = nullptr;
    psiDrawSkinObjectMatrix(&model.glist, &s.unused, &s.obj, s.palette);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].morph == -1);
    Morph_Count = 0;
}
