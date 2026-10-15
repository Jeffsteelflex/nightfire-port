// A mira pela câmera do jogador (driving/camera/camera_aim.h), do MIPS do DRIVING.ELF (RPlayerCamera.obj). Conferido na
// parte 20 do nf_driving_check (tests/driving_check_integ_ai.cpp).
//
// Divergência: o RPlayerCamState (+0x354) do port é um membro do PlayerCamera, sempre presente (o PS2 o cria no
// construtor e nunca o apaga durante a missão): o CameraAiming não vê o ponteiro nulo.

#include "driving/camera/camera_aim.h"

#include <cstring>

#include "driving/camera/camera_math.h"
#include "driving/camera/player_camera.h"
#include "driving/game/bond_car.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;

namespace {
PlayerCamera* g_camera = nullptr;
alignas(16) float g_forward[4];   // 0x3d1ce0
alignas(16) float g_aim[4][4];    // 0x3d1cf0
}  // namespace

PlayerCamera*& current_player_camera() { return g_camera; }

// [driving] 0x001bcca0 RPlayerCamera::GetForwardAimVec4
const float* RPlayerCamera_GetForwardAimVec4(PlayerCamera& cam, float t) {
    PlayerCameraData& d = cam.d;
    if (ee::eq(t, 1.0f)) return d.forward;
    if (ee::eq(t, 0.0f)) return &d.matrix[8];
    // vsub.xyz, vmulx.xyz (t no x de um registro), vadd.xyz: o w do vetor estático fica.
    using namespace ps2::vu;
    Reg a, b, r, k = {ps2::fbits(t), 0, 0, 0};
    load(a, &d.matrix[8]);
    load(b, d.forward);
    load(r, g_forward);
    sub(b, b, a, XYZ);
    mulbc(b, b, k, BX, XYZ);
    add(r, a, b, XYZ);
    store(g_forward, r);
    return g_forward;
}

// [driving] 0x001bcd10 RPlayerCamera::GetAimMatrix4
const float* RPlayerCamera_GetAimMatrix4(PlayerCameraData& d, float a, float b) {
    if (ee::eq(a, 1.0f) && ee::eq(b, 1.0f)) return d.matrix_copy;
    if (ee::eq(a, 0.0f) && ee::eq(b, 1.0f)) return d.matrix;
    Simulation& sim = simulation();
    const RigidBody* rb = sim.cars.empty() ? nullptr : GetRigidBody(sim, sim.cars[0]->d.slot);
    alignas(16) float row0[4] = {}, row1[4] = {};
    if (rb && rb->info) {
        std::memcpy(row0, &rb->info->orient[0][0], 16);
        std::memcpy(row1, &rb->info->orient[1][0], 16);
    }
    alignas(16) float m1[4][4], m2[4][4], t[4][4];
    RCameraMath_BuildRotationMat4(ee::mul(b, d.dz_pitch), m1, row0);
    RCameraMath_BuildRotationMat4(ee::mul(a, d.dz_yaw), m2, row1);
    VU0_MATRIX4_mult(m1, reinterpret_cast<const float(*)[4]>(d.matrix), g_aim);
    std::memcpy(t, g_aim, 64);
    VU0_MATRIX4_mult(m2, t, g_aim);
    return &g_aim[0][0];
}
const float* RPlayerCamera_GetAimMatrix4(PlayerCamera& cam, float a, float b) {
    return RPlayerCamera_GetAimMatrix4(cam.d, a, b);
}

// [driving] 0x001bd2f8 RPlayerCamera::CameraAiming
uint32_t RPlayerCamera_CameraAiming(const PlayerCamera& cam) { return cam.state.zoom_held; }

// [driving] 0x001a4468 RCamera::CreateMatrix4Inv
void RCamera_CreateMatrix4Inv(PlayerCameraData& d) {
    if (d.matrix_changed == 0) return;
    d.matrix_changed = 0;
    // VU0_MATRIX4_3x3transpose (0x002cc498) de +0x00 para +0x60 (outra matriz: as palavras da diagonal, os w e a linha 3
    // copiados).
    const float* m = d.matrix;
    float* inv = d.inverse;
    inv[1] = m[4];
    inv[2] = m[8];
    inv[4] = m[1];
    inv[6] = m[9];
    inv[8] = m[2];
    inv[9] = m[6];
    std::memcpy(inv + 12, m + 12, 16);
    inv[0] = m[0];
    inv[3] = m[3];
    inv[5] = m[5];
    inv[7] = m[7];
    inv[10] = m[10];
    inv[11] = m[11];
    // vmulx.xyz por -1, vmulax/vmadday/vmaddz.xyz pelas linhas da transposta: o w da linha 3 fica.
    using namespace ps2::vu;
    Reg t, k = {0xbf800000u, 0, 0, 0}, r0, r1, r2, acc, o;
    load(t, m + 12);
    mulbc(t, t, k, BX, XYZ);
    load(r0, inv);
    load(r1, inv + 4);
    load(r2, inv + 8);
    load(o, inv + 12);
    mulabc(acc, r0, t, BX, XYZ);
    maddabc(acc, r1, t, BY, XYZ);
    maddbc(o, acc, r2, t, BZ, XYZ);
    store(inv + 12, o);
}

}  // namespace drv
