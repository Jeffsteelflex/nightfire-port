// O som das missões de carro (driving/sound/): propriedades independentes, sem dados do jogo (a conferência bit a bit
// contra o código original do EE fica no nf_driving_check, parte 13). O IOP (o SNDDRV.IRX portado) não roda no
// interpretador: aqui ele é conferido pelo que se ouve, com um banco feito à mão tocando pela cadeia inteira (o SND do
// EE, a tagbuf, o IOP, a SPU2 do port): o som sai na frequência da amostra, o pitch a dobra, uma amostra sem laço acaba
// sozinha (o IOP a vê no laço silencioso de 0x5000 e o EE libera a voz), o SNDstop a cala; a área do reverb que o IOP
// zera fica onde o EE a reservou; e as contas da camada do jogo (EA-XA, AMix, AVoice, APath, o motor) contra valores
// feitos à mão.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/eaxa.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_spu.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snddrv_iop.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Um bloco de PS-ADPCM (16 bytes): o filtro 0, o deslocamento, as flags e 28 amostras de um nibble.
void adpcm_block(std::vector<uint8_t>& out, int shift, uint8_t flags, const int (&nib)[28]) {
    out.push_back(uint8_t(shift & 0xf));
    out.push_back(flags);
    for (int i = 0; i < 28; i += 2) out.push_back(uint8_t((nib[i] & 0xf) | (nib[i + 1] & 0xf) << 4));
}

// Uma onda quadrada de 28 amostras por período (14 em +7, 14 em -8), n blocos, com ou sem laço.
std::vector<uint8_t> square_adpcm(int blocks, bool loop) {
    int nib[28];
    for (int i = 0; i < 28; ++i) nib[i] = i < 14 ? 7 : -8;
    std::vector<uint8_t> a;
    for (int b = 0; b < blocks; ++b) {
        uint8_t flags = 0;
        if (b == 0 && loop) flags = 6;  // o começo do laço
        if (b == blocks - 1) flags = loop ? 3 : 1;  // o fim: com repetição, ou sem (a voz pula para o laço silencioso)
        adpcm_block(a, 2, flags, nib);
    }
    return a;
}

// Um banco BNKl (versão 5, sem parte do IOP) de um timbre: a taxa, as amostras, o deslocamento 0 e a etiqueta 0x8a
// que o SND troca pelo endereço na SPU2; a parte da SPU2 depois do cabeçalho, com a folga de 0x100 bytes.
std::vector<uint8_t> make_bank(const std::vector<uint8_t>& adpcm, int rate, int frames) {
    std::vector<uint8_t> pt = {'P', 'T', 0, 0};
    auto tag = [&](uint8_t t, uint32_t v, int n) {
        pt.push_back(t);
        pt.push_back(uint8_t(n));
        for (int i = n - 1; i >= 0; --i) pt.push_back(uint8_t(v >> (8 * i)));
    };
    tag(0x84, uint32_t(rate), 3);
    tag(0x85, uint32_t(frames), 4);
    tag(0x88, 0, 1);
    tag(0x8a, 0, 4);
    pt.push_back(0xff);
    const uint32_t header = (0x18 + uint32_t(pt.size()) + 0x3f) & ~0x3fu;
    std::vector<uint8_t> b(header + adpcm.size() + 0x100, 0);
    std::memcpy(b.data(), "BNKl", 4);
    b[4] = 5;
    const uint16_t count = 1;
    std::memcpy(b.data() + 6, &count, 2);
    std::memcpy(b.data() + 8, &header, 4);
    const int32_t spu = int32_t(adpcm.size()), iop = 0, off = 4;
    std::memcpy(b.data() + 0xc, &spu, 4);
    std::memcpy(b.data() + 0x10, &iop, 4);
    std::memcpy(b.data() + 0x14, &off, 4);
    std::memcpy(b.data() + 0x18, pt.data(), pt.size());
    std::memcpy(b.data() + header, adpcm.data(), adpcm.size());
    return b;
}

// O SND como o ASoundManager::Init o liga (estéreo), sobre a SPU2 do som do carro.
bool snd_boot() {
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
Listen listen(double seconds) {
    const int n = int(seconds * 48000);
    std::vector<int16_t> buf(size_t(n) * 2);
    snd_render(buf.data(), n);
    Listen l;
    double acc = 0;
    int crossings = 0;
    int prev = 0;
    for (int i = 0; i < n; ++i) {
        const int s = buf[size_t(i) * 2] + buf[size_t(i) * 2 + 1];
        acc += double(s) * s / 4.0;
        const int sign = s > 0 ? 1 : (s < 0 ? -1 : 0);
        if (sign != 0 && prev != 0 && sign != prev) ++crossings;
        if (sign != 0) prev = sign;
    }
    l.rms = std::sqrt(acc / n);
    l.crossings_per_s = crossings / seconds;
    return l;
}

}  // namespace

