// A pausa no estilo Modern. Ver modern_pause.h e docs/design.md.

#include "ui/modern_pause.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "engine/pc_text.h"
#include "platform/input_bindings.h"
#include "platform/psi_input.h"

namespace nf::ui {

namespace {

std::wstring T(PcText id) { return widen(PcText_Utf8(id)); }

std::wstring fmt(PcText id, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, id);
    std::vsnprintf(buf, sizeof(buf), PcText_Utf8(id), ap);
    va_end(ap);
    return widen(buf);
}

// A tag do botão da aba do jogo de cada aba (-1: OPTIONS, só do port).
int game_tag(PauseScreen::Tab t) {
    switch (t) {
        case PauseScreen::Tab::Pause: return int(kMmTabMission);
        case PauseScreen::Tab::Objectives: return int(kMmTabObjectives);
        case PauseScreen::Tab::Controls: return int(kMmTabControls);
        case PauseScreen::Tab::Score: return int(kMmTabScore);
        default: return -1;
    }
}

// As ações da barra de baixo.
enum : int { kActSelect = 1, kActBack, kActResume };

// A linha i da lista principal do jogo: o rótulo, o subtítulo e se é a linha OPTIONS do port (valor 3).
struct PauseRow {
    std::wstring label, sub;
    bool options = false;
    int game_row = 0;
};

std::vector<PauseRow> pause_rows(const MmSnapshot& s) {
    std::vector<PauseRow> out;
    for (size_t i = 0; i < s.rows.size(); ++i) {
        PauseRow r;
        r.game_row = int(i);
        if (s.rows[i].data == 3) {
            r.label = T(PcText::MpOptions);
            r.sub = T(PcText::MpOptionsSub);
            r.options = true;
        } else if (i == 0) {
            r.label = T(PcText::MpResume);
            r.sub = T(PcText::MpResumeSub);
        } else if (i == 1) {
            r.label = T(PcText::MpRestart);
            r.sub = T(PcText::MpRestartSub);
        } else if (i == 2) {
            r.label = T(PcText::MpQuit);
            r.sub = T(PcText::MpQuitSub);
        } else {
            r.label = widen(s.rows[i].text);
        }
        out.push_back(r);
    }
    // Sem a linha do port (testes, outro arquivo): OPTIONS no fim, só da tela.
    bool has_options = false;
    for (const PauseRow& r : out) has_options = has_options || r.options;
    if (!has_options) out.push_back(PauseRow{T(PcText::MpOptions), T(PcText::MpOptionsSub), true, -1});
    return out;
}

// O objetivo atual: o primeiro em andamento.
const MmObjective* current_objective(const MmSnapshot& s) {
    for (const MmObjective& o : s.objectives) {
        if (o.state == MmObjState::Active) return &o;
    }
    return nullptr;
}

std::wstring device_text() {
    const int pad = psiInput_PadIndex();
    if (pad < 0) return T(PcText::CtlKeyboardMouse);
    return fmt(psiInput_ActiveDevice() == 1 ? PcText::CtlPad : PcText::CtlKeyboardPad, pad + 1);
}

// Uma linha de texto centrada na vertical em cy.
void line_at(DrawList& dl, float x, float cy, const std::wstring& s, const TextStyle& st, const Color& c, Align a = Align::Left,
             float w = 4000.0f) {
    const float h = st.size * 1.6f;
    Rect r{x, cy - h * 0.5f, x + w, cy + h * 0.5f};
    if (a == Align::Right) r = Rect{x - w, cy - h * 0.5f, x, cy + h * 0.5f};
    if (a == Align::Centre) r = Rect{x - w * 0.5f, cy - h * 0.5f, x + w * 0.5f, cy + h * 0.5f};
    dl.text(r, st.caps ? to_upper(s) : s, st, c, a);
}

// O ícone da situação de um objetivo, centrado em (cx, cy).
void objective_icon(DrawList& dl, const Tokens& tk, MmObjState st, float cx, float cy, float a) {
    switch (st) {
        case MmObjState::Done:
            dl.glow(Rect{cx - 16.0f, cy - 16.0f, cx + 16.0f, cy + 16.0f}, tk.color.ember.with_alpha(0.30f * a),
                    tk.color.ember_deep.with_alpha(0.0f), cy + 16.0f);
            dl.line(cx - 7.0f, cy + 0.5f, cx - 2.0f, cy + 5.5f, tk.color.accent.with_alpha(a), 2.2f);
            dl.line(cx - 2.0f, cy + 5.5f, cx + 8.0f, cy - 6.0f, tk.color.accent.with_alpha(a), 2.2f);
            break;
        case MmObjState::Failed:
            dl.line(cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 6.0f, tk.color.dim.with_alpha(a), 2.0f);
            dl.line(cx - 6.0f, cy + 6.0f, cx + 6.0f, cy - 6.0f, tk.color.dim.with_alpha(a), 2.0f);
            break;
        case MmObjState::Active:
            dl.ellipse(Rect{cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f}, tk.color.text.with_alpha(0.85f * a), 1.6f);
            dl.ellipse(Rect{cx - 2.5f, cy - 2.5f, cx + 2.5f, cy + 2.5f}, tk.color.accent.with_alpha(a));
            break;
        case MmObjState::Hidden:
            dl.padlock(Rect{cx - 6.0f, cy - 8.0f, cx + 6.0f, cy + 7.0f}, tk.color.disabled.with_alpha(a));
            break;
    }
}

constexpr float kObjIndent = 52.0f;  // o texto do objetivo depois do ícone
constexpr int kControlRows = 2;      // o esquema e o eixo Y

}  // namespace

bool PauseScreen::parse_tab(const std::string& name, Tab* out) {
    static const struct {
        const char* name;
        Tab tab;
    } kNames[] = {{"pause", Tab::Pause}, {"mission", Tab::Pause}, {"objectives", Tab::Objectives},
                  {"controls", Tab::Controls}, {"buttons", Tab::Controls}, {"score", Tab::Score},
                  {"options", Tab::Options}};
    for (const auto& n : kNames) {
        if (name == n.name) {
            *out = n.tab;
            return true;
        }
    }
    return false;
}

const char* PauseScreen::tab_name(Tab t) {
    switch (t) {
        case Tab::Pause: return "pause";
        case Tab::Objectives: return "objectives";
        case Tab::Controls: return "controls";
        case Tab::Score: return "score";
        case Tab::Options: return "options";
    }
    return "";
}

PauseScreen::PauseScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()), options_(tokens) {
    options_.set_embedded(true);
}

