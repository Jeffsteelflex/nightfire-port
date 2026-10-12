#pragma once
// O jogador (módulo Player do ACTION.ELF): pontos de partida, criação (Player_Init), o tick
// (Player_Update: andar, agachar, pular, olhar), a colisão com o cenário (Player_Collision monta a
// cápsula, Player_CollisionHandler e Player_FeetOnPoint tratam o resultado), usar objetos
// (Player_Activate) e a câmera em primeira pessoa (Player_PositionCamera). As armas estão em
// engine/player_weapon.h.
//
// O objeto do jogador é do tipo 3; os dados do tipo (o "bloke", BLData) ficam em obj_tag::data e o
// estado de animação em obj_tag::sphere_src (engine/anim.h). Os outros subestados (escada, corda,
// tirolesa, gravidade zero...) ainda não foram portados. O HUD está em engine/hud.h.

#include <cstdint>

#include "engine/collide.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/player_stats.h"
#include "platform/ptr32.h"

struct ANIMSET_tag;

// Ponto de partida (0xe0 bytes; player_start tem 32).
struct PLAYERSTART_tag {
    _VECTOR pos;          // 0x00
    _VECTOR rot;          // 0x10
    uint16_t unknown_20;
    uint16_t kind;        // 0x22: 1 = início da fase (posicionamento do tipo 0x24), 0 = tipo 0x2d
    level_tag level;      // 0x24: parâmetros do posicionamento; params[0] = canal de switch que o habilita
    uint8_t pad[0xe0 - 0xd4];
};
static_assert(sizeof(PLAYERSTART_tag) == 0xe0);

// Subestados do jogador (obj_tag::unknown_f6).
enum : uint16_t {
    kPlrWalk = 0,
    kPlrClimb = 1,
    kPlrGrapple = 2,
    kPlrSwim = 3,
    kPlrCrouch = 4,
    kPlrScanMode = 5,  // depuração
    kPlrWire = 6,
    kPlrCreep = 7,
    kPlrZeroG = 8,     // e 9
    kPlrRide = 10,
    kPlrZipline = 15,
};

// Uma arma do inventário do jogador (BLData::weapons, 0xc bytes), indexada pela classe da arma
// (weapon_definition_tag::klass) ou pela própria arma.
struct PLRWEAPON_tag {
    float zoom;                 // 0x0 zoom guardado (começa na raiz de weapon_data +0x88)
    int16_t rounds;             // 0x4 balas no pente
    uint8_t have;               // 0x6 o jogador tem a arma
    uint8_t mode;               // 0x7 modo de disparo (índice em weapon_definition_tag::burst)
    int8_t variant;             // 0x8 variante escolhida: arma - classe
    uint8_t unknown_9[3];
};
static_assert(sizeof(PLRWEAPON_tag) == 0xc);

