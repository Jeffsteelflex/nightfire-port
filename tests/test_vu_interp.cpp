// Testes da aritmética do PS2 (platform/ps2_float.h) e do interpretador das VUs (platform/vu_interp.h). Sem dados
// do jogo: os programas são montados aqui a partir dos campos da codificação, e o montador é conferido contra
// palavras que o tools/vudis.py --test decodifica (tiradas do ACTION.ELF), para um erro de campo não se esconder
// dos dois lados. Os valores esperados vêm de contas à mão (truncamento, latências do manual da VU).

#include <cstring>
#include <vector>

#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"
#include "platform/vu_interp.h"
#include "test_framework.h"

using ps2::VuMachine;
using ps2::VuUnit;

namespace {

uint32_t F(float f) { return ps2::fbits(f); }

// ---- montador mínimo -------------------------------------------------------------------------------------
constexpr uint32_t kX = 8, kY = 4, kZ = 2, kW = 1, kXY = 12, kXYZW = 15;
constexpr uint32_t kUpperNop = 0x000002ff;
constexpr uint32_t kLowerNop = 0x8000033c;
constexpr uint32_t kEbit = 0x40000000, kIbit = 0x80000000;

uint32_t up3(uint32_t op, uint32_t dest, int fd, int fs, int ft) {
    return (dest << 21) | (uint32_t(ft) << 16) | (uint32_t(fs) << 11) | (uint32_t(fd) << 6) | op;
}
uint32_t up_add(uint32_t dest, int fd, int fs, int ft) { return up3(0x28, dest, fd, fs, ft); }
uint32_t up_sub(uint32_t dest, int fd, int fs, int ft) { return up3(0x2c, dest, fd, fs, ft); }
uint32_t up_mul(uint32_t dest, int fd, int fs, int ft) { return up3(0x2a, dest, fd, fs, ft); }
uint32_t up_addbc(uint32_t dest, int fd, int fs, int ft, int bc) { return up3(0x00 + bc, dest, fd, fs, ft); }
uint32_t up_mulq(uint32_t dest, int fd, int fs) { return up3(0x1c, dest, fd, fs, 0); }
uint32_t up_muli(uint32_t dest, int fd, int fs) { return up3(0x1e, dest, fd, fs, 0); }
uint32_t up_special(uint32_t sp, uint32_t dest, int fs, int ft) {
    return (dest << 21) | (uint32_t(ft) << 16) | (uint32_t(fs) << 11) | ((sp >> 2) << 6) | 0x3c | (sp & 3);
}
uint32_t up_opmula(int fs, int ft) { return up_special(0x2e, 0xe, fs, ft); }
uint32_t up_opmsub(int fd, int fs, int ft) { return up3(0x2e, 0xe, fd, fs, ft); }

uint32_t lo_special(uint32_t sp, uint32_t dest, int fs, int ft) {
    return 0x80000000u | (dest << 21) | (uint32_t(ft) << 16) | (uint32_t(fs) << 11) | ((sp >> 2) << 6) | 0x3c |
           (sp & 3);
}
uint32_t lo_div(int fs, int fsf, int ft, int ftf) {
    return 0x80000000u | (uint32_t(ftf) << 23) | (uint32_t(fsf) << 21) | (uint32_t(ft) << 16) |
           (uint32_t(fs) << 11) | ((0x38u >> 2) << 6) | 0x3c;
}
uint32_t lo_waitq() { return 0x800003bf; }
uint32_t lo_move(uint32_t dest, int ft, int fs) { return lo_special(0x30, dest, fs, ft); }
uint32_t lo_iaddi(int it, int is, int imm) {
    return 0x80000000u | (uint32_t(it) << 16) | (uint32_t(is) << 11) | ((uint32_t(imm) & 31) << 6) | 0x32;
}
uint32_t lo_iaddiu(int it, int is, uint32_t imm15) {
    return (0x08u << 25) | (uint32_t(it) << 16) | (uint32_t(is) << 11) | ((imm15 & 0x7800) << 10) | (imm15 & 0x7ff);
}
uint32_t lo_branch(uint32_t op, int it, int is, int imm11) {
    return (op << 25) | (uint32_t(it) << 16) | (uint32_t(is) << 11) | (uint32_t(imm11) & 0x7ff);
}
uint32_t lo_lq(uint32_t dest, int ft, int is, int imm) {
    return (dest << 21) | (uint32_t(ft) << 16) | (uint32_t(is) << 11) | (uint32_t(imm) & 0x7ff);
}
uint32_t lo_sq(uint32_t dest, int fs, int it, int imm) {
    return (1u << 25) | (dest << 21) | (uint32_t(it) << 16) | (uint32_t(fs) << 11) | (uint32_t(imm) & 0x7ff);
}
uint32_t lo_fmand(int it, int is) { return (0x1au << 25) | (uint32_t(it) << 16) | (uint32_t(is) << 11); }
uint32_t lo_xgkick(int is) { return 0x800006fcu | (uint32_t(is) << 11); }
uint32_t lo_lqi(uint32_t dest, int ft, int is) { return lo_special(0x34, dest, is, ft); }
uint32_t lo_sqi(uint32_t dest, int fs, int it) { return lo_special(0x35, dest, fs, it); }

struct Prog {
    std::vector<uint32_t> words;  // pares (baixo, cima)
    void pair(uint32_t up, uint32_t lo) {
        words.push_back(lo);
        words.push_back(up);
    }
    void load(VuMachine& m) const { m.load_micro(0, words.data(), uint32_t(words.size() * 4)); }
};

void set_vec(VuMachine& m, int reg, float x, float y, float z, float w) {
    auto& v = m.regs().vf[reg];
    v[0] = F(x);
    v[1] = F(y);
    v[2] = F(z);
    v[3] = F(w);
}

}  // namespace

