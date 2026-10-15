// nf_driving_check, parte 18, continuação (tests/driving_check_missile.h): o corpo simples, as explosões, o míssil, o
// choque de um segmento com o mundo e o tiro do DRIVING.ELF, o código ORIGINAL no interpretador MIPS (com a VU0 em modo
// macro) lado a lado com o porte. docs/driving.md seção 22.
//
//  a) SimpleRigidBody (driving/physics/simple_body.h): UpdatePosition, ApplyFriction, Accelerate, GetScalarVelocity,
//     GetLinearVelocityCH, os três eixos do quatérnio, SetOrientMat e SetCanHitTrigger em corpos sorteados.
//  b) Simulation::DetectCollisionsSRB: cenas sorteadas de corpos rígidos (os donos com a imagem do PS2) e simples (os
//     donos de teste: tipos, bandeiras, caixas, o helicóptero), comparando os mapas de choque.
//  c) A explosão (driving/game/explosion.h): Simulation::SpawnExplosionObject e passos de Explosion::Simulate (com o
//     UpdatePosition e o DetectCollisionsSRB entre eles) sobre essas cenas, comparando os corpos, a explosão, as forças,
//     o dano nos donos (ApplyDamage pela tabela virtual) e o SimRandom; o Simulation::SpawnExplosion (o evento).
//  d) Os destrutores do ESpawnExplosion e do ESpawnExplosionStatic com imagens sorteadas (a posição, a direção, o tipo,
//     a arma, o dano, a face, o nome), comparando as anotações (a galeria, o tranco, a queimadura, a luz, a zona da IA,
//     os timbres), a explosão criada, os sons criados (os AOneShotSound e o ALimitedSound) e os globais do anel.
//
// Fora do código original (ganchos nativos): a memória (FastAlloc, __builtin_new, operator new, Event::operator_new,
// UMemory::Alloc, MEM_fill, memmove, memset), printf, o AttributeSet, o RSceneObj (GetBoundingDimensions,
// GetInstancePosition), a galeria (GFX::LookupReference, GFX::Trigger), o IFeedback, o RScorchWorld, o
// RHighLevelLightManager, o AIZoneController, o TIMER, o AIndex (o mesmo índice de nomes nos dois lados), o
// ABaseSound::operator_new, o AMix e o AVoice (os campos dos sons criados são lidos da memória emulada depois), e as
// entradas da tabela virtual dos donos (ApplyDamage, AddDamageByPlayer, o nome, GetPhysics), que anotam como o porte.

#include "driving_check_missile.h"

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
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/explosion.h"
#include "driving/game/feedback.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/triggers.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/sound/audio_internal.h"
#include "driving/track.h"
#include "driving/physics/world_collision.h"
#include "driving/game/weapons.h"
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
constexpr uint32_t kFnSimCtor = 0x1fe1b0, kFnAssignSlot = 0x1fef48, kFnDetect = 0x2014b8;
constexpr uint32_t kFnSrbUpdate = 0x1fd798, kFnSrbFriction = 0x1fd730, kFnSrbAccel = 0x1fd6d8;
constexpr uint32_t kFnSrbSpeed = 0x1fd648, kFnSrbLinVel = 0x1fd680, kFnSrbFwd = 0x1fd598, kFnSrbRight = 0x1fd5b8;
constexpr uint32_t kFnSrbUp = 0x1fd5d8, kFnSrbSetOrient = 0x1fd5f8, kFnSrbCanHit = 0x1fde20;
constexpr uint32_t kFnSpawnExplosionObject = 0x200200, kFnExplosionSimulate = 0x17ddc0, kFnSpawnExplosion = 0x200088;
constexpr uint32_t kFnESpawnExplosionDtor = 0x15a098, kFnESpawnExplosionStaticDtor = 0x15b010;
// Os ganchos.
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnBuiltinNew = 0x2d0910, kFnOperatorNew = 0x2d09c8;
constexpr uint32_t kFnEventNew = 0x175730, kFnEventDelete = 0x175758, kFnUAlloc = 0x2d03b8, kFnFastFree = 0x2cfe50;
constexpr uint32_t kFnBuiltinDelete = 0x2d09a8, kFnMemFill = 0x260300, kFnMemmove = 0x2a7f34, kFnMemset = 0x2a8038;
constexpr uint32_t kFnPrintf = 0x2a8148, kFnAttrSetCtor = 0x1696f0, kFnStrcasecmp = 0x2a9198;
constexpr uint32_t kFnVideoRate = 0x2d6b90, kFnBoundingDims = 0x1c9778, kFnInstancePos = 0x1c9368;
constexpr uint32_t kFnGfxLookup = 0x232f48, kFnGfxTrigger = 0x2330b0, kFnJolt = 0x165fb0, kFnRumble = 0x1660f0;
constexpr uint32_t kFnScorch = 0x1eb078, kFnLight = 0x1afa88, kFnZoneGet = 0x140100, kFnZoneRegister = 0x1402f8;
constexpr uint32_t kFnTimer = 0x261038, kFnIndexLookup = 0x2f5880, kFnSoundNew = 0x2fca38, kFnMixAdd = 0x2f2d50;
constexpr uint32_t kFnVoiceCtor = 0x2dd790, kFnPOSimulate = 0x195aa8, kFnLookupInt = 0x169bf0;
constexpr uint32_t kFnLookupBool = 0x169b28;

// Os globais do PS2.
constexpr uint32_t kSim = 0x339190, kSigCounter = 0x339180, kMissionPtr = 0x33a024, kBank4 = 0x3562b0;
constexpr uint32_t kExCounter = 0x32bbdc, kRing = 0x32bbfc, kRingTime = 0x32bc00, kLevelName = 0x1004f4;
constexpr uint32_t kLimitedCount = 0x355ec0;

// A RAM emulada.
constexpr uint32_t kRigidVec = 0x01000000, kSrbVec = 0x01001800, kInfos = 0x01003000;  // os infos: 48 x 0x500
constexpr uint32_t kRandom = 0x01012000;
constexpr uint32_t kImages = 0x01013000;      // as imagens dos donos: 0x200 bytes cada (48 rígidos + 96 simples)
constexpr uint32_t kVt = 0x01025000;          // a tabela virtual falsa dos donos (0x300)
constexpr uint32_t kFake = 0x01026000;        // as funções falsas
constexpr uint32_t kMission = 0x01027000;     // 0x1b50
constexpr uint32_t kBank = 0x01029000, kFeedback = 0x01029100, kPhys = 0x01029200, kZone = 0x01029400;
constexpr uint32_t kNames = 0x0102a000;       // as cadeias dos nomes (64 bytes por dono)
constexpr uint32_t kRenders = 0x0102e000;     // os RSceneObj falsos dos donos (0x40 bytes cada)
constexpr uint32_t kEvent = 0x01033000;       // a imagem do evento (0x50) e os dados dele
constexpr uint32_t kCars = 0x01032000;        // o vetor dos carros (o do jogador)
constexpr uint32_t kSceneFake = 0x01034000;   // o RSceneObj falso do ESpawnExplosion (a tabela virtual em +0x3c)
constexpr uint32_t kTargets = 0x01035000;     // os WTargetable (0x50 cada)
constexpr uint32_t kWorldFake = 0x01036000;   // o WWorld falso (as instâncias de render)
constexpr uint32_t kWM = 0x01037000;          // o SWeaponManager (0x190) e a tabela das armas (0xa80)
constexpr uint32_t kCamList = 0x01039000, kCam = 0x01039100;  // a lista das câmeras e o RPlayerCamera (o +0x1f4)
constexpr uint32_t kCamListPtrEmu = 0x333ed8;
constexpr uint32_t kSceneTable = 0x01040000;  // 32 bytes por instância de render
constexpr uint32_t kSceneObjs = 0x01070000;   // 64 bytes por instância de render
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01200000;
constexpr uint32_t kMgr = 0x01036800, kGrid = 0x01036900;
constexpr uint32_t kWorldBegin = 0x01200000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

constexpr uint32_t kFnApplyDamage = kFake + 0x00, kFnName = kFake + 0x10, kFnAddDamage = kFake + 0x20;
constexpr uint32_t kFnGetPhysics = kFake + 0x30, kFnRenderFx = kFake + 0x40, kFnASoundCtor = 0x2ed3d0;
constexpr uint32_t kVtRender = kVt + 0x400;  // a tabela virtual dos RSceneObj falsos (TriggerFX em +0x84)
constexpr uint32_t kFnLockedTarget = kFake + 0x50, kFnEmp = kFake + 0x60, kFnNop = kFake + 0x70;
constexpr uint32_t kMixBase = 0x7000;  // os AMix falsos: kMixBase + o índice do nome

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
}

bool same_bytes(const char* what, const uint8_t* emu, const uint8_t* port, uint32_t n, std::string& out,
                uint32_t skip_from = 0, uint32_t skip_to = 0) {
    for (uint32_t o = 0; o < n; o += 4) {
        if (o >= skip_from && o < skip_to) continue;
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
    int owner = -1;  // o índice ordenado (0..0x8f)
    uint32_t amount = 0, split = 0, dkind = 0;
    uint32_t from[3] = {}, to[3] = {};
    bool operator==(const OwnerCall& o) const {
        return kind == o.kind && owner == o.owner && amount == o.amount && split == o.split && dkind == o.dkind &&
               std::memcmp(from, o.from, 12) == 0 && std::memcmp(to, o.to, 12) == 0;
    }
    std::string text() const {
        char b[200];
        std::snprintf(b, sizeof b, "{%d dono %d %08x %08x %u de %08x,%08x,%08x a %08x,%08x,%08x}", kind, owner, amount,
                      split, dkind, from[0], from[1], from[2], to[0], to[1], to[2]);
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
    std::memcpy(c.from, from, 12);
    std::memcpy(c.to, to, 12);
    g_port_calls.push_back(c);
}

// Os donos do lado do porte: o carro do jogador (vaga rígida 0), os outros donos rígidos e os de corpo simples.
class CkCar : public drv::BondCar {
public:
    int ordered = 0;
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return 0;
    }
    void add_damage_by_player(float damage) override {
        OwnerCall c;
        c.kind = 2;
        c.owner = ordered;
        c.amount = ps2::fbits(damage);
        g_port_calls.push_back(c);
    }
    // O AttackWithEmp (+0x254) é conferido na parte 17; aqui, como o gancho do original, só a anotação do míssil.
    void attack_with_emp() override {}
};
class CkRigid : public drv::BasicOwner {
public:
    int ordered = 0;
    int32_t type = 4;
    std::string nm;
    drv::WWorldPos wp{};
    CkRigid() { set_world_pos(&wp); }
    int32_t kind() override { return type; }
    const char* name() override { return nm.c_str(); }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return 0;
    }
    void add_damage_by_player(float damage) override {
        OwnerCall c;
        c.kind = 2;
        c.owner = ordered;
        c.amount = ps2::fbits(damage);
        g_port_calls.push_back(c);
    }
};
class CkSimple : public drv::SimpleObject {
public:
    int ordered = 0;
    std::string nm;
    bool has_bounds = false;
    float half[4] = {};
    uint32_t word13c = 0;
    const char* name() override { return nm.c_str(); }
    bool collision_bounds(float out[4]) override {
        if (!has_bounds) return false;
        std::memcpy(out, half, 16);
        return true;
    }
    uint32_t raw_word(uint32_t offset) override { return offset == 0x13c ? word13c : 0u; }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        port_damage_call(ordered, from, to, amount, split, kind);
        return 0;
    }
};

// Um som criado (os campos que os dois lados escrevem).
struct SoundRec {
    std::string mix;
    uint32_t limited = 0, transit = 0;
    uint32_t pos[3] = {}, vel[3] = {};
    uint32_t min = 0, max = 0, max2 = 0, volume = 0, pitch = 0, fx = 0, lifetime = 0;
    bool operator==(const SoundRec& o) const {
        return mix == o.mix && limited == o.limited && transit == o.transit && std::memcmp(pos, o.pos, 12) == 0 &&
               std::memcmp(vel, o.vel, 12) == 0 && min == o.min && max == o.max && max2 == o.max2 &&
               volume == o.volume && pitch == o.pitch && fx == o.fx && lifetime == o.lifetime;
    }
    std::string text() const {
        char b[260];
        std::snprintf(b, sizeof b, "{%s lim %u voo %u pos %08x,%08x,%08x vel %08x,%08x,%08x min %08x max %08x %08x vol "
                                   "%08x tom %08x fx %08x vida %08x}",
                      mix.c_str(), limited, transit, pos[0], pos[1], pos[2], vel[0], vel[1], vel[2], min, max, max2,
                      volume, pitch, fx, lifetime);
        return b;
    }
};

