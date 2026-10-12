#pragma once
// Camada 2D em alta definição: o UiHdEnv do quadro a partir das estruturas do jogo (o tamanho das texturas
// 03xxxxxx pela tabela de hashcodes, a identidade dos sprites do HUD dos jogadores (pane e índice; a mira
// é a pane -1) e a altura das maiúsculas das três fontes do jogo).

#include "render/ui_hd.h"

namespace nf {

UiHdEnv ui_hd_game_env(int width, int height);

}  // namespace nf
