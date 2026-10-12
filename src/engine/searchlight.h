#pragma once
// Holofotes (Searchlight_* do ACTION.ELF, 0x001cb4f8-0x001cc33f, junto do módulo de luzes). Ver docs/engine.md,
// "Câmeras de segurança, monitores e holofotes".
//
// Um holofote são até cinco posicionamentos 0xde no mesmo lugar (objeto 0x22), pelo tipo (parâmetro 0): 0 o suporte
// (gira em y com a lâmpada), 1 a lâmpada (a única que pensa: Searchlight_Create junta nela as peças a até 3 m da mesma
// sala), 2 a mancha de luz no chão (com uma luz do tipo 2, a "lanterna" de Lights_CalcClosestLights: raio 500, branca,
// iluminando os objetos com a flag 0x80 a até 4 m da mancha; o objeto nasce 10 km longe e só aparece onde o raio da
// lâmpada bate), 3 o feixe (do tamanho do modelo, apontado pela lâmpada) e 4 o brilho (o clarão visto de frente).
//
// A lâmpada (parâmetros 1 canal que acende, 2 canal que apaga de vez, 3 canal de alarme, 4 e 5 os limites do arco em
// graus): estados (obj_tag::state) 3 apagada (esperando o canal do parâmetro 1; ao acender toca 0xe5), 0 varrendo (um
// círculo de raio "metade do arco" em volta do giro do posicionamento e do centro da inclinação: seno e cosseno do
// tempo x 0,75 grau por tick; o tempo começa ao acaso), 1 seguindo o jogador (a mancha chegou a até 2 m dele: o canal de
// alarme liga a cada tick; a lâmpada vira 15% por tick para os pés dele, presa ao arco; 15 s com ele a mais de 2 m da
// mancha voltam ao 0), 2 quebrada (um tiro com dano, de um projétil: as peças e a luz somem, LOD seguinte, som 0xe6; a
// lâmpada cai até 0,8 rad de inclinação soltando fumaça). O canal do parâmetro 2 apaga o holofote de vez (estado 3 sem
// canal de acender). O alarme não desliga sozinho: quem espera o canal (geradores, personagens) faz o resto, e um
// inversor (InverterTrigger) pode desligá-lo (07000006).
//
// O brilho só aparece com a lâmpada desenhada (obj_tag::unknown_fc) e de frente para a cabeça do jogador (cosseno acima
// de 0,5): o alfa e a escala crescem com o cosseno e, com linha de visão da câmera 0, ele vai para a lista 2 (flag
// 0x2000, por cima de tudo) com escala x1,5.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

struct light_tag;

// Dados do holofote (0x40 bytes em obj->data, tipo 0x22).
struct alignas(16) SEARCHLIGHT_tag {
    uint16_t kind;         // 0x00 parâmetro 0: 0 suporte, 1 lâmpada, 2 mancha, 3 feixe, 4 brilho
    uint16_t pad_02;
    float timer;           // 0x04 lâmpada: o tempo da varredura (ticks, começa entre 0 e 479); mancha: ticks com o
                           //      jogador a mais de 2 m (estado 1)
    uint32_t unknown_08[2];
    _VECTOR rot;           // 0x10 a rotação do posicionamento (y: o centro do giro)
    obj_tag* parts[5];     // 0x20 lâmpada: as peças pelo tipo; na mancha, parts[2] é a luz (light_tag*)
    uint8_t start;         // 0x34 parâmetro 1: canal que acende (0: já começa varrendo)
    uint8_t off;           // 0x35 parâmetro 2: canal que apaga de vez
    uint8_t alarm;         // 0x36 parâmetro 3: ligado enquanto a lâmpada segue o jogador
    uint8_t pad_37;
    float half;            // 0x38 metade do arco (parâmetros 4 e 5) em radianos
    float pitch;           // 0x3c o centro do arco (inclinação)
};
static_assert(sizeof(SEARCHLIGHT_tag) == 0x40);
static_assert(offsetof(SEARCHLIGHT_tag, parts) == 0x20 && offsetof(SEARCHLIGHT_tag, start) == 0x34);

obj_tag* Searchlight_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Searchlight_Update(obj_tag* obj);

// A luz da mancha (o parts[2] dela).
light_tag* Searchlight_Light(const obj_tag* spot);

// Só no port: o que os holofotes fizeram desde a partida.
struct SearchlightStats {
    uint32_t created = 0;   // Searchlight_Create
    uint32_t lamps = 0;     // lâmpadas (tipo 1)
    uint32_t lights = 0;    // luzes das manchas
    uint32_t spotted = 0;   // lâmpadas que passaram a seguir o jogador (0 -> 1)
    uint32_t alarms = 0;    // ticks com o alarme de uma lâmpada ligado
    uint32_t lost = 0;      // lâmpadas que voltaram a varrer (1 -> 0)
    uint32_t switched = 0;  // lâmpadas acesas pelo canal (3 -> 0)
    uint32_t shot = 0;      // lâmpadas quebradas por tiro
    uint32_t off = 0;       // holofotes apagados de vez pelo canal
    uint32_t guards = 0;    // sem jogador (o original leria o objeto nulo)
};
extern SearchlightStats g_searchlight_stats;

void searchlight_debug_power_on();
