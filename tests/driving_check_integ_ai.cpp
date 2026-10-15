// nf_driving_check, parte 20, a IA (tests/driving_check_integ.h; docs/driving.md seção 24): o código ORIGINAL do
// DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte de
//  a) a mira pela câmera (driving/camera/camera_aim.h): GetForwardAimVec4, GetAimMatrix4, CameraAiming, o
//     RCamera::CreateMatrix4Inv e o TransformPoint (0x002d18e8), com câmeras e valores sorteados;
//  b) o WTargetPicker e os WTargetable (driving/game/target_picker.h): as zonas, a tela (0x0022a128, o tan do newlib em
//     double emulado), o fora da tela, as distâncias, os comparadores, o StepCheckHitWorld e o UpdateVisibility sobre o
//     mundo de Paris, e cenas sorteadas de alvos (pontos, corpos, personagens e veículos da IA) com quadros de
//     UpdateTargets (o qsort do newlib, o UpdateSelection, o MoveTargetingCursors, o UpdateAutoDriveTargeting), os
//     registros, o CycleToNextTarget, o GetSelectedTarget e as ativações;
//  c) as armas e a direção automática dos veículos da IA (tests/driving_check_integ_ai_vehicle.inc, sobre a máquina da
//     parte 14).
//
// Ganchos nativos (os dois lados os usam iguais): a memória (FastAlloc, __builtin_new, os free), o
// Simulation::GetRigidBody, o AIVehicle::GetPosition, o ATargeting (o som da trava: o estado pedido é comparado), a
// tabela virtual do personagem (vivo) e do carro (GetPhysics), e o WCollisionMgr::CheckHitWorld (o do porte sobre o mundo
// de Paris, conferido na parte 18: assim os dois lados veem o mesmo mundo).

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/player_camera.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/target_picker.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "driving_check_integ.h"
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

// Os globais do jogo.
constexpr uint32_t kPicker = 0x33a970, kSimRate = 0x339f4c, kSimDt = 0x339f50, kSimSteps = 0x339f54,
                   kSimState = 0x339f44, kCarsVec = 0x339f5c, kWorldCams = 0x333ed8, kRendererPtr = 0x333760,
                   kScreenW = 0x424724, kScreenH = 0x424728, kAspectMult = 0x334200, kMissionPtr = 0x33a024,
                   kWeaponMgrPtr = 0x33a530, kTargetsInWorld = 0x33a964, kTargetsLive = 0x33a960;
// As funções originais.
constexpr uint32_t kFnFwdAim = 0x1bcca0, kFnAimMatrix = 0x1bcd10, kFnCameraAiming = 0x1bd2f8, kFnMatInv = 0x1a4468,
                   kFnXform3 = 0x2d18e8, kFnZoneSet = 0x2273d8, kFnZoneRange = 0x2274a0, kFnOnScreen = 0x22a728,
                   kFnScreenPos = 0x22a128, kFnOffScreen = 0x22a430, kFnDistCam = 0x229ad8, kFnDistPos = 0x229b40,
                   kFnDistCenter = 0x229b88, kFnCmpCenter = 0x22b150, kFnCmpPos = 0x22b1c0, kFnCmpCam = 0x22b2a0,
                   kFnStep = 0x216500, kFnUpdVis = 0x229870, kFnUpdPos = 0x2296d8, kFnCtorPoint = 0x229490,
                   kFnCtorObj = 0x229518, kFnCtorChar = 0x229570, kFnCtorAiv = 0x2295d8, kFnPickerCtor = 0x229f40,
                   kFnRegister = 0x22afa8, kFnUnregister = 0x22b098, kFnUpdateTargets = 0x22b318,
                   kFnActivate = 0x22b638, kFnDeactivate = 0x22b698, kFnSelected = 0x22af38, kFnCycle = 0x22b6b8,
                   kFnDir1 = 0x22b808, kFnDir2 = 0x22b858;
// A memória emulada das cenas.
constexpr uint32_t kCam = 0x01000000, kCamState = 0x01000400, kCamList = 0x01000500, kViewCam = 0x01000520,
                   kRenderer = 0x01000600, kMission = 0x01001000, kWM = 0x01003000, kWTable = 0x01003200,
                   kPlayer = 0x01004000, kPlayerVt = 0x01004400, kCarPhysics = 0x01004800, kCarsArr = 0x01004c00,
                   kBodies = 0x01005000, kInfos = 0x01008000, kObjs = 0x01010000, kChars = 0x01020000,
                   kAivs = 0x01030000, kTargets = 0x01040000, kScratch = 0x01050000, kScratchInst = 0x01051000,
                   kHeap = 0x01100000, kHeapEnd = 0x01800000;
constexpr uint32_t kFnGetPhysics = 0x01004f00, kFnCharAlive = 0x01004f10, kCharVt = 0x01004e00;
constexpr int kMaxTargets = 24;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 4141) {}
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