void PauseScreen::set_tokens(const Tokens* tokens) {
    tk_ = tokens != nullptr ? tokens : &default_tokens();
    options_.set_tokens(tokens);
}

void PauseScreen::set_build(const std::string& build) {
    build_ = build;
    options_.set_build(build);
}

// ---------------------------------------------------------------- abrir, fechar, abas

void PauseScreen::open(const MmSnapshot& s, Tab tab, Cat cat) {
    active_ = true;
    closing_ = false;
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.open, tk_->motion.ease_out);
    content_.snap(1.0f);
    snap_ = s;
    // As abas do jogo na ordem dele (MISSION, OBJECTIVES, CONTROLS, SCORE: a pausa do jogo mostra SCORE também com um
    // jogador, as estatísticas da missão) e a OPTIONS do port.
    tabs_order_ = {Tab::Pause, Tab::Objectives, Tab::Controls, Tab::Score, Tab::Options};
    bar_tabs_ = TabBar{};
    bar_ = PromptBar{};
    menu_ = BigMenu{};
    menu_.x = tk_->space.left_x;
    menu_.y = tk_->space.menu_y;
    mark_ = FocusMark{};
    dialog_ = Dialog{};
    dialog_box_ = false;
    toast_ = Toast{};
    nav_.reset();
    for (bool& p : pending_nav_) p = false;
    hover_ = hover_tab_ = -1;
    waiting_ = false;
    focus_ = 0;
    scroll_.snap(0.0f);
    scroll_target_ = 0.0f;
    tab_ = 0;
    for (size_t i = 0; i < tabs_order_.size(); ++i) {
        if (tabs_order_[i] == tab) tab_ = int(i);
    }
    sync_rows(s);
    menu_.focus = 0;  // a pausa abre na linha 0 (CONTINUE), como a do jogo
    if (this->tab() == Tab::Options) options_.open(cat);
    const int tag = game_tag(this->tab());
    if (tag >= 0 && tag != s.tab) MenuModern_Tab(uint32_t(tag));
}

void PauseScreen::show_tab(Tab tab, Cat cat) {
    for (size_t i = 0; i < tabs_order_.size(); ++i) {
        if (tabs_order_[i] != tab) continue;
        if (int(i) != tab_) set_tab(int(i), true);
        if (tab == Tab::Options && options_.active() && options_.category() != cat) options_.open(cat);
        return;
    }
}

void PauseScreen::close() {
    if (!active_ || closing_) return;
    closing_ = true;
    if (options_.active()) options_.force_close();
    dialog_.hide(Frame{*tk_, last_cv_, Measure{}, time_});
    appear_.set(0.0f, tk_->motion.close, tk_->motion.ease_in);
}

void PauseScreen::force_close() {
    if (options_.active()) options_.force_close();
    active_ = false;
    closing_ = false;
}

float PauseScreen::background() const { return active_ ? appear_.value() : 0.0f; }

bool PauseScreen::mouse_active() const {
    if (tab() == Tab::Options && options_.active()) return options_.mouse_active() || nav_.mouse_active;
    return nav_.mouse_active;
}

void PauseScreen::set_tab(int index, bool animate) {
    if (tabs_order_.empty()) return;
    index = std::clamp(index, 0, int(tabs_order_.size()) - 1);
    if (index == tab_) return;
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const Tab before = tab();
    if (before == Tab::Options && options_.active()) options_.force_close();
    tab_ = index;
    bar_tabs_.set_active(index, f, animate);
    content_.snap(0.0f);
    content_.set(1.0f, tk_->motion.content, tk_->motion.ease_out);
    focus_ = 0;
    scroll_.snap(0.0f);
    scroll_target_ = 0.0f;
    hover_ = -1;
    mark_ = FocusMark{};
    if (tab() == Tab::Options) options_.open(options_.category() < Cat::Count ? options_.category() : Cat::Video);
    // A aba do jogo acompanha (o foco no botão dela; o C_GCPAUSE_Handler monta o painel).
    const int tag = game_tag(tab());
    if (tag >= 0 && tag != snap_.tab) MenuModern_Tab(uint32_t(tag));
    if (tab() == Tab::Pause) menu_.set_focus(menu_.focus, f, false);
    place_mark(false);
}

// ---------------------------------------------------------------- conteúdo

void PauseScreen::sync_rows(const MmSnapshot& s) {
    const std::vector<PauseRow> rows = pause_rows(s);
    menu_.items.clear();
    for (const PauseRow& r : rows) menu_.items.push_back(BigMenu::Item{r.label, r.sub, r.options, false});
    if (menu_.focus >= int(menu_.items.size())) menu_.focus = std::max(0, int(menu_.items.size()) - 1);
}

int PauseScreen::row_count() const {
    switch (tab()) {
        case Tab::Pause: return int(menu_.items.size());
        case Tab::Objectives: return int(snap_.objectives.size());
        case Tab::Controls: return kControlRows;
        case Tab::Score: return 0;  // a tabela só se lê
        default: return 0;
    }
}

float PauseScreen::list_y(int row) const {
    const auto& sp = tk_->space;
    switch (tab()) {
        case Tab::Objectives: return sp.section_h + float(row) * sp.row_h;
        case Tab::Controls: return sp.section_h + float(row) * sp.row_h;
        default: return float(row) * sp.row_h;
    }
}

Rect PauseScreen::row_rect(int i) const {
    const auto& sp = tk_->space;
    if (tab() == Tab::Pause) return i >= 0 && size_t(i) < menu_.rects.size() ? menu_.rects[size_t(i)] : Rect{};
    const float y = sp.list_top + list_y(i) - scroll_.value();
    return Rect{sp.mid_x0, y, sp.mid_x1, y + sp.row_h};
}

Rect PauseScreen::tab_rect(int i) const {
    return i >= 0 && size_t(i) < bar_tabs_.tab_rects.size() ? bar_tabs_.tab_rects[size_t(i)] : Rect{};
}

