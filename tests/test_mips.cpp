// Testes de data/mips_interp.cpp (interpretador MIPS R5900 usado nos construtores estáticos do
// ACTION.ELF) e de game/static_init.cpp. Sem dados do jogo: as instruções são montadas aqui a partir
// dos campos da codificação MIPS, e o montador é conferido contra palavras reais do executável (a
// desmontagem do Ghidra de um _GLOBAL_$I$...), para que um erro de campo não se esconda dos dois lados.

#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"
#include "engine/effect.h"
#include "engine/weapons.h"
#include "engine/drone.h"
#include "engine/pickup.h"
#include "engine/player_stats.h"
#include "engine/font.h"
#include "engine/script.h"
#include "engine/sprite.h"
#include "game/static_init.h"
#include "test_framework.h"

using nf::MipsError;
using nf::MipsMachine;
using R = MipsMachine::Reg;

namespace {

constexpr uint32_t kCode = 0x00100000;
constexpr uint32_t kData = 0x00200000;

// Montador mínimo: tipos R, I e J.
constexpr uint32_t rtype(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct;
}
constexpr uint32_t itype(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (uint32_t(imm) & 0xffff);
}
constexpr uint32_t jtype(uint32_t op, uint32_t target) { return (op << 26) | ((target >> 2) & 0x03ffffff); }

constexpr uint32_t nop = 0;
constexpr uint32_t addiu(int rt, int rs, int32_t imm) { return itype(0x09, rs, rt, imm); }
constexpr uint32_t daddiu(int rt, int rs, int32_t imm) { return itype(0x19, rs, rt, imm); }
constexpr uint32_t slti(int rt, int rs, int32_t imm) { return itype(0x0a, rs, rt, imm); }
constexpr uint32_t sltiu(int rt, int rs, int32_t imm) { return itype(0x0b, rs, rt, imm); }
constexpr uint32_t andi(int rt, int rs, int32_t imm) { return itype(0x0c, rs, rt, imm); }
constexpr uint32_t ori(int rt, int rs, int32_t imm) { return itype(0x0d, rs, rt, imm); }
constexpr uint32_t lui(int rt, int32_t imm) { return itype(0x0f, 0, rt, imm); }
constexpr uint32_t addu(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x21); }
constexpr uint32_t subu(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x23); }
constexpr uint32_t and_(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x24); }
constexpr uint32_t or_(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x25); }
constexpr uint32_t xor_(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x26); }
constexpr uint32_t slt(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x2a); }
constexpr uint32_t sltu(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x2b); }
constexpr uint32_t daddu(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x2d); }
constexpr uint32_t movz(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x0a); }
constexpr uint32_t movn(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x0b); }
constexpr uint32_t sll(int rd, int rt, int sa) { return rtype(0, rt, rd, sa, 0x00); }
constexpr uint32_t srl(int rd, int rt, int sa) { return rtype(0, rt, rd, sa, 0x02); }
constexpr uint32_t sra(int rd, int rt, int sa) { return rtype(0, rt, rd, sa, 0x03); }
constexpr uint32_t dsll32(int rd, int rt, int sa) { return rtype(0, rt, rd, sa, 0x3c); }
constexpr uint32_t dsra32(int rd, int rt, int sa) { return rtype(0, rt, rd, sa, 0x3f); }
constexpr uint32_t mult(int rd, int rs, int rt) { return rtype(rs, rt, rd, 0, 0x18); }
constexpr uint32_t mflo(int rd) { return rtype(0, 0, rd, 0, 0x12); }
constexpr uint32_t mfhi(int rd) { return rtype(0, 0, rd, 0, 0x10); }
constexpr uint32_t jr(int rs) { return rtype(rs, 0, 0, 0, 0x08); }
constexpr uint32_t jalr(int rd, int rs) { return rtype(rs, 0, rd, 0, 0x09); }
constexpr uint32_t jal(uint32_t target) { return jtype(0x03, target); }
constexpr uint32_t j(uint32_t target) { return jtype(0x02, target); }
// Desvios: off em instruções, relativo à instrução seguinte ao desvio.
constexpr uint32_t beq(int rs, int rt, int off) { return itype(0x04, rs, rt, off); }
constexpr uint32_t bne(int rs, int rt, int off) { return itype(0x05, rs, rt, off); }
constexpr uint32_t blez(int rs, int off) { return itype(0x06, rs, 0, off); }
constexpr uint32_t bgtz(int rs, int off) { return itype(0x07, rs, 0, off); }
constexpr uint32_t beql(int rs, int rt, int off) { return itype(0x14, rs, rt, off); }
constexpr uint32_t bnel(int rs, int rt, int off) { return itype(0x15, rs, rt, off); }
constexpr uint32_t blezl(int rs, int off) { return itype(0x16, rs, 0, off); }
constexpr uint32_t bgtzl(int rs, int off) { return itype(0x17, rs, 0, off); }
constexpr uint32_t bltz(int rs, int off) { return itype(0x01, rs, 0, off); }
constexpr uint32_t bgez(int rs, int off) { return itype(0x01, rs, 1, off); }
constexpr uint32_t bltzl(int rs, int off) { return itype(0x01, rs, 2, off); }
constexpr uint32_t bgezl(int rs, int off) { return itype(0x01, rs, 3, off); }
// Memória: op rt, off(base).
constexpr uint32_t mem(uint32_t op, int rt, int32_t off, int base) { return itype(op, base, rt, off); }
constexpr uint32_t lb(int rt, int32_t o, int b) { return mem(0x20, rt, o, b); }
constexpr uint32_t lbu(int rt, int32_t o, int b) { return mem(0x24, rt, o, b); }
constexpr uint32_t lh(int rt, int32_t o, int b) { return mem(0x21, rt, o, b); }
constexpr uint32_t lhu(int rt, int32_t o, int b) { return mem(0x25, rt, o, b); }
constexpr uint32_t lw(int rt, int32_t o, int b) { return mem(0x23, rt, o, b); }
constexpr uint32_t lwu(int rt, int32_t o, int b) { return mem(0x27, rt, o, b); }
constexpr uint32_t ld(int rt, int32_t o, int b) { return mem(0x37, rt, o, b); }
constexpr uint32_t lq(int rt, int32_t o, int b) { return mem(0x1e, rt, o, b); }
constexpr uint32_t lwl(int rt, int32_t o, int b) { return mem(0x22, rt, o, b); }
constexpr uint32_t lwr(int rt, int32_t o, int b) { return mem(0x26, rt, o, b); }
constexpr uint32_t ldl(int rt, int32_t o, int b) { return mem(0x1a, rt, o, b); }
constexpr uint32_t ldr(int rt, int32_t o, int b) { return mem(0x1b, rt, o, b); }
constexpr uint32_t sb(int rt, int32_t o, int b) { return mem(0x28, rt, o, b); }
constexpr uint32_t sh(int rt, int32_t o, int b) { return mem(0x29, rt, o, b); }
constexpr uint32_t sw(int rt, int32_t o, int b) { return mem(0x2b, rt, o, b); }
constexpr uint32_t sd(int rt, int32_t o, int b) { return mem(0x3f, rt, o, b); }
constexpr uint32_t sq(int rt, int32_t o, int b) { return mem(0x1f, rt, o, b); }
constexpr uint32_t swl(int rt, int32_t o, int b) { return mem(0x2a, rt, o, b); }
constexpr uint32_t swr(int rt, int32_t o, int b) { return mem(0x2e, rt, o, b); }
constexpr uint32_t sdl(int rt, int32_t o, int b) { return mem(0x2c, rt, o, b); }
constexpr uint32_t sdr(int rt, int32_t o, int b) { return mem(0x2d, rt, o, b); }
constexpr uint32_t lwc1(int ft, int32_t o, int b) { return mem(0x31, ft, o, b); }
constexpr uint32_t swc1(int ft, int32_t o, int b) { return mem(0x39, ft, o, b); }
// COP1.
constexpr uint32_t mtc1(int rt, int fs) { return (0x11u << 26) | (4u << 21) | (uint32_t(rt) << 16) | (uint32_t(fs) << 11); }
constexpr uint32_t mfc1(int rt, int fs) { return (0x11u << 26) | (uint32_t(rt) << 16) | (uint32_t(fs) << 11); }
constexpr uint32_t fop(uint32_t funct, int fd, int fs, int ft) {
    return (0x11u << 26) | (0x10u << 21) | (uint32_t(ft) << 16) | (uint32_t(fs) << 11) | (uint32_t(fd) << 6) | funct;
}
constexpr uint32_t add_s(int fd, int fs, int ft) { return fop(0x00, fd, fs, ft); }
constexpr uint32_t sub_s(int fd, int fs, int ft) { return fop(0x01, fd, fs, ft); }
constexpr uint32_t mul_s(int fd, int fs, int ft) { return fop(0x02, fd, fs, ft); }
constexpr uint32_t div_s(int fd, int fs, int ft) { return fop(0x03, fd, fs, ft); }
constexpr uint32_t mov_s(int fd, int fs) { return fop(0x06, fd, fs, 0); }
constexpr uint32_t neg_s(int fd, int fs) { return fop(0x07, fd, fs, 0); }
constexpr uint32_t cvt_w_s(int fd, int fs) { return fop(0x24, fd, fs, 0); }
constexpr uint32_t c_lt_s(int fs, int ft) { return fop(0x34, 0, fs, ft); }
constexpr uint32_t c_eq_s(int fs, int ft) { return fop(0x32, 0, fs, ft); }
constexpr uint32_t cvt_s_w(int fd, int fs) {
    return (0x11u << 26) | (0x14u << 21) | (uint32_t(fs) << 11) | (uint32_t(fd) << 6) | 0x20;
}
constexpr uint32_t bc1(uint32_t kind, int off) { return (0x11u << 26) | (8u << 21) | (kind << 16) | (uint32_t(off) & 0xffff); }

