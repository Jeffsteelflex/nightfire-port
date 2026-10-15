// nf_driving_check, parte 19 (tests/driving_check_heli.h), o trilho do carro do jogador: o código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS com a VU0 ao lado do porte (driving/game/rail.cpp), com o Vanquish montado pelo
// construtor do porte (a classe 0, o seu AIGroundVehicle registrado pelo AIVehicleController), o mundo de colisão de
// Paris (o chão sob as rodas) e os 'AISp' de Paris com os seus 'pt'. Cada cena põe o estado do porte na RAM emulada uma
// vez e depois os dois lados andam sozinhos, quadro a quadro:
//  - os destrutores dos eventos do trilho, chamados como o EventManager::RunEvents (com o 3): 202 EControlToSpline
//    (0x0014ae60: o AIGroundVehicle::EngageSplinePath 0x00131508 pela tabela virtual, com o PBondCar::SetCarClass
//    0x00193000 e InitializeCarControls 0x001872d8, e o RPlayerCamera::SetControlToCPU 0x001bd198), 239
//    EEndControlToSpline (0x0014c110: o DisengageSplinePath 0x00131590 com o SetAttackMode 0x0012e6f8 e o
//    SetNavigateMode 0x0012e468 originais), 209 ESetNextAISpline (0x00158370), 108 EAutoDriveSpeedChange (0x001482f8),
//    116 EPathSetDesiredThrottleStatic (0x00152cb0), 153 EPathSetTimeStatic (0x00153340) e 215
//    EPathChangeThrottleStatic (0x00151dc0) com o EPathUtil_GetFromInstanceOrAI (0x00145d00), 42 EControlToCPU
//    (0x0014ac38: o AIGroundVehicle::EngageAutoDrive 0x00130e88) e 43 EControlToPlayer (0x0014ad18: o
//    DisengageAutoDrive 0x00130f98, PBondCar::SetTargetGas 0x001930b0 e SetTargetBrake 0x001930b8, o IOModule +4),
//    sorteados entre os quadros;
//  - a cada quadro o RPathEngine::Update (0x001b2058), o AIGroundVehicle::DoSplinePathUpdate (0x00131620, com o
//    RigidBody::ResetObject) no estado 3 e o PBondCar::ProcessSplinePhysics (0x00192498, com o
//    TempGetHeightInformation sobre a cópia do mundo de Paris) com um trilho;
//  - compara o PBondCar, o RigidBody, o RigidBodyInfo, as rodas (WWorldPos), o AIGroundVehicle, o seu WRoadNav, o
//    AISplinePath, a lista do RPathEngine (cada RPathHandle) e o RPlayerCamState +0x10 da câmera.
// O CarPhysics sorteia às vezes IS_SNOWMOBILE e SUB_PHYSICS (os ramos sem molas e sem nada). O WRoadNav::InitAtPoint
// (0x00225c48, conferido na parte 14) do EngageAutoDrive e do SetNavigateMode(4) é um gancho que anota os argumentos:
// eles são comparados com o ponto e a frente do corpo do porte, e o navegador do porte (o do InitAtPoint de verdade)
// volta para a RAM emulada depois do 42.
// Os eventos com um elemento da IA (AIElementController::RedirectCarpElement) ou com uma instância
// (WWorld::GetSceneObjFromInstance) vão por ganchos que devolvem um registro sem veículo e nenhum objeto de cena: o
// original não faz nada, como o porte (que só conta); o 239 com um elemento da IA não é sorteado (o original leria o
// veículo do registro).
// O que não é comparado: os ponteiros (os do carro como na parte 6; no AIGroundVehicle o carro, o trilho pela presença,
// os navegadores e os alvos; no AISplinePath o caminho do estado e o RPathHandle; no RPathHandle a instância, o estado e
// o nome, os caminhos pelo que apontam), o +0x90 do WRoadNav (o RCameraSpline) e a linha 3 do tensor inverso no
// RigidBodyInfo (+0x70: lixo da pilha do RigidBody::ResetObject no PS2).
// Fora do código original (ganchos nativos): memcpy, memset, FastAlloc/FastFree, __builtin_new/__builtin_delete (um
// alocador em faixa), o delete dos eventos (0x00175758), cosf/sinf (o nf::fm do porte), os dois acima, o
// WRoadNav::InitAtPoint, AttributeSet::LookupBool ("IS_HELI" e "HAS_SIREN" do carro do porte) e o
// IOModule::GetIOModule (0x00167d80: um objeto emulado).

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/bigf.h"
#include "driving/camera/player_camera.h"
#include "driving/carp_runtime.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/camera_events.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/triggers.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/render/path_engine.h"
#include "driving/track.h"
#include "driving_check_heli.h"
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
constexpr uint32_t kFnEngineUpdate = 0x1b2058, kFnDoSplineUpdate = 0x131620, kFnSplinePhysics = 0x192498;
constexpr uint32_t kFnEvControlToSpline = 0x14ae60, kFnEvEndControlToSpline = 0x14c110, kFnEvNextAISpline = 0x158370,
                   kFnEvSpeedChange = 0x1482f8, kFnEvDesiredThrottle = 0x152cb0, kFnEvSetTime = 0x153340,
                   kFnEvChangeThrottle = 0x151dc0, kFnEvControlToCPU = 0x14ac38, kFnEvControlToPlayer = 0x14ad18;
