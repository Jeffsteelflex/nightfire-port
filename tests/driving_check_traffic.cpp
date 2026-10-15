// nf_driving_check, parte 14 (tests/driving_check_traffic.h): o mundo vivo do DRIVING.ELF, o código ORIGINAL no
// interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte. docs/driving.md seção 18.
//
//  a) A rede de estradas de Paris: o grupo 'RNgp' do CARP do porte copiado para a RAM emulada (os registros e um grupo
//     raiz falso só com ele, os ponteiros trocados pelos endereços emulados) e o WRoadNetwork::Init original sobre a
//     cópia: os globais, as tabelas (pelos endereços do porte), os carimbos dos trechos e os contadores das ruas. Depois
//     as consultas com entradas sorteadas: as pontas, os vetores de través, o comprimento, as larguras, o tipo e o
//     índice da faixa, os pesos das faixas, o nó oposto, os pontos (também nos trechos curvos, pela Bézier estática do
//     GetSegmentCurveStep), o ponto mais perto de uma reta.
//  b) O navegador: WRoadNav construído pelo original (o RCameraSpline dele no heap emulado) e o estado do porte copiado
//     para ele; InitAtPoint (os modos 0-5, com e sem faixa: o FindClosestSegmentInd pela grade da colisão copiada pela
//     parte 5) e sequências de IncNavPosition no modo 1 (o tráfego: CalcNextSegmentLane com o SimRandom da Simulation),
//     ChangeLanes, ReverseNavDirection, UpdateLaneChange e InitAtSegment. Compara o navegador inteiro (salvo o ponteiro
//     do RCameraSpline), a curva dele, o SimRandom, o carimbo global e os registros dos trechos.
//  c-e) A frota (o Vanquish e cinco carros do tráfego montados pelo construtor do porte, o estado inteiro copiado para a
//     RAM emulada a cada caso, com a tabela virtual do PBondCar original): o AIRoadSpawn, os testes de colisão do
//     motorista, a física simples do tráfego (ProcessSimplePhysics, ResetCar) e o AIGroundVehicle::Update inteiro.
//  f) Os gatilhos de Paris (o 'Trgr' e as listas 'el' copiados): o Init original (com o Restart), CheckCollide, Process,
//     Update e Restart com os corpos da frota perto dos gatilhos e as bandeiras e formas sorteadas; os eventos criados
//     (o LookupEvent trocado por um gancho que anota o número, o gatilho e a velocidade) e o EventDynamicData. Depois o
//     destrutor do ESpawnTraffic sobre um evento falso, com os motoristas no vetor estático do PS2 (0x3282d0).
//
// Fora do código original (ganchos nativos): a memória (UMemory::Alloc/Free, FastAlloc/FastFree, __builtin_new/delete),
// o atexit, o strcmp, o memset e o sprintf (a linha de depuração do IncNavPosition num trecho de comprimento zero).

#include "driving_check_traffic.h"
#include "driving_check_integ.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/ai_weapons.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/ai/road_spawn.h"
#include "driving/ai/sim_random.h"
#include "driving/bigf.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving/physics/collision.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "driving_check_world.h"
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
constexpr uint32_t kFnRoadInit = 0x220c28;
constexpr uint32_t kFnEndVecs = 0x221730, kFnEndPoints = 0x221888, kFnEndPointsOffset = 0x221910;
constexpr uint32_t kFnLength = 0x221a10, kFnLeft = 0x221ac8, kFnRight = 0x221cb0, kFnLaneType = 0x221e98;
constexpr uint32_t kFnLaneIndex = 0x221f20, kFnLaneWeights = 0x222060, kFnOppNode = 0x222300;
constexpr uint32_t kFnPointT = 0x2223b0, kFnPoint = 0x222468, kFnPointDist = 0x222700, kFnPointLane = 0x222810;
constexpr uint32_t kFnSegIntersect = 0x222968, kFnLineIntersect = 0x222a30, kFnAttached = 0x2270e8;
constexpr uint32_t kFnSpecialRoads = 0x221528;
constexpr uint32_t kFnNavCtor = 0x223338, kFnInitAtPoint = 0x225c48, kFnInitAtSegment = 0x2257c0;
constexpr uint32_t kFnIncNav = 0x224e00, kFnChangeLanes = 0x226f18, kFnReverse = 0x225358;
constexpr uint32_t kFnUpdateLaneChange = 0x227058;
// Os ganchos.
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnFastFree = 0x2cfe50, kFnAlloc = 0x2d03b8, kFnFree = 0x2d0418;
constexpr uint32_t kFnBuiltinNew = 0x2d0910, kFnBuiltinDelete = 0x2d09a8, kFnAtexit = 0x2a71f8;
constexpr uint32_t kFnStrcmp = 0x2a9508, kFnMemset = 0x2a8038, kFnSprintf = 0x2a9098;

// Os globais.
constexpr uint32_t kWorldPtr = 0x33aac8;   // fgWorld
constexpr uint32_t kSim = 0x339190;        // a Simulation (a primeira palavra: o SimRandom)
constexpr uint32_t kRoadInstance = 0x33a5f8, kNodes = 0x33a5fc, kSegments = 0x33a600, kInters = 0x33a604;
constexpr uint32_t kJunctions = 0x33a608, kRoads = 0x33a60c, kStamp = 0x33a610;
constexpr uint32_t kValid = 0x3c8254, kNumRoads = 0x3c8258, kValidTraffic = 0x3c825c, kRightSide = 0x3c827c;
constexpr uint32_t kNumSegments = 0x3c8280, kNumNodes = 0x3c8360, kNumJunctions = 0x3c8364, kNumInters = 0x3c8368;
constexpr uint32_t kLaneOffsets = 0x427000, kLaneWeights = 0x427018;
constexpr uint32_t kCurveSpline = 0x4191e0, kCurveGuard = 0x33a958;

// A RAM emulada.
constexpr uint32_t kWorldBegin = 0x01000000, kWorldEnd = 0x01600000;
constexpr uint32_t kRoadBegin = 0x01600000, kRoadEnd = 0x01800000;
constexpr uint32_t kMgr = 0x01800000, kGrid = 0x01800100, kFakeWorld = 0x01800200, kName = 0x01800300;
constexpr uint32_t kRandom = 0x01800400, kScratch = 0x01801000, kNav = 0x01802000;
constexpr uint32_t kHeapBegin = 0x01810000, kHeapEnd = 0x01c00000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 11) {}
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
    std::snprintf(b, sizeof b, "%08x", v);
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

bool same_words(const char* what, const uint8_t* emu, const uint8_t* port, uint32_t n,
                std::initializer_list<std::pair<uint32_t, uint32_t>> skip, std::string& out) {
    for (uint32_t o = 0; o < n; o += 4) {
        bool skipped = false;
        for (const auto& [a, b] : skip)
            if (o >= a && o < b) skipped = true;
        if (skipped) continue;
        uint32_t x, y;
        std::memcpy(&x, emu + o, 4);
        std::memcpy(&y, port + o, 4);
        if (x != y) {
            out = std::string(what) + " +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

uint32_t fbits(float f) { return ps2::fbits(f); }

// A cópia da rede de estradas: os dados do 'RNgp' do porte num bloco da RAM emulada (a mesma disposição relativa) e
// os cabeçalhos dos grupos com os endereços emulados.
struct RoadCopy {
    std::map<uint32_t, uint32_t> emu;   // o registro do porte (p32) -> o endereço emulado
    uint32_t root = 0;
    uint32_t to_emu(uint32_t p32) const {
        if (!p32) return 0;
        auto it = emu.find(p32);
        return it == emu.end() ? 0xdead0000u : it->second;
    }
};

class TrafficEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", heap, n);
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", alloc);
        m.hook(kFnAlloc, "UMemory::Alloc", alloc);
        m.hook(kFnBuiltinNew, "__builtin_new", alloc);
        m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kFnFree, "UMemory::Free", [](MipsMachine&) {});
        m.hook(kFnBuiltinDelete, "__builtin_delete", [](MipsMachine&) {});
        m.hook(kFnAtexit, "atexit", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnSprintf, "sprintf", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnStrcmp, "strcmp", [](MipsMachine& mm) {
            const char* a = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a0)), 1));
            const char* b = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            mm.set_gpr(R::v0, uint64_t(int64_t(std::strcmp(a, b))));
        });
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.write32(kSim, kRandom);
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 200'000'000);
    }
    float f0() const { return m.fpr(0); }
};

// O grupo 'RNgp' do porte na RAM emulada, e o WWorld falso que o WRoadNetwork::Init lê (+0xc o nome, +0x14 a raiz).
bool copy_roads(TrafficEmu& e, drv::Track& track, RoadCopy& rc, std::string& error) {
    drv::UGroup* root = track.root();
    drv::UGroup* g = drv::UGroup_GroupLocateTag(root, 0x524e6770);
    if (g == drv::UGroup_GroupEnd(root)) {
        error = "a pista não tem 'RNgp'";
        return false;
    }
    drv::UData* first = drv::UGroup_GroupEnd(g);
    drv::UData* last = drv::UGroup_DataEnd(g);
    // Cada registro no seu lugar (o mesmo resto por 16 do endereço do porte), com folga para o sd de 8 bytes do carimbo.
    uint32_t at = kRoadBegin;
    for (drv::UData* d = first; d < last; ++d) {
        const uint32_t p = nf::p32_u(drv::UData_Data(d));
        const uint32_t n = std::max(drv::UData_Size(d), drv::UData_Size(d) * std::max(1u, d->count)) + 16;
        const uint32_t a = ((at + 15) & ~15u) + (p & 15u);
        if (a + n + 64 > kRoadEnd) {
            error = "a rede de estradas não cabe na RAM emulada";
            return false;
        }
        put(e.m, a, nf::p32_ptr<uint8_t>(p), n);
        rc.emu[p] = a;
        at = a + n;
    }
    const uint32_t headers = 16u * uint32_t(last - first) + 32;
    if (((at + 15) & ~15u) + headers + 64 > kRoadEnd) {
        error = "os cabeçalhos da rede não cabem na RAM emulada";
        return false;
    }
    const uint32_t arr = (at + 31) & ~15u;
    for (drv::UData* d = first; d < last; ++d) {
        drv::UData c = *d;
        c.offset = rc.to_emu(nf::p32_u(drv::UData_Data(d)));
        c.flags &= ~2u;
        put(e.m, arr + 16u * uint32_t(d - first), &c, 16);
    }
    const uint32_t grp = arr + headers - 32;
    drv::UData gh = *g;
    gh.offset = arr;
    gh.flags = (gh.flags & 0x1fu & ~2u);  // nenhum subgrupo
    put(e.m, grp, &gh, 16);
    rc.root = grp + 16;
    drv::UData rh = *root;
    rh.offset = grp;
    rh.flags = (1u << 5) | (rh.flags & 0x1fu & ~2u & ~0x10u);
    rh.count = 0;
    put(e.m, rc.root, &rh, 16);
    std::memset(e.m.mem(kFakeWorld, 0x40), 0, 0x40);
    const char* name = "paris_mis01";
    put(e.m, kName, name, uint32_t(std::strlen(name) + 1));
    e.m.write32(kFakeWorld + 0xc, kName);
    e.m.write32(kFakeWorld + 0x14, rc.root);
    e.m.write32(kWorldPtr, kFakeWorld);
    return true;
}

