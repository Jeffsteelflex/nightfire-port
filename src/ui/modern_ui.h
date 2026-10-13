#pragma once
// O estilo de menu Modern (só do port): o sistema de desenho próprio dos menus do PC, inspirado nos menus de
// "007 First Light" (IO Interactive, 2026). Ver docs/design.md (os tokens, a tipografia, a grade, os
// componentes e o movimento) e docs/port.md, "Menus no estilo Modern".
//
// Este arquivo é a base sem GPU (testável):
// - Tokens: as cores, os estilos de texto, as medidas e os tempos do movimento, num lugar só. Os padrões
//   estão em C++ (default_tokens) e hd_ui/design/tokens.json pode trocar qualquer um (parse_tokens).
// - Tela virtual (Canvas): tudo é desenhado numa tela de 1920 x 1080 escalada por igual para a janela: pela
//   altura (janelas mais largas ganham margem dos lados), pela largura nas mais estreitas que 16:9 (4:3 fica
//   com faixas em cima e embaixo). O fundo cobre a janela inteira (left/right/top/bottom: as bordas da
//   janela em unidades da tela virtual).
// - Movimento: as curvas (cubic-bezier, como no CSS) e o Tween, que anda em tempo real (segundos), na taxa do
//   monitor, fora dos 60 Hz da lógica.
// - Lista de desenho (DrawList): o que os componentes (modern_kit.h) pedem, em coordenadas da tela virtual;
//   o renderizador (render/ui_modern_draw_d2d.h) desenha com Direct2D e DirectWrite na resolução da janela.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nf::ui {

// ---------------------------------------------------------------- cores e tokens

struct Color {
    float r = 0, g = 0, b = 0, a = 0;  // 0..1, sem pré-multiplicar
    Color with_alpha(float k) const { return Color{r, g, b, a * k}; }
    friend bool operator==(const Color&, const Color&) = default;
};
// "#rrggbb" ou "#rrggbbaa" (false se não for).
bool parse_color(const std::string& s, Color* out);
Color rgb(uint32_t rrggbb, float a = 1.0f);
Color mix(const Color& a, const Color& b, float t);

enum class Face : uint8_t {
    Sans = 0,  // Rajdhani (hd_ui/fonts_ofl/rajdhani; reserva: Bahnschrift)
    Mono = 1,  // a faixa de dados decorativa (Consolas)
};