TEST(driving_sound_eaxa_nibbles_and_filter) {
    // Filtro 0, deslocamento 12: cada amostra é o nibble com sinal (o alto primeiro).
    uint8_t blk[15] = {0x0c};
    for (int i = 0; i < 14; ++i) blk[1 + i] = uint8_t(((i % 8) << 4) | ((15 - i) & 0xf));
    float out[28];
    MxaPacketF p;
    p.count = 28;
    p.in = blk;
    p.out = out;
    SND_decodexac(p);
    bool ok = true;
    for (int i = 0; i < 14; ++i) {
        const int hi = (i % 8), lo = (15 - i) & 0xf;
        ok &= out[2 * i] == float(hi > 7 ? hi - 16 : hi) && out[2 * i + 1] == float(lo > 7 ? lo - 16 : lo);
    }
    CHECK(ok);
    // Filtro 1 (0,9375 e 0), nibbles zero: o estado decai pelo coeficiente a cada amostra.
    uint8_t blk1[15] = {0x10};
    MxaPacketF q;
    q.count = 28;
    q.s1 = 4096.0f;
    q.in = blk1;
    q.out = out;
    SND_decodexac(q);
    double expect = 4096.0;
    double worst = 0;
    for (int i = 0; i < 28; ++i) {
        expect *= 0.9375;
        worst = std::fmax(worst, std::fabs(out[i] - expect) / expect);
    }
    CHECK(worst < 1e-5);
    CHECK_NEAR(q.s1, out[27], 0.0);
}

TEST(driving_sound_bank_plays_through_the_iop_and_the_spu2) {
    CHECK(snd_boot());
    // 24 kHz, 28 amostras por período: 857 Hz, 1714 cruzamentos por segundo.
    std::vector<uint8_t> loop = make_bank(square_adpcm(8, true), 24000, 8 * 28);
    int32_t bank = -1;
    CHECK(SNDbankadd(&bank, loop.data()) == 7 && bank >= 0);
    SndPlayOpts o{};
    SNDplaysetdef(&o);
    const int32_t h = SNDBANK_play(bank, 0, &o);
    CHECK(h >= 0);
    const Listen a = listen(0.5);
    CHECK(a.rms > 1000.0);
    CHECK_NEAR(a.crossings_per_s, 1714.0, 60.0);
    CHECK(SNDover(h) == 0);
    // O pitch dobrado dobra a frequência.
    CHECK(SNDpitchmult(h, 0x2000) >= 0);
    listen(0.05);
    const Listen b = listen(0.5);
    CHECK_NEAR(b.crossings_per_s, 3428.0, 120.0);
    // Parado: silêncio depois do release, e o som acabou.
    SNDstop(h);
    listen(0.3);
    const Listen c = listen(0.2);
    CHECK(c.rms < 50.0);
    CHECK(SNDover(h) == 1);
    CHECK(SNDbankremove(bank) == 0);

    // Sem laço: 0,1 s de amostra acaba sozinha (o IOP vê o NAX no laço silencioso e o EE libera a voz).
    std::vector<uint8_t> once = make_bank(square_adpcm(86, false), 24000, 86 * 28);
    CHECK(SNDbankadd(&bank, once.data()) == 7);
    const int32_t h2 = SNDBANK_play(bank, 0, &o);
    CHECK(h2 >= 0);
    const Listen d = listen(0.05);
    CHECK(d.rms > 1000.0 && SNDover(h2) == 0);
    listen(0.3);
    CHECK(SNDover(h2) == 1);
    const Listen e = listen(0.2);
    CHECK(e.rms < 50.0);
    SNDbankremove(bank);
    ASystem_Shutdown();
}

TEST(driving_sound_reverb_area_ends_at_the_eea_block) {
    // O EEA guarda só os bits 17-20: o pedido 0x1c0000 acaba em 0x1dffff; o tipo 6 (0xf6c0 bytes) começa em 0x1d0940,
    // o que o SNDDRV_setspufx do EE reserva (0x200000 - 0x2f6c0); o núcleo 0 no tipo 0 (16 bytes) logo abaixo de 2 MB.
    snd_spu_power_on();
    SndLibsd* sd = snd_spu_libsd();
    sd->SetAddr(0x1d01, 0x1c0000);
    SndEffectAttr a;
    a.core = 1;
    a.mode = 6;
    sd->SetEffectAttr(1, a);
    CHECK(sd->GetAddr(0x1c01) == 0x1d0940u);
    sd->SetAddr(0x1d00, 0x1e0000);
    a.core = 0;
    a.mode = 0;
    sd->SetEffectAttr(0, a);
    CHECK(sd->GetAddr(0x1c00) == 0x1ffff0u);
}

