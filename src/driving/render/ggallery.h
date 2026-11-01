#pragma once
// A galeria do HUD das missões de carro (GGallery, GFXGallery.obj do DRIVING.ELF; docs/driving.md seção 28) e as
// funções do GSystem que ela usa (MATH_TransformPoints, COLOR_Interpolate, COLOR_ReduceAlpha).
//
// O arquivo é o hudNTSC.gal do MISC.VIV, um UGroup "GGAL": as texturas 'tx#0'..'tx#14' (SHPS; o 'MAIN' de cada uma vira
// um EAGL::TAR), a fonte (FONT_Load: data\loading\loading.sfn) e os "canvases" GANM (hud_base, hud_msg, pauseN,
// paus_err), cada um com os seus "puppets" (os grupos 'gb' bitmaps/sprites, 'gt' textos, 'qd' quadrados: um 'Name' e
// as chaves 'gk#12288', de 0x80 bytes cada no hudNTSC.gal). Uma chave guarda a posição (x, y, z), o quadro, o
// quatérnio, a escala, a cor (0xAARRGGBB, metade na conversão para o GS), a textura e, a partir de +0x30, o registro do
// sprite (o retângulo do UV, o tamanho, as repetições, o espelhamento).
//
// O estado (+0x60: a posição, +0x70 o quatérnio, +0x80 a escala; +0x50 a cor do GS; +0x90 a textura) é o da última
// chave posta (BITMAP_SetState, SPRITE_SetState, FONT_SetState, QUAD_SetState); os Draw* desenham pelo módulo Draw
// (driving/render/hud_gfx.h) e pela biblioteca FONT (driving/render/ea_font.h).
//
// Portado do MIPS do PS2 com o leiaute do PS2 (GGalleryData, 0xc0 bytes); conferido na parte 24 do nf_driving_check.
// Divergências (só do port): as texturas são números de imagem do port no lugar dos EAGL::TAR (+0x10), a fonte (+0xa8) é
// um EaFont do port; o RRenderer (+0x54 a altura, +0x58 o modo de TV, +0x5c a tela larga) é o hud_renderer().

#include <cstdint>
#include <functional>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/render/ea_font.h"
#include "platform/ptr32.h"

