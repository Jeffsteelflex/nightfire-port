// A tela de entrada (o launcher, ui/modern_setup.h; docs/port.md "Instalação fácil"): quando ela aparece (a linha de
// comando, o [ui] launcher, o Shift, o disco), as linhas e os valores (as mesmas listas da página VIDEO do jogo), o PLAY
// com as opções e a gravação no nightfire.ini com as chaves das opções do jogo. Sem GPU, sem disco, com entradas
// sintéticas (a parte do disco está em test_disc.cpp).

#include <string>
#include <vector>

#include "engine/pc_text.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "test_framework.h"
#include "ui/modern_setup.h"

namespace {

using Row = nf::ui::SetupScreen::Row;
using Zone = nf::ui::SetupScreen::Zone;

// Um disco pronto, como o anfitrião monta.
nf::ui::SetupView ready_view() {
    nf::ui::SetupView v;
    v.ready = true;
    v.can_pack = true;
    v.disc = L"C:\\games\\007 - Nightfire (USA).iso";
    v.disc_kind = L"Disc image";
    v.disc_name = L"007 - Nightfire (USA).iso";
    v.status = L"Ready: 007: Nightfire (PS2, USA), SLUS-20579";
    v.pack_size = L"1.8 GB";
    return v;
}

// Um monitor de 1920 x 1080 com alguns modos (repetidos e fora de ordem, como o DXGI dá).
nf::ui::LauncherHardware monitor() {
    nf::ui::LauncherHardware hw;
    hw.modes = {{1920, 1080}, {800, 600}, {1280, 720}, {1600, 900}, {1280, 720}, {1024, 768}, {640, 480}};
    hw.desktop = {1920, 1080};
    return hw;
}

// Um monitor grande (7680 x 2160, um super ultrawide): dezenas de tamanhos, mais do que cabem na coluna.
nf::ui::LauncherHardware big_monitor() {
    nf::ui::LauncherHardware hw;
    const int sizes[][2] = {{640, 480},   {800, 600},   {1024, 768},  {1152, 864},  {1280, 720},  {1280, 768},
                            {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},  {1366, 768},  {1440, 900},
                            {1440, 1080}, {1600, 900},  {1600, 1024}, {1600, 1200}, {1680, 1050}, {1920, 1080},
                            {1920, 1200}, {1920, 1440}, {2048, 1536}, {2560, 1440}, {2560, 1600}, {3440, 1440},
                            {3840, 1080}, {3840, 1600}, {3840, 2160}, {5120, 1440}, {5120, 2160}, {7680, 2160}};
    for (const auto& sz : sizes) hw.modes.push_back({sz[0], sz[1]});
    hw.desktop = {7680, 2160};
    return hw;
}

struct Harness {
    nf::ui::SetupScreen s;
    nf::ui::Canvas cv = nf::ui::make_canvas(1920, 1080);
    nf::ui::Measure m;
    void open(const nf::ui::SetupView& v, const nf::ui::LauncherSettings& ls,
              const nf::ui::LauncherHardware& hw = monitor()) {
        s.open(v, ls, hw);
        // A primeira montagem engole o que estava apertado.
        s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
        s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    }
    void run(nf::ui::Nav c) {
        s.command(c);
        s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    }
    void go_to(Row r) {
        if (s.zone() == Zone::Menu) run(nf::ui::Nav::Right);
        for (int guard = 0; guard < 32 && s.focus_row() != r; ++guard) {
            run(s.focus() < s.row_of(r) ? nf::ui::Nav::Down : nf::ui::Nav::Up);
        }
    }
    void settle(float seconds) {
        for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    }
};

const wchar_t* const kExe = L"nightfire.exe";

}  // namespace

