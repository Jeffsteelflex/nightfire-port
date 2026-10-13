// nf_driving_check, parte 6 (tests/driving_check_car.h): o carro. O porte monta um Vanquish (o manejo do vanquish.atr
// do disco, drv::AttributeCollection) num corpo rígido da Simulation sobre o chão de verdade de Paris
// (drv::TrackWorld); a cada caso o estado do carro, do corpo e do RigidBodyInfo (sorteado, ou o do caso anterior com os
// controles sorteados de novo) vai para a RAM emulada, e o código ORIGINAL do DRIVING.ELF roda no interpretador MIPS
// (com a VU0 em modo macro) ao lado do porte:
//  - PBondCar::ProcessPhysics (0x18cef8), que chama AddWheelForces, TempGetHeightInformation (o chão: a cópia do mundo
//    da parte 5), ImproveLanding e TwoWheelStunt quando cabem; compara o PBondCar inteiro, o RigidBody, o
//    RigidBodyInfo e a área de rascunho do carro;
//  - PBondCar::CalculateRPM (0x18aa88): o retorno e a marcha;
//  - PBondCar::GetControllerInput (0x18add8), com ações sorteadas na fila: o PBondCar e o corpo.
// Na RAM emulada o carro tem uma tabela virtual falsa cujas entradas usadas são as funções originais (GetPhysics
// 0x193170, IsWheelOnGround 0x1931f8, DisableTwoWheelStunt 0x18bb00) ou ganchos (GetAudio, o TriggerFX do render).
//
// O que não é comparado, e por quê:
//  - os ponteiros (do carro: o render, os atributos, a tabela virtual, o CarPhysics, a fila, o tipo, o som; do corpo e
//    da área de rascunho: o RigidBodyInfo; os WWorldPos das rodas: a instância e o artigo, comparados pelo que
//    apontam);
//  - o w de v0 de cada WWorldPos (+0x0c: lixo da pilha no PS2, ver a parte 5);
//  - os WWorldPos das alavancas no RigidBodyInfo (+0x380, +0x4d8): ponteiros, não usados pelo carro.
// Fora do código original (ganchos nativos): memset, memmove, FastAlloc/FastFree, sinf/cosf, AttributeSet::LookupInt
// e LookupFloat (BOOST_TIME e BOOST_EXTRA_ACC), o evento ECollision, AMenuSound::Trigger, GHud::TheApp e a fila de
// ações (ActionQueue::IsEmpty/GetAction/PopAction).

#include "driving_check_car.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/track.h"
#include "driving_check_world.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kSim = 0x339190;
constexpr uint32_t kScratchPadPtr = 0x3d0d10;  // BondCarScratchPad
constexpr uint32_t kTuningBegin = 0x331960, kTuningEnd = 0x331a58;
constexpr uint32_t kRigidTuningPtr = 0x339178;  // a área de rascunho do corpo rígido

constexpr uint32_t kFnProcessPhysics = 0x18cef8;
constexpr uint32_t kFnCalculateRPM = 0x18aa88;
constexpr uint32_t kFnGetControllerInput = 0x18add8;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000;
constexpr uint32_t kInfos = 0x01002000;
constexpr uint32_t kCar = 0x01012000;
constexpr uint32_t kScratch = 0x01013000;
constexpr uint32_t kVtable = 0x01014000;
constexpr uint32_t kRender = 0x01015000;
constexpr uint32_t kRenderVt = 0x01015100;
constexpr uint32_t kAudio = 0x01016000;
constexpr uint32_t kHud = 0x01017000;
constexpr uint32_t kActions = 0x01018000;
constexpr uint32_t kAttrSet = 0x01018800;
constexpr uint32_t kRigidScratch = 0x01018c00;
constexpr uint32_t kMgr = 0x01019000;
constexpr uint32_t kGrid = 0x01019100;
constexpr uint32_t kFnGetAudio = 0x01019800, kFnTriggerFx = 0x01019810;
constexpr uint32_t kHeapBegin = 0x01020000, kHeapEnd = 0x01100000;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

