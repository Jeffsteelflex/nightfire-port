// Testes dos cadeados, do Q-Worm e do alvo da câmera (engine/ssys.h, engine/player_traverse.h): os códigos de Locks_Init,
// o laser esquentando o cadeado, o decodificador achando os algarismos, a chave, o trancar de novo, o Q-Worm revelando
// o código e a foto, com projéteis sintéticos (objetos do tipo 5 com a arma em +0x44 e quem atirou em +0x30).

#include <cstring>
#include <set>
#include <string>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_traverse.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/sprite.h"
#include "engine/ssys.h"
#include "engine/text.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

struct Fixture {
    alignas(16) obj_tag player{};
    alignas(16) BLData bloke{};
    Fixture() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        hashtable_debug_power_on();
        hashtable_initialise();
        sound_debug_power_on();
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        ssys_debug_power_on();
        traverse_debug_power_on();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(GlobalVars, 0, sizeof(GlobalVars));
        std::memset(&GameState, 0, sizeof(GameState));
        GS_SetRefreshRate(60, 60);
        std::memset(weapon_data, 0, sizeof(weapon_data));
        for (int w = 0; w < kWeaponCount; ++w) {
            weapon_def(w)->id = int16_t(w);
            weapon_def(w)->klass = int16_t(w);
        }
        weapon_def(0x55)->klass = 0x54;  // a variante da câmera conta como câmera
        player.type = 3;
        player.data = &bloke;
        std::memset(glb_players, 0, sizeof(glb_players));
        glb_players[0] = &player;
        glb_blokes[0] = &bloke;
    }
    ~Fixture() {
        glb_players[0] = nullptr;
        glb_blokes[0] = nullptr;
    }
    obj_tag* lock(uint16_t kind, uint16_t key, uint16_t channel) {
        level_tag level{};
        level.params[0] = kind;
        level.params[1] = key;
        level.params[5] = channel;
        _VECTOR pos{0.0f, 1.0f, 0.0f, 1.0f};
        _VECTOR rot{};
        return Lock_Create(&pos, &rot, &level, nullptr);
    }
    // Um projétil da arma "weapon", atirado pelo jogador, na lista de acertos de "target".
    obj_tag* shoot(obj_tag* target, int weapon) {
        obj_tag* b = control_create_object(0x100, nullptr, nullptr, nullptr);
        b->type = 5;
        const weapon_definition_tag* w = weapon_def(weapon);
        std::memcpy(static_cast<uint8_t*>(b->data) + 0x44, &w, 4);
        obj_tag* from = &player;
        std::memcpy(static_cast<uint8_t*>(b->data) + 0x30, &from, 4);
        Coll_AddHitToList(&target->hits, 1.0f, nullptr, b);
        return b;
    }
};

uint8_t has_key(const BLData& b, uint16_t key) { return reinterpret_cast<const uint8_t*>(&b)[0x70c + key]; }

}  // namespace

TEST(locks_init_draws_51_four_digit_codes_from_the_seed) {
    Fixture f;
    SEED_X = 1234;
    SEED_Y = 5678;
    GameState.frame = 7;
    Locks_Init();
    std::set<std::string> codes;
    bool digits = true, hidden = true;
    for (int c = 0; c < kKeyCodes; ++c) {
        for (int k = 0; k < 4; ++k) digits = digits && '0' <= KeyCodes[c * 5 + k] && KeyCodes[c * 5 + k] <= '8';
        hidden = hidden && KeyCodes[c * 5 + 4] == 0;
        codes.insert(std::string(KeyCodes + c * 5, 4));
    }
    CHECK(digits && hidden);
    CHECK(codes.size() > 40);  // 9^4 códigos possíveis: quase todos diferentes
    // O mesmo sorteio e o mesmo quadro dão os mesmos códigos.
    char first[sizeof(KeyCodes)];
    std::memcpy(first, KeyCodes, sizeof(first));
    SEED_X = 1234;
    SEED_Y = 5678;
    Locks_Init();
    CHECK(std::memcmp(first, KeyCodes, sizeof(first)) == 0);
}

