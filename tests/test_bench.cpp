// Testes da escolha do adaptador de vídeo (platform/gpu_adapter.h: a preferência, o ini, a regra da escolha e os
// valores da opção dos menus) e das contas do benchmark (platform/bench_stats.h: médias e percentis em tempos de
// quadro conhecidos). Sem placa de vídeo: as listas de adaptadores são montadas à mão.

#include <string>
#include <vector>

#include "platform/bench_stats.h"
#include "platform/gpu_adapter.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "test_framework.h"

namespace {

// A máquina do usuário: o adaptador padrão é a RTX (índice 0), o integrado da AMD vem depois, e o de software no fim.
std::vector<nf::AdapterInfo> two_gpus() {
    std::vector<nf::AdapterInfo> l(3);
    l[0].index = 0;
    l[0].name = "NVIDIA GeForce RTX 5090";
    l[0].vendor_id = 0x10de;
    l[0].dedicated_vram = 32ull << 30;
    l[0].integrated = false;
    l[0].power_rank = 1;
    l[0].perf_rank = 0;
    l[1].index = 1;
    l[1].name = "AMD Radeon(TM) Graphics";
    l[1].vendor_id = 0x1002;
    l[1].dedicated_vram = 512ull << 20;
    l[1].integrated = true;
    l[1].power_rank = 0;
    l[1].perf_rank = 1;
    l[2].index = 2;
    l[2].name = "Microsoft Basic Render Driver";
    l[2].vendor_id = 0x1414;
    l[2].software = true;
    l[2].integrated = true;  // o de software nunca é escolhido, mesmo "integrado"
    l[2].power_rank = 2;
    l[2].perf_rank = 2;
    return l;
}

}  // namespace

TEST(adapter_preference_parse_and_name) {
    nf::AdapterPreference p;
    CHECK(nf::parse_adapter_preference("auto", &p) && p.kind == nf::AdapterPrefKind::Auto);
    CHECK(nf::parse_adapter_preference(" Integrated ", &p) && p.kind == nf::AdapterPrefKind::Integrated);
    CHECK(nf::parse_adapter_preference("DISCRETE", &p) && p.kind == nf::AdapterPrefKind::Discrete);
    CHECK(nf::parse_adapter_preference("1", &p) && p.kind == nf::AdapterPrefKind::Index && p.index == 1);
    CHECK(nf::parse_adapter_preference("rtx 5090", &p) && p.kind == nf::AdapterPrefKind::Name && p.name == "rtx 5090");
    // Um número de 4 dígitos é um trecho de nome (o modelo), não um índice.
    CHECK(nf::parse_adapter_preference("5090", &p) && p.kind == nf::AdapterPrefKind::Name);
    nf::AdapterPreference keep;
    keep.kind = nf::AdapterPrefKind::Discrete;
    p = keep;
    CHECK(!nf::parse_adapter_preference("", &p) && !nf::parse_adapter_preference("   ", &p));
    CHECK(p == keep);  // sem texto, nada muda
    // A volta: o nome do ini lido de novo dá a mesma preferência.
    for (const char* text : {"auto", "integrated", "discrete", "0", "2", "Radeon"}) {
        nf::AdapterPreference a, b;
        CHECK(nf::parse_adapter_preference(text, &a));
        CHECK(nf::parse_adapter_preference(nf::adapter_preference_name(a), &b));
        CHECK(a == b);
    }
}