TEST(launcher_shows_by_the_rules) {
    using nf::ui::LauncherReason;
    nf::ui::LauncherStart s;
    s.disc_ok = true;
    // Uma partida normal (dois cliques no executável): a tela, com "Show at startup" ligado (o padrão).
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::Startup);
    // Desligada: o jogo começa direto; o Shift seguro a traz de volta.
    s.show_at_startup = false;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::None);
    s.shift = true;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::Shift);
    // Uma linha de comando (as capturas, --level, --disc...) pula a tela, também com o Shift e com ela ligada.
    s.command_line = true;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::None);
    s.show_at_startup = true;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::None);
    // Sem um disco que sirva ela aparece sempre (a tela de configuração de antes), e --setup a força.
    s.disc_ok = false;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::NoDisc);
    s.disc_ok = true;
    s.forced = true;
    CHECK(nf::ui::launcher_reason(s) == LauncherReason::Forced);
    CHECK(std::string(nf::ui::launcher_reason_name(LauncherReason::Shift)) == "shift");

    // O que conta como linha de comando.
    const auto cl = [](std::vector<const wchar_t*> args) {
        args.insert(args.begin(), kExe);
        return nf::ui::launcher_command_line(int(args.size()), args.data());
    };
    CHECK(!cl({}));
    CHECK(!cl({L"--config", L"out/x.ini", L"--save-dir", L"out/saves"}));
    CHECK(!cl({L"--setup"}) && !cl({L"--launcher"}));
    CHECK(!cl({L"--setup", L"--setup-pick", L"a.iso", L"--setup-input", L"none:1 enter:0.1"}));
    CHECK(cl({L"--level", L"07000012"}));
    CHECK(cl({L"--config", L"out/x.ini", L"--screenshot", L"out/a.png"}));
    CHECK(cl({L"--bench", L"10"}) && cl({L"--frames-dump", L"out/f"}) && cl({L"--movie-frame", L"48"}));
    CHECK(cl({L"--open-menu-page", L"4000004b"}) && cl({L"--disc", L"D:"}) && cl({L"--width", L"1280"}));
    // O valor de --config que parece um argumento não conta como um.
    CHECK(!cl({L"--config", L"--level"}));

    // [ui] launcher no nightfire.ini (ligado sem a chave).
    nf::Settings ini;
    ini.parse("[ui]\nhd = off\n");
    CHECK(nf::ui::launcher_enabled(ini));
    ini.parse("[ui]\nlauncher = off\n");
    CHECK(!nf::ui::launcher_enabled(ini));
    ini.parse("[ui]\nlauncher = on\n");
    CHECK(nf::ui::launcher_enabled(ini));
}

TEST(launcher_opens_on_play_and_enter_plays) {
    PcText_SetLanguage("en");
    Harness h;
    nf::ui::LauncherSettings ls;
    h.open(ready_view(), ls);
    // PLAY com o foco: Enter (Select) joga na hora, sem nada mudado.
    CHECK(h.s.zone() == Zone::Menu && h.s.focus_row() == Row::Play);
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.take_request().action == nf::ui::SetupAction::Play);
    CHECK(!h.s.settings_changed());
    // O Start do controle joga de qualquer linha.
    h.go_to(Row::VSync);
    CHECK(h.s.zone() == Zone::Rows);
    h.run(nf::ui::Nav::Start);
    CHECK(h.s.take_request().action == nf::ui::SetupAction::Play);
    // As linhas, em ordem: o disco, a cópia, a tela, a interface, a partida.
    std::vector<Row> rows;
    for (int i = 0; i < h.s.row_count(); ++i) {
        if (h.s.row_kind(i) != Row::Section) rows.push_back(h.s.row_kind(i));
    }
    const std::vector<Row> want = {Row::Disc, Row::Pack, Row::Resolution, Row::WindowMode, Row::VSync,
                                   Row::MenuStyle, Row::HdUi, Row::ShowAtStartup};
    CHECK(rows == want);
    // Tudo cabe na área da lista sem leitores (as quatro seções e as oito linhas).
    nf::ui::DrawList dl;
    h.s.build(dl, h.cv, h.m);
    CHECK(h.s.row_rect(h.s.row_of(Row::ShowAtStartup)).y1 <= nf::ui::default_tokens().space.list_bottom);
    CHECK(dl.find_text(L"Show at startup") != nullptr && dl.find_text(L"HD 2D layer") != nullptr);
}

