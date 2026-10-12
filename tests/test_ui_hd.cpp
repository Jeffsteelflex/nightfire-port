// Testes da camada 2D em alta definição (render/ui_hd.h): o map.json, a escolha das substitutas pelo
// recorte, as âncoras, e os textos refeitos a partir do desenho real de Font_DrawText (fonte sintética); as
// molduras em nove pedaços (psi_2d_nine_slice) e os símbolos de botão por aparelho.

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/font.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/sprite.h"
#include "engine/txt.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_2d.h"
#include "platform/input_bindings.h"
#include "platform/psi_draw.h"
#include "platform/psi_pad.h"
#include "render/ui_hd.h"
#include "test_framework.h"

using namespace nf;

namespace {

// Um quadrado de imagem de uma textura w x h mostrando os texels u..u+uw, v..v+vh como psiDrawSprites os
// manda (ST com meio texel, filtro bilinear).
Psi2DQuad image_quad(float x, float y, float w, float h, int u, int v, int uw, int vh, int tw, int th, uint32_t hash) {
    Psi2DQuad q{};
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = (float(u) + 0.5f) / float(tw);
    q.v0 = (float(v) + 0.5f) / float(th);
    q.u1 = (float(u + uw) + 0.5f) / float(tw);
    q.v1 = (float(v + vh) + 0.5f) / float(th);
    q.rgba[0] = q.rgba[1] = q.rgba[2] = q.rgba[3] = 0x80;
    q.texture = 7;
    q.alpha = 0x44;
    q.linear = true;
    q.origin.kind = Psi2DKind::Image;
    q.origin.texture_hash = hash;
    return q;
}

UiHdEnv env_256(int width, int height) {
    UiHdEnv env;
    env.width = width;
    env.height = height;
    env.texture_size = [](uint32_t hash, int& w, int& h) {
        if (hash != 0x0300007a) return false;
        w = h = 256;
        return true;
    };
    return env;
}

const char kMap[] = R"({
  "anchor": "edges",
  "fonts": {"2": {"family": "Arial", "weight": 700, "italic": true, "size": 1.25, "spacing": 0.05, "fit_min": 0.8}},
  "images": [
    {"name": "meio", "texture": "0300007a", "rect": [190, 0, 2, 57], "file": "svg/mid.svg"},
    {"texture": "0x0300007A", "rect": [185, 0, 6, 57], "file": "svg/left.svg"},
    {"texture": "03000128", "rect": [26, 0, 15, 256], "extent": ["inclusive", "exclusive"], "file": "img/bullets.PNG"},
    {"pane": 1, "sprite": 8, "texture": "0300007a", "file": "svg/blade.svg"}
  ]
})";

}  // namespace

TEST(ui_hd_config_parses_the_map) {
    UiHdConfig c;
    std::string err;
    CHECK(ui_hd_parse_config(kMap, c, &err));
    CHECK(err.empty());
    CHECK(c.anchor == UiHdAnchor::Edges && c.text);
    CHECK(c.fonts[2].family == L"Arial" && c.fonts[2].weight == 700 && c.fonts[2].italic);
    CHECK_NEAR(c.fonts[2].size, 1.25, 1e-6);
    CHECK_NEAR(c.fonts[2].spacing, 0.05, 1e-6);
    CHECK_NEAR(c.fonts[2].fit_min, 0.8, 1e-6);
    CHECK(c.fonts[1].family == L"Bahnschrift");  // as outras ficam no padrão
    CHECK(c.images.size() == 4);
    CHECK(c.images[1].key.texture == 0x0300007a && c.images[1].key.has_rect && c.images[1].key.rect[0] == 185.0f);
    CHECK(c.images[2].type == UiHdAssetType::Png && !c.images[2].exclusive[0] && c.images[2].exclusive[1]);
    CHECK(c.images[3].key.pane == 1 && c.images[3].key.sprite == 8 && !c.images[3].key.has_rect);
    CHECK(c.images[0].name == "meio");
}

TEST(ui_hd_config_rejects_bad_maps) {
    UiHdConfig c;
    std::string err;
    CHECK(!ui_hd_parse_config("{\"images\": [{\"file\": \"a.svg\"}]}", c, &err));  // chave sem nada
    CHECK(!err.empty());
    CHECK(!ui_hd_parse_config("{\"images\": [{\"texture\": \"03000026\", \"file\": \"a.bmp\"}]}", c, &err));
    CHECK(!ui_hd_parse_config("{\"images\": [{\"texture\": \"xyz\", \"file\": \"a.svg\"}]}", c, &err));
    CHECK(!ui_hd_parse_config("{\"anchor\": \"left\"}", c, &err));
    CHECK(!ui_hd_parse_config("{\"fonts\": {\"4\": {}}}", c, &err));
    CHECK(!ui_hd_parse_config("{\"images\": [1, ]}", c, &err));
    CHECK(!ui_hd_parse_config("{} x", c, &err));
    CHECK(ui_hd_parse_config("{\"s\": \"\\u00e1\\n\"}", c, &err));  // escapes e chaves desconhecidas
}

TEST(ui_hd_committed_map_is_valid) {
    // O hd_ui/map.json do repositório lê sem erro e todas as substitutas existem.
    const std::string dir = std::string(NF_SOURCE_DIR) + "/hd_ui/";
    FILE* f = nullptr;
    CHECK(fopen_s(&f, (dir + "map.json").c_str(), "rb") == 0 && f != nullptr);
    if (f == nullptr) return;
    std::string json;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    std::fclose(f);
    UiHdConfig c;
    std::string err;
    CHECK(ui_hd_parse_config(json, c, &err));
    CHECK(!c.images.empty());
    for (const UiHdImageEntry& e : c.images) {
        FILE* a = nullptr;
        const bool ok = fopen_s(&a, (dir + e.file).c_str(), "rb") == 0 && a != nullptr;
        if (!ok) std::printf("  falta %s\n", e.file.c_str());
        CHECK(ok);
        if (a != nullptr) std::fclose(a);
    }
}

TEST(ui_hd_find_image_by_texel_rect_and_identity) {
    UiHdConfig c;
    CHECK(ui_hd_parse_config(kMap, c, nullptr));
    // As pontas e o meio da faixa das mensagens: cada pedaço só no retângulo dele (centros dos texels).
    const float left[4] = {185, 0, 190, 56}, mid[4] = {190, 0, 191, 56}, right[4] = {191, 0, 196, 56};
    CHECK(c.find_image(0x0300007a, left, UiHdImageKey::kAny, UiHdImageKey::kAny) == 1);
    CHECK(c.find_image(0x0300007a, mid, UiHdImageKey::kAny, UiHdImageKey::kAny) == 0);
    CHECK(c.find_image(0x0300007a, right, UiHdImageKey::kAny, UiHdImageKey::kAny) == -1);
    CHECK(c.find_image(0x03000026, left, UiHdImageKey::kAny, UiHdImageKey::kAny) == -1);  // outra textura
    // "exclusive" em v: a pilha de balas pode ir até a borda de baixo da coluna; em u, só até o último centro.
    const float stack[4] = {26, 228, 40, 256}, wide[4] = {26, 0, 41, 28};
    CHECK(c.find_image(0x03000128, stack, UiHdImageKey::kAny, UiHdImageKey::kAny) == 2);
    CHECK(c.find_image(0x03000128, wide, UiHdImageKey::kAny, UiHdImageKey::kAny) == -1);
    // A identidade vem antes do recorte, e só casa com a pane e o sprite certos.
    CHECK(c.find_image(0x0300007a, left, 1, 8) == 3);
    CHECK(c.find_image(0x0300007a, left, 1, 9) == 1);
}

