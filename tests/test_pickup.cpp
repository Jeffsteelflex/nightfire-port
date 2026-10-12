// Testes dos itens pegáveis (src/engine/pickup.cpp): criação posicionada e solta, queda até o chão,
// o jogador pegando arma, munição, colete, chave e invisibilidade, reaparecimento, vida útil, giro e
// o sorteio do conjunto de armas, com definições de arma sintéticas e uma sala com piso em y = 0.

#include <windows.h>

#include <cmath>
#include <cstring>

#include "engine/sound.h"
#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/mission.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kGunSkin = 0x5000100;
constexpr uint32_t kGunItem = 0x2000500;  // modelo do item das armas (weapon_data +0x80)

int pending(const char* name) {
    const auto& m = pending_counts();
    const auto it = m.find(name);
    return it == m.end() ? 0 : it->second;
}

// Esqueleto de um osso e as peles (Bond, o soco e a arma) sem modelos, e scripts vazios: o bastante
// para Player_Init e Player_EquipWeapon (que exige a pele da arma carregada).
struct Assets {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    alignas(4) int16_t body[4] = {};
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
    }
};

void set_u32(int weapon, uint32_t offset, uint32_t v) { std::memcpy(weapon_data + weapon * kWeaponStride + offset, &v, 4); }

// Armas 2 e 0x33 (classe própria, pente de 7, munição do tipo 3 até 50) com o modelo de item kGunItem.
void define_weapons() {
    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    for (int w = 0; w < int(kWeaponDataCount); ++w) weapon_def(w)->id = int16_t(w);
    for (int w : {2, 0x33}) {
        weapon_definition_tag* d = weapon_def(w);
        d->klass = int16_t(w);
        d->selectable = 1;
        d->skin = kGunSkin;
        d->clip = 7;
        d->ammo_type = 3;
        d->zoom_max = 4.0f;
        set_u32(w, 0x80, kGunItem);
    }
    weapon_def(1)->klass = 1;
    weapon_def(1)->selectable = 1;
    const int16_t max = 50;
    std::memcpy(ammo_data + 3 * kAmmoStride + 2, &max, 2);
}

// Colisão de um quadrado de 20 x 20 no plano y = 0 (como em test_bullet.cpp).
struct Square {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2] = {3, 3};
    colldata_tag coll{};
    celglist_tag glist{};
    Square() {
        const int16_t v[4][3] = {{-1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}, {1000, 0, -1000}};
        std::memcpy(head, v, sizeof(v));
        const int16_t n[3] = {0, 16383, 0};
        std::memcpy(head + 24, n, sizeof(n));
        const float d = 0.0f;
        std::memcpy(head + 30, &d, 4);
        const uint16_t t[2][4] = {{0, 3, 6, 12}, {0, 6, 9, 12}};
        std::memcpy(tris, t, sizeof(t));
        box = {{-10, -1, -10}, int16_t(-0x7fff), 0, {10, 1, 10}, 2, 0, {0, 0, 0}, 0.01f};
        coll.boxes = reinterpret_cast<uint8_t*>(&box);
        coll.list = reinterpret_cast<uint8_t*>(tris);
        coll.rest = surface;
        coll.head = head;
        glist.coll = &coll;
        glist.sphere = {0, 0, 0, 15};
    }
};

// Modelo do item: esfera de raio 0,3 no centro, base 0,1 abaixo da origem.
celglist_tag g_item{};

