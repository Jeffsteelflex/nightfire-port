#pragma once
// Explosões (módulo Explode do ACTION.ELF, 0x0017c350-0x0017ce24).
//
// Explode_Create cria um objeto do tipo 0x0f no ponto da explosão: sacode as câmeras perto (com raio
// de dano a partir de 0,5; abaixo disso a explosão não fere), carrega e toca o script do efeito
// (Script_Load/Script_Play: o fogo, o clarão, a luz, a fumaça e os sons vêm do script, 0x6000052 nas
// granadas, foguetes e destrutíveis) e guarda o dano, o raio, quem a causou e a sala.
// No primeiro tick de colisão (Explode_CollisionHandler) o dano se espalha uma vez
// (Explode_Propagate): uma esfera do raio pega os objetos em volta, e cada um recebe
// dano x (1 - distância até a esfera dele / raio) conforme o tipo: personagens pela mensagem 9
// (Drone_ExplosiveHit, com um DroneImpact_tag), o jogador por Player_Hurt, quebráveis por
// Break_ApplyDamage, destrutíveis por Destroy_Smash (dano de pelo menos 1), projéteis explosivos
// explodem junto (Bullet_handle_object_destruction), helicópteros, torretas, sensores, monitores e o
// submarino pelas funções deles. Explode_Update roda o script, solta detritos de metal ou pedra no
// primeiro quarto do script (pelo material atingido) e apaga o objeto quando o script acaba.
//
// O fogo, o clarão, a luz e a fumaça vêm do script do efeito (165 quadros no 0x6000052), tocado pelo
// script player (engine/script.h): o raio vai para SCRIPTINFO::scale e a duração é SCRIPTINFO::end.
//
// Divergências do port (também comentadas em cada função): sem jogador, a direção da explosão para o
// jogador mais perto parte de um ponto zerado (o original usa lixo da pilha); Drone_ExplosiveHit com um
// objeto que não é personagem não manda a mensagem (o original a mandaria com um DCVars de lixo);
// Coll_AddHitToList sem acerto livre não é escrito.

#include <cstdint>

#include "engine/collide.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct SCRIPTINFO;
struct DroneImpact_tag;

// Dados da explosão (obj_tag::data, 0x40 bytes).
struct alignas(16) EXPLODE_tag {
    _VECTOR up;            // 0x00 eixo y da matriz do script (detritos)
    _VECTOR side;          // 0x10 soma dos dois eixos perpendiculares à direção do jogador (espalha os detritos)
    P32<cel_tag> cel;      // 0x20 sala do objeto que explodiu
    P32<obj_tag> from;     // 0x24 quem causou (crédito do dano; Control_ObjectReferenced)
    float damage;          // 0x28 dano no centro (0 com raio < 0,5)
    float radius;          // 0x2c raio do dano (param_5 de Explode_Create)
    uint32_t duration;     // 0x30 quadros do script (SCRIPTINFO + 0xab0), 15 sem script
    P32<SCRIPTINFO> script;  // 0x34
    uint16_t surface;      // 0x38 material atingido (5/6 metal, 8/10/11 pedra em 07000008)
    uint8_t propagated;    // 0x3a o dano já se espalhou
    uint8_t pad[5];
};
static_assert(sizeof(EXPLODE_tag) == 0x40);

// Contagens só do port (nf_loader_check, relatório das capturas).
struct ExplodeStats {
    uint32_t created = 0;     // Explode_Create
    uint32_t propagated = 0;  // Explode_Propagate com o raio
    uint32_t hit_objects = 0; // objetos que receberam dano
};
extern ExplodeStats g_explode_stats;

struct ExplodeGuards {
    uint32_t not_a_drone = 0;     // Drone_ExplosiveHit num objeto que não é personagem
    uint32_t hit_heap_empty = 0;  // Coll_AddHitToList sem acerto livre (o original escreveria no 8)
};
extern ExplodeGuards g_explode_guards;

// Devolve o objeto da explosão (só no port; o original devolve o que Script_Play deixou em v0).
obj_tag* Explode_Create(obj_tag* obj, _VECTOR* pos, const _VECTOR* normal, float radius, float radius2,
                        uint32_t script, float damage, uint8_t a, uint8_t b, uint8_t c, obj_tag* from,
                        uint16_t surface);
void Explode_Update(obj_tag* obj);
void Explode_Propagate(_VECTOR* pos, float radius, cel_tag* cel, float damage, obj_tag* from);
void Explode_Delete(obj_tag* obj);
void Explode_CollisionHandler(obj_tag* obj);

// Chamadas pela explosão, de outros módulos.
float control_ClosestPlayerPnt(_VECTOR* pos, _VECTOR* out);
float Player_Hurt(obj_tag* obj, float damage, _VECTOR* from);
void Drone_ExplosiveHit(obj_tag* obj, DroneImpact_tag* impact);
void GT_ApplyDamage(obj_tag* obj, float damage);
void Sub_ApplyDammage(obj_tag* obj, float damage);
void Sensor_ApplyDamage(obj_tag* obj, float damage);
void Monitor_ApplyDamage(obj_tag* obj, float damage);

// Só para testes: estado inicial das globais do módulo.
void explode_debug_power_on();