// --------------------------------------------------------------------------------------------- aritmética

TEST(ps2_float_truncates_instead_of_rounding) {
    // 1/3 e 1/60: o arredondamento ao mais próximo sobe o último bit; o PS2 trunca.
    CHECK(ps2::div(F(1.0f), F(3.0f)) == 0x3eaaaaaau);
    CHECK(ps2::div(F(1.0f), F(60.0f)) == 0x3c888888u);
    CHECK(F(1.0f / 60.0f) == 0x3c888889u);
    // 1 + 2^-30 trunca para 1; 1 - 2^-30 é o float logo abaixo de 1.
    CHECK(ps2::add(F(1.0f), F(0x1p-30f)) == F(1.0f));
    CHECK(ps2::sub(F(1.0f), F(0x1p-30f)) == 0x3f7fffffu);
    // 0.1f + 0.2f vale exatamente 0.300000004470348...: o float logo abaixo é 0x3e999999 (0.29999998), o logo
    // acima 0x3e99999a (0.30000001, o mais próximo). O PS2 dá o de baixo.
    CHECK(ps2::add(F(0.1f), F(0.2f)) == 0x3e999999u);
    CHECK(F(0.1f + 0.2f) == 0x3e99999au);
    // Soma exata com cancelamento: x + (-x) = +0.
    CHECK(ps2::add(F(1.5f), F(-1.5f)) == 0u);
    CHECK(ps2::add(0x80000000u, 0x80000000u) == 0x80000000u);
}

TEST(ps2_float_has_no_denormals_infinities_or_nan) {
    // Entrada denormal vale zero; resultado pequeno demais vira zero com underflow.
    CHECK(ps2::mul(0x00000001u, F(1.0f)) == 0u);
    ps2::FloatFlags f;
    CHECK(ps2::mul(F(0x1p-100f), F(0x1p-100f), &f) == 0u);
    CHECK(f.underflow && f.zero);
    // Expoente 255 é um número normal: 0x7f800000 vale 2^128 e metade dele é 2^127.
    CHECK(ps2::value(0x7f800000u) == std::ldexp(1.0, 128));
    CHECK(ps2::mul(0x7f800000u, F(0.5f)) == 0x7f000000u);
    // Estouro satura no maior módulo, com o sinal.
    CHECK(ps2::mul(F(0x1p127f), F(4.0f), &f) == 0x7fffffffu);
    CHECK(f.overflow);
    CHECK(ps2::mul(F(-0x1p127f), F(4.0f)) == 0xffffffffu);
    // Divisão por zero: o maior módulo com o sinal de a xor b; 0/0 é inválida.
    ps2::DivFlags d;
    CHECK(ps2::div(F(-2.0f), 0u, &d) == 0xffffffffu);
    CHECK(d.divzero && !d.invalid);
    ps2::div(0u, 0u, &d);
    CHECK(d.invalid);
    // Raiz de negativo usa o módulo e marca inválida.
    CHECK(ps2::sqrt(F(-4.0f), &d) == F(2.0f));
    CHECK(d.invalid);
    CHECK(ps2::rsqrt(F(1.0f), F(4.0f)) == F(0.5f));
}

