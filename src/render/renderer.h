#pragma once
// Renderizador Direct3D 11. Substitui o caminho GS/VU1 do PS2 para a geometria das fases. Os
// recursos são criados durante a carga (texturas por índice da tabela Tex do jogo, malhas por id) e
// cada quadro desenha uma lista de itens (malha + matriz).
//
// Malhas com skinning (personagens) usam um vertex shader próprio, que faz na GPU o que o SkinIt
// faz na VU0: cada vértice na pose de repouso mistura dois ossos da paleta do item (constant buffer
// de kMaxSkinBones matrizes 4x4) com pesos w e 1 - w. Os rostos (malhas com alvos de morph, DrawItem::morph
// com algum peso) usam uma variante que antes soma à pose de repouso os deltas dos alvos vezes os pesos (um
// StructuredBuffer por malha, criado na carga, e um constant buffer de 22 pesos por desenho), como o SkinIt faz
// no EE do PS2 antes da VU0; só ~350 dos ~2000 vértices de uma cabeça têm morph.
//
// Convenção: matrizes em vetor-linha (v' = v * M), igual ao motor do jogo, com row_major no HLSL.
// Profundidade invertida (perto = 1, longe = 0) para precisão com céus a quilômetros de distância.
//
// Estado do GS: cada parte de malha traz o ALPHA_1/TEST_1/ZBUF_1 que a própria cadeia escreve
// (SubMesh::gs, pares A+D) e o PRIM.ABE do lote; o que a malha não escreve vem do desenho
// (DrawItem::gs: psiSetUpColourBlend, psiSwitchZBuffer*), e o que ninguém escreveu segue a regra
// antiga (opaco: teste de alfa em 50% e escreve Z; misturado: sem escrever Z). A equação de mistura
// do GS, (A - B) * C + D com A, B, D em {Cs, Cd, 0} e C em {As, Ad, FIX}, vira um blend state
// (nf::gs_blend_equation em data/map_item.h). O alfa gravado no alvo é o do pixel, como no GS (as
// misturas com C = Ad o leem: máscaras ALPHA 0x64 seguidas de 0x58). O teste de alfa (ATST, AREF) é feito no pixel shader, com o alfa na escala do GS
// (0x80 = 1.0), antes da mistura; o teste e a escrita de Z vêm de TEST_1 (ZTE, ZTST) e ZBUF_1 (ZMSK).
//
// Sprites (psiDrawParticleList): o item traz quatro vértices por partícula, já no mundo e virados
// para a câmera, com a cor na escala do GS; o shader modula a textura como o GS (Ct * Cv / 128,
// At * Av / 128) e, nas misturas com C = As e A = Cs ou B = Cs, multiplica a cor pelo alfa no shader
// (alfa até 2.0, como no GS) e usa ONE no lugar de SRC_ALPHA.
//
// Triângulos soltos (psiDrawShard, os estilhaços de vidro): o item traz três vértices por triângulo,
// já no mundo, e é desenhado como os sprites (cor na escala do GS, sem a rotina da VU1), com um vertex
// buffer dinâmico e o estado do GS do desenho (psiSetShardRenderStates).
//
// Iluminação (a VU1, docs/formats.md, "Microprograma da VU1"): o vertex shader faz por vértice o que a
// rotina escolhida faria com a cor do disco (escala do GS): vezes o tweak (TAmbient), mais as duas luzes
// de psiLight_SetLights com a rotina 0x340, ou o ST do mapa de ambiente da rotina 0x2e0 nas partes com
// SubMesh::env; o pixel shader modula a textura como o GS. As mesmas contas na CPU: platform/vu1.h.
//
// Névoa: um item com fog_pass (onde o jogo chamaria psiDrawFogPass) mistura a cor da névoa no quadro
// pelo Z do GS de cada pixel, calculado da profundidade (vu1::fog_keep).
//
// Limpeza do Z: um item com clear_depth (onde o Game_Draw chama psiClearZ, antes da lista 2) limpa a
// profundidade da cena (no alvo dela, com MSAA ou escala), como o ZClearPacket do PS2; a arma em primeira
// pessoa e o clarão vêm depois e ficam por cima do cenário.
//
// 2D (psiDrawSprites: sprites, HUD, texto; platform/psi_2d.h): o item traz quadrados já em pixels da
// janela (RuntimeGfx::collect_frame aplica psi_2d_viewport), cada um com a textura, o ALPHA_1 do GS e o
// filtro (de ponto nas fontes, bilinear no resto; sempre o nível 0 da textura); o vertex shader os passa
// direto para a tela, o pixel shader modula como o GS, sem Z, com teste de alfa > 0 (o TEST_1 de
// psiStartSprites) e recorte (scissor) na área da tela do PS2.
//
// Camada 2D em alta definição (opcional; render/ui_hd.h): um item com "ui" traz o 2D refeito na
// resolução da janela. As imagens são desenhadas como o 2D acima (a substituta, quando há, no lugar da
// textura, com filtro bilinear e mipmaps); os textos, com Direct2D/DirectWrite no mesmo alvo
// (render/ui_hd_draw.h), na ordem dos itens. O dispositivo é criado com D3D11_CREATE_DEVICE_BGRA_SUPPORT
// para o Direct2D; depois de cada trecho de Direct2D o estado do Direct3D do quadro é refeito.
//
// Captura fora da tela (--screenshot): com "offscreen", o alvo é uma textura do tamanho pedido em vez do
// back buffer da janela (que no Windows não passa do tamanho da área de trabalho).
//
// Adaptador (só do port; platform/gpu_adapter.h, render/gpu_adapters.h): sem adaptador, o dispositivo nasce no
// padrão do sistema (D3D11CreateDevice com D3D_DRIVER_TYPE_HARDWARE, como sempre); com um, nele
// (D3D_DRIVER_TYPE_UNKNOWN). A cadeia de troca vem da fábrica do adaptador. Com o monitor ligado a outra placa (o
// integrado desenhando, o monitor na dedicada), o DXGI copia cada quadro de uma placa para a outra no Present (o
// custo aparece no tempo de quadro da janela, não no da GPU); os modos do monitor saem da saída da outra placa
// (output_of procura o monitor da janela em todos os adaptadores) e a tela cheia exclusiva não é possível.
//
// Medidas (--bench, só do port): com set_profiling, consultas de carimbo de tempo do Direct3D 11
// (D3D11_QUERY_TIMESTAMP dentro de um TIMESTAMP_DISJOINT por quadro) marcam o fim de cada trecho do quadro na GPU
// (GpuPhase: a cena 3D, a névoa, a resolução/redução, o 2D, o desfoque e o Direct2D dos menus Modern); os
// resultados são lidos alguns quadros depois, sem esperar. counters() conta os desenhos (Draw/DrawIndexed) e os
// itens. Fora da tela não há Present para segurar a CPU: com set_offscreen_throttle, present() espera a GPU acabar o
// quadro de dois quadros atrás (uma consulta EVENT), como a fila de quadros de uma cadeia de troca.
//
// Opções do PC (só do port; platform/video_settings.h, a página de vídeo do menu): a cena 3D pode ser
// desenhada num alvo próprio, em escala do tamanho da janela (supersampling acima de 100%) e/ou com MSAA
// de 2, 4 ou 8 amostras. Então o quadro tem duas passadas: os itens 3D (na ordem) no alvo da cena, a
// resolução do MSAA (ResolveSubresource) e a redução para a janela (ps_down: média de até 4 x 4 amostras
// bilineares sobre a área do pixel), e depois os itens 2D (na ordem) no tamanho da janela. A névoa lê a
// profundidade da cena; com MSAA, ps_fog_ms roda por amostra (SV_SampleIndex) e lê a profundidade de cada
// amostra (Texture2DMS), então o MSAA e a névoa convivem. Em 100% sem MSAA o caminho é o de antes (uma
// passada no back buffer, imagens idênticas). Também: o filtro das texturas (bilinear, trilinear,
// anisotrópico 2x-16x), o VSync e a tela cheia exclusiva do DXGI (troca o modo do monitor; a cadeia de
// troca é criada com DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH). Sem VSync, o Present usa DXGI_PRESENT_ALLOW_TEARING quando
// o sistema tem suporte (a cadeia nasce com DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING): sem ele, numa janela composta pelo DWM o
// modelo flip ainda esperaria a atualização do monitor e o "VSync desligado" ficaria preso a 60/144/240 Hz.

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "game/level_scene.h"
#include "platform/gpu_adapter.h"
#include "platform/video_settings.h"

