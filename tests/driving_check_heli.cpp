// nf_driving_check, parte 19 (tests/driving_check_heli.h): a seção do helicóptero de Paris. O código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS (com a VU0 em modo macro) ao lado do porte, com os 19 caminhos ('pt') e os 19
// trilhos ('AISp') de Paris, caminhos montados à mão (os canais de Bézier, escalares e em degraus que Paris não tem) e
// valores sorteados:
//  - o CARP::PathInfo: GetKnotIndex (0x002dae58, com as dicas e a busca binária), EvaluateLinear (0x002db080),
//    EvaluateSpline (0x002db188, com o VU0_EulerToQuat), Evaluate (0x002db320), EvaluateMatrix (0x002db370) e
//    FindChannelIndex (0x002dadc8);
//  - o CARP::AISpline::GetApplyTransform (0x002db690) e o CARP::RotateSplineAboutBase (0x002db478);
//  - o RPathHandle: o construtor (0x001b1110), Init (0x001b1258) e sequências de Update (0x001b14b8) com o acelerador, a
//    aceleração, a espera, o andar, o caminho seguinte, o mestre e o tempo sorteados entre um passo e outro;
//  - o RPathEngine com a lista na RAM emulada: AddInstanceList (0x001b1988, com mestres), Update (0x001b2058), Reset
//    (0x001b1e58), CreatePathHandle (0x001b1c20), DestroyPathHandle (0x001b1dc0), GetPathHandle (0x001b20c8);
//  - o AISplinePath (0x0013da80, Reset 0x0013dcc0, SetNext 0x0013dd28, o destrutor 0x0013dc78) e o AIVehicle::
//    SetSplinePath/SetNextSplinePath (0x0013dfd8, 0x0013e058);
//  - os destrutores dos eventos do trilho (202 EControlToSpline, 239 EEndControlToSpline, 209 ESetNextAISpline, 108
//    EAutoDriveSpeedChange, 116 EPathSetDesiredThrottleStatic, 153 EPathSetTimeStatic, 215 EPathChangeThrottleStatic)
//    pelo carro do jogador e o seu AIGroundVehicle (as tabelas virtuais do PS2), com o AIGroundVehicle::
//    EngageSplinePath (0x00131508) e DisengageSplinePath (0x00131590) e a câmera (RPlayerCamera::SetControlToCPU).
// O que não é comparado: os ponteiros (as instâncias, os estados, os caminhos e os mestres, comparados pelo que
// apontam; o +0xd0 do RPathHandle, o "" do .data no construtor) e a matriz de aplicação de um RPathHandle recém-
// construído (lixo da pilha no PS2, que o Init reescreve).
// Fora do código original (ganchos nativos): memcpy, memset, FastAlloc/FastFree e __builtin_new/__builtin_delete (um
// alocador em faixa), cosf/sinf (o nf::fm do porte), o SetAttackMode e o SetNavigateMode do AIGroundVehicle (anotados).

#include "driving_check_heli.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/carp_runtime.h"
#include "driving/game/bond_car.h"
#include "driving/game/rail.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/render/path_engine.h"
#include "driving/track.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções originais.
constexpr uint32_t kFnFindChannel = 0x2dadc8, kFnKnot = 0x2dae58, kFnEvalLinear = 0x2db080,
                   kFnEvalSpline = 0x2db188, kFnEval = 0x2db320, kFnEvalMatrix = 0x2db370;
constexpr uint32_t kFnRotateAboutBase = 0x2db478, kFnApplyTransform = 0x2db690;
constexpr uint32_t kFnHandleCtor = 0x1b1110, kFnHandleInit = 0x1b1258, kFnHandleUpdate = 0x1b14b8;
constexpr uint32_t kFnEngineAdd = 0x1b1988, kFnEngineUpdate = 0x1b2058, kFnEngineReset = 0x1b1e58,
                   kFnEngineCreate = 0x1b1c20, kFnEngineDestroy = 0x1b1dc0, kFnEngineGet = 0x1b20c8;
constexpr uint32_t kFnSplinePathCtor = 0x13da80, kFnSplinePathReset = 0x13dcc0, kFnSplinePathNext = 0x13dd28,
                   kFnSplinePathDtor = 0x13dc78, kFnSetSplinePath = 0x13dfd8, kFnSetNextSplinePath = 0x13e058;
// Os destrutores dos eventos.
constexpr uint32_t kFnEvControlToSpline = 0x14ae60, kFnEvEndControlToSpline = 0x14c110, kFnEvNextAISpline = 0x158370,
                   kFnEvSpeedChange = 0x1482f8, kFnEvDesiredThrottle = 0x152cb0, kFnEvSetTime = 0x153340,
                   kFnEvChangeThrottle = 0x151dc0;
// Os globais.
constexpr uint32_t kRate = 0x333ff0, kFrequency = 0x333ff4;  // RTimeData
constexpr uint32_t kPathList = 0x3339a8;                      // RPathEngine::fgPathHandles
constexpr uint32_t kSim = 0x339190, kSimSteps = 0x339f54, kSimCars = 0x339f5c;
constexpr uint32_t kWorldCameras = 0x333ed8;
constexpr uint32_t kBondCarVtable = 0x379ef0, kGroundVehicleVtable = 0x36b568;

// A RAM emulada (entre o fim do ELF, 0x4279b4, e o mundo de colisão das outras partes).
constexpr uint32_t kPaths = 0x00500000;     // os 'pt' de Paris e os montados à mão
constexpr uint32_t kSplines = 0x00600000;   // os 'AISp' de Paris
constexpr uint32_t kInst = 0x00610000;      // instâncias e estados de teste
constexpr uint32_t kHandle = 0x00620000;    // RPathHandle de teste
constexpr uint32_t kScratch = 0x00630000;   // entradas e saídas
constexpr uint32_t kCar = 0x00640000;       // o carro do jogador (PBondCar)
constexpr uint32_t kGv = 0x00641000;        // o seu AIGroundVehicle
constexpr uint32_t kCarList = 0x00642000;   // o vetor de carros da Simulation (+0xdcc)
constexpr uint32_t kCamera = 0x00643000;    // a câmera do jogador (o RPlayerCamera, só os campos lidos)
constexpr uint32_t kCamState = 0x00644000;  // o seu RPlayerCamState
constexpr uint32_t kCamList = 0x00645000;   // a lista das câmeras do mundo
constexpr uint32_t kEvent = 0x00646000;     // o objeto do evento
constexpr uint32_t kPhysics = 0x00647000;   // o CarPhysics
constexpr uint32_t kHeapBegin = 0x00700000, kHeapEnd = 0x00f00000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1919) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    float uni(float a, float b) { return a + (b - a) * float(next() & 0xffffff) / float(0x1000000); }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", v);
    return b;
}

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    if (t.bad) ++failures;
}

