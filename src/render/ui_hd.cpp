// Camada 2D em alta definição: a parte sem GPU (configuração, textos, âncoras, substitutas, inventário).
// Ver ui_hd.h.

#include "render/ui_hd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "core/json.h"
#include "platform/psi_pad.h"

namespace nf {

namespace {


bool parse_hex(const std::string& s, uint32_t& out) {
    size_t i = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (i >= s.size() || s.size() - i > 8) return false;
    uint32_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(uint8_t(s[s.size() - n + i])) != suffix[i]) return false;
    }
    return true;
}

bool parse_font(const JVal& v, UiHdFont& f, std::string* error) {
    if (v.type != JVal::Obj) {
        if (error != nullptr) *error = "fonte: esperava um objeto";
        return false;
    }
    if (const JVal* x = v.get("family"); x != nullptr && x->type == JVal::Str) f.family = widen_utf8(x->s);
    if (const JVal* x = v.get("family_macos"); x != nullptr) {
        // Um nome ou uma lista de nomes (a primeira família que existir no macOS).
        std::vector<std::wstring> names;
        if (x->type == JVal::Str) names.push_back(widen_utf8(x->s));
        else if (x->type == JVal::Arr) {
            for (const JVal& n : x->a) {
                if (n.type != JVal::Str) {
                    if (error != nullptr) *error = "family_macos: esperava um nome ou uma lista de nomes";
                    return false;
                }
                names.push_back(widen_utf8(n.s));
            }
        } else {
            if (error != nullptr) *error = "family_macos: esperava um nome ou uma lista de nomes";
            return false;
        }
        f.family_macos = std::move(names);
    }
    if (const JVal* x = v.get("weight"); x != nullptr && x->type == JVal::Num) f.weight = std::clamp(int(x->n), 1, 999);
    if (const JVal* x = v.get("stretch"); x != nullptr && x->type == JVal::Num) f.stretch = std::clamp(int(x->n), 1, 9);
    if (const JVal* x = v.get("italic"); x != nullptr && x->type == JVal::Bool) f.italic = x->b;
    if (const JVal* x = v.get("size"); x != nullptr && x->type == JVal::Num) f.size = std::clamp(float(x->n), 0.1f, 10.0f);
    if (const JVal* x = v.get("fit_min"); x != nullptr && x->type == JVal::Num) f.fit_min = std::clamp(float(x->n), 0.1f, 1.0f);
    if (const JVal* x = v.get("spacing"); x != nullptr && x->type == JVal::Num) f.spacing = std::clamp(float(x->n), -0.5f, 1.0f);
    return true;
}

// ---------------------------------------------------------------- textos

constexpr float kEps = 0.01f;

bool is_text_quad(const Psi2DQuad& q) { return q.origin.kind == Psi2DKind::Glyph || q.origin.kind == Psi2DKind::Symbol; }

// Onde cada caractere contado por __Font_DrawText (i_133, a partir de 1) começa no texto: o '~' consome o
// caractere seguinte sem contar.
std::vector<uint32_t> char_offsets(const char* text, uint32_t max_index) {
    std::vector<uint32_t> off(1, 0);
    if (text == nullptr) return off;
    uint32_t p = 0;
    while (text[p] != 0 && off.size() <= max_index) {
        off.push_back(p);
        p += (text[p] == '~' && text[p + 1] != 0) ? 2 : 1;
    }
    return off;
}

void quad_rgba(const Psi2DQuad& q, uint8_t out[4]) { std::memcpy(out, q.rgba, 4); }

float mapped_x(const Psi2DViewport& vp, float x) { return vp.left + x * vp.scale_x; }
float mapped_y(const Psi2DViewport& vp, float y) { return vp.top + y * vp.scale_y; }

// Tamanho de rasterização: até 12,5% acima do pedido, para não refazer a cada pixel de animação.
int32_t quantize_size(float px) {
    if (!(px > 0.0f)) return 4;
    px = std::min(px, 4096.0f);
    const float step = std::max(4.0f, std::exp2(std::floor(std::log2(px))) / 8.0f);
    return std::clamp(int32_t(std::ceil(px / step) * step), 4, 4096);
}

}  // namespace

// ---------------------------------------------------------------- configuração

UiHdConfig::UiHdConfig() {
    // Padrões com fontes que vêm com o Windows (o map.json do repositório diz o mesmo): a Bahnschrift tem o
    // desenho quadrado, de cantos arredondados, das três fontes do jogo; a Medium é negrito e itálica.
    fonts[1].family = L"Bahnschrift";
    fonts[1].weight = 600;
    fonts[2].family = L"Bahnschrift";
    fonts[2].weight = 400;
    fonts[3].family = L"Bahnschrift";
    fonts[3].weight = 700;
    fonts[3].italic = true;
}

bool UiHdConfig::anchor_skipped(int pane, int sprite) const {
    for (const int p : anchor_skip_panes) {
        if (p == pane) return true;
    }
    for (const auto& [p, s] : anchor_skip_sprites) {
        if (p == pane && s == sprite) return true;
    }
    return false;
}

int UiHdConfig::find_image(uint32_t texture, const float texel[4], int pane, int sprite) const {
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < images.size(); ++i) {
            const UiHdImageKey& k = images[i].key;
            if (images[i].prompt_only) continue;
            const bool identity = k.pane != UiHdImageKey::kAny || k.sprite != UiHdImageKey::kAny;
            if (identity != (pass == 0)) continue;
            if (k.pane != UiHdImageKey::kAny && k.pane != pane) continue;
            if (k.sprite != UiHdImageKey::kAny && k.sprite != sprite) continue;
            if (k.texture != 0 && k.texture != texture) continue;
            if (k.has_rect) {
                if (texel == nullptr) continue;
                // O elemento dentro do retângulo: até o centro do último texel ("inclusive") ou até a borda
                // dele ("exclusive").
                const float end_u = k.rect[0] + k.rect[2] - (images[i].exclusive[0] ? 0.0f : 1.0f);
                const float end_v = k.rect[1] + k.rect[3] - (images[i].exclusive[1] ? 0.0f : 1.0f);
                if (texel[0] < k.rect[0] - kEps || texel[2] > end_u + kEps) continue;
                if (texel[1] < k.rect[1] - kEps || texel[3] > end_v + kEps) continue;
            }
            return int(i);
        }
    }
    return -1;
}

