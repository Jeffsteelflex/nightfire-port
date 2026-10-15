// nf_driving_check, parte 13 (tests/driving_check_sound.h): o som das missões de carro. O código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS lado a lado com o porte (driving/sound/):
//  - o EA-XA: SND::CEAXABLKDecf (o construtor 0x318d90, Feed 0x318db0, Decode 0x318dd8 com o SND::decodexac 0x318900)
//    sobre pacotes de verdade dos fluxos do disco (os SCDl dos .asf dos .MUS e .SPE, cada canal com o seu estado), em
//    pedidos de tamanho sorteado (o bloco pela metade, o resto guardado): as amostras (float, bit a bit), o que cada
//    Decode devolve e o estado do decodificador; e, só no porte, quantos blocos de todos os fluxos usam um filtro fora
//    da tabela (a guarda de eaxa.h);
//  - o núcleo do SND (sistema, bancos, vozes, a plataforma do PS2): a iniciação, todos os bancos de todas as missões
//    (as etiquetas, o alocador da RAM da SPU2 e do IOP, as transferências) e uma sequência sorteada da API sobre os
//    bancos da Paris com os ticks do driver no meio: o estado inteiro do SND comparado depois de cada operação;
//  - a camada do jogo (BondAudioPS2): o AVoice::Play (com o View::Play/Stop/Push e a lista ativa) e o AVoice::Set
//    sorteados, o APath::Build sobre ouvintes e sons sorteados e o carro do jogador quadro a quadro
//    (APlayerVehicle::Play com o AVehicle::Play, o ARaceEngine::Play, o AEngine::Play, o AVehicleWind e os avulsos que
//    criam, mais o PlayCollision e o PlayLanding no meio), com os mesmos bancos, volumes e índices da Paris dos dois
//    lados. Ganchos nativos: o FastAlloc/new (um alocador linear), o strcpy, o AIndex::Lookup (o mapa da STL: o do porte
//    nos dois lados), o AMix::Add/Get (a mixagem do mesmo nome) e o atan2f do newlib (o nf::fm::atan2, como no porte).
//
// Tudo aqui lê dados do jogo: a saída é um relatório, nada é gravado.

#include "driving_check_sound.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/eaxa.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snddrv_iop.h"
#include "platform/crt_compat.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/spu2.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kFnXaCtor = 0x318d90, kFnXaFeed = 0x318db0, kFnXaDecode = 0x318dd8;

// A RAM emulada.
constexpr uint32_t kXaObj = 0x01000000;   // o CEAXABLKDecf (0xa8 bytes)
constexpr uint32_t kXaOutPtr = 0x01000100;  // o float* que o Decode recebe por endereço
constexpr uint32_t kXaIn = 0x01010000;    // o pacote do canal
constexpr uint32_t kXaOut = 0x01200000;   // as amostras

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x50d) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}
uint32_t fbits(float f) { return ps2::fbits(f); }

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Um pacote de um canal de um SCDl: os dois s16 do estado, os blocos e quantas amostras.
struct XaPacket {
    int16_t s1 = 0, s2 = 0;
    std::vector<uint8_t> blocks;
    int32_t frames = 0;
};

// Os SCDl de um .asf (SCHl ... SCEl): para cada canal, o pacote como o SNDSTRMI_parsedata o acha (o canal c começa em
// chunk + 0xc + canais x 4 + offset[c]).
void asf_packets(const std::vector<uint8_t>& asf, int channels, std::vector<XaPacket>& out) {
    size_t p = 0;
    while (p + 8 <= asf.size()) {
        const uint32_t id = rd32(&asf[p]), size = rd32(&asf[p + 4]);
        if (size < 8 || p + size > asf.size()) break;
        if (id == 0x6c444353u /* SCDl */ && size >= 0xc + uint32_t(channels) * 4) {
            const int32_t frames = int32_t(rd32(&asf[p + 8]) & 0x7fffffff);
            for (int c = 0; c < channels; ++c) {
                const size_t at = p + 0xc + size_t(channels) * 4 + rd32(&asf[p + 0xc + size_t(c) * 4]);
                const size_t blocks = (size_t(frames) + 27) / 28 * 15;
                if (at + 4 + blocks > p + size) continue;
                XaPacket pk;
                std::memcpy(&pk.s1, &asf[at], 2);
                std::memcpy(&pk.s2, &asf[at + 2], 2);
                pk.blocks.assign(asf.begin() + std::ptrdiff_t(at + 4), asf.begin() + std::ptrdiff_t(at + 4 + blocks));
                pk.frames = frames;
                out.push_back(std::move(pk));
            }
        }
        if (id == 0x6c454353u /* SCEl */) break;
        p += size;
    }
}

// Os canais de um SCHl: a etiqueta 0x82 do PT (1 sem ela).
int asf_channels(const std::vector<uint8_t>& asf) {
    if (asf.size() < 16 || rd32(&asf[0]) != 0x6c484353u) return 0;
    const uint32_t size = rd32(&asf[4]);
    size_t p = 8;
    if (p + 4 > asf.size() || asf[p] != 'P' || asf[p + 1] != 'T') return 0;
    p += 4;
    int channels = 1;
    while (p < size && p < asf.size()) {
        const uint8_t tag = asf[p++];
        if (tag == 0xff) break;
        if (tag == 0xfc || tag == 0xfd || tag == 0xfe) continue;
        if (p >= asf.size()) break;
        uint32_t n = asf[p++];
        uint32_t v = 0;
        for (uint32_t i = 0; i < n && p < asf.size(); ++i) v = v << 8 | asf[p++];
        if (tag == 0x82) channels = int(v);
    }
    return channels;
}

class SoundEmu {
public:
    MipsMachine m;
    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
    }
    uint32_t call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 200'000'000);
        return uint32_t(m.gpr(R::v0));
    }
    float rdf(uint32_t a) const { return ps2::ffloat(m.read32(a)); }
};

// ---- o EA-XA ----

int check_eaxa(SoundEmu& e, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n-- EA-XA (SND::CEAXABLKDecf, SND::decodexac) --\n");
    // Todos os fluxos dos .MUS e .SPE.
    std::vector<XaPacket> packets;
    uint64_t streams = 0, blocks_total = 0, guarded_blocks = 0;
    std::vector<std::string> archives;
    for (const char* a : {"MIS01", "MIS11", "MIS13A", "MIS13B", "MIS13C", "MIS3", "MIS4"}) {
        archives.push_back(std::string("DRIVING/") + a + ".MUS");
        archives.push_back(std::string("DRIVING/") + a + "EN.SPE");
    }
    Rng rng(seed ^ 0xea7a);
    for (const std::string& path : archives) {
        drv::Bigf arc;
        if (!arc.open(disc, path)) continue;
        for (int i = 0; i < int(arc.entries().size()); ++i) {
            std::vector<uint8_t> asf;
            if (!arc.read(i, asf)) continue;
            const int ch = asf_channels(asf);
            if (ch < 1 || ch > 8) continue;
            ++streams;
            std::vector<XaPacket> pk;
            asf_packets(asf, ch, pk);
            for (const XaPacket& p : pk) {
                const size_t nb = p.blocks.size() / 15;
                blocks_total += nb;
                for (size_t b = 0; b < nb; ++b)
                    if ((p.blocks[b * 15] >> 4) >= 4) ++guarded_blocks;
            }
            // Uns poucos pacotes de cada fluxo para a conferência no interpretador.
            for (const XaPacket& p : pk)
                if (packets.size() < size_t(cases) * 4 && rng.chance(0.02)) packets.push_back(p);
        }
    }
    std::printf("  %" PRIu64 " fluxos, %" PRIu64 " blocos; filtros fora da tabela: %" PRIu64 "\n", streams, blocks_total,
                guarded_blocks);
    if (packets.empty()) {
        std::printf("  sem pacotes (os .MUS/.SPE não abriram)\n");
        return 1;
    }
    // Os pacotes sorteados em sequência por um decodificador de cada lado (como o SFILTER_unpackxapf: SetState com os
    // dois s16, Feed, Decode em pedidos de 1 a 2000 até acabar).
    drv::CEAXABLKDecf port;
    drv::CEAXABLKDecf_Construct(port);
    e.call(kFnXaCtor, {kXaObj});
    std::vector<float> pout(4096);
    uint64_t bad = 0, decodes = 0, samples = 0, partial = 0, n_packets = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };
    const int limit = std::min<int>(int(packets.size()), cases);
    for (int k = 0; k < limit && bad == 0; ++k) {
        const XaPacket& p = packets[size_t(rng.range(0, int(packets.size()) - 1))];
        ++n_packets;
        // SetState: cvt.s.w dos dois s16 (exato).
        const float s1 = float(p.s1), s2 = float(p.s2);
        e.m.write32(kXaObj + 0x98, fbits(s1));
        e.m.write32(kXaObj + 0x9c, fbits(s2));
        drv::CEAXABLKDecf_SetState(port, s1, s2);
        std::memcpy(e.m.mem(kXaIn, uint32_t(p.blocks.size())), p.blocks.data(), p.blocks.size());
        const uint32_t r_emu = e.call(kFnXaFeed, {kXaObj, kXaIn, uint32_t(p.frames * 4), uint32_t(p.frames)});
        const int32_t r_port = drv::CEAXABLKDecf_Feed(port, p.blocks.data(), p.frames * 4, p.frames);
        if (int32_t(r_emu) != r_port) {
            fail("Feed: original " + std::to_string(int32_t(r_emu)) + ", porte " + std::to_string(r_port));
            break;
        }
        for (int guard = 0; guard < 100000; ++guard) {
            const int32_t want = rng.chance(0.3) ? rng.range(1, 27) : rng.range(1, 2000);
            e.m.write32(kXaOutPtr, kXaOut);
            std::memset(e.m.mem(kXaOut, uint32_t(want) * 4), 0xcd, size_t(want) * 4);
            const int32_t n_emu = int32_t(e.call(kFnXaDecode, {kXaObj, kXaOutPtr, uint32_t(want)}));
            float* out = pout.data();
            std::fill(pout.begin(), pout.begin() + want, 0.0f);
            const int32_t n_port = drv::CEAXABLKDecf_Decode(port, &out, want);
            ++decodes;
            if (n_emu != n_port) {
                fail("Decode(" + std::to_string(want) + "): original " + std::to_string(n_emu) + ", porte " +
                     std::to_string(n_port));
                break;
            }
            for (int32_t i = 0; i < n_emu; ++i) {
                const uint32_t a = e.m.read32(kXaOut + uint32_t(i) * 4), b = fbits(pout[size_t(i)]);
                if (a != b) {
                    fail("amostra " + std::to_string(i) + " do Decode(" + std::to_string(want) + "): original " +
                         hex(a) + ", porte " + hex(b));
                    break;
                }
            }
            samples += uint64_t(n_emu);
            if (n_emu % 28 != 0) ++partial;
            // O estado: o que resta, o buffer, o estado do filtro, a entrada.
            const int32_t left = int32_t(e.m.read32(kXaObj + 4)), frames = int32_t(e.m.read32(kXaObj + 8));
            const uint32_t in = e.m.read32(kXaObj + 0xa0);
            const uint32_t es1 = e.m.read32(kXaObj + 0x98), es2 = e.m.read32(kXaObj + 0x9c);
            if (left != port.left || frames != port.frames || in - kXaIn != uint32_t(port.pkt.in - p.blocks.data()) ||
                es1 != fbits(port.pkt.s1) || es2 != fbits(port.pkt.s2)) {
                fail("estado depois do Decode(" + std::to_string(want) + "): original resta " + std::to_string(left) +
                     " quadros " + std::to_string(frames) + " entrada +" + hex(in - kXaIn) + " s " + hex(es1) + "," +
                     hex(es2) + "; porte " + std::to_string(port.left) + " " + std::to_string(port.frames) + " +" +
                     hex(uint32_t(port.pkt.in - p.blocks.data())) + " " + hex(fbits(port.pkt.s1)) + "," +
                     hex(fbits(port.pkt.s2)));
            }
            if (bad != 0 || n_emu == 0) break;
        }
    }
    std::printf("  %" PRIu64 " pacotes, %" PRIu64 " Decode (%" PRIu64 " com bloco pela metade), %" PRIu64
                " amostras: %" PRIu64 " diferentes\n",
                n_packets, decodes, partial, samples, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    return bad ? 1 : 0;
}

// ---- o núcleo do SND (EE) ----

