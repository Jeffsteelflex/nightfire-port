// Testes do menu principal no estilo Modern (só do port; src/ui/modern_front.h e a parte do menu principal da ponte,
// engine/menu_modern.h), num arquivo de menu feito à mão como em test_frontend.cpp: a leitura das páginas (os botões, as
// rodas com os itens das tabelas do jogo, o teclado da tela), os pedidos novos (Focus, Step, as teclas) contra o código
// de menu do jogo, e a tela com estados montados à mão: o que cada escolha manda ao jogo, a regra das missões
// bloqueadas, as caixas do jogo como o Dialog, as opções do codinome e o QUIT GAME.

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
#include "engine/menu_frontend.h"
#include "engine/menu_modern.h"
#include "engine/mission.h"
#include "engine/pc_text.h"
#include "engine/player_stats.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_card.h"
#include "platform/psi_input.h"
#include "platform/video_settings.h"
#include "test_framework.h"
#include "ui/modern_end.h"
#include "ui/modern_front.h"

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

char kEmptyText[] = "";
char kNightFire[] = "NightFire";
char kMultiplayer[] = "Multiplayer";
char kCodenamesText[] = "Codenames";
char kNewCodename[] = "New Codename";
char kDefault[] = "Default";
char kExchange[] = "The Exchange";
char kParis[] = "Paris Prelude";
char kCastle[] = "Night Shift";
char kLockedText[] = "Complete the previous mission.";
char kGoldMedal[] = "GOLD MEDAL";
char kKeyA[] = "A";
char kKeyB[] = "B";
P32<char> g_bank[0x400];
uint32_t g_fixups[1] = {0};

// O heap, a camada 2D, a entrada, os menus com a ponte instalada, o cartão em memória e as tabelas de que o menu
// principal precisa (os perfis padrão, as missões, as dificuldades).
struct FrontWorld {
    MenuFile file;
    nf::MemoryCard card;
    FrontWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        for (P32<char>& s : g_bank) s = kEmptyText;
        g_bank[0x29b] = kNewCodename;
        g_bank[0x211] = kDefault;
        g_bank[0x100] = kParis;
        g_bank[0x101] = kExchange;
        g_bank[0x102] = kCastle;
        g_bank[0x112] = kLockedText;
        g_bank[0x307] = kGoldMedal;
        Bank = g_bank;
        NumEntries = 0x400;
        FixupTable = g_fixups;
        NumFixups = 1;
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
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = 2;
        GameState.level_file = 0x7000048;
        std::memset(def_codename, 0, sizeof(def_codename));
        const uint32_t names[2] = {0x29b, 0x211};
        std::memcpy(def_codename, &names[0], 4);
        std::memcpy(def_codename + 0x38, &names[1], 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        psiInput_SetModal(false);
        Input_Init();
        Input_Update();
        MenuManager_Init();
        GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    }
    ~FrontWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_modern_debug_power_on();
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        nf::set_card(nullptr);
        player_stats_debug_power_on();
        mission_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        std::memset(def_codename, 0, sizeof(def_codename));
        std::memset(sp_level, 0, sizeof(sp_level));
        std::memset(difficulty, 0, sizeof(difficulty));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        std::memset(RewardsTable, 0, sizeof(RewardsTable));
        menu_unlock_everything = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load() {
        file.finish();
        MenuManager_Load(0x8000002, file.b.data());
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
    // Ticks até a fila da ponte esvaziar (cada Focus ou Press encerra um tick).
    void drain() {
        for (int i = 0; i < 64 && !MenuModern_Queue().empty(); ++i) tick();
    }
    M_CONTROL* control(uint32_t id, uint32_t tag) {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
};

// O controlador de vídeo com ganchos vazios (as opções embutidas usam).
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
        menu_modern_debug_power_on();
    }
    ~VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        vc.set_apply(nullptr);
        vc.set_store(nullptr);
        vc.init(nf::VideoSettings{});
        menu_modern_debug_power_on();
    }
};

// A dificuldade (0x40000023) e o mapa das missões (0x4000001c) do 08000002 reduzidos, como em test_frontend.cpp.
void wheel_pages(MenuFile& f) {
    f.page(0x40000023);
    f.control(0x40000023, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x100000bf, MCT_SCROLL, 313, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x100000e6, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x100000be, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x100001ee, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x10000104, MCT_LABEL, 114, 118, 150, 150);
    f.page(0x4000001c);
    f.control(0x40000005, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000000e, MCT_SCROLL, 312, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x100000e3, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x10000020, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000001f, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x10000102, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010e, MCT_LABEL, 50, 419, 540, 21);
    f.control(0x1000022c, MCT_LABEL, 123, 232, 394, 16);
}

