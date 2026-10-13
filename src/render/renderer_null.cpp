// O renderizador que não desenha (só do port; docs/port.md, "macOS: os backends"): a fachada render/renderer.h com um
// Renderer::Impl que só guarda o estado. É o renderizador do nightfire do macOS na etapa 3c (a janela, a entrada, o som,
// os controles e o resto do programa já rodam; o Metal é a etapa 3d e troca este arquivo pelo dele) e serve o Linux, que
// compila o laço do nightfire só para conferir (sem ligar). Nada aqui é do sistema: os modos do monitor vêm do backend da
// janela (platform/display_modes.h).
//
// O que ele faz: guarda o tamanho, as opções de vídeo (a escala, o MSAA, o filtro, o VSync), as malhas e as imagens só
// como contagem; o render() e o resto do desenho não fazem nada; save_png grava a cor com que o quadro foi limpo (o
// FrameParams::clear_color do último render(), ou a do clear_target) no tamanho do alvo, pelo platform/image_io.h: as
// capturas do macOS saem (o --screenshot, o relatório .txt), lisas. O 2D vetorial (os menus Modern, a camada HD) não
// existe aqui (modern_ok() e ui_hd_ok() falsos): o jogo cai nos menus Original, como no Windows sem Direct2D. Também
// ficam aqui as funções neutras que o renderizador do Windows define (gs_alpha_of_blend, gpu_phase_name), o
// enumerate_adapters (nenhum adaptador: o padrão do sistema) e o caminho de Direct3D 9 do RTX Remix, que só existe no
// Windows (create_remix_d3d9 recusa).
//
// O que a 3d tem de trocar: este arquivo inteiro pelo backend do Metal (os shaders de renderer_d3d11.cpp em MSL, o 2D
// vetorial com o Core Text e o Core Graphics, o leitor de SVG, as consultas de tempo do --bench, o adaptador pelo
// MTLDevice), desenhando na CAMetalLayer da NSView que Window::native_handle() dá.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "platform/display_modes.h"
#include "platform/image_io.h"
#include "render/gpu_adapters.h"
#include "render/remix_d3d9.h"
#include "render/renderer.h"

