// A tela de entrada (o launcher) do nightfire.exe. Ver modern_setup.h e docs/design.md.

#include "ui/modern_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include "engine/pc_text.h"
#include "platform/settings.h"

namespace nf::ui {

namespace {

std::wstring T(PcText id) { return widen(PcText_Utf8(id)); }

// Um texto do port com um %s.
std::wstring Tf(PcText id, const std::wstring& arg) {
    const std::wstring fmt = T(id);
    const size_t at = fmt.find(L"%s");
    if (at == std::wstring::npos) return fmt;
    return fmt.substr(0, at) + arg + fmt.substr(at + 2);
}

void line_at(DrawList& dl, float x, float cy, const std::wstring& s, const TextStyle& st, const Color& c, Align a = Align::Left,
             float w = 4000.0f) {
    const float h = st.size * 1.6f;
    Rect r{x, cy - h * 0.5f, x + w, cy + h * 0.5f};
    if (a == Align::Right) r = Rect{x - w, cy - h * 0.5f, x, cy + h * 0.5f};
    if (a == Align::Centre) r = Rect{x - w * 0.5f, cy - h * 0.5f, x + w * 0.5f, cy + h * 0.5f};
    dl.text(r, st.caps ? to_upper(s) : s, st, c, a);
}

// Um caminho longo na largura: o começo, "…" e o fim (o nome do arquivo fica inteiro).
std::wstring elide_middle(const Frame& f, const std::wstring& s, const TextStyle& st, float width) {
    if (text_width(f, s, st) <= width) return s;
    size_t keep_tail = s.size() - std::min(s.size(), s.find_last_of(L"\\/") == std::wstring::npos ? s.size() : s.find_last_of(L"\\/"));
    for (size_t head = std::min<size_t>(s.size(), 24); head > 2; --head) {
        const size_t tail = std::min(keep_tail, s.size() - head);
        const std::wstring t = s.substr(0, head) + L"…" + s.substr(s.size() - tail);
        if (text_width(f, t, st) <= width) return t;
    }
    // Nem o nome cabe: só o fim.
    for (size_t tail = s.size(); tail > 4; --tail) {
        const std::wstring t = L"…" + s.substr(s.size() - tail);
        if (text_width(f, t, st) <= width) return t;
    }
    return s;
}

// A letra da unidade sem a barra ("D:\" -> "D:").
std::wstring drive_name(const std::wstring& root) {
    std::wstring d = root;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    return d;
}

bool is_dropdown(SetupScreen::Row r) {
    return r == SetupScreen::Row::Resolution || r == SetupScreen::Row::WindowMode || r == SetupScreen::Row::MenuStyle;
}
bool is_toggle(SetupScreen::Row r) {
    return r == SetupScreen::Row::VSync || r == SetupScreen::Row::HdUi || r == SetupScreen::Row::ShowAtStartup;
}

// As ações da barra de baixo (o clique).
enum : int { kActSelect = 1, kActBack, kActPlay };

const char* row_name(SetupScreen::Row r) {
    switch (r) {
        case SetupScreen::Row::Section: return "secao";
        case SetupScreen::Row::Disc: return "disco";
        case SetupScreen::Row::Drive: return "leitor";
        case SetupScreen::Row::Pack: return "copia";
        case SetupScreen::Row::Resolution: return "resolucao";
        case SetupScreen::Row::WindowMode: return "modo";
        case SetupScreen::Row::VSync: return "vsync";
        case SetupScreen::Row::MenuStyle: return "estilo";
        case SetupScreen::Row::HdUi: return "hd";
        case SetupScreen::Row::ShowAtStartup: return "mostrar";
        case SetupScreen::Row::Play: return "jogar";
        case SetupScreen::Row::Quit: return "sair";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------- quando aparece

LauncherReason launcher_reason(const LauncherStart& s) {
    if (s.forced) return LauncherReason::Forced;
    if (!s.disc_ok) return LauncherReason::NoDisc;
    if (s.command_line) return LauncherReason::None;
    if (s.show_at_startup) return LauncherReason::Startup;
    if (s.shift) return LauncherReason::Shift;
    return LauncherReason::None;
}

const char* launcher_reason_name(LauncherReason r) {
    switch (r) {
        case LauncherReason::None: return "nenhum";
        case LauncherReason::Forced: return "forcada";
        case LauncherReason::NoDisc: return "sem_disco";
        case LauncherReason::Startup: return "partida";
        case LauncherReason::Shift: return "shift";
    }
    return "?";
}

bool launcher_command_line(int argc, const wchar_t* const* argv) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i] != nullptr ? argv[i] : L"";
        if (a == L"--setup" || a == L"--launcher") continue;
        if (a == L"--config" || a == L"--save-dir" || a == L"--setup-pick" || a == L"--setup-input") {
            ++i;  // o valor
            continue;
        }
        return true;
    }
    return false;
}

bool launcher_enabled(const Settings& ini) { return ini.get_bool("ui", "launcher", true); }

bool store_launcher_settings(const LauncherSettings& now, const LauncherSettings& before, Settings& ini) {
    // As chaves e a escrita de store_video_settings (as das opções do jogo), só as das linhas da tela e só as que mudaram:
    // um valor padrão não vai para o arquivo por causa de outra opção.
    const VideoSettings& a = now.video;
    const VideoSettings& b = before.video;
    bool wrote = false;
    if (a.width != b.width || a.height != b.height) {
        ini.set_int("video", "width", a.width);
        ini.set_int("video", "height", a.height);
        wrote = true;
    }
    if (a.window_mode != b.window_mode) {
        ini.set("video", "window_mode", window_mode_name(a.window_mode));
        ini.remove("video", "fullscreen");  // a chave antiga, substituída por window_mode
        wrote = true;
    }
    if (a.vsync != b.vsync) {
        ini.set_bool("video", "vsync", a.vsync);
        wrote = true;
    }
    if (a.menu_style != b.menu_style) {
        ini.set("ui", "menu_style", menu_style_name(a.menu_style));
        wrote = true;
    }
    if (a.hd_ui != b.hd_ui) {
        ini.set_bool("ui", "hd", a.hd_ui);
        wrote = true;
    }
    if (now.show_at_startup != before.show_at_startup) {
        ini.set_bool("ui", "launcher", now.show_at_startup);
        wrote = true;
    }
    return wrote;
}

// ---------------------------------------------------------------- a tela

SetupScreen::SetupScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

void SetupScreen::open(const SetupView& v, const LauncherSettings& s, const LauncherHardware& hw) {
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.front_in, tk_->motion.ease_out);
    menu_ = BigMenu{};
    mark_ = FocusMark{};
    bar_ = PromptBar{};
    dialog_ = Dialog{};
    dialog_progress_ = false;
    toast_ = Toast{};
    nav_.reset();
    for (bool& p : pending_) p = false;
    request_ = SetupRequest{};
    settings_ = s;
    opened_ = s;
    hw_ = hw;
    rows_.clear();
    focus_ = -1;
    dd_row_ = -1;
    dd_closing_ = -1;
    dd_open_.snap(0.0f);
    scroll_.snap(0.0f);
    scroll_target_ = 0.0f;
    menu_.items = {BigMenu::Item{T(PcText::SuPlay), T(PcText::SuPlaySub), false, false},
                   BigMenu::Item{T(PcText::SuQuit), std::wstring(), true, false}};
    menu_.x = tk_->space.left_x;
    menu_.y = tk_->space.launch_menu_y;
    set_view(v);
    // Com um disco que serve, PLAY; sem, a linha do disco (escolher a imagem).
    if (v.ready) {
        set_zone_menu(0, false);
    } else {
        zone_ = Zone::Rows;
        set_row_focus(row_of(Row::Disc), false);
    }
    // Os sublinhados vão para o lugar na primeira montagem (com a medida de texto do renderizador).
    menu_.mark.placed = false;
    mark_.placed = false;
}

