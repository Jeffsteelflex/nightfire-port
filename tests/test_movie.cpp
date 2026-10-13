// Testes dos filmes (platform/pss.h, platform/movie_decoder.h, platform/psi_movie.h): o demultiplexador num program
// stream montado à mão (pacotes, PES com PTS, o áudio cortado no meio de uma amostra, outro sub-stream ignorado), o
// áudio "SS" desintercalado, as unidades do vídeo para o decodificador, as contas do relógio (o quadro pelo som ouvido,
// pelos VBlanks sem som e depois do fim do som), a conversão de NV12 para BGRA (BT.601 de faixa limitada, como o CSC
// do IPU), a entrada de dados da SPU2 (BVOL, MMIX, faltas), e o tocador com as funções do jogo sobre um arquivo PSS
// feito à mão (sem o decodificador: cada quadro conta pelo relógio): o fim pelo vídeo, o laço, o BVOL que
// changeInputVolume põe, os pedidos recusados e as regras de pular: a dos menus (Menu_StopMovie) e a do GF_MOVIE das
// missões (o filme acaba ou Start, a ação 0x19).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu_frontend.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/audio_out.h"
#include "platform/movie_decoder.h"
#include "platform/os.h"
#include "platform/psi.h"
#include "platform/psi_movie.h"
#include "platform/pss.h"
#include "platform/sfx_iop.h"
#include "platform/spu2.h"
#include "test_framework.h"

namespace {

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
}

