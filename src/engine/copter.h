#pragma once
// Helicópteros (módulo Copter do ACTION.ELF, 0x0017d340-0x0017e5cc). Ver docs/engine.md, "Helicópteros".
//
// Cada posicionamento 0x2f é um caminho (COPTERPATH, na lista CopterPaths): grupo (parâmetro 1), índice (2), quadro de
// entrada (3), duração (13 ou a do caminho) e tipo (0: o primeiro caminho, que também cria o helicóptero; 2: o caminho
// de saída; outros: caminhos de ronda). O helicóptero (objeto 0x3b, sem modelo: as peças são as entidades do script
// player do parâmetro 4 em laço, registradas por Copter_Register) segue o caminho; com o canal do parâmetro 6 desligado
// fica escondido e parado. Estados (obj_tag::state): 0 conta os caminhos do grupo, 1 ronda (a cada 4 s / DiffMod
// procura o jogador num cone de 45 graus à frente, a até 1 km e à vista), 2 ataca (Music_Event 2; metralhadora, arma
// 0x61 em 07000008 e 0x71 nas outras, ou mísseis, 0x62 e 0x6d, a cada FRAME_RATE_INT x DiffMod ticks; até 5 s x
// DiffMod, 100 de dano, ou perder o jogador de vista), 4 foge pelo caminho de saída (depois de levar mais dano que o
// máximo: 299 x DiffMod) e liga o canal do parâmetro 7, 5 acabou a fuga (canal, some), 6 explode (07000012 e 0700000b).
// O dano é o dos tiros nas nove peças, filtrado e multiplicado por Bits (máscara de classes de tiro e multiplicador
// por peça, do ELF); acima da metade do máximo a peça 3 solta fumaça. O canal do parâmetro 8 liga depois de
// parâmetro 9 ticks (com 100, o helicóptero só ronda e some no fim do caminho).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

// Um caminho de helicóptero (0x1c bytes, nó de CopterPaths).
struct COPTERPATH {
    LLNODE_tag node;          // 0x00
    framelist_tag* path;      // 0x08
    float speed;              // 0x0c quadros por tick: quadros x REC_FRAME_RATE x (FRAME_RATE / duração)
    uint16_t frames;          // 0x10 quadros - 1
    uint16_t group;           // 0x12 parâmetro 1
    uint16_t start;           // 0x14 parâmetro 3 (no máximo frames): quadro onde o helicóptero entra no caminho
    uint16_t index;           // 0x16 parâmetro 2
    uint16_t kind;            // 0x18 parâmetro 0
    uint16_t pad_1a;
};
static_assert(sizeof(COPTERPATH) == 0x1c);

// O helicóptero (0x7c bytes em obj->data; também nó de CopterList).
struct alignas(4) COPTER {
    LLNODE_tag node;          // 0x00
    obj_tag* obj;             // 0x08
    uint8_t unknown_0c[0x10];
    float quat[4];            // 0x1c a rotação do caminho (Spline_Interp; quaternion_tag sem o alinhamento de 16)
    obj_tag* sp;              // 0x2c o script player das peças
    // 0x30 as peças (Copter_Register; menos a 2, sem colisão de tiro): 2 o corpo (Copter_GetBody), 3 de onde sai a
    // fumaça, 5 a metralhadora, 6 o clarão dela, 7 e 8 os lançadores de mísseis
    obj_tag* parts[9];
    COPTERPATH* path;         // 0x54 o caminho atual
    COPTERPATH* exit;         // 0x58 o caminho de saída (tipo 2) do grupo
    float t;                  // 0x5c quadro no caminho
    uint32_t unknown_60;
    float damage;             // 0x64
    float max_damage;         // 0x68 299 x DiffMod
    float attack_damage;      // 0x6c dano desde o começo do ataque
    uint16_t enable;          // 0x70 parâmetro 6: escondido até o canal ligar
    uint16_t done;            // 0x72 parâmetro 7: ligado ao fugir ou explodir
    uint16_t channel;         // 0x74 parâmetro 8 (100: só ronda)
    uint16_t delay;           // 0x76 parâmetro 9: ticks até ligar "channel"
    uint16_t paths;           // 0x78 caminhos de ronda do grupo
    uint16_t pad_7a;
};
static_assert(sizeof(COPTER) == 0x7c);
static_assert(offsetof(COPTER, sp) == 0x2c && offsetof(COPTER, path) == 0x54 && offsetof(COPTER, enable) == 0x70);

// Os multiplicadores de dano por peça (Bits, 0x002a37e8, do ELF): a máscara de classes de tiro que a peça ignora
// (Collide_FilterBullets) e o multiplicador.
struct COPTERBIT {
    uint16_t mask;
    uint16_t pad;
    float mul;
};
static_assert(sizeof(COPTERBIT) == 8);
extern COPTERBIT Bits[9];

extern LLISTINFO_tag CopterList;   // os helicópteros (nós COPTER)
extern LLISTINFO_tag CopterPaths;  // os caminhos
extern float DiffMod;              // 1, 1,5, 2 ou 4 pela dificuldade (Copter_Create)
extern float COPT_COS, COPT_SIN;   // o cone de 45 graus (o construtor estático do ELF)

obj_tag* Copter_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void Copter_Init();
void Copter_Register(obj_tag* obj, uint16_t part, void* user);
void Copter_Update(obj_tag* obj);
uint32_t Copter_Move(obj_tag* obj, COPTER* c, int8_t pos_only);
uint32_t Copter_FireGun(obj_tag* obj, COPTER* c);
uint32_t Copter_FireMissile(obj_tag* obj, COPTER* c);
obj_tag* Copter_GetBody(void* copter);
void Copter_Delete(obj_tag* obj);
uint32_t Intersect_ConeSphere(const _VECTOR* apex, const _VECTOR* dir, float cos_a, float sin_a, const _VECTOR* sphere);

// Só no port: o que os helicópteros fizeram desde a partida.
struct CopterStats {
    uint32_t created = 0;
    uint32_t attacks = 0;   // entradas no estado 2
    uint32_t shots = 0;     // balas e mísseis
    uint32_t fled = 0;      // estado 4
    uint32_t exploded = 0;  // estado 6
};
extern CopterStats g_copter_stats;

void copter_debug_power_on();