TEST(laser_heats_the_lock_until_it_opens) {
    Fixture f;
    obj_tag* l = f.lock(1, 7, 0x20);
    CHECK(l != nullptr && l->type == 0x3a && l->state == 0);
    l->unknown_103[1] = l->unknown_103[2] = 0xff;
    // Um tiro do laser (0x4e) por tick tira 3 do verde e do azul do tom.
    int ticks = 0;
    for (; ticks < 200 && l->state == 0; ++ticks) {
        Collide_FreeHitList(&l->hits);
        f.shoot(l, 0x4e);
        Lock_Update(l);
    }
    // Abre quando o azul fica abaixo de 4: 255 - 3n < 4.
    CHECK(ticks == (255 - 4) / 3 + 1);
    CHECK(l->state == 1 && has_key(f.bloke, 7) == 1);
    Collide_FreeHitList(&l->hits);
    Lock_Update(l);  // abrindo: o canal liga e, sem caminho, passa a aberto
    CHECK(switch_channels[0x20] == 1 && l->state == 2);
    CHECK(g_ssys_stats.unlocked == 1);
}

TEST(laser_cools_down_without_hits_and_ignores_other_guns) {
    Fixture f;
    obj_tag* l = f.lock(1, 0, 0);
    l->unknown_103[1] = l->unknown_103[2] = 200;
    f.shoot(l, 2);  // uma pistola não esquenta: o tom volta 1 por tick, como sem tiro
    Lock_Update(l);
    CHECK(l->unknown_103[2] == 201);
    Collide_FreeHitList(&l->hits);
    f.shoot(l, 0x4f);  // o laser melhorado tira 10 e segura o tom por 2 ticks
    Lock_Update(l);
    CHECK(l->unknown_103[2] == 191 && l->unknown_f6 == 1);
    Collide_FreeHitList(&l->hits);
    Lock_Update(l);  // a espera acaba
    CHECK(l->unknown_103[2] == 191);
    Lock_Update(l);  // e o tom volta
    CHECK(l->unknown_103[2] == 192 && l->state == 0);
}

TEST(decryptor_finds_the_code_digit_by_digit) {
    Fixture f;
    SEED_X = 99;
    SEED_Y = 1;
    Locks_Init();
    obj_tag* l = f.lock(2, 5, 0x21);
    // O primeiro tiro do decodificador começa a decodificação (estado 5, o PDA mostra).
    f.shoot(l, 0x56);
    Lock_Update(l);
    CHECK(l->state == 5 && GlobalVars[0x12] == 4 && GlobalVars[0x13] == 0);
    CHECK(std::memcmp(GlobalVars + 8, KeyCodes + 5 * 5, 4) == 0);
    int ticks = 0;
    bool first_digit = false;
    for (; ticks < 1000 && l->state == 5; ++ticks) {
        Lock_Update(l);
        CHECK(GlobalVars[0x14] == 1 || l->state != 5);
        if (!first_digit && GlobalVars[0x13] == 1) {
            first_digit = true;
            // O algarismo achado é o do código.
            CHECK(GlobalVars[0xd] == uint8_t(KeyCodes[5 * 5]));
            // O primeiro leva mais de 4 x 40 ticks de tiro contínuo.
            CHECK(ticks >= 160);
        }
    }
    CHECK(first_digit);
    // Os quatro (e a confirmação): 161 + 116 + 76 + 36 + 36 ticks de tiro com o decodificador simples.
    CHECK(l->state == 1 && ticks > 400 && ticks < 440);
    CHECK(has_key(f.bloke, 5) == 1);
    CHECK(GlobalVars[0x14] == 0 && GlobalVars[0x13] == 0);  // GlobalVars zerado
    CHECK(g_ssys_stats.decrypted == 5);
}

TEST(decryptor_progress_decays_without_hits) {
    Fixture f;
    obj_tag* l = f.lock(0, 1, 0);
    f.shoot(l, 0x57);  // o decodificador melhorado também começa
    Lock_Update(l);
    CHECK(l->state == 5);
    for (int t = 0; t < 10; ++t) Lock_Update(l);  // +3 por tick
    CHECK(l->unknown_f6 == 30);
    Collide_FreeHitList(&l->hits);
    Lock_Update(l);
    CHECK(l->unknown_f6 == 26);
    for (int t = 0; t < 10; ++t) Lock_Update(l);
    CHECK(l->state == 0 && l->unknown_f6 == 0);
}