TEST(ui_hd_asset_uv_maps_the_key_rect) {
    UiHdConfig c;
    CHECK(ui_hd_parse_config(kMap, c, nullptr));
    // Inclusivo: do centro do primeiro texel da chave (0) ao do último (1).
    const float left[4] = {185, 0, 190, 56};
    float uv[4];
    ui_hd_asset_uv(c.images[1], 256, 256, left, uv);
    CHECK_NEAR(uv[0], 0.0, 1e-6);
    CHECK_NEAR(uv[2], 1.0, 1e-6);
    CHECK_NEAR(uv[1], 0.0, 1e-6);
    CHECK_NEAR(uv[3], 1.0, 1e-6);
    // Espelhado: o ST da substituta também.
    const float flipped[4] = {190, 0, 185, 56};
    ui_hd_asset_uv(c.images[1], 256, 256, flipped, uv);
    CHECK(uv[0] == 1.0f && uv[2] == 0.0f);
    // Exclusivo em v: 7 balas de 4 texels a partir da linha 228 -> o pedaço da coluna de 256.
    const float stack[4] = {26, 228, 40, 256};
    ui_hd_asset_uv(c.images[2], 64, 256, stack, uv);
    CHECK_NEAR(uv[1], 228.0 / 256.0, 1e-6);
    CHECK_NEAR(uv[3], 1.0, 1e-6);
    CHECK_NEAR(uv[0], 0.0, 1e-6);
    CHECK_NEAR(uv[2], 1.0, 1e-6);
    // Uma coluna só, esticada: a substituta inteira.
    const float strip[4] = {190, 0, 190, 56};
    ui_hd_asset_uv(c.images[0], 256, 256, strip, uv);
    CHECK(uv[0] == 0.0f && uv[2] == 1.0f);
}

TEST(ui_hd_texel_span_undoes_the_half_texel) {
    const Psi2DQuad q = image_quad(0, 0, 5, 26, 185, 0, 5, 56, 256, 256, 0x0300007a);
    float span[4];
    CHECK(ui_hd_texel_span(q, 256, 256, span));
    CHECK(span[0] == 185.0f && span[1] == 0.0f && span[2] == 190.0f && span[3] == 56.0f);
    CHECK(!ui_hd_texel_span(q, 0, 256, span));
    Psi2DQuad glyph = q;
    glyph.linear = false;  // fontes: sem o meio texel
    glyph.u0 = 16.0f / 256.0f;
    CHECK(ui_hd_texel_span(glyph, 256, 256, span) && span[0] == 16.0f);
}

TEST(ui_hd_anchor_shifts_by_thirds) {
    const Psi2DViewport vp = psi_2d_viewport(2560, 1080);  // 21:9: área 4:3 de 1440 a partir de 560
    CHECK(vp.left == 560.0f);
    CHECK(ui_hd_anchor_shift(UiHdAnchor::Edges, vp, 2560, 20, 107) == -560.0f);
    CHECK(ui_hd_anchor_shift(UiHdAnchor::Edges, vp, 2560, 440, 500) == 560.0f);
    CHECK(ui_hd_anchor_shift(UiHdAnchor::Edges, vp, 2560, 16, 496) == 0.0f);  // a faixa no meio
    CHECK(ui_hd_anchor_shift(UiHdAnchor::Centre, vp, 2560, 20, 107) == 0.0f);
    const Psi2DViewport square = psi_2d_viewport(1024, 768);  // 4:3: nada a mover
    CHECK(ui_hd_anchor_shift(UiHdAnchor::Edges, square, 1024, 20, 107) == 0.0f);
    const Psi2DViewport tall = psi_2d_viewport(800, 1000);  // mais estreita: em y
    CHECK(ui_hd_anchor_shift_y(UiHdAnchor::Edges, tall, 1000, 340, 428) == tall.top);
    CHECK(ui_hd_anchor_shift_y(UiHdAnchor::Edges, tall, 1000, 10, 60) == -tall.top);
    CHECK(ui_hd_anchor_shift_y(UiHdAnchor::Edges, vp, 1080, 340, 428) == 0.0f);
}

TEST(ui_hd_build_maps_images_and_anchors_hud_panes) {
    UiHdConfig c;
    CHECK(ui_hd_parse_config(kMap, c, nullptr));
    static int pane_sprite;  // um "sprite" do jogo na pane 1
    UiHdEnv env = env_256(2560, 1080);
    env.identity = [](const void* s) {
        UiHdIdentity id;
        if (s == &pane_sprite) {
            id.pane = 1;
            id.sprite = 8;
        }
        return id;
    };
    Psi2DQuad quads[3];
    quads[0] = image_quad(123, 364, 5, 26, 185, 0, 5, 56, 256, 256, 0x0300007a);  // ponta da faixa
    quads[1] = image_quad(42, 342, 35, 49, 101, 59, 35, 49, 256, 256, 0x0300007a);  // lâmina da vida
    quads[1].origin.sprite = &pane_sprite;
    quads[2] = image_quad(0, 0, 512, 64, 0, 0, 31, 31, 32, 32, 0x03000002);  // tarja de cinema, sem substituta
    quads[2].rgba[3] = 0x40;
    quads[2].alpha = 0x48;
    const Psi2DList lists[2] = {{0, 2}, {2, 1}};
    const UiHdFrame f = ui_hd_build(quads, 3, lists, 2, c, env);
    CHECK(f.batches.size() == 2);
    const UiHdBatch& a = *f.batches.at(0);
    const UiHdBatch& b = *f.batches.at(1);
    CHECK(a.ops.size() == 2 && a.images.size() == 2 && b.images.size() == 1);
    const Psi2DViewport vp = psi_2d_viewport(2560, 1080);
    // A ponta da faixa: substituta 1, ST 0..1, no lugar original (sem pane: não ancora).
    const UiHdImage& band = a.images[0];
    CHECK(band.asset == 1);
    CHECK_NEAR(band.x0, vp.left + 123 * vp.scale_x, 1e-3);
    CHECK_NEAR(band.u0, 0.0, 1e-6);
    CHECK_NEAR(band.u1, 1.0, 1e-6);
    CHECK(band.clip[0] == vp.left);
    // A rasterização cobre a substituta inteira no tamanho em que aparece (arredondado para cima).
    CHECK(band.asset_w >= int(5 * vp.scale_x) && band.asset_w <= int(5 * vp.scale_x * 1.13f) + 4);
    // A lâmina: pela identidade, com a âncora da pane (terço esquerdo -> borda esquerda), recorte na janela.
    const UiHdImage& blade = a.images[1];
    CHECK(blade.asset == 3);
    CHECK_NEAR(blade.x0, 42 * vp.scale_x, 1e-3);
    CHECK(blade.clip[0] == 0.0f && blade.clip[2] == 2560.0f);
    CHECK(blade.u0 == 0.0f && blade.u1 == 1.0f);
    // A tela cheia sem substituta: a original, com a mistura e o alfa dela, esticada até as bordas.
    const UiHdImage& full = b.images[0];
    CHECK(full.asset == -1 && full.alpha == 0x48 && full.rgba[3] == 0x40 && full.texture == 7);
    CHECK(full.x0 == 0.0f && full.x1 == 2560.0f);
    CHECK(full.u0 == quads[2].u0 && full.orig_uv[2] == quads[2].u1);
    // Sem âncora no map.json, a lâmina fica na área 4:3.
    c.anchor = UiHdAnchor::Centre;
    const UiHdFrame g = ui_hd_build(quads, 3, lists, 2, c, env);
    CHECK_NEAR(g.batches.at(0)->images[1].x0, vp.left + 42 * vp.scale_x, 1e-3);
}

