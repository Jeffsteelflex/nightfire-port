// Testes do dossiê do menu principal (engine/menu_dossier.h) e das trapaças da página de depuração (engine/
// menu_cheats.h), em arquivos de menu feitos à mão como em test_frontend.cpp: os recordes lidos de um perfil gravado,
// as recompensas liberadas pelos bits das medalhas, as rodas e as melhorias, cada caixa C_CHCH* mudando o estado do
// jogo, a página alcançável só com o atalho do port, --unlock-all e os dois modos de movimento das trapaças
// (Player_SSScanMode e Player_ZeroG).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/loadsave.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_cheats.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/pc_text.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_card.h"
#include "platform/psi_file.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

// ---- arquivo de menu (docs/formats.md, "Menus (item tipo 8)") ----
struct MenuFile {
    std::vector<uint8_t> b;
    size_t page_size_at = 0;
    MenuFile() { dw(0xfffffffa); }
    void dw(uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
    }
    void w(uint16_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    }
    void by(uint8_t v) { b.push_back(v); }
    void end_page() {
        if (page_size_at == 0) return;
        const uint32_t size = uint32_t(b.size() - page_size_at - 4);
        std::memcpy(b.data() + page_size_at, &size, 4);
        page_size_at = 0;
    }
    void page(uint32_t id) {
        end_page();
        dw(0xfffffff0);
        dw(id);
        dw(0x80000002);
        dw(0);
        by(0);
        page_size_at = b.size();
        dw(0);
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0) {
        dw(0xfffffff9);
        dw(id);
        by(type);
        by(0);
        w(uint16_t(x));
        w(uint16_t(y));
        w(uint16_t(ww));
        w(uint16_t(h));
        w(20);
        dw(tag);
        w(2);
        dw(0);
        dw(0);
    }
    void finish() {
        end_page();
        dw(0xfffffff2);
        dw(0);
    }
};

