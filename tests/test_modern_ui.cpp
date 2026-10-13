// Testes do estilo de menu Modern (src/ui, só do port): os tokens e o tokens.json do repositório, a tela
// virtual, as curvas de movimento, os componentes (a barra de cima, as categorias, as linhas) e a tela de
// opções (navegação, listas que abrem, chaves, barras, a confirmação de vídeo, o mouse e a tabela de teclas),
// sem GPU: a tela monta a lista de desenho e os testes olham os comandos dela.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>


#include "engine/input.h"
#include "platform/keys.h"
#include "engine/menu.h"
#include "engine/pc_text.h"
#include "engine/resetmap.h"
#include "platform/crt_compat.h"
#include "platform/game_options.h"
#include "platform/input_bindings.h"
#include "platform/psi_input.h"
#include "platform/psi_sound.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "test_framework.h"
#include "ui/modern_kit.h"
#include "ui/modern_options.h"
#include "platform/fmath.h"

using namespace nf::ui;

namespace {

std::string read_repo_file(const char* rel) {
    std::string path = std::string(NF_SOURCE_DIR) + "/" + rel;
    FILE* f = nf::fopen_narrow(path.c_str(), "rb");
    if (f == nullptr) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// O controlador de vídeo com ganchos que só registram (como os testes da página Original).
struct VideoFixture {
    int applied = 0;
    nf::VideoSettings last;
    VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        nf::VideoSettings live;
        live.width = 1600;
        live.height = 900;
        vc.init(live);
        vc.set_hardware({{1280, 720}, {1600, 900}, {1920, 1080}}, nf::DisplayMode{1920, 1080}, {2, 4, 8});
        vc.set_apply([this](const nf::VideoSettings& next, const nf::VideoSettings&) {
            ++applied;
            last = next;
        });
        vc.set_store([](const nf::VideoSettings&) { return true; });
        PcText_SetLanguage("en");
        nf::controls() = nf::ControlsConfig{};
        PlayerSetting[0].invert_look = 0;
        PlayerSetting[0].unknown_09 = 1;
        PlayerSetting[0].style = 0;
        psiInput_SetModal(false);
    }
    ~VideoFixture() {
        nf::VideoController& vc = nf::video_controller();
        if (vc.confirming()) vc.revert();
        vc.set_apply(nullptr);
        vc.set_store(nullptr);
        vc.set_adapters({}, "");
        vc.init(nf::VideoSettings{});
        nf::controls() = nf::ControlsConfig{};
    }
};

const Measure kMeasure = [](const std::wstring& s, const TextStyle& st) { return estimate_width(s, st); };

// Todos os textos de uma montagem da tela, separados por espaço (a descrição sai quebrada em linhas).
std::wstring all_text(const OptionsScreen& s, const Canvas& cv) {
    DrawList dl;
    s.build(dl, cv, kMeasure);
    std::wstring out;
    for (const DrawCmd& c : dl.cmds) {
        if (c.kind == DrawCmd::Text) out += c.text + L" ";
    }
    return out;
}

// As opções do jogo num estado conhecido (os bytes de PlayerSetting[0], as legendas, os volumes) e o .ini sem nada;
// no fim, tudo de volta e nada gravado.
struct GameOptionsFixture {
    nf::Settings saved_settings = nf::settings();
    GameOptionsFixture() {
        PlayerInput_tag& p = PlayerSetting[0];
        p.autoaim = 1;
        p.unknown_08 = 1;
        p.aim_toggle = 0;
        p.crouch_toggle = 1;
        p.auto_switch = 1;
        p.unknown_0c = 1;
        p.unknown_0b = 0;
        DrawInfo[2] = 0;
        psi_sound_debug_power_on();
        nf::settings() = nf::Settings{};
        nf::game_options_debug_power_on();
        nf::game_options_rebase();
    }
    ~GameOptionsFixture() {
        nf::g_controls_persist = false;
        nf::game_options_debug_power_on();
        nf::settings() = saved_settings;
        DrawInfo[2] = 0;
        psi_sound_debug_power_on();
        std::memset(def_codename, 0, sizeof(def_codename));
    }
};

// Uma tela aberta numa categoria, já com um quadro (o leiaute e os retângulos do mouse).
struct Screen {
    OptionsScreen s;
    Canvas cv = make_canvas(1920, 1080);
    explicit Screen(Cat c) {
        s.open(c);
        tick(0.0f);
        tick(0.5f);  // a animação de abrir termina
    }
    void tick(float dt, const UiInput& in = UiInput{}) { s.frame(dt, in, cv, kMeasure); }
    // Um clique (ou só o mouse parado em cima) num ponto da tela virtual.
    void click(float x, float y, bool press = true) {
        UiInput in;
        in.mouse_inside = true;
        in.mouse_x = window_x(cv, x);
        in.mouse_y = window_y(cv, y);
        tick(0.016f, in);
        if (!press) return;
        in.lmb = true;
        tick(0.016f, in);
        in.lmb = false;
        tick(0.016f, in);
    }
    int find(Opt o) const {
        for (size_t i = 0; i < s.items().size(); ++i) {
            if (s.items()[i].opt == o) return int(i);
        }
        return -1;
    }
    void focus(Opt o) {
        // Da linha com o foco até a opção, para baixo ou para cima (pelos comandos, como o teclado).
        const int to = find(o);
        for (int guard = 0; guard < 64 && s.focus() != to; ++guard) {
            s.command(s.focus() < to ? OptionsScreen::Cmd::Down : OptionsScreen::Cmd::Up);
        }
    }
};

}  // namespace

TEST(modern_tokens_defaults_and_repo_file) {
    const Tokens& t = default_tokens();
    // A grade e as colunas: categorias, linhas e descrição não se encostam; tudo dentro da tela virtual.
    CHECK(t.space.left_x < t.space.mid_x0 && t.space.mid_x1 < t.space.desc_x0 && t.space.desc_x1 <= kCanvasW);
    CHECK(t.space.list_top > t.space.top_rule_y && t.space.list_bottom < t.space.bottom_y);
    CHECK(t.space.mid_x1 < t.space.scroll_x && t.space.scroll_x < t.space.desc_x0);
    CHECK(std::fmod(t.space.row_h, t.space.grid) == 4.0f || std::fmod(t.space.row_h, t.space.grid) == 0.0f);
    CHECK(t.motion.focus > 0.1f && t.motion.focus < 0.25f && t.motion.open > 0.2f && t.motion.open <= 0.3f);
    // O hd_ui/design/tokens.json do repositório é válido e diz o mesmo que os padrões nas cores principais.
    const std::string json = read_repo_file("hd_ui/design/tokens.json");
    CHECK(!json.empty());
    Tokens r = default_tokens();
    std::string err;
    CHECK(parse_tokens(json, r, &err));
    CHECK(err.empty());
    CHECK(r.color.accent == t.color.accent && r.type.label.size == t.type.label.size && r.space.row_h == t.space.row_h);
}

