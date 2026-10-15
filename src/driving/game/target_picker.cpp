// A mira das armas do carro (driving/game/target_picker.h, docs/driving.md seção 24): o WTargetPicker e os WTargetable,
// do MIPS do DRIVING.ELF (WTargetPicker.obj, WTargetable.obj, WSimpleZone.obj, WCollisionMgr::StepCheckHitWorld).
// Conferido na parte 20 do nf_driving_check (tests/driving_check_integ_ai.cpp).
//
// Divergências (cada uma também na função):
//  - a lista do PS2 (+0x2c, um list<WTargetable *> do STL da SGI) é um vetor do port na mesma ordem (o RegisterTarget
//    põe no fim, o UnregisterTarget tira todos os iguais, como o list::remove);
//  - o ATargeting (+0x7c, os bipes da trava) não foi portado: o estado pedido fica em target_picker_sound();
//  - o tan do 0x0022a128 é o tan do newlib em double emulado (o fptodp, o __kernel_tan e o dptofp): o port usa o
//    std::tan em double e arredonda para float (conferido; um ulp de diferença em double some no float);
//  - a câmera do jogador vem de current_player_camera() (o DAT_00333ed8 do PS2: +4 o RPlayerCamera e +0 o RViewCamera,
//    cujo RCamera é o mesmo); sem ela o PS2 leria o endereço 0: guardado e contado;
//  - o alvo de tipo 2 (um AICharacter) pergunta "vivo" pela tabela virtual (+0x140, a entrada +0x14) e o de tipo 3
//    (o AIVehicle dos helicópteros) a posição: pelos ganchos de target_picker_env() (sem eles: vivo, a posição fica).

#include "driving/game/target_picker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "driving/ai/element_controller.h"
#include "driving/anim/eagl_anim.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/camera_math.h"
#include "driving/camera/player_camera.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/weapons.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

TargetPickerEnv g_env;
TargetPickerStats g_stats;
int32_t g_sound_state = 0;
std::vector<uint32_t> g_list;  // o list<WTargetable *> do +0x2c

// As constantes do .data (0x33aa28..0x33aa34) e a tabela das zonas por modo (0x39a9e0).
const float kAutoDriveTargetLerpRate = ps2::ffloat(0x3d4ccccdu);  // 0,05
const float kSeekRadius = 130.0f;
const float kHoldRadius = 145.0f;
const float kLockTime = 0.25f;
const uint32_t kTargetRangeBits[2] = {5, 5};

uint32_t* words(uint32_t t) { return nf::p32_ptr<uint32_t>(t); }
WTargetableData& target(uint32_t t) { return *nf::p32_ptr<WTargetableData>(t); }
float& wf(uint32_t t, int i) { return reinterpret_cast<float*>(words(t))[i]; }

WTargetPickerData& picker() { return target_picker(); }

PlayerCamera* camera_or_guard() {
    PlayerCamera* c = current_player_camera();
    if (!c) ++g_stats.guards;  // o PS2 leria o RPlayerCamera nulo
    return c;
}

// O quadrado do comprimento xyz pela VU0 (vmul.xyz, vmulax.x, vmadday.x, vmaddz.x com o 1,0 de vf3.x).
uint32_t vu_len2_bits(const float d[3]) {
    const uint32_t x = ps2::fbits(d[0]), y = ps2::fbits(d[1]), z = ps2::fbits(d[2]);
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::madd(acc, 0x3f800000u, ps2::mul(z, z));
}

// O RigidBody do carro do jogador (o primeiro da lista +0xdcc).
const RigidBody* player_body(Simulation& sim) {
    if (sim.cars.empty()) {
        ++g_stats.guards;
        return nullptr;
    }
    return GetRigidBody(sim, sim.cars[0]->d.slot);
}

// FUN_001d4688 (RViewCamera, sem nome): a razão de aspecto da vista.
float view_aspect() {
    if (g_env.widescreen != 0) return ee::mul(g_env.aspect_mult, ps2::ffloat(0x3fe38e39u));  // 1,7777778
    return ee::mul(g_env.aspect_mult, ps2::ffloat(0x3faaaaaau));                             // 1,3333333
}

// O corpo de um PhysicsObject do port: a posição (rígido ou simples).
bool object_position(PhysicsObject* o, float out[3]) {
    Simulation& sim = simulation();
    if (SimpleObject* s = dynamic_cast<SimpleObject*>(o)) {
        const SimpleRigidBody* b = GetSimpleRigidBody(sim, s->po.slot);
        if (!b) return false;
        std::memcpy(out, b->pos, 12);
        return true;
    }
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (sim.owners[i] == o) {
            std::memcpy(out, GetRigidBody(sim, i)->pos, 12);
            return true;
        }
    return false;
}

}  // namespace

TargetPickerEnv& target_picker_env() { return g_env; }
TargetPickerStats& target_picker_stats() { return g_stats; }
int32_t& target_picker_sound() { return g_sound_state; }