bool ui_hd_parse_config(const std::string& json, UiHdConfig& out, std::string* error) {
    JVal root;
    if (!json_parse(json, root, error)) return false;
    const auto bad = [error](const std::string& what) {
        if (error != nullptr) *error = what;
        return false;
    };
    if (root.type != JVal::Obj) return bad("a raiz deve ser um objeto");
    UiHdConfig c;
    if (const JVal* v = root.get("anchor"); v != nullptr) {
        if (v->type != JVal::Str) return bad("anchor: esperava \"centre\" ou \"edges\"");
        if (v->s == "edges") c.anchor = UiHdAnchor::Edges;
        else if (v->s == "centre" || v->s == "center") c.anchor = UiHdAnchor::Centre;
        else return bad("anchor: esperava \"centre\" ou \"edges\"");
    }
    if (const JVal* v = root.get("text"); v != nullptr && v->type == JVal::Bool) c.text = v->b;
    if (const JVal* v = root.get("anchor_skip"); v != nullptr) {
        // [8, [0, 7]]: um número é uma pane inteira; um par, pane e sprite.
        if (v->type != JVal::Arr) return bad("anchor_skip: lista de panes e de pares [pane, sprite]");
        c.anchor_skip_panes.clear();
        c.anchor_skip_sprites.clear();
        for (const JVal& x : v->a) {
            if (x.type == JVal::Num) {
                c.anchor_skip_panes.push_back(int(x.n));
            } else if (x.type == JVal::Arr && x.a.size() == 2 && x.a[0].type == JVal::Num && x.a[1].type == JVal::Num) {
                c.anchor_skip_sprites.emplace_back(int(x.a[0].n), int(x.a[1].n));
            } else {
                return bad("anchor_skip: lista de panes e de pares [pane, sprite]");
            }
        }
    }
    if (const JVal* fonts = root.get("fonts"); fonts != nullptr) {
        if (fonts->type != JVal::Obj) return bad("fonts: esperava um objeto");
        for (const auto& [key, f] : fonts->o) {
            const int k = std::atoi(key.c_str());
            if (k < 1 || k > 3) return bad("fonts: as chaves são \"1\", \"2\" e \"3\"");
            if (!parse_font(f, c.fonts[k], error)) return false;
        }
    }
    if (const JVal* images = root.get("images"); images != nullptr) {
        if (images->type != JVal::Arr) return bad("images: esperava uma lista");
        for (size_t i = 0; i < images->a.size(); ++i) {
            const JVal& v = images->a[i];
            const std::string where = "images[" + std::to_string(i) + "]: ";
            if (v.type != JVal::Obj) return bad(where + "esperava um objeto");
            UiHdImageEntry e;
            if (const JVal* x = v.get("name"); x != nullptr && x->type == JVal::Str) e.name = x->s;
            if (const JVal* x = v.get("texture"); x != nullptr) {
                if (x->type != JVal::Str || !parse_hex(x->s, e.key.texture)) return bad(where + "texture: hashcode em hexadecimal");
            }
            if (const JVal* x = v.get("rect"); x != nullptr) {
                if (x->type != JVal::Arr || x->a.size() != 4) return bad(where + "rect: [u, v, largura, altura]");
                for (int k = 0; k < 4; ++k) {
                    if (x->a[size_t(k)].type != JVal::Num) return bad(where + "rect: números");
                    e.key.rect[k] = float(x->a[size_t(k)].n);
                }
                if (e.key.rect[2] < 1.0f || e.key.rect[3] < 1.0f) return bad(where + "rect: largura e altura >= 1");
                e.key.has_rect = true;
            }
            if (const JVal* x = v.get("pane"); x != nullptr) {
                if (x->type != JVal::Num) return bad(where + "pane: número (0..21, -1 a mira)");
                e.key.pane = int(x->n);
            }
            if (const JVal* x = v.get("sprite"); x != nullptr) {
                if (x->type != JVal::Num) return bad(where + "sprite: número");
                e.key.sprite = int(x->n);
            }
            if (e.key.texture == 0 && e.key.pane == UiHdImageKey::kAny && e.key.sprite == UiHdImageKey::kAny) {
                return bad(where + "a chave precisa de texture ou de pane/sprite");
            }
            if (const JVal* x = v.get("extent"); x != nullptr) {
                // Uma palavra para os dois eixos ou [u, v].
                const JVal* axes[2] = {x, x};
                if (x->type == JVal::Arr && x->a.size() == 2) axes[0] = &x->a[0], axes[1] = &x->a[1];
                for (int k = 0; k < 2; ++k) {
                    if (axes[k]->type != JVal::Str || (axes[k]->s != "inclusive" && axes[k]->s != "exclusive")) {
                        return bad(where + "extent: \"inclusive\", \"exclusive\" ou [u, v]");
                    }
                    e.exclusive[k] = axes[k]->s == "exclusive";
                }
            }
            const JVal* file = v.get("file");
            if (file == nullptr || file->type != JVal::Str || file->s.empty()) return bad(where + "file: o arquivo da substituta");
            e.file = file->s;
            if (ends_with(e.file, ".svg")) e.type = UiHdAssetType::Svg;
            else if (ends_with(e.file, ".png")) e.type = UiHdAssetType::Png;
            else return bad(where + "file: .svg ou .png");
            c.images.push_back(std::move(e));
        }
    }
    if (const JVal* prompts = root.get("prompts"); prompts != nullptr) {
        // {"ps2": {"A": "svg/...", ...}, "xbox": {"A": "svg/...", "LB": ...}}: entradas só dos símbolos.
        if (prompts->type != JVal::Obj) return bad("prompts: esperava um objeto");
        for (const auto& [set, table] : prompts->o) {
            if (set != "ps2" && set != "xbox") return bad("prompts: os conjuntos são \"ps2\" e \"xbox\"");
            if (table.type != JVal::Obj) return bad("prompts." + set + ": esperava um objeto");
            std::map<std::string, int>& dst = set == "ps2" ? c.prompt_ps2 : c.prompt_xbox;
            for (const auto& [key, file] : table.o) {
                if (file.type != JVal::Str || !(ends_with(file.s, ".svg") || ends_with(file.s, ".png"))) {
                    return bad("prompts." + set + "." + key + ": .svg ou .png");
                }
                UiHdImageEntry e;
                e.name = "símbolo " + set + " " + key;
                e.file = file.s;
                e.type = ends_with(file.s, ".svg") ? UiHdAssetType::Svg : UiHdAssetType::Png;
                e.prompt_only = true;
                dst[key] = int(c.images.size());
                c.images.push_back(std::move(e));
            }
        }
    }
    out = std::move(c);
    return true;
}

// ---------------------------------------------------------------- peças

wchar_t ui_hd_char(uint8_t c) {
    // Windows-1252 de 0x80 a 0x9f (0 onde não há caractere).
    static const uint16_t k1252[32] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                       0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                       0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                       0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    if (c == 0xa0) return L' ';
    if (c >= 0x80 && c < 0xa0) return k1252[c - 0x80] != 0 ? wchar_t(k1252[c - 0x80]) : L'?';
    return wchar_t(c);
}

std::vector<UiHdRun> ui_hd_group_runs(const Psi2DQuad* quads, size_t count) {
    std::vector<UiHdRun> runs;
    bool open = false;
    for (size_t i = 0; i < count; ++i) {
        const Psi2DQuad& q = quads[i];
        if (!is_text_quad(q)) {
            open = false;
            continue;
        }
        const Psi2DOrigin& o = q.origin;
        if (open) {
            UiHdRun& r = runs.back();
            const Psi2DOrigin& prev = quads[r.first + r.count - 1].origin;
            // Sem o contador de chamadas (origens antigas): mesma passada e o índice do caractere crescendo.
            const bool same = o.run != 0 ? o.run == r.run
                                         : (o.pass == r.pass && o.char_index >= prev.char_index);
            if (same && o.text == r.text && o.sprite == r.sprite) {
                ++r.count;
                continue;
            }
        }
        UiHdRun r;
        r.first = uint32_t(i);
        r.count = 1;
        r.run = o.run;
        r.pass = o.pass;
        r.sprite = o.sprite;
        r.text = o.text;
        runs.push_back(r);
        open = true;
    }
    return runs;
}

