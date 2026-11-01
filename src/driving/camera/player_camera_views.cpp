// Os modos da câmera do jogador que faltavam a Paris (docs/driving.md seção 30), portados do MIPS do DRIVING.ELF com a
// aritmética do PS2 na ordem das instruções (platform/ps2_float.h, vu0_macro.h):
//  - a entrada da câmera na configuração do carro (RPlayerCamState::DriveCamInputHandler: o triângulo troca de câmera
//    pelo NextCameraMode, o L2 olha para trás, o freio de mão levanta a câmera Heli, o volante gira o Dashboard);
//  - os modos que o vanquish de Paris alcança: Bumper e Dashboard (selecionáveis: o triângulo os põe), Tumble (o pedido
//    do diretor quando o carro pode ser reposto), Ellipse (a câmera da pausa, que a vitória e a derrota pedem:
//    SetPauseCamera);
//  - as animações relativas ao carro (o 97 EPlayCarCameraAnim: TriggerCarAnimationCamera, a âncora trocada pelo
//    DirectorSetAnchor, o registro 'Cams' do modelo do carro pelo LoadSingleAnimationFromList, UpdateRelativeAnimationCam
//    com AnchorRelativeCamera) e as câmeras animadas de verdade (UpdateAnimationCam: a chave corrente da animação do
//    RAnimEngine);
//  - o SetOldCameraMode com transição (um RPlayerCamera montado na pilha para achar o ponto de partida);
//  - a mira do fuzil das mãos de Bond (RPlayerCamState::AimZoom e AimRelease: o zoom do AutoDrive e a luneta do HUD).
// O que Paris não usa e não foi portado: o Spline (nenhum pedido o alcança: os modos SplineCam não são selecionáveis
// nem têm camID), o Fixed (o camera.ini de Paris não tem modo Fixed: o TriggerFixedCamera do HandleNuclearSequence não
// faz nada) e o Collision (o SetCollisionCam não tem quem o chame). Conferidos na parte 26 do nf_driving_check.
//
// Divergências (cada uma também na função):
//  - as pistas que o PS2 deixa com o lixo da pilha (o w de alguns vetores locais copiados para o olho e para o +0x2f0)
//    ficam com o valor anterior ou zero; a parte 26 não as compara;
//  - o UpdateRelativeAnimationCam, no primeiro quadro de um registro com animação com transição suave (a transição
//    pedida e o modo anterior com transição suave) e sem o ângulo do AutoDrive, multiplica por uma matriz cujas linhas 1
//    a 3 são o lixo da pilha (o caminho que pula a rotação): o port usa zeros, e a curva da transição sai diferente; e,
//    sem animação e sem transição, decide o fim pelo s6 do chamador (lixo): o port usa 0. O destrutor do 97 põe a
//    transição em pelo menos 1, então um 97 saindo de um modo suave cai no primeiro caminho; os 97 de Paris estão em
//    listas de regras que os finais não alcançam, e os registros do modelo têm animação (o segundo não é alcançado). A
//    parte 26 só pede animações com transição saindo de modos sem transição suave.

#include <atomic>
#include <cstring>

#include "driving/anim/eagl_anim.h"
#include "driving/camera/camera_math.h"
#include "driving/camera/player_camera.h"
#include "driving/carp_runtime.h"
#include "driving/light_manager.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/render/anim_engine.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

const uint32_t kOne = 0x3f800000u;
const uint32_t kMinusOne = 0xbf800000u;

float fb(uint32_t b) { return ps2::ffloat(b); }

// (float)(uint32_t) como o GCC do PS2 converte.
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(f, f);
}

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
// d.xyz = a + (b - a) * k (vsub.xyz t, b, a; vmulx.xyz t, t, k; vadd.xyz d, a, t).
void vu_lerp3(Reg d, const Reg a, const Reg b, float k) {
    Reg t, kk = {ps2::fbits(k), 0, 0, 0};
    std::memcpy(t, d, 16);
    sub(t, b, a, XYZ);
    mulbc(t, t, kk, BX, XYZ);
    add(d, a, t, XYZ);
}
// d.xyz = a * k (vmulx.xyz).
void vu_scale3(Reg d, const Reg a, float k) {
    const Reg kk = {ps2::fbits(k), 0, 0, 0};
    mulbc(d, a, kk, BX, XYZ);
}
// vmul.xyz t, a, b; vaddy.x; vaddz.x: (ax bx + ay by) + az bz.
float vu_dot3(const Reg a, const Reg b) {
    Reg t{};
    mul(t, a, b, XYZ);
    addbc(t, t, t, BY, X);
    addbc(t, t, t, BZ, X);
    return ps2::ffloat(t[0]);
}
// O vetor `o` girado no plano xz pela frente `f` (vmul.xz/vmulax.x/vmaddz.x com 1, vrsqrt, vmulq.xz: a frente
// normalizada no plano; vsubz.x/vaddx.z: (-nz, nx); vmulax.xz/vmaddz.xz e vmove.y): o x e o z de `d`, o y de `o`.
void vu_rotate_xz(Reg d, const Reg f, const Reg o) {
    Reg n, acc{}, one = {kOne, 0, 0, 0}, r{}, zero{};
    std::memcpy(n, f, 16);
    mul(n, f, f, X | Z);
    mulabc(acc, f, f, BX, X);
    maddbc(n, acc, one, n, BZ, X);
    const uint32_t q = ps2::rsqrt(kOne, n[0]);
    muls(n, f, q, X | Z);
    subbc(r, zero, n, BZ, X);
    addbc(r, zero, n, BX, Z);
    mulabc(acc, r, o, BX, X | Z);
    maddbc(d, acc, n, o, BZ, X | Z);
    move(d, o, Y);
}

uint32_t r32(uint32_t p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<uint8_t>(p) + off, 4);
    return v;
}
uint16_t r16(uint32_t p, uint32_t off) {
    uint16_t v;
    std::memcpy(&v, nf::p32_ptr<uint8_t>(p) + off, 2);
    return v;
}

const float kUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};  // o vetor (0, 1, 0, 0) do MEM_fill e do 1,0 em +4

}  // namespace

// ---------------------------------------------------------------- a entrada do carro