// As missões: 0 de carro liberada, 1 a pé liberada, 2 a pé bloqueada; a 1 com uma entrada na RewardsTable (a medalha).
void fill_missions() {
    M_ITEM* m = reinterpret_cast<M_ITEM*>(sp_level);
    m[0] = M_ITEM{0x3000085, 0x100, 0, 0x9000001, 1, {}, 0};
    m[1] = M_ITEM{0x3000084, 0x101, 0, 0x7000005, 1, {}, 0};
    m[2] = M_ITEM{0x30000b0, 0x102, 0, 0x7000001, 0, {}, 0x112};
    std::memset(RewardsTable, 0, sizeof(RewardsTable));
    put32(RewardsTable, 0x7000005);
    for (uint32_t k = 0; k < 4; ++k) put32(RewardsTable + 8 + k * 4, 26 + k);
    for (uint32_t i = 0; i < 3; ++i) difficulty[i] = M_ITEM{0x30000a5, 0x14d + i, 0, i + 1, 1, {}, 0};
}

const Measure kMeasure = [](const std::wstring& s, const TextStyle& st) { return estimate_width(s, st); };

template <class S>
void frames(S& screen, const MmSnapshot& s, int n = 1) {
    const Canvas cv = make_canvas(1920, 1080);
    UiInput in;
    for (int i = 0; i < n; ++i) screen.frame(1.0f / 60.0f, in, s, cv, kMeasure);
}

bool queued(const MmRequest& r) {
    for (const MmRequest& q : MenuModern_Queue()) {
        if (q == r) return true;
    }
    return false;
}

// Um estado do menu principal montado à mão (o que MenuModern_Snapshot leria do jogo).
MmSnapshot front_snapshot(MmFrontView view, uint32_t page) {
    MmSnapshot s;
    s.screen = MmScreen::Front;
    s.status = 5;
    s.page = s.base_page = page;
    s.ready = true;
    s.front.view = view;
    return s;
}

MmWheelItem item(const char* name, bool enabled, uint32_t value = 0, bool driving = false) {
    MmWheelItem it;
    it.name = name;
    it.text = "text";
    it.enabled = enabled;
    it.value = value;
    it.driving = driving;
    return it;
}

}  // namespace

// ---- a ponte ----

TEST(modern_front_bridge_views_of_the_front_end_pages) {
    CHECK(MenuModern_FrontView(kMmFeStart) == MmFrontView::Title);
    CHECK(MenuModern_FrontView(kMmFeLanguage) == MmFrontView::Title);
    CHECK(MenuModern_FrontView(kMmFeMain) == MmFrontView::Main);
    CHECK(MenuModern_FrontView(kMmFeCampaign) == MmFrontView::Campaign);
    CHECK(MenuModern_FrontView(kMmFeName) == MmFrontView::Name);
    CHECK(MenuModern_FrontView(kMmFeMap) == MmFrontView::Map);
    CHECK(MenuModern_FrontView(kMmFeControls) == MmFrontView::Options);
    // O dossiê (a página da roda e as quatro seções) e os créditos são vistas Modern; as opções do multijogador e o
    // ajuste de tela ficam no estilo Original.
    CHECK(MenuModern_FrontView(0x4000002b) == MmFrontView::Dossier && MenuModern_FrontView(0x4000003a) == MmFrontView::Dossier);
    CHECK(MenuModern_FrontView(0x4000003d) == MmFrontView::Dossier);
    CHECK(MenuModern_FrontView(0x40000030) == MmFrontView::Credits);
    CHECK(MenuModern_FrontView(0x4000002e) == MmFrontView::None);
    CHECK(MenuModern_FrontView(0x40000047) == MmFrontView::None);
    // A tecla que digita cada caractere (o texto da tecla, maiúscula; o espaço é a tag 0x3e9).
    std::vector<MmKey> keys = {{10, "A"}, {11, "B"}, {1, "1"}, {kMmFeKeyDel, ""}, {kMmFeKeySpace, ""}, {kMmFeKeyEnd, ""}};
    CHECK(MenuModern_KeyFor(keys, 'a') == 10 && MenuModern_KeyFor(keys, 'B') == 11 && MenuModern_KeyFor(keys, '1') == 1);
    CHECK(MenuModern_KeyFor(keys, ' ') == kMmFeKeySpace && MenuModern_KeyFor(keys, 'z') == 0xffffffffu);
    CHECK(MenuModern_KeyFor(keys, '-') == 0xffffffffu);
}

