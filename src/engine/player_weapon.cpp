// As armas do jogador. Ver player_weapon.h.
//
// Divergências:
// - Upgrade_Weapon: um nível de melhoria acima de 9 (os bytes de Upgrades vêm do jogo salvo) leria
//   além das tabelas Upgraded*; aqui a arma fica como está.
// - Nas contas da VU0 os temporários da pilha sem valor (o w da cabeça em Player_PositionGun, o da
//   posição da arma em Player_SetWeaponAnimObj) valem 0.
// - Só do port: com a mira moderna (engine/player_ads.h), Player_WeaponRecoil termina levando a arma para a pose de
//   mira (ads_tick).
// - Os drones (a lista em NPCGlobals + 4) ainda não existem: a lista está vazia e as animações de
//   espera da arma nunca são interrompidas por um inimigo em alerta.
// Só do port (os comandos do PC de platform/input_bindings.h, que o PS2 não tem):
// - Player_WeaponFiring: o comando "reload" (R) recarrega pelo caminho de recarga do botão de usar, sem usar nada
//   (Player_Activate não é chamada: nenhuma porta abre) e também mirando com luneta, onde o usar não recarrega.
// - Player_Weapon: "gadget_1" ... "gadget_5" (1 a 5) escolhem o n-ésimo aparelho do jogador (Player_PortSelectGadget).
// - Player_Zoom: a roda do mouse mirando com uma arma de zoom aproxima e afasta o alvo do zoom (psiInput_SetWheelZoom).

#include "engine/lights.h"
#include "engine/player_weapon.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/effect.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/player.h"
#include "engine/player_ads.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/input_bindings.h"
#include "platform/psi_input.h"
#include "platform/fmath.h"

float MAX_AUTO_AIM_DIST_NEAR = 12.0f;
float MAX_AUTO_AIM_DIST_MIDL = 18.0f;
float Autoaim_Range = 35.0f;
// Autoaim_Angle_H .. Autoaim_HardMul (0x0030cd4c, .sdata): os cones da mira automática (10 graus) e os fatores.
float Autoaim_Angle_H = 0.17453f;
float Autoaim_Angle_V = 0.17453f;
float Autoaim_LockOnMul = 2.0f;
float Autoaim_EasyMul = 1.25f;
float Autoaim_NormalMul = 1.0f;
float Autoaim_HardMul = 0.0f;
obj_tag* Seconds = nullptr;
obj_tag* Minutes = nullptr;
obj_tag* Hours = nullptr;
uint8_t CamHint = 0;
uint8_t StickyHint = 0;
uint8_t Upgrades[36];
uint8_t RamSave[0xd50];
float FlashMod = 0.0f;
float ContinueHealthBoostEasy = 50.0f;
float ContinueHealthBoostMedium = 50.0f;
float ContinueHealthBoostHard = 50.0f;

namespace {
// UpgradedHandguns, UpgradedLasers, UpgradedSnipers, UpgradedSilencedSnipers, UpgradedTasers,
// UpgradedPDAs, UpgradedDartGuns, UpgradedPPK_MP, UpgradedP99_MP, UpgradedGrapple_MP (0x002bee08,
// contíguas como no .data: a arma de cada nível de melhoria).
const uint32_t kUpgraded[40] = {
    0x02, 0x04, 0x06, 0x08, 0x4e, 0x4f, 0x4f, 0x4f, 0x1e, 0x20, 0x22, 0x22, 0x24, 0x26,
    0x28, 0x28, 0x4a, 0x4c, 0x4c, 0x4c, 0x56, 0x57, 0x57, 0x57, 0x43, 0x44, 0x44, 0x44,
};
constexpr int kUpHandguns = 0, kUpLasers = 4, kUpSnipers = 8, kUpSilenced = 12, kUpTasers = 16, kUpPDAs = 20,
              kUpDartGuns = 24;
// BestWeapon (0x002bede0): da melhor para a pior (Player_GetBestWeapon, Player_IsBetterWeapon).
const uint16_t kBestWeapon[18] = {66, 50, 26, 24, 28, 22, 21, 8, 16, 14, 13, 12, 18, 6, 4, 2, 10, 1};
// SatchelDigits e PdaDigits (0x002bed90, 0x002bedb8): os algarismos do visor.
const uint32_t kSatchelDigits[10] = {0x2000791, 0x2000792, 0x2000793, 0x2000794, 0x2000795,
                                     0x2000796, 0x2000797, 0x2000798, 0x2000799, 0x200079a};
const uint32_t kPdaDigits[10] = {0x2000a09, 0x2000a0a, 0x2000a0b, 0x2000a0c, 0x2000a0d,
                                 0x2000a0e, 0x2000a0f, 0x2000a10, 0x2000a11, 0x2000a12};

uint32_t mp_flag(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
uint32_t cheat(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, CheatInfo + offset, 4);
    return v;
}
BLData* bloke(obj_tag* obj) { return static_cast<BLData*>(obj->data); }
// A munição do tipo "type" (o original indexa sem limite a partir de BLData + 0x170).
int16_t& ammo_of(BLData* b, uint8_t type) {
    return *reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(b) + 0x170 + uint32_t(type) * 2);
}
// O espaço de BLData::weapons que guarda o pente da arma: o da classe se a variante gasta a mesma
// munição que ela, senão o da própria arma (Player_AmmoInGun, Player_RoundToFire...).
int32_t clip_slot(int32_t weapon) {
    const weapon_definition_tag* d = weapon_def(weapon);
    if (d->alt != 0 && d->ammo_type == weapon_def(d->klass)->ammo_type) return d->klass;
    return weapon;
}
// Zera o zoom da arma na mão: o guardado da classe se a opção estiver ligada e o jogador mirando,
// limitado ao máximo da arma (o mesmo bloco aparece em Player_Weapon e Player_SetWeaponAnimObj).
void reset_zoom(obj_tag* obj) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    const weapon_definition_tag* d = weapon_def(anim->weapon);
    b->zoom_target = 1.0f;
    if (PlayerSetting[b->player].keep_zoom != 0 && (anim->flags & 1) != 0) b->zoom_target = b->weapons[d->klass].zoom;
    const float max = d->zoom_max;
    b->zoom_max = max;
    if (max < b->zoom_target) b->zoom_target = max;
}
// Algum drone alerta (NPCGlobals + 4: lista de nós com +4 próximo, +0xc objeto, +0xac ativo,
// +0x50c alerta) — sem a IA portada a lista fica vazia.
bool drone_alert() {
    uint8_t* node;
    node = nf::p32_load<uint8_t>(&NPCGlobals.drones.first);
    while (node != nullptr) {
        float active, alert;
        obj_tag* o;
        std::memcpy(&active, node + 0xac, 4);
        o = nf::p32_load<obj_tag>(node + 0xc);
        std::memcpy(&alert, node + 0x50c, 4);
        if (0.0f < active && int16_t(o->state) != 1 && 0.5f < alert) return true;
        node = nf::p32_load<uint8_t>(node + 4);
    }
    return false;
}
_VECTOR& matrix_row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
// Linhas 0-2 de m vezes v (vmulax/vmadday/vmaddz .xyz); w = o de v.
_VECTOR rotate3(const MATRIX& m, const _VECTOR& v) {
    return {m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z, m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z, v.w};
}
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
}  // namespace

weapon_definition_tag* weapon_def(int32_t weapon) {
    return reinterpret_cast<weapon_definition_tag*>(weapon_data + weapon * 0x10c);
}

int16_t ammo_max(uint32_t type) {
    int16_t v;
    std::memcpy(&v, ammo_data + type * 0xc + 2, 2);
    return v;
}

// [decomp] 0x001a8f30 Upgrade_Weapon
// A arma que o jogador realmente recebe pelo nível de melhoria comprado (Upgrades). A 0x54 não muda,
// mas a melhoria dobra o zoom dela e da 0x55.
uint32_t Upgrade_Weapon(uint32_t weapon) {
    int table;
    uint8_t level;
    switch (weapon) {
        case 6:
            table = kUpHandguns;
            level = Upgrades[0];
            break;
        case 10:
            return GameState.unknown_52 != 0 ? 0xc : weapon;
        case 0x1e:
            table = kUpSnipers;
            level = Upgrades[3];
            break;
        case 0x24:
            table = kUpSilenced;
            level = Upgrades[3];
            break;
        case 0x43:
            table = kUpDartGuns;
            level = Upgrades[4];
            break;
        case 0x4a:
            table = kUpTasers;
            level = Upgrades[6];
            break;
        case 0x4e:
            table = kUpLasers;
            level = Upgrades[7];
            break;
        case 0x54: {
            const float zoom = Upgrades[2] == 0 ? 8.0f : 16.0f;
            weapon_def(0x54)->zoom_max = zoom;
            weapon_def(0x55)->zoom_max = zoom;
            return weapon;
        }
        case 0x56:
            table = kUpPDAs;
            level = Upgrades[5];
            break;
        default:
            return weapon;
    }
    if (40 <= table + level) return weapon;  // ver o cabeçalho
    return kUpgraded[table + level];
}

// [decomp] 0x001a17f8 Player_InitAmmoWeapons
// Esvazia o inventário (ou enche tudo com a trapaça de CheatInfo + 4): munição, pentes, modos e
// variantes, e o zoom guardado de cada arma na raiz do máximo.
void Player_InitAmmoWeapons(obj_tag* obj) {
    BLData* b = bloke(obj);
    for (uint32_t i = 0; i < kAmmoTypes; ++i) b->ammo[i] = cheat(4) == 0 ? int16_t(0) : ammo_max(i);
    for (int i = 0; i < kWeaponSlots; ++i) {
        PLRWEAPON_tag& w = b->weapons[i];
        const weapon_definition_tag* d = weapon_def(i);
        if (cheat(4) == 0) {
            w.have = 0;
            w.rounds = 0;
        } else {
            w.have = 1;
            w.rounds = d->clip;
        }
        if (d->selectable == 0) w.have = 0;
        w.mode = 0;
        w.variant = 0;
        w.zoom = std::sqrt(d->zoom_max);
    }
    b->last_weapon = 0;
    b->last_gadget = 0;
}

// [decomp] 0x001a8a58 Player_EquipWeapon
// Dá a arma (já melhorada) ao jogador: se ainda não tinha a classe, ela entra com o pente cheio e o
// que sobrar de "rounds" vai para a munição; se já tinha, só soma a munição (0 se já estava cheia ou
// rounds é 0). Põe na mão se for melhor e a opção estiver ligada. 1 se pegou.
uint32_t Player_EquipWeapon(BLData* b, int16_t weapon, int16_t rounds) {
    int32_t w = weapon;
    obj_tag* player = glb_players[b->player];
    weapon_definition_tag* d = weapon_def(w);
    if (d->skin == 0 || hashtable_getitem(d->skin) == nullptr) return 0;
    if (mp_flag(0x180) == 0) {
        w = int16_t(Upgrade_Weapon(uint32_t(w)));
        d = weapon_def(w);
        if (d->klass == 0x33) b->weapons[17].have = 0;
    }
    const uint16_t klass = uint16_t(d->klass);
    if (b->weapons[klass].have == 0) {
        b->weapons[klass].have = 1;
        int16_t& clip = b->weapons[int16_t(clip_slot(w))].rounds;
        const uint16_t old = uint16_t(clip);
        clip = d->clip;
        const int16_t extra = int16_t(int32_t(int16_t(rounds + old)) - int32_t(uint16_t(d->clip)));
        if (0 < extra) ammo_of(b, d->ammo_type) = int16_t(extra + ammo_of(b, d->ammo_type));
    } else {
        int16_t& a = ammo_of(b, d->ammo_type);
        if (ammo_max(d->ammo_type) <= a || rounds == 0) return 0;
        a = int16_t(rounds + a);
    }
    int16_t& a = ammo_of(b, d->ammo_type);
    const int16_t max = ammo_max(d->ammo_type);
    a = a < max ? a : max;
    if (PlayerSetting[b->player].auto_switch != 0 && Player_IsBetterWeapon(player, int16_t(w))) {
        anim_of(player)->next_weapon = int8_t(w);
    }
    if (w == 0x52) {
        b->weapons[83].have = 0;
        if (anim_of(player)->weapon == 0x53) anim_of(player)->next_weapon = 0x52;
    }
    return 1;
}

// [decomp] 0x001a8d18 Player_EquipAmmo
// Munição avulsa (0x1f e 0x25 são as balas dos rifles, pela melhoria). Armas de munição por variante
// (flag 0x10) com o pente vazio enchem o pente primeiro. Devolve quanto entrou na munição guardada.
int32_t Player_EquipAmmo(BLData* b, int16_t weapon, int16_t rounds) {
    int32_t w = weapon;
    int32_t n = rounds;
    int32_t loaded = 0;
    if (w == 0x1f) w = int16_t(Upgrade_Weapon(0x1e) + 1);
    if (w == 0x25) w = int16_t(Upgrade_Weapon(0x24) + 1);
    const weapon_definition_tag* d = weapon_def(w);
    const uint8_t type = d->ammo_type;
    if (!(ammo_of(b, type) < ammo_max(type) && n != 0)) return 0;
    if ((d->flags & 0x10) != 0) {
        const int16_t k = d->klass;
        const bool shared = d->alt != 0 && type == weapon_def(k)->ammo_type;
        if (b->weapons[int16_t(shared ? k : w)].rounds == 0) {
            const int16_t fill = n < d->clip ? int16_t(n) : d->clip;
            loaded = fill;
            if (shared) w = k;
            b->weapons[int16_t(w)].rounds = fill;
            n = int16_t(n - uint16_t(d->clip));
            if (n < 1) return loaded;
        }
    }
    int16_t& a = ammo_of(b, d->ammo_type);
    const uint16_t before = uint16_t(a);
    a = int16_t(int16_t(n) + before);
    const int16_t max = ammo_max(d->ammo_type);
    a = a < max ? a : max;
    return int16_t(uint16_t(a) - int16_t(loaded + before));
}

// [decomp] 0x001a97c8 Player_CheckWeaponsLoaded
// Tira do inventário as armas cujo modelo não está carregado nesta fase.
void Player_CheckWeaponsLoaded(BLData* b) {
    for (int i = 0; i < kWeaponSlots; ++i) {
        const weapon_definition_tag* d = weapon_def(i);
        if (d->selectable == 0) continue;
        if (d->skin == 0 || hashtable_getitem(d->skin) == nullptr) b->weapons[i].have = 0;
    }
}

