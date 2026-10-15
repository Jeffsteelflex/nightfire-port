// Os fluxos das missões de carro (driving/sound/snd_stream, snd_mix, stream, audio_stream): propriedades independentes,
// sem dados do jogo (a conferência contra o código original do EE fica no nf_driving_check, parte 23). Um .asf feito à
// mão (o cabeçalho PT, os pedaços SCDl de EA-XA de uma onda quadrada, o SCEl) toca pela cadeia inteira como no
// nf_driving: o SNDSTRM e o tocador de pacotes, o mixador do EE (o desempacotador, o reamostrador, o ft16), a tagbuf, o
// IOP e a SPU2 do port. O som sai na frequência da amostra, o pitch o dobra (o reamostrador passando direto) ou o divide
// (interpolando), o fluxo acaba sozinho no SCEl e o pedido sai da fila; um pedido segurado só toca depois do
// modifyhold. Na camada do jogo, o AStream toca um evento em laço além do fim do arquivo e o SetMissionOver cala a
// música em 3 s.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "driving/bigf.h"
#include "driving/game/triggers.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_stream.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_mix.h"
#include "driving/sound/snd_spu.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snd_stream.h"
#include "driving/sound/snddrv_iop.h"
#include "driving/sound/stream.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}
// Um pedaço do .asf: a etiqueta, o tamanho (com o cabeçalho, múltiplo de 4) e o corpo.
void chunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& body) {
    const uint32_t size = (8 + uint32_t(body.size()) + 3) & ~3u;
    out.insert(out.end(), tag, tag + 4);
    put32(out, size);
    out.insert(out.end(), body.begin(), body.end());
    while (out.size() % 4 != 0) out.push_back(0);
}

// Um .asf mono de EA-XA (codec 10, a mixagem do EE: o modo 4) a `rate` Hz: `blocks` blocos de 28 amostras de uma onda
// quadrada (o filtro 0, o deslocamento 2: 14 amostras +7 x 1024, 14 em -8 x 1024), `per_chunk` blocos por SCDl.
std::vector<uint8_t> square_asf(uint32_t rate, int blocks, int per_chunk) {
    const uint32_t frames = uint32_t(blocks) * 28;
    std::vector<uint8_t> pt = {'P', 'T', 0, 0};
    auto tag = [&](uint8_t t, uint32_t v, int n) {
        pt.push_back(t);
        pt.push_back(uint8_t(n));
        for (int i = n - 1; i >= 0; --i) pt.push_back(uint8_t(v >> (8 * i)));
    };
    tag(0x84, rate, 3);
    tag(0x85, frames, 4);
    tag(0xa0, 10, 1);
    tag(0x8c, 4, 1);
    pt.push_back(0xff);
    std::vector<uint8_t> asf;
    chunk(asf, "SCHl", pt);
    std::vector<uint8_t> count;
    put32(count, uint32_t((blocks + per_chunk - 1) / per_chunk));
    chunk(asf, "SCCl", count);
    for (int b = 0; b < blocks; b += per_chunk) {
        const int n = std::min(per_chunk, blocks - b);
        std::vector<uint8_t> body;
        put32(body, uint32_t(n) * 28);
        put32(body, 0);   // o deslocamento do canal 0
        put32(body, 0);   // o estado do decodificador (duas amostras de 16 bits)
        for (int k = 0; k < n; ++k) {
            body.push_back(0x02);  // o filtro 0, o deslocamento 2
            for (int i = 0; i < 14; ++i) body.push_back(i < 7 ? 0x77 : 0x88);
        }
        chunk(asf, "SCDl", body);
    }
    chunk(asf, "SCEl", {});
    return asf;
}

bool boot() {
    snd_power_on();
    snd_spu_power_on();
    snddrv_iop_start(snd_spu_libsd());
    ASystem_SetOpts(1);
    return ASystem_Construct() >= 0;
}

