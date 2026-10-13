#pragma once
// Camada 2D em alta definição: o desenho (render/ui_hd.h) com Direct2D 1.1, DirectWrite e WIC (platform/image_io.h) sobre o
// dispositivo Direct3D 11 do renderizador.
//
// - Texto: um IDWriteTextLayout por linha, com a cor de cada trecho (SetDrawingEffect) e um renderizador
//   de glifos próprio que também desenha o traço do contorno (GetGlyphRunOutline). A fonte de cada fonte
//   do jogo vem do map.json; as da pasta hd_ui/fonts (do usuário, fora do git) entram antes das do
//   sistema (IDWriteFontSetBuilder1, Windows 10 1703+). O tamanho em pixels sai da altura das maiúsculas
//   da fonte do jogo e da razão capHeight / designUnitsPerEm da fonte vetorial.
// - Substitutas: SVG (ID2D1SvgDocument, ID2D1DeviceContext5, Windows 10 1703+) rasterizado no tamanho em
//   que aparece, ou PNG (WIC) com mipmaps; os pixels vão para uma textura D3D11 R8G8B8A8 com a cor sem o
//   alfa pré-multiplicado e o alfa na escala do GS (0x80 = opaco), como as texturas do jogo, para o
//   renderizador desenhá-las com o mesmo shader e a mesma mistura do elemento original. Cache por (arquivo,
//   largura, altura), esquecido depois de alguns segundos sem uso.
// Sem Direct2D ou sem SVG (Windows antigo), ok() ou asset() falham e o renderizador fica com o original.

#include <d3d11.h>

#include <memory>
#include <string>

#include "render/ui_hd.h"

namespace nf {

class UiHdDevice {
public:
    UiHdDevice(ID3D11Device* device, ID3D11DeviceContext* context);
    ~UiHdDevice();
    UiHdDevice(const UiHdDevice&) = delete;
    UiHdDevice& operator=(const UiHdDevice&) = delete;

    bool ok() const;
    // Configuração e a pasta dela (as substitutas, hd_ui/fonts). Recarrega fontes e substitutas.
    void configure(const UiHdConfig* config, const std::wstring& dir);
    // O alvo dos textos (o back buffer ou a textura da captura); nulo solta (antes de ResizeBuffers).
    void set_target(ID3D11Texture2D* target);
    // A substituta "index" de UiHdConfig::images: um SVG rasterizado em w x h, ou o PNG; nulo se falhar.
    ID3D11ShaderResourceView* asset(int index, int w, int h);
    // Textos: begin_text e end_text envolvem um BeginDraw/EndDraw do Direct2D no alvo.
    void begin_text();
    void draw_text(const UiHdText& text);
    void draw_key(const UiHdKey& key);  // entre begin_text e end_text
    // A largura em pixels de um texto de tecla (fonte 2 com essa altura de maiúsculas); 0 sem Direct2D.
    float measure(const std::wstring& label, float cap_px);
    void end_text();
    // Fim do quadro: envelhece o cache das substitutas.
    void end_frame();
    // Uma linha de diagnóstico (fontes escolhidas, erros).
    std::string describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nf
