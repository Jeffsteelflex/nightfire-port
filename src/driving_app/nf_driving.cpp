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
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platform/crt_compat.h"
#include "platform/entry.h"
#include "platform/fmath.h"
#include "platform/gamepad.h"
#include "platform/os.h"
#include "platform/ps2_float.h"

#include "driving/bigf.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "platform/arena.h"
#include "driving/car_model.h"
#include "driving/carp.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_model.h"
#include "driving/eagl_state.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/input.h"
#include "driving/game/scheduler.h"
#include "driving/physics/collide.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/render_method.h"
#include "driving/track.h"
#include "driving_app/drv_renderer.h"
#include "platform/keys.h"
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
    std::string track_census;  // --track-census <pista>: os métodos dos modelos da pista
    std::string track;         // --track <pista>: a pista com o carro
    std::string car;           // --car <carro> (com --track; vazio: sem carro)
    bool viv_given = false;
    bool cam_given = false;
    float cam[5] = {0, 0, 0, 0, 0};          // --cam x,y,z,giro,inclinação (radianos)
    float car_pos[4] = {0, 0, 0, 0};         // --car-pos x,y,z,giro
    bool no_sky = false;                     // --no-sky (depuração)
    bool probe = false;                      // --probe: as instâncias em volta da câmera (depuração)
    bool drive = false;                      // --drive: o carro dirigível (com --track e --car)
    std::string drive_input;                 // --drive-input "W:2 W+A:1.5 ...": as teclas por segundos (captura)
    float seconds = 0;                       // --seconds: o tempo simulado antes da captura (0: o do roteiro)
    bool car_pos_given = false;
};

// "a,b,c" em n floats.
bool parse_floats(const std::string& s, float* out, int n) {
    const char* c = s.c_str();
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(c, &end);
        if (end == c) return false;
        c = end;
        if (*c == ',') ++c;
    }
    return true;
}

std::wstring widen(const std::string& s) { return nf::os::from_utf8(s); }

void console() { nf::os::attach_parent_console(); }