namespace nf {

struct UiHdBatch;
struct UiHdConfig;
class MovieLayer;
class UiHdDevice;
class UiModernDevice;
namespace ui {
class DrawList;
struct Canvas;
struct TextStyle;
}  // namespace ui

// Mistura de cores de um item, com os números de psiSetUpColourBlend (o registrador ALPHA do GS).
enum class Blend : uint8_t {
    Additive = 0,     // Cs * As + Cd
    Alpha = 1,        // (Cs - Cd) * As + Cd
    Opaque = 2,       // Cs (com alpha test em 50%)
    Subtractive = 3,  // Cd - Cs * As
};

// Quad2D::texture sem textura do GS (TEX0 sem TME, o sprite de psiFadeView): a cor é a do vértice como está (0xff =
// 1.0) e o alfa também (0x80 = 1.0). O renderizador usa uma textura 0x80 em tudo, que a modulação do GS (Ct * Cv /
// 128, At * Av / 128) deixa igual à cor e ao alfa do vértice.
constexpr int kQuadNoTexture = -2;

// Um quadrado 2D na janela (DrawItem::quads2d).
struct Quad2D {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixels da janela
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // ST
    uint8_t rgba[4]{};                     // escala do GS (0x80 = 1.0)
    int texture = -1;                      // slot de Tex (-1: branca; kQuadNoTexture: sem textura)
    uint64_t alpha = 0x44;                 // ALPHA_1
    bool linear = true;                    // false: filtro de ponto (fontes)
    // Só na camada HD: a textura no lugar do slot (uma substituta) e o filtro com mipmaps.
    ID3D11ShaderResourceView* srv = nullptr;
    bool smooth = false;
};

struct DrawItem {
    int mesh = -1;
    std::array<float, 16> world{};  // vetor-linha
    ObjectKind kind = ObjectKind::Visible;
    Blend blend = Blend::Opaque;
    // Só nos desenhos com skinning (psiDrawSkinObjectMatrix): uma matriz por osso do esqueleto, já no
    // mundo e com a pose inversa embutida (vetor-linha), copiada do quadro do jogo; world é aplicada
    // depois do skinning (identidade, como no PS2). Vazia = malha rígida. Malha e tipo de desenho têm
    // de combinar: o renderizador pula uma malha com skinning pedida como rígida e vice-versa.
    std::vector<std::array<float, 16>> palette;
    // Só nos rostos (com skinning e a malha com morphs, EntityMesh::morph): o peso de cada alvo (PsiDrawCall::morph;
    // 0 nos que o jogo não escolheu). O vertex shader soma peso x delta à pose de repouso antes do skinning, como o
    // SkinIt. Vazio, ou só zeros: a pose de repouso (o shader de skinning comum).
    std::vector<float> morph;
    // Tweak do desenho (psiSetTweakARGB / 255: r, g, b, a): multiplica a cor e o alfa de cada pixel,
    // antes do teste de alfa. {1, 1, 1, 1} não muda nada.
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
    // Registradores do GS em vigor no desenho (gs.has diz quais foram escritos); os da malha valem
    // por cima. Sem nada escrito, vale blend.
    GsState gs;
    // Só nos sprites (mesh = -1): quatro vértices por partícula (cantos de cima à esquerda, de cima à
    // direita, de baixo à direita e de baixo à esquerda), cor na escala do GS; textura = slot de Tex
    // (-1: nenhuma).
    std::vector<Vertex> sprites;
    int sprite_texture = -1;
    // Só nos triângulos soltos (mesh = -1; os estilhaços de psiDrawShard): três vértices por triângulo,
    // já no mundo, cor na escala do GS, desenhados como os sprites (sem rotina da VU1 nem tweak);
    // textura = slot de Tex (-1: nenhuma).
    std::vector<Vertex> triangles;
    int triangle_texture = -1;
    // Luzes da VU1 em vigor (psiLight_SetLights; 0 = rotinas sem luz): posição no espaço dos vértices
    // (malha rígida: a do mundo pela inversa da matriz, como FillMatrixChainRot; com skinning: no
    // mundo), w = 1; cor TColor0/1 (r, g, b = 0..255 x intensidade x multiplicador; w = 1 / raio²).
    uint8_t num_lights = 0;
    float light_pos[2][4]{};
    float light_color[2][4]{};
    // Linhas 0 e 1 da matriz da câmera (no mundo), que a rotina 0x2e0 usa para o ST do mapa de ambiente.
    float env_s[3]{}, env_t[3]{};
    // Item sem modelo que faz a passada de névoa (psiDrawFogPass) neste ponto da lista: cor da névoa
    // (PS2FogR/G/B) e Ps2Far1 da fase.
    bool fog_pass = false;
    uint8_t fog_rgb[3]{};
    float fog_far1 = 0.0f;
    // Item sem modelo que limpa a profundidade neste ponto da lista (psiClearZ do Game_Draw, antes da lista 2: a
    // arma e os braços em primeira pessoa e o clarão ficam por cima do cenário e só se testam entre si).
    bool clear_depth = false;
    // Só nos itens 2D (mesh = -1; psiDrawSprites): os quadrados, na ordem do jogo, e o recorte (x0, y0,
    // x1, y1 em pixels da janela: a área da tela do PS2).
    std::vector<Quad2D> quads2d;
    float clip2d[4]{};
    // Só no esmaecimento (psiFadeView; um item 2D com um quadrado sem textura na janela inteira): a câmera do jogo
    // (0-10) que o pediu, a identidade com que a interpolação entre ticks acha o do tick anterior. -1: não é.
    int8_t fade = -1;
    // Só nos itens da camada 2D em alta definição (mesh = -1): o lote de uma chamada de psiDrawSprites.
    std::shared_ptr<const UiHdBatch> ui;
    // Só do port (render/frame_interp.h): a identidade do desenho entre ticks, para interpolar a matriz e a
    // paleta entre o tick anterior e o atual. O renderizador não lê nada disto.
    struct Interp {
        uint64_t id = 0;               // 0: sem identidade (não interpola)
        const void* owner = nullptr;   // PsiDrawCall::owner (o cel_tag, obj_tag, SKYOBJ_tag ou ATTACH_tag)
        uint8_t owner_kind = 0;        // PsiOwnerKind
        int8_t list = -1;              // a lista do View_DrawObjList (0 opaca, 1 transparente, 2 sobreposição)
        uint32_t created = 0;          // obj_tag::created
    } interp;
};

// Valor do ALPHA_1 que psiSetUpColourBlend(mode) escreve (0x48, 0x44, 0x2a, 0x42).
uint64_t gs_alpha_of_blend(Blend mode);



struct FrameParams {
    std::array<float, 16> view_proj{};  // vetor-linha
    // A cor com que render() limpa o alvo antes do quadro (só do PC: o fundo que nada cobriu). A tela de carga usa
    // preto, a VRAM zerada do PS2 (PS2WipeVram) por baixo da imagem da fase.
    std::array<float, 4> clear_color{0.125f, 0.145f, 0.19f, 1.0f};
    // Planos da projeção (profundidade invertida, XMMatrixPerspectiveFovRH(fov, aspecto, z_far, z_near)):
    // a passada de névoa recupera a distância de cada pixel com eles.
    float z_near = 0.05f;
    float z_far = 20000.0f;
    // Sombreamento de depuração (tecla V, só do PC): luz direcional fixa no lugar da cor dos vértices.
    bool debug_shading = false;
    float light_dir[3] = {-0.4f, -0.8f, -0.45f};
    float ambient = 0.55f;
    bool lights = true;  // false: sem as luzes da VU1 (rotinas sem luz; --no-lights)
    bool fog = true;     // false: sem a passada de névoa (--no-fog)
    bool morphs = true;  // false: rostos sem os morphs (--no-morphs; a pose de repouso)
    bool env_map = true; // false: as partes da rotina 0x2e0 com as UVs da malha (--no-env-map; depuração)
    bool wireframe = false;
    bool show_volumes = false;  // gatilhos e marcadores de edição
};

// Os trechos do quadro na GPU (as medidas do --bench).
enum class GpuPhase : uint8_t { Scene, Fog, Resolve, TwoD, Blur, Modern, Count };
constexpr int kGpuPhaseCount = int(GpuPhase::Count);
const char* gpu_phase_name(GpuPhase p);  // cena, nevoa, resolucao, 2d, desfoque, direct2d

struct GpuTimes {
    uint64_t frames = 0;              // quadros medidos (válidos)
    double total_ms = 0.0;            // soma do tempo de GPU por quadro (do primeiro ao último carimbo)
    double max_ms = 0.0;
    double phase_ms[kGpuPhaseCount]{};  // soma por trecho
    uint64_t disjoint = 0;            // quadros descartados (o relógio da GPU mudou)
};

struct RenderCounters {
    uint64_t draws = 0;     // Draw/DrawIndexed
    uint64_t items = 0;     // itens da lista desenhados (DrawItem)
    uint64_t occluded = 0;  // Present com a janela coberta (DXGI_STATUS_OCCLUDED: o quadro não foi mostrado)
};

class Renderer {
public:
    // offscreen: desenha numa textura width x height em vez do back buffer da janela (capturas).
    // adapter: o adaptador de vídeo (nulo: o padrão do sistema, como antes).
    Renderer(HWND hwnd, int width, int height, bool vsync, bool offscreen = false, IDXGIAdapter1* adapter = nullptr);
    ~Renderer();

