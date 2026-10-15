// Os modos da câmera do jogador que a seção do helicóptero de Paris usa (docs/driving.md seção 23), portados do MIPS do
// DRIVING.ELF com a aritmética do PS2 na ordem das instruções (platform/ps2_float.h, vu0_macro.h):
//  - o AutoDrive (o modo 0x200 do camera.ini: "CopterAutoDriveCam" para o helicóptero): SetAutoDriveCamera, o caso 0x200
//    do DirectorChangeCameraMode (em player_camera.cpp), UpdateAutoDriveCam com UpdateADWeaponAnims,
//    UpdateADTargetAngles, LimitPitchYaw e as transições, a entrada (CameraInputCallback, AutoDriveCamInputHandler: o
//    giro pelo controle, o zoom, o centro), CameraLockOn (o ponto em que a câmera se prende), InitWeaponChange, InitSpin,
//    WeaponFired;
//  - as câmeras animadas: a do mundo (o 'Cams' da pista; os registros sem animação: a curva do RCameraSpline do olho até
//    o ponto da câmera) e a do trilho da IA (um 'AISp' andado por um AISplinePath, o mesmo do trilho do carro);
//  - o fim delas e as trocas de modo dos eventos: EndCameraAnim, SetLastSelectableCameraMode, SetOldCameraMode,
//    ForceCameraChange, ShakeCamera, EndMissileCamera.
//
// Divergências e o que não foi portado (conta em camera_pending(), as leituras fora do lugar em guards):
//  - as câmeras animadas de verdade (registro 'Cams' com animação, +0x1c não nulo: o RAnimEngine, UpdateAnimationCam) e a
//    câmera relativa a um carro (TriggerCarAnimationCamera: as animações 'Cams' do modelo da âncora; nenhum modelo de
//    Paris as tem): contam;
//  - o SetOldCameraMode com transição de 2 quadros ou mais para um modo que não é Heli com ruído (o PS2 monta um
//    RPlayerCamera inteiro na pilha para achar o ponto de partida): conta e só pede o modo;
//  - o DriveCamInputHandler (a entrada da câmera fora do POV: trocar de câmera, olhar para trás): conta;
//  - o RColorize (a cor da câmera do míssil) e o AICharacterHands::fgZoomButton (o global 0x3276e0: guardado em
//    statics.zoom_button) ficam só anotados;
//  - o PS2 lê o braço -1 do AutoDrive antes do primeiro quadro do modo (ToggleAutoDriveZoom, SetCameraZoom tipo 2): o
//    port usa um braço zerado e conta em guards.

#include <atomic>
#include <cstring>

#include "driving/anim/eagl_anim.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "driving/game/rail.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

std::atomic<uint64_t> g_modes_pending{0};
void pending_mode() { g_modes_pending.fetch_add(1); }

float fb(uint32_t b) { return ps2::ffloat(b); }
const uint32_t kOne = 0x3f800000u;
const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000u};

// vmul.xyz/vmulax.x/vmadday.x/vmaddz.x com 1: (x x + y y) + z z.
uint32_t vu_len2(const Reg v) {
    Reg t, acc{}, one = {kOne, 0, 0, 0};
    mul(t, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, t, BY, X);
    maddbc(t, acc, one, t, BZ, X);
    return t[0];
}
// vsqrt Q; vaddq.x vf3, vf0, Q.
float vu_length(const Reg v) { return ps2::ffloat(ps2::add(0, ps2::sqrt(vu_len2(v)))); }
// vrsqrt Q, vf0w, |v|²; vmulq.xyz d, v, Q (o w de d fica).
void vu_normalize3(Reg d, const Reg v) { muls(d, v, ps2::rsqrt(kOne, vu_len2(v)), XYZ); }
// vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a.
void vu_cross(Reg d, const Reg a, const Reg b) {
    Reg acc{};
    opmula(acc, a, b);
    opmsub(d, acc, b, a);
}
// vmul.xyz t, a, b; vaddy.x; vaddz.x: (ax bx + ay by) + az bz.
float vu_dot3(const Reg a, const Reg b) {
    Reg t{};
    mul(t, a, b, XYZ);
    addbc(t, t, t, BY, X);
    addbc(t, t, t, BZ, X);
    return ps2::ffloat(t[0]);
}
// v * M nas linhas 0..2 (vmulax.xyz ACC, m0, v; vmadday.xyz ACC, m1, v; vmaddz.xyz d, m2, v): só o xyz de d.
void vu_xform3(Reg d, const Reg v, const float* m) {
    Reg m0, m1, m2, acc{};
    load(m0, m);
    load(m1, m + 4);
    load(m2, m + 8);
    mulabc(acc, m0, v, BX, XYZ);
    maddabc(acc, m1, v, BY, XYZ);
    maddbc(d, acc, m2, v, BZ, XYZ);
}
// d = d + row * k (vmulx.xyzw t, row, k; vadd.xyzw d, d, t).
void vu_add_scaled4(Reg d, const float* row, float k) {
    Reg r, t, s = {ps2::fbits(k), 0, 0, 0};
    load(r, row);
    mulbc(t, r, s, BX, XYZW);
    add(d, d, t, XYZW);
}

// A curva da resposta do controle do AutoDrive (0x3825d0: 97 bytes, índice = |v| x 96; e os três zeros seguintes).
const uint8_t kAutoDriveCurve[100] = {
    0,   0,   0,   0,   0,   10,  10,  10,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
    22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,
    42,  43,  44,  45,  46,  47,  48,  49,  50,  52,  54,  56,  58,  60,  62,  64,  66,  68,  70,  72,
    74,  76,  78,  80,  82,  84,  86,  88,  90,  94,  98,  102, 106, 110, 114, 118, 122, 126, 130, 134,
    138, 142, 146, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250, 255, 255, 255, 0,   0,   0};

// (float)(uint32_t) como o GCC do PS2 converte (o bit alto: metade, arredondando, e o dobro).
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(f, f);
}

}  // namespace

uint64_t camera_modes_pending() { return g_modes_pending.load(); }
const uint8_t* camera_autodrive_curve() { return kAutoDriveCurve; }

// ---------------------------------------------------------------- o diretor

void PlayerCamera::SetCameraModeByIndex(int32_t mode, uint16_t delay, uint16_t flags, uint16_t transition,
                                        uint32_t data24, uint32_t data18, const float* vector) {
    if (!(mode < tables->tables.mode_count)) return;
    DirectorQueueData q{};
    q.delay = delay;
    q.mode = uint16_t(mode);
    q.transition = transition;
    q.flags = flags;
    q.data18 = data18;
    q.anchor = 0;
    q.data24 = data24;
    q.data1c = 0;
    if (vector) std::memcpy(q.vector, vector, 16);
    else std::memcpy(q.vector, kVf0, 16);
    AppendDirectorData(q);
}

// [driving] 0x1bb7c0 RPlayerCamera::SetAutoDriveCamera
void PlayerCamera::SetAutoDriveCamera(const CameraContext& ctx, float yaw, float pitch, int32_t relative) {
    float v[4] = {yaw, ee::neg(pitch), 0.0f, relative != 0 ? 1.0f : 0.0f};
    director_queue.clear();
    const int32_t index = tables->tables.autodrive_index;
    SetCameraModeByIndex(index, uint16_t(tables->constants.auto_drive_latency), 4, 0, 0, 0, v);
    d.last_step += 1;
    update(ctx);
    SetCameraModeByIndex(index, 0, 4, 0, 0, 0, v);
    d.last_step += 1;
    update(ctx);
}

// [driving] 0x1bc0e0 RPlayerCamera::SetAutoDriveRotation
float PlayerCamera::SetAutoDriveRotation(float v) const {
    if (tables->mode(d.mode).type != kCamAutoDrive) return 0.0f;
    const float k = fb(0x3ba3d70au), d255 = fb(0x437f0000u);
    if (ee::lt(0.0f, v)) {
        const int32_t i = ee::cvt_w(ee::mul(v, 96.0f));
        if (i < 0 || i >= 100) return 0.0f;  // (fora da tabela: o PS2 leria o que vem depois)
        return ee::div(ee::mul(ee::cvt_s(kAutoDriveCurve[i]), k), d255);
    }
    if (ee::lt(v, 0.0f)) {
        const int32_t i = -ee::cvt_w(ee::mul(v, 96.0f));
        if (i < 0 || i >= 100) return 0.0f;
        return ee::div(ee::mul(ee::neg(ee::cvt_s(kAutoDriveCurve[i])), k), d255);
    }
    return 0.0f;
}

namespace {
// [driving] 0x1bc1e8 (sem nome): os dois giros pedidos presos a -1..1 (a conta do meio vezes 0 não muda nada: fica).
void clamp_rates(float& x, float& y) {
    const float ax = ee::abs(x), ay = ee::abs(y);
    const float big = ee::lt(ay, ax) ? ax : ay;
    if (!ee::lt(0.0f, big)) return;
    double dx = double(x);
    if (dx < 0.0) dx = 0.0 - dx;  // dpcmp/dpsub da biblioteca de ponto flutuante duplo do GCC
    const float diff = float(dx - double(ay));
    const float f0 = ee::sub(big, ee::abs(diff));
    const float f2 = ee::mul(ee::div(f0, big), 0.0f);
    x = ee::lt(0.0f, x) ? ee::add(x, f2) : ee::sub(x, f2);
    y = ee::lt(0.0f, y) ? ee::add(y, f2) : ee::sub(y, f2);
    if (ee::lt(1.0f, x)) x = 1.0f;
    if (ee::lt(x, -1.0f)) x = -1.0f;
    if (ee::lt(1.0f, y)) y = 1.0f;
    if (ee::lt(y, -1.0f)) y = -1.0f;
}
}  // namespace

