// Os eventos de câmera da missão (driving/game/camera_events.h), portados do MIPS do DRIVING.ELF.

#include "driving/game/camera_events.h"

#include "driving/camera/player_camera.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

CameraEventsEnv g_env;

// (float)(uint32_t) como o GCC do PS2 converte.
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(f, f);
}
float flt(uint32_t b) { return ps2::ffloat(b); }

// Os quadros dos eventos (de 60 por segundo para a taxa da simulação): acima de 1, x taxa / 60 (inteiro); `min_one`: os
// outros viram 1.
uint32_t frames(uint32_t v, bool min_one) {
    if (ee::lt(1.0f, u2f(v))) return uint32_t(ee::cvt_w(ee::div(u2f(v * uint32_t(simulation().frame_rate)), 60.0f)));
    return min_one ? 1u : v;
}

}  // namespace

CameraEventsEnv& camera_events_env() { return g_env; }

void camera_events_autodrive_on(float yaw_turns, float pitch_turns) {
    if (g_env.camera) g_env.camera->SetAutoDriveCamera(*g_env.ctx, yaw_turns, pitch_turns, 1);
}

void camera_events_last_selectable(uint16_t delay, uint32_t transition, uint32_t arg4, int32_t keep_autodrive) {
    if (g_env.camera) g_env.camera->SetLastSelectableCameraMode(delay, transition, arg4, keep_autodrive);
}

void camera_events_end_anim(uint16_t delay, uint32_t transition, uint32_t arg4, int16_t cam_id) {
    if (g_env.camera) g_env.camera->EndCameraAnim(*g_env.ctx, delay, transition, arg4, cam_id);
}

void camera_events_lock_off() {
    if (g_env.camera) g_env.camera->CameraLockOn(nullptr, nullptr, 0, 0);
}

void camera_events_control_to_cpu(uint32_t on) {
    if (g_env.camera) g_env.camera->SetControlToCPU(on);
}

bool camera_event_execute(const PendingEvent& e) {
    switch (e.type) {
        case 63:
        case 204:
        case 242:
        case 246:
        case 251:
        case 207:
            break;
        case 97:
            ++g_env.car_anims;  // [driving] 0x153c48 EPlayCarCameraAnim: não portado
            return false;
        default:
            return false;
    }
    uint32_t o[16];
    for (int i = 0; i < 16; ++i) o[i] = e.obj[i];
    PlayerCamera* cam = g_env.camera;
    if (!cam) ++g_env.without_camera;
    else ++g_env.executed;
    switch (e.type) {
        // [driving] 0x153700 EPlayCameraAnim::~EPlayCameraAnim
        case 63: {
            o[3] = frames(o[3], false);
            o[2] = frames(o[2], true);
            if (cam)
                cam->TriggerAnimationCamera(*g_env.ctx, nf::p32_ptr<const uint8_t>(o[1]), uint16_t(o[2]), uint16_t(o[3]),
                                            o[4] != 0 ? 1 : 0, int32_t(o[5]));
            SMissionManager_SetCinematicMode(mission_manager(), 1);
            return true;
        }
        // [driving] 0x1539a8 EPlayCameraSpline::~EPlayCameraSpline
        case 204: {
            o[3] = frames(o[3], false);
            o[2] = frames(o[2], true);
            if (cam) cam->TriggerAIPathAnimationCamera(*g_env.ctx, o[1], uint16_t(o[2]), uint16_t(o[3]), int32_t(o[4]));
            SMissionManager_SetCinematicMode(mission_manager(), 1);
            return true;
        }
        // [driving] 0x148e30 ECameraLockOn::~ECameraLockOn
        case 242: {
            const float offset[4] = {flt(o[7]), flt(o[5]), flt(o[6]), 0.0f};
            const float* target = nf::p32_ptr<const float>(o[2]);
            if (!target && o[1] != 0) {
                // O AIElementController::RedirectCarpElement e a posição viva do elemento: no port, o começo do registro.
                target = nf::p32_ptr<const float>(o[1]);
            }
            if (cam) cam->CameraLockOn(target, offset, o[8], int32_t(o[9]));
            return true;
        }
        // [driving] 0x149338 ECameraZoom::~ECameraZoom
        case 246: {
            o[2] = frames(o[2], true);
            if (cam) cam->SetCameraZoom(2, ee::mul(u2f(o[1]), ps2::ffloat(0x3c23d70au)), int32_t(o[2]));
            return true;
        }
        // [driving] 0x149118 ECameraShake::~ECameraShake
        case 251: {
            if (cam)
                cam->ShakeCamera(u2f(o[2]), int32_t(o[1]), cam->d.matrix + 12, int32_t(o[3]), simulation().frame_rate);
            return true;
        }
        // [driving] 0x149528 EChangeCarCameraView::~EChangeCarCameraView
        case 207: {
            o[3] = frames(o[3], false);
            o[2] = frames(o[2], true);
            if (cam) cam->ForceCameraChange(int16_t(o[1]), uint16_t(o[3]), o[2]);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace drv
