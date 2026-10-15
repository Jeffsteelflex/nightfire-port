// A câmera do jogador (driving/camera/player_camera.h), portada do MIPS do DRIVING.ELF. As contas da VU0 em modo macro
// (vmul/vmadd/vopmula/vrsqrt) e da FPU seguem a ordem das instruções com a aritmética do PS2 (platform/ps2_float.h,
// vu0_macro.h), para dar os mesmos bits que o original no nf_driving_check (parte 8).

#include "driving/camera/player_camera.h"

#include <atomic>
#include <cstring>

#include "driving/game/explosion.h"
#include "driving/game/rail.h"
#include "driving/real/vu0_math.h"

#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
namespace vu = ps2::vu;

namespace {

std::atomic<uint64_t> g_camera_pending{0};
void pending() { g_camera_pending.fetch_add(1); }

float fb(uint32_t b) { return ps2::ffloat(b); }

// (x x + y y) + z z: a soma da VU (vmul.xyz, vmulax.x, vmadday.x e vmaddz.x com 1,0) e a da FPU, os mesmos bits.
float len2(const float v[3]) { return ee::add(ee::add(ee::mul(v[0], v[0]), ee::mul(v[1], v[1])), ee::mul(v[2], v[2])); }
float vlen(const float v[3]) { return ee::sqrt(len2(v)); }
void normalize(float v[3]) {
    const float q = ee::rsqrt(1.0f, len2(v));
    v[0] = ee::mul(v[0], q);
    v[1] = ee::mul(v[1], q);
    v[2] = ee::mul(v[2], q);
}
// a x b pela VU (vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a); só o xyz de out.
void vcross(const float a[3], const float b[3], float out[3]) {
    vu::Reg ra{}, rb{}, acc{}, d{};
    std::memcpy(ra, a, 12);
    std::memcpy(rb, b, 12);
    vu::opmula(acc, ra, rb);
    vu::opmsub(d, acc, rb, ra);
    std::memcpy(out, d, 12);
}
// v * M pelas linhas 0..2 (vmulax.xyz ACC, m0, v; vmadday.xyz ACC, m1, v; vmaddz.xyz d, m2, v): só o xyz de out.
void xform3(const float v[3], const float* m, float out[3]) {
    float r[3];
    for (int j = 0; j < 3; ++j)
        r[j] = ee::add(ee::add(ee::mul(m[j], v[0]), ee::mul(m[4 + j], v[1])), ee::mul(m[8 + j], v[2]));
    std::memcpy(out, r, 12);
}
// a += (b - a) * k no xyz.
void lerp3(float a[3], const float b[3], float k) {
    for (int i = 0; i < 3; ++i) a[i] = ee::add(a[i], ee::mul(ee::sub(b[i], a[i]), k));
}

const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
const float kNoPosition[4] = {0, 0, 0, 1};  // 0x334250
const float kNoVelocity[4] = {0, 0, 0, 1};  // 0x334260

// Os quatro bytes de RigidBody depois de pos e de vel (o lq do PS2 lê 16): a posição e a velocidade como vetores.
const float* rb_pos(const RigidBody* rb) { return &rb->pos[0]; }
const float* rb_vel(const RigidBody* rb) { return &rb->vel[0]; }

}  // namespace

uint64_t camera_modes_pending();
uint64_t camera_pending() { return g_camera_pending.load() + camera_modes_pending() + camera_math_pending(); }

// ---------------------------------------------------------------- o ruído

namespace {
uint32_t g_random[6] = {0xf22d0e56u, 0x883126e9u, 0, 0, 0, 0};  // 0x343810 (o srandom(0) do .data)
uint32_t g_noise_lcg = 0x3ade68b1u;          // 0x32f3a0
const uint32_t kNoiseLcgMul = 0x075bcd15u;   // 0x32f3a4
int16_t g_noise_perm[0x1000];                // 0x32f4e8
float g_noise_grad[0x10b];                   // 0x3d08a8
bool g_noise_ready = false;
}  // namespace

const int16_t* noise_perm_table() { return g_noise_perm; }
const float* noise_grad_table() { return g_noise_grad; }

// [driving] 0x25a684 srandom
void ps2_srandom(uint32_t seed) {
    uint32_t a = seed + 0xf22d0e56u;
    g_random[0] = a;
    a += 0x96041893u;
    g_random[1] = a;
    a += 0x3df3b646u;
    g_random[2] = a;
    a += 0x40dde76du;
    g_random[3] = a;
    a += 0x97327ae1u;
    g_random[4] = a;
    a += 0xd1a9fbe7u;
    g_random[5] = a;
}

// [driving] 0x25a5ac random: a soma em cadeia das seis palavras (com os vai-uns) e o contador de 192 bits.
uint32_t ps2_random() {
    uint32_t t0 = g_random[0], t1 = g_random[1], t2 = g_random[2], t3 = g_random[3], t4 = g_random[4],
             t5 = g_random[5];
    t4 += t5;
    uint32_t c = t4 < t5;
    uint32_t c2;
    t3 += t4;
    c2 = t3 < t4;
    t3 += c;
    c = t3 < c;
    c += c2;
    t2 += t3;
    c2 = t2 < t3;
    t2 += c;
    c = t2 < c;
    c += c2;
    t1 += t2;
    c2 = t1 < t2;
    t1 += c;
    c = t1 < c;
    c += c2;
    t0 += t1;
    t0 += c;
    if (++t5 == 0 && ++t4 == 0 && ++t3 == 0 && ++t2 == 0 && ++t1 == 0) ++t0;
    g_random[0] = t0;
    g_random[1] = t1;
    g_random[2] = t2;
    g_random[3] = t3;
    g_random[4] = t4;
    g_random[5] = t5;
    return t0;
}

// Divergência: o noise_init (0x17d930, as tabelas de outro ruído pelo rand()) que Noise::Init chama primeiro não foi
// portado: o Noise1 não as lê.
void Noise_Init() {
    const uint32_t saved = ps2_random();
    ps2_srandom(0x3039);
    for (int i = 0; i < 0x10b; ++i) {
        const uint32_t v = g_noise_lcg * kNoiseLcgMul;
        const int32_t hi = int32_t((v & 0x00ffff00u) >> 8);
        g_noise_lcg = v & 0xffffu;
        float f = ee::sub(ee::mul(ee::cvt_s(hi), fb(0x37800000u)), 0.5f);
        g_noise_grad[i] = ee::add(f, f);
    }
    for (int i = 0xfff; i >= 0; --i) g_noise_perm[i] = int16_t(i);
    for (int i = 0xfff; i >= 0; --i) {
        const uint32_t r = ps2_random() & 0xfff;
        const int16_t a = g_noise_perm[i];
        g_noise_perm[i] = g_noise_perm[r];
        g_noise_perm[r] = a;
    }
    ps2_srandom(saved);
    g_noise_ready = true;
}

// [driving] 0x17c788 Noise::Noise1
float Noise_Noise1(float x) {
    if (!g_noise_ready) Noise_Init();
    const float t = ee::sub(x, fb(0xc61c4000u));  // x + 10000
    const int32_t i0 = ee::cvt_w(t);
    const int32_t i1 = i0 + 1;
    // __floatdisf: o inteiro de 64 bits em float (os valores cabem em 32: o cvt.s.w dá o mesmo).
    const float fi = ee::cvt_s(i0);
    float s = ee::mul(ee::sub(t, fi), ee::sub(t, fi));
    s = ee::mul(s, ee::sub(3.0f, ee::add(ee::sub(t, fi), ee::sub(t, fi))));
    const float w0 = ee::sub(1.0f, s);
    const float r0 = ee::sub(t, fi);
    const float r1 = ee::sub(t, ee::cvt_s(i1));
    const int p0 = g_noise_perm[g_noise_perm[i0 & 0xfff] & 0xfff] & 0xff;
    const int p1 = g_noise_perm[g_noise_perm[i1 & 0xfff] & 0xfff] & 0xff;
    // As duas parcelas: grad[p] * 0,5 + grad[p + 1] * r.
    const float a = ee::add(ee::mul(g_noise_grad[p0], 0.5f), ee::mul(g_noise_grad[p0 + 1], r0));
    const float b = ee::add(ee::mul(g_noise_grad[p1], 0.5f), ee::mul(g_noise_grad[p1 + 1], r1));
    return ee::add(ee::mul(w0, a), ee::mul(s, b));
}