int32_t PlayerCamera::SetAutoDriveRotationX(float v) {
    d.target_pitch_rate = SetAutoDriveRotation(v);
    clamp_rates(d.target_pitch_rate, d.target_yaw_rate);
    return d.limited;
}

int32_t PlayerCamera::SetAutoDriveRotationY(float v) {
    d.target_yaw_rate = SetAutoDriveRotation(v);
    clamp_rates(d.target_pitch_rate, d.target_yaw_rate);
    return d.limited;
}

void PlayerCamera::SetAutoDriveZoom(float v) {
    d.zoom_speed = 1.0f;
    d.zoom_slope = ee::mul(tables->modes.zoom_factor, v);
}

void PlayerCamera::ToggleAutoDriveZoom(int32_t on) {
    if (on == 1) d.zoom_fov_max = ad_arm_info(d.ad_arm).aim_fov;
    else d.zoom_fov_max = tables->mode(d.mode).default_fov;
}

void PlayerCamera::InitAimZoom() {
    d.dz_pitch = 0.0f;
    d.dz_yaw = 0.0f;
    if (!ee::eq(ad_arm_info(d.ad_arm).aim_fov, d.dz_pitch)) ToggleAutoDriveZoom(int32_t(state.zoom_held));
}

void PlayerCamera::ResetZoomSlope() {
    d.zoom_slope = 0.0f;
    d.zoom_speed = 1.0f;
}

void PlayerCamera::SetLastSelectableCameraMode(uint16_t delay, uint32_t transition, uint32_t arg4,
                                               int32_t keep_autodrive) {
    const CameraTableGlobals& g = tables->tables;
    if (keep_autodrive != 0) {
        if (d.mode == g.autodrive_index) return;
        if (d.prev_mode == g.autodrive_index) return;
    }
    if (g.mode_count <= d.prev_selectable) d.prev_selectable = 0;
    if (tables->mode(d.prev_selectable).selectable == 0 && tables->mode(d.mode).selectable != 0)
        d.prev_selectable = d.mode;
    if (tables->mode(d.prev_selectable).selectable == 0) {
        d.prev_selectable = 0;
        if (g.mode_count != 0 && tables->mode(0).selectable == 0) {
            for (;;) {
                d.prev_selectable += 1;
                if (uint32_t(g.mode_count) <= uint32_t(d.prev_selectable) ||
                    tables->mode(d.prev_selectable).selectable != 0)
                    break;
            }
        }
    }
    if (tables->mode(d.prev_selectable).selectable != 0) SetOldCameraMode(d.prev_selectable, delay, transition, arg4);
}

void PlayerCamera::SetOldCameraMode(int32_t mode, uint16_t delay, uint32_t transition, uint32_t arg4) {
    (void)arg4;
    if (!(mode < tables->tables.mode_count)) return;
    const CameraModeInfo& m = tables->mode(mode);
    bool noisy = false;
    if (m.type == kCamHeli && ee::lt(0.0f, tables->heli(m.index).noise_amount)) noisy = true;
    if (transition < 2 || noisy) {
        d.mode_change |= 8;
        SetCameraModeByIndex(mode, delay, 4, 0, 0, 0, nullptr);
        return;
    }
    // O PS2 monta um RPlayerCamera na pilha, posto no modo pedido, para achar o ponto de partida da transição (e pede
    // o modo da animação do mundo antes): não portado.
    pending_mode();
    d.mode_change |= 8;
    SetCameraModeByIndex(mode, 0, 4, 0, 0, 0, nullptr);
}

void PlayerCamera::PrevCameraMode(uint16_t delay) {
    const CameraTableGlobals& g = tables->tables;
    int32_t m = d.mode;
    if ((tables->mode(m).selectable == 0 && tables->mode(m).type != kCamCollision) || m == g.cinematic_mode_index)
        return;
    if (g.mode_count == 0) return;  // (o PS2 para na divisão por zero)
    int32_t k = 0;
    for (;;) {
        m = (m - 1) % g.mode_count;
        if (!(k < g.mode_count) || tables->mode(m).selectable != 0) break;
        ++k;
    }
    SetCameraModeByIndex(m, delay, 0, 0, 0, 0, nullptr);
}

void PlayerCamera::CameraLockOn(const float* target, const float offset[4], uint32_t frames, int32_t keep_zoom) {
    d.pitch_rate = 0.0f;
    d.zoom_speed = 1.0f;
    d.yaw_rate = 0.0f;
    d.target_pitch_rate = 0.0f;
    d.target_yaw_rate = 0.0f;
    d.limited = 0;
    d.spin = 0;
    if (target) {
        d.lock_target = nf::p32_u(target);
        std::memcpy(d.lock_offset, offset, 16);
        d.lock_count = int32_t(frames);
        d.lock_total = int32_t(frames);
        if (keep_zoom == 0) {
            if (state.zoom_held == 0) statics.zoom_button = 1;
            state.lock_zoom = 1;
        }
        return;
    }
    if (d.lock_target != 0) {
        d.lock_target = 0;
        std::memcpy(d.lock_offset, kVf0, 16);
        d.lock_total = 0;
        d.lock_count = 0;
        statics.zoom_button = 0;
        state.lock_zoom = 0;
    }
}

void PlayerCamera::ShakeCamera(float radius, int32_t frames, const float centre[4], int32_t hold, int32_t frame_rate) {
    Reg e, c;
    load(e, d.eye);
    load(c, centre);
    sub(e, e, c, XYZ);
    const float d2 = ps2::ffloat(vu_len2(e));
    if (!ee::lt(d2, ee::mul(radius, radius))) return;
    const float s = tables->mode(d.mode).explosion_shake_scale;
    const float a = ee::mul(ee::mul(radius, tables->constants.explosion_scale), s);
    const float b = ee::mul(tables->constants.explosion_max_shake, s);
    const float amount = ee::min(b, a);
    if (ee::lt(d.shake_amount, amount)) d.shake_amount = amount;
    const int32_t t = ee::cvt_w(ee::div(ee::cvt_s(frames * frame_rate), 60.0f));
    d.shaking = 0;
    if (d.shake_time < t) d.shake_time = t;
    if (hold != 0) {
        std::memcpy(statics.shake_eye, d.eye, 16);
        d.shaking = 1;
    }
}

void PlayerCamera::ForceCameraChange(int16_t cam_id, uint16_t delay, uint32_t transition) {
    if (cam_id == 0) {
        SetLastSelectableCameraMode(delay, transition, 0, 1);
        return;
    }
    int32_t found = -1;
    for (int32_t i = 0; i < tables->tables.mode_count; ++i)
        if (tables->mode(i).cam_id == cam_id) {
            found = i;
            break;
        }
    if (found != -1) SetCameraModeByIndex(found, delay, 4, uint16_t(transition & 0xffff), 0, 0, nullptr);
}

void PlayerCamera::EndCameraAnim(const CameraContext& ctx, uint16_t delay, uint32_t transition, uint32_t arg4,
                                 int16_t cam_id) {
    if (state.anim == 0 && state.cinematic == 0) return;
    SetAnchor(ctx.cars.empty() ? nullptr : ctx.cars[0]);
    if (cam_id == 0) {
        SetLastSelectableCameraMode(delay, transition, arg4, 1);
    } else {
        int32_t found = -1;
        for (int32_t i = 0; i < tables->tables.mode_count; ++i)
            if (tables->mode(i).cam_id == cam_id) {
                found = i;
                break;
            }
        if (found == -1) SetLastSelectableCameraMode(delay, transition, arg4, 1);
        else SetCameraModeByIndex(found, delay, 4, uint16_t(transition & 0xffff), 0, 0, nullptr);
    }
    state.anim = 0;
    state.cinematic = 0;
}

void PlayerCamera::EndMissileCamera(const CameraContext& ctx) {
    if (state.colorize == 0) return;
    state.colorize = 0;
    SetAnchor(ctx.cars.empty() ? nullptr : ctx.cars[0]);
    SetLastSelectableCameraMode(0, 0, 0, 1);
    d.mode_change |= 8;
    // (RColorize::SetEnabled(0): o port não tem a cor do míssil)
}

void PlayerCamera::InitWeaponChange(const CameraContext& ctx) {
    if (tables->mode(d.mode).type != kCamAutoDrive) return;
    const int8_t a = FindAutoDriveArmInd(ctx, d.mode);
    if (a == d.ad_arm) return;
    d.mode_change |= 1;
    tables->tables.autodrive_prev_weapon = d.ad_arm;
    d.ad_arm = a;
}

void PlayerCamera::InitSpin(const CameraContext& ctx) {
    const float* m = GetAnchorMatrix4(ctx);
    const float up[3] = {m[4], m[5], m[6]};
    const float k = fb(0x3d32aa3fu);
    d.spin = 0x24;
    d.unknown200[3] = fb(0x3f7fc19fu);
    d.unknown200[0] = ee::mul(up[0], k);
    d.unknown200[1] = ee::mul(up[1], k);
    d.unknown200[2] = ee::mul(up[2], k);
}

void PlayerCamera::WeaponFired(const CameraContext& ctx, int32_t weapon) {
    if (ctx.mission_auto_drive != 0 && weapon == 0x1c) d.weapon_anim_step = ctx.sim->steps;
}

