// Testes do som: o PS-ADPCM, a SPU2 do PC (vozes, envelope, laço, mistura), o driver do IOP (3D, 2D, limites de
// vozes, MaxVoices, roubo por prioridade, falas, pausa) e o caminho do jogo (Sound_Play até o nó voltar à lista).
// O driver lê um "disco" de mentira montado aqui numa pasta temporária (SBINFO.SBI, um banco SB_0 e uma fala).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/memory.h"
#include "engine/sound.h"
#include "platform/audio_out.h"
#include "platform/psi.h"
#include "platform/psi_sound.h"
#include "platform/sfx_iop.h"
#include "platform/spu2.h"
#include "test_framework.h"

namespace {

namespace fs = std::filesystem;

// Um bloco de PS-ADPCM com filtro 0, deslocamento 0 e todos os nibbles iguais: amostras constantes (nib << 12).
std::vector<uint8_t> dc_block(uint8_t nib, uint8_t flags) {
    std::vector<uint8_t> b(16, 0);
    b[0] = 0x00;
    b[1] = flags;
    for (int i = 2; i < 16; ++i) b[i] = uint8_t(nib | (nib << 4));
    return b;
}

void append(std::vector<uint8_t>& v, const std::vector<uint8_t>& w) { v.insert(v.end(), w.begin(), w.end()); }
void put32(std::vector<uint8_t>& v, int32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(uint32_t(x) >> (8 * i)));
}
void put16(std::vector<uint8_t>& v, int16_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(uint16_t(x) >> 8));
}
void write(const fs::path& p, const std::vector<uint8_t>& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

struct SfxDef {
    int32_t hash, tracking, inner, outer, max_voices, priority;
    int16_t max_reject;
    int32_t file_ref;
    int16_t multi = 0;    // MultiSample
    int16_t poly = 0;     // Polyphonic
    int32_t samples = 1;  // SampleCount (todas com file_ref)
};

// SFXParameters (68 bytes) e as amostras (28 bytes cada).
std::vector<uint8_t> sfx_record(const SfxDef& d) {
    std::vector<uint8_t> v;
    put32(v, 0);  // ReverbSend
    put32(v, d.tracking);
    put32(v, d.inner);
    put32(v, d.outer);
    put32(v, d.max_voices);
    put32(v, d.priority);
    put32(v, 0);  // Group
    put16(v, d.max_reject);
    put16(v, 0);  // Action2
    put16(v, 0);  // IgnoreAge
    put16(v, 0);  // Ducker
    put32(v, 0);  // DuckerLength
    put32(v, 100);  // MasterVolume
    put32(v, 0);  // MinDelay
    put32(v, 0);  // MaxDelay
    put16(v, d.multi);
    for (int i = 0; i < 3; ++i) put16(v, 0);  // RandomPick, Shuffled, Loop
    put16(v, d.poly);
    v.push_back(0);  // Outdoors
    v.push_back(0);  // PauseInNis
    put32(v, d.samples);
    for (int k = 0; k < d.samples; ++k) {
        put32(v, d.file_ref);
        put32(v, 0);    // PitchOffset
        put32(v, 0);    // RandomPitchOffset
        put32(v, 100);  // BaseVolume
        put32(v, 0);    // RandomVolumeOffset
        put32(v, 0);    // Pan
        put32(v, 0);    // RandomPan
    }
    return v;
}

constexpr int32_t kBankHash = 7;
constexpr int kSpeechBlocks = 40;

constexpr int32_t kMusicHash = 0x27;

// Um marcador da música (MusicMarkerData, 32 bytes).
void put_marker(std::vector<uint8_t>& v, int32_t name, int32_t pos, int32_t type, int32_t loop_start, int32_t loop_marker) {
    for (int32_t x : {name, pos, type, 0, 0, loop_start, 0, loop_marker}) put32(v, x);
}

// A faixa MFX_1 (o hash kMusicHash em MFXINFO[0]): dois trechos de duas unidades de 0x100 bytes (0x80 da esquerda,
// 0x80 da direita). O trecho 0 (unidades 0 e 1, amostras +0x2000) volta ao começo no marcador do tipo 6 em 0x200; o
// trecho 1 (unidades 2 e 3, amostras -0x2000) termina no marcador do tipo 9 em 0x400.
void make_music(const fs::path& root) {
    std::vector<uint8_t> ssd;
    for (int unit = 0; unit < 4; ++unit) {
        for (int b = 0; b < 16; ++b) append(ssd, dc_block(unit < 2 ? 2 : 0xe, 0));
    }
    std::vector<uint8_t> markers;
    put_marker(markers, 0, 0, 10, 0, 0);
    put_marker(markers, 0, 0x200, 6, 0, 0);
    put_marker(markers, 1, 0x200, 10, 0, 0);
    put_marker(markers, 1, 0x400, 9, 0, 0);
    std::vector<uint8_t> smf;
    put32(smf, 2);            // trechos
    put32(smf, 4);            // marcadores
    put32(smf, 20);           // os trechos
    put32(smf, 20 + 2 * 52);  // os marcadores
    put32(smf, 100);          // volume
    for (int s = 0; s < 2; ++s) {  // MusicMarkerStartData: o marcador inicial, MarkerPos, IsInstant, InstantBuffer, State
        smf.insert(smf.end(), markers.begin() + s * 2 * 32, markers.begin() + s * 2 * 32 + 32);
        for (int32_t x : {s * 2, 0, 0, 0, 0}) put32(smf, x);
    }
    append(smf, markers);
    write(root / "PS2" / "MUSIC" / "MFX_0" / "MFX_1.SMF", smf);
    write(root / "PS2" / "MUSIC" / "MFX_0" / "MFX_1.SSD", ssd);
}

// O "disco": SBINFO.SBI (o banco 7 no SB_0), MFXINFO.MXI com uma faixa, o banco com duas amostras (0: laço de 2
// blocos, 1: 4 blocos sem laço, ambas a 48 kHz) e seis sons, uma fala de 40 blocos em STREAMS.BIN e a música.
fs::path make_disc() {
    const fs::path root = fs::temp_directory_path() / "nf_sound_test";
    std::vector<uint8_t> sbi, mxi;
    for (int i = 0; i < 150; ++i) put32(sbi, i == 0 ? kBankHash : -1);
    for (int i = 0; i < 150; ++i) put32(mxi, i == 0 ? kMusicHash : -1);
    write(root / "PS2" / "SBINFO.SBI", sbi);
    write(root / "PS2" / "MFXINFO.MXI", mxi);
    make_music(root);
    std::vector<uint8_t> sbf;
    append(sbf, dc_block(4, 4));
    append(sbf, dc_block(4, 3));  // amostra 0: laço
    for (int i = 0; i < 3; ++i) append(sbf, dc_block(4, 0));
    append(sbf, dc_block(4, 1));  // amostra 1: fim sem repetição
    std::vector<uint8_t> shf;
    put32(shf, 2);
    const int32_t sizes[2] = {32, 64};
    int32_t off = 0;
    for (int i = 0; i < 2; ++i) {
        put32(shf, i == 0 ? 1 : 0);  // Flags (laço)
        put32(shf, off);
        put32(shf, sizes[i]);
        put32(shf, 0x1000);  // 48 kHz
        put32(shf, sizes[i]);
        put32(shf, 1);
        put32(shf, 4);
        put32(shf, i * 0x60);
        put32(shf, 0);
        off += sizes[i];
    }
    const SfxDef defs[] = {
        {0x10, 0, 1, 100, 0, 50, 0, 1},    // 2D
        {0x11, 2, 1, 11, 2, 100, 0, 1},    // 3D, MaxVoices 2
        {0x12, 2, 1, 11, 1, 100, 1, 1},    // 3D, MaxVoices 1 com MaxReject
        {0x13, 0, 1, 100, 0, 10, 0, 0},    // 2D em laço, prioridade baixa
        {0x14, 0, 1, 100, 1, 99, 0, -1},   // a fala 0 (FileRef -1)
        {0x15, 0, 1, 100, 0, 5, 0, 1, 1, 1, 2},  // 2D polifônico (duas amostras juntas), prioridade mínima
    };
    std::vector<uint8_t> sfx;
    const int n = int(sizeof(defs) / sizeof(defs[0]));
    put32(sfx, n);
    int32_t pos = 4 + n * 8;
    for (const SfxDef& d : defs) {
        put32(sfx, d.hash);
        put32(sfx, pos);
        pos += 68 + 28 * d.samples;
    }
    for (const SfxDef& d : defs) append(sfx, sfx_record(d));
    const fs::path bank = root / "PS2" / "ENGLISH" / "SB_0";
    write(bank / "SB_0.SFX", sfx);
    write(bank / "SB_0.SHF", shf);
    write(bank / "SB_0.SBF", sbf);
    // A fala: marcadores {1 início, 2 marcadores, 0x14, 0x48, volume 100} e os dados 0x1000 depois.
    std::vector<uint8_t> marks;
    put32(marks, 1);
    put32(marks, 2);
    put32(marks, 0x14);
    put32(marks, 0x48);
    put32(marks, 100);
    marks.resize(0x1000, 0);
    std::vector<uint8_t> bin = marks;
    for (int i = 0; i < kSpeechBlocks - 1; ++i) append(bin, dc_block(2, 0));
    append(bin, dc_block(2, 1));
    std::vector<uint8_t> lut;
    put32(lut, 0);
    put32(lut, 0x88);
    put32(lut, 0x1000);
    put32(lut, kSpeechBlocks * 16);
    write(root / "PS2" / "ENGLISH" / "STREAMS" / "STREAMS.BIN", bin);
    write(root / "PS2" / "ENGLISH" / "STREAMS" / "STREAMS.LUT", lut);
    return root;
}

// Um lote como o do EE: os comandos e o 5 do fim; devolve SFXActiveData.
std::vector<int32_t> rpc(std::vector<int32_t> cmds) {
    cmds.push_back(5);
    std::vector<int32_t> active(0x55, 0);
    nf::sfx_iop_rpc(cmds.data(), int32_t(cmds.size()), reinterpret_cast<uint8_t*>(active.data()));
    return active;
}

int count_active(const std::vector<int32_t>& active, int32_t hash) {
    int n = 0;
    for (int i = 0; i < active[0]; ++i) n += active[1 + i * 2] == hash ? 1 : 0;
    return n;
}

bool has_handle(const std::vector<int32_t>& active, int32_t handle) {
    for (int i = 0; i < active[0]; ++i) {
        if (active[2 + i * 2] == handle) return true;
    }
    return false;
}

// O driver ligado ao disco de mentira, em estéreo, com o banco carregado e o ouvinte na origem olhando para +z
// (a esquerda, ListenerNorm, é +x).
void boot_driver() {
    nf::sfx_iop_debug_power_on();
    nf::audio_out_debug_power_on();
    nf::sfx_iop_set_root(make_disc());
    rpc({0x1b, 0, 0x1c, 1, 8, 100, 0x15, 70, 0});
    rpc({1, kBankHash});
    rpc({0xe, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 0, 1000, 0, 1000, 0, 0, 0});
}

// Um VBlank: a SPU anda uma amostra (as vozes ligadas têm envelope) e o update do IOP.
std::vector<int32_t> tick() {
    nf::spu().render(nullptr, 1);
    return rpc({4});
}

std::vector<int32_t> start3d(int32_t hash, int32_t x, int32_t y, int32_t z, int32_t handle) {
    return rpc({0xd, hash, x, y, z, 0, 0, 0, handle, 100, -1, -1});
}

uint16_t reg(int v) { return uint16_t(std::clamp(v * 0x3fff / 100, 0, 0x3fff)); }

}  // namespace