TEST(key_opens_and_keypad_lock_relocks_when_its_channel_drops) {
    Fixture f;
    obj_tag* l = f.lock(0, 9, 0x30);
    reinterpret_cast<uint8_t*>(&f.bloke)[0x70c + 9] = 1;
    Lock_Update(l);
    CHECK(l->state == 1);
    Lock_Update(l);
    CHECK(l->state == 2 && switch_channels[0x30] == 1);
    // Canal desligado: o cadeado de teclado tranca e o jogador perde a chave.
    switch_channels[0x30] = 0;
    Lock_Update(l);
    CHECK(l->state == 0 && has_key(f.bloke, 9) == 0);
}

TEST(using_a_lock_without_key_opens_the_keypad_or_plays_locked) {
    Fixture f;
    obj_tag* keypad = f.lock(0, 2, 0);
    Lock_Activate(keypad, &f.player);
    obj_tag* who = nullptr;
    std::memcpy(&who, GlobalVars, 4);
    CHECK(keypad->state == 3 && who == &f.player);
    obj_tag* laser = f.lock(1, 3, 0);
    Lock_Activate(laser, &f.player);
    CHECK(laser->state == 0);
    reinterpret_cast<uint8_t*>(&f.bloke)[0x70c + 3] = 1;
    Lock_Activate(laser, &f.player);
    CHECK(laser->state == 1);
}

TEST(easy_difficulty_starts_flagged_locks_open) {
    Fixture f;
    GameState.difficulty = 1;
    level_tag level{};
    level.params[0] = 0;
    level.params[6] = 1;
    _VECTOR pos{}, rot{};
    obj_tag* l = Lock_Create(&pos, &rot, &level, nullptr);
    CHECK(l->state == 1);
    GameState.difficulty = 2;
    obj_tag* hard = Lock_Create(&pos, &rot, &level, nullptr);
    CHECK(hard->state == 0);
}

TEST(qworm_reveals_the_code_and_sets_its_channel) {
    Fixture f;
    Locks_Init();
    level_tag level{};
    level.params[0] = 4;     // código 4
    level.params[1] = 0x48;  // canal
    level.params[3] = 1;     // o Q-Worm ativa
    _VECTOR pos{}, rot{};
    obj_tag* w = PCQWorm_Create(&pos, &rot, &level, nullptr);
    CHECK(w != nullptr && w->type == 0x49 && w->state == 0);
    f.shoot(w, 2);
    PCQWorm_Update(w);
    CHECK(w->state == 0);
    f.shoot(w, 0x58);
    PCQWorm_Update(w);
    CHECK(w->state == 1);
    PCQWorm_Update(w);
    CHECK(w->state == 2 && switch_channels[0x48] == 1 && KeyCodes[4 * 5 + 4] == 1);
    // Usar só ativa com o parâmetro 2.
    level.params[2] = 0;
    obj_tag* w2 = PCQWorm_Create(&pos, &rot, &level, nullptr);
    PCQWorm_Activate(w2, &f.player);
    CHECK(w2->state == 0);
    level.params[2] = 1;
    obj_tag* w3 = PCQWorm_Create(&pos, &rot, &level, nullptr);
    PCQWorm_Activate(w3, &f.player);
    CHECK(w3->state == 1);
}

TEST(camera_subject_is_photographed_by_camera_shots_only) {
    Fixture f;
    level_tag level{};
    level.params[0] = 0x1c;
    _VECTOR pos{}, rot{};
    obj_tag* s = CamSubject_Create(&pos, &rot, &level, nullptr);
    CHECK(s != nullptr && s->type == 0x48 && s->unknown_f6 == 0x1c);
    f.shoot(s, 2);
    CamSubject_Update(s);
    CHECK(switch_channels[0x1c] == 0 && (s->life & 1) == 0);
    f.shoot(s, 0x55);  // a câmera com flash (classe 0x54)
    CamSubject_Update(s);
    CHECK(switch_channels[0x1c] == 1 && (s->life & 1) != 0);
}