// Um pacote MPEG-2 como o do PSS (sem enchimento) com os PES dados: stream, PTS (ou -1), payload.
struct PesIn {
    uint8_t stream;
    int64_t pts;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> pack(const std::vector<PesIn>& pes) {
    std::vector<uint8_t> out = {0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x04, 0x00, 0x04, 0x01, 0x00, 0xd8, 0x43, 0xf8};
    for (const PesIn& p : pes) {
        std::vector<uint8_t> h = {0x83, uint8_t(p.pts >= 0 ? 0x80 : 0x00), uint8_t(p.pts >= 0 ? 0x0a : 0x05)};
        if (p.pts >= 0) {
            const uint64_t t = uint64_t(p.pts);
            h.push_back(uint8_t(0x21 | ((t >> 29) & 0xe)));
            h.push_back(uint8_t(t >> 22));
            h.push_back(uint8_t(((t >> 14) & 0xfe) | 1));
            h.push_back(uint8_t(t >> 7));
            h.push_back(uint8_t(((t << 1) & 0xfe) | 1));
        }
        for (int k = 0; k < 5; ++k) h.push_back(0xff);
        const size_t len = h.size() + p.payload.size();
        out.insert(out.end(), {0x00, 0x00, 0x01, p.stream, uint8_t(len >> 8), uint8_t(len)});
        out.insert(out.end(), h.begin(), h.end());
        out.insert(out.end(), p.payload.begin(), p.payload.end());
    }
    return out;
}

// O cabeçalho SShd/SSbd (PCM de 16 bits, 48 kHz) e o corpo em blocos de `inter` bytes por canal.
std::vector<uint8_t> ss_audio(const std::vector<int16_t>& left, const std::vector<int16_t>& right, int inter,
                              int channels = 2) {
    std::vector<uint8_t> a = {'S', 'S', 'h', 'd'};
    put32(a, 0x18);
    put32(a, 1);
    put32(a, 48000);
    put32(a, uint32_t(channels));
    put32(a, uint32_t(inter));
    put32(a, 0xffffffff);
    put32(a, 0xffffffff);
    std::vector<uint8_t> body;
    const size_t per = size_t(inter) / 2;
    for (size_t g = 0; g * per < left.size(); ++g) {
        for (int c = 0; c < channels; ++c) {
            const std::vector<int16_t>& s = c == 0 ? left : right;
            for (size_t k = 0; k < per; ++k) {
                const int16_t v = g * per + k < s.size() ? s[g * per + k] : 0;
                body.push_back(uint8_t(uint16_t(v)));
                body.push_back(uint8_t(uint16_t(v) >> 8));
            }
        }
    }
    a.insert(a.end(), {'S', 'S', 'b', 'd'});
    put32(a, uint32_t(body.size()));
    a.insert(a.end(), body.begin(), body.end());
    return a;
}

// Um vídeo elementar: cabeçalho de sequência 512 x 448, 4:3, 30 fps, um GOP e `pictures` quadros (I e depois P), com
// "fatias" de lixo entre eles.
std::vector<uint8_t> es_video(int pictures) {
    std::vector<uint8_t> v = {0x00, 0x00, 0x01, 0xb3, 0x20, 0x01, 0xc0, 0x25, 0x09, 0xc4, 0x22, 0x80};
    v.insert(v.end(), {0x00, 0x00, 0x01, 0xb8, 0x00, 0x08, 0x00, 0x40});
    for (int i = 0; i < pictures; ++i) {
        const uint8_t type = i == 0 ? 1 : 2;
        v.insert(v.end(), {0x00, 0x00, 0x01, 0x00, uint8_t(i >> 2), uint8_t((i << 6) | (type << 3)), 0xff, 0xf8});
        v.insert(v.end(), {0x00, 0x00, 0x01, 0x01});
        for (int k = 0; k < 20 + i; ++k) v.push_back(uint8_t(0x55 + k));
    }
    return v;
}

// Um PSS inteiro: o vídeo e o áudio (sem áudio se vazio) em PES de até `chunk` bytes, alternados, e o fim.
std::vector<uint8_t> pss(const std::vector<uint8_t>& video, const std::vector<uint8_t>& audio, size_t chunk) {
    std::vector<uint8_t> out;
    size_t vi = 0, ai = 0;
    bool first = true;
    while (vi < video.size() || ai < audio.size()) {
        std::vector<PesIn> pes;
        if (vi < video.size()) {
            const size_t n = std::min(chunk, video.size() - vi);
            pes.push_back({0xe0, first ? 6360 : -1, std::vector<uint8_t>(video.begin() + std::ptrdiff_t(vi),
                                                                         video.begin() + std::ptrdiff_t(vi + n))});
            vi += n;
        }
        if (ai < audio.size()) {
            const size_t n = std::min(chunk + 3, audio.size() - ai);  // um número ímpar de bytes por PES, como no disco
            std::vector<uint8_t> p = {0xff, 0xa0, 0x00, 0x00};
            p.insert(p.end(), audio.begin() + std::ptrdiff_t(ai), audio.begin() + std::ptrdiff_t(ai + n));
            pes.push_back({0xbd, first ? 6360 : -1, p});
            ai += n;
        }
        first = false;
        const std::vector<uint8_t> pk = pack(pes);
        out.insert(out.end(), pk.begin(), pk.end());
    }
    out.insert(out.end(), {0x00, 0x00, 0x01, 0xb9});
    return out;
}

std::filesystem::path movie_dir() {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "nf_tests_movies";
    std::filesystem::create_directories(d);
    return d;
}

void write_movie(uint32_t code, const std::vector<uint8_t>& data) {
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.PSS", code);
    FILE* f = nf::os::open_file((movie_dir() / name).wstring(), "wb");
    if (f == nullptr) return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// O tocador do jogo sem o decodificador, na pasta temporária, o relógio fixo no VBlank 100.
void fresh_player() {
    nf::sfx_iop_debug_power_on();
    nf::audio_out_debug_power_on();
    nf::movie_debug_power_on();
    nf::movie_set_enabled(true);
    nf::movie_set_decode(false);
    nf::movie_set_dir(movie_dir().wstring());
    psi_set_clock_vblanks(100.0);
}

void end_player() {
    nf::movie_debug_power_on();
    nf::sfx_iop_debug_power_on();
    psi_set_clock_vblanks(0.0);
}

// Um tick do main com o filme: a SPU2 mistura os 2 VBlanks (1600 amostras), o relógio anda e o quadro é decodificado.
void movie_tick(double& vblank) {
    nf::spu().render(nullptr, 1600);
    vblank += 2.0;
    psi_set_clock_vblanks(vblank);
    psiDecodeBackgroundMovieFrame();
}

}  // namespace

TEST(pss_demux_hand_built_stream) {
    std::vector<int16_t> l = {100, 200, 300, 400}, r = {-100, -200, -300, -400};
    const std::vector<uint8_t> audio = ss_audio(l, r, 4);
    const std::vector<uint8_t> video = es_video(2);
    // Vídeo em dois PES, o áudio cortado com 41 bytes no primeiro (no meio de uma amostra), outro sub-stream ignorado.
    std::vector<uint8_t> a1 = {0xff, 0xa0, 0, 0}, a2 = {0xff, 0xa0, 0, 0};
    a1.insert(a1.end(), audio.begin(), audio.begin() + 41);
    a2.insert(a2.end(), audio.begin() + 41, audio.end());
    const size_t half = video.size() / 2;
    std::vector<uint8_t> d = pack({{0xe0, 6360, {video.begin(), video.begin() + std::ptrdiff_t(half)}}, {0xbd, 6360, a1}});
    const std::vector<uint8_t> d2 = pack({{0xbd, -1, a2}, {0xe0, -1, {video.begin() + std::ptrdiff_t(half), video.end()}},
                                          {0xbd, -1, {0xff, 0xa1, 0, 0, 1, 2, 3}}, {0xbe, -1, {0xff, 0xff}}});
    d.insert(d.end(), d2.begin(), d2.end());
    d.insert(d.end(), {0x00, 0x00, 0x01, 0xb9});

    std::vector<uint8_t> got_video;
    nf::PssAudio got_audio;
    size_t pos = 0, packs_other = 0;
    int ptses = 0;
    bool ended = false;
    while (pos < d.size()) {
        size_t used = 0;
        nf::PssPes pes;
        const nf::PssStep st = nf::pss_next(d.data() + pos, d.size() - pos, &used, &pes);
        CHECK(st != nf::PssStep::Error && st != nf::PssStep::NeedMore);
        if (st == nf::PssStep::Error || st == nf::PssStep::NeedMore) break;
        pos += used;
        if (st == nf::PssStep::Other) ++packs_other;
        if (st == nf::PssStep::End) ended = true;
        if (st != nf::PssStep::Pes) continue;
        if (pes.has_pts) {
            CHECK(pes.pts == 6360);
            ++ptses;
        }
        if (pes.stream == 0xe0) got_video.insert(got_video.end(), pes.data, pes.data + pes.size);
        if (pes.stream == 0xbd) got_audio.push_pes(pes.data, pes.size);
    }
    CHECK(ended && ptses == 2 && packs_other == 3);  // dois pacotes e o enchimento
    CHECK(got_video == video);
    CHECK(got_audio.header_ok() && got_audio.rate() == 48000 && got_audio.channels() == 2 && got_audio.interleave() == 4);
    int16_t pcm[8] = {};
    CHECK(got_audio.take(pcm, 8) == 4);
    const int16_t want[8] = {100, -100, 200, -200, 300, -300, 400, -400};
    CHECK(std::memcmp(pcm, want, sizeof(want)) == 0);
    // Faltando bytes: NeedMore sem consumir.
    size_t used = 99;
    nf::PssPes pes;
    CHECK(nf::pss_next(d.data(), 10, &used, &pes) == nf::PssStep::NeedMore && used == 0);
    CHECK(nf::pss_next(d.data() + 1, d.size() - 1, &used, &pes) == nf::PssStep::Error);
}

TEST(pss_audio_deinterleaves_blocks_byte_by_byte_and_checks_the_header) {
    // Intercalação 0x200 (256 amostras por bloco), três grupos: as amostras saem L, R na ordem de cada canal, mesmo
    // entregues um byte de cada vez.
    std::vector<int16_t> l(768), r(768);
    for (int i = 0; i < 768; ++i) {
        l[size_t(i)] = int16_t(i * 7 - 2000);
        r[size_t(i)] = int16_t(-i * 3 + 500);
    }
    const std::vector<uint8_t> a = ss_audio(l, r, 0x200);
    nf::PssAudio au;
    for (uint8_t b : a) au.push_bytes(&b, 1);
    CHECK(au.header_ok() && au.frames_ready() == 768 && au.frames_decoded() == 768);
    std::vector<int16_t> pcm(768 * 2);
    CHECK(au.take(pcm.data(), 768) == 768);
    bool ok = true;
    for (size_t i = 0; i < 768; ++i) ok = ok && pcm[2 * i] == l[i] && pcm[2 * i + 1] == r[i];
    CHECK(ok);
    // Um grupo incompleto espera o resto.
    nf::PssAudio part;
    part.push_bytes(a.data(), nf::PssAudio::kHeaderBytes + 0x200 + 10);
    CHECK(part.header_ok() && part.frames_ready() == 0);
    // Mono: o canal vai para os dois lados.
    const std::vector<uint8_t> m = ss_audio({1, 2}, {}, 4, 1);
    nf::PssAudio mono;
    mono.push_bytes(m.data(), m.size());
    int16_t two[4] = {};
    CHECK(mono.take(two, 2) == 2 && two[0] == 1 && two[1] == 1 && two[2] == 2 && two[3] == 2);
    // Cabeçalho errado (ou o ADPCM, tipo 0x10): erro, nada sai.
    std::vector<uint8_t> bad = a;
    bad[8] = 0x10;
    nf::PssAudio adpcm;
    adpcm.push_bytes(bad.data(), bad.size());
    CHECK(adpcm.error() && adpcm.frames_ready() == 0);
    // Os 4 bytes de cada PES do private stream 1: só o sub-stream 0xa0 conta.
    nf::PssAudio sub;
    std::vector<uint8_t> other = {0xff, 0xa1, 0, 0};
    other.insert(other.end(), a.begin(), a.end());
    sub.push_pes(other.data(), other.size());
    CHECK(!sub.header_ok());
}

TEST(mpeg2_units_split_at_pictures_with_their_headers) {
    const std::vector<uint8_t> es = es_video(5);
    // Entregue em pedaços de 3 bytes (códigos de início cortados entre pedaços).
    nf::Mpeg2Units u;
    for (size_t i = 0; i < es.size(); i += 3) u.push(es.data() + i, std::min<size_t>(3, es.size() - i));
    CHECK(u.width() == 512 && u.height() == 448 && u.aspect_code() == 2 && u.rate_code() == 5);
    CHECK(std::fabs(u.fps() - 30.0) < 1e-9);
    CHECK(u.pictures() == 5 && u.units_ready() == 4);  // o último quadro só fecha no fim
    u.finish();
    CHECK(u.units_ready() == 5);
    std::vector<uint8_t> all, unit;
    int n = 0;
    while (u.next(unit)) {
        // Cada unidade tem exatamente um quadro; a primeira começa no cabeçalho de sequência.
        int pics = 0;
        for (size_t i = 0; i + 3 < unit.size(); ++i) {
            pics += unit[i] == 0 && unit[i + 1] == 0 && unit[i + 2] == 1 && unit[i + 3] == 0 ? 1 : 0;
        }
        CHECK(pics == 1);
        if (n == 0) CHECK(unit[3] == 0xb3);
        all.insert(all.end(), unit.begin(), unit.end());
        ++n;
    }
    CHECK(n == 5 && all == es);  // nada perdido nem repetido
}

TEST(movie_clock_frame_by_heard_audio_and_by_vblanks) {
    // 30 quadros por segundo a 48 kHz: um quadro a cada 1600 amostras exatas.
    CHECK(nf::movie_frame_at_audio(0, 48000, 30.0) == 0);
    CHECK(nf::movie_frame_at_audio(1599, 48000, 30.0) == 0);
    CHECK(nf::movie_frame_at_audio(1600, 48000, 30.0) == 1);
    CHECK(nf::movie_frame_at_audio(48000 * 60, 48000, 30.0) == 1800);
    CHECK(nf::movie_frame_at_audio(-5, 48000, 30.0) == 0);
    CHECK(nf::movie_frame_at_audio(48000, 48000, 30000.0 / 1001.0) == 29);
    // Sem som: 2 VBlanks por quadro.
    CHECK(nf::movie_frame_at_vblanks(1.99, 30.0) == 0 && nf::movie_frame_at_vblanks(2.0, 30.0) == 1);
    // Ouvido = consumido pela SPU2 menos o que está na fila do aparelho, nunca negativo.
    CHECK(nf::movie_audio_heard(10000, 2400) == 7600);
    CHECK(nf::movie_audio_heard(1000, 2400) == 0);
    CHECK(nf::movie_audio_heard(1000, -5) == 1000);
    // O relógio: o som enquanto ele dura; sem som, os VBlanks desde o começo; depois do fim do som, o fim dele mais os
    // VBlanks desde então, a 800 amostras por VBlank.
    // O relógio: sem som, o da SPU2 desde o começo do filme; com som, o som do filme; depois do fim dele, o fim mais o
    // relógio da SPU2 desde então.
    nf::MovieClock c;
    c.spu_start = 8000;
    CHECK(c.position(123, 12800) == 4800);
    CHECK(c.position(123, 7000) == 0);
    c.has_audio = true;
    CHECK(c.position(123, 12800) == 123);
    c.audio_done = true;
    c.audio_end = 16000;
    c.spu_audio_end = 40000;
    CHECK(c.position(99999, 41600) == 17600);
    CHECK(c.position(99999, 39000) == 16000);
}

TEST(nv12_to_bgra_is_the_ipu_bt601_limited_range) {
    uint8_t bgr[3];
    nf::yuv_to_bgr(16, 128, 128, bgr);
    CHECK(bgr[0] == 0 && bgr[1] == 0 && bgr[2] == 0);
    nf::yuv_to_bgr(235, 128, 128, bgr);
    CHECK(bgr[0] == 255 && bgr[1] == 255 && bgr[2] == 255);
    // As cores primárias de 75% de BT.601 (Y, Cb, Cr) voltam ao RGB delas (191) em até 2.
    struct {
        int y, cb, cr, r, g, b;
    } bars[] = {{65, 100, 212, 191, 0, 0}, {112, 72, 58, 0, 191, 0}, {35, 212, 114, 0, 0, 191}, {180, 128, 128, 191, 191, 191}};
    for (const auto& c : bars) {
        nf::yuv_to_bgr(c.y, c.cb, c.cr, bgr);
        CHECK(std::abs(bgr[2] - c.r) <= 2 && std::abs(bgr[1] - c.g) <= 2 && std::abs(bgr[0] - c.b) <= 2);
    }
    // Uma imagem 4 x 2: o croma de cada bloco 2 x 2 vale para os quatro pixels, o alfa é 0x80.
    const uint8_t nv12[] = {16, 235, 16, 235, 235, 16, 235, 16, 128, 128, 90, 240};
    uint8_t out[4 * 2 * 4];
    nf::nv12_to_bgra(nv12, 4, 2, out);
    CHECK(out[0] == 0 && out[4] == 255 && out[3] == 0x80 && out[7] == 0x80);
    // O bloco da direita (Cb 90, Cr 240: vermelho) nos quatro pixels dele.
    for (int p : {2, 3, 6, 7}) CHECK(out[p * 4 + 2] > out[p * 4 + 1] && out[p * 4 + 2] > out[p * 4]);
    CHECK(out[3 * 4 + 2] == 255);  // Y 235 com o croma vermelho satura o R
}

TEST(spu2_movie_input_mixes_into_core0_by_bvol) {
    nf::Spu2 spu;
    spu.set_master_volume(0x3fff);
    // Uma onda de +-10000 alternados (o bloqueio de DC a deixa passar inteira).
    std::vector<int16_t> in(2 * 2000);
    for (size_t i = 0; i < 2000; ++i) in[2 * i] = in[2 * i + 1] = int16_t(i & 1 ? -10000 : 10000);
    spu.input_push(in.data(), 2000);
    CHECK(spu.input_queued() == 2000);
    std::vector<int16_t> out(2 * 256);
    // Desligada, nada é consumido; o BVOL 0 também cala.
    spu.render(out.data(), 256);
    CHECK(spu.input_queued() == 2000 && spu.input_consumed() == 0);
    spu.input_start();
    spu.render(out.data(), 256);
    CHECK(spu.input_consumed() == 256 && spu.input_queued() == 2000 - 256);
    int quiet = 0;
    for (int16_t v : out) quiet = std::max(quiet, std::abs(int(v)));
    CHECK(quiet < 4);
    // MovieVolume 100 -> BVOL 10000 (0.305): +-10000 sai em ~+-3050 nos dois lados (os mestres em 0x3fff e o AVOL do
    // núcleo 1 quase 1.0).
    spu.set_input_volume(0, 10000);
    spu.render(out.data(), 256);
    int peak_l = 0, peak_r = 0;
    for (int i = 64; i < 256; ++i) {
        peak_l = std::max(peak_l, std::abs(int(out[2 * size_t(i)])));
        peak_r = std::max(peak_r, std::abs(int(out[2 * size_t(i) + 1])));
    }
    CHECK(peak_l > 2950 && peak_l < 3100 && std::abs(peak_l - peak_r) < 4);
    // Vazia, entra silêncio e as faltas contam; input_stop esvazia e zera.
    spu.render(nullptr, 2000);
    CHECK(spu.input_queued() == 0 && spu.input_consumed() == 2000 && spu.input_starved() == 512);
    spu.input_push(in.data(), 10);
    spu.input_stop();
    CHECK(!spu.input_active() && spu.input_queued() == 0 && spu.input_consumed() == 0);
}

TEST(movie_player_plays_to_the_end_of_the_video_by_the_audio_clock) {
    fresh_player();
    // 10 quadros e 10.24 quadros de som (64 grupos de 256 amostras).
    std::vector<int16_t> l(16384, 1000), r(16384, -1000);
    write_movie(0x7370048, pss(es_video(10), ss_audio(l, r, 0x200), 4073));
    // Fora de UsingMovie, ou sem o arquivo: nada toca.
    psiStartBackgroundMovie(0x7370049, 0, 100);
    CHECK(BGFMVPlaying == 0);
    std::filesystem::remove(movie_dir() / "07380048.PSS");
    psiStartBackgroundMovie(0x7380048, 0, 100);
    CHECK(BGFMVPlaying == 0 && nf::movie_stats().refused == 1);
    // Desligados (capturas, --no-movies): o filme não abre, como antes.
    nf::movie_set_enabled(false);
    psiStartBackgroundMovie(0x7370048, 0, 100);
    CHECK(BGFMVPlaying == 0 && psiMovieFinished());
    nf::movie_set_enabled(true);

    psiStartBackgroundMovie(0x7370048, 0, 100);
    CHECK(BGFMVPlaying == 1 && !psiMovieFinished() && LoopingMovie == 0 && MoviePlaying == 0x7370048);
    CHECK(nf::spu().master_volume(0) == 0x3fff && nf::spu().master_volume(1) == 0x3fff);  // audioDecCreate
    double v = 100.0;
    psiDecodeBackgroundMovieFrame();
    // O som entrou na SPU2 com o BVOL de MovieVolume x 100.
    CHECK(nf::spu().input_volume(0) == 10000 && nf::spu().input_active() && nf::spu().input_queued() == 16384);
    CHECK(ImageToUpload == 1 && PS2UploadBGMImage());
    int ticks = 0;
    bool frames_ok = true;
    while (BGFMVPlaying != 0 && ticks < 40) {
        movie_tick(v);
        ++ticks;
        // Um quadro por tick: 1600 amostras consumidas cada.
        if (BGFMVPlaying != 0) frames_ok = frames_ok && nf::movie_stats().frame == std::min(ticks, 9);
    }
    CHECK(frames_ok);
    // O fim é o do vídeo: o tick em que o relógio passa do último quadro (10 x 1600 amostras).
    CHECK(ticks == 10 && nf::movie_stats().finished == 1 && nf::movie_stats().pictures == 10);
    CHECK(nf::spu().input_volume(0) == 0 && !nf::spu().input_active() && ImageToUpload == 0);  // audioDecReset
    end_player();
}

TEST(movie_player_follows_vblanks_after_the_audio_and_loops_silent_movies) {
    fresh_player();
    // O som acaba em 4 quadros, o vídeo tem 8: o resto anda pelos VBlanks.
    std::vector<int16_t> l(6400, 5), r(6400, 5);
    write_movie(0x7390048, pss(es_video(8), ss_audio(l, r, 0x200), 2000));
    psiStartBackgroundMovie(0x7390048, 0, 50);
    double v = 100.0;
    psiDecodeBackgroundMovieFrame();
    CHECK(nf::spu().input_volume(0) == 5000);
    int ticks = 0;
    while (BGFMVPlaying != 0 && ticks < 40) {
        movie_tick(v);
        ++ticks;
    }
    // 6400 amostras = 4 quadros pelo som (o resto do grupo de 256 conta: 6400 = 25 grupos), mais 4 pelos VBlanks.
    CHECK(ticks == 8 || ticks == 9);
    CHECK(nf::movie_stats().finished == 1);

    // Um fundo de página: sem som, em laço (Menu_PlayMovie(filme, 0, 1, ...)): no fim, o arquivo abre de novo.
    write_movie(0x7350048, pss(es_video(5), {}, 1000));
    psiStartBackgroundMovie(0x7350048, 1, 100);
    CHECK(BGFMVPlaying == 1 && LoopingMovie == 1);
    psiDecodeBackgroundMovieFrame();
    CHECK(nf::spu().input_volume(0) == 0 && !nf::spu().input_active());  // sem som, a entrada fica parada
    ticks = 0;
    while (nf::movie_stats().finished < 3 && ticks < 40) {
        movie_tick(v);
        ++ticks;
    }
    CHECK(BGFMVPlaying == 1 && nf::movie_stats().finished == 3 && ticks == 10);
    CHECK(nf::movie_stats().frame <= 1);  // recomeçou
    // Pedir o mesmo filme de novo para e recomeça (psiStartBackgroundMovie para o anterior).
    psiStartBackgroundMovie(0x7350048, 1, 100);
    CHECK(BGFMVPlaying == 1 && nf::movie_stats().stopped == 1 && nf::movie_stats().started == 3);
    psiStopBackgroundMovie();
    CHECK(BGFMVPlaying == 0 && psiMovieFinished() && nf::movie_stats().stopped == 2);
    end_player();
}

TEST(menu_movie_skip_rule_needs_skip_and_the_counter) {
    fresh_player();
    write_movie(0x73b0048, pss(es_video(30), {}, 1000));
    // P_ESTHERO_Handler: pulável depois de 180 ticks (Menu_UpdateMovieCounter a cada tick, MenuManager_Update).
    Menu_PlayMovie(0x73b0048, 1, 0, 0, 0, 3);
    CHECK(BGFMVPlaying == 1);
    Menu_StopMovie(0);
    CHECK(BGFMVPlaying == 1);  // o contador ainda não chegou a 0
    for (int i = 0; i < 3; ++i) Menu_UpdateMovieCounter();
    Menu_StopMovie(0);
    CHECK(BGFMVPlaying == 0);
    // P_INTRO_Handler: as aberturas não pulam (movie_skip 0), nem à força.
    Menu_PlayMovie(0x73b0048, 0, 0, 0, 0, 0);
    Menu_StopMovie(1);
    CHECK(BGFMVPlaying == 1);
    // À força, um pulável para antes do contador.
    Menu_PlayMovie(0x73b0048, 1, 0, 0, 0, 100);
    Menu_StopMovie(1);
    CHECK(BGFMVPlaying == 0);
    movie_skip = 0;
    movie_counter = 0;
    movie_sound = 0;
    end_player();
}

TEST(gameflow_movie_state_waits_for_the_movie_or_start) {
    // O laço do jogo como em test_game.cpp: um mundo vazio, sem fase.
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    pending_reset();
    Door_Init();
    GS_SetRefreshRate(30, 60);  // com um filme, o main do PS2 roda a lógica a 30 ticks por segundo
    while (GameFlow_Depth() != 0) GameFlow_PopState();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    std::memset(MPGame, 0, sizeof(MPGame));
    g_gameflow_hooks.draw = [] {};
    Input_Init();
    fresh_player();
    write_movie(0x7100001, pss(es_video(60), {}, 1000));

    // Sem filme (ou um código sem arquivo, como 07100002): o GF_MOVIE sai no mesmo tick.
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_WAIT_START);

