// Testes de player_weapon.cpp (inventário, munição, a arma na mão e o gatilho) com definições de
// arma sintéticas em weapon_data, um jogador num mundo vazio e scripts de animação vazios.


#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "platform/keys.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_ads.h"
#include "engine/player_traverse.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapon_wheel.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

constexpr uint32_t kGunSkin = 0x5000100;
constexpr uint32_t kIdle = 0x6000b01;
constexpr uint32_t kFire = 0x6000b02;

// Arquivo de sequência (como em test_anim.cpp): cabeçalho de 0x40 bytes e um registro por canal com
// um só trecho de "frames" quadros, todos os coeficientes nulos.
std::vector<uint32_t> make_seq(int16_t frames, uint8_t skeleton, int channels) {
    std::vector<uint32_t> w(0x10, 0);
    for (int c = 0; c < channels; ++c) w.push_back(4u | (uint32_t(frames) << 6) << 16);
    auto* hdr = reinterpret_cast<uint8_t*>(w.data());
    std::memcpy(hdr + 0x38, &frames, 2);
    const int16_t n = int16_t(channels);
    std::memcpy(hdr + 0x3a, &n, 2);
    hdr[0x3c] = skeleton;
    return w;
}

// Esqueleto de um osso, as peles (Bond, o soco do modo história e a arma) sem modelos, os scripts
// do corpo vazios e os da arma com 30 quadros de uma sequência parada (só scripts com sequência
// andam: o bit de começo do script é limpo por AnimSeqTick).
struct Assets {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    alignas(4) int16_t body[4] = {};
    alignas(4) uint16_t gun[8] = {0, 0, 30, 1, 0, 1, 30, 0x0b00};
    std::vector<uint32_t> seq = make_seq(30, 0, 3);
    Assets() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel);
        k->id = 0;
        k->bone_count = 1;
        pSkeletons[0] = k;
        skin.parents = parents;
        skin.skeleton = 0;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        for (uint32_t h : {0x5000007u, 0x50000abu, kGunSkin}) hashtable_additem(h, &skin);
        body[2] = 30;
        for (uint32_t h : {0x60001dcu, 0x60001ddu, 0x60009ffu, 0x6000a00u}) hashtable_additem(h, body);
        hashtable_additem(0x4000b00, seq.data());
        AnimProcessSeqData(0x4000b00);
        for (uint32_t h : {kIdle, kFire}) hashtable_additem(h, gun);
    }
};

void set_ammo_max(uint32_t type, int16_t max) { std::memcpy(ammo_data + type * 0xc + 2, &max, 2); }

// Pistola (2, classe 2): pente de 7, munição do tipo 3 (até 50), um tiro por aperto, 10 ticks entre
// tiros. 0x14: rajada de 3. A 0x13 existe mas o jogador não a recebe (nenhuma das duas está na lista
// de Player_InitWeapon para fases sem regra própria).
void define_weapons() {
    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    for (int w : {2, 0x13, 0x14}) {
        weapon_definition_tag* d = weapon_def(w);
        d->id = int16_t(w);
        d->klass = int16_t(w);
        d->selectable = 1;
        d->skin = kGunSkin;
        d->clip = 7;
        d->ammo_type = 3;
        d->rounds_per_shot = 1;
        d->bullets = 1;
        d->fire_delay = 10;
        d->burst[0] = w == 0x14 ? 3 : 1;
        d->modes = 1;
        d->anim_idle = kIdle;
        d->anim_fire = kFire;
        d->zoom_max = 4.0f;
    }
    weapon_def(1)->id = 1;
    weapon_def(1)->klass = 1;
    weapon_def(1)->selectable = 1;
    set_ammo_max(3, 50);
}