// Os globais.
constexpr uint32_t kRate = 0x333ff0, kFrequency = 0x333ff4;  // RTimeData
constexpr uint32_t kPathList = 0x3339a8;                      // RPathEngine::fgPathHandles
constexpr uint32_t kSim = 0x339190;
constexpr uint32_t kWorldCameras = 0x333ed8;
constexpr uint32_t kRigidScratchPtr = 0x339178;
constexpr uint32_t kNavSpread = 0x327c38;  // o 0x327c38 do SetNavigateMode(4)
constexpr uint32_t kBondCarVtable = 0x379ef0, kGroundVehicleVtable = 0x36b568;

// A RAM emulada.
constexpr uint32_t kPaths = 0x00500000;     // os 'pt' de Paris
constexpr uint32_t kSplines = 0x00600000;   // os 'AISp' de Paris (0x80 cada)
constexpr uint32_t kCar = 0x00640000;       // o PBondCar
constexpr uint32_t kGv = 0x00641000;        // o AIGroundVehicle
constexpr uint32_t kCarList = 0x00642000;   // o vetor de carros da Simulation (+0xdcc)
constexpr uint32_t kCamera = 0x00643000;    // o RPlayerCamera (só o +0x354)
constexpr uint32_t kCamState = 0x00644000;  // o seu RPlayerCamState
constexpr uint32_t kCamList = 0x00645000;   // a lista das câmeras do mundo
constexpr uint32_t kEvent = 0x00646000;     // o objeto do evento
constexpr uint32_t kPhysics = 0x00647000;   // o CarPhysics
constexpr uint32_t kMgr = 0x00648000, kGrid = 0x00648100;
constexpr uint32_t kNav = 0x00649000;       // o WRoadNav (0xc0) e o seu RCameraSpline (+0xc0)
constexpr uint32_t kRedirect = 0x0064a000;  // o registro do AIElementController (sem veículo)
constexpr uint32_t kRigidScratch = 0x0064b000;
constexpr uint32_t kIoModule = 0x0064c000;  // o IOModule (o +4)
constexpr uint32_t kBodies = 0x00650000;    // 48 x 0x80
constexpr uint32_t kInfos = 0x00652000;     // 48 x 0x500
constexpr uint32_t kHeapBegin = 0x00700000, kHeapEnd = 0x00f00000;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

uint32_t body_addr(int slot) { return kBodies + 0x80u * uint32_t(slot); }
uint32_t info_addr(int slot) { return kInfos + 0x500u * uint32_t(slot); }

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x7a11) {}
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

class RailEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    uint64_t redirects = 0, scene_objs = 0, deletes = 0;
    // Os InitAtPoint anotados (o navegador, o ponto, a frente, o terceiro argumento) e o carro das consultas de atributo.
    struct NavInit {
        uint32_t nav;
        uint32_t pos[3], dir[3];
        uint32_t flag;
    };
    std::vector<NavInit> inits;
    bool is_heli = false, has_siren = false;

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
        m.hook(0x175758, "Event::operator_delete", [this](MipsMachine&) { ++deletes; });
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(0x12c608, "AIElementController::RedirectCarpElement", [this](MipsMachine& mm) {
            ++redirects;
            mm.set_gpr(R::v0, kRedirect);
        });
        m.hook(0x230bf0, "WWorld::GetSceneObjFromInstance", [this](MipsMachine& mm) {
            ++scene_objs;
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x225c48, "WRoadNav::InitAtPoint (anotado)", [this](MipsMachine& mm) {
            NavInit n{};
            n.nav = uint32_t(mm.gpr(R::a0));
            for (int i = 0; i < 3; ++i) {
                n.pos[i] = mm.read32(uint32_t(mm.gpr(R::a1)) + 4u * uint32_t(i));
                n.dir[i] = mm.read32(uint32_t(mm.gpr(R::a2)) + 4u * uint32_t(i));
            }
            n.flag = uint32_t(mm.gpr(R::a3) & 0xff);
            inits.push_back(n);
        });
        m.hook(0x169b28, "AttributeSet::LookupBool", [this](MipsMachine& mm) {
            const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            bool v = false;
            if (std::strcmp(key, "IS_HELI") == 0) v = is_heli;
            if (std::strcmp(key, "HAS_SIREN") == 0) v = has_siren;
            mm.set_gpr(R::v0, v ? 1 : 0);
        });
        m.hook(0x167d80, "IOModule::GetIOModule", [](MipsMachine& mm) { mm.set_gpr(R::v0, kIoModule); });
        // O registro do elemento da IA: sem veículo (+0x44 e +0x48 diferentes de 1, +0x50 nulo).
        std::memset(m.mem(kRedirect, 0x60), 0, 0x60);
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
};

