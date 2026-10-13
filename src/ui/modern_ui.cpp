// O estilo de menu Modern: tokens, tela virtual, movimento e lista de desenho. Ver modern_ui.h.

#include "ui/modern_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "core/json.h"

namespace nf::ui {

// ---------------------------------------------------------------- cores

Color rgb(uint32_t v, float a) {
    return Color{float((v >> 16) & 0xff) / 255.0f, float((v >> 8) & 0xff) / 255.0f, float(v & 0xff) / 255.0f, a};
}

Color mix(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

bool parse_color(const std::string& s, Color* out) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    uint32_t v = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
        else return false;
    }
    if (s.size() == 7) {
        *out = rgb(v, 1.0f);
    } else {
        *out = rgb(v >> 8, float(v & 0xff) / 255.0f);
    }
    return true;
}

// ---------------------------------------------------------------- curvas

namespace {

float bez(float p1, float p2, float t) {
    // Bézier cúbica com P0 = 0 e P3 = 1.
    const float u = 1.0f - t;
    return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
}

float bez_d(float p1, float p2, float t) {
    const float u = 1.0f - t;
    return 3.0f * u * u * p1 + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (1.0f - p2);
}

}  // namespace

float curve_eval(const Curve& c, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    // O parâmetro t com bez(x1, x2, t) = x: Newton, e bisseção se a derivada for pequena.
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float err = bez(c.x1, c.x2, t) - x;
        if (std::fabs(err) < 1e-5f) break;
        const float d = bez_d(c.x1, c.x2, t);
        if (std::fabs(d) < 1e-4f) break;
        t = std::clamp(t - err / d, 0.0f, 1.0f);
    }
    if (std::fabs(bez(c.x1, c.x2, t) - x) > 1e-4f) {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 40; ++i) {
            t = (lo + hi) * 0.5f;
            if (bez(c.x1, c.x2, t) < x) lo = t;
            else hi = t;
        }
    }
    return bez(c.y1, c.y2, t);
}

float Tween::value() const {
    if (duration <= 0.0f || elapsed >= duration) return to;
    return from + (to - from) * curve_eval(curve, std::max(0.0f, elapsed) / duration);
}

// ---------------------------------------------------------------- tokens

const Tokens& default_tokens() {
    static const Tokens t{};
    return t;
}

namespace {

struct ColorField {
    const char* name;
    Color* field;
};
struct TypeField {
    const char* name;
    TextStyle* field;
};
struct NumField {
    const char* name;
    float* field;
};
struct CurveField {
    const char* name;
    Curve* field;
};

}  // namespace

