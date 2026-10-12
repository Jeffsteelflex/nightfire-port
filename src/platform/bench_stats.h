#pragma once
// As contas do benchmark (--bench, só do port; main.cpp mede, docs/port.md "Benchmark"): os tempos de quadro em ms
// viram média, percentis e "lows". Sem GPU, testável.
//
// Definições (as mesmas no relatório e nos testes):
// - fps médio = quadros / soma dos tempos; tempo médio = soma / quadros;
// - percentil p (nearest rank): o valor na posição ceil(p / 100 x N) da lista em ordem crescente (1 = o menor);
// - "1% low" = o tempo do percentil 99 (e 1000 / ele em fps); "0.1% low" = o do percentil 99,9;
// - "média dos 1% piores": a média dos ceil(N / 100) quadros mais lentos (a outra definição comum de 1% low;
//   idem para 0,1%).

#include <cstddef>
#include <vector>

namespace nf {

struct FrameTimeStats {
    size_t frames = 0;
    double total_ms = 0.0;
    double avg_ms = 0.0;
    double avg_fps = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double p50_ms = 0.0;
    double p99_ms = 0.0;           // 1% low (tempo)
    double p999_ms = 0.0;          // 0.1% low (tempo)
    double low1_fps = 0.0;         // 1000 / p99
    double low01_fps = 0.0;        // 1000 / p99.9
    double worst1_avg_ms = 0.0;    // média dos 1% mais lentos
    double worst01_avg_ms = 0.0;   // média dos 0,1% mais lentos
};

// O percentil p (0 a 100) de uma lista já em ordem crescente, pela posição mais próxima (nearest rank). Vazia: 0.
double percentile_nearest_rank(const std::vector<double>& sorted, double p);
// A média dos `fraction` (0 a 1) maiores de uma lista em ordem crescente: os ceil(fraction x N) últimos (pelo menos 1).
double mean_of_slowest(const std::vector<double>& sorted, double fraction);
FrameTimeStats frame_time_stats(std::vector<double> frame_ms);

}  // namespace nf
