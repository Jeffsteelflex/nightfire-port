// nf_driving_check, parte 20, os tiros (tests/driving_check_integ.h; docs/driving.md seção 24): as balas do DRIVING.ELF
// (o RayShell e o Shell, driving/game/shells.h), as janelas (WCollisionMgr::CheckHitWindow), os efeitos e o dano do
// mundo das balas (Shell::ApplyWorldEffects, ApplyWorldDamage) e o EFireWeapon inteiro (driving/game/fire_weapon.cpp):
// o código ORIGINAL do EE no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte, em cenas sorteadas
// na pista de Paris.
//
//  a) as utilidades: Util_CollideRayWithSphere/StretchedSphere, Util_GenerateMatrixUnit, os dois Util_PerturbVector,
//     Util_GaussRandom (com o rand e o log da newlib), SMissionManager::GetEnemyAccuracyBoost;
//  b) os globais das balas: StaticStoreTracer, DeleteTracer, ClearTracers, Reset, os raios dos gatilhos, os clarões;
//  c) o RayShell (o construtor faz o tiro inteiro) em cenas de corpos rígidos, simples e personagens (tipo 4) perto de
//     um ponto da pista: os donos atingidos (ApplyDamage, AddDamageByPlayer), as anotações (a galeria, as faíscas, a zona
//     da IA, a queimadura, os estímulos, as janelas), os sons, os globais, os eventos (as explosões da trapaça);
//  d) o Shell: Simulation::SpawnShellObject e os passos (UpdatePosition, DetectCollisionsSRB, Shell::Simulate);
//  e) Shell::ApplyWorldEffects e ApplyWorldDamage sozinhos;
//  f) WCollisionMgr::CheckHitWindow nas janelas de Paris (segmentos que cruzam as barreiras com janelas);
//  g) o EFireWeapon com os caminhos novos: as balas, o ActWeapon (o tiro de um personagem e o de Bond), a pontaria dos
//     carros da IA, a mira da câmera, o helicóptero.
//
// A infraestrutura (a RAM emulada, os donos de teste, a cena, os sons) é a da parte 18 (tests/driving_check_missile.cpp),
// copiada e estendida.

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/player_camera.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/explosion.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/sound/audio_internal.h"
#include "driving/track.h"
#include "driving_check_integ.h"
#include "driving_check_world.h"
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

// As funções originais.
constexpr uint32_t kFnSimCtor = 0x1fe1b0, kFnDetect = 0x2014b8, kFnSrbUpdate = 0x1fd798;
constexpr uint32_t kFnRayShell = 0x198e80, kFnShellSimulate = 0x19d080, kFnSpawnShell = 0x1fffa8;
constexpr uint32_t kFnApplyWorldEffects = 0x19d5a8, kFnApplyWorldDamage = 0x19ddb8, kFnCheckHitWindow = 0x2153b0;
constexpr uint32_t kFnCheckHitWorld = 0x215cc8, kFnEFireWeapon = 0x14ca60;
// Os ganchos.
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnBuiltinNew = 0x2d0910, kFnOperatorNew = 0x2d09c8;
constexpr uint32_t kFnEventNew = 0x175730, kFnEventDelete = 0x175758, kFnUAlloc = 0x2d03b8, kFnFastFree = 0x2cfe50;
constexpr uint32_t kFnBuiltinDelete = 0x2d09a8, kFnMemFill = 0x260300, kFnMemmove = 0x2a7f34, kFnMemset = 0x2a8038;
constexpr uint32_t kFnPrintf = 0x2a8148, kFnAttrSetCtor = 0x1696f0, kFnStrcasecmp = 0x2a9198;
constexpr uint32_t kFnVideoRate = 0x2d6b90, kFnBoundingDims = 0x1c9778, kFnInstancePos = 0x1c9368;
constexpr uint32_t kFnGfxLookup = 0x232f48, kFnGfxTrigger = 0x2330b0, kFnGfxIncidental = 0x233108;
constexpr uint32_t kFnJolt = 0x165fb0, kFnRumble = 0x1660f0, kFnScorch = 0x1eb078, kFnScorchWorld = 0x1ea8e8;
constexpr uint32_t kFnLight = 0x1afa88, kFnZoneGet = 0x140100, kFnZoneRegister = 0x1402f8;
constexpr uint32_t kFnTimer = 0x261038, kFnIndexLookup = 0x2f5880, kFnSoundNew = 0x2fca38, kFnMixAdd = 0x2f2d50;
constexpr uint32_t kFnVoiceCtor = 0x2dd790, kFnPOSimulate = 0x195aa8, kFnLookupInt = 0x169bf0;
constexpr uint32_t kFnLookupBool = 0x169b28, kFnStimuli = 0x1a2868, kFnHitWindowCtor = 0x14f330;
constexpr uint32_t kFnFindSpk = 0x1e8008, kFnParticles = 0x1e6330;

// Os globais do PS2.
constexpr uint32_t kSim = 0x339190, kSigCounter = 0x339180, kMissionPtr = 0x33a024, kBank4 = 0x3562b0;
constexpr uint32_t kExCounter = 0x32bbdc, kRing = 0x32bbfc, kRingTime = 0x32bc00, kLevelName = 0x1004f4;
constexpr uint32_t kUberCheat = 0x1009a0;
constexpr uint32_t kRayEnable = 0x3320b0, kNumTracers = 0x3320b4, kTracers = 0x3320c0, kNumFlashes = 0x3328c0;
constexpr uint32_t kFlashes = 0x3328d0, kNumActive = 0x332950, kActive = 0x332960, kTrigIndex = 0x333560;
constexpr uint32_t kLastExplosion = 0x333564, kSparksFound = 0x333580, kConcrete = 0x333584, kRicochet = 0x333588;
constexpr uint32_t kSparks = 0x3d0d4c, kTracerSide = 0x424180, kRRandom = 0x333e60;
constexpr uint32_t kSndName = 0x32bba0, kSndActor = 0x32bba4, kSndCar = 0x32bba8;
constexpr uint32_t kGaussHave = 0x33a53c, kGaussSaved = 0x417040, kImpurePtr = 0x34ff4c;

// A RAM emulada.
constexpr uint32_t kRigidVec = 0x01000000, kSrbVec = 0x01001800, kInfos = 0x01003000;
constexpr uint32_t kRandom = 0x01012000;
constexpr uint32_t kImages = 0x01013000;      // as imagens dos donos: 0x200 bytes cada (48 rígidos + 96 simples)
constexpr uint32_t kVt = 0x01025000;          // a tabela virtual falsa dos donos (0x300)
constexpr uint32_t kFake = 0x01026000;        // as funções falsas
constexpr uint32_t kMission = 0x01027000;     // 0x1b50
constexpr uint32_t kBank = 0x01029000, kFeedback = 0x01029100, kPhys = 0x01029200, kZone = 0x01029400;
constexpr uint32_t kSettings = 0x01029800;    // as configurações da missão (0x2f8)
constexpr uint32_t kNames = 0x0102a000;       // as cadeias dos nomes (64 bytes por dono)
constexpr uint32_t kRenders = 0x0102e000;
constexpr uint32_t kCars = 0x01032000;
constexpr uint32_t kEvent = 0x01033000;
constexpr uint32_t kSceneFake = 0x01034000;   // o RSceneObj falso do EFireWeapon
constexpr uint32_t kTargets = 0x01035000;     // os WTargetable (0x50 cada)
constexpr uint32_t kWorldFake = 0x01036000;
constexpr uint32_t kMgr = 0x01036800, kGrid = 0x01036900;
constexpr uint32_t kWM = 0x01037000;          // o SWeaponManager (0x190) e a tabela das armas (0xa80)
constexpr uint32_t kCamList = 0x01039000, kCam = 0x01039100;  // a lista das câmeras e o RPlayerCamera
constexpr uint32_t kCamListPtrEmu = 0x333ed8;
constexpr uint32_t kActors = 0x0103c000;      // os atores dos personagens (0x200 cada)
constexpr uint32_t kShapes = 0x0103e000;      // as esferas esticadas dos atores (0x20 cada)
constexpr uint32_t kAW = 0x0103f000;          // o ActWeapon (0x200), o +0x15c (0x20)
constexpr uint32_t kAI = 0x0103f400;          // os AIGroundVehicle (0x2a0 cada, até 4)
constexpr uint32_t kHeliData = 0x0103fe00;    // o +0x134 do helicóptero
constexpr uint32_t kSceneTable = 0x01040000;  // 32 bytes por instância de render
constexpr uint32_t kSceneObjs = 0x01070000;   // 64 bytes por instância de render
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01200000;
constexpr uint32_t kWorldBegin = 0x01200000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

constexpr uint32_t kFnApplyDamage = kFake + 0x00, kFnName = kFake + 0x10, kFnAddDamage = kFake + 0x20;
constexpr uint32_t kFnGetPhysics = kFake + 0x30, kFnPlayerTarget = kFake + 0x50, kFnNop = kFake + 0x70;
constexpr uint32_t kFnGetAI = kFake + 0x80, kFnActorSub = kFake + 0x90, kFnActorValue = kFake + 0xa0;
constexpr uint32_t kFnHitPoints = kFake + 0xb0;
constexpr uint32_t kVtRender = kVt + 0x400;  // a tabela virtual dos RSceneObj falsos
constexpr uint32_t kVtActor = kVt + 0x600;   // a do +0x140 dos atores
constexpr uint32_t kMixBase = 0x7000;

uint32_t image_of(int ordered) { return kImages + 0x200u * uint32_t(ordered); }
uint32_t render_of(int ordered) { return kRenders + 0x40u * uint32_t(ordered); }
uint32_t name_of(int ordered) { return kNames + 64u * uint32_t(ordered); }

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x5a5a) {}
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

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

std::string cstr(const MipsMachine& m, uint32_t a) {
    std::string s;
    if (a == 0 || a >= MipsMachine::kRamSize) return s;
    for (uint32_t i = 0; i < 256; ++i) {
        const char c = char(m.read32((a + i) & ~3u) >> (8 * ((a + i) & 3)));
        if (!c) break;
        s += c;
    }
    return s;
}

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-52s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    failures += int(t.bad);
    std::fflush(stdout);
}

uint32_t rd_word(const uint8_t* p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

bool same_bytes(const char* what, const uint8_t* emu, const uint8_t* port, uint32_t n, std::string& out) {
    for (uint32_t o = 0; o < n; o += 4) {
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

// Uma chamada às entradas dos donos (a tabela virtual): 1 ApplyDamage, 2 AddDamageByPlayer.
struct OwnerCall {
    int kind = 0;
    int owner = -1;
    uint32_t amount = 0, split = 0, dkind = 0, player = 0;
    uint32_t from[3] = {}, to[3] = {};
    bool operator==(const OwnerCall& o) const {
        return kind == o.kind && owner == o.owner && amount == o.amount && split == o.split && dkind == o.dkind &&
               player == o.player && std::memcmp(from, o.from, 12) == 0 && std::memcmp(to, o.to, 12) == 0;
    }
    std::string text() const {
        char b[220];
        std::snprintf(b, sizeof b, "{%d dono %d %08x %08x %u jogador %u de %08x,%08x,%08x a %08x,%08x,%08x}", kind,
                      owner, amount, split, dkind, player, from[0], from[1], from[2], to[0], to[1], to[2]);
        return b;
    }
};
std::vector<OwnerCall> g_port_calls;

void port_damage_call(int owner, const float from[3], const float to[3], float amount, float split, int32_t kind) {
    OwnerCall c;
    c.kind = 1;
    c.owner = owner;
    c.amount = ps2::fbits(amount);
    c.split = ps2::fbits(split);
    c.dkind = uint32_t(kind);
    c.player = drv::damage_source_is_player() ? 1u : 0u;
    std::memcpy(c.from, from, 12);
    std::memcpy(c.to, to, 12);
    g_port_calls.push_back(c);
}
void port_add_call(int owner, float damage) {
    OwnerCall c;
    c.kind = 2;
    c.owner = owner;
    c.amount = ps2::fbits(damage);
    g_port_calls.push_back(c);
}

// Os donos do lado do porte.
class CkCar : public drv::BondCar {
public:
    int ordered = 0;
    float hp = 700.0f;
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return damage_flags;
    }
    void add_damage_by_player(float damage) override { port_add_call(ordered, damage); }
    float hit_points() override { return hp; }
    int32_t damage_flags = 0;
};
class CkRigid : public drv::BasicOwner {
public:
    int ordered = 0;
    int32_t type = 4;
    std::string nm;
    std::unique_ptr<drv::WWorldPos> wp = std::make_unique<drv::WWorldPos>();  // (fora do objeto: o alinhamento de 16)
    int32_t damage_flags = 0;
    std::map<uint32_t, uint32_t> words;  // as palavras da imagem do PS2 que o porte lê e grava (o +0x64, o +0x134)
    CkRigid() { set_world_pos(wp.get()); }
    int32_t kind() override { return type; }
    uint32_t raw_word(uint32_t offset) override {
        auto it = words.find(offset);
        return it == words.end() ? 0u : it->second;
    }
    bool set_raw_word(uint32_t offset, uint32_t value) override {
        words[offset] = value;
        return true;
    }
    const char* name() override { return nm.c_str(); }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return damage_flags;
    }
    void add_damage_by_player(float damage) override { port_add_call(ordered, damage); }
};
// Um dono de corpo simples (os personagens: tipo 4, o +0x70 o ator e o +0x8c; o helicóptero: o +0x134).
class CkSimple : public drv::SimpleObject {
public:
    int ordered = 0;
    std::string nm;
    std::map<uint32_t, uint32_t> words;
    int32_t damage_flags = 0;
    const char* name() override { return nm.c_str(); }
    uint32_t raw_word(uint32_t offset) override {
        auto it = words.find(offset);
        return it == words.end() ? 0u : it->second;
    }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return damage_flags;
    }
    void add_damage_by_player(float damage) override { port_add_call(ordered, damage); }
};

// Um som criado.
struct SoundRec {
    std::string mix;
    uint32_t pos[3] = {}, vel[3] = {};
    uint32_t min = 0, max = 0, max2 = 0, volume = 0, pitch = 0, fx = 0, lifetime = 0;
    bool operator==(const SoundRec& o) const {
        return mix == o.mix && std::memcmp(pos, o.pos, 12) == 0 && std::memcmp(vel, o.vel, 12) == 0 && min == o.min &&
               max == o.max && max2 == o.max2 && volume == o.volume && pitch == o.pitch && fx == o.fx &&
               lifetime == o.lifetime;
    }
    std::string text() const {
        char b[260];
        std::snprintf(b, sizeof b, "{%s pos %08x,%08x,%08x vel %08x,%08x,%08x min %08x max %08x %08x vol %08x tom %08x}",
                      mix.c_str(), pos[0], pos[1], pos[2], vel[0], vel[1], vel[2], min, max, max2, volume, pitch);
        return b;
    }
};

const char* const kMixNames[] = {"Explosions",    "ExplosionBass",   "Weapons Fire", "Henchmen Fire",
                                 "POV Weapons",   "Weapons Transit", "Bullet Impact", "Bullet Ricochet"};
constexpr uint32_t kMixCount = sizeof kMixNames / sizeof kMixNames[0];