void PlayerCamera::InitTransition(int32_t smooth, const float target[4], const float from[4], int32_t frames) {
    Reg a, b, t;
    load(a, target);
    load(b, from);
    load(t, d.unknown2e0);
    sub(t, a, b, XYZ);
    store(d.unknown2e0, t);
    const float dist = vu_length(t);
    d.unknown2e0[3] = dist;
    if (!ee::lt(0.0f, dist)) {
        d.transition_on = 0;  // ClearTransition
        d.transition_first = 0;
        d.transition = 0;
        return;
    }
    if (frames == 0) frames = tables->constants.default_transition;
    load(t, d.unknown2e0);
    vu_normalize3(t, t);
    store(d.unknown2e0, t);
    d.distance_rate = ee::div(d.unknown2e0[3], ee::cvt_s(frames));
    // [driving] 0x1b9e68 RPlayerCamera::SetTransition(int quadros, char)
    d.transition_on = uint8_t(smooth);
    d.transition_first = 1;
    d.transition = frames;
}

void PlayerCamera::UpdateTransition(float out[4], const float base[4]) {
    d.unknown2e0[3] = ee::sub(d.unknown2e0[3], d.distance_rate);
    d.transition_first = 0;  // [driving] 0x1b9e80 RPlayerCamera::DeincrementTransition
    d.transition -= 1;
    Reg dir, b, o, k = {ps2::fbits(d.unknown2e0[3]), 0, 0, 0};
    load(dir, d.unknown2e0);
    load(b, base);
    load(o, out);
    mulbc(o, dir, k, BX, XYZ);
    add(o, b, o, XYZ);
    store(out, o);
}

void PlayerCamera::ResetStateForAnimation() {
    statics.zoom_button = 0;
    state.anim = 1;
    state.colorize = 0;
}

// ---------------------------------------------------------------- o AutoDrive

const AutoDriveArmInfo& PlayerCamera::ad_arm_info(int32_t i) const {
    static const AutoDriveArmInfo kZero{};
    if (i < 0 || i >= tables->modes.autodrive_arm_count) {
        ++const_cast<PlayerCamera*>(this)->guards;
        return kZero;
    }
    return nf::raw(tables->modes.autodrive_arms)[i];
}

int8_t PlayerCamera::FindAutoDriveArmInd(const CameraContext& ctx, int32_t mode) const {
    if (tables->mode(mode).type != kCamAutoDrive) return 0;
    if (ctx.current_weapon == -1) return 0;
    uint32_t mask[2] = {0, 0};
    const int32_t w = size_t(ctx.current_weapon) < ctx.weapons.size() ? ctx.weapons[size_t(ctx.current_weapon)].weapon : 0;
    const int32_t word = (w > -1 ? w : w + 31) >> 5;
    if (word >= 0 && word < 2) mask[word] = 1u << (uint32_t(w) & 31);
    const AutoDriveArmInfo* arms = nf::raw(tables->modes.autodrive_arms);
    for (int32_t i = 0; i < tables->modes.autodrive_arm_count; ++i)
        for (int k = 0; k < 2; ++k)
            if ((arms[i].weapons[k] & mask[k]) != 0) return int8_t(i);
    return 0;
}

void PlayerCamera::UpdateADWeaponAnims(const CameraContext& ctx) {
    CameraTableGlobals& g = tables->tables;
    CameraModeGlobals& mg = tables->modes;
    AutoDriveArmInfo* arms = nf::raw(mg.autodrive_arms);
    const RigidBody* rb = ctx.cars.empty() ? nullptr : GetRigidBody(*ctx.sim, ctx.cars[0]->body);
    if (!rb) {
        ++guards;
        return;
    }
    float orient[4][4];
    std::memcpy(orient, nf::raw(rb->info)->orient, 64);
    const int32_t steps = ctx.sim->steps;
    auto place = [&](const float mq[4][4]) {
        float m80[4][4];
        std::memcpy(m80, orient, 64);
        m80[3][0] = rb->pos[0];
        m80[3][1] = rb->pos[1];
        m80[3][2] = rb->pos[2];
        m80[3][3] = 1.0f;
        float out[4][4];
        MatrixMultiplyFpu(mq, m80, out);
        std::memcpy(d.matrix, out, 64);
        std::memcpy(d.matrix + 12, d.eye, 16);
        d.matrix_changed = 1;
    };
    if (d.ad_arm == -1) {
        const int8_t a = FindAutoDriveArmInd(ctx, d.mode);
        d.ad_arm = a;
        g.autodrive_prev_weapon = a;
        d.weap_change = 0;
        float mq[4][4];
        VU0_quattom4(ad_arm_info(d.ad_arm).rotation, mq);
        place(mq);
    }
    for (int32_t i = 0; i < mg.autodrive_arm_count; ++i) {
        AutoDriveArmInfo& a = arms[i];
        if (a.has_rest_pos == 0) continue;
        if (ee::eq(a.rest_cur, a.rest_pitch)) continue;
        const float t = ee::div(ee::cvt_s(steps - a.rest_start), ee::mul(a.rest_interpol_fall_scale, a.rest_span));
        if (ee::le(1.0f, t)) {
            a.rest_cur = a.rest_pitch;
            std::memcpy(a.rotation, a.rotation_to, 16);
        } else {
            anim::fast_qslerp(a.rotation_from, a.rotation_to, a.rotation, t);
        }
    }
    if (d.weap_change == 0) return;
    const float len = g.autodrive_weap_trans_length;
    if (ee::lt(fb(0x3a83126fu), ee::abs(len)))
        g.autodrive_weap_interp_time = ee::div(ee::cvt_s(steps - d.weap_change_step), ee::mul(mg.max_weap_trans_time, len));
    else
        g.autodrive_weap_interp_time = 1.0f;
    float mq[4][4];
    if (ee::le(1.0f, g.autodrive_weap_interp_time)) {
        g.autodrive_weap_interp_time = 1.0f;
        d.weap_change = 0;
        VU0_quattom4(ad_arm_info(d.ad_arm).rotation, mq);
    } else {
        float q[4];
        anim::fast_qslerp(g.autodrive_prev_weap_quat, ad_arm_info(d.ad_arm).rotation, q, g.autodrive_weap_interp_time);
        VU0_quattom4(q, mq);
    }
    place(mq);
    d.ad_pitch = ee::neg(cam_asin_turns(d.matrix[9]));
    d.ad_yaw = cam_atan2_turns(d.matrix[8], d.matrix[10]);
}

int32_t PlayerCamera::UpdateADTargetAngles(const CameraContext& ctx, float* out_pitch, float* out_yaw) {
    if (ctx.uber_a40 == 0) return 0;
    int32_t s3 = 0, s5 = 0;
    const float* target = ctx.target;
    const AutoDriveArmInfo& arm = ad_arm_info(d.ad_arm);
    const float k = ee::div(ee::cvt_s(3 - ctx.mission_difficulty), 3.0f);
    float f21 = ee::mul(k, arm.max_autoaim_yaw);
    float f20 = ee::mul(k, arm.max_autoaim_pitch);
    bool have = target != nullptr;
    Reg sp0{};
    float sp20[4] = {};
    if (have) {
        Reg t, e;
        load(t, target);
        load(e, d.eye);
        sub(sp0, t, e, XYZ);
        const float dist = vu_length(sp0);
        const float maxd = tables->constants.max_autoaim_distance;
        if (ee::lt(dist, maxd)) {
            const float s = ee::sqrt(ee::sub(1.0f, ee::div(dist, maxd)));
            f21 = ee::mul(f21, s);
            f20 = ee::mul(f20, s);
            const Reg kd = {ps2::fbits(dist), 0, 0, 0};
            mulbc(sp0, sp0, kd, BX, XYZ);
        } else {
            have = false;
        }
    }
    if (have) {
        Reg t, e;
        load(t, target);
        load(e, d.eye);
        sub(sp0, t, e, XYZ);
        vu_normalize3(sp0, sp0);
        std::memcpy(sp20, d.matrix + 8, 16);
        const float v30[4] = {ps2::ffloat(sp0[0]), sp20[1], ps2::ffloat(sp0[2]), 0.0f};
        Reg r30;
        load(r30, v30);
        const float dot = vu_dot3(r30, sp0);
        const float len = vu_length(r30);
        const float ang = cam_acos_turns(ee::div(dot, len));
        const float sp0y = ps2::ffloat(sp0[1]);
        if (state.target_lock != 0) {
            if (ee::lt(f20, ang)) state.target_lock = 0;
            else *out_pitch = ee::lt(sp20[1], sp0y) ? ee::neg(ang) : ang;
        } else if (ee::lt(ang, f20)) {
            *out_pitch = ee::lt(sp20[1], sp0y) ? ee::neg(ang) : ang;
            s3 = 1;
        }
    } else {
        state.target_lock = 0;
    }
    if (!have) {
        state.target_lock = 0;
    } else if (state.target_lock == 0 && s3 == 0) {
        state.target_lock = 0;
    } else {
        float row2[4];
        std::memcpy(row2, d.matrix + 8, 16);
        const float a = cam_atan2_turns(row2[2], row2[0]);
        const float b = cam_atan2_turns(ps2::ffloat(sp0[2]), ps2::ffloat(sp0[0]));
        const float yaw = ee::sub(a, b);
        if (state.target_lock != 0) {
            if (ee::lt(f21, ee::abs(yaw))) state.target_lock = 0;
            else *out_yaw = yaw;
        } else if (ee::lt(ee::abs(yaw), f21)) {
            *out_yaw = yaw;
            s5 = 1;
        }
    }
    if (state.target_lock != 0) return 1;
    if (s5 == 0) return 0;
    return s3 != 0 ? 1 : 0;
}

