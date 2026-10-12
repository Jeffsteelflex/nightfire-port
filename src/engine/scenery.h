#pragma once
// Os enfeites das fases: ondulações na água (Ripples_*), folhas que caem (LeafGen_*, Leaf_*),
// árvores animadas (Env_Tree_*), hélices e ventiladores (rotor_init, rotor_update), paredes de um lado só
// (OneSided_*) e as partículas das lâmpadas que falham (Particles_*, vazias no PS2). Ver docs/engine.md, "Enfeites".
//
// - Ondulações (posicionamento 0x21, objeto 0x34, com o modelo do posicionamento, na sala): a cada tick, com chance
//   FRAME_RATE_MUL / parâmetro 1 (sem os dois: 1 em 30), um anel de água (modelo 0x2000129, Gas_Init tipo 1) num ponto
//   ao acaso a até parâmetro 0 metros, crescendo de 5-10 cm ao dobro em 42 ticks e sumindo (cor 0x7f7f7fff para 6).
//   Um tiro ou um passo na água (material 13 ou 14) faz um anel maior (Env_WaterRipple, 63 ticks: em effect.cpp).
// - Bafo (Env_Breath, do jogador e dos personagens): nuvens brancas da boca nas fases com a chuva de partículas (o tipo 0
//   do ambiente) fora das salas cobertas; senão só no modo de visão 2 (o infravermelho).
// - Folhas (posicionamento 0x2b, objeto 0x31 com o modelo, na sala): a cada tick, com chance 1 em FRAME_RATE_INT / 2,
//   uma folha (objeto 0x2e, modelo 0x20002ca) num ponto ao acaso na esfera do gerador, levada na direção dele (1 a
//   3 cm por tick em x e z), caindo em zigue-zague e girando; vive 256 ticks a 60 Hz.
// - Árvores (posicionamento 0xf3, objeto 0x3f): um objeto animado com a pele do parâmetro 0 e o script do parâmetro 1
//   (0x600008c vira 0x600008d com metade da chance) num quadro ao acaso e em laço (na 07000043 sem o laço); anima fora
//   das cenas.
// - Hélices (posicionamento 0x71, objeto 7, na sala): giram a rotação do posicionamento por tick (x FRAME_RATE_MUL)
//   enquanto o canal do parâmetro 1 estiver desligado (ou sem canal).
// - Paredes de um lado (posicionamento 0xd5, objeto 0x4c, na sala): escondidas (flag 0x10) quando a câmera 0 está
//   atrás delas (a linha 2 da matriz).
// - Partículas (Particles_Create, das lâmpadas que falham, Flicker_Update): no PS2 psiCreateParticles,
//   psiParticleUpdate e psiDrawParticles são vazias; o objeto (tipo 0xc, mode 0x10) não mostra nada.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

// Dados da ondulação (Ripples_Create, 4 bytes, tipo 0x34).
struct RIPPLES_tag {
    int16_t radius;  // 0x00 parâmetro 0 (1 sem os dois)
    int16_t rate;    // 0x02 parâmetro 1: um anel a cada "rate" ticks em média (30 sem os dois)
};
static_assert(sizeof(RIPPLES_tag) == 4);

// Dados da folha (Leaf_Create, 0x40 bytes, tipo 0x2e).
struct alignas(16) LEAF_tag {
    uint8_t unknown_00[0x10];
    _VECTOR vel;     // 0x10 por tick
    _VECTOR spin;    // 0x20 giro por tick (Rand_FRandHalf_Vec 0,1 x FRAME_RATE_MUL)
    int16_t life;    // 0x30 ticks (256 / FRAME_RATE_MUL)
    uint8_t fade;    // 0x32 alfa a menos por tick (nunca escrito: 0)
    uint8_t pad[0x0d];
};
static_assert(sizeof(LEAF_tag) == 0x40 && offsetof(LEAF_tag, life) == 0x30);

// Dados da hélice (rotor_init, 0x20 bytes, tipo 7).
struct alignas(16) ROTOR_tag {
    _VECTOR spin;      // 0x00 a rotação do posicionamento: o giro por tick
    uint8_t channel;   // 0x10 parâmetro 1 (0: sempre gira)
    uint8_t value;     // 0x11 gira com o canal neste valor (nunca escrito: 0)
    uint8_t pad[0x0e];
};
static_assert(sizeof(ROTOR_tag) == 0x20);

obj_tag* Ripples_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Ripples_Update(obj_tag* obj);
void Env_Breath(_VECTOR* mouth, _VECTOR* dir, cel_tag* cel, uint32_t created);
obj_tag* LeafGen_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void LeafGen_Update(obj_tag* obj);
void Leaf_Create(obj_tag* gen, uint16_t count);
void Leaf_Update(obj_tag* obj);
obj_tag* Env_Tree_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void Env_Tree_Update(obj_tag* obj);
obj_tag* rotor_init(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, uint16_t unused, uint8_t unused2, uint8_t channel);
void rotor_update(obj_tag* obj);
obj_tag* OneSided_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist);
void OneSided_Update(obj_tag* obj);
obj_tag* Particles_Create(int32_t kind, _VECTOR* pos, plane_equ_tag* plane);
void Particles_Update(obj_tag* obj);

// Só no port: contagens para o relatório das capturas e o nf_loader_check.
struct SceneryStats {
    uint32_t ripples = 0;        // Ripples_Create
    uint32_t rings = 0;          // anéis do Ripples_Update
    uint32_t leaf_gens = 0;      // LeafGen_Create
    uint32_t leaves = 0;         // folhas criadas
    uint32_t trees = 0;          // Env_Tree_Create
    uint32_t rotors = 0;         // rotor_init
    uint32_t one_sided = 0;      // OneSided_Create
    uint32_t particles = 0;      // Particles_Create
    uint32_t breaths = 0;        // nuvens de Env_Breath
};
extern SceneryStats g_scenery_stats;
void Scenery_Describe(char* out, size_t size);
void scenery_debug_power_on();
