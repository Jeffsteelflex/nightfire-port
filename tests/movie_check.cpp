// Confere o decodificador de MPEG-2 do port (platform/mpeg2_decoder.h) contra o do sistema (o Media Foundation no
// Windows), filme por filme e quadro por quadro, nos 22 filmes do disco (MOVIES/30_FPS/*.PSS). Não entra no ctest:
// precisa dos dados do usuário.
//
// Uso: nf_movie_check [pasta dos dados] [--movie <hash>] [--own-only] [--ref-idct] [--dump <pasta> [--frames a-b]]
//                     [--quiet]
//   A pasta dos dados é a raiz do disco extraído (padrão: a pasta atual), com MOVIES/30_FPS. Por filme: os quadros do
//   stream, os que cada decodificador deu, o que difere por plano (Y, Cb, Cr: a maior diferença, a média absoluta, o
//   PSNR), os quadros com diferença, a diferença média pela posição no GOP (o erro crescendo dentro do GOP seria um
//   desvio de previsão), a ordem de exibição do port (temporal_reference), o relógio do filme (os quadros contra o som:
//   o PC mostra o quadro floor(ouvido / 1600)) e o tempo de cada decodificador por quadro.
//   --own-only: só o do port (sem o do sistema: os totais, a ordem, o tempo).
//   --ref-idct: o do port com a IDCT de referência em ponto flutuante (para separar o arredondamento da IDCT do resto).
//   --dump: grava em PNG (BGRA pela conversão do IPU) os quadros a-b do filme de --movie: o do port, o do sistema e a
//   diferença ampliada (|d| x 16 no Y), e os planos crus (.yuv, I420) dos dois. O que sai é dado do jogo: grave em out/.
//   --where <quadro> [--where-min d]: os pixels desse quadro que diferem d ou mais (padrão 2), por plano.
//   Sai com 0 se nenhum filme teve erro de fluxo nem diferença na contagem de quadros.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "platform/arena.h"
#include "platform/image_io.h"
#include "platform/movie_decoder.h"
#include "platform/mpeg2_decoder.h"
#include "platform/os.h"
#include "platform/pss.h"

namespace {

using Clock = std::chrono::steady_clock;

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Um quadro em planos (Y, Cb, Cr), na ordem de exibição.
struct Planes {
    int w = 0, h = 0;
    std::vector<uint8_t> y, cb, cr;
    int type = 0;  // 1 I, 2 P, 3 B (só os do port)
    int tr = 0;
};

Planes from_nv12(const nf::MovieFrameNV12& f) {
    Planes p;
    p.w = f.width;
    p.h = f.height;
    const size_t n = size_t(f.width) * size_t(f.height);
    p.y.assign(f.data.begin(), f.data.begin() + std::ptrdiff_t(n));
    p.cb.resize(n / 4);
    p.cr.resize(n / 4);
    for (size_t i = 0; i < n / 4; ++i) {
        p.cb[i] = f.data[n + 2 * i];
        p.cr[i] = f.data[n + 2 * i + 1];
    }
    return p;
}

struct PlaneStats {
    int max_abs = 0;
    uint64_t sum_abs = 0;
    uint64_t sum_sq = 0;
    uint64_t pixels = 0;
    uint64_t differing = 0;
    void add(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int* frame_max, uint64_t* frame_sum) {
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) {
            const int d = std::abs(int(a[i]) - int(b[i]));
            if (d == 0) continue;
            max_abs = std::max(max_abs, d);
            sum_abs += uint64_t(d);
            sum_sq += uint64_t(d * d);
            ++differing;
            *frame_max = std::max(*frame_max, d);
            *frame_sum += uint64_t(d);
        }
        pixels += n;
    }
    double mean() const { return pixels ? double(sum_abs) / double(pixels) : 0.0; }
    double psnr() const {
        if (pixels == 0 || sum_sq == 0) return 99.0;
        const double mse = double(sum_sq) / double(pixels);
        return 10.0 * std::log10(255.0 * 255.0 / mse);
    }
};

