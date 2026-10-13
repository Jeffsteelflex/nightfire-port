#pragma once
// Projéteis (módulo Bullet do ACTION.ELF, 0x0012c5a0-0x0012fec4): balas, foguetes, granadas,
// dardos, minas, o gancho, o laser de armadilha e as cápsulas de munição ejetadas.
//
// Um projétil é um objeto do tipo 5 criado por Bullet_init com os dados BU_tag. A cada tick
// Bullet_update anda speed * FRAME_RATE_MUL ao longo de dir e monta o teste de colisão do tick
// (HITTEST_tag dentro do BU_tag): um raio do ponto de partida até pelo menos 1 m à frente, ou uma
// esfera quando o passo é menor que meio raio. Collide_Update roda o teste, copia cada acerto para a
// lista do objeto atingido (com o dano da arma em HITDATA_tag::unknown_08: é assim que o jogador,
// os drones e o resto "sentem" a bala) e chama Bullet_CollisionHandler, que para a bala (estado 2,
// apagada no tick seguinte), quica, ricocheteia ou gruda (estado 3) conforme as flags da arma.
// Bullet_DoTrails cuida dos rastros, do pavio das granadas e da contagem do satchel; Bullet_homing,
// do teleguiado.
//
// Estados (obj_tag::state): 0 recém-criada (primeiro passo de 0,5 a 1 x speed, ao acaso), 1 voando,
// 2 parou num acerto, 3 grudada (ou laser armado), 4 passou do alcance; 2 e 4 apagam no próximo tick.
//
// Divergências do port (também comentadas em cada função):
// - lixo de pilha que o original soma ao w de alguns vetores (dir, pos) não é reproduzido: o w fica
//   como estava (o w desses vetores não é lido);
// - guardas contadas em g_bullet_guards onde o original leria fora de uma tabela ou dividiria por
//   zero (nunca acontece nos dados reais);
// - weapon_definition_tag::on_hit (+0x108) é chamado como ponteiro nativo de 32 bits.
//
// A definição da arma (weapon_definition_tag, engine/player_weapon.h) dá velocidade, alcance, dano,
// dispersão, modelo e as flags do projétil (flags2 em +0x6c, hit_flags em +0x70). Ids e classes
// especiais tratados aqui:
//   id 0x2a e 0x2c-0x2f com raio de explosão: dano 1 no teste; 0x2b, 0x34-0x37 e 0x3a-0x40: dano 0;
//   ids 0x2c-0x31, 0x62 e 0x6d nunca usam o teste de esfera; id 0x6c: satchel de 15 s;
//   classe 0x33/0x6a/0x6e rastro de foguete, 0x37 mina (7777 = do jogador, sem pavio), 0x3b satchel
//   com contagem (5 s x posição na família), 0x45 laser com som, 0x50/0x51 ignoram o material,
//   0x59/0x5b pavio parado, 0x69 fumaça longa e pavio de 5 ticks, 0x4e/0x4f gastam a carga de um
//   satchel atingido; ammo_type 0xe toca o som 0x10e ao sair.

#include <cstddef>
#include <cstdint>

#include "engine/collide.h"
#include "engine/explode.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct BLData;
struct weapon_definition_tag;

// Dados do projétil (obj_tag::data, 0x110 bytes).
struct alignas(16) BU_tag {
    _VECTOR dir;              // 0x00 direção de voo, normalizada (com gravidade, renormalizada a cada tick)
    _VECTOR start;            // 0x10 ponto de partida
    uint32_t unknown_20[4];   // 0x20
    P32<obj_tag> from;        // 0x30 quem atirou (zerado quando ele é apagado: Bullet_init liga o mode 0x400)
    P32<obj_tag> owner;       // 0x34 ignorado pelo teste de colisão (HITTEST_tag::ignore_a)
    P32<obj_tag> trail;       // 0x38 rastro (Swoosh_Create) ou o feixe do laser de armadilha
    P32<obj_tag> occluder;    // 0x3c fumaça que tapa a visão (Occlude_Create)
    P32<obj_tag> target;      // 0x40 onde grudou (0x70 bit 0x100) ou o alvo do teleguiado
    P32<weapon_definition_tag> weapon;  // 0x44 definição da arma (uma entrada de weapon_data)
    uint32_t unknown_48[2];
    HITTEST_tag hit_test;     // 0x50 teste de colisão do tick (obj_tag::hit_test aponta para cá)
    uint32_t sound;           // 0xf0 som em andamento (Sound_Play3D), 0 nenhum
    float dist;               // 0xf4 distância percorrida
    float speed;              // 0xf8 metros por tick a 60 Hz
    float timer;              // 0xfc em ticks: pavio, vida do laser, contagem do satchel (210 padrão)
    uint16_t bounces;         // 0x100 quiques e ricochetes (satchel: último quarto de segundo mostrado)
    int16_t charge;           // 0x102 satchel (classe 0x3b): 100, gasta pelas classes 0x4e/0x4f
    uint8_t resting;          // 0x104 empurrado para cima por um acerto: sem gravidade no tick
    uint8_t pad_105[11];
};
static_assert(sizeof(BU_tag) == 0x110);
static_assert(offsetof(BU_tag, from) == 0x30 && offsetof(BU_tag, weapon) == 0x44);
static_assert(offsetof(BU_tag, hit_test) == 0x50 && offsetof(BU_tag, sound) == 0xf0);
static_assert(offsetof(BU_tag, speed) == 0xf8 && offsetof(BU_tag, bounces) == 0x100);
static_assert(offsetof(BU_tag, resting) == 0x104);

