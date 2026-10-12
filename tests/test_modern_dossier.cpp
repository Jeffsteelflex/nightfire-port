// Testes do dossiê, dos créditos e da aba SCORE no estilo Modern (só do port; src/ui/modern_dossier.h, a vista dos
// créditos de src/ui/modern_front.h, a aba SCORE de src/ui/modern_pause.h e a parte deles na ponte, engine/menu_modern.h),
// em arquivos de menu feitos à mão como em test_dossier.cpp e test_modern_menus.cpp: o que a ponte lê das páginas do
// jogo, o caminho das abas do dossiê pelas páginas do jogo (a roda, o íris, o voltar), o rádio das missões andando como o
// direcional, as linhas dos créditos com a posição e a fonte do jogo, a página dos créditos mostrada e voltando sozinha,
// as estatísticas da aba SCORE com um jogador, e a tecla do quadrado (menu_square).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/menu_modern.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/pc_text.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_card.h"
#include "platform/psi_input.h"
#include "platform/video_settings.h"
#include "render/ui_hd.h"
#include "test_framework.h"
#include "ui/modern_dossier.h"
#include "ui/modern_front.h"
#include "ui/modern_pause.h"

using namespace nf::ui;

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
    void page(uint32_t id, uint32_t menu = 0x80000002) {
        end_page();
        dw(0xfffffff0);
        dw(id);
        dw(menu);
        dw(0);
        by(0);
        page_size_at = b.size();
        dw(0);
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0, uint16_t set = 2,
                 uint32_t fmt = 0) {
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
        w(set);
        dw(fmt);
        dw(0);
    }
    void msg(uint8_t m, uint32_t a, uint32_t bb = 0) {
        dw(0xfffffff6);
        by(m);
        dw(a);
        dw(bb);
    }
    void finish() {
        end_page();
        dw(0xfffffff2);
        dw(0);
    }
};

uint32_t ptr32(const void* p) { return nf::p32_u(p); }
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
M_ITEM* missions() { return reinterpret_cast<M_ITEM*>(sp_level); }

// As missões (sp_level) e as recompensas de duas delas na RewardsTable, como em test_dossier.cpp.
constexpr uint32_t kLevels[12] = {0x9000001, 0x7000005, 0x9000005, 0x9000006, 0x7000001, 0x7000009,
                                  0x700000c, 0x7000011, 0x9000002, 0x9000003, 0x7000014, 0x700001b};
void reward(uint32_t entry, uint32_t level, const uint16_t (&bits)[4], const uint16_t (&kinds)[4]) {
    uint8_t* e = RewardsTable + entry * 0x40;
    std::memset(e, 0, 0x40);
    put32(e, level);
    for (uint32_t m = 0; m < 4; ++m) {
        put32(e + 8 + m * 4, uint32_t(bits[m]) | uint32_t(kinds[m]) << 16);
        put32(e + 0x1c + m * 4, 0x2d0 + entry * 4 + m);      // o texto da recompensa
        put32(e + 0x30 + m * 4, 0x3000100 + entry * 4 + m);  // a imagem
    }
}

// A tabela de textos: as seções, as medalhas, os nomes das linhas dos recordes e da aba SCORE; o resto vazio. Dois
// grupos de rótulos (0 e 1), os dois a partir da entrada 0.
char kEmptyText[] = "";
char kSectionNames[4][10] = {"Records", "Rewards", "Gadgets", "Weapons"};
char kMedals[4][16] = {"PLATINUM MEDAL", "GOLD MEDAL", "SILVER MEDAL", "BRONZE MEDAL"};
char kHighScore[] = "High Score";
char kBondMoves[] = "Bond Moves";
char kUpgrades[] = "Upgrades Unlocked";
char kGrappleText[] = "Shoots a wire.";
char kUpgradeLine[] = "Upgrade: range";
char kCategory[] = "Category";
char kCurrent[] = "Current";
char kTarget[] = "Target";
char kOpponents[] = "Opponents";
char kDispatched[] = "Dispatched";
char kAccuracy[] = "Accuracy Rating";
char kRewardNames[16][8];
P32<char> g_bank[0x400];
uint32_t g_fixups[2] = {0, 0};

