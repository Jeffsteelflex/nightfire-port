// Os componentes do estilo de menu Modern. Ver modern_kit.h e docs/design.md.

#include "ui/modern_kit.h"

#include <algorithm>
#include <cmath>

namespace nf::ui {

namespace {

std::wstring styled(const std::wstring& s, const TextStyle& st) { return st.caps ? to_upper(s) : s; }

// Um texto numa linha, centrado na vertical em cy, a partir de x (Left), terminando em x (Right) ou centrado.
void text_at(DrawList& dl, const Frame& f, float x, float cy, const std::wstring& s, const TextStyle& st, const Color& c,
             Align a) {
    const float h = st.size * 1.6f;
    Rect r;
    if (a == Align::Left) {
        r = Rect{x, cy - h * 0.5f, x + 4000.0f, cy + h * 0.5f};
    } else if (a == Align::Right) {
        r = Rect{x - 4000.0f, cy - h * 0.5f, x, cy + h * 0.5f};
    } else {
        r = Rect{x - 2000.0f, cy - h * 0.5f, x + 2000.0f, cy + h * 0.5f};
    }
    (void)f;
    dl.text(r, styled(s, st), st, c, a);
}

}  // namespace

float text_width(const Frame& f, const std::wstring& s, const TextStyle& st) {
    const std::wstring t = styled(s, st);
    if (t.empty()) return 0.0f;
    return f.measure ? f.measure(t, st) : estimate_width(t, st);
}

std::vector<std::wstring> wrap_lines(const Frame& f, const std::wstring& s, const TextStyle& st, float width) {
    std::vector<std::wstring> lines;
    std::wstring line;
    size_t i = 0;
    const std::wstring text = styled(s, st);
    while (i <= text.size()) {
        if (i < text.size() && text[i] == L'\n') {
            lines.push_back(line);
            line.clear();
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && text[j] != L' ' && text[j] != L'\n') ++j;
        const std::wstring word = text.substr(i, j - i);
        const std::wstring candidate = line.empty() ? word : line + L" " + word;
        if (!line.empty() && text_width(f, candidate, TextStyle{st.face, st.size, st.weight, st.tracking, false}) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (j >= text.size()) break;
        i = text[j] == L' ' ? j + 1 : j;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

TextStyle fit_style(const Frame& f, const std::wstring& s, TextStyle st, float width, float min_size) {
    const float w = text_width(f, s, st);
    if (w > width && w > 0.0f) st.size = std::max(min_size, st.size * width / w);
    return st;
}

// ---------------------------------------------------------------- fundo

void draw_background(DrawList& dl, const Frame& f, float amount) {
    if (amount <= 0.0f) return;
    const Tokens& tk = f.tk;
    const Rect win{f.cv.left, f.cv.top, f.cv.right, f.cv.bottom};
    dl.fill(win, tk.color.scrim.with_alpha(amount));
    // A vinheta: quente no meio, quase preta nas bordas (elipse que alcança os cantos).
    const float cx = win.cx(), cy = win.cy();
    const float rx = win.w() * 0.62f, ry = win.h() * 0.78f;
    dl.radial(Rect{cx - rx, cy - ry, cx + rx, cy + ry}, tk.color.warm.with_alpha(amount), tk.color.vignette.with_alpha(amount));
    // As barras de cima e de baixo um pouco mais escuras.
    const Color shade = rgb(0x000000, 0.35f * amount);
    dl.gradient(Rect{win.x0, win.y0, win.x1, 150.0f}, shade, shade.with_alpha(0.0f), false);
    dl.gradient(Rect{win.x0, 930.0f, win.x1, win.y1}, shade.with_alpha(0.0f), shade.with_alpha(1.4f), false);
}

// ---------------------------------------------------------------- barra de cima

void TabBar::layout(const Frame& f) {
    const Tokens& tk = f.tk;
    std::vector<float> w;
    float total = 0.0f;
    for (const std::wstring& l : labels) {
        w.push_back(text_width(f, l, tk.type.tab));
        total += w.back();
    }
    total += tk.space.tab_gap * float(std::max<size_t>(1, labels.size()) - 1);
    float x = kCanvasW * 0.5f - total * 0.5f;
    tab_rects.clear();
    for (size_t i = 0; i < labels.size(); ++i) {
        tab_rects.push_back(Rect{x, tk.space.tabs_y - 26.0f, x + w[i], tk.space.top_rule_y});
        x += w[i] + tk.space.tab_gap;
    }
    const float kh = tk.space.keycap_h;
    const float first = tab_rects.empty() ? kCanvasW * 0.5f : tab_rects.front().x0;
    const float last = tab_rects.empty() ? kCanvasW * 0.5f : tab_rects.back().x1;
    const float pw = std::max(kh, prompt_icon_width(f, key_prev, glyph_prev));
    const float nw = std::max(kh, prompt_icon_width(f, key_next, glyph_next));
    const float cy = tk.space.tabs_y - 4.0f;
    prev_rect = Rect{first - tk.space.tab_key_gap - pw * 0.5f, cy - kh * 0.5f, first - tk.space.tab_key_gap + pw * 0.5f, cy + kh * 0.5f};
    next_rect = Rect{last + tk.space.tab_key_gap - nw * 0.5f, cy - kh * 0.5f, last + tk.space.tab_key_gap + nw * 0.5f, cy + kh * 0.5f};
    if (!laid_out && active >= 0 && size_t(active) < tab_rects.size()) {
        ux0.snap(tab_rects[size_t(active)].x0 - tk.space.tab_pad);
        ux1.snap(tab_rects[size_t(active)].x1 + tk.space.tab_pad);
    }
    laid_out = true;
}

void TabBar::set_active(int i, const Frame& f, bool animate) {
    active = i;
    if (!laid_out) layout(f);
    if (i < 0 || size_t(i) >= tab_rects.size()) return;
    const float a = tab_rects[size_t(i)].x0 - f.tk.space.tab_pad, b = tab_rects[size_t(i)].x1 + f.tk.space.tab_pad;
    if (animate) {
        ux0.set(a, f.tk.motion.tab, f.tk.motion.ease_in_out);
        ux1.set(b, f.tk.motion.tab, f.tk.motion.ease_in_out);
    } else {
        ux0.snap(a);
        ux1.snap(b);
    }
}

void TabBar::update(float dt) {
    ux0.update(dt);
    ux1.update(dt);
}

void TabBar::draw(DrawList& dl, const Frame& f, float alpha) const {
    const Tokens& tk = f.tk;
    // A linha fina na largura da janela.
    dl.line(f.cv.left, tk.space.top_rule_y, f.cv.right, tk.space.top_rule_y, tk.color.hairline.with_alpha(alpha), 1.0f);
    for (size_t i = 0; i < labels.size() && i < tab_rects.size(); ++i) {
        const Rect& r = tab_rects[i];
        const bool on = int(i) == active;
        const bool lock = i < locked.size() && locked[i];
        Color c = on ? tk.color.accent : tk.color.text_2.with_alpha(0.82f);
        if (lock) {
            c = tk.color.disabled;
            dl.padlock(Rect{r.x0 - 22.0f, tk.space.tabs_y - 7.0f, r.x0 - 12.0f, tk.space.tabs_y + 6.0f}, c.with_alpha(alpha));
        }
        text_at(dl, f, r.x0, tk.space.tabs_y, labels[i], tk.type.tab, c.with_alpha(alpha), Align::Left);
    }
    // O sublinhado (desliza entre as abas) com um brilho baixo.
    const float x0 = ux0.value(), x1 = ux1.value();
    if (x1 > x0) {
        const float y = tk.space.top_rule_y;
        dl.glow(Rect{(x0 + x1) * 0.5f - (x1 - x0) * 0.7f, y - 14.0f, (x0 + x1) * 0.5f + (x1 - x0) * 0.7f, y + 14.0f},
                tk.color.ember.with_alpha(0.22f * alpha), tk.color.ember_deep.with_alpha(0.0f), y);
        dl.fill(Rect{x0, y - tk.space.underline * 0.5f - 0.5f, x1, y + tk.space.underline * 0.5f - 0.5f},
                tk.color.accent.with_alpha(alpha));
    }
    // As teclas de trocar de aba.
    if (!labels.empty()) {
        draw_prompt_icon(dl, f, prev_rect.x0, prev_rect.cy(), key_prev, glyph_prev, alpha * 0.9f);
        draw_prompt_icon(dl, f, next_rect.x0, next_rect.cy(), key_next, glyph_next, alpha * 0.9f);
    }
    // Os textinhos técnicos dos cantos.
    const Color tech = tk.color.dim.with_alpha(0.55f * alpha);
    if (!corner_left.empty()) {
        text_at(dl, f, 24.0f, 30.0f, corner_left, tk.type.mono, tech, Align::Left);
        dl.fill(Rect{104.0f, 26.0f, 116.0f, 34.0f}, tk.color.hairline_2.with_alpha(alpha));
    }
    if (!corner_right.empty()) {
        text_at(dl, f, 1840.0f, 30.0f, corner_right, tk.type.mono, tech, Align::Right);
        const float blink = std::fmod(f.time, 1.6f) < 1.1f ? 1.0f : 0.25f;
        dl.ellipse(Rect{1847.0f, 27.0f, 1853.0f, 33.0f}, rgb(0xd8452b, 0.85f * alpha * blink));
        dl.fill(Rect{1862.0f, 25.0f, 1896.0f, 35.0f}, tk.color.hairline.with_alpha(alpha));
    }
}

int TabBar::hit(float x, float y) const {
    for (size_t i = 0; i < tab_rects.size(); ++i) {
        const Rect& r = tab_rects[i];
        if (Rect{r.x0 - 16.0f, r.y0, r.x1 + 16.0f, r.y1}.contains(x, y)) return int(i);
    }
    if (prev_rect.contains(x, y)) return -2;
    if (next_rect.contains(x, y)) return -3;
    return -1;
}

// ---------------------------------------------------------------- categorias

void CategoryList::layout(const Frame& f) {
    const Tokens& tk = f.tk;
    rects.clear();
    for (size_t i = 0; i < items.size(); ++i) {
        const float cy = tk.space.cat_y + float(i) * tk.space.cat_step;
        rects.push_back(Rect{tk.space.marker_x - 16.0f, cy - tk.space.cat_step * 0.5f, tk.space.mid_x0 - 60.0f,
                             cy + tk.space.cat_step * 0.5f});
    }
}

void CategoryList::draw(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    text_at(dl, f, tk.space.left_x + 3.0f + dx, tk.space.header_y, header, tk.type.header, tk.color.dim.with_alpha(alpha),
            Align::Left);
    for (size_t i = 0; i < items.size() && i < rects.size(); ++i) {
        const float cy = rects[i].cy();
        const Item& it = items[i];
        const bool sel = int(i) == selected;
        Color c = tk.color.text_2.with_alpha(0.86f);
        if (int(i) == hover && !sel) c = tk.color.text;
        if (it.locked) c = tk.color.disabled;
        if (sel) c = tk.color.accent;
        const float x = tk.space.left_x + dx;
        text_at(dl, f, x, cy, it.label, tk.type.category, c.with_alpha(alpha), Align::Left);
        if (it.locked) {
            dl.padlock(Rect{tk.space.marker_x - 5.0f + dx, cy - 7.0f, tk.space.marker_x + 5.0f + dx, cy + 6.0f},
                       tk.color.disabled.with_alpha(alpha));
        } else if (sel) {
            dl.fill(Rect{tk.space.marker_x - 4.0f + dx, cy - 4.0f, tk.space.marker_x + 4.0f + dx, cy + 4.0f},
                    tk.color.accent.with_alpha(alpha));
        }
        if (sel && focused) {
            // A coluna com o foco: o sublinhado de ouro sob a categoria, com um brilho baixo.
            const float w = text_width(f, it.label, tk.type.category);
            const float y = cy + 19.0f;
            dl.glow(Rect{x + w * 0.5f - 90.0f, y - 22.0f, x + w * 0.5f + 90.0f, y + 6.0f},
                    tk.color.ember.with_alpha(0.38f * alpha), tk.color.ember_deep.with_alpha(0.0f), y);
            dl.fill(Rect{x - 2.0f, y - 1.0f, x + w + 2.0f, y + 1.0f}, tk.color.accent.with_alpha(alpha));
        }
    }
}

int CategoryList::hit(float x, float y) const {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(x, y)) return int(i);
    }
    return -1;
}

// ---------------------------------------------------------------- foco

void FocusMark::move_to(float nx0, float nx1, float ny, const Frame& f, bool animate) {
    if (!placed || !animate) {
        x0.snap(nx0);
        x1.snap(nx1);
        y.snap(ny);
        placed = true;
        return;
    }
    const Curve& c = f.tk.motion.ease_out;
    x0.set(nx0, f.tk.motion.focus, c);
    x1.set(nx1, f.tk.motion.focus, c);
    y.set(ny, f.tk.motion.focus, c);
}

void FocusMark::show(bool on, const Frame& f) { a.set(on ? 1.0f : 0.0f, f.tk.motion.focus, f.tk.motion.ease_out); }

void FocusMark::update(float dt) {
    x0.update(dt);
    x1.update(dt);
    y.update(dt);
    a.update(dt);
}

void FocusMark::draw(DrawList& dl, const Frame& f, float alpha, float dy) const {
    const float k = a.value() * alpha;
    if (k <= 0.001f || !placed) return;
    const Tokens& tk = f.tk;
    const float l = x0.value(), r = x1.value(), ly = y.value() + dy;
    const float cx = (l + r) * 0.5f;
    const float gw = std::min(tk.space.glow_w, (r - l) * 0.9f), gh = tk.space.glow_h;
    // O brilho de brasa que sobe do sublinhado: uma névoa baixa ao longo da linha, um halo largo e fraco, línguas
    // de fogo estreitas espalhadas pelo meio (cada uma tremula devagar, na sua fase) e um miolo quente pequeno.
    dl.glow(Rect{cx - gw * 0.95f, ly - gh * 0.42f, cx + gw * 0.95f, ly + gh * 0.42f}, tk.color.ember_deep.with_alpha(0.40f * k),
            tk.color.ember_deep.with_alpha(0.0f), ly);
    dl.glow(Rect{cx - gw * 0.55f, ly - gh * 0.95f, cx + gw * 0.55f, ly + gh * 0.95f}, tk.color.ember_deep.with_alpha(0.26f * k),
            tk.color.ember_deep.with_alpha(0.0f), ly);
    static const float kTongue[11][4] = {
        // posição (fração de gw), meia largura, altura (fração de gh), fase
        {-0.46f, 16.0f, 0.42f, 0.4f}, {-0.36f, 22.0f, 0.62f, 2.2f}, {-0.25f, 14.0f, 0.78f, 4.9f}, {-0.16f, 24.0f, 0.70f, 1.3f},
        {-0.06f, 18.0f, 1.00f, 3.7f}, {0.03f, 26.0f, 0.88f, 0.9f},  {0.12f, 15.0f, 1.08f, 5.6f},  {0.21f, 22.0f, 0.74f, 2.8f},
        {0.30f, 14.0f, 0.58f, 4.3f},  {0.39f, 20.0f, 0.48f, 1.8f},  {0.48f, 12.0f, 0.36f, 3.3f},
    };
    for (const auto& t : kTongue) {
        const float flicker = 0.72f + 0.28f * std::sin(f.time * (1.9f + t[3] * 0.41f) + t[3]);
        const float tx = cx + t[0] * gw + 4.0f * std::sin(f.time * 0.8f + t[3] * 1.3f);
        const float th = gh * t[2] * flicker;
        const float edge = 1.0f - std::fabs(t[0]) * 0.9f;  // mais fracas nas pontas
        dl.glow(Rect{tx - t[1], ly - th, tx + t[1], ly + th}, tk.color.ember.with_alpha(0.50f * k * flicker * edge),
                tk.color.ember_deep.with_alpha(0.0f), ly);
    }
    dl.glow(Rect{cx - gw * 0.12f, ly - gh * 0.40f, cx + gw * 0.12f, ly + gh * 0.40f}, tk.color.accent_hi.with_alpha(0.55f * k),
            tk.color.ember.with_alpha(0.0f), ly);
    // O sublinhado, mais claro no meio.
    const float u = tk.space.underline;
    dl.fill(Rect{l, ly - u, r, ly}, tk.color.accent.with_alpha(k));
    dl.gradient(Rect{cx - gw * 0.5f, ly - u, cx, ly}, tk.color.accent.with_alpha(0.0f), tk.color.accent_hi.with_alpha(0.8f * k), true);
    dl.gradient(Rect{cx, ly - u, cx + gw * 0.5f, ly}, tk.color.accent_hi.with_alpha(0.8f * k), tk.color.accent.with_alpha(0.0f), true);
}

// ---------------------------------------------------------------- lista grande

void BigMenu::layout(const Frame& f) {
    const Tokens& tk = f.tk;
    rects.assign(items.size(), Rect{});
    if (lit.size() != items.size()) lit.assign(items.size(), Tween{});
    float cy = y;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (i > 0) {
            const Item& prev = items[i - 1];
            if (it.compact && !prev.compact) cy += tk.space.menu_gap + (tk.space.menu_step + tk.space.menu_step_small) * 0.5f;
            else cy += it.compact ? tk.space.menu_step_small : tk.space.menu_step;
        }
        const TextStyle& st = it.compact ? tk.type.menu_small : tk.type.menu_big;
        const float step = it.compact ? tk.space.menu_step_small : tk.space.menu_step;
        const float w = text_width(f, it.label, st);
        rects[i] = Rect{tk.space.marker_x - 20.0f, cy - step * 0.5f, std::max(x + w + 40.0f, x + 360.0f), cy + step * 0.5f};
    }
}

float BigMenu::centre(int i) const { return i >= 0 && size_t(i) < rects.size() ? rects[size_t(i)].cy() : y; }

void BigMenu::set_focus(int i, const Frame& f, bool animate) {
    if (items.empty()) return;
    focus = std::clamp(i, 0, int(items.size()) - 1);
    if (rects.size() != items.size()) layout(f);
    const Item& it = items[size_t(focus)];
    const TextStyle& st = it.compact ? f.tk.type.menu_small : f.tk.type.menu_big;
    const float w = text_width(f, it.label, st);
    const float under = centre(focus) + st.size * 0.36f + (it.compact ? 9.0f : 13.0f);
    mark.move_to(x - 4.0f, x + w + 6.0f, under, f, animate);
    mark.show(focused, f);
}

void BigMenu::update(float dt, const Frame& f) {
    if (lit.size() != items.size()) lit.assign(items.size(), Tween{});
    mark.update(dt);
    for (size_t i = 0; i < lit.size(); ++i) {
        lit[i].update(dt);
        lit[i].set(int(i) == focus ? 1.0f : 0.0f, f.tk.motion.focus, f.tk.motion.ease_out);
    }
    mark.show(focused, f);
}

void BigMenu::draw(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = f.tk;
    for (size_t i = 0; i < items.size() && i < rects.size(); ++i) {
        const Item& it = items[i];
        const float cy = rects[i].cy();
        const TextStyle& st = it.compact ? tk.type.menu_small : tk.type.menu_big;
        const float t = i < lit.size() ? lit[i].value() : (int(i) == focus ? 1.0f : 0.0f);
        Color base = it.compact ? tk.color.text_2 : tk.color.text;
        if (int(i) == hover && int(i) != focus) base = mix(base, tk.color.accent_hi, 0.25f);
        Color c = mix(base, tk.color.accent, t);
        if (it.locked) c = tk.color.disabled;
        const float lx = x + dx;
        text_at(dl, f, lx, cy, it.label, st, c.with_alpha(alpha), Align::Left);
        const float mx = tk.space.marker_x + dx;
        if (it.locked) {
            dl.padlock(Rect{mx - 5.0f, cy - 7.0f, mx + 5.0f, cy + 6.0f}, tk.color.disabled.with_alpha(alpha));
        } else if (t > 0.01f) {
            const float s = 4.0f * t;
            dl.fill(Rect{mx - s, cy - s, mx + s, cy + s}, tk.color.accent.with_alpha(alpha * t));
        }
        // O subtítulo ao lado, na linha de base do nome (as maiúsculas centradas: a diferença das alturas).
        if (!it.sub.empty() && t > 0.01f) {
            const float w = text_width(f, it.label, st);
            const float sy = cy + (st.size - tk.type.menu_sub.size) * 0.36f;
            const Color sc = (it.locked ? tk.color.disabled : tk.color.text_2).with_alpha(alpha * t * 0.92f);
            text_at(dl, f, lx + w + 22.0f - 6.0f * (1.0f - t), sy, L"/  " + it.sub, tk.type.menu_sub, sc, Align::Left);
        }
    }
    mark.draw(dl, f, alpha);
}

int BigMenu::hit(float hx, float hy) const {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(hx, hy)) return int(i);
    }
    return -1;
}