void PauseScreen::ensure_visible(int row) {
    const auto& sp = tk_->space;
    const float view = sp.list_bottom - sp.list_top;
    float y0 = list_y(row), y1 = y0 + sp.row_h;
    if (row == 0) y0 = 0.0f;  // a seção de cima vem junto
    float t = scroll_target_;
    if (y0 - 24.0f < t) t = y0 - 24.0f;
    if (y1 + 24.0f > t + view) t = y1 + 24.0f - view;
    const float total = list_y(row_count());
    t = std::clamp(t, 0.0f, std::max(0.0f, total - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void PauseScreen::place_mark(bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const auto& sp = tk_->space;
    if (tab() == Tab::Objectives || tab() == Tab::Controls) {
        if (row_count() == 0) {
            mark_.show(false, f);
            return;
        }
        const float y = list_y(focus_) + sp.row_h - 1.0f;
        mark_.move_to(sp.mid_x0, sp.mid_x1 - 8.0f, y, f, animate);
        mark_.show(true, f);
        return;
    }
    mark_.show(false, f);
}

// ---------------------------------------------------------------- ações

void PauseScreen::command(Nav c) { pending_nav_[int(c)] = true; }

void PauseScreen::request_resume() {
    // O Start do jogo: o MenuManager_Monitor fecha a pausa (a ação de fechar dela) e despausa.
    MenuModern_Press(kMmStart);
    waiting_ = true;
}

void PauseScreen::activate(int row, const MmSnapshot& s) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    switch (tab()) {
        case Tab::Pause: {
            const std::vector<PauseRow> rows = pause_rows(s);
            if (row < 0 || size_t(row) >= rows.size()) return;
            const PauseRow& r = rows[size_t(row)];
            if (r.options) {
                for (size_t i = 0; i < tabs_order_.size(); ++i) {
                    if (tabs_order_[i] == Tab::Options) set_tab(int(i), true);
                }
                return;
            }
            // A linha da lista principal do jogo (tag 6): escolhida e o X (C_GCPAUSE_Handler: 0 fecha, 1 e 2 a
            // confirmação).
            MenuModern_Select(kMmPauseControls, kMmTagMain, r.game_row);
            waiting_ = true;
            return;
        }
        case Tab::Controls:
            if (row == 0) {
                const int n = std::max(1, int(s.styles.size()));
                MenuModern_Pick(kMmPauseControls, kMmTagStyles, (s.style + 1) % n);
                waiting_ = true;
            } else if (row == 1) {
                // O círculo do jogo inverte o eixo (C_GCPAUSE_Handler, 0x5e da lista dos estilos).
                MenuModern_Press(kMmCircle);
                waiting_ = true;
            }
            return;
        default:
            (void)f;
            return;
    }
}

void PauseScreen::act_dialog(Nav c, const MmSnapshot& s) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const int n = int(dialog_.buttons.size());
    if (n == 0) return;
    const auto move = [&](int dir) {
        const int to = std::clamp(dialog_.focus + dir, 0, n - 1);
        if (to == dialog_.focus) return;
        dialog_.focus = to;
        if (size_t(to) < dialog_.button_rects.size()) {
            const Rect& r = dialog_.button_rects[size_t(to)];
            dialog_.mark.move_to(r.x0, r.x1, r.y1, f, true);
        }
        MenuModern_Pick(dialog_list_, dialog_tag_, to);  // a linha da lista do jogo acompanha
    };
    switch (c) {
        case Nav::Left:
        case Nav::Up: move(-1); break;
        case Nav::Right:
        case Nav::Down: move(1); break;
        case Nav::Select:
            MenuModern_Select(dialog_list_, dialog_tag_, dialog_.focus);
            waiting_ = true;
            break;
        case Nav::Back:
            if (dialog_box_) {
                MenuModern_Press(kMmBack);  // a caixa do jogo: voltar responde "não" (C_LBMSGOPTIONS_Handler)
            } else {
                // A confirmação da pausa: a resposta "não" do jogo (a última linha da lista 0xd).
                MenuModern_Select(dialog_list_, dialog_tag_, n - 1);
            }
            waiting_ = true;
            break;
        case Nav::Start: request_resume(); break;
        default: break;
    }
    (void)s;
}

void PauseScreen::act(Nav c, const MmSnapshot& s) {
    if (dialog_.open) {
        act_dialog(c, s);
        return;
    }
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    switch (c) {
        case Nav::TabPrev: set_tab(tab_ - 1, true); return;
        case Nav::TabNext: set_tab(tab_ + 1, true); return;
        case Nav::Start: request_resume(); return;
        case Nav::Back:
            if (tab() == Tab::Pause) {
                request_resume();
            } else {
                for (size_t i = 0; i < tabs_order_.size(); ++i) {
                    if (tabs_order_[i] == Tab::Pause) set_tab(int(i), true);
                }
            }
            return;
        default: break;
    }
    const int n = row_count();
    if (tab() == Tab::Pause) {
        if (c == Nav::Up && menu_.focus > 0) menu_.set_focus(menu_.focus - 1, f, true);
        if (c == Nav::Down && menu_.focus + 1 < n) menu_.set_focus(menu_.focus + 1, f, true);
        if (c == Nav::Select) activate(menu_.focus, s);
        return;
    }
    if (n == 0) return;
    switch (c) {
        case Nav::Up:
            if (focus_ > 0) {
                --focus_;
                ensure_visible(focus_);
                place_mark(true);
            }
            break;
        case Nav::Down:
            if (focus_ + 1 < n) {
                ++focus_;
                ensure_visible(focus_);
                place_mark(true);
            }
            break;
        case Nav::Left:
        case Nav::Right: {
            if (tab() != Tab::Controls) break;
            const int dir = c == Nav::Left ? -1 : 1;
            if (focus_ == 0) {
                const int count = std::max(1, int(s.styles.size()));
                MenuModern_Pick(kMmPauseControls, kMmTagStyles, (s.style + dir + count) % count);
                waiting_ = true;
            } else if ((dir > 0) != s.inverted) {
                MenuModern_Press(kMmCircle);
                waiting_ = true;
            }
            break;
        }
        case Nav::Select: activate(focus_, s); break;
        default: break;
    }
}

void PauseScreen::mouse(const MmSnapshot& s) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const float x = nav_.mx, y = nav_.my;
    if (!nav_.mouse_active || !nav_.inside) return;
    if (nav_.back) {
        act(Nav::Back, s);
        return;
    }
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && (nav_.moved || nav_.pressed) && h != dialog_.focus) {
            dialog_.focus = h;
            const Rect& r = dialog_.button_rects[size_t(h)];
            dialog_.mark.move_to(r.x0, r.x1, r.y1, f, true);
            MenuModern_Pick(dialog_list_, dialog_tag_, h);
        }
        if (nav_.pressed && h >= 0) act_dialog(Nav::Select, s);
        return;
    }
    // A barra de cima e a de baixo.
    hover_tab_ = bar_tabs_.hit(x, y);
    if (nav_.pressed) {
        if (hover_tab_ >= 0) {
            set_tab(hover_tab_, true);
            return;
        }
        if (hover_tab_ == -2) {
            set_tab(tab_ - 1, true);
            return;
        }
        if (hover_tab_ == -3) {
            set_tab(tab_ + 1, true);
            return;
        }
        switch (bar_.hit(x, y)) {
            case kActSelect: act(Nav::Select, s); return;
            case kActBack: act(Nav::Back, s); return;
            case kActResume: request_resume(); return;
            default: break;
        }
    }
    if (tab() == Tab::Pause) {
        const int h = menu_.hit(x, y);
        menu_.hover = h;
        if (h >= 0 && nav_.moved && h != menu_.focus) menu_.set_focus(h, f, true);
        if (h >= 0 && nav_.pressed) {
            if (h != menu_.focus) menu_.set_focus(h, f, true);
            activate(h, s);
        }
        return;
    }
    const auto& sp = tk_->space;
    // A roda rola a lista.
    if (nav_.wheel != 0.0f && x >= sp.mid_x0 - 40.0f && x < sp.mid_x1 + 40.0f) {
        const float view = sp.list_bottom - sp.list_top;
        scroll_target_ = std::clamp(scroll_target_ - nav_.wheel * sp.row_h, 0.0f, std::max(0.0f, list_y(row_count()) - view));
        scroll_.set(scroll_target_, tk_->motion.scroll, tk_->motion.ease_out);
    }
    int h = -1;
    for (int i = 0; i < row_count(); ++i) {
        const Rect r = row_rect(i);
        if (r.contains(x, y) && y >= sp.list_top && y < sp.list_bottom) h = i;
    }
    hover_ = h;
    if (h < 0) return;
    if ((nav_.moved || nav_.pressed) && h != focus_) {
        focus_ = h;
        place_mark(true);
    }
    if (!nav_.pressed || tab() != Tab::Controls) return;
    if (h == 0) {
        // O esquema: o clique na metade esquerda do valor volta um, no resto avança.
        const int count = std::max(1, int(s.styles.size()));
        const bool back = x < sp.mid_x1 - sp.pad_r - 150.0f;
        MenuModern_Pick(kMmPauseControls, kMmTagStyles, (s.style + (back ? count - 1 : 1)) % count);
        waiting_ = true;
    } else {
        activate(h, s);
    }
}

