// Testes dos menus (engine/menu*.cpp): um arquivo de menu montado à mão no formato do disco
// (docs/formats.md, "Menus (item tipo 8)"), lido por MenuManager_Create; a navegação entre controles,
// a entrada pelo teclado do PC (psi_input), os scripts, os componentes, o menu de fim de missão e a
// pausa. Sem arquivo de texto (Txt_BindLabel devolve nulo) e sem fontes carregadas: os textos vêm de
// ponteiros no próprio arquivo (o port é de 32 bits, como o PS2).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_frontend.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

// Um arquivo de menu: os registros de MenuManager_Create.
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
    void page(uint32_t id, uint32_t menu, uint8_t kind = 0) {
        end_page();
        dw(0xfffffff0);
        dw(id);
        dw(menu);
        dw(0);  // filme
        by(kind);
        page_size_at = b.size();
        dw(0);
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0,
                 uint16_t set = 1, uint32_t fmt = 0, uint32_t text = 0, uint8_t kind = 0) {
        dw(0xfffffff9);
        dw(id);
        by(type);
        by(kind);
        w(uint16_t(x));
        w(uint16_t(y));
        w(uint16_t(ww));
        w(uint16_t(h));
        w(20);  // profundidade
        dw(tag);
        w(set);
        dw(fmt);
        dw(text);
    }
    void msg(uint8_t m, uint32_t a, uint32_t bb = 0) {
        dw(0xfffffff6);
        by(m);
        dw(a);
        dw(bb);
    }
    void script(uint32_t id, uint32_t flags = 0) {
        dw(0xfffffff7);
        dw(id);
        dw(flags);
    }
    void key(uint16_t frame, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t flags = 0) {
        dw(0xfffffff5);
        w(frame);
        w(uint16_t(x));
        w(uint16_t(y));
        w(uint16_t(ww));
        w(uint16_t(h));
        dw(flags);
    }
    void key_msg(uint32_t target, uint8_t m, uint32_t a, uint32_t bb = 0) {
        dw(0xfffffff4);
        dw(target);
        by(m);
        dw(a);
        dw(bb);
    }
    void set(uint16_t id) {
        dw(0xfffffffd);
        w(id);
    }
    void component(uint16_t count, int16_t l, int16_t t, int16_t r, int16_t bb) {
        dw(0xfffffffc);
        w(count);
        dw(0);  // textura (0: nenhuma, sem hashtable)
        w(uint16_t(l));
        w(uint16_t(t));
        w(uint16_t(r));
        w(uint16_t(bb));
    }
    void piece(int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t flags, uint32_t sw = 0, uint32_t sh = 0) {
        dw(0xfffffffb);
        for (int16_t v : {x, y, ww, h, int16_t(0), int16_t(0), int16_t(1), int16_t(1)}) w(uint16_t(v));
        dw(0x80808080u);
        dw(flags);
        dw(sw);
        dw(sh);
    }
    void finish() {
        end_page();
        dw(0xfffffff2);
        dw(0);
    }
};

uint32_t ptr32(const void* p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }

char kRetry[] = "TRY AGAIN";
char kQuit[] = "QUIT";

