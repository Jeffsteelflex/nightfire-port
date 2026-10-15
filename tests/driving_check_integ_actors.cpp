// nf_driving_check, parte 20, os atores e os personagens (tests/driving_check_integ.h). O código ORIGINAL do DRIVING.ELF
// no interpretador MIPS (com a VU0 em modo macro) ao lado do porte (driving/game/ai_character.h, driving/anim/actor.h,
// driving/anim/act_actor.h, driving/anim/eagl_anim.h), com estados sorteados:
//  - a base AICharacter: o construtor (0x00118720) sobre memória sorteada, Execute (0x00118af0: a queda do corpo solto
//    com o chão, o despacho dos estados, com a base e com as mãos), NotifyZoneDamage (0x00118eb0), MoveToPoint
//    (0x00119140), Util_VecToPoleDepthAndLong (0x00210e08), Util_CharMapLookup (0x00210ff0), AICharacterEnemy
//    TargetIsInRange (0x001191e8), NeedsToReload (0x00119270) e TargetIsAcquired (0x00119290);
//  - o Human: ApplyDamage (0x0017f6c0: o tiro com zona, a fonte, o IncShotsHit original na imagem do gerente, a vida, o
//    Deactivate), SetAICharacter (0x0017f5a0: o ActWeapon::SetOwner 0x00116608 e o SimpleRigidBody::SetOrientMat
//    originais), Activate e Deactivate;
//  - as mãos: o construtor (0x00123e28), AbleToFire, Fire, Reload, ChangeWeapon, ShowHands, HideHands, os estados pela
//    tabela virtual 0x3696e0 (DoInitial, DoNeutral, DoIdling, DoArming, DoArmed, DoAiming, DoFiring, DoReloading,
//    DoUnarming), UpdateRotPos (0x00124848, com o RPlayerCamera::GetAimMatrix4 0x001bcd10 e o SimRandom originais),
//    GetRotPos (0x00123878) e GetAimFraction (0x001237d0);
//  - os atores: ActPoser::Rotate (0x00112dc0), RotateX (0x00112ef0), CalculateFinalCorrectionMatrix (0x00113020),
//    SnapMatrixLevelWithGround (0x00113068), o EAGLAnim::RawEventChannel::Eval (0x00274958: o cursor, o último tempo e
//    os tratadores chamados) e o EventTarget::ResolveEventId (0x00263e18, com o GetEventId 0x00264298: os nomes em
//    ordem e os números).
// O que o porte chama do ator, da câmera e do mundo (ai_character_actor_api(), ai_character_env(),
// raw_event_dispatch()) é trocado aqui pelos mesmos ganchos que o original vê (SetNewAnimation devolve um número de
// controlador, IsAnimationDone e os tempos dos controladores vêm do caso, o chão, a câmera, o AMenuSound anotados), e as
// chamadas dos dois lados são comparadas na ordem. Os ponteiros (os nós dos atores, as zonas, os objetos) são
// comparados pelo que apontam.

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/sim_random.h"
#include "driving/anim/act_actor.h"
#include "driving/anim/actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/game/ai_character.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving_check_integ.h"
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
constexpr uint32_t kFnCharCtor = 0x118720, kFnExecute = 0x118af0, kFnNotifyZone = 0x118eb0, kFnMoveToPoint = 0x119140;
constexpr uint32_t kFnVecToPole = 0x210e08, kFnCharMap = 0x210ff0, kFnInRange = 0x1191e8, kFnNeedsReload = 0x119270;
constexpr uint32_t kFnAcquired = 0x119290;
constexpr uint32_t kFnApplyDamage = 0x17f6c0, kFnSetAIChar = 0x17f5a0, kFnActivate = 0x17f540, kFnDeactivate = 0x17f570;
constexpr uint32_t kFnHandsCtor = 0x123e28, kFnAbleToFire = 0x1241e0, kFnFire = 0x1240c8, kFnReload = 0x124170;
constexpr uint32_t kFnChangeWeapon = 0x123f90, kFnShow = 0x123ef8, kFnHide = 0x123f10;
constexpr uint32_t kFnHandsUpdateRotPos = 0x124848, kFnGetRotPos = 0x123878, kFnAimFraction = 0x1237d0;
constexpr uint32_t kFnRotate = 0x112dc0, kFnRotateX = 0x112ef0, kFnFinalCorrection = 0x113020, kFnSnap = 0x113068;
constexpr uint32_t kFnRawEventEval = 0x274958, kFnResolveEventId = 0x263e18;
// Os ganchos.
constexpr uint32_t kHkAtan2 = 0x2c0618, kHkCos = 0x2bfa48, kHkSin = 0x2bfc38;
constexpr uint32_t kHkSetNewAnim = 0x107c68, kHkIsDone = 0x10ac58, kHkCurFrame = 0x107388, kHkCurTime = 0x1073a8;
constexpr uint32_t kHkTotTime = 0x1073c0, kHkRemTime = 0x1073d8, kHkWorldPos = 0x108e98, kHkGetOrigin = 0x10ad90;
constexpr uint32_t kHkSetOrigin = 0x10adf8, kHkLocalPosOri = 0x108d48, kHkWeaponPos = 0x108ec8;
constexpr uint32_t kHkFindFace = 0x2322c8, kHkHeight = 0x2327a0;
constexpr uint32_t kHkAimZoom = 0x1be570, kHkAimRelease = 0x1be660, kHkMenuSound = 0x2f4b10;
constexpr uint32_t kHkUpdateCamera = 0x1b29e0;  // o endereço posto na tabela falsa da câmera (+0x24)
constexpr uint32_t kHkGetRigidBody = 0x1ff088, kHkGetSimple = 0x1ff0b0, kHkPlayer = 0x201ae0, kHkIsOwnedBy = 0x195c70;
constexpr uint32_t kHkFindSig = 0x1ff280, kHkMalloc = 0x2871f0, kHkFree = 0x287210;
constexpr uint32_t kHkHandler[4] = {0x10e738, 0x10e7c8, 0x10e880, 0x10e930};  // os quatro tratadores de eventos

// Os globais do PS2.
constexpr uint32_t kGHands = 0x327670, kGActors = 0x3276c0, kGZoomButton = 0x3276e0, kGIsZoomed = 0x3276e4;
constexpr uint32_t kGAimYaw = 0x3276e8, kGAimPitch = 0x3276ec, kGLagPos = 0x3276f0, kGLagVel = 0x327700;
constexpr uint32_t kGToggle = 0x422420, kGCamList = 0x333ed8, kGMission = 0x33a024, kGSim = 0x339190;
constexpr uint32_t kGDt = 0x339f50, kGSteps = 0x339f54, kGCars = 0x339f5c;

// A RAM emulada.
constexpr uint32_t kNodes = 0x01800000, kActors = 0x01800100, kActorSize = 0x80;
constexpr uint32_t kHands = 0x01801000, kChar = 0x01801200, kZone = 0x01801400, kZoneMap = 0x01801500;
constexpr uint32_t kHuman = 0x01801800, kHP = 0x018018a0, kSrb = 0x01801900, kBody = 0x01801a00, kInfo = 0x01801b00;
constexpr uint32_t kCam = 0x01802000, kCamVt = 0x01802400, kCamList = 0x01802500, kCarsVec = 0x01802600;
constexpr uint32_t kCarImg = 0x01802700, kMission = 0x01803000, kRand = 0x01805000, kSrc = 0x01805100;
constexpr uint32_t kWeapons = 0x01805200, kActChar = 0x01805600, kWP = 0x01805700, kOut = 0x01805800;
constexpr uint32_t kEvData = 0x01806000, kHandlers = 0x01807000, kHObj = 0x01807100, kHVt = 0x01807200;
constexpr uint32_t kTarget = 0x01808000, kNames = 0x01809000, kHeap = 0x01810000, kHeapEnd = 0x01900000;
constexpr uint32_t kPoser = 0x01900000, kPM = 0x01900100;

constexpr uint32_t kVtBase = drv::kVtAICharacter, kVtHands = drv::kVtAICharacterHands;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 2020) {}
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

// Um passo de tempo para os eventos (crescente).
float ee_add_step(float t, Rng& r) { return t + r.uni(0.0f, 1.5f); }

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

// Uma chamada para fora (os dois lados anotam na mesma forma).
struct Call {
    uint32_t fn = 0, a = 0, b = 0, c = 0;
    bool operator==(const Call& o) const { return fn == o.fn && a == o.a && b == o.b && c == o.c; }
    std::string text() const { return hex(fn) + "(" + hex(a) + "," + hex(b) + "," + hex(c) + ")"; }
};