// a) A rede e as consultas.
int check_network(TrafficEmu& e, const RoadCopy& rc, int cases, Rng& rng) {
    int failures = 0;
    drv::RoadNetworkGlobals& r = drv::road_network();
    {
        Tally t;
        ++t.cases;
        e.call(kFnRoadInit, {});
        auto eq32 = [&](const char* what, uint32_t addr, uint32_t port) {
            if (e.m.read32(addr) != port)
                t.fail(std::string(what) + ": original " + hex(e.m.read32(addr)) + ", porte " + hex(port));
        };
        if ((e.m.read32(kRoadInstance) != 0) != (r.instance != 0)) t.fail("fgRoadNetwork");
        eq32("fValid", kValid, uint32_t(r.valid));
        eq32("fNumRoads", kNumRoads, uint32_t(r.num_roads));
        eq32("fValidTrafficRoads", kValidTraffic, uint32_t(r.valid_traffic_roads));
        eq32("fRightSideSystem", kRightSide, uint32_t(r.right_side_system));
        eq32("fNumSegments", kNumSegments, uint32_t(r.num_segments));
        eq32("fNumNodes", kNumNodes, uint32_t(r.num_nodes));
        eq32("fNumJunctions", kNumJunctions, uint32_t(r.num_junctions));
        eq32("fNumIntersections", kNumInters, uint32_t(r.num_intersections));
        for (int i = 0; i < 6; ++i) eq32("as linhas dos deslocamentos", kLaneOffsets + 4u * uint32_t(i), r.lane_offsets[i]);
        for (int i = 0; i < 32; ++i) eq32("as linhas dos pesos", kLaneWeights + 4u * uint32_t(i), r.lane_weights[i]);
        auto table = [&](const char* what, uint32_t ptr, const std::vector<uint32_t>& v) {
            const uint32_t base = e.m.read32(ptr);
            for (size_t i = 0; i < v.size(); ++i) {
                const uint32_t a = e.m.read32(base + 4u * uint32_t(i));
                if (a != rc.to_emu(v[i])) {
                    t.fail(std::string(what) + "[" + std::to_string(i) + "]: original " + hex(a) + ", porte " +
                           hex(rc.to_emu(v[i])));
                    return;
                }
            }
        };
        table("fNodes", kNodes, r.nodes);
        table("fSegments", kSegments, r.segments);
        table("fIntersections", kInters, r.intersections);
        table("fJunctions", kJunctions, r.junctions);
        table("fRoads", kRoads, r.roads);
        for (size_t i = 0; i < r.segments.size() && t.bad == 0; ++i) {
            std::string why;
            if (!same_words("trecho", e.m.mem(rc.to_emu(r.segments[i]), 0x68), nf::p32_ptr<uint8_t>(r.segments[i]), 0x68,
                            {}, why))
                t.fail(why + " (trecho " + std::to_string(i) + ")");
        }
        for (size_t i = 0; i < r.roads.size() && t.bad == 0; ++i) {
            std::string why;
            if (!same_words("rua", e.m.mem(rc.to_emu(r.roads[i]), 0x30), nf::p32_ptr<uint8_t>(r.roads[i]), 0x30, {}, why))
                t.fail(why + " (rua " + std::to_string(i) + ")");
        }
        report("WRoadNetwork::Init (Paris: tabelas, carimbos, ruas)", t, failures);
        if (t.bad) return failures;
    }
    const int nseg = r.num_segments;
    const uint32_t out1 = kScratch, out2 = kScratch + 0x10, out3 = kScratch + 0x20, out4 = kScratch + 0x30;
    auto seg_emu = [&](int i) { return rc.to_emu(r.segments[size_t(i)]); };
    auto garbage = [&](float* p) {
        for (int i = 0; i < 4; ++i) p[i] = rng.uni(-100, 100);
        put(e.m, kScratch, p, 16);
        put(e.m, kScratch + 0x10, p, 16);
        put(e.m, kScratch + 0x20, p, 16);
        put(e.m, kScratch + 0x30, p, 16);
    };
    auto cmp3 = [&](Tally& t, const char* what, uint32_t emu, const float* port) {
        std::string why;
        if (!same_words(what, e.m.mem(emu, 12), reinterpret_cast<const uint8_t*>(port), 12, {}, why)) t.fail(why);
    };
    auto cmpf = [&](Tally& t, const char* what, float emu, float port) {
        if (fbits(emu) != fbits(port)) t.fail(std::string(what) + ": original " + hex(fbits(emu)) + ", porte " + hex(fbits(port)));
    };
    Tally t_end, t_len, t_width, t_lane, t_weights, t_opp, t_point, t_curve, t_lanept, t_intersect, t_special;
    int curved_seen = 0;
    for (int c = 0; c < cases; ++c) {
        const int si = rng.range(0, nseg - 1);
        const drv::WRoadSegment* s = drv::road_segment(si);
        if (s->flags & drv::kSegCurved) ++curved_seen;
        float g[4], a[4], b[4];
        // As pontas, os vetores de través e as pontas deslocadas.
        {
            ++t_end.cases;
            garbage(g);
            std::memcpy(a, g, 16);
            std::memcpy(b, g, 16);
            e.call(kFnEndVecs, {1, seg_emu(si), out1, out2});
            drv::WRoadNetwork_GetSegmentEndVecs(s, a, b);
            cmp3(t_end, "GetSegmentEndVecs começo", out1, a);
            cmp3(t_end, "GetSegmentEndVecs fim", out2, b);
            e.call(kFnEndPoints, {1, seg_emu(si), out1, out2});
            drv::WRoadNetwork_GetSegmentEndPoints(s, a, b);
            cmp3(t_end, "GetSegmentEndPoints começo", out1, a);
            cmp3(t_end, "GetSegmentEndPoints fim", out2, b);
            const float off = rng.chance(0.2) ? 0.0f : rng.uni(-20, 20);
            e.m.set_fpr(12, off);
            e.call(kFnEndPointsOffset, {1, seg_emu(si), out1, out2});
            drv::WRoadNetwork_GetSegmentEndPointsOffset(s, off, a, b);
            cmp3(t_end, "GetSegmentEndPoints(desloc.) começo", out1, a);
            cmp3(t_end, "GetSegmentEndPoints(desloc.) fim", out2, b);
            const int ni = rng.range(0, 1);
            const int16_t ex = int16_t(rng.chance(0.5) ? -1 : s->index);
            e.call(kFnAttached, {rc.to_emu(r.nodes[s->node[ni]]), uint32_t(int32_t(ex))});
            const drv::WRoadSegment* at = drv::GetAttachedDirectionalSegment(drv::road_node(s->node[ni]), ex);
            if (uint32_t(e.m.gpr(R::v0)) != rc.to_emu(at ? nf::p32_u(at) : 0)) t_end.fail("GetAttachedDirectionalSegment");
        }
        {
            ++t_len.cases;
            e.call(kFnLength, {1, seg_emu(si)});
            cmpf(t_len, "GetSegmentLength", e.f0(), drv::WRoadNetwork_GetSegmentLength(s));
        }
        {
            ++t_width.cases;
            e.call(kFnLeft, {1, seg_emu(si)});
            cmpf(t_width, "GetSegmentLeftDrivableDist", e.f0(), drv::WRoadNetwork_GetSegmentLeftDrivableDist(s));
            e.call(kFnRight, {1, seg_emu(si)});
            cmpf(t_width, "GetSegmentRightDrivableDist", e.f0(), drv::WRoadNetwork_GetSegmentRightDrivableDist(s));
        }
        {
            ++t_lane.cases;
            e.call(kFnLaneType, {1, seg_emu(si)});
            if (int(e.m.gpr(R::v0) & 0xff) != drv::WRoadNetwork_GetSegmentLaneType(s)) t_lane.fail("GetSegmentLaneType");
            float off = rng.chance(0.1) ? 0.0f : rng.uni(-30, 30);
            e.m.write32(out1, fbits(off));
            e.call(kFnLaneIndex, {1, seg_emu(si), out1});
            const int lane = drv::WRoadNetwork_GetSegmentLaneIndex(s, &off);
            if (int32_t(e.m.gpr(R::v0)) != lane) t_lane.fail("GetSegmentLaneIndex: a faixa");
            if (e.m.read32(out1) != fbits(off)) t_lane.fail("GetSegmentLaneIndex: o deslocamento");
        }
        {
            ++t_weights.cases;
            const int count = rng.range(0, 16), size = count + rng.range(0, 16);
            const int8_t end = int8_t(rng.range(0, 1));
            uint8_t port[64];
            for (int i = 0; i < 64; ++i) port[i] = uint8_t(rng.next());
            put(e.m, kScratch, port, 64);
            e.m.set_gpr(R::t0, uint64_t(int64_t(size)));
            e.m.set_gpr(R::t1, uint64_t(int64_t(count)));
            e.call(kFnLaneWeights, {1, uint32_t(si), uint32_t(int32_t(end)), kScratch});
            drv::WRoadNetwork_GetSegmentLaneWeights(si, end, port, size, count);
            if (std::memcmp(e.m.mem(kScratch, 64), port, 64) != 0) t_weights.fail("GetSegmentLaneWeights");
        }
        {
            ++t_opp.cases;
            const int ni = rng.range(0, 2);
            const uint32_t node = ni < 2 ? r.nodes[s->node[ni]] : r.nodes[size_t(rng.range(0, r.num_nodes - 1))];
            e.call(kFnOppNode, {1, seg_emu(si), rc.to_emu(node)});
            const drv::WRoadNode* pn = drv::WRoadNetwork_GetSegmentOppNode(s, nf::p32_ptr<drv::WRoadNode>(node));
            if (uint32_t(e.m.gpr(R::v0)) != rc.to_emu(nf::p32_u(pn))) t_opp.fail("GetSegmentOppNode");
        }
        {
            ++t_point.cases;
            if (s->flags & drv::kSegCurved) ++t_curve.cases;
            Tally& t = (s->flags & drv::kSegCurved) ? t_curve : t_point;
            float tt = rng.chance(0.1) ? rng.uni(-0.5f, 1.5f) : rng.uni(0, 1);
            garbage(g);
            std::memcpy(a, g, 16);
            e.m.set_fpr(12, tt);
            e.call(kFnPointT, {1, seg_emu(si), out1});
            drv::WRoadNetwork_GetPointOnSegmentT(s, tt, a);
            cmp3(t, "GetPointOnSegment(t)", out1, a);
            const float dist = rng.uni(-10, 200);
            std::memcpy(a, g, 16);
            e.m.set_fpr(12, dist);
            e.call(kFnPointDist, {1, seg_emu(si), out1});
            drv::WRoadNetwork_GetPointOnSegmentDist(s, dist, a);
            cmp3(t, "GetPointOnSegmentDist", out1, a);
            // Entre duas pontas quaisquer (o RCameraSpline estático nos curvos).
            float p0[4], p1[4];
            for (int i = 0; i < 4; ++i) {
                p0[i] = rng.uni(-300, 300);
                p1[i] = rng.uni(-300, 300);
            }
            put(e.m, out2, p0, 16);
            put(e.m, out3, p1, 16);
            std::memcpy(a, g, 16);
            e.m.set_fpr(12, tt);
            e.m.set_gpr(R::t0, out1);
            e.call(kFnPoint, {1, out2, out3, seg_emu(si)});
            drv::WRoadNetwork_GetPointOnSegment(p0, p1, s, tt, a);
            cmp3(t, "GetPointOnSegment(pontas)", out1, a);
        }
        {
            ++t_lanept.cases;
            const int lane = rng.range(-5, 5);
            const float tt = rng.uni(0, 1);
            garbage(g);
            std::memcpy(a, g, 16);
            std::memcpy(b, g, 16);
            e.m.set_fpr(12, tt);
            e.m.set_gpr(R::t0, out2);
            e.call(kFnPointLane, {1, seg_emu(si), uint32_t(lane), out1});
            drv::WRoadNetwork_GetPointAndVecOnSegmentLane(s, tt, lane, a, b);
            cmp3(t_lanept, "GetPointAndVecOnSegmentLane ponto", out1, a);
            cmp3(t_lanept, "GetPointAndVecOnSegmentLane vetor", out2, b);
        }
        {
            ++t_intersect.cases;
            float pt[4], q0[4], q1[4];
            for (int i = 0; i < 4; ++i) pt[i] = rng.uni(-400, 400);
            {
                // O ponto de teste sai do porte: a Bézier estática dele volta ao que era (o original não a usou).
                const drv::CameraSpline keep = drv::curve_spline();
                drv::WRoadNetwork_GetPointOnSegmentT(s, rng.uni(0, 1), pt);
                drv::curve_spline() = keep;
            }
            for (int i = 0; i < 3; ++i) pt[i] = ps2::ee::add(pt[i], rng.uni(-30, 30));
            const bool clamp = rng.chance(0.7);
            garbage(g);
            std::memcpy(a, g, 16);
            put(e.m, out2, pt, 16);
            e.m.set_gpr(R::t0, clamp ? 1 : 0);
            e.call(kFnSegIntersect, {1, seg_emu(si), out2, out1});
            const float rp = drv::WRoadNetwork_GetSegmentPointIntersect(s, pt, a, clamp);
            cmpf(t_intersect, "GetSegmentPointIntersect t", e.f0(), rp);
            cmp3(t_intersect, "GetSegmentPointIntersect ponto", out1, a);
            for (int i = 0; i < 4; ++i) {
                q0[i] = rng.uni(-300, 300);
                q1[i] = rng.chance(0.05) ? q0[i] : rng.uni(-300, 300);
            }
            std::memcpy(a, g, 16);
            put(e.m, out1, g, 16);
            put(e.m, out3, q0, 16);
            put(e.m, out4, q1, 16);
            e.m.set_gpr(R::t0, out1);
            e.m.set_gpr(R::t1, clamp ? 1 : 0);
            e.call(kFnLineIntersect, {1, out3, out4, out2});
            const float rl = drv::WRoadNetwork_GetLinePointIntersect(q0, q1, pt, a, clamp);
            cmpf(t_intersect, "GetLinePointIntersect t", e.f0(), rl);
            cmp3(t_intersect, "GetLinePointIntersect ponto", out1, a);
        }
    }
    {
        ++t_special.cases;
        int32_t port[64];
        e.call(kFnSpecialRoads, {1, kScratch, 64});
        const int n = drv::WRoadNetwork_GetSpecialRoads(port, 64);
        if (int32_t(e.m.gpr(R::v0)) != n) t_special.fail("GetSpecialRoads: a contagem");
        else if (n && std::memcmp(e.m.mem(kScratch, 4u * uint32_t(n)), port, 4u * size_t(n)) != 0)
            t_special.fail("GetSpecialRoads: os trechos");
    }
    // A Bézier estática do GetSegmentCurveStep (os pontos, o tipo).
    {
        Tally t;
        ++t.cases;
        if (drv::curve_spline_built() && e.m.read32(kCurveGuard) != 0) {
            std::string why;
            if (!same_words("RCameraSpline estático", e.m.mem(kCurveSpline, 0x64),
                            reinterpret_cast<const uint8_t*>(&drv::curve_spline()), 0x64, {}, why))
                t.fail(why);
        }
        report("o RCameraSpline do GetSegmentCurveStep", t, failures);
    }
    report("as pontas, os vetores de través, GetAttachedDirectionalSegment", t_end, failures);
    report("GetSegmentLength", t_len, failures);
    report("as larguras (Left/RightDrivableDist)", t_width, failures);
    report("GetSegmentLaneType e GetSegmentLaneIndex", t_lane, failures);
    report("GetSegmentLaneWeights", t_weights, failures);
    report("GetSegmentOppNode", t_opp, failures);
    report("os pontos nos trechos retos (t, distância, pontas)", t_point, failures);
    report("os pontos nos trechos curvos (a Bézier)", t_curve, failures);
    report("GetPointAndVecOnSegmentLane", t_lanept, failures);
    report("Get{Segment,Line}PointIntersect", t_intersect, failures);
    report("GetSpecialRoads", t_special, failures);
    std::printf("  (trechos curvos sorteados: %d)\n", curved_seen);
    return failures;
}

// b) O navegador.
struct NavCopy {
    TrafficEmu* e;
    const RoadCopy* rc;
    uint32_t emu_spline = 0;
    void put_nav(const drv::WRoadNav& n) {
        drv::WRoadNav c = n;
        c.spline = emu_spline;
        put(e->m, kNav, &c, sizeof c);
        put(e->m, emu_spline, drv::road_nav_spline(n), 0x64);
    }
    bool same(const drv::WRoadNav& n, std::string& why) {
        if (!same_words("WRoadNav", e->m.mem(kNav, 0xc0), reinterpret_cast<const uint8_t*>(&n), 0xc0, {{0x90, 0x94}},
                        why))
            return false;
        return same_words("RCameraSpline do navegador", e->m.mem(emu_spline, 0x64),
                          reinterpret_cast<const uint8_t*>(drv::road_nav_spline(n)), 0x64, {}, why);
    }
};

void put_random(TrafficEmu& e) { put(e.m, kRandom, &drv::sim_random(), 16); }
bool same_random(TrafficEmu& e, std::string& why) {
    return same_words("SimRandom", e.m.mem(kRandom, 16), reinterpret_cast<const uint8_t*>(&drv::sim_random()), 16, {},
                      why);
}
void put_stamp(TrafficEmu& e) {
    e.m.write32(kStamp, drv::road_network().segment_stamp[0]);
    e.m.write32(kStamp + 4, drv::road_network().segment_stamp[1]);
}
bool same_stamp(TrafficEmu& e, std::string& why) {
    const drv::RoadNetworkGlobals& r = drv::road_network();
    if (e.m.read32(kStamp) == r.segment_stamp[0] && e.m.read32(kStamp + 4) == r.segment_stamp[1]) return true;
    why = "fSegmentStamp: original " + hex(e.m.read32(kStamp)) + ", porte " + hex(r.segment_stamp[0]);
    return false;
}
bool same_records(TrafficEmu& e, const RoadCopy& rc, std::string& why) {
    const drv::RoadNetworkGlobals& r = drv::road_network();
    for (size_t i = 0; i < r.segments.size(); ++i)
        if (!same_words("trecho", e.m.mem(rc.to_emu(r.segments[i]), 0x68), nf::p32_ptr<uint8_t>(r.segments[i]), 0x68, {},
                        why))
            return false;
    return true;
}

int check_nav(TrafficEmu& e, const RoadCopy& rc, int cases, Rng& rng) {
    int failures = 0;
    drv::RoadNetworkGlobals& r = drv::road_network();
    e.call(kFnNavCtor, {kNav});
    NavCopy nc{&e, &rc, e.m.read32(kNav + 0x90)};
    drv::WRoadNav nav{};
    drv::WRoadNav_Construct(nav);
    Tally t_init, t_inc, t_lanes, t_rev, t_seg, t_upd;
    uint64_t steps = 0, changes = 0, segments_changed = 0, curved = 0, lane_kept = 0;
    const int seq = std::max(1, cases / 10);
    for (int c = 0; c < seq; ++c) {
        // Um ponto perto de um trecho sorteado (ou longe de todos), uma direção, um modo.
        const int si = rng.range(0, r.num_segments - 1);
        float p[4] = {0, 0, 0, 0};
        drv::WRoadNetwork_GetPointOnSegmentT(drv::road_segment(si), rng.uni(0, 1), p);
        for (int i = 0; i < 3; ++i) p[i] = ps2::ee::add(p[i], i == 1 ? rng.uni(-2, 2) : rng.uni(-15, 15));
        if (rng.chance(0.03)) p[0] = rng.uni(-3000, 3000);
        float h[3] = {rng.uni(-1, 1), rng.uni(-0.2f, 0.2f), rng.uni(-1, 1)};
        if (rng.chance(0.08)) h[0] = h[1] = h[2] = 0.0f;
        const int modes[] = {1, 1, 1, 1, 0, 2, 3, 4, 5};
        nav.mode = modes[rng.range(0, 8)];
        nav.unknown04 = uint8_t(rng.range(0, 3));
        nav.unknownb1 = rng.chance(0.2) ? 1 : 0;
        nav.unknownb2 = rng.chance(0.2) ? 1 : 0;
        const bool no_lane = rng.chance(0.2);
        nc.put_nav(nav);
        put_random(e);
        put_stamp(e);
        float hp[4] = {h[0], h[1], h[2], 0}, pp[4] = {p[0], p[1], p[2], p[3]};
        put(e.m, kScratch, pp, 16);
        put(e.m, kScratch + 0x10, hp, 16);
        e.call(kFnInitAtPoint, {kNav, kScratch, kScratch + 0x10, no_lane ? 1u : 0u});
        drv::WRoadNav_InitAtPoint(nav, p, h, no_lane);
        ++t_init.cases;
        std::string why;
        if (!nc.same(nav, why) || !same_stamp(e, why)) {
            t_init.fail(why + " (modo " + std::to_string(nav.mode) + ")");
            continue;
        }
        if (nav.segment < 0) continue;
        nav.mode = 1;  // o tráfego anda no modo 1
        e.m.write32(kNav, 1);
        const int ops = rng.range(5, 40);
        for (int k = 0; k < ops; ++k) {
            const int op = rng.range(0, 99);
            nc.put_nav(nav);
            put_random(e);
            put_stamp(e);
            if (op < 80) {
                ++t_inc.cases;
                ++steps;
                const int16_t before = nav.segment;
                float step = rng.chance(0.05) ? 0.0f : rng.uni(0, rng.chance(0.2) ? 120.0f : 15.0f);
                float hd[4] = {rng.uni(-1, 1), 0, rng.uni(-1, 1), 0};
                const int16_t target = int16_t(rng.chance(0.5) ? -1 : rng.range(0, r.num_segments - 1));
                put(e.m, kScratch + 0x20, hd, 16);
                e.m.set_fpr(12, step);
                e.call(kFnIncNav, {kNav, kScratch + 0x20, uint32_t(int32_t(target))});
                drv::WRoadNav_IncNavPosition(nav, step, hd, target);
                if (nav.segment != before) ++segments_changed;
                if (drv::road_segment(nav.segment)->flags & drv::kSegCurved) ++curved;
                if (!nc.same(nav, why) || !same_random(e, why)) t_inc.fail(why);
                if (nav.lane != 0) ++lane_kept;
            } else if (op < 88) {
                ++t_lanes.cases;
                ++changes;
                const float off = rng.uni(-10, 10), dist = rng.chance(0.5) ? 0.0f : rng.uni(0, 30);
                e.m.set_fpr(12, off);
                e.m.set_fpr(13, dist);
                e.call(kFnChangeLanes, {kNav});
                drv::WRoadNav_ChangeLanes(nav, off, dist);
                if (!nc.same(nav, why)) t_lanes.fail(why);
            } else if (op < 92) {
                ++t_rev.cases;
                e.call(kFnReverse, {kNav});
                drv::WRoadNav_ReverseNavDirection(nav);
                if (!nc.same(nav, why)) t_rev.fail(why);
            } else if (op < 96) {
                ++t_upd.cases;
                const float step = rng.uni(0, 10);
                e.m.set_fpr(12, step);
                e.call(kFnUpdateLaneChange, {kNav});
                const bool rv = drv::WRoadNav_UpdateLaneChange(nav, step);
                if ((e.m.gpr(R::v0) != 0) != rv) t_upd.fail("UpdateLaneChange: o retorno");
                if (!nc.same(nav, why)) t_upd.fail(why);
            } else {
                ++t_seg.cases;
                const int16_t sg = int16_t(rng.range(0, r.num_segments - 1));
                const int8_t lane = int8_t(rng.range(-3, 3));
                const float tt = rng.uni(0, 1);
                const bool fwd = rng.chance(0.5);
                e.m.set_fpr(12, tt);
                e.call(kFnInitAtSegment, {kNav, uint32_t(int32_t(sg)), uint32_t(int32_t(lane)), fwd ? 1u : 0u});
                drv::WRoadNav_InitAtSegment(nav, sg, lane, tt, fwd);
                if (!nc.same(nav, why)) t_seg.fail(why);
            }
        }
        if ((c & 15) == 0 && !same_records(e, rc, why)) t_init.fail(why);
    }
    report("WRoadNav::InitAtPoint (os modos 0-5; FindClosestSegmentInd)", t_init, failures);
    report("WRoadNav::IncNavPosition (modo 1: CalcNextSegmentLane)", t_inc, failures);
    report("WRoadNav::ChangeLanes", t_lanes, failures);
    report("WRoadNav::ReverseNavDirection", t_rev, failures);
    report("WRoadNav::UpdateLaneChange", t_upd, failures);
    report("WRoadNav::InitAtSegment", t_seg, failures);
    std::printf("  (passos %" PRIu64 ", trocas de trecho %" PRIu64 ", em trecho curvo %" PRIu64 ", com faixa %" PRIu64
                ", trocas de faixa %" PRIu64 "; guardas %" PRIu64 ")\n",
                steps, segments_changed, curved, lane_kept, changes, drv::road_nav_stats().guards);
    drv::WRoadNav_Destruct(nav);
    return failures;
}

// ---------------------------------------------------------------- c, d, e: o tráfego (os carros na RAM emulada)

