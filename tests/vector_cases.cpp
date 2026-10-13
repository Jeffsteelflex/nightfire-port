// Os casos da comparação do desenho vetorial. Ver vector_cases.h.

#include "vector_cases.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "engine/menu_modern.h"
#include "engine/pc_text.h"
#include "platform/crt_compat.h"
#include "platform/input_bindings.h"
#include "platform/psi_input.h"
#include "platform/video_settings.h"
#include "ui/modern_end.h"
#include "ui/modern_front.h"
#include "ui/modern_options.h"
#include "ui/modern_pause.h"
#include "ui/modern_setup.h"
#include "ui/modern_wheel.h"

namespace nf::vcase {

namespace {

using namespace nf::ui;

std::string read_text(const std::filesystem::path& p) {
    FILE* f = nf::fopen_narrow(p.string().c_str(), "rb");
    if (f == nullptr) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// Uma tela virtual de escala 1 (as listas sintéticas: as coordenadas são os pixels da imagem).
Canvas plain_canvas(int w, int h, float scale = 1.0f, float ox = 0.0f, float oy = 0.0f) {
    Canvas cv;
    cv.win_w = w;
    cv.win_h = h;
    cv.scale = scale;
    cv.off_x = ox;
    cv.off_y = oy;
    cv.left = -ox / scale;
    cv.top = -oy / scale;
    cv.right = (float(w) - ox) / scale;
    cv.bottom = (float(h) - oy) / scale;
    return cv;
}

Region region(const std::string& name, const Canvas& cv, const Rect& r) {
    Region g;
    g.name = name;
    g.x0 = int(std::floor(window_x(cv, r.x0)));
    g.y0 = int(std::floor(window_y(cv, r.y0)));
    g.x1 = int(std::ceil(window_x(cv, r.x1)));
    g.y1 = int(std::ceil(window_y(cv, r.y1)));
    return g;
}

// ---------------------------------------------------------------- listas sintéticas

const Color kGold = rgb(0xf2c77a);
const Color kText = rgb(0xede6da);

Case shapes_case(const Canvas& cv, const std::string& name) {
    Case c;
    c.name = name;
    c.group = "lista";
    c.width = cv.win_w;
    c.height = cv.win_h;
    c.canvas = cv;
    DrawList d;
    // Retângulos: liso, arredondado, sublinhados finos (presos ao pixel), com alfa.
    d.fill(Rect{20, 20, 140, 80}, kGold);
    d.fill(Rect{160.5f, 20.25f, 280.5f, 80.75f}, rgb(0x8a8174), 12.0f);
    d.fill(Rect{300, 30.3f, 460, 31.9f}, kGold);
    d.fill(Rect{300, 40.6f, 460, 41.1f}, kText);
    d.fill(Rect{300.4f, 50, 301.2f, 80}, kText);
    d.fill(Rect{320, 50, 460, 80}, rgb(0xff8a3d, 0.5f));
    // Contornos: liso, arredondado, largos.
    d.stroke(Rect{20, 100, 140, 160}, kText, 1.0f);
    d.stroke(Rect{160.3f, 100.6f, 280.7f, 160.2f}, kGold, 2.0f, 10.0f);
    d.stroke(Rect{300, 100, 460, 160}, rgb(0xc4bbae, 0.6f), 3.0f, 4.0f);
    // Linhas: horizontal e vertical de 1 e 2 (presas), inclinadas.
    d.line(20, 180.4f, 200, 180.4f, kText, 1.0f);
    d.line(20, 190.7f, 200, 190.7f, kGold, 2.0f);
    d.line(220.6f, 175, 220.6f, 240, kText, 1.0f);
    d.line(240, 175, 300, 240, kGold, 1.0f);
    d.line(320, 240, 460, 175, kText, 2.5f);
    // Elipses: cheia e contorno.
    d.ellipse(Rect{20, 260, 120, 320}, rgb(0x2a1608));
    d.ellipse(Rect{140, 260, 200, 320}, kGold, 2.0f);
    d.ellipse(Rect{215, 270.5f, 255.5f, 311}, rgb(0xff8a3d, 0.7f));
    // Setas finas (as quatro direções), o cadeado e o losango.
    d.chevron(Rect{280, 270, 300, 280}, 0, kText, 1.5f);
    d.chevron(Rect{310, 270, 330, 280}, 1, kText, 1.5f);
    d.chevron(Rect{340, 265, 350, 285}, 2, kGold, 2.0f);
    d.chevron(Rect{360, 265, 370, 285}, 3, kGold, 2.0f);
    d.padlock(Rect{390, 262, 410, 288}, kText);
    d.diamond(Rect{425, 265, 445, 285}, kGold);
    d.diamond(Rect{450.5f, 268.5f, 462.5f, 280.5f}, rgb(0x8a8174));
    // Uma linha fina entre colunas (a hairline dos menus).
    d.fill(Rect{20, 340, 460, 341}, rgb(0xf5e6d0, 0.18f));
    c.lists.push_back(std::move(d));
    return c;
}

Case gradients_case() {
    const Canvas cv = plain_canvas(480, 360);
    Case c;
    c.name = "lista_degrades";
    c.group = "lista";
    c.width = cv.win_w;
    c.height = cv.win_h;
    c.canvas = cv;
    DrawList d;
    // A vinheta: o degradê radial cobre a janela inteira (a borda fora da elipse).
    d.radial(Rect{40, 30, 440, 330}, rgb(0x2a1608, 0.30f), rgb(0x050302, 0.80f));
    // Degradês lineares, vertical e horizontal, com alfa e com a mesma cor.
    d.gradient(Rect{20, 20, 140, 120}, rgb(0x000000, 0.35f), rgb(0x000000, 0.0f), false);
    d.gradient(Rect{160, 20, 300, 60}, kGold.with_alpha(0.0f), rgb(0xffe2a8, 0.8f), true);
    d.gradient(Rect{160, 70, 300, 120}, rgb(0x2a1608, 0.35f), rgb(0x2a1608, 0.0f), false);
    // A sonda da mistura: vermelho opaco a azul transparente (o meio diz como o degradê interpola o alfa).
    d.gradient(Rect{320, 20, 460, 120}, Color{1, 0, 0, 1}, Color{0, 0, 1, 0}, true);
    // O brilho de brasa recortado acima do sublinhado, em camadas como o FocusMark.
    const float ly = 200.0f;
    d.glow(Rect{60, ly - 19, 300, ly + 19}, rgb(0xb8400f, 0.40f), rgb(0xb8400f, 0.0f), ly);
    d.glow(Rect{110, ly - 44, 250, ly + 44}, rgb(0xb8400f, 0.26f), rgb(0xb8400f, 0.0f), ly);
    d.glow(Rect{150, ly - 30, 190, ly + 30}, rgb(0xff8a3d, 0.50f), rgb(0xb8400f, 0.0f), ly);
    d.glow(Rect{160, ly - 18, 200, ly + 18}, rgb(0xffe2a8, 0.55f), rgb(0xff8a3d, 0.0f), ly);
    d.fill(Rect{60, ly - 2, 300, ly}, kGold);
    // Setores da roda: lisos, com o miolo vazio e em degradê do raio de dentro para o de fora.
    const float cx = 380, cy = 250;
    d.sector(cx, cy, 40, 90, -0.6f, 0.6f, rgb(0x1a120c, 0.85f));
    d.sector(cx, cy, 40, 90, 0.65f, 1.9f, rgb(0x2b1d12, 0.85f));
    d.sector_gradient(cx, cy, 40, 90, 2.0f, 3.0f, kGold.with_alpha(0.0f), kGold.with_alpha(0.6f));
    d.sector(cx, cy, 0, 30, 3.1f, 6.2f, rgb(0xff8a3d, 0.4f));
    d.sector_gradient(cx, cy, 90, 110, -0.6f, 0.6f, rgb(0xff8a3d, 0.30f), rgb(0xff8a3d, 0.0f));
    c.lists.push_back(std::move(d));
    return c;
}

struct TextRow {
    const char* name;
    TextStyle st;
    const wchar_t* text;
    Align align;
    bool wrap;
    float h;
};

Case texts_case() {
    const Canvas cv = plain_canvas(960, 720);
    const Tokens& tk = default_tokens();
    Case c;
    c.name = "lista_textos";
    c.group = "lista";
    c.width = cv.win_w;
    c.height = cv.win_h;
    c.canvas = cv;
    TextStyle big_title = tk.type.page_title;
    TextStyle mono = tk.type.mono;
    mono.size = 14.0f;
    TextStyle bold = tk.type.label;
    bold.weight = 700;
    TextStyle light = tk.type.label;
    light.weight = 300;
    light.size = 30.0f;
    TextStyle hero = tk.type.hero;
    hero.size = 56.0f;
    const TextRow rows[] = {
        {"label", tk.type.label, L"Resolution", Align::Left, false, 34},
        {"value", tk.type.value, L"1920 \u00d7 1080", Align::Right, false, 34},
        {"tab", tk.type.tab, L"Options", Align::Centre, false, 30},
        {"menu_big", tk.type.menu_big, L"Resume", Align::Left, false, 60},
        {"page_title", big_title, L"Select Codename", Align::Centre, false, 70},
        {"section", tk.type.section, L"Post effects", Align::Left, false, 26},
        {"keycap", tk.type.keycap, L"Esc", Align::Centre, false, 26},
        {"hero", hero, L"Mission failed", Align::Centre, false, 76},
        {"light", light, L"A\u00e7\u00e3o: op\u00e7\u00f5es \u2014 \u00e9 \u00e7 \u00f5", Align::Left, false, 42},
        {"bold", bold, L"Bold weight 700 \u2026 \u2019quoted\u2019", Align::Right, false, 34},
        {"mono", mono, L"Data 0x1F 07000012", Align::Left, false, 24},
        {"body", tk.type.body, L"Frame interpolation draws every video frame between the last two logic ticks, so the "
                               L"view stays smooth above 60 fps while the game logic keeps its own pace.",
         Align::Centre, true, 90},
        {"legal", tk.type.legal, L"\u00a9 2026 Synthetic test text for the vector comparison. Every name here is made "
                                 L"up; nothing comes from the game disc.",
         Align::Left, true, 46},
    };
    DrawList d;
    float y = 12.0f;
    for (const TextRow& r : rows) {
        const Rect rect = r.wrap ? Rect{180, y, 780, y + r.h} : Rect{24, y, 936, y + r.h};
        d.text(rect, r.text, r.st, kText, r.align, r.wrap);
        c.regions.push_back(region(std::string("texto:") + r.name, cv, rect));
        if (r.st.face == Face::Mono) c.regions.back().box_tol = 16;  // a Menlo é ~8% mais larga que a Consolas
        y += r.h + 8.0f;
    }
    c.lists.push_back(std::move(d));
    return c;
}

Case glyphs_case() {
    const Canvas cv = plain_canvas(480, 280);
    Case c;
    c.name = "lista_glifos";
    c.group = "lista";
    c.width = cv.win_w;
    c.height = cv.win_h;
    c.canvas = cv;
    DrawList d;
    const char* names[] = {"xbox_a", "xbox_b", "xbox_lb", "xbox_rt", "xbox_menu", "xbox_dpad", "ps2_cross", "ps2_r2"};
    float x = 20.0f;
    for (const char* n : names) {
        const bool wide = std::string(n).find("lb") != std::string::npos || std::string(n).find("rt") != std::string::npos;
        const Rect r{x, 20.5f, x + (wide ? 44.0f : 32.0f), 52.5f};
        d.glyph(r, n, 1.0f);
        c.regions.push_back(region(std::string("glifo:") + n, cv, r));
        x += wide ? 54.0f : 42.0f;
    }
    // Com alfa (a camada do Direct2D) e grande.
    d.glyph(Rect{20, 70, 60, 110}, "xbox_y", 0.5f);
    d.glyph(Rect{80, 64, 300, 174}, "results_007", 0.8f);
    // Um nome sem SVG: a tecla clara.
    d.glyph(Rect{320, 70, 352, 102}, "nao_existe", 1.0f);
    // Recortes: um retângulo grande cortado por dois recortes aninhados e um texto cortado ao meio.
    d.push_clip(Rect{20.4f, 190.6f, 250.5f, 260.2f});
    d.fill(Rect{0, 180, 300, 280}, rgb(0x2a1608));
    d.push_clip(Rect{100, 150, 400, 230});
    d.fill(Rect{0, 180, 480, 280}, kGold.with_alpha(0.6f));
    d.text(Rect{60, 200, 400, 240}, L"CLIPPED TEXT", default_tokens().type.menu_big, kText);
    d.pop_clip();
    d.pop_clip();
    // Um recorte esquecido aberto (o desenho fecha os que sobram).
    d.push_clip(Rect{300, 190, 460, 270});
    d.ellipse(Rect{280, 180, 480, 280}, rgb(0xff8a3d, 0.8f));
    c.lists.push_back(std::move(d));
    return c;
}

// ---------------------------------------------------------------- telas

const Measure measure_of(UiVectorCanvas& canvas) {
    return [&canvas](const std::wstring& s, const TextStyle& st) { return canvas.measure(s, st); };
}

// O estado comum das telas: o controlador de vídeo com um monitor conhecido, os textos do port em inglês, os controles
// padrão, sem modal.
struct ScreenWorld {
    ScreenWorld() {
        VideoController& vc = video_controller();
        VideoSettings live;
        live.width = 1920;
        live.height = 1080;
        vc.init(live);
        vc.set_hardware({{1280, 720}, {1600, 900}, {1920, 1080}}, DisplayMode{1920, 1080}, {2, 4, 8});
        vc.set_apply([](const VideoSettings&, const VideoSettings&) {});
        vc.set_store([](const VideoSettings&) { return true; });
        PcText_SetLanguage("en");
        controls() = ControlsConfig{};
        psiInput_SetModal(false);
        menu_modern_debug_power_on();
    }
    ~ScreenWorld() {
        VideoController& vc = video_controller();
        if (vc.confirming()) vc.revert();
        vc.set_apply(nullptr);
        vc.set_store(nullptr);
        vc.init(VideoSettings{});
        controls() = ControlsConfig{};
        menu_modern_debug_power_on();
    }
};

constexpr float kDt = 1.0f / 60.0f;

Case screen_case(const char* name, const Canvas& cv) {
    Case c;
    c.name = name;
    c.group = "tela";
    c.width = cv.win_w;
    c.height = cv.win_h;
    c.canvas = cv;
    return c;
}

MmSnapshot pause_snapshot() {
    MmSnapshot s;
    s.screen = MmScreen::Pause;
    s.status = 3;
    s.page = s.base_page = kMmPausePage;
    s.ready = s.accepts = true;
    s.tab = 0;
    s.tab_text[0] = "MISSION";
    s.tab_text[1] = "OBJECTIVES";
    s.tab_text[3] = "CONTROLS";
    s.tab_text[4] = "SCORE";
    s.rows = {{"CONTINUE", 0}, {"RESTART", 0}, {"QUIT", 0}, {"OPTIONS", 3}};
    s.styles = {"NightFire", "Moonraker", "Octopussy"};
    s.axis_label = "Y-Axis";
    s.axis_value = "Normal";
    s.mission = "Night Shift";
    s.mission_text = "Meet the contact on the roof and stop the courier before the train leaves the station.";
    s.difficulty = "Agent";
    s.current_label = "Escape from the tower";
    s.level = 1;
    s.levels = 2;
    MmObjective a;
    a.state = MmObjState::Active;
    a.label = "Escape from the tower";
    a.text = "Find a way down before the guards close the exits.";
    a.status = "Incomplete";
    MmObjective b;
    b.state = MmObjState::Done;
    b.label = "Meet the contact";
    b.status = "Complete";
    MmObjective h;
    s.objectives = {a, b, h};
    s.visible = 2;
    s.done = 1;
    s.buttons = {{'A', "Jump"}, {'B', "Crouch"}, {'X', "Use"}, {'Y', "Gadget"}, {'L', "Aim"}, {'R', "Fire"}};
    return s;
}

Case options_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_opcoes", cv);
    const Measure m = measure_of(canvas);
    OptionsScreen s;
    s.set_build("vetor");
    s.open(Cat::Video);
    for (int i = 0; i < 40; ++i) s.frame(kDt, UiInput{}, cv, m);
    DrawList d;
    s.build(d, cv, m);
    c.lists.push_back(std::move(d));
    return c;
}

Case pause_screen(UiVectorCanvas& canvas, const Canvas& cv, bool controls_tab) {
    Case c = screen_case(controls_tab ? "tela_pausa_controles" : "tela_pausa", cv);
    const Measure m = measure_of(canvas);
    PauseScreen p;
    p.set_build("vetor");
    MmSnapshot s = pause_snapshot();
    if (controls_tab) s.tab = int(kMmTabControls);
    p.open(s, controls_tab ? PauseScreen::Tab::Controls : PauseScreen::Tab::Pause);
    for (int i = 0; i < 40; ++i) p.frame(kDt, UiInput{}, s, cv, m);
    DrawList d;
    p.build(d, cv, m);
    c.lists.push_back(std::move(d));
    menu_modern_debug_power_on();
    return c;
}

Case fail_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_fim", cv);
    const Measure m = measure_of(canvas);
    FailScreen f;
    f.set_build("vetor");
    MmSnapshot s;
    s.screen = MmScreen::Fail;
    s.status = 4;
    s.page = s.base_page = kMmFailPage;
    s.ready = s.accepts = true;
    s.fail_title = "Mission Failed";
    s.fail_reason = "Killed In Action";
    s.fail_rows = {{"REPLAY", 0}, {"QUIT", 2}};
    f.open(s);
    for (int i = 0; i < 200; ++i) f.frame(kDt, UiInput{}, s, cv, m);
    DrawList back, front;
    f.build_back(back, cv, m);
    f.build(front, cv, m);
    c.lists.push_back(std::move(back));
    c.lists.push_back(std::move(front));
    menu_modern_debug_power_on();
    return c;
}

Case results_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_resultado", cv);
    const Measure m = measure_of(canvas);
    ResultsScreen r;
    r.set_build("vetor");
    MmSnapshot s;
    s.screen = MmScreen::Results;
    s.status = 5;
    s.page = s.base_page = kMmResultsPage;
    s.ready = s.accepts = true;
    s.complete_title = "Mission Complete";
    s.mission = "Night Shift";
    s.score_label = "Total Score:";
    s.score_value = "4,599,808";
    s.target_label = "Gold Medal Target:";
    s.target_value = "100,000";
    s.medal = "Platinum";
    r.open(s);
    for (int i = 0; i < 60; ++i) r.frame(kDt, UiInput{}, s, cv, m);
    DrawList d;
    r.build(d, cv, m);
    c.lists.push_back(std::move(d));
    menu_modern_debug_power_on();
    return c;
}

