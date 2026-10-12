// A física na VU0 (driving/physics/vu0_physics.h): o microprograma VU0_physics portado à mão e o lado do EE
// (Timestep_LoadAndStartVU0, Timestep_CollectVU0data, ResolveLeverForces, Resolve_CollectVU0data).
//
// Cada porte segue o microcódigo instrução por instrução (os números são os pares do programa, como o
// tools/vudis.py DRIVING.ELF --prog=1 os mostra). O hardware para o pipeline quando uma instrução lê um resultado que
// ainda não saiu, então a ordem das contas é a do programa; só Q (DIV/RSQRT) e os indicadores têm latência visível, e
// nestes programas nenhuma leitura de Q acontece antes do resultado chegar (os DIV ficam 10 a 28 pares antes do uso e
// o RSQRT tem WAITQ), nem o FMAND lê um indicador velho (o ADD que ele testa está exatamente 4 pares antes).

#include "driving/physics/vu0_physics.h"

#include <cstring>

#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

ps2::VuMachine& vu0_storage() {
    static ps2::VuMachine m(ps2::VuUnit::VU0);
    return m;
}
Vu0Mode g_mode = Vu0Mode::Native;
std::vector<uint8_t> g_program;
GroundImpactStats g_impacts;

constexpr uint32_t kOne = 0x3f800000;

}  // namespace

ps2::VuMachine& vu0() { return vu0_storage(); }

void vu0_reset() {
    vu0_storage() = ps2::VuMachine(ps2::VuUnit::VU0);
    if (g_mode == Vu0Mode::Interpreted) upload_vu0_physics();
}

void set_vu0_mode(Vu0Mode mode) { g_mode = mode; }
Vu0Mode vu0_mode() { return g_mode; }
void set_vu0_physics_program(std::vector<uint8_t> code) { g_program = std::move(code); }
bool has_vu0_physics_program() { return !g_program.empty(); }

void upload_vu0_physics() {
    if (g_mode == Vu0Mode::Interpreted && !g_program.empty())
        vu0().load_micro(0, g_program.data(), uint32_t(g_program.size()));
}

void vu0_callms(uint32_t instr) {
    if (g_mode == Vu0Mode::Interpreted) {
        vu0().run(instr);
        return;
    }
    if (!vu0_physics_native(vu0(), instr)) physics_pending("vcallms (programa sem porte)");
}

bool vu0_physics_native(ps2::VuMachine& vu, uint32_t instr) {
    switch (instr) {
    case 0x000: VU0MICRO_QuatToMatrix(vu); return true;
    case 0x022: VU0MICRO_MultiplyMatrix(vu); return true;
    case 0x034: VU0MICRO_ResolveLeverForces(vu); return true;
    case 0x0ad: VU0MICRO_UpdatePositionAndOrientation(vu); return true;
    default: return false;
    }
}

// ---------------------------------------------------------------------------------------------------- 0x000

namespace {

// As instruções 0x000-0x01f (e 0x10f-0x12e do passo): o quatérnio q em vf08, vf09, vf10 (as linhas da rotação),
// com vf04..vf07 = q * q.x, q.y, q.z, q.w dobrados como temporários.
void quat_to_matrix(ps2::VuRegs& r, const uint32_t* q) {
    auto V = [&](int n) { return r.vf[n]; };
    const uint32_t* vf00 = r.vf[0];
    // As quatro primeiras e o zero de vf08 vêm na ordem do programa; o passo (0x10f) zera vf08 antes.
    mulbc(V(4), q, q, BX, XYZW);
    mulbc(V(5), q, q, BY, XYZW);
    mulbc(V(6), q, q, BZ, XYZW);
    mulbc(V(7), q, q, BW, XYZW);
    sub(V(8), vf00, vf00, XYZW);
    add(V(4), V(4), V(4), XYZW);
    add(V(5), V(5), V(5), XYZW);
    add(V(6), V(6), V(6), XYZW);
    add(V(7), V(7), V(7), XYZW);
    sub(V(9), vf00, vf00, XYZW);
    sub(V(10), vf00, vf00, XYZW);
    addbc(V(8), vf00, vf00, BW, X);
    addbc(V(8), vf00, V(5), BX, Y);
    addbc(V(8), vf00, V(6), BX, Z);
    addbc(V(9), vf00, V(5), BX, X);
    addbc(V(9), vf00, vf00, BW, Y);
    addbc(V(9), vf00, V(6), BY, Z);
    addbc(V(10), vf00, V(6), BX, X);
    addbc(V(10), vf00, V(5), BZ, Y);
    addbc(V(10), vf00, vf00, BW, Z);
    subbc(V(8), V(8), V(5), BY, X);
    addbc(V(8), V(8), V(6), BW, Y);
    subbc(V(8), V(8), V(5), BW, Z);
    subbc(V(9), V(9), V(6), BW, X);
    subbc(V(9), V(9), V(4), BX, Y);
    addbc(V(9), V(9), V(7), BX, Z);
    subbc(V(10), V(10), V(4), BX, Z);
    addbc(V(10), V(10), V(5), BW, X);
    subbc(V(10), V(10), V(7), BX, Y);
    subbc(V(8), V(8), V(6), BZ, X);
    subbc(V(9), V(9), V(6), BZ, Y);
    subbc(V(10), V(10), V(5), BY, Z);
}

}  // namespace