// O caso: o que os ganchos devolvem.
struct Script {
    bool done[8] = {};
    float frame = 0, cur_time = 0, tot_time = 1, rem_time = 0, floor_h = 0;
    float world_pos[4] = {}, origin[4] = {}, local[16] = {}, weapon[16] = {};
    bool owned = false;
    int source = 0;
};
Script g_s;
std::vector<Call> g_port_calls;
uint32_t g_port_token = 0;
an::ActActorData* g_port_actors = nullptr;  // os oito atores falsos do porte
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
// Um handle (o nó) pelo índice; 0x80 nulo, 0xff fora.
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

uint32_t fbits(float f) { return ps2::fbits(f); }
uint32_t name_hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// Os ganchos do porte (ai_character_actor_api(), ai_character_env()).
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
    a.current_frame = [](uint32_t ctl) {
        g_port_calls.push_back({kHkCurFrame, ctl});
        return g_s.frame;
    };
    a.current_time = [](uint32_t ctl) {
        g_port_calls.push_back({kHkCurTime, ctl});
        return g_s.cur_time;
    };
    a.total_time = [](uint32_t ctl) {
        g_port_calls.push_back({kHkTotTime, ctl});
        return g_s.tot_time;
    };
    a.remaining_time = [](uint32_t ctl) {
        g_port_calls.push_back({kHkRemTime, ctl});
        return g_s.rem_time;
    };
    a.world_position = [](uint32_t actor, float* out) {
        g_port_calls.push_back({kHkWorldPos, uint32_t(port_actor_index(actor))});
        std::memcpy(out, g_s.world_pos, 16);
    };
    a.get_origin = [](uint32_t actor, float* out) {
        g_port_calls.push_back({kHkGetOrigin, uint32_t(port_actor_index(actor))});
        std::memcpy(out, g_s.origin, 16);
    };
    a.set_origin = [](uint32_t actor, float* o) {
        (void)actor;
        g_port_calls.push_back({kHkSetOrigin, fbits(o[0]), fbits(o[1]), fbits(o[2])});
    };
    a.local_pos_ori = [](uint32_t actor, float* out) {
        g_port_calls.push_back({kHkLocalPosOri, uint32_t(port_actor_index(actor))});
        std::memcpy(out, g_s.local, 64);
    };
    a.weapon_position = [](uint32_t actor, float* out, int32_t w) {
        g_port_calls.push_back({kHkWeaponPos, uint32_t(port_actor_index(actor)), uint32_t(w)});
        std::memcpy(out, g_s.weapon, 64);
    };
    a.floor_height = [](uint32_t, const float pos[3], float& h) {
        g_port_calls.push_back({kHkFindFace, fbits(pos[0]), fbits(pos[1]), fbits(pos[2])});
        g_port_calls.push_back({kHkHeight, fbits(pos[0]), fbits(pos[1]), fbits(pos[2])});
        h = g_s.floor_h;
        return true;
    };
    a.owned_by = [](drv::SimpleObject&, drv::PhysicsObject*) { return g_s.owned; };
    drv::AICharacterEnv& env = drv::ai_character_env();
    env.camera_update = []() { g_port_calls.push_back({kHkUpdateCamera}); };
    env.aim_zoom = []() { g_port_calls.push_back({kHkAimZoom}); };
    env.aim_release = []() { g_port_calls.push_back({kHkAimRelease}); };
    env.menu_sound = [](int32_t bank, const char* name, const char* mix) {
        g_port_calls.push_back({kHkMenuSound, uint32_t(bank), name_hash(name), name_hash(mix)});
    };
    env.zoom_button = nullptr;
    env.lookup_vector = nullptr;
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    std::vector<Call> calls;
    uint32_t token = 0, heap = kHeap;
    // Os tratadores dos eventos (a parte dos eventos).
    struct EvCall {
        uint32_t handler, event, time, user;
    };
    std::vector<EvCall> ev_calls;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
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
        auto timer = [this, ret_f](uint32_t fn, const float* v) {
            return [this, ret_f, fn, v](MipsMachine& mm) {
                calls.push_back({fn, uint32_t(mm.gpr(R::a0))});
                ret_f(mm, *v);
            };
        };
        m.hook(kHkCurFrame, "AnimationController::GetCurrentFrame", timer(kHkCurFrame, &g_s.frame));
        m.hook(kHkCurTime, "AnimationController::GetCurrentTime", timer(kHkCurTime, &g_s.cur_time));
        m.hook(kHkTotTime, "AnimationController::GetTotalTime", timer(kHkTotTime, &g_s.tot_time));
        m.hook(kHkRemTime, "AnimationController::GetRemainingTime", timer(kHkRemTime, &g_s.rem_time));
        m.hook(kHkWorldPos, "ActActor::GetActorWorldPosition", [this](MipsMachine& mm) {
            calls.push_back({kHkWorldPos, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0))))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 16), g_s.world_pos, 16);
        });
        m.hook(kHkGetOrigin, "ActActor::GetAnimationOrigin", [this](MipsMachine& mm) {
            calls.push_back({kHkGetOrigin, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0))))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 16), g_s.origin, 16);
        });
        m.hook(kHkSetOrigin, "ActActor::SetAnimationOrigin", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            calls.push_back({kHkSetOrigin, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8)});
        });
        m.hook(kHkLocalPosOri, "ActActor::GetActorLocalPosOri", [this](MipsMachine& mm) {
            calls.push_back({kHkLocalPosOri, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0))))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 64), g_s.local, 64);
        });
        m.hook(kHkWeaponPos, "ActActor::GetWeaponPosition", [this](MipsMachine& mm) {
            calls.push_back({kHkWeaponPos, uint32_t(emu_actor_index(uint32_t(mm.gpr(R::a0)))), uint32_t(mm.gpr(R::a3))});
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 64), g_s.weapon, 64);
        });
        m.hook(kHkFindFace, "WWorldPos::FindClosestFace", [this](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            calls.push_back({kHkFindFace, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8)});
        });
        m.hook(kHkHeight, "WWorldPos::HeightAtPoint", [this, ret_f](MipsMachine& mm) {
            const uint32_t p = uint32_t(mm.gpr(R::a1));
            calls.push_back({kHkHeight, mm.read32(p), mm.read32(p + 4), mm.read32(p + 8)});
            ret_f(mm, g_s.floor_h);
        });
        m.hook(kHkAimZoom, "RPlayerCamState::AimZoom", [this](MipsMachine&) { calls.push_back({kHkAimZoom}); });
        m.hook(kHkAimRelease, "RPlayerCamState::AimRelease", [this](MipsMachine&) { calls.push_back({kHkAimRelease}); });
        m.hook(kHkMenuSound, "AMenuSound::Trigger", [this](MipsMachine& mm) {
            auto str = [&mm](uint32_t a) {
                std::string s;
                for (; a && s.size() < 64; ++a) {
                    const char c = char(mm.mem(a, 1)[0]);
                    if (!c) break;
                    s += c;
                }
                return s;
            };
            calls.push_back({kHkMenuSound, uint32_t(mm.gpr(R::a0)), name_hash(str(uint32_t(mm.gpr(R::a1))).c_str()),
                             name_hash(str(uint32_t(mm.gpr(R::a2))).c_str())});
        });
        m.hook(kHkUpdateCamera, "RPlayerCamera::UpdateCamera", [this](MipsMachine&) { calls.push_back({kHkUpdateCamera}); });
        m.hook(kHkGetRigidBody, "Simulation::GetRigidBody", [](MipsMachine& mm) { mm.set_gpr(R::v0, kBody); });
        m.hook(kHkGetSimple, "Simulation::GetSimpleRigidBody", [](MipsMachine& mm) { mm.set_gpr(R::v0, kSrb); });
        m.hook(kHkPlayer, "Simulation::GetPlayerObject", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCarImg); });
        m.hook(kHkIsOwnedBy, "PhysicsObject::IsOwnedBy", [](MipsMachine& mm) { mm.set_gpr(R::v0, g_s.owned ? 1 : 0); });
        m.hook(kHkFindSig, "Simulation::FindPhysicsObjectSignature",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, g_s.source == 0 ? 0u : kSrc); });
        m.hook(kHkMalloc, "EAGLInternal::EAGLMalloc", [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15u) & ~15u;
            if (heap + n > kHeapEnd) heap = kHeap;
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        });
        m.hook(kHkFree, "EAGLInternal::EAGLFree", [](MipsMachine&) {});
        for (int i = 0; i < 4; ++i) {
            m.hook(kHkHandler[i], "HandleEvent", [this, i](MipsMachine& mm) {
                ev_calls.push_back({uint32_t(i), uint32_t(mm.gpr(R::a1)), mm.fpr_bits(12), uint32_t(mm.gpr(R::a2))});
            });
        }
    }
    int errors = 0;
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
        } catch (const std::exception& x) {
            if (errors++ < 5) std::printf("  ERRO no original %08x: %s\n", fn, x.what());
        }
    }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
    void put_f(uint32_t a, float f) { put(a, &f, 4); }
    float get_f(uint32_t a) {
        float f;
        std::memcpy(&f, m.mem(a, 4), 4);
        return f;
    }
};

