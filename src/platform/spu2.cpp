// A SPU2 no PC (ver spu2.h).

#include "platform/spu2.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nf {

namespace {

// Coeficientes dos filtros do PS-ADPCM, em 1/64.
constexpr int32_t kFilter0[5] = {0, 60, 115, 98, 122};
constexpr int32_t kFilter1[5] = {0, 0, -52, -55, -60};

// Envelope que o driver programa (ADSR1 0x000f, ADSR2 0x1fc0): ataque linear com taxa 0 (passo +7 << 11),
// soltura linear com taxa 0 (passo -8 << 11). A sustentação (nível 0xf, taxa 0x7f) não muda o nível.
constexpr int32_t kAttackStep = 7 << 11;
constexpr int32_t kReleaseStep = 8 << 11;
constexpr int32_t kEnvMax = 0x7fff;

// O preset "studio C" (modo 4 do sceSdSetEffectAttr), da tabela de presets do LIBSD.IRX (.data 0x4294): os 32
// registradores de reverb na ordem da SPU (dAPF1, dAPF2, vIIR, vCOMB1-4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME,
// mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF,
// dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN). Endereços em unidades de 8 bytes (4 amostras); a área de
// trabalho tem 0xdfc unidades (a tabela de tamanhos em 0x4130 do LIBSD.IRX; 0x6fe0 bytes, o valor que o driver
// passa em PS2_SetupReverb).
constexpr uint16_t kStudioC[32] = {
    0x00e3, 0x00a9, 0x6f60, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xa680, 0x5680, 0x52c0, 0x0dfb, 0x0b58, 0x0d09, 0x0a3c, 0x0bd9, 0x0973,
    0x0b59, 0x08da, 0x08d9, 0x05e9, 0x07ec, 0x04b0, 0x06ef, 0x03d2, 0x05ea, 0x031d, 0x031c, 0x0238, 0x0154, 0x00aa, 0x8000, 0x8000,
};
constexpr uint32_t kReverbSamples = 0xdfc * 4;

enum ReverbReg {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2,
    mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2,
    mRAPF2, vLIN, vRIN
};

int32_t clamp16(int32_t v) { return std::clamp(v, -0x8000, 0x7fff); }
int32_t mul15(int32_t a, int32_t b) { return (a * b) >> 15; }
int32_t sreg(uint16_t r) { return int16_t(r); }
// Volume de voz no modo fixo: 15 bits com sinal (o valor efetivo é reg << 1 em 16 bits).
int32_t voice_volume(uint16_t reg) { return int16_t(uint16_t(reg << 1)); }

}  // namespace

uint8_t adpcm_decode_block(const uint8_t* block, int16_t out[28], AdpcmState& st) {
    int shift = block[0] & 0x0f;
    int filter = (block[0] >> 4) & 0x0f;
    if (filter > 4) filter = 0;  // filtros inexistentes (não ocorrem nos dados)
    if (shift > 12) shift = 9;   // a SPU trata 13-15 como 9
    const int32_t f0 = kFilter0[filter];
    const int32_t f1 = kFilter1[filter];
    for (int i = 0; i < 28; ++i) {
        const uint8_t b = block[2 + i / 2];
        const int32_t nib = (i & 1) ? (b >> 4) : (b & 0x0f);
        int32_t s = int16_t(uint16_t(nib << 12)) >> shift;
        s += (st.s1 * f0 + st.s2 * f1 + 32) >> 6;
        s = clamp16(s);
        st.s2 = st.s1;
        st.s1 = s;
        out[i] = int16_t(s);
    }
    return block[1];
}

Spu2::Spu2() : ram(kRamBytes, 0) {
    for (Reverb& rv : rev_) rv.buf.assign(kReverbSamples, 0);
    reset();
}