UiHdRunLines ui_hd_run_lines(const Psi2DQuad* quads, const UiHdRun& run) {
    UiHdRunLines out;
    if (run.count == 0) return out;
    uint32_t max_index = 0;
    for (uint32_t i = 0; i < run.count; ++i) max_index = std::max<uint32_t>(max_index, quads[run.first + i].origin.char_index);
    const std::vector<uint32_t> off = char_offsets(run.text, max_index);
    const auto offset_of = [&](uint16_t ci, uint32_t& o) {
        if (run.text == nullptr || ci == 0 || ci >= off.size()) return false;
        o = off[ci];
        return true;
    };

    // Os caracteres desenhados de cada linha (uma linha muda a linha de base da caneta).
    struct Drawn {
        uint32_t quad;     // primeiro quadrado do caractere (o ™ desenha dois)
        uint32_t offset;   // no texto (UINT32_MAX sem texto)
        bool symbol;
        float x0, x1;
    };
    std::vector<std::vector<Drawn>> lines;
    std::vector<float> baselines;
    for (uint32_t i = 0; i < run.count; ++i) {
        const uint32_t qi = run.first + i;
        const Psi2DQuad& q = quads[qi];
        const Psi2DOrigin& o = q.origin;
        if (i == 0) {
            out.first_x = std::min(q.x0, q.x1);
            out.first_baseline = o.baseline;
        }
        if (lines.empty() || std::fabs(baselines.back() - o.baseline) > kEps) {
            lines.emplace_back();
            baselines.push_back(o.baseline);
        }
        std::vector<Drawn>& line = lines.back();
        const float x0 = std::min(q.x0, q.x1), x1 = std::max(q.x0, q.x1);
        if (!line.empty() && o.char_index != 0 && quads[line.back().quad].origin.char_index == o.char_index &&
            !line.back().symbol && o.kind == Psi2DKind::Glyph) {
            line.back().x1 = std::max(line.back().x1, x1);  // a segunda metade do ™
            continue;
        }
        Drawn d{qi, UINT32_MAX, o.kind == Psi2DKind::Symbol, x0, x1};
        uint32_t at;
        if (offset_of(o.char_index, at)) d.offset = at;
        if (d.symbol) out.symbols.push_back(qi);
        line.push_back(d);
    }

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::vector<Drawn>& drawn = lines[li];
        bool has_symbol = false;
        for (const Drawn& d : drawn) has_symbol |= d.symbol;
        UiHdLine cur;
        bool cur_open = false;
        const auto close = [&]() {
            if (cur_open && !cur.text.empty()) {
                cur.pinned = has_symbol;
                out.lines.push_back(cur);
            }
            cur = UiHdLine{};
            cur.baseline = baselines[li];
            cur_open = false;
        };
        cur.baseline = baselines[li];
        const auto append = [&](wchar_t ch, const uint8_t rgba[4]) {
            if (!cur.spans.empty() && std::memcmp(cur.spans.back().rgba, rgba, 4) == 0 &&
                cur.spans.back().start + cur.spans.back().length == cur.text.size()) {
                ++cur.spans.back().length;
            } else {
                UiHdSpan s;
                s.start = uint32_t(cur.text.size());
                s.length = 1;
                std::memcpy(s.rgba, rgba, 4);
                cur.spans.push_back(s);
            }
            cur.text.push_back(ch);
        };
        for (size_t k = 0; k < drawn.size(); ++k) {
            const Drawn& d = drawn[k];
            if (d.symbol) {
                close();
                continue;
            }
            const Psi2DQuad& q = quads[d.quad];
            uint8_t rgba[4];
            quad_rgba(q, rgba);
            if (!cur_open) {
                cur_open = true;
                cur.x0 = d.x0;
                cur.x1 = d.x1;
            }
            // Os espaços entre o caractere anterior e este (pelo texto; sem texto, pela distância).
            if (k > 0 && !drawn[k - 1].symbol && !cur.text.empty()) {
                const Drawn& prev = drawn[k - 1];
                if (prev.offset != UINT32_MAX && d.offset != UINT32_MAX && d.offset > prev.offset) {
                    uint32_t p = prev.offset + ((uint8_t(run.text[prev.offset]) == '~') ? 2 : 1);
                    for (; p < d.offset; ++p) {
                        const uint8_t c = uint8_t(run.text[p]);
                        if (c == ' ' || c == 0xa0 || c == '\t') append(L' ', cur.spans.back().rgba);
                    }
                } else if (d.offset == UINT32_MAX) {
                    const float gap = d.x0 - prev.x1;
                    const float h = std::fabs(q.y1 - q.y0);
                    if (gap > 0.3f * h) append(L' ', cur.spans.back().rgba);
                }
            }
            const uint8_t c = d.offset != UINT32_MAX ? uint8_t(run.text[d.offset]) : q.origin.glyph;
            append(ui_hd_char(c), rgba);
            cur.x0 = std::min(cur.x0, d.x0);
            cur.x1 = std::max(cur.x1, d.x1);
        }
        close();
    }
    return out;
}

std::vector<uint32_t> ui_hd_anchor_groups(const std::vector<std::array<float, 4>>& rects, const std::vector<int>& panes,
                                          float gap) {
    const size_t n = rects.size();
    std::vector<uint32_t> parent(n);
    for (size_t i = 0; i < n; ++i) parent[i] = uint32_t(i);
    const auto find = [&](uint32_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    for (size_t i = 0; i < n; ++i) {
        if (panes[i] == UiHdImageKey::kAny) continue;
        for (size_t j = i + 1; j < n; ++j) {
            if (panes[j] != panes[i]) continue;
            const auto& a = rects[i];
            const auto& b = rects[j];
            if (a[0] - gap > b[2] || b[0] - gap > a[2] || a[1] - gap > b[3] || b[1] - gap > a[3]) continue;
            parent[find(uint32_t(i))] = find(uint32_t(j));
        }
    }
    // Numera os grupos na ordem em que aparecem.
    std::vector<uint32_t> group(n), id(n, UINT32_MAX);
    uint32_t next = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = find(uint32_t(i));
        if (id[r] == UINT32_MAX) id[r] = next++;
        group[i] = id[r];
    }
    return group;
}

float ui_hd_anchor_shift(UiHdAnchor anchor, const Psi2DViewport& vp, int window_width, float x0, float x1) {
    if (anchor != UiHdAnchor::Edges || vp.left <= 0.0f || window_width <= 0) return 0.0f;
    const float centre = (x0 + x1) * 0.5f;
    if (centre < kPs2ScreenWidth / 3.0f) return -vp.left;
    if (centre > kPs2ScreenWidth * 2.0f / 3.0f) return float(window_width) - (vp.left + vp.width);
    return 0.0f;
}

float ui_hd_anchor_shift_y(UiHdAnchor anchor, const Psi2DViewport& vp, int window_height, float y0, float y1) {
    if (anchor != UiHdAnchor::Edges || vp.top <= 0.0f || window_height <= 0) return 0.0f;
    const float centre = (y0 + y1) * 0.5f;
    if (centre < kPs2ScreenHeight / 3.0f) return -vp.top;
    if (centre > kPs2ScreenHeight * 2.0f / 3.0f) return float(window_height) - (vp.top + vp.height);
    return 0.0f;
}

bool ui_hd_texel_span(const Psi2DQuad& q, int w, int h, float out[4]) {
    if (w <= 0 || h <= 0) return false;
    // ST = (texel + 0.5) / tamanho fora das fontes (psiDrawSprites): o centro do texel; nas fontes, o canto.
    const float half = q.linear ? 0.5f : 0.0f;
    out[0] = q.u0 * float(w) - half;
    out[1] = q.v0 * float(h) - half;
    out[2] = q.u1 * float(w) - half;
    out[3] = q.v1 * float(h) - half;
    return true;
}