// Os nomes dos timbres do índice falso do banco das armas (o mesmo número nos dois lados).
const char* const kPatchNames[] = {"SFX_HugeExp", "SFX_LrgExp", "SFX_MedExp", "SFX_SmallExp", "SFX_DistExp",
                                   "SFX_MissleTrans", "SFX_RocketTrans", "SFX_EMPtrans", "SFX_EMPdetonate",
                                   "SFX_MissleFire", "SFX_RocketFire", "SFX_htorpedofire", "SFX_UMissileTrans"};
const char* const kMixNames[] = {"Explosions", "ExplosionBass", "Weapons Fire", "Henchmen Fire", "POV Weapons",
                                 "Weapons Transit"};

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::vector<drv::EffectsCall> notes;
    std::vector<OwnerCall> calls;
    std::vector<uint32_t> sounds;   // os ABaseSound alocados, em ordem
    std::vector<uint32_t> events;   // os eventos alocados (Event::operator_new)
    std::map<uint32_t, int> owner_index;  // a imagem -> o índice ordenado
    std::map<uint32_t, std::array<float, 4>> bounds;  // o RSceneObj falso -> as meias-medidas
    int32_t timer = 0;
    uint32_t gfx_answer = 0;   // o que o GFX::LookupReference devolve (0: não há)
    uint32_t phys_underwater = 0;
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

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        // Pelo menos 0x140 bytes zerados: o SimpleRigidBody::CheckCollisions de uma explosão (0x98 bytes) lê o +0x13c do
        // dono dela contra um helicóptero (o lixo do heap no PS2; o porte lê 0).
        auto newf = [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 0x140u))); };
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
        m.hook(kFnPrintf, "printf", [](MipsMachine&) {});
        m.hook(kFnAttrSetCtor, "AttributeSet::AttributeSet", [](MipsMachine&) {});
        m.hook(kFnStrcasecmp, "strcasecmp", [](MipsMachine& mm) {
            std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            for (char& c : a) c = char(std::tolower(uint8_t(c)));
            for (char& c : b) c = char(std::tolower(uint8_t(c)));
            mm.set_gpr(R::v0, uint32_t(a == b ? 0 : (a < b ? -1 : 1)));
        });
        m.hook(kFnVideoRate, "GetFoundationVideoModeRate", [](MipsMachine& mm) {
            mm.set_fpr(0, float(drv::simulation().frame_rate));  // a taxa do modo de vídeo (a da Simulation)
        });
        m.hook(kFnBoundingDims, "RSceneObj::GetBoundingDimensions", [this](MipsMachine& mm) {
            const uint32_t r = uint32_t(mm.gpr(R::a0)), out = uint32_t(mm.gpr(R::a1));
            auto it = bounds.find(r);
            if (it == bounds.end()) throw nf::MipsError("RSceneObj falso desconhecido", r, 0);
            put(mm, out, it->second.data(), 16);
        });
        m.hook(kFnLookupInt, "AttributeSet::LookupInt", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnLookupBool, "AttributeSet::LookupBool", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        // A tabela virtual falsa dos donos.
        m.hook(kFnApplyDamage, "PhysicsObject::ApplyDamage (falso)", [this](MipsMachine& mm) {
            OwnerCall c;
            c.kind = 1;
            c.owner = owner_of(uint32_t(mm.gpr(R::a0)));
            c.amount = mm.fpr_bits(12);
            c.split = mm.fpr_bits(13);
            c.dkind = uint32_t(mm.gpr(R::a3));
            std::memcpy(c.from, mm.mem(uint32_t(mm.gpr(R::a1)), 12), 12);
            std::memcpy(c.to, mm.mem(uint32_t(mm.gpr(R::a2)), 12), 12);
            calls.push_back(c);
            note(0x00189598, c.amount, uint32_t(c.owner), c.dkind);  // o porte anota o PBondCar::ApplyDamage
            mm.set_gpr(R::v0, 0);
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
            mm.write32(kPhys + 0xbc, phys_underwater);
            mm.set_gpr(R::v0, kPhys);
        });
        auto entry = [this](uint32_t off, uint32_t fn) {
            m.write32(kVt + off, 0);
            m.write32(kVt + off + 4, fn);
        };
        entry(0x18, kFnApplyDamage);
        entry(0x80, kFnName);
        entry(0x1a8, kFnAddDamage);
        entry(0x1d0, kFnGetPhysics);
        entry(0x400 + 0x80, kFnRenderFx);
        entry(0x400 + 0x38, kFnNop);     // RSceneObj::TransformToWorldSpace (vazia no RVehicle)
        entry(0x298, kFnLockedTarget);  // o alvo travado do carro do jogador
        entry(0x250, kFnEmp);

        // Os sistemas à volta (anotam como o porte).
        m.hook(kFnGfxLookup, "GFX::LookupReference", [this](MipsMachine& mm) {
            note(kFnGfxLookup, gfx_answer);
            mm.set_gpr(R::v0, gfx_answer);
        });
        m.hook(kFnGfxTrigger, "GFX::Trigger", [this](MipsMachine& mm) {
            // (efeito, pos, quat, pos local, quat local, RSceneObj, instância): com o RSceneObj o lugar é o local.
            const uint32_t render = uint32_t(mm.gpr(R::t1));
            const uint32_t p = uint32_t(render ? mm.gpr(R::a3) : mm.gpr(R::a1));
            note(kFnGfxTrigger, uint32_t(mm.gpr(R::a0)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8), render,
                 uint32_t(mm.gpr(R::t2)));
        });
        m.hook(kFnRenderFx, "RSceneObj::TriggerFX (falso)", [this](MipsMachine& mm) {
            note(0x001c0084, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a2)));
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
            for (uint32_t i = 0; i < sizeof kMixNames / sizeof kMixNames[0]; ++i)
                if (name == kMixNames[i]) idx = i;
            mm.set_gpr(R::v0, kMixBase + idx);
        });
        m.hook(kFnVoiceCtor, "AVoice::AVoice", [](MipsMachine&) {});
        m.hook(kFnPOSimulate, "PhysicsObject::Simulate", [this](MipsMachine& mm) {
            note(kFnPOSimulate, mm.read32(uint32_t(mm.gpr(R::a0)) + 0x40));
        });
        m.hook(kFnInstancePos, "RSceneObj::GetInstancePosition",
               [](MipsMachine& mm) { throw nf::MipsError("GetInstancePosition sem cena", uint32_t(mm.gpr(R::a0)), 0); });
        // O míssil: a mira, o RSceneObj do modelo, o som do lock, a zona macia, as janelas, o dano do mundo, o evento do
        // programador, o míssil guiado, as entradas do carro (o alvo travado +0x29c, o EMP +0x254).
        m.hook(0x1c7098, "RSceneObj::RSceneObj", [this](MipsMachine& mm) {
            note(0x1c7098);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x195978, "PhysicsObject::SetRenderObject",
               [](MipsMachine& mm) { mm.write32(uint32_t(mm.gpr(R::a0)) + 0x48, uint32_t(mm.gpr(R::a1))); });
        m.hook(0x1c8b78, "RSceneObj::Hide", [this](MipsMachine&) { note(0x1c8b78); });
        m.hook(0x1cacb8, "RSceneObj::StopFX", [](MipsMachine&) {});
        m.hook(0x190eb8, "PBondCar_PosInSoftZone", [this](MipsMachine& mm) {
            note(0x190eb8);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x2f4b10, "AMenuSound::Trigger (o lock)", [this](MipsMachine&) { note(0x2f4b10); });
        m.hook(0x2f4a20, "AMenuSound::Trigger", [this](MipsMachine& mm) {
            note(0x2f4a20, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)));
        });
        // As janelas (WCollisionMgr::CheckHitWindow) e o dano do mundo (Shell::ApplyWorldDamage) rodam no original
        // (driving/game/shells.h); o que eles chamam fora anota como o porte: o EHitWindow (o evento, tirado da lista dos
        // eventos do operator_new), a queimadura, os estímulos do RAnimEngine.
        m.hook(0x14f330, "EHitWindow::EHitWindow", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1)), d = uint32_t(mm.gpr(R::a3));
            note(0x14f330, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8), mm.read32(d), mm.read32(d + 4),
                 uint32_t(mm.gpr(R::t0)));
            if (!events.empty()) events.pop_back();
        });
        m.hook(0x1ea8e8, "RScorchWorld::ScorchWorld", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            note(0x1ea8e8, mm.fpr_bits(12), uint32_t(mm.gpr(R::a3)), mm.read32(p), mm.read32(p + 4), mm.read32(p + 8));
        });
        m.hook(0x1a2868, "RAnimEngine::Handle::ProcessStimuli", [this](MipsMachine& mm) {
            note(0x1a2868, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                 uint32_t(mm.gpr(R::a3)));
        });
        m.hook(0x208540, "SMissionManager::ProgrammerDefinedEvent",
               [this](MipsMachine& mm) { note(0x208540, uint32_t(mm.gpr(R::a1))); });
        m.hook(0x2064d8, "SMissionManager::SetDriveMissile", [this](MipsMachine& mm) {
            const uint32_t mis = uint32_t(mm.gpr(R::a1));
            note(0x2064d8, mis ? 1u : 0u);
            mm.write32(uint32_t(mm.gpr(R::a0)) + 0x358, mis);
        });
        m.hook(kFnLockedTarget, "PBondCar: o alvo travado (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnEmp, "PBondCar: o EMP (falso)", [this](MipsMachine& mm) {
            note(0x18ac28, uint32_t(owner_of(uint32_t(mm.gpr(R::a0)))));
        });
        m.hook(kFnNop, "nada (falso)", [](MipsMachine&) {});
        m.hook(0x10c6c8, "depuração (FUN_0010c6c8)", [this](MipsMachine&) { note(0x10c6c8); });
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
        // O banco das armas (o vazio: o snd_bank -1), o IFeedback do jogador, a zona.
        m.write32(kSceneFake + 0x3c, kVtRender);
    m.write32(kCamListPtrEmu, kCamList);  // a lista das câmeras: o RPlayerCamera falso (zerado)
    m.write32(kCamList + 4, kCam);
    m.write32(kBank4, kBank);
        m.write32(kBank + 0x8, 0xffffffffu);
        m.write32(kMissionPtr, kMission);
    }

    // A Simulation nova (o construtor original) com os vetores do porte.
    void new_simulation() {
        heap = kHeap;
        events.clear();
        sounds.clear();
        call(kFnSimCtor, {kSim});
    }
};

// ---------------------------------------------------------------- a cena: os dois lados

struct Scene {
    std::vector<std::unique_ptr<drv::PhysicsObject>> owned;  // os donos de teste do porte
    CkCar* player = nullptr;
    int rigid = 0, simple = 0;
};

// Uma orientação sorteada (um quatérnio unitário) e a matriz dela.
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

// O lado emulado de um dono: a imagem do PhysicsObject (o tipo, as bandeiras, a vaga, o RSceneObj, o IFeedback, a
// tabela virtual) e o nome.
void write_image(Emu& e, int ordered, int32_t type, uint16_t flags, int16_t slot, bool render, bool feedback,
                 const std::string& name, uint32_t word13c) {
    const uint32_t a = image_of(ordered);
    std::memset(e.m.mem(a, 0x200), 0, 0x200);
    e.m.write32(a + 0x40, uint32_t(type));
    e.m.write32(a + 0x44, uint32_t(flags) | uint32_t(uint16_t(slot)) << 16);
    e.m.write32(a + 0x48, render ? render_of(ordered) : 0u);
    e.m.write32(a + 0x50, feedback ? kFeedback : 0u);
    e.m.write32(a + 0x68, kVt);
    e.m.write32(a + 0x13c, word13c);
    put(e.m, name_of(ordered), name.c_str(), uint32_t(name.size() + 1));
    e.owner_index[a] = ordered;
}

// O porte -> o original: os corpos, os infos (só a orientação e a caixa contam), as vagas, as assinaturas, o
// SimRandom, o contador das assinaturas, o passo, o dt, os carros.
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
        std::memset(inf + 0x380, 0, 0x40);  // os rastreadores do chão (ponteiros)
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
    // O vetor dos carros: o do jogador (vaga 0).
    if (sc.player) {
        m.write32(kCars, image_of(0));
        m.write32(kSim + 0xdcc, kCars);
        m.write32(kSim + 0xdd0, kCars + 4);
        m.write32(kSim + 0xdd4, kCars + 4);
    }
}

