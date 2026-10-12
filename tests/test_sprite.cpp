// Testes da camada 2D: os sprites (sprite.cpp), o desenho deles na tela (View_DrawSprites, View_AddSprite,
// View_3DPoint2Screen) e o registro 2D da plataforma (psiDrawSprites, psi_2d_viewport), com texturas e
// uma fonte sintéticas.

#include <cstring>

#include "engine/font.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/sprite.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_2d.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kImage = 0x03000777, kFontTex = 0x03000778;
pstex_tag g_image_rec, g_font_rec;
maptex_tag g_image{&g_image_rec, 64, 32}, g_font_tex{&g_font_rec, 256, 64};
CHARDATA_tag g_glyphs[2];
FONT_tag g_font;
P32<char> g_bank[3];
uint32_t g_fix[1] = {0};
char kEmpty[] = "", kText[] = "AB";

// Heap, hashtable com uma imagem 64 x 32 (Tex 3) e a textura de uma fonte (Tex 9, em FontList), uma
// fonte de dois glifos e um idioma com a string 1 = "AB"; a câmera 0 em (0, 0) desenhando sprites.
struct SpriteWorld {
    alignas(16) viewer_tag viewer{};
    SpriteWorld() {
        psiMemHeapSize = 1u << 21;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        g_image_rec = pstex_tag{};
        g_image_rec.tex_index = 3;
        g_font_rec = pstex_tag{};
        g_font_rec.tex_index = 9;
        hashtable_additem(kImage, &g_image);
        hashtable_additem(kFontTex, &g_font_tex);
        font_debug_power_on();
        std::memset(FontTable, 0, sizeof(FontTable));
        g_glyphs[0] = CHARDATA_tag{};
        g_glyphs[0].code = 'A';
        g_glyphs[0].u = 16;
        g_glyphs[0].v = 8;
        g_glyphs[0].w = 10;
        g_glyphs[0].h = 12;
        g_glyphs[1] = g_glyphs[0];
        g_glyphs[1].code = 'B';
        g_glyphs[1].u = 32;
        g_font = FONT_tag{0, nullptr, 0x21, 0xff, 2, g_glyphs, 6.0f, 4.0f};
        FontTable[0] = {kFontTex, &g_font};
        psi_2d_debug_power_on();
        FontList[0] = nf::p32_u(&g_font_rec);
        txt_debug_power_on();
        g_bank[0] = kEmpty;
        g_bank[1] = kText;
        g_bank[2] = kText;
        Bank = g_bank;
        NumEntries = 3;
        FixupTable = g_fix;
        NumFixups = 1;
        sprite_debug_power_on();
        Sprite_InitLists();
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.frame = 77;
        viewer.sprites = 1;
        glb_viewer[0] = &viewer;
        psi_draw_debug_power_on();
        psiPreDraw();
    }
    ~SpriteWorld() {
        glb_viewer[0] = nullptr;
        txt_debug_power_on();
        sprite_debug_power_on();
        psi_2d_debug_power_on();
        psi_draw_debug_power_on();
        hashtable_debug_power_on();
    }
};

SpriteInfo image_info(int16_t x, int16_t y, uint32_t colour = 0x80808080u) {
    SpriteInfo info{};
    info.colour = colour;
    info.priority = 5;
    info.x = x;
    info.y = y;
    info.label = 0xffffffffu;
    info.hash = kImage;
    info.viewer = 0;
    return info;
}

}  // namespace

