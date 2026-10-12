#pragma once
// O sistema de segurança (módulo SSys do ACTION.ELF, 0x001ad878-0x001b0968): as câmeras de vigilância (Sensor_*), os
// monitores que mostram o que elas veem (Monitor_*) e as contagens regressivas do sistema (SSys_*). Ver docs/engine.md,
// "Câmeras de segurança, monitores e holofotes".
//
// - Câmera (posicionamento 0x28, objeto 0x37; Sensor_Create): varre o caminho do posicionamento (Spline_Interp, ida e
//   volta pelo laço; o servo toca 0x4dd no meio e no fim) ou fica parada, com um cone (parâmetro 1, a abertura em graus)
//   e um alcance (parâmetro 2). Vê o jogador pelo feixe (o raio da frente da câmera bate nele: Sensor_SetBeam) ou pelo
//   cone com linha de visão (Sensor_InCone); com a câmera de script (4) ativa, não vê. Estados (obj_tag::state): 0
//   varrendo (o canal de alarme desligado; vendo o jogador por mais de 30 ticks, bipa 0x4a9 e passa ao 1), 1 seguindo o
//   jogador (Vec_Track; o bipe acelera; depois de 2 s vendo, o canal de alarme, parâmetro 3, liga; sem vê-lo por mais
//   de FRAME_RATE_INT ticks, passa ao 2), 2 voltando ao caminho (slerp de 0,1 por tick até a orientação do caminho; o
//   alarme desliga), 3 desligada (o canal do parâmetro 4 ligado: o modelo vira o LOD, o feixe some, o zumbido 0x4aa
//   para; o canal desligando de novo volta ao 0). Peças: o feixe (parâmetro 0, escalado até o que o raio acerta) e o
//   suporte (parâmetro 7, só gira em y). O alarme só fica ligado enquanto a câmera vê o jogador; quem espera o canal
//   (gatilhos, geradores, personagens) faz o resto. Sensor_ApplyDamage não faz nada: no jogo uma câmera "destruída" é
//   um destrutível que liga o canal do parâmetro 4.
// - Monitor (posicionamento 0x30, objeto 0x38; Monitor_Create): usar (Player_Activate) mostra a primeira câmera do grupo
//   do parâmetro 0 (SENSOR::group) que não está desligada: a câmera do jogador (glb_viewer do índice dele) perde a
//   função de atualização e recebe a matriz da câmera de segurança a cada tick, o jogador fica parado
//   (Player_Disable) e o HUD troca os painéis 0 e 1 pelo 16 (o "REC" da câmera de segurança). Usar de novo passa à
//   próxima câmera do grupo; depois da última o jogador volta (Player_Enable, a função de atualização
//   Player_PositionCamera). Uma câmera desligada enquanto vista passa à próxima (desta vez dando a volta na lista). O
//   canal do parâmetro 8 da câmera (SENSOR::view_channel) liga enquanto ela é mostrada. Com o parâmetro 1 = 0 o
//   monitor tem 1 de vida: um tiro (Collide_GetDamageNObjects) o quebra (LOD seguinte, Explode_Create do script
//   0x6000027) e devolve o jogador. Não há imagem no modelo do monitor nem textura renderizada: o PS2 mostra a câmera
//   em tela cheia, com o painel 16 do HUD por cima.
// - SSysItems: quatro contagens regressivas (laser 1, laser 2, câmeras 1 e 2: itens 1-4 de 8 bytes {tempo, canal,
//   estado}). Um interruptor de script com o item (Script_CreateEntity) grava o canal; SSys_Monitor (Game_Run, fora
//   das cenas) começa a contagem de 240 s quando o canal liga e toca as falas do sistema (SSys_Msg: as tabelas
//   Laser1/Laser2/Cam1/Cam2 do ELF) aos 180, 120, 60 e 6 s e no fim, quando desliga o canal; o canal desligado por
//   outro caminho para a contagem (fala 0). O laser 2 de 0700000b primeiro encerra a contagem do laser 1 e espera 8 s.
//   SSys_Init zera as contagens na carga fria; SSys_RamLoad as traz da cópia do recomeço.

#include <cstddef>
#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct viewer_tag;