void SetupScreen::set_rows() {
    // O foco fica na mesma linha (pelo tipo e o leitor) quando a lista muda.
    RowInfo keep{Row::Disc};
    if (focus_ >= 0 && size_t(focus_) < rows_.size()) keep = rows_[size_t(focus_)];
    rows_.clear();
    const auto add = [&](Row kind, PcText label, int drive = -1) {
        RowInfo r;
        r.kind = kind;
        r.label = label;
        r.drive = drive;
        rows_.push_back(r);
    };
    add(Row::Section, PcText::LnSecDisc);
    add(Row::Disc, PcText::LnDisc);
    for (size_t i = 0; i < view_.drives.size(); ++i) add(Row::Drive, PcText::LnDrive, int(i));
    add(Row::Pack, PcText::LnPack);
    add(Row::Section, PcText::LnSecDisplay);
    add(Row::Resolution, PcText::Resolution);
    add(Row::WindowMode, PcText::WindowMode);
    add(Row::VSync, PcText::VSync);
    add(Row::Section, PcText::LnSecInterface);
    add(Row::MenuStyle, PcText::MenuStyle);
    add(Row::HdUi, PcText::LnHdUi);
    add(Row::Section, PcText::LnSecStartup);
    add(Row::ShowAtStartup, PcText::LnShowAtStartup);
    anims_.assign(rows_.size(), RowAnim{});
    // Uma lista aberta fecha (os índices mudaram).
    dd_row_ = -1;
    dd_closing_ = -1;
    dd_open_.snap(0.0f);
    int focus = -1;
    for (size_t i = 0; i < rows_.size() && focus < 0; ++i) {
        if (rows_[i].kind == keep.kind && (keep.kind != Row::Drive || rows_[i].drive == keep.drive)) focus = int(i);
    }
    if (focus < 0) focus = row_of(Row::Disc);
    focus_ = focus;
    layout_rows();
    mark_.placed = false;
}

void SetupScreen::set_view(const SetupView& v) {
    const bool rows_change = v.drives.size() != view_.drives.size() || rows_.empty() || [&] {
        for (size_t i = 0; i < v.drives.size(); ++i) {
            if (v.drives[i].root != view_.drives[i].root || v.drives[i].usable != view_.drives[i].usable) return true;
        }
        return false;
    }();
    view_ = v;
    menu_.items.resize(2);
    menu_.items[0].locked = !view_.ready;
    if (rows_change) set_rows();
}

int SetupScreen::row_of(Row r) const {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind == r) return int(i);
    }
    return -1;
}

bool SetupScreen::row_locked(int i) const {
    if (i < 0 || size_t(i) >= rows_.size()) return false;
    const RowInfo& r = rows_[size_t(i)];
    if (r.kind == Row::Drive) return !view_.drives[size_t(r.drive)].usable;
    if (r.kind == Row::Pack) return !view_.can_pack;
    return false;
}

SetupScreen::Row SetupScreen::focus_row() const {
    if (zone_ == Zone::Menu) return menu_.focus == 0 ? Row::Play : Row::Quit;
    return focus_ >= 0 && size_t(focus_) < rows_.size() ? rows_[size_t(focus_)].kind : Row::Section;
}

// ---------------------------------------------------------------- a lista do meio

void SetupScreen::layout_rows() {
    const auto& sp = tk_->space;
    float y = 0.0f;
    const int anim_row = dd_row_ >= 0 ? dd_row_ : dd_closing_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        RowInfo& r = rows_[i];
        r.h = r.kind == Row::Section ? sp.launch_section_h : sp.launch_row_h;
        r.y = y;
        y += r.h;
        if (int(i) == anim_row) y += sp.launch_option_h * float(choice_labels(r.kind).size()) * dd_open_.value();
    }
}

bool SetupScreen::focusable(int i) const { return i >= 0 && size_t(i) < rows_.size() && rows_[size_t(i)].kind != Row::Section; }

int SetupScreen::first_focusable() const {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (focusable(int(i))) return int(i);
    }
    return -1;
}

int SetupScreen::step_focus(int from, int dir) const {
    for (int i = from + dir; i >= 0 && size_t(i) < rows_.size(); i += dir) {
        if (focusable(i)) return i;
    }
    return -1;
}

float SetupScreen::content_height() const {
    if (rows_.empty()) return 0.0f;
    const RowInfo& last = rows_.back();
    float h = last.y + last.h;
    const int anim_row = dd_row_ >= 0 ? dd_row_ : dd_closing_;
    if (anim_row == int(rows_.size()) - 1) {
        h += tk_->space.launch_option_h * float(choice_labels(last.kind).size()) * dd_open_.value();
    }
    return h;
}

float SetupScreen::view_height() const { return tk_->space.list_bottom - tk_->space.list_top; }

Rect SetupScreen::row_rect(int i) const {
    if (i < 0 || size_t(i) >= rows_.size()) return Rect{};
    const auto& sp = tk_->space;
    const float y = sp.list_top + rows_[size_t(i)].y - scroll_.value();
    return Rect{sp.mid_x0, y, sp.mid_x1, y + rows_[size_t(i)].h};
}

Rect SetupScreen::option_rect(int option) const {
    const int at = dd_row_ >= 0 ? dd_row_ : dd_closing_;
    if (at < 0) return Rect{};
    const auto& sp = tk_->space;
    const Rect r = row_rect(at);
    const float y = r.y1 + sp.launch_option_h * float(option);
    return Rect{sp.mid_x0 + sp.option_indent, y, sp.mid_x1, y + sp.launch_option_h};
}