struct WeaponWorld {
    obj_tag* player = nullptr;
    BLData* b = nullptr;
    level_tag level{};
    WeaponWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        view_debug_power_on();
        player_debug_power_on();
        player_weapon_debug_power_on();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        hashtable_debug_power_on();
        hashtable_initialise();
        anim_debug_power_on();
        static Assets* assets = nullptr;
        delete assets;
        assets = new Assets;
        define_weapons();
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        Input_Init();
        GS_SetRefreshRate(60, 60);
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        _VECTOR pos{1.0f, 10.0f, 2.0f, 1.0f};
        _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
        // A camada 2D como no ResetMap_GameInit (o HUD do Player_Init cria sprites; as mensagens vão
        // para a fila de textos): sem isso as listas seriam as de um heap anterior.
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        // A fase do teste tem missão em andamento (Mission_Init): a vida pode mudar (PlrStat_OkToUpdate).
        Mission_SetStatus(kMissionRunning);
        player = Player_Init(0, &pos, &rot, &level);
        b = player != nullptr ? bloke_of(player) : nullptr;
        if (player != nullptr && anim_of(player) != nullptr) {
            anim_of(player)->root_offset = 1.1f;
            anim_of(player)->root_height = 1.1f;
        }
    }
    void press(int vk, bool down) {
        g_psi_keyboard_mouse.active = true;
        g_psi_keyboard_mouse.down[vk] = down;
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            ++GameState.frame;
            Input_Update();
            Player_Update(player);
            Player_CollisionHandler(player);
            Camera_Update(0);
        }
    }
    ANIMOBJ_tag* anim() const { return anim_of(player); }
};

}  // namespace

TEST(weapon_upgrade_tables) {
    player_weapon_debug_power_on();
    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(&GameState, 0, sizeof(GameState));
    // Cada nível da pistola é outra arma (UpgradedHandguns: 2, 4, 6, 8).
    const uint32_t handguns[4] = {2, 4, 6, 8};
    for (uint8_t level = 0; level < 4; ++level) {
        Upgrades[0] = level;
        CHECK(Upgrade_Weapon(6) == handguns[level]);
    }
    Upgrades[7] = 1;
    CHECK(Upgrade_Weapon(0x4e) == 0x4f);
    CHECK(Upgrade_Weapon(0x30) == 0x30);  // sem melhoria
    CHECK(Upgrade_Weapon(10) == 10);
    GameState.unknown_52 = 1;
    CHECK(Upgrade_Weapon(10) == 0xc);
    // A 0x54 não muda, mas a melhoria dobra o zoom dela e da 0x55.
    CHECK(Upgrade_Weapon(0x54) == 0x54 && weapon_def(0x54)->zoom_max == 8.0f && weapon_def(0x55)->zoom_max == 8.0f);
    Upgrades[2] = 1;
    Upgrade_Weapon(0x54);
    CHECK(weapon_def(0x54)->zoom_max == 16.0f && weapon_def(0x55)->zoom_max == 16.0f);
    // Um nível fora das tabelas deixa a arma como está (divergência documentada).
    Upgrades[0] = 200;
    CHECK(Upgrade_Weapon(6) == 6);
    player_weapon_debug_power_on();
}

TEST(weapon_equip_fills_clip_then_ammo) {
    WeaponWorld w;
    CHECK(w.player != nullptr);
    BLData* b = w.b;
    Player_InitAmmoWeapons(w.player);
    CHECK(b->weapons[2].have == 0 && b->ammo[3] == 0);
    CHECK(b->weapons[2].zoom == 2.0f);  // raiz do zoom máximo
    // Arma nova: pente cheio, o resto vai para a munição.
    CHECK(Player_EquipWeapon(b, 2, 30) == 1);
    CHECK(b->weapons[2].have == 1 && b->weapons[2].rounds == 7 && b->ammo[3] == 23);
    // Já tinha: só a munição, até o máximo do tipo.
    CHECK(Player_EquipWeapon(b, 2, 40) == 1);
    CHECK(b->weapons[2].rounds == 7 && b->ammo[3] == 50);
    CHECK(Player_EquipWeapon(b, 2, 5) == 0);  // cheia
    // Sem o modelo carregado não pega.
    weapon_def(0x13)->skin = 0x5000999;
    CHECK(Player_EquipWeapon(b, 0x13, 10) == 0 && b->weapons[0x13].have == 0);
    // Munição avulsa: entra o que cabe e devolve quanto entrou.
    b->ammo[3] = 45;
    CHECK(Player_EquipAmmo(b, 2, 10) == 5 && b->ammo[3] == 50);
}

