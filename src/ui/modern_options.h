#pragma once
// A tela de opções do PC no estilo Modern (só do port; docs/design.md e docs/port.md, "Menus no estilo
// Modern"). Abre pela linha OPTIONS da pausa quando [ui] menu_style = modern (engine/menu_pc_options.h,
// MenuPc_OpenOptions) e cobre a janela inteira: a cena desfocada e escurecida atrás, a barra de cima (PAUSE
// MENU · OPTIONS, com Q/E), as categorias à esquerda, as linhas no meio, a descrição à direita e os comandos
// embaixo.
//
// As mesmas opções e o mesmo comportamento da página Original (engine/menu_pc_options.h e
// menu_pc_controls.h), pelas mesmas peças: Vídeo, Gráficos e Interface editam uma cópia de nf::VideoSettings
// aplicada com "Apply changes" (F, Y no controle) pelo nf::video_controller() (resolução e modo de tela com a
// confirmação de 15 s, agora num diálogo Modern); Controles (o aparelho e o mouse: sensibilidade, a da mira e a
// inversão), Teclas e Controle mudam na hora e vão
// para o nightfire.ini (nf::persist_controls); a tabela de teclas religa pela captura de psi_input
// (psiInput_BeginCapture), por célula: PRIMARY e SECONDARY (as duas do teclado/mouse, nf::bind_slot) e
// CONTROLLER. DLSS/DLAA fica travado ("Coming soon").
//
// As opções do próprio jogo (as das páginas "Advanced Options" e "AV Options" do menu principal) entram aqui, lidas e
// escritas no lugar do jogo por nf::game_option/set_game_option (platform/game_options.h; o .ini segue a regra de
// lá) e valendo na hora, também na missão em andamento (o jogo lê cada byte a cada tick):
// - GAMEPLAY: mira automática, mira na tela, mira manual e agachar (Toggle/Hold), troca automática de arma, itens
//   piscando e HUD sempre visível (os bytes de PlayerSetting[0]); e duas só do PC, que valem na hora e vão para
//   [game] (platform/pc_gameplay.h): o estilo de mira (engine/player_ads.h) e a câmera lenta da roda de armas
//   (engine/weapon_wheel.h);
// - INTERFACE: as legendas (DrawInfo[2], [ui] subtitles), que valem na hora (as outras linhas esperam o Apply);
// - AUDIO: os volumes da música e dos efeitos (SFXMusicSetVolume/SFXSetVolume, 0-100 em passos de 5 como as barras
//   0-20 do jogo); sem módulo de áudio, a descrição avisa que o volume só fica guardado;
// - CONTROLLER: a vibração (PlayerSetting::unknown_09, o mesmo valor da "Vibration" do jogo).
// A descrição de cada uma é o texto de ajuda do jogo para o valor atual (help_table). As obsoletas no PC (widescreen,
// ajuste de tela, tela dividida, modo do som) não aparecem: no estilo Original continuam nas páginas do jogo.
//
// Sem GPU: a tela recebe a entrada de cada quadro de vídeo (UiInput, montada pelo main a partir do estado do
// teclado, do mouse e do controle), anda as animações em tempo real e monta uma lista de desenho (DrawList)
// na tela virtual de 1920 x 1080 (modern_ui.h); o renderizador (render/ui_modern_draw.h) desenha. Os testes
// usam a mesma tela com comandos e cliques sintéticos.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/pc_text.h"
#include "platform/video_settings.h"
#include "ui/modern_kit.h"

namespace nf {
struct PadState;
}

namespace nf::ui {

enum class Cat : uint8_t { Video, Graphics, Interface, Gameplay, Audio, Controls, MouseKeyboard, Controller, Count };
constexpr int kCatCount = int(Cat::Count);
// O nome no --menu-tab (video, graphics, interface, gameplay, audio, controls, keys, controller; também "mouse" para
// CONTROLS e "bindings" para KEY BINDINGS). Cat::MouseKeyboard é a categoria KEY BINDINGS (só a tabela de teclas: o
// mouse fica em CONTROLS, com o aparelho; o controle, inversão do Y incluída, em CONTROLLER).
const char* cat_name(Cat c);
bool parse_cat(const std::string& name, Cat* out);

// Cada opção da tela.
enum class Opt : uint8_t {
    None, Resolution, WindowMode, VSync, Fov, RenderScale, AntiAliasing, Filter, HdTextures, Dlss, HdUi, HudAnchor,
    MenuStyle, Language, Music, Effects, Device, ButtonLayout, InvertMouse, InvertPad, MouseSens, AimSens, PadSens,
    Deadzone, Triggers, Vibration, AutoAim, Crosshairs, AimMode, CrouchMode, AutoSwitch, Flashing, HudAlways, Subtitles,
    Apply, RestoreKeys, KeyRow, FpsCap, Interpolation, InstantMouse, Adapter, AimStyle, WheelSlowmo,
};
enum class Kind : uint8_t { Section, Dropdown, Toggle, Slider, Info, Locked, LockedSlider, Action, TableHead, KeyRow };

struct Item {
    Kind kind = Kind::Section;
    Opt opt = Opt::None;
    PcText label = PcText::Count;
    int command = -1;  // KeyRow: o comando de input_bindings
    float y = 0.0f;    // na lista (0 = o topo da primeira), sem a rolagem
    float h = 0.0f;
};

// A entrada de um quadro de vídeo.
struct UiInput {
    const bool* keys = nullptr;  // 256, pelo código virtual (nulo: nada)
    bool mouse_inside = false;
    float mouse_x = 0.0f, mouse_y = 0.0f;  // pixels da janela
    bool lmb = false;
    bool back_button = false;  // botão direito ou Mouse4
    float wheel = 0.0f;        // dentes (positivo para cima)
    const nf::PadState* pad = nullptr;
    int device = 0;            // psiInput_ActiveDevice: 0 teclado e mouse, 1 controle
};

class OptionsScreen {
public:
    enum class Cmd : uint8_t { Up, Down, Left, Right, Select, Back, TabPrev, TabNext, Defaults, Apply };
    enum class Zone : uint8_t { Categories, Content };