Rect SetupScreen::underline_of(int i) const {
    // Em coordenadas da lista (y sem a rolagem e sem o topo).
    const auto& sp = tk_->space;
    if (i < 0 || size_t(i) >= rows_.size()) return Rect{};
    const RowInfo& r = rows_[size_t(i)];
    if (i == dd_row_) {
        const float y = r.y + r.h + sp.launch_option_h * float(dd_focus_ + 1);
        return Rect{sp.mid_x0 + sp.option_indent, y, sp.mid_x1 - 8.0f, y};
    }
    return Rect{sp.mid_x0, r.y + r.h, sp.mid_x1 - 8.0f, r.y + r.h};
}

void SetupScreen::place_mark(bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (zone_ != Zone::Rows || focus_ < 0 || dialog_.open) {
        mark_.show(false, f);
        return;
    }
    const Rect u = underline_of(focus_);
    mark_.move_to(u.x0, u.x1, u.y1 - 1.0f, f, animate);
    mark_.show(true, f);
}

void SetupScreen::ensure_visible(int i) {
    if (i < 0 || size_t(i) >= rows_.size()) return;
    const auto& sp = tk_->space;
    const RowInfo& r = rows_[size_t(i)];
    float y0 = r.y, y1 = r.y + r.h;
    if (i == dd_row_) {
        y0 = r.y + r.h + sp.launch_option_h * float(dd_focus_);
        y1 = y0 + sp.launch_option_h;
    }
    // A seção logo acima vem junto quando é a primeira linha dela.
    if (i > 0 && rows_[size_t(i) - 1].kind == Row::Section && i != dd_row_) y0 = rows_[size_t(i) - 1].y;
    const float view = view_height();
    const float margin = 20.0f;
    float t = scroll_target_;
    if (y0 - margin < t) t = y0 - margin;
    if (y1 + margin > t + view) t = y1 + margin - view;
    float total = content_height();
    if (dd_row_ >= 0) total += sp.launch_option_h * float(choice_labels(rows_[size_t(dd_row_)].kind).size()) * (1.0f - dd_open_.value());
    t = std::clamp(t, 0.0f, std::max(0.0f, total - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void SetupScreen::reveal_open_list() {
    // A lista que cabe aparece inteira, com a linha dela. A que não cabe (as resoluções de um monitor grande: dezenas)
    // abre com a opção atual no meio, com opções acima e abaixo à vista: com a atual rente à borda de baixo, a lista
    // parecia acabar nela.
    if (dd_row_ < 0) return;
    const auto& sp = tk_->space;
    const RowInfo& r = rows_[size_t(dd_row_)];
    const int n = int(choice_labels(r.kind).size());
    const float view = view_height();
    const float margin = 20.0f;
    const float y0 = r.y;
    const float y1 = r.y + r.h + sp.launch_option_h * float(n);
    float t = scroll_target_;
    if (y1 - y0 + 2.0f * margin <= view) {
        if (y0 - margin < t) t = y0 - margin;
        if (y1 + margin > t + view) t = y1 + margin - view;
    } else {
        t = r.y + r.h + sp.launch_option_h * (float(dd_focus_) + 0.5f) - view * 0.5f;
    }
    const float total = content_height() + sp.launch_option_h * float(n) * (1.0f - dd_open_.value());
    t = std::clamp(t, 0.0f, std::max(0.0f, total - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void SetupScreen::set_zone_menu(int item, bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    zone_ = Zone::Menu;
    menu_.focused = !dialog_.open;
    menu_.set_focus(std::clamp(item, 0, 1), f, animate && menu_.mark.placed);
    place_mark(animate);
}

void SetupScreen::set_row_focus(int i, bool animate) {
    if (!focusable(i)) i = first_focusable();
    if (i < 0) return;
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const bool from_menu = zone_ != Zone::Rows;
    zone_ = Zone::Rows;
    focus_ = i;
    menu_.focused = false;
    menu_.mark.show(false, f);
    ensure_visible(i);
    place_mark(animate && !from_menu);
}

void SetupScreen::focus_play() {
    close_dropdown();
    set_zone_menu(0, false);
    menu_.mark.placed = false;  // o sublinhado vai para lá na próxima montagem
}

// ---------------------------------------------------------------- o modelo

std::vector<DisplayMode> SetupScreen::resolutions() const {
    const VideoSettings& v = settings_.video;
    const DisplayMode cur{v.width, v.height};
    std::vector<DisplayMode> modes = resolution_choices(hw_.modes, hw_.desktop, v.window_mode, cur);
    if (modes.empty()) modes.push_back(cur);
    return modes;
}

std::vector<std::wstring> SetupScreen::choice_labels(Row r) const {
    std::vector<std::wstring> out;
    const VideoSettings& v = settings_.video;
    switch (r) {
        case Row::Resolution:
            for (const DisplayMode& m : resolutions()) {
                char buf[64];
                if (v.window_mode == WindowMode::Borderless && hw_.desktop.width > 0) {
                    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::Desktop), hw_.desktop.width, hw_.desktop.height);
                } else {
                    std::snprintf(buf, sizeof(buf), "%d \xC3\x97 %d", m.width, m.height);
                }
                out.push_back(widen(buf));
            }
            break;
        case Row::WindowMode: out = {T(PcText::ModeWindowed), T(PcText::ModeBorderless), T(PcText::ModeExclusive)}; break;
        case Row::MenuStyle: out = {T(PcText::StyleModern), T(PcText::StyleOriginal)}; break;
        default: break;
    }
    return out;
}

int SetupScreen::choice_index(Row r) const {
    const VideoSettings& v = settings_.video;
    switch (r) {
        case Row::Resolution: {
            const std::vector<DisplayMode> modes = resolutions();
            for (size_t i = 0; i < modes.size(); ++i) {
                if (modes[i] == DisplayMode{v.width, v.height}) return int(i);
            }
            return 0;
        }
        case Row::WindowMode: return std::clamp(int(v.window_mode), 0, 2);
        case Row::MenuStyle: return v.menu_style == MenuStyle::Original ? 1 : 0;
        default: return 0;
    }
}

void SetupScreen::set_choice(Row r, int i) {
    const int n = int(choice_labels(r).size());
    if (n <= 0) return;
    i = std::clamp(i, 0, n - 1);
    VideoSettings& v = settings_.video;
    switch (r) {
        case Row::Resolution: {
            const std::vector<DisplayMode> modes = resolutions();
            if (size_t(i) < modes.size()) {
                v.width = modes[size_t(i)].width;
                v.height = modes[size_t(i)].height;
            }
            break;
        }
        case Row::WindowMode:
            v.window_mode = WindowMode(i);
            // Como as duas páginas de opções: numa janela maior que o monitor, a maior que cabe; na tela cheia exclusiva,
            // um modo do monitor.
            fit_resolution_to_mode(v, hw_.modes, hw_.desktop);
            break;
        case Row::MenuStyle: v.menu_style = i == 1 ? MenuStyle::Original : MenuStyle::Modern; break;
        default: break;
    }
}

bool SetupScreen::toggle_on(Row r) const {
    switch (r) {
        case Row::VSync: return settings_.video.vsync;
        case Row::HdUi: return settings_.video.hd_ui;
        case Row::ShowAtStartup: return settings_.show_at_startup;
        default: return false;
    }
}

void SetupScreen::set_toggle(Row r, bool on) {
    switch (r) {
        case Row::VSync: settings_.video.vsync = on; break;
        case Row::HdUi: settings_.video.hd_ui = on; break;
        case Row::ShowAtStartup: settings_.show_at_startup = on; break;
        default: break;
    }
}

std::wstring SetupScreen::value_text(Row r) const {
    if (is_dropdown(r)) {
        const std::vector<std::wstring> l = choice_labels(r);
        const int i = choice_index(r);
        return i >= 0 && size_t(i) < l.size() ? l[size_t(i)] : std::wstring();
    }
    if (is_toggle(r)) return T(toggle_on(r) ? PcText::On : PcText::Off);
    switch (r) {
        case Row::Disc: return view_.disc_name.empty() ? T(PcText::LnDiscNone) : view_.disc_name;
        case Row::Pack: return view_.can_pack ? Tf(PcText::LnPackValue, view_.pack_size) : T(PcText::LnPackLocked);
        default: return {};
    }
}

// ---------------------------------------------------------------- as caixas

void SetupScreen::show_message(const std::wstring& title, const std::wstring& body) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    close_dropdown();
    dialog_.show(title, body, {T(PcText::SuOk)}, 0, f);
    dialog_progress_ = false;
    nav_.reset();
}

void SetupScreen::show_progress(const std::wstring& title, const std::wstring& body, float progress) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (!dialog_.open || !dialog_progress_) {
        close_dropdown();
        dialog_.show(title, body, {T(PcText::SuCancel)}, 0, f);
        dialog_progress_ = true;
        nav_.reset();
    }
    dialog_.title = title;
    dialog_.body = body;
    dialog_.countdown = std::clamp(progress, 0.0f, 1.0f);
}

void SetupScreen::hide_dialog() {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (dialog_.open) dialog_.hide(f);
    dialog_progress_ = false;
}

// ---------------------------------------------------------------- as ações

void SetupScreen::activate_menu(int item) {
    if (item == 0) {
        if (!view_.ready) {
            toast_.show(T(PcText::SuDescPlayLocked));
            return;
        }
        request_ = SetupRequest{SetupAction::Play};
        return;
    }
    request_ = SetupRequest{SetupAction::Quit};
}

void SetupScreen::activate_row(int i) {
    if (!focusable(i)) return;
    const RowInfo& r = rows_[size_t(i)];
    if (row_locked(i)) {
        // Uma linha travada diz por quê.
        if (r.kind == Row::Drive) toast_.show(view_.drives[size_t(r.drive)].problem);
        else toast_.show(T(PcText::SuDescPlayLocked));
        return;
    }
    if (is_dropdown(r.kind)) {
        open_dropdown_at(i);
        return;
    }
    if (is_toggle(r.kind)) {
        set_toggle(r.kind, !toggle_on(r.kind));
        return;
    }
    switch (r.kind) {
        case Row::Disc: request_ = SetupRequest{SetupAction::Browse}; break;
        case Row::Drive: request_ = SetupRequest{SetupAction::Drive, r.drive}; break;
        case Row::Pack: request_ = SetupRequest{SetupAction::Pack}; break;
        default: break;
    }
}

void SetupScreen::open_dropdown_at(int i) {
    if (dd_row_ == i) {
        close_dropdown();
        return;
    }
    dd_closing_ = -1;
    dd_row_ = i;
    dd_focus_ = std::max(0, choice_index(rows_[size_t(i)].kind));
    dd_open_.snap(0.0f);
    dd_open_.set(1.0f, tk_->motion.dropdown, tk_->motion.ease_out);
    focus_ = i;
    layout_rows();
    reveal_open_list();
    place_mark(true);
}

void SetupScreen::close_dropdown() {
    if (dd_row_ < 0) return;
    dd_closing_ = dd_row_;
    dd_row_ = -1;
    dd_open_.set(0.0f, tk_->motion.dropdown, tk_->motion.ease_out);
    place_mark(true);
    ensure_visible(focus_);
}

void SetupScreen::act_dropdown(Nav c) {
    const int n = int(choice_labels(rows_[size_t(dd_row_)].kind).size());
    switch (c) {
        case Nav::Up:
            if (dd_focus_ > 0) --dd_focus_;
            ensure_visible(dd_row_);
            place_mark(true);
            break;
        case Nav::Down:
            if (dd_focus_ + 1 < n) ++dd_focus_;
            ensure_visible(dd_row_);
            place_mark(true);
            break;
        case Nav::Select:
            set_choice(rows_[size_t(dd_row_)].kind, dd_focus_);
            close_dropdown();
            break;
        case Nav::Back:
        case Nav::Left:
            close_dropdown();
            break;
        default: break;
    }
}

void SetupScreen::act_rows(Nav c) {
    if (dd_row_ >= 0) {
        act_dropdown(c);
        return;
    }
    if (!focusable(focus_)) {
        set_zone_menu(0, true);
        return;
    }
    const Row kind = rows_[size_t(focus_)].kind;
    switch (c) {
        case Nav::Up: {
            const int to = step_focus(focus_, -1);
            if (to >= 0) set_row_focus(to, true);
            break;
        }
        case Nav::Down: {
            const int to = step_focus(focus_, 1);
            if (to >= 0) set_row_focus(to, true);
            break;
        }
        case Nav::Left:
        case Nav::Right: {
            const int dir = c == Nav::Left ? -1 : 1;
            if (is_dropdown(kind)) {
                const int n = int(choice_labels(kind).size());
                if (n > 0) set_choice(kind, ((choice_index(kind) + dir) % n + n) % n);
            } else if (is_toggle(kind)) {
                set_toggle(kind, dir > 0);
            } else if (dir < 0) {
                // As linhas de ação (o disco, os leitores, a cópia): a esquerda volta para PLAY.
                set_zone_menu(0, true);
            }
            break;
        }
        case Nav::Select: activate_row(focus_); break;
        case Nav::Back: set_zone_menu(0, true); break;
        default: break;
    }
}

void SetupScreen::act(Nav c) {
    if (dialog_.open) {
        // A caixa tem um botão só: OK fecha; o do andamento cancela a cópia.
        if (c == Nav::Select || c == Nav::Start || c == Nav::Back) {
            if (dialog_progress_) request_ = SetupRequest{SetupAction::CancelPack};
            else hide_dialog();
        }
        return;
    }
    // O Start do controle (e a tecla da pausa) joga de qualquer lugar da tela.
    if (c == Nav::Start) {
        close_dropdown();
        activate_menu(0);
        return;
    }
    if (zone_ == Zone::Rows) {
        act_rows(c);
        return;
    }
    switch (c) {
        case Nav::Up:
            if (menu_.focus > 0) set_zone_menu(menu_.focus - 1, true);
            break;
        case Nav::Down:
            if (menu_.focus < 1) set_zone_menu(menu_.focus + 1, true);
            break;
        case Nav::Right: set_row_focus(focus_, false); break;
        case Nav::Select: activate_menu(menu_.focus); break;
        default: break;
    }
}

// ---------------------------------------------------------------- o mouse

int SetupScreen::hit_row(float x, float y) const {
    const auto& sp = tk_->space;
    if (x < sp.mid_x0 || x >= sp.mid_x1 || y < sp.list_top || y >= sp.list_bottom) return -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (row_rect(int(i)).contains(x, y)) return int(i);
    }
    return -1;
}

int SetupScreen::hit_option(float x, float y) const {
    if (dd_row_ < 0) return -1;
    const auto& sp = tk_->space;
    if (y < sp.list_top || y >= sp.list_bottom) return -1;
    const int n = int(choice_labels(rows_[size_t(dd_row_)].kind).size());
    for (int j = 0; j < n; ++j) {
        const Rect r = option_rect(j);
        if (Rect{sp.mid_x0, r.y0, r.x1, r.y1}.contains(x, y)) return j;
    }
    return -1;
}

void SetupScreen::mouse(const Frame& f) {
    if (!nav_.mouse_active || !nav_.inside) return;
    const float x = nav_.mx, y = nav_.my;
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && nav_.pressed) act(Nav::Select);
        if (nav_.back) act(Nav::Back);
        return;
    }
    if (nav_.back) {
        if (dd_row_ >= 0) close_dropdown();
        else act(Nav::Back);
        return;
    }
    // A barra de baixo.
    if (nav_.pressed) {
        switch (bar_.hit(x, y)) {
            case kActSelect: act(Nav::Select); return;
            case kActBack: act(Nav::Back); return;
            case kActPlay: act(Nav::Start); return;
            default: break;
        }
    }
    // PLAY e QUIT.
    const int m = menu_.hit(x, y);
    menu_.hover = m;
    if (m >= 0) {
        if (dd_row_ >= 0 && nav_.pressed) close_dropdown();
        if ((nav_.moved || nav_.pressed) && (zone_ != Zone::Menu || m != menu_.focus) && dd_row_ < 0) set_zone_menu(m, true);
        if (nav_.pressed) activate_menu(m);
        return;
    }
    // A roda rola a coluna do meio, também por cima da lista aberta (antes ela só rolava fora das opções, e uma lista
    // longa parecia acabar na borda de baixo).
    if (nav_.wheel != 0.0f && x >= tk_->space.mid_x0 - 40.0f && x < tk_->space.mid_x1 + 40.0f) {
        scroll_target_ = std::clamp(scroll_target_ - nav_.wheel * tk_->space.launch_row_h, 0.0f,
                                    std::max(0.0f, content_height() - view_height()));
        scroll_.set(scroll_target_, tk_->motion.scroll, tk_->motion.ease_out);
    }
    // A lista aberta.
    if (dd_row_ >= 0) {
        const int o = hit_option(x, y);
        if (o >= 0) {
            if (nav_.moved && o != dd_focus_) {
                dd_focus_ = o;
                place_mark(true);
            }
            if (nav_.pressed) {
                dd_focus_ = o;
                act_dropdown(Nav::Select);
            }
            return;
        }
    }
    // As linhas: passar por cima dá o foco, o clique escolhe.
    const int i = hit_row(x, y);
    if (!focusable(i)) {
        if (nav_.pressed && dd_row_ >= 0) close_dropdown();
        return;
    }
    if ((nav_.moved || nav_.pressed) && (i != focus_ || zone_ != Zone::Rows) && dd_row_ < 0) {
        const bool from_menu = zone_ != Zone::Rows;
        zone_ = Zone::Rows;
        focus_ = i;
        menu_.focused = false;
        menu_.mark.show(false, f);
        place_mark(!from_menu);
    }
    if (!nav_.pressed) return;
    if (dd_row_ >= 0 && i != dd_row_) {
        close_dropdown();
        return;
    }
    activate_row(i);
}