// As funções originais do tráfego.
constexpr uint32_t kFnSpawnPoint = 0x13c5e8, kFnRespawnPosCheck = 0x13c798;
constexpr uint32_t kFnRespawnAvailable = 0x13c860, kFnRandomSpawn = 0x13ca60, kFnRefresh = 0x13d618;
constexpr uint32_t kFnSimplePhysics = 0x18ecb8, kFnResetCarPos = 0x187eb8;
constexpr uint32_t kFnAIUpdate = 0x12e350, kFnGreenLight = 0x13efc8;
constexpr uint32_t kFnTestCollision = 0x135530, kFnStopPoint = 0x135978;
constexpr uint32_t kPBondCarVtable = 0x379ef0;
// Os globais do tráfego.
constexpr uint32_t kSpawnInstance = 0x327d58, kSpawnNum = 0x327d5c, kSpawnFlag = 0x327d60, kSpawnCount = 0x327d64;
constexpr uint32_t kSpawnMin = 0x327d68, kSpawnMax = 0x327d6c, kSpawnForced = 0x327d70, kSpawnNavGuard = 0x327d74;
constexpr uint32_t kSpawnRefresh = 0x3c8278, kSpawnList = 0x423000, kSpawnNav = 0x3c8560;
constexpr uint32_t kCtlInstance = 0x327d80, kMaxTraffic = 0x32b750, kNumTraffic = 0x32b752;
constexpr uint32_t kLightCycle = 0x3c828c, kLightNext = 0x3c8290, kNavSpread = 0x327c38, kUnspawnZ = 0x327c3c;
constexpr uint32_t kCameraList = 0x333ed8, kMissionPtr = 0x33a024, kCarScratchPtr = 0x3d0d10;
constexpr uint32_t kRigidScratchPtr = 0x339178;
// A RAM emulada do tráfego.
constexpr uint32_t kCars = 0x01c00000;       // 8 x 0x500 (PBondCar 0x460)
constexpr uint32_t kAIs = 0x01c02800;        // 8 x 0x2c0
constexpr uint32_t kNavs = 0x01c04000;       // 8 x 0x140: o navegador e o RCameraSpline dele em +0xc0
constexpr uint32_t kPhys = 0x01c05000;       // 8 x 0x100 os CarPhysics
constexpr uint32_t kBodies = 0x01c06000;     // 48 x 0x80
constexpr uint32_t kInfos = 0x01c08000;      // 48 x 0x500
constexpr uint32_t kCarList = 0x01c18000;
constexpr uint32_t kCamList = 0x01c18100, kCamObj = 0x01c18110, kCamObj2 = 0x01c18180, kCamMatrix = 0x01c18200;
constexpr uint32_t kMission = 0x01c18400;
constexpr uint32_t kRender = 0x01c19000, kRenderVt = 0x01c19100, kAudio = 0x01c19800, kCarScratch = 0x01c1a000;
constexpr uint32_t kTypeNames = 0x01c1b000, kRigidScratch = 0x01c1b800, kFnFake = 0x01c1c000;
constexpr int kMaxCars = 8;

uint32_t car_emu(int i) { return kCars + 0x500u * uint32_t(i); }
uint32_t ai_emu(int i) { return kAIs + 0x2c0u * uint32_t(i); }
uint32_t nav_emu(int i) { return kNavs + 0x140u * uint32_t(i); }
uint32_t body_emu(int slot) { return kBodies + 0x80u * uint32_t(slot); }
uint32_t info_emu(int slot) { return kInfos + 0x500u * uint32_t(slot); }

// Os carros do tráfego: os do porte (na Simulation e no AIVehicleController) e os seus lugares na RAM emulada.
struct Fleet {
    TrafficEmu* e = nullptr;
    const RoadCopy* rc = nullptr;
    drv::TrackWorld* tw = nullptr;
    const WorldCopy* wc = nullptr;
    std::vector<drv::BondCar*> cars;  // = simulation().cars
    uint64_t render_calls = 0, audio_calls = 0, other_calls = 0;
    uint32_t heap_mark = 0;  // o heap emulado volta aqui a cada caso (o que fica: os RCameraSpline construídos)
    uint32_t spawn_spline = 0;  // o RCameraSpline do WRoadNav estático do GetRandomTrafficSpawn (0x3c8560)
    float camera[4][4] = {};

    int index_of_ai(const drv::AIGroundVehicle* v) const {
        for (size_t i = 0; i < cars.size(); ++i)
            if (drv::ai_of(*cars[i]) == v) return int(i);
        return -1;
    }
    uint32_t ai_addr(uint32_t p32) const {
        if (p32 == 0) return 0;
        const int i = index_of_ai(nf::p32_ptr<drv::AIGroundVehicle>(p32));
        return i < 0 ? 0xdead0000u : ai_emu(i);
    }
    uint32_t car_addr_of(uint32_t p32_pbondcar) const {
        if (p32_pbondcar == 0) return 0;
        for (size_t i = 0; i < cars.size(); ++i)
            if (nf::p32_u(&cars[i]->d) == p32_pbondcar) return car_emu(int(i));
        return 0xdead0000u;
    }

    void put_nav(int i, const drv::WRoadNav& n) {
        drv::WRoadNav c = n;
        c.spline = nav_emu(i) + 0xc0;
        put(e->m, nav_emu(i), &c, 0xc0);
        put(e->m, nav_emu(i) + 0xc0, drv::road_nav_spline(n), 0x64);
    }

    // O estado do porte inteiro na RAM emulada: os carros, os corpos, os motoristas, a Simulation, os globais.
    void put_all() {
        if (heap_mark) e->heap = heap_mark;
        drv::Simulation& sim = drv::simulation();
        e->m.write32(kSim + 0x04, 0);
        e->m.write32(kSim + 0x1c, kBodies);
        e->m.write32(kSim + 0x24, kInfos);
        for (int s = 0; s < drv::kRigidBodySlots; ++s) e->m.write32(kSim + 0x270 + 4u * uint32_t(s), 0);
        e->m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
        std::memcpy(e->m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
        e->m.write32(kSim + 0xdc4, uint32_t(sim.steps));
        e->m.write32(kSim + 0xdcc, kCarList);
        e->m.write32(kSim + 0xdd0, kCarList + 4u * uint32_t(cars.size()));
        for (size_t i = 0; i < cars.size(); ++i) {
            drv::BondCar& car = *cars[i];
            const int slot = car.d.slot;
            e->m.write32(kCarList + 4u * uint32_t(i), car_emu(int(i)));
            e->m.write32(kSim + 0x270 + 4u * uint32_t(slot), car_emu(int(i)));
            drv::RigidBody* rb = car.body();
            drv::RigidBody b = *rb;
            put(e->m, body_emu(slot), &b, 0x80);
            e->m.write32(body_emu(slot) + 0x5c, info_emu(slot));
            drv::RigidBodyInfo info = *rb->info;
            put(e->m, info_emu(slot), &info, 0x500);
            for (int k = 0; k < 16; ++k) e->m.write32(info_emu(slot) + 0x380 + 4u * uint32_t(k), 0);
            e->m.write32(info_emu(slot) + 0x4d8, 0);
            drv::PBondCarData d = car.d;
            put(e->m, car_emu(int(i)), &d, 0x460);
            uint8_t wp[0x40];
            wp_to_emu(car.d.world_pos, *tw, *wc, wp);
            put(e->m, car_emu(int(i)), wp, 0x40);
            for (int w = 0; w < 4; ++w) {
                wp_to_emu(car.d.wheels[w], *tw, *wc, wp);
                put(e->m, car_emu(int(i)) + 0x90 + 0x40u * uint32_t(w), wp, 0x40);
            }
            const uint32_t ca = car_emu(int(i));
            e->m.write32(ca + 0x48, kRender);
            e->m.write32(ca + 0x4c, 0);
            e->m.write32(ca + 0x50, 0);
            e->m.write32(ca + 0x54, car.d.hit_point_loc ? ca + 0x42c : 0);
            e->m.write32(ca + 0x68, kPBondCarVtable);
            put(e->m, kPhys + 0x100u * uint32_t(i), car.physics(), 0x100);
            e->m.write32(ca + 0x23c, kPhys + 0x100u * uint32_t(i));
            e->m.write32(ca + 0x26c, 0);
            e->m.write32(ca + 0x2cc, kTypeNames + 0x40u * uint32_t(i));
            put(e->m, kTypeNames + 0x40u * uint32_t(i), car.name_.c_str(), uint32_t(car.name_.size() + 1));
            e->m.write32(ca + 0x2e0, 0);
            e->m.write32(ca + 0x2e4, ai_addr(car.d.ai_ground_vehicle));
            e->m.write32(ca + 0x388, kAudio);
            e->m.write32(ca + 0x434, 0);
            if (drv::AIGroundVehicle* v = drv::ai_of(car)) {
                uint8_t raw[drv::kAIGroundVehicleSize];
                std::memcpy(raw, v, sizeof raw);
                auto w32 = [&raw](uint32_t off, uint32_t val) { std::memcpy(raw + off, &val, 4); };
                w32(0x04, car_addr_of(v->physics));
                w32(0x50, 0);
                w32(0x54, nav_emu(int(i)));
                w32(0xf0, 0);
                w32(0x100, 0);
                w32(0x110, 0);
                w32(0x114, 0);
                w32(0x1a4, ai_addr(v->primary_target));
                w32(0x200, ai_addr(v->default_target));
                put(e->m, ai_emu(int(i)), raw, sizeof raw);
                put_nav(int(i), *drv::nav_of(*v));
            }
        }
        // O scratchpad do carro e o do corpo rígido.
        drv::BondCarScratch s = drv::bond_car_scratch();
        put(e->m, kCarScratch, &s, 0x450);
        e->m.write32(kCarScratch + 0x2dc, 0);
        e->m.write32(kCarScratch + 0x410, 0);
        e->m.write32(kCarScratchPtr, kCarScratch);
        e->m.write32(kRigidScratchPtr, kRigidScratch);
        // A câmera (a lista do mundo: [0] -> objeto -> matriz; [1] -> objeto com a posição em +0x30) e a missão.
        e->m.write32(kCameraList, kCamList);
        e->m.write32(kCamList, kCamObj);
        e->m.write32(kCamList + 4, kCamObj2);
        e->m.write32(kCamObj, kCamMatrix);
        put(e->m, kCamMatrix, camera, 64);
        put(e->m, kCamObj2 + 0x30, camera[3], 16);
        e->m.write32(kMissionPtr, kMission);
        e->m.write32(kMission + 0x350, uint32_t(drv::traffic_world().mission_camera_check));
        e->m.write32(kMission + 0x3cc, uint32_t(drv::traffic_world().mission_no_traffic));
        // Os globais do AIRoadSpawn e do AIVehicleController.
        const drv::RoadSpawnGlobals& g = drv::road_spawn();
        e->m.write32(kSpawnInstance, g.instance ? 0x01c1f000u : 0);
        e->m.write32(kSpawnNum, uint32_t(g.num_segments));
        e->m.mem(kSpawnFlag, 1)[0] = g.flag60;
        e->m.write32(kSpawnCount, uint32_t(g.spawn_count));
        std::memcpy(e->m.mem(kSpawnMin, 4), &g.min_dist, 4);
        std::memcpy(e->m.mem(kSpawnMax, 4), &g.max_dist, 4);
        e->m.write32(kSpawnForced, uint32_t(g.forced));
        e->m.write32(kSpawnRefresh, uint32_t(g.refresh_step));
        put(e->m, kSpawnList, g.segments, sizeof g.segments);
        const drv::VehicleControllerGlobals& k = drv::vehicle_controller();
        e->m.write32(kCtlInstance, k.instance ? 0x01c1f010u : 0);
        put(e->m, kMaxTraffic, &k.max_active_traffic, 2);
        put(e->m, kNumTraffic, &k.num_active_traffic, 2);
        e->m.write32(kLightCycle, uint32_t(k.light_cycle));
        e->m.write32(kLightNext, uint32_t(k.light_next));
        std::memcpy(e->m.mem(kNavSpread, 4), &k.nav_spread, 4);
        std::memcpy(e->m.mem(kUnspawnZ, 4), &k.unspawn_z, 4);
        put_random(*e);
        put_stamp(*e);
        if (spawn_spline) {
            drv::WRoadNav c = drv::road_spawn_nav();
            c.spline = spawn_spline;
            put(e->m, kSpawnNav, &c, 0xc0);
            put(e->m, spawn_spline, drv::road_nav_spline(drv::road_spawn_nav()), 0x64);
        }
        // Os carimbos dos trechos e as contagens das ruas (o porte também os muda ao sortear as cenas).
        const drv::RoadNetworkGlobals& r = drv::road_network();
        for (size_t i = 0; i < r.segments.size(); ++i)
            put(e->m, rc->to_emu(r.segments[i]) + 0x60, nf::p32_ptr<uint8_t>(r.segments[i]) + 0x60, 8);
        for (size_t i = 0; i < r.roads.size(); ++i)
            put(e->m, rc->to_emu(r.roads[i]), nf::p32_ptr<uint8_t>(r.roads[i]), 0x30);
        // As marcas das consultas ao chão (o porte consulta o chão também para sortear as posições).
        const drv::WCollisionMgr& mgr = tw->mgr();
        e->m.write32(wc->mgr + 4, mgr.query_stamp);
        e->m.write32(wc->mgr + 0x2c, mgr.barrier_mask);
        e->m.write32(wc->mgr + 0x30, mgr.vu0_path);
        for (uint32_t i = 0; i < wc->count; ++i) e->m.write32(wc->insts + 0x40 * i + 0x10, tw->instances()[i].query_stamp);
    }

    // Compara o estado emulado com o do porte; o primeiro campo diferente (vazio: iguais).
    std::string compare(bool scratch) {
        std::string out;
        for (size_t i = 0; i < cars.size(); ++i) {
            drv::BondCar& car = *cars[i];
            const int slot = car.d.slot;
            drv::RigidBody* rb = car.body();
            const std::string tag = " (carro " + std::to_string(i) + ")";
            if (!same_words("RigidBody", e->m.mem(body_emu(slot), 0x80), reinterpret_cast<const uint8_t*>(rb), 0x80,
                            {{0x5c, 0x60}}, out))
                return out + tag;
            // +0x070: a linha 3 do tensor inverso (lixo da pilha do ResetObject no PS2).
            if (!same_words("RigidBodyInfo", e->m.mem(info_emu(slot), 0x500),
                            reinterpret_cast<const uint8_t*>(nf::raw(rb->info)), 0x500,
                            {{0x70, 0x80}, {0x380, 0x3c0}, {0x4d8, 0x4dc}}, out))
                return out + tag;
            const uint8_t* ec = e->m.mem(car_emu(int(i)), 0x460);
            if (!same_words("PBondCar", ec, reinterpret_cast<const uint8_t*>(&car.d), 0x460,
                            {{0x00, 0x40}, {0x48, 0x6c}, {0x74, 0x84}, {0x90, 0x190}, {0x23c, 0x240}, {0x26c, 0x270},
                             {0x2cc, 0x2d0}, {0x2e0, 0x2e8}, {0x388, 0x38c}, {0x434, 0x438}},
                            out))
                return out + tag;
            for (int w = 0; w < 4; ++w) {
                const drv::WWorldPos o = wp_from_emu(ec + 0x90 + 0x40 * w, *tw, *wc);
                if (!same_words(("rodas[" + std::to_string(w) + "]").c_str(), reinterpret_cast<const uint8_t*>(&o),
                                reinterpret_cast<const uint8_t*>(&car.d.wheels[w]), 0x40, {{0x0c, 0x10}}, out))
                    return out + tag;
            }
            if (drv::AIGroundVehicle* v = drv::ai_of(car)) {
                if (!same_words("AIGroundVehicle", e->m.mem(ai_emu(int(i)), drv::kAIGroundVehicleSize),
                                reinterpret_cast<const uint8_t*>(v), drv::kAIGroundVehicleSize,
                                {{0x04, 0x08}, {0x50, 0x58}, {0xf0, 0xf4}, {0x100, 0x104}, {0x110, 0x118},
                                 {0x1a4, 0x1a8}, {0x200, 0x204}},
                                out))
                    return out + tag;
                if (!same_words("WRoadNav do motorista", e->m.mem(nav_emu(int(i)), 0xc0),
                                reinterpret_cast<const uint8_t*>(drv::nav_of(*v)), 0xc0, {{0x90, 0x94}}, out))
                    return out + tag;
            }
        }
        if (scratch) {
            const drv::BondCarScratch& s = drv::bond_car_scratch();
            if (!same_words("BondCarScratch", e->m.mem(kCarScratch, 0x450), reinterpret_cast<const uint8_t*>(&s), 0x450,
                            {{0x2dc, 0x2e0}, {0x410, 0x414}}, out))
                return out;
        }
        const drv::RoadSpawnGlobals& g = drv::road_spawn();
        auto eq = [&](const char* what, uint32_t a, uint32_t v) {
            if (out.empty() && e->m.read32(a) != v)
                out = std::string(what) + ": original " + hex(e->m.read32(a)) + ", porte " + hex(v);
        };
        eq("AIRoadSpawn 0x327d5c", kSpawnNum, uint32_t(g.num_segments));
        eq("AIRoadSpawn 0x327d64", kSpawnCount, uint32_t(g.spawn_count));
        eq("AIRoadSpawn 0x327d68", kSpawnMin, ps2::fbits(g.min_dist));
        eq("AIRoadSpawn 0x327d6c", kSpawnMax, ps2::fbits(g.max_dist));
        eq("AIRoadSpawn 0x327d70", kSpawnForced, uint32_t(g.forced));
        eq("AIRoadSpawn 0x3c8278", kSpawnRefresh, uint32_t(g.refresh_step));
        for (int i = 0; i < 50; ++i) eq("a lista dos trechos", kSpawnList + 4u * uint32_t(i), uint32_t(g.segments[i]));
        const drv::VehicleControllerGlobals& k = drv::vehicle_controller();
        uint16_t n16 = 0;
        std::memcpy(&n16, e->m.mem(kNumTraffic, 2), 2);
        if (out.empty() && int16_t(n16) != k.num_active_traffic)
            out = "fNumActiveTraffic: original " + std::to_string(int16_t(n16)) + ", porte " +
                  std::to_string(k.num_active_traffic);
        eq("o ciclo dos sinais", kLightCycle, uint32_t(k.light_cycle));
        eq("a troca dos sinais", kLightNext, uint32_t(k.light_next));
        eq("0x327c38 (SetNavigateMode)", kNavSpread, ps2::fbits(k.nav_spread));
        eq("0x327c3c (UnSpawn)", kUnspawnZ, ps2::fbits(k.unspawn_z));
        if (!out.empty()) return out;
        if (!same_random(*e, out)) return out;
        if (!same_stamp(*e, out)) return out;
        if (spawn_spline && !same_words("o WRoadNav estático", e->m.mem(kSpawnNav, 0xc0),
                                        reinterpret_cast<const uint8_t*>(&drv::road_spawn_nav()), 0xc0, {{0x90, 0x94}}, out))
            return out;
        const drv::RoadNetworkGlobals& r = drv::road_network();
        for (size_t i = 0; i < r.roads.size(); ++i)
            if (!same_words("rua", e->m.mem(rc->to_emu(r.roads[i]), 0x30), nf::p32_ptr<uint8_t>(r.roads[i]), 0x30, {},
                            out))
                return out + " (rua " + std::to_string(i) + ")";
        return out;
    }
};

void hook_fleet(TrafficEmu& e, Fleet& f) {
    auto counter = [](uint64_t& c) {
        return [&c](MipsMachine& mm) {
            ++c;
            mm.set_gpr(R::v0, 0);
        };
    };
    e.m.hook(kFnFake, "o render falso (a tabela virtual do RSceneObj)", counter(f.render_calls));
    for (uint32_t off = 0; off < 0x200; off += 8) {
        e.m.write32(kRenderVt + off, 0);
        e.m.write32(kRenderVt + off + 4, kFnFake);
    }
    e.m.write32(kRender + 0x3c, kRenderVt);
    e.m.hook(0x1c7f10, "RSceneObj::Reset", counter(f.render_calls));
    e.m.hook(0x1d2bd8, "RVehicle::SetEMPVictimEffect", counter(f.render_calls));
    e.m.hook(0x1870b0, "PBondCar::InitTyreTracks", counter(f.render_calls));
    e.m.hook(0x2def58, "AVehicle::Reset", counter(f.audio_calls));
    e.m.hook(0x1bbc58, "RPlayerCamera::ResetCamera", counter(f.other_calls));
    e.m.hook(0x149d30, "ECollision::ECollision", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
    e.m.hook(0x175730, "Event::operator_new", [&f](MipsMachine& mm) {
        ++f.other_calls;
        mm.set_gpr(R::v0, 0x01c1e000);
    });
    e.m.hook(0x2a7f34, "memmove", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
        if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    e.m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
    e.m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
    e.m.hook(0x2a9198, "strcasecmp", [](MipsMachine& mm) {
        const char* a = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a0)), 1));
        const char* b = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        int r = 0;
        for (;; ++a, ++b) {
            const int x = std::tolower(static_cast<unsigned char>(*a)), y = std::tolower(static_cast<unsigned char>(*b));
            if (x != y || !x) {
                r = x - y;
                break;
            }
        }
        mm.set_gpr(R::v0, uint64_t(int64_t(r)));
    });
    // AttributeSet::Lookup* sobre o +0x5c de cada carro: o valor da coleção do carro no porte.
    auto car_of_attr = [&f](MipsMachine& mm) -> drv::BondCar* {
        const uint32_t a = uint32_t(mm.gpr(R::a0)) - 0x5c;
        for (size_t i = 0; i < f.cars.size(); ++i)
            if (car_emu(int(i)) == a) return f.cars[i];
        return nullptr;
    };
    e.m.hook(0x169d80, "AttributeSet::LookupFloat", [car_of_attr](MipsMachine& mm) {
        drv::BondCar* c = car_of_attr(mm);
        const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        mm.set_fpr(0, c ? c->lookup_float(key) : 0.0f);
    });
    e.m.hook(0x169cb8, "AttributeSet::LookupUInt", [car_of_attr](MipsMachine& mm) {
        drv::BondCar* c = car_of_attr(mm);
        const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        mm.set_gpr(R::v0, c && std::strcmp(key, "TYRE_DAMAGE_POINTS") == 0 ? c->tyre_damage_points_attr : 0);
    });
    e.m.hook(0x169b28, "AttributeSet::LookupBool", [car_of_attr](MipsMachine& mm) {
        drv::BondCar* c = car_of_attr(mm);
        const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        bool v = false;
        if (c && std::strcmp(key, "IS_HELI") == 0) v = c->is_heli;
        if (c && std::strcmp(key, "HAS_SIREN") == 0) v = c->has_siren;
        mm.set_gpr(R::v0, v ? 1 : 0);
    });
}

