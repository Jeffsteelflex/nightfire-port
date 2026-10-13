// nf_driving_check, parte 7 (tests/driving_check_collide.h): os choques. O porte monta o mundo de colisão de Paris
// (drv::TrackWorld: as instâncias com as barreiras, os objetos 'co', as instâncias de render) e dois corpos: o Vanquish
// do vanquish.atr do disco (o carro do jogador, tipo 1, vaga 0) e um segundo corpo na vaga 1 (um objeto de tipo 4 a 7,
// ou um segundo carro da IA, tipo 2 ou 3); uma cópia do mundo e dos corpos vai para a RAM emulada (a parte 5 copia o
// chão; aqui também as barreiras dos artigos, os objetos e as instâncias de render, com um WWorld falso), e o código
// ORIGINAL do DRIVING.ELF roda no interpretador MIPS com a VU0 ao lado do porte:
//  - RigidBody::CollideWithWorld (0x1fc2f0), no carro e no segundo corpo: o WCollider no dono (criado na primeira
//    chamada e guardado entre os casos que seguem andando), as barreiras, os cilindros, as caixas dos objetos
//    (ResolveWorldOBBCollision), o impulso (GenerateImpulse), o dano do mundo (CalculateAndApplyWorldDamage);
//  - RigidBody::CollideWithObject (0x1fbbd8): o carro contra o segundo corpo (as caixas orientadas, ResolveCollision,
//    o choque e o dano nos donos).
// Compara os dois corpos, os RigidBodyInfo, os carros, a área de rascunho (as duas caixas), os WCollider (a região, as
// barreiras listadas, os carimbos), os eventos ECollision na ordem (o COLLISION_INFO), as chamadas aos donos que o port
// não tem (ApplyDamage, o SetInShock e a PlayAnimation dos objetos), os eventos da missão e os estímulos da animação
// das instâncias batidas; no fim, as marcas de consulta do gerente e das instâncias.
//
// O que não é comparado, e por quê:
//  - os ponteiros (os corpos, os donos, o WCollider em +0x60 do dono, as instâncias das barreiras: comparadas pelo
//    índice);
//  - no COLLISION_INFO: o w do ponto (+0x0c) e o da velocidade (+0x2c), e +0x38..+0x3f: lixo da pilha no PS2;
//  - o "plano" do WCollider (+0x58) e a capacidade dos vetores.
// Fora do código original (ganchos nativos): memset, memmove, FastAlloc/FastFree, __builtin_delete, o evento
// ECollision (registrado), AttributeSet::LookupBool ("IS_POLICE"), SMissionManager::ProgrammerDefinedEvent
// (registrado), RAnimEngine::Handle::ProcessStimuli (registrado), PhysicsObject::PlayAnimation (registrada), a
// mensagem de depuração FUN_0010c6c8, e as entradas dos donos que o port não tem (ApplyDamage; o SetInShock dos
// objetos). As outras entradas dos carros (GetPhysics, SetAgainstWallFlag, SetInShock, AddDamageByPlayer,
// GetDamageByPlayerTimer, GetSplinePath, GetIsInTwoWheelMode, GetCarClass, IsReversing, GetCarControl,
// GetNumWheelsOnGround) são as funções originais.

#include "driving_check_collide.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/physics/collide.h"
#include "driving/physics/collider.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "driving_check_world.h"
#include "platform/crt_compat.h"
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
constexpr uint32_t kRigidScratchPtr = 0x339178;
constexpr uint32_t kWorldPtr = 0x33aac8;       // WWorld::fgWorld
constexpr uint32_t kMissionMgrPtr = 0x33a024;  // glbMissionManager

constexpr uint32_t kFnCollideWithWorld = 0x1fc2f0;
constexpr uint32_t kFnCollideWithObject = 0x1fbbd8;
constexpr uint32_t kFnStaticInit = 0x1fd338;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000;
constexpr uint32_t kInfos = 0x01002000;
constexpr uint32_t kCar[2] = {0x01012000, 0x01012800};
constexpr uint32_t kObj = 0x01013000;  // o dono de um objeto (0x100 bytes)
constexpr uint32_t kRigidScratch = 0x01013800;
constexpr uint32_t kCarVt = 0x01014000, kObjVt = 0x01014400;
constexpr uint32_t kWorld = 0x01015000;
constexpr uint32_t kSceneVt = 0x01015100;
constexpr uint32_t kMission = 0x01015200;
constexpr uint32_t kAttrSet = 0x01015600;
constexpr uint32_t kMgr = 0x01016000, kGrid = 0x01016100;
constexpr uint32_t kFnApplyDamage = 0x01017000, kFnObjShock = 0x01017010, kFnSceneVel = 0x01017020;
constexpr uint32_t kSceneTable = 0x01020000;  // 32 bytes por instância de render
constexpr uint32_t kSceneObjs = 0x01040000;   // 64 bytes por instância de render
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

uint32_t body_addr(int slot) { return kBodies + 0x80u * uint32_t(slot); }

// As pistas: Paris (o carro do jogador nas ruas) e as que têm objetos de colisão (os cilindros e as caixas do mundo).
const char* const kCollideTracks[] = {"paris_mis01", "snow1a_mis3", "junglea_mis13a", "snow2a_mis4"};
uint32_t info_addr(int slot) { return kInfos + 0x500u * uint32_t(slot); }

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
            out = std::string(what) + " +0x" + hex(o).substr(5) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