void ui_hd_asset_uv(const UiHdImageEntry& e, int tex_w, int tex_h, const float span[4], float uv[4]) {
    float k[4] = {0.0f, 0.0f, float(tex_w), float(tex_h)};
    if (e.key.has_rect) std::memcpy(k, e.key.rect, sizeof(k));
    for (int axis = 0; axis < 2; ++axis) {
        const float c0 = span[axis], c1 = span[axis + 2];
        if (std::fabs(c1 - c0) < 1e-3f) {  // uma coluna só, esticada: a substituta inteira
            uv[axis] = 0.0f;
            uv[axis + 2] = 1.0f;
            continue;
        }
        const float extent = e.exclusive[axis] ? k[axis + 2] : std::max(1.0f, k[axis + 2] - 1.0f);
        uv[axis] = (c0 - k[axis]) / extent;
        uv[axis + 2] = (c1 - k[axis]) / extent;
    }
}

// ---------------------------------------------------------------- símbolos de botão

namespace {
UiHdPromptSet g_prompt_setting = UiHdPromptSet::Auto;
UiHdPromptSet g_prompt_device = UiHdPromptSet::Keyboard;

// Os comandos do menu de um botão (platform/input_bindings.h, kBindCommands); nulo: nenhum.
const char* menu_command(UiHdPadButton b) {
    switch (b) {
        case UiHdPadButton::Cross: return "menu_select";
        case UiHdPadButton::Circle: return "menu_circle";
        case UiHdPadButton::Triangle: return "menu_back";
        case UiHdPadButton::Square: return "menu_square";
        case UiHdPadButton::Start: return "pause";
        default: return nullptr;
    }
}

// As entradas do controle do PC (kPadIn*) que fazem o botão do PS2 (psi_pad_convert ao contrário).
std::vector<uint8_t> pad_inputs(UiHdPadButton b, bool triggers_r1) {
    switch (b) {
        case UiHdPadButton::Cross: return {nf::kPadInA};
        case UiHdPadButton::Circle: return {nf::kPadInB};
        case UiHdPadButton::Square: return {nf::kPadInX};
        case UiHdPadButton::Triangle: return {nf::kPadInY};
        case UiHdPadButton::Start: return {nf::kPadInStart};
        case UiHdPadButton::Select: return {nf::kPadInBack};
        case UiHdPadButton::L1: return {triggers_r1 ? nf::kPadInLT : nf::kPadInLB};
        case UiHdPadButton::R1: return {triggers_r1 ? nf::kPadInRT : nf::kPadInRB};
        case UiHdPadButton::L2: return {triggers_r1 ? nf::kPadInLB : nf::kPadInLT};
        case UiHdPadButton::R2: return {triggers_r1 ? nf::kPadInRB : nf::kPadInRT};
        case UiHdPadButton::DpadV: return {nf::kPadInDpadUp, nf::kPadInDpadDown};
        case UiHdPadButton::DpadH: return {nf::kPadInDpadLeft, nf::kPadInDpadRight};
        case UiHdPadButton::Dpad: return {nf::kPadInDpadUp, nf::kPadInDpadDown, nf::kPadInDpadLeft, nf::kPadInDpadRight};
        // O analógico esquerdo: frente, esquerda, trás, direita (a ordem de "WASD").
        case UiHdPadButton::LStick: return {nf::kPadInLYPos, nf::kPadInLXNeg, nf::kPadInLYNeg, nf::kPadInLXPos};
        default: return {};
    }
}

// A primeira tecla (teclado ou mouse) de um comando, como texto; vazio sem tecla.
std::wstring first_key(const nf::Bindings& b, int command) {
    if (command < 0) return std::wstring();
    nf::InputCode in[nf::kMaxBinds];
    if (b.list(command, false, in, nf::kMaxBinds) <= 0) return std::wstring();
    return ui_hd_key_label(in[0]);
}

// Junta os textos das teclas (sem repetir): setas grudadas ("↑↓"), quatro letras sozinhas também ("WASD"),
// o resto com "/" ("Q/Z").
std::wstring join_keys(const std::vector<std::wstring>& keys) {
    std::vector<std::wstring> unique;
    for (const std::wstring& k : keys) {
        if (!k.empty() && std::find(unique.begin(), unique.end(), k) == unique.end()) unique.push_back(k);
    }
    // Os dois sentidos da roda (as armas no padrão: "Cycle Weapon") viram uma tecla só.
    const auto up = std::find(unique.begin(), unique.end(), ui_hd_key_label(nf::wheel_input(false)));
    const auto down = std::find(unique.begin(), unique.end(), ui_hd_key_label(nf::wheel_input(true)));
    if (up != unique.end() && down != unique.end()) {
        *up = L"Wheel";
        unique.erase(std::find(unique.begin(), unique.end(), ui_hd_key_label(nf::wheel_input(true))));
    }
    bool arrows = true, letters = unique.size() == 4;
    for (const std::wstring& k : unique) {
        arrows = arrows && k.size() == 1 && k[0] >= 0x2190 && k[0] <= 0x2193;
        letters = letters && k.size() == 1;
    }
    const bool single = arrows || letters;
    std::wstring out;
    for (size_t i = 0; i < unique.size(); ++i) {
        if (i > 0 && !single) out += L"/";
        out += unique[i];
    }
    return out;
}
}  // namespace

bool ui_hd_parse_prompt_set(const std::string& name, UiHdPromptSet* out) {
    std::string n;
    for (char ch : name) {
        if (ch != ' ' && ch != '\t') n.push_back(char(std::tolower(uint8_t(ch))));
    }
    if (n == "auto") *out = UiHdPromptSet::Auto;
    else if (n == "ps2" || n == "playstation") *out = UiHdPromptSet::Ps2;
    else if (n == "keyboard" || n == "teclado") *out = UiHdPromptSet::Keyboard;
    else if (n == "xbox" || n == "gamepad") *out = UiHdPromptSet::Xbox;
    else return false;
    return true;
}

const char* ui_hd_prompt_set_name(UiHdPromptSet set) {
    switch (set) {
        case UiHdPromptSet::Ps2: return "ps2";
        case UiHdPromptSet::Keyboard: return "keyboard";
        case UiHdPromptSet::Xbox: return "xbox";
        default: return "auto";
    }
}

void UiHd_SetPromptSetting(UiHdPromptSet set) { g_prompt_setting = set; }
UiHdPromptSet UiHd_PromptSetting() { return g_prompt_setting; }
void UiHd_SetPromptDevice(UiHdPromptSet device) {
    if (device != UiHdPromptSet::Auto) g_prompt_device = device;
}
UiHdPromptSet UiHd_PromptDevice() { return g_prompt_device; }

UiHdPromptSet ui_hd_prompt_effective(UiHdPromptSet setting, UiHdPromptSet device) {
    if (setting != UiHdPromptSet::Auto) return setting;
    return device == UiHdPromptSet::Auto ? UiHdPromptSet::Keyboard : device;
}

UiHdPromptSet ui_hd_prompt_effective() { return ui_hd_prompt_effective(g_prompt_setting, g_prompt_device); }

UiHdPadButton ui_hd_symbol_button(uint8_t code) {
    switch (code) {
        case 'A': return UiHdPadButton::Cross;
        case 'B': return UiHdPadButton::Triangle;
        case 'C': return UiHdPadButton::L2;
        case 'D': return UiHdPadButton::R2;
        case 'E': return UiHdPadButton::RStick;
        case 'F': return UiHdPadButton::LStick;
        case 'H': return UiHdPadButton::DpadH;
        case 'I': return UiHdPadButton::Dot;
        case 'J': return UiHdPadButton::Check;
        case 'L': return UiHdPadButton::L1;
        case 'R': return UiHdPadButton::R1;
        case 'S': return UiHdPadButton::Start;
        case 'T': return UiHdPadButton::Select;
        case 'V': return UiHdPadButton::DpadV;
        case 'W': return UiHdPadButton::Dpad;
        case 'X': return UiHdPadButton::Circle;
        case 'Y': return UiHdPadButton::Square;
        default: return UiHdPadButton::None;
    }
}

