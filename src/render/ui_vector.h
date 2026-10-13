#pragma once
// O desenho vetorial fora do Windows (etapa 3d do macOS; só do port): os menus Modern (ui::DrawList, src/ui) e a camada
// 2D em alta definição (os textos, as teclas e as substitutas SVG/PNG de render/ui_hd.h), rasterizados na CPU em imagens
// BGRA com o alfa pré-multiplicado que o renderizador (o Metal no macOS) envia como textura e compõe no quadro por cima
// do que já está no alvo (source-over), no mesmo ponto em que o Direct2D desenha no Windows. No Windows este cabeçalho
// não é usado: o Direct2D desenha direto no alvo do Direct3D 11 (render/ui_modern_draw_d2d.h, render/ui_hd_draw_d2d.h).
//
// A divisão da etapa 3d: o renderizador (render/renderer_metal.*) chama esta interface; o backend dela
// (render/ui_vector_cg.mm: CoreGraphics, CoreText e o leitor de SVG do port) é feito à parte. Os dois lados seguem
// este contrato:
//   - coordenadas em pixels da janela (o alvo do quadro: a janela ou a textura da captura), y para baixo;
//   - UiRaster::bgra: width x height x 4 bytes, linhas de cima para baixo, B G R A com a cor já multiplicada pelo alfa
//     (o renderizador mistura com ONE, ONE_MINUS_SRC_ALPHA); a imagem cobre só o retângulo com desenho (x, y), que pode
//     ser vazio (nada a compor);
//   - as substitutas da camada HD (asset) voltam como as do Direct2D: RGBA8 com a cor SEM o alfa pré-multiplicado e o
//     alfa na escala do GS (0x80 = opaco), para o renderizador desenhá-las com o mesmo shader e a mesma mistura dos
//     elementos originais;
//   - nada aqui toca na GPU nem guarda ponteiros do renderizador: tudo é chamado da thread do jogo.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/ui_hd.h"
#include "ui/modern_ui.h"

namespace nf {

// Uma imagem BGRA8 com o alfa pré-multiplicado e onde ela fica na janela.
struct UiRaster {
    int x = 0, y = 0;            // canto de cima à esquerda, em pixels da janela
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;   // width * height * 4
    // Muda a cada imagem nova (0: nenhuma ainda). O desenho guarda a última imagem de cada tipo e, com a mesma lista, não
    // rasteriza de novo: se `out` já tem essa versão, nada é copiado; o renderizador pode guardar a versão que enviou
    // e pular o envio da textura quando ela não mudar (os menus redesenham a cada quadro a mesma lista).
    uint64_t version = 0;
    bool empty() const { return width <= 0 || height <= 0; }
};

class UiVectorCanvas {
public:
    virtual ~UiVectorCanvas() = default;
    // O desenho funciona (sem ele o jogo fica com o estilo Original e sem a camada HD, como o Windows sem Direct2D).
    virtual bool ok() const = 0;

    // ---- os menus Modern (o que UiModernDevice faz no Windows) ----
    // A pasta hd_ui (as fontes em fonts_ofl/rajdhani e os glifos em svg).
    virtual void configure_modern(const std::wstring& hd_ui_dir) = 0;
    // A largura de um texto em unidades da tela virtual (como UiModernDevice::measure).
    virtual float measure(const std::wstring& text, const ui::TextStyle& style) = 0;
    // A lista inteira, numa janela de window_w x window_h pixels.
    virtual void draw_modern(const ui::DrawList& list, const ui::Canvas& canvas, int window_w, int window_h,
                             UiRaster& out) = 0;
    virtual std::string modern_describe() const = 0;

    // ---- a camada 2D em alta definição (o que UiHdDevice faz no Windows) ----
    // A configuração (map.json) e a pasta dela (as substitutas, hd_ui/fonts).
    virtual void configure_hd(const UiHdConfig* config, const std::wstring& dir) = 0;
    // A substituta "index" de UiHdConfig::images rasterizada em w x h (SVG) ou lida (PNG): RGBA8, cor sem o alfa
    // pré-multiplicado, alfa na escala do GS. false se falhar (o renderizador fica com a textura original).
    virtual bool asset(int index, int w, int h, std::vector<uint8_t>& rgba, int& out_w, int& out_h) = 0;
    // Uma sequência de textos e teclas seguidos de um lote (UiHdBatch::ops sem imagem entre eles), na ordem dada: texts e
    // keys são os do lote inteiro (UiHdBatch::texts e keys) e o UiHdOp::index de cada passo de order é a posição neles,
    // como em UiHdBatch::ops (o renderizador Metal passa assim).
    virtual void draw_hd(const std::vector<const UiHdText*>& texts, const std::vector<const UiHdKey*>& keys,
                         const std::vector<UiHdOp>& order, int window_w, int window_h, UiRaster& out) = 0;
    // A largura em pixels de um texto de tecla (fonte 2 com essa altura de maiúsculas); 0 sem o desenho.
    virtual float hd_measure(const std::wstring& label, float cap_px) = 0;
    // Fim do quadro: envelhece os caches.
    virtual void end_frame() = 0;
    virtual std::string hd_describe() const = 0;
};

// O backend do sistema (no macOS, render/ui_vector_cg.mm); nulo onde não houver.
std::unique_ptr<UiVectorCanvas> create_ui_vector_canvas();

}  // namespace nf