// [driving] 0x002d18e8 TransformPoint
void TransformPoint3(const float m[16], const float in[4], float out[4]) {
    Reg v, r0, r1, r2, r3, acc, o;
    const Reg one = {0, 0, 0, 0x3f800000u};
    load(v, in);
    load(r0, m);
    load(r1, m + 4);
    load(r2, m + 8);
    load(r3, m + 12);
    load(o, out);
    mulabc(acc, r0, v, BX, XYZ);
    maddabc(acc, r1, v, BY, XYZ);
    maddabc(acc, r2, v, BZ, XYZ);
    maddbc(o, acc, r3, one, BW, XYZ);
    store(out, o);
}

// [driving] 0x002273d8 WSimpleZone::SetPosition
void WSimpleZone_SetPosition(int32_t zone[3], const float pos[3]) {
    for (int i = 0; i < 3; ++i) {
        const float f = ee::mul(pos[i], ps2::ffloat(0x3e4ccccdu));  // x 0,2
        const float h = ee::lt(f, 0.0f) ? -0.5f : 0.5f;
        zone[i] = ee::cvt_w(ee::add(h, f));
    }
}

// [driving] 0x002274a0 WSimpleZone::IsZoneInRange
bool WSimpleZone_IsZoneInRange(const int32_t a[3], const int32_t b[3], uint32_t bits) {
    const uint32_t r = 1u << (bits & 31);
    const uint32_t v = (uint32_t(a[0]) - (uint32_t(b[0]) - r)) | (uint32_t(a[1]) - (uint32_t(b[1]) - r)) |
                       (uint32_t(a[2]) - (uint32_t(b[2]) - r));
    return (v >> ((bits + 1) & 31)) == 0;
}

