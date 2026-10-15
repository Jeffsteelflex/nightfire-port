#pragma once
// Os eventos de câmera da missão (docs/driving.md seção 23): os destrutores do PS2 que falam com o RPlayerCamera do
// mundo (o 0x333ed8 +4), portados do MIPS do DRIVING.ELF:
//   63 EPlayCameraAnim (0x153700: TriggerAnimationCamera e o modo de cinema 1),
//  204 EPlayCameraSpline (0x1539a8: TriggerAIPathAnimationCamera e o modo de cinema 1),
//  242 ECameraLockOn (0x148e30: CameraLockOn no ponto de um gatilho ou de um elemento da IA),
//  246 ECameraZoom (0x149338: SetCameraZoom do tipo 2),
//  251 ECameraShake (0x149118: ShakeCamera em volta da própria câmera),
//  207 EChangeCarCameraView (0x149528: ForceCameraChange);
// e o que os eventos que o events.cpp já executava pedem à câmera (o AutoDrive dos 76/77/233/234, o EndCameraAnim do 68,
// o CameraLockOn(0) da vitória e da derrota).
//
// Quem tem a câmera (o nf_driving) a liga em camera_events_env(); sem ela (o nf_driving_check, parte 15) os eventos só
// convertem os números como o PS2 e contam. O 97 EPlayCarCameraAnim (TriggerCarAnimationCamera: as animações 'Cams' do
// modelo do carro-alvo, nenhum em Paris; o carro de um elemento da IA) não foi portado: conta como sem porte.
// Divergência: o ECameraLockOn num elemento da IA (o PS2 pergunta ao AIElementController, não portado, pela posição viva
// do veículo ou do personagem) prende a câmera no ponto de partida do registro 'AIEl' (as três primeiras palavras).

#include <cstdint>

namespace drv {

class PlayerCamera;
struct CameraContext;
struct PendingEvent;

struct CameraEventsEnv {
    PlayerCamera* camera = nullptr;
    CameraContext* ctx = nullptr;
    // Contadores do port.
    uint64_t executed = 0, without_camera = 0, car_anims = 0;
};
CameraEventsEnv& camera_events_env();

// Os destrutores 63, 204, 242, 246, 251, 207 (e o 97, contado): devolve se o número é um destes.
bool camera_event_execute(const PendingEvent& e);

// O que os eventos do events.cpp pedem (nada sem a câmera): [driving] 0x1bb7c0 SetAutoDriveCamera(rumo, inclinação, 1)
// (os ângulos já em voltas), 0x1bb1f8 SetLastSelectableCameraMode, 0x1bb098 EndCameraAnim, 0x1bc400 CameraLockOn(0),
// 0x1bd198 SetControlToCPU.
void camera_events_autodrive_on(float yaw_turns, float pitch_turns);
void camera_events_last_selectable(uint16_t delay, uint32_t transition, uint32_t arg4, int32_t keep_autodrive);
void camera_events_end_anim(uint16_t delay, uint32_t transition, uint32_t arg4, int16_t cam_id);
void camera_events_lock_off();
void camera_events_control_to_cpu(uint32_t on);

}  // namespace drv
