#include "engine/code_resolve.h"

#include "engine/drone.h"
#include "engine/hud.h"
#include "engine/loadsave.h"
#include "engine/weapons.h"

uint32_t Engine_ResolveCode(const char* symbol_name, uint32_t ps2_address) {
    if (const uint32_t a = Drone_ResolveCode(symbol_name, ps2_address)) return a;
    if (const uint32_t a = Hud_ResolveCode(symbol_name, ps2_address)) return a;
    if (const uint32_t a = LS_ResolveCode(symbol_name, ps2_address)) return a;
    return Weapons_ResolveCode(symbol_name, ps2_address);
}
