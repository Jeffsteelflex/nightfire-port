#pragma once
// O objeto de render de um carro do DRIVING.ELF: o RVehicle (RVehicle.obj) sobre o RSkeletalObj e o RSceneObj
// (RSceneObj.obj), e as funções de animação procedural das instâncias (RProcAnim, o gProcAnimFunc de
// RPlayerViewCamera.obj). Portado do MIPS do PS2 e conferido contra o original no nf_driving_check (parte 12, `--so
// carro`). docs/driving.md seção 16.
//
// O que ele faz por quadro (RVehicle::UpdatePosition e RVehicle::Render, lidos na desmontagem):
//  - RSceneObj::UpdatePosition põe a instância de origem do objeto na matriz do corpo rígido (a orientação e a posição
//    menos o "cima" vezes o GetRenderOffset);
//  - RVehicle::ComputeWheels gira os ossos do esqueleto (o 'Skel' do CARP): o cubo de cada roda pela direção (y, em
//    voltas, limitada a +-0,075 e a 0,002 por quadro na vista de dentro, 0,01 nas outras; a da direita ao contrário,
//    porque a instância dela é espelhada) com a altura da mola na linha 3, e a roda pelo giro (x) vezes o cubo; nos
//    submarinos as barbatanas e os lemes pelo controle vertical e pelo freio;
//  - os ponteiros do painel: o volante (+0x40 = a direção x -750 graus), o velocímetro e o conta-giros (GetNeedlePos);
//  - RVehicle::Render: a sombra (RShadowMap, driving/render/shadow.h), ComputeDamage (as zonas de dano do PBondCar no
//    DamageZones da VU), SetProcAnimState (os ossos e os parâmetros para o gProcAnimFunc) e a lista de instâncias,
//    cada uma pela sua função de animação procedural: 0xff a matriz da instância, 0xfe o osso vezes a instância, 0xef
//    uma rotação em z pelo parâmetro (os ponteiros); RRenderSharedData::SendPerCarInstance chama SetPerObjectBuffers,
//    que põe o w de cada lâmpada (RVehicle +0x268) no SwitchData da VU e copia as zonas de dano.
//
// As lâmpadas (o w do SwitchData): o PBondCar liga e desliga cada uma pelo GlareOn/GlareOff (game/bond_car.h), que
// chamam o TriggerFX do render com os tipos 0xb e 0xc (1 e 0 no +0x268); o construtor deixa acesas as 7, 8 e 11-15
// (painel, sempre acesas, as "Misc"); os botões 9 e 10 apagam em 30 quadros a partir do TriggerIlluminate. O x, y e z de
// cada entrada (VScroll, Glow, Illuminate) vêm do banco de ajuste "Render:SwitchEffects" (InitSharedBuffers).
//
// O que fica fora (contado em vehicle_pending): o mapa da cena e o brilho de sombra do RSceneObj::UpdatePosition
// (WTree::FindNode, GetShadowBrightness: o RLightManager), as molas (RSkeletalObj::UpdateSpringMassSystem: só rodam
// com as animações de estímulo do RAnimEngine, que liga o sistema de mola de uma peça batida; nenhuma está ativa sem
// dano), os efeitos (TriggerEffects/SuppressEffects, as partículas, os relâmpagos do EMP, o dissolver da cor).
//
// O leiaute: RVehicleData é o objeto do PS2 (0x370 bytes, FastAlloc(0x370)): o RSceneObj até +0x3f (a tabela virtual em
// +0x3c, como no GCC 2.95), o RSkeletalObj até +0x9f e o RVehicle. Ponteiros como palavras de 32 bits (p32).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "driving/carp.h"