// As anotações comparadas (o lado do original).
class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::vector<drv::EffectsCall> notes;
    std::vector<OwnerCall> calls;
    std::vector<uint32_t> sounds;
    std::vector<uint32_t> events;
    std::map<uint32_t, int> owner_index;
    std::map<uint32_t, int32_t> damage_flags;    // a imagem -> o que o ApplyDamage falso devolve
    std::map<std::pair<uint32_t, std::string>, int32_t> attrs;  // (a imagem, a chave) -> o LookupInt/LookupBool
    std::map<uint32_t, uint32_t> ai_of;          // a imagem do carro -> o AIGroundVehicle emulado
    std::map<uint32_t, float> hp;                // a imagem -> a vida (GetHitPoints)
    int32_t timer = 0;
    uint32_t gfx_answer = 0, spark_answer = 0, player_target = 0;
    uint32_t actor_value_ptr = 0;                 // o que a entrada +0x24 do +0x140 do ator devolve
    int32_t actor_value = 0;
    uint32_t phys_sub = 0;
    uint32_t last_rayshell = 0;  // o último FastAlloc(0xd0): o RayShell
    std::string error;
    uint64_t errors = 0;

    void note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0,
              uint32_t a5 = 0) {
        notes.push_back({fn, {a0, a1, a2, a3, a4, a5}});
    }
    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > kHeapEnd) throw nf::MipsError("o heap emulado acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    bool call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
            return true;
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
            note(0xeeeeeeeeu);
            return false;
        }
    }
    int owner_of(uint32_t image) {
        auto it = owner_index.find(image);
        return it == owner_index.end() ? -2 : it->second;
    }
    int32_t streak_type = 0;  // o BULLET_STREAK_TYPE de qualquer objeto
    int32_t attr(uint32_t obj_attr, const std::string& key) {
        if (key == "BULLET_STREAK_TYPE") return streak_type;
        auto it = attrs.find({obj_attr - 0x5c, key});
        return it == attrs.end() ? 0 : it->second;
    }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto newf = [this](MipsMachine& mm) {
            const uint32_t n = uint32_t(mm.gpr(R::a0));
            const uint32_t a = alloc(std::max(n, 0x140u));
            if (n == 0xd0) last_rayshell = a;
            mm.set_gpr(R::v0, a);
        };
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", newf);
        m.hook(kFnBuiltinNew, "__builtin_new", newf);
        m.hook(kFnOperatorNew, "operator new", newf);
        m.hook(kFnUAlloc, "UMemory::Alloc", newf);
        m.hook(kFnEventNew, "Event::operator_new", [this](MipsMachine& mm) {
            const uint32_t a = alloc(uint32_t(mm.gpr(R::a0)));
            events.push_back(a);
            mm.set_gpr(R::v0, a);
        });
        m.hook(kFnEventDelete, "Event::operator_delete", [](MipsMachine&) {});
        m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kFnBuiltinDelete, "__builtin_delete", [](MipsMachine&) {});
        m.hook(kFnMemFill, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a1));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a2) & 0xff), n);
        });
        m.hook(kFnMemmove, "memmove", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), s = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(d, n), mm.mem(s, n), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnPrintf, "printf", [this](MipsMachine& mm) {
            if (cstr(mm, uint32_t(mm.gpr(R::a0))).find("TOO MANY RAY") != std::string::npos) note(0x2a8148);
        });
        m.hook(kFnAttrSetCtor, "AttributeSet::AttributeSet", [](MipsMachine&) {});
        m.hook(kFnStrcasecmp, "strcasecmp", [](MipsMachine& mm) {
            std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            for (char& c : a) c = char(std::tolower(uint8_t(c)));
            for (char& c : b) c = char(std::tolower(uint8_t(c)));
            mm.set_gpr(R::v0, uint32_t(a == b ? 0 : (a < b ? -1 : 1)));
        });
        m.hook(kFnVideoRate, "GetFoundationVideoModeRate",
               [](MipsMachine& mm) { mm.set_fpr(0, float(drv::simulation().frame_rate)); });
        m.hook(kFnLookupInt, "AttributeSet::LookupInt", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint32_t(attr(uint32_t(mm.gpr(R::a0)), cstr(mm, uint32_t(mm.gpr(R::a1))))));
        });
        m.hook(kFnLookupBool, "AttributeSet::LookupBool", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, attr(uint32_t(mm.gpr(R::a0)), cstr(mm, uint32_t(mm.gpr(R::a1)))) != 0 ? 1u : 0u);
        });
        // A tabela virtual falsa dos donos.
        m.hook(kFnApplyDamage, "PhysicsObject::ApplyDamage (falso)", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 1;
            const uint32_t img = uint32_t(mm.gpr(R::a0));
            c.owner = owner_of(img);
            c.amount = mm.fpr_bits(12);
            c.split = mm.fpr_bits(13);
            c.dkind = uint32_t(mm.gpr(R::a3));
            std::memcpy(c.from, mm.mem(uint32_t(mm.gpr(R::a1)), 12), 12);
            std::memcpy(c.to, mm.mem(uint32_t(mm.gpr(R::a2)), 12), 12);
            // A assinatura de quem causou (t0): é do jogador? (a conta do PBondCar::ApplyDamage, com os objetos do porte)
            const uint32_t sig = mm.read32(uint32_t(mm.gpr(R::t0)));
            drv::Simulation& sim = drv::simulation();
            drv::PhysicsObject* who = drv::FindPhysicsObjectSignature(sim, sig);
            drv::PhysicsObject* pl = sim.cars.empty() ? nullptr : sim.cars[0];
            c.player = who && drv::PhysicsObject_IsOwnedBy(who, pl) ? 1u : 0u;
            calls.push_back(c);
            note(0x00189598, c.amount, uint32_t(c.owner), c.dkind);  // o porte anota o PBondCar::ApplyDamage
            auto it = damage_flags.find(img);
            mm.set_gpr(R::v0, uint32_t(it == damage_flags.end() ? 0 : it->second));
        });
        m.hook(kFnAddDamage, "PBondCar::AddDamageByPlayer (falso)", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 2;
            c.owner = owner_of(uint32_t(mm.gpr(R::a0)));
            c.amount = mm.fpr_bits(12);
            calls.push_back(c);
        });
        m.hook(kFnName, "PhysicsObject: o nome (falso)", [this](MipsMachine& mm) {
            const int o = owner_of(uint32_t(mm.gpr(R::a0)));
            if (o < 0) throw nf::MipsError("dono desconhecido (o nome)", uint32_t(mm.gpr(R::a0)), 0);
            mm.set_gpr(R::v0, name_of(o));
        });
        m.hook(kFnGetPhysics, "PBondCar::GetPhysics (falso)", [this](MipsMachine& mm) {
            mm.write32(kPhys + 0xc0, phys_sub);
            mm.set_gpr(R::v0, kPhys);
        });
        m.hook(kFnPlayerTarget, "PBondCar: o WTargetable (falso)", [this](MipsMachine& mm) {
            note(0x18029c, player_target);
            mm.set_gpr(R::v0, player_target);
        });
        m.hook(kFnGetAI, "PBondCar::GetAIGroundVehicle (falso)", [this](MipsMachine& mm) {
            auto it = ai_of.find(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, it == ai_of.end() ? 0u : it->second);
        });
        m.hook(kFnActorSub, "o ator: a entrada +0x24 do +0x140 (falso)",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, actor_value_ptr); });
        m.hook(kFnActorValue, "o ator: a entrada +0x13c (falso)",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(actor_value)); });
        m.hook(kFnNop, "nada (falso)", [](MipsMachine&) {});
        auto entry = [this](uint32_t base, uint32_t off, uint32_t fn) {
            m.write32(base + off, 0);
            m.write32(base + off + 4, fn);
        };
        entry(kVt, 0x18, kFnApplyDamage);
        entry(kVt, 0x58, kFnGetAI);
        entry(kVt, 0x80, kFnName);
        entry(kVt, 0x138, kFnActorValue);
        entry(kVt, 0x1a8, kFnAddDamage);
        entry(kVt, 0x1d0, kFnGetPhysics);
        entry(kVt, 0x298, kFnPlayerTarget);
        entry(kVtRender, 0x38, kFnNop);  // RSceneObj::TransformToWorldSpace (vazia)
        entry(kVtActor, 0x20, kFnActorSub);
        // Os sistemas à volta (anotam como o porte).
        m.hook(kFnGfxLookup, "GFX::LookupReference", [this](MipsMachine& mm) {
            note(0x232f48, gfx_answer);
            mm.set_gpr(R::v0, gfx_answer);
        });
        m.hook(kFnGfxIncidental, "GFX::TriggerIncidental", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1)), q = uint32_t(mm.gpr(R::a2));
            note(0x233108, uint32_t(mm.gpr(R::a0)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8), mm.read32(q),
                 mm.read32(q + 12));
        });
        m.hook(kFnFindSpk, "RParticleLibrary::FindSystemViaTag", [this](MipsMachine& mm) {
            note(0x1e8008, uint32_t(mm.gpr(R::a1)), spark_answer);
            mm.set_gpr(R::v0, spark_answer);
        });
        m.hook(kFnParticles, "RParticleSystemManager::CreateParticles", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a2));
            note(0x1e6330, uint32_t(mm.gpr(R::a1)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8),
                 uint32_t(mm.gpr(R::t0)));
        });
        m.hook(kFnScorchWorld, "RScorchWorld::ScorchWorld", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            note(0x1ea8e8, mm.fpr_bits(12), uint32_t(mm.gpr(R::a3)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8));
        });
        m.hook(kFnStimuli, "RAnimEngine::Handle::ProcessStimuli", [this](MipsMachine& mm) {
            note(0x1a2868, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                 uint32_t(mm.gpr(R::a3)));
        });
        m.hook(kFnHitWindowCtor, "EHitWindow::EHitWindow", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1)), d = uint32_t(mm.gpr(R::a3));
            note(0x14f330, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8), mm.read32(d), mm.read32(d + 4),
                 uint32_t(mm.gpr(R::t0)));
            if (!events.empty()) events.pop_back();
        });
        m.hook(kFnJolt, "IFeedback::ApplyJolt", [this](MipsMachine& mm) { note(kFnJolt, mm.fpr_bits(12)); });
        m.hook(kFnRumble, "IFeedback::SetRumble",
               [this](MipsMachine& mm) { note(kFnRumble, mm.fpr_bits(12), mm.fpr_bits(13)); });
        m.hook(kFnScorch, "RScorchWorld::ScorchGround", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            note(kFnScorch, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8));
        });
        m.hook(kFnLight, "RHighLevelLightManager::AddExplosion", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            note(kFnLight, mm.fpr_bits(12), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8));
        });
        m.hook(kFnZoneGet, "AIZoneController::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, kZone); });
        m.hook(kFnZoneRegister, "AIZoneController::RegisterZone", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a2));
            note(kFnZoneRegister, uint32_t(mm.gpr(R::a1)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8));
        });
        m.hook(kFnTimer, "TIMER_gettick", [this](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(timer)); });
        m.hook(kFnIndexLookup, "AIndex::Lookup", [this](MipsMachine& mm) {
            const std::string name = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const int32_t patch = drv::ABank_Category(4)->index.Lookup(name.c_str());
            note(kFnIndexLookup, uint32_t(patch));
            mm.set_gpr(R::v0, uint32_t(patch));
        });
        m.hook(kFnSoundNew, "ABaseSound::operator_new", [this](MipsMachine& mm) {
            const uint32_t a = alloc(uint32_t(mm.gpr(R::a0)));
            sounds.push_back(a);
            mm.set_gpr(R::v0, a);
        });
        m.hook(kFnMixAdd, "AMix::Add", [](MipsMachine& mm) {
            const std::string name = cstr(mm, uint32_t(mm.gpr(R::a0)));
            uint32_t idx = 0xff;
            for (uint32_t i = 0; i < kMixCount; ++i)
                if (name == kMixNames[i]) idx = i;
            mm.set_gpr(R::v0, kMixBase + idx);
        });
        m.hook(kFnVoiceCtor, "AVoice::AVoice", [](MipsMachine&) {});
        m.hook(kFnPOSimulate, "PhysicsObject::Simulate", [this](MipsMachine& mm) {
            note(kFnPOSimulate, mm.read32(uint32_t(mm.gpr(R::a0)) + 0x40));
        });
        m.hook(0x195a30, "PhysicsObject::GetHitPoints", [this](MipsMachine& mm) {
            auto it = hp.find(uint32_t(mm.gpr(R::a0)));
            mm.set_fpr(0, it == hp.end() ? 1.0f : it->second);
        });
        m.hook(0x10c6c8, "depuração (FUN_0010c6c8)", [this](MipsMachine&) { note(0x10c6c8); });
        m.hook(0x2f4a20, "AMenuSound::Trigger", [this](MipsMachine& mm) {
            note(0x2f4a20, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)));
        });
        m.hook(0x2a9098, "sprintf", [](MipsMachine& mm) {
            const std::string fmt = cstr(mm, uint32_t(mm.gpr(R::a1)));
            std::string out;
            for (size_t i = 0; i < fmt.size(); ++i) {
                if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's') {
                    out += cstr(mm, uint32_t(mm.gpr(R::a2)));
                    ++i;
                } else {
                    out += fmt[i];
                }
            }
            put(mm, uint32_t(mm.gpr(R::a0)), out.c_str(), uint32_t(out.size() + 1));
            mm.set_gpr(R::v0, uint32_t(out.size()));
        });
        m.hook(0x1ffd70, "Simulation::SpawnMineObject", [this](MipsMachine& mm) {
            note(0x1ffd70, uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x1ffea8, "Simulation::SpawnGrenadeObject", [this](MipsMachine& mm) {
            note(0x1ffea8, uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x19bea0, "Sentry::GetTarget", [this](MipsMachine& mm) {
            note(0x19bea0);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x1c9328, "RSceneObj::GetInstanceSystemID", [this](MipsMachine& mm) { mm.set_gpr(R::v0, system); });
        m.hook(0x22af38, "WTargetPicker::GetSelectedTarget", [this](MipsMachine& mm) {
            note(0x22af38, selected_port);
            mm.set_gpr(R::v0, selected);
        });
        m.hook(0x22b6b8, "WTargetPicker::CycleToNextTarget", [this](MipsMachine&) { note(0x22b6b8); });
        m.hook(0x1c7b60, "RSceneObj::UseInstance", [this](MipsMachine& mm) {
            note(0x1c7b60, scene_port, uint32_t(mm.gpr(R::a2)));
        });
        m.hook(0x2064d8, "SMissionManager::SetDriveMissile", [this](MipsMachine& mm) {
            const uint32_t mis = uint32_t(mm.gpr(R::a1));
            note(0x2064d8, mis ? 1u : 0u);
            mm.write32(uint32_t(mm.gpr(R::a0)) + 0x358, mis);
        });
        m.hook(0x195978, "PhysicsObject::SetRenderObject",
               [](MipsMachine& mm) { mm.write32(uint32_t(mm.gpr(R::a0)) + 0x48, uint32_t(mm.gpr(R::a1))); });
        m.hook(0x1c7098, "RSceneObj::RSceneObj", [this](MipsMachine& mm) {
            note(0x1c7098);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2f4b10, "AMenuSound::Trigger (o lock)", [this](MipsMachine&) { note(0x2f4b10); });
        m.hook(0x190eb8, "PBondCar_PosInSoftZone", [this](MipsMachine& mm) {
            note(0x190eb8);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(kFnInstancePos, "RSceneObj::GetInstancePosition", [this](MipsMachine& mm) {
            note(0x1c9368, scene_port, uint32_t(mm.gpr(R::a1)));
            put(mm, uint32_t(mm.gpr(R::a2)), scene_matrix, 64);
        });
        m.hook(0x1cae00, "RSceneObj::LocateFX", [this](MipsMachine& mm) {
            note(0x1cae00, scene_port, effect_port);
            put(mm, uint32_t(mm.gpr(R::a2)), scene_matrix, 64);
        });
        m.hook(0x1c95f0, "RSceneObj::TransformMatrixByInstancePosition", [this](MipsMachine& mm) {
            note(0x1c95f0, scene_port, uint32_t(mm.gpr(R::a1)));
            float mx[4][4];
            std::memcpy(mx, mm.mem(uint32_t(mm.gpr(R::a2)), 64), 64);
            transform_by_instance(mx);
            put(mm, uint32_t(mm.gpr(R::a2)), mx, 64);
        });
        m.write32(kSceneFake + 0x3c, kVtRender);
        m.write32(kCamListPtrEmu, kCamList);
        m.write32(kCamList + 4, kCam);
        m.write32(kBank4, kBank);
        m.write32(kBank + 0x8, 0xffffffffu);
        m.write32(kMissionPtr, kMission);
        m.write32(kMission + 0x1c, kSettings);
    }

    // O estado do EFireWeapon que os ganchos dão (o RSceneObj, a matriz, o sistema, o alvo).
    float scene_matrix[4][4] = {};
    uint32_t scene_port = 0, effect_port = 0, system = 0, selected = 0, selected_port = 0;
    float tbi[4][4] = {};  // a TransformMatrixByInstancePosition: m x tbi
    void transform_by_instance(float mx[4][4]) {
        float r[4][4];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += mx[i][k] * tbi[k][j];
                r[i][j] = s;
            }
        std::memcpy(mx, r, 64);
    }

    void new_simulation() {
        heap = kHeap;
        events.clear();
        sounds.clear();
        call(kFnSimCtor, {kSim});
    }
};

// ---------------------------------------------------------------- a cena: os dois lados

struct Scene {
    std::vector<std::unique_ptr<drv::PhysicsObject>> owned;
    CkCar* player = nullptr;
    int rigid = 0, simple = 0;
    std::vector<int> humans;     // os índices ordenados dos personagens
    std::vector<int> cars;       // os carros que não são o do jogador (rígidos)
};

void random_quat(Rng& r, float q[4]) {
    float x, y, z, w, n;
    do {
        x = r.real(-1, 1);
        y = r.real(-1, 1);
        z = r.real(-1, 1);
        w = r.real(-1, 1);
        n = x * x + y * y + z * z + w * w;
    } while (n < 0.05f || n > 1.0f);
    const float k = 1.0f / std::sqrt(n);
    q[0] = x * k;
    q[1] = y * k;
    q[2] = z * k;
    q[3] = w * k;
}

void write_image(Emu& e, int ordered, int32_t type, uint16_t flags, int16_t slot, bool feedback,
                 const std::string& name) {
    const uint32_t a = image_of(ordered);
    std::memset(e.m.mem(a, 0x200), 0, 0x200);
    e.m.write32(a + 0x40, uint32_t(type));
    e.m.write32(a + 0x44, uint32_t(flags) | uint32_t(uint16_t(slot)) << 16);
    e.m.write32(a + 0x50, feedback ? kFeedback : 0u);
    e.m.write32(a + 0x68, kVt);
    put(e.m, name_of(ordered), name.c_str(), uint32_t(name.size() + 1));
    e.owner_index[a] = ordered;
}

void sim_to_emu(Emu& e, const Scene& sc) {
    drv::Simulation& sim = drv::simulation();
    MipsMachine& m = e.m;
    m.write32(kSim + 0x0, kRandom);
    put(m, kRandom, &drv::sim_random(), 16);
    m.write32(kSim + 0x4, 0);
    m.write32(kSim + 0x8, uint32_t(sim.rigid_count));
    m.write32(kSim + 0xc, uint32_t(sim.simple_count));
    m.write32(kSim + 0x1c, kRigidVec);
    m.write32(kSim + 0x20, kSrbVec);
    m.write32(kSim + 0x24, kInfos);
    m.write32(kSim + 0x28, MipsMachine::kScratchpad);
    m.write32(kSim + 0x2c, MipsMachine::kScratchpad + 0x1800);
    for (int i = 0; i < drv::kRigidBodySlots; ++i) {
        uint8_t body[0x80];
        std::memcpy(body, &sim.bodies[i], 0x80);
        const uint32_t info = kInfos + 0x500u * uint32_t(i);
        std::memcpy(body + 0x5c, &info, 4);
        put(m, kRigidVec + 0x80u * uint32_t(i), body, 0x80);
        uint8_t inf[0x500];
        std::memcpy(inf, &sim.infos[i], 0x500);
        std::memset(inf + 0x380, 0, 0x40);
        std::memset(inf + 0x4d8, 0, 4);
        put(m, info, inf, 0x500);
        m.write32(kSim + 0x30 + 4u * uint32_t(i), sim.rigid_sig[i]);
        m.write32(kSim + 0x270 + 4u * uint32_t(i), sim.owners[i] ? image_of(i) : 0u);
    }
    put(m, kSrbVec, sim.simple_bodies, 0x40 * drv::kSimpleBodySlots);
    for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
        m.write32(kSim + 0xf0 + 4u * uint32_t(i), sim.simple_sig[i]);
        m.write32(kSim + 0x330 + 4u * uint32_t(i), sim.simple_owners[i] ? image_of(drv::kRigidBodySlots + i) : 0u);
        put(m, kSim + 0x4b0 + 0x18u * uint32_t(i), &sim.srb_hits[i], 0x18);
    }
    m.write32(kSim + 0xdb4, uint32_t(sim.state));
    m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
    m.write32(kSim + 0xdc0, ps2::fbits(sim.dt));
    m.write32(kSim + 0xdc4, uint32_t(sim.steps));
    m.write32(kSigCounter, drv::simulation_signature_counter());
    if (sc.player) {
        m.write32(kCars, image_of(0));
        m.write32(kSim + 0xdcc, kCars);
        m.write32(kSim + 0xdd0, kCars + 4);
        m.write32(kSim + 0xdd4, kCars + 4);
    }
}

bool compare_sim(Emu& e, std::string& why) {
    drv::Simulation& sim = drv::simulation();
    MipsMachine& m = e.m;
    if (!same_bytes("os corpos simples", m.mem(kSrbVec, 0x1800), reinterpret_cast<const uint8_t*>(sim.simple_bodies),
                    0x1800, why))
        return false;
    for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
        if (!same_bytes(("o mapa de choque " + std::to_string(i)).c_str(), m.mem(kSim + 0x4b0 + 0x18u * uint32_t(i), 0x18),
                        reinterpret_cast<const uint8_t*>(&sim.srb_hits[i]), 0x18, why))
            return false;
        if (m.read32(kSim + 0xf0 + 4u * uint32_t(i)) != sim.simple_sig[i]) {
            why = "a assinatura simples " + std::to_string(i);
            return false;
        }
        if ((m.read32(kSim + 0x330 + 4u * uint32_t(i)) != 0) != (sim.simple_owners[i] != nullptr)) {
            why = "o dono simples " + std::to_string(i);
            return false;
        }
    }
    if (m.read32(kSim + 0xc) != uint32_t(sim.simple_count)) {
        why = "a contagem dos corpos simples";
        return false;
    }
    if (m.read32(kSigCounter) != drv::simulation_signature_counter()) {
        why = "o contador das assinaturas";
        return false;
    }
    if (!same_bytes("o SimRandom", m.mem(kRandom, 16), reinterpret_cast<const uint8_t*>(&drv::sim_random()), 16, why))
        return false;
    return true;
}

std::string compare_notes(const std::vector<drv::EffectsCall>& port, const std::vector<drv::EffectsCall>& emu) {
    const size_t n = std::max(port.size(), emu.size());
    for (size_t i = 0; i < n; ++i) {
        const bool hp = i < port.size(), he = i < emu.size();
        if (hp && he && port[i].fn == emu[i].fn && std::memcmp(port[i].a, emu[i].a, sizeof port[i].a) == 0) continue;
        auto txt = [](const drv::EffectsCall& c) {
            char b[120];
            std::snprintf(b, sizeof b, "%06x(%x,%x,%x,%x,%x,%x)", c.fn, c.a[0], c.a[1], c.a[2], c.a[3], c.a[4], c.a[5]);
            return std::string(b);
        };
        return "anotação " + std::to_string(i) + ": original " + (he ? txt(emu[i]) : std::string("nada")) +
               ", porte " + (hp ? txt(port[i]) : std::string("nada"));
    }
    return "";
}

std::string compare_calls(const std::vector<OwnerCall>& port, const std::vector<OwnerCall>& emu) {
    const size_t n = std::max(port.size(), emu.size());
    for (size_t i = 0; i < n; ++i) {
        const bool hp = i < port.size(), he = i < emu.size();
        if (hp && he && port[i] == emu[i]) continue;
        return "chamada ao dono " + std::to_string(i) + ": original " + (he ? emu[i].text() : std::string("nada")) +
               ", porte " + (hp ? port[i].text() : std::string("nada"));
    }
    return "";
}

// A cena sorteada perto de `at`: o carro do jogador na vaga 0 (às vezes), outros corpos rígidos (carros da IA de tipo
// 1, outros), corpos simples de teste e personagens (tipo 4, com o ator).
void random_scene(Rng& r, Emu& e, Scene& sc, const float at[3], float spread, bool force_player = false) {
    drv::simulation_reset(r.chance(0.85) ? 60 : 50);
    drv::Simulation& sim = drv::simulation();
    sim.steps = r.range(0, 5000);
    drv::SimRandom_Construct(drv::sim_random());
    drv::SimRandom_Reset(drv::sim_random());
    for (int k = r.range(0, 40); k > 0; --k) drv::SimRandom_Generate(drv::sim_random());
    drv::simulation_signature_counter() = uint32_t(r.range(1, 0x2000));
    e.owner_index.clear();
    e.damage_flags.clear();
    e.attrs.clear();
    e.ai_of.clear();
    e.hp.clear();
    const int nrigid = force_player ? r.range(1, 6) : r.range(0, 6);
    for (int i = 0; i < nrigid; ++i) {
        drv::RigidBodyParams p;
        p.mass = r.real(50, 2500);
        p.type = i == 0 ? 1 : (r.chance(0.5) ? 1 : r.range(2, 7));
        for (int k = 0; k < 3; ++k) p.pos[k] = at[k] + r.real(-spread, spread);
        p.pos[1] = at[1] + r.real(0.3f, 3);
        for (float& v : p.vel) v = r.real(-20, 20);
        float q[4];
        random_quat(r, q);
        drv::VU0_quattom4(q, p.orient);
        for (int k = 0; k < 3; ++k) p.half_size[k] = r.real(0.3f, 2.5f);
        p.inv_inertia[0] = p.inv_inertia[1] = p.inv_inertia[2] = r.real(0.0005f, 0.01f);
        drv::PhysicsObject* owner = nullptr;
        bool feedback = false;
        int32_t flags = r.chance(0.3) ? (r.chance(0.5) ? 0x80 : 0x10) : 0;
        if (i == 0 && (force_player || r.chance(0.8))) {
            auto c = std::make_unique<CkCar>();
            c->ordered = 0;
            c->d.type = 1;
            c->d.slot = 0;
            c->name_ = "vanquish";
            c->d.physics = nf::p32_u(&c->car_physics);
            c->feedback.port = r.chance(0.8) ? 0 : -1;
            feedback = c->feedback.port >= 0;
            c->d.feedback_object = feedback ? 1u : 0u;
            c->damage_flags = flags;
            sc.player = c.get();
            owner = c.get();
            sc.owned.push_back(std::move(c));
        } else if (p.type == 1) {
            auto c = std::make_unique<CkCar>();
            c->ordered = i;
            c->d.type = 1;
            c->d.slot = int16_t(i);
            c->name_ = "hatchback";
            c->d.physics = nf::p32_u(&c->car_physics);
            c->feedback.port = -1;
            c->damage_flags = flags;
            c->hp = r.chance(0.8) ? r.real(1, 700) : 0.0f;
            owner = c.get();
            sc.cars.push_back(i);
            sc.owned.push_back(std::move(c));
        } else {
            auto o = std::make_unique<CkRigid>();
            o->ordered = i;
            o->type = p.type;
            o->nm = "box";
            o->damage_flags = flags;
            owner = o.get();
            sc.owned.push_back(std::move(o));
        }
        drv::RigidBody* rb = drv::simulation_add_body(sim, p, owner);
        if (!rb) break;
        rb->state = int8_t(r.chance(0.1) ? 3 : (r.chance(0.2) ? 1 : 2));
        if (sc.player && owner == sc.player) sim.cars.push_back(sc.player);
        write_image(e, i, p.type, 0, int16_t(i), feedback, i == 0 && sc.player ? "vanquish" : "box");
        e.damage_flags[image_of(i)] = flags;
        if (CkCar* c = dynamic_cast<CkCar*>(owner)) e.hp[image_of(i)] = c->hp;
        ++sc.rigid;
    }
    if (!sc.player) sim.cars.clear();
    // Os corpos simples: personagens (tipo 4) e outros.
    const int nsimple = r.range(0, 7);
    for (int j = 0; j < nsimple; ++j) {
        auto o = std::make_unique<CkSimple>();
        const int slot = drv::AssignSimpleBodySlot(sim, o.get());
        if (slot < 0) break;
        o->ordered = drv::kRigidBodySlots + slot;
        const bool human = r.chance(0.5);
        o->po.type = human ? 4 : (r.chance(0.3) ? 11 : r.range(2, 9));
        o->po.flags = 1;
        o->po.slot = int16_t(slot);
        o->nm = "thing";
        o->damage_flags = r.chance(0.2) ? 0x80 : 0;
        float pos[3], vel[3], acc[3];
        for (int k = 0; k < 3; ++k) {
            pos[k] = at[k] + r.real(-spread, spread);
            vel[k] = r.real(-10, 10);
            acc[k] = 0;
        }
        pos[1] = at[1] + r.real(0.2f, 2.5f);
        float q[4], om[4][4];
        random_quat(r, q);
        drv::VU0_quattom4(q, om);
        drv::SimpleRigidBody* b = drv::GetSimpleRigidBody(sim, slot);
        drv::SimpleRigidBody_Construct(b, r.real(0.2f, 1.5f), r.real(0.5f, 90), slot, human ? 2 : 3, pos, vel, acc, om);
        b->flags = uint16_t(r.chance(0.15) ? 0x1 : (r.chance(0.5) ? 0x10 : 0x30));
        write_image(e, o->ordered, o->po.type, 1, int16_t(slot), false, o->nm);
        e.damage_flags[image_of(o->ordered)] = o->damage_flags;
        if (human) {
            // O ator (+0x70): as linhas de uma matriz (a orientação do personagem e o lugar dele), o ponto +0x40 (no
            // referencial do ator), a esfera esticada +0xa4 (às vezes nenhuma), a mira +0xc0 com o modo +0xcc; o +0x8c.
            const int hi = int(sc.humans.size());
            if (hi < 8) {
                const uint32_t act_e = kActors + 0x200u * uint32_t(hi);
                static uint8_t s_actors[8][0x200];
                static float s_shapes[8][8];
                uint8_t* act = s_actors[hi];
                std::memset(act, 0, 0x200);
                float mm[4][4];
                std::memcpy(mm, om, 64);
                mm[3][0] = pos[0];
                mm[3][1] = pos[1] - 1.0f;
                mm[3][2] = pos[2];
                mm[3][3] = 1.0f;
                std::memcpy(act, mm, 64);
                const float pt[4] = {r.real(-0.2f, 0.2f), r.real(0.5f, 1.5f), r.real(-0.2f, 0.2f), 1.0f};
                std::memcpy(act + 0x40, pt, 16);
                const bool shape = r.chance(0.7);
                uint32_t shape_e = 0, shape_p = 0;
                if (shape) {
                    float* sh = s_shapes[hi];
                    std::memset(sh, 0, 32);
                    sh[3] = r.real(0.2f, 0.6f);
                    sh[4] = r.real(-0.3f, 0.3f);
                    sh[5] = r.real(1.0f, 3.0f);
                    shape_e = kShapes + 0x20u * uint32_t(hi);
                    shape_p = nf::p32_u(sh);
                    put(e.m, shape_e, sh, 32);
                }
                const uint32_t mode = r.chance(0.3) ? 1u : 0u;
                const float aim[3] = {pos[0] + r.real(-5, 5), pos[1], pos[2] + r.real(-5, 5)};
                std::memcpy(act + 0xc0, aim, 12);
                std::memcpy(act + 0xcc, &mode, 4);
                std::memcpy(act + 0xa4, &shape_p, 4);
                // O +0x140 do ator: a tabela virtual falsa (o EFireWeapon chama a entrada +0x24).
                const uint32_t vta = kVtActor;
                uint8_t emu_act[0x200];
                std::memcpy(emu_act, act, 0x200);
                std::memcpy(emu_act + 0xa4, &shape_e, 4);
                std::memcpy(emu_act + 0x140, &vta, 4);
                put(e.m, act_e, emu_act, 0x200);
                const uint32_t through = r.chance(0.5) ? 1u : 0u;
                o->words[0x70] = r.chance(0.85) ? nf::p32_u(act) : 0u;
                o->words[0x8c] = through;
                e.m.write32(image_of(o->ordered) + 0x70, o->words[0x70] ? act_e : 0u);
                e.m.write32(image_of(o->ordered) + 0x8c, through);
                sc.humans.push_back(o->ordered);
            }
        }
        sc.owned.push_back(std::move(o));
        ++sc.simple;
    }
    // Os donos (+0x58, a corrente do PhysicsObject::IsOwnedBy): às vezes o jogador, às vezes outro corpo simples, às
    // vezes o próprio carro do jogador (o tiro "do jogador": a trapaça 0x1000 dobra o dano e explode).
    // Sem ciclos (o PS2 e o porte dariam voltas para sempre): quem tem dono não é dono de ninguém, e quem é dono de
    // alguém não ganha dono depois.
    std::vector<uint32_t> simple_sigs;
    for (int i = 0; i < drv::kSimpleBodySlots; ++i)
        if (sim.simple_owners[i]) simple_sigs.push_back(sim.simple_sig[i]);
    std::vector<uint32_t> owned_sigs, target_sigs;
    auto in = [](const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    const uint32_t player_sig = sc.player ? sim.rigid_sig[0] : 0u;
    for (auto& up : sc.owned) {
        drv::PhysicsObject* o = up.get();
        const int ord = [&] {
            for (int i = 0; i < drv::kRigidBodySlots; ++i)
                if (sim.owners[i] == o) return i;
            for (int i = 0; i < drv::kSimpleBodySlots; ++i)
                if (sim.simple_owners[i] == o) return drv::kRigidBodySlots + i;
            return -1;
        }();
        if (ord < 0) continue;
        const uint32_t own = ord < drv::kRigidBodySlots ? sim.rigid_sig[ord] : sim.simple_sig[ord - drv::kRigidBodySlots];
        uint32_t sig = 0;
        if (o == sc.player) {
            if (r.chance(0.3)) sig = player_sig;  // ele mesmo: a corrente para nele
        } else if (in(target_sigs, own)) {
            continue;
        } else if (player_sig && r.chance(0.3)) {
            sig = player_sig;
        } else if (!simple_sigs.empty() && r.chance(0.15)) {
            sig = simple_sigs[r.next() % simple_sigs.size()];
            if (sig == own || in(owned_sigs, sig)) sig = 0;
        }
        if (sig == 0) continue;
        if (o != sc.player) {
            owned_sigs.push_back(own);
            target_sigs.push_back(sig);
        }
        if (CkCar* c = dynamic_cast<CkCar*>(o)) c->d.owner_sig = sig;
        else if (CkRigid* k = dynamic_cast<CkRigid*>(o)) k->words[0x58] = sig;
        else if (CkSimple* s = dynamic_cast<CkSimple*>(o)) s->po.owner_sig = sig;
        e.m.write32(image_of(ord) + 0x58, sig);
    }
}

drv::PhysicsObject* port_owner(int ordered) {
    drv::Simulation& sim = drv::simulation();
    if (ordered < 0) return nullptr;
    if (ordered < drv::kRigidBodySlots) return sim.owners[ordered];
    return sim.simple_owners[ordered - drv::kRigidBodySlots];
}
int port_ordered(const drv::PhysicsObject* o) {
    drv::Simulation& sim = drv::simulation();
    if (!o) return -1;
    for (int i = 0; i < drv::kRigidBodySlots; ++i)
        if (sim.owners[i] == o) return i;
    for (int i = 0; i < drv::kSimpleBodySlots; ++i)
        if (sim.simple_owners[i] == o) return drv::kRigidBodySlots + i;
    return -3;
}

std::vector<uint32_t> emu_vector(Emu& e, uint32_t off) {
    std::vector<uint32_t> v;
    const uint32_t b = e.m.read32(kSim + off), en = e.m.read32(kSim + off + 4);
    for (uint32_t a = b; a && a < en; a += 4) v.push_back(e.m.read32(a));
    return v;
}

// Os sons criados.
std::vector<SoundRec> emu_sounds(Emu& e, size_t from) {
    std::vector<SoundRec> v;
    for (size_t i = from; i < e.sounds.size(); ++i) {
        const uint32_t a = e.sounds[i];
        SoundRec s;
        const uint32_t mix = e.m.read32(a + 0x84);
        s.mix = mix >= kMixBase && mix < kMixBase + kMixCount ? kMixNames[mix - kMixBase] : "?";
        for (int k = 0; k < 3; ++k) {
            s.pos[k] = e.m.read32(a + 4 * uint32_t(k));
            s.vel[k] = e.m.read32(a + 0x10 + 4 * uint32_t(k));
        }
        s.min = e.m.read32(a + 0x5c);
        s.max = e.m.read32(a + 0x60);
        s.max2 = e.m.read32(a + 0x64);
        s.volume = e.m.read32(a + 0x6c);
        s.pitch = e.m.read32(a + 0x70);
        s.fx = e.m.read32(a + 0x114);
        s.lifetime = e.m.read32(a + 0x118);
        v.push_back(s);
    }
    return v;
}
std::string mix_name(const drv::AMix* mix) {
    auto* mixes = drv::audio_internals().mixes;
    for (const auto& [n, p] : *mixes)
        if (p.get() == mix) return n;
    return "?";
}
std::vector<SoundRec> port_sounds(size_t before) {
    std::vector<SoundRec> v;
    std::list<drv::ABaseSound*>& l = drv::sound_list();
    const size_t n = l.size() - before;
    std::vector<drv::ABaseSound*> fresh;
    auto it = l.begin();
    for (size_t i = 0; i < n; ++i, ++it) fresh.push_back(*it);
    for (size_t i = fresh.size(); i-- > 0;) {
        drv::ABaseSound* b = fresh[i];
        SoundRec s;
        s.mix = mix_name(b->mix);
        drv::AOneShotSound* os = dynamic_cast<drv::AOneShotSound*>(b);
        for (int k = 0; k < 3; ++k) {
            s.pos[k] = ps2::fbits(b->pos[k]);
            s.vel[k] = ps2::fbits(b->vel[k]);
        }
        s.min = ps2::fbits(b->min_distance);
        s.max = ps2::fbits(b->max_distance);
        s.max2 = ps2::fbits(b->max_distance2);
        s.volume = ps2::fbits(b->volume);
        s.pitch = ps2::fbits(b->pitch);
        s.fx = os ? ps2::fbits(os->fx) : 0u;
        s.lifetime = os ? ps2::fbits(os->lifetime) : 0u;
        v.push_back(s);
    }
    return v;
}
std::string compare_sounds(const std::vector<SoundRec>& p, const std::vector<SoundRec>& em) {
    const size_t n = std::max(p.size(), em.size());
    for (size_t i = 0; i < n; ++i) {
        const bool hp = i < p.size(), he = i < em.size();
        if (hp && he && p[i] == em[i]) continue;
        return "o som " + std::to_string(i) + ": original " + (he ? em[i].text() : std::string("nada")) + ", porte " +
               (hp ? p[i].text() : std::string("nada"));
    }
    return "";
}
void drop_port_sounds(size_t before) {
    while (drv::sound_list().size() > before) drv::sound_list().front()->destroy();
}

// Os eventos criados (os ESpawnExplosion): a imagem do original x a do porte.
std::string compare_events(Emu& e, size_t from) {
    const auto& pend = drv::trigger_manager().pending;
    if (e.events.size() - from != pend.size())
        return "os eventos criados: " + std::to_string(e.events.size() - from) + " x " + std::to_string(pend.size());
    for (size_t k = 0; k < pend.size(); ++k) {
        const uint32_t a = e.events[from + k];
        for (uint32_t i = 0; i < 20; ++i) {
            uint32_t v = e.m.read32(a + 4 * i), want = pend[k].obj[i];
            if (i == 12) {
                v = v ? uint32_t(e.owner_of(v)) : 0xffffffffu;
                want = want ? uint32_t(port_ordered(nf::p32_ptr<drv::PhysicsObject>(want))) : 0xffffffffu;
            }
            if (v != want)
                return "o evento " + std::to_string(k) + " +0x" + hex(4 * i).substr(6) + ": " + hex(v) + " x " + hex(want);
        }
    }
    return "";
}

// ---------------------------------------------------------------- os globais das balas nos dois lados

void globals_to_emu(Emu& e) {
    const drv::RayShellGlobals& g = drv::rayshell_globals();
    MipsMachine& m = e.m;
    m.write32(kRayEnable, uint32_t(g.enable));
    m.write32(kNumTracers, uint32_t(g.num_tracers));
    put(m, kTracers, g.tracers, sizeof g.tracers);
    put(m, kTracerSide, g.tracer_side, sizeof g.tracer_side);
    m.write32(kNumFlashes, uint32_t(g.num_flashes));
    put(m, kFlashes, g.flashes, sizeof g.flashes);
    m.write32(kNumActive, uint32_t(g.num_active));
    put(m, kActive, g.active, sizeof g.active);
    m.write32(kTrigIndex, uint32_t(g.trigger_index));
    m.write32(kLastExplosion, uint32_t(g.last_explosion));
    const drv::ShellEffectsCounters& c = drv::shell_effects_counters();
    m.write32(kSparksFound, uint32_t(c.sparks_found));
    m.write32(kSparks, c.sparks);
    m.write32(kConcrete, c.concrete);
    m.write32(kRicochet, c.ricochet);
    const drv::FireSoundCounters& f = drv::fire_sound_counters();
    m.write32(kSndName, f.name);
    m.write32(kSndActor, f.actor);
    m.write32(kSndCar, f.car);
    m.write32(kRRandom, drv::render_random().seed);
    m.write32(kRRandom + 4, drv::render_random().multiplier);
    const drv::GaussState& gs = drv::gauss_state();
    m.write32(m.read32(kImpurePtr) + 0x58, gs.rand_next);
    m.write32(kGaussHave, uint32_t(gs.have));
    m.write32(kGaussSaved, ps2::fbits(gs.saved));
    m.write32(kUberCheat, drv::uber_bond().word(0x9a0));
}

// Os ponteiros do porte e os do original (o RSceneObj e o efeito do evento do EFireWeapon): o porte -> o original.
std::map<uint32_t, uint32_t> g_ptr_map;
uint32_t to_emu_ptr(uint32_t v) {
    auto it = g_ptr_map.find(v);
    return it == g_ptr_map.end() ? v : it->second;
}

bool compare_globals(Emu& e, std::string& why) {
    const drv::RayShellGlobals& g = drv::rayshell_globals();
    MipsMachine& m = e.m;
    auto word = [&](const char* what, uint32_t a, uint32_t want) {
        if (m.read32(a) == want) return true;
        why = std::string(what) + ": original " + hex(m.read32(a)) + ", porte " + hex(want);
        return false;
    };
    if (!word("fNumTracers", kNumTracers, uint32_t(g.num_tracers))) return false;
    const uint32_t nt = uint32_t(std::clamp(g.num_tracers, 0, 64));
    if (!same_bytes("os rastros", m.mem(kTracers, 0x20 * nt), reinterpret_cast<const uint8_t*>(g.tracers), 0x20 * nt,
                    why))
        return false;
    uint32_t side[64][2];
    for (int i = 0; i < 64; ++i)
        for (int k = 0; k < 2; ++k) side[i][k] = to_emu_ptr(g.tracer_side[i][k]);
    if (!same_bytes("os rastros (0x424180)", m.mem(kTracerSide, 8 * 64), reinterpret_cast<const uint8_t*>(side), 8 * 64,
                    why))
        return false;
    if (!word("fNumFlashes", kNumFlashes, uint32_t(g.num_flashes))) return false;
    if (!same_bytes("os clarões", m.mem(kFlashes, sizeof g.flashes), reinterpret_cast<const uint8_t*>(g.flashes),
                    sizeof g.flashes, why))
        return false;
    if (!word("fNumActiveRayShells", kNumActive, uint32_t(g.num_active))) return false;
    if (!same_bytes("os raios dos gatilhos", m.mem(kActive, sizeof g.active), reinterpret_cast<const uint8_t*>(g.active),
                    sizeof g.active, why))
        return false;
    if (!word("0x333560", kTrigIndex, uint32_t(g.trigger_index))) return false;
    if (!word("0x333564", kLastExplosion, uint32_t(g.last_explosion))) return false;
    const drv::ShellEffectsCounters& c = drv::shell_effects_counters();
    if (!word("0x333580", kSparksFound, uint32_t(c.sparks_found)) || !word("0x3d0d4c", kSparks, c.sparks) ||
        !word("0x333584", kConcrete, c.concrete) || !word("0x333588", kRicochet, c.ricochet))
        return false;
    const drv::FireSoundCounters& f = drv::fire_sound_counters();
    if (!word("0x32bba0", kSndName, f.name) || !word("0x32bba4", kSndActor, f.actor) ||
        !word("0x32bba8", kSndCar, f.car))
        return false;
    if (!word("o RRandom", kRRandom, drv::render_random().seed)) return false;
    const drv::GaussState& gs = drv::gauss_state();
    if (!word("o rand", m.read32(kImpurePtr) + 0x58, gs.rand_next) || !word("0x33a53c", kGaussHave, uint32_t(gs.have)))
        return false;
    if (gs.have && !word("0x417040", kGaussSaved, ps2::fbits(gs.saved))) return false;
    return true;
}

// Globais sorteados (os rastros, os clarões, os raios, os contadores).
void random_globals(Rng& r) {
    drv::RayShellGlobals& g = drv::rayshell_globals();
    drv::Simulation& sim = drv::simulation();
    g = drv::RayShellGlobals{};
    g.num_tracers = r.chance(0.1) ? 64 : r.range(0, 30);
    for (int i = 0; i < 64; ++i) {
        drv::RayTracer& t = g.tracers[i];
        for (int k = 0; k < 3; ++k) {
            t.from[k] = r.real(-100, 100);
            t.to[k] = r.real(-100, 100);
        }
        t.step = sim.steps - r.range(-5, 60);
        t.type = r.range(-1, 3);
        g.tracer_side[i][0] = r.next();
        g.tracer_side[i][1] = r.next();
    }
    g.num_flashes = r.chance(0.1) ? 8 : r.range(0, 7);
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 3; ++k) g.flashes[i].pos[k] = r.real(-100, 100);
        g.flashes[i].sig = r.chance(0.5) ? r.next() : 0u;
    }
    g.num_active = r.chance(0.05) ? 63 : r.range(0, 20);
    for (int i = 0; i < 64; ++i) {
        drv::ActiveRayShell& a = g.active[i];
        a.sig = r.next();
        for (int k = 0; k < 3; ++k) {
            a.start[k] = r.real(-100, 100);
            a.end[k] = r.real(-100, 100);
        }
        a.damage = r.real(0, 100);
        a.half = r.real(0, 100);
    }
    g.trigger_index = r.chance(0.3) ? -1 : r.range(0, 63);
    g.last_explosion = sim.steps - r.range(-2, 40);
    drv::ShellEffectsCounters& c = drv::shell_effects_counters();
    c.concrete = r.next() & 7;
    c.ricochet = uint32_t(r.range(0, 2));
    c.sparks_found = r.chance(0.5) ? 1 : 0;
    c.sparks = c.sparks_found && r.chance(0.7) ? 0x5e5e0000u : 0u;
    drv::FireSoundCounters& f = drv::fire_sound_counters();
    f.name = r.next() & 3;
    f.actor = r.next() & 1;
    f.car = r.next() & 1;
    drv::RRandom_StartUp(drv::render_random(), r.next());
    drv::GaussState& gs = drv::gauss_state();
    gs.rand_next = r.next();
    gs.have = r.chance(0.3) ? 1 : 0;
    gs.saved = r.real(-2, 2);
    drv::uber_bond().word(0x9a0) = r.chance(0.3) ? 0x1000u : 0u;
}

// ---------------------------------------------------------------- o mundo de uma pista

struct WorldCtx {
    drv::Track track;
    drv::TrackWorld tw;
    WorldCopy wc;
    std::vector<std::array<float, 3>> spots;
    bool ok = false;
};

std::vector<uint32_t> g_scene_objs;  // os objetos de cena do porte (o +0x28: o Handle 0x80000000 | a instância)

bool load_world(Emu& e, const nf::DiscSource& disc, const char* name, WorldCtx& w, Rng& r) {
    std::string err;
    if (!w.track.load(disc, name, "", err) || !w.tw.init(w.track.root(), &err)) {
        std::printf("  %s não carregou (%s)\n", name, err.c_str());
        return false;
    }
    std::memset(e.m.mem(kSceneTable, kHeap - kSceneTable), 0, kHeap - kSceneTable);
    if (!copy_world(e.m, w.tw, w.wc, kMgr, kGrid, kWorldBegin, kWorldEnd, err)) {
        std::printf("  %s: %s\n", name, err.c_str());
        return false;
    }
    const uint32_t nri = w.tw.render_instance_count();
    if (kSceneTable + 32 * nri > kSceneObjs || kSceneObjs + 64 * nri > kHeap) {
        std::printf("  %s: as instâncias de render não cabem\n", name);
        return false;
    }
    e.m.write32(kWorldFake + 0x20, w.wc.render);
    e.m.write32(kWorldFake + 0x24, kSceneTable);
    e.m.write32(kWorldFake + 0x28, nri);
    e.m.write32(0x33aac8, kWorldFake);
    g_scene_objs.assign(16u * nri, 0);
    for (uint32_t i = 0; i < nri; ++i) {
        g_scene_objs[16u * i + 10] = 0x80000000u | i;
        const uint8_t* ri = w.tw.render_instances() + 64 * i;
        if (!(ri[0x0c] & 0x10)) continue;
        uint16_t idx;
        std::memcpy(&idx, ri + 0x0e, 2);
        const uint32_t obj = kSceneObjs + 64 * i;
        e.m.write32(kSceneTable + 32u * idx + 8, obj);
        e.m.write32(obj + 0x28, 0x80000000u | i);
        e.m.write32(obj + 0x3c, kVtRender);
    }
    drv::set_world(&w.tw);
    for (int tries = 0; tries < 40000 && w.spots.size() < 600; ++tries) {
        const drv::WCollisionInstance& ci = w.tw.instances()[r.next() % w.tw.instance_count()];
        float c[3];
        for (int k = 0; k < 3; ++k)
            c[k] = -(ci.position[0] * ci.right[k] + ci.position[1] * (k == 1 ? 1.0f : 0.0f) +
                     ci.position[2] * ci.forward[k]);
        float p[3] = {c[0] + r.real(-20, 20), c[1] + 30.0f, c[2] + r.real(-20, 20)};
        float h;
        if (w.tw.world_height_at_point(p, h) && h > c[1] - 40 && h < c[1] + 40) w.spots.push_back({p[0], h, p[2]});
    }
    w.ok = !w.spots.empty();
    std::printf("  %s: %zu pontos com chão, %.1f MB copiados\n", name, w.spots.size(), double(w.wc.bytes) / 1048576.0);
    return w.ok;
}

// O mapa das janelas: a raiz nova no heap emulado (um map vazio do STL do GCC 2.95), o do porte vazio.
void reset_window_map(Emu& e) {
    const uint32_t h = e.alloc(0x40);
    e.m.write32(h + 0x0, 0);
    e.m.write32(h + 0x4, 0);
    e.m.write32(h + 0x8, h);
    e.m.write32(h + 0xc, h);
    e.m.write32(kMgr + 0x14, h);
    e.m.write32(kMgr + 0x18, 0);
    drv::window_map().clear();
    drv::hit_window_events().clear();
}

// O WorldCollisionInfo do original x o do porte.
std::string compare_info(Emu& e, uint32_t emu, const drv::WorldCollisionInfo& p, const WorldCtx& w) {
    uint8_t raw[0x60];
    std::memcpy(raw, e.m.mem(emu, 0x60), 0x60);
    drv::WorldCollisionInfo q;
    std::memcpy(&q, raw, 0x60);
    auto inst = [&](uint32_t a) { return a ? nf::p32_u(w.tw.instances()) + (a - w.wc.insts) : 0u; };
    q.barrier.instance = inst(q.barrier.instance);
    q.face_instance = inst(q.face_instance);
    if (q.object) q.object = nf::p32_u(w.tw.objects()) + (q.object - w.wc.objs);
    drv::WorldCollisionInfo pp = p;
    q.unknown4c = pp.unknown4c = 0;
    q.unknown56 = pp.unknown56 = 0;
    q.unknown5c = pp.unknown5c = 0;
    std::string why;
    same_bytes("o WorldCollisionInfo", reinterpret_cast<const uint8_t*>(&q), reinterpret_cast<const uint8_t*>(&pp), 0x60,
               why);
    return why;
}

// O ambiente do porte nos testes (os objetos de cena do mundo falso, os estímulos anotados, o sistema das faíscas).
uint32_t g_spark_answer = 0;
void port_env() {
    drv::ShellsEnv& s = drv::shells_env();
    s.scene_obj_of = [](uint32_t render) {
        const uint32_t i = (render - nf::p32_u(drv::world()->track_world()->render_instances())) / 64u;
        return nf::p32_u(&g_scene_objs[16u * i]);
    };
    s.stimuli = [](uint32_t, uint32_t, uint32_t, uint32_t) {};
    s.spark_system = []() { return g_spark_answer; };
}

// ---------------------------------------------------------------- a) as utilidades

void check_utils(Emu& e, Rng& r, int cases, int& failures) {
    Tally t_sphere, t_stretch, t_unit, t_perturb, t_gauss, t_boost, t_log;
    constexpr uint32_t kA = kEvent, kB = kEvent + 0x10, kC = kEvent + 0x20, kO = kEvent + 0x30;
    for (int k = 0; k < cases; ++k) {
        // Util_CollideRayWithSphere e a esticada.
        alignas(16) float o[4] = {r.real(-20, 20), r.real(-5, 5), r.real(-20, 20), r.real(0, 1)};
        alignas(16) float d[4] = {r.real(-1, 1), r.real(-0.5f, 0.5f), r.real(-1, 1), r.real(0, 1)};
        alignas(16) float c[4];
        const float t = r.real(-5, 120);
        for (int i = 0; i < 3; ++i) c[i] = o[i] + d[i] * t + r.real(-2, 2);
        c[3] = r.real(0, 1);
        if (r.chance(0.1)) std::memcpy(c, o, 16);
        if (r.chance(0.05)) d[0] = d[1] = d[2] = 0;
        const float radius = r.real(0.05f, 3), stretch = r.real(0.5f, 3);
        put(e.m, kA, o, 16);
        put(e.m, kB, d, 16);
        put(e.m, kC, c, 16);
        alignas(16) float zero[4] = {};
        put(e.m, kO, zero, 16);
        ++t_sphere.cases;
        e.m.set_fpr(12, radius);
        e.call(0x211098, {kA, kB, kC, kO});
        const uint32_t ve = uint32_t(e.m.gpr(R::v0));
        float out[3] = {};
        const bool vp = drv::Util_CollideRayWithSphere(radius, o, d, c, out);
        std::string why;
        if ((ve != 0) != vp) t_sphere.fail("o resultado");
        else if (vp && !same_bytes("o ponto", e.m.mem(kO, 12), reinterpret_cast<const uint8_t*>(out), 12, why))
            t_sphere.fail(why);
        put(e.m, kO, zero, 16);
        ++t_stretch.cases;
        e.m.set_fpr(12, radius);
        e.m.set_fpr(13, stretch);
        e.call(0x211280, {kA, kB, kC, kO});
        const uint32_t se = uint32_t(e.m.gpr(R::v0));
        float out2[3] = {};
        const bool sp = drv::Util_CollideRayWithStretchedSphere(radius, stretch, o, d, c, out2);
        if ((se != 0) != sp) t_stretch.fail("o resultado");
        else if (sp && !same_bytes("o ponto", e.m.mem(kO, 12), reinterpret_cast<const uint8_t*>(out2), 12, why))
            t_stretch.fail(why);
        // Util_GenerateMatrixUnit (os w das linhas 0 e 1 são o lixo da pilha: fora).
        ++t_unit.cases;
        constexpr uint32_t kM = kEvent + 0x100;
        e.call(0x2106c0, {kM, kB});
        float mp[4][4];
        drv::Util_GenerateMatrixUnit(mp, d);
        uint8_t me[64];
        std::memcpy(me, e.m.mem(kM, 64), 64);
        std::memset(me + 12, 0, 4);
        std::memset(me + 28, 0, 4);
        if (!same_bytes("a matriz", me, reinterpret_cast<const uint8_t*>(mp), 64, why)) t_unit.fail(why);
        // Util_PerturbVector(float, float, COORD3 &).
        ++t_perturb.cases;
        const float yaw = r.real(-0.2f, 0.2f), pitch = r.real(-0.2f, 0.2f);
        e.m.set_fpr(12, yaw);
        e.m.set_fpr(13, pitch);
        e.call(0x210b60, {kO, kB});
        float pv[3];
        drv::Util_PerturbVector(yaw, pitch, pv, d);
        if (!same_bytes("o vetor", e.m.mem(kO, 12), reinterpret_cast<const uint8_t*>(pv), 12, why)) t_perturb.fail(why);
        // Util_PerturbVector(COORD3 &, float) com o Util_GaussRandom (o rand e o log).
        ++t_gauss.cases;
        drv::GaussState& gs = drv::gauss_state();
        gs.rand_next = r.next();
        gs.have = r.chance(0.3) ? 1 : 0;
        gs.saved = r.real(-2, 2);
        e.m.write32(e.m.read32(kImpurePtr) + 0x58, gs.rand_next);
        e.m.write32(kGaussHave, uint32_t(gs.have));
        e.m.write32(kGaussSaved, ps2::fbits(gs.saved));
        const float amount = r.real(0, 0.1f);
        e.m.set_fpr(12, amount);
        e.call(0x210c10, {kO, kB});
        drv::Util_PerturbVectorGauss(amount, pv, d);
        if (!same_bytes("o vetor", e.m.mem(kO, 12), reinterpret_cast<const uint8_t*>(pv), 12, why)) t_gauss.fail(why);
        else if (e.m.read32(e.m.read32(kImpurePtr) + 0x58) != gs.rand_next || e.m.read32(kGaussHave) != uint32_t(gs.have))
            t_gauss.fail("o estado do rand");
        else if (gs.have && e.m.read32(kGaussSaved) != ps2::fbits(gs.saved))
            t_gauss.fail("o segundo número guardado");
        // O log da newlib sozinho (a dupla da libgcc).
        ++t_log.cases;
        double x = double(r.real(1e-6f, 1.0f));
        if (r.chance(0.2)) x = double(r.real(1.0f, 1e6f));
        if (r.chance(0.05)) x = 1.0;
        uint64_t xb;
        std::memcpy(&xb, &x, 8);
        e.m.set_gpr(R::a0, xb);
        e.call(0x2c1330, {});
        const uint64_t le = e.m.gpr(R::v0);
        const double lp = drv::ee_log(x);
        uint64_t lb;
        std::memcpy(&lb, &lp, 8);
        if (le != lb) t_log.fail("log(" + std::to_string(x) + "): " + hex(uint32_t(le >> 32)) + hex(uint32_t(le)) +
                                 " x " + hex(uint32_t(lb >> 32)) + hex(uint32_t(lb)));
        // SMissionManager::GetEnemyAccuracyBoost.
        ++t_boost.cases;
        drv::SMissionManagerData& mm = drv::mission_manager();
        static uint8_t s_settings[0x2f8];
        std::memset(s_settings, 0, sizeof s_settings);
        for (int i = 0; i < 3; ++i) {
            const int32_t v = r.range(-50, 50);
            std::memcpy(s_settings + 0x2ac + 4 * i, &v, 4);
        }
        mm.settings = nf::p32_u(s_settings);
        mm.difficulty = r.range(0, 3);
        put(e.m, kSettings, s_settings, sizeof s_settings);
        e.m.write32(kMission + 0x3b8, uint32_t(mm.difficulty));
        e.call(0x208428, {kMission});
        if (uint32_t(e.m.gpr(R::v0)) != uint32_t(drv::SMissionManager_GetEnemyAccuracyBoost()))
            t_boost.fail("o acréscimo");
    }
    report("Util_CollideRayWithSphere", t_sphere, failures);
    report("Util_CollideRayWithStretchedSphere", t_stretch, failures);
    report("Util_GenerateMatrixUnit", t_unit, failures);
    report("Util_PerturbVector(float, float, COORD3 &)", t_perturb, failures);
    report("Util_PerturbVector(COORD3 &, float) (Util_GaussRandom)", t_gauss, failures);
    report("__ieee754_log (a dupla da libgcc)", t_log, failures);
    report("SMissionManager::GetEnemyAccuracyBoost", t_boost, failures);
}

// ---------------------------------------------------------------- b) os globais das balas

void check_globals_fns(Emu& e, Rng& r, int cases, int& failures) {
    Tally t[8];
    const char* names[8] = {"RayShell::StaticStoreTracer", "RayShell::DeleteTracer", "RayShell::ClearTracers",
                            "RayShell::Reset", "RayShell::AddFlash", "RayShell::AddNewRayShellForTriggerCheck",
                            "RayShell::GetTracer/GetNumTracers/GetActiveRayShell", "RayShell::Clear*"};
    static const float kAt[3] = {0, 0, 0};
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        random_scene(r, e, sc, kAt, 10.0f);
        random_globals(r);
        sim_to_emu(e, sc);
        globals_to_emu(e);
        drv::Simulation& sim = drv::simulation();
        const int which = r.range(0, 7);
        std::string why;
        ++t[which].cases;
        alignas(16) float a[4] = {r.real(-50, 50), r.real(-5, 5), r.real(-50, 50), 1.0f};
        alignas(16) float b[4] = {a[0] + r.real(-3, 3), a[1] + r.real(-1, 1), a[2] + r.real(-3, 3), 1.0f};
        put(e.m, kEvent, a, 16);
        put(e.m, kEvent + 0x10, b, 16);
        switch (which) {
            case 0: {
                const int32_t type = r.range(-1, 3);
                const uint32_t s1 = r.next(), s2 = r.next();
                e.call(0x19a9b8, {kEvent, kEvent + 0x10, uint32_t(type), s1, s2});
                drv::RayShell_StaticStoreTracer(a, b, type, s1, s2);
                break;
            }
            case 1: {
                const int32_t i = r.range(0, std::max(0, drv::rayshell_globals().num_tracers - 1));
                e.call(0x1992d8, {uint32_t(i)});
                drv::RayShell_DeleteTracer(i);
                break;
            }
            case 2: {
                const uint32_t step = uint32_t(sim.steps + r.range(-30, 30));
                e.call(0x199358, {step});
                drv::RayShell_ClearTracers(step);
                break;
            }
            case 3:
                e.call(0x199780, {});
                drv::RayShell_Reset();
                break;
            case 4: {
                // A assinatura de um corpo (um personagem, uma sentinela, o jogador) ou nenhuma.
                uint32_t sig = 0;
                const double p = double(r.next() & 0xffff) / 65536.0;
                if (p < 0.5 && sc.simple) {
                    for (int i = 0; i < drv::kSimpleBodySlots; ++i)
                        if (sim.simple_owners[i] && r.chance(0.5)) sig = sim.simple_sig[i];
                } else if (p < 0.75 && sc.rigid) {
                    sig = sim.rigid_sig[r.range(0, sc.rigid - 1)];
                }
                if (r.chance(0.2) && drv::rayshell_globals().num_flashes) sig = drv::rayshell_globals().flashes[0].sig;
                e.m.write32(kEvent + 0x20, sig);
                e.call(0x19abb8, {kEvent + 0x40, kEvent + 0x20, kEvent});
                drv::RayShell_AddFlash(sig, a);
                break;
            }
            case 5: {
                alignas(16) drv::RayShellData rs{};
                for (int i = 0; i < 3; ++i) {
                    rs.pos[i] = a[i];
                    rs.dir[i] = r.real(-1, 1);
                }
                rs.pos[3] = r.real(0, 1);
                rs.dir[3] = r.real(0, 1);
                rs.sig = r.next();
                rs.damage = r.real(0, 300);
                put(e.m, kEvent + 0x100, &rs, sizeof rs);
                const float dist = r.real(0, 200);
                e.m.set_fpr(12, dist);
                e.call(0x1997e0, {kEvent + 0x100});
                drv::RayShell_AddNewRayShellForTriggerCheck(rs, dist);
                break;
            }
            case 6: {
                const int32_t i = r.range(-1, 63);
                e.call(0x199268, {uint32_t(i)});
                drv::RayShell_GetNumTracers(i);
                e.call(0x199278, {});
                const uint32_t te = uint32_t(e.m.gpr(R::v0));
                const drv::ActiveRayShell* tp = drv::RayShell_GetTracer();
                const uint32_t want = tp ? kActive + 0x30u * uint32_t(tp - drv::rayshell_globals().active) : 0u;
                if (te != want) why = "GetTracer: " + hex(te) + " x " + hex(want);
                e.call(0x199230, {});
                if (uint32_t(e.m.gpr(R::v0)) != uint32_t(drv::RayShell_GetNumActiveRayShells()))
                    why = "GetNumActiveRayShells";
                break;
            }
            default: {
                const int c = r.range(0, 1);
                e.call(c ? 0x199258u : 0x199668u, {});
                if (c) drv::RayShell_ClearActiveRayShells();
                else drv::RayShell_ClearFlashes();
                break;
            }
        }
        if (why.empty()) compare_globals(e, why);
        if (!why.empty()) t[which].fail(why);
    }
    for (int i = 0; i < 8; ++i) report(names[i], t[i], failures);
}

// ---------------------------------------------------------------- c) o RayShell

// Um tiro sorteado: quem atira (o jogador, outro corpo, um personagem, nenhum), a arma, o alcance, o dano, o lugar perto
// da cena e a direção para um dos corpos (ou sorteada).
struct alignas(16) Shot {
    float pos[4], dir[4], muzzle[4];
    float range, damage, perturb;
    int32_t weapon, rays, streak;
    uint32_t sig, scene, effect;
    uint32_t pad[3];
};

Shot random_shot(Rng& r, const Scene& sc, const float at[3]) {
    drv::Simulation& sim = drv::simulation();
    Shot s{};
    static const int32_t kW[] = {0, 0, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x10, 2};
    s.weapon = kW[r.next() % (sizeof kW / sizeof kW[0])];
    s.range = r.chance(0.5) ? 220.0f : r.real(5, 400);
    s.damage = r.real(1, 600);
    s.rays = r.chance(0.85) ? 1 : r.range(2, 4);
    s.perturb = r.real(0, 0.05f);
    s.streak = r.range(-1, 3);
    s.scene = r.chance(0.3) ? r.next() : 0u;
    s.effect = r.chance(0.3) ? r.next() : 0u;
    const double p = double(r.next() & 0xffff) / 65536.0;
    if (p < 0.4 && sc.player) s.sig = sim.rigid_sig[0];
    else if (p < 0.55 && sc.rigid > 1) s.sig = sim.rigid_sig[r.range(1, sc.rigid - 1)];
    else if (p < 0.85 && sc.simple) {
        for (int i = 0; i < drv::kSimpleBodySlots; ++i)
            if (sim.simple_owners[i] && (r.chance(0.5) || s.sig == 0)) s.sig = sim.simple_sig[i];
    }
    for (int k = 0; k < 3; ++k) s.pos[k] = at[k] + r.real(-6, 6);
    s.pos[1] = at[1] + r.real(0.5f, 3);
    s.pos[3] = 1.0f;
    // A direção: para um corpo da cena (o centro mais um erro) ou sorteada.
    const float* aim = nullptr;
    if (r.chance(0.7)) {
        const int n = sc.rigid + sc.simple;
        if (n) {
            const int pick = r.range(0, n - 1);
            int seen = 0;
            for (int i = 0; i < drv::kRigidBodySlots + drv::kSimpleBodySlots && !aim; ++i) {
                drv::PhysicsObject* o = port_owner(i);
                if (!o) continue;
                if (seen++ == pick) aim = drv::PhysicsObject_GetPosition(o);
            }
        }
    }
    if (aim) {
        for (int k = 0; k < 3; ++k) s.dir[k] = aim[k] - s.pos[k] + r.real(-0.5f, 0.5f);
    } else {
        s.dir[0] = r.real(-1, 1);
        s.dir[1] = r.real(-0.6f, 0.2f);
        s.dir[2] = r.real(-1, 1);
    }
    s.dir[3] = r.real(0, 1);
    std::memcpy(s.muzzle, s.pos, 16);
    if (r.chance(0.3))
        for (int k = 0; k < 3; ++k) s.muzzle[k] += r.real(-1, 1);
    return s;
}

void check_rayshell(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t;
    uint64_t hits = 0, world = 0, calls = 0, sounds = 0, windows = 0, events = 0;
    const drv::ShellsStats s0 = drv::shells_stats();
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        reset_window_map(e);
        Scene sc;
        const auto& sp = w.spots[r.next() % w.spots.size()];
        const float at[3] = {sp[0], sp[1], sp[2]};
        random_scene(r, e, sc, at, 8.0f, true);
        random_globals(r);
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        g_spark_answer = r.chance(0.5) ? 0x5e5e0000u : 0u;
        e.spark_answer = g_spark_answer;
        e.gfx_answer = r.chance(0.8) ? 0x00c0ffeeu : 0u;
        static uint32_t s_gfx = 0;
        s_gfx = e.gfx_answer;
        drv::effects_env().gfx_lookup = [](const char*) { return s_gfx; };
        e.phys_sub = r.chance(0.2) ? 1u : 0u;
        if (sc.player) sc.player->car_physics.sub_physics = int32_t(e.phys_sub);
        sim_to_emu(e, sc);
        globals_to_emu(e);
        const Shot s = random_shot(r, sc, at);
        put(e.m, kEvent, s.pos, 16);
        put(e.m, kEvent + 0x10, s.dir, 16);
        put(e.m, kEvent + 0x20, s.muzzle, 16);
        e.m.write32(kEvent + 0x30, s.sig);
        // O construtor no original: a memória do FastAlloc (zerada), os argumentos da pilha (os raios, o rastro).
        const uint32_t obj = e.alloc(0xd0);
        e.calls.clear();
        e.notes.clear();
        e.events.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        drv::trigger_manager().pending.clear();
        const size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
        ++t.cases;
        e.m.set_fpr(12, s.range);
        e.m.set_fpr(13, s.damage);
        e.m.set_fpr(14, s.perturb);
        const uint32_t sp_args = MipsMachine::kStackTop - 0x100;
        e.m.write32(sp_args + 0, uint32_t(s.rays));
        e.m.write32(sp_args + 8, uint32_t(s.streak));
        e.m.set_gpr(R::sp, sp_args);
        try {
            e.m.call(kFnRayShell, {obj, uint32_t(s.weapon), kEvent + 0x30, kEvent, kEvent + 0x10, kEvent + 0x20,
                                   s.scene, s.effect},
                     50'000'000);
        } catch (const std::exception& ex) {
            if (e.errors++ == 0) e.error = ex.what();
            e.note(0xeeeeeeeeu);
        }
        alignas(16) drv::RayShellData rs{};
        drv::RayShell_Construct(rs, s.range, s.damage, s.perturb, s.weapon, s.sig, s.pos, s.dir, s.muzzle, s.scene,
                                s.effect, s.rays, s.streak);
        std::string why;
        if (!same_bytes("o RayShell", e.m.mem(obj, 0xd0), reinterpret_cast<const uint8_t*>(&rs), 0xd0, why)) {
        } else if (!(why = compare_calls(g_port_calls, e.calls)).empty()) {
        } else if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
        } else if (!(why = compare_sounds(port_sounds(snd_p), emu_sounds(e, snd_e))).empty()) {
        } else if (!(why = compare_events(e, 0)).empty()) {
        } else if (!compare_globals(e, why)) {
        } else if (!compare_sim(e, why)) {
        }
        if (!why.empty()) t.fail(why);
        calls += g_port_calls.size();
        sounds += drv::sound_list().size() - snd_p;
        events += drv::trigger_manager().pending.size();
        drop_port_sounds(snd_p);
        drv::trigger_manager().pending.clear();
    }
    const drv::ShellsStats& s1 = drv::shells_stats();
    hits = s1.fired - s0.fired;
    world = s1.world_hits - s0.world_hits;
    windows = s1.window_events - s0.window_events;
    report("RayShell::RayShell (o tiro inteiro)", t, failures);
    std::printf("    (%" PRIu64 " acertos em corpos, %" PRIu64 " no mundo, %" PRIu64 " chamadas aos donos, %" PRIu64
                " sons, %" PRIu64 " janelas, %" PRIu64 " explosões)\n",
                hits, world, calls, sounds, windows, events);
}