// ---------------------------------------------------------------- linhas

void draw_hairline(DrawList& dl, const Frame& f, float x0, float x1, float y, float alpha) {
    dl.line(x0, y, x1, y, f.tk.color.hairline.with_alpha(alpha), 1.0f);
}

void draw_row_label(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& label, const RowState& s, float indent,
                    const TextStyle* style) {
    const Tokens& tk = f.tk;
    Color c = mix(tk.color.text, tk.color.accent, s.label_t);
    if (s.disabled) c = tk.color.disabled;
    text_at(dl, f, row.x0 + tk.space.pad_l + indent, row.cy() - 1.0f, label, style != nullptr ? *style : tk.type.label,
            c.with_alpha(s.alpha), Align::Left);
}

void draw_section(DrawList& dl, const Frame& f, const Rect& r, const std::wstring& text, float alpha) {
    const Tokens& tk = f.tk;
    text_at(dl, f, r.x0 + 2.0f, r.y0 + r.h() * 0.64f, text, tk.type.section, tk.color.text.with_alpha(0.92f * alpha), Align::Left);
}

void draw_dropdown_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& value, bool open, const RowState& s,
                         const std::wstring& old, float change_t) {
    const Tokens& tk = f.tk;
    const float right = row.x1 - tk.space.pad_r;
    const float cy = row.cy();
    const Color chev = (s.disabled ? tk.color.disabled : mix(tk.color.text_2, tk.color.accent, s.label_t)).with_alpha(s.alpha);
    dl.chevron(Rect{right - 13.0f, cy - 4.0f, right - 1.0f, cy + 3.0f}, open ? 1 : 0, chev, 1.6f);
    const float vx = right - 32.0f;
    Color vc = s.disabled ? tk.color.disabled : mix(tk.color.text_2, tk.color.text, s.label_t);
    // A troca de valor: o antigo sobe e some, o novo sobe de 6 px e aparece.
    if (!old.empty() && change_t < 1.0f) {
        text_at(dl, f, vx, cy - 6.0f * change_t, old, tk.type.value, vc.with_alpha(s.alpha * (1.0f - change_t)), Align::Right);
    }
    const float k = old.empty() ? 1.0f : change_t;
    text_at(dl, f, vx, cy + 6.0f * (1.0f - k), value, tk.type.value,
            mix(vc, tk.color.accent_hi, (1.0f - k) * 0.6f).with_alpha(s.alpha * k), Align::Right);
}