// ---------------------------------------------------------------- o quadro

void SetupScreen::frame(float dt, const UiInput& in, const Canvas& cv, const Measure& m) {
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    dialog_.update(dt);
    toast_.update(dt, *tk_);
    dd_open_.update(dt);
    scroll_.update(dt);
    mark_.update(dt);
    if (dd_row_ < 0 && dd_closing_ >= 0 && dd_open_.done()) dd_closing_ = -1;
    device_ = in.device;
    const Frame f{*tk_, cv, m, time_};
    // A coluna da esquerda: PLAY e QUIT.
    menu_.x = tk_->space.left_x;
    menu_.y = tk_->space.launch_menu_y;
    menu_.focused = zone_ == Zone::Menu && !dialog_.open;
    menu_.layout(f);
    if (!menu_.mark.placed) menu_.set_focus(menu_.focus, f, false);
    menu_.update(dt, f);
    // As linhas: a cor dos nomes, as chaves e a troca dos valores animam.
    for (size_t i = 0; i < rows_.size(); ++i) {
        RowAnim& a = anims_[i];
        const RowInfo& r = rows_[i];
        a.label.update(dt);
        a.knob.update(dt);
        a.change.update(dt);
        const bool lit = zone_ == Zone::Rows && int(i) == focus_ && !dialog_.open;
        a.label.set(lit ? 1.0f : 0.0f, tk_->motion.focus, tk_->motion.ease_out);
        if (is_toggle(r.kind)) a.knob.set(toggle_on(r.kind) ? 1.0f : 0.0f, tk_->motion.toggle, tk_->motion.ease_out);
        if (r.kind != Row::Section) {
            const std::wstring v = value_text(r.kind);
            if (v != a.shown_value) {
                a.old_value = a.shown_value.empty() ? std::wstring() : a.shown_value;
                a.shown_value = v;
                a.change.snap(a.old_value.empty() ? 1.0f : 0.0f);
                a.change.set(1.0f, tk_->motion.value, tk_->motion.ease_out);
            }
        }
    }
    layout_rows();
    if (!mark_.placed) place_mark(false);
    if (dialog_.open) dialog_.layout(f);
    nav_.read(in, cv, dt, *tk_, true);
    for (int i = 0; i < kNavCount; ++i) {
        if (pending_[i]) nav_.inject(Nav(i));
        pending_[i] = false;
    }
    prompts(bar_, in.device == 1);
    bar_.layout(f);
    // A janela de escolha do Windows aberta: nada aqui responde.
    if (view_.picking) return;
    for (int i = 0; i < kNavCount; ++i) {
        if (nav_.fire(Nav(i))) act(Nav(i));
    }
    mouse(f);
}