// A frota: o Vanquish do jogador (classe 0) e cinco carros do tráfego (classe 3: os três tipos de Paris) montados pelo
// construtor do porte e registrados como no GameLoop_StartUp (RegisterSimCarList: o tráfego vai para longe pelo
// UnSpawn). Devolve falso com a mensagem em `error`.
bool build_fleet(Fleet& f, const nf::DiscSource& disc, std::string& error) {
    drv::Bigf arc;
    if (!arc.open(disc, "DRIVING/MIS01.VIV")) {
        error = "o MIS01.VIV não abriu";
        return false;
    }
    drv::reset_rigid_globals();
    drv::reset_bond_car_tuning();
    drv::physics_pending_reset();
    drv::simulation_reset(60);
    drv::SimRandom_Reset(drv::sim_random());
    drv::AIVehicleController_Init();
    drv::AIRoadSpawn_Init();
    const struct {
        const char* name;
        int cls;
    } kCarsToBuild[] = {{"vanquish", 0}, {"hatchback", 3}, {"sedan2", 3}, {"suv1", 3}, {"hatchback", 3}, {"suv1", 3}};
    for (const auto& c : kCarsToBuild) {
        drv::AttributeCollection attrs;
        if (!attrs.load(arc, "pvehicle", c.name, "paris_mis01", &error)) return false;
        drv::CarDesc desc;
        if (!drv::read_car_desc(arc, c.name, desc, &error)) return false;
        drv::BondCar* car = new drv::BondCar();
        const float dir[3] = {0.0f, 0.0f, 1.0f}, pos[3] = {0.0f, 1.0f, 0.0f};
        if (!drv::PBondCar_Construct(*car, c.cls, c.name, desc, attrs, dir, pos)) {
            error = "sem vaga para o corpo";
            return false;
        }
        drv::simulation().cars.push_back(car);
        f.cars.push_back(car);
    }
    drv::AIVehicleController_RegisterSimCarList();
    return true;
}

// Um ponto sobre um trecho sorteado da rede (com o deslocamento de través), a altura do chão e a frente do trecho.
void road_spot(Rng& rng, drv::TrackWorld& tw, float pos[3], float fwd[3], float spread) {
    const drv::RoadNetworkGlobals& r = drv::road_network();
    const drv::WRoadSegment* s = drv::road_segment(rng.range(0, r.num_segments - 1));
    float p[4] = {0, 0, 0, 0};
    drv::WRoadNetwork_GetPointOnSegmentT(s, rng.uni(0, 1), p);
    pos[0] = p[0] + rng.uni(-spread, spread);
    pos[2] = p[2] + rng.uni(-spread, spread);
    pos[1] = p[1] + 1.0f;
    float h = 0;
    if (tw.world_height_at_point(pos, h)) pos[1] = h + rng.uni(0.5f, 1.2f);
    const bool back = rng.chance(0.5);
    for (int i = 0; i < 3; ++i) fwd[i] = back ? -s->forward[i] : s->forward[i];
}

void yaw_quat(float yaw, float pitch, float roll, float q[4]) {
    const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f), cp = std::cos(pitch * 0.5f),
                sp = std::sin(pitch * 0.5f), cr = std::cos(roll * 0.5f), sr = std::sin(roll * 0.5f);
    q[0] = cy * sp * cr + sy * cp * sr;
    q[1] = sy * cp * cr - cy * sp * sr;
    q[2] = cy * cp * sr - sy * sp * cr;
    q[3] = cy * cp * cr + sy * sp * sr;
}

// Um carro numa rua (ou no ar, ou de lado), andando.
void place_car(Rng& rng, drv::BondCar& car, drv::TrackWorld& tw, float spread) {
    drv::RigidBody* rb = car.body();
    float pos[3], fwd[3];
    road_spot(rng, tw, pos, fwd, spread);
    std::memcpy(rb->pos, pos, 12);
    float q[4];
    const float yaw = std::atan2(fwd[0], fwd[2]) + (rng.chance(0.7) ? rng.uni(-0.3f, 0.3f) : rng.uni(-3.1f, 3.1f));
    yaw_quat(yaw, rng.uni(-0.08f, 0.08f), rng.chance(0.05) ? rng.uni(1.5f, 3.1f) : rng.uni(-0.08f, 0.08f), q);
    drv::SetOrientation(rb, q);
    drv::RigidBodyInfo* info = rb->info;
    for (int i = 0; i < info->lever_count; ++i) {
        float l[4];
        std::memcpy(l, info->lever_local[i], 16);
        drv::ConvertLocalToWorld(rb, l);
        std::memcpy(info->lever_world[i], l, 16);
    }
    const float speed = rng.chance(0.2) ? rng.uni(0, 1.2f) : rng.uni(-4, 20);
    for (int i = 0; i < 3; ++i) rb->vel[i] = info->orient[2][i] * speed + rng.uni(-1, 1);
    rb->vel[1] = rng.uni(-1, 1);
    for (int i = 0; i < 3; ++i) rb->ang_vel[i] = rng.chance(0.3) ? rng.uni(-2, 2) : rng.uni(-0.2f, 0.2f);
    for (int i = 0; i < 3; ++i) {
        rb->momentum[i] = rb->vel[i] * rb->mass;
        rb->ang_momentum[i] = rng.uni(-1000, 1000);
        rb->force[i] = rng.uni(-50, 50);
        rb->torque[i] = rng.uni(-50, 50);
    }
    rb->state = 2;
    rb->ground_levers = int8_t(rng.range(0, 4));
    info->age = uint16_t(rng.chance(0.3) ? rng.range(0, 40) : rng.range(41, 400));
    info->flags[1] = uint8_t(rng.chance(0.05));
    info->flags[2] = uint8_t(rng.chance(0.05));
    drv::UpdateZonalInfo(rb);
}

// O estado sorteado de um carro (os controles, as rodas, o choque, os pneus) e do seu motorista.
void random_car_state(Rng& rng, drv::BondCar& car) {
    drv::PBondCarData& c = car.d;
    c.control.gas = rng.chance(0.3) ? 0.0f : rng.uni(0, 1);
    c.control.brake = rng.chance(0.6) ? 0.0f : rng.uni(0, 1);
    c.control.steering = rng.chance(0.3) ? 0.0f : rng.uni(-1, 1);
    c.control.hand_brake = uint8_t(rng.chance(0.15));
    c.num_wheels_on_ground = int8_t(rng.range(0, 4));
    for (int w = 0; w < 4; ++w) c.suspension[w] = rng.chance(0.3) ? 0.0f : rng.uni(0, 0.4f);
    for (int w = 0; w < 4; ++w) c.tyre_damage_points[w] = rng.chance(0.05) ? 0u : car.tyre_damage_points_attr;
    c.car_wheel_speed = rng.uni(-0.06f, 0.06f);
    c.two_wheel_mode = uint8_t(rng.chance(0.03));
    c.oil_slick = uint8_t(rng.chance(0.05));
    c.landing_flag = uint8_t(rng.chance(0.5) ? rng.range(0, 20) : rng.range(0, 255));
    c.landing_fx_timer = uint8_t(rng.chance(0.8) ? 0 : rng.range(1, 30));
    c.in_shock = int8_t(rng.chance(0.85) ? 0 : rng.range(1, 60));
    c.emp_active = rng.chance(0.03) ? 1 : 0;
    c.wheel_spin_angle[0] = rng.uni(-0.2f, 1.2f);
    c.wheel_spin_angle[1] = rng.uni(-0.2f, 1.2f);
    c.landing_impact = rng.uni(0, 0.2f);
    c.hit_points = rng.chance(0.05) ? rng.uni(-50, 0.6f) : 700.0f;
    for (int g = 0; g < 16; ++g) c.glare_on[g] = rng.chance(0.3) ? 1 : 0;
}

void random_driver(Rng& rng, drv::AIGroundVehicle& v, drv::BondCar& car) {
    v.attack_mode = rng.chance(0.8) ? 12 : 1;
    v.navigate_mode = rng.chance(0.85) ? 3 : 1;
    v.speed = rng.uni(0, 12);
    v.update_state = rng.chance(0.7) ? 0 : 1;
    v.shocked = rng.chance(0.1) ? 1 : 0;
    v.red_light = rng.chance(0.15) ? 1 : 0;
    v.collision_ahead = rng.chance(0.15) ? 1 : 0;
    v.stop_now = rng.chance(0.1) ? 1 : 0;
    v.can_spawn = rng.chance(0.1) ? 1 : 0;
    v.min_sim_update = rng.chance(0.08) ? 1 : 0;
    v.coll_avoidance = rng.chance(0.9) ? 1 : 0;
    v.disabled = uint8_t(rng.chance(0.03));
    v.force_full_update = rng.chance(0.05) ? 1 : 0;
    v.spawn_timer = drv::simulation().steps + (rng.chance(0.5) ? -rng.range(0, 400) : rng.range(0, 400));
    for (int i = 0; i < 3; ++i) v.target_pos[i] = car.body()->pos[i] + rng.uni(-40, 40);
    v.speed_limit = rng.uni(0, 30);
    v.unknown1d0 = rng.uni(0, 20);
    v.min_dist_to_target = rng.uni(0, 20);
    for (int i = 0; i < 3; ++i) v.direction[i] = rng.uni(-1, 1);
    v.unknown1fc = int16_t(rng.chance(0.8) ? -1 : rng.range(0, drv::road_network().num_segments - 1));
    const drv::RoadNetworkGlobals& r = drv::road_network();
    v.road = int16_t(rng.chance(0.3) ? -1 : rng.range(0, r.num_roads - 1));
    // O navegador onde o carro está (o tráfego anda no modo 1), às vezes já avançado.
    drv::WRoadNav& nav = *drv::nav_of(v);
    drv::RigidBody* rb = car.body();
    nav.mode = 1;
    drv::WRoadNav_InitAtPoint(nav, rb->pos, rb->info->orient[2], false);
    if (nav.segment < 0) drv::WRoadNav_InitAtSegment(nav, int16_t(rng.range(0, r.num_segments - 1)), 1, 0.5f, true);
    nav.at_dead_end = uint8_t(rng.chance(0.05));
    if (v.navigate_mode == 1) nav.mode = 0;
}

// Uma cena: o jogador e os carros do tráfego nas ruas, a câmera atrás do jogador, os sinais, o relógio.
void random_scene(Rng& rng, Fleet& f) {
    drv::Simulation& sim = drv::simulation();
    sim.steps = rng.range(0, 100000);
    // O jogador numa rua; os outros perto dele (para se verem) ou longe.
    place_car(rng, *f.cars[0], *f.tw, 4.0f);
    for (size_t i = 1; i < f.cars.size(); ++i) {
        drv::BondCar& car = *f.cars[i];
        place_car(rng, car, *f.tw, 3.0f);
        if (rng.chance(0.5)) {
            // Perto do jogador (até uns 40 m), na rua dele ou atrás dele.
            drv::RigidBody* p = f.cars[0]->body();
            drv::RigidBody* rb = car.body();
            const float d = rng.uni(4, 40);
            for (int k = 0; k < 3; k += 2) rb->pos[k] = p->pos[k] + p->info->orient[2][k] * d + rng.uni(-3, 3);
            rb->pos[1] = p->pos[1] + rng.uni(-0.5f, 0.5f);
            drv::UpdateZonalInfo(rb);
        }
        if (rng.chance(0.15)) car.body()->state = 3;
        random_car_state(rng, car);
        if (drv::AIGroundVehicle* v = drv::ai_of(car)) random_driver(rng, *v, car);
    }
    random_car_state(rng, *f.cars[0]);
    // A câmera: atrás e acima do jogador, olhando para a frente dele.
    const drv::RigidBody* p = f.cars[0]->body();
    float fw[3] = {p->info->orient[2][0], 0.0f, p->info->orient[2][2]};
    const float n = std::sqrt(fw[0] * fw[0] + fw[2] * fw[2]);
    if (n > 0) {
        fw[0] /= n;
        fw[2] /= n;
    } else {
        fw[2] = 1;
    }
    std::memset(f.camera, 0, sizeof f.camera);
    f.camera[0][0] = fw[2];
    f.camera[0][2] = -fw[0];
    f.camera[1][1] = 1;
    f.camera[2][0] = fw[0];
    f.camera[2][2] = fw[2];
    for (int k = 0; k < 3; ++k) f.camera[3][k] = p->pos[k] - fw[k] * 6.0f + (k == 1 ? 2.5f : 0.0f);
    f.camera[3][3] = 1;
    drv::traffic_world().camera = f.camera;
    drv::traffic_world().mission_camera_check = rng.chance(0.2) ? 1 : 0;
    drv::traffic_world().mission_no_traffic = rng.chance(0.05) ? 1 : 0;
    drv::VehicleControllerGlobals& k = drv::vehicle_controller();
    k.light_cycle = rng.range(0, 50);
    k.light_next = sim.steps + rng.range(-20, 320);
    k.max_active_traffic = int16_t(rng.chance(0.5) ? 0 : rng.range(0, 6));
    k.num_active_traffic = int16_t(rng.range(0, 6));
    drv::RoadSpawnGlobals& g = drv::road_spawn();
    g.forced = rng.chance(0.1) ? 1 : 0;
    g.spawn_count = rng.range(0, 1000);
    g.refresh_step = sim.steps + (rng.chance(0.5) ? -rng.range(1, 100) : rng.range(0, 100));
}