struct PickupWorld {
    Square floor;
    obj_tag* player = nullptr;
    BLData* b = nullptr;
    level_tag level{};
    PickupWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        view_debug_power_on();
        player_debug_power_on();
        player_weapon_debug_power_on();
        pickup_debug_power_on();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        cel_tag* room = build_alloc_cel(glb_world->first);
        glb_world->first = room;
        room->type = 0x4c023 | 0x8000000;
        room->glist = &floor.glist;
        room->box_min = {-10, -1, -10, 0};
        room->box_max = {10, 20, 10, 0};
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        hashtable_debug_power_on();
        hashtable_initialise();
        anim_debug_power_on();
        static Assets* assets = nullptr;
        delete assets;
        assets = new Assets;
        define_weapons();
        g_item = {};
        g_item.sphere = {0.0f, 0.0f, 0.0f, 0.3f};
        g_item.box_min = {-0.2f, -0.1f, -0.2f, 0.0f};
        g_item.box_max = {0.2f, 0.1f, 0.2f, 0.0f};
        hashtable_additem(kGunItem, &g_item);
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        Input_Init();
        GS_SetRefreshRate(60, 60);
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        _VECTOR pos{5.0f, 1.0f, 5.0f, 1.0f};
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
        if (b != nullptr) {
            Player_InitAmmoWeapons(player);
            WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
        }
        pending_reset();
        g_sound_stats = SoundStats{};
    }
    // Põe o jogador com o quadril em (x, 1.1, z), como de pé no piso.
    void stand(float x, float z) {
        player->pos = {x, 1.1f, z, 1.0f};
        Control_BuildWorldSph(player);
    }
    // Um item posicionado (como o tipo 0xf0 da fase) com a base no piso.
    obj_tag* place(uint16_t kind, uint16_t item, uint32_t count, float x, float z, uint16_t respawn = 0,
                   uint32_t channel = 0) {
        _VECTOR pos{x, 0.1f, z, 0.0f};
        _VECTOR rot{};
        return Pickup_Create(&pos, &rot, nullptr, &g_item, kind, item, count, channel, 0, 1, respawn, 0, 0);
    }
};

bool alive(const obj_tag* obj) {
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o == obj && (o->life & 1) == 0) return true;
    }
    return false;
}

_VECTOR row3(const MATRIX& m) { return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]}; }

}  // namespace

TEST(pickup_create_placed) {
    PickupWorld w;
    CHECK(w.player != nullptr);
    _VECTOR pos{2.0f, 0.5f, 3.0f, 0.0f};
    _VECTOR rot{};
    // Sem modelo, com o item 0xffff ou a ficha 007 sem a medalha de ouro: nada.
    CHECK(Pickup_Create(&pos, &rot, nullptr, nullptr, 0, 2, 7, 0, 0, 1, 0, 0, 0) == nullptr);
    CHECK(Pickup_Create(&pos, &rot, nullptr, &g_item, 0, 0xffff, 7, 0, 0, 1, 0, 0, 0) == nullptr);
    CHECK(Pickup_Create(&pos, &rot, nullptr, &g_item, kPickupBonus, 2, 7, 0, 0, 1, 0, 0, 0) == nullptr);
    g_item.flags = 0x2001;
    obj_tag* obj = Pickup_Create(&pos, &rot, nullptr, &g_item, kPickupWeapon, 2, 30, 9, 0, 1, 4, 0, 0x2000123);
    CHECK(obj != nullptr && obj->type == kPickupObjectType && obj->glist == &g_item);
    PICKUPINFO* p = pickup_of(obj);
    CHECK(p->state == kPickupResting && p->kind == kPickupWeapon && p->item == 2 && p->count == 30);
    CHECK(p->channel == 9 && p->sound == 0xffff && p->respawn == 4 && p->lifetime == 0 && p->label == 0x2000123);
    CHECK(p->mp_index == -1 && p->spin == 0 && p->vel.y == 0.0f);
    // obj->pos fica no chão (1 m abaixo); a matriz fica onde o item foi posto.
    CHECK_NEAR(obj->pos.y, 0.0f, 1e-4);
    CHECK(obj->pos.x == 2.0f && obj->pos.z == 3.0f);
    CHECK(row3(obj->matrix).x == 2.0f && row3(obj->matrix).y == 0.5f && row3(obj->matrix).z == 3.0f);
    // Sem a camada de sobreposição (0x2000), tingido pela sala (0x800) e 0x40; matriz fixa.
    CHECK(obj->flags == ((0x2001u | 0x840u) & ~0x2000u) && (obj->mode & 0x64) == 0x64);
    // offset = linha 3 - centro da esfera (aqui a esfera do modelo está na origem).
    CHECK(p->offset.x == 0.0f && p->offset.y == 0.0f && p->offset.z == 0.0f);
    CHECK(obj->sphere.y == 0.5f && obj->sphere.w == 0.3f);
    // O colete pisca perto da câmera (0x80000) e não é tingido.
    g_item.flags = 0;
    obj_tag* armour = Pickup_Create(&pos, &rot, nullptr, &g_item, kPickupArmour, 0, 0, 0, 0x99, 1, 0, 0, 0);
    CHECK(armour != nullptr && armour->flags == 0x80040u && pickup_of(armour)->sound == 0x99);
    // A fase 07000008 dá 4 do item 0x2c.
    GameState.level_file = 0x7000008;
    obj_tag* gadget = Pickup_Create(&pos, &rot, nullptr, &g_item, kPickupWeapon, 0x2c, 1, 0, 0, 1, 0, 0, 0);
    CHECK(gadget != nullptr && pickup_of(gadget)->count == 4);
}