// A conferência contra o microprograma original (mat_car_verify.h) numa linha.
std::string verify_line(const drv::CarVerifyStats& v) {
    char b[512];
    std::snprintf(b, sizeof b,
                  "conferencia lotes %d falhas %d sem_pacote %d vertices %d stq_dif %d (max %u ulps, %.2g) cor_dif %d "
                  "(max %d) xyz_dif %d (max xy %d z %d) nevoa_dif %d adc_recorte %d adc_a_mais %d adc_costas %d variantes %d%s%s",
                  v.batches, v.failed_runs, v.missing_kicks, v.vertices, v.stq_diff, v.stq_max_ulps,
                  double(v.stq_max_abs), v.rgba_diff, v.rgba_max, v.xyz_diff, v.xyz_max, v.z_max, v.fog_diff, v.adc_clip,
                  v.adc_extra, v.adc_back, v.variant_batches, v.first_error.empty() ? "" : " | ", v.first_error.c_str());
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
        const bool family =
            drv::car_method_layout(m.first.c_str()) != nullptr || drv::bond_method_layout(m.first.c_str()) != nullptr;
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
                      "desenho instancias %d chamadas %d passadas1 %d passadas2 %d passadas3 %d passadas4 %d substitutos "
                      "%d | camera giro %.3f inclinacao %.3f distancia %.3f fov %.1f | %dx%d | adaptador %s\n",
                      rs->instances, rs->draw_calls, rs->pass_calls[0], rs->pass_calls[1], rs->pass_calls[2],
                      rs->pass_calls[3], rs->fallback_calls, a.yaw, a.pitch, a.dist, a.fov, a.width, a.height, rs->adapter.c_str());
        out += b;
    }
    if (a.verify) {
        out += verify_line(car.verify_stats()) + "\n";
        if (car.bond_verify_stats().batches) out += "mundo " + verify_line(car.bond_verify_stats()) + "\n";
    }
    for (const std::string& m : car.messages()) out += "mensagem " + m + "\n";
    // Um desenho por linha: método, peça, triângulos, as texturas e o estado do GS de cada passada.
    for (size_t i = 0; i < car.draws().size(); ++i) {
        const drv::CarDraw& d = car.draws()[i];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (uint32_t k = 0; k < d.index_count; ++k) {
            const float* pos = d.family == 1 ? car.bond_vertices()[car.bond_indices()[d.first_index + k]].pos
                                             : car.vertices()[car.indices()[d.first_index + k]].pos;
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], pos[c]);
                hi[c] = std::max(hi[c], pos[c]);
            }
        }
        std::snprintf(b, sizeof b, "desenho %zu %s peca %d tris %u props %.2f %.2f %.2f %.2f caixa %.2f,%.2f,%.2f..%.2f,%.2f,%.2f",
                      i, d.method.c_str(), d.part, d.index_count / 3, d.props[0], d.props[1], d.props[2], d.props[3],
                      lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        out += b;
        for (int p = 0; p < drv::kCarMaxPasses; ++p) {
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
                total.adc_back += v.adc_back;
                total.variant_batches += v.variant_batches;
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
    // As 8 pistas (a tabela do main do DRIVING; docs/driving.md seção 1): o CARP resolvido, os modelos, as instâncias e
    // o mundo de colisão ('CDat').
    if (a.check_viv.empty()) {
        static const char* const kTracks[] = {"paris_mis01", "uw_mis11",    "junglea_mis13a", "jungleb_mis13b",
                                              "snow1a_mis3", "snow2a_mis4", "junglec_mis13c", "snow2a_race"};
        int tracks_ok = 0;
        for (const char* t : kTracks) {
            drv::Track track;
            std::string e;
            const bool loaded = track.load(*disc, t, std::string(), e);
            drv::TrackWorld world;
            const bool ground = loaded && world.init(track.root(), &e);
            if (!loaded || !ground) {
                std::printf("pista %s: FALHA %s\n", t, e.c_str());
                ++fail;
                continue;
            }
            ++tracks_ok;
            const drv::TrackStats& ts = track.stats();
            const drv::CarModelStats& ms = track.models().stats();
            uint32_t cells = 0;
            for (uint32_t c = 0; c < world.grid().rows * world.grid().columns; ++c) cells += world.node(c) != nullptr;
            std::printf("pista %-15s %s: modelos %d lotes %d substitutos %d instancias %d artigos %d | colisao %u "
                        "instancias, %u celulas com no\n",
                        t, track.viv().c_str(), ms.models, ms.batches, ms.fallback_batches, ts.instances, ts.articles,
                        world.instance_count(), cells);
        }
        std::printf("pistas carregadas %d de 8\n", tracks_ok);
    }
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
        else if (k == "--viv") {
            a.viv = next();
            a.viv_given = true;
        }
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
        else if (k == "--track-census") a.track_census = next();
        else if (k == "--track") a.track = next();
        else if (k == "--car") a.car = next();
        else if (k == "--cam") a.cam_given = parse_floats(next(), a.cam, 5);
        else if (k == "--car-pos") a.car_pos_given = parse_floats(next(), a.car_pos, 4);
        else if (k == "--drive") a.drive = true;
        else if (k == "--drive-input") a.drive_input = next();
        else if (k == "--seconds") a.seconds = std::stof(next());
        else if (k == "--no-sky") a.no_sky = true;
        else if (k == "--probe") a.probe = true;
        else {
            std::fprintf(stderr, "opção desconhecida: %s\n", k.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

// O valor de uma chave "NOME=valor" de um .tun (as linhas terminam em "\r\n"; os nomes registrados têm espaços que o
// arquivo não tem: "fog START" é "fogSTART").
bool tun_floats(const std::string& text, const std::string& key, float* out, int n) {
    const size_t p = text.find(key + "=");
    if (p == std::string::npos) return false;
    const char* c = text.c_str() + p + key.size() + 1;
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(c, &end);
        if (end == c) return i > 0;
        c = end;
        if (*c == ',') ++c;
    }
    return true;
}

// [driving] 0x001ac668 RFog::RFog e 0x001ac978 RFog::SetFogParams: o "Render:Fog" da pista (fog START, fog END, fog
// mode, fog Density, Fog Colour) no FogData da VU (0x48): {início, 0, fim x escala (1), densidade x 255 / (fim -
// início)}; a cor ARGB do arquivo vai para o FOGCOL. Os padrões do RFog: início 1, fim 5000, modo 3, densidade 0, cor
// 0x80386885.
void track_fog(const std::string& tun, drv::CarConstants& k, float color[3]) {
    float start = 1.0f, end = 5000.0f, density = 0.0f, argb[4] = {128, 56, 104, 133};
    tun_floats(tun, "fogSTART", &start, 1);
    tun_floats(tun, "fogEND", &end, 1);
    tun_floats(tun, "fogDensity", &density, 1);
    tun_floats(tun, "FogColour", argb, 4);
    const float fog_end = end * 1.0f;  // CalcFogEnd: o fim x a escala (+0x28, 1 fora do SetFogScale)
    k.fog[0] = start;
    k.fog[1] = 0.0f;
    k.fog[2] = fog_end;
    k.fog[3] = (density * 255.0f) / (fog_end - start);
    color[0] = argb[1];
    color[1] = argb[2];
    color[2] = argb[3];
}

std::string read_entry(drv::Bigf& arc, const std::string& key) {
    const int idx = arc.find(key);
    if (idx < 0) return std::string();
    std::vector<uint8_t> b;
    arc.read(idx, b);
    return std::string(b.begin(), b.end());
}

// A pista com um carro: a cena que o WRender e o RSky desenham (track.h) e o carro numa posição, vistos de uma câmera.
int run_track(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    drv::Track track;
    track.set_verify(a.verify);
    std::string err;
    const uint64_t freq = nf::os::perf_frequency();
    const uint64_t t0 = nf::os::perf_counter();
    if (!track.load(*disc, a.track, a.viv_given ? a.viv : std::string(), err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    const double load_ms = 1000.0 * double(int64_t(nf::os::perf_counter() - t0)) / double(int64_t(freq));
    drv::CarModel car;
    const bool with_car = !a.car.empty();
    if (with_car && !car.load(*disc, track.viv(), a.car, err)) {
        std::fprintf(stderr, "nf_driving: o carro: %s\n", err.c_str());
        return 1;
    }
    drv::Bigf arc;
    arc.open(*disc, "DRIVING/" + track.viv());
    drv::DrvScene scene;
    track_fog(read_entry(arc, "data\\tuning\\Render\\Fog\\" + a.track + ".tun"), scene.k, scene.fog_color);
    for (int c = 0; c < 3; ++c) scene.clear[c] = scene.fog_color[c] / 255.0f;
    if (with_car) {
        // As luzes do carro (as do .tun da pista, car_model.h) e a névoa da pista.
        const drv::CarConstants& ck = car.constants();
        std::memcpy(scene.k.light_dir_t, ck.light_dir_t, sizeof ck.light_dir_t);
        std::memcpy(scene.k.light_col, ck.light_col, sizeof ck.light_col);
        std::memcpy(scene.k.ambient, ck.ambient, sizeof ck.ambient);
        std::memcpy(scene.k.refl_info2, ck.refl_info2, sizeof ck.refl_info2);
        std::memcpy(car.constants().fog, scene.k.fog, sizeof scene.k.fog);
    }
    float lo[3], hi[3];
    track.bounds(lo, hi);
    float cam[5] = {0.5f * (lo[0] + hi[0]), hi[1] + 50.0f, 0.5f * (lo[2] + hi[2]), 0.0f, -0.6f};
    if (a.cam_given) std::memcpy(cam, a.cam, sizeof cam);
    const float eye[3] = {cam[0], cam[1], cam[2]};
    const float dir[3] = {nf::fm::sin(cam[3]) * nf::fm::cos(cam[4]), nf::fm::sin(cam[4]), nf::fm::cos(cam[3]) * nf::fm::cos(cam[4])};
    const float at[3] = {eye[0] + dir[0], eye[1] + dir[1], eye[2] + dir[2]};
    drv::drv_look_at(eye, at, scene.view);
    const int w = a.width, h = a.height;
    drv::drv_perspective(a.fov, float(w) / float(h), 0.17f, 3000.0f, scene.proj);  // perto 0,17, longe 3000 (a câmera)
    scene.eye[0] = eye[0];
    scene.eye[1] = eye[1];
    scene.eye[2] = eye[2];
    scene.spec_from_view = true;
    float vp[16];
    drv::drv_mat_mul(scene.view, scene.proj, vp);
    std::vector<drv::Track::Draw> draws;
    track.visible(vp, eye, draws);
    // A ordem do RRenderWorldCamera::DoRender: o céu (passada 4, levado ao olho, sem névoa nem luzes pontuais), as
    // passadas 3 e 2, os carros, a 1 e a 0 (do longe para o perto).
    auto push = [&](const drv::Track::Draw& d) {
        const drv::TrackInstance& inst = track.instances()[size_t(d.instance)];
        drv::DrvItem it;
        it.model = &track.models();
        it.part = d.part;
        std::memcpy(it.world, inst.matrix, sizeof it.world);
        const drv::TrackArticle& art = track.articles()[size_t(inst.article)];
        if (art.render_flags & 1) it.flags |= drv::DrvItem::kNoFog;
        if (d.pass == 4) {
            it.flags |= drv::DrvItem::kNoFog | drv::DrvItem::kNoPointLights | drv::DrvItem::kSky;
            it.world[12] = eye[0];
            it.world[13] = eye[1];
            it.world[14] = eye[2];
        }
        scene.items.push_back(it);
    };
    for (int pass : {4, 3, 2}) {
        if (pass == 4 && a.no_sky) continue;
        for (const auto& d : draws)
            if (d.pass == pass) push(d);
    }
    if (with_car) {
        float carm[16] = {nf::fm::cos(a.car_pos[3]), 0, -nf::fm::sin(a.car_pos[3]), 0, 0, 1, 0, 0,
                          nf::fm::sin(a.car_pos[3]), 0, nf::fm::cos(a.car_pos[3]), 0, a.car_pos[0], a.car_pos[1],
                          a.car_pos[2], 1};
        std::vector<uint16_t> list = car.view_draw_list(1);
        for (uint16_t ii : list) {
            if (ii >= car.instances().size() || car.instances()[ii].part < 0) continue;
            drv::DrvItem it;
            it.model = &car;
            it.part = car.instances()[ii].part;
            drv::drv_mat_mul(car.instances()[ii].matrix, carm, it.world);
            scene.items.push_back(it);
        }
    }
    for (int pass : {1, 0}) {
        std::vector<drv::Track::Draw> sorted;
        for (const auto& d : draws)
            if (d.pass == pass) sorted.push_back(d);
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const drv::Track::Draw& x, const drv::Track::Draw& y) { return x.distance > y.distance; });
        for (const auto& d : sorted) push(d);
    }

    drv::DrvRenderOptions ro;
    ro.width = w;
    ro.height = h;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    if (!r.init(nullptr, ro, err) || !r.upload(track.models(), err) || (with_car && !r.upload(car, err))) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    if (a.bench_frames > 0) {
        r.draw_scene(scene);
        r.finish();
        const uint64_t b0 = nf::os::perf_counter();
        for (int i = 0; i < a.bench_frames; ++i) r.draw_scene(scene);
        r.finish();
        const double ms = 1000.0 * double(int64_t(nf::os::perf_counter() - b0)) / double(int64_t(freq)) / a.bench_frames;
        std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f | %s\n", a.bench_frames, ms, 1000.0 / ms,
                    r.stats().adapter.c_str());
    }
    r.draw_scene(scene);
    const drv::TrackStats& ts = track.stats();
    const drv::CarModelStats& ms = track.models().stats();
    char b[1024];
    std::string rep;
    std::snprintf(b, sizeof b,
                  "pista %s arquivo %s carga_ms %.0f | modelos %d lotes %d triangulos %d substitutos %d | instancias %d "
                  "artigos %d lods %d lods_sem_modelo %d sem_artigo %d passadas %d/%d/%d/%d/%d\n",
                  track.name().c_str(), track.viv().c_str(), load_ms, ms.models, ms.batches, ms.triangles,
                  ms.fallback_batches, ts.instances, ts.articles, ts.lods, ts.lods_without_model,
                  ts.instances_without_article, ts.pass_count[0], ts.pass_count[1], ts.pass_count[2], ts.pass_count[3],
                  ts.pass_count[4]);
    rep += b;
    std::snprintf(b, sizeof b,
                  "camera %.2f %.2f %.2f giro %.3f inclinacao %.3f fov %.1f | visiveis %zu itens %zu | desenho chamadas %d "
                  "mundo %d substitutos %d | nevoa %.2f %.2f %.6f cor %.0f %.0f %.0f | caixa %.0f,%.0f,%.0f..%.0f,%.0f,%.0f "
                  "| %dx%d | adaptador %s\n",
                  cam[0], cam[1], cam[2], cam[3], cam[4], a.fov, draws.size(), scene.items.size(), r.stats().draw_calls,
                  r.stats().bond_calls, r.stats().fallback_calls, scene.k.fog[0], scene.k.fog[2], scene.k.fog[3],
                  scene.fog_color[0], scene.fog_color[1], scene.fog_color[2], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], w, h,
                  r.stats().adapter.c_str());
    rep += b;
    rep += "metodos";
    for (const auto& m : ms.method_batches) {
        const bool family =
            drv::car_method_layout(m.first.c_str()) != nullptr || drv::bond_method_layout(m.first.c_str()) != nullptr;
        std::snprintf(b, sizeof b, " %s=%d/%d%s", m.first.c_str(), m.second, ms.method_triangles.at(m.first),
                      family ? "" : "(substituto)");
        rep += b;
    }
    rep += "\n";
    if (a.verify) rep += "mundo " + verify_line(track.models().bond_verify_stats()) + "\n";
    // Depuração: as instâncias cuja caixa contém a câmera.
    if (a.probe) {
        for (size_t i = 0; i < track.instances().size(); ++i) {
            const drv::TrackInstance& inst = track.instances()[i];
            if (inst.article < 0) continue;
            const drv::TrackArticle& art = track.articles()[size_t(inst.article)];
            const int part = art.lods.empty() ? -1 : art.lods[0].part;
            if (part < 0) continue;
            const drv::CarPart& p = track.models().parts()[size_t(part)];
            // A câmera no espaço da instância (a matriz é rotação + translação).
            float d[3] = {eye[0] - inst.matrix[12], eye[1] - inst.matrix[13], eye[2] - inst.matrix[14]}, l[3];
            for (int c = 0; c < 3; ++c)
                l[c] = (d[0] * inst.matrix[c * 4] + d[1] * inst.matrix[c * 4 + 1] + d[2] * inst.matrix[c * 4 + 2]);
            bool in = true;
            for (int c = 0; c < 3; ++c) in = in && l[c] >= p.bbox_min[c] - 2 && l[c] <= p.bbox_max[c] + 2;
            if (!in) continue;
            std::snprintf(b, sizeof b,
                          "dentro instancia %zu %s bandeiras %02x passada %d pos %.1f,%.1f,%.1f raio %.1f peca %d caixa "
                          "%.0f,%.0f,%.0f..%.0f,%.0f,%.0f dims %08x\n",
                          i, art.name.c_str(), inst.flags, inst.pass, inst.matrix[12], inst.matrix[13], inst.matrix[14],
                          inst.radius, part, p.bbox_min[0], p.bbox_min[1], p.bbox_min[2], p.bbox_max[0], p.bbox_max[1],
                          p.bbox_max[2], inst.packed_dims);
            rep += b;
        }
    }
    // O céu (as peças das instâncias da passada 4): o estado do GS de cada desenho.
    for (const drv::TrackInstance& inst : track.instances()) {
        if (inst.pass != 4 || inst.article < 0) continue;
        for (const auto& lod : track.articles()[size_t(inst.article)].lods) {
            for (const drv::CarDraw& d : track.models().draws()) {
                if (d.part != lod.part) continue;
                const drv::eagl::GsRegs& g = d.pass[0];
                const drv::CarPart& pp = track.models().parts()[size_t(d.part)];
                std::snprintf(b, sizeof b,
                              "ceu peca %d %s tris %u test %05llx zbuf %llx alpha %02llx prim %03llx caixa "
                              "%.0f,%.0f,%.0f..%.0f,%.0f,%.0f instancia %.1f,%.1f,%.1f\n",
                              d.part, d.method.c_str(), d.index_count / 3, (unsigned long long)(g.test & 0x7ffff),
                              (unsigned long long)(g.zbuf >> 32), (unsigned long long)(g.alpha & 0xff),
                              (unsigned long long)g.prim, pp.bbox_min[0], pp.bbox_min[1], pp.bbox_min[2], pp.bbox_max[0],
                              pp.bbox_max[1], pp.bbox_max[2], inst.matrix[12], inst.matrix[13], inst.matrix[14]);
                rep += b;
            }
        }
    }
    std::printf("%s", rep.c_str());
    if (!a.screenshot.empty()) {
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- o carro dirigível (--drive)

// As teclas de um passo do roteiro (--drive-input): W acelera, S freia, A/D viram, SPACE é o freio de mão.
struct DriveKeys {
    bool gas = false, brake = false, left = false, right = false, hand = false;
};

DriveKeys parse_keys(const std::string& s) {
    DriveKeys k;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('+', p);
        if (e == std::string::npos) e = s.size();
        const std::string t = s.substr(p, e - p);
        if (t == "W" || t == "w" || t == "up") k.gas = true;
        else if (t == "S" || t == "s" || t == "down") k.brake = true;
        else if (t == "A" || t == "a" || t == "left") k.left = true;
        else if (t == "D" || t == "d" || t == "right") k.right = true;
        else if (t == "SPACE" || t == "space") k.hand = true;
        p = e + 1;
    }
    return k;
}

// O controle do PS2 que o teclado e o controle do PC fazem (só do port): o teclado aperta o X (W/seta acima: o acelerador
// do drivecfg.def), o quadrado (S/seta abaixo: o freio), a bola (espaço: o freio de mão) com a pressão inteira e põe o
// direcional esquerdo nos extremos (A/D, setas); o controle do PC entra pela posição dos botões, como o controle virtual
// do jogo a pé (A = X, B = bola, X = quadrado, Y = triângulo, LB/RB = L1/R1, LT/RT = L2/R2, os direcionais e o
// D-pad), com a pressão 255 nos botões e os gatilhos acima de 30.
drv::PadData pad_from(const DriveKeys& k, const nf::PadState* pc) {
    drv::PadData p;
    // Os bits: L2 0, R2 1, L1 2, R1 3, triângulo 4, bola 5, X 6, quadrado 7, Select 8, L3 9, R3 10, Start 11, cima 12,
    // direita 13, baixo 14, esquerda 15; as pressões: direita, esquerda, cima, baixo, triângulo, bola, X, quadrado, L1,
    // R1, L2, R2.
    auto press = [&](int bit, int pressure) {
        p.buttons |= uint16_t(1u << bit);
        if (pressure >= 0) p.pressure[pressure] = 255;
    };
    if (k.gas) press(6, 6);
    if (k.brake) press(7, 7);
    if (k.hand) press(5, 5);
    if (pc && pc->connected) {
        const uint16_t b = pc->buttons;
        if (b & nf::kPadA) press(6, 6);
        if (b & nf::kPadB) press(5, 5);
        if (b & nf::kPadX) press(7, 7);
        if (b & nf::kPadY) press(4, 4);
        if (b & nf::kPadLB) press(2, 8);
        if (b & nf::kPadRB) press(3, 9);
        if (pc->lt > nf::kPadTriggerThreshold) press(0, 10);
        if (pc->rt > nf::kPadTriggerThreshold) press(1, 11);
        if (b & nf::kPadBack) press(8, -1);
        if (b & nf::kPadLThumb) press(9, -1);
        if (b & nf::kPadRThumb) press(10, -1);
        if (b & nf::kPadStart) press(11, -1);
        if (b & nf::kPadDpadUp) press(12, 2);
        if (b & nf::kPadDpadRight) press(13, 0);
        if (b & nf::kPadDpadDown) press(14, 3);
        if (b & nf::kPadDpadLeft) press(15, 1);
        // Os direcionais: -32768..32767 para 0..255, o y do PS2 para baixo.
        auto byte = [](int v) { return uint8_t((v + 32768) >> 8); };
        p.lx = byte(pc->lx);
        p.ly = uint8_t(255 - byte(pc->ly));
        p.rx = byte(pc->rx);
        p.ry = uint8_t(255 - byte(pc->ry));
    }
    if (k.left != k.right) p.lx = k.left ? 0 : 255;
    return p;
}

// O IOModule do jogo (game/input.h): o PS2PadDevice, a tabela do data\Control\drivecfg.def e o SendGameMessages, que
// manda as ações para a fila do carro (o ActionQueueManager, que só tem a do carro aqui).
struct DriveInput {
    drv::InputMap map;
    drv::PadDevice dev;
    drv::IOModuleState io;
    uint64_t actions = 0;
    void feed(drv::BondCar& car, const drv::PadData& pad, int32_t now) {
        io.now = now;
        dev.Poll(pad);
        drv::SendGameMessages(io, dev, map, [&](int32_t action, int32_t, float value) {
            ++actions;
            car.push_action(action, value);
        });
        io.send_all = 0;  // o UpdateAllDevices zera depois de cada passagem
    }
};

float ee_add_one(float h) { return ps2::ee::add(h, 1.0f); }

// A matriz de desenho do carro: a orientação do corpo e a posição menos o "cima" vezes o deslocamento do render
// (RSceneObj::UpdatePosition, lido no nightfire-cxbx; o PS2 a conferir).
void car_render_matrix(const drv::RigidBody* rb, float offset, float m[16]) {
    const drv::RigidBodyInfo* info = rb->info;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) m[r * 4 + c] = c < 3 ? info->orient[r][c] : 0.0f;
    for (int c = 0; c < 3; ++c) m[12 + c] = rb->pos[c] - info->orient[1][c] * offset;
    m[15] = 1.0f;
}

int run_drive(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    const uint64_t freq = nf::os::perf_frequency();
    const uint64_t t0 = nf::os::perf_counter();
    drv::Track track;
    std::string err;
    if (!track.load(*disc, a.track, a.viv_given ? a.viv : std::string(), err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    const std::string car_name = a.car.empty() ? std::string("vanquish") : a.car;
    drv::CarModel model;
    if (!model.load(*disc, track.viv(), car_name, err)) {
        std::fprintf(stderr, "nf_driving: o carro: %s\n", err.c_str());
        return 1;
    }
    drv::Bigf arc;
    arc.open(*disc, "DRIVING/" + track.viv());
    drv::AttributeCollection attrs;
    if (!attrs.load(arc, "pvehicle", car_name, a.track, &err)) {
        std::fprintf(stderr, "nf_driving: os atributos: %s\n", err.c_str());
        return 1;
    }
    drv::CarDesc desc;
    if (!drv::read_car_desc(arc, car_name, desc, &err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    // Os bancos de ajuste da física (o default.tun de cada um) e o sistema de corpos rígidos.
    auto tun = [&](const std::string& bank) {
        std::string t = read_entry(arc, "data\\tuning\\Physics\\" + bank + "\\" + a.track + ".tun");
        return t.empty() ? read_entry(arc, "data\\tuning\\Physics\\" + bank + "\\default.tun") : t;
    };
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    const std::string rigid = tun("Rigid");
    drv::InitRigidBodySystem(a.track.c_str(), rigid.empty() ? nullptr : rigid.c_str());
    drv::reset_bond_car_tuning();
    const std::string physical = tun("Physical"), friction = tun("Friction");
    drv::load_bond_car_tuning(physical.c_str(), friction.c_str());
    drv::TrackWorld world;
    if (!world.init(track.root(), &err)) {
        std::fprintf(stderr, "nf_driving: o chão: %s\n", err.c_str());
        return 1;
    }
    drv::set_world(&world);
    drv::simulation_reset(60);
    // A largada (GameLoop_StartUp 0x176658): o ponto (padrão: a origem; --car-pos escolhe outro), y = a altura do chão
    // sob ele + 1 (sem chão, a altura fica 0: y = 1). A missão depois põe o carro na largada dela (as regras e os
    // gatilhos do SMissionManager, não portados).
    float start[3] = {a.car_pos[0], a.car_pos[1], a.car_pos[2]};
    float h = 0;
    const bool ground = world.world_height_at_point(start, h);
    start[1] = ee_add_one(h);
    const float dir[3] = {nf::fm::sin(a.car_pos[3]), 0.0f, nf::fm::cos(a.car_pos[3])};
    drv::BondCar* car = new drv::BondCar();
    drv::RigidBody* rb = drv::PBondCar_Construct(*car, 0, car_name, desc, attrs, dir, start);
    if (!rb) {
        std::fprintf(stderr, "nf_driving: sem vaga para o corpo do carro\n");
        return 1;
    }
    const float render_offset = drv::CarDesc_RenderOffset(desc);
    const double load_ms = 1000.0 * double(int64_t(nf::os::perf_counter() - t0)) / double(int64_t(freq));

    // A cena: o mundo (como run_track) e o carro na matriz do corpo.
    drv::DrvScene scene;
    track_fog(read_entry(arc, "data\\tuning\\Render\\Fog\\" + a.track + ".tun"), scene.k, scene.fog_color);
    for (int c = 0; c < 3; ++c) scene.clear[c] = scene.fog_color[c] / 255.0f;
    {
        const drv::CarConstants& ck = model.constants();
        std::memcpy(scene.k.light_dir_t, ck.light_dir_t, sizeof ck.light_dir_t);
        std::memcpy(scene.k.light_col, ck.light_col, sizeof ck.light_col);
        std::memcpy(scene.k.ambient, ck.ambient, sizeof ck.ambient);
        std::memcpy(scene.k.refl_info2, ck.refl_info2, sizeof ck.refl_info2);
        std::memcpy(model.constants().fog, scene.k.fog, sizeof scene.k.fog);
    }
    const std::vector<uint16_t> car_list = model.view_draw_list(1);
    // A câmera do jogador (driving/camera/player_camera.h): as tabelas do data\render\camera.ini do .VIV da missão para
    // este carro, o ruído, a âncora (o corpo do carro) e o RPlayerCamera, atualizado a cada passo da simulação como no
    // jogo (UpdateCamera só anda quando o passo muda).
    drv::CameraTables* cam_tables = new drv::CameraTables();
    {
        drv::IniFile ini;
        const std::string text = read_entry(arc, "data\\render\\camera.ini");
        ini.open(std::vector<uint8_t>(text.begin(), text.end()));
        drv::CameraCarInfo info;
        info.type = car_name;
        cam_tables->load(ini, info);
    }
    drv::Noise_Init();
    drv::CameraAnchor* anchor = new drv::CameraAnchor();
    anchor->kind = 1;
    anchor->flags = 0;
    anchor->body = int16_t(rb->slot);
    anchor->object = car;
    drv::CameraContext cam_ctx;
    cam_ctx.sim = &drv::simulation();
    cam_ctx.world = &world;
    cam_ctx.cars = {anchor};
    drv::PlayerCamera* cam = new drv::PlayerCamera();
    cam->construct(*cam_tables, cam_ctx);
    float eye[3] = {0, 0, 0};
    auto chase = [&](float) {
        cam->update(cam_ctx);
        std::memcpy(eye, cam->d.matrix + 12, sizeof eye);
    };
    // A matriz de visão a partir do referencial da câmera (as linhas: direita, cima, frente, o olho) e o campo de visão:
    // o RViewCamera do jogo projeta 2 x fov na horizontal da tela 4:3 (EAGL::ViewPort::SetPerspective, lido no
    // nightfire-cxbx); aqui o vertical dessa tela (Hor+ nas janelas mais largas).
    auto camera_view = [&](float view[16]) {
        const float* m = cam->d.matrix;
        const float vm[16] = {m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1};
        std::memcpy(view, vm, sizeof vm);
        for (int c = 0; c < 3; ++c) view[12 + c] = -(m[c * 4] * m[12] + m[c * 4 + 1] * m[13] + m[c * 4 + 2] * m[14]);
    };
    auto camera_fov_y = [&]() {
        const double half = double(cam->d.fov) * 3.14159265358979 / 180.0;
        return float(2.0 * std::atan(std::tan(half) * 0.75) * 180.0 / 3.14159265358979);
    };
    std::vector<drv::Track::Draw> draws;
    auto compose = [&](int w, int hgt) {
        scene.items.clear();
        camera_view(scene.view);
        drv::drv_perspective(camera_fov_y(), float(w) / float(hgt), 0.17f, 3000.0f, scene.proj);
        std::memcpy(scene.eye, eye, sizeof eye);
        scene.spec_from_view = true;
        float vp[16];
        drv::drv_mat_mul(scene.view, scene.proj, vp);
        draws.clear();
        track.visible(vp, eye, draws);
        auto push = [&](const drv::Track::Draw& d) {
            const drv::TrackInstance& inst = track.instances()[size_t(d.instance)];
            drv::DrvItem it;
            it.model = &track.models();
            it.part = d.part;
            std::memcpy(it.world, inst.matrix, sizeof it.world);
            const drv::TrackArticle& art = track.articles()[size_t(inst.article)];
            if (art.render_flags & 1) it.flags |= drv::DrvItem::kNoFog;
            if (d.pass == 4) {
                it.flags |= drv::DrvItem::kNoFog | drv::DrvItem::kNoPointLights | drv::DrvItem::kSky;
                it.world[12] = eye[0];
                it.world[13] = eye[1];
                it.world[14] = eye[2];
            }
            scene.items.push_back(it);
        };
        for (int pass : {4, 3, 2}) {
            if (pass == 4 && a.no_sky) continue;
            for (const auto& d : draws)
                if (d.pass == pass) push(d);
        }
        float carm[16];
        car_render_matrix(rb, render_offset, carm);
        for (uint16_t ii : car_list) {
            if (ii >= model.instances().size() || model.instances()[ii].part < 0) continue;
            drv::DrvItem it;
            it.model = &model;
            it.part = model.instances()[ii].part;
            drv::drv_mat_mul(model.instances()[ii].matrix, carm, it.world);
            scene.items.push_back(it);
        }
        for (int pass : {1, 0}) {
            std::vector<drv::Track::Draw> sorted;
            for (const auto& d : draws)
                if (d.pass == pass) sorted.push_back(d);
            std::stable_sort(sorted.begin(), sorted.end(),
                             [](const drv::Track::Draw& x, const drv::Track::Draw& y) { return x.distance > y.distance; });
            for (const auto& d : sorted) push(d);
        }
    };
    DriveInput input;
    {
        // O .def do controle (Master.def: "Driving 1 / drivecfg.def"): o da missão, ou o do MISC.VIV.
        std::string def = read_entry(arc, "data\\Control\\drivecfg.def");
        if (def.empty()) {
            drv::Bigf misc;
            if (misc.open(*disc, "DRIVING/MISC.VIV")) def = read_entry(misc, "data\\Control\\drivecfg.def");
        }
        if (def.empty()) {
            std::fprintf(stderr, "nf_driving: sem o data\\Control\\drivecfg.def\n");
            return 1;
        }
        input.map.LoadParseDefFile(def);
        input.io.first_delay = 18;  // 300 e 150 ms num relógio de 60
        input.io.repeat_delay = 9;
    }
    uint64_t steps = 0;
    // O laço do jogo (RunTheGame, 0x00175d78): o escalonador com as tarefas do jogo. Cada tarefa cria o seu evento e os
    // eventos rodam em ordem depois de cada tick (EventManager::RunEvents): ESimFrameUpdate (prioridade 0), EAnimUpdate,
    // EAIUpdate e ECameraUpdate (2), ESimEndFrame (7); por volta ERenderFrame (6) e EAudioUpdate (7). Os que o port tem:
    // a simulação, a câmera e o pedido de desenho; os outros só contados.
    drv::Scheduler sched(0);
    drv::RunTheGame_AddTasks(sched, false);
    std::vector<int32_t> events;
    uint64_t event_counts[0x14] = {};
    bool render_requested = false;
    sched.call = [&](int32_t fn, uint32_t) { events.push_back(fn); };
    sched.run_events = [&] {
        for (size_t i = 0; i < events.size(); ++i) {
            const int32_t fn = events[i];
            if (fn >= 0 && fn < 0x14) ++event_counts[fn];
            if (fn == drv::kEvSimFrameUpdate) {
                drv::ESimFrameUpdate(drv::simulation());
                ++steps;
            } else if (fn == drv::kEvCameraUpdate) {
                chase(0.12f);
            } else if (fn == drv::kEvRenderFrame) {
                render_requested = true;
            }
        }
        events.clear();
    };
    int32_t clock = 0;  // o Clock do jogo (RealClock: um por quadro de vídeo, 60 Hz)
    // Um quadro do roteiro: a entrada (os SYNCTASKs, antes do Run) e um tick do relógio.
    auto step = [&](const DriveKeys& k) {
        input.feed(*car, pad_from(k, nullptr), clock);
        ++clock;
        sched.Run(clock);
    };
    // O roteiro: "teclas:segundos" separados por espaço (60 passos por segundo).
    std::vector<std::pair<DriveKeys, int>> script;
    {
        size_t p = 0;
        const std::string& s = a.drive_input;
        while (p < s.size()) {
            while (p < s.size() && s[p] == ' ') ++p;
            size_t e = s.find(' ', p);
            if (e == std::string::npos) e = s.size();
            const std::string t = s.substr(p, e - p);
            p = e;
            const size_t c = t.find(':');
            if (c == std::string::npos) continue;
            script.push_back({parse_keys(t.substr(0, c)), int(std::strtof(t.c_str() + c + 1, nullptr) * 60.0f + 0.5f)});
        }
    }
    std::string rep;
    auto report_line = [&](double ms_per_frame) {
        char b[1024];
        const drv::PBondCarData& c = car->d;
        std::snprintf(b, sizeof b,
                      "carro %s largada %.2f,%.2f,%.2f chao %d | passos %llu pos %.2f,%.2f,%.2f velocidade %.2f m/s "
                      "marcha %d rodas_no_chao %d direcao %.3f acelerador %.2f freio %.2f compressao %.3f/%.3f/%.3f/%.3f | "
                      "caixa %.3f,%.3f,%.3f deslocamento %.3f pontos %zu alavancas %d massa %.0f | carga_ms %.0f "
                      "ms_por_quadro %.3f\n",
                      car_name.c_str(), start[0], start[1], start[2], ground ? 1 : 0, (unsigned long long)steps,
                      rb->pos[0], rb->pos[1], rb->pos[2], double(c.car_speed), int(c.gear), int(c.num_wheels_on_ground),
                      double(c.control.steering), double(c.control.gas), double(c.control.brake), double(c.suspension[0]),
                      double(c.suspension[1]), double(c.suspension[2]), double(c.suspension[3]),
                      double((desc.bbox_max[0] - desc.bbox_min[0]) * 0.5f), double((desc.bbox_max[1] - desc.bbox_min[1]) * 0.5f),
                      double((desc.bbox_max[2] - desc.bbox_min[2]) * 0.5f), double(render_offset), desc.collision.size(),
                      int(rb->info->lever_count), double(rb->mass), load_ms, ms_per_frame);
        rep = b;
        const drv::BondCarStats& st = drv::bond_car_stats();
        const drv::CollideEvents& ce = drv::collide_events();
        std::snprintf(b, sizeof b,
                      "eventos aterrissagens %llu poeira %llu recolocar %llu guardas %llu | choques %llu encostado %d "
                      "dano %llu (%.1f) estimulos %llu | pendentes %s\n",
                      (unsigned long long)st.landing_impacts, (unsigned long long)st.landing_fx,
                      (unsigned long long)st.resets, (unsigned long long)st.guards, (unsigned long long)ce.collisions,
                      int(car->d.against_wall), (unsigned long long)car->damage_calls, double(car->damage_total),
                      (unsigned long long)ce.stimuli, drv::physics_pending_report());
        rep += b;
        std::snprintf(b, sizeof b,
                      "agenda relogio %d ticks %llu jogados_fora %llu | eventos simulacao %llu animacao %llu ia %llu "
                      "camera %llu fim %llu desenho %llu audio %llu | entrada acoes %llu guardas %llu\n",
                      int(clock), (unsigned long long)sched.ticks, (unsigned long long)sched.discarded,
                      (unsigned long long)event_counts[drv::kEvSimFrameUpdate],
                      (unsigned long long)event_counts[drv::kEvAnimUpdate],
                      (unsigned long long)event_counts[drv::kEvAIUpdate],
                      (unsigned long long)event_counts[drv::kEvCameraUpdate],
                      (unsigned long long)event_counts[drv::kEvSimEndFrame],
                      (unsigned long long)event_counts[drv::kEvRenderFrame],
                      (unsigned long long)event_counts[drv::kEvAudioUpdate], (unsigned long long)input.actions,
                      (unsigned long long)drv::input_guards());
        rep += b;
    };

    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    if (!a.screenshot.empty()) {
        // A captura: o roteiro (ou --seconds parado), depois um quadro fora da tela.
        if (!r.init(nullptr, ro, err) || !r.upload(track.models(), err) || !r.upload(model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        chase(1.0f);
        int total = 0;
        for (const auto& s : script) total += s.second;
        const int want = a.seconds > 0 ? int(a.seconds * 60.0f + 0.5f) : total;
        int done = 0;
        for (const auto& s : script)
            for (int i = 0; i < s.second && done < want; ++i, ++done) step(s.first);
        for (; done < want; ++done) step(DriveKeys{});
        double ms = 0;
        if (a.bench_frames > 0) {
            // O custo de um quadro inteiro: um passo da física, a visibilidade, a cena e o desenho (fora da tela).
            compose(a.width, a.height);
            r.draw_scene(scene);
            r.finish();
            const uint64_t b0 = nf::os::perf_counter();
            DriveKeys k;
            k.gas = true;
            for (int i = 0; i < a.bench_frames; ++i) {
                step(k);
                compose(a.width, a.height);
                r.draw_scene(scene);
            }
            r.finish();
            ms = 1000.0 * double(int64_t(nf::os::perf_counter() - b0)) / double(int64_t(freq)) / a.bench_frames;
            std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f | %s\n", a.bench_frames, ms, 1000.0 / ms,
                        r.stats().adapter.c_str());
        }
        compose(a.width, a.height);
        r.draw_scene(scene);
        report_line(ms);
        char b[256];
        std::snprintf(b, sizeof b,
                      "camera %.2f %.2f %.2f modo %d braco %d fov %.2f distancia %.2f olhar %.2f %.2f %.2f colisoes %llu "
                      "chao %llu barreiras %llu antivertigem %d pendentes %llu | itens %zu desenho chamadas %d | %dx%d | "
                      "adaptador %s\n",
                      eye[0], eye[1], eye[2], int(cam->d.mode), int(cam->d.arm), double(cam->d.fov),
                      double(cam->d.distance), double(cam->d.look_at[0]), double(cam->d.look_at[1]),
                      double(cam->d.look_at[2]), (unsigned long long)cam->collisions,
                      (unsigned long long)cam->ground_hits, (unsigned long long)cam->barrier_hits,
                      int(cam->statics.vertigo_count), (unsigned long long)drv::camera_pending(), scene.items.size(),
                      r.stats().draw_calls, a.width, a.height, r.stats().adapter.c_str());
        rep += b;
        std::printf("%s", rep.c_str());
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        drv::set_world(nullptr);
        return 0;
    }
    // A janela: a física a 60 passos por segundo do relógio, o desenho a cada volta.
    nf::Window win(L"nf_driving — " + widen(a.track) + L" — " + widen(car_name), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(track.models(), err) || !r.upload(model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    chase(1.0f);
    report_line(0);
    std::printf("%s", rep.c_str());
    std::printf("W/seta acima acelera, S/seta abaixo freia (e dá ré), A/D/setas viram, espaço é o freio de mão, Esc sai\n");
    // O relógio do jogo pelo da parede (60 por segundo); a cada volta a entrada e o Run, que roda os ticks que passaram
    // (12 ou mais são jogados fora, como no PS2) e pede o desenho no último; sem tick, espera o próximo.
    const double t0s = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const int32_t clock0 = clock;
    (void)freq;
    while (win.pump()) {
        if (win.key_down(nf::key::Escape)) break;
        DriveKeys k;
        k.gas = win.key_down('W') || win.key_down(nf::key::Up);
        k.brake = win.key_down('S') || win.key_down(nf::key::Down);
        k.left = win.key_down('A') || win.key_down(nf::key::Left);
        k.right = win.key_down('D') || win.key_down(nf::key::Right);
        k.hand = win.key_down(nf::key::Space);
        nf::PadState pads[nf::kMaxPads];
        nf::system_pad_source().poll(pads);
        input.feed(*car, pad_from(k, &pads[0]), clock);
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        clock = clock0 + int32_t((now - t0s) * 60.0);
        render_requested = false;
        sched.Run(clock);
        if (!render_requested) {
            nf::os::wait_until(t0s + double(clock - clock0 + 1) / 60.0);
            continue;
        }
        if (win.resized()) r.resize(win.width(), win.height());
        compose(win.width(), win.height());
        r.draw_scene(scene);
        r.present();
    }
    drv::set_world(nullptr);
    return 0;
}

int nf_app_main(int argc, wchar_t** argv) {
    console();
    Args a;
    if (!parse(argc, argv, a)) return 1;
    if (a.census) return run_census(a);
    if (a.check) return run_check(a);
    if (!a.track.empty() && a.drive) return run_drive(a);
    if (!a.track.empty()) return run_track(a);
    if (!a.track_census.empty()) {
        const std::shared_ptr<nf::DiscSource> disc = open_data(a);
        drv::CarModel m;
        m.set_resolve_carp(true);
        m.set_verify(a.verify);
        std::string e;
        const std::string t = a.track_census;
        if (!disc || !m.load_carp(*disc, a.viv, "data\\track\\" + t + ".crp", {"data\\track\\" + t + ".ssh"}, t, e)) {
            std::fprintf(stderr, "nf_driving: %s\n", e.c_str());
            return 1;
        }
        Args b = a;
        b.model = t;
        std::printf("%s", summary(report(m, b, nullptr)).c_str());
        std::printf("simbolos_nulos");
        for (const auto& kv : m.carp_misses()) std::printf(" %s=%d", kv.first.c_str(), kv.second);
        const drv::CarpResolveStats& rs = drv::carp_resolve_stats();
        std::printf(" | sr %d nulos %d registros_por_etiqueta %d listas_de_eventos %d\n", rs.symbolic,
                    rs.symbolic_null, rs.tag_records, rs.event_lists);
        return 0;
    }

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