uint32_t fourcc(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
M_ITEM* missions() { return reinterpret_cast<M_ITEM*>(sp_level); }

// As missões do menu (sp_level) e as medalhas de quatro delas na RewardsTable, como no ELF: bronze o cartão (tipo 0),
// prata uma roupa (1), ouro uma melhoria (tipo 4: a pistola, grupo do bit 0, em três missões; o grapple, grupo do bit
// 3, numa), platina outra roupa.
constexpr uint32_t kLevels[12] = {0x9000001, 0x7000005, 0x9000005, 0x9000006, 0x7000001, 0x7000009,
                                  0x700000c, 0x7000011, 0x9000002, 0x9000003, 0x7000014, 0x700001b};
void reward(uint32_t entry, uint32_t level, const uint16_t (&bits)[4], const uint16_t (&kinds)[4]) {
    uint8_t* e = RewardsTable + entry * 0x40;
    std::memset(e, 0, 0x40);
    put32(e, level);
    for (uint32_t m = 0; m < 4; ++m) {
        put32(e + 8 + m * 4, uint32_t(bits[m]) | uint32_t(kinds[m]) << 16);
        put32(e + 0x1c + m * 4, 0x2d0 + entry * 4 + m);   // o texto da recompensa
        put32(e + 0x30 + m * 4, 0x3000100 + entry * 4 + m);  // a imagem
    }
}
void fill_tables() {
    // Os rótulos na tabela 0 da tabela de textos dos testes (a 3 do jogo não existe nela).
    for (uint32_t i = 0; i < 12; ++i) missions()[i] = M_ITEM{0x3000080 + i, 0x100 + i, 0x110 + i, kLevels[i], 0, {}, 0};
    missions()[0].enabled = missions()[1].enabled = 1;
    std::memset(RewardsTable, 0, sizeof(RewardsTable));
    reward(0, 0x7000005, {26, 38, 0, 45}, {0, 1, 4, 1});
    reward(1, 0x7000009, {35, 58, 0, 50}, {0, 2, 4, 1});
    reward(2, 0x7000011, {32, 59, 0, 54}, {0, 2, 4, 1});
    reward(3, 0x9000005, {27, 56, 3, 53}, {0, 2, 4, 1});
}

// Uma tabela de textos mínima: o "1" dos marcadores, as medalhas e as linhas das melhorias; o resto vazio.
char kEmptyText[] = "";
char kGold[] = "GOLD MEDAL";
char kUpgradeLine[] = "Upgrade: range";
char kSniperLine[] = "Upgrade: scope";
char kGrappleText[] = "Shoots a wire.";
char kPistolText[] = "Standard handgun.";
char kRewardNames[16][8];  // os textos das recompensas (0x2d0 + entrada x 4 + medalha)
P32<char> g_bank[0x400];
uint32_t g_fixups[1] = {0};

struct DossierWorld {
    MenuFile file;
    nf::MemoryCard card;
    std::vector<uint8_t> icon = std::vector<uint8_t>(0x128f8, 0x5a);
    DossierWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        for (P32<char>& s : g_bank) s = kEmptyText;
        g_bank[0x307] = kGold;
        g_bank[0x3b3] = kUpgradeLine;
        g_bank[0x3b1] = kSniperLine;
        g_bank[0x336] = kGrappleText;
        g_bank[0x359] = kPistolText;
        for (int i = 0; i < 16; ++i) {
            std::snprintf(kRewardNames[i], sizeof(kRewardNames[i]), "R%d", i);
            g_bank[0x2d0 + i] = kRewardNames[i];
        }
        Bank = g_bank;
        NumEntries = 0x400;
        FixupTable = g_fixups;
        NumFixups = 1;
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();  // também o dossiê, as trapaças, o perfil e o cartão
        nf::set_card(&card);
        SetUpIconFile(icon.data(), 0);
        mission_debug_power_on();
        player_stats_debug_power_on();
        for (uint32_t i = 0; i < kScoringTableCount; ++i) {
            ScoringTable[i] = SCORETABLE{};
            ScoringTable[i].level = kLevels[i];
            ScoringTable[i].rows = ScoringRows[i].row;
            ScoringRows[i].row[0].actual = 0;
        }
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = 2;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        std::memset(def_codename, 0, sizeof(def_codename));
        std::memset(Upgrades, 0, sizeof(Upgrades));
        const char* ids[6] = {"PLRS", "MSSN", "MPSG", "GSET", "CHET", "BNUS"};
        SaveIFFBlocks[0] = SAVEIFFBLOCK{1, fourcc(ids[0]), &LS_MakePlrSettings, &LS_LoadPlrSettings};
        SaveIFFBlocks[1] = SAVEIFFBLOCK{2, fourcc(ids[1]), &LS_MakeMission, &LS_LoadMission};
        SaveIFFBlocks[2] = SAVEIFFBLOCK{4, fourcc(ids[2]), &LS_MakeMPSettings, &LS_LoadMPSettings};
        SaveIFFBlocks[3] = SAVEIFFBLOCK{8, fourcc(ids[3]), &LS_MakeGlobalSettings, &LS_LoadGlobalSettings};
        SaveIFFBlocks[4] = SAVEIFFBLOCK{0x10, fourcc(ids[4]), &LS_MakeCheats, &LS_LoadCheats};
        SaveIFFBlocks[5] = SAVEIFFBLOCK{0x20, fourcc(ids[5]), &LS_MakeBonus, &LS_LoadBonus};
        NumSaveBlocks = 6;
        fill_tables();
        for (obj_tag*& p : glb_players) p = nullptr;
        for (BLData*& b : glb_blokes) b = nullptr;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        Input_Init();
        Input_Update();
        MenuManager_Init();
        GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    }
    ~DossierWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        nf::set_card(nullptr);
        SetUpIconFile(nullptr, 0);
        for (uint32_t i = 0; i < kScoringTableCount; ++i) ScoringTable[i] = SCORETABLE{};
        player_stats_debug_power_on();
        mission_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        std::memset(SaveIFFBlocks, 0, sizeof(SaveIFFBlocks));
        NumSaveBlocks = 0;
        std::memset(def_codename, 0, sizeof(def_codename));
        std::memset(sp_level, 0, sizeof(sp_level));
        std::memset(RewardsTable, 0, sizeof(RewardsTable));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        std::memset(Upgrades, 0, sizeof(Upgrades));
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        std::memset(ds_options, 0, sizeof(ds_options));
        std::memset(ds_weapons, 0, sizeof(ds_weapons));
        std::memset(ds_gadgets, 0, sizeof(ds_gadgets));
        for (obj_tag*& p : glb_players) p = nullptr;
        for (BLData*& b : glb_blokes) b = nullptr;
        menu_unlock_everything = 0;
        switch_NO_DRONES = 0;
        switch_BLIND_DRONES = 0;
        switch_SHOW_COORDS = 0;
        switch_ForceDrawAll = 0;
        switch_AllowFreeze = 0;
        Drone_DummyPlayer = 0;
        debug_scanmode = 0;
        debug_clipmode = 0;
        DrawInfo[1] = 0;
        GameState.paused = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load() {
        file.finish();
        MenuManager_Load(0x8000002, file.b.data());
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    M_CONTROL* control(uint32_t id, uint32_t tag = 0) {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
    const char* text(uint32_t id) {
        M_CONTROL* c = control(id);
        if (c == nullptr) return "";
        const char* s = c->type == MCT_MEMO ? reinterpret_cast<M_MEMOBOX*>(c)->text : reinterpret_cast<M_LABEL*>(c)->string;
        return s != nullptr ? s : "";
    }
};

// As páginas do dossiê como no 08000002 (só os controles que os tratadores tocam).
void dossier_pages(MenuFile& f) {
    f.page(0x4000002b);
    f.control(0x4000002b, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000010a, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010b, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010c, MCT_SCROLL, 312, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x1000010d, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x100001ed, MCT_MEMO, 50, 301, 540, 113);
    f.page(0x4000003a);
    f.control(0x4000003a, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000174, MCT_RADIO, 132, 124, 376, 16);
    f.control(0x10000175, MCT_LABEL, 375, 169, 185, 16);
    f.control(0x10000176, MCT_LABEL, 435, 282, 128, 64);
    f.control(0x10000177, MCT_LABEL, 375, 202, 185, 16);
    f.control(0x10000178, MCT_LABEL, 375, 235, 185, 16);
    f.control(0x100001f8, MCT_LABEL, 76, 306, 240, 16);
    f.page(0x4000003b);
    f.control(0x4000003b, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000017a, MCT_RADIO, 132, 93, 376, 16);
    for (uint32_t i = 0; i < 4; ++i) {
        f.control(0x100001d6 + i, MCT_LABEL, 76, int16_t(130 + 72 * i), 51, 51);
        f.control(0x100001d2 + i, MCT_LABEL, 137, int16_t(147 + 72 * i), 453, 16);
    }
    f.page(0x4000003c);
    f.control(0x4000003c, MCT_WINDOW, 0, 0, 640, 480);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x1000016a, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x1000016b, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x1000016c, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000016d, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000016f, MCT_SCROLL, 312, 48, 16, 32);
    f.page(0x4000003d);
    f.control(0x4000003d, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000016e, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x10000170, MCT_LABEL, 114, 118, 150, 150);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x10000171, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x10000172, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x10000173, MCT_SCROLL, 312, 49, 16, 32);
}