// O heap, a camada 2D e a entrada como no ResetMap_GameInit; os menus começam vazios.
struct MenuWorld {
    MenuFile file;
    MenuWorld() {
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
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.level_file = 0x7000012;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(MPGame, 0, sizeof(MPGame));
        // Sem jogador: o de um teste anterior apontaria para o heap já reiniciado (Player_RamLoad).
        for (obj_tag*& p : glb_players) p = nullptr;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        Input_Init();
        Input_Update();  // a primeira atualização só libera as travas
        MenuManager_Init();
    }
    ~MenuWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        GameState.paused = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load() {
        file.finish();
        MenuManager_Load(0x8000001, file.b.data());
    }
    // Um tick dos menus como no Game_Run, com as teclas seguradas.
    void tick(std::initializer_list<int> keys = {}, int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            for (int k : keys) g_psi_keyboard_mouse.down[k] = true;
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    M_MANAGER& m() { return menu_manager[0]; }
    M_CONTROL* control(uint32_t id, uint32_t tag = 0) {
        M_PAGE* p = m().page;
        if (p == nullptr) return nullptr;
        for (M_CONTROL* c = static_cast<M_CONTROL*>(ll_first(p->controls)); c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
};

// A página de fim de missão do disco (08000001): janela, o título, a lista com tentar de novo (valor 0)
// e sair (2), num menu 0x80000004; e uma página de outro menu, que MenuManager_Create pula.
void end_mission_page(MenuFile& f) {
    f.page(0x40000099, 0x80000002);
    f.control(0x40000099, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000077, MCT_LABEL, 100, 100, 100, 20);
    f.page(0x40000042, 0x80000004);
    f.control(0x40000042, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000001, MCT_LABEL, 105, 120, 431, 16);
    f.msg(0x2b, 6);  // desabilitado: não recebe o foco
    f.control(0x100001a4, MCT_LIST, 197, 186, 246, 115, 0, 4, 1);
    f.msg(0x10, ptr32(kRetry), 0);
    f.msg(0x10, ptr32(kQuit), 2);
    f.msg(0x1d, 0);
    f.msg(0x2b, 4);
    // Um controle de outra plataforma (1): não é criado, e as mensagens dele são puladas.
    f.control(0x10000555, MCT_LABEL, 0, 0, 10, 10, 0, 1, 0, 0, 1);
    f.msg(0x2b, 1);
}

}  // namespace

TEST(menu_script_line_and_spline_hit_the_keys) {
    CHECK_NEAR(Script_CalculateLine(0, 10, 30, 0, 0.0f), 10.0, 1e-6);
    CHECK_NEAR(Script_CalculateLine(0, 10, 30, 0, 0.5f), 20.0, 1e-6);
    CHECK_NEAR(Script_CalculateLine(0, 10, 30, 0, 1.0f), 30.0, 1e-6);
    // Catmull-Rom: passa pelos pontos do meio; numa reta igualmente espaçada é linear.
    CHECK_NEAR(Script_CalculateSpline(0, 10, 30, 40, 0.0f), 10.0, 1e-5);
    CHECK_NEAR(Script_CalculateSpline(0, 10, 30, 40, 1.0f), 30.0, 1e-4);
    CHECK_NEAR(Script_CalculateSpline(0, 10, 20, 30, 0.5f), 15.0, 1e-4);
}

TEST(menu_fixup_resolution_keeps_the_centre) {
    // 640 x 480 do arquivo para 512 x 448: o centro (x + w/2 - 320) escala 0.8 em volta de 256.
    alignas(4) uint8_t raw[0x24]{};
    NEW_CONTROL& d = *reinterpret_cast<NEW_CONTROL*>(raw);
    d.x = 270;
    d.y = 220;
    d.w = 100;
    d.h = 40;
    FixupResolution(&d);
    CHECK(d.w == 80 && d.h == 37);
    CHECK(d.x + d.w / 2 == 256 && d.y + d.h / 2 == 224);
    // Barra de rolagem: pelo menos 16 de altura.
    std::memset(raw, 0, sizeof(raw));
    raw[0x21] = MCT_SCROLL;
    d.w = 10;
    d.h = 4;
    FixupResolution(&d);
    CHECK(d.h == 16 && d.w == 8);
}

TEST(menu_create_reads_pages_controls_and_skips_others) {
    MenuWorld w;
    end_mission_page(w.file);
    w.load();
    CHECK(MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4) == 0);
    CHECK(MenuManager_Exists(0) == 1 && MenuManager_GetStatus() == 4);
    // Só a página do menu pedido; a de outra plataforma não vira controle.
    CHECK(g_menu_stats.pages == 1 && g_menu_stats.controls == 3);
    CHECK(Menu_CurrentPage(0) == 0x40000042);
    M_LISTBOX* list = reinterpret_cast<M_LISTBOX*>(w.control(0x100001a4));
    CHECK(list != nullptr && list->base.type == MCT_LIST);
    CHECK(w.control(0x10000555) == nullptr);
    // As linhas e a escolha: tentar de novo (valor 0) escolhido.
    CHECK(List_SendMessage(list, 0x33, 0, 0) == 2);
    CHECK(List_SendMessage(list, 0x35, 0, 0) == 0);
    CHECK(std::strcmp(list->selected->cells[0].text, "TRY AGAIN") == 0);
    // FixupResolution no controle: o centro x 197 + 123 - 320 = 0 -> 256.
    CHECK(list->base.x + list->base.w / 2 == 256);
    // Um segundo menu no mesmo gerente não abre.
    CHECK(MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4) == 0xffffffffu);
    // Sem arquivo, nada.
    MenuManager_Delete(0);
    CHECK(MenuManager_Exists(0) == 0 && MenuManager_GetStatus() == 0);
    MenuManager_Load(0, nullptr);
    CHECK(MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4) == 0xffffffffu);
}

