// nf_driving.exe: a ferramenta de desenvolvimento das missões de carro (docs/driving.md seção 10).
//
//   nf_driving --data <pasta com DRIVING\> --viv MIS01.VIV --model vanquish [--yaw 0.6] [--pitch 0.25] [--dist 1.0]
//              [--width 1920 --height 1080] [--screenshot out\shots\drv\vanquish.png] [--no-spec] [--no-refl]
//              [--fallback off]                      um carro girando numa janela, ou uma captura (PNG + .txt)
//   nf_driving --check --data <pasta>                 todos os carros e personagens de todas as pistas: o que carrega
//   nf_driving --census --data <pasta>                os 115 render methods: famílias, microprogramas distintos
//
// Sem nada do sistema: o ponto de entrada é o do backend (platform/entry_win32.cpp), a janela e o renderizador vêm de
// platform/window.h e drv_renderer.h, o relógio e o console de platform/os.h.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platform/crt_compat.h"
#include "platform/entry.h"
#include "platform/os.h"

#include "driving/bigf.h"
#include "platform/arena.h"
#include "driving/car_model.h"
#include "driving/carp.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_model.h"
#include "driving/eagl_state.h"
#include "driving/render_method.h"
#include "driving_app/drv_renderer.h"
#include "platform/window.h"