// ---------------------------------------------------------------- d) o Shell

// A imagem do Shell do original x o do porte: as palavras do PhysicsObject e o ShellData.
bool compare_shell(Emu& e, uint32_t a, drv::Shell* s, std::string& why) {
    const drv::PhysicsObjectWords& po = s->po;
    const std::pair<uint32_t, uint32_t> words[] = {{0x40, uint32_t(po.type)},
                                                   {0x44, uint32_t(po.flags) | uint32_t(uint16_t(po.slot)) << 16},
                                                   {0x58, po.owner_sig},
                                                   {0x68, po.vtable}};
    for (const auto& [off, want] : words) {
        if (e.m.read32(a + off) != want) {
            why = "o Shell +0x" + hex(off).substr(6) + ": original " + hex(e.m.read32(a + off)) + ", porte " + hex(want);
            return false;
        }
    }
    return same_bytes("o ShellData (+0x6c)", e.m.mem(a + 0x6c, 0x24), reinterpret_cast<const uint8_t*>(&s->d), 0x24,
                      why);
}

int32_t g_streak_speed_type = -1;
float g_streak_speed = 0;
bool port_streak_speed(int32_t type, float& v) {
    (void)type;
    v = g_streak_speed;
    return true;
}

void check_shell(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t_spawn, t_step;
    uint64_t steps = 0, deleted = 0, hits_body = 0, hits_world = 0, calls = 0;
    drv::ShellsEnv& senv = drv::shells_env();
    senv.streak_speed = port_streak_speed;
    const drv::ShellsStats s0 = drv::shells_stats();
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        reset_window_map(e);
        Scene sc;
        const auto& sp = w.spots[r.next() % w.spots.size()];
        const float at[3] = {sp[0], sp[1], sp[2]};
        random_scene(r, e, sc, at, 10.0f, true);
        random_globals(r);
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        g_spark_answer = r.chance(0.5) ? 0x5e5e0000u : 0u;
        e.spark_answer = g_spark_answer;
        sim_to_emu(e, sc);
        globals_to_emu(e);
        const Shot s = random_shot(r, sc, at);
        // A velocidade do tipo de rastro (a tabela do RBulletStreak: 0x423f38 +0x10).
        g_streak_speed = r.real(50, 400);
        if (s.streak >= 0) e.m.write32(0x423f38 + 0x18u * uint32_t(s.streak) + 0x10, ps2::fbits(g_streak_speed));
        put(e.m, kEvent, s.pos, 16);
        put(e.m, kEvent + 0x10, s.dir, 16);
        e.m.write32(kEvent + 0x30, s.sig);
        const float expl = r.chance(0.4) ? r.real(0, 200) : 0.0f;
        const int32_t weapon = r.chance(0.5) ? 0x10 : (r.chance(0.5) ? 0 : 0x17);
        e.calls.clear();
        e.notes.clear();
        e.events.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        drv::trigger_manager().pending.clear();
        ++t_spawn.cases;
        e.m.set_fpr(12, s.damage);
        e.m.set_fpr(13, expl);
        e.call(kFnSpawnShell, {kSim, uint32_t(weapon), kEvent + 0x10, kEvent, kEvent + 0x30, uint32_t(s.streak)});
        const uint32_t ea = uint32_t(e.m.gpr(R::v0));
        drv::Simulation& sim = drv::simulation();
        drv::Shell* ps = drv::SpawnShellObject(sim, s.damage, expl, weapon, s.dir, s.pos, s.sig, s.streak);
        std::string why;
        if ((ea != 0) != (ps != nullptr)) {
            t_spawn.fail("só um lado criou o Shell");
            continue;
        }
        if (!ps) continue;
        sc.owned.emplace_back(ps);
        e.owner_index[ea] = drv::kRigidBodySlots + ps->po.slot;
        if (!compare_shell(e, ea, ps, why) || !compare_sim(e, why) || !compare_globals(e, why)) {
            t_spawn.fail(why);
            continue;
        }
        const std::vector<uint32_t> ve = emu_vector(e, 0xe20);
        if (ve.size() != sim.shells.size()) {
            t_spawn.fail("o vetor dos Shell (+0xe20)");
            continue;
        }
        for (int st = 0; st < 120; ++st) {
            ++t_step.cases;
            ++steps;
            e.notes.clear();
            e.calls.clear();
            g_port_calls.clear();
            drv::effects_trace().clear();
            drv::trigger_manager().pending.clear();
            e.events.clear();
            const size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
            for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
                if (!sim.simple_owners[i]) continue;
                e.call(kFnSrbUpdate, {kSrbVec + 0x40u * uint32_t(i)});
                drv::SimpleRigidBody_UpdatePosition(drv::GetSimpleRigidBody(sim, i));
            }
            e.call(kFnDetect, {kSim});
            drv::DetectCollisionsSRB(sim);
            const bool alive = std::find(sim.shells.begin(), sim.shells.end(), ps) != sim.shells.end();
            if (!alive) break;
            e.call(kFnShellSimulate, {ea});
            drv::Shell_Simulate(*ps);
            ++sim.steps;
            e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
            std::string w2;
            if (!compare_sim(e, w2)) {
            } else if (!(w2 = compare_calls(g_port_calls, e.calls)).empty()) {
            } else if (!(w2 = compare_notes(drv::effects_trace(), e.notes)).empty()) {
            } else if (!(w2 = compare_sounds(port_sounds(snd_p), emu_sounds(e, snd_e))).empty()) {
            } else if (!(w2 = compare_events(e, 0)).empty()) {
            } else if (!compare_globals(e, w2) || !compare_shell(e, ea, ps, w2)) {
            } else if (emu_vector(e, 0xe20).size() != sim.shells.size()) {
                w2 = "o vetor dos Shell (+0xe20)";
            }
            calls += g_port_calls.size();
            drop_port_sounds(snd_p);
            if (!w2.empty()) {
                t_step.fail("passo " + std::to_string(st) + ": " + w2);
                break;
            }
            if (sim.shells.empty()) {
                ++deleted;
                break;
            }
        }
        drv::trigger_manager().pending.clear();
    }
    const drv::ShellsStats& s1 = drv::shells_stats();
    hits_body = s1.shell_hits - s0.shell_hits;
    hits_world = s1.windows_checked - s0.windows_checked;
    senv.streak_speed = nullptr;
    report("Simulation::SpawnShellObject (Shell::Shell)", t_spawn, failures);
    report("Shell::Simulate (com o DetectCollisionsSRB)", t_step, failures);
    std::printf("    (%" PRIu64 " passos, %" PRIu64 " apagados, %" PRIu64 " em corpos, %" PRIu64 " no mundo, %" PRIu64
                " chamadas aos donos)\n",
                steps, deleted, hits_body, hits_world, calls);
}