// ---------------------------------------------------------------- o desenho

void SetupScreen::prompts(PromptBar& bar, bool pad) const {
    bar.prompts.clear();
    bar.info_title = T(PcText::MdFooterPort);
    bar.info_sub = L"SLUS-20579 · NTSC-U";
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdFooterBuild), build_.empty() ? "dev" : build_.c_str());
        bar.info2_title = widen(buf);
        bar.info2_sub = T(PcText::LnFooter);
    }
    const std::wstring sel = bind_key("menu_select");
    const std::wstring sel_key = sel.empty() ? std::wstring(L"Enter") : sel;
    const std::string a = pad ? std::string("xbox_a") : std::string();
    const std::string b = pad ? std::string("xbox_b") : std::string();
    if (dialog_.open) {
        bar.prompts.push_back(Prompt{sel_key, a, T(PcText::MdPromptSelect), kActSelect});
        bar.prompts.push_back(Prompt{L"Esc", b, T(dialog_progress_ ? PcText::SuCancel : PcText::MdPromptBack), kActBack});
        return;
    }
    if (zone_ == Zone::Menu) {
        // Na coluna da esquerda: Enter joga (ou sai, em QUIT).
        bar.prompts.push_back(Prompt{L"Enter", a, T(menu_.focus == 0 ? PcText::LnPromptPlay : PcText::LnPromptQuit), kActSelect});
        return;
    }
    const Row kind = focus_row();
    if (dd_row_ >= 0) {
        bar.prompts.push_back(Prompt{sel_key, a, T(PcText::MdPromptSelect), kActSelect});
    } else if (is_dropdown(kind) || is_toggle(kind)) {
        bar.prompts.push_back(Prompt{L"← →", pad ? std::string("xbox_dpad_h") : std::string(), T(PcText::MdPromptChange), 0});
        bar.prompts.push_back(
            Prompt{sel_key, a, T(is_toggle(kind) ? PcText::MdPromptToggle : PcText::MdPromptSelect), kActSelect});
    } else if (kind == Row::Disc) {
        bar.prompts.push_back(Prompt{sel_key, a, T(PcText::LnPromptBrowse), kActSelect});
    } else if (!row_locked(focus_)) {
        bar.prompts.push_back(Prompt{sel_key, a, T(PcText::MdPromptSelect), kActSelect});
    }
    bar.prompts.push_back(Prompt{L"Esc", b, T(PcText::MdPromptBack), kActBack});
    // O Start do controle joga de qualquer lugar.
    if (pad && view_.ready) bar.prompts.push_back(Prompt{std::wstring(), "xbox_menu", T(PcText::LnPromptPlay), kActPlay});
}