    void resize(int width, int height);
    // Descarta texturas e malhas (troca de fase).
    void reset_resources();
    // Textura no índice "slot" (o índice da tabela Tex do jogo); só o primeiro quadro.
    void set_texture(int slot, const Texture& texture);
    // Substituta em alta definição para o slot (arquivo DDS: BC1, BC2, BC3, BC7 ou RGBA8). Sem
    // mipmaps no arquivo, a textura é copiada para RGBA8 e ganha mipmaps gerados na GPU.
    bool set_texture_dds(int slot, const std::wstring& path);
    // Uma imagem do port (RGBA, alfa do GS como as texturas do jogo), guardada com uma chave e mantida entre as fases
    // (reset_resources não a solta): os ícones da roda de armas (ui::ImageCmd::port). Com mipmaps.
    void set_ui_image(uint32_t key, const uint8_t* rgba, int width, int height);
    ID3D11ShaderResourceView* ui_image(uint32_t key) const;
    size_t ui_images() const { return ui_images_.size(); }
    // Liga ou desliga o uso das substitutas (as originais continuam carregadas).
    void use_hd(bool on) { use_hd_ = on; }
    bool using_hd() const { return use_hd_; }
    // Malha nova (rígida ou com skinning); submesh.texture já é um slot de textura (ou -1). Devolve
    // o id.
    int add_mesh(const EntityMesh& mesh);
    void render(const std::vector<DrawItem>& items, const FrameParams& frame);
    // Só os itens 2D da lista, por cima do que já está no alvo (sem limpar): o 2D do jogo que fica sobre a cena
    // desfocada dos menus Modern (a cortina de sangue do fim de missão). Depois de render() no mesmo quadro.
    void render_2d(const std::vector<DrawItem>& items, const FrameParams& frame);
    void present();
    // Grava o último quadro renderizado num PNG (antes do present).
    void save_png(const std::wstring& path);

