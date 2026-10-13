// Os filmes: as funções do ACTION.ELF e o tocador do PC por baixo delas. Ver psi_movie.h.

#include "platform/psi_movie.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <vector>

#include "platform/audio_out.h"
#include "platform/movie_decoder.h"
#include "platform/os.h"
#include "platform/psi.h"
#include "platform/pss.h"
#include "platform/sfx_iop.h"

uint32_t BGFMVPlaying = 0;
uint8_t LoopingMovie = 0;
uint32_t MoviePlaying = 0;
uint32_t MovieVolume = 0;
uint32_t ImageToUpload = 0;
uint32_t MovieFrame = 0;

namespace {

using namespace nf;

constexpr int kRate = 48000;
constexpr size_t kAudioAhead = 48000;        // a entrada da SPU2 fica com até 1 s de som
constexpr size_t kProbeBytes = 1u << 20;     // sem áudio no primeiro 1 MB: um filme sem som (os fundos do menu)
constexpr size_t kPumpBytes = 4u << 20;      // o máximo lido de uma vez (um tick)
constexpr int64_t kVideoAhead = 30;          // quadros do vídeo separados adiante do relógio (1 s)
constexpr int kMaxDecodePerCall = 300;       // um salto do relógio (uma pausa longa) decodifica aos poucos

struct Player {
    PssFile file;
    MovieClock clock;
    double fps = 30.0;
    int64_t units_fed = 0;     // unidades entregues ao decodificador
    int64_t decoded = 0;       // quadros que saíram dele (ordem de exibição)
    int64_t shown = -1;        // o quadro da imagem
    bool drained = false;
    bool audio_started = false;  // isStarted
    std::vector<uint8_t> image;  // BGRA
    int width = 0, height = 0;
    uint64_t serial = 0;
    MovieFrameNV12 nv12;
    std::vector<uint8_t> unit;
    std::vector<int16_t> pcm;
};

Player g_p;
Mpeg2Decoder g_decoder;
MovieDecoderChoice g_decoder_choice = MovieDecoderChoice::Auto;  // [video] movie_decoder, --movie-decoder
bool g_decoder_failed = false;
bool g_enabled = false;
bool g_decode = true;
std::shared_ptr<const DiscSource> g_disc;  // de onde vêm os filmes (nulo: nenhum abre)
std::string g_movie_dir;                   // o diretório deles no disco
MovieStats g_stats;

// Lê o arquivo adiante; enquanto o som ainda não apareceu (o primeiro 1 MB), também procura por ele.
void pump(size_t audio_frames, size_t video_units) {
    PssFile& f = g_p.file;
    const bool want_audio = f.has_audio() || f.bytes_read() < kProbeBytes;
    f.pump(want_audio ? audio_frames : 0, video_units, kPumpBytes);
}

// O som do filme que já saiu da caixa: o consumido da entrada da SPU2 menos a fila do aparelho.
int64_t heard_audio() { return movie_audio_heard(int64_t(spu().input_consumed()), audio_out_queued_frames()); }
// O relógio da SPU2 ouvido: tudo o que ela misturou menos a fila do aparelho (800 amostras por VBlank do jogo).
int64_t heard_spu() { return movie_audio_heard(int64_t(spu().samples_rendered()), audio_out_queued_frames()); }

// O quadro do relógio (sem limite no fim).
int64_t clock_frame() { return movie_frame_at_audio(g_p.clock.position(heard_audio(), heard_spu()), kRate, g_p.fps); }

// Os quadros do vídeo, quando o arquivo já acabou (-1 antes).
int64_t total_frames() {
    const Mpeg2Units& v = g_p.file.video();
    return g_p.file.eof() && v.finished() ? int64_t(v.pictures()) : -1;
}

std::string movie_path(uint32_t movie) {
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.PSS", movie);
    return g_movie_dir.empty() ? std::string(name) : g_movie_dir + "/" + name;
}

// [decomp] 0x00108cc0 strFileOpen
// No PS2: o caminho do CD em maiúsculas ("\MOVIES\30_FPS\07100001.PSS;1") e o streaming do CD (sceCdStInit), ou um
// arquivo do host; com 07360048 que não abre, um laço infinito (não existe aqui). No PC: o arquivo da pasta dos filmes,
// e só se o decodificador do vídeo existir (sem ele o filme não abre, como um arquivo que falta: só com
// --movie-decoder mf num sistema sem o do Media Foundation; com auto o do port entra no lugar). Lê o começo até o
// cabeçalho de sequência, para ligar o decodificador no tamanho do vídeo.
bool strFileOpen(uint32_t movie) {
    if (!g_enabled) return false;
    PssFile& f = g_p.file;
    if (g_disc == nullptr || !f.open(g_disc->open(movie_path(movie)))) return false;
    while (f.video().width() == 0 && !f.eof() && f.bytes_read() < kProbeBytes) f.pump(0, 1, kPumpBytes);
    const int w = f.video().width(), h = f.video().height();
    if (w <= 0 || h <= 0) return false;
    if (!g_decode) return true;
    if (g_decoder_failed) return false;
    // Um decodificador por processo, religado se o tamanho mudar (todos os filmes do disco são 512 x 448).
    if (!g_decoder.ok() || g_p.width != w || g_p.height != h) {
        if (!g_decoder.open(w, h, f.video().fps(), g_decoder_choice)) {
            g_decoder_failed = true;
            std::fprintf(stderr, "filmes: sem decodificador de MPEG-2 (%s); os filmes nao tocam\n",
                         g_decoder.failure().c_str());
            return false;
        }
        // Com auto, o do sistema não serviu e o do port entrou no lugar (no Windows sem a "MPEG-2 Video Extension").
        if (g_decoder.own() && g_decoder_choice == MovieDecoderChoice::Auto) {
            std::fprintf(stderr, "filmes: o decodificador de MPEG-2 do sistema nao serve (%s); o do port decodifica\n",
                         g_decoder.failure().c_str());
        }
    }
    g_p.width = w;
    g_p.height = h;
    return true;
}

// [decomp] 0x00108f90 strFileClose
void strFileClose() { g_p.file.close(); }

// [decomp] 0x001072b8 audioDecCreate
// No PS2: zera o AudioDec, manda o buffer de zeros ao IOP e põe o MVOL em 0x3fff. No PC: a entrada da SPU2 vazia.
void audioDecCreate() {
    spu().input_stop();
    changeMasterVolume(0x3fff);
}

// [decomp] 0x00107360 audioDecStart
// O BVOL do núcleo 0 em MovieVolume x 100 (100 -> 10000 = 0x2710: 0.305 no formato de 16 bits com sinal) e a
// transferência em bloco em laço (no PC, a entrada passa a ser consumida).
void audioDecStart() {
    changeInputVolume(uint32_t(uint16_t(MovieVolume * 100)));
    spu().input_start();
    g_p.audio_started = true;
}

// [decomp] 0x001073d8 audioDecReset
// O BVOL em 0 e a transferência parada.
void audioDecReset() {
    changeInputVolume(0);
    g_stats.audio_consumed = spu().input_consumed();
    g_stats.audio_starved = spu().input_starved();
    spu().input_stop();
    g_p.audio_started = false;
}

// O som separado vai para a entrada da SPU2 até ela ter 1 s (no PS2, audioDecSendToIOP manda ao anel do IOP o que a
// transferência já tocou).
void audioDecSendToIOP() {
    PssAudio& a = g_p.file.audio();
    for (int guard = 0; guard < 64 && spu().input_queued() < kAudioAhead; ++guard) {
        if (a.frames_ready() == 0) {
            if (g_p.file.eof()) break;
            pump(kAudioAhead - spu().input_queued(), 0);
            if (a.frames_ready() == 0) break;
        }
        const size_t n = std::min(a.frames_ready(), kAudioAhead - spu().input_queued());
        g_p.pcm.resize(n * 2);
        const size_t got = a.take(g_p.pcm.data(), n);
        spu().input_push(g_p.pcm.data(), got);
    }
}

// [decomp] 0x001083a8 StartUpBGFMV
// No PS2: cria o sceMpeg com os callbacks (vídeo, áudio, sem dados, fundo), o AudioDec e enche o buffer do
// demultiplexador. No PC: o decodificador limpo, o começo do arquivo separado (até 0.5 s de som, ou o primeiro 1 MB
// sem som: um filme mudo) e o relógio no VBlank de agora.
void StartUpBGFMV() {
    MovieFrame = 1;
    audioDecCreate();
    g_p.units_fed = 0;
    g_p.decoded = 0;
    g_p.shown = -1;
    g_p.drained = false;
    g_p.audio_started = false;
    if (g_decode) g_decoder.flush();
    PssFile& f = g_p.file;
    while (!f.eof() && f.bytes_read() < kProbeBytes && !f.has_audio()) f.pump(1, 2, kPumpBytes);
    pump(kRate / 2, 2);
    g_p.clock = MovieClock{};
    g_p.clock.has_audio = f.has_audio() && !f.audio().error();
    g_p.clock.spu_start = heard_spu();
    g_p.fps = f.video().fps() > 0.0 ? f.video().fps() : 30.0;
}

// [decomp] 0x00108700 CleanUpBGFMV
void CleanUpBGFMV() {
    audioDecReset();
    if (g_decode) g_decoder.flush();
}

// [decomp] 0x00108550 DecodeBGFMVFrame
// No PS2: um quadro por tick (sceMpegGetPicture para RGBA32, as tags que a copiam centrada na tela de 512 x 448), o
// som para o IOP (audioDecSendToIOP) e o começo dele quando o IOP tem 0x6000 bytes; 1 no fim do vídeo
// (sceMpegIsEnd). No PC o quadro sai em movie_image, pelo relógio do som; aqui o som vai para a SPU2 (e começa logo:
// divergência, o vídeo e o som juntos desde o primeiro quadro), o vídeo é separado um segundo adiante e o fim é o
// último quadro mostrado pelo tempo dele.
int32_t DecodeBGFMVFrame() {
    if (!g_p.file.is_open()) return 1;
    const int64_t spu_now = heard_spu();
    const int64_t frame = clock_frame();
    const int64_t want = frame + kVideoAhead - g_p.units_fed;
    if (!g_p.file.eof() && want > 0) pump(0, size_t(want));
    if (g_p.clock.has_audio) {
        audioDecSendToIOP();
        if (!g_p.audio_started) audioDecStart();
        // O som acabou (o arquivo e a entrada vazios): o resto do vídeo segue pelos VBlanks.
        if (!g_p.clock.audio_done && g_p.file.eof() && g_p.file.audio().frames_ready() == 0 &&
            spu().input_queued() == 0) {
            g_p.clock.audio_end = heard_audio();
            g_p.clock.spu_audio_end = spu_now;
            g_p.clock.audio_done = true;
        }
    }
    ImageToUpload = 1;
    const int64_t total = total_frames();
    g_stats.clock_frame = clock_frame();
    g_stats.pictures = int64_t(g_p.file.video().pictures());
    if (!g_decode) {
        // Sem decodificador (testes): as unidades até o quadro do relógio contam como mostradas.
        while (g_p.units_fed <= g_stats.clock_frame && g_p.file.video().next(g_p.unit)) ++g_p.units_fed;
        g_p.shown = total >= 0 ? std::min(g_stats.clock_frame, total - 1) : g_stats.clock_frame;
        g_stats.frame = g_p.shown;
        g_stats.decoded = g_p.units_fed;
    }
    return total >= 0 && g_stats.clock_frame >= total ? 1 : 0;
}

// [decomp] 0x001087c8 SwapBGFMVFrame
// No PS2: com folga no VBlank, mais do arquivo (fillBuff); devolve drawid. No PC: o vídeo adiante, sem esperar.
int32_t SwapBGFMVFrame() {
    if (g_p.file.is_open() && !g_p.file.eof()) {
        const int64_t want = clock_frame() + kVideoAhead - g_p.units_fed;
        if (want > 0) pump(0, size_t(want));
    }
    return 0;
}

// Decodifica até o quadro `target` (o último que sair vira a imagem).
void decode_to(int64_t target) {
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t before = g_p.decoded;
    for (int n = 0; g_p.decoded <= target && n < kMaxDecodePerCall * 4; ++n) {
        if (g_decoder.take(g_p.nv12)) {
            ++g_p.decoded;
            if (g_p.decoded - before >= kMaxDecodePerCall) break;
            continue;
        }
        if (g_p.file.video().next(g_p.unit)) {
            g_decoder.feed(g_p.unit.data(), g_p.unit.size());
            ++g_p.units_fed;
            continue;
        }
        if (!g_p.file.eof()) {
            pump(0, 4);
            continue;
        }
        if (!g_p.drained) {
            g_decoder.drain();
            g_p.drained = true;
            continue;
        }
        break;
    }
    if (g_p.decoded - 1 > g_p.shown && !g_p.nv12.data.empty()) {
        const MovieFrameNV12& f = g_p.nv12;
        g_p.image.resize(size_t(f.width) * size_t(f.height) * 4);
        nv12_to_bgra(f.data.data(), f.width, f.height, g_p.image.data());
        g_p.width = f.width;
        g_p.height = f.height;
        g_p.shown = g_p.decoded - 1;
        ++g_p.serial;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    g_stats.decode_ms += ms;
    g_stats.decode_ms_max = std::max(g_stats.decode_ms_max, ms);
    ++g_stats.decode_calls;
    g_stats.decoded = g_p.decoded;
    g_stats.frame = g_p.shown;
}

}  // namespace

// [decomp] 0x00107eb8 UsingMovie
// Os 23 filmes que o jogo conhece (07360048 não está no disco).
uint32_t UsingMovie(uint32_t movie) {
    switch (movie) {
        case 0x7100001: case 0x7100005: case 0x7100009: case 0x710000c: case 0x7100011: case 0x7100014:
        case 0x7100018:
        case 0x7300004: case 0x7300008: case 0x730000b: case 0x730000d: case 0x7300013: case 0x7300017:
        case 0x730001b:
        case 0x7330048: case 0x7350048: case 0x7360048: case 0x7370048: case 0x7380048: case 0x7390048:
        case 0x73a0048: case 0x73b0048: case 0x73f0048:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001081a8 psiStartBackgroundMovie
// Só um filme de UsingMovie; espera os sons do driver, para o anterior, guarda laço, código e volume e abre. Divergência:
// a memória (AllocateMovieMemory, ~2.3 MB do heap do jogo e 0x6800 bytes do IOP) é do tocador do PC, fora do heap.
void psiStartBackgroundMovie(uint32_t movie, int8_t loop, uint32_t volume) {
    if (UsingMovie(movie) == 0) return;
    PS2_SFXWaitTillFinished();
    psiStopBackgroundMovie();
    if (BGFMVPlaying != 0) return;
    LoopingMovie = uint8_t(loop);
    MoviePlaying = movie;
    MovieVolume = volume;
    if (!strFileOpen(movie)) {
        strFileClose();
        ImageToUpload = 0;
        ++g_stats.refused;
        return;
    }
    BGFMVPlaying = 1;
    ++g_stats.started;
    StartUpBGFMV();
}

// [decomp] 0x00108328 psiStopBackgroundMovie
void psiStopBackgroundMovie() {
    if (BGFMVPlaying == 0) return;
    CleanUpBGFMV();
    strFileClose();
    ImageToUpload = 0;
    BGFMVPlaying = 0;
    ++g_stats.stopped;
}

// [decomp] 0x001082e8 psiMovieFinished
bool psiMovieFinished() { return BGFMVPlaying == 0; }

// [decomp] 0x00108060 psiDecodeBackgroundMovieFrame
// Um quadro por tick (o main chama antes do GameFlow_Main enquanto o filme toca); no fim do vídeo, para (ou, em laço,
// fecha e abre o arquivo de novo e começa outra vez).
void psiDecodeBackgroundMovieFrame() {
    if (BGFMVPlaying == 0 || DecodeBGFMVFrame() != 1) return;
    CleanUpBGFMV();
    ++g_stats.finished;
    if (LoopingMovie == 0) {
        strFileClose();
        ImageToUpload = 0;
        BGFMVPlaying = 0;
        return;
    }
    strFileClose();
    if (strFileOpen(MoviePlaying)) {
        StartUpBGFMV();
        DecodeBGFMVFrame();
    }
}

// [decomp] 0x001082f8 psiSwapBGFMVFrame
int32_t psiSwapBGFMVFrame() {
    if (BGFMVPlaying == 0) return -1;
    return SwapBGFMVFrame();
}

// [decomp] 0x00107860 changeMasterVolume
// O MVOL dos dois núcleos (sceSdRemote SD_P_MVOLL/R); os filmes põem 0x3fff, o mesmo valor do driver de som.
void changeMasterVolume(uint32_t volume) { spu().set_master_volume(uint16_t(volume)); }

// [decomp] 0x001078c8 changeInputVolume
// O BVOL do núcleo 0 (SD_P_BVOLL/R: a entrada de dados, por onde o som do filme entra).
void changeInputVolume(uint32_t volume) { spu().set_input_volume(0, uint16_t(volume)); }

// [decomp] 0x00108740 PS2UploadBGMImage
// No PS2 (do VIFDMA, no começo do DMA de cada quadro): copia a imagem do filme para o buffer de quadro, antes do resto
// do desenho. No PC: diz se o renderizador desenha o quadro do filme como fundo (movie_image dá a imagem).
bool PS2UploadBGMImage() { return BGFMVPlaying != 0 && ImageToUpload != 0; }

namespace nf {

void movie_set_enabled(bool on) { g_enabled = on; }
bool movie_enabled() { return g_enabled; }
void movie_set_disc(std::shared_ptr<const DiscSource> disc, const std::string& dir) {
    g_disc = std::move(disc);
    g_movie_dir = dir;
}

void movie_set_dir(const std::wstring& dir) { movie_set_disc(dir.empty() ? nullptr : open_disc_folder(dir), std::string()); }
void movie_set_decode(bool on) { g_decode = on; }

void movie_set_decoder_choice(MovieDecoderChoice choice) {
    if (choice == g_decoder_choice) return;
    g_decoder_choice = choice;
    // O próximo filme liga o decodificador da escolha nova (um que falhou antes pode servir agora).
    if (BGFMVPlaying == 0) g_decoder.close();
    g_decoder_failed = false;
}

MovieDecoderChoice movie_decoder_choice() { return g_decoder_choice; }

bool movie_image(MovieImage* out) {
    if (!PS2UploadBGMImage() || !g_p.file.is_open() || !g_decode) return false;
    int64_t target = clock_frame();
    const int64_t total = total_frames();
    if (total > 0) target = std::min(target, total - 1);
    if (target > g_p.shown) decode_to(target);
    // NF_MOVIE_LOG=<arquivo> (depuração): por quadro de vídeo, o relógio do filme (o som consumido pela SPU2, a fila do
    // aparelho, o ouvido) e o quadro mostrado.
    static FILE* log = [] {
        std::wstring path;
        FILE* f = nullptr;
        if (nf::os::get_env(L"NF_MOVIE_LOG", &path)) f = nf::os::open_file(path, "w");
        if (f != nullptr) std::fprintf(f, "ms\tfilme\tconsumido\tfila\touvido\tspu\tquadro_relogio\tquadro\n");
        return f;
    }();
    if (log != nullptr) {
        static const auto t0 = std::chrono::steady_clock::now();
        std::fprintf(log, "%.3f\t%08x\t%llu\t%lld\t%lld\t%lld\t%lld\t%lld\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                     MoviePlaying, static_cast<unsigned long long>(spu().input_consumed()),
                     static_cast<long long>(audio_out_queued_frames()), static_cast<long long>(heard_audio()),
                     static_cast<long long>(heard_spu() - g_p.clock.spu_start), static_cast<long long>(target),
                     static_cast<long long>(g_p.shown));
    }
    if (g_p.image.empty() || g_p.shown < 0) return false;
    out->width = g_p.width;
    out->height = g_p.height;
    out->bgra = g_p.image.data();
    out->serial = g_p.serial;
    out->index = g_p.shown;
    out->code = MoviePlaying;
    out->looping = LoopingMovie != 0;
    return true;
}

const MovieStats& movie_stats() {
    if (BGFMVPlaying != 0) {
        g_stats.audio_consumed = spu().input_consumed();
        g_stats.audio_starved = spu().input_starved();
    }
    return g_stats;
}

void movie_reset_timing() {
    g_stats.decode_ms = 0.0;
    g_stats.decode_ms_max = 0.0;
    g_stats.decode_calls = 0;
}

std::string movie_decoder_name() {
    if (g_decoder.ok()) return g_decoder.name();
    if (g_decoder_failed) return "nenhum (" + g_decoder.failure() + ")";
    return "-";
}

std::string movie_describe() {
    const MovieStats& s = movie_stats();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "filme %08x tocando %u laco %u quadro %lld/%lld relogio %lld decodificados %lld som %s consumido %llu "
                  "faltas %llu bvol 0x%04x | abertos %u fim %u parados %u recusados %u ligados %d decodificador \"%s\" "
                  "ms %.3f/%.3f",
                  MoviePlaying, BGFMVPlaying, unsigned(LoopingMovie), static_cast<long long>(s.frame),
                  static_cast<long long>(s.pictures), static_cast<long long>(s.clock_frame),
                  static_cast<long long>(s.decoded), g_p.clock.has_audio ? (g_p.clock.audio_done ? "fim" : "sim") : "nao",
                  static_cast<unsigned long long>(s.audio_consumed), static_cast<unsigned long long>(s.audio_starved),
                  unsigned(spu().input_volume(0)), s.started, s.finished, s.stopped, s.refused, g_enabled ? 1 : 0,
                  movie_decoder_name().c_str(), s.decode_calls != 0 ? s.decode_ms / double(s.decode_calls) : 0.0,
                  s.decode_ms_max);
    return buf;
}

void movie_debug_power_on() {
    psiStopBackgroundMovie();
    g_p.file.close();
    g_p.clock = MovieClock{};
    g_p.fps = 30.0;
    g_p.units_fed = g_p.decoded = 0;
    g_p.shown = -1;
    g_p.drained = g_p.audio_started = false;
    g_p.image.clear();
    g_p.width = g_p.height = 0;
    g_p.serial = 0;
    g_stats = MovieStats{};
    g_enabled = false;
    g_decode = true;
    g_decoder_choice = MovieDecoderChoice::Auto;
    g_decoder_failed = false;
    g_decoder.close();
    BGFMVPlaying = 0;
    LoopingMovie = 0;
    MoviePlaying = 0;
    MovieVolume = 0;
    ImageToUpload = 0;
    MovieFrame = 0;
}

}  // namespace nf
