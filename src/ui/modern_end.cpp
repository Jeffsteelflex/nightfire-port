// O fim de missão e o resultado no estilo Modern. Ver modern_end.h e docs/design.md.

#include "ui/modern_end.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/pc_text.h"

namespace nf::ui {

namespace {

std::wstring T(PcText id) { return widen(PcText_Utf8(id)); }

void line_at(DrawList& dl, float x, float cy, const std::wstring& s, const TextStyle& st, const Color& c, Align a = Align::Left,
             float w = 4000.0f) {
    const float h = st.size * 1.6f;
    Rect r{x, cy - h * 0.5f, x + w, cy + h * 0.5f};
    if (a == Align::Right) r = Rect{x - w, cy - h * 0.5f, x, cy + h * 0.5f};
    if (a == Align::Centre) r = Rect{x - w * 0.5f, cy - h * 0.5f, x + w * 0.5f, cy + h * 0.5f};
    dl.text(r, st.caps ? to_upper(s) : s, st, c, a);
}

// Uma fase de uma animação: de 0 a 1 entre start e start + dur (segundos), pela curva.
float phase(float t, float start, float dur, const Curve& c) {
    if (dur <= 0.0f) return t >= start ? 1.0f : 0.0f;
    return curve_eval(c, std::clamp((t - start) / dur, 0.0f, 1.0f));
}

// O subtítulo da linha da lista do fim de missão pelo valor dela (P_ENDMISSION_Handler: 0 tenta de novo, 1
// recomeça a missão, 2 sai).
PcText fail_sub(int32_t data) {
    switch (data) {
        case 0: return PcText::MfReplaySub;
        case 1: return PcText::MfRestartSub;
        default: return PcText::MfQuitSub;
    }
}

std::wstring strip_colon(std::wstring s) {
    while (!s.empty() && (s.back() == L':' || s.back() == L' ')) s.pop_back();
    return s;
}

// A caixa de mensagem do jogo vira o Dialog do kit (as respostas da lista dela).
void show_box(Dialog& d, const MmSnapshot& s, const Frame& f) {
    std::vector<std::wstring> buttons;
    for (const MmItem& o : s.dialog.options) {
        std::wstring t = widen(o.text);
        if (t.find(L'~') != std::wstring::npos) t = T(PcText::MrContinue);
        buttons.push_back(t);
    }
    // As caixas do jogo não têm título: o do port (o cartão, um aviso, uma pergunta).
    std::wstring title = widen(s.dialog.title);
    if (title.empty() && s.dialog.box) {
        title = T(s.page == kMmCardPage ? PcText::FeCardTitle : (buttons.size() > 1 ? PcText::FeConfirmTitle : PcText::FeNoticeTitle));
    }
    d.show(title, widen(s.dialog.body), buttons, std::clamp(s.dialog.focus, 0, std::max(0, int(buttons.size()) - 1)), f);
    d.busy = buttons.empty();
}

// O Dialog: andar, escolher e voltar pela lista do jogo.
bool dialog_command(Dialog& d, Nav c, uint32_t list, uint32_t tag, const Frame& f) {
    const int n = int(d.buttons.size());
    if (n == 0) return false;
    const auto move = [&](int dir) {
        const int to = std::clamp(d.focus + dir, 0, n - 1);
        if (to == d.focus) return;
        d.focus = to;
        if (size_t(to) < d.button_rects.size()) d.mark.move_to(d.button_rects[size_t(to)].x0, d.button_rects[size_t(to)].x1,
                                                               d.button_rects[size_t(to)].y1, f, true);
        MenuModern_Pick(list, tag, to);
    };
    switch (c) {
        case Nav::Left:
        case Nav::Up: move(-1); return false;
        case Nav::Right:
        case Nav::Down: move(1); return false;
        case Nav::Select: MenuModern_Select(list, tag, d.focus); return true;
        case Nav::Back: MenuModern_Press(kMmBack); return true;
        default: return false;
    }
}

void footer(PromptBar& bar, const std::string& build) {
    bar.prompts.clear();
    bar.info_title = T(PcText::MdFooterPort);
    bar.info_sub = L"SLUS-20579 · NTSC-U";
    char buf[128];
    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdFooterBuild), build.empty() ? "dev" : build.c_str());
    bar.info2_title = widen(buf);
    bar.info2_sub = T(PcText::MdFooterGame);
}