// [decomp] 0x001a9858 Player_WeaponNone
// Nada na mão (arma 0x47), sem mirar nem zoom.
void Player_WeaponNone(obj_tag* obj) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->prev_weapon = anim->weapon;
    anim->weapon = kWeaponNone;
    anim->next_weapon = kWeaponNone;
    b->gun->state = 0;
    b->zoom = 1.0f;
    b->muzzle_time = 0;
    anim->flags &= 0xfffe;
}

// [decomp] 0x001957a8 Player_RamLoad
// Depois de um reinício quente (IsWarmReset), volta o que Player_RamSave guardou: munição, armas, o
// resto do bloco 0x70c, os itens, as estatísticas, os switches, a vida (com "boost", no mínimo
// ContinueHealthBoost* da dificuldade), BLData::armour e a arma guardada.
void Player_RamLoad(BLData* b, int8_t boost) {
    if (IsWarmReset == 0) return;
    std::memcpy(b->ammo, RamSave + 0x38, 0x42);
    std::memcpy(b->weapons, RamSave + 0x7c, 0x558);
    std::memcpy(b->unknown_70c, RamSave + 0xa1a, 0x33);
    SSys_RamLoad(RamSave + 0x5d4);
    std::memcpy(PlrMissionStats, RamSave, 0x38);
    std::memcpy(BondMoments, RamSave + 0xa04, 0x14);
    std::memcpy(switch_channels_time, RamSave + 0x604, 0x400);
    std::memcpy(switch_channels, RamSave + 0xa4d, 0x100);
    std::memcpy(switch_channels_hold, RamSave + 0xb4d, 0x100);
    std::memcpy(switch_channels_prev, RamSave + 0xc4d, 0x100);
    float health;
    std::memcpy(&health, RamSave + 0x5fc, 4);
    if (boost != 0) {
        float min = ContinueHealthBoostEasy;
        if (GameState.difficulty != 1) min = GameState.difficulty == 2 ? ContinueHealthBoostMedium : ContinueHealthBoostHard;
        if (health <= min) health = min;
        std::memcpy(RamSave + 0x5fc, &health, 4);
    }
    Player_SetHealth(b, health);
    std::memcpy(&b->armour, RamSave + 0x600, 4);
    std::memcpy(&b->saved_weapon, RamSave + 0xa18, 2);
}

// [decomp] 0x00195670 Player_RamSave
// Guarda o estado do jogador que passa para a próxima fase (Player_RamLoad).
void Player_RamSave(BLData* b) {
    std::memcpy(RamSave + 0x38, b->ammo, 0x42);
    std::memcpy(RamSave + 0x7c, b->weapons, 0x558);
    std::memcpy(RamSave + 0xa1a, b->unknown_70c, 0x33);
    std::memcpy(RamSave + 0x5d4, SSysItems, 0x28);
    std::memcpy(RamSave, PlrMissionStats, 0x38);
    std::memcpy(RamSave + 0xa04, BondMoments, 0x14);
    std::memcpy(RamSave + 0x604, switch_channels_time, 0x400);
    std::memcpy(RamSave + 0xa4d, switch_channels, 0x100);
    std::memcpy(RamSave + 0xb4d, switch_channels_hold, 0x100);
    std::memcpy(RamSave + 0xc4d, switch_channels_prev, 0x100);
    std::memcpy(RamSave + 0x5fc, &b->health, 4);
    std::memcpy(RamSave + 0x600, &b->armour, 4);
    const int16_t next = anim_of(glb_players[b->player])->next_weapon;
    std::memcpy(RamSave + 0xa18, &next, 2);
}

// [decomp] 0x001a7ef8 Player_InitWeapon
// Alcance da mira automática pela dificuldade; cria a arma na mão (sem modelo até Player_SetWeaponAnim),
// 32 objetos de efeito, o brilho e o objeto do clarão, todos só da câmera do jogador; e dá as armas da
// fase (em algumas fases, as que vieram da anterior por Player_RamLoad). A arma pedida (next_weapon)
// entra na mão pela máquina de estados de Player_SetWeaponAnimObj.
void Player_InitWeapon(BLData* b, obj_tag* obj, level_tag*) {
    if (mp_flag(0x180) == 0) {
        if (GameState.difficulty == 2) {
            MAX_AUTO_AIM_DIST_MIDL = 14.0f;
            MAX_AUTO_AIM_DIST_NEAR = 8.0f;
        } else if (GameState.difficulty != 3) {
            MAX_AUTO_AIM_DIST_MIDL = 18.0f;
            MAX_AUTO_AIM_DIST_NEAR = 12.0f;
        } else {
            MAX_AUTO_AIM_DIST_NEAR = 0.0f;
            MAX_AUTO_AIM_DIST_MIDL = 0.0f;
        }
    } else {
        MAX_AUTO_AIM_DIST_MIDL = 16.0f;
        MAX_AUTO_AIM_DIST_NEAR = 10.0f;
    }
    if (b == nullptr) return;
    b->aim_x = 0.0f;
    b->aim_y = 0.0f;
    b->aim_cur_x = 0.0f;
    b->aim_cur_y = 0.0f;
    b->aim_target = nullptr;
    b->unknown_132 = 0;
    obj_tag* gun = control_create_object(0, nullptr, nullptr, nullptr);
    Seconds = nullptr;
    Minutes = nullptr;
    Hours = nullptr;
    if (gun != nullptr) {
        gun->mode |= 4;
        Vec_Zero(&gun->pos);
        Vec_Zero(&gun->rot);
        View_SetDrawInNoViews(gun);
        gun->state = 0;
        gun->type = 0;
        gun->flags |= 0x2020;
        gun->owner = obj;
    }
    b->gun = gun;
    b->sway = 0.0f;
    b->recoil_x = 0.0f;
    b->recoil_y = 0.0f;
    b->sway2 = 0.0f;
    b->zoom_sound = 0;
    b->fire_timer = 0.0f;
    b->burst = 0;
    b->unknown_956 = 0;
    b->alt_hand = 0;
    const int8_t player = b->player;
    BLData* data = bloke(obj);
    for (int i = 0; i < 32; ++i) {
        obj_tag* o = control_create_object(0, nullptr, nullptr, nullptr);
        if (o == nullptr) continue;
        o->type = 1;
        o->owner = obj;
        o->scale = 1.0f;
        o->flags |= 0x20;
        o->mode |= 1;
        View_DrawInThisViewOnly(o, uint32_t(int32_t(player)));
        data->fx[i] = o;
    }
    obj_tag* glow = control_create_object(0, nullptr, nullptr, nullptr);
    if (glow != nullptr) {
        glow->type = 1;
        glow->owner = obj;
        glow->unknown_103[1] = 0;
        glow->unknown_103[2] = 0;
        glow->flags |= 0x20;
        glow->mode |= 2;
        glow->unknown_103[0] = 0x7f;
        hashtable_set_object_to_entity_gfx(glow, 0x2000132);
        View_DrawInThisViewOnly(glow, uint32_t(int32_t(b->player)));
        bloke(obj)->glow = glow;
    }
    obj_tag* muzzle = control_create_object(0, nullptr, nullptr, nullptr);
    if (muzzle != nullptr) {
        muzzle->owner = obj;
        muzzle->type = 1;
        muzzle->flags |= 0x8221;
        View_DrawInThisViewOnly(muzzle, uint32_t(int32_t(b->player)));
        bloke(obj)->muzzle_obj = muzzle;
        bloke(obj)->muzzle_time = 0;
    }
    CamHint = 0;
    StickyHint = 0;
    const int32_t handgun = int32_t(Upgrade_Weapon(6));
    const int16_t laser = int16_t(Upgrade_Weapon(0x4e));
    int16_t sniper = int16_t(Upgrade_Weapon(0x1e));
    Upgrade_Weapon(0x24);
    const int16_t taser = int16_t(Upgrade_Weapon(0x4a));
    const int16_t pda = int16_t(Upgrade_Weapon(0x56));
    const int16_t darts = int16_t(Upgrade_Weapon(0x43));
    Upgrade_Weapon(10);
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->prev_weapon = kWeaponNone;
    anim->weapon = kWeaponNone;
    b->look_state = 3;
    int16_t last = VIDEO_FRAME_RATE == 50 ? 0x59 : 0x5b;
    if (mp_flag(0x180) != 0) {
        MP_EquipPlayer(obj);
        Player_CheckWeaponsLoaded(b);
        return;
    }
    const int16_t hg = int16_t(handgun);
    const auto restored = [&]() {
        // As fases que continuam a anterior: o inventário dela, se a pistola veio junto.
        Player_RamLoad(b, 0);
        anim->next_weapon = int8_t(b->saved_weapon);
        return b->weapons[handgun].have != 0;
    };
    // O fim comum de vários casos (os rótulos 0x001a8980/0x001a8984 do original).
    const auto finish = [&](int16_t weapon, int16_t rounds) {
        Player_EquipWeapon(b, weapon, rounds);
        anim->next_weapon = int8_t(handgun);
    };
    const auto start_01 = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, taser, 999);
        finish(last, 999);
    };
    const auto start_05 = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, taser, 999);
        Player_EquipWeapon(b, 0x50, 0);
        finish(0x54, 0);
    };
    const auto start_07 = [&]() {
        Player_RamLoad(b, 0);
        Player_InitAmmoWeapons(obj);
        b->armour = 0.0f;
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, taser, 999);
        Player_EquipWeapon(b, 0x50, 0);
        Player_EquipWeapon(b, 0x54, 0);
        anim->next_weapon = 1;
    };
    const auto start_09 = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, taser, 999);
        Player_EquipWeapon(b, pda, 0);
        Player_EquipWeapon(b, 0x58, 0);
        Player_EquipWeapon(b, 0x50, 0);
        Player_EquipWeapon(b, darts, 999);
        Player_EquipWeapon(b, last, 999);
        anim->next_weapon = int8_t(darts);
    };
    const auto start_0c = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, sniper, 999);
        Player_EquipWeapon(b, taser, 999);
        Player_EquipWeapon(b, 0x50, 0);
        Player_EquipWeapon(b, 0x54, 0);
        Player_EquipWeapon(b, 0x58, 0);
        anim->next_weapon = int8_t(sniper);
    };
    const auto start_11 = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, 0x10, 7);
        Player_EquipWeapon(b, 0x50, 0);
        anim->next_weapon = 0x10;
    };
    const auto start_14 = [&]() {
        Player_InitAmmoWeapons(obj);
        Player_EquipWeapon(b, hg, 0x30);
        Player_EquipWeapon(b, 0x11, 0xc);
        Player_EquipWeapon(b, 0x34, 5);
        Player_EquipWeapon(b, 0x37, 5);
        Player_EquipWeapon(b, taser, 999);
        Player_EquipWeapon(b, 0x50, 0);
        Player_EquipWeapon(b, 0x54, 0);
        Player_EquipWeapon(b, pda, 0);
        anim->next_weapon = 0x11;
    };
    switch (GameState.level_file) {
        case 0x7000001:
            start_01();
            break;
        case 0x7000002:
        case 0x7000003:
        case 0x7000004:
            if (!restored()) start_01();
            break;
        case 0x7000005:
            start_05();
            break;
        case 0x7000006:
            if (!restored()) start_05();
            break;
        case 0x7000007:
            start_07();
            break;
        case 0x7000008:
            if (!restored()) start_07();
            break;
        case 0x7000009:
            start_09();
            break;
        case 0x700000a:
        case 0x700000b:
            if (!restored()) start_09();
            break;
        case 0x700000c:
            start_0c();
            break;
        case 0x700000d:
            if (!restored()) start_0c();
            break;
        case 0x7000011:
            start_11();
            break;
        case 0x7000012:
        case 0x7000013:
        case 0x700004a:
            Player_RamLoad(b, 0);
            anim->next_weapon = int8_t(b->saved_weapon);
            if (b->weapons[16].have == 0) start_11();
            break;
        case 0x7000014:
            start_14();
            break;
        case 0x7000015:
        case 0x7000016:
        case 0x7000017:
            if (!restored()) start_14();
            break;
        case 0x7000018:
        case 0x7000019:
        case 0x700001a:
        case 0x700001b:
            Player_InitAmmoWeapons(obj);
            Player_EquipWeapon(b, 0x33, 999);
            anim->next_weapon = 0x33;
            b->armour = 50.0f;
            break;
        default: {
            // Fases sem regra própria (testes): quase todas as armas.
            Player_InitAmmoWeapons(obj);
            static const int16_t kAll[] = {6,    0x12, 0x18, 0x46, 0x37, 0x36, 0x35, 0x34, 0x3a, 0x52,
                                           0x2c, 0x2e, 0x30, 0x4a, 0x50, 0x1e};
            for (int16_t w : kAll) Player_EquipWeapon(b, w, w == 0x50 ? int16_t(0) : int16_t(999));
            Player_EquipAmmo(b, 0x1f, 10);
            Player_EquipWeapon(b, 0x24, 999);
            Player_EquipAmmo(b, 0x25, 999);
            Player_EquipWeapon(b, 0xe, 999);
            Player_EquipWeapon(b, 0x10, 999);
            Player_EquipWeapon(b, 0x54, 0);
            Player_EquipWeapon(b, 0x1c, 999);
            Player_EquipWeapon(b, 0x15, 999);
            Player_EquipWeapon(b, 10, 999);
            Player_EquipWeapon(b, 0xd, 999);
            Player_EquipWeapon(b, 0x56, 0);
            Player_EquipWeapon(b, 0x58, 0);
            Player_EquipWeapon(b, 0x41, 999);
            Player_EquipWeapon(b, 0x59, 999);
            Player_EquipWeapon(b, 0x3b, 999);
            Player_EquipWeapon(b, 0x1a, 999);
            Player_EquipAmmo(b, 0x1b, 999);
            static const int16_t kRest[] = {0x33, 0x11, 2, 0x2a, 0x42, 0x43, 0x45};
            for (int16_t w : kRest) Player_EquipWeapon(b, w, 999);
            finish(0x16, 999);
            break;
        }
    }
    Player_EquipWeapon(b, laser, 999);
    Player_EquipWeapon(b, 1, 0);
    Player_CheckWeaponsLoaded(b);
    if (b->weapons[weapon_def(anim->next_weapon)->klass].have == 0) anim->next_weapon = 1;
    Player_RamSave(b);
    Player_CheckWeaponsLoaded(b);
}