// Dados do jogador (0x970 bytes). Velocidades "por tick" já vêm multiplicadas por FRAME_RATE_MUL.
struct alignas(16) BLData {
    _VECTOR old_pos;            // 0x000 posição no começo do tick
    _VECTOR vel;                // 0x010 velocidade local por tick (x de lado, z para a frente)
    _VECTOR prev_vel;           // 0x020
    _VECTOR rot_vel;            // 0x030 giro por tick (y: virar)
    _VECTOR prev_rot_vel;       // 0x040
    _VECTOR fall;               // 0x050 velocidade no mundo em m/s (gravidade, pulo)
    _VECTOR muzzle;             // 0x060
    MATRIX zerog_upright;       // 0x070 gravidade zero: o jogador endireitado (Player_ZeroG, usar)
    quaternion_tag zerog_quat;  // 0x0b0 a mesma orientação como quaternion (o alvo do slerp)
    uint8_t unknown_0c0[0x10];
    int16_t zerog_turn;         // 0x0d0 ticks que faltam do endireitar (1,5 s depois de usar)
    uint8_t unknown_0d2[0x0e];
    _VECTOR orbit_target;       // 0x0e0 câmera orbital (CAM_ORBIT, modo de câmera 1)
    float orbit[4];             // 0x0f0
    P32<obj_tag> orbit_obj;     // 0x100
    uint8_t unknown_104[0x0c];
    float floor_normal_y;       // 0x110 normal do chão (Player_FeetOnPoint); < 0,5 é íngreme
    P32<void> aim_target;       // 0x114 alvo da mira automática (Check_AutoAim)
    float aim_x;                // 0x118 mira dentro da tela, -0,296 a 0,296
    float aim_y;                // 0x11c -0,4 a 0,4
    float aim_cur_x;            // 0x120 a mira suavizada (Player_AutoAim), que a bala e a arma seguem
    float aim_cur_y;            // 0x124
    float aim_turn_x;           // 0x128
    float aim_turn_y;           // 0x12c
    int8_t look_state;          // 0x130 1: olhar manual; 2 e 3: volta a visão para pitch_target
    int8_t walk_class;          // 0x131 1 parado/devagar, 2 rápido (Player_SSWalk)
    uint8_t unknown_132;
    uint8_t unknown_133[0x0d];
    _VECTOR grapple_point;      // 0x140 onde o gancho prendeu (Player_SetGrapplePoint; segue o objeto que se mexe)
    float grapple_speed;        // 0x150 metros por tick ao ser puxado, crescendo 0,05 por tick até 1,5
    P32<obj_tag> grapple_obj;   // 0x154 o ponto de gancho (tipo 0x51) em que prendeu
    P32<obj_tag> rope;          // 0x158 a corda (entidade 0x2000138, Player_InitGrapple; Player_GrappleSetRope)
    // 0x15c o gancho: 1 puxando o jogador, 2 chegou (pendurado enquanto segura o gatilho), 4 soltou
    int16_t unknown_15c;
    int16_t grapple_wait;       // 0x15e ticks antes de o chão poder soltar o gancho (15 ao prender)
    uint8_t move_locked;        // 0x160 Player_Move não anda (o gancho voando: Bullet_init)
    uint8_t unknown_161[0x0f];
    int16_t ammo[33];           // 0x170 munição guardada de cada tipo (ammo_data, weapon_data +0x90)
    uint16_t unknown_1b2;
    PLRWEAPON_tag weapons[114]; // 0x1b4 por arma (classe): pente, se tem, modo, variante
    uint8_t unknown_70c[0x34];  // 0x70c (Player_RamSave guarda 0x33 bytes daqui)
    HITTEST_tag hit_test;       // 0x740 cápsula do corpo (obj_tag::hit_test aponta para cá)
    P32<void> hud;              // 0x7e0 HUDINFO_tag (engine/hud.h), de HUD_Init
    uint32_t unknown_7e4;
    P32<obj_tag> gun;           // 0x7e8 objeto da arma na mão (Player_InitWeapon)
    P32<obj_tag> fx[32];        // 0x7ec objetos de efeito só da câmera do jogador (Player_InitWeapon)
    P32<obj_tag> glow;          // 0x86c brilho (entidade 0x2000132)
    uint32_t unknown_870;
    P32<obj_tag> muzzle_obj;    // 0x874
    P32<obj_tag> ride;          // 0x878 veículo, câmera de controle remoto...
    P32<obj_tag> unknown_87c;
    P32<obj_tag> target;        // 0x880 objeto em que as armas 0x56-0x58 agem (PDA, ...)
    P32<obj_tag> ladder;        // 0x884 a escada, o fio (corda ou tirolesa) ou a parede de esgueirar em uso
    float visibility;           // 0x888 0 a 1 (sombra): obj_tag::unknown_107[0] = 255 * isto
    P32<void> body_glow;        // 0x88c
    P32<ANIMSET_tag> anim_set;  // 0x890 conjunto de animações (PlayerAnimSetInit*)
    float health;               // 0x894
    uint32_t zoom_sound;        // 0x898 som do zoom tocando
    uint32_t water_sound;       // 0x89c
    float ladder_top;           // 0x8a0 a escada: altura de obj_tag::pos acima da qual o jogador sai por cima
    float ladder_bottom;        // 0x8a4 e abaixo da qual sai por baixo (Player_CollLadder, Player_Climb)
    float pitch;                // 0x8a8 olhar para cima e para baixo, -1 a 1 (x 90 graus)
    float pitch_target;         // 0x8ac
    float armour;               // 0x8b0 colete: absorve os tiros antes da vida (Player_HandlePain)
    float unknown_8b4;
    float health_shown;         // 0x8b8
    float unknown_8bc;          // 0x8bc 1 a cada ferimento (Player_HandlePain); cai com o tempo
    float air;                  // 0x8c0 fôlego, 0 a 100
    float underwater;           // 0x8c4 0 a 1
    uint32_t water_flags;       // 0x8c8
    float unknown_8cc;          // 0x8cc a corda: cansaço, 0 a 100 (100 / 30 s pendurado; solta em 100)
    float zoom;                 // 0x8d0 1 sem zoom
    float zoom_target;          // 0x8d4 o zoom anda até aqui (Player_Zoom)
    float zoom_max;             // 0x8d8 weapon_data +0x88 da arma na mão
    float sway;                 // 0x8dc fase do balanço da arma ao andar (Player_WeaponRecoil)
    float recoil_x;             // 0x8e0 decai 25% por tick
    float recoil_y;             // 0x8e4 decai 25% por tick; gira a arma em y
    float sway2;                // 0x8e8 fase do balanço lento
    float scope_speed_x;        // 0x8ec acelerações do giro (AccelFunc0)
    float scope_speed_y;        // 0x8f0
    float aim_speed_x;          // 0x8f4
    float aim_speed_y;          // 0x8f8
    float turn_speed;           // 0x8fc
    float look_speed;           // 0x900
    float climb_yaw;            // 0x904 o giro do jogador ao pegar a escada
    float eye_height;           // 0x908 olhos acima de obj_tag::pos (0,7; 0 debaixo d'água)
    float crouch_drop;          // 0x90c quanto os olhos descem agachado, até 0,45
    float turn_kick;            // 0x910 giro extra que decai (empurrões)
    float unknown_914;
    float fade_time;            // 0x918
    float fade_left;            // 0x91c
    float unknown_920;
    float step;                 // 0x924 distância desde o último passo
    float fall_time;            // 0x928 ticks caindo
    float fire_timer;           // 0x92c ticks até o próximo disparo (Player_WeaponFiring)
    int16_t last_weapon;        // 0x930 última arma (troca rápida, Player_WeaponChange)
    int16_t last_gadget;        // 0x932 último aparelho (armas 0x4a-0x5e)
    uint16_t saved_weapon;      // 0x934 arma na mão guardada por Player_RamSave
    int16_t burst;              // 0x936 tiros que faltam na rajada
    int16_t muzzle_time;        // 0x938
    int16_t idle_ticks;         // 0x93a ticks parado sem mexer nos controles (animações de espera)
    int16_t idle_time;          // 0x93c
    int16_t idle_wait;          // 0x93e espera antes da próxima animação de espera
    uint16_t ground_bits;       // 0x940 histórico de "no chão", um bit por tick (bit 3 = este)
    int16_t prev_substate;      // 0x942
    int16_t ride_time;          // 0x944
    int16_t menu;               // 0x946
    uint16_t bond_moment;       // 0x948 ticks do 007 girando no HUD (PlrStat_DoneBondMoment: 200)
    int8_t active;              // 0x94a 1: atualiza; > 1: espera esses ticks
    int8_t frozen;              // 0x94b
    int8_t jump;                // 0x94c 0 no chão, 1 subindo, 2 caindo
    // 0x94d o passo do subestado de travessia: escada 1; corda 6 parado, 7 e 8 para os lados, 0xa virando,
    // 0xc pendurando na corda; tirolesa 9 chegando; esgueirar 0 parado, 1 e 2 para os lados
    uint8_t traverse_step;
    int8_t player;              // 0x94e índice do jogador
    int8_t player_b;            // 0x94f
    uint8_t cam_mode;           // 0x950 0 primeira pessoa
    int8_t traverse_wait;       // 0x951 ticks até poder pegar de novo escada, corda ou parede (ao soltar)
    int8_t jump_wait;           // 0x952
    int8_t unknown_953;
    uint8_t unknown_954;
    uint8_t unknown_955;
    uint8_t unknown_956;
    int8_t pitch_hold;          // 0x957
    uint8_t alt_hand;           // 0x958 alterna as animações de tiro e recarga (flags 0x40000, 0x200)
    int8_t idle_state;          // 0x959 animação de espera da arma: 0 nenhuma, 1 voltando, 3 olhando
    uint8_t unknown_95a;
    uint8_t muzzle_flash;       // 0x95b
    uint8_t bullet_out;         // 0x95c a bala deste disparo já saiu
    uint8_t taser_hit;          // 0x95d o taser alcançou alguém
    uint8_t unknown_95e;
    uint8_t touch_type;         // 0x95f
    int8_t anim_wait;           // 0x960
    uint8_t unknown_961;
    uint8_t night_vision;       // 0x962
    uint8_t blur;               // 0x963
    uint8_t gun_flag8;          // 0x964 a arma na mão tem a flag 8 (Player_SetWeaponAnim)
    int8_t punch_skin;          // 0x965 também a manga dos braços (AnimObjectSetSleeveType)
    int8_t step_reset;          // 0x966
    uint8_t hit_dir;            // 0x967
    uint8_t pain_flash;         // 0x968 vermelho do ferimento: dano x 21,3 (até 128) somado, até 255
    uint8_t unknown_969[7];
};
static_assert(sizeof(BLData) == 0x970);
static_assert(offsetof(BLData, zerog_upright) == 0x70 && offsetof(BLData, zerog_quat) == 0xb0);
static_assert(offsetof(BLData, zerog_turn) == 0xd0 && offsetof(BLData, orbit_target) == 0xe0);
static_assert(offsetof(BLData, floor_normal_y) == 0x110 && offsetof(BLData, look_state) == 0x130);
static_assert(offsetof(BLData, move_locked) == 0x160 && offsetof(BLData, ammo) == 0x170);
static_assert(offsetof(BLData, grapple_point) == 0x140 && offsetof(BLData, rope) == 0x158);
static_assert(offsetof(BLData, ladder_top) == 0x8a0 && offsetof(BLData, climb_yaw) == 0x904);
static_assert(offsetof(BLData, traverse_step) == 0x94d && offsetof(BLData, traverse_wait) == 0x951);
static_assert(offsetof(BLData, weapons) == 0x1b4 && offsetof(BLData, unknown_70c) == 0x70c);
static_assert(offsetof(BLData, hit_test) == 0x740 && offsetof(BLData, gun) == 0x7e8);
static_assert(offsetof(BLData, glow) == 0x86c && offsetof(BLData, target) == 0x880);
static_assert(offsetof(BLData, fire_timer) == 0x92c && offsetof(BLData, idle_wait) == 0x93e);
static_assert(offsetof(BLData, alt_hand) == 0x958 && offsetof(BLData, gun_flag8) == 0x964);
static_assert(offsetof(BLData, pitch) == 0x8a8 && offsetof(BLData, zoom) == 0x8d0);
static_assert(offsetof(BLData, turn_speed) == 0x8fc && offsetof(BLData, eye_height) == 0x908);
static_assert(offsetof(BLData, fall_time) == 0x928 && offsetof(BLData, ground_bits) == 0x940);
static_assert(offsetof(BLData, active) == 0x94a && offsetof(BLData, player) == 0x94e);
static_assert(offsetof(BLData, touch_type) == 0x95f && offsetof(BLData, pain_flash) == 0x968);
static_assert(offsetof(BLData, armour) == 0x8b0 && offsetof(BLData, unknown_8bc) == 0x8bc);

