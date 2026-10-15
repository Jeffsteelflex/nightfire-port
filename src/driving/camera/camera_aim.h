#pragma once
// A mira pela câmera do jogador (docs/driving.md seção 24): o que as armas, o WTargetPicker e o EFireWeapon perguntam ao
// RPlayerCamera (o de 0x333ed8 +4) na direção automática. Portado do MIPS do DRIVING.ELF (RPlayerCamera.obj).

#include <cstdint>

namespace drv {

class PlayerCamera;
struct PlayerCameraData;

// O RPlayerCamera do jogo (DAT_00333ed8 +4): o nf_driving o põe aqui (nulo sem câmera, como o PS2 antes da missão).
PlayerCamera*& current_player_camera();

// [driving] 0x001bcca0 RPlayerCamera::GetForwardAimVec4(float t): t = 1 a frente guardada (+0x170), t = 0 a linha 2 do
// referencial (+0x20), senão a mistura das duas num vetor estático (0x3d1ce0, como o PS2: vale até a próxima chamada).
const float* RPlayerCamera_GetForwardAimVec4(PlayerCamera& cam, float t);
// [driving] 0x001bcd10 RPlayerCamera::GetAimMatrix4(float a, float b): (1, 1) a matriz guardada (+0x180), (0, 1) o
// referencial (+0x00); senão o referencial girado b x a zona morta (+0x1d0) em torno da linha 0 do carro do jogador e
// a x (+0x1d4) em torno da linha 1, numa matriz estática (0x3d1cf0).
const float* RPlayerCamera_GetAimMatrix4(PlayerCameraData& d, float a, float b);
const float* RPlayerCamera_GetAimMatrix4(PlayerCamera& cam, float a, float b);
// [driving] 0x001bd2f8 RPlayerCamera::CameraAiming: o +0x24 do RPlayerCamState (+0x354; o zoom da mira), 0 sem estado.
uint32_t RPlayerCamera_CameraAiming(const PlayerCamera& cam);
// [driving] 0x001a4468 RCamera::CreateMatrix4Inv: com o +0x50 ligado (a matriz mudou), a inversa em +0x60 (a rotação
// transposta pelo VU0_MATRIX4_3x3transpose 0x002cc498, a linha 3 copiada e o xyz dela = -t pela transposta) e o +0x50
// apagado.
void RCamera_CreateMatrix4Inv(PlayerCameraData& d);

}  // namespace drv
