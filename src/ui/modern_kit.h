#pragma once
// Os componentes do estilo de menu Modern (ver modern_ui.h e docs/design.md): peças sem GPU que montam a lista
// de desenho de uma tela. Cada uma recebe o quadro (Frame: tokens, tela virtual, medida de texto, relógio) e
// desenha em coordenadas da tela virtual; as que respondem ao mouse guardam os retângulos da última montagem
// (hit). Pensadas para todas as telas Modern: as opções do PC agora; a pausa, o fim de missão e o resultado
// depois.
//
// - fundo (draw_background): o véu e a vinheta sobre a cena desfocada (o desfoque é do renderizador);
// - barra de cima (TabBar): abas centradas em versalete espaçado, a ativa em ouro com o sublinhado que
//   desliza, as teclas Q/E (LB/RB no controle) dos lados e a linha fina na largura da janela;
// - lista de categorias (CategoryList): o cabeçalho "OPTIONS /", as categorias em maiúsculas, a escolhida em
//   ouro com o quadradinho, as travadas com o cadeado;
// - linha (draw_row_*): o nome à esquerda, o valor à direita, a linha fina embaixo; o foco (FocusMark) é o
//   nome em ouro, o sublinhado de ouro e o brilho de brasa que sobe do meio dele, animados entre as linhas;
// - controles: lista que abre (valor + chevron; as opções recuadas com o ponto de rádio), chave (switch +
//   On/Off), barra (trilho, preenchimento, losango e o número), informação, travada (cadeado + "em breve");
// - cabeçalho de seção (draw_section), indicador de rolagem (draw_scroll);
// - painel de descrição (draw_description): a faixa de dados decorativa, o título grande, o texto, uma linha,
//   "Current value: X" e, embaixo, um medidor opcional;
// - barra de baixo (PromptBar): a informação do port à esquerda e os comandos à direita (tecla ou botão do
//   controle + o nome), clicáveis;
// - tecla (draw_keycap) e botão do controle (glifo SVG);
// - caixa de diálogo (Dialog) e aviso (Toast).

#include <string>
#include <vector>

#include "ui/modern_ui.h"

namespace nf::ui {

struct Frame {
    const Tokens& tk;
    const Canvas& cv;
    const Measure& measure;
    float time = 0.0f;  // segundos (o brilho tremula com ele)
};

// O texto de um estilo na largura medida (o espaço da última letra não conta; caps já convertido).
float text_width(const Frame& f, const std::wstring& s, const TextStyle& st);
// Quebra um texto em linhas de até `width` (pelas palavras; uma palavra maior que a linha fica sozinha nela).
std::vector<std::wstring> wrap_lines(const Frame& f, const std::wstring& s, const TextStyle& st, float width);
// O tamanho de um estilo que faz o texto caber em `width` (no mínimo min_size).
TextStyle fit_style(const Frame& f, const std::wstring& s, TextStyle st, float width, float min_size);

// ---------------------------------------------------------------- fundo

// O véu e a vinheta na janela inteira; amount de 0 a 1 (o aparecer da tela).
void draw_background(DrawList& dl, const Frame& f, float amount);

// ---------------------------------------------------------------- barra de cima

struct TabBar {
    std::vector<std::wstring> labels;
    std::vector<bool> locked;  // abas travadas (cadeado, apagadas): o resultado mostra o DOSSIER assim
    int active = 0;
    // As teclas dos lados: o texto da tecla (teclado) ou o nome do glifo do controle ("xbox_lb").
    std::wstring key_prev = L"Q", key_next = L"E";
    std::string glyph_prev, glyph_next;
    // Os textinhos técnicos dos cantos de cima (decorativos).
    std::wstring corner_left, corner_right;
    Tween ux0, ux1;  // o sublinhado
    bool laid_out = false;
    // A última montagem.
    std::vector<Rect> tab_rects;
    Rect prev_rect, next_rect;

