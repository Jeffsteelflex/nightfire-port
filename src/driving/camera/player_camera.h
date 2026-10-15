#pragma once
// A câmera do jogador do DRIVING.ELF: RPlayerCamera (0x370 bytes no PS2; RCamera 0xb0, RWorldCamera até 0x120), a
// câmera de perseguição das missões de carro. Portada do MIPS (docs/driving.md seção 14): o construtor e as duas
// RestartCamera, o diretor (RDirectorQueue: ProcessDirectorLogic, a lista de pedidos de modo, DirectorChangeCameraMode),
// UpdateCamera e o modo Heli (UpdateMomentumHeliCam: o braço de cada modo de camera.ini, o recuo com a velocidade, a
// vertigem nas ladeiras, o ruído, a transição entre modos, a suavização), a colisão com o mundo (ResolveAllCollisions:
// três segmentos do carro para o olho pelo WCollider com a máscara 0xc, o anti-vertigem pelo PointInTriangle, e
// CheckObjectCollisions: a elipse em volta dos outros carros), o tremor das explosões (CheckForCameraShaking, o ruído de
// Perlin do Noise::Noise1), o zoom (SetCameraZoom, SetupCameraZoom) e a matriz de visão (SetViewingTransform).
// O nightfire-cxbx (NightfireResearch: PlayerCamera*.cpp, WorldCamera.cpp, DirectorQueue.cpp, leitura do Xbox) deu os
// nomes dos campos; o leiaute e as contas são os do PS2.
//
// O que não foi portado (conta em camera_pending()): os outros modos (Bumper, Dashboard, Spline, Ellipse, Fixed,
// Tumble, as animações, o AutoDrive: a câmera fica parada neles), a câmera do capotamento (o pedido de Tumble quando o
// carro pode ser reposto: PBondCar::GetResetAvailable ainda é zero), os pedidos do diretor com espera ou com outra
// âncora e os corpos simples perto da câmera. As explosões (driving/game/explosion.h) sacodem a câmera desde a seção 22.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "driving/camera/camera_ini.h"
#include "driving/camera/camera_math.h"
#include "driving/physics/collider.h"
#include "platform/ptr32.h"