// [decomp] 0x001a9338 Player_SetWeaponAnim
// Troca o modelo da arma na mão pelo da arma atual (a pele com os braços e a manga do jogador) e
// refaz o conjunto de animações do corpo.
void Player_SetWeaponAnim(obj_tag* obj) {
    const weapon_definition_tag* d = weapon_def(anim_of(obj)->weapon);
    BLData* b = bloke(obj);
    if (d->skin == 0) {
        View_SetDrawInNoViews(b->gun);
    } else {
        AnimObjectDelete(b->gun);
        AnimObjectNew(b->gun, d->skin);
        AnimObjectSetSleeveType(b->gun, uint8_t(b->punch_skin));
        View_SetDrawInNoViews(b->gun);
        b->gun_flag8 = (d->flags & 8) == 0 ? 0 : 1;
    }
    PlayerAnimSetInit(obj);
    b->gun->flags |= 0x1a0a0;
}

// [decomp] 0x001a2e38 Player_SetWeaponAnimObj
// A máquina de estados da arma na mão (ver player_weapon.h): animações de espera depois de 20 s
// parado, guardar e sacar ao trocar, recarregar, tirar o pino... E a posição da arma na tela: a da
// definição (entre a de pé e a agachada), mais o balanço "sway", compensando o deslocamento fixo de
// Player_PositionGun.
void Player_SetWeaponAnimObj(obj_tag* obj, _VECTOR* sway) {
    ANIMOBJ_tag* anim = anim_of(obj);
    BLData* b = bloke(obj);
    weapon_definition_tag* prev = weapon_def(anim->prev_weapon);
    weapon_definition_tag* cur = weapon_def(anim->weapon);
    const int8_t player = b->player;
    const int8_t idle_before = b->idle_state;
    const uint32_t mp = mp_flag(0x180) != 0 ? 1u : 0u;
    const int16_t idle_delay = int16_t(int16_t(FRAME_RATE_INT) * 20);
    weapon_definition_tag* shown = cur;
    if (b->gun->state == 0) {
        if (PlayerSetting[player].any == 0) {
            ++b->idle_ticks;
        } else {
            b->idle_ticks = 0;
        }
        if (0 < b->idle_wait) b->idle_wait = int16_t(b->idle_wait - 1);
    } else {
        b->idle_state = 0;
        b->idle_ticks = 0;
        b->idle_wait = idle_delay;
    }
    obj_tag* gun = b->gun;
    switch (gun->state) {
        default: {
            const int8_t idle = b->idle_state;
            if (idle == 1) {
                if (AnimScriptIsStopped(gun)) {
                    if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                    b->idle_ticks = 0;
                    b->idle_state = 0;
                }
            } else if (idle == 0) {
                uint32_t look = cur->anim_look;
                bool done = false;
                if (cur->anim_fidget != 0 && idle_delay <= b->idle_ticks) {
                    AnimScriptAdd(gun, cur->anim_fidget);
                    b->idle_state = 1;
                    done = true;
                }
                if (!done && look != 0 && b->idle_wait == 0 && (anim->flags & 1) == 0 && b->aim_cur_x == 0.0f &&
                    b->aim_cur_y == 0.0f && !drone_alert()) {
                    AnimScriptAdd(gun, cur->anim_look);
                    b->idle_state = 3;
                }
            } else if (idle == 3) {
                if (AnimScriptIsStopped(gun)) {
                    const bool alert = drone_alert();
                    // Parado, sem mirar e sem alerta, fica no último quadro de "olhar".
                    if (!((anim->flags & 1) == 0 && !alert && b->aim_cur_x == 0.0f && b->aim_cur_y == 0.0f)) {
                        if (cur->anim_look_back != 0) AnimScriptAddSpeed(gun, cur->anim_look_back, 1.25f);
                        b->idle_state = 1;
                        b->idle_ticks = 0;
                        b->idle_wait = idle_delay;
                    }
                }
            }
            if (anim->next_weapon != anim->weapon) {
                anim->prev_weapon = anim->weapon;
                Player_WeaponSelect(obj);
            }
            break;
        }
        case 1:
        case 0xd:
            b->zoom_target = 1.0f;
            if (prev->anim_holster != 0) {
                if (idle_before == 3 && prev->anim_holster_alt != 0) {
                    AnimScriptAdd(gun, prev->anim_holster_alt);
                } else {
                    AnimScriptAddSpeed(gun, prev->anim_holster, 1.25f);
                }
                Sound_Play(uint16_t(uint16_t(prev->id) - 0x4a) < 0x15 ? 0x280 : 0x283, 100.0f, 0, 0);
            }
            if (gun->state == 0xd) gun->state = 0xe;
            shown = prev;
            if (gun->state == 1) gun->state = 2;
            break;
        case 2:
        case 0xe:
            shown = prev;
            if (AnimScriptIsStopped(gun)) {
                if (gun->state == 0xe && mp_flag(0x180) == 0) b->weapons[anim->prev_weapon].have = 0;
                if (anim->next_weapon != anim->weapon) {
                    anim->weapon = anim->next_weapon;
                    if (mp_flag(0x184) != 0) MP_sendTeamBotMessage(2, 0x44, anim->next_weapon, 0, obj);
                }
                gun->state = 3;
            }
            break;
        case 3:
            Player_SetWeaponAnim(obj);
            if (cur->anim_draw == 0) {
                if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                gun->state = 0;
            } else {
                AnimScriptAddSpeed(gun, cur->anim_draw, 1.25f);
                AnimObjectUpdate(gun);
                gun->state = 4;
                Sound_Play(uint16_t(uint16_t(cur->id) - 0x4a) < 0x15 ? 0x281 : 0x282, 100.0f, 0, 0);
            }
            break;
        case 4:
            if (!AnimScriptIsStopped(gun)) {
                if (anim->next_weapon != anim->weapon) {
                    anim->prev_weapon = anim->weapon;
                    AnimScriptSetPlayDirection(gun, 2);
                    gun->state = 2;
                }
            } else {
                if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                b->fire_timer = 0.0f;
                gun->state = 0;
                reset_zoom(obj);
            }
            break;
        case 5:
            if (AnimScriptIsStopped(gun)) {
                AnimScriptAdd(gun, cur->anim_reload);
                gun->state = 6;
            }
            break;
        case 6:
            if (cur->anim_reload == 0) {
                Player_ReloadAmmoType(obj, 0);
                gun->state = 0;
            } else if (AnimScriptIsStopped(gun)) {
                Player_ReloadAmmoType(obj, 0);
                if ((cur->flags & 0x100) == 0) {
                    if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                    gun->state = 0;
                    Input_ClearAction(player, kActFire);
                    break;
                }
                // Bala a bala: continua enquanto houver munição e o gatilho estiver solto.
                if (Player_ReloadAmmoType(obj, 1) != 0 && Input_Action(player, kActFire, 1) == 0) {
                    AnimScriptAdd(gun, cur->anim_reload);
                    break;
                }
                AnimScriptAdd(gun, cur->anim_reload_end);
                gun->state = 7;
                b->alt_hand = 0;
            }
            break;
        case 7:
            if (AnimScriptIsStopped(gun)) {
                if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                gun->state = 0;
                Input_ClearAction(player, kActFire);
            }
            break;
        case 8:
            if (cur->skin == 0) {
                gun->state = 0;
            } else if (AnimScriptIsStopped(gun)) {
                gun->state = 0;
                if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
            }
            break;
        case 9: {
            if (cur->fire_frame != 0) {
                float frame = 0.0f;
                ANIMSCRIPT_tag* s = AnimListBuild(gun, 4, 0, nullptr, 1);
                if (s != nullptr) frame = s->frame;
                if (float(cur->fire_frame) <= frame && b->bullet_out == 0) {
                    b->bullet_out = 1;
                    Player_WeaponInitBullet(obj, cur);
                }
            }
            const uint32_t flags = cur->flags;
            if ((flags & 0x400) == 0) {
                if ((flags & 0x100000) != 0 && Input_Action(player, kActFire, 1) != 0 && AnimScriptIsStopped(gun)) {
                    Player_SetFiringAnim(b, gun, cur);
                }
                if (cur->skin == 0) {
                    gun->state = 0;
                } else if (AnimScriptIsStopped(gun)) {
                    gun->state = 0;
                    // Sem testar se o script existe (o original pula o teste de anim_idle).
                    if ((weapon_def(uint16_t(cur->klass))->flags & 0x1000) == 0) AnimScriptAdd(gun, cur->anim_idle);
                }
            } else {
                if (uint16_t(uint16_t(cur->klass) - 0x50) < 2) {
                    if (int16_t(obj->unknown_f6) == 2 && b->unknown_15c == 1) break;
                    if (b->move_locked != 0) break;
                } else if (Input_Action(player, kActFire, 1) != 0 && Player_WeaponHasAmmo(b, cur->id)) {
                    break;
                }
                if (AnimScriptIsStopped(gun)) {
                    gun->state = 8;
                    AnimScriptAdd(gun, cur->anim_fire_alt);
                    b->idle_state = 1;
                }
            }
            break;
        }
        case 10:
            if (cur->anim_draw == 0) {
                if (cur->anim_idle != 0) AnimScriptAdd(gun, cur->anim_idle);
                gun->state = 0;
            } else {
                AnimScriptSetPlayDirection(gun, 2);
                gun->state = 2;
            }
            break;
        case 0xb:
            b->zoom_target = 1.0f;
            if (AnimScriptIsStopped(gun)) {
                gun->state = 0xc;
                AnimScriptAdd(gun, cur->anim_fire_alt);
            }
            break;
        case 0xc: {
            b->zoom_target = 1.0f;
            float frame = 0.0f;
            ANIMSCRIPT_tag* s = AnimListBuild(gun, 4, 0, nullptr, 1);
            if (s != nullptr) frame = s->frame;
            if (float(cur->fire_frame) <= frame && b->bullet_out == 0) {
                b->bullet_out = 1;
                Player_WeaponInitBullet(obj, cur);
            }
            if (AnimScriptIsStopped(gun)) {
                if (Player_ReloadAmmoType(obj, 1) == 0 && cheat(8) == 0) {
                    gun->state = 0;
                    Player_HandleHasNoAmmo(obj);
                    if (anim->next_weapon != anim->weapon) Player_WeaponSelect(obj);
                    gun->state = 3;
                    if (anim->next_weapon == 0x39) {
                        anim->weapon = anim->next_weapon;
                        gun->state = 0;
                        b->fire_timer = 0.0f;
                    }
                    break;
                }
                gun->state = 3;
            }
            break;
        }
        case 0xf:
            if (AnimScriptIsStopped(gun)) {
                anim->flags |= 1;
                gun->state = 0;
                b->idle_state = 1;
            }
            break;
        case 0x10:
            if (AnimScriptIsStopped(gun)) {
                gun->state = 0;
                b->idle_state = 1;
            }
            break;
    }
    float x = shown->offset[mp][0];
    float y = shown->offset[mp][1];
    float z = shown->offset[mp][2];
    if (mp == 0 && 0.0f < b->crouch_drop) {
        const float t = b->crouch_drop * 2.2222223f;
        const float u = 1.0f - t;
        x = x * u + shown->offset[1][0] * t;
        y = y * u + shown->offset[1][1] * t;
        z = z * u + shown->offset[1][2] * t;
    }
    gun = b->gun;
    gun->pos = {x, y, z, 0.0f};
    gun->pos.x += sway->x;
    gun->pos.y += sway->y;
    gun->pos.z += sway->z;
    gun->pos.y -= -0.2f;
    gun->pos.z -= 0.5f;
}

// [decomp] 0x001a3b40 Player_PositionGun
// A matriz da arma na mão: a rotação dela (rot) sobre a do jogador inclinada pelo pitch, na posição
// dos olhos mais pos (na rotação da arma) mais (0, -0,2, 0,5) na rotação do olhar.
void Player_PositionGun(obj_tag* obj, BLData* b) {
    obj_tag* gun = b->gun;
    gun->mode |= 0x20;
    MATRIX view;
    RotMatrixX(-(b->pitch * 1.5707964f), &view);
    Concat(&obj->matrix, &view);
    MATRIX look;
    Mat_Copy(&view, &look);
    RotMatrixZYX(&gun->rot, &gun->matrix);
    Concat(&view, &gun->matrix);
    const _VECTOR at = rotate3(gun->matrix, gun->pos);
    _VECTOR head{0.0f, 0.0f, 0.0f, 0.0f};
    Player_GetHeadPos(&head, obj, nullptr);
    matrix_row3(gun->matrix) = add4(head, at);
    const _VECTOR offset = rotate3(look, _VECTOR{0.0f, -0.2f, 0.5f, head.w});
    matrix_row3(gun->matrix) = add4(matrix_row3(gun->matrix), offset);
}

// [decomp] 0x001a3de8 Player_WeaponRecoil
// O balanço da arma: ao andar (a fase anda com a velocidade para a frente), um lento sempre, e o
// coice que decai; a mira fora do centro gira a arma para lá.
void Player_WeaponRecoil(obj_tag* obj) {
    BLData* b = bloke(obj);
    _VECTOR sway{0.0f, 0.0f, 0.0f, 0.0f};
    constexpr float kHalfPi = 1.5707964f;
    b->sway = b->sway + b->prev_vel.z;
    sway.x += PS2Sinf(b->sway) * 0.01f;
    sway.y += std::fabs(PS2Sinf(b->sway + kHalfPi)) * 0.01f;
    sway.z += PS2Sinf(b->sway * 0.84329f) * 0.02f;
    const float phase = b->sway2 + kHalfPi / FRAME_RATE;
    b->recoil_x = b->recoil_x * 0.75f;
    b->recoil_y = b->recoil_y * 0.75f;
    b->sway2 = phase;
    sway.x += PS2Sinf(phase) * 0.001f;
    sway.y += PS2Sinf(b->sway2 * 1.7f + kHalfPi) * 0.001f;
    BLData* d = bloke(obj);
    viewer_tag* v = glb_viewer[d->player];
    d->gun->rot.x = (d->aim_cur_y * -0.5235988f) / v->lod_scale;
    d->gun->rot.y = v->aspect * -((d->aim_cur_x * 0.5235988f) / v->lod_scale) + d->recoil_y;
    d->gun->rot.z = 0.0f;
    Player_SetWeaponAnimObj(obj, &sway);
    // Só do port: a pose de mira moderna (engine/player_ads.h) por cima da do jogo.
    ads_tick(obj, &sway);
}