// O heap, a camada 2D, os menus com a ponte instalada, o cartão em memória e as tabelas do menu principal.
struct World {
    MenuFile file;
    nf::MemoryCard card;
    SCORETABLE_ROW rows[kScoreRows]{};
    World() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        for (P32<char>& s : g_bank) s = kEmptyText;
        for (int i = 0; i < 4; ++i) g_bank[0x280 + i] = kSectionNames[i];
        for (int i = 0; i < 4; ++i) g_bank[0x306 + i] = kMedals[i];
        g_bank[0x302] = kHighScore;
        g_bank[0x165] = kBondMoves;
        g_bank[0x305] = kUpgrades;
        g_bank[0x336] = kGrappleText;
        g_bank[0x3b3] = kUpgradeLine;
        g_bank[0x204] = kCategory;  // 0x1000204
        g_bank[0x1e1] = kCurrent;   // 0x10001e1
        g_bank[0x1e2] = kTarget;    // 0x10001e2
        g_bank[0x15a] = kOpponents;
        g_bank[0x15b] = kDispatched;
        g_bank[0x15e] = kAccuracy;
        for (int i = 0; i < 16; ++i) {
            std::snprintf(kRewardNames[i], sizeof(kRewardNames[i]), "R%d", i);
            g_bank[0x2d0 + i] = kRewardNames[i];
        }
        Bank = g_bank;
        NumEntries = 0x400;
        FixupTable = g_fixups;
        NumFixups = 2;
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        menu_modern_debug_power_on();
        MenuModern_Install();
        nf::set_card(&card);
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
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        std::memset(Upgrades, 0, sizeof(Upgrades));
        for (uint32_t i = 0; i < 12; ++i) missions()[i] = M_ITEM{0x3000080 + i, 0x100 + i, 0x110 + i, kLevels[i], 1, {}, 0};
        std::memset(RewardsTable, 0, sizeof(RewardsTable));
        reward(0, 0x7000005, {26, 38, 0, 45}, {0, 1, 4, 1});
        reward(1, 0x9000005, {27, 56, 3, 53}, {0, 2, 4, 1});
        for (uint32_t i = 0; i < 4; ++i) ds_options[i] = M_ITEM{0x300019b + i, 0x280 + i, 0x200 + i, i, 1, {}, 0};
        for (uint32_t i = 0; i < 14; ++i) ds_gadgets[i] = M_ITEM{0x30000e0 + i, 0x321 + i, 0x32f + i, 0x3f, 1, {}, 0};
        ds_gadgets[2] = M_ITEM{0x30000e2, 0x328, 0x336, 3, 1, {}, 0};  // o grapple
        for (uint32_t i = 0; i < 27; ++i) ds_weapons[i] = M_ITEM{0x30000e8 + i, 0x33d + i, 0x359 + i, 0x3f, 1, {}, 0};
        ds_weapons[0].value = 0;
        for (obj_tag*& p : glb_players) p = nullptr;
        for (BLData*& p : glb_blokes) p = nullptr;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        psiInput_SetModal(false);
        Input_Init();
        Input_Update();
        MenuManager_Init();
        GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    }
    ~World() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_modern_debug_power_on();
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        nf::set_card(nullptr);
        for (uint32_t i = 0; i < kScoringTableCount; ++i) ScoringTable[i] = SCORETABLE{};
        player_stats_debug_power_on();
        mission_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        std::memset(sp_level, 0, sizeof(sp_level));
        std::memset(RewardsTable, 0, sizeof(RewardsTable));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        std::memset(Upgrades, 0, sizeof(Upgrades));
        std::memset(ds_options, 0, sizeof(ds_options));
        std::memset(ds_weapons, 0, sizeof(ds_weapons));
        std::memset(ds_gadgets, 0, sizeof(ds_gadgets));
        Menu_PortClearTables();
        BaseMap = 0xffffffffu;
        GameState.paused = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load(uint32_t hash = 0x8000002) {
        file.finish();
        MenuManager_Load(hash, file.b.data());
    }
    // Um tick dos menus como no Game_Run (sem teclas: só os pedidos da ponte).
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    void drain() {
        for (int i = 0; i < 64 && !MenuModern_Queue().empty(); ++i) tick();
    }
    M_CONTROL* control(uint32_t id, uint32_t tag = 0) {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
};