    void layout(const Frame& f);
    // Muda a aba ativa (o sublinhado desliza; animate = false pula direto).
    void set_active(int i, const Frame& f, bool animate);
    void update(float dt);
    void draw(DrawList& dl, const Frame& f, float alpha) const;
    // -1 nada; 0..n-1 uma aba; -2 a tecla de antes; -3 a de depois.
    int hit(float x, float y) const;
};

// ---------------------------------------------------------------- categorias

struct CategoryList {
    struct Item {
        std::wstring label;
        bool locked = false;
    };
    std::wstring header;  // "OPTIONS /"
    std::vector<Item> items;
    int selected = 0;
    int hover = -1;
    bool focused = false;  // a coluna tem o foco do teclado/controle
    std::vector<Rect> rects;

    void layout(const Frame& f);
    void draw(DrawList& dl, const Frame& f, float alpha, float dx) const;
    int hit(float x, float y) const;
};

// ---------------------------------------------------------------- foco

// O sublinhado de ouro com o brilho de brasa, animado de um lugar para outro.
struct FocusMark {
    Tween x0, x1, y, a;
    bool placed = false;
    // Vai para o sublinhado de x0 a x1 na altura y (animate = false: pula).
    void move_to(float nx0, float nx1, float ny, const Frame& f, bool animate);
    void show(bool on, const Frame& f);
    void update(float dt);
    // dy: soma à altura (a lista guarda o foco sem a rolagem).
    void draw(DrawList& dl, const Frame& f, float alpha, float dy = 0.0f) const;
};

// ---------------------------------------------------------------- lista grande

// A lista dos menus do jogo no estilo do menu principal da referência: linhas em versalete grande (e uma segunda
// camada menor), a com o foco em ouro com o quadradinho à esquerda, o subtítulo pequeno ao lado ("/ BACK TO THE
// MISSION") e o sublinhado com o brilho de brasa embaixo; as travadas com o cadeado.
struct BigMenu {
    struct Item {
        std::wstring label;
        std::wstring sub;
        bool compact = false;  // a segunda camada, menor ("small" é macro no windows.h)
        bool locked = false;
    };
    std::vector<Item> items;
    float x = 95.0f;   // o começo dos textos
    float y = 236.0f;  // o centro da primeira linha
    int focus = 0;
    int hover = -1;
    bool focused = true;      // a lista tem o foco do teclado/controle (o sublinhado aparece)
    std::vector<Rect> rects;  // a última montagem
    FocusMark mark;
    std::vector<Tween> lit;   // 0 normal, 1 ouro (a cor anima com o foco)

    void layout(const Frame& f);
    void set_focus(int i, const Frame& f, bool animate);
    void update(float dt, const Frame& f);
    // dx: o deslize de abrir.
    void draw(DrawList& dl, const Frame& f, float alpha, float dx) const;
    int hit(float x, float y) const;
    // O centro da linha i (depois de layout).
    float centre(int i) const;
};

// ---------------------------------------------------------------- linhas

struct RowState {
    bool focused = false;
    bool disabled = false;  // travada ou apagada
    float alpha = 1.0f;     // a linha some perto das pontas da área
    float label_t = 0.0f;   // 0 branco, 1 ouro (a cor do nome anima com o foco)
};

// A linha fina embaixo da linha.
void draw_hairline(DrawList& dl, const Frame& f, float x0, float x1, float y, float alpha);
// O nome (na coluna da esquerda da linha).
void draw_row_label(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& label, const RowState& s,
                    float indent = 0.0f, const TextStyle* style = nullptr);
// Um cabeçalho de seção ("POST EFFECTS").
void draw_section(DrawList& dl, const Frame& f, const Rect& r, const std::wstring& text, float alpha);
// O valor de uma lista que abre (o texto antes do chevron; old/old_t: o valor anterior sumindo, 0 a 1).
void draw_dropdown_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& value, bool open,
                         const RowState& s, const std::wstring& old = {}, float change_t = 1.0f);
// A chave: knob de 0 (desligada) a 1 (ligada), animado; o texto ("On"/"Off") à direita, numa coluna de `reserve`
// pixels (a maior das duas palavras: a chave não pula quando o texto muda).
void draw_toggle(DrawList& dl, const Frame& f, const Rect& row, float knob, const std::wstring& text, const RowState& s,
                 float reserve = 0.0f);
