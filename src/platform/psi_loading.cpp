// A tela de carga do lado da plataforma. Ver psi_loading.h.
//
// Divergências (só do PC): CreateLoadingBlob monta quadrados do registro 2D (Psi2DQuad) em vez de cadeias de DMA no
// heap; a VBlankInt não existe (psi_loading_pump conta os VBlanks pelo relógio de parede enquanto o carregador roda);
// o que vai para a tela passa pelo gancho do main (g_psi_loading_hook).

#include "platform/psi_loading.h"

#include <windows.h>

#include <cstring>

#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "platform/psi_gfx.h"
#include "platform/psi_shard.h"

uint8_t PS2CacheLoadingBlobs = 0;
int8_t BlobToDraw = 0;
int8_t BlobTime = 0;
int8_t BlobDir = 1;
int32_t BlobTexture = -1;
int32_t BlobTextWidth = 0;
int32_t BlobTextHeight = 0;
void (*g_psi_loading_hook)(PsiLoadingEvent event) = nullptr;
PsiLoadingStats g_psi_loading_stats;

namespace {
// O relógio de parede das bolinhas (psi_loading_pump) e o começo da tela (para o tempo dela).
LARGE_INTEGER pump_t0{};
int64_t pump_vblanks = 0;
LARGE_INTEGER screen_t0{};

double seconds_since(const LARGE_INTEGER& t0) {
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    return double(now.QuadPart - t0.QuadPart) / double(f.QuadPart);
}
}  // namespace

void psi_loading_debug_power_on() {
    PS2CacheLoadingBlobs = 0;
    BlobToDraw = 0;
    BlobTime = 0;
    BlobDir = 1;
    BlobTexture = -1;
    BlobTextWidth = BlobTextHeight = 0;
    pump_vblanks = 0;
    g_psi_loading_stats = PsiLoadingStats{};
}

// [decomp] 0x001249c0 ResetLoadingBlobs
void ResetLoadingBlobs() {
    BlobToDraw = 0;
    BlobDir = 1;
    BlobTime = 0;
}

// [decomp] 0x0010b9f8 psiStartLoadingScreen
// No PS2: PS2SafeCleanup (espera o GS e libera os pacotes dos dois buffers), ResetLoadingBlobs e PS2LSActive = 1 (o
// próximo Mem_Init monta as bolinhas). No PC também: o gancho mostra a tela que o ResetMap_GenLoadScreen registrou.
void psiStartLoadingScreen() {
    PS2SafeCleanup();
    ResetLoadingBlobs();
    PS2LSActive = 1;
    ++g_psi_loading_stats.screens;
    QueryPerformanceCounter(&screen_t0);
    QueryPerformanceCounter(&pump_t0);
    pump_vblanks = 0;
    if (g_psi_loading_hook != nullptr) g_psi_loading_hook(PsiLoadingEvent::Screen);
}

// [decomp] 0x0010ba28 psiEndLoadingScreen
void psiEndLoadingScreen() {
    if (PS2LSActive != 0) g_psi_loading_stats.last_ms = seconds_since(screen_t0) * 1000.0;
    PS2LSActive = 0;
    if (g_psi_loading_hook != nullptr) g_psi_loading_hook(PsiLoadingEvent::End);
}

// [decomp] 0x0011f270 psiAllocateLoadingBlobs
// Do Mem_Init: com a tela de carga começada (PS2LSActive 1), as sete cadeias das bolinhas (no PC os quadrados saem de
// psi_loading_blob na hora do desenho, nada vai para o heap) e PS2LSActive 2 (a VBlankInt passa a desenhá-las).
void psiAllocateLoadingBlobs() {
    if (PS2LSActive != 1) return;
    ++g_psi_loading_stats.blobs;
    PS2LSActive = 2;
}

