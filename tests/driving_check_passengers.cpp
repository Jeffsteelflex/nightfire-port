// nf_driving_check, parte 22 (`--so passageiros`; tests/driving_check_passengers.h): os passageiros dos carros da IA de
// Paris. O código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) ao lado do porte, com estados
// sorteados:
//  - o AICharacterEnemyWindow (0x369200), o AICharacterEnemySunroof (0x368f08) e o AICharacterEnemyDriver (0x3681f0): os
//    construtores sobre memória sorteada, o AICharacter::Execute (0x00118af0: o UpdateRotPos, o HandleInterrupts e o
//    estado pela tabela virtual do PS2), cada estado da tabela chamado direto, o TargetIsInRange, o TargetIsAcquired, o
//    PlayZoneInjuryAnim, o SetActor, o GetRotPos de três argumentos (a função da matriz do ator), o
//    VehicleIsAccelerating e os destrutores;
//  - o PBondCar::EnableTargetBeacon (0x0018ad50) e o DisableTargetBeacon (0x0018adc0);
//  - as sobreposições globais do ator: o ActGlobalPoseOverrideArray (CreateGlobalPoseOverrides 0x0010f140,
//    SetGlobalPoseOverride 0x0010f378, SetGlobalPoseOverrides 0x0010f620, BlendMatrices 0x0010f2b0, DoGlobalOverrides
//    0x0010f698, DeleteGlobalPoseOverrides 0x0010f230).
// O que os passageiros chamam do ator, do carro, da mira, do som e da missão é trocado pelos mesmos ganchos dos dois
// lados (passenger_api() e ai_character_actor_api() no porte; os endereços no original), e as chamadas são comparadas
// na ordem. O carro do original é a imagem do PBondCarData do porte com a tabela virtual do PS2 (0x379ef0): as
// entradas que os passageiros chamam (GetResetAvailable, GetAIGroundVehiclePtr, o EMP +0x144) rodam o código original.
// Os ponteiros (os nós dos atores, os Human, o carro, os outros passageiros, os alvos da mira) são comparados pelo que
// apontam.

#include "driving_check_passengers.h"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/anim/act_actor.h"
#include "driving/anim/actor.h"
#include "driving/game/ai_character.h"
#include "driving/game/ai_passengers.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;
namespace an = drv::anim;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções originais.
constexpr uint32_t kFnExecute = 0x118af0, kFnWinCtor = 0x1224b0, kFnSunCtor = 0x120b70, kFnDrvCtor = 0x11aca8;
constexpr uint32_t kFnAccel = 0x121c10, kFnEnable = 0x18ad50, kFnDisable = 0x18adc0;
constexpr uint32_t kFnRot3[3] = {0x122358, 0x120a18, 0x11ab50};
constexpr uint32_t kFnOvCreate = 0x10f140, kFnOvSet = 0x10f378, kFnOvSets = 0x10f620, kFnOvBlend = 0x10f2b0;
constexpr uint32_t kFnOvDo = 0x10f698, kFnOvDelete = 0x10f230;
constexpr uint32_t kVt[3] = {drv::kVtAICharacterEnemyWindow, drv::kVtAICharacterEnemySunroof,
                             drv::kVtAICharacterEnemyDriver};