// Escreve o programa em kCode (ou base) e libera essa faixa para execução.
void program(MipsMachine& m, const std::vector<uint32_t>& code, uint32_t base = kCode) {
    for (size_t i = 0; i < code.size(); ++i) m.write32(base + uint32_t(i * 4), code[i]);
    m.allow_code(base, base + uint32_t(code.size() * 4));
}

uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

}  // namespace

TEST(mips_encoder_matches_elf_words) {
    // _GLOBAL_$I$Copter_Create... (0x0017e5b0) no ACTION.ELF, palavra a palavra, com a desmontagem.
    CHECK(addiu(R::sp, R::sp, -0x10) == 0x27bdfff0u);
    CHECK(addiu(R::a0, R::zero, 1) == 0x24040001u);
    CHECK(sq(R::ra, 0, R::sp) == 0x7fbf0000u);
    CHECK(jal(0x0017e468) == 0x0c05f91au);
    CHECK(ori(R::a1, R::zero, 0xffff) == 0x3405ffffu);
    CHECK(lq(R::ra, 0, R::sp) == 0x7bbf0000u);
    CHECK(jr(R::ra) == 0x03e00008u);
    CHECK(addiu(R::sp, R::sp, 0x10) == 0x27bd0010u);
}

TEST(mips_constructor_stub_calls_with_1_ffff) {
    // O formato dos _GLOBAL_$I$: guarda ra (128 bits) na pilha, chama
    // __static_initialization_and_destruction_0(1, 0xffff) e volta pelo ra restaurado com lq.
    MipsMachine m;
    program(m, {0x27bdfff0u, 0x24040001u, 0x7fbf0000u, jal(kCode + 0x100), 0x3405ffffu, 0x7bbf0000u, 0x03e00008u,
                0x27bd0010u});
    program(m, {lui(R::t0, kData >> 16), sw(R::a0, 0, R::t0), sw(R::a1, 4, R::t0), jr(R::ra), nop}, kCode + 0x100);
    m.call(kCode);
    CHECK(m.read32(kData) == 1);
    CHECK(m.read32(kData + 4) == 0xffff);
    CHECK(uint32_t(m.gpr(R::sp)) == MipsMachine::kStackTop);
    CHECK(m.read32(MipsMachine::kStackTop - 16) == MipsMachine::kReturnAddress);  // o ra salvo por sq
    CHECK(m.steps() == 13);
}

TEST(mips_delay_slots) {
    MipsMachine m;
    program(m, {
                   addiu(R::v0, R::zero, 0),
                   beq(R::zero, R::zero, 3),   // -> +0x14
                   addiu(R::v0, R::v0, 1),     // delay slot: executa
                   addiu(R::v0, R::v0, 0x10),  // pulada
                   addiu(R::v0, R::v0, 0x10),  // pulada
                   bne(R::zero, R::zero, 2),   // não desvia
                   addiu(R::v0, R::v0, 2),     // delay slot: executa mesmo sem desviar
                   addiu(R::v0, R::v0, 4),     // segue normalmente
                   jr(R::ra),
                   addiu(R::v0, R::v0, 8),  // delay slot do jr
               });
    m.call(kCode);
    CHECK(m.gpr(R::v0) == 15);
}

TEST(mips_likely_branches_nullify_delay_slot) {
    MipsMachine m;
    program(m, {
                   addiu(R::v0, R::zero, 0),
                   addiu(R::v1, R::zero, 5),
                   beql(R::v1, R::zero, 2),    // não desvia: anula o delay slot
                   addiu(R::v0, R::v0, 1),     // anulada
                   addiu(R::v0, R::v0, 2),     // executa
                   bnel(R::v1, R::zero, 2),    // desvia: o delay slot executa
                   addiu(R::v0, R::v0, 4),     // executa
                   addiu(R::v0, R::v0, 0x40),  // pulada
                   jr(R::ra),
                   nop,
               });
    m.call(kCode);
    CHECK(m.gpr(R::v0) == 6);

    // Laço com bnel, como o de __do_global_ctors: o corpo fica no delay slot.
    MipsMachine l;
    program(l, {
                   addiu(R::v0, R::zero, 0),
                   addiu(R::v1, R::zero, 10),
                   addiu(R::v1, R::v1, -1),  // laço
                   bnel(R::v1, R::zero, -2),
                   addiu(R::v0, R::v0, 3),  // só quando desvia
                   jr(R::ra),
                   nop,
               });
    l.call(kCode);
    CHECK(l.gpr(R::v0) == 27);
    CHECK(l.gpr(R::v1) == 0);
}

TEST(mips_branch_conditions_are_64bit_signed) {
    struct Case {
        uint32_t insn;
        uint64_t value;
        bool taken;
    };
    const uint64_t neg = ~0ull, big = 0xffffffffull;  // -1 e 4294967295 (positivo em 64 bits)
    const Case cases[] = {
        {blez(R::t0, 3), neg, true},   {blez(R::t0, 3), 0, true},     {blez(R::t0, 3), 1, false},
        {blez(R::t0, 3), big, false},  {bgtz(R::t0, 3), big, true},   {bgtz(R::t0, 3), 0, false},
        {bltz(R::t0, 3), neg, true},   {bltz(R::t0, 3), 0, false},    {bgez(R::t0, 3), 0, true},
        {bgez(R::t0, 3), neg, false},  {blezl(R::t0, 3), 0, true},    {blezl(R::t0, 3), 1, false},
        {bgtzl(R::t0, 3), 1, true},    {bgtzl(R::t0, 3), neg, false}, {bltzl(R::t0, 3), neg, true},
        {bltzl(R::t0, 3), 1, false},   {bgezl(R::t0, 3), 1, true},    {bgezl(R::t0, 3), neg, false},
        {beq(R::t0, R::zero, 3), 0, true}, {bne(R::t0, R::zero, 3), big, true},
    };
    for (const Case& c : cases) {
        MipsMachine m;
        program(m, {addiu(R::v0, R::zero, 0), c.insn, addiu(R::v1, R::zero, 7), addiu(R::v0, R::zero, 1),
                    jr(R::ra), nop, jr(R::ra), nop});
        m.set_gpr(R::t0, c.value);
        m.call(kCode);
        CHECK((m.gpr(R::v0) == 0) == c.taken);
        // O delay slot (v1 = 7) só é anulado por desvio likely não tomado.
        const bool likely = (c.insn >> 26) >= 0x14 || ((c.insn >> 26) == 1 && ((c.insn >> 16) & 2));
        CHECK((m.gpr(R::v1) == 7) == (c.taken || !likely));
    }
}

TEST(mips_jalr_and_jump) {
    MipsMachine m;
    program(m, {
                   addiu(R::sp, R::sp, -16),
                   sq(R::ra, 0, R::sp),
                   lui(R::t9, kCode >> 16),
                   ori(R::t9, R::t9, 0x40),
                   jalr(R::ra, R::t9),
                   addiu(R::a0, R::zero, 7),  // delay slot: o argumento
                   j(kCode + 0x30),
                   addiu(R::v0, R::v0, 100),  // delay slot do j
                   addiu(R::v0, R::v0, 1000),  // pulada
                   addiu(R::v0, R::v0, 1000),  // pulada
                   addiu(R::v0, R::v0, 1000),  // pulada
                   addiu(R::v0, R::v0, 1000),  // pulada
                   lq(R::ra, 0, R::sp),  // +0x30
                   jr(R::ra),
                   addiu(R::sp, R::sp, 16),
                   nop,
                   jr(R::ra),  // +0x40
                   addiu(R::v0, R::a0, 1),
               });
    m.call(kCode);
    CHECK(m.gpr(R::v0) == 108);
    CHECK(uint32_t(m.gpr(R::sp)) == MipsMachine::kStackTop);
}

TEST(mips_conditional_moves) {
    // MOVZ/MOVN (Menu_SpecialCodenameCheck usa): o registrador inteiro (64 bits) se o teste vale, senão nada muda.
    MipsMachine m;
    program(m, {
                   addiu(R::t0, R::zero, -1),     // 0xffff...ffff (64 bits)
                   addiu(R::t1, R::zero, 7),
                   addiu(R::s0, R::zero, 1),
                   addiu(R::s1, R::zero, 2),
                   addiu(R::s2, R::zero, 3),
                   addiu(R::s3, R::zero, 4),
                   movz(R::s0, R::t0, R::zero),   // zero == 0: move
                   movz(R::s1, R::t0, R::t1),     // 7 != 0: fica
                   movn(R::s2, R::t1, R::t1),     // 7 != 0: move
                   movn(R::s3, R::t1, R::zero),   // fica
                   jr(R::ra),
                   nop,
               });
    m.call(kCode);
    CHECK(m.gpr(R::s0) == ~0ull && m.gpr(R::s1) == 2 && m.gpr(R::s2) == 7 && m.gpr(R::s3) == 4);
}