namespace {

struct Args {
    std::string data = ".";
    std::string viv = "MIS01.VIV";
    std::string model = "vanquish";
    std::string screenshot;
    int width = 1280, height = 720;
    float yaw = 0.6f, pitch = 0.22f, dist = 1.0f, fov = 40.0f;
    bool check = false, census = false, spec = true, refl = true, fallback = true, spin = true;
    std::string adapter;
    int part = -1;  // só esta peça (depuração)
    bool verify = false;  // roda a família no interpretador da VU1 e compara (mat_car_verify.h)
    int bench_frames = 0;  // com --screenshot: mede tantos quadros fora da tela antes da captura
    std::string check_viv;  // --check só neste arquivo
    int view = 1;   // a lista de desenho (vd#n do CARP; 0: todas as instâncias)
};

std::wstring widen(const std::string& s) { return nf::os::from_utf8(s); }

void console() { nf::os::attach_parent_console(); }

// A conferência contra o microprograma original (mat_car_verify.h) numa linha.
std::string verify_line(const drv::CarVerifyStats& v) {
    char b[512];
    std::snprintf(b, sizeof b,
                  "conferencia lotes %d falhas %d sem_pacote %d vertices %d stq_dif %d (max %u ulps, %.2g) cor_dif %d "
                  "(max %d) xyz_dif %d (max xy %d z %d) nevoa_dif %d adc_recorte %d adc_a_mais %d%s%s",
                  v.batches, v.failed_runs, v.missing_kicks, v.vertices, v.stq_diff, v.stq_max_ulps,
                  double(v.stq_max_abs), v.rgba_diff, v.rgba_max, v.xyz_diff, v.xyz_max, v.z_max, v.fog_diff, v.adc_clip,
                  v.adc_extra, v.first_error.empty() ? "" : " | ", v.first_error.c_str());
    return b;
}

// O relatório de um carro (linhas "chave valor"), também gravado junto da captura.
std::string report(const drv::CarModel& car, const Args& a, const drv::DrvRenderStats* rs) {
    const drv::CarModelStats& s = car.stats();
    std::string out;
    char b[512];
    std::snprintf(b, sizeof b, "carro %s arquivo %s pecas %d grupos %d primitivas %d lotes %d vertices %d triangulos %d\n",
                  a.model.c_str(), a.viv.c_str(), s.models, s.groups, s.geoprims, s.batches, s.vertices, s.triangles);
    out += b;
    std::snprintf(b, sizeof b, "caixa x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f\n", s.bbox_min[0], s.bbox_max[0],
                  s.bbox_min[1], s.bbox_max[1], s.bbox_min[2], s.bbox_max[2]);
    out += b;
    out += "metodos";
    for (const auto& m : s.method_batches) {
        const bool family = drv::car_method_layout(m.first.c_str()) != nullptr;
        std::snprintf(b, sizeof b, " %s=%d/%d%s", m.first.c_str(), m.second, s.method_triangles.at(m.first),
                      family ? "" : "(substituto)");
        out += b;
    }
    out += "\n";
    std::snprintf(b, sizeof b,
                  "substitutos lotes %d triangulos %d | desenhos %zu texturas %zu | carregador mensagens %d "
                  "runtime_alloc_nao_portados %d | vif erros %d | ops_nao_portados %d | trocas_de_microprograma %d\n",
                  s.fallback_batches, s.fallback_triangles, car.draws().size(), car.textures().size(), s.loader_messages,
                  s.runtime_alloc_unported, s.vif_errors, s.unported_ops, s.microcode_switches);
    out += b;
    std::set<int> used;
    for (const drv::CarDraw& d : car.draws())
        for (int t : d.texture)
            if (t >= 0) used.insert(t);
    out += "texturas_usadas";
    for (int t : used) {
        std::snprintf(b, sizeof b, " %s(%s %dx%d)", car.textures()[size_t(t)].name.c_str(),
                      car.textures()[size_t(t)].source.c_str(), car.textures()[size_t(t)].width,
                      car.textures()[size_t(t)].height);
        out += b;
    }
    out += "\n";
    for (size_t i = 0; i < car.parts().size(); ++i) {
        const drv::CarPart& p = car.parts()[i];
        std::snprintf(b, sizeof b, "peca %zu %s grupos %d primitivas %d vertices %u caixa %.3f,%.3f,%.3f..%.3f,%.3f,%.3f\n",
                      i, p.name.c_str(), p.groups, p.geoprims, p.vertex_count, p.bbox_min[0], p.bbox_min[1],
                      p.bbox_min[2], p.bbox_max[0], p.bbox_max[1], p.bbox_max[2]);
        out += b;
    }
    std::snprintf(b, sizeof b, "instancias %d sem_modelo %d pecas_sem_instancia %d vista %d", car.stats().instances,
                  car.stats().instances_without_model, car.stats().parts_without_instance, a.view);
    out += b;
    for (int v = 1; v < car.view_count(); ++v) {
        std::snprintf(b, sizeof b, " vd#%04x=%zu", v, car.view_draw_list(unsigned(v)).size());
        out += b;
    }
    out += "\n";
    for (size_t i = 0; i < car.instances().size(); ++i) {
        const drv::CarInstance& in = car.instances()[i];
        std::snprintf(b, sizeof b, "instancia %zu peca %d as#%04x pos %.3f,%.3f,%.3f x %.2f,%.2f,%.2f bandeiras %08x %08x\n", i,
                      in.part, in.piece & 0xFFFF, in.matrix[12], in.matrix[13], in.matrix[14], in.matrix[0], in.matrix[1],
                      in.matrix[2], in.flags, in.extra[1]);
        out += b;
    }
    if (rs) {
        std::snprintf(b, sizeof b,
                      "desenho instancias %d chamadas %d passadas1 %d passadas2 %d passadas3 %d substitutos %d | camera "
                      "giro %.3f inclinacao %.3f distancia %.3f fov %.1f | %dx%d | adaptador %s\n",
                      rs->instances, rs->draw_calls, rs->pass_calls[0], rs->pass_calls[1], rs->pass_calls[2],
                      rs->fallback_calls, a.yaw, a.pitch, a.dist, a.fov, a.width, a.height, rs->adapter.c_str());
        out += b;
    }
    if (a.verify) out += verify_line(car.verify_stats()) + "\n";
    for (const std::string& m : car.messages()) out += "mensagem " + m + "\n";
    // Um desenho por linha: método, peça, triângulos, as texturas e o estado do GS de cada passada.
    for (size_t i = 0; i < car.draws().size(); ++i) {
        const drv::CarDraw& d = car.draws()[i];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (uint32_t k = 0; k < d.index_count; ++k) {
            const drv::CarGpuVertex& v = car.vertices()[car.indices()[d.first_index + k]];
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], v.pos[c]);
                hi[c] = std::max(hi[c], v.pos[c]);
            }
        }
        std::snprintf(b, sizeof b, "desenho %zu %s peca %d tris %u props %.2f %.2f %.2f %.2f caixa %.2f,%.2f,%.2f..%.2f,%.2f,%.2f",
                      i, d.method.c_str(), d.part, d.index_count / 3, d.props[0], d.props[1], d.props[2], d.props[3],
                      lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        out += b;
        for (int p = 0; p < 3; ++p) {
            if (!((d.passes >> p) & 1)) continue;
            const drv::eagl::GsRegs& g = d.pass[p];
            const char* tn = d.texture[p] >= 0 ? car.textures()[size_t(d.texture[p])].name.c_str() : "-";
            std::snprintf(b, sizeof b, " | p%d tex %s prim %03llx test %05llx alpha %02llx tfx %d tcc %d clamp %llx", p + 1,
                          tn, (unsigned long long)g.prim, (unsigned long long)(g.test & 0x7ffff),
                          (unsigned long long)(g.alpha & 0xff), int((g.tex0 >> 35) & 3), int((g.tex0 >> 34) & 1),
                          (unsigned long long)(g.clamp & 0xf));
            out += b;
        }
        out += "\n";
    }
    return out;
}