uint32_t body_addr(int slot) { return kBodies + 0x80u * uint32_t(slot); }
uint32_t info_addr(int slot) { return kInfos + 0x500u * uint32_t(slot); }

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 7) {}
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

// Compara n bytes palavra a palavra, pulando os intervalos [a, b) de skip; o primeiro diferente em `out`.
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

class CarEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    int32_t boost_time = 0;
    float boost_extra_acc = 0;
    std::vector<drv::CarAction> actions;
    size_t action_next = 0;
    uint64_t events = 0, fx = 0, audio = 0;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(0x2a8038, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2a7f34, "memmove", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("FastAlloc: a área acabou", heap, n);
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x175730, "Event::operator_new", [this, alloc](MipsMachine& mm) {
            ++events;
            alloc(mm);
        });
        m.hook(0x149d30, "ECollision::ECollision", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(0x2f4b10, "AMenuSound::Trigger", [](MipsMachine&) {});
        m.hook(0x23c618, "GHud::TheApp", [](MipsMachine& mm) { mm.set_gpr(R::v0, kHud); });
        m.hook(0x169bf0, "AttributeSet::LookupInt", [this](MipsMachine& mm) {
            const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            mm.set_gpr(R::v0, std::strcmp(key, "BOOST_TIME") == 0 ? uint64_t(int64_t(boost_time)) : 0);
        });
        m.hook(0x169d80, "AttributeSet::LookupFloat", [this](MipsMachine& mm) {
            const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            mm.set_fpr(0, std::strcmp(key, "BOOST_EXTRA_ACC") == 0 ? boost_extra_acc : 0.0f);
        });
        m.hook(0x1654c8, "ActionQueue::IsEmpty", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, action_next >= actions.size() ? 1 : 0);
        });
        m.hook(0x1655c0, "ActionQueue::GetAction", [this](MipsMachine& mm) {
            const uint32_t a = kActions + 16u * uint32_t(action_next);
            const drv::CarAction& act = actions[action_next];
            mm.write32(a, uint32_t(act.type));
            mm.write32(a + 4, 0);
            std::memcpy(mm.mem(a + 8, 4), &act.value, 4);
            mm.set_gpr(R::v0, a);
        });
        m.hook(0x165568, "ActionQueue::PopAction", [this](MipsMachine&) { ++action_next; });
        m.hook(kFnGetAudio, "PBondCar::GetAudio (falso)", [this](MipsMachine& mm) {
            ++audio;
            mm.set_gpr(R::v0, kAudio);
        });
        m.hook(kFnTriggerFx, "RSceneObj::TriggerFX (falso)", [this](MipsMachine&) { ++fx; });
        // A tabela virtual falsa do carro: delta 0 e a função (8 bytes por entrada; o PS2 lê o lh +0 e o lw +4).
        auto entry = [this](uint32_t off, uint32_t fn) {
            m.write32(kVtable + off, 0);
            m.write32(kVtable + off + 4, fn);
        };
        entry(0x40, kFnGetAudio);
        entry(0x1c8, 0x18bb00);  // DisableTwoWheelStunt
        entry(0x1d0, 0x193170);  // GetPhysics
        entry(0x210, 0x1931f8);  // IsWheelOnGround
        m.write32(kRender + 0x3c, kRenderVt);
        m.write32(kRenderVt + 0x80, 0);
        m.write32(kRenderVt + 0x84, kFnTriggerFx);
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        heap = kHeapBegin;
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 50'000'000);
    }
};