TEST(mips_integer_semantics) {
    MipsMachine m;
    program(m, {
                   lui(R::t0, 0x7fff),
                   ori(R::t0, R::t0, 0xffff),
                   addiu(R::s0, R::t0, 1),        // 0x7fffffff + 1: 32 bits, estende o sinal
                   lui(R::s1, 0x8000),            // estende o sinal
                   ori(R::s2, R::zero, 0xffff),   // imediato sem sinal
                   addiu(R::s3, R::zero, -1),
                   srl(R::s4, R::s1, 4),          // lógico sobre os 32 bits de baixo
                   sra(R::s5, R::s1, 4),
                   addiu(R::v0, R::zero, 37),
                   sll(R::t1, R::v0, 4),          // v0 * 15, como em gcc2_compiled. (sll + subu)
                   subu(R::t1, R::t1, R::v0),
                   addiu(R::a0, R::zero, -3),
                   addiu(R::a1, R::zero, 5),
                   mult(R::t2, R::a0, R::a1),     // R5900: rd = LO
                   mflo(R::t3),
                   mfhi(R::t4),
                   slt(R::t5, R::a0, R::a1),
                   sltu(R::t6, R::a0, R::a1),
                   slti(R::t7, R::a0, -2),
                   sltiu(R::t8, R::a1, -1),       // imediato estendido e comparado sem sinal
                   dsll32(R::a2, R::s3, 0),       // os dois juntos estendem o sinal dos 32 bits de baixo
                   dsra32(R::a2, R::a2, 0),
                   daddu(R::a3, R::s1, R::zero),  // move de 64 bits
                   daddiu(R::v1, R::s1, -1),
                   and_(R::k0, R::s3, R::s2),
                   xor_(R::k1, R::s2, R::s2),
                   addu(R::gp, R::s3, R::s3),
                   andi(R::s6, R::s3, 0xf0f0),
                   or_(R::s7, R::s2, R::s1),
                   jr(R::ra),
                   nop,
               });
    m.set_gpr(R::gp, 0);
    m.call(kCode);
    CHECK(m.gpr(R::s0) == 0xffffffff80000000ull);
    CHECK(m.gpr(R::s1) == 0xffffffff80000000ull);
    CHECK(m.gpr(R::s2) == 0xffffull);
    CHECK(m.gpr(R::s3) == ~0ull);
    CHECK(m.gpr(R::s4) == 0x08000000ull);
    CHECK(m.gpr(R::s5) == 0xfffffffff8000000ull);
    CHECK(m.gpr(R::t1) == 37 * 15);
    CHECK(m.gpr(R::t2) == uint64_t(-15));
    CHECK(m.gpr(R::t3) == uint64_t(-15));
    CHECK(m.gpr(R::t4) == ~0ull);
    CHECK(m.gpr(R::t5) == 1);
    CHECK(m.gpr(R::t6) == 0);
    CHECK(m.gpr(R::t7) == 1);
    CHECK(m.gpr(R::t8) == 1);
    CHECK(m.gpr(R::a2) == ~0ull);
    CHECK(m.gpr(R::a3) == 0xffffffff80000000ull);
    CHECK(m.gpr(R::v1) == 0xffffffff7fffffffull);  // daddiu não corta em 32 bits
    CHECK(m.gpr(R::k0) == 0xffff);
    CHECK(m.gpr(R::k1) == 0);
    CHECK(m.gpr(R::gp) == ~0ull - 1);
    CHECK(m.gpr(R::s6) == 0xf0f0);
    CHECK(m.gpr(R::s7) == 0xffffffff8000ffffull);
    CHECK(m.gpr(R::zero) == 0);

    MipsMachine b;
    program(b, {lui(R::a0, 1), ori(R::a1, R::zero, 1), daddu(R::a1, R::a1, R::a0), mult(R::v0, R::a0, R::a1),
                mfhi(R::v1), jr(R::ra), nop});
    b.call(kCode);
    CHECK(b.gpr(R::v0) == 0x10000);  // 0x10000 * 0x10001 = 0x1_0001_0000
    CHECK(b.gpr(R::v1) == 1);
}

