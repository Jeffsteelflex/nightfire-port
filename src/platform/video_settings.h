#pragma once
// Opções de vídeo e de interface do PC (só do port: o PS2 não tem nenhuma delas). A parte sem GPU:
// os valores (VideoSettings), a leitura e a gravação em nightfire.ini (seções [video] e [ui], ver
// platform/settings.h), as listas de valores que a página de opções oferece, a lista de resoluções e o
// controlador que aplica uma configuração nova com a confirmação de "manter estas configurações?".
//
// Quem aplica de verdade (janela, renderizador, camada HD) é o nightfire.exe: main.cpp põe o gancho
// VideoController::set_apply. A página de opções (engine/menu_pc_options.h) só edita uma cópia e chama
// VideoController::apply; os testes trocam o gancho por um que só registra.
//
// Confirmação: uma mudança de resolução ou de modo de janela pode deixar a tela preta ou fora do monitor,
// então ela fica valendo por kVideoConfirmSeconds; sem keep() nesse tempo (ou com revert()), a
// configuração anterior volta inteira. As outras mudanças são gravadas na hora.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/gpu_adapter.h"

namespace nf {

class Settings;

enum class WindowMode : uint8_t {
    Windowed = 0,    // janela com bordas, do tamanho da resolução
    Borderless = 1,  // janela sem bordas do tamanho do monitor (o --fullscreen de antes)
    Exclusive = 2,   // tela cheia exclusiva do DXGI, com troca do modo do monitor
};

enum class TextureFilter : uint8_t {
    Bilinear = 0,   // MIN_MAG_LINEAR_MIP_POINT
    Trilinear = 1,  // MIN_MAG_MIP_LINEAR
    Aniso2 = 2,
    Aniso4 = 3,
    Aniso8 = 4,     // o padrão (o que o renderizador sempre usou)
    Aniso16 = 5,
};
// Grau de anisotropia do filtro (0 para bilinear e trilinear).
int texture_filter_anisotropy(TextureFilter f);
const char* texture_filter_name(TextureFilter f);  // nome no ini: bilinear, trilinear, aniso2...
bool parse_texture_filter(const std::string& s, TextureFilter* out);
const char* window_mode_name(WindowMode m);        // windowed, borderless, exclusive
bool parse_window_mode(const std::string& s, WindowMode* out);

// O estilo dos menus (só do port; [ui] menu_style): Modern é o sistema de desenho próprio do port
// (src/ui, docs/design.md), desenhado com Direct2D na resolução da janela; Original, os menus do jogo como
// foram portados (a página de opções do PC em abas, engine/menu_pc_options.h).
enum class MenuStyle : uint8_t { Modern = 0, Original = 1 };
const char* menu_style_name(MenuStyle m);  // modern, original
bool parse_menu_style(const std::string& s, MenuStyle* out);

struct DisplayMode {
    int width = 0;
    int height = 0;
    bool operator==(const DisplayMode&) const = default;
};

struct VideoSettings {
    // [video]
    int width = 1600;   // resolução: o tamanho da janela (Windowed) ou o modo do monitor (Exclusive)
    int height = 900;
    WindowMode window_mode = WindowMode::Windowed;
    bool vsync = true;
    int render_scale = 100;  // % do tamanho da janela em que a cena 3D é desenhada (o 2D fica na janela)
    int msaa = 1;            // amostras do MSAA da cena 3D (1 desligado, 2, 4, 8)
    TextureFilter filter = TextureFilter::Aniso8;
    float fov = 60.0f;       // campo de visão vertical em graus (o horizontal cresce com a largura)
    bool hd_textures = true;
    // O ritmo dos quadros (render/frame_interp.h, main.cpp): o limite de quadros por segundo (0 = desligado, sem
    // limite: fps_cap = off ou unlimited no ini), a interpolação entre os ticks de 60 Hz da lógica (interpolation;
    // desligada, cada quadro desenha o último tick) e o olhar do mouse sem o atraso dela (instant_mouse_look).
    int fps_cap = 0;
    bool interpolation = true;
    bool instant_mouse_look = true;
    // O adaptador de vídeo (platform/gpu_adapter.h): auto, integrated, discrete, um índice ou um trecho do nome. Só
    // vale na próxima partida (o dispositivo do Direct3D é criado uma vez): o gancho de aplicação não o usa.
    std::string adapter = "auto";
    // [ui]
    bool hd_ui = false;      // camada 2D em alta definição (F4)
    bool hud_edges = false;  // HUD preso às bordas da janela (só com a camada HD)
    std::string language = "en";  // idioma dos textos só do port (language_pc): pt ou en
    MenuStyle menu_style = MenuStyle::Modern;  // menu_style: modern ou original
    bool operator==(const VideoSettings&) const = default;
};

// Lê as chaves de [video] e [ui] que existirem (as que faltam ficam como estão em `v`).
void load_video_settings(const Settings& s, VideoSettings& v);
// Grava todas as chaves de `v` (a chave antiga [video] fullscreen dá lugar a window_mode).
void store_video_settings(const VideoSettings& v, Settings& s);

// A resolução ou o modo de janela mudou: a mudança precisa de confirmação.
bool video_needs_confirmation(const VideoSettings& from, const VideoSettings& to);

// As resoluções da página: as do monitor (raw, em qualquer ordem e com repetições) de pelo menos
// 640 x 480, em ordem crescente de largura e altura, sem repetidas, mais a atual se ela não estiver lá.
std::vector<DisplayMode> build_mode_list(std::vector<DisplayMode> raw, DisplayMode current);
// As resoluções que a opção percorre no modo `mode`: na janela, as que cabem no monitor (desktop; 0 x 0:
// todas); na tela cheia exclusiva, as do monitor; sem bordas, só a do monitor. A atual sempre entra.
std::vector<DisplayMode> resolution_choices(const std::vector<DisplayMode>& modes, DisplayMode desktop, WindowMode mode,
                                            DisplayMode current);
// A resolução depois de trocar o modo de janela (as duas páginas de opções): numa janela maior que o monitor,
// a maior que cabe nele; na tela cheia exclusiva, a do monitor quando a atual não é um modo dele.
void fit_resolution_to_mode(VideoSettings& v, const std::vector<DisplayMode>& modes, DisplayMode desktop);
// O próximo (dir = +1 ou -1, dando a volta) da lista; sem `current` nela, o primeiro maior (ou menor).
DisplayMode cycle_mode(const std::vector<DisplayMode>& modes, DisplayMode current, int dir);

// A opção "Graphics adapter" das duas páginas: os valores que ela oferece, em ordem ("auto", "integrated",
// "discrete" e um por nome de adaptador de `list`, menos o de software; a mesma placa pode aparecer mais de uma vez na
// lista do DXGI, e o nome escolhe a primeira; duas placas iguais só se separam pelo índice, no ini ou no --adapter),
// mais o atual no fim se for outro (um trecho de nome ou um índice escrito no ini).
std::vector<std::string> adapter_choice_values(const std::vector<AdapterInfo>& list, const std::string& current);
// O nome curto de um adaptador para a página Original (sem "(TM)", "(R)", "GeForce" nem o fabricante no começo: "RTX 5090").
std::string adapter_short_name(const std::string& name);

// Os valores que a página oferece para cada opção, em ordem; o atual entra se não for um deles.
std::vector<int> render_scale_values(int current);        // 50 75 100 125 150 200
std::vector<int> msaa_values(const std::vector<int>& supported, int current);  // 1 + os suportados
std::vector<int> fov_values(int current);                  // 40 a 100 de 5 em 5
std::vector<int> fps_cap_values(int current);              // 0 (desligado) 30 60 120 144 165 240
// O limite de quadros no ini: "off" (ou "unlimited", "0") ou o número (10 a 1000).
std::string fps_cap_name(int cap);
bool parse_fps_cap(const std::string& s, int* out);
// O próximo valor da lista (dir = +1 ou -1, dando a volta nas pontas); sem `current` na lista, o
// primeiro (ou o último).
int cycle_value(const std::vector<int>& values, int current, int dir);

constexpr float kVideoConfirmSeconds = 15.0f;

class VideoController {
public:
    // next, prev: aplicar `next` (prev é o que estava valendo).
    using ApplyFn = std::function<void(const VideoSettings& next, const VideoSettings& prev)>;
    // Grava a configuração (nightfire.ini); falso se não deu.
    using StoreFn = std::function<bool(const VideoSettings&)>;

