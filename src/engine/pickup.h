#pragma once
// Itens pegáveis (módulo Pickup do ACTION.ELF, 0x0018dda0-0x0018f4b8), Player_EquipKey e
// Check_Target (o objeto na mira do jogador e FlashMod, o pulso com que os itens piscam).
//
// Um item é um objeto do tipo 0x2f com um PICKUPINFO (0x40 bytes) em obj->data. Nasce de três jeitos:
// posicionado na fase (tipo 0xf0 de parsemap_create_dynamic_objects, já parado no lugar), solto por
// quem morre ou se rende (Pickup_CreateSimple: a arma da mão de um personagem ou, no multijogador, a do
// jogador; cai até o chão e some depois de 2 min) ou criado por um script ou torreta
// (Script_CreateEntity, GT_Update, ainda não portados). No multijogador os tipos 7 a 11 escolhem a
// arma do conjunto da partida (Pickup_CreateFromSet: PickupMatrix, sorteado em
// Pickup_MakeRandomWeaponSet).
//
// A cada tick (o único callback do tipo em control_funcs), Pickup_Update faz o item solto cair
// (gravidade, raio até o chão) e girar, e procura um
// jogador vivo (estado 1) a menos de raio + 1,1 m do centro da esfera com linha de visão até ela (no
// multijogador também os bots). Pickup_Handler entrega o item: armas e munição por Player_EquipWeapon/
// Player_EquipAmmo, chaves por Player_EquipKey, o colete; se o jogador não puder levar (munição cheia,
// colete já em 50, modelo da arma fora da fase) o item fica. Depois a mensagem ("Picked up %s"...), o
// canal de switch, o som e: some (respawn 0), espera escondido para reaparecer ou fica (0xffff).
//
// Tipos (PICKUPINFO::kind), pelos textos do jogo (USATxt.dat) e pelo que Pickup_Handler faz:
//   0 arma: dá a arma (já melhorada, Upgrade_Weapon) com "count" balas; se o jogador já tem a classe,
//     vira munição (mensagem de munição).                                      "Picked up %s"
//   1 munição da arma "item" (Player_EquipAmmo, "count" balas).              "Picked up %dx %s"
//   2 chave: marca BLData + 0x70c + item (as chaves das portas trancadas, Lock_*). "Picked up Code Key"
//   3 colete: o colete vai a 50 se estiver abaixo; pisca perto da câmera (flag 0x80000). Nos bots, +20
//     de vida e "count" de colete.                                              "Picked up Armor"
//   4 item da missão: só a mensagem com o texto próprio (label) e o canal de switch.
//   5 invisibilidade: BLData::visibility = 0.                                   "Invisibility"
//   6 ficha "007 Bonus": só existe com a medalha de ouro da fase (PlrStats_HasGoldMedal) e conta nas
//     estatísticas (PlrStat_LogBondBonus); gira.                                "007 Bonus collected"
//   7-11 arma do conjunto da partida (multijogador), trocada por um tipo 0 na criação.
// O jogo não tem item de vida: o colete é o que repõe a proteção.
//
// No desenho, o item é tingido pela luz da sala (flag 0x800, obj_tag::color) e, com a flag 0x80000 (o
// colete e os modelos que já a trazem), pulsa com FlashMod a menos de 4 m da câmera
// (View_SetupRenderModes_Tweak).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/player_stats.h"

struct BLData;
struct Drone_tag;

constexpr uint8_t kPickupObjectType = 0x2f;

enum : uint16_t {
    kPickupWeapon = 0,
    kPickupAmmo = 1,
    kPickupKey = 2,
    kPickupArmour = 3,
    kPickupMission = 4,
    kPickupInvisibility = 5,
    kPickupBonus = 6,
    kPickupSetFirst = 7,  // 7-11: PickupMatrix[conjunto][kind - 7]
};

// Estados (PICKUPINFO::state).
enum : uint16_t {
    kPickupFalling = 0,  // solto: cai até o chão (já pode ser pego enquanto cai)
    kPickupResting = 1,
    kPickupRespawning = 2,  // pego, escondido (obj_tag::flags 0x10) até reaparecer
};

