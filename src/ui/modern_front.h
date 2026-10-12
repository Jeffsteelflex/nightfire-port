#pragma once
// O menu principal no estilo Modern (só do port; docs/design.md, "As telas", e docs/port.md, "Menus no estilo
// Modern"). Como a pausa (modern_pause.h): o menu continua o do jogo (o pacote 07000048, o arquivo 08000002, status 5),
// aberto e decidido pelos tratadores dele (engine/menu_frontend.h); esta tela lê o estado dele (MenuModern_Snapshot,
// MmFront) e manda as escolhas pelos eventos dos controles do jogo (MenuModern_Request: o foco num botão ou numa
// tecla, um passo de uma roda, um botão do controle do PS2 por um tick).
//
// As vistas, uma por página do caminho de um jogador:
// - Title (idioma, cartão, filmes, "Press START", a procura do perfil, o filme do herói): a arte do jogo (a textura
//   0x03000074, com o logotipo) inteira na altura da janela, nítida, as bordas sumindo no fundo; embaixo o convite do
//   jogo ("Press the START button", depois do texto legal) com a tecla ou o botão, e o texto legal. Escolher é o X no
//   rótulo de tela cheia da página (o script dele mostra 0x4000004a).
// - Main: o logotipo do jogo (0x0300016d) e a lista grande (BigMenu) NIGHTFIRE, MULTIPLAYER, CODENAMES (os botões do
//   jogo: o foco e o X) e, menores, OPTIONS (a tela de opções embutida, só do port), CREDITS (só do port: a página dos
//   créditos do jogo mostrada como o script do "Credits" da AV Options a mostra, MenuModern_Show; no Modern a AV Options
//   é a categoria AUDIO) e QUIT GAME (o diálogo de sair do port; voltar no menu principal também o abre). MULTIPLAYER
//   mostra a caixa do jogo ("not available yet") como o Dialog do kit.
// - As rodas (Campaign: a roda de codinomes da campanha; Codenames; CodenameMenu; Difficulty; Map): à esquerda o
//   título da página do jogo e as etapas da campanha; no meio as linhas, cada uma um item da roda (os textos das
//   tabelas do jogo); à direita a descrição do jogo. Andar é um passo da roda (Step), escolher é o passo e o X; no mapa,
//   as missões bloqueadas ficam com o cadeado (escolher não manda nada), as de carro com a marca "Driving", a melhor
//   medalha do perfil à direita, a imagem da missão (a textura do item) à esquerda e a aba DOSSIER (o quadrado do
//   jogo: as páginas do dossiê, a vista Dossier). No menu do codinome, Controller Setup, Advanced Options e AV Options abrem a
//   tela de opções embutida nas categorias CONTROLLER, GAMEPLAY e AUDIO no lugar das páginas do jogo (o que mudar
//   marca o codinome como mudado, MenuModern_CodenameOptionsEnd); Driving Controller e Multiplayer Options ficam
//   travadas; Secret Unlocks e Save Codename são as do jogo.
// - Name (o teclado da tela): o nome do jogo (codename_entry) em oito casas, digitado no teclado do PC (letras,
//   algarismos, espaço; Backspace apaga, Enter termina) ou pela grade de teclas Modern (controle e mouse); cada tecla é a
//   do jogo (o foco nela e o X: C_KEYBOARD_Handler), então o limite de 8, as letras, a validação
//   (Menu_IsValidCodename) e os códigos (Menu_SpecialCodenameCheck) são os do jogo.
// - Options: uma página de opções do codinome que o jogo mostrou sozinho (Controller Setup depois de criar um
//   codinome): a tela de opções embutida na categoria; sair dela é o voltar do jogo.
// - Dossier: as páginas do dossiê (a da roda e as quatro seções), pela DossierView (modern_dossier.h).
// - Credits: a página dos créditos: as linhas que o P_CREDITS_Handler pôs nos rótulos, na posição e com a fonte do
//   jogo, no tipo do kit (draw_credits); o ritmo e o fim são os do jogo, voltar é o voltar do jogo.
// As caixas do jogo (a de mensagem 0x4000002f e a do cartão 0x4000002a) viram o Dialog do kit, respondido pela lista
// delas; uma do cartão sem respostas ("Saving data...") é o Dialog sem botões, com a faixa de espera.
//
// O desenho tem camadas, porque a arte e as imagens do jogo são texturas do Direct3D (render/ui_modern_app.h): o fundo
// (build_back), a arte (backdrop), o desfoque (blur: 0 no título, 1 nos menus, animado), o véu sobre a arte
// (build_veil), as imagens (images: o logotipo, a imagem da missão, as do dossiê) e o resto (build).

#include <string>
#include <vector>

#include "engine/menu_modern.h"
#include "ui/modern_dossier.h"
#include "ui/modern_input.h"
#include "ui/modern_options.h"

namespace nf::ui {

class FrontScreen {
public:
    enum class View : uint8_t { Title, Main, Campaign, Codenames, Name, CodenameMenu, Difficulty, Map, Options, Dossier, Credits };
    static const char* view_name(View v);

    explicit FrontScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens);
    void set_build(const std::string& build);
    // A arte do jogo existe (a textura do título no pacote; sem ela, a marca Modern no lugar).
    void set_art(bool on) { art_ = on; }
    // O filme de fundo da página do jogo toca (07330048 no menu principal, 07350048 nas outras): ele fica no lugar da
    // arte, no mesmo retângulo 4:3 e atrás do mesmo desfoque (kMovieImage).
    void set_movie(bool on) { movie_ = on; }
    bool movie() const { return movie_; }
    OptionsScreen& options() { return options_; }
    const OptionsScreen& options() const { return options_; }
    DossierView& dossier() { return dossier_; }
    const DossierView& dossier() const { return dossier_; }