namespace drv {

class PhysicsObject;
struct Simulation;
class TrackWorld;

// O que a câmera lê do PhysicsObject âncora (os campos do PS2: +0x40 o tipo, +0x44 as bandeiras, +0x46 a vaga do corpo)
// e as entradas da tabela virtual que ela chama (pelo objeto do port).
struct CameraAnchor {
    uint8_t unknown00[0x40] = {};
    int32_t kind = 0;      // +0x40 (1: um veículo)
    uint16_t flags = 0;    // +0x44 bit 0: um corpo simples
    int16_t body = -1;     // +0x46 a vaga na Simulation
    PhysicsObject* object = nullptr;  // (port)
};

// RPlayerCamState (0x5c bytes, +0x354): o estado do jogador que a câmera consulta.
struct PlayerCamState {
    uint32_t camera;        // +0x00 (p32)
    uint32_t enabled;       // +0x04 1 no construtor
    uint32_t anim;          // +0x08 uma câmera animada (ResetStateForAnimation; EndCameraAnim só age com ele ou +0x0c)
    uint32_t cinematic;     // +0x0c a câmera de cinema (SetCinematicCamera)
    uint32_t cpu_control;   // +0x10 o carro na CPU (SetControlToCPU: EControlToSpline/EControlToCPU)
    uint32_t unknown14;     // +0x14
    uint32_t paused;        // +0x18 a simulação parada (estado 3): no ResetState e a cada entrada do AutoDrive
    uint32_t unknown1c;     // +0x1c 1 no RestartCamera
    uint32_t target_lock;   // +0x20 a mira automática do AutoDrive presa num alvo (UpdateADTargetAngles)
    uint32_t zoom_held;     // +0x24 o zoom da mira (ToggleAutoDriveZoom; o AutoDrive usa a velocidade do cursor do alvo)
    uint32_t centering;     // +0x28 o AutoDrive voltando ao centro (GAMEACTION_CAMCENTRE)
    uint32_t lock_zoom;     // +0x2c a câmera presa num ponto (CameraLockOn)
    uint32_t unknown30;     // +0x30
    float rot_x;            // +0x34 GAMEACTION_CAMROTATEX
    float rot_x2;           // +0x38 GAMEACTION_CAMROTATEX2
    float rot_y;            // +0x3c GAMEACTION_CAMROTATEY / YINV
    float rot_y2;           // +0x40 GAMEACTION_CAMROTATEY2 / Y2INV
    uint32_t colorize;      // +0x44 a câmera do míssil ligou o RColorize
    uint32_t look_back;     // +0x48 1: olhando para trás
    uint32_t unknown4c[2];  // +0x4c, +0x50
    uint32_t up_hold;       // +0x54
    int32_t up_count;       // +0x58
};
static_assert(sizeof(PlayerCamState) == 0x5c, "RPlayerCamState");

// Um pedido do diretor (RDirectorQueueData, 0x28 bytes).
struct DirectorQueueData {
    float vector[4];        // +0x00
    uint16_t delay;         // +0x10 quadros de espera
    uint16_t mode;          // +0x12 o modo pedido
    uint16_t transition;    // +0x14
    uint16_t flags;         // +0x16
    uint32_t data18;        // +0x18
    uint32_t data1c;        // +0x1c
    uint32_t anchor;        // +0x20 (p32) outra âncora
    uint32_t data24;        // +0x24
};
static_assert(sizeof(DirectorQueueData) == 0x28, "RDirectorQueueData");

// O RPlayerCamera do PS2, campo a campo (os ponteiros como palavras: o port guarda os objetos ao lado).
struct PlayerCameraData {
    // RCamera (0xb0)
    float matrix[16];           // +0x000 o referencial (linhas; a posição na linha 3)
    float velocity[4];          // +0x040 o último argumento de SetViewingTransform (a velocidade da âncora)
    uint32_t matrix_changed;    // +0x050
    uint8_t unknown054[0xc];
    float inverse[16];          // +0x060 (CreateMatrix4Inv)
    uint32_t active;            // +0x0a0
    float fov;                  // +0x0a4 em graus
    uint32_t vtable;            // +0x0a8
    uint32_t unknown0ac;
    // RWorldCamera (até 0x120)
    float eye[4];               // +0x0b0
    float look_at[4];           // +0x0c0
    float look_at_offset[4];    // +0x0d0 (AnchorCamera: o deslocamento da âncora no mundo)
    uint32_t mode_change;       // +0x0e0 1 âncora/reinício, 2 olhar para trás, 4 forçar suave, 8 sem suavizar
    float zoom_fov;             // +0x0e4
    float zoom_fov_target;      // +0x0e8
    uint32_t unknown0ec;
    float unknown0f0[4];        // +0x0f0 (0, 0, 0, 1) no reinício
    uint32_t anim_system;       // +0x100
    uint32_t anim_handle;       // +0x104
    uint32_t camera_anims;      // +0x108
    uint32_t camera_anim_count; // +0x10c
    uint32_t ai_spline;         // +0x110
    uint32_t input_queue;       // +0x114
    uint32_t anchor;            // +0x118 (no port: 1 com âncora)
    uint32_t unknown11c;
    // RPlayerCamera
    int32_t mode;               // +0x120 o modo corrente (índice na tabela de modos)
    int32_t prev_mode;          // +0x124
    int32_t prev_selectable;    // +0x128
    int32_t transition;         // +0x12c quadros de transição que faltam
    int32_t tumble;             // +0x130
    int32_t unknown134;         // +0x134 1: a taxa fixa em 0,1
    int32_t unknown138;
    int32_t last_step;          // +0x13c o passo da simulação da última atualização
    uint32_t lock_target;       // +0x140 (p32 COORD4) o ponto em que o AutoDrive se prende (CameraLockOn)
    uint8_t unknown144[0xc];
    float lock_offset[4];       // +0x150 o deslocamento do ponto (x na direita, y em cima, z na frente da âncora)
    int32_t lock_count;         // +0x160 os quadros que faltam da virada para o ponto
    int32_t lock_total;         // +0x164
    int32_t unknown168;         // +0x168 diferente de zero: o AutoDrive preso ao carro (nenhum código o liga)
    int32_t unknown16c;
    float forward[4];           // +0x170 a linha 2 do referencial (no reinício e no AutoDrive)
    float matrix_copy[16];      // +0x180
    float cursor[2];            // +0x1c0 o cursor da mira na tela (pixels a partir do centro)
    float unknown1c8[2];
    float dz_pitch;             // +0x1d0 a zona morta do AutoDrive (voltas)
    float dz_yaw;               // +0x1d4
    float ad_pitch;             // +0x1d8 a inclinação do AutoDrive (voltas)
    float ad_yaw;               // +0x1dc o rumo do AutoDrive relativo ao carro (voltas)
    float accel_yaw;            // +0x1e0 a aceleração do giro pelo controle (rumo)
    float accel_pitch;          // +0x1e4 (inclinação)
    int32_t weap_change_step;   // +0x1e8 o passo do começo da troca de braço
    int32_t weap_change;        // +0x1ec a troca de braço (de arma) em andamento
    int32_t unknown1f0;
    int32_t weapon_anim_step;   // +0x1f4 o passo do tiro (WeaponFired): o tranco da mira
    int32_t spin;               // +0x1f8 os quadros do giro (InitSpin; só snow1a_mis3)
    int32_t unknown1fc;
    float unknown200[4];        // +0x200 o eixo do giro (o w: o fator 0x3f7fc19f)
    int32_t unknown210, unknown214;
    int32_t collision;          // +0x218 o que ResolveAllCollisions devolve
    float distance;             // +0x21c a distância do braço
    float distance_rate;        // +0x220
    uint8_t unknown224[0xc];
    float noise[4];             // +0x230
    float unknown240[4];        // +0x240
    int32_t unknown250, unknown254;
    float vertigo;              // +0x258 o seno da inclinação do braço
    float rate;                 // +0x25c a suavização do braço
    float car_angle;            // +0x260 o ângulo entre a frente do carro e a da câmera no plano (voltas; AutoDrive)
    int32_t unknown264, unknown268;
    int32_t transition_override;  // +0x26c
    int32_t shake_time;         // +0x270
    float shake_amount;         // +0x274
    int32_t shaking;            // +0x278
    uint8_t transition_on;      // +0x27c
    uint8_t transition_first;   // +0x27d
    int8_t arm;                 // +0x27e o braço do modo Heli
    int8_t ad_arm;              // +0x27f o braço do AutoDrive (-1 no reinício)
    uint8_t unknown280;
    uint8_t unknown281[0xf];
    float pitch_rate;           // +0x290 a velocidade do giro do AutoDrive (seguindo +0x2a0 pela inércia)
    float yaw_rate;             // +0x294
    float zoom_fov_max;         // +0x298
    float zoom_fov_min;         // +0x29c
    float target_pitch_rate;    // +0x2a0 SetAutoDriveRotationX (a curva do controle)
    float target_yaw_rate;      // +0x2a4 SetAutoDriveRotationY
    int32_t limited;            // +0x2a8 LimitPitchYaw prendeu o giro no último quadro
    float zoom_slope;           // +0x2ac
    float zoom_intercept;       // +0x2b0
    float zoom_speed;           // +0x2b4
    uint8_t unknown2b8[8];
    float up[4];                // +0x2c0
    float unknown2d0[4];        // +0x2d0
    float unknown2e0[4];        // +0x2e0
    float offset[4];            // +0x2f0 o braço suavizado
    float unknown300[4];        // +0x300
    float unknown310[16];       // +0x310 (a identidade no reinício)
    uint32_t spline;            // +0x350 (RCameraSpline)
    uint32_t state;             // +0x354 (RPlayerCamState)
    uint32_t ini_loader;        // +0x358
    uint32_t collider;          // +0x35c (WCollider; no port: 1 com ele)
    uint32_t world_pos;         // +0x360 (WWorldPos)
    uint32_t road_nav;          // +0x364 (WRoadNav)
    uint32_t director;          // +0x368 (RDirectorQueue)
    uint32_t unknown36c;
};
static_assert(sizeof(PlayerCameraData) == 0x370, "RPlayerCamera");

// Os globais do módulo que a câmera escreve (no PS2 estáticos: 0x333a20 o olho do tremor, 0x333a38 e 0x333a40 o
// anti-vertigem do ResolveAllCollisions, 0x3d76a0 a matriz sem âncora).
struct CameraStatics {
    float shake_eye[4] = {};    // 0x333a20
    float prev_rates[2] = {};   // 0x333a30 as velocidades do giro do AutoDrive no quadro anterior (+0x290, +0x294)
    int32_t vertigo_count = 0;  // 0x333a38
    float vertigo_point[4] = {};  // 0x333a40
    int32_t zoom_button = 0;    // 0x3276e0 AICharacterHands::fgZoomButton (fora do módulo; escrito pela câmera)
};

// O mundo em volta da câmera.
struct CameraContext {
    Simulation* sim = nullptr;
    TrackWorld* world = nullptr;              // nulo: sem colisão com os prédios
    std::vector<CameraAnchor*> cars;          // a lista dos carros da Simulation (+0xdcc); o primeiro é o do jogador
    int32_t current_weapon = -1;              // o gerente de armas (+4); -1: nenhuma
    std::vector<CameraWeaponSlot> weapons;    // as vagas do gerente (FindHeliArmInd, ResolveWeaponNames)
    // O que os modos da seção 23 leem de fora da câmera. O SMissionManager (0x33a024): +0x34c o estado (4: perdeu),
    // +0x350 a direção automática, +0x358 o míssil guiado, +0x3b8 a dificuldade, +0x3cc o modo de cinema, +0x5ec.
    int32_t mission_state = 0;
    int32_t mission_auto_drive = 0;
    uint32_t mission_missile = 0;
    int32_t mission_difficulty = 0;
    uint32_t mission_cinematic = 0;
    int32_t mission_5ec = 0;
    // O bloco da troca (UberBond, 0x100000): +0x4f4 o nome da pista, +0xa40 a mira automática, +0xa44 o zoom pelo
    // botão (as opções do controle).
    std::string track;
    int32_t uber_a40 = 0, uber_a44 = 0;
    // O alvo escolhido pelo WTargetPicker (0x33a970, GetSelectedTarget; nulo: nenhum: o port não tem a mira).
    const float* target = nullptr;
    // O som do carro do jogador (o AVehicle de PBondCar::GetAudio, +0xb8 e +0xbc: o giro da câmera); nulo: nenhum.
    int32_t* audio_turn = nullptr;
    // A tela do RRenderer (fgRenderer +0x50 a largura, +0x54 a altura).
    int32_t screen_w = 640, screen_h = 448;
};

// O RAnimEngine::Handle que o LoadSingleAnimation cria para um registro 'Cams' (0x40 bytes) sem animação (+0x1c nulo):
// o cabeçalho (0x40 bytes: +0x1c a contagem 1) e a cópia do registro em +0x40, como o construtor do PS2 os escreve. As
// câmeras animadas (+0x1c não nulo) precisam do RAnimEngine, não portado: contam em camera_pending().
struct CameraAnimHandle {
    uint8_t bytes[0x80] = {};
};

class PlayerCamera {
public:
    // [driving] 0x1b2620 RPlayerCamera::RPlayerCamera(void): as tabelas (já carregadas por LoadFile) e o reinício.
    void construct(CameraTables& tables, const CameraContext& ctx);
    // [driving] 0x1bbc90 RPlayerCamera::RestartCamera(void)
    void restart(const CameraContext& ctx);
    // [driving] 0x1b29e0 RPlayerCamera::UpdateCamera(void)
    void update(const CameraContext& ctx);