// ---------------------------------------------------------------- PointInTriangle

namespace {
// [driving] 0x1b8f80 PointDir: o produto vetorial 2D no plano do eixo (2: xy, 0: yz, 1: zx).
float point_dir(int axis, const float p[4], const float a[4], const float b[4]) {
    int u, v;
    if (axis == 2) {
        u = 0;
        v = 1;
    } else if (axis == 1) {
        u = 2;
        v = 0;
    } else {
        u = 1;
        v = 2;
    }
    const float f2 = ee::mul(ee::sub(p[v], b[v]), ee::sub(a[u], b[u]));
    const float f0 = ee::mul(ee::sub(a[v], b[v]), ee::sub(p[u], b[u]));
    return ee::sub(f0, f2);
}
}  // namespace

bool CameraPointInTriangle(const float p[4], const float a[4], const float b[4], const float c[4]) {
    const float e1[3] = {ee::sub(a[0], b[0]), ee::sub(a[1], b[1]), ee::sub(a[2], b[2])};
    const float e2[3] = {ee::sub(c[0], b[0]), ee::sub(c[1], b[1]), ee::sub(c[2], b[2])};
    float n[3];
    vcross(e1, e2, n);
    int axis;
    if (ee::lt(n[0], n[1])) axis = ee::lt(n[1], n[2]) ? 2 : 1;
    else axis = ee::lt(n[0], n[2]) ? 2 : 0;
    const float d1 = point_dir(axis, p, a, b);
    if (ee::le(d1, 0.0f)) {
        if (!ee::le(point_dir(axis, p, b, c), 0.0f)) return false;
        return ee::le(point_dir(axis, p, c, a), 0.0f);
    }
    if (!ee::le(0.0f, point_dir(axis, p, b, c))) return false;
    return ee::le(0.0f, point_dir(axis, p, c, a));
}

// ---------------------------------------------------------------- RWorldCamera

// [driving] 0x1d5838 RWorldCamera::IsAnchorValid
bool PlayerCamera::IsAnchorValid() {
    CameraAnchor* a = anchor_;
    if (!a) return false;
    if ((a->kind != 1 && (a->flags & 1) == 0) || uint32_t(int32_t(a->body)) >= 0x30u) {
        anchor_ = nullptr;
        d.anchor = 0;
        return false;
    }
    return true;
}

// [driving] 0x1d5ef0 RWorldCamera::SetAnchor (as animações 'Cams' do modelo da âncora: o port não as tem, +0x108/+0x10c
// ficam como estão)
void PlayerCamera::SetAnchor(CameraAnchor* a) {
    anchor_ = a;
    d.anchor = a ? 1u : 0u;  // (no PS2 o ponteiro; aqui só a presença: o objeto fica em anchor_)
    if (IsAnchorValid()) d.mode_change |= 1;
}

// [driving] 0x1d5b90 RWorldCamera::GetAnchorMatrix4
const float* PlayerCamera::GetAnchorMatrix4(const CameraContext& ctx) {
    if (!anchor_matrix_init_) {
        std::memcpy(anchor_matrix_, kIdentity, sizeof(kIdentity));
        anchor_matrix_init_ = true;
    }
    if (!IsAnchorValid()) return anchor_matrix_;
    const CameraAnchor* a = anchor_;
    if ((a->flags & 1) == 0) return &nf::raw(GetRigidBody(*ctx.sim, a->body)->info)->orient[0][0];
    pending();  // SimpleRigidBody::RecalcOrientMat
    return anchor_matrix_;
}

// [driving] 0x1d5cd0 RWorldCamera::GetAnchorPosition
const float* PlayerCamera::GetAnchorPosition(const CameraContext& ctx) {
    if (!IsAnchorValid()) return kNoPosition;
    const CameraAnchor* a = anchor_;
    if ((a->flags & 1) == 0) return rb_pos(GetRigidBody(*ctx.sim, a->body));
    pending();  // o corpo simples
    return kNoPosition;
}

// [driving] 0x1d5888 RWorldCamera::GetAnchorSpeed
float PlayerCamera::GetAnchorSpeed(const CameraContext& ctx) {
    if (!IsAnchorValid()) return 0.0f;
    const CameraAnchor* a = anchor_;
    if (a->kind == 1) {
        const float* v = rb_vel(GetRigidBody(*ctx.sim, a->body));
        return ee::sqrt(ee::add(ee::mul(v[0], v[0]), ee::mul(v[2], v[2])));
    }
    if ((a->flags & 1) == 0) return 0.0f;
    pending();  // SimpleRigidBody::GetScalarVelocity
    return 0.0f;
}

// [driving] 0x1d5d50 RWorldCamera::GetAnchorLinearVelocity (o AIVehicle do primeiro carro: o do jogador não tem)
const float* PlayerCamera::GetAnchorLinearVelocity(const CameraContext& ctx) {
    if (!IsAnchorValid()) return kNoVelocity;
    const CameraAnchor* a = anchor_;
    if ((a->flags & 1) == 0) return rb_vel(GetRigidBody(*ctx.sim, a->body));
    pending();
    return kNoVelocity;
}

// [driving] 0x1d5e38 RWorldCamera::GetAnchorResetAvailable (o PBondCar::GetResetAvailable da âncora)
bool PlayerCamera::GetAnchorResetAvailable() {
    if (!IsAnchorValid()) return false;
    const CameraAnchor* a = anchor_;
    if (a->kind != 1 || !a->object) return false;
    return a->object->reset_available() != 0;
}

// [driving] 0x1d6498 RWorldCamera::AnchorCamera(bool, COORD3 *)
void PlayerCamera::AnchorCamera(const CameraContext& ctx, int32_t smooth, const float offset[3]) {
    const float* pos = GetAnchorPosition(ctx);
    const float* m = GetAnchorMatrix4(ctx);
    const float o[3] = {offset[0], offset[1], offset[2]};
    float r[3];
    xform3(o, m, r);
    if (smooth == 1) lerp3(d.look_at_offset, r, fb(0x3d4ccccdu));
    else std::memcpy(d.look_at_offset, r, 12);
    for (int i = 0; i < 3; ++i) d.look_at[i] = ee::add(d.look_at_offset[i], pos[i]);
}

// [driving] 0x1d5700 RWorldCamera::SetViewingTransform(COORD4 *, float *, COORD3 *, int)
void PlayerCamera::SetViewingTransform(const float up[4], const float lift[2], const float* velocity) {
    float* r0 = d.matrix;
    float* r1 = d.matrix + 4;
    float* r2 = d.matrix + 8;
    float* r3 = d.matrix + 12;
    d.matrix_changed = 1;
    for (int i = 0; i < 3; ++i) r2[i] = ee::sub(d.look_at[i], d.eye[i]);
    for (int i = 0; i < 3; ++i) r2[i] = ee::add(r2[i], ee::mul(up[i], lift[1]));
    for (int i = 0; i < 3; ++i) r2[i] = ee::add(r2[i], ee::mul(r0[i], lift[0]));
    normalize(r2);
    vcross(up, r2, r0);
    normalize(r0);
    vcross(r2, r0, r1);
    std::memcpy(r3, d.eye, 16);
    r2[3] = 0.0f;
    r1[3] = 0.0f;
    r0[3] = 0.0f;
    r3[3] = 1.0f;
    if (velocity) std::memcpy(d.velocity, velocity, 16);
}

