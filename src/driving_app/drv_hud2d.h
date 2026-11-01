#pragma once
// O 2D do GS no nf_driving (docs/driving.md seção 28): os lotes de driving/render/gs2d.h (as legendas dos filmes, o HUD
// das galerias) em DrvFxBatch no recorte, com a tela do PS2 (640 x 448) na área 4:3 da janela pela altura, e as imagens
// do 2D (ui_image_*) nas texturas dos efeitos do renderizador.

#include <vector>

#include "driving/render/gs2d.h"
#include "driving_app/drv_renderer.h"

namespace drv {
namespace hud2d {

// O lugar das imagens do 2D entre as texturas dos efeitos.
constexpr int kImageSlotBase = 0x7f000000;

// Manda ao renderizador as imagens que ainda não foram (as novas do registro) e as trocadas (ui_image_update).
void upload_images(DrvRenderer& r);
// Os lotes na janela w x h: acrescentados a `out`.
void to_fx(const std::vector<Gs2dBatch>& in, int w, int h, std::vector<DrvFxBatch>& out);
// Um pacote inteiro (o estado do GS do port começa limpo).
void packet_to_fx(const std::vector<uint64_t>& words, int w, int h, std::vector<DrvFxBatch>& out);
// Esquece o que já subiu (outra janela, outro renderizador).
void reset();

}  // namespace hud2d
}  // namespace drv
