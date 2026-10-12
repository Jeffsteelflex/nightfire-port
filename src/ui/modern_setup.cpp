// A tela de configuração da primeira partida. Ver modern_setup.h e docs/design.md.

#include "ui/modern_setup.h"

#include <algorithm>
#include <cstdio>

#include "engine/pc_text.h"

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

enum : int { kActSelect = 1, kActBack };

}  // namespace

SetupScreen::SetupScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

void SetupScreen::open(const SetupView& v) {
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.front_in, tk_->motion.ease_out);
    menu_ = BigMenu{};
    bar_ = PromptBar{};
    dialog_ = Dialog{};
    dialog_progress_ = false;
    toast_ = Toast{};
    nav_.reset();
    for (bool& p : pending_) p = false;
    request_ = SetupRequest{};
    set_view(v);
    if (v.ready) focus_play();
}

void SetupScreen::set_rows() {
    // O foco fica na mesma linha (pelo tipo e o leitor) quando a lista muda.
    RowInfo keep{Row::Browse, -1};
    if (menu_.focus >= 0 && size_t(menu_.focus) < rows_.size()) keep = rows_[size_t(menu_.focus)];
    rows_.clear();
    rows_.push_back(RowInfo{Row::Browse});
    for (size_t i = 0; i < view_.drives.size(); ++i) rows_.push_back(RowInfo{Row::Drive, int(i)});
    rows_.push_back(RowInfo{Row::Play});
    rows_.push_back(RowInfo{Row::Pack});
    rows_.push_back(RowInfo{Row::Quit});
    menu_.items.clear();
    menu_.x = tk_->space.left_x;
    for (const RowInfo& r : rows_) {
        BigMenu::Item it;
        switch (r.kind) {
            case Row::Browse:
                it.label = T(PcText::SuBrowse);
                it.sub = T(PcText::SuBrowseSub);
                break;
            case Row::Drive:
                it.label = Tf(PcText::SuDrive, drive_name(view_.drives[size_t(r.drive)].root));
                it.sub = T(PcText::SuDriveSub);
                it.locked = !view_.drives[size_t(r.drive)].usable;
                break;
            case Row::Play:
                it.label = T(PcText::SuPlay);
                it.sub = T(PcText::SuPlaySub);
                it.locked = !view_.ready;
                break;
            case Row::Pack:
                it.label = T(PcText::SuPack);
                it.compact = true;
                it.locked = !view_.can_pack;
                break;
            case Row::Quit:
                it.label = T(PcText::SuQuit);
                it.compact = true;
                break;
        }
        menu_.items.push_back(it);
    }
    int focus = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind == keep.kind && (keep.kind != Row::Drive || rows_[i].drive == keep.drive)) focus = int(i);
    }
    menu_.focus = std::clamp(focus, 0, int(rows_.size()) - 1);
    menu_.mark.placed = false;
}

void SetupScreen::set_view(const SetupView& v) {
    const bool rows_change = v.drives.size() != view_.drives.size() || v.ready != view_.ready || v.can_pack != view_.can_pack ||
                             rows_.empty() ||
                             [&] {
                                 for (size_t i = 0; i < v.drives.size(); ++i) {
                                     if (v.drives[i].root != view_.drives[i].root || v.drives[i].usable != view_.drives[i].usable) return true;
                                 }
                                 return false;
                             }();
    view_ = v;
    if (rows_change) set_rows();
}

bool SetupScreen::row_locked(int i) const {
    return i >= 0 && size_t(i) < menu_.items.size() && menu_.items[size_t(i)].locked;
}

void SetupScreen::focus_play() {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind != Row::Play) continue;
        menu_.focus = int(i);
        menu_.mark.placed = false;  // o sublinhado vai para lá na próxima montagem
    }
}

void SetupScreen::show_message(const std::wstring& title, const std::wstring& body) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    dialog_.show(title, body, {T(PcText::SuOk)}, 0, f);
    dialog_progress_ = false;
    nav_.reset();
}