namespace drv {

class BondCar;
struct RigidBody;

struct alignas(16) RVehicleData {
    // RSceneObj
    uint8_t unknown000[8];          // +0x00 a lista do nó do mapa
    uint32_t instance;              // +0x08 (p32 CARP::Instance) a instância de origem (UpdatePosition a move)
    uint32_t base_desc;             // +0x0c (p32 CARP::BaseDesc; a palavra 0 é o grupo 'Arti' do modelo)
    uint32_t carp;                  // +0x10 (p32 RCARPFile)
    uint32_t map_node;              // +0x14
    uint8_t render_type;            // +0x18 o índice do nome do carro (os tamanhos da sombra simples)
    uint8_t unknown019;
    uint8_t brightness;             // +0x1a o brilho de sombra (0..255)
    uint8_t unknown01b;
    uint16_t damaged_zones;         // +0x1c TriggerFX 10
    uint16_t damage_effect_zones;   // +0x1e TriggerFX 9
    uint8_t unknown020[4];
    uint32_t physics;               // +0x24 (p32 PBondCar) o dono (tipo 1: um veículo)
    uint32_t anim;                  // +0x28 (p32 RAnimEngine::Handle: as cópias das instâncias em +0x40, a contagem no
                                    //       byte +0x1c, os estados de animação procedural em +0x8)
    uint8_t unknown02c[0x10];       // +0x2c
    uint32_t vtable;                // +0x3c
    // RSkeletalObj
    float params[12];               // +0x40 os parâmetros da animação procedural (o volante, o velocímetro, o conta-giros)
    uint64_t springs;               // +0x70 os sistemas de animação do tipo 8 (molas)
    uint32_t bone_count;            // +0x78
    uint32_t bones;                 // +0x7c (p32 MATRIX4[bone_count])
    float spring_prev[4];           // +0x80
    float spring_cur[4];            // +0x90 (o construtor: (0, 0, 0, -1))
    // RVehicle
    float own_instance[16];         // +0xa0 a instância do próprio objeto (a identidade)
    float damage_zones[16][4];      // +0xe0 ComputeDamage: (x, y, 3y, 127 inteiro se x passa de 0,75)
    uint8_t unknown1e0[0x58];       // +0x1e0 (+0x1ec, +0x20c, +0x220 no construtor; os efeitos)
    float wheel_offset[4];          // +0x238 a altura de cada roda no modelo (PostLoad)
    int32_t wheel_bone[4];          // +0x248 os ossos das rodas (GetBoneIndex: 1 + a posição no 'Skel'; 0 sem)
    int32_t hub_bone[4];            // +0x258 os dos cubos (nos submarinos as barbatanas e os lemes)
    float switch_w[16];             // +0x268 o w do SwitchData de cada lâmpada
    int32_t switch_time[16];        // +0x2a8 o quadro do TriggerIlluminate (-10000 no construtor)
    uint8_t unknown2e8[0x40];       // +0x2e8
    float steer;                    // +0x328 a direção desenhada
    float sub_steer_vertical;       // +0x32c os submarinos
    uint32_t unknown330;            // +0x330
    uint32_t draw_list_count;       // +0x334
    float sub_brake;                // +0x338
    uint8_t unknown33c[0xc];
    uint32_t draw_list;             // +0x348 (a lista de desenho da vista, SetViewDrawList)
    uint32_t dissolve;              // +0x34c
    uint32_t shadow_model;          // +0x350 (p32 EAGL::Model) o 'as' do tipo 3 (RenderShadow: com ele a sombra simples)
    uint32_t shadow_volume;         // +0x354 o do tipo 4 (RenderShadowVolume: sem quem chame)
    uint32_t particles;             // +0x358
    uint8_t unknown35c[0xc];
    uint32_t emp_bolts;             // +0x368
    uint8_t unknown36c[4];
};
static_assert(sizeof(RVehicleData) == 0x370);
static_assert(offsetof(RVehicleData, physics) == 0x24 && offsetof(RVehicleData, params) == 0x40 &&
              offsetof(RVehicleData, bones) == 0x7c && offsetof(RVehicleData, damage_zones) == 0xe0 &&
              offsetof(RVehicleData, wheel_offset) == 0x238 && offsetof(RVehicleData, switch_w) == 0x268 &&
              offsetof(RVehicleData, switch_time) == 0x2a8 && offsetof(RVehicleData, steer) == 0x328 &&
              offsetof(RVehicleData, sub_brake) == 0x338 && offsetof(RVehicleData, shadow_model) == 0x350);

using Matrix4 = float[4][4];
struct alignas(16) Bone {
    float m[4][4];
};

// O que o PostLoad lê do modelo do carro (o 'Arti' do CARP).
struct VehicleModelInfo {
    std::vector<std::string> bone_names;  // o 'Skel', na ordem (GetBoneIndex devolve a posição + 1)
    float bbox_min_y = 0, bbox_max_y = 0, desc38 = 0;  // o BaseDesc +0x14, +0x24, +0x38
    struct Instance {
        uint8_t procanim = 0xff;  // o byte +0xd
        uint16_t index = 0;       // o u16 +0xe (o osso, ou o parâmetro)
        float y = 0;              // a linha 3, y (+0x34)
    };
    std::vector<Instance> instances;  // as do 'in#0000' (as cópias do Handle)
    int32_t shadow_part = -1, shadow_volume_part = -1;  // as peças dos 'as' do tipo 3 e 4 (o byte +0x1e), -1 sem
};
// O 'Skel' (os nomes dos ossos: RSceneObj::GetNumBones 0x1c9910 e GetBoneIndex 0x1c9980 percorrem os registros de 0x20
// bytes; o nome pelo u16 +0x1c a partir do começo dos dados) e as instâncias do 'Arti' de um CARP, como o PostLoad os vê.
bool vehicle_model_info(const uint8_t* carp, size_t size, const CarpGroup& arti, VehicleModelInfo& out);

// O carro de render do port: os dados do PS2, os ossos e os parâmetros.
class Vehicle {
public:
    RVehicleData d{};
    std::vector<Bone> bones;
    Vehicle();
};

// [driving] 0x001d2338 RVehicle::RVehicle (a parte do render: os campos que o resto usa)
void RVehicle_Construct(Vehicle& v);
// [driving] 0x001cc8e8 RSkeletalObj::AllocateSkeleton: `count` identidades (o +0x7c aponta para elas).
void RSkeletalObj_AllocateSkeleton(Vehicle& v, uint32_t count);
// [driving] 0x001c9980 RSceneObj::GetBoneIndex: a posição do nome no 'Skel' mais 1 (0: não tem).
int32_t RSceneObj_GetBoneIndex(const VehicleModelInfo& m, const char* name);
// [driving] 0x001d2648 RVehicle::PostLoad (com o RSkeletalObj::PostLoad 0x001cc6b0): o esqueleto, os ossos das rodas e
// dos cubos, a altura das rodas, os modelos da sombra. `is_sub` é o IS_SUB do dono (os nomes das barbatanas).
void RVehicle_PostLoad(Vehicle& v, const VehicleModelInfo& m, bool vehicle_owner, bool is_sub);
// [driving] 0x001d2cb0 RVehicle::ComputeWheels: devolve a direção da roda dianteira esquerda (em voltas). `interior` é o
// byte +0x07 do modo da câmera do jogador (RPlayerCamera::fHeliCamInfo[modo]: a vista de dentro).
float RVehicle_ComputeWheels(Vehicle& v, BondCar* car, bool interior);
// [driving] 0x001d3440 RVehicle::GetNeedlePos: o ângulo (graus) de um ponteiro (1 o velocímetro, 2 o conta-giros).
float RVehicle_GetNeedlePos(float value, int needle);
// [driving] 0x001d34a8 RVehicle::UpdatePosition: o RSkeletalObj::UpdatePosition (0x001cc738: o RSceneObj::UpdatePosition
// e as molas), ComputeWheels, o volante e, no carro do jogador, os dois ponteiros (com PBondCar::CalculateRPM).
// `instance` é a instância de origem (16 floats, as linhas; o w de cada linha não muda); `render_offset` o
// GetRenderOffset. Devolve verdadeiro se a posição andou (o RSceneObj refaria o nó do mapa e o brilho: contado).
bool RVehicle_UpdatePosition(Vehicle& v, BondCar* car, bool player, bool interior, float instance[16],
                             float render_offset);
// [driving] 0x001c8d70 RSceneObj::UpdatePosition (a parte que move a instância): as linhas 0-3 da orientação do corpo
// (o xyz), a posição menos a linha 1 vezes `render_offset`. Devolve verdadeiro se andou 1e-5 ou mais (ao quadrado).
bool RSceneObj_UpdateTransform(const RigidBody* body, float render_offset, float instance[16]);
// [driving] 0x001d3138 RVehicle::ComputeDamage
void RVehicle_ComputeDamage(Vehicle& v, const BondCar* car);
// [driving] 0x001d3c28 RVehicle::TriggerFX: os tipos 0xb (a lâmpada acesa: 1,0) e 0xc (apagada); os outros (efeitos,
// faíscas, o clarão do tiro, o dissolver) são contados. Devolve falso nos não portados.
bool RVehicle_TriggerFX(Vehicle& v, int32_t type, int32_t which);
// [driving] 0x001d2aa0 RVehicle::TriggerIlluminate: o quadro de partida e a lâmpada em 2,0.
void RVehicle_TriggerIlluminate(Vehicle& v, int32_t which, int32_t time);

// Os buffers do .bss que os render methods do carro leem (GAME::SwitchData 0x424520, 16 COORD4; GAME::DamageZones
// 0x424620, 16 COORD4) e o banco "Render:SwitchEffects".
struct VehicleSharedBuffers {
    float switch_data[16][4];
    float damage_zones[16][4];
};
VehicleSharedBuffers& vehicle_shared_buffers();
// [driving] 0x001d2910 RVehicle::InitSharedBuffers: (0, 0, 0,5, 0) em cada entrada e o banco de ajuste
// "Render:SwitchEffects" ("VScroll{nome[i]}", "Glow{...}", "Illuminate{...}": x, y, z; o default.tun da missão).
void RVehicle_InitSharedBuffers(const std::string& switch_effects_tun);
// [driving] 0x001d2a50 RVehicle::ClearSharedBuffers
void RVehicle_ClearSharedBuffers();
// [driving] 0x001d2b38 RVehicle::SetPerObjectBuffers: os botões 9 e 10 pelo tempo (`time`: RTimeData::fTime, o quadro),
// o w das 16 lâmpadas e as zonas de dano.
void RVehicle_SetPerObjectBuffers(Vehicle& v, int32_t time);
// [driving] 0x001d2ac0 (o desvanecer dos botões): 1 - (t - início) / 30 até 0; 0 sem início ou antes dele.
float RVehicle_ButtonFade(int32_t time, int32_t start);

// A animação procedural das instâncias (RProcAnim): a lista de matrizes (SetProcAnimXFormList 0x001bef30: o
// esqueleto do objeto; nula, a matriz zero de 0x383430) e de parâmetros (SetProcAnimParamList 0x001bef90).
struct ProcAnimContext {
    const Bone* xforms = nullptr;
    uint32_t xform_count = 0;
    const float* params = nullptr;
    uint32_t param_count = 0;
    const uint8_t* states = nullptr;  // os CARP::ProcAnimState (0x20 bytes) do Handle, pelo u16 +0xe
};
// [driving] 0x001cd178 RSkeletalObj::SetProcAnimState: o esqueleto e os 12 parâmetros do objeto.
ProcAnimContext RSkeletalObj_SetProcAnimState(const Vehicle& v);
// [driving] 0x001bef68 GetProcAnimXForm
const Bone& GetProcAnimXForm(const ProcAnimContext& c, uint32_t index);
// [driving] 0x001befc8 GetProcAnimParameter
float GetProcAnimParameter(const ProcAnimContext& c, uint32_t index);
// [driving] 0x001beee0 GetInstanceMatrix: a matriz da instância (os 64 bytes do CARP::Instance, com as bandeiras no w
// de cada linha) pela função gProcAnimFunc[byte +0xd]; 0xff, 0xfe e 0xef portados; as outras (0x00-0x65 e 0xed-0xfd,
// nenhuma num carro) devolvem falso e contam. O DrawInstance multiplica o resultado pela matriz do objeto.
bool GetInstanceMatrix(const uint32_t instance[16], const ProcAnimContext& c, float out[4][4]);
// BuildRotate(MATRIX4 &, float graus, float x, float y, float z) (0x002d11c8, uma só definição em light_manager.cpp):
// a rotação de um ângulo em graus em torno do eixo (normalizado com RSQRT.S); 0 dá a identidade.
void BuildRotate(float m[16], float degrees, float x, float y, float z);
inline void BuildRotate(float m[4][4], float degrees, float x, float y, float z) {
    BuildRotate(&m[0][0], degrees, x, y, z);
}

// Os contadores do que o render do carro chamaria e o port não tem.
struct VehiclePending {
    uint64_t map_updates = 0;      // RSceneObj::UpdatePosition: WTree::FindNode, SetMapNode, GetShadowBrightness
    uint64_t springs = 0;          // RSkeletalObj::UpdateSpringMassSystem
    uint64_t effects = 0;          // TriggerFX: efeitos, faíscas, clarão, dissolver, danos
    uint64_t procanim_other = 0;   // gProcAnimFunc fora de 0xff/0xfe/0xef
    uint64_t guards = 0;           // memória que o PS2 leria fora do lugar (estados nulos)
};
VehiclePending& vehicle_pending();

}  // namespace drv
