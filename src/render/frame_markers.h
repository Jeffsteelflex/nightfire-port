#pragma once
// Os marcadores do quadro (só do port; sem GPU, testável): os itens sem modelo que o registro do Game_Draw
// (g_psi_frame, platform/psi_draw.h) põe na lista do renderizador, na posição em que o jogo os pediu, e o lugar
// dos objetos provisórios.
//
//   - a passada de névoa (psiDrawFogPass, de psiPostSolid): antes de draws[fog_at];
//   - as limpezas do Z (psiClearZ): antes de draws[i] para cada i de clear_z_at. O Game_Draw limpa o Z antes da
//     lista 2 das câmeras 0-3 (a arma e os braços em primeira pessoa, o clarão do tiro): no PS2 eles nunca são
//     cortados pelo cenário e só se testam entre si. A limpeza de psiPreDraw (antes de qualquer desenho) não vira
//     item: o renderizador já começa o quadro com a profundidade limpa.
// No mesmo índice a névoa vem antes da limpeza: ela lê o Z, e no Game_Draw psiPostSolid vem antes de psiClearZ.

#include <cstddef>
#include <vector>

#include "platform/psi_draw.h"
#include "render/renderer.h"

namespace nf {

DrawItem fog_pass_item(const PsiFrame& frame);
DrawItem clear_depth_item();

// Os marcadores que vêm antes de frame.draws[index], na ordem do jogo; index = frame.draws.size(): os do fim.
void add_frame_markers(const PsiFrame& frame, size_t index, std::vector<DrawItem>& out);

// Onde os objetos provisórios (opacos, que o jogo não conhece) entram: antes do primeiro item misturado, da passada
// de névoa ou da limpeza do Z (assim nunca cobrem a arma em primeira pessoa).
std::vector<DrawItem>::iterator provisional_insert_point(std::vector<DrawItem>& items);

}  // namespace nf