void SetupScreen::show_progress(const std::wstring& title, const std::wstring& body, float progress) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (!dialog_.open || !dialog_progress_) {
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

void SetupScreen::activate(int i) {
    if (i < 0 || size_t(i) >= rows_.size()) return;
    const RowInfo& r = rows_[size_t(i)];
    if (row_locked(i)) {
        // Uma linha travada diz por quê.
        if (r.kind == Row::Drive) toast_.show(view_.drives[size_t(r.drive)].problem);
        else toast_.show(T(PcText::SuDescPlayLocked));
        return;
    }
    switch (r.kind) {
        case Row::Browse: request_ = SetupRequest{SetupAction::Browse}; break;
        case Row::Drive: request_ = SetupRequest{SetupAction::Drive, r.drive}; break;
        case Row::Play: request_ = SetupRequest{SetupAction::Play}; break;
        case Row::Pack: request_ = SetupRequest{SetupAction::Pack}; break;
        case Row::Quit: request_ = SetupRequest{SetupAction::Quit}; break;
    }
}

void SetupScreen::act(Nav c) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (dialog_.open) {
        // A caixa tem um botão só: OK fecha; o do andamento cancela a cópia.
        if (c == Nav::Select || c == Nav::Start || c == Nav::Back) {
            if (dialog_progress_) request_ = SetupRequest{SetupAction::CancelPack};
            else hide_dialog();
        }
        return;
    }
    switch (c) {
        case Nav::Up:
            if (menu_.focus > 0) menu_.set_focus(menu_.focus - 1, f, true);
            break;
        case Nav::Down:
            if (menu_.focus + 1 < int(menu_.items.size())) menu_.set_focus(menu_.focus + 1, f, true);
            break;
        case Nav::Select:
        case Nav::Start: activate(menu_.focus); break;
        default: break;
    }
}

void SetupScreen::frame(float dt, const UiInput& in, const Canvas& cv, const Measure& m) {
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    dialog_.update(dt);
    toast_.update(dt, *tk_);
    const Frame f{*tk_, cv, m, time_};
    menu_.y = rows_.size() > 4 ? 548.0f : 572.0f;
    menu_.focused = !dialog_.open;
    menu_.layout(f);
    if (!menu_.mark.placed) menu_.set_focus(menu_.focus, f, false);
    menu_.update(dt, f);
    if (dialog_.open) dialog_.layout(f);
    nav_.read(in, cv, dt, *tk_, true);
    for (int i = 0; i < kNavCount; ++i) {
        if (pending_[i]) nav_.inject(Nav(i));
        pending_[i] = false;
    }
    // A barra de baixo.
    bar_.prompts.clear();
    bar_.info_title = T(PcText::MdFooterPort);
    bar_.info_sub = L"SLUS-20579 · NTSC-U";
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdFooterBuild), build_.empty() ? "dev" : build_.c_str());
        bar_.info2_title = widen(buf);
        bar_.info2_sub = T(PcText::SuHeader);
    }
    const bool pad = in.device == 1;
    const std::wstring sel = bind_key("menu_select");
    bar_.prompts.push_back(Prompt{sel.empty() ? L"Enter" : sel, pad ? std::string("xbox_a") : std::string(), T(PcText::MdPromptSelect),
                                  kActSelect});
    if (dialog_.open) {
        bar_.prompts.push_back(Prompt{L"Esc", pad ? std::string("xbox_b") : std::string(),
                                      T(dialog_progress_ ? PcText::SuCancel : PcText::MdPromptBack), kActBack});
    }
    bar_.layout(f);
    // A janela de escolha do Windows aberta: nada aqui responde.
    if (view_.picking) return;
    for (int i = 0; i < kNavCount; ++i) {
        if (nav_.fire(Nav(i))) act(Nav(i));
    }
    if (!nav_.mouse_active || !nav_.inside) return;
    const float x = nav_.mx, y = nav_.my;
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && nav_.pressed) act(Nav::Select);
        if (nav_.back) act(Nav::Back);
        return;
    }
    const int h = menu_.hit(x, y);
    menu_.hover = h;
    if (h >= 0 && nav_.moved && h != menu_.focus) menu_.set_focus(h, f, true);
    if (nav_.pressed) {
        if (h >= 0) {
            menu_.set_focus(h, f, true);
            activate(h);
            return;
        }
        if (bar_.hit(x, y) == kActSelect) activate(menu_.focus);
    }
}

