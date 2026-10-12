#pragma once
// Camada 2D em alta definição (só no PC; opcional, --hd-ui / F4). Ver docs/port.md, "Camada 2D em alta
// definição".
//
// O 2D original (platform/psi_2d.h) chega ao renderizador como uma lista de Psi2DQuad na tela do PS2, cada
// um com a origem (Psi2DOrigin: que sprite, que textura, que glifo de que fonte, em que passada). Esta
// camada refaz o desenho desses elementos na resolução da janela, sem mexer na lógica: a posição, o
// tamanho, a cor, o alfa e o tempo continuam sendo os que o jogo decidiu.
//
// - Texto: os glifos seguidos de uma mesma chamada de __Font_DrawText (Psi2DOrigin::run) voltam a ser o
//   texto original, linha a linha (a quebra de linha e as páginas são as do jogo), com a cor de cada
//   caractere; as passadas de sombra e contorno que vêm antes viram efeitos do texto principal (a sombra,
//   uma cópia deslocada; as quatro cópias em (+-1, +-1), um traço em volta). O renderizador desenha com
//   DirectWrite (render/ui_hd_draw.h), na fonte que hd_ui/map.json dá a cada fonte do jogo, com a altura
//   das maiúsculas da fonte do jogo e, se a linha ficar mais larga que a original, comprimida na
//   horizontal até caber.
// - Imagens: hd_ui/map.json liga uma imagem (hashcode da textura e um retângulo em texels, ou a pane e o
//   sprite do HUD) a uma substituta SVG ou PNG. O elemento continua sendo um quadrado do 2D original (cor,
//   alfa, mistura aditiva ou subtrativa, espelhamento e ladrilhos iguais), só com outra textura: a
//   substituta inteira cobre o retângulo da chave e o pedaço do elemento é recortado dela. Sem substituta,
//   a textura original, com filtro bilinear.
// - Âncoras (opcional): numa janela mais larga que 4:3, os elementos do HUD no terço esquerdo da tela do
//   PS2 vão para a borda esquerda e os do terço direito para a direita. Um elemento anda junto com os
//   vizinhos da mesma pane que encostam nele (o medidor de vida inteiro, a faixa das mensagens com as
//   pontas), e o terço é o do grupo.
//
// Este arquivo é a parte sem GPU (testável): a configuração, o agrupamento dos glifos em textos, as
// âncoras e a escolha das substitutas; o resultado é um UiHdBatch por chamada de psiDrawSprites, que o
// renderizador desenha no lugar dos quadrados originais.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "platform/input_bindings.h"
#include "platform/psi_2d.h"

