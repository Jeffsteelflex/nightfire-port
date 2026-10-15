// nf_driving_check, parte 18 (tests/driving_check_weapons.h): as armas e os efeitos do DRIVING.ELF, o código ORIGINAL
// no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte. docs/driving.md seção 22.
//
//  a) A tabela das armas do construtor estático (0x20f2b8) e o intervalo de cada uma contra a do porte
//     (kWeaponDefaultTable, kWeaponFireDelay), e o SWeaponManager construído pelo construtor original.
//  b) O SWeaponManager com estados sorteados (as armas, a munição, os pentes, os canos, os prazos, as ações na fila, o
//     gerente da missão, a direção automática, o HUD, o alvo, a animação do carro): o Update, o FireWeapon de cada
//     tipo, o LoadWeapon, o UnloadWeapon, o SetSpecialWeapon, o SelectSecondary e o resto, comparando o gerente, a tabela,
//     a fila, os globais AICharacterBond::fgRightButton/fgLeftButton, o +0x70c do míssil guiado e as chamadas aos
//     sistemas não portados na ordem (weapons_trace()).
//
// Fora do código original (ganchos nativos): a memória (__builtin_new, operator new, UMemory::FastAlloc), o
// ActionQueueManager (o porte não tem: as ações vêm do weapons_feed), e as funções dos sistemas à volta, que respondem
// com o mesmo estado sorteado que o porte lê de weapons_env() e anotam a chamada como o porte: o GHud, o RPlayerCamera,
// o WTargetPicker, o AttributeSet do jogador e do alvo, o PhysicsObject, o corpo rígido do carro, a animação do carro
// (RAnimEngine::Handle: IsSystemPlaying, GetSystemState, ProcessStimuli), o AMenuSound, o AIZoneController, o RColorize,
// os métodos do PBondCar e do RVehicle (pelas tabelas virtuais falsas), e o SMissionManager (as estatísticas, os
// contadores, o evento do programador).

#include "driving_check_weapons.h"

#include "driving_check_anim.h"
#include "driving_check_missile.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
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
constexpr uint32_t kFnWeaponTableCtor = 0x20f2b8, kFnWMCtor = 0x20bb00, kFnWMUpdate = 0x20c078;
constexpr uint32_t kFnFireWeapon = 0x20de20, kFnLoadWeapon = 0x20d378, kFnUnloadWeapon = 0x20d420;
constexpr uint32_t kFnSetSpecialWeapon = 0x20bff0, kFnSelectSecondary = 0x20d4c0, kFnSetPlayerCar = 0x20bf80;
constexpr uint32_t kFnSetSecondary = 0x20d488, kFnStartNuclear = 0x20ea20, kFnGetWeaponFromSystem = 0x20e678;
// Os ganchos.
constexpr uint32_t kFnBuiltinNew = 0x2d0910, kFnOperatorNew = 0x2d09c8, kFnFastAlloc = 0x2cfda8;
constexpr uint32_t kFnGetQueueManager = 0x165720, kFnRegisterQueue = 0x165778;
constexpr uint32_t kFnTheApp = 0x23c618, kFnLava = 0x23c718, kFnCameraAiming = 0x1bd2f8;
constexpr uint32_t kFnSelectedTarget = 0x22af38, kFnLookupBool = 0x169b28, kFnLookupInt = 0x169bf0;
constexpr uint32_t kFnGetPlayerObject = 0x201ae0, kFnGetPosition = 0x195808, kFnGetRigidBody = 0x1ff088;
constexpr uint32_t kFnIsPlaying = 0x1a2d10, kFnState = 0x1a2d90, kFnStimuli = 0x1a27a0;
constexpr uint32_t kFnZoneGet = 0x140100, kFnStrcasecmp = 0x2a9198, kFnStrcat = 0x2a924c, kFnMemset = 0x2a8038;
constexpr uint32_t kFnWWorldPos = 0x232180, kFnWWorldPosDtor = 0x232200, kFnCoord3to4 = 0x17b8b0;
// As que só anotam (as mesmas que o porte anota).
constexpr uint32_t kNoted[] = {
    0x2f4b10,  // AMenuSound::Trigger (com as cadeias)
    0x1bb010,  // RPlayerCamera::UpdateCurrentArm
    0x22b638,  // WTargetPicker::ActivateTargeting
    0x22b698,  // WTargetPicker::DeactivateTargeting
    0x1402f8,  // AIZoneController::RegisterZone (com o ponto)
    0x1d9010,  // RColorize: o desfoque (com o f12)
    0x1ffe70,  // Simulation::DetonateRemoteMines
    0x1eb078,  // RScorchWorld::ScorchGround
    0x2068e0,  // SMissionManager::IncShotsFired
    0x206990,  // SMissionManager::IncMagicCounter
    0x206ad0,  // SMissionManager::SetMagicCounter
    0x208540,  // SMissionManager::ProgrammerDefinedEvent
    0x205a90,  // SMissionManager::InitPlayerCar
};

// Os globais.
constexpr uint32_t kUberAddr = 0x100000, kMissionPtr = 0x33a024, kSim = 0x339190, kWMPtr = 0x33a530;
constexpr uint32_t kHandsPtr = 0x327670, kRightButton = 0x3268c4, kLeftButton = 0x3268c0, kCamListPtr = 0x333ed8;
constexpr uint32_t kInfiniteAmmo = 0x33a008, kAIVCPtr = 0x327c30, kZonePtr = 0x33a970;

// A RAM emulada.
constexpr uint32_t kHeap = 0x01000000, kHeapEnd = 0x01400000;
constexpr uint32_t kMission = 0x01400000;  // 0x1b50
constexpr uint32_t kCarList = 0x01402000, kCar = 0x01402100, kSceneObj = 0x01402800, kRVehicleVt = 0x01402900;
constexpr uint32_t kPBondCarVt = 0x01403000, kPhys = 0x01403400, kPlayerPos = 0x01403500, kBody = 0x01403600;
constexpr uint32_t kInfo = 0x01403800, kHud = 0x01404000, kTarget = 0x01404800, kTargetObj = 0x01404900;
constexpr uint32_t kCamList = 0x01405000, kCam = 0x01405100, kArm = 0x01405500, kAIVC = 0x01405600;
constexpr uint32_t kTrigger = 0x01405700, kMissile = 0x01406000, kZone = 0x01407000, kFake = 0x01408000;
// As funções falsas das tabelas virtuais.
constexpr uint32_t kFnTriggerFX = kFake + 0x00, kFnGetCarType = kFake + 0x10, kFnGetPhysics = kFake + 0x20;
constexpr uint32_t kFnRocketBoost = kFake + 0x30, kFnFireLaser = kFake + 0x40, kFnTwoWheel = kFake + 0x50;
constexpr uint32_t kCarTypeName = kFake + 0x100;