TEST(modern_tokens_parse_overrides_and_errors) {
    Tokens t = default_tokens();
    std::string err;
    CHECK(parse_tokens(R"({"color": {"accent": "#102030", "scrim": "#00000080"},
                           "type": {"label": {"size": 30, "weight": 700, "caps": true}},
                           "space": {"row_h": 96},
                           "motion": {"focus": 0.3, "ease_out": [0.2, 0.9, 0.3, 1.0]}})",
                      t, &err));
    CHECK_NEAR(t.color.accent.r, 0x10 / 255.0, 1e-6);
    CHECK_NEAR(t.color.accent.b, 0x30 / 255.0, 1e-6);
    CHECK_NEAR(t.color.scrim.a, 0x80 / 255.0, 1e-6);
    CHECK(t.type.label.size == 30.0f && t.type.label.weight == 700 && t.type.label.caps);
    CHECK(t.type.label.tracking == default_tokens().type.label.tracking);  // o resto do estilo fica
    CHECK(t.space.row_h == 96.0f && t.space.section_h == default_tokens().space.section_h);
    CHECK(t.motion.focus == 0.3f && t.motion.ease_out.x1 == 0.2f);
    // Erros: o JSON fica como estava.
    const Tokens before = t;
    CHECK(!parse_tokens(R"({"color": {"accent": "gold"}})", t, &err) && !err.empty());
    CHECK(!parse_tokens(R"({"color": {"nope": "#ffffff"}})", t, &err));
    CHECK(!parse_tokens(R"({"space": {"row_h": -4}})", t, &err));
    CHECK(!parse_tokens(R"({"motion": {"ease_out": [2, 0, 0, 1]}})", t, &err));
    CHECK(!parse_tokens("{", t, &err));
    CHECK(t.color.accent == before.color.accent && t.space.row_h == before.space.row_h);
    Color c;
    CHECK(parse_color("#ffffff", &c) && c.r == 1.0f && c.a == 1.0f);
    CHECK(!parse_color("ffffff", &c) && !parse_color("#fff", &c) && !parse_color("#gg0000", &c));
}

TEST(modern_canvas_fits_height_or_width) {
    Canvas c = make_canvas(1920, 1080);
    CHECK(c.scale == 1.0f && c.off_x == 0.0f && c.off_y == 0.0f && c.left == 0.0f && c.right == kCanvasW);
    c = make_canvas(3840, 2160);
    CHECK(c.scale == 2.0f && c.off_x == 0.0f);
    // 21:9: pela altura, margem dos lados; o fundo vai até as bordas (left < 0, right > 1920).
    c = make_canvas(2560, 1080);
    CHECK(c.scale == 1.0f && c.off_x == 320.0f && c.left == -320.0f && c.right == 2240.0f && c.top == 0.0f);
    // 4:3: pela largura, faixas em cima e embaixo.
    c = make_canvas(1024, 768);
    CHECK_NEAR(c.scale, 1024.0 / 1920.0, 1e-6);
    CHECK_NEAR(c.off_y, (768.0 - 1080.0 * 1024.0 / 1920.0) * 0.5, 1e-3);
    CHECK(c.off_x == 0.0f && c.top < 0.0f && c.bottom > kCanvasH);
    // Ida e volta.
    for (const float x : {0.0f, 333.3f, 1919.0f}) CHECK_NEAR(canvas_x(c, window_x(c, x)), x, 1e-3);
    CHECK_NEAR(canvas_y(c, window_y(c, 777.0f)), 777.0, 1e-3);
}

TEST(modern_curves_and_tweens) {
    const Curve linear{0.0f, 0.0f, 1.0f, 1.0f};
    for (const float t : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) CHECK_NEAR(curve_eval(linear, t), t, 1e-3);
    const Curve out = default_tokens().motion.ease_out;
    CHECK(curve_eval(out, 0.0f) == 0.0f && curve_eval(out, 1.0f) == 1.0f);
    // Sair rápido e chegar devagar: na metade do tempo, bem mais que a metade do caminho; sempre subindo.
    CHECK(curve_eval(out, 0.5f) > 0.8f);
    float prev = 0.0f;
    for (int i = 1; i <= 50; ++i) {
        const float v = curve_eval(out, float(i) / 50.0f);
        CHECK(v >= prev - 1e-4f);
        prev = v;
    }
    const Curve in_out = default_tokens().motion.ease_in_out;
    CHECK_NEAR(curve_eval(in_out, 0.5f), 0.5, 0.02);
    CHECK(curve_eval(in_out, 0.2f) < 0.2f && curve_eval(in_out, 0.8f) > 0.8f);
    // O Tween: chega no fim da duração; interrompido, segue do valor do meio.
    Tween tw;
    tw.snap(0.0f);
    tw.set(100.0f, 0.2f, linear);
    tw.update(0.1f);
    CHECK_NEAR(tw.value(), 50.0, 0.5);
    tw.set(0.0f, 0.2f, linear);
    CHECK_NEAR(tw.value(), 50.0, 0.5);
    tw.update(0.1f);
    CHECK_NEAR(tw.value(), 25.0, 0.5);
    tw.update(1.0f);
    CHECK(tw.done() && tw.value() == 0.0f);
    // Pedir o mesmo alvo não recomeça.
    tw.set(10.0f, 0.2f, linear);
    tw.update(0.05f);
    const float mid = tw.value();
    tw.set(10.0f, 0.2f, linear);
    CHECK(tw.value() == mid);
}

TEST(modern_text_wrap_and_fit) {
    const Tokens& tk = default_tokens();
    const Canvas cv = make_canvas(1920, 1080);
    const Frame f{tk, cv, kMeasure, 0.0f};
    const TextStyle& st = tk.type.body;
    const std::wstring s = L"Renders the 3D scene at another resolution and scales it to the window.";
    const auto lines = wrap_lines(f, s, st, 200.0f);
    CHECK(lines.size() >= 3);
    std::wstring joined;
    for (const auto& l : lines) {
        CHECK(text_width(f, l, st) <= 200.0f || l.find(L' ') == std::wstring::npos);
        joined += (joined.empty() ? L"" : L" ") + l;
    }
    CHECK(joined == s);
    CHECK(wrap_lines(f, L"a\nb", st, 500.0f).size() == 2);
    const TextStyle fit = fit_style(f, L"CONTROLLER SENSITIVITY", tk.type.title, 300.0f, 20.0f);
    CHECK(fit.size < tk.type.title.size && text_width(f, L"CONTROLLER SENSITIVITY", fit) <= 300.5f);
}

TEST(modern_tab_bar_and_categories_layout) {
    const Tokens& tk = default_tokens();
    const Canvas cv = make_canvas(1920, 1080);
    const Frame f{tk, cv, kMeasure, 0.0f};
    TabBar bar;
    bar.labels = {L"PAUSE MENU", L"OPTIONS"};
    bar.active = 1;
    bar.layout(f);
    bar.set_active(1, f, false);
    CHECK(bar.tab_rects.size() == 2);
    // Centradas na tela, sem se encostar, com as teclas dos lados.
    const float mid = (bar.tab_rects.front().x0 + bar.tab_rects.back().x1) * 0.5f;
    CHECK_NEAR(mid, kCanvasW * 0.5f, 1.0);
    CHECK(bar.tab_rects[0].x1 < bar.tab_rects[1].x0);
    CHECK(bar.prev_rect.x1 < bar.tab_rects[0].x0 && bar.next_rect.x0 > bar.tab_rects[1].x1);
    // O sublinhado desliza para a outra aba.
    CHECK_NEAR(bar.ux0.value(), bar.tab_rects[1].x0 - tk.space.tab_pad, 0.01);
    bar.set_active(0, f, true);
    bar.update(tk.motion.tab * 0.5f);
    CHECK(bar.ux0.value() < bar.tab_rects[1].x0 && bar.ux0.value() > bar.tab_rects[0].x0);
    bar.update(1.0f);
    CHECK_NEAR(bar.ux0.value(), bar.tab_rects[0].x0 - tk.space.tab_pad, 0.01);
    CHECK(bar.hit(bar.tab_rects[0].cx(), bar.tab_rects[0].cy()) == 0 && bar.hit(bar.prev_rect.cx(), bar.prev_rect.cy()) == -2);
    CategoryList cats;
    for (int i = 0; i < 7; ++i) cats.items.push_back({L"CATEGORY", i == 3});
    cats.layout(f);
    for (size_t i = 1; i < cats.rects.size(); ++i) CHECK(cats.rects[i].y0 >= cats.rects[i - 1].y1 - 0.01f);
    CHECK(cats.rects.back().x1 < tk.space.mid_x0);
    CHECK(cats.hit(cats.rects[2].cx(), cats.rects[2].cy()) == 2);
}

TEST(modern_options_open_navigation_and_layout) {
    VideoFixture vf;
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    CHECK(s.active() && s.category() == Cat::Video && s.zone() == OptionsScreen::Zone::Content);
    CHECK(s.items().front().kind == Kind::Section && s.focus() == sc.find(Opt::Resolution));
    CHECK(s.open_progress() == 1.0f);
    // As linhas não se sobrepõem e a primeira começa no topo da área.
    for (size_t i = 1; i < s.items().size(); ++i) CHECK(s.item_rect(int(i)).y0 >= s.item_rect(int(i) - 1).y1 - 0.01f);
    CHECK(s.item_rect(0).y0 == default_tokens().space.list_top);
    // Baixo pula as seções.
    s.command(OptionsScreen::Cmd::Down);
    CHECK(s.focus() == sc.find(Opt::WindowMode));
    s.command(OptionsScreen::Cmd::Down);
    s.command(OptionsScreen::Cmd::Down);
    CHECK(s.focus() == sc.find(Opt::Adapter));
    s.command(OptionsScreen::Cmd::Down);
    CHECK(s.focus() == sc.find(Opt::Fov));
    // Voltar leva às categorias; baixo troca a categoria; direita entra nas linhas dela.
    s.command(OptionsScreen::Cmd::Back);
    CHECK(s.zone() == OptionsScreen::Zone::Categories);
    s.command(OptionsScreen::Cmd::Down);
    CHECK(s.category() == Cat::Graphics);
    s.command(OptionsScreen::Cmd::Right);
    CHECK(s.zone() == OptionsScreen::Zone::Content && s.focus() == sc.find(Opt::RenderScale));
    // DLSS/DLAA: travada (tem foco para a descrição, não muda nada).
    sc.focus(Opt::Dlss);
    CHECK(s.focus() == sc.find(Opt::Dlss) && s.items()[size_t(s.focus())].kind == Kind::Locked);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.open_dropdown() == -1 && !s.dialog_open());
    // Das categorias, voltar fecha a tela (com a animação de sair).
    s.command(OptionsScreen::Cmd::Back);
    s.command(OptionsScreen::Cmd::Back);
    CHECK(s.closing());
    sc.tick(0.5f);
    CHECK(!s.active());
}