TEST(mips_loads_and_stores) {
    MipsMachine m;
    const uint8_t bytes[16] = {0x80, 0x81, 0x82, 0xf3, 0x04, 0x05, 0x06, 0x87,
                               0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    std::memcpy(m.mem(kData, 16), bytes, 16);
    program(m, {
                   lui(R::a0, kData >> 16),
                   lb(R::s0, 0, R::a0),
                   lbu(R::s1, 0, R::a0),
                   lh(R::s2, 2, R::a0),
                   lhu(R::s3, 2, R::a0),
                   lw(R::s4, 4, R::a0),
                   lw(R::s5, 0, R::a0),
                   lwu(R::s6, 0, R::a0),
                   ld(R::s7, 0, R::a0),
                   sb(R::s1, 0x20, R::a0),
                   sh(R::s3, 0x22, R::a0),
                   sw(R::s4, 0x24, R::a0),
                   sd(R::s7, 0x28, R::a0),
                   jr(R::ra),
                   nop,
               });
    m.call(kCode);
    CHECK(m.gpr(R::s0) == 0xffffffffffffff80ull);
    CHECK(m.gpr(R::s1) == 0x80);
    CHECK(m.gpr(R::s2) == 0xfffffffffffff382ull);
    CHECK(m.gpr(R::s3) == 0xf382);
    CHECK(m.gpr(R::s4) == 0xffffffff87060504ull);
    CHECK(m.gpr(R::s5) == 0xfffffffff3828180ull);
    CHECK(m.gpr(R::s6) == 0xf3828180ull);
    CHECK(m.gpr(R::s7) == 0x87060504f3828180ull);
    CHECK(std::memcmp(m.mem(kData + 0x20, 1), bytes, 1) == 0);
    CHECK(std::memcmp(m.mem(kData + 0x22, 2), bytes + 2, 2) == 0);
    CHECK(std::memcmp(m.mem(kData + 0x24, 4), bytes + 4, 4) == 0);
    CHECK(std::memcmp(m.mem(kData + 0x28, 8), bytes, 8) == 0);

    // lw desalinhado é erro de endereço no EE: para.
    MipsMachine u;
    program(u, {lui(R::a0, kData >> 16), lw(R::v0, 2, R::a0), jr(R::ra), nop});
    bool threw = false;
    try {
        u.call(kCode);
    } catch (const MipsError& e) {
        threw = e.pc == kCode + 4;
    }
    CHECK(threw);
}

TEST(mips_lq_sq_move_128_bits_and_ignore_low_address_bits) {
    MipsMachine m;
    program(m, {lui(R::a0, kData >> 16), sq(R::t0, 0x14, R::a0), lq(R::t1, 0x1c, R::a0), jr(R::ra), nop});
    m.set_gpr(R::t0, 0x0123456789abcdefull);
    m.set_gpr_hi(R::t0, 0xfedcba9876543210ull);
    m.call(kCode);
    uint64_t lo, hi;
    std::memcpy(&lo, m.mem(kData + 0x10, 8), 8);  // 0x14 e 0x1c caem no quadword de 0x10
    std::memcpy(&hi, m.mem(kData + 0x18, 8), 8);
    CHECK(lo == 0x0123456789abcdefull);
    CHECK(hi == 0xfedcba9876543210ull);
    CHECK(m.gpr(R::t1) == lo);
    CHECK(m.gpr_hi(R::t1) == hi);
}

TEST(mips_unaligned_64bit_copy_every_alignment) {
    // ldl/ldr + sdl/sdr, como as cópias de estruturas de gcc2_compiled.: para cada alinhamento de
    // origem e destino, os 8 bytes chegam inteiros e os vizinhos ficam intactos. Também na ordem
    // inversa (ldr antes de ldl, sdr antes de sdl).
    for (int order = 0; order < 2; ++order) {
        for (uint32_t so = 0; so < 8; ++so) {
            for (uint32_t d = 0; d < 8; ++d) {
                MipsMachine m;
                const uint32_t src = kData + so, dst = kData + 0x100 + d;
                for (uint32_t i = 0; i < 32; ++i) m.mem(kData, 32)[i] = uint8_t(0x11 + i * 7);
                std::memset(m.mem(kData + 0xf0, 0x30), 0xee, 0x30);
                if (order == 0) {
                    program(m, {ldl(R::t0, 7, R::a0), ldr(R::t0, 0, R::a0), sdl(R::t0, 7, R::a1), sdr(R::t0, 0, R::a1),
                                jr(R::ra), nop});
                } else {
                    program(m, {ldr(R::t0, 0, R::a0), ldl(R::t0, 7, R::a0), sdr(R::t0, 0, R::a1), sdl(R::t0, 7, R::a1),
                                jr(R::ra), nop});
                }
                m.set_gpr(R::t0, 0x5555555555555555ull);  // lixo: tem de ser todo substituído
                m.call(kCode, {src, dst});
                uint64_t want;
                std::memcpy(&want, m.mem(src, 8), 8);
                CHECK(m.gpr(R::t0) == want);
                CHECK(std::memcmp(m.mem(dst, 8), m.mem(src, 8), 8) == 0);
                for (uint32_t a = kData + 0xf0; a < kData + 0x120; ++a) {
                    if (a < dst || a >= dst + 8) CHECK(m.mem(a, 1)[0] == 0xee);
                }
            }
        }
    }
}

TEST(mips_unaligned_32bit_copy_every_alignment) {
    for (uint32_t so = 0; so < 4; ++so) {
        for (uint32_t d = 0; d < 4; ++d) {
            MipsMachine m;
            const uint32_t src = kData + so, dst = kData + 0x100 + d;
            for (uint32_t i = 0; i < 16; ++i) m.mem(kData, 16)[i] = uint8_t(0x81 + i * 13);
            std::memset(m.mem(kData + 0xf8, 0x18), 0xee, 0x18);
            program(m, {lwl(R::t0, 3, R::a0), lwr(R::t0, 0, R::a0), swl(R::t0, 3, R::a1), swr(R::t0, 0, R::a1), jr(R::ra),
                        nop});
            m.set_gpr(R::t0, 0x5555555555555555ull);
            m.call(kCode, {src, dst});
            int32_t want;
            std::memcpy(&want, m.mem(src, 4), 4);
            CHECK(m.gpr(R::t0) == uint64_t(int64_t(want)));  // palavra inteira: sinal estendido
            CHECK(std::memcmp(m.mem(dst, 4), m.mem(src, 4), 4) == 0);
            for (uint32_t a = kData + 0xf8; a < kData + 0x110; ++a) {
                if (a < dst || a >= dst + 4) CHECK(m.mem(a, 1)[0] == 0xee);
            }
        }
    }
}

TEST(mips_partial_lwl_lwr) {
    // Little-endian: lwr em addr carrega os bytes de addr até o fim da palavra na parte baixa do
    // registrador; lwl em addr carrega os bytes do começo da palavra até addr na parte alta.
    MipsMachine m;
    const uint8_t word[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    std::memcpy(m.mem(kData, 4), word, 4);
    program(m, {lwr(R::t0, 1, R::a0), lwl(R::t1, 1, R::a0), jr(R::ra), nop});
    m.set_gpr(R::t0, 0x7777777711223344ull);
    m.set_gpr(R::t1, 0x7777777711223344ull);
    m.call(kCode, {kData});
    CHECK(m.gpr(R::t0) == 0x7777777711ddccbbull);  // lwr parcial não estende o sinal
    CHECK(m.gpr(R::t1) == 0xffffffffbbaa3344ull);
}

TEST(mips_native_hook_and_unhooked_call) {
    auto code = [](uint32_t callee) {
        return std::vector<uint32_t>{
            addiu(R::sp, R::sp, -16),  sq(R::ra, 0, R::sp),  lui(R::a0, kData >> 16), addiu(R::a1, R::zero, 0x5a),
            jal(callee),               addiu(R::a2, R::zero, 37),  // delay slot: o tamanho
            lq(R::ra, 0, R::sp),       jr(R::ra),            addiu(R::sp, R::sp, 16),
        };
    };
    MipsMachine m;
    program(m, code(0x00180000));
    m.hook(0x00180000, "memset", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
        std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    m.call(kCode);
    bool filled = true;
    for (uint32_t i = 0; i < 37; ++i) filled = filled && m.mem(kData + i, 1)[0] == 0x5a;
    CHECK(filled);
    CHECK(m.mem(kData + 37, 1)[0] == 0);
    CHECK(m.gpr(R::v0) == kData);
    CHECK(m.native_calls() == 1);
    CHECK(uint32_t(m.gpr(R::sp)) == MipsMachine::kStackTop);

    // Chamada a função sem hook e fora do código permitido: para no destino.
    MipsMachine u;
    program(u, code(0x00190000));
    uint32_t at = 0;
    try {
        u.call(kCode);
    } catch (const MipsError& e) {
        at = e.pc;
    }
    CHECK(at == 0x00190000);
}

TEST(mips_cop2_mmi_and_new_fpu_ops) {
    // VU0 ligada: lqc2, vadd.xyz em modo macro, sqc2; as MMI que entrelaçam quadwords; sqrt.s e max.s.
    ps2::VuMachine vu(ps2::VuUnit::VU0);
    MipsMachine m;
    m.attach_vu0(&vu);
    const float a[4] = {1, 2, 3, 4}, b[4] = {10, 20, 30, 40};
    std::memcpy(m.mem(kData, 16), a, 16);
    std::memcpy(m.mem(kData + 16, 16), b, 16);
    const uint32_t lqc2_1 = (0x36u << 26) | (uint32_t(R::a0) << 21) | (1u << 16) | 0x00;
    const uint32_t lqc2_2 = (0x36u << 26) | (uint32_t(R::a0) << 21) | (2u << 16) | 0x10;
    const uint32_t vadd_xyz = 0x4a000000u | (0xeu << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28;  // vf3 = vf1 + vf2
    const uint32_t sqc2_3 = (0x3eu << 26) | (uint32_t(R::a0) << 21) | (3u << 16) | 0x20;
    const uint32_t pextlw = (0x1cu << 26) | (uint32_t(R::t1) << 21) | (uint32_t(R::t0) << 16) | (uint32_t(R::t2) << 11) |
                            (0x12u << 6) | 0x08;  // t2 = {t0.w0, t1.w0, t0.w1, t1.w1}
    const uint32_t pcpyud = (0x1cu << 26) | (uint32_t(R::t0) << 21) | (uint32_t(R::t1) << 16) | (uint32_t(R::t3) << 11) |
                            (0x0eu << 6) | 0x29;  // t3 = {t0.hi, t1.hi}
    program(m, {lui(R::a0, kData >> 16), lqc2_1, lqc2_2, vadd_xyz, sqc2_3, lq(R::t0, 0, R::a0), lq(R::t1, 16, R::a0),
                pextlw, sq(R::t2, 0x30, R::a0), pcpyud, sq(R::t3, 0x40, R::a0),
                lwc1(0, 4, R::a0),           // f0 = 2
                fop(0x04, 1, 0, 3),          // sqrt.s f1, f3 (f3 = 0 ainda): 0
                lwc1(3, 0x1c, R::a0),        // f3 = 40
                fop(0x04, 2, 0, 3),          // sqrt.s f2, f3 = sqrt(40) truncado
                fop(0x28, 4, 0, 3),          // max.s f4, f0, f3 = 40
                jr(R::ra), nop});
    m.call(kCode);
    float out[4];
    std::memcpy(out, m.mem(kData + 0x20, 16), 16);
    CHECK(out[0] == 11 && out[1] == 22 && out[2] == 33 && out[3] == 0);  // w não escrito: o vf3 inicial (0)
    uint32_t w[4];
    std::memcpy(w, m.mem(kData + 0x30, 16), 16);
    CHECK(w[0] == ps2::fbits(1) && w[1] == ps2::fbits(10) && w[2] == ps2::fbits(2) && w[3] == ps2::fbits(20));
    std::memcpy(w, m.mem(kData + 0x40, 16), 16);
    CHECK(w[0] == ps2::fbits(3) && w[1] == ps2::fbits(4) && w[2] == ps2::fbits(30) && w[3] == ps2::fbits(40));
    CHECK(m.fpr(1) == 0.0f);
    CHECK(ps2::fbits(m.fpr(2)) == 0x40ca62c1u);  // sqrt(40) = 6.3245553...: truncado (o mais próximo é ...c2)
    CHECK(m.fpr(4) == 40.0f);
}

TEST(mips_div_divu_and_word_exchanges) {
    // DIV e DIVU: o quociente em LO e o resto em HI (truncados; o resto com o sinal do dividendo); PADDW zera 128 bits
    // (o "clear" do GCC), PEXEW e PEXCW trocam as palavras 0 e 2 e 1 e 2.
    MipsMachine m;
    const uint32_t div_ = (uint32_t(R::a0) << 21) | (uint32_t(R::a1) << 16) | 0x1a;   // div a0, a1
    const uint32_t divu_ = (uint32_t(R::a2) << 21) | (uint32_t(R::a1) << 16) | 0x1b;  // divu a2, a1
    const uint32_t paddw = (0x1cu << 26) | (uint32_t(R::t0) << 11) | 0x08;              // paddw t0, zero, zero
    const uint32_t pexew = (0x1cu << 26) | (uint32_t(R::t1) << 16) | (uint32_t(R::t2) << 11) | (0x1eu << 6) | 0x09;
    const uint32_t pexcw = (0x1cu << 26) | (uint32_t(R::t1) << 16) | (uint32_t(R::t3) << 11) | (0x1eu << 6) | 0x29;
    const uint32_t w[4] = {1, 2, 3, 4};
    std::memcpy(m.mem(kData, 16), w, 16);
    program(m, {addiu(R::a0, R::zero, -7), addiu(R::a1, R::zero, 2), div_, mflo(R::s0), mfhi(R::s1),
                addiu(R::a2, R::zero, -7), divu_, mflo(R::s2), mfhi(R::s3), lui(R::v0, kData >> 16), lq(R::t1, 0, R::v0),
                lq(R::t0, 0, R::v0), paddw, pexew, pexcw, sq(R::t0, 0x10, R::v0), sq(R::t2, 0x20, R::v0),
                sq(R::t3, 0x30, R::v0), jr(R::ra), nop});
    m.call(kCode);
    CHECK(int64_t(m.gpr(R::s0)) == -3 && int64_t(m.gpr(R::s1)) == -1);
    CHECK(uint32_t(m.gpr(R::s2)) == 0x7ffffffcu && uint32_t(m.gpr(R::s3)) == 1);
    uint32_t o[4];
    std::memcpy(o, m.mem(kData + 0x10, 16), 16);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] == 0);
    std::memcpy(o, m.mem(kData + 0x20, 16), 16);
    CHECK(o[0] == 3 && o[1] == 2 && o[2] == 1 && o[3] == 4);
    std::memcpy(o, m.mem(kData + 0x30, 16), 16);
    CHECK(o[0] == 1 && o[1] == 3 && o[2] == 2 && o[3] == 4);
}

TEST(mips_madd_pipeline1_variable_shifts_and_uncached_ram) {
    // MADD/MADDU somam o produto ao {HI, LO} de 32 bits cada (rd = LO); o pipeline 1 (MULT1, MADD1, DIV1, MFLO1,
    // MFHI1, MTLO1, MTHI1) tem o seu LO1/HI1; MTLO/MTHI; DSLLV/DSRLV/DSRAV deslocam 64 bits por rs & 63; o espelho sem
    // cache (0x20000000 | endereço) lê a mesma RAM; call passa até 8 argumentos (a0..a3, t0..t3).
    MipsMachine m;
    auto mmi = [](int rd, int rs, int rt, uint32_t funct) {
        return (0x1cu << 26) | (uint32_t(rs) << 21) | (uint32_t(rt) << 16) | (uint32_t(rd) << 11) | funct;
    };
    const uint32_t mtlo = rtype(R::a3, 0, 0, 0, 0x13), mthi = rtype(R::a0, 0, 0, 0, 0x11);
    program(m, {addiu(R::a0, R::zero, 3), addiu(R::a1, R::zero, -5), lui(R::a2, 1), addiu(R::a3, R::zero, 7),
                addiu(R::v1, R::zero, 40), addiu(R::t9, R::zero, 60),
                mult(0, R::a0, R::a1),                  // {HI, LO} = -15
                mmi(R::t0, R::a0, R::a3, 0x00),         // madd: -15 + 21 = 6
                mmi(R::t1, R::a2, R::a2, 0x01),         // maddu: 6 + 2^32
                mfhi(R::t2),                            // 1
                mmi(R::t3, R::a1, R::a3, 0x18),         // mult1: -35
                mmi(R::t4, R::a0, R::a0, 0x20),         // madd1: -35 + 9
                mmi(R::t5, 0, 0, 0x10),                 // mfhi1: -1
                mmi(0, R::a1, R::a0, 0x1a),             // div1: -5 / 3
                mmi(R::t6, 0, 0, 0x12), mmi(R::t7, 0, 0, 0x10),  // mflo1 -1, mfhi1 -2
                mtlo, mflo(R::s0), mthi, mfhi(R::s1),   // 7, 3
                mmi(0, R::a3, 0, 0x13), mmi(R::s5, 0, 0, 0x12),  // mtlo1 a3, mflo1 s5 = 7
                rtype(R::v1, R::a2, R::s2, 0, 0x14),    // dsllv s2 = a2 << 40
                rtype(R::a0, R::a1, R::s3, 0, 0x17),    // dsrav s3 = -5 >> 3
                rtype(R::t9, R::a1, R::s4, 0, 0x16),    // dsrlv s4 = (2^64 - 5) >> 60
                lui(R::t8, int32_t((0x20000000u | kData) >> 16)), lw(R::s6, 0, R::t8), jr(R::ra), nop});
    m.write32(kData, 0x12345678u);
    m.call(kCode);
    CHECK(int64_t(m.gpr(R::t0)) == 6 && int64_t(m.gpr(R::t1)) == 6 && int64_t(m.gpr(R::t2)) == 1);
    CHECK(int64_t(m.gpr(R::t3)) == -35 && int64_t(m.gpr(R::t4)) == -26 && int64_t(m.gpr(R::t5)) == -1);
    CHECK(int64_t(m.gpr(R::t6)) == -1 && int64_t(m.gpr(R::t7)) == -2);
    CHECK(int64_t(m.gpr(R::s0)) == 7 && int64_t(m.gpr(R::s1)) == 3 && int64_t(m.gpr(R::s5)) == 7);
    CHECK(m.gpr(R::s2) == 0x0100000000000000ull && int64_t(m.gpr(R::s3)) == -1 && m.gpr(R::s4) == 0xf);
    CHECK(uint32_t(m.gpr(R::s6)) == 0x12345678u);
    // Oito argumentos.
    program(m, {addu(R::v0, R::a0, R::a1), addu(R::v0, R::v0, R::a2), addu(R::v0, R::v0, R::a3),
                addu(R::v0, R::v0, R::t0), addu(R::v0, R::v0, R::t1), addu(R::v0, R::v0, R::t2),
                addu(R::v0, R::v0, R::t3), jr(R::ra), nop},
            kCode + 0x1000);
    m.call(kCode + 0x1000, {1, 2, 3, 4, 5, 6, 7, 8});
    CHECK(m.gpr(R::v0) == 36);
}

TEST(mips_unknown_or_invalid_instructions_stop) {
    const uint32_t bad[] = {
        0x0000000cu,  // syscall
        0x70000004u,  // plzcw (MMI)
        0x4a000000u,  // COP2 (sem VU0 ligada)
        0x46000020u,  // cvt.s.s (não existe)
        0x00000034u,  // teq
    };
    for (const uint32_t w : bad) {
        MipsMachine m;
        program(m, {nop, w, jr(R::ra), nop});
        bool ok = false;
        try {
            m.call(kCode);
        } catch (const MipsError& e) {
            ok = e.pc == kCode + 4 && e.word == w;
        }
        CHECK(ok);
    }
    // Desvio dentro de um delay slot.
    MipsMachine d;
    program(d, {beq(R::zero, R::zero, 2), j(kCode), nop, jr(R::ra), nop});
    bool threw = false;
    try {
        d.call(kCode);
    } catch (const MipsError& e) {
        threw = e.pc == kCode + 4;
    }
    CHECK(threw);
}

TEST(mips_fpu_rounds_toward_zero_and_saturates) {
    MipsMachine m;
    program(m, {
                   mtc1(R::a0, 1),
                   mtc1(R::a1, 2),
                   mul_s(3, 1, 2),
                   lui(R::t0, kData >> 16),
                   swc1(3, 0, R::t0),
                   lwc1(4, 0, R::t0),
                   mfc1(R::v0, 4),
                   jr(R::ra),
                   nop,
               });
    // (1 + 3*2^-12) * (1 + 2^-12) = 1 + 2^-10 + 1,5 ulp: ao mais próximo daria +2 ulp, para zero +1.
    m.call(kCode, {0x3f801800u, 0x3f800800u});
    CHECK(uint32_t(m.gpr(R::v0)) == 0x3f802001u);
    CHECK(m.read32(kData) == 0x3f802001u);
    CHECK(m.fpr_bits(3) == 0x3f802001u);

    struct Case {
        uint32_t insn;
        uint32_t a, b, want;
    };
    const uint32_t one = fbits(1.0f), two = fbits(2.0f), fmax = 0x7f7fffffu;
    const Case cases[] = {
        {mul_s(3, 1, 2), fmax, two, fmax},                   // estoura: satura, sem infinito
        {mul_s(3, 1, 2), 0x00800000u, 0x3f000000u, 0},       // FLT_MIN / 2: sem denormal
        {div_s(3, 1, 2), one, 0, fmax},                      // divisão por zero
        {div_s(3, 1, 2), 0x80000000u | one, 0, 0x80000000u | fmax},
        {div_s(3, 1, 2), one, fbits(3.0f), 0x3eaaaaaau},     // 1/3 truncado (ao mais próximo: ...ab)
        {sub_s(3, 1, 2), one, fbits(0x1p-30f), 0x3f7fffffu},  // 1 - 2^-30: o float abaixo de 1
        {sub_s(3, 1, 2), one, fbits(0x1p-60f), 0x3f7fffffu},  // mesmo quando some no double
        {add_s(3, 1, 2), one, fbits(0x1p-60f), one},
        {add_s(3, 1, 2), one, two, fbits(3.0f)},
        {mul_s(3, 1, 2), 0x7f800000u, one, fmax},            // expoente 255 é número, não infinito
        {mul_s(3, 1, 2), 0x00000001u, fbits(1e30f), 0},      // entrada denormal vale 0
        {mov_s(3, 1), fbits(-1.5f), 0, fbits(-1.5f)},
        {neg_s(3, 1), fbits(-1.5f), 0, fbits(1.5f)},
        {cvt_w_s(3, 1), fbits(-2.7f), 0, uint32_t(-2)},
        {cvt_w_s(3, 1), fbits(2.9f), 0, 2},
        {cvt_w_s(3, 1), fbits(3e9f), 0, 0x7fffffffu},
        {cvt_w_s(3, 1), fbits(-3e9f), 0, 0x80000000u},
        {cvt_s_w(3, 1), 16777219u, 0, 0x4b800001u},          // 2^24 + 3 -> 2^24 + 2 (truncado)
        {cvt_s_w(3, 1), uint32_t(-7), 0, fbits(-7.0f)},
    };
    for (const Case& c : cases) {
        MipsMachine f;
        program(f, {mtc1(R::a0, 1), mtc1(R::a1, 2), c.insn, mfc1(R::v0, 3), jr(R::ra), nop});
        f.call(kCode, {c.a, c.b});
        CHECK(uint32_t(f.gpr(R::v0)) == c.want);
    }
}

TEST(mips_fpu_compare_and_branch) {
    // c.lt.s/c.eq.s + bc1t/bc1f/bc1tl/bc1fl.
    struct Case {
        uint32_t cmp;
        float a, b;
        uint32_t kind;  // 0 bc1f, 1 bc1t, 2 bc1fl, 3 bc1tl
        bool taken;
    };
    const Case cases[] = {
        {c_lt_s(1, 2), 1.0f, 2.0f, 1, true},  {c_lt_s(1, 2), 2.0f, 1.0f, 1, false},
        {c_lt_s(1, 2), 2.0f, 1.0f, 0, true},  {c_eq_s(1, 2), 2.0f, 2.0f, 3, true},
        {c_eq_s(1, 2), 2.0f, 1.0f, 3, false}, {c_eq_s(1, 2), 2.0f, 1.0f, 2, true},
    };
    for (const Case& c : cases) {
        MipsMachine m;
        program(m, {mtc1(R::a0, 1), mtc1(R::a1, 2), c.cmp, addiu(R::v0, R::zero, 0), bc1(c.kind, 3),
                    addiu(R::v1, R::zero, 7), addiu(R::v0, R::zero, 1), jr(R::ra), nop, jr(R::ra), nop});
        m.call(kCode, {fbits(c.a), fbits(c.b)});
        CHECK((m.gpr(R::v0) == 0) == c.taken);
        CHECK((m.gpr(R::v1) == 7) == (c.taken || (c.kind & 2) == 0));
    }
}

TEST(mips_runaway_loop_hits_step_limit) {
    MipsMachine m;
    program(m, {beq(R::zero, R::zero, -1), nop});
    bool threw = false;
    try {
        m.call(kCode, {}, 1000);
    } catch (const MipsError&) {
        threw = true;
    }
    CHECK(threw);
}

// ---------------------------------------------------------------------------------------------
// game/static_init.cpp de ponta a ponta, com um ELF sintético no formato do ACTION.ELF: um segmento
// PT_LOAD (.text, .data, .bss), __CTOR_LIST__, um _GLOBAL_$I$ que chama o
// __static_initialization_and_destruction_0, memset/memcpy/PS2Sinf__Ff (nativas) e as tabelas.

namespace {

constexpr uint32_t kText = 0x00100000, kDataSec = 0x00108000, kBss = 0x00110000;
constexpr uint32_t kFileImage = 0x1000;  // offset do segmento no arquivo
constexpr uint32_t kTextSize = 0x8000, kDataSize = 0x3000, kBssSize = 0x30000;
// Funções do .text.
constexpr uint32_t kMemset = kText, kMemcpy = kText + 0x08, kSinf = kText + 0x10, kGadget = kText + 0x18;
constexpr uint32_t kDoGlobalCtors = kText + 0x20, kGlobalI = kText + 0x40, kStaticInit = kText + 0x60;
// Variáveis.
constexpr uint32_t kCtorList = kDataSec, kAmmo = kDataSec + 0x10, kEffects = kDataSec + 0x200, kWeapons = kBss;
// Tabelas dos personagens da IA no .bss, depois de weapon_data (as listas DASC_* terminam onde começa
// DroneAnimStates, como no ELF).
constexpr uint32_t kDrone = kBss + 0x8000;
constexpr uint32_t kTypeSettings = kDrone, kModeSettings = kDrone + 0x400, kAnimInfo = kDrone + 0x600;
constexpr uint32_t kAnimTables = kDrone + 0x3000, kDasc = kDrone + 0xe800, kAnimStates = kDasc + 0x2468;
constexpr uint32_t kAnimFunc = kDrone + 0x11200, kStateFunc = kDrone + 0x11210, kStateFuncs = kDrone + 0x11220;
// As constantes DroneFiring_* (0x48 bytes) vão até DroneCaptain_Mod_BulletDamage, como no ELF.
constexpr uint32_t kBitDescs = kDrone + 0x11610, kBitMasks = kDrone + 0x116d0, kFiring = kDrone + 0x11768;
constexpr uint32_t kCaptain = kFiring + 0x48, kDamage = kDrone + 0x117c0, kSfxInfo = kDrone + 0x117f0;
constexpr uint32_t kCollBones = kDrone + 0x14330;
constexpr uint32_t kViewBones = kCollBones + 0x2c0;
constexpr uint32_t kCCModes = kViewBones + 0x40;  // CCModeCrouchLeanL..CCModes, LCModeCrouch
constexpr uint32_t kPlrDMod = kCCModes + 0x100;  // Plr_DMod_Multi..Plr_DMod_Hard
// Conjuntos de armas dos itens no .data, depois de EffectInfo (UseableGuns logo depois, como no ELF).
constexpr uint32_t kPickupMatrix = kDataSec + 0xe40, kUseableGuns = kPickupMatrix + 0x70;
// Fontes e a tarja de cinema: FontTable, BorderInfo e as três FONT no .data (FontTable[0] aponta a
// primeira, que aponta os pares e os glifos dela); os glifos e os pares da NFont2 também no .data (o
// glifo 1 aponta o terceiro par); o resto no .bss, depois de Plr_DMod.
constexpr uint32_t kFontTable = kDataSec + 0xf00, kBorderInfo = kDataSec + 0xf30, kFontNFont2 = kDataSec + 0xf60;
constexpr uint32_t kFontDataNFont2 = kDataSec + 0x1000, kKernNFont2 = kDataSec + 0x1e00;
constexpr uint32_t kFontBss = kPlrDMod + 0x100;
constexpr uint32_t kKernMedium = kFontBss, kKernSerp = kFontBss + 0xc00, kFontDataMedium = kFontBss + 0x1400;
constexpr uint32_t kFontDataSerp = kFontBss + 0x2200, kSpecialChar = kFontBss + 0x3000;
// As tabelas das legendas de fala, depois das fontes (o .bss vai até 0x132000).
constexpr uint32_t kSnd2Lbl = kFontBss + 0x3200, kSfxOutput = kSnd2Lbl + 0x1000, kSubtitleMod = kSfxOutput + 0x9200;
// As tabelas dos menus, depois das legendas.
constexpr uint32_t kMenuTables = kSubtitleMod + 0x10;
// A pontuação no .data, depois das fontes: as linhas das 12 missões (uma a cada 0xb8 bytes) até ScoringTable,
// que aponta as linhas de cada missão (+0x18), e ScoringTableDifMul.
constexpr uint32_t kScoreRowsSym = kDataSec + 0x2000, kScoringTable = kScoreRowsSym + 0x8a0;
constexpr uint32_t kDifMul = kScoringTable + 0x2d0;

struct FakeSym {
    const char* name;
    uint32_t value, size;
    uint8_t type;  // 1 OBJECT, 2 FUNC
    uint16_t shndx;
};

void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
void put16(std::vector<uint8_t>& b, size_t o, uint16_t v) { std::memcpy(b.data() + o, &v, 2); }

// Monta o ELF. bad_call: a inicialização chama Gadget_Fire (sem hook, fora do código liberado).
// weapon_size: tamanho do símbolo weapon_data. fill: bytes de weapon_data[0] preenchidos com 0x33
// (0x10c suja o ponteiro de função em +0x104).
nf::Bytes fake_elf(bool bad_call, uint32_t weapon_size, int32_t fill = 0x100) {
    std::vector<uint8_t> image(kTextSize + kDataSize, 0);
    auto code = [&](uint32_t addr, const std::vector<uint32_t>& words) {
        for (size_t i = 0; i < words.size(); ++i) put32(image, addr - kText + i * 4, words[i]);
    };
    for (uint32_t f : {kMemset, kMemcpy, kSinf, kGadget}) code(f, {jr(R::ra), nop});
    code(kDoGlobalCtors, {addiu(R::sp, R::sp, -16), sq(R::ra, 0, R::sp), jal(kGlobalI), nop, lq(R::ra, 0, R::sp),
                          jr(R::ra), addiu(R::sp, R::sp, 16)});
    code(kGlobalI, {0x27bdfff0u, 0x24040001u, 0x7fbf0000u, jal(kStaticInit), 0x3405ffffu, 0x7bbf0000u, 0x03e00008u,
                    0x27bd0010u});
    const int16_t e5 = 5 * 0x10c, e6 = 6 * 0x10c;
    std::vector<uint32_t> init = {
        addiu(R::sp, R::sp, -16),
        sq(R::ra, 0, R::sp),
        lui(R::a0, kWeapons >> 16),
        addiu(R::a1, R::zero, 0x33),
        jal(kMemset),
        addiu(R::a2, R::zero, fill),  // memset(&weapon_data[0], 0x33, fill)
        lui(R::t0, kWeapons >> 16),
        addiu(R::t1, R::zero, 5),
        sh(R::t1, e5, R::t0),  // weapon_data[5].id = 5
        lui(R::t2, kGadget >> 16),
        ori(R::t2, R::t2, kGadget & 0xffff),
        sw(R::t2, e5 + 0x104, R::t0),  // weapon_data[5] +0x104 = Gadget_Fire
        lui(R::t3, 0x0500),
        ori(R::t3, R::t3, 0x0088),
        sw(R::t3, e5 + 0xdc, R::t0),  // pele
        lui(R::t4, kAmmo >> 16),
        ori(R::t4, R::t4, kAmmo & 0xffff),
        sw(R::t4, e6 + 0x10, R::t0),  // um ponteiro de dados (para ammo_data): só aviso
    };
    // DroneAnimStates[1].calls = &DASC_AAA_Undefined + 0x10 (traduzido para a cópia) e
    // DroneModeSettings[0].init = Gadget_Fire (o resolvedor do teste o conhece).
    const uint32_t calls = kAnimStates + 12 + 8, dasc = kDasc + 0x10, mode_init = kModeSettings + 8;
    init.insert(init.end(), {lui(R::t5, dasc >> 16), ori(R::t5, R::t5, dasc & 0xffff), lui(R::t6, calls >> 16),
                             ori(R::t6, R::t6, calls & 0xffff), sw(R::t5, 0, R::t6), lui(R::t7, mode_init >> 16),
                             ori(R::t7, R::t7, mode_init & 0xffff), sw(R::t2, 0, R::t7)});
    if (bad_call) init.insert(init.end(), {jal(kGadget), nop});
    init.insert(init.end(), {lq(R::ra, 0, R::sp), jr(R::ra), addiu(R::sp, R::sp, 16)});
    code(kStaticInit, init);
    // .data: __CTOR_LIST__ = {1, _GLOBAL_$I$, 0}; ammo_data[1].max = 70.
    put32(image, kCtorList - kText, 1);
    put32(image, kCtorList - kText + 4, kGlobalI);
    put16(image, kAmmo - kText + 0xc + 2, 70);
    // EffectInfo[1]: o nome (ponteiro de dados, zerado na cópia) e a marca de bala.
    put32(image, kEffects - kText + 0x88, kAmmo);
    put32(image, kEffects - kText + 0x88 + 4, 0x02000262);
    // PickupMatrix[0] = 6, o último (o conjunto sorteado) = 82; UseableGuns[26] = 82.
    put16(image, kPickupMatrix - kText, 6);
    put16(image, kPickupMatrix - kText + 108, 82);
    put16(image, kUseableGuns - kText + 52, 82);
    put32(image, kFontTable - kText, 0x03000005);
    put32(image, kFontTable - kText + 4, kFontNFont2);
    put32(image, kFontNFont2 - kText, 42);
    put32(image, kFontNFont2 - kText + 4, kKernNFont2);
    put32(image, kFontNFont2 - kText + 0xc, 149);
    put32(image, kFontNFont2 - kText + 0x10, kFontDataNFont2);
    put32(image, kFontDataNFont2 - kText + 0x18 + 0x10, kKernNFont2 + 12);
    put32(image, kBorderInfo - kText, 0xff);
    // ScoringTable[1]: a missão 07000005 com as linhas da segunda missão (os pontos da linha 0 = 1234);
    // o bônus do 00 Agent x2.
    put32(image, kScoringTable - kText + 0x3c, 0x07000005);
    put32(image, kScoringTable - kText + 0x3c + 0x18, kScoreRowsSym + 0xb8);
    put32(image, kScoreRowsSym - kText + 0xb8 + 4, 1234);
    put32(image, kDifMul - kText + 8, 0x40000000);

    const FakeSym syms[] = {
        {"memset", kMemset, 8, 2, 1},
        {"memcpy", kMemcpy, 8, 2, 1},
        {"PS2Sinf__Ff", kSinf, 8, 2, 1},
        {"Gadget_Fire", kGadget, 8, 2, 1},
        {"__do_global_ctors", kDoGlobalCtors, 0x20, 2, 1},
        {"_GLOBAL_$I$weapon_data", kGlobalI, 0x20, 2, 1},
        {"__static_initialization_and_destruction_0", kStaticInit, uint32_t(init.size() * 4), 2, 1},
        {"__CTOR_LIST__", kCtorList, 12, 1, 2},
        {"ammo_data", kAmmo, 408, 1, 2},
        {"EffectInfo", kEffects, 3128, 1, 2},
        {"weapon_data", kWeapons, weapon_size, 1, 3},
        {"DroneTypeSettings", kTypeSettings, 1020, 1, 3},
        {"DroneModeSettings", kModeSettings, 420, 1, 3},
        {"Drone_AnimInfo", kAnimInfo, 10464, 1, 3},
        {"Drone_AnimTables", kAnimTables, 47088, 1, 3},
        {"DASC_AAA_Undefined", kDasc, 720, 1, 3},
        {"DroneAnimStates", kAnimStates, 1428, 1, 3},
        {"DroneData_AnimFunc", kAnimFunc, 16, 1, 3},
        {"DroneData_DAStateFunc", kStateFunc, 16, 1, 3},
        {"NDrone2_StateFuncs", kStateFuncs, 1000, 1, 3},
        {"bitDescs", kBitDescs, 182, 1, 3},
        {"bitMasks", kBitMasks, 140, 1, 3},
        {"DroneFiring_BurstDelay_Min", kFiring, 4, 1, 3},
        {"DroneCaptain_Mod_BulletDamage", kCaptain, 4, 1, 3},
        {"DroneCaptain_Mod_BulletAccuracy", kCaptain + 4, 4, 1, 3},
        {"DroneCaptain_Mod_Health", kCaptain + 8, 4, 1, 3},
        {"DroneDamage_Easy", kDamage, 4, 1, 3},
        {"DroneDamage_Normal", kDamage + 4, 4, 1, 3},
        {"DroneDamage_Hard", kDamage + 8, 4, 1, 3},
        {"DroneDamage_Head", kDamage + 0xc, 4, 1, 3},
        {"DroneDamage_Legs", kDamage + 0x10, 4, 1, 3},
        {"DroneDamage_Arms", kDamage + 0x14, 4, 1, 3},
        {"DroneDamage_Torso", kDamage + 0x18, 4, 1, 3},
        {"DroneArmour_Helmet", kDamage + 0x1c, 4, 1, 3},
        {"DroneArmour_Combat", kDamage + 0x20, 4, 1, 3},
        {"DroneArmour_Jacket", kDamage + 0x24, 4, 1, 3},
        {"DroneArmour_Vest", kDamage + 0x28, 4, 1, 3},
        {"Drone_SfxInfo", kSfxInfo, 11072, 1, 3},
        {"HumanCollisionBones", kCollBones, 340, 1, 3},
        {"AstronautCollisionBones", kCollBones + 0x160, 340, 1, 3},
        {"Drone_View_Bones", kViewBones, 0x20, 1, 3},
        {"CCModeCrouchLeanL", kCCModes, 12, 1, 3},
        {"CCModes", kCCModes + 0x90, 40, 1, 3},
        {"LCModeCrouch", kCCModes + 0xc0, 12, 1, 3},
        {"Plr_DMod_Multi", kPlrDMod, 4, 1, 3},
        {"Plr_DMod_Head", kPlrDMod + 4, 4, 1, 3},
        {"Plr_DMod_LowerLimb", kPlrDMod + 8, 4, 1, 3},
        {"Plr_DMod_UpperLimb", kPlrDMod + 0xc, 4, 1, 3},
        {"Plr_DMod_Easy", kPlrDMod + 0x10, 4, 1, 3},
        {"Plr_DMod_Normal", kPlrDMod + 0x14, 4, 1, 3},
        {"Plr_DMod_Hard", kPlrDMod + 0x18, 4, 1, 3},
        {"PickupMatrix", kPickupMatrix, 110, 1, 2},
        {"UseableGuns", kUseableGuns, 54, 1, 2},
        {"FontTable", kFontTable, 40, 1, 2},
        {"BorderInfo", kBorderInfo, 44, 1, 2},
        {"FONTNFont2", kFontNFont2, 28, 1, 2},
        {"FONTMedium", kFontNFont2 + 0x20, 28, 1, 2},
        {"FONTSerpLight", kFontNFont2 + 0x40, 28, 1, 2},
        {"FontDataNFont2", kFontDataNFont2, 3576, 1, 2},
        {"KernNFont2", kKernNFont2, 252, 1, 2},
        {"KernMedium", kKernMedium, 2652, 1, 3},
        {"KernSerpLight", kKernSerp, 1926, 1, 3},
        {"FontDataMedium", kFontDataMedium, 3576, 1, 3},
        {"FontDataSerpLight", kFontDataSerp, 3576, 1, 3},
        {"specialchar", kSpecialChar, 408, 1, 3},
        {"Snd2Lbl", kSnd2Lbl, 3872, 1, 3},
        {"SFXOutputData", kSfxOutput, 37176, 1, 3},
        {"g_fSubtitleDurationModification", kSubtitleMod, 4, 1, 3},
        {"help_table", kMenuTables, 1560, 1, 3},
        {"CursorInfo", kMenuTables + 0x620, 44, 1, 3},
        {"RewardsTable", kMenuTables + 0x660, 768, 1, 3},
        {"VersionString", kMenuTables + 0x960, 29, 1, 3},
        // O menu principal, o perfil gravado e o cartão (zerados: só os tamanhos contam aqui).
        {"ls", kMenuTables + 0x980, 0x44, 1, 3},
        {"def_codename", kMenuTables + 0x9d0, 0x70, 1, 3},
        {"cn_options", kMenuTables + 0xa40, 0xa8, 1, 3},
        {"difficulty", kMenuTables + 0xaf0, 0x48, 1, 3},
        {"SaveIFFBlocks", kMenuTables + 0xb40, 0x60, 1, 3},
        {"NumSaveBlocks", kMenuTables + 0xba0, 4, 1, 3},
        {"ascii_table", kMenuTables + 0xbb0, 12, 1, 3},
        {"ascii_special", kMenuTables + 0xbc0, 132, 1, 3},
        {"bgcolor", kMenuTables + 0xc50, 64, 1, 3},
        {"ambient", kMenuTables + 0xc90, 16, 1, 3},
        // O dossiê (engine/menu_dossier.h).
        {"ds_options", kMenuTables + 0xca0, 0x60, 1, 3},
        {"ds_weapons", kMenuTables + 0xd00, 0x288, 1, 3},
        {"ds_gadgets", kMenuTables + 0xf90, 0x150, 1, 3},
        // A música (engine/sound.cpp).
        {"MapMusic", kMenuTables + 0x1100, 2660, 1, 3},
        {"MapDroneData", kMenuTables + 0x1b80, 192, 1, 3},
        {"PowerSfx", kMenuTables + 0x1c40, 144, 1, 3},
        // O dano por peça dos helicópteros (engine/copter.h).
        {"Bits", kMenuTables + 0x1cd0, 72, 1, 3},
        // O sistema de segurança (engine/sensor.h) e os canhões montados (engine/gunimp.h).
        {"Laser1.134", kMenuTables + 0x1d20, 56, 1, 3},
        {"Laser2.135", kMenuTables + 0x1d60, 56, 1, 3},
        {"Cam1.132", kMenuTables + 0x1da0, 56, 1, 3},
        {"Cam2.133", kMenuTables + 0x1de0, 56, 1, 3},
        {"MIN3", kMenuTables + 0x1e20, 4, 1, 3},
        {"SSysDelayTimer", kMenuTables + 0x1e30, 4, 1, 3},
        {"TurretGuns", kMenuTables + 0x1e40, 336, 1, 3},
        {"HT_Level_Driving_Paris_ST", kScoreRowsSym, 180, 1, 2},
        {"ScoringTable", kScoringTable, 720, 1, 2},
        {"ScoringTableDifMul", kDifMul, 12, 1, 2},
    };
    std::vector<uint8_t> strtab(1, 0), symtab(16, 0);
    for (const FakeSym& s : syms) {
        std::vector<uint8_t> e(16, 0);
        put32(e, 0, uint32_t(strtab.size()));
        put32(e, 4, s.value);
        put32(e, 8, s.size);
        e[12] = uint8_t(0x10 | s.type);  // GLOBAL
        put16(e, 14, s.shndx);
        symtab.insert(symtab.end(), e.begin(), e.end());
        strtab.insert(strtab.end(), s.name, s.name + std::strlen(s.name) + 1);
    }
    const char shstr[] = "\0.text\0.data\0.bss\0.symtab\0.strtab\0.shstrtab";
    const std::vector<uint8_t> shstrtab(shstr, shstr + sizeof(shstr));

    std::vector<uint8_t> f(kFileImage, 0);
    f.insert(f.end(), image.begin(), image.end());
    const uint32_t symoff = uint32_t(f.size());
    f.insert(f.end(), symtab.begin(), symtab.end());
    const uint32_t stroff = uint32_t(f.size());
    f.insert(f.end(), strtab.begin(), strtab.end());
    const uint32_t shstroff = uint32_t(f.size());
    f.insert(f.end(), shstrtab.begin(), shstrtab.end());
    while (f.size() % 4) f.push_back(0);
    const uint32_t shoff = uint32_t(f.size());
    struct Sh {
        uint32_t name, type, flags, addr, offset, size, link, entsize;
    };
    const Sh sh[] = {
        {0, 0, 0, 0, 0, 0, 0, 0},
        {1, 1, 6, kText, kFileImage, kTextSize, 0, 0},                      // .text
        {7, 1, 3, kDataSec, kFileImage + kTextSize, kDataSize, 0, 0},       // .data
        {13, 8, 3, kBss, kFileImage + kTextSize + kDataSize, kBssSize, 0, 0},  // .bss
        {18, 2, 0, 0, symoff, uint32_t(symtab.size()), 5, 16},              // .symtab
        {26, 3, 0, 0, stroff, uint32_t(strtab.size()), 0, 0},               // .strtab
        {34, 3, 0, 0, shstroff, uint32_t(shstrtab.size()), 0, 0},           // .shstrtab
    };
    for (const Sh& s : sh) {
        std::vector<uint8_t> e(0x28, 0);
        put32(e, 0, s.name);
        put32(e, 4, s.type);
        put32(e, 8, s.flags);
        put32(e, 12, s.addr);
        put32(e, 16, s.offset);
        put32(e, 20, s.size);
        put32(e, 24, s.link);
        put32(e, 36, s.entsize);
        f.insert(f.end(), e.begin(), e.end());
    }
    // Cabeçalho ELF32 little-endian MIPS e o PT_LOAD (.bss além do filesz).
    const uint8_t ident[16] = {0x7f, 'E', 'L', 'F', 1, 1, 1};
    std::memcpy(f.data(), ident, 16);
    put16(f, 0x10, 2);
    put16(f, 0x12, 8);
    put32(f, 0x14, 1);
    put32(f, 0x18, kDoGlobalCtors);
    put32(f, 0x1c, 0x34);
    put32(f, 0x20, shoff);
    put16(f, 0x28, 0x34);
    put16(f, 0x2a, 0x20);
    put16(f, 0x2c, 1);
    put16(f, 0x2e, 0x28);
    put16(f, 0x30, uint16_t(std::size(sh)));
    put16(f, 0x32, 6);
    put32(f, 0x34, 1);
    put32(f, 0x38, kFileImage);
    put32(f, 0x3c, kText);
    put32(f, 0x40, kText);
    put32(f, 0x44, kTextSize + kDataSize);
    put32(f, 0x48, kTextSize + kDataSize + kBssSize);
    put32(f, 0x4c, 7);
    return f;
}

uint32_t test_resolver(const char* name, uint32_t ps2_address) {
    return std::strcmp(name, "Gadget_Fire") == 0 && ps2_address == kGadget ? 0xc0de0001u : 0;
}

bool any_line(const std::vector<std::string>& log, const char* a, const char* b) {
    for (const std::string& l : log) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST(static_init_runs_constructors_and_copies_tables) {
    const nf::Elf elf(fake_elf(false, kWeaponDataSize));
    CHECK(elf.segments().size() == 1);
    CHECK(elf.symbol_at(kStaticInit + 8, 2) != nullptr && elf.symbol_at(kStaticInit + 8, 2)->value == kStaticInit);
    CHECK(nf::static_constructors(elf) == std::vector<uint32_t>{kGlobalI});

    std::string error;
    std::vector<std::string> log;
    CHECK(nf::init_game_tables(elf, &test_resolver, &error, &log));
    if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
    CHECK(error.empty());
    int16_t id;
    uint32_t skin, callback;
    std::memcpy(&id, weapon_data + 5 * kWeaponStride, 2);
    std::memcpy(&skin, weapon_data + 5 * kWeaponStride + 0xdc, 4);
    std::memcpy(&callback, weapon_data + 5 * kWeaponStride + kWeaponCallbackOffset, 4);
    CHECK(id == 5);
    CHECK(skin == 0x05000088u);
    CHECK(callback == 0xc0de0001u);  // traduzido, nunca o endereço do PS2
    CHECK(weapon_data[0] == 0x33 && weapon_data[0xff] == 0x33 && weapon_data[0x100] == 0);  // memset nativo
    int16_t max;
    std::memcpy(&max, ammo_data + kAmmoStride + 2, 2);
    CHECK(max == 70);  // .data comum, copiado como está
    CHECK(EffectInfo[1].name == 0 && EffectInfo[1].decal == 0x02000262u);  // o ponteiro do nome é zerado
    CHECK(PickupMatrix[0] == 6 && PickupMatrix[54] == 82 && UseableGuns[26] == 82 && UseableGuns[0] == 0);
    CHECK(any_line(log, "weapon_data[5]+0x104", "Gadget_Fire"));
    // Ponteiro para dentro de outra tabela copiada e o segundo campo de código.
    CHECK(DroneAnimStates[1].calls == DroneAnimStateCalls + 0x10 && DroneAnimStates[0].calls == nullptr);
    CHECK(nf::p32_fn_u(nf::raw(DroneModeSettings[0].init)) == 0xc0de0001u);
    std::memset(DroneModeSettings, 0, sizeof(DroneModeSettings));
    std::memset(DroneAnimStates, 0, sizeof(DroneAnimStates));
    CHECK(any_line(log, "weapon_data[6]+0x10", "ammo_data"));  // ponteiro de dados avisado
    // Fontes: dois ponteiros por entrada (FONT) e um para qualquer tabela copiada (FontTable, "*").
    CHECK(FontTable[0].hash == 0x03000005u && FontTable[0].font == &FONTNFont2 && FontTable[1].font == nullptr);
    CHECK(FONTNFont2.kern == KernNFont2 && FONTNFont2.chars == FontDataNFont2 && FONTNFont2.count == 149);
    CHECK(FONTNFont2.kern_count == 42 && FONTMedium.kern == nullptr);
    CHECK(FontDataNFont2[1].kern == &KernNFont2[2] && FontDataNFont2[0].kern == nullptr);
    CHECK(BorderInfo.colour == 0xffu);
    CHECK(any_line(log, "FONTNFont2", "KernNFont2 e FontDataNFont2"));
    std::memset(FontTable, 0, sizeof(FontTable));
    std::memset(&FONTNFont2, 0, sizeof(FONTNFont2));
    std::memset(FontDataNFont2, 0, sizeof(FontDataNFont2));
    std::memset(&BorderInfo, 0, sizeof(BorderInfo));
    // A pontuação: o ponteiro das linhas vira o lugar correspondente na cópia (ScoringRows).
    CHECK(ScoringTable[1].level == 0x07000005u && ScoringTable[1].rows == ScoringRows[1].row);
    CHECK(ScoringRows[1].row[0].points == 1234u && ScoringTable[0].rows == nullptr && ScoringTableDifMul[2] == 2.0f);
    CHECK(any_line(log, "ScoringTable", "1 ponteiros para HT_Level_Driving_Paris_ST"));
    std::memset(ScoringTable, 0, sizeof(ScoringTable));
    std::memset(ScoringRows, 0, sizeof(ScoringRows));
    std::memset(ScoringTableDifMul, 0, sizeof(ScoringTableDifMul));

    const std::vector<nf::ConstructorEffect> survey = nf::survey_static_constructors(elf);
    CHECK(survey.size() == 1 && survey[0].error.empty() && survey[0].name == "_GLOBAL_$I$weapon_data");
    // weapon_data e as duas palavras das tabelas dos personagens.
    CHECK(survey.size() == 1 && survey[0].writes.size() == 3 && survey[0].writes[0].symbol == "weapon_data");

    // Chamada a função sem hook: falha com o endereço; tamanho do símbolo diferente do port: falha.
    error.clear();
    CHECK(!nf::init_game_tables(nf::Elf(fake_elf(true, kWeaponDataSize)), &test_resolver, &error));
    CHECK(error.find("0x00100018") != std::string::npos);
    error.clear();
    CHECK(!nf::init_game_tables(nf::Elf(fake_elf(false, kWeaponDataSize - 4)), &test_resolver, &error));
    CHECK(error.find("weapon_data") != std::string::npos);
    // Lixo no ponteiro de função (não é o início de uma função): falha em vez de copiar.
    error.clear();
    CHECK(!nf::init_game_tables(nf::Elf(fake_elf(false, kWeaponDataSize, 0x10c)), &test_resolver, &error));
    CHECK(error.find("weapon_data[0]+0x104") != std::string::npos);

    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    std::memset(EffectInfo, 0, sizeof(EffectInfo));
    pickup_debug_power_on();
}