// As funções (falsas e os métodos do PBondCar) que o porte anota com o endereço do PS2.
constexpr uint32_t kPS2TriggerFX = 0x1d3c28, kPS2RocketBoost = 0x18b768, kPS2FireLaser = 0x18ac08;
constexpr uint32_t kPS2TwoWheel = 0x18b820;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 18) {}
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

uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
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

void put(MipsMachine& m, uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }

// O estado sorteado de um caso (o que os dois lados leem dos sistemas à volta).
struct World {
    int32_t hud_exists = 1, hud_2a0 = 0, lava = 0, aiming = 0, arm_2c = 1, is_sub = 0, boost = 0;
    int32_t only_emp = 0, has_turret = 0, target = 0, infinite = 0;
    int32_t missiles_found = 0, missiles_n = 2, rockets_found = 0, rockets_n = 2;
    std::string car_type = "vanquish";
    float player[3] = {0, 0, 0}, body_pos[4] = {0, 0, 0, 0}, body_fwd[4] = {0, 0, 1, 0}, back = 2.0f;
    uint8_t playing[0x4a] = {}, state[0x4a] = {};
};

struct Ctx {
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    World w;
    std::vector<drv::WeaponsCall> emu_calls;

    void note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, std::string s1 = "",
              std::string s2 = "") {
        drv::WeaponsCall c{fn, {a0, a1, a2, a3}, std::move(s1), std::move(s2)};
        emu_calls.push_back(std::move(c));
    }
    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    std::string error;  // a primeira falha do interpretador
    uint64_t errors = 0;
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
            note(0xeeeeeeeeu);  // a comparação acusa a diferença
        }
    }

    Ctx(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        // A tabela das armas (0xa80) com 0x60 bytes zerados antes: a secundária -1 lê a entrada -1 (no PS2 outra memória
        // do heap; o porte lê uma arma vazia).
        auto newf = [this](MipsMachine& mm) {
            const uint32_t n = uint32_t(mm.gpr(R::a0));
            if (n == 0xa80) alloc(0x60);
            mm.set_gpr(R::v0, alloc(n));
        };
        m.hook(kFnBuiltinNew, "__builtin_new", newf);
        m.hook(kFnOperatorNew, "operator new", newf);
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", newf);
        m.hook(kFnGetQueueManager, "ActionQueueManager::GetActionQueueManager",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnRegisterQueue, "ActionQueueManager::RegisterQueue", [](MipsMachine&) {});
        m.hook(kFnTheApp, "GHud::TheApp", [this](MipsMachine& mm) { mm.set_gpr(R::v0, w.hud_exists ? kHud : 0); });
        m.hook(kFnLava, "GHud::LavaDamageEffectEnabled", [this](MipsMachine& mm) { mm.set_gpr(R::v0, w.lava); });
        m.hook(kFnCameraAiming, "RPlayerCamera::CameraAiming", [this](MipsMachine& mm) { mm.set_gpr(R::v0, w.aiming); });
        m.hook(kFnSelectedTarget, "WTargetPicker::GetSelectedTarget",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, w.target ? kTarget : 0); });
        m.hook(kFnLookupBool, "AttributeSet::LookupBool", [this](MipsMachine& mm) {
            const uint32_t set = uint32_t(mm.gpr(R::a0));
            const std::string key = cstr(mm, uint32_t(mm.gpr(R::a1)));
            int32_t v = 0;
            if (set == kTargetObj + 0x5c) v = key == "ONLY_TARGET_WITH_EMP" ? w.only_emp : 0;
            else if (key == "HAS_TURRET") v = w.has_turret;
            mm.set_gpr(R::v0, uint32_t(v));
        });
        m.hook(kFnLookupInt, "AttributeSet::LookupInt", [this](MipsMachine& mm) {
            const std::string key = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const uint32_t found_at = uint32_t(mm.gpr(R::a2));
            int32_t found = 0, v = 0;
            if (key == "MISSILES_FIRED_SIMULTANEOUSLY") found = w.missiles_found, v = w.missiles_n;
            else if (key == "ROCKETS_FIRED_SIMULTANEOUSLY") found = w.rockets_found, v = w.rockets_n;
            if (found_at) mm.write32(found_at, uint32_t(found));
            mm.set_gpr(R::v0, uint32_t(v));
        });
        m.hook(kFnGetPlayerObject, "Simulation::GetPlayerObject", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCar); });
        m.hook(kFnGetPosition, "PhysicsObject::GetPosition", [](MipsMachine& mm) { mm.set_gpr(R::v0, kPlayerPos); });
        m.hook(kFnGetRigidBody, "Simulation::GetRigidBody", [](MipsMachine& mm) { mm.set_gpr(R::v0, kBody); });
        m.hook(kFnIsPlaying, "RAnimEngine::Handle::IsSystemPlaying", [this](MipsMachine& mm) {
            const uint32_t s = uint32_t(mm.gpr(R::a1));
            mm.set_gpr(R::v0, s < 0x4a ? w.playing[s] : 0);
        });
        m.hook(kFnState, "RAnimEngine::Handle::GetSystemState", [this](MipsMachine& mm) {
            const uint32_t s = uint32_t(mm.gpr(R::a1));
            mm.set_gpr(R::v0, s < 0x4a ? w.state[s] : 0);
        });
        m.hook(kFnStimuli, "RAnimEngine::Handle::ProcessStimuli", [this](MipsMachine& mm) {
            note(kFnStimuli, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)),
                 uint32_t(mm.gpr(R::t0)));
        });
        m.hook(kFnZoneGet, "AIZoneController::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, kZone); });
        m.hook(kFnWWorldPos, "WWorldPos::WWorldPos", [](MipsMachine&) {});
        m.hook(kFnWWorldPosDtor, "WWorldPos::~WWorldPos", [](MipsMachine&) {});
        m.hook(kFnCoord3to4, "Float_COORD3toCOORD4", [](MipsMachine&) {});
        m.hook(kFnStrcasecmp, "strcasecmp", [](MipsMachine& mm) {
            std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            for (char& c : a) c = char(std::tolower(uint8_t(c)));
            for (char& c : b) c = char(std::tolower(uint8_t(c)));
            mm.set_gpr(R::v0, uint32_t(a == b ? 0 : (a < b ? -1 : 1)));
        });
        m.hook(kFnStrcat, "strcat", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0));
            const std::string a = cstr(mm, d), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const std::string r = a + b;
            put(mm, d, r.c_str(), uint32_t(r.size() + 1));
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        for (uint32_t fn : kNoted) {
            m.hook(fn, "anotada", [this, fn](MipsMachine& mm) {
                uint32_t a[4] = {uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                                 uint32_t(mm.gpr(R::a3))};
                std::string s1, s2;
                if (fn == 0x2f4b10) {  // (banco, nome, grupo, modo)
                    s1 = cstr(mm, a[1]);
                    s2 = cstr(mm, a[2]);
                    note(fn, a[0], 0, 0, a[3], s1, s2);
                } else if (fn == 0x1402f8) {  // (zona, tipo, &ponto)
                    note(fn, a[1], mm.read32(a[2]), mm.read32(a[2] + 4), mm.read32(a[2] + 8));
                } else if (fn == 0x1d9010) {
                    note(fn, mm.fpr_bits(12));
                } else if (fn == 0x2068e0 || fn == 0x206990 || fn == 0x208540) {
                    note(fn, a[1]);
                } else if (fn == 0x206ad0) {
                    note(fn, a[1], a[2]);
                } else if (fn == 0x22b638) {
                    note(fn, a[1]);
                } else {
                    note(fn);
                }
                mm.set_gpr(R::v0, 0);
            });
        }
        // As tabelas virtuais falsas: o RVehicle (+0x68 TriggerFX) e o PBondCar.
        m.hook(kFnTriggerFX, "RVehicle::TriggerFX", [this](MipsMachine& mm) {
            note(kPS2TriggerFX, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)));
        });
        m.hook(kFnGetCarType, "PBondCar::GetCarType", [this](MipsMachine& mm) {
            put(mm, kCarTypeName, w.car_type.c_str(), uint32_t(w.car_type.size() + 1));
            mm.set_gpr(R::v0, kCarTypeName);
        });
        m.hook(kFnGetPhysics, "PBondCar::GetPhysics", [this](MipsMachine& mm) {
            mm.write32(kPhys + 0xbc, uint32_t(w.is_sub));
            mm.set_gpr(R::v0, kPhys);
        });
        m.hook(kFnRocketBoost, "PBondCar::EnableRocketBoost", [this](MipsMachine& mm) {
            note(kPS2RocketBoost);
            mm.set_gpr(R::v0, uint32_t(w.boost));
        });
        m.hook(kFnFireLaser, "PBondCar::FireLaser", [this](MipsMachine&) { note(kPS2FireLaser); });
        m.hook(kFnTwoWheel, "PBondCar::EnableTwoWheelStunt", [this](MipsMachine&) { note(kPS2TwoWheel); });
        // Os objetos fixos.
        m.write32(kSim + 0xdcc, kCarList);
        m.write32(kCarList, kCar);
        m.write32(kCar + 0x46, 0);
        m.write32(kCar + 0x48, kSceneObj);
        m.write32(kCar + 0x68, kPBondCarVt);
        m.write32(kSceneObj + 0x28, 0x1234);  // o RAnimEngine::Handle (os ganchos não o leem)
        m.write32(kSceneObj + 0x3c, kRVehicleVt);
        m.write32(kRVehicleVt + 0x6c, kFnTriggerFX);
        m.write32(kPBondCarVt + 0x84, kFnGetCarType);
        m.write32(kPBondCarVt + 0x1d4, kFnGetPhysics);
        m.write32(kPBondCarVt + 0x234, kFnRocketBoost);
        m.write32(kPBondCarVt + 0x24c, kFnFireLaser);
        m.write32(kPBondCarVt + 0x1c4, kFnTwoWheel);
        m.write32(kBody + 0x5c, kInfo);
        m.write32(kTarget + 0x34, 1);
        m.write32(kTarget + 0x48, kTargetObj);
        m.write32(kCamListPtr, kCamList);
        m.write32(kCamList + 4, kCam);
        m.write32(kCam + 0x354, kArm);
        m.write32(kAIVCPtr, kAIVC);
        m.write32(kAIVC + 8, 0);
        m.write32(kHandsPtr, 0);
        m.write32(kMissionPtr, kMission);
        m.write32(kZonePtr, kZone);
    }
};