    PlayerCameraData d{};
    PlayerCamState state{};
    CameraStatics statics{};
    std::unique_ptr<WCollider> collider;
    CameraTables* tables = nullptr;
    // A fila do diretor (+0x368: a lista e o byte de bandeiras +8; 1 o modo cinematográfico, 2 recomeçar).
    std::vector<DirectorQueueData> director_queue;
    uint8_t director_flags = 0;
    // Contadores do port.
    uint64_t updates = 0, heli_updates = 0, collisions = 0, ground_hits = 0, barrier_hits = 0, object_hits = 0;

    // As peças (portadas; públicas para o nf_driving_check).
    void UpdateMomentumHeliCam(const CameraContext& ctx);
    void ProcessDirectorLogic(const CameraContext& ctx);
    void DirectorChangeCameraMode(const CameraContext& ctx, const DirectorQueueData& data);
    void SetCameraModeByIndex(int32_t mode);
    // [driving] 0x1ba418 RPlayerCamera::SetCameraModeByIndex(modo, espera, bandeiras, transição, +0x24, +0x18, vetor)
    void SetCameraModeByIndex(int32_t mode, uint16_t delay, uint16_t flags, uint16_t transition, uint32_t data24,
                              uint32_t data18, const float* vector);
    // [driving] 0x1aab80 RDirectorQueue::AppendData (o SetCameraModeByQueueData 0x1ba3f8)
    void AppendDirectorData(const DirectorQueueData& q) { director_queue.push_back(q); }