// ---------------------------------------------------------------- e) ApplyWorldEffects e ApplyWorldDamage

void check_world_effects(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t_fx, t_dmg;
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        const auto& sp = w.spots[r.next() % w.spots.size()];
        const float at[3] = {sp[0], sp[1], sp[2]};
        random_scene(r, e, sc, at, 8.0f, true);
        random_globals(r);
        g_spark_answer = r.chance(0.5) ? 0x5e5e0000u : 0u;
        e.spark_answer = g_spark_answer;
        e.phys_sub = r.chance(0.3) ? 1u : 0u;
        if (sc.player) sc.player->car_physics.sub_physics = int32_t(e.phys_sub);
        sim_to_emu(e, sc);
        globals_to_emu(e);
        alignas(16) float pos[4] = {at[0] + r.real(-5, 5), at[1] + r.real(0, 3), at[2] + r.real(-5, 5), r.real(0, 1)};
        alignas(16) float nrm[4] = {r.real(-1, 1), r.real(-1, 1), r.real(-1, 1), r.chance(0.5) ? 0.0f : 1.0f};
        if (r.chance(0.3)) {
            nrm[0] = nrm[2] = 0;
            nrm[1] = 1;
        }
        static const uint8_t kSurf[] = {0, 5, 6, 7, 8, 9, 0xc, 0xd, 0x10, 0x15, 0x20, 0x30, 0x40, 0x50, 0x70, 0x80, 0x90, 0x9d};
        const uint8_t surface[2] = {kSurf[r.next() % sizeof kSurf], uint8_t(r.next() & 3)};
        static const int32_t kW[] = {0, 0x17, 0x1a, 0x1b, 0x1d, 0x10, 2};
        const int32_t weapon = kW[r.next() % (sizeof kW / sizeof kW[0])];
        const int32_t flags = r.chance(0.2) ? 0x80 : 0;
        // O atingido: o jogador, um carro, outro corpo, nenhum.
        int who = -1;
        const double p = double(r.next() & 0xffff) / 65536.0;
        if (p < 0.25 && sc.player) who = 0;
        else if (p < 0.5 && !sc.cars.empty()) who = sc.cars[r.next() % sc.cars.size()];
        else if (p < 0.7 && sc.rigid) who = r.range(0, sc.rigid - 1);
        put(e.m, kEvent, pos, 16);
        put(e.m, kEvent + 0x10, nrm, 16);
        put(e.m, kEvent + 0x20, surface, 2);
        e.notes.clear();
        drv::effects_trace().clear();
        const size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
        ++t_fx.cases;
        e.call(kFnApplyWorldEffects,
               {kEvent, kEvent + 0x10, uint32_t(r.range(1, 2)), uint32_t(weapon), kEvent + 0x20, uint32_t(flags),
                who >= 0 ? image_of(who) : 0u});
        drv::Shell_ApplyWorldEffects(pos, nrm, 1, weapon, surface, flags, port_owner(who));
        std::string why;
        if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
        } else if (!(why = compare_sounds(port_sounds(snd_p), emu_sounds(e, snd_e))).empty()) {
        } else if (!compare_globals(e, why)) {
        } else if (!compare_sim(e, why)) {
        }
        if (!why.empty()) t_fx.fail(why);
        drop_port_sounds(snd_p);
        // ApplyWorldDamage: um choque com o mundo de verdade (CheckHitWorld), com o dano e o tamanho sorteados.
        alignas(16) float seg[2][4];
        for (int i = 0; i < 3; ++i) seg[0][i] = pos[i];
        seg[0][1] = at[1] + r.real(0.3f, 4);
        seg[0][3] = 1.0f;
        for (int i = 0; i < 3; ++i) seg[1][i] = seg[0][i] + r.real(-20, 20);
        seg[1][1] = seg[0][1] + r.real(-6, 1);
        seg[1][3] = 1.0f;
        constexpr uint32_t kSeg = kEvent + 0x400, kInfo = kEvent + 0x500;
        put(e.m, kSeg, seg, 32);
        drv::WorldCollisionInfo info;
        drv::WorldCollisionInfo_Init(info);
        put(e.m, kInfo, &info, 0x60);
        const uint32_t heap0 = e.heap;
        e.call(kFnCheckHitWorld, {kMgr, kSeg, kInfo});
        e.heap = heap0;
        drv::WCollisionMgr_CheckHitWorld(&seg[0][0], info);
        if (!info.hit) continue;
        if (r.chance(0.3)) {
            // Uma face de vidro, ou com o bit 2 do subtipo, ou o +0x52.
            const uint8_t ft = r.chance(0.5) ? 0x70 : info.face_type;
            const uint8_t fs = uint8_t(r.next() & 3), f52 = r.chance(0.3) ? 1 : 0;
            info.face_type = ft;
            info.face_subtype = fs;
            info.flag52 = f52;
            e.m.write32(kInfo + 0x50, uint32_t(ft) | uint32_t(fs) << 8 | uint32_t(f52) << 16 |
                                          (e.m.read32(kInfo + 0x50) & 0xff000000u));
        }
        e.notes.clear();
        drv::effects_trace().clear();
        ++t_dmg.cases;
        const float dmg = r.chance(0.2) ? r.real(0, 12) : r.real(0, 900), size = r.real(0, 3);
        const int32_t decal = r.range(0, 5);
        e.m.set_fpr(12, dmg);
        e.m.set_fpr(13, size);
        e.call(kFnApplyWorldDamage, {kEvent + 0x600, uint32_t(decal), kInfo});
        const drv::WWorldPos wp{};
        drv::Shell_ApplyWorldDamage(dmg, size, wp, decal, info);
        why.clear();
        if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) t_dmg.fail(why);
    }
    report("Shell::ApplyWorldEffects", t_fx, failures);
    report("Shell::ApplyWorldDamage", t_dmg, failures);
}