// As funções do original.
constexpr uint32_t kFnSetOpts = 0x2e6c88, kFnSysInit = 0x3074c8, kFnMasterVol = 0x309f30;
constexpr uint32_t kFnBankAdd = 0x308088, kFnBankRemove = 0x308300, kFnBankPlay = 0x3086c8, kFnPlaySetDef = 0x30a8e8;
constexpr uint32_t kFnStop = 0x30a660, kFnStopAll = 0x30a6b8, kFnVol = 0x30a3d0, kFnAutoVol = 0x30a310;
constexpr uint32_t kFnPitchMult = 0x30a4a0, kFn3dPos = 0x30a750, kFnFxLevel = 0x30a548, kFnLowpass = 0x30a5f8;
constexpr uint32_t kFnFxInitBus = 0x30a088, kFnFxMaster = 0x30a178, kFnTimerService = 0x307850;
constexpr uint32_t kFnUpdateVoices = 0x30d610, kFnIopInterrupt = 0x30d8e8, kFnPatPresent = 0x3085d0;

// As variáveis do original (snd_state.h tem a lista).
constexpr uint32_t kGOpts = 0x3571e0, kGSaved = 0x357300, kGInited = 0x3573e8, kGMaster = 0x3573e9;
constexpr uint32_t kGCritical = 0x3573eb, kGNumVoices = 0x3573f0, kGTick = 0x3573f4, kGVoices = 0x357454;
constexpr uint32_t kGBanks = 0x357458, kGBusEe = 0x35745c, kGChanAz = 0x35749c, kGTimerCount = 0x3574ec;
constexpr uint32_t kGTimerRate = 0x3574f0, kGHandleGen = 0x3574f8, kGNextKey = 0x357618, kGGains = 0x357620;
constexpr uint32_t kGRnd = 0x357d38, kGIopUp = 0x357d84, kGIopStatus = 0x3585c0, kGTagbuf = 0x35b380;
constexpr uint32_t kGTagVoices = 0x3586fc, kGTagHead = 0x358700, kGTagTail = 0x358704, kGTagLen = 0x35907a;
constexpr uint32_t kGMixOut = 0x359084, kGDmaId = 0x358708, kGDmaCount = 0x35870c, kGDma = 0x358710;
constexpr uint32_t kGPvoice = 0x3588e0, kGSpuCount = 0x359060, kGSpuTable = 0x359068, kGPlatformUp = 0x359075;
constexpr uint32_t kGFramesTick = 0x359078, kGSpuGen = 0x359080, kGDefaultEnv = 0x357610;
constexpr uint32_t kTagbufUncached = 0x2035b380;

// A RAM emulada.
constexpr uint32_t kSndHeap = 0x01100000;  // o heap do SND (0x10000)
constexpr uint32_t kBankBase = 0x01200000;  // os bancos, um atrás do outro
constexpr uint32_t kPlayOpts = 0x01f00000, kPkt = 0x01f00100, kBankOut = 0x01f00200;

// A SPU2 do lado do porte: só a RAM (as amostras que chegam pelo IOP portado).
class RamLibsd : public drv::SndLibsd {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(nf::Spu2::kRamBytes, 0);
    void Init(int32_t) override {}
    void SetParam(uint16_t, uint16_t) override {}
    uint16_t GetParam(uint16_t) override { return 0; }
    void SetSwitch(uint16_t, uint32_t) override {}
    void SetAddr(uint16_t, uint32_t) override {}
    uint32_t GetAddr(uint16_t) override { return 0; }
    void SetCoreAttr(uint16_t, uint16_t) override {}
    void SetEffectAttr(int32_t, const drv::SndEffectAttr&) override {}
    void VoiceTrans(int32_t, int32_t mode, const uint8_t* src, uint32_t dst, uint32_t size) override {
        if (mode != 0 || src == nullptr || dst >= ram.size()) return;
        std::memcpy(ram.data() + dst, src, std::min<size_t>(size, ram.size() - dst));
    }
    void BlockTrans(int32_t, int32_t, int16_t*, uint32_t) override {}
};

// Um banco dos dois lados: os bytes no porte (com folga zerada: o SNDDRV_dmqueue arredonda as transferências para 64
// bytes e lê além do fim) e o endereço na RAM emulada.
struct SndBankFile {
    std::string name;
    std::vector<uint8_t> port;  // tamanho + 0x100
    uint32_t emu = 0;
    uint32_t size = 0;
};

// O lado emulado: os ganchos do kernel e do SIF, a RAM da SPU2 que as transferências enchem e o que foi ao IOP por RPC.
struct SndEmuSide {
    SoundEmu& e;
    std::vector<uint8_t> spu_ram = std::vector<uint8_t>(nf::Spu2::kRamBytes, 0);
    std::vector<uint8_t> iop_ram = std::vector<uint8_t>(2u << 20, 0);
    drv::IopHeapModel iop_heap;  // o kernel do IOP (o mesmo modelo do port, outra instância)
    std::vector<uint8_t> rpc_init;
    int rpc_calls = 0;
    uint32_t rpc_count[3] = {};
    uint32_t rpc_hash = 0x811c9dc5u;  // FNV-1a como o do IOP portado
    uint64_t transfers = 0;

    explicit SndEmuSide(SoundEmu& emu) : e(emu) {}

    void install() {
        MipsMachine& m = e.m;
        auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
        m.hook(0x0010c6c8, "printf", ret(0));
        m.hook(0x002a8038, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n != 0) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        // O kernel do EE: threads e semáforos (CreateThread 7, CreateSema 3, GetThreadId 1).
        m.hook(0x002b6b60, "CreateThread", ret(7));
        m.hook(0x002b6b70, "iTerminateThread", ret(0));
        m.hook(0x002b6b80, "StartThread", ret(0));
        m.hook(0x002b6bb0, "ChangeThreadPriority", ret(0));
        m.hook(0x002b6bf0, "RotateThreadReadyQueue", ret(0));
        m.hook(0x002b6c50, "GetThreadId", ret(1));
        m.hook(0x002b6c60, "iGetThreadId", ret(1));
        m.hook(0x002b6d60, "CreateSema", ret(3));
        m.hook(0x002b6d70, "DeleteSema", ret(0));
        m.hook(0x002b6d80, "SignalSema", ret(0));
        m.hook(0x002b6d90, "PollSema", ret(0));
        m.hook(0x002b6da0, "WaitSema", ret(0));
        m.hook(0x002b6fe0, "FlushCache", ret(0));
        m.hook(0x002b7010, "iFlushCache", ret(0));
        // O SIF: o DMA do EE copia para a memória do IOP; o estado do DMA é "acabou".
        auto set_dma = [this](MipsMachine& mm) {
            const uint32_t t = uint32_t(mm.gpr(R::a0));
            const uint32_t src = mm.read32(t), dst = mm.read32(t + 4), size = mm.read32(t + 8);
            if (dst <= iop_ram.size() && size <= iop_ram.size() - dst && size != 0)
                std::memcpy(iop_ram.data() + dst, mm.mem(src, size), size);
            mm.set_gpr(R::v0, 1);
        };
        m.hook(0x002b7100, "sceSifDmaStat", ret(uint64_t(-1)));
        m.hook(0x002b7110, "isceSifDmaStat", ret(uint64_t(-1)));
        m.hook(0x002b7120, "sceSifSetDma", set_dma);
        m.hook(0x002b7130, "isceSifSetDma", set_dma);
        m.hook(0x002b8e48, "sceSifSetCmdBuffer", ret(0));
        m.hook(0x002b8e78, "sceSifAddCmdHandler", ret(0));
        // O comando ao IOP: a transferência do bloco para a RAM da SPU2, que acaba na hora (como no porte): o IOP
        // responde com o comando 1 tipo 2, que o SNDDRV_iopinterrupt leva ao SNDDRVI_dmcompleterequest.
        auto send_cmd = [this](MipsMachine& mm) {
            const uint32_t pkt = uint32_t(mm.gpr(R::a1));
            const uint32_t id = mm.read32(pkt + 0x14), src = mm.read32(pkt + 0x18), dst = mm.read32(pkt + 0x1c);
            uint16_t size;
            std::memcpy(&size, mm.mem(pkt + 0x20, 2), 2);
            if (dst < spu_ram.size() && src < iop_ram.size() && size <= iop_ram.size() - src)
                std::memcpy(spu_ram.data() + dst, iop_ram.data() + src, std::min<size_t>(size, spu_ram.size() - dst));
            ++transfers;
            mm.write32(kPkt + 0x10, 2);
            mm.write32(kPkt + 0x0c, id);
            mm.call_nested(kFnIopInterrupt, {kPkt, 0});
            mm.set_gpr(R::v0, 1);
        };
        m.hook(0x002b9008, "sceSifSendCmd", send_cmd);
        m.hook(0x002b9048, "isceSifSendCmd", send_cmd);
        m.hook(0x002b9260, "sceSifInitRpc", ret(0));
        m.hook(0x002b98d0, "sceSifBindRpc", [](MipsMachine& mm) {
            mm.write32(uint32_t(mm.gpr(R::a0)) + 0x24, 1);  // o servidor achado
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x002b9aa0, "sceSifCallRpc", [this](MipsMachine& mm) {
            const uint32_t fn = uint32_t(mm.gpr(R::a1)), send = uint32_t(mm.gpr(R::a3)), size = uint32_t(mm.gpr(R::t0));
            ++rpc_calls;
            if (fn == 0 && size != 0) rpc_init.assign(mm.mem(send, size), mm.mem(send, size) + size);
            if (fn < 3) {
                ++rpc_count[fn];
                const uint8_t* b = size != 0 ? mm.mem(send, size) : nullptr;
                for (uint32_t i = 0; i < size; ++i) rpc_hash = (rpc_hash ^ b[i]) * 0x01000193u;
            }
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x002b9c90, "sceSifCheckStatRpc", ret(0));
        m.hook(0x002be258, "sceSifLoadModule", ret(0));
        m.hook(0x0031b9c0, "sceSifInitIopHeap", ret(0));
        m.hook(0x0031ba48, "sceSifAllocIopHeap",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, iop_heap.alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(0x0031bbb0, "sceSifFreeIopHeap", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint64_t(int64_t(iop_heap.free(uint32_t(mm.gpr(R::a0))))));
        });
    }

    uint32_t call(uint32_t fn, const std::vector<uint32_t>& args) { return e.call(fn, args); }
    uint8_t r8(uint32_t a) const { return *e.m.mem(a, 1); }
    uint16_t r16(uint32_t a) const {
        uint16_t v;
        std::memcpy(&v, e.m.mem(a, 2), 2);
        return v;
    }
    uint32_t r32(uint32_t a) const { return e.m.read32(a); }
};

// Os ponteiros dos dois lados: dentro do mesmo banco no mesmo deslocamento, ou no envelope padrão.
struct PtrMap {
    const std::vector<SndBankFile>* banks = nullptr;
    bool same(uint32_t emu, const void* port) const {
        if (port == nullptr) return emu == 0;
        const uint8_t* p = static_cast<const uint8_t*>(port);
        const uint8_t* env = reinterpret_cast<const uint8_t*>(drv::snd_default_envelope());
        if (p >= env && p < env + 0x40) return emu == kGDefaultEnv + uint32_t(p - env);
        for (const SndBankFile& b : *banks) {
            const uint8_t* base = b.port.data();
            if (p >= base && p < base + b.port.size()) return emu == b.emu + uint32_t(p - base);
        }
        return false;
    }
};

// Compara o estado do SND dos dois lados; a primeira diferença (vazio: iguais).
std::string compare_snd(const SndEmuSide& em, const PtrMap& pm) {
    const drv::SndState& s = drv::snd_state();
    auto bytes = [&](const char* what, uint32_t emu, const void* port, uint32_t size) -> std::string {
        const uint8_t* a = em.e.m.mem(emu, size);
        const uint8_t* b = static_cast<const uint8_t*>(port);
        for (uint32_t i = 0; i < size; ++i)
            if (a[i] != b[i])
                return std::string(what) + " +" + hex(i) + ": original " + hex(a[i]) + ", porte " + hex(b[i]);
        return {};
    };
    std::string d;
    // As opções (menos o gancho de erro em +0x114) e a cópia.
    if (!(d = bytes("opções", kGOpts, s.opts, 0x114)).empty()) return d;
    if (!(d = bytes("opções", kGOpts + 0x118, s.opts + 0x118, 8)).empty()) return d;
    if (!(d = bytes("opções salvas", kGSaved, s.saved, 0xe8)).empty()) return d;
    struct Scalar {
        const char* name;
        uint32_t addr;
        int size;
        uint32_t port;
    };
    const Scalar scalars[] = {
        {"inited", kGInited, 1, s.inited},
        {"volume mestre", kGMaster, 1, uint8_t(s.master_vol)},
        {"seção crítica", kGCritical, 1, uint8_t(s.critical)},
        {"vozes", kGNumVoices, 2, uint16_t(s.num_voices)},
        {"tick", kGTick, 4, s.tick},
        {"timer_count", kGTimerCount, 4, s.timer_count},
        {"timer_rate", kGTimerRate, 2, uint16_t(s.timer_rate)},
        {"geração dos handles", kGHandleGen, 4, uint32_t(s.handle_gen)},
        {"próxima tecla", kGNextKey, 1, uint8_t(s.next_key)},
        {"iop_up", kGIopUp, 1, s.iop_up},
        // (os ponteiros para dentro da tagbuf: o original guarda o endereço sem cache, 0x2035b380 + o deslocamento)
        {"tag_voices", kGTagVoices, 4, s.tag_voices + kTagbufUncached},
        {"tag_head", kGTagHead, 4, s.tag_head + kTagbufUncached},
        {"tag_tail", kGTagTail, 4, s.tag_tail + kTagbufUncached},
        {"tag_len", kGTagLen, 2, s.tag_len},
        {"dma_id", kGDmaId, 4, uint32_t(s.dma_id)},
        {"dma_count", kGDmaCount, 1, uint8_t(s.dma_count)},
        {"dma_current", kGDmaCount + 1, 1, s.dma_current},
        {"dma_busy", kGDmaCount + 2, 1, s.dma_busy},
        {"spu_count", kGSpuCount, 2, s.spu_count},
        {"spu_lo", kGSpuCount + 2, 2, s.spu_lo},
        {"spu_hi", kGSpuCount + 4, 2, s.spu_hi},
        {"spu_top", kGSpuCount + 6, 2, s.spu_top},
        {"platform_up", kGPlatformUp, 1, s.platform_up},
        {"bloco do IOP", 0x3588d4, 4, s.iop_block},
        {"frames_tick", kGFramesTick, 2, s.frames_tick},
        {"spu_gen", kGSpuGen, 4, s.spu_gen},
    };
    for (const Scalar& c : scalars) {
        const uint32_t v = c.size == 1 ? em.r8(c.addr) : c.size == 2 ? em.r16(c.addr) : em.r32(c.addr);
        const uint32_t p = c.size == 1 ? (c.port & 0xff) : c.size == 2 ? (c.port & 0xffff) : c.port;
        if (v != p) return std::string(c.name) + ": original " + hex(v) + ", porte " + hex(p);
    }
    for (int i = 0; i < 8; ++i)
        if (s.mix_out[i] != 0 && em.r32(kGMixOut + uint32_t(i) * 4) != s.mix_out[i] + kTagbufUncached)
            return "mix_out " + std::to_string(i);
    if (!(d = bytes("barramento do EE", kGBusEe, s.bus_ee, 0x14)).empty()) return d;
    if (!(d = bytes("barramento do IOP", kGBusEe + 0x14, s.bus_iop, 0x14)).empty()) return d;
    if (!(d = bytes("barramento da SPU2", kGBusEe + 0x28, s.bus_spu, 0x14)).empty()) return d;
    if (!(d = bytes("azimutes dos canais", kGChanAz, s.chan_az, sizeof(s.chan_az))).empty()) return d;
    if (!(d = bytes("ganhos por azimute", kGGains, s.gains, sizeof(s.gains))).empty()) return d;
    if (!(d = bytes("sorteio", kGRnd, s.rnd, sizeof(s.rnd))).empty()) return d;
    if (!(d = bytes("tagbuf", kGTagbuf, s.tagbuf.data(), 0x2c00)).empty()) return d;
    if (!(d = bytes("vozes da plataforma", kGPvoice, s.pvoice, sizeof(s.pvoice))).empty()) return d;
    // As vozes lógicas (os ponteiros para dentro dos bancos normalizados).
    const uint32_t ev = em.r32(kGVoices);
    for (int v = 0; v < s.num_voices; ++v) {
        const drv::SndVoice& pv = s.voices[v];
        const uint32_t a = ev + uint32_t(v) * 0x88;
        const std::string w = "voz " + std::to_string(v);
        if (!(d = bytes(w.c_str(), a, &pv, 0x68)).empty()) return d;
        if (!(d = bytes(w.c_str(), a + 0x7c, reinterpret_cast<const uint8_t*>(&pv) + 0x7c, 0xc)).empty()) return d;
        const void* ptrs[5] = {nf::raw(pv.env_table), nf::raw(pv.vol_table), nf::raw(pv.bend_table),
                               nf::raw(pv.vol_lfo), nf::raw(pv.pitch_lfo)};
        for (int k = 0; k < 5; ++k)
            if (!pm.same(em.r32(a + 0x68 + uint32_t(k) * 4), ptrs[k]))
                return w + " ponteiro +" + hex(0x68 + uint32_t(k) * 4) + ": original " + hex(em.r32(a + 0x68 + uint32_t(k) * 4));
    }
    // Os bancos.
    const uint32_t eb = em.r32(kGBanks);
    for (int b = 0; b < 0x10; ++b) {
        const drv::SndBankSlot& pb = s.banks[b];
        const uint32_t a = eb + uint32_t(b) * 0x10;
        if (!pm.same(em.r32(a), nf::raw(pb.header))) return "banco " + std::to_string(b) + ": o cabeçalho";
        if (!(d = bytes(("banco " + std::to_string(b)).c_str(), a + 4, reinterpret_cast<const uint8_t*>(&pb) + 4, 0xc))
                 .empty())
            return d;
    }
    // A fila das transferências.
    for (int i = 0; i < 0x1c; ++i) {
        const drv::SndDmaRequest& r = s.dma[i];
        const uint32_t a = kGDma + uint32_t(i) * 0x10;
        if (em.r32(a) != r.id || (r.id != 0 && (em.r32(a + 8) != r.dst || em.r16(a + 0xc) != r.size ||
                                                em.r8(a + 0xe) != r.priority || !pm.same(em.r32(a + 4), r.src))))
            return "transferência " + std::to_string(i);
    }
    // O que foi ao IOP por RPC.
    const drv::SndIopInfo info = drv::snddrv_iop_info();
    for (int k = 0; k < 3; ++k)
        if (em.rpc_count[k] != info.rpc_count[k])
            return "RPC " + std::to_string(k) + " ao IOP: original " + std::to_string(em.rpc_count[k]) + " vezes, porte " +
                   std::to_string(info.rpc_count[k]);
    if (em.rpc_hash != info.rpc_hash) return "os bytes dos RPC ao IOP";
    // A tabela da RAM da SPU2.
    const uint32_t et = em.r32(kGSpuTable);
    for (int i = 0; i < s.spu_count; ++i)
        if (em.r32(et + uint32_t(i) * 4) != s.spu_table[i]) return "bloco da SPU2 " + std::to_string(i);
    return {};
}

int check_snd(SoundEmu& e, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n-- o núcleo do SND (sistema, bancos, vozes, plataforma do PS2) --\n");
    SndEmuSide em(e);
    em.install();
    RamLibsd ram;
    drv::snd_power_on();
    drv::snddrv_iop_start(&ram);

    uint64_t bad = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };

    // A iniciação como o ASoundManager::Init: ASystem::SetOpts(1) (estéreo), o ASystem (SNDSYSI_init com o heap de
    // 0x10000, SNDmastervol(0x40)).
    em.call(kFnSetOpts, {1});
    const int32_t init_emu = int32_t(em.call(kFnSysInit, {kSndHeap, 0x10000, 0x72a00}));
    em.call(kFnMasterVol, {0x40});
    drv::ASystem_SetOpts(1);
    const int32_t init_port = drv::ASystem_Construct();
    std::vector<SndBankFile> files;
    PtrMap pm;
    pm.banks = &files;
    if (init_emu != init_port) fail("SNDSYSI_init: original " + std::to_string(init_emu) + ", porte " + std::to_string(init_port));
    if (bad == 0) {
        const std::string d = compare_snd(em, pm);
        if (!d.empty()) fail("depois da iniciação: " + d);
    }
    // O bloco de iniciação do IOP (o RPC 0) e o que o IOP portado leu dele.
    const drv::SndIopInfo info = drv::snddrv_iop_info();
    if (bad == 0 && (em.rpc_init.size() != 0x14 || em.rpc_init[0x10] != uint8_t(info.outputs) ||
                     em.rpc_init[0x11] != uint8_t(info.extra) || em.rpc_init[0x12] != info.iop_voices ||
                     em.rpc_init[0x13] != info.dts || uint32_t(em.rpc_init[0xc] | em.rpc_init[0xd] << 8) != info.ee_rate ||
                     uint32_t(em.rpc_init[0xe] | em.rpc_init[0xf] << 8) != info.iop_rate))
        fail("o bloco de iniciação do IOP");
    std::printf("  iniciação: %d vozes (SPU2 %u, IOP %u, EE %u), saída %u, tagbuf 0x%x bytes, RPC ao IOP %d\n",
                drv::snd_state().num_voices, drv::snd_state().opts[drv::sndopt::kSpuVoices],
                drv::snd_state().opts[drv::sndopt::kIopVoices], drv::snd_state().opts[drv::sndopt::kMixVoices],
                drv::snd_state().opts[drv::sndopt::kOutputMode], drv::snd_state().tag_len, em.rpc_calls);

    // Os bancos de todas as missões (os .bnk dos .VIV): cada um entra e sai dos dois lados (as etiquetas de todos os
    // timbres, o alocador da RAM da SPU2, as transferências e o endereço gravado em cada timbre).
    uint32_t emu_at = kBankBase;
    auto load_file = [&](const std::vector<uint8_t>& data, const std::string& name) {
        SndBankFile f;
        f.name = name;
        f.size = uint32_t(data.size());
        f.port.assign(data.size() + 0x100, 0);
        std::memcpy(f.port.data(), data.data(), data.size());
        f.emu = emu_at;
        std::memset(e.m.mem(emu_at, f.size + 0x100), 0, f.size + 0x100);
        std::memcpy(e.m.mem(emu_at, f.size), data.data(), data.size());
        emu_at = (emu_at + f.size + 0x100 + 0x3f) & ~0x3fu;
        files.push_back(std::move(f));
        return int(files.size()) - 1;
    };
    auto add = [&](int i, int32_t* bank) {
        SndBankFile& f = files[size_t(i)];
        e.m.write32(kBankOut, 0xdeadbeef);
        const int32_t r_emu = int32_t(em.call(kFnBankAdd, {kBankOut, f.emu}));
        int32_t b_port = -1;
        const int32_t r_port = drv::SNDbankadd(&b_port, f.port.data());
        const int32_t b_emu = int32_t(e.m.read32(kBankOut));
        if (r_emu != r_port || b_emu != b_port)
            fail("SNDbankadd(" + f.name + "): original " + std::to_string(r_emu) + " banco " + std::to_string(b_emu) +
                 ", porte " + std::to_string(r_port) + " banco " + std::to_string(b_port));
        *bank = b_port;
        return r_port;
    };
    auto spu_equal = [&](int32_t bank, const SndBankFile& f) -> bool {
        const drv::SndBankSlot& slot = drv::snd_state().banks[bank];
        int32_t spu_size;
        std::memcpy(&spu_size, f.port.data() + 0xc, 4);
        if (slot.spu_mem == 0 || spu_size <= 0) return true;
        const size_t n = (size_t(spu_size) + 0x3f) & ~size_t(0x3f);
        if (std::memcmp(em.spu_ram.data() + slot.spu_mem, ram.ram.data() + slot.spu_mem, n) != 0) return false;
        return true;
    };
    auto iop_equal = [&](int32_t bank, const SndBankFile& f) -> bool {
        const drv::SndBankSlot& slot = drv::snd_state().banks[bank];
        int32_t iop_size = 0;
        if (f.port[4] == 5) std::memcpy(&iop_size, f.port.data() + 0x10, 4);
        if (slot.iop_mem == 0 || iop_size <= 0) return true;
        const uint32_t n = (uint32_t(iop_size) + 0x3f) & ~0x3fu;
        const uint8_t* p = drv::snddrv_iop_ram(slot.iop_mem, n);
        return p != nullptr && slot.iop_mem + n <= em.iop_ram.size() &&
               std::memcmp(em.iop_ram.data() + slot.iop_mem, p, n) == 0;
    };
    int n_files = 0, n_added = 0, n_full = 0, n_timbres = 0;
    std::vector<std::string> archives = {"MIS01", "MIS3", "MIS4", "MIS11", "MIS13A", "MIS13B", "MIS13C", "RACE", "MISC"};
    std::vector<std::pair<std::string, std::vector<uint8_t>>> paris;
    for (const std::string& a : archives) {
        drv::Bigf arc;
        if (!arc.open(disc, "DRIVING/" + a + ".VIV")) continue;
        for (int i = 0; i < int(arc.entries().size()) && bad == 0; ++i) {
            const std::string& n = arc.entries()[size_t(i)].name;
            if (n.size() < 4 || (n.compare(n.size() - 4, 4, ".bnk") != 0 && n.compare(n.size() - 4, 4, ".BNK") != 0)) continue;
            std::vector<uint8_t> data;
            if (!arc.read(i, data) || data.size() < 0x14 || std::memcmp(data.data(), "BNKl", 4) != 0) continue;
            // Um banco por vez, sempre no mesmo lugar da RAM emulada.
            files.clear();
            emu_at = kBankBase;
            const int fi = load_file(data, a + ":" + n);
            ++n_files;
            uint16_t count;
            std::memcpy(&count, data.data() + 6, 2);
            n_timbres += count;
            if (a == "MIS01") paris.emplace_back(a + ":" + n, data);
            int32_t bank = -1;
            const int32_t r = add(fi, &bank);
            if (bad != 0) break;
            if (r == -6) ++n_full;
            if (r >= 0) {
                ++n_added;
                if (!spu_equal(bank, files[size_t(fi)])) fail("a RAM da SPU2 de " + files[size_t(fi)].name);
                if (!iop_equal(bank, files[size_t(fi)])) fail("a memória do IOP de " + files[size_t(fi)].name);
            }
            std::string d = compare_snd(em, pm);
            if (!d.empty()) fail("depois do SNDbankadd(" + files[size_t(fi)].name + "): " + d);
            // Os timbres presentes.
            for (int p = 0; p < count + 2 && bad == 0; ++p) {
                const int32_t x = int32_t(em.call(kFnPatPresent, {uint32_t(bank), uint32_t(p)}));
                if (x != drv::SNDbankpatpresent(bank, p)) fail("SNDbankpatpresent(" + std::to_string(p) + ")");
            }
            if (r >= 0) {
                const int32_t x = int32_t(em.call(kFnBankRemove, {uint32_t(bank)}));
                if (x != drv::SNDbankremove(bank)) fail("SNDbankremove");
                d = compare_snd(em, pm);
                if (!d.empty()) fail("depois do SNDbankremove(" + files[size_t(fi)].name + "): " + d);
            }
        }
    }
    std::printf("  %d bancos de %zu arquivos (%d timbres): %d entraram na RAM da SPU2, %d não couberam sozinhos\n",
                n_files, archives.size(), n_timbres, n_added, n_full);