TEST(adpcm_decodes_hand_built_blocks) {
    nf::AdpcmState st;
    int16_t out[28];
    // Filtro 0, deslocamento 12: o nibble com sinal (0xf = -1, 0x1 = 1).
    const uint8_t a[16] = {0x0c, 0x00, 0x1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(nf::adpcm_decode_block(a, out, st) == 0 && out[0] == -1 && out[1] == 1 && out[2] == 0);
    // Deslocamento 0: o nibble nos 4 bits de cima; as flags voltam.
    st = {};
    const uint8_t b[16] = {0x00, 0x07, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(nf::adpcm_decode_block(b, out, st) == 7 && out[0] == 0x7000 && out[1] == 0);
    // Filtro 1 (60/64) sem entrada nova: cada amostra é (anterior * 60 + 32) >> 6.
    st = {1000, 0};
    const uint8_t c[16] = {0x1c, 0x00};
    nf::adpcm_decode_block(c, out, st);
    CHECK(out[0] == (1000 * 60 + 32) >> 6 && out[1] == (out[0] * 60 + 32) >> 6);
    // Filtro 2 (115, -52) corta em 16 bits.
    st = {30000, -30000};
    const uint8_t d[16] = {0x20, 0x00, 0x07};
    nf::adpcm_decode_block(d, out, st);
    CHECK(out[0] == 32767);
    // Deslocamentos 13 a 15 valem 9 na SPU.
    st = {};
    const uint8_t e[16] = {0x0f, 0x00, 0x01};
    nf::adpcm_decode_block(e, out, st);
    CHECK(out[0] == (0x1000 >> 9));
}

TEST(spu_voice_loops_and_one_shots_end_by_the_adpcm_flags) {
    nf::Spu2 spu;
    std::vector<uint8_t> data;
    append(data, dc_block(4, 4));
    append(data, dc_block(4, 3));  // laço: 0x0000-0x001f
    append(data, dc_block(4, 0));
    append(data, dc_block(4, 1));  // sem laço: 0x0020-0x003f
    std::memcpy(spu.ram.data() + 0x1000, data.data(), data.size());
    spu.set_start(0, 0x1000);
    spu.set_start(1, 0x1020);
    for (int v = 0; v < 2; ++v) {
        spu.set_pitch(v, 0x1000);
        spu.set_volume_left(v, 0x3fff);
        spu.set_volume_right(v, 0x3fff);
        spu.key_on(v);
    }
    spu.render(nullptr, 56 + 20);  // a voz 1 toca 56 amostras (mais o atraso da interpolação)
    CHECK(spu.envx(0) == 0x7fff);
    CHECK(spu.envx(1) == 0 && spu.end_flag(1));
    spu.render(nullptr, 1000);  // o laço segue tocando
    CHECK(spu.envx(0) == 0x7fff && spu.end_flag(0));
    // Envelope do driver: ataque de 3 amostras, soltura de 2.
    spu.key_on(1);
    spu.render(nullptr, 1);
    CHECK(spu.envx(1) == 7 << 11);
    spu.render(nullptr, 2);
    CHECK(spu.envx(1) == 0x7fff);
    spu.key_off(1);
    spu.render(nullptr, 1);
    CHECK(spu.envx(1) == 0x7fff - (8 << 11));
    spu.render(nullptr, 1);
    CHECK(spu.envx(1) == 0);
}

// A amplitude de pico dos últimos n quadros de um canal.
int peak(const std::vector<int16_t>& out, int frames, int channel, int last) {
    int p = 0;
    for (int f = frames - last; f < frames; ++f) p = std::max(p, std::abs(int(out[2 * f + channel])));
    return p;
}

TEST(spu_mix_scales_by_voice_and_master_volume_and_inverts_phase) {
    nf::Spu2 spu;
    // Um laço de amostras +0x2000, -0x2000 alternadas (nibbles 2 e 0xe): o sinal mais alto que a saída deixa passar
    // inteiro (o bloqueio de DC só tira o que não muda).
    std::vector<uint8_t> blk(16, 0xe2);
    blk[0] = 0x00;
    std::vector<uint8_t> data = blk, end = blk;
    data[1] = 4;
    end[1] = 3;
    append(data, end);
    std::memcpy(spu.ram.data(), data.data(), data.size());
    spu.set_master_volume(0x3fff);
    spu.set_start(0, 0);
    spu.set_pitch(0, 0x1000);
    spu.set_volume_left(0, 0x3fff);   // cheio
    spu.set_volume_right(0, 0x1fff);  // metade
    spu.key_on(0);
    const int n = 256;
    std::vector<int16_t> out(2 * n);
    spu.render(out.data(), n);
    const int l = peak(out, n, 0, 32), r = peak(out, n, 1, 32);
    // 0x2000 x (0x7ffe / 0x8000)^4 (o volume da voz, o envelope e os mestres dos dois núcleos: a voz 0 é do núcleo 0,
    // cuja saída entra no núcleo 1 por AVOL 0x7fff) e o ganho do filtro (~1).
    CHECK(l > 0x1f00 && l <= 0x2004);
    CHECK(std::abs(2 * r - l) < 16);    // metade do volume, metade da amplitude
    // A voz de trás do driver: R = 0x7fff - v é a mesma amplitude com a fase invertida.
    spu.set_volume_right(0, uint16_t(0x7fff - 0x3fff));
    spu.render(out.data(), n);
    CHECK(std::abs(int(out[2 * (n - 1)]) + int(out[2 * (n - 1) + 1])) < 16 && peak(out, n, 1, 32) > 0x1f00);
    // Volume mestre pela metade só no núcleo 1: a voz do núcleo 0 passa pelos dois mestres e cai à metade.
    spu.set_volume_right(0, 0x3fff);
    spu.set_master_volume(1, 0x1fff);
    spu.render(out.data(), n);
    CHECK(std::abs(2 * peak(out, n, 0, 32) - l) < 16);
    // Pela metade nos dois núcleos (o que o driver faria): a mesma voz cai a um quarto.
    spu.set_master_volume(0x1fff);
    spu.render(out.data(), n);
    CHECK(std::abs(4 * peak(out, n, 0, 32) - l) < 32);
}

// Uma voz que toca o laço de +-0x2000 do teste acima, cheia nos dois lados.
void square_voice(nf::Spu2& spu, int v) {
    std::vector<uint8_t> blk(16, 0xe2);
    blk[0] = 0x00;
    std::vector<uint8_t> data = blk, end = blk;
    data[1] = 4;
    end[1] = 3;
    append(data, end);
    std::memcpy(spu.ram.data() + 0x2000, data.data(), data.size());
    spu.set_start(v, 0x2000);
    spu.set_pitch(v, 0x1000);
    spu.set_volume_left(v, 0x3fff);
    spu.set_volume_right(v, 0x3fff);
    spu.key_on(v);
}

TEST(spu_cores_mix_through_core1_and_saturate_each_voice_sum) {
    // O caminho do sceSdInit do LIBSD.IRX: a saída do núcleo 0 entra no núcleo 1 (AVOL 0x7fff, ~1.0) e o mestre do
    // núcleo 1 vale para tudo; o do núcleo 0 só para as vozes 0-23.
    nf::Spu2 spu;
    spu.set_master_volume(0x3fff);
    square_voice(spu, 30);  // núcleo 1
    const int n = 256;
    std::vector<int16_t> out(2 * n);
    spu.render(out.data(), n);
    const int one = peak(out, n, 0, 32);
    CHECK(one > 0x1f00 && one <= 0x2004);
    spu.set_master_volume(0, 0);  // o mestre do núcleo 0 não cala a voz do núcleo 1
    spu.render(out.data(), n);
    CHECK(std::abs(peak(out, n, 0, 32) - one) < 16);
    // Cinco vozes de +-0x2000 no mesmo núcleo somam +-0xa000: a soma do núcleo satura em 16 bits.
    nf::Spu2 sat;
    sat.set_master_volume(0x3fff);
    for (int v = 0; v < 5; ++v) square_voice(sat, v);
    sat.render(out.data(), n);
    CHECK(sat.saturation().voices[0] > 0 && sat.saturation().voices[1] == 0);
    CHECK(sat.saturation().output == 0);  // o núcleo 0 já saturou: a saída fica dentro dos 16 bits
    // As mesmas cinco divididas entre os núcleos (3 + 2): cada soma cabe, a saída final é que satura.
    nf::Spu2 split;
    split.set_master_volume(0x3fff);
    for (int v : {0, 1, 2, 24, 25}) square_voice(split, v);
    split.render(out.data(), n);
    CHECK(split.saturation().voices[0] == 0 && split.saturation().voices[1] == 0);
    CHECK(split.saturation().output > 0 && split.saturation().peak > 0x9000);
}

TEST(spu_effect_return_is_a_16_bit_volume) {
    // EVOL é um volume de 16 bits com sinal (0x7fff = 1.0), não o de 15 bits das vozes e do mestre (0x3fff = 1.0): o
    // máximo do driver (psiSetReverb(100): 80 x 0x3fff / 100 = 0x3332) devolve o reverb a 0.4. Medido pela cauda do
    // reverb (o que sai depois do fim da voz), que o mestre também escala: EVOL 0x7fff com o mestre pela metade
    // (0x1fff) soa como EVOL 0x4000 com o mestre cheio. Se EVOL fosse de 15 bits, 0x7fff seria -1/0x4000 (mudo) e 0x4000
    // seria -1.0.
    // A voz é do núcleo 1, cujo mestre vale para tudo (uma do núcleo 0 passaria pelos dois mestres).
    auto tail_rms = [](uint16_t evol, uint16_t mvol) {
        nf::Spu2 spu;
        spu.set_master_volume(mvol);
        spu.set_effect_volume(evol);
        square_voice(spu, 30);
        spu.set_reverb_send(30, true);
        std::vector<int16_t> out(2 * 9600);
        spu.render(out.data(), 4800);
        spu.key_off(30);
        spu.render(out.data(), 9600);
        double e = 0.0;
        for (int f = 16; f < 9600; ++f) e += double(out[2 * f]) * out[2 * f];  // depois da soltura (2 amostras)
        return std::sqrt(e / 9584);
    };
    const double full = tail_rms(0x7fff, 0x1fff), half = tail_rms(0x4000, 0x3fff), none = tail_rms(0, 0x3fff);
    CHECK(none < 1.0);
    CHECK(half > 10.0);
    CHECK(std::abs(full / half - 1.0) < 0.02);
    // E a escala é linear: o máximo do driver é 0.4 / 0.5 do de 0x4000.
    CHECK(std::abs(tail_rms(0x3332, 0x3fff) / half - 0.8) < 0.02);
}

TEST(spu_output_blocks_dc_like_the_console_output) {
    // Uma voz congelada (pitch 0, a pausa do driver) segura um valor constante; na saída ele some (o capacitor da
    // saída analógica do PS2): passa o degrau e volta a zero.
    nf::Spu2 spu;
    std::vector<uint8_t> data;
    append(data, dc_block(2, 4));
    append(data, dc_block(2, 3));
    std::memcpy(spu.ram.data(), data.data(), data.size());
    spu.set_master_volume(0x3fff);
    spu.set_start(0, 0);
    spu.set_pitch(0, 0x1000);
    spu.set_volume_left(0, 0x3fff);
    spu.set_volume_right(0, 0x3fff);
    spu.key_on(0);
    std::vector<int16_t> out(2 * 48000);
    spu.render(out.data(), 16);
    CHECK(out[2 * 15] > 0x1000);  // o degrau passa
    spu.set_pitch(0, 0);
    spu.render(out.data(), 48000);  // 1 s parada
    CHECK(spu.envx(0) == 0x7fff && std::abs(int(out[2 * 47999])) < 8);
}

TEST(iop_square_root_is_the_driver_newton_with_its_quirk) {
    // 10 passos de Newton a partir de 1, x - (x² - n) / 2x com a divisão de 64 bits truncada (__divdi3). Chegando por
    // cima, a correção trunca para 0 um acima da raiz: 100 -> 11 ((11² - 100) / 22 = 0), 2500 -> 51, 10000 -> 101.
    CHECK(nf::sfx_iop_square_root(0) == 0 && nf::sfx_iop_square_root(1) == 1 && nf::sfx_iop_square_root(4) == 2);
    CHECK(nf::sfx_iop_square_root(100) == 11 && nf::sfx_iop_square_root(2500) == 51);
    CHECK(nf::sfx_iop_square_root(10000) == 101);
    // E não converge para valores grandes (a metade a cada passo até chegar perto): 50 m (n = 250000 dm²) sai 518,
    // 100 m (10^6) sai 1297.
    CHECK(nf::sfx_iop_square_root(250000) == 518 && nf::sfx_iop_square_root(1000000) == 1297);
}

TEST(iop_3d_volume_and_pan_follow_the_driver_formula) {
    boot_driver();
    // 5 m à esquerda (+x), raios 1 e 11 m: d = raiz(2500) = 51 dm - 10 = 41, v = (1000 - 410) / 10 = 59, volume
    // 59² / 100 = 34; direção (980, 0, 0): esquerda ((980 + 1000) x 34 / 2000) x 83 / 100 = 27, direita 0, trás 17
    // -> 17 x 75 / 100 = 12 (a frente é 100 - 17).
    start3d(0x11, 5000, 0, 0, 0x100);
    int front = -1, rear = -1;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        if (nf::spu().volume_left(v) == reg(27)) front = v;
        if (nf::spu().volume_left(v) == reg(12)) rear = v;
    }
    CHECK(front != -1 && nf::spu().volume_right(front) == 0);
    CHECK(rear != -1 && nf::spu().volume_right(rear) == uint16_t(0x7fff - reg(12)));
    // À frente (0, 0, 5 m): esquerda = direita = 34 x 1000 / 2000 = 17, nada atrás.
    boot_driver();
    start3d(0x11, 0, 0, 5000, 0x101);
    int both = 0;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        both += nf::spu().volume_left(v) == reg(17) && nf::spu().volume_right(v) == reg(17) ? 1 : 0;
    }
    CHECK(both == 1);
    // Além do raio externo: volume 0.
    boot_driver();
    start3d(0x11, 0, 0, 30000, 0x102);
    for (int v = 6; v < nf::Spu2::kVoices; ++v) CHECK(nf::spu().volume_left(v) == 0);
    // Mono (modo 0): os três canais viram a média e a voz de trás perde a fase invertida.
    boot_driver();
    rpc({0x1c, 0});
    start3d(0x11, 5000, 0, 0, 0x103);
    const uint16_t avg = reg((27 + 0 + 12) / 3);
    int mono = 0;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        mono += nf::spu().volume_left(v) == avg && nf::spu().volume_right(v) == avg ? 1 : 0;
    }
    CHECK(mono == 2);
}