enum : int { kActSelect = 1, kActBack, kActStats };

}  // namespace

// ---------------------------------------------------------------- o fim de missão

FailScreen::FailScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

void FailScreen::open(const MmSnapshot& s) {
    active_ = true;
    closing_ = false;
    time_ = 0.0f;
    out_.snap(1.0f);
    snap_ = s;
    focus_ = std::clamp(s.fail_row, 0, std::max(0, int(s.fail_rows.size()) - 1));
    hover_ = -1;
    rects_.clear();
    lit_.assign(s.fail_rows.size(), Tween{});
    mark_ = FocusMark{};
    dialog_ = Dialog{};
    bar_ = PromptBar{};
    nav_.reset();
    for (bool& p : pending_) p = false;
    waiting_ = false;
}

void FailScreen::close() {
    if (!active_ || closing_) return;
    closing_ = true;
    out_.set(0.0f, tk_->motion.close * 1.6f, tk_->motion.ease_in);
}

float FailScreen::background() const {
    if (!active_) return 0.0f;
    return phase(time_, 0.0f, tk_->motion.fail_in, tk_->motion.ease_out) * out_.value();
}

void FailScreen::layout(const Frame& f) {
    const Tokens& tk = *tk_;
    const TextStyle& st = tk.type.menu_small;
    std::vector<float> w;
    float total = 0.0f;
    for (const MmItem& it : snap_.fail_rows) {
        w.push_back(text_width(f, widen(it.text), st));
        total += w.back();
    }
    const float gap = 96.0f;
    total += gap * float(std::max<size_t>(1, w.size()) - 1);
    float x = kCanvasW * 0.5f - total * 0.5f;
    const float cy = tk.space.hero_y + 214.0f;
    rects_.clear();
    for (const float ww : w) {
        rects_.push_back(Rect{x - 24.0f, cy - 30.0f, x + ww + 24.0f, cy + 30.0f});
        x += ww + gap;
    }
    if (lit_.size() != rects_.size()) lit_.assign(rects_.size(), Tween{});
}

void FailScreen::set_focus(int i, bool animate) {
    if (rects_.empty()) return;
    focus_ = std::clamp(i, 0, int(rects_.size()) - 1);
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const Rect& r = rects_[size_t(focus_)];
    mark_.move_to(r.x0 + 20.0f, r.x1 - 20.0f, r.cy() + 24.0f, f, animate);
    mark_.show(true, f);
    // A linha da lista do jogo acompanha (como cima/baixo nela).
    MenuModern_Pick(kMmFailList, kMmAnyTag, focus_);
}

void FailScreen::select(int i) {
    if (i < 0 || size_t(i) >= snap_.fail_rows.size()) return;
    // A linha da lista 0x100001a4 e o X: P_ENDMISSION_Handler lê o valor dela.
    MenuModern_Select(kMmFailList, kMmAnyTag, i);
    waiting_ = true;
}

