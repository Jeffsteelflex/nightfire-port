// Testes das opções do PC (só do port): a configuração de vídeo e interface no nightfire.ini
// (platform/video_settings.h), as listas de valores e de resoluções, o controlador com a confirmação de
// resolução, a tabela de textos do port (engine/pc_text.h) e as páginas do menu (engine/menu_pc_options.h)
// montadas sobre um arquivo de menu feito à mão com a pausa e a caixa de mensagem (como test_menu.cpp).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "engine/font.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/map_types.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_mouse.h"
#include "engine/menu_pc_options.h"
#include "engine/modules.h"
#include "engine/pc_text.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/psi.h"
#include "platform/psi_2d.h"
#include "platform/psi_input.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "test_framework.h"

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
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0,
                 uint16_t set = 1, uint32_t fmt = 0, uint32_t text = 0) {
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
        dw(text);
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

// Uma fonte só com ASCII ('!', 'A', 'a': os outros códigos desenham o glifo anterior, como no jogo) nas três
// entradas de FontTable: as listas medem as linhas com ela, e os caracteres acima de 0x7f não têm glifo.
CHARDATA_tag g_chars[3];
FONT_tag g_font;
pstex_tag g_font_rec;
maptex_tag g_font_tex;
constexpr uint32_t kFontHash = 0x03000998;

void make_font() {
    font_debug_power_on();
    std::memset(FontTable, 0, sizeof(FontTable));
    const uint16_t codes[3] = {'!', 'A', 'a'};
    for (int i = 0; i < 3; ++i) {
        g_chars[i] = CHARDATA_tag{};
        g_chars[i].code = codes[i];
        g_chars[i].u = uint16_t(i * 10);
        g_chars[i].w = 8;
        g_chars[i].h = 10;
        g_chars[i].post = 1;
    }
    g_font = FONT_tag{0, nullptr, 0x21, 0xff, 3, g_chars, 6.0f, 4.0f};
    for (int k = 0; k < 3; ++k) FontTable[k] = {kFontHash, &g_font};
    g_font_rec = pstex_tag{};
    g_font_tex = maptex_tag{&g_font_rec, 256, 64};
    hashtable_additem(kFontHash, &g_font_tex);
}

void drop_font() {
    font_debug_power_on();
    std::memset(FontTable, 0, sizeof(FontTable));
}

char kContinue[] = "C O N T I N U E";
char kRestart[] = "R E S T A R T";
char kQuit[] = "Q U I T";

// A pausa do disco reduzida ao que importa: a janela, a lista principal (0x10000028, tag 6, conjunto 4)
// com as três linhas do arquivo (valor 0), e a página da caixa de mensagem (0x4000002f: o fundo, o texto
// e a lista das respostas).
void pause_file(MenuFile& f) {
    f.page(0x4000004b, 0x80000002);
    f.control(0x40000001, MCT_WINDOW, 0, 0, 640, 480, 2);
    f.control(0x10000028, MCT_LIST, 153, 183, 334, 120, 6, 4, 1);
    f.msg(0x10, ptr32(kContinue), 0);
    f.msg(0x10, ptr32(kRestart), 0);
    f.msg(0x10, ptr32(kQuit), 0);
    f.page(0x4000002f, 0x80000002);
    f.control(0x40000003, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000011e, MCT_BUTTON, 120, 150, 400, 180, 0, 1);
    f.control(0x1000011f, MCT_MEMO, 130, 160, 380, 60, 0, 1);
    f.control(0x10000120, MCT_LIST, 200, 230, 240, 60, 0, 4, 1);
}

// O que o gancho de aplicação recebeu.
struct ApplyLog {
    std::vector<nf::VideoSettings> next;
    std::vector<nf::VideoSettings> stored;
};
ApplyLog* g_log = nullptr;

// O heap, a camada 2D, a entrada e os menus como no ResetMap_GameInit; as páginas do PC instaladas e o
// controlador com ganchos que só registram.
struct PcMenuWorld {
    MenuFile file;
    ApplyLog log;
    PcMenuWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        make_font();
        txt_debug_power_on();
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        menu_pc_debug_power_on();
        PcText_SetLanguage("pt");
        PcText_SetHdText(false);
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.level_file = 0x7000012;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(MPGame, 0, sizeof(MPGame));
        for (obj_tag*& p : glb_players) p = nullptr;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        Input_Init();
        Input_Update();
        MenuManager_Init();
        MenuPc_Install();
        g_log = &log;
        nf::VideoSettings v;
        v.width = 1280;
        v.height = 720;
        v.fov = 60.0f;
        nf::VideoController& vc = nf::video_controller();
        vc.init(v);
        vc.set_hardware({{1280, 720}, {1920, 1080}, {2560, 1440}, {800, 600}}, {2560, 1440}, {2, 4, 8});
        vc.set_apply([](const nf::VideoSettings& next, const nf::VideoSettings&) { g_log->next.push_back(next); });
        vc.set_store([](const nf::VideoSettings& s) {
            g_log->stored.push_back(s);
            return true;
        });
        pause_file(file);
        file.finish();
        MenuManager_Load(0x8000001, file.b.data());
    }
    ~PcMenuWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        nf::video_controller().set_apply(nullptr);
        nf::video_controller().set_store(nullptr);
        g_log = nullptr;
        menu_pc_debug_power_on();
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        drop_font();
        GameState.paused = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void tick(std::initializer_list<int> keys = {}, int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            for (int k : keys) g_psi_keyboard_mouse.down[k] = true;
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    // Aperta e solta uma tecla.
    void press(int key, int times = 1) {
        for (int i = 0; i < times; ++i) {
            tick({key});
            tick();
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
    M_LISTBOX* list(uint32_t id, uint32_t tag = 0) { return reinterpret_cast<M_LISTBOX*>(control(id, tag)); }
    void open_pause() {
        MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
        GS_PauseGame(1);
        tick({}, 3);
    }
    // A pausa e, pela linha nova (OPTIONS), a página de opções.
    void open_options() {
        open_pause();
        press(VK_DOWN, 3);
        press(VK_SPACE);
    }
    // O foco num controle pelo id (como a mensagem 0x22 do jogo: o primeiro habilitado com esse id).
    void focus(uint32_t id) { Manager_SendMessage(&m(), 0x22, int32_t(id), 0); }
    // O foco numa linha da aba pelas setas (as que rolaram para fora não aceitam o foco direto): para cima até a faixa
    // das abas, depois para baixo até ela.
    void go(uint32_t id) {
        for (int i = 0; i < 16 && !(focused() >= kPcTabFirst && focused() < kPcTabFirst + uint32_t(kPcTabCount)); ++i) {
            press(VK_UP);
        }
        for (int i = 0; i < 16 && focused() != id; ++i) press(VK_DOWN);
    }
    uint32_t focused() {
        uint32_t id = 0;
        Menu_Focus(0, &id, nullptr);
        return id;
    }
    // O controle de um tipo (o rótulo do nome de uma linha tem o id do controle dela).
    M_CONTROL* of_type(uint32_t id, uint8_t type, uint32_t tag = 0) {
        M_PAGE* p = m().page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr;
             c = c->next) {
            if (c->id == id && c->type == type && c->tag == tag) return c;
        }
        return nullptr;
    }
    // O mouse num ponto da tela do PS2 (numa janela de 640 x 480: a área 4:3 inteira).
    void mouse(float x, float y) {
        const Psi2DViewport vp = psi_2d_viewport(640, 480);
        g_psi_keyboard_mouse.view_width = 640;
        g_psi_keyboard_mouse.view_height = 480;
        g_psi_keyboard_mouse.cursor_inside = true;
        g_psi_keyboard_mouse.cursor_x = vp.left + x * vp.scale_x;
        g_psi_keyboard_mouse.cursor_y = vp.top + y * vp.scale_y;
    }
    // No controle, a uma fração da largura (a página fica em 0, 0).
    void mouse_on(const M_CONTROL* c, float fx = 0.5f) {
        mouse(float(c->x) + float(c->w) * fx, float(c->y) + float(c->h) * 0.5f);
    }
    void click(int button = VK_LBUTTON) {
        tick({button});
        tick();
    }
    void wheel(float notches) {
        g_psi_keyboard_mouse.wheel = notches;
        tick();
    }
};

float cx(const M_CONTROL* c) { return float(c->x) + float(c->w) * 0.5f; }
float cy(const M_CONTROL* c) { return float(c->y) + float(c->h) * 0.5f; }

// O texto da opção escolhida de um rádio e o do nome de uma linha.
const char* radio_text(M_CONTROL* c) {
    return c != nullptr ? nf::p32_ptr<char>(uint32_t(Radio_SendMessage(reinterpret_cast<M_RADIO*>(c), 0x3b, 0, 0))) : "";
}
const char* label_text(M_CONTROL* c) { return c != nullptr ? reinterpret_cast<M_LABEL*>(c)->string : ""; }

int32_t selected(M_LISTBOX* l) { return l != nullptr ? List_SendMessage(l, 0x34, 0, 0) : -1; }

const char* cell(M_LISTBOX* l, int32_t row, int32_t col) {
    const LIST_ROW* r = List_SetIndex(l, &l->rows, row);
    return r != nullptr ? r->cells[col].text : "";
}


}  // namespace

// ---- nightfire.ini ----

TEST(pc_video_settings_ini_round_trip) {
    nf::VideoSettings v;
    v.width = 2560;
    v.height = 1080;
    v.window_mode = nf::WindowMode::Exclusive;
    v.vsync = false;
    v.render_scale = 150;
    v.msaa = 4;
    v.filter = nf::TextureFilter::Trilinear;
    v.fov = 75.0f;
    v.hd_textures = false;
    v.hd_ui = true;
    v.hud_edges = true;
    v.language = "en";
    nf::Settings s;
    s.parse("; minhas opções\n[video]\nfullscreen = 1\n[controls]\nmouse_sensitivity = 0.003\n");
    nf::store_video_settings(v, s);
    // A chave antiga sai (window_mode a substitui); as outras seções ficam.
    CHECK(!s.has("video", "fullscreen") && s.get("video", "window_mode") == "exclusive");
    CHECK(s.get("video", "texture_filter") == "trilinear" && s.get("ui", "anchor") == "edges");
    CHECK(s.get("ui", "language_pc") == "en" && s.get_float("controls", "mouse_sensitivity", 0) == 0.003f);
    // As chaves novas entram na ordem em que foram gravadas.
    const std::string text = s.text();
    CHECK(text.find("width = 2560") < text.find("height = 1080") && text.find("height") < text.find("window_mode"));
    CHECK(text.find("[ui]\r\nhd = 1\r\nanchor = edges\r\nlanguage_pc = en") != std::string::npos);
    // Lido de novo (por um texto, como o arquivo), dá a mesma configuração.
    nf::Settings t;
    t.parse(s.text());
    nf::VideoSettings back;
    nf::load_video_settings(t, back);
    CHECK(back == v);
    // Cada filtro e cada modo têm nome e voltam pelo nome.
    for (int f = 0; f < 6; ++f) {
        nf::TextureFilter out = nf::TextureFilter::Bilinear;
        CHECK(nf::parse_texture_filter(nf::texture_filter_name(nf::TextureFilter(f)), &out) && int(out) == f);
    }
    for (int m = 0; m < 3; ++m) {
        nf::WindowMode out = nf::WindowMode::Windowed;
        CHECK(nf::parse_window_mode(nf::window_mode_name(nf::WindowMode(m)), &out) && int(out) == m);
    }
    CHECK(nf::texture_filter_anisotropy(nf::TextureFilter::Aniso16) == 16);
    CHECK(nf::texture_filter_anisotropy(nf::TextureFilter::Trilinear) == 0);
}

TEST(pc_video_settings_old_keys_and_bad_values) {
    // O arquivo de antes (fullscreen = 1) vira a janela sem bordas; valores fora da faixa são presos.
    nf::Settings s;
    s.parse("[video]\nfullscreen = 1\nmsaa = 3\nrender_scale = 900\ntexture_filter = xyz\n[ui]\nlanguage_pc = fr\n");
    nf::VideoSettings v;
    nf::load_video_settings(s, v);
    CHECK(v.window_mode == nf::WindowMode::Borderless);
    CHECK(v.msaa == 2 && v.render_scale == 400);
    CHECK(v.filter == nf::TextureFilter::Aniso8 && v.language == "en");  // inválidos: o padrão
    // window_mode vale por cima de fullscreen.
    s.set("video", "window_mode", "windowed");
    nf::load_video_settings(s, v);
    CHECK(v.window_mode == nf::WindowMode::Windowed);
}

// ---- listas ----

TEST(pc_video_mode_list) {
    using M = nf::DisplayMode;
    // Ordem crescente, sem repetidas (o DXGI repete por taxa de atualização), sem as menores que 640 x 480,
    // mais a atual.
    const std::vector<M> list = nf::build_mode_list({{1920, 1080}, {640, 400}, {1280, 720}, {1920, 1080}, {800, 600}}, {1366, 768});
    const std::vector<M> want{{800, 600}, {1280, 720}, {1366, 768}, {1920, 1080}};
    CHECK(list == want);
    // Na janela, só as que cabem no monitor; na exclusiva, todas; sem bordas, a do monitor.
    const std::vector<M> raw{{1280, 720}, {1920, 1080}, {3840, 2160}};
    CHECK(nf::resolution_choices(raw, {1920, 1080}, nf::WindowMode::Windowed, {1280, 720}).size() == 2);
    CHECK(nf::resolution_choices(raw, {1920, 1080}, nf::WindowMode::Exclusive, {1280, 720}).size() == 3);
    const std::vector<M> desk = nf::resolution_choices(raw, {1920, 1080}, nf::WindowMode::Borderless, {1280, 720});
    CHECK((desk.size() == 1 && desk[0] == M{1920, 1080}));
    // Percorre dando a volta; fora da lista, vai ao maior seguinte (ou ao menor anterior).
    CHECK((nf::cycle_mode(raw, {3840, 2160}, 1) == M{1280, 720}));
    CHECK((nf::cycle_mode(raw, {1280, 720}, -1) == M{3840, 2160}));
    CHECK((nf::cycle_mode(raw, {1600, 900}, 1) == M{1920, 1080}));
    CHECK((nf::cycle_mode(raw, {1600, 900}, -1) == M{1280, 720}));
}

TEST(pc_video_value_lists_cycle) {
    const std::vector<int> scales = nf::render_scale_values(100);
    CHECK(scales.front() == 50 && scales.back() == 200);
    CHECK(nf::cycle_value(scales, 200, 1) == 50 && nf::cycle_value(scales, 50, -1) == 200);
    CHECK(nf::cycle_value(scales, 100, 1) == 125);
    // Um valor do ini fora dos passos entra na lista, na ordem.
    const std::vector<int> odd = nf::render_scale_values(110);
    CHECK(nf::cycle_value(odd, 110, 1) == 125 && nf::cycle_value(odd, 110, -1) == 100);
    // MSAA: desligado e só as amostras que o renderizador aceita.
    CHECK((nf::msaa_values({2, 4}, 1) == std::vector<int>{1, 2, 4}));
    CHECK((nf::msaa_values({}, 1) == std::vector<int>{1}));
    // Campo de visão de 40 a 100.
    const std::vector<int> fov = nf::fov_values(60);
    CHECK(fov.front() == 40 && fov.back() == 100 && nf::cycle_value(fov, 60, 1) == 65);
    CHECK(nf::fov_values(63).size() == fov.size() + 1);
}

// ---- controlador ----

TEST(pc_video_controller_confirms_display_changes) {
    std::vector<nf::VideoSettings> applied, stored;
    nf::VideoController c;
    nf::VideoSettings v;
    v.width = 1280;
    v.height = 720;
    c.init(v);
    c.set_apply([&](const nf::VideoSettings& next, const nf::VideoSettings&) { applied.push_back(next); });
    c.set_store([&](const nf::VideoSettings& s) {
        stored.push_back(s);
        return true;
    });
    // Sem mudança de tela: aplica e grava na hora.
    nf::VideoSettings fov = v;
    fov.fov = 80.0f;
    CHECK(!c.apply(fov) && !c.confirming());
    CHECK(applied.size() == 1 && stored.size() == 1 && stored[0].fov == 80.0f);
    // Resolução nova: aplica, mas só grava com keep(); sem resposta em kVideoConfirmSeconds, volta inteira.
    nf::VideoSettings big = fov;
    big.width = 1920;
    big.height = 1080;
    big.vsync = false;
    CHECK(c.apply(big) && c.confirming() && c.remaining() == nf::kVideoConfirmSeconds);
    CHECK(applied.size() == 2 && stored.size() == 1 && c.live().width == 1920);
    CHECK(!c.tick(nf::kVideoConfirmSeconds - 0.5f) && c.confirming());
    CHECK(c.tick(1.0f) && !c.confirming());
    CHECK(applied.size() == 3 && applied.back() == fov && c.live() == fov && stored.size() == 1);
    CHECK(c.stats().reverted == 1);
    // Confirmada: grava a nova.
    CHECK(c.apply(big));
    c.keep();
    CHECK(!c.confirming() && stored.size() == 2 && stored.back() == big && c.stats().kept == 1);
    // Modo de tela também pede confirmação; revert() volta já.
    nf::VideoSettings borderless = big;
    borderless.window_mode = nf::WindowMode::Borderless;
    CHECK(nf::video_needs_confirmation(big, borderless) && !nf::video_needs_confirmation(big, big));
    CHECK(c.apply(borderless));
    c.revert();
    CHECK(c.live() == big && !c.confirming() && stored.size() == 2);
    // tick sem confirmação não faz nada.
    CHECK(!c.tick(100.0f));
}

// ---- textos ----

namespace {
bool no_cedilla_or_tilde(uint8_t c) {
    // Como as fontes do jogo: sem Ã, Ç, Õ, ã, õ (0xc3, 0xc7, 0xd5, 0xe3, 0xf5); com o resto.
    return c != 0xc3 && c != 0xc7 && c != 0xd5 && c != 0xe3 && c != 0xf5 && c != 0x7f;
}
bool every_glyph(uint8_t) { return true; }
}  // namespace

TEST(pc_text_table_and_font_fallback) {
    // Sem o glifo, a letra sem acento; com ele, o byte do Windows-1252.
    CHECK(PcText_ToGame("OPÇÕES", no_cedilla_or_tilde) == "OPCOES");
    CHECK(PcText_ToGame("Resolução", no_cedilla_or_tilde) == "Resolu\xe7" "ao");  // ç existe, ã não
    CHECK(PcText_ToGame("Vídeo", no_cedilla_or_tilde) == "V\xed" "deo");
    CHECK(PcText_ToGame("OPÇÕES", every_glyph) == "OP\xc7\xd5" "ES");
    CHECK(PcText_ToGame("a\nb … ’", no_cedilla_or_tilde) == "a\nb \x85 \x92");
    CHECK(PcText_ToGame("日本", every_glyph) == "??");  // fora do Windows-1252
    // A tabela: um texto por id nos dois idiomas; o ponteiro de PcText_Get é fixo e segue o idioma.
    for (int i = 0; i < int(PcText::Count); ++i) {
        CHECK(std::strlen(PcText_Utf8(PcText(i), "pt")) > 0 && std::strlen(PcText_Utf8(PcText(i), "en")) > 0);
    }
    hashtable_debug_power_on();
    hashtable_initialise();
    make_font();  // a fonte 2 só com ASCII
    PcText_SetHdText(false);
    PcText_SetLanguage("pt");
    const uint32_t gen = PcText_Generation();
    const char* p = PcText_Get(PcText::Apply);
    CHECK(std::strcmp(p, "Aplicar mudancas") == 0);
    PcText_SetLanguage("en");
    CHECK(PcText_Generation() != gen && PcText_Get(PcText::Apply) == p && std::strcmp(p, "Apply changes") == 0);
    PcText_SetLanguage("xx");  // desconhecido: português
    CHECK(PcText_Language() == "pt");
    // Pela fonte 2: só os caracteres que ela tem; com a camada HD, os acentos ficam.
    CHECK(std::strcmp(PcText_Get(PcText::PauseEntry), "O P C O E S") == 0);
    PcText_SetHdText(true);
    CHECK(std::strcmp(PcText_Get(PcText::PauseEntry), "O P \xc7 \xd5 E S") == 0);
    PcText_SetHdText(false);
    // Com um glifo para o Í (0xcd) na fonte, ele fica.
    g_chars[2].code = 0xcd;
    CHECK(std::strcmp(PcText_Get(PcText::TabVideo), "V\xcd" "DEO") == 0);
    drop_font();
    hashtable_debug_power_on();
}

// ---- a página de opções (abas) ----

TEST(pc_menu_pause_row_opens_the_options_tabs) {
    PcMenuWorld w;
    w.open_pause();
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    M_LISTBOX* pause = w.list(0x10000028, 6);
    CHECK(pause != nullptr && pause->rows.count == 4);
    CHECK(List_SetIndex(pause, &pause->rows, 3)->data == kPcPauseRow);
    CHECK(std::strcmp(cell(pause, 3, 0), "O P C O E S") == 0);  // sem as fontes: a letra sem acento
    CHECK(g_menu_pc_stats.pause_rows == 1);
    // Para baixo até a linha nova; escolher abre a página de opções (montada agora).
    w.press(VK_DOWN, 3);
    CHECK(selected(pause) == 3);
    w.press(VK_SPACE);
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage && g_menu_pc_stats.built == 1);
    // A janela primeiro; o painel da pausa no tamanho do arquivo (550 x 266 -> 440 x 248 na tela do PS2).
    CHECK(static_cast<M_CONTROL*>(ll_first(w.m().page->controls))->id == kPcWindow);
    M_CONTROL* panel = w.control(kPcPanel);
    CHECK(panel != nullptr && panel->state == 2 && panel->w == 440 && panel->h == 248);
    // As abas deste módulo (as de controles e teclas não foram registradas aqui): da esquerda para a
    // direita, dentro do painel.
    M_CONTROL* prev = nullptr;
    for (uint32_t t = 0; t < 4; ++t) {
        M_CONTROL* b = w.control(kPcTabFirst + t);
        CHECK(b != nullptr && b->type == MCT_BUTTON && b->state == 0);
        CHECK(panel->x < b->x && b->x + b->w < panel->x + panel->w);
        if (prev != nullptr) CHECK(prev->x + prev->w <= b->x);
        prev = b;
    }
    CHECK(((w.control(kPcTabFirst + 4)->state & 1) != 0) && ((w.control(kPcTabFirst + 5)->state & 1) != 0));
    // O foco na aba VÍDEO; as linhas dela aparecem, as das outras não.
    CHECK(w.focused() == kPcTabFirst && MenuPc_CurrentTab() == PcTab::Video);
    CHECK(MenuPc_Control(0, kPcRowResolution)->state == 0 && ((MenuPc_Control(0, kPcRowRenderScale)->state & 1) != 0));
    CHECK(MenuPc_Control(0, kPcRowResolution)->type == MCT_RADIO);
    // A barra de volume: proporcional, no conjunto de componentes da página.
    M_SCROLL* fov = reinterpret_cast<M_SCROLL*>(MenuPc_Control(0, kPcRowFov));
    CHECK(fov->base.type == MCT_SCROLL && fov->proportional == 1 && (fov->base.component >> 16) == kPcComponentSet);
    // O nome da linha: um rótulo desabilitado com o id do controle, à esquerda dele, na mesma altura.
    M_CONTROL* name = w.of_type(kPcRowResolution, MCT_LABEL);
    CHECK(name != nullptr && name->state == 2 && name->x < MenuPc_Control(0, kPcRowResolution)->x);
    CHECK(std::abs(cy(name) - cy(MenuPc_Control(0, kPcRowResolution))) <= 1.0f);
    // Direita: GRÁFICOS na hora, com o foco na aba.
    w.press(VK_RIGHT);
    CHECK(MenuPc_CurrentTab() == PcTab::Graphics && w.focused() == kPcTabFirst + 1);
    CHECK(((MenuPc_Control(0, kPcRowResolution)->state & 1) != 0) && MenuPc_Control(0, kPcRowRenderScale)->state == 0);
    CHECK(MenuPc_Control(0, kPcRowDlss)->state == 2 && std::strcmp(radio_text(MenuPc_Control(0, kPcRowDlss)), "Em breve") == 0);
    // Baixo: a primeira linha; até as texturas HD; DLSS (apagada) e Aplicar (nada a aplicar) são pulados.
    w.press(VK_DOWN);
    CHECK(w.focused() == kPcRowRenderScale);
    w.press(VK_DOWN, 3);
    CHECK(w.focused() == kPcRowHdTextures);
    w.press(VK_DOWN);
    CHECK(w.focused() == kPcRowHdTextures);
    // De cima da primeira linha, volta para a aba.
    w.press(VK_UP, 4);
    CHECK(w.focused() == kPcTabFirst + 1);
    // Esquerda duas vezes: VÍDEO e, dando a volta, ÁUDIO (as barras do jogo, apagadas).
    w.press(VK_LEFT, 2);
    CHECK(MenuPc_CurrentTab() == PcTab::Audio && MenuPc_Control(0, kPcRowMusic)->state == 2);
    CHECK(MenuPc_Slider(0, kPcRowMusic) == 14 && MenuPc_Slider(0, kPcRowEffects) == 20);
    CHECK(g_menu_pc_stats.tab_changes == 3);
    // Voltar volta para a pausa como ela estava (o 0x4c do jogo não roda de novo: as linhas não repetem).
    w.press(VK_BACK);
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    pause = w.list(0x10000028, 6);
    CHECK(pause->rows.count == 4 && g_menu_pc_stats.pause_rows == 1);
    // De novo: a página já existe (não monta outra) e abre na última aba.
    w.press(VK_SPACE);
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage && g_menu_pc_stats.built == 1 && MenuPc_CurrentTab() == PcTab::Audio);
    // Start fecha o menu inteiro.
    w.tick({VK_RETURN});
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0);
}