// [decomp] 0x001a24c8 Player_Weapon
// Os controles da arma: mirar (segurar ou alternar a ação 19; a luneta esconde a arma), a animação
// de entrar e sair da mira, trocar de variante ou de modo de disparo (ação 12), trocar de arma
// (ações 15/16) e de aparelho (10/11), o zoom e a mira automática.
void Player_Weapon(obj_tag* obj) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    weapon_definition_tag* cur = weapon_def(anim->weapon);
    obj->mode |= 0x20;
    const int16_t s = int16_t(obj->unknown_f6);
    const int8_t player = b->player;
    bool aim_input = true;
    if (s == kPlrClimb) {
        int16_t ladder_kind;
        std::memcpy(&ladder_kind, b->ladder->data, 2);
        aim_input = ladder_kind == 0;
    } else if (s == 6 || s == 7) {
        aim_input = false;
    }
    if (aim_input) {
        const uint8_t was = uint8_t(anim->flags);
        if (PlayerSetting[player].aim_toggle == 0) {
            anim->flags &= 0xfffe;
            if (Input_Action(player, kActAimMode, kActionDown) != 0) anim->flags |= 1;
        } else if (Input_Action(player, kActAimMode, kActionPressed) != 0) {
            anim->flags ^= 1;
        }
        const uint32_t flags = cur->flags;
        const uint16_t state = b->gun->state;
        bool drop;
        if ((flags & 0x40) == 0) {
            drop = state == 1 || state == 2 || state == 3 || state == 4 || state == 10 || state == 13 || state == 14 ||
                   state == 15;
        } else {
            drop = (1 <= state && state <= 4) || (6 <= state && state <= 8) || state == 10 ||
                   (13 <= state && state <= 16) || (state == 9 && (flags & 0x8000) != 0 && (was & 1) == 0);
        }
        if (drop) anim->flags &= 0xfffe;
        const uint16_t f = anim->flags;
        if ((f & 1) != (was & 1) && cur->anim_aim_in != 0) {
            if ((f & 1) == 0) {
                if (b->gun->state == 0) {
                    b->gun->state = 0x10;
                    if (cur->id == 0x55) {
                        anim->weapon = 0x54;
                        anim->next_weapon = 0x54;
                        cur = weapon_def(anim->weapon);
                    }
                    if (cur->anim_aim_out == 0) {
                        AnimScriptAddReverse(b->gun, cur->anim_aim_in);
                    } else {
                        AnimScriptAddSpeed(b->gun, cur->anim_aim_out, 1.25f);
                    }
                }
            } else {
                // A mira só liga no fim da animação de entrar (estado 15).
                anim->flags = uint16_t(f & 0xfffe);
                b->gun->state = 0xf;
                AnimScriptAddSpeed(b->gun, cur->anim_aim_in, 1.25f);
            }
        }
    }
    const uint16_t f = anim->flags;
    if ((f & 1) != ((f >> 1) & 1)) {
        anim->flags = uint16_t(f ^ 2);
        if ((f & 1) == 0) {
            b->look_state = 1;
            b->zoom_target = 1.0f;
            Player_ResetWalkAnim(obj);
            if (b->zoom_sound != 0) {
                Sound_Stop(b->zoom_sound);
                b->zoom_sound = 0;
            }
        } else {
            reset_zoom(obj);
            b->look_state = 0;
            Player_ClearAimInertia(obj);
        }
    }
    uint8_t select = uint8_t(Input_Action(player, 12, kActionPressed));
    if (uint16_t(uint16_t(cur->id) - 0x54) < 2) select |= uint8_t(Input_Action(player, kActFire, kActionPressed));
    if ((anim->flags & 1) != 0 && (weapon_def(anim->weapon)->flags & 0x40) != 0) select = 0;
    if (select != 0 && b->gun->state == 0) {
        if (cur->alt == 0) {
            if (cur->modes != 1) {
                PLRWEAPON_tag& w = b->weapons[anim->weapon];
                ++w.mode;
                if (w.mode == cur->modes) w.mode = 0;
            }
        } else {
            const int16_t v = int16_t(anim->weapon + cur->alt);
            if (Player_WeaponHasAmmo(b, v)) {
                anim->next_weapon = int8_t(v);
                reset_zoom(obj);
            }
        }
    }
    if (Input_Action(player, 11, kActionPressed) != 0) {
        Player_WeaponChange(obj, 1, 1);
    } else if (Input_Action(player, 10, kActionPressed) != 0) {
        Player_WeaponChange(obj, -1, 1);
    }
    // Só do port: os espaços dos aparelhos (gadget_1 ... gadget_5, teclas 1 a 5).
    for (int n = 0; n < nf::kPortGadgetSlots; ++n) {
        if (psiInput_PortPressed(player, nf::kPortGadget1 + n)) {
            Player_PortSelectGadget(obj, n + 1);
            break;
        }
    }
    bool normal = true;
    if (GameState.level_file == 0x7000007) {
        if (switch_channels[0x48] == 0) {
            normal = false;
        } else if (switch_channels_prev[0x48] == 0) {
            anim->next_weapon = int8_t(Upgrade_Weapon(6));
        }
    }
    const uint8_t group = normal ? 0 : 2;
    if (Input_Action(player, 16, kActionPressed) != 0) {
        Player_WeaponChange(obj, 1, group);
    } else if (Input_Action(player, 15, kActionPressed) != 0) {
        Player_WeaponChange(obj, -1, group);
    }
    Player_Zoom(obj);
    Player_AutoAim(obj);
}

// [decomp] 0x001a2cc0 Player_WeaponSelect
// Começa a troca para next_weapon: na mesma classe e com o mesmo modelo, só a animação de variante
// (estado 8); senão guarda a arma (estado 1) ou, sem animação de guardar, troca direto (estado 3).
void Player_WeaponSelect(obj_tag* obj) {
    ANIMOBJ_tag* anim = anim_of(obj);
    BLData* b = bloke(obj);
    const weapon_definition_tag* next = weapon_def(anim->next_weapon);
    if (uint16_t(weapon_def(anim->weapon)->klass) == uint16_t(next->klass)) {
        b->weapons[uint16_t(next->klass)].variant = int8_t(anim->next_weapon - int8_t(next->klass));
        if (weapon_def(anim->weapon)->skin == weapon_def(anim->next_weapon)->skin) {
            anim->weapon = anim->next_weapon;
            AnimScriptAddSpeed(b->gun, weapon_def(anim->weapon)->anim_switch, 1.3f);
            b->gun->state = 8;
            const weapon_definition_tag* d = weapon_def(anim->weapon);
            b->weapons[uint16_t(d->klass)].variant = int8_t(anim->weapon - int8_t(d->klass));
            return;
        }
    }
    if (weapon_def(anim->prev_weapon)->anim_holster == 0) {
        anim->weapon = anim->next_weapon;
        b->gun->state = 3;
    } else {
        b->gun->state = 1;
    }
}

// [decomp] 0x001a6f80 Player_WeaponChange
// Pede a próxima arma (dir 1) ou a anterior (-1) do grupo: 0 as armas (1 a 0x47), 1 os aparelhos
// (0x4a a 0x5e; com uma arma na mão volta ao último aparelho), 2 só a 1. Pula as que o jogador não
// tem ou sem munição, lembrando a variante de cada uma.
void Player_WeaponChange(obj_tag* obj, int16_t dir, uint8_t group) {
    BLData* b = bloke(obj);
    const int16_t sub = int16_t(obj->unknown_f6);
    if (sub == 2 || sub == 10 || sub == 12 || sub == 13 || sub == 16) return;
    const uint16_t gs = b->gun->state;
    if (gs == 1 || gs == 3 || (5 <= gs && gs <= 9) || gs == 11 || (13 <= gs && gs <= 16)) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    const int8_t w = anim->weapon;
    if (w == kWeaponNone) return;
    if (kWeaponNone < w && w < 0x5f && 0x5c < w) return;
    if ((anim->flags & 1) != 0) return;
    // Volta à arma (ou ao aparelho) usada por último, se ainda servir.
    const auto usable = [&](int16_t last) {
        const int16_t k = weapon_def(last)->klass;
        return weapon_def(k)->selectable != 0 && b->weapons[k].have != 0 && Player_WeaponHasAmmo(b, k);
    };
    const auto is_weapon = [](int8_t x) { return uint32_t(uint8_t(x)) - 1u < 0x47u; };
    const auto is_gadget = [](int8_t x) { return uint32_t(uint8_t(x)) - 0x4au < 0x15u; };
    int32_t lo = 0x49;
    int32_t hi;
    if (group == 1) {
        hi = 0x5f;
        if (is_weapon(anim->weapon)) {
            if (b->last_gadget != 0 && usable(b->last_gadget)) {
                anim->next_weapon = int8_t(b->last_gadget);
                b->last_weapon = anim->weapon;
                return;
            }
            if (is_weapon(anim->weapon)) b->last_weapon = anim->weapon;
        }
    } else {
        if (group == 0) {
            lo = 0;
            hi = 0x48;
            if (is_gadget(anim->weapon) && b->last_weapon != 0 && usable(b->last_weapon)) {
                anim->next_weapon = int8_t(b->last_weapon);
                b->last_gadget = anim->weapon;
                if (anim->weapon == 0x55) b->last_gadget = 0x54;
                return;
            }
        } else {
            lo = 0;
            hi = 2;
            if (anim->weapon == 1) Sound_Play(0x1db, 100.0f, 0, 0);
        }
        if (is_weapon(anim->weapon)) b->last_weapon = anim->weapon;
    }
    if (is_gadget(anim->weapon)) b->last_gadget = anim->weapon;
    anim->prev_weapon = anim->weapon;
    if (anim->next_weapon < lo || hi < anim->next_weapon) anim->next_weapon = int8_t(lo);
    anim->next_weapon = int8_t(weapon_def(anim->next_weapon)->klass);
    for (int32_t i = 1;; ++i) {
        anim->next_weapon = int8_t(anim->next_weapon + int8_t(dir));
        if (hi <= anim->next_weapon) anim->next_weapon = int8_t(lo + 1);
        if (anim->next_weapon <= lo) anim->next_weapon = int8_t(hi - 1);
        if (0x72 < i) break;
        const int8_t c = anim->next_weapon;
        if (weapon_def(c)->selectable != 1) continue;
        PLRWEAPON_tag& slot = b->weapons[c];
        if (c != 0x54) anim->next_weapon = int8_t(anim->next_weapon + slot.variant);
        if (slot.have != 0) {
            if (Player_WeaponHasAmmo(b, anim->next_weapon)) return;
            if (0 < slot.variant && Player_WeaponHasAmmo(b, c)) {
                slot.variant = 0;
                anim->next_weapon = c;
                return;
            }
        }
        anim->next_weapon = c;
    }
    anim->next_weapon = anim->weapon;
}

// Só do port: o n-ésimo aparelho (1 a kPortGadgetSlots) pelos comandos gadget_1 ... gadget_5 (o PS2 só tem o ciclo
// das ações 10/11). Os aparelhos do jogador na ordem do ciclo do grupo 1 de Player_WeaponChange (de 0x4a a 0x5e, as
// classes que entram na troca e que o jogador tem; a numeração não muda quando um fica sem munição), pedidos como o
// ciclo pede: as mesmas recusas (os subestados de travessia, a arma no meio de uma animação, nada ou o 0x5d/0x5e na
// mão, mirando), a variante lembrada da classe (sem munição nela, a classe) e as anotações last_weapon/last_gadget/
// prev_weapon; a troca acontece pela máquina de estados da arma na mão (Player_WeaponSelect). Um espaço vazio, o
// aparelho que já está na mão ou um sem munição não fazem nada. Devolve se pediu a troca.
bool Player_PortSelectGadget(obj_tag* obj, int slot) {
    if (obj == nullptr || slot < 1) return false;
    BLData* b = bloke(obj);
    const int16_t sub = int16_t(obj->unknown_f6);
    if (sub == 2 || sub == 10 || sub == 12 || sub == 13 || sub == 16) return false;
    const uint16_t gs = b->gun->state;
    if (gs == 1 || gs == 3 || (5 <= gs && gs <= 9) || gs == 11 || (13 <= gs && gs <= 16)) return false;
    ANIMOBJ_tag* anim = anim_of(obj);
    const int8_t w = anim->weapon;
    if (w == kWeaponNone) return false;
    if (kWeaponNone < w && w < 0x5f && 0x5c < w) return false;
    if ((anim->flags & 1) != 0) return false;
    int n = 0;
    for (int32_t c = 0x4a; c < 0x5f; ++c) {
        if (weapon_def(c)->selectable != 1) continue;
        PLRWEAPON_tag& s = b->weapons[c];
        if (s.have == 0 || ++n != slot) continue;
        int8_t pick = int8_t(c != 0x54 ? c + s.variant : c);
        if (!Player_WeaponHasAmmo(b, pick)) {
            if (!(0 < s.variant && Player_WeaponHasAmmo(b, int16_t(c)))) return false;
            s.variant = 0;
            pick = int8_t(c);
        }
        if (pick == w) return false;
        const auto is_weapon = [](int8_t x) { return uint32_t(uint8_t(x)) - 1u < 0x47u; };
        const auto is_gadget = [](int8_t x) { return uint32_t(uint8_t(x)) - 0x4au < 0x15u; };
        if (is_weapon(w)) b->last_weapon = w;
        if (is_gadget(w)) b->last_gadget = w;
        anim->prev_weapon = w;
        anim->next_weapon = pick;
        return true;
    }
    return false;
}

// [decomp] 0x001a16c0 Player_WeaponHasAmmo
// A arma pode atirar: tem munição guardada ou no pente, não gasta munição, ou (flag 0x20000) o
// objeto lançado por ela ainda é do jogador. A 0x45 precisa do pente da 0x45 cheio até o tamanho
// do pente.
bool Player_WeaponHasAmmo(BLData* b, int16_t weapon) {
    if (weapon == 0x45) return weapon_def(0x45)->clip <= b->weapons[0x45].rounds;
    const weapon_definition_tag* d = weapon_def(weapon);
    if (ammo_of(b, d->ammo_type) != 0) return true;
    if (Player_AmmoInGun(b, weapon) != 0) return true;
    if (d->ammo_type == 0) return true;
    if ((d->flags & 0x20000) == 0) return false;
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 5) continue;
        uint8_t* data = static_cast<uint8_t*>(o->data);
        uint8_t* def;
        obj_tag* owner;
        def = nf::p32_load<uint8_t>(data + 0x44);
        owner = nf::p32_load<obj_tag>(data + 0x30);
        int16_t klass;
        std::memcpy(&klass, def + 2, 2);
        if (klass == d->klass && owner == glb_players[b->player]) return true;
    }
    return false;
}