    // Um tick do main com o filme, como no PS2: 2 VBlanks, o quadro (psiDecodeBackgroundMovieFrame) e o GameFlow_Main,
    // cujo SFXUpdate mistura os 2 VBlanks na SPU2 (o relógio de um filme mudo).
    double v = 100.0;
    const auto game_tick = [&v]() {
        v += 2.0;
        psi_set_clock_vblanks(v);
        psiDecodeBackgroundMovieFrame();
        GameFlow_Main();
    };
    // Com o filme da abertura tocando, o GF_MOVIE espera.
    psiStartBackgroundMovie(0x7100001, 0, 100);
    GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
    for (int i = 0; i < 5; ++i) game_tick();
    // O quadro do quinto tick é o 4 (o relógio do quadro é o som misturado antes dele).
    CHECK(GameFlow_GetState() == GF_MOVIE && BGFMVPlaying == 1 && nf::movie_stats().frame == 4);
    // Start (a ação 0x19, Enter ou Espaço no PC) pula: o filme para e o estado sai.
    Input_SetAction(0, 0x19, kActionDown | kActionPressed);
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_WAIT_START && BGFMVPlaying == 0 && nf::movie_stats().stopped == 1);

    // Sem pular, até o fim do vídeo (60 quadros, 2 VBlanks cada sem som): o tick que vê o relógio no fim para o filme e
    // o GF_MOVIE sai nele.
    psiStartBackgroundMovie(0x7100001, 0, 100);
    GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
    int ticks = 0;
    while (GameFlow_GetState() == GF_MOVIE && ticks < 100) {
        game_tick();
        ++ticks;
    }
    CHECK(ticks == 61 && BGFMVPlaying == 0 && nf::movie_stats().finished == 1);
    g_gameflow_hooks.draw = nullptr;
    GS_SetRefreshRate(60, 60);
    end_player();
}
