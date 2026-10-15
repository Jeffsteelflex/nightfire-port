// nf_driving_check, parte 18 (tests/driving_check_anim.h): o RAnimEngine do DRIVING.ELF no interpretador MIPS lado a
// lado com o porte (driving/render/anim_engine.h). docs/driving.md seção 22.
//
// O CARP do Vanquish (MIS01.VIV), desserializado e resolvido pelo porte, vai inteiro para a RAM emulada com as palavras
// que apontam para dentro dele trocadas pelos endereços emulados; o objeto de cena (o dono do Handle, com o dono físico
// e um BaseDesc com a caixa do dano) é falso nos dois lados, com a tabela virtual do emulado apontando para o
// RSceneObj::SetEventDynamicData original. Depois: o Handle::Create das 57 instâncias, e operações sorteadas (os
// estímulos de um sistema ou de todos, os Updates com o passo andando, o Stop, o InitAllSystemStates e as consultas),
// comparando o bloco do Handle (os ponteiros para dentro dele e do CARP relativos), os identificadores que o Create grava
// nos artigos, a lista dos sistemas tocando, o último tique, os eventos criados (o número e os dados, pela
// RegisterEvent::LookupEvent) e o EventDynamicData.
//
// Fora do código original (ganchos nativos): a memória (UMemory::FastAlloc, MEM_fill, MEM_copy) e as criadoras dos
// eventos (a LookupEvent devolve uma função que anota o número e os dados).

#include "driving_check_anim.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/car_model.h"
#include "driving/game/events.h"
#include "driving/game/triggers.h"
#include "driving/render/anim_engine.h"
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

constexpr uint32_t kFnCreate = 0x1a1bf0, kFnStimuli = 0x1a27a0, kFnStimuliAll = 0x1a2868, kFnUpdate = 0x1a1a10;
constexpr uint32_t kFnStop = 0x1a25a0, kFnInitStates = 0x1a2c88, kFnAnyPlaying = 0x1a2cc0, kFnIsPlaying = 0x1a2d10;
constexpr uint32_t kFnStartTick = 0x1a2d40, kFnMaxTicks = 0x1a2d68, kFnState = 0x1a2d90, kFnInstSysID = 0x1a2db8;
constexpr uint32_t kFnExists = 0x1a2df0, kFnFindEffect = 0x1a2750, kFnSetEventData = 0x1c8b08, kFnEvaluate = 0x1a30c0;
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnMemFill = 0x260300, kFnMemCopy = 0x2601f0, kFnLookupEvent = 0x1757b8;

constexpr uint32_t kCount = 0x333600, kLastTick = 0x333604, kList = 0x3d1518, kFrequency = 0x333ff4;
constexpr uint32_t kEventData = 0x32e708;

constexpr uint32_t kHeap = 0x01000000, kHeapEnd = 0x01400000;
constexpr uint32_t kScene = 0x01400000, kOwner = 0x01400100, kBase = 0x01400200, kSceneVt = 0x01400300;
constexpr uint32_t kCreatorStub = 0x01401000;
constexpr uint32_t kCarp = 0x01500000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1818) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
    float real(float a, float b) { return a + (b - a) * float(next() & 0xffffff) / float(0x1000000); }
};

struct Event {
    int32_t type;
    uint32_t data;  // relativo ao começo do CARP
    bool operator==(const Event& o) const { return type == o.type && data == o.data; }
};

struct Ctx {
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    int32_t pending_type = -1;
    std::vector<Event> events;
    std::string error;
    uint64_t errors = 0;

    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
        }
    }
    Ctx(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(kFnFastAlloc, "UMemory::FastAlloc",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(kFnMemFill, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
        });
        m.hook(kFnMemCopy, "MEM_copy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), s = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(d, n), mm.mem(s, n), n);
        });
        m.hook(kFnLookupEvent, "RegisterEvent::LookupEvent", [this](MipsMachine& mm) {
            const int32_t t = int32_t(mm.gpr(R::a0));
            pending_type = t;
            mm.set_gpr(R::v0, drv::event_has_creator(t) ? kCreatorStub : 0);
        });
        m.hook(kCreatorStub, "a criadora", [this](MipsMachine& mm) {
            events.push_back({pending_type, uint32_t(mm.gpr(R::a0)) - kCarp});
        });
        // O objeto de cena falso: a tabela virtual (+0x3c) com o SetEventDynamicData original em +0x64.
        m.write32(kScene + 0x24, kOwner);
        m.write32(kScene + 0x3c, kSceneVt);
        m.write32(kSceneVt + 0x64, kFnSetEventData);
        m.write32(kScene + 0xc, kBase);
    }
};