// ---------------------------------------------------------------- f) WCollisionMgr::CheckHitWindow

void check_windows(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t;
    uint64_t hit = 0, first = 0, again = 0;
    // As janelas da pista: (a instância, a barreira, a janela).
    struct Target {
        uint32_t inst;
        uint32_t barrier;
        const uint8_t* window;
    };
    std::vector<Target> targets;
    drv::TrackWorld& tw = w.tw;
    for (uint32_t i = 0; i < tw.instance_count(); ++i) {
        const drv::WCollisionInstance& ci = tw.instances()[i];
        const uint8_t* art = nf::p32_ptr<const uint8_t>(ci.article);
        if (!art) continue;
        const auto* ah = reinterpret_cast<const drv::CollisionArticle*>(art);
        if (!ah->window_groups) continue;
        const uint8_t* g = art + 0x20 + ah->barrier_offset + ah->window_offset;
        for (int gi = 0; gi < ah->window_groups; ++gi) {
            uint32_t n, size, bi;
            std::memcpy(&n, g, 4);
            std::memcpy(&size, g + 4, 4);
            std::memcpy(&bi, g + 8, 4);
            const uint8_t* en = g + 0xc;
            for (uint32_t k = 0; k < n; ++k) {
                uint32_t nw, side;
                std::memcpy(&nw, en, 4);
                std::memcpy(&side, en + 4, 4);
                if (side == uint32_t(ci.flags) >> 2)
                    for (uint32_t j = 0; j < nw; ++j) targets.push_back({i, bi, en + 8 + 8 * j});
                en += nw * 8 + 8;
            }
            if (size > 0x7fff) break;
            g += size;
        }
    }
    std::printf("    (%zu janelas na pista)\n", targets.size());
    if (targets.empty()) return;
    e.new_simulation();
    reset_window_map(e);
    drv::RRandom_StartUp(drv::render_random(), r.next());
    e.m.write32(kRRandom, drv::render_random().seed);
    e.m.write32(kRRandom + 4, drv::render_random().multiplier);
    std::vector<size_t> pool;  // um grupo pequeno de janelas, para os tiros repetidos na mesma janela
    for (int i = 0; i < 24; ++i) pool.push_back(r.next() % targets.size());
    for (int k = 0; k < cases; ++k) {
        if (k % 256 == 255) {
            // O heap emulado (os nós do map do original nunca são soltos): o mapa recomeça nos dois lados.
            e.heap = kHeap;
            reset_window_map(e);
        }
        const Target& tg = targets[r.chance(0.5) ? pool[r.next() % pool.size()] : r.next() % targets.size()];
        const drv::WCollisionInstance& ci = tw.instances()[tg.inst];
        const uint8_t* art = nf::p32_ptr<const uint8_t>(ci.article);
        const auto* ah = reinterpret_cast<const drv::CollisionArticle*>(art);
        if (tg.barrier >= ah->barrier_count) continue;
        const auto* b = reinterpret_cast<const drv::CollisionBarrier*>(art + 0x20 + ah->barrier_offset) + tg.barrier;
        // O ponto da janela no referencial da instância (ou perto dela) e no mundo.
        int16_t u0, v0, du, dv;
        std::memcpy(&u0, tg.window, 2);
        std::memcpy(&v0, tg.window + 2, 2);
        std::memcpy(&du, tg.window + 4, 2);
        std::memcpy(&dv, tg.window + 6, 2);
        const float u = (float(u0) + float(du) * r.real(-0.2f, 1.2f)) / 8192.0f;
        const float y = b->y0 + (float(v0) - float(dv) * r.real(-0.2f, 1.2f)) / 256.0f;
        const float lx = b->x0 + (b->x1 - b->x0) * u, lz = b->z0 + (b->z1 - b->z0) * u;
        float nx = b->z1 - b->z0, nz = -(b->x1 - b->x0);
        const float nl = std::sqrt(nx * nx + nz * nz) + 1e-6f;
        nx /= nl;
        nz /= nl;
        alignas(16) float m[16], inv[16];
        drv::WCollisionInstance_MakeMatrix(tw.mgr(), ci, m, true);
        std::memcpy(inv, m, 64);
        drv::OrthoInverse(inv);
        const float side = r.chance(0.5) ? 2.0f : -2.0f;
        const float a_l[3] = {lx + nx * side, y, lz + nz * side}, b_l[3] = {lx - nx * side, y, lz - nz * side};
        alignas(16) float seg[2][4];
        drv::Mat_TransformPoint3(a_l, inv, seg[0]);
        drv::Mat_TransformPoint3(b_l, inv, seg[1]);
        seg[0][3] = seg[1][3] = 1.0f;
        constexpr uint32_t kSeg = kEvent + 0x400, kInfo = kEvent + 0x500;
        put(e.m, kSeg, seg, 32);
        drv::WorldCollisionInfo info;
        drv::WorldCollisionInfo_Init(info);
        put(e.m, kInfo, &info, 0x60);
        const uint32_t heap0 = e.heap;
        e.call(kFnCheckHitWorld, {kMgr, kSeg, kInfo});
        e.heap = heap0;
        drv::WCollisionMgr_CheckHitWorld(&seg[0][0], info);
        std::string why = compare_info(e, kInfo, info, w);
        if (!why.empty()) {
            t.fail("o CheckHitWorld: " + why);
            continue;
        }
        const bool effect = r.chance(0.85);
        const int32_t mode = r.range(0, 1);
        e.notes.clear();
        e.events.clear();
        drv::effects_trace().clear();
        const uint64_t ev0 = drv::shells_stats().window_events;
        ++t.cases;
        e.call(kFnCheckHitWindow, {kMgr, kInfo, effect ? 1u : 0u, uint32_t(mode)});
        const uint32_t re = uint32_t(e.m.gpr(R::v0));
        const bool rp = drv::WCollisionMgr_CheckHitWindow(info, effect, mode);
        if ((re != 0) != rp) why = "o resultado: " + std::to_string(re) + " x " + std::to_string(rp);
        else if (!(why = compare_info(e, kInfo, info, w)).empty()) why = "depois: " + why;
        else if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
        } else if (e.m.read32(kRRandom) != drv::render_random().seed) {
            why = "o RRandom";
        } else if (!e.events.empty()) {
            why = "eventos no original";
        }
        if (!why.empty()) t.fail(why);
        if (rp) {
            ++hit;
            const auto& evs = drv::hit_window_events();
            if (drv::shells_stats().window_events > ev0 && !evs.empty()) {
                if (evs.back().image[10] == 0) ++first;
                else ++again;
            }
        }
    }
    report("WCollisionMgr::CheckHitWindow (paris_mis01)", t, failures);
    std::printf("    (%" PRIu64 " janelas atingidas: %" PRIu64 " a primeira vez, %" PRIu64 " de novo)\n", hit, first,
                again);
}