// O original x o porte: os corpos, os mapas de choque, as vagas, as assinaturas, as contagens, o SimRandom.
bool compare_sim(Emu& e, std::string& why, bool rigid = true) {
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
            why = "a assinatura simples " + std::to_string(i) + ": " + hex(m.read32(kSim + 0xf0 + 4u * uint32_t(i))) +
                  " x " + hex(sim.simple_sig[i]);
            return false;
        }
        if ((m.read32(kSim + 0x330 + 4u * uint32_t(i)) != 0) != (sim.simple_owners[i] != nullptr)) {
            why = "o dono simples " + std::to_string(i);
            return false;
        }
    }
    if (rigid) {
        for (int i = 0; i < drv::kRigidBodySlots; ++i) {
            if (!sim.owners[i]) continue;
            if (!same_bytes(("o corpo rígido " + std::to_string(i)).c_str(), m.mem(kRigidVec + 0x80u * uint32_t(i), 0x80),
                            reinterpret_cast<const uint8_t*>(&sim.bodies[i]), 0x80, why, 0x5c, 0x60))
                return false;
        }
    }
    if (m.read32(kSim + 0xc) != uint32_t(sim.simple_count)) {
        why = "a contagem dos corpos simples: " + std::to_string(m.read32(kSim + 0xc)) + " x " +
              std::to_string(sim.simple_count);
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

// A cena sorteada: o carro do jogador na vaga 0 (às vezes), outros corpos rígidos e corpos simples de teste.
void random_scene(Rng& r, Emu& e, Scene& sc, float spread, bool force_player = false) {
    drv::simulation_reset(r.chance(0.85) ? 60 : 50);
    drv::Simulation& sim = drv::simulation();
    sim.steps = r.range(0, 5000);
    drv::SimRandom_Construct(drv::sim_random());
    drv::SimRandom_Reset(drv::sim_random());
    for (int k = r.range(0, 40); k > 0; --k) drv::SimRandom_Generate(drv::sim_random());
    drv::simulation_signature_counter() = uint32_t(r.range(1, 0x2000));
    e.owner_index.clear();
    e.bounds.clear();
    // Os corpos rígidos.
    const int nrigid = force_player ? r.range(1, 6) : r.range(0, 6);
    for (int i = 0; i < nrigid; ++i) {
        drv::RigidBodyParams p;
        p.mass = r.real(50, 2500);
        p.type = i == 0 ? 1 : r.range(1, 7);
        for (float& v : p.pos) v = r.real(-spread, spread);
        p.pos[1] = r.real(-1, 3);
        for (float& v : p.vel) v = r.real(-20, 20);
        float q[4];
        random_quat(r, q);
        drv::VU0_quattom4(q, p.orient);
        for (int k = 0; k < 3; ++k) p.half_size[k] = r.real(0.2f, 2.5f);
        p.inv_inertia[0] = p.inv_inertia[1] = p.inv_inertia[2] = r.real(0.0005f, 0.01f);
        drv::PhysicsObject* owner = nullptr;
        std::string name = r.chance(0.15) ? "tunnel" : (r.chance(0.5) ? "vanquish" : "box");
        bool feedback = false;
        if (i == 0 && (force_player || r.chance(0.8))) {
            auto c = std::make_unique<CkCar>();
            c->ordered = 0;
            c->d.type = 1;
            c->d.slot = 0;
            c->name_ = name == "tunnel" ? "vanquish" : name;
            name = c->name_;
            c->d.physics = nf::p32_u(&c->car_physics);
            c->feedback.port = r.chance(0.8) ? 0 : -1;
            feedback = c->feedback.port >= 0;
            c->d.feedback_object = feedback ? 1u : 0u;
            sc.player = c.get();
            owner = c.get();
            sc.owned.push_back(std::move(c));
        } else {
            auto o = std::make_unique<CkRigid>();
            o->ordered = i;
            o->type = p.type;
            o->nm = name;
            owner = o.get();
            sc.owned.push_back(std::move(o));
        }
        drv::RigidBody* rb = drv::simulation_add_body(sim, p, owner);
        if (!rb) break;
        rb->state = int8_t(r.chance(0.1) ? 3 : (r.chance(0.2) ? 1 : 2));
        if (sc.player && owner == sc.player) {
            sim.cars.push_back(sc.player);
        }
        write_image(e, i, p.type, 0, int16_t(i), false, feedback, name, 0);
        ++sc.rigid;
    }
    if (!sc.player) sim.cars.clear();
    // Os corpos simples de teste.
    const int nsimple = r.range(0, 8);
    for (int j = 0; j < nsimple; ++j) {
        auto o = std::make_unique<CkSimple>();
        const int slot = drv::AssignSimpleBodySlot(sim, o.get());
        if (slot < 0) break;
        o->ordered = drv::kRigidBodySlots + slot;
        o->po.type = r.range(2, 11);
        o->po.flags = 1;
        o->po.slot = int16_t(slot);
        o->nm = r.chance(0.5) ? "thing" : "tunnel";
        o->has_bounds = r.chance(0.5);
        for (int k = 0; k < 3; ++k) o->half[k] = r.real(0.05f, 3.0f);
        o->half[3] = r.real(0, 1);
        o->word13c = r.chance(0.5) ? 0u : 1u;
        static const uint8_t kTypes[] = {2, 3, 5, 6, 7, 8, 8, 9};
        const uint8_t btype = kTypes[r.next() % 8];
        float pos[3], vel[3], acc[3];
        for (int k = 0; k < 3; ++k) {
            pos[k] = r.real(-spread, spread);
            vel[k] = r.real(-30, 30);
            acc[k] = r.real(-5, 5);
        }
        pos[1] = r.real(-1, 3);
        float q[4], om[4][4];
        random_quat(r, q);
        drv::VU0_quattom4(q, om);
        drv::SimpleRigidBody* b = drv::GetSimpleRigidBody(sim, slot);
        drv::SimpleRigidBody_Construct(b, r.real(0, 1.5f), r.real(0.5f, 20), slot, btype, pos, vel, acc, om);
        static const uint16_t kFlags[] = {0x10, 0x20, 0x40, 0x80, 0x30, 0xc0, 0x1c9, 0x131, 0x0c, 0x01, 0xf0, 0x48};
        b->flags = kFlags[r.next() % 12];
        if (r.chance(0.3)) b->flags = uint16_t(b->flags | 0x0c);
        write_image(e, o->ordered, o->po.type, 1, int16_t(slot), o->has_bounds, false, o->nm, o->word13c);
        if (o->has_bounds) {
            std::array<float, 4> h = {o->half[0], o->half[1], o->half[2], o->half[3]};
            e.bounds[render_of(o->ordered)] = h;
        }
        sc.owned.push_back(std::move(o));
        ++sc.simple;
    }
}

// ---------------------------------------------------------------- a) os métodos do SimpleRigidBody

void check_srb_methods(Emu& e, Rng& r, int cases, int& failures) {
    Tally t[9];
    const char* names[9] = {"SimpleRigidBody::UpdatePosition", "SimpleRigidBody::ApplyFriction",
                            "SimpleRigidBody::Accelerate",     "SimpleRigidBody::GetScalarVelocity",
                            "SimpleRigidBody::GetLinearVelocityCH", "SimpleRigidBody::GetForwardVector",
                            "SimpleRigidBody::GetRightVector", "SimpleRigidBody::GetUpVector",
                            "SimpleRigidBody::SetOrientMat"};
    constexpr uint32_t kB = kSrbVec, kOut = kEvent;
    for (int k = 0; k < cases; ++k) {
        drv::simulation_reset(r.chance(0.8) ? 60 : 50);
        drv::Simulation& sim = drv::simulation();
        e.m.write32(kSim + 0xdc0, ps2::fbits(sim.dt));
        e.m.write32(kSim + 0x4, 0);
        e.m.write32(kSim + 0x20, kSrbVec);
        drv::SimpleRigidBody b{};
        float q[4];
        random_quat(r, q);
        if (r.chance(0.2)) for (float& v : q) v *= r.real(0.5f, 1.5f);
        std::memcpy(b.quat, q, 16);
        for (int i = 0; i < 3; ++i) {
            b.pos[i] = r.real(-500, 500);
            b.vel[i] = r.real(-80, 80);
            b.accel[i] = r.real(-10, 10);
        }
        b.type = uint8_t(r.range(1, 9));
        b.slot = int8_t(r.range(0, 95));
        b.flags = uint16_t(r.next() & 0x1ff);
        b.radius = r.real(0, 2);
        b.mass = r.real(0.1f, 50);
        const int op = k % 9;
        Tally& tt = t[op];
        ++tt.cases;
        put(e.m, kB, &b, 0x40);
        float acc[4] = {r.real(-50, 50), r.real(-50, 50), r.real(-50, 50), 0};
        float out_p[4] = {r.real(-1, 1), r.real(-1, 1), r.real(-1, 1), r.real(-1, 1)};
        put(e.m, kOut, out_p, 16);
        const int axis = r.range(0, 2);
        float m4[4][4];
        drv::VU0_quattom4(q, m4);
        put(e.m, kOut + 0x40, m4, 64);
        uint32_t ret_e = 0, ret_p = 0;
        switch (op) {
            case 0:
                e.call(kFnSrbUpdate, {kB});
                drv::SimpleRigidBody_UpdatePosition(&b);
                break;
            case 1:
                e.call(kFnSrbFriction, {kB});
                drv::SimpleRigidBody_ApplyFriction(&b);
                break;
            case 2:
                put(e.m, kOut, acc, 16);
                e.call(kFnSrbAccel, {kB, kOut});
                drv::SimpleRigidBody_Accelerate(&b, acc);
                break;
            case 3:
                e.call(kFnSrbSpeed, {kB});
                ret_e = e.m.fpr_bits(0);
                ret_p = ps2::fbits(drv::SimpleRigidBody_GetScalarVelocity(&b));
                break;
            case 4:
                e.call(kFnSrbLinVel, {kB, uint32_t(axis)});
                ret_e = e.m.fpr_bits(0);
                ret_p = ps2::fbits(drv::SimpleRigidBody_GetLinearVelocityCH(&b, axis));
                break;
            case 5:
                e.call(kFnSrbFwd, {kB, kOut});
                drv::SimpleRigidBody_GetForwardVector(&b, out_p);
                break;
            case 6:
                e.call(kFnSrbRight, {kB, kOut});
                drv::SimpleRigidBody_GetRightVector(&b, out_p);
                break;
            case 7:
                e.call(kFnSrbUp, {kB, kOut});
                drv::SimpleRigidBody_GetUpVector(&b, out_p);
                break;
            case 8:
                e.call(kFnSrbSetOrient, {kB, kOut + 0x40});
                drv::SimpleRigidBody_SetOrientMat(&b, m4);
                break;
        }
        std::string why;
        if (!same_bytes("o corpo", e.m.mem(kB, 0x40), reinterpret_cast<const uint8_t*>(&b), 0x40, why)) {
            tt.fail(why);
        } else if (ret_e != ret_p) {
            tt.fail("o retorno: original " + hex(ret_e) + ", porte " + hex(ret_p));
        } else if (op >= 5 && op <= 7 &&
                   !same_bytes("o eixo", e.m.mem(kOut, 16), reinterpret_cast<const uint8_t*>(out_p), 16, why)) {
            tt.fail(why);
        }
    }
    for (int i = 0; i < 9; ++i) report(names[i], t[i], failures);
}

// ---------------------------------------------------------------- b) DetectCollisionsSRB

void check_detect(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    uint64_t hits = 0;
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        random_scene(r, e, sc, r.chance(0.5) ? 4.0f : 15.0f);
        sim_to_emu(e, sc);
        ++t.cases;
        e.call(kFnDetect, {kSim});
        drv::DetectCollisionsSRB(drv::simulation());
        std::string why;
        if (!compare_sim(e, why)) t.fail(why);
        for (const drv::SimCollisionMap& h : drv::simulation().srb_hits)
            for (uint64_t w : h.bits)
                for (; w; w &= w - 1) ++hits;
    }
    report("Simulation::DetectCollisionsSRB", t, failures);
    std::printf("    (%" PRIu64 " choques marcados)\n", hits);
}

// ---------------------------------------------------------------- c) a explosão

// O PhysicsObject de teste do porte de um índice ordenado (ou nulo).
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

