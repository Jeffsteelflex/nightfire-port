#pragma once
// Torretas (módulo GT do ACTION.ELF, 0x0017f9c8-0x00181880). Ver docs/engine.md, "Torretas".
//
// Uma torreta (posicionamento 0x27, objeto 0x36; GT_Create) é dois scripts de cena tocando juntos: o corpo (parâmetro
// 2, o script A, que mira) e a base (parâmetro 3, o B); os streams 0, 1 e 2 do corpo se registram (GT_Register) como o
// clarão, o cano (de onde saem as balas) e uma terceira peça. Tipo (parâmetro 0): 1 a torreta automática (só liga com
// o canal do parâmetro 4 ligado e o do parâmetro 5 desligado; mira só no jogador; munição infinita), 0 a metralhadora
// montada (mira no jogador e nos personagens; 200 tiros; usar a desmonta e ela vira o item da arma 0x52).
//
// Estados (obj_tag::state): 0 recolhida (a automática espera os canais e um alvo), 1 abrindo (os scripts andam até a
// pausa do fim), 4 ativa (GT_Track acha o alvo; o corpo gira para ele com um slerp de 4 x REC_FRAME_RATE por tick e
// atira a cada 0,1 s a arma 0x60, som 0x4e7 na automática e 0x10a na montada, o clarão piscando e a fumaça; sem alvo
// volta ao repouso e, a automática desligada pelos canais, recolhe), 2 recolhendo (1,2 s), 3 esperando os scripts
// (a montada vira item e some), 5 atordoada (o taser, armas 0x4a e 0x4c: 3 s de giros ao acaso), 6 destruída (a vida,
// 100, acabou: soltando fumaça por 10 s), 7 controlada pelo jogador (multijogador: GT_TakeControl).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

struct SCRIPTINFO;

// Dados da torreta (0x100 bytes em obj->data, tipo 0x36).
struct alignas(16) GUNTURRET {
    MATRIX aim;              // 0x00 a orientação do corpo (linha 3: o pivô)
    MATRIX rest;             // 0x40 a orientação de repouso (a do corpo ao terminar de abrir)
    _VECTOR facing;          // 0x80 a frente da torreta: a linha 2 (montada) ou -linha 1 (automática, no teto)
    _VECTOR control;         // 0x90 jogador controlando: x elevação (±0,5), y giro (±90 graus)
    uint8_t unknown_a0[0x10];
    SCRIPTINFO* body;        // 0xb0 script do parâmetro 2
    SCRIPTINFO* base;        // 0xb4 script do parâmetro 3
    obj_tag* owner;          // 0xb8 quem pôs a torreta (ela mesma, se posicionada)
    obj_tag* target;         // 0xbc o alvo atual (GT_Track)
    obj_tag* flash;          // 0xc0 stream 0 do corpo: o clarão (fica 1 tick, flag 0x10 esconde)
    obj_tag* barrel;         // 0xc4 stream 1: o cano
    obj_tag* part;           // 0xc8 stream 2
    float health;            // 0xcc 100 (GT_ApplyDamage e o dano dos tiros nas peças)
    float muzzle;            // 0xd0 o cano: 1,5 x o z máximo das caixas dos modelos do corpo
    float spin;              // 0xd4 o giro do cano (radianos)
    float spin_speed;        // 0xd8 controlada: decai 10% por tick, 2 voltas por segundo ao atirar
    float timer;             // 0xdc segundos no estado
    float fire_timer;        // 0xe0 segundos desde o último tiro
    uint32_t unknown_e4;
    uint16_t enable;         // 0xe8 parâmetro 4: a automática só liga com este canal ligado (0: sempre)
    uint16_t disable;        // 0xea parâmetro 5: e desliga com este
    int16_t kind;            // 0xec parâmetro 0: 0 montada, 1 automática
    uint16_t param1;         // 0xee parâmetro 1
    int16_t ammo;            // 0xf0 tiros (200 na montada; -1 infinito)
    uint8_t controlled;      // 0xf2 um jogador controla
    uint8_t pad_f3[0x0d];
};
static_assert(sizeof(GUNTURRET) == 0x100);
static_assert(offsetof(GUNTURRET, body) == 0xb0 && offsetof(GUNTURRET, health) == 0xcc);
static_assert(offsetof(GUNTURRET, enable) == 0xe8 && offsetof(GUNTURRET, controlled) == 0xf2);

obj_tag* GT_Create(_VECTOR* pos, _VECTOR* rot, quaternion_tag* quat, level_tag* level, celglist_tag* glist,
                   obj_tag* owner);
void GT_Register(obj_tag* obj, uint16_t stream, void* user);
void GT_Update(obj_tag* obj);
obj_tag* GT_Track(GUNTURRET* gt, obj_tag* obj);
void GT_RandMove(obj_tag* obj, GUNTURRET* gt);
void GT_LoseControl(obj_tag* obj);
uint32_t GT_TakeControl(obj_tag* gun, obj_tag* player);
void GT_Delete(obj_tag* obj);
void GT_Disable(obj_tag* obj);
void GT_ApplyDamage(obj_tag* obj, float damage);
void GT_Activate(obj_tag* obj, obj_tag* by);
void GT_PlayerHasDied(obj_tag* player);

// Só no port: o que as torretas fizeram desde a partida.
struct TurretStats {
    uint32_t created = 0;
    uint32_t opened = 0;     // chegaram ao estado 4
    uint32_t shots = 0;      // balas da arma 0x60
    uint32_t destroyed = 0;  // estado 6
    uint32_t stunned = 0;    // estado 5
};
extern TurretStats g_turret_stats;

void turret_debug_power_on();