    // A sequência sorteada: os bancos da Paris (os que couberem) e a API como o jogo a usa, com os ticks do driver no
    // meio (o servidor de 100 Hz, as vozes que o IOP diz que acabaram).
    Rng rng(seed ^ 0x5d0);
    std::vector<int32_t> loaded;
    files.clear();
    emu_at = kBankBase;
    for (const auto& [name, data] : paris) {
        if (bad != 0) break;
        int32_t bank = -1;
        if (add(load_file(data, name), &bank) >= 0) loaded.push_back(bank);
    }
    std::vector<int32_t> handles;
    uint64_t ops = 0, plays = 0, started = 0, freed_by_iop = 0, ticks = 0;
    drv::SndState& ps = drv::snd_state();
    for (int k = 0; k < cases * 4 && bad == 0; ++k) {
        const int op = rng.range(0, 99);
        std::string what;
        int32_t r_emu = 0, r_port = 0;
        auto pick_handle = [&]() -> int32_t {
            if (handles.empty() || rng.chance(0.05)) return int32_t(rng.next());
            return handles[size_t(rng.range(0, int(handles.size()) - 1))];
        };
        if (op < 30 && !loaded.empty()) {
            // SNDBANK_play com opções sorteadas.
            drv::SndPlayOpts o{};
            drv::SNDplaysetdef(&o);
            em.call(kFnPlaySetDef, {kPlayOpts});
            if (std::memcmp(e.m.mem(kPlayOpts, 0x18), &o, 0x18) != 0) {
                fail("SNDplaysetdef");
                break;
            }
            if (rng.chance(0.7)) o.vol = int8_t(rng.range(0, 127));
            if (rng.chance(0.3)) o.bend = int8_t(rng.range(0, 127));
            if (rng.chance(0.3)) o.key = int8_t(rng.range(0, 127));
            if (rng.chance(0.3)) o.velocity = int8_t(rng.range(0, 127));
            if (rng.chance(0.3)) o.prog_vol = int8_t(rng.range(0, 127));
            if (rng.chance(0.3)) o.fx_level = int8_t(rng.range(0, 127));
            if (rng.chance(0.5)) o.azimuth = uint16_t(rng.next());
            if (rng.chance(0.2)) o.elevation = uint16_t(rng.next());
            if (rng.chance(0.5)) o.pitch_mult = uint16_t(rng.range(0x200, 0x4000));
            if (rng.chance(0.1)) o.time_mult = uint16_t(rng.range(0x200, 0x4000));
            if (rng.chance(0.1)) o.lowpass = uint16_t(rng.next());
            std::memcpy(e.m.mem(kPlayOpts, 0x18), &o, 0x18);
            const int32_t bank = rng.chance(0.03) ? rng.range(-2, 17) : loaded[size_t(rng.range(0, int(loaded.size()) - 1))];
            uint16_t count = 0;
            if (bank >= 0 && bank < 0x10 && ps.banks[bank].header != nullptr) std::memcpy(&count, nf::raw(ps.banks[bank].header) + 6, 2);
            const int32_t patch = rng.chance(0.03) ? rng.range(-1, count + 2) : rng.range(0, std::max(0, count - 1));
            what = "SNDBANK_play(" + std::to_string(bank) + ", " + std::to_string(patch) + ")";
            r_emu = int32_t(em.call(kFnBankPlay, {uint32_t(bank), uint32_t(patch), kPlayOpts}));
            r_port = drv::SNDBANK_play(bank, patch, &o);
            ++plays;
            if (r_port >= 0) {
                ++started;
                handles.push_back(r_port);
                if (handles.size() > 200) handles.erase(handles.begin());
            }
        } else if (op < 40) {
            const int32_t h = pick_handle(), v = rng.range(-10, 140);
            what = "SNDvol";
            r_emu = int32_t(em.call(kFnVol, {uint32_t(h), uint32_t(v)}));
            r_port = drv::SNDvol(h, v);
        } else if (op < 45) {
            const int32_t h = pick_handle(), t = rng.range(0, 300), v = rng.range(0, 127);
            what = "SNDautovol";
            r_emu = int32_t(em.call(kFnAutoVol, {uint32_t(h), uint32_t(t), uint32_t(v)}));
            r_port = drv::SNDautovol(h, t, v);
        } else if (op < 55) {
            const int32_t h = pick_handle();
            const uint32_t pm2 = rng.chance(0.1) ? rng.next() : uint32_t(rng.range(0x100, 0x6000));
            what = "SNDpitchmult";
            r_emu = int32_t(em.call(kFnPitchMult, {uint32_t(h), pm2}));
            r_port = drv::SNDpitchmult(h, pm2);
        } else if (op < 65) {
            const int32_t h = pick_handle(), az = int32_t(rng.next() & 0xffff), el = rng.range(-0x4000, 0x4000);
            what = "SND3dpos";
            r_emu = int32_t(em.call(kFn3dPos, {uint32_t(h), uint32_t(az), uint32_t(el)}));
            r_port = drv::SND3dpos(h, az, el);
        } else if (op < 68) {
            // (só o barramento 0: o 1 leria fora da tabela dos barramentos no original; o porte conta uma guarda)
            const int32_t h = pick_handle(), bus = 0, lv = rng.range(0, 127);
            what = "SNDfxlevel";
            r_emu = int32_t(em.call(kFnFxLevel, {uint32_t(h), uint32_t(bus), uint32_t(lv)}));
            r_port = drv::SNDfxlevel(h, bus, lv);
        } else if (op < 70) {
            const int32_t h = pick_handle(), c = rng.range(0, 0xffff);
            what = "SNDCTRL_lowpass";
            r_emu = int32_t(em.call(kFnLowpass, {uint32_t(h), uint32_t(c)}));
            r_port = drv::SNDCTRL_lowpass(h, c);
        } else if (op < 77) {
            const int32_t h = pick_handle();
            what = "SNDstop";
            r_emu = int32_t(em.call(kFnStop, {uint32_t(h)}));
            r_port = drv::SNDstop(h);
        } else if (op < 78) {
            what = "SNDstopall";
            em.call(kFnStopAll, {});
            drv::SNDstopall();
        } else if (op < 80) {
            // O reverb da SPU2 (o barramento 0 no caminho 8, como o AFX::SetMode) e o seu nível.
            // (o índice 0 com os caminhos sorteados; 0 = todos, como o AFX)
            const uint32_t bus = rng.chance(0.6) ? 0u : uint32_t(rng.range(0, 0x3f)) << 3;
            // (o modo 1 lê um descritor por um ponteiro no atraso: o jogo não usa; o porte conta uma guarda)
            int32_t lv = rng.range(0, 127), mode = rng.range(0, 10);
            if (mode == 1) mode = 9;
            if (rng.chance(0.5)) {
                what = "SNDfxinitbus";
                r_emu = int32_t(em.call(kFnFxInitBus, {bus, uint32_t(lv), uint32_t(mode), uint32_t(-1), uint32_t(-1)}));
                r_port = drv::SNDfxinitbus(bus, lv, mode, -1, -1);
            } else {
                what = "SNDfxmasterlevel";
                r_emu = int32_t(em.call(kFnFxMaster, {bus, uint32_t(lv)}));
                r_port = drv::SNDfxmasterlevel(bus, lv);
            }
        } else {
            // Um tick do driver: o IOP diz quais vozes acabaram (o NAX zero), o relógio anda e o EE libera as vozes.
            what = "tick do driver";
            for (int v = 0; v < 48; ++v) {
                const uint32_t st = rng.chance(0.15) ? 0u : 0x5000u + uint32_t(rng.range(0, 0x1000)) * 16;
                ps.iop_status[4 + v] = st;
                e.m.write32(kGIopStatus + 0x10 + uint32_t(v) * 4, st);
            }
            const int before = [&] {
                int n = 0;
                for (int v = 0; v < ps.num_voices; ++v) n += ps.voices[v].in_use != 0;
                return n;
            }();
            em.call(kFnTimerService, {0x5e});
            em.call(kFnUpdateVoices, {});
            drv::SNDSYSI_timerservice(0x5e);
            drv::SNDDRV_updatevoices();
            int after = 0;
            for (int v = 0; v < ps.num_voices; ++v) after += ps.voices[v].in_use != 0;
            if (after < before) freed_by_iop += uint64_t(before - after);
            ++ticks;
        }
        ++ops;
        if (r_emu != r_port) {
            fail(what + ": original " + std::to_string(r_emu) + ", porte " + std::to_string(r_port));
            break;
        }
        const std::string d = compare_snd(em, pm);
        if (!d.empty()) fail("depois de " + what + " (operação " + std::to_string(ops) + "): " + d);
    }
    std::printf("  %zu bancos da Paris carregados; %" PRIu64 " operações (%" PRIu64 " SNDBANK_play, %" PRIu64
                " tocaram; %" PRIu64 " ticks do driver, %" PRIu64 " vozes liberadas pelo IOP), %" PRIu64
                " transferências: %" PRIu64 " diferentes\n",
                loaded.size(), ops, plays, started, ticks, freed_by_iop, em.transfers, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    std::printf("  guardas do porte: SND %" PRIu64 ", IOP %" PRIu64 "\n", drv::snd_guards(), drv::snddrv_guards());
    return bad ? 1 : 0;
}

// ---- a camada do jogo (BondAudioPS2) ----

// As funções do original.
constexpr uint32_t kFnAVoicePlay = 0x2ddfd0, kFnAVoiceSet = 0x2dd698, kFnAPathBuild = 0x2f1f40, kFnRrandom = 0x2d0aa8;
constexpr uint32_t kFnPlayerVehPlay = 0x2f0900, kFnPlayCollision = 0x2e0890, kFnPlayLanding = 0x2dfa00;
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnFastFree = 0x2cfe50, kFnBuiltinNew = 0x2d0910;
constexpr uint32_t kFnBuiltinDelete = 0x2d09a8, kFnOperatorNew = 0x2d09c8, kFnStrcpy = 0x2a964c;
constexpr uint32_t kFnLookupName = 0x2f5880, kFnMixAdd = 0x2f2d50, kFnMixGet = 0x2f2dc8, kFnAtan2f = 0x2c0618;
constexpr uint32_t kFnEnterCritical = 0x307bb0, kFnLeaveCritical = 0x307be0;
// As variáveis do original.
constexpr uint32_t kGActive = 0x421c08, kGSoundList = 0x355dc0, kGCount = 0x355e60, kGViewsInUse = 0x355cd0;
constexpr uint32_t kGFgBanks = 0x3562a0, kGRandom = 0x343810, kGTimerTick = 0x3c80d4, kGTimerHz = 0x3c80d0;
constexpr uint32_t kGCooldown = 0x355cd8, kGGate = 0x355d0c, kVtRaceEngine = 0x3c5d68;
// A RAM emulada da camada do jogo: os objetos copiados do porte e o heap do FastAlloc.
constexpr uint32_t kGameObj = 0x01c00000, kGameHeap = 0x01d00000, kGameHeapEnd = 0x01ef0000;

// Um campo de um objeto do porte no deslocamento do PS2.
enum class Kind { F32, I32, I16, U8 };
struct Field {
    uint32_t off;
    void* p;
    Kind k;
    const char* name;
};

// O lado emulado da camada do jogo: os objetos do porte copiados para a RAM do interpretador no layout do PS2.
class GameSide {
public:
    MipsMachine& m;
    uint32_t obj = kGameObj, heap = kGameHeap;
    std::map<const drv::AMix*, uint32_t> mix_emu;
    std::map<uint32_t, const drv::AMix*> emu_mix;
    std::map<const drv::ABank*, uint32_t> bank_emu;
    std::map<uint32_t, const drv::ABank*> index_bank;  // o AIndex emulado (+0x10 do ABank) -> o banco do porte
    std::map<const drv::AVoice*, uint32_t> voice_emu;
    uint32_t active_head = 0, sound_head = 0;
    uint64_t sndstop_calls = 0, missing_mixes = 0;