Case front_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_principal", cv);
    const Measure m = measure_of(canvas);
    FrontScreen fs;
    fs.set_build("vetor");
    fs.set_art(false);
    MmSnapshot s;
    s.screen = MmScreen::Front;
    s.status = 5;
    s.page = s.base_page = kMmFeMain;
    s.ready = s.accepts = true;
    s.front.view = MmFrontView::Main;
    s.front.buttons = {"NightFire", "Multiplayer", "Codenames"};
    s.front.button_help = {"Play the single player game", "Not available yet", "Manage your codenames"};
    s.front.focus = 0;
    fs.open(s);
    for (int i = 0; i < 90; ++i) fs.frame(kDt, UiInput{}, s, cv, m);
    DrawList back, veil, front;
    fs.build_back(back, cv, m);
    fs.build_veil(veil, cv, m);
    fs.build(front, cv, m);
    c.lists.push_back(std::move(back));
    c.lists.push_back(std::move(veil));
    c.lists.push_back(std::move(front));
    menu_modern_debug_power_on();
    return c;
}

Case setup_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_entrada", cv);
    const Measure m = measure_of(canvas);
    SetupScreen s;
    s.set_build("vetor");
    SetupView v;
    v.ready = true;
    v.can_pack = true;
    v.disc = L"C:\\games\\007 - Nightfire (USA).iso";
    v.disc_kind = L"Disc image";
    v.disc_name = L"007 - Nightfire (USA).iso";
    v.status = L"Ready: 007: Nightfire (PS2, USA), SLUS-20579";
    v.pack_size = L"1.8 GB";
    LauncherHardware hw;
    hw.modes = {{1920, 1080}, {1280, 720}, {1600, 900}, {1024, 768}};
    hw.desktop = {1920, 1080};
    s.open(v, LauncherSettings{}, hw);
    for (int i = 0; i < 40; ++i) s.frame(kDt, UiInput{}, cv, m);
    DrawList d;
    s.build(d, cv, m);
    c.lists.push_back(std::move(d));
    return c;
}

