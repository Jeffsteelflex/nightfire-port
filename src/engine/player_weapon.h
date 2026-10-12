#pragma once
// As armas do jogador (módulo Player do ACTION.ELF, 0x001a16c0-0x001a9900): o inventário
// (Player_InitWeapon, Player_EquipWeapon, munição), a arma na mão (um objeto animado só da câmera do
// jogador, com os braços e a arma: Player_SetWeaponAnim, a máquina de estados de
// Player_SetWeaponAnimObj, Player_PositionGun), mirar e o zoom (Player_Weapon, Player_Zoom,
// Player_AutoAim), trocar (Player_WeaponChange) e atirar (Player_WeaponFiring, que pede o tiro a
// Player_WeaponInitBullet, que cria as balas com Bullet_init).
//
// A arma na mão (BLData::gun) tem um estado em obj_tag::state:
//   0 parada (animação de espera), 1 guardando, 2 esperando guardar, 3 trocar o modelo e sacar,
//   4 sacando, 5 começando a recarregar, 6 recarregando, 7 terminando a recarga, 8 troca de variante,
//   9 atirando, 10 sacar ao contrário, 11 e 12 arremesso, 13 e 14 guardar e perder a arma,
//   15 mirando (entrando), 16 saindo da mira.

#include <cstdint>

#include "engine/object.h"
#include "engine/weapons.h"
#include "platform/ptr32.h"

struct ANIMSCRIPT_tag;
struct BLData;