    // ---- o rastro da tela (ScreenBlur do PS2; só do port a forma) ----
    // O quadro que vai para a tela mistura o anterior: alvo = anterior x keep + atual x (1 - keep) (keep 0: nada, e o
    // anterior é esquecido). No PS2 é o CreateSqueezeFrame de cada psiPreDraw: a cópia do buffer de desenho para o de
    // exibição com ALPHA FIX = ScreenBlur (o clarão da granada de luz e a troca de zoom). Depois de render(), antes dos
    // menus Modern.
    void screen_blur(float keep);
    bool screen_blur_active() const { return blur_history_valid_; }

    // ---- a tela de carga (só do port; platform/psi_loading.h) ----
    // Limpa o alvo com a cor dada (a tela preta do PS2WipeVram).
    void clear_target(const float rgba[4]);
    // Guarda uma cópia do alvo (a tela de carga desenhada; sobrevive a reset_resources) e a põe de volta no alvo antes
    // de cada passo das bolinhas (o back buffer não guarda nada entre um Present e o outro). release solta a cópia.
    void save_loading_snapshot();
    bool restore_loading_snapshot();
    void release_loading_snapshot();
    // A textura do slot (a original, ou a substituta HD em uso), com uma referência própria: continua valendo depois de
    // reset_resources (a das bolinhas, 0x3000175, que a carga da fase descarta). Nula sem textura no slot.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> keep_texture(int slot) const;
    bool offscreen() const { return offscreen_; }

