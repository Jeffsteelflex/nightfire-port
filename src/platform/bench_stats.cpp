// As contas do benchmark (só do port). Ver bench_stats.h.

#include "platform/bench_stats.h"

#include <algorithm>
#include <cmath>

namespace nf {

double percentile_nearest_rank(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double n = double(sorted.size());
    // ceil com tolerância: 99% de 100 quadros é a posição 99, não 100 por erro de arredondamento.
    size_t rank = size_t(std::ceil(std::clamp(p, 0.0, 100.0) / 100.0 * n - 1e-9));
    rank = std::clamp<size_t>(rank, 1, sorted.size());
    return sorted[rank - 1];
}

double mean_of_slowest(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    size_t k = size_t(std::ceil(std::clamp(fraction, 0.0, 1.0) * double(sorted.size()) - 1e-9));
    k = std::clamp<size_t>(k, 1, sorted.size());
    double sum = 0.0;
    for (size_t i = sorted.size() - k; i < sorted.size(); ++i) sum += sorted[i];
    return sum / double(k);
}

FrameTimeStats frame_time_stats(std::vector<double> frame_ms) {
    FrameTimeStats s;
    s.frames = frame_ms.size();
    if (frame_ms.empty()) return s;
    std::sort(frame_ms.begin(), frame_ms.end());
    for (const double v : frame_ms) s.total_ms += v;
    s.avg_ms = s.total_ms / double(s.frames);
    s.avg_fps = s.total_ms > 0.0 ? 1000.0 * double(s.frames) / s.total_ms : 0.0;
    s.min_ms = frame_ms.front();
    s.max_ms = frame_ms.back();
    s.p50_ms = percentile_nearest_rank(frame_ms, 50.0);
    s.p99_ms = percentile_nearest_rank(frame_ms, 99.0);
    s.p999_ms = percentile_nearest_rank(frame_ms, 99.9);
    s.low1_fps = s.p99_ms > 0.0 ? 1000.0 / s.p99_ms : 0.0;
    s.low01_fps = s.p999_ms > 0.0 ? 1000.0 / s.p999_ms : 0.0;
    s.worst1_avg_ms = mean_of_slowest(frame_ms, 0.01);
    s.worst01_avg_ms = mean_of_slowest(frame_ms, 0.001);
    return s;
}

}  // namespace nf