// O estado em que o sceSdInit do LIBSD.IRX deixa os núcleos (a função em 0x00000ee0 do módulo, que o driver chama
// em psiInitialiseSoundSystem): MVOL e EVOL 0, a entrada externa do núcleo 1 (a saída do 0) em 0x7fff e a do 0 em
// 0, MMIX 0xff0 / 0xffc. O driver não muda AVOL nem MMIX depois.
void Spu2::reset() {
    for (Voice& v : voices_) v = Voice{};
    for (int c = 0; c < kCores; ++c) {
        mvol_[c] = 0;
        evol_[c] = 0;
        Reverb& rv = rev_[c];
        std::fill(rv.buf.begin(), rv.buf.end(), int16_t(0));
        rv.pos = 0;
        rv.in_l = rv.in_r = rv.out_l = rv.out_r = 0;
    }
    avol_[0] = 0;
    avol_[1] = 0x7fff;
    bvol_[0] = bvol_[1] = 0;
    in_.clear();
    in_read_ = 0;
    in_active_ = false;
    in_consumed_ = 0;
    in_starved_ = 0;
    mmix_[0] = 0xff0;
    mmix_[1] = 0xffc;
    rev_phase_ = false;
    sat_ = Saturation{};
    dc_x_[0] = dc_x_[1] = dc_y_[0] = dc_y_[1] = 0.0f;
}

void Spu2::set_volume_left(int v, uint16_t reg) { voices_[v].vol_l = reg & 0x7fff; }
void Spu2::set_volume_right(int v, uint16_t reg) { voices_[v].vol_r = reg & 0x7fff; }
void Spu2::set_pitch(int v, uint16_t pitch) { voices_[v].pitch = std::min<uint16_t>(pitch, 0x3fff); }
void Spu2::set_start(int v, uint32_t addr) {
    voices_[v].ssa = addr & ~15u;
    voices_[v].ext = nullptr;
    voices_[v].ext_size = 0;
    voices_[v].source = nullptr;
}
void Spu2::set_source(int v, Spu2BlockSource* src) {
    voices_[v].source = src;
    voices_[v].ext = nullptr;
    voices_[v].ext_size = 0;
}
void Spu2::set_stream(int v, const uint8_t* data, uint32_t size) {
    voices_[v].ext = data;
    voices_[v].ext_size = size & ~15u;
    voices_[v].ssa = 0;
}
void Spu2::detach(const uint8_t* data) {
    if (data == nullptr) return;
    for (Voice& vc : voices_) {
        if (vc.ext != data) continue;
        vc.ext = nullptr;
        vc.ext_size = 0;
        vc.on = false;
        vc.releasing = false;
        vc.env = 0;
        vc.key_delay = -1;
    }
}

void Spu2::set_reverb_send(int v, bool on) { voices_[v].reverb = on; }
uint16_t Spu2::envx(int v) const { return uint16_t(voices_[v].env); }
uint16_t Spu2::pitch(int v) const { return voices_[v].pitch; }
uint16_t Spu2::volume_left(int v) const { return voices_[v].vol_l; }
uint16_t Spu2::volume_right(int v) const { return voices_[v].vol_r; }
bool Spu2::end_flag(int v) const { return voices_[v].endx; }
void Spu2::set_master_volume(uint16_t reg) { mvol_[0] = mvol_[1] = reg; }
void Spu2::set_master_volume(int core, uint16_t reg) { mvol_[core & 1] = reg; }
void Spu2::set_effect_volume(uint16_t reg) { evol_[0] = evol_[1] = reg; }
void Spu2::set_effect_volume(int core, uint16_t reg) { evol_[core & 1] = reg; }
void Spu2::set_input_volume(int core, uint16_t reg) { bvol_[core & 1] = reg; }

void Spu2::input_push(const int16_t* stereo, size_t frames) {
    if (frames == 0) return;
    if (in_read_ >= (1u << 16)) {
        in_.erase(in_.begin(), in_.begin() + std::ptrdiff_t(in_read_));
        in_read_ = 0;
    }
    in_.insert(in_.end(), stereo, stereo + frames * 2);
}

void Spu2::input_start() {
    in_active_ = true;
    in_consumed_ = 0;
    in_starved_ = 0;
}

void Spu2::input_stop() {
    in_active_ = false;
    in_.clear();
    in_read_ = 0;
    in_consumed_ = 0;
    in_starved_ = 0;
}

void Spu2::key_on(int v, int delay_samples) {
    Voice& vc = voices_[v];
    if (delay_samples > 0) {
        vc.key_delay = delay_samples;
        return;
    }
    start_voice(vc);
}

void Spu2::key_off(int v) {
    Voice& vc = voices_[v];
    vc.key_delay = -1;
    if (vc.on) vc.releasing = true;
}

int Spu2::voices_sounding() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.env > 0 ? 1 : 0;
    return n;
}