namespace nf {

// ---------------------------------------------------------------- configuração (hd_ui/map.json)

enum class UiHdAnchor : uint8_t {
    Centre = 0,  // como o original: tudo na área 4:3 centrada
    Edges = 1,   // painéis do HUD dos terços esquerdo e direito presos às bordas da janela
};

// Uma fonte do jogo (1 NFont2, 2 SerpLight, 3 Medium) e a fonte vetorial que a substitui.
struct UiHdFont {
    std::wstring family = L"Bahnschrift";
    int weight = 400;       // DWRITE_FONT_WEIGHT (100..950)
    int stretch = 5;        // DWRITE_FONT_STRETCH (1..9; 5 normal)
    bool italic = false;
    float size = 1.0f;      // multiplica o tamanho calculado (altura das maiúsculas da fonte do jogo)
    float spacing = 0.0f;   // espaço a mais entre os caracteres, em em
    float fit_min = 0.7f;   // compressão horizontal máxima quando a linha passa da largura original
};

enum class UiHdAssetType : uint8_t { Svg = 0, Png = 1 };

// Chave de uma imagem: o hashcode da textura (03xxxxxx; 0 = qualquer) e um retângulo em texels (u, v,
// largura, altura; sem retângulo = a textura inteira), e/ou a identidade no HUD (pane 0..21, -1 a mira;
// sprite: índice na pane). O elemento casa se a parte da textura que ele mostra fica dentro do retângulo.
struct UiHdImageKey {
    uint32_t texture = 0;
    bool has_rect = false;
    float rect[4]{};
    int pane = kAny;
    int sprite = kAny;
    static constexpr int kAny = -1000;
};

struct UiHdImageEntry {
    UiHdImageKey key;
    std::string file;       // relativo à pasta hd_ui
    UiHdAssetType type = UiHdAssetType::Svg;
    // Como o recorte do elemento cobre os texels, em u e em v: "inclusive" (o padrão: u..u+uw são os
    // centros do primeiro e do último texel, como os sprites com uw = largura - 1; a substituta vai do
    // centro do primeiro texel da chave ao do último) ou "exclusive" (u..u+uw são as bordas: uw texels,
    // como as balas da pilha; a substituta vai da borda da chave à outra).
    bool exclusive[2] = {false, false};
    std::string name;       // comentário
    bool prompt_only = false;  // só pelos símbolos de botão ("prompts"): fora de find_image
};

// ---------------------------------------------------------------- símbolos de botão por aparelho

// O conjunto de símbolos dos textos ("~A"...): o do PS2 (o desenho do jogo, em vetor), teclas do teclado
// (com a tecla ligada a cada comando, platform/input_bindings.h) ou botões do controle do Xbox. Auto segue o
// último aparelho usado (UiHd_SetPromptDevice). [ui] prompts = auto|ps2|keyboard|xbox.
enum class UiHdPromptSet : uint8_t { Auto = 0, Ps2 = 1, Keyboard = 2, Xbox = 3 };
bool ui_hd_parse_prompt_set(const std::string& name, UiHdPromptSet* out);
const char* ui_hd_prompt_set_name(UiHdPromptSet set);
// O que o jogador escolheu (Auto por padrão) e o gancho do aparelho em uso (o main chama a cada quadro
// com psiInput_ActiveDevice; Keyboard até o primeiro aviso). O conjunto em vigor: o escolhido, ou o do
// aparelho com Auto.
void UiHd_SetPromptSetting(UiHdPromptSet set);
UiHdPromptSet UiHd_PromptSetting();
void UiHd_SetPromptDevice(UiHdPromptSet device);
UiHdPromptSet UiHd_PromptDevice();
UiHdPromptSet ui_hd_prompt_effective();
UiHdPromptSet ui_hd_prompt_effective(UiHdPromptSet setting, UiHdPromptSet device);

// Os símbolos da fonte (specialchar do ACTION.ELF, textura 0x03000075): o botão do PS2 de cada código.
enum class UiHdPadButton : uint8_t {
    None, Cross, Circle, Triangle, Square, DpadV, DpadH, Dpad, L1, L2, R1, R2, LStick, RStick, Start, Select,
    Check, Dot,
};
UiHdPadButton ui_hd_symbol_button(uint8_t code);
// O botão do Xbox que faz o que o do PS2 faz no PC (platform/psi_pad.h: A = X, B = círculo, X = quadrado,
// Y = triângulo, Back/View = Select, Menu = Start; com triggers_r1 RT/LT = R1/L1 e RB/LB = R2/L2): "A", "B",
// "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Menu", "View", "DpadV", "DpadH", "Dpad"; nulo nos que não
// são botões (o visto e o ponto dos objetivos).
const char* ui_hd_xbox_name(uint8_t code, bool triggers_r1);
// As teclas de um símbolo no teclado, como texto da tecla (vazio: nenhuma tecla faz isso). Num texto de
// menu (game = false) os botões de face, o Start e o direcional são os comandos do menu (escolher,
// círculo, voltar, quadrado, pausa, setas); no mapa de botões da aba CONTROLS da pausa (game = true) e nos
// gatilhos, analógicos e Select, o comando do jogo que o botão faz no estilo (psi_pad_style_inputs). O
// analógico direito vira "Mouse".
std::wstring ui_hd_prompt_keys(uint8_t code, bool game, const nf::Bindings& bindings, uint16_t style, bool triggers_r1);
// O texto curto de uma tecla (as setas viram ↑ ↓ ← →; o resto, input_label em inglês).
std::wstring ui_hd_key_label(nf::InputCode c);
// A largura de uma tecla desenhada com o texto "label_w" pixels de largura e "h" de altura.
float ui_hd_keycap_width(float label_w, float h);

struct UiHdConfig {
    UiHdAnchor anchor = UiHdAnchor::Centre;
    // Fora das âncoras: panes inteiras (padrão: 8, o reflexo do sol, que segue o sol na tela) e sprites
    // (pane, sprite; padrão: 0:7, o alvo das armas 0x50/0x51, que segue um objeto).
    std::vector<int> anchor_skip_panes{8};
    std::vector<std::pair<int, int>> anchor_skip_sprites{{0, 7}};
    bool anchor_skipped(int pane, int sprite) const;
    bool text = true;       // false: os glifos continuam sendo os da textura (depuração)
    UiHdFont fonts[4];      // [1..3]; [0] não é usada
    std::vector<UiHdImageEntry> images;
    // Símbolos de botão ("prompts" no map.json): o desenho de cada código no conjunto do PS2 ("A" ... "Y") e
    // de cada botão do Xbox (ui_hd_xbox_name), como índices em images (entradas prompt_only).
    std::map<std::string, int> prompt_ps2;
    std::map<std::string, int> prompt_xbox;