// [driving] 0x1bddc8 RPlayerCamState::DriveCamInputHandler
void PlayerCamera::DriveCamInputHandler(const CameraContext& ctx, int32_t action, float value) {
    ++drive_inputs;
    state.paused = (ctx.sim->state == 3 || ctx.sim->steps < 3) ? 1u : 0u;
    const bool idle = state.paused == 0 && state.anim == 0 && state.cinematic == 0 && state.cpu_control == 0 &&
                      state.unknown14 == 0;
    switch (action) {
        case 1:  // GAMEACTION_STEER: o Dashboard gira com o volante
            if (idle) {
                const float v = ee::neg(value);
                std::memcpy(&state.unknown4c[0], &v, 4);
            }
            break;
        case 0x1b:  // GAMEACTION_HANDBRAKE: a câmera Heli sobe
            if (idle) {
                state.up_count = 0x78;
                state.up_hold = 1;
            }
            break;
        case 0x1c:  // GAMEACTION_HANDBRAKERELEASE
            state.up_hold = 0;
            break;
        case 0x26:  // GAMEACTION_CHANGECAMERA
        case 0x27:  // GAMEACTION_CHANGECAMERAUP
            if (idle) NextCameraMode(0);
            break;
        case 0x28:  // GAMEACTION_CHANGECAMERADOWN
            if (idle) PrevCameraMode(0);
            break;
        case 0x29:  // GAMEACTION_CAMLOOKBACK
            state.unknown1c = 0;
            if (idle) SetCameraLookBack(1);
            break;
        case 0x2a:  // GAMEACTION_CAMLOOKBACKRELEASE
            if (state.unknown1c == 0) {
                state.unknown1c = 1;
                if (state.paused == 0) SetCameraLookBack(0);
            }
            break;
        case 0x2c:  // GAMEACTION_CAMROTATEX
            if (idle) std::memcpy(&state.unknown4c[0], &value, 4);
            break;
        default:
            break;
    }
}

// [driving] 0x1be570 RPlayerCamState::AimZoom: sem míssil guiado (0x1be6f8), a mira presa no zoom do braço do AutoDrive
// (InitAimZoom), os giros da mira zerados, o som da mira e o GHud::AimOn. O PS2 compara a arma corrente com 0x1f e 0x16,
// mas o desvio cai sempre no mesmo lugar (a condição é sempre verdadeira).
void PlayerCamera::AimZoom(const CameraContext& ctx) {
    if (state.zoom_held != 0) return;
    if (ctx.mission_missile != 0) return;
    state.zoom_held = 1;
    state.centering = 0;
    state.target_lock = 0;
    InitAimZoom();
    state.rot_y = 0.0f;
    state.rot_x = 0.0f;
    state.rot_y2 = 0.0f;
    state.rot_x2 = 0.0f;
    SetAutoDriveRotationX(0.0f);
    SetAutoDriveRotationY(state.rot_x);
    if (ctx.audio_zoom) *ctx.audio_zoom = 1;  // o AVehicle +0xc0 (o GetAudio do carro do jogador)
    if (ctx.hud_aim) ctx.hud_aim(1);          // [driving] 0x244ef0 GHud::AimOn
}

// [driving] 0x1be660 RPlayerCamState::AimRelease: fora de um CameraLockOn, o som e o GHud::AimOff (se a mira estava
// presa), a mira solta, o zoom do braço desfeito.
void PlayerCamera::AimRelease(const CameraContext& ctx) {
    if (state.lock_zoom != 0) return;
    if (state.zoom_held != 0) {
        if (ctx.audio_zoom) *ctx.audio_zoom = 0;
        if (ctx.hud_aim) ctx.hud_aim(0);  // [driving] 0x244f30 GHud::AimOff
    }
    state.zoom_held = 0;
    state.target_lock = 0;
    state.unknown30 = 0;
    ResetZoomSlope();
    ToggleAutoDriveZoom(int32_t(state.zoom_held));
}

// [driving] 0x1ba5c0 RPlayerCamera::NextCameraMode
void PlayerCamera::NextCameraMode(uint16_t delay) {
    const CameraTableGlobals& g = tables->tables;
    int32_t m = d.mode;
    if ((tables->mode(m).selectable == 0 && tables->mode(m).type != kCamCollision) || m == g.cinematic_mode_index)
        return;
    if (g.mode_count == 0) {  // (o PS2 para na divisão por zero)
        ++guards;
        return;
    }
    int32_t k = 0;
    for (;;) {
        m = (m + 1) % g.mode_count;
        if (!(k < g.mode_count) || tables->mode(m).selectable != 0) break;
        ++k;
    }
    ++mode_switches;
    SetCameraModeByIndex(m, delay, 0, 0, 0, 0, nullptr);
}

// [driving] 0x1bc048 RPlayerCamera::SetCameraLookBack
void PlayerCamera::SetCameraLookBack(int32_t on) {
    const CameraTableGlobals& g = tables->tables;
    const int32_t m = d.mode;
    if (tables->mode(m).look_back != 0) {
        state.look_back = uint32_t(on);
        d.mode_change |= 2;
        return;
    }
    if (on == 0 && (m == g.missile_index || m == g.world_anim_index || m == g.rel_anim_index ||
                    m == g.ai_path_anim_index || m == g.tumble_index))
        state.look_back = 0;
}

// [driving] 0x1bcb88 RPlayerCamera::PauseOff (o ~EResume: a volta da pausa)
void PlayerCamera::PauseOff() {
    if (tables->mode(d.mode).look_back == 0) return;
    if (state.unknown1c == 0) {
        if (state.look_back == 0) SetCameraLookBack(1);
    } else if (state.look_back != 0) {
        SetCameraLookBack(0);
    }
}

// [driving] 0x1bb9e8 RPlayerCamera::SetPauseCamera: a fila do diretor esvaziada e o modo da pausa (o Ellipse).
void PlayerCamera::SetPauseCamera() {
    ++pause_cams;
    director_queue.clear();
    SetCameraModeByIndex(tables->tables.pause_index, 0, 4, 0, 0, 0, nullptr);
}

// ---------------------------------------------------------------- RWorldCamera

// [driving] 0x1d5fa8 RWorldCamera::GetAnchorRenderOffset (o RVehicle::GetRenderOffset do objeto de desenho da âncora)
float PlayerCamera::GetAnchorRenderOffset() {
    if (!IsAnchorValid()) return 0.0f;
    return anchor_object()->render_offset;
}

// [driving] 0x1d5940 RWorldCamera::GetAnchorAcceleration(COORD4 &): a força do corpo nas direções x e z dele, sobre a
// massa (só os veículos; os outros: zero; sem âncora válida `out` fica como estava).
void PlayerCamera::GetAnchorAcceleration(const CameraContext& ctx, float out[4]) {
    if (!IsAnchorValid()) return;
    if (anchor_object()->kind != 1) {
        out[0] = out[1] = out[2] = 0.0f;  // sqc2 vf0 e o w zerado
        out[3] = 0.0f;
        return;
    }
    const RigidBody* b = GetRigidBody(*ctx.sim, anchor_object()->body);
    const RigidBodyInfo* info = nf::raw(b->info);
    Reg f, r;
    load(f, b->force);
    load(r, info->orient[0]);
    out[0] = vu_dot3(f, r);
    out[1] = 0.0f;
    load(r, info->orient[2]);
    out[2] = vu_dot3(f, r);
    out[3] = 0.0f;
    const float k = ee::div(1.0f, b->mass);
    Reg o;
    load(o, out);
    vu_scale3(o, o, k);
    store(out, o);
}

