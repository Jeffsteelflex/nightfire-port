#pragma once
// Os sons do jogo do lado do EE (Sound_*, 0x001dcc68-0x001de3d4; Music_Event; UpdateMusicalEvents). Ver
// docs/engine.md, "Som", e docs/formats.md, "Áudio".
//
// Um som pedido pelo jogo é um nó DYNAMICSOUNDS (0x60 bytes) da lista DynamicSoundList (256 nós, do heap:
// Sound_Init a cada carga); o "handle" que Sound_Play/Sound_Play3D devolvem é o próprio ponteiro do nó (0 sem nó
// livre ou com o som 0xffff). Estados (+0x50): 0 pedido, 1 tocando, 2 acabou ou parado (o nó é devolvido no tick
// seguinte), 3 livre. Sound_UpdateListeners (Game_Run, a cada tick) põe o ouvinte na câmera, manda a sala da
// câmera como ambiente (SFXSetEnvironment), liga e desliga os sons de ambiente do mapa (HandleMapSoundAllocation),
// e Sound_UpdateSounds anda os estados: 0 -> Sound_ReqestPlaySfx (SFXStart ou SFXStart3D) e a legenda da fala
// (Sound_DoSubtitle, se o som foi marcado audível); 1 -> posição (SFXSetPositionAndVelocity) e o fim
// (SFXIsSFXPlaying, ou o laço que só acaba com Sound_Stop); 2 -> SFXRemove e o nó volta à lista livre.
// Sound_Alertness é o que a IA ouve (a soma dos sons não-laço com alerta > 0 em alcance, pela distância).
//
// O volume pedido (o 2º/3º argumento) chega ao IOP, que o ignora (SFXStart3D sobrescreve com 100: sfx_iop.h).
//
// A música (engine/music.cpp): Music_Event guarda os eventos do tick (EventList); UpdateMusicalEvents (de
// SFXUpdate, uma vez por VBlank) começa a faixa da fase (MapMusic), conta o ataque e chama a função da fase
// (UpdateMFX_*), que pede os trechos (SFXJumpToMusicMarker) pelos eventos, pelas cenas e pelo combate.

#include <cstdint>

#include "engine/ps2types.h"
#include "platform/ptr32.h"

// Nó da lista de sons (0x60 bytes).
struct DYNAMICSOUNDS {
    P32<void> prev;        // 0x00 LLNODE_tag
    P32<DYNAMICSOUNDS> next;  // 0x04
    uint32_t pad_08[2];
    _VECTOR pos;           // 0x10
    _VECTOR vel;           // 0x20 (sempre zero: ninguém a escreve)
    uint32_t has_ranges;   // 0x30 alcances do pedido (Sound_Play3D com map_sound); senão -1 ao IOP
    uint32_t sound;        // 0x34 o número do som (os bits acima de 0xfffff são do chamador)
    uint32_t ref;          // 0x38 referência do chamador (Sound_Stop_Ref, Sound_Playing_Ref)
    float volume;          // 0x3c
    float min_range;       // 0x40 SFXOutputData +4 (10 m sem a tabela)
    float max_range;       // 0x44 SFXOutputData +8 (20 m)
    float alertness;       // 0x48 SFXOutputData +0xc (o que a IA ouve)
    int16_t a;             // 0x4c o 3º argumento de Sound_Play (não usado)
    uint8_t is3d;          // 0x4e
    uint8_t moved;         // 0x4f a posição mudou (Sound_SetPosition)
    uint8_t state;         // 0x50 0 pedido, 1 tocando, 2 parar, 3 livre
    uint8_t age;           // 0x51 ticks na lista
    uint8_t audible;       // 0x52 Sound_ReqestPlaySfx: perto o bastante para a legenda
    uint8_t map_sound;     // 0x53 o último argumento de Sound_Play3D (sons de ambiente do mapa: 1)
    uint8_t unknown_54;
    uint8_t frame_delay;   // 0x55 Sound_FrameDelay no pedido
    uint8_t pad_56[10];
};
static_assert(sizeof(DYNAMICSOUNDS) == 0x60);

// Um som de ambiente do mapa (bloco 0x28, GMapSounds, 32 bytes).
struct MAPSOUND_tag {
    uint32_t unknown_00;
    uint32_t sound;        // 0x04
    float pos[3];          // 0x08
    float a;               // 0x14 vira o 6º argumento (short) de Sound_Play3D
    float radius;          // 0x18 liga dentro do raio, desliga além de 1.2 x raio²; o alcance máximo do som
    float min_range;       // 0x1c
};
static_assert(sizeof(MAPSOUND_tag) == 0x20);

extern uint32_t SoundInit;                 // 0x0030d078 um banco foi carregado nesta fase (Sound_Ready)
extern uint32_t GNumMapSounds;             // 0x0030d07c
extern MAPSOUND_tag GMapSounds[50];        // 0x002d3610
extern uint8_t GMapSoundActive[50];        // 0x002d3c50
extern uint32_t GMapSoundHandle[50];       // 0x002d3c88
extern _VECTOR GlobalListenerPos;          // 0x002d3600 a posição do ouvinte (a câmera)
extern uint32_t Sound_FrameDelay;          // 0x0030d080
extern uint32_t EventList[64];             // 0x002d34f8 eventos de música do tick (Music_Event)
extern int32_t MapMusic[0x23 * 0x13];      // 0x0024f4d8 a faixa de cada fase (do ACTION.ELF, kTables)
extern int32_t MapDroneData[0xc * 4];      // 0x0024f418 o estado de música da IA por fase (kTables)