// [driving] 0x324e70 VU0MICRO_QuatToMatrix (instrução 0x000 do VU0_physics: o quatérnio de vf01 em matriz)
void VU0MICRO_QuatToMatrix(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    uint32_t q[4];
    std::memcpy(q, r.vf[1], 16);
    quat_to_matrix(r, q);
}

// ---------------------------------------------------------------------------------------------------- 0x022

// [driving] 0x324f80 VU0MICRO_MultiplyMatrix (instrução 0x022): vf12..vf15 = as linhas vf04..vf07 vezes a matriz
// [vf08; vf09; vf10; vf11] (a última linha é a translação, pelo w de cada linha).
void VU0MICRO_MultiplyMatrix(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    for (int i = 0; i < 4; ++i) {
        const uint32_t* row = r.vf[4 + i];
        uint32_t rowc[4];
        std::memcpy(rowc, row, 16);
        mulabc(r.acc, r.vf[8], rowc, BX, XYZW);
        maddabc(r.acc, r.vf[9], rowc, BY, XYZW);
        maddabc(r.acc, r.vf[10], rowc, BZ, XYZW);
        maddbc(r.vf[12 + i], r.acc, r.vf[11], rowc, BW, XYZW);
    }
}

// ---------------------------------------------------------------------------------------------------- 0x034