TEST(iop_2d_pan_and_the_volume_square) {
    boot_driver();
    // 2D, pan 0: metade em cada lado (o volume já ao quadrado: 100 -> 100).
    rpc({3, 0x10, 0x200});
    int found = 0;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        found += nf::spu().volume_left(v) == reg(50) && nf::spu().volume_right(v) == reg(50) ? 1 : 0;
    }
    CHECK(found == 1);
    // O volume dos efeitos pela metade: (100 x 50 / 100)² / 100 = 25, metade em cada lado -> 12.
    boot_driver();
    rpc({8, 50});
    rpc({3, 0x10, 0x201});
    found = 0;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        found += nf::spu().volume_left(v) == reg(12) && nf::spu().volume_right(v) == reg(12) ? 1 : 0;
    }
    CHECK(found == 1);
}

TEST(iop_max_voices_steals_the_oldest_or_rejects_the_new) {
    boot_driver();
    start3d(0x11, 0, 0, 3000, 0x301);
    tick();
    start3d(0x11, 0, 0, 3000, 0x302);
    tick();
    start3d(0x11, 0, 0, 3000, 0x303);  // MaxVoices 2: o mais velho (0x301) morre
    std::vector<int32_t> active = tick();
    CHECK(count_active(active, 0x11) == 2 && !has_handle(active, 0x301) && has_handle(active, 0x302) &&
          has_handle(active, 0x303));
    // MaxReject: o segundo é recusado e o primeiro fica.
    boot_driver();
    start3d(0x12, 0, 0, 3000, 0x401);
    nf::spu().render(nullptr, 1);  // a voz ligada no fim do lote anterior soa (senão o lote seguinte a acha calada)
    start3d(0x12, 0, 0, 3000, 0x402);
    active = tick();
    CHECK(count_active(active, 0x12) == 1 && has_handle(active, 0x401) && !has_handle(active, 0x402));
}