TEST(driving_sound_iop_heap_is_first_fit) {
    IopHeapModel heap;
    const uint32_t a = heap.alloc(0x1000), b = heap.alloc(0x200), c = heap.alloc(0x80);
    CHECK(a != 0 && b == a + 0x1000 && c == b + 0x200);
    CHECK(heap.free(b) == 0);
    CHECK(heap.alloc(0x100) == b);   // o primeiro buraco que cabe
    CHECK(heap.free(0x12345) == -1);  // um bloco que não existe
}

TEST(driving_sound_mix_transition_and_master) {
    AMix master, child;
    master.volume = 0.5f;
    child.volume = 1.0f;
    child.master = &master;
    AMix_SetTransition(0.25f, &child, 10);
    for (int i = 0; i < 9; ++i) AMix_GetVolume(&child);
    CHECK(child.transition == 1 && child.volume > 0.25f);
    CHECK_NEAR(AMix_GetVolume(&child), 0.125, 0.0);  // o alvo, vezes o mestre
    CHECK(child.transition == 0 && child.volume == 0.25f);
}

TEST(driving_sound_voice_quantizes_and_lists) {
    AMix mix;
    AVoice v(&mix, 3, 7);
    auto& list = voice_active_list();
    const size_t before = list.size();
    AVoice_Play(0.5f, 2.0f, 0.25f, 0.0f, 0.5f, &v, 2);
    const AVoiceView& w = v.views[2];
    CHECK(w.vol == 64 && w.pitch == 0x2000 && w.azimuth == 0x4000 && w.fx == 64 && w.delay == 0);
    CHECK(mix.count == 1 && mix.sum == 0.5f && list.size() == before + 1);
    AVoice_Play(0.8f, -9.0f, 0.0f, 1.0f, 0.0f, &v, 2);  // o pitch fica em 4; o atraso só vale com o anterior gasto
    CHECK(w.pitch == 0x4000 && w.delay == 0 && list.size() == before + 1);
    AVoice_Play(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, &v, 2);  // sem volume: para e sai da lista
    CHECK(w.vol == 0 && list.size() == before);
}

TEST(driving_sound_path_geometry) {
    // O ouvinte na origem olhando +z (a rotação identidade), o som parado.
    AListener l;
    for (int i = 0; i < 3; ++i) l.rot[i][i] = 1.0f;
    l.view = 2;
    APath p;
    p.listener = &l;
    class Probe : public ABaseSound {
    public:
        Probe() : ABaseSound("teste", 4) {}
        void Play(APath&) override {}
    };
    Probe* s = new Probe();
    s->min_distance = 3.0f;
    s->max_distance = 350.0f;
    s->pos[2] = 100.0f;
    APath_Build(p, *s);
    CHECK_NEAR(p.distance, 100.0, 1e-4);
    CHECK_NEAR(p.azimuth, 0.0, 1e-6);
    CHECK_NEAR(p.pitch, 1.0, 1e-6);
    CHECK_NEAR(p.volume, (350.0 - 100.0) / ((350.0 - 3.0) + (100.0 - 3.0) * 8.0), 1e-5);
    CHECK_NEAR(p.delay, (100.0 - 25.0) / 400.0, 1e-6);
    // À direita (+x): um quarto de volta; vindo na direção do ouvinte: o Doppler sobe.
    s->pos[0] = 50.0f;
    s->pos[2] = 0.0f;
    s->vel[0] = -40.0f;
    APath_Build(p, *s);
    CHECK_NEAR(p.azimuth, 0.25, 1e-4);
    CHECK_NEAR(p.pitch, 400.0 / (400.0 - 40.0), 1e-4);
    // Além do máximo: mudo.
    s->pos[0] = 400.0f;
    APath_Build(p, *s);
    CHECK(p.volume == 0.0f);
    s->destroy();
}

TEST(driving_sound_engine_pitch_follows_rpm) {
    // O motor do carro do jogador (sem bancos: os timbres ficam -1, só as contas): as rotações sobem, o pitch sobe;
    // parado nelas, o pitch fica em (rpm + 2000) / 4500 mais a oscilação de ±150 rotações.
    APlayerVehicle* v = new APlayerVehicle("teste");
    AListener l;
    l.view = 2;
    APath path;
    path.listener = &l;
    path.volume = 1.0f;
    path.pitch = 1.0f;
    path.distance = 10.0f;
    float last = 0.0f;
    bool rising = true, in_band = true;
    for (float rpm : {1000.0f, 3000.0f, 6000.0f}) {
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 400; ++k) {
            sound_count() = 1;
            v->rpm = rpm;
            v->gas = 1.0f;
            v->Play(path);
            if (k >= 200) lo = std::fmin(lo, v->engine->pitch), hi = std::fmax(hi, v->engine->pitch);
        }
        in_band &= lo >= (rpm - 151.0f + 2000.0f) / 4500.0f && hi <= (rpm + 151.0f + 2000.0f) / 4500.0f;
        rising &= lo > last;
        last = hi;
    }
    CHECK(rising);
    CHECK(in_band);
    sound_count() = 0;
    v->destroy();
}