// A barra: o trilho (para o mouse), o preenchimento de 0 a 1 e o número.
Rect slider_track(const Frame& f, const Rect& row);
void draw_slider(DrawList& dl, const Frame& f, const Rect& row, float fill, const std::wstring& value, const RowState& s);
// Uma opção de uma lista aberta (o ponto de rádio à direita, de ouro na escolhida).
void draw_option(DrawList& dl, const Frame& f, const Rect& r, const std::wstring& label, bool chosen, const RowState& s);
// Um valor só para ler (o aparelho em uso) e uma linha travada (cadeado e "Coming soon").
void draw_info_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& value, const RowState& s);
void draw_locked_value(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& text, const RowState& s);
// Uma linha de ação ("APPLY CHANGES"): versalete; acesa (on) ou apagada; a tecla dela à direita.
void draw_action(DrawList& dl, const Frame& f, const Rect& row, const std::wstring& label, bool on, const RowState& s,
                 const std::wstring& key, const std::string& glyph);
// O indicador de rolagem: o trilho fino e o pedaço de ouro (frações da altura).
void draw_scroll(DrawList& dl, const Frame& f, float x, float y0, float y1, float view_frac, float pos_frac, float alpha);

// ---------------------------------------------------------------- teclas e comandos

float keycap_width(const Frame& f, const std::wstring& label);
// A tecla com o centro vertical em cy a partir de x; devolve a largura.
float draw_keycap(DrawList& dl, const Frame& f, float x, float cy, const std::wstring& label, float alpha);
// Uma tecla (key) ou o glifo do controle (glyph não vazio); devolve a largura.
float prompt_icon_width(const Frame& f, const std::wstring& key, const std::string& glyph);
float draw_prompt_icon(DrawList& dl, const Frame& f, float x, float cy, const std::wstring& key, const std::string& glyph,
                       float alpha);

struct Prompt {
    std::wstring key;   // texto da tecla
    std::string glyph;  // ou o glifo do controle
    std::wstring label;
    int action = 0;     // o que o clique faz (a tela decide)
};

struct PromptBar {
    std::wstring info_title, info_sub;  // à esquerda: o port e o jogo
    std::wstring info2_title, info2_sub;
    std::vector<Prompt> prompts;        // à direita, da esquerda para a direita
    std::vector<Rect> rects;            // a última montagem (um por comando)
    void layout(const Frame& f);
    void draw(DrawList& dl, const Frame& f, float alpha) const;
    int hit(float x, float y) const;    // a ação do comando sob o ponto, ou 0
};

// ---------------------------------------------------------------- descrição

struct Description {
    std::wstring title;
    std::wstring body;
    std::wstring value_label;  // "Current value:"
    std::wstring value;
    std::wstring notice;       // linha em ouro embaixo (mudanças não aplicadas, estado da captura)
    std::wstring strip[4];     // os textos da faixa de dados
    bool meter = false;
    std::wstring meter_label, meter_value;
    float meter_fill = 0.0f;
};
// O painel; dx: o deslize de abrir. Devolve a altura em que o texto terminou (o medidor fica no lugar dele, embaixo):
// as telas que põem mais coisas no painel continuam dali.
float draw_description(DrawList& dl, const Frame& f, const Description& d, float alpha, float dx);

// ---------------------------------------------------------------- diálogo e aviso

struct Dialog {
    bool open = false;
    std::wstring title, body;
    std::vector<std::wstring> buttons;
    int focus = 0;
    int hover = -1;
    float countdown = -1.0f;  // 0 a 1 (a barra que encolhe), < 0 sem contagem
    bool busy = false;        // sem botões: a faixa de espera (um traço de ouro que corre na linha fina)
    float grow = 0.0f;        // layout(): o painel cresce com um texto de mais de duas linhas
    Tween appear;
    FocusMark mark;
    std::vector<Rect> button_rects;
    void show(const std::wstring& t, const std::wstring& b, std::vector<std::wstring> options, int focused, const Frame& f);
    void hide(const Frame& f);
    void layout(const Frame& f);
    void update(float dt);
    void draw(DrawList& dl, const Frame& f) const;
    int hit(float x, float y) const;
};

struct Toast {
    std::wstring text;
    float age = -1.0f;  // < 0: escondido
    void show(const std::wstring& t) {
        text = t;
        age = 0.0f;
    }
    void update(float dt, const Tokens& tk);
    void draw(DrawList& dl, const Frame& f) const;
};

}  // namespace nf::ui