TEST(modern_front_bridge_main_menu_focus_through_the_cursor) {
    FrontWorld w;
    MenuFile& f = w.file;
    f.page(0x40000002);
    f.control(0x40000002, MCT_WINDOW, 0, 0, 640, 480);
    const uint32_t ids[3] = {0x10000002, 0x10000003, 0x10000004};
    char* texts[3] = {kNightFire, kMultiplayer, kCodenamesText};
    for (int i = 0; i < 3; ++i) {
        f.control(ids[i], MCT_BUTTON, 168, int16_t(271 + 34 * i), 304, 30);
        f.msg(0x18, ptr32(texts[i]));
    }
    f.control(0x10000225, MCT_MEMO, 104, 370, 434, 43);
    f.control(0x100000ed, MCT_LABEL, 0, 0, 640, 480);
    w.load();
    // O fade de entrada do P_MAIN_Handler congela o menu por 15 ticks (uma mensagem atrasada); com o contador das
    // atrasadas em 0, como depois de menu_debug_power_on, nenhuma vaga está livre (free_delay_slot compara com <).
    menu_delay_frame = 1;
    MenuManager_Create(0x80000002u, 0x40000002, -1, 0, 0x28, 0, 5);
    w.tick(20);
    CHECK(menu_manager[0].frozen == 0);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Front && s.front.view == MmFrontView::Main);
    CHECK(s.front.buttons.size() == 3 && s.front.buttons[0] == "NightFire" && s.front.buttons[2] == "Codenames");
    CHECK(s.front.focus == 0);
    // O foco no botão "Codenames" pelo cursor do gerente: o Page_Update do jogo muda o foco.
    MenuModern_Focus(0x10000004, kMmAnyTag);
    w.drain();
    s = MenuModern_Snapshot();
    CHECK(s.front.focus == 2);
    CHECK(g_menu_modern_stats.focuses == 1 && g_menu_modern_stats.dropped == 0);
    // Um controle que não existe na página: o pedido se perde (sem efeito).
    MenuModern_Focus(0x10009999, kMmAnyTag);
    w.drain();
    CHECK(g_menu_modern_stats.dropped == 1);
}

TEST(modern_front_bridge_wheels_step_and_select_like_the_dpad) {
    FrontWorld w;
    wheel_pages(w.file);
    w.load();
    fill_missions();
    MenuManager_Create(0x80000002u, 0x40000023, -1, 0, 0x28, 0, 5);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x100000bf, 0);
    w.tick(2);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.front.view == MmFrontView::Difficulty && s.front.wheel == kMmFeWheelDifficulty);
    CHECK(s.front.items.size() == 3 && s.front.pos == 0 && s.front.max == 2);
    // Dois passos da roda (o 0x49 de cada um refaz os rótulos) e o X: a dificuldade 3 e o mapa.
    MenuModern_Step(kMmFeWheelDifficulty, 2);
    MenuModern_Press(kMmSelect);
    w.drain();
    CHECK(g_menu_modern_stats.steps == 2);
    CHECK(GameState.difficulty == 3);
    w.tick(12);
    CHECK(Menu_CurrentPage(0) == 0x4000001c);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x1000000e, 0);
    w.tick();
    // O mapa: as missões das tabelas do jogo, a de carro marcada, a bloqueada com o texto de bloqueada.
    s = MenuModern_Snapshot();
    CHECK(s.front.view == MmFrontView::Map && s.front.items.size() == 12);
    CHECK(s.front.items[0].driving && s.front.items[0].enabled && s.front.items[0].name == "Paris Prelude");
    CHECK(!s.front.items[1].driving && s.front.items[1].enabled && s.front.items[1].medal == 0);
    CHECK(!s.front.items[2].enabled && s.front.items[2].text == "Complete the previous mission.");
    CHECK(s.front.pos == 1);  // P_NFMAP_Handler: a última missão liberada
    // A medalha de ouro em 07000005 (com as de baixo, como Menu_SetLevelBonus as liga): o rótulo do jogo.
    Menu_SetLevelBonus(0x7000005, 3, 0);
    s = MenuModern_Snapshot();
    CHECK(s.front.items[1].medal == 3 && s.front.items[1].medal_label == "GOLD MEDAL");
    // Tudo liberado pela trapaça: a bloqueada conta como liberada.
    menu_unlock_everything = 1;
    CHECK(MenuModern_Snapshot().front.items[2].enabled);
    menu_unlock_everything = 0;
}