// Uma chamada a um dono que o port não tem (ou que só o port registra pelo outro lado): tipo, vaga e os argumentos.
struct OwnerCall {
    int kind = 0;  // 1 ApplyDamage, 2 SetInShock (objeto), 3 PlayAnimation, 4 evento da missão, 5 estímulo
    int slot = 0;
    uint32_t a = 0, b = 0, c = 0;
    float from[3] = {}, to[3] = {};
    bool operator==(const OwnerCall& o) const {
        return kind == o.kind && slot == o.slot && a == o.a && b == o.b && c == o.c &&
               std::memcmp(from, o.from, 12) == 0 && std::memcmp(to, o.to, 12) == 0;
    }
    std::string text() const {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "{tipo %d vaga %d %08x %08x %08x}", kind, slot, a, b, c);
        return buf;
    }
};

// Os donos do lado do port: o carro (BondCar, com ApplyDamage registrado) e o objeto.
std::vector<OwnerCall>* g_calls = nullptr;
class CheckCar : public drv::BondCar {
public:
    int slot_ = 0;
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        OwnerCall c;
        c.kind = 1;
        c.slot = slot_;
        c.a = ps2::fbits(amount);
        c.b = ps2::fbits(split);
        c.c = uint32_t(kind);
        std::memcpy(c.from, from, 12);
        std::memcpy(c.to, to, 12);
        g_calls->push_back(c);
        return 0;
    }
    void add_damage_by_player(float damage) override {
        const uint64_t before = mission_events;
        drv::BondCar::add_damage_by_player(damage);
        if (mission_events != before) {
            OwnerCall c;
            c.kind = 4;
            c.a = 2;
            g_calls->push_back(c);
        }
    }
    void play_animation(uint32_t id) override {
        OwnerCall c;
        c.kind = 3;
        c.slot = slot_;
        c.a = id;
        g_calls->push_back(c);
    }
};
class CheckObj : public drv::BasicOwner {
public:
    int slot_ = 1;
    void set_in_shock(float shock) override {
        OwnerCall c;
        c.kind = 2;
        c.slot = slot_;
        c.a = ps2::fbits(shock);
        g_calls->push_back(c);
    }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        OwnerCall c;
        c.kind = 1;
        c.slot = slot_;
        c.a = ps2::fbits(amount);
        c.b = ps2::fbits(split);
        c.c = uint32_t(kind);
        std::memcpy(c.from, from, 12);
        std::memcpy(c.to, to, 12);
        g_calls->push_back(c);
        return 0x20;
    }
    void play_animation(uint32_t id) override {
        OwnerCall c;
        c.kind = 3;
        c.slot = slot_;
        c.a = id;
        g_calls->push_back(c);
    }
};

class CollideEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = 0, heap_begin = 0, heap_end = 0;
    std::vector<OwnerCall> calls;
    std::vector<std::vector<uint8_t>> events;
    bool police[2] = {false, false};
    uint64_t debug_prints = 0;

    int slot_of_owner(uint32_t a) {
        if (a == kCar[0]) return 0;
        if (a == kCar[1] || a == kObj) return 1;
        throw nf::MipsError("dono desconhecido", a, 0);
    }

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
            if (heap + n > heap_end) throw nf::MipsError("FastAlloc: a área acabou", heap, n);
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x2d0910, "__builtin_new", alloc);
        m.hook(0x175730, "Event::operator_new", alloc);
        m.hook(0x149d30, "ECollision::ECollision", [this](MipsMachine& mm) {
            const uint8_t* p = mm.mem(uint32_t(mm.gpr(R::a1)), 0x54);
            events.emplace_back(p, p + 0x54);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x2f4b10, "AMenuSound::Trigger", [](MipsMachine&) {});
        m.hook(0x10c6c8, "FUN_0010c6c8 (depuração)", [this](MipsMachine&) { ++debug_prints; });
        m.hook(0x169b28, "AttributeSet::LookupBool", [this](MipsMachine& mm) {
            const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            const uint32_t set = uint32_t(mm.gpr(R::a0));
            const int car = set == kCar[0] + 0x5c ? 0 : 1;
            mm.set_gpr(R::v0, std::strcmp(key, "IS_POLICE") == 0 && police[car] ? 1 : 0);
        });
        m.hook(0x208540, "SMissionManager::ProgrammerDefinedEvent", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 4;
            c.a = uint32_t(mm.gpr(R::a1));
            calls.push_back(c);
        });
        m.hook(0x1a2868, "RAnimEngine::Handle::ProcessStimuli", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 5;
            c.a = uint32_t(mm.gpr(R::a0)) & 0xffff;  // o "handle" falso: o índice da instância de render
            c.b = uint32_t(mm.gpr(R::a1));
            c.c = uint32_t(mm.gpr(R::a2));
            calls.push_back(c);
        });
        m.hook(0x195bd0, "PhysicsObject::PlayAnimation", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 3;
            c.slot = slot_of_owner(uint32_t(mm.gpr(R::a0)));
            c.a = uint32_t(mm.gpr(R::a1));
            calls.push_back(c);
        });
        auto damage = [this](MipsMachine& mm, uint32_t ret) {
            OwnerCall c;
            c.kind = 1;
            c.slot = slot_of_owner(uint32_t(mm.gpr(R::a0)));
            c.a = ps2::fbits(mm.fpr(12));
            c.b = ps2::fbits(mm.fpr(13));
            c.c = uint32_t(mm.gpr(R::a3));
            std::memcpy(c.from, mm.mem(uint32_t(mm.gpr(R::a1)), 12), 12);
            std::memcpy(c.to, mm.mem(uint32_t(mm.gpr(R::a2)), 12), 12);
            calls.push_back(c);
            mm.set_gpr(R::v0, ret);
        };
        m.hook(kFnApplyDamage, "PBondCar::ApplyDamage (falso)", [this, damage](MipsMachine& mm) {
            damage(mm, uint32_t(mm.gpr(R::a0)) == kObj ? 0x20u : 0u);
        });
        m.hook(kFnObjShock, "PhysicsObject::SetInShock (objeto, falso)", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 2;
            c.slot = slot_of_owner(uint32_t(mm.gpr(R::a0)));
            c.a = ps2::fbits(mm.fpr(12));
            calls.push_back(c);
        });
        m.hook(kFnSceneVel, "RSceneObj: a velocidade (falsa)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        // A tabela virtual dos carros: delta 0 e a função (o PS2 lê o lh +0 e o lw +4).
        auto entry = [this](uint32_t vt, uint32_t off, uint32_t fn) {
            m.write32(vt + off, 0);
            m.write32(vt + off + 4, fn);
        };
        entry(kCarVt, 0x10, 0x187a78);   // SetInShock
        entry(kCarVt, 0x18, kFnApplyDamage);
        entry(kCarVt, 0x60, 0x18ad28);   // GetSplinePath
        entry(kCarVt, 0x98, 0x193008);   // GetCarClass
        entry(kCarVt, 0x130, 0x1930c0);  // GetCarControl
        entry(kCarVt, 0x188, 0x193160);  // GetNumWheelsOnGround
        entry(kCarVt, 0x1a8, 0x187608);  // AddDamageByPlayer
        entry(kCarVt, 0x1b0, 0x193168);  // GetDamageByPlayerTimer
        entry(kCarVt, 0x1d0, 0x193170);  // GetPhysics
        entry(kCarVt, 0x1d8, 0x1931a8);  // IsReversing
        entry(kCarVt, 0x1e0, 0x1931b8);  // SetAgainstWallFlag
        entry(kCarVt, 0x228, 0x193248);  // GetIsInTwoWheelMode
        entry(kObjVt, 0x10, kFnObjShock);
        entry(kObjVt, 0x18, kFnApplyDamage);
        entry(kSceneVt, 0x70, kFnSceneVel);
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 50'000'000);
    }
};