Description SetupScreen::description() const {
    Description d;
    d.strip[0] = L"NF.PC";
    d.strip[1] = L"SETUP";
    d.strip[2] = L"DISC 0";
    d.strip[3] = view_.ready ? L"READY" : L"WAITING";
    d.value_label = T(PcText::SuDisc);
    d.value = view_.disc.empty() ? std::wstring(L"—") : view_.disc;
    d.notice = view_.status;
    const RowInfo r = menu_.focus >= 0 && size_t(menu_.focus) < rows_.size() ? rows_[size_t(menu_.focus)] : RowInfo{Row::Browse};
    switch (r.kind) {
        case Row::Browse:
            d.title = T(PcText::SuDescBrowseTitle);
            d.body = T(PcText::SuDescBrowse);
            break;
        case Row::Drive: {
            const SetupView::Drive& dr = view_.drives[size_t(r.drive)];
            d.title = T(PcText::SuDescDriveTitle);
            d.body = T(PcText::SuDescDrive);
            d.value = dr.root;
            d.notice = dr.usable ? std::wstring() : dr.problem;
            break;
        }
        case Row::Play:
            d.title = T(PcText::SuDescPlayTitle);
            d.body = T(view_.ready ? PcText::SuDescPlay : PcText::SuDescPlayLocked);
            break;
        case Row::Pack:
            d.title = T(PcText::SuDescPackTitle);
            d.body = Tf(PcText::SuDescPack, view_.pack_size.empty() ? std::wstring(L"—") : view_.pack_size);
            d.value_label = T(PcText::SuSaveAs);
            d.value = view_.pack_path.empty() ? std::wstring(L"—") : view_.pack_path;
            d.notice = T(PcText::SuPackWarning);
            break;
        case Row::Quit:
            d.title = T(PcText::SuDescQuitTitle);
            d.body = T(PcText::SuDescQuit);
            break;
    }
    return d;
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
    // À esquerda: o título, o pedido e o texto; embaixo, a lista.
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, T(PcText::SuHeader) + L" /", tk.type.header, tk.color.dim.with_alpha(k));
    TextStyle hs = tk.type.hero;
    hs.size = 64.0f;
    hs.tracking = 0.12f;
    const std::wstring title = T(PcText::SuTitle);
    hs = fit_style(f, title, hs, 900.0f, 40.0f);
    line_at(dl, sp.left_x - 4.0f + dx, 236.0f, title, hs, tk.color.text.with_alpha(k));
    const float tw = std::min(900.0f, text_width(f, title, hs));
    dl.fill(Rect{sp.left_x + dx, 290.0f, sp.left_x + 64.0f + dx, 292.0f}, tk.color.accent.with_alpha(k));
    dl.line(sp.left_x + 64.0f + dx, 291.0f, sp.left_x + tw + dx, 291.0f, tk.color.hairline_2.with_alpha(k), 1.0f);
    const std::wstring prompt = T(PcText::SuPrompt);
    const TextStyle ps = fit_style(f, prompt, tk.type.hero_sub, 1120.0f, 14.0f);
    line_at(dl, sp.left_x + dx, 340.0f, prompt, ps, tk.color.accent.with_alpha(k));
    float y = 392.0f;
    for (const std::wstring& l : wrap_lines(f, T(PcText::SuIntro), tk.type.body, 1060.0f)) {
        line_at(dl, sp.left_x + dx, y, l, tk.type.body, tk.color.text_2.with_alpha(k));
        y += tk.type.body.size * tk.type.body.line;
    }
    menu_.draw(dl, f, k, dx);
    // À direita: a descrição da linha, o disco e o estado (o caminho encurtado no meio para caber).
    Description d = description();
    {
        TextStyle vs = tk.type.meta;
        vs.weight = 600;
        const float room = sp.desc_x1 - sp.desc_x0 - text_width(f, d.value_label, tk.type.meta) - 8.0f;
        d.value = elide_middle(f, d.value, vs, room);
    }
    draw_description(dl, f, d, k, -dx);
    bar_.draw(dl, f, k);
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

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

std::string SetupScreen::describe() const {
    std::string rows;
    for (size_t i = 0; i < rows_.size(); ++i) {
        static const char* kNames[] = {"imagem", "leitor", "jogar", "copia", "sair"};
        rows += (i ? "," : "") + std::string(kNames[int(rows_[i].kind)]) + (row_locked(int(i)) ? "*" : "");
    }
    std::string disc;
    for (wchar_t c : view_.disc) disc.push_back(c < 128 ? char(c) : '?');
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "configuracao foco %d linhas %s pronto %d problema %d copia %d leitores %zu dialogo %d%s "
                  "escolhendo %d mouse %d disco \"%s\"",
                  menu_.focus, rows.c_str(), view_.ready ? 1 : 0, view_.problem ? 1 : 0, view_.can_pack ? 1 : 0,
                  view_.drives.size(), dialog_.open ? 1 : 0, dialog_progress_ ? "(andamento)" : "", view_.picking ? 1 : 0,
                  nav_.mouse_active ? 1 : 0, disc.c_str());
    return buf;
}

}  // namespace nf::ui