// [driving] 0x325010 VU0MICRO_ResolveLeverForces (instrução 0x034). Entrada: vf20..vf26 = o corpo (quatérnio,
// posição, velocidade, velocidade angular, momento com a massa no w, momento angular, força), vf17..vf19 = o tensor
// inverso no mundo, vi02 = alavancas, memória 0..11 = os braços r (no mundo, relativos ao centro), 12..23 = as normais
// do chão, 27 = (0, atrito, restituição / MICRO_THRESHOLD, MICRO_THRESHOLD). Para cada alavanca, em sequência (a
// velocidade de uma já leva o impulso das anteriores):
//   v_p = ω x r + v; vn = v_p . n; e = min(|vn|, MICRO_THRESHOLD) x (restituição / MICRO_THRESHOLD)
//   j = -(1 + e) vn / (1/m + n . ((I⁻¹ (r x n)) x r))
//   J = j n + t̂ (n . F/m) atrito, com t̂ a direção da velocidade tangencial
//   só se J.y >= 0 (o FMAND do sinal de y): P += J, L += r x J, v = P / m, ω = I⁻¹ L
// e vf31.x guarda o maior |vn| (o evento de choque de Resolve_CollectVU0data).
void VU0MICRO_ResolveLeverForces(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    auto V = [&](int n) { return r.vf[n]; };
    const uint32_t* vf00 = r.vf[0];
    auto load_q = [&](uint32_t* dst, uint32_t qw) { load(dst, vu.qword(qw), XYZW); };

    r.vi[1] = 0;                                    // 0x034 IADDIU vi01, vi00, 0
    load_q(V(2), uint32_t(r.vi[0] + 0));            // 0x035 LQ vf02, 0(vi00)
    load_q(V(7), uint32_t(r.vi[0] + 12));           // 0x036 LQ vf07, 12(vi00)
    load_q(V(11), 27);                              // 0x037
    load_q(V(12), 28);                              // 0x038
    sub(V(31), vf00, vf00, XYZW);                   // 0x039 SUB vf31 | LQ vf13, 29
    load_q(V(13), 29);
    for (;;) {
        subbc(V(4), vf00, vf00, BW, X);             // 0x03a vf04.x = -1
        opmula(r.acc, V(23), V(2));                 // 0x03b | DIV Q, vf00w, vf24w
        r.q = ps2::div(vf00[3], V(24)[3]);
        opmsub(V(9), r.acc, V(2), V(23));           // 0x03c vf09 = ω x r | IADDI vi02, vi02, -1
        r.vi[2] = uint16_t(r.vi[2] - 1);
        opmula(r.acc, V(2), V(7));                  // 0x03d | IADDIU vi01, vi01, 1
        r.vi[1] = uint16_t(r.vi[1] + 1);
        opmsub(V(1), r.acc, V(7), V(2));            // 0x03e vf01 = r x n
        add(V(9), V(9), V(22), XYZ);                // 0x040 v_p = ω x r + v
        uint32_t rxn[4];
        std::memcpy(rxn, V(1), 16);
        mulabc(r.acc, V(17), rxn, BX, XYZ);         // 0x042
        maddabc(r.acc, V(18), rxn, BY, XYZ);        // 0x043
        mul(V(3), V(9), V(7), XYZ);                 // 0x044 v_p * n
        maddbc(V(1), r.acc, V(19), rxn, BZ, XYZ);   // 0x045 vf01 = I⁻¹ (r x n)
        addbc(V(3), V(3), V(3), BY, X);             // 0x048
        opmula(r.acc, V(1), V(2));                  // 0x049
        opmsub(V(1), r.acc, V(2), V(1));            // 0x04a vf01 = (I⁻¹ (r x n)) x r
        addbc(V(3), V(3), V(3), BZ, X);             // 0x04c vn
        mul(V(1), V(1), V(7), XYZ);                 // 0x04e
        abs(V(8), V(3), X);                         // 0x050 |vn|
        addbc(V(1), V(1), V(1), BY, X);             // 0x052
        minibc(V(5), V(8), V(11), BW, X);           // 0x054 min(|vn|, MICRO_THRESHOLD)
        max(V(31), V(8), V(31), X);                 // 0x055 o maior |vn|
        addbc(V(1), V(1), V(1), BZ, X);             // 0x056 n . ((I⁻¹ (r x n)) x r)
        adds(V(16), vf00, r.q, X);                  // 0x057 vf16.x = 1 / m
        mulbc(V(5), V(5), V(11), BZ, X);            // 0x058 e
        adds(V(1), V(1), r.q, X);                   // 0x05b o denominador
        subbc(V(4), V(4), V(5), BX, X);             // 0x05c -(1 + e)
        mulbc(V(4), V(4), V(3), BX, X);             // 0x060 -(1 + e) vn
        mulbc(V(28), V(26), V(16), BX, XYZ);        // 0x063 F / m
        mul(V(5), V(7), V(9), XYZ);                 // 0x064 n * v_p | DIV Q, vf04x, vf01x
        r.q = ps2::div(V(4)[0], V(1)[0]);
        mul(V(4), V(7), V(28), XYZ);                // 0x067 n * F/m
        addbc(V(5), V(5), V(5), BY, X);             // 0x068
        addbc(V(4), V(4), V(4), BY, X);             // 0x06b
        addbc(V(5), V(5), V(5), BZ, X);             // 0x06c n . v_p
        addbc(V(4), V(4), V(4), BZ, X);             // 0x06f n . F/m
        mulbc(V(3), V(7), V(5), BX, XYZ);           // 0x070 n (n . v_p)
        sub(V(6), V(9), V(3), XYZ);                 // 0x074 a velocidade tangencial
        muls(V(7), V(7), r.q, XYZ);                 // 0x077 j n
        mul(V(8), V(6), V(6), XYZ);                 // 0x078
        addbc(V(8), V(8), V(8), BY, X);             // 0x07c
        addbc(V(8), V(8), V(8), BZ, X);             // 0x080 |vt|²
        mulbc(V(4), V(4), V(11), BY, X);            // 0x084 x atrito | RSQRT Q, vf00w, vf08x
        r.q = ps2::rsqrt(vf00[3], V(8)[0]);
        muls(V(6), V(6), r.q, XYZ);                 // 0x087 t̂ (depois do WAITQ)
        mulbc(V(6), V(6), V(4), BX, XYZ);           // 0x08b
        add(V(7), V(6), V(7), XYZ);                 // 0x08f J
        r.vi[3] = 0x40;                             // 0x092 IADDIU vi03, vi00, 0x40
        // 0x093 FMAND vi04, vi03: o indicador de sinal do y do ADD de 0x08f (4 pares antes).
        r.vi[4] = (V(7)[1] & ps2::kSignBit) ? 0x40 : 0;
        if (r.vi[4] == 0) {                         // 0x094 IBEQ vi04, vi00 -> 0x09c
            add(V(24), V(24), V(7), XYZ);           // 0x09c P += J
            opmula(r.acc, V(2), V(7));              // 0x09d
            opmsub(V(1), r.acc, V(7), V(2));        // 0x09e r x J
            mulbc(V(22), V(24), V(16), BX, XYZ);    // 0x0a0 v = P / m
            add(V(25), V(25), V(1), XYZ);           // 0x0a2 L += r x J
            load_q(V(2), r.vi[1]);                  // 0x0a5 LQ vf02, 0(vi01)
            uint32_t l[4];
            std::memcpy(l, V(25), 16);
            mulabc(r.acc, V(17), l, BX, XYZ);       // 0x0a6 | LQ vf07, 12(vi01)
            load_q(V(7), uint32_t(r.vi[1] + 12));
            maddabc(r.acc, V(18), l, BY, XYZ);      // 0x0a7
            maddbc(V(23), r.acc, V(19), l, BZ, XYZ);  // 0x0a8 ω = I⁻¹ L
            if (r.vi[2] == 0) return;               // 0x0a9 IBNE vi02, vi00 -> 0x03a; 0x0ab E
            continue;
        }
        load_q(V(2), r.vi[1]);                      // 0x096 LQ vf02, 0(vi01)
        load_q(V(7), uint32_t(r.vi[1] + 12));       // 0x098 (delay slot do IBNE de 0x097)
        if (r.vi[2] == 0) return;                   // 0x097 IBNE vi02, vi00 -> 0x03a; 0x09a E
    }
}