bool parse_tokens(const std::string& json, Tokens& out, std::string* error) {
    JVal root;
    if (!json_parse(json, root, error)) return false;
    const auto bad = [error](const std::string& what) {
        if (error != nullptr) *error = what;
        return false;
    };
    if (root.type != JVal::Obj) return bad("a raiz deve ser um objeto");
    Tokens t = out;
    const ColorField colors[] = {
        {"scrim", &t.color.scrim},         {"vignette", &t.color.vignette},     {"warm", &t.color.warm},
        {"text", &t.color.text},           {"text_2", &t.color.text_2},         {"dim", &t.color.dim},
        {"disabled", &t.color.disabled},   {"accent", &t.color.accent},         {"accent_hi", &t.color.accent_hi},
        {"ember", &t.color.ember},         {"ember_deep", &t.color.ember_deep}, {"hairline", &t.color.hairline},
        {"hairline_2", &t.color.hairline_2}, {"track", &t.color.track},         {"knob_off", &t.color.knob_off},
        {"keycap", &t.color.keycap},       {"keycap_text", &t.color.keycap_text}, {"dialog", &t.color.dialog},
        {"meter", &t.color.meter},         {"veil", &t.color.veil},             {"ground", &t.color.ground},
    };
    const TypeField types[] = {
        {"tab", &t.type.tab},           {"header", &t.type.header},         {"category", &t.type.category},
        {"section", &t.type.section},   {"label", &t.type.label},           {"value", &t.type.value},
        {"option", &t.type.option},     {"title", &t.type.title},           {"body", &t.type.body},
        {"meta", &t.type.meta},         {"prompt", &t.type.prompt},         {"keycap", &t.type.keycap},
        {"table_head", &t.type.table_head}, {"cell", &t.type.cell},         {"footer", &t.type.footer},
        {"footer_small", &t.type.footer_small}, {"mono", &t.type.mono},     {"dialog_title", &t.type.dialog_title},
        {"toast", &t.type.toast},       {"menu_big", &t.type.menu_big},     {"menu_small", &t.type.menu_small},
        {"menu_sub", &t.type.menu_sub}, {"hero", &t.type.hero},             {"hero_sub", &t.type.hero_sub},
        {"stat", &t.type.stat},         {"counter", &t.type.counter},       {"page_title", &t.type.page_title},
        {"entry", &t.type.entry},       {"key", &t.type.key},               {"legal", &t.type.legal},
    };
    const NumField spaces[] = {
        {"grid", &t.space.grid},           {"tabs_y", &t.space.tabs_y},       {"top_rule_y", &t.space.top_rule_y},
        {"tab_gap", &t.space.tab_gap},     {"tab_pad", &t.space.tab_pad},     {"tab_key_gap", &t.space.tab_key_gap},
        {"left_x", &t.space.left_x},       {"marker_x", &t.space.marker_x},   {"header_y", &t.space.header_y},
        {"cat_y", &t.space.cat_y},         {"cat_step", &t.space.cat_step},   {"mid_x0", &t.space.mid_x0},
        {"mid_x1", &t.space.mid_x1},       {"pad_l", &t.space.pad_l},         {"pad_r", &t.space.pad_r},
        {"list_top", &t.space.list_top},   {"list_bottom", &t.space.list_bottom}, {"row_h", &t.space.row_h},
        {"section_h", &t.space.section_h}, {"option_h", &t.space.option_h},   {"option_indent", &t.space.option_indent},
        {"key_row_h", &t.space.key_row_h}, {"table_head_h", &t.space.table_head_h}, {"fade_px", &t.space.fade_px},
        {"scroll_x", &t.space.scroll_x},   {"desc_x0", &t.space.desc_x0},     {"desc_x1", &t.space.desc_x1},
        {"strip_y", &t.space.strip_y},     {"title_y", &t.space.title_y},     {"meter_x0", &t.space.meter_x0},
        {"meter_x1", &t.space.meter_x1},   {"meter_y", &t.space.meter_y},     {"bottom_y", &t.space.bottom_y},
        {"prompt_right", &t.space.prompt_right}, {"prompt_gap", &t.space.prompt_gap}, {"keycap_h", &t.space.keycap_h},
        {"underline", &t.space.underline}, {"glow_w", &t.space.glow_w},       {"glow_h", &t.space.glow_h},
        {"toggle_w", &t.space.toggle_w},   {"toggle_h", &t.space.toggle_h},   {"slider_w", &t.space.slider_w},
        {"slider_h", &t.space.slider_h},   {"value_w", &t.space.value_w},     {"slide", &t.space.slide},
        {"menu_y", &t.space.menu_y},       {"menu_step", &t.space.menu_step}, {"menu_step_small", &t.space.menu_step_small},
        {"menu_gap", &t.space.menu_gap},   {"button_row_h", &t.space.button_row_h}, {"hero_y", &t.space.hero_y},
        {"front_top", &t.space.front_top}, {"front_menu_y", &t.space.front_menu_y}, {"logo_h", &t.space.logo_h},
        {"picture", &t.space.picture},     {"key_cell", &t.space.key_cell},   {"key_gap", &t.space.key_gap},
        {"press_y", &t.space.press_y},     {"launch_row_h", &t.space.launch_row_h},
        {"launch_section_h", &t.space.launch_section_h}, {"launch_option_h", &t.space.launch_option_h},
        {"launch_menu_y", &t.space.launch_menu_y},
    };
    const NumField motions[] = {
        {"open", &t.motion.open},         {"close", &t.motion.close},       {"stagger", &t.motion.stagger},
        {"focus", &t.motion.focus},       {"tab", &t.motion.tab},           {"dropdown", &t.motion.dropdown},
        {"value", &t.motion.value},       {"content", &t.motion.content},   {"toggle", &t.motion.toggle},
        {"dialog", &t.motion.dialog},     {"toast_in", &t.motion.toast_in}, {"toast_hold", &t.motion.toast_hold},
        {"toast_out", &t.motion.toast_out}, {"repeat_delay", &t.motion.repeat_delay},
        {"repeat_rate", &t.motion.repeat_rate}, {"scroll", &t.motion.scroll},
        {"fail_in", &t.motion.fail_in},   {"fail_title", &t.motion.fail_title}, {"fail_options", &t.motion.fail_options},
        {"results_in", &t.motion.results_in}, {"front_in", &t.motion.front_in}, {"blur_in", &t.motion.blur_in},
    };
    const CurveField curves[] = {
        {"ease_out", &t.motion.ease_out}, {"ease_in_out", &t.motion.ease_in_out}, {"ease_in", &t.motion.ease_in}};

    if (const JVal* c = root.get("color"); c != nullptr) {
        if (c->type != JVal::Obj) return bad("color: esperava um objeto");
        for (const auto& [key, v] : c->o) {
            Color* f = nullptr;
            for (const ColorField& cf : colors) {
                if (key == cf.name) f = cf.field;
            }
            if (f == nullptr) return bad("color: cor desconhecida: " + key);
            if (v.type != JVal::Str || !parse_color(v.s, f)) return bad("color." + key + ": esperava \"#rrggbb\" ou \"#rrggbbaa\"");
        }
    }
    if (const JVal* ty = root.get("type"); ty != nullptr) {
        if (ty->type != JVal::Obj) return bad("type: esperava um objeto");
        for (const auto& [key, v] : ty->o) {
            TextStyle* f = nullptr;
            for (const TypeField& tf : types) {
                if (key == tf.name) f = tf.field;
            }
            if (f == nullptr) return bad("type: estilo desconhecido: " + key);
            if (v.type != JVal::Obj) return bad("type." + key + ": esperava um objeto");
            for (const auto& [k, x] : v.o) {
                if (k == "face" && x.type == JVal::Str && (x.s == "sans" || x.s == "mono")) {
                    f->face = x.s == "mono" ? Face::Mono : Face::Sans;
                } else if (k == "size" && x.type == JVal::Num) {
                    f->size = std::clamp(float(x.n), 4.0f, 200.0f);
                } else if (k == "weight" && x.type == JVal::Num) {
                    f->weight = std::clamp(int(x.n), 100, 900);
                } else if (k == "tracking" && x.type == JVal::Num) {
                    f->tracking = std::clamp(float(x.n), -0.2f, 2.0f);
                } else if (k == "caps" && x.type == JVal::Bool) {
                    f->caps = x.b;
                } else if (k == "line" && x.type == JVal::Num) {
                    f->line = std::clamp(float(x.n), 0.8f, 3.0f);
                } else {
                    return bad("type." + key + "." + k + ": valor inválido");
                }
            }
        }
    }
    const auto numbers = [&](const char* group, const NumField* fields, size_t n) -> bool {
        const JVal* g = root.get(group);
        if (g == nullptr) return true;
        if (g->type != JVal::Obj) return bad(std::string(group) + ": esperava um objeto");
        for (const auto& [key, v] : g->o) {
            float* f = nullptr;
            for (size_t i = 0; i < n; ++i) {
                if (key == fields[i].name) f = fields[i].field;
            }
            Curve* cv = nullptr;
            if (std::strcmp(group, "motion") == 0) {
                for (const CurveField& cf : curves) {
                    if (key == cf.name) cv = cf.field;
                }
            }
            if (cv != nullptr) {
                if (v.type != JVal::Arr || v.a.size() != 4) return bad(std::string(group) + "." + key + ": esperava [x1, y1, x2, y2]");
                float p[4];
                for (int i = 0; i < 4; ++i) {
                    if (v.a[size_t(i)].type != JVal::Num) return bad(std::string(group) + "." + key + ": esperava números");
                    p[i] = float(v.a[size_t(i)].n);
                }
                if (p[0] < 0.0f || p[0] > 1.0f || p[2] < 0.0f || p[2] > 1.0f) {
                    return bad(std::string(group) + "." + key + ": x1 e x2 vão de 0 a 1");
                }
                *cv = Curve{p[0], p[1], p[2], p[3]};
                continue;
            }
            if (f == nullptr) return bad(std::string(group) + ": chave desconhecida: " + key);
            if (v.type != JVal::Num || !(v.n >= 0.0)) return bad(std::string(group) + "." + key + ": esperava um número >= 0");
            *f = float(v.n);
        }
        return true;
    };
    if (!numbers("space", spaces, sizeof(spaces) / sizeof(spaces[0]))) return false;
    if (!numbers("motion", motions, sizeof(motions) / sizeof(motions[0]))) return false;
    out = t;
    return true;
}

