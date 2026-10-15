// A caixa orientada (driving/physics/obb.h), da desmontagem do DRIVING.ELF (out/disasm_drv_cww2 do checkout
// principal). A aritmética: o Reset pela FPU do EE (mul.s), o teste pela VU0 em macro (vsub/vmul/vaddy/vaddz/vadd).

#include "driving/physics/obb.h"

#include <cstring>

#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {
// vmul.xyz + vaddy.x + vaddz.x: (a.x b.x + a.y b.y) + a.z b.z.
float dot3(const uint32_t* a, const uint32_t* b) {
    Reg t{};
    mul(t, a, b, XYZ);
    addbc(t, t, t, BY, X);
    addbc(t, t, t, BZ, X);
    return f(t, 0);
}
}  // namespace

// [driving] 0x001f5de8 OBB::Reset (e 0x001f5cf0 OBB::OBB, o mesmo código)
void OBB_Reset(OBB& o, const float m[4][4], const float centre[4], const float half[4]) {
    std::memcpy(o.centre, centre, 16);
    for (int i = 0; i < 3; ++i) {
        std::memcpy(o.axis[i], m[i], 16);
        std::memcpy(o.extent[i], m[i], 16);
    }
    std::memcpy(o.half, half, 16);
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) o.extent[i][c] = ee::mul(half[i], o.extent[i][c]);
    std::memset(o.contact, 0, 16);
    std::memset(o.normal, 0, 16);
    o.penetration = ps2::ffloat(0xc7c35000u);  // -100000
}

// [driving] 0x001f5eb0 OBB::CheckOBBOverlap: só o teste dos eixos separadores (nada é guardado). Os eixos de cada caixa
// na ordem x, z, y; a distância entre os centros projetada, menos a meia-medida da caixa do eixo e as três meias-medidas
// da outra projetadas: positiva, as caixas se separam.
bool OBB_CheckOBBOverlap(const OBB& self, const OBB& other) {
    for (int pass = 0; pass < 2; ++pass) {
        const OBB& t0 = pass == 1 ? other : self;  // a caixa cujos eixos são testados
        const OBB& t1 = pass == 1 ? self : other;
        for (int k = 0; k < 3; ++k) {
            const int a = k == 1 ? 2 : (k == 2 ? 1 : 0);
            Reg axis, c0, c1, d{};
            load(axis, t0.axis[a]);
            load(c0, t0.centre);
            load(c1, t1.centre);
            sub(d, c0, c1, XYZ);
            float s = ee::sub(ee::abs(dot3(d, axis)), t0.half[a]);
            for (int j = 0; j < 3; ++j) {
                Reg e;
                load(e, t1.extent[j]);
                s = ee::sub(s, ee::abs(dot3(axis, e)));
            }
            if (ee::lt(0.0f, s)) return false;
        }
    }
    return true;
}

// [driving] 0x001f5ff8 OBB::CheckOBBOverlapAndFindIntersection
bool OBB_CheckOBBOverlapAndFindIntersection(OBB& self, const OBB& other) {
    for (int pass = 0; pass < 2; ++pass) {
        // t1: a caixa cujos eixos são testados; t0: a outra.
        const OBB& t1 = pass == 1 ? other : self;
        const OBB& t0 = pass == 1 ? self : other;
        for (int k = 0; k < 3; ++k) {
            const int a = k == 1 ? 2 : (k == 2 ? 1 : 0);  // a ordem do original: x, z, y
            Reg axis, corner, c1, c0, d{};
            load(axis, t1.axis[a]);
            load(corner, t0.centre);
            load(c1, t1.centre);
            load(c0, t0.centre);
            sub(d, c1, c0, XYZ);
            float s = dot3(d, axis);
            if (ee::lt(s, 0.0f)) {
                s = ee::neg(s);
            } else {
                axis[0] = ps2::neg(axis[0]);
                axis[1] = ps2::neg(axis[1]);
                axis[2] = ps2::neg(axis[2]);
            }
            s = ee::sub(s, t1.half[a]);
            for (int j = 0; j < 3; ++j) {
                Reg e;
                load(e, t0.extent[j]);
                const float p = dot3(axis, e);
                if (ee::lt(0.0f, p)) sub(corner, corner, e, XYZ);
                else if (ee::lt(p, 0.0f)) add(corner, corner, e, XYZ);
                s = ee::sub(s, ee::abs(p));
            }
            if (ee::lt(0.0f, s)) return false;
            if (ee::lt(self.penetration, s)) {
                self.penetration = s;
                std::memcpy(self.contact, corner, 16);
                self.second_pass = uint8_t(pass);
                for (int c = 0; c < 3; ++c) self.normal[c] = pass ? ee::neg(f(axis, c)) : f(axis, c);
            }
        }
    }
    return true;
}

}  // namespace drv