void draw_toggle(DrawList& dl, const Frame& f, const Rect& row, float knob, const std::wstring& text, const RowState& s,
                 float reserve) {
    const Tokens& tk = f.tk;
    const float right = row.x1 - tk.space.pad_r;
    const float cy = row.cy();
    const float a = s.alpha;
    Color tc = s.disabled ? tk.color.disabled : mix(tk.color.text_2, tk.color.text, s.label_t);
    text_at(dl, f, right, cy, text, tk.type.value, tc.with_alpha(a), Align::Right);
    const float tw = tk.space.toggle_w, th = tk.space.toggle_h;
    const float col = std::max({reserve, text_width(f, text, tk.type.value), 36.0f});
    const float x1 = right - col - 26.0f, x0 = x1 - tw;
    const Rect track{x0, cy - th * 0.5f, x1, cy + th * 0.5f};
    dl.fill(track, rgb(0x000000, 0.32f * a), 2.0f);
    dl.fill(track, tk.color.track.with_alpha(a * (0.6f + 0.4f * knob)), 2.0f);
    dl.stroke(track, tk.color.hairline_2.with_alpha(a), 1.0f, 2.0f);
    const float ks = 14.0f;
    const float kx = x0 + 5.0f + knob * (tw - 10.0f - ks);
    const Rect k{kx, cy - ks * 0.5f, kx + ks, cy + ks * 0.5f};
    const Color kc = s.disabled ? tk.color.disabled : mix(tk.color.knob_off, tk.color.accent, knob);
    if (knob > 0.05f && !s.disabled) {
        dl.glow(Rect{k.cx() - 18.0f, cy - 16.0f, k.cx() + 18.0f, cy + 16.0f}, tk.color.ember.with_alpha(0.28f * knob * a),
                tk.color.ember_deep.with_alpha(0.0f), cy + 16.0f);
    }
    dl.fill(k, kc.with_alpha(a), 1.5f);
}