struct TextStyle {
    Face face = Face::Sans;
    float size = 20.0f;      // altura do em, em pixels da tela virtual
    int weight = 500;        // 300 Light, 400 Regular, 500 Medium, 600 SemiBold, 700 Bold
    float tracking = 0.0f;   // espaço entre letras, em em (0.3 = 30% do tamanho)
    bool caps = false;       // maiúsculas (o texto é convertido na hora do desenho)
    float line = 1.35f;      // altura da linha (vezes o tamanho), nos textos com quebra
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// Uma curva de movimento: cubic-bezier(x1, y1, x2, y2), como no CSS.
struct Curve {
    float x1 = 0.16f, y1 = 1.0f, x2 = 0.3f, y2 = 1.0f;
};
float curve_eval(const Curve& c, float t);  // t de 0 a 1 -> progresso (0 a 1, pode passar de 1 se a curva passa)

struct Tokens {
    // Cores.
    struct {
        Color scrim = rgb(0x0b0806, 0.58f);       // véu sobre a cena desfocada
        Color vignette = rgb(0x050302, 0.80f);    // borda da vinheta
        Color warm = rgb(0x2a1608, 0.30f);        // calor no meio da vinheta
        Color text = rgb(0xede6da);               // texto principal (branco quente)
        Color text_2 = rgb(0xc4bbae);             // valores, corpo das descrições
        Color dim = rgb(0x8a8174);                // inativo (cinza-marrom)
        Color disabled = rgb(0x5d564d);           // apagado, travado
        Color accent = rgb(0xf2c77a);             // ouro: foco, aba ativa, títulos de seção, preenchimento
        Color accent_hi = rgb(0xffe2a8);          // ouro claro (o miolo do brilho, o valor ao mudar)
        Color ember = rgb(0xff8a3d);              // brasa: o brilho do foco
        Color ember_deep = rgb(0xb8400f);         // brasa escura (a borda do brilho)
        Color hairline = rgb(0xf5e6d0, 0.10f);    // linhas finas
        Color hairline_2 = rgb(0xf5e6d0, 0.18f);  // linhas finas mais fortes (o trilho da barra de rolagem)
        Color track = rgb(0xf5e6d0, 0.13f);       // trilhos (barra, chave)
        Color knob_off = rgb(0x9a9083);           // botão da chave desligada
        Color keycap = rgb(0xd9d2c7, 0.92f);      // tecla
        Color keycap_text = rgb(0x17110c);        // letra da tecla
        Color dialog = rgb(0x120d09, 0.985f);     // fundo da caixa de diálogo
        Color meter = rgb(0xf6dba4);              // o medidor da descrição
        Color veil = rgb(0x060302, 0.62f);        // o véu mais fundo do fim de missão (sobre o véu comum)
        Color ground = rgb(0x0d0a08);             // o fundo opaco do resultado (o menu principal não tem cena)
    } color;
    // Texto.
    struct {
        TextStyle tab{Face::Sans, 16.0f, 600, 0.20f, true};
        TextStyle header{Face::Sans, 14.0f, 500, 0.42f, true};      // "OPTIONS /"
        TextStyle category{Face::Sans, 23.0f, 500, 0.03f, true};
        TextStyle section{Face::Sans, 14.0f, 600, 0.34f, true};     // "POST EFFECTS"
        TextStyle label{Face::Sans, 23.0f, 500, 0.02f, false};
        TextStyle value{Face::Sans, 22.0f, 500, 0.02f, false};
        TextStyle option{Face::Sans, 22.0f, 500, 0.02f, false};     // opções abertas de uma lista
        TextStyle title{Face::Sans, 36.0f, 600, 0.03f, true};       // título da descrição
        TextStyle body{Face::Sans, 20.0f, 400, 0.01f, false, 1.42f};
        TextStyle meta{Face::Sans, 20.0f, 500, 0.01f, false};       // "Current value: X"
        TextStyle prompt{Face::Sans, 18.0f, 500, 0.02f, false};
        TextStyle keycap{Face::Sans, 16.0f, 700, 0.0f, true};
        TextStyle table_head{Face::Sans, 13.0f, 600, 0.30f, true};
        TextStyle cell{Face::Sans, 20.0f, 500, 0.02f, false};
        TextStyle footer{Face::Sans, 17.0f, 500, 0.02f, false};
        TextStyle footer_small{Face::Sans, 11.0f, 700, 0.22f, true};
        TextStyle mono{Face::Mono, 9.0f, 400, 0.10f, true};
        TextStyle dialog_title{Face::Sans, 32.0f, 600, 0.06f, true};
        TextStyle toast{Face::Sans, 18.0f, 500, 0.02f, false};
        // Os menus do jogo (pausa, fim de missão, resultado).
        TextStyle menu_big{Face::Sans, 44.0f, 500, 0.05f, true};     // linhas grandes ("RESUME")
        TextStyle menu_small{Face::Sans, 24.0f, 500, 0.08f, true};   // linhas menores ("OPTIONS")
        TextStyle menu_sub{Face::Sans, 14.0f, 600, 0.24f, true};     // o subtítulo da linha com o foco
        TextStyle hero{Face::Sans, 92.0f, 300, 0.20f, true};         // "MISSION FAILED", "MISSION COMPLETE"
        TextStyle hero_sub{Face::Sans, 22.0f, 500, 0.30f, true};     // o motivo, o nome da missão
        TextStyle stat{Face::Sans, 72.0f, 300, 0.02f, false};        // os números grandes do resultado
        TextStyle counter{Face::Sans, 96.0f, 300, 0.0f, false};      // o contador dos objetivos
        // O menu principal.
        TextStyle page_title{Face::Sans, 52.0f, 300, 0.12f, true};   // o título da página ("SELECT CODENAME")
        TextStyle entry{Face::Sans, 60.0f, 300, 0.0f, true};         // as letras do codinome digitado
        TextStyle key{Face::Sans, 24.0f, 600, 0.0f, true};           // as teclas do teclado da tela
        TextStyle legal{Face::Sans, 14.0f, 400, 0.01f, false, 1.45f}; // o texto legal do "Press START"
    } type;
    // Medidas (pixels da tela virtual de 1920 x 1080; a grade é de 8).
    struct {
        float grid = 8.0f;
        float tabs_y = 52.0f;          // centro dos textos das abas
        float top_rule_y = 97.0f;      // linha fina sob a barra de cima
        float tab_gap = 80.0f;         // entre os textos das abas
        float tab_pad = 9.0f;          // o sublinhado passa do texto
        float tab_key_gap = 118.0f;    // da tecla Q/E ao texto da aba da ponta
        float left_x = 95.0f;          // coluna das categorias
        float marker_x = 64.0f;        // o quadradinho da categoria escolhida (e o cadeado)
        float header_y = 148.0f;       // "OPTIONS /"
        float cat_y = 212.0f;          // centro da primeira categoria
        float cat_step = 56.0f;
        float mid_x0 = 552.0f;         // coluna das linhas
        float mid_x1 = 1248.0f;
        float pad_l = 18.0f;           // o nome começa em mid_x0 + pad_l
        float pad_r = 26.0f;           // o valor termina em mid_x1 - pad_r
        float list_top = 113.0f;       // topo da primeira linha
        float list_bottom = 950.0f;    // fim da área das linhas
        float row_h = 84.0f;
        float section_h = 74.0f;
        float option_h = 76.0f;        // opção de uma lista aberta
        float option_indent = 44.0f;
        float key_row_h = 60.0f;
        float table_head_h = 54.0f;
        float fade_px = 44.0f;         // as linhas somem perto das pontas da área
        float scroll_x = 1256.0f;      // indicador de rolagem
        float desc_x0 = 1336.0f;       // coluna da descrição
        float desc_x1 = 1760.0f;
        float strip_y = 138.0f;        // a faixa de dados
        float title_y = 212.0f;        // centro do título da descrição
        float meter_x0 = 1298.0f;
        float meter_x1 = 1866.0f;
        float meter_y = 842.0f;
        float bottom_y = 1029.0f;      // centro da barra de baixo
        float prompt_right = 1862.0f;
        float prompt_gap = 36.0f;
        float keycap_h = 30.0f;
        float underline = 2.0f;        // espessura do sublinhado do foco e da aba
        float glow_w = 300.0f;         // largura do brilho do foco
        float glow_h = 46.0f;          // altura do brilho (sobe a partir do sublinhado)
        float toggle_w = 48.0f;
        float toggle_h = 24.0f;
        float slider_w = 230.0f;
        float slider_h = 4.0f;
        float value_w = 70.0f;         // coluna do número da barra
        float slide = 14.0f;           // deslocamento das colunas ao abrir
        // Os menus do jogo: a lista grande (como o menu principal da referência) e as linhas do mapa de botões.
        float menu_y = 236.0f;         // centro da primeira linha grande
        float menu_step = 78.0f;       // entre as linhas grandes
        float menu_step_small = 54.0f; // entre as menores
        float menu_gap = 30.0f;        // das grandes às menores
        float button_row_h = 58.0f;    // uma linha do mapa de botões
        float hero_y = 452.0f;         // centro do título do fim de missão
        // O menu principal.
        float front_top = 196.0f;      // topo das listas (as rodas do jogo) e do título da página
        float front_menu_y = 356.0f;   // centro da primeira linha grande do menu principal
        float logo_h = 120.0f;         // a altura do logotipo do jogo no menu principal
        float picture = 280.0f;        // a imagem da missão (redonda) na coluna da esquerda
        float key_cell = 60.0f;        // uma tecla do teclado da tela
        float key_gap = 10.0f;
        float press_y = 908.0f;        // o "Press START" da tela de título
        // A tela de entrada (o launcher, ui/modern_setup.h): linhas mais baixas que as das opções, para as quatro seções
        // caberem sem rolar, e a lista grande (PLAY, QUIT) embaixo na coluna da esquerda.
        float launch_row_h = 70.0f;
        float launch_section_h = 56.0f;
        float launch_option_h = 60.0f;  // opção de uma lista aberta
        float launch_menu_y = 760.0f;   // centro de PLAY
    } space;
    // Movimento (segundos; as curvas como no CSS).
    struct {
        float open = 0.25f;        // aparecer: o fundo e as colunas
        float close = 0.18f;
        float stagger = 0.04f;     // entre as colunas ao abrir
        float focus = 0.17f;       // o sublinhado e o brilho do foco
        float tab = 0.22f;         // o sublinhado da aba
        float dropdown = 0.18f;    // abrir e fechar uma lista
        float value = 0.14f;       // troca de valor (o texto antigo some, o novo aparece)
        float content = 0.20f;     // troca de categoria
        float toggle = 0.12f;
        float dialog = 0.16f;
        float toast_in = 0.20f;
        float toast_hold = 2.4f;
        float toast_out = 0.35f;
        float repeat_delay = 0.38f;  // repetição das setas seguradas
        float repeat_rate = 0.075f;
        float scroll = 0.20f;
        float fail_in = 0.90f;      // o fim de missão: o véu aparece devagar
        float fail_title = 1.80f;   // o título aparece e o espaço entre as letras abre
        float fail_options = 0.80f; // as opções entram depois de tanto
        float results_in = 0.45f;   // o resultado aparece
        float front_in = 0.60f;     // o menu principal aparece (do preto)
        float blur_in = 0.90f;      // a arte do título desfoca e escurece quando o menu entra
        Curve ease_out{0.16f, 1.0f, 0.3f, 1.0f};
        Curve ease_in_out{0.65f, 0.0f, 0.35f, 1.0f};
        Curve ease_in{0.5f, 0.0f, 0.75f, 0.0f};
    } motion;
};

const Tokens& default_tokens();
// Troca os tokens que o JSON traz (o resto fica como em `out`). false com a mensagem em `error` se o JSON
// for inválido ou um valor tiver o tipo errado.
bool parse_tokens(const std::string& json, Tokens& out, std::string* error);

// ---------------------------------------------------------------- tela virtual

constexpr float kCanvasW = 1920.0f;
constexpr float kCanvasH = 1080.0f;

struct Canvas {
    int win_w = 0, win_h = 0;
    float scale = 1.0f;          // pixels da janela por unidade
    float off_x = 0, off_y = 0;  // onde o (0, 0) da tela virtual cai na janela
    // As bordas da janela em unidades da tela virtual (left <= 0, right >= 1920...).
    float left = 0, top = 0, right = kCanvasW, bottom = kCanvasH;
};
Canvas make_canvas(int window_w, int window_h);
inline float canvas_x(const Canvas& c, float wx) { return (wx - c.off_x) / c.scale; }
inline float canvas_y(const Canvas& c, float wy) { return (wy - c.off_y) / c.scale; }
inline float window_x(const Canvas& c, float x) { return c.off_x + x * c.scale; }
inline float window_y(const Canvas& c, float y) { return c.off_y + y * c.scale; }

// ---------------------------------------------------------------- movimento

// Um valor que anda de `from` até `to` em `duration` segundos pela curva. set() começa do valor de agora
// (um movimento interrompido continua de onde estava).
struct Tween {
    float from = 0, to = 0;
    float elapsed = 0, duration = 0;
    Curve curve;
    void snap(float v) {
        from = to = v;
        elapsed = duration = 0;
    }
    void set(float target, float seconds, const Curve& c) {
        if (target == to && (elapsed < duration || value() == target)) return;
        from = value();
        to = target;
        elapsed = 0;
        duration = seconds;
        curve = c;
    }
    void update(float dt) { elapsed += dt; }
    bool done() const { return elapsed >= duration; }
    float progress() const { return duration <= 0 ? 1.0f : std::min(1.0f, elapsed / duration); }
    float value() const;
};

// ---------------------------------------------------------------- lista de desenho

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    Rect offset(float dx, float dy) const { return Rect{x0 + dx, y0 + dy, x1 + dx, y1 + dy}; }
};