void FailScreen::frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    out_.update(dt);
    mark_.update(dt);
    dialog_.update(dt);
    if (closing_) {
        if (out_.done()) active_ = closing_ = false;
        return;
    }
    snap_ = s;
    waiting_ = !MenuModern_Queue().empty();
    const Frame f{*tk_, cv, m, time_};
    layout(f);
    for (size_t i = 0; i < lit_.size(); ++i) {
        lit_[i].update(dt);
        lit_[i].set(int(i) == focus_ ? 1.0f : 0.0f, tk_->motion.focus, tk_->motion.ease_out);
    }
    if (!mark_.placed && !rects_.empty()) {
        const Rect& r = rects_[size_t(std::clamp(focus_, 0, int(rects_.size()) - 1))];
        mark_.move_to(r.x0 + 20.0f, r.x1 - 20.0f, r.cy() + 24.0f, f, false);
        mark_.show(true, f);
    }
    if (s.dialog.open && !dialog_.open) {
        show_box(dialog_, s, f);
        dialog_list_ = s.dialog.list;
        dialog_tag_ = s.dialog.tag;
        nav_.reset();
    } else if (!s.dialog.open && dialog_.open) {
        dialog_.hide(f);
    }
    if (dialog_.open) dialog_.layout(f);
    // O Start do jogo também escolhe no fim de missão (status 4): Enter escolhe.
    nav_.read(in, cv, dt, *tk_, true);
    for (int i = 0; i < kNavCount; ++i) {
        if (pending_[i]) nav_.inject(Nav(i));
        pending_[i] = false;
    }
    footer(bar_, build_);
    const bool pad = nav_.device == 1;
    const std::wstring sel = bind_key("menu_select");
    bar_.prompts.push_back(Prompt{sel.empty() ? L"Enter" : sel, pad ? std::string("xbox_a") : std::string(), T(PcText::MdPromptSelect),
                                  kActSelect});
    bar_.layout(f);
    // As opções só respondem depois de aparecerem.
    const bool ready = s.ready && !waiting_ && time_ >= tk_->motion.fail_options;
    if (!ready) return;
    if (dialog_.open) {
        for (int i = 0; i < kNavCount; ++i) {
            if (nav_.fire(Nav(i)) && dialog_command(dialog_, Nav(i), dialog_list_, dialog_tag_, f)) {
                waiting_ = true;
                break;
            }
        }
        if (!waiting_ && nav_.mouse_active && nav_.inside) {
            const int h = dialog_.hit(nav_.mx, nav_.my);
            dialog_.hover = h;
            if (h >= 0 && nav_.pressed) {
                dialog_.focus = h;
                MenuModern_Select(dialog_list_, dialog_tag_, h);
                waiting_ = true;
            }
        }
        return;
    }
    const int n = int(rects_.size());
    if (nav_.fire(Nav::Left) || nav_.fire(Nav::Up)) {
        if (focus_ > 0) set_focus(focus_ - 1, true);
    } else if (nav_.fire(Nav::Right) || nav_.fire(Nav::Down)) {
        if (focus_ + 1 < n) set_focus(focus_ + 1, true);
    } else if (nav_.fire(Nav::Select) || nav_.fire(Nav::Start)) {
        select(focus_);
        return;
    }
    if (nav_.mouse_active && nav_.inside) {
        hover_ = -1;
        for (int i = 0; i < n; ++i) {
            if (rects_[size_t(i)].contains(nav_.mx, nav_.my)) hover_ = i;
        }
        if (hover_ >= 0 && (nav_.moved || nav_.pressed) && hover_ != focus_) set_focus(hover_, true);
        if (hover_ >= 0 && nav_.pressed) select(hover_);
        if (nav_.pressed && bar_.hit(nav_.mx, nav_.my) == kActSelect) select(focus_);
    }
}

void FailScreen::build_back(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    // O fundo, que aparece devagar, por cima da cena desfocada e da cortina de sangue do jogo (a cortina escurece a
    // cena: precisa dela clara por trás; render/ui_modern_app.cpp a desenha antes): um véu leve na janela inteira, que
    // deixa a cortina à vista em cima, e uma faixa funda atrás do texto.
    const float veil = phase(time_, 0.0f, tk.motion.fail_in, tk.motion.ease_out) * out_.value();
    draw_background(dl, f, veil * 0.35f);
    const Color band = tk.color.veil.with_alpha(veil * 1.3f);
    // As emendas da faixa caem em pixels inteiros da janela: bordas suavizadas lado a lado deixariam uma fresta clara.
    const auto snap = [&](float y) { return canvas_y(cv, std::round(window_y(cv, y))); };
    const float y0 = snap(190.0f), y1 = snap(372.0f), y2 = snap(790.0f);
    dl.gradient(Rect{cv.left, y0, cv.right, y1}, band.with_alpha(0.0f), band, false);
    dl.fill(Rect{cv.left, y1, cv.right, y2}, band);
    dl.gradient(Rect{cv.left, y2, cv.right, cv.bottom}, band, band.with_alpha(0.6f), false);
}

void FailScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    const float out = out_.value();
    const float cx = kCanvasW * 0.5f, hy = tk.space.hero_y;
    // O título: aparece e o espaço entre as letras abre devagar.
    const float tt = std::clamp((time_ - 0.15f) / tk.motion.fail_title, 0.0f, 1.0f);
    const float ta = curve_eval(tk.motion.ease_out, std::min(1.0f, tt * 1.7f)) * out;
    TextStyle hs = tk.type.hero;
    hs.tracking = tk.type.hero.tracking * (0.55f + 0.45f * curve_eval(tk.motion.ease_out, tt));
    std::wstring title = widen(snap_.fail_title);
    if (title.empty()) title = L"Mission Failed";
    hs = fit_style(f, title, hs, 1500.0f, 48.0f);
    line_at(dl, cx, hy, title, hs, tk.color.text.with_alpha(ta), Align::Centre);
    // As linhas finas que crescem dos lados do título, com um losango na ponta.
    const float tw = text_width(f, title, hs);
    const float lt = phase(time_, 0.55f, 1.1f, tk.motion.ease_out) * out;
    if (lt > 0.0f) {
        const float len = 220.0f * lt;
        const float gap = 44.0f;
        const Color lc = tk.color.accent.with_alpha(0.55f * lt);
        dl.line(cx - tw * 0.5f - gap - len, hy, cx - tw * 0.5f - gap, hy, lc, 1.0f);
        dl.line(cx + tw * 0.5f + gap, hy, cx + tw * 0.5f + gap + len, hy, lc, 1.0f);
        const float d = 4.0f;
        dl.diamond(Rect{cx - tw * 0.5f - gap - len - d, hy - d, cx - tw * 0.5f - gap - len + d, hy + d}, tk.color.accent.with_alpha(lt));
        dl.diamond(Rect{cx + tw * 0.5f + gap + len - d, hy - d, cx + tw * 0.5f + gap + len + d, hy + d}, tk.color.accent.with_alpha(lt));
    }
    // Em cima, a missão; embaixo, o motivo do jogo.
    const float ka = phase(time_, 0.35f, 0.8f, tk.motion.ease_out) * out;
    std::wstring kicker = widen(snap_.mission);
    if (snap_.level > 0 && snap_.levels > 1) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MpLevel), snap_.level, snap_.levels);
        kicker += (kicker.empty() ? L"" : L"   ·   ") + widen(buf);
    }
    line_at(dl, cx, hy - 104.0f + 6.0f * (1.0f - ka), kicker, tk.type.header, tk.color.dim.with_alpha(ka), Align::Centre);
    const float ra = phase(time_, 0.7f, 0.8f, tk.motion.ease_out) * out;
    if (!snap_.fail_reason.empty()) {
        line_at(dl, cx, hy + 86.0f + 8.0f * (1.0f - ra), widen(snap_.fail_reason), tk.type.hero_sub, tk.color.text_2.with_alpha(ra),
                Align::Centre);
    }
    // As opções do jogo, lado a lado, com o foco das linhas.
    const float oa = phase(time_, tk.motion.fail_options, 0.5f, tk.motion.ease_out) * out;
    const float oy = 12.0f * (1.0f - oa);
    for (size_t i = 0; i < snap_.fail_rows.size() && i < rects_.size(); ++i) {
        const Rect r = rects_[i].offset(0.0f, oy);
        const float t = i < lit_.size() ? lit_[i].value() : 0.0f;
        Color c = mix(tk.color.text_2, tk.color.accent, t);
        if (int(i) == hover_ && int(i) != focus_) c = tk.color.text;
        line_at(dl, r.x0 + 24.0f, r.cy(), widen(snap_.fail_rows[i].text), tk.type.menu_small, c.with_alpha(oa));
    }
    if (oa > 0.0f) mark_.draw(dl, f, oa, oy);
    // O que a opção com o foco faz.
    if (focus_ >= 0 && size_t(focus_) < snap_.fail_rows.size()) {
        line_at(dl, cx, hy + 296.0f + oy, T(fail_sub(snap_.fail_rows[size_t(focus_)].data)), tk.type.body,
                tk.color.dim.with_alpha(oa), Align::Centre);
    }
    bar_.draw(dl, f, oa);
    dialog_.draw(dl, f);
}

std::string FailScreen::describe() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "fim_moderno ativo %d fechando %d tempo %.2f foco %d opcoes %zu motivo \"%s\" esperando %d",
                  active_ ? 1 : 0, closing_ ? 1 : 0, double(time_), focus_, snap_.fail_rows.size(), snap_.fail_reason.c_str(),
                  waiting_ ? 1 : 0);
    return buf;
}

// ---------------------------------------------------------------- o resultado

ResultsScreen::ResultsScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

ResultsScreen::View ResultsScreen::view_of(uint32_t page) {
    if (page == kMmStatsPage) return View::Stats;
    if (page == kMmBonusPage) return View::Bonus;
    return View::Results;
}

void ResultsScreen::open(const MmSnapshot& s) {
    active_ = true;
    closing_ = false;
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.results_in, tk_->motion.ease_out);
    content_.snap(1.0f);
    snap_ = s;
    view_ = view_of(s.base_page);
    tabs_ = TabBar{};
    menu_ = BigMenu{};
    bar_ = PromptBar{};
    dialog_ = Dialog{};
    toast_ = Toast{};
    nav_.reset();
    for (bool& p : pending_) p = false;
    waiting_ = false;
    set_items();
}