const char* ui_hd_xbox_name(uint8_t code, bool triggers_r1) {
    switch (ui_hd_symbol_button(code)) {
        case UiHdPadButton::Cross: return "A";
        case UiHdPadButton::Circle: return "B";
        case UiHdPadButton::Square: return "X";
        case UiHdPadButton::Triangle: return "Y";
        case UiHdPadButton::L1: return triggers_r1 ? "LT" : "LB";
        case UiHdPadButton::R1: return triggers_r1 ? "RT" : "RB";
        case UiHdPadButton::L2: return triggers_r1 ? "LB" : "LT";
        case UiHdPadButton::R2: return triggers_r1 ? "RB" : "RT";
        case UiHdPadButton::LStick: return "LS";
        case UiHdPadButton::RStick: return "RS";
        case UiHdPadButton::Start: return "Menu";
        case UiHdPadButton::Select: return "View";
        case UiHdPadButton::DpadV: return "DpadV";
        case UiHdPadButton::DpadH: return "DpadH";
        case UiHdPadButton::Dpad: return "Dpad";
        default: return nullptr;
    }
}

std::wstring ui_hd_key_label(nf::InputCode c) {
    // A roda, curta para caber num símbolo de tecla (a próxima/anterior arma no padrão).
    if (c.kind == nf::InputKind::Wheel) return c.code == 0 ? L"Wheel↑" : L"Wheel↓";
    if (c.kind == nf::InputKind::Key) {
        switch (c.code) {
            case 0x26: return L"↑";  // VK_UP
            case 0x28: return L"↓";  // VK_DOWN
            case 0x25: return L"←";  // VK_LEFT
            case 0x27: return L"→";  // VK_RIGHT
            default: break;
        }
    }
    return widen_utf8(nf::input_label(c, true));
}

float ui_hd_keycap_width(float label_w, float h) { return std::max(h, label_w + 0.6f * h); }

std::wstring ui_hd_prompt_keys(uint8_t code, bool game, const nf::Bindings& bindings, uint16_t style, bool triggers_r1) {
    const UiHdPadButton button = ui_hd_symbol_button(code);
    if (button == UiHdPadButton::None || button == UiHdPadButton::Check || button == UiHdPadButton::Dot) return std::wstring();
    if (button == UiHdPadButton::RStick) return L"Mouse";
    if (!game) {
        // Os comandos do menu.
        if (const char* cmd = menu_command(button)) return first_key(bindings, nf::find_bind_command(cmd));
        static const char* const kUp = "menu_up";
        static const char* const kDown = "menu_down";
        static const char* const kLeft = "menu_left";
        static const char* const kRight = "menu_right";
        std::vector<const char*> cmds;
        if (button == UiHdPadButton::DpadV) cmds = {kUp, kDown};
        if (button == UiHdPadButton::DpadH) cmds = {kLeft, kRight};
        if (button == UiHdPadButton::Dpad) cmds = {kUp, kDown, kLeft, kRight};
        if (!cmds.empty()) {
            std::vector<std::wstring> keys;
            for (const char* cmd : cmds) keys.push_back(first_key(bindings, nf::find_bind_command(cmd)));
            return join_keys(keys);
        }
    }
    // O comando do jogo que cada entrada do controle faz no estilo (o primeiro da tabela).
    std::vector<std::wstring> keys;
    for (const uint8_t in : pad_inputs(button, triggers_r1)) {
        int found = -1;
        for (int c = 0; c < nf::kBindCommandCount && found < 0; ++c) {
            const nf::BindCommand& cmd = nf::kBindCommands[c];
            if (cmd.group != nf::BindGroup::Game) continue;
            for (const nf::BindTarget& t : cmd.targets) {
                if (t.action < 0) continue;
                if ((psi_pad_style_inputs(style, t.action, t.sign, triggers_r1) >> in) & 1u) {
                    found = c;
                    break;
                }
            }
        }
        keys.push_back(first_key(bindings, found));
    }
    return join_keys(keys);
}

// ---------------------------------------------------------------- o quadro

namespace {

struct Builder {
    const Psi2DQuad* quads;
    size_t count;
    const UiHdConfig& config;
    const UiHdEnv& env;
    Psi2DViewport vp;
    float area[4];
    float window[4];
    std::vector<uint32_t> list_of;  // chamada de psiDrawSprites de cada quadrado
    std::vector<std::array<float, 2>> shift;  // âncora de cada quadrado (pixels da janela)
    UiHdFrame frame;

    UiHdBatch& batch(uint32_t quad) {
        const uint32_t list = quad < list_of.size() ? list_of[quad] : 0;
        std::shared_ptr<UiHdBatch>& b = frame.batches[list];
        if (b == nullptr) b = std::make_shared<UiHdBatch>();
        return *b;
    }

    UiHdIdentity identity(const void* sprite) const {
        if (sprite == nullptr || !env.identity) return UiHdIdentity{};
        return env.identity(sprite);
    }

    // Âncoras: os quadrados do HUD (com pane, fora de anchor_skip, sem os esticados de tela cheia) em grupos
    // por pane e por contato (ui_hd_anchor_groups, 8 pixels de folga); o terço do grupo decide.
    void compute_anchors() {
        shift.assign(count, {0.0f, 0.0f});
        if (config.anchor != UiHdAnchor::Edges) return;
        std::vector<std::array<float, 4>> rects;
        std::vector<int> panes;
        std::vector<uint32_t> which;
        for (size_t i = 0; i < count; ++i) {
            const Psi2DQuad& q = quads[i];
            const UiHdIdentity id = identity(q.origin.sprite);
            if (id.pane == UiHdImageKey::kAny || config.anchor_skipped(id.pane, id.sprite)) continue;
            const float in[4] = {q.x0, q.y0, q.x1, q.y1};
            float r[4];
            if (psi_2d_map_rect(vp, env.width, env.height, in, r)) continue;
            rects.push_back({std::min(q.x0, q.x1), std::min(q.y0, q.y1), std::max(q.x0, q.x1), std::max(q.y0, q.y1)});
            panes.push_back(id.pane);
            which.push_back(uint32_t(i));
        }
        const std::vector<uint32_t> group = ui_hd_anchor_groups(rects, panes, 8.0f);
        std::vector<std::array<float, 4>> box;
        for (size_t k = 0; k < group.size(); ++k) {
            if (group[k] >= box.size()) box.resize(group[k] + 1, {1e9f, 1e9f, -1e9f, -1e9f});
            auto& b = box[group[k]];
            b[0] = std::min(b[0], rects[k][0]);
            b[1] = std::min(b[1], rects[k][1]);
            b[2] = std::max(b[2], rects[k][2]);
            b[3] = std::max(b[3], rects[k][3]);
        }
        for (size_t k = 0; k < group.size(); ++k) {
            const auto& b = box[group[k]];
            shift[which[k]] = {ui_hd_anchor_shift(config.anchor, vp, env.width, b[0], b[2]),
                               ui_hd_anchor_shift_y(config.anchor, vp, env.height, b[1], b[3])};
        }
    }

