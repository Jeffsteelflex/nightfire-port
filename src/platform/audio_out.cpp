// A saída de áudio do PC: ver audio_out.h.

#include "platform/audio_out.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "platform/audio_device.h"
#include "platform/os.h"
#include "platform/sfx_iop.h"
#include "platform/fmath.h"

namespace nf {

namespace {

constexpr int kFramesPerVBlank = 800;              // 48000 / 60
constexpr uint32_t kMaxGapVBlanks = 600;           // 10 s
constexpr int kChunkFrames = 4096;
constexpr int kMaxQueuedChunks = 8;                // acima disso o pedaço é descartado (latência; o XAudio2 guarda 16)

AudioMode g_mode = AudioMode::Off;
bool g_started = false;
uint32_t g_last_vblank = 0;
uint64_t g_vblanks = 0;
AudioOutStats g_stats;
std::vector<int16_t> g_mix;
AudioSource g_source = nullptr;      // nula: a SPU2 do jogo a pé
const Spu2* g_source_stats = nullptr;

// Medida dos últimos 0.5 s: (pico, soma dos quadrados, amostras) por lote.
struct Meter {
    float peak;
    double sumsq;
    uint32_t n;
};
std::vector<Meter> g_meters;

// ---- o aparelho (platform/audio_device.h: o XAudio2 no Windows) ----
std::unique_ptr<AudioDevice> g_device;
uint64_t g_submitted = 0;  // amostras entregues à voz desde que ela foi criada (o silêncio da frente incluído)

// ---- WAV ----
FILE* g_wav = nullptr;
uint32_t g_wav_frames = 0;
uint32_t g_wav_since_header = 0;

void wav_write_header() {
    if (g_wav == nullptr) return;
    const uint32_t data_bytes = g_wav_frames * 4;
    uint8_t h[44];
    auto put32 = [&](int at, uint32_t v) {
        h[at] = uint8_t(v);
        h[at + 1] = uint8_t(v >> 8);
        h[at + 2] = uint8_t(v >> 16);
        h[at + 3] = uint8_t(v >> 24);
    };
    auto put16 = [&](int at, uint16_t v) {
        h[at] = uint8_t(v);
        h[at + 1] = uint8_t(v >> 8);
    };
    std::memcpy(h, "RIFF", 4);
    put32(4, 36 + data_bytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, 48000);
    put32(28, 48000 * 4);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(h + 36, "data", 4);
    put32(40, data_bytes);
    const long pos = std::ftell(g_wav);
    std::fseek(g_wav, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof(h), g_wav);
    std::fseek(g_wav, pos > 44 ? pos : 44, SEEK_SET);
}

void meter(const int16_t* pcm, int frames) {
    Meter m{0.0f, 0.0, uint32_t(frames * 2)};
    for (int i = 0; i < frames * 2; ++i) {
        const float v = float(pcm[i]);
        m.peak = std::max(m.peak, std::fabs(v));
        m.sumsq += double(v) * double(v);
    }
    g_meters.push_back(m);
    // ~0.5 s de lotes
    uint32_t total = 0;
    size_t first = g_meters.size();
    while (first > 0 && total < 48000) total += g_meters[--first].n;
    if (first > 0) g_meters.erase(g_meters.begin(), g_meters.begin() + std::ptrdiff_t(first));
}

void device_submit(const int16_t* pcm, int frames) {
    if (g_device == nullptr || frames <= 0) return;
    const int queued = g_device->queued_buffers();
    if (queued == 0 && g_started) ++g_stats.starved;
    if (queued >= kMaxQueuedChunks) {
        ++g_stats.dropped_chunks;
        return;
    }
    if (g_device->submit(pcm, frames)) g_submitted += uint64_t(frames);
}

void deliver(const int16_t* pcm, int frames) {
    meter(pcm, frames);
    g_stats.frames += uint64_t(frames);
    if (g_mode == AudioMode::Dump && g_wav != nullptr) {
        std::fwrite(pcm, 4, size_t(frames), g_wav);
        g_wav_frames += uint32_t(frames);
        g_wav_since_header += uint32_t(frames);
        if (g_wav_since_header >= 48000) {
            g_wav_since_header = 0;
            wav_write_header();
            std::fflush(g_wav);
        }
    }
}

}  // namespace

bool audio_out_open_device() {
    audio_out_close();
    g_device = open_audio_device();
    if (g_device == nullptr) return false;
    g_submitted = 0;
    // 50 ms de silêncio na frente: a folga para o ritmo irregular dos quadros.
    std::vector<int16_t> silence(2400 * 2, 0);
    device_submit(silence.data(), 2400);
    g_device->start();
    g_mode = AudioMode::Device;
    return true;
}

bool audio_out_open_dump(const std::wstring& path) {
    audio_out_close();
    g_wav = os::open_file(path, "wb");
    if (g_wav == nullptr) return false;
    g_wav_frames = 0;
    g_wav_since_header = 0;
    wav_write_header();
    g_mode = AudioMode::Dump;
    return true;
}

void audio_out_close() {
    g_device.reset();
    if (g_wav != nullptr) {
        wav_write_header();
        std::fclose(g_wav);
        g_wav = nullptr;
        // A medida da folga para quem analisa o dump: onde a SPU2 saturou (amostras por canal).
        const Spu2::Saturation& s = (g_source_stats != nullptr ? *g_source_stats : spu()).saturation();
        const double peak_db = s.peak > 0 ? 20.0 * nf::fm::log10(double(s.peak) / 32768.0) : -120.0;
        std::fprintf(stderr, "spu2: saturacoes vozes0 %llu vozes1 %llu nucleo0 %llu saida %llu pico_sem_corte %+.1f dB\n",
                     static_cast<unsigned long long>(s.voices[0]), static_cast<unsigned long long>(s.voices[1]),
                     static_cast<unsigned long long>(s.core0), static_cast<unsigned long long>(s.output), peak_db);
    }
    g_mode = AudioMode::Off;
}

AudioMode audio_out_mode() { return g_mode; }

void audio_out_catch_up(uint32_t vblank) {
    if (!g_started) {
        g_started = true;
        g_last_vblank = vblank;
        return;
    }
    uint32_t n = vblank - g_last_vblank;
    g_last_vblank = vblank;
    if (n == 0) return;
    if (n > kMaxGapVBlanks) n = kMaxGapVBlanks;
    g_vblanks += n;
    const int frames = int(n) * kFramesPerVBlank;
    g_mix.resize(size_t(frames) * 2);
    if (g_source != nullptr) g_source(g_mix.data(), frames);
    else spu().render(g_mix.data(), frames);
    deliver(g_mix.data(), frames);
    if (g_mode == AudioMode::Device) {
        // Só o fim do que foi misturado cabe na fila; em pedaços de até kChunkFrames.
        const int keep = std::min(frames, kChunkFrames * 2);
        const int16_t* p = g_mix.data() + size_t(frames - keep) * 2;
        for (int left = keep; left > 0;) {
            const int part = std::min(left, kChunkFrames);
            device_submit(p, part);
            p += part * 2;
            left -= part;
        }
    }
}

uint64_t audio_out_vblanks() { return g_vblanks; }

int64_t audio_out_queued_frames() {
    if (g_mode != AudioMode::Device || g_device == nullptr) return 0;
    return std::max<int64_t>(0, int64_t(g_submitted) - int64_t(g_device->played_frames()));
}

AudioOutStats audio_out_stats() {
    AudioOutStats s = g_stats;
    float peak = 0.0f;
    double sumsq = 0.0;
    uint32_t n = 0;
    for (const Meter& m : g_meters) {
        peak = std::max(peak, m.peak);
        sumsq += m.sumsq;
        n += m.n;
    }
    auto db = [](double v) { return v > 0.0 ? float(20.0 * nf::fm::log10(v / 32768.0)) : -120.0f; };
    s.peak_db = db(peak);
    s.rms_db = n != 0 ? db(std::sqrt(sumsq / n)) : -120.0f;
    return s;
}

void audio_out_set_source(AudioSource render, const Spu2* stats) {
    g_source = render;
    g_source_stats = stats;
}

void audio_out_debug_power_on() {
    audio_out_close();
    g_started = false;
    g_last_vblank = 0;
    g_vblanks = 0;
    g_stats = AudioOutStats{};
    g_meters.clear();
}

}  // namespace nf
