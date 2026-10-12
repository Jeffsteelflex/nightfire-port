#pragma once
// Luzes (módulo Lights/Light do ACTION.ELF): a luz ambiente dos objetos (Lights_CalcAmbientLight),
// as luzes pontuais da fase e as dinâmicas (Light_*: as do mapa, as dos objetos 0x4e, o clarão dos
// tiros, as que piscam) numa lista dupla, e a escolha das mais próximas de um objeto
// (Lights_CalcClosestLights), que o desenho manda à VU1 (psiLight_SetLights: no máximo duas).

#include <cstdint>

#include "engine/object.h"
#include "platform/ptr32.h"

struct sphere_tag;

// Uma luz (0x50 bytes; LightList).
struct alignas(16) light_tag {
    LLNODE_tag node;          // 0x00 LightList
    uint32_t unknown_08[2];
    _VECTOR pos;              // 0x10
    int16_t life;             // 0x20 ticks + 1 até sumir (0: não some)
    uint16_t channel;         // 0x22 canal de switch que a acende (0: sempre)
    uint16_t flicker_max;     // 0x24 pisca: ticks acesa no máximo (0: não pisca)
    uint16_t flicker_rand;    // 0x26 metade do sorteio do intervalo apagada
    uint16_t flicker;         // 0x28 contador (bits 0xf000: fase acesa; 0xfff: ticks)
    uint16_t frame_mask;      // 0x2a acesa nos quadros com (frame & máscara) == máscara
    uint16_t sound;           // 0x2c som ao acender (0xffff: nenhum)
    uint16_t unknown_2e;
    P32<obj_tag> obj;         // 0x30 tipo 2: o objeto que a leva (lanterna)
    uint32_t handle;          // 0x34 psiLight_Create
    float dist;               // 0x38 distância / (raio / 2) ao último objeto (a ordem da escolha)
    float radius;             // 0x3c
    float intensity;          // 0x40
    uint8_t r, g, b;          // 0x44
    uint8_t kind;             // 0x47 0 só objetos com a flag 0x80; 1 não ilumina os com 0x100;
                              //      2 lanterna (objetos 0x80 a até 4 m dela no plano)
    uint8_t on;               // 0x48
    uint8_t pad[7];
};
static_assert(sizeof(light_tag) == 0x50 && offsetof(light_tag, radius) == 0x3c && offsetof(light_tag, on) == 0x48);

extern DLISTINFO_tag LightList;

// Aproxima a cor do objeto (obj_tag::color) da luz da sala, 10% por tick; com a visão noturna do
// jogador ligada, objetos com a flag 0x10000 recebem o dobro da luz.
void Lights_CalcAmbientLight(obj_tag* obj);
// Um passo de "from" em direção a "to": fração "step" da diferença, no mínimo 1.
float ClrStep(float from, float to, float step);

void Light_Init();
light_tag* Light_Create(_VECTOR* pos, uint8_t r, uint8_t g, uint8_t b, float radius, uint16_t channel, int16_t ticks,
                        uint8_t kind, float intensity, uint16_t flicker_max, uint16_t flicker_rand,
                        uint16_t frame_mask, uint32_t sound);
uint32_t Light_Delete(light_tag* light);
light_tag* Light_FindMapLight(_VECTOR* pos, float radius);
void Light_SetAmbientRadiators(uint8_t* data);
void Light_Update();
P32<light_tag>* Lights_CalcClosestLights(sphere_tag* sphere, uint16_t flags, uint16_t* count);

// No PS2: o identificador da luz na parte da plataforma (sempre 0) e a remoção (nada).
uint32_t psiLight_Create(light_tag* light);
void psiLight_Delete(uint32_t handle);

// Objetos de luz da fase: halos (tipo 1, sem atualização) e lâmpadas que falham (tipo 0x26).
obj_tag* Corona_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
obj_tag* Flicker_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag* unused);
void Flicker_Update(obj_tag* obj);

// Só para testes.
void lights_debug_power_on();