// ---------------------------------------------------------------- o quadro

void PauseScreen::frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    content_.update(dt);
    mark_.update(dt);
    scroll_.update(dt);
    bar_tabs_.update(dt);
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

    // A barra de cima: as abas (os textos do jogo onde há) e as teclas Q/E (LB/RB no controle).
    bar_tabs_.labels.clear();
    for (const Tab t : tabs_order_) {
        std::wstring label;
        switch (t) {
            case Tab::Pause: label = T(PcText::MdTopPause); break;
            case Tab::Objectives: label = widen(s.tab_text[kMmTabObjectives]); break;
            case Tab::Controls: label = widen(s.tab_text[kMmTabControls]); break;
            case Tab::Score: label = widen(s.tab_text[kMmTabScore]); break;
            case Tab::Options: label = T(PcText::MdTopOptions); break;
        }
        if (label.empty()) {
            label = t == Tab::Objectives ? T(PcText::MpTabObjectives) : t == Tab::Controls ? T(PcText::MpTabControls) : T(PcText::MpTabScore);
        }
        bar_tabs_.labels.push_back(label);
    }
    const bool pad = in.device == 1;
    {
        const std::wstring kp = bind_key("menu_tab_prev"), kn = bind_key("menu_tab_next");
        bar_tabs_.key_prev = kp.empty() ? L"Q" : kp;
        bar_tabs_.key_next = kn.empty() ? L"E" : kn;
        bar_tabs_.glyph_prev = pad ? bind_glyph("menu_tab_prev", "xbox_lb") : std::string();
        bar_tabs_.glyph_next = pad ? bind_glyph("menu_tab_next", "xbox_rb") : std::string();
    }
    bar_tabs_.corner_left = L"NF.PC / SYS";
    bar_tabs_.corner_right = L"SLUS-20579";
    const bool first = !bar_tabs_.laid_out;
    bar_tabs_.layout(f);
    if (first) bar_tabs_.set_active(tab_, f, false);
    sync_rows(s);
    menu_.focused = tab() == Tab::Pause && !dialog_.open;
    menu_.layout(f);
    if (first) {
        menu_.set_focus(menu_.focus, f, false);
        place_mark(false);
    }
    if (tab() == Tab::Pause && !menu_.mark.placed) menu_.set_focus(menu_.focus, f, false);

    // A confirmação do jogo (a lista 0xd ou a caixa de mensagem) vira o Dialog do kit, e some com ela.
    if (s.dialog.open && !dialog_.open) {
        std::vector<std::wstring> buttons;
        for (const MmItem& o : s.dialog.options) {
            std::wstring t = widen(o.text);
            if (t.find(L'~') != std::wstring::npos) t = T(PcText::MrContinue);  // "Press ~A to Continue"
            buttons.push_back(t);
        }
        std::wstring title = widen(s.dialog.title), body = widen(s.dialog.body);
        if (!s.dialog.box) {
            // A pergunta do jogo ("RESTART: ARE YOU SURE?") e o que acontece (só do port).
            const bool quit = s.rows.size() > 2 && s.row == 2;
            body = T(quit ? PcText::MpQuitBody : PcText::MpRestartBody);
        }
        dialog_.show(title, body, buttons, std::clamp(s.dialog.focus, 0, std::max(0, int(buttons.size()) - 1)), f);
        dialog_box_ = s.dialog.box;
        dialog_list_ = s.dialog.list;
        dialog_tag_ = s.dialog.tag;
        nav_.reset();  // o botão que abriu a confirmação só conta solto
    } else if (!s.dialog.open && dialog_.open) {
        dialog_.hide(f);
        nav_.reset();
    }
    if (dialog_.open) dialog_.layout(f);

    // A entrada.
    const bool in_options = tab() == Tab::Options && options_.active();
    nav_.read(in, cv, dt, *tk_, in_options || dialog_.open);
    bool injected[kNavCount] = {};
    for (int i = 0; i < kNavCount; ++i) {
        injected[i] = pending_nav_[i];
        if (pending_nav_[i]) nav_.inject(Nav(i));
        pending_nav_[i] = false;
    }
    if (in_options && !dialog_.open) {
        // A aba OPTIONS: a tela de opções lê a entrada dela; a pausa fica com o Start, as abas e o voltar do topo. Os
        // comandos dos testes vão à tela de opções como os dela.
        options_.frame(dt, in, cv, m);
        static const OptionsScreen::Cmd kCmd[kNavCount] = {
            OptionsScreen::Cmd::Up,     OptionsScreen::Cmd::Down,    OptionsScreen::Cmd::Left,
            OptionsScreen::Cmd::Right,  OptionsScreen::Cmd::Select,  OptionsScreen::Cmd::Back,
            OptionsScreen::Cmd::TabPrev, OptionsScreen::Cmd::TabNext, OptionsScreen::Cmd::Select};
        for (int i = 0; i < kNavCount; ++i) {
            if (injected[i] && Nav(i) != Nav::Start) options_.command(kCmd[i]);
        }
        const int t = options_.take_tab_request();
        if (t != 0) set_tab(tab_ + t, true);
        if (options_.take_back_request()) {
            for (size_t i = 0; i < tabs_order_.size(); ++i) {
                if (tabs_order_[i] == Tab::Pause) set_tab(int(i), true);
            }
        }
        if (nav_.fire(Nav::Start) && options_.capturing() < 0 && !options_.dialog_open()) request_resume();
        if (nav_.pressed && nav_.inside && options_.capturing() < 0) {
            const int h = bar_tabs_.hit(nav_.mx, nav_.my);
            if (h >= 0) set_tab(h, true);
            if (h == -2) set_tab(tab_ - 1, true);
            if (h == -3) set_tab(tab_ + 1, true);
        }
        return;
    }
    if (!waiting_ && s.ready) {
        for (int i = 0; i < kNavCount; ++i) {
            if (!nav_.fire(Nav(i))) continue;
            act(Nav(i), s);
            if (waiting_ || !active_) break;
        }
        if (!waiting_) mouse(s);
    }
    prompts(bar_);
    bar_.layout(f);
}