// O relatório sem as linhas por peça e por desenho (essas ficam só no .txt).
std::string summary(const std::string& rep) {
    std::string out;
    size_t at = 0;
    while (at < rep.size()) {
        size_t end = rep.find('\n', at);
        if (end == std::string::npos) end = rep.size();
        const std::string line = rep.substr(at, end - at);
        const bool detail = line.rfind("peca ", 0) == 0 || line.rfind("instancia ", 0) == 0 ||
                            (line.rfind("desenho ", 0) == 0 && line.size() > 8 && line[8] >= '0' && line[8] <= '9');
        if (!detail) out += line + "\n";
        at = end + 1;
    }
    return out;
}

// O disco do jogador (--data: a pasta com DRIVING\\, uma imagem .iso, uma unidade; nf::open_disc).
std::shared_ptr<nf::DiscSource> open_data(const Args& a) {
    std::string error;
    std::shared_ptr<nf::DiscSource> disc = nf::open_disc(std::wstring(a.data.begin(), a.data.end()), &error);
    if (disc == nullptr) std::fprintf(stderr, "não abri os dados em %s: %s\n", a.data.c_str(), error.c_str());
    return disc;
}

int run_census(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    drv::Bigf misc;
    if (disc == nullptr || !misc.open(*disc, "DRIVING/MISC.VIV")) {
        std::fprintf(stderr, "não abri MISC.VIV em %s\n", a.data.c_str());
        return 1;
    }
    drv::eagl::reset_registries();
    drv::eagl::register_eagl_types();
    std::vector<drv::RenderMethodInfo> all;
    std::vector<std::vector<uint8_t>> keep;
    std::vector<std::unique_ptr<drv::eagl::DynamicLoader>> loaders;
    for (const char* obj : {"eaglrm", "bondrm"}) {
        const int idx = misc.find(std::string("data\\render\\") + obj + ".o");
        std::vector<uint8_t> b;
        misc.read(idx, b);
        keep.push_back(std::vector<uint8_t>(b.size() + 16));
        uint8_t* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(keep.back().data()) + 15) & ~uintptr_t(15));
        std::memcpy(p, b.data(), b.size());
        loaders.push_back(std::make_unique<drv::eagl::DynamicLoader>(p, uint32_t(b.size()), nullptr));
        auto c = drv::render_methods_census(*loaders.back(), obj);
        all.insert(all.end(), c.begin(), c.end());
    }
    std::map<uint64_t, std::vector<std::string>> by_code;
    std::map<std::string, std::vector<const drv::RenderMethodInfo*>> by_family;
    for (const auto& m : all) {
        by_code[m.code_hash].push_back(m.name);
        by_family[m.family].push_back(&m);
    }
    std::printf("render methods %zu, microprogramas distintos %zu\n", all.size(), by_code.size());
    for (const auto& f : by_family) {
        std::set<uint64_t> codes;
        int instr = 0;
        for (const auto* m : f.second) {
            if (codes.insert(m->code_hash).second) instr += m->instructions;
        }
        std::printf("familia %-22s metodos %3zu microprogramas %3zu instrucoes_distintas %5d:", f.first.c_str(),
                    f.second.size(), codes.size(), instr);
        for (const auto* m : f.second) std::printf(" %s", m->name.c_str());
        std::printf("\n");
    }
    for (const auto& m : all) {
        std::printf("metodo %-36s %-6s instr %4d max %3d xgkick %2d recorte %d efu %d pele %d familia %-20s abs",
                    m.name.c_str(), m.object.c_str(), m.instructions, m.max_verts, m.xgkicks, m.clip ? 1 : 0,
                    m.efu ? 1 : 0, m.skin ? 1 : 0, m.family.c_str());
        for (int r : m.abs_reads) std::printf(" %x", r);
        std::printf(" | parametros");
        for (const auto& p : m.params) std::printf(" %s", p.c_str());
        const auto& same = by_code[m.code_hash];
        if (same.size() > 1) {
            std::printf(" | mesmo_codigo");
            for (const auto& s : same)
                if (s != m.name) std::printf(" %s", s.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

int run_check(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    std::vector<std::string> vivs;
    if (disc != nullptr) {
        // Os .VIV de DRIVING\\, sem o diretório, na ordem do disco (a do FindFirstFile numa pasta do NTFS: por nome).
        for (const nf::DiscEntry& e : disc->list("DRIVING")) {
            std::string name = e.path.substr(e.path.find_last_of('/') + 1);
            const size_t semi = name.find(';');
            if (semi != std::string::npos) name.resize(semi);
            if (name.size() > 4 && nf::str_icmp(name.c_str() + name.size() - 4, ".VIV") == 0) vivs.push_back(name);
        }
    }
    if (vivs.empty()) {
        std::fprintf(stderr, "sem arquivos .VIV em %s\\DRIVING\n", a.data.c_str());
        return 1;
    }
    int ok = 0, fail = 0;
    drv::CarVerifyStats total;
    std::map<std::string, int> methods, fallback;
    for (const std::string& viv : vivs) {
        if (!a.check_viv.empty() && nf::str_icmp(viv.c_str(), a.check_viv.c_str()) != 0) continue;
        drv::Bigf arc;
        if (!arc.open(*disc, "DRIVING/" + viv)) continue;
        for (const drv::BigfEntry& e : arc.entries()) {
            std::string lower = e.name;
            for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
            if (lower.find("data\\car\\model\\") == std::string::npos || lower.size() < 4 ||
                lower.compare(lower.size() - 4, 4, ".crp") != 0)
                continue;
            const std::string name = drv::bigf_base_name(e.name);
            std::fflush(stdout);
            drv::CarModel car;
            car.set_verify(a.verify);
            std::string err;
            if (!car.load(*disc, viv, name, err)) {
                std::printf("FALHOU %s %s: %s\n", viv.c_str(), name.c_str(), err.c_str());
                ++fail;
                continue;
            }
            ++ok;
            const drv::CarModelStats& s = car.stats();
            std::printf("ok %-10s %-22s pecas %3d instancias %3d primitivas %4d lotes %5d triangulos %6d substitutos "
                        "%4d carregador %d vif %d ops %d nulos %d memoria %u MB\n",
                        viv.c_str(), name.c_str(), s.models, s.instances, s.geoprims, s.batches, s.triangles,
                        s.fallback_batches, s.loader_messages, s.vif_errors, s.unported_ops, s.dma_null_sources,
                        unsigned(nf::os::private_memory() >> 20));
            if (a.verify) {
                // Um carro com referências não resolvidas (lidas como zeros) fica fora da soma: o lixo não diz nada.
                std::printf("  %s%s\n", verify_line(car.verify_stats()).c_str(),
                            s.dma_null_sources ? " (fora da soma: dados nulos)" : "");
            }
            if (a.verify && !s.dma_null_sources) {
                const drv::CarVerifyStats& v = car.verify_stats();
                total.batches += v.batches;
                total.failed_runs += v.failed_runs;
                total.missing_kicks += v.missing_kicks;
                total.vertices += v.vertices;
                total.stq_diff += v.stq_diff;
                total.rgba_diff += v.rgba_diff;
                total.xyz_diff += v.xyz_diff;
                total.fog_diff += v.fog_diff;
                total.adc_clip += v.adc_clip;
                total.adc_extra += v.adc_extra;
                total.stq_max_ulps = std::max(total.stq_max_ulps, v.stq_max_ulps);
                total.stq_max_abs = std::max(total.stq_max_abs, v.stq_max_abs);
                total.rgba_max = std::max(total.rgba_max, v.rgba_max);
                total.xyz_max = std::max(total.xyz_max, v.xyz_max);
                total.z_max = std::max(total.z_max, v.z_max);
                if (total.first_error.empty()) total.first_error = v.first_error;
            }
            for (const auto& m : s.method_batches) {
                methods[m.first] += m.second;
                if (!drv::car_method_layout(m.first.c_str())) fallback[m.first] += m.second;
            }
        }
    }
    std::printf("carros carregados %d, falhas %d\n", ok, fail);
    if (a.verify) std::printf("%s (todos)\n", verify_line(total).c_str());
    std::printf("metodos (lotes):");
    for (const auto& m : methods) std::printf(" %s=%d%s", m.first.c_str(), m.second, fallback.count(m.first) ? "*" : "");
    std::printf("\n(* fora da família reproduzida: desenhados pelo substituto)\n");
    return fail ? 2 : 0;
}

bool parse(int argc, wchar_t** argv, Args& a) {
    auto narrow = [](const wchar_t* w) { return nf::os::to_utf8(w); };
    for (int i = 1; i < argc; ++i) {
        const std::string k = narrow(argv[i]);
        auto next = [&]() { return i + 1 < argc ? narrow(argv[++i]) : std::string(); };
        if (k == "--data") a.data = next();
        else if (k == "--viv") a.viv = next();
        else if (k == "--model") a.model = next();
        else if (k == "--screenshot") a.screenshot = next();
        else if (k == "--width") a.width = std::stoi(next());
        else if (k == "--height") a.height = std::stoi(next());
        else if (k == "--yaw") a.yaw = std::stof(next());
        else if (k == "--pitch") a.pitch = std::stof(next());
        else if (k == "--dist") a.dist = std::stof(next());
        else if (k == "--fov") a.fov = std::stof(next());
        else if (k == "--check") a.check = true;
        else if (k == "--check-viv") {
            a.check = true;
            a.check_viv = next();
        }
        else if (k == "--census") a.census = true;
        else if (k == "--no-spec") a.spec = false;
        else if (k == "--no-refl") a.refl = false;
        else if (k == "--no-fallback") a.fallback = false;
        else if (k == "--still") a.spin = false;
        else if (k == "--adapter") a.adapter = next();
        else if (k == "--part") a.part = std::stoi(next());
        else if (k == "--verify") a.verify = true;
        else if (k == "--bench-frames") a.bench_frames = std::stoi(next());
        else if (k == "--view") a.view = std::stoi(next());
        else {
            std::fprintf(stderr, "opção desconhecida: %s\n", k.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int nf_app_main(int argc, wchar_t** argv) {
    console();
    Args a;
    if (!parse(argc, argv, a)) return 1;
    if (a.census) return run_census(a);
    if (a.check) return run_check(a);

    drv::CarModel car;
    car.set_verify(a.verify);
    std::string err;
    if (!car.load(a.data, a.viv, a.model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.spec = a.spec;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.only_part = a.part;
    ro.view = a.view;
    drv::DrvRenderer r;
    if (!a.screenshot.empty()) {
        if (!r.init(nullptr, ro, err) || !r.upload(car, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        if (a.bench_frames > 0) {
            // Quadros girando, fora da tela, até a GPU terminar: o custo médio por quadro (sem Present).
            r.draw(car, a.yaw, a.pitch, a.dist);
            r.finish();
            const uint64_t f = nf::os::perf_frequency();
            const uint64_t t0 = nf::os::perf_counter();
            for (int i = 0; i < a.bench_frames; ++i) r.draw(car, a.yaw + 0.01f * float(i), a.pitch, a.dist);
            r.finish();
            const uint64_t t1 = nf::os::perf_counter();
            const double ms = 1000.0 * double(int64_t(t1 - t0)) / double(int64_t(f)) / a.bench_frames;
            std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f | %s\n", a.bench_frames, ms, 1000.0 / ms,
                        r.stats().adapter.c_str());
        }
        r.draw(car, a.yaw, a.pitch, a.dist);
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        const std::string rep = report(car, a, &r.stats());
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        std::printf("%s", summary(rep).c_str());
        return 0;
    }
    nf::Window win(L"nf_driving — " + widen(a.model), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(car, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s", summary(report(car, a, nullptr)).c_str());
    const uint64_t freq = nf::os::perf_frequency();
    uint64_t t0 = nf::os::perf_counter();
    float yaw = a.yaw;
    while (win.pump()) {
        const uint64_t t1 = nf::os::perf_counter();
        const float dt = float(double(int64_t(t1 - t0)) / double(int64_t(freq)));
        t0 = t1;
        if (a.spin) yaw += dt * 0.6f;
        if (win.resized()) r.resize(win.width(), win.height());
        r.draw(car, yaw, a.pitch, a.dist);
        r.present();
    }
    return 0;
}