enum class Align : uint8_t { Left = 0, Centre = 1, Right = 2 };

struct DrawCmd {
    enum Kind : uint8_t {
        Fill,       // retângulo (radius: cantos)
        Stroke,     // contorno (width)
        Line,       // de (x0, y0) a (x1, y1) (width)
        Text,       // texto no retângulo: align na horizontal, centrado na vertical numa linha (wrap: quebra
                    // na largura, de cima para baixo)
        Ellipse,    // elipse no retângulo (width > 0: só o contorno)
        Glow,       // o brilho de brasa: elipse em degradê radial (c no miolo, c2 na borda), recortada acima de y1
        Radial,     // degradê radial no retângulo inteiro (c no meio, c2 na borda): a vinheta
        Gradient,   // degradê linear (c em cima/à esquerda, c2 embaixo/à direita; dir 0 vertical, 1 horizontal)
        Chevron,    // seta fina (dir 0 baixo, 1 cima, 2 esquerda, 3 direita; width do traço)
        Padlock,    // cadeado
        Diamond,    // losango cheio (o botão da barra)
        Glyph,      // ícone de botão do controle (glyph: o nome, "xbox_a"...): o SVG de hd_ui/svg
        Sector,     // setor de anel (a roda de armas): o centro e o raio de fora em r (o quadrado do círculo), o de
                    // dentro em radius (0: até o centro), de a0 a a1 (radianos, 0 em cima, sentido horário); dir 1:
                    // em degradê do raio de dentro (c) para o de fora (c2)
        PushClip,   // recorte (retângulo) até o PopClip
        PopClip,
    };
    Kind kind = Fill;
    Rect r;
    Color c, c2;
    float width = 1.0f;
    float radius = 0.0f;
    int dir = 0;
    float a0 = 0.0f, a1 = 0.0f;  // Sector
    Align align = Align::Left;
    bool wrap = false;
    TextStyle style;
    std::wstring text;
    std::string glyph;
};