// ---------------------------------------------------------------- desenho

void PauseScreen::prompts(PromptBar& bar) const {
    const bool pad = nav_.device == 1;
    bar.prompts.clear();
    bar.info_title = T(PcText::MdFooterPort);
    bar.info_sub = L"SLUS-20579 · NTSC-U";
    char buf[128];
    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdFooterBuild), build_.empty() ? "dev" : build_.c_str());
    bar.info2_title = widen(buf);
    bar.info2_sub = T(PcText::MdFooterGame);
    const auto add = [&](int action, const std::wstring& key, const std::string& glyph, PcText label) {
        Prompt p;
        p.action = action;
        p.key = key;
        p.glyph = pad ? glyph : std::string();
        p.label = T(label);
        bar.prompts.push_back(p);
    };
    const std::wstring sel = bind_key("menu_select");
    if (dialog_.open) {
        add(kActSelect, sel.empty() ? L"Enter" : sel, "xbox_a", PcText::MdPromptSelect);
        add(kActBack, L"Esc", "xbox_b", PcText::MdPromptBack);
        return;
    }
    const std::wstring resume = bind_key("pause");
    add(kActResume, resume.empty() ? L"Enter" : resume, "xbox_menu", PcText::MpPromptResume);
    if (tab() == Tab::Controls) {
        bar.prompts.push_back(Prompt{L"← →", pad ? std::string("xbox_dpad_h") : std::string(), T(PcText::MdPromptChange), 0});
    } else if (tab() == Tab::Pause) {
        // No topo, voltar fecha a pausa como o Resume: só o Resume aparece.
        add(kActSelect, sel.empty() ? L"Enter" : sel, "xbox_a", PcText::MdPromptSelect);
        return;
    }
    add(kActBack, L"Esc", "xbox_b", PcText::MdPromptBack);
}

void PauseScreen::draw_pause(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const MmSnapshot& s = snap_;
    line_at(dl, tk.space.left_x + 3.0f + dx, tk.space.header_y, T(PcText::MpHeader), tk.type.header, tk.color.dim.with_alpha(alpha));
    menu_.draw(dl, f, alpha, dx);
    // À direita, a missão.
    Description d;
    d.strip[0] = L"NF_PC/PAUSE";
    d.strip[1] = s.level > 0 ? L"LVL." + std::to_wstring(s.level) : L"LVL.-";
    d.strip[2] = s.mission.empty() ? std::wstring() : L"[" + to_upper(widen(s.mission)) + L"]";
    d.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    d.title = s.mission.empty() ? T(PcText::MpHeader) : widen(s.mission);
    d.body = widen(s.mission_text);
    const int total = int(s.objectives.size());
    if (total > 0) {
        d.meter = true;
        d.meter_label = T(PcText::MpObjectivesDone);
        d.meter_value = std::to_wstring(s.done) + L" / " + std::to_wstring(total);
        d.meter_fill = float(s.done) / float(total);
    }
    const float dxp = tk.space.slide * (1.0f - alpha);
    float y = draw_description(dl, f, d, alpha, dxp);
    const float x0 = tk.space.desc_x0 + dxp, x1 = tk.space.desc_x1 + dxp;
    // O objetivo atual (o rótulo do jogo, "Current Objective:") e os fatos da missão.
    y += 4.0f;
    draw_hairline(dl, f, x0, x1, y, alpha);
    y += 34.0f;
    if (const MmObjective* o = current_objective(s)) {
        std::wstring label = widen(s.current_label);
        if (!label.empty() && label.back() == L':') label.pop_back();
        line_at(dl, x0, y, label, tk.type.section, tk.color.text_2.with_alpha(alpha));
        y += 34.0f;
        for (const std::wstring& l : wrap_lines(f, widen(o->label), tk.type.meta, x1 - x0)) {
            line_at(dl, x0, y, l, tk.type.meta, tk.color.text.with_alpha(alpha));
            y += tk.type.meta.size * 1.35f;
        }
        y += 14.0f;
        draw_hairline(dl, f, x0, x1, y, alpha);
        y += 34.0f;
    }
    TextStyle vs = tk.type.meta;
    vs.weight = 600;
    if (s.level > 0 && s.levels > 0) {
        line_at(dl, x0, y, fmt(PcText::MpLevel, s.level, s.levels), tk.type.meta, tk.color.text_2.with_alpha(alpha));
        y += 32.0f;
    }
    if (!s.difficulty.empty()) {
        const std::wstring lab = T(PcText::MpDifficulty);
        line_at(dl, x0, y, lab, tk.type.meta, tk.color.text_2.with_alpha(alpha));
        line_at(dl, x0 + text_width(f, lab, tk.type.meta) + 8.0f, y, widen(s.difficulty), vs, tk.color.text.with_alpha(alpha));
        y += 32.0f;
    }
}