TEST(menu_find_control_moves_between_buttons) {
    MenuWorld w;
    // Três botões numa coluna e um à direita do do meio.
    w.file.page(0x40000077, 0x80000002);
    w.file.control(0x40000077, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x10000002, MCT_BUTTON, 270, 100, 100, 40, 0);
    w.file.control(0x10000003, MCT_BUTTON, 270, 200, 100, 40, 1);
    w.file.control(0x10000004, MCT_BUTTON, 270, 300, 100, 40, 2);
    w.file.control(0x10000005, MCT_BUTTON, 470, 200, 100, 40, 3);
    w.load();
    MenuManager_Create(0x80000002u, 0x40000077, 0, 0, 0x28, 0, 5);
    // O foco começa no id 0x10000002 (MenuManager_Create) e o cursor no meio dele.
    w.tick();
    uint32_t id = 0, tag = 0;
    CHECK(Menu_Focus(0, &id, &tag) && id == 0x10000002);
    M_CONTROL* first = w.control(0x10000002);
    CHECK(w.m().cursor->x == first->x + first->w / 2 && w.m().cursor->y == first->y + first->h / 2);
    // Para baixo: o vizinho de baixo; para a direita a partir dele: o da direita; para cima no topo dá a
    // volta para o de baixo de tudo.
    CHECK(Menu_FindControl(&w.m(), 2) == w.control(0x10000003, 1));
    w.tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == 0x10000003);
    CHECK(Menu_FindControl(&w.m(), 8) == w.control(0x10000005, 3));
    w.tick();
    CHECK(Menu_FindControl(&w.m(), 4) == w.control(0x10000003, 1));
    w.tick();
    CHECK(Menu_FindControl(&w.m(), 1) == w.control(0x10000002, 0));
    w.tick();
    CHECK(Menu_FindControl(&w.m(), 1) == w.control(0x10000004, 2));
    // A seta do teclado faz o mesmo pelo MenuManager_Update (a ação 0x1b, apertada).
    w.tick();
    w.tick({VK_DOWN});
    w.tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == 0x10000002);  // de baixo, para baixo, dá a volta
}