// ---------------------------------------------------------------- tela virtual

Canvas make_canvas(int w, int h) {
    Canvas c;
    c.win_w = std::max(1, w);
    c.win_h = std::max(1, h);
    c.scale = std::min(float(c.win_w) / kCanvasW, float(c.win_h) / kCanvasH);
    c.off_x = (float(c.win_w) - kCanvasW * c.scale) * 0.5f;
    c.off_y = (float(c.win_h) - kCanvasH * c.scale) * 0.5f;
    c.left = canvas_x(c, 0.0f);
    c.top = canvas_y(c, 0.0f);
    c.right = canvas_x(c, float(c.win_w));
    c.bottom = canvas_y(c, float(c.win_h));
    return c;
}

// ---------------------------------------------------------------- lista de desenho

namespace {
DrawCmd& add(std::vector<DrawCmd>& v, DrawCmd::Kind k, const Rect& r, const Color& c) {
    v.emplace_back();
    DrawCmd& d = v.back();
    d.kind = k;
    d.r = r;
    d.c = c;
    return d;
}
}  // namespace

void DrawList::fill(const Rect& r, const Color& c, float radius) {
    if (c.a <= 0.0f) return;
    add(cmds, DrawCmd::Fill, r, c).radius = radius;
}

void DrawList::stroke(const Rect& r, const Color& c, float width, float radius) {
    if (c.a <= 0.0f) return;
    DrawCmd& d = add(cmds, DrawCmd::Stroke, r, c);
    d.width = width;
    d.radius = radius;
}