bool same_bytes(const std::string& what, const uint8_t* emu, const void* port_v, uint32_t n, std::string& out,
                std::initializer_list<std::pair<uint32_t, uint32_t>> skip = {}) {
    const uint8_t* port = static_cast<const uint8_t*>(port_v);
    for (uint32_t o = 0; o < n; o += 4) {
        bool skipped = false;
        for (const auto& [a, b] : skip)
            if (o >= a && o < b) skipped = true;
        if (skipped) continue;
        uint32_t x, y;
        std::memcpy(&x, emu + o, 4);
        std::memcpy(&y, port + o, 4);
        if (x != y) {
            out = what + " +0x" + hex(o).substr(5) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

// Um alvo de tipo 1 do lado do porte: um PhysicsObject com a vida dada (o corpo na vaga da Simulation).
class CkObject : public drv::BasicOwner {
public:
    float hp = 1.0f;
    float hit_points() override { return hp; }
};

// O lado do porte de um personagem (tipo 2) e de um veículo da IA (tipo 3): imagens com o leiaute do PS2 que o
// UpdatePosition lê (o +0x30 do personagem) e os ganchos do target_picker_env() leem (vivo, a posição, o tipo).
struct alignas(16) PortChar {
    uint8_t bytes[0x300] = {};
};

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    int32_t sound_state = -1;
    uint64_t sound_calls = 0;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(0x2a8038, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) heap = kHeap;  // as cenas curtas reaproveitam a área
            if (n) std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x2d0910, "__builtin_new", alloc);
        m.hook(0x2d09c8, "operator_new", alloc);
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x1ff088, "Simulation::GetRigidBody", [](MipsMachine& mm) {
            const int16_t slot = int16_t(mm.gpr(R::a1) & 0xffff);
            mm.set_gpr(R::v0, kBodies + uint32_t(int32_t(slot)) * 0x80u);
        });
        m.hook(0x13e0b8, "AIVehicle::GetPosition", [](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0)) + 0x200);
        });
        m.hook(0x2e6a88, "ATargeting::ATargeting", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        m.hook(0x2e6ac8, "ATargeting::SetState", [this](MipsMachine& mm) {
            sound_state = int32_t(mm.gpr(R::a1));
            ++sound_calls;
        });
        m.hook(kFnGetPhysics, "PBondCar::GetPhysics (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCarPhysics); });
        m.hook(kFnCharAlive, "AICharacter: vivo (falso)", [](MipsMachine& mm) {
            mm.set_gpr(R::v0, mm.read32(uint32_t(mm.gpr(R::a0)) + 0x2f0));
        });
        // O CheckHitWorld do porte (conferido na parte 18) sobre o mundo de Paris.
        m.hook(0x215cc8, "WCollisionMgr::CheckHitWorld (o do porte)", [](MipsMachine& mm) {
            alignas(16) float seg[8];
            std::memcpy(seg, mm.mem(uint32_t(mm.gpr(R::a1)), 32), 32);
            const uint32_t ia = uint32_t(mm.gpr(R::a2));
            drv::WorldCollisionInfo info;
            std::memcpy(&info, mm.mem(ia, 0x60), 0x60);
            const bool hit = drv::WCollisionMgr_CheckHitWorld(seg, info);
            if (info.face_instance) {
                const uint32_t flags = nf::p32_ptr<const drv::WCollisionInstance>(info.face_instance)->flags;
                mm.write32(kScratchInst + 0x18, flags);
                info.face_instance = kScratchInst;
            }
            info.object = 0;
            std::memcpy(mm.mem(ia, 0x60), &info, 0x60);
            mm.set_gpr(R::v0, hit ? 1u : 0u);
        });
    }
    void call(uint32_t fn, std::initializer_list<uint32_t> args) { m.call(fn, std::vector<uint32_t>(args)); }
    void put(uint32_t a, const void* p, uint32_t n) { std::memcpy(m.mem(a, n), p, n); }
    void putf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }
    float getf(uint32_t a) { return ps2::ffloat(m.read32(a)); }
};

// A cena: a câmera, o render, a missão, o gerente das armas e o carro do jogador, nos dois lados.
struct Scene {
    drv::PlayerCamera cam;
    drv::BondCar player;
    drv::CarPhysics physics{};
    std::vector<std::unique_ptr<CkObject>> objects;
    std::vector<std::unique_ptr<PortChar>> chars, aivs;
};

void random_camera(Rng& r, Emu& e, Scene& s, const float around[3]) {
    drv::PlayerCameraData& d = s.cam.d;
    std::memset(&d, 0, sizeof d);
    const float yaw = r.uni(-3.14159f, 3.14159f), pitch = r.uni(-0.6f, 0.6f);
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // As linhas: direita, cima, frente, a posição.
    const float m[16] = {cy, 0, -sy, 0, sy * sp, cp, cy * sp, 0, sy * cp, -sp, cy * cp, 0,
                         around[0] + r.uni(-20, 20), around[1] + r.uni(1, 12), around[2] + r.uni(-20, 20), 1};
    std::memcpy(d.matrix, m, 64);
    if (r.chance(0.1)) d.matrix[15] = r.uni(-2, 2);
    for (int i = 0; i < 16; ++i) d.inverse[i] = r.uni(-3, 3);
    d.matrix_changed = r.chance(0.8) ? 1 : 0;
    d.fov = r.chance(0.5) ? r.uni(15, 75) : 33.0f;
    for (int i = 0; i < 4; ++i) d.forward[i] = r.chance(0.5) ? m[8 + i] : r.uni(-1, 1);
    for (int i = 0; i < 16; ++i) d.matrix_copy[i] = r.uni(-1, 1);
    d.dz_pitch = r.uni(-0.3f, 0.3f);
    d.dz_yaw = r.uni(-0.3f, 0.3f);
    s.cam.state.zoom_held = r.chance(0.3) ? 1 : 0;
    e.put(kCam, &d, sizeof d);
    e.m.write32(kCam + 0x354, kCamState);
    std::memset(e.m.mem(kCamState, 0x5c), 0, 0x5c);
    e.m.write32(kCamState + 0x24, s.cam.state.zoom_held);
}