inline BLData* bloke_of(obj_tag* obj) { return static_cast<BLData*>(obj->data); }

constexpr uint32_t kPlayerStarts = 32;
extern PLAYERSTART_tag player_start[kPlayerStarts];
extern uint32_t player_start_positions_index;
extern uint32_t player_start_position;
extern BLData* glb_blokes[4];  // dados de cada jogador (nulo sem jogador)
extern _VECTOR WldGravity;     // (0, -9,8, 0) a partir de Player_Init
extern uint8_t surfaced;       // 1 fora d'água
extern uint8_t debug_scanmode;
extern uint8_t debug_clipmode;  // 0x0030cb5e (C_CHCHFLY_Handler; nada mais lê)
extern uint8_t benchmark;

// Constantes de giro (o .data do ACTION.ELF; ReadTuningVars pode mudá-las).
extern float Plr_AimSpeed_X, Plr_AimSpeed_Y, Plr_AimTurnSpeed_X, Plr_AimTurnSpeed_Y;
extern float Plr_ScopeSpeed_X, Plr_ScopeSpeed_X_Mul, Plr_ScopeSpeed_X_Steps;
extern float Plr_ScopeSpeed_Y, Plr_ScopeSpeed_Y_Mul, Plr_ScopeSpeed_Y_Steps;
extern float Plr_NoAimTurnSpeed_X, Plr_NoAimTurnSpeed_X_Mul, Plr_NoAimTurnSpeed_X_Steps;
extern float Plr_NoAimTurnSpeed_Y, Plr_NoAimTurnSpeed_Y_Mul, Plr_NoAimTurnSpeed_Y_Steps;

