// O menu principal no estilo Modern. Ver modern_front.h e docs/design.md.

#include "ui/modern_front.h"
#include "platform/keys.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>


#include "engine/pc_text.h"
#include "platform/gamepad.h"
#include "platform/input_bindings.h"
#include "platform/psi_input.h"
#include "platform/fmath.h"

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

std::wstring trim_dot(std::wstring s) {
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ' || s.back() == L':')) s.pop_back();
    return s;
}

// As ações da barra de baixo.
enum : int { kActSelect = 1, kActBack, kActDossier, kActDelete, kActSpace, kActDone, kActCreate };

// As texturas do jogo que o menu principal desenha (o pacote 07000048).
constexpr uint32_t kArt = 0x3000074;   // a arte do "Press START" (512 x 512, 4:3 na TV), com o logotipo
constexpr uint32_t kLogo = 0x300016d;  // "007 nightfire" (256 x 128; 124 x 40 na tela do PS2: 3,62 por 1 na TV)
constexpr float kLogoAspect = 3.62f;

// A categoria das opções Modern pelo nome (o --menu-tab), com a de reserva se ela ainda não existe.
Cat cat_named(const char* name, Cat fallback) {
    Cat c = fallback;
    return parse_cat(name, &c) ? c : fallback;
}

// A categoria das opções Modern no lugar de uma página de opções do codinome do jogo.
Cat cat_for_page(uint32_t page) {
    switch (page) {
        case kMmFeControls: return cat_named("controller", Cat::Controller);
        case kMmFeAdvanced: return cat_named("gameplay", Cat::Controls);
        case kMmFeAv: return cat_named("audio", Cat::Audio);
        default: return Cat::Video;
    }
}

bool is_list(FrontScreen::View v) {
    switch (v) {
        case FrontScreen::View::Campaign:
        case FrontScreen::View::Codenames:
        case FrontScreen::View::CodenameMenu:
        case FrontScreen::View::Difficulty:
        case FrontScreen::View::Map: return true;
        default: return false;
    }
}

// As linhas do menu do codinome (C_SBCNOPTIONS_Handler, pela posição da roda): 0 Secret Unlocks (o teclado do jogo em
// modo de código), 1 Controller Setup, 2 o controle das missões de carro, 3 Advanced Options, 4 Multiplayer Options,
// 5 AV Options, 6 Save Codename / Codenames.
enum : int { kCnSecret = 0, kCnControls = 1, kCnDriving = 2, kCnAdvanced = 3, kCnMultiplayer = 4, kCnAv = 5, kCnSave = 6 };

// As linhas do menu principal Modern depois dos três botões do jogo (só do port).
enum : int { kMainOptions = 3, kMainCredits = 4, kMainQuit = 5 };

}  // namespace

const char* FrontScreen::view_name(View v) {
    switch (v) {
        case View::Title: return "titulo";
        case View::Main: return "principal";
        case View::Campaign: return "campanha";
        case View::Codenames: return "codinomes";
        case View::Name: return "nome";
        case View::CodenameMenu: return "codinome";
        case View::Difficulty: return "dificuldade";
        case View::Map: return "mapa";
        case View::Options: return "opcoes";
        case View::Dossier: return "dossie";
        case View::Credits: return "creditos";
    }
    return "";
}

FrontScreen::FrontScreen(const Tokens* tokens)
    : tk_(tokens != nullptr ? tokens : &default_tokens()), options_(tokens), dossier_(tokens) {
    options_.set_embedded(true);
}

void FrontScreen::set_tokens(const Tokens* tokens) {
    tk_ = tokens != nullptr ? tokens : &default_tokens();
    options_.set_tokens(tokens);
    dossier_.set_tokens(tokens);
}

void FrontScreen::set_build(const std::string& build) {
    build_ = build;
    options_.set_build(build);
    dossier_.set_build(build);
}

FrontScreen::View FrontScreen::view_of(const MmSnapshot& s) {
    switch (s.front.view) {
        case MmFrontView::Main: return View::Main;
        case MmFrontView::Campaign: return View::Campaign;
        case MmFrontView::Codenames: return View::Codenames;
        case MmFrontView::Name: return View::Name;
        case MmFrontView::CodenameMenu: return View::CodenameMenu;
        case MmFrontView::Difficulty: return View::Difficulty;
        case MmFrontView::Map: return View::Map;
        case MmFrontView::Options: return View::Options;
        case MmFrontView::Dossier: return View::Dossier;
        case MmFrontView::Credits: return View::Credits;
        default: return View::Title;
    }
}

// ---------------------------------------------------------------- abrir, fechar, vistas

void FrontScreen::open(const MmSnapshot& s) {
    active_ = true;
    closing_ = false;
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.front_in, tk_->motion.ease_out);
    snap_ = s;
    menu_ = BigMenu{};
    mark_ = FocusMark{};
    key_mark_ = FocusMark{};
    tabs_ = TabBar{};
    tabs_on_ = false;
    bar_ = PromptBar{};
    dialog_ = Dialog{};
    dialog_kind_ = DialogKind::None;
    toast_ = Toast{};
    nav_.reset();
    for (bool& p : pending_) p = false;
    typed_.clear();
    waiting_ = false;
    quit_ = false;
    if (options_.active()) options_.force_close();
    opts_ = OptsFrom::None;
    skip_option_page_ = 0;
    pic_ = pic_prev_ = 0;
    pic_t_.snap(1.0f);
    for (bool& k : prev_keys_) k = true;  // o que já estava apertado não digita
    prev_pad_ = 0xffff;
    view_ = view_of(s);
    blur_.snap(view_ == View::Title ? 0.0f : 1.0f);
    change_view(view_, s);
    content_.snap(1.0f);
}

void FrontScreen::close() {
    if (!active_ || closing_) return;
    closing_ = true;
    if (options_.active()) options_.force_close();
    if (opts_ == OptsFrom::Codename || opts_ == OptsFrom::Game) MenuModern_CodenameOptionsEnd();
    opts_ = OptsFrom::None;
    dialog_.hide(Frame{*tk_, last_cv_, Measure{}, time_});
    appear_.set(0.0f, tk_->motion.close * 1.4f, tk_->motion.ease_in);
}

void FrontScreen::force_close() {
    if (options_.active()) options_.force_close();
    opts_ = OptsFrom::None;
    active_ = closing_ = false;
}

void FrontScreen::change_view(View v, const MmSnapshot& s) {
    const View from = view_;
    view_ = v;
    snap_ = s;
    content_.snap(0.0f);
    content_.set(1.0f, tk_->motion.content * 1.5f, tk_->motion.ease_out);
    hover_ = key_hover_ = -1;
    mark_ = FocusMark{};
    key_mark_ = FocusMark{};
    scroll_.snap(0.0f);
    scroll_target_ = 0.0f;
    lit_.clear();
    tabs_ = TabBar{};
    tabs_on_ = false;
    nav_.reset();
    if (v == View::Main) {
        menu_ = BigMenu{};
        menu_.x = tk_->space.left_x;
        menu_.y = tk_->space.front_menu_y;
        static const PcText kSubs[3] = {PcText::FeSubNightfire, PcText::FeSubMultiplayer, PcText::FeSubCodenames};
        static const char* const kNames[3] = {"NightFire", "Multiplayer", "Codenames"};
        for (int i = 0; i < 3; ++i) {
            const std::string& b = size_t(i) < s.front.buttons.size() ? s.front.buttons[size_t(i)] : std::string();
            menu_.items.push_back(BigMenu::Item{widen(b.empty() ? std::string(kNames[i]) : b), T(kSubs[i]), false, false});
        }
        menu_.items.push_back(BigMenu::Item{T(PcText::MpOptions), T(PcText::MpOptionsSub), true, false});
        menu_.items.push_back(BigMenu::Item{T(PcText::FeCredits), T(PcText::FeCreditsSub), true, false});
        menu_.items.push_back(BigMenu::Item{T(PcText::FeQuit), T(PcText::FeQuitSub), true, false});
    }
    if (v == View::Options && opts_ == OptsFrom::None && s.front.option_page != skip_option_page_) {
        // Uma página de opções do codinome que o jogo mostrou (Controller Setup depois de criar um codinome): as opções
        // Modern na categoria; sair delas é o voltar do jogo.
        open_options(cat_for_page(s.front.option_page), OptsFrom::Game);
    }
    if (v == View::Dossier) dossier_.enter(s, Frame{*tk_, last_cv_, Measure{}, time_});
    sync_from_game(s);
    // De volta dos créditos (o jogo volta sozinho no fim, ou pelo voltar): a linha CREDITS continua com o foco.
    if (v == View::Main && from == View::Credits) menu_.focus = kMainCredits;
}

void FrontScreen::sync_from_game(const MmSnapshot& s) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    switch (view_) {
        case View::Main:
            menu_.focus = std::clamp(s.front.focus, 0, std::max(0, int(menu_.items.size()) - 1));
            menu_.mark = FocusMark{};
            break;
        case View::Name:
            layout_keys();
            key_focus_ = 0;
            for (size_t i = 0; i < key_order_.size(); ++i) {
                if (s.front.keys[size_t(key_order_[i])].tag == kMmFeKeyEnd) key_focus_ = int(i);
            }
            break;
        default:
            if (is_list(view_)) {
                focus_ = std::clamp(s.front.pos, 0, std::max(0, row_count() - 1));
                ensure_visible(focus_);
                scroll_.snap(scroll_target_);
                lit_.assign(size_t(row_count()), Tween{});
                if (size_t(focus_) < lit_.size()) lit_[size_t(focus_)].snap(1.0f);
                place_mark(false);
            }
            break;
    }
    (void)f;
}