constexpr uint32_t kVtBondCar = 0x379ef0;  // a tabela do PBondCar no PS2
// Os ganchos.
constexpr uint32_t kHkAtan2 = 0x2c0618, kHkCos = 0x2bfa48, kHkSin = 0x2bfc38;
constexpr uint32_t kHkSetNewAnim = 0x107c68, kHkIsDone = 0x10ac58, kHkLocalPosOri = 0x108d48, kHkWeaponPos = 0x108ec8;
constexpr uint32_t kHkTimeScale = 0x10ace0, kHkBone = 0x108438, kHkOvCreate = 0x1090c0, kHkOvSets = 0x109120;
constexpr uint32_t kHkRotate = 0x10afc0, kHkCrossNew = 0x107d08, kHkCrossStart = 0x1075e0, kHkIKs = 0x108f60;
constexpr uint32_t kHkIKInfo = 0x109050, kHkLookupFX = 0x1cad58, kHkLocateFX = 0x1cae00, kHkStimuli = 0x1a27a0;
constexpr uint32_t kHkGetACharacter = 0x118988, kHkSay = 0x2fbe00, kHkIncKills = 0x206960;
constexpr uint32_t kHkRegister = 0x22afa8, kHkUnregister = 0x22b098, kHkNewCharTarget = 0x229570;
constexpr uint32_t kHkNewObjTarget = 0x229518, kHkRemoveRef = 0x229bb0;
constexpr uint32_t kHkActivate = 0x17f540, kHkDeactivate = 0x17f570, kHkEnable = 0x18ad50, kHkDisable = 0x18adc0;
constexpr uint32_t kHkGetRigidBody = 0x1ff088, kHkAIVPos = 0x13e0b8, kHkAlloc = 0x2cfda8, kHkFree = 0x2cfe50;
constexpr uint32_t kHkKillActor = 0x10b4e0, kHkFastFree2 = 0x2d0a50, kHkNew = 0x2d09c8;
// Os globais do PS2.
constexpr uint32_t kGSim = 0x339190, kGRate = 0x339f4c, kGDt = 0x339f50, kGSteps = 0x339f54, kGCars = 0x339f5c;
constexpr uint32_t kGHumans = 0x339f80, kGMission = 0x33a024, kGDriverIK = 0x326950;
// A RAM emulada.
constexpr uint32_t kNodes = 0x01800000, kActors = 0x01800100, kActorSize = 0x80;
constexpr uint32_t kChar = 0x01801000, kOther[2] = {0x01801200, 0x01801400};
constexpr uint32_t kCarE[2] = {0x01802000, 0x01802800}, kRender = 0x01803000, kHPCar = 0x01803200;
constexpr uint32_t kHPChar = 0x01803210, kAIGV = 0x01804000, kTGV = 0x01804800, kTPos = 0x01804c00;
constexpr uint32_t kBodyP = 0x01806000, kInfoP = 0x01806100, kBodyE[2] = {0x01806200, 0x01806400};
constexpr uint32_t kInfoE[2] = {0x01806300, 0x01806500}, kCarImgP = 0x01806600, kCarsVec = 0x01806700;
constexpr uint32_t kHumansArr = 0x01806800, kHumanImg = 0x01806a00;
constexpr uint32_t kRand = 0x01807000, kMission = 0x01808000, kTarget = 0x01809000, kOut = 0x0180a000;
constexpr uint32_t kM = 0x0180b000, kOv = 0x0180c000, kOvBones = 0x0180c100, kOvLocal = 0x0180d000;
constexpr uint32_t kOvGlobals = 0x0180f000, kHeap = 0x01900000, kHeapEnd = 0x01a00000;
constexpr uint32_t kHandle = 0x00c0ffee;  // o RAnimEngine::Handle do carro (só passado adiante)
// O WakeUp (0x0012cb08) com o carro feito pelos ganchos: os passageiros comparados.
constexpr uint32_t kFnWakeUp = 0x12cb08;
constexpr uint32_t kHkSpawnCar = 0x1ff850, kHkVCGet = 0x13e678, kHkRegisterSimCar = 0x13f118, kHkResetCar = 0x187eb8;
constexpr uint32_t kHkLookupVector = 0x169e48, kHkNewActorHandle = 0x10b298, kHkSpawnHuman = 0x1ffb90;
constexpr uint32_t kHkSetAIChar = 0x17f5a0, kHkSetOwner = 0x195c30, kHkEventNew = 0x175730, kHkAICommand = 0x145fc8;
constexpr uint32_t kGActDB = 0x326000;
constexpr uint32_t kCtl = 0x01811000, kElem = 0x01811100, kSeats = 0x01811300, kActChar = 0x01811400;
constexpr uint32_t kActWeap = 0x01811c00;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 2222) {}
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
uint32_t fbits(float f) { return ps2::fbits(f); }
uint32_t name_hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
uint32_t words_hash(const uint32_t* w, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
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

struct Call {
    uint32_t fn = 0, a = 0, b = 0, c = 0;
    bool operator==(const Call& o) const { return fn == o.fn && a == o.a && b == o.b && c == o.c; }
    std::string text() const { return hex(fn) + "(" + hex(a) + "," + hex(b) + "," + hex(c) + ")"; }
};

// O caso: o que os ganchos devolvem.
struct Script {
    bool done[8] = {};
    float local[16] = {}, weapon[16] = {};
    float fxm[2][16] = {};
    uint32_t fx[2] = {};   // os handles dos efeitos "steerleft" e "steerright" (0: não achou)
    int32_t bone_bias = 0; // o osso de um nome: o hash mais isso (às vezes -1)
    bool no_bone = false;
    int next_node = 1, next_human = 0;  // o WakeUp: os handles e os Human entregues em ordem
    bool human_ok[3] = {true, true, true};
    bool recording = true;              // o WakeUp do original: as chamadas só depois do RegisterSimCar
};
Script g_s;
std::vector<Call> g_port_calls;
uint32_t g_port_token = 0;

// Os oito atores falsos dos dois lados (o nó com o ator em +8, o visível em +0x30).
an::ActActorData* g_port_actors = nullptr;
an::ActListNode* g_port_nodes = nullptr;
int port_actor_index(uint32_t actor) {
    for (int i = 0; i < 8; ++i)
        if (nf::p32_u(&g_port_actors[i]) == actor) return i;
    return 0xff;
}
int emu_actor_index(uint32_t actor) {
    if (actor >= kActors && actor < kActors + 8 * kActorSize && (actor - kActors) % kActorSize == 0)
        return int((actor - kActors) / kActorSize);
    return 0xff;
}
uint32_t emu_node(int i) { return kNodes + 12u * uint32_t(i); }
uint32_t port_node(int i) { return nf::p32_u(&g_port_nodes[i]); }
int emu_node_index(uint32_t h) {
    if (h == 0) return 0x80;
    for (int i = 0; i < 8; ++i)
        if (emu_node(i) == h) return i;
    return 0xff;
}
int port_node_index(uint32_t h) {
    if (h == 0) return 0x80;
    for (int i = 0; i < 8; ++i)
        if (port_node(i) == h) return i;
    return 0xff;
}
int32_t bone_of(uint32_t hash) {
    if (g_s.no_bone && (hash & 1)) return -1;
    return int32_t((hash + uint32_t(g_s.bone_bias)) % 40u);
}

// Os objetos do porte (o mundo de cada caso).
struct PortWorld {
    drv::BondCar* player = nullptr;
    drv::BondCar* car[2] = {};
    std::unique_ptr<drv::AIGroundVehicle> aigv, tgv;
    float car_hp = 1.0f, char_hp = 1.0f;
    alignas(16) uint8_t render[0x40] = {};
    alignas(16) uint8_t self[0x180] = {};
    alignas(16) uint8_t other[2][0x180] = {};
    drv::Human* humans[3] = {};
    drv::WTargetableData target{}, new_target{};
    int humans_n = 0;
};
PortWorld* g_w = nullptr;

uint32_t port_self() { return nf::p32_u(g_w->self); }
uint32_t port_other(int i) { return nf::p32_u(g_w->other[i]); }
// As marcas dos ponteiros: o carro (1, 2), o Human (1..3), o personagem (o próprio 1, os outros 2, 3), o alvo da mira
// (1 o que já estava, 2 o novo).
uint32_t port_car_mark(uint32_t p) {
    for (int i = 0; i < 2; ++i)
        if (g_w->car[i] && p == nf::p32_u(&g_w->car[i]->d)) return uint32_t(i + 1);
    return p == 0 ? 0u : 0xffu;
}
uint32_t emu_car_mark(uint32_t p) {
    for (int i = 0; i < 2; ++i)
        if (p == kCarE[i]) return uint32_t(i + 1);
    return p == 0 ? 0u : 0xffu;
}
uint32_t port_human_mark(uint32_t p) {
    for (int i = 0; i < 3; ++i)
        if (g_w->humans[i] && p == drv::Human_Image(*g_w->humans[i])) return uint32_t(i + 1);
    return p == 0 ? 0u : 0xffu;
}
uint32_t emu_human_mark(uint32_t p) {
    for (int i = 0; i < 3; ++i)
        if (p == kHumanImg + 0x100u * uint32_t(i)) return uint32_t(i + 1);
    return p == 0 ? 0u : 0xffu;
}
uint32_t port_char_mark(uint32_t p) {
    if (p == 0) return 0;
    if (p == port_self()) return 1;
    for (int i = 0; i < 2; ++i)
        if (p == port_other(i)) return uint32_t(i + 2);
    return 0xff;
}
uint32_t emu_char_mark(uint32_t p) {
    if (p == 0) return 0;
    if (p == kChar) return 1;
    for (int i = 0; i < 2; ++i)
        if (p == kOther[i]) return uint32_t(i + 2);
    return 0xff;
}
uint32_t port_target_mark(uint32_t p) {
    if (p == 0) return 0;
    if (p == nf::p32_u(&g_w->target)) return 1;
    if (p == nf::p32_u(&g_w->new_target)) return 2;
    return 0xff;
}
uint32_t g_emu_new_target = 0;
uint32_t emu_target_mark(uint32_t p) {
    if (p == 0) return 0;
    if (p == kTarget) return 1;
    if (p == g_emu_new_target && p != 0) return 2;
    return 0xff;
}

// Os ganchos do porte.
void install_port_api() {
    drv::AICharacterActorApi& a = drv::ai_character_actor_api();
    a.set_new_animation = [](uint32_t actor, int32_t bank, int32_t index) -> uint32_t {
        g_port_calls.push_back({kHkSetNewAnim, uint32_t(port_actor_index(actor)), uint32_t(bank), uint32_t(index)});
        return ++g_port_token;
    };
    a.is_animation_done = [](uint32_t actor) {
        const int i = port_actor_index(actor);
        g_port_calls.push_back({kHkIsDone, uint32_t(i)});
        return i < 8 ? g_s.done[i] : false;
    };
    a.local_pos_ori = [](uint32_t actor, float* out) {
        g_port_calls.push_back({kHkLocalPosOri, uint32_t(port_actor_index(actor))});
        std::memcpy(out, g_s.local, 64);
    };
    a.weapon_position = [](uint32_t actor, float* out, int32_t w) {
        g_port_calls.push_back({kHkWeaponPos, uint32_t(port_actor_index(actor)), uint32_t(w)});
        std::memcpy(out, g_s.weapon, 64);
    };
    drv::PassengerApi& p = drv::passenger_api();
    p.set_time_scale = [](uint32_t actor, float s) {
        g_port_calls.push_back({kHkTimeScale, uint32_t(port_actor_index(actor)), fbits(s)});
    };
    p.get_bone_index = [](uint32_t actor, const char* name) {
        const uint32_t h = name_hash(name);
        g_port_calls.push_back({kHkBone, uint32_t(port_actor_index(actor)), h});
        return bone_of(h);
    };
    p.create_overrides = [](uint32_t actor, int32_t n, const int32_t* bones, int32_t local) {
        g_port_calls.push_back({kHkOvCreate, uint32_t(port_actor_index(actor)) | uint32_t(n) << 8 | uint32_t(local) << 16,
                                uint32_t(bones[0]), uint32_t(bones[1])});
    };
    p.set_overrides = [](uint32_t actor, int32_t n, const float* m, const float* w) {
        uint32_t words[34];
        std::memcpy(words, m, 128);
        std::memcpy(words + 32, w, 8);
        g_port_calls.push_back({kHkOvSets, uint32_t(port_actor_index(actor)), uint32_t(n), words_hash(words, 34)});
    };
    p.rotate_actor = [](uint32_t actor, float t) {
        g_port_calls.push_back({kHkRotate, uint32_t(port_actor_index(actor)), fbits(t)});
    };
    p.set_new_crossfade = [](uint32_t actor, float t, int32_t bank, int32_t index) -> uint32_t {
        g_port_calls.push_back({kHkCrossNew, uint32_t(port_actor_index(actor)) | fbits(t) << 8, uint32_t(bank),
                                uint32_t(index)});
        return ++g_port_token;
    };
    p.start_new_crossfade = [](uint32_t ctl, float t, int32_t bank, int32_t index) {
        g_port_calls.push_back({kHkCrossStart, ctl ^ fbits(t), uint32_t(bank), uint32_t(index)});
    };
    p.create_iks = [](uint32_t actor, int32_t n, const int32_t* bones, const float* va, const float* vb) {
        uint32_t words[12];
        std::memcpy(words, va, 32);
        std::memcpy(words + 8, vb, 8);
        words[10] = uint32_t(bones[0]);
        words[11] = uint32_t(bones[1]);
        g_port_calls.push_back({kHkIKs, uint32_t(port_actor_index(actor)), uint32_t(n), words_hash(words, 12)});
    };
    p.set_ik_info_array = [](uint32_t actor, int32_t n, const float* info) {
        uint32_t words[24];
        std::memcpy(words, info, 96);
        g_port_calls.push_back({kHkIKInfo, uint32_t(port_actor_index(actor)), uint32_t(n), words_hash(words, 24)});
    };
    p.lookup_fx = [](uint32_t scene, const char* name) -> uint32_t {
        const uint32_t h = name_hash(name);
        g_port_calls.push_back({kHkLookupFX, scene == nf::p32_u(g_w->render) ? 1u : 0xffu, h});
        return std::strcmp(name, "steerleft") == 0 ? g_s.fx[0] : (std::strcmp(name, "steerright") == 0 ? g_s.fx[1] : 0u);
    };
    p.locate_fx = [](uint32_t scene, uint32_t fx, float* m) {
        g_port_calls.push_back({kHkLocateFX, scene == nf::p32_u(g_w->render) ? 1u : 0xffu, fx});
        std::memcpy(m, g_s.fxm[fx == g_s.fx[0] ? 0 : 1], 64);
    };
    p.process_stimuli = [](uint32_t h, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode) {
        g_port_calls.push_back({kHkStimuli, h, system | stimulus << 8 | mode << 16, steps});
    };
    p.say = [](uint32_t, const char* what) { g_port_calls.push_back({kHkSay, name_hash(what)}); };
    p.inc_kills = [](int32_t n) { g_port_calls.push_back({kHkIncKills, uint32_t(n)}); };
    p.register_target = [](uint32_t t) { g_port_calls.push_back({kHkRegister, port_target_mark(t)}); };
    p.unregister_target = [](uint32_t t) { g_port_calls.push_back({kHkUnregister, port_target_mark(t)}); };
    p.new_character_target = [](uint32_t ch) -> uint32_t {
        g_port_calls.push_back({kHkNewCharTarget, port_char_mark(ch)});
        return nf::p32_u(&g_w->new_target);
    };
    p.remove_reference = [](uint32_t t) { g_port_calls.push_back({kHkRemoveRef, port_target_mark(t)}); };
    p.human_activate = [](uint32_t img) { g_port_calls.push_back({kHkActivate, port_human_mark(img)}); };
    p.human_deactivate = [](uint32_t img) { g_port_calls.push_back({kHkDeactivate, port_human_mark(img)}); };
    p.enable_beacon = [](uint32_t car, int32_t create) {
        g_port_calls.push_back({kHkEnable, port_car_mark(car), uint32_t(create)});
    };
    p.disable_beacon = [](uint32_t car) { g_port_calls.push_back({kHkDisable, port_car_mark(car)}); };
    p.new_actor_handle = [](uint32_t user, const char* model, const char* weapon, uint32_t callback,
                            const float* m) -> uint32_t {
        uint32_t words[16];
        std::memcpy(words, m, 64);
        const uint32_t vt = nf::p32_ptr<const drv::AICharacterData>(user)->vtable;
        g_port_calls.push_back({kHkNewActorHandle, vt, name_hash(model) ^ name_hash(weapon) * 3u ^ callback,
                                words_hash(words, 16)});
        return port_node(g_s.next_node++ & 7);
    };
    p.spawn_human = [](const float dir[3], const float pos[3]) -> drv::Human* {
        uint32_t d[3], q[3];
        std::memcpy(d, dir, 12);
        std::memcpy(q, pos, 12);
        g_port_calls.push_back({kHkSpawnHuman, words_hash(d, 3), words_hash(q, 3)});
        const int k = g_s.next_human++;
        return k < 3 && g_s.human_ok[k] ? g_w->humans[k] : nullptr;
    };
    p.human_set_character = [](drv::Human& h, uint32_t ch) {
        g_port_calls.push_back({kHkSetAIChar, port_human_mark(drv::Human_Image(h)),
                                nf::p32_ptr<const drv::AICharacterData>(ch)->vtable});
    };
    p.human_set_owner = [](drv::Human& h, drv::BondCar& car) {
        g_port_calls.push_back({kHkSetOwner, port_human_mark(drv::Human_Image(h)), port_car_mark(nf::p32_u(&car.d))});
    };
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    std::vector<Call> calls;
    uint32_t token = 0, heap = kHeap;
    int errors = 0;

    std::string str(uint32_t a) {
        std::string s;
        for (; a && s.size() < 64; ++a) {
            const char c = char(m.mem(a, 1)[0]);
            if (!c) break;
            s += c;
        }
        return s;
    }

    // `passengers`: os ganchos dos passageiros (sem eles: as funções do carro, do alvo e das sobreposições rodam).
    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size, bool passengers) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto ret_f = [](MipsMachine& mm, float f) { mm.set_fpr(0, f); };
        m.hook(kHkAtan2, "atan2f", [ret_f](MipsMachine& mm) { ret_f(mm, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(kHkCos, "cosf", [ret_f](MipsMachine& mm) { ret_f(mm, nf::fm::cos(mm.fpr(12))); });
        m.hook(kHkSin, "sinf", [ret_f](MipsMachine& mm) { ret_f(mm, nf::fm::sin(mm.fpr(12))); });
        m.hook(kHkAlloc, "UMemory::FastAlloc", [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15u) & ~15u;
            if (heap + n > kHeapEnd) heap = kHeap;
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        });
        m.hook(kHkFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kHkNew, "operator new", [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15u) & ~15u;
            if (heap + n > kHeapEnd) heap = kHeap;
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        });
        m.hook(kHkFastFree2, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x2a8148, "printf", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        if (!passengers) {
            m.hook(kHkNewObjTarget, "WTargetable::WTargetable(PhysicsObject *)", [this](MipsMachine& mm) {
                calls.push_back({kHkNewObjTarget, emu_car_mark(uint32_t(mm.gpr(R::a1)))});
                mm.set_gpr(R::v0, mm.gpr(R::a0));
            });
            m.hook(kHkRegister, "WTargetPicker::RegisterTarget",
                   [this](MipsMachine& mm) { calls.push_back({kHkRegister, uint32_t(mm.gpr(R::a1)) != 0 ? 2u : 0u}); });
            return;
        }
        m.hook(kHkSetNewAnim, "ActActor::SetNewAnimation", [this](MipsMachine& mm) {
            calls.push_back({kHkSetNewAnim, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a1)),
                             uint32_t(mm.gpr(R::a2))});
            mm.set_gpr(R::v0, ++token);
        });
        m.hook(kHkIsDone, "ActActor::IsAnimationDone", [this](MipsMachine& mm) {
            const int i = emu_actor_index(uint32_t(mm.gpr(R::a0)));
            calls.push_back({kHkIsDone, uint32_t(i)});
            mm.set_gpr(R::v0, i < 8 && g_s.done[i] ? 1 : 0);
        });
        m.hook(kHkLocalPosOri, "ActActor::GetActorLocalPosOri", [this](MipsMachine& mm) {
            calls.push_back({kHkLocalPosOri, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0))))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 64), g_s.local, 64);
        });
        m.hook(kHkWeaponPos, "ActActor::GetWeaponPosition", [this](MipsMachine& mm) {
            calls.push_back({kHkWeaponPos, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a3))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 64), g_s.weapon, 64);
        });
        m.hook(kHkTimeScale, "ActActor::SetTimeScale", [this](MipsMachine& mm) {
            calls.push_back({kHkTimeScale, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), mm.fpr_bits(12)});
        });
        m.hook(kHkBone, "ActActor::GetBoneIndex", [this](MipsMachine& mm) {
            const uint32_t h = name_hash(str(uint32_t(mm.gpr(R::a1))).c_str());
            calls.push_back({kHkBone, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), h});
            mm.set_gpr(R::v0, int64_t(bone_of(h)));
        });
        m.hook(kHkOvCreate, "ActActor::CreateGlobalPoseOverrides", [this](MipsMachine& mm) {
            const uint32_t b = uint32_t(mm.gpr(R::a2));
            calls.push_back({kHkOvCreate,
                             uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))) | uint32_t(mm.gpr(R::a1)) << 8 |
                                 uint32_t(mm.gpr(R::a3)) << 16,
                             mm.read32(b), mm.read32(b + 4)});
        });
        m.hook(kHkOvSets, "ActActor::SetGlobalPoseOverrides", [this](MipsMachine& mm) {
            uint32_t words[34];
            std::memcpy(words, mm.mem(uint32_t(mm.gpr(R::a2)), 128), 128);
            std::memcpy(words + 32, mm.mem(uint32_t(mm.gpr(R::a3)), 8), 8);
            calls.push_back({kHkOvSets, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a1)),
                             words_hash(words, 34)});
        });
        m.hook(kHkRotate, "ActActor::RotateActor", [this](MipsMachine& mm) {
            calls.push_back({kHkRotate, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), mm.fpr_bits(12)});
        });
        m.hook(kHkCrossNew, "ActActor::SetNewCrossFadeAnimation", [this](MipsMachine& mm) {
            calls.push_back({kHkCrossNew, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))) | mm.fpr_bits(12) << 8,
                             uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2))});
            mm.set_gpr(R::v0, ++token);
        });
        m.hook(kHkCrossStart, "CrossFadeAnimationController::StartNewCrossFade", [this](MipsMachine& mm) {
            calls.push_back({kHkCrossStart, uint32_t(mm.gpr(R::a0)) ^ mm.fpr_bits(12), uint32_t(mm.gpr(R::a1)),
                             uint32_t(mm.gpr(R::a2))});
        });
        m.hook(kHkIKs, "ActActor::CreateIKs", [this](MipsMachine& mm) {
            uint32_t words[12];
            std::memcpy(words, mm.mem(uint32_t(mm.gpr(R::a3)), 32), 32);
            std::memcpy(words + 8, mm.mem(uint32_t(mm.gpr(R::t0)), 8), 8);
            words[10] = mm.read32(uint32_t(mm.gpr(R::a2)));
            words[11] = mm.read32(uint32_t(mm.gpr(R::a2)) + 4);
            calls.push_back({kHkIKs, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a1)),
                             words_hash(words, 12)});
        });
        m.hook(kHkIKInfo, "ActActor::SetIKInfoArray", [this](MipsMachine& mm) {
            uint32_t words[24];
            std::memcpy(words, mm.mem(uint32_t(mm.gpr(R::a2)), 96), 96);
            calls.push_back({kHkIKInfo, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a1)),
                             words_hash(words, 24)});
        });
        m.hook(kHkLookupFX, "RSceneObj::LookupFXHandle", [this](MipsMachine& mm) {
            const std::string name = str(uint32_t(mm.gpr(R::a1)));
            calls.push_back({kHkLookupFX, uint32_t(mm.gpr(R::a0)) == kRender ? 1u : 0xffu, name_hash(name.c_str())});
            mm.set_gpr(R::v0, name == "steerleft" ? g_s.fx[0] : (name == "steerright" ? g_s.fx[1] : 0u));
        });
        m.hook(kHkLocateFX, "RSceneObj::LocateFX", [this](MipsMachine& mm) {
            const uint32_t fx = uint32_t(mm.gpr(R::a1));
            calls.push_back({kHkLocateFX, uint32_t(mm.gpr(R::a0)) == kRender ? 1u : 0xffu, fx});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a2)), 64), g_s.fxm[fx == g_s.fx[0] ? 0 : 1], 64);
        });
        m.hook(kHkStimuli, "RAnimEngine::Handle::ProcessStimuli", [this](MipsMachine& mm) {
            calls.push_back({kHkStimuli, uint32_t(mm.gpr(R::a0)),
                             uint32_t(mm.gpr(R::a1)) | uint32_t(mm.gpr(R::a2)) << 8 | uint32_t(mm.gpr(R::t0)) << 16,
                             uint32_t(mm.gpr(R::a3))});
        });
        m.hook(kHkGetACharacter, "AICharacter::GetACharacter", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kHkSay, "ACharacter::Say",
               [this](MipsMachine& mm) { calls.push_back({kHkSay, name_hash(str(uint32_t(mm.gpr(R::a1))).c_str())}); });
        m.hook(kHkIncKills, "SMissionManager::IncKills",
               [this](MipsMachine& mm) { calls.push_back({kHkIncKills, uint32_t(mm.gpr(R::a1))}); });
        m.hook(kHkRegister, "WTargetPicker::RegisterTarget",
               [this](MipsMachine& mm) { calls.push_back({kHkRegister, emu_target_mark(uint32_t(mm.gpr(R::a1)))}); });
        m.hook(kHkUnregister, "WTargetPicker::UnregisterTarget",
               [this](MipsMachine& mm) { calls.push_back({kHkUnregister, emu_target_mark(uint32_t(mm.gpr(R::a1)))}); });
        m.hook(kHkNewCharTarget, "WTargetable::WTargetable(AICharacter *)", [this](MipsMachine& mm) {
            g_emu_new_target = uint32_t(mm.gpr(R::a0));
            calls.push_back({kHkNewCharTarget, emu_char_mark(uint32_t(mm.gpr(R::a1)))});
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(kHkRemoveRef, "WTargetable::RemoveReference",
               [this](MipsMachine& mm) { calls.push_back({kHkRemoveRef, emu_target_mark(uint32_t(mm.gpr(R::a0)))}); });
        m.hook(kHkActivate, "Human::Activate",
               [this](MipsMachine& mm) { calls.push_back({kHkActivate, emu_human_mark(uint32_t(mm.gpr(R::a0)))}); });
        m.hook(kHkDeactivate, "Human::Deactivate",
               [this](MipsMachine& mm) { calls.push_back({kHkDeactivate, emu_human_mark(uint32_t(mm.gpr(R::a0)))}); });
        m.hook(kHkEnable, "PBondCar::EnableTargetBeacon", [this](MipsMachine& mm) {
            calls.push_back({kHkEnable, emu_car_mark(uint32_t(mm.gpr(R::a0))), uint32_t(mm.gpr(R::a1))});
        });
        m.hook(kHkDisable, "PBondCar::DisableTargetBeacon",
               [this](MipsMachine& mm) { calls.push_back({kHkDisable, emu_car_mark(uint32_t(mm.gpr(R::a0)))}); });
        m.hook(kHkGetRigidBody, "Simulation::GetRigidBody", [this](MipsMachine& mm) {
            const int slot = int(int16_t(uint16_t(mm.gpr(R::a1))));
            uint32_t b = kBodyP;
            for (int i = 0; i < 2; ++i)
                if (g_w->car[i] && slot == g_w->car[i]->d.slot) b = kBodyE[i];
            mm.set_gpr(R::v0, b);
        });
        m.hook(kHkAIVPos, "AIVehicle::GetPosition", [](MipsMachine& mm) { mm.set_gpr(R::v0, kTPos); });
        m.hook(kHkKillActor, "ActActorDatabase::KillActorByHandle", [this](MipsMachine& mm) {
            calls.push_back({kHkKillActor, uint32_t(emu_node_index(uint32_t(mm.gpr(R::a1))))});
        });
        // O WakeUp: o carro pronto (a imagem do carro 0), o resto anotado.
        m.hook(kHkSpawnCar, "Simulation::SpawnCarObject", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCarE[0]); });
        m.hook(kHkResetCar, "PBondCar::ResetCar", [](MipsMachine&) {});
        m.hook(kHkVCGet, "AIVehicleController::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x0180f800); });
        m.hook(kHkRegisterSimCar, "AIVehicleController::RegisterSimCar", [](MipsMachine&) { g_s.recording = true; });
        m.hook(kHkEventNew, "Event::operator new", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x0180f900); });
        m.hook(kHkAICommand, "EAICommand::EAICommand", [](MipsMachine&) {});
        m.hook(kHkLookupVector, "AttributeSet::LookupVector", [this](MipsMachine& mm) {
            const std::string k = str(uint32_t(mm.gpr(R::a1)));
            const int i = k == "AIC_EWIND_POS" ? 0 : (k == "AIC_ESUNR_POS" ? 1 : 2);
            mm.set_gpr(R::v0, kSeats + 0x10u * uint32_t(i));
        });
        m.hook(kHkNewActorHandle, "ActActorDatabase::GetNewActorHandle", [this](MipsMachine& mm) {
            uint32_t words[16];
            std::memcpy(words, mm.mem(uint32_t(mm.gpr(R::t1)), 64), 64);
            const uint32_t vt = mm.read32(uint32_t(mm.gpr(R::a1)) + 0x140);
            const std::string model = str(uint32_t(mm.gpr(R::a2)));
            const uint32_t wp = uint32_t(mm.gpr(R::a3));
            const std::string weapon = wp ? str(wp) : std::string();
            calls.push_back({kHkNewActorHandle, vt,
                             name_hash(model.c_str()) ^ name_hash(wp ? weapon.c_str() : nullptr) * 3u ^
                                 uint32_t(mm.gpr(R::t0)),
                             words_hash(words, 16)});
            mm.write32(uint32_t(mm.gpr(R::a0)), emu_node(g_s.next_node++ & 7));
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(kHkSpawnHuman, "Simulation::SpawnHumanObject", [this](MipsMachine& mm) {
            uint32_t d[3], q[3];
            std::memcpy(d, mm.mem(uint32_t(mm.gpr(R::a2)), 12), 12);
            std::memcpy(q, mm.mem(uint32_t(mm.gpr(R::a3)), 12), 12);
            calls.push_back({kHkSpawnHuman, words_hash(d, 3), words_hash(q, 3)});
            const int k = g_s.next_human++;
            mm.set_gpr(R::v0, k < 3 && g_s.human_ok[k] ? kHumanImg + 0x100u * uint32_t(k) : 0u);
        });
        m.hook(kHkSetAIChar, "Human::SetAICharacter", [this](MipsMachine& mm) {
            calls.push_back({kHkSetAIChar, emu_human_mark(uint32_t(mm.gpr(R::a0))),
                             mm.read32(uint32_t(mm.gpr(R::a1)) + 0x140)});
        });
        m.hook(kHkSetOwner, "PhysicsObject::SetOwnerObject", [this](MipsMachine& mm) {
            calls.push_back({kHkSetOwner, emu_human_mark(uint32_t(mm.gpr(R::a0))), emu_car_mark(uint32_t(mm.gpr(R::a1)))});
        });
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
        } catch (const std::exception& x) {
            if (errors++ < 5) std::printf("  ERRO no original %08x: %s\n", fn, x.what());
        }
    }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
};

