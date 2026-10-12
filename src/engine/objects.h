#pragma once
// Criação dos objetos dos posicionamentos (parsemap_create_dynamic_objects) e os *_Create já
// portados. Os outros tipos ainda passam por pending_create (modules.h), que só registra a chamada
// e guarda um objeto provisório para o renderizador.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

// Ambiente de uma sala (Env_Create, 0x50 bytes): chuva ou neve e a direção do vento.
struct ENV_tag {
    _VECTOR dir;          // 0x00 direção (linha 2 de RotMatrix(rot))
    _VECTOR side;         // 0x10 dir x (0, 1, 0)
    _VECTOR pos;          // 0x20
    float unknown_30;     // params[3] * 0,1
    float unknown_34;     // params[3] * 0,1
    float unknown_38;     // -1
    float unknown_3c;     // params[7]
    float unknown_40;     // params[8]
    float inv_radius;     // 0x44: 1 / raio da sala
    uint8_t unknown_48[3];  // params[4..6]
    uint8_t kind;         // 0x4b params[0]
    uint8_t unknown_4c;   // params[1] quando params[0] == 0
    uint8_t unknown_4d;   // 1
    uint8_t unknown_4e;   // params[2]
    uint8_t pad[1];
};
static_assert(sizeof(ENV_tag) == 0x50);

extern ENV_tag* GlobalEnv;
extern _VECTOR SunVector;

// Só no port: chamadas a parsemap_create_dynamic_objects (para o nf_loader_check).
extern uint32_t g_dynamic_object_calls;

obj_tag* DynamicObject_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
obj_tag* Break_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, level_tag* level);
void Env_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, level_tag* level);