struct MovieResult {
    std::string code;
    int64_t pictures = 0;      // códigos de imagem no stream
    int64_t own_frames = 0;
    int64_t sys_frames = 0;
    int64_t compared = 0;
    PlaneStats plane[3];
    int64_t frames_diff = 0;   // quadros com algum pixel diferente
    int64_t frames_over2 = 0;  // quadros com uma diferença > 2
    int worst_max = 0;
    int64_t worst_frame = -1;
    int worst_type = 0;
    double gop_sum[20] = {};   // |diferença| média do Y pela posição no GOP (quadros desde o último I)
    int64_t gop_count[20] = {};
    double gop_max[20] = {};
    int64_t order_errors = 0;
    uint64_t errors = 0, concealed = 0, unsupported = 0, dropped = 0;
    double own_ms = 0.0, sys_ms = 0.0;
    int64_t audio_frames = 0;  // amostras estéreo do som
    int64_t types[4] = {};
    // FNV-1a de 64 bits de todos os quadros do port (Y, Cb, Cr): o mesmo em qualquer CPU e compilador (o decodificador
    // só faz contas exatas), para conferir outra plataforma contra esta.
    uint64_t hash = 0xcbf29ce484222325ull;
};

// Os planos crus (I420: Y, Cb, Cr) para análise.
void dump_raw(const std::wstring& path, const Planes& p) {
    FILE* f = nf::os::open_file(path, "wb");
    if (f == nullptr) return;
    std::fwrite(p.y.data(), 1, p.y.size(), f);
    std::fwrite(p.cb.data(), 1, p.cb.size(), f);
    std::fwrite(p.cr.data(), 1, p.cr.size(), f);
    std::fclose(f);
}

void dump_png(const std::wstring& path, const Planes& p, bool diff_of = false, const Planes* other = nullptr) {
    // NV12 de volta para usar a conversão do IPU.
    const int w = p.w, h = p.h;
    std::vector<uint8_t> nv12(size_t(w) * size_t(h) * 3 / 2);
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        int v = p.y[i];
        if (diff_of) v = std::min(255, 16 + std::abs(int(p.y[i]) - int(other->y[i])) * 16);
        nv12[i] = uint8_t(v);
    }
    uint8_t* uv = nv12.data() + size_t(w) * size_t(h);
    for (size_t i = 0; i < size_t(w) * size_t(h) / 4; ++i) {
        if (diff_of) {
            uv[2 * i] = uint8_t(std::clamp(128 + (int(p.cb[i]) - int(other->cb[i])) * 16, 0, 255));
            uv[2 * i + 1] = uint8_t(std::clamp(128 + (int(p.cr[i]) - int(other->cr[i])) * 16, 0, 255));
        } else {
            uv[2 * i] = p.cb[i];
            uv[2 * i + 1] = p.cr[i];
        }
    }
    std::vector<uint8_t> bgra(size_t(w) * size_t(h) * 4);
    nf::nv12_to_bgra(nv12.data(), w, h, bgra.data(), 0xff);
    nf::write_png_bgra(path, w, h, bgra.data());
}

struct Options {
    std::string root = ".";
    std::string movie;
    bool own_only = false;
    bool ref_idct = false;
    std::string dump;
    int64_t dump_from = 0, dump_to = 10;
    bool quiet = false;
    int64_t where = -1;  // --where <quadro> [--where-min d]: os pixels que diferem nesse quadro
    int where_min = 2;
};

