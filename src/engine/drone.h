#pragma once
// Inimigos e personagens controlados pela IA (módulos Drone/NDrone2/DroneAnim/Drone_SM do ACTION.ELF).
//
// Cada personagem é um objeto do tipo 2 (0x4d quando criado por outro objeto) com um Drone_tag de
// 0xd20 bytes em obj->data. O Drone_tag começa com o nó da lista NPCGlobals.drones; os parâmetros do
// posicionamento (DIVars_tag) ficam guardados no fim (+0xc00) e NDrone2_DefaultInit os aplica depois
// da carga (Drone_PostLoad_Init). O comportamento vem de três fontes:
//  - o "modo" (parâmetro 5 do posicionamento): DroneModeSettings (antigos, < 0x24) ou a estrutura de
//    comportamento dos parâmetros 15 em diante (_BehaviourStruct, NDrone2_DoModeSettingsNEW);
//  - o "tipo" (Drone_tag::type, DTYPE): DroneTypeSettings dá o estado inicial e o de ataque, e às vezes
//    uma função de inicialização e outra de controle por tick;
//  - a máquina de estados (Drone_SM_*): mensagens com destino, estado exigido e atraso, entregues a
//    NDrone2_ProcessStateMachine, que chama a função do estado (NDrone2_StateFuncs, 250 estados).
// As animações passam por DroneAnim_*: cada "DAnim" (Drone_AnimInfo) tem um script por subclasse de
// personagem (Drone_AnimTables) e um "estado de animação" (DroneAnimStates) que diz que DAnim tocar
// para cada chamada (DroneAnim_GetDAnimForAnimState).
//
// As tabelas estáticas (DroneTypeSettings, DroneModeSettings, Drone_AnimInfo/Tables, DroneAnimStates e
// os vetores DASC_* para os quais ela aponta, NDrone2_StateFuncs, bitDescs/bitMasks...) são copiadas
// do ACTION.ELF do usuário na partida (game/static_init.cpp); os ponteiros de código passam por
// Drone_ResolveCode (funções ainda não portadas viram nulo: ver cada uso).

#include <cstddef>
#include <cstdint>

#include "engine/ainetwork.h"
#include "engine/collide.h"
#include "engine/object.h"
#include "engine/player_stats.h"
#include "engine/ps2types.h"

struct ANIMSCRIPT_tag;
struct DroneAlert_tag;
struct BU_tag;
struct Drone_tag;
struct DCVars_tag;
struct MsgObject;

// ---- máquina de estados ----

// Mensagem (0x1c bytes).
struct MsgObject {
    uint32_t msg;         // 0x00 (1 entrar no estado, 2 sair, 3 tick, 4 tempo, 0xc/0xd temporizadores...)
    uint32_t state;       // 0x04 estado exigido do destino (0 qualquer; 0xc5 bots)
    uint32_t sender;      // 0x08 id da máquina que mandou
    uint32_t dest;        // 0x0c id do destino (0 todos)
    uint32_t sent;        // 0x10 GameState.frame no envio
    uint32_t due;         // 0x14 GameState.frame da entrega
    uint32_t data;        // 0x18
};
static_assert(sizeof(MsgObject) == 0x1c);

using DroneStateHandler = uint32_t (*)(DCVars_tag* dcv, uint32_t state, MsgObject* msg);

// Máquina de estados de um personagem (Drone_tag + 0x108, 0x2c bytes).
struct StateMachineInfo_tag {
    uint32_t id;             // 0x00 (NPCGlobals.sm_next_id)
    uint32_t state;          // 0x04
    uint32_t prev_state;     // 0x08
    uint32_t next_state;     // 0x0c pedido por Drone_SM_SetState
    uint32_t return_state;   // 0x10 estado para onde voltar depois de um impacto (DroneFunc_HandleImpact)
    uint32_t entered;        // 0x14 GameState.frame ao entrar no estado
    uint8_t change;          // 0x18 há troca de estado pendente
    uint8_t pad_19[3];
    uint32_t result;         // 0x1c estado pedido pela última consulta (visão, alerta, ataque forçado)
    // 0x20 distância pela rota de missão até o jogador (estados de aliado guiando:
    // DroneMove_ObjectMissionPathDistance)
    float path_dist;
    uint32_t change_param;   // 0x24 (às vezes um ponteiro: o impacto da morte por explosão)
    DroneStateHandler handler;  // 0x28 NDrone2_ProcessStateMachine
};
static_assert(sizeof(StateMachineInfo_tag) == 0x2c);

// Atalho para as quatro coisas que as funções da IA passam juntas (0x10 bytes, Drone_DCVfromOBJ).
struct DCVars_tag {
    obj_tag* obj;
    Drone_tag* drone;
    cel_tag* cel;
    StateMachineInfo_tag* sm;
};
static_assert(sizeof(DCVars_tag) == 0x10);

// Mensagem guardada para entrega futura (0x20 bytes: nó + MsgObject).
struct DroneSMItem {
    DroneSMItem* next;
    MsgObject msg;
};
static_assert(sizeof(DroneSMItem) == 0x20);

// ---- parâmetros do posicionamento ----

// O que Drone_Create guarda do posicionamento (0xc0 bytes; Drone_tag + 0xc00).
struct alignas(16) DIVars_tag {
    obj_tag* obj;            // 0x00
    uint32_t unknown_04[3];
    _VECTOR pos;             // 0x10
    _VECTOR rot;             // 0x20
    obj_tag* owner;          // 0x30 (objeto 0x4d quando não nulo)
    // 0x34 cópia de level_tag::params: [0] pele (05xxxxxx), [1] script (06xxxxxx; 0x6000000 nenhum),
    // [2] dificuldade mínima, [3] canal a, [4] grupo, [5] modo, [6] canal b, [7] ?, [8] estado
    // de ataque pedido, [9] arma, [10] arma secundária, [11] olhar (alto: tipo, baixo: variante),
    // [12] ?, [13] script de entrada, [14] alcance, [15..] estrutura de comportamento nova.
    uint32_t params[33];
    uint32_t unknown_b8[2];
};
static_assert(sizeof(DIVars_tag) == 0xc0 && offsetof(DIVars_tag, params) == 0x34);

// Estrutura de comportamento "nova" (0x1c bytes; behaviour_util_get).
struct _BehaviourStruct {
    uint32_t type;           // 0x00 < 0x11 (índice de drone_stats)
    uint32_t attack;         // 0x04 < 6
    uint16_t words;          // 0x08 palavras de bits (1 a 3)
    uint16_t bits;           // 0x0a bits usados (1 a 0x5b)
    uint32_t* props;         // 0x0c destino das palavras (Drone_tag::behaviour_a)
    uint32_t friend_type;    // 0x10
    uint16_t words2;         // 0x14
    uint16_t bits2;          // 0x16
    uint32_t* props2;        // 0x18 (Drone_tag::behaviour_b)
};
static_assert(sizeof(_BehaviourStruct) == 0x1c);

// ---- o personagem ----

