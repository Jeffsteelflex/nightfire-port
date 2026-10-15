// O hardware do som das missões de carro (driving/sound/snd_spu.h).

#include "driving/sound/snd_spu.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "driving/sound/snddrv_iop.h"

namespace drv {

const int16_t* snddrv_iop_output(int32_t* frames);

namespace {

constexpr int kTickFrames = 512;

// As funções do libsd sobre a Spu2: o número tem o parâmetro no byte alto e, nos de voz, a voz x 2 | o núcleo no baixo
// (nos do núcleo o bit 7 e o núcleo no bit 0).
class SpuLibsd : public SndLibsd {
public:
    nf::Spu2 spu;
    uint16_t adsr[48][2] = {};
    uint32_t eea[2] = {0x1e0000, 0x1e0000};
    int fx_type[2] = {0, 0};
    uint64_t unknown = 0;

    static int voice_of(uint16_t entry) { return ((entry >> 1) & 0x1f) + ((entry & 1) != 0 ? 24 : 0); }

    void Init(int32_t) override { spu.reset(); }
    void SetParam(uint16_t entry, uint16_t value) override {
        const int id = entry >> 8;
        if ((entry & 0x80) != 0) {
            const int core = entry & 1;
            switch (id) {
                case 0x08: spu.set_mmix(core, value); return;
                case 0x09: spu.set_master_volume(core, value); return;   // MVOLL (o R é igual no driver)
                case 0x0a: return;                                       // MVOLR
                case 0x0b: spu.set_effect_volume(core, value); return;   // EVOLL
                case 0x0c: return;                                       // EVOLR
                case 0x0f: spu.set_input_volume(core, value); return;    // BVOLL
                case 0x10: return;                                       // BVOLR
                default: ++unknown; return;
            }
        }
        const int v = voice_of(entry);
        switch (id) {
            case 0x00: spu.set_volume_reg(v, 0, value); return;
            case 0x01: spu.set_volume_reg(v, 1, value); return;
            case 0x02: spu.set_pitch(v, value); return;
            case 0x03:
                adsr[v][0] = value;
                spu.set_adsr(v, adsr[v][0], adsr[v][1]);
                return;
            case 0x04:
                adsr[v][1] = value;
                spu.set_adsr(v, adsr[v][0], adsr[v][1]);
                return;
            default: ++unknown; return;
        }
    }
    uint16_t GetParam(uint16_t entry) override {
        const int id = entry >> 8;
        if ((entry & 0x80) != 0) {
            const int core = entry & 1;
            if (id == 0x0b || id == 0x0c) return spu.effect_volume(core);
            ++unknown;
            return 0;
        }
        const int v = voice_of(entry);
        switch (id) {
            case 0x05: return spu.envx(v);
            case 0x06: return uint16_t(spu.volume_current(v, 0));
            case 0x07: return uint16_t(spu.volume_current(v, 1));
            default: ++unknown; return 0;
        }
    }
    void SetSwitch(uint16_t entry, uint32_t value) override {
        const int id = entry >> 8, core = entry & 1;
        for (int i = 0; i < 24; ++i) {
            const int v = core * 24 + i;
            const bool on = ((value >> i) & 1) != 0;
            switch (id) {
                case 0x15: if (on) spu.key_on(v); break;
                case 0x16: if (on) spu.key_off(v); break;
                case 0x18: spu.set_dry(v, 0, on); break;
                case 0x1a: spu.set_dry(v, 1, on); break;
                case 0x19: spu.set_reverb_send(v, on); break;
                case 0x1b: break;  // VMIXER: o driver o escreve igual ao VMIXEL
                default: ++unknown; return;
            }
        }
    }
    void SetAddr(uint16_t entry, uint32_t addr) override {
        const int id = entry >> 8;
        if (id == 0x1d) {
            eea[entry & 1] = addr;
            return;
        }
        const int v = voice_of(entry);
        if (entry == (0x2040 | (entry & 0x3f))) spu.set_start(v, addr);
        else if (entry == (0x2140 | (entry & 0x3f))) spu.set_loop_start(v, addr);
        else ++unknown;
    }
    uint32_t GetAddr(uint16_t entry) override {
        const int id = entry >> 8;
        if (id == 0x1c) {
            // ESA: o começo da área do reverb (a SPU2 do PC guarda o reverb fora da RAM; a área só é zerada). O registro
            // EEA da SPU2 guarda só os bits 17-20 do endereço (o libsd escreve addr >> 17): a área acaba no fim do bloco
            // de 128 KB que contém o EEA pedido (0x1c0000 -> 0x1dffff), e começa o tamanho do tipo abaixo disso. O
            // SNDDRV_setspufx do EE reserva exatamente isso (0x1e0000 - 0xf6c0 = 0x1d0940 no tipo 6 do núcleo 1).
            static constexpr uint32_t kUnits[10] = {2, 0x4d8, 0x3e8, 0x908, 0xdfc, 0x15bc, 0x1ed8, 0x3008, 0x3008, 0x780};
            const int core = entry & 1;
            const uint32_t end = ((eea[core] >> 17) + 1) << 17;
            return end - kUnits[fx_type[core]] * 8;
        }
        if (entry == (0x2240 | (entry & 0x3f))) return spu.next_address(voice_of(entry));
        ++unknown;
        return 0;
    }
    void SetCoreAttr(uint16_t, uint16_t) override {}
    void SetEffectAttr(int32_t core, const SndEffectAttr& a) override {
        const int t = a.mode < 0 || a.mode > 9 ? 0 : a.mode;
        fx_type[core & 1] = t;
        spu.set_reverb_type(core & 1, t);
    }
    void VoiceTrans(int32_t, int32_t mode, const uint8_t* src, uint32_t dst, uint32_t size) override {
        if (mode != 0 || src == nullptr || dst >= nf::Spu2::kRamBytes) {
            ++unknown;
            return;
        }
        std::memcpy(spu.ram.data() + dst, src, std::min<size_t>(size, nf::Spu2::kRamBytes - dst));
    }
    void BlockTrans(int32_t, int32_t mode, int16_t*, uint32_t) override {
        if ((mode & 0x10) != 0) {
            spu.input_start();
            // A metade que a SPU2 toca primeiro (o IOP enche a outra): silêncio, um tick.
            std::vector<int16_t> zeros(kTickFrames * 2, 0);
            spu.input_push(zeros.data(), kTickFrames);
        } else {
            spu.input_stop();
        }
    }
};

SpuLibsd* g_sd = nullptr;

int g_pos = 0;  // amostras do tick atual já tocadas
uint64_t g_ticks = 0;

SpuLibsd& sd() {
    if (g_sd == nullptr) g_sd = new SpuLibsd();
    return *g_sd;
}

}  // namespace

nf::Spu2& snd_spu() { return sd().spu; }
SndLibsd* snd_spu_libsd() { return &sd(); }

void snd_spu_power_on() {
    SpuLibsd& s = sd();
    s.spu.reset();
    std::fill(s.spu.ram.begin(), s.spu.ram.end(), uint8_t(0));
    for (auto& a : s.adsr) a[0] = a[1] = 0;
    s.eea[0] = s.eea[1] = 0x1e0000;
    s.fx_type[0] = s.fx_type[1] = 0;
    s.unknown = 0;
    g_pos = 0;
    g_ticks = 0;
}

uint64_t snd_ticks() { return g_ticks; }

void snd_render(int16_t* out, int frames) {
    while (frames > 0) {
        if (g_pos == 0) {
            snd_driver_tick();
            ++g_ticks;
            int32_t n = 0;
            const int16_t* half = snddrv_iop_output(&n);
            // A metade é em blocos de 256 amostras: o canal 1 do EE (esquerdo), depois o 0 (direito).
            int16_t stereo[kTickFrames * 2];
            for (int b = 0; b < 2; ++b)
                for (int i = 0; i < 256; ++i) {
                    stereo[(b * 256 + i) * 2 + 0] = half[b * 512 + i];
                    stereo[(b * 256 + i) * 2 + 1] = half[b * 512 + 256 + i];
                }
            if (sd().spu.input_active()) sd().spu.input_push(stereo, kTickFrames);
        }
        const int n = std::min(frames, kTickFrames - g_pos);
        sd().spu.render(out, n);
        if (out != nullptr) out += n * 2;
        frames -= n;
        g_pos = (g_pos + n) % kTickFrames;
    }
}

}  // namespace drv