TEST(modern_front_bridge_typing_reaches_the_game_keyboard) {
    FrontWorld w;
    MenuFile& f = w.file;
    f.page(0x40000020);
    f.control(0x40000011, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000074, MCT_BUTTON, 120, 201, 32, 32, 10);
    f.msg(0x18, ptr32(kKeyA));
    f.control(0x10000074, MCT_BUTTON, 160, 201, 32, 32, 11);
    f.msg(0x18, ptr32(kKeyB));
    f.control(0x10000074, MCT_BUTTON, 204, 323, 25, 16, 1000);
    f.control(0x10000074, MCT_BUTTON, 290, 323, 60, 16, 1001);
    f.control(0x10000074, MCT_BUTTON, 404, 319, 25, 24, 0x3ea);
    f.control(0x10000242, MCT_LABEL, 50, 40, 540, 17);
    f.control(0x10000075, MCT_LABEL, 117, 135, 406, 16);
    w.load();
    MenuManager_Create(0x80000002u, 0x40000020, -1, 0, 0x28, 0, 5);
    w.tick();
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.front.view == MmFrontView::Name && s.front.entry == "BOND" && s.front.suggested && !s.front.cheat);
    CHECK(s.front.keys.size() == 5 && s.front.max_len == 8);
    // Uma tecla da tela pela ponte: o foco nela e o X (C_KEYBOARD_Handler); a primeira apaga o "BOND" sugerido.
    MenuModern_Key(MenuModern_KeyFor(s.front.keys, 'a'));
    w.drain();
    CHECK(std::strcmp(codename_entry, "A") == 0);
    // O limite do jogo: 8 letras.
    for (int i = 0; i < 10; ++i) MenuModern_Key(MenuModern_KeyFor(s.front.keys, 'b'));
    w.drain();
    CHECK(std::strcmp(codename_entry, "ABBBBBBB") == 0);
    MenuModern_Key(kMmFeKeyDel);
    w.drain();
    CHECK(std::strcmp(codename_entry, "ABBBBBB") == 0);
    s = MenuModern_Snapshot();
    CHECK(s.front.entry == "ABBBBBB" && !s.front.suggested);
    // A tela Modern digita pelas mesmas teclas: ' ' e '\n' viram o espaço e o End do jogo.
    VideoFixture v;
    FrontScreen fs;
    fs.open(s);
    frames(fs, s, 2);
    fs.type('a');
    fs.type('\b');
    fs.type(' ');
    fs.type('\n');
    fs.type('#');  // sem tecla no teclado do jogo: nada
    frames(fs, s);
    const std::vector<MmRequest> q = MenuModern_Queue();
    CHECK(q.size() == 8);
    if (q.size() == 8) {
        CHECK(q[0] == (MmRequest{MmAction::Focus, kMmFeKeys, 10, 0}) && q[1] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
        CHECK(q[2].b == kMmFeKeyDel && q[4].b == kMmFeKeySpace && q[6].b == kMmFeKeyEnd);
    }
    // O End com um nome que não vale ("DEFAULT"): a caixa do jogo (Menu_IsValidCodename), e a próxima tecla apaga.
    menu_modern_debug_power_on();
    std::snprintf(codename_entry, sizeof(codename_entry), "DEFAULT");
    MenuModern_Key(kMmFeKeyEnd);
    w.drain();
    CHECK(del_when_keypressed == 1 && menu_ls().active == 0);
}

// ---- a tela ----