TEST(ui_hd_anchor_groups_keep_touching_sprites_together) {
    // A faixa das mensagens (pontas e meio, pane 3) é um grupo só, no meio: nada se move. O medidor de vida
    // (pane 1) vai para a esquerda; o ícone de toque da mesma pane, longe dele, para a direita. O reflexo do
    // sol (pane 8, em anchor_skip) não se move.
    static int sprites[6];
    UiHdEnv env;
    env.width = 2560;
    env.height = 1080;
    env.identity = [](const void* s) {
        UiHdIdentity id;
        const int k = int(static_cast<const int*>(s) - sprites);
        if (k >= 0 && k < 3) id.pane = 3;
        if (k == 3 || k == 4) id.pane = 1;
        if (k == 5) id.pane = 8;
        id.sprite = k;
        return id;
    };
    Psi2DQuad q[6];
    q[0] = image_quad(11, 16, 5, 56, 185, 0, 5, 56, 256, 256, 0);
    q[1] = image_quad(16, 16, 480, 56, 190, 0, 1, 56, 256, 256, 0);
    q[2] = image_quad(496, 16, 5, 56, 191, 0, 5, 56, 256, 256, 0);
    q[3] = image_quad(20, 341, 87, 86, 102, 113, 85, 86, 256, 256, 0);
    q[4] = image_quad(400, 48, 32, 32, 0, 0, 31, 31, 32, 32, 0);
    q[5] = image_quad(40, 40, 32, 32, 0, 0, 31, 31, 32, 32, 0);
    for (int i = 0; i < 6; ++i) q[i].origin.sprite = &sprites[i];
    UiHdConfig c;
    c.anchor = UiHdAnchor::Edges;
    const Psi2DList list{0, 6};
    const UiHdFrame f = ui_hd_build(q, 6, &list, 1, c, env);
    const UiHdBatch& b = *f.batches.at(0);
    const Psi2DViewport vp = psi_2d_viewport(2560, 1080);
    CHECK(b.images.size() == 6);
    CHECK_NEAR(b.images[0].x0, vp.left + 11 * vp.scale_x, 1e-3);
    CHECK_NEAR(b.images[2].x0, vp.left + 496 * vp.scale_x, 1e-3);
    CHECK_NEAR(b.images[3].x0, 20 * vp.scale_x, 1e-3);
    CHECK_NEAR(b.images[4].x0, 2.0 * vp.left + 400 * vp.scale_x, 1e-3);
    CHECK_NEAR(b.images[5].x0, vp.left + 40 * vp.scale_x, 1e-3);
    // Os grupos: só encostar não basta entre panes diferentes.
    const std::vector<std::array<float, 4>> rects = {{0, 0, 10, 10}, {12, 0, 20, 10}, {12, 0, 20, 10}, {100, 0, 110, 10}};
    const std::vector<int> panes = {1, 1, 2, 1};
    const std::vector<uint32_t> g = ui_hd_anchor_groups(rects, panes, 4.0f);
    CHECK(g[0] == g[1] && g[2] != g[0] && g[3] != g[0] && g[3] != g[2]);
    // anchor_skip no map.json.
    UiHdConfig d;
    CHECK(ui_hd_parse_config(R"({"anchor_skip": [8, 14, [0, 7]]})", d, nullptr));
    CHECK(d.anchor_skipped(14, 3) && d.anchor_skipped(0, 7) && !d.anchor_skipped(0, 6) && !d.anchor_skipped(1, 7));
    CHECK(!ui_hd_parse_config(R"({"anchor_skip": ["x"]})", d, nullptr));
}

TEST(ui_hd_chars_undo_the_font_substitutions) {
    CHECK(ui_hd_char('A') == L'A');
    CHECK(ui_hd_char(0x92) == L'’');
    CHECK(ui_hd_char(0x85) == L'…');
    CHECK(ui_hd_char(0x99) == L'™');
    CHECK(ui_hd_char(0xa0) == L' ');
    CHECK(ui_hd_char(0xe9) == L'é');
    CHECK(ui_hd_char(0x81) == L'?');
}

// ---------------------------------------------------------------- textos pelo caminho real do jogo

namespace {

constexpr uint32_t kFontTex = 0x03000778, kSymbolTex = 0x03000075;
pstex_tag g_font_rec, g_symbol_rec;
maptex_tag g_font_tex{&g_font_rec, 256, 64}, g_symbol_tex{&g_symbol_rec, 64, 64};
CHARDATA_tag g_glyphs[6];
FONT_tag g_font, g_font1;  // a fonte 2 do teste e uma cópia para a 1 (o código de fonte é o da entrada)

// Uma fonte de seis glifos (A B C H M T, 10 x 12 texels, 2 de espaço depois) e o símbolo "~X"; a câmera 0
// desenhando sprites; o heap e as listas da camada 2D.
struct TextWorld {
    alignas(16) viewer_tag viewer{};
    TextWorld() {
        psiMemHeapSize = 1u << 21;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        g_font_rec = pstex_tag{};
        g_font_rec.tex_index = 9;
        g_symbol_rec = pstex_tag{};
        g_symbol_rec.tex_index = 4;
        hashtable_additem(kFontTex, &g_font_tex);
        hashtable_additem(kSymbolTex, &g_symbol_tex);
        font_debug_power_on();
        std::memset(FontTable, 0, sizeof(FontTable));
        const char codes[6] = {'A', 'B', 'C', 'H', 'M', 'T'};
        for (int i = 0; i < 6; ++i) {
            g_glyphs[i] = CHARDATA_tag{};
            g_glyphs[i].code = uint16_t(codes[i]);
            g_glyphs[i].u = uint16_t(12 * i);
            g_glyphs[i].w = 10;
            g_glyphs[i].h = 12;
            g_glyphs[i].post = 2;
        }
        g_font = FONT_tag{0, nullptr, 0x21, 0xff, 6, g_glyphs, 6.0f, 4.0f};
        g_font1 = g_font;
        FontTable[0] = {kFontTex, &g_font1};
        FontTable[1] = {kFontTex, &g_font};
        std::memset(specialchar, 0, sizeof(specialchar));
        specialchar[0].code = 'X';
        specialchar[0].hash = kSymbolTex;
        specialchar[0].tex_w = specialchar[0].tex_h = 64;
        specialchar[0].uw = specialchar[0].vh = 15;
        specialchar[0].width = specialchar[0].height = 16;
        psi_2d_debug_power_on();
        FontList[0] = uint32_t(reinterpret_cast<uintptr_t>(&g_font_rec));
        txt_debug_power_on();
        sprite_debug_power_on();
        Sprite_InitLists();
        viewer.sprites = 1;
        glb_viewer[0] = &viewer;
        psi_draw_debug_power_on();
        psiPreDraw();
    }
    ~TextWorld() {
        glb_viewer[0] = nullptr;
        txt_debug_power_on();
        sprite_debug_power_on();
        psi_2d_debug_power_on();
        psi_draw_debug_power_on();
        font_debug_power_on();
        std::memset(FontTable, 0, sizeof(FontTable));
        std::memset(specialchar, 0, sizeof(specialchar));
        hashtable_debug_power_on();
    }
    // Um texto como o HUD o cria: formato "fonte 2", alinhamento opcional, cor e efeitos.
    sprite* text(char* str, char* fmt, int16_t x, int16_t y, uint32_t colour, uint32_t colour2, uint16_t flags) {
        sprite* s = Sprite_Create();
        Sprite_Link2Viewer(s, 0);
        Sprite_SetTextNFmt(s, str, fmt, 0);
        s->x = x;
        s->y = y;
        s->colour = colour;
        s->colour2 = colour2;
        s->flags = flags;
        s->priority = 1;
        return s;
    }
    void draw(sprite* s) {
        sprite* list[1] = {s};
        View_DrawSprites(&viewer, list, 0, 1);
    }
};

std::wstring w(const char* s) { return std::wstring(s, s + std::strlen(s)); }

}  // namespace