TEST(modern_options_long_list_opens_around_the_value_and_the_wheel_scrolls_it) {
    // As resoluções de um monitor grande (7680 x 2160): a lista abria com a atual rente à borda de baixo (parecia acabar
    // nela) e a roda não rolava com o mouse em cima das opções.
    VideoFixture vf;
    std::vector<nf::DisplayMode> modes;
    const int sizes[][2] = {{640, 480},   {800, 600},   {1024, 768},  {1152, 864},  {1280, 720},  {1280, 768},
                            {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},  {1366, 768},  {1440, 900},
                            {1440, 1080}, {1600, 900},  {1600, 1024}, {1600, 1200}, {1680, 1050}, {1920, 1080},
                            {1920, 1200}, {1920, 1440}, {2048, 1536}, {2560, 1440}, {2560, 1600}, {3440, 1440},
                            {3840, 1080}, {3840, 1600}, {3840, 2160}, {5120, 1440}, {5120, 2160}, {7680, 2160}};
    for (const auto& sz : sizes) modes.push_back({sz[0], sz[1]});
    nf::video_controller().set_hardware(modes, nf::DisplayMode{7680, 2160}, {2, 4, 8});
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    const auto& sp = default_tokens().space;
    sc.focus(Opt::Resolution);
    s.command(OptionsScreen::Cmd::Select);
    sc.tick(0.6f);
    const int cur = s.dropdown_focus();
    const int n = int(modes.size());
    CHECK(s.open_dropdown() == sc.find(Opt::Resolution) && cur == 13);  // 1600 x 900, a da janela
    const auto visible = [&](int o) {
        const Rect r = s.option_rect(o);
        return r.y0 >= sp.list_top - 0.5f && r.y1 <= sp.list_bottom + 0.5f;
    };
    // A atual no meio da coluna, com opções acima e abaixo à vista.
    CHECK(visible(cur - 3) && visible(cur) && visible(cur + 3));
    CHECK_NEAR(s.option_rect(cur).cy(), (sp.list_top + sp.list_bottom) * 0.5f, sp.option_h);
    CHECK(!visible(n - 1));
    // A roda com o mouse em cima das opções rola a lista aberta até o maior tamanho.
    UiInput in;
    in.mouse_inside = true;
    in.mouse_x = window_x(sc.cv, s.option_rect(cur).cx());
    in.mouse_y = window_y(sc.cv, s.option_rect(cur).cy());
    sc.tick(0.016f, in);
    const float before = s.scroll();
    bool saw_last = false;
    in.wheel = -1.0f;
    for (int i = 0; i < 40; ++i) {
        sc.tick(0.016f, in);
        saw_last = saw_last || visible(n - 1);
    }
    in.wheel = 0.0f;
    for (int i = 0; i < 60; ++i) {
        sc.tick(0.016f, in);
        saw_last = saw_last || visible(n - 1);
    }
    CHECK(s.scroll() > before && saw_last && s.open_dropdown() == sc.find(Opt::Resolution));
}

