#pragma once
// O motor de animação das instâncias (RAnimEngine do DRIVING.ELF, ColourConversion.obj; docs/driving.md seção 22): os
// "sistemas" de animação de um objeto de cena (o lançador de mísseis do carro, a antena do radar, as peças de um objeto
// derrubado), cada um uma máquina de estados movida por estímulos.
//
// O RSceneObj::UseInstanceList cria um RAnimEngine::Handle para as instâncias de um objeto (Handle::Create): um bloco
// com as cópias das instâncias (0x40 bytes cada), as cópias dos estados de dano ('di', 0x20), os sistemas (0x40 cada,
// um por identificador de sistema dos artigos das instâncias, em ordem) e as cópias dos efeitos dos artigos ('ef').
// Cada artigo ('as') traz em +8 as animações dele (CARP::AnimInfo, os elementos de 0x10 do 'an': as chaves 'aq' em +0,
// o sistema em +4 e o espelhado em +6, os quadros em +8, as bandeiras em +0xa (1 repete), o estímulo em +0xc, o estado
// de partida em +0xd, o de chegada em +0xe), ordenadas por (estímulo, estado).
//
// Um estímulo (Handle::ProcessStimuli: o SWeaponManager pede 10 para disparar, 11 para recarregar, 8 e 9 para abrir e
// fechar o lançador) escolhe em cada instância do sistema a animação de (estímulo, estado corrente); com alguma, o
// sistema entra na lista dos que tocam (até 256, 0x3d1518) com o tempo de início; tocando, o estímulo fica numa fila de
// quatro. A cada quadro (RAnimEngine::Update, do EAnimUpdate) cada sistema da lista anda: as chaves que o tempo passou
// criam os eventos da lista 'el' delas (o EventDynamicData com a instância), como o EFireWeapon que cria o míssil; no
// fim o sistema vai para o estado de chegada e o próximo estímulo da fila começa.
//
// O tempo do motor: o passo da simulação x RTimeData::fFrequency (1 / a taxa do vídeo) x 60, truncado (fptoui): com as
// contas do EE, um passo pode dar um tique a menos (3 x (1/60) x 60 = 2,9999998 → 2).
//
// Portado do MIPS do PS2; o leiaute do PS2 fica no bloco do Handle (as palavras de 32 bits, os ponteiros p32), para a
// parte 18 do nf_driving_check comparar com o original.
//
// Divergências (cada uma também na função):
//  - o bloco do Handle vem do armazenamento do port (anim_handle_storage), não do heap do jogo;
//  - o RSceneObj::SetEventDynamicData (a entrada +0x60 da tabela virtual do objeto de cena) é o do RSceneObj para todos
//    (o único que o jogo tem): o EventDynamicData do port (trigger_manager()) com o objeto e o dono físico dele.

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

// O cabeçalho do Handle (0x40 bytes; as cópias das instâncias vêm logo depois).
struct AnimHandleData {
    uint32_t size;          // +0x00 o tamanho do bloco depois do cabeçalho
    uint32_t scene_obj;     // +0x04 (p32 RSceneObj) o dono (o SetEventDynamicData dele)
    uint32_t damage;        // +0x08 (p32) as cópias dos estados de dano (0x20 cada)
    uint32_t systems;       // +0x0c (p32 AnimSystemData[n_systems])
    uint32_t system_ids;    // +0x10 (p32 uint8_t[n_systems]) os identificadores em ordem (a busca binária)
    uint32_t effects;       // +0x14 (p32) as cópias dos efeitos dos artigos (0x40 cada)
    uint32_t effect_bits;   // +0x18 (p32 uint16_t[n_effects]); os uint8_t do +0x17 de cada efeito vêm depois
    uint8_t n_instances;    // +0x1c
    uint8_t n_damage;       // +0x1d
    uint8_t n_systems;      // +0x1e
    uint8_t n_effects;      // +0x1f
    uint64_t unknown20;     // +0x20
    uint64_t effects_flag8; // +0x28 os efeitos com a bandeira 8
    uint64_t effects_type6; // +0x30 os do tipo 6
    uint64_t effects_96;    // +0x38 os com as bandeiras 0x96
};
static_assert(sizeof(AnimHandleData) == 0x40);