TEST(ps2_float_compare_convert_and_minmax) {
    // MAX/MINI comparam sinal e módulo: +0 > -0 > -1.
    CHECK(ps2::max(0x80000000u, 0u) == 0u);
    CHECK(ps2::max(F(-1.0f), 0x80000000u) == 0x80000000u);
    CHECK(ps2::min(F(-1.0f), F(-2.0f)) == F(-2.0f));
    // Conversões truncam e saturam.
    CHECK(ps2::to_int(F(1.9f)) == 1);
    CHECK(ps2::to_int(F(-1.9f)) == -1);
    CHECK(ps2::to_int(F(3e9f)) == 0x7fffffff);
    CHECK(ps2::to_int(F(-3e9f)) == int32_t(0x80000000u));
    CHECK(ps2::to_int(F(1.5f), 4) == 24);
    CHECK(ps2::from_int(0x7fffffff) == 0x4effffffu);  // 2^31 - 1 truncado (o mais próximo daria 2^31)
    CHECK(ps2::from_int(24, 4) == F(1.5f));
    CHECK(ps2::ee::lt(ps2::ffloat(0x80000000u), 0.0f) == false);  // -0 não é menor que +0
    CHECK(ps2::ee::cvt_w(-0.5f) == 0);
}

// --------------------------------------------------------------------------------------------- montador

TEST(vu_assembler_matches_known_words) {
    // As palavras do tools/vudis.py --test (do ACTION.ELF) que o montador daqui precisa reproduzir.
    CHECK(up3(0x2a, kXYZW, 4, 0, 4) == 0x01e4012au);           // MUL.xyzw vf04, vf00, vf04
    CHECK(up_special(0x00, kXYZW, 4, 1) == 0x01e1203cu);       // ADDAx.xyzw ACC, vf04, vf01x
    CHECK(up3(0x08, kXYZW, 3, 5, 1) == 0x01e128c8u);           // MADDx.xyzw vf03, vf05, vf01x
    CHECK(up_special(0x1f, 0xe, 4, 7) == 0x01c721ffu);         // CLIPw.xyz vf04, vf07w
    CHECK(up_special(0x2f, 0, 0, 0) == kUpperNop);
    CHECK(lo_special(0x30, 0, 0, 0) == kLowerNop);
    CHECK(lo_xgkick(10) == (0x800006fcu | (10u << 11)));
    CHECK(lo_branch(0x29, 1, 2, 3) == (0x52000000u | (1u << 16) | (2u << 11) | 3));
}

// --------------------------------------------------------------------------------------------- micro

TEST(vu_upper_dest_mask_and_broadcast) {
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 2, 3, 4);
    set_vec(m, 2, 10, 20, 30, 40);
    set_vec(m, 3, 7, 7, 7, 7);
    Prog p;
    p.pair(up_add(kX | kZ, 3, 1, 2), kLowerNop);     // vf03.xz = vf01 + vf02
    p.pair(up_addbc(kY | kW, 4, 1, 2, 3), kLowerNop); // vf04.yw = vf01 + vf02.w
    p.pair(kUpperNop | kEbit, kLowerNop);
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    const auto& r = m.regs();
    CHECK(r.vf[3][0] == F(11) && r.vf[3][1] == F(7) && r.vf[3][2] == F(33) && r.vf[3][3] == F(7));
    CHECK(r.vf[4][0] == 0 && r.vf[4][1] == F(42) && r.vf[4][2] == 0 && r.vf[4][3] == F(44));
    CHECK(r.vf[0][3] == F(1.0f) && r.vf[0][0] == 0);  // vf00 intocado
}