// A explosão do original (a imagem na memória emulada) x a do porte: as palavras do PhysicsObject e do Explosion.
bool compare_explosion(Emu& e, uint32_t emu, drv::Explosion* x, std::string& why) {
    MipsMachine& m = e.m;
    const drv::PhysicsObjectWords& po = x->po;
    const uint32_t want[] = {uint32_t(po.type), uint32_t(po.flags) | uint32_t(uint16_t(po.slot)) << 16, 0, 0, 0, 0,
                             po.owner_sig, 0, 0, 0, po.vtable};
    for (uint32_t i = 0; i < 11; ++i) {
        const uint32_t v = m.read32(emu + 0x40 + 4 * i);
        if (v != want[i]) {
            why = "a explosão +0x" + hex(0x40 + 4 * i).substr(5) + ": original " + hex(v) + ", porte " + hex(want[i]);
            return false;
        }
    }
    const drv::ExplosionData& d = x->d;
    if (m.read32(emu + 0x6c) != uint32_t(d.kind) || m.read32(emu + 0x70) != ps2::fbits(d.damage)) {
        why = "a explosão: o tipo ou o dano";
        return false;
    }
    const int src_e = m.read32(emu + 0x74) ? e.owner_of(m.read32(emu + 0x74)) : -1;
    const int src_p = d.source ? port_ordered(nf::p32_ptr<drv::PhysicsObject>(d.source)) : -1;
    if (src_e != src_p) {
        why = "a explosão: quem causou (" + std::to_string(src_e) + " x " + std::to_string(src_p) + ")";
        return false;
    }
    return same_bytes("a explosão +0x78", m.mem(emu + 0x78, 0x18), reinterpret_cast<const uint8_t*>(d.seen), 0x18, why);
}

// Os vetores do original (+0xdfc os mísseis, +0xe2c as explosões): os endereços em ordem.
std::vector<uint32_t> emu_vector(Emu& e, uint32_t off) {
    std::vector<uint32_t> v;
    const uint32_t b = e.m.read32(kSim + off), en = e.m.read32(kSim + off + 4);
    for (uint32_t a = b; a && a < en; a += 4) v.push_back(e.m.read32(a));
    return v;
}

// A mesma Simulation nos dois lados, um passo: o UpdatePosition de cada corpo simples com dono, o
// DetectCollisionsSRB e os Simulate das explosões e dos mísseis vivos (as de teste não fazem nada).
void step_both(Emu& e, Tally& t, std::map<uint32_t, drv::PhysicsObject*>& pairs) {
    drv::Simulation& sim = drv::simulation();
    for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
        if (!sim.simple_owners[i]) continue;
        e.call(kFnSrbUpdate, {kSrbVec + 0x40u * uint32_t(i)});
        drv::SimpleRigidBody_UpdatePosition(drv::GetSimpleRigidBody(sim, i));
    }
    e.call(kFnDetect, {kSim});
    drv::DetectCollisionsSRB(sim);
    // As explosões e os mísseis na ordem do porte (o tipo 3 antes do 6, os iguais na ordem em que nasceram).
    std::vector<std::pair<uint32_t, drv::PhysicsObject*>> live;
    for (const auto& [ea, po] : pairs) {
        const bool alive_p = std::find(sim.explosions.begin(), sim.explosions.end(), po) != sim.explosions.end() ||
                             std::find(sim.missiles.begin(), sim.missiles.end(), po) != sim.missiles.end();
        if (alive_p) live.push_back({ea, po});
    }
    std::sort(live.begin(), live.end(), [&](const auto& a, const auto& b) {
        const int ka = a.second->kind(), kb = b.second->kind();
        if (ka != kb) return ka < kb;
        return sim.simple_seq[static_cast<drv::SimpleObject*>(a.second)->po.slot] <
               sim.simple_seq[static_cast<drv::SimpleObject*>(b.second)->po.slot];
    });
    for (const auto& [ea, po] : live) {
        if (po->kind() == 3) {
            e.call(kFnExplosionSimulate, {ea});
        } else {
            e.call(0x181c80, {ea});  // Missile::Simulate
        }
        po->simulate();
    }
    ++sim.steps;
    e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
    (void)t;
}

void check_explosion(Emu& e, Rng& r, int cases, int& failures) {
    Tally t_spawn, t_sim, t_event;
    uint64_t forces = 0, damages = 0, deleted = 0, steps = 0;
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        random_scene(r, e, sc, 6.0f);
        drv::Simulation& sim = drv::simulation();
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.auto_drive = r.chance(0.2);
        std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
        e.m.write32(kMission + 0x350, uint32_t(mm.auto_drive));
        sim_to_emu(e, sc);
        // A explosão: o tipo, o dano, o lugar, quem causou (um dono ou nada), a assinatura.
        const int32_t kind = r.range(0, 6);
        const float damage = r.chance(0.1) ? r.real(0, 2) : r.real(0, 2500);
        float pos[4] = {r.real(-5, 5), r.real(-1, 3), r.real(-5, 5), 1.0f};
        float dir[4] = {r.real(-1, 1), r.real(-1, 1), r.real(-1, 1), 0};
        int src = -1;
        if (r.chance(0.5) && (sc.rigid || sc.simple)) {
            for (int tries = 0; tries < 20 && src < 0; ++tries) {
                const int i = r.range(0, 0x8f);
                if (port_owner(i)) src = i;
            }
        }
        const uint32_t sig = r.chance(0.5) ? 0u : (r.next() & 0x7fffffff);
        put(e.m, kEvent, pos, 16);
        put(e.m, kEvent + 0x10, dir, 16);
        e.m.write32(kEvent + 0x20, sig);
        e.m.set_fpr(12, damage);
        e.calls.clear();
        e.notes.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        ++t_spawn.cases;
        e.call(kFnSpawnExplosionObject,
               {kSim, uint32_t(kind), 0, kEvent + 0x10, kEvent, src >= 0 ? image_of(src) : 0u, kEvent + 0x20});
        const uint32_t ex_e = uint32_t(e.m.gpr(R::v0));
        drv::Explosion* ex_p = drv::SpawnExplosionObject(sim, damage, kind, dir, pos, port_owner(src), sig);
        std::string why;
        if ((ex_e != 0) != (ex_p != nullptr)) {
            t_spawn.fail("só um lado criou a explosão");
            continue;
        }
        if (!ex_p) continue;
        sc.owned.emplace_back(ex_p);
        e.owner_index[ex_e] = drv::kRigidBodySlots + ex_p->po.slot;
        if (!compare_sim(e, why) || !compare_explosion(e, ex_e, ex_p, why)) {
            t_spawn.fail(why);
            continue;
        }
        std::map<uint32_t, drv::PhysicsObject*> pairs = {{ex_e, ex_p}};
        const int nsteps = r.range(1, 14);
        bool bad = false;
        for (int s = 0; s < nsteps && !bad; ++s) {
            ++t_sim.cases;
            ++steps;
            step_both(e, t_sim, pairs);
            const bool alive_e = !emu_vector(e, 0xe2c).empty();
            const bool alive_p = !sim.explosions.empty();
            std::string w2;
            if (alive_e != alive_p) w2 = "só um lado apagou a explosão";
            else if (!compare_sim(e, w2)) {
            } else if (!(w2 = compare_calls(g_port_calls, e.calls)).empty()) {
            } else if (!(w2 = compare_notes(drv::effects_trace(), e.notes)).empty()) {
            } else if (!compare_explosion(e, ex_e, ex_p, w2)) {
            }
            if (!w2.empty()) {
                t_sim.fail("passo " + std::to_string(s) + ": " + w2);
                bad = true;
            }
            if (!alive_p) {
                ++deleted;
                break;
            }
        }
        damages += g_port_calls.size();
        forces = drv::effects_stats().forces;
    }
    report("Simulation::SpawnExplosionObject", t_spawn, failures);
    report("Explosion::Simulate (com o DetectCollisionsSRB)", t_sim, failures);
    std::printf("    (%" PRIu64 " passos, %" PRIu64 " chamadas aos donos, %" PRIu64 " apagadas)\n", steps, damages,
                deleted);
    (void)forces;
    (void)t_event;
}

// ---------------------------------------------------------------- d) os destrutores do ESpawnExplosion

// Os sons criados do lado do original (os ABaseSound alocados) e do porte (os primeiros da lista).
std::vector<SoundRec> emu_sounds(Emu& e, size_t from) {
    std::vector<SoundRec> v;
    for (size_t i = from; i < e.sounds.size(); ++i) {
        const uint32_t a = e.sounds[i];
        SoundRec s;
        const uint32_t mix = e.m.read32(a + 0x84);
        s.mix = mix >= kMixBase && mix < kMixBase + 6 ? kMixNames[mix - kMixBase] : "?";
        const uint32_t vt = e.m.read32(a + 0xac);
        s.limited = vt == 0x3c4c58 ? 1u : 0u;
        for (int k = 0; k < 3; ++k) {
            s.pos[k] = e.m.read32(a + 4 * uint32_t(k));
            s.vel[k] = e.m.read32(a + 0x10 + 4 * uint32_t(k));
        }
        s.min = e.m.read32(a + 0x5c);
        s.max = e.m.read32(a + 0x60);
        s.max2 = e.m.read32(a + 0x64);
        s.volume = e.m.read32(a + 0x6c);
        s.pitch = e.m.read32(a + 0x70);
        s.fx = s.limited || vt == 0x3c4a48 ? e.m.read32(a + 0x114) : 0u;
        s.lifetime = s.limited || vt == 0x3c4a48 ? e.m.read32(a + 0x118) : 0u;
        s.transit = vt == 0x3c4a48 || s.limited ? 0u : 1u;
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
        const std::string tn = typeid(*b).name();
        s.limited = tn.find("ALimitedSound") != std::string::npos ? 1u : 0u;
        drv::AOneShotSound* os = dynamic_cast<drv::AOneShotSound*>(b);
        s.transit = os ? 0u : 1u;
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
        return "som " + std::to_string(i) + ": original " + (he ? em[i].text() : std::string("nada")) + ", porte " +
               (hp ? p[i].text() : std::string("nada"));
    }
    return "";
}

// Os globais do .data do ESpawnExplosion nos dois lados.
void effects_globals_to_emu(Emu& e) {
    const drv::EffectsGlobals& g = drv::effects_globals();
    e.m.write32(kExCounter, g.ex_counter);
    e.m.write32(kRing, g.ring);
    for (int i = 0; i < 4; ++i) e.m.write32(kRingTime + 4u * uint32_t(i), uint32_t(g.ring_time[i]));
    e.m.write32(kLimitedCount, uint32_t(g.limited_count));
}
bool compare_effects_globals(Emu& e, std::string& why) {
    const drv::EffectsGlobals& g = drv::effects_globals();
    bool ok = e.m.read32(kExCounter) == g.ex_counter && e.m.read32(kRing) == g.ring &&
              e.m.read32(kLimitedCount) == uint32_t(g.limited_count);
    for (int i = 0; i < 4; ++i) ok = ok && e.m.read32(kRingTime + 4u * uint32_t(i)) == uint32_t(g.ring_time[i]);
    if (!ok) why = "os globais do anel dos sons (0x32bbdc..0x32bc0c, 0x355ec0)";
    return ok;
}

// O que o porte usa como o RSceneObj do evento: a matriz sorteada (a mesma do gancho do original). O EFireWeapon do
// porte lê o +0x24 do RSceneObj, então lá o RSceneObj do porte é outro endereço: as anotações do original trocam o
// falso por ele.
float g_scene_matrix[4][4];
uint32_t g_scene_port = kSceneFake;
uint32_t scene_alias(uint32_t a) { return a == kSceneFake ? g_scene_port : a; }
bool port_instance_position(uint32_t, int32_t, float m[4][4]) {
    std::memcpy(m, g_scene_matrix, 64);
    return true;
}