// O porte lê o mesmo mundo.
void install_env(Ctx& c, uint32_t port_target) {
    drv::WeaponsEnv& e = drv::weapons_env();
    World& w = c.w;
    e = drv::WeaponsEnv{};
    e.anim.is_playing = [&w](uint32_t, int32_t s) { return s >= 0 && s < 0x4a && w.playing[s] != 0; };
    e.anim.state = [&w](uint32_t, int32_t s) { return s >= 0 && s < 0x4a ? int32_t(w.state[s]) : 0; };
    e.anim.stimulus = [](uint32_t, int32_t s, int32_t stim, int32_t time, int32_t mode) {
        drv::weapons_note_call(kFnStimuli, uint32_t(s), uint32_t(stim), uint32_t(time), uint32_t(mode));
    };
    e.anim_handle = [](uint32_t) { return 0x1234u; };
    e.lookup_int = [&w](const char* key, bool* found) {
        int32_t f = 0, v = 0;
        if (std::strcmp(key, "MISSILES_FIRED_SIMULTANEOUSLY") == 0) f = w.missiles_found, v = w.missiles_n;
        else if (std::strcmp(key, "ROCKETS_FIRED_SIMULTANEOUSLY") == 0) f = w.rockets_found, v = w.rockets_n;
        if (found) *found = f != 0;
        return v;
    };
    e.lookup_bool = [&w](const char* key) { return std::strcmp(key, "HAS_TURRET") == 0 && w.has_turret != 0; };
    e.player_position = [&w](float out[3]) { std::memcpy(out, w.player, 12); };
    e.car_type = [&w]() { return w.car_type.c_str(); };
    e.car_is_sub = [&w]() { return w.is_sub; };
    e.camera_exists = []() { return true; };
    e.camera_aiming = [&w]() { return w.aiming != 0; };
    e.selected_target = [&w, port_target]() { return w.target ? port_target : 0u; };
    e.target_only_emp = [&w](uint32_t) { return w.only_emp != 0; };
    e.camera_arm_2c = [&w]() { return w.arm_2c; };
    e.player_body = [&w](float pos[4], float fwd[4], float& back) {
        std::memcpy(pos, w.body_pos, 16);
        std::memcpy(fwd, w.body_fwd, 16);
        back = w.back;
    };
    e.enable_rocket_boost = [&w]() { return w.boost != 0; };
    e.hud_exists = [&w]() { return w.hud_exists != 0; };
    e.hud_2a0 = [&w]() { return w.hud_2a0; };
    e.hud_lava_damage = [&w]() { return w.lava != 0; };
    e.infinite_ammo = w.infinite;
}