void Sound_Init();
void Sound_Kill();
void Sound_Ready();
void Sound_LoadMapSounds(uint8_t* data);
void Sound_UpdateListeners();
void Sound_UpdateSounds(_VECTOR* listener);
void Sound_ReqestPlaySfx(DYNAMICSOUNDS* s);
void HandleMapSoundAllocation();
uint32_t Sound_Play(uint32_t sound, float volume, int16_t a, uint32_t ref);
uint32_t Sound_Play3D(uint32_t sound, _VECTOR* pos, float volume, float max_range, float min_range, int16_t a,
                      uint32_t ref, int32_t map_sound);
void Sound_Stop(uint32_t handle);
void Sound_StopAllWithId(uint32_t sound);
void Sound_SetPosition(uint32_t handle, _VECTOR* pos);
void Sound_SetVolume(uint32_t handle, float volume);
void Sound_SetAlertness(uint32_t handle, float level);
void Sound_ModAlertness(uint32_t handle, float level);
uint32_t Sound_IsFinnished(uint32_t handle);
uint32_t Sound_IsLooping(uint32_t handle);
void Sound_ZeroAlertness();
float Sound_Alertness(_VECTOR* pos, float* level, DYNAMICSOUNDS** sound);
void Sound_Stop_Ref(uint32_t handle, uint32_t ref);
uint32_t Sound_Playing_Ref(uint32_t handle, uint32_t ref);
void Music_Event(uint32_t event, uint32_t value);
uint32_t Get_Music_Event(uint32_t event);

// Os gatilhos de som e de música do mapa (posicionamentos 0xf9 e 0xfb; tipos 0x40 e 0x44, sem modelo).
// SoundTrigger (sound.cpp, 0x14 bytes de dados): com o canal (parâmetro 0) ligado toca o som (parâmetro 2), 3D na
// posição ou 2D (parâmetro 3 != 0); para quando acaba, quando o canal de parada (parâmetro 4) liga ou, sem o
// parâmetro 7, quando o canal desliga; o parâmetro 1 = 1 o apaga depois de tocar uma vez; o parâmetro 5 o faz o único
// som desses tocando (GlobalVoiceHandle); o parâmetro 6 é a dificuldade (0 todas, 1-3 até ela, 5-7 só a 1, 2 ou 3).
// MusicTrigger (music.cpp, 0xc bytes): com o canal (parâmetro 0) ligado manda o evento de música (parâmetro 2) com o
// valor 2 por 30 ticks; o canal de parada (parâmetro 1) o apaga, e o parâmetro 3 o apaga depois do primeiro evento.
struct obj_tag;
struct level_tag;
struct celglist_tag;
obj_tag* SoundTrigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void SoundTrigger_Update(obj_tag* obj);
obj_tag* MusicTrigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void MusicTrigger_Update(obj_tag* obj);

// A música de cada fase (engine/music.cpp).
void PickRandomStealth();
void PickRandomCombat(int32_t instant);
void UpdateMFX_02MayhewA();
void UpdateMFX_02MayhewB();
void UpdateMFX_02MayhewC();
void UpdateMFX_02MayhewD();
void UpdateMFX_01CastleA();
void UpdateMFX_01CastleB();
void UpdateMFX_01CastleC();
void UpdateMFX_01CastleD();
void UpdateMFX_03TowerA();
void UpdateMFX_03TowerB();
void UpdateMFX_03TowerC();
void UpdateMFX_PowerStationA1();
void UpdateMFX_PowerStationA2();
void UpdateMFX_HT_Level_Tower2A();
void UpdateMFX_HT_Level_Tower2B();
void UpdateMFX_HT_Level_Tower2Elevator();
void UpdateMFX_HT_Level_Tower2C();
void UpdateMFX_HT_Level_EvilBase();
void UpdateMFX_HT_Level_EvilSilo();
void UpdateMFX_HT_Level_EvilBaseC();
void UpdateMFX_HT_Level_SpaceStationD();

// Só no port: o estado da música do EE (relatório e testes) e a volta ao estado da partida.
struct MusicState {
    int32_t level = 0;         // a fase da linha de MapMusic em uso (MapMusicData)
    int32_t track = 0;         // a faixa pedida (MusicNowPlaying)
    int32_t last_jump = 0;     // o trecho que o IOP diz tocar (MusicLastJump)
    int32_t request = 0;       // o último trecho pedido (MusicLastJumpRequest)
    int32_t under_attack = 0;  // MusicUnderAttack
    int32_t wait_for_end = 0;  // MusicWaitForEnd: a música da fase acabou (morte, vitória, fim)
};
MusicState music_state();
void music_debug_power_on();

// Só no port: contadores desde a última zerada (relatório e nf_loader_check) e o estado de teste.
struct SoundStats {
    uint32_t calls = 0;      // chamadas de Sound_Play/Sound_Play3D (com ou sem nó)
    uint32_t requested = 0;  // Sound_Play/Sound_Play3D com nó
    uint32_t played = 0;     // pedidos que chegaram ao IOP (Sound_ReqestPlaySfx sem corte por distância)
    uint32_t speech = 0;     // pedidos de sons com legenda (Snd2Lbl) que viraram legenda
    uint32_t no_node = 0;    // sem nó livre
};
extern SoundStats g_sound_stats;
void sound_debug_power_on();
// Os nós em uso (relatório).
int sound_nodes_in_use();