void check_espawn(Emu& e, Rng& r, int cases, int& failures) {
    Tally t_dtor, t_static, t_spawn;
    uint64_t sounds = 0, gfx = 0;
    e.m.hook(kFnInstancePos, "RSceneObj::GetInstancePosition", [&e](MipsMachine& mm) {
        e.note(kFnInstancePos, scene_alias(uint32_t(mm.gpr(R::a0))), uint32_t(mm.gpr(R::a1)));
        put(mm, uint32_t(mm.gpr(R::a2)), g_scene_matrix, 64);
        mm.set_gpr(R::v0, 0);
    });
    drv::EffectsEnv& env = drv::effects_env();
    const drv::EffectsEnv saved = env;
    env.instance_position = port_instance_position;
    static uint32_t s_gfx_answer = 0;
    env.gfx_lookup = [](const char*) { return s_gfx_answer; };
    static std::string s_level;
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        random_scene(r, e, sc, 6.0f, true);
        drv::Simulation& sim = drv::simulation();
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.auto_drive = r.chance(0.2);
        std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
        e.m.write32(kMission + 0x350, uint32_t(mm.auto_drive));
        // O carro do jogador debaixo d'água (GetPhysics +0xbc), o nível, a galeria, o TIMER e o anel.
        e.phys_underwater = r.chance(0.2) ? 1u : 0u;
        if (sc.player) std::memcpy(reinterpret_cast<uint8_t*>(&sc.player->car_physics) + 0xbc, &e.phys_underwater, 4);
        s_level = r.chance(0.2) ? "jungleb_mis13b" : "paris_mis01";
        env.level = s_level.c_str();
        put(e.m, kLevelName, s_level.c_str(), uint32_t(s_level.size() + 1));
        s_gfx_answer = r.chance(0.7) ? 0x00c0ffeeu : 0u;
        e.gfx_answer = s_gfx_answer;
        drv::EffectsGlobals& g = drv::effects_globals();
        g.ex_counter = uint32_t(r.range(0, 2));
        g.ring = r.next() & 0xff;
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        for (int i = 0; i < 4; ++i) g.ring_time[i] = e.timer + r.range(-60, 60);
        g.limited_count = r.range(0, 5);
        effects_globals_to_emu(e);
        sim_to_emu(e, sc);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) g_scene_matrix[i][j] = r.real(-1, 1);
        g_scene_matrix[3][0] = r.real(-10, 10);
        g_scene_matrix[3][1] = r.real(-2, 4);
        g_scene_matrix[3][2] = r.real(-10, 10);
        // A imagem do evento nos dois lados: o lugar e a direção, o RSceneObj (o falso), o objeto, o tipo, a arma, o
        // dano, a face, o efeito (uma cópia nos dois lados) e o nome.
        const bool is_static = r.chance(0.3);
        drv::PendingEvent pe{is_static ? 154 : 33, 0, 0.0f, 0, 0, 0};
        uint32_t img[20] = {};
        static uint8_t s_effect[0x40];
        static char s_name[32];
        const uint32_t kEffectEmu = kEvent + 0x100, kNameEmu = kEvent + 0x200, kDataEmu = kEvent + 0x300;
        static float s_data[4];
        if (!is_static) {
            const float p4[4] = {r.real(-8, 8), r.real(-1, 3), r.real(-8, 8), 1.0f};
            float d4[4] = {r.real(-1, 1), r.real(-1, 1), r.real(-1, 1), r.real(0, 1)};
            if (r.chance(0.3)) d4[0] = d4[2] = 0, d4[1] = 1, d4[3] = 1;
            std::memcpy(&img[1], p4, 16);
            std::memcpy(&img[5], d4, 16);
            img[9] = r.chance(0.3) ? kSceneFake : 0u;
            int obj = -1;
            if (r.chance(0.5)) {
                for (int tries = 0; tries < 20 && obj < 0; ++tries) {
                    const int i = r.range(0, 0x8f);
                    if (port_owner(i)) obj = i;
                }
            }
            img[10] = obj >= drv::kRigidBodySlots ? 1u : 0u;
            img[11] = obj < 0 ? 0xffffffffu : uint32_t(obj >= drv::kRigidBodySlots ? obj - drv::kRigidBodySlots : obj);
            img[13] = uint32_t(r.range(0, 6));
            static const int32_t kWeapons[] = {0, 1, 2, 0xe, 0x15, 0x1e, 0x1e};
            img[14] = uint32_t(kWeapons[r.next() % 7]);
            img[15] = ps2::fbits(r.chance(0.5) ? r.real(0, 300) : r.real(0, 2000));
            static const int32_t kFaces[] = {0, 1, 2, 3, 5, 6, 9, 10, 0x50, 0x60, 7};
            img[16] = uint32_t(kFaces[r.next() % 11]);
            const bool with_effect = r.chance(0.3);
            if (with_effect) {
                for (int i = 0; i < 0x40; i += 4) {
                    const float f = r.real(-3, 3);
                    std::memcpy(s_effect + i, &f, 4);
                }
                float q[4];
                random_quat(r, q);
                std::memcpy(s_effect + 0x20, q, 16);
                const uint32_t flags = r.chance(0.5) ? 0u : (r.next() & 0x10ff);
                std::memcpy(s_effect + 0x10, &flags, 4);
                s_effect[0x15] = uint8_t(r.range(0, 30));
                s_effect[0x17] = uint8_t(r.range(0, 9));
                const uint32_t ref = r.chance(0.5) ? 0u : 0x00beef00u;
                std::memcpy(s_effect + 0x18, &ref, 4);
                put(e.m, kEffectEmu, s_effect, 0x40);
            }
            const bool with_name = r.chance(0.2);
            if (with_name) {
                std::snprintf(s_name, sizeof s_name, "%s", r.chance(0.5) ? "Ex_Big" : "Ex_Other");
                put(e.m, kNameEmu, s_name, 32);
            }
            img[18] = ps2::fbits(r.real(0, 2));
            // O original: os endereços emulados; o porte: os p32.
            uint32_t ei[20];
            std::memcpy(ei, img, sizeof ei);
            ei[0] = 0x370498;
            ei[12] = obj < 0 ? 0u : image_of(obj);
            ei[17] = with_effect ? kEffectEmu : 0u;
            ei[19] = with_name ? kNameEmu : 0u;
            put(e.m, kEvent, ei, 0x50);
            pe.size = 0x50;
            std::memcpy(pe.obj, img, sizeof img);
            pe.obj[0] = 0x370498;
            pe.obj[12] = obj < 0 ? 0u : nf::p32_u(port_owner(obj));
            pe.obj[17] = with_effect ? nf::p32_u(s_effect) : 0u;
            pe.obj[19] = with_name ? nf::p32_u(s_name) : 0u;
        } else {
            // O ESpawnExplosionStatic (0x20 bytes): +4 o lugar (COORD3, ou nulo), +8 o gatilho, +0xc o tipo, +0x10 a
            // arma, +0x14 o dano, +0x18 a face, +0x1c o nome.
            for (float& f : s_data) f = r.real(-8, 8);
            const bool with_pos = r.chance(0.8);
            put(e.m, kDataEmu, s_data, 16);
            uint32_t ei[8] = {0x370ac8, with_pos ? kDataEmu : 0u, 0, uint32_t(r.range(0, 6)),
                              uint32_t(r.chance(0.2) ? 0x1e : r.range(0, 3)), ps2::fbits(r.real(0, 1500)),
                              uint32_t(r.range(0, 10)), 0};
            put(e.m, kEvent, ei, 0x20);
            pe.size = 0x20;
            std::memcpy(pe.obj, ei, sizeof ei);
            pe.obj[1] = with_pos ? nf::p32_u(s_data) : 0u;
        }
        // Os dois lados.
        e.calls.clear();
        e.notes.clear();
        e.events.clear();
        const size_t emu_sounds_before = e.sounds.size();
        g_port_calls.clear();
        drv::effects_trace().clear();
        drv::trigger_manager().pending.clear();
        const size_t port_sounds_before = drv::sound_list().size();
        const size_t ex_before_p = sim.explosions.size();
        Tally& tt = is_static ? t_static : t_dtor;
        ++tt.cases;
        e.call(is_static ? kFnESpawnExplosionStaticDtor : kFnESpawnExplosionDtor, {kEvent, 0});
        if (is_static) drv::ESpawnExplosionStatic_Execute(pe);
        else drv::ESpawnExplosion_Execute(pe);
        std::string why;
        if (is_static) {
            // O static só cria o ESpawnExplosion (o anel dos eventos): a imagem.
            const auto& pend = drv::trigger_manager().pending;
            if (e.events.size() != pend.size()) {
                why = "os eventos criados: " + std::to_string(e.events.size()) + " x " + std::to_string(pend.size());
            } else if (!pend.empty()) {
                uint32_t want[20];
                std::memcpy(want, pend[0].obj, sizeof want);
                for (uint32_t i = 0; i < 20 && why.empty(); ++i) {
                    const uint32_t v = e.m.read32(e.events[0] + 4 * i);
                    if (v != want[i]) why = "o ESpawnExplosion +0x" + hex(4 * i).substr(6) + ": " + hex(v) + " x " + hex(want[i]);
                }
            }
        } else {
            std::vector<uint32_t> ex_e = emu_vector(e, 0xe2c);
            const size_t n_p = sim.explosions.size() - ex_before_p;
            if (ex_e.size() != n_p) {
                why = "as explosões criadas: " + std::to_string(ex_e.size()) + " x " + std::to_string(n_p);
            } else if (!compare_sim(e, why)) {
            } else if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
            } else if (!(why = compare_sounds(port_sounds(port_sounds_before), emu_sounds(e, emu_sounds_before))).empty()) {
            } else if (!compare_effects_globals(e, why)) {
            } else if (n_p) {
                drv::Explosion* x = static_cast<drv::Explosion*>(sim.explosions.back());
                sc.owned.emplace_back(x);
                compare_explosion(e, ex_e.back(), x, why);
            }
            sounds += drv::sound_list().size() - port_sounds_before;
            gfx += drv::effects_stats().gfx_triggers;
        }
        if (!why.empty()) tt.fail(why);
        // Os sons do porte: apagados (o PlayVoices não roda aqui).
        while (drv::sound_list().size() > port_sounds_before) drv::sound_list().front()->destroy();
        drv::trigger_manager().pending.clear();
    }
    env = saved;
    report("ESpawnExplosion::~ESpawnExplosion", t_dtor, failures);
    report("ESpawnExplosionStatic::~ESpawnExplosionStatic", t_static, failures);
    std::printf("    (%" PRIu64 " sons criados)\n", sounds);
    (void)gfx;
    (void)t_spawn;
}

// ---------------------------------------------------------------- o mundo de uma pista (para o míssil e o segmento)