TEST(pc_menu_video_tab_edits_and_applies) {
    PcMenuWorld w;
    w.open_options();
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage && MenuPc_CurrentTab() == PcTab::Video);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowResolution)), "1280 x 720") == 0);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowVSync)), "Ligado") == 0);
    // Até o campo de visão (a barra: 60 graus = posição 4 de 0 a 12); direita muda a cópia, não aplica.
    w.press(VK_DOWN, 4);
    CHECK(w.focused() == kPcRowFov && MenuPc_Slider(0, kPcRowFov) == 4);
    CHECK(std::strcmp(label_text(w.of_type(kPcRowFov, MCT_LABEL)), "Campo de visao  60") == 0);
    w.press(VK_RIGHT);
    CHECK(MenuPc_Slider(0, kPcRowFov) == 5 && w.log.next.empty());
    CHECK(std::strcmp(label_text(w.of_type(kPcRowFov, MCT_LABEL)), "Campo de visao  65") == 0);
    // Aplicar (agora acesa, depois do limite de quadros, da interpolação, do mouse sem atraso e do adaptador: a lista
    // rola duas linhas): o gancho recebe a configuração; sem mudança de tela, grava na hora.
    w.press(VK_DOWN, 4);
    CHECK(w.focused() == kPcRowAdapter);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowAdapter)),
                      PcText_ToGame(PcText_Utf8(PcText::AdapterAuto)).c_str()) == 0);
    w.press(VK_DOWN);
    CHECK(w.focused() == kPcRowApplyVideo);
    w.press(VK_SPACE);
    CHECK(w.log.next.size() == 1 && w.log.next[0].fov == 65.0f && w.log.stored.size() == 1);
    CHECK(nf::video_controller().live().fov == 65.0f && !nf::video_controller().confirming());
    // Escolher num rádio avança a opção: a sincronia vertical desliga (na cópia).
    w.press(VK_UP, 6);
    CHECK(w.focused() == kPcRowVSync);
    w.press(VK_SPACE);
    CHECK(MenuPc_Choice(0, kPcRowVSync) == 0 && nf::video_controller().live().vsync);
    // GRÁFICOS edita a mesma cópia: a escala vai a 125%.
    w.press(VK_UP, 3);
    CHECK(w.focused() == kPcTabFirst);
    w.press(VK_RIGHT);
    w.press(VK_DOWN);
    CHECK(w.focused() == kPcRowRenderScale);
    w.press(VK_RIGHT);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowRenderScale)), "125%") == 0);
    // Voltar descarta o que não foi aplicado.
    w.press(VK_BACK);
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    CHECK(nf::video_controller().live().render_scale == 100 && nf::video_controller().live().vsync);
    w.press(VK_SPACE);
    CHECK(MenuPc_CurrentTab() == PcTab::Graphics);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowRenderScale)), "100%") == 0);
    CHECK(w.log.next.size() == 1);
}