    // Camada 2D em alta definição: a configuração (nula desliga o dispositivo) e a pasta das substitutas e
    // das fontes. Os itens com "ui" só são desenhados com ela.
    void set_ui_hd(const UiHdConfig* config, const std::wstring& dir);
    std::string ui_hd_describe() const;
    // A largura de um texto de tecla da camada HD (UiHdEnv::measure); 0 sem a camada.
    float ui_hd_measure(const std::wstring& label, float cap_px) const;
    bool ui_hd_ok() const;  // o Direct2D funciona (sem ele a camada HD não liga)

    // ---- o estilo de menu Modern (src/ui, render/ui_modern_draw.h; só do port) ----
    // Cria o desenho dos menus Modern (Direct2D) com a pasta hd_ui (fontes e glifos); false sem Direct2D.
    bool set_modern_ui(const std::wstring& hd_ui_dir);
    bool modern_ok() const;
    float modern_measure(const std::wstring& text, const ui::TextStyle& style) const;
    // Desfoca o que já está no alvo (a cena) para o fundo de um menu Modern: uma gaussiana separável em 1/4
    // da resolução, misturada por `amount` (0 a 1). Depois de render() e antes de draw_modern().
    void blur_background(float amount);
    void draw_modern(const ui::DrawList& list, const ui::Canvas& canvas);
    std::string modern_describe() const;
    // O nível 0 da textura original do slot, em RGBA (inventário do 2D); false se não houver.
    bool read_texture(int slot, int& width, int& height, std::vector<uint8_t>& rgba);
    // Um PNG RGBA (WIC).
    static bool write_png(const std::wstring& path, int width, int height, const uint8_t* rgba);