    UiHdConfig();
    // A entrada que serve para um elemento (texture: hashcode; texel: retângulo do elemento em texels,
    // u0 <= u1, v0 <= v1; pane/sprite: identidade no HUD, kAny se não houver). Chaves com pane ou sprite
    // vêm antes das só com textura; entre iguais, a primeira do arquivo. -1 se nenhuma.
    int find_image(uint32_t texture, const float texel[4], int pane, int sprite) const;
};

// Lê o map.json (JSON comum; ver hd_ui/map.json). Devolve false e a mensagem em "error" se não der.
bool ui_hd_parse_config(const std::string& json, UiHdConfig& out, std::string* error);

// ---------------------------------------------------------------- o que o jogo sabe (por quadro)

// A identidade de um sprite no HUD do jogador.
struct UiHdIdentity {
    int pane = UiHdImageKey::kAny;   // 0..21; -1 a mira
    int sprite = UiHdImageKey::kAny;
};

struct UiHdEnv {
    int width = 0, height = 0;  // janela
    // Tamanho da textura de um hashcode 03xxxxxx (false se não houver).
    std::function<bool(uint32_t hash, int& w, int& h)> texture_size;
    // A identidade de um sprite do jogo (nulo: nenhuma).
    std::function<UiHdIdentity(const void* sprite)> identity;
    // Altura das maiúsculas de cada fonte do jogo (1..3), em pixels da tela do PS2 com escala 1 (a do 'H').
    float cap_height[4] = {0.0f, 12.0f, 12.0f, 12.0f};
    // Símbolos de botão: o conjunto em vigor neste quadro (Ps2, Keyboard ou Xbox; Auto vale Ps2), os gatilhos
    // do controle (RT/LT = R1/L1), o texto da tecla de um símbolo (ui_hd_prompt_keys; vazio: sem tecla), se
    // o texto de um sprite é do mapa de botões do jogo (a aba CONTROLS da pausa) e a largura de um texto de
    // tecla em pixels (nulo: estimada).
    UiHdPromptSet prompts = UiHdPromptSet::Ps2;
    bool triggers_r1 = true;
    std::function<std::wstring(uint8_t code, bool game)> key_label;
    std::function<bool(const void* sprite)> game_context;
    std::function<float(const std::wstring& label, float cap_px)> measure;
};

// ---------------------------------------------------------------- o desenho

// Uma imagem: um quadrado do 2D original já na janela, com a textura original ou uma substituta.
struct UiHdImage {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // janela
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // ST na textura original, ou na substituta (asset >= 0)
    uint8_t rgba[4]{};                     // escala do GS
    int32_t texture = -1;                  // slot de Tex (a original)
    uint64_t alpha = 0x44;                 // ALPHA_1
    bool linear = true;                    // false: filtro de ponto (glifos com "text": false)
    int32_t asset = -1;                    // UiHdConfig::images
    int32_t asset_w = 0, asset_h = 0;      // pixels em que a substituta inteira é rasterizada (SVG)
    float orig_uv[4]{};                    // o ST original (se a substituta não carregar)
    float clip[4]{};                       // recorte (janela)
};

// Um trecho de cor de uma linha (caracteres [start, start + length) do texto UTF-16).
struct UiHdSpan {
    uint32_t start = 0, length = 0;
    uint8_t rgba[4]{};  // escala do GS: 0x80 = 1.0 (a textura das fontes é branca)
};

struct UiHdLine {
    std::wstring text;
    std::vector<UiHdSpan> spans;
    float x0 = 0, x1 = 0;  // extensão dos glifos originais (janela)
    float baseline = 0;    // linha de base (janela)
    // Um trecho entre símbolos de botão ("~A"): alinhado à esquerda em x0 e comprimido para caber até x1,
    // fora do alinhamento do bloco.
    bool pinned = false;
};

struct UiHdEffect {
    enum Kind : uint8_t {
        Copy = 0,    // o texto inteiro deslocado (sombra, realce), numa cor só
        Stroke = 1,  // traço em volta dos glifos (o contorno de quatro cópias)
    };
    Kind kind = Copy;
    float dx = 0, dy = 0;  // janela
    float width = 0;       // largura total do traço (janela)
    uint8_t rgba[4]{};     // escala do GS
};

struct UiHdText {
    uint8_t font = 2;      // 1..3
    float cap_px = 0;      // altura das maiúsculas da fonte do jogo, na janela
    uint8_t align = 0;     // 0 à esquerda, 2 centrado, 4 à direita (o bloco)
    uint64_t alpha = 0x44; // ALPHA_1 dos glifos
    std::vector<UiHdLine> lines;
    std::vector<UiHdEffect> effects;  // desenhados antes, nesta ordem
    float clip[4]{};
};

// Uma tecla do teclado no lugar de um símbolo de botão (Direct2D: tecla arredondada e o texto).
struct UiHdKey {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // janela
    std::wstring label;
    float cap_px = 0;      // altura das maiúsculas do texto (janela)
    uint8_t rgba[4]{};     // escala do GS (o alfa do símbolo)
    float clip[4]{};
};

struct UiHdOp {
    enum Kind : uint8_t { Image = 0, Text = 1, Key = 2 };
    Kind kind = Image;
    uint32_t index = 0;
};

// O que o renderizador desenha no lugar de uma chamada de psiDrawSprites.
struct UiHdBatch {
    std::vector<UiHdImage> images;
    std::vector<UiHdText> texts;
    std::vector<UiHdKey> keys;
    std::vector<UiHdOp> ops;
    bool empty() const { return ops.empty(); }
};

// O 2D de um quadro inteiro (PsiFrame::quads2d/lists2d) refeito em HD: um lote por chamada de
// psiDrawSprites (o índice de lists2d), com cada elemento no lote da chamada em que ele termina (um texto
// pode atravessar a descarga de SprBuffList a cada 63 sprites; as passadas de sombra e contorno esperam o
// texto principal).
struct UiHdFrame {
    std::map<uint32_t, std::shared_ptr<UiHdBatch>> batches;
};
UiHdFrame ui_hd_build(const Psi2DQuad* quads, size_t count, const Psi2DList* lists, size_t list_count,
                      const UiHdConfig& config, const UiHdEnv& env);

// ---------------------------------------------------------------- peças (expostas para os testes)

// Um texto do jogo: os quadrados seguidos de uma chamada de __Font_DrawText.
struct UiHdRun {
    uint32_t first = 0, count = 0;  // em quads
    uint16_t run = 0;
    Psi2DPass pass = Psi2DPass::Main;
    const void* sprite = nullptr;
    const char* text = nullptr;
};
// Agrupa os glifos e símbolos (Psi2DKind::Glyph/Symbol) em textos; imagens ficam de fora.
std::vector<UiHdRun> ui_hd_group_runs(const Psi2DQuad* quads, size_t count);

// O caractere Unicode de um byte do texto do jogo (Windows-1252, como o USATxt.dat), com as trocas que a
// fonte do jogo faz por falta de glifo desfeitas: 0x92 é ’, 0x85 é …, 0x99 é ™. 0xa0 vira espaço.
wchar_t ui_hd_char(uint8_t c);

// As linhas de um texto (só os quadrados da passada; sem efeitos), em coordenadas da tela do PS2.
struct UiHdRunLines {
    std::vector<UiHdLine> lines;          // x0, x1 e baseline na tela do PS2
    std::vector<uint32_t> symbols;        // índices (em quads) dos símbolos de botão
    float first_x = 0, first_baseline = 0;  // o primeiro glifo (para medir o deslocamento das cópias)
};
UiHdRunLines ui_hd_run_lines(const Psi2DQuad* quads, const UiHdRun& run);

// Grupos de âncora: os retângulos (tela do PS2) de uma mesma pane que se encostam (até "gap" pixels)
// formam um grupo; devolve o grupo de cada retângulo (índices de 0 a n - 1). Pane kAny: grupo próprio.
std::vector<uint32_t> ui_hd_anchor_groups(const std::vector<std::array<float, 4>>& rects, const std::vector<int>& panes,
                                          float gap);

// Deslocamento horizontal (pixels da janela) da âncora de um elemento cujo retângulo de referência, na
// tela do PS2, vai de x0 a x1: -vp.left no terço esquerdo, +vp.left no direito, 0 no meio ou com Centre.
float ui_hd_anchor_shift(UiHdAnchor anchor, const Psi2DViewport& vp, int window_width, float x0, float x1);
float ui_hd_anchor_shift_y(UiHdAnchor anchor, const Psi2DViewport& vp, int window_height, float y0, float y1);

// O retângulo em texels que um quadrado mostra (centros do primeiro e do último texel, na ordem do
// quadrado: espelhado, u0 > u1) numa textura de w x h; false sem tamanho.
bool ui_hd_texel_span(const Psi2DQuad& q, int w, int h, float out[4]);
// ST na substituta inteira (que cobre o retângulo da chave) para o pedaço [c0, c1] de texels.
void ui_hd_asset_uv(const UiHdImageEntry& e, int tex_w, int tex_h, const float span[4], float uv[4]);

// ---------------------------------------------------------------- inventário (--ui-inventory)

// Os elementos de imagem distintos que o 2D desenhou (hashcode e recorte), quantas vezes, de que pane e
// sprite do HUD e em que tamanho na tela do PS2. Junta com um inventory.tsv que já exista.
class UiHdInventory {
public:
    struct Entry {
        uint32_t hash = 0;
        int u = 0, v = 0, w = 0, h = 0;  // recorte em texels (u, v o canto; w, h a extensão, como uw e vh)
        int tex_w = 0, tex_h = 0;
        int slot = -1;                   // slot de Tex visto por último nesta execução (-1: de outra)
        float screen_w = 0, screen_h = 0;  // maior tamanho visto na tela do PS2
        uint32_t count = 0;              // quadros em que apareceu
        uint8_t kind = 0;                // Psi2DKind
        uint64_t alpha = 0x44;
        std::string owners;              // "pane:sprite" separados por vírgula (-1 a mira, ? sem pane)
        bool seen_now = false;           // apareceu nesta execução
    };
    void add_frame(const Psi2DQuad* quads, size_t count, const UiHdEnv& env);
    bool load(const std::string& tsv);
    std::string to_tsv() const;
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    std::map<std::string, size_t> index_;
};

}  // namespace nf