void random_render(Rng& r, Emu& e) {
    drv::TargetPickerEnv& env = drv::target_picker_env();
    env.screen_w = 640;
    env.screen_h = 448;
    env.widescreen = r.chance(0.2) ? 1 : 0;
    env.fov_mult = r.chance(0.8) ? 1.0f : r.uni(0.8f, 1.25f);
    env.aspect_mult = r.chance(0.8) ? 1.0f : r.uni(0.9f, 1.1f);
    e.m.write32(kRendererPtr, kRenderer);
    e.m.write32(kRenderer + 0x50, uint32_t(env.screen_w));
    e.m.write32(kRenderer + 0x54, uint32_t(env.screen_h));
    e.m.write32(kRenderer + 0x5c, uint32_t(env.widescreen));
    e.putf(kRenderer + 0x6c, env.fov_mult);
    e.m.write32(kScreenW, uint32_t(env.screen_w));
    e.m.write32(kScreenH, uint32_t(env.screen_h));
    e.putf(kAspectMult, env.aspect_mult);
}

// O corpo da vaga nos dois lados (a posição e a orientação; a vaga 0 é a do jogador).
void put_body(Emu& e, int slot, const float pos[3], const float fwd[3]) {
    drv::Simulation& sim = drv::simulation();
    drv::RigidBody* b = drv::GetRigidBody(sim, slot);
    drv::RigidBodyInfo* info = &sim.info_storage[slot];
    b->info = info;
    std::memcpy(b->pos, pos, 12);
    std::memset(info->orient, 0, sizeof info->orient);
    info->orient[0][0] = fwd[2];
    info->orient[0][2] = -fwd[0];
    info->orient[1][1] = 1.0f;
    std::memcpy(info->orient[2], fwd, 12);
    info->orient[3][3] = 1.0f;
    const uint32_t eb = kBodies + uint32_t(slot) * 0x80, ei = kInfos + uint32_t(slot) * 0x80;
    std::memset(e.m.mem(eb, 0x80), 0, 0x80);
    e.put(eb + 0x10, pos, 12);
    e.m.write32(eb + 0x5c, ei);
    e.put(ei, info->orient, 64);
}

void setup_globals(Emu& e, Scene& s, Rng& r) {
    drv::Simulation& sim = drv::simulation();
    sim.steps = r.range(0, 100000);
    sim.state = r.chance(0.05) ? 3 : 1;
    e.m.write32(kSimSteps, uint32_t(sim.steps));
    e.putf(kSimDt, sim.dt);
    e.m.write32(kSimRate, uint32_t(sim.frame_rate));
    e.m.write32(kSimState, uint32_t(sim.state));
    // O carro do jogador (vaga 0) e a física dele.
    sim.cars.clear();
    sim.cars.push_back(&s.player);
    s.player.d.slot = 0;
    s.player.d.physics = nf::p32_u(&s.physics);
    s.physics.is_sub = r.chance(0.1) ? 1 : 0;
    s.physics.sub_physics = r.chance(0.1) ? 1 : 0;
    e.m.write32(kCarsVec, kCarsArr);
    e.m.write32(kCarsArr, kPlayer);
    std::memset(e.m.mem(kPlayer, 0x80), 0, 0x80);
    e.m.write32(kPlayer + 0x44, 0);  // +0x46 a vaga 0
    e.m.write32(kPlayer + 0x68, kPlayerVt);
    e.m.write32(kPlayerVt + 0x1d0, 0);
    e.m.write32(kPlayerVt + 0x1d4, kFnGetPhysics);
    std::memset(e.m.mem(kCarPhysics, 0x100), 0, 0x100);
    e.m.write32(kCarPhysics + 0xbc, uint32_t(s.physics.is_sub));
    e.m.write32(kCarPhysics + 0xc0, uint32_t(s.physics.sub_physics));
    // A câmera.
    e.m.write32(kWorldCams, kCamList);
    e.m.write32(kCamList, kViewCam);
    e.m.write32(kCamList + 4, kCam);
    e.m.write32(kViewCam, kCam);
    drv::current_player_camera() = &s.cam;
    // A missão e o gerente das armas.
    drv::SMissionManagerData& mm = drv::mission_manager();
    mm.state = r.chance(0.08) ? r.range(3, 4) : 1;
    mm.cinematic_mode = r.chance(0.05) ? 1u : 0u;
    e.m.write32(kMissionPtr, kMission);
    e.m.write32(kMission + 0x34c, uint32_t(mm.state));
    e.m.write32(kMission + 0x3cc, mm.cinematic_mode);
    drv::SWeaponManagerData* wm = drv::weapon_manager();
    wm->secondary = r.chance(0.5) ? 0x1c : r.range(0, 31);
    e.m.write32(kWeaponMgrPtr, kWM);
    e.m.write32(kWM + 4, uint32_t(wm->secondary));
    e.m.write32(kWM + 0x10, kWTable);
    for (int i = 0; i < 32; ++i) e.m.write32(kWTable + uint32_t(i) * 0x54, uint32_t(drv::weapon_entry(*wm, i)->type));
}

// Os pares de alvos (o emulado e o do porte) e o mapa de um para o outro.
struct Targets {
    std::vector<std::pair<uint32_t, uint32_t>> list;  // (emulado, p32 do porte)
    std::map<uint32_t, uint32_t> to_port;
    std::map<uint32_t, uint32_t> obj_to_port;  // o objeto (+0x48) emulado -> o do porte
};

bool compare_target(Emu& e, const Targets& t, uint32_t emu, uint32_t port, std::string& why) {
    const uint8_t* a = e.m.mem(emu, 0x50);
    const uint8_t* b = nf::p32_ptr<const uint8_t>(port);
    if (!same_bytes("alvo", a, b, 0x48, why)) return false;
    uint32_t oa, ob;
    std::memcpy(&oa, a + 0x48, 4);
    std::memcpy(&ob, b + 0x48, 4);
    auto it = t.obj_to_port.find(oa);
    if ((it == t.obj_to_port.end() ? oa : it->second) != ob) {
        why = "alvo +0x48: original " + hex(oa) + ", porte " + hex(ob);
        return false;
    }
    return true;
}