// O controlador de vídeo com ganchos vazios (o menu principal Modern tem as opções embutidas).
struct VideoFixture {
    VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        vc.init(nf::VideoSettings{});
        vc.set_hardware({{1280, 720}, {1920, 1080}}, nf::DisplayMode{1920, 1080}, {2, 4});
        vc.set_apply([](const nf::VideoSettings&, const nf::VideoSettings&) {});
        vc.set_store([](const nf::VideoSettings&) { return true; });
        PcText_SetLanguage("en");
        nf::controls() = nf::ControlsConfig{};
        psiInput_SetModal(false);
    }
    ~VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        vc.set_apply(nullptr);
        vc.set_store(nullptr);
        vc.init(nf::VideoSettings{});
    }
};

// O mapa das missões (de onde o quadrado abre o dossiê) e as páginas do dossiê como no 08000002 (os controles que os
// tratadores tocam), como em test_dossier.cpp.
void dossier_pages(MenuFile& f) {
    f.page(0x4000001c);
    f.control(0x40000005, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000000e, MCT_SCROLL, 312, 48, 16, 32);
    f.page(0x4000002b);
    f.control(0x4000002b, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000010a, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010b, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010c, MCT_SCROLL, 312, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x1000010d, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x100001ed, MCT_MEMO, 50, 301, 540, 113);
    f.page(0x4000003a);
    f.control(0x4000003a, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000001, MCT_LABEL, 50, 46, 375, 19);
    f.msg(0x18, 0, 0x280);
    f.control(0x10000174, MCT_RADIO, 132, 124, 376, 16);
    f.control(0x10000001, MCT_LABEL, 82, 196, 248, 29);
    f.msg(0x18, 0, 0x302);
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

const Measure kMeasure = [](const std::wstring& s, const TextStyle& st) { return estimate_width(s, st); };

// A vista do dossiê e o jogo juntos: um quadro de vídeo da vista com o estado do jogo e um tick dos menus, n vezes (as
// páginas visitadas ficam em `pages`).
struct DossierRun {
    World& w;
    DossierView view;
    NavState nav;
    UiInput in;
    std::vector<uint32_t> pages;
    explicit DossierRun(World& world) : w(world) { nav.reset(); }
    void enter() {
        const Canvas cv = make_canvas(1920, 1080);
        const Frame f{default_tokens(), cv, kMeasure, 0.0f};
        view.enter(MenuModern_Snapshot(), f);
    }
    void run(int n) {
        const Canvas cv = make_canvas(1920, 1080);
        for (int i = 0; i < n; ++i) {
            const MmSnapshot s = MenuModern_Snapshot();
            const Frame f{default_tokens(), cv, kMeasure, 0.0f};
            nav.clear_fired();
            view.frame(1.0f / 60.0f, nav, in, s, f);
            w.tick();
            const uint32_t p = Menu_CurrentPage(0);
            if (pages.empty() || pages.back() != p) pages.push_back(p);
        }
    }
    bool visited(uint32_t page) const {
        for (uint32_t p : pages) {
            if (p == page) return true;
        }
        return false;
    }
};

bool queued(const MmRequest& r) {
    for (const MmRequest& q : MenuModern_Queue()) {
        if (q == r) return true;
    }
    return false;
}

MmWheelItem item(const char* name, uint32_t image, int medal = 0) {
    MmWheelItem it;
    it.name = name;
    it.text = "text";
    it.image = image;
    it.medal = medal;
    return it;
}

// Um estado do dossiê montado à mão (o que MenuModern_Snapshot leria do jogo).
MmSnapshot dossier_snapshot(int section) {
    static const uint32_t kPages[4] = {kMmFeRecords, kMmFeRewards, kMmFeGadgets, kMmFeWeapons};
    MmSnapshot s;
    s.screen = MmScreen::Front;
    s.status = 5;
    s.page = s.base_page = section >= 0 ? kPages[section] : kMmFeDossier;
    s.ready = s.accepts = true;
    s.front.view = MmFrontView::Dossier;
    s.front.dossier.section = section;
    for (int i = 0; i < 4; ++i) s.front.dossier.sections.push_back(item(kSectionNames[i], 0));
    return s;
}

}  // namespace

// ---- o dossiê: a ponte ----

