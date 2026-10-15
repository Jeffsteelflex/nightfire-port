// nf_driving_check, parte 23 (tests/driving_check_streams.h): os fluxos das missões de carro. O código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte (driving/sound/):
//  - o cabeçalho PT dos fluxos: o SNDI_patchtohdr sobre o SCHl de cada .asf do .MUS e do .SPE de todas as missões;
//  - o SND dos fluxos inteiro, do SNDSTRM ao mixador do EE: o SND iniciado como o ASoundManager::Init, um fluxo como o
//    do AStreamPriv (4 pedidos, 15 pacotes, 0x20000 bytes de buffer) e uma sequência sorteada da API (os pedidos com os
//    .asf da Paris, segurados ou não, o volume, a rampa, o azimute, o pitch (o reamostrador nos dois caminhos), o
//    passa-baixa, o purge, o modifyhold, o status) com os ticks do driver no meio (o servidor de 100 Hz com o
//    SNDSTRMI_service, o SNDDRV_updatevoices, o MIX_audio de 512 amostras, o SNDPKTPLAYI_flushcallbackdata): depois de
//    cada operação o fluxo, os pedidos, o tocador de pacotes, a fila dos callbacks, as vozes e os filtros do mixador
//    (o desempacotador com o seu CEAXABLKDecf, o reamostrador, o passa-baixa), as somas em float e as amostras de 16
//    bits que vão ao IOP, e as vozes do SND;
//  - a camada do jogo sobre o SND inteiro: o ASoundManager::Init dos fluxos (AStream::SetPath, os AStream "Music" e
//    "Speech" de Paris, o AFader "SpeechVsAmbience") e uma sequência sorteada: quadros (o relógio, o AStream::Play dos
//    dois fluxos com o volume, o pitch e o azimute do caminho, o AFader::Update, o SNDSYS_service, os ticks do driver),
//    os destrutores dos eventos 20 (música, falas, NIS, um arquivo que não existe), 273, 241 e 266, o
//    AFader::SetSecondary e o Call911, o AStream::Event/Next/Last/Stop/FadeOut/GetLatency e os globais (a missão
//    acabada, o fade do HUD, a pausa): depois de cada operação os dois AStream (o ABaseSound, o AStreamPriv, a fila de
//    eventos lida na deque do GCC 2.95 do original), o AFader, os globais, as mixagens e tudo o que a parte do SND
//    confere, nos dois fluxos. As mixagens do original são cópias das do porte (o AMix::Add/Get/Reset ficam no porte:
//    o .ini não é lido no lado emulado), mas o SetTransition e o GetPresetVolume rodam os originais sobre elas.
//  O STREAM_* (a leitura do disco em anel do librealfilez) é o do PC (driving/sound/stream.h) nos dois lados: o lado
//  emulado usa outra instância do mesmo modelo, com os arquivos copiados para a RAM emulada. Os .asf grandes (a música,
//  os ambientes) entram só com os primeiros ~400 KB (cortados num pedaço inteiro).
//
// Tudo aqui lê dados do jogo: a saída é um relatório, nada é gravado.

#include "driving_check_streams.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/game/triggers.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_stream.h"
#include "driving/sound/eaxa.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_mix.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snd_stream.h"
#include "driving/sound/snddrv_iop.h"
#include "driving/sound/stream.h"
#include "platform/crt_compat.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções do original.
constexpr uint32_t kFnSetOpts = 0x2e6c88, kFnSysInit = 0x3074c8, kFnMasterVol = 0x309f30, kFnPlaySetDef = 0x30a8e8;
constexpr uint32_t kFnTimerService = 0x307850, kFnUpdateVoices = 0x30d610, kFnIopInterrupt = 0x30d8e8;
constexpr uint32_t kFnMixAudio = 0x3137b8, kFnFlush = 0x3120d8, kFnPatchToHdr = 0x30c0f8, kFnSysService = 0x311140;
constexpr uint32_t kFnStrmCreate = 0x309700, kFnStrmQueue = 0x3097c0, kFnStrmPurge = 0x3097e0;
constexpr uint32_t kFnStrmStatus = 0x309a50, kFnStrmReqStatus = 0x309b48, kFnStrmModifyHold = 0x309c88;
constexpr uint32_t kFnStrmVol = 0x309d50, kFnStrmAutoVol = 0x309d98, kFnStrm3dPos = 0x309e00;
constexpr uint32_t kFnStrmPitch = 0x309e60, kFnStrmLowpass = 0x309ea8, kFnStrmGreedy = 0x309ef0;

// As variáveis do original.
constexpr uint32_t kGOpts = 0x3571e0, kGVoices = 0x357454, kGStreams = 0x357160, kGPlayers = 0x35e37c;
constexpr uint32_t kGCbCount = 0x35e078, kGCb = 0x35e07c, kGMixVoices = 0x35e658, kGMixOutbuf = 0x35e54c;
constexpr uint32_t kGMixPending = 0x35e564, kGMixPendingWet = 0x35e57c, kGMixOut = 0x359084;
constexpr uint32_t kTagbufUncached = 0x2035b380, kGTagbuf = 0x35b380, kGNumVoices = 0x3573f0;

// A RAM emulada.
constexpr uint32_t kSndHeap = 0x01100000;  // o heap do SND (0x10000)
constexpr uint32_t kStrmMem = 0x01200000;  // a memória do fluxo (alinhada a 0x400, como o UMemory::Alloc)
constexpr uint32_t kHdrIn = 0x01280000;    // o SCHl do teste do SNDI_patchtohdr
constexpr uint32_t kHdrOut = 0x012f0000;   // o formato, os atributos e a informação
constexpr uint32_t kOpts = 0x01f00000, kStatus = 0x01f00100, kName = 0x01f00200, kPkt = 0x01f00300;
constexpr uint32_t kFileLo = 0x00500000, kFileHi = 0x00ff0000;  // os .asf dos pedidos
constexpr int32_t kBuffer = 0x20000;
constexpr size_t kCut = 400 * 1024;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) / double(0x1000000) < p; }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%x", v);
    return b;
}
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Os .asf: inteiros até kCut, senão os primeiros pedaços inteiros até passar de kCut.
std::vector<uint8_t> cut_asf(const std::vector<uint8_t>& d) {
    if (d.size() <= kCut) return d;
    size_t p = 0;
    while (p + 8 <= d.size() && p < kCut) {
        const uint32_t size = rd32(&d[p + 4]) & 0xffffff;
        if (size < 8 || p + size > d.size()) break;
        p += size;
    }
    return std::vector<uint8_t>(d.begin(), d.begin() + long(p));
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    // O STREAM do lado emulado: os modelos por identificador e os blocos dos arquivos na RAM emulada.
    std::map<uint32_t, std::unique_ptr<drv::StreamModel>> models;
    std::vector<std::pair<uint32_t, uint32_t>> blocks;  // (endereço, tamanho)
    drv::StreamModel::Loader loader;
    uint64_t file_bytes = 0;

    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
    }
    uint32_t call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 400'000'000);
        return uint32_t(m.gpr(R::v0));
    }
    std::string cstr(uint32_t a) const { return reinterpret_cast<const char*>(m.mem(a, 1)); }

    uint32_t place(std::vector<uint8_t>& data) {
        // Os blocos que nenhum modelo usa mais saem; o novo vai no primeiro buraco que couber.
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                    [this](const std::pair<uint32_t, uint32_t>& b) {
                                        for (const auto& kv : models)
                                            if (kv.second->holds(b.first)) return false;
                                        return true;
                                    }),
                     blocks.end());
        std::sort(blocks.begin(), blocks.end());
        const uint32_t n = (uint32_t(data.size()) + 0x3f) & ~0x3fu;
        uint32_t at = kFileLo;
        for (const auto& b : blocks) {
            if (at + n <= b.first) break;
            at = std::max(at, (b.first + b.second + 0x3f) & ~0x3fu);
        }
        if (at + n > kFileHi) throw std::runtime_error("a RAM emulada dos arquivos acabou");
        std::memcpy(m.mem(at, uint32_t(data.size())), data.data(), data.size());
        blocks.emplace_back(at, n);
        file_bytes += data.size();
        return at;
    }
    drv::StreamModel* model(uint32_t h) {
        auto it = models.find(h);
        return it == models.end() ? nullptr : it->second.get();
    }

    void install() {
        auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
        m.hook(0x0010c6c8, "printf", ret(0));
        m.hook(0x002a8148, "printf", ret(0));
        m.hook(0x002a8038, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n != 0) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x002a7df0, "memcmp", [](MipsMachine& mm) {
            const uint32_t n = uint32_t(mm.gpr(R::a2));
            int r = 0;
            if (n != 0) r = std::memcmp(mm.mem(uint32_t(mm.gpr(R::a0)), n), mm.mem(uint32_t(mm.gpr(R::a1)), n), n);
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        // O kernel do EE e o SIF (como na parte 13): o RPC ao IOP não faz nada aqui (o fluxo não toca na SPU2).
        for (uint32_t a : {0x002b6b60u, 0x002b6c50u, 0x002b6c60u}) m.hook(a, "thread", ret(7));
        for (uint32_t a : {0x002b6b70u, 0x002b6b80u, 0x002b6bb0u, 0x002b6bf0u, 0x002b6d70u, 0x002b6d80u, 0x002b6d90u,
                           0x002b6da0u, 0x002b6fe0u, 0x002b7010u, 0x002b8e48u, 0x002b8e78u, 0x002b9260u, 0x002b9c90u,
                           0x002be258u, 0x0031b9c0u, 0x0031bbb0u})
            m.hook(a, "kernel", ret(0));
        m.hook(0x002b6d60, "CreateSema", ret(3));
        m.hook(0x002b7100, "sceSifDmaStat", ret(uint64_t(-1)));
        m.hook(0x002b7110, "isceSifDmaStat", ret(uint64_t(-1)));
        m.hook(0x002b7120, "sceSifSetDma", ret(1));
        m.hook(0x002b7130, "isceSifSetDma", ret(1));
        m.hook(0x002b9008, "sceSifSendCmd", ret(1));
        m.hook(0x002b9048, "isceSifSendCmd", ret(1));
        m.hook(0x002b98d0, "sceSifBindRpc", [](MipsMachine& mm) {
            mm.write32(uint32_t(mm.gpr(R::a0)) + 0x24, 1);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x002b9aa0, "sceSifCallRpc", ret(0));
        m.hook(0x0031ba48, "sceSifAllocIopHeap", ret(0x80000));
        // O STREAM do PC (o mesmo modelo do porte, outra instância).
        m.hook(0x00305938, "STREAM_create", [this](MipsMachine& mm) {
            auto md = std::make_unique<drv::StreamModel>(loader, [this](std::vector<uint8_t>& d) { return place(d); });
            const uint32_t h = md->create(int32_t(mm.gpr(R::a0)), int32_t(mm.gpr(R::a1)), int32_t(mm.gpr(R::a2)),
                                          uint32_t(mm.gpr(R::a3)), int32_t(mm.gpr(R::t0)));
            if (h != 0) models[h] = std::move(md);
            mm.set_gpr(R::v0, h);
        });
        m.hook(0x00305c88, "STREAM_destroy", [this](MipsMachine& mm) {
            const uint32_t h = uint32_t(mm.gpr(R::a0));
            if (drv::StreamModel* md = model(h)) {
                md->destroy();
                models.erase(h);
            }
        });
        m.hook(0x003067e0, "STREAM_buffersize", [this](MipsMachine& mm) {
            drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, uint64_t(int64_t(md ? md->buffersize() : 0)));
        });
        m.hook(0x00305d88, "STREAM_setgreedylevel", [this](MipsMachine& mm) {
            if (drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)))) md->setgreedylevel(int32_t(mm.gpr(R::a1)));
        });
        m.hook(0x00305ee8, "STREAM_queuefile", [this](MipsMachine& mm) {
            drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)));
            const std::string name = cstr(uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, md ? md->queuefile(name.c_str(), int32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3))) : 0);
        });
        m.hook(0x00306370, "STREAM_kill", [this](MipsMachine& mm) {
            if (drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)))) md->kill();
        });
        m.hook(0x00306540, "STREAM_get", [this](MipsMachine& mm) {
            drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, md ? md->get() : 0);
        });
        m.hook(0x00306648, "STREAM_release", [this](MipsMachine& mm) {
            if (drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)))) md->release(uint32_t(mm.gpr(R::a1)));
        });
        m.hook(0x00306728, "STREAM_gettable", [this](MipsMachine& mm) {
            drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, uint64_t(int64_t(md ? md->gettable() : 0)));
        });
        m.hook(0x00306758, "STREAM_state", [this](MipsMachine& mm) {
            drv::StreamModel* md = model(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, uint64_t(int64_t(md ? md->state() : 0)));
        });
    }
};

