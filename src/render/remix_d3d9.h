#pragma once
// Caminho de desenho opcional por Direct3D 9 de função fixa (só do port; --renderer d3d9ff; docs/remix.md). Só no
// Windows: este cabeçalho é neutro (a interface e a fábrica), a classe com o Direct3D 9 está em remix_d3d9.cpp.
//
// Protótipo para o RTX Remix, que só intercepta Direct3D 9 (o Direct3D 11 do port não é visto por ele). O
// renderizador Direct3D 11 continua o padrão e a referência; este caminho desenha a mesma lista de itens (a saída do
// RuntimeGfx::collect_frame, ou o quadro interpolado de render/frame_interp.h) do jeito que o Remix reconhece num jogo
// de DirectX 9 de função fixa:
//   - malhas em vertex e index buffers estáticos (D3DPOOL_MANAGED), criados uma vez na carga da fase e guardados pelo
//     id da malha do Renderer (GfxMirror); cada parte de malha é um DrawIndexedPrimitive com a faixa exata de vértices
//     que ela usa (o hash de geometria do Remix é das posições e dos índices dessa faixa: fica o mesmo entre quadros e
//     entre execuções);
//   - texturas criadas uma vez por slot de Tex, com a cadeia de mipmaps feita na CPU (o hash de textura do Remix é o
//     XXH3 do nível 0 enviado; o alfa do GS, 0x80 = opaco, vai dobrado para 0xff = opaco, como numa textura comum);
//   - SetTransform para mundo, visão e projeção (a câmera do Remix sai de VIEW e PROJECTION; projeção comum, z de 0
//     a 1, sem a profundidade invertida do Direct3D 11);
//   - formatos FVF; os personagens com skinning por vertex blending indexado da função fixa (D3DFVF_XYZB2 com o
//     último beta em UBYTE4: o peso do primeiro osso e os dois índices; D3DTS_WORLDMATRIX(n) = a paleta do jogo),
//     que o Remix lê como skinning na GPU, com o hash da pose de repouso. Ossos além do que o aparelho faz em
//     hardware (D3DCAPS9::MaxVertexBlendMatrixIndex; o dxvk-remix diz 8) vão por processamento de vértices em
//     software (D3DCREATE_MIXED_VERTEXPROCESSING + SetSoftwareVertexProcessing), que aceita 256 matrizes;
//   - o 2D (o HUD, os menus Original, as legendas) depois de todo o 3D, com projeção ortográfica (_44 = 1) e sem
//     escrita de Z: o que o Remix trata como UI (rtx.orthographicIsUI) e desenha por cima da imagem path traced;
//   - as luzes da VU1 do quadro (as duas mais perto de cada desenho, levadas ao mundo e sem repetição) como
//     D3DLIGHT9 pontuais com D3DRS_LIGHTING desligado: não mudam a imagem rasterizada, mas o Remix as converte em
//     luzes do path tracer.
// O dispositivo é criado com D3DCREATE_FPU_PRESERVE: sem ele o Direct3D 9 (e a ponte do Remix) poriam a FPU em
// precisão simples, e a lógica do jogo mudaria.
//
// O que difere do Direct3D 11 (aproximações da função fixa, documentadas em docs/remix.md):
//   - sem as rotinas da VU1: a cor é textura x cor do vértice x 2 (D3DTOP_MODULATE2X, a escala do GS) x o tweak
//     (D3DTA_TFACTOR num segundo estágio); as luzes dinâmicas da VU1 não iluminam a imagem rasterizada;
//   - a névoa vira névoa linear por vértice (D3DRS_FOGVERTEXMODE) entre as distâncias em que a curva do GS
//     (vu1::fog_keep de vu1::gs_z) começa e termina, nos desenhos antes da passada de névoa do jogo;
//   - o mapa de ambiente (rotina 0x2e0) usa as coordenadas guardadas na malha (sem gerar ST pela normal; ver
//     apply_state);
//   - alfa acima de 1.0 (o GS vai até 2.0) satura; as partículas aditivas não são pré-multiplicadas;
//   - a camada 2D HD (Direct2D) e os menus Modern não existem aqui (o main força o estilo Original e desliga a
//     camada HD); as substitutas HD do pacote do PCSX2 não são usadas (no Remix, as texturas se trocam pelo hash
//     dele);
//   - a janela não muda o tamanho do back buffer (a imagem estica).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/renderer.h"
#include "render/runtime_gfx.h"

