// As cortinas de visibilidade na VU0 (driving/vu0_culling.h): o microprograma VU0_culling portado à mão.
//
// Cada porte segue o microcódigo instrução por instrução (os números são os pares do programa, como o
// tools/vudis.py DRIVING.ELF --prog=2 os mostra). O hardware para o pipeline quando uma instrução lê um resultado que
// ainda não saiu, então a ordem das contas é a do programa; só Q e os indicadores têm latência visível: cada RSQRT fica
// exatamente 13 pares antes do MULq que o lê (0x016/0x023, 0x04f/0x05c, 0x05c/0x069, 0x069/0x076) e cada FMAND 4 pares
// depois da instrução cujo sinal ele testa (0x034/0x038, 0x09b/0x09f, 0x0b0/0x0b4).

#include "driving/vu0_culling.h"

#include <cstring>

#include "driving/physics/rigid_body.h"
#include "driving/physics/vu0_physics.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;

namespace {

std::vector<uint8_t> g_program;

// Os bits de sinal do MAC (S: bit 7 x, 6 y, 5 z, 4 w) de um resultado já calculado nas pistas do campo `dest`; as
// pistas fora do campo dão zero, como na VU.
uint16_t sign_bits(uint32_t dest, const bool sign[4]) {
    uint16_t s = 0;
    for (int l = 0; l < 4; ++l)
        if (on(dest, l) && sign[l]) s = uint16_t(s | (0x80u >> l));
    return s;
}

}  // namespace

void set_vu0_culling_program(std::vector<uint8_t> code) { g_program = std::move(code); }
bool has_vu0_culling_program() { return !g_program.empty(); }

// [driving] 0x0022f3e0 UploadCullingMicrocode
void UploadCullingMicrocode(bool) {
    if (vu0_mode() == Vu0Mode::Interpreted && !g_program.empty())
        vu0().load_micro(0, g_program.data(), uint32_t(g_program.size()));
}

void vu0_culling_callms(uint32_t instr) {
    if (vu0_mode() == Vu0Mode::Interpreted) {
        vu0().run(instr);
        return;
    }
    if (!vu0_culling_native(vu0(), instr)) physics_pending("vcallms (cortinas: programa sem porte)");
}

bool vu0_culling_native(ps2::VuMachine& vu, uint32_t instr) {
    switch (instr) {
    case 0x000: VU0MICRO_AddCurtain(vu); return true;
    case 0x092: VU0MICRO_CheckAgainstVisCurtains(vu); return true;
    case 0x0a8: VU0MICRO_CheckPositionAgainstVisCurtains(vu); return true;
    default: return false;
    }
}

// ---------------------------------------------------------------------------------------------------- 0x000