Rect slider_track(const Frame& f, const Rect& row) {
    const Tokens& tk = f.tk;
    const float right = row.x1 - tk.space.pad_r - tk.space.value_w - 22.0f;
    return Rect{right - tk.space.slider_w, row.cy() - tk.space.slider_h * 0.5f, right, row.cy() + tk.space.slider_h * 0.5f};
}

void draw_slider(DrawList& dl, const Frame& f, const Rect& row, float fill, const std::wstring& value, const RowState& s) {
    const Tokens& tk = f.tk;
    const float a = s.alpha;
    const Rect t = slider_track(f, row);
    fill = std::clamp(fill, 0.0f, 1.0f);
    dl.fill(t, tk.color.track.with_alpha(a), 1.0f);
    const float fx = t.x0 + t.w() * fill;
    const Color fc = s.disabled ? tk.color.disabled : tk.color.accent;
    dl.fill(Rect{t.x0, t.y0, fx, t.y1}, fc.with_alpha(a), 1.0f);
    const float ks = s.focused && !s.disabled ? 14.0f : 11.0f;
    if (s.focused && !s.disabled) {
        dl.glow(Rect{fx - 22.0f, t.cy() - 18.0f, fx + 22.0f, t.cy() + 18.0f}, tk.color.ember.with_alpha(0.35f * a),
                tk.color.ember_deep.with_alpha(0.0f), t.cy() + 18.0f);
    }
    dl.diamond(Rect{fx - ks * 0.5f, t.cy() - ks * 0.5f, fx + ks * 0.5f, t.cy() + ks * 0.5f},
               (s.disabled ? tk.color.disabled : (s.focused ? tk.color.accent_hi : tk.color.accent)).with_alpha(a));
    const Color vc = s.disabled ? tk.color.disabled : mix(tk.color.text_2, tk.color.text, s.label_t);
    text_at(dl, f, row.x1 - tk.space.pad_r, row.cy(), value, tk.type.value, vc.with_alpha(a), Align::Right);
}

