// Estado do GS das malhas: pares A+D dos pacotes DIRECT (gs_apply_direct), o estado de cada lote em
// decode_gfx (na ordem da cadeia) e a equação de mistura do GS (gs_blend_equation).

#include <cstring>
#include <vector>

#include "data/map_item.h"
#include "test_framework.h"

using nf::BlendFactor;
using nf::BlendOp;
using nf::GsState;

namespace {

struct Bytes {
    std::vector<uint8_t> b;
    void u32(uint32_t v) {
        const size_t at = b.size();
        b.resize(at + 4);
        std::memcpy(&b[at], &v, 4);
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
};

// GIF tag PACKED com NREG = 1 e REGS = 0xE (A+D), seguida dos pares {dado, registrador}.
void ad_packet(Bytes& w, const std::vector<std::pair<uint64_t, int>>& regs, bool eop = true) {
    w.u64(uint64_t(regs.size()) | (eop ? 1ull << 15 : 0) | 1ull << 60);
    w.u64(0xE);
    for (const auto& [data, reg] : regs) {
        w.u64(data);
        w.u64(uint64_t(reg));
    }
}

// Cadeia DMA: tag CNT com os códigos VIF a partir da palavra 2 da tag, completada com NOPs.
void cnt(Bytes& w, const std::vector<uint32_t>& vif) {
    const size_t words = vif.size() < 2 ? 0 : vif.size() - 2;
    const uint32_t qwc = uint32_t((words + 3) / 4);
    w.u32(qwc | 1u << 28);  // CNT
    w.u32(0);
    for (uint32_t v : vif) w.u32(v);
    for (size_t k = vif.size(); k < 2 + size_t(qwc) * 4; ++k) w.u32(0);
}

void ref_texture(Bytes& w, uint32_t texture) {
    w.u32(4 | 3u << 28);  // REF, qwc 4
    w.u32(texture);
    w.u32(0);
    w.u32(0);
}

void ret(Bytes& w) {
    w.u32(6u << 28);
    w.u32(0);
    w.u32(0);
    w.u32(0);
}

std::vector<uint32_t> words_of(const Bytes& b) {
    std::vector<uint32_t> out(b.b.size() / 4);
    std::memcpy(out.data(), b.b.data(), out.size() * 4);
    return out;
}

// FLUSHE e DIRECT com as escritas A+D (o DIRECT na palavra 3 de um quadword, como nos dados).
std::vector<uint32_t> direct(const std::vector<std::pair<uint64_t, int>>& regs) {
    Bytes p;
    ad_packet(p, regs);
    std::vector<uint32_t> v = {0x10000000, 0, 0, 0, 0, 0x50000000u | uint32_t(p.b.size() / 16)};
    // A tag começa na palavra 2: códigos nas palavras 2..7, o DIRECT fica na palavra 7 (3 mod 4).
    for (uint32_t w : words_of(p)) v.push_back(w);
    return v;
}

// Um lote de três vértices (uma tira) com o PRIM dado; routine é a palavra w da GIF tag (8: rotinas
// comuns da VU1, 0x10: mapa de ambiente).
std::vector<uint32_t> batch(uint32_t prim, uint32_t routine = 8) {
    std::vector<uint32_t> v = {0x01000404, 0x6c018000};  // STCYCL 4,4; UNPACK V4-32 x1 (GIF tag)
    const uint64_t tag = 3 | 1ull << 15 | 1ull << 46 | uint64_t(prim) << 47 | 3ull << 60;
    v.push_back(uint32_t(tag));
    v.push_back(uint32_t(tag >> 32));
    v.push_back(0x412);
    v.push_back(routine);
    v.push_back(0x64038000 | 0xc7);  // UNPACK V2-32 x3 (UV)
    for (int i = 0; i < 6; ++i) v.push_back(0);
    v.push_back(0x01000103);  // STCYCL 3,1
    v.push_back(0x68038001);  // UNPACK V3-32 x3 (posição)
    const float pos[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (const auto& p : pos) {
        for (float f : p) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            v.push_back(u);
        }
    }
    v.push_back(0x6e038003);  // UNPACK V4-8 x3 (normal, com sinal)
    for (int i = 0; i < 3; ++i) v.push_back(0x00007f00);
    v.push_back(0x6e03c002);  // UNPACK V4-8 x3 sem sinal (cor)
    for (int i = 0; i < 3; ++i) v.push_back(0x40808080);
    v.push_back(0x17000000);  // MSCNT
    return v;
}

// Bloco 0x2d com uma caixa e a cadeia dada.
std::vector<uint8_t> gfx_block(const Bytes& chain) {
    Bytes w;
    w.u32(0x2d000000);  // cabeçalho do bloco (o tamanho não é lido)
    w.u32(0x10 + 0x10);  // cabeçalho de geometria em 0x20
    w.u32(0);
    w.u32(0);
    for (int k = 0; k < 4; ++k) w.u32(0);
    w.u32(1);     // 0x20: uma caixa
    w.u32(0x30);  // caixas
    w.u32(0);     // sem skinning
    w.u32(0);
    for (int k = 0; k < 14; ++k) w.u32(k == 8 ? 0x70 : 0);  // caixa: cadeia DMA em +0x20
    while (w.b.size() < 0x70) w.u32(0);
    w.b.insert(w.b.end(), chain.b.begin(), chain.b.end());
    return w.b;
}

constexpr uint64_t kOpaqueZ = 0x31000000, kNoZ = 0x131000000ull, kTest = 0x5001b;

}  // namespace

TEST(gs_apply_direct_reads_the_ad_pairs) {
    Bytes p;
    // Uma tag REGLIST (pulada) antes da PACKED com A+D.
    p.u64(1 | 1ull << 58 | 2ull << 60);
    p.u64(0x55);
    p.u64(0x1111);
    p.u64(0x2222);
    ad_packet(p, {{0x7f00000048, 0x42}, {kNoZ, 0x4e}, {kTest, 0x47}, {0x123, 0x06}});
    GsState s;
    const int n = nf::gs_apply_direct(nf::ByteView(p.b.data(), p.b.size()), s);
    CHECK(n == 4);
    CHECK(s.alpha == 0x7f00000048 && s.zbuf == kNoZ && s.test == kTest);
    CHECK(s.has == (GsState::kAlpha | GsState::kTest | GsState::kZbuf));
    // Só o que foi escrito muda.
    Bytes q;
    ad_packet(q, {{0x44, 0x42}});
    nf::gs_apply_direct(nf::ByteView(q.b.data(), q.b.size()), s);
    CHECK(s.alpha == 0x44 && s.zbuf == kNoZ && s.test == kTest);
}

TEST(decode_gfx_keeps_the_gs_state_of_each_batch_in_chain_order) {
    Bytes chain;
    cnt(chain, {0x01000404, 0x6c018000, 5 | 1u << 15, 0, 0x412, 0, 0x17000000});  // chamada inicial, sem vértices
    ref_texture(chain, 0);
    cnt(chain, direct({{0x44, 0x42}, {kOpaqueZ, 0x4e}, {kTest, 0x47}}));
    cnt(chain, batch(0x5c));
    cnt(chain, direct({{0x7f00000048, 0x42}, {kNoZ, 0x4e}}));
    cnt(chain, batch(0x1c));  // ABE desligado
    cnt(chain, direct({{0x44, 0x42}, {kOpaqueZ, 0x4e}}));
    cnt(chain, batch(0x5c));
    cnt(chain, batch(0x5c));  // mesmo estado e textura: junta com o anterior
    ret(chain);
    const std::vector<uint8_t> blk = gfx_block(chain);
    const nf::EntityMesh mesh = nf::decode_gfx(nf::ByteView(blk.data(), blk.size()), [](int) { return 1; });
    CHECK(!mesh.skinned && mesh.inherited_gs == 0);
    CHECK(mesh.submeshes.size() == 3);
    if (mesh.submeshes.size() != 3) return;
    const GsState& a = mesh.submeshes[0].gs;
    const GsState& b = mesh.submeshes[1].gs;
    const GsState& c = mesh.submeshes[2].gs;
    CHECK(a.alpha == 0x44 && a.zbuf == kOpaqueZ && a.test == kTest && a.has == 7 && a.abe == 1);
    CHECK(b.alpha == 0x7f00000048 && b.zbuf == kNoZ && b.test == kTest && b.abe == 0);
    CHECK(c == a);
    // Na ordem da cadeia: 1 triângulo, 1 triângulo, depois os dois lotes juntos.
    CHECK(mesh.submeshes[0].index_count == 3 && mesh.submeshes[1].index_count == 3 &&
          mesh.submeshes[2].index_count == 6);
    CHECK(mesh.submeshes[0].first_index == 0 && mesh.submeshes[1].first_index == 3);
    for (const nf::SubMesh& sm : mesh.submeshes) CHECK(sm.texture == 0);
}

TEST(decode_gfx_counts_batches_without_their_own_gs_state) {
    Bytes chain;
    ref_texture(chain, 0);
    cnt(chain, direct({{0x44, 0x42}}));  // só ALPHA
    cnt(chain, batch(0x5c));
    ret(chain);
    const std::vector<uint8_t> blk = gfx_block(chain);
    const nf::EntityMesh mesh = nf::decode_gfx(nf::ByteView(blk.data(), blk.size()), [](int) { return 1; });
    CHECK(mesh.inherited_gs == 1);
    CHECK(mesh.submeshes.size() == 1 && mesh.submeshes[0].gs.has == GsState::kAlpha);
}

TEST(decode_gfx_keeps_vu1_inputs_and_the_env_routine) {
    Bytes chain;
    ref_texture(chain, 0);
    cnt(chain, direct({{0x44, 0x42}, {kOpaqueZ, 0x4e}, {kTest, 0x47}}));
    cnt(chain, batch(0x5c));
    cnt(chain, batch(0x5c, 0x10));  // mesma textura e estado, rotina do mapa de ambiente: outra parte
    cnt(chain, batch(0x5c, 0x10));
    ret(chain);
    const std::vector<uint8_t> blk = gfx_block(chain);
    const nf::EntityMesh mesh = nf::decode_gfx(nf::ByteView(blk.data(), blk.size()), [](int) { return 1; });
    CHECK(mesh.submeshes.size() == 2);
    if (mesh.submeshes.size() != 2) return;
    CHECK(!mesh.submeshes[0].env && mesh.submeshes[1].env);
    CHECK(mesh.submeshes[0].index_count == 3 && mesh.submeshes[1].index_count == 6);
    // A cor fica na escala do GS (0x80 = 1.0) e a normal é o inteiro do UNPACK / 128, como a VU1 os usa.
    const nf::Vertex& v = mesh.vertices[0];
    CHECK(v.color[0] == 0x80 && v.color[1] == 0x80 && v.color[2] == 0x80 && v.color[3] == 0x40);
    CHECK(v.normal[0] == 0.0f && v.normal[1] == 127.0f / 128.0f && v.normal[2] == 0.0f);
}

TEST(gs_blend_equation_maps_the_game_modes) {
    // psiSetUpColourBlend 1 e as malhas: (Cs - Cd) * As + Cd.
    nf::GsBlendEq e = nf::gs_blend_equation(0x44, true, false);
    CHECK(e.enable && e.supported && e.src == BlendFactor::SrcAlpha && e.dst == BlendFactor::InvSrcAlpha &&
          e.op == BlendOp::Add);
    // 0x48: Cs * As + Cd (aditiva); com o Cs já multiplicado no shader, o fator vira One.
    e = nf::gs_blend_equation(0x7f00000048, true, false);
    CHECK(e.enable && e.src == BlendFactor::SrcAlpha && e.dst == BlendFactor::One && e.op == BlendOp::Add);
    CHECK(e.premultipliable);
    e = nf::gs_blend_equation(0x48, true, true);
    CHECK(e.src == BlendFactor::One && e.dst == BlendFactor::One);
    // 0x42: Cd - Cs * As (subtrativa).
    e = nf::gs_blend_equation(0x7f00000042, true, false);
    CHECK(e.enable && e.src == BlendFactor::SrcAlpha && e.dst == BlendFactor::One && e.op == BlendOp::RevSubtract);
    // 0x2a: Cs (opaca), e qualquer equação com ABE desligado.
    e = nf::gs_blend_equation(0x2a, true, false);
    CHECK(!e.enable && e.src == BlendFactor::One && e.dst == BlendFactor::Zero);
    e = nf::gs_blend_equation(0x44, false, false);
    CHECK(!e.enable);
    // 0x64 com FIX 0: (Cs - Cd) * 0 + Cd = Cd (só o alfa do destino muda).
    e = nf::gs_blend_equation(0x64, true, false);
    CHECK(e.enable && e.src == BlendFactor::Fix && e.dst == BlendFactor::InvFix && e.fix == 0.0f);
    e = nf::gs_blend_equation(0x4000000064, true, false);
    CHECK(e.fix == 0.5f);
    // 0x58: Cs * Ad + Cd.
    e = nf::gs_blend_equation(0x58, true, false);
    CHECK(e.enable && e.src == BlendFactor::DstAlpha && e.dst == BlendFactor::One && !e.premultipliable);
    // (Cd - Cs) * As + Cs = Cs * (1 - As) + Cd * As.
    e = nf::gs_blend_equation(0x01 | 0x00 << 2 | 0x00 << 4 | 0x00 << 6, true, false);
    CHECK(e.src == BlendFactor::InvSrcAlpha && e.dst == BlendFactor::SrcAlpha && e.op == BlendOp::Add);
    // (Cs - 0) * As + Cs = Cs * (1 + As): não cabe; volta para a de transparência.
    e = nf::gs_blend_equation(0x00 | 0x02 << 2 | 0x00 << 4 | 0x00 << 6, true, false);
    CHECK(!e.supported && e.src == BlendFactor::SrcAlpha && e.dst == BlendFactor::InvSrcAlpha);
    // (0 - Cd) * As + 0 = 0 - Cd * As (o GS satura em 0).
    e = nf::gs_blend_equation(0x02 | 0x01 << 2 | 0x00 << 4 | 0x02 << 6, true, false);
    CHECK(e.src == BlendFactor::Zero && e.dst == BlendFactor::SrcAlpha && e.op == BlendOp::Subtract);
}

TEST(alpha_coverage_only_for_opaque_alpha_cutouts) {
    // Só do PC (o MSAA): o alfa vai para a cobertura só nos desenhos que o teste de alfa recorta e que ficam opacos
    // onde passam. O teste lido como resolve_state o lê do TEST_1: ATST nos bits 1-3, AREF nos bits 4-11.
    const auto atst = [](uint64_t test) { return float((test >> 1) & 7); };
    const auto aref = [](uint64_t test) { return float((test >> 4) & 0xff); };
    const nf::GsBlendEq lerp = nf::gs_blend_equation(0x44, true, false);     // (Cs - Cd) * As + Cd
    const nf::GsBlendEq add = nf::gs_blend_equation(0x48, true, false);      // Cs * As + Cd
    const nf::GsBlendEq opaque = nf::gs_blend_equation(0x44, false, false);  // ABE desligado
    CHECK(lerp.enable && add.enable && !opaque.enable);
    // As folhas e grades (TEST 0x507fd: GREATER 0x7f, só o alfa cheio passa) com a mistura de transparência: opacas.
    CHECK(nf::gs_alpha_coverage_candidate(atst(0x507fd), aref(0x507fd), true, lerp));
    // O comum das fases (TEST 0x5001b: GEQUAL 1) misturado: as bordas já são suaves (a mistura), fica como está.
    CHECK(!nf::gs_alpha_coverage_candidate(atst(0x5001b), aref(0x5001b), true, lerp));
    // O mesmo teste sem mistura: recorte opaco.
    CHECK(nf::gs_alpha_coverage_candidate(atst(0x5001b), aref(0x5001b), true, opaque));
    // A aditiva com o alfa cheio ainda soma o fundo: não é opaca.
    CHECK(!nf::gs_alpha_coverage_candidate(atst(0x507fd), aref(0x507fd), true, add));
    // A regra antiga dos opacos sem estado (GEQUAL 64, contínua) e GREATER 0 inteiro (passa o alfa 1): recortes.
    CHECK(nf::gs_alpha_coverage_candidate(5.0f, 64.0f, false, opaque));
    CHECK(nf::gs_alpha_coverage_candidate(6.0f, 0.0f, true, opaque));
    // Sem teste, sem limiar (GEQUAL 0 passa tudo) ou os testes por cima (LESS, LEQUAL, EQUAL, NOTEQUAL): nada.
    CHECK(!nf::gs_alpha_coverage_candidate(-1.0f, 0.0f, true, opaque));
    CHECK(!nf::gs_alpha_coverage_candidate(5.0f, 0.0f, true, opaque));
    for (float f : {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 7.0f}) CHECK(!nf::gs_alpha_coverage_candidate(f, 64.0f, true, opaque));
    // Com a mistura de transparência, só um limiar que exige As >= 1 (GEQUAL 0x80 também serve).
    CHECK(nf::gs_alpha_coverage_candidate(5.0f, 128.0f, true, lerp));
    CHECK(!nf::gs_alpha_coverage_candidate(5.0f, 127.0f, true, lerp));
}