TEST(ui_hd_groups_glyphs_into_lines_with_spaces_and_colours) {
    TextWorld world;
    char str[] = "AB C\nHAT\xbaM";  // 0xba troca a cor no meio da segunda linha
    char fmt[] = "\xff\x02";
    sprite* s = world.text(str, fmt, 100, 50, 0x7d6d59ffu, 0, 0);
    world.draw(s);
    const auto& quads = g_psi_frame.quads2d;
    CHECK(quads.size() == 7);  // A B C H A T M
    const std::vector<UiHdRun> runs = ui_hd_group_runs(quads.data(), quads.size());
    CHECK(runs.size() == 1 && runs[0].count == 7 && runs[0].pass == Psi2DPass::Main && runs[0].sprite == s);
    const UiHdRunLines rl = ui_hd_run_lines(quads.data(), runs[0]);
    CHECK(rl.lines.size() == 2);
    CHECK(rl.lines[0].text == L"AB C");  // o espaço vem do texto, não de um glifo
    CHECK(rl.lines[1].text == L"HATM");
    // A linha de base de cada linha é a caneta (Pos.y); o primeiro glifo da fonte dá a medida.
    CHECK(rl.lines[0].baseline == 50.0f);
    CHECK(rl.lines[1].baseline > rl.lines[0].baseline);
    CHECK(rl.lines[0].x0 == 100.0f);
    // Cores: a primeira linha inteira na cor do sprite; a segunda troca no M (0xba: dourado).
    CHECK(rl.lines[0].spans.size() == 1 && rl.lines[0].spans[0].length == 4);
    CHECK(rl.lines[0].spans[0].rgba[0] == 0x7d);
    CHECK(rl.lines[1].spans.size() == 2 && rl.lines[1].spans[1].start == 3 && rl.lines[1].spans[1].rgba[0] == 0x78);
}

TEST(ui_hd_text_shadow_and_outline_become_effects) {
    TextWorld world;
    char str[] = "HAT";
    char fmt[] = "\xff\x02\xfe\x03";  // centrado
    sprite* outlined = world.text(str, fmt, 200, 100, 0xffffffffu, 0x000000ffu, SPRF_OUTLINE);
    world.draw(outlined);
    char str2[] = "AB";
    sprite* shadowed = world.text(str2, fmt, 200, 200, 0x80808080u, 0x10101080u, SPRF_SHADOW);
    world.draw(shadowed);
    const auto& quads = g_psi_frame.quads2d;
    // Contorno: 4 cópias + o texto (3 glifos cada); sombra: 1 cópia + o texto (2 glifos cada).
    CHECK(quads.size() == 15 + 4);
    const std::vector<UiHdRun> runs = ui_hd_group_runs(quads.data(), quads.size());
    CHECK(runs.size() == 7);
    CHECK(runs[0].pass == Psi2DPass::Outline && runs[4].pass == Psi2DPass::Main && runs[5].pass == Psi2DPass::Shadow);
    UiHdConfig c;
    UiHdEnv env;
    env.width = 1024;
    env.height = 768;
    env.cap_height[2] = 12.0f;
    const Psi2DViewport vp = psi_2d_viewport(env.width, env.height);
    std::vector<Psi2DList> lists(g_psi_frame.lists2d.begin(), g_psi_frame.lists2d.end());
    const UiHdFrame f = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
    std::vector<const UiHdText*> texts;
    for (const auto& [list, b] : f.batches) {
        for (const UiHdOp& op : b->ops) {
            CHECK(op.kind == UiHdOp::Text);
            texts.push_back(&b->texts[op.index]);
        }
    }
    CHECK(texts.size() == 2);
    if (texts.size() != 2) return;
    const UiHdText& a = *texts[0];
    CHECK(a.lines.size() == 1 && a.lines[0].text == L"HAT" && a.font == 2 && (a.align & 2) != 0);
    CHECK_NEAR(a.cap_px, 12.0 * vp.scale_y, 1e-4);  // 12 texels x escala 1, levados à janela
    CHECK(a.effects.size() == 1 && a.effects[0].kind == UiHdEffect::Stroke);
    CHECK_NEAR(a.effects[0].width, 2.0 * vp.scale_x, 1e-4);  // 1 pixel do PS2 para cada lado
    CHECK(a.effects[0].rgba[3] == 0x7f);
    const UiHdText& b = *texts[1];
    CHECK(b.lines.size() == 1 && b.lines[0].text == L"AB");
    CHECK(b.effects.size() == 1 && b.effects[0].kind == UiHdEffect::Copy);
    CHECK_NEAR(b.effects[0].dx, vp.scale_x, 1e-4);  // (+1, +1) na tela do PS2
    CHECK_NEAR(b.effects[0].dy, vp.scale_y, 1e-4);
    CHECK(b.effects[0].rgba[0] == 0x10);
    // O texto principal fica onde os glifos originais estavam (centrado: a caneta recua metade da largura).
    CHECK_NEAR(a.lines[0].x0, vp.left + vp.scale_x * (200.0 - outlined->uw * 0.5), 1e-3);
    CHECK_NEAR(a.lines[0].baseline, vp.top + vp.scale_y * 100.0, 1e-3);
}

TEST(ui_hd_button_symbols_split_the_line) {
    TextWorld world;
    char str[] = "AB ~X CH";
    char fmt[] = "\xff\x02";
    sprite* s = world.text(str, fmt, 10, 40, 0x80808080u, 0, 0);
    world.draw(s);
    const auto& quads = g_psi_frame.quads2d;
    CHECK(quads.size() == 5);  // A B, o símbolo, C H
    const std::vector<UiHdRun> runs = ui_hd_group_runs(quads.data(), quads.size());
    CHECK(runs.size() == 1);
    const UiHdRunLines rl = ui_hd_run_lines(quads.data(), runs[0]);
    CHECK(rl.lines.size() == 2 && rl.symbols.size() == 1);
    if (rl.lines.size() != 2) return;
    CHECK(rl.lines[0].text == L"AB" && rl.lines[1].text == L"CH");
    CHECK(rl.lines[0].pinned && rl.lines[1].pinned);
    CHECK(rl.lines[1].x0 > quads[2].x1);  // o segundo trecho começa depois do símbolo
    // No lote: o texto e o símbolo como imagem (a textura dele, com filtro bilinear).
    UiHdConfig c;
    UiHdEnv env;
    env.width = 512;
    env.height = 448;
    std::vector<Psi2DList> lists(g_psi_frame.lists2d.begin(), g_psi_frame.lists2d.end());
    const UiHdFrame f = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
    CHECK(f.batches.size() == 1);
    const UiHdBatch& b = *f.batches.begin()->second;
    CHECK(b.texts.size() == 1 && b.images.size() == 1 && b.images[0].linear);
    CHECK(b.ops.size() == 2 && b.ops[0].kind == UiHdOp::Text && b.ops[1].kind == UiHdOp::Image);
}