Description SetupScreen::description() const {
    Description d;
    d.strip[0] = L"NF.PC";
    d.strip[1] = L"LAUNCH";
    d.strip[2] = L"DISC 0";
    d.strip[3] = view_.ready ? L"READY" : L"WAITING";
    const Row kind = focus_row();
    const auto current = [&] {
        d.value_label = T(PcText::MdCurrentValue);
        d.value = value_text(kind);
        if (settings_changed()) d.notice = T(PcText::LnChanged);
    };
    switch (kind) {
        case Row::Play:
            d.title = T(PcText::SuDescPlayTitle);
            d.body = T(view_.ready ? PcText::LnDescPlay : PcText::SuDescPlayLocked);
            d.value_label = T(PcText::SuDisc);
            d.value = view_.disc.empty() ? std::wstring(L"—") : view_.disc;
            d.notice = view_.status;
            break;
        case Row::Quit:
            d.title = T(PcText::SuDescQuitTitle);
            d.body = T(PcText::LnDescQuit);
            break;
        case Row::Disc:
            d.title = T(PcText::LnDisc);
            // Sem disco: o que a linha faz (a escolha da imagem); com um, o disco em uso e como trocar.
            d.body = T(!view_.ready ? PcText::SuDescBrowse : view_.packed ? PcText::LnDescDiscPacked : PcText::LnDescDisc);
            d.value_label = T(PcText::SuDisc);
            d.value = view_.disc.empty() ? std::wstring(L"—") : view_.disc;
            d.notice = view_.status;
            break;
        case Row::Drive: {
            const SetupView::Drive& dr = view_.drives[size_t(rows_[size_t(focus_)].drive)];
            d.title = T(PcText::SuDescDriveTitle);
            d.body = T(PcText::SuDescDrive);
            d.value_label = T(PcText::SuDisc);
            d.value = dr.root;
            d.notice = dr.usable ? std::wstring() : dr.problem;
            break;
        }
        case Row::Pack:
            d.title = T(PcText::SuDescPackTitle);
            d.body = Tf(PcText::SuDescPack, view_.pack_size.empty() ? std::wstring(L"—") : view_.pack_size);
            d.value_label = T(PcText::SuSaveAs);
            d.value = view_.pack_path.empty() ? std::wstring(L"—") : view_.pack_path;
            d.notice = T(PcText::SuPackWarning);
            break;
        case Row::Resolution:
            d.title = T(PcText::Resolution);
            d.body = T(PcText::LnDescResolution);
            current();
            break;
        case Row::WindowMode:
            d.title = T(PcText::WindowMode);
            d.body = T(PcText::LnDescWindowMode);
            current();
            break;
        case Row::VSync:
            d.title = T(PcText::VSync);
            d.body = T(PcText::MdDescVSync);
            current();
            break;
        case Row::MenuStyle:
            d.title = T(PcText::MenuStyle);
            d.body = T(PcText::LnDescMenuStyle);
            current();
            break;
        case Row::HdUi:
            d.title = T(PcText::LnHdUi);
            d.body = T(PcText::LnDescHdUi);
            current();
            break;
        case Row::ShowAtStartup:
            d.title = T(PcText::LnShowAtStartup);
            d.body = T(PcText::LnDescShow);
            current();
            break;
        case Row::Section: break;
    }
    return d;
}