void draw_option(DrawList& dl, const Frame& f, const Rect& r, const std::wstring& label, bool chosen, const RowState& s) {
    const Tokens& tk = f.tk;
    const float a = s.alpha;
    Color c = mix(tk.color.text_2, tk.color.text, std::max(s.label_t, chosen ? 0.6f : 0.0f));
    if (chosen && s.label_t <= 0.0f) c = tk.color.text;
    text_at(dl, f, r.x0 + tk.space.pad_l, r.cy() - 1.0f, label, tk.type.option, c.with_alpha(a), Align::Left);
    const float cx = r.x1 - tk.space.pad_r - 4.0f, cy = r.cy();
    if (chosen) {
        dl.glow(Rect{cx - 16.0f, cy - 16.0f, cx + 16.0f, cy + 16.0f}, tk.color.ember.with_alpha(0.35f * a),
                tk.color.ember_deep.with_alpha(0.0f), cy + 16.0f);
        dl.ellipse(Rect{cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 6.0f}, tk.color.accent_hi.with_alpha(a));
    } else {
        dl.ellipse(Rect{cx - 10.0f, cy - 10.0f, cx + 10.0f, cy + 10.0f}, rgb(0x000000, 0.35f * a));
        dl.ellipse(Rect{cx - 10.0f, cy - 10.0f, cx + 10.0f, cy + 10.0f}, tk.color.hairline_2.with_alpha(a), 1.0f);
    }
}

void draw_info_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& value, const RowState& s) {
    const Tokens& tk = f.tk;
    text_at(dl, f, row.x1 - tk.space.pad_r, row.cy(), value, tk.type.value, tk.color.text_2.with_alpha(s.alpha), Align::Right);
}

void draw_locked_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& text, const RowState& s) {
    const Tokens& tk = f.tk;
    const float right = row.x1 - tk.space.pad_r;
    const float w = text_width(f, text, tk.type.value);
    text_at(dl, f, right, row.cy(), text, tk.type.value, tk.color.dim.with_alpha(s.alpha), Align::Right);
    const float px = right - w - 22.0f;
    dl.padlock(Rect{px - 6.0f, row.cy() - 8.0f, px + 6.0f, row.cy() + 7.0f}, tk.color.dim.with_alpha(s.alpha));
}

void draw_action(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& label, bool on, const RowState& s,
                 const std::wstring& key, const std::string& glyph) {
    const Tokens& tk = f.tk;
    const float a = s.alpha;
    TextStyle st = tk.type.tab;
    st.size = 17.0f;
    Color c = on ? mix(tk.color.accent, tk.color.accent_hi, s.label_t * 0.5f) : tk.color.disabled;
    text_at(dl, f, row.x0 + tk.space.pad_l, row.cy(), label, st, c.with_alpha(a), Align::Left);
    if (on) {
        const float w = prompt_icon_width(f, key, glyph);
        draw_prompt_icon(dl, f, row.x1 - tk.space.pad_r - w, row.cy(), key, glyph, a);
    }
}