// [driving] 0x1d65e0 RWorldCamera::AnchorRelativeCamera(bool, CameraAnchorInfo *): o deslocamento (0, y menos o
// deslocamento do desenho, z) suavizado no referencial do carro (sobre o do mundo do quadro anterior: como no PS2) e
// girado para o mundo.
void PlayerCamera::AnchorRelativeCamera(const CameraContext& ctx, int32_t smooth, const CameraAnchorInfo& info) {
    const float* M = GetAnchorMatrix4(ctx);
    const float* P = GetAnchorPosition(ctx);
    const float ro = GetAnchorRenderOffset();
    const float local[4] = {0.0f, ee::sub(info.offset[1], ro), info.offset[2], 0.0f};
    Reg o;
    load(o, d.look_at_offset);
    if (smooth == 1) {
        Reg l;
        load(l, local);
        vu_lerp3(o, o, l, fb(0x3d4ccccdu));
    } else {
        load(o, local);
    }
    vu_xform3(o, o, M);
    store(d.look_at_offset, o);
    Reg p, la;
    load(p, P);
    load(la, d.look_at);
    add(la, o, p, XYZ);
    store(d.look_at, la);
}

// ---------------------------------------------------------------- Bumper

// [driving] 0x1b3b40 RPlayerCamera::UpdateBumperCam
// Divergência: o w do olho no primeiro quadro (o lixo da pilha no PS2) fica o anterior.
void PlayerCamera::UpdateBumperCam(const CameraContext& ctx) {
    const CameraModeInfo& m = tables->mode(d.mode);
    const int32_t idx = m.index;
    if (idx < 0 || !(idx < tables->tables.bumper_count)) return;
    ++view_updates;
    const BumperCamInfo& b = nf::raw(tables->tables.bumpers)[idx];
    const float* arm = (state.look_back == 1 && m.look_back != 0) ? b.backwards_arm : b.forward_arm;
    const float* M = GetAnchorMatrix4(ctx);
    const float* P = GetAnchorPosition(ctx);
    Reg a, t{}, e{}, p;
    load(a, arm);
    vu_xform3(t, a, M);
    load(p, P);
    add(e, p, t, XYZ);
    const float ey = ee::add(ps2::ffloat(e[1]), arm[3]);
    if (d.mode_change != 0) {
        d.eye[0] = ps2::ffloat(e[0]);
        d.eye[1] = ey;
        d.eye[2] = ps2::ffloat(e[2]);
    } else {
        const float y = d.eye[1];
        const float dy = ee::sub(ey, y);
        d.eye[0] = ps2::ffloat(e[0]);
        d.eye[2] = ps2::ffloat(e[2]);
        d.eye[1] = ee::add(y, ee::mul(dy, tables->constants.bumper_y_lerp_rate));
    }
    Reg eye, la;
    load(eye, d.eye);
    load(la, d.look_at);
    add(la, eye, t, XYZ);
    store(d.look_at, la);
    SetViewingTransform(M + 4, nullptr, GetAnchorLinearVelocity(ctx));
    if (ee::lt(2.0f, m.default_fov)) d.fov = m.default_fov;
}

// ---------------------------------------------------------------- Dashboard