TEST(weapon_reload_and_rounds) {
    WeaponWorld w;
    BLData* b = w.b;
    obj_tag* p = w.player;
    w.anim()->weapon = 2;
    b->weapons[2].rounds = 2;
    b->ammo[3] = 10;
    // Só testar não mexe em nada.
    CHECK(Player_ReloadAmmoType(p, 1) == 1 && b->weapons[2].rounds == 2 && b->ammo[3] == 10);
    CHECK(Player_ReloadAmmoType(p, 0) == 1 && b->weapons[2].rounds == 7 && b->ammo[3] == 5);
    CHECK(Player_ReloadAmmoType(p, 0) == 0);  // pente cheio
    b->ammo[3] = 0;
    b->weapons[2].rounds = 0;
    CHECK(Player_ReloadAmmoType(p, 1) == 0);
    // Atirar tira do pente; vazio não atira; com a trapaça de munição infinita não tira.
    b->weapons[2].rounds = 1;
    CHECK(Player_RoundToFire(p, 1, 1) == 1 && b->weapons[2].rounds == 0);
    CHECK(Player_RoundToFire(p, 1, 1) == 0);
    b->weapons[2].rounds = 3;
    const uint32_t on = 1;
    std::memcpy(CheatInfo + 8, &on, 4);
    CHECK(Player_RoundToFire(p, 1, 1) == 1 && b->weapons[2].rounds == 3);
    std::memset(CheatInfo, 0, sizeof(CheatInfo));
    // Uma arma que não gasta munição sempre atira.
    w.anim()->weapon = 1;
    CHECK(Player_RoundToFire(p, 1, 1) == 1);
}

TEST(weapon_init_puts_handgun_in_hand) {
    WeaponWorld w;
    BLData* b = w.b;
    CHECK(b->gun != nullptr && b->glow != nullptr && b->muzzle_obj != nullptr);
    for (obj_tag* fx : b->fx) CHECK(fx != nullptr);
    // Fase sem regra própria: a pistola melhorada (2) é pedida; a troca acontece nos ticks.
    CHECK(w.anim()->weapon == kWeaponNone && w.anim()->next_weapon == 2);
    CHECK(b->weapons[2].have == 1 && b->weapons[2].rounds == 7);
    w.tick();
    // Sem animação de guardar a anterior: troca o modelo (estado 3) e, sem animação de sacar, fica
    // parada (0) com a pele da arma na mão.
    w.tick(2);
    CHECK(w.anim()->weapon == 2);
    CHECK(anim_of(b->gun) != nullptr && anim_of(b->gun)->skin != nullptr);
    CHECK(b->gun->state == 0);
    CHECK((b->gun->flags & 0x1a0a0) == 0x1a0a0);
}

TEST(weapon_semi_auto_fires_once_per_press) {
    WeaponWorld w;
    BLData* b = w.b;
    w.tick(3);
    CHECK(w.anim()->weapon == 2 && b->weapons[2].rounds == 7);
    // Segurar o gatilho: um tiro (rajada de 1), a animação de tiro (estado 9) e o clarão.
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(b->weapons[2].rounds == 6);
    CHECK(b->gun->state == 9 && b->muzzle_flash == 1);
    w.tick(40);
    CHECK(b->weapons[2].rounds == 6);
    // Soltar e apertar de novo depois da animação (30 quadros): outro tiro.
    w.press(nf::key::LButton, false);
    w.tick();
    CHECK(b->gun->state == 0);
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(b->weapons[2].rounds == 5);
    w.press(nf::key::LButton, false);
}