TEST(modern_dossier_bridge_reads_the_pages_and_steps_the_mission_radio) {
    World w;
    dossier_pages(w.file);
    w.load();
    // Um recorde, os momentos Bond e a medalha de ouro em The Exchange (07000005), a última jogada.
    ScoringTable[1].best = 123456;
    ScoringRows[1].row[0].actual = 4;
    Menu_SetLevelBonus(0x7000005, 3, 0);
    GameState.unknown_44[1] = 0x7000005;
    MenuManager_Create(0x80000002u, 0x4000001c, -1, 0, 0x28, 0, 5);
    w.tick(2);
    // O quadrado do mapa mostra a página do dossiê (P_DOSSIER_Handler: o íris e a roda no começo), a roda com o foco.
    Manager_SendMessage(&menu_manager[0], 0x44, 0x4000002b, 0);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x1000010c, 0);
    w.tick(2);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Front && s.front.view == MmFrontView::Dossier && s.front.dossier.section == -1);
    CHECK(s.accepts && s.front.wheel == kMmDsWheel && s.front.dossier.section_pos == 0);
    CHECK(s.front.dossier.sections.size() == 4 && s.front.dossier.sections[1].name == "Rewards");
    // A vista entra na aba RECORDS pela roda do jogo: a roda já está nos recordes, então só o X (C_SBDOSSIER_Handler).
    VideoFixture v;
    DossierRun r(w);
    r.enter();
    r.run(1);
    CHECK(g_menu_modern_stats.presses == 1 && g_menu_modern_stats.steps == 0);
    r.run(4);
    CHECK(Menu_CurrentPage(0) == 0x4000003a && r.view.tab() == 0 && r.view.target() == -1);
    // Os recordes como o C_RBDSRECORDS_Handler os escreveu: as 12 missões do rádio na última jogada, os valores, a
    // medalha e a imagem dela.
    s = MenuModern_Snapshot();
    const MmDossier& d = s.front.dossier;
    CHECK(d.section == 0 && s.front.wheel == kMmDsRecordsRadio && s.front.items.size() == 12 && s.front.pos == 1);
    CHECK(s.front.items[1].value == 0x7000005 && s.front.items[1].medal == 3 && s.front.items[1].image == 0x3000081);
    CHECK(d.score_label == "High Score" && d.score == "123456" && d.moments == "4" && d.upgrades == "1");
    CHECK(d.medal == "GOLD MEDAL" && d.medal_image == 0x300013e);
    CHECK(s.front.title == "Records");
    // Andar na lista é o passo do rádio, um por vez como o direcional: o 0x49 do rádio refaz a página.
    MenuModern_Step(kMmDsRecordsRadio, 6);
    w.drain();
    s = MenuModern_Snapshot();
    CHECK(s.front.pos == 6 && g_menu_modern_stats.steps == 5);
    CHECK(s.front.dossier.score == "0" && s.front.dossier.medal.empty() && s.front.dossier.medal_image == 0);
}

TEST(modern_dossier_tabs_and_back_go_through_the_game_pages) {
    World w;
    dossier_pages(w.file);
    w.load();
    // As recompensas de bronze e prata em 07000005; a melhoria do grapple (o ouro de 09000005).
    Menu_SetLevelBonus(0x7000005, 2, 0);
    Menu_SetLevelBonus(0x9000005, 3, 0);
    GameState.unknown_44[1] = 0x7000005;
    MenuManager_Create(0x80000002u, 0x4000001c, -1, 0, 0x28, 0, 5);
    w.tick(2);
    Manager_SendMessage(&menu_manager[0], 0x44, 0x4000002b, 0);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x1000010c, 0);
    w.tick(2);
    VideoFixture v;
    DossierRun r(w);
    r.enter();
    r.run(6);
    CHECK(Menu_CurrentPage(0) == 0x4000003a);
    // REWARDS: o voltar do jogo (a página da roda), o passo da roda até a seção e o X; as recompensas aparecem na hora.
    r.pages.clear();
    r.view.go_tab(1);
    CHECK(r.view.target() == 1);
    r.run(10);
    CHECK(r.visited(0x4000002b) && Menu_CurrentPage(0) == 0x4000003b && r.view.tab() == 1 && r.view.target() == -1);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.front.dossier.section == 1 && s.front.dossier.rewards[0].shown && s.front.dossier.rewards[1].shown);
    CHECK(!s.front.dossier.rewards[2].shown && !s.front.dossier.rewards[3].shown);
    CHECK(s.front.dossier.rewards[0].image == 0x3000100 && s.front.dossier.rewards[1].text == "R1");
    CHECK(s.front.dossier.rewards[2].medal == "GOLD MEDAL");
    // GADGETS: de novo pela roda, e a página só vem depois do íris do jogo (10 ticks); a vista espera.
    r.pages.clear();
    r.view.go_tab(2);
    r.run(6);
    CHECK(Menu_CurrentPage(0) == 0x4000002b && r.view.tab() == 1 && r.view.target() == 2);
    r.run(20);
    CHECK(Menu_CurrentPage(0) == 0x4000003c && r.view.tab() == 2 && r.view.target() == -1);
    CHECK(ds_gadgets[6].image == 0x300018f);  // P_DSGADGETS_Handler rodou (a imagem do barbeador a 60 Hz)
    s = MenuModern_Snapshot();
    CHECK(s.front.items.size() == 14 && s.front.wheel == kMmDsGadgetWheel && s.front.dossier.upgrade.size() == 14);
    CHECK(s.front.dossier.upgrade[2] == 1 && s.front.dossier.upgrade[0] == 0);
    // O grapple na roda do jogo: o texto com a linha da melhoria.
    MenuModern_Step(kMmDsGadgetWheel, 2);
    w.drain();
    CHECK(MenuModern_Snapshot().front.dossier.memo == "Shoots a wire.\nUpgrade: range");
    // Sair: o voltar do jogo duas vezes (a seção, a página da roda); o jogo volta ao mapa.
    r.pages.clear();
    r.view.leave();
    r.run(12);
    CHECK(r.visited(0x4000002b) && Menu_CurrentPage(0) == 0x4000001c);
}