std::string compare_calls(const Emu& e) {
    const size_t n = std::max(e.calls.size(), g_port_calls.size());
    for (size_t i = 0; i < n; ++i) {
        const bool he = i < e.calls.size(), hp = i < g_port_calls.size();
        if (he && hp && e.calls[i] == g_port_calls[i]) continue;
        return "chamada " + std::to_string(i) + ": original " + (he ? e.calls[i].text() : std::string("nada")) +
               ", porte " + (hp ? g_port_calls[i].text() : std::string("nada"));
    }
    return "";
}
void begin_case(Emu& e) {
    e.calls.clear();
    e.token = 0x7000;
    g_port_calls.clear();
    g_port_token = 0x7000;
}

// Os atores (a visibilidade sorteada) dos dois lados.
void setup_actors(Emu& e, Rng& r) {
    for (int i = 0; i < 8; ++i) {
        const int vis = r.chance(0.5) ? 1 : 0;
        std::memset(e.m.mem(kActors + uint32_t(i) * kActorSize, kActorSize), 0, kActorSize);
        e.m.write32(kActors + uint32_t(i) * kActorSize + 0x30, uint32_t(vis));
        e.m.write32(emu_node(i) + 8, kActors + uint32_t(i) * kActorSize);
        g_port_actors[i] = an::ActActorData{};
        g_port_actors[i].visible = vis;
        g_port_nodes[i].actor = nf::p32_u(&g_port_actors[i]);
        g_s.done[i] = r.chance(0.5);
    }
}
std::string compare_actors(Emu& e) {
    for (int i = 0; i < 8; ++i) {
        const uint32_t v = e.m.read32(kActors + uint32_t(i) * kActorSize + 0x30);
        if (v != uint32_t(g_port_actors[i].visible))
            return "ator " + std::to_string(i) + " visível: original " + std::to_string(v) + ", porte " +
                   std::to_string(g_port_actors[i].visible);
    }
    return "";
}

void random_quat(Rng& r, float q[4]) {
    double v[4], l = 0;
    for (double& x : v) {
        x = r.uni(-1.0f, 1.0f);
        l += x * x;
    }
    l = std::sqrt(l > 1e-9 ? l : 1.0);
    for (int k = 0; k < 4; ++k) q[k] = float(v[k] / l);
}

// O tamanho do objeto de cada classe (0 janela, 1 teto, 2 motorista).
constexpr uint32_t kSize[3] = {0x170, 0x180, 0x160};