void draw_scroll(DrawList& dl, const Frame& f, float x, float y0, float y1, float view_frac, float pos_frac, float alpha) {
    const Tokens& tk = f.tk;
    if (view_frac >= 1.0f) {
        dl.line(x, y0, x, y1, tk.color.hairline.with_alpha(alpha), 1.0f);
        return;
    }
    dl.line(x, y0, x, y1, tk.color.hairline_2.with_alpha(alpha), 1.0f);
    const float len = (y1 - y0) * std::clamp(view_frac, 0.08f, 1.0f);
    const float top = y0 + (y1 - y0 - len) * std::clamp(pos_frac, 0.0f, 1.0f);
    dl.fill(Rect{x - 1.5f, top, x + 1.5f, top + len}, tk.color.accent.with_alpha(alpha));
}

// ---------------------------------------------------------------- teclas e comandos

float keycap_width(const Frame& f, const std::wstring& label) {
    const float h = f.tk.space.keycap_h;
    return std::max(h, text_width(f, label, f.tk.type.keycap) + 16.0f);
}

float draw_keycap(DrawList& dl, const Frame& f, float x, float cy, const std::wstring& label, float alpha) {
    const Tokens& tk = f.tk;
    const float h = tk.space.keycap_h;
    const float w = keycap_width(f, label);
    const Rect r{x, cy - h * 0.5f, x + w, cy + h * 0.5f};
    dl.fill(r, tk.color.keycap.with_alpha(alpha), 3.0f);
    dl.text(r, to_upper(label), tk.type.keycap, tk.color.keycap_text.with_alpha(alpha), Align::Centre);
    return w;
}

float prompt_icon_width(const Frame& f, const std::wstring& key, const std::string& glyph) {
    if (!glyph.empty()) return f.tk.space.keycap_h + 2.0f;
    return keycap_width(f, key);
}

float draw_prompt_icon(DrawList& dl, const Frame& f, float x, float cy, const std::wstring& key, const std::string& glyph,
                       float alpha) {
    if (!glyph.empty()) {
        const float h = f.tk.space.keycap_h + 2.0f;
        dl.glyph(Rect{x, cy - h * 0.5f, x + h, cy + h * 0.5f}, glyph, alpha);
        return h;
    }
    return draw_keycap(dl, f, x, cy, key, alpha);
}

void PromptBar::layout(const Frame& f) {
    const Tokens& tk = f.tk;
    rects.assign(prompts.size(), Rect{});
    float x = tk.space.prompt_right;
    for (size_t k = prompts.size(); k-- > 0;) {
        const Prompt& p = prompts[k];
        const float w = prompt_icon_width(f, p.key, p.glyph) + 10.0f + text_width(f, p.label, tk.type.prompt);
        rects[k] = Rect{x - w, tk.space.bottom_y - 20.0f, x, tk.space.bottom_y + 20.0f};
        x -= w + tk.space.prompt_gap;
    }
}

void PromptBar::draw(DrawList& dl, const Frame& f, float alpha) const {
    const Tokens& tk = f.tk;
    const float y = tk.space.bottom_y;
    // A informação à esquerda (o port e o jogo), cada bloco com a linha fina vertical.
    const auto block = [&](float x, const std::wstring& title, const std::wstring& sub, bool accent_sub) {
        if (title.empty()) return;
        dl.line(x, y - 22.0f, x, y + 20.0f, tk.color.hairline_2.with_alpha(alpha), 1.0f);
        text_at(dl, f, x + 11.0f, sub.empty() ? y : y - 11.0f, title, tk.type.footer, tk.color.text.with_alpha(0.9f * alpha),
                Align::Left);
        if (!sub.empty()) {
            const TextStyle& st = accent_sub ? tk.type.footer_small : tk.type.footer;
            text_at(dl, f, x + 11.0f, y + 12.0f, sub, st, (accent_sub ? tk.color.accent : tk.color.text).with_alpha(0.9f * alpha),
                    Align::Left);
        }
    };
    block(76.0f, info_title, info_sub, true);
    block(710.0f, info2_title, info2_sub, false);
    for (size_t k = 0; k < prompts.size() && k < rects.size(); ++k) {
        const Prompt& p = prompts[k];
        const Rect& r = rects[k];
        const float w = draw_prompt_icon(dl, f, r.x0, y, p.key, p.glyph, alpha);
        text_at(dl, f, r.x0 + w + 10.0f, y, p.label, tk.type.prompt, tk.color.text.with_alpha(0.92f * alpha), Align::Left);
    }
    // Os cantinhos de baixo (decorativos).
    const Color c = tk.color.hairline_2.with_alpha(alpha);
    dl.line(24.0f, 1056.0f, 34.0f, 1056.0f, c, 1.0f);
    dl.line(24.0f, 1046.0f, 24.0f, 1056.0f, c, 1.0f);
    dl.line(1886.0f, 1056.0f, 1896.0f, 1056.0f, c, 1.0f);
    dl.line(1896.0f, 1046.0f, 1896.0f, 1056.0f, c, 1.0f);
}

int PromptBar::hit(float x, float y) const {
    for (size_t k = 0; k < rects.size() && k < prompts.size(); ++k) {
        if (rects[k].contains(x, y)) return prompts[k].action;
    }
    return 0;
}

// ---------------------------------------------------------------- descrição