TEST(iop_voice_budget_steals_the_lowest_priority) {
    // O limite é 15 - SFXStartedCount / 400 vozes (cada começo soma 100): no 14º começo do mesmo lote o limite é 12 e
    // há 13 vozes, então o item de menor prioridade (0x13, prioridade 10, em laço) morre para dar lugar.
    boot_driver();
    nf::sfx_iop_reset_stats();
    std::vector<int32_t> cmds = {3, 0x13, 0x500};
    for (int i = 0; i < 13; ++i) {
        cmds.push_back(3);
        cmds.push_back(0x10);
        cmds.push_back(0x501 + i);
    }
    rpc(cmds);
    const std::vector<int32_t> active = tick();
    CHECK(count_active(active, 0x13) == 0 && count_active(active, 0x10) == 13);
    CHECK(nf::g_sfx_iop_stats.stolen == 1 && nf::g_sfx_iop_stats.started == 14);
}

TEST(iop_rejected_polyphonic_item_does_not_start_samples_it_never_cued) {
    // O trovão de 0700000a (0x5d/0x5e: duas amostras juntas, prioridade 95) com as vozes cheias: o próprio item novo é o
    // de menor prioridade, rouba a si mesmo e é recusado antes de SFXSetup preencher CuedSamples/CueSampleWaiting. Ele
    // fica na lista até o fim do próximo SFXUpdate, que o trata como os outros: num item nunca usado CueSampleWaiting é
    // 0 e CuedSamples nulo, e StartSample leria a amostra no endereço 0 (o IRX leria a memória 0 do IOP; o PC caía).
    // O port não começa uma amostra fora do banco do item (conta em guards) e o item morre no fim do update.
    boot_driver();
    nf::sfx_iop_reset_stats();
    std::vector<int32_t> cmds;
    for (int i = 0; i < 13; ++i) {
        cmds.push_back(3);
        cmds.push_back(0x10);
        cmds.push_back(0x501 + i);
    }
    cmds.push_back(3);
    cmds.push_back(0x15);
    cmds.push_back(0x600);
    rpc(cmds);
    CHECK(nf::g_sfx_iop_stats.rejected == 1 && nf::g_sfx_iop_stats.stolen == 1);
    const std::vector<int32_t> active = tick();
    CHECK(count_active(active, 0x15) == 0 && count_active(active, 0x10) == 13);
    CHECK(nf::g_sfx_iop_stats.guards == 2);
    // Com vozes livres o mesmo som começa e toca as duas amostras.
    boot_driver();
    nf::sfx_iop_reset_stats();
    rpc({3, 0x15, 0x601});
    const std::vector<int32_t> again = tick();
    CHECK(count_active(again, 0x15) == 1 && nf::g_sfx_iop_stats.guards == 0);
    nf::spu().render(nullptr, 4);  // as duas amostras ligaram no fim do lote (o update as começa)
    int sounding = 0;
    for (int v = 0; v < nf::Spu2::kVoices; ++v) sounding += nf::spu().envx(v) != 0 ? 1 : 0;
    CHECK(sounding == 2);
}