TEST(sprite_2d_viewport_is_centred_4_3) {
    Psi2DViewport v = psi_2d_viewport(1600, 900);
    CHECK(v.width == 1200.0f && v.height == 900.0f && v.left == 200.0f && v.top == 0.0f);
    CHECK(v.scale_x == 1200.0f / 512.0f && v.scale_y == 900.0f / 448.0f);
    v = psi_2d_viewport(1024, 768);
    CHECK(v.left == 0.0f && v.width == 1024.0f && v.height == 768.0f);
    v = psi_2d_viewport(800, 1000);  // mais estreita que 4:3: pela largura, centrada na vertical
    CHECK(v.width == 800.0f && v.height == 600.0f && v.top == 200.0f && v.left == 0.0f);
    // Retângulos: um comum fica na área 4:3; um da largura inteira da tela do PS2 vai de borda a borda
    // da janela larga (a altura continua a da área), e um de altura inteira numa janela estreita.
    v = psi_2d_viewport(1600, 900);
    const float hud[4] = {256.0f, 224.0f, 512.0f, 448.0f};
    float r[4];
    CHECK(!psi_2d_map_rect(v, 1600, 900, hud, r));
    CHECK(r[0] == 800.0f && r[1] == 450.0f && r[2] == 1400.0f && r[3] == 900.0f);
    const float border[4] = {0.0f, 0.0f, 512.0f, 64.0f};
    CHECK(psi_2d_map_rect(v, 1600, 900, border, r));
    CHECK(r[0] == 0.0f && r[2] == 1600.0f && r[1] == 0.0f && r[3] == 64.0f * 900.0f / 448.0f);
    const float wider[4] = {-8.0f, 100.0f, 520.0f, 200.0f};
    CHECK(psi_2d_map_rect(v, 1600, 900, wider, r) && r[0] == 0.0f && r[2] == 1600.0f);
    const float column[4] = {0.0f, 0.0f, 64.0f, 448.0f};
    CHECK(!psi_2d_map_rect(v, 1600, 900, column, r) && r[1] == 0.0f && r[3] == 900.0f);
    v = psi_2d_viewport(800, 1000);
    CHECK(psi_2d_map_rect(v, 800, 1000, column, r) && r[1] == 0.0f && r[3] == 1000.0f && r[2] == 100.0f);
    v = psi_2d_viewport(1024, 768);  // exatamente 4:3: nada a esticar
    CHECK(!psi_2d_map_rect(v, 1024, 768, border, r) && r[2] == 1024.0f);
}

TEST(sprite_create_image_and_text) {
    const SpriteWorld w;
    SpriteInfo info = image_info(10, 20);
    sprite* s = Sprite_Create2(&info);
    CHECK(s != nullptr && s->viewer == 0 && s->priority == 5 && s->created == 77);
    CHECK(s->texture == nf::p32_u(&g_image_rec));
    // Tamanho e recorte 0 = os da textura (o recorte é inclusivo: largura - 1).
    CHECK(s->tex_w == 64 && s->tex_h == 32 && s->w == 64 && s->h == 32 && s->uw == 63 && s->vh == 31);
    CHECK(s->x == 10 && s->y == 20 && s->text == nullptr && s->scale.x == 1.0f);
    CHECK(sprite_texture_hash(s) == kImage);
    info.w = 100;
    info.uw = 15;
    sprite* t = Sprite_Create2(&info);
    CHECK(t->w == 100 && t->h == 32 && t->uw == 15);
    // Texto: o rótulo, o formato e a medida (largura em uw, para alinhar).
    char fmt[] = "\xff\x01";
    SpriteInfo text{};
    text.colour = 0x7d6d59ffu;
    text.priority = 3;
    text.label = 1;
    text.format = fmt;
    text.x = 7;
    text.viewer = -1;
    sprite* u = Sprite_Create2(&text);
    CHECK(u != nullptr && u->text == kText && u->length == 2 && u->uw == 20 && u->vh == 19);
    CHECK(u->tex_w == 7);  // comportamento original: tex_w recebe a posição num texto
    Sprite_SetText(u, kText + 1, 0);
    CHECK(u->length == 1 && u->uw == 10);
    // Sprite_Delete devolve o nó (no começo da lista de livres; Sprite_Create tira do fim).
    const uint16_t used = SpriteList.used.count;
    Sprite_Delete(t);
    CHECK(SpriteList.used.count == used - 1 && SpriteList.free.first == &t->node);
}

TEST(sprite_build_list_sorts_and_counts_background) {
    const SpriteWorld w;
    const int8_t prio[] = {10, 60, 0, 55, -1};
    for (int8_t p : prio) {
        SpriteInfo info = image_info(0, 0);
        info.priority = uint16_t(uint8_t(p));
        info.flags = 0xf;
        Sprite_Create2(&info);
    }
    SpriteInfo hidden = image_info(0, 0, 0x80808000u);  // alfa 0
    Sprite_Create2(&hidden);
    SpriteInfo other = image_info(0, 0);
    other.viewer = 3;
    Sprite_Create2(&other);
    uint32_t a = 9, b = 9, c = 9;
    P32<sprite>* list = Sprite_BuildList(0, &a, &b, &c);
    CHECK(c == 4 && a == 0);
    CHECK(list[0]->priority == 60 && list[1]->priority == 55 && list[2]->priority == 10 && list[3]->priority == 0);
    // Comportamento original: count_b é o índice do último de fundo (>= 50), um a menos que eles.
    CHECK(b == 1);
    CHECK((list[0]->flags & 0xf) == 0);
}