uint16_t save_profile(const char* name) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Save_State != 7 && Save_State != 8; ++i) {
        LS_Save(buf, 0, 0xff);
        PS2PollMemoryCard();
        if (PS2CardFlow.message == 4 && PS2CardFlow.answered == 0) {
            PS2CardFlow.answer = 2;
            PS2CardFlow.answered = 1;
        }
    }
    return Save_State;
}

uint16_t load_profile(const char* name) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Load_State != 7 && Load_State != 8; ++i) {
        LS_Load(buf, 0, 0xff);
        PS2PollMemoryCard();
    }
    return Load_State;
}

}  // namespace

TEST(dossier_records_page_reads_the_saved_profile) {
    DossierWorld w;
    // O jogo deixou um recorde e a medalha de ouro em The Exchange (07000005); o perfil grava e lê de volta.
    ScoringTable[1].best = 123456;
    ScoringRows[1].row[0].actual = 4;
    Menu_SetLevelBonus(0x7000005, 3, 0);
    CHECK(save_profile("JAMES") == 7);
    ScoringTable[1].best = 0;
    ScoringRows[1].row[0].actual = 0;
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    CHECK(load_profile("JAMES") == 7);
    CHECK(ScoringTable[1].best == 123456 && Menu_HasMedal(0x7000005, 3, 0) == 1 && Menu_HasMedal(0x7000005, 4, 0) == 0);

    dossier_pages(w.file);
    w.load();
    GameState.unknown_44[1] = 0x7000005;  // a última missão jogada
    MenuManager_Create(0x80000002u, 0x4000003a, -1, 0, 0x28, 0, 5);
    w.tick();
    // O rádio com as 12 missões (todas, mesmo as bloqueadas) na última jogada.
    M_CONTROL* radio = w.control(0x10000174);
    CHECK(radio != nullptr && __Menu_SendMessage(radio, 0x33, 0, 0) == 11);
    CHECK(radio != nullptr && uint32_t(__Menu_SendMessage(radio, 0x35, 0, 0)) == 0x7000005);
    CHECK(std::strcmp(w.text(0x10000175), "123456") == 0);
    CHECK(std::strcmp(w.text(0x10000177), "4") == 0);
    CHECK(std::strcmp(w.text(0x10000178), "1") == 0);  // a melhoria da pistola (o ouro da missão)
    // A melhor medalha: ouro, com o texto e a imagem.
    M_CONTROL* medal = w.control(0x100001f8);
    M_CONTROL* image = w.control(0x10000176);
    CHECK(medal != nullptr && (medal->state & 1) == 0 && std::strcmp(w.text(0x100001f8), "GOLD MEDAL") == 0);
    CHECK(image != nullptr && reinterpret_cast<M_LABEL*>(image)->image == 0x300013e);
    // Outra missão, sem nada: zeros e a medalha some.
    __Menu_SendMessage(radio, 0x1e, 0x700000c, 0);
    CHECK(std::strcmp(w.text(0x10000175), "0") == 0 && std::strcmp(w.text(0x10000178), "0") == 0);
    CHECK((medal->state & 1) != 0 && (image->state & 1) != 0);  // escondidos
    // A platina só conta com as de baixo: só o bit da platina em 07000009 não dá medalha nenhuma.
    REWARDINFO_tag info{};
    PlrStarts_ProcessRewardCounter(reinterpret_cast<uint64_t*>(menu_bonus), 0x7000009, 4, 1, &info);
    __Menu_SendMessage(radio, 0x1e, 0x7000009, 0);
    CHECK((medal->state & 1) != 0);
    CHECK(MenuDossier_Describe(nullptr, 0) > 0);
}