void PauseScreen::draw_objectives(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmSnapshot& s = snap_;
    // À esquerda, o resumo: cumpridos de quantos, fracassados, confidenciais.
    std::wstring head = widen(s.tab_text[kMmTabObjectives]);
    if (head.empty()) head = T(PcText::MpTabObjectives);
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, head + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    const int total = int(s.objectives.size());
    int hidden = 0;
    for (const MmObjective& o : s.objectives) hidden += o.state == MmObjState::Hidden ? 1 : 0;
    const std::wstring done = std::to_wstring(s.done);
    const float cy = 268.0f;
    line_at(dl, sp.left_x + dx, cy, done, tk.type.counter, tk.color.accent.with_alpha(alpha));
    const float dw = text_width(f, done, tk.type.counter);
    TextStyle of = tk.type.counter;
    of.size = 40.0f;
    line_at(dl, sp.left_x + dw + 14.0f + dx, cy + 18.0f, L"/ " + std::to_wstring(total), of, tk.color.dim.with_alpha(alpha));
    line_at(dl, sp.left_x + 3.0f + dx, cy + 72.0f, T(PcText::MpCountDone), tk.type.section, tk.color.text_2.with_alpha(alpha));
    float y = cy + 128.0f;
    const auto fact = [&](PcText label, int n) {
        draw_hairline(dl, f, sp.left_x + dx, 420.0f + dx, y - 26.0f, alpha);
        line_at(dl, sp.left_x + 3.0f + dx, y, T(label), tk.type.meta, tk.color.text_2.with_alpha(alpha));
        line_at(dl, 420.0f + dx, y, std::to_wstring(n), tk.type.meta, tk.color.text.with_alpha(alpha), Align::Right);
        y += 52.0f;
    };
    fact(PcText::MpCountFailed, s.failed);
    fact(PcText::MpCountHidden, hidden);
    draw_hairline(dl, f, sp.left_x + dx, 420.0f + dx, y - 26.0f, alpha);

    // No meio, a lista.
    const float top = sp.list_top, bottom = sp.list_bottom;
    const float scroll = scroll_.value();
    dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, top - 2.0f, sp.mid_x1 + 20.0f + dx, bottom});
    const float sy = top - scroll;
    draw_section(dl, f, Rect{sp.mid_x0 + dx, sy, sp.mid_x1 + dx, sy + sp.section_h}, T(PcText::MpSecObjectives), alpha);
    if (total == 0) {
        line_at(dl, sp.mid_x0 + sp.pad_l + dx, sy + sp.section_h + sp.row_h * 0.5f, T(PcText::MpNoObjectives), tk.type.label,
                tk.color.dim.with_alpha(alpha));
    }
    for (int i = 0; i < total; ++i) {
        const MmObjective& o = s.objectives[size_t(i)];
        const float y0 = top + list_y(i) - scroll, y1 = y0 + sp.row_h;
        if (y1 < top - 4.0f || y0 > bottom + 4.0f) continue;
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y1};
        const bool on = i == focus_;
        const float a = alpha * (i == hover_ && !on ? 1.0f : 1.0f);
        objective_icon(dl, tk, o.state, row.x0 + sp.pad_l + 10.0f, row.cy(), a);
        std::wstring label = o.state == MmObjState::Hidden ? T(PcText::MpClassified) : widen(o.label);
        Color lc = tk.color.text;
        if (o.state == MmObjState::Done) lc = tk.color.text_2;
        if (o.state == MmObjState::Failed) lc = tk.color.dim;
        if (o.state == MmObjState::Hidden) lc = tk.color.disabled;
        if (on && o.state != MmObjState::Hidden) lc = tk.color.accent;
        // O texto encolhe para caber antes da situação.
        const std::wstring status = o.state == MmObjState::Hidden ? T(PcText::MpClassifiedStatus) : widen(o.status);
        const float sw = text_width(f, status, tk.type.value);
        const float room = row.w() - sp.pad_l - kObjIndent - sp.pad_r - sw - 36.0f;
        const TextStyle ls = fit_style(f, label, tk.type.label, room, 17.0f);
        line_at(dl, row.x0 + sp.pad_l + kObjIndent, row.cy() - 1.0f, label, ls, lc.with_alpha(a));
        if (o.state == MmObjState::Failed) {
            const float lw = std::min(room, text_width(f, label, ls));
            dl.line(row.x0 + sp.pad_l + kObjIndent - 2.0f, row.cy() + 1.0f, row.x0 + sp.pad_l + kObjIndent + lw + 2.0f, row.cy() + 1.0f,
                    tk.color.dim.with_alpha(0.8f * a), 1.4f);
        }
        Color sc = tk.color.text_2;
        if (o.state == MmObjState::Done) sc = tk.color.accent;
        if (o.state == MmObjState::Hidden || o.state == MmObjState::Failed) sc = tk.color.dim;
        line_at(dl, row.x1 - sp.pad_r, row.cy(), status, tk.type.value, sc.with_alpha(a), Align::Right);
        draw_hairline(dl, f, row.x0, row.x1 - 8.0f, y1, alpha);
    }
    mark_.draw(dl, f, alpha, top - scroll);
    dl.pop_clip();
    const float view = bottom - top, totalh = list_y(total);
    if (totalh > view) draw_scroll(dl, f, sp.scroll_x + dx, top + 24.0f, bottom, view / totalh, scroll / (totalh - view), alpha);

    // À direita, o objetivo com o foco: o texto do jogo e a dica.
    Description d;
    d.strip[0] = L"NF_PC/OBJ";
    d.strip[1] = L"OBJ." + std::to_wstring(focus_ + 1);
    d.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    if (focus_ >= 0 && focus_ < total) {
        const MmObjective& o = s.objectives[size_t(focus_)];
        if (o.state == MmObjState::Hidden) {
            d.title = T(PcText::MpClassified);
            d.body = T(PcText::MpClassifiedBody);
            d.value = T(PcText::MpClassifiedStatus);
        } else {
            d.title = widen(o.label);
            d.body = o.text.empty() ? T(PcText::MpNoHint) : widen(o.text);
            d.value = widen(o.status);
        }
        d.strip[2] = L"[" + to_upper(d.value) + L"]";
        d.value_label = T(PcText::MpStatus);
    } else {
        d.title = head;
        d.body = T(PcText::MpNoObjectives);
    }
    draw_description(dl, f, d, alpha, tk.space.slide * (1.0f - alpha));
}