    explicit GameSide(MipsMachine& mm) : m(mm) {}

    uint32_t take(uint32_t& at, uint32_t n, uint32_t end) {
        const uint32_t a = (at + 15) & ~15u;
        if (a + n > end) throw nf::MipsError("RAM emulada da camada do jogo cheia", 0, 0);
        at = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    uint32_t static_obj(uint32_t n) { return take(obj, n, kGameHeap); }
    uint32_t alloc(uint32_t n) { return take(heap, n, kGameHeapEnd); }
    void reset_heap() { heap = kGameHeap; }
    void wf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }
    float rf(uint32_t a) const { return ps2::ffloat(m.read32(a)); }
    void w16(uint32_t a, uint16_t v) { std::memcpy(m.mem(a, 2), &v, 2); }
    uint16_t r16(uint32_t a) const {
        uint16_t v;
        std::memcpy(&v, m.mem(a, 2), 2);
        return v;
    }
    void w8(uint32_t a, uint8_t v) { *m.mem(a, 1) = v; }
    uint8_t r8(uint32_t a) const { return *m.mem(a, 1); }
    std::string cstr(uint32_t a) const {
        std::string s;
        for (int i = 0; i < 256; ++i) {
            const char c = char(r8(a + uint32_t(i)));
            if (c == 0) break;
            s += c;
        }
        return s;
    }