struct alignas(16) Drone_tag {
    LLNODE_tag node;              // 0x000 NPCGlobals.drones
    Drone_tag* group_next;        // 0x008 próximo do mesmo grupo (NDrone2_AddToGroup)
    obj_tag* obj;                 // 0x00c
    obj_tag* body_glow;           // 0x010 BodyGlow_Create
    uint8_t vulnerable;           // 0x014 a vida diminui com o dano (NDrone2_HitDamage); 0 em algumas peles
    uint8_t unknown_15;           // 0x015 algumas peles: DTYPE 0x12 em vez de 9
    uint8_t forced_attack;        // 0x016 conta como visto atacando (NDrone2_SeenAndAttacking)
    uint8_t no_bunching;          // 0x017 DroneMove_NoBunching
    uint8_t ninja_eyes;           // 0x018
    uint8_t captain;              // 0x019
    uint8_t scripted_move;        // 0x01a a raiz anda em script_pos (DroneAnim_PreTransCallback)
    uint8_t ignore_impacts;       // 0x01b DroneFunc_HandleImpact e NDrone2_PunchImpact não fazem nada
    // 0x01c o último tiro tirou mais de 1/4 da vida máxima e sobrou mais de 1/3 (NDrone2_BulletImpact);
    // com 0, a animação de tipo 6 dura até 15 quadros
    uint8_t heavy_hit;
    uint8_t unknown_1d;           // 0x01d zerado quando a visão perde o oponente (DroneVision_SeekOpponentLOS)
    uint8_t gun_in_wall;          // 0x01e a arma atravessa uma parede (DroneVision_GunThroughWall)
    uint8_t aim_from_events;      // 0x01f mirar segue aim_event (NDrone2_AimTest)
    uint8_t aim_event;            // 0x020 eventos 0xf/0x10/0x12 da animação: mirando
    uint8_t unknown_21;
    // 0x022 mirando no oponente (NDrone2_DoTracking); a visão então testa se a arma atravessa uma parede
    uint8_t aiming;
    uint8_t zero_g;               // 0x023 (classe 0x18 e tipos 0x1b/0x1c): a raiz não anda
    uint8_t float_to_target;      // 0x024 sem gravidade: flutua até o alvo do movimento (NDrone2_Move)
    uint8_t avoiding_player;      // 0x025 desviou do jogador neste tick (NDrone2_Steer)
    uint8_t touching_player;      // 0x026 encostou no jogador: não anda para trás (DroneAnim_PreTransCallback)
    uint8_t root_moved;           // 0x027
    uint8_t unknown_28;
    uint8_t unknown_29;
    // 0x02a o jogador mira neste personagem (Check_Target liga; NDrone2_PreDroneControl desliga 30 ticks depois)
    uint8_t in_sights;
    // 0x02b já reagiu a um explosivo (DroneFunc_ConsiderExplosive): tem de onde fugir (explosive_pos,
    // DroneMove_FindSafetyFromScaryObject)
    uint8_t explosive_seen;
    uint8_t head_tracking;        // 0x02c
    uint8_t unknown_2d;
    uint8_t unknown_2e;
    uint8_t small_capsule;        // 0x02f cápsula de raio 0,3 só no quadril (NDrone2_Collision)
    uint8_t unknown_30;
    uint8_t mode_set;             // 0x031
    uint8_t mode_old;             // 0x032 modo da tabela antiga (DroneModeSettings)
    uint8_t unknown_33[2];
    uint8_t face_done;            // 0x035 o script do rosto terminou
    uint8_t face_playing;         // 0x036
    uint8_t talk_pending;         // 0x037
    uint8_t no_move;              // 0x038 (zerado a cada tick)
    uint8_t unknown_39;
    // 0x03a levou tiro na cabeça (NDrone2_HitDamage): morre sem falar nem alertar os outros
    uint8_t head_shot;
    uint8_t firing;               // 0x03b atirando (DroneWeap_Fire liga; DroneWeap_HandleFiring atira)
    uint8_t burst_over;           // 0x03c a rajada acabou (ou não pôde atirar)
    uint8_t one_burst;            // 0x03d uma rajada só: ao acabar, desliga firing (Drone_SM_RouteMsg)
    uint8_t anim_fire;            // 0x03e a bala sai pela animação de tiro (DroneAnim_PlayFiringAnim)
    uint8_t fire_ready;           // 0x03f pronto para atirar neste tick (DroneWeap_DoFiring)
    uint8_t fired;                // 0x040 atirou (DroneWeap_FireWeapon liga, DroneAnim_PlayFiringAnim zera)
    uint8_t lost_sight;           // 0x041 posto em 1 a cada tick sem ver o oponente (DroneVision_ProcessDroneSight)
    uint8_t seen_logged;          // 0x042 já contou "inimigo viu o jogador" (DroneVision_LogSeenOpponent)
    uint8_t unknown_43;
    uint8_t side;                 // 0x044 1 inimigo, 2 aliado, 3 civil
    uint8_t weapon2;              // 0x045 parâmetro 10
    uint8_t unknown_46[2];
    uint32_t talk_time;           // 0x048
    uint32_t talk_script;         // 0x04c
    float unknown_50;             // 0x050 (0.075 se zero)
    uint8_t unknown_54[0x1c];
    _VECTOR contact_local;        // 0x070 direção do contato com o jogador no espaço do objeto
    _VECTOR contact_rot;          // 0x080 ângulos dessa direção (Vec_DirToRot)
    _VECTOR face_dir;             // 0x090 contato com o jogador - posição: com touching_player, a raiz não anda nela
    float root_y;                 // 0x0a0 altura da raiz da animação menos 0,02 (no mínimo 0,2)
    float prev_root_y;            // 0x0a4
    float root_height;            // 0x0a8 a da animação (ANIMOBJ_tag::root_height)
    float health;                 // 0x0ac
    float max_health;             // 0x0b0
    uint8_t stats[7];             // 0x0b4 drone_stats (0xb4: precisão)
    uint8_t armour;               // 0x0bb proteção: 1 colete, 2 jaqueta, 4 capacete, 8 de combate (HitDamage)
    uint16_t stun_time;           // 0x0bc atordoamento acumulado (mensagem 7, DroneFunc_HandleImpact)
    uint8_t unknown_be[2];
    float accuracy;               // 0x0c0 (stats[0] em float)
    uint8_t base_type;            // 0x0c4 DTYPE sem ataque
    uint8_t type;                 // 0x0c5 DTYPE (índice de DroneTypeSettings)
    uint8_t attack_type;          // 0x0c6 DTYPE ao atacar (0 nenhum)
    uint8_t behaviour_type;       // 0x0c7 _BehaviourStruct::type
    uint8_t behaviour_attack;     // 0x0c8
    uint8_t behaviour_friend;     // 0x0c9
    uint8_t unknown_ca[2];
    uint32_t created;             // 0x0cc GameState.frame em NDrone2_PostLoad_Init
    uint32_t unknown_d0;          // 0x0d0 (15; 60 em 07000014 para a classe 0x13)
    uint32_t unknown_d4;
    uint16_t klass;               // 0x0d8 classe do personagem (pela pele)
    uint16_t subclass;            // 0x0da índice da animação em Drone_AnimTables
    uint16_t saved_subclass;      // 0x0dc
    uint16_t unknown_de;
    uint32_t skin;                // 0x0e0 05xxxxxx
    float view_angle;             // 0x0e4 (pi/2)
    float range_e8;               // 0x0e8 alcances (parâmetro 14)
    float unknown_ec;             // 0x0ec (4)
    float range_f0;               // 0x0f0
    float unknown_f4;             // 0x0f4
    float unknown_f8;             // 0x0f8 (2)
    float range_fc;               // 0x0fc
    float damage_mul;             // 0x100 dano das balas (1; 2 capitão; 2 algumas armas)
    uint32_t msg_time;            // 0x104 manda a mensagem 4 a si mesmo neste quadro
    StateMachineInfo_tag sm;      // 0x108
    uint8_t channel_a;            // 0x134 parâmetro 3
    uint8_t channel_b;            // 0x135 parâmetro 6: ao ligar, ataca (estado 0x13a)
    uint8_t channel_a_state;      // 0x136
    uint8_t channel_b_state;      // 0x137
    uint16_t mode;                // 0x138 parâmetro 5
    uint16_t channel_state;       // 0x13a parâmetro 8 (0x65 nenhum; 0xc some ao ligar)
    uint8_t param12;              // 0x13c
    uint8_t chat_type;            // 0x13d
    uint8_t unknown_13e[2];
    uint32_t group;               // 0x140 (NDrone2_AddToGroup)
    uint32_t group_id;            // 0x144 parâmetro 4
    uint32_t param7;              // 0x148
    float hit_damage_mul;         // 0x14c multiplica o dano dos tiros (1; o roteador volta a 1 ao trocar de estado)
    float last_damage;            // 0x150 dano do último acerto (NDrone2_HitDamage)
    float player_dist[4];         // 0x154 distância a cada jogador (100000 sem jogador)
    uint8_t unknown_164[0x0c];
    obj_tag* opponent;            // 0x170 (0x170 a 0x2b0 zerados na inicialização)
    uint8_t opponent_player;      // 0x174 alvo em NPCGlobals.opponents (DroneFunc_AllocateTargetID; 0xff nenhum)
    uint8_t unknown_175[0x0b];
    _VECTOR opp_pos;              // 0x180
    _VECTOR opp_rot;              // 0x190
    float opp_dist;               // 0x1a0
    uint8_t unknown_1a4[0x0c];
    _VECTOR opp_delta;            // 0x1b0 posição do oponente - a própria
    float unknown_1c0;
    float opp_yaw;                // 0x1c4 direção até o oponente
    float unknown_1c8;
    float unknown_1cc;
    float opp_facing;             // 0x1d0 quanto o oponente olha para cá
    float opp_bearing;            // 0x1d4 quanto este olha para o oponente
    uint16_t hits_taken;          // 0x1d8 tiros e golpes recebidos (NDrone2_DealWithObjHit, Drone_BulletHit)
    uint16_t view_bone;           // 0x1da índice em Drone_View_Bones do osso que a visão testa
    // 0x1dc o alvo (jogador) anda; logo depois de começar a andar (0x1dd) ou de parar (0x1df) a
    // pontaria muda por um tempo (DroneWeap_DoOpponentTargetting); 0x1de/0x1e0 já marcaram a troca.
    uint8_t target_moving;
    uint8_t target_started;       // 0x1dd
    uint8_t target_started_seen;  // 0x1de
    uint8_t target_stopped;       // 0x1df
    uint8_t target_stopped_seen;  // 0x1e0
    uint8_t unknown_1e1[0x0f];
    // 0x1f0 posição do oponente + aim_offset (NDrone2_SetOpponent): para onde a mira aponta
    // (AnimObjectSetAiming)
    _VECTOR target_pos;
    _VECTOR aim_error;            // 0x200 erro de pontaria do tiro (DroneWeap_DoBulletAccuracy; 0 acerta)
    _VECTOR aim_offset;           // 0x210 osso visado - posição do oponente (NDrone2_SetOpponentAimPos)
    uint8_t unknown_220[4];
    uint32_t target_change_time;  // 0x224 GameState.frame em que o alvo começou a andar ou parou
    // 0x228 visão do oponente: 2 linha de visão, 4 vê (passou do tempo de reação), 8 já viu alguma vez (a
    // cabeça segue o oponente), 0x40 tinha linha de visão no tick anterior.
    uint32_t sight_flags;
    float visibility_mul;         // 0x22c fator dos volumes de IA onde está o oponente (Drone_ProcessOpponents)
    float visibility;             // 0x230 quanto vê o oponente (DroneVision_OpponentVisibility; vê de 0,7 em diante)
    uint32_t first_seen;          // 0x234 GameState.frame da primeira vez que viu o oponente
    uint32_t seen_time;           // 0x238
    uint8_t unknown_23c[4];
    _VECTOR seen_pos;             // 0x240
    _VECTOR seen_opp_pos;         // 0x250 onde o oponente estava quando foi visto por último
    _VECTOR seen_opp_rot;         // 0x260
    uint32_t seen_ticks;          // 0x270 ticks seguidos vendo o oponente
    uint32_t lost_ticks;          // 0x274 ticks seguidos sem ver o oponente
    // 0x278 interrogatório (NDrone2_DSTATE_Interogate): distância do oponente no começo, a menor desde
    // então (afastar-se 4 m dela leva ao ataque), o quadro e a posição do oponente no começo.
    float interrogate_dist;
    float interrogate_min;        // 0x27c
    uint32_t interrogate_time;    // 0x280
    uint8_t unknown_284[0x0c];
    _VECTOR interrogate_pos;      // 0x290
    uint8_t unknown_2a0[0x10];
    obj_tag* unknown_2b0;         // 0x2b0
    obj_tag* attacker;            // 0x2b4 quem atirou ou bateu por último (DroneFunc_OnInitDeath)
    obj_tag* near_armed;          // 0x2b8 inimigo armado à vista (NDrone2_NearArmedDrone)
    obj_tag* unknown_2bc;
    Drone_tag* unknown_2c0;       // 0x2c0 próximo personagem que NDrone2_FindOpponent testa (aliados)
    uint32_t unknown_2c4;         // 0x2c4 quadro
    uint32_t unknown_2c8;         // 0x2c8 contador
    uint32_t guard_zone;          // 0x2cc zona do oponente na porta guardada (NDrone2_DoorGuardHandler; 0xff: a do parceiro)
    uint8_t unknown_2d0[0x10];
    _VECTOR guard_planes[3];      // 0x2e0 planos da porta guardada (NDrone2_SetDronePartner)
    float nearest_dist;           // 0x310 distância ao personagem mais perto (até 3 m; NDrone2_Steer)
    Drone_tag* nearest;           // 0x314 esse personagem
    Drone_tag* unknown_318;
    // 0x31c para que lado está o mais perto (DroneMove_NoBunching): 2 à frente, 4 atrás, 8 à esquerda,
    // 0x10 à direita; LinkCreep_Handler põe 0x1e quando o caminho está bloqueado
    uint32_t bunch_flags;
    uint32_t boundary_flags;      // 0x320 2/4 sem andar para frente/trás, 8/0x10 para os lados
    uint32_t explosive_time;      // 0x324 até quando foge do explosivo
    uint8_t unknown_328[8];
    _VECTOR explosive_pos;        // 0x330 de onde fugir (DroneMove_FindSafetyFromScaryObject)
    cel_tag* explosive_cel;       // 0x340
    uint8_t unknown_344[0x0c];
    float explosive_radius;       // 0x350 a que distância fugir
    uint16_t explosive_state;     // 0x354 estado depois de fugir (0: return_state)
    uint8_t unknown_356[2];
    // 0x358 GameState.frame até o qual a cabeça não troca de alvo (NDrone2_HeadTrackObj)
    uint32_t head_track_until;
    uint16_t head_mode;           // 0x35c 2: a cabeça procura alvos até 50 m² (NDrone2_DoHeadTrackingBehaviour)
    uint8_t unknown_35e[2];
    obj_tag* head_target;         // 0x360
    uint8_t unknown_364[0x1c];
    float head_yaw;               // 0x380 (NDrone2_GetHeadPos)
    uint8_t unknown_384[0x0c];
    _VECTOR head_pos;             // 0x390 posição dos olhos (NDrone2_GetHeadPos)
    uint32_t eye2_flags;          // 0x3a0 com 0x30000000, a visão testa também a partir de eye2
    uint8_t unknown_3a4[0x0c];
    _VECTOR eye2;                 // 0x3b0
    cel_tag* eye2_cel;            // 0x3c0
    uint8_t unknown_3c4[2];
    uint16_t move_quad;           // 0x3c6 quadrante do último passo de combate (DroneAnim_GetMoveQuad)
    uint8_t unknown_3c8[8];
    HITTEST_tag hit_test;         // 0x3d0 obj->hit_test (cápsula: type 0x800)
    HITDATA_tag* last_hit;        // 0x470 o acerto deste tick (zerado a cada tick)
    float moved;                  // 0x474 quanto andou no plano no último tick
    float speed;                  // 0x478 |vel|
    uint8_t unknown_47c[4];
    _VECTOR fall;                 // 0x480 velocidade de queda ou de flutuação, m/s (Drone_CollisionHandler)
    _VECTOR vel;                  // 0x490
    float unknown_4a0;            // 0x4a0 (0.1 / FRAME_RATE_DIV)
    uint8_t unknown_4a4[0x0c];
    _VECTOR move_rot;             // 0x4b0 rotação usada por NDrone2_Move
    float anim_param;             // 0x4c0 call_param da animação em curso
    float yaw_offset;             // 0x4c4 (zerado a cada tick)
    uint8_t unknown_4c8[6];
    uint16_t unknown_4ce;         // 0x4ce vez na conversa sequestrador/refém (NDrone2_MonitorTalkToMissionObj)
    uint32_t sound;               // 0x4d0
    // 0x4d4 GameState.frame do último som ou fala (NDrone2_PlaySFX; NDrone2_HitTalkExtra espera 3 s)
    uint32_t sfx_time;
    uint32_t* behaviour;          // 0x4d8 aponta para behaviour_a ou behaviour_b
    uint32_t behaviour_a[3];      // 0x4dc
    uint32_t behaviour_b[3];      // 0x4e8 (ao atacar com estado 100)
    uint32_t control_flags;       // 0x4f4 0x8000000: não gira para o alvo (NDrone2_Move, DroneMove_TurnToObj)
    // 0x4f8: 1 respeita boundary_flags, 0x10, 0x20/0x40/0x80, 0x100 ativo (pensa), 0x200 morto,
    // 0x400 morreu agora, 0x2000 pode ser alertado por outros, 0x8000 animação sem repetir, 0x10000
    // procura o oponente (a visão o testa; ligado pelos estados de guarda), 0x20000 alertado por
    // outro, 0x80000 anima, 0x100000 animou neste tick, 0x400000 não ouve, 0x800000 ignora alertas,
    // 0x2000000/0x4000000 atacando (0x4000000 com 0x8000000 viu, 0x10000000 ouviu, 0x40000000
    // recebeu alarme), 0x80000000 posto no chão.
    uint32_t flags;
    // 0x4fc o que o personagem sabe e passa a quem o vê (DroneVision_ConsiderAlerted: 2 viu o
    // oponente, 1/4/8/0x10...; os bits recebidos de outro ficam << 16); 0x20 interrogando, 0x10 fim.
    uint32_t flags2;
    float alert_base;             // 0x500 nível de alerta mínimo
    float alert_attack;           // 0x504
    float prev_alert;             // 0x508
    float alert;                  // 0x50c (0 a 1)
    float sound_alertness;        // 0x510 o que ouviu no tick (Sound_Alertness)
    float sound_alert_step;       // 0x514 sound_alertness * 0,01, somado ao alerta
    float sound_info;             // 0x518 DYNAMICSOUNDS +0x48 do som ouvido
    uint8_t alert_status;         // 0x51c Drone_AlertStatusSet
    uint8_t prev_alert_status;    // 0x51d
    uint8_t alert_status_changed; // 0x51e
    uint8_t unknown_51f;
    uint32_t alert_status_time;   // 0x520
    uint32_t attack_time;         // 0x524
    Drone_tag* alerted_by;        // 0x528 quem o alertou (DroneVision_ConsiderAlerted) ou o interrogador
    float visibility_here;        // 0x52c fator dos volumes de IA na própria posição
    ANIMSCRIPT_tag* script;       // 0x530 o script da animação atual
    uint8_t unknown_534[4];
    ANIMSCRIPT_tag* blend_script; // 0x538 o anterior, saindo
    ANIMSCRIPT_tag* face_script;  // 0x53c
    uint32_t impact_anim_frame;   // 0x540 GameState.frame da última animação de impacto
    void (*anim_func)(Drone_tag*, uint16_t);     // 0x544 DroneData_AnimFunc
    void (*state_func)(Drone_tag*, uint16_t);    // 0x548 DroneData_DAStateFunc
    float snap_rotate;            // 0x54c giro pendente (DroneAnim_SnapRotate)
    uint8_t unknown_550[4];
    uint32_t entry_script;        // 0x554
    // 0x558 a chamada de animação pendente (0x10 bytes; DroneAnim_CallAnim, DroneAnim_ZeroAnimCall).
    uint16_t call_state;          // 0x558 estado de animação pedido
    uint8_t call_new;             // 0x55a
    uint8_t unknown_55b;
    uint16_t call_from_state;     // 0x55c estado de animação na hora do pedido
    uint8_t call_state_b2;        // 0x55e DroneAnimStates[call] +2
    uint8_t call_state_kind;      // 0x55f DroneAnimStates[call] +3
    uint16_t call_variant;        // 0x560
    uint16_t unknown_562;
    uint32_t call_time;           // 0x564 GameState.frame do pedido
    uint8_t anim_kind;            // 0x568 Drone_AnimInfo +0xc
    uint8_t anim_state_kind;      // 0x569 DroneAnimStates +3
    uint16_t anim_state;          // 0x56a Drone_AnimInfo +8
    uint16_t danim;               // 0x56c a DAnim atual (0xffff nenhuma)
    uint16_t anim_frame;          // 0x56e quadro inicial (Drone_AnimTables +2)
    uint32_t anim_flags;          // 0x570 Drone_AnimInfo +0
    uint32_t prev_anim_flags;     // 0x574
    uint8_t anim_call;            // 0x578
    uint8_t anim_playing;         // 0x579 o script sem repetição ainda toca
    uint8_t anim_once;            // 0x57a
    uint8_t anim_call_done;       // 0x57b
    uint32_t anim_script_hash;    // 0x57c 06xxxxxx
    uint32_t anim_start;          // 0x580 GameState.frame
    uint8_t end_param;            // 0x584 Drone_AnimInfo +0xe
    uint8_t anim_info_10;         // 0x585 +0x10
    uint8_t anim_info_f;          // 0x586 +0xf
    uint8_t unknown_587;
    float call_param;             // 0x588 (DroneAnim_CallAnim; copiado para +0x4c0 ao começar)
    float move_speed;             // 0x58c Drone_AnimInfo +4: avanço por tick sem animar
    float anim_speed;             // 0x590 Drone_AnimInfo +0x14
    uint16_t end_danim;           // 0x594 DAnim seguinte (Drone_AnimInfo +0xa)
    uint16_t end_state;           // 0x596 estado da IA ao terminar a chamada
    uint32_t end_msg;             // 0x598 mensagem ao terminar a chamada
    uint32_t idle_anim_time;      // 0x59c próxima troca da animação de parado/andando
    // 0x5a0 estado de volta (ao ser alertado, ao chegar ao ponto de IA, ao fugir:
    // DroneMove_FindSafetyFromScaryPosition)
    uint16_t return_state;
    uint16_t initial_state;       // 0x5a2
    uint16_t attack_state;        // 0x5a4
    uint16_t node_return_state;   // 0x5a6 estado de volta depois da ação de um nó (NDrone2_ReachedDestNode)
    uint8_t unknown_5a8[8];
    _VECTOR return_pos;           // 0x5b0 pés ao ser alertado
    cel_tag* return_cel;          // 0x5c0
    uint8_t unknown_5c4[0x0c];
    _VECTOR push_offset;          // 0x5d0 empurrão dos tiros, somado à posição e reduzido a 95% por tick
    _VECTOR prev_push_offset;     // 0x5e0 o do tick anterior (já somado)
    CelPos_tag goal_from;         // 0x5f0 de onde a rota até o alvo saiu (NDrone2_MoveToGoalPosition)
    uint8_t unknown_610[0x50];
    float move_dist;              // 0x660 distância 2D dos pés até move_target
    float move_arrive;            // 0x664 raio de chegada (0,3)
    uint8_t unknown_668[8];
    // 0x670 próximo ponto a andar (NDrone2_FollowRoute; no começo, a própria posição) e, em 0x690, os
    // ângulos até ele (y: o giro que NDrone2_Move busca; DroneAnim_SnapRotate gira daqui).
    CelPos_tag move_target;
    _VECTOR move_angles;          // 0x690
    uint8_t unknown_6a0[0x50];
    AIPoint_tag goal_point;       // 0x6f0 alvo das rotas (AINetwork_SetupGoalPosition)
    uint8_t unknown_790[0xa0];
    CelPos_tag cover_pos;         // 0x830 onde ficar na cobertura (NDrone2_FindCover)
    uint8_t unknown_850[4];
    AIRoute_tag* active_route;    // 0x854 route ou route2, a que está sendo seguida
    uint8_t unknown_858[8];
    AIRoute_tag route;            // 0x860 a rota do caminho (list: Mem_Malloc de AINetwork.max_nodes)
    uint8_t unknown_960[0x10];
    AIRoute_tag route2;           // 0x970
    AIPoint_tag* mission_point;   // 0xa70 ponto de IA no fim da rota de missão
    uint8_t unknown_a74[0x0c];
    AITarget_tag goal_target;     // 0xa80
    AITarget_tag sound_target;    // 0xae0 o último som ouvido (DroneFunc_HandleSoundAlerts)
    uint8_t unknown_b40[0x60];
    uint16_t unknown_ba0;         // 0xba0
    uint8_t unknown_ba2[2];
    uint32_t bounds_skip;         // 0xba4 limites ignorados ao empurrar (AINetwork_GetBoundsPushVectorForSphere)
    obj_tag* door;                // 0xba8 porta que está abrindo
    uint16_t action_node;         // 0xbac posição na rota do nó com ação (porta, chute...)
    uint16_t wait_node;           // 0xbae nó de espera em que já esperou (0xffff nenhum)
    uint32_t door_time;           // 0xbb0
    CoverNode_tag* cover;         // 0xbb4 a cobertura tomada (NDrone2_FindCover; CoverNode_tag::user)
    uint16_t cover_kind;          // 0xbb8 bits 0-1: como usá-la (NDrone2_CoverNodeOK: 1/2 quina, 3 baixa)
    uint8_t cover_step;           // 0xbba passo da animação de cobertura (DroneAnim_CoverAnim)
    uint8_t cover_mode;           // 0xbbb modo na quina: 0/1 agachado inclinando/saindo, 2/3 de pé, 4 como baixa
    uint16_t clip;                // 0xbbc balas no pente (DroneWeap_FireWeapon gasta uma por tiro)
    uint16_t clip_size;           // 0xbbe
    int16_t burst;                // 0xbc0 tiros que faltam na rajada (DroneWeap_NextBulletTime)
    uint8_t unknown_bc2[2];
    uint32_t next_shot;           // 0xbc4 GameState.frame do próximo tiro
    uint32_t last_shot;           // 0xbc8 GameState.frame do último tiro (mais a pausa entre rajadas)
    // 0xbcc parâmetro 11. Pelo uso nas armas (DroneWeap_ThrowGrenade/CanThrowGrenade), é a granada
    // (índice em weapon_data: 0x34-0x36, 0x3a..., 0x6c) e look_variant quantas restam.
    uint16_t look;
    uint16_t look_variant;        // 0xbce
    float unknown_bd0;            // 0xbd0 (0.5)
    uint32_t enter_script;        // 0xbd4 parâmetro 13
    uint8_t unknown_bd8[8];
    _VECTOR script_pos;           // 0xbe0
    _VECTOR script_rot;           // 0xbf0
    DIVars_tag divars;            // 0xc00
    uint8_t timer1_on;            // 0xcc0
    uint8_t unknown_cc1[3];
    uint32_t timer1_time;         // 0xcc4
    uint32_t timer1_data;         // 0xcc8
    uint8_t timer2_on;            // 0xccc
    uint8_t unknown_ccd[3];
    uint32_t timer2_time;         // 0xcd0
    uint32_t timer2_data;         // 0xcd4
    // 0xcd8 variáveis de cada estado (conversa, espera, saída).
    // 0xcd8 (0xff: não começou; astronauta: quadro do último destino; NDrone2_BondTalk: o passo ou a contagem das
    // falas ao jogador)
    uint32_t state_a;
    // 0xcdc (astronauta: ticks até sortear outro destino; NDrone2_BondTalk: GameState.frame da próxima fala)
    uint32_t state_b;
    uint32_t state_c;             // 0xce0 (DroneFunc_DoBondTalk: falas desde que o jogador passou a mirar)
    uint8_t unknown_ce4[0xc];
    uint32_t talk_step;           // 0xcf0 a conversa com o jogador em 07000008 (0, 1, 2; 99 acabou: HandleTalking)
    uint32_t talk_next;           // 0xcf4 GameState.frame da próxima fala dessa conversa
    float state_radius;           // 0xcf8 (civis: distância de quem está armado)
    float fade;                   // 0xcfc alfa ao sumir (NDrone2_FadeOut)
    uint8_t unknown_d00[0x1c];
    void* bot;                    // 0xd1c dados do bot (multijogador)
};
static_assert(sizeof(Drone_tag) == 0xd20);
static_assert(offsetof(Drone_tag, health) == 0xac && offsetof(Drone_tag, klass) == 0xd8);
static_assert(offsetof(Drone_tag, sm) == 0x108 && offsetof(Drone_tag, channel_a) == 0x134);
static_assert(offsetof(Drone_tag, player_dist) == 0x154 && offsetof(Drone_tag, opponent) == 0x170);
static_assert(offsetof(Drone_tag, opp_pos) == 0x180 && offsetof(Drone_tag, opp_delta) == 0x1b0);
static_assert(offsetof(Drone_tag, seen_time) == 0x238 && offsetof(Drone_tag, unknown_2b0) == 0x2b0);
static_assert(offsetof(Drone_tag, boundary_flags) == 0x320 && offsetof(Drone_tag, head_target) == 0x360);
static_assert(offsetof(Drone_tag, hit_test) == 0x3d0 && offsetof(Drone_tag, last_hit) == 0x470);
static_assert(offsetof(Drone_tag, vel) == 0x490 && offsetof(Drone_tag, move_rot) == 0x4b0);
static_assert(offsetof(Drone_tag, behaviour) == 0x4d8 && offsetof(Drone_tag, flags) == 0x4f8);
static_assert(offsetof(Drone_tag, alert) == 0x50c && offsetof(Drone_tag, script) == 0x530);
static_assert(offsetof(Drone_tag, head_pos) == 0x390 && offsetof(Drone_tag, alert_status) == 0x51c);
static_assert(offsetof(Drone_tag, call_time) == 0x564 && offsetof(Drone_tag, call_param) == 0x588);
static_assert(offsetof(Drone_tag, anim_func) == 0x544 && offsetof(Drone_tag, call_state) == 0x558);
static_assert(offsetof(Drone_tag, anim_kind) == 0x568 && offsetof(Drone_tag, anim_script_hash) == 0x57c);
static_assert(offsetof(Drone_tag, end_danim) == 0x594 && offsetof(Drone_tag, initial_state) == 0x5a2);
static_assert(offsetof(Drone_tag, move_target) == 0x670 && offsetof(Drone_tag, move_angles) == 0x690);
static_assert(offsetof(Drone_tag, goal_from) == 0x5f0 && offsetof(Drone_tag, move_dist) == 0x660);
static_assert(offsetof(Drone_tag, goal_point) == 0x6f0 && offsetof(Drone_tag, goal_target) == 0xa80);
static_assert(offsetof(Drone_tag, sight_flags) == 0x228 && offsetof(Drone_tag, explosive_radius) == 0x350);
static_assert(offsetof(Drone_tag, idle_anim_time) == 0x59c && offsetof(Drone_tag, mission_point) == 0xa70);
static_assert(offsetof(Drone_tag, state_a) == 0xcd8 && offsetof(Drone_tag, fade) == 0xcfc);
static_assert(offsetof(Drone_tag, active_route) == 0x854 && offsetof(Drone_tag, route) == 0x860);
static_assert(offsetof(Drone_tag, route2) == 0x970 && offsetof(Drone_tag, unknown_ba0) == 0xba0);
static_assert(offsetof(Drone_tag, door) == 0xba8 && offsetof(Drone_tag, look) == 0xbcc);
static_assert(offsetof(Drone_tag, script_pos) == 0xbe0 && offsetof(Drone_tag, divars) == 0xc00);
static_assert(offsetof(Drone_tag, timer1_on) == 0xcc0 && offsetof(Drone_tag, bot) == 0xd1c);
static_assert(offsetof(Drone_tag, gun_in_wall) == 0x1e && offsetof(Drone_tag, aiming) == 0x22);
static_assert(offsetof(Drone_tag, lost_sight) == 0x41 && offsetof(Drone_tag, seen_logged) == 0x42);
static_assert(offsetof(Drone_tag, view_bone) == 0x1da && offsetof(Drone_tag, target_pos) == 0x1f0);
static_assert(offsetof(Drone_tag, aim_offset) == 0x210 && offsetof(Drone_tag, sight_flags) == 0x228);
static_assert(offsetof(Drone_tag, visibility_mul) == 0x22c && offsetof(Drone_tag, first_seen) == 0x234);
static_assert(offsetof(Drone_tag, seen_opp_rot) == 0x260 && offsetof(Drone_tag, lost_ticks) == 0x274);
static_assert(offsetof(Drone_tag, near_armed) == 0x2b8 && offsetof(Drone_tag, unknown_2c0) == 0x2c0);
static_assert(offsetof(Drone_tag, eye2_flags) == 0x3a0 && offsetof(Drone_tag, eye2) == 0x3b0);
static_assert(offsetof(Drone_tag, eye2_cel) == 0x3c0 && offsetof(Drone_tag, sfx_time) == 0x4d4);
static_assert(offsetof(Drone_tag, sound_alertness) == 0x510 && offsetof(Drone_tag, sound_info) == 0x518);
static_assert(offsetof(Drone_tag, alerted_by) == 0x528 && offsetof(Drone_tag, visibility_here) == 0x52c);
static_assert(offsetof(Drone_tag, return_state) == 0x5a0 && offsetof(Drone_tag, return_pos) == 0x5b0);
static_assert(offsetof(Drone_tag, return_cel) == 0x5c0 && offsetof(Drone_tag, sound_target) == 0xae0);
static_assert(offsetof(Drone_tag, small_capsule) == 0x2f && offsetof(Drone_tag, head_shot) == 0x3a);
static_assert(offsetof(Drone_tag, contact_local) == 0x70 && offsetof(Drone_tag, armour) == 0xbb);
static_assert(offsetof(Drone_tag, stun_time) == 0xbc && offsetof(Drone_tag, last_damage) == 0x150);
static_assert(offsetof(Drone_tag, hits_taken) == 0x1d8 && offsetof(Drone_tag, nearest_dist) == 0x310);
static_assert(offsetof(Drone_tag, bunch_flags) == 0x31c && offsetof(Drone_tag, head_mode) == 0x35c);
static_assert(offsetof(Drone_tag, fall) == 0x480 && offsetof(Drone_tag, control_flags) == 0x4f4);
static_assert(offsetof(Drone_tag, impact_anim_frame) == 0x540 && offsetof(Drone_tag, push_offset) == 0x5d0);
static_assert(offsetof(Drone_tag, prev_push_offset) == 0x5e0 && offsetof(Drone_tag, bounds_skip) == 0xba4);
static_assert(offsetof(StateMachineInfo_tag, return_state) == 0x10 && offsetof(StateMachineInfo_tag, path_dist) == 0x20);
static_assert(offsetof(Drone_tag, move_quad) == 0x3c6 && offsetof(Drone_tag, cover_pos) == 0x830);
static_assert(offsetof(Drone_tag, cover) == 0xbb4 && offsetof(Drone_tag, cover_kind) == 0xbb8);
static_assert(offsetof(Drone_tag, interrogate_min) == 0x27c && offsetof(Drone_tag, interrogate_pos) == 0x290);
static_assert(offsetof(Drone_tag, cover_mode) == 0xbbb && offsetof(Drone_tag, node_return_state) == 0x5a6);
static_assert(offsetof(Drone_tag, action_node) == 0xbac && offsetof(Drone_tag, wait_node) == 0xbae);
static_assert(offsetof(Drone_tag, guard_zone) == 0x2cc && offsetof(Drone_tag, guard_planes) == 0x2e0);