// ---------------------------------------------------------------------------------------------------- 0x0ad

// [driving] 0x3253d8 VU0MICRO_UpdatePositionAndOrientation (instrução 0x0ad). Entrada: vf04 = (dt, 0,5, 1000, 0),
// vf05 = (-M_LIMIT m, M_LIMIT m, -AM_LIMIT, AM_LIMIT), vf11..vf13 = o tensor inverso no mundo, vf20..vf27 = o corpo,
// memória 0..15 = as alavancas no corpo, 16..18 = a orientação transposta, 19 = a diagonal do tensor (I), 20..22 = a
// orientação. Em ordem:
//   P limitado a ±M_LIMIT m; P += F dt; L += τ dt; v = P / m; ω = I⁻¹(mundo) L limitado a ±AM_LIMIT;
//   L refeito de ω (R I Rᵀ ω, com a I do corpo); pos += v dt;
//   q' = 1000 q + 0,5 (ω dt ⊗ 1000 q) normalizado (RSQRT); a matriz de q'; I⁻¹(mundo) = Rᵀ diag(I⁻¹) R;
//   as 16 alavancas giradas para o mundo (memória 0..15).
void VU0MICRO_UpdatePositionAndOrientation(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    auto V = [&](int n) { return r.vf[n]; };
    const uint32_t* vf00 = r.vf[0];
    auto load_q = [&](uint32_t* dst, uint32_t qw) { load(dst, vu.qword(qw), XYZW); };

    maxbc(V(24), V(24), V(5), BX, XYZ);             // 0x0ad
    mulbc(V(27), V(27), V(4), BX, XYZ);             // 0x0ae τ dt
    const uint32_t mass = V(24)[3];
    mulbc(V(26), V(26), V(4), BX, XYZ);             // 0x0af F dt | DIV Q, vf00w, vf24w
    r.q = ps2::div(vf00[3], mass);
    minibc(V(24), V(24), V(5), BY, XYZ);            // 0x0b1
    add(V(24), V(24), V(26), XYZ);                  // 0x0b5 P += F dt
    add(V(25), V(25), V(27), XYZ);                  // 0x0b6 L += τ dt
    muls(V(22), V(24), r.q, XYZ);                   // 0x0b9 v = P / m
    {
        uint32_t l[4];
        std::memcpy(l, V(25), 16);
        mulabc(r.acc, V(11), l, BX, XYZ);           // 0x0ba
        maddabc(r.acc, V(12), l, BY, XYZ);          // 0x0bb
        maddbc(V(23), r.acc, V(13), l, BZ, XYZ);    // 0x0bc ω = I⁻¹ L
    }
    mulbc(V(18), V(22), V(4), BX, XYZ);             // 0x0bd v dt
    maxbc(V(23), V(23), V(5), BZ, XYZ);             // 0x0c0 | LQ vf01, 16(vi00)
    load_q(V(1), 16);
    load_q(V(2), 17);                               // 0x0c1
    load_q(V(3), 18);                               // 0x0c2
    load_q(V(8), 19);                               // 0x0c3
    minibc(V(23), V(23), V(5), BW, XYZ);            // 0x0c4
    {
        uint32_t w[4];
        std::memcpy(w, V(23), 16);
        mulabc(r.acc, V(1), w, BX, XYZ);            // 0x0c8
        maddabc(r.acc, V(2), w, BY, XYZ);           // 0x0c9
        maddbc(V(11), r.acc, V(3), w, BZ, XYZ);     // 0x0ca ω no corpo
    }
    load_q(V(1), 20);                               // 0x0cc
    load_q(V(2), 21);                               // 0x0cd
    mul(V(11), V(8), V(11), XYZ);                   // 0x0ce L no corpo = I ω | LQ vf03, 22(vi00)
    load_q(V(3), 22);
    {
        uint32_t l[4];
        std::memcpy(l, V(11), 16);
        mulabc(r.acc, V(1), l, BX, XYZ);            // 0x0d2
        maddabc(r.acc, V(2), l, BY, XYZ);           // 0x0d3
        maddbc(V(25), r.acc, V(3), l, BZ, XYZ);     // 0x0d4 L no mundo
    }
    add(V(21), V(21), V(18), XYZ);                  // 0x0d5 pos += v dt
    mulbc(V(18), V(23), V(4), BX, XYZ);             // 0x0d6 ω dt | MR32 vf31, vf00
    mr32(V(31), vf00, XYZW);
    mulbc(V(20), V(20), V(4), BZ, XYZW);            // 0x0d7 q x 1000
    opmula(r.acc, V(18), V(20));                    // 0x0db
    opmsub(V(16), r.acc, V(20), V(18));             // 0x0dc vf16 = ω dt x q
    mulbc(V(17), V(18), V(20), BW, XYZ);            // 0x0dd q.w ω dt
    add(V(16), V(16), V(17), XYZ);                  // 0x0e1
    mul(V(17), V(18), V(20), XYZ);                  // 0x0e2
    addbc(V(17), V(17), V(17), BY, X);              // 0x0e6
    addbc(V(17), V(17), V(17), BZ, X);              // 0x0ea ω dt . q
    subbc(V(16), V(31), V(17), BX, W);              // 0x0ee vf16.w = -(ω dt . q)
    mulbc(V(16), V(16), V(4), BY, XYZW);            // 0x0f2 x 0,5
    add(V(16), V(16), V(20), XYZW);                 // 0x0f6 q' = q + dq
    mul(V(17), V(16), V(16), XYZW);                 // 0x0fa
    addbc(V(17), V(17), V(17), BY, X);              // 0x0fe
    addbc(V(17), V(17), V(17), BZ, X);              // 0x102
    addbc(V(17), V(17), V(17), BW, X);              // 0x106 |q'|²
    r.q = ps2::rsqrt(vf00[3], V(17)[0]);            // 0x10a RSQRT Q, vf00w, vf17x; 0x10b WAITQ
    muls(V(20), V(16), r.q, XYZW);                  // 0x10c q normalizado
    {
        uint32_t q[4];
        std::memcpy(q, V(20), 16);
        quat_to_matrix(r, q);                       // 0x10f-0x12e: vf08..vf10 (e vf04..vf07 temporários)
    }
    sub(V(5), vf00, vf00, XYZW);                    // 0x130 | MOVE vf01, vf08
    move(V(1), V(8), XYZW);
    sub(V(6), vf00, vf00, XYZW);                    // 0x131 | MOVE vf02, vf09
    move(V(2), V(9), XYZW);
    sub(V(7), vf00, vf00, XYZW);                    // 0x132 | MOVE vf03, vf10
    move(V(3), V(10), XYZW);
    addbc(V(1), vf00, V(9), BX, Y);                 // 0x134 a transposta em vf01..vf03
    addbc(V(1), vf00, V(10), BX, Z);                // 0x135
    addbc(V(5), V(5), V(21), BW, X);                // 0x136 diag(I⁻¹) em vf05..vf07
    addbc(V(2), vf00, V(8), BY, X);                 // 0x137
    addbc(V(2), vf00, V(10), BY, Z);                // 0x138
    addbc(V(6), V(6), V(22), BW, Y);                // 0x139
    addbc(V(3), vf00, V(8), BZ, X);                 // 0x13a
    addbc(V(3), vf00, V(9), BZ, Y);                 // 0x13b
    addbc(V(7), V(7), V(23), BW, Z);                // 0x13c
    for (int i = 0; i < 3; ++i) {                   // 0x13d-0x145: vf12..vf14 = diag(I⁻¹) R
        uint32_t d[4];
        std::memcpy(d, V(5 + i), 16);
        mulabc(r.acc, V(8), d, BX, XYZ);
        maddabc(r.acc, V(9), d, BY, XYZ);
        maddbc(V(12 + i), r.acc, V(10), d, BZ, XYZ);
    }
    mul(V(4), vf00, vf00, XYZW);                    // 0x146 | IADDIU vi14, vi00, 0x10
    r.vi[14] = 0x10;
    for (int i = 0; i < 3; ++i) {                   // 0x147-0x14f: vf15..vf17 = Rᵀ diag(I⁻¹) R
        uint32_t t[4];
        std::memcpy(t, V(1 + i), 16);
        mulabc(r.acc, V(12), t, BX, XYZ);
        if (i == 0) r.vi[1] = 0;                    // 0x147 | IADDIU vi01, vi00, 0
        maddabc(r.acc, V(13), t, BY, XYZ);
        if (i == 0) r.vi[2] = 0;                    // 0x148 | IADDIU vi02, vi00, 0
        maddbc(V(15 + i), r.acc, V(14), t, BZ, XYZ);
        if (i == 1) {                               // 0x14c | LQI vf04, (vi01++)
            load_q(V(4), r.vi[1]);
            r.vi[1] = uint16_t(r.vi[1] + 1);
        }
    }
    // 0x150-0x15e: as 16 alavancas para o mundo, duas por volta (vf11 e vf12 em revezamento), cada uma gravada um
    // par depois de calculada; o w gravado é o que vf11/vf12 tinham (o programa só escreve xyz).
    auto sqi = [&](const uint32_t* s) {
        store(vu.qword(r.vi[2]), s, XYZW);
        r.vi[2] = uint16_t(r.vi[2] + 1);
    };
    auto lqi = [&](uint32_t* d) {
        load_q(d, r.vi[1]);
        r.vi[1] = uint16_t(r.vi[1] + 1);
    };
    {
        uint32_t c[4];
        std::memcpy(c, V(4), 16);
        mulabc(r.acc, V(8), c, BX, XYZ);            // 0x150 | LQI vf05
        lqi(V(5));
        maddabc(r.acc, V(9), c, BY, XYZ);           // 0x151 | IADDI vi14, vi14, -2
        r.vi[14] = uint16_t(r.vi[14] - 2);
        maddbc(V(11), r.acc, V(10), c, BZ, XYZ);    // 0x152 | B 0x158
    }
    for (;;) {
        {
            uint32_t c[4];
            std::memcpy(c, V(5), 16);
            mulabc(r.acc, V(8), c, BX, XYZ);        // 0x158 | LQI vf04
            lqi(V(4));
            maddabc(r.acc, V(9), c, BY, XYZ);       // 0x159
            uint32_t old11[4];
            std::memcpy(old11, V(11), 16);
            maddbc(V(12), r.acc, V(10), c, BZ, XYZ);  // 0x15a | SQI vf11
            sqi(old11);
        }
        if (r.vi[14] == 0) break;                   // 0x15b IBNE vi14, vi00 -> 0x154
        {
            uint32_t c[4];
            std::memcpy(c, V(4), 16);
            mulabc(r.acc, V(8), c, BX, XYZ);        // 0x154 | LQI vf05
            lqi(V(5));
            maddabc(r.acc, V(9), c, BY, XYZ);       // 0x155 | IADDI vi14, vi14, -2
            r.vi[14] = uint16_t(r.vi[14] - 2);
            uint32_t old12[4];
            std::memcpy(old12, V(12), 16);
            maddbc(V(11), r.acc, V(10), c, BZ, XYZ);  // 0x156 | SQI vf12
            sqi(old12);
        }
    }
    sqi(V(12));                                     // 0x15e (delay slot do E): SQI vf12
}