// [decomp] 0x001a9400 PositionBeam
// Um feixe (o modelo de 1 m ao longo de z) de "from" a "to": na posição de "from", escala = a distância, esfera centrada em
// "from" com o dobro da distância de raio (mode 1: matriz pela rotação; 0x20: refazer) e os ângulos da direção de "to"
// para "from", com a inclinação negada e sem giro em z.
void PositionBeam(obj_tag* beam, _VECTOR* from, _VECTOR* to) {
    beam->pos = *from;
    beam->mode &= 0xffdf;
    const float d = Vec_Dist3D(from, to);
    const uint16_t mode = beam->mode;
    beam->scale = d;
    beam->mode = uint16_t(mode | 1);
    beam->mode = uint16_t(mode | 0x21);
    beam->sphere = *from;
    beam->sphere.w = d + d;
    vecutil_cartesian_to_spherical_acc(&beam->rot, from->x - to->x, from->y - to->y, from->z - to->z);
    beam->rot.z = 0.0f;
    beam->rot.x = -beam->rot.x;
}

// [decomp] 0x001a94d8 FractalThing
// O raio do taser em zigue-zague: o ponto do meio entre a e b, deslocado ao acaso (até a distância x 0,5, ou
// raiz(distância) / 6 com sqrt_mode), vai para objs[(from + to) / 2], e cada metade se divide de novo enquanto houver
// objeto entre os índices.
// Divergência: o w do ponto do meio é lixo da pilha no original; aqui 0 (o desenho não o lê).
void FractalThing(P32<obj_tag>* objs, int16_t from, int16_t to, _VECTOR* a, _VECTOR* b, uint8_t sqrt_mode) {
    if (!(1 < int32_t(to) - int32_t(from))) return;
    const int16_t mid = int16_t((int32_t(from) + int32_t(to)) / 2);
    float r;
    float k;
    if (sqrt_mode == 0) {
        r = Vec_Dist3D(a, b);
        k = 0.5f;
    } else {
        r = std::sqrt(Vec_Dist3D(a, b));
        k = 0.16666667f;
    }
    r = r * k;
    const float half = r * 0.5f;
    alignas(16) _VECTOR m;
    m.x = (a->x + b->x) * 0.5f + (Rand_FRand(r) - half);
    m.y = (a->y + b->y) * 0.5f + (Rand_FRand(r) - half);
    m.z = (a->z + b->z) * 0.5f + (Rand_FRand(r) - half);
    m.w = 0.0f;
    objs[mid]->pos = m;
    FractalThing(objs, from, mid, a, &m, sqrt_mode);
    FractalThing(objs, mid, to, &m, b, sqrt_mode);
}

// [decomp] 0x001a4560 Draw_LaserBeam
// O raio do laser (armas com weapon_definition_tag::flags2 0x100, chamada por Player_MuzzleFlash a cada tiro, até o
// ponto acertado): o primeiro objeto de efeito do jogador (BLData::fx[0]) com o modelo do raio (0x2000131 na arma
// 0x4e, o laser do relógio; 0x20006c1 nas outras), opaco, visto por todas as câmeras, esticado do cano (osso 0 da
// arma) até "to" (PositionBeam, aqui escrito por extenso no original).
void Draw_LaserBeam(BLData* b, _VECTOR* to) {
    alignas(16) _VECTOR from;
    AnimGetBoneWorldTrans(b->gun, 0, 0, &from, nullptr);
    obj_tag* beam = b->fx[0];
    beam->unknown_103[3] = 0xff;
    if (anim_of(glb_players[b->player])->weapon == 0x4e) {
        hashtable_set_object_to_entity_gfx(beam, 0x2000131);
    } else {
        hashtable_set_object_to_entity_gfx(beam, 0x20006c1);
    }
    View_SetDrawInAllViews(beam);
    PositionBeam(beam, &from, to);
}

// [decomp] 0x001a46a8 Draw_TaserBeam
// O raio do taser (flags2 0x80, quando o tiro acertou: BLData::taser_hit): do cano até "to", encurtado ao alcance
// da arma 0x4c (o taser) se for mais longe; de 2 a 32 segmentos (0,8 por metro) nos objetos de efeito, os pontos do
// meio em zigue-zague (FractalThing) e cada segmento o modelo 0x200026d esticado até o próximo, com alfa ao acaso
// (0x7f a 0xfe).
void Draw_TaserBeam(BLData* b, _VECTOR* to) {
    alignas(16) _VECTOR from;
    AnimGetBoneWorldTrans(b->gun, 0, 0, &from, nullptr);
    b->fx[0]->pos = from;
    float len = Vec_Dist3D(&from, to);
    const float range = weapon_def(0x4c)->range;
    if (range < len) {
        const float k = range / len;
        *to = {from.x + (to->x - from.x) * k, from.y + (to->y - from.y) * k, from.z + (to->z - from.z) * k,
               from.w + (to->w - from.w) * k};
        len = range;
    }
    int32_t n = int16_t(int32_t(len * 0.8f));
    if (n < 2) {
        n = 2;
    } else if (0x20 < n) {
        n = 0x20;
    }
    P32<obj_tag>* fx = b->fx;
    if (1 < n) {
        const int16_t mid = int16_t(n / 2);
        const float r = std::sqrt(Vec_Dist3D(&from, to)) * 0.16666667f;
        const float half = r * 0.5f;
        alignas(16) _VECTOR m;
        m.x = (from.x + to->x) * 0.5f + (Rand_FRand(r) - half);
        m.y = (from.y + to->y) * 0.5f + (Rand_FRand(r) - half);
        m.z = (from.z + to->z) * 0.5f + (Rand_FRand(r) - half);
        m.w = 0.0f;
        fx[mid]->pos = m;
        FractalThing(fx, 0, mid, &from, &m, 1);
        FractalThing(fx, mid, int16_t(n), &m, to, 1);
    }
    --n;
    const auto segment = [](obj_tag* o, _VECTOR* end) {
        hashtable_set_object_to_entity_gfx(o, 0x200026d);
        View_SetDrawInAllViews(o);
        alignas(16) _VECTOR start = o->pos;
        PositionBeam(o, &start, end);
        o->unknown_103[3] = uint8_t(Rand_Rand(0x80) + 0x7f);
    };
    for (int32_t i = 0; i < n; ++i) segment(fx[i], &fx[i + 1]->pos);
    segment(fx[n], to);
}

// [decomp] 0x001a65d8 Player_LaserPointer
// A mira laser (armas com weapon_definition_tag::flags 8, ou a variante; ligada e desligada pelo evento 4/1 da
// animação da arma, BLData::gun_flag8): um raio dos olhos até 100 m à frente do anexo 4 da arma (a matriz dele, linha
// 2); o feixe (BLData::fx[0]) sai do anexo até o ponto do raio mais perto do acerto (ou até o fim), com o modelo
// 0x2000131 e alfa (alfa + 0x3f + 0..128) / 2 na visão especial (viewer_tag::mode 1), senão 0x2000133 com o alfa
// andando +-4 entre 0x10 e 0x30; só na câmera do jogador em primeira pessoa (BLData::cam_mode 0). Com acerto, o ponto
// (BLData::glow, vermelho 0x40) fica 1 cm à frente da superfície, alinhado à normal, com escala 1 + distância² /
// MAX_VIEW_CONE (até 4), visto por todas as câmeras.
// Divergência: o w do ponto a 100 m é lixo da pilha no original (vmaddw.xyz); aqui o do anexo.
void Player_LaserPointer(obj_tag* obj) {
    P32<HITDATA_tag> hits = nullptr;
    BLData* b = bloke(obj);
    obj_tag* glow = b->glow;
    if (b->gun_flag8 == 0) return;
    const int8_t player = b->player;
    const weapon_definition_tag* cur = weapon_def(anim_of(obj)->weapon);
    if ((cur->flags & 8) == 0 && (weapon_def(int32_t(anim_of(obj)->weapon) + int32_t(cur->alt))->flags & 8) == 0) {
        return;
    }
    if (b->fx[0] == nullptr || glow == nullptr) return;
    alignas(16) _VECTOR head, gun, end;
    Player_GetHeadPos(&head, obj, nullptr);
    MATRIX m;
    AnimGetBoneWorldTrans(b->gun, 4, 0, &gun, &m);
    alignas(16) const _VECTOR far_pt{m.m[2][0] * 100.0f + m.m[3][0], m.m[2][1] * 100.0f + m.m[3][1],
                                     m.m[2][2] * 100.0f + m.m[3][2], gun.w};
    P32<HITDATA_tag> hit = nullptr;
    if (Collide_RayIntersect(&head, &far_pt, obj->cel, obj, nullptr, &hits, 0, 8, 0)) hit = hits;
    if (hit != nullptr) {
        vecutil_calculate_closest_point_on_line(&end, &head, &far_pt, &hit->pos);
    } else {
        end = far_pt;
    }
    obj_tag* beam = b->fx[0];
    beam->pos = gun;
    beam->mode &= 0xffdf;
    if (glb_viewer[player]->mode == 1) {
        hashtable_set_object_to_entity_gfx(beam, 0x2000131);
        const float a = (float(int32_t(beam->unknown_103[3]) + 0x3f) + Rand_FRand(128.0f)) * 0.5f;
        beam->unknown_103[3] = uint8_t(int32_t(a));
    } else {
        hashtable_set_object_to_entity_gfx(beam, 0x2000133);
        uint8_t a = uint8_t(beam->unknown_103[3] - 4 + Rand_Rand(9));
        if (a < 0x10) {
            a = 0x10;
        } else if (0x30 < a) {
            a = 0x30;
        }
        beam->unknown_103[3] = a;
    }
    if (b->cam_mode == 0) View_DrawInThisViewOnly(beam, uint32_t(int32_t(b->player)));
    const float dist = Vec_Dist3D(&gun, &end);
    beam->scale = dist;
    beam->mode |= 0x21;
    beam->flags |= 0x2000;
    vecutil_cartesian_to_spherical_acc(&beam->rot, gun.x - end.x, gun.y - end.y, gun.z - end.z);
    beam->rot.z = 0.0f;
    beam->rot.x = -beam->rot.x;
    if (hit != nullptr) {
        Mat_Align2Dir(&glow->matrix, reinterpret_cast<const _VECTOR*>(&hit->plane), &CONSTANT_UP1, &CONSTANT_UP2);
        alignas(16) _VECTOR dir;
        Mat_GetDir(&dir, &glow->matrix);
        glow->pos.x = end.x + dir.x * 0.01f;
        glow->pos.y = end.y + dir.y * 0.01f;
        glow->unknown_103[0] = 0x40;
        glow->unknown_103[1] = 0;
        glow->unknown_103[2] = 0;
        glow->mode |= 0x20;
        float s = (dist * dist) / MAX_VIEW_CONE;
        glow->pos.z = end.z + dir.z * 0.01f;
        s = s + 1.0f;
        glow->scale = s;
        if (4.0f < s) glow->scale = 4.0f;
        View_SetDrawInAllViews(glow);
    }
    Collide_FreeHitList(&hits);
}

// [decomp] 0x001a69d0 Player_SetWatch
// O relógio no pulso (fora do multijogador, com a arma na mão animada e da pele 0x500004b, a do relógio de Bond): o
// mostrador (o anexo 2 com a entidade 0x20008f9) e os três ponteiros (Hours, Minutes, Seconds: objetos 0x2000876/7/8
// criados na primeira vez, presos ao jogador), girados em volta de x do anexo 2 pela hora: a hora de partida da missão
// (centésimos de segundo: 9:00 nas 07000001-04, 19:30 nas 05-08, 22:30 nas 09-0b, 8:00 nas 0c-0d, 6:00 nas 11-13 e
// 4a, 4:00 nas 14-16; 0 nas outras) mais o cronômetro da missão do jogador 0 (PlrMissionStats[0].elapsed). Com outra
// arma, os ponteiros são apagados.
void Player_SetWatch(obj_tag* obj) {
    float base = 0.0f;
    BLData* b = bloke(obj);
    if (mp_flag(0x180) != 0) return;
    obj_tag* gun = b->gun;
    if (gun == nullptr || gun->sphere_src == nullptr) return;
    switch (GameState.level_file) {
        case 0x7000001: case 0x7000002: case 0x7000003: case 0x7000004: base = 3.24e6f; break;
        case 0x7000005: case 0x7000006: case 0x7000007: case 0x7000008: base = 7.02e6f; break;
        case 0x7000009: case 0x700000a: case 0x700000b: base = 8.1e6f; break;
        case 0x700000c: case 0x700000d: base = 2.88e6f; break;
        case 0x7000011: case 0x7000012: case 0x7000013: case 0x700004a: base = 2.16e6f; break;
        case 0x7000014: case 0x7000015: case 0x7000016: base = 1.44e6f; break;
        default: break;
    }
    if (anim_of(gun)->skin->hash != 0x500004b) {
        for (obj_tag** hand : {&Hours, &Minutes, &Seconds}) {
            if (*hand != nullptr) {
                (*hand)->life |= 1;
                *hand = nullptr;
            }
        }
        return;
    }
    AnimDatumSetEntity(gun, 2, 0x20008f9, 0xff);
    const float t = float(uint32_t(PlrMissionStats[0].elapsed)) + base;
    alignas(16) _VECTOR at;
    MATRIX m;
    AnimGetBoneWorldTrans(b->gun, 2, 0, &at, &m);
    const auto create = [&](obj_tag** hand, uint32_t entity) {
        if (*hand != nullptr) return;
        *hand = Control_CreateObjEx(0, &at, nullptr, &m, hashtable_hashcode_to_celglist(entity), obj, 1, 0, 1.0f, 0,
                                    0xff, 0xff, 0xff);
    };
    create(&Hours, 0x2000876);
    create(&Minutes, 0x2000877);
    create(&Seconds, 0x2000878);
    const auto turn = [&](obj_tag* hand, float rate) {
        if (hand == nullptr) return;
        const float a = std::fmod(t * rate, 6.2831855f);
        MATRIX r;
        RotMatrixX(-a - 1.5707964f, &r);
        Concat(&m, &r);
        Mat_CopyRot(&r, &hand->matrix);
        Mat_SetTrans(reinterpret_cast<_VECTOR*>(&m.m[3][0]), &hand->matrix);
        hand->mode |= 0x24;
        hand->flags = (hand->flags | 0x2000) & 0xfffff7ffu;
    };
    turn(Hours, 1.4544411e-06f);
    turn(Minutes, 1.7453292e-05f);
    turn(Seconds, 0.0010471976f);
}

// [decomp] 0x001a9680 Player_GetBestWeapon
// Pede a melhor arma (BestWeapon) que o jogador tem e pode atirar, na variante escolhida.
void Player_GetBestWeapon(obj_tag* obj) {
    BLData* b = bloke(obj);
    for (int i = 0; i < 18; ++i) {
        const int16_t k = weapon_def(kBestWeapon[i])->klass;
        const int16_t w = k == 0x1a ? int16_t(0x1a) : int16_t(k + b->weapons[k].variant);
        if (b->weapons[k].have != 0 && Player_WeaponHasAmmo(b, w)) {
            anim_of(obj)->next_weapon = int8_t(w);
            return;
        }
    }
}