    void init(const VideoSettings& live);
    void set_apply(ApplyFn f) { apply_ = std::move(f); }
    void set_store(StoreFn f) { store_ = std::move(f); }
    const VideoSettings& live() const { return live_; }
    // O que o hardware oferece (main.cpp, do DXGI): os modos do monitor, o tamanho dele e as amostras de
    // MSAA que o renderizador aceita.
    void set_hardware(std::vector<DisplayMode> modes, DisplayMode desktop, std::vector<int> msaa) {
        modes_ = std::move(modes);
        desktop_ = desktop;
        msaa_ = std::move(msaa);
    }
    const std::vector<DisplayMode>& display_modes() const { return modes_; }
    DisplayMode desktop() const { return desktop_; }
    const std::vector<int>& msaa_support() const { return msaa_; }
    // Os adaptadores de vídeo (main.cpp, do DXGI) e o nome do que o renderizador usa agora.
    void set_adapters(std::vector<AdapterInfo> list, std::string in_use) {
        adapters_ = std::move(list);
        adapter_in_use_ = std::move(in_use);
    }
    const std::vector<AdapterInfo>& adapters() const { return adapters_; }
    const std::string& adapter_in_use() const { return adapter_in_use_; }
    // Só para o gancho de aplicação corrigir o que não deu (MSAA sem suporte, por exemplo).
    void correct_live(const VideoSettings& v) { live_ = v; }

    // Aplica `next`. Com mudança de resolução ou de modo de janela, começa a confirmação e devolve true
    // (nada é gravado ainda); senão grava e devolve false. Durante uma confirmação, a anterior é a de
    // antes dela.
    bool apply(const VideoSettings& next);
    bool confirming() const { return confirming_; }
    float remaining() const { return remaining_; }
    // Conta o tempo da confirmação; ao acabar, volta à anterior (revert) e devolve true.
    bool tick(float seconds);
    void keep();    // confirma: grava a configuração em vigor
    void revert();  // desfaz a última aplicação (volta à anterior, sem gravar)

    struct Stats {
        uint32_t applied = 0;
        uint32_t stored = 0;
        uint32_t kept = 0;
        uint32_t reverted = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void store();
    VideoSettings live_;
    VideoSettings before_;  // a anterior, durante a confirmação
    bool confirming_ = false;
    float remaining_ = 0.0f;
    ApplyFn apply_;
    StoreFn store_;
    Stats stats_;
    std::vector<DisplayMode> modes_;
    DisplayMode desktop_;
    std::vector<int> msaa_{2, 4, 8};
    std::vector<AdapterInfo> adapters_;
    std::string adapter_in_use_;
};

// O controlador do executável (main.cpp põe os ganchos; a página de opções o usa).
VideoController& video_controller();

}  // namespace nf