// ---------------------------------------------------------------- linhas

int FrontScreen::row_count() const {
    if (view_ == View::Main) return int(menu_.items.size());
    if (is_list(view_)) return int(snap_.front.items.size());
    return 0;
}

bool FrontScreen::row_enabled(int i) const {
    if (i < 0 || i >= row_count()) return false;
    if (view_ == View::CodenameMenu) return i != kCnDriving && i != kCnMultiplayer;
    if (is_list(view_)) return snap_.front.items[size_t(i)].enabled;
    return true;
}

float FrontScreen::row_y(int i) const { return float(i) * tk_->space.row_h; }

int FrontScreen::focus() const {
    if (view_ == View::Main) return menu_.focus;
    if (view_ == View::Name) return key_focus_;
    return focus_;
}

Rect FrontScreen::row_rect(int i) const {
    const auto& sp = tk_->space;
    if (view_ == View::Main) return i >= 0 && size_t(i) < menu_.rects.size() ? menu_.rects[size_t(i)] : Rect{};
    const float y = sp.front_top + row_y(i) - scroll_.value();
    return Rect{sp.mid_x0, y, sp.mid_x1, y + sp.row_h};
}

Rect FrontScreen::key_rect(int i) const { return i >= 0 && size_t(i) < key_rects_.size() ? key_rects_[size_t(i)] : Rect{}; }

uint32_t FrontScreen::key_tag(int i) const {
    if (i < 0 || size_t(i) >= key_order_.size()) return 0xffffffffu;
    return snap_.front.keys[size_t(key_order_[size_t(i)])].tag;
}

Rect FrontScreen::tab_rect(int i) const {
    return i >= 0 && size_t(i) < tabs_.tab_rects.size() ? tabs_.tab_rects[size_t(i)] : Rect{};
}

void FrontScreen::ensure_visible(int row) {
    const auto& sp = tk_->space;
    const float view = sp.list_bottom - sp.front_top;
    const float y0 = row_y(row), y1 = y0 + sp.row_h;
    float t = scroll_target_;
    if (y0 - 24.0f < t) t = y0 - 24.0f;
    if (y1 + 24.0f > t + view) t = y1 + 24.0f - view;
    const float total = row_y(row_count());
    t = std::clamp(t, 0.0f, std::max(0.0f, total - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void FrontScreen::place_mark(bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const auto& sp = tk_->space;
    if (!is_list(view_) || row_count() == 0) {
        mark_.show(false, f);
        return;
    }
    mark_.move_to(sp.mid_x0, sp.mid_x1 - 8.0f, row_y(focus_) + sp.row_h - 1.0f, f, animate);
    mark_.show(true, f);
}

void FrontScreen::move_focus(int to) {
    const int n = row_count();
    if (n == 0) return;
    to = std::clamp(to, 0, n - 1);
    if (to == focus_) return;
    focus_ = to;
    ensure_visible(focus_);
    place_mark(true);
    // A roda do jogo acompanha (como o direcional: a descrição, a imagem e os rótulos dele).
    if (snap_.front.wheel != 0) MenuModern_Step(snap_.front.wheel, focus_);
}

// ---------------------------------------------------------------- o teclado da tela

void FrontScreen::layout_keys() {
    const auto& sp = tk_->space;
    const std::vector<MmKey>& keys = snap_.front.keys;
    key_order_.clear();
    key_rects_.clear();
    // As teclas comuns em linhas pela altura delas no arquivo (cada linha da esquerda para a direita); as especiais
    // (espaço, apagar, pronto) numa última linha.
    std::vector<int> normal;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].tag < kMmFeKeyDel) normal.push_back(int(i));
    }
    std::sort(normal.begin(), normal.end(), [&](int a, int b) {
        const MmKey& ka = keys[size_t(a)];
        const MmKey& kb = keys[size_t(b)];
        if (std::abs(ka.y - kb.y) > 4) return ka.y < kb.y;
        return ka.x < kb.x;
    });
    const float cell = sp.key_cell, gap = sp.key_gap;
    const float cx = (sp.mid_x0 + sp.mid_x1) * 0.5f;
    float y = 420.0f;
    size_t i = 0;
    while (i < normal.size()) {
        size_t j = i + 1;
        while (j < normal.size() && std::abs(keys[size_t(normal[j])].y - keys[size_t(normal[i])].y) <= 4) ++j;
        const float w = float(j - i) * cell + float(j - i - 1) * gap;
        float x = cx - w * 0.5f;
        for (size_t k = i; k < j; ++k) {
            key_order_.push_back(normal[k]);
            key_rects_.push_back(Rect{x, y, x + cell, y + cell});
            x += cell + gap;
        }
        y += cell + gap;
        i = j;
    }
    struct Special {
        uint32_t tag;
        float cells;
    };
    const Special specials[3] = {{kMmFeKeySpace, 3.0f}, {kMmFeKeyDel, 2.0f}, {kMmFeKeyEnd, 2.0f}};
    std::vector<std::pair<int, float>> row;
    for (const Special& sp2 : specials) {
        for (size_t k = 0; k < keys.size(); ++k) {
            if (keys[k].tag == sp2.tag) row.push_back({int(k), sp2.cells * cell + (sp2.cells - 1.0f) * gap});
        }
    }
    float w = 0.0f;
    for (const auto& r : row) w += r.second;
    w += gap * float(std::max<size_t>(1, row.size()) - 1);
    float x = cx - w * 0.5f;
    y += 8.0f;
    for (const auto& r : row) {
        key_order_.push_back(r.first);
        key_rects_.push_back(Rect{x, y, x + r.second, y + cell});
        x += r.second + gap;
    }
    key_focus_ = std::clamp(key_focus_, 0, std::max(0, int(key_rects_.size()) - 1));
}

void FrontScreen::press_key(int i) {
    const uint32_t tag = key_tag(i);
    if (tag == 0xffffffffu) return;
    MenuModern_Key(tag);
}

void FrontScreen::type_char(char c) {
    const std::vector<MmKey>& keys = snap_.front.keys;
    uint32_t tag = 0xffffffffu;
    if (c == '\b') tag = kMmFeKeyDel;
    else if (c == '\n' || c == '\r') tag = kMmFeKeyEnd;
    else tag = MenuModern_KeyFor(keys, c);
    if (tag == 0xffffffffu) return;
    // A tecla da tela acompanha (o foco vai a ela, como no jogo).
    for (size_t i = 0; i < key_order_.size(); ++i) {
        if (keys[size_t(key_order_[i])].tag == tag && int(i) != key_focus_) {
            key_focus_ = int(i);
            const Frame f{*tk_, last_cv_, Measure{}, time_};
            const Rect& r = key_rects_[i];
            key_mark_.move_to(r.x0 + 4.0f, r.x1 - 4.0f, r.y1 + 6.0f, f, true);
        }
    }
    MenuModern_Key(tag);
}