// [decomp] 0x001a6eb0 Player_IsBetterWeapon
// "weapon" vem antes da arma na mão em BestWeapon (com um aparelho na mão, nunca).
bool Player_IsBetterWeapon(obj_tag* obj, int16_t weapon) {
    const int16_t current = weapon_def(anim_of(obj)->weapon)->klass;
    if (uint32_t(int32_t(current)) - 0x4au < 0x15u) return false;
    int32_t at_current = -1;
    int32_t at_new = -1;
    for (int32_t i = 0; i < 18; ++i) {
        const int16_t k = weapon_def(kBestWeapon[i])->klass;
        if (k == weapon_def(weapon)->klass) at_new = i;
        if (k == current) at_current = i;
    }
    if (at_current == -1 || at_new == -1) return false;
    return at_new < at_current;
}

// [decomp] 0x001a3c70 Player_SetFiringAnim
// A animação de tiro: anim_fire, ou anim_fire_alt com o pente vazio (flag 0x1000 e a 0x11), metade
// das vezes (0x80000) ou alternando as mãos (0x40000). Põe a arma no estado 9. Devolve o script.
ANIMSCRIPT_tag* Player_SetFiringAnim(BLData* b, obj_tag* gun, weapon_definition_tag* def) {
    uint32_t script = 0;
    const int8_t w = anim_of(glb_players[b->player])->weapon;
    const int32_t slot = clip_slot(w);
    if (((def->flags & 0x1000) != 0 || def->klass == 0x11) && b->weapons[slot].rounds < 1) script = def->anim_fire_alt;
    if ((def->flags & 0x80000) != 0 && 0x32 < Rand_Rand(100)) script = def->anim_fire_alt;
    if (script == 0) script = def->anim_fire;
    if ((def->flags & 0x40000) != 0) {
        b->alt_hand = uint8_t(1 - b->alt_hand);
        script = b->alt_hand == 0 ? def->anim_fire : def->anim_fire_alt;
    }
    if (def->skin == 0 || script == 0) return nullptr;
    b->gun->state = 9;
    return AnimScriptAdd(gun, script);
}

// [decomp] 0x001a1988 Player_ReloadAmmoType
// Passa munição guardada para o pente (até enchê-lo, ou uma bala com a flag 0x100). Com test_only só
// diz se haveria o que recarregar.
uint32_t Player_ReloadAmmoType(obj_tag* obj, int8_t test_only) {
    const int8_t w = anim_of(obj)->weapon;
    const weapon_definition_tag* d = weapon_def(w);
    BLData* b = bloke(obj);
    int16_t& ammo = ammo_of(b, d->ammo_type);
    const int32_t have = ammo;
    if (have <= 0) return 0;
    int16_t& clip = b->weapons[clip_slot(w)].rounds;
    int32_t n = int32_t(d->clip) - int32_t(clip);
    if (0 < n && (d->flags & 0x100) != 0) n = 1;
    if (have < n) n = have;
    if (n == 0) return 0;
    if (test_only == 0) {
        ammo = int16_t(ammo - n);
        clip = int16_t(clip + n);
    }
    return 1;
}

// [decomp] 0x001a2ba8 Player_HandleHasNoAmmo
// Acabou a munição: a variante (flag 0x10) se ela tiver, senão a melhor arma. Algumas armas ficam
// na mão; a 0x52 vazia vira a 0x53.
void Player_HandleHasNoAmmo(obj_tag* obj) {
    ANIMOBJ_tag* anim = anim_of(obj);
    BLData* b = bloke(obj);
    const int8_t w = anim->weapon;
    if (w < 0x34) {
        if (0x31 < w || w == 1) return;
    } else if (0x49 < w) {
        if (w < 0x52) return;
        if (w == 0x52) {
            if (b->weapons[82].rounds < 1) {
                b->weapons[82].have = 0;
                Player_EquipWeapon(b, 0x53, 999);
                anim->next_weapon = 0x53;
                return;
            }
            anim->next_weapon = 0x52;
            return;
        }
    }
    const weapon_definition_tag* d = weapon_def(w);
    if ((d->flags & 0x10) != 0) {
        const int32_t v = int32_t(w) + int32_t(d->alt);
        if (Player_WeaponHasAmmo(b, int16_t(v))) {
            anim->next_weapon = int8_t(v);
            return;
        }
    }
    Player_GetBestWeapon(obj);
}

// [decomp] 0x001a9148 Player_AmmoIndex
// O espaço de BLData::weapons com o pente da arma (clip_slot; usado pelo HUD).
int16_t Player_AmmoIndex(int16_t weapon) { return int16_t(clip_slot(weapon)); }

// [decomp] 0x001a9190 Player_AmmoInGun
int16_t Player_AmmoInGun(BLData* b, int16_t weapon) { return b->weapons[int16_t(clip_slot(weapon))].rounds; }

// [decomp] 0x001a18e0 Player_RoundToFire
// Há "need" balas no pente para atirar (armas sem munição sempre); tira "take" delas, a não ser com a
// trapaça de munição infinita (CheatInfo + 8).
uint32_t Player_RoundToFire(obj_tag* obj, uint8_t need, uint8_t take) {
    const int8_t w = anim_of(obj)->weapon;
    if (weapon_def(w)->ammo_type == 0) return 1;
    int16_t& clip = bloke(obj)->weapons[clip_slot(w)].rounds;
    if (clip < int16_t(uint16_t(need))) return 0;
    if (cheat(8) == 0) clip = int16_t(clip - uint16_t(take));
    return 1;
}

// [decomp] 0x001a4a98 Player_GetAimingPoint
// Um ponto 256 m à frente dos olhos na direção da mira: o centro da tela deslocado pela mira
// suavizada (em primeira pessoa), na rotação do jogador inclinada pelo pitch.
void Player_GetAimingPoint(obj_tag* obj, _VECTOR* out) {
    BLData* b = bloke(obj);
    _VECTOR head{0.0f, 0.0f, 0.0f, 0.0f};
    Player_GetHeadPos(&head, obj, nullptr);
    _VECTOR dir{0.0f, 0.0f, -1.0f, 0.0f};
    if (b->cam_mode == 0) {
        const viewer_tag* v = glb_viewer[b->player];
        dir.x = ((v->width * b->aim_cur_x) / v->lod_scale) / (v->proj_x + v->proj_x);
        dir.y = -((v->height * b->aim_cur_y) / v->lod_scale) / (v->proj_y + v->proj_y);
    }
    MATRIX m;
    RotMatrixX(-(b->pitch * 1.5707964f), &m);
    Concat(&obj->matrix, &m);
    _VECTOR at = rotate3(m, dir);
    Vec_MulR32(&at, &at, -256.0f);
    *out = add4(head, at);
}

// [decomp] 0x00195a88 Player_ResetWalkAnim
void Player_ResetWalkAnim(obj_tag* obj) { bloke(obj)->unknown_954 = 0xff; }

// [decomp] 0x001a1a70 Player_Zoom
// Mirando, o direcional muda o alvo do zoom (2,5% por tick); o zoom anda até o alvo (15% por tick),
// entre 1 e o máximo da arma (até 50), e a classe guarda o valor. Mudar o zoom desfoca a tela.
void Player_Zoom(obj_tag* obj) {
    BLData* b = bloke(obj);
    // Só do port: mirando com uma arma de zoom (o máximo dela acima de 1, o mesmo caso em que o direcional dá zoom), a
    // roda do mouse não troca de arma: cada dente para cima aproxima o alvo do zoom (para baixo, afasta) como
    // kWheelZoomTicks ticks do direcional, e o zoom anda até ele pelo caminho do jogo logo abaixo (15% por tick, com o
    // desfoque); a classe guarda o alvo, como o direcional faz com o zoom.
    constexpr float kWheelZoomTicks = 9.0f;
    const bool wheel_zoom = (anim_of(obj)->flags & 1) != 0 && 1.0f < b->zoom_max;
    psiInput_SetWheelZoom(b->player, wheel_zoom);
    if (wheel_zoom) {
        const float notches = psiInput_TakeWheelZoom(b->player);
        if (notches != 0.0f) {
            const float hi = std::min(b->zoom_max, 50.0f);
            b->zoom_target = std::clamp(b->zoom_target * nf::fm::pow(1.025f, kWheelZoomTicks * notches), 1.0f, hi);
            b->weapons[uint16_t(weapon_def(anim_of(obj)->weapon)->klass)].zoom = b->zoom_target;
        }
    }
    const float before = b->zoom;
    float input = 0.0f;
    if ((anim_of(obj)->flags & 1) != 0) input = Input_Actionf(b->player, kActDpadY, kActionDown);
    float diff;
    float step;
    if (input == 0.0f) {
        diff = b->zoom - b->zoom_target;
        step = FRAME_RATE_MUL * 0.15f + 1.0f;
        if (0.0f < diff) step = 1.0f / step;
        if (b->zoom_sound != 0) {
            Sound_Stop(b->zoom_sound);
            b->zoom_sound = 0;
        }
    } else {
        step = FRAME_RATE_MUL * 0.025f + 1.0f;
        if (0.0f < input) step = 1.0f / step;
        b->zoom_target = b->zoom_target * step;
        diff = input;
    }
    if (b->zoom_max < b->zoom_target) b->zoom_target = b->zoom_max;
    if (1.0f <= b->zoom_target) {
        if (50.0f < b->zoom_target) b->zoom_target = 50.0f;
    } else {
        b->zoom_target = 1.0f;
    }
    if (diff != 0.0f) {
        const float z = b->zoom * step;
        b->zoom = z;
        const bool past = diff < 0.0f ? b->zoom_target < z : z < b->zoom_target;
        if (past) b->zoom = b->zoom_target;
    }
    if (input != 0.0f) {
        b->zoom_target = b->zoom;
        b->weapons[uint16_t(weapon_def(anim_of(obj)->weapon)->klass)].zoom = b->zoom;
        if (b->zoom != before) {
            if (Input_Action(b->player, 20, kActionPressed) != 0) {
                if (b->zoom_sound != 0) Sound_Stop(b->zoom_sound);
            } else if (Input_Action(b->player, 21, kActionPressed) != 0 && b->zoom_sound != 0) {
                Sound_Stop(b->zoom_sound);
            }
        }
    }
    if (mp_flag(0x180) == 0 && b->zoom != before) GlobalBlur = 0x7f;
}

// [decomp] 0x001a73f8 Check_AutoAim
// Um candidato da mira automática (Player_AutoAim chama para cada drone e cada jogador, ou para os objetos 0x51 com as
// armas 0x50/0x51). Fica de fora: objeto nulo ou desligado (obj_tag +0xfc), o drone morto, da classe 0xb, no estado 1
// ou que não é inimigo (Drone_tag::side 1; no multijogador, o do mesmo time), o outro jogador do mesmo time, o próprio,
// sem vida ou morto (tipo 0x12). O ponto mirado é o osso 2 (a coluna) dos drones e jogadores, ou a posição do objeto,
// a no máximo *best metros dos olhos. O cone: Autoaim_Angle_H/V (10 graus) x a mira automática da arma (weapon_data
// +0x14, em %) x (1 - distância / Autoaim_Range) x o fator da dificuldade (Operative 1,25, Agent 1, 00 Agent 0: sem
// mira automática), 1 inteiro com as armas 0x50/0x51, o dobro (Autoaim_LockOnMul) para o alvo atual. Dentro do cone
// de giro (atan2 até o ponto contra obj_tag::rot.y) e do de inclinação (asin contra BLData::pitch x 90 graus) e com
// linha de visada, o alvo vira BLData::aim_target, e a posição dele na tela (View_3DPoint2Screen na câmera do jogador)
// vai para aim_cur_x/y em quartos da tela a partir do meio (Player_AutoAim corta pela metade no mesmo tick: a mira, a
// arma e a bala seguem); *best passa a ser a distância (o mais perto ganha). Fora da tela, só a distância.
void Check_AutoAim(obj_tag* obj, obj_tag* target, float* best) {
    BLData* b = bloke(obj);
    viewer_tag* v = glb_viewer[b->player];
    if (target == nullptr || target->unknown_fc == 0) return;
    uint32_t mp_team;
    std::memcpy(&mp_team, MPSettings + 0x184, 4);
    const uint8_t type = target->type;
    if (type == 2) {
        Drone_tag* d = static_cast<Drone_tag*>(target->data);
        if (d->health <= 0.0f) return;
        if (d->klass == 0xb) return;
        if (target->state == 1) return;
        if (mp_team == 0) {
            if (d->side != 1) return;
        } else {
            obj_tag* owner;
            owner = nf::p32_load<obj_tag>(reinterpret_cast<const uint8_t*>(d) + 0xc);
            if (MP_areObjectsOnSameTeam(obj, owner)) return;
        }
    } else if (type == 3) {
        if (mp_team != 0 && MP_areObjectsOnSameTeam(obj, target)) return;
        BLData* tb = bloke(target);
        if (tb->player == b->player) return;
        if (tb->health <= 0.0f) return;
    } else if (type == 0x12) {
        return;
    }
    alignas(16) _VECTOR head, pos;
    Player_GetHeadPos(&head, obj, nullptr);
    if (uint8_t(type - 2) < 2) {
        AnimGetBoneWorldTrans(target, 0x80000002u, 0, &pos, nullptr);
    } else {
        pos = target->pos;
    }
    const float dist = Vec_Dist3D(&head, &pos);
    if (*best < dist) return;
    const float bearing = nf::fm::atan2(pos.x - head.x, pos.z - head.z);
    const float turn = Vec_AngleDifference(bearing, obj->rot.y);
    float diff_mul;
    switch (GameState.difficulty) {
        case 1: diff_mul = Autoaim_EasyMul; break;
        case 2: diff_mul = Autoaim_NormalMul; break;
        case 3:
        case 4: diff_mul = Autoaim_HardMul; break;
        default: diff_mul = Autoaim_NormalMul; break;
    }
    ANIMOBJ_tag* anim = anim_of(obj);
    float scale = weapon_def(anim->weapon)->autoaim * 0.01f * (1.0f - dist / Autoaim_Range);
    scale = scale * diff_mul;
    if (uint8_t(uint8_t(anim->weapon) - 0x50) < 2) scale = 1.0f;
    float cone_v = Autoaim_Angle_V * scale;
    float cone_h = Autoaim_Angle_H * scale;
    if (target == b->aim_target) {
        cone_v = cone_v * Autoaim_LockOnMul;
        cone_h = cone_h * Autoaim_LockOnMul;
    }
    if (cone_h == 0.0f && cone_v == 0.0f) return;
    if (!(std::fabs(turn) < cone_h)) return;
    const float up = Vec_AngleDifference(nf::fm::asin((pos.y - head.y) / dist), b->pitch * 1.5707964f);
    if (!(std::fabs(-up) < cone_v)) return;
    if (!Collide_LineOfSight(&head, &pos, obj->cel, obj, target, 10)) return;
    alignas(16) _VECTOR screen{0.0f, 0.0f, 0.0f, 0.0f};
    if (0 < View_3DPoint2Screen(&pos, &screen, v->index)) {
        b->aim_cur_x = (screen.x - v->width * 0.5f) / (v->width * 0.25f);
        b->aim_target = target;
        b->aim_cur_y = (screen.y - v->height * 0.5f) / (v->height * 0.25f);
    }
    *best = dist;
}