    // Uma imagem; uma moldura fora da proporção (psi_2d_nine_slice) vira nove, todas da mesma rasterização
    // da substituta (na escala dos cantos: as bordas e o meio esticam a imagem, como no 2D original).
    void add_image(uint32_t qi, float dx, float dy, bool anchored) {
        const Psi2DQuad& q = quads[qi];
        if (const Psi2DFrame* f = psi_2d_find_frame(q.origin.texture_hash)) {
            const float in[4] = {q.x0, q.y0, q.x1, q.y1};
            float r[4];
            Psi2DQuad pieces[9];
            float scale[2];
            if (!psi_2d_map_rect(vp, env.width, env.height, in, r) &&
                psi_2d_nine_slice(q, *f, vp.scale_x / vp.scale_y, pieces, scale) == 9) {
                for (const Psi2DQuad& p : pieces) add_piece(p, qi, dx, dy, anchored, scale);
                return;
            }
        }
        add_piece(q, qi, dx, dy, anchored, nullptr);
    }

    // force_asset >= 0: a substituta inteira no lugar do elemento (os símbolos de botão de "prompts").
    void add_piece(const Psi2DQuad& q, uint32_t qi, float dx, float dy, bool anchored, const float* slice_scale,
                   int force_asset = -1) {
        UiHdImage im;
        const float in[4] = {q.x0, q.y0, q.x1, q.y1};
        float r[4];
        const bool stretched = psi_2d_map_rect(vp, env.width, env.height, in, r);
        if (!stretched) {
            r[0] += dx;
            r[2] += dx;
            r[1] += dy;
            r[3] += dy;
        }
        im.x0 = r[0];
        im.y0 = r[1];
        im.x1 = r[2];
        im.y1 = r[3];
        im.u0 = im.orig_uv[0] = q.u0;
        im.v0 = im.orig_uv[1] = q.v0;
        im.u1 = im.orig_uv[2] = q.u1;
        im.v1 = im.orig_uv[3] = q.v1;
        std::memcpy(im.rgba, q.rgba, 4);
        im.texture = q.texture;
        im.alpha = q.alpha;
        im.linear = q.origin.kind != Psi2DKind::Glyph || q.linear;
        std::memcpy(im.clip, stretched || anchored ? window : area, sizeof(im.clip));
        if (force_asset >= 0) {
            im.asset = force_asset;
            im.u0 = im.v0 = 0.0f;
            im.u1 = im.v1 = 1.0f;
            im.asset_w = quantize_size(std::fabs(im.x1 - im.x0));
            im.asset_h = quantize_size(std::fabs(im.y1 - im.y0));
            UiHdBatch& b = batch(qi);
            b.ops.push_back({UiHdOp::Image, uint32_t(b.images.size())});
            b.images.push_back(im);
            return;
        }
        // Substituta: pelo hashcode e o recorte em texels, ou pela identidade no HUD.
        int tw = 0, th = 0;
        float span[4];
        const bool sized = q.origin.texture_hash != 0 && env.texture_size && env.texture_size(q.origin.texture_hash, tw, th) &&
                           ui_hd_texel_span(q, tw, th, span);
        const UiHdIdentity id = identity(q.origin.sprite);
        if (q.origin.kind != Psi2DKind::Glyph && (sized || id.pane != UiHdImageKey::kAny)) {
            float sorted[4];
            if (sized) {
                sorted[0] = std::min(span[0], span[2]);
                sorted[1] = std::min(span[1], span[3]);
                sorted[2] = std::max(span[0], span[2]);
                sorted[3] = std::max(span[1], span[3]);
            }
            const int e = config.find_image(q.origin.texture_hash, sized ? sorted : nullptr, id.pane, id.sprite);
            if (e >= 0) {
                const UiHdImageEntry& entry = config.images[size_t(e)];
                float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
                const bool by_identity = entry.key.pane != UiHdImageKey::kAny || entry.key.sprite != UiHdImageKey::kAny;
                if (sized && (entry.key.has_rect || !by_identity)) {
                    ui_hd_asset_uv(entry, tw, th, span, uv);
                } else {
                    // Pela identidade, sem retângulo: o elemento mostra a substituta inteira (com o espelhamento
                    // dele).
                    if (q.u0 > q.u1) std::swap(uv[0], uv[2]);
                    if (q.v0 > q.v1) std::swap(uv[1], uv[3]);
                }
                im.asset = e;
                im.u0 = uv[0];
                im.v0 = uv[1];
                im.u1 = uv[2];
                im.v1 = uv[3];
                const float du = std::fabs(uv[2] - uv[0]), dv = std::fabs(uv[3] - uv[1]);
                im.asset_w = quantize_size(std::fabs(im.x1 - im.x0) / std::max(du, 1.0f / 4096.0f));
                im.asset_h = quantize_size(std::fabs(im.y1 - im.y0) / std::max(dv, 1.0f / 4096.0f));
                if (slice_scale != nullptr && sized) {
                    // Um pedaço de moldura: a substituta inteira na escala dos cantos.
                    float ext[2] = {float(tw), float(th)};
                    if (entry.key.has_rect) {
                        for (int a = 0; a < 2; ++a) {
                            ext[a] = entry.exclusive[a] ? entry.key.rect[a + 2] : std::max(1.0f, entry.key.rect[a + 2] - 1.0f);
                        }
                    }
                    im.asset_w = quantize_size(ext[0] * slice_scale[0] * vp.scale_x);
                    im.asset_h = quantize_size(ext[1] * slice_scale[1] * vp.scale_y);
                }
            }
        }
        UiHdBatch& b = batch(qi);
        b.ops.push_back({UiHdOp::Image, uint32_t(b.images.size())});
        b.images.push_back(im);
    }

    // O desenho de um símbolo de botão de um texto ("~A") no conjunto em vigor.
    struct SymbolPlan {
        uint32_t quad = 0;
        int kind = 0;         // 0 a imagem de sempre (add_image), 1 a substituta forçada, 2 uma tecla
        int asset = -1;
        std::wstring label;
        float key_h = 0.0f;   // tecla: altura (janela)
        float cap_px = 0.0f;  // tecla: altura das maiúsculas do texto (janela)
        float width = 0.0f;   // tecla: largura (tela do PS2)
        float shift = 0.0f;   // deslocamento em x (tela do PS2)
    };

    SymbolPlan plan_symbol(uint32_t s, bool game) const {
        SymbolPlan p;
        p.quad = s;
        const Psi2DQuad& q = quads[s];
        const uint8_t code = q.origin.glyph;
        if (env.prompts == UiHdPromptSet::Keyboard && env.key_label) {
            p.label = env.key_label(code, game);
            if (!p.label.empty()) {
                p.kind = 2;
                // Altura: a do símbolo, no mínimo 17 pixels da tela do PS2 (o Select tem 12).
                p.key_h = std::max(std::fabs(q.y1 - q.y0), 17.0f) * vp.scale_y * 0.92f;
                p.cap_px = p.key_h * 0.5f;
                const float label_w = env.measure ? env.measure(p.label, p.cap_px)
                                                  : 0.62f * p.cap_px / 0.7f * float(p.label.size());
                p.width = ui_hd_keycap_width(label_w, p.key_h) / std::max(vp.scale_x, 1e-3f);
                return p;
            }
        }
        if (env.prompts == UiHdPromptSet::Xbox) {
            if (const char* name = ui_hd_xbox_name(code, env.triggers_r1)) {
                const auto it = config.prompt_xbox.find(name);
                if (it != config.prompt_xbox.end()) {
                    p.kind = 1;
                    p.asset = it->second;
                    return p;
                }
            }
        }
        const auto it = config.prompt_ps2.find(std::string(1, char(code)));
        if (it != config.prompt_ps2.end()) {
            p.kind = 1;
            p.asset = it->second;
        }
        return p;
    }