MovieResult check_movie(const Options& opt, const std::wstring& path, const std::string& code) {
    MovieResult r;
    r.code = code;
    nf::PssFile file;
    if (!file.open(path)) {
        std::printf("%s: não abriu\n", code.c_str());
        return r;
    }
    std::deque<Planes> own_q, sys_q;
    nf::Mpeg2VideoDecoder own;
    own.set_reference_idct(opt.ref_idct);
    own.set_output([&](const nf::Mpeg2Picture& p) {
        Planes f;
        f.w = p.width;
        f.h = p.height;
        f.y.resize(size_t(p.width) * size_t(p.height));
        f.cb.resize(size_t(p.chroma_width) * size_t(p.chroma_height));
        f.cr.resize(f.cb.size());
        for (int y = 0; y < p.height; ++y) std::memcpy(&f.y[size_t(y * p.width)], p.y + y * p.y_stride, size_t(p.width));
        for (int y = 0; y < p.chroma_height; ++y) {
            std::memcpy(&f.cb[size_t(y * p.chroma_width)], p.cb + y * p.c_stride, size_t(p.chroma_width));
            std::memcpy(&f.cr[size_t(y * p.chroma_width)], p.cr + y * p.c_stride, size_t(p.chroma_width));
        }
        f.type = p.coding_type;
        f.tr = p.temporal_reference;
        own_q.push_back(std::move(f));
    });
    nf::Mpeg2Decoder sys;
    bool sys_ok = false;
    int64_t index = 0;
    int last_tr = -1;
    int64_t since_i = 0;
    std::vector<uint8_t> unit;
    nf::MovieFrameNV12 frame;
    std::vector<int16_t> pcm(48000 * 2);

    auto compare_ready = [&]() {
        while (!own_q.empty() && (opt.own_only || !sys_q.empty())) {
            const Planes& a = own_q.front();
            // A ordem de exibição do port: temporal_reference sobe de 1 em 1 e volta a 0 num GOP novo.
            if (last_tr >= 0 && a.tr != last_tr + 1 && a.tr != 0) ++r.order_errors;
            last_tr = a.tr;
            if (a.type == 1) since_i = 0;
            if (a.type >= 1 && a.type <= 3) ++r.types[a.type];
            for (const std::vector<uint8_t>* plane : {&a.y, &a.cb, &a.cr}) {
                for (uint8_t v : *plane) r.hash = (r.hash ^ v) * 0x100000001b3ull;
            }
            if (!opt.own_only) {
                const Planes& b = sys_q.front();
                int fmax = 0;
                uint64_t fsum[3] = {};
                if (a.w == b.w && a.h == b.h) {
                    r.plane[0].add(a.y, b.y, &fmax, &fsum[0]);
                    r.plane[1].add(a.cb, b.cb, &fmax, &fsum[1]);
                    r.plane[2].add(a.cr, b.cr, &fmax, &fsum[2]);
                } else {
                    fmax = 255;
                }
                ++r.compared;
                if (fmax > 0) ++r.frames_diff;
                if (fmax > 2) ++r.frames_over2;
                if (fmax > r.worst_max) {
                    r.worst_max = fmax;
                    r.worst_frame = index;
                    r.worst_type = a.type;
                }
                const int g = int(std::min<int64_t>(since_i, 19));
                const double m = a.y.empty() ? 0.0 : double(fsum[0]) / double(a.y.size());
                r.gop_sum[g] += m;
                r.gop_max[g] = std::max(r.gop_max[g], double(fmax));
                ++r.gop_count[g];
                if (opt.where >= 0 && index == opt.where) {
                    // Os pixels que mais diferem (depuração).
                    const std::vector<uint8_t>* pa[3] = {&a.y, &a.cb, &a.cr};
                    const std::vector<uint8_t>* pb[3] = {&b.y, &b.cb, &b.cr};
                    for (int pl = 0; pl < 3; ++pl) {
                        const int w = pl == 0 ? a.w : a.w / 2;
                        int shown = 0;
                        for (size_t i = 0; i < pa[pl]->size() && shown < 40; ++i) {
                            const int d = int((*pa[pl])[i]) - int((*pb[pl])[i]);
                            if (std::abs(d) < opt.where_min) continue;
                            std::printf("    quadro %lld plano %d x %d y %d port %d sistema %d\n",
                                        static_cast<long long>(index), pl, int(i % size_t(w)), int(i / size_t(w)),
                                        (*pa[pl])[i], (*pb[pl])[i]);
                            ++shown;
                        }
                    }
                }
                if (!opt.dump.empty() && index >= opt.dump_from && index <= opt.dump_to) {
                    const std::wstring base = nf::os::join(widen(opt.dump), widen(code + "_" + std::to_string(index)));
                    dump_png(base + L"_port.png", a);
                    dump_png(base + L"_sistema.png", b);
                    dump_png(base + L"_dif.png", a, true, &b);
                    dump_raw(base + L"_port.yuv", a);
                    dump_raw(base + L"_sistema.yuv", b);
                }
                sys_q.pop_front();
            }
            ++since_i;
            ++index;
            own_q.pop_front();
        }
    };

    while (true) {
        if (file.video().units_ready() == 0) {
            if (file.eof()) break;
            file.pump(0, 8);
            // O som: só a conta das amostras.
            while (file.audio().frames_ready() > 0) {
                r.audio_frames += int64_t(file.audio().take(pcm.data(), pcm.size() / 2));
            }
            continue;
        }
        file.video().next(unit);
        if (!opt.own_only && !sys_ok) {
            sys_ok = sys.open(file.video().width(), file.video().height(), file.video().fps(),
                              nf::MovieDecoderChoice::System);
            if (!sys_ok) {
                std::printf("%s: sem o decodificador do sistema (%s)\n", code.c_str(), sys.failure().c_str());
                return r;
            }
        }
        auto t0 = Clock::now();
        own.decode(unit.data(), unit.size());
        auto t1 = Clock::now();
        r.own_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!opt.own_only) {
            t0 = Clock::now();
            sys.feed(unit.data(), unit.size());
            while (sys.take(frame)) {
                sys_q.push_back(from_nv12(frame));
                ++r.sys_frames;
            }
            t1 = Clock::now();
            r.sys_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        r.own_frames = int64_t(own.stats().output);
        compare_ready();
    }
    while (file.audio().frames_ready() > 0) r.audio_frames += int64_t(file.audio().take(pcm.data(), pcm.size() / 2));
    own.finish();
    r.own_frames = int64_t(own.stats().output);
    if (!opt.own_only) {
        sys.drain();
        while (sys.take(frame)) {
            sys_q.push_back(from_nv12(frame));
            ++r.sys_frames;
        }
    }
    compare_ready();
    r.pictures = int64_t(file.video().pictures());
    r.errors = own.stats().errors;
    r.concealed = own.stats().concealed;
    r.unsupported = own.stats().unsupported;
    r.dropped = own.stats().dropped;
    if (r.errors != 0) std::printf("%s: erro do fluxo: %s\n", code.c_str(), own.last_error().c_str());
    return r;
}