TEST(pc_menu_interface_subtitles_row_is_the_game_option) {
    PcMenuWorld w;
    DrawInfo[2] = 0;
    w.open_options();
    // INTERFACE: a faixa das abas, duas à direita; as linhas HD, a posição do HUD e as legendas.
    w.press(VK_RIGHT, 2);
    CHECK(MenuPc_CurrentTab() == PcTab::Interface);
    w.press(VK_DOWN, 3);
    CHECK(w.focused() == kPcRowSubtitles && MenuPc_Choice(0, kPcRowSubtitles) == 0);
    CHECK(std::strcmp(label_text(w.of_type(kPcRowSubtitles, MCT_LABEL)), "Legendas") == 0);
    // Escolher liga as legendas do jogo na hora (sem o Apply: nada vai ao controlador de vídeo).
    w.press(VK_SPACE);
    CHECK(DrawInfo[2] == 1 && nf::game_option(nf::GameOption::Subtitles) == 1 && w.log.next.empty());
    w.press(VK_LEFT);
    CHECK(DrawInfo[2] == 0 && MenuPc_Choice(0, kPcRowSubtitles) == 0);
    // O valor vem do jogo ao entrar na aba.
    DrawInfo[2] = 1;
    w.press(VK_UP, 3);
    w.press(VK_LEFT);
    w.press(VK_RIGHT);
    CHECK(MenuPc_CurrentTab() == PcTab::Interface && MenuPc_Choice(0, kPcRowSubtitles) == 1);
    DrawInfo[2] = 0;
}