// ---- tabelas estáticas (copiadas do ELF) ----

// DroneTypeSettings (85 entradas de 0xc bytes).
struct DroneTypeSetting {
    int16_t initial_state;   // < 0: escolhido no código
    int16_t attack_state;
    void (*init)(Drone_tag*);       // +4
    uint32_t (*control)(DCVars_tag*);  // +8 nulo: NDrone2_ControlSTANDARD
};
static_assert(sizeof(DroneTypeSetting) == 0xc);
constexpr int kDroneTypeCount = 85;

// DroneModeSettings (35 entradas de 0xc bytes).
struct DroneModeSetting {
    uint8_t type;            // DTYPE
    uint8_t side;            // Drone_tag::side
    uint8_t pad[2];
    float alert;             // nível de alerta inicial
    void (*init)(Drone_tag*);
};
static_assert(sizeof(DroneModeSetting) == 0xc);
constexpr int kDroneModeCount = 35;

// Drone_AnimInfo (436 entradas de 0x18 bytes): uma "DAnim".
struct DroneAnimInfo {
    uint32_t flags;          // 0x00 1 repete, 4 anda sem animar, 8/0x10/0x20 virada, 0x80/0x100/0x200
    float move_speed;        // 0x04
    uint16_t anim_state;     // 0x08 índice de DroneAnimStates
    uint16_t next;           // 0x0a DAnim ao terminar
    uint8_t kind;            // 0x0c
    uint8_t pad_0d;
    uint8_t end_param;       // 0x0e
    uint8_t unknown_0f;
    uint8_t unknown_10;
    uint8_t pad_11[3];
    float speed;             // 0x14
};
static_assert(sizeof(DroneAnimInfo) == 0x18);
constexpr int kDroneAnimCount = 436;