// O estado do port na RAM emulada (o carro, o corpo e o info da vaga, a área de rascunho, a Simulation).
void put_state(CarEmu& e, drv::BondCar& car, drv::TrackWorld& tw, const WorldCopy& wc, uint32_t physics_emu) {
    drv::Simulation& sim = drv::simulation();
    const int slot = car.d.slot;
    drv::RigidBody* rb = car.body();
    drv::RigidBody b = *rb;
    std::memcpy(e.m.mem(body_addr(slot), 0x80), &b, 0x80);
    e.m.write32(body_addr(slot) + 0x5c, info_addr(slot));
    drv::RigidBodyInfo info = *rb->info;
    std::memcpy(e.m.mem(info_addr(slot), 0x500), &info, 0x500);
    for (int i = 0; i < 16; ++i) e.m.write32(info_addr(slot) + 0x380 + 4u * uint32_t(i), 0);
    e.m.write32(info_addr(slot) + 0x4d8, 0);
    drv::PBondCarData d = car.d;
    uint8_t* raw = reinterpret_cast<uint8_t*>(&d);
    std::memcpy(e.m.mem(kCar, 0x460), raw, 0x460);
    uint8_t wp[0x40];
    wp_to_emu(car.d.world_pos, tw, wc, wp);
    put(e.m, kCar + 0x00, wp, 0x40);
    for (int w = 0; w < 4; ++w) {
        wp_to_emu(car.d.wheels[w], tw, wc, wp);
        put(e.m, kCar + 0x90 + 0x40u * uint32_t(w), wp, 0x40);
    }
    e.m.write32(kCar + 0x48, kRender);
    e.m.write32(kCar + 0x4c, 0);
    e.m.write32(kCar + 0x50, 0);
    e.m.write32(kCar + 0x54, 0);
    e.m.write32(kCar + 0x5c, kAttrSet);
    e.m.write32(kCar + 0x68, kVtable);
    e.m.write32(kCar + 0x23c, physics_emu);
    e.m.write32(kCar + 0x26c, kActions);
    e.m.write32(kCar + 0x2cc, 0);
    e.m.write32(kCar + 0x388, kAudio);
    drv::BondCarScratch s = drv::bond_car_scratch();
    std::memcpy(e.m.mem(kScratch, 0x450), &s, 0x450);
    e.m.write32(kScratch + 0x2dc, s.force_info ? info_addr(slot) : 0);
    e.m.write32(kScratch + 0x410, s.info ? info_addr(slot) : 0);
    // A Simulation: os corpos fora do scratchpad, o passo, a taxa, o dono da vaga.
    e.m.write32(kSim + 0x04, 0);
    e.m.write32(kSim + 0x1c, kBodies);
    e.m.write32(kSim + 0x24, kInfos);
    e.m.write32(kSim + 0x270 + 4u * uint32_t(slot), kCar);
    e.m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
    std::memcpy(e.m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
    e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
    e.m.write32(kScratchPadPtr, kScratch);
    e.m.write32(kRigidTuningPtr, kRigidScratch);
    // As marcas das consultas ao chão (o porte consulta o chão também para sortear as posições).
    const drv::WCollisionMgr& mgr = tw.mgr();
    e.m.write32(wc.mgr + 4, mgr.query_stamp);
    e.m.write32(wc.mgr + 0x2c, mgr.barrier_mask);
    e.m.write32(wc.mgr + 0x30, mgr.vu0_path);
    for (uint32_t i = 0; i < wc.count; ++i) e.m.write32(wc.insts + 0x40 * i + 0x10, tw.instances()[i].query_stamp);
    e.boost_time = car.boost_time;
    e.boost_extra_acc = car.boost_extra_acc;
}

// Compara o estado emulado com o do port; devolve o primeiro campo diferente (vazio: iguais).
std::string compare_state(CarEmu& e, drv::BondCar& car, drv::TrackWorld& tw, const WorldCopy& wc, bool scratch) {
    std::string out;
    const int slot = car.d.slot;
    drv::RigidBody* rb = car.body();
    if (!same_words("RigidBody", e.m.mem(body_addr(slot), 0x80), reinterpret_cast<const uint8_t*>(rb), 0x80,
                    {{0x5c, 0x60}}, out))
        return out;
    if (!same_words("RigidBodyInfo", e.m.mem(info_addr(slot), 0x500), reinterpret_cast<const uint8_t*>(nf::raw(rb->info)),
                    0x500, {{0x380, 0x3c0}, {0x4d8, 0x4dc}}, out))
        return out;
    const uint8_t* ec = e.m.mem(kCar, 0x460);
    const uint8_t* pc = reinterpret_cast<const uint8_t*>(&car.d);
    if (!same_words("PBondCar", ec, pc, 0x460,
                    {{0x00, 0x40}, {0x48, 0x6c}, {0x74, 0x84}, {0x90, 0x190}, {0x23c, 0x240}, {0x26c, 0x270},
                     {0x2cc, 0x2d0}, {0x2e0, 0x2e8}, {0x388, 0x38c}, {0x434, 0x438}},
                    out))
        return out;
    for (int w = 0; w < 4; ++w) {
        const drv::WWorldPos o = wp_from_emu(ec + 0x90 + 0x40 * w, tw, wc);
        if (!same_words(("rodas[" + std::to_string(w) + "]").c_str(), reinterpret_cast<const uint8_t*>(&o),
                        reinterpret_cast<const uint8_t*>(&car.d.wheels[w]), 0x40, {{0x0c, 0x10}}, out))
            return out;
    }
    if (scratch) {
        const drv::BondCarScratch& s = drv::bond_car_scratch();
        if (!same_words("BondCarScratch", e.m.mem(kScratch, 0x450), reinterpret_cast<const uint8_t*>(&s), 0x450,
                        {{0x2dc, 0x2e0}, {0x410, 0x414}}, out))
            return out;
        const uint32_t fi = e.m.read32(kScratch + 0x2dc), ii = e.m.read32(kScratch + 0x410);
        if ((fi != 0) != (s.force_info != 0) || (ii != 0) != (s.info != 0))
            return "BondCarScratch: os ponteiros do info";
    }
    return out;
}

// Um quatérnio (x, y, z, w) de guinada, arfagem e rolagem pequenas (ou uma inclinação qualquer).
void random_quat(Rng& rng, float q[4]) {
    if (rng.chance(0.1)) {
        float v[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)};
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        for (int i = 0; i < 4; ++i) q[i] = n > 0 ? v[i] / n : (i == 3 ? 1.0f : 0.0f);
        return;
    }
    const float yaw = rng.uni(-3.14159f, 3.14159f) * 0.5f;
    const float pitch = rng.uni(-0.15f, 0.15f) * 0.5f, roll = rng.uni(-0.15f, 0.15f) * 0.5f;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    q[0] = cy * sp * cr + sy * cp * sr;
    q[1] = sy * cp * cr - cy * sp * sr;
    q[2] = cy * cp * sr - sy * sp * cr;
    q[3] = cy * cp * cr + sy * sp * sr;
}