TEST(launcher_rows_change_the_values) {
    PcText_SetLanguage("en");
    Harness h;
    nf::ui::LauncherSettings ls;
    ls.video.width = 1600;
    ls.video.height = 900;
    h.open(ready_view(), ls);
    // A resolução: as do monitor que cabem nele, em ordem, sem as repetidas (as mesmas da página VIDEO).
    const std::vector<std::wstring> res = h.s.choice_labels(Row::Resolution);
    CHECK(res.size() == 6 && res.front() == L"640 × 480" && res.back() == L"1920 × 1080");
    CHECK(h.s.value_text(Row::Resolution) == L"1600 × 900");
    h.go_to(Row::Resolution);
    h.run(nf::ui::Nav::Right);
    CHECK(h.s.settings().video.width == 1920 && h.s.settings().video.height == 1080 && h.s.settings_changed());
    h.run(nf::ui::Nav::Left);
    h.run(nf::ui::Nav::Left);
    CHECK(h.s.settings().video.width == 1280 && h.s.settings().video.height == 720);
    // A lista aberta: Select abre com o foco na escolhida, Down e Select trocam, a lista fecha.
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.open_dropdown() == h.s.row_of(Row::Resolution) && h.s.dropdown_focus() == 3);
    h.run(nf::ui::Nav::Down);
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.open_dropdown() < 0 && h.s.settings().video.width == 1600);
    // Voltar com a lista aberta só a fecha.
    h.run(nf::ui::Nav::Select);
    h.run(nf::ui::Nav::Back);
    CHECK(h.s.open_dropdown() < 0 && h.s.zone() == Zone::Rows && h.s.settings().video.width == 1600);
    // O modo de tela: sem bordas mostra o monitor; exclusiva com um tamanho que o monitor não tem vai para o dele.
    h.go_to(Row::WindowMode);
    h.run(nf::ui::Nav::Right);
    CHECK(h.s.settings().video.window_mode == nf::WindowMode::Borderless);
    CHECK(h.s.choice_labels(Row::Resolution).size() == 1 && h.s.value_text(Row::Resolution) == L"1920x1080 (desktop)");
    h.run(nf::ui::Nav::Right);
    CHECK(h.s.settings().video.window_mode == nf::WindowMode::Exclusive && h.s.settings().video.width == 1600);
    h.run(nf::ui::Nav::Right);  // dá a volta
    CHECK(h.s.settings().video.window_mode == nf::WindowMode::Windowed);
    // As chaves: esquerda desliga, direita liga, Select alterna.
    h.go_to(Row::VSync);
    h.run(nf::ui::Nav::Left);
    CHECK(!h.s.settings().video.vsync && h.s.value_text(Row::VSync) == L"Off");
    h.go_to(Row::HdUi);
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.settings().video.hd_ui);
    h.go_to(Row::ShowAtStartup);
    h.run(nf::ui::Nav::Select);
    CHECK(!h.s.settings().show_at_startup);
    // O estilo dos menus pela lista.
    h.go_to(Row::MenuStyle);
    h.run(nf::ui::Nav::Select);
    h.run(nf::ui::Nav::Down);
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.settings().video.menu_style == nf::MenuStyle::Original && h.s.value_text(Row::MenuStyle) == L"Original");
    // A descrição da camada HD diz que os menus Modern são sempre nativos; a linha com o foco e o aviso das mudanças.
    h.go_to(Row::HdUi);
    h.settle(0.3f);
    nf::ui::DrawList dl;
    h.s.build(dl, h.cv, h.m);
    // (Os textos da descrição vêm quebrados em linhas: juntos de novo com espaços.)
    std::wstring all;
    for (const nf::ui::DrawCmd& c : dl.cmds) all += c.text + L" ";
    CHECK(all.find(L"always drawn at native") != std::wstring::npos);
    CHECK(all.find(L"saved when you press Play") != std::wstring::npos);
    // QUIT não devolve as opções: só o PLAY (o anfitrião grava o que settings() diz).
    h.run(nf::ui::Nav::Back);
    CHECK(h.s.zone() == Zone::Menu && h.s.focus_row() == Row::Play);
    h.run(nf::ui::Nav::Select);
    CHECK(h.s.take_request().action == nf::ui::SetupAction::Play);
    const std::string d = h.s.describe();
    CHECK(d.find("modo windowed vsync 0 estilo original hd 1 mostrar 0 mudou 1") != std::string::npos);
}