// Os oito atores falsos dos dois lados (o nó com o ator em +8, o visível em +0x30).
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

// Compara um AICharacter (0x150) ou as mãos (0x170): os ponteiros pelo que apontam.
std::string compare_char(Emu& e, uint32_t emu, const void* port, uint32_t size, uint32_t port_zone) {
    const uint8_t* p = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < size; o += 4) {
        uint32_t x = e.m.read32(emu + o), y;
        std::memcpy(&y, p + o, 4);
        if (o == 0x98) {
            x = uint32_t(emu_node_index(x));
            y = uint32_t(port_node_index(y));
        } else if (o == 0xa4) {
            x = x == 0 ? 0u : (x == kZone ? 1u : 2u);
            y = y == 0 ? 0u : (y == port_zone ? 1u : 3u);
        } else if (o == 0x74 || o == 0x68) {
            x = x != 0;
            y = y != 0;
        }
        if (x != y) return "personagem +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " + hex(y);
    }
    return "";
}

// Um estado sorteado do personagem (a base ou as mãos), dos dois lados.
void random_char(Rng& r, Emu& e, uint32_t emu, drv::AICharacterHandsData& port, bool hands, uint32_t port_zone) {
    std::vector<uint8_t> raw(0x170);
    for (auto& b : raw) b = uint8_t(r.next());
    drv::AICharacterHandsData& h = port;
    std::memcpy(&h, raw.data(), 0x170);
    drv::AICharacterData& c = h.base;
    for (int i = 0; i < 16; ++i) c.matrix[i] = r.uni(-2, 2);
    for (int i = 0; i < 3; ++i) c.offset[i] = r.uni(-1, 1);
    c.state = r.chance(0.85) ? std::vector<int>{0, 1, 2, 8, 9, 10, 11, 12, 13}[size_t(r.range(0, 8))] : r.range(0, 0x18);
    c.sub_state = r.chance(0.7) ? (r.chance(0.5) ? 8 : 0xd) : r.range(0, 0x10);
    c.anim = r.range(-1, 12);
    c.index = r.range(1, 7);
    c.range_sq = r.uni(10, 8000);
    c.hit_points = 0;
    c.world_pos = 0;
    c.bank = r.range(0, 17);
    c.clip = r.range(-2, 16);
    c.clip_size = r.chance(0.2) ? 0 : r.range(1, 16);
    c.sound = 0;
    c.actor = r.chance(0.95) ? port_node(r.range(1, 7)) : 0;
    if (c.actor == 0) c.actor = port_node(0);
    c.controller = uint32_t(r.range(1, 1000));
    c.zone_map = 0;
    for (int i = 0; i < 16; ++i) c.body[i] = r.uni(-3, 3);
    for (int i = 0; i < 3; ++i) c.velocity[i] = r.uni(-20, 20);
    c.falling = r.chance(0.5) ? 1 : 0;
    c.floor_check = r.chance(0.5) ? 1 : 0;
    c.vtable = hands ? kVtHands : kVtBase;
    h.reload_pending = r.chance(0.3) ? 1 : 0;
    h.hide_on_change = r.chance(0.7) ? 1 : 0;
    h.pending_weapon = r.chance(0.5) ? 0 : r.range(1, 7);
    h.shake = r.chance(0.5) ? r.uni(-0.03f, 0.0f) : 0.0f;
    h.aim_fraction = r.uni(0, 1);
    h.visible = r.chance(0.5) ? 1 : 0;
    // O original: os mesmos bytes, os ponteiros traduzidos.
    std::memcpy(e.m.mem(emu, 0x170), &h, 0x170);
    e.m.write32(emu + 0x98, emu_node(port_node_index(c.actor)));
    (void)port_zone;
}

// Os globais das mãos dos dois lados.
void random_hands_globals(Rng& r, Emu& e) {
    drv::HandsGlobals& g = drv::hands_globals();
    for (int i = 0; i < 8; ++i) {
        const int n = i == 0 ? 0 : r.range(0, 7);
        g.actor[i] = port_node(n);
        e.m.write32(kGActors + 4u * uint32_t(i), emu_node(n));
        g.toggle[i] = r.range(0, 1);
        e.m.write32(kGToggle + 4u * uint32_t(i), uint32_t(g.toggle[i]));
    }
    g.is_zoomed = r.range(0, 1);
    drv::hands_zoom_button() = r.range(0, 1);
    e.m.write32(kGIsZoomed, uint32_t(g.is_zoomed));
    e.m.write32(kGZoomButton, uint32_t(drv::hands_zoom_button()));
}

std::string compare_hands_globals(Emu& e) {
    drv::HandsGlobals& g = drv::hands_globals();
    for (int i = 0; i < 8; ++i) {
        const int x = emu_node_index(e.m.read32(kGActors + 4u * uint32_t(i))), y = port_node_index(g.actor[i]);
        if (x != y) return "fgActor[" + std::to_string(i) + "]";
        if (e.m.read32(kGToggle + 4u * uint32_t(i)) != uint32_t(g.toggle[i])) return "o lado do tiro " + std::to_string(i);
    }
    if (e.m.read32(kGIsZoomed) != uint32_t(g.is_zoomed)) return "fgIsZoomed";
    if (e.m.read32(kGZoomButton) != uint32_t(drv::hands_zoom_button())) return "fgZoomButton";
    for (int i = 0; i < 4; ++i) {
        if (e.m.read32(kGLagPos + 4u * uint32_t(i)) != fbits(g.lag_pos[i])) return "o atraso da posição";
        if (e.m.read32(kGLagVel + 4u * uint32_t(i)) != fbits(g.lag_vel[i])) return "o atraso da velocidade";
    }
    return "";
}

// O mundo das mãos: a Simulation com o carro do jogador (o corpo e a orientação), o SimRandom, o gerente da missão
// (a direção automática, os tiros), a câmera e a mira.
struct World {
    drv::BondCar* car = nullptr;
    std::vector<uint8_t> cam;
};

void random_quat(Rng& r, float q[4]) {
    double v[4], l = 0;
    for (double& x : v) {
        x = r.uni(-1.0f, 1.0f);
        l += x * x;
    }
    l = std::sqrt(l > 1e-9 ? l : 1.0);
    for (int k = 0; k < 4; ++k) q[k] = float(v[k] / l);
}