// O WCollider do original (na RAM emulada) contra o do port.
std::string compare_collider(CollideEmu& e, uint32_t emu, const drv::WCollider* port, const drv::TrackWorld& tw,
                             const WorldCopy& wc) {
    if (!emu || !port) return (emu != 0) == (port != nullptr) ? std::string() : "WCollider: só um lado tem";
    std::string out;
    if (!same_words("WCollider", e.m.mem(emu, 0x30), reinterpret_cast<const uint8_t*>(port->position), 0x30, {}, out))
        return out;
    if (e.m.read32(emu + 0x54) != port->region_valid) return "WCollider +0x54 (a região montada)";
    if (e.m.read32(emu + 0x5c) != port->mask) return "WCollider +0x5c (a máscara)";
    auto vsize = [&](uint32_t off, uint32_t elem) {
        const uint32_t a = e.m.read32(emu + off), b = e.m.read32(emu + off + 4);
        return a ? (b - a) / elem : 0u;
    };
    if (vsize(0x30, 8) != port->instances.size()) return "WCollider: as instâncias";
    const uint32_t nb = vsize(0x3c, 0x28);
    if (nb != port->barriers.size())
        return "WCollider: barreiras " + std::to_string(nb) + " no original, " + std::to_string(port->barriers.size()) +
               " no porte";
    const uint32_t bars = e.m.read32(emu + 0x3c);
    for (uint32_t i = 0; i < nb; ++i) {
        drv::BarrierListEntry be;
        std::memcpy(&be, e.m.mem(bars + 0x28 * i, 0x28), 0x28);
        be.instance = nf::p32_u(tw.instances()) + (be.instance - wc.insts);
        if (!same_words(("barreira " + std::to_string(i)).c_str(), reinterpret_cast<const uint8_t*>(&be),
                        reinterpret_cast<const uint8_t*>(&port->barriers[i]), 0x28, {}, out))
            return out;
    }
    if (vsize(0x48, 8) != port->stamps.size()) return "WCollider: os carimbos";
    return out;
}

}  // namespace

