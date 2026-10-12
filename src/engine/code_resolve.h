#pragma once
// Tradução dos ponteiros de código das tabelas copiadas do ELF (game/static_init.cpp) para as funções
// portadas: cada módulo conhece as suas (Weapons_ResolveCode, Drone_ResolveCode, Hud_ResolveCode); 0 = ainda não
// portada.

#include <cstdint>

uint32_t Engine_ResolveCode(const char* symbol_name, uint32_t ps2_address);
