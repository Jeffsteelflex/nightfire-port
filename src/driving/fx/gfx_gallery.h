#pragma once
// A galeria dos efeitos do jogo de carro (GFX.obj e GFXGallery.obj do DRIVING.ELF; docs/driving.md seção 25).
//
// A galeria é o data\gfx\data\gall1.gal do MISC.VIV (as ilhas usam ears_gall1/sav_gall1: GSystem::GetGalleryName), um
// UGroup "GGAL" com 106 efeitos "GANM" (as "Giotto" da EA: "Ex_Small", "Cr_Dmg1", "Pr_Dstry"...): cada um com as
// bandeiras ('Flgs': 1 uma vez só, 8 grudado na instância, 0x10 com a velocidade de quem disparou), o nome, o número
// de emissores de partículas ('NUMP') e a vida em quadros ('LIFE'), e as trilhas: 'gp' (um emissor de partículas: as
// chaves 'gk' de 0xf4 bytes trazem a posição e o quatérnio do emissor no efeito e o GenericParticleData inteiro do
// tipo, que o PARTICLE_Add põe na biblioteca na primeira vez), 'gs' (um som), 'gg' (um clarão do RGlareManager), 'gd'
// (destroços do RDebris).
//
// GFX::Trigger põe um efeito numa "tela" (Canvas, 0x90 bytes; 512 numa lista, as ativas e as livres pelos índices): a
// posição e o quatérnio, ou a instância de um RSceneObj que ele segue. A cada quadro desenhado o PS2 manda as telas ao
// recorte do mundo (GFXGallery::CULL_*, pelo RRenderWorldCamera::PerformCulling) e o GFX::Update (o FRAME_Draw, do
// RRenderWorldCamera::DrawEffects) anda cada tela: o quadro dela, a posição, e o CANVAS_Draw, que renova o sistema de
// partículas de cada emissor (PARTICLE_Draw: o emissor no mundo, a direção e o lado pelo quatérnio da chave, a
// distância à câmera, o RParticleSystemManager::AddOrRefresh); uma tela "uma vez só" para depois de LIFE quadros, e
// sai da lista quando os sistemas dela se apagam (CANVAS_HasStopped).
//
// Portado do MIPS do PS2; conferido na parte 21 do nf_driving_check (tests/driving_check_fx.cpp).
//
// Divergências (só do port, nas funções): os sons das trilhas 'gs' (o SOUND_Play, um ASound em laço), os clarões 'gg'
// (o RGlareManager) e os destroços 'gd' (o RDebris) não são portados: contados e anotados (gfx_stats()); a tela grava
// as bandeiras dela no meio de uma palavra de 64 bits (+0x68) como o PS2, e os bits que o CANVAS_Add não limpa ficam
// com o que a tela anterior deixou (o mesmo no port: as telas não são zeradas).

#include <cstdint>
#include <string>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/fx/particles.h"