// ---------------------------------------------------------------- g) o EFireWeapon

// Os sons do EFireWeapon no original: o tiro da arma de um personagem (o ActWeapon sem a bandeira do jogador) usa como
// nome da mistura o lixo da pilha (sp+0xb0; fire_weapon.cpp, o cabeçalho), que o porte troca por "Henchmen Fire": aqui o
// som com a mistura desconhecida vira "Henchmen Fire" (um só, e só nesse caso).
std::vector<SoundRec> fire_emu_sounds(Emu& e, size_t from, bool henchman) {
    std::vector<SoundRec> v = emu_sounds(e, from);
    if (!henchman) return v;
    int n = 0;
    for (SoundRec& x : v)
        if (x.mix == "?" && n++ == 0) x.mix = "Henchmen Fire";
    return v;
}

uint32_t g_fire_system = 0;
int32_t g_streak_type = 0, g_streak_count = 0;
uint32_t g_selected = 0, g_player_target = 0;
float g_scene_m[4][4];
float g_tbi[4][4];
int32_t g_actor_value = 0;
bool g_actor_has = false;
std::map<std::pair<uint32_t, std::string>, int32_t> g_port_attrs;

bool fire_instance_position(uint32_t, int32_t, float m[4][4]) {
    std::memcpy(m, g_scene_m, 64);
    return true;
}
uint32_t fire_instance_system(uint32_t, uint32_t) { return g_fire_system; }
int32_t fire_lookup_int(uint32_t image, const char* key) {
    if (std::strcmp(key, "BULLET_STREAK_TYPE") == 0) return g_streak_type;
    auto it = g_port_attrs.find({image, key});
    return it == g_port_attrs.end() ? 0 : it->second;
}
int32_t fire_streak_count(int32_t) { return g_streak_count; }