// ---------------------------------------------------------------------------------------------------- o EE

GroundImpactStats& ground_impacts() { return g_impacts; }

namespace {
// ctc2 vi3 + vsqi: copia n quadwords para a memória da VU0 a partir do quadword at.
void upload(uint32_t at, const void* src, uint32_t n) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < n; ++i) store(vu0().qword(at + i), reinterpret_cast<const uint32_t*>(s + 16 * i), XYZW);
    vu0().regs().vi[3] = uint16_t(at + n);
}
void lqc2(int reg, const void* src) { load(vu0().regs().vf[reg], src, XYZW); }
void sqc2(void* dst, int reg) { store(dst, vu0().regs().vf[reg], XYZW); }
}  // namespace

// [driving] 0x1f89e0 Timestep_LoadAndStartVU0
void Timestep_LoadAndStartVU0(RigidBody* rb, RigidBodyInfo* info, float scale) {
    RigidTuning& t = rigid_tuning();
    TimestepConstants& ts = timestep_constants();
    if (t.timestep_latched == 0) {  // a constante de vf04 só é montada na primeira chamada (com o primeiro dt)
        t.timestep_latched = 1;
        ts.dt_scaled = ee::mul(simulation().dt, scale);
        ts.half = 0.5f;
        ts.quat_scale = 1000.0f;
        ts.zero = 0.0f;
    }
    // vf05: os limites (-M_LIMIT m, M_LIMIT m, -AM_LIMIT, AM_LIMIT).
    const float limits[4] = {ee::mul(ee::neg(t.m_limit), rb->mass), ee::mul(t.m_limit, rb->mass), ee::neg(t.am_limit),
                             t.am_limit};
    Timestep_CollectVU0data();
    upload(0x00, info->lever_local, 16);
    // A diagonal do tensor no corpo (I = 1 / I⁻¹), w = 0 (o memset da pilha).
    const float inertia[4] = {ee::div(1.0f, rb->inv_inertia_x), ee::div(1.0f, rb->inv_inertia_y),
                              ee::div(1.0f, rb->inv_inertia_z), 0.0f};
    float transposed[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) transposed[i][j] = info->orient[j][i];
    upload(0x10, transposed, 3);
    upload(0x13, inertia, 1);
    upload(0x14, info->orient, 3);
    RigidBodyScratch& s = rigid_scratch();
    s.vu0_busy = 1;
    s.vu0_info = info;
    s.vu0_body = rb;
    const float c[4] = {ts.dt_scaled, ts.half, ts.quat_scale, ts.zero};
    lqc2(4, c);
    lqc2(5, limits);
    lqc2(11, info->inv_world_tensor[0]);
    lqc2(12, info->inv_world_tensor[1]);
    lqc2(13, info->inv_world_tensor[2]);
    const uint8_t* body = reinterpret_cast<const uint8_t*>(rb);
    for (int i = 0; i < 8; ++i) lqc2(20 + i, body + 16 * i);
    vu0_callms(0xad);
}