TEST(adapter_choose_by_preference) {
    const std::vector<nf::AdapterInfo> l = two_gpus();
    nf::AdapterPreference p;
    CHECK(nf::choose_adapter(l, p) == -1);  // auto: o padrão do sistema (sem adaptador explícito)
    p.kind = nf::AdapterPrefKind::Integrated;
    CHECK(nf::choose_adapter(l, p) == 1);
    p.kind = nf::AdapterPrefKind::Discrete;
    CHECK(nf::choose_adapter(l, p) == 0);
    p.kind = nf::AdapterPrefKind::Index;
    p.index = 1;
    CHECK(nf::choose_adapter(l, p) == 1);
    p.index = 2;
    CHECK(nf::choose_adapter(l, p) == -1);  // o de software, não
    p.index = 7;
    CHECK(nf::choose_adapter(l, p) == -1);  // fora da lista: o padrão
    p.kind = nf::AdapterPrefKind::Name;
    p.name = "radeon";
    CHECK(nf::choose_adapter(l, p) == 1);   // sem diferenciar maiúsculas
    p.name = "RTX 5090";
    CHECK(nf::choose_adapter(l, p) == 0);
    p.name = "basic render";
    CHECK(nf::choose_adapter(l, p) == -1);
    p.name = "arc";
    CHECK(nf::choose_adapter(l, p) == -1);
    // Sem integrado (uma placa só), integrated cai no padrão; dois dedicados: o primeiro da ordem de desempenho.
    std::vector<nf::AdapterInfo> d = l;
    d[1].integrated = false;
    d[1].perf_rank = 0;
    d[0].perf_rank = 1;
    p.kind = nf::AdapterPrefKind::Integrated;
    CHECK(nf::choose_adapter(d, p) == -1);
    p.kind = nf::AdapterPrefKind::Discrete;
    CHECK(nf::choose_adapter(d, p) == 1);
    // Sem as ordens do DXGI (sem IDXGIFactory6: -1), vale a ordem da lista.
    for (nf::AdapterInfo& a : d) a.perf_rank = a.power_rank = -1;
    CHECK(nf::choose_adapter(d, p) == 0);
}

TEST(adapter_ini_round_trip_and_menu_values) {
    nf::Settings s;
    s.parse("[video]\nadapter = Integrated\n");
    nf::VideoSettings v;
    CHECK(v.adapter == "auto");  // o padrão
    nf::load_video_settings(s, v);
    CHECK(v.adapter == "integrated");  // normalizado
    v.adapter = "Radeon";
    nf::store_video_settings(v, s);
    CHECK(s.get("video", "adapter") == "Radeon");
    nf::VideoSettings w;
    nf::load_video_settings(s, w);
    CHECK(w.adapter == "Radeon" && w == v);
    // Vazio no ini: fica o que estava.
    nf::Settings e;
    e.parse("[video]\nadapter =\n");
    nf::VideoSettings x;
    x.adapter = "discrete";
    nf::load_video_settings(e, x);
    CHECK(x.adapter == "discrete");
    // Os valores da opção: os três modos, um por adaptador (sem o de software) e o atual que não é nenhum deles.
    const std::vector<nf::AdapterInfo> l = two_gpus();
    const std::vector<std::string> vals = nf::adapter_choice_values(l, "auto");
    CHECK(vals.size() == 5 && vals[0] == "auto" && vals[1] == "integrated" && vals[2] == "discrete");
    CHECK(vals[3] == "NVIDIA GeForce RTX 5090" && vals[4] == "AMD Radeon(TM) Graphics");
    CHECK(nf::adapter_choice_values(l, "Radeon").back() == "Radeon");
    CHECK(nf::adapter_choice_values(l, "discrete").size() == 5);
    // A mesma placa duas vezes na lista do DXGI (a RTX 5090 desta máquina aparece três vezes): um valor só, o nome.
    std::vector<nf::AdapterInfo> twins = l;
    twins[1].name = twins[0].name;
    const std::vector<std::string> tv = nf::adapter_choice_values(twins, "auto");
    CHECK(tv.size() == 4 && tv[3] == "NVIDIA GeForce RTX 5090");
    CHECK(nf::adapter_choice_values(twins, "1").back() == "1");  // um índice do ini continua na lista
    CHECK(nf::adapter_short_name("NVIDIA GeForce RTX 5090") == "RTX 5090");
    CHECK(nf::adapter_short_name("AMD Radeon(TM) Graphics") == "Radeon Graphics");
    CHECK(nf::adapter_short_name("Intel(R) UHD Graphics 770") == "UHD Graphics 770");
    CHECK(nf::adapter_short_name("AMD") == "AMD");  // só o fabricante: fica
}