void check_fire(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t;
    uint64_t rays = 0, shells = 0, missiles = 0, aw_shots = 0, ai_cars = 0, heli = 0, cam = 0, snd = 0;
    drv::EffectsEnv& env = drv::effects_env();
    const drv::EffectsEnv saved = env;
    env.instance_position = fire_instance_position;
    env.instance_system = fire_instance_system;
    env.lookup_int = fire_lookup_int;
    env.streak_count = fire_streak_count;
    static uint32_t s_gfx = 0;
    env.gfx_lookup = [](const char*) { return s_gfx; };
    static int32_t cam_1f4 = 0;
    env.camera_1f4 = &cam_1f4;
    drv::ShellsEnv& senv = drv::shells_env();
    senv.locate_fx = [](uint32_t, uint32_t, float m[4][4]) {
        std::memcpy(m, g_scene_m, 64);
        return true;
    };
    senv.transform_by_instance = [](uint32_t, uint32_t, float m[4][4]) {
        float r[4][4];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * g_tbi[k][j];
                r[i][j] = s;
            }
        std::memcpy(m, r, 64);
    };
    senv.player_targetable = []() { return g_player_target; };
    senv.actor_vehicle_value = [](uint32_t, int32_t& v) {
        v = g_actor_value;
        return g_actor_has;
    };
    senv.lookup_bool = [](uint32_t image, const char* key) {
        auto it = g_port_attrs.find({image, key});
        return it != g_port_attrs.end() && it->second != 0;
    };
    senv.streak_speed = port_streak_speed;
    drv::weapons_env().selected_target = []() { return g_selected; };
    // O gerente das armas: o do porte, com a tabela copiada para o original.
    drv::SWeaponManagerData& wm = drv::weapons_create();
    e.m.write32(0x33a530, kWM);
    // O RPlayerCamera do porte (a mira pela câmera): só a matriz e a frente guardada contam.
    static drv::PlayerCamera s_cam;
    drv::PlayerCamera* saved_cam = drv::current_player_camera();
    std::vector<int32_t> systems_by_weapon[0x20];
    for (int32_t wt = 0; wt < 0x20; ++wt)
        for (int k = 0; k < 8 && drv::kWeaponSystems[wt][k] != 0; ++k) systems_by_weapon[wt].push_back(drv::kWeaponSystems[wt][k]);
    static const int32_t kWeapons[] = {0, 0, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x10, 0x10, 1, 2, 0x15, 0x19, 0x1c, 7, 0x13};
    // O ActWeapon e o +0x15c: o original em kAW, o porte aqui.
    alignas(16) static uint8_t s_aw[0x200];
    alignas(16) static uint8_t s_acc[0x20];
    alignas(16) static uint8_t s_heli[0x200];
    alignas(16) static uint8_t s_ai[4][0x2a0];
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        reset_window_map(e);
        Scene sc;
        const auto& sp = w.spots[r.next() % w.spots.size()];
        const float at[3] = {sp[0], sp[1], sp[2]};
        random_scene(r, e, sc, at, 8.0f, true);
        e.phys_sub = r.chance(0.2) ? 1u : 0u;  // o carro do jogador submarino (o PBondCar::GetPhysics +0xc0)
        if (sc.player) sc.player->car_physics.sub_physics = int32_t(e.phys_sub);
        drv::Simulation& sim = drv::simulation();
        random_globals(r);
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.missile = 0;
        std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
        e.m.write32(kMission + 0x1c, kSettings);
        static uint8_t s_settings[0x2f8];
        std::memset(s_settings, 0, sizeof s_settings);
        for (int i = 0; i < 3; ++i) {
            const int32_t v = r.range(-40, 40);
            std::memcpy(s_settings + 0x2ac + 4 * i, &v, 4);
        }
        mm.settings = nf::p32_u(s_settings);
        put(e.m, kSettings, s_settings, sizeof s_settings);
        mm.difficulty = r.range(0, 2);
        e.m.write32(kMission + 0x3b8, uint32_t(mm.difficulty));
        mm.auto_drive = r.chance(0.2) ? 1 : 0;
        e.m.write32(kMission + 0x350, uint32_t(mm.auto_drive));
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        s_gfx = r.chance(0.8) ? 0x00c0ffeeu : 0u;
        e.gfx_answer = s_gfx;
        g_spark_answer = r.chance(0.5) ? 0x5e5e0000u : 0u;
        e.spark_answer = g_spark_answer;
        static std::string s_level;
        s_level = r.chance(0.3) ? "snow1a_mis3" : "paris_mis01";
        env.level = s_level.c_str();
        put(e.m, kLevelName, s_level.c_str(), uint32_t(s_level.size() + 1));
        drv::TargetPickerData& tpk = drv::target_picker();
        tpk.mode = 0;
        for (int i = 0; i < 3; ++i) tpk.world_target[i] = at[i] + r.real(-150, 150);
        drv::target_globals().targets_in_world = r.next() & 0xff;
        drv::target_globals().live = r.range(0, 3);
        e.m.write32(0x33a964, drv::target_globals().targets_in_world);
        e.m.write32(0x33a960, uint32_t(drv::target_globals().live));
        put(e.m, 0x33a970 + 0x10, tpk.world_target, 16);
        e.m.write32(0x33a970 + 0xa4, 0);
        e.m.write32(0x33194c, uint32_t(drv::missile_globals().wiggle_toggle));
        e.m.write32(0x331950, ps2::fbits(drv::missile_globals().pitch));
        // A tabela das armas (o dano sorteado) e o gerente no original.
        drv::WeaponEntry* table = drv::weapon_entry(wm, 0);
        for (int i = 0; i < drv::kWeaponCount; ++i) table[i].damage = r.range(0, 900);
        wm.turret_on_target = r.chance(0.5) ? 1 : 0;
        uint8_t wimg[0x190];
        std::memcpy(wimg, &wm, 0x190);
        const uint32_t tbl = kWM + 0x200;
        std::memcpy(wimg + 0x10, &tbl, 4);
        put(e.m, kWM, wimg, 0x190);
        for (int i = 0; i < drv::kWeaponCount; ++i) {
            uint8_t ent[0x54];
            std::memcpy(ent, &table[i], 0x54);
            const uint32_t z = 0;
            std::memcpy(ent + 0x04, &z, 4);
            const std::string sn = table[i].sound_name ? nf::p32_ptr<const char>(table[i].sound_name) : "";
            const uint32_t sa = kWM + 0x1000 + 32u * uint32_t(i);
            put(e.m, sa, sn.c_str(), uint32_t(sn.size() + 1));
            std::memcpy(ent + 0x44, table[i].sound_name ? &sa : &z, 4);
            put(e.m, tbl + 0x54u * uint32_t(i), ent, 0x54);
        }
        // A arma pelo sistema da instância.
        const int32_t wt = kWeapons[r.next() % (sizeof kWeapons / sizeof kWeapons[0])];
        g_fire_system = systems_by_weapon[wt].empty() ? 0x4f : uint32_t(systems_by_weapon[wt][r.next() % systems_by_weapon[wt].size()]);
        e.system = g_fire_system;
        g_streak_type = r.range(0, 3);
        e.streak_type = g_streak_type;
        g_streak_count = r.range(0, 4);
        for (uint32_t i = 0; i < 4; ++i) e.m.write32(0x423f38 + 0x18u * i + 0x14, uint32_t(g_streak_count));
        g_streak_speed = r.real(50, 400);
        for (int i = 0; i < 4; ++i) e.m.write32(0x423f38 + 0x18u * uint32_t(i) + 0x10, ps2::fbits(g_streak_speed));
        g_port_attrs.clear();
        // Quem atira: o carro do jogador (às vezes com a direção automática), um carro da IA, o helicóptero, uma
        // sentinela, um personagem (o ActWeapon com o dono personagem ou sem dono), a arma de Bond, ninguém.
        const double ps = double(r.next() & 0xffff) / 65536.0;
        int owner = -1;     // o índice ordenado do dono do RSceneObj (+0x24)
        bool use_aw = false, aw_player = false;
        int aw_holder = -1;
        if (ps < 0.25) {
            owner = 0;
        } else if (ps < 0.45 && !sc.cars.empty()) {
            owner = sc.cars[r.next() % sc.cars.size()];
        } else if (ps < 0.55 && sc.rigid > 1) {
            owner = r.range(1, sc.rigid - 1);  // um helicóptero (o tipo vira 10) ou outro
            if (r.chance(0.7)) {
                drv::PhysicsObject* o = port_owner(owner);
                if (CkRigid* cr = dynamic_cast<CkRigid*>(o)) {
                    cr->type = 10;
                    e.m.write32(image_of(owner) + 0x40, 10);
                } else if (CkCar* cc = dynamic_cast<CkCar*>(o)) {
                    (void)cc;  // um carro não vira helicóptero aqui
                }
            }
        } else if (ps < 0.62 && sc.simple) {
            for (int i = 0; i < drv::kSimpleBodySlots; ++i)
                if (sim.simple_owners[i] && sim.simple_owners[i]->kind() == 11) owner = drv::kRigidBodySlots + i;
        } else if (ps < 0.9) {
            use_aw = true;
            aw_player = r.chance(0.35);
            if (!aw_player && !sc.humans.empty() && r.chance(0.85)) aw_holder = sc.humans[r.next() % sc.humans.size()];
        }
        // O helicóptero: o +0x134 (a direção +0xf0, o lugar +0x110).
        if (owner >= 0 && port_owner(owner)->kind() == 10) {
            std::memset(s_heli, 0, sizeof s_heli);
            float* hf = reinterpret_cast<float*>(s_heli);
            for (int i = 0; i < 4; ++i) hf[0xf0 / 4 + i] = r.real(-1, 1);
            for (int i = 0; i < 3; ++i) hf[0x110 / 4 + i] = at[i] + r.real(-30, 30);
            put(e.m, kHeliData, s_heli, sizeof s_heli);
            e.m.write32(image_of(owner) + 0x134, kHeliData);
            if (CkRigid* cr = dynamic_cast<CkRigid*>(port_owner(owner))) cr->words[0x134] = nf::p32_u(s_heli);
            ++heli;
        }
        // Os carros da IA: o AIGroundVehicle (a precisão, o dano, a mira, o alvo primário) e os atributos.
        for (size_t ci = 0; ci < sc.cars.size() && ci < 4; ++ci) {
            const int o = sc.cars[ci];
            uint8_t* ai = s_ai[ci];
            std::memset(ai, 0, 0x2a0);
            drv::AIGroundVehicle& v = *reinterpret_cast<drv::AIGroundVehicle*>(ai);
            v.accuracy = uint8_t(r.range(0, 120));
            v.damage = int8_t(r.range(-120, 120));
            for (int i = 0; i < 3; ++i) {
                v.unknown240[i] = r.real(-1, 1);
                v.unknown250[i] = r.real(-1, 1);
                v.unknown1a8[i] = at[i] + r.real(-40, 40);
            }
            CkCar* cc = static_cast<CkCar*>(port_owner(o));
            cc->d.ai_ground_vehicle = nf::p32_u(ai);
            uint8_t img[0x2a0];
            std::memcpy(img, ai, 0x2a0);
            put(e.m, kAI + 0x2a0u * uint32_t(ci), img, 0x2a0);
            e.ai_of[image_of(o)] = kAI + 0x2a0u * uint32_t(ci);
            const int32_t good = r.chance(0.5) ? 1 : 0, flying = r.chance(0.3) ? 1 : 0;
            e.attrs[{image_of(o), "AI_GOODAIM"}] = good;
            e.attrs[{image_of(o), "IS_FLYING"}] = flying;
            g_port_attrs[{drv::object_image(cc), "AI_GOODAIM"}] = good;
            g_port_attrs[{drv::object_image(cc), "IS_FLYING"}] = flying;
            if (owner == o) ++ai_cars;
        }
        // O jogador: o HAS_TURRET e o WTargetable dele; o alvo do WTargetPicker.
        if (sc.player) {
            const int32_t turret = r.chance(0.5) ? 1 : 0;
            e.attrs[{image_of(0), "HAS_TURRET"}] = turret;
            g_port_attrs[{drv::object_image(sc.player), "HAS_TURRET"}] = turret;
            const int32_t fly = r.chance(0.2) ? 1 : 0;
            e.attrs[{image_of(0), "IS_FLYING"}] = fly;
            g_port_attrs[{drv::object_image(sc.player), "IS_FLYING"}] = fly;
        }
        static drv::WTargetableData s_target;
        s_target = drv::WTargetableData{};
        for (int i = 0; i < 3; ++i) s_target.pos[i] = at[i] + r.real(-30, 30);
        s_target.pos[3] = 1.0f;
        s_target.kind = 0;
        s_target.refs = 1;
        s_target.fixed = 1;
        put(e.m, kTargets, &s_target, 0x50);
        const bool has_selected = r.chance(0.4);
        g_selected = has_selected ? nf::p32_u(&s_target) : 0u;
        e.selected = has_selected ? kTargets : 0u;
        e.selected_port = g_selected;
        g_player_target = 0;
        e.player_target = 0;
        // A cena do evento: a matriz (o lugar na linha 3, a direção na 2), a câmera, o efeito.
        float q[4];
        random_quat(r, q);
        drv::VU0_quattom4(q, g_scene_m);
        g_scene_m[3][0] = at[0] + r.real(-3, 3);
        g_scene_m[3][1] = at[1] + r.real(0.5f, 3);
        g_scene_m[3][2] = at[2] + r.real(-3, 3);
        g_scene_m[3][3] = 1.0f;
        std::memcpy(e.scene_matrix, g_scene_m, 64);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) g_tbi[i][j] = (i == j ? 1.0f : 0.0f);
        for (int j = 0; j < 3; ++j) g_tbi[3][j] = g_scene_m[3][j];
        std::memcpy(e.tbi, g_tbi, 64);
        s_cam = drv::PlayerCamera{};
        for (int i = 0; i < 4; ++i) s_cam.d.forward[i] = r.real(-1, 1);
        for (int i = 0; i < 16; ++i) s_cam.d.matrix[i] = r.real(-1, 1);
        s_cam.d.matrix[12] = at[0] + r.real(-5, 5);
        s_cam.d.matrix[13] = at[1] + r.real(1, 5);
        s_cam.d.matrix[14] = at[2] + r.real(-5, 5);
        const bool has_cam = true;  // o jogo sempre tem a câmera do jogador (sem ela o PS2 leria o endereço 0)
        drv::current_player_camera() = has_cam ? &s_cam : nullptr;
        uint8_t cam_img[0x400] = {};
        std::memcpy(cam_img, &s_cam.d, std::min<size_t>(sizeof s_cam.d, 0x400));
        const uint32_t aiming = 0;
        std::memcpy(cam_img + 0x354, &aiming, 4);  // o RPlayerCamState: nulo (o CameraAiming dá 0)
        put(e.m, kCam, cam_img, 0x400);
        e.m.write32(kCamList + 4, has_cam ? kCam : 0u);
        cam_1f4 = 0;
        e.m.write32(kCam + 0x1f4, 0);
        // O efeito do evento (às vezes): o tipo +0x14 (6 ou 2) e o índice +0x15.
        static uint8_t s_fx[0x20];
        std::memset(s_fx, 0, sizeof s_fx);
        const bool has_fx = r.chance(0.15);
        if (has_fx) {
            s_fx[0x14] = uint8_t(r.chance(0.5) ? 6 : (r.chance(0.5) ? 2 : 1));
            s_fx[0x15] = uint8_t(r.range(0, 30));
        }
        constexpr uint32_t kFx = kEvent + 0x200;
        put(e.m, kFx, s_fx, sizeof s_fx);
        e.effect_port = has_fx ? nf::p32_u(s_fx) : 0u;
        // O ActWeapon: as bandeiras (+0x138), o dono (+0x144), a precisão (+0x15c), o contador e o rastro (+0x160).
        std::memset(s_aw, 0, sizeof s_aw);
        std::memset(s_acc, 0, sizeof s_acc);
        uint32_t aw_e = 0, aw_p = 0;
        if (use_aw) {
            ++aw_shots;
            const uint32_t flags = aw_player ? 2u : 0u;
            std::memcpy(s_aw + 0x138, &flags, 4);
            const uint32_t holder_p = aw_holder >= 0 ? drv::object_image(port_owner(aw_holder)) : 0u;
            std::memcpy(s_aw + 0x144, &holder_p, 4);
            const bool acc = r.chance(0.7);
            if (acc) {
                const int32_t pct = r.range(-150, 150), a2 = r.range(-20, 130);
                std::memcpy(s_acc + 0xc, &pct, 4);
                std::memcpy(s_acc + 0x10, &a2, 4);
            }
            const uint32_t acc_p = acc ? nf::p32_u(s_acc) : 0u;
            std::memcpy(s_aw + 0x15c, &acc_p, 4);
            const uint32_t cnt = uint32_t(r.range(0, 4));
            const int32_t st = r.range(0, 3);
            std::memcpy(s_aw + 0x160, &cnt, 4);
            std::memcpy(s_aw + 0x164, &st, 4);
            uint8_t img[0x200];
            std::memcpy(img, s_aw, 0x200);
            const uint32_t holder_e = aw_holder >= 0 ? image_of(aw_holder) : 0u;
            std::memcpy(img + 0x144, &holder_e, 4);
            const uint32_t acc_e = acc ? kAW + 0x300 : 0u;
            std::memcpy(img + 0x15c, &acc_e, 4);
            put(e.m, kAW + 0x300, s_acc, sizeof s_acc);
            put(e.m, kAW, img, 0x200);
            aw_e = kAW;
            aw_p = nf::p32_u(s_aw);
            // O valor do ator (a entrada +0x24 do +0x140 e a +0x13c do que ela devolve).
            g_actor_has = r.chance(0.5);
            g_actor_value = r.range(0, 6);
            e.actor_value_ptr = g_actor_has ? image_of(0) : 0u;
            e.actor_value = g_actor_value;
        }
        // O RSceneObj falso: o +0x24 (o dono) nos dois lados.
        static uint8_t s_scene[0x40];
        std::memset(s_scene, 0, sizeof s_scene);
        uint32_t own_p = owner >= 0 ? drv::object_image(port_owner(owner)) : 0u;
        std::memcpy(s_scene + 0x24, &own_p, 4);
        e.m.write32(kSceneFake + 0x24, owner >= 0 ? image_of(owner) : 0u);
        e.scene_port = nf::p32_u(s_scene);
        g_ptr_map = {{nf::p32_u(s_scene), kSceneFake}, {nf::p32_u(s_fx), kFx}};
        // O contador dos rastros do dono (+0x64).
        if (owner >= 0) {
            const uint32_t counter = uint32_t(r.range(0, 4));
            e.m.write32(image_of(owner) + 0x64, counter);
            drv::object_set_word(port_owner(owner), 0x64, counter);
        }
        if (sc.player) {
            const uint32_t counter = uint32_t(r.range(0, 4));
            e.m.write32(image_of(0) + 0x64, counter);
            sc.player->d.unknown064 = counter;
        }
        // O jogador tem o +0x5c... (os atributos são pela imagem); o +0x54 (a vida) dos carros.
        sim_to_emu(e, sc);
        globals_to_emu(e);
        const uint32_t index = uint32_t(r.range(0, 60));
        const uint32_t ei[6] = {0x3710b0, has_fx ? kFx : 0u, kSceneFake, 0x1234, index, aw_e};
        put(e.m, kEvent, ei, 0x18);
        drv::PendingEvent pe{10, 0, 0.0f, 0, 0, 0};
        pe.size = 0x18;
        std::memcpy(pe.obj, ei, sizeof ei);
        pe.obj[1] = has_fx ? nf::p32_u(s_fx) : 0u;
        pe.obj[2] = nf::p32_u(s_scene);
        pe.obj[5] = aw_p;
        cam += (mm.auto_drive && owner == 0) || aw_player ? 1 : 0;
        e.calls.clear();
        e.notes.clear();
        e.events.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        drv::trigger_manager().pending.clear();
        const size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
        const size_t mis_before = sim.missiles.size(), sh_before = sim.shells.size();
        const uint64_t rays0 = drv::shells_stats().rayshells;
        ++t.cases;
        e.last_rayshell = 0;
        e.call(kFnEFireWeapon, {kEvent, 0});
        drv::EFireWeapon_Execute(pe);
        const bool fired_ray = drv::shells_stats().rayshells != rays0;
        drv::RayShellData last_ray = drv::rayshell_last();
        last_ray.scene = to_emu_ptr(last_ray.scene);
        last_ray.effect = to_emu_ptr(last_ray.effect);
        rays += drv::shells_stats().rayshells - rays0;
        // Os objetos novos do porte ficam com a cena (o míssil apaga o som dele no destrutor).
        for (size_t i = mis_before; i < sim.missiles.size(); ++i) sc.owned.emplace_back(sim.missiles[i]);
        for (size_t i = sh_before; i < sim.shells.size(); ++i) sc.owned.emplace_back(sim.shells[i]);
        std::string why;
        const std::vector<uint32_t> me = emu_vector(e, 0xdfc), se = emu_vector(e, 0xe20);
        if ((e.last_rayshell != 0) != fired_ray) {
            why = "o RayShell: só um lado atirou";
        } else if (fired_ray && !same_bytes("o RayShell", e.m.mem(e.last_rayshell, 0xd0),
                                            reinterpret_cast<const uint8_t*>(&last_ray), 0xd0, why)) {
        } else if (me.size() != sim.missiles.size() - mis_before) {
            why = "os mísseis criados: " + std::to_string(me.size()) + " x " + std::to_string(sim.missiles.size() - mis_before);
        } else if (se.size() != sim.shells.size() - sh_before) {
            why = "os Shell criados: " + std::to_string(se.size()) + " x " + std::to_string(sim.shells.size() - sh_before);
        } else if (!(why = compare_calls(g_port_calls, e.calls)).empty()) {
        } else if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
        } else if (!(why = compare_sounds(port_sounds(snd_p), fire_emu_sounds(e, snd_e, use_aw && !aw_player))).empty()) {
        } else if (!(why = compare_events(e, 0)).empty()) {
        } else if (!compare_globals(e, why)) {
        } else if (!compare_sim(e, why)) {
        } else if (e.m.read32(kCam + 0x1f4) != uint32_t(cam_1f4)) {
            why = "o RPlayerCamera +0x1f4";
        } else if (use_aw && e.m.read32(kAW + 0x160) != rd_word(s_aw, 0x160)) {
            why = "o contador do ActWeapon (+0x160)";
        } else if (owner >= 0 && e.m.read32(image_of(owner) + 0x64) != drv::object_word(port_owner(owner), 0x64)) {
            why = "o contador dos rastros do dono (+0x64)";
        } else if (sc.player && e.m.read32(image_of(0) + 0x64) != sc.player->d.unknown064) {
            why = "o contador dos rastros do jogador (+0x64)";
        } else if (e.m.read32(0x33a964) != drv::target_globals().targets_in_world ||
                   e.m.read32(0x33a960) != uint32_t(drv::target_globals().live)) {
            why = "os globais do WTargetable";
        } else {
            for (size_t i = 0; i < me.size() && why.empty(); ++i) {
                drv::Missile* mp = static_cast<drv::Missile*>(sim.missiles[mis_before + i]);
                if (e.m.read32(me[i] + 0x6f4) != uint32_t(mp->d.weapon) ||
                    e.m.read32(me[i] + 0x718) != ps2::fbits(mp->d.damage))
                    why = "o míssil: a arma ou o dano";
                else if (!same_bytes("o corpo do míssil",
                                     e.m.mem(kSrbVec + 0x40u * uint32_t(mp->po.slot), 0x40),
                                     reinterpret_cast<const uint8_t*>(drv::GetSimpleRigidBody(sim, mp->po.slot)), 0x40,
                                     why)) {
                } else {
                    const uint32_t te = e.m.read32(me[i] + 0x6c);
                    if ((te != 0) != (mp->d.target != 0)) why = "o alvo do míssil";
                    else if (te && !same_bytes("o alvo do míssil", e.m.mem(te, 0x48),
                                               nf::p32_ptr<const uint8_t>(mp->d.target), 0x48, why)) {
                    }
                }
                ++missiles;
            }
            for (size_t i = 0; i < se.size() && why.empty(); ++i) {
                drv::Shell* sp2 = static_cast<drv::Shell*>(sim.shells[sh_before + i]);
                compare_shell(e, se[i], sp2, why);
                ++shells;
            }
        }
        snd += drv::sound_list().size() - snd_p;
        if (!why.empty()) {
            char ctx[160];
            std::snprintf(ctx, sizeof ctx, " [arma %x sistema %x dono %d tipo %d aw %d jogador %d portador %d efeito %d]", wt,
                          g_fire_system, owner, owner >= 0 ? port_owner(owner)->kind() : -1, use_aw ? 1 : 0,
                          aw_player ? 1 : 0, aw_holder, has_fx ? int(s_fx[0x14]) : -1);
            t.fail(why + ctx);
        }
        sc.owned.clear();
        drop_port_sounds(snd_p);
        drv::trigger_manager().pending.clear();
    }
    env = saved;
    g_ptr_map.clear();
    senv = drv::ShellsEnv{};
    drv::current_player_camera() = saved_cam;
    drv::weapons_env().selected_target = nullptr;
    drv::weapons_destroy();
    report("EFireWeapon::~EFireWeapon (as balas, a IA, a câmera)", t, failures);
    std::printf("    (%" PRIu64 " RayShell, %" PRIu64 " Shell, %" PRIu64 " mísseis, %" PRIu64 " ActWeapon, %" PRIu64
                " carros da IA, %" PRIu64 " helicópteros, %" PRIu64 " pela câmera, %" PRIu64 " sons)\n",
                rays, shells, missiles, aw_shots, ai_cars, heli, cam, snd);
}

}  // namespace