class DrawList {
public:
    std::vector<DrawCmd> cmds;
    void fill(const Rect& r, const Color& c, float radius = 0.0f);
    void stroke(const Rect& r, const Color& c, float width, float radius = 0.0f);
    void line(float x0, float y0, float x1, float y1, const Color& c, float width);
    void text(const Rect& r, const std::wstring& s, const TextStyle& st, const Color& c, Align a = Align::Left,
              bool wrap = false);
    void ellipse(const Rect& r, const Color& c, float stroke = 0.0f);
    void glow(const Rect& r, const Color& core, const Color& edge, float clip_y);
    void radial(const Rect& r, const Color& centre, const Color& edge);
    void gradient(const Rect& r, const Color& a, const Color& b, bool horizontal);
    void chevron(const Rect& r, int dir, const Color& c, float width);
    void padlock(const Rect& r, const Color& c);
    void diamond(const Rect& r, const Color& c);
    void glyph(const Rect& r, const std::string& name, float alpha);
    // Setor de anel com centro (cx, cy), raios r_in e r_out, de a0 a a1 (0 em cima, sentido horário).
    void sector(float cx, float cy, float r_in, float r_out, float a0, float a1, const Color& c);
    // O mesmo setor em degradê radial: `inner` no raio de dentro, `outer` no de fora (o brilho da roda).
    void sector_gradient(float cx, float cy, float r_in, float r_out, float a0, float a1, const Color& inner,
                         const Color& outer);
    void push_clip(const Rect& r);
    void pop_clip();
    size_t count(DrawCmd::Kind k) const;
    // O primeiro texto igual a `s` (testes e relatórios); nulo se não houver.
    const DrawCmd* find_text(const std::wstring& s) const;
};