const uint8_t* Spu2::voice_data(const Voice& vc, uint32_t addr) const {
    if (vc.ext != nullptr) return addr + 16 <= vc.ext_size ? vc.ext + addr : nullptr;
    return addr + 16 <= kRamBytes ? ram.data() + addr : nullptr;
}

uint32_t Spu2::voice_size(const Voice& vc) const { return vc.ext != nullptr ? vc.ext_size : kRamBytes; }

void Spu2::start_voice(Voice& vc) {
    vc.on = true;
    vc.releasing = false;
    vc.endx = false;
    vc.key_delay = -1;
    vc.env = 0;
    vc.addr = vc.ssa;
    vc.loop_addr = vc.ssa;
    vc.stop_after = false;
    vc.adpcm = AdpcmState{};
    vc.block_pos = 28;
    std::memset(vc.hist, 0, sizeof(vc.hist));
    vc.counter = 0;
}

// A próxima amostra decodificada da voz (avança um bloco quando o atual acaba).
int16_t Spu2::next_sample(Voice& vc) {
    if (vc.block_pos >= 28) {
        if (vc.stop_after) {
            // Fim sem repetição no bloco anterior: a SPU marca ENDX e zera o envelope.
            vc.on = false;
            vc.env = 0;
            vc.stop_after = false;
            return 0;
        }
        const uint8_t* blk = vc.source != nullptr ? vc.source->next_block(int(&vc - voices_)) : voice_data(vc, vc.addr);
        if (blk == nullptr) {  // passou do fim da RAM ou do buffer: a voz para
            vc.on = false;
            vc.env = 0;
            vc.endx = true;
            return 0;
        }
        const uint8_t flags = adpcm_decode_block(blk, vc.block, vc.adpcm);
        if (flags & 4) vc.loop_addr = vc.addr;
        vc.block_pos = 0;
        if (flags & 1) {
            vc.endx = true;
            if (flags & 2) {
                vc.addr = vc.loop_addr;
            } else {
                vc.stop_after = true;
                vc.addr = vc.loop_addr;
            }
        } else {
            vc.addr += 16;
            if (vc.addr >= voice_size(vc)) vc.addr = 0;
        }
    }
    return vc.block[vc.block_pos++];
}

void Spu2::reverb_process(Reverb& rv, int32_t in_l, int32_t in_r) {
    const uint16_t* r = kStudioC;
    const uint32_t size = kReverbSamples;
    auto at = [&](uint32_t reg_units, int32_t delta) -> int16_t& {
        const uint32_t i = (rv.pos + reg_units * 4 + uint32_t(int32_t(size) + delta)) % size;
        return rv.buf[i];
    };
    const int32_t lin = mul15(in_l, sreg(r[vLIN]));
    const int32_t rin = mul15(in_r, sreg(r[vRIN]));
    const int32_t iir = sreg(r[vIIR]);
    const int32_t wall = sreg(r[vWALL]);
    // Reflexões do mesmo lado e do outro lado.
    const int32_t lsame = clamp16(mul15(lin + mul15(at(r[dLSAME], 0), wall) - at(r[mLSAME], -1), iir) + at(r[mLSAME], -1));
    const int32_t rsame = clamp16(mul15(rin + mul15(at(r[dRSAME], 0), wall) - at(r[mRSAME], -1), iir) + at(r[mRSAME], -1));
    const int32_t ldiff = clamp16(mul15(lin + mul15(at(r[dRDIFF], 0), wall) - at(r[mLDIFF], -1), iir) + at(r[mLDIFF], -1));
    const int32_t rdiff = clamp16(mul15(rin + mul15(at(r[dLDIFF], 0), wall) - at(r[mRDIFF], -1), iir) + at(r[mRDIFF], -1));
    at(r[mLSAME], 0) = int16_t(lsame);
    at(r[mRSAME], 0) = int16_t(rsame);
    at(r[mLDIFF], 0) = int16_t(ldiff);
    at(r[mRDIFF], 0) = int16_t(rdiff);
    // Ecos (pentes).
    int32_t lout = mul15(sreg(r[vCOMB1]), at(r[mLCOMB1], 0)) + mul15(sreg(r[vCOMB2]), at(r[mLCOMB2], 0)) +
                   mul15(sreg(r[vCOMB3]), at(r[mLCOMB3], 0)) + mul15(sreg(r[vCOMB4]), at(r[mLCOMB4], 0));
    int32_t rout = mul15(sreg(r[vCOMB1]), at(r[mRCOMB1], 0)) + mul15(sreg(r[vCOMB2]), at(r[mRCOMB2], 0)) +
                   mul15(sreg(r[vCOMB3]), at(r[mRCOMB3], 0)) + mul15(sreg(r[vCOMB4]), at(r[mRCOMB4], 0));
    // Dois passa-tudo.
    auto apf = [&](int32_t x, uint16_t m, uint16_t d, uint16_t v) {
        const int32_t delayed = at(m, -int32_t(d) * 4);
        x = clamp16(x - mul15(sreg(v), delayed));
        at(m, 0) = int16_t(x);
        return clamp16(mul15(x, sreg(v)) + delayed);
    };
    lout = apf(lout, r[mLAPF1], r[dAPF1], r[vAPF1]);
    rout = apf(rout, r[mRAPF1], r[dAPF1], r[vAPF1]);
    lout = apf(lout, r[mLAPF2], r[dAPF2], r[vAPF2]);
    rout = apf(rout, r[mRAPF2], r[dAPF2], r[vAPF2]);
    rv.out_l = lout;
    rv.out_r = rout;
    rv.pos = (rv.pos + 1) % size;
}