struct Listen {
    double rms = 0;
    double crossings_per_s = 0;
};
// Como o nf_driving: a cada quadro do jogo (1/60 s) o SNDSYS_service (o servidor dos fluxos) e 800 amostras da saída
// (os ticks do driver no meio).
Listen play(double seconds) {
    const int frames = int(seconds * 60 + 0.5);
    std::vector<int16_t> buf(800 * 2);
    double acc = 0;
    int crossings = 0, prev = 0;
    for (int f = 0; f < frames; ++f) {
        SNDSYS_service();
        snd_render(buf.data(), 800);
        for (int i = 0; i < 800; ++i) {
            const int s = buf[size_t(i) * 2] + buf[size_t(i) * 2 + 1];
            acc += double(s) * s / 4.0;
            const int sign = s > 0 ? 1 : (s < 0 ? -1 : 0);
            if (sign != 0 && prev != 0 && sign != prev) ++crossings;
            if (sign != 0) prev = sign;
        }
    }
    Listen l;
    l.rms = std::sqrt(acc / (frames * 800.0));
    l.crossings_per_s = crossings / (frames / 60.0);
    return l;
}

// A memória de um fluxo (como a do AStreamPriv: alinhada a 0x400).
struct StreamMem {
    std::vector<uint8_t> buf;
    uint8_t* mem = nullptr;
    int32_t size = 0;
    explicit StreamMem(int32_t buffer) {
        size = buffer + SNDSTRM_overhead(4, 0xf);
        buf.assign(size_t(size) + 0x400, 0);
        uintptr_t a = reinterpret_cast<uintptr_t>(buf.data());
        mem = reinterpret_cast<uint8_t*>((a + 0x3ff) & ~uintptr_t(0x3ff));
    }
};

}  // namespace

TEST(driving_stream_asf_plays_through_the_ee_mixer_and_the_iop) {
    CHECK(boot());
    // 1 s a 48 kHz (o reamostrador passa direto): 28 amostras por período, 1714 Hz, 3428 cruzamentos por segundo.
    const std::vector<uint8_t> asf = square_asf(48000, 1715, 115);
    stream_power_on();
    stream_set_loader([&asf](const std::string& name, std::vector<uint8_t>& out) {
        if (name != "|square.asf") return false;
        out = asf;
        return true;
    });
    StreamMem m(0x20000);
    SndPlayOpts o{};
    SNDplaysetdef(&o);
    const int32_t st = SNDSTRM_create(&o, 4, 0xf, m.mem, m.size);
    CHECK(st >= 0);
    const int32_t id = SNDSTRM_queuefile(st, 0xfa, "|square.asf", 0);
    CHECK(id >= 0);
    play(0.15);  // (o prebuffer de 250 ms do pedido e o começo)
    const Listen a = play(0.25);
    CHECK(a.rms > 1000.0);
    CHECK_NEAR(a.crossings_per_s, 3428.0, 150.0);
    SndRequestStatus rs{};
    SNDSTRM_requeststatus(id, &rs);
    CHECK(rs.state == 2);
    // O pitch dobrado dobra a frequência; a metade passa pelo caminho que interpola.
    CHECK(SNDSTRM_pitchmult(st, 0x2000) >= 0);
    play(0.05);
    const Listen b = play(0.1);
    CHECK_NEAR(b.crossings_per_s, 6856.0, 300.0);
    CHECK(SNDSTRM_pitchmult(st, 0x800) >= 0);
    play(0.05);
    const Listen c = play(0.2);
    CHECK_NEAR(c.crossings_per_s, 1714.0, 100.0);
    // O arquivo acaba sozinho no SCEl: silêncio e o pedido fora da fila.
    play(1.5);
    const Listen d = play(0.2);
    CHECK(d.rms < 50.0);
    SNDSTRM_requeststatus(id, &rs);
    CHECK(rs.state == 3);
    SndStreamStatus ss{};
    SNDSTRM_status(st, &ss);
    CHECK(ss.requests == 0);
    CHECK(snd_stream_guards() == 0 && mix_guards() == 0);
    SNDSTRM_destroy(st);
    stream_set_loader(nullptr);
    ASystem_Shutdown();
}