// O corpo numa posição sorteada (sobre uma instância do chão quando acha a altura) com velocidades sorteadas.
void random_body(Rng& rng, drv::BondCar& car, drv::TrackWorld& tw, const std::vector<uint32_t>& ground) {
    drv::RigidBody* rb = car.body();
    const drv::WCollisionInstance& ci = tw.instances()[ground[rng.next() % ground.size()]];
    float c[3];
    drv::WCollisionInstance_CalcPosition(tw.mgr(), ci, c);
    float p[3] = {c[0] + rng.uni(-4, 4), c[1] + 2.0f, c[2] + rng.uni(-4, 4)};
    float h = 0;
    if (tw.world_height_at_point(p, h)) p[1] = h + rng.uni(0.3f, 1.4f);
    else p[1] = c[1] + rng.uni(-1, 3);
    if (rng.chance(0.1)) p[1] += rng.uni(1, 6);  // no ar
    std::memcpy(rb->pos, p, 12);
    float q[4];
    random_quat(rng, q);
    drv::SetOrientation(rb, q);
    // As alavancas no mundo pela nova orientação (o que UpdatePositionAndOrientation faria).
    drv::RigidBodyInfo* info = rb->info;
    for (int i = 0; i < info->lever_count; ++i) {
        float l[4];
        std::memcpy(l, info->lever_local[i], 16);
        drv::ConvertLocalToWorld(rb, l);
        std::memcpy(info->lever_world[i], l, 16);
    }
    const float speed = rng.chance(0.2) ? rng.uni(0, 1.5f) : rng.uni(-8, 45);
    for (int i = 0; i < 3; ++i) rb->vel[i] = info->orient[2][i] * speed + rng.uni(-2, 2);
    rb->vel[1] = rng.uni(-3, 3);
    for (int i = 0; i < 3; ++i) rb->ang_vel[i] = rng.chance(0.3) ? rng.uni(-2.5f, 2.5f) : rng.uni(-0.3f, 0.3f);
    for (int i = 0; i < 3; ++i) {
        rb->momentum[i] = rb->vel[i] * rb->mass;
        rb->ang_momentum[i] = rng.uni(-2000, 2000);
        rb->force[i] = rng.uni(-50, 50);
        rb->torque[i] = rng.uni(-50, 50);
    }
    rb->state = 2;
    rb->ground_levers = int8_t(rng.range(0, 4));
    info->age = uint16_t(rng.chance(0.3) ? rng.range(0, 40) : rng.range(41, 400));
    info->flags[1] = uint8_t(rng.chance(0.1));
    info->flags[2] = uint8_t(rng.chance(0.1));
}