// Uma imagem do jogo (uma textura 03xxxxxx do pacote carregado) que o renderizador desenha com o Direct3D entre duas
// listas de desenho: o menu principal Modern mostra assim a arte do "Press START", o logotipo e a imagem da missão
// (render/ui_modern_app.h). Na tela virtual; uv é a parte da textura (0 a 1).
struct ImageCmd {
    uint32_t hash = 0;
    Rect r;
    Rect uv{0.0f, 0.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    // A textura inteira com meio texel de margem em cada borda (o renderizador mede a textura): para as imagens de
    // tamanho que a tela não sabe (as do dossiê). Com isso, uv é ignorado.
    bool whole = false;
    // Uma imagem do port guardada pelo renderizador com esse hash (Renderer::set_ui_image: os ícones da roda de armas,
    // feitos das imagens do dossiê do pacote do menu), não uma textura do pacote carregado; desenhada com filtro e
    // mipmaps.
    bool port = false;
    // A cor que multiplica a imagem (r, g, b; o alfa é `alpha`): os ícones da roda são de um tom só e ganham a cor do
    // estado (o ouro do apontado).
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};
using ImageList = std::vector<ImageCmd>;
// O hash de ImageCmd que quer dizer o quadro do filme que toca (platform/psi_movie.h; o renderizador o desenha da
// textura dele, Renderer::movie_srv), não uma textura do jogo.
constexpr uint32_t kMovieImage = 0xffffffffu;

// A largura de um texto num estilo (o renderizador mede com o DirectWrite; os testes usam estimate_width).
using Measure = std::function<float(const std::wstring& text, const TextStyle& style)>;
float estimate_width(const std::wstring& text, const TextStyle& style);

// Maiúsculas (latim-1 e os acentos do português).
std::wstring to_upper(const std::wstring& s);
std::wstring widen(const std::string& utf8);

}  // namespace nf::ui