namespace drv {

// A tela (GFXGallery::Canvas, 0x90 bytes).
struct GfxCanvas {
    float quat2[4];          // +0x00 o quatérnio local (com uma instância: o da instância)
    float pos2[3];           // +0x10 a posição local
    uint32_t ref;            // +0x1c (p32 UGroup) o efeito
    float quat[4];           // +0x20 o quatérnio no mundo
    float pos[3];            // +0x30 a posição no mundo (o FRAME_Draw a regrava a cada quadro)
    int32_t life;            // +0x3c 'LIFE'
    float extra[3];          // +0x40 a velocidade de quem disparou (o bit 0x10 das bandeiras)
    uint32_t extra_ptr;      // +0x4c
    uint32_t scene;          // +0x50 o RSceneObj seguido (0 nenhum)
    P32<ParticleEmitter> emitters;  // +0x54 NUMP emissores de 0x40 bytes
    int32_t instance;        // +0x58
    int32_t frame;           // +0x5c
    float unknown60;         // +0x60
    int32_t unknown64;       // +0x64
    int32_t age;             // +0x68 os quadros vividos
    uint16_t flags;          // +0x6c 0x400 fora da vista, 0x800 uma vez só, 0x1000 parou, 0x2000 suprimida,
                             //       0x4000 destroços já lançados, 0x8000 (o 12º argumento do CANVAS_Add)
    uint8_t nump;            // +0x6e
    uint8_t sticky;          // +0x6f o índice grudado (0xff nenhum)
    uint32_t sounds[8];      // +0x70 os ASound das trilhas 'gs' (o port não os cria)
};
static_assert(sizeof(GfxCanvas) == 0x90);

// GFXGallery (0x1430 bytes no PS2).
struct GfxGallery {
    struct Sticky {
        float pos[3];
        int32_t canvas;
    };
    Sticky sticky[64];            // +0x000 as posições grudadas (STICKY_UpdateAllIndexes)
    uint32_t sticky_used[2];      // +0x400 os bits das 64
    int32_t active[0x200];        // +0x408 os índices das telas ativas
    int32_t free_list[0x200];     // +0xc08 os livres
    uint32_t root = 0;            // +0x1408 (p32 UGroup) o GGAL
    uint32_t file = 0;            // +0x140c o arquivo
    struct Cached {
        uint32_t track;           // a trilha 'gp'
        uint32_t tag;             // a etiqueta dada na biblioteca
    };
    Cached cache[256];            // +0x1410 (no PS2 um bloco de 0x800 bytes)
    GfxCanvas canvases[0x200];    // +0x1414 (no PS2 um bloco de 0x12000 bytes)
    int32_t cache_count = 0;      // +0x1418
    int32_t active_count = 0;     // +0x141c
    int32_t free_count = 0x200;   // +0x1420
    int32_t cull_cursor = 0;      // +0x1424
    int32_t drawn = 0;            // +0x1428
    int32_t culled = 0;           // +0x142c
    std::vector<uint8_t> data;    // o gall1.gal (na arena: o UGroup guarda endereços p32)
};

// A galeria (GFX::fGallery); nula antes do GFX_Init.
GfxGallery* gfx_gallery();
// [driving] 0x00232ce0 GFX::Init (0x00233618 GFXGallery::GFXGallery): o arquivo da galeria (o port o recebe lido; vazio:
// uma galeria sem efeitos).
void GFX_Init(std::vector<uint8_t> file);
// [driving] 0x00232d28 GFX::Shutdown
void GFX_Shutdown();
// [driving] 0x00232f48 GFX::LookupReference(char *) (0x00232e18 e GFXGallery::GET_Canvas 0x002339e8): o efeito pelo nome
// (sem maiúsculas), ou 0.
uint32_t GFX_LookupReference(const char* name);
// [driving] 0x002330b0 GFX::Trigger(GiottoRef *, COORD3 &pos, COORD4 &quat, COORD3 &pos2, COORD4 &quat2, RSceneObj *,
// int instância, x): o 0x00232af8 com a galeria; o índice da tela + 1 (0 com as bandeiras 1, ou sem vaga).
int32_t GFX_Trigger(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3], const float quat2[4],
                    uint32_t scene, int32_t instance, uint32_t extra);
// [driving] 0x00233108 GFX::TriggerIncidental(GiottoRef *, COORD3 &, COORD4 &, COORD3 &, COORD4 &, RSceneObj *, int, x): o
// mesmo 0x00232af8 com o 1 do segundo argumento (docs/driving.md seção 26): a reserva da tela (o último do CANVAS_Add)
// é a bandeira 1 do efeito em vez de 0. Os impactos das balas (Shell::ApplyWorldEffects) e o EHitWindow o usam.
int32_t GFX_TriggerIncidental(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                              const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra);
// [driving] 0x00232e80 GFX::Update (o FRAME_Draw, sem o GHud::TheApp +0x2a0: o port não pausa a galeria)
void GFX_Update();
// [driving] 0x00234198 GFXGallery::CULL_Start; 0x002341a8 CULL_GetNextCanvas (pela 0x00232d58); 0x00234258
// CULL_SetCanvas (pela 0x00232d98): o recorte das telas pelo RRenderWorldCamera::CullModule.
void GFXGallery_CULL_Start(GfxGallery& g);
int32_t GFXGallery_CULL_GetNextCanvas(GfxGallery& g, float sphere[4], float* height, int32_t* check_far, float* far_scale);
void GFXGallery_CULL_SetCanvas(GfxGallery& g, int32_t item, int32_t culled);
// [driving] 0x002337f8 GFXGallery::PurgeAllEffects (pelo GFX::PurgeAllEffects 0x00232df0)
void GFXGallery_PurgeAllEffects(GfxGallery& g);

// As funções do GFXGallery que a parte 21 confere uma a uma.
// [driving] 0x00234d80 GFXGallery::CANVAS_Add
int32_t GFXGallery_CANVAS_Add(GfxGallery& g, uint32_t ref, uint32_t scene, int32_t instance, const float pos[4],
                              const float quat[4], const float pos2[4], const float quat2[4], uint32_t extra,
                              int32_t loop, int32_t with_extra, int32_t reserve);