TEST(weapon_burst_and_empty_clip_reload) {
    WeaponWorld w;
    BLData* b = w.b;
    Player_EquipWeapon(b, 0x14, 20);
    w.anim()->next_weapon = 0x14;
    w.tick(3);
    CHECK(w.anim()->weapon == 0x14 && b->weapons[0x14].rounds == 7);
    // Rajada de 3 com o gatilho seguro: três tiros a cada fire_delay (10 ticks) e mais nada.
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(b->weapons[0x14].rounds == 6);
    w.tick(9);
    CHECK(b->weapons[0x14].rounds == 6);
    w.tick();
    CHECK(b->weapons[0x14].rounds == 5);
    w.tick(10);
    CHECK(b->weapons[0x14].rounds == 4);
    w.tick(40);
    CHECK(b->weapons[0x14].rounds == 4);
    // Soltar no meio da rajada a interrompe (BLData::burst volta a 0).
    w.press(nf::key::LButton, false);
    w.tick();
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(b->weapons[0x14].rounds == 3);
    w.press(nf::key::LButton, false);
    w.tick(30);
    CHECK(b->weapons[0x14].rounds == 3);
    // Pente vazio e gatilho solto: recarrega sozinho com a munição guardada (estado 6; sem
    // animação de recarga a munição passa no tick seguinte).
    b->weapons[0x14].rounds = 0;
    const int16_t stored = b->ammo[3];
    w.tick();
    CHECK(b->gun->state == 6);
    w.tick();
    CHECK(b->weapons[0x14].rounds == 7 && b->ammo[3] == stored - 7);
}

TEST(weapon_change_skips_missing) {
    WeaponWorld w;
    BLData* b = w.b;
    Player_EquipWeapon(b, 0x14, 20);
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    // A 0x13 está entre as duas mas o jogador não a tem.
    Player_WeaponChange(w.player, 1, 0);
    CHECK(w.anim()->next_weapon == 0x14 && w.anim()->prev_weapon == 2);
    w.tick(3);
    CHECK(w.anim()->weapon == 0x14);
    // Para trás volta à pistola; sem munição nenhuma as duas ficam de fora e sobra o soco (1), que
    // não gasta munição.
    Player_WeaponChange(w.player, -1, 0);
    CHECK(w.anim()->next_weapon == 2);
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    b->weapons[2].rounds = 0;
    b->weapons[0x14].rounds = 0;
    b->ammo[3] = 0;
    Player_WeaponChange(w.player, 1, 0);
    CHECK(w.anim()->next_weapon == 1);
}

namespace {
// Uma parede de esgueirar (tipo 0x2b) encostada: com touch_type 6, Player_Activate gruda nela (o "usar" acha o que
// usar, como uma porta na frente) e devolve 1.
obj_tag* creep_wall_at(WeaponWorld& w) {
    _VECTOR pos = w.player->pos;
    _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
    obj_tag* o = control_create_object(4, &pos, &rot, nullptr);
    o->type = 0x2b;
    o->sphere = {pos.x, pos.y, pos.z, 2.0f};
    return o;
}
}  // namespace

TEST(weapon_reload_key_reloads_without_using) {
    // O "usar" (E) do PS2 recarrega só quando nada foi usado: encostado na parede de esgueirar, ele a usa e não recarrega.
    WeaponWorld w;
    BLData* b = w.b;
    w.tick(3);
    CHECK(w.anim()->weapon == 2 && b->gun->state == 0);
    creep_wall_at(w);
    b->weapons[2].rounds = 3;
    b->ammo[3] = 20;
    b->traverse_wait = 30;  // Player_CollCreepWall não gruda (o uso conta, mas o jogador fica onde está)
    b->touch_type = 6;
    w.press('E', true);
    w.tick();
    w.press('E', false);
    CHECK(b->gun->state == 0 && b->weapons[2].rounds == 3);
    w.tick();
    // O "reload" (R, só do PC) recarrega pelo mesmo caminho, sem usar nada: com a parede ali, nada de esgueirar.
    b->traverse_wait = 0;
    b->touch_type = 6;
    const uint32_t creeps = g_traverse_stats.creeps;
    w.press('R', true);
    w.tick();
    CHECK(b->gun->state == 6 && int16_t(w.player->unknown_f6) != kPlrCreep && g_traverse_stats.creeps == creeps);
    w.tick();
    CHECK(b->weapons[2].rounds == 7 && b->ammo[3] == 16);
    // Segurar R não recarrega de novo (só a borda); com o pente cheio, nada.
    b->weapons[2].rounds = 5;
    w.tick(3);
    CHECK(b->weapons[2].rounds == 5);
    w.press('R', false);
    w.tick();
    b->weapons[2].rounds = 7;
    w.press('R', true);
    w.tick();
    CHECK(b->gun->state == 0 && b->ammo[3] == 16);
    w.press('R', false);
}

