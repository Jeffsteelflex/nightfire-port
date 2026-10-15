// Os personagens das missões de carro (driving/anim/eagl_anim.h, actor.h; o estado dos modelos em eagl_state.h):
// propriedades lidas no formato dos dados e no código do jogo, conferidas sem o original (a conferência bit a bit
// contra o DRIVING.ELF fica no nf_driving_check, parte 16): as contas da VU0 em casos conhecidos, o DeltaCompressedData
// num bloco feito à mão (8, 16 e 4 bits, o recomeço, o indexado), um FnDeltaF3 com blocos e constantes, a cadeia do
// esqueleto (local, global, pele) num esqueleto de dois ossos, os tempos do ActPoser e o estado dos personagens.

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "driving/anim/actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/eagl_state.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv::anim;

namespace {

// Um bloco de dados com o leiaute do jogo, alinhado a 16, e o endereço P32 de cada posição.
struct Blob {
    std::vector<uint32_t> words;
    explicit Blob(size_t bytes) : words((bytes + 3) / 4 + 4, 0) {}
    uint8_t* base() { return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(words.data()) + 15) & ~uintptr_t(15)); }
    uint32_t at(uint32_t off) { return nf::p32_u(base() + off); }
    void u8(uint32_t off, uint8_t v) { base()[off] = v; }
    void u16(uint32_t off, uint16_t v) { std::memcpy(base() + off, &v, 2); }
    void u32(uint32_t off, uint32_t v) { std::memcpy(base() + off, &v, 4); }
    void f(uint32_t off, float v) { std::memcpy(base() + off, &v, 4); }
};

}  // namespace

TEST(driving_anim_vu0_known_cases) {
    // A identidade dos dois lados: exata (os produtos por 0 somam zero, por 1 copiam).
    alignas(16) float m[16], id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, out[16];
    for (int i = 0; i < 16; ++i) m[i] = 0.25f * float(i + 1) * (i % 3 == 0 ? -1.0f : 1.0f);
    MATRIX4_mult(id, m, out);
    CHECK(std::memcmp(out, m, 64) == 0);
    MATRIX4_mult(m, id, out);
    CHECK(std::memcmp(out, m, 64) == 0);
    // No lugar: a saída sobre a entrada a.
    alignas(16) float a[16];
    std::memcpy(a, m, 64);
    MATRIX4_mult(a, id, a);
    CHECK(std::memcmp(a, m, 64) == 0);
    // SQT: o quatérnio identidade dá a escala na diagonal e a translação na linha 3 (com o w dela).
    alignas(16) const float s[4] = {2, 3, 4, 9}, qi[4] = {0, 0, 0, 1}, t[4] = {5, 6, 7, 1};
    SQTquattom4(s, qi, t, out);
    const float want[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1};
    CHECK(std::memcmp(out, want, 64) == 0);
    // 90 graus em z (linhas: p' = p x M): x vai para y, y para -x.
    const float h = std::sqrt(0.5f);
    alignas(16) const float qz[4] = {0, 0, h, h}, one[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 1};
    SQTquattom4(one, qz, zero, out);
    CHECK_NEAR(out[0], 0.0f, 1e-6f);
    CHECK_NEAR(out[1], 1.0f, 1e-6f);
    CHECK_NEAR(out[4], -1.0f, 1e-6f);
    CHECK_NEAR(out[5], 0.0f, 1e-6f);
    CHECK_NEAR(out[10], 1.0f, 1e-6f);
    // O slerp rápido: as pontas (normalizadas), o comprimento 1 no meio, e a ponta oposta pelo caminho curto.
    alignas(16) const float q0[4] = {0.0f, 0.6f, 0.0f, 0.8f}, q1[4] = {0.6f, 0.0f, 0.0f, 0.8f};
    alignas(16) float q[4];
    fast_qslerp(q0, q1, q, 0.0f);
    for (int k = 0; k < 4; ++k) CHECK_NEAR(q[k], q0[k], 1e-6f);
    fast_qslerp(q0, q1, q, 1.0f);
    for (int k = 0; k < 4; ++k) CHECK_NEAR(q[k], q1[k], 1e-6f);
    fast_qslerp(q0, q1, q, 0.37f);
    CHECK_NEAR(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0f, 1e-5f);
    alignas(16) const float neg[4] = {-q0[0], -q0[1], -q0[2], -q0[3]};
    fast_qslerp(q0, neg, q, 0.5f);  // as pontas opostas: q1 t - q0 (1 - t) = -q0
    for (int k = 0; k < 4; ++k) CHECK_NEAR(q[k], neg[k], 1e-6f);
}