struct WorldCtx {
    drv::Track track;
    drv::TrackWorld tw;
    WorldCopy wc;
    std::vector<std::array<float, 3>> spots;  // pontos com chão embaixo (o y: a altura do chão)
    bool ok = false;
};

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
    // O WWorld falso (as instâncias de render e os objetos de cena), como na parte 7.
    const uint32_t nri = w.tw.render_instance_count();
    if (kSceneTable + 32 * nri > kSceneObjs || kSceneObjs + 64 * nri > kHeap) {
        std::printf("  %s: as instâncias de render não cabem\n", name);
        return false;
    }
    e.m.write32(kWorldFake + 0x20, w.wc.render);
    e.m.write32(kWorldFake + 0x24, kSceneTable);
    e.m.write32(kWorldFake + 0x28, nri);
    e.m.write32(0x33aac8, kWorldFake);  // WWorld::fgWorld
    for (uint32_t i = 0; i < nri; ++i) {
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
    // Os pontos: perto dos objetos de colisão e sorteados na pista, com chão embaixo.
    for (uint32_t i = 0; i < w.tw.object_count() && w.spots.size() < 200; ++i) {
        const drv::WCollisionObject& o = w.tw.objects()[i];
        float p[3] = {o.position[0] + r.real(-6, 6), o.position[1] + 3.0f, o.position[2] + r.real(-6, 6)};
        float h;
        if (w.tw.world_height_at_point(p, h)) w.spots.push_back({p[0], h, p[2]});
    }
    for (int tries = 0; tries < 40000 && w.spots.size() < 600; ++tries) {
        // As instâncias de colisão dão a região: a origem do mundo no referencial delas, desfeita.
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
    std::printf("  %s: %zu pontos com chão, %u objetos de colisão, %.1f MB copiados\n", name, w.spots.size(),
                w.tw.object_count(), double(w.wc.bytes) / 1048576.0);
    return w.ok;
}

// O WorldCollisionInfo do original x o do porte (os ponteiros pelos índices: a barreira e a face pela instância, o
// objeto pelo índice).
std::string compare_info(Emu& e, uint32_t emu, const drv::WorldCollisionInfo& p, const WorldCtx& w) {
    uint8_t raw[0x60];
    std::memcpy(raw, e.m.mem(emu, 0x60), 0x60);
    drv::WorldCollisionInfo q;
    std::memcpy(&q, raw, 0x60);
    auto inst = [&](uint32_t a) { return a ? nf::p32_u(w.tw.instances()) + (a - w.wc.insts) : 0u; };
    q.barrier.instance = inst(q.barrier.instance);
    q.face_instance = inst(q.face_instance);
    if (q.object) q.object = nf::p32_u(w.tw.objects()) + (q.object - w.wc.objs);
    // A construção inline do original não zera o +0x4c, o +0x56 e o +0x5c (o lixo da pilha vai junto na cópia do
    // choque achado): não comparados.
    drv::WorldCollisionInfo pp = p;
    q.unknown4c = pp.unknown4c = 0;
    q.unknown56 = pp.unknown56 = 0;
    q.unknown5c = pp.unknown5c = 0;
    std::string why;
    same_bytes("o WorldCollisionInfo", reinterpret_cast<const uint8_t*>(&q), reinterpret_cast<const uint8_t*>(&pp), 0x60,
               why);
    return why;
}

// ---------------------------------------------------------------- f) WCollisionMgr::CheckHitWorld

void check_hitworld(Emu& e, Rng& r, WorldCtx& w, const char* name, int cases, int& failures) {
    Tally t;
    uint64_t hits[4] = {};
    const uint32_t mgr = e.m.read32(0x33a540);  // WCollisionMgr::fgCollisionMgr
    for (int k = 0; k < cases; ++k) {
        const auto& sp = w.spots[r.next() % w.spots.size()];
        alignas(16) float seg[2][4];
        seg[0][0] = sp[0] + r.real(-3, 3);
        seg[0][1] = sp[1] + r.real(0.1f, 6);
        seg[0][2] = sp[2] + r.real(-3, 3);
        seg[0][3] = r.chance(0.5) ? 0.0f : 1.0f;
        const float len = r.chance(0.5) ? r.real(0.2f, 3) : r.real(3, 40);
        float d[3] = {r.real(-1, 1), r.chance(0.3) ? r.real(-1, -0.2f) : r.real(-0.3f, 0.3f), r.real(-1, 1)};
        const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-6f;
        for (int i = 0; i < 3; ++i) seg[1][i] = seg[0][i] + d[i] / n * len;
        if (w.tw.object_count() && r.chance(0.5)) {
            // Na direção de um objeto de colisão (os cilindros e as caixas).
            const drv::WCollisionObject& o = w.tw.objects()[r.next() % w.tw.object_count()];
            const float a = r.real(0, 6.2831853f), dist = r.real(1, 15);
            seg[0][0] = o.position[0] + std::cos(a) * dist;
            seg[0][1] = o.position[1] + r.real(-0.5f, 1.5f) * o.half[1];
            seg[0][2] = o.position[2] + std::sin(a) * dist;
            for (int i = 0; i < 3; ++i) seg[1][i] = o.position[i] + r.real(-0.5f, 0.5f) + (o.position[i] - seg[0][i]) * r.real(0, 0.6f);
            seg[1][1] = seg[0][1] + r.real(-0.5f, 0.5f);
        }
        seg[1][3] = 1.0f;
        constexpr uint32_t kSeg = kEvent + 0x400, kInfo = kEvent + 0x500;
        put(e.m, kSeg, seg, 32);
        drv::WorldCollisionInfo info;
        drv::WorldCollisionInfo_Init(info);
        put(e.m, kInfo, &info, 0x60);
        ++t.cases;
        // Os vector<> das listas de instâncias e de objetos que o original monta a cada chamada vêm do heap emulado
        // (o free não faz nada aqui): devolvidos depois da chamada, ou 4000 casos esgotam o heap.
        const uint32_t heap0 = e.heap;
        e.call(0x215cc8, {mgr, kSeg, kInfo});
        e.heap = heap0;
        drv::WCollisionMgr_CheckHitWorld(&seg[0][0], info);
        const std::string why = compare_info(e, kInfo, info, w);
        if (!why.empty()) t.fail(why);
        ++hits[info.hit & 3];
    }
    char label[96];
    std::snprintf(label, sizeof label, "WCollisionMgr::CheckHitWorld (%s)", name);
    report(label, t, failures);
    std::printf("    (nada %" PRIu64 ", o chão %" PRIu64 ", barreiras %" PRIu64 ", objetos %" PRIu64 ")\n", hits[0],
                hits[1], hits[2], hits[3]);
}

// ---------------------------------------------------------------- e) o míssil

// A imagem do míssil do original x o Missile do porte: as palavras do PhysicsObject, o MissileData (menos o alvo e o
// som, comparados pelo índice e pelos campos), o WWorldPos e o som do voo.
bool compare_missile(Emu& e, uint32_t ma, drv::Missile* mp, const WorldCtx& w,
                     const std::map<uint32_t, uint32_t>& target_map, std::string& why) {
    MipsMachine& m = e.m;
    const drv::PhysicsObjectWords& po = mp->po;
    const std::pair<uint32_t, uint32_t> words[] = {{0x40, uint32_t(po.type)},
                                                   {0x44, uint32_t(po.flags) | uint32_t(uint16_t(po.slot)) << 16},
                                                   {0x58, po.owner_sig},
                                                   {0x68, po.vtable}};
    for (const auto& [off, want] : words) {
        if (m.read32(ma + off) != want) {
            why = "o míssil +0x" + hex(off).substr(5) + ": original " + hex(m.read32(ma + off)) + ", porte " + hex(want);
            return false;
        }
    }
    uint8_t d_emu[0x6c4];
    std::memcpy(d_emu, m.mem(ma + 0x6c, 0x6c4), 0x6c4);
    drv::MissileData d = mp->d;
    // O alvo: o endereço emulado do WTargetable -> o p32 do porte.
    uint32_t te;
    std::memcpy(&te, d_emu, 4);
    auto it = target_map.find(te);
    const uint32_t tmapped = te == 0 ? 0u : (it == target_map.end() ? 0xbad0000u : it->second);
    std::memcpy(d_emu, &tmapped, 4);
    const uint32_t zero = 0;
    std::memcpy(d_emu + 0x684, &zero, 4);  // +0x6f0 o som
    d.sound = 0;
    if (!same_bytes("o MissileData (a partir do +0x6c)", d_emu, reinterpret_cast<const uint8_t*>(&d), 0x6c4, why))
        return false;
    uint8_t wp_port[0x40], wp_emu[0x40];
    wp_to_emu(mp->world_pos_, w.tw, w.wc, wp_port);
    std::memcpy(wp_emu, m.mem(ma, 0x40), 0x40);
    std::memset(wp_port + 0x0c, 0, 4);  // o w do v0: o lixo da pilha do FindClosestFace no PS2 (driving/physics/world.h)
    std::memset(wp_emu + 0x0c, 0, 4);
    if (!same_bytes("o WWorldPos do míssil", wp_emu, wp_port, 0x40, why)) return false;
    // O som do voo (o ASound do +0x6f0).
    const uint32_t se = m.read32(ma + 0x6f0);
    if ((se != 0) != (mp->sound != nullptr)) {
        why = "o som do voo: só um lado tem";
        return false;
    }
    if (se) {
        const drv::ABaseSound& b = *reinterpret_cast<const drv::ABaseSound*>(mp->sound);
        const std::pair<uint32_t, float> f[] = {{0x00, b.pos[0]}, {0x04, b.pos[1]}, {0x08, b.pos[2]}, {0x10, b.vel[0]},
                                                {0x14, b.vel[1]}, {0x18, b.vel[2]}, {0x5c, b.min_distance},
                                                {0x60, b.max_distance}, {0x64, b.max_distance2}, {0x6c, b.volume},
                                                {0x70, b.pitch}};
        for (const auto& [off, v] : f) {
            if (m.read32(se + off) != ps2::fbits(v)) {
                why = "o som do voo +0x" + hex(off).substr(6) + ": original " + hex(m.read32(se + off)) + ", porte " +
                      hex(ps2::fbits(v));
                return false;
            }
        }
    }
    return true;
}

// Os eventos criados (os ESpawnExplosion): a imagem do original x a do porte (o objeto pelo índice ordenado).
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

// Os globais do Missile.obj, o alvo do mundo do WTargetPicker (0x33a970 +0x10) e os do WTargetable.obj.
void missile_globals_to_emu(Emu& e) {
    e.m.write32(0x33194c, uint32_t(drv::missile_globals().wiggle_toggle));
    e.m.write32(0x331950, ps2::fbits(drv::missile_globals().pitch));
    put(e.m, 0x33a970 + 0x10, drv::target_picker().world_target, 16);
    e.m.write32(0x33a970 + 0xa4, uint32_t(drv::target_picker().mode));
    e.m.write32(0x33a964, drv::target_globals().targets_in_world);
    e.m.write32(0x33a960, uint32_t(drv::target_globals().live));
}
bool compare_missile_globals(Emu& e, std::string& why) {
    const bool ok = e.m.read32(0x33194c) == uint32_t(drv::missile_globals().wiggle_toggle) &&
                    e.m.read32(0x331950) == ps2::fbits(drv::missile_globals().pitch) &&
                    e.m.read32(0x33a964) == drv::target_globals().targets_in_world &&
                    e.m.read32(0x33a960) == uint32_t(drv::target_globals().live);
    if (!ok) why = "os globais do Missile.obj e do WTargetable.obj (0x33194c, 0x331950, 0x33a960, 0x33a964)";
    return ok;
}

// O alvo do míssil recém-criado: o do original (na memória emulada) e o do porte vão para o mapa e são comparados.
bool map_new_target(Emu& e, uint32_t ma, drv::Missile* mp, std::map<uint32_t, uint32_t>& target_map, std::string& why) {
    const uint32_t te = e.m.read32(ma + 0x6c);
    const uint32_t tp = mp->d.target;
    if ((te != 0) != (tp != 0)) {
        why = "o alvo do míssil: só um lado tem";
        return false;
    }
    if (!te || target_map.count(te)) return true;
    target_map[te] = tp;
    uint8_t emu_t[0x50];
    std::memcpy(emu_t, e.m.mem(te, 0x50), 0x50);
    uint8_t port_t[0x50];
    std::memcpy(port_t, nf::p32_ptr<const uint8_t>(tp), 0x50);
    std::memset(emu_t + 0x48, 0, 4);  // o objeto (nenhum nos alvos do mundo)
    std::memset(port_t + 0x48, 0, 4);
    return same_bytes("o WTargetable novo", emu_t, port_t, 0x50, why);
}

// O mapa das janelas (o WCollisionMgr +0x14 do original: a raiz nova no heap emulado, um map vazio do STL do GCC 2.95;
// o do porte esvaziado) e o RRandom do render nos dois lados (as janelas o sorteiam).
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
    e.m.write32(0x333e60, drv::render_random().seed);
    e.m.write32(0x333e64, drv::render_random().multiplier);
}

// Os WTargetable dos casos (o porte: aqui; o original: kTargets).
alignas(16) drv::WTargetableData g_targets[4];