TEST(iop_speech_streams_play_to_the_end_and_free_the_channel) {
    boot_driver();
    nf::sfx_iop_reset_stats();
    rpc({3, 0x14, 0x600});
    std::vector<int32_t> active = rpc({4});  // SFXUpdateStreams liga as vozes da fala
    CHECK(count_active(active, 0x14) == 1 && nf::g_sfx_iop_stats.speech == 1);
    nf::spu().render(nullptr, 64);
    active = rpc({4});
    CHECK(nf::sfx_iop_state().speech_playing == 1);
    // 40 blocos x 28 amostras a 0x759 (22 kHz): ~2440 amostras de 48 kHz.
    nf::spu().render(nullptr, 4000);
    active = rpc({4});  // a voz chegou ao fim: o canal para (estado 10)
    active = rpc({4});  // o item vê o fim e solta o canal
    CHECK(count_active(active, 0x14) == 0 && nf::sfx_iop_state().speech_playing == 0);
    // O canal está livre: a fala começa de novo.
    active = rpc({3, 0x14, 0x601});
    active = rpc({4});
    CHECK(count_active(active, 0x14) == 1);
}

// A música segue os marcadores: o trecho 0 repete no laço, o salto pedido acontece no marcador seguinte (o laço) e o
// trecho 1 para no marcador de fim. As duas vozes tocam a 32 kHz (pitch 0xaaa).
TEST(iop_music_loops_jumps_at_the_next_marker_and_ends) {
    boot_driver();
    rpc({0x15, 100, 0x11, kMusicHash, 0});  // volume da música e SFXStartMusic(trecho 0)
    std::vector<int32_t> active = rpc({4});  // SFXUpdateStreams liga as vozes
    std::vector<int> voices;
    for (int v = 0; v < nf::Spu2::kVoices; ++v) {
        if (nf::spu().pitch(v) == 0xaaa) voices.push_back(v);
    }
    CHECK(voices.size() == 2);
    if (voices.size() != 2) return;
    // O trecho 0 dura 2 x 8 blocos x 28 amostras a 32 kHz (672 amostras de 48 kHz): 20000 amostras passam por ~30
    // laços e a música continua, sempre com amostras positivas (o bloqueio de DC as leva a ~0, nunca abaixo).
    std::vector<int16_t> out(2 * 20000);
    nf::spu().render(out.data(), 20000);
    active = rpc({4});
    CHECK(nf::spu().envx(voices[0]) != 0 && nf::spu().envx(voices[1]) != 0);
    CHECK(active[0x51] == 0 && active[0x53] == 0);
    // (As primeiras amostras ficam de fora: a interpolação cúbica desce um pouco abaixo de 0 no degrau do começo.)
    CHECK(nf::spu().volume_left(voices[0]) == 0x3fff && nf::spu().volume_right(voices[1]) == 0x3fff);
    CHECK(*std::min_element(out.begin() + 2 * 16, out.end()) > -64);
    // O salto ao trecho 1 sem IsInstant: o pedido vale já (MusicLastJump), o trecho muda no marcador do laço.
    active = rpc({0x13, 1, 0, 4});
    CHECK(active[0x51] == 1 && active[0x53] == 0);
    std::vector<int16_t> out2(2 * 1000);
    nf::spu().render(out2.data(), 1000);
    active = rpc({4});
    CHECK(active[0x51] == 1 && active[0x53] == 1);
    CHECK(*std::min_element(out2.begin(), out2.end()) < -0x1000);  // as amostras negativas do trecho 1
    // O trecho 1 (672 amostras) acaba no marcador do tipo 9: as vozes param e o canal é solto.
    nf::spu().render(nullptr, 2000);
    rpc({4});
    CHECK(nf::spu().envx(voices[0]) == 0 && nf::spu().envx(voices[1]) == 0);
}