    int width() const { return width_; }
    int height() const { return height_; }

    // ---- os filmes (só do port; platform/psi_movie.h, render/movie_layer.h) ----
    // O quadro do filme (BGRA, alfa na escala do GS), copiado para a textura dele quando serial muda.
    void set_movie_frame(const uint8_t* bgra, int width, int height, uint64_t serial);
    // Com on, o próximo render() começa pelo filme: o alvo da cena preto e a imagem em rect (x0, y0, x1, y1 em pixels
    // da janela: a tela do PS2), como a cópia da imagem para o buffer de quadro do PS2 antes do resto do desenho.
    void set_movie_background(bool on, const float rect[4]);
    ID3D11ShaderResourceView* movie_srv() const;  // a textura do quadro (os menus Modern a desenham como fundo)

    // ---- o adaptador e as medidas (só do port) ----
    // O adaptador do dispositivo (nome, fabricante, memória, integrado pela memória unificada do Direct3D).
    const AdapterInfo& adapter() const { return adapter_; }
    void set_profiling(bool on);
    bool profiling() const { return profiling_; }
    // Começo e fim de um quadro medido (entre eles, os trechos marcados pelos desenhos).
    void gpu_frame_begin();
    void gpu_frame_end();
    // Lê os quadros já prontos (wait: espera todos, no fim da medida).
    void gpu_collect(bool wait);
    const GpuTimes& gpu_times() const { return gpu_times_; }
    // Zera as somas e a numeração dos quadros (o próximo gpu_frame_begin é o quadro 0 de gpu_per_frame).
    void reset_gpu_times() {
        gpu_times_ = GpuTimes{};
        gpu_seq_ = 0;
        gpu_per_frame_.clear();
    }
    // Por quadro (desde reset_gpu_times, na ordem dos gpu_frame_begin): o total e os trechos, em ms (-1: não lido ainda
    // ou descartado). Para o --bench-frames.
    using GpuFrameTimes = std::array<float, kGpuPhaseCount + 1>;
    const std::vector<GpuFrameTimes>& gpu_per_frame() const { return gpu_per_frame_; }
    const RenderCounters& counters() const { return counters_; }
    // Só do PC: com MSAA, os desenhos opacos recortados pelo teste de alfa do GS usam o alfa para a cobertura
    // (alpha-to-coverage, gs_alpha_coverage_candidate em data/map_item.h): as bordas de folhas e grades ficam suaves
    // como as dos triângulos. Sem MSAA não muda nada. Ligado; --no-alpha-coverage (depuração) desliga, para comparar.
    void set_alpha_to_coverage(bool on) { alpha_coverage_ = on; }
    bool alpha_to_coverage() const { return alpha_coverage_; }
    // As partes desenhadas com a cobertura no último render() (o relatório das capturas).
    uint32_t alpha_coverage_parts() const { return a2c_parts_; }
    // Partes de malha da rotina do mapa de ambiente (0x2e0) desenhadas no último render() (o relatório das capturas).
    uint32_t env_parts() const { return env_parts_; }
    void reset_counters() { counters_ = RenderCounters{}; }
    void set_offscreen_throttle(bool on) { throttle_ = on; }

    // ---- opções do PC (só do port) ----
    void set_vsync(bool on) { vsync_ = on; }
    bool vsync() const { return vsync_; }
    // A cena 3D em scale_percent % do tamanho da janela (no máximo 16384 de lado) e com msaa amostras
    // (1, 2, 4 ou 8). Sem suporte ou sem memória, cai para menos MSAA e depois para 100%: devolve falso.
    bool set_scene_quality(int scale_percent, int msaa);
    int scene_scale() const { return scene_scale_; }
    int scene_msaa() const { return scene_msaa_; }
    int scene_width() const { return scene_w_; }
    int scene_height() const { return scene_h_; }
    // As amostras de MSAA aceitas para a cena (cor B8G8R8A8 e profundidade D32): entre 2, 4 e 8.
    std::vector<int> supported_msaa() const;
    void set_texture_filter(TextureFilter f);
    TextureFilter texture_filter() const { return filter_; }
    bool tearing() const { return tearing_; }
    // Tela cheia exclusiva (DXGI) no modo do monitor mais próximo de width x height; off volta à janela.
    bool set_exclusive_fullscreen(bool on, int width, int height);
    bool exclusive_fullscreen() const;
    // Os modos do monitor da janela (DXGI, B8G8R8A8) e o tamanho dele.
    std::vector<DisplayMode> display_modes() const;
    DisplayMode desktop_mode() const;

private:
    template <typename T>
    using Com = Microsoft::WRL::ComPtr<T>;