// Os dois lados: as bases para normalizar os ponteiros.
struct Sides {
    Emu& e;
    // Os fluxos: a memória que cada lado deu ao SNDSTRM_create (o do porte em P32).
    int nstreams = 1;
    uint32_t emu_base[2] = {kStrmMem, 0};
    uint8_t* port_base[2] = {nullptr, nullptr};
    uint8_t*& pmem = port_base[0];
    uint32_t emu_handle = 0, port_handle = 0;
    uint8_t* heap() const { return drv::snd_state().heap_mem; }

    uint32_t r32(uint32_t a) const { return e.m.read32(a); }
    // Um ponteiro da memória de um fluxo em (fluxo << 24) | deslocamento; 0xffffffff nulo.
    uint32_t emu_mem(uint32_t v) const {
        if (v == 0) return 0xffffffffu;
        // (o bloco de base mais alta que contém v: os dois fluxos do jogo ficam lado a lado)
        int best = -1;
        for (int i = 0; i < nstreams; ++i)
            if (v >= emu_base[i] && v < emu_base[i] + 0x40000 && (best < 0 || emu_base[i] > emu_base[best])) best = i;
        if (best < 0) return 0xfffffffeu;
        return (uint32_t(best) << 24) | (v - emu_base[best]);
    }
    uint32_t port_mem(uint32_t v) const {
        if (v == 0) return 0xffffffffu;
        int best = -1;
        uint32_t bb = 0;
        for (int i = 0; i < nstreams; ++i) {
            const uint32_t b = nf::p32_u(port_base[i]);
            if (v >= b && v < b + 0x40000 && (best < 0 || b > bb)) {
                best = i;
                bb = b;
            }
        }
        if (best < 0) return 0xfffffffeu;
        return (uint32_t(best) << 24) | (v - bb);
    }
    uint32_t emu_heap(uint32_t v) const { return v == 0 ? 0xffffffffu : v - kSndHeap; }
    uint32_t port_heap(const void* p) const {
        return p == nullptr ? 0xffffffffu : uint32_t(static_cast<const uint8_t*>(p) - heap());
    }
    // Um ponteiro para dentro dos .asf: (fluxo, pedido, deslocamento).
    std::string emu_data(uint32_t v) const {
        if (v == 0) return "nulo";
        uint32_t id, off;
        for (const auto& kv : e.models)
            if (kv.second->locate(v, &id, &off))
                return std::to_string(emu_mem(kv.first) >> 24) + "/" + std::to_string(id) + ":" + hex(off);
        return "?";  // (um ponteiro velho: o pedido já saiu dos dois lados)
    }
    // Um ponteiro velho do lado do porte (o pedido já saiu) não se compara: o lado emulado reaproveita a RAM.
    bool data_differs(uint32_t emu, uint32_t port) const {
        const std::string p = port_data(port);
        return p != "?" && emu_data(emu) != p;
    }
    std::string port_data(uint32_t v) const {
        if (v == 0) return "nulo";
        uint32_t id, off;
        for (int i = 0; i < nstreams; ++i) {
            const drv::SndStreamChannel* c = drv::snd_stream_channel(i);
            if (c == nullptr) continue;
            if (drv::StreamModel* md = drv::stream_model(c->stream))
                if (md->locate(v, &id, &off)) return std::to_string(i) + "/" + std::to_string(id) + ":" + hex(off);
        }
        return "?";  // (um ponteiro velho: o pedido já saiu dos dois lados)
    }
};