Case wheel_screen(UiVectorCanvas& canvas, const Canvas& cv) {
    Case c = screen_case("tela_roda", cv);
    const Measure m = measure_of(canvas);
    WheelView v;
    for (const wchar_t* n : {L"Wolfram P2K", L"Storm M32", L"Frinesi", L"Kowloon Type 80", L"Raptor"}) {
        WheelViewSlot s;
        s.name = n;
        s.ammo = L"7-41";
        v.weapons.push_back(s);
    }
    v.weapons[0].current = true;
    v.weapons[2].usable = false;
    v.weapons[2].ammo = L"\u2014";
    v.weapons[3].mode = L"Burst";
    for (const wchar_t* n : {L"Q-Laser", L"Grapple", L"Micro Camera"}) {
        WheelViewSlot g;
        g.name = n;
        g.ammo = L"\u2014";
        v.gadgets.push_back(g);
    }
    v.hover_ring = 2;
    v.hover_index = 1;
    v.sel_x = 0.55f;
    v.sel_y = 0.45f;
    v.key = L"Tab";
    v.in_hand = L"Wolfram P2K";
    DrawList back, front;
    ImageList icons;
    build_weapon_wheel(back, icons, front, cv, default_tokens(), m, v);
    c.lists.push_back(std::move(back));
    c.lists.push_back(std::move(front));
    return c;
}

