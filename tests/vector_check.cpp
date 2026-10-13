// nf_vector_check: o desenho vetorial de uma plataforma contra as referências do Direct2D (docs/port.md, "macOS: o
// desenho vetorial"; os casos em tests/vector_cases.h).
//
//   nf_vector_check --hd-ui <pasta hd_ui> --write <pasta>
//       desenha os casos com o backend desta plataforma e grava <caso>.png e measures.tsv (no Windows, com o Direct2D:
//       são as referências de tests/data/vector);
//   nf_vector_check --hd-ui <pasta hd_ui> --compare <referências> --out <pasta> [--strict] [--bench]
//       desenha, compara com as referências e grava <caso>.png (este desenho), <caso>_dif.png (a diferença, 4x) e
//       report.txt. --strict sai com 1 se algum caso passar da tolerância; --bench mede o tempo de CPU de uma tela
//       cheia (a pausa Modern) a 1920 x 1080 e a 2560 x 1440, sem e com o cache.
//
// O texto do CoreText e o do DirectWrite nunca são iguais pixel a pixel (a rasterização, o gama do texto do Direct2D,
// as fontes da camada HD, que são outras no macOS): além da diferença média, a comparação mede o que importa, as caixas
// da tinta de cada texto e de cada SVG (as regiões dos casos) e as larguras do measure.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "platform/crt_compat.h"
#include "platform/image_io.h"
#include "vector_cases.h"

using namespace nf;
using namespace nf::vcase;

#if !defined(_WIN32)
namespace nf::vcase {
std::unique_ptr<UiVectorCanvas> create_tool_canvas() { return create_ui_vector_canvas(); }
bool tool_parsed_svg(UiVectorCanvas&, const std::string&, int, int, std::vector<uint8_t>&) { return false; }
}  // namespace nf::vcase
#endif

namespace {

std::wstring wide(const std::string& s) { return std::filesystem::path(s).wstring(); }

struct Ink {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
};

// A caixa dos pixels que se afastam do fundo (mais de 40 num canal) dentro de uma região.
Ink ink_box(const std::vector<uint8_t>& bgra, int w, int h, const Region& r) {
    Ink b;
    for (int y = std::max(0, r.y0); y < std::min(h, r.y1); ++y) {
        for (int x = std::max(0, r.x0); x < std::min(w, r.x1); ++x) {
            const uint8_t* p = bgra.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
            const int d = std::max({std::abs(p[2] - kBackground[0]), std::abs(p[1] - kBackground[1]), std::abs(p[0] - kBackground[2])});
            if (d <= 40) continue;
            if (b.empty) {
                b.x0 = b.x1 = x;
                b.y0 = b.y1 = y;
                b.empty = false;
            }
            b.x0 = std::min(b.x0, x);
            b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x + 1);
            b.y1 = std::max(b.y1, y + 1);
        }
    }
    return b;
}

struct Metrics {
    double mae = 0;       // a diferença média por canal (0 a 255)
    double over = 0;      // a fração dos pixels com algum canal diferente em mais de 32
    int max = 0;
};

Metrics compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int w, int h, const Region* only,
                std::vector<uint8_t>* diff) {
    Metrics m;
    if (diff != nullptr) diff->assign(a.size(), 255);
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (only != nullptr) {
        x0 = std::max(0, only->x0);
        y0 = std::max(0, only->y0);
        x1 = std::min(w, only->x1);
        y1 = std::min(h, only->y1);
    }
    double sum = 0;
    size_t n = 0, over = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            int worst = 0;
            for (int k = 0; k < 3; ++k) {
                const int d = std::abs(int(a[i + size_t(k)]) - int(b[i + size_t(k)]));
                sum += d;
                worst = std::max(worst, d);
                if (diff != nullptr) (*diff)[i + size_t(k)] = uint8_t(std::min(255, d * 4));
            }
            ++n;
            over += worst > 32 ? 1 : 0;
            m.max = std::max(m.max, worst);
        }
    }
    if (n > 0) {
        m.mae = sum / double(n * 3);
        m.over = double(over) / double(n);
    }
    return m;
}

// As tolerâncias por grupo (as diferenças que ficam: o antisserrilhado das bordas e do texto, o gama do texto do
// Direct2D, as fontes da camada HD, que são outras no macOS).
struct Tolerance {
    double mae, over;   // a imagem inteira: a diferença média por canal e a fração dos pixels com mais de 32
    int box;            // a caixa da tinta de cada região, em pixels (cada borda), se a região não disser outra
};
Tolerance tolerance(const std::string& group) {
    if (group == "lista") return {1.5, 0.015, 2};
    if (group == "tela") return {1.5, 0.01, 2};
    if (group == "hd") return {8.0, 0.08, 12};
    return {1.5, 0.005, 3};  // svg (a coluna das balas em 4 pixels de largura passa de 2)
}