    // ---- seção 23: o AutoDrive, as câmeras animadas, os eventos de câmera (player_camera_modes.cpp) ----
    // [driving] 0x1bb7c0 SetAutoDriveCamera(float rumo, float inclinação, int: relativos ao carro)
    void SetAutoDriveCamera(const CameraContext& ctx, float yaw, float pitch, int32_t relative);
    // [driving] 0x1bc0e0 SetAutoDriveRotation, 0x1bc390 SetAutoDriveRotationX, 0x1bc3c8 SetAutoDriveRotationY (devolvem
    // +0x2a8), 0x1bc1e8 (sem nome: os dois presos a -1..1)
    float SetAutoDriveRotation(float v) const;
    int32_t SetAutoDriveRotationX(float v);
    int32_t SetAutoDriveRotationY(float v);
    // [driving] 0x1bc4a0 SetAutoDriveZoom, 0x1bc4c0 ToggleAutoDriveZoom, 0x1bd440 InitAimZoom, 0x1bd4a0 ResetZoomSlope
    void SetAutoDriveZoom(float v);
    void ToggleAutoDriveZoom(int32_t on);
    void InitAimZoom();
    void ResetZoomSlope();
    // [driving] 0x1bb1f8 SetLastSelectableCameraMode(espera, transição, ?, bool), 0x1bb340 SetOldCameraMode (só o caminho
    // com transição < 2; o outro monta um RPlayerCamera na pilha: conta), 0x1ba6b0 PrevCameraMode
    void SetLastSelectableCameraMode(uint16_t delay, uint32_t transition, uint32_t arg4, int32_t keep_autodrive);
    void SetOldCameraMode(int32_t mode, uint16_t delay, uint32_t transition, uint32_t arg4);
    void PrevCameraMode(uint16_t delay);
    // [driving] 0x1bd198 SetControlToCPU, 0x1bc400 CameraLockOn(COORD4 *, COORD4 &, unsigned, int), 0x1ba2f8
    // ShakeCamera(float raio, int quadros, COORD4 &centro, int), 0x1bd1b8 ForceCameraChange, 0x1bb098 EndCameraAnim,
    // 0x1bb970 EndMissileCamera, 0x1bd3c8 InitWeaponChange, 0x1bd338 InitSpin, 0x1bd288 WeaponFired
    void SetControlToCPU(uint32_t on) { state.cpu_control = on; }
    void CameraLockOn(const float* target, const float offset[4], uint32_t frames, int32_t keep_zoom);
    void ShakeCamera(float radius, int32_t frames, const float centre[4], int32_t hold, int32_t frame_rate);
    void ForceCameraChange(int16_t cam_id, uint16_t delay, uint32_t transition);
    void EndCameraAnim(const CameraContext& ctx, uint16_t delay, uint32_t transition, uint32_t arg4, int16_t cam_id);
    void EndMissileCamera(const CameraContext& ctx);
    void InitWeaponChange(const CameraContext& ctx);
    void InitSpin(const CameraContext& ctx);
    void WeaponFired(const CameraContext& ctx, int32_t weapon);
    // [driving] 0x1b9cf8 InitTransition, 0x1b9de8 UpdateTransition
    void InitTransition(int32_t smooth, const float target[4], const float from[4], int32_t frames);
    void UpdateTransition(float out[4], const float base[4]);
    // [driving] 0x1baef8 FindAutoDriveArmInd, 0x1b62d0 UpdateADWeaponAnims, 0x1b6848 UpdateADTargetAngles, 0x1b6c60
    // LimitPitchYaw, 0x1b72f0 UpdateAutoDriveCam
    int8_t FindAutoDriveArmInd(const CameraContext& ctx, int32_t mode) const;
    void UpdateADWeaponAnims(const CameraContext& ctx);
    int32_t UpdateADTargetAngles(const CameraContext& ctx, float* pitch, float* yaw);
    int32_t LimitPitchYaw(const AutoDriveArmInfo& arm, float* pitch, float* yaw);
    void UpdateAutoDriveCam(const CameraContext& ctx);
    // [driving] 0x1bcc00 CameraInputCallback (a entrada do IOModule: na configuração do POV, o AutoDriveCamInputHandler
    // 0x1be038; na do carro, o DriveCamInputHandler 0x1bddc8, não portado: conta)
    void CameraInputCallback(const CameraContext& ctx, int32_t action, float value, int32_t config_type);
    void AutoDriveCamInputHandler(const CameraContext& ctx, int32_t action, float value);
    // As câmeras animadas: [driving] 0x1bc510 TriggerAnimationCamera(registro 'Cams', transição, espera, bool, fov),
    // 0x1d6778 RWorldCamera::LoadSingleAnimation, 0x1d6970 PlayCurrentAnimation, 0x1b53c0 UpdateWorldAnimationCam;
    // 0x1bc890 TriggerAIPathAnimationCamera('AISp', transição, espera, fov), 0x1d67f0 LoadAISplinePathAnimation,
    // 0x1b6160 UpdateAIPathAnimationCam
    void TriggerAnimationCamera(const CameraContext& ctx, const uint8_t* cams, uint16_t transition, uint16_t delay,
                                int32_t distribute, int32_t fov);
    bool LoadSingleAnimation(const uint8_t* cams);
    bool PlayCurrentAnimation();
    void UpdateWorldAnimationCam(const CameraContext& ctx);
    void TriggerAIPathAnimationCamera(const CameraContext& ctx, uint32_t ai_spline, uint16_t transition, uint16_t delay,
                                      int32_t fov);
    bool LoadAISplinePathAnimation(uint32_t ai_spline);
    void UpdateAIPathAnimationCam(const CameraContext& ctx);
    // [driving] 0x1bddb0 RPlayerCamState::ResetStateForAnimation
    void ResetStateForAnimation();
    // O braço i do AutoDrive (fora da tabela: um braço zerado, contado em guards; o PS2 leria a memória ao lado).
    const AutoDriveArmInfo& ad_arm_info(int32_t i) const;
    uint64_t guards = 0;

