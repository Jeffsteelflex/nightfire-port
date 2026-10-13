// Testes dos menus do jogo no estilo Modern (só do port): a ponte com os menus do jogo (engine/menu_modern.h), sobre
// um arquivo de menu feito à mão com a pausa, o fim de missão e o resultado do disco reduzidos ao que importa (como
// test_menu.cpp), e as telas sem GPU (src/ui/modern_pause.h, modern_end.h) com estados do jogo montados à mão: a
// navegação das abas, a situação dos objetivos, o que cada escolha manda ao jogo (a linha da lista e o botão do
// controle do PS2), o sim/não da confirmação e os fluxos do fim de missão e do resultado.

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
#include "engine/menu_modern.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/pc_text.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "platform/video_settings.h"
#include "test_framework.h"
#include "ui/modern_end.h"
#include "ui/modern_pause.h"

using namespace nf::ui;

namespace {

// ---- arquivo de menu (o formato de docs/formats.md, "Menus (item tipo 8)") ----
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
    void page(uint32_t id, uint32_t menu) {
        end_page();
        dw(0xfffffff0);
        dw(id);
        dw(menu);
        dw(0);
        by(0);
        page_size_at = b.size();
        dw(0);
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0, uint16_t set = 1,
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

char kMission[] = "MISSION";
char kObjectives[] = "OBJECTIVES";
char kControls[] = "CONTROLS";
char kScore[] = "SCORE";
char kContinue[] = "C O N T I N U E";
char kRestart[] = "R E S T A R T";
char kQuit[] = "Q U I T";
char kQuestion[] = "RESTART: ARE YOU SURE?";
char kYes[] = "Y E S";
char kNo[] = "N O";
char kNormal[] = "Normal";
char kAxis[] = "Y-Axis";
char kFailed[] = "Mission Failed";
char kReplay[] = "R E P L A Y";
char kScoreValue[] = "1";

// A pausa do disco (08000001, página 0x4000004b) reduzida: os quatro botões das abas primeiro (o foco vai ao primeiro
// 0x10000028), a lista principal (tag 6), a pergunta (0xc) e o sim/não (0xd) depois dela, os objetivos (8), os
// estilos (9), o mapa de botões (10) e o eixo Y (0xe, 0x10).
void pause_page(MenuFile& f) {
    f.page(0x4000004b, 0x80000002);
    f.control(0x40000001, MCT_WINDOW, 0, 0, 640, 480);
    const int16_t xs[4] = {84, 200, 340, 465};
    const uint32_t tags[4] = {0, 1, 3, 4};
    char* texts[4] = {kMission, kObjectives, kControls, kScore};
    for (int i = 0; i < 4; ++i) {
        f.control(0x10000028, MCT_BUTTON, xs[i], 120, 100, 16, tags[i], 2);
        f.msg(0x18, ptr32(texts[i]));
    }
    f.control(0x10000028, MCT_LIST, 153, 183, 334, 120, 6, 4, 1);
    f.msg(0x10, ptr32(kContinue), 0);
    f.msg(0x10, ptr32(kRestart), 0);
    f.msg(0x10, ptr32(kQuit), 0);
    f.control(0x10000028, MCT_LABEL, 180, 190, 280, 16, 0xc);
    f.msg(0x18, ptr32(kQuestion));
    f.control(0x10000028, MCT_LIST, 230, 215, 180, 80, 0xd, 4, 1);
    f.msg(0x10, ptr32(kYes), 0);
    f.msg(0x10, ptr32(kNo), 0);
    f.control(0x10000028, MCT_LIST, 100, 170, 440, 190, 8, 4, 1);
    f.control(0x10000028, MCT_LIST, 110, 160, 280, 30, 9, 4, 1);
    f.control(0x10000028, MCT_LIST, 90, 200, 460, 160, 10, 4, 1);
    f.control(0x10000028, MCT_LABEL, 460, 165, 90, 16, 0xe);
    f.msg(0x18, ptr32(kNormal));
    f.control(0x10000028, MCT_LABEL, 400, 165, 60, 16, 0x10);
    f.msg(0x18, ptr32(kAxis));
}

// O fim de missão (08000001, página 0x40000042, menu 0x80000004): o título e a lista com os valores do jogo.
void end_page(MenuFile& f) {
    f.page(0x40000042, 0x80000004);
    f.control(0x40000042, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000001, MCT_LABEL, 105, 120, 431, 16);
    f.msg(0x18, ptr32(kFailed));
    f.msg(0x2b, 2);
    f.control(0x100001a4, MCT_LIST, 197, 186, 246, 115, 0, 4, 1);
    f.msg(0x10, ptr32(kReplay), 0);
    f.msg(0x10, ptr32(kQuit), 2);
    f.msg(0x1d, 0);
}

// O resultado do menu principal (08000002): o botão com o foco (0x10000165), a pontuação; as estatísticas e o mapa das
// missões, para onde o "continuar" vai.
void front_end_pages(MenuFile& f) {
    f.page(0x40000036, 0x80000002);
    f.control(0x40000036, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000165, MCT_BUTTON, 60, 380, 50, 40);
    f.control(0x10000167, MCT_LABEL, 320, 330, 200, 16);
    f.msg(0x18, ptr32(kScoreValue));
    f.control(0x1000010e, MCT_LABEL, 50, 420, 400, 16);
    f.page(0x40000037, 0x80000002);
    f.control(0x40000037, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000022, MCT_LIST, 60, 80, 520, 300, 0, 4, 1);
    f.page(0x4000001c, 0x80000002);
    f.control(0x4000001c, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000000e, MCT_BUTTON, 60, 80, 100, 40);
}

// O heap, a camada 2D, a entrada e os menus como no ResetMap_GameInit, com a ponte instalada; uma missão de duas
// fases com objetivos.
struct ModernWorld {
    MenuFile file;
    OBJECTIVE_tag objs[4]{};
    ModernWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        menu_modern_debug_power_on();
        MenuModern_Install();
        mission_debug_power_on();
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.level_file = 0x7000012;
        GameState.difficulty = 2;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(MPGame, 0, sizeof(MPGame));
        for (obj_tag*& p : glb_players) p = nullptr;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        psiInput_SetModal(false);
        Input_Init();
        Input_Update();
        MenuManager_Init();
        // A missão 07000011, na fase 07000012 (ordem 1): um cumprido, um em andamento ("não deixe acontecer"), um à
        // espera do canal que o revela; a fase seguinte tem um ainda escondido.
        BaseMap = 0x7000011;
        ThisOrderNum = 1;
        MissionData[0] = MISSIONDATA_tag{0x7000011, 0x7000011, 0, &objs[0], 1, {}};
        MissionData[1] = MISSIONDATA_tag{0x7000012, 0x7000011, 1, &objs[1], 2, {}};
        MissionData[2] = MISSIONDATA_tag{0x700004a, 0x7000011, 2, &objs[3], 1, {}};
        objs[0].state = 4;
        objs[1].state = 2;
        objs[1].flags = 2;
        objs[2].state = 1;
        objs[2].trigger = 5;
        objs[3].state = 0;
        GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    }
    ~ModernWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_modern_debug_power_on();
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        mission_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        GameState.paused = 0;
        IsWarmReset = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load() {
        file.finish();
        MenuManager_Load(0x8000001, file.b.data());
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
    M_MANAGER& m() { return menu_manager[0]; }
    M_CONTROL* control(uint32_t id, uint32_t tag) {
        M_PAGE* p = m().page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
    bool shown(uint32_t tag) {
        M_CONTROL* c = control(0x10000028, tag);
        return c != nullptr && (c->state & 1) == 0;
    }
    void open_pause() {
        MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
        GS_PauseGame(1);
        tick(2);
    }
};

// O controlador de vídeo com ganchos vazios (a aba OPTIONS abre a tela de opções embutida).
struct VideoFixture {
    VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        vc.init(nf::VideoSettings{});
        vc.set_hardware({{1280, 720}, {1920, 1080}}, nf::DisplayMode{1920, 1080}, {2, 4});
        vc.set_apply([](const nf::VideoSettings&, const nf::VideoSettings&) {});
        vc.set_store([](const nf::VideoSettings&) { return true; });
        PcText_SetLanguage("en");
        nf::controls() = nf::ControlsConfig{};
        PlayerSetting[0].style = 0;
        PlayerSetting[0].invert_look = 0;
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

const Measure kMeasure = [](const std::wstring& s, const TextStyle& st) { return estimate_width(s, st); };

// Um estado de pausa montado à mão (o que MenuModern_Snapshot leria do jogo).
MmSnapshot pause_snapshot(bool multiplayer = false) {
    MmSnapshot s;
    s.screen = MmScreen::Pause;
    s.status = 3;
    s.page = s.base_page = kMmPausePage;
    s.ready = true;
    s.multiplayer = multiplayer;
    s.tab = 0;
    s.tab_text[0] = "MISSION";
    s.tab_text[1] = "OBJECTIVES";
    s.tab_text[3] = "CONTROLS";
    s.tab_text[4] = "SCORE";
    s.rows = {{"CONTINUE", 0}, {"RESTART", 0}, {"QUIT", 0}, {"OPTIONS", 3}};
    s.styles = {"NightFire", "Moonraker", "Octopussy"};
    s.axis_label = "Y-Axis";
    s.axis_value = "Normal";
    MmObjective a;
    a.state = MmObjState::Active;
    a.label = "Escape from the tower";
    a.status = "Incomplete";
    MmObjective h;
    s.objectives = {a, h};
    return s;
}

// Uma tela com quadros de 1/60 s.
template <class S>
void frames(S& screen, const MmSnapshot& s, int n = 1, float dt = 1.0f / 60.0f) {
    const Canvas cv = make_canvas(1920, 1080);
    UiInput in;
    for (int i = 0; i < n; ++i) screen.frame(dt, in, s, cv, kMeasure);
}

bool queued(const MmRequest& r) {
    for (const MmRequest& q : MenuModern_Queue()) {
        if (q == r) return true;
    }
    return false;
}

}  // namespace

// ---- a ponte ----

TEST(modern_bridge_text_and_objective_states) {
    CHECK(MenuModern_Utf8("C O N T I N U E", true) == "CONTINUE");
    CHECK(MenuModern_Utf8("Q U I T  G A M E", true) == "QUIT GAME");
    CHECK(MenuModern_Utf8("Y E S", true) == "YES");
    // Texto comum não muda; sem `collapse`, nem o espaçado.
    CHECK(MenuModern_Utf8("Mission Failed", true) == "Mission Failed");
    CHECK(MenuModern_Utf8("C O N T I N U E", false) == "C O N T I N U E");
    // Windows-1252: ’ (0x92) e … (0x85) viram UTF-8; o latim-1 também.
    CHECK(MenuModern_Utf8("Kiko\x92s", false) == "Kiko\xe2\x80\x99s");
    CHECK(MenuModern_Utf8("\x85", false) == "\xe2\x80\xa6");
    CHECK(MenuModern_Utf8("\xe9", false) == "\xc3\xa9");
    CHECK(MenuModern_Utf8(nullptr).empty());
    // A situação dos objetivos (OBJECTIVE_tag::state).
    CHECK(MenuModern_ObjectiveState(0) == MmObjState::Hidden);
    CHECK(MenuModern_ObjectiveState(1) == MmObjState::Hidden);
    CHECK(MenuModern_ObjectiveState(2) == MmObjState::Active);
    CHECK(MenuModern_ObjectiveState(3) == MmObjState::Active);
    CHECK(MenuModern_ObjectiveState(4) == MmObjState::Done);
    CHECK(MenuModern_ObjectiveState(5) == MmObjState::Failed);
}

TEST(modern_bridge_pause_snapshot_and_objectives) {
    ModernWorld w;
    pause_page(w.file);
    w.load();
    CHECK(MenuModern_Snapshot().screen == MmScreen::None);
    w.open_pause();
    const MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Pause && s.status == 3 && s.page == kMmPausePage && s.ready);
    // Os textos das abas e das linhas, com as letras espaçadas juntas.
    CHECK(s.tab == 0 && s.tab_text[1] == "OBJECTIVES" && s.tab_text[3] == "CONTROLS");
    CHECK(s.rows.size() == 3 && s.rows[0].text == "CONTINUE" && s.rows[1].text == "RESTART" && s.rows[2].text == "QUIT");
    CHECK(s.row == 0 && !s.dialog.open);
    // Os objetivos: os visíveis na ordem do jogo (o cumprido da fase anterior e o em andamento desta), depois os
    // escondidos (o que espera o canal e o da fase seguinte).
    CHECK(s.objectives.size() == 4 && s.visible == 2 && s.done == 1 && s.failed == 0);
    CHECK(s.objectives[0].state == MmObjState::Done && s.objectives[0].raw == 4);
    CHECK(s.objectives[1].state == MmObjState::Active && s.objectives[1].guard);
    CHECK(s.objectives[2].state == MmObjState::Hidden && s.objectives[3].state == MmObjState::Hidden);
    CHECK(s.level == 2 && s.levels == 3);
    // Um objetivo fracassado conta.
    w.objs[1].state = 5;
    const MmSnapshot f = MenuModern_Snapshot();
    CHECK(f.failed == 1 && f.objectives[1].state == MmObjState::Failed);
}

TEST(modern_bridge_tabs_style_and_axis_through_the_game) {
    ModernWorld w;
    pause_page(w.file);
    w.load();
    w.open_pause();
    CHECK(w.shown(kMmTagMain) && !w.shown(kMmTagStyles));
    // A aba CONTROLS: o cursor vai ao botão dela, o Page_Update muda o foco e o C_GCPAUSE_Handler mostra o painel.
    MenuModern_Tab(kMmTabControls);
    CHECK(MenuModern_Queue().size() == 1);
    w.tick();
    CHECK(MenuModern_Queue().empty());
    CHECK(MenuModern_Snapshot().tab == int(kMmTabControls));
    CHECK(w.shown(kMmTagStyles) && w.shown(kMmTagButtons) && !w.shown(kMmTagMain));
    // O esquema: a linha da lista dos estilos (0x1d, como cima/baixo) e o jogo troca o estilo do jogador.
    MenuModern_Pick(kMmPauseControls, kMmTagStyles, 2);
    w.tick();
    CHECK(PlayerSetting[0].style == 2 && MenuModern_Snapshot().style == 2);
    // O mapa de botões do estilo (Menu_DisplayControllerStyleList): 13 células, o símbolo de cada uma.
    const MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.buttons.size() == 13 && s.buttons[0].symbol == 'A' && s.buttons[12].symbol == 'T');
    // O círculo do jogo inverte o eixo Y (0x5e da lista dos estilos).
    CHECK(!s.inverted);
    MenuModern_Press(kMmCircle);
    w.tick();
    CHECK(PlayerSetting[0].invert_look == 1 && MenuModern_Snapshot().inverted);
    MenuModern_Press(kMmCircle);
    w.tick();
    CHECK(PlayerSetting[0].invert_look == 0);
    // De volta à aba MISSION: a lista principal de novo.
    MenuModern_Tab(kMmTabMission);
    w.tick();
    CHECK(w.shown(kMmTagMain) && !w.shown(kMmTagStyles));
    // Um pedido para um controle que não existe na página se perde (conta) sem mexer em nada.
    MenuModern_Tab(77);
    w.tick();
    CHECK(g_menu_modern_stats.dropped == 1 && MenuModern_Queue().empty());
    PlayerSetting[0].style = 0;
}

TEST(modern_bridge_restart_asks_and_yes_no_answer_through_the_list) {
    ModernWorld w;
    pause_page(w.file);
    w.load();
    w.open_pause();
    // RESTART: a linha 1 da lista principal e o X; o jogo mostra a pergunta e o sim/não, com o "não" escolhido.
    MenuModern_Select(kMmPauseControls, kMmTagMain, 1);
    w.tick();
    MmSnapshot s = MenuModern_Snapshot();
    // (A pergunta é o rótulo 0x1ff do *Txt.dat, que o tratador escreve na tag 0xc: sem o arquivo de texto, vazia.)
    CHECK(s.dialog.open && !s.dialog.box);
    CHECK(s.dialog.options.size() == 2 && s.dialog.options[0].text == "YES" && s.dialog.options[1].text == "NO");
    CHECK(s.dialog.focus == 1 && s.dialog.list == kMmPauseControls && s.dialog.tag == kMmTagConfirm);
    CHECK(!w.shown(kMmTagMain) && w.shown(kMmTagConfirm));
    // NÃO: a lista principal volta e a pausa continua.
    MenuModern_Select(kMmPauseControls, kMmTagConfirm, 1);
    w.tick();
    s = MenuModern_Snapshot();
    CHECK(!s.dialog.open && w.shown(kMmTagMain) && MenuManager_Exists(0) == 1);
    // De novo, e SIM: o jogo recomeça a missão (a primeira fase) e fecha a pausa.
    MenuModern_Select(kMmPauseControls, kMmTagMain, 1);
    w.tick();
    CHECK(MenuModern_Snapshot().dialog.open);
    MenuModern_Pick(kMmPauseControls, kMmTagConfirm, 0);
    w.tick();
    CHECK(MenuModern_Snapshot().dialog.focus == 0);
    MenuModern_Press(kMmSelect);
    w.tick();
    CHECK(MenuManager_Exists(0) == 0 && GameState.load_file == 0x7000011);
    CHECK(Menu_PlayerHasQuitOrRestarted() == 1);
}

TEST(modern_bridge_resume_and_start_close_the_pause) {
    ModernWorld w;
    pause_page(w.file);
    w.load();
    // RESUME: a linha 0 (CONTINUE) e o X; o C_GCPAUSE_Handler fecha o menu, que despausa (ação de fechar 0x1e).
    w.open_pause();
    CHECK(GameState.paused == 1);
    MenuModern_Select(kMmPauseControls, kMmTagMain, 0);
    w.tick();
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0);
    CHECK(MenuModern_Snapshot().screen == MmScreen::None);
    // O Start: o MenuManager_Monitor fecha e despausa; o Start fica sem o bit de apertado (o Player_Update do mesmo
    // tick não abre a pausa de novo).
    w.open_pause();
    MenuModern_Press(kMmStart);
    w.tick();
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0);
    CHECK(Input_Action(0, kActPause, kActionPressed) == 0);
}