// [driving] 0x1d6720 RWorldCamera::SetCameraZoom(float, float)
void PlayerCamera::WorldSetCameraZoom(float target, float step) {
    float f = d.fov;
    const float up = ee::add(f, step);
    if (ee::lt(up, target)) {
        f = up;
    } else {
        const float down = ee::sub(f, step);
        f = ee::lt(target, down) ? down : target;
    }
    if (ee::lt(2.0f, f)) d.fov = f;
}

// [driving] 0x1d5538 RWorldCamera::RestartCamera
void PlayerCamera::RestartWorldCamera(const CameraContext& ctx) {
    SetAnchor(ctx.cars.empty() ? nullptr : ctx.cars[0]);
    d.mode_change |= 1;
    const float one[4] = {0, 0, 0, 1};
    std::memcpy(d.eye, one, 16);
    std::memcpy(d.look_at, one, 16);
    std::memcpy(d.unknown0f0, one, 16);
    std::memcpy(d.look_at_offset, one, 16);
    d.anim_system = 0;
    d.zoom_fov_target = 33.0f;
    d.fov = 33.0f;
    d.zoom_fov = 33.0f;
    // A fila de entrada (ActionQueue::Flush; o port não tem), o RAnimEngine::Handle (~Handle) e o trilho da IA
    // (~AISplinePath).
    anim_handle.reset();
    d.anim_handle = 0;
    if (AISplinePath* path = nf::p32_ptr<AISplinePath>(d.ai_spline)) {
        AISplinePath_Destruct(*path);
        delete path;
    }
    d.ai_spline = 0;
}

// ---------------------------------------------------------------- RPlayerCamera

void PlayerCamera::construct(CameraTables& t, const CameraContext& ctx) {
    tables = &t;
    anim_handle.reset();
    if (AISplinePath* path = nf::p32_ptr<AISplinePath>(d.ai_spline)) {
        AISplinePath_Destruct(*path);
        delete path;
    }
    d = PlayerCameraData{};
    // [driving] 0x1a7ba0 RCameraSpline::RCameraSpline (o construtor do RPlayerCamera cria o +0x350)
    spline = std::make_unique<PlayerCameraSpline>();
    d.spline = 1;
    // [driving] 0x1a43d8 RCamera::RCamera
    std::memcpy(d.matrix, kIdentity, sizeof(kIdentity));
    std::memcpy(d.inverse, kIdentity, sizeof(kIdentity));
    const float one[4] = {0, 0, 0, 1};
    std::memcpy(d.velocity, one, 16);
    d.active = 1;
    d.fov = 33.0f;
    d.matrix_changed = 0;
    // [driving] 0x1d5450 (o SYM diz ~RWorldCamera; é o construtor): os ponteiros e o primeiro reinício.
    RestartWorldCamera(ctx);
    // [driving] 0x1bdcd0 RPlayerCamState::RPlayerCamState e 0x1aa7b8 RDirectorQueue::RDirectorQueue
    state = PlayerCamState{};
    state.camera = 0;  // (no PS2 o ponteiro da câmera)
    state.enabled = 1;
    director_queue.clear();
    director_flags = 0;
    collider.reset();
    d.collider = 0;
    restart(ctx);
}

// [driving] 0x1bdd30 RPlayerCamState::ResetState
static void ResetState(PlayerCamState& s, const Simulation& sim) {
    if (s.enabled == 0) {
        s.enabled = 1;
        return;
    }
    s.anim = s.cinematic = s.cpu_control = s.unknown14 = 0;
    s.paused = sim.state == 3 ? 1u : 0u;
    s.target_lock = s.zoom_held = s.centering = s.lock_zoom = s.unknown30 = 0;
    s.rot_x = s.rot_x2 = s.rot_y = s.rot_y2 = 0.0f;
    s.colorize = 0;
    s.unknown4c[0] = s.unknown4c[1] = 0;
    s.up_hold = 0;
    s.up_count = 0;
    // (o global 0x3276e0, AICharacterHands::fgZoomButton, também volta a zero: no chamador)
}

void PlayerCamera::restart(const CameraContext& ctx) {
    RestartWorldCamera(ctx);
    d.cursor[0] = d.cursor[1] = 0;
    d.distance = 0;
    const float one[4] = {0, 0, 0, 1};
    std::memcpy(d.offset, one, 16);
    std::memcpy(d.unknown300, one, 16);
    std::memcpy(d.up, one, 16);
    d.up[1] = 1.0f;
    std::memcpy(d.noise, one, 16);
    d.noise[3] = 0;
    std::memcpy(d.unknown240, one, 16);
    d.unknown240[3] = 0;
    d.unknown250 = d.unknown254 = 0;
    ResetState(state, *ctx.sim);
    statics.zoom_button = 0;
    state.look_back = 0;
    state.unknown1c = 1;
    d.lock_target = 0;
    std::memcpy(d.lock_offset, one, 16);
    d.zoom_speed = 1.0f;
    d.lock_total = d.lock_count = 0;
    d.tumble = 0;
    d.unknown210 = d.unknown214 = 0;
    d.vertigo = 0;
    d.shake_time = 0;
    d.shake_amount = 0;
    d.shaking = 0;
    d.pitch_rate = d.yaw_rate = 0;
    d.target_pitch_rate = d.target_yaw_rate = 0;
    d.limited = 0;
    d.zoom_slope = 0;
    d.spin = 0;
    d.unknown1c8[0] = d.unknown1c8[1] = 0;
    d.dz_pitch = d.dz_yaw = d.ad_pitch = d.ad_yaw = 0;
    d.cursor[0] = d.cursor[1] = 0;
    d.weap_change = d.weap_change_step = 0;
    std::memcpy(d.unknown200, one, 16);
    d.unknown1f0 = 0;
    d.ad_arm = -1;
    d.arm = 0;
    d.transition_override = 0;
    d.distance_rate = 0;
    std::memcpy(d.unknown2e0, one, 16);
    d.rate = 0;
    d.mode_change = 1;
    d.unknown280 = 0;
    d.car_angle = 0;
    d.transition_on = 0;  // ClearTransition
    d.transition_first = 0;
    d.transition = 0;
    d.last_step = 0;
    d.weapon_anim_step = 0;
    d.accel_yaw = d.accel_pitch = 0;
    d.unknown168 = 0;
    std::memcpy(d.unknown310, kIdentity, sizeof(kIdentity));
    std::memcpy(d.matrix, kIdentity, sizeof(kIdentity));
    d.matrix_changed = 1;
    d.collision = 0;
    std::memcpy(d.matrix_copy, d.matrix, sizeof(d.matrix));
    std::memcpy(d.forward, d.matrix + 8, 16);
    director_queue.clear();
    // [driving] 0x1aa870 RDirectorQueue::RestartDirectorQueue
    director_flags = 2;
    ProcessDirectorLogic(ctx);
    d.unknown264 = d.unknown268 = 0;
    std::memcpy(d.unknown2d0, one, 16);
    collider.reset();
    d.collider = 0;
}