// ---- o dossiê: a tela ----

TEST(modern_dossier_view_draws_the_game_values_and_steps_the_lists) {
    VideoFixture v;
    const Canvas cv = make_canvas(1920, 1080);
    const Frame f{default_tokens(), cv, kMeasure, 0.0f};
    UiInput in;
    // Os recordes: os valores do jogo, a imagem da missão e a da medalha.
    MmSnapshot s = dossier_snapshot(0);
    s.front.wheel = kMmDsRecordsRadio;
    s.front.items = {item("Paris Prelude", 0x3000080), item("The Exchange", 0x3000081, 3), item("Night Shift", 0x3000082)};
    s.front.pos = 1;
    s.front.dossier.score_label = "High Score";
    s.front.dossier.score = "123456";
    s.front.dossier.moments_label = "Bond Moves";
    s.front.dossier.moments = "4";
    s.front.dossier.medal = "GOLD MEDAL";
    s.front.dossier.medal_image = 0x300013e;
    DossierView view;
    view.enter(s, f);
    NavState nav;
    nav.reset();
    for (int i = 0; i < 3; ++i) view.frame(1.0f / 60.0f, nav, in, s, f);
    CHECK(view.tab() == 0 && view.focus() == 1 && MenuModern_Queue().empty());
    DrawList dl;
    view.build(dl, f, 1.0f, 0.0f);
    CHECK(dl.find_text(L"123456") != nullptr && dl.find_text(L"GOLD MEDAL") != nullptr && dl.find_text(L"RECORDS") != nullptr);
    CHECK(dl.find_text(L"The Exchange") != nullptr && dl.find_text(L"Night Shift") != nullptr);
    ImageList im;
    view.images(im, 1.0f, 0.0f);
    bool mission = false, medal = false;
    for (const ImageCmd& c : im) {
        mission = mission || (c.hash == 0x3000081 && c.whole);
        medal = medal || (c.hash == 0x300013e && c.whole);
    }
    CHECK(mission && medal);
    // Baixo e direita: o passo do rádio do jogo (a próxima missão); a aba seguinte (E) pede a seção pelo jogo.
    nav.inject(Nav::Down);
    view.frame(1.0f / 60.0f, nav, in, s, f);
    CHECK(queued(MmRequest{MmAction::Step, kMmDsRecordsRadio, 0, 2}) && view.focus() == 2);
    menu_modern_debug_power_on();
    nav.clear_fired();
    nav.inject(Nav::TabNext);
    view.frame(1.0f / 60.0f, nav, in, s, f);
    CHECK(view.target() == 1 && queued(MmRequest{MmAction::Press, kMmBack, 0, 0}));
    menu_modern_debug_power_on();
    // As recompensas: as mostradas com a imagem do jogo, as outras com o cadeado e a medalha que falta.
    MmSnapshot rw = dossier_snapshot(1);
    rw.front.wheel = kMmDsRewardsRadio;
    rw.front.items = s.front.items;
    rw.front.pos = 1;
    rw.front.dossier.rewards[0] = MmReward{true, 0x3000100, "Reward Card", "BRONZE MEDAL"};
    rw.front.dossier.rewards[1] = MmReward{true, 0x3000101, "MP Skin", "SILVER MEDAL"};
    rw.front.dossier.rewards[2] = MmReward{false, 0, "", "GOLD MEDAL"};
    rw.front.dossier.rewards[3] = MmReward{false, 0, "", "PLATINUM MEDAL"};
    DossierView rv;
    rv.enter(rw, f);
    rv.frame(1.0f / 60.0f, nav, in, rw, f);
    DrawList rl;
    rv.build(rl, f, 1.0f, 0.0f);
    CHECK(rl.find_text(L"Reward Card") != nullptr && rl.find_text(L"2 / 4") != nullptr && rl.count(DrawCmd::Padlock) == 2);
    ImageList ri;
    rv.images(ri, 1.0f, 0.0f);
    int rewards = 0;
    for (const ImageCmd& c : ri) rewards += c.hash == 0x3000100 || c.hash == 0x3000101 ? 1 : 0;
    CHECK(rewards == 2);
    // Os aparelhos: o texto do jogo para o item da roda, com a linha da melhoria separada (o aviso em ouro).
    MmSnapshot g = dossier_snapshot(2);
    g.front.wheel = kMmDsGadgetWheel;
    g.front.items = {item("Stunner", 0x30000e0), item("Laser", 0x30000e1), item("Grapple", 0x30000e2)};
    g.front.dossier.upgrade = {0, 0, 1};
    g.front.pos = 2;
    g.front.dossier.memo = "Shoots a wire.\nUpgrade: range";
    DossierView gv;
    gv.enter(g, f);
    gv.frame(1.0f / 60.0f, nav, in, g, f);
    DrawList gl;
    gv.build(gl, f, 1.0f, 0.0f);
    CHECK(gl.find_text(L"Shoots a wire.") != nullptr && gl.find_text(L"Upgrade: range") != nullptr);
    CHECK(gl.find_text(L"GRAPPLE") != nullptr);  // o título da descrição
    // Voltar sai pelo jogo (o voltar da seção).
    nav.clear_fired();
    nav.inject(Nav::Back);
    gv.frame(1.0f / 60.0f, nav, in, g, f);
    CHECK(gv.target() == DossierView::kLeave && queued(MmRequest{MmAction::Press, kMmBack, 0, 0}));
}