TEST(modern_options_dropdown_toggle_slider_and_apply) {
    VideoFixture vf;
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    // A lista do modo de tela abre (Select), anda (baixo) e escolhe (Select); a cópia pendente muda.
    sc.focus(Opt::WindowMode);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.open_dropdown() == sc.find(Opt::WindowMode) && s.dropdown_focus() == 0);
    sc.tick(0.3f);
    // Aberta, ela empurra as linhas de baixo: a do VSync desce três opções.
    const float option_h = default_tokens().space.option_h;
    CHECK_NEAR(s.item_rect(sc.find(Opt::VSync)).y0 - s.item_rect(sc.find(Opt::WindowMode)).y1, 3.0 * option_h, 0.5);
    s.command(OptionsScreen::Cmd::Down);
    CHECK(s.dropdown_focus() == 1);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.open_dropdown() == -1 && s.pending().window_mode == nf::WindowMode::Borderless);
    sc.tick(0.3f);
    CHECK_NEAR(s.item_rect(sc.find(Opt::VSync)).y0, s.item_rect(sc.find(Opt::WindowMode)).y1, 0.5);
    // Voltar numa lista aberta só fecha.
    s.command(OptionsScreen::Cmd::Select);
    s.command(OptionsScreen::Cmd::Down);
    s.command(OptionsScreen::Cmd::Back);
    CHECK(s.open_dropdown() == -1 && s.zone() == OptionsScreen::Zone::Content &&
          s.pending().window_mode == nf::WindowMode::Borderless);
    // A chave: Select alterna, esquerda desliga, direita liga.
    sc.focus(Opt::VSync);
    CHECK(s.toggle_on(Opt::VSync));
    s.command(OptionsScreen::Cmd::Select);
    CHECK(!s.pending().vsync && s.value_text(Opt::VSync) == L"Off");
    s.command(OptionsScreen::Cmd::Right);
    CHECK(s.pending().vsync);
    s.command(OptionsScreen::Cmd::Left);
    CHECK(!s.pending().vsync);
    // A barra do campo de visão anda de 5 em 5 e para nas pontas.
    sc.focus(Opt::Fov);
    s.command(OptionsScreen::Cmd::Right);
    CHECK(s.pending().fov == 65.0f && s.value_text(Opt::Fov) == L"65°");
    for (int i = 0; i < 20; ++i) s.command(OptionsScreen::Cmd::Right);
    CHECK(s.pending().fov == 100.0f);
    // Nada foi aplicado ainda; F aplica (o modo de tela pede a confirmação: o diálogo abre).
    CHECK(vf.applied == 0);
    s.command(OptionsScreen::Cmd::Apply);
    CHECK(vf.applied == 1 && vf.last.window_mode == nf::WindowMode::Borderless && !vf.last.vsync && vf.last.fov == 100.0f);
    CHECK(s.dialog_open() && nf::video_controller().confirming() && s.dialog_focus() == 0);
    // Manter.
    s.command(OptionsScreen::Cmd::Select);
    CHECK(!s.dialog_open() && !nf::video_controller().confirming());
    CHECK(nf::video_controller().live().window_mode == nf::WindowMode::Borderless && s.toast_visible());
}

TEST(modern_options_graphics_adapter_row) {
    VideoFixture vf;
    std::vector<nf::AdapterInfo> l(2);
    l[0].index = 0;
    l[0].name = "NVIDIA GeForce RTX 5090";
    l[1].index = 1;
    l[1].name = "AMD Radeon(TM) Graphics";
    l[1].integrated = true;
    nf::video_controller().set_adapters(l, "NVIDIA GeForce RTX 5090");
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    // Em VIDEO, depois do VSync: Automático, Integrado, Dedicado e as duas placas pelo nome.
    CHECK(sc.find(Opt::Adapter) == sc.find(Opt::VSync) + 1);
    CHECK(s.choice_count(Opt::Adapter) == 5 && s.choice_index(Opt::Adapter) == 0);
    CHECK(s.value_text(Opt::Adapter) == L"Automatic");
    // A descrição diz qual placa está em uso.
    sc.focus(Opt::Adapter);
    CHECK(all_text(s, sc.cv).find(L"5090.") != std::wstring::npos);
    // Escolher o integrado: só a cópia muda; o Apply guarda e avisa que vale ao reiniciar (sem a confirmação de tela).
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.open_dropdown() == sc.find(Opt::Adapter));
    s.command(OptionsScreen::Cmd::Down);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.pending().adapter == "integrated" && s.value_text(Opt::Adapter) == L"Integrated" && vf.applied == 0);
    s.command(OptionsScreen::Cmd::Apply);
    CHECK(vf.applied == 1 && vf.last.adapter == "integrated" && !s.dialog_open());
    CHECK(nf::video_controller().live().adapter == "integrated");
    CHECK(s.toast_visible() && s.toast() == L"Restart the game to use the new graphics adapter");
    // Uma placa pelo nome: o valor é o nome inteiro.
    s.command(OptionsScreen::Cmd::Select);
    for (int i = 0; i < 3; ++i) s.command(OptionsScreen::Cmd::Down);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(s.pending().adapter == "AMD Radeon(TM) Graphics");
}