// c) O nascimento do tráfego (AIRoadSpawn) e os dois testes do motorista que não mexem em nada.
int check_spawn(TrafficEmu& e, Fleet& f, int cases, Rng& rng) {
    int failures = 0;
    Tally t_refresh, t_pos, t_avail, t_point, t_random, t_green, t_test, t_stop;
    uint64_t listed = 0, picked = 0, available = 0, green = 0, hits = 0;
    const uint32_t spawn = 0x01c1f000;
    drv::RoadNetworkGlobals& r = drv::road_network();
    // O WRoadNav estático do GetRandomTrafficSpawn: construído dos dois lados (o original no 0x3c8560, guarda ligada).
    e.call(kFnNavCtor, {kSpawnNav});
    e.m.write32(kSpawnNavGuard, 1);
    drv::road_spawn_nav();
    f.spawn_spline = e.m.read32(kSpawnNav + 0x90);
    f.heap_mark = e.heap;
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        std::string why;
        const std::string tag = " (caso " + std::to_string(c) + ")";
        // RefreshSpawnData: forçado (pela câmera) ou pelo relógio.
        {
            ++t_refresh.cases;
            const bool force = rng.chance(0.3);
            f.put_all();
            e.m.set_fpr(12, 75.0f);
            e.m.set_fpr(13, 150.0f);
            e.call(kFnRefresh, {spawn, force ? 1u : 0u});
            drv::AIRoadSpawn_RefreshSpawnData(force, 75.0f, 150.0f);
            why = f.compare(false);
            if (!why.empty()) t_refresh.fail(why + tag);
            if (!same_records(e, *f.rc, why)) t_refresh.fail(why + tag);
            listed += uint64_t(drv::road_spawn().num_segments);
        }
        // As distâncias.
        float pos[4] = {0, 0, 0, 0}, fwd[3];
        road_spot(rng, *f.tw, pos, fwd, 10.0f);
        if (rng.chance(0.3))
            for (int k = 0; k < 3; k += 2) pos[k] = f.cars[0]->body()->pos[k] + rng.uni(-200, 200);
        put(e.m, kScratch, pos, 16);
        {
            ++t_pos.cases;
            const float radius = rng.uni(0, 3);
            f.put_all();
            e.m.set_fpr(12, radius);
            e.call(kFnRespawnPosCheck, {spawn, kScratch});
            const bool rv = drv::AIRoadSpawn_TrafficRespawnPosCheck(pos, radius);
            if ((e.m.gpr(R::v0) != 0) != rv) t_pos.fail("TrafficRespawnPosCheck: o retorno" + tag);
        }
        {
            ++t_avail.cases;
            f.put_all();
            e.m.set_fpr(12, 2.0f);
            e.call(kFnRespawnAvailable, {spawn, kScratch});
            const bool rv = drv::AIRoadSpawn_TrafficRespawnAvailable(pos);
            if ((e.m.gpr(R::v0) != 0) != rv) t_avail.fail("TrafficRespawnAvailable: o retorno" + tag);
            available += rv;
        }
        // O lugar num trecho dado (com uma rua com tráfego às vezes).
        {
            ++t_point.cases;
            // Um trecho com faixas (num sem faixas o PS2 para: break 7 na divisão; o porte conta uma guarda).
            int16_t seg = int16_t(rng.range(0, r.num_segments - 1));
            for (int k = 0; k < 64; ++k) {
                const drv::WRoadSegment* sp = drv::road_segment(seg);
                if (int(sp->left_lanes) + int(sp->right_lanes) != 0) break;
                seg = int16_t(rng.range(0, r.num_segments - 1));
            }
            int8_t lane = int8_t(rng.range(-5, 5));
            float t = rng.uni(0, 1);
            int32_t fw = rng.range(0, 1);
            f.put_all();
            put(e.m, kScratch + 0x10, &seg, 2);
            put(e.m, kScratch + 0x14, &lane, 1);
            put(e.m, kScratch + 0x18, &t, 4);
            put(e.m, kScratch + 0x1c, &fw, 4);
            e.m.set_gpr(R::t0, kScratch + 0x1c);
            e.call(kFnSpawnPoint, {spawn, kScratch + 0x10, kScratch + 0x14, kScratch + 0x18});
            drv::AIRoadSpawn_GetTrafficSpawnPointOnSegment(&seg, &lane, &t, &fw);
            if (int8_t(e.m.mem(kScratch + 0x14, 1)[0]) != lane) t_point.fail("GetTrafficSpawnPointOnSegment: a faixa" + tag);
            if (e.m.read32(kScratch + 0x18) != ps2::fbits(t)) t_point.fail("GetTrafficSpawnPointOnSegment: o t" + tag);
            if (int32_t(e.m.read32(kScratch + 0x1c)) != fw) t_point.fail("GetTrafficSpawnPointOnSegment: o sentido" + tag);
            if (!same_random(e, why)) t_point.fail(why + tag);
        }
        // O sorteio de um lugar livre.
        {
            ++t_random.cases;
            int16_t seg = 0;
            int8_t lane = 0;
            float t = 0;
            int32_t fw = 1;
            f.put_all();
            put(e.m, kScratch + 0x10, &seg, 2);
            put(e.m, kScratch + 0x14, &lane, 1);
            put(e.m, kScratch + 0x18, &t, 4);
            put(e.m, kScratch + 0x1c, &fw, 4);
            e.m.set_gpr(R::t0, kScratch + 0x1c);
            e.call(kFnRandomSpawn, {spawn, kScratch + 0x10, kScratch + 0x14, kScratch + 0x18});
            const bool rv = drv::AIRoadSpawn_GetRandomTrafficSpawn(&seg, &lane, &t, &fw);
            if ((e.m.gpr(R::v0) != 0) != rv) t_random.fail("GetRandomTrafficSpawn: o retorno" + tag);
            int16_t es = 0;
            std::memcpy(&es, e.m.mem(kScratch + 0x10, 2), 2);
            if (es != seg) t_random.fail("GetRandomTrafficSpawn: o trecho" + tag);
            if (int8_t(e.m.mem(kScratch + 0x14, 1)[0]) != lane) t_random.fail("GetRandomTrafficSpawn: a faixa" + tag);
            if (e.m.read32(kScratch + 0x18) != ps2::fbits(t)) t_random.fail("GetRandomTrafficSpawn: o t" + tag);
            why = f.compare(false);
            if (!why.empty()) t_random.fail(why + tag);
            picked += rv;
        }
        // O sinal de um cruzamento.
        {
            ++t_green.cases;
            const int count = rng.range(1, 6), index = rng.range(0, count);
            float p[4] = {pos[0] + rng.uni(-5, 5), pos[1], pos[2] + rng.uni(-5, 5), 0};
            put(e.m, kScratch + 0x20, p, 16);
            f.put_all();
            e.call(kFnGreenLight, {0x01c1f010, kScratch + 0x20, uint32_t(index), uint32_t(count)});
            const bool rv = drv::AIVehicleController_IntersectionGreenLight(p, index, count);
            if ((e.m.gpr(R::v0) != 0) != rv) t_green.fail("IntersectionGreenLight: o retorno" + tag);
            green += rv;
        }
        // TestForCollision e GetCollisionStopPoint com o carro 1 e um ponto e um obstáculo à frente dele.
        {
            drv::BondCar& car = *f.cars[1];
            const drv::RigidBody* rb = car.body();
            float pt[4], ob[4];
            for (int k = 0; k < 3; ++k) {
                pt[k] = rb->pos[k] + rb->info->orient[2][k] * rng.uni(-5, 20) + rng.uni(-3, 3);
                ob[k] = rb->pos[k] + rb->info->orient[2][k] * rng.uni(-5, 20) + rng.uni(-3, 3);
            }
            pt[3] = rng.uni(-5, 5);
            ob[3] = rng.uni(-5, 5);
            const float radius = rng.uni(0, 3);
            f.put_all();
            put(e.m, kScratch + 0x30, pt, 16);
            put(e.m, kScratch + 0x40, ob, 16);
            ++t_test.cases;
            e.m.set_fpr(12, radius);
            e.call(kFnTestCollision, {car_emu(1), kScratch + 0x30, kScratch + 0x40});
            const bool rv = drv::TestForCollision(car, pt, ob, radius);
            if ((e.m.gpr(R::v0) != 0) != rv) t_test.fail("TestForCollision: o retorno" + tag);
            hits += rv;
            ++t_stop.cases;
            e.m.set_fpr(12, radius);
            e.call(kFnStopPoint, {car_emu(1), kScratch + 0x30, kScratch + 0x40});
            drv::GetCollisionStopPoint(car, pt, ob, radius);
            if (!same_words("GetCollisionStopPoint", e.m.mem(kScratch + 0x30, 16), reinterpret_cast<const uint8_t*>(pt), 16,
                            {}, why))
                t_stop.fail(why + tag);
        }
    }
    report("AIRoadSpawn::RefreshSpawnData (a lista em volta, a grade)", t_refresh, failures);
    report("AIRoadSpawn::TrafficRespawnPosCheck", t_pos, failures);
    report("AIRoadSpawn::TrafficRespawnAvailable (a câmera)", t_avail, failures);
    report("AIRoadSpawn::GetTrafficSpawnPointOnSegment", t_point, failures);
    report("AIRoadSpawn::GetRandomTrafficSpawn (o navegador estático)", t_random, failures);
    report("AIVehicleController::IntersectionGreenLight", t_green, failures);
    report("TestForCollision", t_test, failures);
    report("GetCollisionStopPoint", t_stop, failures);
    std::printf("  (trechos listados %" PRIu64 ", lugares sorteados %" PRIu64 ", carros que podem sumir %" PRIu64
                ", sinais verdes %" PRIu64 ", à frente %" PRIu64 ")\n",
                listed, picked, available, green, hits);
    return failures;
}

// d) O carro do tráfego: ProcessSimplePhysics (e AddSimpleWheelForces) e ResetCar.
int check_simple_car(TrafficEmu& e, Fleet& f, int cases, Rng& rng) {
    int failures = 0;
    Tally t_phys, t_reset;
    uint64_t grounded = 0, copied = 0;
    drv::BondCarScratch& s = drv::bond_car_scratch();
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        const std::string tag = " (caso " + std::to_string(c) + ")";
        const int i = rng.range(1, int(f.cars.size()) - 1);
        drv::BondCar& car = *f.cars[size_t(i)];
        if (rng.chance(0.15)) car.d.car_class = rng.chance(0.5) ? 1 : 2;
        car.body()->state = 2;
        {
            ++t_phys.cases;
            std::memcpy(&s.physics, car.physics(), sizeof(drv::CarPhysics));
            const uint32_t saved = car.d.physics;
            car.d.physics = nf::p32_u(&s.physics);
            f.put_all();
            e.m.write32(car_emu(i) + 0x23c, kCarScratch);
            e.call(kFnSimplePhysics, {car_emu(i)});
            drv::PBondCar_ProcessSimplePhysics(car);
            car.d.physics = saved;
            e.m.write32(car_emu(i) + 0x23c, kPhys + 0x100u * uint32_t(i));
            const std::string why = f.compare(true);
            if (!why.empty()) t_phys.fail(why + tag);
            grounded += car.d.num_wheels_on_ground == 4;
            copied += (car.body()->radius_flags & 1u) == 0 && car.d.car_class == 3;
        }
        {
            ++t_reset.cases;
            float pos[4], fwd[4] = {0, 0, 0, 0};
            road_spot(rng, *f.tw, pos, fwd, 2.0f);
            pos[3] = 0;
            if (rng.chance(0.2)) pos[1] += rng.uni(-5, 5);
            f.put_all();
            put(e.m, kScratch, pos, 16);
            put(e.m, kScratch + 0x10, fwd, 16);
            e.call(kFnResetCarPos, {car_emu(i), kScratch, kScratch + 0x10});
            drv::PBondCar_ResetCar(car, pos, fwd);
            std::string why = f.compare(false);
            if (why.empty() && !same_words("ResetCar: a posição", e.m.mem(kScratch, 12), reinterpret_cast<const uint8_t*>(pos),
                                           12, {}, why)) {
            }
            if (!why.empty()) t_reset.fail(why + tag);
        }
        car.d.car_class = 3;
    }
    report("PBondCar::ProcessSimplePhysics (e AddSimpleWheelForces, Paris)", t_phys, failures);
    report("PBondCar::ResetCar(COORD3 &, COORD3 &) (e RigidBody::ResetObject)", t_reset, failures);
    std::printf("  (quatro rodas no chão %" PRIu64 ", rodas da direita copiadas %" PRIu64 ")\n", grounded, copied);
    return failures;
}

// e) O passo do motorista: AIGroundVehicle::Update de um carro do tráfego (o estado 4), com os outros por perto.
int check_driver(TrafficEmu& e, Fleet& f, int cases, Rng& rng) {
    int failures = 0;
    Tally t_update;
    const drv::GroundVehicleStats before = drv::ground_vehicle_stats();
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        const std::string tag = " (caso " + std::to_string(c) + ")";
        const int i = rng.range(1, int(f.cars.size()) - 1);
        drv::AIGroundVehicle* v = drv::ai_of(*f.cars[size_t(i)]);
        if (!v) continue;
        // Às vezes vários passos seguidos do mesmo carro (o estado anda).
        const int steps = rng.chance(0.3) ? rng.range(2, 12) : 1;
        f.put_all();
        for (int k = 0; k < steps; ++k) {
            ++t_update.cases;
            e.call(kFnAIUpdate, {ai_emu(i)});
            drv::AIGroundVehicle_Update(*v);
            const std::string why = f.compare(false);
            if (!why.empty()) {
                t_update.fail(why + tag + " passo " + std::to_string(k));
                break;
            }
        }
    }
    const drv::GroundVehicleStats& after = drv::ground_vehicle_stats();
    report("AIGroundVehicle::Update (o tráfego: o motorista inteiro)", t_update, failures);
    std::printf("  (nascimentos %" PRIu64 ", sumiços %" PRIu64 ", sinais vermelhos %" PRIu64 ", atrás de outro carro %" PRIu64
                "; render %" PRIu64 ", som %" PRIu64 ", guardas %" PRIu64 ")\n",
                after.spawns - before.spawns, after.unspawns - before.unspawns, after.red_lights - before.red_lights,
                after.blocked - before.blocked, f.render_calls, f.audio_calls, after.guards - before.guards);
    return failures;
}

// f) Os gatilhos da pista (WTriggerManager) e o ESpawnTraffic.
constexpr uint32_t kFnTrigInit = 0x22cbe0, kFnTrigRestart = 0x22cd30, kFnTrigUpdate = 0x22f0c0;
constexpr uint32_t kFnTrigProcess = 0x22d240, kFnTrigCheck = 0x22eb28;
constexpr uint32_t kFnSpawnTrafficDtor = 0x15c080, kSpawnTrafficVtable = 0x370408;
constexpr uint32_t kFnLookupEvent = 0x1757b8, kFnPrintf = 0x2a8148;
constexpr uint32_t kTrigManagerPtr = 0x33aa88, kTrigSizeG = 0x33aa8c, kTrigDataG = 0x33aa90, kTrigStamp = 0x33aa98;
constexpr uint32_t kEventData = 0x32e708, kTrafficStatic = 0x3282d0;
constexpr uint32_t kTrigHeader = 0x01d00000, kFakeMap = 0x01d00010, kRootArray = 0x01d00040;
constexpr uint32_t kSpawnEvent = 0x01d00080, kFnEventSink = 0x01c1c100;
constexpr uint32_t kTrigBegin = 0x01d00100, kTrigEnd = 0x01e00000;
constexpr uint32_t kTagMap = 0x4d617020, kTagTrgr = 0x54726772, kTagEl = 0x656c2020;
constexpr uint32_t kRayCount = 0x332950, kRayActive = 0x332960, kRayIndex = 0x333560;  // os raios ativos (RayShell)

// Os gatilhos de Paris na RAM emulada: os registros (os eventos trocados pelas cópias das listas 'el').
struct TriggerCopy {
    drv::UData* trgr = nullptr;
    uint32_t data = 0;
    int count = 0;
    std::map<uint32_t, uint32_t> lists;  // a lista 'el' do porte (p32) -> o endereço emulado
    uint32_t list_emu(uint32_t p32) const {
        if (!p32) return 0;
        auto it = lists.find(p32);
        return it == lists.end() ? 0xdead0000u : it->second;
    }
    uint32_t trig_emu(int i) const { return data + 0x40u * uint32_t(i); }
    int index_emu(uint32_t a) const { return a ? int((a - data) / 0x40u) : -1; }
    int index_port(uint32_t p32) const {
        return p32 ? int((p32 - drv::trigger_manager().data) / 0x40u) : -1;
    }
    // Os dados de um evento (dentro de uma lista 'el' do porte) na cópia emulada da lista.
    uint32_t data_emu(uint32_t p32) const {
        auto it = lists.upper_bound(p32);
        if (it == lists.begin()) return 0xdead0000u;
        --it;
        return it->second + (p32 - it->first);
    }
};

void collect_event_lists(drv::UGroup* g, std::map<uint32_t, uint32_t>& out, int depth) {
    if (depth > 24) return;
    drv::UData* arr = drv::UGroup_GetArray(g);
    const uint32_t groups = drv::UGroup_GroupCount(g);
    for (uint32_t i = 0; i < groups; ++i) collect_event_lists(arr + i, out, depth + 1);
    for (drv::UData* d = arr + groups; d < arr + groups + g->count; ++d)
        if (drv::UData_MatchTag(d) == kTagEl) out[nf::p32_u(drv::UData_Data(d))] = drv::UData_Size(d);
}

bool copy_triggers(TrafficEmu& e, drv::Track& track, const RoadCopy& rc, TriggerCopy& tc, std::string& error) {
    drv::UGroup* root = track.root();
    drv::UGroup* map = drv::UGroup_GroupLocateTag(root, kTagMap);
    if (map == drv::UGroup_GroupEnd(root)) {
        error = "a pista não tem 'Map '";
        return false;
    }
    drv::UData* trgr = drv::UGroup_DataLocateTag(map, kTagTrgr);
    if (trgr == drv::UGroup_DataEnd(map)) {
        error = "a pista não tem 'Trgr'";
        return false;
    }
    tc.trgr = trgr;
    tc.count = int(trgr->count);
    const uint8_t* src = drv::UData_Data(trgr);
    const uint32_t size = drv::UData_Size(trgr);
    if (size != 0x40u * uint32_t(tc.count)) {
        error = "o 'Trgr' não tem 0x40 bytes por gatilho";
        return false;
    }
    std::map<uint32_t, uint32_t> sizes;
    collect_event_lists(root, sizes, 0);
    tc.data = kTrigBegin;
    uint32_t at = kTrigBegin + size;
    for (int i = 0; i < tc.count; ++i) {
        const drv::WTrigger* t = reinterpret_cast<const drv::WTrigger*>(src) + i;
        if (!t->events || tc.lists.count(t->events)) continue;
        auto it = sizes.find(t->events);
        if (it == sizes.end()) {
            error = "a lista de eventos do gatilho " + std::to_string(i) + " não é um 'el'";
            return false;
        }
        const uint32_t a = ((at + 15) & ~15u) + (t->events & 15u);
        if (a + it->second + 16 > kTrigEnd) {
            error = "as listas de eventos não cabem na RAM emulada";
            return false;
        }
        put(e.m, a, nf::p32_ptr<uint8_t>(t->events), it->second);
        tc.lists[t->events] = a;
        at = a + it->second;
    }
    for (int i = 0; i < tc.count; ++i) {
        drv::WTrigger t = reinterpret_cast<const drv::WTrigger*>(src)[i];
        t.events = tc.list_emu(t.events);
        put(e.m, tc.trig_emu(i), &t, 0x40);
    }
    // O cabeçalho do 'Trgr' para o Init original, e um 'Map ' vazio na raiz falsa (o Restart procura o 'Rule' nele).
    drv::UData h = *trgr;
    h.flags = size << 8;
    h.offset = tc.data;
    put(e.m, kTrigHeader, &h, 16);
    drv::UData mh{};
    mh.tag = kTagMap;
    mh.offset = kFakeMap + 0x20;
    put(e.m, kFakeMap, &mh, 16);
    std::memset(e.m.mem(kFakeMap + 0x20, 0x20), 0, 0x20);
    put(e.m, kRootArray, &mh, 16);
    std::memcpy(e.m.mem(kRootArray + 16, 16), e.m.mem(rc.root - 16, 16), 16);
    drv::UData rh{};
    std::memcpy(&rh, e.m.mem(rc.root, 16), 16);
    rh.offset = kRootArray;
    rh.flags = (2u << 5) | (rh.flags & 0x1fu);
    put(e.m, rc.root, &rh, 16);
    return true;
}

