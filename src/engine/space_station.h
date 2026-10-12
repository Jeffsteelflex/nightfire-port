#pragma once
// O laser e os mísseis da estação espacial (Create_SpaceLaser, SpaceLaser_*, Create_SpaceMissile, SpaceMissile_* do
// ACTION.ELF, 0x001a9ef0-0x001aa9ff; 0700001b). Ver docs/engine.md, "A estação espacial".
//
// - Laser (posicionamento 0x38, objeto 0x50 sem modelo; Create_SpaceLaser): com o canal de ligar (parâmetro 7) ligado,
//   a contagem do parâmetro 3 (segundos) corre; a cada 10 s um dos dois scripts de carga (parâmetros 0 e 1, ao acaso:
//   o evento 15 do script registra a boca, SpaceLaser_Register) mira onde o jogador estava, o som 0x5de da metade em
//   diante, e no fim o tiro: o raio de 1 km até a parede, o som 0x46a, um projétil da arma 0x6b, o feixe (PositionBeam,
//   5 ticks) e as faíscas do emissor 0xc000010 no ponto. Acabada a contagem, o script do fim (parâmetro 2) toca em laço
//   e, parâmetro 4 segundos depois, o canal do parâmetro 5 liga (o 99 de 0700001b: missão fracassada). O canal do
//   parâmetro 6 o apaga.
// - Míssil (posicionamento 0x39, objeto 0x4f sem modelo; Create_SpaceMissile): o script do parâmetro 0 (o míssil no
//   silo) e até quatro de acompanhamento (8-11) tocam desde a carga; com o canal do parâmetro 7 ligado, a contagem do
//   parâmetro 3 (segundos) corre e, no fim, o míssil se arma (MissileDeploy[índice - 1] = 1: o painel 20 do HUD pisca)
//   e o script do silo toca até a pausa dele; então decide: com o canal do índice (parâmetro 4, 1 a 8) ligado — o
//   jogador sabotou o míssil — toca o script do parâmetro 2 (MissileDeploy 2) e, no fim, liga o canal do parâmetro 6;
//   sem ele, o lançamento (o script do parâmetro 1, MissileDeploy 3) e o canal do parâmetro 5. Create_SpaceMissile
//   zera MissileDeploy (os 8 bytes) a cada míssil criado, como o original.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

struct SCRIPTINFO;

// Dados do laser (0x50 bytes, tipo 0x50).
struct alignas(16) SPACELASER_tag {
    _VECTOR origin;        // 0x00 a boca (SpaceLaser_Register: o stream do script de carga que a registra)
    _VECTOR target;        // 0x10 o alvo: o jogador quando a carga começa, depois o ponto do acerto
    uint32_t charge[2];    // 0x20 parâmetros 0 e 1: os scripts de carga
    uint32_t final;        // 0x28 parâmetro 2: o script do fim (em laço)
    SCRIPTINFO* script;    // 0x2c o script tocando
    float countdown;       // 0x30 parâmetro 3: segundos até o fim (correm com o canal de ligar)
    float after;           // 0x34 parâmetro 4: segundos do fim até o canal
    float wait;            // 0x38 segundos até a próxima carga (10)
    uint32_t sound;        // 0x3c o som da carga
    uint16_t done;         // 0x40 parâmetro 5: ligado depois do fim
    uint16_t stop;         // 0x42 parâmetro 6: ligado, o laser some
    uint16_t enable;       // 0x44 parâmetro 7 (0: sempre)
    uint8_t pad[10];
};
static_assert(sizeof(SPACELASER_tag) == 0x50 && offsetof(SPACELASER_tag, done) == 0x40);

// Dados do míssil (0x28 bytes, tipo 0x4f).
struct SPACEMISSILE_tag {
    SCRIPTINFO* script;    // 0x00 parâmetro 0 (o silo), depois o do desfecho
    uint32_t launch;       // 0x04 parâmetro 1: o script do lançamento
    uint32_t sabotage;     // 0x08 parâmetro 2: o script do míssil sabotado
    uint16_t index;        // 0x0c parâmetro 4: o número do míssil (1-8): MissileDeploy e o canal da sabotagem
    uint16_t launched;     // 0x0e parâmetro 5: ligado no fim do lançamento
    uint16_t sabotaged;    // 0x10 parâmetro 6: ligado no fim da sabotagem
    uint16_t enable;       // 0x12 parâmetro 7 (0: sempre)
    float delay;           // 0x14 parâmetro 3: segundos até se armar
    SCRIPTINFO* extra[4];  // 0x18 parâmetros 8-11: os scripts de acompanhamento
};
static_assert(sizeof(SPACEMISSILE_tag) == 0x28);

extern uint8_t MissileDeploy[8];  // 0 parado, 1 armado, 2 sabotado, 3 lançado (o painel 20 do HUD)

obj_tag* Create_SpaceLaser(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void SpaceLaser_Update(obj_tag* obj);
void SpaceLaser_Register(obj_tag* obj, uint16_t stream, void* user);
obj_tag* Create_SpaceMissile(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void SpaceMissile_Update(obj_tag* obj);

// Só no port: o que a estação fez desde a partida.
struct SpaceStats {
    uint32_t lasers = 0;     // Create_SpaceLaser
    uint32_t charges = 0;    // cargas do laser
    uint32_t shots = 0;      // tiros do laser
    uint32_t missiles = 0;   // Create_SpaceMissile
    uint32_t armed = 0;      // mísseis armados
    uint32_t launched = 0;   // lançados
    uint32_t sabotaged = 0;  // sabotados
    uint32_t guards = 0;     // sem jogador (o original leria o objeto nulo)
};
extern SpaceStats g_space_stats;

void space_station_debug_power_on();