TEST(modern_options_confirmation_reverts_on_timeout) {
    VideoFixture vf;
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    sc.focus(Opt::Resolution);
    s.command(OptionsScreen::Cmd::Left);  // 1600 x 900 -> 1280 x 720
    CHECK(s.pending().width == 1280);
    s.command(OptionsScreen::Cmd::Apply);
    CHECK(s.dialog_open());
    // O tempo da confirmação anda com os quadros; acabado, a anterior volta e o diálogo fecha.
    for (int i = 0; i < 20; ++i) sc.tick(1.0f);
    CHECK(!s.dialog_open() && !nf::video_controller().confirming());
    CHECK(nf::video_controller().live().width == 1600 && s.pending().width == 1600);
    CHECK(vf.applied == 2);  // aplicar e voltar
}

TEST(modern_options_controls_has_the_mouse_and_no_duplicates) {
    VideoFixture vf;
    const float saved = psiMouseSensitivity;
    psiMouseSensitivity = nf::kDefaultMouseSensitivity;
    // CONTROLS: o aparelho e o mouse (sensibilidade, a da mira e a inversão do Y), onde o jogador procura.
    std::vector<Opt> rows;
    {
        Screen sc(Cat::Controls);
        for (const Item& it : sc.s.items()) {
            if (it.kind != Kind::Section) rows.push_back(it.opt);
        }
    }
    CHECK(rows == (std::vector<Opt>{Opt::Device, Opt::MouseSens, Opt::AimSens, Opt::InvertMouse}));
    // Cada opção aparece numa categoria só: o controle (inversão do Y incluída) em CONTROLLER, as teclas em KEY BINDINGS.
    int seen[64] = {};
    for (int c = 0; c < kCatCount; ++c) {
        Screen sc{Cat(c)};
        for (const Item& it : sc.s.items()) {
            if (it.kind == Kind::Section || it.kind == Kind::TableHead || it.kind == Kind::KeyRow) continue;
            if (it.opt == Opt::Apply) continue;  // cada categoria do Apply tem o dela
            ++seen[int(it.opt)];
            if (c == int(Cat::MouseKeyboard)) CHECK(it.opt == Opt::RestoreKeys);
        }
    }
    for (int o = 0; o < 64; ++o) CHECK(seen[o] <= 1);
    CHECK(seen[int(Opt::InvertPad)] == 1 && seen[int(Opt::MouseSens)] == 1 && seen[int(Opt::AimSens)] == 1);
    {
        Screen sc(Cat::Controller);
        CHECK(sc.find(Opt::InvertPad) >= 0 && sc.find(Opt::PadSens) >= 0);
    }
    // A barra da sensibilidade: o valor x 1000 e a volta em centímetros no medidor da descrição; a direita sobe um passo
    // (x 2^(1/8)).
    Screen sc(Cat::Controls);
    OptionsScreen& s = sc.s;
    sc.s.command(OptionsScreen::Cmd::Right);  // das categorias para as linhas
    sc.focus(Opt::MouseSens);
    CHECK(s.value_text(Opt::MouseSens) == L"2.20" && s.slider_pos(Opt::MouseSens) == nf::kMouseSensDefaultPos);
    DrawList dl;
    sc.tick(0.5f);
    s.build(dl, sc.cv, kMeasure);
    CHECK(dl.find_text(L"9.1 cm at 800 DPI  ·  4.5 cm at 1600 DPI") != nullptr);
    s.command(OptionsScreen::Cmd::Right);
    CHECK_NEAR(psiMouseSensitivity, nf::kDefaultMouseSensitivity * nf::fm::pow(2.0, 1.0 / 8.0), 1e-7);
    CHECK(s.slider_pos(Opt::MouseSens) == nf::kMouseSensDefaultPos + 1 && s.value_text(Opt::MouseSens) == L"2.40");
    // Restaurar padrões em CONTROLS volta o mouse.
    s.command(OptionsScreen::Cmd::Defaults);
    s.command(OptionsScreen::Cmd::Left);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(psiMouseSensitivity == nf::kDefaultMouseSensitivity);
    psiMouseSensitivity = saved;
}

TEST(modern_options_mouse_and_controls_categories) {
    VideoFixture vf;
    Screen sc(Cat::Video);
    OptionsScreen& s = sc.s;
    // Clicar numa categoria troca o conteúdo.
    const Rect cr = s.category_rect(int(Cat::Controller));
    sc.click(cr.cx(), cr.cy());
    CHECK(s.category() == Cat::Controller && s.zone() == OptionsScreen::Zone::Categories);
    // Passar o mouse numa linha dá o foco; clicar na chave alterna (vale na hora: a vibração do jogador).
    const int vib = sc.find(Opt::Vibration);
    const Rect vr = s.item_rect(vib);
    sc.click(vr.cx(), vr.cy(), false);
    CHECK(s.zone() == OptionsScreen::Zone::Content && s.focus() == vib);
    sc.click(vr.cx(), vr.cy());
    CHECK(PlayerSetting[0].unknown_09 == 0 && !nf::controls().vibration);
    // Clicar no trilho da barra põe o valor sob o mouse (a zona morta no fim do trilho = 30 = 47%).
    const int dz = sc.find(Opt::Deadzone);
    const Frame f{default_tokens(), sc.cv, kMeasure, 0.0f};
    const Rect track = slider_track(f, s.item_rect(dz));
    sc.click(track.x1 - 0.5f, track.cy());
    CHECK(s.slider_pos(Opt::Deadzone) == 30);
    sc.click(track.x0, track.cy());
    CHECK(s.slider_pos(Opt::Deadzone) == 0 && nf::controls().pad_deadzone == 0.0f);
    // Restaurar padrões (R) pergunta antes (o foco começa em Cancelar); Restaurar volta a zona morta e a vibração.
    s.command(OptionsScreen::Cmd::Defaults);
    CHECK(s.dialog_open() && s.dialog_focus() == 1);
    s.command(OptionsScreen::Cmd::Left);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(!s.dialog_open());
    CHECK_NEAR(nf::controls().pad_deadzone, 40.0 / 127.0, 1e-6);
    CHECK(PlayerSetting[0].unknown_09 == 1 && nf::controls().vibration);
}

