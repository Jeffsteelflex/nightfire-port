#pragma once
// O quadro dos filmes no Direct3D 11 (só do port; platform/psi_movie.h, docs/port.md "Filmes").
//
// No PS2 a imagem decodificada (512 x 448, RGBA32 do IPU) é copiada para o buffer de quadro no começo de cada quadro
// (PS2UploadBGMImage), antes de todo o resto: o filme é o fundo, e o que o jogo desenha depois (as legendas, os menus do
// menu principal) fica por cima. Aqui: uma textura dinâmica com o último quadro (copiada só quando ele muda) e um
// desenho que, no começo do quadro (Renderer::render, no alvo da cena), pinta o alvo de preto e põe a imagem no
// retângulo da tela do PS2 (a área 4:3 de psi_2d_viewport: barras pretas nas janelas mais largas), escalada com um
// filtro bicúbico de Catmull-Rom (9 leituras bilineares; nítido ao ampliar 512 x 448 para 1080p ou 4K, sem o borrão do
// bilinear). A textura também serve de imagem para os menus Modern (o fundo animado atrás do desfoque).

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace nf {

class MovieLayer {
public:
    bool init(ID3D11Device* device);
    // A imagem BGRA (alfa na escala do GS: 0x80 = opaco). Copiada para a textura quando serial muda.
    void upload(ID3D11DeviceContext* ctx, const uint8_t* bgra, int width, int height, uint64_t serial);
    // Pinta o alvo (rtv, target_w x target_h) de preto e desenha a imagem em rect (x0, y0, x1, y1, pixels do alvo).
    void draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, int target_w, int target_h, const float rect[4]);
    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    template <typename T>
    using Com = Microsoft::WRL::ComPtr<T>;
    Com<ID3D11Device> device_;
    Com<ID3D11Texture2D> tex_;
    Com<ID3D11ShaderResourceView> srv_;
    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11Buffer> cb_;
    Com<ID3D11SamplerState> sampler_;
    Com<ID3D11RasterizerState> raster_;
    Com<ID3D11DepthStencilState> no_depth_;
    int width_ = 0, height_ = 0;
    uint64_t serial_ = ~0ull;
};

}  // namespace nf