int main_body(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--movie") opt.movie = next();
        else if (a == "--own-only") opt.own_only = true;
        else if (a == "--ref-idct") opt.ref_idct = true;
        else if (a == "--dump") opt.dump = next();
        else if (a == "--frames") {
            const std::string v = next();
            const size_t dash = v.find('-');
            opt.dump_from = std::stoll(v.substr(0, dash));
            opt.dump_to = dash == std::string::npos ? opt.dump_from : std::stoll(v.substr(dash + 1));
        } else if (a == "--quiet") opt.quiet = true;
        else if (a == "--where") opt.where = std::stoll(next());
        else if (a == "--where-min") opt.where_min = std::stoi(next());
        else if (!a.empty() && a[0] != '-') opt.root = a;
        else {
            std::printf("opção desconhecida: %s\n", a.c_str());
            return 1;
        }
    }
    const std::wstring dir = nf::os::join(widen(opt.root), L"MOVIES/30_FPS");
    std::vector<std::string> codes;
    for (const nf::os::DirEntry& e : nf::os::list_dir(dir)) {
        if (e.dir || e.name.size() != 12) continue;
        std::string n;
        for (wchar_t c : e.name) n += char(std::toupper(static_cast<unsigned char>(c & 0x7f)));
        if (n.substr(8) != ".PSS") continue;
        codes.push_back(n.substr(0, 8));
    }
    std::sort(codes.begin(), codes.end());
    if (!opt.movie.empty()) {
        std::string m = opt.movie;
        for (char& c : m) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (m.size() < 8) m = std::string(8 - m.size(), '0') + m;
        codes.assign(1, m);
    }
    if (codes.empty()) {
        std::printf("nenhum filme em %ls\n", dir.c_str());
        return 2;
    }
    if (!opt.dump.empty()) nf::os::make_dir(widen(opt.dump));
    if (!opt.own_only) {
        // Sem o decodificador do sistema (a build headless, outra plataforma, o Windows sem a extensão): só o do port.
        nf::Mpeg2Decoder probe;
        if (!probe.open(512, 448, 30.0, nf::MovieDecoderChoice::System)) {
            std::printf("sem o decodificador do sistema (%s): só o do port\n", probe.failure().c_str());
            opt.own_only = true;
        }
    }
    std::printf("decodificador do port%s contra o do sistema%s\n", opt.ref_idct ? " (IDCT de referência)" : "",
                opt.own_only ? " (desligado: --own-only)" : "");
    std::printf("%-8s %6s %6s %6s %5s/%5s/%5s %4s %4s %4s %8s %8s %8s %6s %6s %6s %6s %5s %6s %6s %8s\n", "filme",
                "quadros", "port", "sist", "I", "P", "B", "maxY", "maxU", "maxV", "medY", "medU", "medV", "psnrY",
                "psnrU", "psnrV", "q_dif", "q>2", "ordem", "erros", "som_q");
    MovieResult total;
    int bad = 0;
    double own_ms = 0.0, sys_ms = 0.0;
    int64_t own_frames = 0;
    std::vector<MovieResult> results;
    for (const std::string& code : codes) {
        const MovieResult r = check_movie(opt, nf::os::join(dir, widen(code + ".PSS")), code);
        results.push_back(r);
        const double som_q = double(r.audio_frames) / 1600.0;
        std::printf("%-8s %6lld %6lld %6lld %5lld/%5lld/%5lld %4d %4d %4d %8.5f %8.5f %8.5f %6.2f %6.2f %6.2f %6lld %5lld "
                    "%6lld %6llu %8.1f\n",
                    code.c_str(), static_cast<long long>(r.pictures), static_cast<long long>(r.own_frames),
                    static_cast<long long>(r.sys_frames), static_cast<long long>(r.types[1]),
                    static_cast<long long>(r.types[2]), static_cast<long long>(r.types[3]), r.plane[0].max_abs,
                    r.plane[1].max_abs, r.plane[2].max_abs, r.plane[0].mean(), r.plane[1].mean(), r.plane[2].mean(),
                    r.plane[0].psnr(), r.plane[1].psnr(), r.plane[2].psnr(), static_cast<long long>(r.frames_diff),
                    static_cast<long long>(r.frames_over2), static_cast<long long>(r.order_errors),
                    static_cast<unsigned long long>(r.errors + r.unsupported), som_q);
        if (!opt.quiet && !opt.own_only) {
            std::printf("    pior quadro %lld (tipo %d) max %d | |dY| médio pela posição no GOP:", static_cast<long long>(r.worst_frame),
                        r.worst_type, r.worst_max);
            for (int g = 0; g < 20; ++g) {
                if (r.gop_count[g] == 0) continue;
                std::printf(" %d:%.4f/%g", g, r.gop_sum[g] / double(r.gop_count[g]), r.gop_max[g]);
            }
            std::printf("\n");
        }
        if (!opt.quiet) {
            std::printf("    tempo por quadro: port %.3f ms%s", r.own_frames ? r.own_ms / double(r.own_frames) : 0.0,
                        opt.own_only ? "" : ",");
            if (!opt.own_only) std::printf(" sistema %.3f ms", r.sys_frames ? r.sys_ms / double(r.sys_frames) : 0.0);
            std::printf(" | relógio: %lld quadros de vídeo, som %.2f quadros (%lld amostras), o último quadro em %lld "
                        "amostras | ocultados %llu sem_ref %llu | hash do port %016llx\n",
                        static_cast<long long>(r.own_frames), som_q, static_cast<long long>(r.audio_frames),
                        static_cast<long long>(r.own_frames > 0 ? (r.own_frames - 1) * 1600 : 0),
                        static_cast<unsigned long long>(r.concealed), static_cast<unsigned long long>(r.dropped),
                        static_cast<unsigned long long>(r.hash));
        }
        own_ms += r.own_ms;
        sys_ms += r.sys_ms;
        own_frames += r.own_frames;
        for (int p = 0; p < 3; ++p) {
            total.plane[p].max_abs = std::max(total.plane[p].max_abs, r.plane[p].max_abs);
            total.plane[p].sum_abs += r.plane[p].sum_abs;
            total.plane[p].sum_sq += r.plane[p].sum_sq;
            total.plane[p].pixels += r.plane[p].pixels;
            total.plane[p].differing += r.plane[p].differing;
        }
        total.compared += r.compared;
        total.frames_diff += r.frames_diff;
        total.frames_over2 += r.frames_over2;
        total.pictures += r.pictures;
        total.own_frames += r.own_frames;
        total.sys_frames += r.sys_frames;
        for (int g = 0; g < 20; ++g) {
            total.gop_sum[g] += r.gop_sum[g];
            total.gop_count[g] += r.gop_count[g];
            total.gop_max[g] = std::max(total.gop_max[g], r.gop_max[g]);
        }
        const bool count_ok = r.own_frames == r.pictures && (opt.own_only || r.sys_frames == r.own_frames);
        if (!count_ok || r.errors != 0 || r.unsupported != 0 || r.order_errors != 0) ++bad;
    }
    std::printf("total: %lld quadros no stream, port %lld, sistema %lld, comparados %lld; maior diferença Y %d Cb %d Cr "
                "%d; média |d| Y %.5f Cb %.5f Cr %.5f; PSNR Y %.2f Cb %.2f Cr %.2f dB; pixels diferentes Y %.4f%% Cb "
                "%.4f%% Cr %.4f%%; quadros com diferença %lld, com diferença > 2 %lld\n",
                static_cast<long long>(total.pictures), static_cast<long long>(total.own_frames),
                static_cast<long long>(total.sys_frames), static_cast<long long>(total.compared), total.plane[0].max_abs,
                total.plane[1].max_abs, total.plane[2].max_abs, total.plane[0].mean(), total.plane[1].mean(),
                total.plane[2].mean(), total.plane[0].psnr(), total.plane[1].psnr(), total.plane[2].psnr(),
                total.plane[0].pixels ? 100.0 * double(total.plane[0].differing) / double(total.plane[0].pixels) : 0.0,
                total.plane[1].pixels ? 100.0 * double(total.plane[1].differing) / double(total.plane[1].pixels) : 0.0,
                total.plane[2].pixels ? 100.0 * double(total.plane[2].differing) / double(total.plane[2].pixels) : 0.0,
                static_cast<long long>(total.frames_diff), static_cast<long long>(total.frames_over2));
    if (!opt.own_only) {
        std::printf("|dY| médio pela posição no GOP (todos):");
        for (int g = 0; g < 20; ++g) {
            if (total.gop_count[g] == 0) continue;
            std::printf(" %d:%.4f/%g", g, total.gop_sum[g] / double(total.gop_count[g]), total.gop_max[g]);
        }
        std::printf("\n");
    }
    std::printf("tempo por quadro (decodificar, sem a conversão para BGRA): port %.3f ms%s", own_frames ? own_ms / double(own_frames) : 0.0,
                opt.own_only ? "\n" : "");
    if (!opt.own_only) std::printf(", sistema %.3f ms\n", total.sys_frames ? sys_ms / double(total.sys_frames) : 0.0);
    std::printf("%d filme(s) com problema\n", bad);
    return bad == 0 ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    return nf::run_main([&] { return main_body(argc, argv); });
}