namespace drv {

struct GGalleryData {
    uint32_t gallery = 0;          // +0x00 (p32 UGroup) o GGAL
    uint32_t file = 0;             // +0x04
    uint32_t pad08[2] = {};
    uint32_t textures[15] = {};    // +0x10 (o port: o número da imagem)
    int32_t texture_count = 0;     // +0x4c
    uint32_t colour = 0x7f7f7f7f;  // +0x50 a cor do GS da chave
    uint32_t pad54[3] = {};
    float pos[4] = {0, 0, 0, 1};   // +0x60
    float quat[4] = {0, 0, 0, 1};  // +0x70
    float scale[4] = {1, 1, 1, 1}; // +0x80
    uint32_t texture = 0;          // +0x90
    uint32_t pad94[3] = {};
    uint32_t a0[2] = {};           // +0xa0 (o 0x0039d178)
    P32<EaFont> font;              // +0xa8
    uint32_t font_file = 0;        // +0xac
    int32_t custom_state = 0;      // +0xb0 o estado do desenho é de quem chama (sem o STATE_SetDefaultRenderState)
    uint32_t padb4[3] = {};
};
static_assert(sizeof(GGalleryData) == 0xc0);

// O RRenderer::fgRenderer que o HUD lê.
struct HudRendererInfo {
    int32_t width = 640;   // +0x50
    int32_t height = 448;  // +0x54
    int32_t mode = 0;      // +0x58
    int32_t wide = 0;      // +0x5c
};
HudRendererInfo& hud_renderer();

// [driving] 0x00236738 GGallery::GGallery(char *) (o arquivo lido; a imagem do 'MAIN' de cada 'tx#N' por `image`)
void GGallery_Construct(GGalleryData& g, uint8_t* file,
                        const std::function<uint32_t(const uint8_t* ssh, uint32_t size)>& image);
// [driving] 0x00236b48 GGallery::DataIndex
uint32_t GGallery_DataIndex(int32_t a, uint32_t b);
// [driving] 0x00236b58 GGallery::GET_Gallery
uint32_t GGallery_GET_Gallery(const GGalleryData& g);
// [driving] 0x00236b60 GGallery::GET_Canvas: o subgrupo com o 'Name' (sem maiúsculas), ou 0.
uint32_t GGallery_GET_Canvas(const GGalleryData& g, uint32_t group, const char* name);
// [driving] 0x00236c20 GGallery::GET_Name
uint32_t GGallery_GET_Name(const GGalleryData& g, uint32_t group, uint32_t child);
// [driving] 0x00236cc0 GGallery::GET_Puppet: as chaves do subgrupo com o 'Name', ou 0.
uint32_t GGallery_GET_Puppet(const GGalleryData& g, uint32_t canvas, const char* name);
// [driving] 0x00236de8 GGallery::GET_PuppetAndGroup
uint32_t GGallery_GET_PuppetAndGroup(const GGalleryData& g, uint32_t canvas, const char* name, uint32_t* group);
// [driving] 0x00236f20 GGallery::GET_Texture
uint32_t GGallery_GET_Texture(const GGalleryData& g, int32_t index);
// [driving] 0x00236f30 GGallery::GET_Key
uint32_t GGallery_GET_Key(const GGalleryData& g, uint32_t puppet, int32_t index);
// [driving] 0x00237078 GGallery::GET_FirstIdleKey
uint32_t GGallery_GET_FirstIdleKey(const GGalleryData& g, uint32_t group);
// [driving] 0x002370e8 GGallery::Get_InterpolatedKey(float, Keys *saída, Keys *a, Keys *b)
void GGallery_Get_InterpolatedKey(float t, uint8_t* out, const uint8_t* a, const uint8_t* b);
// [driving] 0x00237278 GGallery::STATE_Reset
void GGallery_STATE_Reset(GGalleryData& g);
// [driving] 0x00237308 GGallery::STATE_SetDefaultRenderState
void GGallery_STATE_SetDefaultRenderState(GGalleryData& g);
// [driving] 0x00237350 GGallery::BITMAP_SetState
void GGallery_BITMAP_SetState(GGalleryData& g, const uint8_t* key);
// [driving] 0x00237410 GGallery::BITMAP_Draw2D
void GGallery_BITMAP_Draw2D(GGalleryData& g, const uint8_t* key);
// [driving] 0x002377d8 GGallery::FONT_SetState
void GGallery_FONT_SetState(GGalleryData& g, const uint8_t* key);
// [driving] 0x002378a0 GGallery::FONT_SetScale
void GGallery_FONT_SetScale(GGalleryData& g, float sx, float sy);
// [driving] 0x002378b8 GGallery::FONT_Load (o arquivo lido e a imagem da fonte do port)
void GGallery_FONT_Load(GGalleryData& g, std::vector<uint8_t> file, uint32_t image);
// [driving] 0x00237928 GGallery::FONT_Draw(Text *, char *): o texto do registro (+4 o rótulo do .loc) se `text` é nulo.
void GGallery_FONT_Draw_Text(GGalleryData& g, const uint8_t* key, const char* text);
// [driving] 0x00237a50 GGallery::FONT_Draw(float x, float y, char *, unsigned int cor)
void GGallery_FONT_Draw_Colour(GGalleryData& g, float x, float y, const char* text, uint32_t colour);
// [driving] 0x00237b80 GGallery::FONT_Draw(float x, float y, char *)
void GGallery_FONT_Draw(GGalleryData& g, float x, float y, const char* text);
// [driving] 0x00237ca8 GGallery::FONT_Width / 0x00237ce0 FONT_Height (o FONT_getrecta)
float GGallery_FONT_Width(const GGalleryData& g, const char* text);
float GGallery_FONT_Height(const GGalleryData& g, const char* text);
// [driving] 0x00238188 GGallery::QUAD_SetState
void GGallery_QUAD_SetState(GGalleryData& g, const uint8_t* key);
// [driving] 0x002381c8 GGallery::QUAD_Draw(float alfa, Quad *)
void GGallery_QUAD_Draw(GGalleryData& g, float alpha, const uint8_t* quad);
// [driving] 0x00238408 GGallery::QUAD_Draw2D(float x, float y, float w, float h, unsigned int cor)
void GGallery_QUAD_Draw2D(GGalleryData& g, float x, float y, float w, float h, uint32_t colour);
// [driving] 0x00238518 GGallery::QUAD_Interpolate(float t, Quad *a, Quad *b, Quad *saída)
void GGallery_QUAD_Interpolate(float t, const uint8_t* a, const uint8_t* b, uint8_t* out);
// [driving] 0x002386c8 GGallery::SPRITE_SetState
void GGallery_SPRITE_SetState(GGalleryData& g, const uint8_t* key);
// [driving] 0x00238d40 GGallery::SPRITE_Draw2D(int x, int y, Keys *, Sprite *)
void GGallery_SPRITE_Draw2D_At(GGalleryData& g, int32_t x, int32_t y, const uint8_t* key, const uint8_t* sprite);
// [driving] 0x002391e8 GGallery::SPRITE_Draw2D(Keys *): na posição da chave.
void GGallery_SPRITE_Draw2D(GGalleryData& g, const uint8_t* key);
// [driving] 0x002393b8 GGallery::SPRITE_Flush
void GGallery_SPRITE_Flush(GGalleryData& g);
// [driving] 0x00239498 GGallery::TRANSFORM_Vertices(COORD4 *, int, COORD3 pos, COORD4 quat, COORD3 escala)
void GGallery_TRANSFORM_Vertices(float (*v)[4], int32_t n, const float pos[3], const float quat[4], const float scale[3]);

// [driving] 0x0024b238 GSystem::MATH_TransformPoints(COORD4 &ponto, COORD3 &translação, COORD4 &quatérnio, COORD3 &escala)
void GSystem_MATH_TransformPoints(float point[4], const float trans[4], const float quat[4], const float scale[4]);
// [driving] 0x0024b4a8 GSystem::COLOR_Interpolate(unsigned int a, unsigned int b, float t)
uint32_t GSystem_COLOR_Interpolate(uint32_t a, uint32_t b, float t);
// [driving] 0x0024b640 GSystem::COLOR_ReduceAlpha(unsigned int &, float)
void GSystem_COLOR_ReduceAlpha(uint32_t& c, float f);
// A conversão da cor da chave (0xAARRGGBB) para o GS (os bytes RGBA pela metade).
uint32_t gallery_colour(uint32_t c);

}  // namespace drv