void DrawList::line(float x0, float y0, float x1, float y1, const Color& c, float width) {
    if (c.a <= 0.0f) return;
    add(cmds, DrawCmd::Line, Rect{x0, y0, x1, y1}, c).width = width;
}

void DrawList::text(const Rect& r, const std::wstring& s, const TextStyle& st, const Color& c, Align a, bool wrap) {
    if (c.a <= 0.0f || s.empty()) return;
    DrawCmd& d = add(cmds, DrawCmd::Text, r, c);
    d.text = s;
    d.style = st;
    d.align = a;
    d.wrap = wrap;
}

void DrawList::ellipse(const Rect& r, const Color& c, float stroke_w) {
    if (c.a <= 0.0f) return;
    add(cmds, DrawCmd::Ellipse, r, c).width = stroke_w;
}

void DrawList::glow(const Rect& r, const Color& core, const Color& edge, float clip_y) {
    if (core.a <= 0.0f) return;
    DrawCmd& d = add(cmds, DrawCmd::Glow, r, core);
    d.c2 = edge;
    d.radius = clip_y;
}

void DrawList::radial(const Rect& r, const Color& centre, const Color& edge) {
    if (centre.a <= 0.0f && edge.a <= 0.0f) return;
    add(cmds, DrawCmd::Radial, r, centre).c2 = edge;
}

