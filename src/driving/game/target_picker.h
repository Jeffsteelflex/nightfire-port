#pragma once
// A mira das armas do carro: o WTargetPicker (0x33a970) e os WTargetable que se registram nele (docs/driving.md seção
// 24), do MIPS do DRIVING.ELF (WTargetPicker.obj, WTargetable.obj, WSimpleZone.obj). Os carros da IA (PBondCar da classe
// 2) e os passageiros com teto solar registram um alvo; a cada quadro desenhado o RRenderWorldCamera chama o
// UpdateTargets, que acha os alvos perto do carro do jogador (a zona de 2^5 x 5 m), os põe na tela, ordena (o qsort do
// newlib pelo comparador +0xa8: a distância ao alvo do mundo na tela) e escolhe o primeiro, preso enquanto ficar perto
// do centro (os raios 130 e 145 em pixels); o tiro do jogador (EFireWeapon, SWeaponManager::FireWeapon) pergunta o
// GetSelectedTarget, e o míssil segue o alvo. Na direção automática o UpdateAutoDriveTargeting gira o quatérnio +0x00
// para o alvo (a câmera o usa).
//
// O alvo do mundo (+0x10: o ponto 175 m à frente do carro, ou 1000 m à frente da câmera na direção automática) e o
// GetWorldTarget estão em game/missile.cpp (seção 22); a estrutura do PS2 inteira está aqui.

#include <cstdint>
#include <functional>

