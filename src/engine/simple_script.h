#pragma once
// Objetos de roteiro simples (módulo SS do ACTION.ELF, 0x001a9948-0x001a9eec). Ver docs/engine.md, "Objetos de roteiro
// simples".
//
// Um posicionamento 0xe2 (objeto 0x25; SS_Create) é um modelo que anda pelo seu caminho (KeyFrame_Interp, ou
// Spline_Interp com o parâmetro 6): portões, pontes, plataformas, hélices, a água que sobe ou desce. O modo (parâmetro
// 0) diz como: 8 anda sempre em laço, 1 anda já, 0x40 espera um disparo, 0 parado. Disparos (SS_Operate): o canal do
// parâmetro 3 ligado, ou um toque das classes de objeto em "flags" (parâmetro 2: 2 tiro, 8 o jogador, 0x10 um
// personagem); com o bit 4, usar (SS_Activate) também. Sem o bit 1 de "flags", o próprio disparo liga o canal. Ao andar
// para no quadro do parâmetro 5 (esperando outro disparo) e, no fim do caminho, volta ao começo esperando (parâmetro 1
// zero) ou para de vez (parâmetro 1 não zero). Com o parâmetro 7, a altura da água da sala segue a esfera do objeto.
// O som do parâmetro 4 toca ao disparar.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

// Dados do objeto (0x2c bytes em obj->data, tipo 0x25).
struct SIMPLESCRIPT_tag {
    float t;                 // 0x00 quadro no caminho
    float speed;             // 0x04 quadros por tick: quadros / duração (0x4b se o caminho não tem)
    float stop;              // 0x08 parâmetro 5: quadro onde para esperando (múltiplo de speed)
    P32<framelist_tag> path;  // 0x0c
    uint32_t channel;        // 0x10 parâmetro 3: dispara e é ligado pelo disparo (sem o bit 1 de flags)
    uint32_t sound;          // 0x14 parâmetro 4 (0: 0xffff, nenhum)
    uint32_t sound_handle;   // 0x18 o som tocando
    uint16_t frames;         // 0x1c quadros do caminho - 1
    uint16_t flags;          // 0x1e parâmetro 2: 1 o canal só dispara, 2 tiros, 4 usar, 8 jogador, 0x10 personagem
    uint16_t start_mode;     // 0x20 parâmetro 0
    uint16_t mode;           // 0x22 0 parado, 1 andando, 8 laço, 0x40 esperando
    uint16_t once;           // 0x24 parâmetro 1: no fim do caminho para de vez
    uint16_t spline;         // 0x26 parâmetro 6: Spline_Interp em vez de KeyFrame_Interp
    uint16_t moved;          // 0x28 já andou (o objeto passa a refazer a matriz: mode 0x20)
    uint16_t water;          // 0x2a parâmetro 7: a água da sala acompanha o objeto
};
static_assert(sizeof(SIMPLESCRIPT_tag) == 0x2c);

obj_tag* SS_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void SS_Update(obj_tag* obj);
uint32_t SS_Operate(obj_tag* obj, SIMPLESCRIPT_tag* ss);
void SS_Activate(obj_tag* obj, obj_tag* by);

// Só no port: o que os objetos de roteiro simples fizeram desde a partida.
struct SimpleScriptStats {
    uint32_t created = 0;
    uint32_t triggered = 0;  // passagens de 0x40 (esperando) a 1 (andando)
    uint32_t finished = 0;   // chegadas ao fim do caminho
};
extern SimpleScriptStats g_simple_script_stats;