// [driving] 0x1b3ce0 RPlayerCamera::UpdateDashboardCam
// Divergência: o w do olho no primeiro quadro do braço de trás (o lixo da pilha no PS2) fica o anterior; o w do +0x2f0
// no primeiro quadro do da frente fica zero.
void PlayerCamera::UpdateDashboardCam(const CameraContext& ctx) {
    const Simulation& sim = *ctx.sim;
    const bool paused = sim.state == 3;
    const CameraModeInfo& m = tables->mode(d.mode);
    const int32_t idx = m.index;
    if (idx < 0 || !(idx < tables->tables.dashboard_count)) return;
    ++view_updates;
    const DashboardCamInfo& db = nf::raw(tables->tables.dashboards)[idx];
    if (state.look_back == 1 && m.look_back != 0) {
        // O braço de trás (como o Bumper).
        const float* M = GetAnchorMatrix4(ctx);
        const float* P = GetAnchorPosition(ctx);
        Reg a, t{}, e{}, p;
        load(a, db.backwards_arm);
        vu_xform3(t, a, M);
        load(p, P);
        add(e, p, t, XYZ);
        if (d.mode_change != 0) {
            d.eye[0] = ps2::ffloat(e[0]);
            d.eye[1] = ps2::ffloat(e[1]);
            d.eye[2] = ps2::ffloat(e[2]);
        } else {
            const float y = d.eye[1];
            const float dy = ee::sub(ps2::ffloat(e[1]), y);
            d.eye[0] = ps2::ffloat(e[0]);
            d.eye[2] = ps2::ffloat(e[2]);
            d.eye[1] = ee::add(y, ee::mul(dy, tables->constants.bumper_y_lerp_rate));
        }
        Reg eye, la;
        load(eye, d.eye);
        load(la, d.look_at);
        add(la, eye, t, XYZ);
        store(d.look_at, la);
        SetViewingTransform(M + 4, nullptr, GetAnchorLinearVelocity(ctx));
        if (ee::lt(2.0f, m.default_fov)) d.fov = m.default_fov;
        return;
    }
    float arm[4];
    std::memcpy(arm, db.forward_arm, 16);
    const float* M = GetAnchorMatrix4(ctx);
    const float* P = GetAnchorPosition(ctx);
    if (d.mode_change == 0 && anchor_object()->kind == 1) {
        if (!paused) {
            // A vertigem: o braço sobe ou desce com a inclinação do carro.
            const float fy = M[9];
            // (descendo o máximo da descida; subindo o oposto do da subida)
            const float lim = ee::lt(fy, 0.0f) ? db.max_vertigo_downhill : ee::neg(db.max_vertigo_uphill);
            float v = ee::mul(ee::mul(lim, arm[2]), fy);
            if (ee::lt(v, fb(0xbd4ccccdu))) v = fb(0xbd4ccccdu);
            if (ee::lt(fb(0x3d4ccccdu), v)) v = fb(0x3d4ccccdu);
            d.vertigo = ee::add(d.vertigo, ee::mul(ee::sub(v, d.vertigo), db.vertigo_lerp));
        }
        arm[1] = ee::add(arm[1], d.vertigo);
    }
    Reg a, t{}, p;
    load(a, arm);
    vu_xform3(t, a, M);
    load(p, P);
    if (d.mode_change != 0) {
        std::memcpy(d.offset, t, 12);
        d.offset[3] = 0.0f;
        Reg eye;
        load(eye, d.eye);
        add(eye, p, t, XYZ);
        store(d.eye, eye);
    } else {
        if (!paused) {
            float s = ee::abs(GetAnchorSpeed(ctx));
            const float f1 = ee::mul(sim.dt, 60.0f);
            s = ee::mul(s, f1);
            s = ee::mul(s, db.intertia_scale);
            float r = ee::min(db.intertia_max, s);
            r = ee::max(r, db.intertia_min);
            d.rate = r;
        }
        d.distance = vu_length(t);
        Reg o, w{};
        load(o, d.offset);
        sub(w, t, o, XYZ);
        vu_scale3(w, w, d.rate);
        add(o, o, w, XYZ);
        store(d.offset, o);
        Reg eye;
        load(eye, d.eye);
        add(eye, p, o, XYZ);
        store(d.eye, eye);
    }
    d.eye[3] = 1.0f;
    {
        Reg eye, f, la;
        load(eye, d.eye);
        load(f, M + 8);
        load(la, d.look_at);
        if (state.look_back == 1 && m.look_back != 0) sub(la, eye, f, XYZ);
        else add(la, eye, f, XYZ);
        store(d.look_at, la);
    }
    d.look_at[3] = 1.0f;
    float up[4];
    std::memcpy(up, M + 4, 16);
    float lift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!paused) {
        // A inércia pela aceleração do carro. O PS2 lê as chaves torque* (+0x40, +0x50, +0x64), não as force* (+0x20,
        // +0x30, +0x60) que o camera.ini de Paris dá: sem torque*, o Dashboard do vanquish não balança.
        float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // (sem âncora válida o PS2 deixa o lixo da pilha)
        GetAnchorAcceleration(ctx, acc);
        Reg ac, fs;
        load(ac, acc);
        vu_scale3(ac, ac, ee::neg(sim.dt));
        load(fs, db.torque_scale);
        mul(ac, ac, fs, XYZ);
        store(acc, ac);
        float fx = ee::min(db.torque_max[0], acc[0]);
        fx = ee::max(fx, ee::neg(db.torque_max[0]));
        acc[0] = fx;
        float fz = ee::min(db.torque_max[2], acc[2]);
        fz = ee::max(fz, ee::neg(db.torque_max[2]));
        acc[2] = fz;
        Reg f, s, k = {ps2::fbits(db.torque_pace), 0, 0, 0};
        load(f, d.dash_force);
        load(s, acc);
        sub(s, s, f, XYZW);
        mulbc(s, s, k, BX, XYZW);
        add(f, f, s, XYZW);
        store(d.dash_force, f);
    }
    up[0] = ee::sub(up[0], d.dash_force[0]);
    lift[1] = ee::sub(lift[1], d.dash_force[2]);
    if (!ee::eq(db.steer_scale, 0.0f)) {
        if (!paused) {
            float sv;
            std::memcpy(&sv, &state.unknown4c[0], 4);
            float v = ee::min(db.steer_max, ee::mul(sv, db.steer_scale));
            v = ee::max(v, ee::neg(db.steer_max));
            d.dash_steer = ee::add(d.dash_steer, ee::mul(ee::sub(v, d.dash_steer), db.steer_pace));
        }
        lift[0] = d.dash_steer;
    }
    SetViewingTransform(up, lift, GetAnchorLinearVelocity(ctx));
    if (ee::lt(2.0f, m.default_fov)) d.fov = m.default_fov;
    if (ee::eq(db.glance_scale, 0.0f)) return;
    if (!paused) {
        float gv;
        std::memcpy(&gv, &state.unknown4c[1], 4);
        float v = ee::min(db.glance_max, ee::mul(gv, db.glance_scale));
        v = ee::max(v, ee::neg(db.glance_max));
        d.dash_glance = ee::add(d.dash_glance, ee::mul(ee::sub(v, d.dash_glance), db.glance_pace));
    }
    float row1[4], row0[4], r[4][4], cm[4][4];
    std::memcpy(row1, M + 4, 16);
    std::memcpy(row0, M, 16);
    RCameraMath_BuildRotationMat4(ee::add(db.forward_yaw, d.dash_glance), r, row1);
    std::memcpy(cm, d.matrix, 64);
    VU0_MATRIX4_mult(r, cm, cm);
    RCameraMath_BuildRotationMat4(db.forward_pitch, r, row0);
    VU0_MATRIX4_mult(r, cm, cm);
    std::memcpy(d.matrix, cm, 64);
    d.matrix_changed = 1;
}

// ---------------------------------------------------------------- Tumble