// O mundo, os caminhos e os trilhos (comuns às cenas).
struct RailWorld {
    drv::TrackWorld* tw = nullptr;
    WorldCopy wc;
    std::map<uint32_t, uint32_t> path_emu;  // p32 PathInfo do porte -> endereço emulado
    std::vector<const drv::AISplineRec*> splines;
    std::vector<uint32_t> spline_emu;
};

// A lista do RPathEngine emulada (os nós +8).
std::vector<uint32_t> emu_list(RailEmu& e) {
    std::vector<uint32_t> out;
    const uint32_t lp = e.m.read32(kPathList);
    if (lp == 0) return out;
    const uint32_t head = e.m.read32(lp);
    for (uint32_t n = e.m.read32(head); n != head && out.size() < 10000; n = e.m.read32(n)) out.push_back(n + 8);
    return out;
}

// O estado do porte na RAM emulada (no começo de cada cena).
void put_scene(RailEmu& e, drv::BondCar& car, drv::AIGroundVehicle& v, const RailWorld& w,
               const drv::PlayerCamera& cam) {
    drv::Simulation& sim = drv::simulation();
    const int slot = car.d.slot;
    drv::RigidBody* rb = car.body();
    drv::RigidBody b = *rb;
    e.put(body_addr(slot), &b, 0x80);
    e.m.write32(body_addr(slot) + 0x5c, info_addr(slot));
    drv::RigidBodyInfo info = *rb->info;
    e.put(info_addr(slot), &info, 0x500);
    for (int i = 0; i < 16; ++i) e.m.write32(info_addr(slot) + 0x380 + 4u * uint32_t(i), 0);
    e.m.write32(info_addr(slot) + 0x4d8, 0);
    drv::PBondCarData d = car.d;
    e.put(kCar, &d, 0x460);
    uint8_t wp[0x40];
    wp_to_emu(car.d.world_pos, *w.tw, w.wc, wp);
    e.put(kCar, wp, 0x40);
    for (int k = 0; k < 4; ++k) {
        wp_to_emu(car.d.wheels[k], *w.tw, w.wc, wp);
        e.put(kCar + 0x90 + 0x40u * uint32_t(k), wp, 0x40);
    }
    e.m.write32(kCar + 0x48, 0);
    e.m.write32(kCar + 0x4c, 0);
    e.m.write32(kCar + 0x50, 0);
    e.m.write32(kCar + 0x54, 0);
    e.m.write32(kCar + 0x5c, 0);
    e.m.write32(kCar + 0x68, kBondCarVtable);
    e.put(kPhysics, car.physics(), 0x100);
    e.m.write32(kCar + 0x23c, kPhysics);
    e.m.write32(kCar + 0x26c, 0);
    e.m.write32(kCar + 0x2cc, 0);
    e.m.write32(kCar + 0x2e0, 0);
    e.m.write32(kCar + 0x2e4, kGv);
    e.m.write32(kCar + 0x388, 0);
    e.m.write32(kCar + 0x434, 0);
    // O AIGroundVehicle (sem trilho no começo da cena) e o seu navegador.
    uint8_t raw[drv::kAIGroundVehicleSize];
    std::memcpy(raw, &v, sizeof raw);
    auto w32 = [&raw](uint32_t off, uint32_t val) { std::memcpy(raw + off, &val, 4); };
    w32(0x04, kCar);
    w32(0x50, 0);
    w32(0x54, kNav);
    w32(0x6c, kGroundVehicleVtable);
    w32(0xf0, 0);
    w32(0x100, 0);
    w32(0x110, 0);
    w32(0x114, 0);
    w32(0x1a4, 0);
    w32(0x200, 0);
    e.put(kGv, raw, sizeof raw);
    const drv::WRoadNav& nav = *drv::nav_of(v);
    drv::WRoadNav nc = nav;
    nc.spline = kNav + 0xc0;
    e.put(kNav, &nc, 0xc0);
    e.put(kNav + 0xc0, drv::road_nav_spline(nav), 0x64);
    // A Simulation: os corpos, o passo, a taxa, o dono da vaga e o vetor de carros.
    e.m.write32(kSim + 0x04, 0);
    e.m.write32(kSim + 0x1c, kBodies);
    e.m.write32(kSim + 0x24, kInfos);
    e.m.write32(kSim + 0x270 + 4u * uint32_t(slot), kCar);
    e.m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
    std::memcpy(e.m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
    e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
    e.m.write32(kSim + 0xdcc, kCarList);
    e.m.write32(kSim + 0xdd0, kCarList + 4);
    e.m.write32(kCarList, kCar);
    e.m.write32(kRigidScratchPtr, kRigidScratch);
    // A câmera: a lista do mundo [1] -> o RPlayerCamera -> +0x354 o estado.
    e.m.write32(kWorldCameras, kCamList);
    e.m.write32(kCamList, 0);
    e.m.write32(kCamList + 4, kCamera);
    e.m.write32(kCamera + 0x354, kCamState);
    e.m.write32(kCamState + 0x10, cam.state.cpu_control);
    // As marcas das consultas ao chão.
    const drv::WCollisionMgr& mgr = w.tw->mgr();
    e.m.write32(w.wc.mgr + 4, mgr.query_stamp);
    e.m.write32(w.wc.mgr + 0x2c, mgr.barrier_mask);
    e.m.write32(w.wc.mgr + 0x30, mgr.vu0_path);
    for (uint32_t i = 0; i < w.wc.count; ++i)
        e.m.write32(w.wc.insts + 0x40 * i + 0x10, w.tw->instances()[i].query_stamp);
    // A lista do RPathEngine vazia (o porte também), o espalhamento do modo 4 e o IOModule.
    e.m.write32(kPathList, 0);
    std::memcpy(e.m.mem(kNavSpread, 4), &drv::vehicle_controller().nav_spread, 4);
    e.m.write32(kIoModule + 4, uint32_t(drv::mission_env().io_updating));
    e.is_heli = car.is_heli;
    e.has_siren = car.has_siren;
}

// O RPathHandle emulado contra o do porte (os ponteiros pelo que apontam; sem mestre no trilho).
bool same_handle(RailEmu& e, uint32_t he, const drv::RPathHandle& hp, const RailWorld& w, std::string& what) {
    if (!same_words("RPathHandle", e.m.mem(he, 0xd4), &hp, 0xd4, {{0x00, 0x08}, {0x98, 0xa4}, {0xd0, 0xd4}}, what))
        return false;
    auto ptr = [&](uint32_t off, uint32_t port) {
        const uint32_t ev = e.m.read32(he + off);
        const uint32_t want = port == 0 ? 0 : (w.path_emu.count(port) ? w.path_emu.at(port) : 0xffffffffu);
        return ev == want;
    };
    if (!ptr(0x98, hp.current)) {
        what = "RPathHandle: o caminho corrente";
        return false;
    }
    if (!ptr(0x9c, hp.next)) {
        what = "RPathHandle: o caminho seguinte";
        return false;
    }
    if (e.m.read32(he + 0xa0) != 0 || hp.master != 0) {
        what = "RPathHandle: um mestre";
        return false;
    }
    return true;
}

// Compara tudo; o primeiro diferente (vazio: iguais).
std::string compare(RailEmu& e, drv::BondCar& car, drv::AIGroundVehicle& v, const RailWorld& w,
                    const drv::PlayerCamera& cam, bool with_nav = true) {
    std::string out;
    const int slot = car.d.slot;
    drv::RigidBody* rb = car.body();
    if (!same_words("RigidBody", e.m.mem(body_addr(slot), 0x80), rb, 0x80, {{0x5c, 0x60}}, out)) return out;
    // +0x070: a linha 3 do tensor inverso (lixo da pilha do ResetObject no PS2, como na parte 14).
    if (!same_words("RigidBodyInfo", e.m.mem(info_addr(slot), 0x500), nf::raw(rb->info), 0x500,
                    {{0x70, 0x80}, {0x380, 0x3c0}, {0x4d8, 0x4dc}}, out))
        return out;
    const uint8_t* ec = e.m.mem(kCar, 0x460);
    if (!same_words("PBondCar", ec, &car.d, 0x460,
                    {{0x00, 0x40}, {0x48, 0x6c}, {0x74, 0x84}, {0x90, 0x190}, {0x23c, 0x240}, {0x26c, 0x270},
                     {0x2cc, 0x2d0}, {0x2e0, 0x2e8}, {0x388, 0x38c}, {0x434, 0x438}},
                    out))
        return out;
    for (int k = 0; k < 4; ++k) {
        const drv::WWorldPos o = wp_from_emu(ec + 0x90 + 0x40 * k, *w.tw, w.wc);
        if (!same_words(("rodas[" + std::to_string(k) + "]").c_str(), reinterpret_cast<const uint8_t*>(&o),
                        &car.d.wheels[k], 0x40, {{0x0c, 0x10}}, out))
            return out;
    }
    if (!same_words("AIGroundVehicle", e.m.mem(kGv, drv::kAIGroundVehicleSize), &v, drv::kAIGroundVehicleSize,
                    {{0x04, 0x08}, {0x50, 0x58}, {0x6c, 0x70}, {0xf0, 0xf4}, {0x100, 0x104}, {0x110, 0x118},
                     {0x1a4, 0x1a8}, {0x200, 0x204}},
                    out))
        return out;
    if (e.m.read32(kGv + 0x1a4) != 0 || v.primary_target != 0) return "AIGroundVehicle +0x1a4: um alvo";
    if (with_nav && !same_words("WRoadNav", e.m.mem(kNav, 0xc0), drv::nav_of(v), 0xc0, {{0x90, 0x94}}, out)) return out;
    if (e.m.read32(kNavSpread) != ps2::fbits(drv::vehicle_controller().nav_spread))
        return "0x327c38 (o espalhamento do modo 4): original " + hex(e.m.read32(kNavSpread)) + ", porte " +
               hex(ps2::fbits(drv::vehicle_controller().nav_spread));
    if (e.m.read32(kIoModule + 4) != uint32_t(drv::mission_env().io_updating)) return "o IOModule +4";
    if (e.m.read32(kCamState + 0x10) != cam.state.cpu_control)
        return "RPlayerCamState +0x10: original " + hex(e.m.read32(kCamState + 0x10)) + ", porte " +
               hex(cam.state.cpu_control);
    // O trilho.
    const uint32_t spe = e.m.read32(kGv + 0x50);
    const drv::AISplinePath* spp = nf::p32_ptr<const drv::AISplinePath>(v.spline_path);
    if ((spe == 0) != (spp == nullptr)) return "AIGroundVehicle +0x50: o trilho";
    if (spp) {
        if (!same_words("AISplinePath", e.m.mem(spe, 0xb0), spp, 0xb0, {{0x4c, 0x50}, {0x60, 0x70}}, out)) return out;
        const uint32_t want = spp->state.path ? (w.path_emu.count(spp->state.path) ? w.path_emu.at(spp->state.path)
                                                                                   : 0xffffffffu)
                                              : 0;
        if (e.m.read32(spe + 0x4c) != want) return "AISplinePath: o caminho do estado";
    }
    // A lista do RPathEngine.
    const std::vector<uint32_t> le = emu_list(e);
    const std::vector<drv::RPathHandle*>& lp = drv::path_engine_handles();
    if (le.size() != lp.size())
        return "a lista do RPathEngine: original " + std::to_string(le.size()) + ", porte " + std::to_string(lp.size());
    for (size_t k = 0; k < le.size(); ++k) {
        if (!same_handle(e, le[k], *lp[k], w, out)) return out + " (nó " + std::to_string(k) + ")";
        // O RPathHandle do trilho move a instância do AISplinePath.
        if (spp && nf::p32_ptr<const drv::RPathHandle>(spp->handle) == lp[k]) {
            if (e.m.read32(le[k]) != spe || e.m.read32(spe + 0x60) != le[k]) return "o RPathHandle do trilho";
        }
    }
    return out;
}

}  // namespace