    explicit OptionsScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }
    // Dentro da pausa Modern (src/ui/modern_pause.h, a aba OPTIONS): o fundo e a barra de cima são da pausa; Q/E e
    // o voltar da coluna das categorias viram pedidos para ela (take_tab_request, take_back_request).
    void set_embedded(bool on) { embedded_ = on; }
    bool embedded() const { return embedded_; }
    int take_tab_request() {
        const int t = host_tab_;
        host_tab_ = 0;
        return t;
    }
    bool take_back_request() {
        const bool b = host_back_;
        host_back_ = false;
        return b;
    }

    // Abre na categoria (a cópia pendente começa igual à configuração em vigor), com o foco na primeira linha dela.
    void open(Cat cat);
    // Deep link: abre direto numa categoria, para quem chega por ela de fora das opções (o menu principal Modern abre
    // GAMEPLAY, CONTROLLER e AUDIO pelo menu do codinome). Como open(cat), mais: com back_leaves, voltar das linhas sai
    // da tela (request_close; embutida, take_back_request) em vez de ir à coluna das categorias, que continua lá (o
    // mouse troca de categoria; o teclado chega a ela pela esquerda nas linhas de ação). Fora da pausa:
    // set_embedded(true) (a tela anfitriã desenha o
    // fundo e a barra de cima, sem a aba PAUSE MENU), open_at(...), frame()/build() a cada quadro e
    // take_back_request() para saber quando voltar. O --menu-tab usa o mesmo nome de parse_cat.
    void open_at(Cat cat, bool back_leaves = true);
    bool back_leaves() const { return back_leaves_; }
    // Começa a fechar (a animação de sair); o fim vem em active() falso.
    void request_close();
    // Fecha na hora (o menu do jogo sumiu): desfaz uma confirmação pendente.
    void force_close();
    bool active() const { return active_; }
    bool closing() const { return closing_; }

    // Um quadro: a entrada, as ações e as animações (dt em segundos).
    void frame(float dt, const UiInput& in, const Canvas& cv, const Measure& m);
    // A lista de desenho do quadro.
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    // O véu e o desfoque (0 a 1).
    float background() const;
    // O mouse foi o último a mexer (o cursor do Windows aparece).
    bool mouse_active() const { return mouse_active_; }

    // ---- testes e relatório ----
    void command(Cmd c);  // como uma tecla apertada neste quadro
    Cat category() const { return cat_; }
    Zone zone() const { return zone_; }
    int focus() const { return focus_; }        // índice em items() (-1 nenhum)
    int key_column() const { return col_; }
    int open_dropdown() const { return dd_item_; }
    int dropdown_focus() const { return dd_focus_; }
    bool dialog_open() const { return dialog_.open; }
    int dialog_focus() const { return dialog_.focus; }
    const std::vector<Item>& items() const { return items_; }
    const VideoSettings& pending() const { return pending_; }
    float scroll() const { return scroll_.value(); }
    float open_progress() const { return appear_.value(); }
    const std::wstring& toast() const { return toast_.text; }
    bool toast_visible() const { return toast_.age >= 0.0f; }
    int capturing() const { return capture_cmd_; }
    // O retângulo de um item na tela virtual com a rolagem atual (para os cliques dos testes).
    Rect item_rect(int index) const;
    // O retângulo de uma opção da lista aberta.
    Rect option_rect(int option) const;
    Rect category_rect(int cat) const;
    // Os valores das opções (como a tela os mostra).
    std::wstring value_text(Opt o) const;
    int choice_index(Opt o) const;
    int choice_count(Opt o) const;
    int slider_pos(Opt o) const;
    bool toggle_on(Opt o) const;
    std::string describe() const;

private:
    struct Choice {
        std::vector<std::wstring> labels;
        int index = 0;
    };
    struct SliderSpec {
        int lo = 0, hi = 1, pos = 0;
        std::wstring text;
    };
    struct ItemAnim {
        Tween label;   // 0 branco, 1 ouro
        Tween knob;    // chave
        Tween fill;    // barra
        Tween change;  // troca de valor
        std::wstring old_value;
        std::wstring shown_value;
    };
    enum class DialogKind : uint8_t { None, Keep, Defaults };

