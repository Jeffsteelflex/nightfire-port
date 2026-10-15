// A sombra dos carros (driving/render/shadow.h), portada do MIPS do DRIVING.ELF (as desmontagens em out/agent_drv_car/
// do checkout principal). As contas seguem a FPU do EE e a VU0 em modo macro (vadd/vsub/vmulx) na ordem das instruções.
//
// Divergência (port): nenhuma nas contas. O estado de render que o PS2 deixa (sem DisableAlphaWrites desfeito quando o
// quadrado não é desenhado, porque essas saídas pulam o EnableAlphaWrites) não existe no PC.

#include "driving/render/shadow.h"

#include <cstdlib>
#include <cstring>

#include "driving/game/bond_car.h"
#include "driving/physics/rigid_body.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

uint32_t* R(float* p) { return reinterpret_cast<uint32_t*>(p); }
const uint32_t* R(const float* p) { return reinterpret_cast<const uint32_t*>(p); }

// vadd.xyzw / vsub.xyzw / vmulx.xyzw de quatro componentes.
void vadd4(float* o, const float* a, const float* b) {
    for (int c = 0; c < 4; ++c) R(o)[c] = ps2::add(R(a)[c], R(b)[c]);
}
void vsub4(float* o, const float* a, const float* b) {
    for (int c = 0; c < 4; ++c) R(o)[c] = ps2::sub(R(a)[c], R(b)[c]);
}
void vmulx4(float* o, const float* a, float k) {
    for (int c = 0; c < 4; ++c) R(o)[c] = ps2::mul(R(a)[c], ps2::fbits(k));
}

bool tun_float(const std::string& text, const std::string& key, float& out) {
    const size_t p = text.find(key + "=");
    if (p == std::string::npos) return false;
    out = float(std::strtod(text.c_str() + p + key.size() + 1, nullptr));
    return true;
}

}  // namespace

// [driving] 0x001eb7f0 RShadowMap::LoadAttributes
void RShadowMap_LoadAttributes(ShadowSettings& s, const std::string& tun, const std::string& car, float& width,
                               float& length) {
    width = ps2::ffloat(0x3f4ccccdu);  // 0,8
    length = width;
    s.strength = 0x18;
    tun_float(tun, "Shadowwidth{" + car + "}", width);
    tun_float(tun, "Shadowlength{" + car + "}", length);
    float v = 0;
    if (tun_float(tun, "PS2shadowstrength", v)) s.strength = int32_t(v);
    if (tun_float(tun, "Enableprojectedshadows", v)) s.projected = int32_t(v);
}

// [driving] 0x001ec4a0 RShadowMap::DrawSimpleShadow
void RShadowMap_DrawSimpleShadow(const BondCar& car, float width, float length, int32_t strength, ShadowQuad& out) {
    out = ShadowQuad();
    // As coordenadas de textura (0x392728).
    static const float kUv[4][4] = {{0, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}};
    std::memcpy(out.uv, kUv, sizeof kUv);
    const int32_t level_max = (strength * 0xff) >> 8;  // mult por 255, sra 8
    if (car.d.type != 1) {
        out.reason = 1;
        return;
    }
    const RigidBody* body = car.body();
    const RigidBodyInfo* info = body->info;
    if (ee::lt(info->ground[1], ps2::ffloat(0x3dcccccdu))) {  // a normal do chão sob o centro, y < 0,1
        out.reason = 2;
        return;
    }
    const float f20 = ee::cvt_s(level_max);
    float centre[4] = {body->pos[0], 0.0f, body->pos[2], 1.0f};
    float drop = 0.0f;
    float wheel[4][4] = {};
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c) R(wheel[i])[c] = ps2::add(R(info->lever_world[i])[c], R(body->pos)[c]);
        const float h = car.d.wheel_road_normal[i][3];  // GetWheelRoadHeight (a +0x1f4 da tabela virtual)
        const float below = ee::neg(h);
        if (ee::lt(drop, below)) drop = below;
        wheel[i][1] = ee::add(wheel[i][1], h);
        wheel[i][3] = 1.0f;
        centre[1] = ee::add(centre[1], wheel[i][1]);
    }
    const float limited = ee::lt(2.0f, drop) ? 2.0f : drop;
    const float f = ee::mul(f20, ee::mul(ee::sub(2.0f, limited), 0.5f));
    const float half = ee::lt(f, 0.0f) ? -0.5f : 0.5f;
    out.level = ee::cvt_w(ee::add(half, f));
    if (out.level <= 0) {
        out.reason = 3;
        return;
    }
    centre[1] = ee::add(ee::mul(centre[1], 0.25f), ps2::ffloat(0x3dcccccdu));
    float across[4], along[4], right[4], rear[4];
    vsub4(across, wheel[1], wheel[0]);
    vsub4(along, wheel[2], wheel[0]);
    vsub4(right, wheel[3], wheel[1]);
    vsub4(rear, wheel[3], wheel[2]);
    vadd4(across, across, rear);
    vadd4(along, along, right);
    if (ee::lt(3.0f, ee::abs(across[1]))) {
        out.reason = 4;
        return;
    }
    if (ee::lt(ps2::ffloat(0x406ccccdu), ee::abs(along[1]))) {  // 3,7
        out.reason = 5;
        return;
    }
    vmulx4(across, across, ee::mul(width, 0.5f));
    vmulx4(along, along, ee::mul(length, 0.5f));
    vsub4(out.corner[0], centre, across);
    vsub4(out.corner[0], out.corner[0], along);
    vadd4(out.corner[1], centre, across);
    vsub4(out.corner[1], out.corner[1], along);
    vsub4(out.corner[2], centre, across);
    vadd4(out.corner[2], out.corner[2], along);
    vadd4(out.corner[3], centre, across);
    vadd4(out.corner[3], out.corner[3], along);
    const uint32_t l = uint32_t(out.level);
    const uint32_t colour = (l << 24) | (l << 16) | (l << 8) | l;
    for (uint32_t& c : out.colour) c = colour;
    out.drawn = true;
}

}  // namespace drv