// A ficha "007 Bonus" (tipo 6) só existe depois da medalha de ouro da missão (PlrStats_HasGoldMedal: o bit
// da medalha 3 da missão em BaseMap na RewardsTable, no perfil do jogador 0) e conta nas estatísticas.
TEST(pickup_bonus_token_needs_the_gold_medal) {
    PickupWorld w;
    const uint32_t mission = 0x7000077;
    std::memset(RewardsTable, 0, sizeof(RewardsTable));
    std::memcpy(RewardsTable, &mission, 4);
    const uint32_t silver = 0x10004, gold = 0x10005;  // medalhas 2 e 3: tipo 1, bits 4 e 5
    std::memcpy(RewardsTable + 4 + 2 * 4, &silver, 4);
    std::memcpy(RewardsTable + 4 + 3 * 4, &gold, 4);
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    const uint32_t base_before = BaseMap;
    BaseMap = mission;
    const auto token = [&] { return w.place(kPickupBonus, 0, 1, 0.5f, 0.5f); };
    CHECK(!PlrStats_HasGoldMedal() && token() == nullptr);
    // A prata não basta.
    uint64_t bits = uint64_t(1) << 4;
    std::memcpy(menu_bonus, &bits, 8);
    CHECK(!PlrStats_HasGoldMedal() && token() == nullptr);
    // O ouro, sim; o perfil é o do jogador 0 (o 1 não conta).
    bits = uint64_t(1) << 5;
    std::memcpy(menu_bonus + 8, &bits, 8);
    CHECK(!PlrStats_HasGoldMedal());
    std::memcpy(menu_bonus, &bits, 8);
    CHECK(PlrStats_HasGoldMedal());
    obj_tag* t = token();
    CHECK(t != nullptr && pickup_of(t)->kind == kPickupBonus);
    // Pega: some e conta uma ficha.
    PlrStat_ResetForMission();
    w.stand(0.0f, 0.0f);
    Pickup_Update(t);
    CHECK(!alive(t) && PlrMissionStats[0].bonus == 1);
    // A medalha de outra missão não vale para esta.
    BaseMap = 0x7000078;
    CHECK(!PlrStats_HasGoldMedal() && token() == nullptr);
    BaseMap = base_before;
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    std::memset(RewardsTable, 0, sizeof(RewardsTable));
}