void random_controls(Rng& rng, drv::BondCar& car) {
    drv::PBondCarData& c = car.d;
    c.control.gas = rng.chance(0.3) ? 0.0f : rng.uni(0, 1);
    c.control.brake = rng.chance(0.6) ? 0.0f : rng.uni(0, 1);
    c.control.steering = rng.chance(0.3) ? 0.0f : rng.uni(-1, 1);
    c.control.hand_brake = uint8_t(rng.chance(0.2));
    c.control.gear = uint8_t(rng.chance(0.05));
}

void random_car(Rng& rng, drv::BondCar& car) {
    drv::PBondCarData& c = car.d;
    random_controls(rng, car);
    c.reverse_timer = int8_t(rng.range(0, 35));
    c.num_wheels_on_ground = int8_t(rng.range(0, 4));
    for (int w = 0; w < 4; ++w) c.suspension[w] = rng.chance(0.3) ? 0.0f : rng.uni(0, 0.4f);
    c.wheel_spin_enable = uint8_t(rng.chance(0.2));
    c.handbrake_reverse = uint8_t(rng.chance(0.3));
    c.car_wheel_speed = rng.uni(-0.06f, 0.06f);
    c.drive_state = int8_t(rng.range(0, 5));
    c.rocket_boost = rng.chance(0.8) ? 0 : rng.range(1, 700);
    c.gear = int8_t(rng.range(0, 5));
    c.previous_gear = int8_t(rng.range(0, 5));
    c.gear_change_timer = uint8_t(rng.chance(0.5) ? 0 : rng.range(1, 15));
    c.two_wheel_mode = uint8_t(rng.chance(0.08));
    c.two_wheel_timer = rng.chance(0.8) ? 1.0f : rng.uni(0.1f, 1.0f);
    c.tensor_dirty = uint8_t(rng.chance(0.1));
    c.against_wall = uint8_t(rng.chance(0.1));
    c.oil_slick = uint8_t(rng.chance(0.05));
    c.landing_flag = uint8_t(rng.chance(0.5) ? rng.range(0, 20) : rng.range(0, 255));
    c.landing_fx_timer = uint8_t(rng.chance(0.8) ? 0 : rng.range(1, 30));
    c.in_shock = int8_t(rng.chance(0.9) ? 0 : rng.range(1, 10));
    c.car_class = rng.chance(0.9) ? 0 : 2;
    c.wheel_spin_angle[0] = rng.uni(-0.2f, 1.2f);
    c.wheel_spin_angle[1] = rng.uni(-0.2f, 1.2f);
    c.landing_impact = rng.uni(0, 0.2f);
}

}  // namespace