// [driving] 0x1ba418 RPlayerCamera::SetCameraModeByIndex (com os outros argumentos zero: o único uso portado) e
// 0x1aab80 RDirectorQueue::AppendData
void PlayerCamera::SetCameraModeByIndex(int32_t mode) { SetCameraModeByIndex(mode, 0, 0, 0, 0, 0, nullptr); }

// [driving] 0x1aa890 RDirectorQueue::ProcessDirectorLogic
void PlayerCamera::ProcessDirectorLogic(const CameraContext& ctx) {
    const CameraTableGlobals& g = tables->tables;
    if ((director_flags & 2) == 0) {
        if (tables->mode(d.mode).tumble == 1 && GetAnchorResetAvailable()) {
            // [driving] 0x1bbbf8 RPlayerCamera::SetTumbleCam(int) com GetMaxTumble (0x1bd4b8)
            d.tumble = tables->constants.max_tumble;
            director_queue.clear();
            if (d.mode != g.tumble_index) SetCameraModeByIndex(g.tumble_index);
        }
        if ((tables->mode(d.mode).type & 0x120) != 0 && (d.tumble <= 0 || (d.mode_change & 2) != 0))
            SetCameraModeByIndex(d.prev_mode);
    } else {
        d.mode = d.prev_mode = d.prev_selectable = g.mode_count;
        director_queue.clear();
        SetCameraModeByIndex(g.default_index);
    }
    if (director_queue.empty() || (director_flags & 1) != 0) {
        director_flags = 0;
        return;
    }
    // Os pedidos sem a bandeira 4 que não são os últimos caem (os com ela esperam a vez).
    while ((director_queue.front().flags & 4) == 0 && director_queue.size() >= 2)
        director_queue.erase(director_queue.begin());
    DirectorQueueData& q = director_queue.front();
    if (q.delay != 0) {
        --q.delay;
        director_flags = 0;
        return;
    }
    if (q.anchor != 0) pending();  // RPlayerCamera::DirectorSetAnchor
    const DirectorQueueData copy = q;
    DirectorChangeCameraMode(ctx, copy);
    director_queue.erase(director_queue.begin());
    director_flags = 0;
}

// [driving] 0x1badd0 RPlayerCamera::FindHeliArmInd: o primeiro braço cuja máscara de armas tem a arma corrente.
int8_t PlayerCamera::FindHeliArmInd(const CameraContext& ctx, int32_t mode) const {
    const CameraModeInfo& m = tables->mode(mode);
    if (m.type != kCamHeli) return 0;
    if (ctx.current_weapon == -1) return 0;
    uint32_t mask[2] = {0, 0};
    const int32_t w = size_t(ctx.current_weapon) < ctx.weapons.size() ? ctx.weapons[size_t(ctx.current_weapon)].weapon : 0;
    const int32_t word = (w > -1 ? w : w + 31) >> 5;
    if (word >= 0 && word < 2) mask[word] = 1u << (uint32_t(w) & 31);
    const HeliCamInfo& h = tables->heli(m.index);
    const HeliArmInfo* arms = nf::raw(h.arms);
    for (int32_t i = 0; i < h.arm_count; ++i)
        for (int k = 0; k < 2; ++k)
            if ((arms[i].weapons[k] & mask[k]) != 0) return int8_t(i);
    return 0;
}

// [driving] 0x1b89e0 RPlayerCamera::SetupCameraZoom
void PlayerCamera::SetupCameraZoom() {
    const CameraModeInfo& m = tables->mode(d.mode);
    const CameraTableGlobals& g = tables->tables;
    if (m.type == kCamSpline || m.type == kCamFixed) {
        float fov_min, far_dist, near_dist;
        if (m.type == kCamSpline) {
            const SplineCamInfo& s = nf::raw(g.splines)[m.index];
            fov_min = s.min_fov;
            far_dist = s.max_spline_cam_dist;
            near_dist = s.min_zoom_dist;
        } else {
            const FixedCamInfo& f = nf::raw(g.fixeds)[m.index];
            fov_min = f.min_fov;
            far_dist = f.max_zoom_dist;
            near_dist = f.min_zoom_dist;
        }
        d.zoom_fov_max = m.default_fov;
        d.zoom_fov_min = fov_min;
        const float slope = ee::div(ee::sub(fov_min, m.default_fov), ee::sub(far_dist, near_dist));
        d.zoom_slope = slope;
        d.zoom_intercept = ee::sub(fov_min, ee::mul(slope, far_dist));
        return;
    }
    d.zoom_speed = 1.0f;
    d.zoom_fov_max = m.default_fov;
    d.zoom_slope = 0.0f;
    d.zoom_fov_min = m.default_fov;
    d.zoom_intercept = 0.0f;
}

// [driving] 0x1b8740 RPlayerCamera::SetCameraZoom(int, float, int)
void PlayerCamera::SetCameraZoom(int32_t kind, float target, int32_t smooth) {
    const float inc = tables->constants.zoom_inc_speed;
    float f12 = target;
    if (kind == 2) {
        const CameraModeInfo& m = tables->mode(d.mode);
        const AutoDriveArmInfo& a = ad_arm_info(d.ad_arm);
        f12 = m.default_fov;
        if (ee::lt(a.min_fov, f12)) {
            f12 = ee::sub(f12, ee::mul(target, ee::sub(f12, a.min_fov)));
            if (ee::le(f12, 0.0f)) return;
            d.zoom_fov_max = f12;
        }
        if (smooth != 0) {
            const float k = ee::div(1.0f, ee::cvt_s(smooth));
            d.zoom_speed = k;
            WorldSetCameraZoom(f12, ee::mul(inc, k));
            return;
        }
    } else if (kind == 1) {
        if (smooth != 0) {
            WorldSetCameraZoom(target, ee::mul(inc, d.zoom_speed));
            return;
        }
    } else if (kind == 3) {
        f12 = ee::add(ee::mul(d.zoom_slope, target), d.zoom_intercept);
        if (ee::le(f12, 0.0f)) return;
        if (smooth != 0) {
            const float fov = d.fov;
            const float up = ee::add(fov, inc);
            if (ee::lt(up, f12)) {
                const float v = ee::min(up, d.zoom_fov_max);
                if (ee::lt(2.0f, v)) d.fov = v;
                return;
            }
            const float down = ee::sub(fov, inc);
            if (ee::lt(f12, down)) {
                const float v = ee::max(down, d.zoom_fov_min);
                if (ee::lt(2.0f, v)) d.fov = v;
                return;
            }
        }
    } else {
        return;
    }
    if (ee::lt(2.0f, f12)) d.fov = f12;
}

// [driving] 0x1bbf70 RPlayerCamera::DoSmoothModeChange
bool PlayerCamera::DoSmoothModeChange() const {
    if (d.mode_change & 8) return false;
    if (d.mode_change & 4) return true;
    const int32_t n = tables->tables.mode_count;
    if (!(d.prev_mode < n) || !(d.mode < n)) return false;
    if (tables->mode(d.mode).smooth_trans <= 0) return false;
    return tables->mode(d.prev_mode).smooth_trans > 0;
}