TEST(driving_anim_delta_compressed_data) {
    // Dois canais de 8 bits: {deslocamento, escala, base} = {0,5; 1; 10} e {-1; 0,25; 0}; os deltas de três quadros.
    Blob d(64);
    d.u16(0, 2);
    d.u8(2, 8);
    d.f(4, 0.5f), d.f(8, 1.0f), d.f(12, 10.0f);
    d.f(16, -1.0f), d.f(20, 0.25f), d.f(24, 0.0f);
    const uint8_t deltas[6] = {2, 4, 1, 8, 0, 0};
    for (int i = 0; i < 6; ++i) d.u8(28 + uint32_t(i), deltas[i]);
    float out[2] = {77, 77};
    // Recomeço (de -1): a base, depois os deltas dos quadros 0 e 1 (b x escala + deslocamento).
    DecompressValues(d.at(0), 0, 2, -1, 2, out, out);
    CHECK(out[0] == 14.0f && out[1] == 1.0f);
    DecompressValues(d.at(0), 0, 2, 2, 3, out, out);
    CHECK(out[0] == 14.5f && out[1] == 0.0f);
    // Para trás (to < from): recomeça da base.
    DecompressValues(d.at(0), 0, 2, 3, 1, out, out);
    CHECK(out[0] == 12.5f && out[1] == 0.0f);
    // to == from: nada; to == 0: só a base (com from -1) ou a cópia do cache.
    out[0] = 5;
    DecompressValues(d.at(0), 0, 2, 1, 1, out, out);
    CHECK(out[0] == 5.0f);
    float cache[2] = {3, 4};
    DecompressValues(d.at(0), 0, 2, -1, 0, cache, out);
    CHECK(out[0] == 10.0f && out[1] == 0.0f);
    // Um canal só a partir do segundo (first 1, nchan 1): o ponteiro começa no byte 1 de cada quadro.
    float one = 0;
    DecompressValues(d.at(0), 1, 1, -1, 1, &one, &one);
    CHECK(one == 0.0f + (4 * 0.25f - 1.0f));
    // 16 bits.
    Blob w(64);
    w.u16(0, 1);
    w.u8(2, 16);
    w.f(4, 0.0f), w.f(8, 0.5f), w.f(12, 1.0f);
    w.u16(16, 300), w.u16(18, 2);
    float v = 0;
    DecompressValues(w.at(0), 0, 1, -1, 2, &v, &v);
    CHECK(v == 1.0f + 150.0f + 1.0f);
    // 4 bits: três canais, dois bytes por quadro (o par no meio byte de baixo).
    Blob n4(96);
    n4.u16(0, 3);
    n4.u8(2, 4);
    for (int c = 0; c < 3; ++c) n4.f(4 + 12u * uint32_t(c), 0.0f), n4.f(8 + 12u * uint32_t(c), 1.0f), n4.f(12 + 12u * uint32_t(c), 0.0f);
    n4.u8(40, 0x21);  // quadro 0: canal 0 = 1, canal 1 = 2
    n4.u8(41, 0x03);  // canal 2 = 3
    float o3[3] = {9, 9, 9};
    DecompressValues(n4.at(0), 0, 3, -1, 1, o3, o3);
    CHECK(o3[0] == 1.0f && o3[1] == 2.0f && o3[2] == 3.0f);
    // O indexado: t x (b x escala + deslocamento) somado ao cache, nos índices da lista (grupos de 2 floats).
    const uint16_t index[1] = {4};
    float pose[8] = {0, 0, 0, 0, 0, 0, 0, 0}, cache2[2] = {1, 1};
    DecompressValuesIndexed(0.5f, d.at(0), 0, 2, 0, 1, cache2, pose, 2, index, 8);
    CHECK(pose[4] == 1.0f + 0.5f * 2.5f && pose[5] == 1.0f + 0.5f * 0.0f && pose[0] == 0.0f);
}