    // Teclas mais largas que o símbolo empurram o que vem depois na mesma linha; o bloco volta ao
    // alinhamento do formato (centrado: metade para a esquerda; à direita: tudo). Devolve o deslocamento de
    // cada linha (tela do PS2).
    std::vector<float> layout_symbols(const UiHdRunLines& rl, std::vector<SymbolPlan>& plans, uint8_t align) const {
        std::vector<float> line_shift(rl.lines.size(), 0.0f);
        bool any = false;
        for (const SymbolPlan& p : plans) any = any || p.kind == 2;
        if (!any) return line_shift;
        struct Item {
            float x;
            float baseline;
            int line;   // >= 0: rl.lines
            int plan;   // >= 0: plans
        };
        std::vector<Item> items;
        for (size_t i = 0; i < rl.lines.size(); ++i) items.push_back({rl.lines[i].x0, rl.lines[i].baseline, int(i), -1});
        for (size_t i = 0; i < plans.size(); ++i) {
            const Psi2DQuad& q = quads[plans[i].quad];
            items.push_back({std::min(q.x0, q.x1), q.origin.baseline, -1, int(i)});
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (std::fabs(a.baseline - b.baseline) > kEps) return a.baseline < b.baseline;
            return a.x < b.x;
        });
        for (size_t first = 0; first < items.size();) {
            size_t end = first;
            float acc = 0.0f;
            while (end < items.size() && std::fabs(items[end].baseline - items[first].baseline) <= kEps) {
                Item& it = items[end];
                if (it.line >= 0) line_shift[size_t(it.line)] = acc;
                if (it.plan >= 0) {
                    SymbolPlan& p = plans[size_t(it.plan)];
                    p.shift = acc;
                    if (p.kind == 2) {
                        const Psi2DQuad& q = quads[p.quad];
                        acc += p.width - std::fabs(q.x1 - q.x0);
                    }
                }
                ++end;
            }
            const float back = (align & 2) != 0 ? acc * 0.5f : ((align & 4) != 0 ? acc : 0.0f);
            for (size_t k = first; k < end; ++k) {
                if (items[k].line >= 0) line_shift[size_t(items[k].line)] -= back;
                if (items[k].plan >= 0) plans[size_t(items[k].plan)].shift -= back;
            }
            first = end;
        }
        return line_shift;
    }

    void add_key(const SymbolPlan& p, float dx, float dy, bool anchored) {
        const Psi2DQuad& q = quads[p.quad];
        UiHdKey k;
        k.x0 = mapped_x(vp, std::min(q.x0, q.x1) + p.shift) + dx;
        k.x1 = k.x0 + p.width * vp.scale_x;
        const float cy = mapped_y(vp, (q.y0 + q.y1) * 0.5f) + dy;
        k.y0 = cy - p.key_h * 0.5f;
        k.y1 = cy + p.key_h * 0.5f;
        k.label = p.label;
        k.cap_px = p.cap_px;
        k.rgba[0] = k.rgba[1] = k.rgba[2] = 0x80;
        k.rgba[3] = q.rgba[3];
        std::memcpy(k.clip, anchored ? window : area, sizeof(k.clip));
        UiHdBatch& b = batch(p.quad);
        b.ops.push_back({UiHdOp::Key, uint32_t(b.keys.size())});
        b.keys.push_back(std::move(k));
    }

    // Um texto a partir da passada principal (ou de uma cópia sem principal), com as cópias de efeito.
    void add_text(const UiHdRun& main, const std::vector<UiHdRun>& aux) {
        const UiHdRunLines rl = ui_hd_run_lines(quads, main);
        const Psi2DOrigin& o = quads[main.first].origin;
        const bool game = env.game_context && env.game_context(o.sprite);
        std::vector<SymbolPlan> plans;
        for (const uint32_t s : rl.symbols) plans.push_back(plan_symbol(s, game));
        const std::vector<float> line_shift = layout_symbols(rl, plans, o.align);
        // A âncora do grupo do primeiro glifo (os glifos de um texto se encostam: o mesmo grupo).
        const float dx = shift[main.first][0], dy = shift[main.first][1];
        const bool anchored = dx != 0.0f || dy != 0.0f;
        const uint32_t last = main.first + main.count - 1;
        UiHdBatch& b = batch(last);
        if (!rl.lines.empty()) {
            UiHdText t;
            t.font = o.font >= 1 && o.font <= 3 ? o.font : 2;
            const float scale = o.scale > 0.0f && o.scale < 100.0f ? o.scale : 1.0f;
            t.cap_px = env.cap_height[t.font] * scale * vp.scale_y;
            t.align = o.align;
            t.alpha = quads[main.first].alpha;
            std::memcpy(t.clip, anchored ? window : area, sizeof(t.clip));
            for (size_t li = 0; li < rl.lines.size(); ++li) {
                UiHdLine line = rl.lines[li];
                line.x0 = mapped_x(vp, line.x0 + line_shift[li]) + dx;
                line.x1 = mapped_x(vp, line.x1 + line_shift[li]) + dx;
                line.baseline = mapped_y(vp, line.baseline) + dy;
                t.lines.push_back(std::move(line));
            }
            // As cópias: um contorno são quatro cópias em (+-1, +-1) na mesma cor.
            std::vector<UiHdEffect> copies;
            int outline = 0;
            for (const UiHdRun& a : aux) {
                const UiHdRunLines al = ui_hd_run_lines(quads, a);
                UiHdEffect e;
                const float ox = al.first_x - rl.first_x, oy = al.first_baseline - rl.first_baseline;
                e.dx = ox * vp.scale_x;
                e.dy = oy * vp.scale_y;
                uint32_t g = a.first;
                while (g + 1 < a.first + a.count && quads[g].origin.kind != Psi2DKind::Glyph) ++g;
                quad_rgba(quads[g], e.rgba);
                // Font_DrawText desloca as cópias de 1 pixel da tela do PS2, sem a escala do texto.
                const bool unit = std::fabs(std::fabs(ox) - 1.0f) < 0.1f && std::fabs(std::fabs(oy) - 1.0f) < 0.1f;
                if (a.pass == Psi2DPass::Outline && unit) ++outline;
                copies.push_back(e);
            }
            size_t i = 0;
            bool stroked = false;
            for (const UiHdRun& a : aux) {
                UiHdEffect e = copies[i++];
                const bool unit = std::fabs(std::fabs(e.dx / vp.scale_x) - 1.0f) < 0.1f &&
                                  std::fabs(std::fabs(e.dy / vp.scale_y) - 1.0f) < 0.1f;
                if (outline >= 4 && a.pass == Psi2DPass::Outline && unit) {
                    if (stroked) continue;
                    stroked = true;
                    e.kind = UiHdEffect::Stroke;
                    e.dx = e.dy = 0.0f;
                    e.width = 2.0f * std::max(vp.scale_x, vp.scale_y);
                }
                t.effects.push_back(e);
            }
            b.ops.push_back({UiHdOp::Text, uint32_t(b.texts.size())});
            b.texts.push_back(std::move(t));
        }
        for (const SymbolPlan& p : plans) {
            if (p.kind == 2) {
                add_key(p, dx, dy, anchored);
            } else if (p.kind == 1) {
                add_piece(quads[p.quad], p.quad, dx + p.shift * vp.scale_x, dy, anchored, nullptr, p.asset);
            } else {
                add_image(p.quad, dx + p.shift * vp.scale_x, dy, anchored);
            }
        }
    }