// O mundo no lado emulado (o que os ganchos não respondem na hora).
void world_to_emu(Ctx& c) {
    MipsMachine& m = c.m;
    const World& w = c.w;
    m.write32(kHud + 0x2a0, uint32_t(w.hud_2a0));
    m.write32(kArm + 0x2c, uint32_t(w.arm_2c));
    m.write32(kInfiniteAmmo, uint32_t(w.infinite));
    put(m, kPlayerPos, w.player, 12);
    put(m, kBody + 0x10, w.body_pos, 16);
    put(m, kInfo + 0x20, w.body_fwd, 16);
    put(m, kInfo + 0x4c8, &w.back, 4);
}

// ---------------------------------------------------------------- a) a tabela e o construtor

constexpr uint32_t kWeaponTable = 0x4165c0, kFireDelay = 0x33a030, kSortOrder = 0x33a4b0, kSystems = 0x33a0b0;

int check_table(Ctx& c, std::string& why) {
    MipsMachine& m = c.m;
    int bad = 0;
    for (int i = 0; i < 32; ++i) {
        for (int k = 0; k < 21; ++k) {
            const uint32_t emu = m.read32(kWeaponTable + uint32_t(i) * 0x54 + uint32_t(k) * 4);
            const drv::WeaponWord& d = drv::kWeaponDefaultTable[i][k];
            const bool same = d.s ? (cstr(m, emu) == d.s && emu != 0) : (emu == uint32_t(d.v));
            if (!same && bad++ == 0) why = "tabela " + std::to_string(i) + " palavra " + std::to_string(k);
        }
        if (m.read32(kFireDelay + uint32_t(i) * 4) != fbits(drv::kWeaponFireDelay[i]) && bad++ == 0)
            why = "intervalo " + std::to_string(i);
        if (m.read32(kSortOrder + uint32_t(i) * 4) != uint32_t(drv::kWeaponSortOrder[i]) && bad++ == 0)
            why = "ordem " + std::to_string(i);
        for (int k = 0; k < 8; ++k) {
            if (m.read32(kSystems + uint32_t(i) * 0x20 + uint32_t(k) * 4) != uint32_t(drv::kWeaponSystems[i][k]) &&
                bad++ == 0)
                why = "sistemas " + std::to_string(i);
        }
    }
    return bad;
}

// O gerente, a tabela e a fila emulados (endereços).
struct Emu {
    uint32_t wm = 0, table = 0, queue = 0;
};

// O lado emulado recebe o estado do porte (os ponteiros trocados pelos emulados).
void port_to_emu(Ctx& c, const Emu& e, uint32_t port_trigger) {
    MipsMachine& m = c.m;
    drv::SWeaponManagerData& w = *drv::weapon_manager();
    uint8_t buf[0x190];
    std::memcpy(buf, &w, 0x190);
    auto set = [&](uint32_t off, uint32_t v) { std::memcpy(buf + off, &v, 4); };
    set(0x10, e.table);
    set(0x14, w.player_car ? kSceneObj : 0);
    set(0x20, e.queue);
    set(0x80, w.special_trigger == 0 ? 0 : (w.special_trigger == port_trigger ? kTrigger : 0xdead));
    put(m, e.wm, buf, 0x190);
    const drv::WeaponEntry* t = drv::weapon_entry(w, 0);
    for (int i = 0; i < 32; ++i) {
        uint8_t eb[0x54];
        std::memcpy(eb, &t[i], 0x54);
        const uint32_t name = m.read32(kWeaponTable + uint32_t(i) * 0x54 + 4);
        const uint32_t snd = m.read32(kWeaponTable + uint32_t(i) * 0x54 + 0x44);
        std::memcpy(eb + 4, &name, 4);
        std::memcpy(eb + 0x44, &snd, 4);
        put(m, e.table + uint32_t(i) * 0x54, eb, 0x54);
    }
    const drv::ActionQueueData& q = *drv::weapon_queue(w);
    put(m, e.queue, &q, sizeof q);
    m.write32(e.queue, 0);
}

std::string compare_calls(const std::vector<drv::WeaponsCall>& pc, const std::vector<drv::WeaponsCall>& ec) {
    char b[200];
    if (pc.size() != ec.size()) {
        std::snprintf(b, sizeof b, "chamadas: porte %zu original %zu", pc.size(), ec.size());
        std::string r = b;
        const size_t n = std::min(pc.size(), ec.size());
        for (size_t i = 0; i < n; ++i) {
            if (pc[i].fn != ec[i].fn) {
                std::snprintf(b, sizeof b, " (a %zu: porte %x original %x)", i, pc[i].fn, ec[i].fn);
                return r + b;
            }
        }
        if (n < pc.size()) std::snprintf(b, sizeof b, " (porte a mais %x)", pc[n].fn);
        else std::snprintf(b, sizeof b, " (original a mais %x)", ec[n].fn);
        return r + b;
    }
    for (size_t i = 0; i < pc.size(); ++i) {
        const drv::WeaponsCall& x = pc[i];
        const drv::WeaponsCall& y = ec[i];
        if (x.fn != y.fn || std::memcmp(x.args, y.args, sizeof x.args) != 0 || x.s1 != y.s1 || x.s2 != y.s2) {
            std::snprintf(b, sizeof b, "chamada %zu: porte %x(%x %x %x %x '%s' '%s'), original %x(%x %x %x %x '%s' '%s')", i,
                          x.fn, x.args[0], x.args[1], x.args[2], x.args[3], x.s1.c_str(), x.s2.c_str(), y.fn, y.args[0],
                          y.args[1], y.args[2], y.args[3], y.s1.c_str(), y.s2.c_str());
            return b;
        }
    }
    return "";
}