TEST(modern_options_keys_table_and_wheel) {
    VideoFixture vf;
    Screen sc(Cat::MouseKeyboard);
    OptionsScreen& s = sc.s;
    int rows = 0, heads = 0;
    for (const Item& it : s.items()) {
        rows += it.kind == Kind::KeyRow ? 1 : 0;
        heads += it.kind == Kind::TableHead ? 1 : 0;
    }
    CHECK(rows == nf::kBindCommandCount && heads == 1);
    // Os comandos novos do menu Modern estão na tabela, com Q/E/R e LB/RB/X.
    const int prev = nf::find_bind_command("menu_tab_prev"), next = nf::find_bind_command("menu_tab_next");
    const int defaults = nf::find_bind_command("menu_defaults");
    CHECK(prev >= 0 && next >= 0 && defaults >= 0);
    const nf::Bindings d = nf::default_bindings();
    CHECK(d.in[prev][0] == nf::key_input('Q') && d.has(prev, nf::pad_input(nf::kPadInLB)));
    CHECK(d.in[next][0] == nf::key_input('E') && d.has(defaults, nf::key_input('R')));
    // Eles não tiram LB/RB do controle do PS2 (o jogo continua com L2/R2); só o L3 da roda de armas sai (nenhum estilo o usa).
    CHECK(nf::bindings_pad_mask(d) == 1u << nf::kPadInLThumb);
    // Descer até a primeira linha da tabela; direita anda pelas colunas e da primeira volta às categorias.
    int first_row = -1;
    for (size_t i = 0; i < s.items().size() && first_row < 0; ++i) {
        if (s.items()[i].kind == Kind::KeyRow) first_row = int(i);
    }
    for (int g = 0; g < 10 && s.focus() != first_row; ++g) s.command(OptionsScreen::Cmd::Down);
    CHECK(s.focus() == first_row && s.key_column() == 0);
    s.command(OptionsScreen::Cmd::Right);
    s.command(OptionsScreen::Cmd::Right);
    s.command(OptionsScreen::Cmd::Right);
    CHECK(s.key_column() == 2);
    // Descer pela tabela rola a lista para manter o foco à vista.
    for (int g = 0; g < 20; ++g) s.command(OptionsScreen::Cmd::Down);
    sc.tick(0.5f);
    CHECK(s.scroll() > 0.0f);
    const Rect fr = s.item_rect(s.focus());
    CHECK(fr.y0 >= default_tokens().space.list_top - 0.5f && fr.y1 <= default_tokens().space.list_bottom + 0.5f);
    // A roda sobre a lista rola para cima.
    UiInput in;
    in.mouse_inside = true;
    in.mouse_x = 900.0f;
    in.mouse_y = 500.0f;
    sc.tick(0.016f, in);
    in.wheel = 3.0f;
    const float before = s.scroll();
    sc.tick(0.016f, in);
    in.wheel = 0.0f;
    sc.tick(0.5f, in);
    CHECK(s.scroll() < before);
    // bind_slot: a segunda tecla de um comando fica atrás da primeira.
    nf::Bindings b = nf::default_bindings();
    const int fire = nf::find_bind_command("fire");
    CHECK(!nf::bind_slot(b, fire, 1, nf::key_input('F')).refused);
    CHECK(b.in[fire][0] == nf::key_input(nf::key::LButton) && b.in[fire][1] == nf::key_input('F'));
    CHECK(!nf::bind_slot(b, fire, 0, nf::key_input('G')).refused);
    CHECK(b.in[fire][0] == nf::key_input('G') && b.in[fire][1] == nf::key_input(nf::key::LButton));
    CHECK(nf::clear_bind_slot(b, fire, 1) && b.in[fire][0] == nf::key_input('G') && !b.has(fire, nf::key_input(nf::key::LButton)));
    const int pause = nf::find_bind_command("pause");
    CHECK(nf::clear_bind_slot(b, pause, 0) && b.in[pause][0] == nf::key_input(nf::key::Escape));  // Enter sai, o Esc fica
    CHECK(!nf::clear_bind_slot(b, pause, 0));  // obrigatória, a única tecla
}

TEST(modern_options_open_animation_and_draw_list) {
    VideoFixture vf;
    OptionsScreen s;
    const Canvas cv = make_canvas(1920, 1080);
    s.open(Cat::Graphics);
    s.frame(0.0f, UiInput{}, cv, kMeasure);
    CHECK(s.open_progress() == 0.0f && s.background() == 0.0f);
    s.frame(default_tokens().motion.open * 0.5f, UiInput{}, cv, kMeasure);
    const float half = s.open_progress();
    CHECK(half > 0.5f && half < 1.0f);  // ease-out: mais da metade na metade do tempo
    s.frame(1.0f, UiInput{}, cv, kMeasure);
    CHECK(s.open_progress() == 1.0f);
    DrawList dl;
    s.build(dl, cv, kMeasure);
    // A barra de cima, as categorias, o título da descrição e os comandos de baixo.
    CHECK(dl.find_text(L"OPTIONS") != nullptr && dl.find_text(L"PAUSE MENU") != nullptr);
    CHECK(dl.find_text(L"GRAPHICS") != nullptr && dl.find_text(L"KEY BINDINGS") != nullptr);
    CHECK(dl.find_text(L"RENDER SCALE") != nullptr);  // o título (versalete) da opção com o foco
    CHECK(dl.find_text(L"Restore to defaults") != nullptr && dl.find_text(L"Back") != nullptr);
    // O brilho do foco e o fundo (véu e vinheta), os recortes em par.
    CHECK(dl.count(DrawCmd::Glow) >= 3 && dl.count(DrawCmd::Radial) == 1);
    CHECK(dl.count(DrawCmd::PushClip) == dl.count(DrawCmd::PopClip));
    // Os textos ficam dentro da tela virtual na horizontal (fora o fundo, que vai até as bordas da janela).
    for (const DrawCmd& c : dl.cmds) {
        if (c.kind != DrawCmd::Text) continue;
        const float w = estimate_width(c.style.caps ? to_upper(c.text) : c.text, c.style);
        float x0 = c.r.x0, x1 = c.r.x0 + w;
        if (c.align == Align::Right) x0 = c.r.x1 - w, x1 = c.r.x1;
        if (c.align == Align::Centre) x0 = c.r.cx() - w * 0.5f, x1 = c.r.cx() + w * 0.5f;
        CHECK(x0 >= -1.0f && x1 <= kCanvasW + 1.0f);
    }
    // Fechar: o fundo some com a tela.
    s.request_close();
    s.frame(default_tokens().motion.close * 0.5f, UiInput{}, cv, kMeasure);
    CHECK(s.background() < 1.0f && s.active());
    s.frame(1.0f, UiInput{}, cv, kMeasure);
    CHECK(!s.active() && s.background() == 0.0f);
}