// ---------------------------------------------------------------- camada HD

UiHdLine hd_line(const std::wstring& text, float x0, float x1, float baseline, const uint8_t rgba[4]) {
    UiHdLine l;
    l.text = text;
    l.x0 = x0;
    l.x1 = x1;
    l.baseline = baseline;
    UiHdSpan sp;
    sp.start = 0;
    sp.length = uint32_t(text.size());
    std::copy(rgba, rgba + 4, sp.rgba);
    l.spans.push_back(sp);
    return l;
}

void set_clip(float out[4], float x0, float y0, float x1, float y1) {
    out[0] = x0;
    out[1] = y0;
    out[2] = x1;
    out[3] = y1;
}

Case hd_texts_case() {
    Case c;
    c.name = "hd_textos";
    c.group = "hd";
    c.kind = Case::Hd;
    c.width = 800;
    c.height = 520;
    const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    const uint8_t yellow[4] = {0x80, 0x78, 0x20, 0x80};
    const uint8_t black[4] = {0x00, 0x00, 0x00, 0x80};
    const uint8_t dim[4] = {0x60, 0x60, 0x60, 0x50};
    const auto add = [&](UiHdText t, const char* name, float x0, float y0, float x1, float y1) {
        c.order.push_back(UiHdOp{UiHdOp::Text, uint32_t(c.texts.size())});
        c.texts.push_back(std::move(t));
        Region r;
        r.name = name;
        r.x0 = int(x0);
        r.y0 = int(y0);
        r.x1 = int(x1);
        r.y1 = int(y1);
        r.box_tol = 12;  // a fonte do map.json no macOS é outra (DIN Alternate, ~8% mais estreita)
        c.regions.push_back(r);
    };
    // 1: fonte 2 (a das legendas), à esquerda, com o contorno de 1 pixel do PS2 (o traço).
    {
        UiHdText t;
        t.font = 2;
        t.cap_px = 18;
        t.align = 0;
        t.lines.push_back(hd_line(L"Welcome back - right on time...", 40, 380, 60, white));
        UiHdEffect e;
        e.kind = UiHdEffect::Stroke;
        e.width = 2.6f;
        std::copy(black, black + 4, e.rgba);
        t.effects.push_back(e);
        set_clip(t.clip, 0, 0, 800, 520);
        add(std::move(t), "hd:legenda", 20, 20, 520, 80);
    }
    // 2: fonte 1, centrada, duas linhas, sombra deslocada; a segunda linha com dois trechos de cor.
    {
        UiHdText t;
        t.font = 1;
        t.cap_px = 14;
        t.align = 2;
        t.lines.push_back(hd_line(L"NEW OBJECTIVE", 300, 500, 130, yellow));
        UiHdLine l2 = hd_line(L"Picked up 20x 9mm rounds", 270, 530, 152, white);
        l2.spans[0].length = 10;
        UiHdSpan s2;
        s2.start = 10;
        s2.length = uint32_t(l2.text.size()) - 10;
        std::copy(yellow, yellow + 4, s2.rgba);
        l2.spans.push_back(s2);
        t.lines.push_back(l2);
        UiHdEffect e;
        e.kind = UiHdEffect::Copy;
        e.dx = 2;
        e.dy = 2;
        std::copy(black, black + 4, e.rgba);
        t.effects.push_back(e);
        set_clip(t.clip, 0, 0, 800, 520);
        add(std::move(t), "hd:objetivo", 200, 100, 600, 170);
    }
    // 3: fonte 3 (negrito itálico), à direita, grande, e uma linha mais larga que a original (comprimida até o fit_min).
    {
        UiHdText t;
        t.font = 3;
        t.cap_px = 30;
        t.align = 4;
        t.lines.push_back(hd_line(L"MISSION FAILED", 360, 760, 250, white));
        t.lines.push_back(hd_line(L"Compressed line that is much wider", 600, 760, 290, dim));
        set_clip(t.clip, 0, 0, 800, 520);
        add(std::move(t), "hd:titulo", 340, 200, 790, 300);
    }
    // 4: um trecho preso (entre símbolos) e um texto cortado pelo recorte.
    {
        UiHdText t;
        t.font = 2;
        t.cap_px = 12;
        t.align = 0;
        UiHdLine l = hd_line(L"Select", 60, 110, 360, white);
        l.pinned = true;
        t.lines.push_back(l);
        t.lines.push_back(hd_line(L"Back to the mission map", 160, 330, 360, white));
        set_clip(t.clip, 0, 0, 800, 520);
        add(std::move(t), "hd:presa", 40, 330, 400, 375);
    }
    {
        UiHdText t;
        t.font = 2;
        t.cap_px = 24;
        t.align = 0;
        t.lines.push_back(hd_line(L"Clipped at the band", 40, 460, 450, yellow));
        set_clip(t.clip, 40, 425, 300, 445);
        add(std::move(t), "hd:recorte", 30, 410, 480, 470);
    }
    return c;
}

