#pragma once
// A tela de entrada (o launcher) do nightfire.exe (só do port; docs/port.md, "Instalação fácil", e docs/design.md, "A
// tela de entrada"). Antes do jogo, como os lançadores dos jogos de PC:
//  - aparece a cada partida normal (sem argumentos na linha de comando além de --config e --save-dir), com PLAY no foco:
//    Enter (A ou Start no controle, ou um clique) começa o jogo na hora;
//  - "Show at startup" ([ui] launcher no nightfire.ini, ligado por padrão) a desliga; desligada, segurar Shift enquanto o
//    jogo abre a traz de volta (platform/os.h, key_held);
//  - aparece sozinha quando nenhum disco serve (platform/disc_source.h: nem a cópia pessoal, nem os arquivos ou uma imagem
//    ao lado do executável, nem o [game] disc do nightfire.ini, nem um leitor de DVD com o disco), como a tela de
//    configuração da primeira partida que ela era;
//  - --setup (e --launcher) a força, também nas capturas; qualquer outro argumento a pula (as capturas, o --bench, o
//    --level, o --disc...: launcher_reason).
//
// No estilo Modern (o kit de modern_kit.h), sem nada do jogo (antes do disco não há texturas nem textos do jogo, só os do
// port). Três colunas: à esquerda o título, o disco em uso e a lista grande PLAY / QUIT; no meio as linhas, em quatro
// seções: DISC (o disco em uso, que escolhe outra imagem pela janela de escolha de arquivo; USE THE DISC IN DRIVE X: um por
// leitor com um disco de PS2; a cópia em arquivo único), DISPLAY (a resolução, o modo de tela e o VSync: as mesmas listas
// de valores da página VIDEO do jogo, platform/video_settings.h), INTERFACE (o estilo dos menus, Modern ou Original, e a
// camada 2D em HD) e STARTUP ("Show at startup"); à direita a descrição da linha com o foco. As caixas do kit: o problema
// de um disco, o andamento da cópia (a barra e Cancelar) e o fim dela.
//
// PLAY devolve as opções (settings()): o anfitrião (render/setup_app.h e o laço do nightfire.exe) as grava no
// nightfire.ini com as mesmas chaves das opções do jogo (store_video_settings, [ui] launcher) e o jogo abre com elas.
// QUIT sai sem gravar nada.
//
// Sem GPU e sem disco: a tela recebe o que mostrar (SetupView, montada pelo anfitrião), as opções e os modos do monitor, e
// a entrada de cada quadro (UiInput); devolve os pedidos (take_request: escolher a imagem, um leitor, jogar, copiar,
// cancelar a cópia, sair). Teclado, mouse e controle como nas outras telas Modern (NavState: Enter e Espaço escolhem, Esc
// volta, A/B no controle, Start joga).

#include <cstdint>
#include <string>
#include <vector>

#include "engine/pc_text.h"
#include "platform/disc_source.h"
#include "platform/video_settings.h"
#include "ui/modern_input.h"

namespace nf {
class Settings;
}