std::string compare_state(Ctx& c, const Emu& e, uint32_t port_trigger, const uint8_t* port_missile) {
    MipsMachine& m = c.m;
    drv::SWeaponManagerData& w = *drv::weapon_manager();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&w);
    char b[160];
    for (uint32_t off = 0; off < 0x190; off += 4) {
        uint32_t pv, ev = m.read32(e.wm + off);
        std::memcpy(&pv, p + off, 4);
        if (off == 0x10) pv = pv ? e.table : 0;
        if (off == 0x14) pv = pv ? kSceneObj : 0;
        if (off == 0x20) pv = pv ? e.queue : 0;
        if (off == 0x80) pv = pv == 0 ? 0 : (pv == port_trigger ? kTrigger : 0xdead);
        if (pv != ev) {
            std::snprintf(b, sizeof b, "gerente +0x%x: porte %08x original %08x", off, pv, ev);
            return b;
        }
    }
    const drv::WeaponEntry* t = drv::weapon_entry(w, 0);
    for (int i = 0; i < 32; ++i) {
        const uint8_t* pe = reinterpret_cast<const uint8_t*>(&t[i]);
        for (uint32_t off = 0; off < 0x54; off += 4) {
            uint32_t pv, ev = m.read32(e.table + uint32_t(i) * 0x54 + off);
            std::memcpy(&pv, pe + off, 4);
            if (off == 4 || off == 0x44) {
                const char* ps = nf::p32_ptr<const char>(pv);
                if ((ps ? std::string(ps) : std::string()) != cstr(m, ev)) {
                    std::snprintf(b, sizeof b, "arma %d +0x%x: a cadeia", i, off);
                    return b;
                }
                continue;
            }
            if (pv != ev) {
                std::snprintf(b, sizeof b, "arma %d +0x%x: porte %08x original %08x", i, off, pv, ev);
                return b;
            }
        }
    }
    const drv::ActionQueueData& q = *drv::weapon_queue(w);
    const uint8_t* pq = reinterpret_cast<const uint8_t*>(&q);
    for (uint32_t off = 4; off < sizeof q; off += 4) {
        uint32_t pv, ev = m.read32(e.queue + off);
        std::memcpy(&pv, pq + off, 4);
        if (pv != ev) {
            std::snprintf(b, sizeof b, "fila +0x%x: porte %08x original %08x", off, pv, ev);
            return b;
        }
    }
    const drv::WeaponsEnv& env = drv::weapons_env();
    if (uint32_t(env.fg_right_button) != m.read32(kRightButton) || uint32_t(env.fg_left_button) != m.read32(kLeftButton))
        return "AICharacterBond::fgRightButton/fgLeftButton";
    uint32_t pm;
    std::memcpy(&pm, port_missile + 0x70c, 4);
    if (pm != m.read32(kMissile + 0x70c)) return "o +0x70c do míssil guiado";
    return compare_calls(drv::weapons_trace(), c.emu_calls);
}

// ---------------------------------------------------------------- b) os casos sorteados

void random_world(Rng& r, World& w) {
    w.hud_exists = r.chance(0.9);
    w.hud_2a0 = r.chance(0.1);
    w.lava = r.chance(0.1);
    w.aiming = r.chance(0.15);
    w.arm_2c = r.chance(0.5);
    w.is_sub = r.chance(0.1);
    w.boost = r.chance(0.5);
    w.only_emp = r.chance(0.3);
    w.has_turret = r.chance(0.1);
    w.target = r.chance(0.4);
    w.infinite = r.chance(0.1);
    w.missiles_found = r.chance(0.5);
    w.missiles_n = r.range(1, 4);
    w.rockets_found = r.chance(0.5);
    w.rockets_n = r.range(1, 4);
    w.car_type = r.chance(0.1) ? "rental" : "vanquish";
    for (float& f : w.player) f = r.real(-100, 100);
    for (int i = 0; i < 3; ++i) w.body_pos[i] = r.real(-100, 100), w.body_fwd[i] = r.real(-1, 1);
    w.body_pos[3] = r.real(-1, 1);
    w.body_fwd[3] = r.real(-1, 1);
    w.back = r.real(0, 5);
    for (int s = 0; s < 0x4a; ++s) {
        w.playing[s] = uint8_t(r.chance(0.3));
        w.state[s] = uint8_t(r.range(0, 3));
    }
}

int pick_type(Rng& r) {
    static const int kCommon[] = {0, 1, 2, 3, 2, 2, 11, 14, 26, 21, 4, 8, 10, 12, 13, 15, 18, 20, 23, 24, 27, 0x1c};
    return r.chance(0.8) ? kCommon[r.next() % (sizeof kCommon / sizeof kCommon[0])] : r.range(0, 31);
}

void random_manager(Rng& r, drv::SWeaponManagerData& w, int32_t steps, uint32_t port_trigger) {
    w.primary = r.chance(0.5) ? -1 : pick_type(r);
    w.secondary = r.chance(0.05) ? -1 : pick_type(r);
    w.special = r.chance(0.5) ? -1 : pick_type(r);
    w.hands_secondary = pick_type(r);
    w.last_fired = r.chance(0.2) ? -1 : pick_type(r);
    w.last_target = r.chance(0.5) ? 0 : kTarget;
    w.primary_held = r.chance(0.3);
    w.secondary_held = r.chance(0.3);
    w.special_held = r.chance(0.3);
    w.hands_fired = r.chance(0.3);
    w.reload_pending = r.chance(0.3);
    w.boost_blur = r.chance(0.5) ? 0 : r.range(-5, 200);
    w.multi_damage = r.chance(0.3);
    w.next_select = steps + r.range(-30, 10);
    w.click_count = r.range(0, 5);
    w.nuclear_state = 0;  // o HandleNuclearSequence depois do disparo não foi portado
    w.special_trigger = r.chance(0.5) ? port_trigger : 0;
    w.special_range = r.real(0, 200);
    drv::WeaponEntry* t = drv::weapon_entry(w, 0);
    for (int i = 0; i < 32; ++i) {
        drv::WeaponEntry& e = t[i];
        int nsys = 0;
        while (nsys < 8 && drv::kWeaponSystems[i][nsys] != 0) ++nsys;
        e.flags = r.next() & 0x67u;
        e.ammo = r.chance(0.2) ? r.range(-2, 0) : r.range(1, 40);
        e.clip = r.range(-1, 6);
        e.clip_size = r.chance(0.5) ? 4 : r.range(1, 6);
        for (int32_t& f : e.last_fire) f = r.chance(0.2) ? -1000 : steps - r.range(0, 80);
        e.fire_delay = r.range(0, 40);
        e.barrel = nsys > 1 ? r.range(0, nsys - 1) : 0;
        e.reload_barrel = nsys > 1 ? r.range(0, nsys - 1) : 0;
        e.sound_bank = r.chance(0.1) ? -1 : 4;
        e.shots = r.range(0, 9);
    }
    drv::ActionQueueData& q = *drv::weapon_queue(w);
    drv::ActionQueue_Flush(q);
    const int n = r.chance(0.3) ? 0 : r.range(1, 6);
    static const int kActions[] = {0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x3f, 0x1c, 0x26, 0x40};
    for (int i = 0; i < n; ++i) drv::ActionQueue_Push(q, {kActions[r.next() % 13], r.range(0, 2), r.real(0, 1)});
}

}  // namespace