void PauseScreen::draw_controls(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmSnapshot& s = snap_;
    const bool pad = nav_.device == 1;
    std::wstring head = widen(s.tab_text[kMmTabControls]);
    if (head.empty()) head = T(PcText::MpTabControls);
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, head + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    // À esquerda, o aparelho e onde religar.
    line_at(dl, sp.left_x + 3.0f + dx, sp.cat_y, T(PcText::MpDevice), tk.type.section, tk.color.text_2.with_alpha(alpha));
    line_at(dl, sp.left_x + dx, sp.cat_y + 44.0f, device_text(), tk.type.category, tk.color.text.with_alpha(alpha));
    float hy = sp.cat_y + 104.0f;
    for (const std::wstring& l : wrap_lines(f, T(PcText::MpRebindHint), tk.type.body, 380.0f)) {
        line_at(dl, sp.left_x + dx, hy, l, tk.type.body, tk.color.dim.with_alpha(alpha));
        hy += tk.type.body.size * tk.type.body.line;
    }

    // No meio: o esquema, o eixo Y e o mapa de botões.
    const float top = sp.list_top;
    draw_section(dl, f, Rect{sp.mid_x0 + dx, top, sp.mid_x1 + dx, top + sp.section_h}, T(PcText::MpSecLayout), alpha);
    const std::wstring style = s.style >= 0 && size_t(s.style) < s.styles.size() ? widen(s.styles[size_t(s.style)]) : std::wstring();
    for (int i = 0; i < kControlRows; ++i) {
        const float y0 = top + list_y(i);
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y0 + sp.row_h};
        RowState rs;
        rs.alpha = alpha;
        rs.focused = i == focus_;
        rs.label_t = i == focus_ ? 1.0f : 0.0f;
        if (i == 0) {
            draw_row_label(dl, f, row, T(PcText::MdButtonLayout), rs);
            // O valor entre as setas (esquerda e direita trocam).
            const float right = row.x1 - sp.pad_r;
            const Color cc = mix(tk.color.text_2, tk.color.accent, rs.label_t).with_alpha(alpha);
            dl.chevron(Rect{right - 8.0f, row.cy() - 7.0f, right, row.cy() + 7.0f}, 3, cc, 1.6f);
            const float vw = text_width(f, style, tk.type.value);
            line_at(dl, right - 22.0f, row.cy(), style, tk.type.value, mix(tk.color.text_2, tk.color.text, rs.label_t).with_alpha(alpha),
                    Align::Right);
            dl.chevron(Rect{right - 44.0f - vw - 8.0f, row.cy() - 7.0f, right - 44.0f - vw, row.cy() + 7.0f}, 2, cc, 1.6f);
        } else {
            std::wstring axis = widen(s.axis_label);
            if (axis.empty()) axis = T(PcText::CtlPadInvert);
            draw_row_label(dl, f, row, axis, rs);
            draw_toggle(dl, f, row, s.inverted ? 1.0f : 0.0f, widen(s.axis_value), rs, 80.0f);
        }
        draw_hairline(dl, f, row.x0, row.x1 - 8.0f, row.y1, alpha);
    }
    mark_.draw(dl, f, alpha, top);
    // O mapa de botões que o jogo monta para o esquema: duas colunas, o símbolo do aparelho e o que o botão faz.
    const float my = top + list_y(kControlRows);
    std::wstring sec = T(PcText::MpSecButtons);
    if (!style.empty()) sec += L"  ·  " + style;
    draw_section(dl, f, Rect{sp.mid_x0 + dx, my, sp.mid_x1 + dx, my + sp.section_h}, sec, alpha);
    const float gy = my + sp.section_h;
    const float colw = (sp.mid_x1 - sp.mid_x0) * 0.5f;
    for (size_t i = 0; i < s.buttons.size(); ++i) {
        const MmButton& b = s.buttons[i];
        const int col = int(i % 2), line = int(i / 2);
        const float x0 = sp.mid_x0 + float(col) * colw + dx;
        const float y0 = gy + float(line) * sp.button_row_h;
        const float cy = y0 + sp.button_row_h * 0.5f;
        float iw = 0.0f;
        const float ix = x0 + sp.pad_l;
        if (pad) {
            const std::string g = symbol_glyph(b.symbol, nf::controls().pad_triggers);
            if (!g.empty()) iw = draw_prompt_icon(dl, f, ix, cy, std::wstring(), g, alpha);
        } else {
            const std::wstring keys = symbol_keys(b.symbol, uint16_t(s.style));
            if (!keys.empty()) {
                iw = draw_keycap(dl, f, ix, cy, keys, alpha * 0.95f);
            } else {
                line_at(dl, ix, cy, L"—", tk.type.cell, tk.color.dim.with_alpha(alpha));
                iw = 20.0f;
            }
        }
        (void)iw;
        line_at(dl, x0 + sp.pad_l + 104.0f, cy, widen(b.label), fit_style(f, widen(b.label), tk.type.cell, colw - 140.0f, 15.0f),
                tk.color.text_2.with_alpha(alpha));
        draw_hairline(dl, f, x0 + (col == 0 ? 0.0f : 12.0f), x0 + colw - (col == 0 ? 12.0f : 8.0f), y0 + sp.button_row_h, alpha * 0.7f);
    }

    // À direita, a linha com o foco.
    Description d;
    d.strip[0] = L"NF_PC/CTL";
    d.strip[1] = L"STYLE." + std::to_wstring(s.style + 1);
    d.strip[2] = L"[" + to_upper(style) + L"]";
    d.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    if (focus_ == 0) {
        d.title = T(PcText::MdButtonLayout);
        d.body = T(PcText::MdDescButtonLayout);
        d.value = style;
    } else {
        d.title = widen(s.axis_label.empty() ? std::string(PcText_Utf8(PcText::CtlPadInvert)) : s.axis_label);
        d.body = T(PcText::MdDescInvertPad);
        d.value = widen(s.axis_value);
    }
    d.value_label = T(PcText::MdCurrentValue);
    draw_description(dl, f, d, alpha, tk.space.slide * (1.0f - alpha));
}