int run_collide_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    std::printf("\n== os choques: o código original do EE (interpretador MIPS + VU0) x o porte (CollideWithWorld, "
                "CollideWithObject) ==\n");
    int failures = 0;
    CollideEmu* e = new CollideEmu();
    std::vector<OwnerCall> port_calls;
    g_calls = &port_calls;
    std::vector<std::vector<uint8_t>> port_events;
    try {
        e->setup(elf, vu0_program, vu0_size);
        std::string error;
        drv::Bigf arc;
        drv::AttributeCollection vanquish;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || !vanquish.load(arc, "pvehicle", "vanquish", "paris_mis01", &error)) {
            std::printf("  o vanquish.atr não abriu (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        Rng rng(seed ^ 0xc011);
        for (const char* track_name : kCollideTracks) {
        drv::Track track;
        if (!track.load(disc, track_name, "", error)) {
            std::printf("  %s não carregou (%s)\n", track_name, error.c_str());
            ++failures;
            continue;
        }
        drv::TrackWorld tw;
        if (!tw.init(track.root(), &error)) {
            std::printf("  %s: o mundo de colisão não montou (%s)\n", track_name, error.c_str());
            ++failures;
            continue;
        }
        std::memset(e->m.mem(kSceneTable, kWorldBegin - kSceneTable), 0, kWorldBegin - kSceneTable);
        WorldCopy wc;
        if (!copy_world(e->m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  %s: %s\n", track_name, error.c_str());
            ++failures;
            continue;
        }
        e->heap_begin = (kWorldBegin + wc.bytes + 0xfff) & ~0xfffu;
        e->heap_end = kWorldEnd;
        e->heap = e->heap_begin;
        // O WWorld falso: as instâncias de render, a tabela dos objetos de cena (as instâncias com o bit 0x10 do byte
        // +0x0c têm um objeto, cujo "handle" em +0x28 é o índice) e a contagem.
        const uint32_t nri = tw.render_instance_count();
        if (kSceneTable + 32 * nri > kSceneObjs || kSceneObjs + 64 * nri > kWorldBegin) {
            std::printf("  %s: as instâncias de render não cabem\n", track_name);
            ++failures;
            continue;
        }
        e->m.write32(kWorld + 0x20, wc.render);
        e->m.write32(kWorld + 0x24, kSceneTable);
        e->m.write32(kWorld + 0x28, nri);
        e->m.write32(kWorldPtr, kWorld);
        uint32_t scene_objects = 0;
        for (uint32_t i = 0; i < nri; ++i) {
            const uint8_t* ri = tw.render_instances() + 64 * i;
            if (!(ri[0x0c] & 0x10)) continue;
            uint16_t idx;
            std::memcpy(&idx, ri + 0x0e, 2);
            const uint32_t obj = kSceneObjs + 64 * i;
            e->m.write32(kSceneTable + 32u * idx + 8, obj);
            e->m.write32(obj + 0x28, 0x80000000u | i);
            e->m.write32(obj + 0x3c, kSceneVt);
            ++scene_objects;
        }
        e->m.write32(kMissionMgrPtr, kMission);
        // As constantes: o construtor estático original e o do port.
        e->call(kFnStaticInit, {1, 0xffff});
        drv::reset_rigid_globals();
        drv::reset_bond_car_tuning();
        drv::physics_pending_reset();
        drv::simulation_reset(60);
        drv::set_world(&tw);
        drv::CollideEvents& ce = drv::collide_events();
        ce.collision = [&port_events](const drv::CollisionInfo& c) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
            port_events.emplace_back(p, p + 0x54);
        };
        ce.mission_event = [&port_calls](int32_t ev) {
            OwnerCall c;
            c.kind = 4;
            c.a = uint32_t(ev);
            port_calls.push_back(c);
        };
        ce.stimulus = [&port_calls](uint32_t index, uint32_t stim, int32_t step) {
            OwnerCall c;
            c.kind = 5;
            c.a = index;
            c.b = stim;
            c.c = uint32_t(step);
            port_calls.push_back(c);
        };
        // O Vanquish (vaga 0) e o segundo corpo (vaga 1).
        const float half[4] = {0.95f, 0.62f, 2.25f, 0.0f};
        CheckCar* cars[2] = {new CheckCar(), new CheckCar()};
        CheckObj* obj = new CheckObj();
        for (int k = 0; k < 2; ++k) {
            CheckCar& c = *cars[k];
            c.slot_ = k;
            c.name_ = "vanquish";
            c.car_physics = vanquish.car_physics();
            c.lookup_mass = vanquish.lookup_float("MASS");
            for (int i = 0; i < 8; ++i)
                c.collision_points.push_back({(i & 1) ? half[0] : -half[0], (i & 2) ? half[1] : -half[1],
                                              (i & 4) ? half[2] : -half[2], 0.0f});
            c.d.hit_points = 700.0f;
            c.d.hit_point_loc = nf::p32_u(&c.d.hit_points);
        }
        drv::Simulation& sim = drv::simulation();
        auto make_body = [&](int slot, int type, drv::PhysicsObject* owner, float mass, const float* hs) {
            drv::RigidBodyParams p;
            p.mass = mass;
            p.type = type;
            p.slot = slot;
            std::memcpy(p.half_size, hs, 16);
            drv::Util_GenerateCarTensor(p.inv_inertia, p.mass, hs[0], hs[1], hs[2]);
            p.owner = owner;
            sim.owners[slot] = owner;
            drv::RigidBody* rb = drv::GetRigidBody(sim, slot);
            drv::RigidBody_Construct(rb, p);
            return rb;
        };
        drv::RigidBody* car_body = make_body(0, 1, cars[0], cars[0]->lookup_mass, half);
        cars[0]->d.slot = 0;
        // As instâncias do mundo com barreiras e os objetos (para pôr os corpos perto deles).
        struct Wall {
            uint32_t inst, index;
        };
        std::vector<Wall> walls;
        uint32_t barrier_total = 0, tilted_walls = 0;
        for (uint32_t i = 0; i < tw.instance_count(); ++i) {
            const drv::WCollisionInstance& ci = tw.instances()[i];
            if (!ci.article) continue;
            const auto* a = nf::p32_ptr<const drv::CollisionArticle>(ci.article);
            for (uint32_t k = 0; k < a->barrier_count; ++k) walls.push_back({i, k});
            barrier_total += a->barrier_count;
            if (ci.flags & 3) tilted_walls += a->barrier_count;
        }
        uint32_t cyl = 0, boxes = 0, moving = 0;
        for (uint32_t i = 0; i < tw.object_count(); ++i) {
            const drv::WCollisionObject& o = tw.objects()[i];
            if (o.is_cylinder) ++cyl;
            else ++boxes;
            if (o.flags & 1) ++moving;
        }
        std::printf("  %s: %zu barreiras em instâncias (%u inclinadas), %u cilindros e %u caixas (%u que "
                    "andam), %u instâncias de render (%u com objeto de cena); %.1f MB copiados\n",
                    track_name, walls.size(), tilted_walls, cyl, boxes, moving, nri, scene_objects, double(wc.bytes) / 1048576.0);
        (void)barrier_total;

        // ---- os casos
        const bool debug = nf::get_env_narrow("NF_COLLIDE_DEBUG", nullptr);
        Tally t_world, t_world2, t_object;
        uint64_t hits = 0, ev_total = 0, box_hits = 0, cyl_hits = 0, refreshes = 0, kept = 0, damage_calls = 0,
                 against = 0, stimuli = 0, mission = 0, obj_hits = 0;
        int body2_kind = 0;  // 0 objeto, 1 carro
        drv::RigidBody* body2 = nullptr;
        auto put_owner_state = [&](int slot, drv::PhysicsObject* owner) {
            drv::RigidBody* rb = drv::GetRigidBody(sim, slot);
            drv::RigidBody b = *rb;
            std::memcpy(e->m.mem(body_addr(slot), 0x80), &b, 0x80);
            e->m.write32(body_addr(slot) + 0x5c, info_addr(slot));
            drv::RigidBodyInfo info = *rb->info;
            std::memcpy(e->m.mem(info_addr(slot), 0x500), &info, 0x500);
            for (int i = 0; i < 16; ++i) e->m.write32(info_addr(slot) + 0x380 + 4u * uint32_t(i), 0);
            e->m.write32(info_addr(slot) + 0x4d8, 0);
            (void)owner;
        };
        auto put_car = [&](int k, uint32_t collider) {
            drv::PBondCarData d = cars[k]->d;
            std::memcpy(e->m.mem(kCar[k], 0x460), &d, 0x460);
            e->m.write32(kCar[k] + 0x48, 0);
            e->m.write32(kCar[k] + 0x4c, 0);
            e->m.write32(kCar[k] + 0x50, 0);
            e->m.write32(kCar[k] + 0x54, kCar[k] + 0x42c);  // a vida em +0x42c
            e->m.write32(kCar[k] + 0x5c, kAttrSet);
            e->m.write32(kCar[k] + 0x60, collider);
            e->m.write32(kCar[k] + 0x68, kCarVt);
            e->m.write32(kCar[k] + 0x23c, kAttrSet + 0x100);  // o CarPhysics
            std::memcpy(e->m.mem(kAttrSet + 0x100, 0x100), &cars[k]->car_physics, 0x100);
            e->m.write32(kCar[k] + 0x2e4, 0);  // sem AIGroundVehicle
            e->m.write32(kCar[k] + 0x26c, 0);
            e->m.write32(kCar[k] + 0x2cc, 0);
            e->m.write32(kCar[k] + 0x388, 0);
            e->police[k] = cars[k]->is_police;
        };
        uint32_t emu_collider[2] = {0, 0};
        auto put_all = [&]() {
            put_owner_state(0, cars[0]);
            put_car(0, emu_collider[0]);
            if (body2) {
                put_owner_state(1, body2_kind ? static_cast<drv::PhysicsObject*>(cars[1]) : obj);
                if (body2_kind) {
                    put_car(1, emu_collider[1]);
                } else {
                    std::memset(e->m.mem(kObj, 0x100), 0, 0x100);
                    e->m.write32(kObj + 0x60, emu_collider[1]);
                    e->m.write32(kObj + 0x68, kObjVt);
                }
            }
            e->m.write32(kSim + 0x04, 0);
            e->m.write32(kSim + 0x1c, kBodies);
            e->m.write32(kSim + 0x24, kInfos);
            for (int s = 0; s < 48; ++s) e->m.write32(kSim + 0x270 + 4u * uint32_t(s), 0);
            e->m.write32(kSim + 0x270, kCar[0]);
            if (body2) e->m.write32(kSim + 0x274, body2_kind ? kCar[1] : kObj);
            e->m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
            std::memcpy(e->m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
            e->m.write32(kSim + 0xdc4, uint32_t(sim.steps));
            e->m.write32(kRigidScratchPtr, kRigidScratch);
            std::memcpy(e->m.mem(kRigidScratch, sizeof(drv::RigidBodyScratch)), &drv::rigid_scratch(),
                        sizeof(drv::RigidBodyScratch));
            const drv::WCollisionMgr& mgr = tw.mgr();
            e->m.write32(wc.mgr + 4, mgr.query_stamp);
            e->m.write32(wc.mgr + 0x2c, mgr.barrier_mask);
            e->m.write32(wc.mgr + 0x30, mgr.vu0_path);
            for (uint32_t i = 0; i < wc.count; ++i)
                e->m.write32(wc.insts + 0x40 * i + 0x10, tw.instances()[i].query_stamp);
        };
        auto compare_all = [&]() -> std::string {
            std::string out;
            for (int s = 0; s < (body2 ? 2 : 1); ++s) {
                drv::RigidBody* rb = drv::GetRigidBody(sim, s);
                if (!same_words(("RigidBody " + std::to_string(s)).c_str(), e->m.mem(body_addr(s), 0x80),
                                reinterpret_cast<const uint8_t*>(rb), 0x80, {{0x5c, 0x60}}, out))
                    return out;
                if (!same_words(("RigidBodyInfo " + std::to_string(s)).c_str(), e->m.mem(info_addr(s), 0x500),
                                reinterpret_cast<const uint8_t*>(nf::raw(rb->info)), 0x500,
                                {{0x380, 0x3c0}, {0x4d8, 0x4dc}}, out))
                    return out;
            }
            for (int k = 0; k < (body2 && body2_kind ? 2 : 1); ++k) {
                if (!same_words(("PBondCar " + std::to_string(k)).c_str(), e->m.mem(kCar[k], 0x460),
                                reinterpret_cast<const uint8_t*>(&cars[k]->d), 0x460,
                                {{0x00, 0x40}, {0x48, 0x6c}, {0x74, 0x84}, {0x90, 0x190}, {0x23c, 0x240},
                                 {0x26c, 0x270}, {0x2cc, 0x2d0}, {0x2e0, 0x2e8}, {0x388, 0x38c}, {0x434, 0x438}},
                                out))
                    return out;
            }
            const uint8_t* ps = reinterpret_cast<const uint8_t*>(&drv::rigid_scratch());
            if (!same_words("rascunho (as caixas)", e->m.mem(kRigidScratch + 0x200, 0x160), ps + 0x200, 0x160, {}, out))
                return out;
            if (e->m.read32(kRigidScratch + 0x398) != drv::rigid_scratch().body_box_valid) return "rascunho +0x398";
            // Os WCollider.
            emu_collider[0] = e->m.read32(kCar[0] + 0x60);
            out = compare_collider(*e, emu_collider[0], cars[0]->collider.get(), tw, wc);
            if (!out.empty()) return "carro: " + out;
            if (body2) {
                drv::PhysicsObject* o2 = body2_kind ? static_cast<drv::PhysicsObject*>(cars[1]) : obj;
                emu_collider[1] = e->m.read32((body2_kind ? kCar[1] : kObj) + 0x60);
                out = compare_collider(*e, emu_collider[1], o2->collider.get(), tw, wc);
                if (!out.empty()) return "segundo corpo: " + out;
            }
            // Os eventos ECollision.
            if (e->events.size() != port_events.size())
                return "eventos: " + std::to_string(e->events.size()) + " no original, " +
                       std::to_string(port_events.size()) + " no porte";
            for (size_t i = 0; i < port_events.size(); ++i)
                if (!same_words(("evento " + std::to_string(i)).c_str(), e->events[i].data(), port_events[i].data(),
                                0x54, {{0x0c, 0x10}, {0x2c, 0x30}, {0x38, 0x40}}, out))
                    return out;
            if (e->calls.size() != port_calls.size())
                return "chamadas: " + std::to_string(e->calls.size()) + " no original, " +
                       std::to_string(port_calls.size()) + " no porte";
            for (size_t i = 0; i < port_calls.size(); ++i)
                if (!(e->calls[i] == port_calls[i]))
                    return "chamada " + std::to_string(i) + ": original " + e->calls[i].text() + ", porte " +
                           port_calls[i].text();
            return out;
        };
        // Um corpo perto de uma barreira de Paris (ou de um objeto, ou solto numa instância), virado ao acaso, andando.
        auto place = [&](drv::RigidBody* rb, const float* hs) {
            float p[3] = {0, 0, 0};
            const int where = rng.range(0, 9);
            if (where < 7 && !walls.empty()) {
                const Wall& w = walls[rng.next() % walls.size()];
                const drv::WCollisionInstance& ci = tw.instances()[w.inst];
                const auto* a = nf::p32_ptr<const uint8_t>(ci.article);
                const auto* ah = reinterpret_cast<const drv::CollisionArticle*>(a);
                const auto* b =
                    reinterpret_cast<const drv::CollisionBarrier*>(a + 0x20 + ah->barrier_offset) + w.index;
                const float t = rng.uni(0, 1);
                const float l[3] = {b->x0 + (b->x1 - b->x0) * t, b->y0 + rng.uni(-0.5f, 1.5f),
                                    b->z0 + (b->z1 - b->z0) * t};
                float m[16];
                drv::WCollisionInstance_MakeMatrix(tw.mgr(), ci, m, true);
                // Do referencial da instância ao mundo: m é o mundo -> instância (só rotação e translação).
                const float lt[3] = {l[0] - m[12], l[1] - m[13], l[2] - m[14]};
                for (int c = 0; c < 3; ++c) p[c] = lt[0] * m[c * 4 + 0] + lt[1] * m[c * 4 + 1] + lt[2] * m[c * 4 + 2];
                const float side = rng.uni(-4.0f, 4.0f);
                const float nx = (b->z1 - b->z0), nz = -(b->x1 - b->x0);
                const float nl = std::sqrt(nx * nx + nz * nz);
                if (nl > 0) {
                    p[0] += nx / nl * side;
                    p[2] += nz / nl * side;
                }
            } else if (where < 9 && tw.object_count()) {
                const drv::WCollisionObject& o = tw.objects()[rng.next() % tw.object_count()];
                p[0] = o.position[0] + rng.uni(-4, 4);
                p[1] = o.position[1] + rng.uni(0.2f, 1.5f);
                p[2] = o.position[2] + rng.uni(-4, 4);
            } else {
                const drv::WCollisionInstance& ci = tw.instances()[rng.next() % tw.instance_count()];
                drv::WCollisionInstance_CalcPosition(tw.mgr(), ci, p);
                p[1] += rng.uni(0.0f, 2.0f);
            }
            std::memcpy(rb->pos, p, 12);
            const float yaw = rng.uni(-3.14159f, 3.14159f), pitch = rng.chance(0.8) ? 0.0f : rng.uni(-0.3f, 0.3f);
            float q[4] = {std::sin(pitch * 0.5f) * std::cos(yaw * 0.5f), std::sin(yaw * 0.5f) * std::cos(pitch * 0.5f),
                          -std::sin(yaw * 0.5f) * std::sin(pitch * 0.5f), std::cos(yaw * 0.5f) * std::cos(pitch * 0.5f)};
            const float ql = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            for (float& v : q) v /= ql;
            drv::SetOrientation(rb, q);
            drv::RigidBodyInfo* info = rb->info;
            // As quinas da caixa no mundo (o que o passo da VU0 deixa nos dois buffers).
            for (int buf = 0; buf < 2; ++buf)
                for (int k = 0; k < 8; ++k)
                    for (int c = 0; c < 3; ++c) {
                        const float sx = (k & 1) ? hs[0] : -hs[0], sy = (k & 2) ? hs[1] : -hs[1],
                                    sz = (k & 4) ? hs[2] : -hs[2];
                        info->corners[buf][k][c] =
                            sx * info->orient[0][c] + sy * info->orient[1][c] + sz * info->orient[2][c];
                    }
            info->corner_buffer = int8_t(rng.range(0, 1));
            const float speed = rng.chance(0.2) ? rng.uni(0, 2.5f) : rng.uni(-10, 45);
            for (int c = 0; c < 3; ++c) rb->vel[c] = info->orient[2][c] * speed + rng.uni(-4, 4);
            rb->vel[1] = rng.uni(-2, 2);
            for (int c = 0; c < 3; ++c) {
                rb->ang_vel[c] = rng.chance(0.4) ? rng.uni(-2, 2) : rng.uni(-0.2f, 0.2f);
                rb->momentum[c] = rb->vel[c] * rb->mass;
                rb->ang_momentum[c] = rng.uni(-3000, 3000);
            }
            rb->state = 2;
            info->flags[0] = uint8_t(rng.chance(0.05));
            info->flags[1] = uint8_t(rng.chance(0.5));
            info->flags[3] = uint8_t(rng.chance(0.3));
            if (rng.chance(0.03)) rb->radius_flags &= ~1u;
            else rb->radius_flags |= 1u;
        };
        auto drift = [&](drv::RigidBody* rb) {
            // Segue andando: a posição avança um passo (as quinas e a orientação ficam).
            for (int c = 0; c < 3; ++c) rb->pos[c] += rb->vel[c] * (1.0f / 60.0f) + rng.uni(-0.05f, 0.05f);
            rb->radius_flags |= 1u;
            rb->state = 2;
        };
        auto random_car = [&](CheckCar& c, bool player) {
            c.d.two_wheel_mode = uint8_t(rng.chance(0.1));
            c.d.in_shock = int8_t(rng.chance(0.7) ? 0 : rng.range(1, 20));
            c.d.car_class = player ? 0 : (rng.chance(0.5) ? 2 : 3);
            c.d.damage_by_player = rng.chance(0.5) ? 0.0f : rng.uni(0, 900);
            c.d.damage_by_player_timer = uint8_t(rng.chance(0.5) ? 0 : rng.range(1, 60));
            c.d.against_wall = uint8_t(rng.chance(0.3));
            c.d.reversing = uint8_t(rng.chance(0.2));
            c.d.num_wheels_on_ground = int8_t(rng.range(0, 4));
            c.d.hit_points = rng.chance(0.2) ? rng.uni(-10, 50) : rng.uni(50, 700);
            c.is_police = rng.chance(0.2);
            c.d.control.steering = rng.uni(-1, 1);
            c.d.control.gas = rng.uni(0, 1);
        };
        auto reset_colliders = [&]() {
            cars[0]->collider.reset();
            cars[1]->collider.reset();
            obj->collider.reset();
            emu_collider[0] = emu_collider[1] = 0;
            e->heap = e->heap_begin;
        };
        for (int c = 0; c < cases; ++c) {
            const bool keep = c > 0 && rng.chance(0.5);
            if (!keep) {
                reset_colliders();
                place(car_body, half);
                random_car(*cars[0], true);
                // O segundo corpo: às vezes nenhum, um objeto ou um carro da IA, perto do carro.
                if (body2) {
                    drv::RigidBody_Destruct(body2);
                    sim.owners[1] = nullptr;
                    body2 = nullptr;
                }
                const int k = rng.range(0, 9);
                if (k >= 2) {
                    body2_kind = k >= 6 ? 1 : 0;
                    float hs2[4] = {half[0], half[1], half[2], 0};
                    if (!body2_kind)
                        for (int i = 0; i < 3; ++i) hs2[i] = rng.uni(0.2f, 1.5f);
                    if (body2_kind) {
                        random_car(*cars[1], false);
                        body2 = make_body(1, cars[1]->d.car_class == 3 ? 3 : 2, cars[1], cars[1]->lookup_mass, hs2);
                        cars[1]->d.slot = 1;
                    } else {
                        body2 = make_body(1, rng.range(4, 7), obj, rng.uni(5, 2500), hs2);
                    }
                    place(body2, hs2);
                    if (rng.chance(0.7))
                        for (int i = 0; i < 3; ++i) body2->pos[i] = car_body->pos[i] + rng.uni(-3.5f, 3.5f);
                    body2->state = int8_t(rng.chance(0.8) ? 2 : rng.range(0, 3));
                }
            } else {
                ++kept;
                drift(car_body);
                if (body2) drift(body2);
                random_car(*cars[0], true);
            }
            sim.steps = rng.range(0, 100000);
            tw.set_vu0_path(rng.chance(0.8) ? 1 : 0);
            drv::rigid_scratch().body_box_valid = uint32_t(rng.chance(0.5));
            const int op = rng.range(0, 9);
            port_events.clear();
            port_calls.clear();
            e->events.clear();
            e->calls.clear();
            put_all();
            Tally* t = &t_world;
            if (op < 6) {
                e->call(kFnCollideWithWorld, {body_addr(0)});
                drv::CollideWithWorld(car_body);
            } else if (op < 8 && body2) {
                t = &t_world2;
                e->call(kFnCollideWithWorld, {body_addr(1)});
                drv::CollideWithWorld(body2);
            } else if (body2) {
                t = &t_object;
                e->call(kFnCollideWithObject, {body_addr(0), 0, 1});
                drv::CollideWithObject(car_body, 0);
                obj_hits += port_events.empty() ? 0 : 1;
            } else {
                e->call(kFnCollideWithWorld, {body_addr(0)});
                drv::CollideWithWorld(car_body);
            }
            ++t->cases;
            const std::string d = compare_all();
            if (!d.empty()) {
                t->fail("caso " + std::to_string(c) + ": " + d);
                if (debug) {
                    std::printf("    [caso %d op %d] %s\n", c, op, d.c_str());
                    for (int s = 0; s < (body2 ? 2 : 1); ++s) {
                        const drv::RigidBody* rb = drv::GetRigidBody(sim, s);
                        float r;
                        std::memcpy(&r, &rb->radius_flags, 4);
                        std::printf("      corpo %d tipo %d estado %d pos (%.3f %.3f %.3f) raio %.3f eventos porte %zu "
                                    "original %zu\n",
                                    s, rb->type, rb->state, double(rb->pos[0]), double(rb->pos[1]), double(rb->pos[2]),
                                    double(r), port_events.size(), e->events.size());
                    }
                }
                // Os dois lados divergiram: recomeça do zero no próximo caso.
                reset_colliders();
            }
            hits += port_events.empty() ? 0 : 1;
            ev_total += port_events.size();
            for (const OwnerCall& oc : port_calls) {
                if (oc.kind == 1) ++damage_calls;
                if (oc.kind == 4) ++mission;
                if (oc.kind == 5) ++stimuli;
            }
            for (const auto& ev : port_events) {
                if (ev[0x4c] == 6 && ev[0x4a] == 0x10 && ev[0x4e] == 0x10) ++box_hits;
            }
            against += cars[0]->d.against_wall;
        }
        (void)cyl_hits;
        (void)refreshes;
        std::printf("    casos com choque %" PRIu64 " (eventos %" PRIu64 ", das caixas %" PRIu64 ", entre corpos %" PRIu64
                    "), seguindo andando %" PRIu64 ", ApplyDamage %" PRIu64 ", estímulos %" PRIu64
                    ", eventos da missão %" PRIu64 ", encostado %" PRIu64 ", mensagens de depuração %" PRIu64
                    ", guardas do mundo %" PRIu64 "\n",
                    hits, ev_total, box_hits, obj_hits, kept, damage_calls, stimuli, mission, against,
                    e->debug_prints, drv::g_world_collision_guards);
        report("RigidBody::CollideWithWorld (o carro do jogador)", t_world, failures);
        report("RigidBody::CollideWithWorld (o segundo corpo)", t_world2, failures);
        report("RigidBody::CollideWithObject (o carro e o segundo corpo)", t_object, failures);
        // As marcas de consulta (o gerente e as instâncias), no fim.
        {
            Tally t;
            ++t.cases;
            if (e->m.read32(wc.mgr + 4) != tw.mgr().query_stamp) t.fail("WCollisionMgr +0x04");
            for (uint32_t i = 0; i < wc.count && !t.bad; ++i)
                if (e->m.read32(wc.insts + 0x40 * i + 0x10) != tw.instances()[i].query_stamp)
                    t.fail("a marca da instância " + std::to_string(i));
            report("as marcas de consulta (gerente e instâncias)", t, failures);
        }
        drv::set_world(nullptr);
        ce.collision = nullptr;
        ce.mission_event = nullptr;
        ce.stimulus = nullptr;
        drv::simulation_reset(60);
        delete cars[0];
        delete cars[1];
        delete obj;
        }  // as pistas
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    g_calls = nullptr;
    delete e;
    return failures;
}

}  // namespace drvcheck