int run_weapons_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    (void)disc;
    int failures = 0;
    std::printf("\nparte 18: as armas e os efeitos (o SWeaponManager, a tabela das armas)\n");
    Ctx c(elf, vu0_program, vu0_size);
    c.call(kFnWeaponTableCtor, {1, 0xffff});
    {
        std::string why;
        const int bad = check_table(c, why);
        std::printf("  a tabela das armas do construtor estático (32 armas, os intervalos, a ordem, os sistemas): %s%s\n",
                    bad ? "DIFERENTE: " : "igual", why.c_str());
        failures += bad != 0;
    }
    // O construtor original e o do porte (a taxa de quadros 60, sem trapaças).
    drv::simulation_reset(60);
    drv::uber_bond_defaults("paris_mis01", 1);
    drv::SMissionManager_Construct(false);
    c.m.write32(kSim + 0xdbc, 60);
    c.m.write32(kUberAddr + 0x9a0, 0);
    Emu e;
    e.wm = c.alloc(0x190);
    c.call(kFnWMCtor, {e.wm});
    e.table = c.m.read32(e.wm + 0x10);
    e.queue = c.m.read32(e.wm + 0x20);
    c.m.write32(kWMPtr, e.wm);  // o glbWeaponManager (0x33a530): o Update chama o FirePrimary e o resto por ele
    drv::weapons_create();
    alignas(16) static float port_trigger_pos[4] = {0, 0, 0, 0};
    const uint32_t port_trigger = nf::p32_u(port_trigger_pos);
    alignas(16) static uint8_t port_missile[0x730] = {};
    {
        const std::string d = compare_state(c, e, port_trigger, port_missile);
        std::printf("  SWeaponManager::SWeaponManager (a tabela copiada, a fila, as prioridades): %s%s\n",
                    d.empty() ? "igual" : "DIFERENTE: ", d.c_str());
        failures += !d.empty();
    }

    Rng r(seed);
    drv::weapons_trace_enabled() = true;
    const char* kOpNames[] = {"Update", "FireWeapon", "LoadWeapon", "UnloadWeapon", "SetSpecialWeapon",
                              "SelectSecondary", "SetSecondary", "SetPlayerCar", "StartNuclearSequence",
                              "GetWeaponFromSystem"};
    constexpr int kOps = 10;
    uint64_t tried[kOps] = {}, bad[kOps] = {};
    std::string first_bad;
    // O estado sorteado de um caso nos dois lados.
    auto prepare = [&]() {
        World& w = c.w;
        random_world(r, w);
        install_env(c, kTarget);
        world_to_emu(c);
        drv::Simulation& sim = drv::simulation();
        sim.state = r.chance(0.1) ? 3 : 0;
        sim.frame_rate = r.chance(0.8) ? 60 : 50;
        sim.steps = r.range(100, 100000);
        c.m.write32(kSim + 0xdb4, uint32_t(sim.state));
        c.m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
        c.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
        drv::SMissionManagerData& mm = drv::mission_manager();
        mm.state = r.chance(0.8) ? 1 : r.range(0, 4);
        mm.auto_drive = r.chance(0.2);
        mm.missile = r.chance(0.15) ? nf::p32_u(port_missile) : 0;
        mm.cinematic_mode = r.chance(0.15) ? uint32_t(r.range(1, 7)) : 0;
        mm.unknown5ec = r.chance(0.3);
        std::memset(c.m.mem(kMission, 0x1b50), 0, 0x1b50);
        c.m.write32(kMission + 0x34c, uint32_t(mm.state));
        c.m.write32(kMission + 0x350, uint32_t(mm.auto_drive));
        c.m.write32(kMission + 0x358, mm.missile ? kMissile : 0);
        c.m.write32(kMission + 0x3cc, mm.cinematic_mode);
        c.m.write32(kMission + 0x5ec, uint32_t(mm.unknown5ec));
        std::memset(port_missile, 0, sizeof port_missile);
        c.m.write32(kMissile + 0x70c, 0);
        drv::UberBondBlock& u = drv::uber_bond();
        u.word(0x9a0) = r.chance(0.2) ? int32_t(r.next() & 0xf) : 0;
        u.word(0x9a4) = r.chance(0.2) ? int32_t(r.next() & 0x3) : 0;
        u.word(0xa48) = r.chance(0.3);
        c.m.write32(kUberAddr + 0x9a0, uint32_t(u.word(0x9a0)));
        c.m.write32(kUberAddr + 0x9a4, uint32_t(u.word(0x9a4)));
        c.m.write32(kUberAddr + 0xa48, uint32_t(u.word(0xa48)));
        for (int i = 0; i < 3; ++i) port_trigger_pos[i] = r.real(-100, 100);
        put(c.m, kTrigger, port_trigger_pos, 12);
        drv::SWeaponManagerData& wm = *drv::weapon_manager();
        random_manager(r, wm, sim.steps, port_trigger);
        wm.player_car = 1u;  // o SetPlayerCar do GameLoop_StartUp vem antes de qualquer Update
        drv::weapons_env().fg_right_button = r.chance(0.5);
        drv::weapons_env().fg_left_button = r.chance(0.5);
        c.m.write32(kRightButton, uint32_t(drv::weapons_env().fg_right_button));
        c.m.write32(kLeftButton, uint32_t(drv::weapons_env().fg_left_button));
        port_to_emu(c, e, port_trigger);
    };
    for (int k = 0; k < cases; ++k) {
        World& w = c.w;
        prepare();
        drv::SWeaponManagerData& wm = *drv::weapon_manager();
        drv::weapons_trace().clear();
        c.emu_calls.clear();
        const int op = r.chance(0.5) ? 0 : r.range(1, kOps - 1);
        if (op == 0) w.has_turret = 0;  // o UpdateTurret não foi portado (nenhum carro de Paris tem torre)
        ++tried[op];
        switch (op) {
            case 0:
                c.call(kFnWMUpdate, {e.wm});
                drv::SWeaponManager_Update(wm);
                break;
            case 1: {
                const int t = pick_type(r);
                c.call(kFnFireWeapon, {e.wm, uint32_t(t)});
                drv::SWeaponManager_FireWeapon(wm, t);
                break;
            }
            case 2: {
                const int t = pick_type(r), a = r.range(-5, 60), b = r.range(-2, 8), d = r.range(-2, 300);
                c.call(kFnLoadWeapon, {e.wm, uint32_t(t), uint32_t(a), uint32_t(b), uint32_t(d)});
                drv::SWeaponManager_LoadWeapon(wm, t, a, b, d);
                break;
            }
            case 3: {
                const int t = pick_type(r);
                c.call(kFnUnloadWeapon, {e.wm, uint32_t(t)});
                drv::SWeaponManager_UnloadWeapon(wm, t);
                break;
            }
            case 4: {
                const int t = r.chance(0.3) ? 2 : pick_type(r);
                const bool trig = r.chance(0.5);
                const float range = r.real(0, 300);
                c.m.set_fpr(12, range);
                c.call(kFnSetSpecialWeapon, {e.wm, uint32_t(t), trig ? kTrigger : 0});
                drv::SWeaponManager_SetSpecialWeapon(wm, t, trig ? port_trigger : 0, range);
                break;
            }
            case 5: {
                const bool next = r.chance(0.5);
                c.call(kFnSelectSecondary, {e.wm, uint32_t(next)});
                drv::SWeaponManager_SelectSecondary(wm, next);
                break;
            }
            case 6: {
                const int t = pick_type(r);
                c.call(kFnSetSecondary, {e.wm, uint32_t(t)});
                drv::SWeaponManager_SetSecondary(wm, t);
                break;
            }
            case 7:
                c.call(kFnSetPlayerCar, {e.wm, kSceneObj});
                drv::SWeaponManager_SetPlayerCar(wm, 1u);
                break;
            case 8: {
                const float pos[3] = {r.real(-500, 500), r.real(-50, 50), r.real(-500, 500)};
                const float rad = r.real(0, 2);
                put(c.m, kTrigger + 0x40, pos, 12);
                c.m.set_fpr(12, rad);
                c.call(kFnStartNuclear, {e.wm, kTrigger + 0x40});
                drv::SWeaponManager_StartNuclearSequence(wm, pos, rad);
                break;
            }
            case 9: {
                const int s = r.range(0, 0x4a);
                c.call(kFnGetWeaponFromSystem, {e.wm, uint32_t(s)});
                const int32_t emu = int32_t(c.m.gpr(R::v0));
                if (emu != drv::SWeaponManager_GetWeaponFromSystem(s)) c.note(0xffffffffu);  // força a diferença
                break;
            }
            default:
                break;
        }
        const std::string d = compare_state(c, e, port_trigger, port_missile);
        if (!d.empty()) {
            ++bad[op];
            if (first_bad.empty()) first_bad = std::string(kOpNames[op]) + " caso " + std::to_string(k) + ": " + d;
        }
    }

    // Os destrutores dos eventos das armas e dos power-ups (a imagem do objeto sorteada; os ponteiros para uma
    // instância, um estado de animação procedural e o objeto de cena dele falsos nos dois lados).
    struct EvCase {
        int32_t type;
        uint32_t dtor;
        const char* name;
    };
    static const EvCase kEvents[] = {
        {25, 0x1554c0, "EPowerUp"},          {56, 0x1556b8, "EPowerUpAmmo"},     {60, 0x155a70, "EPowerUpMultiDamage"},
        {149, 0x14b630, "EDisablePowerUp"},  {150, 0x14bb58, "EEnablePowerUp"},  {192, 0x158df0, "ESetSpecialWeapon"},
        {249, 0x14f0a0, "EGadgetOn"},        {269, 0x15dcf8, "EUnloadWeapon"},    {11, 0x14f168, "EHideInstance"},
        {12, 0x159a88, "EShowInstance"},
    };
    constexpr uint32_t kEvObj = 0x01409000, kInst = 0x01409100, kHolder = 0x01409200, kHolderScene = 0x01409300;
    constexpr uint32_t kArticle = 0x01409400, kModelGroup = 0x01409500, kModelC = 0x01409600;
    alignas(16) static uint8_t port_inst[0x40], port_holder[0x40], port_scene[0x40], port_article[0x40],
        port_group[0x40], port_model[0x40];
    int32_t scene_found = 0;
    c.m.hook(0x232f70, "GFX::Trigger", [&c](MipsMachine& mm) {
        const uint32_t pos = uint32_t(mm.gpr(R::a1));
        c.note(0x232f70, mm.read32(pos), mm.read32(pos + 4), mm.read32(pos + 8), 0, cstr(mm, uint32_t(mm.gpr(R::a0))));
    });
    c.m.hook(0x1c8b78, "RSceneObj::Hide", [&c](MipsMachine&) { c.note(0x1c8b78); });
    c.m.hook(0x1c8b90, "RSceneObj::Show", [&c](MipsMachine&) { c.note(0x1c8b90); });
    c.m.hook(0x2015d8, "Simulation::TrackInstance", [&c](MipsMachine&) { c.note(0x2015d8); });
    c.m.hook(0x201690, "Simulation::UntrackInstance", [&c](MipsMachine&) { c.note(0x201690); });
    c.m.hook(0x1a2868, "RAnimEngine::Handle::ProcessStimuli", [&c](MipsMachine& mm) {
        c.note(0x1a2868, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)));
    });
    c.m.hook(0x245890, "GHud::TriggerMissionMsg", [&c](MipsMachine& mm) { c.note(0x245890, uint32_t(mm.gpr(R::a1))); });
    c.m.hook(0x244f48, "GHud::GadgetOn", [&c](MipsMachine& mm) { c.note(0x244f48, uint32_t(mm.gpr(R::a1)), mm.fpr_bits(12)); });
    c.m.hook(0x230bf0, "WWorld::GetSceneObjFromInstance",
             [&scene_found](MipsMachine& mm) { mm.set_gpr(R::v0, scene_found ? kHolderScene : 0); });
    c.m.hook(0x175758, "Event::operator_delete", [](MipsMachine&) {});
    uint64_t ev_tried[10] = {}, ev_bad[10] = {};
    std::string ev_first;
    for (int k = 0; k < cases / 2; ++k) {
        World& w = c.w;
        prepare();
        drv::hud_exists() = w.hud_exists != 0;
        scene_found = r.chance(0.7);
        drv::weapons_env().scene_obj_of = [&scene_found](uint32_t) { return scene_found ? 1u : 0u; };
        const int ei = r.range(0, 9);
        const EvCase& ec = kEvents[ei];
        ++ev_tried[ei];
        // A instância, o estado (o "holder"), o objeto de cena dele (o +0x28: a animação) e o modelo do artigo.
        const bool has_inst = r.chance(0.9), has_holder = r.chance(0.8), has_scene = r.chance(0.8);
        const uint32_t handle = r.chance(0.7) ? 0x5678u : 0u;
        const uint32_t model_tag = r.chance(0.3) ? 0x696u : uint32_t(r.range(0, 0x1000));
        std::memset(port_inst, 0, sizeof port_inst);
        std::memset(port_holder, 0, sizeof port_holder);
        std::memset(port_scene, 0, sizeof port_scene);
        port_inst[0xc] = uint8_t(r.next());
        const uint16_t hf = uint16_t(r.next());
        std::memcpy(port_holder + 0x12, &hf, 2);
        auto w32 = [](uint8_t* p, uint32_t off, uint32_t v) { std::memcpy(p + off, &v, 4); };
        w32(port_inst, 0x1c, nf::p32_u(port_article));
        w32(port_article, 0, nf::p32_u(port_group));
        w32(port_group, 0xc, nf::p32_u(port_model));
        w32(port_model, 0x28, model_tag);
        w32(port_holder, 8, has_scene ? nf::p32_u(port_scene) : 0);
        w32(port_scene, 0x28, handle);
        std::memset(c.m.mem(kInst, 0x700), 0, 0x700);
        put(c.m, kInst, port_inst, 0x40);
        c.m.write32(kInst + 0x1c, kArticle);
        c.m.write32(kArticle, kModelGroup);
        c.m.write32(kModelGroup + 0xc, kModelC);
        c.m.write32(kModelC + 0x28, model_tag);
        c.m.write32(kHolder + 0x12 - 2, 0);
        put(c.m, kHolder + 0x12, &hf, 2);
        c.m.write32(kHolder + 8, has_scene ? kHolderScene : 0);
        c.m.write32(kHolderScene + 0x28, handle);
        drv::PendingEvent pe{};
        pe.type = ec.type;
        uint32_t emu_obj[8] = {};
        for (int i = 1; i < 8; ++i) emu_obj[i] = pe.obj[i] = r.next();
        switch (ec.type) {
            case 25:
                pe.obj[1] = has_inst ? nf::p32_u(port_inst) : 0, emu_obj[1] = has_inst ? kInst : 0;
                pe.obj[2] = has_holder ? nf::p32_u(port_holder) : 0, emu_obj[2] = has_holder ? kHolder : 0;
                for (int i = 3; i < 7; ++i) emu_obj[i] = pe.obj[i] = fbits(r.real(-500, 500));
                break;
            case 56:
            case 269:
                emu_obj[1] = pe.obj[1] = uint32_t(pick_type(r));
                emu_obj[2] = pe.obj[2] = uint32_t(r.range(-3, 40));
                break;
            case 11:
            case 12:
                // (o EShowInstance não confere o ponteiro: sempre uma instância)
                pe.obj[1] = has_inst || ec.type == 12 ? nf::p32_u(port_inst) : 0;
                emu_obj[1] = has_inst || ec.type == 12 ? kInst : 0;
                break;
            case 149:
            case 150:
                pe.obj[1] = has_inst ? nf::p32_u(port_inst) : 0, emu_obj[1] = has_inst ? kInst : 0;
                pe.obj[2] = has_holder ? nf::p32_u(port_holder) : 0, emu_obj[2] = has_holder ? kHolder : 0;
                break;
            case 192: {
                emu_obj[1] = pe.obj[1] = uint32_t(r.chance(0.3) ? 2 : pick_type(r));
                const bool trig = r.chance(0.5);
                pe.obj[2] = trig ? port_trigger : 0, emu_obj[2] = trig ? kTrigger : 0;
                emu_obj[3] = pe.obj[3] = fbits(r.real(0, 300));
                break;
            }
            case 249:
                emu_obj[2] = pe.obj[2] = fbits(r.real(0, 10));
                break;
            default:
                break;
        }
        put(c.m, kEvObj, emu_obj, sizeof emu_obj);
        drv::weapons_trace().clear();
        c.emu_calls.clear();
        c.call(ec.dtor, {kEvObj, 3});
        drv::weapons_event_execute(pe);
        std::string d = compare_state(c, e, port_trigger, port_missile);
        if (d.empty() && (port_inst[0xc] != c.m.mem(kInst, 0x40)[0xc])) d = "a instância +0xc";
        if (d.empty() && std::memcmp(port_holder + 0x12, c.m.mem(kHolder, 0x40) + 0x12, 2) != 0) d = "o estado +0x12";
        if (!d.empty()) {
            ++ev_bad[ei];
            if (ev_first.empty()) ev_first = std::string(ec.name) + " caso " + std::to_string(k) + ": " + d;
        }
    }
    drv::weapons_env().scene_obj_of = nullptr;
    for (int i = 0; i < 10; ++i) {
        std::printf("  ~%-28s %6" PRIu64 " casos, %" PRIu64 " diferentes\n", kEvents[i].name, ev_tried[i], ev_bad[i]);
        failures += int(ev_bad[i]);
    }
    if (!ev_first.empty()) std::printf("    primeira diferença: %s\n", ev_first.c_str());
    drv::weapons_trace_enabled() = false;
    uint64_t total_bad = 0;
    for (int op = 0; op < kOps; ++op) {
        std::printf("  SWeaponManager::%-22s %6" PRIu64 " casos, %" PRIu64 " diferentes\n", kOpNames[op], tried[op],
                    bad[op]);
        total_bad += bad[op];
    }
    if (!first_bad.empty()) std::printf("    primeira diferença: %s\n", first_bad.c_str());
    if (c.errors) std::printf("    o interpretador falhou %" PRIu64 " vezes: %s\n", c.errors, c.error.c_str());
    std::printf("  (o porte: %" PRIu64 " estímulos, %" PRIu64 " tiros, %" PRIu64 " recargas, %" PRIu64
                " guardas; o interpretador: %" PRIu64 " instruções)\n",
                drv::weapons_stats().stimuli, drv::weapons_stats().fired, drv::weapons_stats().reloads,
                drv::weapons_stats().guards, c.m.steps());
    failures += int(total_bad);
    drv::weapons_destroy();
    drv::mission_reset();
    // O motor de animação das instâncias (tests/driving_check_anim.cpp).
    failures += run_anim_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    // O corpo simples, as explosões, o míssil e o tiro (tests/driving_check_missile.cpp).
    failures += run_missile_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    return failures;
}

}  // namespace drvcheck