// Definição de uma arma (0x10c bytes; weapon_data tem 115, preenchidas no boot pelos construtores
// estáticos do ACTION.ELF, engine/weapons.h). Campos identificados pelo uso; os scripts são hashes 06xxxxxx tocados no
// objeto da arma na mão.
struct weapon_definition_tag {
    int16_t id;                  // 0x00 o próprio índice
    int16_t klass;               // 0x02 a arma base da classe (o espaço em BLData::weapons)
    uint8_t selectable;          // 0x04 entra na troca de armas
    int8_t alt;                  // 0x05 variante: arma + alt (ex.: com silenciador)
    uint8_t unknown_06[2];
    float blast_radius;          // 0x08 raio da explosão do projétil (Explode_Create)
    float damage;                // 0x0c dano por projétil (vai para o acerto do alvo, HITDATA_tag::unknown_08)
    uint16_t stats;              // 0x10 bits 0xf8: conta nos tiros disparados (PlrStat_LogShotFired); classes do
                                 //      tiro que quebráveis e destrutíveis ignoram pela máscara (Collide_FilterBullets)
    uint8_t unknown_12[2];
    float autoaim;               // 0x14 mira automática da arma, em % do cone (Check_AutoAim)
    uint8_t bullets;             // 0x18 projéteis por disparo (0: não atira)
    uint8_t unknown_19[3];
    float range;                 // 0x1c alcance (o projétil some depois de percorrê-lo)
    float speed;                 // 0x20 velocidade do projétil em metros por tick a 60 Hz
    float spread;                // 0x24 dispersão: desvio de até spread x 0,0014 na direção
    int16_t burst[3];            // 0x28 tiros por rajada em cada modo de disparo
    uint8_t modes;               // 0x2e quantos modos de disparo
    uint8_t unknown_2f[0x11];
    uint32_t fire_delay;         // 0x40 ticks entre disparos (no mínimo 1)
    uint16_t fire_frame;         // 0x44 quadro da animação de tiro em que a bala sai (0: na hora)
    uint8_t unknown_46[2];
    uint32_t flash;              // 0x48 modelo do clarão no cano (0: sem clarão)
    uint8_t unknown_4c[5];
    uint8_t flash_alpha_min;     // 0x51 alfa do clarão, sorteado entre os dois
    uint8_t flash_alpha_max;     // 0x52
    uint8_t unknown_53;
    uint8_t light_b;             // 0x54 cor da luz do clarão
    uint8_t light_g;
    uint8_t light_r;
    uint8_t unknown_57;
    float light_range;           // 0x58
    uint32_t projectile;         // 0x5c modelo do projétil (entidade 02xxxxxx; 0 sem modelo)
    uint8_t unknown_60[8];
    // 0x68, pelo uso: 8 BLData::gun_flag8; 0x10 a variante tem munição própria; 0x40 luneta (mirar
    // esconde a arma); 0x100 recarrega bala a bala; 0x200 e 0x40000 alternam duas animações (recarga,
    // tiro); 0x400 tiro em duas fases (anim_fire_alt); 0x800 modelo em weapon_definition_tag::
    // datum_entity; 0x8000 segurar o gatilho não repete; 0x10000 a bala sai entre o olho e o cano;
    // 0x20000 lança um objeto (tipo 5) que volta a ser munição; 0x80000 sorteia anim_fire_alt;
    // 0x100000 tiro contínuo enquanto segura. Nos projéteis: 0x4000 o projétil explode quando
    // atingido, 0x40000 a cápsula sai do osso 5 (com alt_hand zerado), 0x400000 sem dispersão para
    // drones e para o jogador mirando.
    uint32_t flags;
    // 0x6c: 0x40 solta cartucho; 0x80 raio do taser; 0x100 raio laser; 0x400 tiro em duas fases;
    // 0x800 o raio da mira usa a máscara 0x208 em vez de 0x20a. Para os projéteis (Bullet_*,
    // engine/bullet.h): 2 fumaça, 4 míssil pilotado, 8 gravidade, 0x10/0x1000 traçante, 0x20 gás,
    // 0x100 modelo esticado, 0x200 acelera, 0x800 não atravessa vidro, 0x2000 luz, 0x4000 câmera
    // no projétil, 0x8000 gira, 0x10000 rastro, 0x20000 teleguiado, 0x40000 zunido, 0x80000 fumaça.
    uint32_t flags2;
    // 0x70, flags do impacto do projétil (Bullet_*, engine/bullet.h): 2/0x1000 efeito, 4 explode,
    // 0x10 ricocheteia, 0x40 gancho, 0x80 quica, 0x100 gruda, 0x200 pavio, 0x2000 granada de luz,
    // 0x10000 pavio de 300, 0x20000 granada de fumaça, 0x80000 laser de armadilha,
    // 0x100000/0x200000 explosões menores.
    uint32_t hit_flags;
    int16_t trail_period;        // 0x74 ticks entre segmentos do rastro (Swoosh)
    int16_t trail_width;         // 0x76
    uint8_t casing_delay;        // 0x78 ticks até a cápsula sair (Bullet_init_casing)
    uint8_t unknown_79[3];
    uint32_t datum_entity;       // 0x7c modelo preso ao osso 0 da arma (flag 0x800)
    uint8_t unknown_80[8];
    float zoom_max;              // 0x88
    float swing;                 // 0x8c balanço da câmera (Camera_ApplySwing)
    uint8_t ammo_type;           // 0x90 índice em ammo_data (0: não gasta)
    uint8_t rounds_per_shot;     // 0x91
    int16_t clip;                // 0x92 tamanho do pente
    uint8_t rumble;              // 0x94 vibração ao atirar
    uint8_t unknown_95[3];
    float spread_step;           // 0x98 dispersão a mais por tiro já dado na rajada (até clip tiros)
    uint8_t unknown_9c[4];
    uint32_t anim_idle;          // 0xa0
    uint32_t anim_reload;        // 0xa4
    uint32_t anim_reload_start;  // 0xa8
    uint32_t anim_reload_end;    // 0xac
    uint32_t anim_fire;          // 0xb0
    uint32_t anim_fire_alt;      // 0xb4 (também o arremesso e o pente vazio)
    uint32_t anim_aim_in;        // 0xb8
    uint32_t anim_aim_out;       // 0xbc
    uint32_t anim_draw;          // 0xc0 sacar
    uint32_t anim_holster;       // 0xc4 guardar
    uint32_t anim_switch;        // 0xc8 trocar de variante
    uint32_t anim_fidget;        // 0xcc espera
    uint32_t anim_look;          // 0xd0 espera: olhar a arma
    uint32_t anim_look_back;     // 0xd4
    uint32_t anim_holster_alt;   // 0xd8 guardar depois de olhar
    uint32_t skin;               // 0xdc pele (05xxxxxx) dos braços com a arma; 0: nada na mão
    float offset[2][3];          // 0xe0 posição na tela: modo história, multijogador (e agachado)
    float casing_vel[3];         // 0xf8 velocidade de saída da cápsula no espaço do osso (y negado)
    F32<void(obj_tag*)> on_fire;  // 0x104 depois de atirar sem munição a gastar (ex.: aparelhos)
    // 0x108 chamada quando o projétil acerta algo (normal e ponto do acerto); a bala é apagada.
    F32<void(obj_tag*, plane_equ_tag*, _VECTOR*)> on_hit;
};
static_assert(sizeof(weapon_definition_tag) == 0x10c);
static_assert(offsetof(weapon_definition_tag, flags) == 0x68 && offsetof(weapon_definition_tag, clip) == 0x92);
static_assert(offsetof(weapon_definition_tag, skin) == 0xdc && offsetof(weapon_definition_tag, on_fire) == 0x104);
static_assert(offsetof(weapon_definition_tag, damage) == 0x0c && offsetof(weapon_definition_tag, speed) == 0x20);
static_assert(offsetof(weapon_definition_tag, projectile) == 0x5c && offsetof(weapon_definition_tag, hit_flags) == 0x70);
static_assert(offsetof(weapon_definition_tag, casing_delay) == 0x78 && offsetof(weapon_definition_tag, spread_step) == 0x98);
static_assert(offsetof(weapon_definition_tag, casing_vel) == 0xf8 && offsetof(weapon_definition_tag, on_hit) == 0x108);