// O mundo do caso: a Simulation (o carro do jogador e os dois da IA, com os corpos), o SimRandom, a missão, o
// AIGroundVehicle do carro e o alvo dele, os Human, dos dois lados.
void random_world(Rng& r, Emu& e, PortWorld& w) {
    drv::simulation_reset(r.chance(0.8) ? 60 : 50);
    drv::Simulation& sim = drv::simulation();
    sim.cars.clear();
    sim.steps = r.range(0, 20000);
    auto body = [&](drv::BondCar* car, float spread) {
        drv::RigidBodyParams p;
        p.mass = r.uni(100, 2500);
        p.type = 1;
        for (float& v : p.pos) v = r.uni(-spread, spread);
        for (float& v : p.vel) v = r.chance(0.3) ? r.uni(-80, 80) : r.uni(-30, 30);
        float q[4];
        random_quat(r, q);
        drv::VU0_quattom4(q, p.orient);
        drv::RigidBody* rb = drv::simulation_add_body(sim, p, car);
        car->d.slot = int16_t(rb->slot);
        sim.cars.push_back(car);
        return rb;
    };
    delete w.player;
    w.player = new drv::BondCar();
    drv::RigidBody* rbp = body(w.player, 300);
    for (int i = 0; i < 2; ++i) {
        delete w.car[i];
        w.car[i] = new drv::BondCar();
        drv::RigidBody* rb = body(w.car[i], 300);
        // Às vezes perto do jogador (o alcance dos atiradores).
        if (r.chance(0.6))
            for (int k = 0; k < 3; ++k) rb->pos[k] = rbp->pos[k] + r.uni(-60, 60);
    }
    drv::SimRandom_Construct(drv::sim_random());
    drv::SimRandom_Reset(drv::sim_random());
    for (int k = r.range(0, 40); k > 0; --k) drv::SimRandom_Generate(drv::sim_random());
    e.put(kRand, &drv::sim_random(), sizeof(drv::SimRandom));
    e.m.write32(kGSim, kRand);
    e.put(kGDt, &sim.dt, 4);
    e.m.write32(kGSteps, uint32_t(sim.steps));
    e.m.write32(kGRate, uint32_t(sim.frame_rate));
    e.m.write32(kGCars, kCarsVec);
    e.m.write32(kGCars + 4, kCarsVec + 12);
    e.m.write32(kCarsVec, kCarImgP);
    e.m.write32(kCarsVec + 4, kCarE[0]);
    e.m.write32(kCarsVec + 8, kCarE[1]);
    std::memset(e.m.mem(kCarImgP, 0x100), 0, 0x100);
    e.m.write32(kCarImgP + 0x44, uint32_t(uint16_t(rbp->slot)) << 16);
    // Os corpos e os RigidBodyInfo (o GetRigidBody devolve a imagem pela vaga).
    e.put(kBodyP, rbp, 0x80);
    e.m.write32(kBodyP + 0x5c, kInfoP);
    e.put(kInfoP, nf::raw(rbp->info), 0x40);
    for (int i = 0; i < 2; ++i) {
        drv::RigidBody* rb = drv::GetRigidBody(sim, w.car[i]->d.slot);
        e.put(kBodyE[i], rb, 0x80);
        e.m.write32(kBodyE[i] + 0x5c, kInfoE[i]);
        e.put(kInfoE[i], nf::raw(rb->info), 0x40);
    }
    // A missão (o estado +0x34c; o IncKills é gancho).
    if (!drv::mission_exists()) drv::SMissionManager_Construct(false);
    drv::SMissionManagerData& mm = drv::mission_manager();
    mm.state = std::vector<int>{1, 1, 1, 3, 4, 0, 2}[size_t(r.range(0, 6))];
    e.m.write32(kGMission, kMission);
    std::memset(e.m.mem(kMission, 0x400), 0, 0x400);
    e.m.write32(kMission + 0x34c, uint32_t(mm.state));
    // O AIGroundVehicle do carro 0 e o alvo dele (sem corpo: o GetPosition devolve a linha 3 da matriz).
    w.aigv = std::make_unique<drv::AIGroundVehicle>();
    w.tgv = std::make_unique<drv::AIGroundVehicle>();
    w.tgv->physics = 0;
    w.tgv->active = r.chance(0.7) ? 1 : 0;
    for (int k = 0; k < 3; ++k) w.tgv->rot_pos[3][k] = rbp->pos[k] + r.uni(-80, 80);
    w.aigv->primary_target = r.chance(0.5) ? nf::p32_u(w.tgv.get()) : 0u;
    std::memset(e.m.mem(kAIGV, 0x400), 0, 0x400);
    std::memset(e.m.mem(kTGV, 0x400), 0, 0x400);
    e.m.write32(kAIGV + 0x1a4, w.aigv->primary_target ? kTGV : 0u);
    e.m.write32(kTGV + 0x58, uint32_t(w.tgv->active));
    e.put(kTPos, w.tgv->rot_pos[3], 16);
    // Os carros da IA: os campos que os passageiros leem.
    w.car_hp = r.chance(0.3) ? r.uni(-10, 0) : r.uni(0.1f, 200);
    std::memset(w.render, 0, sizeof w.render);
    std::memcpy(w.render + 0x28, &kHandle, 4);
    for (int i = 0; i < 2; ++i) {
        drv::PBondCarData& d = w.car[i]->d;
        d.render_object = nf::p32_u(w.render);
        d.hit_point_loc = r.chance(0.9) ? nf::p32_u(&w.car_hp) : 0u;
        d.reset_available = r.chance(0.8) ? 0 : r.range(1, 30);
        d.ai_ground_vehicle = (i == 0 && r.chance(0.9)) ? nf::p32_u(w.aigv.get()) : 0u;
        d.emp_active = r.chance(0.2) ? 1 : 0;
        d.target_beacon = 0;
        e.put(kCarE[i], &d, 0x460);
        e.m.write32(kCarE[i] + 0x48, kRender);
        e.m.write32(kCarE[i] + 0x54, d.hit_point_loc ? kHPCar : 0u);
        e.m.write32(kCarE[i] + 0x68, kVtBondCar);
        e.m.write32(kCarE[i] + 0x2e4, d.ai_ground_vehicle ? kAIGV : 0u);
    }
    std::memset(e.m.mem(kRender, 0x40), 0, 0x40);
    e.m.write32(kRender + 0x28, kHandle);
    e.put(kHPCar, &w.car_hp, 4);
}

// Um personagem de uma classe com estado sorteado, dos dois lados: `cls` 0 janela, 1 teto, 2 motorista; o próprio
// (`which` 0) ou um dos outros (1, 2) do vetor dos Human.
void random_char(Rng& r, Emu& e, PortWorld& w, int cls, int which) {
    uint8_t* raw = which == 0 ? w.self : w.other[which - 1];
    const uint32_t emu = which == 0 ? kChar : kOther[which - 1];
    for (uint32_t i = 0; i < 0x180; ++i) raw[i] = uint8_t(r.next());
    drv::AICharacterData& c = *reinterpret_cast<drv::AICharacterData*>(raw);
    for (int i = 0; i < 16; ++i) c.matrix[i] = r.uni(-2, 2);
    for (int i = 0; i < 3; ++i) c.matrix[12 + i] = r.uni(-300, 300);
    // Às vezes perto do carro do jogador (o alcance dos atiradores).
    if (r.chance(0.5)) {
        const drv::RigidBody* rbp = drv::GetRigidBody(drv::simulation(), w.player->d.slot);
        for (int i = 0; i < 3; ++i) c.matrix[12 + i] = rbp->pos[i] + r.uni(-90, 90);
    }
    static const std::vector<int> kStates[3] = {{0, 1, 2, 8, 9, 10, 11, 13, 14, 15, 22},
                                                {0, 1, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 21, 22},
                                                {0, 1, 2}};
    c.state = r.chance(0.9) ? kStates[cls][size_t(r.range(0, int(kStates[cls].size()) - 1))] : r.range(0, 0x16);
    c.sub_state = r.chance(0.6) ? std::vector<int>{0, 1, 8, 0x14, 0x15, 0x16}[size_t(r.range(0, 5))] : r.range(0, 0x18);
    c.anim = r.range(-1, 20);
    c.index = cls == 1 ? r.range(1, 2) : r.range(0, 3);
    c.unknown5c = r.range(0, 3);
    c.unknown60 = r.range(0, 3);
    c.range_sq = r.chance(0.5) ? r.uni(100, 60000) : 5625.0f;
    w.char_hp = r.chance(0.2) ? r.uni(-5, 0) : r.uni(0.1f, 150);
    c.hit_points = r.chance(0.9) ? nf::p32_u(&w.char_hp) : 0u;
    c.damage_taken = r.uni(-10, 50);
    c.initial_hp = r.uni(0, 150);
    c.bank = r.range(0, 17);
    c.clip = r.range(-3, 26);
    c.clip_size = r.chance(0.3) ? 0 : r.range(1, 25);
    c.target_right = r.range(0, 2);
    c.unknown8c = r.chance(0.5) ? 60 : r.range(1, 100);
    c.unknown90 = int32_t(drv::simulation().steps) + (r.chance(0.7) ? r.range(-100, 0) : r.range(1, 100));
    c.sound = 0;
    c.actor = port_node(r.chance(0.97) ? r.range(1, 7) : 0);
    c.controller = uint32_t(r.range(1, 1000));
    c.unknowna0 = r.chance(0.5) ? 0 : r.range(1, 1000);
    c.zone_map = r.chance(0.5) ? drv::ai_character_zone_map() : 0u;
    c.zone_last_hit = r.chance(0.9) ? r.range(0, 7) : r.range(-2, 10);
    c.unknownac = r.chance(0.8) ? 1 : 0;
    c.counts_hits = r.range(0, 1);
    c.unknowncc = r.range(0, 1);
    c.falling = 0;
    c.vtable = kVt[cls];
    const int human = which;  // o Human de cada um: 1, 2, 3
    c.world_pos = w.humans[human] ? drv::Human_Image(*w.humans[human]) : 0u;
    // A parte da classe: o carro e os campos dela.
    uint32_t* words = reinterpret_cast<uint32_t*>(raw);
    const int car = r.chance(0.85) ? 0 : 1;
    words[0x150 / 4] = nf::p32_u(&w.car[car]->d);
    if (cls == 0) {
        drv::AICharacterEnemyWindowData& d = *reinterpret_cast<drv::AICharacterEnemyWindowData*>(raw);
        d.aim = r.uni(-70, 90);
        d.others[0] = 0;
        d.others[1] = 0;
        d.side = r.range(0, 2);
        d.countdown = r.chance(0.5) ? 0 : r.range(-3, 20000);
    } else if (cls == 1) {
        drv::AICharacterEnemySunroofData& d = *reinterpret_cast<drv::AICharacterEnemySunroofData*>(raw);
        for (int k = 0; k < 3; ++k) d.vel[k] = r.uni(-80, 80);
        d.accel = r.range(0, 4);
        d.others[0] = 0;
        d.others[1] = 0;
        d.target = r.chance(0.7) ? nf::p32_u(&w.target) : 0u;
    } else {
        drv::AICharacterEnemyDriverData& d = *reinterpret_cast<drv::AICharacterEnemyDriverData*>(raw);
        d.fx_left = r.chance(0.7) ? 0x4100u + uint32_t(r.range(0, 3)) : 0u;
        d.fx_right = r.chance(0.7) ? 0x4200u + uint32_t(r.range(0, 3)) : 0u;
        d.idle = r.range(-1, 4);
    }
    // O original: os mesmos bytes, os ponteiros traduzidos.
    e.put(emu, raw, 0x180);
    e.m.write32(emu + 0x68, c.hit_points ? kHPChar : 0u);
    e.m.write32(emu + 0x74, c.world_pos ? kHumanImg + 0x100u * uint32_t(human) : 0u);
    e.m.write32(emu + 0x98, emu_node(port_node_index(c.actor)));
    e.m.write32(emu + 0xa4, c.zone_map ? 0x326860u : 0u);
    e.m.write32(emu + 0x150, kCarE[car]);
    if (cls == 1) e.m.write32(emu + 0x178, words[0x178 / 4] ? kTarget : 0u);
    e.put(kHPChar, &w.char_hp, 4);
}

