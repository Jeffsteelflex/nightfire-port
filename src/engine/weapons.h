#pragma once
// Tabelas de armas e munição do ACTION.ELF. Não estão prontas no .data do executável (weapon_data
// é montada por um construtor estático do GCC, _GLOBAL_$I$weapon_data) e são dados do jogo: o port
// as preenche na partida a partir do ACTION.ELF do usuário (nf::init_game_tables, game/static_init.h).

#include <cstdint>

constexpr uint32_t kWeaponStride = 0x10c;  // weapon_definition_tag
constexpr uint32_t kWeaponDataCount = 115;
constexpr uint32_t kWeaponDataSize = kWeaponStride * kWeaponDataCount;  // 30820 bytes, como o símbolo
constexpr uint32_t kWeaponCallbackOffset = 0x104;  // ponteiro de função chamado com o objeto do jogador

constexpr uint32_t kAmmoStride = 0xc;
constexpr uint32_t kAmmoDataCount = 34;
constexpr uint32_t kAmmoDataSize = kAmmoStride * kAmmoDataCount;  // 408 bytes, como o símbolo

extern uint8_t weapon_data[kWeaponDataSize];  // definida em weapons.cpp
extern uint8_t ammo_data[kAmmoDataSize];      // definida em weapons.cpp (no PS2, .data comum)

// Traduz um ponteiro de código do PS2 guardado numa tabela (o símbolo do ELF e o endereço) para a
// função equivalente do port, ou 0 se ela ainda não foi portada. init_game_tables chama isto para
// cada ponteiro de código das tabelas copiadas.
uint32_t Weapons_ResolveCode(const char* symbol_name, uint32_t ps2_address);
