#pragma once
// Interface de plataforma da EuroLand ("psi*"), implementada para PC.
//
// A lógica do jogo chama 106 funções de plataforma (lista em out/layers_report.txt, gerada por
// tools/layers.py; resumo em docs/engine.md). Este arquivo cresce conforme elas são implementadas.
// Já implementadas aqui ou em outros arquivos:
//   relógio          psiInitTimeIn100ths, psiGetTimeIn100ths, VBlankCount  (este arquivo)
//   seno da VU0      PS2Sinf, PS2Sinf2, PS2Sinf3                            (engine/vecmath.h)
//   cópia alinhada   amemcpy                                                (este arquivo)
//   heap             psiMem_Init, psiMem_Info                               (este arquivo)
//   recursos         psiCreateMapTextures, psiCreateEntityGfx...            (psi_gfx.h)
//   desenho          psiPreDraw, psiUseCamera, psiDrawObjectMatrix...       (psi_draw.h)

#include <cstddef>
#include <cstdint>

#include "platform/psi_loading.h"  // a tela de carga (psiStartLoadingScreen, psiEndLoadingScreen...)
#include "platform/psi_movie.h"  // os filmes (psiStartBackgroundMovie, psiMovieFinished...)
#include "platform/psi_sound.h"  // SFX* (volumes e modo do som)

// Contador de VBlank do PS2 (incrementado pela interrupção a 60 Hz no NTSC). No PC é derivado do
// relógio de alta resolução por psi_update_clock().
extern uint32_t VBlankCount;

// Chamada uma vez por volta do laço principal.
void psi_update_clock();
// Só do port: o relógio em VBlanks com a fração do VBlank em curso (no último psi_update_clock), para a
// interpolação entre ticks (render/frame_interp.h).
double psi_clock_vblanks();
// Só do port: troca o relógio do Windows por um simulado (--fixed-video-hz: um monitor de N Hz, cada quadro de
// vídeo avança 60 / N VBlanks). Depois disso psi_update_clock não muda mais nada.
void psi_set_clock_vblanks(double vblanks);

// [decomp] psiInitTimeIn100ths / psiGetTimeIn100ths: cronômetro em centésimos de segundo.
void psiInitTimeIn100ths();
int psiGetTimeIn100ths();

// amemcpy (0x0011f5c0 no PS2): memcpy otimizado para blocos alinhados a 16 bytes.
void amemcpy(void* dst, const void* src, size_t size);

// [decomp] psiMem_Init: entrega o heap do gerenciador de memória (engine/memory.h). No PS2 é o
// maior malloc que termina abaixo de 0x1fe0000; no PC é uma reserva de psiMemHeapSize bytes.
extern uint32_t psiMemHeapSize;
void psiMem_Init(uint8_t** heap, uint32_t* size);
struct MEMINFO_tag;
void psiMem_Info(MEMINFO_tag* info);

// ---- ainda não implementadas no PC (stubs em psi_stubs.cpp, registradas em pending.h) ----
void psiStopAllRumble();
void psiPreGameRun();  // névoa, partículas e empacotamento de texturas na VRAM
void PS2_SFXWaitTillFinished();
// A posição da imagem na TV (o ajuste de tela das opções, P_SCREENADJUST_Handler; o perfil a grava). No PC só é
// guardada: a janela não tem margem de TV, nada no desenho a lê.
extern int32_t PS2ScreenXPos;      // 0x0030c75c
extern int32_t PS2ScreenYPos;
extern uint8_t PS2UpdateScreenPos;
void psiAdjustScreenPos(int32_t dx, int32_t dy);
void psiSetScreenPos(int32_t x, int32_t y);
void psiGetScreenPos(int32_t* x, int32_t* y);
void psiRestoreScreenPos();


// Começo e fim de cada Game_Run: no PS2 contam o tempo da lógica e atualizam o som; no PC limpam a
// VRAM na primeira vez e atualizam o som.
void psiPreGame_Run();
void psiPostGame_Run();
// Ainda não implementada no PC (o psiDecompressWoman está em platform/psi_woman.h).
void psiLaunchDriving(uint8_t* data, uint32_t size);
void SFXUpdate();