Case hd_keys_case() {
    Case c;
    c.name = "hd_teclas";
    c.group = "hd";
    c.kind = Case::Hd;
    c.width = 640;
    c.height = 200;
    struct K {
        const wchar_t* label;
        float h;
        uint8_t alpha;
    };
    const K keys[] = {{L"Esc", 22, 0x80}, {L"Enter", 22, 0x80}, {L"W", 18, 0x80}, {L"\u2191", 26, 0x80},
                      {L"Mouse", 30, 0x80}, {L"Space", 40, 0x80}, {L"Tab", 26, 0x40}, {L"LMB", 16, 0x80}};
    float x = 20;
    for (const K& k : keys) {
        UiHdKey key;
        key.label = k.label;
        key.cap_px = k.h * 0.42f;
        const float w = ui_hd_keycap_width(key.cap_px * 0.62f * float(std::wcslen(k.label)), k.h);
        key.x0 = x;
        key.y0 = 60;
        key.x1 = x + w;
        key.y1 = 60 + k.h;
        key.rgba[0] = key.rgba[1] = key.rgba[2] = 0x80;
        key.rgba[3] = k.alpha;
        set_clip(key.clip, 0, 0, 640, 200);
        c.order.push_back(UiHdOp{UiHdOp::Key, uint32_t(c.keys.size())});
        c.keys.push_back(key);
        Region r;
        r.name = "tecla:" + std::string(k.label[0] < 128 ? std::string(1, char(k.label[0])) : std::string("seta")) +
                 std::to_string(int(k.h));
        r.x0 = int(key.x0) - 2;
        r.y0 = int(key.y0) - 2;
        r.x1 = int(std::ceil(key.x1)) + 2;
        r.y1 = int(std::ceil(key.y1)) + 2;
        c.regions.push_back(r);
        x += w + 12;
    }
    // Uma tecla presa por um recorte e uma tecla entre dois textos (a ordem conta).
    UiHdText t;
    t.font = 2;
    t.cap_px = 14;
    const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    t.lines.push_back(hd_line(L"Press", 20, 70, 150, white));
    set_clip(t.clip, 0, 0, 640, 200);
    c.order.push_back(UiHdOp{UiHdOp::Text, uint32_t(c.texts.size())});
    c.texts.push_back(t);
    UiHdKey key;
    key.label = L"Enter";
    key.cap_px = 10;
    key.x0 = 76;
    key.y0 = 134;
    key.x1 = 130;
    key.y1 = 158;
    key.rgba[0] = key.rgba[1] = key.rgba[2] = key.rgba[3] = 0x80;
    set_clip(key.clip, 0, 0, 120, 200);
    c.order.push_back(UiHdOp{UiHdOp::Key, uint32_t(c.keys.size())});
    c.keys.push_back(key);
    return c;
}