TEST(menu_end_mission_list_choice_restarts_the_level) {
    MenuWorld w;
    end_mission_page(w.file);
    w.load();
    // A câmera 6 (o esmaecimento entre fases), que o GF_FADE_REQUEST usa.
    viewer_tag fade_view{};
    viewer_tag* saved_fade_view = glb_viewer[6];
    glb_viewer[6] = &fade_view;
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    const uint32_t restarts = g_mission_port.restarts;
    MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4);
    GS_PauseGame(1);
    w.tick({}, 3);
    M_LISTBOX* list = reinterpret_cast<M_LISTBOX*>(w.control(0x100001a4));
    CHECK(list != nullptr);
    // Para baixo pela barra da lista (o foco é a lista): sair; para cima: tentar de novo.
    w.tick({VK_DOWN});
    w.tick();
    CHECK(List_SendMessage(list, 0x35, 0, 0) == 2);
    w.tick({VK_UP});
    w.tick();
    CHECK(List_SendMessage(list, 0x35, 0, 0) == 0);
    // Escolher (Espaço, ação 0x1f): P_ENDMISSION_Handler recomeça a fase (quente, sem filme), empilha o
    // fade e fecha o menu.
    w.tick({VK_SPACE});
    CHECK(MenuManager_Exists(0) == 0);
    CHECK(GameState.load_file == 0x7000012 && IsWarmReset == 1);
    CHECK(g_mission_port.restarts == restarts + 1);
    // O pedido de fade vira a espera do fade ao empilhar (GameFlow_PushState), por cima da carga: o preto em 80 ticks
    // na câmera 6.
    CHECK(GameFlow_GetState() == GF_FADE_WAIT);
    CHECK(fade_view.fade_state == 1 && fade_view.fade_rgba[0] == 0 && fade_view.fade_speed > 0.0f);
    GameFlow_PopState();
    CHECK(GameFlow_GetState() == GF_LOAD_MAP);
    glb_viewer[6] = saved_fade_view;
    IsWarmReset = 0;
}

TEST(menu_end_mission_quit_goes_to_the_front_end) {
    MenuWorld w;
    end_mission_page(w.file);
    w.load();
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4);
    w.tick({}, 2);
    // Com o status 4, o Start (Enter) também escolhe (Menu_InputAction).
    M_LISTBOX* list = reinterpret_cast<M_LISTBOX*>(w.control(0x100001a4));
    List_SendMessage(list, 0x1e, 2, 1);  // escolhe a linha de valor 2 sem avisar
    w.tick({VK_RETURN});
    CHECK(MenuManager_Exists(0) == 0);
    CHECK(GameState.load_file == 0x7000048 && GameState.menu_page == 0x4000001c);
}

TEST(menu_pause_opens_and_start_closes_and_unpauses) {
    MenuWorld w;
    w.file.page(0x4000004b, 0x80000002);
    w.file.control(0x4000004b, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x10000028, MCT_BUTTON, 84, 120, 107, 16, 0);
    w.load();
    // Como o Player_Update: o menu de pausa, fechado pelo Start (0x1e), e a pausa do jogo.
    MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
    GS_PauseGame(1);
    CHECK(GameState.paused == 1 && GS_IsPaused(-1) && GS_IsPaused(0));
    w.tick({}, 5);
    CHECK(MenuManager_GetStatus() == 3 && Menu_CurrentPage(0) == 0x4000004b);
    // Escolher (Espaço) num botão sem tratador para a tag dele não fecha.
    w.tick({VK_SPACE});
    w.tick();
    CHECK(MenuManager_Exists(0) == 1);
    // Start (Enter): MenuManager_Monitor fecha e despausa; o Start fica ligado sem o bit de apertado,
    // para o Player_Update do mesmo tick não abrir de novo.
    w.tick({VK_RETURN});
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0 && !GS_IsPaused(0));
    CHECK(Input_Action(0, kActPause, kActionPressed) == 0);
    CHECK(Menu_PlayerHasQuitOrRestarted() == 0);
    // GS_PausePlayer só mexe no jogador pedido.
    GS_PausePlayer(1, 2);
    CHECK(GS_IsPaused(2) && !GS_IsPaused(0));
    GS_PausePlayer(0, 2);
}