TEST(driving_anim_delta_f3_blocks_and_constants) {
    // Um FnDeltaF3 de um canal (a translação do osso 0, índice 8), blocos de dois quadros (deslocamento 1), quatro
    // quadros sem lista de chaves, e uma constante (índice 0: a escala do osso 0).
    Blob d(256);
    Blob idx(16);
    idx.u16(0, 8);
    d.u16(0, kFnDeltaF3);
    d.u32(4, idx.at(0));
    d.u32(8, 0);
    d.u16(0xc, 4);
    d.u16(0xe, 1);
    d.u8(0x10, 1);
    d.u8(0x11, 1);
    // O cabeçalho: base 0, faixa 65535 (escala 1 por unidade de 16 bits), deltas: a = 32768, b = 255.
    for (int k = 0; k < 3; ++k) {
        d.f(0x14 + 4u * uint32_t(k), 0.0f);
        d.f(0x20 + 4u * uint32_t(k), 65535.0f);
        d.u16(0x2c + 2u * uint32_t(k), 32768);
        d.u16(0x32 + 2u * uint32_t(k), 255);
    }
    // Os blocos (10 bytes): o quadro 0 (100, 200, 300), o delta do quadro 1 (bytes 0, 10, 20); o quadro 2 (400, 500,
    // 600), o delta do quadro 3 (5, 5, 5).
    const uint32_t blocks = 0x14 + 0x24;
    const uint16_t f0[3] = {100, 200, 300}, f2[3] = {400, 500, 600};
    for (int k = 0; k < 3; ++k) {
        d.u16(blocks + 2u * uint32_t(k), f0[k]);
        d.u16(blocks + 10 + 2u * uint32_t(k), f2[k]);
    }
    d.u8(blocks + 6, 0), d.u8(blocks + 7, 10), d.u8(blocks + 8, 20);
    d.u8(blocks + 16, 5), d.u8(blocks + 17, 5), d.u8(blocks + 18, 5);
    // As constantes: a lista de índices (alinhada a 2) e os três floats (alinhados a 4).
    const uint32_t consts = blocks + 20;
    d.u16(consts, 0);
    d.f(consts + 4, 2.0f), d.f(consts + 8, 3.0f), d.f(consts + 12, 4.0f);

    reset_anim_stats();
    std::unique_ptr<FnAnim> a = NewFnAnim(kFnDeltaF3);
    CHECK(a != nullptr);
    a->SetAnimMemoryMap(d.at(0));
    float len = 0;
    CHECK(a->GetLength(len) && len == 4.0f);
    // A dequantização (o InitBuffersAsRequired): a escala de 16 bits é a faixa / 65535 (1 por unidade); o delta de 8
    // bits é v x (2 x b / 65535 x faixa / 255) + (2 x a / 65535 - 1) x faixa = 2 v + 1.
    const double dscale = (2.0 * 255.0 / 65535.0) * 65535.0 / 255.0, doff = (2.0 * 32768.0 / 65535.0) * 65535.0 - 65535.0;
    CHECK_NEAR(dscale, 2.0, 1e-9);
    CHECK_NEAR(doff, 1.0, 1e-9);
    float pose[12] = {};
    auto value = [&](float t) {
        std::memset(pose, 0, sizeof pose);
        a->Eval(0, t, pose, 12);
    };
    value(0.0f);
    CHECK_NEAR(pose[8], 100.0f, 0.01f);
    CHECK_NEAR(pose[9], 200.0f, 0.01f);
    CHECK_NEAR(pose[10], 300.0f, 0.01f);
    CHECK(pose[0] == 2.0f && pose[1] == 3.0f && pose[2] == 4.0f);  // a constante
    value(1.0f);  // o quadro 1: o 0 mais o delta (0, 10, 20)
    CHECK_NEAR(pose[8], 100.0 + 0 * dscale + doff, 0.01);
    CHECK_NEAR(pose[9], 200.0 + 10 * dscale + doff, 0.01);
    value(2.0f);  // o começo do bloco 1: o valor inteiro
    CHECK_NEAR(pose[8], 400.0f, 0.01f);
    CHECK_NEAR(pose[10], 600.0f, 0.01f);
    value(2.5f);  // entre o quadro 2 e o 3 (o delta 5)
    CHECK_NEAR(pose[8], 400.0 + 0.5 * (5 * dscale + doff), 0.01);
    value(1.0f);  // de volta ao bloco 0 (o recomeço do bloco)
    CHECK_NEAR(pose[9], 200.0 + 10 * dscale + doff, 0.01);
    value(9.0f);  // depois do fim: o último quadro, sem interpolar
    CHECK_NEAR(pose[8], 400.0 + 5 * dscale + doff, 0.01);
    CHECK(anim_stats().guards == 0);
}