TEST(modern_front_main_menu_rows_and_quit) {
    VideoFixture v;
    FrontScreen fs;
    MmSnapshot s = front_snapshot(MmFrontView::Main, kMmFeMain);
    s.front.buttons = {"NightFire", "Multiplayer", "Codenames"};
    s.front.focus = 0;
    fs.open(s);
    frames(fs, s, 2);
    CHECK(fs.view() == FrontScreen::View::Main && fs.focus() == 0);
    DrawList dl;
    fs.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.find_text(L"NIGHTFIRE") != nullptr && dl.find_text(L"QUIT GAME") != nullptr);
    // NIGHTFIRE: o X no botão com o foco (C_GONIGHTFIRE_Handler).
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && MenuModern_Queue()[0] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // MULTIPLAYER: o foco vai ao botão do jogo ao andar; escolher, o X (a caixa "not available yet" do jogo).
    fs.command(Nav::Down);
    frames(fs, s);
    CHECK(queued(MmRequest{MmAction::Focus, 0x10000003, kMmAnyTag, 0}));
    menu_modern_debug_power_on();
    s.front.focus = 1;
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // OPTIONS (só do port): as opções embutidas, sem pedido ao jogo; voltar das categorias fecha.
    fs.command(Nav::Down);
    frames(fs, s);
    fs.command(Nav::Down);
    frames(fs, s);
    menu_modern_debug_power_on();
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.options_open() && fs.options().active() && MenuModern_Queue().empty());
    fs.options().command(OptionsScreen::Cmd::Back);
    frames(fs, s);
    fs.options().command(OptionsScreen::Cmd::Back);
    frames(fs, s, 2);
    CHECK(!fs.options_open());
    // CREDITS (só do port): a página dos créditos do jogo, como o script do "Credits" da AV Options a mostra.
    fs.command(Nav::Down);
    frames(fs, s);
    CHECK(fs.focus() == 4);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && queued(MmRequest{MmAction::Show, kMmFeCredits, 0, 0}));
    menu_modern_debug_power_on();
    frames(fs, s, 2);
    // QUIT GAME: o diálogo do port, com CANCEL escolhido; voltar fecha, QUIT pede para sair.
    fs.command(Nav::Down);
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.dialog_open() && fs.dialog_focus() == 1 && !fs.take_quit());
    fs.command(Nav::Back);
    frames(fs, s);
    CHECK(!fs.dialog_open() && !fs.take_quit());
    // Voltar no menu principal também pergunta.
    fs.command(Nav::Back);
    frames(fs, s);
    CHECK(fs.dialog_open());
    fs.command(Nav::Left);
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.take_quit() && !fs.take_quit());
    CHECK(MenuModern_Queue().empty());
}

TEST(modern_front_locked_missions_send_nothing) {
    VideoFixture v;
    FrontScreen fs;
    MmSnapshot s = front_snapshot(MmFrontView::Map, kMmFeMap);
    s.front.wheel = kMmFeWheelMap;
    s.front.items = {item("Paris Prelude", true, 0x9000001, true), item("The Exchange", true, 0x7000005),
                     item("Alpine Escape", false, 0x9000005, true)};
    s.front.pos = 1;
    fs.open(s);
    frames(fs, s, 2);
    CHECK(fs.view() == FrontScreen::View::Map && fs.focus() == 1);
    // A missão liberada: a roda nela (já está) e o X (C_SBNFMAP_Handler carrega a fase).
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // A bloqueada: o foco anda (a roda do jogo acompanha) mas escolher não manda nada, só o aviso.
    fs.command(Nav::Down);
    frames(fs, s);
    CHECK(queued(MmRequest{MmAction::Step, kMmFeWheelMap, 0, 2}));
    menu_modern_debug_power_on();
    s.front.pos = 2;
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().empty() && !fs.toast().empty());
    // A de carro é escolhível (o port a pula e libera a seguinte): o passo e o X.
    fs.command(Nav::Up);
    frames(fs, s);
    fs.command(Nav::Up);
    frames(fs, s);
    menu_modern_debug_power_on();
    s.front.pos = 0;
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // A aba DOSSIER (E): o quadrado do jogo na roda.
    fs.command(Nav::TabNext);
    frames(fs, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmSquare, 0, 0}));
    // O desenho: o cadeado da bloqueada e a marca da de carro.
    DrawList dl;
    fs.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.count(DrawCmd::Padlock) >= 1 && dl.find_text(L"Driving") != nullptr);
}