// A primeira diferença (vazio: iguais).
std::string compare_all(const Sides& s, int frames_tick) {
    const Emu& e = s.e;
    auto bytes = [&](const std::string& what, uint32_t emu, const void* port, uint32_t size) -> std::string {
        const uint8_t* a = e.m.mem(emu, size);
        const uint8_t* b = static_cast<const uint8_t*>(port);
        for (uint32_t i = 0; i < size; ++i)
            if (a[i] != b[i]) return what + " +" + hex(i) + ": original " + hex(a[i]) + ", porte " + hex(b[i]);
        return {};
    };
    auto word = [&](const std::string& what, uint32_t emu, uint32_t port) -> std::string {
        if (emu != port) return what + ": original " + hex(emu) + ", porte " + hex(port);
        return {};
    };
    std::string d;
    // O relógio e a seção crítica do SND.
    const drv::SndState& st0 = drv::snd_state();
    if (!(d = word("seção crítica", e.m.mem(0x3573eb, 1)[0], uint8_t(st0.critical))).empty()) return d;
    if (!(d = word("tick do SND", e.m.read32(0x3573f4), st0.tick)).empty()) return d;
    if (!(d = word("timer_count", e.m.read32(0x3574ec), st0.timer_count)).empty()) return d;
    // As tabelas dos fluxos e dos tocadores.
    for (int i = 0; i < 8; ++i) {
        const drv::SndStreamChannel* pc = drv::snd_stream_channel(i);
        if (!(d = word("fluxo " + std::to_string(i), s.emu_mem(s.r32(kGStreams + uint32_t(i) * 4)),
                       pc ? s.port_mem(nf::p32_u(pc)) : 0xffffffffu))
                 .empty())
            return d;
        const drv::SndPacketPlayer* pp = drv::snd_packet_player(i);
        if (!(d = word("tocador " + std::to_string(i), s.emu_mem(s.r32(kGPlayers + uint32_t(i) * 4)),
                       pp ? s.port_mem(nf::p32_u(pp)) : 0xffffffffu))
                 .empty())
            return d;
    }
    // Cada fluxo.
    for (int si = 0; si < s.nstreams; ++si) {
        const drv::SndStreamChannel* c = drv::snd_stream_channel(si);
        if (c == nullptr) continue;
        const uint32_t ec = s.r32(kGStreams + uint32_t(si) * 4);
        const uint8_t* pc = reinterpret_cast<const uint8_t*>(c);
        if (!(d = word("fluxo +0 (o STREAM)", s.emu_mem(s.r32(ec)), s.port_mem(c->stream))).empty()) return d;
        if (!(d = bytes("fluxo", ec + 4, pc + 4, 0x18 - 4)).empty()) return d;
        // Os atributos: os bytes menos os ponteiros (os do esticamento no heap do SND, os do usuário nos pedaços).
        for (uint32_t base : {0x1cu, 0x84u}) {
            if (!(d = bytes("atributos +" + hex(base), ec + base, pc + base, 0x18)).empty()) return d;
            if (!(d = bytes("atributos +" + hex(base), ec + base + 0x30, pc + base + 0x30, 0x18)).empty()) return d;
            if (!(d = bytes("atributos +" + hex(base), ec + base + 0x58, pc + base + 0x58, 0x10)).empty()) return d;
            for (int k = 0; k < 6; ++k)
                if (!(d = word("atributos +" + hex(base) + " esticamento " + std::to_string(k),
                               s.emu_heap(s.r32(ec + base + 0x18 + uint32_t(k) * 4)),
                               s.port_heap(nf::p32_ptr<uint8_t>(rd32(pc + base + 0x18 + k * 4)))))
                         .empty())
                    return d;
            for (int k = 0; k < 4; ++k)
                if (s.data_differs(s.r32(ec + base + 0x48 + uint32_t(k) * 4), rd32(pc + base + 0x48 + k * 4)))
                    return "atributos +" + hex(base) + " usuário " + std::to_string(k);
        }
        if (!(d = bytes("opções e filtro", ec + 0xec, pc + 0xec, 0x30)).empty()) return d;
        for (uint32_t o : {0x11cu, 0x120u, 0x128u, 0x12cu, 0x134u})
            if (!(d = word("fluxo +" + hex(o), s.emu_mem(s.r32(ec + o)), s.port_mem(rd32(pc + o)))).empty()) return d;
        for (uint32_t o : {0x124u, 0x130u})
            if (!(d = word("fluxo +" + hex(o), s.r32(ec + o), rd32(pc + o))).empty()) return d;
        // Os pedidos.
        for (int k = 0; k < 4; ++k) {
            const uint32_t er = ec + 0x138 + uint32_t(k) * 0x28;
            const uint8_t* pr = pc + 0x138 + k * 0x28;
            const std::string w = "pedido " + std::to_string(k);
            for (uint32_t o : {0u, 4u})
                if (!(d = word(w + " +" + hex(o), s.emu_mem(s.r32(er + o)), s.port_mem(rd32(pr + o)))).empty()) return d;
            if (!(d = bytes(w, er + 8, pr + 8, 0x20)).empty()) return d;
        }
        // O tocador.
        const uint32_t ep = ec + 0x138 + 4 * 0x28;
        const uint8_t* pp = pc + 0x138 + 4 * 0x28;
        if (!(d = bytes("tocador", ep, pp, 0x30)).empty()) return d;
        if (!(d = word("tocador memória", s.emu_mem(s.r32(ep + 0x30)), s.port_mem(rd32(pp + 0x30)))).empty()) return d;
        if (!(d = bytes("tocador", ep + 0x34, pp + 0x34, 8)).empty()) return d;
        if (!(d = word("tocador contexto", s.emu_mem(s.r32(ep + 0x3c)), s.port_mem(rd32(pp + 0x3c)))).empty()) return d;
        if (!(d = bytes("tocador", ep + 0x40, pp + 0x40, 0x1c)).empty()) return d;
        // Os pacotes vivos (do próximo a liberar ao próximo a escrever; os outros guardam ponteiros velhos).
        const drv::SndPacketPlayer* ppl = reinterpret_cast<const drv::SndPacketPlayer*>(pp);
        const int slots = int16_t(ppl->slots) > 0 ? int16_t(ppl->slots) : 1;
        for (int k = ppl->release_rd % slots, guard = 0; ppl->voice >= 0 && k != ppl->wr % slots && guard < 16;
             k = (k + 1) % slots, ++guard) {
            const uint32_t ek = ep + 0x5c + uint32_t(k) * 0x20;
            const uint8_t* pk = pp + 0x5c + k * 0x20;
            if (!(d = bytes("pacote " + std::to_string(k), ek, pk, 8)).empty()) return d;
            for (int ch = 0; ch < ppl->fmt.channels && ch < 6; ++ch)  // (os outros canais guardam o pacote velho)
                if (s.data_differs(s.r32(ek + 8 + uint32_t(ch) * 4), rd32(pk + 8 + ch * 4)))
                    return "pacote " + std::to_string(k) + " canal " + std::to_string(ch) + ": original " +
                           s.emu_data(s.r32(ek + 8 + uint32_t(ch) * 4)) + ", porte " + s.port_data(rd32(pk + 8 + ch * 4));
        }
    }
    // A fila dos callbacks (vazia depois de cada operação).
    if (!(d = word("callbacks", s.r32(kGCbCount), uint32_t(drv::snd_callback_queue_count()))).empty()) return d;
    // O mixador: o estalo do fim, as vozes, as cadeias.
    const drv::MixInternals mi = drv::mix_internals();
    const int outs = mi.create->outputs;
    if (!(d = bytes("mixador: o estalo", kGMixPending, mi.pending, uint32_t(outs) * 4)).empty()) return d;
    if (!(d = bytes("mixador: o estalo molhado", kGMixPendingWet, mi.pending_wet, 4)).empty()) return d;
    const uint32_t emv = s.r32(kGMixVoices);
    for (int v = 0; v < mi.create->voices; ++v) {
        const uint32_t ev = emv + uint32_t(v) * 0x60;
        const uint8_t* pv = reinterpret_cast<const uint8_t*>(&mi.voices[v]);
        const std::string w = "voz do mixador " + std::to_string(v);
        if (!(d = bytes(w, ev, pv, 0x40)).empty()) return d;
        if (!(d = bytes(w, ev + 0x44, pv + 0x44, 4)).empty()) return d;
        for (uint32_t o = 0x40; o < 0x60; o += 4) {
            if (o == 0x44) continue;
            if (!(d = word(w + " +" + hex(o), s.emu_heap(s.r32(ev + o)),
                           s.port_heap(nf::p32_ptr<uint8_t>(rd32(pv + o)))))
                     .empty())
                return d;
        }
        if (mi.voices[v].state == 0) continue;
        // A cadeia: cada filtro com o seu estado.
        uint32_t ef = s.r32(ev + 0x40);
        const drv::SFilter* pf = nf::raw(mi.voices[v].chain);
        for (int n = 0; ef != 0 && pf != nullptr && n < 8; ++n) {
            const uint8_t* pb = reinterpret_cast<const uint8_t*>(pf);
            const std::string wf = w + " filtro " + std::to_string(n);
            if (!(d = bytes(wf, ef, pb, 8)).empty()) return d;
            // (o SNDMEMI_allocz não zera: os campos que nenhuma iniciação escreve (+0x0c, +0x10 na cabeça, +0x14, o
            // +0x1a fora do desempacotador, +0x1b) guardam o que estava no heap, que o porte não reproduz byte a byte
            // (o CEAXABLKDecf do porte não tem o leiaute do PS2); só o +0x1a, passado como t0 à entrada, que o
            // desempacotador ignora, é lido)
            if (!(d = word(wf + " +8", s.emu_heap(s.r32(ef + 8)), s.port_heap(nf::p32_ptr<uint8_t>(rd32(pb + 8)))))
                     .empty())
                return d;
            if (n > 0 && !(d = word(wf + " +0x10", s.emu_heap(s.r32(ef + 0x10)),
                                    s.port_heap(nf::p32_ptr<uint8_t>(rd32(pb + 0x10)))))
                             .empty())
                return d;
            if (!(d = bytes(wf + " ordem", ef + 0x18, pb + 0x18, 2)).empty()) return d;
            if (pf->process == drv::kFnUnpackXapf && !(d = bytes(wf + " canal", ef + 0x1a, pb + 0x1a, 1)).empty())
                return d;
            if (pf->process == drv::kFnUnpackXapf) {
                const auto* x = reinterpret_cast<const drv::SFilterXapf*>(pf);
                if (!(d = word(wf + " decodificador", s.emu_heap(s.r32(ef + 0x1c)), s.port_heap(nf::raw(x->dec)))).empty())
                    return d;
                if (s.data_differs(s.r32(ef + 0x20), nf::p32_u(nf::raw(x->pkt))))
                    return wf + " pacote: original " + s.emu_data(s.r32(ef + 0x20)) + ", porte " +
                           s.port_data(nf::p32_u(nf::raw(x->pkt)));
                if (!(d = bytes(wf, ef + 0x24, pb + 0x24, 0x11)).empty()) return d;
                // O CEAXABLKDecf (0xa8 no PS2): o que o porte guarda.
                const uint32_t ed = s.r32(ef + 0x1c);
                const drv::CEAXABLKDecf& pd = *nf::raw(x->dec);
                if (!(d = word(wf + " xa restantes", s.r32(ed + 4), uint32_t(pd.left))).empty()) return d;
                if (!(d = word(wf + " xa amostras", s.r32(ed + 8), uint32_t(pd.frames))).empty()) return d;
                if (!(d = word(wf + " xa bytes", s.r32(ed + 0xc), uint32_t(pd.bytes))).empty()) return d;
                if (pd.left > 0 &&
                    !(d = word(wf + " xa próxima", s.r32(ed + 0x10) - (ed + 0x24), uint32_t((pd.next - pd.buffer) * 4)))
                         .empty())
                    return d;
                // (o construtor do original não inicia o buffer, o +0x10, o pedido nem a entrada: só a parte válida)
                if (pd.left > 0) {
                    const uint32_t first = s.r32(ed + 0x10) - (ed + 0x24);
                    if (!(d = bytes(wf + " xa buffer", ed + 0x24 + first, reinterpret_cast<const uint8_t*>(pd.buffer) + first,
                                    uint32_t(pd.left) * 4))
                             .empty())
                        return d;
                }
                if (!(d = bytes(wf + " xa estado", ed + 0x98, &pd.pkt.s1, 4)).empty()) return d;
                if (!(d = bytes(wf + " xa estado", ed + 0x9c, &pd.pkt.s2, 4)).empty()) return d;
                if (pd.frames != 0 && s.data_differs(s.r32(ed + 0xa0), nf::p32_u(pd.pkt.in)))
                    return wf + " xa entrada: original " + s.emu_data(s.r32(ed + 0xa0)) + ", porte " +
                           s.port_data(nf::p32_u(pd.pkt.in));
            } else if (pf->process == drv::kFnRsf || pf->process == drv::kFnLpfRc) {
                if (!(d = bytes(wf, ef + 0x1c, pb + 0x1c, pf->process == drv::kFnRsf ? 0x1c : 0xc)).empty()) return d;
            }
            ef = s.r32(ef + 8);
            pf = nf::raw(pf->next);
        }
        if ((ef == 0) != (pf == nullptr)) return w + ": o tamanho da cadeia";
    }
    // As somas em float e as amostras que vão ao IOP.
    const drv::SndState& ps = drv::snd_state();
    for (int o = 0; o < outs; ++o) {
        if (!(d = bytes("soma da saída " + std::to_string(o), s.r32(kGMixOutbuf + uint32_t(o) * 4), mi.outbuf[o],
                        uint32_t(frames_tick) * 4))
                 .empty())
            return d;
        if (!(d = bytes("saída " + std::to_string(o), kGTagbuf + ps.mix_out[o], ps.tagbuf.data() + ps.mix_out[o],
                        uint32_t(frames_tick) * 2))
                 .empty())
            return d;
    }
    // As vozes do SND (os ponteiros para os bancos ficam nulos nos fluxos).
    const uint32_t ev0 = s.r32(kGVoices);
    for (int v = 0; v < ps.num_voices; ++v) {
        const uint8_t* pv = reinterpret_cast<const uint8_t*>(&ps.voices[v]);
        const uint32_t a = ev0 + uint32_t(v) * 0x88;
        const std::string w = "voz do SND " + std::to_string(v);
        if (!(d = bytes(w, a, pv, 0x68)).empty()) return d;
        if (!(d = bytes(w, a + 0x7c, pv + 0x7c, 0xc)).empty()) return d;
    }
    return {};
}

struct AsfFile {
    std::string name;  // "|nome.asf"
    std::vector<uint8_t> data;
};