namespace {

// Um Skeleton de dois ossos: a raiz em (0, 1, 0), o filho a (0, 2, 0) da raiz; a inversa da ligação de cada osso é a
// translação oposta da global de ligação.
struct TwoBones {
    Blob b{0x10 + 2 * 0x70};
    TwoBones() {
        b.u32(8, 2);
        b.u32(0xc, 0);
        for (int i = 0; i < 2; ++i) {
            const uint32_t o = 0x10 + 0x70u * uint32_t(i);
            for (int k = 0; k < 3; ++k) b.f(o + 4u * uint32_t(k), 1.0f);
            b.u32(o + 0xc, uint32_t(i - 1));  // -1, 0
            b.f(o + 0x1c, 1.0f);              // o quatérnio identidade
            b.f(o + 0x24, i == 0 ? 1.0f : 2.0f);
            b.f(o + 0x2c, 0.0f);
            for (int k = 0; k < 4; ++k) b.f(o + 0x30 + 20u * uint32_t(k), 1.0f);  // a diagonal da inversa
            b.f(o + 0x30 + 0x34, i == 0 ? -1.0f : -3.0f);                          // a translação y
        }
    }
};

}  // namespace

TEST(driving_anim_skeleton_bind_pose_skins_to_identity) {
    TwoBones s;
    const uint32_t skel = s.b.at(0);
    CHECK(Skeleton_NumBones(skel) == 2 && Skeleton_Parent(skel, 1) == 0);
    std::vector<float> pose(2 * kPoseFloats), local(32), global(32), skin(32);
    Skeleton_GetStillPose(skel, pose.data());
    CHECK(pose[3] == 1.0f && pose[7] == 1.0f && pose[9] == 1.0f && pose[11] == 1.0f && pose[12 + 9] == 2.0f);
    Skeleton_PoseSQTToLocal(skel, pose.data(), local.data());
    Skeleton_PoseLocalToGlobal(skel, local.data(), global.data());
    CHECK(global[13] == 1.0f && global[16 + 13] == 3.0f);  // as translações somam pela cadeia
    Skeleton_PoseGlobalToSkin(skel, global.data(), skin.data());
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 16; ++i) CHECK(skin[size_t(16 * b + i)] == ((i % 5) == 0 ? 1.0f : 0.0f));
    // No lugar dá o mesmo.
    std::vector<float> inplace = local;
    Skeleton_PoseLocalToGlobal(skel, inplace.data(), inplace.data());
    CHECK(std::memcmp(inplace.data(), global.data(), 128) == 0);
    // A escala do comprimento multiplica a primeira coluna da rotação.
    pose[3] = 2.0f;
    Skeleton_PoseSQTToLocal(skel, pose.data(), local.data());
    CHECK(local[0] == 2.0f && local[5] == 1.0f && local[4] == 0.0f);
}