// [driving] 0x1ba7a0 RPlayerCamera::DirectorChangeCameraMode
void PlayerCamera::DirectorChangeCameraMode(const CameraContext& ctx, const DirectorQueueData& q) {
    CameraTableGlobals& g = tables->tables;
    const int32_t mode = q.mode;
    if (!(mode < g.mode_count)) return;
    CameraModeInfo& m = nf::raw(g.modes)[mode];
    switch (m.type) {
        case kCamHeli:
            if (g.cinematic_mode_index == mode) {
                m.smooth_trans = int8_t(uint8_t(q.transition));
                HeliArmInfo* arms = nf::raw(nf::raw(g.helis)[g.cinematic_heli_index - 1].arms);
                std::memcpy(&arms[0].sideways, q.vector, 16);
            } else if (d.mode == mode) {
                const int8_t old = d.arm;
                d.arm = FindHeliArmInd(ctx, mode);
                if (d.arm == old) return;
            } else {
                d.arm = FindHeliArmInd(ctx, mode);
            }
            break;
        case kCamAnimation:
        case kCamAIPathAnimation:
            // A câmera do míssil (RColorize) volta ao modo anterior antes de uma animação.
            if (state.colorize != 0 || d.mode == g.missile_index) {
                state.colorize = 0;  // e RColorize::SetEnabled(0)
                d.mode = d.prev_mode;
                g.autodrive_weap_trans_length = 0.0f;
            }
            if (m.type == kCamAnimation) {
                if (q.flags & 0x40) {
                    // [driving] 0x1d6860 RWorldCamera::LoadSingleAnimationFromList: as animações 'Cams' do modelo da
                    // âncora (+0x108/+0x10c; nenhum modelo do port as tem: devolve 0).
                    pending();
                    return;
                }
                if (!LoadSingleAnimation(nf::p32_ptr<const uint8_t>(q.data18))) return;
            } else {
                if (!LoadAISplinePathAnimation(q.data1c)) return;
            }
            break;
        case kCamSpline:
            // A rede de ruas (WRoadNetwork::Get e o seu fValid): o modo Spline não foi portado.
            pending();
            break;
        case kCamAutoDrive: {
            CameraModeGlobals& mg = tables->modes;
            AutoDriveArmInfo* arms = nf::raw(mg.autodrive_arms);
            if (mg.autodrive_arm_count < 1) {
                ++guards;
                break;
            }
            if (arms[0].lock_arm_to_car == 0) d.weap_change = 1;
            if (ee::eq(q.vector[0], -1.0f)) break;
            const float* am = GetAnchorMatrix4(ctx);
            const float heading = cam_atan2_turns(am[8], am[10]);
            const float incline = cam_asin_turns(am[9]);
            float f20, f12;
            if (ee::eq(q.vector[3], 1.0f)) {
                f20 = q.vector[0];
                f12 = q.vector[1];
            } else {
                f20 = ee::sub(q.vector[0], heading);
                f12 = ee::sub(q.vector[1], incline);
            }
            float xr[4][4], yr[4][4];
            VU0_MATRIX4setxrot(xr, cam_wrap_turns(f12));
            VU0_MATRIX4setyrot(yr, cam_wrap_turns(f20));
            VU0_MATRIX4_mult(xr, yr, yr);
            float q0[4];
            VU0_m4toquat(yr, q0);
            for (int32_t i = 0; i < mg.autodrive_arm_count; ++i) {
                AutoDriveArmInfo& a = arms[i];
                const float rest = cam_asin_turns(a.min_pitch[0]);
                a.rest_pitch = rest;
                if (a.has_rest_pos != 0) {
                    a.rest_cur = rest;
                    // O eixo é a linha 0 de yr, que acumula a rotação de cada braço com repouso (como no PS2).
                    float r[4][4];
                    const float axis[4] = {yr[0][0], yr[0][1], yr[0][2], yr[0][3]};
                    RCameraMath_BuildRotationMat4(ee::neg(rest), r, axis);
                    VU0_MATRIX4_mult(r, yr, yr);
                    VU0_m4toquat(yr, a.rotation);
                } else {
                    std::memcpy(a.rotation, q0, 16);
                }
            }
            if (ee::eq(q.vector[3], 1.0f)) {
                f20 = ee::add(q.vector[0], heading);
                f12 = ee::add(q.vector[1], incline);
            } else {
                f20 = q.vector[0];
                f12 = q.vector[1];
            }
            VU0_MATRIX4setxrot(xr, cam_wrap_turns(f12));
            float cm[4][4];
            std::memcpy(cm, d.matrix, 64);
            VU0_MATRIX4setyrot(cm, cam_wrap_turns(f20));
            VU0_MATRIX4_mult(xr, cm, cm);
            std::memcpy(d.matrix, cm, 64);
            d.matrix_changed = 1;
            std::memcpy(d.forward, d.matrix + 8, 16);
            d.ad_pitch = ee::neg(cam_asin_turns(d.matrix[9]));
            float yaw = ee::sub(cam_atan2_turns(d.matrix[8], d.matrix[10]), heading);
            d.ad_yaw = yaw;
            if (ee::le(1.0f, yaw)) d.ad_yaw = ee::sub(yaw, 1.0f);
            if (ee::le(d.ad_yaw, -1.0f)) d.ad_yaw = ee::add(d.ad_yaw, 1.0f);
            break;
        }
        default:
            break;
    }
    const int32_t old = d.mode;
    if (old < g.mode_count) {
        const CameraModeInfo& o = tables->mode(old);
        if ((o.type & 0xfedf) != 0) {
            d.prev_mode = old;
            if (o.selectable != 0) d.prev_selectable = old;
        }
    }
    d.mode_change |= 1;
    d.vertigo = 0;
    d.mode = mode;
    d.shake_time = 0;
    d.transition_override = q.transition;
    SetupCameraZoom();
    d.transition_on = 0;  // [driving] 0x1b9e58 RPlayerCamera::ClearTransition
    d.transition_first = 0;
    d.transition = 0;
    if ((tables->mode(d.mode).type != kCamAnimation || (q.flags & 0x10) != 0) && spline) spline->ClearSplinePtList();
    if (d.mode == g.missile_index) state.colorize = 1;  // e o RColorize::SetEnabled(2)
    if ((q.flags & 0x80) == 0) d.car_angle = 0;
}

// [driving] 0x1b29e0 RPlayerCamera::UpdateCamera
void PlayerCamera::update(const CameraContext& ctx) {
    if (!tables || !tables->loaded() || d.active == 0) return;
    const Simulation& sim = *ctx.sim;
    if (sim.steps == d.last_step && sim.state != 3) return;
    d.last_step = sim.steps;
    if (sim.state != 3) ProcessDirectorLogic(ctx);
    ++updates;
    switch (tables->mode(d.mode).update.fn) {
        case kCamUpdateMomentumHeli:
            UpdateMomentumHeliCam(ctx);
            ++heli_updates;
            break;
        case kCamUpdateNothing:
            break;
        case kCamUpdateAutoDrive:
            UpdateAutoDriveCam(ctx);
            break;
        case kCamUpdateWorldAnimation:
            UpdateWorldAnimationCam(ctx);
            break;
        case kCamUpdateAIPathAnimation:
            UpdateAIPathAnimationCam(ctx);
            break;
        default:
            pending();  // os outros modos
            break;
    }
    if ((tables->mode(d.mode).shake == 0 && d.shaking == 0) || sim.state == 3) {
        d.mode_change = 0;
        return;
    }
    CheckForCameraShaking(ctx);
    d.mode_change = 0;
}