TEST(ui_hd_text_without_font_layer_keeps_glyph_quads) {
    TextWorld world;
    char str[] = "ABC";
    char fmt[] = "\xff\x02";
    world.draw(world.text(str, fmt, 10, 40, 0x80808080u, 0, 0));
    UiHdConfig c;
    c.text = false;
    UiHdEnv env;
    env.width = 512;
    env.height = 448;
    const auto& quads = g_psi_frame.quads2d;
    std::vector<Psi2DList> lists(g_psi_frame.lists2d.begin(), g_psi_frame.lists2d.end());
    const UiHdFrame f = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
    const UiHdBatch& b = *f.batches.begin()->second;
    CHECK(b.texts.empty() && b.images.size() == 3 && !b.images[0].linear);  // as fontes com filtro de ponto
}

TEST(ui_hd_inventory_counts_and_round_trips) {
    Psi2DQuad quads[3];
    quads[0] = image_quad(123, 364, 5, 26, 185, 0, 5, 56, 256, 256, 0x0300007a);
    quads[1] = quads[0];
    quads[1].x0 = 384;
    quads[1].x1 = 389;
    quads[2] = image_quad(128, 364, 256, 26, 190, 0, 1, 56, 256, 256, 0x0300007a);
    UiHdInventory inv;
    const UiHdEnv env = env_256(1024, 768);
    inv.add_frame(quads, 3, env);
    inv.add_frame(quads, 3, env);
    CHECK(inv.entries().size() == 2);
    CHECK(inv.entries()[0].count == 2 && inv.entries()[0].u == 185 && inv.entries()[0].w == 5 && inv.entries()[0].h == 56);
    CHECK(inv.entries()[1].screen_w == 256.0f);
    const std::string tsv = inv.to_tsv();
    UiHdInventory back;
    CHECK(back.load(tsv));
    CHECK(back.entries().size() == 2 && back.to_tsv() == tsv);
    back.add_frame(quads, 1, env);  // somando a uma lista que já existia
    CHECK(back.entries()[0].count == 3);
}

// ---------------------------------------------------------------- molduras em nove pedaços (psi_2d)

namespace {

constexpr uint32_t kPanelTex = 0x03000070;
constexpr float kPs2PixelAspect = (4.0f / 3.0f * 448.0f) / 512.0f;  // largura / altura de um pixel do PS2 em 4:3

// O painel inteiro (512 x 256, u = 0..511 como o rótulo da pausa) num retângulo da tela do PS2.
Psi2DQuad panel_quad(float x, float y, float w, float h) { return image_quad(x, y, w, h, 0, 0, 511, 255, 512, 256, kPanelTex); }

// Texels mostrados por um pedaço, em u e em v.
float texels_u(const Psi2DQuad& q) { return std::fabs(q.u1 - q.u0) * 512.0f; }
float texels_v(const Psi2DQuad& q) { return std::fabs(q.v1 - q.v0) * 256.0f; }

}  // namespace

TEST(psi_2d_frame_table_has_the_menu_panel) {
    const Psi2DFrame* f = psi_2d_find_frame(kPanelTex);
    CHECK(f != nullptr);
    if (f == nullptr) return;
    CHECK(f->tex_w == 512 && f->tex_h == 256);
    // Os cortes cabem na textura e deixam um meio para esticar.
    CHECK(f->inset[0] + f->inset[2] < f->tex_w - 1 && f->inset[1] + f->inset[3] < f->tex_h - 1);
    CHECK(psi_2d_find_frame(0) == nullptr && psi_2d_find_frame(0x0300007a) == nullptr);
}

TEST(psi_2d_nine_slice_keeps_the_games_pages_as_one_quad) {
    const Psi2DFrame& f = *psi_2d_find_frame(kPanelTex);
    // A pausa (rótulo 45, 107, 550 x 266 no arquivo de 640 x 480 -> 440 x 248 na tela do PS2): texels quase
    // quadrados na tela (3% de diferença), um quadrado só, igual ao de entrada.
    const Psi2DQuad q = panel_quad(36, 100, 440, 248);
    Psi2DQuad out[9];
    float scale[2] = {1, 1};
    CHECK(psi_2d_nine_slice(q, f, kPs2PixelAspect, out, scale) == 1);
    CHECK(std::memcmp(&out[0], &q, sizeof(q)) == 0);
    CHECK(scale[0] == 0.0f && scale[1] == 0.0f);
    // Uma parte da textura (meio painel) nunca é cortada, mesmo esticada.
    const Psi2DQuad half = image_quad(36, 41, 220, 380, 0, 0, 255, 255, 512, 256, kPanelTex);
    CHECK(psi_2d_nine_slice(half, f, kPs2PixelAspect, out) == 1);
}

TEST(psi_2d_nine_slice_tiles_the_quad_and_keeps_square_corners) {
    const Psi2DFrame& f = *psi_2d_find_frame(kPanelTex);
    // A página de vídeo do PC: a mesma largura da pausa, 1,5 vez mais alta.
    const Psi2DQuad q = panel_quad(36, 41, 440, 380);
    Psi2DQuad p[9];
    float scale[2];
    CHECK(psi_2d_nine_slice(q, f, kPs2PixelAspect, p, scale) == 9);
    // Os pedaços cobrem o quadrado sem vão nem sobra, na tela e no ST.
    for (int r = 0; r < 3; ++r) {
        CHECK(p[r * 3].x0 == q.x0 && p[r * 3 + 2].x1 == q.x1);
        CHECK(p[r * 3].u0 == q.u0 && p[r * 3 + 2].u1 == q.u1);
        for (int c = 0; c < 2; ++c) {
            CHECK(p[r * 3 + c].x1 == p[r * 3 + c + 1].x0);
            CHECK(p[r * 3 + c].u1 == p[r * 3 + c + 1].u0);
        }
    }
    for (int c = 0; c < 3; ++c) {
        CHECK(p[c].y0 == q.y0 && p[6 + c].y1 == q.y1);
        CHECK(p[c].v0 == q.v0 && p[6 + c].v1 == q.v1);
        CHECK(p[c].y1 == p[3 + c].y0 && p[3 + c].y1 == p[6 + c].y0);
    }
    // Os cortes caem nos texels da tabela.
    CHECK_NEAR(texels_u(p[0]), f.inset[0], 1e-3);
    CHECK_NEAR(texels_u(p[2]), f.inset[2], 1e-3);
    CHECK_NEAR(texels_v(p[0]), f.inset[1], 1e-3);
    CHECK_NEAR(texels_v(p[6]), f.inset[3], 1e-3);
    // Cantos com texels quadrados na tela, na escala do eixo menos esticado (aqui x, a da pausa).
    const float sx_quad = (q.x1 - q.x0) * kPs2PixelAspect / 511.0f;
    for (const int k : {0, 2, 6, 8}) {
        const float sx = (p[k].x1 - p[k].x0) * kPs2PixelAspect / texels_u(p[k]);
        const float sy = (p[k].y1 - p[k].y0) / texels_v(p[k]);
        CHECK_NEAR(sx, sy, 1e-4);
        CHECK_NEAR(sx, sx_quad, 1e-4);
    }
    // As bordas de cima e de baixo têm a altura dos cantos; o meio estica só na vertical aqui.
    CHECK_NEAR(p[1].y1 - p[1].y0, p[0].y1 - p[0].y0, 1e-4);
    CHECK_NEAR((p[4].x1 - p[4].x0) * kPs2PixelAspect / texels_u(p[4]), sx_quad, 1e-4);
    CHECK((p[4].y1 - p[4].y0) / texels_v(p[4]) > 1.4f * sx_quad);
    // A escala dos cantos devolvida (pixels da tela do PS2 por texel).
    CHECK_NEAR(scale[0], sx_quad / kPs2PixelAspect, 1e-5);
    CHECK_NEAR(scale[1], sx_quad, 1e-5);
}