void ResultsScreen::close() {
    if (!active_ || closing_) return;
    closing_ = true;
    appear_.set(0.0f, tk_->motion.close, tk_->motion.ease_in);
}

void ResultsScreen::set_items() {
    const int keep = menu_.focus;
    menu_.items.clear();
    menu_.x = tk_->space.left_x;
    if (view_ == View::Stats) {
        menu_.items.push_back(BigMenu::Item{T(PcText::MdPromptBack), std::wstring(), true, false});
    } else {
        menu_.items.push_back(BigMenu::Item{T(PcText::MrContinue), T(PcText::MrContinueSub), true, false});
        menu_.items.push_back(BigMenu::Item{T(PcText::MrStats), T(PcText::MrStatsSub), true, false});
        menu_.items.push_back(BigMenu::Item{T(PcText::MrDossier), T(PcText::MrDossierSub), true, false});
    }
    menu_.focus = std::clamp(keep, 0, int(menu_.items.size()) - 1);
}

void ResultsScreen::activate(int i) {
    if (view_ == View::Stats) {
        // Voltar das estatísticas: o voltar do jogo (o 0x5f do gerente desempilha o resultado).
        MenuModern_Press(kMmBack);
        waiting_ = true;
        return;
    }
    switch (i) {
        case 0:
            // Continuar: o X no botão da página (P_NFRESULTS_Handler / P_NFBONUS_Handler, 0x4b).
            MenuModern_Press(kMmSelect);
            waiting_ = true;
            break;
        case 1:
            // As estatísticas: o círculo (0x5e).
            MenuModern_Press(kMmCircle);
            waiting_ = true;
            break;
        default:
            // O dossiê: o quadrado (0x5d; P_NFRESULTS_Handler / P_NFBONUS_Handler mostram 0x4000002b, que fica no estilo
            // Original até o menu principal ganhar o Modern; voltar dele reabre esta tela).
            MenuModern_Press(kMmSquare);
            waiting_ = true;
            break;
    }
}

void ResultsScreen::act(Nav c) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (dialog_.open) {
        if (dialog_command(dialog_, c, dialog_list_, dialog_tag_, f)) waiting_ = true;
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
        case Nav::TabNext:
            if (view_ != View::Stats) activate(1);
            else toast_.show(T(PcText::MrToastDossier));
            break;
        case Nav::TabPrev:
        case Nav::Back:
            if (view_ == View::Stats) activate(0);
            break;
        default: break;
    }
}