int run_car_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                   int cases, uint32_t seed) {
    std::printf("\n== o carro: o código original do EE (interpretador MIPS + VU0) x o porte (PBondCar) ==\n");
    int failures = 0;
    CarEmu* e = new CarEmu();
    try {
        e->setup(elf, vu0_program, vu0_size);
        // As constantes do carro: o .data do ELF x os valores iniciais do port.
        {
            Tally t;
            ++t.cases;
            drv::reset_bond_car_tuning();
            const drv::BondCarTuning& k = drv::bond_car_tuning();
            struct F {
                uint32_t addr;
                const void* port;
            };
            const F fields[] = {
                {0x331970, &k.enable_roll_stops_threshold}, {0x331974, &k.base_friction_mass},
                {0x331980, &k.shred_drag}, {0x331988, &k.pad_dead_zone}, {0x33198c, &k.two_wheel_angle},
                {0x331990, &k.two_wheel_scale}, {0x331994, &k.two_wheel_limit},
                {0x331998, &k.two_wheel_opposite_scale}, {0x33199c, &k.two_wheel_tensor_scale},
                {0x3319a0, &k.skid_audio_scale}, {0x3319a4, &k.rolling_resistance}, {0x3319a8, &k.min_button_value},
                {0x3319ac, &k.wheel_spin_extra_rpm}, {0x3319b4, &k.damage_scale_collision},
                {0x3319b8, &k.emp_lifetime}, {0x3319bc, &k.tyre_damage_radius}, {0x3319c0, &k.max_wheel_spin_rate},
                {0x3319c4, &k.max_wheel_spin_rate_ai}, {0x3319c8, &k.post_brake_accel_count},
                {0x3319cc, &k.post_brake_accel_scale}, {0x3319d0, &k.post_brake_accel_min},
                {0x331964, &k.landing_air_steps}, {0x331968, &k.landing_spin_steps},
            };
            for (const F& f : fields) {
                uint32_t a, b;
                std::memcpy(&a, e->m.mem(f.addr, 4), 4);
                std::memcpy(&b, f.port, 4);
                if (a != b) t.fail("0x" + hex(f.addr) + ": .data " + hex(a) + ", porte " + hex(b));
            }
            for (int i = 0; i < 16; ++i) {
                uint32_t a, b, c2, d2;
                std::memcpy(&a, e->m.mem(0x3319d8 + 4u * uint32_t(i), 4), 4);
                std::memcpy(&b, &k.friction[i], 4);
                std::memcpy(&c2, e->m.mem(0x331a18 + 4u * uint32_t(i), 4), 4);
                std::memcpy(&d2, &k.lateral_loss[i], 4);
                if (a != b) t.fail("friction[" + std::to_string(i) + "]: .data " + hex(a) + ", porte " + hex(b));
                if (c2 != d2) t.fail("lateralLoss[" + std::to_string(i) + "]: .data " + hex(c2) + ", porte " + hex(d2));
            }
            report("as constantes do carro (0x331960..0x331a57) x o porte", t, failures);
        }
        (void)kTuningBegin;
        (void)kTuningEnd;
        // Paris: o mundo de colisão e o manejo do Vanquish.
        drv::Track track;
        std::string error;
        if (!track.load(disc, "paris_mis01", "", error)) {
            std::printf("  paris_mis01 não carregou (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        drv::TrackWorld tw;
        if (!tw.init(track.root(), &error)) {
            std::printf("  o mundo de colisão não montou (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        WorldCopy wc;
        if (!copy_world(e->m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  %s\n", error.c_str());
            delete e;
            return failures + 1;
        }
        drv::Bigf arc;
        drv::AttributeCollection vanquish;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || !vanquish.load(arc, "pvehicle", "vanquish", "paris_mis01", &error)) {
            std::printf("  o vanquish.atr não abriu (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        drv::reset_rigid_globals();
        drv::physics_pending_reset();
        drv::simulation_reset(60);
        drv::set_world(&tw);
        drv::BondCar* car = new drv::BondCar();
        car->name_ = "vanquish";
        car->car_physics = vanquish.car_physics();
        car->boost_time = vanquish.lookup_int("BOOST_TIME");
        car->boost_extra_acc = vanquish.lookup_float("BOOST_EXTRA_ACC");
        car->lookup_mass = vanquish.lookup_float("MASS");
        car->wheel_x_offset = vanquish.lookup_float("CAR_WHEEL_X_OFFSET");
        car->wheel_zf_offset = vanquish.lookup_float("CAR_WHEEL_ZF_OFFSET");
        car->wheel_zr_offset = vanquish.lookup_float("CAR_WHEEL_ZR_OFFSET");
        // Uma caixa de carro (as meias-medidas são do teste; a do Vanquish vem do modelo, ver nf_driving).
        const float half[4] = {0.95f, 0.62f, 2.25f, 0.0f};
        for (int i = 0; i < 8; ++i)
            car->collision_points.push_back({(i & 1) ? half[0] : -half[0], (i & 2) ? half[1] : -half[1],
                                             (i & 4) ? half[2] : -half[2], 0.0f});
        drv::RigidBodyParams p;
        p.mass = car->lookup_mass;
        p.type = 1;
        std::memcpy(p.half_size, half, 16);
        drv::Util_GenerateCarTensor(p.inv_inertia, p.mass, half[0], half[1], half[2]);
        p.owner = car;
        drv::RigidBody* rb = drv::simulation_add_body(drv::simulation(), p, car);
        car->d.slot = rb->slot;
        std::vector<uint32_t> ground;
        for (uint32_t i = 0; i < tw.instance_count(); ++i)
            if (tw.instances()[i].article && !(tw.instances()[i].flags & 1)) ground.push_back(i);
        std::printf("  vanquish: massa %.0f, %d chaves, MAXACC %.3f, SPRING_REST_LENGTH %.3f; corpo na vaga %d com %d "
                    "alavancas\n",
                    double(car->lookup_mass), vanquish.keys(), double(car->car_physics.max_acc),
                    double(car->car_physics.spring_rest_length), car->d.slot, int(rb->info->lever_count));

        Rng rng(seed ^ 0xca5);
        Tally t_pp, t_rpm, t_input;
        uint64_t on_ground = 0, landings = 0, two_wheel = 0, reversing = 0, limited = 0;
        drv::BondCarScratch& s = drv::bond_car_scratch();
        for (int c = 0; c < cases; ++c) {
            drv::simulation().steps = rng.range(0, 100000);
            const bool keep = c > 0 && rng.chance(0.4);
            if (keep) random_controls(rng, *car);
            else {
                random_body(rng, *car, tw, ground);
                random_car(rng, *car);
                if (rng.chance(0.3))
                    for (auto& w : car->d.wheels) w = drv::WWorldPos{};
            }
            tw.set_barrier_mask(rng.chance(0.5) ? 0x10 : 0xf0);
            tw.set_vu0_path(rng.chance(0.8) ? 1 : 0);
            // ProcessPhysics: +0x23c aponta para a cópia do CarPhysics na área de rascunho (como em Simulate).
            std::memcpy(&s.physics, &car->car_physics, sizeof(drv::CarPhysics));
            const uint32_t saved = car->d.physics;
            car->d.physics = nf::p32_u(&s.physics);
            put_state(*e, *car, tw, wc, kScratch);
            e->call(kFnProcessPhysics, {kCar});
            drv::PBondCar_ProcessPhysics(*car);
            ++t_pp.cases;
            std::string d = compare_state(*e, *car, tw, wc, true);
            if (!d.empty()) t_pp.fail("caso " + std::to_string(c) + ": " + d);
            car->d.physics = saved;
            on_ground += car->d.num_wheels_on_ground == 4;
            landings += car->d.landing_flag > 50;
            two_wheel += car->d.two_wheel_mode != 0;
            reversing += car->d.reversing != 0;
            limited += s.limited != 0;
            // CalculateRPM sobre o estado que saiu (igual dos dois lados se a comparação passou).
            put_state(*e, *car, tw, wc, kScratch);
            car->d.physics = nf::p32_u(&s.physics);
            e->call(kFnCalculateRPM, {kCar});
            const float rpm_emu = e->m.fpr(0);
            const float rpm = drv::PBondCar_CalculateRPM(*car);
            car->d.physics = saved;
            ++t_rpm.cases;
            if (ps2::fbits(rpm_emu) != ps2::fbits(rpm))
                t_rpm.fail("caso " + std::to_string(c) + ": original " + hex(ps2::fbits(rpm_emu)) + ", porte " +
                           hex(ps2::fbits(rpm)));
            else if (e->m.mem(kCar + 0x2d8, 1)[0] != uint8_t(car->d.gear))
                t_rpm.fail("caso " + std::to_string(c) + ": a marcha");
            // GetControllerInput com ações sorteadas.
            std::vector<drv::CarAction> acts;
            const int n = rng.range(0, 6);
            static const int32_t kTypes[] = {1, 2, 3, 12, 16, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 4, 40};
            for (int i = 0; i < n; ++i)
                acts.push_back({kTypes[rng.next() % (sizeof(kTypes) / sizeof(kTypes[0]))], rng.uni(-1, 1)});
            car->d.force_stop = uint8_t(rng.chance(0.05) ? (rng.chance(0.5) ? 1 : 2) : 0);
            car->d.steer_override = rng.chance(0.1) ? rng.uni(-1, 1) : 0.0f;
            car->d.steer_vertical_override = rng.chance(0.1) ? rng.uni(-1, 1) : 0.0f;
            car->d.axis_gas = rng.chance(0.8) ? 0.0f : rng.uni(0, 1);
            car->d.axis_brake = rng.chance(0.8) ? 0.0f : rng.uni(0, 1);
            car->d.steer_target = rng.uni(-1, 1);
            // GetControllerInput lê +0x23c fora da troca (o CarPhysics do carro); dos dois lados, a cópia (os mesmos bytes).
            put_state(*e, *car, tw, wc, kScratch);
            e->actions = acts;
            e->action_next = 0;
            e->call(kFnGetControllerInput, {kCar});
            car->action_count = 0;
            for (const auto& a : acts) car->push_action(a.type, a.value);
            const uint32_t saved2 = car->d.physics;
            car->d.physics = nf::p32_u(&s.physics);
            drv::PBondCar_GetControllerInput(*car);
            car->d.physics = saved2;
            ++t_input.cases;
            d = compare_state(*e, *car, tw, wc, false);
            if (!d.empty()) t_input.fail("caso " + std::to_string(c) + ": " + d);
            car->d.force_stop = 0;
        }
        std::printf("    quatro rodas no chão %" PRIu64 ", ImproveLanding %" PRIu64 ", TwoWheelStunt %" PRIu64
                    " (no modo %" PRIu64 "), ré %" PRIu64 ", roda no limite %" PRIu64 "; eventos %" PRIu64
                    ", efeitos %" PRIu64 ", som %" PRIu64 ", guardas %" PRIu64 "\n",
                    on_ground, drv::bond_car_stats().improve_landing, drv::bond_car_stats().two_wheel, two_wheel,
                    reversing, limited, e->events, e->fx, e->audio, drv::bond_car_stats().guards);
        (void)landings;
        report("PBondCar::ProcessPhysics (e AddWheelForces, o chão de Paris)", t_pp, failures);
        report("PBondCar::CalculateRPM (e a marcha)", t_rpm, failures);
        report("PBondCar::GetControllerInput (as ações)", t_input, failures);
        drv::set_world(nullptr);
        drv::simulation_reset(60);
        delete car;
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    delete e;
    return failures;
}

}  // namespace drvcheck