inline BU_tag* bullet_of(obj_tag* obj) { return static_cast<BU_tag*>(obj->data); }

// Dados de uma cápsula ejetada (objeto do tipo 0xd, 0x40 bytes; 32 reaproveitadas em ShellCasings).
struct alignas(16) CASING_tag {
    _VECTOR vel;          // 0x00 por tick
    _VECTOR spin;         // 0x10 somado a obj_tag::rot por tick
    float floor_y;        // 0x20 altura do chão: ao passar dela toca o som (depois vira -1e6)
    float timer;          // 0x24 estado 3: ticks até sair da arma; depois, idade em ticks
    P32<obj_tag> shooter;  // 0x28
    uint32_t entity;      // 0x2c modelo (ammo_data + 4 do tipo de munição da arma)
    uint16_t surface;     // 0x30 material do chão sob o atirador (escolhe o som)
    uint8_t pad_32[14];
};
static_assert(sizeof(CASING_tag) == 0x40);

constexpr int kShellCasings = 32;
extern obj_tag* ShellCasings[kShellCasings];  // 0x0026f498
extern int16_t NextCasing;                    // 0x0030ca92

// Situações em que o original leria fora de uma tabela ou pararia numa divisão por zero; o port
// pula e conta (zero nos dados reais).
struct BulletGuards {
    uint32_t no_viewer = 0;             // zunido sem glb_viewer[0] (o original leria o endereço 0xc0)
    uint32_t zero_trail_period = 0;     // rastro com período 0 (break 7 no original)
    uint32_t surface_out_of_table = 0;  // material >= 23 no quique (o original lê além de EffectInfo)
    uint32_t digit_out_of_table = 0;    // contagem do satchel fora de 0..99 s
};
extern BulletGuards g_bullet_guards;

obj_tag* Bullet_init(int16_t player, obj_tag* from, obj_tag* owner, weapon_definition_tag* weapon, _VECTOR* pos,
                     const _VECTOR* dir, uint16_t sound);
void Bullet_update(obj_tag* obj);
void Bullet_CollisionHandler(obj_tag* obj);
void Bullet_Delete(obj_tag* obj);
uint32_t Bullet_handle_object_destruction(obj_tag* obj, obj_tag* from);
void Bullet_DoTrails(obj_tag* obj, BU_tag* b, _VECTOR* old_pos, float step);
void Bullet_trail_effect_smoke(obj_tag* obj);
bool Bullet_heatseekable(obj_tag* obj);
void Bullet_homing(BU_tag* b, obj_tag* obj);
void Bullet_InitCasingLists();
void Bullet_init_casing(obj_tag* obj);
void Bullet_init_casing_ex(obj_tag* obj);
void Bullet_casing_update(obj_tag* obj);
// Atualização dos objetos do tipo 0x10 (os dígitos do satchel e outros detritos).
void Debris_Update(obj_tag* obj);
// Ricochete pelo material (EffectInfo + 0x1a): Rand_Rand(máscara do material) & mask.
int8_t Effect_RicochetProb(uint8_t surface, uint16_t mask);

// Chamadas pelo módulo e ainda não portadas (stubs em modules_stubs.cpp; Sound_Play3D em door.h,
// Sound_Stop em modules.h, Light_Create em anim.h; Effect_Create, Gas_Init e Swoosh_Create em
// effect.h; Explode_Create, portada, em explode.h).
uint32_t Sound_IsFinnished(uint32_t handle);  // portada (engine/sound.h)
void Sound_SetPosition(uint32_t handle, _VECTOR* pos);
void PositionBeam(obj_tag* beam, _VECTOR* from, _VECTOR* to);
void GT_Disable(obj_tag* obj);
obj_tag* Occlude_Create(_VECTOR* pos, int16_t ticks, uint16_t kind);  // portada (bullet.cpp)
void Occlude_Update(obj_tag* obj);                                   // portada (bullet.cpp; control_funcs 0x2a)
obj_tag* Emitter_Create(uint32_t def, _VECTOR* pos, _VECTOR* dir, float a, uint32_t b, uint32_t c, uint32_t d,
                        float e);
void Player_SetFlashBang(BLData* b, float ticks, uint8_t level);
// Portada (drone_func.cpp).
void Drone_MessageObjVicinity(uint32_t msg, obj_tag* obj, float radius, uint32_t data, uint32_t delay);
void Camera_Shake(_VECTOR* pos, float amount);
void Player_SetGrapplePoint(BLData* b, _VECTOR* pos, obj_tag* obj);
// Copter_GetBody e CopterList: engine/copter.h.

// Só para testes: estado inicial das globais do módulo.
void bullet_debug_power_on();