float draw_description(DrawList& dl, const Frame& f, const Description& d, float alpha, float dx) {
    const Tokens& tk = f.tk;
    const float x0 = tk.space.desc_x0 + dx, x1 = tk.space.desc_x1 + dx;
    const float w = x1 - x0;
    // A faixa de dados: um friso escuro com textinhos técnicos e marcas.
    const float sy = tk.space.strip_y;
    dl.fill(Rect{x0, sy, x1, sy + 24.0f}, rgb(0xf5e6d0, 0.035f * alpha));
    dl.fill(Rect{x0 + 4.0f, sy + 4.0f, x0 + 6.0f, sy + 20.0f}, tk.color.text.with_alpha(0.75f * alpha));
    const float xs[4] = {x0 + 16.0f, x0 + 92.0f, x0 + 236.0f, x1 - 8.0f};
    for (int k = 0; k < 4; ++k) {
        if (d.strip[k].empty()) continue;
        text_at(dl, f, xs[k], sy + 12.0f, d.strip[k], tk.type.mono, tk.color.dim.with_alpha(0.8f * alpha),
                k == 3 ? Align::Right : Align::Left);
    }
    dl.fill(Rect{x0 + 166.0f, sy + 8.0f, x0 + 180.0f, sy + 16.0f}, tk.color.hairline_2.with_alpha(alpha));
    for (float tx = x0 + 300.0f; tx < x1 - 70.0f; tx += 10.0f) {
        const float th = std::fmod(tx - x0, 40.0f) < 1.0f ? 8.0f : 4.0f;
        dl.line(tx, sy + 12.0f - th * 0.5f, tx, sy + 12.0f + th * 0.5f, tk.color.hairline_2.with_alpha(0.8f * alpha), 1.0f);
    }
    // O título (encolhe para caber), o texto, uma linha, o valor e outra linha.
    float y = tk.space.title_y;
    const TextStyle ts = fit_style(f, d.title, tk.type.title, w, 24.0f);
    text_at(dl, f, x0, y, d.title, ts, tk.color.text.with_alpha(alpha), Align::Left);
    y += 46.0f;
    const TextStyle& bs = tk.type.body;
    const float lh = bs.size * bs.line;
    for (const std::wstring& line : wrap_lines(f, d.body, bs, w)) {
        text_at(dl, f, x0, y, line, bs, tk.color.text_2.with_alpha(alpha), Align::Left);
        y += lh;
    }
    y += 20.0f;
    if (!d.value.empty() || !d.value_label.empty()) {
        draw_hairline(dl, f, x0, x1, y, alpha);
        y += 38.0f;
        const float lw = text_width(f, d.value_label, tk.type.meta);
        text_at(dl, f, x0, y, d.value_label, tk.type.meta, tk.color.text_2.with_alpha(alpha), Align::Left);
        TextStyle vs = tk.type.meta;
        vs.weight = 600;
        text_at(dl, f, x0 + lw + 8.0f, y, d.value, vs, tk.color.text.with_alpha(alpha), Align::Left);
        y += 38.0f;
        draw_hairline(dl, f, x0, x1, y, alpha);
        // Uns textinhos técnicos sob a linha (decorativos, como os da referência).
        y += 22.0f;
        text_at(dl, f, x0, y, L"REF.SLUS-20579", tk.type.mono, tk.color.dim.with_alpha(0.35f * alpha), Align::Left);
        text_at(dl, f, x0 + 104.0f, y, L"PS2>PC  D3D11/D2D", tk.type.mono, tk.color.dim.with_alpha(0.35f * alpha), Align::Left);
    }
    if (!d.notice.empty()) {
        y += 30.0f;
        bool first = true;
        for (const std::wstring& line : wrap_lines(f, d.notice, tk.type.meta, w - 16.0f)) {
            if (first) dl.fill(Rect{x0, y - 4.0f, x0 + 6.0f, y + 2.0f}, tk.color.accent.with_alpha(alpha));
            first = false;
            text_at(dl, f, x0 + 16.0f, y, line, tk.type.meta, tk.color.accent.with_alpha(alpha), Align::Left);
            y += tk.type.meta.size * 1.35f;
        }
    }
    if (d.meter) {
        const float my = tk.space.meter_y;
        const float mx0 = tk.space.meter_x0 + dx, mx1 = tk.space.meter_x1 + dx;
        TextStyle ls = tk.type.meta;
        ls.caps = true;
        ls.tracking = 0.06f;
        text_at(dl, f, mx0 + 2.0f, my - 20.0f, d.meter_label, ls, tk.color.text.with_alpha(alpha), Align::Left);
        text_at(dl, f, mx1 - 2.0f, my - 20.0f, d.meter_value, tk.type.meta, tk.color.text.with_alpha(alpha), Align::Right);
        const Rect bar{mx0, my, mx1, my + 18.0f};
        dl.fill(bar, rgb(0xf5e6d0, 0.20f * alpha));
        dl.fill(Rect{bar.x0, bar.y0, bar.x0 + bar.w() * std::clamp(d.meter_fill, 0.0f, 1.0f), bar.y1}, tk.color.meter.with_alpha(alpha));
    }
    return y;
}

// ---------------------------------------------------------------- diálogo

void Dialog::show(const std::wstring& t, const std::wstring& b, std::vector<std::wstring> options, int focused, const Frame& f) {
    title = t;
    body = b;
    buttons = std::move(options);
    focus = focused;
    hover = -1;
    countdown = -1.0f;
    busy = false;
    open = true;
    appear.set(1.0f, f.tk.motion.dialog, f.tk.motion.ease_out);
    mark.placed = false;
    mark.a.snap(1.0f);
    layout(f);
    if (focus >= 0 && size_t(focus) < button_rects.size()) {
        const Rect& r = button_rects[size_t(focus)];
        mark.move_to(r.x0, r.x1, r.y1, f, false);
    }
}

void Dialog::hide(const Frame& f) {
    open = false;
    appear.set(0.0f, f.tk.motion.dialog, f.tk.motion.ease_out);
}

void Dialog::layout(const Frame& f) {
    const Tokens& tk = f.tk;
    button_rects.clear();
    float total = 0.0f;
    std::vector<float> w;
    for (const std::wstring& b : buttons) {
        w.push_back(std::max(190.0f, text_width(f, b, tk.type.tab) + 64.0f));
        total += w.back();
    }
    total += 32.0f * float(std::max<size_t>(1, buttons.size()) - 1);
    float x = kCanvasW * 0.5f - total * 0.5f;
    // Um texto de mais de duas linhas (as caixas do jogo) aumenta o painel para baixo e para cima por igual.
    const TextStyle& bs = tk.type.body;
    const size_t lines = wrap_lines(f, body, bs, 840.0f - 120.0f).size() + (countdown >= 0.0f ? 1 : 0);
    grow = lines > 2 ? float(lines - 2) * bs.size * bs.line : 0.0f;
    const float by = 540.0f + 74.0f + grow * 0.5f;
    for (float bw : w) {
        button_rects.push_back(Rect{x, by - 30.0f, x + bw, by + 30.0f});
        x += bw + 32.0f;
    }
}