// [driving] 0x1f8cf0 RigidBody::Timestep_CollectVU0data
void Timestep_CollectVU0data() {
    RigidBodyScratch& s = rigid_scratch();
    RigidBody* rb = s.vu0_body;
    if (!rb) return;
    RigidBodyInfo* info = s.vu0_info;
    sqc2(info->orient[0], 8);  // qmfc2.I: espera a VU0 terminar
    sqc2(info->orient[1], 9);
    sqc2(info->orient[2], 10);
    sqc2(info->inv_world_tensor[0], 15);
    sqc2(info->inv_world_tensor[1], 16);
    sqc2(info->inv_world_tensor[2], 17);
    uint8_t* body = reinterpret_cast<uint8_t*>(rb);
    for (int i = 0; i < 6; ++i) sqc2(body + 16 * i, 20 + i);
    for (uint32_t i = 0; i < 16; ++i) store(info->lever_world[i], vu0().qword(i), XYZW);
    vu0().regs().vi[3] = 17;  // o vlqi lê um adiante
    ControlSleep(s.vu0_body);

    const RigidBodyInfo* oinfo = s.vu0_body->info;  // a orientação vem do info do corpo (o mesmo)
    s.vu0_info->corner_buffer = int8_t(s.vu0_info->corner_buffer ^ 1);
    float(*dst)[4] = s.vu0_info->corners[int(s.vu0_info->corner_buffer)];
    const float hz = s.vu0_info->half_size[2];
    const float hy = s.vu0_info->half_size[1];
    float hx = s.vu0_info->half_size[0];
    if (s.vu0_body->type < 4) hx = ee::add(hx, 0.18f);
    const float nx = ee::neg(hx), ny = ee::neg(hy), nz = ee::neg(hz);
    const float corners[8][4] = {{nx, ny, nz, 1.0f}, {hx, ny, nz, 1.0f}, {hx, ny, hz, 1.0f}, {nx, ny, hz, 1.0f},
                                 {nx, hy, hz, 1.0f}, {hx, hy, hz, 1.0f}, {hx, hy, nz, 1.0f}, {nx, hy, nz, 1.0f}};
    Reg r0, r1, r2, c, acc, out;
    for (int k = 0; k < 8; ++k) {
        load(c, corners[k]);
        load(r0, oinfo->orient[0]);
        load(r1, oinfo->orient[1]);
        load(r2, oinfo->orient[2]);
        load(out, dst[k]);
        mulabc(acc, r0, c, BX, XYZ);
        maddabc(acc, r1, c, BY, XYZ);
        maddbc(out, acc, r2, c, BZ, XYZ);
        store(dst[k], out);
    }
    UpdateZonalInfo(s.vu0_body);
    s.vu0_busy = 0;
    s.vu0_body = nullptr;
}