TEST(dossier_rewards_follow_the_medal_bits) {
    DossierWorld w;
    dossier_pages(w.file);
    w.load();
    // Bronze e prata em 07000009: as duas primeiras recompensas, com a imagem e o texto da RewardsTable.
    Menu_SetLevelBonus(0x7000009, 2, 0);
    GameState.unknown_44[1] = 0x7000009;
    MenuManager_Create(0x80000002u, 0x4000003b, -1, 0, 0x28, 0, 5);
    w.tick();
    const auto shown = [&](uint32_t i) {
        M_CONTROL* img = w.control(0x100001d6 + i);
        M_CONTROL* txt = w.control(0x100001d2 + i);
        return img != nullptr && txt != nullptr && (img->state & 1) == 0 && (txt->state & 1) == 0;
    };
    CHECK(shown(0) && shown(1) && !shown(2) && !shown(3));
    CHECK(reinterpret_cast<M_LABEL*>(w.control(0x100001d6))->image == 0x3000104);
    CHECK(reinterpret_cast<M_LABEL*>(w.control(0x100001d7))->image == 0x3000105);
    CHECK(std::strcmp(w.text(0x100001d2), "R4") == 0 && std::strcmp(w.text(0x100001d3), "R5") == 0);
    // Ouro e platina sem as de baixo (07000011): nada aparece.
    REWARDINFO_tag info{};
    PlrStarts_ProcessRewardCounter(reinterpret_cast<uint64_t*>(menu_bonus), 0x7000011, 3, 1, &info);
    PlrStarts_ProcessRewardCounter(reinterpret_cast<uint64_t*>(menu_bonus), 0x7000011, 4, 1, &info);
    __Menu_Send(0, 0x1000017a, 0x1e, 0x7000011, 0);
    CHECK(!shown(0) && !shown(1) && !shown(2) && !shown(3));
    // Todas as quatro em 07000005: todas aparecem.
    Menu_SetLevelBonus(0x7000005, 4, 0);
    __Menu_Send(0, 0x1000017a, 0x1e, 0x7000005, 0);
    CHECK(shown(0) && shown(1) && shown(2) && shown(3));
}