TEST(modern_options_input_swallow_and_tab_keys) {
    VideoFixture vf;
    OptionsScreen s;
    const Canvas cv = make_canvas(1920, 1080);
    bool keys[256] = {};
    UiInput in;
    in.keys = keys;
    // A tecla que abriu a tela (Espaço na pausa) ainda está apertada: não conta até ser solta.
    keys[nf::key::Space] = true;
    s.open(Cat::Video);
    s.frame(0.3f, in, cv, kMeasure);
    CHECK(s.open_dropdown() == -1);
    keys[nf::key::Space] = false;
    s.frame(0.016f, in, cv, kMeasure);
    keys[nf::key::Space] = true;
    s.frame(0.016f, in, cv, kMeasure);
    CHECK(s.open_dropdown() == s.focus());  // agora sim: abriu a lista da resolução
    keys[nf::key::Space] = false;
    keys[nf::key::Escape] = true;
    s.frame(0.016f, in, cv, kMeasure);
    CHECK(s.open_dropdown() == -1);
    keys[nf::key::Escape] = false;
    s.frame(0.016f, in, cv, kMeasure);
    // Segurar baixo repete depois da espera.
    const int start = s.focus();
    keys[nf::key::Down] = true;
    s.frame(0.016f, in, cv, kMeasure);
    CHECK(s.focus() != start);
    const int after_one = s.focus();
    s.frame(0.1f, in, cv, kMeasure);
    CHECK(s.focus() == after_one);  // ainda na espera
    s.frame(default_tokens().motion.repeat_delay, in, cv, kMeasure);
    CHECK(s.focus() != after_one);
    keys[nf::key::Down] = false;
    s.frame(0.016f, in, cv, kMeasure);
    // Q leva à aba PAUSE MENU: o sublinhado desliza e a tela fecha.
    keys['Q'] = true;
    s.frame(0.016f, in, cv, kMeasure);
    keys['Q'] = false;
    for (int i = 0; i < 40 && s.active(); ++i) s.frame(0.02f, in, cv, kMeasure);
    CHECK(!s.active());
}

TEST(modern_options_gameplay_rows_write_the_player_settings) {
    VideoFixture vf;
    GameOptionsFixture gf;
    Screen sc(Cat::Gameplay);
    OptionsScreen& s = sc.s;
    PlayerInput_tag& p = PlayerSetting[0];
    // As linhas da "Advanced Options" do jogo (a vibração fica em CONTROLLER), com o foco na primeira.
    const Opt rows[] = {Opt::AutoAim, Opt::Crosshairs, Opt::AimMode, Opt::CrouchMode, Opt::AutoSwitch, Opt::Flashing,
                        Opt::HudAlways};
    for (Opt o : rows) CHECK(sc.find(o) >= 0);
    CHECK(sc.find(Opt::Vibration) < 0 && s.focus() == sc.find(Opt::AutoAim));
    CHECK(s.items()[size_t(sc.find(Opt::AimMode))].kind == Kind::Dropdown);
    // Cada chave escreve o seu byte (no formato da página do jogo: itens piscando 2/0, HUD sempre visível 0/1).
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.autoaim == 0 && !s.toggle_on(Opt::AutoAim));
    sc.focus(Opt::Crosshairs);
    s.command(OptionsScreen::Cmd::Left);
    CHECK(p.unknown_08 == 0);
    sc.focus(Opt::AutoSwitch);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.auto_switch == 0);
    sc.focus(Opt::Flashing);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.unknown_0c == 0);
    s.command(OptionsScreen::Cmd::Right);
    CHECK(p.unknown_0c == 2);
    sc.focus(Opt::HudAlways);
    CHECK(s.toggle_on(Opt::HudAlways));
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.unknown_0b == 1);
    // Mira manual e agachar: "Toggle" (1) e "Hold" (0), na ordem da página do jogo.
    sc.focus(Opt::AimMode);
    CHECK(s.choice_count(Opt::AimMode) == 2 && s.choice_index(Opt::AimMode) == 1 && s.value_text(Opt::AimMode) == L"Hold");
    s.command(OptionsScreen::Cmd::Left);
    CHECK(p.aim_toggle == 1 && s.value_text(Opt::AimMode) == L"Toggle");
    sc.focus(Opt::CrouchMode);
    s.command(OptionsScreen::Cmd::Select);  // abre a lista
    CHECK(s.open_dropdown() == sc.find(Opt::CrouchMode));
    s.command(OptionsScreen::Cmd::Down);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.crouch_toggle == 0 && s.open_dropdown() == -1);
    // A descrição é o texto de ajuda do jogo para o valor em vigor.
    sc.tick(0.5f);
    CHECK(all_text(s, sc.cv).find(L"You must hold down the Crouch button") != std::wstring::npos);
    sc.focus(Opt::Crosshairs);
    sc.tick(0.5f);
    CHECK(all_text(s, sc.cv).find(L"Crosshair only appears while you're manually aiming.") != std::wstring::npos);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.unknown_08 == 1 && all_text(s, sc.cv).find(L"A small crosshair indicates") != std::wstring::npos);
    // Sem um .ini para gravar (os testes, as capturas sem --config) nada foi para o arquivo.
    CHECK(!nf::settings().has("game", "crosshairs") && !nf::game_options_ini().known[int(nf::GameOption::AutoAim)]);
    // Com ele, a opção mudada vai para [game] na hora, e só ela.
    nf::g_controls_persist = true;
    sc.focus(Opt::Crosshairs);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.unknown_08 == 0 && nf::settings().get("game", "crosshairs") == "0");
    CHECK(!nf::settings().has("game", "auto_aim") && !nf::settings().has("controls", "pad_style"));
    // Restaurar os padrões pergunta e põe os do perfil padrão do jogo (def_codename).
    std::memset(def_codename, 0, sizeof(def_codename));
    def_codename[0x24] = 1;  // mira automática
    def_codename[0x29] = 1;  // mira na tela
    def_codename[0x27] = 1;  // agachar alternando
    def_codename[0x2a] = 1;  // troca automática
    def_codename[0x2b] = 1;  // itens piscando (o valor do boot)
    s.command(OptionsScreen::Cmd::Defaults);
    CHECK(s.dialog_open());
    s.command(OptionsScreen::Cmd::Left);
    s.command(OptionsScreen::Cmd::Select);
    CHECK(p.autoaim == 1 && p.unknown_08 == 1 && p.aim_toggle == 0 && p.crouch_toggle == 1 && p.auto_switch == 1);
    CHECK(p.unknown_0c == 1 && p.unknown_0b == 0);
    CHECK(nf::settings().get("game", "crosshairs") == "1" && nf::settings().get("game", "manual_aim") == "hold");
}