TEST(vu_cross_product_with_opmula_opmsub) {
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 0, 0, 0);
    set_vec(m, 2, 0, 1, 0, 0);
    set_vec(m, 3, 2, 3, 4, 9);
    set_vec(m, 4, 5, 6, 7, 9);
    Prog p;
    p.pair(up_opmula(1, 2), kLowerNop);
    p.pair(up_opmsub(5, 2, 1), kLowerNop);  // vf05 = vf01 x vf02 = z
    p.pair(up_opmula(3, 4), kLowerNop);
    p.pair(up_opmsub(6, 4, 3) | kEbit, kLowerNop);  // (2,3,4) x (5,6,7) = (-3, 6, -3)
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    const auto& r = m.regs();
    CHECK(r.vf[5][0] == 0 && r.vf[5][1] == 0 && r.vf[5][2] == F(1));
    CHECK(r.vf[6][0] == F(-3) && r.vf[6][1] == F(6) && r.vf[6][2] == F(-3));
    CHECK(r.vf[6][3] == 0);  // w não é escrito
}

TEST(vu_q_register_latency) {
    // DIV Q = 1/4 (7 ciclos). Um MULq logo depois ainda vê o Q velho (2); 7 pares depois vê o novo.
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 0, 0, 0);
    set_vec(m, 2, 4, 0, 0, 0);
    set_vec(m, 3, 8, 8, 8, 8);
    m.regs().q = F(2.0f);
    Prog p;
    p.pair(kUpperNop, lo_div(1, 0, 2, 0));          // ciclo 1: DIV
    p.pair(up_mulq(kX, 4, 3), kLowerNop);           // ciclo 2: Q velho
    for (int i = 0; i < 4; ++i) p.pair(kUpperNop, kLowerNop);  // ciclos 3-6
    p.pair(up_mulq(kY, 4, 3), kLowerNop);           // ciclo 7: ainda velho (7 - 1 = 6 < 7)
    p.pair(up_mulq(kZ, 4, 3), kLowerNop);           // ciclo 8: novo
    p.pair(kUpperNop, lo_div(3, 0, 2, 0));          // DIV Q = 8 / 4
    p.pair(kUpperNop, lo_waitq());                  // espera
    p.pair(up_mulq(kW, 4, 3) | kEbit, kLowerNop);   // Q = 2
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    const auto& r = m.regs();
    CHECK(r.vf[4][0] == F(16));
    CHECK(r.vf[4][1] == F(16));
    CHECK(r.vf[4][2] == F(2));
    CHECK(r.vf[4][3] == F(16));
    CHECK(r.q == F(2.0f));
    CHECK(m.stats().stall_cycles >= 6);  // o WAITQ esperou
}

TEST(vu_mac_flag_is_seen_four_cycles_later) {
    // ADD.y com resultado negativo marca Sy (0x40). FMAND 3 pares depois ainda vê o indicador velho (0); 4 pares
    // depois vê o novo.
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 0, -5, 0, 0);
    set_vec(m, 2, 0, 1, 0, 0);
    m.regs().vi[3] = 0x40;
    Prog p;
    p.pair(up_add(kY, 4, 1, 2), kLowerNop);   // ciclo 1
    p.pair(kUpperNop, kLowerNop);             // 2
    p.pair(kUpperNop, kLowerNop);             // 3
    p.pair(kUpperNop, lo_fmand(5, 3));        // 4: velho
    p.pair(kUpperNop, lo_fmand(6, 3));        // 5: 5 - 1 = 4, novo
    p.pair(kUpperNop | kEbit, kLowerNop);
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    CHECK(m.regs().vi[5] == 0);
    CHECK(m.regs().vi[6] == 0x40);
    CHECK((m.regs().mac & 0x40) != 0);
}

TEST(vu_fmac_hazard_stalls_but_keeps_sequential_values) {
    // vf04 = vf01 + vf02 e logo depois vf05 = vf04 * vf04: o hardware para 3 ciclos; o valor é o novo.
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 1, 1, 1);
    set_vec(m, 2, 2, 2, 2, 2);
    Prog p;
    p.pair(up_add(kXYZW, 4, 1, 2), kLowerNop);
    p.pair(up_mul(kXYZW, 5, 4, 4) | kEbit, kLowerNop);
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    CHECK(m.regs().vf[5][0] == F(9));
    CHECK(m.stats().stall_cycles == 3);
}