TEST(dossier_wheel_pages_and_upgrade_levels) {
    DossierWorld w;
    // As seções e uns itens das rodas (valor: o grupo de melhoria).
    for (uint32_t i = 0; i < 4; ++i) ds_options[i] = M_ITEM{0x300019b + i, 0x280 + i, 0x200 + i, i, 1, {}, 0};
    for (uint32_t i = 0; i < 14; ++i) ds_gadgets[i] = M_ITEM{0x30000e0 + i, 0x321 + i, 0x32f + i, 0x3f, 1, {}, 0};
    ds_gadgets[2] = M_ITEM{0x30000e2, 0x328, 0x336, 3, 1, {}, 0};  // o grapple
    for (uint32_t i = 0; i < 27; ++i) ds_weapons[i] = M_ITEM{0x30000e8 + i, 0x33d + i, 0x359 + i, 0x3f, 1, {}, 0};
    ds_weapons[0].value = 0;
    // Melhorias: a pistola em duas missões (nível 2), o grapple na missão de carro 09000005.
    Menu_SetLevelBonus(0x7000005, 3, 0);
    Menu_SetLevelBonus(0x7000009, 3, 0);
    Menu_SetLevelBonus(0x9000005, 3, 0);
    CHECK(Menu_GetObjectUpgradeLevel(0, 0) == 2);
    CHECK(Menu_GetObjectUpgradeLevel(3, 0) == 1);
    CHECK(Menu_GetObjectUpgradeLevel(9, 0) == 0);
    CHECK(Menu_GetObjectUpgradeLevel(0x3f, 0) == 0);  // fora dos 63 bits
    CHECK(Upgrades[0] == 2 && Upgrades[1] == 1);      // Menu_SetBonus: Set_Upgrade(bit / 3, contagem)

    dossier_pages(w.file);
    w.load();
    MenuManager_Create(0x80000002u, 0x4000002b, -1, 0, 0x28, 0, 5);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x1000010c, 0);
    w.tick(2);
    M_SCROLL* wheel = reinterpret_cast<M_SCROLL*>(w.control(0x1000010c));
    CHECK(wheel != nullptr && wheel->min == 0 && wheel->max == 3);
    // Os recordes: a página na hora; voltar volta ao dossiê.
    __Menu_Send(0, 0x1000010c, 0x2e, 0, 0);
    C_SBDOSSIER_Handler(0, &wheel->base, 0x1000010c, 0x4b, 0, 0);
    CHECK(Menu_CurrentPage(0) == 0x4000003a);
    Manager_SendMessage(&menu_manager[0], 0x5f, 0, 0);
    w.tick();
    CHECK(Menu_CurrentPage(0) == 0x4000002b);
    // Os aparelhos: o íris fecha e a página vem 10 ticks depois; o grapple tem a linha da melhoria.
    wheel = reinterpret_cast<M_SCROLL*>(w.control(0x1000010c));
    __Menu_Send(0, 0x1000010c, 0x2e, 2, 0);
    C_SBDOSSIER_Handler(0, &wheel->base, 0x1000010c, 0x4b, 0, 0);
    CHECK(Menu_CurrentPage(0) == 0x4000002b);
    w.tick(12);
    CHECK(Menu_CurrentPage(0) == 0x4000003c);
    CHECK(ds_gadgets[6].image == 0x300018f);  // a 60 Hz
    M_CONTROL* gadgets = w.control(0x1000016f);
    CHECK(gadgets != nullptr && reinterpret_cast<M_SCROLL*>(gadgets)->max == 0xd);
    __Menu_SendMessage(gadgets, 0x2e, 2, 0);
    C_SBDSGTSCROLL_Handler(0, gadgets, 0x1000016f, 0x49, 0, 0);
    CHECK(std::strcmp(w.text(0x1000016b), "Shoots a wire.\nUpgrade: range") == 0);
    __Menu_SendMessage(gadgets, 0x2e, 0, 0);
    C_SBDSGTSCROLL_Handler(0, gadgets, 0x1000016f, 0x49, 0, 0);
    CHECK(std::strchr(w.text(0x1000016b), '\n') == nullptr);
    // As armas: a pistola mostra o modelo do nível 2 (P_DSWEAPONS_Handler troca o primeiro item).
    Manager_SendMessage(&menu_manager[0], 0x5f, 0, 0);
    w.tick();
    wheel = reinterpret_cast<M_SCROLL*>(w.control(0x1000010c));
    __Menu_Send(0, 0x1000010c, 0x2e, 3, 0);
    C_SBDOSSIER_Handler(0, &wheel->base, 0x1000010c, 0x4b, 0, 0);
    w.tick(12);
    CHECK(Menu_CurrentPage(0) == 0x4000003d);
    CHECK(ds_weapons[0].image == 0x30000f2 && ds_weapons[0].name == 0x33e && ds_weapons[0].text == 0x35a);
    M_CONTROL* weapons = w.control(0x10000173);
    CHECK(weapons != nullptr && reinterpret_cast<M_SCROLL*>(weapons)->max == 0x1a);
}