TEST(driving_actor_poser_times_and_loop) {
    TwoBones s;
    ActSkeleton sk;
    sk.attach(s.b.at(0), 0);
    ActAnimGroup g;
    g.length = 10.0f;  // sem dados de animação: o DoMainPose só mexe nos tempos
    ActPoser p(&sk, 1.0f / 60.0f);
    p.SetNormalAnimation(&g, 0.5f);  // segundos x 20 quadros por segundo
    CHECK(p.mode == 1 && p.time == 10.0f && p.next_time == 10.0f && p.end_time == 10.0f && p.length == 10.0f);
    CHECK_NEAR(p.previous_time, 10.0f - 20.0f / 60.0f, 1e-5f);
    p.SetNormalAnimation(&g, 0.0f);
    CHECK(p.previous_time == 0.0f);  // não fica negativo
    // Um quadro do jogo anda 20/60 de quadro de animação; o fim (o total - 1) para o tempo e IsAnimationDone liga.
    int steps = 0;
    while (!p.IsAnimationDone() && steps < 100) {
        p.DoMainPose();
        p.AdvanceTime();
        ++steps;
    }
    CHECK(steps > 20 && steps < 30);
    const float t = p.next_time;
    p.DoMainPose();
    p.AdvanceTime();
    CHECK(p.next_time == t);  // no fim o tempo não anda mais (o laço é de quem chama)
    // A escala do tempo.
    p.SetNormalAnimation(&g, 0.0f);
    p.time_scale = 2.0f;
    p.DoMainPose();
    p.AdvanceTime();
    CHECK_NEAR(p.next_time, 40.0f / 60.0f, 1e-5f);
}

TEST(driving_character_runtime_geoprimstate) {
    // O estado dos 73 modelos de personagens: faixa de triângulos, mistura (Cs - Cd) As + Cd, teste de alfa ALWAYS sem
    // ligar o teste, textura, sem névoa (o construtor não liga o FGE).
    drv::eagl::RStateManager_Reset();
    int unknown = 0;
    const uint8_t* s = drv::eagl::GeoPrimState_FromRuntimeSpec(
        "UID=433;SetAlphaBlendMode=EAGL::ABM_BLEND;SetAlphaTestMethod=EAGL::ATM_ALWAYS;SetCullEnable=true;"
        "SetTextureEnable=true;SetPrimitiveType=EAGL::PT_TRIANGLESTRIP;",
        &unknown);
    uint64_t prim, test, alpha;
    std::memcpy(&prim, s + 0x10, 8);
    std::memcpy(&test, s + 0x30, 8);
    std::memcpy(&alpha, s + 0x40, 8);
    CHECK(unknown == 0);
    CHECK(prim == 0x5c);                       // tristrip (4), IIP, TME, ABE; sem FGE
    CHECK((test & 0xF) == 2);                  // ATST ALWAYS, ATE desligado
    CHECK(alpha == 0x8000000044ull);
    const uint8_t* add = drv::eagl::GeoPrimState_FromRuntimeSpec("SetAlphaBlendMode=EAGL::ABM_ADD;Foo=bar;", &unknown);
    std::memcpy(&alpha, add + 0x40, 8);
    CHECK(alpha == 0x8000000048ull && unknown == 1);
    drv::eagl::RStateManager_Reset();
}