TEST(vu_pair_lower_reads_before_upper_and_same_dest_discards_lower) {
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 2, 3, 4);
    set_vec(m, 2, 10, 10, 10, 10);
    set_vec(m, 4, 100, 100, 100, 100);
    Prog p;
    // ADD vf04 = vf01 + vf02 | MOVE vf05 = vf04: a de baixo copia o vf04 de antes (100).
    p.pair(up_add(kXYZW, 4, 1, 2), lo_move(kXYZW, 5, 4));
    // ADD vf06 = vf01 + vf01 | MOVE vf06 = vf02: as duas escrevem vf06; fica a de cima.
    p.pair(up_add(kXYZW, 6, 1, 1) | kEbit, lo_move(kXYZW, 6, 2));
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    CHECK(m.regs().vf[5][0] == F(100));
    CHECK(m.regs().vf[4][0] == F(11));
    CHECK(m.regs().vf[6][0] == F(2) && m.regs().vf[6][3] == F(8));
}

TEST(vu_branch_delay_slot_and_ebit_delay_slot) {
    // Laço de 3 voltas: vi01 = 3; corpo soma 1 em vf04.x; IBNE com delay slot que soma 10 em vf05.x.
    // O fim (bit E) tem delay slot que soma 100 em vf06.x.
    VuMachine m(VuUnit::VU0);
    set_vec(m, 3, 1, 0, 0, 0);
    set_vec(m, 7, 10, 0, 0, 0);
    set_vec(m, 8, 100, 0, 0, 0);
    Prog p;
    p.pair(kUpperNop, lo_iaddiu(1, 0, 3));                // 0: vi01 = 3
    p.pair(up_add(kX, 4, 4, 3), lo_iaddi(1, 1, -1));       // 1: vf04 += 1; vi01--
    p.pair(kUpperNop, kLowerNop);                          // 2 (o desvio não lê o vi01 velho)
    p.pair(kUpperNop, lo_branch(0x29, 1, 0, -3));          // 3: IBNE vi01, vi00 -> 1
    p.pair(up_add(kX, 5, 5, 7), kLowerNop);                // 4: delay slot
    p.pair(kUpperNop | kEbit, kLowerNop);                  // 5: E
    p.pair(up_add(kX, 6, 6, 8), kLowerNop);                // 6: delay slot do E
    p.pair(up_add(kX, 9, 9, 8), kLowerNop);                // 7: não roda
    p.load(m);
    m.run(0);
    const auto& r = m.regs();
    CHECK(r.vf[4][0] == F(3));
    CHECK(r.vf[5][0] == F(30));
    CHECK(r.vf[6][0] == F(100));
    CHECK(r.vf[9][0] == 0);
    CHECK(r.vi[1] == 0);
}

TEST(vu_branch_reads_old_vi_right_after_integer_write) {
    // IADDI vi01 = vi01 - 1 logo antes do IBNE: o desvio vê o valor velho (1), desvia uma vez a mais.
    VuMachine m(VuUnit::VU0);
    set_vec(m, 3, 1, 0, 0, 0);
    m.regs().vi[1] = 1;
    Prog p;
    p.pair(up_add(kX, 4, 4, 3), lo_iaddi(1, 1, -1));   // 0
    p.pair(kUpperNop, lo_branch(0x29, 1, 0, -2));      // 1: IBNE -> 0 com o vi01 de antes
    p.pair(kUpperNop, kLowerNop);                      // 2: delay slot
    p.pair(kUpperNop | kEbit, kLowerNop);              // 3
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    // Volta 1: vi01 1 -> 0, desvio lê 1 (velho) e volta. Volta 2: vi01 0 -> -1, lê 0, sai.
    CHECK(m.regs().vf[4][0] == F(2));
    CHECK(int16_t(m.regs().vi[1]) == -1);
}