TEST(bench_percentiles_on_known_frame_times) {
    // 1, 2, ..., 100 ms: a média 50,5; o percentil 50 é o 50.º (nearest rank), o 99 o 99.º e o 99,9 o 100.º.
    std::vector<double> ms;
    for (int i = 100; i >= 1; --i) ms.push_back(double(i));  // fora de ordem de propósito
    const nf::FrameTimeStats s = nf::frame_time_stats(ms);
    CHECK(s.frames == 100);
    CHECK_NEAR(s.total_ms, 5050.0, 1e-9);
    CHECK_NEAR(s.avg_ms, 50.5, 1e-9);
    CHECK_NEAR(s.avg_fps, 1000.0 / 50.5, 1e-9);
    CHECK_NEAR(s.min_ms, 1.0, 0.0);
    CHECK_NEAR(s.max_ms, 100.0, 0.0);
    CHECK_NEAR(s.p50_ms, 50.0, 0.0);
    CHECK_NEAR(s.p99_ms, 99.0, 0.0);
    CHECK_NEAR(s.p999_ms, 100.0, 0.0);
    CHECK_NEAR(s.low1_fps, 1000.0 / 99.0, 1e-9);
    CHECK_NEAR(s.worst1_avg_ms, 100.0, 0.0);   // 1% de 100 = 1 quadro
    CHECK_NEAR(s.worst01_avg_ms, 100.0, 0.0);  // pelo menos 1
    // 1000 quadros de 10 ms com 10 de 50 ms e 1 de 200: o 1% pior é exatamente os 10 lentos... e o de 200.
    std::vector<double> t(989, 10.0);
    for (int i = 0; i < 10; ++i) t.push_back(50.0);
    t.push_back(200.0);
    const nf::FrameTimeStats u = nf::frame_time_stats(t);
    CHECK(u.frames == 1000);
    CHECK_NEAR(u.p99_ms, 50.0, 0.0);    // a posição 990 em 1000
    CHECK_NEAR(u.p999_ms, 50.0, 0.0);   // a posição 999: o último de 50 (o de 200 é o 1000.º)
    CHECK_NEAR(u.max_ms, 200.0, 0.0);
    CHECK_NEAR(u.worst1_avg_ms, (9 * 50.0 + 200.0) / 10.0, 1e-9);  // os 10 últimos: nove de 50 e o de 200
    CHECK_NEAR(u.worst01_avg_ms, 200.0, 0.0);                       // 0,1% de 1000 = 1
    CHECK_NEAR(u.avg_ms, (989 * 10.0 + 10 * 50.0 + 200.0) / 1000.0, 1e-9);
    // Lista vazia e um quadro só.
    CHECK(nf::frame_time_stats({}).frames == 0 && nf::frame_time_stats({}).avg_fps == 0.0);
    const nf::FrameTimeStats one = nf::frame_time_stats({16.0});
    CHECK_NEAR(one.p99_ms, 16.0, 0.0);
    CHECK_NEAR(one.avg_fps, 62.5, 1e-9);
    CHECK_NEAR(nf::percentile_nearest_rank({1.0, 2.0, 3.0, 4.0}, 25.0), 1.0, 0.0);
    CHECK_NEAR(nf::percentile_nearest_rank({1.0, 2.0, 3.0, 4.0}, 26.0), 2.0, 0.0);
    CHECK_NEAR(nf::percentile_nearest_rank({1.0, 2.0, 3.0, 4.0}, 0.0), 1.0, 0.0);
    CHECK_NEAR(nf::percentile_nearest_rank({1.0, 2.0, 3.0, 4.0}, 100.0), 4.0, 0.0);
}