int run_heli_rail_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                         int scenes, uint32_t seed) {
    int failures = 0;
    RailEmu* e = new RailEmu();
    drv::BondCar* car = nullptr;
    drv::TrackWorld* tw = nullptr;
    drv::PlayerCamera* cam = nullptr;
    const drv::CameraEventsEnv saved_env = drv::camera_events_env();
    try {
        drv::RTimeData_Init(60.0f);
        e->setup(elf, vu0_program, vu0_size);
        drv::Track track;
        std::string error;
        if (!track.load(disc, "paris_mis01", "", error)) {
            std::printf("  trilho: paris_mis01 não carregou (%s)\n", error.c_str());
            delete e;
            return 1;
        }
        RailWorld w;
        tw = new drv::TrackWorld();
        if (!tw->init(track.root(), &error)) {
            std::printf("  trilho: o mundo de colisão não montou (%s)\n", error.c_str());
            delete tw;
            delete e;
            return 1;
        }
        w.tw = tw;
        if (!copy_world(e->m, *tw, w.wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  trilho: %s\n", error.c_str());
            delete tw;
            delete e;
            return 1;
        }
        drv::set_world(tw);
        // A rede de estradas de Paris no porte (o InitAtPoint de verdade do 42 a consulta).
        drv::WRoadNetwork_Shutdown();
        drv::WRoadNetwork_Init(track.root(), "paris_mis01");
        // Os 'pt' e os 'AISp' de Paris na RAM emulada.
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        uint32_t at = kPaths;
        for (uint32_t i = 1; i < 0x100; ++i) {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x70740000u | i);
            if (d == drv::UGroup_DataEnd(map)) continue;
            const uint32_t size = drv::UData_Size(d);
            e->put(at, drv::UData_Data(d), size);
            w.path_emu[nf::p32_u(drv::UData_Data(d))] = at;
            at = (at + size + 15) & ~15u;
        }
        {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x41495370u);
            if (d != drv::UGroup_DataEnd(map)) {
                const auto* s = reinterpret_cast<const drv::AISplineRec*>(drv::UData_Data(d));
                for (uint32_t i = 0; i < d->count; ++i) {
                    if (s[i].path == 0 || !w.path_emu.count(s[i].path)) continue;
                    drv::AISplineRec c = s[i];
                    c.path = w.path_emu.at(s[i].path);
                    const uint32_t a = kSplines + 0x80u * uint32_t(w.splines.size());
                    e->put(a, &c, sizeof c);
                    w.splines.push_back(&s[i]);
                    w.spline_emu.push_back(a);
                }
            }
        }
        if (w.splines.empty()) {
            std::printf("  trilho: Paris sem 'AISp'\n");
            drv::set_world(nullptr);
            delete tw;
            delete e;
            return 1;
        }
        // Um 'AISp' sem caminho (o +0x40 nulo: os eventos não fazem nada).
        static drv::AISplineRec empty_spline;
        empty_spline = *w.splines[0];
        empty_spline.path = 0;
        const uint32_t empty_emu = kSplines + 0x80u * uint32_t(w.splines.size());
        e->put(empty_emu, &empty_spline, sizeof empty_spline);
        // O Vanquish do jogador (a classe 0) e o seu AIGroundVehicle.
        drv::Bigf arc;
        drv::AttributeCollection attrs;
        drv::CarDesc desc;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || !attrs.load(arc, "pvehicle", "vanquish", "paris_mis01", &error) ||
            !drv::read_car_desc(arc, "vanquish", desc, &error)) {
            std::printf("  trilho: o vanquish (%s)\n", error.c_str());
            drv::set_world(nullptr);
            delete tw;
            delete e;
            return 1;
        }
        drv::reset_rigid_globals();
        drv::reset_bond_car_tuning();
        drv::physics_pending_reset();
        drv::simulation_reset(60);
        drv::RPathEngine_Purge();
        car = new drv::BondCar();
        {
            const float dir[3] = {0.0f, 0.0f, 1.0f}, pos[3] = {0.0f, 1.0f, 0.0f};
            if (!drv::PBondCar_Construct(*car, 0, "vanquish", desc, attrs, dir, pos)) {
                std::printf("  trilho: sem vaga para o corpo do vanquish\n");
                drv::set_world(nullptr);
                delete car;
                delete tw;
                delete e;
                return 1;
            }
        }
        drv::simulation().cars.push_back(car);
        drv::AIVehicleController_Init();
        drv::AIVehicleController_Restart();
        drv::AIVehicleController_RegisterSimCarList();
        drv::AIGroundVehicle* v = drv::ai_of(*car);
        if (!v) {
            std::printf("  trilho: o vanquish sem AIGroundVehicle\n");
            throw nf::MipsError("sem AIGroundVehicle", 0, 0);
        }
        cam = new drv::PlayerCamera();
        drv::camera_events_env().camera = cam;
        drv::camera_events_env().ctx = nullptr;
        // O CarPhysics do carro numa cópia (as bandeiras sorteadas por cena).
        static drv::CarPhysics phys;
        phys = *car->physics();
        const drv::CarPhysics phys0 = phys;
        const uint32_t saved_physics = car->d.physics;
        car->d.physics = nf::p32_u(&phys);
        std::vector<uint32_t> ground;
        for (uint32_t i = 0; i < tw->instance_count(); ++i)
            if (tw->instances()[i].article && !(tw->instances()[i].flags & 1)) ground.push_back(i);

        Rng rng(seed ^ 0x2a11u);
        Tally t_ev[9], t_frame, t_update, t_physics;
        static const char* const kEvNames[9] = {
            "202 EControlToSpline (EngageSplinePath, SetControlToCPU)",
            "239 EEndControlToSpline (DisengageSplinePath)",
            "209 ESetNextAISpline (SetNextSplinePath)",
            "108 EAutoDriveSpeedChange",
            "116 EPathSetDesiredThrottleStatic",
            "153 EPathSetTimeStatic",
            "215 EPathChangeThrottleStatic",
            "42 EControlToCPU (EngageAutoDrive)",
            "43 EControlToPlayer (DisengageAutoDrive)",
        };
        static const uint32_t kEvFns[9] = {kFnEvControlToSpline, kFnEvEndControlToSpline, kFnEvNextAISpline,
                                           kFnEvSpeedChange,     kFnEvDesiredThrottle,    kFnEvSetTime,
                                           kFnEvChangeThrottle,  kFnEvControlToCPU,       kFnEvControlToPlayer};
        static const int32_t kEvTypes[9] = {202, 239, 209, 108, 116, 153, 215, 42, 43};
        uint64_t engaged_frames = 0, physics_frames = 0, snow = 0, sub = 0;
        for (int sc = 0; sc < scenes; ++sc) {
            // A cena: o carro sem trilho, num ponto do chão de Paris, com o estado das rodas sorteado.
            drv::AIVehicle_SetSplinePath(*v, nullptr);
            drv::RPathEngine_Purge();
            e->heap = kHeapBegin;
            v->controller = 1;
            v->active = 1;
            v->valid_command = 1;
            v->rec_speed_offset = rng.chance(0.5) ? 1.0f : rng.uni(0.0f, 2.0f);
            car->d.car_class = 0;
            phys = phys0;
            if (rng.chance(0.1)) {
                phys.is_snowmobile = 1;
                ++snow;
            } else if (rng.chance(0.05)) {
                phys.sub_physics = 1;
                ++sub;
            }
            drv::simulation().steps = rng.range(0, 20000);
            drv::RigidBody* rb = car->body();
            {
                const drv::WCollisionInstance& ci = tw->instances()[ground[rng.next() % ground.size()]];
                float c[3];
                drv::WCollisionInstance_CalcPosition(tw->mgr(), ci, c);
                float p[3] = {c[0] + rng.uni(-4, 4), c[1] + 2.0f, c[2] + rng.uni(-4, 4)};
                float h = 0;
                if (tw->world_height_at_point(p, h)) p[1] = h + rng.uni(0.3f, 1.4f);
                std::memcpy(rb->pos, p, 12);
                for (int i = 0; i < 3; ++i) {
                    rb->vel[i] = rng.uni(-10, 10);
                    rb->momentum[i] = rb->vel[i] * rb->mass;
                }
            }
            drv::PBondCarData& c = car->d;
            c.control.gas = rng.uni(0, 1);
            c.control.brake = rng.chance(0.5) ? 0.0f : rng.uni(0, 1);
            c.control.steering = rng.uni(-1, 1);
            c.steer_target = rng.uni(-1, 1);
            c.axis_gas = rng.uni(0, 1);
            c.wheel_spin_angle[0] = rng.uni(-0.2f, 1.2f);
            c.wheel_spin_angle[1] = rng.uni(-0.2f, 1.2f);
            for (int k = 0; k < 4; ++k) {
                c.wheel_slip[k] = rng.uni(0, 1);
                c.suspension[k] = rng.uni(0, 0.4f);
            }
            c.skidding = uint8_t(rng.chance(0.3));
            if (rng.chance(0.3))
                for (auto& wh : c.wheels) wh = drv::WWorldPos{};
            cam->state.cpu_control = uint32_t(rng.range(0, 1));
            put_scene(*e, *car, *v, w, *cam);
            std::string why = compare(*e, *car, *v, w, *cam);
            if (!why.empty()) {
                t_frame.fail("cena " + std::to_string(sc) + ", a cópia: " + why);
                continue;
            }
            // Um evento: o objeto na RAM emulada e o PendingEvent do porte.
            auto run_event = [&](int k, uint32_t o1, uint32_t o2, uint32_t o3, uint32_t o4, uint32_t o5) {
                drv::PendingEvent pe;
                pe.type = kEvTypes[k];
                pe.obj[1] = o1;
                pe.obj[2] = o2;
                pe.obj[3] = o3;
                pe.obj[4] = o4;
                pe.obj[5] = o5;
                uint32_t eo[6] = {0, o1, o2, o3, o4, o5};
                // O 'AISp' do porte (p32) vira o emulado na palavra 2 dos 202/209.
                if (k == 0 || k == 2) {
                    if (o2 == nf::p32_u(&empty_spline)) eo[2] = empty_emu;
                    for (size_t i = 0; i < w.splines.size(); ++i)
                        if (o2 == nf::p32_u(w.splines[i])) eo[2] = w.spline_emu[i];
                }
                for (int i = 0; i < 6; ++i) e->m.write32(kEvent + 4u * uint32_t(i), eo[i]);
                ++t_ev[k].cases;
                e->inits.clear();
                e->call(kEvFns[k], {kEvent, 3});
                drv::rail_event_execute(pe);
                const bool autodrive = kEvTypes[k] == 42;
                std::string d = compare(*e, *car, *v, w, *cam, !autodrive);
                if (d.empty() && autodrive) {
                    // Os dois InitAtPoint (o do SetNavigateMode(4) e o do EngageAutoDrive) com o ponto e a frente do
                    // corpo do porte; o modo 4 do navegador; depois o navegador do porte na RAM emulada.
                    const drv::RigidBody* rb = car->body();
                    const float* fwd = nf::raw(rb->info)->orient[2];
                    if (e->inits.size() != 2) {
                        d = "InitAtPoint: " + std::to_string(e->inits.size()) + " chamadas no original";
                    } else {
                        for (size_t n = 0; n < 2 && d.empty(); ++n) {
                            const RailEmu::NavInit& ni = e->inits[n];
                            if (ni.nav != kNav) d = "InitAtPoint: o navegador";
                            for (int i = 0; i < 3 && d.empty(); ++i)
                                if (ni.pos[i] != ps2::fbits(rb->pos[i]) || ni.dir[i] != ps2::fbits(fwd[i]))
                                    d = "InitAtPoint " + std::to_string(n) + ": o ponto ou a frente";
                            const uint32_t want = n == 0 ? uint32_t(v->coll_avoidance == 0 ? 1 : 0) : 0u;
                            if (d.empty() && ni.flag != want) d = "InitAtPoint " + std::to_string(n) + ": o terceiro argumento";
                        }
                    }
                    if (d.empty() && (e->m.read32(kNav) != 4 || drv::nav_of(*v)->mode != 4)) d = "o modo 4 do navegador";
                    const drv::WRoadNav& nav = *drv::nav_of(*v);
                    drv::WRoadNav nc = nav;
                    nc.spline = kNav + 0xc0;
                    e->put(kNav, &nc, 0xc0);
                    e->put(kNav + 0xc0, drv::road_nav_spline(nav), 0x64);
                }
                if (!d.empty()) t_ev[k].fail("cena " + std::to_string(sc) + ": " + d);
                return d.empty();
            };
            auto pick_spline = [&]() -> uint32_t {
                if (rng.chance(0.08)) return nf::p32_u(&empty_spline);
                return nf::p32_u(w.splines[size_t(rng.range(0, int(w.splines.size()) - 1))]);
            };
            auto element = [&]() -> uint32_t { return rng.chance(0.08) ? uint32_t(rng.range(1, 30)) : 0u; };
            // O 202 (às vezes não: um 209 cria o trilho sem a CPU).
            bool ok = true;
            if (rng.chance(0.85)) ok = run_event(0, element(), pick_spline(), 0, 0, 0);
            else ok = run_event(2, 0, pick_spline(), 0, 0, 0);
            const int frames = rng.range(1, 40);
            for (int f = 0; ok && f < frames; ++f) {
                // Os eventos entre os quadros.
                if (rng.chance(0.12)) {
                    const int k = rng.range(2, 6);
                    const uint32_t inst = rng.chance(0.06) ? uint32_t(rng.range(1, 2000)) : 0u;
                    switch (k) {
                        case 2: ok = run_event(2, element(), pick_spline(), 0, 0, 0); break;
                        case 3: ok = run_event(3, ps2::fbits(rng.uni(0.0f, 2.0f)), 0, 0, 0, 0); break;
                        case 4: ok = run_event(4, inst, element(), ps2::fbits(rng.uni(-0.5f, 2.0f)), 0, 0); break;
                        case 5: ok = run_event(5, inst, element(), ps2::fbits(rng.uni(-0.2f, 1.2f)), 0, 0); break;
                        default:
                            ok = run_event(6, inst, element(), uint32_t(rng.range(0, 90)),
                                           ps2::fbits(rng.uni(0.0f, 0.2f)), ps2::fbits(rng.uni(0.0f, 2.0f)));
                            break;
                    }
                    if (!ok) break;
                }
                if (rng.chance(0.03)) {
                    ok = run_event(1, 0, 0, 0, 0, 0);
                    if (!ok) break;
                }
                if (rng.chance(0.02)) {
                    ok = run_event(rng.chance(0.5) ? 7 : 8, 0, 0, 0, 0, 0);
                    if (!ok) break;
                }
                if (rng.chance(0.03)) {
                    ok = run_event(0, 0, pick_spline(), 0, 0, 0);
                    if (!ok) break;
                }
                // O quadro: o RPathEngine (o EAnimUpdate), o AIGroundVehicle no estado 3, o carro com trilho.
                ++t_frame.cases;
                const float now = ps2::ee::cvt_s(drv::simulation().steps);
                e->call(kFnEngineUpdate, {}, now);
                drv::RPathEngine_Update(now);
                std::string d = compare(*e, *car, *v, w, *cam);
                if (!d.empty()) {
                    t_frame.fail("cena " + std::to_string(sc) + " quadro " + std::to_string(f) + ": " + d);
                    break;
                }
                if (v->controller == 3 && v->spline_path != 0) {
                    ++t_update.cases;
                    ++engaged_frames;
                    e->call(kFnDoSplineUpdate, {kGv});
                    drv::AIGroundVehicle_DoSplinePathUpdate(*v);
                    d = compare(*e, *car, *v, w, *cam);
                    if (!d.empty()) {
                        t_update.fail("cena " + std::to_string(sc) + " quadro " + std::to_string(f) + ": " + d);
                        break;
                    }
                }
                if (v->spline_path != 0) {
                    ++t_physics.cases;
                    ++physics_frames;
                    e->call(kFnSplinePhysics, {kCar});
                    drv::PBondCar_ProcessSplinePhysics(*car);
                    d = compare(*e, *car, *v, w, *cam);
                    if (!d.empty()) {
                        t_physics.fail("cena " + std::to_string(sc) + " quadro " + std::to_string(f) + ": " + d);
                        break;
                    }
                }
                ++drv::simulation().steps;
                e->m.write32(kSim + 0xdc4, uint32_t(drv::simulation().steps));
            }
            // O fim da cena: o 239 com a CPU no trilho.
            if (ok && v->controller == 3) run_event(1, 0, 0, 0, 0, 0);
        }
        std::printf("    cenas %d: quadros com o trilho na CPU %" PRIu64 ", com o ProcessSplinePhysics %" PRIu64
                    " (IS_SNOWMOBILE %" PRIu64 ", SUB_PHYSICS %" PRIu64 "); ganchos: elementos da IA %" PRIu64
                    ", instâncias %" PRIu64 ", deletes %" PRIu64 "\n",
                    scenes, engaged_frames, physics_frames, snow, sub, e->redirects, e->scene_objs, e->deletes);
        for (int k = 0; k < 9; ++k) report(kEvNames[k], t_ev[k], failures);
        report("RPathEngine::Update com o trilho", t_frame, failures);
        report("AIGroundVehicle::DoSplinePathUpdate (ResetObject)", t_update, failures);
        report("PBondCar::ProcessSplinePhysics (o chão de Paris)", t_physics, failures);
        drv::AIVehicle_SetSplinePath(*v, nullptr);
        car->d.physics = saved_physics;
    } catch (const std::exception& ex) {
        std::printf("  trilho: erro: %s\n", ex.what());
        ++failures;
    }
    drv::RPathEngine_Purge();
    drv::WRoadNetwork_Shutdown();
    drv::camera_events_env() = saved_env;
    drv::set_world(nullptr);
    drv::simulation().cars.clear();
    drv::simulation_reset(60);
    drv::AIVehicleController_Restart();
    delete cam;
    delete car;
    delete tw;
    delete e;
    return failures;
}

}  // namespace drvcheck