    void open(const MmSnapshot& s);
    void close();
    void force_close();
    bool active() const { return active_; }
    bool closing() const { return closing_; }
    void frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m);

    // As camadas do desenho, nesta ordem.
    void build_back(DrawList& dl, const Canvas& cv, const Measure& m) const;
    void backdrop(ImageList& out, const Canvas& cv) const;
    float blur() const;
    void build_veil(DrawList& dl, const Canvas& cv, const Measure& m) const;
    void images(ImageList& out, const Canvas& cv) const;
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;

    bool mouse_active() const;
    // QUIT GAME confirmado (uma vez).
    bool take_quit() {
        const bool q = quit_;
        quit_ = false;
        return q;
    }

    // ---- testes e relatório ----
    void command(Nav c) { pending_[int(c)] = true; }
    // Uma tecla do teclado do PC na vista Name (como digitada neste quadro): letra, algarismo, ' ', '\b' apaga, '\n'
    // termina.
    void type(char c) { typed_.push_back(c); }
    View view() const { return view_; }
    int focus() const;
    bool dialog_open() const { return dialog_.open; }
    int dialog_focus() const { return dialog_.focus; }
    bool options_open() const { return opts_ != OptsFrom::None; }
    const std::wstring& toast() const { return toast_.text; }
    Rect row_rect(int i) const;     // a linha i da vista (BigMenu no menu principal)
    Rect key_rect(int i) const;     // a tecla i da grade (na ordem da grade)
    int key_count() const { return int(key_rects_.size()); }
    uint32_t key_tag(int i) const;  // a tag do jogo da tecla i da grade
    Rect tab_rect(int i) const;
    std::string describe() const;

private:
    enum class OptsFrom : uint8_t { None, Main, Codename, Game };
    enum class DialogKind : uint8_t { None, Game, Quit };

    static View view_of(const MmSnapshot& s);
    void change_view(View v, const MmSnapshot& s);
    void sync_from_game(const MmSnapshot& s);
    int row_count() const;
    bool row_enabled(int i) const;
    float row_y(int i) const;  // o topo da linha i na lista (sem a rolagem)
    void ensure_visible(int i);
    void place_mark(bool animate);
    void move_focus(int to);
    void activate(int i);
    void act(Nav c);
    void act_dialog(Nav c);
    void act_name(Nav c);
    void press_key(int grid_index);
    void type_char(char c);
    void open_options(Cat cat, OptsFrom from);
    void close_options();
    void quit_dialog();
    void show_game_box(const MmSnapshot& s);
    void mouse();
    void layout_keys();
    void layout_tabs(const Frame& f, const UiInput& in);
    void prompts(PromptBar& bar) const;
    // Desenho.
    void draw_title(DrawList& dl, const Frame& f, float alpha) const;
    void draw_main(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_left(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_name(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_credits(DrawList& dl, const Frame& f, float alpha) const;
    Description description() const;
    Rect art_rect(const Canvas& cv) const;
    Rect picture_rect() const;

    const Tokens* tk_;
    std::string build_;
    OptionsScreen options_;
    DossierView dossier_;
    bool art_ = true;
    bool movie_ = false;
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    Tween appear_;   // 0 a 1: aparecer (do preto) e sair
    Tween blur_;     // 0 o título nítido, 1 os menus sobre a arte desfocada
    Tween content_;  // a troca de vista
    View view_ = View::Title;
    MmSnapshot snap_;
    // O menu principal.
    BigMenu menu_;
    // As rodas.
    int focus_ = 0;
    int hover_ = -1;
    FocusMark mark_;
    Tween scroll_;
    float scroll_target_ = 0.0f;
    std::vector<Tween> lit_;
    // A imagem da missão: a de agora e a anterior sumindo.
    uint32_t pic_ = 0, pic_prev_ = 0;
    bool pic_on_ = true, pic_prev_on_ = true;
    Tween pic_t_;
    // O teclado da tela: as teclas na ordem da grade (índices em snap_.front.keys) e os retângulos.
    std::vector<int> key_order_;
    std::vector<Rect> key_rects_;
    int key_focus_ = 0;
    int key_hover_ = -1;
    FocusMark key_mark_;
    bool prev_keys_[256] = {};
    uint16_t prev_pad_ = 0xffff;
    // A barra de cima (o mapa: MISSIONS · DOSSIER; as opções: o menu de onde vieram · OPTIONS).
    TabBar tabs_;
    bool tabs_on_ = false;
    // As opções embutidas.
    OptsFrom opts_ = OptsFrom::None;
    uint32_t skip_option_page_ = 0;  // a página de opções do jogo que já foi fechada (espera o jogo voltar)
    // Diálogo, aviso, barra de baixo, entrada.
    Dialog dialog_;
    DialogKind dialog_kind_ = DialogKind::None;
    uint32_t dialog_list_ = 0, dialog_tag_ = 0;
    std::string dialog_sig_;  // o texto e as respostas da caixa do jogo mostrada (a do cartão troca de mensagem)
    Toast toast_;
    PromptBar bar_;
    NavState nav_;
    bool pending_[kNavCount] = {};
    std::string typed_;
    bool waiting_ = false;
    bool quit_ = false;
    mutable Canvas last_cv_;
};

}  // namespace nf::ui