TEST(modern_bridge_end_mission_and_results_flows) {
    ModernWorld w;
    end_page(w.file);
    front_end_pages(w.file);
    w.load();
    // O fim de missão: o título e as linhas do jogo com os valores; REPLAY recomeça a fase quente.
    const uint32_t restarts = g_mission_port.restarts;
    MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4);
    GS_PauseGame(1);
    w.tick(2);
    MmSnapshot s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Fail && s.fail_title == "Mission Failed");
    CHECK(s.fail_rows.size() == 2 && s.fail_rows[0].text == "REPLAY" && s.fail_rows[0].data == 0 && s.fail_rows[1].data == 2);
    // A morte (estado 3 da missão) é o que deixa a cortina de sangue do HUD por trás da tela.
    CHECK(!s.died);
    Mission_SetStatus(kMissionDied);
    CHECK(MenuModern_Snapshot().died);
    MenuModern_Select(kMmFailList, kMmAnyTag, 0);
    w.tick();
    CHECK(MenuManager_Exists(0) == 0 && GameState.load_file == 0x7000012 && IsWarmReset == 1);
    CHECK(g_mission_port.restarts == restarts + 1);
    IsWarmReset = 0;
    while (GameFlow_Depth() > 0) GameFlow_PopState();
    // O resultado no menu principal: o círculo abre as estatísticas, voltar volta, o X continua para o mapa das
    // missões (0x4000001c), onde o port carrega a próxima missão.
    GameState.level_file = 0x7000048;
    MenuManager_Create(0x80000002u, 0x40000036, -1, 0, 0x28, 0, 5);
    w.tick(2);
    s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Results && s.base_page == kMmResultsPage && s.score_value == "1");
    MenuModern_Press(kMmCircle);
    w.tick();
    s = MenuModern_Snapshot();
    CHECK(s.screen == MmScreen::Results && s.base_page == kMmStatsPage);
    MenuModern_Press(kMmBack);
    w.tick();
    CHECK(MenuModern_Snapshot().base_page == kMmResultsPage);
    MenuModern_Press(kMmSelect);
    w.tick();
    // O mapa das missões: a tela do menu principal Modern (src/ui/modern_front.h) no lugar do resultado.
    s = MenuModern_Snapshot();
    CHECK(Menu_CurrentPage(0) == 0x4000001c && s.screen == MmScreen::Front && s.front.view == MmFrontView::Map);
}