TEST(weapon_wheel_switches_or_zooms_while_aiming) {
    WeaponWorld w;
    BLData* b = w.b;
    Player_EquipWeapon(b, 0x14, 20);
    g_psi_keyboard_mouse.active = true;
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    // Roda para baixo: a próxima arma (a 0x14); para cima: a anterior (a pistola).
    g_psi_keyboard_mouse.wheel = -1.0f;
    w.tick();
    CHECK(w.anim()->next_weapon == 0x14);
    w.tick(3);
    CHECK(w.anim()->weapon == 0x14);
    g_psi_keyboard_mouse.wheel = 1.0f;
    w.tick();
    CHECK(w.anim()->next_weapon == 2);
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    // Mirando com uma arma de zoom (zoom máximo 4), a roda dá zoom: cada dente para cima, x 1,025^9 no alvo; nada de
    // trocar de arma; a classe guarda o alvo.
    w.press(nf::key::RButton, true);
    w.tick(2);
    CHECK((w.anim()->flags & 1) != 0 && b->zoom_max == 4.0f);
    const float before = b->zoom_target;
    g_psi_keyboard_mouse.wheel = 1.0f;
    w.tick();
    CHECK(w.anim()->next_weapon == 2 && w.anim()->weapon == 2);
    CHECK_NEAR(b->zoom_target / before, nf::fm::pow(1.025, 9.0), 1e-4);
    CHECK(b->weapons[2].zoom == b->zoom_target);
    // O zoom anda até o alvo pelo caminho do jogo (15% por tick).
    w.tick(10);
    CHECK_NEAR(b->zoom, b->zoom_target, 1e-4);
    // Para baixo afasta, até 1; para cima, até o máximo da arma.
    g_psi_keyboard_mouse.wheel = -10.0f;
    w.tick();
    CHECK(b->zoom_target == 1.0f);
    g_psi_keyboard_mouse.wheel = 20.0f;
    w.tick();
    CHECK(b->zoom_target == 4.0f && w.anim()->weapon == 2);
    // Sem mirar, a roda volta a trocar de arma.
    w.press(nf::key::RButton, false);
    w.tick(3);
    g_psi_keyboard_mouse.wheel = -1.0f;
    w.tick();
    CHECK(w.anim()->next_weapon == 0x14);
}

TEST(weapon_gadget_keys_pick_the_nth_owned_gadget) {
    WeaponWorld w;
    BLData* b = w.b;
    // Três aparelhos na ordem do ciclo do jogo (0x4a ... 0x5e): 0x4a, 0x50 e 0x58; a 0x54 existe mas o jogador não a tem.
    for (int g : {0x4a, 0x50, 0x54, 0x58}) {
        weapon_definition_tag* d = weapon_def(g);
        d->id = int16_t(g);
        d->klass = int16_t(g);
        d->selectable = 1;
        d->skin = kGunSkin;
        d->anim_idle = kIdle;
        d->burst[0] = 1;
        d->modes = 1;
    }
    for (int g : {0x58, 0x4a, 0x50}) b->weapons[g].have = 1;
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    // 2: o segundo (0x50); a troca passa pela máquina de estados da arma na mão.
    w.press('2', true);
    w.tick();
    w.press('2', false);
    CHECK(w.anim()->next_weapon == 0x50 && w.anim()->prev_weapon == 2 && b->last_weapon == 2);
    w.tick(3);
    CHECK(w.anim()->weapon == 0x50);
    // 3: o terceiro (0x58: a 0x54 não conta); 1: o primeiro.
    w.press('3', true);
    w.tick();
    w.press('3', false);
    CHECK(w.anim()->next_weapon == 0x58 && b->last_gadget == 0x50);
    w.tick(3);
    w.press('1', true);
    w.tick();
    w.press('1', false);
    CHECK(w.anim()->next_weapon == 0x4a);
    w.tick(3);
    CHECK(w.anim()->weapon == 0x4a);
    // 4 e 5: espaços vazios, nada muda; o que já está na mão, nada.
    for (int k : {'4', '5', '1'}) {
        w.press(k, true);
        w.tick();
        w.press(k, false);
        w.tick();
        CHECK(w.anim()->next_weapon == 0x4a && w.anim()->weapon == 0x4a);
    }
    // Uma arma da mão volta pelo ciclo das armas (Player_WeaponChange do grupo 0 volta à última arma).
    Player_WeaponChange(w.player, 1, 0);
    CHECK(w.anim()->next_weapon == 2);
    // Mirando, nada (como a troca do jogo).
    w.tick(3);
    w.anim()->flags |= 1;
    CHECK(!Player_PortSelectGadget(w.player, 2));
    w.anim()->flags &= 0xfffe;
    CHECK(Player_PortSelectGadget(w.player, 2) && w.anim()->next_weapon == 0x50);
}