TEST(vu_memory_lqi_sqi_and_ibit) {
    VuMachine m(VuUnit::VU0);
    for (uint32_t n = 0; n < 4; ++n) {
        uint32_t* q = m.qword(n);
        for (int l = 0; l < 4; ++l) q[l] = F(float(n * 10 + l));
    }
    Prog p;
    p.pair(kUpperNop, lo_lqi(kXYZW, 1, 2));             // vf01 = mem[vi02++]
    p.pair(kUpperNop, lo_lqi(kXYZW, 2, 2));             // vf02 = mem[1]
    p.pair(kUpperNop | kIbit, F(0.5f));                // LOI 0.5
    p.pair(up_muli(kXYZW, 3, 2), lo_sqi(kXY, 3, 4));    // vf03 = vf02 * I; mem[vi04++].xy = vf03 (velho: 0)
    p.pair(kUpperNop, lo_sq(kZ | kW, 3, 0, 9));         // mem[9].zw = vf03 (o novo)
    p.pair(kUpperNop | kEbit, lo_lq(kX, 5, 0, 3));      // vf05.x = mem[3].x
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    const auto& r = m.regs();
    CHECK(r.vi[2] == 2 && r.vi[4] == 1);
    CHECK(r.vf[2][1] == F(11));
    CHECK(r.vf[3][1] == F(5.5f));
    CHECK(m.qword(0)[0] == 0 && m.qword(0)[1] == 0 && m.qword(0)[2] == F(2.0f));  // xy do vf03 de antes (0)
    CHECK(m.qword(9)[2] == F(6.0f) && m.qword(9)[3] == F(6.5f));
    CHECK(r.vf[5][0] == F(30));
    CHECK(r.i == F(0.5f));
}

TEST(vu1_xgkick_captures_the_gif_packet) {
    VuMachine m(VuUnit::VU1);
    // Tag PACKED com NLOOP 2, NREG 1 (8 quadwords? não: 2 x 1 = 2) e uma segunda tag IMAGE de 1 com EOP.
    uint32_t* t0 = m.qword(0x10);
    t0[0] = 2;               // NLOOP 2
    t0[1] = 1u << 28;        // NREG 1 (bits 60-63), FLG 0
    uint32_t* t1 = m.qword(0x13);
    t1[0] = 1 | 0x8000;      // NLOOP 1, EOP
    t1[1] = 2u << 26;        // FLG 2 (IMAGE)
    m.regs().vi[5] = 0x10;
    Prog p;
    p.pair(kUpperNop | kEbit, lo_xgkick(5));
    p.pair(kUpperNop, kLowerNop);
    p.load(m);
    m.run(0);
    CHECK(m.kicks().size() == 1);
    CHECK(m.kicks()[0].address == 0x10);
    CHECK(m.kicks()[0].data.size() == 5 * 16);  // tag + 2 + tag + 1
}

TEST(vu_macro_mode_matches_micro_semantics) {
    // VADD.xyz vf03, vf01, vf02 e VDIV Q, vf01x, vf02y pelo modo macro (palavra COP2 com o bit CO).
    VuMachine m(VuUnit::VU0);
    set_vec(m, 1, 1, 2, 3, 4);
    set_vec(m, 2, 3, 4, 5, 6);
    CHECK(m.macro(0x4a000000u | up_add(0xe, 3, 1, 2)));
    CHECK(m.regs().vf[3][0] == F(4) && m.regs().vf[3][2] == F(8) && m.regs().vf[3][3] == 0);
    CHECK(m.macro(0x4a000000u | (lo_div(1, 0, 2, 1) & 0x1ffffff)));
    CHECK(m.regs().q == F(0.25f));
    // VCALLMS fica com quem chama.
    CHECK(!m.macro(0x4a000038u | (0x34u << 6)));
    // As mesmas contas pelo vu0_macro.h.
    uint32_t a[4] = {F(1), F(2), F(3), F(4)}, b[4] = {F(3), F(4), F(5), F(6)}, d[4] = {7, 7, 7, 7};
    ps2::vu::add(d, a, b, ps2::vu::XYZ);
    CHECK(d[0] == m.regs().vf[3][0] && d[2] == m.regs().vf[3][2] && d[3] == 7);
}
