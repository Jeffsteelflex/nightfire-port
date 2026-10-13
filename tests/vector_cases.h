#pragma once
// Os casos da comparação do desenho vetorial (nf_vector_check; docs/port.md, "macOS: o desenho vetorial"): o que o
// Direct2D desenha hoje no Windows (as referências em tests/data/vector) contra o que o backend do macOS
// (render/ui_vector_cg.mm) desenha, pela mesma interface (render/ui_vector.h). Tudo sintético, sem dados do jogo:
//   - listas: cada tipo de DrawCmd dos menus Modern (formas, linhas presas ao pixel, degradês, o brilho, os setores da
//     roda, textos em vários estilos e alinhamentos, os glifos SVG, os recortes, uma tela virtual escalada);
//   - telas: as telas Modern montadas como os testes montam (estados feitos à mão) a 1920 x 1080;
//   - hd: textos e teclas da camada 2D em alta definição;
//   - svg: cada SVG de hd_ui/svg em dois tamanhos (as substitutas, como a camada HD as pede), numa folha.
// Cada caso vira uma imagem opaca: o fundo kBackground com o que o desenho deu por cima (o alfa pré-multiplicado, como o
// renderizador compõe). As regiões são onde a comparação mede a caixa da tinta (cada texto, cada SVG).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/ui_hd.h"
#include "render/ui_vector.h"
#include "ui/modern_ui.h"

namespace nf::vcase {

constexpr uint8_t kBackground[3] = {64, 72, 80};  // R, G, B

struct Region {
    std::string name;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixels da imagem
    // A tolerância da caixa da tinta (pixels por borda); -1: a do grupo. Maior onde a fonte é outra no macOS (a mono:
    // Menlo no lugar da Consolas; a camada HD: DIN Alternate no lugar da Bahnschrift).
    int box_tol = -1;
};

struct Case {
    enum Kind : uint8_t { Modern, Hd, SvgSheet };
    std::string name;   // o nome do arquivo (sem .png)
    std::string group;  // "lista", "tela", "hd", "svg"
    Kind kind = Modern;
    int width = 0, height = 0;
    // Modern: as listas, desenhadas nesta ordem (uma chamada de draw_modern cada, como as camadas do menu principal).
    std::vector<ui::DrawList> lists;
    ui::Canvas canvas;
    // Hd: os textos e as teclas e a ordem deles.
    std::vector<UiHdText> texts;
    std::vector<UiHdKey> keys;
    std::vector<UiHdOp> order;
    // SvgSheet: as substitutas (índices em Assets::config.images) e o retângulo de cada uma na folha (x, y, w, h).
    std::vector<int> assets;
    std::vector<std::array<int, 4>> cells;
    std::vector<Region> regions;
};

// O map.json do repositório com cada SVG de hd_ui/svg acrescentado como substituta (na ordem dos nomes), para as folhas
// de SVG. first_svg é o índice da primeira.
struct Assets {
    UiHdConfig config;
    std::vector<std::string> files;  // "svg/xxx.svg", na ordem
    int first_svg = 0;
    std::string error;
};
Assets load_assets(const std::wstring& hd_ui_dir);

// Todos os casos. As telas se montam com a medida do próprio desenho (as larguras dos textos decidem o leiaute, como no
// jogo), por isso o canvas entra aqui.
std::vector<Case> build_cases(UiVectorCanvas& canvas, const Assets& assets);

// A imagem de um caso: o fundo e o desenho por cima, BGRA opaco de width x height. Devolve os milissegundos de CPU
// do desenho (sem a composição).
double render_case(UiVectorCanvas& canvas, const Case& c, std::vector<uint8_t>& bgra);
// Para o teste de conformidade do renderizador (tests/render_conformance.cpp, que desenha pela fachada Renderer): a lista
// da pausa Modern do caso tela_pausa, com a medida dada (a do Renderer), e os casos hd_textos (keys falso) e hd_teclas.
ui::DrawList pause_list(const ui::Measure& m, const ui::Canvas& cv);
Case hd_case(bool keys);

// Os textos e estilos cuja largura (measure) a comparação confere, e as teclas da camada HD (hd_measure).
struct MeasureItem {
    std::string name;  // "estilo:texto"
    std::wstring text;
    ui::TextStyle style;
    bool hd = false;   // hd_measure(text, cap)
    float cap = 0;
    // A diferença relativa aceita: a Rajdhani é o mesmo arquivo nos dois (1%); a mono e a camada HD usam outras fontes no
    // macOS (15% e 25%); 0: só informada (a seta, que vem da fonte de reserva de cada sistema).
    float tol = 0.01f;
};
std::vector<MeasureItem> measure_items();

// O tempo de CPU de uma tela cheia (a pausa Modern a w x h): a lista dela, com um primeiro comando diferente em cada
// variante (para nada vir pronto do começo guardado), e o tempo médio por quadro da tela animada (o brilho do foco muda a
// cada quadro, como no jogo), só o draw_modern.
ui::DrawList timing_list(UiVectorCanvas& canvas, int w, int h, int variant);
double timing_animated(UiVectorCanvas& canvas, int w, int h, int frames, size_t* changed);

// A conferência do leitor de SVG do port (core/svg.h) no Windows: o SVG lido pelo port e pintado com as primitivas do
// Direct2D (caminhos, pincéis de degradê, camadas de recorte), em w x h, RGBA com o alfa do GS como as substitutas, para
// comparar com o que o próprio leitor de SVG do Direct2D desenha (asset). Só no Windows; nas outras, false.
bool tool_parsed_svg(UiVectorCanvas& canvas, const std::string& xml, int w, int h, std::vector<uint8_t>& rgba);

// O backend desta plataforma para a ferramenta: no macOS o do sistema (create_ui_vector_canvas); no Windows o Direct2D
// de hoje por trás da mesma interface (tests/vector_canvas_d2d.cpp, as referências). Nulo onde não houver.
std::unique_ptr<UiVectorCanvas> create_tool_canvas();

}  // namespace nf::vcase