void random_world(Rng& r, Emu& e, World& w) {
    drv::simulation_reset(r.chance(0.8) ? 60 : 50);
    drv::Simulation& sim = drv::simulation();
    sim.cars.clear();
    sim.steps = r.range(0, 20000);
    delete w.car;
    w.car = new drv::BondCar();
    drv::RigidBodyParams p;
    p.mass = r.uni(100, 2500);
    p.type = 1;
    for (float& v : p.pos) v = r.uni(-500, 500);
    for (float& v : p.vel) v = r.uni(-30, 30);
    float q[4];
    random_quat(r, q);
    drv::VU0_quattom4(q, p.orient);
    drv::RigidBody* rb = drv::simulation_add_body(sim, p, w.car);
    w.car->d.slot = int16_t(rb->slot);
    sim.cars.push_back(w.car);
    // O SimRandom.
    drv::SimRandom_Construct(drv::sim_random());
    drv::SimRandom_Reset(drv::sim_random());
    for (int k = r.range(0, 40); k > 0; --k) drv::SimRandom_Generate(drv::sim_random());
    e.put(kRand, &drv::sim_random(), sizeof(drv::SimRandom));
    e.m.write32(kGSim, kRand);
    // A Simulation do original: o passo, o tempo e o vetor dos carros.
    e.put_f(kGDt, sim.dt);
    e.m.write32(kGSteps, uint32_t(sim.steps));
    e.m.write32(kGCars, kCarsVec);
    e.m.write32(kCarsVec, kCarImg);
    std::memset(e.m.mem(kCarImg, 0x100), 0, 0x100);
    e.m.write32(kCarImg + 0x44, uint32_t(rb->slot) << 16);
    // O corpo e o RigidBodyInfo (o GetRigidBody devolve a imagem).
    e.put(kBody, rb, 0x80);
    e.m.write32(kBody + 0x5c, kInfo);
    e.put(kInfo, nf::raw(rb->info), 0x40);
    // O gerente da missão.
    if (!drv::mission_exists()) drv::SMissionManager_Construct(false);
    drv::SMissionManagerData& mm = drv::mission_manager();
    mm.auto_drive = r.chance(0.6) ? 1 : 0;
    mm.shots_fired = r.range(0, 30);
    mm.shots_hit = r.range(0, 30);
    mm.last_shot_step = sim.steps - r.range(0, 4000);
    e.m.write32(kGMission, kMission);
    std::memset(e.m.mem(kMission, 0x1b50), 0, 0x1b50);
    e.put(kMission, &mm, 0x600);
    // A câmera: a matriz (a rotação sorteada, o olho), a cópia (+0x180), as zonas mortas (+0x1d0, +0x1d4).
    w.cam.assign(0x400, 0);
    float cq[4], cm[4][4];
    random_quat(r, cq);
    drv::VU0_quattom4(cq, cm);
    for (int k = 0; k < 3; ++k) cm[3][k] = r.uni(-500, 500);
    std::memcpy(w.cam.data(), cm, 64);
    float copy[16];
    for (float& v : copy) v = r.uni(-1, 1);
    std::memcpy(w.cam.data() + 0x180, copy, 64);
    const float dz[2] = {r.uni(-0.3f, 0.3f), r.uni(-0.3f, 0.3f)};
    std::memcpy(w.cam.data() + 0x1d0, dz, 8);
    e.put(kCam, w.cam.data(), 0x400);
    e.m.write32(kCam + 0xa8, kCamVt);
    std::memset(e.m.mem(kCamVt, 0x40), 0, 0x40);
    e.m.write32(kCamVt + 0x24, kHkUpdateCamera);
    e.m.write32(kGCamList, kCamList);
    e.m.write32(kCamList + 4, kCam);
    const uint8_t* camp = w.cam.data();
    drv::ai_character_env().camera = [camp]() { return camp; };
    // A mira e o atraso do braço.
    drv::HandsGlobals& g = drv::hands_globals();
    const int aim = r.range(0, 9);
    g.aim_yaw = aim == 0 ? 1.0f : (aim == 1 ? 0.0f : (aim < 5 ? 0.4f : r.uni(-1, 1)));
    g.aim_pitch = aim < 2 ? 1.0f : (aim < 5 ? 0.08f : r.uni(-1, 1));
    e.put_f(kGAimYaw, g.aim_yaw);
    e.put_f(kGAimPitch, g.aim_pitch);
    for (int i = 0; i < 4; ++i) {
        g.lag_pos[i] = r.uni(-0.5f, 0.5f);
        g.lag_vel[i] = r.uni(-0.5f, 0.5f);
        e.put_f(kGLagPos + 4u * uint32_t(i), g.lag_pos[i]);
        e.put_f(kGLagVel + 4u * uint32_t(i), g.lag_vel[i]);
    }
}

void random_script(Rng& r) {
    g_s.frame = r.chance(0.3) ? r.uni(4.5f, 5.5f) : (r.chance(0.3) ? r.uni(13.5f, 14.5f) : r.uni(0, 30));
    g_s.cur_time = r.uni(0, 3);
    g_s.tot_time = r.uni(0.1f, 3);
    g_s.rem_time = r.uni(0, 3);
    g_s.floor_h = r.uni(-5, 5);
    for (float& v : g_s.world_pos) v = r.uni(-100, 100);
    for (float& v : g_s.origin) v = r.uni(-100, 100);
    for (float& v : g_s.local) v = r.uni(-2, 2);
    for (float& v : g_s.weapon) v = r.uni(-2, 2);
    g_s.owned = r.chance(0.5);
    g_s.source = r.range(0, 2);
}

void begin_case(Emu& e) {
    e.calls.clear();
    e.token = 0x7000;
    g_port_calls.clear();
    g_port_token = 0x7000;
}

}  // namespace