TEST(modern_front_game_boxes_become_the_dialog) {
    VideoFixture v;
    FrontScreen fs;
    MmSnapshot s = front_snapshot(MmFrontView::Campaign, kMmFeCampaign);
    s.front.wheel = kMmFeWheelCampaign;
    s.front.items = {item("New Codename", true), item("Play without saving", true)};
    fs.open(s);
    frames(fs, s, 2);
    // A caixa de mensagem do jogo por cima (Menu_CreateOptionBox, o aviso do "Play without saving"): o Dialog, com as
    // respostas da lista dela.
    s.page = kMmBoxPage;
    s.dialog.open = s.dialog.box = true;
    s.dialog.body = "You will not be able to save.";
    s.dialog.options = {{"OK", 3}};
    s.dialog.list = kMmBoxList;
    frames(fs, s, 2);
    CHECK(fs.dialog_open());
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 2 && queued(MmRequest{MmAction::Pick, kMmBoxList, 0, 0}) &&
          queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // A caixa do cartão sem respostas ("Saving data..."): o Dialog sem botões, que não responde.
    s.page = kMmCardPage;
    s.dialog.body = "Saving data...";
    s.dialog.options.clear();
    s.dialog.list = kMmCardList;
    frames(fs, s, 2);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.dialog_open() && MenuModern_Queue().empty());
    DrawList dl;
    fs.build(dl, make_canvas(1920, 1080), kMeasure);
    // O título da caixa do cartão no PC (PcText::FeCardTitle): "Save data", não mais "Memory card".
    CHECK(dl.find_text(L"SAVE DATA") != nullptr && dl.find_text(L"MEMORY CARD") == nullptr &&
          dl.find_text(L"Saving data...") != nullptr);
    // A pergunta do cartão (sobrescrever?): voltar é o voltar do jogo; a caixa some com a do jogo.
    s.dialog.body = "Overwrite?";
    s.dialog.options = {{"Cancel", 4}, {"Yes", 2}};
    frames(fs, s, 2);
    fs.command(Nav::Right);
    frames(fs, s);
    CHECK(fs.dialog_focus() == 1 && queued(MmRequest{MmAction::Pick, kMmCardList, 0, 1}));
    menu_modern_debug_power_on();
    fs.command(Nav::Back);
    frames(fs, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmBack, 0, 0}));
    s.page = kMmFeCampaign;
    s.dialog = MmDialog{};
    frames(fs, s, 2);
    CHECK(!fs.dialog_open());
}

TEST(modern_front_codename_menu_opens_the_modern_options) {
    VideoFixture v;
    FrontScreen fs;
    MmSnapshot s = front_snapshot(MmFrontView::CodenameMenu, kMmFeCnMenu);
    s.front.wheel = kMmFeWheelCnMenu;
    const char* names[7] = {"Secret Unlocks", "Controller Setup", "Driving Controller", "Advanced Options",
                            "Multiplayer Options", "AV Options", "Save Codename"};
    for (const char* n : names) s.front.items.push_back(item(n, true));
    s.front.pos = 1;
    fs.open(s);
    frames(fs, s, 2);
    // Controller Setup: as opções Modern em CONTROLLER no lugar da página do jogo (nenhum pedido).
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.options_open() && fs.options().category() == Cat::Controller && MenuModern_Queue().empty());
    // Uma opção mudada lá marca o codinome como mudado ao sair (como o "escolher" das páginas do jogo).
    cn_modified_flag = 0;
    PlayerSetting[0].invert_look = PlayerSetting[0].invert_look == 0 ? 1 : 0;
    fs.options().command(OptionsScreen::Cmd::Back);
    frames(fs, s, 2);
    CHECK(!fs.options_open() && cn_modified_flag == 1);
    PlayerSetting[0].invert_look = 0;
    cn_modified_flag = 0;
    // Advanced Options em GAMEPLAY (os passos da roda não seguram a navegação); sem mudança, o codinome fica como estava.
    fs.command(Nav::Down);
    frames(fs, s);
    fs.command(Nav::Down);
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(fs.options_open() && fs.options().category() == Cat::Gameplay);
    fs.options().command(OptionsScreen::Cmd::Back);
    frames(fs, s, 2);
    CHECK(cn_modified_flag == 0);
    // Driving Controller e Multiplayer Options: travadas (o aviso, nenhum pedido).
    menu_modern_debug_power_on();
    fs.command(Nav::Up);
    frames(fs, s);
    s.front.pos = 2;
    frames(fs, s);
    menu_modern_debug_power_on();
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(!fs.options_open() && MenuModern_Queue().empty() && !fs.toast().empty());
    // Save Codename: o passo da roda e o X (C_SBCNOPTIONS_Handler grava pelo cartão).
    for (int i = 0; i < 4; ++i) {
        fs.command(Nav::Down);
        frames(fs, s);
    }
    menu_modern_debug_power_on();
    s.front.pos = 6;
    frames(fs, s);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // A página "Controller Setup" que o jogo mostra sozinho depois de criar um codinome: as opções Modern, e sair delas
    // é o voltar do jogo.
    MmSnapshot g = front_snapshot(MmFrontView::Options, kMmFeControls);
    g.front.option_page = kMmFeControls;
    frames(fs, g, 2);
    CHECK(fs.view() == FrontScreen::View::Options && fs.options_open() && fs.options().category() == Cat::Controller);
    fs.options().command(OptionsScreen::Cmd::Back);
    frames(fs, g, 2);
    CHECK(!fs.options_open() && queued(MmRequest{MmAction::Press, kMmBack, 0, 0}));
}

