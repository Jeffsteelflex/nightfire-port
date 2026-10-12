#pragma once
// A tela de carga (ResetMap_GenLoadScreen, engine/resetmap.cpp) do lado da plataforma: psiStartLoadingScreen,
// psiEndLoadingScreen, psiAllocateLoadingBlobs, CacheLoadingBlobs, CreateLoadingBlob, ResetLoadingBlobs e a parte da
// VBlankInt que anima as "bolinhas" durante a carga.
//
// No PS2: o ResetMap_GenLoadScreen monta os sprites da lista 99 (a imagem da fase em 512 x 448, o rótulo 0x3c1, o nome
// da missão e uma dica ao acaso) e os desenha quatro vezes na câmera 10 (psiPreDraw ... psiPostDraw: os dois buffers
// de quadro ficam com a tela); o primeiro psiPreDraw, com PS2CacheLoadingBlobs, guarda a textura das bolinhas
// (CacheLoadingBlobs: 0x3000175, a área dela no espaço de texturas da VRAM). psiStartLoadingScreen põe PS2LSActive em
// 1; o Mem_Init seguinte (o reinício da carga da fase) chama psiAllocateLoadingBlobs, que monta sete cadeias de DMA
// (CreateLoadingBlob(n): seis quadrados de 16 x 16 da textura em x = 55 + 16 i, y = 309..325, os n primeiros na cor
// 0x8029a0ee e o resto em 0x800c315c, ALPHA_1 0x64 com FIX 0x80 = a cor da textura sem mistura) e põe PS2LSActive em 2.
// Então a cada VBlank a VBlankInt desconta BlobTime (um byte com sinal): ao passar de 0 volta a 10 e BlobToDraw anda
// BlobDir (vai e volta entre 0 e 6, 11 VBlanks por passo), e manda a cadeia BlobToDraw direto para o buffer que está
// na tela. A carga da fase (o leitor de DVD) leva segundos; psiEndLoadingScreen (o fim do ResetMap_Load) zera
// PS2LSActive e o jogo volta a desenhar. Não há tempo mínimo: a tela fica o tempo que a carga levar.
//
// No PC a carga inteira cabe num tick e nada é desenhado nela (o main pula os quadros de vídeo em GF_LOAD_MAP). Os
// ganchos deixam o main mostrar o que o PS2 mostra:
//   - Wipe (PS2WipeVram, no psiResetResources do começo da carga): a VRAM zerada, a tela preta;
//   - Screen (psiStartLoadingScreen): o registro 2D que o GenLoadScreen acabou de desenhar (g_psi_frame) é a tela de
//     carga; o main a desenha, guarda uma cópia (as texturas da fase anterior vão sumir no reinício seguinte) e a
//     mostra;
//   - Blob (psi_loading_pump, chamado pelo laço do carregador): passaram VBlanks no relógio de parede e BlobToDraw
//     mudou; o main volta a cópia e desenha os quadrados de psi_loading_blob(BlobToDraw);
//   - End (psiEndLoadingScreen).
// Sem gancho (testes, nf_loader_check) só o estado anda.

#include <cstdint>

#include "platform/psi_2d.h"

extern uint8_t PS2CacheLoadingBlobs;   // 0x0030ca1c: o próximo psiPreDraw guarda a textura das bolinhas
extern int8_t BlobToDraw;              // 0x0030d720: a cadeia que a VBlankInt desenha (0..6)
extern int8_t BlobTime;                // 0x0030d721: VBlanks até o próximo passo
extern int8_t BlobDir;                 // 0x0030d722: +1 ou -1
// CacheLoadingBlobs: a textura das bolinhas. No PS2, o endereço dela na VRAM e a área no espaço de texturas; no PC, o
// índice de Tex (-1: não achada) e o tamanho.
extern int32_t BlobTexture;
extern int32_t BlobTextWidth;
extern int32_t BlobTextHeight;

constexpr int kLoadingBlobs = 7;       // LoadingBlobs[7]
constexpr int kLoadingBlobCells = 6;   // quadrados por cadeia

enum class PsiLoadingEvent : uint8_t { Wipe, Screen, Blob, End };
// Só do PC: o gancho do main (nulo: nenhum).
extern void (*g_psi_loading_hook)(PsiLoadingEvent event);

void psiStartLoadingScreen();
void psiEndLoadingScreen();
void psiAllocateLoadingBlobs();
void CacheLoadingBlobs();
void ResetLoadingBlobs();
// A parte da VBlankInt com PS2LSActive == 2: um VBlank das bolinhas. Devolve true se BlobToDraw mudou.
bool psi_loading_vblank();
// Só do PC: os quadrados da cadeia n (CreateLoadingBlob(n)) na tela do PS2, com a textura de BlobTexture.
void psi_loading_blob(int n, Psi2DQuad out[kLoadingBlobCells]);
// Só do PC: chamado pelo laço do carregador durante a carga (engine/resetmap.cpp, load_pack). Com as bolinhas ligadas
// (PS2LSActive == 2), roda um psi_loading_vblank por VBlank do relógio de parede (60 Hz) desde a última chamada e avisa
// o gancho (Blob) quando a cadeia muda.
void psi_loading_pump();

// Só do PC: contagens para o relatório das capturas e o nf_loader_check.
struct PsiLoadingStats {
    uint32_t screens = 0;    // psiStartLoadingScreen
    uint32_t wipes = 0;      // PS2WipeVram
    uint32_t blobs = 0;      // psiAllocateLoadingBlobs que montaram as cadeias
    uint32_t steps = 0;      // passos de BlobToDraw (psi_loading_vblank)
    uint32_t pumps = 0;      // psi_loading_pump
    double last_ms = 0.0;    // a última tela de carga: do psiStartLoadingScreen ao psiEndLoadingScreen
};
extern PsiLoadingStats g_psi_loading_stats;

// Só para testes: o estado inicial.
void psi_loading_debug_power_on();