TEST(psi_2d_nine_slice_mirrors_and_clamps_the_corners) {
    const Psi2DFrame& f = *psi_2d_find_frame(kPanelTex);
    // Espelhado em u: a borda esquerda da tela mostra o canto direito da textura.
    Psi2DQuad m = panel_quad(36, 41, 440, 380);
    std::swap(m.u0, m.u1);
    Psi2DQuad p[9];
    CHECK(psi_2d_nine_slice(m, f, kPs2PixelAspect, p) == 9);
    CHECK(p[0].u0 > p[0].u1);
    CHECK_NEAR(texels_u(p[0]), f.inset[2], 1e-3);
    CHECK_NEAR(texels_u(p[2]), f.inset[0], 1e-3);
    // Larga e baixa (o eixo y manda): os cantos na escala de y, sem passar do quadrado.
    const Psi2DQuad wide = panel_quad(0, 200, 500, 60);
    CHECK(psi_2d_nine_slice(wide, f, kPs2PixelAspect, p) == 9);
    const float sy = 60.0f / 255.0f;
    CHECK_NEAR((p[0].y1 - p[0].y0) / texels_v(p[0]), sy, 1e-4);
    CHECK_NEAR((p[0].x1 - p[0].x0) * kPs2PixelAspect / texels_u(p[0]), sy, 1e-4);
    // Estreita e alta (o eixo x manda, com uma escala minúscula): os pedaços continuam no quadrado.
    const Psi2DQuad tiny = panel_quad(0, 0, 10, 60);
    CHECK(psi_2d_nine_slice(tiny, f, kPs2PixelAspect, p) == 9);
    for (const Psi2DQuad& piece : p) CHECK(piece.x1 >= piece.x0 - 1e-5f && piece.y1 >= piece.y0 - 1e-5f);
    CHECK_NEAR(p[2].x1, 10.0, 1e-5);
}

TEST(ui_hd_frame_slices_share_one_raster_of_the_asset) {
    UiHdConfig c;
    CHECK(ui_hd_parse_config(R"({"images": [{"texture": "03000070", "rect": [0, 0, 512, 256], "file": "svg/p.svg"}]})", c,
                             nullptr));
    UiHdEnv env;
    env.width = 3840;
    env.height = 2160;
    env.texture_size = [](uint32_t hash, int& w, int& h) {
        if (hash != kPanelTex) return false;
        w = 512;
        h = 256;
        return true;
    };
    const Psi2DQuad quads[1] = {panel_quad(36, 41, 440, 380)};
    const Psi2DList list{0, 1};
    const UiHdFrame fr = ui_hd_build(quads, 1, &list, 1, c, env);
    CHECK(fr.batches.size() == 1);
    if (fr.batches.empty()) return;
    const UiHdBatch& b = *fr.batches.begin()->second;
    CHECK(b.images.size() == 9);
    if (b.images.size() != 9) return;
    for (const UiHdImage& im : b.images) {
        CHECK(im.asset == 0);
        CHECK(im.asset_w == b.images[0].asset_w && im.asset_h == b.images[0].asset_h);
    }
    // A substituta inteira cobre o painel: de 0 a 1 nos pedaços das pontas, contínua entre eles.
    CHECK_NEAR(b.images[0].u0, 0.0, 1e-5);
    CHECK_NEAR(b.images[8].u1, 1.0, 1e-5);
    CHECK_NEAR(b.images[8].v1, 1.0, 1e-5);
    CHECK_NEAR(b.images[0].u1, b.images[1].u0, 1e-6);
    // Rasterizada na escala dos cantos: um canto aparece com os pixels dela 1:1 (até o degrau de 12,5%).
    const float corner_w = b.images[0].x1 - b.images[0].x0;
    const float raster_w = float(b.images[0].asset_w) * (b.images[0].u1 - b.images[0].u0);
    CHECK(raster_w >= corner_w - 0.5f && raster_w <= corner_w * 1.13f + 1.0f);
}

// ---------------------------------------------------------------- símbolos de botão por aparelho

TEST(ui_hd_prompt_settings_parse_and_resolve) {
    UiHdPromptSet s = UiHdPromptSet::Ps2;
    CHECK(ui_hd_parse_prompt_set("auto", &s) && s == UiHdPromptSet::Auto);
    CHECK(ui_hd_parse_prompt_set(" Keyboard", &s) && s == UiHdPromptSet::Keyboard);
    CHECK(ui_hd_parse_prompt_set("XBOX", &s) && s == UiHdPromptSet::Xbox);
    CHECK(ui_hd_parse_prompt_set("ps2", &s) && s == UiHdPromptSet::Ps2);
    CHECK(!ui_hd_parse_prompt_set("switch", &s) && s == UiHdPromptSet::Ps2);
    for (const UiHdPromptSet x : {UiHdPromptSet::Auto, UiHdPromptSet::Ps2, UiHdPromptSet::Keyboard, UiHdPromptSet::Xbox}) {
        UiHdPromptSet back;
        CHECK(ui_hd_parse_prompt_set(ui_hd_prompt_set_name(x), &back) && back == x);
    }
    // Auto segue o aparelho; o escolhido vale por cima; sem aviso, teclado.
    CHECK(ui_hd_prompt_effective(UiHdPromptSet::Auto, UiHdPromptSet::Xbox) == UiHdPromptSet::Xbox);
    CHECK(ui_hd_prompt_effective(UiHdPromptSet::Auto, UiHdPromptSet::Keyboard) == UiHdPromptSet::Keyboard);
    CHECK(ui_hd_prompt_effective(UiHdPromptSet::Auto, UiHdPromptSet::Auto) == UiHdPromptSet::Keyboard);
    CHECK(ui_hd_prompt_effective(UiHdPromptSet::Ps2, UiHdPromptSet::Xbox) == UiHdPromptSet::Ps2);
    // O gancho: o aparelho em uso muda o conjunto em vigor só com Auto.
    const UiHdPromptSet old_setting = UiHd_PromptSetting(), old_device = UiHd_PromptDevice();
    UiHd_SetPromptSetting(UiHdPromptSet::Auto);
    UiHd_SetPromptDevice(UiHdPromptSet::Xbox);
    CHECK(ui_hd_prompt_effective() == UiHdPromptSet::Xbox);
    UiHd_SetPromptDevice(UiHdPromptSet::Keyboard);
    CHECK(ui_hd_prompt_effective() == UiHdPromptSet::Keyboard);
    UiHd_SetPromptSetting(UiHdPromptSet::Xbox);
    CHECK(ui_hd_prompt_effective() == UiHdPromptSet::Xbox);
    UiHd_SetPromptSetting(old_setting);
    UiHd_SetPromptDevice(old_device);
}