TEST(sprite_draw_image_records_quads_with_origin) {
    SpriteWorld w;
    w.viewer.viewport[0] = 4.0f;
    w.viewer.viewport[2] = 8.0f;
    SpriteInfo info = image_info(10, 20, 0x80402080u);
    info.flags = SPRF_ADDITIVE;
    sprite* s = Sprite_Create2(&info);
    P32<sprite> list[1] = {s};
    View_DrawSprites(&w.viewer, list, 0, 1);
    CHECK(SprCnt == 0 && g_psi_frame.lists2d.size() == 1 && g_psi_frame.quads2d.size() == 1);
    const Psi2DQuad& q = g_psi_frame.quads2d[0];
    CHECK(q.x0 == 14.0f && q.y0 == 28.0f && q.x1 == 78.0f && q.y1 == 60.0f);
    // Meio texel fora das fontes; recorte de 63 texels a partir de 0.
    CHECK(q.u0 == 0.5f / 64.0f && q.u1 == 63.5f / 64.0f && q.v0 == 0.5f / 32.0f && q.v1 == 31.5f / 32.0f);
    // Cor como o GS recebe: r, g, b e alfa / 2; aditiva; filtro bilinear; Tex 3.
    CHECK(q.rgba[0] == 0x80 && q.rgba[1] == 0x40 && q.rgba[2] == 0x20 && q.rgba[3] == 0x40);
    CHECK(q.alpha == 0x48 && q.linear && q.texture == 3);
    CHECK(q.origin.kind == Psi2DKind::Image && q.origin.sprite == s && q.origin.texture_hash == kImage);
    // O desenho 2D é um PsiDrawCall sem modelo, sem teste nem escrita de Z (psiStartSprites).
    const PsiDrawCall& call = g_psi_frame.draws.back();
    CHECK(call.sprites2d == 0 && call.glist == nullptr && call.gs_test == 0xd && (call.gs_zbuf >> 32 & 1) == 1);
}

TEST(sprite_draw_shadow_and_tiles) {
    SpriteWorld w;
    SpriteInfo info = image_info(0, 0, 0x808080ffu);
    info.flags = SPRF_SHADOW;
    sprite* s = Sprite_Create2(&info);
    P32<sprite> list[1] = {s};
    View_DrawSprites(&w.viewer, list, 0, 1);
    // A sombra (preta, no lugar) vem antes; a imagem desce e anda clamp(64 / 64, 1, 5) = 1 pixel.
    CHECK(g_psi_frame.quads2d.size() == 2);
    const Psi2DQuad& shadow = g_psi_frame.quads2d[0];
    const Psi2DQuad& image = g_psi_frame.quads2d[1];
    CHECK(shadow.rgba[0] == 0 && shadow.rgba[3] == 0x7f && shadow.x0 == 0.0f && shadow.origin.pass == Psi2DPass::Shadow);
    CHECK(image.rgba[0] == 0x80 && image.x0 == 1.0f && image.y0 == 1.0f && image.origin.pass == Psi2DPass::Main);
    // Ladrilhos: (uw + 1) / largura da textura colunas; a segunda sai espelhada (u = uw, extensão -uw).
    psiPreDraw();
    s->flags = SPRF_TILE;
    s->w = 128;
    s->uw = 127;
    View_DrawSprites(&w.viewer, list, 0, 1);
    CHECK(g_psi_frame.quads2d.size() == 2);
    const Psi2DQuad& left = g_psi_frame.quads2d[0];
    const Psi2DQuad& right = g_psi_frame.quads2d[1];
    CHECK(left.x0 == 0.0f && left.x1 == 64.0f && left.u0 == 0.5f / 64.0f && left.u1 == 63.5f / 64.0f);
    CHECK(right.x0 == 64.0f && right.x1 == 128.0f && right.u0 == 63.5f / 64.0f && right.u1 == 0.5f / 64.0f);
}

