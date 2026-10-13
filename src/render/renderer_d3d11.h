#pragma once
// O renderizador Direct3D 11: o backend do Windows de render/renderer.h (o Renderer::Impl). Só os arquivos do backend
// (renderer_d3d11.cpp) o incluem. A descrição do que ele faz está em render/renderer.h.

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "render/renderer.h"

namespace nf {

class MovieLayer;
class UiHdDevice;
class UiModernDevice;

// GpuImage (render/renderer.h) é, neste backend, um ID3D11ShaderResourceView.
inline ID3D11ShaderResourceView* srv_of(GpuImage* image) { return reinterpret_cast<ID3D11ShaderResourceView*>(image); }
inline GpuImage* image_of(ID3D11ShaderResourceView* srv) { return reinterpret_cast<GpuImage*>(srv); }

struct Renderer::Impl {
    // offscreen: desenha numa textura width x height em vez do back buffer da janela (capturas).
    // adapter: o adaptador de vídeo (nulo: o padrão do sistema, como antes).
    Impl(HWND hwnd, int width, int height, bool vsync, bool offscreen, IDXGIAdapter1* adapter);
    ~Impl();

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

    // ---- o estilo de menu Modern (src/ui, render/ui_modern_draw_d2d.h; só do port) ----
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

    int width() const { return width_; }
    int height() const { return height_; }

    // ---- os filmes (só do port; platform/psi_movie.h, render/movie_layer_d3d11.h) ----
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
    bool adapter_chosen_ = false;  // o adaptador pedido abriu (Renderer::adapter_chosen)
};

}  // namespace nf
