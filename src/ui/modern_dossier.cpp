// O dossiê no estilo Modern. Ver modern_dossier.h e docs/design.md.

#include "ui/modern_dossier.h"

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

// O comando "voltar" da barra de baixo (o kActBack do FrontScreen, que trata o clique).
constexpr int kPromptBack = 2;
// Os nomes das abas quando a tabela do jogo não tem (testes).
const wchar_t* const kTabNames[4] = {L"Records", L"Rewards", L"Gadgets", L"Weapons"};
// Um pedido de página que não mudou a página em tanto tempo é mandado de novo (a página ainda não recebia entrada).
constexpr float kRetry = 1.5f;

// O nível de melhoria em losangos (um por nível).
void upgrade_marks(DrawList& dl, const Tokens& tk, float right, float cy, uint32_t level, float t, float a) {
    for (uint32_t k = 0; k < level && k < 4; ++k) {
        const float px = right - 6.0f - float(k) * 20.0f;
        dl.diamond(Rect{px - 5.0f, cy - 5.0f, px + 5.0f, cy + 5.0f}, mix(tk.color.text_2, tk.color.accent, 0.6f + 0.4f * t).with_alpha(a));
    }
}

}  // namespace

DossierView::DossierView(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

// ---------------------------------------------------------------- entrar e seguir o jogo

void DossierView::enter(const MmSnapshot& s, const Frame& f) {
    last_cv_ = f.cv;
    snap_ = s;
    const int sec = s.front.dossier.section;
    tab_ = sec >= 0 ? sec : 0;
    target_ = -1;
    sent_ = false;
    tabs_ = TabBar{};
    mark_ = FocusMark{};
    hover_ = -1;
    scroll_.snap(0.0f);
    scroll_target_ = 0.0f;
    lit_.clear();
    pic_ = pic_prev_ = 0;
    pic_t_.snap(1.0f);
    // Na página da roda o conteúdo aparece quando a seção chega.
    content_.snap(sec >= 0 ? 1.0f : 0.0f);
    if (sec >= 0) sync(s, true);
}

bool DossierView::live() const { return snap_.front.dossier.section == tab_ && tab_ >= 0 && target_ < 0; }

void DossierView::sync(const MmSnapshot& s, bool snap_focus) {
    const int n = int(s.front.items.size());
    focus_ = std::clamp(s.front.pos, 0, std::max(0, n - 1));
    lit_.assign(size_t(n), Tween{});
    if (focus_ < n) lit_[size_t(focus_)].snap(1.0f);
    scroll_target_ = 0.0f;
    ensure_visible(focus_);
    if (snap_focus) scroll_.snap(scroll_target_);
    mark_ = FocusMark{};
    place_mark(false);
}

void DossierView::go_tab(int t) {
    if (t < 0 || t >= kTabs) return;
    if (target_ == kLeave) return;
    const int now = target_ >= 0 ? target_ : tab_;
    if (t == now) return;
    target_ = t == snap_.front.dossier.section && snap_.front.dossier.section == tab_ ? -1 : t;
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    tabs_.set_active(t, f, true);
}

void DossierView::leave() { target_ = kLeave; }

// Um pedido por vez: na página da roda, a seção pedida (o passo da roda e o X) ou o voltar ao sair; numa seção que não
// é a pedida (ou saindo), o voltar do jogo. Cada pedido espera a página do jogo mudar.
void DossierView::drive(const MmSnapshot& s) {
    if (!s.accepts || MenuModern_Queued(MmAction::Press)) return;
    if (sent_) {
        if (s.page == sent_page_ && time_ - sent_time_ < kRetry) return;
        sent_ = false;
    }
    const int sec = s.front.dossier.section;
    const auto sent = [&] {
        sent_ = true;
        sent_page_ = s.page;
        sent_time_ = time_;
    };
    if (sec < 0) {
        if (s.page != kMmFeDossier) return;  // a caixa do jogo por cima, por exemplo
        if (target_ == kLeave) {
            MenuModern_Press(kMmBack);
            sent();
            return;
        }
        const int t = target_ >= 0 ? target_ : tab_;
        if (s.front.dossier.section_pos != t) MenuModern_Step(kMmDsWheel, t);
        MenuModern_Press(kMmSelect);
        sent();
        return;
    }
    if (target_ == kLeave || (target_ >= 0 && target_ != sec)) {
        MenuModern_Press(kMmBack);
        sent();
    }
}

void DossierView::frame(float dt, NavState& nav, const UiInput& in, const MmSnapshot& s, const Frame& f) {
    last_cv_ = f.cv;
    time_ += dt;
    content_.update(dt);
    mark_.update(dt);
    scroll_.update(dt);
    tabs_.update(dt);
    pic_t_.update(dt);
    for (size_t i = 0; i < lit_.size(); ++i) {
        lit_[i].update(dt);
        lit_[i].set(int(i) == focus_ ? 1.0f : 0.0f, tk_->motion.focus, tk_->motion.ease_out);
    }
    const int before = snap_.front.dossier.section;
    const int sec = s.front.dossier.section;
    // A seção chegou: é a aba (a pedida, ou a do jogo se nada foi pedido).
    if (sec >= 0 && sec != before && (target_ < 0 || target_ == sec)) {
        tab_ = sec;
        target_ = -1;
        snap_ = s;
        sync(s, true);
        content_.snap(0.0f);
        content_.set(1.0f, tk_->motion.content * 1.5f, tk_->motion.ease_out);
    }
    // O que se desenha é o último estado da seção da aba (a caminho de outra, o conteúdo velho some).
    if (sec >= 0 && sec == tab_ && target_ < 0) {
        const size_t items_before = snap_.front.items.size();
        snap_ = s;
        if (lit_.size() != snap_.front.items.size()) lit_.resize(snap_.front.items.size());
        // O rádio ou a roda do jogo mudou sozinho: a tela vai junto (os passos pedidos não seguram a navegação).
        if (!MenuModern_Queued(MmAction::Step) && s.front.pos != focus_ && s.front.pos >= 0 && s.front.pos < row_count()) {
            focus_ = s.front.pos;
            ensure_visible(focus_);
            place_mark(items_before == snap_.front.items.size());
        }
        if (!mark_.placed) place_mark(false);
    } else {
        snap_.page = s.page;
        snap_.ready = s.ready;
        snap_.accepts = s.accepts;
        snap_.front.dossier.section = sec;
        snap_.front.dossier.section_pos = s.front.dossier.section_pos;
        if (!s.front.dossier.sections.empty()) snap_.front.dossier.sections = s.front.dossier.sections;
    }
    content_.set(live() ? 1.0f : 0.0f, tk_->motion.content * (live() ? 1.5f : 1.0f), tk_->motion.ease_out);
    // A imagem da esquerda.
    const uint32_t p = picture();
    if (p != pic_) {
        pic_prev_ = pic_;
        pic_ = p;
        pic_t_.snap(0.0f);
        pic_t_.set(1.0f, tk_->motion.content * 1.6f, tk_->motion.ease_out);
    }
    // A barra de cima.
    tabs_.labels.clear();
    for (int i = 0; i < kTabs; ++i) {
        const std::vector<MmWheelItem>& secs = snap_.front.dossier.sections;
        tabs_.labels.push_back(size_t(i) < secs.size() && !secs[size_t(i)].name.empty() ? widen(secs[size_t(i)].name) : kTabNames[i]);
    }
    const bool pad = in.device == 1;
    const std::wstring kp = bind_key("menu_tab_prev"), kn = bind_key("menu_tab_next");
    tabs_.key_prev = kp.empty() ? L"Q" : kp;
    tabs_.key_next = kn.empty() ? L"E" : kn;
    tabs_.glyph_prev = pad ? bind_glyph("menu_tab_prev", "xbox_lb") : std::string();
    tabs_.glyph_next = pad ? bind_glyph("menu_tab_next", "xbox_rb") : std::string();
    tabs_.corner_left = L"NF.PC / DOS";
    tabs_.corner_right = L"SLUS-20579";
    const bool first = !tabs_.laid_out;
    tabs_.layout(f);
    const int active = target_ >= 0 && target_ < kTabs ? target_ : tab_;
    if (first) tabs_.set_active(active, f, false);
    else if (tabs_.active != active) tabs_.set_active(active, f, true);
    // A entrada (uma aba pedida ou o voltar viram o pedido de página no mesmo quadro).
    if (s.ready) {
        for (int i = 0; i < kNavCount; ++i) {
            if (nav.fire(Nav(i))) act(Nav(i));
        }
        mouse(nav);
    }
    drive(s);
}

// ---------------------------------------------------------------- a lista

Rect DossierView::row_rect(int i) const {
    const auto& sp = tk_->space;
    const float y = sp.front_top + row_y(i) - scroll_.value();
    return Rect{sp.mid_x0, y, sp.mid_x1, y + sp.row_h};
}

Rect DossierView::tab_rect(int i) const { return i >= 0 && size_t(i) < tabs_.tab_rects.size() ? tabs_.tab_rects[size_t(i)] : Rect{}; }

void DossierView::ensure_visible(int row) {
    const auto& sp = tk_->space;
    const float view = sp.list_bottom - sp.front_top;
    const float y0 = row_y(row), y1 = y0 + sp.row_h;
    float t = scroll_target_;
    if (y0 - 24.0f < t) t = y0 - 24.0f;
    if (y1 + 24.0f > t + view) t = y1 + 24.0f - view;
    t = std::clamp(t, 0.0f, std::max(0.0f, row_y(row_count()) - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void DossierView::place_mark(bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const auto& sp = tk_->space;
    if (row_count() == 0) {
        mark_.show(false, f);
        return;
    }
    mark_.move_to(sp.mid_x0, sp.mid_x1 - 8.0f, row_y(focus_) + sp.row_h - 1.0f, f, animate);
    mark_.show(true, f);
}

void DossierView::move_focus(int to) {
    const int n = row_count();
    if (n == 0 || !live()) return;
    to = std::clamp(to, 0, n - 1);
    if (to == focus_) return;
    focus_ = to;
    ensure_visible(focus_);
    place_mark(true);
    // O rádio das missões ou a roda dos itens do jogo acompanha, um passo por vez como o direcional (o 0x49 refaz a
    // página: os recordes, as recompensas, o texto do item).
    MenuModern_Step(snap_.front.wheel, focus_);
}

void DossierView::act(Nav c) {
    switch (c) {
        case Nav::Up: move_focus(focus_ - 1); break;
        case Nav::Down: move_focus(focus_ + 1); break;
        // Nos recordes e nas recompensas o direcional do jogo troca a missão para os lados ("~H Change").
        case Nav::Left:
            if (mission_tab()) move_focus(focus_ - 1);
            break;
        case Nav::Right:
            if (mission_tab()) move_focus(focus_ + 1);
            break;
        case Nav::TabPrev: go_tab((target_ >= 0 && target_ < kTabs ? target_ : tab_) - 1); break;
        case Nav::TabNext: go_tab((target_ >= 0 && target_ < kTabs ? target_ : tab_) + 1); break;
        case Nav::Back: leave(); break;
        default: break;
    }
}

void DossierView::mouse(NavState& nav) {
    if (!nav.mouse_active || !nav.inside) return;
    const float x = nav.mx, y = nav.my;
    if (nav.back) {
        leave();
        return;
    }
    if (nav.pressed) {
        const int t = tabs_.hit(x, y);
        const int now = target_ >= 0 && target_ < kTabs ? target_ : tab_;
        if (t >= 0) go_tab(t);
        if (t == -2) go_tab(now - 1);
        if (t == -3) go_tab(now + 1);
        if (t != -1) return;
    }
    if (!live()) return;
    const auto& sp = tk_->space;
    if (nav.wheel != 0.0f && x >= sp.mid_x0 - 40.0f && x < sp.mid_x1 + 40.0f) {
        const float view = sp.list_bottom - sp.front_top;
        scroll_target_ = std::clamp(scroll_target_ - nav.wheel * sp.row_h, 0.0f, std::max(0.0f, row_y(row_count()) - view));
        scroll_.set(scroll_target_, tk_->motion.scroll, tk_->motion.ease_out);
    }
    int h = -1;
    for (int i = 0; i < row_count(); ++i) {
        if (row_rect(i).contains(x, y) && y >= sp.front_top && y < sp.list_bottom) h = i;
    }
    hover_ = h;
    if (h >= 0 && (nav.moved || nav.pressed) && h != focus_) {
        focus_ = h;
        place_mark(true);
        MenuModern_Step(snap_.front.wheel, focus_);
    }
}

// ---------------------------------------------------------------- desenho

uint32_t DossierView::picture() const {
    const std::vector<MmWheelItem>& items = snap_.front.items;
    if (focus_ < 0 || size_t(focus_) >= items.size()) return 0;
    return items[size_t(focus_)].image;
}

Rect DossierView::picture_rect() const {
    const auto& sp = tk_->space;
    const float y0 = 506.0f;
    return Rect{sp.left_x + 4.0f, y0, sp.left_x + 4.0f + sp.picture, y0 + sp.picture};
}

Rect DossierView::medal_rect(float dx) const {
    // A medalha dos recordes (o 007 do jogo, 2 por 1 na TV), embaixo do painel da direita.
    const auto& sp = tk_->space;
    return Rect{sp.desc_x0 + dx, 650.0f, sp.desc_x0 + 220.0f + dx, 760.0f};
}

Rect DossierView::reward_rect(int i, float dx) const {
    const auto& sp = tk_->space;
    const float y0 = 360.0f + float(i) * 122.0f;
    return Rect{sp.desc_x0 + dx, y0, sp.desc_x0 + 92.0f + dx, y0 + 92.0f};
}

DossierView::Offsets DossierView::offsets(float dx) const {
    // As colunas entram deslizando (ao abrir a tela e ao chegar uma aba), como as do menu principal.
    const float k = 12.0f * (1.0f - content_.value());
    return Offsets{-(dx + k), dx + k, tk_->space.slide * (1.0f - content_.value()) + dx};
}

void DossierView::images(ImageList& out, float alpha, float dx) const {
    const float a = alpha * content_.value();
    if (a <= 0.001f) return;
    const Offsets o = offsets(dx);
    // A imagem da missão ou do item (redonda, o anel escuro por cima em build): a de agora entrando, a anterior saindo.
    const Rect r = picture_rect().offset(o.left, 0.0f);
    const float t = pic_t_.value();
    const auto add = [&](uint32_t hash, const Rect& rr, float aa) {
        if (hash == 0 || aa <= 0.001f) return;
        ImageCmd p;
        p.hash = hash;
        p.r = rr;
        p.whole = true;
        p.alpha = aa;
        out.push_back(p);
    };
    add(pic_prev_, r, a * (1.0f - t));
    add(pic_, r, a * t);
    const MmDossier& d = snap_.front.dossier;
    if (tab_ == 0 && d.medal_image != 0) add(d.medal_image, medal_rect(o.right), a);
    if (tab_ == 1) {
        for (int i = 0; i < 4; ++i) {
            if (d.rewards[i].shown) add(d.rewards[i].image, reward_rect(i, o.right), a);
        }
    }
}

void DossierView::draw_left(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmDossier& d = snap_.front.dossier;
    const float x = sp.left_x + dx;
    line_at(dl, x + 3.0f, sp.header_y, T(PcText::DsHeader) + L" /", tk.type.header, tk.color.dim.with_alpha(alpha));
    // O título da página do jogo ("Records"), grande, com o traço de ouro, e a descrição da seção (ds_options).
    std::wstring title = widen(snap_.front.title);
    if (title.empty()) title = tab_ < int(d.sections.size()) ? widen(d.sections[size_t(tab_)].name) : kTabNames[tab_];
    const TextStyle ts = fit_style(f, title, tk.type.page_title, 400.0f, 30.0f);
    float y = 236.0f;
    line_at(dl, x - 3.0f, y, title, ts, tk.color.text.with_alpha(alpha));
    y += ts.size * 1.12f + 4.0f;
    dl.fill(Rect{x, y, x + 64.0f, y + 2.0f}, tk.color.accent.with_alpha(alpha));
    dl.line(x + 64.0f, y + 1.0f, x + 420.0f, y + 1.0f, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    y += 44.0f;
    if (size_t(tab_) < d.sections.size()) {
        for (const std::wstring& l : wrap_lines(f, widen(d.sections[size_t(tab_)].text), tk.type.body, 410.0f)) {
            line_at(dl, x, y, l, tk.type.body, tk.color.text_2.with_alpha(alpha * 0.9f));
            y += tk.type.body.size * tk.type.body.line;
        }
    }
    // O anel escuro em volta da imagem redonda (a substituta HD vem quadrada) e a moldura fina.
    const Rect r = picture_rect().offset(dx, 0.0f);
    const float rad = r.w() * 0.5f;
    const float ring = rad * 0.48f;
    const float mid = rad * 0.985f + ring * 0.5f;
    dl.ellipse(Rect{r.cx() - mid, r.cy() - mid, r.cx() + mid, r.cy() + mid}, tk.color.ground.with_alpha(alpha), ring);
    dl.ellipse(Rect{r.x0 - 10.0f, r.y0 - 10.0f, r.x1 + 10.0f, r.y1 + 10.0f}, tk.color.hairline_2.with_alpha(alpha), 1.0f);
}

void DossierView::draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const std::vector<MmWheelItem>& items = snap_.front.items;
    const MmDossier& d = snap_.front.dossier;
    const float top = sp.front_top, bottom = sp.list_bottom;
    const float scroll = scroll_.value();
    const int n = row_count();
    dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, top - 2.0f, sp.mid_x1 + 20.0f + dx, bottom});
    for (int i = 0; i < n; ++i) {
        const MmWheelItem& it = items[size_t(i)];
        const float y0 = top + row_y(i) - scroll, y1 = y0 + sp.row_h;
        if (y1 < top - 4.0f || y0 > bottom + 4.0f) continue;
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y1};
        float a = alpha;
        if (y0 < top + sp.fade_px * 0.5f && scroll > 0.5f) a *= std::clamp((y1 - top) / sp.row_h, 0.0f, 1.0f);
        if (y1 > bottom - 2.0f) a *= std::clamp((bottom - y0) / sp.row_h, 0.0f, 1.0f);
        RowState rs;
        rs.alpha = a;
        rs.focused = i == focus_;
        rs.label_t = size_t(i) < lit_.size() ? lit_[size_t(i)].value() : (rs.focused ? 1.0f : 0.0f);
        rs.disabled = !it.enabled;
        float indent = 0.0f;
        if (mission_tab()) {
            // O número da missão (a ordem do jogo).
            wchar_t num[8];
            std::swprintf(num, 8, L"%02d", i + 1);
            line_at(dl, row.x0 + sp.pad_l, row.cy(), num, tk.type.table_head, mix(tk.color.dim, tk.color.accent, rs.label_t).with_alpha(a));
            indent = 46.0f;
        }
        draw_row_label(dl, f, row, widen(it.name), rs, indent);
        const float right = row.x1 - sp.pad_r;
        if (tab_ == 0) {
            // A melhor medalha (cada uma só com as de baixo, a regra do dossiê).
            if (it.medal > 0) {
                const std::wstring m = widen(it.medal_label);
                const float w = text_width(f, m, tk.type.value);
                dl.diamond(Rect{right - w - 25.0f, row.cy() - 5.0f, right - w - 15.0f, row.cy() + 5.0f},
                           (it.medal >= 3 ? tk.color.accent : tk.color.text_2).with_alpha(a));
                line_at(dl, right, row.cy(), m, tk.type.value, mix(tk.color.text_2, tk.color.text, rs.label_t).with_alpha(a), Align::Right);
            } else {
                line_at(dl, right, row.cy(), L"—", tk.type.value, tk.color.disabled.with_alpha(a), Align::Right);
            }
        } else if (tab_ == 1) {
            // As recompensas ganhas: uma por medalha (a recompensa i só com a medalha i e as de baixo).
            const int got = std::clamp(it.medal, 0, 4);
            for (int k = 0; k < 4; ++k) {
                const float px = right - 6.0f - float(3 - k) * 20.0f;
                const Color pc = k < got ? mix(tk.color.text_2, tk.color.accent, 0.5f + 0.5f * rs.label_t) : tk.color.track;
                dl.diamond(Rect{px - 5.0f, row.cy() - 5.0f, px + 5.0f, row.cy() + 5.0f}, pc.with_alpha(a));
            }
            wchar_t cnt[16];
            std::swprintf(cnt, 16, L"%d / 4", got);
            line_at(dl, right - 92.0f, row.cy(), cnt, tk.type.value, mix(tk.color.dim, tk.color.text_2, rs.label_t).with_alpha(a), Align::Right);
        } else if (size_t(i) < d.upgrade.size() && d.upgrade[size_t(i)] > 0) {
            // A melhoria ganha (Menu_GetObjectUpgradeLevel do grupo do item): a palavra e um losango por nível.
            const uint32_t level = d.upgrade[size_t(i)];
            upgrade_marks(dl, tk, right, row.cy(), level, rs.label_t, a);
            line_at(dl, right - float(std::min<uint32_t>(level, 4)) * 20.0f - 10.0f, row.cy(), T(PcText::DsUpgrade), tk.type.value,
                    mix(tk.color.dim, tk.color.text_2, rs.label_t).with_alpha(a), Align::Right);
        }
        draw_hairline(dl, f, row.x0, row.x1 - 8.0f, y1, a);
    }
    mark_.draw(dl, f, alpha, top - scroll);
    dl.pop_clip();
    const float view = bottom - top, totalh = row_y(n);
    if (totalh > view) draw_scroll(dl, f, sp.scroll_x + dx, top + 24.0f, bottom, view / totalh, scroll / (totalh - view), alpha);
}

void DossierView::draw_records(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmDossier& d = snap_.front.dossier;
    const std::vector<MmWheelItem>& items = snap_.front.items;
    const MmWheelItem* it = focus_ >= 0 && size_t(focus_) < items.size() ? &items[size_t(focus_)] : nullptr;
    Description desc;
    desc.strip[0] = L"NF_PC/DOS";
    wchar_t num[16];
    std::swprintf(num, 16, L"MSN.%02d", focus_ + 1);
    desc.strip[1] = num;
    desc.strip[2] = it != nullptr ? L"[" + to_upper(widen(it->name)) + L"]" : std::wstring();
    desc.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    desc.title = it != nullptr ? widen(it->name) : std::wstring();
    draw_description(dl, f, desc, alpha, dx);
    // Os recordes do jogo: o nome da linha (o rótulo da página) e o valor que o C_RBDSRECORDS_Handler escreveu.
    const float x0 = sp.desc_x0 + dx, x1 = sp.desc_x1 + dx;
    float y = 282.0f;
    draw_hairline(dl, f, x0, x1, y, alpha);
    const auto stat = [&](const std::string& label, const std::string& value, bool big) {
        y += big ? 50.0f : 42.0f;
        line_at(dl, x0, y, widen(label), tk.type.section, tk.color.text_2.with_alpha(alpha));
        TextStyle vs = tk.type.stat;
        vs.size = big ? 52.0f : 36.0f;
        line_at(dl, x1, y, value.empty() ? std::wstring(L"0") : widen(value), vs, tk.color.text.with_alpha(alpha), Align::Right);
        y += big ? 50.0f : 42.0f;
        draw_hairline(dl, f, x0, x1, y, alpha);
    };
    stat(d.score_label, d.score, true);
    stat(d.moments_label, d.moments, false);
    stat(d.upgrades_label, d.upgrades, false);
    // A melhor medalha: o nome do jogo e a imagem dele (em images(), no retângulo da medalha).
    y += 40.0f;
    std::wstring head = T(PcText::FeBestMedal);
    while (!head.empty() && (head.back() == L':' || head.back() == L' ')) head.pop_back();
    line_at(dl, x0, y, head, tk.type.section, tk.color.dim.with_alpha(alpha));
    const Rect m = medal_rect(dx);
    if (d.medal.empty()) {
        line_at(dl, x0, m.cy(), T(PcText::DsNoMedal), tk.type.meta, tk.color.dim.with_alpha(alpha));
    } else {
        TextStyle ms = tk.type.meta;
        ms.weight = 600;
        ms.caps = true;
        ms.tracking = 0.08f;
        line_at(dl, m.x1 + 24.0f, m.cy(), widen(d.medal), fit_style(f, widen(d.medal), ms, x1 - m.x1 - 24.0f, 14.0f),
                tk.color.accent.with_alpha(alpha));
    }
}

void DossierView::draw_rewards(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    const auto& sp = tk.space;
    const MmDossier& d = snap_.front.dossier;
    const std::vector<MmWheelItem>& items = snap_.front.items;
    const MmWheelItem* it = focus_ >= 0 && size_t(focus_) < items.size() ? &items[size_t(focus_)] : nullptr;
    int got = 0;
    for (const MmReward& r : d.rewards) got += r.shown ? 1 : 0;
    Description desc;
    desc.strip[0] = L"NF_PC/DOS";
    wchar_t num[16];
    std::swprintf(num, 16, L"MSN.%02d", focus_ + 1);
    desc.strip[1] = num;
    std::swprintf(num, 16, L"%d/4", got);
    desc.strip[2] = num;
    desc.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    desc.title = it != nullptr ? widen(it->name) : std::wstring();
    draw_description(dl, f, desc, alpha, dx);
    const float x0 = sp.desc_x0 + dx, x1 = sp.desc_x1 + dx;
    line_at(dl, x0, 300.0f, T(PcText::DsRewards), tk.type.section, tk.color.text_2.with_alpha(alpha));
    line_at(dl, x1, 300.0f, std::to_wstring(got) + L" / 4", tk.type.meta, tk.color.text.with_alpha(alpha), Align::Right);
    draw_hairline(dl, f, x0, x1, 330.0f, alpha);
    // As quatro recompensas: a imagem e o texto do jogo, ou o cadeado e a medalha que falta.
    for (int i = 0; i < 4; ++i) {
        const MmReward& r = d.rewards[i];
        const Rect im = reward_rect(i, dx);
        const float tx = im.x1 + 20.0f;
        if (r.shown) {
            dl.stroke(Rect{im.x0 - 1.0f, im.y0 - 1.0f, im.x1 + 1.0f, im.y1 + 1.0f}, tk.color.hairline_2.with_alpha(alpha), 1.0f);
            const std::vector<std::wstring> lines = wrap_lines(f, widen(r.text), tk.type.label, x1 - tx);
            const float lh = tk.type.label.size * 1.25f;
            float ly = im.cy() - lh * float(std::max<size_t>(1, lines.size()) - 1) * 0.5f;
            for (const std::wstring& l : lines) {
                line_at(dl, tx, ly, l, tk.type.label, tk.color.text.with_alpha(alpha));
                ly += lh;
            }
        } else {
            dl.fill(im, rgb(0xf5e6d0, 0.03f * alpha));
            dl.stroke(im, tk.color.hairline.with_alpha(alpha), 1.0f);
            dl.padlock(Rect{im.cx() - 9.0f, im.cy() - 12.0f, im.cx() + 9.0f, im.cy() + 10.0f}, tk.color.disabled.with_alpha(alpha));
            line_at(dl, tx, im.cy() - 14.0f, T(PcText::FeLocked), tk.type.label, tk.color.disabled.with_alpha(alpha));
            line_at(dl, tx, im.cy() + 18.0f, T(PcText::DsRequires) + L" " + widen(r.medal), tk.type.meta, tk.color.dim.with_alpha(alpha));
        }
        if (i < 3) draw_hairline(dl, f, x0, x1, im.y1 + 15.0f, alpha * 0.7f);
    }
}

void DossierView::draw_item(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const MmDossier& d = snap_.front.dossier;
    const std::vector<MmWheelItem>& items = snap_.front.items;
    const MmWheelItem* it = focus_ >= 0 && size_t(focus_) < items.size() ? &items[size_t(focus_)] : nullptr;
    Description desc;
    desc.strip[0] = L"NF_PC/DOS";
    wchar_t num[16];
    std::swprintf(num, 16, tab_ == 2 ? L"GDT.%02d" : L"WPN.%02d", focus_ + 1);
    desc.strip[1] = num;
    desc.strip[2] = it != nullptr ? L"[" + to_upper(widen(it->name)) + L"]" : std::wstring();
    desc.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    if (it != nullptr) {
        desc.title = widen(it->name);
        // O texto do jogo para o item da roda (o memo, com a linha da melhoria embaixo, em ouro); enquanto a roda do
        // jogo ainda anda até o item, o texto da tabela.
        std::wstring text = snap_.front.pos == focus_ && !d.memo.empty() ? widen(d.memo) : widen(it->text);
        const size_t nl = text.find(L'\n');
        if (nl != std::wstring::npos) {
            desc.notice = text.substr(nl + 1);
            text = text.substr(0, nl);
        }
        desc.body = text;
        const uint32_t level = size_t(focus_) < d.upgrade.size() ? d.upgrade[size_t(focus_)] : 0;
        desc.value_label = T(PcText::DsUpgradeLevel);
        desc.value = level > 0 ? std::to_wstring(level) : T(PcText::FeNoMedal);
    }
    draw_description(dl, f, desc, alpha, dx);
}

void DossierView::build(DrawList& dl, const Frame& f, float alpha, float dx) const {
    last_cv_ = f.cv;
    tabs_.draw(dl, f, alpha);
    const float a = alpha * content_.value();
    const Offsets o = offsets(dx);
    if (a > 0.001f) {
        draw_left(dl, f, a, o.left);
        draw_rows(dl, f, a, o.mid);
        switch (tab_) {
            case 0: draw_records(dl, f, a, o.right); break;
            case 1: draw_rewards(dl, f, a, o.right); break;
            default: draw_item(dl, f, a, o.right); break;
        }
    } else {
        // A caminho da aba: só o cabeçalho.
        line_at(dl, tk_->space.left_x + 3.0f, tk_->space.header_y, T(PcText::DsHeader) + L" /", tk_->type.header,
                tk_->color.dim.with_alpha(alpha));
    }
}

void DossierView::prompts(PromptBar& bar, bool pad) const {
    const auto add = [&](int action, const std::wstring& key, const std::string& glyph, const std::wstring& label) {
        Prompt p;
        p.action = action;
        p.key = key;
        p.glyph = pad ? glyph : std::string();
        p.label = label;
        bar.prompts.push_back(p);
    };
    // A aba a caminho (ou a mostrada): as missões dos recordes e das recompensas, os itens dos aparelhos e das armas.
    const int shown = target_ >= 0 && target_ < kTabs ? target_ : tab_;
    add(0, L"↑↓", "xbox_dpad_v", shown <= 1 ? T(PcText::FeStepMission) : T(PcText::DsPromptScroll));
    add(kPromptBack, L"Esc", "xbox_b", T(PcText::MdPromptBack));
}

std::string DossierView::describe() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "dossie_moderno aba %d alvo %d secao %d foco %d linhas %d enviado %d conteudo %.2f", tab_, target_,
                  snap_.front.dossier.section, focus_, row_count(), sent_ ? 1 : 0, double(content_.value()));
    return buf;
}

}  // namespace nf::ui