// ---- a mira moderna (engine/player_ads.h) e a roda de armas (engine/weapon_wheel.h) com o jogador ----

TEST(ads_raises_the_gun_to_the_sights_and_back) {
    WeaponWorld w;
    BLData* b = w.b;
    ads_debug_power_on();
    ads_set_device_override(0);  // teclado e mouse: o estilo auto é o moderno
    const AimStyle style_before = ads_style();
    ads_set_style(AimStyle::Auto);
    // A pistola sem zoom (as do teste têm zoom 4: essas ficam com a visão do jogo) e uma linha de mira para a pele dela.
    weapon_def(2)->zoom_max = 1.0f;
    static const AdsSightEntry kEntry{kGunSkin, {-0.30f, -0.20f, 1.40f}, {-0.30f, -0.18f, 1.65f}, 1.25f, 1, 0.0f};
    ads_debug_extra_entry(&kEntry);
    w.tick(3);
    CHECK(w.anim()->weapon == 2 && w.b->gun->state == 0);
    const _VECTOR hip = b->gun->pos;
    CHECK(ads_weight() == 0.0f && ads_view_zoom() == 1.0f && !ads_hide_crosshair(b));
    // Segurar o botão de mirar: o modo de mira do jogo liga, e a arma vai para a pose em ~10 ticks.
    w.press(nf::key::RButton, true);
    w.tick();
    CHECK((w.anim()->flags & 1) != 0);
    w.tick(5);
    CHECK(ads_weight() > 0.2f && ads_weight() < 1.0f);
    w.tick(6);
    CHECK_NEAR(ads_weight(), 1.0f, 1e-6);
    CHECK_NEAR(ads_view_zoom(), 1.25f, 1e-5);
    CHECK(ads_hide_crosshair(b));
    CHECK_NEAR(ads_effective_zoom(b), b->zoom * 1.25f, 1e-6);
    // A alça e a massa no centro (com um quarto do balanço de parado, uns milímetros), a alça na profundidade do quadril
    // x 0,85 (presa a 0,8 m) vezes o zoom.
    float r[3], f[3];
    const float rot[3] = {b->gun->rot.x, b->gun->rot.y, b->gun->rot.z};
    const float pos[3] = {b->gun->pos.x, b->gun->pos.y, b->gun->pos.z};
    ads_view_point(rot, pos, kEntry.rear, r);
    ads_view_point(rot, pos, kEntry.front, f);
    CHECK(std::fabs(r[0]) < 0.006f && std::fabs(r[1]) < 0.006f);
    CHECK(std::fabs(f[0]) < 0.006f && std::fabs(f[1]) < 0.006f);
    CHECK_NEAR(r[2], 0.8f * 1.25f, 0.01);
    CHECK(f[2] > r[2]);
    // A mira fica no centro: as setas giram a visão (como na luneta) e a mira do jogo não sai do lugar.
    const float yaw0 = w.player->rot.y;
    w.press(nf::key::Right, true);
    w.tick(4);
    w.press(nf::key::Right, false);
    CHECK(b->aim_x == 0.0f && b->aim_y == 0.0f && w.player->rot.y != yaw0);
    // Só do port: mirando no estilo moderno o W anda (o modo de mira do jogo zera a frente em Player_Move).
    const _VECTOR at0 = w.player->pos;
    w.press('W', true);
    w.tick(10);
    w.press('W', false);
    CHECK(std::hypot(w.player->pos.x - at0.x, w.player->pos.z - at0.z) > 0.05f);
    // Recarregar tira a arma da pose (o botão ainda seguro); no fim da recarga ela volta (os ticks da pose direto: a
    // pistola do teste não tem animação de recarga, a máquina de estados sairia do 6 no mesmo tick).
    b->gun->state = 6;
    for (int i = 0; i < 3; ++i) ads_tick(w.player, nullptr);
    CHECK(ads_weight() < 1.0f && ads_weight() > 0.0f);
    b->gun->state = 0;
    for (int i = 0; i < 12; ++i) ads_tick(w.player, nullptr);
    CHECK_NEAR(ads_weight(), 1.0f, 1e-6);
    // Soltar: a arma volta ao quadril e o zoom leve some.
    w.press(nf::key::RButton, false);
    w.tick(12);
    CHECK(ads_weight() == 0.0f && ads_view_zoom() == 1.0f);
    CHECK_NEAR(b->gun->pos.x, hip.x, 0.02);
    CHECK_NEAR(b->gun->pos.z, hip.z, 0.02);
    // Com o controle (auto = clássico) nada disso: a mira do jogo se move pela tela e a arma fica no quadril.
    ads_set_device_override(1);
    w.press(nf::key::RButton, true);
    w.tick(12);
    CHECK((w.anim()->flags & 1) != 0 && ads_weight() == 0.0f && !ads_fixed_crosshair(w.player));
    // ... e mirando a frente fica zerada, como no PS2: só se anda de lado.
    const _VECTOR at1 = w.player->pos;
    w.press('W', true);
    w.tick(10);
    w.press('W', false);
    CHECK(std::hypot(w.player->pos.x - at1.x, w.player->pos.z - at1.z) < 0.01f);
    w.press(nf::key::Right, true);
    w.tick(2);
    w.press(nf::key::Right, false);
    CHECK(b->aim_x != 0.0f);
    w.press(nf::key::RButton, false);
    w.tick(3);
    // Uma arma de zoom (a 0x14 com zoom 4) no estilo moderno: a mira presa no centro, mas sem a pose (a visão de zoom do
    // jogo).
    ads_set_device_override(0);
    CHECK(ads_fixed_crosshair(w.player));
    weapon_def(2)->zoom_max = 4.0f;
    w.press(nf::key::RButton, true);
    w.tick(12);
    CHECK(ads_fixed_crosshair(w.player) && ads_weight() == 0.0f);
    w.press(nf::key::RButton, false);
    ads_debug_extra_entry(nullptr);
    ads_set_device_override(-1);
    ads_set_style(style_before);
}