struct alignas(16) PICKUPINFO {
    _VECTOR vel;         // 0x00 velocidade da queda em metros por tick (gravidade x REC_FRAME_RATE²)
    _VECTOR offset;      // 0x10 linha 3 da matriz menos o centro da esfera (no fim da criação)
    uint16_t state;      // 0x20 kPickupFalling/Resting/Respawning
    uint16_t kind;       // 0x22 tipo (ver o cabeçalho)
    uint16_t item;       // 0x24 a arma (índice de weapon_data) ou a chave; 0xffff não cria
    uint16_t count;      // 0x26 balas, ou colete dos bots
    uint16_t channel;    // 0x28 canal de switch ligado ao pegar (0: nenhum)
    uint16_t sound;      // 0x2a som ao pegar (0xffff: nenhum); arma e munição trocam por 0xf5, o colete
                         //      por 0x141 e a ficha por 0x14c
    uint16_t respawn;    // 0x2c 0: some ao ser pego; 0xffff: fica; senão reaparece depois de
                         //      respawn x 10 s (FRAME_RATE_INT x respawn x 10 ticks)
    uint16_t lifetime;   // 0x2e ticks até sumir sozinho (0: nunca); os soltos têm 2 min (30 s no MP)
    int16_t mp_index;    // 0x30 -1 (MP_RegisterPickup)
    uint16_t unknown_32;
    uint32_t label;      // 0x34 texto da mensagem do tipo 4 (0: sem mensagem)
    uint16_t spin;       // 0x38 gira em y a 1 rad/s (ficha 007 e multijogador)
    uint8_t unknown_3a[6];
};
static_assert(sizeof(PICKUPINFO) == 0x40);

inline PICKUPINFO* pickup_of(obj_tag* obj) { return static_cast<PICKUPINFO*>(obj->data); }

// Conjuntos de armas do multijogador (0x002b9100, .data): 11 conjuntos de 5 armas (o 10 é o sorteado
// por Pickup_MakeRandomWeaponSet), e as 27 armas que o sorteio usa (0x002b9170). Copiados do ELF na
// partida (game/static_init.cpp).
constexpr int kPickupSets = 11;
extern int16_t PickupMatrix[kPickupSets * 5];
extern int16_t UseableGuns[27];
extern uint32_t g_pickup_guards;  // acessos inválidos do original evitados (ver pickup.cpp)

obj_tag* Pickup_Create(_VECTOR* pos, _VECTOR* rot, MATRIX* m, celglist_tag* glist, uint16_t kind, uint16_t item,
                       uint32_t count, uint32_t channel, uint32_t sound, int8_t placed, uint16_t respawn,
                       uint16_t lifetime, uint32_t label);
void Pickup_Update(obj_tag* obj);
void Pickup_Handler(obj_tag* obj, PICKUPINFO* info, obj_tag* by);
void Pickup_MakeRandomWeaponSet();
obj_tag* Pickup_CreateFromSet(_VECTOR* pos, _VECTOR* rot, uint16_t kind);
// A arma "weapon" no chão, na matriz m, com "count" balas: kind 0 arma, 1 munição.
obj_tag* Pickup_CreateSimple(MATRIX* m, int16_t weapon, int16_t count, int16_t kind);
// Marca a chave "key" (BLData + 0x70c + key = 1; Player_RamSave guarda essas 0x33 chaves).
void Player_EquipKey(BLData* b, uint16_t key);
// O pulso dos itens (FlashMod) e o objeto na mira do jogador; declarada também em modules.h.
void Check_Target(obj_tag* obj);

// Chamadas dos itens ainda não portadas (stubs em modules_stubs.cpp): multijogador e os bots (as
// estatísticas, PlrStat_*, estão em engine/player_stats.h). Declaradas aqui para não mexer nos cabeçalhos compartilhados.
struct MP_PICKUP;
void MP_RegisterPickup(obj_tag* obj);
void MP_UnregisterPickup(obj_tag* obj);
MP_PICKUP* MP_getMPpickupFromPickupinfo(PICKUPINFO* info);  // stub: nulo
uint8_t* BOT_getDefaultStats(uint32_t character);            // stub: um bloco zerado
uint32_t Upgrade_MPWeapon(int16_t player, uint32_t weapon);   // stub: a própria arma
uint32_t BOTWEAP_EquipWeapon(Drone_tag* drone, int16_t weapon, int16_t rounds);  // stub: 0 (não pega)
int32_t BOTWEAP_EquipAmmo(Drone_tag* drone, int16_t weapon, int16_t rounds);     // stub: 0
uint32_t BOTWEAP_hasLoadedWeapon(Drone_tag* drone, int16_t weapon);              // stub: 0
void BOTSTATE_pickupWeaponChangeChoice(Drone_tag* drone, int16_t weapon, int8_t a, int8_t b);
void BOTWEAP_setDroneArmour(Drone_tag* drone);
void BOTSTATE_setPickupVisitTime(Drone_tag* drone, MP_PICKUP* pickup);
int32_t Txt_GetLanguage();  // portada (engine/txt.cpp)
uint32_t MP_isObjectOnTeam(obj_tag* obj, uint32_t team);  // stub: 0 (Check_Target, multijogador)

// Só para testes: estado inicial das globais do módulo.
void pickup_debug_power_on();