// [decomp] 0x001a7c98 Player_AutoAim
// Com a mira automática ligada e sem mirar, procura um alvo (Check_AutoAim: os drones e os
// jogadores, ou os objetos do tipo 0x51 para as armas 0x50/0x51) e a mira volta ao centro pela
// metade a cada tick; mirando, a mira suavizada segue a mira manual pela metade.
void Player_AutoAim(obj_tag* obj) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    uint8_t on = mp_flag(0x180) == 0 ? PlayerSetting[b->player].autoaim : PlayerSetting[b->player].autoaim_mp;
    if (uint8_t(anim->weapon - 0x50) < 2) on = 1;
    if (on != 0 && (anim->flags & 1) == 0) {
        float best = Autoaim_Range;
        if (uint32_t(uint8_t(anim->weapon)) - 0x50u < 2u) {
            best = weapon_def(anim->weapon)->range;
            for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
                if (o->type == 0x51) Check_AutoAim(obj, o, &best);
            }
        } else {
            uint8_t* node;
            node = nf::p32_load<uint8_t>(&NPCGlobals.drones.first);
            for (; node != nullptr; node = nf::p32_load<uint8_t>(node + 4)) {
                obj_tag* o;
                o = nf::p32_load<obj_tag>(node + 0xc);
                Check_AutoAim(obj, o, &best);
            }
            for (int i = 0; i < 4; ++i) Check_AutoAim(obj, glb_players[i], &best);
        }
        if (best == Autoaim_Range) b->aim_target = nullptr;
    } else if ((anim->flags & 1) != 0) {
        b->aim_cur_x = b->aim_cur_x + (b->aim_x - b->aim_cur_x) * 0.5f;
        b->aim_cur_y = b->aim_cur_y + (b->aim_y - b->aim_cur_y) * 0.5f;
        Check_Target(obj);
        return;
    }
    b->aim_cur_x = b->aim_cur_x + -b->aim_cur_x * 0.5f;
    b->aim_cur_y = b->aim_cur_y + -b->aim_cur_y * 0.5f;
    b->aim_x = b->aim_x + -b->aim_x * 0.5f;
    b->aim_y = b->aim_y + -b->aim_y * 0.5f;
    Check_Target(obj);
}

namespace {
// Os visores da arma: SatchelDigits e PdaDigits (contíguas no .data, uma logo depois da outra).
uint32_t digit_entity(int32_t index) {
    static const uint32_t kDigits[20] = {
        0x2000791, 0x2000792, 0x2000793, 0x2000794, 0x2000795, 0x2000796, 0x2000797, 0x2000798, 0x2000799, 0x200079a,
        0x2000a09, 0x2000a0a, 0x2000a0b, 0x2000a0c, 0x2000a0d, 0x2000a0e, 0x2000a0f, 0x2000a10, 0x2000a11, 0x2000a12,
    };
    return 0 <= index && index < 20 ? kDigits[index] : 0;
}
float shot_frame(obj_tag* gun) {
    ANIMSCRIPT_tag* s = AnimListBuild(gun, 4, 0, nullptr, 1);
    return s != nullptr ? s->frame : 0.0f;
}
}  // namespace

// [decomp] 0x001a5460 Player_WeaponFiring
// O gatilho e o que a arma mostra: esconde os efeitos da câmera do jogador, atualiza os visores e
// modelos presos à arma (contador da bomba 0x3b, PDA, granadas da 0x11, a carga da 0x33...), mira
// (BLData::muzzle), usa ou recarrega (ação 14), recarrega sozinho com o pente vazio e atira: a cada
// fire_delay ticks enquanto houver tiros na rajada, a animação de tiro e a bala (na hora ou no
// quadro fire_frame, por Player_SetWeaponAnimObj).
void Player_WeaponFiring(obj_tag* obj, int8_t force) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    weapon_definition_tag* cur = weapon_def(anim->weapon);
    b->muzzle_flash = 0;
    for (int i = 0; i < 32; ++i) View_SetDrawInNoViews(b->fx[i]);
    View_SetDrawInNoViews(b->glow);
    obj_tag* gun = b->gun;
    if ((cur->flags & 0x800) != 0) {
        ANIMOBJ_tag* ga = anim_of(gun);
        if (ga != nullptr && ga->skin->n_extra != 0) {
            if (gun->state == 8) {
                AnimDatumSetEntity(gun, 0, 0, 0xff);
            } else if (cur->datum_entity != 0) {
                AnimDatumSetEntity(gun, 0, cur->datum_entity, 0xff);
            }
        }
    }
    if ((weapon_def(int32_t(anim->weapon) + int32_t(cur->alt))->flags & 0x800) != 0) AnimDatumSetEntity(gun, 0, 0, 0xff);
    if (cur->klass == 0x3b) {
        // Bomba: o visor conta até (variante + 1) * 5 segundos.
        if (gun->state != 8 || 22.0f <= shot_frame(gun)) {
            const int32_t target = int16_t((uint16_t(cur->id) - uint16_t(cur->klass)) * 5 + 5);
            int16_t& shown = reinterpret_cast<int16_t&>(gun->unknown_f6);
            if (shown < target) ++shown;
            if (target < shown) --shown;
            AnimDatumSetEntity(gun, 1, digit_entity(shown / 10), 0xff);
            AnimDatumSetEntity(gun, 2, digit_entity(shown % 10), 0xff);
        }
    }
    if (uint32_t(uint16_t(cur->klass)) - 0x56u < 2u) {
        // PDA: os quatro algarismos de GlobalVars + 0xd ('*' apaga) e um tempo para apagar.
        int16_t& time = reinterpret_cast<int16_t&>(gun->unknown_f6);
        if (GlobalVars[0x14] == 0) {
            if (0 < time) --time;
            if (time == 0) {
                for (int i = 0; i < 4; ++i) AnimDatumSetEntity(gun, i, 0, 0xff);
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                const uint8_t c = GlobalVars[0xd + i];
                AnimDatumSetEntity(gun, 3 - i, c == '*' ? 0u : digit_entity(10 + int32_t(c) - '0'), 0xff);
            }
            time = int16_t(int32_t(FRAME_RATE));
        }
    }
    if (cur->klass == 0x33) {
        if (gun->state == 9) {
            ANIMSCRIPT_tag* s = AnimListBuild(gun, 4, 0, nullptr, 1);
            if (s != nullptr && s->frame < float(cur->fire_frame)) {
                const float charge = (s->frame / float(cur->fire_frame)) * 0.75f;
                _VECTOR at;
                AnimGetBoneWorldTrans(gun, 0, 0, &at, nullptr);
                Light_Create(&at, 0, 0xff, 0xff, 5.0f, 0, 0, 1, charge + charge, 0, 0, 0, 0xffff);
                const float clip = float(int32_t(cur->clip));
                b->weapons[0x33].rounds = int16_t(int32_t(clip - (clip * s->frame) / float(cur->fire_frame)));
            }
        }
        for (int i = 0x1e; i < 0x26; ++i) AnimDatumSetEntity(gun, i, 0x20008e9, 0xff);
        for (int i = 5; i < 0x19; ++i) AnimDatumSetEntity(gun, i, 0x20008e8, 0xff);
    }
    if (cur->id == 0x54 && gun->state == 0 && CamHint == 0) {
        CamHint = 1;
        Text_AddMsg(b->player, 0, 1, Txt_BindLabel(0x1000216, 0), 0, 0xb4);
    }
    if (cur->id == 0x55 && gun->state == 0) {
        _VECTOR at;
        AnimGetBoneWorldTrans(gun, 0, 0, &at, nullptr);
        Light_Create(&at, 0xff, 0x7f, 0, 1.5f, 0, 0, 1, Rand_FRand(0.5f) + 0.5f, 0, 0, 0, 0xffff);
    }
    if (cur->klass == 0x1a) {
        for (int i = 8; i < 0xc; ++i) AnimDatumSetEntity(gun, i, 0x2000132, 0xff);
        if (FRAME_RATE * 15.0f < float(b->idle_ticks) && 0x14 < int32_t(GameState.start_time) % 0x28) {
            AnimDatumSetEntity(gun, 8, 0, 0xff);
        }
        if (b->idle_wait == 0) AnimDatumSetEntity(gun, 9, 0, 0xff);
        if (b->weapons[27].rounds == 0) AnimDatumSetEntity(gun, 10, 0, 0xff);
        if (cur->id == 0x1b && b->weapons[27].rounds < 3 && 0.75f < FlashMod) AnimDatumSetEntity(gun, 10, 0, 0xff);
        if (b->weapons[26].rounds == 0) AnimDatumSetEntity(gun, 0xb, 0, 0xff);
        if (cur->id == 0x1a && b->weapons[26].rounds < 7 && 0.75f < FlashMod) AnimDatumSetEntity(gun, 0xb, 0, 0xff);
        if (FRAME_RATE * 45.0f < float(b->idle_ticks)) {
            for (int i = 8; i < 0xc; ++i) AnimDatumSetEntity(gun, i, 0, 0xff);
        }
    }
    if (cur->klass == 0x11) {
        // Lançador: as granadas visíveis no tambor.
        const int32_t slot = clip_slot(0x11);
        int32_t n = b->weapons[slot].rounds;
        if (0 < n && gun->state == 9) n = int16_t(b->weapons[slot].rounds + 1);
        if (gun->state == 6 && 80.0f < shot_frame(gun)) {
            const int16_t a = ammo_of(b, cur->ammo_type);
            n = cur->clip < a ? cur->clip : a;
        }
        for (int i = 4; i < 7; ++i) AnimDatumSetEntity(gun, i, 0, 0xff);
        if (0 < n && gun->state != 9) AnimDatumSetEntity(gun, 4, 0x2000816, 0xff);
        if (1 < n) AnimDatumSetEntity(gun, 5, 0x2000816, 0xff);
        if (2 < n) AnimDatumSetEntity(gun, 6, 0x2000816, 0xff);
    }
    if (cur->id == 0x37 && gun->state == 0 && StickyHint == 0) {
        StickyHint = 1;
        Text_AddMsg(b->player, 0, 1, Txt_BindLabel(0x10002dc, 0), 0, 0xb4);
    }
    Player_GetAimingPoint(obj, &b->muzzle);
    const int8_t player = b->player;
    const int16_t burst = cur->burst[b->weapons[anim->weapon].mode];
    if (Input_Action(player, kActFire, kActionDown) == 0 && Input_Action(player, kActUse, kActionPressed) != 0 &&
        int16_t(obj->unknown_f6) != kPlrRide) {
        // Usar; se não havia o que usar, recarregar.
        uint32_t used = Player_Activate(obj, b);
        if ((anim->flags & 1) != 0 && (weapon_def(anim->weapon)->flags & 0x40) != 0) used = 1;
        if (used == 0 && gun->state == 0 && Player_ReloadAmmoType(obj, 1) != 0) {
            b->burst = burst;
            gun->state = 6;
            if ((cur->flags & 0x200) == 0) {
                if (cur->anim_reload_start != 0) {
                    gun->state = 5;
                    AnimScriptAdd(gun, cur->anim_reload_start);
                } else if (cur->anim_reload != 0) {
                    AnimScriptAddSpeed(gun, cur->anim_reload, 1.35f);
                }
                return;
            }
            b->alt_hand = uint8_t(1 - b->alt_hand);
            AnimScriptAdd(gun, b->alt_hand == 0 ? cur->anim_reload_end : cur->anim_reload);
            return;
        }
    }
    // Só do port: o comando "reload" (R). A recarga do bloco de cima, sem Player_Activate (nada é usado: uma porta na
    // frente não abre) e sem a regra da luneta (mirando com ela, o usar não recarrega; a recarga tira a mira pelos
    // estados 6-8 em Player_Weapon, como a recarga automática do pente vazio).
    if (Input_Action(player, kActFire, kActionDown) == 0 && psiInput_PortPressed(player, nf::kPortReload) &&
        int16_t(obj->unknown_f6) != kPlrRide && gun->state == 0 && Player_ReloadAmmoType(obj, 1) != 0) {
        b->burst = burst;
        gun->state = 6;
        if ((cur->flags & 0x200) == 0) {
            if (cur->anim_reload_start != 0) {
                gun->state = 5;
                AnimScriptAdd(gun, cur->anim_reload_start);
            } else if (cur->anim_reload != 0) {
                AnimScriptAddSpeed(gun, cur->anim_reload, 1.35f);
            }
            return;
        }
        b->alt_hand = uint8_t(1 - b->alt_hand);
        AnimScriptAdd(gun, b->alt_hand == 0 ? cur->anim_reload_end : cur->anim_reload);
        return;
    }
    if (Input_Action(player, kActFire, kActionDown) == 0) {
        b->burst = 0;
        if (Player_RoundToFire(obj, cur->rounds_per_shot, 0) == 0) {
            // Pente vazio: recarrega sozinho, ou troca de arma sem munição.
            if (gun->state != 0 || int16_t(obj->unknown_f6) == kPlrRide) return;
            if (Player_ReloadAmmoType(obj, 1) == 0) {
                Player_HandleHasNoAmmo(obj);
                return;
            }
            b->fire_timer = 0.0f;
            b->burst = burst;
            gun->state = 6;
            if (cur->anim_reload_start != 0) {
                gun->state = 5;
                AnimScriptAdd(gun, cur->anim_reload_start);
                return;
            }
            if (cur->anim_reload == 0) return;
            if ((cur->flags & 0x200) == 0) {
                AnimScriptAddSpeed(gun, cur->anim_reload, 1.35f);
                if ((cur->flags & 0x1000) == 0) return;
                ANIMSCRIPT_tag* s = AnimListBuild(gun, 4, 0, nullptr, 1);
                if (s != nullptr) s->frame = 6.0f;
                return;
            }
            b->alt_hand = uint8_t(1 - b->alt_hand);
            AnimScriptAdd(gun, b->alt_hand == 0 ? cur->anim_reload_end : cur->anim_reload);
            return;
        }
    } else {
        if (surfaced == 0 && (cur->flags & 2) == 0) return;
        const uint32_t klass = uint16_t(cur->klass);
        if (klass == 0x54 && (anim->flags & 1) == 0) return;
        if (klass - 0x50u < 2u) {
            if (int16_t(obj->unknown_f6) == kPlrGrapple && b->unknown_15c == 1) return;
            if (b->move_locked != 0) return;
            if (!(b->touch_type == 2 || mp_flag(0x180) != 0)) return;
        }
        if (klass == 0x58) {
            obj_tag* t = b->target;
            if (t == nullptr || !(t->type == 0x49 && t->state == 0)) return;
            if (cur->range < Vec_Dist3D(&obj->pos, &t->pos)) return;
        }
        if (klass - 0x56u < 2u) {
            obj_tag* t = b->target;
            if (t == nullptr) return;
            bool ok = false;
            if (t->type == 0x3a) {
                int16_t kind;
                std::memcpy(&kind, t->data, 2);
                if ((kind == 0 || kind == 2) && (t->state == 0 || t->state == 5)) ok = true;
            }
            if (t->type == 0 && t->glist == hashtable_hashcode_to_celglist(0x20002f8)) ok = true;
            if (!ok) return;
            if (cur->range < Vec_Dist3D(&obj->pos, &b->target->pos)) return;
        }
        if (Player_RoundToFire(obj, cur->rounds_per_shot, 0) == 0 && int16_t(obj->unknown_f6) != kPlrRide) return;
    }
    if (Input_Action(player, kActFire, kActionPressed) != 0 || force == 1) {
        if (int16_t(obj->unknown_f6) == kPlrRide) {
            if (b->ride != nullptr) Bullet_handle_object_destruction(b->ride, obj);
            return;
        }
        if (b->fire_timer <= 0.0f) {
            b->fire_timer = 1.0f;
            b->burst = burst;
        }
    }
    if (b->fire_timer <= 0.0f) return;
    b->fire_timer = b->fire_timer - FRAME_RATE_MUL;
    const int16_t gs = int16_t(gun->state);
    if (gs == 9) {
        if ((cur->flags & 0x8000) != 0) return;
    } else if (gs != 0 && gs != 0xb) {
        return;
    }
    if (0.0f < b->fire_timer) return;
    b->burst = int16_t(b->burst - 1);
    if (b->burst < 0) return;
    if (Player_RoundToFire(obj, cur->rounds_per_shot, cur->rounds_per_shot) == 0) return;
    if (obj->type == 3 && cur->rumble != 0) Input_RumbleStart(player, 5, cur->rumble);
    const float delay = float(cur->fire_delay);
    b->fire_timer = 1.0f < delay ? delay : 1.0f;
    ANIMSCRIPT_tag* s = nullptr;
    if (!(gun->state == 9 && (cur->flags & 0x100400) != 0)) s = Player_SetFiringAnim(b, gun, cur);
    if ((cur->flags2 & 0x400) != 0) {
        if (cur->anim_fire_alt == 0) {
            gun->state = 0xc;
        } else {
            gun->state = 0xb;
            if (s != nullptr) s->speed = 1.5f;
        }
    }
    if (cur->fire_frame == 0) {
        b->bullet_out = 1;
        Player_WeaponInitBullet(obj, cur);
    } else {
        b->bullet_out = 0;
    }
}