TEST(weapon_wheel_release_selects_through_the_game_path) {
    WeaponWorld w;
    BLData* b = w.b;
    Player_EquipWeapon(b, 0x14, 20);
    for (int g : {0x4a, 0x50}) {
        weapon_definition_tag* d = weapon_def(g);
        d->id = int16_t(g);
        d->klass = int16_t(g);
        d->selectable = 1;
        d->skin = kGunSkin;
        d->anim_idle = kIdle;
        d->burst[0] = 1;
        d->modes = 1;
        b->weapons[g].have = 1;
    }
    w.tick(3);
    CHECK(w.anim()->weapon == 2);
    WeaponWheel wheel;
    const float dt = 1.0f / 60.0f;
    // Segurar abre a roda com as armas que o jogador tem (a pistola na mão, a 0x14; a 0x13 não) e os dois aparelhos.
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(wheel.open() && wheel.slowmo() && wheel.count(true) == 2);
    int pistol = -1, other = -1;
    for (int i = 0; i < wheel.count(false); ++i) {
        const WheelSlot& s = wheel.slots(false)[i];
        CHECK(s.weapon != 0x13);
        if (s.weapon == 2) pistol = i;
        if (s.weapon == 0x14) other = i;
    }
    CHECK(pistol >= 0 && other >= 0 && wheel.slots(false)[pistol].current && !wheel.slots(false)[other].current);
    CHECK(wheel.slots(false)[other].usable && wheel.slots(false)[other].clip == 7);
    // O mouse aponta a 0x14 (contagens até a borda na direção do espaço; y da tela para baixo).
    const float a = weapon_wheel_item_angle(other, wheel.count(false), false);
    wheel.frame(dt, true, nf::fm::sin(a) * WeaponWheel::kMouseCounts, -nf::fm::cos(a) * WeaponWheel::kMouseCounts, 0.0f, 0.0f,
                w.player);
    CHECK(wheel.hovered().ring == 2 && wheel.hovered().index == other);
    // Soltar escolhe pelo caminho da troca do jogo: next_weapon, a anterior e a memória da última arma.
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(!wheel.open() && wheel.choices() == 1 && wheel.last_choice() == 0x14);
    CHECK(w.anim()->next_weapon == 0x14 && w.anim()->prev_weapon == 2 && b->last_weapon == 2);
    w.tick(4);
    CHECK(w.anim()->weapon == 0x14);
    // Soltar no meio (nada apontado): nada muda.
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(wheel.choices() == 1 && w.anim()->next_weapon == 0x14);
    // Apontar um espaço vazio (os espaços são fixos: com poucas armas os de baixo ficam vazios) e soltar: nada muda.
    CHECK(wheel.count(false) < 6);
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    const float ea = weapon_wheel_slot_angle(5, weapon_wheel_ring_slots(wheel.count(false), false));
    wheel.frame(dt, true, nf::fm::sin(ea) * WeaponWheel::kMouseCounts, -nf::fm::cos(ea) * WeaponWheel::kMouseCounts, 0.0f, 0.0f,
                w.player);
    CHECK(wheel.hovered().ring == 2 && wheel.hovered().index == 5);
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(wheel.choices() == 1 && w.anim()->next_weapon == 0x14);
    // O analógico direito aponta os aparelhos com meio empurrão (o anel de dentro): o segundo (0x50), à direita... depois
    // de soltar o analógico o espaço apontado fica.
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    const float ga = weapon_wheel_item_angle(1, 2, true);
    wheel.frame(dt, true, 0.0f, 0.0f, nf::fm::sin(ga) * 0.6f, nf::fm::cos(ga) * 0.6f, w.player);
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(wheel.hovered().ring == 1 && wheel.hovered().index == 1);
    // A arma ocupada ao soltar (recarregando): a escolha espera até ela ficar livre.
    b->gun->state = 6;
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(w.anim()->next_weapon == 0x14);
    b->gun->state = 0;
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(w.anim()->next_weapon == 0x50 && wheel.choices() == 2);
    w.tick(4);
    CHECK(w.anim()->weapon == 0x50 && b->last_weapon == 0x14);
    // Não abre com um menu aberto do jogo ou com o jogador fora do estado de jogo (morrendo).
    w.player->state = 2;
    CHECK(!weapon_wheel_can_open(w.player));
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(!wheel.open());
    w.player->state = 1;
    // Segurando desde antes não abre (só um aperto novo).
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(!wheel.open());
    wheel.frame(dt, false, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    wheel.frame(dt, true, 0.0f, 0.0f, 0.0f, 0.0f, w.player);
    CHECK(wheel.open());
    // Aberta, o jogador anda mas não gira, não mira nem atira (psiInput_SetWheelOpen).
    psiInput_SetWheelOpen(true);
    w.press('W', true);
    w.press(nf::key::Right, true);
    w.press(nf::key::RButton, true);
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(Input_Actionf(0, kActForward, kActionDown) > 0.0f);
    CHECK(Input_Actionf(0, kActTurn, kActionDown) == 0.0f && Input_Action(0, kActAimMode, kActionDown) == 0 &&
          Input_Action(0, kActFire, kActionDown) == 0);
    // Fechada com o botão do mouse ainda seguro: o tiro fica de fora até soltar.
    psiInput_SetWheelOpen(false);
    w.press(nf::key::RButton, false);
    w.tick();
    CHECK(Input_Action(0, kActFire, kActionDown) == 0);
    w.press(nf::key::LButton, false);
    w.tick();
    w.press(nf::key::LButton, true);
    w.tick();
    CHECK(Input_Action(0, kActFire, kActionDown) != 0);
    for (int vk : {int('W'), int(nf::key::Right), int(nf::key::LButton)}) w.press(vk, false);
}