void FrontScreen::act_name(Nav c) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const int n = int(key_rects_.size());
    if (n == 0) {
        if (c == Nav::Back) {
            MenuModern_Press(kMmBack);
            waiting_ = true;
        }
        return;
    }
    const auto go = [&](int to) {
        if (to < 0 || to == key_focus_) return;
        key_focus_ = to;
        const Rect& r = key_rects_[size_t(to)];
        key_mark_.move_to(r.x0 + 4.0f, r.x1 - 4.0f, r.y1 + 6.0f, f, true);
    };
    const Rect cur = key_rects_[size_t(std::clamp(key_focus_, 0, n - 1))];
    switch (c) {
        case Nav::Left:
        case Nav::Right: {
            // Na mesma linha, a vizinha; nas pontas, para.
            const bool right = c == Nav::Right;
            int best = -1;
            float bd = 1e9f;
            for (int i = 0; i < n; ++i) {
                const Rect& r = key_rects_[size_t(i)];
                if (std::fabs(r.cy() - cur.cy()) > 4.0f || i == key_focus_) continue;
                const float d = right ? r.cx() - cur.cx() : cur.cx() - r.cx();
                if (d > 0.0f && d < bd) {
                    bd = d;
                    best = i;
                }
            }
            go(best);
            break;
        }
        case Nav::Up:
        case Nav::Down: {
            // A linha de cima ou de baixo: a tecla mais perto na horizontal.
            const bool down = c == Nav::Down;
            float row = down ? 1e9f : -1e9f;
            for (int i = 0; i < n; ++i) {
                const float y = key_rects_[size_t(i)].cy();
                if (down ? (y > cur.cy() + 4.0f && y < row) : (y < cur.cy() - 4.0f && y > row)) row = y;
            }
            int best = -1;
            float bd = 1e9f;
            for (int i = 0; i < n; ++i) {
                const Rect& r = key_rects_[size_t(i)];
                if (std::fabs(r.cy() - row) > 4.0f) continue;
                const float d = std::fabs(r.cx() - cur.cx());
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
            go(best);
            break;
        }
        case Nav::Select: press_key(key_focus_); break;
        case Nav::Start: type_char('\n'); break;
        case Nav::Back:
            MenuModern_Press(kMmBack);
            waiting_ = true;
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- ações

void FrontScreen::open_options(Cat cat, OptsFrom from) {
    opts_ = from;
    // Do menu principal, as opções inteiras (voltar das linhas vai às categorias); do menu do codinome e das páginas
    // de opções do jogo, o atalho direto na categoria (voltar das linhas já sai: OptionsScreen::open_at).
    if (from == OptsFrom::Main) options_.open(cat);
    else options_.open_at(cat, true);
    if (from == OptsFrom::Codename || from == OptsFrom::Game) MenuModern_CodenameOptionsBegin();
    tabs_ = TabBar{};
    content_.snap(0.0f);
    content_.set(1.0f, tk_->motion.content, tk_->motion.ease_out);
    nav_.reset();
}

void FrontScreen::close_options() {
    const OptsFrom from = opts_;
    if (options_.active()) options_.force_close();
    opts_ = OptsFrom::None;
    tabs_ = TabBar{};
    tabs_on_ = false;
    if (from == OptsFrom::Codename) MenuModern_CodenameOptionsEnd();
    if (from == OptsFrom::Game) {
        // A página de opções do jogo continua aberta por baixo: o voltar dela (o jogo volta ao menu do codinome).
        MenuModern_CodenameOptionsEnd();
        MenuModern_Press(kMmBack);
        skip_option_page_ = snap_.front.option_page;
        waiting_ = true;
    }
    content_.snap(0.0f);
    content_.set(1.0f, tk_->motion.content, tk_->motion.ease_out);
    menu_.mark = FocusMark{};
    place_mark(false);
    nav_.reset();
}

void FrontScreen::quit_dialog() {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    dialog_.show(T(PcText::FeQuitTitle), T(PcText::FeQuitBody), {T(PcText::FeQuitYes), T(PcText::MdCancel)}, 1, f);
    dialog_kind_ = DialogKind::Quit;
    nav_.reset();
}

void FrontScreen::show_game_box(const MmSnapshot& s) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    std::vector<std::wstring> buttons;
    for (const MmItem& o : s.dialog.options) {
        std::wstring t = widen(o.text);
        if (t.find(L'~') != std::wstring::npos) t = T(PcText::MrContinue);  // "Press ~A to Continue"
        buttons.push_back(t);
    }
    std::wstring title = widen(s.dialog.title);
    if (title.empty()) {
        if (s.page == kMmCardPage) title = T(PcText::FeCardTitle);
        else title = T(buttons.size() > 1 ? PcText::FeConfirmTitle : PcText::FeNoticeTitle);
    }
    const int focus = std::clamp(s.dialog.focus, 0, std::max(0, int(buttons.size()) - 1));
    const bool was_open = dialog_.open && dialog_kind_ == DialogKind::Game;
    const float k = dialog_.appear.value();
    dialog_.show(title, widen(s.dialog.body), buttons, focus, f);
    if (was_open) dialog_.appear.snap(k);  // só o texto mudou ("Saving data..." e depois a pergunta)
    dialog_.busy = buttons.empty();
    dialog_kind_ = DialogKind::Game;
    dialog_list_ = s.dialog.list;
    dialog_tag_ = s.dialog.tag;
}

void FrontScreen::activate(int i) {
    switch (view_) {
        case View::Title:
            // O rótulo de tela cheia do "Press START" (o script dele mostra 0x4000004a), depois do texto legal.
            if (snap_.page == kMmFeStart && !snap_.front.press.empty()) {
                MenuModern_Press(kMmSelect);
                waiting_ = true;
            }
            return;
        case View::Main:
            if (i >= 0 && i <= 2) {
                // O botão do jogo: o foco nele (se ainda não) e o X (C_GONIGHTFIRE, C_GOMULTIPLAYER, C_GOCODENAMES).
                if (snap_.front.focus != i) MenuModern_Focus(kMmFeMainButtons[i], kMmAnyTag);
                MenuModern_Press(kMmSelect);
                waiting_ = true;
            } else if (i == kMainOptions) {
                open_options(options_.category() < Cat::Count ? options_.category() : Cat::Video, OptsFrom::Main);
            } else if (i == kMainCredits) {
                // Os créditos do jogo (só do port: no Modern a AV Options, de onde o jogo os abre, não aparece): a
                // página deles mostrada como o script do "Credits" da AV Options faz; no fim o jogo volta sozinho.
                MenuModern_Show(kMmFeCredits);
                waiting_ = true;
            } else if (i == kMainQuit) {
                quit_dialog();
            }
            return;
        case View::CodenameMenu:
            switch (i) {
                case kCnControls: open_options(cat_for_page(kMmFeControls), OptsFrom::Codename); return;
                case kCnAdvanced: open_options(cat_for_page(kMmFeAdvanced), OptsFrom::Codename); return;
                case kCnAv: open_options(cat_for_page(kMmFeAv), OptsFrom::Codename); return;
                case kCnDriving:
                case kCnMultiplayer: toast_.show(T(PcText::FeNotAvailableToast)); return;
                default: break;
            }
            break;
        default: break;
    }
    if (!is_list(view_) || i < 0 || i >= row_count()) return;
    if (!row_enabled(i)) {
        toast_.show(T(view_ == View::Map ? PcText::FeLockedToast : PcText::FeNotAvailableToast));
        return;
    }
    // A roda na linha e o X (o tratador da roda decide: ler o codinome, a dificuldade, a missão...).
    if (snap_.front.pos != i) MenuModern_Step(snap_.front.wheel, i);
    MenuModern_Press(kMmSelect);
    waiting_ = true;
}

void FrontScreen::act_dialog(Nav c) {
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
        if (dialog_kind_ == DialogKind::Game) MenuModern_Pick(dialog_list_, dialog_tag_, to);
    };
    switch (c) {
        case Nav::Left:
        case Nav::Up: move(-1); break;
        case Nav::Right:
        case Nav::Down: move(1); break;
        case Nav::Select:
        case Nav::Start:
            if (dialog_kind_ == DialogKind::Quit) {
                if (dialog_.focus == 0) quit_ = true;
                dialog_.hide(f);
                dialog_kind_ = DialogKind::None;
                nav_.reset();
            } else {
                MenuModern_Select(dialog_list_, dialog_tag_, dialog_.focus);
                waiting_ = true;
            }
            break;
        case Nav::Back:
            if (dialog_kind_ == DialogKind::Quit) {
                dialog_.hide(f);
                dialog_kind_ = DialogKind::None;
                nav_.reset();
            } else {
                // A caixa do jogo: voltar responde "não" (C_LBMSGOPTIONS_Handler) ou volta da do cartão.
                MenuModern_Press(kMmBack);
                waiting_ = true;
            }
            break;
        default: break;
    }
}

void FrontScreen::act(Nav c) {
    if (dialog_.open) {
        act_dialog(c);
        return;
    }
    if (view_ == View::Name) {
        act_name(c);
        return;
    }
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    switch (c) {
        case Nav::Up:
        case Nav::Down: {
            const int dir = c == Nav::Up ? -1 : 1;
            if (view_ == View::Main) {
                const int to = std::clamp(menu_.focus + dir, 0, int(menu_.items.size()) - 1);
                if (to != menu_.focus) {
                    menu_.set_focus(to, f, true);
                    if (to <= 2) MenuModern_Focus(kMmFeMainButtons[to], kMmAnyTag);
                }
            } else if (is_list(view_)) {
                move_focus(focus_ + dir);
            }
            break;
        }
        case Nav::Select:
        case Nav::Start: activate(view_ == View::Main ? menu_.focus : focus_); break;
        case Nav::Back:
            if (view_ == View::Main) {
                quit_dialog();
            } else if (view_ != View::Title && view_ != View::Dossier) {
                MenuModern_Press(kMmBack);
                waiting_ = true;
            }
            break;
        case Nav::TabNext:
            if (view_ == View::Map) {
                // O dossiê: o quadrado na roda das missões (C_SBNFMAP_Handler, a página do dossiê no estilo Original).
                if (snap_.front.pos != focus_) MenuModern_Step(snap_.front.wheel, focus_);
                MenuModern_Press(kMmSquare);
                waiting_ = true;
            }
            break;
        default: break;
    }
}