TEST(pickup_dropped_falls_to_the_floor) {
    PickupWorld w;
    // Solta a 2 m do chão: alinha para cima, desce até a base do modelo (raio 0,3 - 0,1) e cai.
    MATRIX m;
    Mat_Identity(&m);
    m.m[3][0] = 1.0f;
    m.m[3][1] = 2.0f;
    m.m[3][2] = -1.0f;
    m.m[3][3] = 1.0f;
    obj_tag* obj = Pickup_CreateSimple(&m, 2, 5, kPickupWeapon);
    CHECK(obj != nullptr);
    PICKUPINFO* p = pickup_of(obj);
    CHECK(p->state == kPickupFalling && p->sound == 0xf5 && p->respawn == 0 && p->count == 5);
    CHECK(p->lifetime == 120 * 60);  // 2 min
    CHECK_NEAR(row3(obj->matrix).y, 1.8f, 1e-5);
    int ticks = 0;
    while (p->state == kPickupFalling && ticks < 200) {
        ++GameState.frame;
        Pickup_Update(obj);
        ++ticks;
    }
    CHECK(p->state == kPickupResting);
    // Caiu com a gravidade (1,8 m em ~36 ticks a 9,8 m/s²) e pousou com a base no piso, parado.
    CHECK(30 < ticks && ticks < 45);
    CHECK_NEAR(row3(obj->matrix).y + g_item.box_min.y, 0.0f, 1e-4);
    CHECK_NEAR(obj->pos.y, 0.1f, 1e-4);
    CHECK(row3(obj->matrix).x == 1.0f && row3(obj->matrix).z == -1.0f);
    CHECK(p->vel.x == 0.0f && p->vel.y == 0.0f && p->vel.z == 0.0f);
    CHECK(pending("MP_RegisterPickup") == 1);
    const float y = row3(obj->matrix).y;
    Pickup_Update(obj);
    CHECK(row3(obj->matrix).y == y);
}

TEST(pickup_simple_filters_weapons) {
    PickupWorld w;
    MATRIX m;
    Mat_Identity(&m);
    m.m[3][1] = 1.0f;
    CHECK(Pickup_CreateSimple(&m, 2, 0, 0) == nullptr);  // sem balas
    for (int none : {1, 0x45, 0x47, 0x50, 0x51, 0x53, 0, 0x72}) {
        CHECK(Pickup_CreateSimple(&m, int16_t(none), 5, 0) == nullptr);
    }
    // 0x6a e 0x6e viram a 0x33.
    obj_tag* obj = Pickup_CreateSimple(&m, 0x6e, 3, 1);
    CHECK(obj != nullptr && pickup_of(obj)->item == 0x33 && pickup_of(obj)->kind == kPickupAmmo);
    // Sem modelo de item (weapon_data +0x80 não registrado): nada.
    set_u32(0x33, 0x80, 0x2000501);
    CHECK(Pickup_CreateSimple(&m, 0x33, 3, 0) == nullptr);
}

TEST(pickup_player_takes_weapon_then_ammo) {
    PickupWorld w;
    BLData* b = w.b;
    w.stand(0.0f, 0.0f);
    // Longe demais (raio 0,3 + 1,1 do centro da esfera até o quadril): fica.
    obj_tag* far_item = w.place(kPickupWeapon, 2, 30, 0.0f, 1.5f);
    Pickup_Update(far_item);
    CHECK(b->weapons[2].have == 0 && alive(far_item));
    // Perto: pega a arma (pente cheio, o resto na munição), o item some e toca o som 0xf5.
    obj_tag* gun = w.place(kPickupWeapon, 2, 30, 0.5f, 0.5f);
    Pickup_Update(gun);
    CHECK(b->weapons[2].have == 1 && b->weapons[2].rounds == 7 && b->ammo[3] == 23);
    CHECK(!alive(gun) && pickup_of(gun)->sound == 0xf5 && g_sound_stats.calls == 1);
    // A mesma arma de novo vira munição; com a munição cheia o item fica.
    obj_tag* again = w.place(kPickupWeapon, 2, 20, 0.5f, -0.5f);
    Pickup_Update(again);
    CHECK(b->ammo[3] == 43 && !alive(again));
    obj_tag* clip = w.place(kPickupAmmo, 2, 20, -0.5f, -0.5f);
    Pickup_Update(clip);
    CHECK(b->ammo[3] == 50 && !alive(clip));
    obj_tag* full = w.place(kPickupAmmo, 2, 20, -0.5f, 0.5f);
    Pickup_Update(full);
    CHECK(b->ammo[3] == 50 && alive(full) && pickup_of(full)->state == kPickupResting);
    // Morto (tipo 0x12) ou fora do estado 1 não pega.
    b->ammo[3] = 0;
    w.player->type = 0x12;
    Pickup_Update(full);
    CHECK(b->ammo[3] == 0 && alive(full));
    w.player->type = 3;
    w.player->state = 2;
    Pickup_Update(full);
    CHECK(b->ammo[3] == 0 && alive(full));
    w.player->state = 1;
    Pickup_Update(full);
    CHECK(b->ammo[3] == 20 && !alive(full));
    // Sem texto (stubs nulos) nenhuma mensagem vai para a tela.
    CHECK(pending("Text_AddMsg") == 0);
}