void put_triggers(TrafficEmu& e, const TriggerCopy& tc) {
    for (int i = 0; i < tc.count; ++i) {
        drv::WTrigger t = *drv::trigger_at(i);
        t.events = tc.list_emu(t.events);
        put(e.m, tc.trig_emu(i), &t, 0x40);
    }
}

// Os gatilhos, os globais do WTriggerManager, o EventDynamicData e os eventos criados dos dois lados.
std::string compare_triggers(TrafficEmu& e, const TriggerCopy& tc, const std::vector<drv::PendingEvent>& emu_events) {
    std::string out;
    const drv::TriggerManagerGlobals& g = drv::trigger_manager();
    const uint32_t mgr = e.m.read32(kTrigManagerPtr);
    auto eq = [&](const char* what, uint32_t a, uint32_t b) {
        if (out.empty() && a != b) out = std::string(what) + ": original " + hex(a) + ", porte " + hex(b);
    };
    eq("o gerente: a contagem", e.m.read32(mgr), uint32_t(g.count));
    eq("o gerente: os gatilhos", e.m.read32(mgr + 4), g.triggers ? tc.data : 0);
    eq("o gerente: ativo", e.m.read32(mgr + 8), uint32_t(g.active));
    eq("TriggerDataSize", e.m.read32(kTrigSizeG), g.data_size);
    eq("TriggerData", e.m.read32(kTrigDataG), g.data ? tc.data : 0);
    eq("QueryStamp", e.m.read32(kTrigStamp), g.query_stamp);
    if (!out.empty()) return out;
    for (int i = 0; i < tc.count; ++i)
        if (!same_words("WTrigger", e.m.mem(tc.trig_emu(i), 0x40), reinterpret_cast<const uint8_t*>(drv::trigger_at(i)),
                        0x40, {{0x18, 0x1c}}, out))
            return out + " (gatilho " + std::to_string(i) + ")";
    if (!same_words("EventDynamicData", e.m.mem(kEventData, 0x64), reinterpret_cast<const uint8_t*>(&g.event_data), 0x64,
                    {{0x30, 0x34}}, out))
        return out;
    if (tc.index_emu(e.m.read32(kEventData + 0x30)) != tc.index_port(g.event_data.trigger))
        return "EventDynamicData +0x30: o gatilho " + std::to_string(tc.index_emu(e.m.read32(kEventData + 0x30))) +
               " no original, " + std::to_string(tc.index_port(g.event_data.trigger)) + " no porte";
    if (emu_events.size() != g.pending.size())
        return "eventos criados: " + std::to_string(emu_events.size()) + " no original, " +
               std::to_string(g.pending.size()) + " no porte";
    for (size_t i = 0; i < emu_events.size(); ++i) {
        const drv::PendingEvent& a = emu_events[i];
        const drv::PendingEvent& b = g.pending[i];
        if (a.type != b.type || tc.index_emu(a.trigger) != tc.index_port(b.trigger) ||
            ps2::fbits(a.speed) != ps2::fbits(b.speed) || a.flag != b.flag || a.index != b.index ||
            a.data != tc.data_emu(b.data))
            return "o evento " + std::to_string(i) + ": tipo " + std::to_string(a.type) + " gatilho " +
                   std::to_string(tc.index_emu(a.trigger)) + " no original, tipo " + std::to_string(b.type) + " gatilho " +
                   std::to_string(tc.index_port(b.trigger)) + " no porte";
    }
    return out;
}

// Um corpo perto de um gatilho (dentro do alcance ou um pouco fora), andando para qualquer lado.
void near_trigger(Rng& rng, drv::RigidBody* rb, const drv::WTrigger& t) {
    float reach = t.radius + 3.0f;
    if (t.shape == 1) reach = std::max(reach, 0.5f * std::max(t.width, t.depth) + 3.0f);
    reach = std::min(reach, 200.0f);
    const float a = rng.uni(-3.14159f, 3.14159f), d = rng.uni(0, reach * 1.2f);
    rb->pos[0] = t.position[0] + std::cos(a) * d;
    rb->pos[2] = t.position[2] + std::sin(a) * d;
    rb->pos[1] = t.position[1] + rng.uni(-3.0f, std::max(3.0f, t.height + 3.0f));
    for (int i = 0; i < 3; ++i) rb->vel[i] = rng.chance(0.1) ? 0.0f : rng.uni(-20, 20);
}

void mutate_trigger(Rng& rng, drv::WTrigger& t) {
    if (rng.chance(0.3)) t.flags ^= drv::kTrigEnabled;
    if (rng.chance(0.2)) t.flags ^= drv::kTrigOnce;
    if (rng.chance(0.3)) {
        const int k = rng.range(0, 2);
        t.flags ^= k == 0 ? drv::kTrigRigidType1 : k == 1 ? drv::kTrigRigidType2 : drv::kTrigOthers;
    }
    if (rng.chance(0.15)) t.flags ^= drv::kTrigDirectional;
    if (rng.chance(0.15)) t.flags ^= drv::kTrigRotated;
    if (rng.chance(0.04)) t.flags ^= drv::kTrigIgnored;
    if (rng.chance(0.1)) t.flags ^= drv::kTrigWhileActive;
    if (rng.chance(0.15)) t.shape = uint8_t(rng.chance(0.95) ? rng.range(1, 3) : rng.range(0, 5));
    if (rng.chance(0.15)) {
        t.width = rng.uni(0.5f, 30);
        t.depth = rng.uni(0.5f, 30);
        t.height = rng.uni(0.2f, 10);
        t.radius = rng.uni(1, 20);
    }
    if (rng.chance(0.15)) {
        const float y = rng.uni(-3.14159f, 3.14159f), p = rng.chance(0.7) ? 0.0f : rng.uni(-0.4f, 0.4f);
        t.forward[0] = std::sin(y) * std::cos(p);
        t.forward[1] = std::sin(p);
        t.forward[2] = std::cos(y) * std::cos(p);
        t.right[0] = std::cos(y);
        t.right[1] = 0;
        t.right[2] = -std::sin(y);
    }
}

int check_triggers(TrafficEmu& e, Fleet& f, drv::Track& track, int cases, Rng& rng) {
    int failures = 0;
    Tally t_init, t_check, t_process, t_update, t_restart, t_spawn, t_ray;
    TriggerCopy tc;
    std::string error;
    if (!copy_triggers(e, track, *f.rc, tc, error)) {
        std::printf("  os gatilhos: %s\n", error.c_str());
        return 1;
    }
    int32_t last_type = 0;
    std::vector<drv::PendingEvent> emu_events;
    e.m.hook(kFnLookupEvent, "RegisterEvent::LookupEvent", [&last_type](MipsMachine& mm) {
        last_type = int32_t(mm.gpr(R::a0));
        mm.set_gpr(R::v0, kFnEventSink);
    });
    e.m.hook(kFnEventSink, "a função criadora de um evento", [&last_type, &emu_events](MipsMachine& mm) {
        float speed = 0.0f;
        if (last_type == 28) std::memcpy(&speed, mm.mem(uint32_t(mm.gpr(R::a0)) + 4, 4), 4);
        emu_events.push_back({last_type, mm.read32(kEventData + 0x30), speed, uint32_t(mm.gpr(R::a0)),
                              mm.read32(kEventData + 0x38), int32_t(mm.read32(kEventData + 0x3c))});
        mm.set_gpr(R::v0, 0);
    });
    auto zero = [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); };
    // Os raios dos tiros (docs/driving.md seção 26): o GetTracer (0x00199278) como no PS2, sobre o índice 0x333560 e os
    // raios ativos 0x332960 (o número em 0x332950) da RAM emulada, iguais aos do porte a cada caso.
    e.m.hook(0x199278, "RayShell::GetTracer", [](MipsMachine& mm) {
        const int32_t i = int32_t(mm.read32(kRayIndex));
        mm.set_gpr(R::v0, i == -1 ? 0u : kRayActive + 0x30u * uint32_t(i));
    });
    e.m.hook(0x1b1f28, "RPathEngine::GetFirstPathHandle", zero);
    e.m.hook(kFnPrintf, "printf", zero);
    drv::TriggerManagerGlobals& g = drv::trigger_manager();
    const drv::TriggerStats before = drv::trigger_stats();
    {
        std::map<int32_t, int> in_lists;
        int shapes[6] = {};
        for (int i = 0; i < tc.count; ++i) {
            const drv::WTrigger* t = reinterpret_cast<const drv::WTrigger*>(drv::UData_Data(tc.trgr)) + i;
            shapes[std::min<int>(t->shape, 5)]++;
            const drv::TriggerEvents* l = nf::p32_ptr<const drv::TriggerEvents>(t->events);
            if (!l) continue;
            const drv::TriggerEvent* ev = reinterpret_cast<const drv::TriggerEvent*>(l + 1);
            for (int k = 0; k < l->count; ++k) in_lists[ev[k].type]++;
        }
        std::string s;
        for (const auto& [type, n] : in_lists) s += " " + std::to_string(type) + ":" + std::to_string(n);
        std::printf("  os gatilhos de Paris: %d (caixas %d, esferas %d, cilindros %d), eventos por número%s\n", tc.count,
                    shapes[1], shapes[2], shapes[3], s.c_str());
        // As listas que nenhum gatilho usa (as regras da missão, o 'Rule' do SMissionManager, não portado).
        std::map<uint32_t, uint32_t> all;
        collect_event_lists(track.root(), all, 0);
        std::map<int32_t, int> others;
        int other_lists = 0;
        for (const auto& [addr, size] : all) {
            if (tc.lists.count(addr)) continue;
            ++other_lists;
            const drv::TriggerEvents* l = nf::p32_ptr<const drv::TriggerEvents>(addr);
            const drv::TriggerEvent* ev = reinterpret_cast<const drv::TriggerEvent*>(l + 1);
            for (int k = 0; k < l->count; ++k) others[ev[k].type]++;
        }
        s.clear();
        for (const auto& [type, n] : others) s += " " + std::to_string(type) + ":" + std::to_string(n);
        std::printf("  as outras listas de eventos (as regras da missão): %d, eventos por número%s\n", other_lists,
                    s.c_str());
    }

    // O Init (com o Restart: os EStartMission de Paris) dos dois lados.
    {
        ++t_init.cases;
        g.pending.clear();
        e.call(kFnTrigInit, {kTrigHeader});
        drv::WTriggerManager_Init(tc.trgr);
        f.heap_mark = e.heap;  // a cópia dos gatilhos e o gerente ficam
        const std::string why = compare_triggers(e, tc, emu_events);
        if (!why.empty()) t_init.fail(why);
    }
    const size_t start_events = g.pending.size();
    const uint32_t mgr = e.m.read32(kTrigManagerPtr);
    uint64_t touched = 0, fired = 0, spawned = 0;
    std::map<int32_t, uint64_t> created;
    const int nc = int(f.cars.size());
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        const std::string tag = " (caso " + std::to_string(c) + ")";
        for (int k = rng.range(0, 3); k > 0; --k) mutate_trigger(rng, *drv::trigger_at(rng.range(0, tc.count - 1)));
        if (rng.chance(0.1)) g.active = rng.chance(0.5) ? 1 : 0;
        if (rng.chance(0.03)) g.query_stamp = rng.next();
        std::vector<int8_t> types(size_t(nc), 0);
        std::vector<int> near(size_t(nc), 0);
        for (int i = 0; i < nc; ++i) {
            drv::RigidBody* rb = f.cars[size_t(i)]->body();
            types[size_t(i)] = rb->type;
            near[size_t(i)] = rng.range(0, tc.count - 1);
            if (rng.chance(0.85)) near_trigger(rng, rb, *drv::trigger_at(near[size_t(i)]));
            if (rng.chance(0.15)) rb->type = int8_t(rng.range(0, 4));
            rb->state = int8_t(rng.chance(0.8) ? 2 : (rng.chance(0.5) ? 3 : 1));
        }
        f.put_all();
        put_triggers(e, tc);
        e.m.write32(mgr + 8, uint32_t(g.active));
        e.m.write32(kTrigStamp, g.query_stamp);
        emu_events.clear();
        g.pending.clear();
        const uint64_t fired0 = drv::trigger_stats().fired;
        const int op = rng.range(0, 99);
        Tally* t = nullptr;
        if (op < 25) {
            t = &t_check;
            for (int k = 0; k < 8; ++k) {
                ++t_check.cases;
                const int i = rng.range(0, nc - 1);
                const int ti = rng.chance(0.8) ? near[size_t(i)] : rng.range(0, tc.count - 1);
                drv::RigidBody* rb = f.cars[size_t(i)]->body();
                e.call(kFnTrigCheck, {mgr, body_emu(rb->slot), tc.trig_emu(ti)});
                const bool a = (e.m.gpr(R::v0) & 0xff) != 0;
                const bool b = drv::WTriggerManager_CheckCollide(rb, *drv::trigger_at(ti));
                touched += b;
                if (a != b)
                    t_check.fail(std::string("o resultado: original ") + (a ? "1" : "0") + ", porte " + (b ? "1" : "0") +
                                 " (gatilho " + std::to_string(ti) + ", forma " +
                                 std::to_string(drv::trigger_at(ti)->shape) + ")" + tag);
            }
        } else if (op < 70) {
            t = &t_process;
            ++t_process.cases;
            const int i = rng.range(0, nc - 1);
            drv::RigidBody* rb = f.cars[size_t(i)]->body();
            e.call(kFnTrigProcess, {mgr, uint32_t(rb->slot), body_emu(rb->slot)});
            drv::WTriggerManager_Process(rb->slot, rb);
        } else if (op < 82) {
            // Os raios (WTriggerManager::Process(int), 0x0022def0; com o Update, todos): de um a seis, perto de um
            // gatilho, de quem atira (o jogador, outro carro, ninguém), com o meio comprimento; dos dois lados.
            t = &t_ray;
            ++t_ray.cases;
            drv::RayShellGlobals& rg = drv::rayshell_globals();
            drv::Simulation& sim = drv::simulation();
            for (int s = 0; s < drv::kRigidBodySlots; ++s) e.m.write32(kSim + 0x30 + 4u * uint32_t(s), sim.rigid_sig[s]);
            const int nrays = rng.range(1, 6);
            rg.num_active = nrays;
            for (int k = 0; k < nrays; ++k) {
                drv::ActiveRayShell& a = rg.active[k];
                a = drv::ActiveRayShell{};
                const drv::WTrigger& tr = *drv::trigger_at(rng.range(0, tc.count - 1));
                for (int i = 0; i < 3; ++i) a.start[i] = tr.position[i] + rng.uni(-15, 15);
                a.start[1] = tr.position[1] + rng.uni(-2, 6);
                const float yaw = rng.uni(-3.14159f, 3.14159f), pitch = rng.uni(-0.3f, 0.3f);
                const float len = rng.chance(0.2) ? rng.uni(0.0f, 0.02f) : rng.uni(1, 220);
                a.end[0] = a.start[0] + std::sin(yaw) * std::cos(pitch) * len;
                a.end[1] = a.start[1] + std::sin(pitch) * len;
                a.end[2] = a.start[2] + std::cos(yaw) * std::cos(pitch) * len;
                a.half = len * 0.5f;
                a.damage = rng.uni(0, 120);
                const int who = rng.range(0, 2);
                a.sig = who == 0 ? sim.rigid_sig[f.cars[0]->d.slot]
                                 : (who == 1 ? sim.rigid_sig[f.cars[size_t(rng.range(0, nc - 1))]->d.slot] : rng.next());
                std::memcpy(e.m.mem(kRayActive + 0x30u * uint32_t(k), 0x30), &a, 0x30);
            }
            e.m.write32(kRayCount, uint32_t(nrays));
            e.m.write32(kRayIndex, uint32_t(rg.trigger_index));
            if (rng.chance(0.6)) {
                const int k = rng.range(0, nrays - 1);
                e.call(0x22def0, {mgr, uint32_t(k)});
                drv::WTriggerManager_ProcessRay(k);
            } else {
                e.call(kFnTrigUpdate, {mgr});
                drv::WTriggerManager_Update();
            }
            if (e.m.read32(kRayIndex) != uint32_t(rg.trigger_index) || e.m.read32(kRayCount) != uint32_t(rg.num_active))
                t_ray.fail("o índice ou o número dos raios" + tag);
            rg.num_active = 0;
            e.m.write32(kRayCount, 0);
        } else if (op < 90) {
            t = &t_update;
            ++t_update.cases;
            e.call(kFnTrigUpdate, {mgr});
            drv::WTriggerManager_Update();
        } else {
            t = &t_restart;
            ++t_restart.cases;
            e.call(kFnTrigRestart, {mgr});
            drv::WTriggerManager_Restart();
        }
        fired += drv::trigger_stats().fired - fired0;
        for (const drv::PendingEvent& ev : g.pending) created[ev.type]++;
        const std::string why = compare_triggers(e, tc, emu_events);
        if (!why.empty()) t->fail(why + tag);
        for (int i = 0; i < nc; ++i) f.cars[size_t(i)]->body()->type = types[size_t(i)];
    }
    g.pending.clear();

    // O EFireRandomTrigger: um evento falso (a tabela, quatro gatilhos e chances sorteados, o flag e o índice) apagado
    // pelo destrutor original, com o RRandom do porte no 0x333e60; o gatilho escolhido dispara os seus eventos.
    Tally t_random;
    uint64_t random_fired = 0;
    for (int c = 0; c < cases; ++c) {
        ++t_random.cases;
        const std::string tag = " (caso " + std::to_string(c) + ")";
        drv::FireRandomTrigger ev{};
        uint32_t emu_trig[4] = {};
        for (int k = 0; k < 4; ++k) {
            if (rng.chance(0.85)) {
                const int ti = rng.range(0, tc.count - 1);
                ev.trigger[k] = nf::p32_u(drv::trigger_at(ti));
                emu_trig[k] = tc.trig_emu(ti);
            }
            ev.chance[k] = rng.chance(0.1) ? 0.0f : rng.uni(0, rng.chance(0.5) ? 25.0f : 100.0f);
        }
        ev.flag = rng.chance(0.3) ? 1u : 0u;
        ev.index = rng.chance(0.2) ? -1 : rng.range(0, 47);
        if (rng.chance(0.05)) drv::RRandom_StartUp(drv::render_random(), rng.next());
        put_triggers(e, tc);
        e.m.write32(0x333e60, drv::render_random().seed);
        e.m.write32(0x333e64, drv::render_random().multiplier);
        const uint32_t obj = kSpawnEvent + 0x40;
        e.m.write32(obj, 0x3710e0);
        for (int k = 0; k < 4; ++k) {
            e.m.write32(obj + 4 + 8u * uint32_t(k), emu_trig[k]);
            std::memcpy(e.m.mem(obj + 8 + 8u * uint32_t(k), 4), &ev.chance[k], 4);
        }
        e.m.write32(obj + 0x24, ev.flag);
        e.m.write32(obj + 0x28, uint32_t(ev.index));
        emu_events.clear();
        g.pending.clear();
        const uint64_t fired0 = drv::trigger_stats().fired;
        e.call(0x14c688, {obj, 0});
        drv::EFireRandomTrigger_Execute(ev);
        random_fired += drv::trigger_stats().fired - fired0;
        std::string why = compare_triggers(e, tc, emu_events);
        if (why.empty() && e.m.read32(0x333e60) != drv::render_random().seed)
            why = "o RRandom: original " + hex(e.m.read32(0x333e60)) + ", porte " + hex(drv::render_random().seed);
        if (!why.empty()) t_random.fail(why + tag);
    }
    g.pending.clear();

    // O EFireTriggerSpeedCondition: um evento falso (o gatilho, a velocidade, "menor que", o flag e a vaga de um corpo da
    // frota com a velocidade sorteada) apagado pelo destrutor original.
    Tally t_speed;
    uint64_t speed_fired = 0;
    for (int c = 0; c < cases; ++c) {
        ++t_speed.cases;
        const std::string tag = " (caso " + std::to_string(c) + ")";
        random_scene(rng, f);
        drv::RigidBody* rb = f.cars[size_t(rng.range(0, nc - 1))]->body();
        if (rng.chance(0.2)) for (int k = 0; k < 3; ++k) rb->vel[k] = 0.0f;
        const int ti = rng.range(0, tc.count - 1);
        drv::FireTriggerSpeed ev{};
        ev.trigger = rng.chance(0.95) ? nf::p32_u(drv::trigger_at(ti)) : 0u;
        ev.speed = rng.chance(0.1) ? 0.0f : rng.uni(0, 25);
        ev.less = rng.chance(0.5) ? 1u : 0u;
        ev.flag = rng.chance(0.3) ? 1u : 0u;
        ev.index = rb->slot;
        f.put_all();
        put_triggers(e, tc);
        const uint32_t obj = kSpawnEvent + 0x40;
        e.m.write32(obj, 0x3710c8);
        e.m.write32(obj + 4, ev.trigger ? tc.trig_emu(ti) : 0u);
        std::memcpy(e.m.mem(obj + 8, 4), &ev.speed, 4);
        e.m.write32(obj + 0xc, ev.less);
        e.m.write32(obj + 0x10, ev.flag);
        e.m.write32(obj + 0x14, uint32_t(ev.index));
        emu_events.clear();
        g.pending.clear();
        const uint64_t fired0 = drv::trigger_stats().fired;
        e.call(0x14c8c8, {obj, 0});
        drv::EFireTriggerSpeedCondition_Execute(ev);
        speed_fired += drv::trigger_stats().fired - fired0;
        const std::string why = compare_triggers(e, tc, emu_events);
        if (!why.empty()) t_speed.fail(why + tag);
    }
    g.pending.clear();

    // O ESpawnTraffic: um evento falso {tabela, gatilho, velocidade} apagado pelo destrutor original; os 20 motoristas
    // do tráfego no vetor estático do PS2 (0x3282d0, 0x2a0 cada; os da frota copiados, os outros zerados).
    drv::VehicleControllerGlobals& k = drv::vehicle_controller();
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        const std::string tag = " (caso " + std::to_string(c) + ")";
        for (int n = rng.range(1, 3); n > 0; --n) f.cars[size_t(rng.range(1, nc - 1))]->body()->state = 3;
        const int ti = rng.range(0, tc.count - 1);
        const float speed = rng.chance(0.3) ? 0.0f : (rng.chance(0.1) ? -rng.uni(0, 10) : rng.uni(1, 40));
        ++t_spawn.cases;
        f.put_all();
        put_triggers(e, tc);
        int map_ai[drv::kTrafficVehicles];
        for (int j = 0; j < drv::kTrafficVehicles; ++j) {
            map_ai[j] = k.traffic[j] ? f.index_of_ai(k.traffic[j]) : -1;
            uint8_t* dst = e.m.mem(kTrafficStatic + 0x2a0u * uint32_t(j), 0x2a0);
            if (map_ai[j] >= 0) std::memcpy(dst, e.m.mem(ai_emu(map_ai[j]), 0x2a0), 0x2a0);
            else std::memset(dst, 0, 0x2a0);
        }
        e.m.write32(kSpawnEvent, kSpawnTrafficVtable);
        e.m.write32(kSpawnEvent + 4, tc.trig_emu(ti));
        std::memcpy(e.m.mem(kSpawnEvent + 8, 4), &speed, 4);
        e.call(kFnSpawnTrafficDtor, {kSpawnEvent, 0});
        for (int j = 0; j < drv::kTrafficVehicles; ++j)
            if (map_ai[j] >= 0)
                std::memcpy(e.m.mem(ai_emu(map_ai[j]), 0x2a0), e.m.mem(kTrafficStatic + 0x2a0u * uint32_t(j), 0x2a0), 0x2a0);
        const uint64_t s0 = drv::trigger_stats().traffic_spawned;
        drv::ESpawnTraffic_Execute(*drv::trigger_at(ti), speed);
        spawned += drv::trigger_stats().traffic_spawned - s0;
        const std::string why = f.compare(false);
        if (!why.empty()) t_spawn.fail(why + tag);
    }

    report("WTriggerManager::Init (o Restart: os EStartMission de Paris)", t_init, failures);
    report("WTriggerManager::CheckCollide(RigidBody *, WTrigger *)", t_check, failures);
    report("WTriggerManager::Process(int, RigidBody *) (e WTrigger::FireEvents)", t_process, failures);
    report("WTriggerManager::Update (os corpos rígidos)", t_update, failures);
    report("WTriggerManager::Process(int) (os raios dos tiros, seção 26)", t_ray, failures);
    std::printf("  (os raios: %llu conferidos, %llu gatilhos tocados)\n",
                (unsigned long long)(drv::trigger_stats().rays - before.rays),
                (unsigned long long)(drv::trigger_stats().ray_hits - before.ray_hits));
    report("WTriggerManager::Restart", t_restart, failures);
    report("EFireRandomTrigger::~EFireRandomTrigger (o RRandom, os gatilhos)", t_random, failures);
    report("EFireTriggerSpeedCondition::~EFireTriggerSpeedCondition", t_speed, failures);
    report("ESpawnTraffic::~ESpawnTraffic (um carro do tráfego no gatilho)", t_spawn, failures);
    std::string kinds;
    for (const auto& [type, n] : created) kinds += " " + std::to_string(type) + ":" + std::to_string(n);
    const drv::TriggerStats& after = drv::trigger_stats();
    std::printf("  (%d gatilhos, %zu listas de eventos, %zu eventos no Init; tocados %" PRIu64 ", disparos %" PRIu64
                ", eventos por número%s; sorteios que dispararam %" PRIu64 ", velocidades %" PRIu64 "; carros postos na rua %" PRIu64
                ", guardas %" PRIu64 ")\n",
                tc.count, tc.lists.size(), start_events, touched, fired, kinds.c_str(), random_fired, speed_fired, spawned,
                after.guards - before.guards);
    return failures;
}