namespace nf {

struct D3D9FrameParams {
    std::array<float, 16> view{};        // vetor-linha (XMMatrixLookToRH da câmera do jogo)
    std::array<float, 16> projection{};  // vetor-linha, z de 0 (perto) a 1 (longe)
    bool fog = true;                     // false: sem névoa (--no-fog)
    bool wireframe = false;
    bool show_volumes = false;
    bool game_lights = true;             // as luzes da VU1 do quadro como D3DLIGHT9 (para o Remix)
    // > 0: os desenhos presos à câmera (a lista 2: a arma em primeira pessoa e o clarão) usam o viewport com
    // profundidade de 0 a viewmodel_max_z, como os jogos de PC faziam com a arma (fica sempre na frente); o Remix
    // os separa como view model com rtx.viewModel.enable e rtx.viewModel.maxZThreshold >= esse valor. 0: como o PS2
    // (só a limpeza do Z que o Game_Draw pede antes da lista 2, o item clear_depth, que vale nos dois casos).
    float viewmodel_max_z = 0.0f;
};

// O que o último quadro desenhou (a linha d3d9ff do relatório das capturas).
struct D3D9FrameStats {
    uint32_t rigid = 0;       // partes de malha rígidas (DrawIndexedPrimitive)
    uint32_t skinned = 0;     // partes de malha com skinning
    uint32_t swvp = 0;        // desenhos com skinning em software
    uint32_t env_parts = 0;   // partes da rotina 0x2e0 (mapa de ambiente), desenhadas com as coordenadas da malha
    uint32_t sprites = 0;     // listas de partículas (DrawPrimitiveUP)
    uint32_t triangles = 0;   // listas de triângulos soltos (estilhaços)
    uint32_t quads2d = 0;     // quadrados 2D
    uint32_t skipped = 0;     // itens sem malha, malha e desenho que não combinam, paleta curta
    uint32_t hd_items = 0;    // itens da camada HD (não desenhados aqui)
    uint32_t missing_textures = 0;  // desenhos com um slot de textura que não chegou a este caminho
    uint32_t lights = 0;      // luzes passadas por SetLight
    uint32_t viewmodel = 0;   // itens da lista 2 desenhados com o viewport de view model
    uint32_t depth_clears = 0;  // limpezas do Z no meio do quadro (psiClearZ antes da lista 2)
    bool fog = false;
    float fog_start = 0.0f, fog_end = 0.0f;
};

// O caminho (no Windows; remix_d3d9.cpp tem a classe com o Direct3D 9). Os recursos da fase chegam como GfxMirror.
class RemixD3D9Renderer : public GfxMirror {
public:
    ~RemixD3D9Renderer() override = default;
    // O quadro: o 3D na ordem dos itens e depois o 2D, no back buffer.
    virtual void render(const std::vector<DrawItem>& items, const D3D9FrameParams& frame) = 0;
    virtual void present() = 0;
    // O back buffer num PNG (antes do present).
    virtual bool save_png(const std::wstring& path) = 0;
    virtual std::string describe() const = 0;
    virtual const D3D9FrameStats& stats() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    // O hash que o Remix daria à textura do slot: o XXH3 de 64 bits do nível 0 como foi enviado (A8R8G8B8, linhas
    // sem folga), a conta do D3D9CommonTexture do dxvk-remix (0 sem textura).
    virtual uint64_t texture_hash(int slot) const = 0;
    // Guarda, a cada quadro, os hashes de textura por categoria (2D, céu, marcas, partículas, mundo) para o rtx.conf
    // sugerido (--d3d9-remix-conf).
    virtual void set_record_hashes(bool on) = 0;
    // Um rtx.conf sugerido: a escala, os eixos e as texturas de UI, céu, marcas e partículas vistas até aqui. Os hashes
    // são candidatos: só o Remix confirma (o menu Game Setup mostra as texturas marcadas).
    virtual bool write_remix_conf(const std::wstring& path) const = 0;
};

// window: a janela do jogo (Window::native_handle; o back buffer tem width x height, qualquer que seja o tamanho dela).
// adapter: o adaptador do Direct3D 11 escolhido por [video] adapter (nulo: auto). O Direct3D 9 só lista os
// adaptadores com monitor; o do mesmo fabricante e modelo (GetAdapterIdentifier: VendorId e DeviceId) é usado, e
// sem ele (o integrado sem monitor, por exemplo) o padrão (D3DADAPTER_DEFAULT), como antes; describe() diz qual.
// Lança std::runtime_error se o Direct3D 9 não criar o dispositivo. Só existe no Windows.
std::unique_ptr<RemixD3D9Renderer> create_remix_d3d9(void* window, int width, int height, bool vsync,
                                                     const AdapterInfo* adapter = nullptr);
// Uma janela invisível para o dispositivo Direct3D 11 do port (as camadas 2D, o Direct2D), que neste modo não
// apresenta nada: a janela do jogo fica só com o Direct3D 9. O identificador do sistema (o HWND).
void* create_remix_hidden_window(int width, int height);

}  // namespace nf
