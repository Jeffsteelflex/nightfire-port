#pragma once
// Portas (módulo Door do ACTION.ELF, 0x00135440-0x00136884). Ver docs/engine.md, "Portas".
//
// Uma porta é um objeto do tipo 0x1c que segue o próprio caminho (bloco 0x19): a posição no caminho
// (DOORINFO::pos, de 0 a 1) vira a matriz por Spline_Interp. Portas de girar (obj_tag::unknown_f6
// == 0) ficam fechadas no meio do caminho (0,5) e abrem para um lado ou para o outro; as de correr
// (1 e 2) vão de 0 (fechada) a 1 (aberta). obj_tag::state guarda o estado: 2 fechada, 4 aberta,
// 8 abrindo, 0x10 fechando. Portas com a flag 8 em obj_tag::unknown_fd são ligadas aos portais que
// cobrem (build_LinkDoors2Portals): fechadas, escondem a sala do outro lado (Door_IsClosed).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

enum : uint16_t {
    kDoorClosed = 2,
    kDoorOpen = 4,
    kDoorOpening = 8,
    kDoorClosing = 0x10,
};

// Dados da porta (obj_tag::data, 0x80 bytes). Os primeiros 8 bytes são o nó da lista TheDoors.
struct alignas(16) DOORINFO {
    LLNODE_tag node;          // 0x00 TheDoors
    obj_tag* obj;             // 0x08
    framelist_tag* path;      // 0x0c
    MATRIX hinge;             // 0x10 de girar: inversa da matriz fechada; de correr: a do fim do caminho
    float pos;                // 0x50 posição no caminho, 0 a 1
    float speed;              // 0x54 fração do caminho por quadro a 60 Hz
    float dir;                // 0x58 sentido do movimento (+1 ou -1)
    float side;               // 0x5c de girar: para que lado do caminho a porta abre (+1 ou -1)
    uint16_t snd_open;        // 0x60 som ao começar a abrir (params[5], 0xffff sem som)
    uint16_t snd_close;       // 0x62 som ao começar a fechar (params[6])
    uint16_t snd_locked;      // 0x64 som de porta trancada (params[7])
    uint16_t channel;         // 0x66 canal de switch: ligado com a porta aberta, e abre/fecha a porta (params[2])
    uint16_t lock_channel;    // 0x68 canal que destranca a porta (params[3])
    uint16_t group;           // 0x6a grupo: as portas do grupo se movem juntas (params[1])
    uint16_t cooldown;        // 0x6c quadros até a porta reagir de novo
    uint16_t frames;          // 0x6e quadros do caminho - 1
    uint16_t flags;           // 0x70 params[8] | params[9]: bit 0 Spline (senão KeyFrame), bit 2 começa
                              //      aberta, bit 5 não fecha sozinha; bit 4: já mexida neste quadro
    uint16_t open_time;       // 0x72 quadros aberta (fecha sozinha depois de 6 segundos)
    uint16_t msg_shown;       // 0x74 Door_DoMsg já mostrou a mensagem
    uint8_t one_way;          // 0x76 1 ou 2: só abre por um dos lados (params[10])
    uint8_t no_touch;         // 0x77 não abre ao ser tocada (params[11])
    uint8_t unknown_78[8];
};
static_assert(sizeof(DOORINFO) == 0x80);
static_assert(offsetof(DOORINFO, pos) == 0x50 && offsetof(DOORINFO, snd_open) == 0x60);
static_assert(offsetof(DOORINFO, flags) == 0x70 && offsetof(DOORINFO, one_way) == 0x76);

extern LLISTINFO_tag TheDoors;
// Estado somado das portas de cada grupo neste quadro (2 = fechadas). 0xca bytes no original.
extern int16_t DoorGroupsStates[104];

void Door_Init();
obj_tag* Door_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Door_SetupSwing(obj_tag* door, level_tag* level);
void Door_SetState(obj_tag* door, uint16_t state);
void Door_Interp(obj_tag* door, DOORINFO* info, int8_t advance);
void Door_ReactToHit(obj_tag* door);
uint32_t Door_Activate(obj_tag* door, obj_tag* by, uint8_t touch);
char* Door_DoMsg(obj_tag* door);
void Doors_Calc();
void Door_Update(obj_tag* door);
uint8_t Door_Link2Portal(obj_tag* door);
bool Door_IsLocked(obj_tag* door);
bool Door_IsClosed(const obj_tag* door);

// Chamados pelas portas e ainda não portados (stubs em modules_stubs.cpp).
uint32_t Sound_Play3D(uint32_t sound, _VECTOR* pos, float volume, float a, float b, int16_t c, uint32_t d, int32_t e);
char* Txt_BindLabel(uint32_t label, int8_t flag);
void Text_AddMsg(int8_t a, uint8_t b, int32_t type, char* text, uint32_t c, int16_t time);