void FrontScreen::mouse() {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (!nav_.mouse_active || !nav_.inside) return;
    const float x = nav_.mx, y = nav_.my;
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && (nav_.moved || nav_.pressed) && h != dialog_.focus) {
            dialog_.focus = h;
            const Rect& r = dialog_.button_rects[size_t(h)];
            dialog_.mark.move_to(r.x0, r.x1, r.y1, f, true);
            if (dialog_kind_ == DialogKind::Game) MenuModern_Pick(dialog_list_, dialog_tag_, h);
        }
        if (nav_.pressed && h >= 0) act_dialog(Nav::Select);
        else if (nav_.back) act_dialog(Nav::Back);
        return;
    }
    if (nav_.back) {
        act(Nav::Back);
        return;
    }
    if (view_ == View::Title) {
        if (nav_.pressed) activate(0);
        return;
    }
    if (nav_.pressed) {
        if (tabs_on_) {
            const int t = tabs_.hit(x, y);
            if (t == 1 || t == -3) {
                act(Nav::TabNext);
                return;
            }
        }
        switch (bar_.hit(x, y)) {
            case kActSelect:
                if (view_ == View::Name) press_key(key_focus_);
                else act(Nav::Select);
                return;
            case kActBack: act(Nav::Back); return;
            case kActDossier: act(Nav::TabNext); return;
            case kActDelete:
                if (view_ == View::Name) {
                    type_char('\b');
                } else if (view_ == View::Codenames && focus_ >= 2) {
                    MenuModern_Step(snap_.front.wheel, focus_);
                    MenuModern_Press(kMmCircle);
                    waiting_ = true;
                }
                return;
            case kActSpace: type_char(' '); return;
            case kActDone: type_char('\n'); return;
            default: break;
        }
    }
    if (view_ == View::Main) {
        const int h = menu_.hit(x, y);
        menu_.hover = h;
        if (h >= 0 && (nav_.moved || nav_.pressed) && h != menu_.focus) {
            menu_.set_focus(h, f, true);
            if (h <= 2) MenuModern_Focus(kMmFeMainButtons[h], kMmAnyTag);
        }
        if (h >= 0 && nav_.pressed) activate(h);
        return;
    }
    if (view_ == View::Name) {
        int h = -1;
        for (size_t i = 0; i < key_rects_.size(); ++i) {
            if (key_rects_[i].contains(x, y)) h = int(i);
        }
        key_hover_ = h;
        if (h >= 0 && (nav_.moved || nav_.pressed) && h != key_focus_) {
            key_focus_ = h;
            const Rect& r = key_rects_[size_t(h)];
            key_mark_.move_to(r.x0 + 4.0f, r.x1 - 4.0f, r.y1 + 6.0f, f, true);
        }
        if (h >= 0 && nav_.pressed) press_key(h);
        return;
    }
    if (!is_list(view_)) return;
    const auto& sp = tk_->space;
    // A roda do mouse rola a lista; passar por cima dá o foco (a roda do jogo acompanha); o clique escolhe.
    if (nav_.wheel != 0.0f && x >= sp.mid_x0 - 40.0f && x < sp.mid_x1 + 40.0f) {
        const float view = sp.list_bottom - sp.front_top;
        scroll_target_ = std::clamp(scroll_target_ - nav_.wheel * sp.row_h, 0.0f, std::max(0.0f, row_y(row_count()) - view));
        scroll_.set(scroll_target_, tk_->motion.scroll, tk_->motion.ease_out);
    }
    int h = -1;
    for (int i = 0; i < row_count(); ++i) {
        const Rect r = row_rect(i);
        if (r.contains(x, y) && y >= sp.front_top && y < sp.list_bottom) h = i;
    }
    hover_ = h;
    if (h < 0) return;
    if ((nav_.moved || nav_.pressed) && h != focus_) {
        focus_ = h;
        place_mark(true);
        if (snap_.front.wheel != 0) MenuModern_Step(snap_.front.wheel, focus_);
    }
    if (nav_.pressed) activate(h);
}

// ---------------------------------------------------------------- o quadro

void FrontScreen::layout_tabs(const Frame& f, const UiInput& in) {
    tabs_on_ = opts_ != OptsFrom::None || view_ == View::Map;
    if (!tabs_on_) return;
    if (opts_ != OptsFrom::None) {
        const std::wstring ctx =
            opts_ == OptsFrom::Main ? T(PcText::FeMainHeader) : T(PcText::FeStepCodename);
        tabs_.labels = {ctx, T(PcText::MdTopOptions)};
    } else {
        tabs_.labels = {T(PcText::FeMissions), T(PcText::MrDossier)};
    }
    const bool pad = in.device == 1;
    const std::wstring kp = bind_key("menu_tab_prev"), kn = bind_key("menu_tab_next");
    tabs_.key_prev = kp.empty() ? L"Q" : kp;
    tabs_.key_next = kn.empty() ? L"E" : kn;
    tabs_.glyph_prev = pad ? bind_glyph("menu_tab_prev", "xbox_lb") : std::string();
    tabs_.glyph_next = pad ? bind_glyph("menu_tab_next", "xbox_rb") : std::string();
    tabs_.corner_left = L"NF.PC / FE";
    tabs_.corner_right = L"SLUS-20579";
    const bool first = !tabs_.laid_out;
    tabs_.layout(f);
    const int active = opts_ != OptsFrom::None ? 1 : 0;
    if (first) tabs_.set_active(active, f, false);
    else if (tabs_.active != active) tabs_.set_active(active, f, true);
}

void FrontScreen::frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    blur_.update(dt);
    content_.update(dt);
    mark_.update(dt);
    key_mark_.update(dt);
    scroll_.update(dt);
    tabs_.update(dt);
    dialog_.update(dt);
    toast_.update(dt, *tk_);
    pic_t_.update(dt);
    const Frame f{*tk_, cv, m, time_};
    menu_.update(dt, f);
    for (size_t i = 0; i < lit_.size(); ++i) {
        lit_[i].update(dt);
        lit_[i].set(int(i) == focus_ ? 1.0f : 0.0f, tk_->motion.focus, tk_->motion.ease_out);
    }
    if (closing_) {
        if (appear_.done()) active_ = closing_ = false;
        return;
    }
    // Um botão do jogo na fila: a tela espera o efeito dele; os passos das rodas e os focos não seguram a navegação.
    waiting_ = MenuModern_Queued(MmAction::Press) || MenuModern_Queued(MmAction::Show);
    const bool moving = MenuModern_Queued(MmAction::Step) || MenuModern_Queued(MmAction::Focus);

    // A vista segue a página do jogo (a página de opções do jogo já fechada fica até o jogo voltar dela).
    View v = view_of(s);
    if (v == View::Options && s.front.option_page == skip_option_page_ && skip_option_page_ != 0) v = view_;
    if (v != View::Options) skip_option_page_ = 0;
    if (v != view_) change_view(v, s);
    const size_t items_before = snap_.front.items.size();
    snap_ = s;
    if (is_list(view_)) {
        if (lit_.size() != size_t(row_count())) lit_.resize(size_t(row_count()));
        // A roda do jogo mudou sozinha (a lista de codinomes lida, a última missão liberada ao entrar): a tela vai junto.
        if (!waiting_ && !moving && s.front.pos != focus_ && s.front.pos >= 0 && s.front.pos < row_count()) {
            focus_ = s.front.pos;
            ensure_visible(focus_);
            if (items_before != s.front.items.size()) scroll_.snap(scroll_target_);
            place_mark(items_before == s.front.items.size());
        }
        if (!mark_.placed) place_mark(false);
    }
    if (view_ == View::Name) {
        layout_keys();
        if (!key_mark_.placed && size_t(key_focus_) < key_rects_.size()) {
            const Rect& r = key_rects_[size_t(key_focus_)];
            key_mark_.move_to(r.x0 + 4.0f, r.x1 - 4.0f, r.y1 + 6.0f, f, false);
            key_mark_.show(true, f);
        }
    }
    if (view_ == View::Main) {
        menu_.focused = !dialog_.open && opts_ == OptsFrom::None;
        menu_.layout(f);
        if (!menu_.mark.placed) menu_.set_focus(menu_.focus, f, false);
    }
    blur_.set(view_ == View::Title ? 0.0f : 1.0f, tk_->motion.blur_in, tk_->motion.ease_in_out);
    // A imagem da missão: a do item com o foco (a anterior some).
    if (view_ == View::Map && focus_ >= 0 && focus_ < row_count()) {
        const MmWheelItem& it = s.front.items[size_t(focus_)];
        if (it.image != pic_) {
            pic_prev_ = pic_;
            pic_prev_on_ = pic_on_;
            pic_ = it.image;
            pic_t_.snap(0.0f);
            pic_t_.set(1.0f, tk_->motion.content * 1.6f, tk_->motion.ease_out);
        }
        pic_on_ = it.enabled;
    }

    // As caixas do jogo (a de mensagem e a do cartão) viram o Dialog do kit, e somem com ela.
    if (s.dialog.open) {
        std::string sig = s.dialog.body;
        for (const MmItem& o : s.dialog.options) sig += "|" + o.text;
        if (dialog_kind_ != DialogKind::Game || sig != dialog_sig_) {
            const bool fresh = dialog_kind_ != DialogKind::Game;
            show_game_box(s);
            dialog_sig_ = sig;
            if (fresh) nav_.reset();
        }
    } else if (dialog_kind_ == DialogKind::Game) {
        dialog_.hide(f);
        dialog_kind_ = DialogKind::None;
        nav_.reset();
    }
    if (dialog_.open) dialog_.layout(f);
    layout_tabs(f, in);

    // As opções embutidas: a tela de opções lê a entrada dela; Q e voltar das categorias fecham.
    if (opts_ != OptsFrom::None) {
        nav_.read(in, cv, dt, *tk_, true);
        static const OptionsScreen::Cmd kCmd[kNavCount] = {
            OptionsScreen::Cmd::Up,      OptionsScreen::Cmd::Down,    OptionsScreen::Cmd::Left,
            OptionsScreen::Cmd::Right,   OptionsScreen::Cmd::Select,  OptionsScreen::Cmd::Back,
            OptionsScreen::Cmd::TabPrev, OptionsScreen::Cmd::TabNext, OptionsScreen::Cmd::Select};
        options_.frame(dt, in, cv, m);
        for (int i = 0; i < kNavCount; ++i) {
            if (pending_[i] && Nav(i) != Nav::Start) options_.command(kCmd[i]);
            pending_[i] = false;
        }
        const int t = options_.take_tab_request();
        bool close = t < 0;
        if (options_.take_back_request()) close = true;
        if (nav_.pressed && nav_.inside && options_.capturing() < 0) {
            const int h = tabs_.hit(nav_.mx, nav_.my);
            if (h == 0 || h == -2) close = true;
        }
        if (close) close_options();
        return;
    }

    // A entrada. Na vista do nome o teclado digita: as letras, os algarismos, o espaço, Backspace e Enter não vão aos
    // comandos do menu (as setas e Esc continuam).
    bool filtered[256] = {};
    UiInput uin = in;
    bool typing_keys[256] = {};
    for (int vk = 'A'; vk <= 'Z'; ++vk) typing_keys[vk] = true;
    for (int vk = '0'; vk <= '9'; ++vk) typing_keys[vk] = true;
    for (int vk = nf::key::Numpad0; vk <= nf::key::Numpad9; ++vk) typing_keys[vk] = true;
    typing_keys[nf::key::Space] = typing_keys[nf::key::Back] = typing_keys[nf::key::Return] = typing_keys[nf::key::Delete] = true;
    const bool name_typing = view_ == View::Name && !dialog_.open;
    if (name_typing && in.keys != nullptr) {
        for (int vk = 0; vk < 256; ++vk) filtered[vk] = in.keys[vk] && !typing_keys[vk];
        uin.keys = filtered;
    }
    nav_.read(uin, cv, dt, *tk_, true);
    for (int i = 0; i < kNavCount; ++i) {
        if (pending_[i]) nav_.inject(Nav(i));
        pending_[i] = false;
    }
    if (view_ == View::Dossier && !dialog_.open) {
        // O dossiê: a vista dele segue as páginas do jogo e manda os pedidos; aqui só o clique no "voltar" de baixo.
        dossier_.frame(dt, nav_, in, s, f);
        if (s.ready && nav_.mouse_active && nav_.inside && nav_.pressed && bar_.hit(nav_.mx, nav_.my) == kActBack) dossier_.leave();
        prompts(bar_);
        bar_.layout(f);
        return;
    }
    bool edge[256] = {};
    for (int vk = 0; vk < 256; ++vk) {
        const bool now = in.keys != nullptr && in.keys[vk];
        edge[vk] = now && !prev_keys_[vk];
        prev_keys_[vk] = now;
    }
    const uint16_t pad_now = in.pad != nullptr && in.pad->connected ? in.pad->buttons : 0;
    const uint16_t pad_edge = prev_pad_ == 0xffff ? 0 : uint16_t(pad_now & ~prev_pad_);
    prev_pad_ = pad_now;
    std::string typed;
    typed.swap(typed_);
    if (s.ready && !dialog_.open) {
        if (name_typing) {
            // O teclado do PC digita pelas teclas do jogo (a fila guarda a ordem).
            for (int vk = 'A'; vk <= 'Z'; ++vk) {
                if (edge[vk]) typed.push_back(char(vk));
            }
            for (int d = 0; d <= 9; ++d) {
                if (edge['0' + d] || edge[nf::key::Numpad0 + d]) typed.push_back(char('0' + d));
            }
            if (edge[nf::key::Space]) typed.push_back(' ');
            if (edge[nf::key::Back] || edge[nf::key::Delete]) typed.push_back('\b');
            if (edge[nf::key::Return]) typed.push_back('\n');
            if ((pad_edge & nf::kPadX) != 0) typed.push_back('\b');
            if ((pad_edge & nf::kPadY) != 0) typed.push_back(' ');
            for (const char c : typed) type_char(c);
        } else if (view_ == View::Codenames && focus_ >= 2 && !waiting_ &&
                   (edge[nf::key::Delete] || (pad_edge & nf::kPadX) != 0)) {
            // Apagar um codinome do cartão: o círculo do jogo (C_SBCNSELECT_Handler, com a pergunta do cartão).
            MenuModern_Step(s.front.wheel, focus_);
            MenuModern_Press(kMmCircle);
            waiting_ = true;
        } else if ((view_ == View::Campaign || view_ == View::Codenames) && !waiting_ && (pad_edge & nf::kPadY) != 0) {
            // Criar: o quadrado do jogo ("~Y Create").
            MenuModern_Press(kMmSquare);
            waiting_ = true;
        } else if (view_ == View::Map && !waiting_ && (pad_edge & nf::kPadX) != 0) {
            act(Nav::TabNext);
        }
    }
    const bool can_act = s.ready && (!waiting_ || view_ == View::Name);
    if (can_act) {
        for (int i = 0; i < kNavCount; ++i) {
            if (!nav_.fire(Nav(i))) continue;
            act(Nav(i));
            if (waiting_ && view_ != View::Name) break;
        }
        if (!waiting_ || view_ == View::Name) mouse();
    }
    prompts(bar_);
    bar_.layout(f);
}