TEST(ui_hd_xbox_names_match_the_pad_conversion) {
    // Para cada símbolo, o botão do Xbox escolhido, passado por psi_pad_convert (o controle do PC para o do
    // PS2), aperta o botão do PS2 que o símbolo desenha.
    const struct {
        uint8_t code;
        uint16_t ps2;
    } kSymbols[] = {{'A', kPs2Cross},  {'B', kPs2Triangle}, {'X', kPs2Circle}, {'Y', kPs2Square}, {'L', kPs2L1},
                    {'R', kPs2R1},     {'C', kPs2L2},       {'D', kPs2R2},     {'S', kPs2Start},  {'T', kPs2Select},
                    {'E', kPs2R3},     {'F', kPs2L3},       {'V', kPs2Up},     {'H', kPs2Left},   {'W', kPs2Right}};
    const struct {
        const char* xbox;
        const char* pad;
    } kNames[] = {{"A", "pad_a"},       {"B", "pad_b"},       {"X", "pad_x"},       {"Y", "pad_y"},
                  {"LB", "pad_lb"},     {"RB", "pad_rb"},     {"LT", "pad_lt"},     {"RT", "pad_rt"},
                  {"LS", "pad_ls"},     {"RS", "pad_rs"},     {"Menu", "pad_start"}, {"View", "pad_back"},
                  {"DpadV", "pad_up"},  {"DpadH", "pad_left"}, {"Dpad", "pad_right"}};
    for (const bool triggers : {true, false}) {
        for (const auto& s : kSymbols) {
            const char* name = ui_hd_xbox_name(s.code, triggers);
            CHECK(name != nullptr);
            if (name == nullptr) continue;
            const char* pad = nullptr;
            for (const auto& n : kNames) {
                if (std::strcmp(n.xbox, name) == 0) pad = n.pad;
            }
            CHECK(pad != nullptr);
            InputCode in;
            CHECK(pad != nullptr && parse_input_name(pad, &in) && in.is_pad());
            PadState st;
            st.connected = true;
            if (in.code < 16) st.buttons = uint16_t(1u << in.code);
            if (in.code == kPadInLT) st.lt = 255;
            if (in.code == kPadInRT) st.rt = 255;
            uint16_t buttons = 0;
            uint8_t sticks[4];
            psi_pad_convert(st, 0, triggers, &buttons, sticks);
            if ((buttons & s.ps2) == 0) std::printf("  %c -> %s não aperta o botão do PS2\n", s.code, name);
            CHECK((buttons & s.ps2) != 0);
        }
    }
    CHECK(ui_hd_xbox_name('I', true) == nullptr && ui_hd_xbox_name('J', true) == nullptr);
    CHECK(ui_hd_symbol_button('J') == UiHdPadButton::Check && ui_hd_symbol_button('?') == UiHdPadButton::None);
}

TEST(ui_hd_keyboard_prompts_follow_the_bindings) {
    Bindings b = default_bindings();
    // Menus: os comandos do menu com as teclas padrão (input_bindings: space, esc, tab, enter, setas).
    CHECK(ui_hd_prompt_keys('A', false, b, 0, true) == L"Space");
    CHECK(ui_hd_prompt_keys('B', false, b, 0, true) == L"Esc");
    CHECK(ui_hd_prompt_keys('X', false, b, 0, true) == L"Tab");
    CHECK(ui_hd_prompt_keys('S', false, b, 0, true) == L"Enter");
    CHECK(ui_hd_prompt_keys('V', false, b, 0, true) == L"↑↓");
    CHECK(ui_hd_prompt_keys('H', false, b, 0, true) == L"←→");
    CHECK(ui_hd_prompt_keys('W', false, b, 0, true) == L"↑↓←→");
    CHECK(ui_hd_prompt_keys('Y', false, b, 0, true) == L"E");  // o quadrado (menu_square): o E, como o DOSSIER do mapa Modern
    CHECK(ui_hd_prompt_keys('J', false, b, 0, true).empty());  // o visto não é botão
    // Religar muda o que aparece.
    bind(b, find_bind_command("menu_select"), key_input('F'));
    CHECK(ui_hd_prompt_keys('A', false, b, 0, true) == L"F");
    bind(b, find_bind_command("menu_up"), key_input('I'));
    CHECK(ui_hd_prompt_keys('V', false, b, 0, true) == L"I/↓");
    // O mapa de botões do jogo (estilo 0, "NightFire": a aba CONTROLS da pausa diz X agachar, L1 mirar, R1
    // atirar, analógico esquerdo andar e o direito olhar): as teclas desses comandos.
    const Bindings d = default_bindings();
    CHECK(ui_hd_prompt_keys('A', true, d, 0, true) == L"C");
    CHECK(ui_hd_prompt_keys('L', true, d, 0, true) == L"Mouse 2");
    CHECK(ui_hd_prompt_keys('R', true, d, 0, true) == L"Mouse 1");
    CHECK(ui_hd_prompt_keys('F', true, d, 0, true) == L"WASD");
    CHECK(ui_hd_prompt_keys('E', true, d, 0, true) == L"Mouse");
    CHECK(ui_hd_prompt_keys('S', true, d, 0, true) == L"Enter");
    // As armas (o direcional para os lados no estilo 0) estão na roda: os dois sentidos viram uma tecla só; a visão
    // (select) é o N.
    CHECK(ui_hd_prompt_keys('H', true, d, 0, true) == L"Wheel" && ui_hd_prompt_keys('T', true, d, 0, true) == L"N");
    CHECK(ui_hd_key_label(wheel_input(true)) == L"Wheel↓");
    // Os gatilhos não dependem do lado: com RB/LB = R1/L1 o resultado é o mesmo.
    CHECK(ui_hd_prompt_keys('R', true, d, 0, false) == L"Mouse 1");
    CHECK(ui_hd_key_label(key_input(0x26)) == L"↑" && ui_hd_key_label(key_input('Q')) == L"Q");
    CHECK(ui_hd_keycap_width(0.0f, 20.0f) == 20.0f && ui_hd_keycap_width(40.0f, 20.0f) == 52.0f);
}