// ---------------------------------------------------------------- SVG

Case svg_sheet(const Assets& a, const char* name, int box) {
    Case c;
    c.name = name;
    c.group = "svg";
    c.kind = Case::SvgSheet;
    const int cols = 8, pad = 8;
    const int cell = box + pad;
    const int n = int(a.files.size());
    c.width = cols * cell + pad;
    c.height = ((n + cols - 1) / cols) * cell + pad;
    for (int i = 0; i < n; ++i) {
        const int index = a.first_svg + i;
        // O tamanho pela proporção do viewBox (guardada por load_assets no retângulo da chave), no quadrado de box
        // pixels.
        int w = box, h = box;
        const float vw = a.config.images[size_t(index)].key.rect[2];
        const float vh = a.config.images[size_t(index)].key.rect[3];
        if (vw > 0 && vh > 0) {
            if (vw >= vh) h = std::max(4, int(std::lround(box * vh / vw)));
            else w = std::max(4, int(std::lround(box * vw / vh)));
        }
        const int x = pad + (i % cols) * cell, y = pad + (i / cols) * cell;
        c.assets.push_back(index);
        c.cells.push_back({x, y, w, h});
        Region r;
        r.name = "svg:" + a.files[size_t(i)].substr(4);
        r.x0 = x;
        r.y0 = y;
        r.x1 = x + w;
        r.y1 = y + h;
        c.regions.push_back(r);
    }
    return c;
}