    void install() {
        auto none = [](MipsMachine&) {};
        m.hook(0x0010c6c8, "printf", none);
        m.hook(0x002a8148, "printf", none);
        auto alloc_hook = [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); };
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", alloc_hook);
        m.hook(kFnBuiltinNew, "__builtin_new", alloc_hook);
        m.hook(kFnOperatorNew, "operator new", alloc_hook);
        m.hook(kFnFastFree, "UMemory::FastFree", none);
        m.hook(kFnBuiltinDelete, "__builtin_delete", none);
        m.hook(kFnStrcpy, "strcpy", [this](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0));
            const std::string s = cstr(uint32_t(mm.gpr(R::a1)));
            std::memcpy(mm.mem(d, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnLookupName, "AIndex::Lookup(char *)", [this](MipsMachine& mm) {
            const auto it = index_bank.find(uint32_t(mm.gpr(R::a0)));
            const std::string name = cstr(uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, uint64_t(int64_t(it == index_bank.end() ? -1 : it->second->index.Lookup(name.c_str()))));
        });
        auto mix_hook = [this](MipsMachine& mm) { mm.set_gpr(R::v0, mix_by_name(cstr(uint32_t(mm.gpr(R::a0))))); };
        m.hook(kFnMixAdd, "AMix::Add", mix_hook);
        m.hook(kFnMixGet, "AMix::Get", mix_hook);
        m.hook(kFnAtan2f, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(kFnEnterCritical, "SNDSYS_entercritical", none);
        m.hook(kFnLeaveCritical, "SNDSYS_leavecritical", none);
        m.hook(kFnStop, "SNDstop", [this](MipsMachine& mm) {
            ++sndstop_calls;
            mm.set_gpr(R::v0, 0);
        });
        m.write32(kGTimerHz, 100);  // o TIMER_init: a frequência do contador
        m.write32(kGViewsInUse, 4);  // DAT_00355cd0: só a vista 2 (um jogador)
        active_head = static_obj(12);
        sound_head = static_obj(12);
    }

    // As mixagens.
    void write_mix(const drv::AMix* x, uint32_t a) {
        m.write32(a + 0x00, mix_of(x->master));
        wf(a + 0x04, x->volume);
        wf(a + 0x08, x->saved);
        wf(a + 0x0c, x->preset);
        wf(a + 0x10, x->initial);
        wf(a + 0x14, x->target);
        wf(a + 0x18, x->step);
        m.write32(a + 0x1c, uint32_t(x->transition));
        m.write32(a + 0x20, uint32_t(x->priority));
        m.write32(a + 0x24, uint32_t(x->count));
        wf(a + 0x28, x->sum);
        wf(a + 0x2c, x->gain);
        std::memcpy(m.mem(a + 0x30, 32), x->name, 32);
    }
    uint32_t mix_of(const drv::AMix* x) {
        if (x == nullptr) return 0;
        const auto it = mix_emu.find(x);
        if (it != mix_emu.end()) return it->second;
        const uint32_t a = static_obj(0x50);
        mix_emu[x] = a;
        emu_mix[a] = x;
        write_mix(x, a);
        return a;
    }
    uint32_t mix_by_name(const std::string& name) {
        auto& mixes = *drv::audio_internals().mixes;
        const auto it = mixes.find(name);
        if (it != mixes.end()) return mix_of(it->second.get());
        // Uma mixagem que o porte não criou: o original a cria (vale 0 e conta como diferença).
        ++missing_mixes;
        const uint32_t a = static_obj(0x50);
        wf(a + 0x0c, -1.0f);
        wf(a + 0x10, -1.0f);
        wf(a + 0x2c, 1.0f);
        return a;
    }
    std::string compare_mix(const drv::AMix* x, uint32_t a) const {
        const auto it = mix_emu.find(x->master);
        if (m.read32(a) != (x->master == nullptr ? 0u : it == mix_emu.end() ? 1u : it->second)) return "o mestre";
        const std::pair<uint32_t, uint32_t> f[] = {
            {0x04, fbits(x->volume)}, {0x08, fbits(x->saved)},  {0x0c, fbits(x->preset)},     {0x10, fbits(x->initial)},
            {0x14, fbits(x->target)}, {0x18, fbits(x->step)},   {0x1c, uint32_t(x->transition)}, {0x20, uint32_t(x->priority)},
            {0x24, uint32_t(x->count)}, {0x28, fbits(x->sum)}, {0x2c, fbits(x->gain)}};
        for (const auto& [off, v] : f)
            if (m.read32(a + off) != v) return "o campo +0x" + hex(off) + " (original " + hex(m.read32(a + off)) + ", porte " + hex(v) + ")";
        return {};
    }
    void write_all_mixes() {
        for (auto& [name, x] : *drv::audio_internals().mixes) write_mix(x.get(), mix_of(x.get()));
    }
    std::string compare_all_mixes() const {
        for (auto& [name, x] : *drv::audio_internals().mixes) {
            const auto it = mix_emu.find(x.get());
            if (it == mix_emu.end()) continue;
            const std::string d = compare_mix(x.get(), it->second);
            if (!d.empty()) return "a mixagem \"" + name + "\": " + d;
        }
        return {};
    }

    // Os bancos (ABank: +0x04 a categoria, +0x08 o banco do SND, +0x0c o resultado, +0x10 o AIndex).
    uint32_t bank_of(const drv::ABank* b) {
        if (b == nullptr) return 0;
        const auto it = bank_emu.find(b);
        if (it != bank_emu.end()) return it->second;
        const uint32_t a = static_obj(0x18);
        m.write32(a + 0x04, uint32_t(b->category));
        m.write32(a + 0x08, uint32_t(b->snd_bank));
        m.write32(a + 0x0c, uint32_t(b->status));
        bank_emu[b] = a;
        index_bank[a + 0x10] = b;
        return a;
    }
    void write_banks() {
        for (int i = 0; i < 10; ++i) m.write32(kGFgBanks + uint32_t(i) * 4, bank_of(drv::ABank_Category(i)));
    }

    // As vozes (AVoice: +0x00 a mixagem, as três vistas de 0x20 bytes a partir de +0x04).
    void write_voice(const drv::AVoice* v, uint32_t a) {
        m.write32(a, mix_of(v->mix));
        for (int i = 0; i < 3; ++i) {
            const drv::AVoiceView& w = v->views[i];
            const uint32_t b = a + 4 + 0x20 * uint32_t(i);
            m.write32(b + 0x00, uint32_t(w.patch));
            m.write32(b + 0x04, uint32_t(w.bank));
            m.write32(b + 0x08, uint32_t(w.loop));
            m.write32(b + 0x0c, uint32_t(w.handle));
            w16(b + 0x10, w.pitch);
            w16(b + 0x12, w.azimuth);
            w16(b + 0x14, uint16_t(w.delay));
            w8(b + 0x16, uint8_t(w.vol));
            w8(b + 0x17, uint8_t(w.listed));
            w8(b + 0x18, w.fx);
            m.write32(b + 0x1c, uint32_t(w.over));
        }
    }
    std::string compare_voice(const drv::AVoice* v, uint32_t a) const {
        if (m.read32(a) != (v->mix == nullptr ? 0u : mix_emu.count(v->mix) ? mix_emu.at(v->mix) : 1u)) return "a mixagem";
        for (int i = 0; i < 3; ++i) {
            const drv::AVoiceView& w = v->views[i];
            const uint32_t b = a + 4 + 0x20 * uint32_t(i);
            const std::string vi = "vista " + std::to_string(i) + ": ";
            if (int32_t(m.read32(b)) != w.patch || int32_t(m.read32(b + 4)) != w.bank) return vi + "o timbre";
            if (int32_t(m.read32(b + 8)) != w.loop) return vi + "o laço";
            if (int32_t(m.read32(b + 0xc)) != w.handle) return vi + "o handle";
            if (r16(b + 0x10) != w.pitch)
                return vi + "o pitch (original " + hex(r16(b + 0x10)) + ", porte " + hex(w.pitch) + ")";
            if (r16(b + 0x12) != w.azimuth)
                return vi + "o azimute (original " + hex(r16(b + 0x12)) + ", porte " + hex(w.azimuth) + ")";
            if (int16_t(r16(b + 0x14)) != w.delay) return vi + "o atraso";
            if (int8_t(r8(b + 0x16)) != w.vol)
                return vi + "o volume (original " + std::to_string(int8_t(r8(b + 0x16))) + ", porte " + std::to_string(w.vol) + ")";
            if (int8_t(r8(b + 0x17)) != w.listed) return vi + "o volume da lista";
            if (r8(b + 0x18) != w.fx) return vi + "o efeito";
            if (int32_t(m.read32(b + 0x1c)) != w.over) return vi + "o fim";
        }
        return {};
    }
    // As vozes vivas: as que já tinham endereço o mantêm; as novas ganham um.
    void set_voices(const std::vector<const drv::AVoice*>& live) {
        std::map<const drv::AVoice*, uint32_t> next;
        for (const drv::AVoice* v : live) {
            if (v == nullptr || next.count(v)) continue;
            const auto it = voice_emu.find(v);
            next[v] = it != voice_emu.end() ? it->second : static_obj(100);
        }
        voice_emu = std::move(next);
        for (const auto& [v, a] : voice_emu) write_voice(v, a);
    }
    uint32_t voice_addr(const drv::AVoice* v) const {
        if (v == nullptr) return 0;
        const auto it = voice_emu.find(v);
        return it == voice_emu.end() ? 1u : it->second;
    }
    uint32_t view_addr(const drv::AVoiceView* view) const {
        for (const auto& [v, a] : voice_emu)
            for (int i = 0; i < 3; ++i)
                if (&v->views[i] == view) return a + 4 + 0x20 * uint32_t(i);
        return 1;
    }

    // A lista ativa das vistas (std::list do GCC: {próximo, anterior, dado}) e a lista dos sons (vazia: só os novos).
    void write_lists() {
        const uint32_t h = active_head;
        m.write32(kGActive, h);
        m.write32(h, h);
        m.write32(h + 4, h);
        for (const drv::AVoiceView* view : *drv::audio_internals().active) {
            const uint32_t n = alloc(12), last = m.read32(h + 4);
            m.write32(n, h);
            m.write32(n + 4, last);
            m.write32(n + 8, view_addr(view));
            m.write32(last, n);
            m.write32(h + 4, n);
        }
        m.write32(kGSoundList, sound_head);
        m.write32(sound_head, sound_head);
        m.write32(sound_head + 4, sound_head);
    }
    std::string compare_active() const {
        const auto& list = *drv::audio_internals().active;
        auto it = list.begin();
        uint32_t n = m.read32(active_head);
        for (int k = 0; n != active_head; ++k, n = m.read32(n), ++it) {
            if (k > 10000) return "a lista ativa: sem fim";
            if (it == list.end()) return "a lista ativa: o original tem mais (" + std::to_string(k) + " no porte)";
            if (m.read32(n + 8) != view_addr(*it)) return "a lista ativa: a posição " + std::to_string(k);
        }
        if (it != list.end()) return "a lista ativa: o porte tem mais";
        return {};
    }
    std::vector<uint32_t> new_sounds() const {
        std::vector<uint32_t> r;
        for (uint32_t n = m.read32(sound_head); n != sound_head && r.size() < 1000; n = m.read32(n)) r.push_back(m.read32(n + 8));
        return r;
    }
    // Um AOneShotSound criado dos dois lados (o porte pelo construtor, o original montado no lugar).
    std::string compare_oneshot(const drv::AOneShotSound* s, uint32_t a) {
        const auto it = emu_mix.find(m.read32(a + 0x84));
        if (it == emu_mix.end() || it->second != s->mix) {
            auto name_of = [](const drv::AMix* x) -> std::string {
                for (const auto& [n, p] : *drv::audio_internals().mixes)
                    if (p.get() == x) return n;
                return "?";
            };
            return "a mixagem (original " + (it == emu_mix.end() ? std::string("fora do mapa") : name_of(it->second)) +
                   ", porte " + name_of(s->mix) + ")";
        }
        if (m.read32(a + 0x88) != s->views) return "as vistas";
        const std::pair<uint32_t, float> f[] = {
            {0x00, s->pos[0]},        {0x04, s->pos[1]},        {0x08, s->pos[2]},     {0x10, s->vel[0]},
            {0x14, s->vel[1]},        {0x18, s->vel[2]},        {0x20, s->dir[0]},     {0x24, s->dir[1]},
            {0x28, s->dir[2]},        {0x5c, s->min_distance},  {0x60, s->max_distance}, {0x64, s->max_distance2},
            {0x68, s->falloff},       {0x6c, s->volume},        {0x70, s->pitch},      {0x74, s->last_volume[0]},
            {0x78, s->last_volume[1]}, {0x7c, s->last_volume[2]}, {0x9c, s->fade},     {0x114, s->fx},
            {0x118, s->lifetime}};
        for (const auto& [off, v] : f)
            if (m.read32(a + off) != fbits(v))
                return "o campo +0x" + hex(off) + " (original " + hex(m.read32(a + off)) + ", porte " + hex(fbits(v)) + ")";
        if (int32_t(m.read32(a + 0x80)) != s->disabled) return "desligado";
        const uint32_t va = a + 0xb0;
        // A voz: a mixagem do som e as vistas (o original a constrói com o AVoice::AVoice, o porte também).
        if (m.read32(va) != m.read32(a + 0x84)) return "a mixagem da voz";
        for (int i = 0; i < 3; ++i) {
            const drv::AVoiceView& w = s->voice.views[i];
            const uint32_t b = va + 4 + 0x20 * uint32_t(i);
            if (int32_t(m.read32(b)) != w.patch || int32_t(m.read32(b + 4)) != w.bank) return "o timbre da voz";
            if (int32_t(m.read32(b + 8)) != w.loop || int32_t(m.read32(b + 0xc)) != w.handle) return "o laço da voz";
            if (r16(b + 0x10) != w.pitch || int16_t(r16(b + 0x14)) != w.delay || int8_t(r8(b + 0x16)) != w.vol) return "a vista da voz";
            if (int32_t(m.read32(b + 0x1c)) != w.over) return "o fim da voz";
        }
        return {};
    }

    // Os campos.
    void write_fields(uint32_t base, const std::vector<Field>& fs) {
        for (const Field& f : fs) {
            switch (f.k) {
                case Kind::F32:
                case Kind::I32: m.write32(base + f.off, *static_cast<const uint32_t*>(f.p)); break;
                case Kind::I16: w16(base + f.off, *static_cast<const uint16_t*>(f.p)); break;
                case Kind::U8: w8(base + f.off, *static_cast<const uint8_t*>(f.p)); break;
            }
        }
    }
    std::string compare_fields(uint32_t base, const std::vector<Field>& fs, const char* what) const {
        for (const Field& f : fs) {
            uint32_t e = 0, p = 0;
            switch (f.k) {
                case Kind::F32:
                case Kind::I32:
                    e = m.read32(base + f.off);
                    p = *static_cast<const uint32_t*>(f.p);
                    break;
                case Kind::I16:
                    e = r16(base + f.off);
                    p = *static_cast<const uint16_t*>(f.p);
                    break;
                case Kind::U8:
                    e = r8(base + f.off);
                    p = *static_cast<const uint8_t*>(f.p);
                    break;
            }
            if (e != p)
                return std::string(what) + "." + f.name + " (+0x" + hex(f.off) + "): original " + hex(e) + ", porte " + hex(p);
        }
        return {};
    }
};

std::vector<Field> vehicle_fields(drv::APlayerVehicle& v) {
    std::vector<Field> f;
    auto F = [&](uint32_t o, float& x, const char* n) { f.push_back({o, &x, Kind::F32, n}); };
    auto I = [&](uint32_t o, int32_t& x, const char* n) { f.push_back({o, &x, Kind::I32, n}); };
    for (uint32_t i = 0; i < 3; ++i) {
        F(0x00 + 4 * i, v.pos[i], "pos");
        F(0x10 + 4 * i, v.vel[i], "vel");
        F(0x20 + 4 * i, v.dir[i], "dir");
        F(0x30 + 4 * i, v.axis1[i], "axis1");
        F(0x40 + 4 * i, v.axis2[i], "axis2");
        F(0x50 + 4 * i, v.aim[i], "aim");
        F(0x74 + 4 * i, v.last_volume[i], "last_volume");
        F(0x144 + 4 * i, v.scrape[i], "scrape");
        I(0x17c + 4 * i, v.scrape_timer[i], "scrape_timer");
    }
    F(0x5c, v.min_distance, "min_distance");
    F(0x60, v.max_distance, "max_distance");
    F(0x64, v.max_distance2, "max_distance2");
    F(0x68, v.falloff, "falloff");
    F(0x6c, v.volume, "volume");
    F(0x70, v.pitch, "pitch");
    I(0x80, v.disabled, "disabled");
    f.push_back({0x88, &v.views, Kind::I32, "views"});
    F(0x9c, v.fade, "fade");
    I(0xa0, v.fade_t, "fade_t");
    I(0xa4, v.fade_len, "fade_len");
    I(0xa8, v.fading, "fading");
    I(0xb0, v.gun_active, "gun_active");
    I(0xb4, v.gun_firing, "gun_firing");
    I(0xb8, v.unknown_b8, "unknown_b8");
    I(0xbc, v.unknown_bc, "unknown_bc");
    I(0xc0, v.surface_type, "surface_type");
    I(0xc4, v.unknown_c4, "unknown_c4");
    I(0xd0, v.vehicle_type, "vehicle_type");
    for (uint32_t i = 0; i < 4; ++i) {
        I(0xd4 + 4 * i, v.surface[i], "surface");
        I(0xe4 + 4 * i, v.prev_surface[i], "prev_surface");
        F(0xf4 + 4 * i, v.compression[i], "compression");
        I(0x22c + 4 * i, v.road_surface[i], "road_surface");
    }
    F(0x104, v.front_slip, "front_slip");
    F(0x108, v.back_slip, "back_slip");
    F(0x10c, v.front_skid, "front_skid");
    F(0x110, v.back_skid, "back_skid");
    F(0x114, v.skid_multiple, "skid_multiple");
    F(0x118, v.gas, "gas");
    F(0x11c, v.rpm, "rpm");
    F(0x120, v.rpm_wobble, "rpm_wobble");
    F(0x124, v.wobble_dir, "wobble_dir");
    F(0x128, v.offroad_level, "offroad_level");
    F(0x12c, v.gravel_level, "gravel_level");
    F(0x130, v.cobble_level, "cobble_level");
    F(0x134, v.cobble_dist, "cobble_dist");
    F(0x138, v.rail_dist, "rail_dist");
    F(0x13c, v.speed, "speed");
    F(0x140, v.car_speed, "car_speed");
    F(0x150, v.landing_force, "landing_force");
    I(0x154, v.horn, "horn");
    I(0x158, v.cobble_toggle, "cobble_toggle");
    I(0x15c, v.rail_toggle, "rail_toggle");
    I(0x160, v.boost, "boost");
    I(0x164, v.alive, "alive");
    I(0x168, v.squeal, "squeal");
    I(0x16c, v.cobble_patch[0], "cobble_patch");
    I(0x170, v.cobble_patch[1], "cobble_patch");
    I(0x174, v.rail_patch[0], "rail_patch");
    I(0x178, v.rail_patch[1], "rail_patch");
    f.push_back({0x188, &v.unknown_188, Kind::I16, "unknown_188"});
    I(0x18c, v.gas_button, "gas_button");
    I(0x190, v.brake_button, "brake_button");
    I(0x194, v.xy_trigger, "xy_trigger");
    I(0x198, v.xy_release, "xy_release");
    F(0x19c, v.steering, "steering");
    F(0x1a0, v.steering_vertical, "steering_vertical");
    return f;
}

std::vector<Field> engine_fields(drv::ARaceEngine& e) {
    return {{0x00, &e.pitch, Kind::F32, "pitch"}, {0x04, &e.load_amp, Kind::F32, "load_amp"},
            {0x08, &e.idle_amp, Kind::F32, "idle_amp"}, {0x0c, &e.start, Kind::F32, "start"},
            {0x10, &e.rpm, Kind::F32, "rpm"},           {0x14, &e.inv_max, Kind::F32, "inv_max"},
            {0x18, &e.load, Kind::F32, "load"},         {0x4c, &e.low, Kind::F32, "low"},
            {0x50, &e.high, Kind::F32, "high"}};
}

std::vector<Field> wind_fields(drv::AVehicleWind& w) {
    return {{0x0c, &w.level, Kind::F32, "level"},   {0x10, &w.level_step, Kind::F32, "level_step"},
            {0x14, &w.pitch, Kind::F32, "pitch"},   {0x18, &w.pitch_step, Kind::F32, "pitch_step"},
            {0x1c, &w.volume, Kind::F32, "volume"}, {0x20, &w.azimuth, Kind::F32, "azimuth"}};
}

// Um som de teste (o ABaseSound é abstrato) para o APath::Build.
class TestSound : public drv::ABaseSound {
public:
    TestSound() : ABaseSound("Player Car", 4) {}
    void Play(drv::APath&) override {}
};

float rnd_f(Rng& r, float lo, float hi) { return lo + (hi - lo) * float(r.next() & 0xffffff) / float(0x1000000); }

int check_game(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n-- a camada do jogo (AVoice, APath, o carro do jogador) --\n");
    SoundEmu e;
    e.setup(elf);
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    e.m.attach_vu0(&vu);
    GameSide g(e.m);
    g.install();

    uint64_t bad = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };

    // O som da Paris no porte, como o nf_driving o liga (os volumes, os bancos e os índices da missão 1).
    drv::Bigf arc;
    if (!arc.open(disc, "DRIVING/MIS01.VIV")) {
        std::printf("  sem DRIVING/MIS01.VIV\n");
        return 1;
    }
    auto loader = [&arc](const std::string& path) {
        std::string key = path;
        for (char& ch : key)
            if (ch == '/') ch = '\\';
        std::vector<uint8_t> out;
        for (int i = 0; i < int(arc.entries().size()); ++i)
            if (nf::str_icmp(arc.entries()[size_t(i)].name.c_str(), key.c_str()) == 0) {
                arc.read(i, out);
                break;
            }
        return out;
    };
    drv::AudioInit ai;
    ai.track = "paris_mis01";
    if (!drv::audio_init(ai, loader)) {
        std::printf("  o audio_init falhou\n");
        return 1;
    }
    g.write_banks();
    drv::AudioInternals in = drv::audio_internals();
    Rng r(seed ^ 0xa0d10);