// ---- os créditos ----

TEST(modern_credits_read_the_game_lines_and_the_page_returns_by_itself) {
    World w;
    MenuFile& f = w.file;
    f.page(0x40000002);
    f.control(0x40000002, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000002, MCT_BUTTON, 168, 271, 304, 30);
    f.page(0x40000030);
    f.control(0x40000030, MCT_WINDOW, 0, 0, 640, 480);
    for (uint32_t t = 0; t < 26; ++t) {
        f.control(0x10000213, MCT_LABEL, 58, int16_t(-170 + 30 * int(t)), 254, 16, t);
        f.control(0x10000214, MCT_LABEL, 328, int16_t(-138 + 30 * int(t)), 254, 16, t);
    }
    f.control(0x10000215, MCT_LABEL, 0, 0, 640, 480);
    f.control(0x1000023c, MCT_LABEL, 0, 0, 640, 480);
    w.load();
    // Três linhas como as do ELF: as duas empresas (fonte 1), um cargo (fonte 2 do formato: a 3 do jogo) e um nome, e uma
    // linha larga do texto legal.
    Menu_PortClearTables();
    CreditsSource[0] = CREDIT_SOURCE{0, Menu_PortTableString("EUROCOM"), 0, Menu_PortTableString("ELECTRONIC ARTS"), 1, 1, 0};
    CreditsSource[1] = CREDIT_SOURCE{0, Menu_PortTableString("Producer"), 0, Menu_PortTableString("Jane Doe"), 2, 0, 0};
    CreditsSource[2] = CREDIT_SOURCE{0, Menu_PortTableString("All rights reserved."), 0, nullptr, 0, 0, 1};
    CreditsSourceCount = 3;
    // O fade de entrada do P_MAIN_Handler congela o menu por 15 ticks (uma mensagem atrasada; ver test_modern_front.cpp).
    menu_delay_frame = 1;
    MenuManager_Create(0x80000002u, 0x40000002, -1, 0, 0x28, 0, 5);
    w.tick(20);
    CHECK(menu_manager[0].frozen == 0);
    // A linha CREDITS do menu principal Modern: a página mostrada como o script do "Credits" da AV Options faz.
    MenuModern_Show(kMmFeCredits);
    w.drain();
    CHECK(Menu_CurrentPage(0) == 0x40000030 && g_menu_modern_stats.shows == 1);
    CHECK(MenuModern_FrontView(Menu_CurrentPage(0)) == MmFrontView::Credits);
    w.tick(150);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Front && s.front.view == MmFrontView::Credits);
    const std::vector<MmCreditLine>& lines = s.front.credits.lines;
    CHECK(lines.size() == 5);
    const auto find = [&](const char* text) -> const MmCreditLine* {
        for (const MmCreditLine& l : lines) {
            if (l.text == text) return &l;
        }
        return nullptr;
    };
    const MmCreditLine* eurocom = find("EUROCOM");
    const MmCreditLine* ea = find("ELECTRONIC ARTS");
    const MmCreditLine* role = find("Producer");
    const MmCreditLine* name = find("Jane Doe");
    const MmCreditLine* legal = find("All rights reserved.");
    CHECK(eurocom != nullptr && ea != nullptr && role != nullptr && name != nullptr && legal != nullptr);
    if (eurocom == nullptr || ea == nullptr || role == nullptr || name == nullptr || legal == nullptr) return;
    // As fontes do formato do jogo (credit_format): 1 as empresas, 3 os cargos, 2 os nomes e o texto legal.
    CHECK(eurocom->font == 1 && ea->font == 1 && role->font == 3 && name->font == 2 && legal->font == 2);
    // As colunas (nome em 0x10, cargo em 0xf0, 0x100 de largura) e a linha larga (0x1e0); a mais velha mais em cima, 28
    // pixels (14 ticks de 2) entre elas.
    CHECK(!eurocom->right && ea->right && eurocom->x == 0x10 && ea->x == 0xf0 && eurocom->w == 0x100 && !eurocom->wide);
    CHECK(legal->wide && legal->w == 0x1e0 && eurocom->y == ea->y);
    CHECK(role->y == eurocom->y + 28 && legal->y == role->y + 28 && s.front.credits.fade == 0.0f);
    // A tela: as linhas no tipo do kit, centradas; voltar é o voltar do jogo.
    VideoFixture v;
    FrontScreen fs;
    fs.open(s);
    const Canvas cv = make_canvas(1920, 1080);
    for (int i = 0; i < 40; ++i) fs.frame(1.0f / 60.0f, UiInput{}, s, cv, kMeasure);
    CHECK(fs.view() == FrontScreen::View::Credits);
    DrawList dl;
    fs.build(dl, cv, kMeasure);
    const DrawCmd* e = dl.find_text(L"EUROCOM");
    const DrawCmd* j = dl.find_text(L"Jane Doe");
    CHECK(e != nullptr && j != nullptr && dl.find_text(L"PRODUCER") != nullptr);
    if (e != nullptr && j != nullptr) {
        CHECK(e->align == Align::Centre && e->r.cx() < 960.0f && j->r.cx() > 960.0f);
        CHECK(e->style.size > j->style.size);  // as empresas maiores que os nomes
    }
    fs.command(Nav::Back);
    fs.frame(1.0f / 60.0f, UiInput{}, s, cv, kMeasure);
    CHECK(queued(MmRequest{MmAction::Press, kMmBack, 0, 0}));
    menu_modern_debug_power_on();
    // O fim é do jogo: 18 linhas depois da última a página escurece (o alfa do rótulo 0x10000215) e volta sozinha.
    w.tick(160);
    s = MenuModern_Snapshot();
    CHECK(Menu_CurrentPage(0) == 0x40000030 && s.front.credits.fade > 0.0f);
    w.tick(30);
    CHECK(Menu_CurrentPage(0) == 0x40000002);
}