void over(uint8_t* dst, float r, float g, float b, float a) {
    // dst (BGRA opaco) = src * a + dst * (1 - a), com src sem pré-multiplicar.
    dst[0] = uint8_t(std::clamp(std::lround(b * 255.0f * a + dst[0] * (1.0f - a)), 0L, 255L));
    dst[1] = uint8_t(std::clamp(std::lround(g * 255.0f * a + dst[1] * (1.0f - a)), 0L, 255L));
    dst[2] = uint8_t(std::clamp(std::lround(r * 255.0f * a + dst[2] * (1.0f - a)), 0L, 255L));
}

void composite(const UiRaster& r, int w, int h, std::vector<uint8_t>& bgra) {
    for (int y = 0; y < r.height; ++y) {
        const int dy = r.y + y;
        if (dy < 0 || dy >= h) continue;
        for (int x = 0; x < r.width; ++x) {
            const int dx = r.x + x;
            if (dx < 0 || dx >= w) continue;
            const uint8_t* s = r.bgra.data() + (size_t(y) * size_t(r.width) + size_t(x)) * 4;
            uint8_t* d = bgra.data() + (size_t(dy) * size_t(w) + size_t(dx)) * 4;
            // ONE, ONE_MINUS_SRC_ALPHA (o alfa pré-multiplicado, como o renderizador compõe).
            const uint32_t ia = 255u - s[3];
            for (int k = 0; k < 3; ++k) d[k] = uint8_t(std::min(255u, uint32_t(s[k]) + (uint32_t(d[k]) * ia + 127u) / 255u));
        }
    }
}

}  // namespace

Assets load_assets(const std::wstring& hd_ui_dir) {
    Assets a;
    const std::filesystem::path dir(hd_ui_dir);
    const std::string json = read_text(dir / "map.json");
    if (json.empty() || !ui_hd_parse_config(json, a.config, &a.error)) {
        if (a.error.empty()) a.error = "sem o map.json";
    }
    a.first_svg = int(a.config.images.size());
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir / "svg", ec)) {
        if (e.path().extension() == ".svg") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        UiHdImageEntry e;
        e.file = "svg/" + f.filename().string();
        e.type = UiHdAssetType::Svg;
        e.prompt_only = true;
        e.name = f.filename().string();
        // A proporção do viewBox no retângulo da chave (as folhas usam para o tamanho de cada célula).
        const std::string xml = read_text(f);
        const size_t vb = xml.find("viewBox=\"");
        if (vb != std::string::npos) {
            float v[4] = {0, 0, 0, 0};
            if (NF_SSCANF(xml.c_str() + vb + 9, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) {
                e.key.has_rect = true;
                for (int k = 0; k < 4; ++k) e.key.rect[k] = v[k];
            }
        }
        a.config.images.push_back(e);
        a.files.push_back(e.file);
    }
    return a;
}

std::vector<Case> build_cases(UiVectorCanvas& canvas, const Assets& assets) {
    std::vector<Case> cases;
    cases.push_back(shapes_case(plain_canvas(480, 360), "lista_formas"));
    cases.push_back(gradients_case());
    cases.push_back(texts_case());
    cases.push_back(glyphs_case());
    // A mesma lista das formas numa tela virtual escalada e deslocada (as posições fracionárias, as larguras escaladas).
    cases.push_back(shapes_case(plain_canvas(700, 520, 1.37f, 13.3f, 7.7f), "lista_escala"));
    {
        ScreenWorld world;
        const Canvas cv = make_canvas(1920, 1080);
        cases.push_back(options_screen(canvas, cv));
        cases.push_back(pause_screen(canvas, cv, false));
        cases.push_back(pause_screen(canvas, cv, true));
        cases.push_back(fail_screen(canvas, cv));
        cases.push_back(results_screen(canvas, cv));
        cases.push_back(front_screen(canvas, cv));
        cases.push_back(setup_screen(canvas, cv));
        cases.push_back(wheel_screen(canvas, cv));
    }
    cases.push_back(hd_texts_case());
    cases.push_back(hd_keys_case());
    if (!assets.files.empty()) {
        cases.push_back(svg_sheet(assets, "svg_pequeno", 40));
        cases.push_back(svg_sheet(assets, "svg_grande", 176));
    }
    return cases;
}

double render_case(UiVectorCanvas& canvas, const Case& c, std::vector<uint8_t>& bgra) {
    bgra.assign(size_t(c.width) * size_t(c.height) * 4, 255);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 0] = kBackground[2];
        bgra[i + 1] = kBackground[1];
        bgra[i + 2] = kBackground[0];
    }
    double ms = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto since = [&]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    if (c.kind == Case::Modern) {
        for (const ui::DrawList& l : c.lists) {
            UiRaster r;
            const auto a = std::chrono::steady_clock::now();
            canvas.draw_modern(l, c.canvas, c.width, c.height, r);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            composite(r, c.width, c.height, bgra);
        }
        return ms;
    }
    if (c.kind == Case::Hd) {
        std::vector<const UiHdText*> texts;
        std::vector<const UiHdKey*> keys;
        for (const UiHdText& t : c.texts) texts.push_back(&t);
        for (const UiHdKey& k : c.keys) keys.push_back(&k);
        UiRaster r;
        canvas.draw_hd(texts, keys, c.order, c.width, c.height, r);
        ms = since();
        composite(r, c.width, c.height, bgra);
        return ms;
    }
    for (size_t i = 0; i < c.assets.size(); ++i) {
        const auto& cell = c.cells[i];
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!canvas.asset(c.assets[i], cell[2], cell[3], rgba, w, h)) continue;
        for (int y = 0; y < std::min(h, cell[3]); ++y) {
            for (int x = 0; x < std::min(w, cell[2]); ++x) {
                const uint8_t* s = rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                uint8_t* d = bgra.data() + (size_t(cell[1] + y) * size_t(c.width) + size_t(cell[0] + x)) * 4;
                const float a = std::min(1.0f, s[3] / 128.0f);
                over(d, s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f, a);
            }
        }
    }
    return since();
}