// [driving] 0x1b3788 RPlayerCamera::UpdateTumbleCam: a câmera do capotamento, em volta do carro pelo braço do modo
// anterior (o Heli) ou (0, 3, -6), na direção em que o carro anda.
void PlayerCamera::UpdateTumbleCam(const CameraContext& ctx) {
    const CameraTableGlobals& g = tables->tables;
    if (!(d.prev_mode < g.mode_count)) return;
    const CameraModeInfo& pm = tables->mode(d.prev_mode);
    Reg off{};
    if (pm.type == kCamHeli) {
        const int32_t hi = pm.index;
        if (hi < 0 || !(hi < g.heli_count)) return;
        const HeliCamInfo& h = tables->heli(hi);
        if (d.arm < 0 || d.arm >= h.arm_count) {
            ++guards;  // (o PS2 leria o braço fora da tabela: o braço vem do FindHeliArmInd, sempre dentro)
            return;
        }
        const HeliArmInfo& arm = nf::raw(h.arms)[d.arm];
        Reg a;
        load(a, &arm.sideways);
        vu_scale3(off, a, h.tumble_arm_scale);
        AnchorCamera(ctx, 1, arm.anchor.offset);
    } else {
        const float o[4] = {0.0f, 3.0f, -6.0f, 0.0f};
        load(off, o);
        AnchorCamera(ctx, 1, tables->modes.tumble_anchor.offset);
    }
    ++view_updates;
    d.distance = vu_length(off);
    Reg v{}, w{};
    if (!ee::eq(GetAnchorSpeed(ctx), 0.0f)) {
        load(v, GetAnchorLinearVelocity(ctx));
    } else {
        Reg la, eye;
        load(la, d.look_at);
        load(eye, d.eye);
        sub(v, la, eye, XYZ);
    }
    {
        Reg la, eye;
        load(la, d.look_at);
        load(eye, d.eye);
        sub(w, la, eye, XYZ);
    }
    vu_lerp3(w, w, v, tables->modes.vector_lerp);
    Reg rel{};
    vu_rotate_xz(rel, w, off);
    vu_normalize3(rel, rel);
    vu_scale3(rel, rel, d.distance);
    if (d.mode_change & 1) {
        if (!DoSmoothModeChange()) {
            const float y = ee::add(d.eye[1], 2.0f);
            d.offset[0] = d.eye[0];
            d.offset[2] = d.eye[2];
            d.offset[1] = y;
            Reg o, la;
            load(o, d.offset);
            load(la, d.look_at);
            sub(o, o, la, XYZ);
            store(d.offset, o);
        } else {
            Reg o, eye, la;
            load(o, d.offset);
            load(eye, d.eye);
            load(la, d.look_at);
            sub(o, eye, la, XYZ);
            store(d.offset, o);
        }
    }
    {
        Reg o;
        load(o, d.offset);
        vu_lerp3(o, o, rel, tables->modes.rel_pos_lerp);
        store(d.offset, o);
        Reg la, eye;
        load(la, d.look_at);
        load(eye, d.eye);
        add(eye, la, o, XYZ);
        store(d.eye, eye);
    }
    ResolveAllCollisions(ctx, d.eye, GetAnchorPosition(ctx), 0);
    d.tumble -= 1;
    SetViewingTransform(kUp, nullptr, GetAnchorLinearVelocity(ctx));
    SetCameraZoom(1, d.fov, 1);
}

// ---------------------------------------------------------------- Ellipse

// [driving] 0x1b4c30 RPlayerCamera::UpdateEllipseCam: a câmera que gira em volta do carro (a da pausa, a do fim da
// missão): uma faceta por quadro numa elipse de raios xRad e zRad em volta da frente do carro, a altura pela lista
// "heights" (interpolada no segundo e no quarto quarto da volta).
// Divergência: o w do +0x2f0 (o lixo da pilha no PS2) fica o do vetor local (zero).
void PlayerCamera::UpdateEllipseCam(const CameraContext& ctx) {
    const CameraTableGlobals& g = tables->tables;
    const CameraModeInfo& m = tables->mode(d.mode);
    const int32_t idx = m.index;
    if (idx < 0 || !(idx < g.ellipse_count)) return;
    const EllipseCamInfo& e = nf::raw(g.ellipses)[idx];
    const uint32_t facets = uint32_t(e.facets);
    const uint32_t hc = uint32_t(e.height_count);
    const int8_t* heights = nf::raw(e.heights);
    const uint32_t q = facets >> 2;
    if (facets == 0 || (facets >> 1) == 0 || hc == 0 || !heights) {
        ++guards;  // (o PS2 para na divisão por zero)
        return;
    }
    ++view_updates;
    if (d.mode_change != 0) {
        d.ellipse_facet = ee::cvt_w(ee::mul(0.0f, u2f(facets)));
        d.ellipse_height = 0;
    }
    d.ellipse_facet = int32_t((uint32_t(d.ellipse_facet) + 1) % facets);
    const uint32_t cnt = uint32_t(d.ellipse_facet);
    const float turns = ee::div(u2f(cnt), u2f(facets));
    const float s = cam_sin_turns(turns);
    const float c = cam_cos_turns(turns);
    float off[4] = {ee::mul(e.x_rad, c), 0.0f, ee::mul(e.z_rad, s), 0.0f};
    if (cnt % (facets >> 1) == 0) d.ellipse_height = int32_t((uint32_t(d.ellipse_height) + 1) % hc);
    const uint32_t hi = uint32_t(d.ellipse_height);
    bool interp;
    if (cnt < 3 * q) interp = !(cnt < q) && cnt < 2 * q;
    else interp = true;
    if (!interp) {
        off[1] = ee::mul(ee::cvt_s(heights[hi % hc]), 0.5f);
    } else {
        const uint32_t part = q ? cnt % q : 0;
        if (q == 0) ++guards;
        const float h0 = ee::mul(ee::cvt_s(heights[hi % hc]), 0.5f);
        const float h1 = ee::mul(ee::cvt_s(heights[(hi + 1) % hc]), 0.5f);
        const float k = ee::mul(ee::sub(h1, h0), u2f(part));
        off[1] = ee::add(ee::div(k, u2f(q)), h0);
    }
    const float* M = GetAnchorMatrix4(ctx);
    Reg f, o, r{};
    load(f, M + 8);
    load(o, off);
    vu_rotate_xz(r, f, o);
    store(off, r);
    off[1] = ee::add(ee::abs(off[1]), 1.0f);
    if (d.mode_change == 0) {
        AnchorCamera(ctx, 1, e.anchor.offset);
        if (d.transition > 0 && d.transition_on == 1) UpdateTransition(d.offset, off);
        else std::memcpy(d.offset, off, 16);
    } else if (DoSmoothModeChange()) {
        AnchorCamera(ctx, 1, e.anchor.offset);
        InitTransition(1, d.offset, off, m.smooth_trans);
    } else {
        AnchorCamera(ctx, 0, e.anchor.offset);
        std::memcpy(d.offset, off, 16);
    }
    {
        Reg la, ofs, eye;
        load(la, d.look_at);
        load(ofs, d.offset);
        load(eye, d.eye);
        add(eye, la, ofs, XYZ);
        store(d.eye, eye);
    }
    ResolveAllCollisions(ctx, d.eye, GetAnchorPosition(ctx), 0);
    SetViewingTransform(kUp, nullptr, GetAnchorLinearVelocity(ctx));
    SetCameraZoom(1, m.default_fov, 1);
}

// ---------------------------------------------------------------- o SetOldCameraMode com transição