void PauseScreen::draw_score(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    std::wstring head = widen(snap_.tab_text[kMmTabScore]);
    if (head.empty()) head = T(PcText::MpTabScore);
    line_at(dl, sp.left_x + 3.0f + dx, sp.header_y, head + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    if (snap_.multiplayer) {
        // O placar do multijogador: as linhas da lista do jogo (Menu_GetMPScore ainda é um stub).
        for (size_t i = 0; i < snap_.score.size(); ++i) {
            const float y0 = sp.list_top + float(i) * sp.key_row_h;
            const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y0 + sp.key_row_h};
            const std::vector<std::string>& cells = snap_.score[i];
            for (size_t k = 0; k < cells.size(); ++k) {
                const float x = k == 0 ? row.x0 + sp.pad_l : row.x1 - sp.pad_r - float(cells.size() - 1 - k) * 160.0f;
                line_at(dl, x, row.cy(), widen(cells[k]), tk.type.cell, tk.color.text_2.with_alpha(alpha), k == 0 ? Align::Left : Align::Right);
            }
            draw_hairline(dl, f, row.x0, row.x1 - 8.0f, row.y1, alpha);
        }
        return;
    }
    // Com um jogador, as estatísticas da missão que o C_GCPAUSE_Handler monta com o PlrStat_GetScore: à esquerda a missão
    // e o que a tabela é; no meio e à direita a tabela do jogo (Category / Current / Target), as linhas recuadas do jogo
    // ("  ...") recuadas, as sem números como cabeçalho do grupo.
    float ly = sp.cat_y;
    if (!snap_.mission.empty()) {
        line_at(dl, sp.left_x + dx, ly, widen(snap_.mission), fit_style(f, widen(snap_.mission), tk.type.hero_sub, 400.0f, 15.0f),
                tk.color.accent.with_alpha(alpha));
        ly += 48.0f;
    }
    for (const std::wstring& l : wrap_lines(f, T(PcText::MpScoreBody), tk.type.body, 380.0f)) {
        line_at(dl, sp.left_x + dx, ly, l, tk.type.body, tk.color.dim.with_alpha(alpha));
        ly += tk.type.body.size * tk.type.body.line;
    }
    if (!snap_.difficulty.empty()) {
        ly += 24.0f;
        draw_hairline(dl, f, sp.left_x + dx, 420.0f + dx, ly - 26.0f, alpha);
        const std::wstring lab = T(PcText::MpDifficulty);
        line_at(dl, sp.left_x + 3.0f + dx, ly, lab, tk.type.meta, tk.color.text_2.with_alpha(alpha));
        line_at(dl, 420.0f + dx, ly, widen(snap_.difficulty), tk.type.meta, tk.color.text.with_alpha(alpha), Align::Right);
        draw_hairline(dl, f, sp.left_x + dx, 420.0f + dx, ly + 26.0f, alpha);
    }
    const float x0 = sp.mid_x0 + dx, x1 = sp.desc_x1 + dx;
    const float cols[2] = {x1 - 240.0f, x1 - sp.pad_r};
    std::vector<std::string> header = {"Category", "Current", "Target"};
    size_t first = 0;
    if (!snap_.score.empty()) {
        header = snap_.score[0];
        first = 1;
    }
    float y = sp.list_top + 34.0f;
    for (size_t k = 0; k < header.size() && k < 3; ++k) {
        const std::wstring h = to_upper(widen(header[k]));
        if (k == 0) line_at(dl, x0 + sp.pad_l, y, h, tk.type.table_head, tk.color.dim.with_alpha(alpha));
        else line_at(dl, cols[k - 1], y, h, tk.type.table_head, tk.color.dim.with_alpha(alpha), Align::Right);
    }
    y += 26.0f;
    draw_hairline(dl, f, x0, x1, y, alpha);
    const float rh = 56.0f;
    const auto blank = [](const std::string& t) { return t.find_first_not_of(' ') == std::string::npos; };
    if (first >= snap_.score.size()) {
        line_at(dl, x0 + sp.pad_l, y + rh * 0.5f, L"—", tk.type.cell, tk.color.disabled.with_alpha(alpha));
        return;
    }
    for (size_t i = first; i < snap_.score.size(); ++i) {
        const std::vector<std::string>& r = snap_.score[i];
        bool empty = true;
        for (const std::string& c : r) empty = empty && blank(c);
        if (empty) continue;  // a linha em branco do jogo depois do cabeçalho
        const std::string name = r.empty() ? std::string() : r[0];
        const bool indented = !name.empty() && name[0] == ' ';
        const bool group = (r.size() < 2 || blank(r[1])) && (r.size() < 3 || blank(r[2]));
        const float cy = y + rh * 0.5f;
        std::wstring label = widen(name);
        while (!label.empty() && label[0] == L' ') label.erase(0, 1);
        if (group) {
            line_at(dl, x0 + sp.pad_l, cy, label, tk.type.section, tk.color.text_2.with_alpha(alpha));
        } else {
            const float ix = indented ? 30.0f : 0.0f;
            if (indented) dl.fill(Rect{x0 + sp.pad_l + 8.0f, cy - 1.0f, x0 + sp.pad_l + 18.0f, cy + 1.0f}, tk.color.dim.with_alpha(alpha));
            line_at(dl, x0 + sp.pad_l + ix, cy, label, tk.type.cell, (indented ? tk.color.text_2 : tk.color.text).with_alpha(alpha));
            for (size_t k = 1; k < r.size() && k < 3; ++k) {
                const std::wstring v = blank(r[k]) ? std::wstring(L"—") : widen(r[k]);
                const Color c = blank(r[k]) ? tk.color.disabled : (k == 1 ? tk.color.text : tk.color.text_2);
                line_at(dl, cols[k - 1], cy, v, tk.type.cell, c.with_alpha(alpha), Align::Right);
            }
        }
        y += rh;
        draw_hairline(dl, f, x0 + (indented ? 30.0f : 0.0f), x1, y, alpha * (group ? 0.6f : 1.0f));
    }
}

void PauseScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    const float k = appear_.value();
    draw_background(dl, f, k);
    bar_tabs_.draw(dl, f, k);
    if (tab() == Tab::Options && options_.active()) {
        options_.build(dl, cv, m);
    } else {
        // O conteúdo entra deslizando (ao abrir e ao trocar de aba).
        const float c = content_.value();
        const float a = k * (0.25f + 0.75f * c);
        const float dx = tk.space.slide * (1.0f - k) + 12.0f * (1.0f - c);
        switch (tab()) {
            case Tab::Pause: draw_pause(dl, f, a, -dx); break;
            case Tab::Objectives: draw_objectives(dl, f, a, dx); break;
            case Tab::Controls: draw_controls(dl, f, a, dx); break;
            case Tab::Score: draw_score(dl, f, a, dx); break;
            default: break;
        }
        bar_.draw(dl, f, k);
    }
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

std::string PauseScreen::describe() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "pausa_moderna ativa %d fechando %d aparecer %.2f aba %s abas %zu foco %d lista %d dialogo %d foco_dialogo %d "
                  "esperando %d aba_jogo %d linha_jogo %d mouse %d",
                  active_ ? 1 : 0, closing_ ? 1 : 0, double(appear_.value()), tab_name(tab()), tabs_order_.size(), focus_,
                  menu_.focus, dialog_.open ? 1 : 0, dialog_.focus, waiting_ ? 1 : 0, snap_.tab, snap_.row, nav_.mouse_active ? 1 : 0);
    return buf;
}

}  // namespace nf::ui