TEST(pickup_armour_key_invisibility_and_channel) {
    PickupWorld w;
    BLData* b = w.b;
    w.stand(0.0f, 0.0f);
    b->armour = 10.0f;
    obj_tag* vest = w.place(kPickupArmour, 0, 0, 0.3f, 0.0f, 0, 0x21);
    GameState.frame = 77;
    Pickup_Update(vest);
    CHECK(b->armour == 50.0f && b->unknown_8bc == 1.0f && !alive(vest) && pickup_of(vest)->sound == 0x141);
    CHECK(switch_channels[0x21] == 1 && switch_channels_time[0x21] == 77);
    // Com o colete em 50 o item fica (nem liga o canal).
    obj_tag* vest2 = w.place(kPickupArmour, 0, 0, 0.3f, 0.0f, 0, 0x22);
    Pickup_Update(vest2);
    CHECK(alive(vest2) && switch_channels[0x22] == 0);
    // Chave 5: BLData + 0x70c + 5.
    obj_tag* key = w.place(kPickupKey, 5, 0, -0.3f, 0.0f);
    Pickup_Update(key);
    CHECK(b->unknown_70c[5] == 1 && b->unknown_70c[4] == 0 && !alive(key));
    // Invisibilidade.
    b->visibility = 1.0f;
    obj_tag* invis = w.place(kPickupInvisibility, 0, 0, 0.0f, 0.3f);
    Pickup_Update(invis);
    CHECK(b->visibility == 0.0f && !alive(invis));
    // Item da missão: só o canal.
    obj_tag* mission = w.place(kPickupMission, 0, 0, 0.0f, -0.3f, 0, 0x30);
    Pickup_Update(mission);
    CHECK(switch_channels[0x30] == 1 && !alive(mission));
}

TEST(pickup_respawns_after_tens_of_seconds) {
    PickupWorld w;
    BLData* b = w.b;
    w.stand(0.0f, 0.0f);
    GameState.frame = 1000;
    obj_tag* item = w.place(kPickupAmmo, 2, 5, 0.4f, 0.0f, 1);
    Pickup_Update(item);
    PICKUPINFO* p = pickup_of(item);
    CHECK(b->ammo[3] == 5 && alive(item) && p->state == kPickupRespawning && (item->flags & 0x10) != 0);
    CHECK(item->created == 1000);
    // Escondido por respawn x 10 s (FRAME_RATE_INT x 10 ticks): não é pego de novo.
    GameState.frame = 1000 + 600;
    Pickup_Update(item);
    CHECK(p->state == kPickupRespawning && b->ammo[3] == 5);
    GameState.frame = 1000 + 601;
    Pickup_Update(item);
    CHECK(p->state == kPickupResting && (item->flags & 0x10) == 0 && b->ammo[3] == 5);
    Pickup_Update(item);
    CHECK(b->ammo[3] == 10 && p->state == kPickupRespawning);
    // 0xffff: fica sempre, à vista.
    obj_tag* fixed = w.place(kPickupAmmo, 2, 5, -0.4f, 0.0f, 0xffff);
    Pickup_Update(fixed);
    Pickup_Update(fixed);
    CHECK(b->ammo[3] == 20 && alive(fixed) && pickup_of(fixed)->state == kPickupResting && (fixed->flags & 0x10) == 0);
}