// [driving] 0x1bb340 RPlayerCamera::SetOldCameraMode (o caminho com transição): um RPlayerCamera na pilha (0x1b2620,
// destruído no fim por 0x1b2728) na âncora desta câmera roda a função do modo pedido uma vez; o ponto a que ele chega
// vira um registro 'Cams' de 0x40 bytes (o operator new; a bandeira 8 do pedido o apaga depois): relativo ao carro
// (`arg4` 1: o modo RelativeAnim, o olho e a frente no referencial do carro) ou no mundo (o WorldAnim, com a bandeira 1
// do registro: o olho relativo ao ponto olhado). Os globais do módulo (o olho do tremor, o anti-vertigem, o fgZoomButton
// que o reinício zera) são os mesmos das duas câmeras, como no PS2.
void PlayerCamera::SetOldCameraModeTransition(const CameraContext& ctx, int32_t mode, uint16_t delay,
                                              uint32_t transition, uint32_t arg4) {
    const uint32_t fn = tables->mode(mode).update.fn;
    auto tmp = std::make_unique<PlayerCamera>();
    tmp->statics = statics;
    tmp->construct(*tables, ctx);
    tmp->d.mode = mode;
    tmp->SetAnchor(anchor_object());
    tmp->d.mode_change |= 8;
    {
        Reg e, l, o;
        load(e, tmp->d.eye);
        load(l, tmp->d.look_at);
        load(o, tmp->d.offset);
        sub(o, e, l, XYZ);
        store(tmp->d.offset, o);
    }
    tmp->RunModeUpdate(ctx, fn);
    ctx_ = &ctx;
    auto rec = std::make_unique<uint8_t[]>(0x40);
    std::memset(rec.get(), 0, 0x40);  // MEM_fill
    const uint32_t rp = nf::p32_u(rec.get());
    if (arg4 == 1) {
        tmp->AnchorRelativeCamera(ctx, 0, tables->modes.anim_anchor);
        Reg e, l, o;
        load(e, tmp->d.eye);
        load(l, tmp->d.look_at);
        load(o, tmp->d.offset);
        sub(o, e, l, XYZ);
        store(tmp->d.offset, o);
        const float* M = GetAnchorMatrix4(ctx);
        float T[4][4];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) T[r][c] = M[c * 4 + r];
        Reg a{}, b{}, f;
        vu_xform3(a, o, &T[0][0]);
        load(f, tmp->d.matrix + 8);
        vu_xform3(b, f, &T[0][0]);
        std::memcpy(rec.get() + 0x20, b, 12);
        std::memcpy(rec.get() + 0x30, a, 12);
        old_mode_records.push_back(std::move(rec));
        SetCameraModeByIndex(tables->tables.rel_anim_index, delay, 0x1c, uint16_t(transition), 0, rp, nullptr);
    } else {
        std::memcpy(rec.get() + 0x00, tmp->d.matrix + 0, 12);
        std::memcpy(rec.get() + 0x10, tmp->d.matrix + 4, 12);
        std::memcpy(d.look_at_offset, tmp->d.look_at_offset, 16);
        std::memcpy(rec.get() + 0x20, tmp->d.matrix + 8, 12);
        std::memcpy(rec.get() + 0x30, tmp->d.offset, 12);
        rec[0xc] = 1;
        old_mode_records.push_back(std::move(rec));
        SetCameraModeByIndex(tables->tables.world_anim_index, delay, 0x1c, uint16_t(transition), 0, rp, nullptr);
    }
    SetCameraModeByIndex(mode, 0, 4, 0, 0, 0, nullptr);
    statics = tmp->statics;
}

// ---------------------------------------------------------------- as animações relativas ao carro

uint8_t* PlayerCamera::anim_record() {
    if (engine_handle) return nf::p32_ptr<uint8_t>(engine_handle) + 0x40;
    return anim_handle ? anim_handle->bytes + 0x40 : nullptr;
}

// [driving] 0x1bba38 RPlayerCamera::DirectorSetAnchor: a âncora do pedido; com transição (fora da câmera do míssil) o
// olho fica onde estava e a câmera olha na mesma direção a partir dele.
bool PlayerCamera::DirectorSetAnchor(const CameraContext& ctx, const DirectorQueueData& q) {
    CameraAnchor* a = nf::p32_ptr<CameraAnchor>(q.anchor);
    if (!a) return false;
    if (a == anchor_object()) return true;
    if (d.mode == tables->tables.missile_index || q.transition == 0) {
        SetAnchor(a);
        return true;
    }
    Reg o, p, la, eye, off;
    load(o, d.look_at_offset);
    load(p, GetAnchorPosition(ctx));
    load(la, d.look_at);
    add(la, o, p, XYZ);
    store(d.look_at, la);
    load(off, d.offset);
    load(eye, d.eye);
    add(eye, la, off, XYZ);
    store(d.eye, eye);
    SetAnchor(a);
    Reg t;
    load(p, GetAnchorPosition(ctx));
    load(eye, d.eye);
    std::memcpy(t, eye, 16);
    sub(t, eye, p, XYZ);
    const float len = vu_length(t);
    d.distance = len;
    Reg r2, r3;
    load(r2, d.matrix + 8);
    load(r3, d.matrix + 12);
    load(la, d.look_at);
    Reg s;
    std::memcpy(s, la, 16);
    vu_scale3(s, r2, len);
    add(la, r3, s, XYZ);
    store(d.look_at, la);
    load(p, GetAnchorPosition(ctx));
    load(o, d.look_at_offset);
    sub(o, la, p, XYZ);
    store(d.look_at_offset, o);
    load(eye, d.eye);
    load(off, d.offset);
    sub(off, eye, la, XYZ);
    store(d.offset, off);
    return true;
}

// [driving] 0x1bc9f0 RPlayerCamera::TriggerCarAnimationCamera: o pedido do modo RelativeAnim com a âncora e o número da
// animação (as bandeiras 0x44: espera a vez, a animação da lista do modelo da âncora).
void PlayerCamera::TriggerCarAnimationCamera(uint32_t anim_id, uint16_t transition, uint16_t delay,
                                             CameraAnchor* anchor, uint16_t flags) {
    ++car_anim_requests;
    ResetStateForAnimation();
    // [driving] 0x1aa6a8 RDirectorQueueData::RDirectorQueueData(espera, modo, transição, bandeiras, +0x18, +0x20, +0x24)
    DirectorQueueData q{};
    q.vector[3] = 1.0f;
    q.delay = delay;
    q.mode = uint16_t(int32_t(tables->tables.rel_anim_index));
    q.transition = transition;
    q.flags = uint16_t(flags | 0x44);
    q.data18 = 0;
    q.anchor = nf::p32_u(anchor);
    q.data24 = anim_id;
    q.data1c = 0;
    AppendDirectorData(q);  // [driving] 0x1ba3f8 SetCameraModeByQueueData
}

