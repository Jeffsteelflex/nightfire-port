#pragma once
// O desenho do estilo de menu Modern (src/ui/modern_ui.h): a lista de desenho na resolução da janela, com
// Direct2D e DirectWrite sobre o dispositivo Direct3D 11 do renderizador (independente da camada 2D em alta
// definição: os menus Modern são sempre vetoriais).
//
// - Fontes: a Rajdhani de hd_ui/fonts_ofl/rajdhani (OFL), numa coleção privada do DirectWrite (nada é
//   instalado no sistema); sem ela, a Bahnschrift. A faixa de dados usa a Consolas.
// - Texto: um IDWriteTextLayout por texto, no tamanho em pixels da janela (tamanho x escala), com o
//   espaçamento entre letras (IDWriteTextLayout1::SetCharacterSpacing) e a cor do comando.
// - Formas: linhas finas presas ao pixel, retângulos (arredondados), elipses, chevrons, cadeado, losango, os
//   degradês (o brilho de brasa: degradê radial recortado acima do sublinhado; a vinheta; as faixas) e os
//   glifos dos botões do controle (os SVG de hd_ui/svg, ID2D1DeviceContext5).
// Sem Direct2D (ok() falso) o main fica com o estilo Original.

#include <d3d11.h>

#include <memory>
#include <string>

#include "ui/modern_ui.h"

namespace nf {

class UiModernDevice {
public:
    UiModernDevice(ID3D11Device* device, ID3D11DeviceContext* context);
    ~UiModernDevice();
    UiModernDevice(const UiModernDevice&) = delete;
    UiModernDevice& operator=(const UiModernDevice&) = delete;

    bool ok() const;
    // A pasta hd_ui (as fontes em fonts_ofl/rajdhani e os glifos em svg).
    void configure(const std::wstring& hd_ui_dir);
    // O alvo (o back buffer ou a textura da captura); nulo solta (antes de ResizeBuffers).
    void set_target(ID3D11Texture2D* target);
    // A largura de um texto em unidades da tela virtual (o espaço da última letra não conta).
    float measure(const std::wstring& text, const ui::TextStyle& style);
    void draw(const ui::DrawList& list, const ui::Canvas& canvas);
    std::string describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nf