// [driving] 0x1f8fd0 RigidBody::UpdatePositionAndOrientation
void UpdatePositionAndOrientation(RigidBody* rb, float scale) { Timestep_LoadAndStartVU0(rb, rb->info, scale); }

// [driving] 0x1f91e8 RigidBody::ResolveLeverForces
void ResolveLeverForces(RigidBody* rb, const LeverParams* params, const float (*levers)[4], const float (*normals)[4],
                        const float (*tensor_rows)[4]) {
    RigidTuning& t = rigid_tuning();
    RigidBodyScratch& s = rigid_scratch();
    float restitution_scale = t.ground_restitution_scale;
    if (rb->type < 4) {
        PhysicsObject* owner = GetOwner(rb);
        const PhysicsAttributes* attr = owner_attributes(owner);
        if (attr && attr->alt_layout != 0) restitution_scale = t.vehicle_restitution_scale;
    }
    float(*block)[4] = s.lever_block;
    std::memset(&block[0][0], 0, 4);  // sw zero, 0x360
    block[0][1] = params->friction;
    block[0][2] = restitution_scale;
    block[0][3] = t.micro_threshold;
    block[1][0] = params->restitution;
    block[1][2] = params->restitution;
    block[1][1] = ee::mul(params->restitution, 4.0f);
    block[2][0] = ee::neg(params->restitution);
    block[2][2] = ee::neg(params->restitution);
    block[2][1] = ee::mul(ee::neg(params->restitution), 4.0f);  // +0x37c e +0x38c ficam como estavam
    Resolve_CollectVU0data();
    upload(0x1b, block, 3);
    upload(0x00, levers, uint32_t(params->count));
    upload(0x0c, normals, uint32_t(params->count));
    const uint8_t* body = reinterpret_cast<const uint8_t*>(rb);
    for (int i = 0; i < 7; ++i) lqc2(20 + i, body + 16 * i);
    lqc2(17, tensor_rows[0]);
    lqc2(18, tensor_rows[1]);
    lqc2(19, tensor_rows[2]);
    vu0().regs().vi[2] = uint16_t(params->count);  // ctc2.I: espera a VU0 terminar o anterior
    vu0_callms(0x34);
    s.vu0_busy = 1;
    s.vu0_body = rb;
}

