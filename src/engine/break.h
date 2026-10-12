#pragma once
// Destruição de objetos (módulo Break do ACTION.ELF, 0x0012b270-0x0012c5a0, e o Hurt em
// 0x0017e5d0-0x0017ea80): quebráveis, estilhaços, objetos destrutíveis e zonas que machucam.
//
// Quebrável (tipo 0x20, Break_Create em objects.cpp): perde resistência com os tiros que o atingem
// (Break_Update, pela lista de acertos do tick) e com as explosões (Break_ApplyDamage); abaixo de
// zero, Break_Kill o remove. O de tipo 2 (vidro) se estilhaça: cada triângulo do modelo
// (psiGetTriList, montado na carga por psiCreateBreakableData) é cortado ao meio pela aresta mais
// longa até as arestas ficarem curtas (mais curtas perto do ponto atingido) e cada pedaço vira um
// estilhaço (SHARD_tag, na ShardList) que espera um tempo proporcional à distância do tiro, voa na
// direção do tiro com gravidade e giro e some depois de 25 ticks. Os outros soltam detritos do
// material de um terço dos triângulos de colisão (Debris_Create_Loop).
//
// Destrutível (tipo 0x21, Destroy_Create): perde resistência com os tiros; ao chegar abaixo de zero
// (ou quando o canal de entrada liga) troca para o modelo quebrado, apaga a luz da fase que o
// iluminava e as marcas de bala, liga o canal de saída (na hora ou depois de um atraso) e pode
// explodir (Destroy_Smash).
//
// Zona que machuca (tipo 0x27, Hurt_Create): percorre o próprio caminho quando o canal liga ou
// quando é tocada (pelo jogador, por um drone ou por uma bala, conforme a máscara); quem encosta
// nela recebe o dano (Hurt_GetDamage, em Player_DealWithObjHit e NDrone2_DealWithObjHit).
//
// Divergências do port (também comentadas em cada função):
// - ponteiros nulos que o original leria (o acerto de Break_Kill, o modelo sem colisão) são testados
//   e contados em g_break_guards;
// - Break_DoBreak monta o acerto na pilha só com a posição, a direção e o plano: o resto (material,
//   objeto) fica zerado em vez do lixo da pilha;
// - ShatterPolyRecurse parte o vértice novo de uma cópia do vértice 0 quando não há rotação (o
//   original deixa lixo da pilha no cabeçalho e no w, que ninguém lê);
// - o w da velocidade dos estilhaços não recebe o lixo de pilha que UpdateAllShards soma;
// - divisões por zero dão +-FLT_MAX como no FPU do EE (no PC dariam infinito).

#include <cstdint>

#include "engine/collide.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

// Dados do quebrável (obj_tag::data, 0xc bytes; Break_Create).
struct BREAK_tag {
    uint32_t kind;      // 0x00 parâmetro 0: 2 = vidro (estilhaça), outros soltam detritos
    float strength;     // 0x04 parâmetro 1: resistência (abaixo de zero, Break_Kill)
    uint16_t mask;      // 0x08 ~parâmetro 2: classes de tiro ignoradas (weapon_definition_tag::stats)
    uint16_t pad;
};
static_assert(sizeof(BREAK_tag) == 0xc);

// Dados do destrutível (obj_tag::data, 0x18 bytes; Destroy_Create).
struct DESTROY_tag {
    P32<celglist_tag> smashed;  // 0x00 modelo depois de quebrado (celglist_tag::lod ou a entidade seguinte)
    float strength;         // 0x04 parâmetro 3
    float blast;            // 0x08 parâmetro 5 / 100: raio do dano da explosão
    int32_t delay;          // 0x0c parâmetro 6 x -FRAME_RATE: ticks até ligar o canal de saída (negativo
                            //      até quebrar; Destroy_Smash troca o sinal e Destroy_Update conta)
    uint16_t channel_in;    // 0x10 parâmetro 1: quebra quando este canal liga
    uint16_t channel_out;   // 0x12 parâmetro 0: canal ligado quando quebra
    uint16_t mask;          // 0x14 ~parâmetro 4: classes de tiro ignoradas (0x20: explosões também)
    int8_t explosion;       // 0x16 parâmetro 2: 0 nenhuma, 1 script 0x6000027, 2 0x600004f, outros 0x6000052
    uint8_t smashed_flag;   // 0x17 já quebrou
};
static_assert(sizeof(DESTROY_tag) == 0x18);