    std::unique_ptr<PlayerCameraSpline> spline;            // +0x350 (d.spline: a presença)
    std::unique_ptr<CameraAnimHandle> anim_handle;   // +0x104 (d.anim_handle: a presença)
    // Os registros que o diretor ainda vai ler (o +0x18 dos pedidos da animação do mundo: no PS2 o ponteiro do registro
    // 'Cams' do CARP; aqui o p32 dele, que já aponta para os dados da pista).
    uint64_t autodrive_updates = 0, anim_updates = 0, ai_path_updates = 0, input_actions = 0;
    int8_t FindHeliArmInd(const CameraContext& ctx, int32_t mode) const;
    void SetupCameraZoom();
    void SetCameraZoom(int32_t kind, float target, int32_t smooth);
    bool DoSmoothModeChange() const;
    int32_t ResolveAllCollisions(const CameraContext& ctx, float eye[4], const float anchor[4], int32_t flag);
    int32_t CheckObjectCollisions(const CameraContext& ctx, float eye[4]);
    void CheckForCameraShaking(const CameraContext& ctx);
    // RWorldCamera
    void RestartWorldCamera(const CameraContext& ctx);
    void SetAnchor(CameraAnchor* anchor);
    bool IsAnchorValid();
    const float* GetAnchorMatrix4(const CameraContext& ctx);
    const float* GetAnchorPosition(const CameraContext& ctx);
    float GetAnchorSpeed(const CameraContext& ctx);
    const float* GetAnchorLinearVelocity(const CameraContext& ctx);
    bool GetAnchorResetAvailable();
    void AnchorCamera(const CameraContext& ctx, int32_t smooth, const float offset[3]);
    void SetViewingTransform(const float up[4], const float lift[2], const float* velocity);
    void WorldSetCameraZoom(float target, float step);