void Spu2::render(int16_t* out, int frames) {
    // Volumes de cada núcleo: MVOL no modo fixo de 15 bits (0x3fff -> 0x7ffe, quase 1.0), EVOL e AVOL de 16 bits com
    // sinal (0x7fff = quase 1.0: o EVOL do driver, no máximo 0x3332, é 0.4).
    int32_t mvol[kCores], evol[kCores], avol[kCores];
    for (int c = 0; c < kCores; ++c) {
        mvol[c] = voice_volume(mvol_[c]);
        evol[c] = sreg(evol_[c]);
        avol[c] = sreg(avol_[c]);
    }
    const int32_t bvol0 = sreg(bvol_[0]);
    for (int f = 0; f < frames; ++f) {
        int32_t dry_l[kCores] = {}, dry_r[kCores] = {}, wet_l[kCores] = {}, wet_r[kCores] = {};
        for (int vi = 0; vi < kVoices; ++vi) {
            Voice& vc = voices_[vi];
            if (vc.key_delay >= 0 && --vc.key_delay < 0) start_voice(vc);
            if (!vc.on && vc.env == 0) continue;
            // Envelope.
            if (vc.releasing) {
                vc.env = std::max(0, vc.env - kReleaseStep);
                if (vc.env == 0) vc.on = false;
            } else if (vc.on) {
                vc.env = std::min(kEnvMax, vc.env + kAttackStep);
            }
            if (!vc.on && vc.env == 0) continue;
            // Interpolação de 4 pontos (Hermite) entre hist[1] e hist[2]; o contador de 12 bits anda pelo pitch.
            const float t = float(vc.counter & 0xfff) / 4096.0f;
            const float p0 = vc.hist[0], p1 = vc.hist[1], p2 = vc.hist[2], p3 = vc.hist[3];
            const float c1 = 0.5f * (p2 - p0);
            const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
            const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
            const int32_t s = clamp16(int32_t(((c3 * t + c2) * t + c1) * t + p1));
            vc.counter += vc.pitch;
            while (vc.counter >= 0x1000) {
                vc.counter -= 0x1000;
                vc.hist[0] = vc.hist[1];
                vc.hist[1] = vc.hist[2];
                vc.hist[2] = vc.hist[3];
                vc.hist[3] = vc.on ? next_sample(vc) : 0;
            }
            const int32_t v = mul15(s, vc.env);
            const int32_t l = mul15(v, voice_volume(vc.vol_l));
            const int32_t r = mul15(v, voice_volume(vc.vol_r));
            const int c = core_of(vi);
            dry_l[c] += l;
            dry_r[c] += r;
            if (vc.reverb) {
                wet_l[c] += l;
                wet_r[c] += r;
            }
        }
        // As somas de cada núcleo saturam em 16 bits (são as amostras da área de saída da RAM); o MMIX decide o que
        // entra no seco (bits 10-11) e no reverb (bits 8-9).
        int32_t sdry[kCores][2], swet[kCores][2];
        for (int c = 0; c < kCores; ++c) {
            sdry[c][0] = clamp16(dry_l[c]);
            sdry[c][1] = clamp16(dry_r[c]);
            swet[c][0] = clamp16(wet_l[c]);
            swet[c][1] = clamp16(wet_r[c]);
            sat_.voices[c] += uint64_t((sdry[c][0] != dry_l[c]) + (sdry[c][1] != dry_r[c]));
            if (!(mmix_[c] & 0x800)) sdry[c][0] = 0;
            if (!(mmix_[c] & 0x400)) sdry[c][1] = 0;
            if (!(mmix_[c] & 0x200)) swet[c][0] = 0;
            if (!(mmix_[c] & 0x100)) swet[c][1] = 0;
        }
        // A entrada de dados do núcleo 0 (os filmes): uma amostra da fila por amostra de saída, vezes o BVOL; o MMIX
        // 0xff0 a põe no seco (bits 7/6) e no reverb (bits 5/4).
        int32_t inp[2] = {0, 0};
        if (in_active_) {
            if (in_read_ + 2 <= in_.size()) {
                inp[0] = mul15(in_[in_read_], bvol0);
                inp[1] = mul15(in_[in_read_ + 1], bvol0);
                in_read_ += 2;
                ++in_consumed_;
            } else {
                ++in_starved_;
            }
        }
        const int32_t inp_dry[2] = {(mmix_[0] & 0x80) ? inp[0] : 0, (mmix_[0] & 0x40) ? inp[1] : 0};
        // O reverb de cada núcleo a 24 kHz: a média de duas amostras entra, a saída vale para as duas.
        for (int c = 0; c < kCores; ++c) {
            rev_[c].in_l += swet[c][0];
            rev_[c].in_r += swet[c][1];
        }
        rev_[0].in_l += (mmix_[0] & 0x20) ? inp[0] : 0;
        rev_[0].in_r += (mmix_[0] & 0x10) ? inp[1] : 0;
        if (rev_phase_) {
            for (Reverb& rv : rev_) {
                reverb_process(rv, clamp16(rv.in_l / 2), clamp16(rv.in_r / 2));
                rv.in_l = rv.in_r = 0;
            }
        }
        rev_phase_ = !rev_phase_;
        // Núcleo 0: seco + a entrada de dados + reverb x EVOL, x MVOL, saturado (é a entrada externa do núcleo 1).
        int32_t ext[2];
        for (int ch = 0; ch < 2; ++ch) {
            const int32_t rev_out = ch == 0 ? rev_[0].out_l : rev_[0].out_r;
            const int64_t sum0 = int64_t(sdry[0][ch]) + inp_dry[ch] + mul15(rev_out, evol[0]);
            const int64_t o0 = (sum0 * mvol[0]) >> 15;
            ext[ch] = int32_t(std::clamp<int64_t>(o0, -0x8000, 0x7fff));
            sat_.core0 += uint64_t(int64_t(ext[ch]) != o0);
        }
        // Núcleo 1: seco + a entrada externa x AVOL (só no seco: MMIX 0xffc) + reverb x EVOL, x MVOL, saturado (o DAC).
        int32_t mixed[2];
        for (int ch = 0; ch < 2; ++ch) {
            const int32_t e = mul15(ext[ch], avol[1]);
            const int32_t rev_out = ch == 0 ? rev_[1].out_l : rev_[1].out_r;
            int64_t sum1 = int64_t(sdry[1][ch]) + mul15(rev_out, evol[1]);
            if (mmix_[1] & (ch == 0 ? 0x8 : 0x4)) sum1 += e;
            const int64_t o1 = (sum1 * mvol[1]) >> 15;
            const int32_t o1c = int32_t(std::clamp<int64_t>(o1, -0x7fffffff, 0x7fffffff));
            mixed[ch] = clamp16(o1c);
            sat_.output += uint64_t(mixed[ch] != o1c);
            sat_.peak = std::max(sat_.peak, std::abs(o1c));
        }
        // O capacitor da saída analógica (y = x - x_ant + 0.9995 y_ant: ~4 Hz).
        for (int c = 0; c < 2; ++c) {
            const float x = float(mixed[c]);
            dc_y_[c] = x - dc_x_[c] + 0.9995f * dc_y_[c];
            dc_x_[c] = x;
            if (out != nullptr) out[2 * f + c] = int16_t(clamp16(int32_t(std::lround(dc_y_[c]))));
        }
    }
    rendered_ += uint64_t(frames);
}

}  // namespace nf
