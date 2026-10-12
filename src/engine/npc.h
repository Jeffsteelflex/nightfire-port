#pragma once
// Globais dos drones e da IA (NPCGlobals, 0x00298e10, 0x1d10 bytes). Zeradas por Drone_LevelReset;
// Drone_PreLoad_Init aponta as tabelas de coberturas e pontos de IA (CoverNodes, AIPoints).
// Os campos sem nome ficam como unknown_<deslocamento> até aparecerem no código portado; os
// deslocamentos foram lidos nos acessos do ACTION.ELF e são conferidos por static_assert.

#include <cstddef>
#include <cstdint>

#include "engine/ainetwork.h"
#include "engine/object.h"

struct Drone_tag;

// Oponente de um lado (8 lados, 0x20 bytes cada): Drone_InitComms copia a posição do objeto.
struct alignas(16) NPCOpponent_tag {
    uint32_t flags;           // 0x00 bit 0: oponente ativo; bits 0-1 aceitam coberturas sem dono
    obj_tag* obj;             // 0x04
    uint32_t frame;           // 0x08 GameState.frame da cópia
    uint32_t unknown_0c;
    _VECTOR pos;              // 0x10
};
static_assert(sizeof(NPCOpponent_tag) == 0x20);

// Alerta difundido aos personagens (0x60 bytes, NPCGlobals.alert): NDrone2_DroneAlertToObject e
// NDrone2_DroneAlertToPosition o preenchem e mandam a mensagem a todos com o endereço dele como dado
// (DroneVision_EnemyAlerts o lê).
struct alignas(16) DroneAlert_tag {
    obj_tag* obj;             // 0x00 objeto que causou o alerta (nulo: só a posição)
    obj_tag* sender;          // 0x04 quem deu o alerta
    uint32_t frame;           // 0x08 GameState.frame do alerta
    uint32_t msg;             // 0x0c mensagem difundida (0x11 viu o oponente...)
    _VECTOR pos;              // 0x10
    cel_tag* cel;             // 0x20
    uint32_t unknown_24[3];
    _VECTOR rot;              // 0x30 rotação do objeto
    _VECTOR vel;              // 0x40 old_pos - pos do objeto
    float param[3];           // 0x50 os três floats de NDrone2_DroneAlertToObject/Position
    uint32_t unknown_5c;
};
static_assert(sizeof(DroneAlert_tag) == 0x60);

// Ponto de atenção dinâmico (0x30 bytes, refeito a cada tick por Drone_BuildDynamicAwarePoints):
// projéteis perigosos e nuvens de fumaça perto dos quais a IA não deve passar.
struct alignas(16) DroneAwarePoint_tag {
    uint8_t kind;             // 0x00 0 e 2 fumaça (tipo 0x2a), 1 projétil da arma 0x36, 2 granadas e afins
    uint8_t pad_01[7];
    obj_tag* obj;             // 0x08
    uint32_t unknown_0c;
    _VECTOR pos;              // 0x10
    cel_tag* cel;             // 0x20
    float radius;             // 0x24
    uint32_t unknown_28[2];
};
static_assert(sizeof(DroneAwarePoint_tag) == 0x30);
constexpr int kMaxAwarePoints = 64;

// Registro de um som da IA (NDrone2_PlaySFX, 16 em anel).
struct DroneSfxLog_tag {
    uint32_t sfx;             // índice em Drone_SfxInfo (0 se não tocou)
    uint32_t handle;          // devolvido por Sound_Play3D
};