namespace nf::ui {

// ---------------------------------------------------------------- quando aparece

struct LauncherStart {
    bool forced = false;           // --setup ou --launcher
    bool disc_ok = false;          // a procura (ou o --disc) achou um disco que serve
    bool command_line = false;     // a linha de comando diz o que rodar (launcher_command_line)
    bool show_at_startup = true;   // [ui] launcher
    bool shift = false;            // Shift seguro na partida
};
enum class LauncherReason : uint8_t { None, Forced, NoDisc, Startup, Shift };
// Por que a tela aparece (None: o jogo começa direto). Na ordem: forçada; sem disco que sirva (sempre, também nas
// capturas, como a tela de configuração de antes); com a linha de comando dizendo o que rodar, nunca; "Show at startup"
// ligado; desligado, o Shift seguro.
LauncherReason launcher_reason(const LauncherStart& s);
const char* launcher_reason_name(LauncherReason r);  // nenhum, forcada, sem_disco, partida, shift
// A linha de comando diz o que rodar: algum argumento além de --config <ini>, --save-dir <pasta>, --setup, --launcher,
// --setup-pick <caminho> e --setup-input <roteiro> (argv[0] é o executável).
bool launcher_command_line(int argc, const wchar_t* const* argv);
// [ui] launcher no nightfire.ini ("Show at startup"; ligado se não houver).
bool launcher_enabled(const Settings& ini);

// ---------------------------------------------------------------- o que mostra

// O disco (o anfitrião monta; já no idioma dos textos do port).
struct SetupView {
    struct Drive {
        std::wstring root;     // "D:\"
        bool usable = false;   // o disco certo
        std::wstring problem;  // senão, por quê
        bool in_use = false;   // é o disco em uso
    };
    bool ready = false;          // há um disco que serve
    std::wstring disc;           // o caminho dele (ou do último recusado)
    std::wstring disc_kind;      // "Disc image", "DVD drive"...
    std::wstring disc_name;      // o nome curto para a linha (o arquivo, "Drive E:", "This program...")
    bool packed = false;         // o disco em uso é este programa (a cópia pessoal)
    std::wstring status;         // pronto (o código do disco), ou o problema
    bool problem = false;        // status é um problema
    std::vector<Drive> drives;   // os leitores com um disco de PS2
    bool can_pack = false;       // a cópia pessoal dá para fazer (um disco que serve, com SYSTEM.CNF)
    std::wstring pack_size;      // "1.9 GB"
    std::wstring pack_path;      // onde ela vai
    bool picking = false;        // a janela de escolha do Windows está aberta
};

// As opções da tela (o que PLAY grava e o jogo usa). Das de vídeo ela muda a resolução, o modo de tela, o VSync, o estilo
// dos menus e a camada 2D em HD; o resto de `video` passa como veio (o que o nightfire.ini e a linha de comando deram).
struct LauncherSettings {
    VideoSettings video;
    bool show_at_startup = true;  // [ui] launcher
    bool operator==(const LauncherSettings&) const = default;
};
// Grava no nightfire.ini (não salva o arquivo) as opções que mudaram desde `before`, com as chaves e a escrita das opções
// do jogo (store_video_settings: [video] width/height, window_mode, vsync; [ui] menu_style, hd) e [ui] launcher. Só as
// que mudaram: um valor padrão não vai para o arquivo por causa de outra opção. Devolve se gravou alguma.
bool store_launcher_settings(const LauncherSettings& now, const LauncherSettings& before, Settings& ini);

// O que o monitor oferece (do renderizador; os testes passam uma lista).
struct LauncherHardware {
    std::vector<DisplayMode> modes;  // os modos do monitor (em qualquer ordem, com repetições)
    DisplayMode desktop;             // o tamanho dele (0 x 0: não se sabe)
};

enum class SetupAction : uint8_t { None, Browse, Drive, Play, Pack, CancelPack, Quit };
struct SetupRequest {
    SetupAction action = SetupAction::None;
    int drive = -1;  // Drive: o índice em SetupView::drives
};

class SetupScreen {
public:
    // As linhas do meio (e a coluna da esquerda: Play, Quit).
    enum class Row : uint8_t {
        Section, Disc, Drive, Pack, Resolution, WindowMode, VSync, MenuStyle, HdUi, ShowAtStartup, Play, Quit
    };
    enum class Zone : uint8_t { Menu, Rows };

    explicit SetupScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }

    // Abre com o disco, as opções e o monitor: com um disco que serve, o foco em PLAY; sem, na linha do disco.
    void open(const SetupView& v, const LauncherSettings& s = LauncherSettings{}, const LauncherHardware& hw = LauncherHardware{});
    // O que mostrar agora (a cada mudança; as linhas acompanham os leitores).
    void set_view(const SetupView& v);
    const SetupView& view() const { return view_; }
    // As opções como estão agora (o que PLAY grava) e se mudaram desde open().
    const LauncherSettings& settings() const { return settings_; }
    bool settings_changed() const { return !(settings_ == opened_); }
    void frame(float dt, const UiInput& in, const Canvas& cv, const Measure& m);
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    SetupRequest take_request() {
        const SetupRequest r = request_;
        request_ = SetupRequest{};
        return r;
    }
    bool mouse_active() const { return nav_.mouse_active; }

    // As caixas (o anfitrião pede): uma mensagem com OK; o andamento da cópia (progress de 0 a 1, com Cancelar);
    // fechar a caixa aberta.
    void show_message(const std::wstring& title, const std::wstring& body);
    void show_progress(const std::wstring& title, const std::wstring& body, float progress);
    void hide_dialog();
    bool dialog_open() const { return dialog_.open; }
    // Leva o foco para PLAY (um disco acabou de servir).
    void focus_play();