bool FrontScreen::mouse_active() const {
    if (opts_ != OptsFrom::None && options_.active()) return options_.mouse_active() || nav_.mouse_active;
    return nav_.mouse_active;
}

// ---------------------------------------------------------------- desenho

void FrontScreen::prompts(PromptBar& bar) const {
    const bool pad = nav_.device == 1;
    bar.prompts.clear();
    bar.info_title = T(PcText::MdFooterPort);
    bar.info_sub = L"SLUS-20579 · NTSC-U";
    char buf[128];
    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdFooterBuild), build_.empty() ? "dev" : build_.c_str());
    bar.info2_title = widen(buf);
    bar.info2_sub = T(PcText::MdFooterGame);
    if (!snap_.front.profile.empty() && snap_.front.slot >= 2 && snap_.front.slot != 999) {
        bar.info2_sub = T(PcText::FeProfile) + L" " + widen(snap_.front.profile);
    }
    const auto add = [&](int action, const std::wstring& key, const std::string& glyph, const std::wstring& label) {
        Prompt p;
        p.action = action;
        p.key = key;
        p.glyph = pad ? glyph : std::string();
        p.label = label;
        bar.prompts.push_back(p);
    };
    const std::wstring sel = bind_key("menu_select");
    const std::wstring select_key = sel.empty() ? L"Enter" : sel;
    if (dialog_.open) {
        if (!dialog_.buttons.empty()) add(kActSelect, select_key, "xbox_a", T(PcText::MdPromptSelect));
        if (dialog_kind_ == DialogKind::Quit || !dialog_.buttons.empty()) add(kActBack, L"Esc", "xbox_b", T(PcText::MdPromptBack));
        return;
    }
    switch (view_) {
        case View::Title: return;
        case View::Main: add(kActSelect, select_key, "xbox_a", T(PcText::MdPromptSelect)); return;
        case View::Dossier: dossier_.prompts(bar, pad); return;
        case View::Credits: add(kActBack, L"Esc", "xbox_b", T(PcText::MdPromptBack)); return;
        case View::Name:
            // No teclado as letras digitam (Espaço é o espaço): escolher uma tecla da grade só no controle e no mouse.
            if (pad) add(kActSelect, select_key, "xbox_a", T(PcText::MdPromptSelect));
            add(kActDelete, L"Backspace", "xbox_x", T(PcText::FeKeyDel));
            if (pad) add(kActSpace, L"Space", "xbox_y", T(PcText::FeKeySpace));
            add(kActDone, L"Enter", "xbox_menu", T(PcText::FeKeyEnd));
            add(kActBack, L"Esc", "xbox_b", T(PcText::MdPromptBack));
            return;
        default: break;
    }
    if (view_ == View::Codenames && focus_ >= 2) add(kActDelete, L"Delete", "xbox_x", T(PcText::FeKeyDel));
    if (view_ == View::Map) {
        const std::wstring kn = bind_key("menu_tab_next");
        add(kActDossier, kn.empty() ? L"E" : kn, "xbox_x", T(PcText::MrDossier));
    }
    add(kActSelect, select_key, "xbox_a", T(PcText::MdPromptSelect));
    add(kActBack, L"Esc", "xbox_b", T(PcText::MdPromptBack));
}

Rect FrontScreen::art_rect(const Canvas& cv) const {
    // A arte é 4:3 na TV: a altura da janela inteira (as janelas mais estreitas que 4:3, a largura), centrada.
    const float wh = cv.bottom - cv.top, ww = cv.right - cv.left;
    float h = wh, w = wh * 4.0f / 3.0f;
    if (w > ww) {
        w = ww;
        h = ww * 3.0f / 4.0f;
    }
    const float cx = (cv.left + cv.right) * 0.5f, cy = (cv.top + cv.bottom) * 0.5f;
    return Rect{cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f};
}

Rect FrontScreen::picture_rect() const {
    const auto& sp = tk_->space;
    // Abaixo das etapas, com folga para o anel escuro em volta (draw_left).
    const float y0 = 506.0f;
    return Rect{sp.left_x + 4.0f, y0, sp.left_x + 4.0f + sp.picture, y0 + sp.picture};
}

void FrontScreen::build_back(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    (void)m;
    // O fundo é desta tela (o menu principal não tem cena): opaco ao abrir (a tela aparece do preto) e sumindo ao
    // fechar (a página do jogo aparece por baixo).
    const float k = closing_ ? appear_.value() : 1.0f;
    dl.fill(Rect{cv.left, cv.top, cv.right, cv.bottom}, tk_->color.ground.with_alpha(k));
}

void FrontScreen::backdrop(ImageList& out, const Canvas& cv) const {
    if (!active_ || (!art_ && !movie_)) return;
    ImageCmd a;
    a.hash = kArt;
    a.r = art_rect(cv);
    a.uv = Rect{0.5f / 512.0f, 0.5f / 512.0f, 511.5f / 512.0f, 511.5f / 512.0f};
    if (movie_) {
        // O filme de fundo da página (512 x 448, 4:3 na TV como a arte): o quadro inteiro no mesmo retângulo.
        a.hash = kMovieImage;
        a.uv = Rect{0.5f / 512.0f, 0.5f / 448.0f, 511.5f / 512.0f, 447.5f / 448.0f};
    }
    // Nos menus a arte fica mais fraca (o desfoque e o véu fazem o resto).
    a.alpha = appear_.value() * (1.0f - 0.22f * blur_.value());
    out.push_back(a);
}