int32_t PlayerCamera::LimitPitchYaw(const AutoDriveArmInfo& arm, float* pitch, float* yaw) {
    // A ordem dos desvios do MIPS (os rótulos são os endereços). As tabelas de cinco amostras; o índice -1 lê a palavra
    // anterior da estrutura, como o PS2 (min_pitch[-1] = max_yaw[4], max_pitch[-1] = min_pitch[4]).
    const float* words = reinterpret_cast<const float*>(&arm);
    auto min_yaw = [&](int32_t i) { return words[(0xac >> 2) + i]; };
    auto max_yaw = [&](int32_t i) { return words[(0xc0 >> 2) + i]; };
    auto min_pitch = [&](int32_t i) { return words[(0xd4 >> 2) + i]; };
    auto max_pitch = [&](int32_t i) { return words[(0xe8 >> 2) + i]; };
    int32_t result = 0;
    if (ee::eq(max_pitch(0), 0.0f) && ee::eq(min_pitch(0), 0.0f)) return 0;
    const float f5 = max_yaw(0);
    int32_t limited = 1;
    if (ee::eq(f5, 0.0f) && ee::eq(min_yaw(0), 0.0f)) limited = 0;
    float f25 = 0.0f, f2 = 0.0f;
    float f20 = 0, f21 = 0, f22 = 0, f4 = 0;
    int32_t s5 = 0;
    bool cond = false, in_range = false;
    if (ee::eq(f5, 0.0f)) goto L6e34;
L6d30:
    if (limited != 0) {
        f2 = max_yaw(s5);
        if (ee::eq(min_yaw(s5), f2)) goto L6dac;
    }
    {
        const float f4a = min_yaw(s5), f3 = max_yaw(s5);
        f25 = ee::mul(ee::add(f4a, f3), 0.5f);
        const float y = *yaw;
        cond = ee::lt(y, f25) ? ee::lt(f4a, y) : ee::lt(y, f3);
        goto L6df4;
    }
L6dac:
    if (s5 == 0 && ee::lt(*yaw, f5)) {
        in_range = s5 < 5;
        goto L6e38;
    }
    if (s5 <= 0) goto L6e00;
    {
        const float y = *yaw;
        if (!ee::le(min_yaw(s5 - 1), y)) {
            ++s5;
            goto L6e04;
        }
        cond = ee::lt(y, f2);
    }
L6df4:
    if (cond) {
        in_range = s5 < 5;
        goto L6e38;
    }
L6e00:
    ++s5;
L6e04:
    if (!(s5 < 5)) goto L7134;
    if (!ee::eq(max_yaw(s5), 0.0f)) goto L6d30;
    s5 -= (0 < s5) ? 1 : 0;
L6e34:
    in_range = s5 < 5;
L6e38:
    if (!in_range) goto L7134;
    {
        const int32_t s3 = s5 - 1;
        const float f24 = ee::neg(cam_asin_turns(min_pitch(s5)));
        const float f23 = ee::neg(cam_asin_turns(min_pitch(s3)));
        f20 = ee::neg(cam_asin_turns(max_pitch(s5)));
        const float f7 = ee::neg(cam_asin_turns(max_pitch(s3)));
        if (limited != 0 && ee::eq(min_yaw(s5), max_yaw(s5))) {
            // L6f64
            if (s5 <= 0) {
                f22 = max_yaw(s5);
                f21 = min_yaw(s5);
                f4 = f24;
                goto L7154;
            }
            f22 = max_yaw(s5);
            f21 = min_yaw(s3);
            const float f5b = ee::sub(f22, f21);
            float t = f5b;
            if (!ee::eq(f5b, 0.0f)) {
                t = ee::div(ee::sub(*yaw, f21), f5b);
                if (ee::lt(1.0f, t)) t = 1.0f;
                if (ee::lt(t, 0.0f)) t = 0.0f;
            }
            const float f8 = ee::sub(f24, f23);
            const float f6 = *pitch;
            const float f9 = ee::sub(f20, f7);
            f4 = ee::add(f23, ee::mul(t, f8));
            f20 = ee::add(f7, ee::mul(t, f9));
            if (ee::lt(f4, f6) && ee::lt(ee::abs(f5b), ee::abs(f8))) {
                float f3 = f8;
                if (!ee::eq(f8, 0.0f)) {
                    f3 = ee::div(ee::sub(f6, f23), f8);
                    if (ee::lt(1.0f, f3)) f3 = 1.0f;
                    if (ee::lt(f3, 0.0f)) f3 = 0.0f;
                }
                const float lo = min_yaw(s3);
                const float span = ee::sub(max_yaw(s5), lo);
                f4 = ee::add(f23, ee::mul(f3, f8));
                *yaw = ee::add(lo, ee::mul(f3, span));
            }
            const float f2p = *pitch;
            if (ee::lt(f2p, f20) && ee::lt(ee::abs(f5b), ee::abs(f9))) {
                float f3 = f9;
                if (!ee::eq(f9, 0.0f)) {
                    f3 = ee::div(ee::sub(f2p, f7), f9);
                    if (ee::lt(1.0f, f3)) f3 = 1.0f;
                    if (ee::lt(f3, 0.0f)) f3 = 0.0f;
                }
                const float lo = min_yaw(s3);
                const float span = ee::sub(max_yaw(s5), lo);
                f20 = ee::add(f7, ee::mul(f3, f9));
                *yaw = ee::add(lo, ee::mul(f3, span));
            }
            goto L7154;
        }
        f22 = max_yaw(s5);
        f21 = min_yaw(s5);
        if (s5 <= 0) {
            f4 = f24;
            goto L7154;
        }
        const float y = *yaw;
        float f0, t;
        if (ee::lt(y, f25)) {
            f0 = min_yaw(s3);
            t = ee::sub(f21, f0);
        } else {
            f0 = max_yaw(s3);
            t = ee::sub(f22, f0);
        }
        if (!ee::eq(t, 0.0f)) {
            t = ee::div(ee::sub(y, f0), t);
            if (ee::lt(1.0f, t)) t = 1.0f;
            if (ee::lt(t, 0.0f)) t = 0.0f;
        }
        const float f1 = ee::sub(f20, f7);
        const float f0b = ee::sub(f24, f23);
        f20 = ee::add(f7, ee::mul(t, f1));
        f4 = ee::add(f23, ee::mul(t, f0b));
        goto L7154;
    }
L7134:
    f22 = max_yaw(4);
    f21 = min_yaw(4);
    f20 = ee::neg(cam_asin_turns(max_pitch(4)));
    f4 = ee::neg(cam_asin_turns(min_pitch(4)));
L7154:
    if (limited != 0) {
        if (ee::le(0.5f, f22) && ee::lt(f22, *yaw)) *yaw = ee::sub(*yaw, 1.0f);
        if (ee::le(f21, -0.5f) && ee::lt(*yaw, f21)) *yaw = ee::add(*yaw, 1.0f);
        if (ee::le(f22, *yaw)) {
            *yaw = f22;
            result = 1;
        }
        if (ee::le(*yaw, f21)) {
            *yaw = f21;
            result = 1;
        }
    } else {
        const float y = *yaw;
        if (ee::lt(1.0f, y)) *yaw = ee::sub(y, 1.0f);
        else if (ee::lt(y, -1.0f)) *yaw = ee::add(y, 1.0f);
    }
    if (ee::le(f4, *pitch)) {
        *pitch = f4;
        result = 1;
    }
    if (ee::le(*pitch, f20)) {
        *pitch = f20;
        result = 1;
    }
    return result;
}