    struct GpuMesh {
        Com<ID3D11Buffer> vb, ib;
        std::vector<SubMesh> submeshes;
        bool skinned = false;  // vértices SkinnedVertex
        int bones = 0;         // com skinning: matrizes que a paleta precisa ter
        // Rosto: os deltas dos alvos de morph (StructuredBuffer<float3>, EntityMesh::morph) e quantos alvos.
        Com<ID3D11ShaderResourceView> morph_srv;
        int morph_targets = 0;
    };

    void create_targets();
    void create_scene_targets();
    bool try_scene_targets(int scale_percent, int msaa);
    bool separate_scene() const { return scene_rtv_ != nullptr; }
    ID3D11RenderTargetView* scene_rtv() const { return scene_rtv_ != nullptr ? scene_rtv_.Get() : rtv_.Get(); }
    void resolve_scene();
    void draw_item(const DrawItem& inst, const FrameParams& frame);
    void create_pipeline();
    ID3D11BlendState* blend_state(const GsBlendEq& b, bool coverage = false);
    void use_shader(int skinned);  // 0: rígido (e sprites), 1: com skinning, 2: com skinning e morphs (rostos)
    ID3D11ShaderResourceView* texture_srv(int slot) const;
    void apply_state(const DrawItem& inst, const GsState& own, bool sprite, bool env = false, bool screen = false);
    void draw_sprites(const DrawItem& inst);
    void draw_triangles(const DrawItem& inst);
    void draw_fog(const DrawItem& inst, const FrameParams& frame);
    void draw_2d(const DrawItem& inst);
    void draw_ui(const UiHdBatch& batch, const FrameParams& frame);
    void bind_frame_state(const FrameParams& frame);
    ID3D11DepthStencilState* depth_state(D3D11_COMPARISON_FUNC func, bool write);
    // O trecho atual da GPU: muda (marca o fim do anterior) quando um desenho de outro trecho começa.
    void gpu_phase(GpuPhase p);
    void gpu_stamp(GpuPhase p);

    AdapterInfo adapter_;
    // Medidas: um anel de quadros com as consultas (o DISJOINT e os carimbos com o trecho que cada um fecha).
    struct GpuFrame {
        Com<ID3D11Query> disjoint;
        std::vector<Com<ID3D11Query>> stamps;
        std::vector<GpuPhase> phases;  // o trecho que termina em cada carimbo (o primeiro é o começo)
        int used = 0;
        bool pending = false;
        uint64_t seq = 0;  // o número do quadro desde reset_gpu_times
    };
    static constexpr int kGpuRing = 5;
    bool gpu_read(GpuFrame& f, bool wait);
    GpuFrame gpu_ring_[kGpuRing];
    int gpu_cur_ = -1;     // o quadro em gravação (-1: nenhum)
    int gpu_next_ = 0;
    bool profiling_ = false;
    GpuPhase phase_ = GpuPhase::Scene;
    GpuTimes gpu_times_;
    uint64_t gpu_seq_ = 0;
    std::vector<GpuFrameTimes> gpu_per_frame_;
    RenderCounters counters_;
    uint32_t env_parts_ = 0;
    bool throttle_ = false;
    bool tearing_ = false;   // DXGI_FEATURE_PRESENT_ALLOW_TEARING (o Present sem VSync não espera o monitor)
    UINT swap_flags_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    Com<ID3D11Query> throttle_q_[3];
    int throttle_i_ = 0;