    CameraAnchor* anchor_object() const { return anchor_; }

private:
    CameraAnchor* anchor_ = nullptr;  // +0x118 (o campo do PS2 guarda só a presença no port)
    float anchor_matrix_[16] = {};  // 0x3d76a0: a identidade quando não há âncora
    bool anchor_matrix_init_ = false;
};

// [driving] 0x17bd00 Noise::Init(void) e 0x17c788 Noise::Noise1(float): o ruído de Perlin de uma dimensão (as tabelas de
// 4096 permutações e 0x10b gradientes; o embaralhamento pelo random() da biblioteca do jogo com a semente 12345).
void Noise_Init();
float Noise_Noise1(float x);
// As tabelas do ruído (0x32f4e8: 4096 permutações; 0x3d08a8: 0x10b gradientes), para o nf_driving_check.
const int16_t* noise_perm_table();
const float* noise_grad_table();
// [driving] 0x25a5ac random() e 0x25a684 srandom(): o gerador de 192 bits da biblioteca do PS2.
uint32_t ps2_random();
void ps2_srandom(uint32_t seed);

// [driving] 0x1b9020 PointInTriangle e 0x1b8f80 PointDir (sem classe): o ponto p no triângulo abc, no plano do eixo em
// que a normal (com sinal) é maior.
bool CameraPointInTriangle(const float p[4], const float a[4], const float b[4], const float c[4]);

uint64_t camera_pending();

}  // namespace drv