// Dados da câmera de segurança (0x64 bytes em obj->data, tipo 0x37). A orientação fica num quatérnio desalinhado
// (+0x14): o port a lê e escreve por cópia (o PS2 usa só lwc1/swc1 nela).
struct SENSOR {
    LLNODE_tag node;         // 0x00 CameraList (só as com o parâmetro 5 = 0)
    P32<obj_tag> obj;        // 0x08 a câmera
    P32<celglist_tag> glist;  // 0x0c o modelo (volta a ele quando a câmera religa)
    P32<celglist_tag> broken;  // 0x10 o LOD do modelo: a câmera desligada
    float quat[4];           // 0x14 a orientação ao seguir o jogador (Vec_Track) e ao voltar (slerp)
    P32<framelist_tag> path;  // 0x24 a varredura (o caminho do posicionamento)
    float t;                 // 0x28 o quadro no caminho
    float speed;             // 0x2c quadros por tick a 60 Hz
    uint32_t frames;         // 0x30 quadros do caminho - 1 (0: parada)
    uint32_t angle;          // 0x34 parâmetro 1: a abertura do cone em graus
    float cos_half;          // 0x38 cosseno e seno da metade da abertura
    float sin_half;          // 0x3c
    float range;             // 0x40 parâmetro 2: alcance do feixe e do cone
    float seen;              // 0x44 segundos seguindo o jogador (estado 1)
    float beep;              // 0x48 o bipe do estado 1; no 0, "o servo já tocou no meio do caminho"
    P32<obj_tag> beam;       // 0x4c parâmetro 0: o feixe
    P32<obj_tag> base;       // 0x50 parâmetro 7: o suporte
    uint32_t hum;            // 0x54 o zumbido (Sound_Play3D 0x4aa)
    uint16_t alarm;          // 0x58 parâmetro 3: ligado depois de 2 s vendo o jogador
    uint16_t off;            // 0x5a parâmetro 4: ligado, a câmera desliga (estado 3)
    uint16_t view_channel;   // 0x5c parâmetro 8: ligado enquanto um monitor mostra a câmera
    uint8_t group;           // 0x5e parâmetro 6: os monitores deste grupo a mostram
    uint8_t unlisted;        // 0x5f parâmetro 5: fora da CameraList (nenhum monitor a mostra)
    uint8_t viewed;          // 0x60 1 um monitor começou a mostrá-la (o modelo some), 2 mostrando
    uint8_t pad[3];
};
static_assert(sizeof(SENSOR) == 0x64);
static_assert(offsetof(SENSOR, path) == 0x24 && offsetof(SENSOR, beam) == 0x4c && offsetof(SENSOR, viewed) == 0x60);

// Dados do monitor (0x14 bytes, tipo 0x38).
struct MONITOR_tag {
    P32<viewer_tag> viewer;  // 0x00 a câmera do jogador que usou
    P32<obj_tag> camera;  // 0x04 a câmera de segurança mostrada (nula: nenhuma)
    P32<obj_tag> player;  // 0x08 quem usou
    float health;        // 0x0c 1 com o parâmetro 1 = 0; 0 é indestrutível
    uint16_t index;      // 0x10 o índice da câmera do jogador
    uint16_t pad;
};
static_assert(sizeof(MONITOR_tag) == 0x14);

obj_tag* Sensor_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
int32_t Sensor_SetBeam(obj_tag* player, obj_tag* obj, SENSOR* s);
uint32_t Sensor_InCone(obj_tag* player, obj_tag* obj, SENSOR* s);
void Sensor_Update(obj_tag* obj);
obj_tag* Monitor_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag* next);
void Monitor_Update(obj_tag* obj);
void Monitor_Activate(obj_tag* obj, obj_tag* by);
void SSys_Msg(uint16_t item, uint16_t msg);
void SSys_Monitor();
void SSys_Init();
void SSys_RamLoad(uint8_t* items);

// As falas do sistema (Laser1.134, Laser2.135, Cam1.132, Cam2.133 do ACTION.ELF, 0x002beef0-0x002befcf, copiadas na
// partida): por fala, 0 a 6, {rótulo do texto, som}; SSys_Msg só toca o som. SSysMinutes: MIN3, MIN2, MIN1 e MIN01
// (0x0030cd84, os segundos das falas 5, 4, 3 e 2 da contagem: 180, 120, 60 e 6).
struct SSYSMSG {
    uint32_t label;
    uint32_t sound;
};
extern SSYSMSG Laser1_134[7];
extern SSYSMSG Laser2_135[7];
extern SSYSMSG Cam1_132[7];
extern SSYSMSG Cam2_133[7];
extern float SSysMinutes[4];

// SSysItems: 5 itens de 8 bytes (o 0 não é usado): float tempo (-1 parado), byte canal (0xff nenhum), byte estado.
extern uint8_t SSysItems[40];
extern float SSysDelayTimer;

// Só no port: o que o sistema de segurança fez desde a partida (nf_loader_check, relatório das capturas, testes).
struct SecurityStats {
    uint32_t sensors = 0;    // Sensor_Create
    uint32_t monitors = 0;   // Monitor_Create
    uint32_t spotted = 0;    // câmeras que passaram a seguir o jogador (estado 0 -> 1)
    uint32_t alarms = 0;     // ticks com o alarme de uma câmera ligado (estado 1, 2 s vendo)
    uint32_t lost = 0;       // câmeras que perderam o jogador (1 -> 2)
    uint32_t disabled = 0;   // câmeras desligadas pelo canal (estado 3)
    uint32_t viewed = 0;     // câmeras mostradas num monitor (Monitor_Activate e as trocas)
    uint32_t broken = 0;     // monitores quebrados
    uint32_t countdowns = 0; // contagens regressivas do SSys começadas
    uint32_t messages = 0;   // falas do SSys (SSys_Msg)
    uint32_t guards = 0;     // casos em que o original leria um objeto nulo (sensor.cpp)
};
extern SecurityStats g_security_stats;

// A linha "seguranca" do relatório das capturas (main.cpp): as câmeras por estado (varrendo/seguindo/voltando/
// desligadas) e com o alarme ligado, o monitor mostrando (a câmera e o grupo), as lâmpadas dos holofotes por estado
// (varrendo/seguindo/quebradas/apagadas), as minas, o canhão montado e os mísseis (armados/sabotados/lançados).
int Security_Describe(char* out, size_t size);

void security_debug_power_on();