TEST(menu_input_action_quirks) {
    MenuWorld w;
    // Voltar (0x22) segurado anula escolher e Start; escolher anula voltar.
    w.tick({VK_SPACE, VK_BACK});
    CHECK(Menu_InputAction(0, 0x1f, 4) == 0);
    CHECK(Menu_InputAction(0, 0x22, 4) == 0);
    w.tick({});
    w.tick({VK_BACK});
    CHECK(Menu_InputAction(0, 0x22, 4) == 100);
    // Start só vale como escolher com um menu de status acima de 3.
    w.tick({});
    w.tick({VK_RETURN});
    CHECK(Menu_InputAction(0, 0x1f, 4) == 0);
    menu_manager[1].status = 4;
    CHECK(Menu_InputAction(0, 0x1f, 4) == 1);
    CHECK(Menu_InputAction(-1, 0x1f, 4) == 1);
    menu_manager[1].status = 0;
}

TEST(menu_script_keyframes_move_the_control_and_send_messages) {
    MenuWorld w;
    w.file.page(0x40000077, 0x80000002);
    w.file.control(0x40000077, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x10000002, MCT_LABEL, 0, 0, 640, 480);  // tela cheia: não passa por FixupResolution
    w.file.script(0x20000005);
    w.file.key(0, 0, 0, 640, 480);
    w.file.key(10, 0, 0, 640, 480);
    w.file.key_msg(0xfffffffe, 0x2b, 1);  // no fim: esconde o próprio rótulo
    w.load();
    MenuManager_Create(0x80000002u, 0x40000077, 0, 0, 0x28, 0, 5);
    M_LABEL* l = reinterpret_cast<M_LABEL*>(w.control(0x10000002));
    CHECK(l != nullptr && l->base.scripts.count == 1);
    M_SCRIPT* s = static_cast<M_SCRIPT*>(ll_first(l->base.scripts));
    CHECK(s->keys.count == 2);
    // As chaves passam por FixupResolution (640 x 480 -> 512 x 448).
    M_KEYFRAME* k0 = static_cast<M_KEYFRAME*>(ll_first(s->keys));
    CHECK(k0->w == 512 && k0->h == 448 && k0->next->frame == 10);
    // Move a segunda chave e toca: o rótulo anda linearmente e esconde no fim.
    k0->next->x = 100;
    CHECK(Script_Play(&l->base, 0x20000005, 0) == 1);
    for (int i = 0; i < 6; ++i) Script_RunFrame(&w.m(), &l->base);
    CHECK(l->base.x == 50 && l->base.state == 0);
    for (int i = 0; i < 6; ++i) Script_RunFrame(&w.m(), &l->base);
    CHECK(l->base.x == 100 && (l->base.state & 1) != 0 && l->base.key == nullptr);
    // Script_AddKeyFrame num quadro existente atualiza a chave (a não ser que ela seja fixa).
    M_KEYFRAME upd{};
    upd.frame = 10;
    upd.x = 7;
    CHECK(Script_AddKeyFrame(s, &upd) == k0->next && k0->next->x == 7 && s->keys.count == 2);
    k0->next->flags = 0x20;
    upd.x = 9;
    CHECK(Script_AddKeyFrame(s, &upd) == nullptr && k0->next->x == 7);
    // Uma chave nova entra na ordem dos quadros.
    upd.frame = 5;
    M_KEYFRAME* mid = Script_AddKeyFrame(s, &upd);
    CHECK(mid != nullptr && k0->next == mid && mid->next->frame == 10 && s->keys.count == 3);
}