void Player_ResetStartPos();
void Player_AddNewStartPos(_VECTOR* pos, _VECTOR* rot, uint16_t kind, level_tag* level);
void Player_Start();
obj_tag* Player_Init(int16_t index, _VECTOR* pos, _VECTOR* rot, level_tag* level);
void Player_Update(obj_tag* obj);
void Player_SSWalk(BLData* b, obj_tag* obj);
void Player_SSCrouch(BLData* b, obj_tag* obj);
// Gravidade zero (subestados 8 e 9: 0700001b e a trapaça C_CHCHZEROG) e o voo de depuração (5: debug_scanmode, a
// trapaça C_CHCHFLY).
void Player_ZeroG(BLData* b, obj_tag* obj);
void Player_SSScanMode(BLData* b, obj_tag* obj);
void Player_Move(BLData* b, obj_tag* obj, float scale);
float AccelFunc0(float input, float* speed, float base, float mul, float steps, float dead, float full);
void Player_MonitorAir(BLData* b, obj_tag* obj);
void Player_HandleJump(obj_tag* obj, BLData* b, _VECTOR* gravity);
bool Player_InWater(BLData* b, obj_tag* obj);
void Player_Aiming(obj_tag* obj);
void Player_ViewClamping(obj_tag* obj);
void Player_Collision(obj_tag* obj);
void Player_CollisionHandler(obj_tag* obj);
obj_tag* Player_FeetOnPoint(obj_tag* obj, HITTEST_tag* test, float* normal_y, int8_t dead);
uint32_t Player_DealWithObjHit(obj_tag* obj, BLData* b, HITDATA_tag* hit);
uint8_t Player_Activate(obj_tag* obj, BLData* b);
void Player_PositionCamera(obj_tag* obj, void* data);
void Player_GetHeadPos(_VECTOR* out, obj_tag* obj, MATRIX* m);
void Player_SetCamMode(BLData* b, uint16_t mode);
int16_t Player_ChangeSubState(obj_tag* obj, int32_t substate);
void Player_StandAtNewPosition(obj_tag* obj, _VECTOR* pos, _VECTOR* rot, uint16_t substate);
void Player_ClearInertia(obj_tag* obj);
void Player_ClearAimInertia(obj_tag* obj);
void Player_InShadow(BLData* b, obj_tag* obj);
void Player_SetHealth(BLData* b, float health);
void Player_SetFlashBang(BLData* b, float ticks, uint8_t level);  // o clarão: borrão e esmaecimento branco
// Dano e morte (player.cpp).
void Player_HandlePain(obj_tag* obj, BLData* b, float damage, uint16_t type, int16_t location);
uint32_t Player_CheckForDeath(obj_tag* obj, uint32_t type);
void Player_HandleDeath(obj_tag* obj);
// Multiplicadores do dano que o jogador recebe (copiados do ELF na partida): pela dificuldade no modo
// história; no multijogador, o geral e pelo lugar do acerto.
extern float Plr_DMod_Multi, Plr_DMod_Head, Plr_DMod_LowerLimb, Plr_DMod_UpperLimb;
extern float Plr_DMod_Easy, Plr_DMod_Normal, Plr_DMod_Hard;