TEST(modern_front_title_and_warning) {
    VideoFixture v;
    FrontScreen fs;
    // O título: escolher só depois do texto legal (o convite do jogo aparece) e só na página do "Press START".
    MmSnapshot s = front_snapshot(MmFrontView::Title, kMmFeStart);
    s.front.legal = "NIGHTFIRE Interactive Game";
    fs.open(s);
    frames(fs, s, 2);
    CHECK(fs.view() == FrontScreen::View::Title && fs.blur() == 0.0f);
    fs.command(Nav::Select);
    frames(fs, s);
    CHECK(MenuModern_Queue().empty());
    s.front.press = "Press the START button";
    fs.command(Nav::Start);
    frames(fs, s);
    CHECK(MenuModern_Queue().size() == 1 && queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    DrawList dl;
    fs.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.find_text(L"PRESS THE START BUTTON") != nullptr && dl.find_text(L"NIGHTFIRE Interactive Game") != nullptr);
    // A arte do título e o desfoque que cresce quando o menu entra.
    ImageList art;
    fs.backdrop(art, make_canvas(1920, 1080));
    CHECK(art.size() == 1 && art[0].hash == 0x3000074);
    MmSnapshot m = front_snapshot(MmFrontView::Main, kMmFeMain);
    frames(fs, m, 120);
    CHECK(fs.view() == FrontScreen::View::Main && fs.blur() > 0.99f);
    ImageList pictures;
    fs.images(pictures, make_canvas(1920, 1080));
    CHECK(pictures.size() == 1 && pictures[0].hash == 0x300016d);
    // O filme de fundo da página (07330048 no menu principal) no lugar da arte, no mesmo retângulo 4:3 (a altura da
    // janela) e atrás do mesmo desfoque; sem a arte no pacote, também.
    fs.set_movie(true);
    ImageList movie;
    fs.backdrop(movie, make_canvas(1920, 1080));
    CHECK(movie.size() == 1 && movie[0].hash == kMovieImage && movie[0].r.x0 == art[0].r.x0 &&
          movie[0].r.y1 == art[0].r.y1 && fs.blur() > 0.99f);
    fs.set_art(false);
    movie.clear();
    fs.backdrop(movie, make_canvas(1920, 1080));
    CHECK(movie.size() == 1 && movie[0].hash == kMovieImage);
    fs.set_movie(false);
    movie.clear();
    fs.backdrop(movie, make_canvas(1920, 1080));
    CHECK(movie.empty());
    fs.set_art(true);
    // O aviso de controle desconectado: o texto do jogo e o do port, sem botões.
    WarningScreen ws;
    MmSnapshot w;
    w.screen = MmScreen::Warning;
    w.status = 2;
    w.page = w.base_page = kMmBoxPage;
    w.dialog.open = w.dialog.box = true;
    w.dialog.body = "analog controller is not inserted into controller port 1";
    ws.open(w);
    const Canvas cv = make_canvas(1920, 1080);
    ws.frame(1.0f / 60.0f, w, cv, kMeasure);
    CHECK(ws.active() && ws.body().find(L"Analog controller") == 0);
    DrawList wl;
    ws.build(wl, cv, kMeasure);
    CHECK(wl.find_text(L"CONTROLLER DISCONNECTED") != nullptr);
}