TEST(menu_component_pieces_fill_the_control) {
    MenuWorld w;
    // Componente 0 do conjunto 1, em todos os estados (0xf0): ponta esquerda de 8, ponta direita de 8
    // (flag 2) e o miolo que estica (em x 8: as peças que esticam não andam sozinhas).
    w.file.set(1);
    w.file.component(3, 4, 2, 4, 2);
    w.file.piece(0, 0, 8, 16, 0xf0);
    w.file.piece(0, 0, 8, 16, 0xf2);
    w.file.piece(8, 0, 0, 16, 0xf0, 1);
    w.file.page(0x40000077, 0x80000002);
    w.file.control(0x40000077, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x10000002, MCT_BUTTON, 270, 100, 100, 40, 0, 1);
    w.load();
    MenuManager_Create(0x80000002u, 0x40000077, 0, 0, 0x28, 0, 5);
    w.tick();
    M_CONTROL* b = w.control(0x10000002);
    CHECK(b != nullptr && b->sprite_count == 3);
    const M_COMPONENT* k = Component_GetHandle(b);
    CHECK(k->left == 4 && k->top == 2 && k->right == 4 && k->bottom == 2 && k->count == 3);
    sprite** s = b->sprites;
    CHECK(s[0]->x == b->x && s[0]->w == 8);
    CHECK(s[1]->x == b->x + b->w - 8 && s[1]->w == 8);
    CHECK(s[2]->x == b->x + 8 && s[2]->w == b->w - 16);
    // Prioridade: a profundidade do controle menos a da página (5 na página atual).
    CHECK(s[0]->viewer == 7 && s[0]->priority == int8_t(b->depth - w.m().page->base.depth));
    // O rótulo do botão fica dentro das margens do componente.
    const M_BUTTON* button = reinterpret_cast<const M_BUTTON*>(b);
    CHECK(button->label.base.x == b->x + 4 && button->label.base.w == b->w - 8);
    // Escondido (0x2b 1): os sprites do componente somem.
    __Menu_SendMessage(b, 0x2b, 1, 0);
    CHECK(s[0]->priority == -1);
}

TEST(menu_label_text_colour_and_sprite) {
    MenuWorld w;
    static char hello[] = "HELLO";
    w.file.page(0x40000077, 0x80000002);
    w.file.control(0x40000077, MCT_WINDOW, 0, 0, 640, 480);
    w.file.control(0x10000002, MCT_LABEL, 270, 100, 100, 40);
    w.file.msg(0x18, ptr32(hello));
    w.file.msg(0x1b, 0x11223344);
    w.load();
    MenuManager_Create(0x80000002u, 0x40000077, 0, 0, 0x28, 0, 5);
    w.tick();
    M_LABEL* l = reinterpret_cast<M_LABEL*>(w.control(0x10000002));
    CHECK(l->spr != nullptr && l->spr->text == hello && l->spr->viewer == 7);
    // Com o foco (é o único controle): a cor de foco; o alfa da segunda cor fica em até 0x80.
    CHECK(l->spr->colour == 0x11223344u && (l->spr->colour2 & 0xff) == 0x44);
    // Formato padrão centrado (0xfe 3): x no meio do controle.
    CHECK(l->spr->x == l->base.x + l->base.w / 2);
    // Imagem (0x24): o sprite vira o retângulo do controle, sem texto.
    Label_SendMessage(l, 0x24, 0x3000001, 0);
    w.tick();
    CHECK(l->spr->text == nullptr && l->spr->x == l->base.x && l->spr->w == l->base.w);
    CHECK(Label_SendMessage(l, 0x2f, 0, 0) == int32_t(ptr32(hello)));
}