// g) Os objetos de cena derrubáveis (Smackable): o objeto de cena, a instância, o 'ps  ', o 'pD' (com os pontos de
// colisão) e a PhysicsData copiados para a RAM emulada; o SpawnSmackableObject original (com o construtor do Smackable e
// o RigidBody de tipo 4), o EWakeupSmackable, passos do Simulate (com o GoToSleep), o ApplyDamage e o destrutor, contra
// o porte. Ganchos: o mapa da Simulation (SpawnPhysicsObject/DeletePhysicsObject: o vetor dos Smackable), o som, os
// estímulos do RAnimEngine, o TriggerFX e a lista dinâmica da grade. O nó da árvore do RSceneObj::UpdatePosition fica de
// fora (o bit 26 das bandeiras ligado dos dois lados); o WTree::CheckContainment é conferido à parte com nós do 'Wmap'.
constexpr uint32_t kFnSpawnSmackable = 0x1ffa90, kFnSmackSimulate = 0x19e718, kFnSmackSleep = 0x19e610;
constexpr uint32_t kFnSmackDamage = 0x19e8c0, kFnSmackDtor = 0x19e590, kFnWakeupDtor = 0x15de48;
constexpr uint32_t kFnTensor = 0x210840, kFnContainment = 0x22c530, kFnUpdateRotPos = 0x22cad8;
constexpr uint32_t kWakeupVtable = 0x370210, kSceneObjVtable = 0x386a88;
constexpr uint32_t kSmScene = 0x01e00000, kSmInst = 0x01e00040, kSmPs = 0x01e00080, kSmPd = 0x01e000a0;
constexpr uint32_t kSmColl = 0x01e000d0, kSmTrig = 0x01e000e0, kSmDesc = 0x01e00200, kSmVec = 0x01e00400;
constexpr uint32_t kSmVel = 0x01e00500, kSmEvent = 0x01e00520, kSmNode = 0x01e00600, kSmBox = 0x01e00610;
constexpr uint32_t kSmSphere = 0x01e00620, kSmOut = 0x01e00630, kSmFrom = 0x01e00640, kSmTo = 0x01e00650;

struct SmackCandidate {
    drv::RSceneObj* obj;
    uint32_t ps, desc, phys;
    uint32_t desc_size;
};