TEST(cheat_checkboxes_change_the_game_state) {
    DossierWorld w;
    // A página de depuração do 08000001: as 18 caixas (e o botão sem nome).
    const uint32_t ids[18] = {0x1000021d, 0x10000019, 0x100000e7, 0x100000e8, 0x1000001c, 0x1000001a,
                              0x10000012, 0x10000015, 0x10000016, 0x10000017, 0x100000bd, 0x100000c7,
                              0x1000001b, 0x10000101, 0x10000018, 0x10000169, 0x100001f7, 0x10000216};
    w.file.page(kCheatsPage);
    w.file.control(kCheatsPage, MCT_WINDOW, 0, 0, 640, 480);
    for (uint32_t i = 0; i < 18; ++i) w.file.control(ids[i], MCT_CHECKBOX, 320, int16_t(42 + 20 * i), 173, 17);
    w.file.control(0x1000021e, MCT_BUTTON, 467, 422, 113, 17);
    w.load();
    MenuCheats_Install();
    SoundInfo[0] = 100;
    // Um jogador com o HUD (os 22 painéis ligados) para a caixa do HUD; sem objeto de jogador (voo, gravidade zero).
    static BLData bloke;
    static HUDINFO_tag hud;
    std::memset(&bloke, 0, sizeof(bloke));
    std::memset(&hud, 0, sizeof(hud));
    for (HUDPANE_tag& p : hud.panes) p.valid = p.enabled = 1;
    bloke.hud = &hud;
    glb_blokes[0] = &bloke;
    MenuManager_Create(0x80000002u, kCheatsPage, 0, 0, 0x1e, 1, 3);
    w.tick();
    const auto box = [&](uint32_t id) { return reinterpret_cast<M_CHECKBOX*>(w.control(id)); };
    for (uint32_t id : ids) CHECK(box(id) != nullptr);
    if (box(ids[0]) == nullptr) return;
    // Ao criar (0x51), cada caixa mostra a global: marcadas as de valor 1 no boot (controles, HUD, personagens,
    // música, travamento).
    CHECK(box(0x1000021d)->checked == 1 && box(0x1000001c)->checked == 1 && box(0x10000015)->checked == 1);
    CHECK(box(0x1000001b)->checked == 1 && box(0x10000169)->checked == 1);
    CHECK(box(0x10000016)->checked == 0 && box(0x100000e8)->checked == 0 && box(0x10000216)->checked == 0);
    // Os nomes do port no lugar do "1" do disco.
    CHECK(box(0x1000001c)->label.string == PcText_Get(PcText::DbgHud));
    CHECK(reinterpret_cast<M_BUTTON*>(w.control(0x1000021e))->label.string == PcText_Get(PcText::DbgEndMission));
    // Trocar uma caixa e escolher (0x4b, como o X): a global segue.
    const auto toggle = [&](uint32_t id, int32_t v) {
        M_CONTROL* c = &box(id)->base;
        __Menu_SendMessage(c, 0x2e, v, 0);
        const MenuHandlerEntry* e = Menu_FindHandler(id);
        if (e != nullptr && e->handler != nullptr) e->handler(0, c, id, 0x4b, 0, 0);
    };
    toggle(0x10000016, 1);
    CHECK(switch_BLIND_DRONES == 1);
    toggle(0x10000015, 0);
    CHECK(switch_NO_DRONES == 1);
    toggle(0x10000015, 1);
    CHECK(switch_NO_DRONES == 0);
    toggle(0x100000e8, 1);
    uint32_t god = 0;
    std::memcpy(&god, CheatInfo, 4);
    CHECK(god == 1);
    toggle(0x1000001a, 1);
    CHECK(switch_SHOW_COORDS == 1);
    toggle(0x10000012, 1);
    CHECK(DrawInfo[1] == 1 && switch_CONFIG_WIDESCREEN == 1);
    toggle(0x100000bd, 1);
    CHECK(Drone_DummyPlayer == 1);
    toggle(0x10000017, 1);
    CHECK(Drone_DebugSpr == 1);
    toggle(0x100000c7, 1);
    CHECK(switch_AllowFreeze == 1);
    toggle(0x1000001b, 0);
    CHECK(SoundInfo[0] == 0);
    toggle(0x10000101, 1);
    CHECK(switch_ForceDrawAll == 1);
    toggle(0x10000216, 1);
    CHECK(menu_unlock_everything == 1);
    toggle(0x10000169, 0);
    toggle(0x100001f7, 1);
    toggle(0x1000021d, 0);
    CHECK(bDebugLockup == 0 && bTweakBrightness == 1 && bDebugControls == 0);
    // O HUD: todos os painéis do jogador, a opção da mira e a do multijogador.
    toggle(0x1000001c, 0);
    bool all_off = true;
    for (const HUDPANE_tag& p : hud.panes) all_off = all_off && p.enabled == 0;
    uint32_t mp_hud = 1;
    std::memcpy(&mp_hud, MPSettings + 0x28, 4);
    CHECK(hud_state == 0 && all_off && PlayerSetting[0].unknown_08 == 0 && mp_hud == 0);
    toggle(0x1000001c, 1);
    CHECK(hud_state == 1 && hud.panes[0].enabled == 1 && hud.panes[21].enabled == 1);
    // Sem objeto de jogador o voo e a gravidade zero só guardam o valor; as armas, sem jogador, as duas trapaças.
    glb_blokes[0] = nullptr;
    toggle(0x10000019, 1);
    toggle(0x10000018, 1);
    toggle(0x100000e7, 1);
    uint32_t weapons[2] = {};
    std::memcpy(weapons, CheatInfo + 4, 8);
    CHECK(debug_scanmode == 1 && debug_clipmode == 1 && weapons[0] == 1 && weapons[1] == 1);
    CHECK(g_menu_cheat_guards.no_player == 3);
    // Criada de novo, a página mostra o que ficou.
    MenuManager_Delete(0);
    MenuManager_Create(0x80000002u, kCheatsPage, 0, 0, 0x1e, 1, 3);
    w.tick();
    CHECK(box(0x10000016)->checked == 1 && box(0x10000015)->checked == 1 && box(0x10000019)->checked == 1);
    CHECK(box(0x100000e7)->checked == 1 && box(0x1000001b)->checked == 0 && box(0x10000018)->checked == 1);
}