std::vector<MeasureItem> measure_items() {
    const ui::Tokens& tk = ui::default_tokens();
    struct S {
        const char* name;
        ui::TextStyle st;
    };
    const S styles[] = {{"label", tk.type.label},       {"value", tk.type.value},   {"tab", tk.type.tab},
                        {"section", tk.type.section},   {"menu_big", tk.type.menu_big}, {"hero", tk.type.hero},
                        {"body", tk.type.body},         {"keycap", tk.type.keycap}, {"page_title", tk.type.page_title},
                        {"prompt", tk.type.prompt},     {"mono", tk.type.mono}};
    const wchar_t* texts[] = {L"Resolution", L"Options", L"Resume Mission", L"1920 \u00d7 1080", L"Frame rate limit",
                              L"Mission failed", L"Select Codename", L"W", L"Esc", L"Op\u00e7\u00f5es de v\u00eddeo",
                              L"Press the START button", L"Killed In Action", L"0123456789", L"iiiiiiii", L"WWWWWW"};
    std::vector<MeasureItem> out;
    for (const S& s : styles) {
        for (const wchar_t* t : texts) {
            MeasureItem m;
            std::string ascii;
            for (const wchar_t* p = t; *p != 0; ++p) ascii.push_back(*p < 128 && *p != ' ' ? char(*p) : '_');
            m.name = std::string(s.name) + ":" + ascii;
            m.text = t;
            m.style = s.st;
            m.tol = s.st.face == ui::Face::Mono ? 0.15f : 0.01f;
            out.push_back(m);
        }
    }
    const wchar_t* keys[] = {L"Esc", L"Enter", L"W", L"\u2191", L"Mouse", L"Space", L"Ctrl", L"LMB"};
    for (const float cap : {9.0f, 16.0f}) {
        for (const wchar_t* k : keys) {
            MeasureItem m;
            m.hd = true;
            m.cap = cap;
            m.text = k;
            m.tol = k[0] < 128 ? 0.25f : 0.0f;
            std::string ascii;
            for (const wchar_t* p = k; *p != 0; ++p) ascii.push_back(*p < 128 ? char(*p) : '^');
            m.name = "tecla" + std::to_string(int(cap)) + ":" + ascii;
            out.push_back(m);
        }
    }
    return out;
}

ui::DrawList pause_list(const Measure& m, const Canvas& cv) {
    ScreenWorld world;
    PauseScreen p;
    p.set_build("vetor");
    MmSnapshot s = pause_snapshot();
    p.open(s, PauseScreen::Tab::Pause);
    for (int i = 0; i < 40; ++i) p.frame(kDt, UiInput{}, s, cv, m);
    DrawList d;
    p.build(d, cv, m);
    menu_modern_debug_power_on();
    return d;
}

Case hd_case(bool keys) { return keys ? hd_keys_case() : hd_texts_case(); }

ui::DrawList timing_list(UiVectorCanvas& canvas, int w, int h, int variant) {
    ScreenWorld world;
    const Canvas cv = make_canvas(w, h);
    const Measure m = measure_of(canvas);
    PauseScreen p;
    p.set_build("vetor");
    MmSnapshot s = pause_snapshot();
    p.open(s, PauseScreen::Tab::Pause);
    for (int i = 0; i < 40; ++i) p.frame(kDt, UiInput{}, s, cv, m);
    DrawList d;
    // O primeiro comando de cada variante é outro (um ponto quase transparente num canto): nada vem do começo guardado.
    d.fill(Rect{cv.left + float(variant), cv.top, cv.left + float(variant) + 1.0f, cv.top + 1.0f}, Color{0, 0, 0, 0.004f});
    p.build(d, cv, m);
    menu_modern_debug_power_on();
    return d;
}

double timing_animated(UiVectorCanvas& canvas, int w, int h, int frames, size_t* changed) {
    ScreenWorld world;
    const Canvas cv = make_canvas(w, h);
    const Measure m = measure_of(canvas);
    PauseScreen p;
    p.set_build("vetor");
    MmSnapshot s = pause_snapshot();
    p.open(s, PauseScreen::Tab::Pause);
    for (int i = 0; i < 40; ++i) p.frame(kDt, UiInput{}, s, cv, m);
    UiRaster out;
    double ms = 0.0;
    uint64_t last = 0;
    size_t diff = 0;
    for (int i = 0; i < frames; ++i) {
        p.frame(kDt, UiInput{}, s, cv, m);
        DrawList d;
        p.build(d, cv, m);
        const auto t0 = std::chrono::steady_clock::now();
        canvas.draw_modern(d, cv, w, h, out);
        ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        diff += out.version != last ? 1 : 0;
        last = out.version;
    }
    menu_modern_debug_power_on();
    if (changed != nullptr) *changed = diff;
    return frames > 0 ? ms / frames : 0.0;
}

}  // namespace nf::vcase