// Drone_AnimTables: por DAnim, 27 subclasses de {quadro de... (+0: não zero = existe), +2 quadro,
// script baixo}: { uint16_t script; uint16_t frame; }. O script é 0x6000000 + script.
struct DroneAnimTableEntry {
    uint16_t script;
    uint16_t frame;
};
constexpr int kDroneAnimSubclasses = 27;

// DroneAnimStates (119 entradas de 0xc bytes): +0 a DAnim base (DroneAnim_CanDoAnimState), +3 tipo,
// +8 a lista de chamadas (DASC_*): entradas de 6 bytes {chamada (0 qualquer), variantes, DAnim (u16),
// mistura, chamada}, terminada por 0xff.
struct DroneAnimState {
    uint16_t danim;
    uint8_t unknown_02;
    uint8_t kind;
    uint32_t unknown_04;
    const uint8_t* calls;
};
static_assert(sizeof(DroneAnimState) == 0xc);
constexpr int kDroneAnimStateCount = 119;

// DroneData_AnimFunc / DroneData_DAStateFunc: {id, função}, terminadas por id 0xffff.
struct DroneDataFunc {
    uint16_t id;
    uint16_t pad;
    void (*fn)(Drone_tag*, uint16_t);
};
static_assert(sizeof(DroneDataFunc) == 8);

using DroneStateFunc = uint32_t (*)(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject*);
constexpr int kDroneStateCount = 250;

extern DroneTypeSetting DroneTypeSettings[kDroneTypeCount];
extern DroneModeSetting DroneModeSettings[kDroneModeCount];
extern DroneAnimInfo Drone_AnimInfo[kDroneAnimCount];
extern DroneAnimTableEntry Drone_AnimTables[kDroneAnimCount][kDroneAnimSubclasses];
extern DroneAnimState DroneAnimStates[kDroneAnimStateCount];
constexpr uint32_t kDroneAnimCallsSize = 0x2468;  // DASC_AAA_Undefined até DroneAnimStates
extern uint8_t DroneAnimStateCalls[kDroneAnimCallsSize];
extern DroneDataFunc DroneData_AnimFunc[2];
extern DroneDataFunc DroneData_DAStateFunc[2];
extern DroneStateFunc NDrone2_StateFuncs[kDroneStateCount];
extern uint8_t bitDescs[182];     // por propriedade: {palavra | deslocamento << 3, índice da máscara}
extern uint32_t bitMasks[35];
extern float DroneCaptain_Mod_BulletDamage;
extern float DroneCaptain_Mod_BulletAccuracy;
extern float DroneCaptain_Mod_Health;
extern uint32_t drone_stats[17][3][8];  // por tipo de comportamento, três conjuntos de 8

// Quantas vezes cada estado sem porte foi pedido (nf_loader_check imprime com os nomes do ELF).
extern uint32_t g_drone_state_pending[kDroneStateCount];

// Traduz um ponteiro de código das tabelas dos drones (nome com mangling do ELF) para a função
// portada; 0 se ainda não portada.
uint32_t Drone_ResolveCode(const char* symbol_name, uint32_t ps2_address);

// ---- globais ----

extern uint8_t Drone_bDisableSystem;  // resetmap.cpp
extern uint8_t Drone_DummyPlayer;
extern uint8_t Drone_DebugSpr;  // 0x0030ca9c a trapaça C_CHCHDEBUG (nada no executável lê)
// Mensagens com atraso: Drone_SM_LLItems[0] é a cabeça das pendentes (ordenadas pela entrega),
// [1] a das livres; as outras 0x3fe começam livres.
struct DroneSMLists {
    uint32_t count;          // 0x400
    DroneSMItem* items;      // &Drone_SM_LLItems[0]
    DroneSMItem* pending;    // &Drone_SM_LLItems[0]
    DroneSMItem* free;       // &Drone_SM_LLItems[1]
};
extern DroneSMLists Drone_SM_LLDelayed;
extern DroneSMItem Drone_SM_LLItems[0x400];
extern uint32_t g_drone_guards;  // leituras e escritas fora do lugar que o port evita (drone_init.cpp)

// ---- funções ----

// Criação e carga (drone.cpp).
obj_tag* Drone_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level);
obj_tag* NDrone2_CreateObj(DIVars_tag* vars);
void Drone_PreLoad_Init();
void Drone_LevelReset();
void Drone_PostLoad_Init();
void NDrone2_PostLoad_Init(obj_tag* obj);
obj_tag* NDrone2_DefaultInit(DIVars_tag* vars);
uint32_t NDrone2_SetupGroups();
uint32_t NDrone2_AddToGroup(Drone_tag* drone, uint32_t group);
void NDrone2_Enable(int8_t on, Drone_tag* drone);
void Drone_Delete(obj_tag* obj);
uint32_t Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag* dcv);
void Drone_GetOpponentInfo(Drone_tag* drone, obj_tag* obj);
void DroneInit_Collision(Drone_tag* drone);
float DroneInit_TakeXHits(uint32_t weapon, float hits);
void DroneFunc_SetAsAttacking(Drone_tag* drone);

// Controle por tick.
void Drone_InitComms();
void Drone_PauseNoDraw(int8_t on);
void Drone_Control(obj_tag* obj);
uint32_t NDrone2_PreDroneControl(DCVars_tag* dcv);
uint32_t NDrone2_ControlSTANDARD(DCVars_tag* dcv);
uint32_t NDrone2_PostDroneControl(DCVars_tag* dcv);
uint32_t NDrone2_DoAnimation(Drone_tag* drone);

