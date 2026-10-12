#pragma once
// A ligação do estilo de menu Modern com o nightfire.exe (só do port): os tokens (hd_ui/design/tokens.json), as
// telas (ui/modern_pause.h com as opções embutidas, ui/modern_end.h), a entrada de cada quadro de vídeo (teclado,
// mouse e controle do psi_input), o bloqueio da entrada do jogo enquanto uma tela está aberta (psiInput_SetModal) e
// o desenho (o desfoque da cena e a lista de desenho pelo renderizador). Ver docs/port.md, "Menus no estilo Modern".
//
// As telas seguem os menus do jogo (engine/menu_modern.h): a cada quadro o app lê MenuModern_Snapshot e abre a
// tela que o menu aberto pede (a pausa, o fim de missão, o resultado, o menu principal, o aviso de controle
// desconectado) quando o estilo Modern vale, ou a fecha quando o menu do jogo some. O 2D do jogo por baixo some
// (hidden_2d: o do menu, câmera 7, e na pausa, no resultado, no menu principal e no aviso também o HUD; o fim de missão
// deixa a cortina de sangue do HUD, desenhada nítida sobre a cena desfocada).
//
// O menu principal (ui/modern_front.h) desenha imagens do jogo com o Direct3D entre as listas do Direct2D: o fundo, a
// arte do título (desfocada nos menus pelo Renderer::blur_background), o véu, o logotipo, a imagem da missão e as
// imagens do dossiê (psi_2d_texture_slot dá o índice de Tex e o tamanho de cada textura), e o resto. QUIT GAME pede para o main sair (take_quit).

#include <cstdint>
#include <string>
#include <vector>

#include "ui/modern_end.h"
#include "ui/modern_front.h"
#include "ui/modern_pause.h"
#include "ui/modern_wheel.h"

namespace nf {

class FilesBin;
class Renderer;
struct DrawItem;
struct FrameParams;

struct ModernUiInput {
    const bool* keys = nullptr;  // 256 (nulo: o teclado não vale neste quadro)
    bool mouse_inside = false;
    float mouse_x = 0.0f, mouse_y = 0.0f;  // pixels da janela
    float wheel = 0.0f;
};

class ModernUiApp {
public:
    // A pasta hd_ui (fontes, glifos e o design/tokens.json) e a versão do port para a barra de baixo.
    void init(Renderer& renderer, const std::wstring& hd_ui_dir, const std::string& build);
    // O Direct2D funciona (sem ele, o estilo Original).
    bool available() const { return available_; }
    // As opções: a aba OPTIONS da pausa Modern (a pausa do jogo precisa estar aberta); false sem Direct2D.
    bool open(ui::Cat cat);
    bool open_last();
    // A pausa Modern numa aba (--open-menu-page 4000004b --menu-tab objectives).
    bool open_pause(ui::PauseScreen::Tab tab);
    bool active() const;
    // Um quadro de vídeo: dt em segundos; enabled: o estilo Modern em vigor (só abre telas com ele; uma tela aberta
    // fica até o menu do jogo fechar).
    void frame(float dt, const ModernUiInput& in, bool enabled);
    // As câmeras cujo 2D some por baixo da tela (bit i = glb_viewer[i]; ~0: todo o 2D). 0 sem tela.
    uint32_t hidden_2d() const;
    // Depois de renderer.render() com a cena: o desfoque, o 2D que fica (overlay, nítido) e a tela.
    void draw(const std::vector<DrawItem>& overlay, const FrameParams& frame);
    bool mouse_active() const;
    // QUIT GAME confirmado no menu principal: o main sai do laço.
    bool take_quit() { return front_.take_quit(); }
    // Os filmes (platform/psi_movie.h), antes de frame(): um quadro na tela (shown) e se é o fundo em laço de uma página
    // (looping) ou um filme de tela cheia (as aberturas, a demonstração, o clipe, o trailer, o fim do jogo). Com um de tela
    // cheia o menu principal Modern sai da frente (não desenha nem prende a entrada: Start pula como no jogo); o fundo
    // de uma página é desenhado no lugar da arte, atrás do desfoque.
    void set_movie(bool shown, bool looping) {
        movie_ = shown;
        movie_loop_ = looping;
    }
    std::string describe() const;
    const ui::PauseScreen& pause() const { return pause_; }
    // Os tokens em vigor (a roda de armas, ui/modern_wheel.h, desenha com eles).
    const ui::Tokens& tokens() const { return tokens_; }
    // Os ícones da roda de armas (engine/weapon_wheel.h, weapon_wheel_icon): as imagens do dossiê do pacote do menu
    // (07000048, que numa fase não está carregado) decodificadas do FILES.BIN (data/pack_images.h), recortadas do disco
    // do radar (ui::wheel_icon_image) e guardadas no renderizador (Renderer::set_ui_image). Uma vez, depois das tabelas
    // do ELF (install_game_tables: ds_weapons e ds_gadgets). Devolve quantas.
    int load_wheel_icons(const FilesBin& files);
    int wheel_icons() const { return wheel_icons_; }
    // A roda de armas por cima do quadro (o fundo e a frente pelo Direct2D, os ícones pelo Direct3D entre eles). Um
    // ícone que não carregou deixa o espaço só com o texto.
    void draw_wheel(ui::WheelView view, const FrameParams& frame);
    const ui::FrontScreen& front() const { return front_; }

private:
    // Imagens do jogo (texturas 03xxxxxx) com o Direct3D, na tela virtual.
    void draw_images(const ui::ImageList& list, const ui::Canvas& cv, const FrameParams& frame);

    Renderer* renderer_ = nullptr;
    ui::Tokens tokens_;
    ui::PauseScreen pause_;
    ui::FailScreen fail_;
    ui::ResultsScreen results_;
    ui::FrontScreen front_;
    ui::WarningScreen warning_;
    ui::Cat last_ = ui::Cat::Video;
    bool available_ = false;
    bool was_active_ = false;
    bool want_options_ = false;     // as opções pedidas antes da pausa Modern abrir
    ui::PauseScreen::Tab want_tab_ = ui::PauseScreen::Tab::Pause;
    bool want_pause_ = false;
    std::string tokens_status_;
    int wheel_icons_ = 0;      // load_wheel_icons
    bool movie_ = false;       // set_movie
    bool movie_loop_ = false;
    bool movie_full() const { return movie_ && !movie_loop_; }
};

}  // namespace nf