TEST(launcher_play_stores_the_game_options_keys) {
    nf::ui::LauncherSettings before;
    before.video.width = 1600;
    before.video.height = 900;
    nf::ui::LauncherSettings now = before;
    // Nada mudou: nada é gravado (nem um padrão por causa de outra opção).
    nf::Settings ini;
    ini.parse("; as minhas opções\n[video]\nfov = 70\n[controls]\nmouse_sensitivity = 0.003\n");
    const std::string untouched = ini.text();
    CHECK(!nf::ui::store_launcher_settings(now, before, ini) && ini.text() == untouched);
    // Só "Show at startup": só [ui] launcher.
    now.show_at_startup = false;
    CHECK(nf::ui::store_launcher_settings(now, before, ini));
    CHECK(ini.get("ui", "launcher") == "0" && !ini.has("video", "width") && !ini.has("ui", "menu_style"));
    CHECK(!nf::ui::launcher_enabled(ini));
    // A resolução, o modo, o VSync, o estilo e a camada HD: as chaves das opções do jogo, que load_video_settings lê de volta.
    now.video.width = 1920;
    now.video.height = 1080;
    now.video.window_mode = nf::WindowMode::Exclusive;
    now.video.vsync = false;
    now.video.menu_style = nf::MenuStyle::Original;
    now.video.hd_ui = true;
    ini.parse("[video]\nfullscreen = 1\nfov = 70\n");
    CHECK(nf::ui::store_launcher_settings(now, before, ini));
    CHECK(!ini.has("video", "fullscreen") && ini.get("video", "window_mode") == "exclusive");
    nf::VideoSettings back;
    nf::load_video_settings(ini, back);
    CHECK(back.width == 1920 && back.height == 1080 && back.window_mode == nf::WindowMode::Exclusive && !back.vsync);
    CHECK(back.menu_style == nf::MenuStyle::Original && back.hd_ui && back.fov == 70.0f);
    // As mesmas chaves que a página VIDEO grava (store_video_settings): o que ela escreve, a tela de entrada lê igual.
    nf::Settings game;
    nf::store_video_settings(now.video, game);
    for (const char* key : {"width", "height", "window_mode", "vsync"}) CHECK(game.get("video", key) == ini.get("video", key));
    for (const char* key : {"menu_style", "hd"}) CHECK(game.get("ui", key) == ini.get("ui", key));
    // O resto das opções de vídeo não muda (o FOV do arquivo continua 70, nada de render_scale novo).
    CHECK(ini.get("video", "fov") == "70" && !ini.has("video", "render_scale"));
}

TEST(launcher_mouse_opens_the_list_and_picks) {
    PcText_SetLanguage("en");
    Harness h;
    nf::ui::LauncherSettings ls;
    ls.video.width = 1280;
    ls.video.height = 720;
    h.open(ready_view(), ls);
    nf::ui::UiInput in;
    const auto click = [&](const nf::ui::Rect& r) {
        in.mouse_inside = true;
        in.mouse_x = nf::ui::window_x(h.cv, r.cx());
        in.mouse_y = nf::ui::window_y(h.cv, r.cy());
        in.lmb = false;
        h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
        in.lmb = true;
        h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
        in.lmb = false;
        h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    };
    // O clique na linha da resolução abre a lista; o clique numa opção escolhe e fecha.
    click(h.s.row_rect(h.s.row_of(Row::Resolution)));
    CHECK(h.s.zone() == Zone::Rows && h.s.open_dropdown() == h.s.row_of(Row::Resolution));
    for (int i = 0; i < 20; ++i) h.s.frame(1.0f / 60.0f, in, h.cv, h.m);  // a lista termina de abrir
    click(h.s.option_rect(3));  // 640, 800, 1024, 1280, ... -> a quarta: 1280 x 720
    CHECK(h.s.open_dropdown() < 0 && h.s.settings().video.width == 1280);
    click(h.s.row_rect(h.s.row_of(Row::Resolution)));
    for (int i = 0; i < 20; ++i) h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    click(h.s.option_rect(4));
    CHECK(h.s.settings().video.width == 1600 && h.s.settings().video.height == 900);
    // A chave pelo clique (depois de a lista terminar de fechar: as linhas de baixo sobem).
    for (int i = 0; i < 20; ++i) h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    click(h.s.row_rect(h.s.row_of(Row::HdUi)));
    CHECK(h.s.settings().video.hd_ui);
    // O botão direito volta para PLAY; o clique em PLAY joga.
    in.back_button = true;
    h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    in.back_button = false;
    h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    CHECK(h.s.zone() == Zone::Menu);
    click(h.s.menu_rect(0));
    CHECK(h.s.take_request().action == nf::ui::SetupAction::Play);
    click(h.s.menu_rect(1));
    CHECK(h.s.take_request().action == nf::ui::SetupAction::Quit);
}