// Compara n bytes palavra a palavra, pulando as faixas [a, b); o primeiro diferente em `out`.
bool same_words(const char* what, const uint8_t* emu, const void* port, uint32_t n,
                std::initializer_list<std::pair<uint32_t, uint32_t>> skip, std::string& out) {
    const uint8_t* p = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
        bool skipped = false;
        for (const auto& s : skip)
            if (o >= s.first && o < s.second) skipped = true;
        if (skipped) continue;
        uint32_t x, y;
        std::memcpy(&x, emu + o, 4);
        std::memcpy(&y, p + o, 4);
        if (x != y) {
            out = std::string(what) + " +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

uint32_t fb(float f) { return ps2::fbits(f); }

class HeliEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    std::vector<std::string> calls;  // os ganchos anotados (SetAttackMode, SetNavigateMode)

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(0x2a7e84, "memcpy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2a8038, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("a área de alocação acabou", heap, n);
            std::memset(mm.mem(heap, n), 0xcd, n);  // lixo: o que não é escrito aparece
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x2d0910, "__builtin_new", alloc);
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(0x12e6f8, "AIGroundVehicle::SetAttackMode", [this](MipsMachine& mm) {
            calls.push_back("SetAttackMode " + std::to_string(int32_t(mm.gpr(R::a1))));
        });
        m.hook(0x12e468, "AIGroundVehicle::SetNavigateMode", [this](MipsMachine& mm) {
            calls.push_back("SetNavigateMode " + std::to_string(int32_t(mm.gpr(R::a1))) + " " +
                            hex(uint32_t(mm.gpr(R::a2))));
        });
        // O relógio do RTimeData (o Init com a taxa do NTSC).
        const drv::PathEngineClock& c = drv::path_engine_clock();
        put_f(kRate, c.rate);
        put_f(kFrequency, c.frequency);
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0.0f) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.set_fpr(12, f12);
        m.call(fn, args, 50'000'000);
    }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
    void put_f(uint32_t a, float f) { put(a, &f, 4); }
    float get_f(uint32_t a) {
        float f;
        std::memcpy(&f, m.mem(a, 4), 4);
        return f;
    }
    uint32_t alloc(uint32_t n) {
        const uint32_t a = heap;
        heap += (n + 15) & ~15u;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
};

// Um caminho do port e o seu endereço emulado.
struct PathRef {
    const drv::PathInfo* port = nullptr;
    uint32_t emu = 0;
    uint32_t size = 0;
};