// O CARP do porte para a RAM emulada: as palavras que apontam para dentro dele viram endereços emulados.
uint8_t* scratch_src(uint32_t ph, uint32_t i) { return nf::p32_ptr<uint8_t>(ph) + 0x40 + i * 0x40; }

struct CarpCopy {
    uint32_t port = 0, size = 0;
    uint32_t to_emu(uint32_t p) const { return (p >= port && p < port + size) ? kCarp + (p - port) : p; }
    bool in(uint32_t p) const { return p >= port && p < port + size; }
};

}  // namespace

int run_anim_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                    int cases, uint32_t seed) {
    int failures = 0;
    drv::CarModel model;
    model.set_resolve_carp(true);
    std::string err;
    if (!model.load(disc, "MIS01.VIV", "vanquish", err)) {
        std::printf("  RAnimEngine: o Vanquish não abriu: %s\n", err.c_str());
        return 1;
    }
    uint32_t inst = 0, n = 0, dmg = 0;
    if (!drv::carp_instance_list(model.carp_root(), inst, n, dmg)) {
        std::printf("  RAnimEngine: o Vanquish sem 'in#0000'\n");
        return 1;
    }
    Ctx c(elf, vu0_program, vu0_size);
    CarpCopy cc;
    cc.port = nf::p32_u(model.carp_data());
    cc.size = uint32_t(model.carp_size());
    {
        // Só os ponteiros que o motor segue (uma palavra de dados qualquer pode cair no intervalo do bloco por acaso):
        // o artigo da instância (+0x1c), os efeitos (+4) e as animações (+8) do artigo, as chaves de cada animação
        // (+0) e a lista 'el' de cada chave (+0x1c).
        std::vector<uint8_t> buf(model.carp_data(), model.carp_data() + cc.size);
        std::set<uint32_t> done;
        auto reloc = [&](uint32_t port_addr) {
            if (!cc.in(port_addr) || !done.insert(port_addr).second) return;
            uint32_t v;
            std::memcpy(&v, buf.data() + (port_addr - cc.port), 4);
            v = cc.to_emu(v);
            std::memcpy(buf.data() + (port_addr - cc.port), &v, 4);
        };
        auto rd = [](uint32_t p, uint32_t off) {
            uint32_t v;
            std::memcpy(&v, nf::p32_ptr<uint8_t>(p) + off, 4);
            return v;
        };
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t in = inst + i * 0x40;
            reloc(in + 0x1c);
            const uint32_t art = rd(in, 0x1c);
            if (!art) continue;
            reloc(art + 4);
            reloc(art + 8);
            const uint32_t anims = rd(art, 8);
            for (uint32_t a = anims; anims && nf::p32_ptr<uint8_t>(a)[0xf] != 0; a += 0x10) {
                reloc(a);
                const uint32_t keys = rd(a, 0);
                uint16_t frames;
                std::memcpy(&frames, nf::p32_ptr<uint8_t>(a) + 8, 2);
                for (uint32_t k = 0; keys && k < frames; ++k) reloc(keys + k * 0x20 + 0x1c);
            }
        }
        std::memcpy(c.m.mem(kCarp, cc.size), buf.data(), cc.size);
    }
    // Os artigos das instâncias (o Create grava +0x1a/+0x1b neles).
    std::set<uint32_t> articles;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t a;
        std::memcpy(&a, nf::p32_ptr<uint8_t>(inst + i * 0x40) + 0x1c, 4);
        if (a) articles.insert(a);
    }
    // O objeto de cena do porte (o dono físico com as bandeiras e a vaga, o BaseDesc com a caixa do dano).
    alignas(16) static uint8_t scene[0x40], owner[0x50], base[0x60];
    std::memset(scene, 0, sizeof scene);
    std::memset(owner, 0, sizeof owner);
    std::memset(base, 0, sizeof base);
    const uint32_t ps = nf::p32_u(scene), po = nf::p32_u(owner), pb = nf::p32_u(base);
    std::memcpy(scene + 0x24, &po, 4);
    std::memcpy(scene + 0xc, &pb, 4);
    Rng r(seed);
    {
        const uint16_t fl = uint16_t(r.next()), slot = uint16_t(r.range(0, 47));
        std::memcpy(owner + 0x44, &fl, 2);
        std::memcpy(owner + 0x46, &slot, 2);
        std::memcpy(c.m.mem(kOwner + 0x44, 2), &fl, 2);
        std::memcpy(c.m.mem(kOwner + 0x46, 2), &slot, 2);
        const float box[8] = {-1.0f, -0.5f, -2.5f, 0.0f, 1.0f, 0.8f, 2.4f, 0.0f};
        std::memcpy(base + 0x40, box, sizeof box);
        std::memcpy(c.m.mem(kBase + 0x40, sizeof box), box, sizeof box);
    }
    drv::anim_engine_reset(60.0f);
    {
        const uint32_t fb = ps2::fbits(drv::anim_engine().frequency);
        c.m.write32(kFrequency, fb);
        c.m.write32(kCount, 0);
        c.m.write32(kLastTick, 0);
    }
    drv::trigger_manager().pending.clear();
    std::memset(&drv::trigger_manager().event_data, 0, sizeof(drv::EventDynamicData));
    std::memset(c.m.mem(kEventData, 0x64), 0, 0x64);

    auto compare = [&](uint32_t ph, uint32_t eh, std::string& why) -> bool {
        char b[200];
        const drv::AnimHandleData& h = *nf::p32_ptr<drv::AnimHandleData>(ph);
        const uint32_t total = 0x40 + h.size;
        const uint8_t* p = nf::p32_ptr<uint8_t>(ph);
        for (uint32_t off = 0; off + 4 <= total; off += 4) {
            uint32_t pv, ev = c.m.read32(eh + off);
            std::memcpy(&pv, p + off, 4);
            // Os ponteiros conhecidos: o cabeçalho (+4 o objeto de cena, +8..+0x18 para dentro do bloco), o +0 de cada
            // sistema (o Handle) e o artigo (+0x1c) de cada cópia de instância.
            const bool header_ptr = off >= 8 && off <= 0x18;
            const bool system_ptr = off >= h.systems - ph && off < h.systems - ph + uint32_t(h.n_systems) * 0x40 &&
                                    (off - (h.systems - ph)) % 0x40 == 0;
            const bool inst_art = off >= 0x40 && off < 0x40 + uint32_t(h.n_instances) * 0x40 && (off - 0x40) % 0x40 == 0x1c;
            if (header_ptr || system_ptr) pv = pv ? eh + (pv - ph) : 0;
            else if (inst_art) pv = cc.to_emu(pv);
            else if (off == 4) pv = pv == ps ? kScene : pv;
            if (pv != ev) {
                std::snprintf(b, sizeof b, "Handle +0x%x: porte %08x original %08x", off, pv, ev);
                why = b;
                return false;
            }
        }
        for (uint32_t a : articles) {
            uint32_t pv;
            std::memcpy(&pv, nf::p32_ptr<uint8_t>(a) + 0x18, 4);
            if (pv != c.m.read32(cc.to_emu(a) + 0x18)) {
                why = "o artigo +0x18 (os identificadores do sistema)";
                return false;
            }
        }
        const drv::AnimEngineGlobals& g = drv::anim_engine();
        if (g.count != c.m.read32(kCount)) {
            std::snprintf(b, sizeof b, "a lista: porte %u original %u", g.count, c.m.read32(kCount));
            why = b;
            return false;
        }
        if (g.last_tick != c.m.read32(kLastTick)) {
            why = "o último tique";
            return false;
        }
        for (uint32_t i = 0; i < g.count; ++i) {
            const uint32_t pv = eh + (g.active[i] - ph);
            if (pv != c.m.read32(kList + i * 4)) {
                std::snprintf(b, sizeof b, "a lista [%u]", i);
                why = b;
                return false;
            }
        }
        // Os eventos (o número e os dados) e o EventDynamicData.
        const std::vector<drv::PendingEvent>& pe = drv::trigger_manager().pending;
        if (pe.size() != c.events.size()) {
            std::snprintf(b, sizeof b, "eventos: porte %zu original %zu", pe.size(), c.events.size());
            why = b;
            return false;
        }
        for (size_t i = 0; i < pe.size(); ++i) {
            if (pe[i].type != c.events[i].type || pe[i].data - cc.port != c.events[i].data) {
                std::snprintf(b, sizeof b, "evento %zu: porte %d original %d", i, pe[i].type, c.events[i].type);
                why = b;
                return false;
            }
        }
        const drv::EventDynamicData& d = drv::trigger_manager().event_data;
        const uint8_t* dp = reinterpret_cast<const uint8_t*>(&d);
        for (uint32_t off = 0; off < 0x40; off += 4) {
            uint32_t pv, ev = c.m.read32(kEventData + off);
            std::memcpy(&pv, dp + off, 4);
            if (off == 0 && pv != 0) pv = eh + (pv - ph);
            else if (off == 8) pv = pv == ps ? kScene : pv;
            if (pv != ev) {
                std::snprintf(b, sizeof b, "EventDynamicData +0x%x: porte %08x original %08x", off, pv, ev);
                why = b;
                return false;
            }
        }
        return true;
    };

    // O Create.
    uint32_t steps = 1000;
    const uint32_t ei = cc.to_emu(inst), ed = dmg ? cc.to_emu(dmg) : 0;
    c.call(kFnCreate, {n, steps, ei, ed, kScene});
    const uint32_t eh = uint32_t(c.m.gpr(R::v0));
    const uint32_t ph = drv::AnimHandle_Create(n, steps, inst, dmg, ps);
    std::string why;
    const bool created_ok = compare(ph, eh, why);
    std::printf("  RAnimEngine::Handle::Create (o Vanquish: %u instâncias, %u sistemas, %u efeitos): %s%s\n", n,
                nf::p32_ptr<drv::AnimHandleData>(ph)->n_systems, nf::p32_ptr<drv::AnimHandleData>(ph)->n_effects,
                created_ok ? "igual" : "DIFERENTE: ", why.c_str());
    failures += !created_ok;

    // As operações sorteadas.
    const drv::AnimHandleData& hd = *nf::p32_ptr<drv::AnimHandleData>(ph);
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < hd.n_systems; ++i) ids.push_back(nf::p32_ptr<uint8_t>(hd.system_ids)[i]);
    const char* kNames[] = {"ProcessStimuli", "ProcessStimuli (todos)", "Update", "Stop", "InitAllSystemStates",
                            "consultas", "EvaluateInstance"};
    uint64_t tried[7] = {}, bad[7] = {};
    std::string first;
    uint64_t events = 0;
    for (int k = 0; k < cases && created_ok; ++k) {
        drv::trigger_manager().pending.clear();
        c.events.clear();
        const double x = double(r.next() % 1000) / 1000.0;
        const int op = x < 0.36 ? 0 : x < 0.44 ? 1 : x < 0.78 ? 2 : x < 0.80 ? 3 : x < 0.82 ? 4 : x < 0.90 ? 5 : 6;
        ++tried[op];
        bool ok = true;
        switch (op) {
            case 0: {
                const uint32_t sys = r.chance(0.9) ? ids[r.next() % ids.size()] : uint32_t(r.range(0, 0x50));
                const uint32_t stim = r.chance(0.8) ? uint32_t(r.range(8, 11)) : uint32_t(r.range(0, 12));
                const uint32_t mode = uint32_t(r.range(0, 2));
                c.call(kFnStimuli, {eh, sys, stim, steps, mode});
                drv::AnimHandle_ProcessStimuli(ph, sys, stim, steps, mode);
                break;
            }
            case 1: {
                const uint32_t stim = uint32_t(r.range(1, 12)), mode = uint32_t(r.range(0, 2));
                c.call(kFnStimuliAll, {eh, stim, steps, mode});
                drv::AnimHandle_ProcessStimuliAll(ph, stim, steps, mode);
                break;
            }
            case 2:
                steps += uint32_t(r.range(0, 3));
                c.call(kFnUpdate, {steps});
                drv::RAnimEngine_Update(steps);
                break;
            case 3:
                c.call(kFnStop, {eh});
                drv::AnimHandle_Stop(ph);
                break;
            case 4: {
                const uint32_t st = uint32_t(r.range(1, 4));
                c.call(kFnInitStates, {eh, st});
                drv::AnimHandle_InitAllSystemStates(ph, uint8_t(st));
                break;
            }
            case 6: {
                // Uma cópia de instância com uma animação e um tempo sorteados (nos dois Handles), a matriz.
                const uint32_t i = uint32_t(r.range(0, int(n) - 1));
                // (numa cópia à parte: o tempo e a animação sorteados não são estados que o motor criaria)
                alignas(16) static uint8_t scratch[0x40];
                std::memcpy(scratch, nf::p32_ptr<uint8_t>(ph) + 0x40 + i * 0x40, 0x40);
                uint8_t* pc = scratch;
                const uint32_t ec = c.alloc(0x40);
                std::memcpy(c.m.mem(ec, 0x40), c.m.mem(eh + 0x40 + i * 0x40, 0x40), 0x40);
                // A animação: uma das do artigo (as que o motor põe) ou 100 em diante (a própria instância).
                uint32_t count = 0;
                {
                    uint32_t art, anims = 0;
                    std::memcpy(&art, scratch_src(ph, i) + 0x1c, 4);
                    if (art) std::memcpy(&anims, nf::p32_ptr<uint8_t>(art) + 8, 4);
                    for (uint32_t a = anims; anims && nf::p32_ptr<uint8_t>(a)[0xf] != 0; a += 0x10) ++count;
                }
                const uint8_t d = count && r.chance(0.9) ? uint8_t(r.range(0, int(count) - 1)) : uint8_t(r.range(100, 255));
                const uint16_t t = uint16_t(r.chance(0.5) ? r.range(0, 80) : r.range(0, 0xffff));
                const uint8_t fl = uint8_t(pc[0xc] ^ (r.chance(0.3) ? 0x80 : 0));
                pc[0xc] = fl;
                pc[0xd] = d;
                std::memcpy(pc + 0xe, &t, 2);
                std::memcpy(c.m.mem(ec, 0x40) + 0xc, pc + 0xc, 4);
                const uint32_t eout = c.alloc(0x40);
                c.call(kFnEvaluate, {ec, eout});
                alignas(16) float pout[4][4];
                drv::RAnimEngine_EvaluateInstance(pc, pout);
                if (std::memcmp(pout, c.m.mem(eout, 0x40), 0x40) != 0) {
                    ok = false;
                    why = "a matriz";
                }
                break;
            }
            default: {
                const uint32_t sys = r.chance(0.9) ? ids[r.next() % ids.size()] : uint32_t(r.range(0, 0x50));
                const uint32_t q = uint32_t(r.range(0, 7));
                uint32_t pv = 0;
                switch (q) {
                    case 0: c.call(kFnAnyPlaying, {eh}); pv = drv::AnimHandle_AnySystemPlaying(ph); break;
                    case 1: c.call(kFnIsPlaying, {eh, sys}); pv = drv::AnimHandle_IsSystemPlaying(ph, sys); break;
                    case 2: c.call(kFnStartTick, {eh, sys}); pv = drv::AnimHandle_GetSystemStartTick(ph, sys); break;
                    case 3: c.call(kFnMaxTicks, {eh, sys}); pv = drv::AnimHandle_GetSystemMaxTicks(ph, sys); break;
                    case 4: c.call(kFnState, {eh, sys}); pv = drv::AnimHandle_GetSystemState(ph, sys); break;
                    case 5: {
                        const uint32_t i = uint32_t(r.range(0, int(n) - 1));
                        c.call(kFnInstSysID, {eh, i});
                        pv = drv::AnimHandle_GetInstanceSystemID(ph, i);
                        break;
                    }
                    case 6: c.call(kFnExists, {eh, sys}); pv = drv::AnimHandle_GetSystemExists(ph, sys); break;
                    default: {
                        const uint32_t id = uint32_t(r.range(0, 60));
                        c.call(kFnFindEffect, {eh, id});
                        const uint32_t p = drv::AnimHandle_FindEffectByID(ph, id);
                        pv = p ? eh + (p - ph) : 0;
                        break;
                    }
                }
                if (pv != uint32_t(c.m.gpr(R::v0))) {
                    ok = false;
                    why = "a consulta " + std::to_string(q);
                }
                break;
            }
        }
        events += c.events.size();
        if (ok) ok = compare(ph, eh, why);
        if (!ok) {
            ++bad[op];
            if (first.empty()) first = std::string(kNames[op]) + " caso " + std::to_string(k) + ": " + why;
        }
    }
    for (int i = 0; i < 7; ++i) {
        std::printf("  RAnimEngine %-24s %6" PRIu64 " casos, %" PRIu64 " diferentes\n", kNames[i], tried[i], bad[i]);
        failures += int(bad[i]);
    }
    if (!first.empty()) std::printf("    primeira diferença: %s\n", first.c_str());
    if (c.errors) {
        std::printf("    o interpretador falhou %" PRIu64 " vezes: %s\n", c.errors, c.error.c_str());
        failures += int(c.errors);
    }
    const drv::AnimEngineStats& st = drv::anim_engine_stats();
    std::printf("  (o porte: %" PRIu64 " estímulos, %" PRIu64 " sistemas começados, %" PRIu64 " acabados, %" PRIu64
                " eventos das chaves; o original criou %" PRIu64 ")\n",
                st.stimuli, st.started, st.finished, st.events, events);
    drv::AnimHandle_Destroy(ph);
    drv::anim_engine_reset();
    drv::trigger_manager().pending.clear();
    return failures;
}

}  // namespace drvcheck