// Só no port: a última chamada a Player_Init (pontos de partida para a câmera livre e as
// ferramentas de verificação).
struct PlayerInitCall {
    _VECTOR pos{};
    _VECTOR rot{};
    bool called = false;
    uint8_t pad[15]{};
};
extern PlayerInitCall g_player_init_call;

// Só no port (o olhar do mouse sem o atraso da interpolação entre ticks, main.cpp): o giro que o mouse aplicou ao
// jogador 0 no último Player_Update, em radianos (yaw: o que entrou em obj_tag::rot.y; pitch: o que BLData::pitch
// x 90 graus andou, depois do Player_ViewClamping). count conta os Player_Update que passaram por ali.
struct PlayerMouseLook {
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint32_t count = 0;
};
extern PlayerMouseLook g_player_mouse_look;
// Só no port: se o Player_Update deste tick passaria o mouse ao olhar do jogador (vivo, ativo, fora de pausa, no
// estado 1 e com a câmera em primeira pessoa) e quais giros ele aceitaria (os testes de lá: o giro com o
// subestado que anda e sem trava, a elevação com o que mira), mais o que TakeMouseLook usaria (mirando, zoom).
bool Player_PortMouseLookGate(const obj_tag* obj, bool* yaw_ok, bool* pitch_ok, bool* aiming, float* zoom);

// Só para testes: estado inicial das globais do módulo.
void player_debug_power_on();