// [driving] 0x1d6860 RWorldCamera::LoadSingleAnimationFromList: o registro da lista do modelo (+0x108) cuja primeira
// animação é do sistema `anim_id`, num Handle novo do RAnimEngine.
bool PlayerCamera::LoadSingleAnimationFromList(uint32_t anim_id) {
    if (d.camera_anims == 0) return false;
    const uint32_t n = d.camera_anim_count;
    auto system_of = [&](uint32_t i, bool& ok) -> uint32_t {
        const uint32_t rec = d.camera_anims + i * 0x40;
        const uint32_t art = r32(rec, 0x1c);
        const uint32_t anims = art ? r32(art, 8) : 0;
        if (!anims) {
            ok = false;  // (o PS2 leria o endereço 0)
            ++guards;
            return 0;
        }
        ok = true;
        return r16(anims, 4);
    };
    uint32_t i = 0;
    for (; i < n; ++i) {
        bool ok;
        const uint32_t s = system_of(i, ok);
        if (ok && s == anim_id) break;
    }
    if (i >= n) return false;
    anim_handle.reset();
    if (engine_handle) AnimHandle_Destroy(engine_handle);
    engine_handle = AnimHandle_Create(1, rtime_data().time, d.camera_anims + i * 0x40, 0, 0);
    d.anim_handle = engine_handle ? 1u : 0u;
    if (!engine_handle) return false;
    d.anim_system = anim_id;
    return true;
}

// [driving] 0x1d61b8 RWorldCamera::UpdateAnimationCam(MATRIX4 &, char)
int32_t PlayerCamera::UpdateAnimationCam(float out[4][4], int32_t flags) {
    if (!engine_handle) return 0;  // (o Handle de um registro sem animação não tem sistema: o PS2 dá 0 também)
    const uint32_t inst = AnimHandle_GetFirstSystemInstance(engine_handle, d.anim_system);
    if (!inst) return 0;
    const uint32_t art = r32(inst, 0x1c);
    if (!art) return int32_t(inst);  // (o PS2 devolve o ponteiro da instância)
    const uint8_t* ip = nf::p32_ptr<uint8_t>(inst);
    const uint32_t ai = r32(art, 8) + uint32_t(ip[0xd]) * 0x10;
    if (ip[0xc] & 0x10) return 0;
    const int32_t playing = AnimHandle_IsSystemPlaying(engine_handle, d.anim_system) ? 1 : 0;
    if (playing != 1) return playing;
    const uint8_t* ap = nf::p32_ptr<uint8_t>(ai);
    uint16_t tick;
    std::memcpy(&tick, ip + 0xe, 2);
    const float t = ee::div(ee::cvt_s(int32_t(uint32_t(tick) * uint32_t(ap[0xf]))), 60.0f);
    int32_t whole = ee::cvt_w(t);
    int32_t adjust = 0;
    if (ee::lt(t, 0.0f) && !ee::eq(t, ee::cvt_s(whole))) adjust = 1;
    uint32_t k0 = uint32_t(whole - adjust);
    const float frac = ee::sub(t, u2f(k0));
    const uint16_t count = r16(ai, 8);
    const uint32_t keys = r32(ai, 0);
    if (!(k0 < count)) k0 = uint32_t(count) - 1;
    uint32_t k1 = k0 + 1;
    if (!(k1 < count)) k1 = uint32_t(count) - 1;
    float q[4];
    anim::fast_qslerp(nf::p32_ptr<float>(keys + (k0 << 5)), nf::p32_ptr<float>(keys + (k1 << 5)), q, frac);
    VU0_quattom4(q, out);
    const Reg m1 = {kMinusOne, 0, 0, 0};
    if (flags & 2) {
        Reg r;
        load(r, out[0]);
        mulbc(r, r, m1, BX, XYZW);
        store(out[0], r);
        float tmp[4];
        std::memcpy(tmp, out[2], 16);
        load(r, out[1]);
        mulbc(r, r, m1, BX, XYZW);
        store(out[2], r);
        load(r, tmp);
        mulbc(r, r, m1, BX, XYZW);
        store(out[1], r);
    } else {
        Reg r1, r2, zero{};
        load(r1, out[1]);
        load(r2, out[2]);
        sub(r1, zero, r1, XYZ);
        store(out[1], r2);
        store(out[2], r1);
    }
    {
        Reg a, b, e;
        load(a, nf::p32_ptr<float>(keys + (k0 << 5) + 0x10));
        load(b, nf::p32_ptr<float>(keys + (k1 << 5) + 0x10));
        load(e, d.eye);
        vu_lerp3(e, a, b, frac);
        store(d.eye, e);
    }
    if (flags & 1) {
        out[0][0] = ee::neg(out[0][0]);
        out[1][0] = ee::neg(out[1][0]);
        out[2][0] = ee::neg(out[2][0]);
        d.eye[0] = ee::neg(d.eye[0]);
        Reg r;
        load(r, out[0]);
        mulbc(r, r, m1, BX, XYZW);
        store(out[0], r);
    }
    d.eye[3] = 1.0f;
    std::memcpy(out[3], d.eye, 16);
    WorldSetCameraZoom(33.0f, 1.5f);
    return playing;
}