TEST(launcher_long_list_opens_around_the_value_and_the_wheel_scrolls_it) {
    // As resoluções de um monitor grande: a lista abria com a atual rente à borda de baixo (parecia acabar nela) e a
    // roda não rolava com o mouse em cima das opções.
    PcText_SetLanguage("en");
    Harness h;
    nf::ui::LauncherSettings ls;
    ls.video.width = 1920;
    ls.video.height = 1080;
    h.open(ready_view(), ls, big_monitor());
    const auto& sp = nf::ui::default_tokens().space;
    const std::vector<std::wstring> res = h.s.choice_labels(Row::Resolution);
    CHECK(res.size() == 30 && res.back() == L"7680 × 2160");
    h.go_to(Row::Resolution);
    h.run(nf::ui::Nav::Select);
    h.settle(0.6f);
    const int cur = h.s.dropdown_focus();
    CHECK(h.s.open_dropdown() == h.s.row_of(Row::Resolution) && res[size_t(cur)] == L"1920 × 1080");
    const auto visible = [&](int o) {
        const nf::ui::Rect r = h.s.option_rect(o);
        return r.y0 >= sp.list_top - 0.5f && r.y1 <= sp.list_bottom + 0.5f;
    };
    // A atual no meio da coluna, com opções acima e abaixo à vista.
    CHECK(visible(cur - 3) && visible(cur) && visible(cur + 3));
    CHECK_NEAR(h.s.option_rect(cur).cy(), (sp.list_top + sp.list_bottom) * 0.5f, sp.launch_option_h);
    CHECK(!visible(int(res.size()) - 1));
    // A roda com o mouse em cima das opções rola a lista aberta até o maior tamanho.
    nf::ui::UiInput in;
    in.mouse_inside = true;
    in.mouse_x = nf::ui::window_x(h.cv, h.s.option_rect(cur).cx());
    in.mouse_y = nf::ui::window_y(h.cv, h.s.option_rect(cur).cy());
    h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    const float before = h.s.scroll();
    bool saw_last = false;
    in.wheel = -1.0f;
    for (int i = 0; i < 40; ++i) {
        h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
        saw_last = saw_last || visible(int(res.size()) - 1);
    }
    in.wheel = 0.0f;
    for (int i = 0; i < 60; ++i) {
        h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
        saw_last = saw_last || visible(int(res.size()) - 1);
    }
    CHECK(h.s.scroll() > before && saw_last);
    CHECK(h.s.open_dropdown() == h.s.row_of(Row::Resolution));
    // E volta para cima.
    in.wheel = 1.0f;
    for (int i = 0; i < 60; ++i) h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    in.wheel = 0.0f;
    for (int i = 0; i < 60; ++i) h.s.frame(1.0f / 60.0f, in, h.cv, h.m);
    CHECK(h.s.scroll() == 0.0f && visible(0));
    // Uma lista curta (o modo de tela) abre inteira, com a linha dela.
    h.run(nf::ui::Nav::Back);
    h.go_to(Row::WindowMode);
    h.run(nf::ui::Nav::Select);
    h.settle(0.6f);
    CHECK(h.s.open_dropdown() == h.s.row_of(Row::WindowMode));
    CHECK(h.s.row_rect(h.s.row_of(Row::WindowMode)).y0 >= sp.list_top - 0.5f && visible(0) && visible(2));
}

TEST(launcher_portuguese_texts) {
    PcText_SetLanguage("pt");
    Harness h;
    h.open(ready_view(), nf::ui::LauncherSettings{});
    h.go_to(Row::ShowAtStartup);
    h.settle(0.3f);
    nf::ui::DrawList dl;
    h.s.build(dl, h.cv, h.m);
    CHECK(dl.find_text(L"Mostrar ao abrir") != nullptr && dl.find_text(L"Camada 2D em HD") != nullptr);
    std::wstring all;
    for (const nf::ui::DrawCmd& c : dl.cmds) all += c.text + L" ";
    CHECK(all.find(L"segure Shift") != std::wstring::npos);
    PcText_SetLanguage("en");
}