TEST(driving_stream_held_request_waits_for_modifyhold) {
    CHECK(boot());
    const std::vector<uint8_t> asf = square_asf(48000, 600, 100);
    stream_power_on();
    stream_set_loader([&asf](const std::string& name, std::vector<uint8_t>& out) {
        if (name != "|held.asf") return false;
        out = asf;
        return true;
    });
    StreamMem m(0x20000);
    SndPlayOpts o{};
    SNDplaysetdef(&o);
    const int32_t st = SNDSTRM_create(&o, 4, 0xf, m.mem, m.size);
    const int32_t id = SNDSTRM_queuefile(st, -1, "|held.asf", 0);
    CHECK(id >= 0);
    // Segurado (o prebuffer do NIS): lido, mas mudo.
    const Listen a = play(0.3);
    CHECK(a.rms < 50.0);
    SndRequestStatus rs{};
    SNDSTRM_requeststatus(id, &rs);
    CHECK(rs.state < 2);
    // Solto: toca.
    CHECK(SNDSTRM_modifyhold(id, 0) >= 0);
    play(0.05);
    const Listen b = play(0.2);
    CHECK(b.rms > 1000.0);
    CHECK_NEAR(b.crossings_per_s, 3428.0, 150.0);
    SNDSTRM_destroy(st);
    stream_set_loader(nullptr);
    ASystem_Shutdown();
}

TEST(driving_stream_astream_loops_and_fades_at_mission_over) {
    // A camada do jogo: o ASoundManager::Init com os fluxos (sem BIG: o STREAM lê do carregador), a música num laço de
    // 0,5 s pedida por um EStreamEvent; depois de 1,5 s ainda toca (o AStream a pede de novo quando só ela resta na
    // fila do SND); o SetMissionOver a cala em 3 s.
    const std::vector<uint8_t> asf = square_asf(48000, 858, 100);
    stream_set_loader([&asf](const std::string& name, std::vector<uint8_t>& out) {
        if (name != "|theme.asf") return false;
        out = asf;
        return true;
    });
    const std::string ini = "Music = 1.0, 5\r\nSpeech = 1.0, 5\r\nAmbience = 1.0, 5\r\nNIS = 1.0, 5\r\n";
    AudioInit ai;
    ai.track = "test";
    ai.stream_file = "TESTEN";
    ai.big_open = [](const std::string&) -> std::unique_ptr<Bigf> { return nullptr; };
    CHECK(audio_init(ai, [&ini](const std::string& path) {
        std::vector<uint8_t> v;
        if (path == "data/audio/test.ini") v.assign(ini.begin(), ini.end());
        return v;
    }));
    audio_set_timer(0);
    static char music[] = "music", theme[] = "theme";
    PendingEvent e{};
    e.type = 20;
    e.obj[1] = nf::p32_u(music);
    e.obj[2] = nf::p32_u(theme);
    e.obj[3] = uint32_t(-1);  // sem transição
    e.obj[4] = 1;             // em laço
    CHECK(audio_event_execute(e));
    int32_t timer = 0;
    // Um quadro do jogo: o relógio de 100 Hz, o AStream::Play (o BuildPaths) e o som.
    auto frames = [&](double seconds) {
        Listen l;
        const int n = int(seconds * 60 + 0.5);
        double rms = 0;
        for (int i = 0; i < n; ++i) {
            timer += (i % 3 == 2) ? 1 : 2;
            audio_set_timer(timer);
            astream_test_play("Music", 1.0f, 1.0f, 0.0f);
            const Listen f = play(1.0 / 60.0);
            rms += f.rms * f.rms;
            l.crossings_per_s += f.crossings_per_s / n;
        }
        l.rms = std::sqrt(rms / n);
        return l;
    };
    frames(0.3);
    const Listen a = frames(0.4);
    CHECK(a.rms > 1000.0);
    frames(1.5);
    const Listen b = frames(0.4);
    CHECK(b.rms > 1000.0);  // (o arquivo tem 0,5 s: tocou em laço)
    audio_set_mission_over();
    frames(3.5);
    const Listen c = frames(0.3);
    CHECK(c.rms < 50.0);
    CHECK(audio_stream_stats().guards == 0);
    audio_shutdown();
    stream_set_loader(nullptr);
}