int check_smackables(TrafficEmu& e, Fleet& f, drv::Track& track, int cases, Rng& rng) {
    int failures = 0;
    Tally t_tensor, t_contain, t_rotpos, t_spawn, t_wake, t_sim, t_damage, t_dtor;
    drv::Simulation& sim = drv::simulation();
    // O mundo do port (o que o nf_driving põe na carga).
    {
        drv::SmackableWorld& sw = drv::smackable_world();
        const drv::WRender& wr = track.world_render();
        sw.instances = wr.instances;
        sw.instance_count = 0;
        for (int p = 0; p < 5; ++p) sw.instance_count += wr.pass_count[p];
        sw.proc_anims = wr.proc_anims;
        sw.moved.clear();
        drv::fgWorldMap() = track.world_map();
    }
    std::vector<SmackCandidate> cands;
    for (drv::RSceneObj& o : track.scene_objects()) {
        const uint32_t ps = drv::WWorld_GetProcAnimStateFromInstance(o.instance);
        if (!ps) continue;
        uint32_t w4;
        std::memcpy(&w4, nf::p32_ptr<const uint8_t>(ps) + 4, 4);
        if (!((w4 >> 8) & 2)) continue;
        const uint32_t desc = drv::scene_object_desc(o);
        if (!desc) continue;
        const drv::CarpBaseDesc& d = *nf::p32_ptr<const drv::CarpBaseDesc>(desc);
        if (!d.physics || d.collision_prims > 16) continue;
        cands.push_back({&o, ps, desc, d.physics, 0x60u + 16u * d.collision_prims});
    }
    std::printf("  os objetos de cena derrubáveis de Paris: %zu (com 'pD' e PhysicsData), espaço PHYS: %d\n", cands.size(),
                track.physics_symbols());
    if (cands.empty()) return 1;

    uint64_t stimuli = 0, dyn_grid = 0, sounds = 0;
    e.m.hook(0x200cb8, "Simulation::SpawnPhysicsObject", [](MipsMachine& mm) {
        const uint32_t end = mm.read32(kSim + 0xde8);
        mm.write32(end, uint32_t(mm.gpr(R::a1)));
        mm.write32(kSim + 0xde8, end + 4);
    });
    e.m.hook(0x200f18, "Simulation::DeletePhysicsObject", [](MipsMachine& mm) {
        const uint32_t obj = uint32_t(mm.gpr(R::a1));
        const uint32_t begin = mm.read32(kSim + 0xde4), end = mm.read32(kSim + 0xde8);
        for (uint32_t a = begin; a < end; a += 4)
            if (mm.read32(a) == obj) {
                std::memmove(mm.mem(a, end - a), mm.mem(a + 4, end - a - 4), end - a - 4);
                mm.write32(kSim + 0xde8, end - 4);
                break;
            }
    });
    e.m.hook(0x2fca38, "ABaseSound::operator_new", [&sounds](MipsMachine& mm) {
        ++sounds;
        mm.set_gpr(R::v0, 0);
    });
    e.m.hook(0x2ef6f0, "ASmackable::ASmackable", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
    auto count = [](uint64_t& c) {
        return [&c](MipsMachine& mm) {
            ++c;
            mm.set_gpr(R::v0, 0);
        };
    };
    e.m.hook(0x1a2868, "RAnimEngine::Handle::ProcessStimuli", count(stimuli));
    e.m.hook(0x1a2b50, "RAnimEngine::Handle::ProcessStimuliZones", count(stimuli));
    e.m.hook(0x1caba8, "RSceneObj::TriggerFX", count(stimuli));
    e.m.hook(0x21cad0, "WGrid::AddGridNodeDynamicElement", count(dyn_grid));

    // As funções puras primeiro.
    for (int c = 0; c < cases; ++c) {
        ++t_tensor.cases;
        const float mass = rng.chance(0.1) ? rng.uni(0.5f, 5) : rng.uni(5, 3000);
        const float x = rng.uni(0.05f, 3), z = rng.uni(0.05f, 3);
        const float y = rng.chance(0.5) ? rng.uni(0.05f, 3) : rng.uni(3, 15);
        e.m.set_fpr(12, mass);
        e.m.set_fpr(13, x);
        e.m.set_fpr(14, y);
        e.m.set_fpr(15, z);
        e.call(kFnTensor, {kSmOut});
        float out[3];
        drv::Util_GenerateTensor(out, mass, x, y, z);
        std::string why;
        if (!same_words("Util_GenerateTensor", e.m.mem(kSmOut, 12), reinterpret_cast<const uint8_t*>(out), 12, {}, why))
            t_tensor.fail(why);
    }
    {
        const uint8_t* wmap = nf::p32_ptr<const uint8_t>(track.world_map());
        uint32_t root;
        std::memcpy(&root, wmap, 4);
        float corner[3], size;
        std::memcpy(corner, wmap + 0x1c, 12);
        std::memcpy(&size, wmap + 0x2c, 4);
        const float half = ps2::ee::mul(size, 0.5f);
        const float box[4] = {ps2::ee::add(corner[0], half), corner[1], ps2::ee::add(corner[2], half), half};
        for (int c = 0; c < cases; ++c) {
            ++t_contain.cases;
            // Um nó ao acaso: a raiz e os filhos de alguns níveis.
            const drv::MapNode* n = nf::p32_ptr<const drv::MapNode>(root);
            for (int depth = rng.range(0, 6); depth > 0 && n->children; --depth)
                n = nf::p32_ptr<const drv::MapNode>(n->children) + rng.range(0, 3);
            const drv::RSceneObj& o = *cands[size_t(rng.range(0, int(cands.size()) - 1))].obj;
            const uint8_t* inst = nf::p32_ptr<const uint8_t>(o.instance);
            float sphere[4];
            std::memcpy(sphere, inst + 0x30, 12);
            sphere[3] = rng.uni(0.1f, 40);
            if (rng.chance(0.5)) {
                sphere[0] += rng.uni(-100, 100);
                sphere[2] += rng.uni(-100, 100);
            }
            put(e.m, kSmNode, n, 16);
            put(e.m, kSmBox, box, 16);
            put(e.m, kSmSphere, sphere, 16);
            e.call(kFnContainment, {kSmNode, kSmBox, kSmSphere});
            const bool a = (e.m.gpr(R::v0) & 1) != 0;
            const bool b = drv::WTree_CheckContainment(*n, box, sphere);
            if (a != b) t_contain.fail(std::string("o resultado: original ") + (a ? "1" : "0") + ", porte " + (b ? "1" : "0"));
        }
    }
    for (int c = 0; c < cases; ++c) {
        ++t_rotpos.cases;
        drv::WTrigger t{};
        t.shape = uint8_t(rng.range(1, 3));
        t.height = rng.uni(0.2f, 8);
        t.radius = rng.uni(0.2f, 8);
        t.flags = uint16_t(rng.next());
        for (int k = 0; k < 3; ++k) t.position[k] = rng.uni(-500, 500);
        const float pos[4] = {rng.uni(-500, 500), rng.uni(-10, 20), rng.uni(-500, 500), 0};
        put(e.m, kSmTrig, &t, 0x40);
        put(e.m, kSmVel, pos, 16);
        // O WTriggerManager emulado (o índice do gatilho no AddGridNodeDynamicElement): o do Init da parte f.
        e.call(kFnUpdateRotPos, {kSmTrig, 0, kSmVel});
        drv::WTrigger_UpdateRotPos(t, pos);
        std::string why;
        if (!same_words("WTrigger", e.m.mem(kSmTrig, 0x40), reinterpret_cast<const uint8_t*>(&t), 0x40, {}, why))
            t_rotpos.fail(why);
    }

    // Um caso: o objeto de cena, a sua instância e o 'ps  ' (salvos e devolvidos no fim), um gatilho sorteado.
    drv::WTrigger trig{};
    auto put_scene = [&](const SmackCandidate& k, const drv::RSceneObj& o) {
        drv::RSceneObj eo = o;
        eo.next = eo.prev = 0;
        eo.instance = kSmInst;
        eo.unknown0c = kSmDesc;
        eo.carp = 0;
        eo.map_node = 0;
        eo.physics = 0;
        eo.article = 0;
        eo.path = 0;
        std::memset(eo.unknown30, 0, sizeof eo.unknown30);
        eo.vtable = kSceneObjVtable;
        put(e.m, kSmScene, &eo, 0x40);
        put(e.m, kSmInst, nf::p32_ptr<const uint8_t>(o.instance), 0x40);
        uint8_t ps[0x20];
        std::memcpy(ps, nf::p32_ptr<const uint8_t>(k.ps), 0x20);
        const uint32_t scene = kSmScene;
        std::memcpy(ps + 8, &scene, 4);
        put(e.m, kSmPs, ps, 0x20);
        std::vector<uint8_t> desc(k.desc_size);
        std::memcpy(desc.data(), nf::p32_ptr<const uint8_t>(k.desc), k.desc_size);
        const uint32_t pd = kSmPd;
        std::memcpy(desc.data() + 0xc, &pd, 4);
        std::memset(desc.data(), 0, 4);
        put(e.m, kSmDesc, desc.data(), k.desc_size);
        drv::SmackablePhysicsData p = *nf::p32_ptr<const drv::SmackablePhysicsData>(k.phys);
        p.attributes = kSmColl;
        put(e.m, kSmPd, &p, sizeof p);
        std::memset(e.m.mem(kSmColl, 16), 0, 16);
        put(e.m, kSmTrig, &trig, 0x40);
        // O WWorld falso: as instâncias (a do caso) e o 'ps  ' (pelo índice da instância).
        uint16_t idx;
        std::memcpy(&idx, nf::p32_ptr<const uint8_t>(o.instance) + 0xe, 2);
        e.m.write32(kFakeWorld + 0x20, kSmInst);
        e.m.write32(kFakeWorld + 0x24, kSmPs - 0x20u * idx);
        e.m.write32(kFakeWorld + 0x28, 1);
        e.m.write32(kSim + 0x8, uint32_t(drv::RigidBodiesInUse(sim)));
        e.m.write32(kSim + 0xde4, kSmVec);
        e.m.write32(kSim + 0xde8, kSmVec);
        e.m.write32(kSim + 0xdec, kSmVec + 0x100);
    };
    // O Smackable emulado contra o do porte (os ponteiros pelos endereços do caso).
    auto compare_smack = [&](uint32_t es, const drv::Smackable* s, const SmackCandidate& k, std::string& why) -> bool {
        if (!es || !s) {
            if (es || s) why = std::string("o Smackable: original ") + (es ? "criado" : "nulo") + ", porte " + (s ? "criado" : "nulo");
            return why.empty();
        }
        drv::SmackableData ed;
        std::memcpy(&ed, e.m.mem(es + 0x40, 0x48), 0x48);
        if (!same_words("Smackable", reinterpret_cast<const uint8_t*>(&ed), reinterpret_cast<const uint8_t*>(&s->d), 0x48,
                        {{0x08, 0x0c}, {0x14, 0x18}, {0x1c, 0x20}, {0x38, 0x44}}, why))
            return false;
        auto eq = [&](const char* what, uint32_t a, bool ok) {
            if (why.empty() && !ok) why = std::string(what) + ": original " + hex(a);
        };
        eq("Smackable +0x48 (o render)", ed.render, ed.render == kSmScene && s->d.render == nf::p32_u(k.obj));
        eq("Smackable +0x54 (a vida)", ed.hit_point_loc, ed.hit_point_loc == es + 0x74 && s->d.hit_point_loc == nf::p32_u(&s->d.hit_points));
        eq("Smackable +0x78 (a PhysicsData)", ed.params, ed.params == kSmPd && s->d.params == k.phys);
        eq("Smackable +0x7c (o gatilho)", ed.trigger, ed.trigger == kSmTrig && s->d.trigger == nf::p32_u(&trig));
        eq("Smackable +0x80 (o 'ps  ')", ed.proc_anim, ed.proc_anim == kSmPs && s->d.proc_anim == k.ps);
        if (!why.empty()) return false;
        const int slot = s->d.slot;
        const drv::RigidBody* rb = s->body();
        if (!same_words("RigidBody", e.m.mem(body_emu(slot), 0x80), reinterpret_cast<const uint8_t*>(rb), 0x80,
                        {{0x5c, 0x60}}, why))
            return false;
        if (!same_words("RigidBodyInfo", e.m.mem(info_emu(slot), 0x500), reinterpret_cast<const uint8_t*>(nf::raw(rb->info)),
                        0x500, {{0x70, 0x80}, {0x380, 0x3c0}, {0x4d8, 0x4dc}}, why))  // +0x70: lixo da pilha (ver o Fleet)
            return false;
        return true;
    };
    auto compare_world = [&](const SmackCandidate& k, std::string& why) -> bool {
        if (!same_words("a instância", e.m.mem(kSmInst, 0x40), nf::p32_ptr<const uint8_t>(k.obj->instance), 0x40, {}, why))
            return false;
        if (!same_words("o 'ps  '", e.m.mem(kSmPs, 0x20), nf::p32_ptr<const uint8_t>(k.ps), 0x20, {{0x08, 0x0c}}, why))
            return false;
        if (!same_words("o gatilho", e.m.mem(kSmTrig, 0x40), reinterpret_cast<const uint8_t*>(&trig), 0x40, {}, why))
            return false;
        drv::RSceneObj eo;
        std::memcpy(&eo, e.m.mem(kSmScene, 0x40), 0x40);
        if (eo.flags != k.obj->flags) {
            why = "RSceneObj +0x18: original " + hex(eo.flags) + ", porte " + hex(k.obj->flags);
            return false;
        }
        const int32_t en = int32_t(e.m.read32(kSim + 0x8));
        if (en != drv::RigidBodiesInUse(sim)) {
            why = "Simulation +0x8: original " + std::to_string(en) + ", porte " + std::to_string(drv::RigidBodiesInUse(sim));
            return false;
        }
        const uint32_t vec = (e.m.read32(kSim + 0xde8) - e.m.read32(kSim + 0xde4)) / 4;
        if (vec != drv::smackables().size()) {
            why = "os Smackable vivos: original " + std::to_string(vec) + ", porte " + std::to_string(drv::smackables().size());
            return false;
        }
        return true;
    };
    // O porte depois de um caso: o Smackable apagado (como o DeleteDeadObjects) e o objeto de cena como era.
    auto cleanup = [&](const SmackCandidate& k, drv::Smackable* s, const uint8_t inst[0x40], const uint8_t ps[0x20],
                       const drv::RSceneObj& saved) {
        if (s) {
            auto& v = drv::smackables();
            v.erase(std::remove(v.begin(), v.end(), s), v.end());
            sim.dead.erase(std::remove(sim.dead.begin(), sim.dead.end(), static_cast<drv::PhysicsObject*>(s)), sim.dead.end());
            delete s;
        }
        for (drv::PhysicsObject* o : sim.dead) delete o;
        sim.dead.clear();
        std::memcpy(nf::p32_ptr<uint8_t>(k.obj->instance), inst, 0x40);
        std::memcpy(nf::p32_ptr<uint8_t>(k.ps), ps, 0x20);
        *k.obj = saved;
        drv::smackable_world().moved.clear();
    };
    auto random_trigger = [&](const drv::RSceneObj& o) {
        trig = drv::WTrigger{};
        const uint8_t* inst = nf::p32_ptr<const uint8_t>(o.instance);
        std::memcpy(trig.position, inst + 0x30, 12);
        trig.radius = rng.uni(0.5f, 3);
        trig.shape = uint8_t(rng.range(1, 3));
        trig.flags = uint16_t(0x035e | (rng.chance(0.5) ? 0x100 : 0) | (rng.chance(0.2) ? 1 : 0));
        trig.height = rng.uni(0.5f, 6);
        trig.width = rng.uni(0.2f, 2);
        trig.depth = rng.uni(0.2f, 2);
        trig.forward[2] = 1;
        trig.right[0] = 1;
    };

    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        const std::string tag = " (caso " + std::to_string(c) + ")";
        const SmackCandidate& k = cands[size_t(rng.range(0, int(cands.size()) - 1))];
        drv::RSceneObj& o = *k.obj;
        uint8_t saved_inst[0x40], saved_ps[0x20];
        std::memcpy(saved_inst, nf::p32_ptr<const uint8_t>(o.instance), 0x40);
        std::memcpy(saved_ps, nf::p32_ptr<const uint8_t>(k.ps), 0x20);
        const drv::RSceneObj saved = o;
        o.flags |= 0x04000000u;  // sem o nó da árvore (ver o cabeçalho)
        if (rng.chance(0.3)) nf::p32_ptr<uint8_t>(k.ps)[4] = uint8_t(rng.range(0, 255));
        random_trigger(o);
        const bool via_event = rng.chance(0.4);
        float vel[4] = {0, 0, 0, 0};
        if (!via_event && rng.chance(0.3))
            for (int i = 0; i < 3; ++i) vel[i] = rng.uni(-5, 5);
        const uint32_t delay = uint32_t(rng.range(0, 10));
        f.put_all();
        put_scene(k, o);
        put(e.m, kSmVel, vel, 16);
        // O nascimento: pelo EWakeupSmackable (o objeto do 'ps  ', o atraso de 0,1 s) ou direto.
        uint32_t es = 0;
        drv::Smackable* s = nullptr;
        Tally& tb = via_event ? t_wake : t_spawn;
        ++tb.cases;
        if (via_event) {
            e.m.write32(kSmEvent, kWakeupVtable);
            e.m.write32(kSmEvent + 4, 0);
            e.m.write32(kSmEvent + 8, kSmInst);
            e.m.write32(kSmEvent + 0xc, kSmPs);
            std::memset(e.m.mem(kSmEvent + 0x10, 0x20), 0, 0x20);
            e.m.write32(kSmEvent + 0x30, kSmTrig);
            e.m.write32(kSmEvent + 0x34, 0);
            e.m.write32(kSmEvent + 0x38, 0);
            e.call(kFnWakeupDtor, {kSmEvent, 0});
            const uint32_t n = (e.m.read32(kSim + 0xde8) - e.m.read32(kSim + 0xde4)) / 4;
            es = n ? e.m.read32(e.m.read32(kSim + 0xde8) - 4) : 0;
            drv::WakeupSmackable ev{};
            ev.instance = o.instance;
            ev.proc_anim = k.ps;
            ev.trigger = nf::p32_u(&trig);
            const size_t before = drv::smackables().size();
            drv::EWakeupSmackable_Execute(ev);
            if (drv::smackables().size() > before) s = drv::smackables().back();
        } else {
            e.m.set_gpr(R::t0, delay);
            e.call(kFnSpawnSmackable, {kSim, kSmScene, kSmTrig, kSmVel});
            es = uint32_t(e.m.gpr(R::v0));
            s = drv::SpawnSmackableObject(sim, o, &trig, vel, delay);
        }
        std::string why;
        if (!compare_smack(es, s, k, why) || !compare_world(k, why)) tb.fail(why + tag);
        if (!why.empty() || !s || !es) {
            cleanup(k, s, saved_inst, saved_ps, saved);
            continue;
        }
        // Alguns passos: o Simulate (com o corpo sorteado), um ApplyDamage às vezes; o destrutor no fim.
        const int steps = rng.range(1, 8);
        bool alive = true;
        for (int st = 0; st < steps && alive && why.empty(); ++st) {
            drv::RigidBody* rb = s->body();
            const int slot = s->d.slot;
            rb->state = int8_t(rng.chance(0.6) ? 2 : 1);
            for (int i = 0; i < 3; ++i) {
                rb->vel[i] = rng.chance(0.3) ? 0.0f : rng.uni(-4, 4);
                rb->pos[i] += rng.uni(-0.5f, 0.5f);
            }
            if (rng.chance(0.3)) s->d.sleep_delay = 0;
            if (rng.chance(0.2)) rb->radius_flags ^= 2u;
            // O corpo, o Smackable (+0x6c..+0x74) e o mundo do porte de volta à RAM emulada.
            put(e.m, body_emu(slot), rb, 0x80);
            e.m.write32(body_emu(slot) + 0x5c, info_emu(slot));
            e.m.write32(es + 0x6c, s->d.sleep_delay);
            e.m.write32(es + 0x70, s->d.awake_timer);
            std::memcpy(e.m.mem(es + 0x74, 4), &s->d.hit_points, 4);
            if (rng.chance(0.25)) {
                ++t_damage.cases;
                const float amount = rng.chance(0.2) ? -rng.uni(0, 5) : rng.uni(0, 40);
                const int32_t kind = rng.range(1, 3);
                const float from[4] = {rb->pos[0] + 1, rb->pos[1], rb->pos[2], 0}, to[4] = {rb->pos[0], rb->pos[1], rb->pos[2], 0};
                put(e.m, kSmFrom, from, 16);
                put(e.m, kSmTo, to, 16);
                e.m.set_fpr(12, amount);
                e.m.set_fpr(13, 0.5f);
                e.call(kFnSmackDamage, {es, kSmFrom, kSmTo, uint32_t(kind)});
                const int32_t r = s->apply_damage(from, to, amount, 0.5f, kind);
                if (int32_t(e.m.gpr(R::v0)) != r) t_damage.fail("o retorno" + tag);
                if (!compare_smack(es, s, k, why)) {
                    t_damage.fail(why + tag);
                    break;
                }
            }
            ++t_sim.cases;
            e.call(kFnSmackSimulate, {es});
            s->simulate();
            if (!compare_smack(es, s, k, why) || !compare_world(k, why)) {
                t_sim.fail(why + tag + " passo " + std::to_string(st));
                break;
            }
            alive = std::find(drv::smackables().begin(), drv::smackables().end(), s) != drv::smackables().end();
        }
        if (why.empty()) {
            // O destrutor (o DeleteDeadObjects): a instância na pose do corpo, a vaga devolvida.
            ++t_dtor.cases;
            e.call(kFnSmackDtor, {es, 0});
            auto& v = drv::smackables();
            v.erase(std::remove(v.begin(), v.end(), s), v.end());
            sim.dead.erase(std::remove(sim.dead.begin(), sim.dead.end(), static_cast<drv::PhysicsObject*>(s)), sim.dead.end());
            const int slot = s->d.slot;
            delete s;
            s = nullptr;
            if (e.m.read32(kSim + 0x270 + 4u * uint32_t(slot)) != 0 || sim.owners[slot] != nullptr)
                t_dtor.fail("a vaga" + tag);
            else if (!same_words("a instância", e.m.mem(kSmInst, 0x40), nf::p32_ptr<const uint8_t>(o.instance), 0x40, {}, why))
                t_dtor.fail(why + tag);
            else if (e.m.read32(kSmScene + 0x24) != 0 || o.physics != 0)
                t_dtor.fail("RSceneObj +0x24" + tag);
            else if (int32_t(e.m.read32(kSim + 0x8)) != drv::RigidBodiesInUse(sim))
                t_dtor.fail("Simulation +0x8" + tag);
        }
        cleanup(k, s, saved_inst, saved_ps, saved);
    }
    report("Util_GenerateTensor", t_tensor, failures);
    report("WTree::CheckContainment (nós do 'Wmap')", t_contain, failures);
    report("WTrigger::UpdateRotPos", t_rotpos, failures);
    report("Simulation::SpawnSmackableObject (Smackable::Smackable, o corpo)", t_spawn, failures);
    report("EWakeupSmackable::~EWakeupSmackable", t_wake, failures);
    report("Smackable::Simulate (UpdatePosition, GoToSleep)", t_sim, failures);
    report("Smackable::ApplyDamage", t_damage, failures);
    report("Smackable::~Smackable (SetPhysics(0), a vaga)", t_dtor, failures);
    const drv::SmackableStats& ss = drv::smackable_stats();
    std::printf("  (acordados %" PRIu64 ", dormiram %" PRIu64 ", escondidos %" PRIu64 "; no original: sons %" PRIu64
                ", estímulos %" PRIu64 ", grade dinâmica %" PRIu64 "; guardas do porte %" PRIu64 ")\n",
                ss.woken, ss.asleep, ss.hidden, sounds, stimuli, dyn_grid, ss.guards);
    return failures;
}

}  // namespace

int run_traffic_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    std::printf("\nparte 14: o mundo vivo (a rede de estradas de Paris, o navegador, o tráfego)\n");
    drv::Track track;
    std::string error;
    if (!track.load(disc, "paris_mis01", "", error)) {
        std::printf("  Paris: %s\n", error.c_str());
        return 1;
    }
    drv::TrackWorld tw;
    if (!tw.init(track.root(), &error)) {
        std::printf("  o chão de Paris: %s\n", error.c_str());
        return 1;
    }
    drv::set_world(&tw);
    drv::WRoadNetwork_Shutdown();
    drv::WRoadNetwork_Init(track.root(), "paris_mis01");
    const drv::RoadNetworkGlobals& r = drv::road_network();
    std::printf("  Paris: %d nós, %d trechos, %d cruzamentos, %d ruas, tráfego %d\n", r.num_nodes, r.num_segments,
                r.num_intersections, r.num_roads, r.valid_traffic_roads);
    TrafficEmu e;
    e.setup(elf, vu0_program, vu0_size);
    WorldCopy wc;
    if (!copy_world(e.m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
        std::printf("  %s\n", error.c_str());
        return 1;
    }
    RoadCopy rc;
    if (!copy_roads(e, track, rc, error)) {
        std::printf("  %s\n", error.c_str());
        return 1;
    }
    drv::SimRandom_Reset(drv::sim_random());
    Rng rng(seed ^ 0x14u);
    int failures = 0;
    Fleet fleet;
    fleet.e = &e;
    fleet.rc = &rc;
    fleet.tw = &tw;
    fleet.wc = &wc;
    try {
        failures += check_network(e, rc, cases, rng);
        failures += check_nav(e, rc, cases, rng);
        if (!build_fleet(fleet, disc, error)) {
            std::printf("  a frota: %s\n", error.c_str());
            ++failures;
        } else {
            hook_fleet(e, fleet);
            int registered = 0;
            for (drv::BondCar* c : fleet.cars)
                if (drv::ai_of(*c) && drv::ai_of(*c)->controller == 4) ++registered;
            std::printf("  a frota: o vanquish e %d carros do tráfego (%d motoristas do tráfego registrados)\n",
                        int(fleet.cars.size()) - 1, registered);
            const int traffic_cases = std::max(1, cases / 4);
            failures += check_spawn(e, fleet, traffic_cases, rng);
            failures += check_simple_car(e, fleet, traffic_cases, rng);
            failures += check_driver(e, fleet, traffic_cases, rng);
            failures += check_triggers(e, fleet, track, traffic_cases, rng);
            failures += check_smackables(e, fleet, track, traffic_cases, rng);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    drv::traffic_world().camera = nullptr;
    drv::WTriggerManager_Reset();
    for (drv::BondCar* car : fleet.cars) delete car;
    drv::simulation().cars.clear();
    drv::simulation_reset(60);
    drv::set_world(nullptr);
    return failures;
}

#include "driving_check_ai.inc"
// A parte 20: as armas e a direção automática dos veículos da IA (a mesma máquina).
#include "driving_check_integ_ai_vehicle.inc"

}  // namespace drvcheck
