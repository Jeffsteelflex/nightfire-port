#pragma once
// A pausa no estilo Modern (só do port; docs/design.md e docs/port.md, "Menus no estilo Modern"). Cobre a pausa do
// jogo (página 0x4000004b, status 3) quando [ui] menu_style = modern: a pausa continua sendo a do jogo, aberta pelo
// Player_Update e decidida pelos tratadores dele; esta tela lê o estado dela (engine/menu_modern.h,
// MenuModern_Snapshot) e manda as escolhas pelos mesmos eventos dos controles do jogo (MenuModern_Request).
//
// A barra de cima, como "PAUSE MENU · TUTORIALS · OPTIONS" da referência: PAUSE MENU · OBJECTIVES · CONTROLS · SCORE ·
// OPTIONS, com Q/E (LB/RB). Cada aba do jogo é a dele (o foco no botão da
// aba: MISSION 0, OBJECTIVES 1, CONTROLS 3, SCORE 4; o C_GCPAUSE_Handler monta o painel):
// - PAUSE MENU: a lista grande à esquerda como o menu principal da referência (RESUME, RESTART MISSION, QUIT
//   MISSION; OPTIONS menor), cada uma a linha da lista principal do jogo (tag 6), e à direita a missão (nome e
//   descrição de sp_level, o nível dentro dela, a dificuldade, o objetivo atual e o medidor dos objetivos);
// - OBJECTIVES: os objetivos com a situação (cumprido, fracassado, em andamento, confidencial), o texto do jogo e
//   a dica dele na descrição;
// - CONTROLS: o esquema de botões do jogo (a lista tag 9) e o eixo Y (o círculo do jogo inverte), e o mapa de
//   botões que o jogo monta para o esquema, com os símbolos do aparelho (os botões do Xbox ou as teclas ligadas);
// - SCORE: como na pausa do jogo, também com um jogador: a lista tag 8 que o C_GCPAUSE_Handler monta (com um jogador
//   as estatísticas da missão do PlrStat_GetScore, Category / Current / Target; no multijogador o placar), só para ler;
// - OPTIONS: a tela de opções (modern_options.h), embutida.
// RESTART e QUIT pedem a confirmação do jogo (a lista tag 0xd com a pergunta da tag 0xc), desenhada como o Dialog do
// kit e respondida pela lista. Fechar: RESUME (a linha 0), o Start (o do controle ou a tecla da pausa, Enter por
// padrão: o MenuManager_Monitor do jogo fecha e despausa) ou voltar no topo (PAUSE MENU), que manda o mesmo Start.

#include <string>
#include <vector>

#include "engine/menu_modern.h"
#include "ui/modern_input.h"
#include "ui/modern_options.h"

namespace nf::ui {

class PauseScreen {
public:
    enum class Tab : uint8_t { Pause, Objectives, Controls, Score, Options };
    // O nome no --menu-tab (pause, objectives, controls, score; as categorias das opções abrem OPTIONS).
    static bool parse_tab(const std::string& name, Tab* out);
    static const char* tab_name(Tab t);

    explicit PauseScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens);
    void set_build(const std::string& build);
    OptionsScreen& options() { return options_; }
    const OptionsScreen& options() const { return options_; }

    // Abre sobre a pausa do jogo, na aba (OPTIONS: na categoria).
    void open(const MmSnapshot& s, Tab tab = Tab::Pause, Cat cat = Cat::Video);
    // Muda de aba (sem animação de abrir).
    void show_tab(Tab tab, Cat cat = Cat::Video);
    // A pausa do jogo sumiu (fechou, recomeçou, virou o aviso de controle): a animação de sair.
    void close();
    void force_close();
    bool active() const { return active_; }
    bool closing() const { return closing_; }

    void frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m);
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    float background() const;
    bool mouse_active() const;

    // ---- testes e relatório ----
    void command(Nav c);  // como uma tecla apertada no próximo quadro
    Tab tab() const { return tabs_order_.empty() ? Tab::Pause : tabs_order_[size_t(tab_)]; }
    const std::vector<Tab>& tabs() const { return tabs_order_; }
    int focus() const { return focus_; }        // a linha com o foco em OBJECTIVES e CONTROLS
    int menu_focus() const { return menu_.focus; }  // a linha com o foco em PAUSE MENU
    bool dialog_open() const { return dialog_.open; }
    int dialog_focus() const { return dialog_.focus; }
    // O retângulo da linha i da lista da aba atual (tela virtual), para os cliques dos testes.
    Rect row_rect(int i) const;
    Rect tab_rect(int i) const;
    std::string describe() const;

private:
    void set_tab(int index, bool animate);
    void sync_rows(const MmSnapshot& s);
    int row_count() const;
    void act(Nav c, const MmSnapshot& s);
    void act_dialog(Nav c, const MmSnapshot& s);
    void activate(int row, const MmSnapshot& s);
    void request_resume();
    void mouse(const MmSnapshot& s);
    void place_mark(bool animate);
    void ensure_visible(int row);
    float list_y(int row) const;  // o topo da linha na lista (sem a rolagem)
    void draw_pause(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_objectives(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_controls(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_score(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void prompts(PromptBar& bar) const;

    const Tokens* tk_;
    std::string build_;
    OptionsScreen options_;
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    Tween appear_;
    Tween content_;
    std::vector<Tab> tabs_order_;
    int tab_ = 0;
    TabBar bar_tabs_;
    PromptBar bar_;
    BigMenu menu_;            // PAUSE MENU
    int focus_ = 0;           // a linha com o foco (OBJECTIVES, CONTROLS)
    FocusMark mark_;
    Tween scroll_;
    float scroll_target_ = 0.0f;
    Dialog dialog_;
    bool dialog_box_ = false;      // a caixa de mensagem do jogo (senão a confirmação da pausa)
    uint32_t dialog_list_ = 0, dialog_tag_ = 0;
    Toast toast_;
    NavState nav_;
    bool pending_nav_[kNavCount] = {};  // comandos dos testes
    int hover_ = -1;
    int hover_tab_ = -1;
    bool waiting_ = false;    // um pedido ao jogo ainda na fila (os comandos esperam)
    MmSnapshot snap_;         // o último estado do jogo (para o desenho)
    mutable Canvas last_cv_;
};

}  // namespace nf::ui