// Os outros passageiros: no vetor dos Human (o próprio sempre; os outros às vezes), com classes sorteadas.
void random_humans(Rng& r, Emu& e, PortWorld& w, int cls) {
    std::vector<drv::Human*>& v = drv::simulation_humans();
    v.clear();
    std::vector<uint32_t> img;
    // A ordem sorteada: o próprio e até dois outros.
    std::vector<int> order = {0};
    for (int i = 1; i <= 2; ++i)
        if (r.chance(0.6)) order.insert(order.begin() + r.range(0, int(order.size())), i);
    for (int i = 1; i <= 2; ++i) random_char(r, e, w, r.range(0, 2), i);
    random_char(r, e, w, cls, 0);
    for (int idx : order) {
        drv::Human* h = w.humans[idx];
        h->h.character = idx == 0 ? port_self() : port_other(idx - 1);
        if (idx > 0 && r.chance(0.15)) h->h.character = 0;
        v.push_back(h);
        const uint32_t hi = kHumanImg + 0x100u * uint32_t(idx);
        std::memset(e.m.mem(hi, 0x100), 0, 0x100);
        const uint32_t ch = h->h.character == 0 ? 0u : (idx == 0 ? kChar : kOther[idx - 1]);
        e.m.write32(hi + 0x70, ch);
        img.push_back(hi);
    }
    for (size_t i = 0; i < img.size(); ++i) e.m.write32(kHumansArr + 4u * uint32_t(i), img[i]);
    e.m.write32(kGHumans, kHumansArr);
    e.m.write32(kGHumans + 4, kHumansArr + 4u * uint32_t(img.size()));
}

// Compara um passageiro: os ponteiros pelo que apontam.
std::string compare_char(Emu& e, uint32_t emu, const uint8_t* port, int cls) {
    for (uint32_t o = 0; o < kSize[cls]; o += 4) {
        uint32_t x = e.m.read32(emu + o), y;
        std::memcpy(&y, port + o, 4);
        if (o == 0x98) {
            x = uint32_t(emu_node_index(x));
            y = uint32_t(port_node_index(y));
        } else if (o == 0x68) {
            x = x == kHPChar ? 1u : (x == 0 ? 0u : 0xffu);
            y = y == nf::p32_u(&g_w->char_hp) ? 1u : (y == 0 ? 0u : 0xfeu);
        } else if (o == 0x74) {
            x = emu_human_mark(x);
            y = port_human_mark(y);
        } else if (o == 0xa4) {
            x = x == 0x326860u ? 1u : (x == 0 ? 0u : 0xffu);
            y = y == drv::ai_character_zone_map() ? 1u : (y == 0 ? 0u : 0xfeu);
        } else if (o == 0x150) {
            x = emu_car_mark(x);
            y = port_car_mark(y);
        } else if ((cls == 0 && (o == 0x158 || o == 0x15c)) || (cls == 1 && (o == 0x170 || o == 0x174))) {
            x = emu_char_mark(x);
            y = port_char_mark(y);
        } else if (cls == 1 && o == 0x178) {
            x = emu_target_mark(x);
            y = port_target_mark(y);
        }
        if (x != y) return "+0x" + hex(o).substr(5) + ": original " + hex(x) + ", porte " + hex(y);
    }
    return "";
}

void random_script(Rng& r) {
    for (float& v : g_s.local) v = r.uni(-2, 2);
    for (float& v : g_s.weapon) v = r.uni(-2, 2);
    for (int k = 0; k < 3; ++k) g_s.weapon[12 + k] = r.uni(-300, 300);
    for (auto& m : g_s.fxm)
        for (float& v : m) v = r.uni(-3, 3);
    g_s.fx[0] = r.chance(0.8) ? 0x4100u + uint32_t(r.range(0, 3)) : 0u;
    g_s.fx[1] = r.chance(0.8) ? 0x4200u + uint32_t(r.range(0, 3)) : 0u;
    g_s.bone_bias = r.range(0, 1000);
    g_s.no_bone = r.chance(0.2);
}

// ---- o EHitWindow::~EHitWindow (0x0014f480, seção 26): os sons do vidro e os efeitos da galeria ----
// O original num interpretador próprio: o ABaseSound::operator_new dá blocos da pilha, o AIndex::Lookup, o AMix::Add, o
// GFX::LookupReference e o GFX::TriggerIncidental gravam o que recebem; o AVoice::AVoice não faz nada. Do porte, o
// hit_window_env().sound e os effects_env() da galeria gravam o mesmo. Os sons são comparados pelos campos que o
// destrutor escreve (o lugar, as distâncias, o +0x114), na ordem.
constexpr uint32_t kHwBanks = 0x01a10000, kHwEvent = 0x01a11000, kHwFgBanks = 0x003562a0;
constexpr uint32_t kHwCntShatter = 0x0032bbc0, kHwCntAlarm = 0x0032bbc4;
std::vector<std::string>* g_hw_port = nullptr;
uint32_t g_hw_port_refs = 0;

std::string hw_sound_text(int32_t cat, const std::string& name, const std::string& mix, const uint32_t pos[3],
                          uint32_t mn, uint32_t mx, uint32_t mx2, uint32_t fx) {
    return "som " + std::to_string(cat) + " " + name + " " + mix + " " + hex(pos[0]) + "," + hex(pos[1]) + "," +
           hex(pos[2]) + " " + hex(mn) + " " + hex(mx) + " " + hex(mx2) + " " + hex(fx);
}
std::string hw_gfx_text(uint32_t ref, const uint32_t* pos, const uint32_t* quat, const uint32_t* pos2,
                        const uint32_t* quat2, uint32_t scene, uint32_t inst, uint32_t extra) {
    std::string t = "efeito " + hex(ref);
    for (int i = 0; i < 3; ++i) t += " " + hex(pos[i]);
    for (int i = 0; i < 4; ++i) t += " " + hex(quat[i]);
    for (int i = 0; i < 3; ++i) t += " " + hex(pos2[i]);
    for (int i = 0; i < 4; ++i) t += " " + hex(quat2[i]);
    return t + " " + hex(scene) + " " + hex(inst) + " " + hex(extra);
}

void check_hit_window(const nf::Elf& elf, const uint8_t* vu0_program, uint32_t vu0_size, int cases, uint32_t seed,
                      int& failures) {
    Emu ew;
    ew.setup(elf, vu0_program, vu0_size, false);
    std::vector<std::string> emu_rec, port_rec;
    std::vector<uint32_t> sounds;  // os blocos dos AOneShotSound do original, na ordem
    std::vector<std::string> sound_name, sound_mix;
    std::vector<int32_t> sound_cat;
    uint32_t emu_refs = 0;
    MipsMachine& m = ew.m;
    // ABank::fgBanks: dez bancos falsos (o +8 o banco do SND).
    for (uint32_t c = 0; c < 10; ++c) {
        m.write32(kHwFgBanks + 4 * c, kHwBanks + 0x40 * c);
        std::memset(m.mem(kHwBanks + 0x40 * c, 0x40), 0, 0x40);
        m.write32(kHwBanks + 0x40 * c + 8, 100 + c);
    }
    m.hook(0x2fca38, "ABaseSound::operator_new", [&](MipsMachine& mm) {
        const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15u) & ~15u;
        if (ew.heap + n > kHeapEnd) ew.heap = kHeap;
        std::memset(mm.mem(ew.heap, n), 0, n);
        mm.set_gpr(R::v0, ew.heap);
        sounds.push_back(ew.heap);
        sound_name.emplace_back();
        sound_mix.emplace_back();
        sound_cat.push_back(-1);
        ew.heap += n;
    });
    m.hook(0x2f5880, "AIndex::Lookup", [&](MipsMachine& mm) {
        const uint32_t idx = uint32_t(mm.gpr(R::a0));
        if (!sound_cat.empty()) {
            sound_cat.back() = int32_t((idx - 0x10 - kHwBanks) / 0x40);
            sound_name.back() = ew.str(uint32_t(mm.gpr(R::a1)));
        }
        mm.set_gpr(R::v0, 7);
    });
    m.hook(0x2f2d50, "AMix::Add", [&](MipsMachine& mm) {
        if (!sound_mix.empty()) sound_mix.back() = ew.str(uint32_t(mm.gpr(R::a0)));
        mm.set_gpr(R::v0, 0);
    });
    m.hook(0x2dd790, "AVoice::AVoice", [](MipsMachine&) {});
    m.hook(0x175758, "Event::operator delete", [](MipsMachine&) {});
    m.hook(0x232f48, "GFX::LookupReference", [&](MipsMachine& mm) {
        emu_rec.push_back("busca " + ew.str(uint32_t(mm.gpr(R::a0))));
        mm.set_gpr(R::v0, 0x7000u + emu_refs++);
    });
    m.hook(0x233108, "GFX::TriggerIncidental", [&](MipsMachine& mm) {
        auto w = [&](uint32_t a, int i) { return mm.read32(a + 4u * uint32_t(i)); };
        uint32_t pos[3], quat[4], pos2[3], quat2[4];
        const uint32_t ap = uint32_t(mm.gpr(R::a1)), aq = uint32_t(mm.gpr(R::a2)), ap2 = uint32_t(mm.gpr(R::a3)),
                       aq2 = uint32_t(mm.gpr(R::t0));
        for (int i = 0; i < 3; ++i) pos[i] = w(ap, i), pos2[i] = w(ap2, i);
        for (int i = 0; i < 4; ++i) quat[i] = w(aq, i), quat2[i] = w(aq2, i);
        emu_rec.push_back(hw_gfx_text(uint32_t(mm.gpr(R::a0)), pos, quat, pos2, quat2, uint32_t(mm.gpr(R::t1)),
                                      uint32_t(mm.gpr(R::t2)), uint32_t(mm.gpr(R::t3))));
        mm.set_gpr(R::v0, 0);
    });
    // O porte.
    drv::EffectsEnv& fe = drv::effects_env();
    const drv::EffectsEnv saved_fe = fe;
    const drv::HitWindowEnv saved_hw = drv::hit_window_env();
    const drv::HitWindowCounters saved_cnt = drv::hit_window_counters();
    g_hw_port = &port_rec;
    fe.gfx_lookup = [](const char* name) -> uint32_t {
        g_hw_port->push_back(std::string("busca ") + name);
        return 0x7000u + g_hw_port_refs++;
    };
    fe.gfx_trigger_incidental = [](uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                                   const float quat2[4], uint32_t scene, int32_t inst, uint32_t extra) -> int32_t {
        uint32_t p[3], q[4], p2[3], q2[4];
        std::memcpy(p, pos, 12);
        std::memcpy(q, quat, 16);
        std::memcpy(p2, pos2, 12);
        std::memcpy(q2, quat2, 16);
        g_hw_port->push_back(hw_gfx_text(ref, p, q, p2, q2, scene, uint32_t(inst), extra));
        return 0;
    };
    drv::hit_window_env().sound = [](int32_t cat, const char* name, const char* mix, const float pos[3], float mn,
                                     float mx, float mx2, float fx) {
        uint32_t p[3];
        std::memcpy(p, pos, 12);
        g_hw_port->push_back(hw_sound_text(cat, name, mix, p, fbits(mn), fbits(mx), fbits(mx2), fbits(fx)));
    };
    Rng rng(seed ^ 0x26262626u);
    Tally t;
    uint64_t n_sounds = 0, n_gfx = 0;
    for (int c = 0; c < cases; ++c) {
        ++t.cases;
        drv::HitWindowEvent ev{};
        ev.image[0] = 0x00371008;
        float pt[4] = {rng.uni(-500, 500), rng.chance(0.5) ? rng.uni(-5, 10.5f) : rng.uni(9.5f, 60), rng.uni(-500, 500),
                       1.0f};
        if (rng.chance(0.05)) pt[1] = 10.0f;
        std::memcpy(&ev.image[1], pt, 16);
        for (int i = 5; i < 9; ++i) ev.image[i] = fbits(rng.uni(-1, 1));
        const int hk = rng.range(0, 9);
        const uint32_t hits = hk < 5 ? 0u : (hk < 8 ? uint32_t(rng.range(1, 6)) : rng.next());
        ev.image[9] = uint32_t(rng.range(0, 7));
        ev.image[10] = hits;
        ev.image[11] = rng.next() & 0xff;
        for (int i = 12; i < 20; ++i) ev.image[i] = fbits(i == 15 || i == 19 ? 1.0f : rng.uni(-600, 600));
        ev.image[20] = rng.chance(0.5) ? 1u : 0u;
        drv::HitWindowCounters cnt{rng.chance(0.2) ? rng.next() : uint32_t(rng.range(0, 40)),
                                   rng.chance(0.2) ? rng.next() : uint32_t(rng.range(0, 40))};
        m.write32(kHwCntShatter, cnt.shatter);
        m.write32(kHwCntAlarm, cnt.alarm);
        drv::hit_window_counters() = cnt;
        emu_rec.clear();
        port_rec.clear();
        sounds.clear();
        sound_name.clear();
        sound_mix.clear();
        sound_cat.clear();
        emu_refs = 0;
        g_hw_port_refs = 0;
        ew.put(kHwEvent, ev.image, 0x54);
        // (os sons entram no registro do original pela ordem dos blocos, antes dos efeitos: o destrutor cria os sons
        // primeiro; o porte também)
        ew.call(0x14f480, {kHwEvent, 0});
        std::vector<std::string> erec;
        for (size_t k = 0; k < sounds.size(); ++k) {
            const uint32_t a = sounds[k];
            const uint32_t pos[3] = {m.read32(a), m.read32(a + 4), m.read32(a + 8)};
            erec.push_back(hw_sound_text(sound_cat[k], sound_name[k], sound_mix[k], pos, m.read32(a + 0x5c),
                                         m.read32(a + 0x60), m.read32(a + 0x64), m.read32(a + 0x114)));
        }
        for (const std::string& r : emu_rec) erec.push_back(r);
        drv::EHitWindow_Destroy(ev);
        n_sounds += sounds.size();
        n_gfx += emu_refs;
        std::string why;
        const size_t n = std::max(erec.size(), port_rec.size());
        for (size_t i = 0; i < n && why.empty(); ++i) {
            const std::string a = i < erec.size() ? erec[i] : std::string("nada");
            const std::string b = i < port_rec.size() ? port_rec[i] : std::string("nada");
            if (a != b) why = "registro " + std::to_string(i) + ": original " + a + ", porte " + b;
        }
        if (why.empty() && (m.read32(kHwCntShatter) != drv::hit_window_counters().shatter ||
                            m.read32(kHwCntAlarm) != drv::hit_window_counters().alarm))
            why = "os contadores";
        if (!why.empty()) t.fail(why + " (tiros " + std::to_string(int32_t(hits)) + ")");
    }
    fe = saved_fe;
    drv::hit_window_env() = saved_hw;
    drv::hit_window_counters() = saved_cnt;
    g_hw_port = nullptr;
    report("EHitWindow::~EHitWindow (os vidros)", t, failures);
    std::printf("  (sons do vidro %llu, efeitos da galeria %llu)\n", (unsigned long long)n_sounds,
                (unsigned long long)n_gfx);
}

}  // namespace