    // 0. rrandom (0x002d0aa8): estados sorteados, dois sorteios cada.
    uint64_t rnd_draws = 0, rnd_negative = 0;
    for (int k = 0; k < cases && bad == 0; ++k) {
        for (int i = 0; i < 6; ++i) {
            in.random[i] = r.chance(0.05) ? 0xffffffffu : r.next() ^ (r.next() << 16);
            e.m.write32(kGRandom + 4 * uint32_t(i), in.random[i]);
        }
        for (int j = 0; j < 2 && bad == 0; ++j) {
            const float p = drv::rrandom();
            e.call(kFnRrandom, {});
            ++rnd_draws;
            rnd_negative += ps2::ee::lt(p, 0.0f) ? 1 : 0;
            if (e.m.fpr_bits(0) != fbits(p)) fail("rrandom: original " + hex(e.m.fpr_bits(0)) + ", porte " + hex(fbits(p)));
            for (int i = 0; i < 6; ++i)
                if (e.m.read32(kGRandom + 4 * uint32_t(i)) != in.random[i]) fail("rrandom: o estado");
        }
    }
    std::printf("  rrandom: %" PRIu64 " sorteios (%" PRIu64 " negativos, como no original): %" PRIu64 " diferentes\n",
                rnd_draws, rnd_negative, bad);

    // 1. AVoice::Play e AVoice::Set sorteados sobre seis vozes de duas mixagens.
    uint64_t voice_ops = 0, voice_stops = 0, voice_pushes = 0;
    const uint64_t voice_bad0 = bad;
    {
        drv::AMix* mixes[2] = {drv::AMix_Get("Player Car"), drv::AMix_Get("Skids")};
        std::vector<drv::AVoice*> voices;
        for (int i = 0; i < 6; ++i) voices.push_back(new drv::AVoice(mixes[i % 2], r.range(-1, 8), r.range(-1, 40)));
        for (int k = 0; k < cases * 2 && bad == 0; ++k) {
            drv::AVoice* v = voices[size_t(r.range(0, 5))];
            for (drv::AMix* x : mixes)
                if (r.chance(0.1)) x->gain = rnd_f(r, 0.0f, 1.5f);
            if (r.chance(0.1)) v->views[r.range(0, 2)].delay = -1;  // como o PlayVoices quando toca
            if (r.chance(0.05)) v->views[r.range(0, 2)].loop = r.range(0, 1);
            g.reset_heap();
            g.write_all_mixes();
            std::vector<const drv::AVoice*> live(voices.begin(), voices.end());
            g.set_voices(live);
            g.write_lists();
            const size_t listed_before = in.active->size();
            std::string what;
            if (r.chance(0.08)) {
                const int32_t bank = r.chance(0.2) ? -1 : r.range(0, 8), patch = r.chance(0.2) ? -1 : r.range(0, 40);
                what = "AVoice::Set";
                v->Set(bank, patch);
                e.call(kFnAVoiceSet, {g.voice_addr(v), uint32_t(bank), uint32_t(patch)});
            } else {
                const float vol = r.chance(0.15) ? rnd_f(r, -0.01f, 0.01f) : rnd_f(r, -0.3f, 1.4f);
                const float pitch = rnd_f(r, -6.0f, 6.0f), az = rnd_f(r, -1.0f, 1.0f);
                const float delay = r.chance(0.3) ? rnd_f(r, -1.0f, 400.0f) : 0.0f, fx = rnd_f(r, -0.5f, 1.5f);
                const int32_t view = r.range(0, 2);
                what = "AVoice::Play";
                drv::AVoice_Play(vol, pitch, az, delay, fx, v, view);
                e.m.set_fpr(12, vol);
                e.m.set_fpr(13, pitch);
                e.m.set_fpr(14, az);
                e.m.set_fpr(15, delay);
                e.m.set_fpr(16, fx);
                e.call(kFnAVoicePlay, {g.voice_addr(v), uint32_t(view)});
                if (in.active->size() < listed_before) ++voice_stops;
                if (in.active->size() > listed_before) ++voice_pushes;
            }
            ++voice_ops;
            std::string d;
            for (const drv::AVoice* x : voices)
                if (d.empty()) d = g.compare_voice(x, g.voice_addr(x));
            if (d.empty()) d = g.compare_active();
            if (d.empty()) d = g.compare_all_mixes();
            if (!d.empty()) fail(what + " (operação " + std::to_string(k) + "): " + d);
        }
        for (drv::AVoice* v : voices) delete v;
    }
    std::printf("  AVoice::Play e AVoice::Set: %" PRIu64 " chamadas sorteadas (%" PRIu64 " entraram na lista ativa, %" PRIu64
                " saíram): %" PRIu64 " diferentes\n",
                voice_ops, voice_pushes, voice_stops, bad - voice_bad0);