    // Conteúdo.
    void build_items();
    void layout_items();
    bool focusable(int i) const;
    int first_focusable() const;
    int step_focus(int from, int dir) const;
    void set_focus(int i, bool animate = true);
    void set_category(Cat c, bool animate = true);
    void ensure_visible(int i);
    float content_height() const;
    float view_height() const;
    float row_screen_y(int i) const;  // topo do item na tela virtual
    Rect underline_of(int i) const;   // o sublinhado do foco (x0, x1, y em y1)
    void place_mark(bool animate);
    // Modelo (as opções).
    Choice choices(Opt o) const;
    void set_choice(Opt o, int index);
    SliderSpec slider(Opt o) const;
    void set_slider(Opt o, int pos);
    void set_toggle(Opt o, bool on);
    bool is_pending_opt(Opt o) const;
    // A descrição da opção (as do jogo seguem o valor atual, como o texto de ajuda das páginas do jogo).
    PcText desc_for(Opt o) const;
    void leave();  // voltar da coluna das categorias (ou das linhas, com back_leaves)
    bool pending_changes() const;
    void apply();
    void restore_defaults();
    std::wstring device_text() const;
    std::wstring key_cell(int command, int col) const;
    // Ações.
    void act(Cmd c);
    void act_content(Cmd c);
    void act_dropdown(Cmd c);
    void act_dialog(Cmd c);
    void activate(int i);
    void open_dropdown_at(int i);
    void close_dropdown();
    void begin_capture(int i, int col);
    void finish_capture();
    void choose_tab(int t);
    void show_toast(PcText id);
    void show_toast(const std::wstring& s);
    // Mouse.
    void mouse(const UiInput& in, const Canvas& cv);
    int hit_item(float x, float y) const;
    int hit_option(float x, float y) const;
    int key_col_at(float x) const;
    void slider_from_x(int i, float x);
    // Desenho.
    void draw_content(DrawList& dl, const Frame& f, float alpha, float dx) const;
    Description description(const Frame& f) const;
    void prompts(const Frame& f, PromptBar& bar) const;
    std::wstring key_name(int command) const;  // a primeira tecla do comando (teclado)
    std::string pad_glyph(int which) const;     // o glifo do controle de um comando da barra

    const Tokens* tk_;
    std::string build_;
    bool embedded_ = false;
    bool back_leaves_ = false;  // open_at: voltar das linhas sai da tela
    int host_tab_ = 0;       // embutida: -1/+1 a aba anterior/seguinte pedida
    bool host_back_ = false; // embutida: voltar da coluna das categorias
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    float close_delay_ = -1.0f;
    Tween appear_;
    Tween content_;  // a troca de categoria
    Cat cat_ = Cat::Video;
    Zone zone_ = Zone::Categories;
    std::vector<Item> items_;
    std::vector<ItemAnim> anims_;
    int focus_ = -1;
    int col_ = 0;
    int dd_item_ = -1;
    int dd_focus_ = 0;
    Tween dd_open_;     // 0 a 1: a lista aberta
    int dd_closing_ = -1;  // a lista que está fechando (animação)
    int dd_closing_count_ = 0;
    Tween scroll_;
    float scroll_target_ = 0.0f;
    FocusMark mark_;
    TabBar tabs_;
    CategoryList cats_;
    PromptBar bar_;
    Dialog dialog_;
    DialogKind dialog_kind_ = DialogKind::None;
    Toast toast_;
    VideoSettings pending_;
    // Captura de tecla.
    int capture_cmd_ = -1;
    int capture_col_ = 0;
    // Entrada.
    bool swallow_ = true;  // ignora tudo até soltar (o que abriu a tela, o fim de uma captura)
    bool held_[10] = {};
    float repeat_[4] = {};
    bool mouse_active_ = false;
    float last_mx_ = -1.0f, last_my_ = -1.0f;
    bool prev_lmb_ = false, prev_back_ = false;
    int drag_slider_ = -1;
    int hover_cat_ = -1;
    float wheel_acc_ = 0.0f;
    int pad_device_ = 0;
    // A última montagem (para o mouse).
    mutable Canvas last_cv_;
    mutable bool laid_out_ = false;
};

}  // namespace nf::ui