void ResultsScreen::frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    content_.update(dt);
    tabs_.update(dt);
    dialog_.update(dt);
    toast_.update(dt, *tk_);
    const Frame f{*tk_, cv, m, time_};
    menu_.update(dt, f);
    if (closing_) {
        if (appear_.done()) active_ = closing_ = false;
        return;
    }
    snap_ = s;
    waiting_ = !MenuModern_Queue().empty();
    // A página do jogo decide a vista (o resultado, as estatísticas, o bônus).
    const View v = view_of(s.base_page);
    if (v != view_) {
        view_ = v;
        menu_.focus = 0;
        set_items();
        content_.snap(0.0f);
        content_.set(1.0f, tk_->motion.content, tk_->motion.ease_out);
        menu_.mark = FocusMark{};
        nav_.reset();
    }
    // A barra de cima: RESULTS · STATS · DOSSIER.
    tabs_.labels = {widen(s.results_title.empty() ? std::string("Results") : s.results_title), T(PcText::MrStats), T(PcText::MrDossier)};
    tabs_.locked = {false, false, false};
    const bool pad = in.device == 1;
    const std::wstring kp = bind_key("menu_tab_prev"), kn = bind_key("menu_tab_next");
    tabs_.key_prev = kp.empty() ? L"Q" : kp;
    tabs_.key_next = kn.empty() ? L"E" : kn;
    tabs_.glyph_prev = pad ? bind_glyph("menu_tab_prev", "xbox_lb") : std::string();
    tabs_.glyph_next = pad ? bind_glyph("menu_tab_next", "xbox_rb") : std::string();
    tabs_.corner_left = L"NF.PC / RES";
    tabs_.corner_right = L"SLUS-20579";
    const bool first = !tabs_.laid_out;
    tabs_.layout(f);
    const int active_tab = view_ == View::Stats ? 1 : 0;
    if (first) tabs_.set_active(active_tab, f, false);
    else if (tabs_.active != active_tab) tabs_.set_active(active_tab, f, true);
    menu_.y = view_ == View::Stats ? 700.0f : 660.0f;
    menu_.focused = !dialog_.open;
    menu_.layout(f);
    if (!menu_.mark.placed) menu_.set_focus(menu_.focus, f, false);
    // A caixa de mensagem do jogo.
    if (s.dialog.open && !dialog_.open) {
        show_box(dialog_, s, f);
        dialog_list_ = s.dialog.list;
        dialog_tag_ = s.dialog.tag;
        nav_.reset();
    } else if (!s.dialog.open && dialog_.open) {
        dialog_.hide(f);
    }
    if (dialog_.open) dialog_.layout(f);
    // O Start também escolhe no menu principal (status 5): Enter escolhe.
    nav_.read(in, cv, dt, *tk_, true);
    for (int i = 0; i < kNavCount; ++i) {
        if (pending_[i]) nav_.inject(Nav(i));
        pending_[i] = false;
    }
    footer(bar_, build_);
    const std::wstring sel = bind_key("menu_select");
    if (view_ == View::Stats) {
        bar_.prompts.push_back(Prompt{L"Esc", pad ? std::string("xbox_b") : std::string(), T(PcText::MdPromptBack), kActBack});
    } else {
        bar_.prompts.push_back(Prompt{sel.empty() ? L"Enter" : sel, pad ? std::string("xbox_a") : std::string(), T(PcText::MdPromptSelect),
                                      kActSelect});
    }
    bar_.layout(f);
    if (!s.ready || waiting_) return;
    for (int i = 0; i < kNavCount; ++i) {
        if (!nav_.fire(Nav(i))) continue;
        act(Nav(i));
        if (waiting_) return;
    }
    if (!nav_.mouse_active || !nav_.inside) return;
    const float x = nav_.mx, y = nav_.my;
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && nav_.pressed) {
            MenuModern_Select(dialog_list_, dialog_tag_, h);
            waiting_ = true;
        }
        return;
    }
    if (nav_.back) {
        act(Nav::Back);
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
        const int t = tabs_.hit(x, y);
        if ((t == 1 || t == -3) && view_ != View::Stats) activate(1);
        if ((t == 0 || t == -2) && view_ == View::Stats) activate(0);
        // Das estatísticas o jogo não abre o dossiê (P_NFSTATS_Handler não trata o quadrado): o aviso.
        if (t == 2 && view_ != View::Stats) activate(2);
        if (t == 2 && view_ == View::Stats) toast_.show(T(PcText::MrToastDossier));
        switch (bar_.hit(x, y)) {
            case kActSelect: activate(menu_.focus); break;
            case kActBack: act(Nav::Back); break;
            default: break;
        }
    }
}