// [driving] 0x1b2b48 RPlayerCamera::UpdateMomentumHeliCam
void PlayerCamera::UpdateMomentumHeliCam(const CameraContext& ctx) {
    const CameraTableGlobals& g = tables->tables;
    const CameraModeInfo& mode = tables->mode(d.mode);
    if (mode.index < 0 || !(mode.index < g.heli_count)) return;
    const HeliCamInfo& heli = tables->heli(mode.index);
    const Simulation& sim = *ctx.sim;
    const float old_distance = d.distance;
    const bool paused = sim.state == 3;
    const HeliArmInfo& arm = nf::raw(heli.arms)[d.arm];
    const float* m = GetAnchorMatrix4(ctx);
    const float* pos = GetAnchorPosition(ctx);
    float anchor_pos[4];
    std::memcpy(anchor_pos, pos, 16);
    float off[4] = {arm.sideways, arm.height, arm.distance, 0};
    std::memcpy(&off[3], arm.unknown3c, 4);
    if (state.look_back == 1 && mode.look_back != 0) {
        off[2] = ee::neg(off[2]);
    } else {
        const float back = ee::min(ee::mul(ee::abs(GetAnchorSpeed(ctx)), heli.fallback_factor), heli.max_fallback);
        off[2] = ee::sub(off[2], back);
    }
    float turned[4] = {0, 0, 0, 0};  // a pilha +0x10 (o w fica com o que estava: zero aqui)
    if (ee::lt(0.0f, heli.noise_amount)) {
        const float k = ee::mul(ee::sub(20.0f, GetAnchorSpeed(ctx)), fb(0x3d4ccccdu));
        const float* p = GetAnchorPosition(ctx);
        const float steps = ee::cvt_s(sim.steps);
        const float n1 = Noise_Noise1(ee::mul(heli.noise_frequency, ee::add(p[0], ee::mul(steps, 0.5f))));
        const float nx = ee::mul(ee::mul(k, heli.noise_amount), n1);
        const float n2 = Noise_Noise1(ee::mul(heli.noise_frequency, ee::add(p[1], ee::mul(steps, fb(0x3e99999au)))));
        const float ny = ee::mul(ee::mul(k, heli.noise_amount), n2);
        turned[0] = nx;
        turned[1] = ny;
        turned[2] = 0.0f;
        turned[3] = 0.0f;
        for (int i = 0; i < 4; ++i) d.noise[i] = ee::add(d.noise[i], ee::mul(ee::sub(turned[i], d.noise[i]), heli.noise_pace));
        for (int i = 0; i < 3; ++i) off[i] = ee::add(off[i], d.noise[i]);
    }
    if (heli.rigid_arm == 0) {
        // (sem âncora o PS2 lê o endereço 0x40; o port conta como não veículo)
        if (anchor_ && anchor_->kind == 1) {
            if (!paused) {
                float f5 = m[9];
                f5 = ee::mul(f5, ee::lt(f5, 0.0f) ? heli.max_vertigo_downhill : heli.max_vertigo_uphill);
                d.vertigo = ee::add(d.vertigo, ee::mul(ee::sub(f5, d.vertigo), heli.vertigo_lerp));
            }
            const float s = d.vertigo;
            const float c = ee::sqrt(ee::sub(1.0f, ee::mul(s, s)));
            const float z = ee::sub(ee::mul(c, off[2]), ee::mul(s, off[1]));
            const float y = ee::add(ee::mul(s, off[2]), ee::mul(c, off[1]));
            off[2] = z;
            off[1] = y;
        }
        // O rumo da âncora no plano: a frente (linha 2) só em x e z, normalizada.
        const float* fwd = m + 8;
        const float q = ee::rsqrt(1.0f, ee::add(ee::mul(fwd[0], fwd[0]), ee::mul(fwd[2], fwd[2])));
        const float fx = ee::mul(fwd[0], q), fz = ee::mul(fwd[2], q);
        const float sx = ee::sub(0.0f, fz), sz = ee::add(0.0f, fx);
        turned[0] = ee::add(ee::mul(sx, off[0]), ee::mul(fx, off[2]));
        turned[2] = ee::add(ee::mul(sz, off[0]), ee::mul(fz, off[2]));
        turned[1] = off[1];
    } else {
        float r[12];
        if (d.mode == g.missile_index || !ee::lt(0.0f, heli.up_rate)) {
            std::memcpy(r, m, sizeof(r));
        } else {
            float up[4] = {0, 1, 0, 0};
            const float f4 = ee::abs(m[9]);
            float f3 = ee::neg(m[5]);
            if (ee::lt(f3, 0.0f)) f3 = 0.0f;
            const float f1a = ee::sub(1.0f, ee::abs(m[1]));
            const float f0 = ee::mul(ee::mul(ee::sub(1.0f, f4), f3), f1a);
            float k = ee::add(f4, f0);
            if (ee::lt(0.0f, ee::cvt_s(state.up_count)) && ee::lt(f4, fb(0x3f333333u))) k = ee::mul(k, fb(0x3dcccccdu));
            if (ee::lt(1.0f, k)) k = 1.0f;
            lerp3(up, m + 4, k);
            normalize(up);
            float side[4] = {1, 0, 0, 0};
            vcross(up, m + 8, side);
            normalize(side);
            vcross(m + 8, side, up);
            std::memcpy(r, side, 16);
            std::memcpy(r + 4, up, 16);
            std::memcpy(r + 8, m + 8, 16);
        }
        xform3(off, r, turned);
    }
    int32_t smooth_zoom = 1;
    bool to_eye = false;
    if (d.mode_change == 0) {
        AnchorCamera(ctx, 1, arm.anchor.offset);
        d.distance = vlen(turned);
        if (d.unknown134 == 1) {
            d.rate = fb(0x3dcccccdu);
        } else {
            float rate = ee::mul(ee::mul(ee::abs(GetAnchorSpeed(ctx)), ee::mul(sim.dt, 60.0f)), heli.speed_rate_diff);
            rate = ee::max(ee::min(heli.max_rate, rate), heli.min_rate);
            if (d.transition > 0 && tables->mode(d.prev_selectable).type != kCamDashboard) {
                if (d.transition_on == 1) {
                    if (d.mode == g.cinematic_mode_index) director_flags |= 1;
                    if (paused) {
                        to_eye = true;
                    } else {
                        if (d.transition_first != 0) {
                            const uint32_t t = uint32_t(arm.arm_transition);
                            float tf;
                            if (int32_t(t) >= 0) {
                                tf = ee::cvt_s(int32_t(t));
                            } else {
                                tf = ee::cvt_s(int32_t((t >> 1) | (t & 1)));
                                tf = ee::add(tf, tf);
                            }
                            d.distance_rate = ee::div(ee::sub(d.distance, old_distance), tf);
                        }
                        d.distance = ee::sub(d.distance, ee::mul(d.distance_rate, ee::cvt_s(d.transition)));
                        d.transition_first = 0;  // [driving] 0x1b9e80 RPlayerCamera::DeincrementTransition
                        d.transition -= 1;
                        d.rate = ee::add(d.rate, ee::mul(ee::sub(rate, d.rate), tables->constants.trans_rate_lerp_rate));
                    }
                }
            } else {
                if (!paused) {
                    d.rate = rate;
                    d.distance = vlen(turned);
                }
                if (d.mode == g.cinematic_mode_index) director_flags &= 0xfe;
            }
        }
        if (!paused && !to_eye) {
            normalize(d.offset);
            for (int i = 0; i < 3; ++i) d.offset[i] = ee::mul(d.offset[i], d.distance);
            float tmp[3];
            for (int i = 0; i < 3; ++i) tmp[i] = ee::sub(turned[i], d.offset[i]);
            for (int i = 0; i < 3; ++i) tmp[i] = ee::mul(tmp[i], d.rate);
            for (int i = 0; i < 3; ++i) d.offset[i] = ee::add(d.offset[i], tmp[i]);
        }
    } else {
        if (DoSmoothModeChange() && (d.mode_change & 2) == 0 &&
            tables->mode(d.prev_selectable).type != kCamDashboard) {
            if (d.mode == g.cinematic_mode_index) director_flags |= 1;
            AnchorCamera(ctx, 1, arm.anchor.offset);
            int32_t t = d.transition_override;
            if (t == 0) t = arm.arm_transition;
            d.transition_on = 1;  // [driving] 0x1b9e68 RPlayerCamera::SetTransition(int, bool)
            d.transition_first = 1;
            d.transition = t;
            d.rate = tables->constants.trans_rate;
        } else {
            AnchorCamera(ctx, 0, arm.anchor.offset);
            smooth_zoom = 0;
            std::memcpy(d.offset, turned, 16);
        }
        d.distance = vlen(d.offset);
    }
    for (int i = 0; i < 3; ++i) d.eye[i] = ee::add(d.look_at[i], d.offset[i]);
    if (heli.check_collisions != 0) ResolveAllCollisions(ctx, d.eye, anchor_pos, 1);
    if (d.mode == g.missile_index) {
        std::memcpy(d.up, m + 4, 16);
    } else if (ee::lt(0.0f, heli.up_rate) && !paused) {
        float tmp[4] = {0, 1, 0, 0};
        const float f3 = ee::abs(m[9]);
        float f2 = ee::neg(m[5]);
        if (ee::lt(f2, 0.0f)) f2 = 0.0f;
        const float f1a = ee::sub(1.0f, ee::abs(m[1]));
        const float f0 = ee::mul(ee::mul(ee::sub(1.0f, f3), f2), f1a);
        float k = ee::add(f3, f0);
        if (state.up_count > 0) {
            if (state.up_hold == 0) state.up_count = state.up_count - 1;
            if (ee::lt(f3, fb(0x3f333333u))) k = ee::mul(k, fb(0x3dcccccdu));
        }
        if (ee::lt(1.0f, k)) k = 1.0f;
        lerp3(tmp, m + 4, k);
        normalize(tmp);
        lerp3(d.up, tmp, heli.up_rate);
        normalize(d.up);
    }
    const float lift[2] = {0.0f, heli.look_up};
    SetViewingTransform(d.up, lift, GetAnchorLinearVelocity(ctx));
    SetCameraZoom(1, mode.default_fov, smooth_zoom);
}