    void run_all(const Psi2DList* lists, size_t list_count) {
        list_of.assign(count, 0);
        for (size_t l = 0; l < list_count; ++l) {
            for (uint32_t i = 0; i < lists[l].count && lists[l].first + i < count; ++i) list_of[lists[l].first + i] = uint32_t(l);
        }
        compute_anchors();
        const std::vector<UiHdRun> runs = config.text ? ui_hd_group_runs(quads, count) : std::vector<UiHdRun>{};
        std::vector<int32_t> run_at(count, -1);
        for (size_t r = 0; r < runs.size(); ++r) run_at[runs[r].first] = int32_t(r);
        std::vector<UiHdRun> pending;  // cópias de sombra e contorno esperando o texto principal
        const auto flush = [&]() {
            for (const UiHdRun& a : pending) add_text(a, {});
            pending.clear();
        };
        for (size_t i = 0; i < count;) {
            if (run_at[i] >= 0) {
                const UiHdRun& r = runs[size_t(run_at[i])];
                if (r.pass != Psi2DPass::Main) {
                    if (!pending.empty() && (pending.back().text != r.text || pending.back().sprite != r.sprite)) flush();
                    pending.push_back(r);
                } else {
                    if (!pending.empty() && (pending.back().text != r.text || pending.back().sprite != r.sprite)) flush();
                    add_text(r, pending);
                    pending.clear();
                }
                i = r.first + r.count;
                continue;
            }
            flush();
            const float dx = shift[i][0], dy = shift[i][1];
            add_image(uint32_t(i), dx, dy, dx != 0.0f || dy != 0.0f);
            ++i;
        }
        flush();
    }
};

}  // namespace

UiHdFrame ui_hd_build(const Psi2DQuad* quads, size_t count, const Psi2DList* lists, size_t list_count,
                      const UiHdConfig& config, const UiHdEnv& env) {
    Builder b{quads, count, config, env, psi_2d_viewport(env.width, env.height), {}, {}, {}, {}, {}};
    b.area[0] = b.vp.left;
    b.area[1] = b.vp.top;
    b.area[2] = b.vp.left + b.vp.width;
    b.area[3] = b.vp.top + b.vp.height;
    b.window[0] = b.window[1] = 0.0f;
    b.window[2] = float(env.width);
    b.window[3] = float(env.height);
    b.run_all(lists, list_count);
    return std::move(b.frame);
}

// ---------------------------------------------------------------- inventário

void UiHdInventory::add_frame(const Psi2DQuad* quads, size_t count, const UiHdEnv& env) {
    std::map<std::string, bool> seen;  // uma contagem por quadro
    for (size_t i = 0; i < count; ++i) {
        const Psi2DQuad& q = quads[i];
        if (q.origin.kind == Psi2DKind::Glyph || q.origin.pass != Psi2DPass::Main) continue;
        if (q.x0 == q.x1 || q.y0 == q.y1) continue;  // sem área: não desenha nada
        int tw = 0, th = 0;
        float span[4] = {0, 0, 0, 0};
        if (q.origin.texture_hash != 0 && env.texture_size) env.texture_size(q.origin.texture_hash, tw, th);
        if (!ui_hd_texel_span(q, tw, th, span)) {
            tw = th = 0;
        }
        Entry e;
        e.hash = q.origin.texture_hash;
        e.u = int(std::lround(std::min(span[0], span[2])));
        e.v = int(std::lround(std::min(span[1], span[3])));
        e.w = int(std::lround(std::fabs(span[2] - span[0])));
        e.h = int(std::lround(std::fabs(span[3] - span[1])));
        e.tex_w = tw;
        e.tex_h = th;
        e.slot = q.texture;
        e.kind = uint8_t(q.origin.kind);
        e.alpha = q.alpha;
        char key[96];
        std::snprintf(key, sizeof(key), "%08x_%d_%d_%dx%d", e.hash, e.u, e.v, e.w, e.h);
        std::string owner = "?";
        if (q.origin.sprite != nullptr && env.identity) {
            const UiHdIdentity id = env.identity(q.origin.sprite);
            if (id.pane != UiHdImageKey::kAny) owner = std::to_string(id.pane) + ":" + std::to_string(id.sprite);
        }
        size_t at;
        if (auto it = index_.find(key); it != index_.end()) {
            at = it->second;
        } else {
            at = entries_.size();
            index_[key] = at;
            entries_.push_back(e);
        }
        Entry& x = entries_[at];
        x.slot = q.texture;
        if (tw > 0) {
            x.tex_w = tw;
            x.tex_h = th;
        }
        x.seen_now = true;
        x.screen_w = std::max(x.screen_w, std::fabs(q.x1 - q.x0));
        x.screen_h = std::max(x.screen_h, std::fabs(q.y1 - q.y0));
        if (!seen[key]) {
            ++x.count;
            seen[key] = true;
        }
        if (("," + x.owners + ",").find("," + owner + ",") == std::string::npos) {
            x.owners = x.owners.empty() ? owner : x.owners + "," + owner;
        }
    }
}

std::string UiHdInventory::to_tsv() const {
    std::string out = "hash\tu\tv\tw\th\ttex_w\ttex_h\ttipo\talpha\ttela_w\ttela_h\tquadros\tdonos\trecorte\n";
    std::vector<const Entry*> sorted;
    for (const Entry& e : entries_) sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) {
        if (a->hash != b->hash) return a->hash < b->hash;
        if (a->v != b->v) return a->v < b->v;
        if (a->u != b->u) return a->u < b->u;
        if (a->w != b->w) return a->w < b->w;
        return a->h < b->h;
    });
    char line[512];
    for (const Entry* e : sorted) {
        std::snprintf(line, sizeof(line), "%08x\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%02llx\t%.1f\t%.1f\t%u\t%s\t%08x_%d_%d_%dx%d.png\n",
                      e->hash, e->u, e->v, e->w, e->h, e->tex_w, e->tex_h,
                      e->kind == uint8_t(Psi2DKind::Symbol) ? "simbolo" : "imagem", (unsigned long long)e->alpha,
                      double(e->screen_w), double(e->screen_h), e->count, e->owners.c_str(), e->hash, e->u, e->v, e->w,
                      e->h);
        out += line;
    }
    return out;
}

bool UiHdInventory::load(const std::string& tsv) {
    std::istringstream in(tsv);
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            header = false;
            if (line.rfind("hash\t", 0) == 0) continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> f;
        size_t p = 0;
        for (;;) {
            const size_t t = line.find('\t', p);
            f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
            if (t == std::string::npos) break;
            p = t + 1;
        }
        if (f.size() < 13) return false;
        Entry e;
        if (!parse_hex(f[0], e.hash)) return false;
        e.u = std::atoi(f[1].c_str());
        e.v = std::atoi(f[2].c_str());
        e.w = std::atoi(f[3].c_str());
        e.h = std::atoi(f[4].c_str());
        e.tex_w = std::atoi(f[5].c_str());
        e.tex_h = std::atoi(f[6].c_str());
        e.kind = uint8_t(f[7] == "simbolo" ? Psi2DKind::Symbol : Psi2DKind::Image);
        uint32_t alpha = 0x44;
        parse_hex(f[8], alpha);
        e.alpha = alpha;
        e.screen_w = float(std::atof(f[9].c_str()));
        e.screen_h = float(std::atof(f[10].c_str()));
        e.count = uint32_t(std::strtoul(f[11].c_str(), nullptr, 10));
        e.owners = f[12];
        char key[96];
        std::snprintf(key, sizeof(key), "%08x_%d_%d_%dx%d", e.hash, e.u, e.v, e.w, e.h);
        if (index_.count(key) != 0) {
            Entry& x = entries_[index_[key]];
            x.count += e.count;
            continue;
        }
        index_[key] = entries_.size();
        entries_.push_back(e);
    }
    return true;
}

}  // namespace nf