int run_passenger_checks(const nf::Elf& elf, const nf::DiscSource&, const uint8_t* vu0_program, uint32_t vu0_size,
                         int cases, uint32_t seed) {
    std::printf("\nparte 22: os passageiros dos carros da IA (driving/game/ai_passengers.h)\n");
    int failures = 0;
    Emu e;
    e.setup(elf, vu0_program, vu0_size, true);
    Emu eb;  // sem os ganchos dos passageiros: o farol do carro e as sobreposições rodam
    eb.setup(elf, vu0_program, vu0_size, false);
    Rng rng(seed ^ 0x22222222u);
    const drv::AICharacterActorApi saved_api = drv::ai_character_actor_api();
    const drv::PassengerApi saved_papi = drv::passenger_api();
    PortWorld world;
    g_w = &world;
    for (auto& h : world.humans) h = new drv::Human();
    install_port_api();
    std::vector<an::ActActorData> port_actors(8);
    std::vector<an::ActListNode> port_nodes(8);
    g_port_actors = port_actors.data();
    g_port_nodes = port_nodes.data();
    // O IK do motorista: o .data do original e o do porte começam iguais.
    std::memcpy(drv::passenger_driver_ik(), e.m.mem(kGDriverIK, 96), 96);

    static const char* const kClass[3] = {"janela", "teto", "motorista"};
    Tally t_ctor[3], t_exec[3], t_state[3], t_range[3], t_acq[3], t_injury[3], t_setactor, t_rot[3], t_accel, t_dtor[3];
    Tally t_beacon, t_ov_create, t_ov_set, t_ov_blend, t_ov_do, t_wake, t_seg_near, t_seg_dir, t_seg_check;
    uint64_t seg_touched = 0;
    uint64_t fires = 0, deaths = 0, wake_created = 0;
    std::vector<an::ActCharacterData> port_chars(8);
    std::vector<an::ActWeaponData> port_weapons(8);

    auto check = [&](Tally& t, int cls, const std::string& what) {
        std::string why = compare_char(e, kChar, world.self, cls);
        if (why.empty()) why = compare_calls(e);
        if (why.empty()) why = compare_actors(e);
        if (why.empty() && e.m.read32(kRand + 4) != drv::sim_random().last) why = "o SimRandom";
        if (why.empty() && std::memcmp(e.m.mem(kGDriverIK, 96), drv::passenger_driver_ik(), 96) != 0) why = "o IK 0x326950";
        if (!why.empty()) t.fail(std::string(kClass[cls]) + " " + what + ": " + why);
        // (o IK volta a ficar igual para os casos seguintes)
        std::memcpy(e.m.mem(kGDriverIK, 96), drv::passenger_driver_ik(), 96);
    };

    for (int i = 0; i < cases; ++i) {
        random_script(rng);
        random_world(rng, e, world);
        const int cls = rng.range(0, 2);
        // ---- o construtor, sobre memória sorteada ----
        {
            ++t_ctor[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            float m[16];
            for (float& v : m) v = rng.uni(-5, 5);
            e.put(kM, m, 64);
            const uint32_t car = rng.chance(0.85) ? 0u : 1u;
            begin_case(e);
            g_emu_new_target = 0;
            if (cls == 0) {
                e.call(kFnWinCtor, {kChar, kM, kCarE[car]});
                drv::AICharacterEnemyWindow_Construct(*reinterpret_cast<drv::AICharacterEnemyWindowData*>(world.self), m,
                                                      nf::p32_u(&world.car[car]->d));
            } else if (cls == 1) {
                const int32_t w = rng.range(-1, 4), u60 = rng.range(0, 5);
                e.call(kFnSunCtor, {kChar, kM, uint32_t(w), uint32_t(u60), kCarE[car]});
                drv::AICharacterEnemySunroof_Construct(*reinterpret_cast<drv::AICharacterEnemySunroofData*>(world.self), m,
                                                       w, u60, nf::p32_u(&world.car[car]->d));
            } else {
                e.call(kFnDrvCtor, {kChar, kM, kCarE[car]});
                drv::AICharacterEnemyDriver_Construct(*reinterpret_cast<drv::AICharacterEnemyDriverData*>(world.self), m,
                                                      nf::p32_u(&world.car[car]->d));
            }
            check(t_ctor[cls], cls, "construtor");
        }
        // ---- o Execute (o UpdateRotPos, o HandleInterrupts, o estado) ----
        {
            ++t_exec[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            begin_case(e);
            const int before = reinterpret_cast<drv::AICharacterData*>(world.self)->state;
            e.call(kFnExecute, {kChar});
            drv::AICharacter_Execute(port_self());
            const drv::AICharacterData& pc = *reinterpret_cast<drv::AICharacterData*>(world.self);
            if (pc.state == 0xb && before != 0xb) ++fires;
            if (pc.state == 0x16 && before != 0x16) ++deaths;
            check(t_exec[cls], cls, "Execute (estado " + std::to_string(before) + ")");
        }
        // ---- um estado da tabela, chamado direto ----
        {
            ++t_state[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            const int s = rng.range(0, 22);
            const uint32_t fn = e.m.read32(kVt[cls] + 0x44 + 8u * uint32_t(s));
            begin_case(e);
            e.call(fn, {kChar});
            const drv::AIVtable* v = drv::ai_vtable(kVt[cls]);
            if (v && v->states[s]) v->states[s](port_self());
            check(t_state[cls], cls, "estado " + std::to_string(s));
        }
        // ---- TargetIsInRange e TargetIsAcquired (+0x114, +0x124) ----
        if (cls != 2) {
            ++t_range[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            begin_case(e);
            e.call(e.m.read32(kVt[cls] + 0x114), {kChar});
            const uint32_t ro = uint32_t(e.m.gpr(R::v0));
            const drv::AIVtable* v = drv::ai_vtable(kVt[cls]);
            const uint32_t rp = uint32_t(v->target_in_range(port_self()));
            check(t_range[cls], cls, "TargetIsInRange");
            if (ro != rp) t_range[cls].fail(std::string(kClass[cls]) + " TargetIsInRange: original " + hex(ro) +
                                            ", porte " + hex(rp));
            ++t_acq[cls].cases;
            begin_case(e);
            e.call(e.m.read32(kVt[cls] + 0x124), {kChar});
            const uint32_t ao = uint32_t(e.m.gpr(R::v0));
            const uint32_t ap = uint32_t(v->target_acquired(port_self()));
            check(t_acq[cls], cls, "TargetIsAcquired");
            if (ao != ap) t_acq[cls].fail(std::string(kClass[cls]) + " TargetIsAcquired: original " + hex(ao) +
                                          ", porte " + hex(ap));
        }
        // ---- PlayZoneInjuryAnim (+0x3c) ----
        if (cls != 2) {
            ++t_injury[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            const float dmg = rng.chance(0.5) ? rng.uni(0, 20) : rng.uni(0, 200);
            const int32_t zone = rng.range(0, 7), kind = rng.range(0, 3);
            begin_case(e);
            e.m.set_fpr(12, dmg);
            e.call(e.m.read32(kVt[cls] + 0x3c), {kChar, uint32_t(zone), uint32_t(kind)});
            drv::ai_vtable(kVt[cls])->play_zone_injury_anim(port_self(), dmg, zone, kind);
            check(t_injury[cls], cls, "PlayZoneInjuryAnim");
        }
        // ---- SetActor da janela (+0x1c) ----
        if (cls == 0) {
            ++t_setactor.cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            const int n = rng.range(0, 7);
            e.m.write32(kOut, emu_node(n));
            begin_case(e);
            e.call(e.m.read32(kVt[0] + 0x1c), {kChar, kOut});
            drv::ai_vtable(kVt[0])->set_actor(port_self(), port_node(n));
            check(t_setactor, 0, "SetActor");
        }
        // ---- GetRotPos de três argumentos (a função da matriz do ator) ----
        {
            ++t_rot[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            std::memset(e.m.mem(kOut, 0x60), 0, 0x60);
            begin_case(e);
            e.call(kFnRot3[cls], {kChar, kOut, kOut + 0x40});
            alignas(16) float m[16] = {}, vel[4] = {};
            drv::AICharacterEnemy_CarRotPos(port_self(), m, vel);
            check(t_rot[cls], cls, "GetRotPos");
            if (std::memcmp(e.m.mem(kOut, 64), m, 64) != 0 || std::memcmp(e.m.mem(kOut + 0x40, 12), vel, 12) != 0)
                t_rot[cls].fail(std::string(kClass[cls]) + " GetRotPos: a matriz ou a velocidade");
        }
        // ---- VehicleIsAccelerating do teto ----
        if (cls == 1) {
            ++t_accel.cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            begin_case(e);
            e.call(kFnAccel, {kChar});
            const uint32_t ro = uint32_t(e.m.gpr(R::v0));
            const uint32_t rp = drv::AICharacterEnemySunroof_VehicleIsAccelerating(port_self()) ? 1u : 0u;
            check(t_accel, 1, "VehicleIsAccelerating");
            if (ro != rp) t_accel.fail("VehicleIsAccelerating: original " + hex(ro) + ", porte " + hex(rp));
        }
        // ---- o destrutor (+0xc com 2: sem soltar a memória; o ator nulo) ----
        {
            ++t_dtor[cls].cases;
            setup_actors(e, rng);
            random_humans(rng, e, world, cls);
            reinterpret_cast<drv::AICharacterData*>(world.self)->actor = 0;
            e.m.write32(kChar + 0x98, 0);
            world.target.kind = 3;
            e.m.write32(kTarget + 0x34, 3);
            begin_case(e);
            e.call(e.m.read32(kVt[cls] + 0xc), {kChar, 2});
            if (cls == 0) drv::AICharacterEnemyWindow_Destruct(*reinterpret_cast<drv::AICharacterEnemyWindowData*>(world.self));
            else if (cls == 1)
                drv::AICharacterEnemySunroof_Destruct(*reinterpret_cast<drv::AICharacterEnemySunroofData*>(world.self));
            else drv::AICharacterEnemyDriver_Destruct(*reinterpret_cast<drv::AICharacterEnemyDriverData*>(world.self));
            check(t_dtor[cls], cls, "destrutor");
            if (e.m.read32(kTarget + 0x34) != uint32_t(world.target.kind))
                t_dtor[cls].fail("destrutor: o tipo do alvo (+0x34)");
        }
        // ---- o WakeUp de um veículo com passageiros (a parte dos passageiros) ----
        {
            ++t_wake.cases;
            setup_actors(e, rng);
            drv::simulation_humans().clear();
            e.m.write32(kGHumans, kHumansArr);
            e.m.write32(kGHumans + 4, kHumansArr);
            e.m.write32(kGCars + 4, kCarsVec);  // sem tráfego: o laço dos carros do WakeUp não roda
            // A arma de cada ator (o +0 do ator, o +0x24 do personagem): o +0x15c recebe o passageiro.
            for (int a = 0; a < 8; ++a) {
                const bool has_w = rng.chance(0.8);
                port_chars[size_t(a)] = an::ActCharacterData{};
                port_weapons[size_t(a)] = an::ActWeaponData{};
                port_chars[size_t(a)].weapon[0] = has_w ? nf::p32_u(&port_weapons[size_t(a)]) : 0u;
                g_port_actors[a].character = nf::p32_u(&port_chars[size_t(a)]);
                const uint32_t ec = kActChar + 0x80u * uint32_t(a), ew = kActWeap + 0x170u * uint32_t(a);
                std::memset(e.m.mem(ec, 0x80), 0, 0x80);
                std::memset(e.m.mem(ew, 0x170), 0, 0x170);
                e.m.write32(ec + 0x24, has_w ? ew : 0u);
                e.m.write32(kActors + uint32_t(a) * kActorSize, ec);
            }
            // Os assentos (AIC_EWIND_POS, AIC_ESUNR_POS, AIC_BOND_POS) dos dois lados.
            static const char* const kSeatKeys[3] = {"AIC_EWIND_POS", "AIC_ESUNR_POS", "AIC_BOND_POS"};
            drv::BondCar& car = *world.car[0];
            car.attr_vectors.clear();
            for (int k = 0; k < 3; ++k) {
                std::array<float, 4> v = {rng.uni(-1, 1), rng.uni(0, 1.5f), rng.uni(-1, 1), 1.0f};
                car.attr_vectors[kSeatKeys[k]] = v;
                e.put(kSeats + 0x10u * uint32_t(k), v.data(), 16);
            }
            e.put(kCarE[0], &car.d, 0x460);
            e.m.write32(kCarE[0] + 0x48, kRender);
            e.m.write32(kCarE[0] + 0x54, car.d.hit_point_loc ? kHPCar : 0u);
            e.m.write32(kCarE[0] + 0x68, kVtBondCar);
            e.m.write32(kCarE[0] + 0x2e4, car.d.ai_ground_vehicle ? kAIGV : 0u);
            // O elemento: um veículo com um a três passageiros das classes de Paris (e às vezes uma que nenhum lado
            // cria: o WakeUp sai).
            drv::AIElement el{};
            for (int r = 0; r < 4; ++r)
                for (int c2 = 0; c2 < 4; ++c2) el.matrix[r][c2] = rng.uni(-50, 50);
            el.flags = uint16_t((rng.chance(0.3) ? 0x40 : 0) | (rng.chance(0.5) ? 0x10 : 0) | 0x8);
            el.difficulty = 0;
            el.type = 1;
            el.state = 0;
            el.health = rng.uni(10, 500);
            el.heli = 0;
            el.passengers = rng.range(1, 3);
            for (int k = 0; k < 3; ++k) {
                drv::AIPassenger& pa = el.passenger[k];
                const int t = rng.range(0, 9);
                pa.type = t < 3 ? 3 : (t < 6 ? 4 : (t < 8 ? 0x11 : 5));
                pa.model = rng.chance(0.9) ? (rng.chance(0.5) ? 20 : 29) : rng.range(0, 31);
                pa.weapon = rng.range(0, 2);
                pa.unknown0c = rng.chance(0.3) ? -100 : 0;
                pa.unknown10 = rng.range(0, 100);
                pa.health = rng.chance(0.15) ? 0.0f : rng.uni(1, 150);
                pa.accuracy = rng.range(0, 5);
            }
            el.command = 0;
            el.physics = 0;
            e.put(kElem, &el, 0x100);
            std::memset(e.m.mem(kCtl, 0x58), 0, 0x58);
            e.m.write32(kGActDB, 0x0180fa00);
            e.m.write32(kMission + 0x3b8, 0);
            for (int k = 0; k < 3; ++k) {
                g_s.human_ok[k] = rng.chance(0.85);
                world.humans[k]->h.hit_pos[3] = 0.0f;
                std::memset(e.m.mem(kHumanImg + 0x100u * uint32_t(k), 0x100), 0, 0x100);
            }
            begin_case(e);
            g_s.next_node = 1;
            g_s.next_human = 0;
            g_emu_new_target = 0;
            e.call(kFnWakeUp, {kCtl, kElem});
            const uint32_t emu_awake = e.m.read32(kCtl + 0x44);
            g_s.next_node = 1;
            g_s.next_human = 0;
            int32_t awake = 0;
            el.physics = nf::p32_u(&car.d);
            const int32_t created = drv::AIElementController_WakeUpPassengers(nf::p32_u(&el), awake, car);
            std::string why = compare_calls(e);
            if (why.empty() && emu_awake != uint32_t(awake))
                why = "os personagens acordados: original " + std::to_string(emu_awake) + ", porte " + std::to_string(awake);
            for (int k = 0; k < 3 && why.empty(); ++k) {
                const uint32_t ec = e.m.read32(kElem + 0xe0 + 4u * uint32_t(k));
                const uint32_t pc = el.characters[k];
                if ((ec != 0) != (pc != 0)) {
                    why = "o personagem " + std::to_string(k);
                    break;
                }
                const uint32_t eh = e.m.read32(kElem + 0xec + 4u * uint32_t(k)), ph = el.humans[k];
                if (emu_human_mark(eh) != port_human_mark(ph)) why = "o Human " + std::to_string(k);
                if (ec == 0) continue;
                const uint32_t vt = e.m.read32(ec + 0x140);
                const int cls = vt == kVt[0] ? 0 : (vt == kVt[1] ? 1 : 2);
                // O personagem: os mesmos campos do compare_char, os ponteiros pelo que apontam.
                for (uint32_t o = 0; o < kSize[cls] && why.empty(); o += 4) {
                    uint32_t x = e.m.read32(ec + o), y;
                    std::memcpy(&y, nf::p32_ptr<const uint8_t>(pc) + o, 4);
                    if (o == 0x98) {
                        x = uint32_t(emu_node_index(x));
                        y = uint32_t(port_node_index(y));
                    } else if (o == 0x68) {
                        x = x == 0 ? 0u : (x - kElem);
                        y = y == 0 ? 0u : (y - nf::p32_u(&el));
                    } else if (o == 0x74) {
                        x = emu_human_mark(x);
                        y = port_human_mark(y);
                    } else if (o == 0xa4) {
                        x = x == 0x326860u ? 1u : (x == 0 ? 0u : 0xffu);
                        y = y == drv::ai_character_zone_map() ? 1u : (y == 0 ? 0u : 0xfeu);
                    } else if (o == 0x150) {
                        x = emu_car_mark(x);
                        y = port_car_mark(y);
                    } else if (cls == 1 && o == 0x178) {
                        x = x != 0;
                        y = y != 0;
                    } else if (o >= 0x144 && o < 0x150) {
                        continue;  // (o construtor da base não escreve: a memória de cada lado)
                    }
                    if (x != y)
                        why = "o personagem " + std::to_string(k) + " +0x" + hex(o).substr(5) + ": original " + hex(x) +
                              ", porte " + hex(y);
                }
            }
            for (int k = 0; k < 3 && why.empty(); ++k) {
                uint32_t hp;
                std::memcpy(&hp, &world.humans[k]->h.hit_pos[3], 4);
                if (e.m.read32(kHumanImg + 0x100u * uint32_t(k) + 0x8c) != hp) why = "o +0x8c do Human " + std::to_string(k);
            }
            for (int a = 0; a < 8 && why.empty(); ++a) {
                const uint32_t ew = e.m.read32(kActChar + 0x80u * uint32_t(a) + 0x24);
                if (!ew) continue;
                const uint32_t x = e.m.read32(ew + 0x15c), y = port_weapons[size_t(a)].accuracy;
                const uint32_t xm = x == 0 ? 0u : x - kElem, ym = y == 0 ? 0u : y - nf::p32_u(&el);
                if (xm != ym) why = "o passageiro na arma do ator " + std::to_string(a);
            }
            if (!why.empty()) {
                for (int k = 0; k < el.passengers; ++k)
                    why += " | p" + std::to_string(k) + " tipo " + std::to_string(el.passenger[k].type) + " vida " +
                           std::to_string(el.passenger[k].health) + " humano " + std::to_string(g_s.human_ok[k]) +
                           " pers " + hex(e.m.read32(kElem + 0xe0 + 4u * uint32_t(k)));
                t_wake.fail(why);
            }
            wake_created += uint64_t(created);
            // Os personagens do porte (o PS2 os apaga com os Human; aqui direto, sem os atores falsos).
            for (int k = 0; k < 3; ++k) {
                if (!el.characters[k]) continue;
                drv::AICharacterData& c = *nf::p32_ptr<drv::AICharacterData>(el.characters[k]);
                c.actor = 0;
                const drv::AIVtable* v = drv::ai_vtable(c.vtable);
                if (v && v->destroy) v->destroy(el.characters[k]);
            }
            e.m.write32(kGCars + 4, kCarsVec + 12);
        }
        // ---- os gatilhos tocados pelos raios: a reta mais perto (3D e no plano), a direção, o CheckCollide do segmento
        // contra um gatilho sorteado de cada forma (o Process(int) inteiro, com a grade de Paris, fica na parte 14) ----
        {
            drv::WTrigger tr{};
            for (int i = 0; i < 3; ++i) tr.position[i] = rng.uni(-20, 20);
            tr.radius = rng.uni(0.5f, 15);
            tr.shape = uint8_t(rng.chance(0.95) ? rng.range(1, 3) : rng.range(0, 5));
            tr.flags = uint16_t(rng.next() & 0x3fff);
            tr.height = rng.uni(0.2f, 10);
            const float yaw = rng.uni(-3.14159f, 3.14159f), pitch = rng.chance(0.7) ? 0.0f : rng.uni(-0.4f, 0.4f);
            tr.forward[0] = std::sin(yaw) * std::cos(pitch);
            tr.forward[1] = std::sin(pitch);
            tr.forward[2] = std::cos(yaw) * std::cos(pitch);
            tr.right[0] = std::cos(yaw);
            tr.right[1] = 0;
            tr.right[2] = -std::sin(yaw);
            tr.width = rng.uni(0.5f, 30);
            tr.depth = rng.uni(0.5f, 30);
            alignas(16) float seg[2][4];
            for (int i = 0; i < 3; ++i) seg[0][i] = tr.position[i] + rng.uni(-25, 25);
            seg[0][1] = tr.position[1] + rng.uni(-3, 12);
            const float len = rng.chance(0.1) ? rng.uni(0, 0.05f) : rng.uni(0.5f, 120);
            const float sy = rng.uni(-3.14159f, 3.14159f), sp = rng.chance(0.3) ? 0.0f : rng.uni(-0.6f, 0.6f);
            seg[1][0] = seg[0][0] + std::sin(sy) * std::cos(sp) * len;
            seg[1][1] = rng.chance(0.2) ? seg[0][1] : seg[0][1] + std::sin(sp) * len;
            seg[1][2] = seg[0][2] + std::cos(sy) * std::cos(sp) * len;
            seg[0][3] = seg[1][3] = 1.0f;
            const float half = rng.chance(0.1) ? rng.uni(0, 0.02f) : len * 0.5f;
            eb.put(kOut, seg, 32);
            eb.put(kOut + 0x40, &tr, 0x40);
            // NearestPointLine3D e 2D (o w da saída fica).
            ++t_seg_near.cases;
            const float pt[4] = {tr.position[0], tr.position[1], tr.position[2], 0.0f};
            for (int d2 = 0; d2 < 2; ++d2) {
                alignas(16) float out[4] = {7.0f, 7.0f, 7.0f, 3.0f};
                eb.put(kOut + 0x80, pt, 16);
                eb.put(kOut + 0x90, out, 16);
                eb.call(d2 ? 0x2318b0u : 0x231cc8u, {kOut + 0x80, kOut, kOut + 0x90});
                if (d2) drv::WWorldMath_NearestPointLine2D(pt, seg, out);
                else drv::WWorldMath_NearestPointLine3D(pt, seg, out);
                if (std::memcmp(eb.m.mem(kOut + 0x90, 16), out, 16) != 0)
                    t_seg_near.fail(std::string(d2 ? "NearestPointLine2D" : "NearestPointLine3D"));
            }
            ++t_seg_dir.cases;
            eb.call(0x22caa8, {kOut + 0x40, kOut});
            if ((uint32_t(eb.m.gpr(R::v0)) & 0xff) != (drv::WTrigger_TestDirectionSeg(tr, seg) ? 1u : 0u))
                t_seg_dir.fail("TestDirection(COORD4 *)");
            ++t_seg_check.cases;
            eb.m.set_fpr(12, half);
            eb.call(0x22e3a0, {0, kOut, kOut + 0x40});
            const bool a = (uint32_t(eb.m.gpr(R::v0)) & 0xff) != 0;
            const bool b = drv::WTriggerManager_CheckCollideSeg(half, seg, tr);
            seg_touched += b;
            if (a != b)
                t_seg_check.fail(std::string("forma ") + std::to_string(tr.shape) + ": original " + (a ? "1" : "0") +
                                 ", porte " + (b ? "1" : "0"));
        }
        // ---- PBondCar::EnableTargetBeacon / DisableTargetBeacon (o original sem os ganchos dos passageiros) ----
        {
            ++t_beacon.cases;
            drv::BondCar& car = *world.car[0];
            const bool has = rng.chance(0.5), create = rng.chance(0.5), enable = rng.chance(0.6);
            drv::WTargetableData beacon{};
            beacon.unknown40 = rng.range(0, 2);
            car.d.target_beacon = has ? nf::p32_u(&beacon) : 0u;
            eb.put(kCarE[0], &car.d, 0x460);
            eb.m.write32(kCarE[0] + 0x2e0, has ? kTarget : 0u);
            std::memset(eb.m.mem(kTarget, 0x50), 0, 0x50);
            eb.m.write32(kTarget + 0x40, uint32_t(beacon.unknown40));
            eb.calls.clear();
            if (enable) eb.call(kFnEnable, {kCarE[0], create ? 1u : 0u});
            else eb.call(kFnDisable, {kCarE[0]});
            if (enable) drv::PBondCar_EnableTargetBeacon(car, create);
            else drv::PBondCar_DisableTargetBeacon(car);
            const uint32_t eo = eb.m.read32(kCarE[0] + 0x2e0);
            const uint32_t po = car.d.target_beacon;
            std::string why;
            if ((eo != 0) != (po != 0)) why = "o farol (+0x2e0)";
            else if (eo != 0 && eb.m.read32(eo + 0x40) != uint32_t(nf::p32_ptr<drv::WTargetableData>(po)->unknown40))
                why = "o +0x40 do farol";
            if (!why.empty()) t_beacon.fail(std::string(enable ? "Enable" : "Disable") + ": " + why);
            if (po != 0 && po != nf::p32_u(&beacon)) {
                drv::WTargetPicker_UnregisterTarget(po);
                drv::WTargetable_RemoveReference(po);
            }
            car.d.target_beacon = 0;
        }
        // ---- as sobreposições globais (o ActGlobalPoseOverrideArray) ----
        {
            const int n = rng.range(1, 3);
            const int32_t local = rng.chance(0.5) ? 1 : 0;
            int32_t bones[3];
            for (int& b : bones) b = rng.range(0, 7);
            ++t_ov_create.cases;
            eb.put(kOvBones, bones, 12);
            std::vector<uint8_t> raw(0x1c);
            for (auto& b : raw) b = uint8_t(rng.next());
            an::ActGlobalPoseOverrideArrayData ov{};
            std::memcpy(&ov, raw.data(), 0x1c);
            eb.put(kOv, raw.data(), 0x1c);
            eb.m.write32(kOv + 0x10, kOvGlobals);
            std::vector<float> globals(16 * 8), local_m(16 * 8);
            ov.globals = nf::p32_u(globals.data());
            ov.count = 0;
            eb.m.write32(kOv, 0);
            eb.call(kFnOvCreate, {kOv, uint32_t(n), kOvBones, uint32_t(local)});
            an::ActGlobalPoseOverrideArray_CreateGlobalPoseOverrides(ov, n, bones, local);
            auto emu_arr = [&](uint32_t off) { return eb.m.read32(kOv + off); };
            std::string why;
            if (emu_arr(0) != uint32_t(ov.count) || emu_arr(0x14) != uint32_t(ov.local_space)) why = "o número ou o local";
            for (int k = 0; k < n && why.empty(); ++k) {
                if (eb.m.read32(emu_arr(4) + 4u * uint32_t(k)) != uint32_t(nf::p32_ptr<int32_t>(ov.bones)[k])) why = "os ossos";
                if (eb.m.read32(emu_arr(0x18) + 4u * uint32_t(k)) != uint32_t(nf::p32_ptr<int32_t>(ov.set)[k]))
                    why = "o vetor dos já postos";
            }
            if (!why.empty()) t_ov_create.fail(why);
            // SetGlobalPoseOverride(s): as matrizes e os pesos (às vezes os dois de uma vez).
            const int sets = rng.range(1, 3);
            for (int s = 0; s < sets; ++s) {
                ++t_ov_set.cases;
                float mats[48], w[3];
                for (float& v : mats) v = rng.uni(-2, 2);
                for (float& v : w) v = rng.chance(0.3) ? 1.0f : (rng.chance(0.2) ? rng.uni(-0.5f, 0) : rng.uni(0, 1.5f));
                eb.put(kOut, mats, sizeof mats);
                eb.put(kOut + 0x100, w, sizeof w);
                if (rng.chance(0.5)) {
                    eb.call(kFnOvSets, {kOv, uint32_t(n), kOut, kOut + 0x100});
                    an::ActGlobalPoseOverrideArray_SetGlobalPoseOverrides(ov, mats, w);
                } else {
                    const int k = rng.range(0, n - 1);
                    eb.m.set_fpr(12, w[k]);
                    eb.call(kFnOvSet, {kOv, uint32_t(k), kOut + 0x40u * uint32_t(k)});
                    an::ActGlobalPoseOverrideArray_SetGlobalPoseOverride(ov, k, mats + 16 * k, w[k]);
                }
                std::string w2;
                const uint32_t em = emu_arr(8), ew = emu_arr(0xc), es = emu_arr(0x18);
                for (int k = 0; k < 16 * n && w2.empty(); ++k)
                    if (eb.m.read32(em + 4u * uint32_t(k)) != fbits(nf::p32_ptr<float>(ov.matrices)[k])) w2 = "as matrizes";
                for (int k = 0; k < n && w2.empty(); ++k) {
                    if (eb.m.read32(es + 4u * uint32_t(k)) != uint32_t(nf::p32_ptr<int32_t>(ov.set)[k])) w2 = "os postos";
                    if (nf::p32_ptr<int32_t>(ov.set)[k] &&
                        eb.m.read32(ew + 4u * uint32_t(k)) != fbits(nf::p32_ptr<float>(ov.weights)[k]))
                        w2 = "os pesos";
                }
                if (!w2.empty()) t_ov_set.fail(w2);
            }
            // DoGlobalOverrides(espelhado, locais) sobre matrizes locais e globais sorteadas (ortonormais com o lugar).
            ++t_ov_do.cases;
            for (int b = 0; b < 8; ++b) {
                float q[4];
                random_quat(rng, q);
                float m[4][4];
                drv::VU0_quattom4(q, m);
                for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-3, 3);
                std::memcpy(&local_m[size_t(16 * b)], m, 64);
                random_quat(rng, q);
                drv::VU0_quattom4(q, m);
                for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-3, 3);
                std::memcpy(&globals[size_t(16 * b)], m, 64);
            }
            eb.put(kOvLocal, local_m.data(), 16 * 8 * 4);
            eb.put(kOvGlobals, globals.data(), 16 * 8 * 4);
            const uint32_t mirrored = rng.chance(0.3) ? 1u : 0u;
            eb.call(kFnOvDo, {kOv, mirrored, kOvLocal});
            an::ActGlobalPoseOverrideArray_DoGlobalOverrides(ov, int32_t(mirrored), local_m.data());
            std::string w3;
            for (int k = 0; k < 16 * 8 && w3.empty(); ++k) {
                if (eb.m.read32(kOvLocal + 4u * uint32_t(k)) != fbits(local_m[size_t(k)]))
                    w3 = "a matriz local " + std::to_string(k / 16) + " [" + std::to_string(k % 16) + "]";
                if (eb.m.read32(kOvGlobals + 4u * uint32_t(k)) != fbits(globals[size_t(k)]))
                    w3 = "a matriz global " + std::to_string(k / 16);
            }
            const uint32_t em = emu_arr(8);
            for (int k = 0; k < 16 * n && w3.empty(); ++k)
                if (eb.m.read32(em + 4u * uint32_t(k)) != fbits(nf::p32_ptr<float>(ov.matrices)[k])) w3 = "as matrizes pedidas";
            if (!w3.empty()) t_ov_do.fail(std::string(local ? "local" : "global") + (mirrored ? " espelhado" : "") + ": " + w3);
            // BlendMatrices sozinho.
            ++t_ov_blend.cases;
            float a[16], b[16];
            {
                float q[4], m[4][4];
                random_quat(rng, q);
                drv::VU0_quattom4(q, m);
                for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-3, 3);
                std::memcpy(a, m, 64);
                random_quat(rng, q);
                drv::VU0_quattom4(q, m);
                std::memcpy(b, m, 64);
            }
            const float t = rng.uni(0, 1);
            eb.put(kOut, a, 64);
            eb.put(kOut + 0x40, b, 64);
            eb.m.set_fpr(12, t);
            eb.call(kFnOvBlend, {kOv, kOut, kOut + 0x40});
            an::ActGlobalPoseOverrideArray_BlendMatrices(t, a, b);
            if (std::memcmp(eb.m.mem(kOut, 64), a, 64) != 0) t_ov_blend.fail("BlendMatrices");
            // DeleteGlobalPoseOverrides: o número zerado.
            eb.call(kFnOvDelete, {kOv});
            an::ActGlobalPoseOverrideArray_DeleteGlobalPoseOverrides(ov);
            if (eb.m.read32(kOv) != uint32_t(ov.count)) t_ov_create.fail("DeleteGlobalPoseOverrides");
        }
    }
    for (int c = 0; c < 3; ++c) {
        const std::string k = kClass[c];
        report((k + ": o construtor").c_str(), t_ctor[c], failures);
        report((k + ": AICharacter::Execute").c_str(), t_exec[c], failures);
        report((k + ": os estados pela tabela virtual").c_str(), t_state[c], failures);
        if (c != 2) {
            report((k + ": TargetIsInRange").c_str(), t_range[c], failures);
            report((k + ": TargetIsAcquired").c_str(), t_acq[c], failures);
            report((k + ": PlayZoneInjuryAnim").c_str(), t_injury[c], failures);
        }
        report((k + ": GetRotPos (a função da matriz do ator)").c_str(), t_rot[c], failures);
        report((k + ": o destrutor").c_str(), t_dtor[c], failures);
    }
    report("janela: SetActor", t_setactor, failures);
    report("teto: VehicleIsAccelerating", t_accel, failures);
    report("AIElementController::WakeUp (os passageiros)", t_wake, failures);
    report("PBondCar::EnableTargetBeacon / DisableTargetBeacon", t_beacon, failures);
    report("WWorldMath::NearestPointLine3D e NearestPointLine2D", t_seg_near, failures);
    report("WTrigger::TestDirection(COORD4 *)", t_seg_dir, failures);
    report("WTriggerManager::CheckCollide(float, COORD4 *, WTrigger *)", t_seg_check, failures);
    std::printf("  (gatilhos tocados pelos segmentos: %llu)\n", (unsigned long long)seg_touched);
    report("sobreposições: Create e DeleteGlobalPoseOverrides", t_ov_create, failures);
    report("sobreposições: SetGlobalPoseOverride(s)", t_ov_set, failures);
    report("sobreposições: DoGlobalOverrides", t_ov_do, failures);
    report("sobreposições: BlendMatrices", t_ov_blend, failures);
    std::printf("  (tiros pedidos pelos Execute %" PRIu64 ", mortes %" PRIu64 ", personagens criados pelo WakeUp %" PRIu64
                ")\n",
                fires, deaths, wake_created);
    check_hit_window(elf, vu0_program, vu0_size, cases, seed, failures);
    // O mundo do porte de volta.
    drv::simulation_humans().clear();
    drv::ai_character_actor_api() = saved_api;
    drv::passenger_api() = saved_papi;
    g_w = nullptr;
    return failures;
}

}  // namespace drvcheck