TEST(pc_menu_video_resolution_confirmation) {
    PcMenuWorld w;
    w.open_options();
    // A resolução seguinte (os modos do "monitor" de 2560 x 1440) e Aplicar: a caixa de confirmação do jogo
    // aparece por cima e as linhas somem por baixo.
    w.press(VK_DOWN);
    w.press(VK_RIGHT);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowResolution)), "1920 x 1080") == 0);
    w.go(kPcRowApplyVideo);
    w.press(VK_SPACE);
    CHECK(Menu_CurrentPage(0) == 0x4000002f && nf::video_controller().confirming());
    CHECK(w.log.next.size() == 1 && w.log.next[0].width == 1920 && w.log.stored.empty());
    CHECK(g_menu_pc_stats.confirms == 1);
    M_LISTBOX* answers = w.list(0x10000120);
    CHECK(answers != nullptr && std::strcmp(cell(answers, 0, 0), "YES") == 0 && std::strcmp(cell(answers, 1, 0), "NO") == 0);
    // Sem resposta, o tempo acaba (60 ticks por segundo) e a anterior volta: a caixa fecha.
    w.tick({}, int(nf::kVideoConfirmSeconds * 60.0f) - 30);
    CHECK(nf::video_controller().confirming() && Menu_CurrentPage(0) == 0x4000002f);
    w.tick({}, 40);
    CHECK(!nf::video_controller().confirming() && Menu_CurrentPage(0) == kPcOptionsPage);
    CHECK(w.log.next.size() == 2 && w.log.next[1].width == 1280 && w.log.stored.empty());
    // As linhas voltam (a lista rolou uma linha para mostrar Aplicar, a oitava: a da resolução fica acima).
    CHECK(MenuPc_Control(0, kPcRowVSync)->state == 0);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowResolution)), "1280 x 720") == 0);
    // De novo, e SIM: fica e grava.
    w.go(kPcRowResolution);
    w.press(VK_RIGHT);
    w.go(kPcRowApplyVideo);
    w.press(VK_SPACE);
    CHECK(Menu_CurrentPage(0) == 0x4000002f);
    w.press(VK_SPACE);  // a primeira resposta (SIM)
    w.tick({}, 2);
    CHECK(!nf::video_controller().confirming() && nf::video_controller().live().width == 1920);
    CHECK(w.log.stored.size() == 1 && w.log.stored[0].width == 1920 && Menu_CurrentPage(0) == kPcOptionsPage);
    // Mudar o modo de tela também pede confirmação; voltar na caixa é "não".
    w.go(kPcRowWindowMode);
    w.press(VK_RIGHT);
    CHECK(std::strcmp(radio_text(MenuPc_Control(0, kPcRowResolution)), "2560x1440 (desktop)") == 0);
    w.go(kPcRowApplyVideo);
    w.press(VK_SPACE);
    CHECK(Menu_CurrentPage(0) == 0x4000002f && nf::video_controller().live().window_mode == nf::WindowMode::Borderless);
    w.press(VK_BACK);
    w.tick({}, 2);
    CHECK(!nf::video_controller().confirming() && nf::video_controller().live().window_mode == nf::WindowMode::Windowed);
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage && w.log.stored.size() == 1);
    // Com a confirmação aberta, fechar o menu (Start) também volta à anterior (MenuPc_Frame).
    w.go(kPcRowResolution);
    w.press(VK_RIGHT);
    w.go(kPcRowApplyVideo);
    w.press(VK_SPACE);
    CHECK(nf::video_controller().confirming());
    w.tick({VK_RETURN});
    MenuPc_Frame(false);
    CHECK(MenuManager_Exists(0) == 0 && !nf::video_controller().confirming());
    CHECK(nf::video_controller().live().width == 1920);
}