namespace drv {

class PhysicsObject;

// O WTargetPicker do PS2 (0xb0 bytes; o construtor é o 0x00229f40, que o DRIVING.SYM casado chama GetViewMatrix).
struct alignas(16) WTargetPickerData {
    float aim_quat[4] = {0, 0, 0, 1};  // +0x00 o quatérnio da mira automática do AutoDrive
    float world_target[4] = {};        // +0x10 o alvo do mundo; o +0x1c (que os vadd.xyz não tocam) é o passo em que o
                                       //   cursor começou a seguir o alvo (MoveTargetingCursors)
    float screen[2] = {};              // +0x20 o alvo do mundo na tela
    uint32_t selected = 0;             // +0x28 (p32 WTargetableData) o alvo escolhido
    uint32_t list = 0;                 // +0x2c o list<WTargetable *> (no port, o vetor do target_picker.cpp)
    uint32_t sorted[16] = {};          // +0x30 os alvos visíveis perto do jogador, ordenados
    int32_t count = 0;                 // +0x70
    int32_t cycle = 0;                 // +0x74 o índice do CycleToNextTarget
    uint32_t unknown78 = 0;
    uint32_t targeting_sound = 0;      // +0x7c ATargeting (o som da trava; o port anota o SetState: target_picker_sound())
    uint32_t unknown80 = 0;
    uint32_t unknown84 = 0, unknown88 = 0;
    float cursor[2] = {};              // +0x8c o cursor da mira na tela
    uint32_t unknown94 = 0, unknown98 = 0;
    int32_t active = 0;                // +0x9c ActivateTargeting
    int32_t lock = 0;                  // +0xa0 0 nada, 1 seguindo, 2 travado
    int32_t mode = 0;                  // +0xa4 0 o carro, 1 a direção automática
    int32_t sort = 1;                  // +0xa8 o comparador do qsort (gSort 0x33aa38: 0 o centro, 1 o alvo do mundo, 2 a câmera)
    uint32_t unknownac = 0;
};
static_assert(sizeof(WTargetPickerData) == 0xb0, "WTargetPicker");

// O que o picker lê do render (o RRenderer::fgRenderer e o RViewCamera). Os padrões são os do PS2 em NTSC.
struct TargetPickerEnv {
    int32_t screen_w = 640, screen_h = 448;  // fgRenderer +0x50/+0x54 (= DAT_00424724/28, do FeatureManager::Init)
    int32_t widescreen = 0;                  // fgRenderer +0x5c
    float fov_mult = 1.0f;                   // fgRenderer +0x6c
    float aspect_mult = 1.0f;                // RViewCamera::fAspectRatioMult
    // A entrada +0x14 da tabela virtual de um AICharacter (+0x140): vivo (o alvo de tipo 2). Sem ela: vivo, contado.
    std::function<bool(uint32_t aicharacter)> character_alive;
    // AIVehicle::GetPosition (o alvo de tipo 3, os helicópteros da IA, não portados): sem ela, a posição fica.
    std::function<bool(uint32_t aivehicle, float out[3], bool& heli)> ai_vehicle_position;
};
TargetPickerEnv& target_picker_env();

struct TargetPickerStats {
    uint64_t updates = 0, registered = 0, unregistered = 0, visible = 0, on_screen = 0, selections = 0, locks = 0,
             autodrive = 0, guards = 0, sound_states = 0, visibility_checks = 0;
};
TargetPickerStats& target_picker_stats();
// O estado que o PS2 pede ao ATargeting::SetState (0x002e6ac8: os bipes da trava, o som não portado): 0, 1 ou 2.
int32_t& target_picker_sound();

// [driving] 0x00229f40 WTargetPicker::WTargetPicker (o construtor do global 0x33a970, pelo 0x0022c100): zera os campos,
// o quatérnio (0, 0, 0, 1), o comparador 1 e a lista vazia (o port: os alvos registrados esquecidos).
void WTargetPicker_Construct();
// [driving] 0x0022afa8 WTargetPicker::RegisterTarget(WTargetable *): na lista uma vez (AddReference, UpdatePosition,
// UpdateVisibility).
void WTargetPicker_RegisterTarget(uint32_t target);
// [driving] 0x0022b098 WTargetPicker::UnregisterTarget(WTargetable *): sai da lista (e da escolha) e RemoveReference.
void WTargetPicker_UnregisterTarget(uint32_t target);
// [driving] 0x0022b130 WTargetPicker::ClearAllTargets
void WTargetPicker_ClearAllTargets();
// [driving] 0x0022b318 WTargetPicker::UpdateTargets (a cada quadro desenhado: RRenderWorldCamera::DrawStaticWorldGeometry)
void WTargetPicker_UpdateTargets();
// [driving] 0x0022aab0 WTargetPicker::UpdateSelection
void WTargetPicker_UpdateSelection();
// [driving] 0x0022a960 WTargetPicker::MoveTargetingCursors
void WTargetPicker_MoveTargetingCursors();
// [driving] 0x0022a728 WTargetPicker::IsPointOnScreen(COORD2 &)
bool WTargetPicker_IsPointOnScreen(const float point[2]);
// [driving] 0x0022a128 (sem nome no SYM): o ponto do mundo na tela pela câmera do jogador; atrás dela (z <= 0,01),
// (10000, 10000).
void WTargetPicker_GetScreenPos(const float pos[4], float out[2]);
// [driving] 0x0022a430 (WTargetPicker::GetOffScreenPos no Xbox): a direção do ponto na tela, presa à borda da caixa da
// mira.
void WTargetPicker_GetOffScreenPos(const float pos[4], float out[2]);
// [driving] 0x0022b638 WTargetPicker::ActivateTargeting(int modo)
void WTargetPicker_ActivateTargeting(int32_t mode);
// [driving] 0x0022b698 WTargetPicker::DeactivateTargeting
void WTargetPicker_DeactivateTargeting();
// [driving] 0x0022af38 WTargetPicker::GetSelectedTarget: o escolhido se está na tela (+0x44), senão nulo.
uint32_t WTargetPicker_GetSelectedTarget();
// [driving] 0x0022b6b8 WTargetPicker::CycleToNextTarget
void WTargetPicker_CycleToNextTarget();
// [driving] 0x0022b918 WTargetPicker::UpdateAutoDriveTargeting
void WTargetPicker_UpdateAutoDriveTargeting();
// [driving] 0x0022b808 WTargetPicker::GetDirectionToTarget(COORD3 &): a frente da mira da câmera.
void WTargetPicker_GetDirectionToTarget(float out[3]);
// [driving] 0x0022b858 WTargetPicker::GetDirectionToTarget(MATRIX4 &): a matriz da mira da câmera sem a posição.
void WTargetPicker_GetDirectionToTargetMatrix(float out[16]);
// [driving] 0x0022b150 CompareDistFromScreenCenter, 0x0022b1c0 CompareDistFromScreenPos, 0x0022b2a0
// CompareDistFromCamera: os comparadores do qsort (gSort).
int WTargetPicker_CompareDistFromScreenCenter(const void* a, const void* b);
int WTargetPicker_CompareDistFromScreenPos(const void* a, const void* b);
int WTargetPicker_CompareDistFromCamera(const void* a, const void* b);

// Os WTargetable (0x50 bytes, WTargetableData em game/missile.h). Os construtores do port alocam (o FastAlloc do PS2) e
// o RemoveReference apaga com zero referências.
// [driving] 0x00229490 WTargetable::WTargetable(float x, float y, float z): o tipo 0, um ponto.
uint32_t WTargetable_NewPoint(float x, float y, float z);
// [driving] 0x00229518 WTargetable::WTargetable(PhysicsObject *): o tipo 1, na posição do corpo.
uint32_t WTargetable_NewObject(PhysicsObject* object);
// [driving] 0x00229570 WTargetable::WTargetable(AICharacter *): o tipo 2 (o AICharacter com o leiaute do PS2).
uint32_t WTargetable_NewCharacter(uint32_t aicharacter);
// [driving] 0x002295d8 WTargetable::WTargetable(AIVehicle *): o tipo 3.
uint32_t WTargetable_NewAIVehicle(uint32_t aivehicle);
// [driving] 0x00229640 WTargetable::CommonInit
void WTargetable_CommonInit(uint32_t target);
// [driving] 0x002296d8 WTargetable::UpdatePosition
void WTargetable_UpdatePosition(uint32_t target);
// [driving] 0x00229870 WTargetable::UpdateVisibility: visível (+0x0c) se a até 240 m da câmera e o mundo não corta.
void WTargetable_UpdateVisibility(uint32_t target);
// [driving] 0x00229ad8 WTargetable::DistFromCamera(int câmera): o quadrado da distância.
float WTargetable_DistFromCamera(uint32_t target, int32_t camera);
// [driving] 0x00229b40 WTargetable::DistFromScreenPos(COORD2 &) e 0x00229b88 DistFromScreenCenter: os quadrados.
float WTargetable_DistFromScreenPos(uint32_t target, const float point[2]);
float WTargetable_DistFromScreenCenter(uint32_t target);
// [driving] 0x002273d8 WSimpleZone::SetPosition(COORD3 &): cada eixo / 5 arredondado.
void WSimpleZone_SetPosition(int32_t zone[3], const float pos[3]);
// [driving] 0x002274a0 WSimpleZone::IsZoneInRange(WSimpleZone &, int bits)
bool WSimpleZone_IsZoneInRange(const int32_t a[3], const int32_t b[3], uint32_t bits);
// [driving] 0x00216500 WCollisionMgr::StepCheckHitWorld(float, COORD4 *): o segmento em passos de até 48 m (o float
// do primeiro argumento não é lido); verdadeiro se algum passo bate. `seg` muda (o PS2 anda com ele).
bool WCollisionMgr_StepCheckHitWorld(float seg[2][4]);
// [driving] 0x002d18e8 TransformPoint(MATRIX4 &, COORD4 &, COORD4 &): out.xyz = in.xyz x m + a linha 3 (o w de out fica).
void TransformPoint3(const float m[16], const float in[4], float out[4]);

}  // namespace drv