// [decomp] 0x001a4bf0 Player_WeaponInitBullet
// O disparo: um raio dos olhos ao ponto de mira acha onde a bala vai (BLData::muzzle, para o
// clarão); as balas saem dos olhos (ou entre eles e o cano) nessa direção, "bullets" por disparo.
// O detonador (flag 0x20000) não atira: os projéteis vivos da mesma classe deste jogador recebem -10 no pavio
// (explodem) e a mão volta à arma da classe (ou à melhor, sem munição); a 0x53 controla a metralhadora montada; o
// taser sem alvo ao alcance devolve a carga.
void Player_WeaponInitBullet(obj_tag* obj, weapon_definition_tag* def) {
    BLData* b = bloke(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    const int8_t player = b->player;
    if ((def->flags & 0x20000) != 0) {
        for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o->type != 5) continue;
            uint8_t* data = static_cast<uint8_t*>(o->data);
            uint8_t* od;
            obj_tag* owner;
            od = nf::p32_load<uint8_t>(data + 0x44);
            owner = nf::p32_load<obj_tag>(data + 0x30);
            int16_t klass;
            std::memcpy(&klass, od + 2, 2);
            if (klass == def->klass && owner == obj) {
                const float t = -10.0f;
                std::memcpy(data + 0xfc, &t, 4);
            }
        }
        if (!Player_WeaponHasAmmo(b, def->klass)) {
            b->weapons[uint16_t(def->klass)].have = 0;
            Player_GetBestWeapon(obj);
            return;
        }
        anim->next_weapon = int8_t(def->klass);
        return;
    }
    if (def->id == 0x53) {
        for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o->type != 0x36) continue;
            obj_tag* user;
            user = nf::p32_load<obj_tag>(static_cast<uint8_t*>(o->data) + 0xb8);
            if (user == obj) {
                GT_TakeControl(o, obj);
                return;
            }
        }
    }
    if (def->bullets == 0) return;
    _VECTOR from{0.0f, 0.0f, 0.0f, 0.0f};
    Player_GetHeadPos(&from, obj, nullptr);
    _VECTOR to{0.0f, 0.0f, 0.0f, 0.0f};
    Player_GetAimingPoint(obj, &to);
    P32<HITDATA_tag> hits = nullptr;
    Collide_RayIntersect(&from, &to, obj->cel, obj, nullptr, &hits, 0, (def->flags2 & 0x800) != 0 ? 0x208u : 0x20au, 0);
    if (hits != nullptr) to = hits->pos;
    const _VECTOR aim = to;
    if (anim->weapon == 0x4a || anim->weapon == 0x4c) {
        b->taser_hit = 0;
        if (hits != nullptr && hits->obj != nullptr && uint8_t(hits->obj->type - 2) < 2 &&
            Vec_Dist3D(&from, &hits->pos) < weapon_def(anim->weapon)->range) {
            b->taser_hit = 1;
        }
        if (b->taser_hit == 0) {
            b->muzzle_flash = 1;
            Collide_FreeHitList(&hits);
            const int8_t w = anim->weapon;
            const weapon_definition_tag* d = weapon_def(w);
            const int32_t back = int32_t(b->weapons[w].rounds) + int32_t(d->rounds_per_shot);
            b->weapons[w].rounds = back < d->clip ? int16_t(back) : d->clip;
            return;
        }
    }
    Collide_FreeHitList(&hits);
    b->muzzle_flash = 1;
    b->muzzle = aim;
    const uint16_t klass = uint16_t(weapon_def(anim->weapon)->klass);
    bool between = def->skin != 0;
    if (klass == 1 || klass == 0x52 || klass == 0x54 || klass == 0x58 || klass == 0x59 || klass == 0x5b) between = false;
    const uint32_t flags = weapon_def(anim->weapon)->flags;
    if ((anim->flags & 1) != 0 && (flags & 0x40) != 0) between = false;
    if ((flags & 0x10000) == 0) between = false;
    if (between) {
        _VECTOR muzzle;
        AnimGetBoneWorldTrans(b->gun, 0, 0, &muzzle, nullptr);
        from.y = (from.y + muzzle.y) * 0.5f;
        from.x = (from.x + muzzle.x) * 0.5f;
        from.z = (from.z + muzzle.z) * 0.5f;
    }
    _VECTOR dir{aim.x - from.x, aim.y - from.y, aim.z - from.z, aim.w - from.w};
    Vec_Normalise(&dir, &dir);
    for (uint16_t n = 0; n < def->bullets; ++n) {
        if ((def->stats & 0xf8) != 0) PlrStat_LogShotFired(player);
        if (def->klass == 0x54) Sound_Play(0xec, 100.0f, 0, 0);
        if (def->klass == 0x52) {
            Mat_GetDir(&dir, &obj->matrix);
            from = add4(obj->pos, _VECTOR{dir.x, dir.y, dir.z, dir.w});
            if (GT_DeployMiniGun(&from, &obj->matrix, obj) == 0) ++b->weapons[82].rounds;
        } else {
            Bullet_init(player, obj, obj, def, &from, &dir, 0xffff);
        }
    }
    if (def->skin != 0 && (def->flags2 & 0x40) != 0 && mp_flag(0x180) == 0) Bullet_init_casing(obj);
    if (def->ammo_type == 0 && def->on_fire != nullptr) def->on_fire(obj);
}

// [decomp] 0x001a4298 Player_MuzzleFlash
// O clarão do tiro: uma luz no cano (osso 0, ou o 4 da outra mão), a fumaça (Gas_Init) no modo
// história e, nas armas de raio (laser, taser), o raio até "target" ou até o que estiver no caminho.
void Player_MuzzleFlash(obj_tag* obj, _VECTOR* target) {
    const weapon_definition_tag* d = weapon_def(anim_of(obj)->weapon);
    BLData* b = bloke(obj);
    bool smoke = true;
    if (d->skin != 0 && d->flash != 0) {
        _VECTOR at;
        MATRIX m;
        const uint32_t bone = ((d->flags & 0x40000) == 0 || b->alt_hand != 0) ? 0u : 4u;
        AnimGetBoneWorldTrans(b->gun, bone, 0, &at, &m);
        b->muzzle_time = d->klass == 0x33 ? 7 : 3;
        Light_Create(&at, d->light_r, d->light_g, d->light_b, d->light_range, 0, 0, 1, 2.0f, 0, 0, 0, 0xffff);
        if (mp_flag(0x180) == 0) {
            smoke = (d->flags & 0x2000) == 0;
            if (smoke) {
                _VECTOR rot{0.0f, 0.0f, Rand_FRand(6.2831855f), 0.0f};
                Gas_Init(obj->cel, &at, &rot, nullptr, 0.05f, 0.1f, 0x2000039, 7, 0x48403080, 1, 0x80);
            }
        }
    }
    if ((d->flags2 & 0x180) != 0 && target != nullptr) {
        P32<HITDATA_tag> hits = nullptr;
        _VECTOR at;
        AnimGetBoneWorldTrans(b->gun, 0, 0, &at, nullptr);
        const bool hit = Collide_RayIntersect(&at, target, obj->cel, obj, nullptr, &hits, 0, 8, 0);
        if (hit && hits != nullptr) *target = hits->pos;
        if ((d->flags2 & 0x100) != 0) Draw_LaserBeam(b, target);
        if ((d->flags2 & 0x80) != 0 && b->taser_hit != 0) Draw_TaserBeam(b, target);
        Collide_FreeHitList(&hits);
    }
    if (mp_flag(0x180) != 0) {
        uint32_t flash = 0;
        const int32_t i = AnimDatumGetWeaponInfo(obj, nullptr, &flash, nullptr);
        if (-1 < i) AnimDatumSetEntity(obj, i + 1, flash, 3);
    }
}

// [decomp] 0x001a3fe0 Draw_MuzzleFlash
// Enquanto BLData::muzzle_time dura, o modelo do clarão (weapon_definition_tag::flash) no objeto
// do clarão, só na câmera do jogador: no osso do cano (0, ou o 4 da outra mão), apontado para onde
// o cano aponta, girado ao acaso em volta dele, com 0,9 a 1,1 de escala e alfa sorteado.
void Draw_MuzzleFlash(obj_tag* obj) {
    BLData* b = bloke(obj);
    const weapon_definition_tag* d = weapon_def(anim_of(obj)->weapon);
    const int8_t player = b->player;
    if (d->flash == 0) return;
    _VECTOR at;
    MATRIX m;
    const uint32_t bone = ((d->flags & 0x40000) == 0 || b->alt_hand != 0) ? 0u : 4u;
    AnimGetBoneWorldTrans(b->gun, bone, 0, &at, &m);
    // Um metro à frente do osso (linha 2 da matriz dele).
    const _VECTOR tip{m.m[2][0] + m.m[3][0], m.m[2][1] + m.m[3][1], m.m[2][2] + m.m[3][2], 0.0f};
    obj_tag* f = b->muzzle_obj;
    f->pos = at;
    Vec_Cartesian_2_Spherical(&f->rot, tip.x - at.x, tip.y - at.y, tip.z - at.z);
    f->rot.x = -f->rot.x;
    f->rot.z = Rand_FRand(6.2831855f);
    View_DrawInThisViewOnly(f, uint32_t(int32_t(player)));
    hashtable_set_object_to_entity_gfx(f, d->flash);
    f->mode |= 0x24;
    f->flags |= 0x2000;
    f->scale = Rand_FRand(0.2f) + 0.9f;
    const _VECTOR aim{f->rot.x, f->rot.y, 0.0f, 0.0f};
    MATRIX a;
    RotMatrixZYX(&aim, &a);
    const _VECTOR roll{0.0f, 0.0f, f->rot.z, 0.0f};
    RotMatrix(&roll, &m);
    MulMatrix0(&a, &m, &f->matrix);
    f->matrix.m[3][0] = f->pos.x;
    f->matrix.m[3][1] = f->pos.y;
    f->matrix.m[3][2] = f->pos.z;
    MatrixScale(&f->matrix, f->scale);
    const float spread = Rand_FRand(float(int32_t(uint32_t(d->flash_alpha_max) - uint32_t(d->flash_alpha_min))));
    f->unknown_103[3] = uint8_t(int32_t(float(d->flash_alpha_min) + spread));
}

void player_weapon_debug_power_on() {
    MAX_AUTO_AIM_DIST_NEAR = 12.0f;
    MAX_AUTO_AIM_DIST_MIDL = 18.0f;
    Autoaim_Range = 35.0f;
    Autoaim_Angle_H = Autoaim_Angle_V = 0.17453f;
    Autoaim_LockOnMul = 2.0f;
    Autoaim_EasyMul = 1.25f;
    Autoaim_NormalMul = 1.0f;
    Autoaim_HardMul = 0.0f;
    Seconds = Minutes = Hours = nullptr;
    CamHint = StickyHint = 0;
    std::memset(Upgrades, 0, sizeof(Upgrades));
    std::memset(RamSave, 0, sizeof(RamSave));
    FlashMod = 0.0f;
}