void PlayerCamera::UpdateAutoDriveCam(const CameraContext& ctx) {
    CameraTableGlobals& g = tables->tables;
    CameraModeGlobals& mg = tables->modes;
    const int32_t steps = ctx.sim->steps;
    if (ctx.mission_missile != 0) return;
    if (ctx.sim->state == 3) return;
    ++autodrive_updates;
    const float* M = GetAnchorMatrix4(ctx);
    const float* P = GetAnchorPosition(ctx);
    if (d.unknown168 != 0) {
        const AutoDriveArmInfo& a = ad_arm_info(d.ad_arm);
        Reg rel, r{}, p, e;
        load(rel, a.rel_pos);
        vu_xform3(r, rel, M);
        load(p, P);
        load(e, d.eye);
        add(e, r, p, XYZ);
        store(d.eye, e);
        std::memcpy(d.matrix, M, 64);
        std::memcpy(d.matrix + 12, d.eye, 16);
        d.matrix_changed = 1;
        return;
    }
    const int8_t found = FindAutoDriveArmInd(ctx, d.mode);
    const int8_t old = d.ad_arm;
    if (found != old) {
        d.mode_change |= 1;
        g.autodrive_prev_weapon = old;
        d.ad_arm = found;
    }
    const CameraModeInfo& mode = tables->mode(d.mode);
    if (d.ad_arm < 0 || d.ad_arm >= mg.autodrive_arm_count) {
        ++guards;
        return;
    }
    AutoDriveArmInfo& arm = nf::raw(mg.autodrive_arms)[d.ad_arm];
    float f1;
    if (state.zoom_held == 0) {
        d.zoom_fov_max = mode.default_fov;
    } else if (ee::lt(d.zoom_fov_max, arm.min_fov)) {
        d.zoom_fov_max = arm.min_fov;
    }
    f1 = d.zoom_slope;
    float target_fov;
    if (ee::eq(f1, 0.0f)) {
        target_fov = d.zoom_fov_max;
    } else {
        float f = ee::min(mode.default_fov, ee::add(d.fov, f1));
        f = ee::max(f, arm.min_fov);
        if (ee::lt(2.0f, f)) d.fov = f;
        d.zoom_fov_max = d.fov;
        target_fov = d.zoom_fov_max;
    }
    SetCameraZoom(1, target_fov, 1);

    float tmp0[4] = {};   // sp+0x00
    float tmp10[4] = {};  // sp+0x10
    if (d.mode_change & 1) {
        for (int i = 0; i < 3; ++i) tmp0[i] = ee::sub(d.look_at[i], d.look_at_offset[i]);
        const float* M2 = GetAnchorMatrix4(ctx);
        Reg rel, r{};
        load(rel, arm.rel_pos);
        vu_xform3(r, rel, M2);
        std::memcpy(tmp10, r, 16);
        tmp10[3] = 0.0f;
        if (DoSmoothModeChange()) {
            AnchorCamera(ctx, 1, arm.anchor.offset);
            InitTransition(1, d.offset, tmp10, mode.smooth_trans);
        } else if (d.ad_arm != -1) {
            AnchorCamera(ctx, 0, arm.anchor.offset);
            std::memcpy(d.offset, tmp10, 16);
        }
        for (int i = 0; i < 3; ++i) d.eye[i] = ee::add(d.look_at[i], d.offset[i]);
        d.limited = 0;
        d.target_yaw_rate = 0.0f;
        d.pitch_rate = 0.0f;
        d.target_pitch_rate = 0.0f;
        d.yaw_rate = 0.0f;
        d.unknown280 = 0;
        if (d.ad_arm != -1 && g.autodrive_prev_weapon != -1) {
            AutoDriveArmInfo& cur = arm;
            float m60[4][4];
            std::memcpy(m60, M, 64);
            m60[3][0] = P[0];
            m60[3][1] = P[1];
            m60[3][2] = P[2];
            m60[3][3] = 1.0f;
            OrthoInverse(m60);
            float rel_m[4][4];
            MatrixMultiplyFpu(reinterpret_cast<const float(*)[4]>(d.matrix), m60, rel_m);
            VU0_m4toquat(rel_m, g.autodrive_prev_weap_quat);
            if (d.weap_change == 0 && g.autodrive_prev_weapon != -1) {
                if (g.autodrive_prev_weapon < 0 || g.autodrive_prev_weapon >= mg.autodrive_arm_count) {
                    ++guards;
                } else {
                    AutoDriveArmInfo& prev = nf::raw(mg.autodrive_arms)[g.autodrive_prev_weapon];
                    if (prev.preserve_transform != 0) {
                        std::memcpy(prev.rotation, g.autodrive_prev_weap_quat, 16);
                        if (prev.has_rest_pos != 0) {
                            const float a = cam_asin_turns(d.matrix[9]);
                            const float rp = prev.rest_pitch;
                            const float diff = ee::sub(a, rp);
                            prev.rest_cur = a;
                            prev.rest_span = diff;
                            if (ee::lt(diff, fb(0x3a83126fu))) {
                                prev.rest_cur = rp;
                            } else {
                                std::memcpy(prev.rotation_from, g.autodrive_prev_weap_quat, 16);
                                prev.rest_start = steps;
                                const float axis[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // 0x382568
                                float rot[4][4], out[4][4];
                                RCameraMath_BuildRotationMat4(prev.rest_span, rot, axis);
                                VU0_MATRIX4_mult(rot, rel_m, out);
                                VU0_m4toquat(out, prev.rotation_to);
                                prev.rest_span = ee::add(prev.rest_span, prev.rest_span);
                            }
                        }
                    }
                }
            }
            if (cur.preserve_transform != 0) {
                Reg a, b, t{};
                load(a, g.autodrive_prev_weap_quat);
                load(b, cur.rotation);
                mul(t, a, b, XYZW);
                addbc(t, t, t, BY, X);
                addbc(t, t, t, BZ, X);
                addbc(t, t, t, BW, X);
                const float ang = cam_acos_turns(ps2::ffloat(t[0]));
                g.autodrive_weap_trans_length = ee::add(ang, ang);
                d.weap_change = 1;
                d.weap_change_step = steps;
                g.autodrive_weap_interp_time = 0.0f;
            }
        }
        if (d.prev_mode == g.missile_index) {
            d.prev_mode = d.mode;
            g.autodrive_weap_trans_length = 0.0f;
            for (int i = 0; i < 3; ++i) d.matrix[8 + i] = ee::sub(tmp0[i], d.eye[i]);
            float m[4][4];
            std::memcpy(m, d.matrix, 64);
            RCameraMath_VU0_GenerateMatrix4(m[2], m);
            std::memcpy(d.matrix, m, 64);
            d.matrix_changed = 1;
        }
    }
    UpdateADWeaponAnims(ctx);
    if (d.weapon_anim_step != 0) {
        const float t = ee::div(ee::cvt_s(steps - d.weapon_anim_step), tables->constants.weapon_animation_length);
        if (ee::lt(t, 1.0f)) {
            const float s = cam_sin_turns(ee::mul(t, 0.5f));
            d.cursor[1] = ee::add(d.cursor[1], ee::mul(s, tables->constants.weapon_animation_amplitude));
        } else {
            d.weapon_anim_step = 0;
        }
    }
    d.pitch_rate = ee::add(d.pitch_rate, ee::mul(ee::sub(d.target_pitch_rate, d.pitch_rate), mg.inertia));
    d.yaw_rate = ee::add(d.yaw_rate, ee::mul(ee::sub(d.target_yaw_rate, d.yaw_rate), mg.inertia));
    const float zoom = ee::div(d.fov, 33.0f);  // f20
    float add_pitch = 0, add_yaw = 0;          // sp+0, sp+4
    bool simple = arm.allow_deadzone == 0 || state.centering != 0;
    if (!simple) {
        int32_t locked = 0;
        float tgt_pitch = 0.0f, tgt_yaw = 0.0f;  // sp+0x130, sp+0x134
        float f21, f22;
        if (state.zoom_held != 0) {
            f21 = ee::mul(ee::mul(d.pitch_rate, zoom), arm.target_cursor_move_speed);
            f22 = ee::mul(ee::mul(d.yaw_rate, zoom), arm.target_cursor_move_speed);
        } else {
            f21 = ee::mul(ee::mul(d.pitch_rate, zoom), arm.norm_cursor_move_speed);
            f22 = ee::mul(ee::mul(d.yaw_rate, zoom), arm.norm_cursor_move_speed);
            locked = UpdateADTargetAngles(ctx, &tgt_pitch, &tgt_yaw);
        }
        if (ee::lt(ee::mul(statics.prev_rates[1], d.yaw_rate), 0.0f)) d.accel_yaw = 0.0f;
        else if (ee::le(ee::abs(d.yaw_rate), fb(0x3a83126fu))) d.accel_yaw = 0.0f;
        else d.accel_yaw = ee::add(d.accel_yaw, f22);
        float f8 = ee::add(ee::div(ee::abs(d.accel_yaw), 1.75f), fb(0x3e23d70au));
        if (ee::lt(0.5f, f8)) f8 = 0.5f;
        if (ee::lt(ee::mul(statics.prev_rates[0], d.pitch_rate), 0.0f)) d.accel_pitch = 0.0f;
        else if (ee::le(ee::abs(d.pitch_rate), fb(0x3a83126fu))) d.accel_pitch = 0.0f;
        else d.accel_pitch = ee::add(d.accel_pitch, f21);
        float f7 = ee::add(ee::div(ee::abs(d.accel_pitch), 1.25f), fb(0x3da3d70au));
        if (ee::lt(0.5f, f7)) f7 = 0.5f;
        statics.prev_rates[0] = d.pitch_rate;
        statics.prev_rates[1] = d.yaw_rate;
        bool track = false;
        if (locked != 0) {
            if (state.target_lock == 0) state.target_lock = 1;
            track = true;
        } else if (state.target_lock != 0) {
            track = true;
        }
        if (track) {
            const float s = arm.autoaim_interpol_speed;
            d.dz_pitch = ee::add(d.dz_pitch, ee::mul(ee::sub(tgt_pitch, d.dz_pitch), s));
            d.dz_yaw = ee::add(d.dz_yaw, ee::mul(ee::sub(tgt_yaw, d.dz_yaw), arm.autoaim_interpol_speed));
            add_pitch = ee::mul(ee::mul(f7, d.pitch_rate), arm.norm_cursor_end_move_speed);
            add_yaw = ee::mul(ee::mul(f8, d.yaw_rate), arm.norm_cursor_end_move_speed);
        } else if (state.zoom_held != 0) {
            const float f3 = ee::add(d.dz_pitch, f21);
            d.dz_pitch = f3;
            if (ee::lt(ee::mul(arm.max_deadzone_pitch, zoom), ee::abs(f3))) {
                add_pitch = ee::mul(ee::mul(ee::mul(f7, d.pitch_rate), zoom), arm.target_cursor_end_move_speed);
                const float lim = ee::lt(f3, 0.0f) ? ee::neg(arm.max_deadzone_pitch) : arm.max_deadzone_pitch;
                d.dz_pitch = ee::mul(lim, zoom);
            } else {
                d.accel_pitch = 0.0f;
                add_pitch = 0.0f;
            }
            const float f4 = ee::add(d.dz_yaw, f22);
            d.dz_yaw = f4;
            if (ee::lt(ee::mul(arm.max_deadzone_yaw, zoom), ee::abs(f4))) {
                add_yaw = ee::mul(ee::mul(ee::mul(f8, d.yaw_rate), zoom), arm.target_cursor_end_move_speed);
                const float lim = ee::lt(f4, 0.0f) ? ee::neg(arm.max_deadzone_yaw) : arm.max_deadzone_yaw;
                d.dz_yaw = ee::mul(lim, zoom);
            } else {
                d.accel_yaw = 0.0f;
                add_yaw = 0.0f;
            }
        } else {
            add_pitch = ee::mul(ee::mul(ee::mul(f7, d.pitch_rate), zoom), arm.norm_cursor_end_move_speed);
            d.dz_pitch = 0.0f;
            add_yaw = ee::mul(ee::mul(ee::mul(f8, d.yaw_rate), zoom), arm.norm_cursor_end_move_speed);
            d.dz_yaw = 0.0f;
        }
        const float inv360 = fb(0x3b360b62u);
        const float tan_dz_yaw = cam_tan_turns(d.dz_yaw);
        const float tan_fov = cam_tan_turns(ee::mul(ee::mul(d.fov, inv360), 0.5f));
        d.cursor[0] = ee::div(
            ee::mul(ee::mul(ee::mul(ee::cvt_s(ctx.screen_w), fb(0x3efae148u)), 0.5f), tan_dz_yaw), tan_fov);
        const float tan_dz_pitch = cam_tan_turns(d.dz_pitch);
        const float tan_fov2 = cam_tan_turns(ee::mul(ee::mul(d.fov, inv360), 0.5f));
        d.ad_pitch = ee::add(d.ad_pitch, add_pitch);
        d.ad_yaw = ee::add(d.ad_yaw, add_yaw);
        d.cursor[1] = ee::div(
            ee::mul(ee::mul(ee::mul(ee::cvt_s(ctx.screen_h), fb(0x3f1eb852u)), 0.5f), tan_dz_pitch), tan_fov2);
    } else {
        d.dz_pitch = 0.0f;
        d.dz_yaw = 0.0f;
        d.cursor[0] = 0.0f;
        d.cursor[1] = 0.0f;
        d.ad_pitch = ee::add(d.ad_pitch, ee::mul(ee::mul(d.pitch_rate, zoom), arm.norm_cursor_end_move_speed));
        d.ad_yaw = ee::add(d.ad_yaw, ee::mul(ee::mul(d.yaw_rate, zoom), arm.norm_cursor_end_move_speed));
    }
    if (d.spin != 0) {
        d.ad_yaw = ee::add(d.ad_yaw, fb(0x3c638e39u));
        int32_t v1 = 4;
        if (!ee::lt(0.0f, arm.max_yaw[4])) {
            v1 = -1;
            for (int32_t k = 3; k >= 0; --k)
                if (ee::lt(0.0f, arm.max_yaw[k])) {
                    v1 = k;
                    break;
                }
        }
        if (v1 >= 0 && ee::lt(arm.max_yaw[v1], d.ad_yaw)) d.ad_yaw = arm.min_yaw[v1];
        d.spin -= 1;
        if (d.spin < 0) d.spin = 0;
    }
    const float upv[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float rightv[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float* up = upv;
    const float* right = rightv;
    if (arm.lock_arm_to_car != 0) {
        up = M + 4;
        right = M;
    }
    if (d.lock_target != 0) {
        const float* T = nf::p32_ptr<const float>(d.lock_target);
        Reg dir, t, e;
        load(t, T);
        load(e, d.eye);
        std::memset(dir, 0, sizeof dir);
        sub(dir, t, e, XYZ);
        float* df = reinterpret_cast<float*>(dir);
        df[1] = ee::add(df[1], d.lock_offset[1]);
        vu_add_scaled4(dir, M, d.lock_offset[0]);
        vu_add_scaled4(dir, M + 8, d.lock_offset[2]);
        vu_normalize3(dir, dir);
        Reg r30{};
        if (d.lock_count > 0) {
            load(r30, d.matrix + 8);
            const float f = ee::div(ee::cvt_s(d.lock_total - d.lock_count), ee::cvt_s(d.lock_total));
            Reg diff, k = {ps2::fbits(f), 0, 0, 0};
            sub(diff, dir, r30, XYZ);
            mulbc(diff, diff, k, BX, XYZ);
            add(dir, r30, diff, XYZ);
            vu_normalize3(dir, dir);
            d.lock_count -= 1;
        }
        Reg u, side = {};
        std::memcpy(side, r30, 16);
        load(u, up);
        vu_cross(side, u, dir);
        vu_normalize3(side, side);
        Reg rr;
        load(rr, right);
        const float k08 = fb(0x3f4ccccdu);
        const float a1 = ee::neg(cam_acos_turns(vu_dot3(rr, side)));
        d.ad_yaw = ee::add(d.ad_yaw, ee::mul(ee::sub(a1, d.ad_yaw), k08));
        Reg r50{};
        vu_cross(r50, side, u);
        float f2 = cam_acos_turns(vu_dot3(r50, dir));
        if (ee::lt(ps2::ffloat(r50[1]), ps2::ffloat(dir[1]))) f2 = ee::neg(f2);
        d.dz_pitch = 0.0f;
        d.dz_yaw = 0.0f;
        d.cursor[0] = 0.0f;
        d.cursor[1] = 0.0f;
        d.ad_pitch = ee::add(d.ad_pitch, ee::mul(ee::sub(f2, d.ad_pitch), k08));
        if (LimitPitchYaw(arm, &d.ad_pitch, &d.ad_yaw) != 0) SetCameraZoom(2, 0.0f, 0x1e);
    }
    d.limited = LimitPitchYaw(arm, &d.ad_pitch, &d.ad_yaw);
    if (d.limited != 0 && ctx.audio_turn) {
        ctx.audio_turn[0] = 0;
        ctx.audio_turn[1] = 0;
    }
    float rot[4][4];
    RCameraMath_BuildRotationMat4(d.ad_yaw, rot, up);
    {
        Reg v, r;
        load(v, right);
        load(r, d.matrix);
        vu_xform3(r, v, &rot[0][0]);
        store(d.matrix, r);
    }
    RCameraMath_BuildRotationMat4(d.ad_pitch, rot, d.matrix);
    {
        Reg v, r;
        load(v, up);
        load(r, d.matrix + 4);
        vu_xform3(r, v, &rot[0][0]);
        store(d.matrix + 4, r);
    }
    {
        Reg r0, r1, r2;
        load(r0, d.matrix);
        load(r1, d.matrix + 4);
        load(r2, d.matrix + 8);
        vu_cross(r2, r0, r1);
        store(d.matrix + 8, r2);
    }
    d.matrix[3] = 0.0f;
    d.matrix[7] = 0.0f;
    d.matrix[11] = 0.0f;
    if (arm.allow_deadzone != 0 && state.centering == 0) {
        float r60[4][4];
        RCameraMath_BuildRotationMat4(d.dz_pitch, r60, d.matrix);
        {
            Reg v, f;
            load(v, d.matrix + 8);
            load(f, d.forward);
            vu_xform3(f, v, &r60[0][0]);
            store(d.forward, f);
        }
        RCameraMath_BuildRotationMat4(d.dz_yaw, r60, d.matrix + 4);
        {
            Reg v, f;
            load(v, d.forward);
            load(f, d.forward);
            vu_xform3(f, v, &r60[0][0]);
            store(d.forward, f);
        }
    } else {
        std::memcpy(d.matrix_copy, d.matrix, 64);
        std::memcpy(d.forward, d.matrix + 8, 16);
    }
    if ((d.mode_change & 1) == 0) {
        Reg rel, s60{};
        load(rel, arm.rel_pos);
        vu_xform3(s60, rel, M);
        float sp60[4];
        store(sp60, s60);
        if (d.transition > 0 && d.transition_on == 1) UpdateTransition(d.offset, sp60);
        else std::memcpy(d.offset, sp60, 16);
        for (int i = 0; i < 3; ++i) d.eye[i] = ee::add(d.offset[i], P[i]);
    }
    {
        Reg rel, s60{};
        load(rel, arm.rel_pos);
        vu_xform3(s60, rel, d.matrix);
        Reg s70;
        load(s70, P);
        vu_add_scaled4(s70, M, arm.anchor.offset[0]);
        vu_add_scaled4(s70, M + 4, arm.anchor.offset[1]);
        vu_add_scaled4(s70, M + 8, arm.anchor.offset[2]);
        Reg e;
        load(e, d.eye);
        add(e, s70, s60, XYZ);
        store(d.eye, e);
        d.eye[3] = 1.0f;
        std::memcpy(d.matrix + 12, d.eye, 16);
        d.matrix_changed = 1;
    }
    const float k25 = fb(0x3b23d70au);
    if (ee::lt(k25, ee::abs(d.pitch_rate)) || ee::lt(k25, ee::abs(d.yaw_rate))) state.centering = 0;
    if (state.centering != 0) {
        const float f1c = ee::sub(1.0f, tables->constants.centering_speed);
        d.ad_pitch = ee::mul(d.ad_pitch, f1c);
        d.ad_yaw = ee::mul(d.ad_yaw, f1c);
        const float small = fb(0x38d1b717u);
        if (ee::lt(ee::abs(d.ad_pitch), small)) state.centering = 0;
        else if (ee::lt(ee::abs(d.ad_yaw), small)) state.centering = 0;
    }
    const float a[4] = {M[8], 0.0f, M[10], 0.0f};
    const float b[4] = {d.matrix[8], 0.0f, d.matrix[10], 0.0f};
    d.car_angle = RCameraMath_FindAngleBetweenVectors(a, b);
    std::memcpy(d.velocity, GetAnchorLinearVelocity(ctx), 16);
    for (int i = 0; i < 3; ++i) d.offset[i] = ee::sub(d.eye[i], d.look_at[i]);
    Reg dl, e, l;
    load(e, d.eye);
    load(l, d.look_at);
    std::memcpy(dl, e, 16);
    sub(dl, e, l, XYZ);
    d.distance = vu_length(dl);
}

// ---------------------------------------------------------------- a entrada

void PlayerCamera::CameraInputCallback(const CameraContext& ctx, int32_t action, float value, int32_t config_type) {
    ++input_actions;
    if (config_type == 1) AutoDriveCamInputHandler(ctx, action, value);
    else pending_mode();  // [driving] 0x1bddc8 RPlayerCamState::DriveCamInputHandler
}

void PlayerCamera::AutoDriveCamInputHandler(const CameraContext& ctx, int32_t action, float value) {
    state.paused = ctx.sim->state == 3 ? 1u : 0u;
    if (ctx.mission_state == 4) return;
    const bool active = ctx.mission_missile == 0;  // [driving] 0x1be6f8 (o SYM sugere IsDriveMissileOn: sem míssil)
    auto pick = [](float a, float b) { return ee::le(ee::abs(b), ee::abs(a)) ? a : b; };
    auto audio = [&](int k, int32_t v) {
        if (ctx.audio_turn) ctx.audio_turn[k] = v;
    };
    switch (action) {
        case 0x21:
        case 0x22:
        case 0x23:
            if (!active || state.paused != 0) return;
            InitWeaponChange(ctx);
            return;
        case 0x2c:
        case 0x2f: {
            if (action == 0x2f && state.zoom_held != 0 && state.unknown30 != 0) return;
            if (!active) return;
            float& slot = action == 0x2c ? state.rot_x : state.rot_x2;
            slot = value;
            if (!ee::eq(value, 0.0f)) state.centering = 0;
            const float v = action == 0x2c ? pick(state.rot_x, state.rot_x2) : pick(state.rot_x2, state.rot_x);
            const int32_t r = SetAutoDriveRotationY(v);
            audio(0, (r == 0 && !ee::eq(v, 0.0f)) ? 1 : 0);
            return;
        }
        case 0x2d:
        case 0x2e:
        case 0x30:
        case 0x31: {
            const bool second = action >= 0x30;
            if (second && state.zoom_held != 0 && state.unknown30 != 0) return;
            if (!active) return;
            float& slot = second ? state.rot_y2 : state.rot_y;
            slot = value;
            if (!ee::eq(value, 0.0f)) state.centering = 0;
            const float v = second ? pick(state.rot_y2, state.rot_y) : pick(state.rot_y, state.rot_y2);
            const bool negate = action == 0x2d || action == 0x30;
            const int32_t r = SetAutoDriveRotationX(negate ? ee::neg(v) : v);
            audio(1, (r == 0 && !ee::eq(v, 0.0f)) ? 1 : 0);
            return;
        }
        case 0x32:
            if (ctx.mission_cinematic != 0) return;
            if (dafi_strcasecmp(ctx.track.c_str(), "snow1a_mis3") != 0) return;  // (o nome da pista do bloco da troca)
            audio(0, 1);
            if (!active) return;
            if (state.paused != 0) return;
            InitSpin(ctx);
            return;
        case 0x33:
            if (active && state.paused == 0) state.centering = 1;
            audio(0, 0);
            return;
        case 0x34:
        case 0x35:
        case 0x36: {
            if (state.zoom_held == 0 || state.paused != 0 || !active || state.lock_zoom != 0) return;
            float v;
            if (action == 0x34) v = ee::neg(value);
            else v = ee::lt(0.5f, value) ? (action == 0x35 ? -1.0f : 1.0f) : 0.0f;
            SetAutoDriveZoom(v);
            return;
        }
        case 0x38:
            if (state.zoom_held == 0) {
                const bool skip = ctx.mission_5ec != 0 && state.lock_zoom == 0;
                if (!skip) statics.zoom_button = 1;
                return;
            }
            if (ctx.uber_a44 == 0) return;
            if (state.lock_zoom == 0) statics.zoom_button = 0;
            return;
        case 0x3a:
            if (ctx.uber_a44 != 0) return;
            if (state.lock_zoom == 0) statics.zoom_button = 0;
            return;
        default:
            return;
    }
}

// ---------------------------------------------------------------- as câmeras animadas

bool PlayerCamera::LoadSingleAnimation(const uint8_t* cams) {
    anim_handle.reset();
    d.anim_handle = 0;
    if (!cams) return false;
    auto h = std::make_unique<CameraAnimHandle>();
    // [driving] 0x1a1bf0 RAnimEngine::Handle::Create e 0x1a1f58 Handle::Handle, para um registro sem animação: o tamanho
    // (0x40 por instância), a contagem 1 e a cópia do registro (com a regra da bandeira 0x10 sem estados de animação).
    const uint32_t size = 0x40;
    std::memcpy(h->bytes + 0x00, &size, 4);
    h->bytes[0x1c] = 1;
    std::memcpy(h->bytes + 0x40, cams, 0x40);
    if (h->bytes[0x4c] & 0x10) {
        h->bytes[0x4d] = 0xff;
        h->bytes[0x4e] = 0;
        h->bytes[0x4f] = 0;
        h->bytes[0x4c] = uint8_t(h->bytes[0x4c] & 0xef);
    }
    uint32_t anim = 0;
    std::memcpy(&anim, cams + 0x1c, 4);
    if (anim != 0) pending_mode();  // a animação de verdade (o RAnimEngine): não portada
    anim_handle = std::move(h);
    d.anim_handle = 1;
    d.anim_system = 0;  // GetInstanceSystemID(0): sem animação, 0
    return true;
}

bool PlayerCamera::PlayCurrentAnimation() {
    if (!anim_handle) return false;
    pending_mode();  // ProcessStimuli + IsSystemPlaying do RAnimEngine
    return false;
}

void PlayerCamera::TriggerAnimationCamera(const CameraContext& ctx, const uint8_t* cams, uint16_t transition,
                                          uint16_t delay, int32_t distribute, int32_t fov) {
    if (!cams) return;
    const float f = ee::cvt_s(fov);
    d.zoom_fov_target = 33.0f;
    d.fov = 33.0f;
    d.zoom_fov = 33.0f;
    if (ee::lt(2.0f, f)) d.zoom_fov_target = f;
    d.spin = 0;
    ResetStateForAnimation();
    d.zoom_slope = 0.0f;
    d.zoom_speed = 1.0f;
    d.pitch_rate = 0.0f;
    d.yaw_rate = 0.0f;
    d.target_pitch_rate = 0.0f;
    d.target_yaw_rate = 0.0f;
    d.limited = 0;
    tables->tables.autodrive_weap_trans_length = 0.0f;
    EndMissileCamera(ctx);
    SetCameraModeByIndex(tables->tables.world_anim_index, delay, 4, transition, 0, nf::p32_u(cams), nullptr);
    uint32_t anim = 0;
    std::memcpy(&anim, cams + 0x1c, 4);
    if (anim != 0) return;
    if (!spline) spline = std::make_unique<PlayerCameraSpline>();
    spline->AddToSplinePtList(reinterpret_cast<const float*>(cams + 0x30));
    if (distribute != 0 && spline->GetPointListSize() >= 2) {
        uint32_t total_frames = 0;
        float total_dist = 0.0f;
        const float* prev = d.eye;
        const int32_t n = tables->tables.mode_count;
        auto is_static = [&](const DirectorQueueData& q, uint8_t*& rec) {
            if (!(int32_t(q.mode) <= n)) {
                ++guards;
                return false;
            }
            if (tables->mode(q.mode).type != kCamAnimation) return false;
            rec = nf::p32_ptr<uint8_t>(q.data18);
            if (!rec) return false;
            uint32_t a = 0;
            std::memcpy(&a, rec + 0x1c, 4);
            return a == 0;
        };
        for (DirectorQueueData& q : director_queue) {
            uint8_t* rec = nullptr;
            if (!is_static(q, rec)) continue;
            Reg a, b;
            load(a, rec + 0x30);
            load(b, prev);
            sub(a, a, b, XYZ);
            const float dist = vu_length(a);
            std::memcpy(rec + 0x2c, &dist, 4);
            total_dist = ee::add(total_dist, dist);
            total_frames += q.transition;
            prev = reinterpret_cast<const float*>(rec + 0x30);
        }
        if (total_frames != 0 && !ee::eq(total_dist, 0.0f)) {
            const float inv = ee::div(1.0f, total_dist);
            for (DirectorQueueData& q : director_queue) {
                uint8_t* rec = nullptr;
                if (!is_static(q, rec)) continue;
                float dist;
                std::memcpy(&dist, rec + 0x2c, 4);
                const float f1 = ee::mul(dist, inv);
                q.transition = uint16_t(ee::cvt_w(ee::mul(f1, u2f(total_frames))));
            }
        }
    }
    d.last_step += 1;
    update(ctx);
}

void PlayerCamera::UpdateWorldAnimationCam(const CameraContext& ctx) {
    if (ctx.sim->state == 3) return;
    if (!anim_handle) return;
    ++anim_updates;
    uint8_t* rec = anim_handle->bytes + 0x40;  // s4
    float old_eye[4];
    std::memcpy(old_eye, d.eye, 16);
    const float* P = GetAnchorPosition(ctx);
    for (int i = 0; i < 3; ++i) d.look_at[i] = ee::add(P[i], d.look_at_offset[i]);
    uint32_t anim = 0;
    std::memcpy(&anim, rec + 0x1c, 4);
    if (!spline) spline = std::make_unique<PlayerCameraSpline>();
    if (d.mode_change & 1) {
        if (d.transition_override == 0) {
            if (anim != 0) PlayCurrentAnimation();
        } else {
            float* p0;
            if (rec[0xc] & 1) {
                for (int i = 0; i < 3; ++i) d.offset[i] = ee::sub(d.eye[i], d.look_at[i]);
                p0 = d.offset;
            } else {
                p0 = d.eye;
            }
            float m10[4][4];
            float dir1[4] = {0, 0, 0, 0};
            if (anim != 0) {
                pending_mode();  // a primeira chave da animação (VU0_quattom4 do quatérnio da chave): não portada
                std::memcpy(m10, kVf0, 16);
                std::memcpy(m10[1], kVf0, 16);
                std::memcpy(m10[2], kVf0, 16);
                std::memcpy(m10[3], kVf0, 16);
            } else {
                std::memcpy(m10, rec, 64);
                m10[0][3] = 0.0f;
                m10[1][3] = 0.0f;
                m10[2][3] = 0.0f;
                m10[3][3] = 1.0f;
            }
            float* p1 = m10[3];
            float dir0[4];
            std::memcpy(dir0, d.velocity, 16);
            {
                Reg a, b, t{};
                load(a, p1);
                load(b, p0);
                sub(t, a, b, XYZ);
                Reg v;
                load(v, dir0);
                if (ee::lt(vu_dot3(v, t), 0.0f)) {
                    const Reg k = {0xbf800000u, 0, 0, 0};
                    mulbc(v, v, k, BX, XYZ);
                    store(dir0, v);
                }
                dir0[1] = ee::abs(dir0[1]);
            }
            float q0[4], q1[4];
            VU0_m4toquat(reinterpret_cast<const float(*)[4]>(d.matrix), q0);
            VU0_m4toquat(m10, q1);
            spline->BuildSpline(fb(0x3ea8f5c3u), p0, dir0, p1, dir1, q0, q1);
            d.transition = d.transition_override;
            d.unknown2e0[3] = 0.0f;
            d.distance_rate = ee::div(1.0f, u2f(uint32_t(d.transition_override)));
        }
    }
    int32_t done;
    if (d.transition > 0) {
        d.unknown2e0[3] = ee::add(d.unknown2e0[3], d.distance_rate);
        float m[4][4];
        std::memcpy(m, d.matrix, 64);
        spline->EvaluateSpline(d.unknown2e0[3], m, 1);
        std::memcpy(d.matrix, m, 64);
        const float t = d.unknown2e0[3];
        const float fov = ee::add(ee::mul(d.zoom_fov, ee::sub(1.0f, t)), ee::mul(d.zoom_fov_target, t));
        d.matrix_changed = 1;
        if (ee::lt(2.0f, fov)) d.fov = fov;
        d.transition -= 1;
        if (d.transition == 0 && anim != 0) {
            done = PlayCurrentAnimation() ? 1 : 0;
        } else if (d.transition > 0) {
            done = 1;
        } else {
            done = spline->GetPointListSize() > 0 ? 0 : 1;
        }
    } else if (anim != 0) {
        pending_mode();  // RWorldCamera::UpdateAnimationCam (a animação de verdade)
        done = 0;
        d.matrix_changed = 1;
        if (ee::lt(2.0f, d.zoom_fov_target)) d.fov = d.zoom_fov_target;
    } else {
        done = 0;
    }
    if (done == 1) {
        director_flags |= 1;
        if (rec[0xc] & 1) {
            std::memcpy(d.offset, d.matrix + 12, 16);
            for (int i = 0; i < 3; ++i) d.matrix[12 + i] = ee::add(d.look_at[i], d.offset[i]);
            d.matrix_changed = 1;
        } else {
            for (int i = 0; i < 3; ++i) d.offset[i] = ee::sub(d.matrix[12 + i], d.look_at[i]);
        }
    } else {
        director_flags &= 0xfe;
        if (rec[0xc] & 1) {
            for (int i = 0; i < 3; ++i) d.matrix[12 + i] = ee::add(d.offset[i], d.look_at[i]);
            d.matrix_changed = 1;
            Reg o;
            load(o, d.offset);
            const float dist = vu_length(o);
            d.distance = dist;
            for (int i = 0; i < 3; ++i) d.look_at[i] = ee::add(d.matrix[12 + i], ee::mul(d.matrix[8 + i], dist));
            const float* P2 = GetAnchorPosition(ctx);
            for (int i = 0; i < 3; ++i) d.look_at_offset[i] = ee::sub(d.look_at[i], P2[i]);
        } else {
            for (int i = 0; i < 3; ++i) d.offset[i] = ee::sub(d.matrix[12 + i], d.look_at[i]);
        }
    }
    const float rate = ee::cvt_s(ctx.sim->frame_rate);
    for (int i = 0; i < 3; ++i) d.velocity[i] = ee::mul(ee::sub(old_eye[i], d.eye[i]), rate);
    std::memcpy(d.eye, d.matrix + 12, 16);
}

bool PlayerCamera::LoadAISplinePathAnimation(uint32_t ai_spline) {
    const AISplineRec* rec = nf::p32_ptr<const AISplineRec>(ai_spline);
    AISplinePath* path = nf::p32_ptr<AISplinePath>(d.ai_spline);
    if (!path) {
        path = new AISplinePath();
        AISplinePath_Construct(*path, rec);
        d.ai_spline = nf::p32_u(path);
    } else {
        AISplinePath_Reset(*path, rec);
    }
    if (RPathHandle* h = nf::p32_ptr<RPathHandle>(path->handle)) RPathHandle_SetNextPath(*h, 0);
    return true;
}

void PlayerCamera::TriggerAIPathAnimationCamera(const CameraContext& ctx, uint32_t ai_spline, uint16_t transition,
                                                uint16_t delay, int32_t fov) {
    if (fov <= 0) {
        d.fov = 33.0f;
    } else {
        const float f = ee::cvt_s(fov);
        if (ee::lt(2.0f, f)) d.fov = f;
    }
    d.spin = 0;
    d.zoom_fov_target = 33.0f;
    d.zoom_fov = 33.0f;
    ResetStateForAnimation();
    d.zoom_slope = 0.0f;
    d.zoom_speed = 1.0f;
    d.pitch_rate = 0.0f;
    d.yaw_rate = 0.0f;
    d.target_pitch_rate = 0.0f;
    d.target_yaw_rate = 0.0f;
    d.limited = 0;
    tables->tables.autodrive_weap_trans_length = 0.0f;
    EndMissileCamera(ctx);
    DirectorQueueData q{};
    std::memcpy(q.vector, kVf0, 16);
    q.delay = delay;
    q.mode = uint16_t(tables->tables.ai_path_anim_index);
    q.transition = transition;
    q.flags = 4;
    q.data1c = ai_spline;
    AppendDirectorData(q);
    d.last_step += 1;
    update(ctx);
}

void PlayerCamera::UpdateAIPathAnimationCam(const CameraContext& ctx) {
    if (ctx.sim->state == 3) return;
    AISplinePath* path = nf::p32_ptr<AISplinePath>(d.ai_spline);
    if (!path) return;
    RPathHandle* h = nf::p32_ptr<RPathHandle>(path->handle);
    if (!h) {
        ++guards;
        return;
    }
    ++ai_path_updates;
    float m[4][4];
    RPathHandle_GetOrientMat(*h, m);
    const float* pos = RPathHandle_GetPosition(*h);
    m[3][0] = pos[0];
    m[3][1] = pos[1];
    m[3][2] = pos[2];
    VU0_MATRIX4_mult(m, path->apply, m);
    GetAnchorMatrix4(ctx);
    AnchorCamera(ctx, 0, tables->modes.anim_anchor.offset);
    d.offset[0] = 0.0f;
    d.offset[1] = 0.0f;
    d.offset[2] = 0.0f;
    d.offset[3] = 1.0f;
    std::memcpy(d.eye, m[3], 16);
    std::memcpy(d.look_at, d.eye, 16);
    std::memcpy(d.matrix, m, 64);
    d.matrix[13] = d.eye[1];
    d.matrix[12] = d.eye[0];
    d.matrix[14] = d.eye[2];
    d.matrix[15] = 1.0f;
    d.matrix_changed = 1;
    d.matrix[3] = 0.0f;
    d.matrix[7] = 0.0f;
    d.matrix[11] = 0.0f;
    d.velocity[0] = h->velocity[0];
    d.velocity[1] = h->velocity[1];
    d.velocity[2] = h->velocity[2];
}

}  // namespace drv