TEST(pickup_lifetime_and_spin) {
    PickupWorld w;
    // Sem jogador por perto, o item solto some quando a vida útil acaba.
    w.stand(8.0f, 8.0f);
    MATRIX m;
    Mat_Identity(&m);
    m.m[3][1] = 0.1f;
    obj_tag* obj = Pickup_CreateSimple(&m, 2, 5, kPickupWeapon);
    CHECK(obj != nullptr);
    pickup_of(obj)->lifetime = 3;
    Pickup_Update(obj);
    Pickup_Update(obj);
    CHECK(alive(obj));
    Pickup_Update(obj);
    CHECK(!alive(obj) && pending("MP_UnregisterPickup") == 1);
    // No multijogador o item gira 1 rad/s em y, sem sair do lugar, e não é tingido pela sala.
    const uint32_t on = 1;
    std::memcpy(MPSettings + 0x180, &on, 4);
    _VECTOR pos{-3.0f, 0.1f, 2.0f, 0.0f};
    _VECTOR rot{};
    const int registered = pending("MP_RegisterPickup");
    obj_tag* spin = Pickup_Create(&pos, &rot, nullptr, &g_item, kPickupWeapon, 2, 5, 0, 0, 1, 0, 0, 0);
    CHECK(spin != nullptr && pickup_of(spin)->spin == 1 && (spin->flags & 0x800) == 0);
    CHECK(pending("MP_RegisterPickup") == registered + 1);  // parado: registrado já na criação
    for (int i = 0; i < 30; ++i) Pickup_Update(spin);
    const float yaw = std::atan2(spin->matrix.m[2][0], spin->matrix.m[2][2]);
    CHECK_NEAR(std::fabs(yaw), 0.5, 1e-3);
    CHECK_NEAR(spin->matrix.m[1][1], 1.0, 1e-5);
    CHECK(row3(spin->matrix).x == -3.0f && row3(spin->matrix).z == 2.0f);
    std::memset(MPSettings, 0, sizeof(MPSettings));
}

TEST(pickup_flash_pulse_and_target) {
    PickupWorld w;
    BLData* b = w.b;
    // FlashMod: pulso de 1 Hz pelo relógio em centésimos, 1 no começo e 0,5 meio segundo depois.
    GameState.start_time = 0;
    GameState.frame = 1;
    Check_Target(w.player);
    CHECK_NEAR(FlashMod, 1.0, 1e-4);
    GameState.start_time = 50;
    Check_Target(w.player);
    CHECK_NEAR(FlashMod, 0.5, 1e-4);
    GameState.start_time = 25;
    Check_Target(w.player);
    CHECK_NEAR(FlashMod, 0.75, 1e-3);
    // Pendurado no gancho (subestado 2) nada muda.
    w.player->unknown_f6 = kPlrGrapple;
    GameState.start_time = 0;
    Check_Target(w.player);
    CHECK_NEAR(FlashMod, 0.75, 1e-3);
    w.player->unknown_f6 = kPlrWalk;
    // A cada 6 ticks a mira é refeita: nada em 50 m à frente, sem alvo e a mira no padrão (0, 2).
    b->target = w.player;
    b->unknown_132 = 1;
    b->unknown_133[0] = 3;
    GameState.frame = 5;
    Check_Target(w.player);
    CHECK(b->target == w.player && b->unknown_132 == 1);
    GameState.frame = 6;
    Check_Target(w.player);
    CHECK(b->target == nullptr && b->unknown_132 == 0 && b->unknown_133[0] == 2);
    FlashMod = 0.0f;
}

TEST(pickup_random_weapon_set) {
    pickup_debug_power_on();
    for (int i = 0; i < 27; ++i) UseableGuns[i] = int16_t(100 + i);
    for (int i = 0; i < 50; ++i) PickupMatrix[i] = int16_t(i);
    for (int round = 0; round < 20; ++round) {
        Pickup_MakeRandomWeaponSet();
        const int16_t* set = PickupMatrix + 50;
        for (int i = 0; i < 5; ++i) {
            CHECK(100 <= set[i] && set[i] < 127);
            for (int k = 0; k < i; ++k) CHECK(set[k] != set[i]);
        }
        CHECK(set[0] != 126);  // a primeira só entre as 26 primeiras
    }
    bool kept = true;
    for (int i = 0; i < 50; ++i) kept = kept && PickupMatrix[i] == i;
    CHECK(kept);
    pickup_debug_power_on();
}