bool read_measures(const std::string& path, std::map<std::string, double>& out) {
    FILE* f = fopen_narrow(path.c_str(), "rb");
    if (f == nullptr) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        char* tab = std::strchr(line, '\t');
        if (tab == nullptr) continue;
        *tab = 0;
        out[line] = std::strtod(tab + 1, nullptr);
    }
    std::fclose(f);
    return true;
}

double measure_of(UiVectorCanvas& canvas, const MeasureItem& m) {
    return m.hd ? canvas.hd_measure(m.text, m.cap) : canvas.measure(m.text, m.style);
}

// O tempo de CPU da pausa Modern numa tela cheia, num desenho novo (os caches de cada tamanho começam vazios): a lista
// toda desenhada (seis variantes em rodízio, mais do que as camadas guardam), a tela animada quadro a quadro (o brilho do
// foco muda a cada quadro: só o fim da lista é desenhado de novo) e a mesma lista de novo (a imagem guardada).
void bench(const std::wstring& hd_dir, std::string& report) {
    for (const auto& [w, h] : {std::pair<int, int>{1920, 1080}, std::pair<int, int>{2560, 1440}}) {
        std::unique_ptr<UiVectorCanvas> canvas = create_tool_canvas();
        if (canvas == nullptr) return;
        canvas->configure_modern(hd_dir);
        std::vector<ui::DrawList> lists;
        for (int v = 0; v < 6; ++v) lists.push_back(timing_list(*canvas, w, h, v));
        const ui::Canvas cv = ui::make_canvas(w, h);
        UiRaster out;
        canvas->draw_modern(lists[0], cv, w, h, out);  // aquece as fontes e as linhas de texto
        const int n = 24;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) canvas->draw_modern(lists[size_t(i % 6)], cv, w, h, out);
        const double full = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        size_t changed = 0;
        const double animated = timing_animated(*canvas, w, h, 120, &changed);
        canvas->draw_modern(lists[0], cv, w, h, out);  // a lista volta a ser a última de uma camada
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) canvas->draw_modern(lists[0], cv, w, h, out);
        const double cached = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        char line[320];
        std::snprintf(line, sizeof(line),
                      "tempo pausa %dx%d: %zu comandos; a lista toda %.2f ms; animada %.2f ms por quadro (%zu de 120 quadros "
                      "mudaram); igual %.3f ms; imagem %dx%d\n",
                      w, h, lists[0].cmds.size(), full, animated, changed, cached, out.width, out.height);
        report += line;
        std::fputs(line, stdout);
        const std::string d = "  " + canvas->modern_describe() + "\n";
        report += d;
        std::fputs(d.c_str(), stdout);
    }
}