// [driving] 0x00216500 WCollisionMgr::StepCheckHitWorld
bool WCollisionMgr_StepCheckHitWorld(float seg[2][4]) {
    // O comprimento (xyz) e o número de passos de até 48 m.
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(seg[0][i], seg[1][i]);
    const float len = ps2::ffloat(ps2::add(0u, ps2::sqrt(vu_len2_bits(d))));
    const int32_t n = ee::cvt_w(ee::div(len, 48.0f)) + 1;
    const float step_len = ee::div(len, ee::cvt_s(n));
    // A direção (as quatro faixas: o w entra na norma) vezes o passo; o w do passo vira 1.
    Reg a, b, v, sq, one = {0x3f800000u, 0, 0, 0}, acc, q4, st;
    load(a, seg[1]);
    load(b, seg[0]);
    sub(v, a, b, XYZW);
    mul(sq, v, v, XYZW);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddabc(acc, one, sq, BZ, X);
    maddbc(q4, acc, one, sq, BW, X);
    const uint32_t rq = ps2::rsqrt(0x3f800000u, q4[0]);
    muls(v, v, rq, XYZW);
    const Reg k = {ps2::fbits(step_len), 0, 0, 0};
    mulbc(st, v, k, BX, XYZW);
    st[3] = 0x3f800000u;
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    for (int32_t i = 0; i < n; ++i) {
        Reg p0, p1;
        load(p0, seg[0]);
        load(p1, seg[1]);
        add(p1, p0, st, XYZ);
        store(seg[1], p1);
        seg[1][3] = 1.0f;
        seg[0][3] = 1.0f;
        WCollisionMgr_CheckHitWorld(&seg[0][0], info);
        if (info.hit != 0) return true;
        std::memcpy(seg[0], seg[1], 16);
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------- WTargetable

// [driving] 0x00229640 WTargetable::CommonInit
void WTargetable_CommonInit(uint32_t t) {
    uint32_t* w = words(t);
    TargetGlobals& g = target_globals();
    w[0] = w[1] = 0;
    w[10] = g.targets_in_world & 3;
    ++g.targets_in_world;
    w[2] = 0;
    w[0x10] = 1;
    w[0xc] = 5;
    w[0xb] = 0;
    w[3] = 0;
    w[6] = 0;
    w[0xe] = 0;
    w[0xf] = 1;
    ++g.live;
}

namespace {
uint32_t new_target() {
    WTargetableData* t = new WTargetableData();
    std::memset(t, 0, sizeof *t);
    const uint32_t p = nf::p32_u(t);
    WTargetable_Adopt(p);
    return p;
}
}  // namespace

// [driving] 0x00229490 WTargetable::WTargetable(float, float, float)
uint32_t WTargetable_NewPoint(float x, float y, float z) {
    const uint32_t t = new_target();
    uint32_t* w = words(t);
    w[7] = w[8] = w[9] = 0;
    WTargetable_CommonInit(t);
    wf(t, 0) = x;
    wf(t, 1) = y;
    wf(t, 2) = z;
    w[0x12] = 0;
    w[0xd] = 0;
    WSimpleZone_SetPosition(reinterpret_cast<int32_t*>(w + 7), &wf(t, 0));
    return t;
}

// [driving] 0x00229518 WTargetable::WTargetable(PhysicsObject *)
uint32_t WTargetable_NewObject(PhysicsObject* object) {
    const uint32_t t = new_target();
    uint32_t* w = words(t);
    w[7] = w[9] = w[8] = 0;
    WTargetable_CommonInit(t);
    w[0x12] = nf::p32_u(object);
    w[0xd] = 1;
    WTargetable_UpdatePosition(t);
    return t;
}

// [driving] 0x00229570 WTargetable::WTargetable(AICharacter *)
uint32_t WTargetable_NewCharacter(uint32_t aicharacter) {
    const uint32_t t = new_target();
    uint32_t* w = words(t);
    w[7] = w[8] = w[9] = 0;
    WTargetable_CommonInit(t);
    w[0x12] = aicharacter;
    w[0xd] = 2;
    WSimpleZone_SetPosition(reinterpret_cast<int32_t*>(w + 7), &wf(t, 0));
    return t;
}

// [driving] 0x002295d8 WTargetable::WTargetable(AIVehicle *)
uint32_t WTargetable_NewAIVehicle(uint32_t aivehicle) {
    const uint32_t t = new_target();
    uint32_t* w = words(t);
    w[7] = w[8] = w[9] = 0;
    WTargetable_CommonInit(t);
    w[0x12] = aivehicle;
    w[0xd] = 3;
    WSimpleZone_SetPosition(reinterpret_cast<int32_t*>(w + 7), &wf(t, 0));
    return t;
}

// [driving] 0x002296d8 WTargetable::UpdatePosition
void WTargetable_UpdatePosition(uint32_t t) {
    uint32_t* w = words(t);
    const int32_t kind = int32_t(w[0xd]);
    int32_t* zone = reinterpret_cast<int32_t*>(w + 7);
    if (kind == 2) {
        // O AICharacter (leiaute do PS2): a posição em +0x30, 0,2 m acima.
        const uint32_t* c = nf::p32_ptr<const uint32_t>(w[0x12]);
        if (c) {
            w[0] = c[0xc];
            w[1] = c[0xd];
            w[2] = c[0xe];
            wf(t, 1) = ee::add(wf(t, 1), 0.2f);
        } else {
            ++g_stats.guards;  // o PS2 leria o personagem nulo
        }
        WSimpleZone_SetPosition(zone, &wf(t, 0));
    } else if (kind == 1) {
        if (w[0x12] != 0) {
            float p[3];
            if (object_position(nf::p32_ptr<PhysicsObject>(w[0x12]), p)) std::memcpy(&wf(t, 0), p, 12);
            else ++g_stats.guards;
            WSimpleZone_SetPosition(zone, &wf(t, 0));
        }
    } else if (kind == 3) {
        float p[3];
        bool heli = false;
        if (g_env.ai_vehicle_position && g_env.ai_vehicle_position(w[0x12], p, heli)) {
            std::memcpy(&wf(t, 0), p, 12);
            if (heli) wf(t, 1) = ee::add(wf(t, 1), 1.5f);
        } else {
            ++g_stats.guards;
        }
        WSimpleZone_SetPosition(zone, &wf(t, 0));
    }
    // A contagem da visibilidade (+0x30, 5 no CommonInit): a cada seis chamadas o UpdateVisibility.
    int32_t n = int32_t(w[0xc]) - 1;
    if (int32_t(w[0xc]) < 1) {
        WTargetable_UpdateVisibility(t);
        n = 5;
    }
    w[0xc] = uint32_t(n);
}

// [driving] 0x00229870 WTargetable::UpdateVisibility
void WTargetable_UpdateVisibility(uint32_t t) {
    PlayerCamera* cam = current_player_camera();
    if (!cam) return;  // o PS2 também sai com o DAT_00333ed8 (ou o +4) nulo
    ++g_stats.visibility_checks;
    float seg[2][4];
    std::memcpy(seg[0], &cam->d.matrix[12], 16);
    seg[0][3] = 1.0f;
    std::memcpy(seg[1], &wf(t, 0), 12);
    seg[1][3] = 1.0f;
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(seg[0][i], seg[1][i]);
    const float dist = ps2::ffloat(ps2::add(0u, ps2::sqrt(vu_len2_bits(d))));
    if (ee::le(dist, 240.0f)) words(t)[3] = WCollisionMgr_StepCheckHitWorld(seg) ? 0u : 1u;
    else words(t)[3] = 0;
}

// [driving] 0x00229ad8 WTargetable::DistFromCamera
float WTargetable_DistFromCamera(uint32_t t, int32_t camera) {
    PlayerCamera* cam = camera == 0 ? camera_or_guard() : nullptr;
    if (!cam) {
        if (camera != 0) ++g_stats.guards;  // só a câmera 0 existe no port
        return 0.0f;
    }
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(cam->d.matrix[12 + i], wf(t, i));
    return ps2::ffloat(vu_len2_bits(d));
}

// [driving] 0x00229b40 WTargetable::DistFromScreenPos
float WTargetable_DistFromScreenPos(uint32_t t, const float point[2]) {
    const float dx = ee::sub(wf(t, 4), point[0]), dy = ee::sub(wf(t, 5), point[1]);
    return ee::add(ee::mul(dx, dx), ee::mul(dy, dy));
}

// [driving] 0x00229b88 WTargetable::DistFromScreenCenter
float WTargetable_DistFromScreenCenter(uint32_t t) {
    return ee::add(ee::mul(wf(t, 4), wf(t, 4)), ee::mul(wf(t, 5), wf(t, 5)));
}

// ---------------------------------------------------------------------------------------------------- WTargetPicker

// [driving] 0x00229f40 WTargetPicker::WTargetPicker
void WTargetPicker_Construct() {
    WTargetPickerData& p = picker();
    p.count = 0;
    p.selected = 0;
    p.unknown78 = 0;
    p.unknown80 = 0;
    p.active = 0;
    p.lock = 0;
    p.unknownac = 0;
    p.aim_quat[0] = p.aim_quat[1] = p.aim_quat[2] = 0.0f;
    p.aim_quat[3] = 1.0f;
    p.sort = 1;
    p.list = 0;
    g_list.clear();
    p.targeting_sound = 0;  // o ATargeting (não portado)
    g_sound_state = 0;
}

// [driving] 0x0022afa8 WTargetPicker::RegisterTarget
void WTargetPicker_RegisterTarget(uint32_t t) {
    if (t == 0) return;
    if (std::find(g_list.begin(), g_list.end(), t) != g_list.end()) return;
    WTargetable_AddReference(t);
    g_list.push_back(t);
    ++g_stats.registered;
    WTargetable_UpdatePosition(t);
    WTargetable_UpdateVisibility(t);
}

// [driving] 0x0022b098 WTargetPicker::UnregisterTarget
void WTargetPicker_UnregisterTarget(uint32_t t) {
    if (t == 0) return;
    if (std::find(g_list.begin(), g_list.end(), t) == g_list.end()) return;
    WTargetPickerData& p = picker();
    if (t == p.selected) p.selected = 0;
    g_list.erase(std::remove(g_list.begin(), g_list.end(), t), g_list.end());
    ++g_stats.unregistered;
    WTargetable_RemoveReference(t);
}

// [driving] 0x0022b130 WTargetPicker::ClearAllTargets: o list::clear (os nós; as referências ficam, como no PS2).
void WTargetPicker_ClearAllTargets() { g_list.clear(); }

// [driving] 0x0022a728 WTargetPicker::IsPointOnScreen
bool WTargetPicker_IsPointOnScreen(const float point[2]) {
    const float w = ee::cvt_s(g_env.screen_w), h = ee::cvt_s(g_env.screen_h);
    float hx = ee::mul(w, ps2::ffloat(0x3eeb8520u));  // 0,46000004
    const float hy = ee::mul(h, ps2::ffloat(0x3ecccccdu));  // 0,4
    if (g_env.widescreen != 0) hx = ee::add(hx, 107.0f);
    const float x = ee::sub(point[0], ee::mul(w, 0.5f));
    const float y = ee::sub(point[1], ee::mul(h, 0.5f));
    return ee::le(ee::neg(hy), y) && ee::le(y, hy) && ee::le(ee::neg(hx), x) && ee::le(x, hx);
}

// [driving] 0x0022a128 (WTargetPicker, o ponto na tela)
void WTargetPicker_GetScreenPos(const float pos[4], float out[2]) {
    PlayerCamera* cam = camera_or_guard();
    if (!cam) {
        out[0] = out[1] = 10000.0f;
        return;
    }
    // Uma cópia do RCamera (0xa8 bytes) com a inversa refeita se a matriz mudou.
    PlayerCameraData copy;
    std::memcpy(&copy, &cam->d, 0xa8);
    float f = ee::mul(copy.fov, g_env.fov_mult);
    f = ee::mul(f, ps2::ffloat(0x3c8efa35u));  // pi / 180
    const float tanv = float(std::tan(double(f)));  // fptodp, tan (newlib), dptofp
    RCamera_CreateMatrix4Inv(copy);
    alignas(16) float p[4] = {pos[0], pos[1], pos[2], pos[3]};
    alignas(16) float c[4] = {0, 0, 0, 0};
    TransformPoint3(copy.inverse, p, c);
    if (ee::le(c[2], ps2::ffloat(0x3c23d70au))) {  // 0,01
        out[0] = out[1] = 10000.0f;
        return;
    }
    float aspect = view_aspect();
    if (g_env.widescreen != 0) aspect = ee::mul(aspect, 0.75f);
    float fh = ee::cvt_s(g_env.screen_h), fw = ee::cvt_s(g_env.screen_w);
    const float kh = ee::div(2.0f, fh), kw = ee::div(2.0f, fw);
    const float z = ee::mul(ee::abs(c[2]), tanv);
    const float y = ee::mul(c[1], aspect);
    fh = ee::mul(fh, 0.5f);
    fw = ee::mul(fw, 0.5f);
    const float zh = ee::mul(z, kh), zw = ee::mul(z, kw);
    out[1] = ee::sub(fh, ee::div(y, zh));
    out[0] = ee::add(fw, ee::div(c[0], zw));
}

// [driving] 0x0022a430 (WTargetPicker::GetOffScreenPos no Xbox)
void WTargetPicker_GetOffScreenPos(const float pos[4], float out[2]) {
    PlayerCamera* cam = camera_or_guard();
    if (!cam) {
        out[0] = out[1] = 10000.0f;
        return;
    }
    const float* m = cam->d.matrix;
    // A matriz da vista: a rotação transposta e -t pela rotação transposta (o w da linha 3 = 1).
    alignas(16) float v[16] = {m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1};
    Reg r0, r1, r2, r3, t, acc, o;
    const float nt[4] = {ee::neg(m[12]), ee::neg(m[13]), ee::neg(m[14]), 0.0f};
    load(t, nt);
    load(r0, v);
    load(r1, v + 4);
    load(r2, v + 8);
    load(r3, v + 12);
    mulabc(acc, r0, t, BX, XYZW);
    maddabc(acc, r1, t, BY, XYZW);
    maddabc(acc, r2, t, BZ, XYZW);
    maddbc(o, acc, r3, t, BW, XYZW);
    store(v + 12, o);
    v[15] = 1.0f;
    load(r3, v + 12);
    const float pt[4] = {pos[0], pos[1], pos[2], 1.0f};
    load(t, pt);
    mulabc(acc, r0, t, BX, XYZW);
    maddabc(acc, r1, t, BY, XYZW);
    maddabc(acc, r2, t, BZ, XYZW);
    maddbc(o, acc, r3, t, BW, XYZW);
    o[2] = 0;
    // A normal xyz (vrsqrt de x² + y² + z²).
    Reg sq, one = {0x3f800000u, 0, 0, 0}, l;
    mul(sq, o, o, XYZ);
    mulabc(acc, o, o, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddbc(l, acc, one, sq, BZ, X);
    muls(o, o, ps2::rsqrt(0x3f800000u, l[0]), XYZ);
    const float nx = ps2::ffloat(o[0]), ny = ps2::ffloat(o[1]);
    const float w = ee::cvt_s(g_env.screen_w), h = ee::cvt_s(g_env.screen_h);
    float sx = ee::mul(ee::add(nx, 0.5f), w);
    const float sy = ee::mul(ee::sub(0.5f, ny), h);
    const float hx = ee::mul(w, ps2::ffloat(0x3eeb8520u)), cx = ee::mul(w, 0.5f);
    const float cy = ee::mul(h, 0.5f), hy = ee::mul(h, ps2::ffloat(0x3ecccccdu));
    float ox = sx, oy = sy, f0 = 0.0f, f1 = 0.0f;
    bool scale_y = false;
    if (ee::lt(sx, ee::sub(cx, hx))) {
        f0 = ee::sub(sy, cy);
        ox = ee::sub(cx, hx);
        f1 = ee::sub(cx, sx);
        scale_y = true;
    } else if (ee::lt(ee::add(cx, hx), sx)) {
        f0 = ee::sub(sy, cy);
        ox = ee::add(cx, hx);
        f1 = ee::sub(sx, cx);
        scale_y = true;
    }
    if (scale_y) oy = ee::add(cy, ee::div(ee::mul(f0, hx), f1));
    float edge = ee::sub(cy, hy), d = 0.0f;
    bool scale_x = false;
    if (ee::lt(oy, edge)) {
        d = ee::sub(cy, oy);
        scale_x = true;
    } else {
        edge = ee::add(cy, hy);
        if (ee::lt(edge, oy)) {
            d = ee::sub(oy, cy);
            scale_x = true;
        }
    }
    if (scale_x) {
        oy = edge;
        ox = ee::add(cx, ee::div(ee::mul(ee::sub(ox, cx), hy), d));
    }
    (void)sx;
    out[0] = ox;
    out[1] = oy;
}

// [driving] 0x0022b638 WTargetPicker::ActivateTargeting
void WTargetPicker_ActivateTargeting(int32_t mode) {
    WTargetPickerData& p = picker();
    if (p.active == 0 || p.mode != mode) {
        p.active = 1;
        p.mode = mode;
        p.aim_quat[3] = 1.0f;
        p.unknown98 = 0;
        p.lock = 0;
        p.selected = 0;
        p.aim_quat[0] = p.aim_quat[1] = p.aim_quat[2] = 0.0f;
        p.unknown84 = 0;
        p.unknown88 = 0;
        p.unknown94 = 0;
    }
}

// [driving] 0x0022b698 WTargetPicker::DeactivateTargeting
void WTargetPicker_DeactivateTargeting() {
    WTargetPickerData& p = picker();
    if (p.active != 0) {
        p.lock = 0;
        p.active = 0;
        p.selected = 0;
    }
}

// [driving] 0x0022af38 WTargetPicker::GetSelectedTarget
uint32_t WTargetPicker_GetSelectedTarget() {
    const uint32_t s = picker().selected;
    if (s != 0 && words(s)[0x11] != 0) return s;
    return 0;
}

// [driving] 0x0022b6b8 WTargetPicker::CycleToNextTarget
void WTargetPicker_CycleToNextTarget() {
    WTargetPickerData& p = picker();
    if (p.count > 0) {
        ++p.cycle;
        if (p.count <= p.cycle) p.cycle = 0;
        if (words(p.sorted[p.cycle])[0x11] == 0) p.cycle = 0;
        p.selected = p.sorted[p.cycle];
        return;
    }
    p.selected = 0;
}

// [driving] 0x0022b150 WTargetPicker::CompareDistFromScreenCenter
int WTargetPicker_CompareDistFromScreenCenter(const void* a, const void* b) {
    const uint32_t tb = *static_cast<const uint32_t*>(b);
    const float fa = WTargetable_DistFromScreenCenter(*static_cast<const uint32_t*>(a));
    const float fb = WTargetable_DistFromScreenCenter(tb);
    const float d = ee::sub(fa, fb);
    if (ee::eq(d, 0.0f)) return 0;
    return ee::lt(0.0f, d) ? 1 : -1;
}

// [driving] 0x0022b1c0 WTargetPicker::CompareDistFromScreenPos (o alvo do mundo na tela, 0x0022a128 no +0x10)
int WTargetPicker_CompareDistFromScreenPos(const void* a, const void* b) {
    const uint32_t ta = *static_cast<const uint32_t*>(a), tb = *static_cast<const uint32_t*>(b);
    float s[2];
    WTargetPicker_GetScreenPos(picker().world_target, s);
    const float fa = words(ta)[0x11] == 0 ? 1e7f : WTargetable_DistFromScreenPos(ta, s);
    const float fb = words(tb)[0x11] == 0 ? 1e7f : WTargetable_DistFromScreenPos(tb, s);
    const float d = ee::sub(fa, fb);
    if (ee::eq(d, 0.0f)) return 0;
    return ee::lt(0.0f, d) ? 1 : -1;
}

// [driving] 0x0022b2a0 WTargetPicker::CompareDistFromCamera
int WTargetPicker_CompareDistFromCamera(const void* a, const void* b) {
    const uint32_t tb = *static_cast<const uint32_t*>(b);
    const float fa = WTargetable_DistFromCamera(*static_cast<const uint32_t*>(a), 0);
    const float fb = WTargetable_DistFromCamera(tb, 0);
    const float d = ee::sub(fa, fb);
    if (ee::eq(d, 0.0f)) return 0;
    return ee::lt(0.0f, d) ? 1 : -1;
}

// [driving] 0x0022aab0 WTargetPicker::UpdateSelection
void WTargetPicker_UpdateSelection() {
    WTargetPickerData& p = picker();
    Simulation& sim = simulation();
    ++g_stats.selections;
    if (p.mode == 1) p.selected = 0;
    if (p.selected != 0) {
        bool found = false;
        for (int32_t i = 0; i < p.count; ++i)
            if (p.sorted[i] == p.selected) {
                found = true;
                break;
            }
        if (!found) p.selected = 0;
    }
    // O alvo do mundo (+0x10; vmulx.xyz e vadd.xyz: o w, o passo da trava, fica).
    Reg vpos = {0, 0, 0, 0x3f800000u}, vfwd, vk = {0, 0, 0, 0}, vt, vold;
    load(vold, p.world_target);
    if (p.mode == 1) {
        PlayerCamera* cam = camera_or_guard();
        if (cam) {
            load(vpos, &cam->d.matrix[12]);  // a linha 3 inteira (o w também vai ao segmento do submarino)
            load(vfwd, RPlayerCamera_GetForwardAimVec4(*cam, 1.0f));
            vk[0] = 0x447a0000u;  // 1000
            mulbc(vt, vfwd, vk, BX, XYZ);
            add(vold, vpos, vt, XYZ);
            store(p.world_target, vold);
        }
    } else {
        const RigidBody* rb = player_body(sim);
        if (rb) {
            const float pos[4] = {rb->pos[0], rb->pos[1], rb->pos[2], 1.0f};
            load(vpos, pos);
            load(vfwd, rb->info->orient[2]);
            vk[0] = 0x432f0000u;  // 175
            mulbc(vt, vfwd, vk, BX, XYZ);
            add(vold, vpos, vt, XYZ);
            store(p.world_target, vold);
        }
    }
    BondCar* car = sim.cars.empty() ? nullptr : sim.cars[0];
    const CarPhysics* ph = car ? car->physics() : nullptr;
    if (!ph) ++g_stats.guards;  // o PS2 chamaria a entrada +0x1d4 do carro nulo
    // Num submarino (SUB_PHYSICS) o alvo é o choque do segmento até ele com o mundo (salvo a instância com o bit 1).
    if (ph && ph->sub_physics != 0) {
        // O começo: o lugar do ramo de cima (a câmera, ou o corpo com w 1).
        alignas(16) float seg[2][4] = {};
        store(seg[0], vpos);
        seg[1][0] = p.world_target[0];
        seg[1][1] = p.world_target[1];
        seg[1][2] = p.world_target[2];
        seg[1][3] = 1.0f;
        WorldCollisionInfo info;
        WorldCollisionInfo_Init(info);
        if (WCollisionMgr_CheckHitWorld(&seg[0][0], info)) {
            uint32_t flags = 0;
            if (info.face_instance) flags = nf::p32_ptr<const WCollisionInstance>(info.face_instance)->flags;
            else ++g_stats.guards;  // o PS2 leria o +0x18 do endereço 0 (a instância nula de um objeto)
            if ((flags & 2) == 0) {
                p.world_target[0] = info.point[0];
                p.world_target[1] = info.point[1];
                p.world_target[2] = info.point[2];
            }
        }
    }
    // O alvo do mundo na tela (num submarino, o centro).
    if (ph && ph->is_sub != 0) {
        p.screen[0] = ee::mul(ee::cvt_s(g_env.screen_w), 0.5f);
        p.screen[1] = ee::mul(ee::cvt_s(g_env.screen_h), 0.5f);
    } else {
        WTargetPicker_GetScreenPos(p.world_target, p.screen);
    }
    if (p.count > 0) {
        p.cycle = 0;
        p.selected = p.sorted[0];
        if (p.selected != 0 && (p.mode == 1 || (ph && ph->is_sub != 0))) {
            const float r = p.lock == 0 ? kSeekRadius : kHoldRadius;
            if (ee::lt(ee::mul(r, r), WTargetable_DistFromScreenPos(p.selected, p.screen))) p.selected = 0;
        }
    }
}

// [driving] 0x0022a960 WTargetPicker::MoveTargetingCursors
void WTargetPicker_MoveTargetingCursors() {
    WTargetPickerData& p = picker();
    Simulation& sim = simulation();
    bool follow = false;
    bool weapon_ok = p.mode != 1;
    if (!weapon_ok) {
        SWeaponManagerData* wm = weapon_manager();
        const WeaponEntry* e = wm ? weapon_entry(*wm, wm->secondary) : nullptr;
        if (!e) ++g_stats.guards;
        else weapon_ok = e->type == 0x1c;
    }
    if (weapon_ok && p.selected != 0) follow = words(p.selected)[0x11] != 0;
    if (follow) {
        const int32_t start = int32_t(ps2::fbits(p.world_target[3]));  // o +0x1c como inteiro
        const float t = ee::mul(ee::cvt_s(int32_t(uint32_t(sim.steps) - uint32_t(start))), sim.dt);
        if (ee::lt(kLockTime, t)) {
            p.lock = 2;
            g_sound_state = 2;
            ++g_stats.locks;
        } else {
            p.lock = 1;
            g_sound_state = 1;
        }
        ++g_stats.sound_states;
        p.cursor[0] = wf(p.selected, 4);
        p.cursor[1] = wf(p.selected, 5);
    } else {
        p.lock = 0;
        p.world_target[3] = ps2::ffloat(uint32_t(sim.steps));
        g_sound_state = 0;
        ++g_stats.sound_states;
        WTargetPicker_GetScreenPos(p.world_target, p.cursor);
    }
}

// [driving] 0x0022b918 WTargetPicker::UpdateAutoDriveTargeting
void WTargetPicker_UpdateAutoDriveTargeting() {
    WTargetPickerData& p = picker();
    PlayerCamera* cam = camera_or_guard();
    if (!cam) return;
    ++g_stats.autodrive;
    if (RPlayerCamera_CameraAiming(*cam) == 1) {
        p.aim_quat[0] = 0.0f;
        p.aim_quat[3] = 1.0f;
        p.aim_quat[1] = 0.0f;
        p.aim_quat[2] = 0.0f;
        return;
    }
    alignas(16) float q[4];
    if (p.lock == 2) {
        // A direção para o alvo no referencial da câmera (as colunas da matriz: o pextlw/pcpyld transpõe).
        const float* m = cam->d.matrix;
        alignas(16) float dv[4] = {ee::sub(wf(p.selected, 0), m[12]), ee::sub(wf(p.selected, 1), m[13]),
                                   ee::sub(wf(p.selected, 2), m[14]), 0.0f};
        const float c0[4] = {m[0], m[4], m[8], m[12]}, c1[4] = {m[1], m[5], m[9], m[13]},
                    c2[4] = {m[2], m[6], m[10], m[14]};
        Reg v, r0, r1, r2, acc, o;
        load(v, dv);
        load(r0, c0);
        load(r1, c1);
        load(r2, c2);
        load(o, dv);
        mulabc(acc, r0, v, BX, XYZ);
        maddabc(acc, r1, v, BY, XYZ);
        maddbc(o, acc, r2, v, BZ, XYZ);
        store(dv, o);
        // RCameraMath::VU0_GenerateQuat (0x001a7398): VU0_GenerateMatrix4 e VU0_m4toquat.
        alignas(16) float g[4][4] = {};
        RCameraMath_VU0_GenerateMatrix4(dv, g);
        VU0_m4toquat(g, q);
    } else {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
    }
    // VU0_fastqslerp (0x002cc018, o mesmo código do slerp rápido do EAGL).
    anim::fast_qslerp(p.aim_quat, q, p.aim_quat, kAutoDriveTargetLerpRate);
}

// [driving] 0x0022b808 WTargetPicker::GetDirectionToTarget(COORD3 &)
void WTargetPicker_GetDirectionToTarget(float out[3]) {
    PlayerCamera* cam = camera_or_guard();
    if (!cam) return;
    std::memcpy(out, RPlayerCamera_GetForwardAimVec4(*cam, 1.0f), 12);
}

// [driving] 0x0022b858 WTargetPicker::GetDirectionToTarget(MATRIX4 &)
void WTargetPicker_GetDirectionToTargetMatrix(float out[16]) {
    PlayerCamera* cam = camera_or_guard();
    if (!cam) return;
    std::memcpy(out, RPlayerCamera_GetAimMatrix4(*cam, 1.0f, 1.0f), 64);
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;  // o vf0 (0, 0, 0, 1)
}

// [driving] 0x0022b318 WTargetPicker::UpdateTargets
void WTargetPicker_UpdateTargets() {
    WTargetPickerData& p = picker();
    Simulation& sim = simulation();
    ++g_stats.updates;
    int32_t zone[3] = {0, 0, 0};
    if (const RigidBody* rb = player_body(sim)) WSimpleZone_SetPosition(zone, rb->pos);
    int32_t n = 0;
    const std::vector<uint32_t> list = g_list;  // o PS2 anda pelos nós (nada aqui muda a lista)
    for (uint32_t t : list) {
        uint32_t* w = words(t);
        int32_t c = int32_t(w[10]) - 1;
        w[10] = uint32_t(c);
        if (c < 0) w[10] = w[11];
        if (w[10] != 0) continue;
        const int32_t kind = int32_t(w[0xd]);
        bool alive = true;
        if (kind == 2) {
            if (g_env.character_alive) alive = g_env.character_alive(w[0x12]);
            else ++g_stats.guards;
        } else if (kind == 1) {
            PhysicsObject* o = nf::p32_ptr<PhysicsObject>(w[0x12]);
            alive = o && ee::lt(0.0f, o->hit_points());
            if (!o) ++g_stats.guards;
        }
        if (!alive) continue;
        WTargetable_UpdatePosition(t);
        if (uint32_t(p.mode) > 1) ++g_stats.guards;  // o PS2 leria além da tabela das zonas (só os modos 0 e 1)
        if (!WSimpleZone_IsZoneInRange(zone, reinterpret_cast<const int32_t*>(w + 7),
                                       kTargetRangeBits[p.mode == 1 ? 1 : 0])) {
            if (int32_t(w[11]) < 6) ++w[11];
            continue;
        }
        w[10] = 0;
        w[11] = 0;
        float s[2];
        WTargetPicker_GetScreenPos(&wf(t, 0), s);
        wf(t, 4) = s[0];
        wf(t, 5) = s[1];
        const bool on = WTargetPicker_IsPointOnScreen(s);
        if (w[0x10] == 0 || w[3] == 0) continue;
        w[0x11] = on ? 1u : 0u;
        ++g_stats.visible;
        if (!on) {
            WTargetPicker_GetOffScreenPos(&wf(t, 0), s);
            wf(t, 4) = s[0];
            wf(t, 5) = s[1];
        } else {
            ++g_stats.on_screen;
        }
        p.sorted[n++] = t;
        if (n >= 16) break;
    }
    p.count = n;
    if (n > 0) {
        static int (*const kSort[3])(const void*, const void*) = {WTargetPicker_CompareDistFromScreenCenter,
                                                                  WTargetPicker_CompareDistFromScreenPos,
                                                                  WTargetPicker_CompareDistFromCamera};
        newlib_qsort(p.sorted, size_t(n), 4, kSort[uint32_t(p.sort) < 3 ? p.sort : 1]);
    }
    bool skip = false;
    if (mission_exists()) {
        const SMissionManagerData& m = mission_manager();
        skip = m.state == 3 || m.state == 4 || m.cinematic_mode != 0;
    }
    if (sim.state == 3) skip = true;
    if (p.active != 0 && !skip) {
        WTargetPicker_UpdateSelection();
        WTargetPicker_MoveTargetingCursors();
        if (p.mode == 1) WTargetPicker_UpdateAutoDriveTargeting();
    }
}

}  // namespace drv