void ResultsScreen::draw_results(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmSnapshot& s = snap_;
    // À esquerda: o título, a missão e a descrição dela; embaixo, as escolhas.
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, widen(s.results_title.empty() ? std::string("Results") : s.results_title) + L" /",
            tk.type.header, tk.color.dim.with_alpha(alpha));
    TextStyle hs = tk.type.hero;
    hs.size = 64.0f;
    hs.tracking = 0.12f;
    std::wstring title = widen(s.complete_title.empty() ? std::string("Mission Complete") : s.complete_title);
    hs = fit_style(f, title, hs, 900.0f, 40.0f);
    line_at(dl, sp.left_x - 4.0f + dx, 236.0f, title, hs, tk.color.text.with_alpha(alpha));
    const float tw = std::min(900.0f, text_width(f, title, hs));
    dl.fill(Rect{sp.left_x + dx, 290.0f, sp.left_x + 64.0f + dx, 292.0f}, tk.color.accent.with_alpha(alpha));
    dl.line(sp.left_x + 64.0f + dx, 291.0f, sp.left_x + tw + dx, 291.0f, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    float y = 340.0f;
    if (!s.mission.empty()) {
        line_at(dl, sp.left_x + dx, y, widen(s.mission), tk.type.hero_sub, tk.color.accent.with_alpha(alpha));
        y += 44.0f;
    }
    for (const std::wstring& l : wrap_lines(f, widen(s.mission_text), tk.type.body, 620.0f)) {
        line_at(dl, sp.left_x + dx, y, l, tk.type.body, tk.color.text_2.with_alpha(alpha));
        y += tk.type.body.size * tk.type.body.line;
    }
    menu_.draw(dl, f, alpha, dx);
    // À direita: a pontuação, a meta e a medalha que o jogo escreveu.
    const float x0 = 1180.0f - dx, x1 = 1760.0f - dx;
    float ry = 236.0f;
    const auto block = [&](const std::wstring& label, const std::wstring& value, bool big) {
        line_at(dl, x0, ry, strip_colon(label), tk.type.section, tk.color.dim.with_alpha(alpha));
        ry += big ? 68.0f : 54.0f;
        TextStyle vs = tk.type.stat;
        if (!big) vs.size = 52.0f;
        line_at(dl, x0 - 3.0f, ry, value.empty() ? std::wstring(L"—") : value, vs, tk.color.text.with_alpha(alpha));
        ry += big ? 70.0f : 58.0f;
        draw_hairline(dl, f, x0, x1, ry, alpha);
        ry += 44.0f;
    };
    block(widen(s.score_label.empty() ? std::string("Total Score") : s.score_label), widen(s.score_value), true);
    block(widen(s.target_label.empty() ? std::string("Gold Medal Target") : s.target_label), widen(s.target_value), false);
    line_at(dl, x0, ry, T(PcText::MrMedal), tk.type.section, tk.color.dim.with_alpha(alpha));
    ry += 44.0f;
    if (!s.medal.empty()) {
        for (const std::wstring& l : wrap_lines(f, widen(s.medal), tk.type.meta, x1 - x0)) {
            line_at(dl, x0, ry, l, tk.type.meta, tk.color.text.with_alpha(alpha));
            ry += tk.type.meta.size * 1.4f;
        }
    } else {
        line_at(dl, x0, ry, T(PcText::MrNoMedal), tk.type.meta, tk.color.dim.with_alpha(alpha));
        ry += tk.type.meta.size * 1.4f;
    }
}

void ResultsScreen::draw_stats(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmSnapshot& s = snap_;
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, T(PcText::MrStats) + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    if (!s.mission.empty()) line_at(dl, sp.left_x + dx, 236.0f, widen(s.mission), tk.type.hero_sub, tk.color.accent.with_alpha(alpha));
    menu_.draw(dl, f, alpha, dx);
    // A tabela: as colunas do jogo (Category, Current, Target, TOTAL) e as linhas da lista dele (P_NFSTATS_Handler
    // as escreve com PlrStat_GetScore), ou as categorias com "—" se a lista ainda não existe.
    const float x0 = sp.mid_x0 + dx, x1 = sp.desc_x1 + dx;
    const float cols[3] = {x1 - 380.0f, x1 - 200.0f, x1 - sp.pad_r};
    float y = sp.list_top + 34.0f;
    const TextStyle& hs = tk.type.table_head;
    for (size_t k = 0; k < s.stats_head.size() && k < 4; ++k) {
        const std::wstring h = to_upper(widen(s.stats_head[k]));
        if (k == 0) line_at(dl, x0 + sp.pad_l, y, h, hs, tk.color.dim.with_alpha(alpha));
        else line_at(dl, cols[k - 1], y, h, hs, tk.color.dim.with_alpha(alpha), Align::Right);
    }
    y += 26.0f;
    draw_hairline(dl, f, x0, x1, y, alpha);
    const float rh = 52.0f;
    std::vector<std::vector<std::string>> rows = s.stats;
    if (rows.empty()) {
        for (const std::string& n : s.stats_names) rows.push_back({n, "", "", ""});
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const float cy = y + rh * 0.5f;
        const std::vector<std::string>& r = rows[i];
        const bool total = i + 1 == rows.size();
        const TextStyle& cs = tk.type.cell;
        for (size_t k = 0; k < r.size() && k < 4; ++k) {
            std::wstring v = widen(r[k]);
            const bool empty = v.empty() || v == L" ";
            if (k > 0 && empty) v = L"—";
            const Color c = k == 0 ? (total ? tk.color.accent : tk.color.text) : (empty ? tk.color.disabled : tk.color.text_2);
            if (k == 0) line_at(dl, x0 + sp.pad_l, cy, v, cs, c.with_alpha(alpha));
            else line_at(dl, cols[k - 1], cy, v, cs, c.with_alpha(alpha), Align::Right);
        }
        y += rh;
        draw_hairline(dl, f, x0, x1, y, alpha * (total ? 1.6f : 1.0f));
    }
}