TEST(cheat_medal_page_rows_and_the_debug_page_shortcut) {
    DossierWorld w;
    // A pausa, a página de depuração por cima dela e a página de fim de missão (a lista vazia no disco).
    w.file.page(0x4000004b);
    w.file.control(0x4000004b, MCT_WINDOW, 0, 0, 640, 480);
    w.file.page(kCheatsPage);
    w.file.control(kCheatsPage, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x1000021e, MCT_BUTTON, 467, 422, 113, 17);
    w.file.page(kCheatMedalPage);
    w.file.control(kCheatMedalPage, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x1000021c, MCT_LIST, 207, 109, 226, 262);
    w.load();
    MenuCheats_Install();
    // Sem jogador (o menu principal, uma fase sem início) o atalho não abre nada.
    CHECK(!Menu_PortShowCheats(0));
    static obj_tag player;
    static BLData bloke;
    std::memset(&player, 0, sizeof(player));
    std::memset(&bloke, 0, sizeof(bloke));
    player.data = &bloke;
    glb_players[0] = &player;
    glb_blokes[0] = &bloke;
    // Numa fase: a pausa e a página de depuração por cima; voltar volta à pausa.
    CHECK(Menu_PortShowCheats(0));
    CHECK(MenuManager_GetStatus() == 3 && Menu_CurrentPage(0) == kCheatsPage && GameState.paused == 1);
    Manager_SendMessage(&menu_manager[0], 0x5f, 0, 0);
    w.tick();
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    // Com a pausa aberta, só a página.
    CHECK(Menu_PortShowCheats(0) && Menu_CurrentPage(0) == kCheatsPage);
    // A página de fim de missão: as seis linhas do port; a quarta (prata) cumpre a missão (canal 100) e fecha o menu.
    Manager_SendMessage(&menu_manager[0], 0x44, int32_t(kCheatMedalPage), 0);
    w.tick();
    M_CONTROL* list = w.control(0x1000021c);
    CHECK(list != nullptr && __Menu_SendMessage(list, 0x33, 0, 0) == 6);
    switch_channels[100] = 0;
    __Menu_SendMessage(list, 0x1d, 3, 0);
    const MenuHandlerEntry* e = Menu_FindHandler(kCheatMedalPage);
    CHECK(e != nullptr && e->handler != nullptr);
    if (e != nullptr && e->handler != nullptr) e->handler(0, list, kCheatMedalPage, 0x4b, 0, 0);
    CHECK(medal_from_cheat == 2 && switch_channels[100] == 1 && MenuManager_Exists(0) == 0);
    switch_channels[100] = 0;
    medal_from_cheat = 0;
}

TEST(unlock_all_gives_every_medal_like_a_platinum_game) {
    DossierWorld w;
    def_codename[0] = 0x9b;
    Menu_PortUnlockAll();
    // Todas as missões liberadas, as quatro medalhas de cada missão com recompensas, as melhorias contadas.
    CHECK(Menu_GetNightfireStatus() == 0xfff);
    for (uint32_t level : {0x7000005u, 0x7000009u, 0x7000011u, 0x9000005u}) {
        for (uint32_t m = 1; m < 5; ++m) CHECK(Menu_HasMedal(level, m, 0) == 1);
        CHECK(Menu_GetBestMedal(level, 0) == 4);
    }
    CHECK(Menu_GetObjectUpgradeLevel(0, 0) == 3 && Upgrades[0] == 3);
    CHECK(GameState.unknown_52 == 1);  // a última missão (0700001b) termina o jogo
    CHECK(menu_bonus[0xa4] == 0);
    // Os dois perfis padrão levam as missões, as medalhas e o jogo terminado (o nome fica).
    for (uint32_t slot = 0; slot < 2; ++slot) {
        const uint8_t* d = def_codename + slot * 0x38;
        uint32_t m = 0;
        uint64_t bits = 0;
        std::memcpy(&m, d + 4, 4);
        std::memcpy(&bits, d + 8, 8);
        CHECK(m == 0xfff && bits == Menu_GetBonus(0) && bits != 0 && d[0x19] == 1);
    }
    CHECK(def_codename[0] == 0x9b);
    // Um perfil padrão aplicado depois (Play without saving) mantém tudo.
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    std::memset(sp_level + 0x10, 0, 1);
    Menu_MapDefaultCodename(0, 1, 0xf);
    CHECK(Menu_GetNightfireStatus() == 0xfff && Menu_HasMedal(0x7000011, 4, 0) == 1);
}