// ---- a aba SCORE com um jogador ----

TEST(modern_pause_score_tab_reads_the_mission_stats) {
    World w;
    MenuFile& f = w.file;
    f.page(0x4000004b);
    f.control(0x40000001, MCT_WINDOW, 0, 0, 640, 480);
    const int16_t xs[4] = {84, 200, 340, 465};
    const uint32_t tags[4] = {0, 1, 3, 4};
    for (int i = 0; i < 4; ++i) f.control(0x10000028, MCT_BUTTON, xs[i], 120, 100, 16, tags[i]);
    f.control(0x10000028, MCT_LIST, 153, 183, 334, 120, 6, 4, 1);
    f.control(0x10000028, MCT_LIST, 100, 170, 440, 190, 8, 4, 1);
    w.load(0x8000001);
    // A missão 07000011 (a primeira entrada da tabela de pontuação) com tiros, um abatido e vida.
    BaseMap = 0x7000011;
    Mission_SetStatus(kMissionRunning);
    ScoringTable[0].level = 0x7000011;
    ScoringTable[0].rows = w.rows;
    const uint32_t targets[kScoreRows] = {2, 65, 0, 0, 2, 100, 100, 720, 7};
    for (uint32_t i = 0; i < kScoreRows; ++i) w.rows[i] = SCORETABLE_ROW{targets[i], 100, 0, 0.0f, 0.0f};
    PlrMissionStats[0].shots_fired = 5;
    PlrMissionStats[0].shots_hit = 1;
    PlrMissionStats[0].dispatched = 1;
    PlrMissionStats[0].health = 65.0f;
    PlrMissionStats[0].elapsed = 938;
    MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
    w.tick(2);
    // A aba SCORE pelo foco no botão dela (tag 4): o C_GCPAUSE_Handler monta as estatísticas com o PlrStat_GetScore.
    MenuModern_Tab(kMmTabScore);
    w.drain();
    w.tick();
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Pause && !s.multiplayer && s.tab == int(kMmTabScore));
    CHECK(s.score.size() == 11);
    if (s.score.size() == 11) {
        CHECK(s.score[0][0] == "Category" && s.score[0][1] == "Current" && s.score[0][2] == "Target");
        CHECK(s.score[3][0] == "Opponents" && s.score[4][0] == "  Dispatched" && s.score[4][1] == "1" && s.score[4][2] == "65");
        CHECK(s.score[7][0] == "Accuracy Rating" && s.score[7][1] == "20" && s.score[7][2] == "100");
    }
    // A pausa Modern: PAUSE MENU · OBJECTIVES · CONTROLS · SCORE · OPTIONS, e a tabela do jogo na aba SCORE.
    VideoFixture v;
    PauseScreen p;
    p.open(s, PauseScreen::Tab::Score);
    const Canvas cv = make_canvas(1920, 1080);
    for (int i = 0; i < 30; ++i) p.frame(1.0f / 60.0f, UiInput{}, s, cv, kMeasure);
    CHECK(p.tabs().size() == 5 && p.tabs()[3] == PauseScreen::Tab::Score && p.tab() == PauseScreen::Tab::Score);
    DrawList dl;
    p.build(dl, cv, kMeasure);
    CHECK(dl.find_text(L"Dispatched") != nullptr && dl.find_text(L"Accuracy Rating") != nullptr && dl.find_text(L"20") != nullptr);
    CHECK(dl.find_text(L"CATEGORY") != nullptr && dl.find_text(L"TARGET") != nullptr);
    CHECK(dl.find_text(L"OPPONENTS") != nullptr);  // a linha sem números: o cabeçalho do grupo
}