// ---- o mouse ----

TEST(menu_mouse_hit_test_and_slider_value) {
    PcMenuWorld w;
    w.open_options();
    M_MANAGER* m = &w.m();
    // O controle sob o ponto; no nome da linha (um rótulo desabilitado), o controle dela.
    M_CONTROL* mode = MenuPc_Control(0, kPcRowWindowMode);
    bool via = true;
    CHECK(Menu_MouseHitTest(m, cx(mode), cy(mode), &via) == mode && !via);
    M_CONTROL* name = w.of_type(kPcRowWindowMode, MCT_LABEL);
    CHECK(Menu_MouseHitTest(m, cx(name), cy(name), &via) == mode && via);
    // As abas; o painel (desabilitado) e o canto da tela não são nada.
    M_CONTROL* tab = w.control(kPcTabFirst + 2);
    CHECK(Menu_MouseHitTest(m, cx(tab), cy(tab), &via) == tab && !via);
    CHECK(Menu_MouseHitTest(m, 5.0f, 5.0f) == nullptr);
    M_CONTROL* panel = w.control(kPcPanel);
    CHECK(Menu_MouseHitTest(m, float(panel->x) + 4.0f, float(panel->y + panel->h) - 4.0f) == nullptr);
    // Linhas escondidas (outra aba) e apagadas não são alvo.
    M_CONTROL* scale = MenuPc_Control(0, kPcRowRenderScale);
    CHECK(Menu_MouseHitTest(m, cx(scale), cy(scale)) != scale);
    // A barra de volume: o trilho entre as setas vai do mínimo ao máximo (o preenchimento até o mouse).
    M_SCROLL* s = reinterpret_cast<M_SCROLL*>(MenuPc_Control(0, kPcRowFov));
    const float y = cy(&s->base);
    const float x0 = float(s->base.x + s->margin_a);
    const float x1 = float(s->base.x + s->base.w - s->margin_b);
    CHECK(x0 < x1 && s->min == 0 && s->max == 12);
    CHECK(Menu_MouseScrollValue(s, x0 + 0.1f, y) == 0);
    CHECK(Menu_MouseScrollValue(s, x1 - 0.1f, y) == 12);
    CHECK(Menu_MouseScrollValue(s, (x0 + x1) * 0.5f, y) == 6);
    CHECK(Menu_MouseScrollValue(s, x0 - 50.0f, y) == 0 && Menu_MouseScrollValue(s, x1 + 50.0f, y) == 12);
    // As setas nas pontas, o trilho no meio.
    CHECK(Menu_MouseScrollPart(s, float(s->base.x) + 1.0f, y) == -1);
    CHECK(Menu_MouseScrollPart(s, float(s->base.x + s->base.w) - 1.0f, y) == 1);
    CHECK(Menu_MouseScrollPart(s, (x0 + x1) * 0.5f, y) == 0);
    // A linha da lista sob a altura: a da pausa.
    w.press(VK_BACK);
    M_LISTBOX* l = w.list(0x10000028, 6);
    const float top = float(l->base.y);
    CHECK(Menu_MouseListRow(l, top + l->row_height * 0.5f) == 0);
    CHECK(Menu_MouseListRow(l, top + l->row_height * 2.5f) == 2);
    CHECK(Menu_MouseListRow(l, top - 1.0f) == -1);
    CHECK(Menu_MouseListRow(l, top + l->row_height * 7.5f) == -1);
}