TEST(modern_options_subtitles_and_volumes) {
    VideoFixture vf;
    GameOptionsFixture gf;
    {
        // INTERFACE: as legendas (DrawInfo[2]) valem na hora, sem o Apply.
        Screen sc(Cat::Interface);
        OptionsScreen& s = sc.s;
        const int sub = sc.find(Opt::Subtitles);
        CHECK(sub >= 0 && s.items()[size_t(sub)].kind == Kind::Toggle && sc.find(Opt::Apply) > sub);
        sc.focus(Opt::Subtitles);
        CHECK(!s.toggle_on(Opt::Subtitles));
        s.command(OptionsScreen::Cmd::Select);
        CHECK(DrawInfo[2] == 1 && nf::game_option(nf::GameOption::Subtitles) == 1);
        CHECK(s.pending() == nf::video_controller().live());  // nada pendente para o Apply
        sc.tick(0.5f);
        CHECK(all_text(s, sc.cv).find(L"Subtitles for all dialogue are displayed.") != std::wstring::npos);
        // Com o .ini: [ui] subtitles.
        nf::g_controls_persist = true;
        s.command(OptionsScreen::Cmd::Left);
        CHECK(DrawInfo[2] == 0 && nf::settings().get("ui", "subtitles") == "0");
        nf::g_controls_persist = false;
    }
    {
        // ÁUDIO: as barras da AV Options do jogo (0-20 = volume / 5) mexem nos volumes guardados.
        Screen sc(Cat::Audio);
        OptionsScreen& s = sc.s;
        CHECK(s.items()[size_t(sc.find(Opt::Music))].kind == Kind::Slider);
        CHECK(s.items()[size_t(sc.find(Opt::Effects))].kind == Kind::Slider);
        CHECK(s.focus() == sc.find(Opt::Music) && s.slider_pos(Opt::Music) == 14 && s.value_text(Opt::Music) == L"70%");
        s.command(OptionsScreen::Cmd::Right);
        CHECK(SFXMusicGetVolume() == 75 && MusicMasterVolume == 75);
        sc.focus(Opt::Effects);
        s.command(OptionsScreen::Cmd::Left);
        s.command(OptionsScreen::Cmd::Left);
        CHECK(SFXGetVolume() == 90 && s.slider_pos(Opt::Effects) == 18);
        // A descrição diz que o volume vale na hora (o módulo de áudio toca com ele) e fica guardado.
        sc.tick(0.5f);
        const std::wstring text = all_text(s, sc.cv);
        CHECK(text.find(L"Volume of the in game sfx.") != std::wstring::npos);
        CHECK(text.find(L"The volume changes at once") != std::wstring::npos);
        CHECK(text.find(L"not available") == std::wstring::npos);
        // Com o .ini: [game] music_volume / effects_volume.
        nf::g_controls_persist = true;
        s.command(OptionsScreen::Cmd::Right);
        CHECK(nf::settings().get_int("game", "effects_volume", -1) == 95 && !nf::settings().has("game", "music_volume"));
        // Restaurar: os padrões do "Restore Defaults" do jogo (70 e 100).
        s.command(OptionsScreen::Cmd::Defaults);
        s.command(OptionsScreen::Cmd::Left);
        s.command(OptionsScreen::Cmd::Select);
        CHECK(SFXMusicGetVolume() == 70 && SFXGetVolume() == 100);
        CHECK(nf::settings().get_int("game", "music_volume", -1) == 70 && nf::settings().get_int("game", "effects_volume", -1) == 100);
    }
}

TEST(modern_options_deep_link_opens_a_category) {
    VideoFixture vf;
    GameOptionsFixture gf;
    const Canvas cv = make_canvas(1920, 1080);
    Cat c = Cat::Video;
    CHECK(parse_cat("gameplay", &c) && c == Cat::Gameplay && std::string(cat_name(Cat::Gameplay)) == "gameplay");
    CHECK(parse_cat("audio", &c) && c == Cat::Audio && parse_cat("controller", &c) && c == Cat::Controller);
    {
        // Sozinha: abre na categoria com o foco na primeira linha; voltar das linhas sai de vez.
        OptionsScreen s;
        s.open_at(Cat::Gameplay);
        s.frame(0.0f, UiInput{}, cv, kMeasure);
        s.frame(0.5f, UiInput{}, cv, kMeasure);
        CHECK(s.active() && s.back_leaves() && s.category() == Cat::Gameplay && s.zone() == OptionsScreen::Zone::Content);
        CHECK(s.focus() >= 0 && s.items()[size_t(s.focus())].opt == Opt::AutoAim);
        s.command(OptionsScreen::Cmd::Back);
        CHECK(s.closing());
    }
    {
        // Embutida numa tela anfitriã (o menu principal): voltar vira o pedido dela; a tela continua aberta.
        OptionsScreen s;
        s.set_embedded(true);
        s.open_at(Cat::Controller);
        s.frame(0.0f, UiInput{}, cv, kMeasure);
        CHECK(s.category() == Cat::Controller && s.items()[size_t(s.focus())].opt == Opt::PadSens);
        s.command(OptionsScreen::Cmd::Down);
        s.command(OptionsScreen::Cmd::Back);
        CHECK(s.take_back_request() && s.active() && !s.closing() && !s.take_back_request());
        // As outras categorias continuam lá (o mouse troca).
        s.frame(0.5f, UiInput{}, cv, kMeasure);
        UiInput in;
        in.mouse_inside = true;
        const Rect cr = s.category_rect(int(Cat::Gameplay));
        in.mouse_x = window_x(cv, cr.cx());
        in.mouse_y = window_y(cv, cr.cy());
        s.frame(0.016f, in, cv, kMeasure);
        in.lmb = true;
        s.frame(0.016f, in, cv, kMeasure);
        CHECK(s.category() == Cat::Gameplay);
        // Sem back_leaves, voltar das linhas vai às categorias, como na pausa.
        s.open_at(Cat::Audio, false);
        CHECK(!s.back_leaves());
        s.command(OptionsScreen::Cmd::Back);
        CHECK(s.zone() == OptionsScreen::Zone::Categories && !s.take_back_request());
        s.command(OptionsScreen::Cmd::Back);
        CHECK(s.take_back_request());
    }
    // open() (a aba OPTIONS da pausa) não sai pelas linhas.
    OptionsScreen s;
    s.open_at(Cat::Audio);
    s.open(Cat::Audio);
    CHECK(!s.back_leaves());
}