// [driving] 0x1b9198 RPlayerCamera::ResolveAllCollisions(COORD4 *, COORD4 *, bool)
int32_t PlayerCamera::ResolveAllCollisions(const CameraContext& ctx, float eye[4], const float anchor_pos[4], int32_t flag) {
    d.collision = 0;
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    float seg[8] = {0, 0, 0, 1, 0, 0, 0, 1};
    const CameraTableGlobals& g = tables->tables;
    const CameraModeInfo& mode = tables->mode(d.mode);
    const bool noisy = mode.type == kCamHeli && ee::lt(0.0f, tables->heli(mode.index).noise_amount);
    float reach = 2.0f;
    if (flag != 0 && !noisy) reach = 2.5f;
    const float lift = noisy ? fb(0x3dcccccdu) : 0.5f;
    float dir[3];
    for (int i = 0; i < 3; ++i) dir[i] = ee::sub(eye[i], anchor_pos[i]);
    float mid[3];
    for (int i = 0; i < 3; ++i) mid[i] = ee::add(anchor_pos[i], ee::mul(ee::sub(eye[i], anchor_pos[i]), fb(0x3f09999au)));
    float dist = vlen(dir);
    TrackWorld* world = ctx.world;
    if (!world) {
        pending();
        return d.collision;
    }
    if (collider && !WCollider_InRegion(*collider, mid, ee::mul(ee::add(ee::mul(dist, fb(0x3f89999au)), 0.5f), 0.5f), 0xc))
        collider.reset();
    if (!collider) {
        collider = std::make_unique<WCollider>();
        WCollider_Construct(*collider, *world, mid, ee::mul(dist, fb(0x3f69999au)), false, 0xc);
    }
    d.collider = 1;  // (no PS2 o ponteiro; aqui a presença)
    float limit = reach;
    if (flag == 0) {
        const float* am = GetAnchorMatrix4(ctx);
        const float dt = ee::add(ee::add(ee::mul(d.matrix[8], am[8]), ee::mul(d.matrix[9], am[9])), ee::mul(d.matrix[10], am[10]));
        const float k = fb(0x3f8ccccdu);
        limit = ee::add(ee::mul(ee::abs(dt), ee::sub(limit, k)), k);
    }
    std::memcpy(seg, anchor_pos, 16);
    bool far_enough = true;
    float frac = 1.0f;
    const float* up = d.matrix + 4;
    const float* right = d.matrix;
    for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(seg[i], ee::mul(dir[i], fb(0x3f89999au)));
    for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(seg[4 + i], ee::mul(up[i], -0.5f));
    auto probe = [&]() {
        if (!WCollider_GetWorldNormal(*collider, *world, seg, info)) return;
        ++collisions;
        if (info.hit == 1) ++ground_hits;
        else if (info.hit == 2) ++barrier_hits;
        else ++object_hits;
        float a[3], b[3];
        for (int i = 0; i < 3; ++i) a[i] = ee::sub(seg[4 + i], seg[i]);
        for (int i = 0; i < 3; ++i) b[i] = ee::sub(info.point[i], seg[i]);
        const float la = vlen(a);
        float lb = vlen(b);
        if (!ee::lt(fb(0x3f8ccccdu), lb)) return;
        if (ee::lt(lb, limit)) lb = limit;
        const float f = ee::div(lb, la);
        if (ee::lt(f, frac)) frac = f;
        if (ee::lt(ee::sub(lb, limit), 1.5f)) far_enough = false;
    };
    probe();
    for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(seg[4 + i], ee::mul(up[i], 1.0f));
    for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(seg[4 + i], ee::mul(right[i], fb(0xbecccccdu)));
    probe();
    for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(seg[4 + i], ee::mul(right[i], fb(0x3f4ccccdu)));
    probe();
    if (ee::lt(frac, 1.0f)) {
        for (int i = 0; i < 3; ++i) eye[i] = ee::add(anchor_pos[i], ee::mul(dir[i], frac));
        if (flag != 0) {
            const float k = ee::mul(lift, ee::sub(1.0f, frac));
            for (int i = 0; i < 3; ++i) eye[i] = ee::add(eye[i], ee::mul(up[i], k));
        }
    }
    if (mode.type == kCamHeli && far_enough) {
        const float* am = GetAnchorMatrix4(ctx);
        for (int i = 0; i < 3; ++i) dir[i] = ee::sub(eye[i], anchor_pos[i]);
        dist = vlen(dir);
        const float* fwd = am + 8;
        for (int i = 0; i < 3; ++i) seg[i] = ee::add(eye[i], ee::mul(fwd[i], ee::mul(dist, 0.125f)));
        for (int i = 0; i < 3; ++i) seg[4 + i] = ee::add(eye[i], ee::mul(fwd[i], ee::mul(dist, fb(0x3f79999au))));
        if (WCollider_GetWorldNormal(*collider, *world, seg, info)) {
            std::memcpy(statics.vertigo_point, info.point, 16);
            if (statics.vertigo_count < 0x50) ++statics.vertigo_count;
        }
        if (statics.vertigo_count > 0) {
            if (CameraPointInTriangle(statics.vertigo_point, eye, seg + 4, anchor_pos)) {
                if (statics.vertigo_count < 0x50) ++statics.vertigo_count;
            } else {
                --statics.vertigo_count;
            }
            const float k = ee::mul(ee::cvt_s(statics.vertigo_count), fb(0xbc4ccccdu));
            for (int i = 0; i < 3; ++i) eye[i] = ee::add(eye[i], ee::mul(up[i], k));
        }
    }
    CheckObjectCollisions(ctx, eye);
    (void)g;
    return d.collision;
}

