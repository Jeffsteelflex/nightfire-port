// O desenho da roda de armas. Ver modern_wheel.h.

#include "ui/modern_wheel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/pc_text.h"
#include "engine/txt.h"
#include "engine/weapon_wheel.h"

namespace nf::ui {

namespace {

constexpr float kPi = 3.14159265f;

std::wstring pc(PcText t) { return widen(PcText_Utf8(t)); }

// O nome de um rótulo do jogo (sem os códigos ~X dos símbolos de botão).
std::wstring label_text(uint32_t label) {
    if (label == 0) return {};
    const char* s = Txt_BindLabel(label, 0);
    if (s == nullptr) return {};
    std::string clean;
    for (const char* p = s; *p != 0; ++p) {
        if (*p == '~' && p[1] != 0) {
            ++p;
            continue;
        }
        clean.push_back(*p);
    }
    return wheel_game_text(clean.c_str());
}

WheelViewSlot view_slot(const WheelSlot& s) {
    WheelViewSlot v;
    v.name = label_text(s.name_label);
    while (!v.name.empty() && v.name.back() == L' ') v.name.pop_back();  // "AT-600 Scorpion " no jogo
    if (v.name.empty() && s.klass == 1) v.name = pc(PcText::WheelUnarmed);  // o soco: sem nome no HUD do jogo
    if (v.name.empty()) {
        wchar_t buf[16];
        std::swprintf(buf, 16, L"#%02X", unsigned(uint8_t(s.weapon)));
        v.name = buf;
    }
    if (s.uses_ammo) {
        wchar_t buf[32];
        std::swprintf(buf, 32, L"%d-%d", int(s.clip), int(s.stored));
        v.ammo = buf;
    } else {
        v.ammo = L"—";
    }
    v.mode = label_text(s.mode_label);
    v.usable = s.usable;
    v.current = s.current;
    v.icon = s.icon;
    return v;
}

float ease_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

const WheelGeometry& wheel_geometry() {
    static const WheelGeometry g;
    return g;
}

WheelSlotGeom wheel_slot_geometry(const WheelGeometry& g, int ring, int index, int slots, float k) {
    WheelSlotGeom s;
    slots = std::max(1, slots);
    s.r0 = (ring == 2 ? g.outer0 : g.inner0) * k;
    s.r1 = (ring == 2 ? g.outer1 : g.inner1) * k;
    s.rm = (s.r0 + s.r1) * 0.5f;
    const float half = kPi / float(slots);
    // A folga em pixels no meio do anel vira um ângulo (constante no setor: as bordas ficam radiais).
    const float gap = slots == 1 ? 0.0f : std::min(half * 0.3f, g.gap / std::max(1.0f, s.rm));
    s.mid = 2.0f * kPi * float(index) / float(slots);
    s.a0 = s.mid - half + gap;
    s.a1 = s.mid + half - gap;
    s.px = g.cx + s.rm * std::sin(s.mid);
    s.py = g.cy - s.rm * std::cos(s.mid);
    const float chord = 2.0f * s.rm * std::sin(std::max(0.02f, half - gap));
    s.box = std::min(s.r1 - s.r0, chord);
    // O nome cabe nos espaços de pelo menos 92 de lado (os anéis de 8, 12 e 16 espaços de fora, 6 e 8 de dentro).
    s.names = s.box >= 92.0f * k;
    return s;
}

std::wstring wheel_game_text(const char* s) {
    // Windows-1252: o latim-1 e os de 0x80 a 0x9f que o jogo usa (’ … ™ e as aspas).
    static const wchar_t k80[32] = {0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160,
                                    0x2039, 0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
                                    0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178};
    std::wstring out;
    if (s == nullptr) return out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != 0; ++p) {
        out.push_back(*p >= 0x80 && *p < 0xa0 ? k80[*p - 0x80] : wchar_t(*p));
    }
    return out;
}

std::vector<uint8_t> wheel_icon_image(const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> out(size_t(std::max(0, w)) * size_t(std::max(0, h)) * 4, 0);
    if (rgba == nullptr || w <= 0 || h <= 0) return out;
    const float cx = float(w) * 0.5f, cy = float(h) * 0.5f, size = float(std::min(w, h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgba + (size_t(y) * size_t(w) + size_t(x)) * 4;
            const float r = p[0], g = p[1], b = p[2];
            const float src_a = std::min(1.0f, float(p[3]) / 128.0f);
            // O disco do radar: o anel verde claro fica a 0,40 do lado (e as letrinhas do radar logo dentro dele); fora,
            // o fundo escuro.
            const float rr = std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy) / size;
            const float disc = 1.0f - smoothstep(0.375f, 0.395f, rr);
            // O verde do radar: vermelho e azul abaixo de ~60% do verde (as letrinhas do radar chegam a 57%; o verde-oliva
            // de uma granada passa de 62%). Os quase pretos são o fundo também.
            const float q = std::max(r, b) / std::max(g, 1.0f);
            const float key = g >= 12.0f ? smoothstep(0.56f, 0.65f, q) : 1.0f;
            const float dark = smoothstep(8.0f, 22.0f, std::max({r, g, b}));
            const float a = src_a * disc * key * dark;
            // Um tom só: a luminância sem o verde refletido, levantada (as armas pretas aparecem no vidro escuro).
            const float gg = std::min(g, std::max(r, b));
            float lum = (0.30f * r + 0.59f * gg + 0.11f * b) / 255.0f;
            lum = 0.30f + 0.70f * std::min(1.0f, lum * 1.6f);
            uint8_t* o = out.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
            o[0] = o[1] = o[2] = uint8_t(std::lround(std::clamp(lum, 0.0f, 1.0f) * 255.0f));
            o[3] = uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 128.0f));
        }
    }
    return out;
}