constexpr int kWeaponCount = 115;     // entradas em weapon_data
constexpr int kWeaponSlots = 114;     // BLData::weapons
constexpr int kAmmoTypes = 33;        // BLData::ammo
constexpr int8_t kWeaponNone = 0x47;  // sem nada na mão

weapon_definition_tag* weapon_def(int32_t weapon);
int16_t ammo_max(uint32_t type);

// Globais do módulo.
extern float MAX_AUTO_AIM_DIST_NEAR;
extern float MAX_AUTO_AIM_DIST_MIDL;
extern float Autoaim_Range;
extern float Autoaim_Angle_H;
extern float Autoaim_Angle_V;
extern float Autoaim_LockOnMul;
extern float Autoaim_EasyMul;
extern float Autoaim_NormalMul;
extern float Autoaim_HardMul;
extern obj_tag* Seconds;  // os ponteiros do relógio (Player_SetWatch)
extern obj_tag* Minutes;
extern obj_tag* Hours;
extern uint8_t CamHint, StickyHint;
extern uint8_t Upgrades[36];       // nível de cada melhoria (Upgrade_Weapon lê os bytes 0-5)
extern uint8_t RamSave[0xd50];     // o que passa de uma fase para a seguinte (Player_RamSave)
extern float FlashMod;

void Player_InitWeapon(BLData* b, obj_tag* obj, level_tag* level);
uint32_t Upgrade_Weapon(uint32_t weapon);
void Player_InitAmmoWeapons(obj_tag* obj);
uint32_t Player_EquipWeapon(BLData* b, int16_t weapon, int16_t rounds);
int32_t Player_EquipAmmo(BLData* b, int16_t weapon, int16_t rounds);
void Player_CheckWeaponsLoaded(BLData* b);
void Player_WeaponNone(obj_tag* obj);
void Player_RamLoad(BLData* b, int8_t boost);
void Player_RamSave(BLData* b);
void Player_SetWeaponAnim(obj_tag* obj);
void Player_SetWeaponAnimObj(obj_tag* obj, _VECTOR* sway);
void Player_PositionGun(obj_tag* obj, BLData* b);
void Player_Weapon(obj_tag* obj);
void Player_WeaponSelect(obj_tag* obj);
void Player_WeaponChange(obj_tag* obj, int16_t dir, uint8_t group);
// Só do port: o n-ésimo aparelho do jogador (1 a 5; os comandos gadget_1 ... gadget_5), na ordem do ciclo dos
// aparelhos, pelo mesmo caminho de Player_WeaponChange. Devolve se pediu a troca.
bool Player_PortSelectGadget(obj_tag* obj, int slot);
bool Player_WeaponHasAmmo(BLData* b, int16_t weapon);
int16_t Player_AmmoIndex(int16_t weapon);
void Player_GetBestWeapon(obj_tag* obj);
bool Player_IsBetterWeapon(obj_tag* obj, int16_t weapon);
void Player_WeaponRecoil(obj_tag* obj);
ANIMSCRIPT_tag* Player_SetFiringAnim(BLData* b, obj_tag* gun, weapon_definition_tag* def);
void Player_WeaponFiring(obj_tag* obj, int8_t force);
void Player_WeaponInitBullet(obj_tag* obj, weapon_definition_tag* def);
uint32_t Player_ReloadAmmoType(obj_tag* obj, int8_t test_only);
void Player_HandleHasNoAmmo(obj_tag* obj);
int16_t Player_AmmoInGun(BLData* b, int16_t weapon);
uint32_t Player_RoundToFire(obj_tag* obj, uint8_t need, uint8_t take);
void Player_GetAimingPoint(obj_tag* obj, _VECTOR* out);
void Player_ResetWalkAnim(obj_tag* obj);
void Player_Zoom(obj_tag* obj);
void Player_AutoAim(obj_tag* obj);
void Player_MuzzleFlash(obj_tag* obj, _VECTOR* target);
void Draw_MuzzleFlash(obj_tag* obj);

// Só para testes: estado inicial das globais do módulo.
void player_weapon_debug_power_on();