void ResultsScreen::draw_bonus(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, T(PcText::MrBonus) + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    float y = 236.0f;
    for (const std::wstring& l : wrap_lines(f, widen(snap_.bonus), tk.type.hero_sub, 1200.0f)) {
        line_at(dl, sp.left_x + dx, y, l, tk.type.hero_sub, tk.color.text.with_alpha(alpha));
        y += 44.0f;
    }
    menu_.draw(dl, f, alpha, dx);
}

void ResultsScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    const float k = appear_.value();
    // O fundo é desta tela (o menu principal não tem cena): opaco, quente, com a vinheta e o 007 do jogo bem fraco.
    const Rect win{cv.left, cv.top, cv.right, cv.bottom};
    dl.fill(win, tk.color.ground.with_alpha(k));
    draw_background(dl, f, k);
    dl.glyph(Rect{1010.0f, 330.0f, 1910.0f, 778.0f}, "results_007", (view_ == View::Stats ? 0.022f : 0.034f) * k);
    tabs_.draw(dl, f, k);
    const float c = content_.value();
    const float a = k * (0.25f + 0.75f * c);
    const float dx = tk.space.slide * (1.0f - k) + 12.0f * (1.0f - c);
    switch (view_) {
        case View::Results: draw_results(dl, f, a, dx); break;
        case View::Stats: draw_stats(dl, f, a, dx); break;
        case View::Bonus: draw_bonus(dl, f, a, dx); break;
    }
    bar_.draw(dl, f, k);
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

// ---------------------------------------------------------------- o aviso de controle desconectado

namespace {
// O texto do jogo (o rótulo 0x3c9 com a porta; a primeira letra maiúscula) e o que fazer no PC: o controle de volta, ou
// uma tecla (psi_input: o teclado usado conecta o jogador 0 de novo), e o jogo volta à pausa.
std::wstring warning_body(const MmSnapshot& s) {
    std::wstring b = widen(s.dialog.body);
    if (!b.empty() && b[0] >= L'a' && b[0] <= L'z') b[0] = wchar_t(b[0] - L'a' + L'A');
    return b + L"\n" + T(PcText::FeWarningHint);
}
}  // namespace

WarningScreen::WarningScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

void WarningScreen::open(const MmSnapshot& s) {
    active_ = true;
    closing_ = false;
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.open, tk_->motion.ease_out);
    dialog_ = Dialog{};
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    dialog_.show(T(PcText::FeWarningTitle), warning_body(s), {}, 0, f);
    dialog_.busy = true;
}

void WarningScreen::close() {
    if (!active_ || closing_) return;
    closing_ = true;
    appear_.set(0.0f, tk_->motion.close, tk_->motion.ease_in);
    dialog_.hide(Frame{*tk_, last_cv_, Measure{}, time_});
}

void WarningScreen::frame(float dt, const MmSnapshot& s, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    dialog_.update(dt);
    if (closing_) {
        if (appear_.done()) active_ = closing_ = false;
        return;
    }
    // O jogo troca o texto (o número da porta, o multitap): a caixa acompanha.
    const std::wstring body = warning_body(s);
    if (!s.dialog.body.empty() && body != dialog_.body) dialog_.body = body;
    const Frame f{*tk_, cv, m, time_};
    dialog_.layout(f);
}

void WarningScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Frame f{*tk_, cv, m, time_};
    draw_background(dl, f, appear_.value());
    dialog_.draw(dl, f);
}

std::string WarningScreen::describe() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "aviso_moderno ativo %d fechando %d texto %zu", active_ ? 1 : 0, closing_ ? 1 : 0,
                  dialog_.body.size());
    return buf;
}

std::string ResultsScreen::describe() const {
    char buf[256];
    static const char* const kViews[] = {"resultado", "estatisticas", "bonus"};
    std::snprintf(buf, sizeof(buf), "resultado_moderno ativo %d fechando %d vista %s foco %d dialogo %d esperando %d", active_ ? 1 : 0,
                  closing_ ? 1 : 0, kViews[int(view_)], menu_.focus, dialog_.open ? 1 : 0, waiting_ ? 1 : 0);
    return buf;
}

}  // namespace nf::ui