// ---- as telas ----

TEST(modern_pause_tabs_follow_the_game_tabs) {
    VideoFixture v;
    PauseScreen p;
    const MmSnapshot s = pause_snapshot();
    p.open(s);
    frames(p, s, 2);
    CHECK(p.active() && p.tab() == PauseScreen::Tab::Pause);
    // PAUSE MENU · OBJECTIVES · CONTROLS · SCORE · OPTIONS: a pausa do jogo mostra SCORE também com um jogador.
    CHECK(p.tabs().size() == 5 && p.tabs()[3] == PauseScreen::Tab::Score && p.tabs()[4] == PauseScreen::Tab::Options);
    // E (a próxima aba): OBJECTIVES, e o jogo recebe o foco no botão da aba dele (tag 1).
    p.command(Nav::TabNext);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Objectives && queued(MmRequest{MmAction::Tab, kMmTabObjectives, 0, 0}));
    menu_modern_debug_power_on();
    p.command(Nav::TabNext);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Controls && queued(MmRequest{MmAction::Tab, kMmTabControls, 0, 0}));
    menu_modern_debug_power_on();
    // SCORE: o foco no botão da aba do jogo (tag 4), que monta as estatísticas da missão.
    p.command(Nav::TabNext);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Score && queued(MmRequest{MmAction::Tab, kMmTabScore, 0, 0}));
    menu_modern_debug_power_on();
    // OPTIONS é só do port: a tela de opções embutida, sem pedido ao jogo.
    p.command(Nav::TabNext);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Options && p.options().active() && p.options().embedded());
    CHECK(MenuModern_Queue().empty());
    // A última aba não dá a volta; Q nas opções (que a recebem como delas) volta para SCORE.
    p.command(Nav::TabNext);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Options);
    p.command(Nav::TabPrev);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Score && !p.options().active());
    // Voltar de OBJECTIVES leva a PAUSE MENU (tag 0 no jogo, que está na aba 1).
    p.show_tab(PauseScreen::Tab::Objectives);
    MmSnapshot on_objectives = s;
    on_objectives.tab = int(kMmTabObjectives);
    frames(p, on_objectives);
    menu_modern_debug_power_on();
    p.command(Nav::Back);
    frames(p, on_objectives);
    CHECK(p.tab() == PauseScreen::Tab::Pause && queued(MmRequest{MmAction::Tab, kMmTabMission, 0, 0}));
    // No multijogador: PAUSE MENU · OBJECTIVES · CONTROLS · SCORE · OPTIONS.
    PauseScreen mp;
    const MmSnapshot ms = pause_snapshot(true);
    mp.open(ms);
    frames(mp, ms);
    CHECK(mp.tabs().size() == 5 && mp.tabs()[3] == PauseScreen::Tab::Score && mp.tabs()[4] == PauseScreen::Tab::Options);
}