void DrawList::gradient(const Rect& r, const Color& a, const Color& b, bool horizontal) {
    if (a.a <= 0.0f && b.a <= 0.0f) return;
    DrawCmd& d = add(cmds, DrawCmd::Gradient, r, a);
    d.c2 = b;
    d.dir = horizontal ? 1 : 0;
}

void DrawList::chevron(const Rect& r, int dir, const Color& c, float width) {
    if (c.a <= 0.0f) return;
    DrawCmd& d = add(cmds, DrawCmd::Chevron, r, c);
    d.dir = dir;
    d.width = width;
}

void DrawList::padlock(const Rect& r, const Color& c) {
    if (c.a <= 0.0f) return;
    add(cmds, DrawCmd::Padlock, r, c);
}

void DrawList::diamond(const Rect& r, const Color& c) {
    if (c.a <= 0.0f) return;
    add(cmds, DrawCmd::Diamond, r, c);
}

void DrawList::sector(float cx, float cy, float r_in, float r_out, float a0, float a1, const Color& c) {
    if (c.a <= 0.0f || r_out <= 0.0f || a1 <= a0) return;
    DrawCmd& d = add(cmds, DrawCmd::Sector, Rect{cx - r_out, cy - r_out, cx + r_out, cy + r_out}, c);
    d.radius = std::max(0.0f, r_in);
    d.a0 = a0;
    d.a1 = a1;
}

void DrawList::sector_gradient(float cx, float cy, float r_in, float r_out, float a0, float a1, const Color& inner,
                               const Color& outer) {
    if ((inner.a <= 0.0f && outer.a <= 0.0f) || r_out <= 0.0f || a1 <= a0) return;
    DrawCmd& d = add(cmds, DrawCmd::Sector, Rect{cx - r_out, cy - r_out, cx + r_out, cy + r_out}, inner);
    d.c2 = outer;
    d.dir = 1;
    d.radius = std::max(0.0f, r_in);
    d.a0 = a0;
    d.a1 = a1;
}

void DrawList::glyph(const Rect& r, const std::string& name, float alpha) {
    if (alpha <= 0.0f) return;
    DrawCmd& d = add(cmds, DrawCmd::Glyph, r, Color{1, 1, 1, alpha});
    d.glyph = name;
}

void DrawList::push_clip(const Rect& r) { add(cmds, DrawCmd::PushClip, r, Color{}); }

void DrawList::pop_clip() { add(cmds, DrawCmd::PopClip, Rect{}, Color{}); }

size_t DrawList::count(DrawCmd::Kind k) const {
    size_t n = 0;
    for (const DrawCmd& d : cmds) n += d.kind == k ? 1 : 0;
    return n;
}

const DrawCmd* DrawList::find_text(const std::wstring& s) const {
    for (const DrawCmd& d : cmds) {
        if (d.kind == DrawCmd::Text && d.text == s) return &d;
    }
    return nullptr;
}

// ---------------------------------------------------------------- texto

float estimate_width(const std::wstring& text, const TextStyle& st) {
    // A Rajdhani é estreita: ~0.47 em por caractere em média (maiúsculas um pouco mais).
    const float per = st.face == Face::Mono ? 0.6f : (st.caps ? 0.56f : 0.47f);
    const float n = float(text.size());
    if (n <= 0.0f) return 0.0f;
    return n * st.size * per + (n - 1.0f) * st.size * st.tracking;
}

std::wstring to_upper(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        if (c >= L'a' && c <= L'z') {
            c = wchar_t(c - 32);
        } else if (c >= 0xe0 && c <= 0xfe && c != 0xf7) {
            c = wchar_t(c - 32);  // à..þ -> À..Þ (fora o ÷)
        }
    }
    return out;
}

std::wstring widen(const std::string& utf8) { return widen_utf8(utf8); }

}  // namespace nf::ui