// [decomp] 0x0011f2e8 CacheLoadingBlobs
// A textura das bolinhas (0x3000175): no PS2, o tamanho dela (Tex), a largura do espaço de texturas em que está, a
// área dentro dele e o endereço na VRAM (psiFont_SetTexture). No PC: o índice de Tex e o tamanho.
void CacheLoadingBlobs() {
    void* item = hashtable_getitem(0x3000175);
    if (item == nullptr) return;
    uint32_t rec;
    std::memcpy(&rec, item, 4);
    const pstex_tag* tex = nf::p32_ptr<const pstex_tag>(rec);
    if (tex == nullptr || tex->tex_index >= kMaxPS2Textures) return;  // só no port: o original não confere
    const PS2TEX_tag& t = Tex[tex->tex_index];
    BlobTextWidth = t.width;
    BlobTextHeight = t.height;
    BlobTexture = tex->tex_index;
}

// CreateLoadingBlob (0x0011bf70), no PC: a cadeia n são seis SPRITE do GS de 16 x 16 pixels, de x = 55 + 16 i e
// y = 309 a 325 (XYZ2 0x370 + 0x100 i, 0x1350 a 0x1450 em 12.4), UV 0..16 na área da textura (CLAMP_1 REGION_REPEAT
// do tamanho dela), os n primeiros com RGBAQ 0x8029a0ee e os outros com 0x800c315c, TEX0 MODULATE com o alfa da
// textura, filtro de ponto (TEX1 0), ALPHA_1 0x64 com FIX 0x80 ((Cs - Cd) * 1.0 + Cd: a cor sem mistura) e sem
// teste de alfa.
void psi_loading_blob(int n, Psi2DQuad out[kLoadingBlobCells]) {
    const float su = BlobTextWidth > 0 ? 16.0f / float(BlobTextWidth) : 1.0f;
    const float sv = BlobTextHeight > 0 ? 16.0f / float(BlobTextHeight) : 1.0f;
    for (int i = 0; i < kLoadingBlobCells; ++i) {
        Psi2DQuad& q = out[i];
        q = Psi2DQuad{};
        q.x0 = 55.0f + 16.0f * float(i);
        q.x1 = q.x0 + 16.0f;
        q.y0 = 309.0f;
        q.y1 = 325.0f;
        q.u0 = q.v0 = 0.0f;
        q.u1 = su;
        q.v1 = sv;
        const uint32_t rgba = n <= i ? 0x800c315cu : 0x8029a0eeu;
        q.rgba[0] = uint8_t(rgba);
        q.rgba[1] = uint8_t(rgba >> 8);
        q.rgba[2] = uint8_t(rgba >> 16);
        q.rgba[3] = uint8_t(rgba >> 24);
        q.texture = BlobTexture;
        q.alpha = 0x64u | (uint64_t(0x80) << 32);
        q.linear = false;
        q.origin.texture_hash = 0x3000175;
    }
}

// A parte de VBlankInt (0x00124088) com PS2LSActive == 2: BlobTime (com sinal) desconta 1; ao ficar negativo volta a 10
// e BlobToDraw anda BlobDir; em 0 a direção vira +1, em 6 vira -1.
bool psi_loading_vblank() {
    if (PS2LSActive != 2) return false;
    const int8_t before = BlobToDraw;
    BlobTime = int8_t(BlobTime - 1);
    if (BlobTime < 0) {
        BlobTime = 10;
        BlobToDraw = int8_t(BlobToDraw + BlobDir);
        ++g_psi_loading_stats.steps;
    }
    if (BlobToDraw == 0) BlobDir = 1;
    if (BlobToDraw == 6) BlobDir = -1;
    return BlobToDraw != before;
}

void psi_loading_pump() {
    ++g_psi_loading_stats.pumps;
    if (PS2LSActive != 2) return;
    const int64_t vblanks = int64_t(seconds_since(pump_t0) * 60.0);
    bool changed = false;
    while (pump_vblanks < vblanks) {
        ++pump_vblanks;
        changed = psi_loading_vblank() || changed;
    }
    if (changed && g_psi_loading_hook != nullptr) g_psi_loading_hook(PsiLoadingEvent::Blob);
}