// Um sistema (0x40 bytes).
struct AnimSystemData {
    uint32_t handle;        // +0x00 (p32 AnimHandleData)
    uint32_t start;         // +0x04 o tique do começo
    uint32_t last;          // +0x08 o tique do último Update
    uint16_t zones;         // +0x0c as zonas de dano das instâncias (CalcDamageZone)
    uint16_t max_ticks;     // +0x0e os quadros da animação mais longa x 4
    int8_t queue[4];        // +0x10 os estímulos à espera (-1 vazio)
    uint8_t pending_state;  // +0x14 o estado depois dos da fila
    uint8_t id;             // +0x15
    uint8_t state;          // +0x16 o estado corrente (1 no começo)
    uint8_t next_state;     // +0x17 o de chegada da animação que toca
    uint8_t loop;           // +0x18
    uint8_t playing;        // +0x19 as instâncias ainda tocando
    uint8_t n_instances;    // +0x1a
    uint8_t n_anims;        // +0x1b as animações do artigo (100 no começo; o menor das instâncias)
    uint8_t unknown1c;
    uint8_t started;        // +0x1d o primeiro Update já passou
    uint8_t instances[0x22];// +0x1e os índices das instâncias
};
static_assert(sizeof(AnimSystemData) == 0x40);

// CARP::AnimInfo (um elemento do 'an', 0x10 bytes).
struct CarpAnimInfo {
    uint32_t keys;          // +0x00 (p32) as chaves ('aq', 0x20 cada: a lista 'el' em +0x1c)
    uint16_t system_id;     // +0x04
    uint16_t mirror_id;     // +0x06 o do lado espelhado (bandeira 0x80 da instância)
    uint16_t frames;        // +0x08
    uint16_t flags;         // +0x0a 1: repete
    uint8_t stimulus;       // +0x0c
    uint8_t from_state;     // +0x0d
    uint8_t to_state;       // +0x0e
    uint8_t valid;          // +0x0f
};
static_assert(sizeof(CarpAnimInfo) == 0x10);

// Os globais do motor (0x333600 a contagem, 0x333604 o último tique, 0x3d1518 a lista dos sistemas tocando).
struct AnimEngineGlobals {
    uint32_t count = 0;
    uint32_t last_tick = 0;
    uint32_t active[256] = {};  // (p32 AnimSystemData)
    float frequency = 1.0f / 60.0f;  // RTimeData::fFrequency (0x333ff4: 1 / GetFoundationVideoModeRate)
};
AnimEngineGlobals& anim_engine();
void anim_engine_reset(float video_rate = 60.0f);

struct AnimEngineStats {
    uint64_t handles = 0, stimuli = 0, started = 0, updates = 0, events = 0, finished = 0, guards = 0;
};
AnimEngineStats& anim_engine_stats();

// O tique do motor de um passo da simulação (o fptoui(passo x fFrequency x 60)).
uint32_t anim_ticks(uint32_t steps);