int run_integ_shell_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                           uint32_t vu0_size, int cases, uint32_t seed) {
    std::printf("-- os tiros: o RayShell, o Shell, as janelas e o EFireWeapon (o código original do EE x o porte)\n");
    int failures = 0;
    std::unique_ptr<Emu> e(new Emu());
    e->setup(elf, vu0_program, vu0_size);
    // As tabelas do ruído (o Noise::Init do jogo, conferido na parte 8): as do porte nos dois lados.
    drv::Noise_Noise1(0.0f);
    std::memcpy(e->m.mem(0x32f4e8, 0x2000), drv::noise_perm_table(), 0x2000);
    std::memcpy(e->m.mem(0x3d08a8, 0x10b * 4), drv::noise_grad_table(), 0x10b * 4);
    Rng r(seed ^ 0x5be11u);
    drv::SMissionManager_Construct(false);
    drv::effects_trace_enabled() = true;
    drv::shells_reset();
    port_env();
    check_utils(*e, r, cases, failures);
    check_globals_fns(*e, r, cases, failures);
    {
        WorldCtx w;
        if (load_world(*e, disc, "paris_mis01", w, r)) {
            check_rayshell(*e, r, w, cases, failures);
            check_shell(*e, r, w, cases / 4, failures);
            check_world_effects(*e, r, w, cases / 2, failures);
            check_windows(*e, r, w, cases, failures);
            check_fire(*e, r, w, cases, failures);
        } else {
            ++failures;
        }
        drv::set_world(nullptr);
    }
    drv::shells_env() = drv::ShellsEnv{};
    drv::effects_trace_enabled() = false;
    drv::effects_reset();
    drv::shells_reset();
    drv::mission_reset();
    if (e->errors) std::printf("    o interpretador falhou %" PRIu64 " vezes: %s\n", e->errors, e->error.c_str());
    drv::simulation_reset(60);
    return failures;
}

}  // namespace drvcheck