namespace {

// Um jogador mínimo para os modos de movimento: o objeto, o BLData, o objeto de animação e a câmera 0.
struct MoveWorld {
    obj_tag obj{};
    alignas(16) BLData b{};
    ANIMOBJ_tag anim{};
    viewer_tag viewer{};
    viewer_tag* saved = nullptr;
    MoveWorld() {
        obj.data = &b;
        obj.sphere_src = reinterpret_cast<uint8_t*>(&anim);
        Mat_Identity(&obj.matrix);
        b.zoom = 1.0f;
        saved = glb_viewer[0];
        glb_viewer[0] = &viewer;
        viewer.cel = nf::p32_ptr<cel_tag>(0x1234);
        std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    }
    ~MoveWorld() {
        glb_viewer[0] = saved;
        std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    }
    // Um tick como Player_Update: velocidade zerada, o modo, a velocidade local para o mundo e guardada.
    void tick(void (*mode)(BLData*, obj_tag*)) {
        Vec_Zero(&b.vel);
        Vec_Zero(&b.rot_vel);
        mode(&b, &obj);
        b.prev_vel = b.vel;
    }
};

float length3(const _VECTOR& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

}  // namespace

TEST(fly_mode_moves_freely_and_climbs) {
    MoveWorld m;
    m.obj.pos.y = 1.0f;
    m.b.fall = _VECTOR{0.0f, -5.0f, 0.0f, 0.0f};
    // Para a frente segurado: a velocidade sobe suavizada até 0,5 por tick e não passa; pular sobe 3 m/s.
    Input_SetAction(0, kActForward, kActionDown);
    Input_SetAction(0, kActJump, kActionDown);
    float last = 0.0f;
    bool rising = true;
    for (int i = 0; i < 120; ++i) {
        m.tick(Player_SSScanMode);
        rising = rising && last <= m.b.vel.z;
        last = m.b.vel.z;
    }
    CHECK(rising);
    CHECK_NEAR(m.b.vel.z, 0.5f, 1e-4f);
    CHECK_NEAR(m.obj.pos.y, 1.0f + 120.0f * 3.0f / 60.0f, 1e-3f);
    CHECK(m.obj.matrix.m[3][1] == m.obj.pos.y);
    // Sem queda, a câmera solta da sala, a animação parada.
    CHECK(length3(m.b.fall) == 0.0f && m.viewer.cel == nullptr && m.anim.speed == 0.0f && m.b.look_state == 2);
    // Virar: a matriz continua ortonormal.
    std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    Input_SetAction(0, kActTurn, kActionDown);
    for (int i = 0; i < 30; ++i) m.tick(Player_SSScanMode);
    _VECTOR dir;
    Mat_GetDir(&dir, &m.obj.matrix);
    CHECK_NEAR(length3(dir), 1.0f, 1e-4f);
    CHECK(std::fabs(dir.x) > 0.99f);  // um quarto de volta em meio segundo (pi rad/s)
    // Mirando (animação com a flag 1): o direcional muda o campo de visão da câmera, entre 0,2 e 3,14.
    m.anim.flags = 1;
    m.viewer.fov = 1.0f;
    m.viewer.width = 512.0f;
    m.viewer.height = 448.0f;
    std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    Input_SetAction(0, kActDpadY, kActionPressed);
    for (int i = 0; i < 200; ++i) m.tick(Player_SSScanMode);
    CHECK_NEAR(m.viewer.fov, 3.14f, 1e-5f);
}

TEST(zero_gravity_drifts_and_rights_itself) {
    MoveWorld m;
    m.obj.unknown_f6 = kPlrZeroG;  // 8: sem a animação de andar
    Input_SetAction(0, kActForward, kActionDown);
    for (int i = 0; i < 200; ++i) m.tick(Player_ZeroG);
    // A velocidade para a frente fica no limite de 0,05 por tick; sem gravidade nem olhar para baixo.
    CHECK_NEAR(m.b.vel.z, 0.05f, 1e-6f);
    CHECK(length3(m.b.fall) == 0.0f && m.b.pitch == 0.0f && m.b.look_state == 2);
    // Solto, flutua: a velocidade só cai 4% da diferença por tick.
    std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    m.tick(Player_ZeroG);
    CHECK(0.04f < m.b.vel.z && m.b.vel.z < 0.05f);
    // De cabeça para baixo e olhando para o lado, usar endireita em 1,5 s (a frente volta ao plano horizontal).
    RotMatrixX(2.5f, &m.obj.matrix);
    Input_SetAction(0, kActUse, kActionPressed | kActionDown);
    m.tick(Player_ZeroG);
    CHECK(m.b.zerog_turn == 89);
    std::memset(PlayerSetting, 0, sizeof(PlayerSetting));
    for (int i = 0; i < 89; ++i) m.tick(Player_ZeroG);
    CHECK(m.b.zerog_turn == 0);
    _VECTOR up;
    Mat_GetUp(&up, &m.obj.matrix);
    CHECK(up.y > 0.99f);
}
