#pragma once
// Os clarões (docs/driving.md seção 29): o RGlareManager (RVehicleParticle.obj; RGlareManager::fgThis_RGlareManager),
// o GlareBlinkBrightness, as trilhas 'gg' da galeria (GFXGallery::GLARE_Interpolate e GLARE_Draw) e o
// DrawGroupDrawEffects do mundo (RDirectorQueue.obj).
//
// Como o PS2 faz: o construtor lê do "Render:Glare" (data\tuning\Render\Glare\<pista>.tun ou default.tun) os 80 tipos de
// clarão (o índice "Glare ID" de 0 a 79: a textura, o giro, a cor e o tamanho do halo e do raio (spike) e o Z Bias) e
// tira das texturas o UV de cada um no atlas 'flar' do contexto 0 (CreateUVsFromTexIDs: 0 e 1 metades de cima, 2..17
// quartos da metade de baixo). A cada quadro desenhado chegam até 128 clarões (um registro de 0x40 bytes, o
// CARP::GlareData: a posição, as bandeiras, o tipo e a intensidade): o AddGlare (o GLARE_Draw das trilhas 'gg' da galeria,
// três vezes com um tremor de ±1 m quando a chave pede, e o RLensFlareManager) e o AddModelGlare (as lâmpadas das
// instâncias do mundo, DrawGroupDrawEffects, e dos objetos de cena, RSceneObj::RenderEffect: a intensidade vezes o
// pisca-pisca, recusado se a distância² / a intensidade passa de 1,6; a posição pela matriz da instância; os
// direcionais só de frente para a câmera). O DrawGlares (no DrawEffects, com o Z sem escrita) põe dois retângulos do
// RenderSpritesRot por clarão (o halo e o raio): na posição puxada para o olho em Z Bias x tamanho, o tamanho x 1,4, a
// cor do tipo com o alfa 0x20 (ou a intensidade x 32 nos direcionais), o giro do tipo vezes a distância (mod 2 pi, menos
// 3,93), na mistura aditiva Cs As + Cd com o teste de alfa GEQUAL 0x70 (AFAIL só RGB), e esvazia a lista.
//
// Divergências (só do port, também nas funções): o PS2 monta o registro do GLARE_Draw na pilha e o AddGlare copia os 64
// bytes, com o lixo da pilha em +0x16 e +0x20..+0x3f (que o DrawGlares não lê): o port escreve zeros; o EAGL::DrawArray
// do DrawGlares vira uma lista de retângulos para o PC (GlareDrawList).

#include <cstdint>
#include <vector>

#include "driving/fx/tuning_db.h"