    HWND hwnd_;
    int width_, height_;
    bool vsync_;
    bool offscreen_ = false;
    Com<ID3D11Texture2D> target_;  // o alvo de cor: o back buffer ou a textura fora da tela
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> ctx_;
    Com<IDXGISwapChain1> swap_;
    Com<ID3D11RenderTargetView> rtv_;
    Com<ID3D11Texture2D> depth_;
    Com<ID3D11DepthStencilView> dsv_;
    Com<ID3D11ShaderResourceView> depth_srv_;  // a profundidade lida pela passada de névoa
    Com<ID3D11VertexShader> vs_, vs_skin_, vs_skin_morph_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11PixelShader> ps_a2c_;            // ps_main com o alfa para a cobertura
    bool a2c_bound_ = false;                   // ps_a2c_ é o pixel shader em uso
    bool alpha_coverage_ = true;               // set_alpha_to_coverage
    uint32_t a2c_parts_ = 0;                   // partes desenhadas com a cobertura no último render()
    Com<ID3D11InputLayout> layout_, layout_skin_, layout_skin_morph_;
    Com<ID3D11Buffer> cb_frame_, cb_object_, cb_skin_, cb_fog_, cb_morph_;
    Com<ID3D11VertexShader> fog_vs_;
    Com<ID3D11PixelShader> fog_ps_;
    Com<ID3D11BlendState> fog_blend_;
    Com<ID3D11DepthStencilState> no_depth_;
    Com<ID3D11SamplerState> sampler_;
    Com<ID3D11RasterizerState> raster_solid_, raster_wire_;
    Com<ID3D11DepthStencilState> depth_state_;
    std::vector<std::pair<uint32_t, Com<ID3D11BlendState>>> gs_blends_;      // por GsBlendEq
    std::vector<std::pair<uint32_t, Com<ID3D11DepthStencilState>>> depths_;  // por (função, escrita)
    Com<ID3D11Buffer> sprite_vb_, sprite_ib_;
    size_t sprite_vb_vertices_ = 0, sprite_ib_quads_ = 0;
    Com<ID3D11Buffer> tri_vb_;  // triângulos soltos (estilhaços)
    size_t tri_vb_vertices_ = 0;
    Com<ID3D11Buffer> quad2d_vb_;  // 2D (psiDrawSprites)
    size_t quad2d_vb_vertices_ = 0;
    Com<ID3D11SamplerState> sampler_point_, sampler_linear_;  // 2D: sem mipmaps, coordenadas presas
    Com<ID3D11SamplerState> sampler_smooth_;                  // camada HD: bilinear com mipmaps
    std::unique_ptr<UiHdDevice> ui_;
    std::unique_ptr<UiModernDevice> modern_;
    // O desfoque do fundo dos menus Modern.
    void ensure_blur_targets();
    Com<ID3D11Texture2D> blur_full_;               // a cópia do alvo (tamanho da janela)
    Com<ID3D11ShaderResourceView> blur_full_srv_;
    Com<ID3D11Texture2D> blur_tex_[2];             // 1/4 da resolução, ida e volta
    Com<ID3D11RenderTargetView> blur_rtv_[2];
    Com<ID3D11ShaderResourceView> blur_srv_[2];
    int blur_w_ = 0, blur_h_ = 0;
    Com<ID3D11PixelShader> blur_ps_, blur_mix_ps_;
    Com<ID3D11Buffer> cb_blur_;
    Com<ID3D11RasterizerState> raster_2d_;                    // 2D: com scissor
    int shader_state_ = -1;
    Com<ID3D11ShaderResourceView> white_;
    Com<ID3D11ShaderResourceView> no_texture_;  // 0x80 em tudo: kQuadNoTexture
    Com<ID3D11Texture2D> loading_snapshot_;      // a tela de carga (save_loading_snapshot)
    Com<ID3D11Texture2D> blur_history_;          // o quadro anterior (screen_blur)
    Com<ID3D11ShaderResourceView> blur_history_srv_;
    Com<ID3D11BlendState> blur_history_blend_;
    bool blur_history_valid_ = false;
    std::vector<GpuMesh> meshes_;
    std::vector<Com<ID3D11ShaderResourceView>> textures_;
    std::vector<Com<ID3D11ShaderResourceView>> hd_textures_;
    std::vector<std::pair<uint32_t, Com<ID3D11ShaderResourceView>>> ui_images_;  // set_ui_image
    bool use_hd_ = true;
    Com<ID3D11VertexShader> blit_vs_;
    Com<ID3D11PixelShader> blit_ps_;
    // Opções do PC: o alvo próprio da cena (nulo: a cena vai direto ao back buffer, como antes).
    Com<ID3D11Texture2D> scene_color_;           // scene_w_ x scene_h_, com MSAA ou não
    Com<ID3D11RenderTargetView> scene_rtv_;
    Com<ID3D11Texture2D> scene_resolved_;        // com MSAA e escala: a cena resolvida, antes de reduzir
    Com<ID3D11ShaderResourceView> scene_srv_;    // a fonte da redução (sem MSAA)
    Com<ID3D11PixelShader> fog_ps_ms_, down_ps_;
    Com<ID3D11Buffer> cb_down_;
    int scene_w_ = 0, scene_h_ = 0;
    int scene_scale_ = 100, scene_msaa_ = 1;
    bool in_2d_ = false;                         // segunda passada: o 2D no tamanho da janela
    TextureFilter filter_ = TextureFilter::Aniso8;
    // Os filmes: a textura e o desenho do quadro (no começo de render, com movie_on_).
    std::unique_ptr<MovieLayer> movie_;
    bool movie_on_ = false;
    float movie_rect_[4]{};
};

}  // namespace nf