TEST(menu_mouse_drives_the_options_page) {
    PcMenuWorld w;
    w.open_options();
    // Passar por cima de uma linha dá o foco a ela; pelo nome também.
    M_CONTROL* mode = MenuPc_Control(0, kPcRowWindowMode);
    w.mouse_on(mode);
    w.tick();
    CHECK(w.focused() == kPcRowWindowMode && psiInput_MenuMouse().active);
    w.mouse_on(w.of_type(kPcRowVSync, MCT_LABEL));
    w.tick();
    CHECK(w.focused() == kPcRowVSync);
    // Clicar na seta da direita do rádio avança (na cópia); na da esquerda, volta.
    w.mouse_on(mode, 0.97f);
    w.click();
    CHECK(MenuPc_Choice(0, kPcRowWindowMode) == 1 && w.log.next.empty() && w.focused() == kPcRowWindowMode);
    w.mouse_on(mode, 0.02f);
    w.click();
    CHECK(MenuPc_Choice(0, kPcRowWindowMode) == 0);
    // A roda sobre o rádio: para cima avança, para baixo volta.
    w.mouse_on(mode, 0.5f);
    w.wheel(1.0f);
    CHECK(MenuPc_Choice(0, kPcRowWindowMode) == 1);
    w.wheel(-1.0f);
    CHECK(MenuPc_Choice(0, kPcRowWindowMode) == 0);
    // A barra: clicar no fim do trilho põe o máximo; arrastar até o começo, o mínimo; solto, mexer não muda.
    M_SCROLL* s = reinterpret_cast<M_SCROLL*>(MenuPc_Control(0, kPcRowFov));
    const float y = cy(&s->base);
    const float x0 = float(s->base.x + s->margin_a);
    const float x1 = float(s->base.x + s->base.w - s->margin_b);
    w.mouse(x1 - 0.5f, y);
    w.tick({VK_LBUTTON});
    CHECK(MenuPc_Slider(0, kPcRowFov) == 12 && w.focused() == kPcRowFov);
    CHECK(std::strcmp(label_text(w.of_type(kPcRowFov, MCT_LABEL)), "Campo de visao  100") == 0);
    w.mouse((x0 + x1) * 0.5f, y);
    w.tick({VK_LBUTTON});
    CHECK(MenuPc_Slider(0, kPcRowFov) == 6);
    w.mouse(x0 + 0.5f, y);
    w.tick({VK_LBUTTON});
    CHECK(MenuPc_Slider(0, kPcRowFov) == 0);
    w.tick();
    w.mouse(x1 - 0.5f, y);
    w.tick();
    CHECK(MenuPc_Slider(0, kPcRowFov) == 0);
    // As setas da barra andam um passo; a roda também.
    w.mouse(float(s->base.x) + 2.0f, y);
    w.click();
    CHECK(MenuPc_Slider(0, kPcRowFov) == 0);
    w.mouse(float(s->base.x + s->base.w) - 2.0f, y);
    w.click();
    CHECK(MenuPc_Slider(0, kPcRowFov) == 1);
    w.wheel(2.0f);
    CHECK(MenuPc_Slider(0, kPcRowFov) == 3 && w.log.next.empty());
    // Clicar numa aba a abre (o foco vai junto); passar por cima de outra só dá o foco.
    w.mouse_on(w.control(kPcTabFirst + 1));
    w.click();
    CHECK(MenuPc_CurrentTab() == PcTab::Graphics && w.focused() == kPcTabFirst + 1);
    w.mouse_on(w.control(kPcTabFirst + 2));
    w.tick();
    CHECK(MenuPc_CurrentTab() == PcTab::Graphics && w.focused() == kPcTabFirst + 2);
    // Uma tecla tira o mouse do comando: parado, ele não pega o foco de volta.
    w.press(VK_LEFT);
    CHECK(!psiInput_MenuMouse().active && w.focused() == kPcTabFirst + 1);
    w.tick({}, 3);
    CHECK(w.focused() == kPcTabFirst + 1);
    // Clicar em Aplicar aplica a cópia (o campo de visão de 55 graus).
    w.mouse_on(w.control(kPcTabFirst));
    w.click();
    CHECK(MenuPc_CurrentTab() == PcTab::Video);
    w.wheel(-2.0f);  // a linha de Aplicar é a nona: a roda rola as linhas (sobre a aba, a roda é da página)
    w.mouse_on(MenuPc_Control(0, kPcRowApplyVideo));
    w.click();
    CHECK(w.log.next.size() == 1 && w.log.next[0].fov == 55.0f && w.log.stored.size() == 1);
    // O botão direito volta (como o triângulo).
    w.click(VK_RBUTTON);
    CHECK(Menu_CurrentPage(0) == 0x4000004b && g_menu_mouse_stats.backs == 1);
}