namespace drv {

// Um clarão (CARP::GlareData, 0x40 bytes; também o lugar da lista do RGlareManager).
struct GlareData {
    float pos[4];          // +0x00
    uint32_t flags;        // +0x10 0x20 direcional, 0x400 pisca, 0x800 e 0x4000 a forma do pisca
    uint8_t kind;          // +0x14 o tipo do efeito (2 clarão; 5 luz dinâmica, 6 efeito da galeria, 8)
    uint8_t unknown15[3];  // +0x15
    int32_t type;          // +0x18 o tipo do clarão (0..79)
    float intensity;       // +0x1c
    float dir[3];          // +0x20 a direção (os direcionais)
    uint32_t blink_start;  // +0x2c o RTimeData::fTime do começo do pisca
    float blink_rate;      // +0x30
    float blink_duty;      // +0x34
    float blink_base;      // +0x38
    float blink_amp;       // +0x3c
};
static_assert(sizeof(GlareData) == 0x40);

// Um tipo de clarão (0x40 bytes em +0x2038).
struct GlareType {
    float halo_uv[4];      // +0x00 u, v, lado, "Halo Spin"
    float spike_uv[4];     // +0x10 u, v, lado, "Spike Spin"
    uint32_t halo_col;     // +0x20 "Halo Col"
    uint32_t spike_col;    // +0x24 "Spike Col"
    float halo_size;       // +0x28 "Halo Size"
    float spike_size;      // +0x2c "Spike Size"
    int32_t halo_tex;      // +0x30 "Halo Tex"
    int32_t spike_tex;     // +0x34 "Spike Tex"
    float zbias;           // +0x38 "Z Bias" (2, e 0,3 nos tipos 33, 34 e 52, antes do .tun)
    uint32_t unknown3c;
};
static_assert(sizeof(GlareType) == 0x40);

// O RGlareManager (0x3438 bytes).
struct RGlareManager {
    uint32_t vtable;              // +0x00 0x394f68
    uint32_t unknown04[3];
    uint32_t parameters[4];       // +0x10 o "Parameters" dos dois DrawArray (zerados)
    GlareData glares[0x80];       // +0x20
    int32_t count;                // +0x2020
    int32_t enabled;              // +0x2024 1
    uint32_t unknown2028[4];
    GlareType types[0x50];        // +0x2038
};
static_assert(sizeof(RGlareManager) == 0x3438);

// O que o DrawGlares manda ao EAGL::DrawArray (o RenderSpritesRot): dois retângulos por clarão.
struct GlareDrawList {
    std::vector<float> coords;    // (x, y, z, tamanho)
    std::vector<float> uvuv;      // (u, v, lado, giro)
    std::vector<uint32_t> colours;
    uint64_t alpha = 0, test = 0; // o ALPHA_1 e o TEST_1 do GeoPrimState 0x339010
    float control[4] = {};        // o Control (0x3e3f40)
};

// Os globais do RVehicleParticle.obj que o DrawGlares usa.
struct GlareGlobals {
    uint8_t state[0x80] = {};     // 0x339010 o GeoPrimState (lista de triângulos, ALPHA 0x48, PRIM | 0x58, AREF 0x70)
    int32_t buffer = 0;           // 0x3390a0 a metade dos vetores de desenho
    int32_t index = 0;            // 0x339094 a variável do índice "Glare ID"
    float control[4] = {};        // 0x3e3f40
    float coords[2][0x100][4] = {};   // 0x3e1f40
    float uvuv[2][0x100][4] = {};     // 0x3dff40
    uint32_t colours[2][0x100] = {};  // 0x3df630
    int32_t enabled = 1;          // 0x333784 os clarões do mundo ligados (DrawGroupDrawEffects)
};
GlareGlobals& glare_globals();
RGlareManager& glare_manager();

// [driving] 0x001f14b0 RGlareManager::RGlareManager (com o "Render:Glare" aberto).
void RGlareManager_Construct(RGlareManager& m, const TuningReader& tun);
// [driving] 0x001f19f0 RGlareManager::CreateUVsFromTexIDs
void RGlareManager_CreateUVsFromTexIDs(RGlareManager& m);
// [driving] 0x001f1f88 RGlareManager::AddGlare(CARP::GlareData &, float) (o float não é lido)
void RGlareManager_AddGlare(RGlareManager& m, const GlareData& g);
// [driving] 0x001f1b60 GlareBlinkBrightness(CARP::GlareData &) (`time` o RTimeData::fTime)
float GlareBlinkBrightness(const GlareData& g, uint32_t time);
// [driving] 0x001f1d18 RGlareManager::AddModelGlare(float distância², CARP::GlareData &, MATRIX4 &): `camera_fwd` a linha
// 2 da câmera do render (o RRenderer +4).
void RGlareManager_AddModelGlare(RGlareManager& m, float dist2, const GlareData& g, const float matrix[4][4],
                                 const float camera_fwd[4], uint32_t time);
// [driving] 0x001f2058 RGlareManager::DrawGlares(bool): `eye` o olho da câmera do render, `control_y` o 66 / (fov x
// margem); a lista do quadro em `out` (nula: só anda).
void RGlareManager_DrawGlares(RGlareManager& m, const float eye[4], float control_y, GlareDrawList* out);

// [driving] 0x00235ea0 GFXGallery::GLARE_Interpolate(float t, Glares *a, Glares *b, Glares *out): o 'gg' da galeria
// (+0 o tipo, +4 a intensidade, +8 o tremor).
void GFXGallery_GLARE_Interpolate(float t, const uint32_t a[3], const uint32_t b[3], uint32_t out[3]);
// [driving] 0x00235d50 GFXGallery::GLARE_Draw(UGroup *, Glares *, COORD4 *): o AddGlare do registro montado (com o
// tremor do gerador de 16 bits 0x33ba74/0x33ba78).
void GFXGallery_GLARE_Draw(const uint32_t glare[3], const float pos[4]);
// O gerador do tremor (0x33ba74 a semente, 0x33ba78 o multiplicador).
struct GlareJitter {
    uint32_t seed = 0, mul = 0;
};
GlareJitter& glare_jitter();
// [driving] 0x0024b328 GSystem::RANDOM_Startup(unsigned int): o multiplicador 0x075bcd15 e a semente 0x3ade68b1 andada
// (n % 500) vezes (s = s x mul & 0xffff); o GSystem::CURATOR_Init o chama com 0x642f2b (175 vezes).
void GSystem_RANDOM_Startup(uint32_t n);

// [driving] 0x001ad1a0 DrawGroupDrawEffects(float distância, DrawGroup *, MATRIX4 &): os efeitos de um artigo do mundo
// desenhado (`effects` o +4 do artigo, a lista de CARP::GlareData terminada pelo tipo 0): os clarões (tipo 2, com os
// clarões do mundo ligados) e os efeitos da galeria (tipo 6: GFX::Draw, contado).
void DrawGroupDrawEffects(float dist, const GlareData* effects, const float matrix[4][4], const float camera_fwd[4],
                          uint32_t time);

struct GlareStats {
    uint64_t added = 0, model = 0, model_far = 0, model_back = 0, full = 0, drawn = 0, sprites = 0, gallery = 0;
    uint64_t world_effects = 0, world_gfx = 0, unknown_effects = 0;
};
GlareStats& glare_stats();

}  // namespace drv