// ---- a tecla do quadrado ----

TEST(modern_square_key_defaults_to_e_and_shows_in_the_prompts) {
    // O quadrado dos menus do jogo (o dossiê no mapa e no resultado, "Create" nos codinomes): o E, a mesma tecla da aba
    // DOSSIER do mapa Modern (menu_tab_next).
    const nf::Bindings b = nf::default_bindings();
    const int square = nf::find_bind_command("menu_square");
    const int next = nf::find_bind_command("menu_tab_next");
    CHECK(square >= 0 && next >= 0);
    CHECK(b.has(square, nf::key_input('E')) && b.has(next, nf::key_input('E')));
    // Nenhum outro comando do menu usa o E (o "usar" do jogo é de outro grupo).
    for (int c = 0; c < nf::kBindCommandCount; ++c) {
        if (c == square || c == next || nf::kBindCommands[c].group != nf::BindGroup::Menu) continue;
        CHECK(!b.has(c, nf::key_input('E')));
    }
    // O símbolo do quadrado ("~Y") na barra de ajuda da camada HD, com as teclas: a tecla E.
    CHECK(nf::ui_hd_prompt_keys('Y', false, b, 0, true) == L"E");
    // Religado, o símbolo segue.
    nf::Bindings r = b;
    nf::bind(r, square, nf::key_input('F'));
    CHECK(nf::ui_hd_prompt_keys('Y', false, r, 0, true) == L"F");
}