int run_integ_actor_checks(const nf::Elf& elf, const nf::DiscSource&, const uint8_t* vu0_program, uint32_t vu0_size,
                           int cases, uint32_t seed) {
    std::printf("-- os atores e os personagens (driving/anim/act_actor.h, driving/game/ai_character.h)\n");
    int failures = 0;
    Emu e;
    e.setup(elf, vu0_program, vu0_size);
    Rng rng(seed ^ 0x20202020u);
    const drv::AICharacterActorApi saved_api = drv::ai_character_actor_api();
    const drv::AICharacterEnv saved_env = drv::ai_character_env();
    const drv::HandsGlobals saved_hands = drv::hands_globals();
    install_port_api();
    std::vector<an::ActActorData> port_actors(8);
    std::vector<an::ActListNode> port_nodes(8);
    g_port_actors = port_actors.data();
    g_port_nodes = port_nodes.data();
    World world;

    Tally t_ctor, t_hctor, t_pole, t_map, t_zone, t_exec, t_move, t_range, t_reload, t_acq;
    Tally t_damage, t_setchar, t_act, t_able, t_fire, t_reloadh, t_change, t_show, t_hide, t_state, t_rot, t_getrot;
    Tally t_aimfrac, t_rotate, t_rotatex, t_final, t_snap, t_events, t_resolve;
    uint32_t cmp_zone = 0;  // a zona do porte deste caso (p32)
    std::vector<uint8_t> port_zone_buf(0x200);

    auto zone_setup = [&](Rng& r, drv::AICharacterData& pc, bool on) {
        if (!on) {
            pc.zone_map = 0;
            e.m.write32(kChar + 0xa4, 0);
            cmp_zone = 0;
            return;
        }
        const int32_t w = r.range(1, 12), h = r.range(1, 12);
        uint8_t* z = port_zone_buf.data();
        std::memset(z, 0, 0x200);
        std::memcpy(z + 0, &w, 4);
        std::memcpy(z + 4, &h, 4);
        const float rad = r.uni(0.2f, 2.0f), y = r.uni(-1, 1), ys = r.uni(0.3f, 2.0f);
        std::memcpy(z + 0xc, &rad, 4);
        std::memcpy(z + 0x10, &y, 4);
        std::memcpy(z + 0x14, &ys, 4);
        // O mapa com folga dos dois lados (um índice negativo lê antes dele, igual nos dois).
        for (int i = 0; i < 0x180; ++i) z[0x40 + i] = uint8_t(r.range(0, 4) * (r.chance(0.2) ? -1 : 1));
        const uint32_t map_p = nf::p32_u(z + 0x80);
        std::memcpy(z + 8, &map_p, 4);
        pc.zone_map = nf::p32_u(z);
        cmp_zone = pc.zone_map;
        e.put(kZone, z, 0x200);
        e.m.write32(kZone + 8, kZone + 0x80);
        e.m.write32(kChar + 0xa4, kZone);
    };

    for (int i = 0; i < cases; ++i) {
        random_script(rng);
        // ---- o construtor da base, sobre memória sorteada ----
        {
            ++t_ctor.cases;
            std::vector<uint8_t> raw(0x150);
            for (auto& b : raw) b = uint8_t(rng.next());
            drv::AICharacterData pc;
            std::memcpy(&pc, raw.data(), 0x150);
            e.put(kChar, raw.data(), 0x150);
            e.call(kFnCharCtor, {kChar});
            drv::AICharacter_Construct(pc);
            const std::string why = compare_char(e, kChar, &pc, 0x150, 0);
            if (!why.empty()) t_ctor.fail(why);
        }
        // ---- o construtor das mãos ----
        {
            ++t_hctor.cases;
            std::vector<uint8_t> raw(0x170);
            for (auto& b : raw) b = uint8_t(rng.next());
            drv::AICharacterHandsData ph;
            std::memcpy(&ph, raw.data(), 0x170);
            e.put(kHands, raw.data(), 0x170);
            const int32_t w = rng.range(0, 7);
            e.call(kFnHandsCtor, {kHands, uint32_t(w)});
            drv::AICharacterHands_Construct(ph, w);
            const std::string why = compare_char(e, kHands, &ph, 0x170, 0);
            if (!why.empty()) t_hctor.fail(why);
        }
        // ---- Util_VecToPoleDepthAndLong e Util_CharMapLookup ----
        {
            ++t_pole.cases;
            float v[4] = {rng.uni(-2, 2), rng.uni(-2, 2), rng.uni(-2, 2), 0};
            if (rng.chance(0.1)) v[0] = 0.0f;
            e.put(kOut, v, 16);
            e.call(kFnVecToPole, {kOut, kOut + 0x10, kOut + 0x14});
            float d = 0, l = 0;
            drv::Util_VecToPoleDepthAndLong(v, d, l);
            if (e.m.read32(kOut + 0x10) != fbits(d) || e.m.read32(kOut + 0x14) != fbits(l))
                t_pole.fail("v " + hex(fbits(v[0])) + "," + hex(fbits(v[1])) + "," + hex(fbits(v[2])) + ": original " +
                            hex(e.m.read32(kOut + 0x10)) + "/" + hex(e.m.read32(kOut + 0x14)) + ", porte " + hex(fbits(d)) +
                            "/" + hex(fbits(l)));
            ++t_map.cases;
            drv::AICharacterHandsData tmp;
            zone_setup(rng, tmp.base, true);
            const int32_t w = rng.range(1, 12), h = rng.range(1, 12);
            const float u = rng.chance(0.1) ? rng.uni(-0.1f, 0.0f) : rng.uni(0, 1);
            const float vv = rng.chance(0.1) ? rng.uni(-0.05f, 0.0f) : rng.uni(0, 1);
            e.m.set_fpr(12, u);
            e.m.set_fpr(13, vv);
            e.call(kFnCharMap, {kZone + 0x80, uint32_t(w), uint32_t(h)});
            const int32_t pr = drv::Util_CharMapLookup(u, vv, nf::p32_u(port_zone_buf.data() + 0x80), w, h);
            if (uint32_t(e.m.gpr(R::v0)) != uint32_t(pr))
                t_map.fail("original " + hex(uint32_t(e.m.gpr(R::v0))) + ", porte " + hex(uint32_t(pr)));
        }
        // ---- NotifyZoneDamage ----
        {
            ++t_zone.cases;
            setup_actors(e, rng);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kChar, ph, false, 0);
            // Uma matriz ortonormal com o lugar.
            float q[4], m[4][4];
            random_quat(rng, q);
            drv::VU0_quattom4(q, m);
            for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-50, 50);
            std::memcpy(ph.base.matrix, m, 64);
            e.put(kChar, m, 64);
            zone_setup(rng, ph.base, rng.chance(0.9));
            float pos[4] = {m[3][0] + rng.uni(-2, 2), m[3][1] + rng.uni(-2, 2), m[3][2] + rng.uni(-2, 2), 1};
            e.put(kOut, pos, 16);
            begin_case(e);
            e.call(kFnNotifyZone, {kChar, kOut});
            drv::AICharacter_NotifyZoneDamage(nf::p32_u(&ph), pos);
            const std::string why = compare_char(e, kChar, &ph, 0x150, cmp_zone);
            if (!why.empty()) t_zone.fail(why);
        }
        // ---- o mundo das mãos (a Simulation, a câmera, a missão) para o resto ----
        random_world(rng, e, world);
        // ---- Execute (a base e as mãos) ----
        {
            ++t_exec.cases;
            setup_actors(e, rng);
            random_hands_globals(rng, e);
            const bool hands = rng.chance(0.5);
            drv::AICharacterHandsData ph;
            random_char(rng, e, hands ? kHands : kChar, ph, hands, 0);
            // Qualquer endereço não nulo (nulo, o PS2 chamaria o FindClosestFace com ele e o porte conta uma guarda).
            ph.base.world_pos = nf::p32_u(&ph);
            e.m.write32((hands ? kHands : kChar) + 0x74, kWP);
            begin_case(e);
            const uint32_t emu = hands ? kHands : kChar;
            e.call(kFnExecute, {emu});
            drv::AICharacter_Execute(nf::p32_u(&ph));
            std::string why = compare_char(e, emu, &ph, hands ? 0x170 : 0x150, 0);
            if (why.empty()) why = compare_calls(e);
            if (why.empty()) why = compare_actors(e);
            if (why.empty()) why = compare_hands_globals(e);
            if (why.empty() && e.m.read32(kRand + 4) != drv::sim_random().last) why = "o SimRandom";
            if (!why.empty()) t_exec.fail(std::string(hands ? "mãos: " : "base: ") + why);
        }
        // ---- MoveToPoint ----
        {
            ++t_move.cases;
            setup_actors(e, rng);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kChar, ph, false, 0);
            const float pt[4] = {rng.uni(-100, 100), rng.uni(-100, 100), rng.uni(-100, 100), 1};
            const bool with_y = rng.chance(0.5);
            e.put(kOut, pt, 16);
            begin_case(e);
            e.call(kFnMoveToPoint, {kChar, kOut, with_y ? 1u : 0u});
            drv::AICharacter_MoveToPoint(nf::p32_u(&ph), pt, with_y);
            std::string why = compare_calls(e);
            if (!why.empty()) t_move.fail(why);
        }
        // ---- AICharacterEnemy: TargetIsInRange, NeedsToReload, TargetIsAcquired ----
        {
            setup_actors(e, rng);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kChar, ph, false, 0);
            ++t_range.cases;
            begin_case(e);
            e.call(kFnInRange, {kChar});
            const uint32_t a = uint32_t(e.m.gpr(R::v0));
            const bool b = drv::AICharacterEnemy_TargetIsInRange(nf::p32_u(&ph));
            if (a != (b ? 1u : 0u)) t_range.fail("original " + std::to_string(a) + ", porte " + std::to_string(b));
            ++t_reload.cases;
            e.call(kFnNeedsReload, {kChar});
            if (uint32_t(e.m.gpr(R::v0)) != (drv::AICharacterEnemy_NeedsToReload(nf::p32_u(&ph)) ? 1u : 0u))
                t_reload.fail("NeedsToReload");
            ++t_acq.cases;
            if (rng.chance(0.3)) {
                ph.base.index = 3;
                e.m.write32(kChar + 0x58, 3);
            }
            begin_case(e);
            e.call(kFnAcquired, {kChar});
            const uint32_t ea = uint32_t(e.m.gpr(R::v0));
            const bool pa = drv::AICharacterEnemy_TargetIsAcquired(nf::p32_u(&ph));
            std::string why = ea != (pa ? 1u : 0u) ? "o resultado" : compare_char(e, kChar, &ph, 0x150, 0);
            if (why.empty()) why = compare_calls(e);
            if (!why.empty()) t_acq.fail(why);
        }
        // ---- Human::ApplyDamage ----
        {
            ++t_damage.cases;
            setup_actors(e, rng);
            drv::AICharacterHandsData ph;
            const bool hands = rng.chance(0.4);
            random_char(rng, e, kChar, ph, hands, 0);
            ph.base.counts_hits = rng.chance(0.7) ? 1 : 0;
            e.m.write32(kChar + 0xb0, uint32_t(ph.base.counts_hits));
            // A matriz ortonormal (a zona inverte).
            float q[4], m[4][4];
            random_quat(rng, q);
            drv::VU0_quattom4(q, m);
            for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-50, 50);
            std::memcpy(ph.base.matrix, m, 64);
            e.put(kChar, m, 64);
            zone_setup(rng, ph.base, rng.chance(0.8));
            // O ator do personagem visível quase sempre.
            if (rng.chance(0.9)) {
                const int ai = port_node_index(ph.base.actor);
                g_port_actors[ai].visible = 1;
                e.m.write32(kActors + uint32_t(ai) * kActorSize + 0x30, 1);
            }
            // O Human dos dois lados (o corpo simples na vaga do porte; a imagem do PS2 com o GetSimpleRigidBody).
            drv::Simulation& sim = drv::simulation();
            auto hm = std::make_unique<drv::Human>();
            hm->po.type = 4;
            hm->po.slot = int16_t(drv::AssignSimpleBodySlot(sim, hm.get()));
            drv::SimpleRigidBody* sb = drv::GetSimpleRigidBody(sim, hm->po.slot);
            sb->flags = uint16_t(rng.next());
            e.put(kSrb, sb, 0x40);
            float hp = rng.chance(0.2) ? rng.uni(0, 3) : rng.uni(0, 200);
            auto hp_store = std::make_unique<float>(hp);
            hm->po.hit_point_loc = rng.chance(0.9) ? nf::p32_u(hp_store.get()) : 0;
            hm->h.kind = rng.chance(0.1) ? 1 : 0;
            hm->h.character = nf::p32_u(&ph);
            hm->h.dead = rng.chance(0.1) ? 1 : 0;
            for (float& v : hm->h.hit_pos) v = rng.uni(-10, 10);
            std::memset(e.m.mem(kHuman, 0x90), 0, 0x90);
            e.m.write32(kHuman + 0x40, 4);
            e.m.write32(kHuman + 0x44, uint32_t(hm->po.slot) << 16);
            e.m.write32(kHuman + 0x54, hm->po.hit_point_loc ? kHP : 0);
            e.put_f(kHP, hp);
            e.m.write32(kHuman + 0x68, 0x377f20);
            e.m.write32(kHuman + 0x6c, uint32_t(hm->h.kind));
            e.m.write32(kHuman + 0x70, kChar);
            e.m.write32(kHuman + 0x74, uint32_t(hm->h.dead));
            e.put(kHuman + 0x80, hm->h.hit_pos, 16);
            // O gerente: os tiros (o IncShotsHit original na imagem).
            const float dmg = rng.chance(0.3) ? rng.uni(0, 25) : rng.uni(0, 80);
            const int32_t kind = rng.range(1, 4);
            float from[4] = {rng.uni(-50, 50), rng.uni(-50, 50), rng.uni(-50, 50), 1};
            float to[4] = {m[3][0] + rng.uni(-2, 2), m[3][1] + rng.uni(-2, 2), m[3][2] + rng.uni(-2, 2), 1};
            e.put(kOut, from, 16);
            e.put(kOut + 0x10, to, 16);
            e.m.write32(kOut + 0x20, 0x01020304);  // a assinatura (o gancho a ignora)
            g_s.owned = g_s.source == 1;  // a fonte do porte: 1 um objeto do jogador, 2 de outro
            begin_case(e);
            e.m.set_fpr(12, dmg);
            e.call(kFnApplyDamage, {kHuman, kOut, kOut + 0x10, uint32_t(kind), kOut + 0x20});
            const uint32_t er = uint32_t(e.m.gpr(R::v0));
            const int32_t pr = drv::Human_ApplyDamage(*hm, dmg, from, to, kind, g_s.source);
            std::string why;
            if (er != uint32_t(pr)) why = "o resultado: original " + hex(er) + ", porte " + hex(uint32_t(pr));
            if (why.empty() && e.m.read32(kHuman + 0x74) != uint32_t(hm->h.dead)) why = "morto";
            for (int k = 0; k < 4 && why.empty(); ++k)
                if (e.m.read32(kHuman + 0x80 + 4u * uint32_t(k)) != fbits(hm->h.hit_pos[k]))
                    why = "o ponto do tiro " + std::to_string(k);
            if (why.empty() && hm->po.hit_point_loc && e.m.read32(kHP) != fbits(*hp_store)) why = "a vida";
            if (why.empty() && e.m.read32(kMission + 0x3c0) != uint32_t(drv::mission_manager().shots_hit))
                why = "o IncShotsHit";
            if (why.empty()) {
                uint16_t ef;
                std::memcpy(&ef, e.m.mem(kSrb + 0x1e, 2), 2);
                if (ef != sb->flags) why = "as bandeiras do corpo simples";
            }
            if (why.empty()) why = compare_char(e, kChar, &ph, hands ? 0x170 : 0x150, cmp_zone);
            if (!why.empty()) t_damage.fail(why);
            drv::ReleaseSimpleBodySlot(sim, hm->po.slot);
            hm->po.slot = -1;
            hm->h.character = 0;
        }
        // ---- Human::SetAICharacter, Activate, Deactivate ----
        {
            ++t_setchar.cases;
            setup_actors(e, rng);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kChar, ph, false, 0);
            float q[4], m[4][4];
            random_quat(rng, q);
            drv::VU0_quattom4(q, m);
            for (int k = 0; k < 3; ++k) m[3][k] = rng.uni(-50, 50);
            std::memcpy(ph.base.matrix, m, 64);
            e.put(kChar, m, 64);
            auto hp_store = std::make_unique<float>(rng.uni(0, 100));
            ph.base.hit_points = rng.chance(0.7) ? nf::p32_u(hp_store.get()) : 0;
            e.m.write32(kChar + 0x68, ph.base.hit_points ? kHP : 0);
            // O ActCharacter do ator com as duas armas (às vezes sem).
            const int ai = port_node_index(ph.base.actor);
            auto ach = std::make_unique<an::ActCharacterData>();
            auto w0 = std::make_unique<an::ActWeaponData>(), w1 = std::make_unique<an::ActWeaponData>();
            const bool has0 = rng.chance(0.8), has1 = rng.chance(0.4);
            w0->flags = rng.next();
            w1->flags = rng.next();
            ach->weapon[0] = has0 ? nf::p32_u(w0.get()) : 0;
            ach->weapon[1] = has1 ? nf::p32_u(w1.get()) : 0;
            g_port_actors[ai].character = nf::p32_u(ach.get());
            std::memset(e.m.mem(kActChar, 0x84), 0, 0x84);
            std::memset(e.m.mem(kWeapons, 0x400), 0, 0x400);
            e.m.write32(kActors + uint32_t(ai) * kActorSize, kActChar);
            e.m.write32(kActChar + 0x24, has0 ? kWeapons : 0);
            e.m.write32(kActChar + 0x28, has1 ? kWeapons + 0x200 : 0);
            e.m.write32(kWeapons + 0x138, w0->flags);
            e.m.write32(kWeapons + 0x200 + 0x138, w1->flags);
            drv::Simulation& sim = drv::simulation();
            auto hm = std::make_unique<drv::Human>();
            hm->po.type = 4;
            hm->po.slot = int16_t(drv::AssignSimpleBodySlot(sim, hm.get()));
            drv::SimpleRigidBody* sb = drv::GetSimpleRigidBody(sim, hm->po.slot);
            for (float& v : sb->quat) v = rng.uni(-1, 1);
            for (float& v : sb->pos) v = rng.uni(-10, 10);
            for (float& v : sb->vel) v = rng.uni(-10, 10);
            for (float& v : sb->accel) v = rng.uni(-10, 10);
            sb->flags = uint16_t(rng.next());
            e.put(kSrb, sb, 0x40);
            std::memset(e.m.mem(kHuman, 0x90), 0, 0x90);
            e.m.write32(kHuman + 0x44, uint32_t(hm->po.slot) << 16);
            begin_case(e);
            e.call(kFnSetAIChar, {kHuman, kChar});
            drv::Human_SetAICharacter(*hm, nf::p32_u(&ph));
            std::string why;
            if (std::memcmp(e.m.mem(kSrb, 0x40), sb, 0x40) != 0) {
                for (uint32_t o = 0; o < 0x40; o += 4) {
                    uint32_t x = e.m.read32(kSrb + o), y;
                    std::memcpy(&y, reinterpret_cast<const uint8_t*>(sb) + o, 4);
                    if (x != y) {
                        why = "o corpo simples +0x" + hex(o).substr(6) + ": original " + hex(x) + ", porte " + hex(y);
                        break;
                    }
                }
            }
            if (why.empty() && (e.m.read32(kHuman + 0x54) != 0) != (hm->po.hit_point_loc != 0)) why = "a vida do Human";
            for (int k = 0; k < 2 && why.empty(); ++k) {
                const uint32_t ew = kWeapons + 0x200u * uint32_t(k);
                const an::ActWeaponData& pw = k == 0 ? *w0 : *w1;
                if (e.m.read32(ew + 0x138) != pw.flags) why = "as bandeiras da arma " + std::to_string(k);
                if (why.empty() && (e.m.read32(ew + 0x144) == kHuman) != (pw.owner == drv::Human_Image(*hm)))
                    why = "o dono da arma " + std::to_string(k);
            }
            if (!why.empty()) t_setchar.fail(why);
            ++t_act.cases;
            {
                const bool on = rng.chance(0.5);
                sb->flags = uint16_t(rng.next());
                e.put(kSrb, sb, 0x40);
                e.call(on ? kFnActivate : kFnDeactivate, {kHuman});
                if (on) drv::Human_Activate(*hm);
                else drv::Human_Deactivate(*hm);
                uint16_t ef;
                std::memcpy(&ef, e.m.mem(kSrb + 0x1e, 2), 2);
                if (ef != sb->flags) t_act.fail(on ? "Activate" : "Deactivate");
            }
            drv::ReleaseSimpleBodySlot(sim, hm->po.slot);
            hm->po.slot = -1;
            hm->h.character = 0;
            g_port_actors[ai].character = 0;
        }
        // ---- as mãos: as funções e os estados ----
        {
            setup_actors(e, rng);
            random_hands_globals(rng, e);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kHands, ph, true, 0);
            const uint32_t self = nf::p32_u(&ph);
            const int which = rng.range(0, 6);
            Tally* t = nullptr;
            begin_case(e);
            uint32_t er = 0, pr = 0;
            switch (which) {
                case 0:
                    t = &t_able;
                    e.call(kFnAbleToFire, {kHands});
                    er = uint32_t(e.m.gpr(R::v0));
                    pr = drv::AICharacterHands_AbleToFire(self) ? 1u : 0u;
                    break;
                case 1:
                    t = &t_fire;
                    e.call(kFnFire, {kHands});
                    drv::AICharacterHands_Fire(self);
                    break;
                case 2:
                    t = &t_reloadh;
                    e.call(kFnReload, {kHands});
                    drv::AICharacterHands_Reload(self);
                    break;
                case 3: {
                    t = &t_change;
                    const int32_t w = rng.range(-1, 9);
                    e.call(kFnChangeWeapon, {kHands, uint32_t(w)});
                    drv::AICharacterHands_ChangeWeapon(self, w);
                    break;
                }
                case 4:
                    t = &t_show;
                    e.call(kFnShow, {kHands});
                    drv::AICharacterHands_ShowHands(self);
                    break;
                case 5:
                    t = &t_hide;
                    e.call(kFnHide, {kHands});
                    drv::AICharacterHands_HideHands(self);
                    break;
                default: {
                    t = &t_state;
                    static const int states[9] = {0, 1, 2, 8, 9, 10, 11, 12, 13};
                    const int st = states[rng.range(0, 8)];
                    const uint32_t fn = e.m.read32(kVtHands + 0x44 + 8u * uint32_t(st));
                    e.call(fn, {kHands});
                    drv::ai_vtable(kVtHands)->states[st](self);
                    break;
                }
            }
            ++t->cases;
            std::string why = er != pr ? "o resultado" : compare_char(e, kHands, &ph, 0x170, 0);
            if (why.empty()) why = compare_calls(e);
            if (why.empty()) why = compare_actors(e);
            if (why.empty()) why = compare_hands_globals(e);
            if (!why.empty()) t->fail(why);
        }
        // ---- as mãos: UpdateRotPos, GetRotPos, GetAimFraction ----
        {
            ++t_rot.cases;
            setup_actors(e, rng);
            random_hands_globals(rng, e);
            drv::AICharacterHandsData ph;
            random_char(rng, e, kHands, ph, true, 0);
            if (rng.chance(0.4)) {
                ph.base.state = 0xb;
                e.m.write32(kHands + 0x4c, 0xb);
            }
            begin_case(e);
            e.call(kFnHandsUpdateRotPos, {kHands});
            drv::AICharacterHands_UpdateRotPos(nf::p32_u(&ph));
            std::string why = compare_char(e, kHands, &ph, 0x170, 0);
            if (why.empty()) why = compare_calls(e);
            if (why.empty()) why = compare_hands_globals(e);
            if (why.empty() && std::memcmp(e.m.mem(kRand, 16), &drv::sim_random(), 16) != 0) why = "o SimRandom";
            if (!why.empty()) t_rot.fail(why);
            ++t_getrot.cases;
            e.call(kFnGetRotPos, {kHands, kOut, kOut + 0x40});
            float m[16], vel[3];
            drv::AICharacterHands_GetRotPos(nf::p32_u(&ph), m, vel);
            if (std::memcmp(e.m.mem(kOut, 64), m, 64) != 0 || std::memcmp(e.m.mem(kOut + 0x40, 12), vel, 12) != 0)
                t_getrot.fail("a matriz ou a velocidade");
            ++t_aimfrac.cases;
            const bool have = rng.chance(0.7);
            drv::hands_globals().hands = have ? nf::p32_u(&ph) : 0;
            e.m.write32(kGHands, have ? kHands : 0);
            e.call(kFnAimFraction, {});
            if (e.m.fpr_bits(0) != fbits(drv::AICharacterHands_GetAimFraction())) t_aimfrac.fail("GetAimFraction");
            drv::hands_globals().hands = 0;
        }
        // ---- o ActPoser: Rotate, RotateX, CalculateFinalCorrectionMatrix, SnapMatrixLevelWithGround ----
        {
            const int32_t mirrored = rng.chance(0.5) ? 1 : 0;
            an::ActPoser p(nullptr, 0, mirrored, rng.range(0, 1), 1.0f / 60.0f);
            an::ActPoseMatrices& pm = p.pm();
            float* mats[4] = {pm.initial, pm.correction, pm.snap, pm.pose_to_world};
            for (float* mm : mats) {
                float q[4], r4[4][4];
                random_quat(rng, q);
                drv::VU0_quattom4(q, r4);
                for (int k = 0; k < 3; ++k) r4[3][k] = rng.uni(-30, 30);
                if (rng.chance(0.2)) r4[0][3] = rng.uni(-1, 1);
                std::memcpy(mm, r4, 64);
            }
            std::memset(e.m.mem(kPoser, 0x80), 0, 0x80);
            e.m.write32(kPoser + 4, uint32_t(mirrored));
            e.m.write32(kPoser + 0x28, kPM);
            e.put(kPM, &pm, 0x100);
            const int which = rng.range(0, 3);
            Tally* t = which == 0 ? &t_rotate : which == 1 ? &t_rotatex : which == 2 ? &t_final : &t_snap;
            ++t->cases;
            const float turns = rng.chance(0.2) ? 0.0f : rng.uni(-1.5f, 1.5f);
            if (which == 0) {
                e.m.set_fpr(12, turns);
                e.call(kFnRotate, {kPoser});
                p.Rotate(turns);
            } else if (which == 1) {
                e.m.set_fpr(12, turns);
                e.call(kFnRotateX, {kPoser});
                p.RotateX(turns);
            } else if (which == 2) {
                e.call(kFnFinalCorrection, {kPoser});
                p.CalculateFinalCorrectionMatrix();
            } else {
                e.call(kFnSnap, {kPoser, kPM});
                an::ActPoser::SnapMatrixLevelWithGround(pm.initial);
            }
            for (uint32_t o = 0; o < 0x100; o += 4) {
                uint32_t x = e.m.read32(kPM + o), y;
                std::memcpy(&y, reinterpret_cast<const uint8_t*>(&pm) + o, 4);
                if (x != y) {
                    t->fail("+0x" + hex(o).substr(5) + ": original " + hex(x) + ", porte " + hex(y));
                    break;
                }
            }
        }
        // ---- o RawEventChannel::Eval ----
        {
            ++t_events.cases;
            const int n = rng.range(0, 8);
            std::vector<uint8_t> data(8 + 0x10 * size_t(n) + 16, 0);
            std::memcpy(data.data() + 4, &n, 4);
            float tt = rng.uni(-2, 3);
            for (int k = 0; k < n; ++k) {
                if (!rng.chance(0.2)) tt = ee_add_step(tt, rng);
                const uint32_t id = uint32_t(rng.range(0, 3));
                const float v1 = rng.uni(-1, 1), v2 = rng.uni(-1, 1);
                std::memcpy(data.data() + 8 + 0x10 * k, &id, 4);
                std::memcpy(data.data() + 8 + 0x10 * k + 4, &tt, 4);
                std::memcpy(data.data() + 8 + 0x10 * k + 8, &v1, 4);
                std::memcpy(data.data() + 8 + 0x10 * k + 12, &v2, 4);
            }
            e.put(kEvData, data.data(), uint32_t(data.size()));
            // Os tratadores: quatro objetos (às vezes nulos) com a entrada +0xc no gancho de cada um.
            std::vector<uint32_t> port_handlers(4);
            static uint32_t port_hobj[4][2];
            for (int k = 0; k < 4; ++k) {
                const bool on = rng.chance(0.85);
                port_handlers[size_t(k)] = on ? nf::p32_u(port_hobj[k]) : 0;
                e.m.write32(kHandlers + 4u * uint32_t(k), on ? kHObj + 8u * uint32_t(k) : 0);
                e.m.write32(kHObj + 8u * uint32_t(k) + 4, kHVt + 0x10u * uint32_t(k));
                e.m.write32(kHVt + 0x10u * uint32_t(k) + 8, 0);
                e.m.write32(kHVt + 0x10u * uint32_t(k) + 0xc, kHkHandler[k]);
            }
            // O cursor como o jogo o deixa (0..n; -1 só num canal vazio: com eventos o PS2 leria antes deles).
            int32_t cursor = n > 0 ? rng.range(0, n) : rng.range(-1, 0);
            float last = rng.uni(-2, 3), prev = rng.uni(-2, 4), time = rng.chance(0.2) ? prev : rng.uni(-2, 4);
            if (rng.chance(0.15)) prev = time = 0.0f;
            char detail[400];
            std::snprintf(detail, sizeof detail, " (n %d cursor %d last %g prev %g time %g tempos", n, cursor,
                          double(last), double(prev), double(time));
            std::string det = detail;
            for (int k = 0; k < n; ++k) det += " " + std::to_string(e.get_f(kEvData + 8 + 0x10u * uint32_t(k) + 4));
            det += ")";
            e.m.write32(kOut, uint32_t(cursor));
            e.put_f(kOut + 4, last);
            const uint32_t user = rng.next();
            e.ev_calls.clear();
            e.m.set_fpr(12, prev);
            e.m.set_fpr(13, time);
            e.call(kFnRawEventEval, {kEvData, kOut, kOut + 4, kHandlers, user});
            struct PCall {
                uint32_t handler, event, time, user;
            };
            static std::vector<PCall> pcalls;
            static uint32_t pdata, ph0;
            pcalls.clear();
            pdata = nf::p32_u(data.data());
            ph0 = nf::p32_u(port_hobj[0]);
            an::RawEventDispatch saved = an::raw_event_dispatch();
            an::raw_event_dispatch() = [](uint32_t h, float tm, uint32_t ev, uint32_t u) {
                pcalls.push_back({(h - ph0) / 8u, ev - pdata, ps2::fbits(tm), u});
            };
            an::RawEventChannel_Eval(nf::p32_u(data.data()), cursor, last, prev, time, nf::p32_u(port_handlers.data()),
                                     user);
            an::raw_event_dispatch() = saved;
            // O cursor -1 de um canal vazio: o PS2 lê o "evento -1" (antes dos dados) e chama o tratador dele; o porte o
            // conta como guarda (an::anim_stats().guards) e não chama: fora da comparação.
            e.ev_calls.erase(std::remove_if(e.ev_calls.begin(), e.ev_calls.end(),
                                            [](const Emu::EvCall& c) { return c.event < kEvData + 8; }),
                             e.ev_calls.end());
            std::string why;
            if (e.m.read32(kOut) != uint32_t(cursor)) why = "o cursor";
            else if (e.m.read32(kOut + 4) != fbits(last)) why = "o último tempo";
            else if (e.ev_calls.size() != pcalls.size())
                why = "tratadores: original " + std::to_string(e.ev_calls.size()) + ", porte " +
                      std::to_string(pcalls.size());
            for (size_t k = 0; k < pcalls.size() && why.empty(); ++k) {
                const Emu::EvCall& c = e.ev_calls[k];
                if (c.handler != pcalls[k].handler || c.event - kEvData != pcalls[k].event || c.time != pcalls[k].time ||
                    c.user != pcalls[k].user)
                    why = "tratador " + std::to_string(k);
            }
            if (!why.empty()) t_events.fail(why + det);
        }
    }

    // ---- o EventTarget::ResolveEventId: uma seqüência de nomes do começo (vazio, crescendo de 50 em 50) ----
    {
        std::vector<std::string> vocab;
        for (int k = 0; k < 140; ++k) {
            char b[64];
            std::snprintf(b, sizeof b, "Character_%s_%c%d", (k % 3) == 0 ? "Fire" : (k % 3) == 1 ? "Drop" : "Physics",
                          char('a' + (k * 7) % 26), k * 37 % 101);
            vocab.push_back(b);
        }
        for (int run = 0; run < std::max(1, cases / 400); ++run) {
            const uint32_t port_t = an::ActEventResolver_Construct();
            std::memset(e.m.mem(kTarget, 0x18), 0, 0x18);
            e.m.write32(kTarget + 0x14, 0x32);
            e.heap = kHeap;
            std::string why;
            for (int k = 0; k < 300 && why.empty(); ++k) {
                ++t_resolve.cases;
                std::string name = vocab[size_t(rng.range(0, int(vocab.size()) - 1))];
                if (rng.chance(0.9)) name = "event." + name;
                else if (rng.chance(0.5)) name = "evento." + name;
                std::memcpy(e.m.mem(kNames, uint32_t(name.size() + 1)), name.c_str(), name.size() + 1);
                e.m.write32(kOut, 0xdeadbeef);
                e.call(kFnResolveEventId, {kTarget, kNames, kOut});
                const uint32_t er = uint32_t(e.m.gpr(R::v0)) & 0xff;
                int32_t pid = int32_t(0xdeadbeef);
                const bool pr = an::EventTarget_ResolveEventId(port_t, name.c_str(), pid);
                if (er != (pr ? 1u : 0u)) why = "o resultado de " + name;
                else if (pr && e.m.read32(kOut) != uint32_t(pid)) why = "o número de " + name;
                const an::EventTargetData& pt = *nf::p32_ptr<const an::EventTargetData>(port_t);
                uint16_t ecount;
                std::memcpy(&ecount, e.m.mem(kTarget + 2, 2), 2);
                if (why.empty() && (ecount != pt.count || e.m.read32(kTarget + 0x10) != uint32_t(pt.capacity)))
                    why = "a contagem ou a capacidade";
            }
            // Os nomes em ordem e os números, e a tabela pelo número.
            const an::EventTargetData& pt = *nf::p32_ptr<const an::EventTargetData>(port_t);
            auto emu_str = [&](uint32_t a) { return std::string(reinterpret_cast<const char*>(e.m.mem(a, 1))); };
            for (int k = 0; k < pt.count && why.empty(); ++k) {
                const uint32_t en = e.m.read32(kTarget + 8) + 8u * uint32_t(k);
                const uint32_t pn = pt.names + 8u * uint32_t(k);
                const std::string es = emu_str(e.m.read32(en));
                const std::string ps = nf::p32_ptr<const char>(*nf::p32_ptr<const uint32_t>(pn));
                if (es != ps || e.m.read32(en + 4) != *nf::p32_ptr<const uint32_t>(pn + 4)) why = "o nome " + std::to_string(k);
                const std::string eb = emu_str(e.m.read32(e.m.read32(kTarget + 0xc) + 4u * uint32_t(k)));
                const std::string pb = nf::p32_ptr<const char>(*nf::p32_ptr<const uint32_t>(pt.by_id + 4u * uint32_t(k)));
                if (why.empty() && eb != pb) why = "o nome do número " + std::to_string(k);
            }
            if (!why.empty()) t_resolve.fail(why);
        }
    }

    drv::ai_character_actor_api() = saved_api;
    drv::ai_character_env() = saved_env;
    drv::hands_globals() = saved_hands;
    drv::simulation().cars.clear();
    delete world.car;
    drv::mission_reset();
    drv::simulation_reset(60);

    report("AICharacter::AICharacter 0x00118720", t_ctor, failures);
    report("AICharacterHands::AICharacterHands 0x00123e28", t_hctor, failures);
    report("Util_VecToPoleDepthAndLong 0x00210e08", t_pole, failures);
    report("Util_CharMapLookup 0x00210ff0", t_map, failures);
    report("AICharacter::NotifyZoneDamage 0x00118eb0", t_zone, failures);
    report("AICharacter::Execute 0x00118af0 (a base e as mãos)", t_exec, failures);
    report("AICharacter::MoveToPoint 0x00119140", t_move, failures);
    report("AICharacterEnemy::TargetIsInRange 0x001191e8", t_range, failures);
    report("AICharacterEnemy::NeedsToReload 0x00119270", t_reload, failures);
    report("AICharacterEnemy::TargetIsAcquired 0x00119290", t_acq, failures);
    report("Human::ApplyDamage 0x0017f6c0", t_damage, failures);
    report("Human::SetAICharacter 0x0017f5a0", t_setchar, failures);
    report("Human::Activate/Deactivate 0x0017f540/0x0017f570", t_act, failures);
    report("AICharacterHands::AbleToFire 0x001241e0", t_able, failures);
    report("AICharacterHands::Fire 0x001240c8", t_fire, failures);
    report("AICharacterHands::Reload 0x00124170", t_reloadh, failures);
    report("AICharacterHands::ChangeWeapon 0x00123f90", t_change, failures);
    report("AICharacterHands::ShowHands 0x00123ef8", t_show, failures);
    report("AICharacterHands::HideHands 0x00123f10", t_hide, failures);
    report("AICharacterHands::Do* (os estados pela tabela 0x3696e0)", t_state, failures);
    report("AICharacterHands::UpdateRotPos 0x00124848", t_rot, failures);
    report("AICharacterHands::GetRotPos 0x00123878", t_getrot, failures);
    report("AICharacterHands::GetAimFraction 0x001237d0", t_aimfrac, failures);
    report("ActPoser::Rotate 0x00112dc0", t_rotate, failures);
    report("ActPoser::RotateX 0x00112ef0", t_rotatex, failures);
    report("ActPoser::CalculateFinalCorrectionMatrix 0x00113020", t_final, failures);
    report("ActPoser::SnapMatrixLevelWithGround 0x00113068", t_snap, failures);
    report("EAGLAnim::RawEventChannel::Eval 0x00274958", t_events, failures);
    report("EAGLAnim::EventTarget::ResolveEventId 0x00263e18", t_resolve, failures);
    return failures;
}

}  // namespace drvcheck