TEST(modern_pause_rows_and_confirmation_map_to_the_game_events) {
    VideoFixture v;
    PauseScreen p;
    MmSnapshot s = pause_snapshot();
    p.open(s);
    frames(p, s, 2);
    // RESTART MISSION: a linha 1 da lista principal (tag 6) e o X.
    p.command(Nav::Down);
    frames(p, s);
    CHECK(p.menu_focus() == 1);
    p.command(Nav::Select);
    frames(p, s);
    CHECK(MenuModern_Queue().size() == 2);
    CHECK(MenuModern_Queue()[0] == (MmRequest{MmAction::Pick, kMmPauseControls, kMmTagMain, 1}));
    CHECK(MenuModern_Queue()[1] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // O jogo mostrou a confirmação: o Dialog do kit, com o "não" do jogo escolhido.
    s.row = 1;
    s.dialog.open = true;
    s.dialog.title = "RESTART: ARE YOU SURE?";
    s.dialog.options = {{"YES", 0}, {"NO", 0}};
    s.dialog.focus = 1;
    s.dialog.list = kMmPauseControls;
    s.dialog.tag = kMmTagConfirm;
    frames(p, s, 2);
    CHECK(p.dialog_open() && p.dialog_focus() == 1);
    // Esquerda: SIM, e a linha da lista do jogo acompanha.
    p.command(Nav::Left);
    frames(p, s);
    CHECK(p.dialog_focus() == 0 && queued(MmRequest{MmAction::Pick, kMmPauseControls, kMmTagConfirm, 0}));
    menu_modern_debug_power_on();
    // Escolher: a linha e o X na lista do sim/não.
    p.command(Nav::Select);
    frames(p, s);
    CHECK(MenuModern_Queue().size() == 2 && MenuModern_Queue()[0] == (MmRequest{MmAction::Pick, kMmPauseControls, kMmTagConfirm, 0}));
    CHECK(MenuModern_Queue()[1] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // Voltar na confirmação: o "não" do jogo (a última linha).
    p.command(Nav::Back);
    frames(p, s);
    CHECK(queued(MmRequest{MmAction::Pick, kMmPauseControls, kMmTagConfirm, 1}) && queued(MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // O jogo fechou a confirmação: o Dialog some.
    s.dialog = MmDialog{};
    frames(p, s, 2);
    CHECK(!p.dialog_open());
    // Voltar no topo e o Start: o Start do jogo (o MenuManager_Monitor fecha a pausa).
    p.command(Nav::Back);
    frames(p, s);
    CHECK(MenuModern_Queue().size() == 1 && MenuModern_Queue()[0] == (MmRequest{MmAction::Press, kMmStart, 0, 0}));
    menu_modern_debug_power_on();
    // RESUME: a linha 0.
    p.command(Nav::Up);
    frames(p, s);
    p.command(Nav::Select);
    frames(p, s);
    CHECK(queued(MmRequest{MmAction::Pick, kMmPauseControls, kMmTagMain, 0}));
    menu_modern_debug_power_on();
    // OPTIONS (a linha do port) abre a aba OPTIONS, sem pedido ao jogo.
    for (int i = 0; i < 3; ++i) {
        p.command(Nav::Down);
        frames(p, s);
    }
    p.command(Nav::Select);
    frames(p, s);
    CHECK(p.tab() == PauseScreen::Tab::Options && MenuModern_Queue().empty());
    // A pausa do jogo sumiu: a tela sai (a animação) e fica inativa.
    p.close();
    frames(p, s, 30);
    CHECK(!p.active() && !p.closing());
}

TEST(modern_pause_controls_tab_and_objective_rows) {
    VideoFixture v;
    PauseScreen p;
    MmSnapshot s = pause_snapshot();
    p.open(s, PauseScreen::Tab::Controls);
    frames(p, s, 2);
    CHECK(p.tab() == PauseScreen::Tab::Controls && queued(MmRequest{MmAction::Tab, kMmTabControls, 0, 0}));
    menu_modern_debug_power_on();
    s.tab = int(kMmTabControls);
    // O esquema: direita escolhe o próximo estilo na lista do jogo (tag 9); esquerda do primeiro dá a volta.
    p.command(Nav::Right);
    frames(p, s);
    CHECK(queued(MmRequest{MmAction::Pick, kMmPauseControls, kMmTagStyles, 1}));
    menu_modern_debug_power_on();
    p.command(Nav::Left);
    frames(p, s);
    CHECK(queued(MmRequest{MmAction::Pick, kMmPauseControls, kMmTagStyles, 2}));
    menu_modern_debug_power_on();
    // O eixo Y: direita liga (o círculo do jogo); já ligado, direita não manda nada.
    p.command(Nav::Down);
    frames(p, s);
    CHECK(p.focus() == 1);
    p.command(Nav::Right);
    frames(p, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmCircle, 0, 0}));
    menu_modern_debug_power_on();
    s.inverted = true;
    p.command(Nav::Right);
    frames(p, s);
    CHECK(MenuModern_Queue().empty());
    // OBJECTIVES: as linhas são os objetivos (o escondido inclusive); cima/baixo andam sem pedidos.
    p.show_tab(PauseScreen::Tab::Objectives);
    frames(p, s);
    menu_modern_debug_power_on();
    p.command(Nav::Down);
    frames(p, s);
    CHECK(p.focus() == 1 && MenuModern_Queue().empty());
    p.command(Nav::Down);
    frames(p, s);
    CHECK(p.focus() == 1);
    // A lista de desenho mostra o texto do jogo e o rótulo do escondido.
    DrawList dl;
    p.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.find_text(L"Escape from the tower") != nullptr && dl.find_text(L"Classified objective") != nullptr);
    CHECK(dl.find_text(L"Incomplete") != nullptr);
}

TEST(modern_fail_screen_selects_the_game_rows) {
    VideoFixture v;
    FailScreen f;
    MmSnapshot s;
    s.screen = MmScreen::Fail;
    s.status = 4;
    s.page = s.base_page = kMmFailPage;
    s.ready = true;
    s.fail_title = "Mission Failed";
    s.fail_reason = "Killed In Action";
    s.fail_rows = {{"REPLAY", 0}, {"QUIT", 2}};
    f.open(s);
    frames(f, s, 2);
    CHECK(f.active() && f.focus() == 0 && f.option_count() == 2);
    // As opções só respondem depois de aparecerem.
    f.command(Nav::Right);
    frames(f, s);
    CHECK(f.focus() == 0 && MenuModern_Queue().empty());
    frames(f, s, 60);
    f.command(Nav::Right);
    frames(f, s);
    CHECK(f.focus() == 1 && queued(MmRequest{MmAction::Pick, kMmFailList, kMmAnyTag, 1}));
    menu_modern_debug_power_on();
    // Escolher: a linha da lista do jogo e o X (P_ENDMISSION_Handler lê o valor dela).
    f.command(Nav::Left);
    frames(f, s);
    menu_modern_debug_power_on();
    f.command(Nav::Select);
    frames(f, s);
    CHECK(MenuModern_Queue().size() == 2 && MenuModern_Queue()[0] == (MmRequest{MmAction::Pick, kMmFailList, kMmAnyTag, 0}));
    CHECK(MenuModern_Queue()[1] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    // O desenho: o título, o motivo do jogo e as linhas.
    DrawList dl;
    f.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.find_text(L"MISSION FAILED") != nullptr && dl.find_text(L"KILLED IN ACTION") != nullptr);
    CHECK(dl.find_text(L"REPLAY") != nullptr);
    // O véu aparece devagar (o fundo do desfoque acompanha) e some ao fechar.
    CHECK(f.background() > 0.9f);
    f.close();
    frames(f, s, 40);
    CHECK(!f.active());
}

TEST(modern_results_screen_continue_stats_and_dossier) {
    VideoFixture v;
    ResultsScreen r;
    MmSnapshot s;
    s.screen = MmScreen::Results;
    s.status = 5;
    s.page = s.base_page = kMmResultsPage;
    s.ready = true;
    s.complete_title = "Mission Complete";
    s.score_value = "1";
    r.open(s);
    frames(r, s, 2);
    CHECK(r.view() == ResultsScreen::View::Results && r.focus() == 0);
    // CONTINUE: o X no botão da página (P_NFRESULTS_Handler continua).
    r.command(Nav::Select);
    frames(r, s);
    CHECK(MenuModern_Queue().size() == 1 && MenuModern_Queue()[0] == (MmRequest{MmAction::Press, kMmSelect, 0, 0}));
    menu_modern_debug_power_on();
    // STATS: o círculo (0x5e); E também.
    r.command(Nav::Down);
    frames(r, s);
    r.command(Nav::Select);
    frames(r, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmCircle, 0, 0}));
    menu_modern_debug_power_on();
    r.command(Nav::TabNext);
    frames(r, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmCircle, 0, 0}));
    menu_modern_debug_power_on();
    // DOSSIER: o quadrado (0x5d, o dossiê do jogo).
    r.command(Nav::Down);
    frames(r, s);
    r.command(Nav::Select);
    frames(r, s);
    CHECK(queued(MmRequest{MmAction::Press, kMmSquare, 0, 0}) && r.toast().empty());
    menu_modern_debug_power_on();
    // A página das estatísticas do jogo: a vista troca; voltar é o voltar do jogo.
    s.base_page = s.page = kMmStatsPage;
    frames(r, s, 2);
    CHECK(r.view() == ResultsScreen::View::Stats);
    r.command(Nav::Back);
    frames(r, s);
    CHECK(MenuModern_Queue().size() == 1 && MenuModern_Queue()[0] == (MmRequest{MmAction::Press, kMmBack, 0, 0}));
    // Sem a pontuação do jogo, as estatísticas mostram as categorias dele com "—".
    s.stats_names = {"Bond Moves", "TOTAL"};
    frames(r, s);
    DrawList dl;
    r.build(dl, make_canvas(1920, 1080), kMeasure);
    CHECK(dl.find_text(L"Bond Moves") != nullptr && dl.find_text(L"—") != nullptr);
}