// Os caminhos montados à mão: canais de cada tipo (linear 4, quatérnio, escalar, degrau, Bézier de posição e de
// rotação em Euler), com tempos crescentes sorteados.
std::vector<uint8_t> make_path(Rng& rng, const std::vector<uint32_t>& flags, int keys, float duration) {
    const uint32_t nch = uint32_t(flags.size());
    std::vector<uint8_t> b(0x40 + 0x14 * nch, 0);
    auto align = [&]() {
        while (b.size() & 15) b.push_back(0);
    };
    auto w32 = [&](uint32_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    w32(0, fb(duration));
    w32(4, nch);
    w32(0xc, uint32_t(-1));
    w32(0x10, uint32_t(-1));
    for (uint32_t c = 0; c < nch; ++c) {
        const uint32_t base = 0x40 + 0x14 * c;
        // O nome.
        align();
        const uint32_t name = uint32_t(b.size());
        const char* nm = c == 0 ? "xform" : c == 1 ? "XLATE" : "extra";
        for (const char* p = nm; *p; ++p) b.push_back(uint8_t(*p));
        b.push_back(0);
        align();
        const uint32_t times = uint32_t(b.size());
        float t = rng.uni(-2.0f, 2.0f);
        for (int k = 0; k < keys; ++k) {
            const uint32_t v = fb(t);
            for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
            t += rng.chance(0.1) ? 0.0f : rng.uni(0.5f, duration / float(keys));
        }
        align();
        const uint32_t keydata = uint32_t(b.size());
        const uint32_t fl = flags[c];
        const int floats = (fl & drv::kPathSpline) ? 12 : (fl & drv::kPathScalar) ? 1 : 4;
        for (int k = 0; k < keys * floats; ++k) {
            float v = rng.uni(-50.0f, 50.0f);
            if ((fl & drv::kPathRotation) && !(fl & drv::kPathSpline)) v = rng.uni(-1.0f, 1.0f);
            if ((fl & drv::kPathRotation) && (fl & drv::kPathSpline)) v = rng.uni(-1.5f, 1.5f);
            const uint32_t x = fb(v);
            for (int i = 0; i < 4; ++i) b.push_back(uint8_t(x >> (8 * i)));
        }
        w32(base + 0, name);
        w32(base + 4, times);
        w32(base + 8, keydata);
        w32(base + 0xc, uint32_t(keys));
        w32(base + 0x10, fl);
    }
    // O primeiro canal de rotação e o primeiro de posição (sem degrau: o EvaluateMatrix lê as duas partes).
    for (uint32_t c = 0; c < nch; ++c) {
        int32_t r, p;
        std::memcpy(&r, b.data() + 0xc, 4);
        std::memcpy(&p, b.data() + 0x10, 4);
        if ((flags[c] & drv::kPathRotation) && r == -1) w32(0xc, c);
        else if (!(flags[c] & drv::kPathRotation) && !(flags[c] & drv::kPathScalar) && p == -1) w32(0x10, c);
    }
    align();
    return b;
}

// Um tempo para um canal: dentro, nas pontas, nas chaves exatas, fora (os em degrau só entre 0 e as chaves).
float pick_time(Rng& rng, const drv::PathInfo* p, const drv::PathChannel* c) {
    const float* times = reinterpret_cast<const float*>(p->at(c->times));
    const uint32_t n = c->key_count;
    if (c->flags & drv::kPathStepped) return rng.uni(0.0f, float(n) - 1.001f);
    switch (rng.range(0, 5)) {
        case 0: return times[rng.range(0, int(n) - 1)];
        case 1: return rng.uni(times[0] - 5.0f, times[0]);
        case 2: return rng.uni(times[n - 1], times[n - 1] + 5.0f);
        default: return rng.uni(times[0], times[n - 1]);
    }
}

// ---- A: o CARP::PathInfo -----------------------------------------------------------------------------------------

void check_path_info(HeliEmu& e, Rng& rng, const std::vector<PathRef>& paths, int cases, int& failures) {
    Tally knot, linear, spline, eval, matrix, find;
    for (int i = 0; i < cases; ++i) {
        const PathRef& pr = paths[size_t(rng.range(0, int(paths.size()) - 1))];
        const drv::PathInfo* p = pr.port;
        const uint32_t ci = uint32_t(rng.range(0, int(p->channel_count) - 1));
        const drv::PathChannel* c = p->channel(ci);
        const uint32_t cemu = pr.emu + 0x40 + 0x14 * ci;
        const float t = pick_time(rng, p, c);
        const uint32_t hint = rng.chance(0.2) ? rng.next() % (c->key_count + 3) : uint32_t(rng.range(0, int(c->key_count) - 1));
        // GetKnotIndex
        {
            ++knot.cases;
            e.call(kFnKnot, {pr.emu, hint, cemu}, t);
            const uint32_t a = uint32_t(e.m.gpr(R::v0));
            const uint32_t b = drv::PathInfo_GetKnotIndex(p, t, hint, c);
            if (a != b) knot.fail("t " + std::to_string(t) + ": original " + std::to_string(a) + ", porte " + std::to_string(b));
        }
        // EvaluateLinear / EvaluateSpline (o de cada canal) e Evaluate
        {
            float out[4] = {};
            const uint32_t fn = (c->flags & drv::kPathSpline) ? kFnEvalSpline : kFnEvalLinear;
            Tally& tl = (c->flags & drv::kPathSpline) ? spline : linear;
            ++tl.cases;
            std::memset(e.m.mem(kScratch, 16), 0, 16);
            e.call(fn, {pr.emu, hint, cemu, kScratch}, t);
            const uint32_t a = uint32_t(e.m.gpr(R::v0));
            const uint32_t b = (c->flags & drv::kPathSpline) ? drv::PathInfo_EvaluateSpline(p, t, hint, c, out)
                                                            : drv::PathInfo_EvaluateLinear(p, t, hint, c, out);
            std::string what;
            if (a != b) tl.fail("chave: original " + std::to_string(a) + ", porte " + std::to_string(b));
            else if (!same_words("valor", e.m.mem(kScratch, 16), out, 16, {}, what)) tl.fail(what);
            ++eval.cases;
            float out2[4] = {};
            std::memset(e.m.mem(kScratch, 16), 0, 16);
            e.call(kFnEval, {pr.emu, ci, hint, kScratch}, t);
            const uint32_t a2 = uint32_t(e.m.gpr(R::v0));
            const uint32_t b2 = drv::PathInfo_Evaluate(p, ci, t, hint, out2);
            if (a2 != b2) eval.fail("chave");
            else if (!same_words("Evaluate", e.m.mem(kScratch, 16), out2, 16, {}, what)) eval.fail(what);
        }
        // EvaluateMatrix (uma matriz de entrada sorteada: o canal que falta deixa a sua parte)
        if (p->position_channel != -1 || p->rotation_channel != -1) {
            bool stepped = false;
            if (p->position_channel != -1 && (p->channel(uint32_t(p->position_channel))->flags & drv::kPathStepped))
                stepped = true;
            if (p->rotation_channel != -1 && (p->channel(uint32_t(p->rotation_channel))->flags & drv::kPathStepped))
                stepped = true;
            const drv::PathChannel* tc = p->channel(uint32_t(
                p->position_channel != -1 ? p->position_channel : p->rotation_channel));
            const float tm = stepped ? rng.uni(0.0f, 1.0f) : pick_time(rng, p, tc);
            ++matrix.cases;
            float m[4][4];
            for (auto& r : m)
                for (float& x : r) x = rng.uni(-3.0f, 3.0f);
            uint32_t pk = rng.next() % 4, rk = rng.next() % 4;
            float w = 7.0f;
            e.put(kScratch, m, 64);
            e.m.write32(kScratch + 0x40, pk);
            e.m.write32(kScratch + 0x44, rk);
            e.put_f(kScratch + 0x48, w);
            e.call(kFnEvalMatrix, {pr.emu, kScratch + 0x40, kScratch + 0x44, kScratch, kScratch + 0x48}, tm);
            drv::PathInfo_EvaluateMatrix(p, tm, pk, rk, m, w);
            std::string what;
            if (!same_words("matriz", e.m.mem(kScratch, 64), m, 64, {}, what)) matrix.fail(what);
            else if (e.m.read32(kScratch + 0x40) != pk || e.m.read32(kScratch + 0x44) != rk) matrix.fail("as chaves");
            else if (e.m.read32(kScratch + 0x48) != fb(w)) matrix.fail("o peso");
        }
        // FindChannelIndex
        {
            ++find.cases;
            const char* names[] = {"xform", "XFORM", "xlate", "Xlate", "extra", "nada"};
            const char* nm = names[rng.range(0, 5)];
            std::memcpy(e.m.mem(kScratch + 0x100, 8), nm, std::strlen(nm) + 1);
            e.call(kFnFindChannel, {pr.emu, kScratch + 0x100});
            const uint32_t a = uint32_t(e.m.gpr(R::v0));
            const uint32_t b = drv::PathInfo_FindChannelIndex(p, nm);
            if (a != b) find.fail(std::string(nm) + ": original " + std::to_string(a) + ", porte " + std::to_string(b));
        }
    }
    report("PathInfo::GetKnotIndex (dicas e busca)", knot, failures);
    report("PathInfo::EvaluateLinear (linear, slerp, escalar, degrau)", linear, failures);
    report("PathInfo::EvaluateSpline (Bézier; rotação pelo VU0_EulerToQuat)", spline, failures);
    report("PathInfo::Evaluate", eval, failures);
    report("PathInfo::EvaluateMatrix", matrix, failures);
    report("PathInfo::FindChannelIndex", find, failures);
}

// ---- C: o CARP::AISpline -------------------------------------------------------------------------------------------

void check_splines(HeliEmu& e, Rng& rng, const std::vector<const drv::AISplineRec*>& splines,
                   const std::map<uint32_t, uint32_t>& path_emu, int cases, int& failures) {
    Tally apply, rotate;
    for (int i = 0; i < cases; ++i) {
        // GetApplyTransform num 'AISp' de Paris com a colocação e o "desligado" sorteados (o caminho de Paris tem os
        // dois canais: a matriz da pilha é toda escrita).
        drv::AISplineRec s = *splines[size_t(rng.range(0, int(splines.size()) - 1))];
        if (rng.chance(0.7)) s.disabled = 0;
        if (rng.chance(0.1)) s.path = 0;
        for (auto& r : s.matrix)
            for (float& x : r) x = rng.uni(-1.0f, 1.0f);
        for (int k = 0; k < 3; ++k) s.matrix[3][k] = rng.uni(-1000.0f, 1000.0f);
        drv::AISplineRec se = s;
        if (s.path != 0) se.path = path_emu.at(s.path);
        e.put(kScratch, &se, sizeof se);
        float out[4][4];
        std::memset(e.m.mem(kScratch + 0x100, 64), 0, 64);
        ++apply.cases;
        e.call(kFnApplyTransform, {kScratch, kScratch + 0x100});
        drv::AISpline_GetApplyTransform(&s, out);
        std::string what;
        if (!same_words("GetApplyTransform", e.m.mem(kScratch + 0x100, 64), out, 64, {}, what)) apply.fail(what);
        // RotateSplineAboutBase com três matrizes sorteadas.
        float base[4][4], ev[4][4], sp[4][4], o2[4][4];
        for (float(*mm)[4] : {base, ev, sp})
            for (int r = 0; r < 4; ++r)
                for (int k = 0; k < 4; ++k) mm[r][k] = rng.uni(r == 3 ? -500.0f : -1.0f, r == 3 ? 500.0f : 1.0f);
        e.put(kScratch + 0x200, base, 64);
        e.put(kScratch + 0x240, ev, 64);
        e.put(kScratch + 0x280, sp, 64);
        std::memset(e.m.mem(kScratch + 0x2c0, 64), 0, 64);
        ++rotate.cases;
        e.call(kFnRotateAboutBase, {kScratch + 0x200, kScratch + 0x240, kScratch + 0x280, kScratch + 0x2c0});
        std::memset(o2, 0, sizeof o2);
        drv::CARP_RotateSplineAboutBase(base, ev, sp, o2);
        if (!same_words("RotateSplineAboutBase", e.m.mem(kScratch + 0x2c0, 64), o2, 64, {}, what)) rotate.fail(what);
    }
    report("AISpline::GetApplyTransform (os 'AISp' de Paris)", apply, failures);
    report("RotateSplineAboutBase", rotate, failures);
}

// ---- B: o RPathHandle ----------------------------------------------------------------------------------------------

// O RPathHandle emulado contra o do porte (os ponteiros pelo que apontam).
bool same_handle(HeliEmu& e, uint32_t he, const drv::RPathHandle& hp, const std::map<uint32_t, uint32_t>& path_emu,
                 uint32_t master_emu, bool apply, std::string& what) {
    const uint8_t* eb = e.m.mem(he, 0xd4);
    if (!same_words("RPathHandle", eb, &hp, 0xd4,
                    {{0x00, 0x08}, {0x98, 0xa4}, {0xd0, 0xd4}, {apply ? 0u : 0x48u, apply ? 0u : 0x88u}}, what))
        return false;
    auto ptr = [&](uint32_t off, uint32_t port) {
        const uint32_t ev = e.m.read32(he + off);
        const uint32_t want = port == 0 ? 0 : (path_emu.count(port) ? path_emu.at(port) : 0xffffffffu);
        return ev == want;
    };
    if (!ptr(0x98, hp.current)) {
        what = "o caminho corrente";
        return false;
    }
    if (!ptr(0x9c, hp.next)) {
        what = "o caminho seguinte";
        return false;
    }
    if ((e.m.read32(he + 0xa0) != 0) != (hp.master != 0) || (hp.master != 0 && e.m.read32(he + 0xa0) != master_emu)) {
        what = "o mestre";
        return false;
    }
    return true;
}

void check_handles(HeliEmu& e, Rng& rng, const std::vector<PathRef>& paths, const std::map<uint32_t, uint32_t>& path_emu,
                   int cases, int& failures) {
    Tally ctor, init, update;
    // A instância e o estado do porte (memória do porte, p32) e os emulados.
    alignas(16) static uint8_t inst_port[0x40];
    alignas(16) static drv::ProcAnimState st_port;
    alignas(16) static uint8_t master_inst[0x40];
    static drv::RPathHandle master_port;
    // Os RPathHandle emulados num endereço 8 mod 16, como o nó da lista (+8 de um bloco do FastAlloc): o Init grava a
    // matriz de aplicação com sq/sqc2, que ignoram os 4 bits de baixo do endereço.
    const uint32_t inst_emu = kInst, st_emu = kInst + 0x40, he = kHandle + 8, master_he = kHandle + 0x108;
    for (int i = 0; i < cases / 4; ++i) {
        const PathRef& pr = paths[size_t(rng.range(0, int(paths.size()) - 1))];
        // Só os caminhos com os dois canais e sem degrau (o EvaluateMatrix do Update não leria além das chaves).
        bool ok = pr.port->position_channel != -1 && pr.port->rotation_channel != -1;
        for (uint32_t c = 0; ok && c < pr.port->channel_count; ++c)
            if (pr.port->channel(c)->flags & drv::kPathStepped) ok = false;
        if (!ok) continue;
        for (int k = 0; k < 16; ++k) {
            const float v = rng.uni(k >= 12 ? -500.0f : -1.0f, k >= 12 ? 500.0f : 1.0f);
            std::memcpy(inst_port + 4 * k, &v, 4);
        }
        inst_port[0xc] = 0x10;
        std::memset(&st_port, 0, sizeof st_port);
        st_port.type = 2;
        st_port.flags = rng.chance(0.8) ? 0x8000u : 0u;
        st_port.path = nf::p32_u(pr.port);
        st_port.start_time = rng.chance(0.3) ? 0.0f : rng.uni(-10.0f, pr.port->duration + 10.0f);
        st_port.speed = rng.uni(0.0f, 3.0f);
        st_port.identity_axes = uint8_t(rng.chance(0.5) ? 1 : 0);
        drv::ProcAnimState st_emu_copy = st_port;
        st_emu_copy.path = pr.emu;
        e.put(inst_emu, inst_port, 0x40);
        e.put(st_emu, &st_emu_copy, 0x20);
        // O construtor e o Init.
        std::memset(e.m.mem(he, 0xd4), 0xcd, 0xd4);
        ++ctor.cases;
        e.call(kFnHandleCtor, {he, inst_emu, st_emu});
        drv::RPathHandle hp;
        drv::RPathHandle_Construct(hp, nf::p32_u(inst_port), nf::p32_u(&st_port));
        std::string what;
        if (!same_handle(e, he, hp, path_emu, 0, false, what)) ctor.fail(what);
        float now = float(rng.range(0, 100000));
        ++init.cases;
        e.call(kFnHandleInit, {he}, now);
        drv::RPathHandle_Init(hp, now);
        if (!same_handle(e, he, hp, path_emu, 0, true, what)) init.fail(what);
        else if (!same_words("instância", e.m.mem(inst_emu, 0x40), inst_port, 0x40, {}, what)) init.fail(what);
        // Um mestre às vezes: outro RPathHandle no mesmo caminho, com o tempo à frente (o +0xa0 e o atraso).
        bool with_master = rng.chance(0.2);
        if (with_master) {
            std::memcpy(master_inst, inst_port, 0x40);
            drv::RPathHandle_Construct(master_port, nf::p32_u(master_inst), nf::p32_u(&st_port));
            drv::RPathHandle_Init(master_port, now);
            master_port.time = rng.uni(0.0f, pr.port->duration);
            drv::RPathHandle mcopy = master_port;
            mcopy.instance = inst_emu + 0x80;
            mcopy.state = st_emu;
            mcopy.current = pr.emu;
            mcopy.next = pr.emu;
            e.put(master_he, &mcopy, 0xd4);
            hp.master = nf::p32_u(&master_port);
            hp.delta = rng.uni(-5.0f, 5.0f);
            e.m.write32(he + 0xa0, master_he);
            e.put_f(he + 0xa8, hp.delta);
        }
        // Os passos.
        const int steps = rng.range(1, 40);
        for (int s = 0; s < steps; ++s) {
            // Mexe no estado entre os passos.
            if (rng.chance(0.15)) hp.throttle = rng.chance(0.3) ? 0.0f : rng.uni(-0.5f, 2.0f);
            if (rng.chance(0.15)) hp.desired = rng.chance(0.3) ? hp.throttle : rng.uni(-0.5f, 2.0f);
            if (rng.chance(0.15)) hp.accel = rng.uni(0.0f, 0.5f);
            if (rng.chance(0.1)) hp.accel_delay = rng.range(0, 5);
            if (rng.chance(0.05)) hp.running = rng.chance(0.8) ? 1 : 0;
            if (rng.chance(0.08)) hp.time = rng.uni(-20.0f, pr.port->duration + 20.0f);
            if (rng.chance(0.08)) {
                const PathRef& nx = paths[size_t(rng.range(0, int(paths.size()) - 1))];
                bool okn = nx.port->position_channel != -1 && nx.port->rotation_channel != -1;
                for (uint32_t c = 0; okn && c < nx.port->channel_count; ++c)
                    if (nx.port->channel(c)->flags & drv::kPathStepped) okn = false;
                hp.next = rng.chance(0.3) || !okn ? 0 : nf::p32_u(nx.port);
            }
            if (rng.chance(0.03)) hp.current = 0;
            if (with_master && rng.chance(0.1)) {
                master_port.time = rng.uni(-5.0f, pr.port->duration + 5.0f);
                e.put_f(master_he + 0xac, master_port.time);
            }
            // O mesmo no emulado (os campos não ponteiros, o corrente e o seguinte).
            {
                const uint8_t* pb = reinterpret_cast<const uint8_t*>(&hp);
                e.put(he + 0x88, pb + 0x88, 0x10);
                e.put(he + 0xa4, pb + 0xa4, 0xd0 - 0xa4);
                e.m.write32(he + 0x98, hp.current ? path_emu.at(hp.current) : 0);
                e.m.write32(he + 0x9c, hp.next ? path_emu.at(hp.next) : 0);
            }
            const int dt = rng.chance(0.1) ? rng.range(-2, 0) : rng.chance(0.1) ? rng.range(2, 50) : 1;
            now += float(dt);
            ++update.cases;
            e.call(kFnHandleUpdate, {he}, now);
            drv::RPathHandle_Update(hp, now);
            if (!same_handle(e, he, hp, path_emu, master_he, true, what)) {
                update.fail(what);
                break;
            }
            if (!same_words("instância", e.m.mem(inst_emu, 0x40), inst_port, 0x40, {}, what)) {
                update.fail(what);
                break;
            }
        }
    }
    report("RPathHandle::RPathHandle (a instância e o estado)", ctor, failures);
    report("RPathHandle::Init", init, failures);
    report("RPathHandle::Update (acelerador, espera, fim, seguinte, mestre)", update, failures);
}

// ---- D: o RPathEngine e o AISplinePath, com a lista na RAM emulada ------------------------------------------------

// Os nós da lista emulada (o fgPathHandles: o ponteiro para a cabeça; cada nó {next, prev, RPathHandle}).
std::vector<uint32_t> emu_list(HeliEmu& e) {
    std::vector<uint32_t> out;
    const uint32_t lp = e.m.read32(kPathList);
    if (lp == 0) return out;
    const uint32_t head = e.m.read32(lp);
    for (uint32_t n = e.m.read32(head); n != head && out.size() < 10000; n = e.m.read32(n)) out.push_back(n + 8);
    return out;
}

void check_engine(HeliEmu& e, Rng& rng, const std::vector<PathRef>& paths, const std::map<uint32_t, uint32_t>& path_emu,
                  const std::vector<const drv::AISplineRec*>& splines, int cases, int& failures) {
    Tally add, upd, reset, create, destroy, get, spath, sreset, snext, sset;
    std::vector<PathRef> full;
    for (const PathRef& p : paths) {
        bool ok = p.port->position_channel != -1 && p.port->rotation_channel != -1;
        for (uint32_t c = 0; ok && c < p.port->channel_count; ++c)
            if (p.port->channel(c)->flags & drv::kPathStepped) ok = false;
        if (ok) full.push_back(p);
    }
    for (int round = 0; round < std::max(1, cases / 200); ++round) {
        drv::RPathEngine_Purge();
        e.m.write32(kPathList, 0);
        e.heap = kHeapBegin;
        drv::simulation().steps = rng.range(0, 20000);
        e.m.write32(kSimSteps, uint32_t(drv::simulation().steps));
        // AddInstanceList: n instâncias (algumas sem a bandeira, algumas de outro tipo, algumas com mestre).
        const int n = rng.range(1, 12);
        static std::vector<uint8_t> inst_port, st_port;
        inst_port.assign(0x40 * size_t(n), 0);
        st_port.assign(0x20 * size_t(n), 0);
        std::vector<uint8_t> st_emu(0x20 * size_t(n), 0);
        for (int i = 0; i < n; ++i) {
            uint8_t* ip = inst_port.data() + 0x40 * i;
            for (int k = 0; k < 16; ++k) {
                const float v = rng.uni(k >= 12 ? -500.0f : -1.0f, k >= 12 ? 500.0f : 1.0f);
                std::memcpy(ip + 4 * k, &v, 4);
            }
            ip[0xc] = uint8_t(rng.chance(0.85) ? 0x10 : 0x00);
            const uint16_t si = uint16_t(i);
            std::memcpy(ip + 0xe, &si, 2);
            std::memset(ip + 0x1c, 0, 4);  // sem artigo
            drv::ProcAnimState s{};
            s.type = uint8_t(rng.chance(0.85) ? 2 : 1);
            s.flags = rng.chance(0.8) ? 0x8000u : 0u;
            const PathRef& pr = full[size_t(rng.range(0, int(full.size()) - 1))];
            s.path = rng.chance(0.9) ? nf::p32_u(pr.port) : 0;
            s.start_time = rng.uni(0.0f, pr.port->duration);
            s.speed = rng.uni(0.1f, 2.0f);
            s.identity_axes = uint8_t(rng.chance(0.5));
            if (i > 0 && rng.chance(0.3)) s.master = nf::p32_u(inst_port.data() + 0x40 * rng.range(0, i - 1));
            std::memcpy(st_port.data() + 0x20 * i, &s, 0x20);
            drv::ProcAnimState se = s;
            se.path = s.path ? path_emu.at(s.path) : 0;
            if (s.master) se.master = kInst + uint32_t(nf::p32_ptr<uint8_t>(s.master) - inst_port.data());
            std::memcpy(st_emu.data() + 0x20 * i, &se, 0x20);
        }
        e.put(kInst, inst_port.data(), uint32_t(inst_port.size()));
        const uint32_t st_base = kInst + 0x1000;
        e.put(st_base, st_emu.data(), uint32_t(st_emu.size()));
        const float now0 = ps2::ee::cvt_s(drv::simulation().steps);
        ++add.cases;
        e.call(kFnEngineAdd, {kInst, st_base, uint32_t(n)}, now0);
        drv::RPathEngine_AddInstanceList(nf::p32_u(inst_port.data()), nf::p32_u(st_port.data()), uint32_t(n), now0);
        // A lista: na mesma ordem, cada nó igual.
        auto compare_lists = [&](Tally& t) {
            const std::vector<uint32_t> le = emu_list(e);
            const std::vector<drv::RPathHandle*>& lp = drv::path_engine_handles();
            if (le.size() != lp.size()) {
                t.fail("a lista: original " + std::to_string(le.size()) + ", porte " + std::to_string(lp.size()));
                return false;
            }
            for (size_t k = 0; k < le.size(); ++k) {
                const drv::RPathHandle& hp = *lp[k];
                // O mestre emulado: o nó do porte na mesma posição.
                uint32_t master_emu = 0;
                for (size_t j = 0; j < lp.size(); ++j)
                    if (nf::p32_u(lp[j]) == hp.master) master_emu = le[j];
                std::string what;
                if (!same_handle(e, le[k], hp, path_emu, master_emu, true, what)) {
                    t.fail("nó " + std::to_string(k) + ": " + what);
                    return false;
                }
                if (hp.instance != 0 && nf::p32_ptr<uint8_t>(hp.instance) >= inst_port.data() &&
                    nf::p32_ptr<uint8_t>(hp.instance) < inst_port.data() + inst_port.size()) {
                    const uint32_t off = uint32_t(nf::p32_ptr<uint8_t>(hp.instance) - inst_port.data());
                    if (e.m.read32(le[k]) != kInst + off) {
                        t.fail("nó " + std::to_string(k) + ": a instância");
                        return false;
                    }
                }
            }
            std::string what2;
            if (!same_words("instâncias", e.m.mem(kInst, uint32_t(inst_port.size())), inst_port.data(),
                            uint32_t(inst_port.size()), {}, what2)) {
                t.fail("as instâncias movidas");
                return false;
            }
            return true;
        };
        compare_lists(add);
        // Os Update e um Reset.
        float now = now0;
        for (int s = 0; s < 30; ++s) {
            now += rng.chance(0.1) ? 0.0f : 1.0f;
            ++upd.cases;
            e.call(kFnEngineUpdate, {}, now);
            drv::RPathEngine_Update(now);
            if (!compare_lists(upd)) break;
        }
        ++reset.cases;
        e.call(kFnEngineReset, {}, now);
        drv::RPathEngine_Reset(now);
        compare_lists(reset);
        // GetPathHandle de cada instância.
        for (int i = 0; i < n; ++i) {
            ++get.cases;
            e.call(kFnEngineGet, {kInst + 0x40u * uint32_t(i)});
            const uint32_t ge = uint32_t(e.m.gpr(R::v0));
            const drv::RPathHandle* gp = drv::RPathEngine_GetPathHandle(nf::p32_u(inst_port.data() + 0x40 * i));
            const std::vector<uint32_t> le = emu_list(e);
            const std::vector<drv::RPathHandle*>& lp = drv::path_engine_handles();
            size_t ie = le.size(), ip = lp.size();
            for (size_t k = 0; k < le.size(); ++k)
                if (le[k] == ge) ie = k;
            for (size_t k = 0; k < lp.size(); ++k)
                if (lp[k] == gp) ip = k;
            if (ie != ip) get.fail("instância " + std::to_string(i));
        }
        // O AISplinePath num 'AISp' de Paris (a matriz e o "desligado" sorteados), com Reset, SetNext e o destrutor.
        {
            drv::AISplineRec s = *splines[size_t(rng.range(0, int(splines.size()) - 1))];
            if (rng.chance(0.5)) s.disabled = 0;
            drv::AISplineRec se = s;
            se.path = path_emu.at(s.path);
            e.put(kSplines + 0x800, &se, sizeof se);
            const uint32_t spe = e.alloc(0xb0);
            std::memset(e.m.mem(spe, 0xb0), 0xcd, 0xb0);
            static drv::AISplinePath spp;
            static drv::AISplineRec sport;
            sport = s;
            ++spath.cases;
            e.call(kFnSplinePathCtor, {spe, kSplines + 0x800});
            drv::AISplinePath_Construct(spp, &sport);
            uint32_t name_emu = kSplines + 0x800 + 0x44;
            auto same_sp = [&](Tally& t, const char* stage) {
                std::string what;
                if (!same_words(stage, e.m.mem(spe, 0xb0), &spp, 0xb0, {{0x4c, 0x50}, {0x60, 0x70}}, what)) {
                    t.fail(what);
                    return;
                }
                if (e.m.read32(spe + 0x4c) != path_emu.at(spp.state.path)) {
                    t.fail(std::string(stage) + ": o caminho do estado");
                    return;
                }
                compare_lists(t);
                const uint32_t hemu = e.m.read32(spe + 0x60);
                const drv::RPathHandle* hp = nf::p32_ptr<const drv::RPathHandle>(spp.handle);
                if (hemu == 0 || !hp) {
                    if ((hemu == 0) != (hp == nullptr)) t.fail(std::string(stage) + ": o RPathHandle");
                    return;
                }
                if (e.m.read32(hemu + 0xd0) != name_emu || hp->user != nf::p32_u(sport.name))
                    t.fail(std::string(stage) + ": o nome no +0xd0");
            };
            same_sp(spath, "AISplinePath");
            for (int s2 = 0; s2 < 10; ++s2) {
                now += 1.0f;
                e.call(kFnEngineUpdate, {}, now);
                drv::RPathEngine_Update(now);
            }
            drv::simulation().steps += 10;
            e.m.write32(kSimSteps, uint32_t(drv::simulation().steps));
            // Reset com outro 'AISp'.
            drv::AISplineRec s2r = *splines[size_t(rng.range(0, int(splines.size()) - 1))];
            if (rng.chance(0.5)) s2r.disabled = 0;
            drv::AISplineRec s2e = s2r;
            s2e.path = path_emu.at(s2r.path);
            e.put(kSplines + 0x880, &s2e, sizeof s2e);
            sport = s2r;
            name_emu = kSplines + 0x880 + 0x44;
            ++sreset.cases;
            e.call(kFnSplinePathReset, {spe, kSplines + 0x880});
            drv::AISplinePath_Reset(spp, &sport);
            same_sp(sreset, "AISplinePath::Reset");
            // SetNext com outro.
            const drv::AISplineRec* nx = splines[size_t(rng.range(0, int(splines.size()) - 1))];
            drv::AISplineRec nxe = *nx;
            nxe.path = path_emu.at(nx->path);
            e.put(kSplines + 0x900, &nxe, sizeof nxe);
            ++snext.cases;
            e.call(kFnSplinePathNext, {spe, kSplines + 0x900});
            drv::AISplinePath_SetNext(spp, nx);
            same_sp(snext, "AISplinePath::SetNext");
            // O destrutor (DestroyPathHandle) e a lista.
            ++destroy.cases;
            e.call(kFnSplinePathDtor, {spe, 0});
            drv::AISplinePath_Destruct(spp);
            compare_lists(destroy);
        }
        // CreatePathHandle de uma instância à parte (às vezes sem a bandeira ou sem caminho: nulo).
        {
            uint8_t* ip = inst_port.data();
            const drv::ProcAnimState* s0 = reinterpret_cast<const drv::ProcAnimState*>(st_port.data());
            ++create.cases;
            e.call(kFnEngineCreate, {kInst, st_base}, now);
            const uint32_t ce = uint32_t(e.m.gpr(R::v0));
            const drv::RPathHandle* cp = drv::RPathEngine_CreatePathHandle(nf::p32_u(ip), nf::p32_u(s0), now);
            if ((ce == 0) != (cp == nullptr)) create.fail("nulo");
            else compare_lists(create);
        }
        // AIVehicle::SetSplinePath e SetNextSplinePath num AIGroundVehicle (o +0x50).
        {
            static drv::AIGroundVehicle gv;
            std::memset(&gv, 0, sizeof gv);
            const uint32_t gve = e.alloc(0x2a0);
            const drv::AISplineRec* a = splines[size_t(rng.range(0, int(splines.size()) - 1))];
            const drv::AISplineRec* b = splines[size_t(rng.range(0, int(splines.size()) - 1))];
            drv::AISplineRec ae = *a, be = *b;
            ae.path = path_emu.at(a->path);
            be.path = path_emu.at(b->path);
            e.put(kSplines + 0xa00, &ae, sizeof ae);
            e.put(kSplines + 0xa80, &be, sizeof be);
            for (int op = 0; op < 4; ++op) {
                const int kind = rng.range(0, 2);
                ++sset.cases;
                if (kind == 0) {
                    e.call(kFnSetSplinePath, {gve, kSplines + 0xa00});
                    drv::AIVehicle_SetSplinePath(gv, a);
                } else if (kind == 1) {
                    e.call(kFnSetNextSplinePath, {gve, kSplines + 0xa80});
                    drv::AIVehicle_SetNextSplinePath(gv, b);
                } else {
                    e.call(kFnSetSplinePath, {gve, 0});
                    drv::AIVehicle_SetSplinePath(gv, nullptr);
                }
                if ((e.m.read32(gve + 0x50) == 0) != (gv.spline_path == 0)) {
                    sset.fail("o trilho (+0x50)");
                    break;
                }
                if (!compare_lists(sset)) break;
            }
            drv::AIVehicle_SetSplinePath(gv, nullptr);
        }
    }
    drv::RPathEngine_Purge();
    report("RPathEngine::AddInstanceList (com mestres)", add, failures);
    report("RPathEngine::Update (a lista inteira)", upd, failures);
    report("RPathEngine::Reset", reset, failures);
    report("RPathEngine::GetPathHandle", get, failures);
    report("RPathEngine::CreatePathHandle", create, failures);
    report("AISplinePath::AISplinePath (o RPathHandle na lista)", spath, failures);
    report("AISplinePath::Reset", sreset, failures);
    report("AISplinePath::SetNext", snext, failures);
    report("AISplinePath::~AISplinePath (DestroyPathHandle)", destroy, failures);
    report("AIVehicle::SetSplinePath / SetNextSplinePath", sset, failures);
}

}  // namespace