// O SNDI_patchtohdr sobre o SCHl de cada .asf (todos os .MUS e .SPE).
int check_headers(Emu& e, const std::vector<AsfFile>& all) {
    std::printf("\n-- o cabeçalho dos fluxos (SNDI_patchtohdr) --\n");
    // O SNDI_patchtohdr lê as opções e os azimutes dos canais do SND: os dois lados como no boot (as partes anteriores
    // do nf_driving_check mexem no SND do porte).
    drv::snd_power_on();
    uint64_t n = 0, bad = 0;
    std::string first;
    for (const AsfFile& f : all) {
        if (f.data.size() < 16 || rd32(f.data.data()) != 0x6c484353u) continue;
        const uint32_t size = std::min<uint32_t>(rd32(&f.data[4]) & 0xffffff, uint32_t(f.data.size()));
        std::memcpy(e.m.mem(kHdrIn, size), f.data.data(), size);
        std::memset(e.m.mem(kHdrOut, 0x100), 0xcd, 0x100);
        // o original lê o heap do SND para os blocos de esticamento: nenhum .asf os tem (conferido abaixo)
        e.call(kFnPatchToHdr, {0, kHdrIn + 8, kHdrOut, kHdrOut + 0x10, kHdrOut + 0x80});
        drv::SndSampleFormat fmt;
        drv::SndSampleAttr attr;
        uint32_t info[7];
        std::vector<uint8_t> copy(f.data.begin(), f.data.begin() + long(size));
        drv::snd_test_patchtohdr(0, copy.data() + 8, &fmt, &attr, info);
        ++n;
        std::string d;
        auto cmp = [&](const char* what, uint32_t emu, const void* port, uint32_t sz) {
            if (!d.empty()) return;
            if (std::memcmp(e.m.mem(emu, sz), port, sz) != 0) d = f.name + ": " + what;
        };
        cmp("o formato", kHdrOut, &fmt, 4);
        cmp("os atributos", kHdrOut + 0x10, &attr, 0x18);
        cmp("os atributos (tamanhos)", kHdrOut + 0x10 + 0x30, reinterpret_cast<const uint8_t*>(&attr) + 0x30, 0x18);
        cmp("os atributos (usuário)", kHdrOut + 0x10 + 0x58, reinterpret_cast<const uint8_t*>(&attr) + 0x58, 0x10);
        cmp("a informação", kHdrOut + 0x80, info, 0x1c);
        for (int k = 0; k < 6 && d.empty(); ++k)
            if (e.m.read32(kHdrOut + 0x10 + 0x18 + uint32_t(k) * 4) != 0 || attr.stretch[k] != nullptr)
                d = f.name + ": um bloco de esticamento";
        for (int k = 0; k < 4 && d.empty(); ++k) {
            const uint32_t ea = e.m.read32(kHdrOut + 0x10 + 0x48 + uint32_t(k) * 4);
            const uint8_t* pa = nf::raw(attr.user[k]);
            const uint32_t eo = ea == 0 ? 0xffffffffu : ea - kHdrIn;
            const uint32_t po = pa == nullptr ? 0xffffffffu : uint32_t(pa - copy.data());
            if (eo != po) d = f.name + ": o usuário " + std::to_string(k);
        }
        if (!d.empty() && bad++ == 0) first = d;
    }
    std::printf("  %" PRIu64 " cabeçalhos: %" PRIu64 " diferentes\n", n, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    return bad ? 1 : 0;
}

// O SND dos fluxos inteiro, do SNDSTRM ao mixador do EE.
int check_snd_streams(Emu& e, const std::vector<AsfFile>& paris, int cases, uint32_t seed) {
    std::printf("\n-- os fluxos do SND (SNDSTRM, SNDPKTPLAY, o mixador do EE) --\n");
    std::map<std::string, const std::vector<uint8_t>*> by_name;
    for (const AsfFile& f : paris) by_name[f.name] = &f.data;
    auto loader = [&by_name](const std::string& name, std::vector<uint8_t>& out) {
        std::string k = name;
        for (char& ch : k)
            if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
        auto it = by_name.find(k);
        if (it == by_name.end()) return false;
        out = *it->second;
        return true;
    };
    e.loader = loader;
    drv::stream_set_loader(loader);
    drv::stream_power_on();

    uint64_t bad = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };
    // A iniciação como o ASoundManager::Init (o ASystem: SNDSYSI_init com o heap de 0x10000, SNDmastervol(0x40)); o IOP
    // portado do lado do porte sobre uma SPU2 que não toca (os fluxos não chegam a ela: o mixador do EE é conferido).
    class NullLibsd : public drv::SndLibsd {
    public:
        void Init(int32_t) override {}
        void SetParam(uint16_t, uint16_t) override {}
        uint16_t GetParam(uint16_t) override { return 0; }
        void SetSwitch(uint16_t, uint32_t) override {}
        void SetAddr(uint16_t, uint32_t) override {}
        uint32_t GetAddr(uint16_t) override { return 0; }
        void SetCoreAttr(uint16_t, uint16_t) override {}
        void SetEffectAttr(int32_t, const drv::SndEffectAttr&) override {}
        void VoiceTrans(int32_t, int32_t, const uint8_t*, uint32_t, uint32_t) override {}
        void BlockTrans(int32_t, int32_t, int16_t*, uint32_t) override {}
    };
    static NullLibsd libsd;
    drv::snd_power_on();
    drv::snddrv_iop_start(&libsd);
    e.call(kFnSetOpts, {1});
    const int32_t init_emu = int32_t(e.call(kFnSysInit, {kSndHeap, 0x10000, 0x72a00}));
    e.call(kFnMasterVol, {0x40});
    drv::ASystem_SetOpts(1);
    const int32_t init_port = drv::ASystem_Construct();
    if (init_emu != init_port) fail("SNDSYSI_init");
    const drv::SndState& ps = drv::snd_state();
    const int frames_tick = ps.frames_tick;
    Sides s{e};
    // O fluxo como o do AStreamPriv.
    const int32_t mem_size = kBuffer + drv::SNDSTRM_overhead(4, 0xf);
    std::vector<uint8_t> port_mem(size_t(mem_size) + 0x400, 0);
    {
        uintptr_t a = reinterpret_cast<uintptr_t>(port_mem.data());
        a = (a + 0x3ff) & ~uintptr_t(0x3ff);
        s.pmem = reinterpret_cast<uint8_t*>(a);
    }
    std::memset(e.m.mem(kStrmMem, uint32_t(mem_size)), 0, uint32_t(mem_size));
    drv::SndPlayOpts opts{};
    drv::SNDplaysetdef(&opts);
    e.call(kFnPlaySetDef, {kOpts});
    const int32_t st_emu = int32_t(e.call(kFnStrmCreate, {kOpts, 4, 0xf, kStrmMem, uint32_t(mem_size)}));
    const int32_t st_port = drv::SNDSTRM_create(&opts, 4, 0xf, s.pmem, mem_size);
    if (st_emu != st_port) fail("SNDSTRM_create: original " + std::to_string(st_emu) + ", porte " + std::to_string(st_port));
    if (const drv::SndStreamChannel* c = drv::snd_stream_channel(0)) {
        s.port_handle = c->stream;
        s.emu_handle = e.m.read32(e.m.read32(kGStreams));
    }
    if (bad == 0) {
        const std::string d = compare_all(s, frames_tick);
        if (!d.empty()) fail("depois do SNDSTRM_create: " + d);
    }
    // As saídas do mixador do porte (a tagbuf) para o MIX_audio.
    int16_t* outs[8] = {};
    auto port_tick = [&]() {
        drv::SNDSYSI_timerservice(0x5e);
        drv::SNDDRV_updatevoices();
        drv::SndState& st = drv::snd_state();
        for (int i = 0; i < 8; ++i)
            outs[i] = st.mix_out[i] != 0 ? reinterpret_cast<int16_t*>(st.tagbuf.data() + st.mix_out[i]) : nullptr;
        drv::MIX_audio(outs, st.frames_tick);
        drv::SNDPKTPLAYI_flushcallbackdata();
    };
    auto emu_tick = [&]() {
        e.call(kFnTimerService, {0x5e});
        e.call(kFnUpdateVoices, {});
        e.call(kFnMixAudio, {kGMixOut, uint32_t(frames_tick)});
        e.call(kFnFlush, {});
    };

    Rng rng(seed ^ 0x23a5);
    std::vector<int32_t> ids;
    uint64_t ops = 0, ticks = 0, queued = 0, played_ticks = 0, pitched = 0, filtered = 0, held = 0, purges = 0,
             services = 0;
    double peak = 0;
    for (int k = 0; k < cases * 4 && bad == 0; ++k) {
        const int op = rng.range(0, 99);
        std::string what;
        int32_t r_emu = 0, r_port = 0;
        auto pick_id = [&]() -> int32_t {
            if (ids.empty() || rng.chance(0.08)) return int32_t(rng.next() & 0xfff) - 0x10;
            return ids[size_t(rng.range(0, int(ids.size()) - 1))];
        };
        if (op < 8) {
            const AsfFile& f = paris[size_t(rng.range(0, int(paris.size()) - 1))];
            int32_t hold = 250;
            const int h = rng.range(0, 9);
            if (h == 0) hold = -1, ++held;
            else if (h == 1) hold = 0;
            else if (h == 2) hold = rng.range(1, 5000);
            const std::string name = rng.chance(0.02) ? std::string("|nao_existe.asf") : f.name;
            std::memcpy(e.m.mem(kName, uint32_t(name.size() + 1)), name.c_str(), name.size() + 1);
            what = "SNDSTRM_queuefile(" + name + ", " + std::to_string(hold) + ")";
            r_emu = int32_t(e.call(kFnStrmQueue, {0, uint32_t(hold), kName, 0}));
            r_port = drv::SNDSTRM_queuefile(0, hold, name.c_str(), 0);
            if (r_port >= 0) {
                ids.push_back(r_port);
                if (ids.size() > 40) ids.erase(ids.begin());
                ++queued;
            }
        } else if (op < 10) {
            what = "SNDSTRM_purge";
            r_emu = int32_t(e.call(kFnStrmPurge, {0}));
            r_port = drv::SNDSTRM_purge(0);
            ++purges;
        } else if (op < 13) {
            const int32_t id = pick_id();
            const int16_t hold = int16_t(rng.chance(0.6) ? 0 : rng.range(-1, 3000));
            what = "SNDSTRM_modifyhold";
            r_emu = int32_t(e.call(kFnStrmModifyHold, {uint32_t(id), uint32_t(int32_t(hold))}));
            r_port = drv::SNDSTRM_modifyhold(id, hold);
        } else if (op < 17) {
            const int32_t v = rng.range(0, 127);
            what = "SNDSTRM_vol";
            r_emu = int32_t(e.call(kFnStrmVol, {0, uint32_t(v)}));
            r_port = drv::SNDSTRM_vol(0, v);
        } else if (op < 18) {
            const int32_t ms = rng.range(0, 4000), v = rng.range(0, 127);
            what = "SNDSTRM_autovol";
            r_emu = int32_t(e.call(kFnStrmAutoVol, {0, uint32_t(ms), uint32_t(v)}));
            r_port = drv::SNDSTRM_autovol(0, ms, v);
        } else if (op < 20) {
            const int32_t az = int32_t(rng.next() & 0xffff);
            what = "SNDSTRM_3dpos";
            r_emu = int32_t(e.call(kFnStrm3dPos, {0, uint32_t(az), 0}));
            r_port = drv::SNDSTRM_3dpos(0, az, 0);
        } else if (op < 22) {
            // O pitch: quase sempre 1,0 (o reamostrador copia); às vezes outro (o caminho interpolado).
            const int32_t p = rng.chance(0.5) ? 0x1000 : rng.range(0x600, 0x1c00);
            if (p != 0x1000) ++pitched;
            what = "SNDSTRM_pitchmult";
            r_emu = int32_t(e.call(kFnStrmPitch, {0, uint32_t(p)}));
            r_port = drv::SNDSTRM_pitchmult(0, p);
        } else if (op < 24) {
            const int32_t lp = rng.chance(0.5) ? 0xffff : rng.range(0, 24000);
            if (lp < 24000) ++filtered;
            what = "SNDSTRM_lowpass";
            r_emu = int32_t(e.call(kFnStrmLowpass, {0, uint32_t(lp)}));
            r_port = drv::SNDSTRM_lowpass(0, lp);
        } else if (op < 25) {
            const int32_t lv = rng.range(0, 0x20000);
            what = "SNDSTRM_setgreedylevel";
            r_emu = int32_t(e.call(kFnStrmGreedy, {0, uint32_t(lv)}));
            r_port = drv::SNDSTRM_setgreedylevel(0, lv);
        } else if (op < 30) {
            what = "SNDSTRM_status";
            std::memset(e.m.mem(kStatus, 0x10), 0xcd, 0x10);
            drv::SndStreamStatus st{};
            r_emu = int32_t(e.call(kFnStrmStatus, {0, kStatus}));
            r_port = drv::SNDSTRM_status(0, &st);
            if (std::memcmp(e.m.mem(kStatus, 0xc), &st, 0xc) != 0) fail("SNDSTRM_status: os campos");
        } else if (op < 35) {
            const int32_t id = pick_id();
            what = "SNDSTRM_requeststatus";
            std::memset(e.m.mem(kStatus, 0x10), 0xcd, 0x10);
            drv::SndRequestStatus st{};
            r_emu = int32_t(e.call(kFnStrmReqStatus, {uint32_t(id), kStatus}));
            r_port = drv::SNDSTRM_requeststatus(id, &st);
            if (std::memcmp(e.m.mem(kStatus, 0x10), &st, 0x10) != 0) fail("SNDSTRM_requeststatus: os campos");
        } else if (op < 55) {
            // Um quadro do jogo: o SNDSYS_service (os clientes do serviço: o SNDSTRMI_service).
            what = "SNDSYS_service";
            r_emu = int32_t(e.call(kFnSysService, {}));
            r_port = drv::SNDSYS_service();
            ++services;
        } else {
            what = "tick do driver";
            emu_tick();
            port_tick();
            ++ticks;
            if (drv::mix_voices_playing() > 0) ++played_ticks;
            if (nf::get_env_narrow("NF_P23_DEBUG", nullptr) && ticks < 40) {
                const drv::SndStreamChannel* c = drv::snd_stream_channel(0);
                const drv::StreamModel* md = drv::stream_model(s.port_handle);
                std::printf("  tick %llu: estado %d pedidos %d voz %d livres %d | stream avail %d uso %d estado %d | srv %d tick %u\n",
                            (unsigned long long)ticks, c ? c->state : -1, c ? c->active.count : -1, c ? c->voice : -2,
                            c ? c->free_list.count : -1, md ? md->gettable() : -1, md ? md->usage() : -1,
                            md ? md->state() : -1, int(drv::snd_state().n_server_clients), drv::snd_state().tick);
            }
            for (int o = 0; o < 2; ++o)
                for (int i = 0; i < frames_tick; ++i)
                    peak = std::max(peak, std::abs(double(outs[o] ? outs[o][i] : 0)));
        }
        ++ops;
        if (r_emu != r_port) {
            fail(what + ": original " + std::to_string(r_emu) + ", porte " + std::to_string(r_port));
            break;
        }
        const std::string d = compare_all(s, frames_tick);
        if (!d.empty()) fail("depois de " + what + " (operação " + std::to_string(ops) + "): " + d);
        if (!d.empty() && nf::get_env_narrow("NF_P23_DEBUG", nullptr)) {
            if (const drv::SndPacketPlayer* pl = drv::snd_packet_player(0))
                std::printf("  tocador voz %d alto %d ativo %d liberar %d escrever %d ler %d,%d na fila %d,%d quadros %d/%d\n",
                            pl->voice, pl->high, pl->active, pl->release_rd, pl->wr, pl->rd[0], pl->rd[1], pl->queued[0],
                            pl->queued[1], pl->frames_queued, pl->frames_out);
            const drv::MixInternals mi = drv::mix_internals();
            const uint32_t emv = e.m.read32(kGMixVoices);
            for (int v = 0; v < mi.create->voices; ++v) {
                if (mi.voices[v].state == 0 && e.m.mem(emv + uint32_t(v) * 0x60, 1)[0] == 0) continue;
                std::printf("  voz %d estado %d/%d\n", v, e.m.mem(emv + uint32_t(v) * 0x60, 1)[0], mi.voices[v].state);
                uint32_t ef = e.m.read32(emv + uint32_t(v) * 0x60 + 0x40);
                const drv::SFilter* pf = nf::raw(mi.voices[v].chain);
                for (int n = 0; n < 6 && (ef != 0 || pf != nullptr); ++n) {
                    std::printf("    emu %x: %08x %08x ordem %x | porte %x: %08x %08x ordem %x\n", ef ? ef - kSndHeap : 0,
                                ef ? e.m.read32(ef) : 0, ef ? e.m.read32(ef + 4) : 0, ef ? e.m.read32(ef + 0x18) & 0xffff : 0,
                                pf ? s.port_heap(pf) : 0, pf ? pf->process : 0, pf ? pf->restore : 0, pf ? pf->order : 0);
                    ef = ef ? e.m.read32(ef + 8) : 0;
                    pf = pf ? nf::raw(pf->next) : nullptr;
                }
            }
            for (int v = 0; v < mi.create->voices; ++v)
                std::printf("  voz %d: lowpass %x/%x rsf %x/%x unpack %x/%x\n", v,
                            e.m.read32(emv + uint32_t(v) * 0x60 + 0x58), s.port_heap(nf::raw(mi.voices[v].lowpass)),
                            e.m.read32(emv + uint32_t(v) * 0x60 + 0x50), s.port_heap(nf::raw(mi.voices[v].rsf)),
                            e.m.read32(emv + uint32_t(v) * 0x60 + 0x48), s.port_heap(nf::raw(mi.voices[v].unpack)));
        }
    }
    std::printf("  %" PRIu64 " operações: %" PRIu64 " pedidos (%" PRIu64 " segurados), %" PRIu64 " purges, %" PRIu64
                " pitch fora de 1, %" PRIu64 " passa-baixas, %" PRIu64 " SNDSYS_service, %" PRIu64 " ticks do driver (%" PRIu64
                " com voz tocando, pico %.0f), %" PRIu64 " KB de .asf lidos: %" PRIu64 " diferentes\n",
                ops, queued, held, purges, pitched, filtered, services, ticks, played_ticks, peak, e.file_bytes / 1024,
                bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    std::printf("  guardas do porte: mixador %" PRIu64 ", fluxos %" PRIu64 ", SND %" PRIu64 "\n", drv::mix_guards(),
                drv::snd_stream_guards(), drv::snd_guards());
    drv::stream_set_loader(nullptr);
    return bad ? 1 : 0;
}

// ---- a camada do jogo: AStream, AFader e os eventos de som ----

constexpr uint32_t kGameHeap = 0x01400000, kGameHeapEnd = 0x01c00000;
constexpr uint32_t kGSoundList = 0x355dc0, kGFailed = 0x355dc4, kGViews = 0x355cd0, kGTimerRate = 0x3c80d0;
constexpr uint32_t kGTrigger = 0x355db0, kGHoldBegin = 0x355db4, kGMissionOver = 0x355e88, kGHudFade = 0x355e6c;
constexpr uint32_t kGIsPaused = 0x3c82c8, kGCount = 0x355e60, kGSystem = 0x355d28;
constexpr uint32_t kFnSetPath = 0x2e86a0, kFnAStreamCreate = 0x2e9578, kFnAStreamPlay = 0x2e8c70;
constexpr uint32_t kFnAStreamEvent = 0x2e9728, kFnNext = 0x2e99d0, kFnLast = 0x2e9ae0, kFnStop = 0x2e9bc8;
constexpr uint32_t kFnFadeOut = 0x2e9bf8, kFnLatency = 0x2e9c48, kFnAFaderCreate = 0x2f9688, kFnAFaderGet = 0x2f9710;
constexpr uint32_t kFnQueuePrimary = 0x2f9798, kFnAFaderUpdate = 0x2f9828, kFnSetSecondary = 0x2f97b8;
constexpr uint32_t kFnCall911 = 0x2f9808, kFnAMixGet = 0x2f2dc8;
constexpr uint32_t kFnEStream = 0x15d000, kFnERelease = 0x1560d8, kFnESetMix = 0x157308, kFnEResetMix = 0x1562c8;
constexpr uint32_t kFnSetTransition = 0x2f33a0;
constexpr uint32_t kStrA = 0x01f01000, kStrB = 0x01f01100, kStrC = 0x01f01200, kPath = 0x01f01300, kEvent = 0x01f01400;

// O lado emulado da camada do jogo: o alocador do jogo, as mixagens (cópias das do porte com o mesmo nome) e os
// ganchos.
struct GameEmu {
    Emu& e;
    uint32_t heap = kGameHeap;
    std::map<const drv::AMix*, uint32_t> mix_emu;
    std::map<uint32_t, const drv::AMix*> emu_mix;
    std::vector<std::string> mix_order;

    explicit GameEmu(Emu& emu) : e(emu) {}
    uint32_t alloc(uint32_t n, uint32_t align = 16) {
        heap = (heap + align - 1) & ~(align - 1);
        const uint32_t a = heap;
        heap += (n + 15) & ~15u;
        if (heap > kGameHeapEnd) throw std::runtime_error("o heap emulado do jogo acabou");
        std::memset(e.m.mem(a, n), 0, n);
        return a;
    }
    uint32_t str(const std::string& s) {
        const uint32_t a = alloc(uint32_t(s.size() + 1));
        std::memcpy(e.m.mem(a, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
        return a;
    }
    const drv::AMix* port_mix(const std::string& name) {
        const drv::AudioInternals in = drv::audio_internals();
        auto it = in.mixes->find(name);
        if (it == in.mixes->end()) return drv::AMix_Get(name.c_str());  // (o original também a cria)
        return it->second.get();
    }
    uint32_t mix(const drv::AMix* m) {
        if (m == nullptr) return 0;
        auto it = mix_emu.find(m);
        if (it != mix_emu.end()) return it->second;
        const uint32_t a = alloc(0x50);
        mix_emu[m] = a;
        emu_mix[a] = m;
        mix_order.push_back(m->name);
        write_mix(m, a);
        return a;
    }
    void write_mix(const drv::AMix* m, uint32_t a) {
        const uint32_t master = mix(m->master);
        e.m.write32(a, master);
        const float f[] = {m->volume, m->saved, m->preset, m->initial, m->target, m->step};
        for (int i = 0; i < 6; ++i) e.m.write32(a + 4 + uint32_t(i) * 4, ps2::fbits(f[i]));
        e.m.write32(a + 0x1c, uint32_t(m->transition));
        e.m.write32(a + 0x20, uint32_t(m->priority));
        e.m.write32(a + 0x24, uint32_t(m->count));
        e.m.write32(a + 0x28, ps2::fbits(m->sum));
        e.m.write32(a + 0x2c, ps2::fbits(m->gain));
        std::memcpy(e.m.mem(a + 0x30, 32), m->name, 32);
    }
    // Uma lista vazia do GCC (o ponteiro para o nó cabeça), como o ASoundManager::Init.
    void empty_list(uint32_t global) {
        const uint32_t holder = alloc(4), node = alloc(0xc);
        e.m.write32(holder, node);
        e.m.write32(node, node);
        e.m.write32(node + 4, node);
        e.m.write32(global, holder);
    }

    void install() {
        MipsMachine& m = e.m;
        auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
        auto alloc_hook = [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); };
        m.hook(0x002cfda8, "UMemory::FastAlloc", alloc_hook);
        m.hook(0x002d0910, "__builtin_new", alloc_hook);
        m.hook(0x002d03b8, "UMemory::Alloc", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)), std::max<uint32_t>(16, uint32_t(mm.gpr(R::a1)))));
        });
        m.hook(0x002cfe50, "UMemory::FastFree", ret(0));
        m.hook(0x002d0418, "UMemory::Free", ret(0));
        m.hook(0x002d09a8, "__builtin_delete", ret(0));
        auto cstr = [](MipsMachine& mm, int r) { return std::string(reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(r)), 1))); };
        m.hook(0x002a9198, "strcasecmp", [cstr](MipsMachine& mm) {
            const int r = nf::str_icmp(cstr(mm, R::a0).c_str(), cstr(mm, R::a1).c_str());
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        m.hook(0x002a9508, "strcmp", [cstr](MipsMachine& mm) {
            const int r = std::strcmp(cstr(mm, R::a0).c_str(), cstr(mm, R::a1).c_str());
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        m.hook(0x002a964c, "strcpy", [cstr](MipsMachine& mm) {
            const std::string s = cstr(mm, R::a1);
            const uint32_t d = uint32_t(mm.gpr(R::a0));
            std::memcpy(mm.mem(d, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x002a924c, "strcat", [cstr](MipsMachine& mm) {
            const std::string a = cstr(mm, R::a0), b = cstr(mm, R::a1);
            const uint32_t d = uint32_t(mm.gpr(R::a0));
            std::memcpy(mm.mem(d + uint32_t(a.size()), uint32_t(b.size() + 1)), b.c_str(), b.size() + 1);
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x002a9760, "strlen", [cstr](MipsMachine& mm) { mm.set_gpr(R::v0, cstr(mm, R::a0).size()); });
        m.hook(0x002a9a50, "strncpy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), sa = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            const char* src = reinterpret_cast<const char*>(mm.mem(sa, 1));
            uint32_t i = 0;
            for (; i < n && src[i] != 0; ++i) mm.mem(d + i, 1)[0] = uint8_t(src[i]);
            for (; i < n; ++i) mm.mem(d + i, 1)[0] = 0;
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x002a9098, "sprintf", [cstr](MipsMachine& mm) {
            // Os formatos dos fluxos: "%s", "|%s.asf", "%s%s.asf" (os argumentos inteiros de a2 em diante).
            const std::string fmt = cstr(mm, R::a1);
            const int regs[] = {R::a2, R::a3, R::t0, R::t1, R::t2, R::t3};
            int next = 0;
            std::string out;
            for (size_t i = 0; i < fmt.size(); ++i) {
                if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's' && next < 6) {
                    out += cstr(mm, regs[next++]);
                    ++i;
                } else if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 'd' && next < 6) {
                    out += std::to_string(int32_t(mm.gpr(regs[next++])));
                    ++i;
                } else {
                    out += fmt[i];
                }
            }
            const uint32_t d = uint32_t(mm.gpr(R::a0));
            std::memcpy(mm.mem(d, uint32_t(out.size() + 1)), out.c_str(), out.size() + 1);
            mm.set_gpr(R::v0, out.size());
        });
        m.hook(0x002a7e84, "memcpy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), sa = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n != 0) std::memmove(mm.mem(d, n), mm.mem(sa, n), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x002a7f34, "memmove", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), sa = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n != 0) std::memmove(mm.mem(d, n), mm.mem(sa, n), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(0x00261038, "TIMER_gettick", [](MipsMachine& mm) { mm.set_gpr(R::v0, uint64_t(int64_t(drv::TIMER_gettick()))); });
        m.hook(0x002543d0, "FILESYS_existssync", ret(1));
        m.hook(0x00254308, "FILESYS_addbigsync", [](MipsMachine& mm) {
            mm.write32(uint32_t(mm.gpr(R::a3)), 1);
            mm.set_gpr(R::v0, 1);
        });
        m.hook(0x00254378, "FILESYS_delbigsync", ret(0));
        m.hook(0x00253ba8, "FILESYS_bypassqueuefileinfo", [cstr](MipsMachine& mm) {
            mm.set_gpr(R::v0, drv::filesys_find(cstr(mm, R::a0), nullptr) ? 1 : 0);
        });
        m.hook(0x00206b20, "SMissionManager::GetPlayerHealth", [](MipsMachine& mm) { mm.set_fpr(0, 1.0f); });
        m.hook(0x00206788, "SMissionManager::GoingToSkipCinematic", ret(0));
        auto mix_hook = [this, cstr](MipsMachine& mm) { mm.set_gpr(R::v0, mix(port_mix(cstr(mm, R::a0)))); };
        m.hook(0x002f2d50, "AMix::Add", mix_hook);
        m.hook(kFnAMixGet, "AMix::Get", mix_hook);
        m.hook(0x002f2e18, "AMix::Remove", ret(0));
        // AMix::Reset: cada mixagem do .ini (a lista do porte, na mesma ordem) pelo SetTransition original.
        m.hook(0x002f3450, "AMix::Reset", [this](MipsMachine& mm) {
            const int32_t t = int32_t(mm.gpr(R::a0)) > 0 ? int32_t(mm.gpr(R::a0)) : 1;
            const drv::AudioInternals in = drv::audio_internals();
            for (const drv::AMix* pm : *in.mix_list) {
                const uint32_t a = mix(pm);
                mm.set_fpr(12, ps2::ffloat(mm.read32(a + 0x10)));
                mm.call_nested(kFnSetTransition, {a, uint32_t(t)});
            }
        });
    }
    // Uma chamada com um float em f12.
    uint32_t callf(uint32_t fn, float f12, const std::vector<uint32_t>& args) {
        e.m.set_fpr(12, f12);
        return e.call(fn, args);
    }
};

// O deque<AStreamEntry> do GCC 2.95 (0x28 bytes no começo do AStreamPriv; seis entradas de 0x4c por nó).
std::vector<drv::AStreamEntryView> emu_queue(const Emu& e, uint32_t priv) {
    std::vector<drv::AStreamEntryView> out;
    uint32_t cur = e.m.read32(priv + 8), last = e.m.read32(priv + 0x10), node = e.m.read32(priv + 0x14);
    const uint32_t fin = e.m.read32(priv + 0x18);
    for (int n = 0; cur != fin && n < 4096; ++n) {
        drv::AStreamEntryView v;
        v.name = reinterpret_cast<const char*>(e.m.mem(cur, 1));
        v.fade = ps2::ffloat(e.m.read32(cur + 0x38));
        v.loop = int32_t(e.m.read32(cur + 0x3c));
        v.effect = int32_t(e.m.read32(cur + 0x40));
        v.held = int32_t(e.m.read32(cur + 0x44));
        v.tick = int32_t(e.m.read32(cur + 0x48));
        out.push_back(v);
        cur += 0x4c;
        if (cur == last) {
            node += 4;
            cur = e.m.read32(node);
            last = cur + 0x1c8;
        }
    }
    return out;
}

std::string compare_game(GameEmu& g, uint32_t emu_music, uint32_t emu_speech, uint32_t emu_fader) {
    const Emu& e = g.e;
    auto fw = [](const std::string& w, uint32_t a, uint32_t b) -> std::string {
        if (a != b) return w + ": original " + hex(a) + ", porte " + hex(b);
        return {};
    };
    std::string d;
    // Os globais.
    const drv::AStreamGlobalsView gv = drv::astream_globals();
    if (!(d = fw("AStream::fTrigger", e.m.read32(kGTrigger), uint32_t(gv.trigger))).empty()) return d;
    if (!(d = fw("AStream::fHoldBegin", e.m.read32(kGHoldBegin), uint32_t(gv.hold_begin))).empty()) return d;
    // Os dois fluxos.
    for (int k = 0; k < 2; ++k) {
        const uint32_t a = k == 0 ? emu_music : emu_speech;
        const drv::AStreamView v = drv::astream_view(k == 0 ? "Music" : "Speech");
        const std::string w = k == 0 ? "Music" : "Speech";
        const uint32_t p = e.m.read32(a + 0xb0);
        auto f32 = [&](uint32_t addr, float x) { return ps2::fbits(x) == e.m.read32(addr); };
        if (!f32(a + 0x6c, v.volume)) return w + ": o volume (+0x6c)";
        if (!f32(a + 0x70, v.pitch)) return w + ": o pitch";
        if (!f32(a + 0x60, v.max_distance) || !f32(a + 0x64, v.max_distance2)) return w + ": a distância";
        auto it = g.emu_mix.find(e.m.read32(a + 0x84));
        const std::string emix = it != g.emu_mix.end() ? it->second->name : std::string("?");
        if (emix != v.mix) return w + ": a mixagem: original " + emix + ", porte " + v.mix;
        if (!(d = fw(w + " +0xb4", e.m.read32(a + 0xb4), uint32_t(v.hold_timeout))).empty()) return d;
        if (!(d = fw(w + " +0xb8", e.m.read32(a + 0xb8), uint32_t(v.held_ticks))).empty()) return d;
        if (!(d = fw(w + " +0xbc", e.m.read32(a + 0xbc), uint32_t(v.release_tick))).empty()) return d;
        for (int i = 0; i < 3; ++i)
            if (!(d = fw(w + " status " + std::to_string(i), e.m.read32(p + 0x28 + uint32_t(i) * 4), uint32_t(v.status[i]))).empty())
                return d;
        for (int i = 0; i < 4; ++i)
            if (!(d = fw(w + " requeststatus " + std::to_string(i), e.m.read32(p + 0x34 + uint32_t(i) * 4),
                         uint32_t(v.rstatus[i])))
                     .empty())
                return d;
        const uint32_t fp = e.m.read32(p + 0x44);
        const std::string ef = fp != 0 ? reinterpret_cast<const char*>(e.m.mem(fp, 1)) : "";
        if (ef != v.file) return w + ": o arquivo: original [" + ef + "], porte [" + v.file + "]";
        if (!f32(p + 0x48, v.filter)) return w + ": o filtro";
        if (!f32(p + 0x4c, v.fade)) return w + ": a transição";
        if (!f32(p + 0x50, v.priv_volume)) return w + ": o volume (+0x50)";
        const int32_t ints[] = {v.stream, v.req, v.event_tick, v.play_tick};
        for (int i = 0; i < 4; ++i)
            if (!(d = fw(w + " +" + hex(0x58 + uint32_t(i) * 4), e.m.read32(p + 0x58 + uint32_t(i) * 4), uint32_t(ints[i]))).empty())
                return d;
        const int32_t flags[] = {v.over, v.not_found, v.paused, v.loop, v.effect, v.held};
        for (int i = 0; i < 6; ++i)
            if (!(d = fw(w + " +" + hex(0x70 + uint32_t(i) * 4), e.m.read32(p + 0x70 + uint32_t(i) * 4), uint32_t(flags[i]))).empty())
                return d;
        if (std::string(reinterpret_cast<const char*>(e.m.mem(p + 0x10c, 1))) != v.name) return w + ": o nome (+0x10c)";
        const std::vector<drv::AStreamEntryView> q = emu_queue(e, p);
        if (q.size() != v.queue.size())
            return w + ": a fila: original " + std::to_string(q.size()) + ", porte " + std::to_string(v.queue.size());
        for (size_t i = 0; i < q.size(); ++i)
            if (q[i].name != v.queue[i].name || ps2::fbits(q[i].fade) != ps2::fbits(v.queue[i].fade) ||
                q[i].loop != v.queue[i].loop || q[i].effect != v.queue[i].effect || q[i].held != v.queue[i].held ||
                q[i].tick != v.queue[i].tick)
                return w + ": a fila, entrada " + std::to_string(i);
    }
    // O AFader.
    const drv::AFaderView fv = drv::afader_view("SpeechVsAmbience");
    const uint32_t fp = e.m.read32(emu_fader);
    if (ps2::fbits(fv.level) != e.m.read32(fp)) return "AFader: o nível";
    if (std::string(reinterpret_cast<const char*>(e.m.mem(fp + 4, 1))) != fv.second) return "AFader: a secundária";
    if (ps2::fbits(fv.filter) != e.m.read32(fp + 0x24)) return "AFader: o filtro";
    const int32_t fl[] = {fv.primary, fv.restart, fv.fading, fv.call911, fv.allow_second, fv.second_on, fv.w40};
    for (int i = 0; i < 7; ++i)
        if (!(d = fw("AFader +" + hex(0x28 + uint32_t(i) * 4), e.m.read32(fp + 0x28 + uint32_t(i) * 4), uint32_t(fl[i]))).empty())
            return d;
    // As mixagens.
    for (const auto& kv : g.mix_emu) {
        const drv::AMix* m = kv.first;
        const uint32_t a = kv.second;
        const float f[] = {m->volume, m->saved, m->target, m->step};
        const uint32_t off[] = {4, 8, 0x14, 0x18};
        for (int i = 0; i < 4; ++i)
            if (ps2::fbits(f[i]) != e.m.read32(a + off[i]))
                return std::string("a mixagem ") + m->name + " +" + hex(off[i]) + ": original " + hex(e.m.read32(a + off[i])) +
                       ", porte " + hex(ps2::fbits(f[i]));
        if (uint32_t(m->transition) != e.m.read32(a + 0x1c)) return std::string("a mixagem ") + m->name + ": a transição";
        if (uint32_t(m->count) != e.m.read32(a + 0x24) || ps2::fbits(m->sum) != e.m.read32(a + 0x28))
            return std::string("a mixagem ") + m->name + ": as somas";
    }
    return {};
}

int check_game_streams(const nf::Elf& elf, const nf::DiscSource& disc, const std::vector<AsfFile>& paris, int cases,
                       uint32_t seed) {
    std::printf("\n-- os fluxos do jogo (AStream, AFader, os eventos de som) sobre o SND inteiro --\n");
    Emu e;
    e.setup(elf);
    e.install();
    GameEmu g(e);
    g.install();
    std::map<std::string, const std::vector<uint8_t>*> by_name;
    for (const AsfFile& f : paris) by_name[f.name] = &f.data;
    auto loader = [&by_name](const std::string& name, std::vector<uint8_t>& out) {
        std::string k = name;
        for (char& ch : k)
            if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
        auto it = by_name.find(k);
        if (it == by_name.end()) return false;
        out = *it->second;
        return true;
    };
    e.loader = loader;
    drv::stream_set_loader(loader);
    drv::stream_power_on();
    uint64_t bad = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };
    // O porte: o ASoundManager::Init com os volumes da Paris, sem bancos, e os dois fluxos.
    drv::Bigf viv;
    viv.open(disc, "DRIVING/MIS01.VIV");
    auto file_loader = [&viv](const std::string& path) {
        std::vector<uint8_t> out;
        std::string key = path;
        for (char& ch : key)
            if (ch == '/') ch = '\\';
        if (path.find("paris_mis01.ini") == std::string::npos) return out;  // sem bancos: o heap do SND como o do original
        for (int i = 0; i < int(viv.entries().size()); ++i)
            if (nf::str_icmp(viv.entries()[size_t(i)].name.c_str(), key.c_str()) == 0) {
                viv.read(i, out);
                break;
            }
        return out;
    };
    drv::AudioInit ai;
    ai.track = "paris_mis01";
    ai.stream_file = "MIS01EN";
    ai.big_open = [&disc](const std::string& path) -> std::unique_ptr<drv::Bigf> {
        auto b = std::make_unique<drv::Bigf>();
        if (!b->open(disc, path)) return nullptr;
        return b;
    };
    drv::audio_set_timer(0);
    if (!drv::audio_init(ai, file_loader)) {
        std::printf("  o ASoundManager::Init do porte falhou\n");
        return 1;
    }
    // O original: o SND como o ASystem, as listas do ASoundManager, o caminho e os fluxos.
    e.call(kFnSetOpts, {1});
    e.call(kFnSysInit, {kSndHeap, 0x10000, 0x72a00});
    e.call(kFnMasterVol, {0x40});
    e.m.write32(kGSystem, 1);
    e.m.write32(kGViews, 4);
    e.m.write32(kGTimerRate, 100);
    g.empty_list(kGSoundList);
    g.empty_list(kGFailed);
    e.call(kFnSetPath, {g.str("driving/")});
    const uint32_t emu_music = e.call(kFnAStreamCreate, {g.str("Music"), g.str("MIS01"), 0x20000});
    const uint32_t emu_speech = e.call(kFnAStreamCreate, {g.str("Speech"), g.str("MIS01EN"), 0x20000});
    const uint32_t f2 = e.call(kFnAMixGet, {g.str("0:Fader2")});
    const uint32_t emu_fader = e.call(kFnAFaderCreate, {g.str("SpeechVsAmbience"), emu_speech, f2});
    Sides s{e};
    s.nstreams = 2;
    const drv::AStreamView pv0 = drv::astream_view("Music"), pv1 = drv::astream_view("Speech");
    s.emu_base[0] = e.m.read32(e.m.read32(emu_music + 0xb0) + 0x88);
    s.emu_base[1] = e.m.read32(e.m.read32(emu_speech + 0xb0) + 0x88);
    s.port_base[0] = pv0.mem;
    s.port_base[1] = pv1.mem;
    const int frames_tick = drv::snd_state().frames_tick;
    std::string d0 = compare_all(s, frames_tick);
    if (d0.empty()) d0 = compare_game(g, emu_music, emu_speech, emu_fader);
    if (!d0.empty()) fail("depois da iniciação: " + d0);

    // As listas de nomes.
    std::vector<std::string> music_files, speech_files, amb = {"city", "crowds", "residential", "for_heli", "Off"};
    for (const AsfFile& f : paris) {
        std::string n = f.name.substr(1, f.name.size() - 5);  // "|nome.asf" -> "nome"
        if (n.rfind("par_", 0) == 0) speech_files.push_back(n);
        else if (n != "city" && n != "crowds" && n != "residential" && n != "for_heli") music_files.push_back(n);
    }
    const std::vector<std::string> mixes = {"Music", "Speech", "Ambience", "0:Fader2", "0:Fader0", "Player Car", "NIS"};
    int16_t* outs[8] = {};
    auto port_tick = [&]() {
        drv::SNDSYSI_timerservice(0x5e);
        drv::SNDDRV_updatevoices();
        drv::SndState& st = drv::snd_state();
        for (int i = 0; i < 8; ++i)
            outs[i] = st.mix_out[i] != 0 ? reinterpret_cast<int16_t*>(st.tagbuf.data() + st.mix_out[i]) : nullptr;
        drv::MIX_audio(outs, st.frames_tick);
        drv::SNDPKTPLAYI_flushcallbackdata();
    };
    auto emu_tick = [&]() {
        e.call(kFnTimerService, {0x5e});
        e.call(kFnUpdateVoices, {});
        e.call(kFnMixAudio, {kGMixOut, uint32_t(frames_tick)});
        e.call(kFnFlush, {});
    };
    auto set_str = [&](uint32_t a, const std::string& t) {
        std::memcpy(e.m.mem(a, uint32_t(t.size() + 1)), t.c_str(), t.size() + 1);
    };
    Rng rng(seed ^ 0x9a5);
    int32_t timer = 0;
    uint64_t ops = 0, frames = 0, events = 0, primaries = 0, seconds = 0, music_events = 0, played = 0, repairs = 0;
    double peak = 0;
    for (int k = 0; k < cases * 2 && bad == 0; ++k) {
        const int op = rng.range(0, 99);
        std::string what;
        const uint64_t guards_before = drv::audio_stream_stats().guards;
        try {
            if (op < 40) {
                // Um quadro do jogo: o relógio, o fgCount, o Play dos dois fluxos (o caminho infinito: o volume 1 e o pitch
                // 1, às vezes outro), o AFader::Update, o SNDSYS_service e um ou dois ticks do driver.
                what = "quadro";
                timer += rng.range(1, 3);
                drv::audio_set_timer(timer);
                const int32_t count = rng.range(1, 3);
                drv::sound_count() = count;
                e.m.write32(kGCount, uint32_t(count));
                for (int sidx = 0; sidx < 2; ++sidx) {
                    const float vol = rng.chance(0.8) ? 1.0f : float(rng.range(0, 1000)) / 800.0f;
                    const float pit = rng.chance(0.9) ? 1.0f : float(rng.range(-100, 5000)) / 1000.0f;
                    const float az = rng.chance(0.9) ? 0.0f : float(rng.range(-500, 500)) / 1000.0f;
                    e.m.write32(kPath, ps2::fbits(vol));
                    e.m.write32(kPath + 4, ps2::fbits(pit));
                    e.m.write32(kPath + 8, ps2::fbits(az));
                    e.call(kFnAStreamPlay, {sidx == 0 ? emu_music : emu_speech, kPath});
                    drv::astream_test_play(sidx == 0 ? "Music" : "Speech", vol, pit, az);
                }
                e.call(kFnAFaderUpdate, {emu_fader});
                drv::afader_test_update("SpeechVsAmbience");
                e.call(kFnSysService, {});
                drv::SNDSYS_service();
                const int n = rng.range(1, 2);
                for (int t = 0; t < n; ++t) {
                    emu_tick();
                    port_tick();
                    for (int o = 0; o < 2; ++o)
                        for (int i = 0; i < frames_tick; ++i) peak = std::max(peak, std::abs(double(outs[o] ? outs[o][i] : 0)));
                }
                if (drv::mix_voices_playing() > 0) ++played;
                ++frames;
            } else if (op < 50) {
                // O EStreamEvent: a música (laço, transição, segurado), as falas, o NIS ou um fluxo que não existe.
                what = "EStreamEvent";
                const int kind = rng.range(0, 9);
                std::string stream, file;
                if (kind < 4) {
                    stream = rng.chance(0.5) ? "music" : "Music";
                    file = music_files[size_t(rng.range(0, int(music_files.size()) - 1))];
                    ++music_events;
                } else if (kind < 8) {
                    stream = "speech";
                    file = speech_files[size_t(rng.range(0, int(speech_files.size()) - 1))];
                    ++primaries;
                } else if (kind == 8) {
                    stream = "nis";
                    file = speech_files[size_t(rng.range(0, int(speech_files.size()) - 1))];
                } else {
                    stream = "music";
                    file = "nao_existe";
                }
                const int32_t fade = rng.chance(0.4) ? -1 : rng.range(0, 3);
                const uint32_t loop = rng.chance(0.4) ? 1 : 0, alive = rng.chance(0.3) ? 1 : 0;
                const uint32_t held = rng.chance(0.1) ? 1 : 0;
                set_str(kStrA, stream);
                set_str(kStrB, file);
                const uint32_t w[7] = {0x370318, kStrA, kStrB, uint32_t(fade), loop, alive, held};
                for (int i = 0; i < 7; ++i) e.m.write32(kEvent + uint32_t(i) * 4, w[i]);
                e.call(kFnEStream, {kEvent, 0});
                drv::PendingEvent pe;
                pe.type = 20;
                pe.obj[1] = nf::p32_u(e.m.mem(kStrA, 1));
                pe.obj[2] = nf::p32_u(e.m.mem(kStrB, 1));
                pe.obj[3] = uint32_t(fade);
                pe.obj[4] = loop;
                pe.obj[5] = alive;
                pe.obj[6] = held;
                drv::audio_event_execute(pe);
                ++events;
            } else if (op < 54) {
                what = "AFader::SetSecondary";
                const std::string sec = amb[size_t(rng.range(0, int(amb.size()) - 1))];
                set_str(kStrA, sec);
                e.call(kFnSetSecondary, {emu_fader, kStrA});
                drv::afader_test_set_secondary("SpeechVsAmbience", sec.c_str());
                ++seconds;
            } else if (op < 55) {
                what = "AFader::Call911";
                e.call(kFnCall911, {emu_fader});
                drv::afader_test_call911("SpeechVsAmbience");
            } else if (op < 58) {
                what = "EReleaseStream";
                e.m.write32(kEvent, 0x370978);
                e.call(kFnERelease, {kEvent, 0});
                drv::PendingEvent pe;
                pe.type = 273;
                drv::audio_event_execute(pe);
            } else if (op < 62) {
                what = "ESetAudioMix";
                const std::string mx = mixes[size_t(rng.range(0, int(mixes.size()) - 1))];
                const float pct = float(rng.range(-150, 50));
                const int32_t ticks = rng.range(0, 120);
                set_str(kStrA, mx);
                const uint32_t w[4] = {0x370810, kStrA, uint32_t(ticks), ps2::fbits(pct)};
                for (int i = 0; i < 4; ++i) e.m.write32(kEvent + uint32_t(i) * 4, w[i]);
                e.call(kFnESetMix, {kEvent, 0});
                drv::PendingEvent pe;
                pe.type = 241;
                pe.obj[1] = nf::p32_u(e.m.mem(kStrA, 1));
                pe.obj[2] = uint32_t(ticks);
                pe.obj[3] = ps2::fbits(pct);
                drv::audio_event_execute(pe);
            } else if (op < 64) {
                what = "EResetAudioMix";
                const int32_t ticks = rng.range(-5, 120);
                e.m.write32(kEvent, 0x370948);
                e.m.write32(kEvent + 4, uint32_t(ticks));
                e.call(kFnEResetMix, {kEvent, 0});
                drv::PendingEvent pe;
                pe.type = 266;
                pe.obj[1] = uint32_t(ticks);
                drv::audio_event_execute(pe);
            } else if (op < 72) {
                const int which = rng.range(0, 1);
                const uint32_t es = which == 0 ? emu_music : emu_speech;
                const char* ps = which == 0 ? "Music" : "Speech";
                const int f = rng.range(0, 9);
                if (f < 4) {
                    what = std::string("AStream::Next ") + ps;
                    e.call(kFnNext, {es});
                    drv::astream_test_next(ps);
                } else if (f < 6) {
                    what = std::string("AStream::Last ") + ps;
                    e.call(kFnLast, {es});
                    drv::astream_test_last(ps);
                } else if (f < 8) {
                    what = std::string("AStream::Stop ") + ps;
                    e.call(kFnStop, {es});
                    drv::astream_test_stop(ps);
                } else if (f < 9) {
                    what = std::string("AStream::FadeOut ") + ps;
                    e.call(kFnFadeOut, {es});
                    drv::astream_test_fadeout(ps);
                } else {
                    what = std::string("AStream::GetLatency ") + ps;
                    e.call(kFnLatency, {es});
                    const float l = drv::astream_test_latency(ps);
                    if (ps2::fbits(l) != e.m.fpr_bits(0)) fail(what + ": original " + hex(e.m.fpr_bits(0)) + ", porte " + hex(ps2::fbits(l)));
                }
            } else if (op < 76) {
                // Os globais do ASoundManager: a missão acabada, o fade do HUD, a pausa.
                what = "globais";
                drv::AStreamGlobalsView gv = drv::astream_globals();
                const int f = rng.range(0, 5);
                if (f == 0) gv.mission_over = rng.chance(0.3) ? 1 : 0;
                else if (f < 3) gv.hud_fade = rng.chance(0.2) ? -1 : 0;
                else gv.is_paused = rng.chance(0.3) ? 1 : 0;
                drv::astream_set_globals(gv);
                e.m.write32(kGMissionOver, uint32_t(gv.mission_over));
                std::memcpy(e.m.mem(kGHudFade, 2), &gv.hud_fade, 2);
                e.m.write32(kGIsPaused, uint32_t(gv.is_paused));
            } else {
                // Um evento da música direto (o AStream::Event do EStreamEvent, com efeito às vezes).
                what = "AStream::Event";
                const int which = rng.chance(0.7) ? 0 : 1;
                const std::string file = which == 0 ? music_files[size_t(rng.range(0, int(music_files.size()) - 1))]
                                                    : speech_files[size_t(rng.range(0, int(speech_files.size()) - 1))];
                const float fade = rng.chance(0.5) ? -1.0f : float(rng.range(0, 30)) / 10.0f;
                const bool loop = rng.chance(0.4), effect = rng.chance(0.2), held = rng.chance(0.08);
                set_str(kStrA, file);
                g.callf(kFnAStreamEvent, fade, {which == 0 ? emu_music : emu_speech, kStrA, loop ? 1u : 0u, effect ? 1u : 0u,
                                                 held ? 1u : 0u});
                drv::astream_test_event(which == 0 ? "Music" : "Speech", fade, file.c_str(), loop, effect, held);
            }
        } catch (const nf::MipsError& err) {
            char b[96];
            std::snprintf(b, sizeof b, " (operação %llu): erro no interpretador em %08x: ", (unsigned long long)ops, err.pc);
            fail(what + b + err.what());
            break;
        }
        ++ops;
        // A guarda do porte (o pop_front da deque vazia depois do DecodeError, audio_stream.h): a deque do original
        // fica com o começo depois do fim; repara-se como o porte a deixa (vazia: o começo no fim).
        if (drv::audio_stream_stats().guards != guards_before) {
            for (int k2 = 0; k2 < 2; ++k2) {
                const uint32_t priv = e.m.read32((k2 == 0 ? emu_music : emu_speech) + 0xb0);
                if (!drv::astream_view(k2 == 0 ? "Music" : "Speech").queue.empty()) continue;
                if (e.m.read32(priv + 8) == e.m.read32(priv + 0x18)) continue;
                for (uint32_t w = 0; w < 0x10; w += 4) e.m.write32(priv + 8 + w, e.m.read32(priv + 0x18 + w));
                ++repairs;
            }
        }
        if (nf::get_env_narrow("NF_P23_DEBUG", nullptr)) std::fprintf(stderr, "op %d %s\n", int(ops), what.c_str());
        std::string d;
        try {
            d = compare_game(g, emu_music, emu_speech, emu_fader);
        } catch (const nf::MipsError& err) {
            d = std::string("um ponteiro do original fora da RAM (") + err.what() + ")";
        }
        if (d.empty()) d = compare_all(s, frames_tick);
        if (!d.empty()) fail("depois de " + what + " (operação " + std::to_string(ops) + "): " + d);
    }
    std::printf("  %" PRIu64 " operações: %" PRIu64 " quadros (%" PRIu64 " com voz tocando, pico %.0f), %" PRIu64
                " EStreamEvent (%" PRIu64 " da música, %" PRIu64 " falas), %" PRIu64
                " trocas da secundária, %zu mixagens: %" PRIu64 " diferentes\n",
                ops, frames, played, peak, events, music_events, primaries, seconds, g.mix_emu.size(), bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    const drv::AudioStreamStats st = drv::audio_stream_stats();
    std::printf("  porte: %" PRIu64 " eventos, %" PRIu64 " pedidos ao SNDSTRM, %" PRIu64 " recusados, guardas %" PRIu64
                " (deques do original reparadas %" PRIu64 ")\n",
                st.events, st.queued, st.failed, st.guards, repairs);
    drv::audio_shutdown();
    drv::stream_set_loader(nullptr);
    return bad ? 1 : 0;
}

}  // namespace

int run_stream_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n== parte 23: os fluxos (o mixador do EE, o SNDSTRM, o AStream e o AFader, os eventos de som) ==\n");
    int failures = 0;
    try {
        // Os .asf de todos os .MUS e .SPE (os cabeçalhos) e os de Paris cortados (a sequência).
        std::vector<AsfFile> all, paris;
        for (const nf::DiscEntry& de : disc.list("DRIVING")) {
            std::string n = de.path.substr(de.path.find_last_of('/') + 1);
            const size_t semi = n.find(';');
            if (semi != std::string::npos) n.resize(semi);
            if (n.size() < 4) continue;
            const std::string ext = n.substr(n.size() - 4);
            if (!(ext == ".MUS" || ext == ".SPE" || ext == ".mus" || ext == ".spe")) continue;
            drv::Bigf arc;
            if (!arc.open(disc, "DRIVING/" + n)) continue;
            const bool is_paris = n.rfind("MIS01", 0) == 0 || n.rfind("mis01", 0) == 0;
            for (int i = 0; i < int(arc.entries().size()); ++i) {
                AsfFile f;
                std::string en = arc.entries()[size_t(i)].name;
                for (char& ch : en)
                    if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
                f.name = "|" + en;
                if (!arc.read(i, f.data)) continue;
                if (is_paris) {
                    AsfFile c;
                    c.name = f.name;
                    c.data = cut_asf(f.data);
                    paris.push_back(std::move(c));
                }
                all.push_back(std::move(f));
            }
        }
        std::printf("  %zu .asf nos .MUS e .SPE, %zu de Paris\n", all.size(), paris.size());
        Emu e;
        e.setup(elf);
        e.install();
        failures += check_headers(e, all);
        all.clear();
        failures += check_snd_streams(e, paris, cases, seed);
        failures += check_game_streams(elf, disc, paris, cases, seed);
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s (pc %08x, instrução %08x)\n", err.what(), err.pc, err.word);
        ++failures;
    } catch (const std::exception& ex) {
        std::printf("  erro: %s\n", ex.what());
        ++failures;
    }
    return failures;
}

}  // namespace drvcheck