// Dados da zona que machuca (obj_tag::data, 0x1c bytes; Hurt_Create).
struct HURT_tag {
    float t;                // 0x00 posição no caminho, em quadros
    float speed;            // 0x04 quadros por tick: (quadros / FRAME_RATE) x FRAME_RATE / duração
    P32<framelist_tag> path;  // 0x08
    uint16_t frames;        // 0x0c quadros do caminho - 1
    uint16_t pad_0e;
    float damage;           // 0x10 parâmetro 1 (Hurt_GetDamage)
    uint8_t type;           // 0x14 parâmetro 0 (Hurt_GetType)
    uint8_t once;           // 0x15 parâmetro 5: 1 = some no fim do caminho (estado 2)
    uint16_t touch;         // 0x16 parâmetro 4: quem dispara ao tocar (2 bala, 8 jogador, 0x10 drone; 0 sozinha)
    uint16_t channel;       // 0x18 parâmetro 2: só age com o canal ligado
    uint16_t sound;         // 0x1a parâmetro 3 (0xffff nenhum): toca no começo do caminho
};
static_assert(sizeof(HURT_tag) == 0x1c);

// Vértice de um triângulo a estilhaçar (0x40 bytes): o mesmo registro dos vértices de
// psiCreateBreakableData.
struct alignas(16) SHATTER_VERT {
    uint32_t texture;       // 0x00 só no vértice 0 da lista: a textura (endereço do pacote de Tex)
    uint32_t unknown_04[3];
    _VECTOR pos;            // 0x10 espaço do objeto (sem a rotação dele)
    float uv[2];            // 0x20
    float rgba[4];          // 0x28 cor na escala do GS (0..255, 0x80 = 1.0)
    uint32_t unknown_38[2];
};
static_assert(sizeof(SHATTER_VERT) == 0x40);

struct _SHATTER_VERT_LIST {
    SHATTER_VERT vert[3];
};
static_assert(sizeof(_SHATTER_VERT_LIST) == 0xc0);

// Estilhaço (0xa0 bytes, nó da ShardList).
struct alignas(16) SHARD_tag {
    LLNODE_tag node;        // 0x00
    P32<int32_t> chain;     // 0x08 cadeia de DMA do triângulo (psiCreateShard)
    uint32_t texture;       // 0x0c
    float life;             // 0x10 segundos: 25 ticks; desenhado enquanto > 0
    float delay;            // 0x14 segundos parado antes de voar: distância ao tiro x 15 ticks
    uint32_t unknown_18[2];
    _VECTOR pos;            // 0x20 centro do triângulo no mundo
    _VECTOR rot;            // 0x30 ângulos de Euler (View_RotTransMatrix)
    _VECTOR vel;            // 0x40 por tick
    _VECTOR spin;           // 0x50 por tick
    uint8_t unknown_60[0x40];
};
static_assert(sizeof(SHARD_tag) == 0xa0);

extern LLISTINFO_tag ShardList;  // 0x00316dd8

// Situações em que o original leria um ponteiro nulo; o port pula e conta.
struct BreakGuards {
    uint32_t null_hit = 0;  // Break_Kill sem acerto (o original leria o endereço 0x40)
};
extern BreakGuards g_break_guards;

// Contagens só do port (nf_loader_check, relatório das capturas).
struct BreakStats {
    uint32_t broken = 0;     // Break_Kill
    uint32_t shards = 0;     // estilhaços criados (ShatterPolyRecurse)
    uint32_t smashed = 0;    // Destroy_Smash
};
extern BreakStats g_break_stats;

void Break_Update(obj_tag* obj);
void Break_ApplyDamage(obj_tag* obj, HITDATA_tag* hit);
void Break_DoBreak(_VECTOR* pos);
void Break_Kill(obj_tag* obj, HITDATA_tag* hit);
void ShatterPolyRecurse(int32_t depth, _SHATTER_VERT_LIST* list, obj_tag* obj, HITDATA_tag* hit, uint32_t texture);
void UpdateAllShards();
void DrawAllShards();

obj_tag* Destroy_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag* next_glist);
void Destroy_Update(obj_tag* obj);
void Destroy_CollisionHandler(obj_tag* obj);
void Destroy_Smash(obj_tag* obj, obj_tag* from, uint16_t flags);

obj_tag* Hurt_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Hurt_Update(obj_tag* obj);

// Detritos do material de um terço dos triângulos de colisão (módulo Debris).
void Debris_Create_Loop(colldata_tag* coll, HITDATA_tag* hit, obj_tag* obj);
// Fala dos inimigos de 07000001 quando o jogador quebra algo (módulo Drone).
void Drone_BrokenObject(obj_tag* obj, HITDATA_tag* hit);

// Só para testes: estado inicial das globais do módulo.
void break_debug_power_on();