void check_missiles(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t_spawn, t_step, t_user, t_picker;
    uint64_t world_hits = 0, body_hits = 0, expired = 0, steps = 0, events = 0, explosions = 0;
    static const int32_t kWeapons[] = {2, 2, 2, 1, 0xe, 0x15, 0x16, 0x19, 0x1c, 0x1e, 0x1f, 0x10, 2, 1};
    drv::EffectsEnv& env = drv::effects_env();
    const drv::EffectsEnv saved = env;
    static uint32_t s_gfx = 0;
    env.gfx_lookup = [](const char*) { return s_gfx; };
    env.instance_position = port_instance_position;
    static std::string s_level;
    const drv::MissileStats ms0 = drv::missile_stats();
    // Os objetos de cena do mundo falso do load_world (o RAnimEngine::Handle de cada um: 0x80000000 | a instância).
    static std::vector<uint32_t> s_scene_objs;
    s_scene_objs.assign(16u * w.tw.render_instance_count(), 0);
    for (uint32_t i = 0; i < w.tw.render_instance_count(); ++i) s_scene_objs[16u * i + 10] = 0x80000000u | i;
    static const drv::TrackWorld* s_tw = nullptr;
    s_tw = &w.tw;
    drv::ShellsEnv& senv = drv::shells_env();
    const drv::ShellsEnv saved_senv = senv;
    senv.scene_obj_of = [](uint32_t render) {
        const uint32_t i = (render - nf::p32_u(s_tw->render_instances())) / 64u;
        return nf::p32_u(&s_scene_objs[16u * i]);
    };
    senv.stimuli = [](uint32_t, uint32_t, uint32_t, uint32_t) {};
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        drv::RRandom_StartUp(drv::render_random(), r.next());
        reset_window_map(e);
        Scene sc;
        random_scene(r, e, sc, 8.0f, true);
        drv::Simulation& sim = drv::simulation();
        // A cena vai para um ponto da pista.
        const auto& sp = w.spots[r.next() % w.spots.size()];
        for (int i = 0; i < drv::kRigidBodySlots; ++i) {
            if (!sim.owners[i]) continue;
            drv::RigidBody* rb = drv::GetRigidBody(sim, i);
            rb->pos[0] += sp[0];
            rb->pos[1] += sp[1] + 1.0f;
            rb->pos[2] += sp[2];
        }
        for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
            if (!sim.simple_owners[i]) continue;
            drv::SimpleRigidBody* b = drv::GetSimpleRigidBody(sim, i);
            b->pos[0] += sp[0];
            b->pos[1] += sp[1] + 1.0f;
            b->pos[2] += sp[2];
        }
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.auto_drive = r.chance(0.1);
        mm.cinematic_mode = r.chance(0.1) ? 1u : 0u;
        mm.missile = 0;
        std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
        e.m.write32(kMission + 0x350, uint32_t(mm.auto_drive));
        e.m.write32(kMission + 0x3cc, mm.cinematic_mode);
        s_level = r.chance(0.1) ? "snow1a" : "paris_mis01";
        env.level = s_level.c_str();
        put(e.m, kLevelName, s_level.c_str(), uint32_t(s_level.size() + 1));
        s_gfx = r.chance(0.8) ? 0x00c0ffeeu : 0u;
        e.gfx_answer = s_gfx;
        drv::EffectsGlobals& g = drv::effects_globals();
        g.ex_counter = uint32_t(r.range(0, 2));
        g.ring = r.next() & 0xff;
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        for (int i = 0; i < 4; ++i) g.ring_time[i] = e.timer + r.range(-60, 60);
        drv::MissileGlobals& mg = drv::missile_globals();
        mg.wiggle_toggle = r.range(0, 1);
        mg.pitch = 1.0f + 0.01f * float(r.range(0, 3));
        drv::TargetPickerData& tpk = drv::target_picker();
        tpk.mode = 0;
        for (int i = 0; i < 3; ++i) tpk.world_target[i] = sp[i] + r.real(-150, 150);
        tpk.world_target[3] = r.real(0, 1);
        drv::target_globals().targets_in_world = r.next() & 0xff;
        drv::target_globals().live = r.range(0, 3);
        effects_globals_to_emu(e);
        missile_globals_to_emu(e);
        e.phys_underwater = 0;
        sim_to_emu(e, sc);
        // O alvo do mundo do WTargetPicker (UpdateSelection, sem alvos registrados) nos dois lados, às vezes num
        // submarino (a física +0xc0: o choque com o mundo).
        if (r.chance(0.5)) {
            const uint32_t sub = r.chance(0.4) ? 1u : 0u;
            std::memcpy(reinterpret_cast<uint8_t*>(&sc.player->car_physics) + 0xc0, &sub, 4);
            e.m.write32(kPhys + 0xc0, sub);
            e.m.write32(0x33a970 + 0x28, 0);
            e.m.write32(0x33a970 + 0x70, 0);
            ++t_picker.cases;
            e.call(0x22aab0, {0x33a970});
            drv::WTargetPicker_UpdateWorldTarget();
            std::string wp;
            if (!same_bytes("o alvo do mundo (+0x10)", e.m.mem(0x33a970 + 0x10, 16),
                            reinterpret_cast<const uint8_t*>(drv::target_picker().world_target), 16, wp))
                t_picker.fail(wp);
            const uint32_t zero = 0;
            std::memcpy(reinterpret_cast<uint8_t*>(&sc.player->car_physics) + 0xc0, &zero, 4);
            e.m.write32(kPhys + 0xc0, 0);
        }
        // O míssil: a arma, a velocidade de quem atira, o dano, a direção, o lugar, a assinatura de quem atira, o alvo.
        const int32_t weapon = kWeapons[r.next() % (sizeof kWeapons / sizeof kWeapons[0])];
        const float speed = r.real(0, 45);
        const float damage = r.real(10, 3000);
        float dir[4] = {r.real(-1, 1), r.chance(0.3) ? r.real(-0.6f, -0.05f) : r.real(-0.15f, 0.25f), r.real(-1, 1), 0};
        float pos[4] = {sp[0] + r.real(-2, 2), sp[1] + r.real(0.3f, 4), sp[2] + r.real(-2, 2), 1.0f};
        uint32_t sig = 0;
        const double ps = double(r.next() & 0xffff) / 65536.0;
        if (ps < 0.6) sig = sim.rigid_sig[0];
        else if (ps < 0.75 && sc.rigid > 1) sig = sim.rigid_sig[r.range(1, sc.rigid - 1)];
        else if (ps < 0.85 && sc.simple) {
            for (int i = 0; i < drv::kSimpleBodySlots; ++i)
                if (sim.simple_owners[i]) sig = sim.simple_sig[i];
        }
        std::map<uint32_t, uint32_t> target_map;
        uint32_t target_e = 0, target_p = 0;
        const double pt = double(r.next() & 0xffff) / 65536.0;
        if (pt < 0.3) {
            drv::WTargetableData& t = g_targets[0];
            t = drv::WTargetableData{};
            const bool obj = pt < 0.1 && sc.rigid > 1;
            const int oi = obj ? r.range(1, sc.rigid - 1) : -1;
            if (obj) {
                std::memcpy(t.pos, drv::GetRigidBody(sim, oi)->pos, 12);
                t.kind = 1;
                t.object = nf::p32_u(port_owner(oi));
            } else {
                for (int i = 0; i < 3; ++i) t.pos[i] = pos[i] + dir[i] * r.real(5, 60) + r.real(-3, 3);
                t.kind = 0;
            }
            t.pos[3] = 1.0f;
            t.fixed = r.chance(0.5);
            t.refs = r.range(0, 2);
            uint8_t img[0x50];
            std::memcpy(img, &t, 0x50);
            const uint32_t eo = obj ? image_of(oi) : 0u;
            std::memcpy(img + 0x48, &eo, 4);
            put(e.m, kTargets, img, 0x50);
            target_e = kTargets;
            target_p = nf::p32_u(&t);
            target_map[target_e] = target_p;
        }
        const int32_t system = r.chance(0.5) ? r.range(7, 8) : r.range(0, 0x50);
        put(e.m, kEvent, pos, 16);
        put(e.m, kEvent + 0x10, dir, 16);
        e.m.write32(kEvent + 0x20, sig);
        e.calls.clear();
        e.notes.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        drv::trigger_manager().pending.clear();
        e.events.clear();
        size_t ev_from = 0;
        size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
        e.m.set_fpr(12, speed);
        e.m.set_fpr(13, damage);
        ++t_spawn.cases;
        e.call(0x1ffc40, {kSim, uint32_t(weapon), kEvent + 0x10, kEvent, kEvent + 0x20, target_e, uint32_t(system)});
        const uint32_t ma = uint32_t(e.m.gpr(R::v0));
        drv::Missile* mp = drv::SpawnMissileObject(sim, speed, damage, weapon, dir, pos, sig, target_p, system);
        std::string why;
        if ((ma != 0) != (mp != nullptr)) {
            t_spawn.fail("só um lado criou o míssil");
            continue;
        }
        if (!mp) continue;
        sc.owned.emplace_back(mp);
        e.owner_index[ma] = drv::kRigidBodySlots + mp->po.slot;
        std::map<uint32_t, drv::PhysicsObject*> pairs = {{ma, mp}};
        if (!map_new_target(e, ma, mp, target_map, why) || !compare_sim(e, why) ||
            !compare_missile(e, ma, mp, w, target_map, why) ||
            !(why = compare_notes(drv::effects_trace(), e.notes)).empty() || !compare_missile_globals(e, why)) {
            t_spawn.fail(why);
            continue;
        }
        // O míssil guiado pelo jogador (os tipos 0x16 e 0x1f): o gerente da missão aponta para ele e a direção pedida.
        if ((weapon == 0x16 || weapon == 0x1f || r.chance(0.05)) && r.chance(0.6)) {
            mm.missile = nf::p32_u(mp);
            e.m.write32(kMission + 0x358, ma);
            const float sx = r.real(-1, 1), sy = r.real(-1, 1);
            ++t_user.cases;
            e.m.set_fpr(12, sx);
            e.call(0x181df0, {ma});
            e.m.set_fpr(12, sy);
            e.call(0x181e30, {ma});
            drv::Missile_SetUserSteerX(*mp, sx);
            drv::Missile_SetUserSteerY(*mp, sy);
            if (!compare_missile(e, ma, mp, w, target_map, why)) t_user.fail(why);
        }
        const int nsteps = r.range(20, 160);
        for (int s = 0; s < nsteps; ++s) {
            ++t_step.cases;
            ++steps;
            e.notes.clear();
            e.calls.clear();
            g_port_calls.clear();
            drv::effects_trace().clear();
            drv::trigger_manager().pending.clear();
            ev_from = e.events.size();
            snd_e = e.sounds.size();
            snd_p = drv::sound_list().size();
            step_both(e, t_step, pairs);
            std::string w2 = compare_events(e, ev_from);
            // Os eventos do passo (os ESpawnExplosion), na ordem: o destrutor nos dois lados.
            const size_t nev = drv::trigger_manager().pending.size();
            if (w2.empty() && nev) {
                const std::vector<uint32_t> ex_before = emu_vector(e, 0xe2c);
                const size_t exp_before = sim.explosions.size();
                const std::vector<drv::PendingEvent> pend = drv::trigger_manager().pending;
                drv::trigger_manager().pending.clear();
                for (size_t i = 0; i < nev; ++i) {
                    e.call(kFnESpawnExplosionDtor, {e.events[ev_from + i], 0});
                    drv::ESpawnExplosion_Execute(pend[i]);
                }
                events += nev;
                const std::vector<uint32_t> ex_after = emu_vector(e, 0xe2c);
                if (ex_after.size() - ex_before.size() != sim.explosions.size() - exp_before) {
                    w2 = "as explosões dos eventos: " + std::to_string(ex_after.size() - ex_before.size()) + " x " +
                         std::to_string(sim.explosions.size() - exp_before);
                } else {
                    for (size_t i = 0; i < sim.explosions.size() - exp_before; ++i) {
                        drv::PhysicsObject* x = sim.explosions[exp_before + i];
                        const uint32_t xa = ex_after[ex_before.size() + i];
                        pairs[xa] = x;
                        sc.owned.emplace_back(x);
                        e.owner_index[xa] = drv::kRigidBodySlots + static_cast<drv::Explosion*>(x)->po.slot;
                        ++explosions;
                    }
                }
            }
            if (!w2.empty()) {
            } else if (!compare_sim(e, w2)) {
            } else if (!(w2 = compare_calls(g_port_calls, e.calls)).empty()) {
            } else if (!(w2 = compare_notes(drv::effects_trace(), e.notes)).empty()) {
            } else if (!(w2 = compare_sounds(port_sounds(snd_p), emu_sounds(e, snd_e))).empty()) {
            } else if (!compare_effects_globals(e, w2) || !compare_missile_globals(e, w2)) {
            } else if ((e.m.read32(kMission + 0x358) != 0) != (mm.missile != 0)) {
                w2 = "o míssil guiado do gerente";
            } else if (e.m.read32(0x333e60) != drv::render_random().seed) {
                w2 = "o RRandom (as janelas)";
            } else {
                for (const auto& [ea, po] : pairs) {
                    if (po->kind() == 6) {
                        if (!compare_missile(e, ea, static_cast<drv::Missile*>(po), w, target_map, w2)) break;
                    } else if (!compare_explosion(e, ea, static_cast<drv::Explosion*>(po), w2)) {
                        break;
                    }
                }
            }
            if (!w2.empty()) {
                t_step.fail("passo " + std::to_string(s) + ": " + w2);
                break;
            }
            if (sim.missiles.empty() && sim.explosions.empty()) break;
        }
        // Os sons de um tiro do porte: os AOneShotSound (o do voo vai com o míssil).
        drv::trigger_manager().pending.clear();
    }
    senv = saved_senv;
    const drv::MissileStats& ms = drv::missile_stats();
    world_hits = ms.world_hits - ms0.world_hits;
    body_hits = ms.body_hits - ms0.body_hits;
    expired = ms.expired - ms0.expired;
    env = saved;
    report("WTargetPicker::UpdateSelection (o alvo do mundo)", t_picker, failures);
    report("Simulation::SpawnMissileObject (Missile::Missile)", t_spawn, failures);
    report("Missile::SetUserSteerX/Y", t_user, failures);
    report("Missile::Simulate (o passo, os eventos, as explosões)", t_step, failures);
    std::printf("    (%" PRIu64 " passos; bateram no mundo %" PRIu64 ", em corpos %" PRIu64 ", expiraram %" PRIu64
                "; %" PRIu64 " eventos, %" PRIu64 " explosões)\n",
                steps, world_hits, body_hits, expired, events, explosions);
}

