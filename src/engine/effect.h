#pragma once
// Efeitos de impacto e fumaça (módulos Effect, Gas, Swoosh e Debris do ACTION.ELF): o que um tiro,
// um passo ou um corpo atingido deixa no cenário conforme o material (EffectInfo): marcas
// (anexos de sala ou de objeto, da lista EffectList), fumaça e poeira (objetos do tipo 0xe,
// Gas_Init/gases_smoke), faíscas (Emitter_Create), detritos (tipo 0x10) e sons; os rastros de
// projéteis (tipo 0x41, Swoosh).

#include <cstdint>

#include "engine/collide.h"
#include "engine/object.h"

// Fumaça de um material (0x1c bytes; três por material em HEINFO_tag::gas).
struct HEGAS_tag {
    uint32_t entity;      // 0x00 modelo (0: nenhuma)
    uint32_t kind;        // 0x04 comportamento em gases_smoke
    uint32_t color0;      // 0x08 cor inicial (r << 24 | g << 16 | b << 8 | a)
    uint32_t color1;      // 0x0c cor final
    float scale0;         // 0x10 escala inicial
    float scale1;         // 0x14 escala final
    uint16_t life;        // 0x18 ticks
    uint8_t on_surface;   // 0x1a afasta 5 cm da superfície; só em superfícies quase verticais
    uint8_t face_out;     // 0x1b gira para fora da superfície
};
static_assert(sizeof(HEGAS_tag) == 0x1c);

// Efeitos de um material de colisão (0x88 bytes; EffectInfo tem 23, índice HITDATA_tag::surface
// & 0x3f).
struct HEINFO_tag {
    uint32_t name;           // 0x00 ponteiro do PS2 para o nome ("CARPET"...); zerado na cópia
    uint32_t decal;          // 0x04 marca de bala (Effect_Bullet)
    uint32_t decal_foot_b;   // 0x08 marca de pé com a flag 4 (Effect_Player)
    uint32_t decal_foot_a;   // 0x0c marca de pé (flags 8 ou nenhuma)
    uint32_t laser_gas;      // 0x10 fumaça do laser (Effect_Laser)
    uint16_t sound_bullet;   // 0x14
    uint16_t sound_step;     // 0x16 passos e laser
    uint16_t sound_body;     // 0x18
    uint16_t ricochet;       // 0x1a máscara de ricochete (Effect_RicochetProb)
    float bounce_keep;       // 0x1c quanto da velocidade sobra num quique
    HEGAS_tag gas[3];        // 0x20
    uint32_t debris;         // 0x74 modelo dos detritos (Debris_Create)
    float debris_speed;      // 0x78
    float debris_spin;       // 0x7c
    uint8_t unknown_80;
    uint8_t debris_life;     // 0x81 ticks (metade fixa, metade sorteada)
    uint8_t debris_on;       // 0x82
    uint8_t debris_max;      // 0x83 até quantos detritos
    uint32_t emitter;        // 0x84 emissor de faíscas (Emitter_Create, com a flag 0x20)
};
static_assert(sizeof(HEINFO_tag) == 0x88);
static_assert(offsetof(HEINFO_tag, gas) == 0x20 && offsetof(HEINFO_tag, debris) == 0x74);

constexpr uint32_t kEffectInfoCount = 23;
constexpr uint32_t kEffectInfoSize = kEffectInfoCount * sizeof(HEINFO_tag);  // 3128, como o símbolo
// Copiada do ACTION.ELF no boot (nf::init_game_tables), como weapon_data.
extern HEINFO_tag EffectInfo[kEffectInfoCount];
// O material de um acerto (o original indexa com surface & 0x3f sem limite; aqui acima de 22 vale o
// 22, contado em g_effect_guards).
const HEINFO_tag& effect_info(uint8_t surface);

extern DLISTINFO_tag EffectList;  // 200 anexos de 0x60 (marcas)

struct EffectGuards {
    uint32_t surface_out_of_table = 0;  // material >= 23 (o original leria além de EffectInfo)
};
extern EffectGuards g_effect_guards;

void Effect_InitLists();
void Effect_Create(HITDATA_tag* hit, obj_tag* obj, uint16_t flags, float volume);
void Effect_Bullet(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags);
void Effect_Player(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags, float volume);
void Effect_Laser(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags);
void Effect_Body(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags);
void Effect_UnLink(ATTACH_tag* e);
void Effect_RemoveSObjs(obj_tag* obj);

obj_tag* Gas_Init(cel_tag* cel, _VECTOR* pos, _VECTOR* rot, _VECTOR* vel, float scale0, float scale1,
                  uint32_t entity, uint32_t kind, uint32_t color0, uint32_t color1, uint16_t life);
void gases_smoke(obj_tag* obj);
void Env_GetWindVel(cel_tag* cel, _VECTOR* pos, _VECTOR* out);

obj_tag* Swoosh_Create(_VECTOR* from, _VECTOR* to, int16_t width, uint32_t entity, uint8_t kind);
void Swoosh_Update(obj_tag* obj);

void Debris_Create(HITDATA_tag* hit, const HEINFO_tag* info, cel_tag* cel, uint16_t count);
void Debris_CreateEx(_VECTOR* pos, _VECTOR* dir, _VECTOR* side, float speed, uint32_t life, uint32_t count,
                     cel_tag* cel, uint32_t* entities, uint32_t n_entities);

// Só para testes: estado inicial das globais do módulo.
void effect_debug_power_on();