TEST(ui_hd_keyboard_prompt_widens_the_line_and_keeps_it_centred) {
    TextWorld world;
    char str[] = "AB ~X CH";
    char fmt[] = "\xff\x02\xfe\x03";  // centrado
    sprite* s = world.text(str, fmt, 200, 40, 0x80808080u, 0, 0);
    world.draw(s);
    const auto& quads = g_psi_frame.quads2d;
    CHECK(quads.size() == 5);
    if (quads.size() != 5) return;
    std::vector<Psi2DList> lists(g_psi_frame.lists2d.begin(), g_psi_frame.lists2d.end());
    UiHdConfig c;
    UiHdEnv env;
    env.width = 1024;
    env.height = 768;  // 4:3: x da janela = 2 x x do PS2
    env.cap_height[2] = 12.0f;
    // Sem prompts: o símbolo é a imagem de sempre, nada se move.
    const UiHdFrame plain = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
    const UiHdBatch& p = *plain.batches.begin()->second;
    CHECK(p.keys.empty() && p.images.size() == 1 && p.texts.size() == 1);
    // Teclado: "Backspace" (9 caracteres x 10 pixels) numa tecla bem mais larga que o símbolo.
    env.prompts = UiHdPromptSet::Keyboard;
    env.key_label = [](uint8_t code, bool) { return code == 'X' ? std::wstring(L"Backspace") : std::wstring(); };
    env.measure = [](const std::wstring& label, float) { return 10.0f * float(label.size()); };
    const UiHdFrame kf = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
    const UiHdBatch& k = *kf.batches.begin()->second;
    CHECK(k.keys.size() == 1 && k.images.empty() && k.texts.size() == 1);
    if (k.keys.size() != 1 || k.texts.empty() || p.texts.empty()) return;
    const UiHdKey& key = k.keys[0];
    CHECK(key.label == L"Backspace");
    const float vp_scale = 2.0f;
    const float sym_w = (quads[2].x1 - quads[2].x0) * vp_scale;
    const float key_w = key.x1 - key.x0;
    const float key_h = key.y1 - key.y0;
    CHECK_NEAR(key_w, ui_hd_keycap_width(90.0f, key_h), 1e-3);
    const float extra = key_w - sym_w;
    CHECK(extra > 40.0f);
    // A linha cresce "extra": o trecho antes recua metade, o depois anda metade (o bloco continua centrado).
    const UiHdText& t0 = p.texts[0];
    const UiHdText& t1 = k.texts[0];
    CHECK(t1.lines.size() == 2 && t0.lines.size() == 2);
    if (t1.lines.size() != 2 || t0.lines.size() != 2) return;
    CHECK_NEAR(t1.lines[0].x0 - t0.lines[0].x0, -extra * 0.5, 1e-3);
    CHECK_NEAR(t1.lines[1].x0 - t0.lines[1].x0, extra * 0.5, 1e-3);
    CHECK_NEAR(key.x0 - p.images[0].x0, -extra * 0.5, 1e-3);
    // A tecla fica entre os dois trechos, com a mesma folga do símbolo antes do texto seguinte.
    CHECK_NEAR(t1.lines[1].x0 - key.x1, t0.lines[1].x0 - p.images[0].x1, 1e-3);
}

TEST(ui_hd_pad_prompts_swap_the_symbol_image) {
    TextWorld world;
    char str[] = "AB ~X CH";
    char fmt[] = "\xff\x02";
    world.draw(world.text(str, fmt, 10, 40, 0x80808080u, 0, 0));
    const auto& quads = g_psi_frame.quads2d;
    std::vector<Psi2DList> lists(g_psi_frame.lists2d.begin(), g_psi_frame.lists2d.end());
    UiHdConfig c;
    CHECK(ui_hd_parse_config(R"({"images": [{"texture": "03000075", "file": "svg/sheet.svg"}],
        "prompts": {"ps2": {"X": "svg/circle.svg"}, "xbox": {"B": "svg/b.svg", "A": "svg/a.svg"}}})", c, nullptr));
    CHECK(c.images.size() == 4 && c.prompt_ps2.count("X") == 1 && c.prompt_xbox.size() == 2);
    // As entradas dos símbolos ficam fora da busca por textura.
    CHECK(c.find_image(0x03000075, nullptr, UiHdImageKey::kAny, UiHdImageKey::kAny) == 0);
    UiHdEnv env;
    env.width = 512;
    env.height = 448;
    for (const UiHdPromptSet set : {UiHdPromptSet::Ps2, UiHdPromptSet::Xbox}) {
        env.prompts = set;
        const UiHdFrame f = ui_hd_build(quads.data(), quads.size(), lists.data(), lists.size(), c, env);
        const UiHdBatch& b = *f.batches.begin()->second;
        CHECK(b.images.size() == 1 && b.keys.empty());
        if (b.images.empty()) continue;
        const int want = set == UiHdPromptSet::Ps2 ? c.prompt_ps2.at("X") : c.prompt_xbox.at("B");  // círculo = B
        CHECK(b.images[0].asset == want);
        CHECK(b.images[0].u0 == 0.0f && b.images[0].u1 == 1.0f && b.images[0].v1 == 1.0f);
        // O símbolo continua no lugar dele.
        CHECK(b.images[0].x0 == quads[2].x0 && b.images[0].x1 == quads[2].x1);
    }
}

TEST(ui_hd_committed_map_covers_every_symbol_and_menu_piece) {
    const std::string dir = std::string(NF_SOURCE_DIR) + "/hd_ui/";
    FILE* f = nullptr;
    CHECK(fopen_s(&f, (dir + "map.json").c_str(), "rb") == 0 && f != nullptr);
    if (f == nullptr) return;
    std::string json;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    std::fclose(f);
    UiHdConfig c;
    CHECK(ui_hd_parse_config(json, c, nullptr));
    // Todos os símbolos da fonte (specialchar do ACTION.ELF: A B C D E F H I J L R S T V W X Y) no conjunto
    // do PS2, e todos os botões do Xbox que eles viram, com os gatilhos dos dois lados.
    for (const char code : std::string("ABCDEFHIJLRSTVWXY")) {
        CHECK(c.prompt_ps2.count(std::string(1, code)) == 1);
        for (const bool triggers : {true, false}) {
            if (const char* name = ui_hd_xbox_name(uint8_t(code), triggers)) CHECK(c.prompt_xbox.count(name) == 1);
        }
    }
    // O painel dos menus inteiro (a moldura de psi_2d) e as peças vistas no inventário têm substituta.
    const float panel[4] = {0, 0, 511, 255};
    CHECK(c.find_image(kPanelTex, panel, UiHdImageKey::kAny, UiHdImageKey::kAny) >= 0);
    const struct {
        uint32_t hash;
        float span[4];
    } kPieces[] = {
        {0x030000db, {0, 0, 127, 127}},   {0x03000042, {0, 0, 16, 2}},     {0x03000042, {0, 14, 16, 16}},
        {0x03000042, {8, 7, 10, 9}},      {0x03000042, {16, 0, 32, 16}},   {0x03000042, {32, 0, 48, 16}},
        {0x03000042, {48, 0, 64, 16}},    {0x03000042, {48, 16, 64, 32}},  {0x03000042, {25, 19, 27, 21}},
        {0x03000042, {31, 19, 33, 21}},   {0x03000042, {38, 19, 39, 20}},  {0x03000140, {0, 0, 255, 127}},
        {0x0300014c, {1, 1, 256, 256}},
    };
    for (const auto& piece : kPieces) {
        const int e = c.find_image(piece.hash, piece.span, UiHdImageKey::kAny, UiHdImageKey::kAny);
        if (e < 0) std::printf("  sem substituta: %08x %g,%g\n", piece.hash, double(piece.span[0]), double(piece.span[1]));
        CHECK(e >= 0);
    }
}