void SetupScreen::draw_left(DrawList& dl, const Frame& f, float k, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const float x = sp.left_x + dx;
    const float width = sp.mid_x0 - sp.left_x - 56.0f;
    line_at(dl, x + 3.0f, sp.header_y, T(PcText::LnHeader), tk.type.header, tk.color.dim.with_alpha(k));
    // O título (o do port: antes do disco não há o logotipo do jogo) com o traço de ouro e a linha fina.
    const std::wstring title = T(PcText::SuTitle);
    const TextStyle ts = fit_style(f, title, tk.type.page_title, width, 30.0f);
    line_at(dl, x - 2.0f, 214.0f, title, ts, tk.color.text.with_alpha(k));
    const float tw = std::min(width, text_width(f, title, ts));
    dl.fill(Rect{x, 252.0f, x + 64.0f, 254.0f}, tk.color.accent.with_alpha(k));
    dl.line(x + 64.0f, 253.0f, x + std::max(tw, 120.0f), 253.0f, tk.color.hairline_2.with_alpha(k), 1.0f);
    float y = 304.0f;
    if (view_.ready) {
        // O disco em uso: o tipo, o nome e o estado.
        line_at(dl, x, y, view_.disc_kind, tk.type.section, tk.color.dim.with_alpha(k));
        y += 36.0f;
        TextStyle ns = tk.type.label;
        ns.weight = 500;
        line_at(dl, x, y, elide_middle(f, view_.disc_name.empty() ? view_.disc : view_.disc_name, ns, width), ns,
                tk.color.text.with_alpha(k));
        y += 40.0f;
        for (const std::wstring& l : wrap_lines(f, view_.status, tk.type.meta, width - 18.0f)) {
            line_at(dl, x + 18.0f, y, l, tk.type.meta, tk.color.accent.with_alpha(k));
            y += tk.type.meta.size * 1.4f;
        }
        dl.fill(Rect{x, 380.0f - 4.0f, x + 8.0f, 380.0f + 4.0f}, tk.color.accent.with_alpha(k));
    } else {
        // Sem disco: o pedido e a frase de que o jogo vem da cópia do jogador.
        for (const std::wstring& l : wrap_lines(f, to_upper(T(PcText::SuPrompt)), tk.type.meta, width)) {
            line_at(dl, x, y, l, tk.type.meta, tk.color.accent.with_alpha(k));
            y += tk.type.meta.size * 1.4f;
        }
        y += 14.0f;
        for (const std::wstring& l : wrap_lines(f, T(PcText::SuIntro), tk.type.body, width)) {
            line_at(dl, x, y, l, tk.type.body, tk.color.text_2.with_alpha(k));
            y += tk.type.body.size * tk.type.body.line;
        }
        if (view_.problem && !view_.status.empty()) {
            y += 14.0f;
            for (const std::wstring& l : wrap_lines(f, view_.status, tk.type.meta, width)) {
                line_at(dl, x, y, l, tk.type.meta, tk.color.text.with_alpha(k));
                y += tk.type.meta.size * 1.4f;
            }
        }
    }
    menu_.draw(dl, f, k, dx);
}

void SetupScreen::draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const float top = sp.list_top, bottom = sp.list_bottom;
    const float scroll = scroll_.value();
    const float total = content_height();
    dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, top - 2.0f, sp.mid_x1 + 20.0f + dx, bottom});
    // A opacidade de uma faixa da lista: some perto das pontas cortadas.
    const auto fade = [&](float y0, float y1) {
        float a = 1.0f;
        const float cy = (y0 + y1) * 0.5f;
        if (scroll > 0.5f) a *= std::clamp((cy - top - 6.0f) / (sp.fade_px * 1.6f), 0.0f, 1.0f);
        if (total - scroll > (bottom - top) + 0.5f) a *= std::clamp((bottom - cy - 6.0f) / (sp.fade_px * 1.6f), 0.0f, 1.0f);
        return a;
    };
    const float reserve = std::max(text_width(f, T(PcText::On), tk.type.value), text_width(f, T(PcText::Off), tk.type.value));
    const int anim_row = dd_row_ >= 0 ? dd_row_ : dd_closing_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const RowInfo& r = rows_[i];
        const RowAnim& an = anims_[i];
        const float y0 = top + r.y - scroll;
        const float y1 = y0 + r.h;
        if ((y1 < top - 4.0f || y0 > bottom + 4.0f) && int(i) != anim_row) continue;
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y1};
        RowState s;
        s.alpha = alpha * fade(y0, y1);
        s.focused = zone_ == Zone::Rows && int(i) == focus_ && dd_row_ < 0;
        s.label_t = an.label.value();
        if (dd_row_ >= 0 && int(i) != dd_row_) s.alpha *= 0.55f;  // com uma lista aberta, o resto recua
        const float hair_x1 = sp.mid_x1 - 8.0f + dx;
        std::wstring label = r.label != PcText::Count ? T(r.label) : std::wstring();
        if (r.kind == Row::Drive) label = Tf(PcText::LnDrive, drive_name(view_.drives[size_t(r.drive)].root));
        if (r.kind == Row::Section) {
            draw_section(dl, f, row, label, s.alpha);
            continue;
        }
        RowState ls = s;
        if (row_locked(int(i))) {
            ls.disabled = true;
            ls.label_t = 0.0f;
        }
        draw_row_label(dl, f, row, label, ls);
        if (is_dropdown(r.kind)) {
            draw_dropdown_value(dl, f, row, an.shown_value, int(i) == dd_row_, s, an.old_value, an.change.value());
        } else if (is_toggle(r.kind)) {
            draw_toggle(dl, f, row, an.knob.value(), value_text(r.kind), s, reserve);
        } else if (row_locked(int(i))) {
            draw_locked_value(dl, f, row, r.kind == Row::Drive ? T(PcText::LnDriveLocked) : T(PcText::LnPackLocked), s);
        } else {
            // As linhas de ação: o valor (o disco em uso encurtado no meio para caber ao lado do nome).
            std::wstring v = an.shown_value;
            if (r.kind == Row::Drive) {
                v = T(view_.drives[size_t(r.drive)].in_use ? PcText::LnDriveInUse : PcText::LnDriveUse);
            }
            const float room = row.w() - sp.pad_l - sp.pad_r - text_width(f, label, tk.type.label) - 48.0f;
            v = elide_middle(f, v, tk.type.value, room);
            const Color vc = (r.kind == Row::Disc && !view_.ready) ? tk.color.dim : mix(tk.color.text_2, tk.color.text, s.label_t);
            line_at(dl, row.x1 - sp.pad_r, row.cy(), v, tk.type.value, vc.with_alpha(s.alpha), Align::Right);
        }
        draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
        // As opções da lista aberta (ou fechando), recuadas, abaixo da linha.
        if (int(i) == anim_row) {
            const std::vector<std::wstring> labels = choice_labels(r.kind);
            const int chosen = choice_index(r.kind);
            const float open = dd_open_.value();
            const float oh = sp.launch_option_h;
            const float area = oh * float(labels.size()) * open;
            dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, std::max(top, y1), sp.mid_x1 + 20.0f + dx, std::min(bottom, y1 + area)});
            for (size_t j = 0; j < labels.size(); ++j) {
                const float oy = y1 + oh * float(j) - oh * float(labels.size()) * (1.0f - open) * 0.35f;
                const Rect orow{sp.mid_x0 + sp.option_indent + dx, oy, sp.mid_x1 + dx, oy + oh};
                RowState os;
                os.alpha = alpha * open * fade(oy, oy + oh);
                os.focused = dd_row_ >= 0 && int(j) == dd_focus_;
                os.label_t = os.focused ? 1.0f : 0.0f;
                draw_option(dl, f, orow, labels[j], int(j) == chosen, os);
                draw_hairline(dl, f, orow.x0, hair_x1, orow.y1, os.alpha);
            }
            dl.pop_clip();
        }
    }
    mark_.draw(dl, f, alpha, top - scroll);
    dl.pop_clip();
    // O indicador de rolagem, só quando a lista passa da área.
    const float view = bottom - top;
    if (total > view + 0.5f) {
        draw_scroll(dl, f, sp.scroll_x + dx, top + 24.0f, bottom, view / total, scroll / (total - view), alpha);
    }
}

void SetupScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const auto& sp = tk.space;
    const Frame f{tk, cv, m, time_};
    const float k = appear_.value();
    // O fundo opaco (antes do disco não há cena nem arte do jogo), a vinheta e o 007 do port bem fraco.
    const Rect win{cv.left, cv.top, cv.right, cv.bottom};
    dl.fill(win, tk.color.ground);
    draw_background(dl, f, k);
    dl.glyph(Rect{1010.0f, 330.0f, 1910.0f, 778.0f}, "results_007", 0.022f * k);
    const float dx = sp.slide * (1.0f - k);
    draw_left(dl, f, k, -dx);
    draw_rows(dl, f, k, dx * 0.6f);
    // À direita: a descrição da linha com o foco (o caminho do disco encurtado no meio para caber).
    Description d = description();
    {
        TextStyle vs = tk.type.meta;
        vs.weight = 600;
        const float room = sp.desc_x1 - sp.desc_x0 - text_width(f, d.value_label, tk.type.meta) - 8.0f;
        d.value = elide_middle(f, d.value, vs, room);
    }
    draw_description(dl, f, d, k, dx);
    bar_.draw(dl, f, k);
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

// ---------------------------------------------------------------- os textos

std::wstring setup_size_text(uint64_t bytes) {
    wchar_t buf[32];
    if (bytes >= (1ull << 30)) std::swprintf(buf, 32, L"%.1f GB", double(bytes) / double(1ull << 30));
    else std::swprintf(buf, 32, L"%llu MB", static_cast<unsigned long long>((bytes + (1ull << 19)) >> 20));
    return buf;
}

std::wstring setup_problem_text(const DiscCheck& c) {
    const std::wstring id = widen(c.boot_id);
    switch (c.status) {
        case DiscStatus::Ok: return {};
        case DiscStatus::Missing: return T(PcText::SuErrMissing);
        case DiscStatus::Unreadable: return T(PcText::SuErrUnreadable);
        case DiscStatus::NotPs2: return T(PcText::SuErrNotPs2);
        case DiscStatus::WrongGame: return Tf(PcText::SuErrWrongGame, id);
        case DiscStatus::WrongVersion: return Tf(PcText::SuErrWrongVersion, id);
        case DiscStatus::Incomplete: return Tf(PcText::SuErrIncomplete, widen(c.missing));
    }
    return {};
}

std::wstring setup_ready_text(const DiscCheck& c) {
    if (c.unverified) return T(PcText::SuReadyUnverified);
    return Tf(PcText::SuReady, widen(c.boot_id));
}

std::wstring setup_kind_text(DiscKind k) {
    switch (k) {
        case DiscKind::Folder: return T(PcText::SuKindFolder);
        case DiscKind::Image: return T(PcText::SuKindImage);
        case DiscKind::Drive: return T(PcText::SuKindDrive);
        case DiscKind::Packed: return T(PcText::SuKindPacked);
    }
    return {};
}

std::wstring setup_disc_name(DiscKind k, const std::wstring& location) {
    switch (k) {
        case DiscKind::Packed: return T(PcText::LnDiscPacked);
        case DiscKind::Drive: return Tf(PcText::LnDiscDrive, drive_name(location));
        case DiscKind::Image: {
            const size_t slash = location.find_last_of(L"\\/");
            return slash == std::wstring::npos ? location : location.substr(slash + 1);
        }
        case DiscKind::Folder: return location;
    }
    return location;
}

std::string SetupScreen::describe() const {
    std::string rows;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind == Row::Section) continue;
        rows += (rows.empty() ? "" : ",") + std::string(row_name(rows_[i].kind)) + (row_locked(int(i)) ? "*" : "");
    }
    std::string disc;
    for (wchar_t c : view_.disc) disc.push_back(c < 128 ? char(c) : '?');
    const VideoSettings& v = settings_.video;
    char buf[1400];
    std::snprintf(buf, sizeof(buf),
                  "configuracao zona %s foco %d (%s) linhas %s pronto %d problema %d copia %d leitores %zu dialogo %d%s "
                  "escolhendo %d mouse %d lista_aberta %d opcao %d rolagem %.0f | opcoes resolucao %dx%d modo %s vsync %d "
                  "estilo %s hd %d mostrar %d mudou %d | disco \"%s\"",
                  zone_ == Zone::Menu ? "menu" : "linhas", focus(), row_name(focus_row()), rows.c_str(), view_.ready ? 1 : 0,
                  view_.problem ? 1 : 0, view_.can_pack ? 1 : 0, view_.drives.size(), dialog_.open ? 1 : 0,
                  dialog_progress_ ? "(andamento)" : "", view_.picking ? 1 : 0, nav_.mouse_active ? 1 : 0, dd_row_, dd_focus_,
                  double(scroll_.value()), v.width, v.height, window_mode_name(v.window_mode), v.vsync ? 1 : 0,
                  menu_style_name(v.menu_style), v.hd_ui ? 1 : 0, settings_.show_at_startup ? 1 : 0, settings_changed() ? 1 : 0,
                  disc.c_str());
    return buf;
}

}  // namespace nf::ui