float FrontScreen::blur() const { return active_ ? blur_.value() : 0.0f; }

void FrontScreen::build_veil(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    const Frame f{*tk_, cv, m, time_};
    const float k = closing_ ? appear_.value() : 1.0f;
    const float b = blur_.value();
    const Color g = tk_->color.ground;
    if (art_ || movie_) {
        // As bordas da arte (ou do filme) somem no fundo.
        const Rect a = art_rect(cv);
        if (a.x0 > cv.left + 1.0f) {
            dl.gradient(Rect{a.x0 - 1.0f, cv.top, a.x0 + 260.0f, cv.bottom}, g.with_alpha(k), g.with_alpha(0.0f), true);
            dl.gradient(Rect{a.x1 - 260.0f, cv.top, a.x1 + 1.0f, cv.bottom}, g.with_alpha(0.0f), g.with_alpha(k), true);
        }
    }
    // O título: escuro embaixo, onde ficam o convite e o texto legal.
    const float t = 1.0f - b;
    if (t > 0.001f) {
        dl.gradient(Rect{cv.left, 560.0f, cv.right, cv.bottom}, g.with_alpha(0.0f), g.with_alpha(0.94f * t * k), false);
    }
    // Os menus: o fundo da pausa (véu e vinheta) sobre a arte desfocada, mais um escurecido para o texto.
    if (b > 0.001f) {
        draw_background(dl, f, b * k);
        dl.fill(Rect{cv.left, cv.top, cv.right, cv.bottom}, g.with_alpha((view_ == View::Credits ? 0.55f : 0.14f) * b * k));
    }
}

void FrontScreen::images(ImageList& out, const Canvas& cv) const {
    if (!active_ || opts_ != OptsFrom::None) return;
    (void)cv;
    const auto& sp = tk_->space;
    const float k = appear_.value();
    const float c = content_.value();
    if (view_ == View::Main) {
        ImageCmd l;
        l.hash = kLogo;
        const float h = sp.logo_h, w = h * kLogoAspect;
        const float dx = sp.slide * (1.0f - k) + 12.0f * (1.0f - c);
        l.r = Rect{sp.left_x - 10.0f - dx, 120.0f, sp.left_x - 10.0f - dx + w, 120.0f + h};
        l.uv = Rect{0.5f / 256.0f, 0.5f / 128.0f, 255.5f / 256.0f, 127.5f / 128.0f};
        l.alpha = k * c;
        out.push_back(l);
    }
    if (view_ == View::Dossier) {
        const float a = k * (0.25f + 0.75f * c);
        const float dx = sp.slide * (1.0f - k) + 12.0f * (1.0f - c);
        dossier_.images(out, a, dx);
    }
    if (view_ == View::Map) {
        // A imagem da missão (redonda, com a moldura de lente dela): a do item com o foco entrando, a anterior saindo;
        // uma bloqueada mais apagada (o jogo a desenha cinza).
        const Rect r = picture_rect();
        const float t = pic_t_.value();
        const auto add = [&](uint32_t hash, bool on, float a) {
            if (hash == 0 || a <= 0.001f) return;
            ImageCmd p;
            p.hash = hash;
            p.r = r;
            p.uv = Rect{0.5f / 128.0f, 0.5f / 128.0f, 127.5f / 128.0f, 127.5f / 128.0f};
            p.alpha = a * (on ? 1.0f : 0.32f);
            out.push_back(p);
        };
        add(pic_prev_, pic_prev_on_, k * c * (1.0f - t));
        add(pic_, pic_on_, k * c * t);
    }
}

void FrontScreen::draw_title(DrawList& dl, const Frame& f, float alpha) const {
    const Tokens& tk = f.tk;
    const float cx = kCanvasW * 0.5f;
    if (!art_) {
        // Sem a arte do jogo: a marca Modern (o 007 de ouro e NIGHTFIRE espaçado).
        TextStyle hs = tk.type.hero;
        hs.tracking = 0.32f;
        line_at(dl, cx, 420.0f, L"007", tk.type.hero_sub, tk.color.accent.with_alpha(alpha), Align::Centre);
        line_at(dl, cx, 500.0f, L"Nightfire", hs, tk.color.text.with_alpha(alpha), Align::Centre);
    }
    // O convite do jogo, depois do texto legal: a tecla (ou o botão) e o texto, respirando devagar.
    const MmFront& fr = snap_.front;
    const bool ready = snap_.page == kMmFeStart && !fr.press.empty();
    if (ready) {
        const float breath = 0.80f + 0.20f * (0.5f + 0.5f * nf::fm::sin(f.time * 2.4f));
        const float a = alpha * breath;
        const bool pad = nav_.device == 1;
        const std::wstring text = widen(fr.press);
        const TextStyle& st = tk.type.hero_sub;
        const std::wstring key = L"Enter";
        const std::string glyph = pad ? std::string("xbox_menu") : std::string();
        const float iw = prompt_icon_width(f, key, glyph);
        const float tw = text_width(f, text, st);
        const float x0 = cx - (iw + 20.0f + tw) * 0.5f;
        const float y = tk.space.press_y;
        draw_prompt_icon(dl, f, x0, y, key, glyph, a);
        line_at(dl, x0 + iw + 20.0f, y, text, st, tk.color.text.with_alpha(a));
        // As linhas finas dos lados do convite.
        const float lw = 120.0f;
        dl.line(x0 - 40.0f - lw, y, x0 - 40.0f, y, tk.color.accent.with_alpha(0.45f * a), 1.0f);
        dl.line(x0 + iw + 20.0f + tw + 40.0f, y, x0 + iw + 20.0f + tw + 40.0f + lw, y, tk.color.accent.with_alpha(0.45f * a), 1.0f);
    }
    // O texto legal do jogo, pequeno, embaixo.
    if (!fr.legal.empty()) {
        const TextStyle& ls = tk.type.legal;
        const float lh = ls.size * ls.line;
        const std::vector<std::wstring> lines = wrap_lines(f, widen(fr.legal), ls, 1240.0f);
        float y = 1050.0f - lh * float(lines.size());
        for (const std::wstring& l : lines) {
            line_at(dl, cx, y, l, ls, tk.color.dim.with_alpha(0.85f * alpha), Align::Centre);
            y += lh;
        }
    }
}