// [driving] 0x001a1bc8 RAnimEngine::Handle::Create(unsigned int espelhado, unsigned int bandeiras): o efeito entra no
// Handle (as bandeiras 0x109e; espelhado só com 0x200).
bool AnimHandle_EffectWanted(uint32_t mirrored, uint32_t flags);
// [driving] 0x001a1bf0 RAnimEngine::Handle::Create(unsigned int n, unsigned int tempo, CARP::Instance *, ...): o Handle
// das `n` instâncias (p32) com os estados de dano `damage` (p32, 0 sem) e o dono `scene_obj` (p32); no fim o estímulo 1
// em todos os sistemas. Devolve o Handle (p32).
uint32_t AnimHandle_Create(uint32_t n, uint32_t steps, uint32_t instances, uint32_t damage, uint32_t scene_obj);
// [driving] 0x001a2558 RAnimEngine::Handle::~Handle (o Stop; o bloco volta ao armazenamento)
void AnimHandle_Destroy(uint32_t handle);
// [driving] 0x001a25a0 RAnimEngine::Handle::Stop: os sistemas deste Handle saem da lista.
void AnimHandle_Stop(uint32_t handle);
// [driving] 0x001a1e88 __lower_bound<unsigned char*, ...> (a busca do sistema pelo identificador): 0 sem.
uint32_t AnimHandle_FindSystem(uint32_t handle, uint32_t id);
// [driving] 0x001a27a0 RAnimEngine::Handle::ProcessStimuli(sistema, estímulo, tempo, modo)
void AnimHandle_ProcessStimuli(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode);
// [driving] 0x001a2868 RAnimEngine::Handle::ProcessStimuli(estímulo, tempo, modo): todos os sistemas.
void AnimHandle_ProcessStimuliAll(uint32_t handle, uint32_t stimulus, uint32_t steps, uint32_t mode);
// [driving] 0x001a2c88 RAnimEngine::Handle::InitAllSystemStates
void AnimHandle_InitAllSystemStates(uint32_t handle, uint8_t state);
// [driving] 0x001a2cc0 RAnimEngine::Handle::AnySystemPlaying
bool AnimHandle_AnySystemPlaying(uint32_t handle);
// [driving] 0x001a2d10 RAnimEngine::Handle::IsSystemPlaying
bool AnimHandle_IsSystemPlaying(uint32_t handle, uint32_t system);
// [driving] 0x001a2630 RAnimEngine::Handle::SetFrame (e 0x001a38e0 System::SetFrame): as instâncias do sistema com a
// animação de (estímulo, estado corrente) vão a ela, no quadro dado (no máximo o último). A torre dos carros da IA.
void AnimHandle_SetFrame(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t frame);
// [driving] 0x001a2d40 RAnimEngine::Handle::GetSystemStartTick
uint32_t AnimHandle_GetSystemStartTick(uint32_t handle, uint32_t system);
// [driving] 0x001a2d68 RAnimEngine::Handle::GetSystemMaxTicks
uint32_t AnimHandle_GetSystemMaxTicks(uint32_t handle, uint32_t system);
// [driving] 0x001a2d90 RAnimEngine::Handle::GetSystemState
uint32_t AnimHandle_GetSystemState(uint32_t handle, uint32_t system);
// [driving] 0x001a2db8 RAnimEngine::Handle::GetInstanceSystemID
uint32_t AnimHandle_GetInstanceSystemID(uint32_t handle, uint32_t instance);
// [driving] 0x001a2df0 RAnimEngine::Handle::GetSystemExists
bool AnimHandle_GetSystemExists(uint32_t handle, uint32_t system);
// [driving] 0x001a2750 RAnimEngine::Handle::FindEffectByID: a cópia do efeito (p32), 0 sem.
uint32_t AnimHandle_FindEffectByID(uint32_t handle, uint32_t id);
// [driving] 0x001a2738 RAnimEngine::Handle::SetEffectBits
void AnimHandle_SetEffectBits(uint32_t handle, uint32_t effect, uint16_t bits);
// [driving] 0x001a33c0 RAnimEngine::System::ProcessStimuli
void AnimSystem_ProcessStimuli(uint32_t system, uint32_t stimulus, uint32_t ticks, uint32_t mode);
// [driving] 0x001a35f0 RAnimEngine::System::Update(índice na lista, tique): devolve o índice (um a menos se o
// sistema saiu da lista).
uint32_t AnimSystem_Update(uint32_t system, uint32_t index, uint32_t ticks);
// [driving] 0x001a1a10 RAnimEngine::Update(tempo): os sistemas da lista (uma vez por tique).
void RAnimEngine_Update(uint32_t steps);

// O RSceneObj::SetEventDynamicData do RSceneObj (0x001c8b08, o do anim_engine.cpp): o EventDynamicData zerado com o objeto
// de cena e o dono físico (o ActWeapon::SetEventDynamicData o chama antes de pôr a arma no +0x34).
void RSceneObj_SetEventDynamicData(uint32_t scene);

// [driving] 0x001a30c0 RAnimEngine::EvaluateInstance(CARP::Instance *, MATRIX4 &): a matriz de uma cópia de instância
// na animação dela (o +0xd abaixo de 100: as chaves do quadro e do seguinte pelo tempo +0xe x o ritmo +0xf / 60, o slerp
// rápido e a posição interpolada, o x espelhado com a bandeira 0x80); senão a própria instância. É o gProcAnimFunc dos
// tipos 0x00-0x65 (0x001bf318).
void RAnimEngine_EvaluateInstance(const uint8_t* inst, float out[4][4]);

// [driving] 0x002db798 CARP::BaseDesc::CalcDamageZone(COORD3 &) e 0x002db930 (a máscara das zonas vizinhas)
uint32_t BaseDesc_CalcDamageZone(uint32_t base_desc, const float pos[3]);
uint16_t BaseDesc_ZoneNeighbours(uint32_t zone);

// Port: um texto com os sistemas do Handle, as animações das instâncias e os eventos das chaves (o --anim-dump do
// nf_driving).
std::string AnimHandle_Describe(uint32_t handle);
// Port: as instâncias ('in#0000') e os estados de dano ('di#0000') do primeiro 'Arti' de um CARP resolvido.
struct UData;
bool carp_instance_list(UData* root, uint32_t& instances, uint32_t& count, uint32_t& damage);

}  // namespace drv