// [driving] 0x1f8ff0 RigidBody::Resolve_CollectVU0data
void Resolve_CollectVU0data() {
    RigidBodyScratch& s = rigid_scratch();
    RigidBody* rb = s.vu0_body;
    if (!rb) return;
    uint8_t* body = reinterpret_cast<uint8_t*>(rb);
    for (int i = 0; i < 4; ++i) sqc2(body + 0x20 + 16 * i, 22 + i);  // velocidade, angular, P, L
    const float speed = ps2::ffloat(vu0().regs().vf[31][0]);
    if (ee::lt(0.3f, speed)) {
        // O choque contra o chão: um ECollision (som e dano) com a força min(|v| (m / 8 / 4) / v, 1). O evento não
        // foi portado; o porte guarda os números.
        float strength = ee::mul(ee::div(s.vu0_body->mass, base_friction_mass()), 0.25f);
        strength = ee::mul(ee::abs(speed), strength);
        strength = ee::div(strength, speed);
        strength = ee::min(strength, 1.0f);
        g_impacts.events++;
        g_impacts.last_speed = speed;
        g_impacts.last_strength = strength;
        g_impacts.last_slot = s.vu0_body->slot;
        physics_pending("ECollision (choque contra o chão)");
    }
    s.vu0_busy = 0;
    s.vu0_body = nullptr;
}

}  // namespace drv