    // ---- testes e relatório ----
    void command(Nav c) { pending_[int(c)] = true; }
    Zone zone() const { return zone_; }
    // O que tem o foco: Play ou Quit na coluna da esquerda, ou a linha do meio.
    Row focus_row() const;
    int focus() const { return zone_ == Zone::Menu ? menu_.focus : focus_; }  // Menu: 0 PLAY, 1 QUIT; Rows: índice
    int row_count() const { return int(rows_.size()); }
    Row row_kind(int i) const { return rows_[size_t(i)].kind; }
    int row_drive(int i) const { return rows_[size_t(i)].drive; }
    int row_of(Row r) const;  // a primeira linha desse tipo (-1 nenhuma)
    bool row_locked(int i) const;
    bool play_locked() const { return !view_.ready; }
    // O retângulo de uma linha do meio (na tela virtual, com a rolagem de agora), de uma opção da lista aberta e de
    // PLAY / QUIT (0, 1).
    Rect row_rect(int i) const;
    Rect option_rect(int option) const;
    Rect menu_rect(int i) const { return i >= 0 && size_t(i) < menu_.rects.size() ? menu_.rects[size_t(i)] : Rect{}; }
    int open_dropdown() const { return dd_row_; }
    int dropdown_focus() const { return dd_focus_; }
    // As escolhas de uma linha de lista (os textos) e a escolhida.
    std::vector<std::wstring> choice_labels(Row r) const;
    int choice_index(Row r) const;
    std::wstring value_text(Row r) const;
    const Dialog& dialog() const { return dialog_; }
    float scroll() const { return scroll_.value(); }
    std::string describe() const;

private:
    struct RowInfo {
        Row kind = Row::Section;
        int drive = -1;
        PcText label = PcText::Count;
        float y = 0.0f;  // na lista, sem a rolagem
        float h = 0.0f;
    };
    struct RowAnim {
        Tween label;   // 0 branco, 1 ouro
        Tween knob;    // chave
        Tween change;  // troca de valor
        std::wstring old_value, shown_value;
    };
    void set_rows();
    void layout_rows();
    bool focusable(int i) const;
    int first_focusable() const;
    int step_focus(int from, int dir) const;
    void set_zone_menu(int item, bool animate);
    void set_row_focus(int i, bool animate);
    void place_mark(bool animate);
    void ensure_visible(int i);
    void reveal_open_list();  // ao abrir uma lista: ela inteira, ou a opção atual no meio se não couber
    float content_height() const;
    float view_height() const;
    Rect underline_of(int i) const;
    // O modelo.
    std::vector<DisplayMode> resolutions() const;
    void set_choice(Row r, int index);
    bool toggle_on(Row r) const;
    void set_toggle(Row r, bool on);
    // As ações.
    void activate_menu(int item);
    void activate_row(int i);
    void act(Nav c);
    void act_rows(Nav c);
    void act_dropdown(Nav c);
    void open_dropdown_at(int i);
    void close_dropdown();
    void mouse(const Frame& f);
    int hit_row(float x, float y) const;
    int hit_option(float x, float y) const;
    // O desenho.
    Description description() const;
    void prompts(PromptBar& bar, bool pad) const;
    void draw_left(DrawList& dl, const Frame& f, float k, float dx) const;
    void draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const;

    const Tokens* tk_;
    std::string build_;
    SetupView view_;
    LauncherSettings settings_;
    LauncherSettings opened_;
    LauncherHardware hw_;
    float time_ = 0.0f;
    Tween appear_;
    BigMenu menu_;           // PLAY, QUIT
    Zone zone_ = Zone::Menu;
    std::vector<RowInfo> rows_;
    std::vector<RowAnim> anims_;
    int focus_ = -1;         // a linha do meio com o foco (Rows)
    int dd_row_ = -1;        // a lista aberta
    int dd_focus_ = 0;
    int dd_closing_ = -1;    // a lista que está fechando (a animação)
    Tween dd_open_;
    Tween scroll_;
    float scroll_target_ = 0.0f;
    FocusMark mark_;         // o foco das linhas do meio
    PromptBar bar_;
    Dialog dialog_;
    bool dialog_progress_ = false;  // a caixa é a do andamento (o botão cancela a cópia)
    Toast toast_;
    NavState nav_;
    bool pending_[kNavCount] = {};
    SetupRequest request_;
    int device_ = 0;
    mutable Canvas last_cv_;
};

// O tamanho em texto ("1.9 GB", "650 MB").
std::wstring setup_size_text(uint64_t bytes);
// O motivo de um disco recusado, no idioma do port (vazio: o disco serve).
std::wstring setup_problem_text(const DiscCheck& c);
// O estado de um disco que serve ("Ready: 007: Nightfire (PS2, USA), SLUS-20579").
std::wstring setup_ready_text(const DiscCheck& c);
// O tipo de um disco ("Disc image", "DVD drive"...).
std::wstring setup_kind_text(DiscKind k);
// O nome curto de um disco para a linha: o arquivo de uma imagem, a pasta, "Drive E:", "This program (single-file copy)".
std::wstring setup_disc_name(DiscKind k, const std::wstring& location);

}  // namespace nf::ui