WheelView make_wheel_view(const ::WeaponWheel& w, const std::wstring& key, const std::string& glyph) {
    WheelView v;
    for (int i = 0; i < w.count(false); ++i) {
        v.weapons.push_back(view_slot(w.slots(false)[i]));
        if (v.weapons.back().current) v.in_hand = v.weapons.back().name;
    }
    for (int i = 0; i < w.count(true); ++i) {
        v.gadgets.push_back(view_slot(w.slots(true)[i]));
        if (v.gadgets.back().current) v.in_hand = v.gadgets.back().name;
    }
    v.outer_slots = weapon_wheel_ring_slots(w.count(false), false);
    v.inner_slots = weapon_wheel_ring_slots(w.count(true), true);
    v.hover_ring = w.hovered().ring;
    v.hover_index = w.hovered().index;
    v.sel_x = w.sel_x();
    v.sel_y = w.sel_y();
    v.shown = w.shown();
    v.key = key;
    v.glyph = glyph;
    return v;
}

void build_weapon_wheel(DrawList& back, ImageList& icons, DrawList& front, const Canvas& cv, const Tokens& tk,
                        const Measure& m, const WheelView& v) {
    const float a = ease_out(v.shown);
    if (a <= 0.0f) return;
    const Frame f{tk, cv, m, 0.0f};
    const WheelGeometry& g = wheel_geometry();
    const float k = 0.94f + 0.06f * a;  // a roda cresce um pouco ao abrir
    const float cx = g.cx, cy = g.cy;
    const auto at = [&](float r, float ang, float* x, float* y) {
        *x = cx + r * std::sin(ang);
        *y = cy - r * std::cos(ang);
    };
    // O fundo: a cena escurece em volta (a vinheta), mais no meio, atrás da roda.
    back.radial(Rect{cv.left, cv.top, cv.right, cv.bottom}, rgb(0x0b0806, 0.30f * a), tk.color.vignette.with_alpha(0.85f * a));
    const float back_r = (g.outer1 + 60.0f) * k;
    back.radial(Rect{cx - back_r, cy - back_r, cx + back_r, cy + back_r}, rgb(0x0b0806, 0.62f * a), rgb(0x0b0806, 0.0f));

    const WheelViewSlot* hovered = nullptr;  // o apontado com arma (o miolo)
    bool hover_names = true;                 // o apontado mostra o nome no espaço (senão o miolo o mostra maior)

    const auto ring = [&](const std::vector<WheelViewSlot>& slots, int ring_id, int count) {
        const bool inner = ring_id == 1;
        const int n = int(slots.size());
        // Os espaços: vidro escuro quente; os vazios mais apagados.
        for (int i = 0; i < count; ++i) {
            const WheelSlotGeom s = wheel_slot_geometry(g, ring_id, i, count, k);
            const bool owned = i < n;
            const bool usable = owned && slots[size_t(i)].usable;
            const bool hover = ring_id == v.hover_ring && i == v.hover_index;
            Color fill = !owned ? rgb(0x120d09, 0.40f) : usable ? rgb(0x18110b, 0.80f) : rgb(0x0f0b08, 0.64f);
            if (hover) fill = !owned ? rgb(0x18110b, 0.55f) : usable ? rgb(0x261b12, 0.92f) : rgb(0x1c150f, 0.86f);
            back.sector(cx, cy, s.r0, s.r1, s.a0, s.a1, fill.with_alpha(a));
            // A linha fina da borda de fora.
            const Color edge = !owned ? tk.color.hairline : usable ? tk.color.hairline_2 : tk.color.hairline;
            if (!(hover && usable)) {
                const Color he = hover ? tk.color.text_2.with_alpha(owned ? 0.55f : 0.32f) : edge;
                back.sector(cx, cy, s.r1 - (hover ? 2.0f : 1.0f), s.r1, s.a0, s.a1, he.with_alpha(a));
            }
            if (hover && owned) {
                hovered = &slots[size_t(i)];
                hover_names = s.names;
            }
            if (hover && usable) {
                // O apontado: o ouro sobe para a borda, a borda de ouro (3 px, por dentro), os risquinhos dos cantos e o
                // brilho de brasa além dela.
                back.sector_gradient(cx, cy, s.r0, s.r1, s.a0, s.a1, tk.color.accent.with_alpha(0.0f),
                                     tk.color.accent.with_alpha(0.12f * a));
                back.sector(cx, cy, s.r1 - 3.0f, s.r1, s.a0, s.a1, tk.color.accent.with_alpha(a));
                const float tick = inner ? 12.0f : 18.0f;
                for (const float edge_a : {s.a0, s.a1}) {
                    float x0, y0, x1, y1;
                    at(s.r1 - 3.0f, edge_a, &x0, &y0);
                    at(s.r1 - 3.0f - tick, edge_a, &x1, &y1);
                    back.line(x0, y0, x1, y1, tk.color.accent.with_alpha(0.75f * a), 1.5f);
                }
                back.sector_gradient(cx, cy, s.r1, s.r1 + 30.0f * k, s.a0, s.a1, tk.color.ember.with_alpha(0.30f * a),
                                     tk.color.ember.with_alpha(0.0f));
            }
        }
        // O conteúdo: o ícone, o nome e a munição, de pé no meio do espaço.
        for (int i = 0; i < std::min(n, count); ++i) {
            const WheelViewSlot& sl = slots[size_t(i)];
            const WheelSlotGeom s = wheel_slot_geometry(g, ring_id, i, count, k);
            const bool hover = ring_id == v.hover_ring && i == v.hover_index;
            if (sl.current) {
                // A da mão: o losango de ouro logo além da borda de fora (no anel de dentro, na folga entre os anéis).
                const float dr = inner ? 4.0f : 5.0f;
                float dx, dy;
                at(s.r1 + dr + 1.0f, s.mid, &dx, &dy);
                front.diamond(Rect{dx - dr, dy - dr, dx + dr, dy + dr}, tk.color.accent.with_alpha(a));
            }
            // A largura de uma linha de texto de pé no espaço: a corda em cima e embaixo, a espessura do anel dos lados.
            const float sn = std::fabs(std::sin(s.mid)), cs = std::fabs(std::cos(s.mid));
            const float chord = 2.0f * s.rm * std::sin(std::max(0.02f, (s.a1 - s.a0) * 0.5f));
            const float width = std::min({chord / std::max(cs, 0.05f), (s.r1 - s.r0) / std::max(sn, 0.05f), 210.0f * k}) * 0.86f;
            const Color nc = hover && sl.usable ? tk.color.accent_hi : sl.usable ? tk.color.text : tk.color.disabled;
            const Color ac = sl.usable ? (hover ? tk.color.accent : tk.color.text_2) : tk.color.disabled;
            TextStyle ns = tk.type.label;
            ns.size = (inner ? 14.0f : 17.0f) * k;
            ns.weight = 600;
            ns.caps = true;
            ns.tracking = 0.10f;
            ns = fit_style(f, sl.name, ns, width, 10.0f * k);
            TextStyle as = tk.type.value;
            as.size = (inner ? 13.0f : 15.0f) * k;
            const float name_h = s.names ? (inner ? 17.0f : 20.0f) * k : 0.0f;
            const float ammo_h = (inner ? 14.0f : 16.0f) * k;
            if (sl.icon != 0) {
                // A imagem do dossiê: só o disco do radar (0,1 a 0,9 da textura), onde a arma está; ela ocupa uns 85% dele.
                const float S = std::clamp(s.box * (s.names ? 0.66f : 0.86f), 36.0f * k, 96.0f * k);
                const float vis = S * 0.86f;
                const float total = vis + 4.0f * k + name_h + ammo_h;
                const float top = s.py - total * 0.5f;
                const float icy = top + vis * 0.5f;
                ImageCmd ic;
                ic.hash = sl.icon;
                ic.port = true;
                ic.r = Rect{s.px - S * 0.5f, icy - S * 0.5f, s.px + S * 0.5f, icy + S * 0.5f};
                ic.uv = Rect{0.1f, 0.1f, 0.9f, 0.9f};
                ic.alpha = a * (sl.usable ? 1.0f : 0.55f);
                ic.tint = hover && sl.usable ? tk.color.accent_hi
                          : sl.usable        ? (sl.current ? tk.color.text : tk.color.text_2)
                                             : tk.color.disabled;
                icons.push_back(ic);
                float y = top + vis + 4.0f * k;
                if (s.names) {
                    front.text(Rect{s.px - width * 0.5f, y, s.px + width * 0.5f, y + name_h}, sl.name, ns, nc.with_alpha(a),
                               Align::Centre);
                    y += name_h;
                }
                front.text(Rect{s.px - width * 0.5f, y, s.px + width * 0.5f, y + ammo_h}, sl.ammo, as, ac.with_alpha(a),
                           Align::Centre);
            } else {
                // Sem imagem (o soco, as armas que o dossiê não tem): o nome e a munição no meio, como texto.
                front.text(Rect{s.px - width * 0.5f, s.py - 20.0f * k, s.px + width * 0.5f, s.py}, sl.name, ns,
                           nc.with_alpha(a), Align::Centre);
                front.text(Rect{s.px - width * 0.5f, s.py + 1.0f * k, s.px + width * 0.5f, s.py + 1.0f * k + ammo_h},
                           sl.ammo, as, ac.with_alpha(a), Align::Centre);
            }
        }
    };
    ring(v.weapons, 2, std::max(v.outer_slots, int(v.weapons.size())));
    ring(v.gadgets, 1, std::max(v.inner_slots, int(v.gadgets.size())));

    // O miolo: o apontado em detalhe, ou a arma da mão.
    const float hole = g.hole * k;
    back.ellipse(Rect{cx - hole, cy - hole, cx + hole, cy + hole}, rgb(0x0b0806, 0.88f * a));
    back.ellipse(Rect{cx - hole, cy - hole, cx + hole, cy + hole}, tk.color.hairline_2.with_alpha(a), 1.0f);
    const float tw = hole * 1.62f;
    if (hovered != nullptr) {
        const WheelViewSlot* hs = hovered;
        TextStyle ts = tk.type.dialog_title;
        ts.size = (hover_names ? 22.0f : 24.0f) * k;
        ts = fit_style(f, hs->name, ts, tw, 12.0f);
        front.text(Rect{cx - tw * 0.5f, cy - 50.0f * k, cx + tw * 0.5f, cy - 22.0f * k}, hs->name, ts,
                   (hs->usable ? tk.color.accent : tk.color.dim).with_alpha(a), Align::Centre);
        TextStyle big = tk.type.stat;
        big.size = 40.0f * k;
        front.text(Rect{cx - tw * 0.5f, cy - 18.0f * k, cx + tw * 0.5f, cy + 24.0f * k}, hs->ammo, big,
                   (hs->usable ? tk.color.text : tk.color.disabled).with_alpha(a), Align::Centre);
        std::wstring sub = hs->mode;
        if (!hs->usable) sub = pc(PcText::WheelNoAmmo);
        else if (hs->current) sub = pc(PcText::WheelInHand);
        TextStyle ms = tk.type.section;
        ms = fit_style(f, sub, ms, tw, 9.0f);
        front.text(Rect{cx - tw * 0.5f, cy + 30.0f * k, cx + tw * 0.5f, cy + 50.0f * k}, sub, ms,
                   tk.color.text_2.with_alpha(a), Align::Centre);
    } else {
        TextStyle ts = tk.type.section;
        ts = fit_style(f, v.in_hand, ts, tw, 9.0f);
        front.text(Rect{cx - tw * 0.5f, cy - 24.0f * k, cx + tw * 0.5f, cy - 4.0f * k}, v.in_hand, ts,
                   tk.color.text_2.with_alpha(a), Align::Centre);
        const std::wstring keep = pc(PcText::WheelKeep);
        TextStyle ks = tk.type.meta;
        ks.size = 16.0f;
        ks = fit_style(f, keep, ks, tw, 10.0f);
        front.text(Rect{cx - tw * 0.5f, cy + 4.0f * k, cx + tw * 0.5f, cy + 24.0f * k}, keep, ks,
                   tk.color.dim.with_alpha(a), Align::Centre);
    }

    // O vetor de escolha: um losango de brasa e a linha fina do miolo até ele.
    const float sr = std::sqrt(v.sel_x * v.sel_x + v.sel_y * v.sel_y);
    if (sr > 0.02f) {
        const float R = g.outer1 * k;
        const float sx = cx + v.sel_x * R, sy = cy - v.sel_y * R;
        if (sr * R > hole) {
            front.line(cx + v.sel_x / sr * hole, cy - v.sel_y / sr * hole, sx, sy, tk.color.ember.with_alpha(0.35f * a),
                       1.5f);
        }
        front.diamond(Rect{sx - 7.0f, sy - 7.0f, sx + 7.0f, sy + 7.0f}, tk.color.ember.with_alpha(0.9f * a));
    }

    // Embaixo, numa pílula escura (o que a cena tiver atrás não se confunde com a roda): a tecla da roda e "Soltar para
    // equipar".
    const std::wstring release = pc(PcText::WheelRelease);
    const float by = cy + (g.outer1 + 30.0f) * k + 34.0f;
    const float iw = prompt_icon_width(f, v.key, v.glyph);
    const float lw = text_width(f, release, tk.type.prompt);
    const float pw = iw + 12.0f + lw;
    const float ph = tk.space.keycap_h + 14.0f;
    back.fill(Rect{cx - pw * 0.5f - 16.0f, by - ph * 0.5f, cx + pw * 0.5f + 16.0f, by + ph * 0.5f},
              rgb(0x0b0806, 0.72f * a), ph * 0.5f);
    float x = cx - pw * 0.5f;
    x += draw_prompt_icon(front, f, x, by, v.key, v.glyph, a) + 12.0f;
    front.text(Rect{x, by - 14.0f, x + lw + 4.0f, by + 14.0f}, release, tk.type.prompt, tk.color.text_2.with_alpha(a));
}

}  // namespace nf::ui