// --svg-parser (só no Windows): cada SVG pelo leitor do Direct2D (asset) e pelo leitor do port pintado com as primitivas
// do Direct2D, nos dois tamanhos das folhas; a diferença média de cada um sobre o fundo e as duas folhas lado a lado.
int check_parser(UiVectorCanvas& canvas, const Assets& assets, const std::string& hd_ui, const std::string& dir) {
    int bad = 0;
    for (const int box : {40, 176}) {
        Case sheet;
        for (const Case& c : build_cases(canvas, assets)) {
            if (c.kind == Case::SvgSheet && c.cells.size() == assets.files.size() &&
                (box == 40 ? c.name == "svg_pequeno" : c.name == "svg_grande")) {
                sheet = c;
            }
        }
        if (sheet.cells.empty()) return 1;
        std::vector<uint8_t> a, b;
        render_case(canvas, sheet, a);
        b = a;
        // A folha do port: o fundo e cada SVG lido pelo port por cima.
        for (size_t i = 0; i < b.size(); i += 4) {
            b[i + 0] = kBackground[2];
            b[i + 1] = kBackground[1];
            b[i + 2] = kBackground[0];
        }
        for (size_t i = 0; i < sheet.cells.size(); ++i) {
            const auto& cell = sheet.cells[i];
            FILE* f = fopen_narrow((hd_ui + "/" + assets.files[i]).c_str(), "rb");
            std::string xml;
            if (f != nullptr) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) xml.append(buf, n);
                std::fclose(f);
            }
            std::vector<uint8_t> rgba;
            if (!tool_parsed_svg(canvas, xml, cell[2], cell[3], rgba)) {
                std::printf("%s: o port não pintou\n", assets.files[i].c_str());
                ++bad;
                continue;
            }
            for (int y = 0; y < cell[3]; ++y) {
                for (int x = 0; x < cell[2]; ++x) {
                    const uint8_t* s = rgba.data() + (size_t(y) * size_t(cell[2]) + size_t(x)) * 4;
                    uint8_t* d = b.data() + (size_t(cell[1] + y) * size_t(sheet.width) + size_t(cell[0] + x)) * 4;
                    const float al = std::min(1.0f, s[3] / 128.0f);
                    d[0] = uint8_t(std::lround(s[2] * al + d[0] * (1.0f - al)));
                    d[1] = uint8_t(std::lround(s[1] * al + d[1] * (1.0f - al)));
                    d[2] = uint8_t(std::lround(s[0] * al + d[2] * (1.0f - al)));
                }
            }
            const Metrics m = compare(a, b, sheet.width, sheet.height, &sheet.regions[i], nullptr);
            std::printf("%-30s %3dx%-3d dif_media %5.2f max %3d%s\n", assets.files[i].c_str(), cell[2], cell[3], m.mae, m.max,
                        m.mae > 1.0 ? "  <" : "");
            bad += m.mae > 1.0 ? 1 : 0;
        }
        write_png_bgra(wide(dir + "/" + sheet.name + "_d2d.png"), sheet.width, sheet.height, a.data());
        write_png_bgra(wide(dir + "/" + sheet.name + "_port.png"), sheet.width, sheet.height, b.data());
    }
    std::printf("%d SVG(s) com diferença média acima de 1\n", bad);
    return bad > 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string hd_ui, write_dir, ref_dir, out_dir, parser_dir;
    bool strict = false, do_bench = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--hd-ui" && i + 1 < argc) hd_ui = argv[++i];
        else if (a == "--write" && i + 1 < argc) write_dir = argv[++i];
        else if (a == "--svg-parser" && i + 1 < argc) write_dir = parser_dir = argv[++i];
        else if (a == "--compare" && i + 1 < argc) ref_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--strict") strict = true;
        else if (a == "--bench") do_bench = true;
        else {
            std::fprintf(stderr, "argumento desconhecido: %s\n", a.c_str());
            return 2;
        }
    }
    if (hd_ui.empty() || (write_dir.empty() && (ref_dir.empty() || out_dir.empty()))) {
        std::fprintf(stderr, "uso: nf_vector_check --hd-ui <pasta> (--write <pasta> | --compare <refs> --out <pasta>) "
                             "[--strict] [--bench]\n");
        return 2;
    }
    std::unique_ptr<UiVectorCanvas> canvas = create_tool_canvas();
    if (canvas == nullptr || !canvas->ok()) {
        std::fprintf(stderr, "sem o desenho vetorial nesta plataforma\n");
        return 1;
    }
    const std::wstring hd_dir = wide(hd_ui);
    canvas->configure_modern(hd_dir);
    Assets assets = load_assets(hd_dir);
    if (!assets.error.empty()) std::fprintf(stderr, "map.json: %s\n", assets.error.c_str());
    canvas->configure_hd(&assets.config, hd_dir);
    std::string report = "desenho modern: " + canvas->modern_describe() + "\ndesenho hd: " + canvas->hd_describe() + "\n";
    std::fputs(report.c_str(), stdout);

    const std::string dest = write_dir.empty() ? out_dir : write_dir;
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (!parser_dir.empty()) return check_parser(*canvas, assets, hd_ui, parser_dir);
    const std::vector<Case> cases = build_cases(*canvas, assets);
    const std::vector<MeasureItem> items = measure_items();
    int failures = 0;

    if (!write_dir.empty()) {
        for (const Case& c : cases) {
            std::vector<uint8_t> img;
            const double ms = render_case(*canvas, c, img);
            write_png_bgra(wide(dest + "/" + c.name + ".png"), c.width, c.height, img.data());
            std::printf("%-22s %4dx%-4d %7.2f ms\n", c.name.c_str(), c.width, c.height, ms);
        }
        FILE* f = fopen_narrow((dest + "/measures.tsv").c_str(), "wb");
        if (f != nullptr) {
            for (const MeasureItem& m : items) std::fprintf(f, "%s\t%.4f\n", m.name.c_str(), measure_of(*canvas, m));
            std::fclose(f);
        }
        if (do_bench) bench(hd_dir, report);
        return 0;
    }

    char line[512];
    std::snprintf(line, sizeof(line), "%-22s %-6s %9s %8s %5s %6s  %s\n", "caso", "grupo", "dif_media", "pixels%", "max",
                  "caixa", "resultado");
    report += line;
    std::fputs(line, stdout);
    std::string details;
    for (const Case& c : cases) {
        std::vector<uint8_t> img;
        const double ms = render_case(*canvas, c, img);
        write_png_bgra(wide(dest + "/" + c.name + ".png"), c.width, c.height, img.data());
        int rw = 0, rh = 0;
        std::vector<uint8_t> ref;
        if (!read_image_bgra(wide(ref_dir + "/" + c.name + ".png"), 8192, &rw, &rh, &ref) || rw != c.width || rh != c.height) {
            std::snprintf(line, sizeof(line), "%-22s sem a referência (ou de outro tamanho)\n", c.name.c_str());
            report += line;
            std::fputs(line, stdout);
            ++failures;
            continue;
        }
        std::vector<uint8_t> diff;
        const Metrics m = compare(img, ref, c.width, c.height, nullptr, &diff);
        write_png_bgra(wide(dest + "/" + c.name + "_dif.png"), c.width, c.height, diff.data());
        const Tolerance t = tolerance(c.group);
        int worst_box = 0;
        bool box_fail = false;
        for (const Region& r : c.regions) {
            const Ink a = ink_box(img, c.width, c.height, r), b = ink_box(ref, c.width, c.height, r);
            const Metrics rm = compare(img, ref, c.width, c.height, &r, nullptr);
            int box = 0;
            if (a.empty != b.empty) box = 999;
            else if (!a.empty) box = std::max({std::abs(a.x0 - b.x0), std::abs(a.y0 - b.y0), std::abs(a.x1 - b.x1), std::abs(a.y1 - b.y1)});
            const int tol = r.box_tol >= 0 ? r.box_tol : t.box;
            if (box > tol) box_fail = true;
            worst_box = std::max(worst_box, box);
            std::snprintf(line, sizeof(line),
                          "  %-26s tinta %4d,%-4d %4dx%-4d ref %4d,%-4d %4dx%-4d  borda %3d (até %2d)  dif_media %5.2f%s\n",
                          r.name.c_str(), a.x0, a.y0, a.x1 - a.x0, a.y1 - a.y0, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, box,
                          tol, rm.mae, box > tol ? "  <" : "");
            details += line;
        }
        const bool ok = m.mae <= t.mae && m.over <= t.over && !box_fail;
        failures += ok ? 0 : 1;
        std::snprintf(line, sizeof(line), "%-22s %-6s %9.2f %7.2f%% %5d %6d  %s (%.1f ms)\n", c.name.c_str(), c.group.c_str(),
                      m.mae, m.over * 100.0, m.max, worst_box, ok ? "ok" : "FORA", ms);
        report += line;
        std::fputs(line, stdout);
    }
    report += "\nregiões (a caixa da tinta: x,y largura x altura; borda = o maior deslocamento de uma borda, em pixels):\n";
    report += details;

    // As larguras.
    std::map<std::string, double> ref_w;
    if (read_measures(ref_dir + "/measures.tsv", ref_w)) {
        double worst = 0, worst_hd = 0, sum = 0;
        int n = 0, out_of = 0;
        std::string worst_name, worst_hd_name;
        std::string mlines = "\nlarguras (measure; a referência e esta):\n";
        for (const MeasureItem& it : items) {
            const auto r = ref_w.find(it.name);
            if (r == ref_w.end()) continue;
            const double w = measure_of(*canvas, it);
            const double rel = std::fabs(w - r->second) / std::max(1.0, r->second);
            const bool bad = it.tol > 0.0f && rel > it.tol;
            out_of += bad ? 1 : 0;
            std::snprintf(line, sizeof(line), "  %-40s %9.3f %9.3f %+6.2f%% (até %s)%s\n", it.name.c_str(), r->second, w,
                          (w - r->second) / std::max(1.0, r->second) * 100.0,
                          it.tol > 0.0f ? (std::to_string(int(it.tol * 100.0f + 0.5f)) + "%").c_str() : "só informada",
                          bad ? "  <" : "");
            mlines += line;
            if (it.style.face == ui::Face::Mono && !it.hd) continue;  // outra fonte: fora da média e do pior
            if (it.hd) {
                if (rel > worst_hd) {
                    worst_hd = rel;
                    worst_hd_name = it.name;
                }
            } else {
                sum += rel;
                ++n;
                if (rel > worst) {
                    worst = rel;
                    worst_name = it.name;
                }
            }
        }
        std::snprintf(line, sizeof(line),
                      "larguras: menus Modern (Rajdhani) %d textos, média %.2f%%, pior %.2f%% (%s); teclas da camada HD pior "
                      "%.2f%% (%s); %d fora da tolerância\n",
                      n, n > 0 ? sum / n * 100.0 : 0.0, worst * 100.0, worst_name.c_str(), worst_hd * 100.0,
                      worst_hd_name.c_str(), out_of);
        report += line;
        std::fputs(line, stdout);
        report += mlines;
        if (out_of > 0) ++failures;
    } else {
        report += "sem measures.tsv nas referências\n";
        ++failures;
    }
    if (do_bench) bench(hd_dir, report);
    FILE* f = fopen_narrow((dest + "/report.txt").c_str(), "wb");
    if (f != nullptr) {
        std::fputs(report.c_str(), f);
        std::fclose(f);
    }
    std::printf("%d caso(s) fora da tolerância\n", failures);
    return strict && failures > 0 ? 1 : 0;
}