// [driving] 0x002345d8 GFXGallery::FRAME_Draw
void GFXGallery_FRAME_Draw(GfxGallery& g);
// [driving] 0x00234818 GFXGallery::CANVAS_Draw
void GFXGallery_CANVAS_Draw(GfxGallery& g, GfxCanvas& c, const float pos[4], const float quat[4]);
// [driving] 0x002360c0 GFXGallery::PARTICLE_Draw: devolve se o sistema é "uma vez só".
uint32_t GFXGallery_PARTICLE_Draw(GfxGallery& g, uint32_t track, ParticleEmitter& e, const uint8_t* key,
                                  const float pos[4], const float quat[4], const float* velocity, int32_t frame,
                                  int32_t stopping, int32_t culled, int32_t keep);
// [driving] 0x00235ed0 GFXGallery::PARTICLE_Add: a etiqueta do tipo da trilha (a cópia do GenericParticleData da
// chave: a gravidade e o espalhamento x 127, o giro x 360; a etiqueta é o número de tipos + 5).
uint32_t GFXGallery_PARTICLE_Add(GfxGallery& g, uint32_t track, const uint8_t* data);
// [driving] 0x00235270 GFXGallery::CANVAS_HasStopped
bool GFXGallery_CANVAS_HasStopped(GfxGallery& g, GfxCanvas& c);
// [driving] 0x00233f88 GFXGallery::GET_EffectPosQuat (0x00233460: a posição e o quatérnio de uma tela)
bool GFXGallery_GET_EffectPosQuat(GfxGallery& g, uint32_t index, float pos[4], float quat[4]);
// [driving] 0x00234508 GFXGallery::GET_StickyEffectPos
bool GFXGallery_GET_StickyEffectPos(const GfxGallery& g, uint32_t index, float out[4]);
// [driving] 0x00234350 GFXGallery::STICKY_GetNewIndex, 0x002343e0 STICKY_UpdateAllIndexes, 0x002344d0
// STICKY_ReleaseIndex, 0x00234330 STICKY_ReleaseAllIndexes
int32_t GFXGallery_STICKY_GetNewIndex(GfxGallery& g, int32_t canvas);
void GFXGallery_STICKY_UpdateAllIndexes(GfxGallery& g);
void GFXGallery_STICKY_ReleaseIndex(GfxGallery& g, uint32_t index);
void GFXGallery_STICKY_ReleaseAllIndexes(GfxGallery& g);
// [driving] 0x00233f18 GFXGallery::GET_FirstIdleKey: os dados da primeira chave 'gk' de uma trilha.
const uint8_t* GFXGallery_GET_FirstIdleKey(uint32_t track);

// [driving] 0x002cbc00 VU0_v3quatrotate(q, v, out) e 0x002cbcf8 VU0_v4quatrotate_xlate(q, v, t, out): v girado por q
// (+ t), xyz; o w da saída fica.
void VU0_v3quatrotate(const float q[4], const float v[4], float out[4]);
void VU0_v4quatrotate_xlate(const float q[4], const float v[4], const float t[4], float out[4]);
// O produto de quatérnios inline (q x l) do GFXGallery (0x00233460, PARTICLE_Draw), na ordem da VU0.
void gfx_quat_mul(const float q[4], const float l[4], float out[4]);

// O que a galeria lê de fora (o nf_driving liga; nos testes, nulos).
struct GfxEnv {
    // RSceneObj::GetInstancePosition(int instância, MATRIX4 &, bool): a matriz da instância no mundo.
    bool (*instance_position)(uint32_t scene, int32_t instance, float m[4][4]) = nullptr;
    // A posição da câmera (o RRenderer::fgRenderer +0x40).
    float camera[4] = {0, 0, 0, 1};
};
GfxEnv& gfx_env();

// Os emissores de uma tela (no PS2 o UMemory::FastAlloc de NUMP x 0x40 e o FastFree): por padrão new[]/delete[]; a parte 21
// os põe num bloco crescente (a ordem dos endereços é a ordem do mapa dos sistemas, como no heap emulado).
struct GfxEmitterAlloc {
    ParticleEmitter* (*alloc)(uint32_t n) = nullptr;
    void (*free)(ParticleEmitter* p) = nullptr;
};
GfxEmitterAlloc& gfx_emitter_alloc();

struct GfxStats {
    uint64_t triggers = 0, refused = 0, lookups = 0, lookups_failed = 0, incidental = 0;
    uint64_t frames = 0, canvas_draws = 0, particle_draws = 0, particle_types = 0, stopped = 0;
    uint64_t sounds = 0, glares = 0, debris = 0;  // as trilhas não portadas (contadas)
    uint64_t guards = 0;
};
GfxStats& gfx_stats();

}  // namespace drv
