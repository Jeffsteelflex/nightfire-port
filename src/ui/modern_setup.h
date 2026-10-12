#pragma once
// A tela de configuração da primeira partida (só do port; docs/port.md, "Instalação fácil", e docs/design.md, "A tela
// de configuração"): aparece quando o nightfire.exe não acha um disco que sirva (platform/disc_source.h: nem a cópia
// pessoal, nem os arquivos ou uma imagem ao lado do executável, nem o [game] disc do nightfire.ini, nem um leitor de
// DVD com o disco), ou com --setup. No estilo Modern (o kit de modern_kit.h), sem nada do jogo: antes do disco não há
// texturas nem textos do jogo, só os do port.
//
// À esquerda o título e a lista: SELECT DISC IMAGE (a escolha de arquivo do Windows), USE THE DISC IN DRIVE X: (um por
// leitor com um disco de PS2; travado com o motivo se for outro jogo ou outra versão), PLAY (travado até haver um disco
// que sirva), MAKE A SINGLE-FILE COPY (a cópia pessoal, platform/disc_pack.h; travado sem disco) e QUIT. À direita, a
// descrição da linha com o foco, o disco escolhido e o estado (pronto, ou por que não serve). As caixas do kit: o
// problema de um disco, o andamento da cópia (a barra e Cancelar) e o fim dela (com o aviso de que o arquivo contém o
// jogo e não pode ser compartilhado).
//
// Sem GPU e sem disco: a tela recebe o que mostrar (SetupView, montada pelo anfitrião: render/setup_app.h) e a entrada de
// cada quadro (UiInput), e devolve os pedidos (take_request: escolher a imagem, um leitor, jogar, copiar, cancelar a
// cópia, sair); o anfitrião faz o resto (a janela de escolha, a conferência, o nightfire.ini, a cópia numa thread).
// Teclado, mouse e controle como nas outras telas Modern (NavState: Enter e Espaço escolhem, Esc volta, A/B no controle).

#include <cstdint>
#include <string>
#include <vector>

#include "platform/disc_source.h"
#include "ui/modern_input.h"

namespace nf::ui {

// O que a tela mostra (o anfitrião monta; já no idioma dos textos do port).
struct SetupView {
    struct Drive {
        std::wstring root;     // "D:\"
        bool usable = false;   // o disco certo
        std::wstring problem;  // senão, por quê
    };
    bool ready = false;          // há um disco que serve
    std::wstring disc;           // o caminho dele (ou do último recusado)
    std::wstring disc_kind;      // "Disc image", "DVD drive"...
    std::wstring status;         // pronto (o código do disco), ou o problema
    bool problem = false;        // status é um problema
    std::vector<Drive> drives;   // os leitores com um disco de PS2
    bool can_pack = false;       // a cópia pessoal dá para fazer (um disco que serve, com SYSTEM.CNF)
    std::wstring pack_size;      // "1.9 GB"
    std::wstring pack_path;      // onde ela vai
    bool picking = false;        // a janela de escolha do Windows está aberta
};

enum class SetupAction : uint8_t { None, Browse, Drive, Play, Pack, CancelPack, Quit };
struct SetupRequest {
    SetupAction action = SetupAction::None;
    int drive = -1;  // Drive: o índice em SetupView::drives
};

class SetupScreen {
public:
    explicit SetupScreen(const Tokens* tokens = nullptr);
    void set_tokens(const Tokens* tokens) { tk_ = tokens != nullptr ? tokens : &default_tokens(); }
    void set_build(const std::string& build) { build_ = build; }

    void open(const SetupView& v);
    // O que mostrar agora (a cada mudança; as linhas acompanham os leitores).
    void set_view(const SetupView& v);
    const SetupView& view() const { return view_; }
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
    // Leva o foco para a linha PLAY (um disco acabou de servir).
    void focus_play();

    // ---- testes e relatório ----
    enum class Row : uint8_t { Browse, Drive, Play, Pack, Quit };
    void command(Nav c) { pending_[int(c)] = true; }
    int focus() const { return menu_.focus; }
    int row_count() const { return int(rows_.size()); }
    Row row_kind(int i) const { return rows_[size_t(i)].kind; }
    bool row_locked(int i) const;
    Rect row_rect(int i) const { return i >= 0 && size_t(i) < menu_.rects.size() ? menu_.rects[size_t(i)] : Rect{}; }
    const Dialog& dialog() const { return dialog_; }
    std::string describe() const;

private:
    struct RowInfo {
        Row kind;
        int drive = -1;
    };
    void set_rows();
    void activate(int i);
    void act(Nav c);
    Description description() const;

    const Tokens* tk_;
    std::string build_;
    SetupView view_;
    float time_ = 0.0f;
    Tween appear_;
    BigMenu menu_;
    std::vector<RowInfo> rows_;
    PromptBar bar_;
    Dialog dialog_;
    bool dialog_progress_ = false;  // a caixa é a do andamento (o botão cancela a cópia)
    Toast toast_;
    NavState nav_;
    bool pending_[kNavCount] = {};
    SetupRequest request_;
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

}  // namespace nf::ui