// ---------------------------------------------------------------- g) o EFireWeapon

uint32_t g_fire_system = 0;
int32_t g_fire_streak_type = 0, g_fire_streak_count = 0;
uint32_t g_fire_target = 0;
uint32_t port_instance_system(uint32_t, uint32_t) { return g_fire_system; }
int32_t port_lookup_int(uint32_t, const char* key) {
    return std::strcmp(key, "BULLET_STREAK_TYPE") == 0 ? g_fire_streak_type : 0;
}
int32_t port_streak_count(int32_t) { return g_fire_streak_count; }

void check_fire(Emu& e, Rng& r, WorldCtx& w, int cases, int& failures) {
    Tally t;
    uint64_t missiles = 0, sounds = 0;
    drv::EffectsEnv& env = drv::effects_env();
    const drv::EffectsEnv saved = env;
    env.instance_position = port_instance_position;
    env.instance_system = port_instance_system;
    env.lookup_int = port_lookup_int;
    env.streak_count = port_streak_count;
    int32_t cam_1f4 = 0;
    env.camera_1f4 = &cam_1f4;
    static uint32_t s_gfx = 0;
    env.gfx_lookup = [](const char*) { return s_gfx; };
    drv::weapons_env().selected_target = []() { return g_fire_target; };
    // O gerente das armas: o do porte, com a tabela copiada para o original.
    drv::SWeaponManagerData& wm = drv::weapons_create();
    e.m.write32(0x33a530, kWM);
    e.m.write32(kCamListPtrEmu, kCamList);
    e.m.write32(kCamList + 4, kCam);
    e.m.hook(0x1c9328, "RSceneObj::GetInstanceSystemID", [](MipsMachine& mm) { mm.set_gpr(R::v0, g_fire_system); });
    e.m.hook(0x22af38, "WTargetPicker::GetSelectedTarget", [&e](MipsMachine& mm) {
        e.note(0x22af38, g_fire_target ? kTargets : 0u);
        mm.set_gpr(R::v0, g_fire_target ? kTargets : 0u);
    });
    e.m.hook(0x22b6b8, "WTargetPicker::CycleToNextTarget", [&e](MipsMachine&) { e.note(0x22b6b8); });
    e.m.hook(0x1c7b60, "RSceneObj::UseInstance", [&e](MipsMachine& mm) {
        e.note(0x1c7b60, scene_alias(uint32_t(mm.gpr(R::a1))), uint32_t(mm.gpr(R::a2)));
    });
    e.m.hook(kFnLookupInt, "AttributeSet::LookupInt", [](MipsMachine& mm) {
        const std::string key = cstr(mm, uint32_t(mm.gpr(R::a1)));
        mm.set_gpr(R::v0, uint32_t(key == "BULLET_STREAK_TYPE" ? g_fire_streak_type : 0));
    });
    // Os sistemas que dão as armas do switch portado (os mísseis e as que não fazem nada).
    std::vector<int32_t> systems;
    for (int32_t wtype : {1, 2, 0xe, 0x15, 0x16, 0x19, 0x1c, 0x1e, 0x1f, 3, 4, 5, 6, 8, 9, 0xa, 0xb, 0xc, 0xd})
        for (int k = 0; k < 8 && drv::kWeaponSystems[wtype][k] != 0; ++k) systems.push_back(drv::kWeaponSystems[wtype][k]);
    systems.push_back(0x4f);  // nenhuma arma
    for (int k = 0; k < cases; ++k) {
        e.new_simulation();
        Scene sc;
        random_scene(r, e, sc, 8.0f, true);
        drv::Simulation& sim = drv::simulation();
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.auto_drive = 0;
        mm.missile = 0;
        std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
        drv::EffectsGlobals& g = drv::effects_globals();
        e.timer = r.range(0, 100000);
        drv::audio_set_timer(e.timer);
        s_gfx = 0x00c0ffeeu;
        e.gfx_answer = s_gfx;
        drv::MissileGlobals& mg = drv::missile_globals();
        mg.wiggle_toggle = r.range(0, 1);
        mg.pitch = 1.0f;
        (void)g;
        drv::target_picker().mode = 0;
        for (int i = 0; i < 3; ++i) drv::target_picker().world_target[i] = r.real(-500, 500);
        effects_globals_to_emu(e);
        missile_globals_to_emu(e);
        sim_to_emu(e, sc);
        // O gerente: a tabela do porte (o dano de cada arma sorteado) no original.
        drv::WeaponEntry* table = drv::weapon_entry(wm, 0);
        for (int i = 0; i < drv::kWeaponCount; ++i) table[i].damage = r.range(0, 900);
        uint8_t wimg[0x190];
        std::memcpy(wimg, &wm, 0x190);
        const uint32_t tbl = kWM + 0x200;
        std::memcpy(wimg + 0x10, &tbl, 4);
        put(e.m, kWM, wimg, 0x190);
        for (int i = 0; i < drv::kWeaponCount; ++i) {
            uint8_t ent[0x54];
            std::memcpy(ent, &table[i], 0x54);
            const uint32_t z = 0;
            std::memcpy(ent + 0x04, &z, 4);  // o nome (não lido aqui)
            const std::string sn = table[i].sound_name ? nf::p32_ptr<const char>(table[i].sound_name) : "";
            const uint32_t sa = kWM + 0x1000 + 32u * uint32_t(i);
            put(e.m, sa, sn.c_str(), uint32_t(sn.size() + 1));
            std::memcpy(ent + 0x44, table[i].sound_name ? &sa : &z, 4);
            put(e.m, tbl + 0x54u * uint32_t(i), ent, 0x54);
        }
        // A cena: o RSceneObj falso com o dono (o carro do jogador, um dono rígido que não é carro, ou nenhum).
        int owner = 0;
        const double po = double(r.next() & 0xffff) / 65536.0;
        if (po < 0.15) owner = -1;
        else if (po < 0.3 && sc.rigid > 1) {
            const int c = r.range(1, sc.rigid - 1);
            if (port_owner(c)->kind() != 1) owner = c;
        }
        const uint32_t scene = kSceneFake;
        e.m.write32(scene + 0x24, owner < 0 ? 0u : image_of(owner));
        g_fire_system = uint32_t(systems[r.next() % systems.size()]);
        g_fire_streak_type = r.range(0, 5);
        g_fire_streak_count = r.range(0, 4);
        e.m.write32(0x423f38 + 0x18u * uint32_t(g_fire_streak_type) + 0x14, uint32_t(g_fire_streak_count));
        const uint32_t counter = uint32_t(r.range(0, 4));
        e.m.write32(image_of(0) + 0x64, counter);
        if (sc.player) sc.player->d.unknown064 = counter;
        cam_1f4 = 0;
        e.m.write32(kCam + 0x1f4, 0);
        g_fire_target = 0;
        // A instância: a matriz sorteada perto de um ponto da pista (o lugar na linha 3, a direção na linha 2).
        const auto& sp = w.spots[r.next() % w.spots.size()];
        float q[4];
        random_quat(r, q);
        drv::VU0_quattom4(q, g_scene_matrix);
        g_scene_matrix[3][0] = sp[0] + r.real(-2, 2);
        g_scene_matrix[3][1] = sp[1] + r.real(0.5f, 3);
        g_scene_matrix[3][2] = sp[2] + r.real(-2, 2);
        g_scene_matrix[3][3] = 1.0f;
        const uint32_t index = uint32_t(r.range(0, 60));
        // A imagem do evento (0x18): +4 o efeito (nenhum), +8 o RSceneObj, +0xc a cópia, +0x10 o índice, +0x14 nada.
        const uint32_t ei[6] = {0x3710b0, 0, scene, 0x1234, index, 0};
        put(e.m, kEvent, ei, 0x18);
        drv::PendingEvent pe{10, 0, 0.0f, 0, 0, 0};
        pe.size = 0x18;
        std::memcpy(pe.obj, ei, sizeof ei);
        // O porte lê o +0x24 do RSceneObj: a imagem do porte (o PBondCar do carro, ou o p32 do dono de teste).
        static uint8_t s_scene[0x40];
        std::memset(s_scene, 0, sizeof s_scene);
        uint32_t own_p = 0;
        if (owner == 0 && sc.player) own_p = nf::p32_u(&sc.player->d);
        else if (owner > 0) own_p = 0;  // um dono que não é carro: o porte não o acha pela imagem (sem tiro de IA)
        std::memcpy(s_scene + 0x24, &own_p, 4);
        pe.obj[2] = nf::p32_u(s_scene);
        g_scene_port = pe.obj[2];
        if (owner > 0) {
            // Sem imagem do porte para o dono rígido de teste: só os donos que o porte acha (o jogador e nenhum).
            e.m.write32(scene + 0x24, 0);
        }
        e.calls.clear();
        e.notes.clear();
        e.events.clear();
        g_port_calls.clear();
        drv::effects_trace().clear();
        const size_t snd_e = e.sounds.size(), snd_p = drv::sound_list().size();
        const size_t mis_before = sim.missiles.size();
        ++t.cases;
        e.call(0x14ca60, {kEvent, 0});
        drv::EFireWeapon_Execute(pe);
        std::string why;
        const std::vector<uint32_t> me = emu_vector(e, 0xdfc);
        if (me.size() != sim.missiles.size() - mis_before) {
            why = "os mísseis criados: " + std::to_string(me.size()) + " x " + std::to_string(sim.missiles.size() - mis_before);
        } else if (!compare_sim(e, why)) {
        } else if (!(why = compare_notes(drv::effects_trace(), e.notes)).empty()) {
        } else if (!(why = compare_sounds(port_sounds(snd_p), emu_sounds(e, snd_e))).empty()) {
        } else if (e.m.read32(image_of(0) + 0x64) != (sc.player ? sc.player->d.unknown064 : counter)) {
            why = "o contador dos rastros (o dono +0x64)";
        } else if (e.m.read32(kCam + 0x1f4) != uint32_t(cam_1f4)) {
            why = "o RPlayerCamera +0x1f4";
        } else if (!me.empty()) {
            drv::Missile* mp = static_cast<drv::Missile*>(sim.missiles.back());
            sc.owned.emplace_back(mp);
            e.owner_index[me.back()] = drv::kRigidBodySlots + mp->po.slot;
            std::map<uint32_t, uint32_t> targets;
            if (map_new_target(e, me.back(), mp, targets, why)) compare_missile(e, me.back(), mp, w, targets, why);
            ++missiles;
        }
        sounds += drv::sound_list().size() - snd_p;
        if (!why.empty()) t.fail(why);
    }
    env = saved;
    g_scene_port = kSceneFake;
    drv::weapons_env().selected_target = nullptr;
    drv::weapons_destroy();
    report("EFireWeapon::~EFireWeapon", t, failures);
    std::printf("    (%" PRIu64 " mísseis, %" PRIu64 " sons)\n", missiles, sounds);
}

}  // namespace

int run_missile_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    std::printf("\n== o corpo simples, as explosões, o míssil e o tiro: o código original do EE (interpretador MIPS + "
                "VU0) x o porte ==\n");
    int failures = 0;
    std::unique_ptr<Emu> e(new Emu());
    e->setup(elf, vu0_program, vu0_size);
    Rng r(seed ^ 0x3e15);
    check_srb_methods(*e, r, cases * 9 / 4, failures);
    check_detect(*e, r, cases / 4, failures);
    drv::SMissionManager_Construct(false);
    drv::effects_trace_enabled() = true;
    check_explosion(*e, r, cases / 4, failures);
    check_espawn(*e, r, cases / 2, failures);
    {
        WorldCtx w;
        if (load_world(*e, disc, "paris_mis01", w, r)) {
            check_hitworld(*e, r, w, "paris_mis01", cases, failures);
            check_missiles(*e, r, w, cases / 4, failures);
            check_fire(*e, r, w, cases / 2, failures);
        } else {
            ++failures;
        }
        drv::set_world(nullptr);
    }
    for (const char* name : {"snow1a_mis3", "junglea_mis13a", "snow2a_mis4"}) {
        WorldCtx w;
        if (load_world(*e, disc, name, w, r)) check_hitworld(*e, r, w, name, cases / 2, failures);
        else ++failures;
        drv::set_world(nullptr);
    }
    drv::effects_trace_enabled() = false;
    drv::effects_reset();
    drv::mission_reset();
    if (e->errors) std::printf("    o interpretador falhou %" PRIu64 " vezes: %s\n", e->errors, e->error.c_str());
    drv::simulation_reset(60);
    return failures;
}

}  // namespace drvcheck
