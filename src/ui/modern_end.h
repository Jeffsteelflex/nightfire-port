#pragma once
// O fim de missão e o resultado no estilo Modern (só do port; docs/design.md e docs/port.md, "Menus no estilo
// Modern"). Como a pausa (modern_pause.h): os menus continuam os do jogo (P_ENDMISSION_Handler, P_NFRESULTS_Handler,
// P_NFSTATS_Handler, P_NFBONUS_Handler) e estas telas leem o estado deles (MenuModern_Snapshot) e mandam as escolhas
// pelos eventos dos controles do jogo (MenuModern_Request).
//
// - FailScreen (o menu 0x80000004, página 0x40000042, status 4, que o Mission_Update abre): "MISSION FAILED" em tela
//   cheia sobre a cena desfocada, com a cortina de sangue do HUD por trás (só o 2D do menu some), o motivo do jogo
//   (o rótulo de falha da missão, "Killed In Action" na morte, ou o objetivo fracassado) e as linhas da lista do
//   jogo (REPLAY, QUIT), escolhidas pela lista 0x100001a4. O movimento é lento e contido: o véu aparece em 0,9 s, o
//   título surge com o espaço entre as letras abrindo devagar (1,8 s), as linhas finas crescem dos lados dele e as
//   opções entram depois.
// - ResultsScreen (o menu principal 07000048, páginas 0x40000036 resultado, 0x40000037 estatísticas, 0x40000038
//   bônus, status 5): "MISSION COMPLETE", a missão, a pontuação, a meta da medalha de ouro e a medalha que o jogo
//   escreveu (PlrStat_GetScore), as estatísticas (a lista do jogo, ou as categorias dele com "—" antes de o jogo
//   montá-la) e CONTINUE / STATS / DOSSIER como a página oferece: escolher (X) continua, o círculo
//   abre as estatísticas, voltar volta delas; o DOSSIER é o quadrado (a página do dossiê: o dossiê Modern, uma vista
//   do menu principal, modern_dossier.h; voltar dele reabre esta tela).
// Uma caixa de mensagem do jogo por cima (Menu_CreateOptionBox: "Do you want to save your progress?") vira o
// Dialog do kit, respondido pela lista dela.

#include <string>
#include <vector>

#include "engine/menu_modern.h"
#include "ui/modern_input.h"

namespace nf::ui {

class FailScreen {
public:
    explicit FailScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }
    void open(const MmSnapshot& s);
    void close();
    void force_close() { active_ = closing_ = false; }
    bool active() const { return active_; }
    bool closing() const { return closing_; }
    void frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m);
    // O fundo (o véu) e, separado, o resto: a cortina de sangue do jogo é desenhada antes dos dois.
    void build_back(DrawList& dl, const Canvas& cv, const Measure& m) const;
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    float background() const;
    bool mouse_active() const { return nav_.mouse_active; }
    // A missão fracassou pela morte: o HUD do jogo (a cortina de sangue) fica por trás da tela.
    bool died() const { return snap_.died; }
    // ---- testes e relatório ----
    void command(Nav c) { pending_[int(c)] = true; }
    int focus() const { return focus_; }
    int option_count() const { return int(snap_.fail_rows.size()); }
    Rect option_rect(int i) const { return i >= 0 && size_t(i) < rects_.size() ? rects_[size_t(i)] : Rect{}; }
    float time() const { return time_; }
    std::string describe() const;

private:
    void layout(const Frame& f);
    void set_focus(int i, bool animate);
    void select(int i);

    const Tokens* tk_;
    std::string build_;
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    Tween out_;  // fechando: 1 -> 0
    int focus_ = 0;
    int hover_ = -1;
    std::vector<Rect> rects_;
    std::vector<Tween> lit_;
    FocusMark mark_;
    Dialog dialog_;
    uint32_t dialog_list_ = 0, dialog_tag_ = 0;
    PromptBar bar_;
    NavState nav_;
    bool pending_[kNavCount] = {};
    bool waiting_ = false;
    MmSnapshot snap_;
    mutable Canvas last_cv_;
};

class ResultsScreen {
public:
    enum class View : uint8_t { Results, Stats, Bonus };
    explicit ResultsScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }
    void open(const MmSnapshot& s);
    void close();
    void force_close() { active_ = closing_ = false; }
    bool active() const { return active_; }
    bool closing() const { return closing_; }
    void frame(float dt, const UiInput& in, const MmSnapshot& s, const Canvas& cv, const Measure& m);
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    float background() const { return 0.0f; }  // sem cena: o fundo é dele (opaco)
    bool mouse_active() const { return nav_.mouse_active; }
    // ---- testes e relatório ----
    void command(Nav c) { pending_[int(c)] = true; }
    View view() const { return view_; }
    int focus() const { return menu_.focus; }
    bool dialog_open() const { return dialog_.open; }
    const std::wstring& toast() const { return toast_.text; }
    Rect item_rect(int i) const { return i >= 0 && size_t(i) < menu_.rects.size() ? menu_.rects[size_t(i)] : Rect{}; }
    std::string describe() const;

private:
    static View view_of(uint32_t page);
    void set_items();
    void activate(int i);
    void act(Nav c);
    void draw_results(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_stats(DrawList& dl, const Frame& f, float alpha, float dx) const;
    void draw_bonus(DrawList& dl, const Frame& f, float alpha, float dx) const;

    const Tokens* tk_;
    std::string build_;
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    Tween appear_;
    Tween content_;
    View view_ = View::Results;
    TabBar tabs_;
    BigMenu menu_;
    PromptBar bar_;
    Dialog dialog_;
    uint32_t dialog_list_ = 0, dialog_tag_ = 0;
    Toast toast_;
    NavState nav_;
    bool pending_[kNavCount] = {};
    bool waiting_ = false;
    MmSnapshot snap_;
    mutable Canvas last_cv_;
};

// O aviso de controle desconectado (status 2: a caixa 0x4000002f que o MenuManager_Monitor mostra no jogo quando o
// controle em uso some): o Dialog do kit sem botões sobre a cena desfocada, com o título do port e o texto do jogo
// ("...reconnect the controller... and press START button to continue."). Só desenho: o jogo espera o controle voltar
// e o Start sozinho (a tela não prende a entrada; psiInput_SetModal fica desligado).
class WarningScreen {
public:
    explicit WarningScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void open(const MmSnapshot& s);
    void close();
    void force_close() { active_ = closing_ = false; }
    bool active() const { return active_; }
    bool closing() const { return closing_; }
    void frame(float dt, const MmSnapshot& s, const Canvas& cv, const Measure& m);
    void build(DrawList& dl, const Canvas& cv, const Measure& m) const;
    float background() const { return active_ ? appear_.value() : 0.0f; }
    const std::wstring& body() const { return dialog_.body; }
    std::string describe() const;

private:
    const Tokens* tk_;
    bool active_ = false;
    bool closing_ = false;
    float time_ = 0.0f;
    Tween appear_;
    Dialog dialog_;
    mutable Canvas last_cv_;
};

}  // namespace nf::ui