TEST(sprite_draw_text_glyphs_from_font) {
    SpriteWorld w;
    char fmt[] = "\xff\x01";
    sprite* s = Sprite_CreateLink(0);
    Sprite_SetParams(s, 1, 100, 50, 0, 0x7d6d59ffu);
    Sprite_SetTextNFmt(s, kText, fmt, 0);
    P32<sprite> list[1] = {s};
    View_DrawSprites(&w.viewer, list, 0, 1);
    CHECK(g_psi_frame.quads2d.size() == 2);
    const Psi2DQuad& a = g_psi_frame.quads2d[0];
    const Psi2DQuad& b = g_psi_frame.quads2d[1];
    // Fonte: sem meio texel, canto de baixo recuado 1/16, filtro de ponto, textura da fonte.
    CHECK(a.x0 == 100.0f && a.x1 == 110.0f - 1.0f / 16.0f && a.u0 == 16.0f / 256.0f && a.u1 == 26.0f / 256.0f);
    CHECK(!a.linear && a.texture == 9 && a.alpha == 0x44);
    CHECK(a.rgba[0] == 0x7d && a.rgba[1] == 0x6d && a.rgba[2] == 0x59 && a.rgba[3] == 0x7f);
    CHECK(b.x0 == 110.0f && b.u0 == 32.0f / 256.0f);
    // A linha de base: y + y_offset - (altura + y_offset do primeiro glifo).
    CHECK(a.y0 == 38.0f);
    CHECK(a.origin.kind == Psi2DKind::Glyph && a.origin.glyph == 'A' && a.origin.font == 1 && a.origin.sprite == s);
    CHECK(b.origin.glyph == 'B' && b.origin.char_index == 2 && b.origin.text == kText);
    // Contorno (SPRF_OUTLINE): quatro cópias deslocadas antes de cada texto, com a cor 2.
    psiPreDraw();
    s->flags = SPRF_OUTLINE;
    s->colour2 = 0x00000080u;
    View_DrawSprites(&w.viewer, list, 0, 1);
    CHECK(g_psi_frame.quads2d.size() == 10);
    CHECK(g_psi_frame.quads2d[0].x0 == 99.0f && g_psi_frame.quads2d[0].origin.pass == Psi2DPass::Outline);
    CHECK(g_psi_frame.quads2d[0].rgba[0] == 0 && g_psi_frame.quads2d[9].origin.pass == Psi2DPass::Main);
}

TEST(sprite_add_sprite_flushes_at_63) {
    SpriteWorld w;
    for (int i = 0; i < 63; ++i) View_AddSprite(0)->texture = 0;
    CHECK(SprCnt == 63 && g_psi_frame.quads2d.empty());
    SPRITE_DRAW* d = View_AddSprite(0);
    CHECK(d == &SprBuffList[0] && SprCnt == 1 && g_psi_frame.quads2d.size() == 63);
    // Reservando 2: descarrega quando sobram menos de 3 livres.
    SprCnt = 61;
    View_AddSprite(2);
    CHECK(SprCnt == 1);
    SprCnt = 0;
}

TEST(sprite_3d_point_to_screen) {
    alignas(16) static viewer_tag v{};
    v.view = MATRIX{};
    for (int k = 0; k < 4; ++k) v.view.m[k][k] = 1.0f;
    v.pos = {0.0f, 0.0f, 0.0f, 1.0f};
    v.proj_x = 100.0f;
    v.proj_y = 100.0f;
    v.width = 512.0f;
    v.height = 448.0f;
    glb_viewer[2] = &v;
    _VECTOR out{};
    const _VECTOR ahead{0.0f, 0.0f, 10.0f, 1.0f}, right{1.0f, 2.0f, 10.0f, 1.0f}, behind{0.0f, 0.0f, -4.0f, 1.0f};
    CHECK(View_3DPoint2Screen(&ahead, &out, 2) == 1 && out.x == 256.0f && out.y == 224.0f && out.z == 10.0f);
    // x da vista positivo vai para a esquerda da tela; y positivo para baixo.
    CHECK(View_3DPoint2Screen(&right, &out, 2) == 1 && out.x == 246.0f && out.y == 244.0f);
    CHECK(View_3DPoint2Screen(&behind, &out, 2) == -1 && out.z == 4.0f);
    CHECK(View_3DPoint2Screen(&ahead, &out, 3) == 0 && View_3DPoint2Screen(nullptr, &out, 2) == 0);
    glb_viewer[2] = nullptr;
}