namespace nf {

uint64_t gs_alpha_of_blend(Blend mode) {
    // O mesmo de renderer_d3d11.cpp (os números de psiSetUpColourBlend).
    switch (mode) {
        case Blend::Additive: return 0x48;
        case Blend::Alpha: return 0x44;
        case Blend::Subtractive: return 0x42;
        default: return 0x2a;
    }
}

const char* gpu_phase_name(GpuPhase p) {
    switch (p) {
        case GpuPhase::Scene: return "cena";
        case GpuPhase::Fog: return "nevoa";
        case GpuPhase::Resolve: return "resolucao";
        case GpuPhase::TwoD: return "2d";
        case GpuPhase::Blur: return "desfoque";
        case GpuPhase::Modern: return "direct2d";
        default: return "?";
    }
}

std::vector<AdapterInfo> enumerate_adapters(bool) { return {}; }

std::unique_ptr<RemixD3D9Renderer> create_remix_d3d9(void*, int, int, bool, const AdapterInfo*) {
    throw std::runtime_error("o caminho de Direct3D 9 para o RTX Remix (--renderer d3d9ff) só existe no Windows");
}

void* create_remix_hidden_window(int, int) { return nullptr; }

struct Renderer::Impl {
    void* window = nullptr;
    int width = 0, height = 0;
    bool vsync = true;
    bool offscreen = false;
    AdapterInfo adapter;
    bool hd = true;
    int scale = 100;
    int msaa = 1;
    TextureFilter filter = TextureFilter::Aniso8;
    bool profiling = false;
    bool alpha_coverage = true;
    bool throttle = false;
    bool exclusive = false;
    int meshes = 0;
    std::vector<uint32_t> ui_keys;
    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool blur = false;
    std::vector<DisplayMode> modes;
    DisplayMode desktop;
    GpuTimes times;
    std::vector<GpuFrameTimes> per_frame;
    RenderCounters counters;
    std::chrono::steady_clock::time_point next_present{};
};

Renderer::Renderer(void* window, int width, int height, bool vsync, bool offscreen, const AdapterInfo*) {
    impl_ = std::make_unique<Impl>();
    impl_->window = window;
    impl_->width = width;
    impl_->height = height;
    impl_->vsync = vsync;
    impl_->offscreen = offscreen;
    impl_->adapter.name = "nenhum (sem renderizador: etapa 3c)";
    // Os modos do monitor da janela (as listas de resolução da tela de entrada e da página VIDEO).
    system_display_modes(window, &impl_->modes, &impl_->desktop);
}

Renderer::~Renderer() = default;

void Renderer::resize(int width, int height) {
    if (width > 0 && height > 0 && !impl_->offscreen) {
        impl_->width = width;
        impl_->height = height;
    }
}
void Renderer::reset_resources() { impl_->meshes = 0; }
void Renderer::set_texture(int, const Texture&) {}
void Renderer::update_texture(int, const Texture&) {}
bool Renderer::set_texture_dds(int, const std::wstring&) { return false; }
void Renderer::set_ui_image(uint32_t key, const uint8_t*, int, int) {
    if (std::find(impl_->ui_keys.begin(), impl_->ui_keys.end(), key) == impl_->ui_keys.end()) impl_->ui_keys.push_back(key);
}
GpuImage* Renderer::ui_image(uint32_t) const { return nullptr; }
size_t Renderer::ui_images() const { return impl_->ui_keys.size(); }
void Renderer::use_hd(bool on) { impl_->hd = on; }
bool Renderer::using_hd() const { return impl_->hd; }
int Renderer::add_mesh(const EntityMesh&) { return impl_->meshes++; }

void Renderer::render(const std::vector<DrawItem>& items, const FrameParams& frame) {
    std::memcpy(impl_->clear, frame.clear_color.data(), sizeof(impl_->clear));
    impl_->counters.items += items.size();
}
void Renderer::render_2d(const std::vector<DrawItem>&, const FrameParams&) {}
// Sem desenho não há o Present que segura o laço no ritmo do monitor: com o VSync, uma espera até o próximo 1/60 s (o
// laço do jogo não gira solto a 100% da CPU com a janela preta).
void Renderer::present() {
    if (!impl_->vsync || impl_->offscreen) return;
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::microseconds(16667);
    const auto now = clock::now();
    if (impl_->next_present < now - period) impl_->next_present = now;
    impl_->next_present += period;
    std::this_thread::sleep_until(impl_->next_present);
}

void Renderer::save_png(const std::wstring& path) {
    // A cor do último quadro limpo, opaca (como o save_png do Direct3D 11 força o alfa).
    const int w = std::max(1, impl_->width), h = std::max(1, impl_->height);
    const auto to8 = [](float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const uint8_t bgra[4] = {to8(impl_->clear[2]), to8(impl_->clear[1]), to8(impl_->clear[0]), 255};
    std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) std::memcpy(&pixels[i], bgra, 4);
    write_png_bgra(path, w, h, pixels.data());  // platform/image_io.h (o ImageIO no macOS); lança se falhar
}

void Renderer::screen_blur(float keep) { impl_->blur = keep > 0.0f; }
bool Renderer::screen_blur_active() const { return impl_->blur; }
void Renderer::clear_target(const float rgba[4]) { std::memcpy(impl_->clear, rgba, sizeof(impl_->clear)); }
void Renderer::save_loading_snapshot() {}
bool Renderer::restore_loading_snapshot() { return false; }
void Renderer::release_loading_snapshot() {}
GpuImageRef Renderer::keep_texture(int) const { return nullptr; }
bool Renderer::offscreen() const { return impl_->offscreen; }

void Renderer::set_ui_hd(const UiHdConfig*, const std::wstring&) {}
std::string Renderer::ui_hd_describe() const { return "desligada (sem renderizador)"; }
float Renderer::ui_hd_measure(const std::wstring&, float) const { return 0.0f; }
bool Renderer::ui_hd_ok() const { return false; }

bool Renderer::set_modern_ui(const std::wstring&) { return false; }
bool Renderer::modern_ok() const { return false; }
float Renderer::modern_measure(const std::wstring&, const ui::TextStyle&) const { return 0.0f; }
void Renderer::blur_background(float) {}
void Renderer::draw_modern(const ui::DrawList&, const ui::Canvas&) {}
std::string Renderer::modern_describe() const { return "desligado (sem renderizador)"; }
bool Renderer::read_texture(int, int&, int&, std::vector<uint8_t>&) { return false; }
bool Renderer::write_png(const std::wstring& path, int width, int height, const uint8_t* rgba) {
    return write_png_rgba(path, width, height, rgba);
}

int Renderer::width() const { return impl_->width; }
int Renderer::height() const { return impl_->height; }

void Renderer::set_movie_frame(const uint8_t*, int, int, uint64_t) {}
void Renderer::set_movie_background(bool, const float[4]) {}
GpuImage* Renderer::movie_image() const { return nullptr; }

const AdapterInfo& Renderer::adapter() const { return impl_->adapter; }
bool Renderer::adapter_chosen() const { return false; }
void Renderer::set_profiling(bool on) { impl_->profiling = on; }
bool Renderer::profiling() const { return impl_->profiling; }
void Renderer::gpu_frame_begin() {}
void Renderer::gpu_frame_end() {}
void Renderer::gpu_collect(bool) {}
const GpuTimes& Renderer::gpu_times() const { return impl_->times; }
void Renderer::reset_gpu_times() {
    impl_->times = GpuTimes{};
    impl_->per_frame.clear();
}
const std::vector<Renderer::GpuFrameTimes>& Renderer::gpu_per_frame() const { return impl_->per_frame; }
const RenderCounters& Renderer::counters() const { return impl_->counters; }
void Renderer::set_alpha_to_coverage(bool on) { impl_->alpha_coverage = on; }
bool Renderer::alpha_to_coverage() const { return impl_->alpha_coverage; }
uint32_t Renderer::alpha_coverage_parts() const { return 0; }
uint32_t Renderer::env_parts() const { return 0; }
void Renderer::reset_counters() { impl_->counters = RenderCounters{}; }
void Renderer::set_offscreen_throttle(bool on) { impl_->throttle = on; }

void Renderer::set_vsync(bool on) { impl_->vsync = on; }
bool Renderer::vsync() const { return impl_->vsync; }
bool Renderer::set_scene_quality(int scale_percent, int msaa) {
    impl_->scale = std::clamp(scale_percent, 50, 200);
    impl_->msaa = msaa == 2 || msaa == 4 || msaa == 8 ? msaa : 1;
    return true;
}
int Renderer::scene_scale() const { return impl_->scale; }
int Renderer::scene_msaa() const { return impl_->msaa; }
int Renderer::scene_width() const { return impl_->width * impl_->scale / 100; }
int Renderer::scene_height() const { return impl_->height * impl_->scale / 100; }
std::vector<int> Renderer::supported_msaa() const { return {2, 4, 8}; }
void Renderer::set_texture_filter(TextureFilter f) { impl_->filter = f; }
TextureFilter Renderer::texture_filter() const { return impl_->filter; }
bool Renderer::tearing() const { return false; }
// A tela cheia exclusiva do DXGI (o modo do monitor trocado) não existe no macOS: o modo Exclusive das opções é a janela
// sem bordas do tamanho do monitor, que a Window já abriu (o laço cria a janela assim para os dois modos de tela cheia).
// Aceita, sem fazer nada; o Metal da etapa 3d fica com a mesma regra.
bool Renderer::set_exclusive_fullscreen(bool on, int, int) {
    impl_->exclusive = on && !impl_->offscreen;
    return on == impl_->exclusive;
}
bool Renderer::exclusive_fullscreen() const { return impl_->exclusive; }
std::vector<DisplayMode> Renderer::display_modes() const { return impl_->modes; }
DisplayMode Renderer::desktop_mode() const { return impl_->desktop; }

}  // namespace nf
