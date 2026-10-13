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
// âncora, as explosões (nenhuma existe no port) e os corpos simples perto da câmera.

#include <cstdint>
#include <memory>
#include <vector>

#include "driving/camera/camera_ini.h"
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
    uint32_t unknown08[4];  // +0x08..+0x14
    uint32_t paused;        // +0x18 a simulação estava parada (estado 3) no ResetState
    uint32_t unknown1c;     // +0x1c 1 no RestartCamera
    uint32_t unknown20[9];  // +0x20..+0x40
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
    int32_t unknown140;
    uint8_t unknown144[0xc];
    float unknown150[4];        // +0x150
    int32_t unknown160, unknown164, unknown168, unknown16c;
    float unknown170[4];        // +0x170 a linha 2 do referencial no reinício
    float matrix_copy[16];      // +0x180
    int32_t unknown1c0[8];      // +0x1c0..+0x1dc
    int32_t unknown1e0[4];      // +0x1e0..+0x1ec
    int32_t unknown1f0, unknown1f4, unknown1f8, unknown1fc;
    float unknown200[4];        // +0x200
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
    int32_t unknown260, unknown264, unknown268;
    int32_t transition_override;  // +0x26c
    int32_t shake_time;         // +0x270
    float shake_amount;         // +0x274
    int32_t shaking;            // +0x278
    uint8_t transition_on;      // +0x27c
    uint8_t transition_first;   // +0x27d
    int8_t arm;                 // +0x27e o braço do modo Heli
    int8_t unknown27f;          // +0x27f -1 no reinício
    uint8_t unknown280;
    uint8_t unknown281[0xf];
    int32_t unknown290, unknown294;
    float zoom_fov_max;         // +0x298
    float zoom_fov_min;         // +0x29c
    int32_t unknown2a0, unknown2a4, unknown2a8;
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
    int32_t vertigo_count = 0;  // 0x333a38
    float vertigo_point[4] = {};  // 0x333a40
};

// O mundo em volta da câmera.
struct CameraContext {
    Simulation* sim = nullptr;
    TrackWorld* world = nullptr;              // nulo: sem colisão com os prédios
    std::vector<CameraAnchor*> cars;          // a lista dos carros da Simulation (+0xdcc); o primeiro é o do jogador
    int32_t current_weapon = -1;              // o gerente de armas (+4); -1: nenhuma
    std::vector<CameraWeaponSlot> weapons;    // as vagas do gerente (FindHeliArmInd, ResolveWeaponNames)
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