    // 2. APath::Build: sons e ouvintes sorteados (com a faixa de altura do ambiente e as distâncias limite).
    uint64_t path_cases = 0, path_bad0 = bad;
    {
        TestSound* s = new TestSound();
        drv::AListener l;
        const uint32_t sa = g.static_obj(0xb0), la = g.static_obj(0x80), pa = g.static_obj(0x20);
        for (int k = 0; k < cases && bad == 0; ++k) {
            for (int i = 0; i < 3; ++i) {
                l.pos[i] = rnd_f(r, -300.0f, 300.0f);
                s->pos[i] = r.chance(0.05) ? l.pos[i] : ps2::ee::add(l.pos[i], rnd_f(r, -60.0f, 60.0f) * (r.chance(0.3) ? 8.0f : 1.0f));
                s->vel[i] = rnd_f(r, -50.0f, 50.0f);
                l.vel[i] = rnd_f(r, -50.0f, 50.0f);
                s->dir[i] = rnd_f(r, -1.0f, 1.0f);
            }
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c) l.rot[row][c] = rnd_f(r, -1.0f, 1.0f);
            for (int c = 0; c < 4; ++c) l.trans[c] = rnd_f(r, -300.0f, 300.0f);
            l.view = r.chance(0.8) ? 2 : r.range(0, 3);
            l.band_top = r.chance(0.5) ? -100000.0f : rnd_f(r, -50.0f, 50.0f);
            l.band_height = rnd_f(r, 0.5f, 30.0f);
            s->min_distance = r.chance(0.5) ? 3.0f : rnd_f(r, 0.0f, 40.0f);
            s->max_distance = r.chance(0.1) ? 0.0f : r.chance(0.5) ? 350.0f : rnd_f(r, 1.0f, 500.0f);
            drv::APath p;
            p.volume = rnd_f(r, -1, 1), p.pitch = rnd_f(r, -1, 1), p.azimuth = rnd_f(r, -1, 1);
            p.facing = rnd_f(r, -1, 1), p.delay = rnd_f(r, -1, 1), p.distance = rnd_f(r, -1, 1);
            p.listener = &l;
            // O lado emulado.
            for (int i = 0; i < 3; ++i) {
                g.wf(sa + 0x00 + 4 * uint32_t(i), s->pos[i]);
                g.wf(sa + 0x10 + 4 * uint32_t(i), s->vel[i]);
                g.wf(sa + 0x20 + 4 * uint32_t(i), s->dir[i]);
                g.wf(la + 0x40 + 4 * uint32_t(i), l.pos[i]);
                g.wf(la + 0x50 + 4 * uint32_t(i), l.vel[i]);
            }
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c) g.wf(la + uint32_t(row * 16 + c * 4), l.rot[row][c]);
            for (int c = 0; c < 4; ++c) g.wf(la + 0x30 + uint32_t(c) * 4, l.trans[c]);
            e.m.write32(la + 0x5c, uint32_t(l.view));
            g.wf(la + 0x70, l.band_top);
            g.wf(la + 0x74, l.band_height);
            g.wf(sa + 0x5c, s->min_distance);
            g.wf(sa + 0x60, s->max_distance);
            const float pf[6] = {p.volume, p.pitch, p.azimuth, p.facing, p.delay, p.distance};
            for (int i = 0; i < 6; ++i) g.wf(pa + 4 * uint32_t(i), pf[i]);
            e.m.write32(pa + 0x18, la);
            drv::APath_Build(p, *s);
            e.call(kFnAPathBuild, {pa, sa});
            ++path_cases;
            const float out[6] = {p.volume, p.pitch, p.azimuth, p.facing, p.delay, p.distance};
            static const char* kNames[6] = {"volume", "pitch", "azimuth", "facing", "delay", "distance"};
            for (int i = 0; i < 6; ++i)
                if (e.m.read32(pa + 4 * uint32_t(i)) != fbits(out[i])) {
                    fail("APath::Build (caso " + std::to_string(k) + "): " + kNames[i] + " original " +
                         hex(e.m.read32(pa + 4 * uint32_t(i))) + ", porte " + hex(fbits(out[i])));
                    break;
                }
        }
        s->destroy();
    }
    std::printf("  APath::Build: %" PRIu64 " caminhos sorteados: %" PRIu64 " diferentes\n", path_cases, bad - path_bad0);

    // 3. O carro do jogador quadro a quadro.
    drv::AudioCarAttributes ca;
    ca.engine = "vanquish";
    ca.volume = 1.0f;
    ca.skid_multiple = 1.5f;
    drv::audio_create_player_car(ca);
    in = drv::audio_internals();
    drv::APlayerVehicle* pv = in.player;
    auto* engine = dynamic_cast<drv::ARaceEngine*>(pv != nullptr ? pv->engine : nullptr);
    if (pv == nullptr || engine == nullptr || pv->wind == nullptr) {
        std::printf("  o carro do jogador não foi criado\n");
        drv::audio_shutdown();
        return 1;
    }
    const uint32_t P = g.static_obj(0x240), E = g.static_obj(0x60), W = g.static_obj(0x30);
    const uint32_t L = g.static_obj(0x80), PA = g.static_obj(0x20), PT = g.static_obj(0x20);
    drv::AListener listener;
    drv::APath path;
    path.listener = &listener;
    const std::vector<Field> vf = vehicle_fields(*pv), ef = engine_fields(*engine), wf = wind_fields(*pv->wind);
    uint64_t frames = 0, collisions = 0, landings = 0, oneshots = 0, road_swaps = 0, cooldown_forced = 0;
    const uint64_t car_bad0 = bad;
    int32_t timer = 1000;
    float rpm_base = 800.0f;
    static const int32_t kSurfaces[] = {0, 1, 1, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 15};
    int32_t surfaces[4] = {1, 1, 1, 1};
    for (int k = 0; k < cases * 2 && bad == 0; ++k) {
        // As entradas sorteadas (o fim do PBondCar::Simulate, o caminho, o relógio, as mixagens).
        if (k % 400 == 0) {
            static const int32_t kTypes[] = {0, 0, 4, 5, 7};
            pv->vehicle_type = kTypes[r.range(0, 4)];
            pv->squeal = r.range(0, 1);
        }
        const int op = r.range(0, 99);
        drv::AudioCarFeed fd;
        for (int i = 0; i < 3; ++i) {
            fd.pos[i] = ps2::ee::add(pv->pos[i], rnd_f(r, -1.0f, 1.0f));
            const float sp = r.chance(0.3) ? rnd_f(r, 0.0f, 20.0f) : rnd_f(r, 0.0f, 60.0f);
            fd.vel[i] = rnd_f(r, -1.0f, 1.0f) * sp;
            for (int j = 0; j < 3; ++j) fd.orient[i][j] = rnd_f(r, -1.0f, 1.0f);
            fd.aim[i] = rnd_f(r, -1.0f, 1.0f);
        }
        rpm_base = std::clamp(rpm_base + rnd_f(r, -400.0f, 400.0f), 0.0f, 7000.0f);
        fd.rpm = r.chance(0.05) ? rnd_f(r, 0.0f, 600.0f) : rpm_base;
        fd.gas = r.chance(0.3) ? 0.0f : r.chance(0.2) ? 1.0f : rnd_f(r, 0.0f, 1.0f);
        fd.front_slip = r.chance(0.5) ? 0.0f : rnd_f(r, 0.0f, 1.3f);
        fd.back_slip = r.chance(0.5) ? 0.0f : rnd_f(r, 0.0f, 1.3f);
        for (int i = 0; i < 4; ++i) {
            if (r.chance(0.12)) surfaces[i] = kSurfaces[r.range(0, int(sizeof(kSurfaces) / 4) - 1)];
            fd.surface[i] = surfaces[i];
            fd.compression[i] = rnd_f(r, 0.0f, 1.0f);
        }
        fd.alive = r.chance(0.95) ? 1 : 0;
        fd.car_speed = rnd_f(r, 0.0f, 80.0f);
        fd.control_gas = rnd_f(r, -1.0f, 1.0f);
        fd.control_brake = rnd_f(r, -1.0f, 1.0f);
        fd.landing = r.chance(0.5) ? 0.0f : rnd_f(r, 0.0f, 1.2f);
        fd.gas_input = fd.gas;
        fd.brake_input = r.chance(0.8) ? 0.0f : 1.0f;
        if (op < 70) drv::audio_feed_player_car(fd);
        drv::sound_count() = r.chance(0.8) ? 1 : r.range(0, 4);
        timer += r.range(0, 4);
        drv::audio_set_timer(timer);
        path.volume = r.chance(0.1) ? 0.0f : rnd_f(r, 0.0f, 1.0f);
        path.pitch = rnd_f(r, 0.8f, 1.25f);
        path.azimuth = rnd_f(r, -0.5f, 0.5f);
        path.distance = r.chance(0.6) ? rnd_f(r, 0.0f, 3.0f) : rnd_f(r, 0.0f, 400.0f);
        listener.view = r.chance(0.9) ? 2 : r.range(0, 1);
        if (r.chance(0.05)) {
            auto& mixes = *in.mixes;
            auto it = mixes.begin();
            std::advance(it, r.range(0, int(mixes.size()) - 1));
            drv::AMix_SetTransition(rnd_f(r, 0.0f, 1.0f), it->second.get(), r.range(1, 40));
        }
        if (r.chance(0.03))
            for (int i = 0; i < 3; ++i) pv->scrape_timer[i] = r.range(0, 12);

        // O estado do porte vai para o lado emulado.
        g.reset_heap();
        g.write_all_mixes();
        g.write_banks();
        // As vozes com o nome do papel (para o relatório).
        std::vector<std::pair<std::string, const drv::AVoice*>> roles;
        for (int i = 0; i < 3; ++i) roles.emplace_back("road" + std::to_string(i), pv->road[i]);
        for (int i = 0; i < 2; ++i) {
            roles.emplace_back("skid" + std::to_string(i), pv->skid[i]);
            roles.emplace_back("grass_skids" + std::to_string(i), pv->grass_skids[i]);
            roles.emplace_back("offroad_skids" + std::to_string(i), pv->offroad_skids[i]);
            roles.emplace_back("ice_skids" + std::to_string(i), pv->ice_skids[i]);
        }
        for (int i = 0; i < 3; ++i) roles.emplace_back("scrapes" + std::to_string(i), pv->scrapes[i]);
        for (int i = 0; i < 10; ++i) roles.emplace_back("engine" + std::to_string(i), engine->voices[i]);
        roles.emplace_back("wind", pv->wind->voice);
        roles.emplace_back("road_noise", pv->road_noise);
        std::vector<const drv::AVoice*> live;
        for (const auto& rv : roles) live.push_back(rv.second);
        g.set_voices(live);
        g.write_lists();
        g.write_fields(P, vf);
        e.m.write32(P + 0x84, g.mix_of(pv->mix));
        std::memcpy(e.m.mem(P + 0x8c, 16), pv->name, 16);
        e.m.write32(P + 0x1b0, g.bank_of(pv->collisions));
        for (uint32_t i = 0; i < 3; ++i) e.m.write32(P + 0x1b4 + 4 * i, g.voice_addr(pv->road[i]));
        for (uint32_t i = 0; i < 2; ++i) {
            e.m.write32(P + 0x1c0 + 4 * i, g.voice_addr(pv->skid[i]));
            e.m.write32(P + 0x1c8 + 4 * i, g.voice_addr(pv->grass_skids[i]));
            e.m.write32(P + 0x1d0 + 4 * i, g.voice_addr(pv->offroad_skids[i]));
            e.m.write32(P + 0x1d8 + 4 * i, g.voice_addr(pv->ice_skids[i]));
        }
        for (uint32_t i = 0; i < 3; ++i) e.m.write32(P + 0x1e0 + 4 * i, g.voice_addr(pv->scrapes[i]));
        e.m.write32(P + 0x1f4, W);
        e.m.write32(P + 0x1fc, g.mix_of(pv->mix_asphalt));
        e.m.write32(P + 0x200, g.mix_of(pv->mix_offroad));
        e.m.write32(P + 0x204, g.mix_of(pv->mix_skids));
        e.m.write32(P + 0x208, g.mix_of(pv->mix_scrapes));
        std::memcpy(e.m.mem(P + 0x210, 16), pv->engine_name, 16);
        e.m.write32(P + 0x220, E);
        e.m.write32(P + 0x224, g.voice_addr(pv->road_noise));
        g.write_fields(E, ef);
        e.m.write32(E + 0x1c, P);
        e.m.write32(E + 0x20, kVtRaceEngine);
        for (uint32_t i = 0; i < 10; ++i) e.m.write32(E + 0x24 + 4 * i, g.voice_addr(engine->voices[i]));
        g.write_fields(W, wf);
        e.m.write32(W + 0x00, P);
        e.m.write32(W + 0x04, g.mix_of(pv->wind->mix));
        e.m.write32(W + 0x08, g.voice_addr(pv->wind->voice));
        std::memcpy(e.m.mem(L, sizeof listener.rot), listener.rot, sizeof listener.rot);
        e.m.write32(L + 0x5c, uint32_t(listener.view));
        const float pf[6] = {path.volume, path.pitch, path.azimuth, path.facing, path.delay, path.distance};
        for (int i = 0; i < 6; ++i) g.wf(PA + 4 * uint32_t(i), pf[i]);
        e.m.write32(PA + 0x18, L);
        e.m.write32(kGCount, uint32_t(drv::sound_count()));
        e.m.write32(kGTimerTick, uint32_t(timer));
        for (int i = 0; i < 6; ++i) e.m.write32(kGRandom + 4 * uint32_t(i), in.random[i]);
        e.m.write32(kGGate, *in.gate);
        const size_t sounds_before = drv::sound_list().size();
        const drv::AVoice* road_before = pv->road_noise;
        bool road_swap = false;  // o APlayerVehicle::Play troca o ruído do asfalto quando uma roda mudou de superfície
        for (int i = 0; i < 4; ++i) road_swap |= pv->surface[i] != pv->road_surface[i];

        // A operação dos dois lados.
        std::string what;
        if (op >= 92) {
            // Um choque (o ECollision). O original não para no fim dos 12 descansos (lê o que vem depois na memória;
            // o porte para: a divergência anotada no PlayCollision): o 12º descansa no máximo até agora.
            if (timer < in.cooldown[11]) {
                in.cooldown[11] = timer;
                ++cooldown_forced;
            }
            for (int i = 0; i < 12; ++i) e.m.write32(kGCooldown + 4 * uint32_t(i), uint32_t(in.cooldown[i]));
            static const int32_t kHitSurf[] = {0, 1, 1, 2, 3, 4, 5, 5, 8, 9, 10, 12, 13, 13, 15};
            const int32_t surf = kHitSurf[r.range(0, int(sizeof(kHitSurf) / 4) - 1)];
            const float force = r.chance(0.2) ? rnd_f(r, 0.0f, 0.06f) : rnd_f(r, 0.0f, 1.2f);
            float pt[4], hv[4];
            for (int i = 0; i < 4; ++i) pt[i] = rnd_f(r, -300.0f, 300.0f), hv[i] = rnd_f(r, -30.0f, 30.0f);
            for (int i = 0; i < 4; ++i) g.wf(PT + 4 * uint32_t(i), pt[i]), g.wf(PT + 0x10 + 4 * uint32_t(i), hv[i]);
            if (op >= 97) {
                what = "AVehicle::PlayLanding";
                pv->PlayLanding(force, pt, hv);
                e.m.set_fpr(12, force);
                e.call(kFnPlayLanding, {P, PT, PT + 0x10});
                ++landings;
            } else {
                what = "AVehicle::PlayCollision";
                pv->PlayCollision(force, pt, hv, surf);
                e.m.set_fpr(12, force);
                e.call(kFnPlayCollision, {P, PT, PT + 0x10, uint32_t(surf)});
                ++collisions;
            }
        } else {
            what = "APlayerVehicle::Play";
            for (int i = 0; i < 12; ++i) e.m.write32(kGCooldown + 4 * uint32_t(i), uint32_t(in.cooldown[i]));
            pv->Play(path);
            e.call(kFnPlayerVehPlay, {P, PA});
            ++frames;
        }

        // A comparação.
        std::string d = g.compare_fields(P, vf, "AVehicle");
        if (d.empty()) d = g.compare_fields(E, ef, "ARaceEngine");
        if (d.empty()) d = g.compare_fields(W, wf, "AVehicleWind");
        if (what != "APlayerVehicle::Play") road_swap = false;
        if (d.empty() && road_swap) {
            // O ruído do asfalto trocado: a voz nova dos dois lados.
            ++road_swaps;
            const uint32_t na = e.m.read32(P + 0x224);
            if ((pv->road_noise == nullptr) != (na == 0)) d = "o ruído do asfalto (nulo de um lado só)";
            else if (pv->road_noise != nullptr) {
                d = g.compare_voice(pv->road_noise, na);
                if (!d.empty()) d = "o ruído do asfalto novo: " + d;
            }
            // (a voz nova ganha um endereço fixo na próxima cópia; a velha saiu dos dois lados)
            g.voice_emu.erase(road_before);
            g.voice_emu.erase(pv->road_noise);
        } else if (d.empty() && e.m.read32(P + 0x224) != g.voice_addr(pv->road_noise)) {
            d = "o ruído do asfalto trocado só no original";
        }
        for (const auto& [role, v] : roles)
            if (d.empty() && v != nullptr && role != "road_noise") {
                d = g.compare_voice(v, g.voice_addr(v));
                if (!d.empty()) d = "a voz " + role + ": " + d;
            }
        if (d.empty() && pv->road_noise != nullptr && !road_swap) {
            d = g.compare_voice(pv->road_noise, g.voice_addr(pv->road_noise));
            if (!d.empty()) d = "a voz road_noise: " + d;
        }
        if (d.empty()) d = g.compare_active();
        if (d.empty()) d = g.compare_all_mixes();
        if (d.empty())
            for (int i = 0; i < 6; ++i)
                if (e.m.read32(kGRandom + 4 * uint32_t(i)) != in.random[i]) d = "o estado do random";
        if (d.empty())
            for (int i = 0; i < 12; ++i)
                if (int32_t(e.m.read32(kGCooldown + 4 * uint32_t(i))) != in.cooldown[i]) d = "o descanso " + std::to_string(i);
        if (d.empty() && e.m.read32(kGGate) != *in.gate) d = "o portão dos objetos batidos";
        // Os avulsos criados.
        const std::vector<uint32_t> made = g.new_sounds();
        // (os novos ficam no começo das duas listas, o mais novo primeiro)
        std::vector<drv::AOneShotSound*> port_made;
        {
            auto it = drv::sound_list().begin();
            for (size_t n = drv::sound_list().size() - sounds_before; n > 0; --n, ++it)
                port_made.push_back(dynamic_cast<drv::AOneShotSound*>(*it));
        }
        if (d.empty() && made.size() != port_made.size())
            d = "os avulsos criados: original " + std::to_string(made.size()) + ", porte " + std::to_string(port_made.size());
        for (size_t i = 0; i < port_made.size() && d.empty(); ++i) {
            if (port_made[i] == nullptr) d = "um som novo que não é avulso";
            else d = g.compare_oneshot(port_made[i], made[i]);
            if (!d.empty()) {
                d = "o avulso " + std::to_string(i) + ": " + d + " | original:";
                for (uint32_t a : made) d += " " + hex(e.m.read32(a + 0x84)) + "/" + hex(e.m.read32(a + 0x6c));
                d += " porte:";
                for (drv::AOneShotSound* s : port_made)
                    d += " " + hex(g.mix_emu.count(s->mix) ? g.mix_emu.at(s->mix) : 0u) + "/" + hex(fbits(s->volume));
            }
        }
        oneshots += port_made.size();
        for (drv::AOneShotSound* s : port_made)
            if (s != nullptr) s->destroy();
        if (d.empty() && g.sndstop_calls != 0) d = "o original chamou o SNDstop";
        if (d.empty() && g.missing_mixes != 0) d = "o original pediu uma mixagem que o porte não tem";
        if (!d.empty()) fail(what + " (operação " + std::to_string(k) + "): " + d);
    }
    std::printf("  o carro do jogador: %" PRIu64 " quadros do APlayerVehicle::Play, %" PRIu64 " PlayCollision, %" PRIu64
                " PlayLanding (%" PRIu64 " avulsos criados, %" PRIu64 " trocas do ruído do asfalto, %" PRIu64
                " descansos encurtados): %" PRIu64 " diferentes\n",
                frames, collisions, landings, oneshots, road_swaps, cooldown_forced, bad - car_bad0);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    drv::audio_shutdown();
    return bad ? 1 : 0;
}

}  // namespace

int run_sound_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n== parte 13: o som (o SND da EA: o EA-XA, o núcleo do SND; a camada do jogo) ==\n");
    int failures = 0;
    try {
        SoundEmu e;
        e.setup(elf);
        failures += check_eaxa(e, disc, cases, seed);
        failures += check_snd(e, disc, cases, seed);
        failures += check_game(elf, disc, cases, seed);
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s (pc %08x, instrução %08x)\n", err.what(), err.pc, err.word);
        ++failures;
    }
    return failures;
}

}  // namespace drvcheck