void Dialog::update(float dt) {
    appear.update(dt);
    mark.update(dt);
}

void Dialog::draw(DrawList& dl, const Frame& f) const {
    const float k = appear.value();
    if (k <= 0.001f) return;
    const Tokens& tk = f.tk;
    dl.fill(Rect{f.cv.left, f.cv.top, f.cv.right, f.cv.bottom}, rgb(0x050302, 0.62f * k));
    const float lift = 10.0f * (1.0f - k);
    const float half = 150.0f + grow * 0.5f;
    const Rect p{kCanvasW * 0.5f - 420.0f, 540.0f - half + lift, kCanvasW * 0.5f + 420.0f, 540.0f + half + lift};
    dl.fill(p, tk.color.dialog.with_alpha(k));
    dl.gradient(Rect{p.x0, p.y0, p.x1, p.y0 + 120.0f}, rgb(0x2a1608, 0.35f * k), rgb(0x2a1608, 0.0f), false);
    dl.line(p.x0, p.y0, p.x1, p.y0, tk.color.accent.with_alpha(0.45f * k), 1.0f);
    dl.line(p.x0, p.y1, p.x1, p.y1, tk.color.accent.with_alpha(0.45f * k), 1.0f);
    dl.fill(Rect{p.x0, p.y0 - 1.0f, p.x0 + 64.0f, p.y0 + 1.0f}, tk.color.accent.with_alpha(k));
    const TextStyle ts = fit_style(f, title, tk.type.dialog_title, p.w() - 80.0f, 22.0f);
    text_at(dl, f, p.cx(), p.y0 + 62.0f, title, ts, tk.color.text.with_alpha(k), Align::Centre);
    float y = p.y0 + 112.0f;
    for (const std::wstring& line : wrap_lines(f, body, tk.type.body, p.w() - 120.0f)) {
        text_at(dl, f, p.cx(), y, line, tk.type.body, tk.color.text_2.with_alpha(k), Align::Centre);
        y += tk.type.body.size * tk.type.body.line;
    }
    if (countdown >= 0.0f) {
        const float half = 180.0f * std::clamp(countdown, 0.0f, 1.0f);
        dl.line(p.cx() - 180.0f, y + 2.0f, p.cx() + 180.0f, y + 2.0f, tk.color.hairline.with_alpha(k), 1.0f);
        dl.fill(Rect{p.cx() - half, y + 1.0f, p.cx() + half, y + 3.0f}, tk.color.accent.with_alpha(k));
    }
    if (busy && buttons.empty()) {
        // A espera: um traço de ouro que corre e volta na linha fina, devagar (o cartão gravando).
        const float by = std::max(y + 18.0f, p.y1 - 70.0f);
        dl.line(p.cx() - 180.0f, by, p.cx() + 180.0f, by, tk.color.hairline.with_alpha(k), 1.0f);
        const float ph = std::fmod(f.time, 1.6f) / 1.6f;
        const float s = 0.5f - 0.5f * std::cos(ph * 6.2831853f);
        const float cx = p.cx() - 135.0f + 270.0f * s;
        dl.fill(Rect{cx - 45.0f, by - 1.0f, cx + 45.0f, by + 1.0f}, tk.color.accent.with_alpha(k));
    }
    for (size_t i = 0; i < buttons.size() && i < button_rects.size(); ++i) {
        const Rect r = button_rects[i].offset(0.0f, lift);
        const bool on = int(i) == focus;
        const Color c = on ? tk.color.accent : (int(i) == hover ? tk.color.text : tk.color.text_2);
        text_at(dl, f, r.cx(), r.cy(), buttons[i], tk.type.tab, c.with_alpha(k), Align::Centre);
    }
    // O foco dos botões (o mesmo sublinhado com brilho das linhas).
    mark.draw(dl, f, k);
}

int Dialog::hit(float x, float y) const {
    if (!open) return -1;
    for (size_t i = 0; i < button_rects.size(); ++i) {
        if (button_rects[i].contains(x, y)) return int(i);
    }
    return -1;
}

// ---------------------------------------------------------------- aviso

void Toast::update(float dt, const Tokens& tk) {
    if (age < 0.0f) return;
    age += dt;
    if (age > tk.motion.toast_in + tk.motion.toast_hold + tk.motion.toast_out) age = -1.0f;
}

void Toast::draw(DrawList& dl, const Frame& f) const {
    if (age < 0.0f || text.empty()) return;
    const Tokens& tk = f.tk;
    float k = 1.0f;
    if (age < tk.motion.toast_in) {
        k = curve_eval(tk.motion.ease_out, age / tk.motion.toast_in);
    } else if (age > tk.motion.toast_in + tk.motion.toast_hold) {
        k = 1.0f - std::min(1.0f, (age - tk.motion.toast_in - tk.motion.toast_hold) / tk.motion.toast_out);
    }
    const float tw = text_width(f, text, tk.type.toast);
    const float cy = 952.0f + 8.0f * (1.0f - k);
    const Rect r{kCanvasW * 0.5f - tw * 0.5f - 34.0f, cy - 20.0f, kCanvasW * 0.5f + tw * 0.5f + 22.0f, cy + 20.0f};
    dl.fill(r, rgb(0x120c08, 0.92f * k), 2.0f);
    dl.stroke(r, tk.color.accent.with_alpha(0.35f * k), 1.0f, 2.0f);
    dl.fill(Rect{r.x0 + 14.0f, cy - 3.0f, r.x0 + 20.0f, cy + 3.0f}, tk.color.accent.with_alpha(k));
    text_at(dl, f, r.x0 + 30.0f, cy, text, tk.type.toast, tk.color.text.with_alpha(k), Align::Left);
}

}  // namespace nf::ui