TEST(iop_pause_freezes_voices_and_restores_their_pitch) {
    boot_driver();
    rpc({3, 0x13, 0x700});
    int voice = -1;
    for (int v = 6; v < nf::Spu2::kVoices; ++v) {
        if (nf::spu().pitch(v) == 0x1000 && nf::spu().volume_left(v) != 0) voice = v;
    }
    CHECK(voice != -1);
    nf::spu().render(nullptr, 16);
    rpc({9});  // SFXPause: o pitch vai a 0 (a voz congela, não para)
    CHECK(voice != -1 && nf::spu().pitch(voice) == 0 && nf::spu().envx(voice) != 0);
    rpc({10});
    CHECK(voice != -1 && nf::spu().pitch(voice) == 0x1000);
}

TEST(sound_play_reaches_the_iop_and_the_node_comes_back) {
    psiMemHeapSize = 1u << 21;
    PtrHeap = nullptr;
    Mem_Init();
    psi_sound_debug_power_on();
    sound_debug_power_on();
    nf::sfx_iop_debug_power_on();
    nf::audio_out_debug_power_on();
    nf::sfx_iop_set_root(make_disc());
    VBlankCount = 1000;
    Sound_Init();  // SFXInitialise: o comando 0 vai no próximo lote
    ++VBlankCount;
    SFXUpdate();
    SFXLoadSoundBank(kBankHash);
    CHECK(nf::sfx_iop_state().bank_slot == 0);
    const uint32_t h = Sound_Play(0x10, 100.0f, 0, 0);
    CHECK(h != 0 && sound_nodes_in_use() == 1);
    _VECTOR listener{};
    Sound_UpdateSounds(&listener);  // estado 0 -> SFXStart no lote, estado 1
    ++VBlankCount;
    SFXUpdate();  // o IOP começa o som
    CHECK(SFXIsSFXPlaying(h, -1) == 1);
    // A amostra (112 amostras) acaba no primeiro VBlank misturado; o pedido lembrado vive 5 VBlanks; depois o nó
    // passa a 2 (acabou) e volta à lista livre.
    bool finished = false;
    for (int i = 0; i < 12 && sound_nodes_in_use() != 0; ++i) {
        ++VBlankCount;
        SFXUpdate();
        finished = finished || Sound_IsFinnished(h) == 1;
        Sound_UpdateSounds(&listener);
    }
    CHECK(finished && sound_nodes_in_use() == 0);
    sound_debug_power_on();
    psi_sound_debug_power_on();
    nf::sfx_iop_debug_power_on();
    nf::audio_out_debug_power_on();
    nf::sfx_iop_set_root({});
}

