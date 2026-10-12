// Tabelas de armas e munição (ver weapons.h). Começam zeradas; nf::init_game_tables as copia da
// imagem do ACTION.ELF depois de rodar os construtores estáticos, como o __main do PS2.
//
// O módulo de armas do jogador (player_weapon.h) lê weapon_data como weapon_definition_tag.

#include "engine/weapons.h"

alignas(16) uint8_t weapon_data[kWeaponDataSize];
alignas(16) uint8_t ammo_data[kAmmoDataSize];

// Nenhuma arma do jogo tem esse ponteiro (o campo +0x104 fica zerado nas 115); ainda nenhuma função
// é traduzida: todo ponteiro de código vira 0 ("não portada").
uint32_t Weapons_ResolveCode(const char* /*symbol_name*/, uint32_t /*ps2_address*/) { return 0; }