struct alignas(16) NPCGLOBALS_tag {
    uint8_t enabled;                  // 0x000 Drone_LevelReset põe 1
    uint8_t unknown_001[3];
    LLISTINFO_tag drones;             // 0x004 lista dos drones (nó = Drone_tag: +4 próximo, +0xc objeto)
    Drone_tag* groups[31];            // 0x010 primeiro de cada grupo (Drone_tag::group_next segue)
    obj_tag* spawners[31];            // 0x08c gerador de cada grupo (DroneSpawner_Create)
    // 0x108 personagens de cada grupo; 0xffff depois que o gerador do grupo os tirou do mapa
    // (DroneSpawner_Init: groups[] continua apontando para o primeiro, já apagado)
    uint16_t group_counts[31];
    uint16_t unknown_146;
    uint32_t corpse_time;          // 0x148 FRAME_RATE_INT * 10 tempo de um corpo antes de sumir (estado 0x48)
    uint32_t unknown_14c;             // 0x14c 3
    uint32_t down_count;              // 0x150 personagens nos estados 0xf/0x47/0x48/0xbf
    uint32_t down_or_49_count;        // 0x154 os mesmos mais os no estado 0x49
    uint32_t oldest_down_time;        // 0x158
    uint32_t oldest_down_id;          // 0x15c
    uint32_t prev_counts[7];          // 0x160 contagens do tick anterior (lados 1, 3, 2 e unknown_188)
    uint32_t side_counts[3];          // 0x17c ativos por lado no tick (1, 2, 3; NDrone2_ControlSTANDARD)
    // 0x188 contagens do tick, zeradas por Drone_InitComms: [2] personagens vendo o oponente, [3] que o
    // perderam de vista há mais de 2 s (DroneVision_ProcessDroneSight).
    uint32_t unknown_188[4];
    uint8_t new_sight_said;           // 0x198 a fala "achei" (DroneFunc_NewSightTalk) já foi dita
    uint8_t lost_sight_said;          // 0x199 a fala "sumiu" (DroneFunc_LostSightTalk) já foi dita
    uint8_t unknown_19a[2];
    uint32_t unknown_19c;             // 0x19c reféns salvos (DroneFunc_HostageSaved)
    uint32_t dummy_player_unknown;    // 0x1a0 byte: o jogador falso substitui o de verdade (NDrone2_Player)
    obj_tag* dummy_player;            // 0x1a4
    uint32_t postload_frame;          // 0x1a8
    uint32_t unknown_1ac;             // 0x1ac quadro; a visão só roda 15 quadros depois dele e de postload_frame
    uint32_t impact_anim_frame;       // 0x1b0 GameState.frame da última animação de impacto (de qualquer um)
    uint32_t patrol_talk_time;        // 0x1b4 próxima fala de patrulha (NDrone2_PatrolTalk)
    uint32_t injured_talk_time;       // 0x1b8 (DroneFunc_InjuredTalk, só multijogador)
    uint32_t power_talk_channel;      // 0x1bc o canal do último aviso dos alto-falantes (NDrone2_PowerstationTalk)
    AITarget_tag player_target;       // 0x1c0 alvo das rotas até o jogador (frame em 0x214)
    AITarget_tag alert_target;        // 0x220 (NDrone2_SetDroneAlertRoute; frame em 0x274)
    uint16_t sm_next_id;              // 0x280
    uint16_t vision_checks;           // 0x282 testes de linha de visão da IA (limita os testes por tick)
    uint32_t sight_cursor;            // 0x284 id da máquina do último testado (DroneVision_ProcessDroneSight)
    uint32_t alert_cursor[2];         // 0x288 par (ids) em que DroneVision_FindAlertedDrones parou
    // 0x290 o alerta difundido (frame, cel e param[2] zerados no PreLoad): NDrone2_DroneAlertToObject/
    // Position e o aviso de ferido (DroneFunc_SendHurtMessage, mensagem 0x12, sem objeto e com o ferido
    // como remetente). Vai como dado da mensagem e vira alert_target (NDrone2_SetupAlertTarget).
    DroneAlert_tag alert;
    uint32_t cover_next;              // 0x2f0 próxima cobertura a avaliar (Drone_ProcessCoverNodes)
    uint32_t cover_count;             // 0x2f4
    CoverNode_tag* cover_nodes;       // 0x2f8 = CoverNodes
    uint32_t unknown_2fc;
    NPCOpponent_tag opponents[8];     // 0x300
    uint8_t unknown_400[0x1000 - 0x400];
    uint32_t aware_count;             // 0x1000
    uint32_t unknown_1004[3];
    DroneAwarePoint_tag aware_points[kMaxAwarePoints];  // 0x1010
    AIEmitter_tag emitter;            // 0x1c10 emissor geral (Drone_PostLoad_Init)
    uint32_t ai_point_count;          // 0x1c60
    AIPoint_tag* ai_points;           // 0x1c64 = AIPoints
    uint32_t unknown_1c68;            // 0x1c68
    uint32_t unknown_1c6c;
    float unknown_1c70;               // 0x1c70 0,5
    uint8_t unknown_1c74;             // 0x1c74 não zero: ninguém vê o oponente (DroneVision_ProcessDroneSight)
    uint8_t alarm_said;               // 0x1c75 aviso do alarme (canal 0x96) já dado em 07000014
    uint8_t interrogating;            // 0x1c76 há um interrogatório (DroneVision_BeginInterogate)
    uint8_t unknown_1c77;
    uint32_t alarm_time;              // 0x1c78 GameState.frame do último aviso do alarme
    uint8_t unknown_1c7c[0x10];
    DroneSfxLog_tag sfx_log[16];      // 0x1c8c últimos sons da IA (NDrone2_PlaySFX)
    uint32_t sfx_log_next;            // 0x1d0c
};
static_assert(sizeof(NPCGLOBALS_tag) == 0x1d10);
static_assert(offsetof(NPCGLOBALS_tag, drones) == 0x004 && offsetof(NPCGLOBALS_tag, groups) == 0x010);
static_assert(offsetof(NPCGLOBALS_tag, spawners) == 0x08c && offsetof(NPCGLOBALS_tag, group_counts) == 0x108);
static_assert(offsetof(NPCGLOBALS_tag, corpse_time) == 0x148 && offsetof(NPCGLOBALS_tag, side_counts) == 0x17c);
static_assert(offsetof(NPCGLOBALS_tag, down_count) == 0x150 && offsetof(NPCGLOBALS_tag, prev_counts) == 0x160);
static_assert(offsetof(NPCGLOBALS_tag, dummy_player_unknown) == 0x1a0);
static_assert(offsetof(NPCGLOBALS_tag, dummy_player) == 0x1a4 && offsetof(NPCGLOBALS_tag, postload_frame) == 0x1a8);
static_assert(offsetof(NPCGLOBALS_tag, player_target) == 0x1c0 && offsetof(NPCGLOBALS_tag, alert_target) == 0x220);
static_assert(offsetof(NPCGLOBALS_tag, player_target) + offsetof(AITarget_tag, frame) == 0x214);
static_assert(offsetof(NPCGLOBALS_tag, alert_target) + offsetof(AITarget_tag, frame) == 0x274);
static_assert(offsetof(NPCGLOBALS_tag, sm_next_id) == 0x280 && offsetof(NPCGLOBALS_tag, vision_checks) == 0x282);
static_assert(offsetof(NPCGLOBALS_tag, sight_cursor) == 0x284 && offsetof(NPCGLOBALS_tag, alert_cursor) == 0x288);
static_assert(offsetof(NPCGLOBALS_tag, alert) == 0x290 && offsetof(NPCGLOBALS_tag, cover_next) == 0x2f0);
static_assert(offsetof(NPCGLOBALS_tag, alert) + offsetof(DroneAlert_tag, pos) == 0x2a0);
static_assert(offsetof(NPCGLOBALS_tag, new_sight_said) == 0x198 && offsetof(NPCGLOBALS_tag, unknown_1ac) == 0x1ac);
static_assert(offsetof(NPCGLOBALS_tag, aware_count) == 0x1000 && offsetof(NPCGLOBALS_tag, aware_points) == 0x1010);
static_assert(offsetof(NPCGLOBALS_tag, interrogating) == 0x1c76 && offsetof(NPCGLOBALS_tag, sfx_log) == 0x1c8c);
static_assert(offsetof(NPCGLOBALS_tag, sfx_log_next) == 0x1d0c);
static_assert(offsetof(NPCGLOBALS_tag, patrol_talk_time) == 0x1b4);
static_assert(offsetof(NPCGLOBALS_tag, impact_anim_frame) == 0x1b0 &&
              offsetof(NPCGLOBALS_tag, injured_talk_time) == 0x1b8);
static_assert(offsetof(NPCGLOBALS_tag, cover_count) == 0x2f4 && offsetof(NPCGLOBALS_tag, cover_nodes) == 0x2f8);
static_assert(offsetof(NPCGLOBALS_tag, opponents) == 0x300 && offsetof(NPCGLOBALS_tag, emitter) == 0x1c10);
static_assert(offsetof(NPCGLOBALS_tag, ai_point_count) == 0x1c60 && offsetof(NPCGLOBALS_tag, ai_points) == 0x1c64);
static_assert(offsetof(NPCGLOBALS_tag, unknown_1c68) == 0x1c68 && offsetof(NPCGLOBALS_tag, unknown_1c70) == 0x1c70);
static_assert(offsetof(NPCGLOBALS_tag, unknown_1c74) == 0x1c74 && offsetof(NPCGLOBALS_tag, alarm_time) == 0x1c78);

extern NPCGLOBALS_tag NPCGlobals;