void FrontScreen::draw_main(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    // Sob o logotipo (uma imagem do jogo, images()), a linha fina e o cabeçalho.
    const float x = tk.space.left_x - dx;
    dl.fill(Rect{x, 268.0f, x + 64.0f, 270.0f}, tk.color.accent.with_alpha(alpha));
    dl.line(x + 64.0f, 269.0f, x + 420.0f, 269.0f, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    menu_.draw(dl, f, alpha, -dx);
    // A descrição da linha com o foco, embaixo da lista (o texto de ajuda do jogo nos botões dele).
    std::wstring d;
    const int i = menu_.focus;
    if (i >= 0 && size_t(i) < snap_.front.button_help.size()) d = widen(snap_.front.button_help[size_t(i)]);
    if (i == kMainOptions) d = T(PcText::FeDescOptions);
    if (i == kMainCredits) d = T(PcText::FeDescCredits);
    if (i == kMainQuit) d = T(PcText::FeDescQuit);
    if (!d.empty() && size_t(menu_.items.size()) > 0) {
        const float y0 = menu_.rects.empty() ? 760.0f : menu_.rects.back().y1 + 54.0f;
        float y = y0;
        for (const std::wstring& l : wrap_lines(f, d, tk.type.body, 560.0f)) {
            line_at(dl, x, y, l, tk.type.body, tk.color.text_2.with_alpha(alpha * 0.9f));
            y += tk.type.body.size * tk.type.body.line;
        }
    }
}

void FrontScreen::draw_left(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmFront& fr = snap_.front;
    const float x = sp.left_x + dx;
    // O cabeçalho: a parte do menu principal (o texto do botão do jogo).
    // (os nomes dos botões do menu principal do jogo: "NightFire" e "Codenames").
    const bool campaign = view_ == View::Campaign || view_ == View::Difficulty || view_ == View::Map;
    const std::wstring head = campaign ? L"Nightfire" : L"Codenames";
    line_at(dl, x + 3.0f, sp.header_y, head + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    // O título da página do jogo, grande, com o traço de ouro.
    std::wstring title = widen(fr.title);
    if (title.empty()) title = view_ == View::Map ? T(PcText::FeStepMission) : T(PcText::FeStepCodename);
    const TextStyle ts = fit_style(f, title, tk.type.page_title, 400.0f, 30.0f);
    float y = 236.0f;
    const std::vector<std::wstring> lines = wrap_lines(f, title, ts, 430.0f);
    for (const std::wstring& l : lines) {
        line_at(dl, x - 3.0f, y, l, ts, tk.color.text.with_alpha(alpha));
        y += ts.size * 1.12f;
    }
    y += 4.0f;
    dl.fill(Rect{x, y, x + 64.0f, y + 2.0f}, tk.color.accent.with_alpha(alpha));
    dl.line(x + 64.0f, y + 1.0f, x + 420.0f, y + 1.0f, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    y += 52.0f;
    // As etapas da campanha (codinome, dificuldade, missão): a de agora em ouro, as feitas claras, as que faltam apagadas.
    if (campaign) {
        const int step = view_ == View::Campaign ? 0 : (view_ == View::Difficulty ? 1 : 2);
        static const PcText kSteps[3] = {PcText::FeStepCodename, PcText::FeStepDifficulty, PcText::FeStepMission};
        for (int i = 0; i < 3; ++i) {
            const Color c = i == step ? tk.color.accent : (i < step ? tk.color.text_2 : tk.color.disabled);
            wchar_t num[8];
            std::swprintf(num, 8, L"%02d", i + 1);
            line_at(dl, x, y, num, tk.type.table_head, c.with_alpha(alpha));
            line_at(dl, x + 40.0f, y, T(kSteps[i]), tk.type.section, c.with_alpha(alpha));
            if (i == step) {
                dl.fill(Rect{sp.marker_x - 4.0f + dx, y - 4.0f, sp.marker_x + 4.0f + dx, y + 4.0f}, tk.color.accent.with_alpha(alpha));
            }
            y += 36.0f;
        }
    }
    if (view_ == View::Map) {
        // Em volta da imagem da missão (images(): redonda, a lente do jogo), um anel escuro que esconde os cantos
        // (a substituta HD do pacote do PCSX2 vem quadrada) e a moldura fina.
        const Rect r = picture_rect().offset(dx, 0.0f);
        const float rad = r.w() * 0.5f;
        const float ring = rad * 0.48f;
        const float mid = rad * 0.985f + ring * 0.5f;
        dl.ellipse(Rect{r.cx() - mid, r.cy() - mid, r.cx() + mid, r.cy() + mid}, tk.color.ground.with_alpha(alpha), ring);
        dl.ellipse(Rect{r.x0 - 10.0f, r.y0 - 10.0f, r.x1 + 10.0f, r.y1 + 10.0f}, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    }
}

void FrontScreen::draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmFront& fr = snap_.front;
    const float top = sp.front_top, bottom = sp.list_bottom;
    const float scroll = scroll_.value();
    const int n = row_count();
    dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, top - 2.0f, sp.mid_x1 + 20.0f + dx, bottom});
    if (n == 0 && fr.listing) {
        line_at(dl, sp.mid_x0 + sp.pad_l + dx, top + sp.row_h * 0.5f, T(PcText::FeListing), tk.type.label, tk.color.dim.with_alpha(alpha));
    }
    for (int i = 0; i < n; ++i) {
        const MmWheelItem& it = fr.items[size_t(i)];
        const float y0 = top + row_y(i) - scroll, y1 = y0 + sp.row_h;
        if (y1 < top - 4.0f || y0 > bottom + 4.0f) continue;
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y1};
        // As linhas somem perto das pontas cortadas da área.
        float a = alpha;
        if (y0 < top + sp.fade_px * 0.5f && scroll > 0.5f) a *= std::clamp((y1 - top) / sp.row_h, 0.0f, 1.0f);
        if (y1 > bottom - 2.0f) a *= std::clamp((bottom - y0) / sp.row_h, 0.0f, 1.0f);
        RowState rs;
        rs.alpha = a;
        rs.focused = i == focus_;
        rs.label_t = size_t(i) < lit_.size() ? lit_[size_t(i)].value() : (rs.focused ? 1.0f : 0.0f);
        rs.disabled = !row_enabled(i);
        std::wstring label = widen(it.name);
        float indent = 0.0f;
        if (view_ == View::Map) {
            // O número da missão (a ordem do jogo).
            wchar_t num[8];
            std::swprintf(num, 8, L"%02d", i + 1);
            const Color nc = rs.disabled ? tk.color.disabled : mix(tk.color.dim, tk.color.accent, rs.label_t);
            line_at(dl, row.x0 + sp.pad_l, row.cy(), num, tk.type.table_head, nc.with_alpha(a));
            indent = 46.0f;
        }
        if (view_ == View::CodenameMenu && i == kCnSave) {
            // Gravar o codinome: a linha de ação, em versalete.
            TextStyle st = tk.type.tab;
            st.size = 18.0f;
            const Color c = mix(tk.color.accent, tk.color.accent_hi, rs.label_t * 0.6f);
            line_at(dl, row.x0 + sp.pad_l, row.cy(), label, st, c.with_alpha(a));
        } else {
            draw_row_label(dl, f, row, label, rs, indent);
        }
        // À direita, o que o item é.
        RowState vs = rs;
        vs.disabled = false;
        switch (view_) {
            case View::Campaign:
            case View::Codenames: {
                PcText tag = PcText::FeTagCard;
                if (i == 0) tag = PcText::FeTagNew;
                else if (i == 1) tag = view_ == View::Campaign ? PcText::FeTagNoSave : PcText::FeTagDefault;
                const Color c = mix(tk.color.dim, tk.color.text_2, rs.label_t);
                line_at(dl, row.x1 - sp.pad_r, row.cy(), T(tag), tk.type.value, c.with_alpha(a), Align::Right);
                break;
            }
            case View::Difficulty:
                if (rs.disabled) {
                    draw_locked_value(dl, f, row, T(PcText::FeLocked), vs);
                } else {
                    // A medida da dificuldade: um, dois ou três losangos acesos.
                    const int lit = std::clamp(i + 1, 1, 3);
                    for (int p = 0; p < 3; ++p) {
                        const float px = row.x1 - sp.pad_r - 6.0f - float(2 - p) * 22.0f;
                        const Color pc = p < lit ? mix(tk.color.text_2, tk.color.accent, rs.label_t) : tk.color.track;
                        dl.diamond(Rect{px - 6.0f, row.cy() - 6.0f, px + 6.0f, row.cy() + 6.0f}, pc.with_alpha(a));
                    }
                }
                break;
            case View::Map:
                if (rs.disabled) {
                    draw_locked_value(dl, f, row, T(PcText::FeLocked), vs);
                } else if (it.driving && it.medal == 0) {
                    line_at(dl, row.x1 - sp.pad_r, row.cy(), T(PcText::FeDrivingTag), tk.type.value, tk.color.dim.with_alpha(a), Align::Right);
                } else if (it.medal > 0) {
                    const float w = text_width(f, widen(it.medal_label), tk.type.value);
                    const float mx = row.x1 - sp.pad_r - w - 20.0f;
                    dl.diamond(Rect{mx - 5.0f, row.cy() - 5.0f, mx + 5.0f, row.cy() + 5.0f},
                               (it.medal >= 3 ? tk.color.accent : tk.color.text_2).with_alpha(a));
                    line_at(dl, row.x1 - sp.pad_r, row.cy(), widen(it.medal_label), tk.type.value,
                            mix(tk.color.text_2, tk.color.text, rs.label_t).with_alpha(a), Align::Right);
                }
                break;
            case View::CodenameMenu:
                if (rs.disabled) {
                    draw_locked_value(dl, f, row, T(PcText::FeNotAvailable), vs);
                } else if (i != kCnSave) {
                    const float right = row.x1 - sp.pad_r;
                    const Color cc = mix(tk.color.text_2, tk.color.accent, rs.label_t).with_alpha(a);
                    dl.chevron(Rect{right - 8.0f, row.cy() - 7.0f, right, row.cy() + 7.0f}, 3, cc, 1.6f);
                }
                break;
            default: break;
        }
        draw_hairline(dl, f, row.x0, row.x1 - 8.0f, y1, a);
    }
    mark_.draw(dl, f, alpha, top - scroll);
    dl.pop_clip();
    const float view = bottom - top, totalh = row_y(n);
    if (totalh > view) draw_scroll(dl, f, sp.scroll_x + dx, top + 24.0f, bottom, view / totalh, scroll / (totalh - view), alpha);
}

Description FrontScreen::description() const {
    const MmFront& fr = snap_.front;
    Description d;
    d.strip[0] = L"NF_PC/FE";
    d.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    if (view_ == View::Name) {
        d.strip[1] = fr.cheat ? L"CODE" : L"NAME";
        d.title = fr.cheat ? widen(fr.title) : T(PcText::FeStepCodename);
        d.body = T(fr.cheat ? PcText::FeCodeHint : PcText::FeCodenameHint);
        d.value_label = T(fr.cheat ? PcText::FeCode : PcText::FeProfile);
        std::wstring e = widen(fr.entry);
        d.value = e.empty() ? L"—" : e;
        char n[32];
        std::snprintf(n, sizeof(n), "%d / %d", int(fr.entry.size()), fr.max_len);
        d.strip[2] = widen(n);
        return d;
    }
    if (focus_ < 0 || size_t(focus_) >= fr.items.size()) {
        d.title = widen(fr.title);
        d.body = fr.listing ? T(PcText::FeListing) : widen(fr.desc);
        return d;
    }
    const MmWheelItem& it = fr.items[size_t(focus_)];
    wchar_t num[16];
    std::swprintf(num, 16, L"ITEM.%02d", focus_ + 1);
    d.strip[1] = num;
    d.strip[2] = L"[" + to_upper(widen(it.name)) + L"]";
    d.title = widen(it.name);
    // O texto do item (o mesmo que o jogo põe no memo da página; o de bloqueado quando ele está bloqueado).
    d.body = it.text.empty() ? widen(fr.desc) : widen(it.text);
    if (view_ == View::Map) {
        d.value_label = T(PcText::FeBestMedal);
        d.value = it.medal > 0 ? widen(it.medal_label) : T(PcText::FeNoMedal);
        if (it.driving) d.notice = T(PcText::FeDrivingNote);
    }
    if (view_ == View::CodenameMenu && !row_enabled(focus_)) d.notice = T(PcText::FeNotAvailableToast);
    return d;
}