namespace {

// Procura no lote do EE a sequência cmd, a, b (um comando com dois argumentos).
bool batch_has(int32_t cmd, int32_t a, int32_t b) {
    for (int i = 0; i + 2 < SFXBatchPos; ++i) {
        if (SFXBatchCommands[i] == cmd && SFXBatchCommands[i + 1] == a && SFXBatchCommands[i + 2] == b) return true;
    }
    return false;
}

}  // namespace

// A música do EE numa fase com uma linha de MapMusic montada aqui (a de 07000015, cuja função só usa os rodízios):
// o começo da fase pede a faixa e o trecho inicial; com o evento 2 = 1 (inimigos atentos) o contador +0x1c (10)
// esgota em 12 ticks, liga o ataque e pede o primeiro trecho de combate; sem o evento, o contador +0x20 (320) esgota
// em 323 ticks e vem o primeiro furtivo (a música tocava um trecho de combate); o evento 8 (fim da missão) pede o
// trecho de +0x14, imediato, porque a música estava num trecho de combate.
TEST(music_starts_the_track_and_follows_the_attack_and_the_mission_end) {
    std::vector<int32_t> saved(MapMusic, MapMusic + 0x23 * 0x13);
    psi_sound_debug_power_on();
    sound_debug_power_on();
    std::fill(MapMusic, MapMusic + 0x23 * 0x13, 0);
    const int32_t row[0x13] = {0x7000015, 0x24, 0x1b, 4, 3, 0x11, 0xe, 10, 320, 7, 9, -1, 0, 0, 0x1e, 0xc, -1, 0, 0};
    std::copy(row, row + 0x13, MapMusic + 5 * 0x13);
    MapMusic[0] = 0x7000001;  // a linha 0, onde MapMusicData começa (07000001)
    Music_Event(0, 0x7000015);
    Music_Event(1, 1);
    UpdateMusicalEvents();
    CHECK(batch_has(0x11, 0x24, 0x1b));
    CHECK(music_state().level == 0x7000015 && music_state().track == 0x24 && music_state().under_attack == 0);
    SFXBatchPos = 0;
    int ticks = 0;
    while (music_state().under_attack == 0 && ticks < 100) {
        Music_Event(2, 1);
        UpdateMusicalEvents();
        ++ticks;
    }
    CHECK(ticks == 12 && music_state().request == 7 && batch_has(0x13, 7, 0));
    // O IOP diz que o trecho 7 toca.
    SFXActiveData[0x53] = 7;
    SFXBatchPos = 0;
    ticks = 0;
    while (music_state().under_attack != 0 && ticks < 1000) {
        UpdateMusicalEvents();
        ++ticks;
    }
    CHECK(ticks == 323 && music_state().last_jump == 7);
    CHECK(music_state().request == 0x1e && batch_has(0x13, 0x1e, 0));
    SFXBatchPos = 0;
    Music_Event(8, 1);
    UpdateMusicalEvents();
    CHECK(music_state().request == 0x11 && batch_has(0x13, 0x11, 1) && music_state().wait_for_end == 1);
    std::copy(saved.begin(), saved.end(), MapMusic);
    SFXActiveData[0x53] = 0;
    SFXBatchPos = 0;
    sound_debug_power_on();
    psi_sound_debug_power_on();
}