// [driving] 0x1b5998 RPlayerCamera::UpdateRelativeAnimationCam: a câmera de um registro 'Cams' do modelo do carro,
// relativa ao carro (o referencial da frente do carro no plano), com a transição pela curva do RCameraSpline.
// Divergências: ver o cabeçalho (as linhas de lixo da pilha e o s6 do chamador).
void PlayerCamera::UpdateRelativeAnimationCam(const CameraContext& ctx) {
    if (ctx.sim->state == 3) return;
    if (d.anim_handle == 0) return;
    uint8_t* rec = anim_record();
    if (!rec) return;
    ++rel_anim_updates;
    const float* M = GetAnchorMatrix4(ctx);
    float (*m310)[4] = reinterpret_cast<float (*)[4]>(d.unknown310);
    float gen[4][4] = {};
    {
        const float fwd[4] = {M[8], 0.0f, M[10], 0.0f};
        RCameraMath_VU0_GenerateMatrix4(fwd, gen);
    }
    float old_eye[4];
    std::memcpy(old_eye, d.eye, 16);
    const uint32_t art = r32(nf::p32_u(rec), 0x1c);
    if (!spline) spline = std::make_unique<PlayerCameraSpline>();
    if (d.mode_change & 1) {
        if (DoSmoothModeChange() && d.transition_override != 0) {
            float p1[4] = {}, p1b[4] = {}, dir1[4] = {};
            float s1m[4][4] = {};  // o s1 (sp90): ver o cabeçalho
            const Reg m1 = {kMinusOne, 0, 0, 0};
            if (art) {
                const uint32_t ai = r32(art, 8) + uint32_t(rec[0xd]) * 0x10;
                const uint32_t keys = r32(ai, 0);
                std::memcpy(p1, nf::p32_ptr<uint8_t>(keys) + 0x10, 16);
                if (r16(ai, 8) < 2) std::memcpy(p1b, p1, 16);
                else std::memcpy(p1b, nf::p32_ptr<uint8_t>(keys) + 0x30, 16);
                VU0_quattom4(nf::p32_ptr<float>(keys), m310);
                Reg r;
                load(r, m310[0]);
                mulbc(r, r, m1, BX, XYZW);
                store(m310[0], r);
                float tmp[4];
                std::memcpy(tmp, m310[2], 16);
                load(r, m310[1]);
                mulbc(r, r, m1, BX, XYZW);
                store(m310[2], r);
                load(r, tmp);
                mulbc(r, r, m1, BX, XYZW);
                store(m310[1], r);
                std::memcpy(s1m[0], tmp, 16);
                if (!ee::eq(d.car_angle, 0.0f)) {
                    float yr[4][4] = {};
                    VU0_MATRIX4setyrot(yr, cam_wrap_turns(d.car_angle));
                    Reg v;
                    load(v, p1b);
                    vu_xform3(v, v, &yr[0][0]);
                    store(p1b, v);
                    load(v, p1);
                    vu_xform3(v, v, &yr[0][0]);
                    store(p1, v);
                    VU0_MATRIX4_mult(m310, yr, m310);
                    std::memcpy(s1m[1], yr[0], 16);
                    std::memcpy(s1m[2], yr[1], 16);
                    std::memcpy(s1m[3], yr[2], 16);
                }
                Reg a, b, t{};
                load(a, p1b);
                load(b, p1);
                sub(t, a, b, XYZ);
                store(dir1, t);
            } else {
                std::memcpy(m310, rec, 64);
                m310[3][3] = 1.0f;
                m310[0][3] = 0.0f;
                m310[1][3] = 0.0f;
                m310[2][3] = 0.0f;
                std::memcpy(p1, m310[3], 16);
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c) s1m[r][c] = gen[c][r];
            }
            float p0[4] = {};
            {
                Reg a, b, v;
                load(a, d.offset);
                load(b, d.look_at_offset);
                load(v, p0);
                add(v, a, b, XYZ);
                const float ro = GetAnchorRenderOffset();
                Reg u, s;
                load(u, gen[1]);
                std::memcpy(s, v, 16);
                vu_scale3(s, u, ro);
                add(v, v, s, XYZ);
                vu_xform3(v, v, &s1m[0][0]);
                store(p0, v);
            }
            float dir0[4];
            std::memcpy(dir0, d.velocity, 16);
            {
                Reg a, b, t{}, v;
                load(a, p1);
                load(b, p0);
                sub(t, a, b, XYZ);
                load(v, dir0);
                if (ee::lt(vu_dot3(v, t), 0.0f)) {
                    vu_scale3(v, v, -1.0f);
                    store(dir0, v);
                }
                dir0[1] = ee::abs(dir0[1]);
            }
            float m100[4][4], q0[4], q1[4];
            {
                float cm[4][4];
                std::memcpy(cm, d.matrix, 64);
                std::memset(m100, 0, sizeof m100);
                VU0_MATRIX3x4_mult(cm, s1m, m100);
            }
            VU0_m4toquat(m100, q0);
            VU0_m4toquat(m310, q1);
            spline->ClearSplinePtList();
            spline->BuildSpline(0.5f, p0, dir0, p1, dir1, q0, q1);
            d.transition = d.transition_override;
            d.unknown2e0[3] = 0.0f;
            d.distance_rate = ee::div(1.0f, u2f(uint32_t(d.transition_override)));
        } else {
            PlayCurrentAnimation(ctx);
        }
    }
    int32_t s6 = 0;
    if (d.transition > 0) {
        d.unknown2e0[3] = ee::add(d.unknown2e0[3], d.distance_rate);
        spline->EvaluateSpline(d.unknown2e0[3], m310, 1);
        d.transition -= 1;
        if (d.transition == 0) s6 = art ? (PlayCurrentAnimation(ctx) ? 1 : 0) : 1;
        else s6 = 1;
    } else if (art) {
        s6 = UpdateAnimationCam(m310, 2);
        if (s6 == 1 && !ee::eq(d.car_angle, 0.0f)) {
            float yr[4][4] = {};
            VU0_MATRIX4setyrot(yr, cam_wrap_turns(d.car_angle));
            Reg v;
            load(v, m310[3]);
            vu_xform3(v, v, &yr[0][0]);
            store(m310[3], v);
            VU0_MATRIX4_mult(m310, yr, m310);
        }
    }
    AnchorRelativeCamera(ctx, 0, tables->modes.anim_anchor);
    {
        Reg o, r3;
        load(o, d.offset);
        load(r3, m310[3]);
        vu_xform3(o, r3, &gen[0][0]);
        store(d.offset, o);
    }
    d.offset[3] = 1.0f;
    {
        Reg o, la, eye;
        load(o, d.offset);
        load(la, d.look_at);
        load(eye, d.eye);
        add(eye, o, la, XYZ);
        store(d.eye, eye);
    }
    {
        float cm[4][4];
        std::memcpy(cm, d.matrix, 64);
        VU0_MATRIX3x4_mult(m310, gen, cm);
        std::memcpy(d.matrix, cm, 64);
    }
    d.matrix[12] = d.eye[0];
    d.matrix[13] = d.eye[1];
    d.matrix[14] = d.eye[2];
    d.matrix[15] = 1.0f;
    d.matrix[3] = 0.0f;
    d.matrix[7] = 0.0f;
    d.matrix[11] = 0.0f;
    d.matrix_changed = 1;
    if (s6 == 1) {
        director_flags |= 1;
    } else {
        director_flags &= 0xfe;
        Reg o;
        load(o, d.offset);
        const float dist = vu_length(o);
        d.distance = dist;
        Reg r2, r3, la, s;
        load(r2, d.matrix + 8);
        load(r3, d.matrix + 12);
        load(la, d.look_at);
        std::memcpy(s, la, 16);
        vu_scale3(s, r2, dist);
        add(la, r3, s, XYZ);
        store(d.look_at, la);
        Reg p, lo;
        load(p, GetAnchorPosition(ctx));
        load(lo, d.look_at_offset);
        sub(lo, la, p, XYZ);
        store(d.look_at_offset, lo);
        Reg eye, off;
        load(eye, d.eye);
        load(off, d.offset);
        sub(off, eye, la, XYZ);
        store(d.offset, off);
    }
    {
        Reg v, a, e;
        load(v, d.velocity);
        load(a, old_eye);
        load(e, d.eye);
        sub(v, a, e, XYZ);
        vu_scale3(v, v, ee::cvt_s(ctx.sim->frame_rate));
        store(d.velocity, v);
    }
}

}  // namespace drv