TEST(menu_rewards_medals_and_level_bonus) {
    MenuWorld w;
    // Uma fase com medalhas: 1 bit 3 tipo 0, 2 bit 4 tipo 1, 3 bit 5 tipo 1, 4 bit 6 tipo 4 (fichas).
    std::memset(RewardsTable, 0, sizeof(RewardsTable));
    const uint32_t level = 0x7000077;
    std::memcpy(RewardsTable, &level, 4);
    const uint32_t words[4] = {3, 0x10004, 0x10005, 0x40006};
    std::memcpy(RewardsTable + 8, words, sizeof(words));
    std::memset(sp_level, 0, sizeof(sp_level));
    std::memcpy(sp_level + 0xc, &level, 4);
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    CHECK(Menu_GetLevelIndex(level) == 0 && Menu_GetLevelIndex(0x7000001) == 0xffffffffu);
    REWARDINFO_tag info{};
    uint64_t bits = 0;
    PlrStarts_ProcessRewardCounter(&bits, level, 2, 0, &info);
    CHECK(info.kind == 1 && info.bit == 4 && info.has == 0);
    PlrStarts_ProcessRewardCounter(&bits, level, 2, 1, &info);
    CHECK(bits == (uint64_t(1) << 4));
    PlrStarts_ProcessRewardCounter(&bits, 0x7000001, 2, 0, &info);
    CHECK(info.kind == 5);  // fase fora da tabela
    // A missão cumprida com a medalha 3 liga as medalhas 3, 2 e 1 e marca a missão e a seguinte.
    CHECK(Menu_SetLevelBonus(level, 3, 0) == 0);
    CHECK(menu_bonus[0xa4] == 1 && sp_level[0x10] == 1 && sp_level[0x18 + 0x10] == 1);
    CHECK(Menu_GetBonus(0) == ((uint64_t(1) << 3) | (uint64_t(1) << 4) | (uint64_t(1) << 5)));
    CHECK(Menu_GetBestMedal(level, 0) == 3 && Menu_HasMedal(level, 4, 0) == 0);
    CHECK(Menu_GetNightfireStatus() == 3);
    // De novo com a mesma medalha: nada novo.
    CHECK(Menu_SetLevelBonus(level, 3, 0) == 0 && menu_bonus[0xa4] == 0);
    const uint8_t* b = Menu_GetLevelBonuses(level);
    CHECK(b[4] == 1 && b[0x10] == 1 && b[0x1c] == 1 && b[0x28] == 0);
    std::memset(sp_level, 0, sizeof(sp_level));
}

TEST(menu_number_and_time_formats) {
    char buf[64];
    SeparateNumber(7, buf);
    CHECK(std::strcmp(buf, "7") == 0);
    SeparateNumber(12345, buf);
    CHECK(std::strcmp(buf, "12,345") == 0);
    SeparateNumber(1002003, buf);
    CHECK(std::strcmp(buf, "1,002,003") == 0);
    CHECK(std::strcmp(Timer_Seconds2String(12550, 0), "02:06") == 0);   // 125,5 s arredonda para 126
    CHECK(std::strcmp(Timer_Seconds2String(366000, 0), "01:01:00") == 0);
    CHECK(std::strcmp(Timer_Seconds2String(-5, 2), "00:00") == 0);
}

TEST(menu_handler_table_and_port_registration) {
    // Os portados respondem; os ainda não portados (o multijogador) são stubs (nulos); ids sem
    // tratador não existem.
    CHECK(Menu_FindHandler(0x4000004b)->handler == P_PAUSE_Handler);
    CHECK(Menu_FindHandler(0x40000042)->handler == P_ENDMISSION_Handler);
    CHECK(Menu_FindHandler(0x40000002)->handler == P_MAIN_Handler);
    CHECK(Menu_FindHandler(0x4000002d)->handler == P_CNOPTIONS_Handler);
    CHECK(Menu_FindHandler(0x40000012) != nullptr && Menu_FindHandler(0x40000012)->handler == nullptr);
    CHECK(Menu_FindHandler(0x100001a4) == nullptr);
    // Uma página do port registra o próprio tratador.
    static int calls = 0;
    const MenuHandler h = [](uint8_t, M_CONTROL*, uint32_t, uint32_t, int32_t, int32_t) -> int32_t {
        ++calls;
        return 1;
    };
    Menu_PortRegisterHandler(0x40001000, "P_PCOPTIONS", h);
    M_CONTROL c{};
    c.id = 0x40001000;
    CHECK(Handler_HandleMessage(0, &c, 0x4c, 0, 0) == 1 && calls == 1);
    Menu_PortRegisterHandler(0x40001000, nullptr, nullptr);
    CHECK(Handler_HandleMessage(0, &c, 0x4c, 0, 0) == 0 && calls == 1);
}
