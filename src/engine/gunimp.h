#pragma once
// O canhão montado (GunImp_* do ACTION.ELF, 0x00131f20-0x0013306f). Ver docs/engine.md, "O canhão montado".
//
// Um canhão (posicionamento 0x34, objeto 0x47; GunImp_Create) é a base (o modelo do posicionamento, que gira em y) e o
// cano (o modelo da arma de TurretGuns, que também inclina), com a matriz do quatérnio do posicionamento. No jogo
// sozinho a arma é sempre a 11 de TurretGuns (arma 0x67, o canhão de laser de 0700000d); no multijogador a da regra
// da partida (MPSettings +0x1b4, 10 = ao acaso; sem MPSettings +0x1b8 nenhum canhão nasce).
//
// Usar (GunImp_Activate, de Player_Activate) com o canhão livre e o jogador de pé: o jogador vai ao subestado 0xc,
// congelado (Player_Disable com congelar), câmera no modo 0xe (a primeira pessoa, presa ao canhão), sem arma, com
// BLData::ride = o canhão, e o campo de visão cai para 30 graus (60 no multijogador). A cada tick (GunImp_Update, estado
// 1): usar de novo solta (GunImp_Deactivate); os analógicos giram (0,015 rad por tick, a inclinação entre -90 e +45
// graus) e o canhão segue 15% por tick; o jogador fica atrás da base (o z mínimo da caixa do modelo x 0,75) olhando
// pela inclinação; o tiro (ação 9) dispara a arma da tabela pela boca do cano a cada "atraso" ticks com o clarão, uma
// luz de um tick e, nas armas de feixe (0x67, 0x70), o feixe até onde o raio de 1 km bate e o clarão do impacto (a
// 0x67 ainda espalha 5 de dano num raio de 4 m, Explode_Propagate). Cada tiro de feixe esquenta 2 (o 0x61, 8); acima de
// 200 o canhão superaquece (solta fumaça a cada 6 quadros e não atira) até esfriar a 0 (1 por tick). O som contínuo do
// feixe (TurretGuns +0x18) para ao soltar o gatilho ou superaquecer. A morte do jogador (GunImp_PlayerHasDied) o solta.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

// Uma arma de canhão (TurretGuns, 0x0026f520, 12 de 0x1c bytes, copiada do ELF na partida).
struct TURRETGUN {
    float muzzle[3];     // 0x00 a boca no espaço do cano
    uint32_t entity;     // 0x0c o modelo do cano
    uint32_t weapon;     // 0x10 a arma (weapon_data) dos tiros
    float delay;         // 0x14 ticks a 60 Hz entre tiros
    uint32_t sound;      // 0x18 o som do tiro (das armas de feixe: o contínuo)
};
static_assert(sizeof(TURRETGUN) == 0x1c);
extern TURRETGUN TurretGuns[12];

// Dados do canhão (0xa0 bytes em obj->data, tipo 0x47).
struct alignas(16) GUNIMP_tag {
    obj_tag* gun;          // 0x00 o cano (o modelo de TurretGuns)
    obj_tag* player;       // 0x04 quem está usando
    uint32_t unknown_08[2];
    MATRIX base;           // 0x10 a matriz do posicionamento
    uint32_t unknown_50[4];
    _VECTOR seat;          // 0x60 onde fica o jogador, no espaço da base (z: o z mínimo da caixa x 0,75)
    float pitch_target;    // 0x70 a inclinação pedida (entre -pi/2 e pi/4)
    float yaw_target;      // 0x74 o giro pedido
    float unknown_78;      // 0x78 zerado a cada tick
    float unknown_7c;
    float pitch;           // 0x80 a inclinação do cano
    float yaw;             // 0x84 o giro da base
    uint32_t unknown_88[2];
    float heat;            // 0x90 o calor dos tiros (esfria 1 por tick)
    float fire_wait;       // 0x94 ticks até o próximo tiro
    uint32_t loop;         // 0x98 o som contínuo do feixe
    uint8_t overheated;    // 0x9c superaquecido (até esfriar)
    uint8_t kind;          // 0x9d a entrada de TurretGuns
    uint8_t pad[2];
};
static_assert(sizeof(GUNIMP_tag) == 0xa0);
static_assert(offsetof(GUNIMP_tag, seat) == 0x60 && offsetof(GUNIMP_tag, heat) == 0x90);

obj_tag* GunImp_Create(_VECTOR* pos, quaternion_tag* quat, celglist_tag* glist, level_tag* level);
void GunImp_Update(obj_tag* obj);
void GunImp_Activate(obj_tag* obj, obj_tag* by);
void GunImp_Deactivate(obj_tag* obj);
void GunImp_PlayerHasDied(obj_tag* player);

// Só no port: o que os canhões fizeram desde a partida.
struct GunImpStats {
    uint32_t created = 0;
    uint32_t mounted = 0;     // GunImp_Activate que montou
    uint32_t shots = 0;       // tiros
    uint32_t overheats = 0;   // superaquecimentos
    uint32_t guards = 0;      // GunImp_Deactivate sem jogador (o original leria o objeto nulo)
};
extern GunImpStats g_gunimp_stats;

void gunimp_debug_power_on();
