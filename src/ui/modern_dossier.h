#pragma once
// O dossiê no estilo Modern (só do port; docs/design.md, "As telas", e docs/port.md, "Menus no estilo Modern"): uma
// vista do menu principal Modern (FrontScreen, modern_front.h) sobre as páginas do dossiê do jogo (engine/menu_dossier.h:
// a página da roda 0x4000002b, os recordes 0x4000003a, as recompensas 0x4000003b, os aparelhos 0x4000003c e as armas
// 0x4000003d), abertas pelo jogo (o quadrado no mapa das missões, no resultado e no bônus) e decididas pelos tratadores
// dele. A tela lê o estado delas (MenuModern_Snapshot, MmDossier) e manda as escolhas pelos eventos dos controles do jogo.
//
// Uma tela só, com a barra de cima RECORDS · REWARDS · GADGETS · WEAPONS (os nomes de ds_options). Cada aba é a página
// do jogo, alcançada pela navegação dele: trocar de aba é o voltar do jogo (a seção volta à página da roda), o passo da
// roda do dossiê até a seção e o X (C_SBDOSSIER_Handler: os recordes e as recompensas na hora, os aparelhos e as armas
// depois do íris). A página da roda não tem vista própria: ao chegar nela (vinda do mapa ou do resultado, a roda no
// começo) a tela entra na aba RECORDS, ou na aba pedida. Sair (voltar) é o voltar do jogo duas vezes (a seção, a página
// da roda): o jogo volta aonde voltaria, o mapa ou o resultado. A tela segue a página do jogo em cada quadro (um pedido
// por vez, esperando a página mudar), então o íris e as transições do jogo são os do jogo.
//
// - RECORDS e REWARDS: à esquerda o título da página do jogo, a descrição da seção (o texto de ds_options) e a imagem
//   redonda da missão (a de sp_level, como no mapa); no meio as 12 missões (o rádio do jogo: andar é o passo do rádio,
//   como o direcional; o 0x49 do rádio refaz a página), com a melhor medalha ou as recompensas ganhas; à direita os
//   recordes (o recorde, os momentos Bond, as melhorias e a melhor medalha com a imagem do jogo) ou as quatro
//   recompensas (a imagem e o texto do jogo, ou o cadeado e a medalha que falta).
// - GADGETS e WEAPONS: à esquerda a imagem do item (a da roda do jogo); no meio os itens (a roda: andar é o passo dela),
//   com o nível de melhoria; à direita o nome e o texto do jogo (o memo, com a linha da melhoria em ouro).
// As imagens são texturas do jogo (ModernUiApp::draw_images; as substitutas HD valem).

#include <string>
#include <vector>

#include "engine/menu_modern.h"
#include "ui/modern_input.h"

namespace nf::ui {

class DossierView {
public:
    static constexpr int kTabs = 4;
    static constexpr int kLeave = 4;  // target(): sair do dossiê

    explicit DossierView(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }

    // A vista entra (o jogo mostrou o dossiê): a aba é a seção do jogo, ou RECORDS na página da roda.
    void enter(const MmSnapshot& s, const Frame& f);
    // Um quadro: os comandos já lidos (nav), o estado do jogo. Manda no máximo um pedido de navegação de página por vez.
    void frame(float dt, NavState& nav, const UiInput& in, const MmSnapshot& s, const Frame& f);
    // O conteúdo (a barra de cima, as três colunas) com o alfa e o deslize da tela.
    void build(DrawList& dl, const Frame& f, float alpha, float dx) const;
    // As imagens do jogo (a da missão ou do item, a da medalha, as das recompensas), antes de build (o anel por cima),
    // com o mesmo alfa e deslize.
    void images(ImageList& out, float alpha, float dx) const;
    void prompts(PromptBar& bar, bool pad) const;

    // ---- testes e relatório ----
    int tab() const { return tab_; }        // a aba mostrada (0 a 3)
    int target() const { return target_; }  // -1 nenhuma, 0 a 3 a aba pedida, kLeave sair
    int section() const { return snap_.front.dossier.section; }
    int focus() const { return focus_; }
    int row_count() const { return int(snap_.front.items.size()); }
    Rect row_rect(int i) const;
    Rect tab_rect(int i) const;
    void go_tab(int t);  // como Q/E ou o clique numa aba
    void leave();        // como voltar
    std::string describe() const;

private:
    struct Offsets {
        float left, mid, right;  // o deslize das três colunas
    };
    Offsets offsets(float dx) const;
    bool live() const;  // a página do jogo é a da aba mostrada e nada está a caminho
    void drive(const MmSnapshot& s);
    void sync(const MmSnapshot& s, bool snap_focus);
    void move_focus(int to);
    void ensure_visible(int row);
    void place_mark(bool animate);
    void act(Nav c);
    void mouse(NavState& nav);
    float row_y(int i) const { return float(i) * tk_->space.row_h; }
    Rect picture_rect() const;
    uint32_t picture() const;
    bool mission_tab() const { return tab_ == 0 || tab_ == 1; }
    void draw_left(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_rows(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_records(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_rewards(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_item(DrawList& dl, const Frame& f, float alpha, float dx) const;
    Rect medal_rect(float dx) const;
    Rect reward_rect(int i, float dx) const;

    const Tokens* tk_;
    std::string build_;
    MmSnapshot snap_;
    float time_ = 0.0f;
    int tab_ = 0;
    int target_ = -1;
    // O pedido de página mandado (o voltar, ou o X na roda): espera a página do jogo mudar (ou 1,5 s).
    bool sent_ = false;
    uint32_t sent_page_ = 0;
    float sent_time_ = 0.0f;
    Tween content_;  // 0 a caminho de outra aba, 1 na aba
    TabBar tabs_;
    int focus_ = 0;
    int hover_ = -1;
    FocusMark mark_;
    Tween scroll_;
    float scroll_target_ = 0.0f;
    std::vector<Tween> lit_;
    // A imagem da esquerda: a de agora e a anterior sumindo.
    uint32_t pic_ = 0, pic_prev_ = 0;
    Tween pic_t_;
    mutable Canvas last_cv_;
};

}  // namespace nf::ui