void FrontScreen::draw_name(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmFront& fr = snap_.front;
    draw_left(dl, f, alpha, -dx);
    // O nome: oito casas, as letras do jogo (codename_entry); o sugerido ("BOND") apagado e marcado, porque a primeira
    // tecla o troca; o lugar da próxima letra com o sublinhado de ouro e o cursor.
    const int n = std::max(1, fr.max_len);
    const float slot = 64.0f, gap = 14.0f;
    const float total = float(n) * slot + float(n - 1) * gap;
    const float x0 = (sp.mid_x0 + sp.mid_x1) * 0.5f - total * 0.5f + dx;
    const float cy = 286.0f;
    const std::wstring e = widen(fr.entry);
    const int len = int(e.size());
    if (fr.suggested && len > 0) {
        const float sx1 = x0 + float(len) * (slot + gap) - gap;
        dl.fill(Rect{x0 - 6.0f, cy - 44.0f, sx1 + 6.0f, cy + 34.0f}, tk.color.accent.with_alpha(0.13f * alpha), 2.0f);
    }
    for (int i = 0; i < n; ++i) {
        const float sx = x0 + float(i) * (slot + gap);
        const bool here = i == std::min(len, n - 1) && (len < n);
        const Color ul = here ? tk.color.accent : tk.color.hairline_2;
        dl.fill(Rect{sx, cy + 40.0f, sx + slot, cy + 42.0f}, ul.with_alpha(alpha));
        if (here) {
            dl.glow(Rect{sx + slot * 0.5f - 46.0f, cy + 10.0f, sx + slot * 0.5f + 46.0f, cy + 52.0f},
                    tk.color.ember.with_alpha(0.30f * alpha), tk.color.ember_deep.with_alpha(0.0f), cy + 41.0f);
            const float blink = std::fmod(f.time, 1.06f) < 0.62f ? 1.0f : 0.0f;
            if (!fr.suggested) dl.fill(Rect{sx + slot * 0.5f - 1.0f, cy - 30.0f, sx + slot * 0.5f + 1.0f, cy + 26.0f},
                                       tk.color.accent.with_alpha(alpha * blink));
        }
        if (i < len) {
            const Color c = fr.suggested ? tk.color.text_2 : tk.color.text;
            line_at(dl, sx + slot * 0.5f, cy, std::wstring(1, e[size_t(i)]), tk.type.entry, c.with_alpha(alpha), Align::Centre);
        }
    }
    // A grade de teclas (as teclas do jogo).
    for (size_t i = 0; i < key_rects_.size() && i < key_order_.size(); ++i) {
        const MmKey& k = fr.keys[size_t(key_order_[i])];
        const Rect r = key_rects_[i].offset(dx, 0.0f);
        const bool on = int(i) == key_focus_;
        const bool hover = int(i) == key_hover_ && !on;
        dl.fill(r, rgb(0xf5e6d0, (on ? 0.09f : 0.04f) * alpha), 2.0f);
        dl.stroke(r, (on ? tk.color.accent.with_alpha(0.85f) : tk.color.hairline_2).with_alpha(alpha), 1.0f, 2.0f);
        std::wstring label;
        TextStyle st = tk.type.key;
        Color c = on ? tk.color.accent : (hover ? tk.color.text : tk.color.text_2);
        if (k.tag == kMmFeKeySpace) label = T(PcText::FeKeySpace);
        else if (k.tag == kMmFeKeyDel) label = T(PcText::FeKeyDel);
        else if (k.tag == kMmFeKeyEnd) label = T(PcText::FeKeyEnd);
        else label = widen(k.text);
        if (k.tag >= kMmFeKeyDel) {
            st = tk.type.tab;
            st.size = 16.0f;
            if (k.tag == kMmFeKeyEnd && !on) c = mix(tk.color.accent, tk.color.text_2, 0.25f);
        }
        line_at(dl, r.cx(), r.cy(), label, st, c.with_alpha(alpha), Align::Centre);
    }
    key_mark_.draw(dl, f, alpha);
    draw_description(dl, f, description(), alpha, tk.space.slide * (1.0f - alpha));
}

// Os créditos: as linhas que o P_CREDITS_Handler pôs nos rótulos, na posição dele (a tela do PS2 levada à tela virtual: a
// altura de 448 a 1080, o meio no meio), com o tipo do kit pela fonte do jogo: 1 os títulos grandes (as empresas), 3 os
// cargos, 2 os nomes e o texto legal. O ritmo (uma linha a cada 14 ticks, 2 pixels por tick) e o fim são os do jogo.
void FrontScreen::draw_credits(DrawList& dl, const Frame& f, float alpha) const {
    const Tokens& tk = f.tk;
    const MmCredits& cr = snap_.front.credits;
    const float k = kCanvasH / 448.0f;
    const float top = 118.0f, bottom = 968.0f, edge = 150.0f;
    line_at(dl, tk.space.left_x + 3.0f, 62.0f, L"Nightfire / " + T(PcText::FeCredits), tk.type.header, tk.color.dim.with_alpha(alpha));
    TextStyle company = tk.type.hero;
    company.size = 34.0f;
    company.tracking = 0.22f;
    TextStyle role = tk.type.section;
    role.size = 15.0f;
    TextStyle name = tk.type.label;
    dl.push_clip(Rect{f.cv.left, top, f.cv.right, bottom});
    for (const MmCreditLine& l : cr.lines) {
        const float cy = (float(l.y) + 7.0f) * k;
        if (cy < top - 40.0f || cy > bottom + 40.0f) continue;
        // As pontas da área: a linha aparece embaixo e some em cima.
        const float a = alpha * std::clamp((cy - top) / edge, 0.0f, 1.0f) * std::clamp((bottom - cy) / edge, 0.0f, 1.0f);
        if (a <= 0.001f) continue;
        const float cx = kCanvasW * 0.5f + (float(l.x) + float(l.w) * 0.5f - 256.0f) * k;
        const float width = std::max(200.0f, float(l.w) * k);
        const std::wstring text = widen(l.text);
        if (l.font == 1) {
            const TextStyle st = fit_style(f, text, company, width, 22.0f);
            line_at(dl, cx, cy, text, st, tk.color.text.with_alpha(a), Align::Centre);
            dl.fill(Rect{cx - 32.0f, cy + 30.0f, cx + 32.0f, cy + 32.0f}, tk.color.accent.with_alpha(a));
        } else if (l.font == 3) {
            line_at(dl, cx, cy, text, fit_style(f, text, role, width, 12.0f), tk.color.text_2.with_alpha(0.9f * a), Align::Centre);
        } else {
            line_at(dl, cx, cy, text, fit_style(f, text, name, width, 15.0f), tk.color.text.with_alpha(a), Align::Centre);
        }
    }
    dl.pop_clip();
    // O escurecer do fim (o do jogo, o rótulo de tela cheia).
    if (cr.fade > 0.001f) dl.fill(Rect{f.cv.left, f.cv.top, f.cv.right, f.cv.bottom}, tk.color.ground.with_alpha(cr.fade * alpha));
}

void FrontScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    const float k = appear_.value();
    const float c = content_.value();
    const float a = k * (0.25f + 0.75f * c);
    const float dx = tk.space.slide * (1.0f - k) + 12.0f * (1.0f - c);
    if (tabs_on_) tabs_.draw(dl, f, k);
    if (opts_ != OptsFrom::None && options_.active()) {
        options_.build(dl, cv, m);
    } else {
        switch (view_) {
            case View::Title: draw_title(dl, f, k * c); break;
            case View::Main: draw_main(dl, f, a, dx); break;
            case View::Name: draw_name(dl, f, a, dx); break;
            case View::Options: break;  // a página de opções do jogo fechada, esperando o jogo voltar
            case View::Dossier: dossier_.build(dl, f, a, dx); break;
            case View::Credits: draw_credits(dl, f, k * c); break;
            default:
                draw_left(dl, f, a, -dx);
                draw_rows(dl, f, a, dx);
                draw_description(dl, f, description(), a, tk.space.slide * (1.0f - a));
                break;
        }
        if (view_ != View::Title) bar_.draw(dl, f, k);
    }
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

std::string FrontScreen::describe() const {
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "frontend_moderno ativo %d fechando %d vista %s pagina %08x foco %d linhas %d roda %d dialogo %d foco_dialogo %d "
                  "opcoes %d desfoque %.2f nome \"%s\" esperando %d mouse %d filme %d",
                  active_ ? 1 : 0, closing_ ? 1 : 0, view_name(view_), snap_.page, focus(), row_count(), snap_.front.pos,
                  dialog_.open ? 1 : 0, dialog_.focus, int(opts_), double(blur_.value()), snap_.front.entry.c_str(),
                  waiting_ ? 1 : 0, nav_.mouse_active ? 1 : 0, movie_ ? 1 : 0);
    std::string out = buf;
    if (view_ == View::Dossier) out += " | " + dossier_.describe();
    if (view_ == View::Credits) {
        char c[96];
        std::snprintf(c, sizeof(c), " | creditos linhas %zu escurecer %.2f", snap_.front.credits.lines.size(),
                      double(snap_.front.credits.fade));
        out += c;
    }
    return out;
}

}  // namespace nf::ui