// Modo, tipo e comportamento (drone_init.cpp).
void NDrone2_DoModeSettingsOLD(Drone_tag* drone, uint32_t mode, uint32_t* behaviour, DIVars_tag* vars);
void NDrone2_DoModeSettingsNEW(Drone_tag* drone, uint32_t mode, uint32_t* behaviour, DIVars_tag* vars);
void NDrone2_DoTypeSettingsOLD(Drone_tag* drone, uint8_t type);
void NDrone2_GetDroneTypeAttackTypeFriend(Drone_tag* drone);
uint32_t NDrone2_GetDTYPENEW(Drone_tag* drone, uint32_t behaviour_type, uint32_t attack);
int16_t NDrone2_ChangeToAttackMode(Drone_tag* drone);
uint32_t behaviour_util_get(_BehaviourStruct* out, uint32_t* data);
uint32_t behaviour_util_getProperty(int32_t prop, uint32_t* words);
void behaviour_util_setProperty(int32_t prop, uint32_t* words, uint32_t value);
uint32_t* behaviour_util_getStats(int32_t type, int32_t set);
void NDrone2_init_DMODE_Defaults(Drone_tag* d);
void NDrone2_init_DMODE_Normal(Drone_tag* d);
void NDrone2_init_DMODE_Guard(Drone_tag* d);
void NDrone2_init_DMODE_Retreater(Drone_tag* d);
void NDrone2_init_DMODE_Sniper(Drone_tag* d);
void NDrone2_init_DMODE_Stealth(Drone_tag* d);
void NDrone2_init_DMODE_Attacker(Drone_tag* d);
void NDrone2_init_DMODE_RunToPoint(Drone_tag* d);
void NDrone2_init_DMODE_Assassin(Drone_tag* d);
void NDrone2_init_DMODE_HostageKiller(Drone_tag* d);
void NDrone2_init_DMODE_Hostage(Drone_tag* d);
void NDrone2_init_DMODE_HostageTied(Drone_tag* d);
void NDrone2_init_DMODE_JustStand4Demo(Drone_tag* d);
void NDrone2_init_DMODE_DeleteMe(Drone_tag* d);
void NDrone2_init_DMODE_Civilian(Drone_tag* d);
void NDrone2_init_DMODE_CivilianScared(Drone_tag* d);
void NDrone2_init_DMODE_MissionFailer(Drone_tag* d);
void NDrone2_init_DMODE_Mayhew(Drone_tag* d);
void NDrone2_init_DMODE_Ninja(Drone_tag* d);
void NDrone2_init_DMODE_AlarmRaiser(Drone_tag* d);
void NDrone2_init_DMODE_SearchLight(Drone_tag* d);
void NDrone2_init_DMODE_Ambush(Drone_tag* d);
void NDrone2_init_DMODE_Zoe(Drone_tag* d);
void NDrone2_init_DMODE_PartyGirl(Drone_tag* d);
void NDrone2_init_DMODE_CivilianGuard(Drone_tag* d);
void NDrone2_init_DMODE_Interogator(Drone_tag* d);
void NDrone2_init_DMODE_CivDoorGuard(Drone_tag* d);
void NDrone2_init_DMODE_TruckDriver(Drone_tag* d);
void NDrone2_init_DMODE_CastleChatGuard1(Drone_tag* d);
void NDrone2_init_DMODE_CastleChatGuard2(Drone_tag* d);
void NDrone2_init_DMODE_SniperAlert(Drone_tag* d);
void NDrone2_init_DMODE_PartyGirlLooker(Drone_tag* d);
void NDrone2_init_DMODE_Bot(Drone_tag* d);
void NDrone2_initDTYPE_Sniper(Drone_tag* d);
void NDrone2_initDTYPE_SniperAlert(Drone_tag* d);
void NDrone2_initDTYPE_BotInit(Drone_tag* d);

// Animação (drone_anim.cpp).
void DroneAnim_SetAnimD(uint16_t danim, uint16_t blend, Drone_tag* drone);
uint32_t DroneAnim_SetDAnimInternal(uint16_t danim, uint16_t blend, uint8_t call, Drone_tag* drone);
void DroneAnim_SetHTAnim(uint32_t script, uint32_t flags, uint16_t frame, Drone_tag* drone, uint16_t blend,
                         float speed);
void DroneAnim_ZeroAnimCall(Drone_tag* drone);
uint32_t DroneAnim_SetEndAnim(Drone_tag* drone, uint8_t param);
void DroneAnim_OnStartAnim(Drone_tag* drone);
uint8_t DroneAnim_OnEndAnim(Drone_tag* drone, uint32_t flags, uint8_t param);
void DroneAnim_SnapRotate(Drone_tag* drone);
void DroneAnim_SnapRotate2(Drone_tag* drone);
void DroneAnim_CallHandler(DCVars_tag* dcv);
uint32_t DroneAnim_CanSetAnimCall(Drone_tag* drone);
uint32_t DroneAnim_CheckForAnimComplete(Drone_tag* drone);
int16_t DroneAnim_GetDAnimForAnimState(uint16_t call, uint16_t anim_state, uint16_t variant, uint16_t* out4,
                                       int8_t* out5);
uint32_t DroneAnim_GetDAnimForCall(Drone_tag* drone, uint16_t call, uint16_t variant, uint16_t* danim,
                                   uint16_t* out4, int8_t* out5);
bool DroneAnim_SetAnimCall(DCVars_tag* dcv, uint16_t danim, uint16_t blend, uint8_t call);
void DroneAnim_CallFullyComplete(Drone_tag* drone);
void DroneAnim_SetEndAIState(Drone_tag* drone, int16_t state, uint32_t msg);
void DroneAnim_PreTransCallback(obj_tag* obj, void* anim_state, _VECTOR* delta);
void DroneAnim_EventFunc(obj_tag* obj, void* anim_state, ANIMSCRIPT_tag* script, uint16_t event);
void DroneAnim_AnimEndCallback(obj_tag* obj, void* anim_state, ANIMSCRIPT_tag* script);
DroneDataFunc* DroneData_GetAnimFuncInfo(uint16_t id);
DroneDataFunc* DroneData_GetDAStateFuncInfo(uint16_t id);
void DAnimFunc_Run(Drone_tag* drone, uint16_t phase);
void DAStateFunc_AimStand(Drone_tag* drone, uint16_t phase);
void AnimScriptCallOnEvent(ANIMSCRIPT_tag* s, void (*fn)(obj_tag*, void*, ANIMSCRIPT_tag*, uint16_t));
void AnimScriptCallOnStop(ANIMSCRIPT_tag* s, void (*fn)(obj_tag*, void*, ANIMSCRIPT_tag*));
void AnimScriptSetAnimRotation(ANIMSCRIPT_tag* s, int8_t on);
void AnimObjectSetHeadTracking(obj_tag* obj, void* target, float dist, float speed, int8_t delay, int8_t bone_set,
                               uint8_t bone);

// Máquina de estados (drone_sm.cpp).
uint32_t Drone_SM_Init();
bool Drone_SM_InitObject(obj_tag* obj);
uint32_t Drone_SM_SetState(StateMachineInfo_tag* sm, uint32_t state, uint32_t param);
void Drone_SM_SendMsg(uint32_t msg, uint32_t state, uint32_t sender, uint32_t dest);
void Drone_SM_SendMsgSelf(uint32_t msg, uint32_t data, uint32_t delay, uint32_t state, DCVars_tag* dcv);
void Drone_SM_BroadcastMsg(uint32_t msg, uint32_t data, uint32_t delay, uint32_t sender);
void Drone_SM_SendDelayedMsgs();
void Drone_SM_RouteMsg(MsgObject* msg);
void Drone_SM_RouteMsgDCV(DCVars_tag* dcv, MsgObject* msg);
uint32_t NDrone2_ProcessStateMachine(DCVars_tag* dcv, uint32_t state, MsgObject* msg);

// Auxiliares usados por estados, visão, movimento e dano (drone_func.cpp).
uint32_t DroneAnim_CallAnim(float param, uint32_t call, uint32_t variant, int16_t end_state, uint32_t end_msg,
                            int8_t unused, DCVars_tag* dcv);
bool DroneAnim_CanDoAnimState(DCVars_tag* dcv, int16_t call, int16_t variant);
uint8_t DroneAnim_CurrentMoveState(Drone_tag* drone);
void Drone_AlertStatusSet(uint8_t status, DCVars_tag* dcv);
// O estado de alerta de um personagem (tipo 2): Drone_tag + 0x51c (alert_status, prev_alert_status,
// alert_status_changed); nulo para os outros objetos. Lido pelo rosto (AnimFacialUpdate).
uint8_t* Drone_AlertStatus(obj_tag* obj);
uint32_t Drone_Message(obj_tag* obj, uint32_t msg, uint32_t data, uint32_t delay);
uint32_t NDrone2_ObjectIsArmed(obj_tag* obj);
void DroneFunc_SetMissionFailReason(Drone_tag* drone, uint32_t reason);
uint32_t NDrone2_GetHeadPos(obj_tag* obj, _VECTOR* pos, float* yaw);
uint32_t NDrone2_GetHeadPosOld(obj_tag* obj, _VECTOR* pos, float* yaw);
uint32_t Drone_IsTalking(Drone_tag* drone);
uint32_t DroneFunc_GetBonePos(obj_tag* obj, uint32_t bone, _VECTOR* out);

// Rotas (drone_route.cpp).
void NDrone2_GetAnglesToMoveTarget(DCVars_tag* dcv, _VECTOR* out);
void NDrone2_SetIdleTimeOut(DCVars_tag* dcv, uint32_t base, uint32_t range);
uint32_t NDrone2_ObstructedPatrolPath(DCVars_tag* dcv);
uint32_t NDrone2_InitAIPath(DCVars_tag* dcv, AIPath_tag* path, NODE_tag* start);
uint32_t NDrone2_AssignAIPath(DCVars_tag* dcv, uint32_t mask);
uint32_t NDrone2_FollowRoute(AIRoute_tag* route, DCVars_tag* dcv);
uint32_t NDrone2_UpdatePatrolRoute(DCVars_tag* dcv);
uint32_t NDrone2_UpdateMissionRoute(DCVars_tag* dcv);
int16_t NDrone2_NearestNodeDCV(DCVars_tag* dcv);
AIPoint_tag* NDrone2_ClosestAIPoint(DCVars_tag* dcv, uint16_t kind, _VECTOR* pos, cel_tag* cel);
uint32_t NDrone2_MoveToGoalPosition(DCVars_tag* dcv, CelPos_tag* pos, float radius, uint16_t clear);
uint32_t NDrone2_FindRunToPoint(DCVars_tag* dcv);

// Comportamento (drone_behaviour.cpp).
uint32_t DroneAnim_SetStandIdleAnim(DCVars_tag* dcv, int8_t wait);
uint32_t DroneAnim_SetWalkIdleAnim(DCVars_tag* dcv, int8_t wait);
uint32_t NDrone2_PatrolTalk(DCVars_tag* dcv);
uint32_t NDrone2_NearArmedPerson(DCVars_tag* dcv, float radius);
uint32_t NDrone2_BondWithinAngle(DCVars_tag* dcv, float angle);
uint32_t NDrone2_HeadTrackObj(obj_tag* obj, uint32_t ticks, float speed, DCVars_tag* dcv);
void NDrone2_SetAsDead(DCVars_tag* dcv);
uint32_t NDrone2_FadeOut(DCVars_tag* dcv);
uint32_t DroneFunc_DeadDrone(DCVars_tag* dcv);
void DroneFunc_DropGrenade(DCVars_tag* dcv, uint16_t kind, uint16_t count);
uint32_t DroneFunc_SetDeathChannel(Drone_tag* drone);
void DroneFunc_OnInitDeath(DCVars_tag* dcv);
uint32_t DroneAnim_LocationDeathAnim(DCVars_tag* dcv, int16_t end_state);
uint32_t NDrone2_ExplosionDir(DCVars_tag* dcv, const void* impact);
uint32_t DroneFunc_DoForcedAttack(DCVars_tag* dcv);
void DroneFunc_ConsiderExplosive(DCVars_tag* dcv, obj_tag* obj, int16_t state);
void NDrone2_SeenAndAttacking(DCVars_tag* dcv);
// Falas (drone_behaviour.cpp): o grito de morte por classe, a conversa dos guardas de 07000008 com o jogador, os
// avisos dos alto-falantes de 0700000c/0d (PowerSfx: {canal em 0700000c, canal em 0700000d, fala}, do ELF) e o que os
// convidados da festa de 07000007 dizem ao jogador (NDrone2_BondTalk, pelos estados dos civis e das moças da festa).
uint32_t NDrone2_DeathTalk(DCVars_tag* dcv);
void NDrone2_HandleTalking(DCVars_tag* dcv);
uint32_t NDrone2_PowerstationTalk(DCVars_tag* dcv);
uint32_t NDrone2_BondTalk(DCVars_tag* dcv);
void Drone_MayhewTalk(uint32_t sfx);
uint32_t DroneFunc_DoBondTalk(DCVars_tag* dcv, uint32_t sfx, uint32_t track, uint32_t wait, int8_t repeat);
struct PowerSfxEntry {
    uint16_t channel_c;  // 0x00 o canal que dá o aviso em 0700000c (0 nenhum)
    uint16_t channel_d;  // 0x02 em 0700000d
    uint32_t sfx;        // 0x04 a fala (índice de Drone_SfxInfo); 0 termina a tabela
};
static_assert(sizeof(PowerSfxEntry) == 8);
extern PowerSfxEntry PowerSfx[18];  // 0x0029ab20