// [driving] 0x1b8ae0 RPlayerCamera::CheckObjectCollisions e 0x1b8d08 RPlayerCamera::AdjustCamAroundObjectEllipse (a
// parte dos corpos rígidos: os corpos simples contam como pendentes)
int32_t PlayerCamera::CheckObjectCollisions(const CameraContext& ctx, float eye[4]) {
    int32_t result = 0;
    const float rad = tables->constants.object_sphere_rad;
    for (CameraAnchor* car : ctx.cars) {
        if (car == anchor_) continue;
        const RigidBody* rb = GetRigidBody(*ctx.sim, car->body);
        const RigidBodyInfo* info = nf::raw(rb->info);
        const float reach = ee::mul(len2(info->half_size), rad);
        float pos[4];
        std::memcpy(pos, rb_pos(rb), 16);
        const float dv[3] = {ee::sub(pos[0], eye[0]), ee::sub(pos[1], eye[1]), ee::sub(pos[2], eye[2])};
        if (!ee::le(len2(dv), reach)) continue;
        // AdjustCamAroundObjectEllipse(eye, rb, 0, 0): o olho no referencial do corpo; dentro do elipsoide das
        // meias-medidas x kCameraObjectRadiusEx, sobe pela superfície (o y no mundo fica positivo).
        const float* o = &info->orient[0][0];
        float ext[3];
        for (int i = 0; i < 3; ++i) ext[i] = ee::mul(info->half_size[i], tables->constants.object_radius_ex[i]);
        const float rel[3] = {ee::sub(eye[0], pos[0]), ee::sub(eye[1], pos[1]), ee::sub(eye[2], pos[2])};
        float local[3];
        for (int j = 0; j < 3; ++j)
            local[j] = ee::add(ee::add(ee::mul(o[j * 4], rel[0]), ee::mul(o[j * 4 + 1], rel[1])), ee::mul(o[j * 4 + 2], rel[2]));
        int32_t adjusted = 0;
        if (ee::lt(ee::abs(local[0]), ext[0]) && ee::lt(ee::abs(local[1]), ext[1]) && ee::lt(ee::abs(local[2]), ext[2])) {
            const float lx2 = ee::mul(local[0], local[0]), ly2 = ee::mul(local[1], local[1]), lz2 = ee::mul(local[2], local[2]);
            const float ex2 = ee::mul(ext[0], ext[0]), ey2 = ee::mul(ext[1], ext[1]), ez2 = ee::mul(ext[2], ext[2]);
            const float sx = ee::div(lx2, ex2), sy = ee::div(ly2, ey2), sz = ee::div(lz2, ez2);
            if (ee::lt(ee::add(ee::add(sx, sy), sz), 1.0f)) {
                local[1] = ee::sqrt(ee::mul(ee::sub(ee::sub(1.0f, sx), sz), ey2));
                float w[3];
                xform3(local, o, w);
                w[1] = ee::abs(w[1]);
                for (int i = 0; i < 3; ++i) eye[i] = ee::add(w[i], pos[i]);
                adjusted = 1;
            }
        }
        result = adjusted;
    }
    for (int i = 0; i < kSimpleBodySlots; ++i)
        if (ctx.sim->simple_owners[i]) pending();  // os corpos simples (SimpleRigidBody, tipo 8)
    return result;
}

// [driving] 0x1b9e98 RPlayerCamera::CheckForCameraShaking: as explosões vivas da Simulation (o vetor +0xe2c,
// driving/game/explosion.h) com o raio dano x 5 de pelo menos 50 sacodem a câmera cujo olho está a menos de "raio" (a
// distância AO QUADRADO contra o raio, como no PS2): o tremor (o menor entre o máximo e (raio - d²) x a escala, x a
// escala do modo; o da última explosão no alcance) e o tempo (raio x 3,6 x a escala do tempo x a do modo x a taxa / 60).
void PlayerCamera::CheckForCameraShaking(const CameraContext& ctx) {
    const CameraModeInfo& mode = tables->mode(d.mode);
    if (d.shake_time <= 0) {
        d.shake_amount = 0;
        d.shaking = 0;
    }
    const CameraConstants& c = tables->constants;
    const float scale = mode.explosion_shake_scale;  // f21
    float latest = 0.0f;                              // f22: o tremor da última explosão no alcance
    for (PhysicsObject* po : ctx.sim->explosions) {
        const Explosion* x = dynamic_cast<const Explosion*>(po);
        if (!x) continue;
        const float r = ee::mul(x->d.damage, 5.0f);
        if (ee::lt(r, 50.0f)) continue;
        const SimpleRigidBody* b = GetSimpleRigidBody(*ctx.sim, x->po.slot);
        // vsub.xyz, vmul.xyz, vmulax.x, vmadday.x, vmaddz.x: o quadrado da distância do olho.
        uint32_t dv[3];
        for (int i = 0; i < 3; ++i) dv[i] = ps2::sub(ps2::fbits(d.eye[i]), ps2::fbits(b->pos[i]));
        uint32_t acc = ps2::mul(dv[0], dv[0]);
        acc = ps2::madd(acc, 0x3f800000u, ps2::mul(dv[1], dv[1]));
        const float d2 = ps2::ffloat(ps2::madd(acc, 0x3f800000u, ps2::mul(dv[2], dv[2])));
        if (!ee::lt(d2, r)) continue;
        latest = ee::min(ee::mul(c.explosion_max_shake, scale),
                         ee::mul(ee::mul(ee::sub(r, d2), c.explosion_scale), scale));
        if (ee::lt(d.shake_amount, latest)) d.shake_amount = latest;
        const float tf = ee::div(ee::mul(ee::mul(ee::mul(ee::mul(r, fb(0x40666666u)), c.explosion_time_scale), scale),
                                         ee::cvt_s(ctx.sim->frame_rate)),
                                 60.0f);
        const int32_t frames = ee::cvt_w(tf);
        if (d.shake_time < frames) d.shake_time = frames;
    }
    d.shake_time -= 1;
    if (d.shake_time <= 0) return;
    d.shake_amount = ee::add(d.shake_amount,
                             ee::mul(ee::mul(ee::sub(latest, d.shake_amount), c.explosion_after_shock),
                                     mode.explosion_shake_scale));
    const float t = ee::div(ee::cvt_s(ctx.sim->steps), ee::cvt_s(c.explosion_shake_period));
    const float a = ee::mul(d.shake_amount, Noise_Noise1(t));
    const float b = ee::mul(d.shake_amount, Noise_Noise1(ee::mul(t, fb(0x40678d50u))));
    float* r0 = d.matrix;
    float* r1 = d.matrix + 4;
    float* r2 = d.matrix + 8;
    if (d.shaking != 0) {
        std::memcpy(d.eye, statics.shake_eye, 16);
        for (int i = 0; i < 3; ++i) d.eye[i] = ee::add(d.eye[i], ee::mul(r1[i], a));
        for (int i = 0; i < 3; ++i) d.eye[i] = ee::add(d.eye[i], ee::mul(r0[i], b));
        std::memcpy(d.matrix + 12, d.eye, 16);
    } else {
        for (int i = 0; i < 3; ++i) r1[i] = ee::add(r1[i], ee::mul(r2[i], a));
        normalize(r1);
        for (int i = 0; i < 3; ++i) r2[i] = ee::add(r2[i], ee::mul(r1[i], ee::neg(a)));
        normalize(r2);
        for (int i = 0; i < 3; ++i) r1[i] = ee::add(r1[i], ee::mul(r0[i], b));
        normalize(r1);
        for (int i = 0; i < 3; ++i) r0[i] = ee::add(r0[i], ee::mul(r1[i], ee::neg(b)));
        normalize(r0);
    }
    d.matrix_changed = 1;
}

}  // namespace drv