TEST(menu_mouse_on_the_pause_list) {
    PcMenuWorld w;
    w.open_pause();
    M_LISTBOX* l = w.list(0x10000028, 6);
    CHECK(l != nullptr && w.m().page->focus == &l->base);
    const float x = cx(&l->base);
    const auto row_y = [&](int i) { return float(l->base.y) + (float(i) + 0.5f) * l->row_height; };
    // Passar por cima escolhe a linha (como cima/baixo).
    w.mouse(x, row_y(1));
    w.tick();
    CHECK(selected(l) == 1);
    // A roda anda uma linha por dente (para baixo, a seguinte) e para nas pontas.
    w.wheel(-1.0f);
    CHECK(selected(l) == 2);
    w.wheel(3.0f);
    CHECK(selected(l) == 0);
    // Clicar numa linha a escolhe de vez: OPTIONS abre a página de opções.
    w.mouse(x, row_y(3));
    w.click();
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage);
    CHECK(g_menu_mouse_stats.selects == 1 && g_menu_mouse_stats.rows == 2 && g_menu_mouse_stats.steps == 3);
    // Mouse4 volta como o botão direito.
    w.click(VK_XBUTTON1);
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    // Clicar em CONTINUE fecha o menu.
    l = w.list(0x10000028, 6);
    w.mouse(x, row_y(0) + 0.25f);
    w.click();
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0);
}