// Visão e percepção (drone_vision.cpp).
// Drone_SfxInfo (0x15a entradas de 0x20 bytes, copiadas do ELF): falas e sons da IA, com o último
// quadro em que cada um tocou (o intervalo mínimo vale para todos os personagens).
struct DroneSfxInfo {
    uint32_t sfx;            // 0x00 som (0 nenhum)
    uint32_t script;         // 0x04 script do rosto (06xxxxxx) que toca o som; 0 só o som
    uint32_t last;           // 0x08 GameState.frame da última vez
    uint32_t interval;       // 0x0c intervalo mínimo em quadros a 60 Hz (vezes FRAME_RATE_DIV)
    float volume;            // 0x10
    float range_a;           // 0x14 vezes a escala do pedido (Sound_Play3D)
    float range_b;           // 0x18
    int16_t pitch_min;       // 0x1c com os dois zero, sem variação
    int16_t pitch_max;       // 0x1e
};
static_assert(sizeof(DroneSfxInfo) == 0x20);
constexpr uint32_t kDroneSfxCount = 0x15a;
extern DroneSfxInfo Drone_SfxInfo[kDroneSfxCount];
extern uint32_t Drone_View_Bones[8];  // ossos que a visão testa em rodízio (do ELF; o último é -1)
extern uint32_t switch_BLIND_DRONES;  // chave de depuração: os personagens não veem nem ouvem
extern uint32_t g_drone_vision_guards;  // leituras fora do lugar que o port evita (drone_vision.cpp)

uint32_t DroneWeap_GetWeaponInfo(Drone_tag* drone, int32_t* bone, uint32_t* flash, uint16_t* kind, _VECTOR* pos,
                                 _VECTOR* dir);
void DroneFunc_ClampPosToCollisionCylinder(obj_tag* obj, _VECTOR* pos);
uint32_t DroneVision_CanSeeObjectFrom(Drone_tag* drone, obj_tag* obj, uint32_t bone, _VECTOR* from, cel_tag* cel,
                                      uint32_t mask);
uint32_t NDrone2_CanSeeObject(Drone_tag* drone, obj_tag* obj, uint32_t bone, uint32_t mask);
uint32_t DroneVision_LineOfSightToObject(CelPos_tag* from, CelPos_tag* to, obj_tag* a, obj_tag* b, int8_t unused,
                                         uint32_t mask);
uint32_t DroneVision_GunThroughWall(Drone_tag* drone);
float DroneVision_ObjectVisibility(Drone_tag* drone, obj_tag* obj);
float DroneVision_OpponentVisibility(Drone_tag* drone);
uint32_t DroneVision_SeekOpponentLOS(Drone_tag* drone);
void DroneVision_HaveOpponentSight(Drone_tag* drone);
void DroneVision_LogSeenOpponent(Drone_tag* drone);
uint32_t DroneVision_DroneCanSeeDrone(Drone_tag* drone, Drone_tag* other, int8_t* in_view);
int8_t DroneVision_ConsiderAlerted(Drone_tag* drone, Drone_tag* alerter);
int8_t DroneVision_DetermineAlertCheck(Drone_tag* a, Drone_tag* b);
void DroneVision_FindAlertedDrones();
void DroneVision_ProcessDroneSight();
uint32_t DroneVision_AlertSound(DCVars_tag* dcv);
int32_t DroneVision_ShoutFromOtherDrone(DCVars_tag* dcv, DroneAlert_tag* alert, uint32_t msg);
int32_t DroneVision_EnemyAlerts(DCVars_tag* dcv, MsgObject* msg);
uint32_t DroneVision_EnemyLookForOpponent(DCVars_tag* dcv);
void DroneVision_BeginInterogate(Drone_tag* drone);
void DroneVision_EndInterogate(Drone_tag* drone);
void DroneVision_SetInterogateAssist(Drone_tag* drone);
void DroneVision_EndInterogateAssist(Drone_tag* drone);
uint32_t DroneVision_TestEndInterogationAssist(Drone_tag* drone);
float Drone_VisibilityForPosition(_VECTOR* pos, uint16_t kind);
void Drone_ProcessOpponents();
void Drone_BuildDynamicAwarePoints();
uint32_t NDrone2_FindOpponent(Drone_tag* drone);
void NDrone2_SetOpponent(Drone_tag* drone, obj_tag* opponent);
void NDrone2_SetOpponentAimPos(Drone_tag* drone);
uint32_t NDrone2_DroneInSights(DCVars_tag* dcv);
void NDrone2_SetupAlertTarget();
void NDrone2_DroneAlertToObject(DCVars_tag* dcv, uint32_t msg, obj_tag* obj, float a, float b, float c);
void NDrone2_DroneAlertToPosition(obj_tag* sender, uint32_t msg, _VECTOR* pos, cel_tag* cel, float a, float b,
                                  float c);
uint32_t NDrone2_NearArmedDrone(DCVars_tag* dcv, float radius, int8_t check);
uint32_t NDrone2_NotNearArmedDrone(DCVars_tag* dcv, float radius);
void DroneFunc_HandleSoundAlerts(DCVars_tag* dcv);
void DroneFunc_CheckAlarmRaised();
uint8_t DroneFunc_AllocateTargetID(Drone_tag* drone, obj_tag* obj);
uint32_t DroneFunc_ReactionTime(Drone_tag* drone);
void DroneFunc_TargetSeen(Drone_tag* drone, _VECTOR* pos);
uint32_t DroneFunc_NewSightTalk(DCVars_tag* dcv);
uint32_t DroneFunc_LostSightTalk(DCVars_tag* dcv);
uint32_t NDrone2_PlaySFX(DCVars_tag* dcv, uint32_t sfx, _VECTOR* pos, int8_t check, float scale);
uint32_t NDrone2_SpeechSFX(DCVars_tag* dcv, uint32_t sfx, int8_t force, int8_t face, int8_t need_face);
void NDrone2_SpeechSFXd(Drone_tag* drone, uint32_t sfx, int8_t force, int8_t face, int8_t need_face);
uint16_t AnimScriptPeekSfx(uint32_t script);

// Movimento, colisão e olhar (drone_move.cpp).

// Um lado do teste de obstáculo de NDrone2_Steer (0xd0 bytes, na pilha): o personagem que anda ou o
// obstáculo (outro personagem ou o jogador), cada um apontando para o outro.
struct alignas(16) ObstacleEval_tag {
    uint32_t flags;               // 0x00 2: obstáculo parado (nunca volta a 0 entre obstáculos, como no original)
    _VECTOR* pos;                 // 0x04
    _VECTOR* rot;                 // 0x08
    float speed;                  // 0x0c quanto anda por tick (Drone_tag::moved; 0 para o jogador)
    float radius;                 // 0x10
    uint8_t unknown_14[0x0c];
    _VECTOR vel;                  // 0x20 passo deste tick no plano (x, z)
    _VECTOR contact;              // 0x30 posição no momento do contato
    ObstacleEval_tag* other;      // 0x40
    uint8_t unknown_44[0x44];
    float radius_sum;             // 0x88
    float share;                  // 0x8c parte do desvio que cabe a este: speed / (speed + other->speed)
    float steer;                  // 0x90 giro sugerido (rad; o sinal afasta do obstáculo)
    float weight;                 // 0x94 urgência: 1 no contato, menos quanto mais longe no tempo
    uint8_t unknown_98[8];
    float t;                      // 0xa0 ticks até o contato
    float limit;                  // 0xa4 aproximação por tick
    float dist;                   // 0xa8 distância no momento do contato
    uint8_t unknown_ac[4];
    _VECTOR delta;                // 0xb0 do contato deste ao do obstáculo (y = 0)
    uint8_t unknown_c0[4];
    float angle;                  // 0xc4 direção do obstáculo menos a frente (rad)
    uint8_t unknown_c8[8];
};
static_assert(sizeof(ObstacleEval_tag) == 0xd0 && offsetof(ObstacleEval_tag, other) == 0x40);
static_assert(offsetof(ObstacleEval_tag, t) == 0xa0 && offsetof(ObstacleEval_tag, angle) == 0xc4);

extern _VECTOR DroneReachCheckPos;  // 0x0029b2f0 o último ponto testado por DroneMove_SetBoundryFlags

// Caixas de colisão das juntas de um personagem (módulo de animação; HumanCollisionBones e
// AstronautCollisionBones, 17 entradas de 0x14 bytes terminadas por bone < 0, copiadas do ELF). A
// caixa fica entre bone e other (o pai de bone quando other < 0), orientada por other.
struct AnimCollBone {
    int16_t bone;                 // 0x00 vai para HITDATA_tag::unknown_52 (o lugar do acerto)
    int16_t other;                // 0x02
    float unknown_04;
    float half[3];                // 0x08 meias medidas, vezes ANIMSKIN_tag::scale
};
static_assert(sizeof(AnimCollBone) == 0x14);
constexpr int kAnimCollBones = 17;
extern AnimCollBone HumanCollisionBones[kAnimCollBones];
extern AnimCollBone AstronautCollisionBones[kAnimCollBones];
extern COLLJOINT_tag HitList2[kAnimCollBones - 1];  // 0x00319da0 o resultado de AnimGetCollData

uint32_t SphereSphereSweep(float r1, const _VECTOR& p1, const _VECTOR& p1_end, float r2, const _VECTOR& p2,
                           const _VECTOR& p2_end, float& t_first, float& t_second);
uint32_t NDrone2_EvaluateObstacle(Drone_tag* drone, Drone_tag* other, ObstacleEval_tag* eval);
void NDrone2_Steer(DCVars_tag* dcv, float angle);
void NDrone2_Move(DCVars_tag* dcv);
void DroneMove_SetBoundryFlags(DCVars_tag* dcv);
uint8_t DroneMove_NoBunching(DCVars_tag* dcv);
uint32_t DroneMove_FindSafetyFromScaryPosition(DCVars_tag* dcv, _VECTOR* pos, obj_tag* obj, float dist);
bool DroneMove_FindSafetyFromScaryObject(DCVars_tag* dcv);
float DroneMove_ObjectMissionPathDistance(DCVars_tag* dcv, obj_tag* obj, float radius);
float DroneMove_NextRouteAngle(DCVars_tag* dcv);
void DroneMove_TurnToObj(DCVars_tag* dcv, obj_tag* obj, float offset, float tolerance);
float DroneMove_DistanceToNextRouteNode(DCVars_tag* dcv);
void DroneMove_AstronautCombat(DCVars_tag* dcv);
void DroneMove_AstronautAnim(DCVars_tag* dcv);
void DroneMove_AstronautHitMove(DCVars_tag* dcv);
void DroneMove_AstronautDeathMove(DCVars_tag* dcv);
uint32_t NDrone2_CanReachGoalPosition(DCVars_tag* dcv);
void NDrone2_Collision(obj_tag* obj);
uint32_t NDrone2_DoCollision(DCVars_tag* dcv);
uint32_t NDrone2_DoGravity(DCVars_tag* dcv);
uint32_t DroneFunc_NearPlayerCollision(Drone_tag* drone);
obj_tag* Drone_FeetOnPoint(obj_tag* obj, HITTEST_tag* test, float* normal_y, int8_t unused, float* height);
void Drone_CollisionHandler(obj_tag* obj);
void NDrone2_DoTracking(int8_t on, DCVars_tag* dcv);
void NDrone2_DoHeadTracking(int8_t on, obj_tag* target, DCVars_tag* dcv);
void NDrone2_DoHeadTrackingBehaviour(DCVars_tag* dcv);
uint8_t NDrone2_AimTest(DCVars_tag* dcv);

// Dano (drone_damage.cpp).

// Explosão que atinge um personagem (dado da mensagem 9, montado na pilha por Explode_Propagate).
struct alignas(16) DroneImpact_tag {
    float damage;                 // 0x00 dano na distância do personagem
    float radius;                 // 0x04 raio da explosão
    uint32_t unknown_08[2];
    _VECTOR pos;                  // 0x10 centro da explosão
    obj_tag* source;              // 0x20 quem causou a explosão (EXPLODE_tag::from)
    uint32_t unknown_24[3];
};
static_assert(sizeof(DroneImpact_tag) == 0x30 && offsetof(DroneImpact_tag, source) == 0x20);

// Multiplicadores do dano que os personagens recebem (copiados do ELF na partida): pela dificuldade,
// pelo lugar do acerto e pela proteção (Drone_tag::armour).
extern float DroneDamage_Easy, DroneDamage_Normal, DroneDamage_Hard;
extern float DroneDamage_Head, DroneDamage_Legs, DroneDamage_Arms, DroneDamage_Torso;
extern float DroneArmour_Helmet, DroneArmour_Combat, DroneArmour_Jacket, DroneArmour_Vest;

