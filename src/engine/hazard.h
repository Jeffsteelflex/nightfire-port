#pragma once
// Perigos e efeitos de fase pequenos: as minas (Mine_*, 0x0017c930-0x0017d02f, no módulo Explode do ACTION.ELF), os
// atiradores (Shooter_*, 0x0017d030-0x0017d177) e o tremor de fim de fase (Apocalypse_*, 0x0018d4f0-0x0018d61b e
// 0x0018dcc0). Ver docs/engine.md, "Minas, atiradores, inversores e o tremor".
//
// - Mina (posicionamento 0xfe, objeto 0x4b; Mine_Create): gira em y (parâmetro 2 x 0,0002 rad por tick) e balança em
//   y em volta do posicionamento (seno do quadro x parâmetro 3 x 0,0002, amplitude parâmetro 4 x 0,1). Qualquer acerto
//   de um objeto (o jogador ou um personagem encostando, um projétil) a explode: a explosão do script do parâmetro 5
//   com o dano do parâmetro 0 e o raio do parâmetro 1 (o original tem Explode_Create inline, com a própria mina como
//   causadora e a sala dela) e a mina some. As de 07000015 (as minas do rio): dano 80, raio 10 ou 15, sem giro nem
//   balanço, script 0x6000052.
// - Atirador (posicionamento 0xd2, objeto 0x4e, sem dados; Shooter_Create): com o canal do parâmetro 0 (em
//   obj_tag::state) ligado, dá um tiro da arma do parâmetro 1 (em obj_tag::unknown_f6) pela frente do posicionamento
//   (reto para baixo se a classe da arma for 0x3b) e some. 07000001, 07000015 e 0700004a.
// - Tremor (posicionamento 0x4a, objeto 0x54, sem sala; Apocalypse_Create): com o canal do parâmetro 0 ligado, por
//   parâmetro 1 segundos: vibração do controle (parâmetro 2, a força caindo com o tempo), o sacudir da câmera 0
//   (parâmetro 3: viewer_tag::shake = 5 x o que falta) e o clarão (parâmetro 4: Player_SetFlashBang com o que falta e
//   382 x a fração, no máximo 255); no fim some. 07000014-16 e 0700001b.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

// Dados da mina (0x30 bytes, tipo 0x4b).
struct alignas(16) MINE_tag {
    _VECTOR home;      // 0x00 a posição do posicionamento (o y é o centro do balanço)
    float spin;        // 0x10 parâmetro 2 x 0,0002: giro em y por tick
    float bob_rate;    // 0x14 parâmetro 3 x 0,0002: radianos por quadro do balanço
    float bob;         // 0x18 parâmetro 4 x 0,1: amplitude do balanço
    float damage;      // 0x1c parâmetro 0
    float radius;      // 0x20 parâmetro 1
    uint32_t script;   // 0x24 parâmetro 5: o efeito da explosão
    uint8_t pad[8];
};
static_assert(sizeof(MINE_tag) == 0x30 && offsetof(MINE_tag, script) == 0x24);

// Dados do tremor (0x10 bytes, tipo 0x54).
struct APOCALYPSE_tag {
    float left;        // 0x00 ticks a 60 Hz que faltam (parâmetro 1 x 60)
    float total;       // 0x04
    uint16_t channel;  // 0x08 parâmetro 0
    int16_t rumble;    // 0x0a parâmetro 2: força da vibração
    uint8_t shake;     // 0x0c parâmetro 3: sacode a câmera 0
    uint8_t flash;     // 0x0d parâmetro 4: o clarão
    uint8_t on;        // 0x0e o canal já ligou
    uint8_t pad;
};
static_assert(sizeof(APOCALYPSE_tag) == 0x10);

obj_tag* Mine_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Mine_Update(obj_tag* obj);
obj_tag* Shooter_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Shooter_Update(obj_tag* obj);
obj_tag* Apocalypse_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Apocalypse_Update(obj_tag* obj);

// Só no port: o que esses objetos fizeram desde a partida.
struct HazardStats {
    uint32_t mines = 0;        // Mine_Create
    uint32_t mines_blown = 0;  // minas que explodiram
    uint32_t shooters = 0;     // Shooter_Create
    uint32_t shots = 0;        // tiros dos atiradores
    uint32_t quakes = 0;       // Apocalypse_Create
    uint32_t quakes_on = 0;    // tremores que começaram
};
extern HazardStats g_hazard_stats;

void hazard_debug_power_on();