// [driving] 0x325980 VU0_culling + 0x000 (o AddCurtain): entrada vf01 = o começo da cortina, vf02 = o fim, vf03 = o
// olho, vi01 = o índice da cortina; saída vi02 (1: gravada em 4 x vi01) e os temporários vf04..vf21, vf27..vf31.
void VU0MICRO_AddCurtain(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    auto V = [&](int n) { return r.vf[n]; };
    const uint32_t* vf00 = r.vf[0];
    sub(V(4), V(2), V(1), Y);                   // 0x000 SUB.y vf04 = fim - começo (a altura) | IADDIU vi14, 0x80
    r.vi[14] = 0x80;
    sub(V(4), vf00, vf00, XZW);                 // 0x001 | IADDIU vi02, vi00, 1
    r.vi[2] = 1;
    sub(V(5), V(1), V(2), X);                   // 0x002 SUB.x vf05 = começo - fim | IADD vi01 x 2
    r.vi[1] = uint16_t(r.vi[1] + r.vi[1]);
    sub(V(5), V(1), V(2), Z);                   // 0x003 | IADD vi01 x 2
    r.vi[1] = uint16_t(r.vi[1] + r.vi[1]);
    sub(V(5), vf00, vf00, YW);                  // 0x004
    sub(V(6), V(1), V(2), Y);                   // 0x005 vf06 = (0, começo - fim, 0, 0)
    sub(V(6), vf00, vf00, XZW);                 // 0x006
    addbc(V(31), vf00, vf00, BW, XYZ);          // 0x007 vf31.xyz = 1
    opmula(r.acc, V(4), V(5));                  // 0x008
    opmsub(V(7), r.acc, V(5), V(4));            // 0x009 vf07 = vf04 x vf05 (a normal da parede)
    mulbc(V(31), vf00, vf00, BW, W);            // 0x00a vf31.w = 1
    mul(V(8), V(7), V(7), XYZ);                 // 0x00d
    mul(r.acc, V(7), V(7), X);                  // 0x00e MULA.x
    maddabc(r.acc, V(31), V(8), BY, X);         // 0x011 MADDAy.x
    maddbc(V(8), r.acc, V(31), V(8), BZ, X);    // 0x012 MADDz.x vf08.x = |n|^2
    r.q = ps2::rsqrt(vf00[3], V(8)[0]);         // 0x016 RSQRT Q, vf00w, vf08x
    muls(V(7), V(7), r.q, XYZ);                 // 0x023 MULq.xyz
    mul(V(9), V(1), V(7), XYZ);                 // 0x027
    mul(V(10), V(3), V(7), XYZ);                // 0x028
    sub(V(9), vf00, vf00, W);                   // 0x029
    addbc(V(9), V(9), V(9), BY, X);             // 0x02b
    addbc(V(10), V(10), V(10), BY, X);          // 0x02c
    addbc(V(9), V(9), V(9), BZ, X);             // 0x02f vf09.x = começo . n
    addbc(V(10), V(10), V(10), BZ, X);          // 0x030 vf10.x = olho . n
    sub(V(11), V(3), V(1), XYZ);                // 0x031 olho - começo
    sub(V(12), V(3), V(2), XYZ);                // 0x032 olho - fim
    addbc(V(7), V(9), V(9), BX, W);             // 0x033 vf07.w = 0 + começo . n
    {
        ps2::FloatFlags f;                      // 0x034 SUBx.x vf10.x = olho . n - começo . n
        V(10)[0] = ps2::sub(V(10)[0], V(9)[0], &f);
        const bool sign[4] = {f.sign, false, false, false};
        r.vi[14] = uint16_t(r.vi[14] & sign_bits(X, sign));  // 0x038 FMAND vi14, vi14
    }
    if (r.vi[14] != 0) {                        // 0x039 IBEQ vi14, vi00, 0x03d
        r.vi[2] = 0;                            // 0x03b [E] IADDI vi02, vi00, 0: o olho está atrás da parede
        return;
    }
    opmula(r.acc, V(6), V(11));                 // 0x03d
    opmsub(V(13), r.acc, V(11), V(6));          // 0x03e vf13 = vf06 x (olho - começo)
    opmula(r.acc, V(4), V(12));                 // 0x03f
    opmsub(V(14), r.acc, V(12), V(4));          // 0x040 vf14 = vf04 x (olho - fim)
    opmula(r.acc, V(5), V(12));                 // 0x041
    opmsub(V(15), r.acc, V(12), V(5));          // 0x042 vf15 = vf05 x (olho - fim)
    mul(V(16), V(13), V(13), XYZ);              // 0x043
    mul(V(17), V(14), V(14), XYZ);              // 0x044
    mul(V(18), V(15), V(15), XYZ);              // 0x046
    addbc(V(16), V(16), V(16), BY, X);          // 0x047
    addbc(V(17), V(17), V(17), BY, X);          // 0x048
    addbc(V(18), V(18), V(18), BY, X);          // 0x04a
    addbc(V(16), V(16), V(16), BZ, X);          // 0x04b
    addbc(V(17), V(17), V(17), BZ, X);          // 0x04c
    addbc(V(18), V(18), V(18), BZ, X);          // 0x04e
    r.q = ps2::rsqrt(vf00[3], V(16)[0]);        // 0x04f RSQRT Q, vf00w, vf16x
    muls(V(13), V(13), r.q, XYZ);               // 0x05c MULq.xyz vf13 | RSQRT Q, vf00w, vf17x
    r.q = ps2::rsqrt(vf00[3], V(17)[0]);
    sub(V(19), vf00, vf00, W);                  // 0x05d
    mul(V(19), V(1), V(13), XYZ);               // 0x060
    addbc(V(19), V(19), V(19), BY, X);          // 0x064
    addbc(V(19), V(19), V(19), BZ, X);          // 0x068 vf19.x = começo . n13
    muls(V(14), V(14), r.q, XYZ);               // 0x069 MULq.xyz vf14 | RSQRT Q, vf00w, vf18x
    r.q = ps2::rsqrt(vf00[3], V(18)[0]);
    addbc(V(13), V(19), V(19), BX, W);          // 0x06c vf13.w = 0 + começo . n13
    mul(V(20), V(3), V(14), XYZ);               // 0x06d
    addbc(V(20), V(20), V(20), BY, X);          // 0x071
    addbc(V(20), V(20), V(20), BZ, X);          // 0x075 vf20.x = olho . n14
    muls(V(15), V(15), r.q, XYZ);               // 0x076 MULq.xyz vf15
    addbc(V(14), V(19), V(20), BX, W);          // 0x079 vf14.w = 0 + olho . n14
    mul(V(21), V(3), V(15), XYZ);               // 0x07a
    addbc(V(27), vf00, V(7), BX, X);            // 0x07b
    addbc(V(27), vf00, V(13), BX, Y);           // 0x07c
    addbc(V(27), vf00, V(14), BX, Z);           // 0x07d
    addbc(V(21), V(21), V(21), BY, X);          // 0x07e
    addbc(V(28), vf00, V(7), BY, X);            // 0x07f
    addbc(V(28), vf00, V(13), BY, Y);           // 0x080
    addbc(V(28), vf00, V(14), BY, Z);           // 0x081
    addbc(V(21), V(21), V(21), BZ, X);          // 0x082 vf21.x = olho . n15
    addbc(V(29), vf00, V(7), BZ, X);            // 0x083
    addbc(V(29), vf00, V(13), BZ, Y);           // 0x084
    addbc(V(29), vf00, V(14), BZ, Z);           // 0x085
    addbc(V(15), V(19), V(21), BX, W);          // 0x086 vf15.w = 0 + olho . n15
    addbc(V(30), vf00, V(7), BW, X);            // 0x087
    addbc(V(30), vf00, V(13), BW, Y);           // 0x088
    addbc(V(30), vf00, V(14), BW, Z);           // 0x089
    addbc(V(27), V(19), V(15), BX, W);          // 0x08a
    addbc(V(28), V(19), V(15), BY, W);          // 0x08b
    addbc(V(29), V(19), V(15), BZ, W);          // 0x08c
    addbc(V(30), V(19), V(15), BW, W);          // 0x08d
    const uint32_t at = r.vi[1];
    store(vu.qword(at + 0), V(27), XYZW);       // 0x08e SQ vf27, 0(vi01)
    store(vu.qword(at + 1), V(28), XYZW);       // 0x08f
    store(vu.qword(at + 2), V(29), XYZW);       // 0x090 [E]
    store(vu.qword(at + 3), V(30), XYZW);       // 0x091 (no atraso do E)
}