void Drone_BulletHit(obj_tag* obj, BU_tag* bullet, HITDATA_tag* hit);
void NDrone2_DealWithObjHit(obj_tag* obj, Drone_tag* drone, HITDATA_tag* hit);
void NDrone2_HitDamage(DCVars_tag* dcv, float damage, int32_t location, int32_t weapon, int8_t force);
void NDrone2_DoHitEffects(DCVars_tag* dcv, HITDATA_tag* hit, float mul);
void DroneFunc_SendHurtMessage(DCVars_tag* dcv);
uint32_t NDrone2_PunchImpact(DCVars_tag* dcv, HITDATA_tag* hit, uint32_t state, int8_t anim);
uint32_t NDrone2_BulletImpact(DCVars_tag* dcv, HITDATA_tag* hit, uint32_t state, int8_t anim);
uint32_t NDrone2_ExplosiveImpact(DCVars_tag* dcv, DroneImpact_tag* impact, uint32_t state, int8_t anim);
uint32_t DroneFunc_HandleImpact(DCVars_tag* dcv, MsgObject* msg, uint32_t state, int8_t anim);
uint32_t DroneAnim_LocationImpactAnim(DCVars_tag* dcv);
void DroneFunc_InjuredTalk(DCVars_tag* dcv);
uint32_t NDrone2_PainTalk(DCVars_tag* dcv);
uint32_t NDrone2_HitTalkExtra(DCVars_tag* dcv);