bool compare_picker(Emu& e, const Targets& t, std::string& why) {
    const uint8_t* a = e.m.mem(kPicker, 0xb0);
    const drv::WTargetPickerData& p = drv::target_picker();
    if (!same_bytes("picker", a, &p, 0xb0, why, {{0x28, 0x70}, {0x7c, 0x80}})) return false;
    auto map_ptr = [&](uint32_t x) {
        auto it = t.to_port.find(x);
        return it == t.to_port.end() ? x : it->second;
    };
    uint32_t sa;
    std::memcpy(&sa, a + 0x28, 4);
    if (map_ptr(sa) != p.selected) {
        why = "picker +0x28 (o escolhido): original " + hex(sa) + ", porte " + hex(p.selected);
        return false;
    }
    const int n = std::min<int>(p.count, 16);
    for (int i = 0; i < n; ++i) {
        uint32_t x;
        std::memcpy(&x, a + 0x30 + 4 * i, 4);
        if (map_ptr(x) != p.sorted[i]) {
            why = "picker os ordenados [" + std::to_string(i) + "]";
            return false;
        }
    }
    return true;
}

}  // namespace

int run_integ_ai_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                        int cases, uint32_t seed) {
    std::printf("-- a IA: a mira pela câmera, o WTargetPicker e os WTargetable\n");
    int failures = 0;
    auto emu = std::make_unique<Emu>();
    Emu& e = *emu;
    e.setup(elf, vu0_program, vu0_size);
    Rng r(seed ^ 0x2400);
    drv::simulation_reset(60);
    drv::SMissionManager_Construct(false);
    if (!drv::weapon_manager()) drv::weapons_create();
    auto scene = std::make_unique<Scene>();
    Scene& s = *scene;
    // O mundo de Paris (o CheckHitWorld do porte, nos dois lados).
    drv::Track track;
    drv::TrackWorld tw;
    std::string err;
    if (!track.load(disc, "paris_mis01", "", err) || !tw.init(track.root(), &err)) {
        std::printf("  paris_mis01 não carregou (%s)\n", err.c_str());
        return 1;
    }
    drv::set_world(&tw);
    const float street[3] = {-327.7f, 0.0f, -17.7f};
    float ground = 0.0f;
    float base[3] = {street[0], 20.0f, street[2]};
    if (tw.world_height_at_point(base, ground)) base[1] = ground;

    // ---------------------------------------------------------------- a) a mira pela câmera
    {
        Tally t_fwd, t_aim, t_cam_aiming, t_inv, t_xf;
        for (int c = 0; c < cases; ++c) {
            setup_globals(e, s, r);
            random_camera(r, e, s, base);
            const float fwd[3] = {r.uni(-1, 1), 0, r.uni(-1, 1)};
            put_body(e, 0, base, fwd);
            std::string why;
            // GetForwardAimVec4(t)
            {
                const float tv = r.chance(0.3) ? 1.0f : (r.chance(0.3) ? 0.0f : r.uni(-0.5f, 1.5f));
                e.m.set_fpr(12, tv);
                e.call(kFnFwdAim, {kCam});
                const uint32_t ra = uint32_t(e.m.gpr(R::v0));
                const float* pp = drv::RPlayerCamera_GetForwardAimVec4(s.cam, tv);
                ++t_fwd.cases;
                if (!same_bytes("GetForwardAimVec4", e.m.mem(ra, 16), pp, 16, why)) t_fwd.fail(why);
            }
            // GetAimMatrix4(a, b)
            {
                const float av = r.chance(0.4) ? float(r.range(0, 1)) : r.uni(-1, 2);
                const float bv = r.chance(0.4) ? 1.0f : r.uni(-1, 2);
                e.m.set_fpr(12, av);
                e.m.set_fpr(13, bv);
                e.call(kFnAimMatrix, {kCam});
                const uint32_t ra = uint32_t(e.m.gpr(R::v0));
                const float* pp = drv::RPlayerCamera_GetAimMatrix4(s.cam, av, bv);
                ++t_aim.cases;
                if (!same_bytes("GetAimMatrix4", e.m.mem(ra, 64), pp, 64, why)) t_aim.fail(why);
            }
            {
                e.call(kFnCameraAiming, {kCam});
                ++t_cam_aiming.cases;
                if (uint32_t(e.m.gpr(R::v0)) != drv::RPlayerCamera_CameraAiming(s.cam)) t_cam_aiming.fail("o valor");
            }
            // RCamera::CreateMatrix4Inv numa cópia (os 0xb0 bytes do RCamera).
            {
                e.call(kFnMatInv, {kCam});
                drv::RCamera_CreateMatrix4Inv(s.cam.d);
                ++t_inv.cases;
                if (!same_bytes("CreateMatrix4Inv", e.m.mem(kCam, 0xa8), &s.cam.d, 0xa8, why)) t_inv.fail(why);
            }
            // TransformPoint (0x002d18e8)
            {
                alignas(16) float mm[16], in[4], out_p[4];
                for (float& f : mm) f = r.uni(-3, 3);
                for (float& f : in) f = r.uni(-100, 100);
                for (float& f : out_p) f = r.uni(-5, 5);
                e.put(kScratch, mm, 64);
                e.put(kScratch + 0x40, in, 16);
                e.put(kScratch + 0x50, out_p, 16);
                e.call(kFnXform3, {kScratch, kScratch + 0x40, kScratch + 0x50});
                drv::TransformPoint3(mm, in, out_p);
                ++t_xf.cases;
                if (!same_bytes("TransformPoint", e.m.mem(kScratch + 0x50, 16), out_p, 16, why)) t_xf.fail(why);
            }
        }
        report("RPlayerCamera::GetForwardAimVec4", t_fwd, failures);
        report("RPlayerCamera::GetAimMatrix4", t_aim, failures);
        report("RPlayerCamera::CameraAiming", t_cam_aiming, failures);
        report("RCamera::CreateMatrix4Inv", t_inv, failures);
        report("TransformPoint (0x002d18e8)", t_xf, failures);
    }

    // ---------------------------------------------------------------- b) as peças do WTargetPicker
    {
        Tally t_zone, t_range, t_onscreen, t_screen, t_off, t_dist, t_cmp, t_step, t_vis;
        for (int c = 0; c < cases; ++c) {
            setup_globals(e, s, r);
            random_render(r, e);
            random_camera(r, e, s, base);
            std::string why;
            // WSimpleZone
            {
                float p[3];
                for (float& f : p) f = r.chance(0.2) ? float(r.range(-5, 5)) * 2.5f : r.uni(-3000, 3000);
                e.put(kScratch + 0x100, p, 12);
                e.call(kFnZoneSet, {kScratch, kScratch + 0x100});
                int32_t z[3];
                drv::WSimpleZone_SetPosition(z, p);
                ++t_zone.cases;
                if (!same_bytes("WSimpleZone::SetPosition", e.m.mem(kScratch, 12), z, 12, why)) t_zone.fail(why);
                int32_t z2[3];
                for (int32_t& v : z2) v = z[0] + r.range(-80, 80);
                e.put(kScratch + 0x20, z2, 12);
                const uint32_t bits = uint32_t(r.range(0, 8));
                e.call(kFnZoneRange, {kScratch, kScratch + 0x20, bits});
                ++t_range.cases;
                if ((e.m.gpr(R::v0) != 0) != drv::WSimpleZone_IsZoneInRange(z, z2, bits)) t_range.fail("o resultado");
            }
            // IsPointOnScreen, a tela e o fora da tela.
            {
                const float pt[2] = {r.uni(-200, 900), r.uni(-200, 700)};
                e.put(kScratch, pt, 8);
                e.call(kFnOnScreen, {kPicker, kScratch});
                ++t_onscreen.cases;
                if ((e.m.gpr(R::v0) != 0) != drv::WTargetPicker_IsPointOnScreen(pt)) t_onscreen.fail("o resultado");
                alignas(16) float p[4] = {s.cam.d.matrix[12] + r.uni(-150, 150), s.cam.d.matrix[13] + r.uni(-30, 30),
                                          s.cam.d.matrix[14] + r.uni(-150, 150), r.uni(-2, 2)};
                if (r.chance(0.05)) std::memcpy(p, &s.cam.d.matrix[12], 12);
                e.put(kScratch + 0x40, p, 16);
                e.put(kCam, &s.cam.d, 0xa8);  // o 0x0022a128 copia o RCamera (a inversa da cópia não volta)
                e.call(kFnScreenPos, {kPicker, kScratch + 0x40});
                const uint64_t v = e.m.gpr(R::v0);
                float sp[2];
                drv::WTargetPicker_GetScreenPos(p, sp);
                ++t_screen.cases;
                if (uint32_t(v) != ps2::fbits(sp[0]) || uint32_t(v >> 32) != ps2::fbits(sp[1]))
                    t_screen.fail("x " + hex(uint32_t(v)) + "/" + hex(ps2::fbits(sp[0])) + " y " +
                                  hex(uint32_t(v >> 32)) + "/" + hex(ps2::fbits(sp[1])));
                e.call(kFnOffScreen, {kPicker, kScratch + 0x40});
                const uint64_t v2 = e.m.gpr(R::v0);
                drv::WTargetPicker_GetOffScreenPos(p, sp);
                ++t_off.cases;
                if (uint32_t(v2) != ps2::fbits(sp[0]) || uint32_t(v2 >> 32) != ps2::fbits(sp[1]))
                    t_off.fail("x " + hex(uint32_t(v2)) + "/" + hex(ps2::fbits(sp[0])) + " y " +
                               hex(uint32_t(v2 >> 32)) + "/" + hex(ps2::fbits(sp[1])));
            }
            // As distâncias e os comparadores, sobre dois alvos de ponto.
            {
                drv::WTargetableData a{}, b{};
                for (drv::WTargetableData* w : {&a, &b}) {
                    for (int i = 0; i < 3; ++i) w->pos[i] = s.cam.d.matrix[12 + i] + r.uni(-100, 100);
                    float* f = reinterpret_cast<float*>(w);
                    f[4] = r.uni(-100, 800);
                    f[5] = r.uni(-100, 600);
                    reinterpret_cast<uint32_t*>(w)[0x11] = r.chance(0.8) ? 1 : 0;
                }
                e.put(kScratch, &a, 0x50);
                e.put(kScratch + 0x60, &b, 0x50);
                e.m.write32(kScratch + 0xc0, kScratch);
                e.m.write32(kScratch + 0xc4, kScratch + 0x60);
                const float pt[2] = {r.uni(0, 640), r.uni(0, 448)};
                e.put(kScratch + 0xd0, pt, 8);
                const uint32_t pa = nf::p32_u(&a), pb = nf::p32_u(&b);
                std::string w2;
                e.call(kFnDistCam, {kScratch, 0});
                if (e.m.fpr_bits(0) != ps2::fbits(drv::WTargetable_DistFromCamera(pa, 0))) w2 = "DistFromCamera";
                e.call(kFnDistPos, {kScratch, kScratch + 0xd0});
                if (e.m.fpr_bits(0) != ps2::fbits(drv::WTargetable_DistFromScreenPos(pa, pt))) w2 = "DistFromScreenPos";
                e.call(kFnDistCenter, {kScratch});
                if (e.m.fpr_bits(0) != ps2::fbits(drv::WTargetable_DistFromScreenCenter(pa))) w2 = "DistFromScreenCenter";
                ++t_dist.cases;
                if (!w2.empty()) t_dist.fail(w2);
                // Os comparadores (o CompareDistFromScreenPos lê o alvo do mundo do picker).
                float wt[4] = {s.cam.d.matrix[12] + r.uni(-100, 100), s.cam.d.matrix[13], s.cam.d.matrix[14] + r.uni(-100, 100), 0};
                std::memcpy(drv::target_picker().world_target, wt, 12);
                e.put(kPicker + 0x10, wt, 12);
                const uint32_t ports[2] = {pa, pb};
                uint32_t ab[2] = {kScratch + 0xc0, kScratch + 0xc4};
                const int i0 = r.range(0, 1), i1 = r.range(0, 1);
                w2.clear();
                e.call(kFnCmpCenter, {ab[i0], ab[i1]});
                if (int32_t(e.m.gpr(R::v0)) != drv::WTargetPicker_CompareDistFromScreenCenter(&ports[i0], &ports[i1]))
                    w2 = "CompareDistFromScreenCenter";
                e.put(kCam, &s.cam.d, 0xa8);
                e.call(kFnCmpPos, {ab[i0], ab[i1]});
                if (int32_t(e.m.gpr(R::v0)) != drv::WTargetPicker_CompareDistFromScreenPos(&ports[i0], &ports[i1]))
                    w2 = "CompareDistFromScreenPos";
                e.call(kFnCmpCam, {ab[i0], ab[i1]});
                if (int32_t(e.m.gpr(R::v0)) != drv::WTargetPicker_CompareDistFromCamera(&ports[i0], &ports[i1]))
                    w2 = "CompareDistFromCamera";
                ++t_cmp.cases;
                if (!w2.empty()) t_cmp.fail(w2);
            }
            // StepCheckHitWorld e UpdateVisibility sobre Paris.
            {
                alignas(16) float seg[2][4];
                for (int k = 0; k < 2; ++k) {
                    seg[k][0] = base[0] + r.uni(-250, 250);
                    seg[k][1] = base[1] + r.uni(-3, 25);
                    seg[k][2] = base[2] + r.uni(-250, 250);
                    seg[k][3] = r.chance(0.7) ? 1.0f : r.uni(-2, 2);
                }
                e.put(kScratch, seg, 32);
                e.m.set_fpr(12, 16.0f);
                e.call(kFnStep, {0, kScratch});
                const bool hit_e = e.m.gpr(R::v0) != 0;
                const bool hit_p = drv::WCollisionMgr_StepCheckHitWorld(seg);
                ++t_step.cases;
                if (hit_e != hit_p) t_step.fail("o resultado");
                else if (!same_bytes("o segmento", e.m.mem(kScratch, 32), seg, 32, why)) t_step.fail(why);
                drv::WTargetableData w{};
                w.pos[0] = s.cam.d.matrix[12] + r.uni(-260, 260);
                w.pos[1] = s.cam.d.matrix[13] + r.uni(-10, 10);
                w.pos[2] = s.cam.d.matrix[14] + r.uni(-260, 260);
                reinterpret_cast<uint32_t*>(&w)[3] = r.next() & 1;
                e.put(kScratch + 0x100, &w, 0x50);
                e.call(kFnUpdVis, {kScratch + 0x100});
                drv::WTargetable_UpdateVisibility(nf::p32_u(&w));
                ++t_vis.cases;
                if (!same_bytes("UpdateVisibility", e.m.mem(kScratch + 0x100, 0x50), &w, 0x50, why)) t_vis.fail(why);
            }
        }
        report("WSimpleZone::SetPosition", t_zone, failures);
        report("WSimpleZone::IsZoneInRange", t_range, failures);
        report("WTargetPicker::IsPointOnScreen", t_onscreen, failures);
        report("WTargetPicker: o ponto na tela (0x0022a128)", t_screen, failures);
        report("WTargetPicker: fora da tela (0x0022a430)", t_off, failures);
        report("WTargetable::DistFromCamera/ScreenPos/ScreenCenter", t_dist, failures);
        report("WTargetPicker: os comparadores do qsort", t_cmp, failures);
        report("WCollisionMgr::StepCheckHitWorld (Paris)", t_step, failures);
        report("WTargetable::UpdateVisibility (Paris)", t_vis, failures);
    }

    // ---------------------------------------------------------------- c) cenas de alvos e quadros
    {
        Tally t_ctor, t_frames, t_calls, t_sound;
        const int scenes = std::max(10, cases / 40);
        for (int sc = 0; sc < scenes; ++sc) {
            setup_globals(e, s, r);
            random_render(r, e);
            const float pfwd[3] = {r.uni(-1, 1), 0, r.uni(-1, 1)};
            float ppos[3] = {base[0] + r.uni(-30, 30), base[1], base[2] + r.uni(-30, 30)};
            put_body(e, 0, ppos, pfwd);
            random_camera(r, e, s, ppos);
            // Os dois pickers do zero.
            std::memset(e.m.mem(kPicker, 0xb0), 0, 0xb0);
            e.call(kFnPickerCtor, {kPicker});
            // O picker do PS2 é um global do .bss (zerado antes do construtor): o do porte também.
            static_cast<drv::WTargetPickerData&>(drv::target_picker()) = drv::WTargetPickerData{};
            std::memset(&static_cast<drv::WTargetPickerData&>(drv::target_picker()), 0, sizeof(drv::WTargetPickerData));
            drv::WTargetPicker_Construct();
            drv::TargetGlobals& tg = drv::target_globals();
            e.m.write32(kTargetsInWorld, tg.targets_in_world);
            e.m.write32(kTargetsLive, uint32_t(tg.live));
            e.sound_state = -1;
            drv::target_picker_sound() = -1;
            // Os alvos.
            Targets t;
            s.objects.clear();
            s.chars.clear();
            s.aivs.clear();
            drv::Simulation& sim = drv::simulation();
            for (int i = 1; i < 64; ++i) sim.owners[i] = nullptr;
            drv::TargetPickerEnv& env = drv::target_picker_env();
            env.character_alive = [](uint32_t c) { return nf::p32_ptr<const uint32_t>(c)[0x2f0 / 4] != 0; };
            env.ai_vehicle_position = [](uint32_t a, float out[3], bool& heli) {
                const uint8_t* b = nf::p32_ptr<const uint8_t>(a);
                std::memcpy(out, b + 0x200, 12);
                uint32_t w0;
                std::memcpy(&w0, b, 4);
                heli = w0 == 2;
                return true;
            };
            const int n = r.range(1, kMaxTargets);
            std::string why;
            for (int i = 0; i < n; ++i) {
                const int kind = r.range(0, 3);
                const float pos[3] = {ppos[0] + r.uni(-220, 220), ppos[1] + r.uni(-5, 15), ppos[2] + r.uni(-220, 220)};
                const uint32_t te = kTargets + uint32_t(i) * 0x60;
                std::memset(e.m.mem(te, 0x60), 0, 0x60);
                uint32_t tp = 0;
                if (kind == 0) {
                    e.m.set_fpr(12, pos[0]);
                    e.m.set_fpr(13, pos[1]);
                    e.m.set_fpr(14, pos[2]);
                    e.call(kFnCtorPoint, {te});
                    tp = drv::WTargetable_NewPoint(pos[0], pos[1], pos[2]);
                } else if (kind == 1) {
                    const int slot = int(s.objects.size()) + 1;
                    s.objects.push_back(std::make_unique<CkObject>());
                    CkObject& o = *s.objects.back();
                    o.hp = r.chance(0.8) ? r.uni(1, 100) : (r.chance(0.5) ? 0.0f : -5.0f);
                    const float fw[3] = {0, 0, 1};
                    put_body(e, slot, pos, fw);
                    sim.owners[slot] = &o;
                    const uint32_t oe = kObjs + uint32_t(slot) * 0x100;
                    std::memset(e.m.mem(oe, 0x100), 0, 0x100);
                    e.m.write32(oe + 0x44, uint32_t(slot) << 16);
                    e.m.write32(oe + 0x54, oe + 0x80);
                    e.putf(oe + 0x80, o.hp);
                    t.obj_to_port[oe] = nf::p32_u(static_cast<drv::PhysicsObject*>(&o));
                    e.call(kFnCtorObj, {te, oe});
                    tp = drv::WTargetable_NewObject(&o);
                } else if (kind == 2) {
                    s.chars.push_back(std::make_unique<PortChar>());
                    PortChar& pc = *s.chars.back();
                    const uint32_t ce = kChars + uint32_t(s.chars.size()) * 0x300;
                    std::memset(e.m.mem(ce, 0x300), 0, 0x300);
                    std::memcpy(pc.bytes + 0x30, pos, 12);
                    const uint32_t alive = r.chance(0.8) ? 1 : 0;
                    std::memcpy(pc.bytes + 0x2f0, &alive, 4);
                    e.put(ce, pc.bytes, 0x300);
                    e.m.write32(ce + 0x140, kCharVt);
                    e.m.write32(kCharVt + 0x10, 0);
                    e.m.write32(kCharVt + 0x14, kFnCharAlive);
                    t.obj_to_port[ce] = nf::p32_u(&pc);
                    e.call(kFnCtorChar, {te, ce});
                    tp = drv::WTargetable_NewCharacter(nf::p32_u(&pc));
                } else {
                    s.aivs.push_back(std::make_unique<PortChar>());
                    PortChar& pa = *s.aivs.back();
                    const uint32_t ae = kAivs + uint32_t(s.aivs.size()) * 0x300;
                    std::memset(e.m.mem(ae, 0x300), 0, 0x300);
                    const uint32_t w0 = r.chance(0.5) ? 2 : 1;
                    std::memcpy(pa.bytes, &w0, 4);
                    std::memcpy(pa.bytes + 0x200, pos, 12);
                    e.put(ae, pa.bytes, 0x300);
                    t.obj_to_port[ae] = nf::p32_u(&pa);
                    e.call(kFnCtorAiv, {te, ae});
                    tp = drv::WTargetable_NewAIVehicle(nf::p32_u(&pa));
                }
                t.list.push_back({te, tp});
                t.to_port[te] = tp;
                ++t_ctor.cases;
                if (!compare_target(e, t, te, tp, why)) t_ctor.fail("o construtor (tipo " + std::to_string(kind) + "): " + why);
            }
            // Os registros e os quadros.
            const int frames = r.range(5, 40);
            for (int f = 0; f < frames; ++f) {
                // As mudanças sorteadas do quadro.
                sim.steps += r.range(0, 3);
                e.m.write32(kSimSteps, uint32_t(sim.steps));
                if (r.chance(0.3)) random_camera(r, e, s, ppos);
                if (r.chance(0.1)) {
                    sim.state = r.chance(0.1) ? 3 : 1;
                    e.m.write32(kSimState, uint32_t(sim.state));
                }
                if (r.chance(0.05)) {
                    drv::mission_manager().cinematic_mode = r.chance(0.3) ? 1u : 0u;
                    e.m.write32(kMission + 0x3cc, drv::mission_manager().cinematic_mode);
                }
                for (size_t k = 0; k < s.objects.size(); ++k) {
                    if (!r.chance(0.3)) continue;
                    const int slot = int(k) + 1;
                    drv::RigidBody* b = drv::GetRigidBody(sim, slot);
                    for (int i = 0; i < 3; ++i) b->pos[i] = ps2::ee::add(b->pos[i], r.uni(-3, 3));
                    e.put(kBodies + uint32_t(slot) * 0x80 + 0x10, b->pos, 12);
                    if (r.chance(0.1)) {
                        s.objects[k]->hp = r.chance(0.5) ? 0.0f : 50.0f;
                        e.putf(kObjs + uint32_t(slot) * 0x100 + 0x80, s.objects[k]->hp);
                    }
                }
                const int op = r.range(0, 9);
                std::string what;
                if (op <= 2) {
                    const auto& [te, tp] = t.list[size_t(r.range(0, int(t.list.size()) - 1))];
                    e.call(kFnRegister, {kPicker, te});
                    drv::WTargetPicker_RegisterTarget(tp);
                    what = "RegisterTarget";
                } else if (op == 3) {
                    const auto& [te, tp] = t.list[size_t(r.range(0, int(t.list.size()) - 1))];
                    e.call(kFnUnregister, {kPicker, te});
                    drv::WTargetPicker_UnregisterTarget(tp);
                    what = "UnregisterTarget";
                } else if (op == 4) {
                    const int mode = r.chance(0.7) ? r.range(0, 1) : 0;
                    e.call(kFnActivate, {kPicker, uint32_t(mode)});
                    drv::WTargetPicker_ActivateTargeting(mode);
                    if (r.chance(0.3)) {
                        const int32_t so = r.range(0, 2);
                        drv::target_picker().sort = so;
                        e.m.write32(kPicker + 0xa8, uint32_t(so));
                    }
                    what = "ActivateTargeting";
                } else if (op == 5 && r.chance(0.3)) {
                    e.call(kFnDeactivate, {kPicker});
                    drv::WTargetPicker_DeactivateTargeting();
                    what = "DeactivateTargeting";
                } else if (op == 6) {
                    e.call(kFnCycle, {kPicker});
                    drv::WTargetPicker_CycleToNextTarget();
                    what = "CycleToNextTarget";
                } else {
                    e.put(kCam, &s.cam.d, 0xa8);
                    e.call(kFnUpdateTargets, {kPicker});
                    drv::WTargetPicker_UpdateTargets();
                    what = "UpdateTargets";
                    ++t_frames.cases;
                }
                ++t_calls.cases;
                // GetSelectedTarget, e as duas direções da mira.
                e.call(kFnSelected, {kPicker});
                const uint32_t sel_e = uint32_t(e.m.gpr(R::v0));
                const uint32_t sel_p = drv::WTargetPicker_GetSelectedTarget();
                auto itp = t.to_port.find(sel_e);
                const uint32_t sel_m = itp == t.to_port.end() ? sel_e : itp->second;
                bool ok = sel_m == sel_p;
                if (!ok) why = "GetSelectedTarget";
                if (ok) ok = compare_picker(e, t, why);
                for (const auto& [te, tp] : t.list) {
                    if (!ok) break;
                    ok = compare_target(e, t, te, tp, why);
                }
                if (ok && (e.m.read32(kTargetsLive) != uint32_t(drv::target_globals().live) ||
                           e.m.read32(kTargetsInWorld) != drv::target_globals().targets_in_world)) {
                    ok = false;
                    why = "os globais do WTargetable";
                }
                if (!ok) {
                    t_calls.fail(what + ": " + why);
                    break;
                }
                ++t_sound.cases;
                if (e.sound_state != drv::target_picker_sound()) t_sound.fail(what + ": o estado do som");
            }
            // As direções da mira pela câmera (o GetDirectionToTarget).
            {
                e.call(kFnDir1, {kPicker, kScratch});
                float d1[3];
                drv::WTargetPicker_GetDirectionToTarget(d1);
                e.call(kFnDir2, {kPicker, kScratch + 0x40});
                float d2[16];
                drv::WTargetPicker_GetDirectionToTargetMatrix(d2);
                ++t_calls.cases;
                if (!same_bytes("GetDirectionToTarget", e.m.mem(kScratch, 12), d1, 12, why) ||
                    !same_bytes("GetDirectionToTarget (matriz)", e.m.mem(kScratch + 0x40, 64), d2, 64, why))
                    t_calls.fail(why);
            }
            // Os alvos saem (o PS2 os deixa vivos: só os do porte que ninguém mais referencia são apagados).
            for (const auto& [te, tp] : t.list) drv::WTargetPicker_UnregisterTarget(tp);
            drv::WTargetPicker_Construct();
            tg.live = int32_t(e.m.read32(kTargetsLive));
            for (int i = 1; i < 64; ++i) sim.owners[i] = nullptr;
        }
        report("WTargetable: os construtores (os quatro tipos)", t_ctor, failures);
        report("WTargetPicker: as chamadas sorteadas (o picker, os alvos, os globais)", t_calls, failures);
        report("WTargetPicker::UpdateTargets (dos quadros acima)", t_frames, failures);
        report("ATargeting::SetState (o estado pedido)", t_sound, failures);
    }

    drv::set_world(nullptr);
    drv::current_player_camera() = nullptr;
    drv::simulation().cars.clear();
    drv::mission_reset();
    failures += run_integ_ai_vehicle_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    return failures;
}

}  // namespace drvcheck