int run_heli_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                    int cases, uint32_t seed) {
    std::printf("\n== a seção do helicóptero: o código original do EE (interpretador MIPS + VU0) x o porte ==\n");
    int failures = 0;
    HeliEmu* e = new HeliEmu();
    try {
        drv::RTimeData_Init(60.0f);
        e->setup(elf, vu0_program, vu0_size);
        drv::Track track;
        std::string error;
        if (!track.load(disc, "paris_mis01", "", error)) {
            std::printf("  paris_mis01 não carregou (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        if (map == drv::UGroup_GroupEnd(root)) {
            std::printf("  sem o 'Map '\n");
            delete e;
            return failures + 1;
        }
        // Os 'pt' de Paris na RAM emulada.
        std::vector<PathRef> paths;
        std::map<uint32_t, uint32_t> path_emu;
        uint32_t at = kPaths;
        for (uint32_t i = 1; i < 0x100; ++i) {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x70740000u | i);
            if (d == drv::UGroup_DataEnd(map)) continue;
            PathRef pr;
            pr.port = reinterpret_cast<const drv::PathInfo*>(drv::UData_Data(d));
            pr.size = drv::UData_Size(d);
            pr.emu = at;
            e->put(at, pr.port, pr.size);
            at = (at + pr.size + 15) & ~15u;
            path_emu[nf::p32_u(pr.port)] = pr.emu;
            paths.push_back(pr);
        }
        const size_t paris_paths = paths.size();
        // Os montados à mão (guardados até o fim).
        Rng rng(seed);
        static std::vector<std::vector<uint8_t>> synthetic;
        synthetic.clear();
        const std::vector<std::vector<uint32_t>> kinds = {
            {drv::kPathRotation, 0},
            {drv::kPathRotation | drv::kPathSpline, drv::kPathSpline},
            {drv::kPathScalar, drv::kPathRotation},
            {drv::kPathStepped, drv::kPathStepped | drv::kPathRotation},
            {drv::kPathSpline | drv::kPathRotation, drv::kPathScalar | drv::kPathSpline, 0},
            {0},
            {drv::kPathRotation},
        };
        for (int k = 0; k < 14; ++k) {
            synthetic.push_back(make_path(rng, kinds[size_t(k) % kinds.size()], rng.range(1, 30), rng.uni(5.0f, 200.0f)));
            PathRef pr;
            pr.port = reinterpret_cast<const drv::PathInfo*>(synthetic.back().data());
            pr.size = uint32_t(synthetic.back().size());
            pr.emu = at;
            e->put(at, pr.port, pr.size);
            at = (at + pr.size + 15) & ~15u;
            path_emu[nf::p32_u(pr.port)] = pr.emu;
            paths.push_back(pr);
        }
        std::printf("  caminhos: %zu de Paris, %zu montados à mão\n", paris_paths, paths.size() - paris_paths);
        // Os 'AISp' de Paris.
        std::vector<const drv::AISplineRec*> splines;
        {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x41495370u);
            if (d != drv::UGroup_DataEnd(map)) {
                const auto* s = reinterpret_cast<const drv::AISplineRec*>(drv::UData_Data(d));
                for (uint32_t i = 0; i < d->count; ++i)
                    if (s[i].path != 0 && path_emu.count(s[i].path)) splines.push_back(&s[i]);
            }
        }
        std::printf("  trilhos ('AISp'): %zu\n", splines.size());
        check_path_info(*e, rng, paths, cases, failures);
        check_splines(*e, rng, splines, path_emu, std::max(50, cases / 4), failures);
        std::vector<PathRef> paris(paths.begin(), paths.begin() + long(paris_paths));
        check_handles(*e, rng, paths, path_emu, cases, failures);
        check_engine(*e, rng, paris, path_emu, splines, cases, failures);
        std::printf("  (o interpretador rodou %" PRIu64 " instruções)\n", e->m.steps());
    } catch (const std::exception& ex) {
        std::printf("  erro: %s\n", ex.what());
        ++failures;
    }
    delete e;
    failures += run_heli_rail_checks(elf, disc, vu0_program, vu0_size, std::max(100, cases / 8), seed);
    failures += run_heli_camera_checks(elf, disc, vu0_program, vu0_size, std::max(300, cases / 4), seed);
    return failures;
}

}  // namespace drvcheck