// Estados de combate e auxiliares (drone_combat.cpp).
extern uint32_t g_drone_combat_guards;  // leituras fora do lugar que o port evita (drone_combat.cpp)
void NDrone2_InvalidateAttackRoute(DCVars_tag* dcv);
uint32_t NDrone2_ReFindMissionPath(DCVars_tag* dcv);
uint32_t NDrone2_SetupHostageKiller(DCVars_tag* dcv);
void NDrone2_SetupHostageExecute(DCVars_tag* dcv);
void NDrone2_HostageSituationActive(DCVars_tag* dcv);
uint32_t NDrone2_MonitorTalkToMissionObj(DCVars_tag* dcv);
uint32_t NDrone2_TalkToMissionObj(DCVars_tag* dcv);
void DroneFunc_HostageSaved(DCVars_tag* dcv);
uint32_t NDrone2_FindAmbushCover(DCVars_tag* dcv);
uint32_t DroneFunc_AttackNoWeapon(DCVars_tag* dcv);
uint32_t DroneFunc_FirstAlertState(DCVars_tag* dcv);
uint32_t DroneFunc_FirstSightState(DCVars_tag* dcv);
uint32_t DroneFunc_FirstAttack(DCVars_tag* dcv);
uint32_t DroneFunc_ChooseSFX(uint32_t first, uint32_t last);
uint32_t NDrone2_AttackTalk(DCVars_tag* dcv);
uint32_t NDrone2_SeenPlayerTalk(DCVars_tag* dcv);
uint32_t NDrone2_HasOpponent(DCVars_tag* dcv);
uint32_t NDrone2_CanAltAttack(DCVars_tag* dcv, int16_t variant);
uint32_t NDrone2_CanAimCrouch(DCVars_tag* dcv);
uint32_t NDrone2_CanStepLeft(DCVars_tag* dcv);
uint32_t NDrone2_CanStepRight(DCVars_tag* dcv);
uint32_t NDrone2_CanStrafeLeft(DCVars_tag* dcv);
uint32_t NDrone2_CanStrafeRight(DCVars_tag* dcv);
uint32_t NDrone2_CanStrafeDodgeLeft(DCVars_tag* dcv);
uint32_t NDrone2_CanStrafeDodgeRight(DCVars_tag* dcv);
uint32_t NDrone2_CanRollLeft(DCVars_tag* dcv);
uint32_t NDrone2_CanRollRight(DCVars_tag* dcv);
uint32_t NDrone2_CanBackoff(DCVars_tag* dcv);
uint32_t NDrone2_ChooseCombatMove(DCVars_tag* dcv);
uint32_t NDrone2_EvasiveMove(DCVars_tag* dcv);
uint32_t NDrone2_MoveTarget_IsRouteNode(int16_t delta, AIRoute_tag* route, DCVars_tag* dcv, int8_t at_end);
uint32_t NDrone2_MoveToObject(DCVars_tag* dcv, obj_tag* obj, float radius, int8_t follow);
uint32_t NDrone2_SetGoalToDroneWithLosToOpponent(DCVars_tag* dcv);
uint32_t NDrone2_SetGoalToCloserPosition(DCVars_tag* dcv);
uint32_t DroneAnim_ForceCrouchCover(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t DroneAnim_CanUseCornerAsLowCover(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t DroneAnim_CanCrouchLean(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t DroneAnim_CanCrouchStepOut(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t DroneAnim_CanStandLean(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t DroneAnim_CanStandStepOut(DCVars_tag* dcv, CoverNode_tag* node);
uint32_t NDrone2_CoverNodeOK(DCVars_tag* dcv, CoverNode_tag* node, uint16_t kind, int8_t force);
uint32_t NDrone2_GetPosForCornerCover(CoverNode_tag* node, uint32_t side, _VECTOR* pos, _VECTOR* rot, int8_t step);
void NDrone2_DisownCover(DCVars_tag* dcv);
uint32_t NDrone2_FindCover(DCVars_tag* dcv, int8_t force);
uint32_t NDrone2_CoverAvailable(DCVars_tag* dcv, int8_t force);
uint32_t DroneAnim_InTransition(Drone_tag* drone);
uint32_t DroneAnim_GetMoveQuad(float angle, uint16_t prev);
uint32_t DroneAnim_SetCombatMoveAnim(DCVars_tag* dcv, float dist);
uint32_t DroneFunc_CombatState(DCVars_tag* dcv);
// Animações de cobertura (copiadas do ELF): numa quina, CCModes[modo * 2 + lado - 1][passo] (os 9
// vetores CCMode* de 6 entradas, contíguos em CCModeTables); numa cobertura baixa, LCModes[passo].
constexpr uint32_t kCCModeTableSize = 0x90;
extern uint16_t CCModeTables[kCCModeTableSize / 2];
extern uint16_t* CCModes[10];
extern uint16_t LCModeCrouch[6];
extern uint16_t* LCModes;
uint16_t DroneAnim_ValidateCoverAnim(DCVars_tag* dcv, uint16_t anim);
uint32_t DroneAnim_CoverAnim(DCVars_tag* dcv, uint8_t step, int16_t end_state);
uint32_t NDrone2_SetAngleToCover(DCVars_tag* dcv, float offset);
void NDrone2_SlideToPosition(_VECTOR* pos, float t, int8_t with_y, DCVars_tag* dcv);
void DroneFunc_RunForCoverTalk(DCVars_tag* dcv);
void DroneFunc_UnderCoverTalk(DCVars_tag* dcv);
void DroneFunc_UnderCoverAimTalk(DCVars_tag* dcv);
void NDrone2_OpenDoor(DCVars_tag* dcv);
uint32_t NDrone2_DoorIsOpen(DCVars_tag* dcv);
void NDrone2_FindElevatorDoor(DCVars_tag* dcv);
void NDrone2_KickObject(DCVars_tag* dcv);
void NDrone2_MoveTo_RouteNode(DCVars_tag* dcv, int16_t delta, AIRoute_tag* route);
uint32_t DroneAnim_SetScript(uint32_t script, uint16_t anim_state, uint16_t next, uint8_t end_param, int16_t end_state,
                             uint32_t end_msg, uint32_t flags, DCVars_tag* dcv);
void NDrone2_DistanceToAIPoint(DCVars_tag* dcv, AIPoint_tag* point);
uint32_t DroneFunc_Using2ndBehaviour(Drone_tag* drone);
uint32_t DroneFunc_Set1stBehaviour(Drone_tag* drone);
uint32_t DroneFunc_Set2ndBehaviour(Drone_tag* drone);
uint32_t NDrone2_IsCivilian(DCVars_tag* dcv);
void NDrone2_CivilianScaredTalk(DCVars_tag* dcv);
uint32_t NDrone2_MoveToAlertPosition(DCVars_tag* dcv);
uint32_t NDrone2_SetDroneAlertRoute(DCVars_tag* dcv);
uint32_t NDrone2_SetSoundAlertRoute(DCVars_tag* dcv);
void NDrone2_SetAngleToAlert(DCVars_tag* dcv, float offset);
float NDrone2_GetAngleDifferenceToAlert(DCVars_tag* dcv);
void DroneFunc_HeardNoiseTalk(DCVars_tag* dcv);
uint32_t NDrone2_FindAlarmPoint(DCVars_tag* dcv);
void plane_from_two_vectors_and_point(_VECTOR* plane, const _VECTOR* a, const _VECTOR* b, const _VECTOR* p);
uint32_t NDrone2_SetDronePartner(uint16_t type, DCVars_tag* dcv);
uint32_t NDrone2_DoorGuardHandler(DCVars_tag* dcv);
void NDrone2_LogDroneDeath(DCVars_tag* dcv);
uint32_t DroneFunc_RecoverTime(DCVars_tag* dcv, HITDATA_tag* hit);
uint32_t DroneFunc_AnyoneNear(DCVars_tag* dcv, float radius);
uint32_t NDrone2_CheckUnsurrender(DCVars_tag* dcv);
uint32_t DroneAnim_SetDAnimNow(uint16_t danim, uint16_t blend, int8_t call, DCVars_tag* dcv);
uint32_t NDrone2_NearArmedBond(DCVars_tag* dcv, float radius);

// Funções de estado (drone_states.cpp; nomes do ELF: NDrone2_DSTATE_<nome>).
#define NF_DRONE_STATES(X)                                                                                 \
    X(Global) X(WaitSwitch) X(Disabled) X(Idle) X(Alert) X(InitPatrol) X(Patrol) X(CivilianInit) X(Civilian) \
    X(CivilianPatrol) X(ReturnToPatrolPath) X(CivilianMission) X(CivilianMissionWait) X(AllyLeadInit)       \
    X(PartyGirlInit) X(PartyGirl) X(Death_Anim) X(DeathByExplosion) X(Dead) X(Fade) X(FadeFast)             \
    X(EnemyRunToPoint) X(DeleteMe) X(JustStand) X(WaitForever)                                           \
    /* script de entrada (drone_script.cpp) */                                                          \
    X(PlayScript)                                                                                        \
    /* combate (drone_combat.cpp) */                                                                     \
    X(Attack) X(Alerted1stEncounter) X(Combat) X(CombatNoMove) X(CombatOutOfRange) X(CombatNewSighting)  \
    X(CombatNoSight) X(CombatTooClose) X(CombatWait) X(CombatNoRoute) X(NoOpponent) X(Obstructed)        \
    X(DroneStuck) X(DrawWeapon) X(AimStand) X(AimStandFire) X(AimStandReload) X(AimStandDiscard) X(Prone) \
    X(ProneFire) X(AimBackoff) X(CrouchCover) X(AimCrouch) X(AimCrouchFire) X(AimCrouchReload)           \
    X(AltAttack) X(StepAimLeft) X(StepAimRight) X(StrafeAimLeft) X(StrafeAimRight) X(StrafeDodgeLeft)   \
    X(StrafeDodgeRight) X(RollLeftCrouch) X(RollRightCrouch) X(RunForCover) X(UnderCoverInit)               \
    X(UnderCoverIdle) X(UnderCoverAim) X(UnderCoverFire) X(UnderCoverSniperFire) X(UnderCoverSniperReload)  \
    X(UnderCoverReturn) X(UnderCoverTypeChange) X(UnderCoverLeave) X(UnderCoverLeaveNow) X(GrenadeThrow)    \
    X(SniperIdle) X(SniperAim) X(SniperFire) X(SniperReload) X(OpenDoor) X(KickObject) X(ActionAnim)       \
    X(StandFiddle) X(GoToGoalPosition) X(HostageGoToGoalPosition) X(AllyGoToGoalPosition) X(AlertToPosition) \
    X(SearchArea) X(Investigate) X(SeenOpponent) X(SeenDeadBody) X(SeenSurrenderedDrone) X(SeenDroneShot)    \
    X(SeenExplosive) X(HeardNoise) X(HeardNoiseAware) X(HeardNoiseSuspect) X(HeardNoiseAlert)                 \
    X(CivilianGuard) X(CivilianDoorGuard) X(RunToAlarm) X(PressAlarm) X(DonePressAlarm) X(CivilianScared)      \
    X(CivilianHiding) X(EnemyMission) X(NinjaStand) X(NinjaAttack) X(NinjaAttackLongRange)                   \
    X(NinjaAttackMidRange) X(NinjaAttackShortRange) X(NinjaGetCloseToPlayer) X(NinjaSword) X(NinjaSomersault) \
    X(NinjaBackflip) X(NinjaSideflip) X(NinjaSideflipLeft) X(NinjaSideflipRight) X(NinjaStandFire)           \
    X(NinjaNoRoute) X(SpaceDrake) X(AstronautLaunch) X(AstronautHit) X(AstronautDeath) X(AstronautCombat)    \
    X(AstronautCombatMove) X(PunchImpact) X(ExplosiveImpact) X(BulletImpact) X(KnockedOut_Anim) X(Knocked_Out) \
    X(SpecialDeath_Anim) X(Taser) X(Stunned) X(Stunned_Recover) X(StunGrenadeImpact) X(StunGrenadeLoop)        \
    X(StunGrenadeRecover) X(StunDartImpact) X(StunDartLoop) X(StunDartRecover) X(SmokedOut) X(SmokedOut_Loop)   \
    X(SmokedOut_Recover) X(Surrender_Anim) X(Surrendered) X(Unsurrender_Anim) X(TruckDriverInit)               \
    X(TruckDriverInitAlert) X(TruckDriverIdle) X(TruckDriverMission) X(AbseilInit) X(AbseilSlide) X(AbseilHang) \
    X(AbseilStepOff) X(AbseilDeath) X(RunAwayFromObject) X(HideFromScaryObject) X(RecoverFromScaryObject)       \
    X(ElevatorJumper) X(HoldItRightThere) X(FailMission) X(StandBlind) X(HangUp)                         \
    X(InterogateAssist) X(InterogateAssistWait) X(Interogator) X(Interogate) X(InterogateWalk)      \
    X(CivilianChallenge) X(KikoMission) X(KikoMissionRun) X(AllyLead) X(AllyLeadPlayerInWay) X(AllyLeadHide) \
    X(AllyLeadWait) X(AllyLeadMissionWait) X(AllyLeadBondCombat) X(AllyLeadDone) X(AllyFollowInit)      \
    X(AllyFollow) X(AllyFollowWait) X(AllyFollowDone) X(HostageKiller) X(HostageKillerAttack) X(Hostage)   \
    X(HostageDie) X(HostageSaved) X(HostageIdle) X(HostageHide) X(HostageDead) X(CastleChatGuard1)       \
    X(AmbushInit) X(AmbushWait) X(Tester1) X(Tester2) X(Tester3) X(Tester4)
#define NF_DECLARE_STATE(name) \
    uint32_t NDrone2_DSTATE_##name(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg);
NF_DRONE_STATES(NF_DECLARE_STATE)
#undef NF_DECLARE_STATE

// Armas da IA (drone_weapon.cpp; módulo DroneWeap). Um estado pede o tiro com DroneWeap_Fire (firing;
// one_burst para uma rajada só) e, a cada tick, NDrone2_ControlSTANDARD chama DroneWeap_HandleFiring:
// rajadas de burst balas (weapon_data +0x2c, o terceiro modo), uma a cada fire_delay ticks e uma pausa
// entre rajadas (DroneWeap_BurstDelay), com o erro de pontaria de DroneWeap_DoBulletAccuracy; as
// balas são as do jogador (Bullet_init com a definição da arma na mão).
static_assert(offsetof(Drone_tag, firing) == 0x3b && offsetof(Drone_tag, fired) == 0x40);
static_assert(offsetof(Drone_tag, target_moving) == 0x1dc && offsetof(Drone_tag, target_stopped_seen) == 0x1e0);
static_assert(offsetof(Drone_tag, aim_error) == 0x200 && offsetof(Drone_tag, target_change_time) == 0x224);
static_assert(offsetof(Drone_tag, clip) == 0xbbc && offsetof(Drone_tag, burst) == 0xbc0);
static_assert(offsetof(Drone_tag, next_shot) == 0xbc4 && offsetof(Drone_tag, last_shot) == 0xbc8);

// Constantes de tiro da IA (DroneFiring_BurstDelay_Min até DroneFiring_PlayerBackShot_Damage, 0x48
// bytes contíguos no ACTION.ELF, copiados na partida). Tempos em segundos, pausas em ticks a 60 Hz.
struct DroneFiringSettings {
    float burst_delay_min;              // 15
    float burst_delay_normal;           // 45 (vezes 1,66/1,33/1/0,66/0,33 por stats[1])
    float burst_delay_max;              // 60
    float burst_delay_min_dist;         // 4: mais perto, a pausa mínima
    float burst_delay_max_dist;         // 30: mais longe, a máxima
    float accuracy_easy;                // 0,5 (dificuldade 1)
    float accuracy_normal;              // 1 (0, 2 e 5 em diante)
    float accuracy_hard;                // 2 (3 e 4)
    float new_sighting_time_to_hit;     // 2: só acerta depois de ver o alvo por este tempo
    float target_first_moved_time_to_hit;   // 2
    float target_first_moved_accuracy;      // 0,25
    float target_moving_accuracy;           // 0,75
    float target_first_stopped_time_to_hit; // 2
    float target_first_stopped_accuracy;    // 1
    float too_close_distance;           // 4
    float too_close_accuracy;           // 2 (aplicado duas vezes, como no original)
    float too_close_damage;             // 2: dano no jogador de perto (Drone_ModBulletDamage)
    float player_back_shot_damage;      // 2: dano no jogador pelas costas (subclasse 0x12)
};
static_assert(sizeof(DroneFiringSettings) == 0x48);
extern DroneFiringSettings DroneFiring;

void DroneWeap_HandleFiring(DCVars_tag* dcv);
uint32_t DroneWeap_DoFiring(DCVars_tag* dcv);
void DroneWeap_Fire(int8_t restart, DCVars_tag* dcv);
void DroneWeap_FireWeapon(DCVars_tag* dcv);
uint32_t DroneWeap_Ready2Fire(DCVars_tag* dcv);
uint32_t DroneWeap_NextBulletTime(DCVars_tag* dcv, int8_t new_burst, int8_t start);
uint32_t DroneWeap_BurstDelay(Drone_tag* drone);
uint32_t DroneWeap_ManageBurstCount(DCVars_tag* dcv);
void DroneWeap_DoBulletAccuracy(DCVars_tag* dcv);
void DroneWeap_DoOpponentTargetting(DCVars_tag* dcv);
uint32_t DroneWeap_AimTarget_IsOpponent(int8_t check_angle, int8_t keep_turning, DCVars_tag* dcv);
uint32_t DroneWeap_OpponentIsAimingAtMe(DCVars_tag* dcv, float degrees);
uint32_t DroneWeap_OpponentHasWeapon(DCVars_tag* dcv);
void DroneWeap_ThrowGrenade(DCVars_tag* dcv);
uint32_t DroneWeap_CanThrowGrenade(DCVars_tag* dcv);
void DroneWeap_CloseCombatImpact(DCVars_tag* dcv, int32_t kind);
void DroneWeap_ChangeWeapon(DCVars_tag* dcv, int32_t weapon);
void DroneWeap_PlaceTertiary(DCVars_tag* dcv);
void DroneWeap_DropWeapon(DCVars_tag* dcv);
uint32_t DroneAnim_PlayFiringAnim(DCVars_tag* dcv);
uint32_t NDrone2_CanMoveToRelativePos(Drone_tag* drone, obj_tag* obj, float x, float y, float z);
void NDrone2_SetAngleToObj(DCVars_tag* dcv, obj_tag* obj, float offset, int8_t force);
uint32_t NDrone2_BondIsFacingMe(DCVars_tag* dcv, float degrees);
float Drone_ModBulletDamage(Drone_tag* drone, obj_tag* player, HITDATA_tag* hit);
float BOT_getAggressionMul(Drone_tag* drone);
extern uint32_t g_drone_weapon_guards;  // escritas fora do lugar que o port evita (drone_weapon.cpp)

// Geradores (drone_spawner.cpp; módulo DroneSpawner). Um gerador é um objeto 0x30 (posicionamento
// 0xf1) com um DroneSpawner_tag em obj->data e o seu grupo de personagens (parâmetro 1) em
// NPCGlobals.spawners. Na carga, NDrone2_SetupGroups chama DroneSpawner_Init para cada grupo com
// gerador: os personagens do grupo viram modelos (os DIVars_tag deles vão para entries) e saem do mapa.
// Depois, a cada tick (DroneSpawner_Control), o gerador recria personagens dos modelos com o próprio
// caminho da carga (NDrone2_CreateObj + NDrone2_PostLoad_Init), no máximo max_alive vivos e uma vez por
// modelo em cada leva (wave), longe do jogador (distance) e fora da vista dele. Tipo 2: repõe os que
// morrem até max_alive; tipo 3: a próxima leva só quando a anterior inteira morreu. O objeto 0x30 do
// "jogador falso" (Drone_PostLoad_Init) usa a mesma estrutura, zerada: sem grupo, o controle não faz nada.

// Um modelo do gerador (0xd0 bytes, DroneSpawner_tag::entries).
struct alignas(16) DroneSpawnEntry_tag {
    uint32_t spawned;        // 0x00 personagens gerados deste modelo na leva atual (só gera com 0)
    // 0x04 a sala do personagem modelo na hora de DroneSpawner_Init. Ainda nula no original (o personagem
    // só é posto no mapa por NDrone2_DefaultInit, depois), o que desliga o teste de linha de visão de
    // Drone_PositionVisible: na prática, só a distância conta.
    cel_tag* cel;
    uint32_t unknown_08[2];
    DIVars_tag vars;         // 0x10 o Drone_tag::divars do modelo (o obj aponta para o último gerado)
};
static_assert(sizeof(DroneSpawnEntry_tag) == 0xd0 && offsetof(DroneSpawnEntry_tag, vars) == 0x10);

// Dados do objeto gerador (0x3c bytes, DroneSpawner_Create). Os canais são índices de switch_channels.
struct DroneSpawner_tag {
    uint32_t kind;            // 0x00 parâmetro 0: 2 repõe, 3 em levas (menor que 2: não cria o gerador)
    uint32_t group;           // 0x04 parâmetro 1: o grupo dos modelos (0: o gerador não faz nada)
    uint16_t entry_count;     // 0x08 modelos (o tamanho do grupo na carga)
    // 0x0a parâmetro 2: vivos ao mesmo tempo e gerados por leva (0 ou mais que o grupo: o grupo)
    uint16_t max_alive;
    uint16_t waves;           // 0x0c parâmetro 3: levas (0: 32000)
    uint16_t channel_start;   // 0x0e parâmetro 4: começa quando o canal liga (0: desde a carga)
    uint16_t channel_stop;    // 0x10 parâmetro 5: o gerador para e some quando o canal liga
    uint16_t channel_done;    // 0x12 parâmetro 6: ligado quando a última leva inteira morreu
    uint16_t channel_kill;    // 0x14 parâmetro 7: tira do mapa os personagens gerados
    uint16_t unknown_16;
    float distance;           // 0x18 parâmetro 8 (inteiro; 0: 10): distância mínima do jogador a um modelo
    uint8_t spawn;            // 0x1c gera neste tick
    uint8_t ready;            // 0x1d DroneSpawner_Init já rodou
    uint8_t running;          // 0x1e o canal de começo já ligou
    // 0x1f gera sem olhar distância nem visão: na primeira leva, se o canal de começo já estava ligado
    // (ou é 0) e o de parada desligado na carga
    uint8_t anywhere;
    uint8_t killing;          // 0x20 o canal de remoção ligou
    uint8_t unknown_21[3];
    uint32_t init_frame;      // 0x24 GameState.frame de DroneSpawner_Init
    uint16_t wave_spawned;    // 0x28 gerados na leva atual
    uint16_t alive;           // 0x2a gerados vivos (contados a cada tick)
    uint16_t wave;            // 0x2c leva atual
    uint16_t entry_count2;    // 0x2e = entry_count (o sorteio do modelo usa este)
    uint16_t unknown_30;
    uint16_t unknown_32;
    obj_tag** spawned;        // 0x34 max_alive vagas: os personagens gerados vivos
    DroneSpawnEntry_tag* entries;  // 0x38 entry_count modelos
};
static_assert(sizeof(DroneSpawner_tag) == 0x3c && offsetof(DroneSpawner_tag, distance) == 0x18);
static_assert(offsetof(DroneSpawner_tag, init_frame) == 0x24 && offsetof(DroneSpawner_tag, spawned) == 0x34);

void DroneSpawner_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void DroneSpawner_Init(obj_tag* spawner);
obj_tag* DroneSpawner_SpawnDrone(obj_tag* spawner);
void DroneSpawner_Control(obj_tag* spawner);
void DroneSpawner_Delete(obj_tag* spawner);
void DroneSpawner_DroneDelete(obj_tag* spawner, obj_tag* drone);
uint32_t Drone_PositionVisible(_VECTOR* pos, cel_tag* cel);

// Contagens só do port (nf_loader_check, testes): geradores criados, modelos tirados do mapa,
// personagens gerados, levas fechadas, vagas soltas por DroneSpawner_DroneDelete e casos protegidos
// (grupo fora da tabela de 31, sem jogador ou câmera para Drone_PositionVisible).
struct DroneSpawnerStats {
    uint32_t created = 0;
    uint32_t templates = 0;
    uint32_t spawned = 0;
    uint32_t waves = 0;
    uint32_t released = 0;
    uint32_t guards = 0;
};
extern DroneSpawnerStats g_drone_spawner_stats;

// A lista dos personagens (NPCGlobals.drones).
LLISTINFO_tag* Drone_List();

// Só para testes.
void drone_debug_power_on();