// ---------------------------------------------------------------------------------------------------- 0x092 / 0x0a8

namespace {

// O laço dos dois testes: as cortinas de 0 a vi01 - 1, quatro quadwords cada (com a leitura antecipada da seguinte, que
// fica nos registradores como no microcódigo). `margin`: com o vf04 = (raio, raio, raio, margem) do 0x092 (MADDw), ou
// sem (MSUBw do 0x0a8).
void curtain_loop(ps2::VuMachine& vu, bool margin) {
    ps2::VuRegs& r = vu.regs();
    auto V = [&](int n) { return r.vf[n]; };
    const uint32_t* vf00 = r.vf[0];
    r.vi[3] = 0;                                // IADDIU vi03, vi00, 0
    for (int k = 0; k < 4; ++k) load(V(5 + k), vu.qword(r.vi[3]++), XYZW);  // LQI vf05..vf08
    for (;;) {
        uint32_t rows[4][4];
        for (int k = 0; k < 4; ++k) std::memcpy(rows[k], V(5 + k), 16);
        mulabc(r.acc, rows[0], V(1), BX, XYZW);         // MULAx | IADDI vi01, vi01, -1
        r.vi[1] = uint16_t(r.vi[1] - 1);
        maddabc(r.acc, rows[1], V(1), BY, XYZW);        // MADDAy | LQI vf05
        load(V(5), vu.qword(r.vi[3]++), XYZW);
        maddabc(r.acc, rows[2], V(1), BZ, XYZW);        // MADDAz | LQI vf06
        load(V(6), vu.qword(r.vi[3]++), XYZW);
        bool sign[4];
        if (margin) {
            for (int l = 0; l < 4; ++l)                 // MSUBAw ACC -= linha 3 x 1 | LQI vf07
                r.acc[l] = ps2::msub(r.acc[l], rows[3][l], vf00[3]);
            load(V(7), vu.qword(r.vi[3]++), XYZW);
            for (int l = 0; l < 4; ++l) {               // MADDw vf03 = ACC + vf04 x 1 | LQI vf08
                ps2::FloatFlags f;
                V(3)[l] = ps2::madd(r.acc[l], V(4)[l], vf00[3], &f);
                sign[l] = f.sign;
            }
            load(V(8), vu.qword(r.vi[3]++), XYZW);
        } else {
            for (int l = 0; l < 4; ++l) {               // MSUBw vf03 = ACC - linha 3 x 1 | LQI vf07
                ps2::FloatFlags f;
                V(3)[l] = ps2::msub(r.acc[l], rows[3][l], vf00[3], &f);
                sign[l] = f.sign;
            }
            load(V(7), vu.qword(r.vi[3]++), XYZW);
            load(V(8), vu.qword(r.vi[3]++), XYZW);      // LQI vf08
        }
        r.vi[14] = 0xf0;                                // IADDIU vi14, vi00, 0xf0
        r.vi[13] = uint16_t(r.vi[14] & sign_bits(XYZW, sign));  // FMAND vi13, vi14
        if (r.vi[13] == r.vi[14]) {                     // IBEQ vi13, vi14: as quatro distâncias negativas
            r.vi[2] = 0;                                // [E] IADDIU vi02, vi00, 0
            return;
        }
        if (r.vi[1] == 0) break;                        // IBNE vi01, vi00
    }
    r.vi[2] = 1;                                        // [E] IADDIU vi02, vi00, 1
}

}  // namespace

// [driving] 0x325e10 CheckAgainstVisCurtains (VU0_culling + 0x490, instrução 0x092): entrada vf01 = a esfera (o raio no
// w), vf02.x = a margem do quarto plano, vi01 = as cortinas; saída vi02.
void VU0MICRO_CheckAgainstVisCurtains(ps2::VuMachine& vu) {
    ps2::VuRegs& r = vu.regs();
    const uint32_t* vf00 = r.vf[0];
    addbc(r.vf[4], vf00, r.vf[1], BW, XYZ);     // 0x092 ADDw.xyz vf04 = o raio
    mulbc(r.vf[4], vf00, r.vf[2], BX, W);       // 0x093 MULx.w vf04.w = 1 x a margem
    curtain_loop(vu, true);
}

// [driving] 0x325ec0 CheckPositionAgainstVisCurtains (VU0_culling + 0x540, instrução 0x0a8): o ponto vf01 contra as
// vi01 cortinas; saída vi02.
void VU0MICRO_CheckPositionAgainstVisCurtains(ps2::VuMachine& vu) { curtain_loop(vu, false); }

}  // namespace drv
