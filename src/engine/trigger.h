#pragma once
// Gatilhos (módulo Trigger): objetos do tipo 0x1d que ligam canais de switch (switch_channels) quando
// tocados (pelo jogador, personagens ou balas: a lista de acertos do objeto), usados (E) ou quando
// outros canais ligam (E, OU, em sequência, cópia), e os que trocam de fase ou tocam um filme.
// Os canais são como a fase conversa consigo mesma: personagens esperam um canal para aparecer
// (NDrone2_DSTATE_WaitSwitch), portas e scripts também.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/player_stats.h"

// Dados de um gatilho (0x40 bytes em obj->data).
struct alignas(16) TRIGGER_Tag {
    framelist_tag* path;      // 0x00 caminho por onde o gatilho anda (nulo: parado)
    float pos;                // 0x04 posição no caminho (quadros)
    float speed;              // 0x08 quadros por tick a 60 Hz
    uint16_t frames;          // 0x0c quadros do caminho - 1 (0: parado)
    uint16_t unknown_0e;
    _VECTOR up;               // 0x10 eixo y da posição (Mat_GetUp)
    uint32_t channel;         // 0x20 canal de saída
    uint16_t lock;            // 0x24 com este canal ligado o gatilho para (parâmetro 10)
    uint16_t inputs[8];       // 0x26 canais de entrada (o último sempre 0); no 10: trava, condição,
                              //      canais a ligar
    uint16_t touch;           // 0x36 quem dispara: 1 exige o canal inputs[4], 2 balas, 4 usar (E),
                              //      8 o jogador, 0x10 personagens (2 por padrão)
    uint16_t mode;            // 0x38 1 toque uma vez, 2 toque, 3 E, 4 E em sequência, 5 cópia,
                              //      10 troca de fase, 0xc OU, 0xd filme
    uint16_t hide_on_lock;    // 0x3a (parâmetro 11)
    int16_t cooldown;         // 0x3c ticks até poder mostrar a mensagem de novo
    uint16_t unknown_3e;
};
static_assert(sizeof(TRIGGER_Tag) == 0x40 && offsetof(TRIGGER_Tag, channel) == 0x20);
static_assert(offsetof(TRIGGER_Tag, touch) == 0x36 && offsetof(TRIGGER_Tag, cooldown) == 0x3c);

extern uint8_t ProcessTouch;  // ligado enquanto Trigger_Calc atualiza os gatilhos de toque

void Trigger_Calc();
void Trigger_Update(obj_tag* trigger);
uint32_t Trigger_Hit(obj_tag* trigger, TRIGGER_Tag* t);
void Trigger_Activate(obj_tag* trigger, obj_tag* by);
obj_tag* Trigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
// No ELF são void, mas deixam em v0 o objeto de Trigger_Create, que Script_CreateEntity usa; aqui o devolvem.
obj_tag* Trigger_TouchOnce(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
obj_tag* Trigger_Touch(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_MultiplexIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_MultiplexOrIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_MultiplexSIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_MultiplexOut(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_LoadLevelCreate(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Trigger_MoviePlayer(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
uint16_t Trigger_Type(obj_tag* trigger);
uint32_t Trigger_Locked(obj_tag* trigger);
// Inversor (posicionamento 0x41, objeto 0x53): com o canal do parâmetro 0 ligado, desliga a cada tick o do parâmetro 1.
// Só no port: InverterTrigger_Create devolve o objeto (o original devolve 0); os canais passam por & 0xff.
obj_tag* InverterTrigger_Create(level_tag* level);
void InverterTrigger_Update(obj_tag* obj);

// Dicas (posicionamento 0x33, objeto do tipo 0x43; no ELF logo depois dos gatilhos): um volume que, tocado pelo
// jogador com o canal "liga" ligado (0 sempre) e o "apaga" desligado, toca um som, mostra um texto (mensagem de
// informação, 3 s) e some; com o canal "apaga" ligado some calado. Só com as dicas ligadas (DrawInfo[3], 1 desde
// bootup_bootup) e na dificuldade pedida.
struct HINT_tag {
    uint32_t sound;  // 0x00 som (parâmetro 1; 0 nenhum)
    uint32_t label;  // 0x04 rótulo do texto (parâmetro 0)
    uint32_t on;     // 0x08 canal que a liga (parâmetro 3; 0 sempre ligada)
    uint32_t off;    // 0x0c canal que a apaga (parâmetro 4; 0 nenhum)
};
static_assert(sizeof(HINT_tag) == 0x10);
obj_tag* Hint_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Hint_Update(obj_tag* hint);

// Ainda não portado (stub em modules_stubs.cpp).
