// nf_driving_check, parte 28 (tests/driving_check_fx_ped.h): o que faltava em Paris (docs/driving.md seção 32), o código
// ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte:
//
//  a) Os pedestres: o AICharacterPedestrian::GetPedBase pelos nomes das missões e por nomes sorteados; o Init com as
//     configurações sem pedestres (Paris: o NUM_PED_TYPES_PS2=0 do paris.atr; os conjuntos sem tipos; sem a rede), com
//     ruas especiais sorteadas numa rede falsa (o WRoadNetwork::GetSpecialRoads e o LoadPedTextures originais); o Manage
//     e o InvalidateFurthestPed com zero pedestres.
//  b) Os efeitos das instâncias: o RSceneObj::TriggerFX (todos os tipos), o ToggleEffect, o DecayEffects e o
//     RenderEffects (com o AdjustEffectPosition, o RenderEffect e o AddDynamicLightEffect originais) sobre objetos de cena
//     com RAnimEngine::Handle e efeitos sorteados; o RVehicle::TriggerFX inteiro (com o RVehicle::StartMuzzleFlash
//     original); o 0x001af708 e o AddDynamicLightEffect sozinhos; o GFX::Purge, o SuppressEffect e o SetIntensity (com os
//     CANVAS_* originais) sobre uma galeria falsa.
//  c) Os clarões dos canos: o RMuzzleFlash::Init (o InitPOVLayer), o Draw, o DrawPOV, o ActWeapon::StartMuzzleFlash, o
//     ActWeapon::ManualRender, o RVehicle::StartMuzzleFlash e o RVehicle::RenderFX (a parte dos clarões).
//  d) Os eventos: os destrutores do 36 EPlaySystemAnim, 39 EActorMuzzleFlash, 130 ESetCollisionGeometry, 173
//     ESuppressEffect, 252/253 ESwitchEffectOff/On e 279 ESetReflectiveObject; o WCollisionMgr::SetCollisionArticle (o
//     UGroup::DataLocateTag e o DataCountType originais; a instância de render, o artigo, a referência e o grupo: três
//     leituras).
//  e) O RScorchWorld::ScorchWorld sobre um mundo sintético (o WWorldPos::FindClosestFace e o WCollisionMgr::CheckHitWorld
//     sintéticos, iguais nos dois lados; o HeightAtPoint e o RDecalManager::AddDecal originais).
//
// Fora do código original (ganchos nativos, com a mesma regra nos dois lados): o memset, o printf, o operator new, o
// strstr; o GFX::Trigger/Purge/SuppressEffect/SetIntensity (gravados) no b; o RAnimEngine::Handle::ProcessStimuli e o
// ProcessStimuliZones (gravados), o GetInstanceSystemID e o IsSystemPlaying (uma tabela sorteada), o GetInstancePosition
// (uma matriz por objeto e instância), o GetWorldHeightAtPoint (uma altura pela posição), o RGlareManager::AddModelGlare
// (gravado), o GetVelocity (uma velocidade por objeto), o StartColourDissolve e o RVehicleParticle::TriggerSparks
// (gravados), o AttributeSet::LookupBool/LookupInt e os atributos de física do dono, o UVolatileMaterial::Draw (gravado:
// o material, o tipo, a contagem, a matriz e a camada), o SetZWritesEnable (0x2913f8, o ZMSK), as texturas falsas, o
// PhysicsObject::GetPosition, o SetNowVisible/SendPerViewPort/RSceneObj::Render, o GetPlayerObject, o GetSig, o
// SetCollisionArticle (gravado, no d), a entrada +0x84 dos objetos do 173 (gravada), o RedirectCarpElement, o
// GetSceneObjFromInstance, o _Rb_tree::find e o insert_unique do mapa da STL (um std::map: a biblioteca, não o jogo), o
// UpdateCar e o REmp::DrawShootingEffect e o mundo sintético do e.

#include "driving_check_fx_ped.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/pedestrian.h"
#include "driving/ai/road_network.h"
#include "driving/ai/sim_random.h"
#include "driving/bigf.h"
#include "driving/carp_runtime.h"
#include "driving/fx/bullet_streak.h"
#include "driving/fx/decals.h"
#include "driving/fx/fx_events.h"
#include "driving/fx/fx_lights.h"
#include "driving/fx/fx_render.h"
#include "driving/fx/particles.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/muzzle_flash.h"
#include "driving/fx/scene_fx.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "driving/game/missile.h"
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
constexpr uint32_t kFnGetPedBase = 0x1266a8, kFnPedInit = 0x125bb8, kFnPedManage = 0x1262f0, kFnPedInvalidate = 0x126818;
constexpr uint32_t kFnTriggerFX = 0x1caba8, kFnToggleEffect = 0x1ca198, kFnDecayEffects = 0x1ca340;
constexpr uint32_t kFnRenderEffects = 0x1ca930, kFnVehicleTriggerFX = 0x1d3c28, kFnBrightness = 0x1af708;
constexpr uint32_t kFnDynamicLight = 0x1af8c0, kFnGfxPurge = 0x233160, kFnGfxSuppress = 0x2331c0;
constexpr uint32_t kFnGfxIntensity = 0x2331e8, kFnMuzzleInit = 0x1e5488, kFnMuzzleDraw = 0x1e55f0;
constexpr uint32_t kFnMuzzlePov = 0x1e5808, kFnWeaponStart = 0x116720, kFnManualRender = 0x1169c8;
constexpr uint32_t kFnVehicleStart = 0x1d38d8, kFnRenderFX = 0x1d3278;
constexpr uint32_t kFnSystemAnim = 0x154ad8, kFnActorFlash = 0x1479c0, kFnCollisionGeometry = 0x157be0;
constexpr uint32_t kFnSuppress = 0x15d320, kFnSwitchOff = 0x15d538, kFnSwitchOn = 0x15d650, kFnReflective = 0x1589f0;
constexpr uint32_t kFnSetCollisionArticle = 0x213720, kFnScorchWorld = 0x1ea8e8;
// Os ganchos.
constexpr uint32_t kFnMemset = 0x2a8038, kFnPrintf = 0x10c6c8, kFnOpNew = 0x2d09c8, kFnBuiltinNew = 0x2d0910;
constexpr uint32_t kFnStrstr = 0x2a9c10, kFnEventDelete = 0x175758;
constexpr uint32_t kFnGfxTrigger = 0x2330b0, kFnStimZones = 0x1a2b50, kFnStimuli = 0x1a27a0;
constexpr uint32_t kFnInstancePosition = 0x1c9368, kFnSystemID = 0x1a2db8, kFnSystemPlaying = 0x1a2d10;
constexpr uint32_t kFnWorldHeight = 0x2151b8, kFnModelGlare = 0x1f1d18, kFnGetVelocity = 0x1c9c78;
constexpr uint32_t kFnDissolve = 0x1d3b10, kFnSparks = 0x1eed18, kFnLookupBool = 0x169b28, kFnLookupInt = 0x169bf0;
constexpr uint32_t kFnAttributes = 0x126248;  // (a entrada +0x1d4 falsa do dono: uma função que nada mais chama aqui)
constexpr uint32_t kFnVolatileDraw = 0x2cb938, kFnZmsk = 0x2913f8, kFnGetContext = 0x1d0420, kFnFindTexture = 0x1cf340;
constexpr uint32_t kFnGetPosition = 0x195808, kFnSetNowVisible = 0x1c8c70, kFnPerViewPort = 0x1adc68;
constexpr uint32_t kFnSceneRender = 0x1c85f8, kFnLoadCharacter = 0x10cb88;
constexpr uint32_t kFnRecTriggerFX = 0x125b48;  // (a entrada +0x84 falsa do 173: o ~AICharacterPedestrian, não chamado)
constexpr uint32_t kFnPlayerObject = 0x201ae0, kFnGetSig = 0x1958a8;
constexpr uint32_t kFnFindClosestFace = 0x2322c8, kFnCheckHitWorld = 0x215cc8;

// Os globais do PS2.
constexpr uint32_t kTime = 0x334010, kCameraHolder = 0x333ed8, kLmPtr = 0x333830, kCollisionMgrPtr = 0x33a540;
constexpr uint32_t kWorldPtr = 0x33aac8, kDecalMgrPtr = 0x334bc0, kSim = 0x339190, kRandSeed = 0x333e60;
constexpr uint32_t kRandMul = 0x333e64, kLayerData = 0x3c8338, kLayerCur = 0x3c833c, kWeaponMgrPtr = 0x33a530;
constexpr uint32_t kGalleryPtr = 0x33aad0, kControllerPtr = 0x327c30, kGlobal3340e4 = 0x3340e4;
constexpr uint32_t kSimState = kSim + 0xdb4, kSimSteps = kSim + 0xdc4, kSimCars = kSim + 0xdcc, kSimReflect = kSim + 0xdc8;
constexpr uint32_t kMission = 0x1004f4;
// Os pedestres.
constexpr uint32_t kPedEnabled = 0x3c8230, kPedBase = 0x3c8234, kPedNum = 0x3c8238, kPedSpecialN = 0x3c823c;
constexpr uint32_t kPedForce = 0x3c8240, kPedLastPos = 0x3c8244, kPedChat = 0x3c8248, kPedLastType = 0x3c824c;
constexpr uint32_t kPedVoice = 0x3c8250, kPedInvalid = 0x327ac4, kPedPlayer = 0x422760, kPedSpecial = 0x422460;
constexpr uint32_t kPedSets = 0x3277b0;
constexpr uint32_t kNetValid = 0x3c8254, kNetSegments = 0x33a600, kNetNodes = 0x33a5fc, kNetNumSegments = 0x3c8280;
// Os clarões dos canos.
constexpr uint32_t kMfPositions = 0x334e40, kMfUv = 0x334fe0, kMfColours = 0x3de460, kMfMaterials = 0x3de4d0;
constexpr uint32_t kMfTextures = 0x3de830, kMfPovMaterial = 0x3de7a0, kMfPovPos = 0x3de850, kMfPovUv = 0x3de890;
constexpr uint32_t kMfPovTypes = 0x3350f0;
// As marcas.
constexpr uint32_t kDecalCoords = 0x3da420, kDecalStq = 0x3d8020, kDecalColours = 0x3d7720, kDecalDelay = 0x3dc820;
constexpr uint32_t kDecalAlpha = 0x3dc880;

// A RAM emulada.
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;
constexpr uint32_t kFake = 0x01900000;
constexpr uint32_t kScenes = kFake;                     // 8 objetos de 0x400 (os RVehicle cabem)
constexpr uint32_t kVtScene = kFake + 0x2000, kVtVehicle = kFake + 0x2100, kVtRec = kFake + 0x2200;
constexpr uint32_t kVtOwner = kFake + 0x2300;
constexpr uint32_t kHandles = kFake + 0x4000;           // 8 Handles de 0x1000
constexpr uint32_t kInst = kFake + 0xc000;              // 8 instâncias de 0x40
constexpr uint32_t kVel = kFake + 0xc400;               // 8 velocidades
constexpr uint32_t kLm = kFake + 0xd000;                // o RLightManager (o +0x2e0)
constexpr uint32_t kCamHolder = kFake + 0xd400, kRCam = kFake + 0xd500;
constexpr uint32_t kP0 = kFake + 0xd800, kP1 = kFake + 0xd810;
constexpr uint32_t kOwner = kFake + 0xd900, kAttr = kFake + 0xda00, kParticles = kFake + 0xdb00;
constexpr uint32_t kGal = kFake + 0x10000, kCanv = kFake + 0x12000, kUg = kFake + 0x13000, kEmit = kFake + 0x14000;
constexpr uint32_t kSys = kFake + 0x16000;
constexpr uint32_t kWeapon = kFake + 0x20000, kWInst = kFake + 0x20200, kWm = kFake + 0x20300;
constexpr uint32_t kCarList = kFake + 0x20500, kCar = kFake + 0x20600, kPos = kFake + 0x20700;
constexpr uint32_t kTex = kFake + 0x21000, kLayer = kFake + 0x22000;
constexpr uint32_t kEvent = kFake + 0x23000, kEffRec = kFake + 0x23100, kPhys = kFake + 0x24000;
constexpr uint32_t kWorld = kFake + 0x25000, kRInst = kFake + 0x26000, kPs = kFake + 0x27000, kCtl = kFake + 0x28000;
constexpr uint32_t kTrack = kFake + 0x28100, kCarp = kFake + 0x29000, kCarImg = kFake + 0x2a000;
constexpr uint32_t kMgr = kFake + 0x30000, kCi = kFake + 0x30100, kArti = kFake + 0x31000, kUgca = kFake + 0x31100;
constexpr uint32_t kWp = kFake + 0x40000, kDecalMgr = kFake + 0x41000;
constexpr uint32_t kSegTab = kFake + 0x50000, kSegs = kFake + 0x50400, kNodeTab = kFake + 0x52000;
constexpr uint32_t kNodes = kFake + 0x52400;
constexpr uint32_t kScratch = kFake + 0x60000;
constexpr uint32_t kZero16 = kFake + 0x61000;  // dezesseis bytes zerados (o kZeroCoord3 dos testes)

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2828) {}
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
    const uint32_t n = std::min<uint32_t>(0x1000, MipsMachine::kRamSize - a);
    const char* p = reinterpret_cast<const char*>(const_cast<MipsMachine&>(m).mem(a, n));
    size_t len = 0;
    while (len < n && p[len]) ++len;
    s.assign(p, len);
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
    std::printf("  %-60s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    failures += int(t.bad);
    std::fflush(stdout);
}

void put(MipsMachine& m, uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }
void get(MipsMachine& m, uint32_t addr, void* dst, uint32_t n) { std::memcpy(dst, m.mem(addr, n), n); }

bool same_bytes(const char* what, const void* emu, const void* port, uint32_t n, std::string& out) {
    const uint8_t* a = static_cast<const uint8_t*>(emu);
    const uint8_t* b = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
        uint32_t x = 0, y = 0;
        std::memcpy(&x, a + o, std::min<uint32_t>(4, n - o));
        std::memcpy(&y, b + o, std::min<uint32_t>(4, n - o));
        if (x != y) {
            out = std::string(what) + " +0x" + hex(o).substr(3) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

void random_vec(Rng& r, float* v, int n, float lo, float hi) {
    for (int i = 0; i < n; ++i) v[i] = r.real(lo, hi);
}
void random_unit(Rng& r, float v[3]) {
    float l = 0;
    do {
        random_vec(r, v, 3, -1, 1);
        l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    } while (l < 0.1f);
    for (int i = 0; i < 3; ++i) v[i] /= l;
}
void random_matrix(Rng& r, float m[4][4]) {
    float a[3], b[3], c[3];
    random_unit(r, a);
    random_unit(r, b);
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
    const float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    if (l < 1e-3f) c[0] = 1, c[1] = 0, c[2] = 0;
    else for (float& x : c) x /= l;
    b[0] = c[1] * a[2] - c[2] * a[1];
    b[1] = c[2] * a[0] - c[0] * a[2];
    b[2] = c[0] * a[1] - c[1] * a[0];
    for (int i = 0; i < 3; ++i) m[0][i] = a[i], m[1][i] = b[i], m[2][i] = c[i];
    m[0][3] = m[1][3] = m[2][3] = 0;
    random_vec(r, m[3], 3, -300, 300);
    m[3][3] = 1;
}

uint32_t hash32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// --------------------------------------------------------------------------------------------- o que os ganchos gravam

struct Call {
    uint32_t fn;
    std::vector<uint32_t> w;
    bool operator==(const Call& o) const { return fn == o.fn && w == o.w; }
};
std::string call_text(const Call& c) {
    std::string s = hex(c.fn) + "(";
    for (size_t i = 0; i < c.w.size(); ++i) s += (i ? "," : "") + hex(c.w[i]);
    return s + ")";
}
bool same_calls(const char* what, const std::vector<Call>& a, const std::vector<Call>& b, std::string& why) {
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        if (i >= a.size() || i >= b.size() || !(a[i] == b[i])) {
            why = std::string(what) + " #" + std::to_string(i) + ": original " +
                  (i < a.size() ? call_text(a[i]) : std::string("nada")) + ", porte " +
                  (i < b.size() ? call_text(b[i]) : std::string("nada"));
            return false;
        }
    }
    return true;
}

// O estado sorteado que os ganchos dos dois lados leem.
struct World {
    uint32_t salt = 0;
    std::vector<int32_t> canvas_returns;  // o que o GFX::Trigger devolve, na ordem
    size_t canvas_emu = 0, canvas_port = 0;  // o próximo de cada lado
    bool playing[8][256] = {};             // o IsSystemPlaying de cada (objeto, sistema)
    uint32_t emu_scene[8] = {};
    uint32_t port_scene[8] = {};
    int32_t scene_index(uint32_t a, bool emu) const {
        for (int i = 0; i < 8; ++i)
            if ((emu ? emu_scene[i] : port_scene[i]) == a) return i;
        return -1;
    }
};
World g_w;
std::vector<Call> g_emu_calls, g_port_calls;

// A matriz de uma instância de um objeto (o GetInstancePosition dos dois lados).
void instance_matrix(int32_t scene, int32_t inst, bool world, float m[4][4]) {
    Rng r(hash32(uint32_t(scene) * 131 + uint32_t(inst) * 7 + g_w.salt + (world ? 1u : 0u)));
    random_matrix(r, m);
}
// A altura do chão (o GetWorldHeightAtPoint dos dois lados): um em quatro sem chão.
bool world_height(const float p[3], float* y) {
    const uint32_t h = hash32(ps2::fbits(p[0]) ^ (ps2::fbits(p[2]) * 3) ^ g_w.salt);
    if ((h & 3) == 0) return false;
    *y = float(int32_t(h >> 8 & 0xff) - 128) * 0.25f;
    return true;
}

// ---------------------------------------------------------------------------------------------------------- o Emu

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::string error;
    uint64_t errors = 0;

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
            m.call(fn, args, 200'000'000);
            return true;
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
            return false;
        }
    }
    float getf(uint32_t a) { return ps2::ffloat(m.read32(a)); }
    void putf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto newfn = [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u))); };
        m.hook(kFnOpNew, "operator new", newfn);
        m.hook(kFnBuiltinNew, "__builtin_new", newfn);
        m.hook(kFnPrintf, "printf (0x10c6c8)", [](MipsMachine&) {});
        m.hook(kFnEventDelete, "Event::operator delete", [](MipsMachine&) {});
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnStrstr, "strstr", [](MipsMachine& mm) {
            const uint32_t h = uint32_t(mm.gpr(R::a0));
            const std::string a = cstr(mm, h), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const size_t p = a.find(b);
            mm.set_gpr(R::v0, p == std::string::npos ? 0u : h + uint32_t(p));
        });
        // A câmera do jogador (0x333ed8 +4: o RCamera) e o RLightManager.
        m.write32(kCameraHolder, kCamHolder);
        m.write32(kCamHolder + 4, kRCam);
        m.write32(kLmPtr, kLm);
        m.write32(kLayerData, kLayer);
        m.write32(kLayerCur, 0);
        std::memset(m.mem(kZero16, 16), 0, 16);
    }
};

uint32_t call_f(Emu& e, uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0, float f13 = 0) {
    e.m.set_fpr(12, f12);
    e.m.set_fpr(13, f13);
    if (!e.call(fn, args)) return 0xdeadbeefu;
    return uint32_t(e.m.gpr(R::v0));
}

bool take_error(Emu& e, Tally& t, const std::string& what) {
    if (!e.errors) return false;
    t.fail(what + ": " + e.error);
    e.errors = 0;
    return true;
}

}  // namespace
}  // namespace drvcheck


// a) Os pedestres.

namespace drvcheck {
namespace {

// A rede falsa dos dois lados (a tabela dos trechos e a dos nós): `n` trechos, alguns especiais.
alignas(16) drv::WRoadSegment g_port_segs[64];
alignas(16) drv::WRoadNode g_port_nodes[64];

void build_ped_network(Emu& e, Rng& r, int n) {
    std::memset(g_port_segs, 0, sizeof g_port_segs);
    std::memset(g_port_nodes, 0, sizeof g_port_nodes);
    drv::RoadNetworkGlobals& net = drv::road_network();
    net.segments.assign(size_t(n), 0u);
    net.nodes.assign(64, 0u);
    for (int i = 0; i < 64; ++i) {
        random_vec(r, g_port_nodes[i].position, 3, -2000.0f, 2000.0f);
        net.nodes[size_t(i)] = nf::p32_u(&g_port_nodes[i]);
        put(e.m, kNodes + uint32_t(i) * 0x20, &g_port_nodes[i], 0x20);
        e.m.write32(kNodeTab + uint32_t(i) * 4, kNodes + uint32_t(i) * 0x20);
    }
    for (int i = 0; i < n; ++i) {
        drv::WRoadSegment& s = g_port_segs[i];
        s.node[0] = uint16_t(r.range(0, 63));
        s.node[1] = uint16_t(r.range(0, 63));
        float f[3];
        random_unit(r, f);
        std::memcpy(s.forward, f, 12);
        s.flags = uint16_t(r.next() & 0xffff);
        if (r.chance(0.3)) s.flags |= drv::kSegSpecial;
        s.unknown59 = uint8_t(r.chance(0.7) ? r.range(1, 3) : 0);
        net.segments[size_t(i)] = nf::p32_u(&s);
        put(e.m, kSegs + uint32_t(i) * 0x68, &s, 0x68);
        e.m.write32(kSegTab + uint32_t(i) * 4, kSegs + uint32_t(i) * 0x68);
    }
    net.num_segments = n;
    e.m.write32(kNetSegments, kSegTab);
    e.m.write32(kNetNodes, kNodeTab);
    e.m.write32(kNetNumSegments, uint32_t(n));
}

bool compare_peds(Emu& e, std::string& why) {
    const drv::PedestrianGlobals& g = drv::pedestrian_globals();
    const uint32_t w[] = {uint32_t(g.enabled), uint32_t(g.ped_base), uint32_t(g.num_peds), uint32_t(g.num_special),
                          uint32_t(g.force_special), uint32_t(g.use_last_spawn), uint32_t(g.chat_allowed),
                          uint32_t(g.last_spawn_type)};
    const uint32_t a[] = {kPedEnabled, kPedBase, kPedNum, kPedSpecialN, kPedForce, kPedLastPos, kPedChat, kPedLastType};
    for (size_t i = 0; i < 8; ++i)
        if (e.m.read32(a[i]) != w[i]) {
            why = "o global " + hex(a[i]) + ": original " + hex(e.m.read32(a[i])) + ", porte " + hex(w[i]);
            return false;
        }
    if ((e.m.read32(kPedVoice) & 0xff) != g.voice_delay) {
        why = "o atraso da voz: original " + std::to_string(e.m.read32(kPedVoice) & 0xff) + ", porte " +
              std::to_string(g.voice_delay);
        return false;
    }
    if (e.m.read32(kPedInvalid) != uint32_t(g.invalidated)) {
        why = "o InvalidatedPed (0x327ac4)";
        return false;
    }
    if (!same_bytes("a posição do jogador (0x422760)", e.m.mem(kPedPlayer, 12), g.player, 12, why)) return false;
    const uint32_t ns = std::min<uint32_t>(uint32_t(std::max(g.num_special, 0)), 32u);
    if (!same_bytes("as ruas especiais (0x422460)", e.m.mem(kPedSpecial, ns * 0x18), g.special, ns * 0x18, why))
        return false;
    for (int s = 0; s < 7; ++s)
        if (e.m.read32(kPedSets + uint32_t(s) * 0x70) != uint32_t(g.set_count[s])) {
            why = "a contagem do conjunto " + std::to_string(s);
            return false;
        }
    return true;
}

void check_peds(Emu& e, Rng& r, const nf::DiscSource& disc, int cases, int& failures) {
    Tally tb, ti, tm;
    std::string why;
    // GetPedBase: os nomes das missões e sorteados.
    static const char* const kNames[] = {"paris_mis01", "snow1a_mis3", "snow2a_mis4", "junglea_mis13a",
                                         "jungleb_mis13b", "junglec_mis13c", "uw_mis11", "race", "", "xjunglecx",
                                         "snow1a", "uw_"};
    static const char* const kParts[] = {"snow1a", "snow2a", "junglea", "jungleb", "uw_", "junglec", "paris", "_mis",
                                         "x", "jungle", "snow"};
    for (int c = 0; c < std::max(100, cases / 4); ++c) {
        std::string name;
        if (c < int(sizeof kNames / sizeof kNames[0])) {
            name = kNames[c];
        } else {
            const int k = r.range(0, 3);
            for (int i = 0; i < k; ++i) name += kParts[r.range(0, 10)];
        }
        put(e.m, kMission, name.c_str(), uint32_t(name.size() + 1));
        const uint32_t emu = call_f(e, kFnGetPedBase, {});
        const int32_t port = drv::AICharacterPedestrian_GetPedBase(name.c_str());
        ++tb.cases;
        if (!take_error(e, tb, "GetPedBase") && emu != uint32_t(port))
            tb.fail("\"" + name + "\": original " + std::to_string(emu) + ", porte " + std::to_string(port));
    }
    report("pedestres: AICharacterPedestrian::GetPedBase", tb, failures);

    // Init e Manage nas configurações sem pedestres (o Init com algum tipo cria os 16: fora daqui).
    uint32_t orig_sets[7];
    for (int s = 0; s < 7; ++s) orig_sets[s] = e.m.read32(kPedSets + uint32_t(s) * 0x70);
    // A rede de ruas global começa zerada, como numa partida antes do WRoadNetwork::Init. As partes anteriores a deixam
    // como o Shutdown do PS2 (0x002212f8) a deixa: as tabelas liberadas, mas o fValid e o fNumSegments de Paris ficam
    // (a parte 19 a monta e a desmonta). O Init dos pedestres de Paris, no fim, chamava o GetSpecialRoads com esse estado
    // restaurado: os milhares de trechos de Paris sobre a tabela vazia do port (no PS2 o fSegments seria 0), e a leitura
    // saía do vetor (o travamento da conferência completa, que só aparecia depois das outras partes).
    drv::WRoadNetwork_Shutdown();
    drv::road_network() = drv::RoadNetworkGlobals();
    e.m.hook(kFnLoadCharacter, "ActCharacter::LoadCharacter", [](MipsMachine&) { ++g_w.salt; });
    static float player[3];
    e.m.write32(kSimCars, kCarList);
    e.m.write32(kCarList, kCar);
    e.m.hook(kFnGetPosition, "PhysicsObject::GetPosition", [](MipsMachine& mm) {
        put(mm, kPos, player, 12);
        mm.set_gpr(R::v0, kPos);
    });
    static int32_t lookup_value = 0, lookup_found = 0;
    e.m.hook(kFnLookupInt, "AttributeSet::LookupInt", [](MipsMachine& mm) {
        const uint32_t f = uint32_t(mm.gpr(R::a2));
        if (f) mm.write32(f, uint32_t(lookup_found));
        mm.set_gpr(R::v0, uint32_t(lookup_value));
    });
    static const char* const kZeroNames[] = {"paris_mis01", "junglea_mis13a", "jungleb_mis13b", "uw_mis11",
                                             "junglec_mis13c"};
    for (int c = 0; c < std::max(40, cases / 10); ++c) {
        for (int s = 0; s < 7; ++s) e.m.write32(kPedSets + uint32_t(s) * 0x70, orig_sets[s]);
        drv::pedestrian_globals() = drv::PedestrianGlobals();
        const int which = r.range(0, 4);
        const std::string name = kZeroNames[which];
        put(e.m, kMission, name.c_str(), uint32_t(name.size() + 1));
        // Paris: o atributo achado com 0; os outros conjuntos sem tipos: achado com 0 ou sem ele.
        lookup_found = (which == 0 || r.chance(0.5)) ? 1 : 0;
        lookup_value = 0;
        const int32_t valid = r.chance(0.9) ? 1 : 0;
        e.m.write32(kNetValid, uint32_t(valid));
        drv::road_network().valid = valid;
        build_ped_network(e, r, r.range(0, 60));
        // Lixo nos globais antes (o Init os escreve).
        // Os globais como o port os começa (o Init os escreve quando há a rede).
        for (uint32_t a : {kPedEnabled, kPedBase, kPedNum, kPedSpecialN, kPedLastPos, kPedChat, kPedLastType})
            e.m.write32(a, 0);
        e.m.write32(kPedForce, 0xffffffffu);
        e.m.write32(kPedInvalid, 0xffffffffu);
        e.m.write32(kPedVoice, e.m.read32(kPedVoice) & ~0xffu);
        std::memset(e.m.mem(kPedPlayer, 12), 0, 12);
        call_f(e, kFnPedInit, {16});
        drv::AICharacterPedestrian_Init(name.c_str(), lookup_value, lookup_found != 0);
        ++ti.cases;
        if (take_error(e, ti, "Init")) continue;
        if (!compare_peds(e, why)) {
            ti.fail("\"" + name + "\": " + why);
            continue;
        }
        // O Manage, alguns passos com o jogador sorteado (perto e longe das ruas especiais).
        for (int k = 0; k < 8; ++k) {
            const drv::PedestrianGlobals& g = drv::pedestrian_globals();
            if (g.num_special > 0 && r.chance(0.6)) {
                const drv::PedSpecialRoad& sr = g.special[r.range(0, g.num_special - 1)];
                const float d = r.real(0.0f, 300.0f), a = r.real(0.0f, 6.28f);
                player[0] = sr.x + d * std::cos(a);
                player[1] = r.real(-10.0f, 10.0f);
                player[2] = sr.z + d * std::sin(a);
            } else {
                random_vec(r, player, 3, -2000.0f, 2000.0f);
            }
            if (r.chance(0.1)) {
                const uint8_t v = uint8_t(r.range(0, 3));
                e.m.write32(kPedVoice, (e.m.read32(kPedVoice) & ~0xffu) | v);
                drv::pedestrian_globals().voice_delay = v;
            }
            call_f(e, kFnPedManage, {});
            drv::AICharacterPedestrian_Manage(player);
            ++tm.cases;
            if (take_error(e, tm, "Manage")) break;
            if (!compare_peds(e, why)) {
                tm.fail("passo " + std::to_string(k) + ": " + why);
                break;
            }
        }
    }
    for (int s = 0; s < 7; ++s) e.m.write32(kPedSets + uint32_t(s) * 0x70, orig_sets[s]);
    drv::road_network() = drv::RoadNetworkGlobals();
    report("pedestres: AICharacterPedestrian::Init (sem pedestres)", ti, failures);
    report("pedestres: AICharacterPedestrian::Manage e InvalidateFurthestPed", tm, failures);
    // Paris de verdade: o NUM_PED_TYPES_PS2 do paris.atr (lido pelo nf_driving: 0) dá zero pedestres, e a rede de ruas de
    // Paris (o WRoadNetwork::Init do porte sobre a pista do disco) não tem rua especial.
    drv::pedestrian_globals() = drv::PedestrianGlobals();
    {
        drv::Track track;
        std::string error;
        if (!track.load(disc, "paris_mis01", "", error)) {
            std::printf("    Paris: %s\n", error.c_str());
            ++failures;
        } else {
            drv::WRoadNetwork_Init(track.root(), "paris_mis01");
            drv::AICharacterPedestrian_Init("paris_mis01", 0, true);
            const drv::PedestrianGlobals& g = drv::pedestrian_globals();
            std::printf("    (Paris: %d trechos na rede, conjunto %d, pedestres %d, especiais %d)\n",
                        drv::road_network().num_segments, g.ped_base, g.num_peds, g.num_special);
            if (drv::road_network().valid == 0 || g.enabled == 0 || g.num_peds != 0 || g.num_special != 0 ||
                g.guards != 0) {
                std::printf("    Paris: esperava a rede válida, zero pedestres e zero ruas especiais\n");
                ++failures;
            }
            drv::WRoadNetwork_Shutdown();
        }
    }
    drv::road_network() = drv::RoadNetworkGlobals();
    drv::pedestrian_globals() = drv::PedestrianGlobals();
}

}  // namespace
}  // namespace drvcheck

// b) Os efeitos das instâncias, as luzes dinâmicas e a galeria.

namespace drvcheck {
namespace {

constexpr int kObjs = 6;          // 0-3 objetos de cena, 4-5 RVehicle
constexpr int kMaxEffects = 40;
constexpr uint32_t kHandleFx = 0x40, kHandleZones = 0xa40;  // os efeitos e os u16 (os identificadores depois)

alignas(16) uint8_t g_port_obj[kObjs][0x400];
alignas(16) uint8_t g_port_handle[kObjs][0x1000];
alignas(16) float g_port_inst[kObjs][16];
alignas(16) float g_port_vel[kObjs][4];
alignas(16) uint8_t g_port_owner[0x100];
alignas(16) uint8_t g_port_dummy[16];
bool g_turret = false;
int32_t g_snow = 0;

uint32_t emu_obj(int i) { return kScenes + uint32_t(i) * 0x400; }
uint32_t emu_handle(int i) { return kHandles + uint32_t(i) * 0x1000; }
int32_t handle_index(uint32_t h, bool emu) {
    for (int i = 0; i < kObjs; ++i)
        if (h == (emu ? emu_handle(i) : nf::p32_u(g_port_handle[i]))) return i;
    return -1;
}
// O deslocamento de um efeito no Handle dele (para comparar os ponteiros dos dois lados).
uint32_t effect_key(uint32_t effect, bool emu) {
    for (int i = 0; i < kObjs; ++i) {
        const uint32_t base = emu ? emu_handle(i) : nf::p32_u(g_port_handle[i]);
        if (effect >= base && effect < base + 0x1000) return uint32_t(i) << 16 | (effect - base);
    }
    return 0xffffffffu;
}

// Um efeito sorteado (0x40 bytes).
void random_effect(Rng& r, uint8_t* e) {
    for (int k = 0; k < 0x40; ++k) e[k] = uint8_t(r.next());
    float p[4];
    random_vec(r, p, 3, -3.0f, 3.0f);
    p[3] = r.chance(0.5) ? 1.0f : r.real(-1, 1);
    std::memcpy(e, p, 16);
    uint32_t flags = r.next();
    if (r.chance(0.5)) flags &= 0x29f6u;
    std::memcpy(e + 0x10, &flags, 4);
    const int t = r.range(0, 9);
    e[0x14] = uint8_t(t < 3 ? 6 : t < 5 ? 2 : t < 7 ? 5 : t < 8 ? 8 : r.range(0, 9));
    e[0x15] = uint8_t(r.range(0, 3));
    e[0x16] = uint8_t(r.range(0, 15));
    e[0x17] = uint8_t(r.range(0, 12));
    uint32_t canvas = r.chance(0.5) ? 0u : uint32_t(r.range(1, 512));
    if (e[0x14] == 5) {
        // A luz dinâmica: a prioridade, a cor, o começo e a onda.
        const int32_t prio = r.range(0, 5);
        std::memcpy(e + 0x18, &prio, 4);
        canvas = r.next();  // (a cor)
        const uint32_t start = uint32_t(r.range(0, 30000));
        std::memcpy(e + 0x20, &start, 4);
        const float f[4] = {r.real(0.0f, 0.2f), r.real(0.0f, 1.0f), r.real(0.0f, 2.0f), r.real(-2.0f, 2.0f)};
        std::memcpy(e + 0x24, f, 16);
    } else {
        float q[4];
        random_vec(r, q, 4, -1.0f, 1.0f);
        std::memcpy(e + 0x20, q, 16);
    }
    std::memcpy(e + 0x1c, &canvas, 4);
}

// Monta os seis objetos com Handles nos dois lados.
void build_objects(Emu& e, Rng& r) {
    std::memset(g_port_obj, 0, sizeof g_port_obj);
    std::memset(g_port_handle, 0, sizeof g_port_handle);
    for (int i = 0; i < kObjs; ++i) {
        uint8_t* o = g_port_obj[i];
        for (int k = 0; k < 0x400; ++k) o[k] = uint8_t(r.next());
        const uint16_t z1 = uint16_t(r.chance(0.5) ? 0 : r.next()), z2 = uint16_t(r.chance(0.5) ? 0 : r.next());
        std::memcpy(o + 0x1c, &z1, 2);
        std::memcpy(o + 0x1e, &z2, 2);
        const uint32_t decay = r.chance(0.3) ? 0u : uint32_t(r.range(0, 40000));
        std::memcpy(o + 0x20, &decay, 4);
        random_matrix(r, reinterpret_cast<float(*)[4]>(g_port_inst[i]));
        random_vec(r, g_port_vel[i], 4, -30.0f, 30.0f);
        uint8_t* h = g_port_handle[i];
        const int n = r.range(1, kMaxEffects);
        h[0x1c] = uint8_t(r.range(0, 8));
        h[0x1f] = uint8_t(n);
        for (int k = 0; k < n; ++k) random_effect(r, h + kHandleFx + k * 0x40);
        for (int k = 0; k < n; ++k) {
            const uint16_t zz = uint16_t(r.chance(0.4) ? 0 : (r.chance(0.5) ? 1u << r.range(0, 15) : r.next()));
            std::memcpy(h + kHandleZones + k * 2, &zz, 2);
            h[kHandleZones + n * 2 + k] = uint8_t(r.chance(0.8) ? r.range(0, 12) : r.next());
        }
        const uint64_t lim = n >= 64 ? ~0ull : (1ull << n) - 1;
        auto sub = [&](double p) {
            uint64_t m = 0;
            for (int k = 0; k < n; ++k)
                if (r.chance(p)) m |= 1ull << k;
            return m & lim;
        };
        const uint64_t masks[4] = {sub(0.3), sub(0.3), sub(0.5), r.chance(0.1) ? 0ull : sub(0.7)};
        std::memcpy(h + 0x20, masks, 32);
        // Os ponteiros: a instância, o Handle (os efeitos, os u16), a tabela virtual; o dono e as partículas do RVehicle.
        const uint32_t eh = emu_handle(i);
        std::memcpy(e.m.mem(eh, 0x1000), h, 0x1000);
        e.m.write32(eh + 0x14, eh + kHandleFx);
        e.m.write32(eh + 0x18, eh + kHandleZones);
        const uint32_t ph = nf::p32_u(h);
        const uint32_t pf = ph + kHandleFx;
        std::memcpy(h + 0x14, &pf, 4);
        const uint32_t pz = ph + kHandleZones;
        std::memcpy(h + 0x18, &pz, 4);
        std::memset(h + 4, 0, 0x10);
        std::memset(e.m.mem(eh + 4, 0x10), 0, 0x10);
        const uint32_t eo = emu_obj(i);
        std::memcpy(e.m.mem(eo, 0x400), o, 0x400);
        e.m.write32(eo + 0x08, kInst + uint32_t(i) * 0x40);
        e.m.write32(eo + 0x28, eh);
        e.m.write32(eo + 0x3c, i < 4 ? kVtScene : kVtVehicle);
        put(e.m, kInst + uint32_t(i) * 0x40, g_port_inst[i], 64);
        put(e.m, kVel + uint32_t(i) * 16, g_port_vel[i], 16);
        const uint32_t pi = nf::p32_u(g_port_inst[i]);
        std::memcpy(o + 0x08, &pi, 4);
        std::memcpy(o + 0x28, &ph, 4);
        const uint32_t vt = i < 4 ? 0x12345600u : drv::kVtRVehicle;
        std::memcpy(o + 0x3c, &vt, 4);
        if (i >= 4) {
            const uint32_t own = nf::p32_u(g_port_owner);
            std::memcpy(o + 0x24, &own, 4);
            e.m.write32(eo + 0x24, kOwner);
            const bool parts = r.chance(0.6);
            const uint32_t pp = parts ? nf::p32_u(g_port_dummy) : 0u;
            std::memcpy(o + 0x358, &pp, 4);
            e.m.write32(eo + 0x358, parts ? kParticles : 0u);
        }
        g_w.emu_scene[i] = eo;
        g_w.port_scene[i] = nf::p32_u(o);
    }
    for (int i = 0; i < kObjs; ++i)
        for (int s = 0; s < 256; ++s) g_w.playing[i][s] = r.chance(0.5);
    // O dono dos RVehicle (o tipo, a tabela virtual com os atributos de física: +0xc8 a moto de neve).
    for (uint8_t& b : g_port_owner) b = uint8_t(r.next());
    const int32_t kind = r.chance(0.7) ? 1 : r.range(0, 4);
    std::memcpy(g_port_owner + 0x40, &kind, 4);
    put(e.m, kOwner, g_port_owner, 0x100);
    e.m.write32(kOwner + 0x68, kVtOwner);
    g_turret = r.chance(0.5);
    g_snow = r.chance(0.5) ? 0 : 1;
    e.m.write32(kAttr + 0xc8, uint32_t(g_snow));
    g_w.canvas_returns.clear();
    for (int k = 0; k < 256; ++k) g_w.canvas_returns.push_back(r.chance(0.2) ? 0 : r.range(1, 512));
    g_w.canvas_emu = g_w.canvas_port = 0;
}

bool compare_objects(Emu& e, std::string& why) {
    for (int i = 0; i < kObjs; ++i) {
        const uint8_t* a = e.m.mem(emu_obj(i), 0x400);
        const uint8_t* b = g_port_obj[i];
        const uint32_t end = i < 4 ? 0x40u : 0x370u;
        for (uint32_t o = 0; o < end; o += 4) {
            if (o == 0x08 || o == 0x24 || o == 0x28 || o == 0x3c || o == 0x358) continue;
            uint32_t x, y;
            std::memcpy(&x, a + o, 4);
            std::memcpy(&y, b + o, 4);
            if (x != y) {
                why = "o objeto " + std::to_string(i) + " +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " +
                      hex(y);
                return false;
            }
        }
        const uint8_t* ha = e.m.mem(emu_handle(i), 0x1000);
        const uint8_t* hb = g_port_handle[i];
        if (!same_bytes(("as máscaras do Handle " + std::to_string(i)).c_str(), ha + 0x20, hb + 0x20, 0x20, why))
            return false;
        for (int k = 0; k < hb[0x1f]; ++k)
            if (!same_bytes(("a tela do efeito " + std::to_string(k) + " do objeto " + std::to_string(i)).c_str(),
                            ha + kHandleFx + k * 0x40 + 0x1c, hb + kHandleFx + k * 0x40 + 0x1c, 4, why))
                return false;
    }
    return true;
}

void scene_hooks(Emu& e) {
    MipsMachine& m = e.m;
    m.hook(kFnGfxTrigger, "GFX::Trigger (gravado)", [](MipsMachine& mm) {
        Call c{kFnGfxTrigger, {uint32_t(mm.gpr(R::a0))}};
        for (R reg : {R::a1, R::a2, R::a3, R::t0}) {
            const uint32_t n = (reg == R::a1 || reg == R::a3) ? 3u : 4u;
            for (uint32_t k = 0; k < n; ++k) c.w.push_back(mm.read32(uint32_t(mm.gpr(reg)) + k * 4));
        }
        c.w.push_back(uint32_t(g_w.scene_index(uint32_t(mm.gpr(R::t1)), true)));
        c.w.push_back(uint32_t(mm.gpr(R::t2)));
        const uint32_t x = uint32_t(mm.gpr(R::t3));
        c.w.push_back(x == 0 ? 0u : (x - kVel) / 16 + 1);
        g_emu_calls.push_back(c);
        const int32_t ret = g_w.canvas_emu < g_w.canvas_returns.size() ? g_w.canvas_returns[g_w.canvas_emu++] : 0;
        mm.set_gpr(R::v0, uint32_t(ret));
    });
    for (uint32_t fn : {kFnGfxPurge, kFnGfxSuppress, kFnGfxIntensity})
        m.hook(fn, "GFX (gravado)", [fn](MipsMachine& mm) {
            g_emu_calls.push_back({fn, {uint32_t(mm.gpr(R::a0)), fn == kFnGfxIntensity ? mm.fpr_bits(12) : 0u}});
        });
    m.hook(kFnStimZones, "RAnimEngine::Handle::ProcessStimuliZones (gravado)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnStimZones, {uint32_t(handle_index(uint32_t(mm.gpr(R::a0)), true)),
                                              uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)),
                                              uint32_t(mm.gpr(R::t0))}});
    });
    m.hook(kFnInstancePosition, "RSceneObj::GetInstancePosition (sorteada)", [](MipsMachine& mm) {
        float mat[4][4];
        instance_matrix(g_w.scene_index(uint32_t(mm.gpr(R::a0)), true), int32_t(mm.gpr(R::a1)), mm.gpr(R::a3) != 0, mat);
        put(mm, uint32_t(mm.gpr(R::a2)), mat, 64);
        mm.set_gpr(R::v0, 0);
    });
    m.hook(kFnSystemID, "RAnimEngine::Handle::GetInstanceSystemID", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, (uint32_t(mm.gpr(R::a1)) * 3 + 1) & 0xff);
    });
    m.hook(kFnSystemPlaying, "RAnimEngine::Handle::IsSystemPlaying", [](MipsMachine& mm) {
        const int32_t h = handle_index(uint32_t(mm.gpr(R::a0)), true);
        mm.set_gpr(R::v0, h >= 0 && g_w.playing[h][mm.gpr(R::a1) & 0xff] ? 1u : 0u);
    });
    m.hook(kFnWorldHeight, "WCollisionMgr::GetWorldHeightAtPoint (sorteada)", [](MipsMachine& mm) {
        float p[3], y = 0;
        get(mm, uint32_t(mm.gpr(R::a1)), p, 12);
        const bool ok = world_height(p, &y);
        if (ok) mm.write32(uint32_t(mm.gpr(R::a2)), ps2::fbits(y));
        mm.set_gpr(R::v0, ok ? 1u : 0u);
    });
    m.hook(kFnModelGlare, "RGlareManager::AddModelGlare (gravado)", [](MipsMachine& mm) {
        Call c{kFnModelGlare, {effect_key(uint32_t(mm.gpr(R::a1)), true), mm.fpr_bits(12)}};
        for (uint32_t k = 0; k < 16; ++k) c.w.push_back(mm.read32(uint32_t(mm.gpr(R::a2)) + k * 4));
        g_emu_calls.push_back(c);
    });
    m.hook(kFnGetVelocity, "RSceneObj::GetVelocity (sorteada)", [](MipsMachine& mm) {
        const int32_t i = g_w.scene_index(uint32_t(mm.gpr(R::a0)), true);
        mm.set_gpr(R::v0, (i >= 0 && (i & 1)) ? kVel + uint32_t(i) * 16 : 0u);
    });
    m.hook(kFnDissolve, "RVehicle::StartColourDissolve (gravado)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnDissolve, {}});
        (void)mm;
    });
    m.hook(kFnSparks, "RVehicleParticle::TriggerSparks (gravado)", [](MipsMachine& mm) {
        Call c{kFnSparks, {mm.fpr_bits(12)}};
        for (R reg : {R::a1, R::a2})
            for (uint32_t k = 0; k < 3; ++k) c.w.push_back(mm.read32(uint32_t(mm.gpr(reg)) + k * 4));
        c.w.push_back(uint32_t(mm.gpr(R::a3)));
        g_emu_calls.push_back(c);
    });
    m.hook(kFnLookupBool, "AttributeSet::LookupBool (HAS_TURRET)", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, g_turret ? 1u : 0u);
    });
    m.hook(kFnAttributes, "(a entrada +0x1d4 do dono: os atributos de física)", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, kAttr);
    });
    // As tabelas virtuais falsas: a do RSceneObj (+0x74 o GetVelocity, +0x84 o TriggerFX), a do RVehicle (+0x84 o dele)
    // e a do dono (+0x1d4 os atributos).
    for (uint32_t vt : {kVtScene, kVtVehicle}) {
        m.write32(vt + 0x70, 0);
        m.write32(vt + 0x74, kFnGetVelocity);
        m.write32(vt + 0x80, 0);
        m.write32(vt + 0x84, vt == kVtScene ? kFnTriggerFX : kFnVehicleTriggerFX);
    }
    m.write32(kVtOwner + 0x1d0, 0);
    m.write32(kVtOwner + 0x1d4, kFnAttributes);
}

void port_scene_env() {
    drv::SceneFxEnv& s = drv::scene_fx_env();
    s = drv::SceneFxEnv();
    s.velocity = [](uint32_t scene) -> uint32_t {
        const int32_t i = g_w.scene_index(scene, false);
        return (i >= 0 && (i & 1)) ? nf::p32_u(g_port_vel[i]) : 0u;
    };
    s.instance_position = [](uint32_t scene, int32_t inst, float m[4][4]) {
        instance_matrix(g_w.scene_index(scene, false), inst, true, m);
        return true;
    };
    s.world_height = world_height;
    s.stimuli_zones = [](uint32_t h, uint32_t stim, uint32_t zones, uint32_t time, uint32_t mode) {
        g_port_calls.push_back({kFnStimZones, {uint32_t(handle_index(h, false)), stim, zones, time, mode}});
    };
    s.model_glare = [](uint32_t effect, const float m[4][4]) {
        Call c{kFnModelGlare, {effect_key(effect, false), 0u}};
        for (int k = 0; k < 16; ++k) c.w.push_back(ps2::fbits(m[k / 4][k % 4]));
        g_port_calls.push_back(c);
    };
    s.sparks = [](uint32_t, uint32_t which, const float pos[3], const float dir[3], float scale) {
        Call c{kFnSparks, {ps2::fbits(scale)}};
        for (int k = 0; k < 3; ++k) c.w.push_back(ps2::fbits(pos[k]));
        for (int k = 0; k < 3; ++k) c.w.push_back(ps2::fbits(dir[k]));
        c.w.push_back(which);
        g_port_calls.push_back(c);
    };
    s.system_playing = [](uint32_t h, uint32_t inst) {
        const int32_t i = handle_index(h, false);
        return i >= 0 && g_w.playing[i][(inst * 3 + 1) & 0xff];
    };
    s.gfx_trigger = [](uint32_t ref, const float pos[3], const float quat[4], const float pos2[3], const float quat2[4],
                       uint32_t scene, int32_t instance, uint32_t extra) {
        Call c{kFnGfxTrigger, {ref}};
        for (int k = 0; k < 3; ++k) c.w.push_back(ps2::fbits(pos[k]));
        for (int k = 0; k < 4; ++k) c.w.push_back(ps2::fbits(quat[k]));
        for (int k = 0; k < 3; ++k) c.w.push_back(ps2::fbits(pos2[k]));
        for (int k = 0; k < 4; ++k) c.w.push_back(ps2::fbits(quat2[k]));
        c.w.push_back(uint32_t(g_w.scene_index(scene, false)));
        c.w.push_back(uint32_t(instance));
        int32_t vi = 0;
        for (int i = 0; i < kObjs; ++i)
            if (extra == nf::p32_u(g_port_vel[i])) vi = i + 1;
        c.w.push_back(uint32_t(vi));
        g_port_calls.push_back(c);
        return g_w.canvas_port < g_w.canvas_returns.size() ? g_w.canvas_returns[g_w.canvas_port++] : 0;
    };
    s.gfx_purge = [](int32_t h) { g_port_calls.push_back({kFnGfxPurge, {uint32_t(h), 0u}}); };
    s.gfx_suppress = [](int32_t h) { g_port_calls.push_back({kFnGfxSuppress, {uint32_t(h), 0u}}); };
    s.gfx_intensity = [](int32_t h, float f) { g_port_calls.push_back({kFnGfxIntensity, {uint32_t(h), ps2::fbits(f)}}); };
    drv::MuzzleFlashEnv& mf = drv::muzzle_flash_env();
    mf = drv::MuzzleFlashEnv();
    mf.has_turret = [](uint32_t) { return g_turret; };
    mf.is_snowmobile = [](uint32_t) { return g_snow; };
}

void check_scene_fx(Emu& e, Rng& r, int cases, int& failures) {
    Tally tt, tv, tg, td, tr;
    std::string why;
    scene_hooks(e);
    const drv::SceneFxEnv saved_env = drv::scene_fx_env();
    const drv::MuzzleFlashEnv saved_mf = drv::muzzle_flash_env();
    port_scene_env();
    drv::RLightManagerPositional& lm = drv::light_manager_positional();
    const drv::RLightManagerPositional saved_lm = lm;
    drv::RRandom& rr = drv::render_random();
    const drv::RRandom saved_rr = rr;
    for (int c = 0; c < cases; ++c) {
        if (c % 8 == 0) build_objects(e, r);
        g_w.salt = r.next();
        const uint32_t time = uint32_t(r.range(0, 40000));
        e.m.write32(kTime, time);
        drv::rtime_data().time = time;
        float eye[4];
        random_vec(r, eye, 3, -300.0f, 300.0f);
        eye[3] = 1.0f;
        put(e.m, kRCam + 0x30, eye, 16);
        std::memcpy(drv::scene_fx_env().eye, eye, 16);
        // O RLightManager das luzes dinâmicas (o +0x2e0) e o RRandom (o StartMuzzleFlash do RVehicle).
        for (uint8_t& b : reinterpret_cast<uint8_t(&)[sizeof lm.hl]>(lm.hl)) b = uint8_t(r.next());
        lm.hl.count = r.range(0, 4);
        put(e.m, kLm + 0x2e0, &lm.hl, sizeof lm.hl);
        rr.seed = r.next() & 0xffff;
        rr.multiplier = r.chance(0.5) ? 0x075bcd15u : r.next();
        e.m.write32(kRandSeed, rr.seed);
        e.m.write32(kRandMul, rr.multiplier);
        const int obj = r.range(0, kObjs - 1);
        const int op = r.range(0, 9);
        g_emu_calls.clear();
        g_port_calls.clear();
        const uint64_t dissolves0 = drv::scene_fx_stats().dissolves;
        Tally* t = nullptr;
        const float intensity = r.chance(0.5) ? -1.0f : r.real(0.0f, 2.0f);
        if (op < 4 || (op < 6 && obj < 4)) {
            // RSceneObj::TriggerFX: os tipos 0-11, as máscaras pelo identificador, pelas zonas, pelos bits altos.
            t = &tt;
            const uint32_t type = uint32_t(r.range(0, 11));
            uint32_t mask;
            const int mk = r.range(0, 3);
            mask = mk == 0 ? uint32_t(r.range(0, 12)) : mk == 1 ? 1u << (16 + r.range(0, 15)) : mk == 2 ? r.next() & 0xffff : r.next();
            call_f(e, kFnTriggerFX, {emu_obj(obj), type, mask, 0}, intensity);
            drv::RSceneObj_TriggerFX(intensity, g_w.port_scene[obj], type, mask);
        } else if (op < 6) {
            // RVehicle::TriggerFX: 6 as faíscas, 7 o dissolver, 8 o clarão do tiro, 0xb/0xc as lâmpadas, os outros.
            t = &tv;
            static const int32_t kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 6, 8, 8};
            const int32_t type = kTypes[r.range(0, 15)];
            uint32_t mask = (type == 8) ? uint32_t(r.range(0, 1)) : (type == 0xb || type == 0xc) ? uint32_t(r.range(0, 15))
                                                                                                  : r.next() & 0x1ffff;
            const uint32_t extra = r.chance(0.3) ? 0x2du : uint32_t(r.range(0, 0x40));
            float p0[4], p1[4];
            random_vec(r, p0, 4, -5.0f, 5.0f);
            random_vec(r, p1, 4, -1.0f, 1.0f);
            put(e.m, kP0, p0, 16);
            put(e.m, kP1, p1, 16);
            call_f(e, kFnVehicleTriggerFX, {emu_obj(obj), uint32_t(type), mask, extra, kP0, kP1}, intensity);
            drv::RVehicle_TriggerFXFull(intensity, g_w.port_scene[obj], type, mask, extra, p0, p1);
        } else if (op < 7) {
            t = &td;
            call_f(e, kFnDecayEffects, {emu_obj(obj)});
            drv::RSceneObj_DecayEffects(g_w.port_scene[obj]);
        } else if (op < 9) {
            t = &tr;
            const bool glares_only = r.chance(0.2);
            call_f(e, kFnRenderEffects, {emu_obj(obj), glares_only ? 1u : 0u});
            drv::RSceneObj_RenderEffects(g_w.port_scene[obj], glares_only);
        } else {
            // ToggleEffect direto.
            t = &tg;
            const int n = g_port_handle[obj][0x1f];
            const uint32_t idx = uint32_t(r.range(0, n - 1));
            const bool on = r.chance(0.5);
            call_f(e, kFnToggleEffect, {emu_obj(obj), on ? 1u : 0u, idx, emu_handle(obj) + kHandleFx + idx * 0x40},
                   intensity);
            drv::RSceneObj_ToggleEffect(intensity, g_w.port_scene[obj], on, idx,
                                        nf::p32_u(g_port_handle[obj]) + kHandleFx + idx * 0x40);
        }
        ++t->cases;
        if (take_error(e, *t, "caso " + std::to_string(c))) continue;
        // O dissolver do port só conta: as chamadas do original viram uma contagem.
        uint64_t emu_dissolves = 0;
        std::vector<Call> ec;
        for (const Call& k : g_emu_calls) {
            if (k.fn == kFnDissolve) ++emu_dissolves;
            else ec.push_back(k);
        }
        if (!compare_objects(e, why) || !same_calls("as chamadas", ec, g_port_calls, why) ||
            !same_bytes("o RHighLevelLightManager", e.m.mem(kLm + 0x2e0, sizeof lm.hl), &lm.hl, sizeof lm.hl, why)) {
            t->fail("caso " + std::to_string(c) + " (objeto " + std::to_string(obj) + "): " + why);
            build_objects(e, r);
            continue;
        }
        if (emu_dissolves != drv::scene_fx_stats().dissolves - dissolves0) {
            t->fail("caso " + std::to_string(c) + ": o dissolver");
            continue;
        }
        if (e.m.read32(kRandSeed) != rr.seed) {
            t->fail("caso " + std::to_string(c) + ": o RRandom");
            rr.seed = e.m.read32(kRandSeed);
        }
    }
    report("efeitos: RSceneObj::TriggerFX (Trigger/Suppress/DamageEffects)", tt, failures);
    report("efeitos: RVehicle::TriggerFX (com o StartMuzzleFlash)", tv, failures);
    report("efeitos: RSceneObj::ToggleEffect", tg, failures);
    report("efeitos: RSceneObj::DecayEffects", td, failures);
    report("efeitos: RSceneObj::RenderEffects (AdjustEffectPosition, RenderEffect)", tr, failures);
    drv::scene_fx_env() = saved_env;
    drv::muzzle_flash_env() = saved_mf;
    lm = saved_lm;
    rr = saved_rr;
    for (uint32_t fn : {kFnGfxTrigger, kFnGfxPurge, kFnGfxSuppress, kFnGfxIntensity, kFnStimZones, kFnInstancePosition,
                        kFnSystemID, kFnSystemPlaying, kFnWorldHeight, kFnModelGlare, kFnGetVelocity, kFnDissolve,
                        kFnSparks, kFnLookupBool, kFnAttributes})
        e.m.unhook(fn);
}

// O brilho (0x001af708) e o RHighLevelLightManager::AddDynamicLightEffect sozinhos.
void check_lights(Emu& e, Rng& r, int cases, int& failures) {
    Tally tb, ta;
    std::string why;
    drv::RLightManagerPositional& lm = drv::light_manager_positional();
    const drv::RLightManagerPositional saved = lm;
    alignas(16) static uint8_t effect[0x40];
    for (int c = 0; c < cases; ++c) {
        random_effect(r, effect);
        effect[0x14] = 5;
        uint32_t flags = r.next() & 0x4900u;
        if (r.chance(0.5)) flags |= 0x800;
        std::memcpy(effect + 0x10, &flags, 4);
        if (r.chance(0.2)) {
            const float big = r.real(1.0f, 50.0f);
            std::memcpy(effect + 0x24, &big, 4);
        }
        put(e.m, kEffRec, effect, 0x40);
        const uint32_t time = r.chance(0.1) ? r.next() : uint32_t(r.range(0, 60000));
        e.m.write32(kTime, time);
        call_f(e, kFnBrightness, {kEffRec});
        const uint32_t emu = e.m.fpr_bits(0);
        const uint32_t port = ps2::fbits(drv::DynamicLight_Brightness(nf::p32_u(effect), time));
        ++tb.cases;
        if (!take_error(e, tb, "brilho") && emu != port) tb.fail("original " + hex(emu) + ", porte " + hex(port));
        for (uint8_t& b : reinterpret_cast<uint8_t(&)[sizeof lm.hl]>(lm.hl)) b = uint8_t(r.next());
        lm.hl.count = r.range(0, 4);
        put(e.m, kLm + 0x2e0, &lm.hl, sizeof lm.hl);
        float m[4][4];
        random_matrix(r, m);
        put(e.m, kScratch, m, 64);
        call_f(e, kFnDynamicLight, {kLm + 0x2e0, kEffRec, kScratch});
        drv::RHighLevelLightManager_AddDynamicLightEffect(lm.hl, nf::p32_u(effect), m, time);
        ++ta.cases;
        if (!take_error(e, ta, "AddDynamicLightEffect") &&
            !same_bytes("o RHighLevelLightManager", e.m.mem(kLm + 0x2e0, sizeof lm.hl), &lm.hl, sizeof lm.hl, why))
            ta.fail("caso " + std::to_string(c) + ": " + why);
    }
    lm = saved;
    report("luzes: o brilho da luz dinâmica (0x001af708)", tb, failures);
    report("luzes: RHighLevelLightManager::AddDynamicLightEffect", ta, failures);
}

// A galeria falsa: GFX::Purge (CANVAS_Purge, CANVAS_PurgeSceneObj), SuppressEffect e SetIntensity.
constexpr int kCanvases = 16, kGalEmit = 8;
alignas(16) drv::GfxGallery g_port_gal;
alignas(16) uint8_t g_port_ug[kCanvases][0x40];
alignas(16) drv::ParticleEmitter g_port_emit[kCanvases][kGalEmit];
alignas(16) drv::RParticleSystem g_port_sys[kCanvases][kGalEmit];

void check_gallery(Emu& e, Rng& r, int cases, int& failures) {
    Tally tp, ts, ti;
    std::string why;
    e.m.write32(kGalleryPtr, kGal);
    e.m.write32(kGal + 0x1414, kCanv);
    e.m.hook(kFnGetVelocity, "RSceneObj::GetVelocity (sorteada)", [](MipsMachine& mm) {
        const int32_t i = g_w.scene_index(uint32_t(mm.gpr(R::a0)), true);
        mm.set_gpr(R::v0, (i >= 0 && (i & 1)) ? kVel + uint32_t(i) * 16 : 0u);
    });
    drv::SceneFxEnv& env = drv::scene_fx_env();
    const drv::SceneFxEnv saved = env;
    env.velocity = [](uint32_t scene) -> uint32_t {
        const int32_t i = g_w.scene_index(scene, false);
        return (i >= 0 && (i & 1)) ? nf::p32_u(g_port_vel[i]) : 0u;
    };
    for (int c = 0; c < cases; ++c) {
        if (c % 16 == 0) {
            build_objects(e, r);
            for (int i = 0; i < kCanvases; ++i) {
                drv::GfxCanvas& k = g_port_gal.canvases[i];
                uint8_t* raw = reinterpret_cast<uint8_t*>(&k);
                for (int b = 0; b < 0x90; ++b) raw[b] = uint8_t(r.next());
                const int nump = r.range(0, kGalEmit);
                k.nump = uint8_t(nump);
                // O UGroup do efeito (o 'NUMP' com as posições relativas: os mesmos bytes nos dois lados).
                uint8_t* u = g_port_ug[i];
                std::memset(u, 0, 0x40);
                const uint32_t hdr[4] = {0x4d4e4147u, 2u, 2u, 1u};  // GANM, relativo, 2 dados, o vetor em +0x10
                std::memcpy(u, hdr, 16);
                const uint32_t d0[4] = {0x466c6773u, 0x402u, 1u, 0x20u};  // 'Flgs' em +0x30
                const uint32_t d1[4] = {0x4e554d50u, 0x402u, 1u, 0x14u};  // 'NUMP' em +0x34
                std::memcpy(u + 0x10, d0, 16);
                std::memcpy(u + 0x20, d1, 16);
                const int32_t nump_tag = r.chance(0.9) ? nump : r.range(0, kGalEmit);
                const uint32_t fl = r.next();
                std::memcpy(u + 0x30, &fl, 4);
                std::memcpy(u + 0x34, &nump_tag, 4);
                put(e.m, kUg + uint32_t(i) * 0x40, u, 0x40);
                k.ref = nf::p32_u(u);
                k.scene = r.chance(0.5) ? g_w.port_scene[r.range(0, kObjs - 1)] : 0u;
                for (int j = 0; j < kGalEmit; ++j) {
                    drv::ParticleEmitter& em = g_port_emit[i][j];
                    std::memset(&em, 0, sizeof em);
                    drv::RParticleSystem& sy = g_port_sys[i][j];
                    for (uint8_t& b : reinterpret_cast<uint8_t(&)[sizeof sy]>(sy)) b = uint8_t(r.next());
                    em.system = r.chance(0.7) ? &sy : nullptr;
                }
                k.emitters = g_port_emit[i];
                // A imagem do PS2: os ponteiros traduzidos.
                const uint32_t ec = kCanv + uint32_t(i) * 0x90;
                put(e.m, ec, raw, 0x90);
                e.m.write32(ec + 0x1c, kUg + uint32_t(i) * 0x40);
                e.m.write32(ec + 0x50, k.scene ? g_w.emu_scene[g_w.scene_index(k.scene, false)] : 0u);
                e.m.write32(ec + 0x54, kEmit + uint32_t(i) * kGalEmit * 0x40);
                for (int j = 0; j < kGalEmit; ++j) {
                    const uint32_t ee = kEmit + uint32_t(i * kGalEmit + j) * 0x40;
                    std::memset(e.m.mem(ee, 0x40), 0, 0x40);
                    const uint32_t es = kSys + uint32_t(i * kGalEmit + j) * 0x70;
                    put(e.m, es, &g_port_sys[i][j], 0x70);
                    e.m.write32(ee + 0x18, g_port_emit[i][j].system ? es : 0u);
                }
            }
        }
        const int op = r.range(0, 2);
        const int32_t handle = r.chance(0.1) ? 0 : r.range(1, kCanvases);
        Tally* t = op == 0 ? &tp : op == 1 ? &ts : &ti;
        if (op == 0) {
            call_f(e, kFnGfxPurge, {uint32_t(handle)});
            if (handle - 1 >= 0) drv::GFXGallery_CANVAS_PurgeSceneObj(g_port_gal, handle - 1);
        } else if (op == 1) {
            call_f(e, kFnGfxSuppress, {uint32_t(handle == 0 ? 1 : handle)});
            drv::GFXGallery_CANVAS_Suppress(g_port_gal, handle == 0 ? 1 : handle);
        } else {
            const float f = r.real(-1.0f, 3.0f);
            call_f(e, kFnGfxIntensity, {uint32_t(handle == 0 ? 1 : handle)}, f);
            drv::GFXGallery_CANVAS_SetIntensity(g_port_gal, handle == 0 ? 1 : handle, f);
        }
        ++t->cases;
        if (take_error(e, *t, "galeria")) continue;
        bool ok = true;
        for (int i = 0; i < kCanvases && ok; ++i) {
            const uint32_t ec = kCanv + uint32_t(i) * 0x90;
            const uint8_t* a = e.m.mem(ec, 0x90);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&g_port_gal.canvases[i]);
            for (uint32_t o = 0; o < 0x90 && ok; o += 4) {
                if (o == 0x1c || o == 0x54) continue;
                uint32_t x, y;
                std::memcpy(&x, a + o, 4);
                std::memcpy(&y, b + o, 4);
                if (o == 0x50) y = y ? g_w.emu_scene[g_w.scene_index(y, false)] : 0u;
                if (x != y) {
                    why = "a tela " + std::to_string(i) + " +0x" + hex(o).substr(6) + ": original " + hex(x) + ", porte " +
                          hex(y);
                    ok = false;
                }
            }
            for (int j = 0; j < kGalEmit && ok; ++j) {
                const uint32_t es = kSys + uint32_t(i * kGalEmit + j) * 0x70;
                if (!same_bytes("o sistema (+0x38, +0x4c)", e.m.mem(es + 0x38, 4), &g_port_sys[i][j].active, 4, why) ||
                    !same_bytes("o sistema (+0x4c)", e.m.mem(es + 0x4c, 4), &g_port_sys[i][j].rate_scale, 4, why))
                    ok = false;
            }
        }
        if (!ok) t->fail("caso " + std::to_string(c) + ": " + why);
    }
    env = saved;
    e.m.unhook(kFnGetVelocity);
    report("galeria: GFX::Purge (CANVAS_Purge, CANVAS_PurgeSceneObj)", tp, failures);
    report("galeria: GFX::SuppressEffect (CANVAS_Suppress)", ts, failures);
    report("galeria: GFX::SetIntensity (CANVAS_SetIntensity)", ti, failures);
}

}  // namespace
}  // namespace drvcheck

// c) Os clarões dos canos.

namespace drvcheck {
namespace {

constexpr uint32_t kFnUpdateCar = 0x1ee4a0, kFnEmpShoot = 0x1db848;

// As texturas falsas do FindOrCreateTexture, na ordem em que o Init as pede (as 24 do POV, depois as 5 do Draw).
std::vector<uint32_t> g_tex_names;
int32_t texture_slot_of(uint32_t tex) {
    if (tex < kTex || tex >= kTex + 0x1000 || (tex - kTex) % 0x20) return -1000;
    const int32_t n = int32_t((tex - kTex) / 0x20);
    return n < 24 ? drv::kFxTexMuzzlePov0 + n : drv::kFxTexMuzzle0 + (n - 24);
}

// O UVolatileMaterial::Draw gravado: o material (0-4 o do tipo, 5 o do POV), o tipo, a contagem, a matriz, o ALPHA_1 e o
// TEST_1 do material e as três listas do fgLayerData (a textura pelo índice do port).
Call draw_call(int material, int prim, int count, const float m[4][4], uint64_t alpha, uint64_t test, const float* coords,
               const float* uv, const uint32_t* colours, int texture) {
    Call c{kFnVolatileDraw, {uint32_t(material), uint32_t(prim), uint32_t(count)}};
    for (int k = 0; k < 16; ++k) c.w.push_back(ps2::fbits(m[k / 4][k % 4]));
    c.w.push_back(uint32_t(alpha));
    c.w.push_back(uint32_t(alpha >> 32));
    c.w.push_back(uint32_t(test));
    c.w.push_back(uint32_t(test >> 32));
    for (int k = 0; k < count * 4; ++k) c.w.push_back(ps2::fbits(coords[k]));
    for (int k = 0; k < count * 2; ++k) c.w.push_back(ps2::fbits(uv[k]));
    for (int k = 0; k < count; ++k) c.w.push_back(colours[k]);
    c.w.push_back(uint32_t(texture));
    return c;
}

void port_draws(const std::vector<drv::VolatileDraw>& out, std::vector<Call>& calls) {
    for (const drv::VolatileDraw& d : out) {
        const int material = d.texture >= drv::kFxTexMuzzlePov0 ? 5 : d.texture - drv::kFxTexMuzzle0;
        calls.push_back(draw_call(material, d.prim, d.count, d.matrix, d.alpha, d.test, d.coords.data(), d.uv.data(),
                                  d.colours.data(), d.texture));
    }
}

void muzzle_hooks(Emu& e) {
    MipsMachine& m = e.m;
    m.hook(kFnGetContext, "RTextureContextManager::GetContext", [](MipsMachine& mm) { mm.set_gpr(R::v0, kTex - 0x100); });
    m.hook(kFnFindTexture, "RTextureContext::FindOrCreateTexture (falsa)", [](MipsMachine& mm) {
        const uint32_t t = kTex + uint32_t(g_tex_names.size()) * 0x20;
        g_tex_names.push_back(uint32_t(mm.gpr(R::a1)));
        mm.write32(t + 0x10, 0xabcdef57u);
        mm.set_gpr(R::v0, t);
    });
    m.hook(kFnVolatileDraw, "UVolatileMaterial::Draw (gravado)", [](MipsMachine& mm) {
        const uint32_t mat = uint32_t(mm.gpr(R::a0));
        const int material = mat == kMfPovMaterial ? 5 : int((mat - kMfMaterials) / 0x90);
        const int count = int(mm.gpr(R::a2));
        float mtx[4][4], coords[26 * 4], uv[26 * 2];
        uint32_t colours[26];
        get(mm, uint32_t(mm.gpr(R::a3)), mtx, 64);
        const uint32_t layer = mm.read32(kLayerData + mm.read32(kLayerCur) * 4);
        const int n = std::min(count, 26);
        get(mm, mm.read32(layer + 0x18), coords, uint32_t(n) * 16);
        get(mm, mm.read32(layer + 0x20), uv, uint32_t(n) * 8);
        get(mm, mm.read32(layer + 0x28), colours, uint32_t(n) * 4);
        uint64_t alpha, test;
        get(mm, mat + 0x40, &alpha, 8);
        get(mm, mat + 0x30, &test, 8);
        g_emu_calls.push_back(draw_call(material, int(mm.gpr(R::a1)), n, mtx, alpha, test, coords, uv, colours,
                                        texture_slot_of(mm.read32(layer + 0x30))));
    });
    m.hook(kFnZmsk, "SetZWritesEnable (gravado)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnZmsk, {uint32_t(mm.gpr(R::a1))}});
    });
    m.hook(kFnSetNowVisible, "RSceneObj::SetNowVisible", [](MipsMachine&) {});
    m.hook(kFnPerViewPort, "RRenderSharedData::SendPerViewPort", [](MipsMachine&) {});
    m.hook(kFnSceneRender, "RSceneObj::Render", [](MipsMachine&) {});
    m.hook(kFnUpdateCar, "RVehicleParticle::UpdateCar", [](MipsMachine&) {});
    m.hook(kFnEmpShoot, "REmp::DrawShootingEffect", [](MipsMachine&) {});
    m.hook(kFnRenderEffects, "RSceneObj::RenderEffects", [](MipsMachine&) {});
}

// O ZMSK do original (0 antes do desenho, 1 depois) sai das chamadas: o port não escreve o Z nos clarões.
bool split_zmsk(std::vector<Call>& calls, std::string& why) {
    std::vector<Call> out;
    int open = 0;
    for (const Call& c : calls) {
        if (c.fn != kFnZmsk) {
            out.push_back(c);
            continue;
        }
        if (c.w[0] == 0) ++open;
        else --open;
        if (open < 0 || open > 1) {
            why = "o SetZWritesEnable fora de ordem";
            return false;
        }
    }
    if (open != 0) {
        why = "o SetZWritesEnable sem o 1 depois";
        return false;
    }
    calls.swap(out);
    return true;
}

void check_muzzle(Emu& e, Rng& r, int cases, int& failures) {
    Tally ti, td, tp, ta, tm, tv, tf;
    std::string why;
    muzzle_hooks(e);
    drv::MuzzleFlashState& s = drv::muzzle_flash();
    const drv::MuzzleFlashState saved_state = s;
    const drv::MuzzleFlashEnv saved_env = drv::muzzle_flash_env();
    drv::RRandom& rr = drv::render_random();
    const drv::RRandom saved_rr = rr;
    // Init: os dois lados com o .data do ELF e os materiais construídos (o construtor estático do port nos dois).
    {
        auto read = [&](uint32_t a, void* out, uint32_t n) {
            std::memcpy(out, e.m.mem(a, n), n);
            return true;
        };
        std::vector<uint8_t> data_pos(e.m.mem(kMfPositions, 26 * 16), e.m.mem(kMfPositions, 26 * 16) + 26 * 16);
        drv::RMuzzleFlash_StaticInit(s, read);
        put(e.m, kMfMaterials, s.material, sizeof s.material);
        put(e.m, kMfPovMaterial, s.pov_material, sizeof s.pov_material);
        g_tex_names.clear();
        call_f(e, kFnMuzzleInit, {});
        drv::RMuzzleFlash_Init(s);
        ++ti.cases;
        if (!take_error(e, ti, "Init")) {
            bool ok = same_bytes("os materiais", e.m.mem(kMfMaterials, sizeof s.material), s.material, sizeof s.material,
                                 why) &&
                      same_bytes("o material do POV", e.m.mem(kMfPovMaterial, 0x90), s.pov_material, 0x90, why) &&
                      same_bytes("o modelo", e.m.mem(kMfPositions, sizeof s.positions), s.positions, sizeof s.positions,
                                 why) &&
                      same_bytes("os UV", e.m.mem(kMfUv, sizeof s.uv), s.uv, sizeof s.uv, why) &&
                      same_bytes("as cores", e.m.mem(kMfColours, sizeof s.colours), s.colours, sizeof s.colours, why) &&
                      same_bytes("o quadrado do POV", e.m.mem(kMfPovPos, sizeof s.pov_positions), s.pov_positions,
                                 sizeof s.pov_positions, why) &&
                      same_bytes("os giros do UV", e.m.mem(kMfPovUv, sizeof s.pov_uv), s.pov_uv, sizeof s.pov_uv, why);
            // As texturas: os nomes na ordem do port (as do POV por tipo, depois as cinco) e as bandeiras limpas.
            std::vector<uint32_t> names;
            for (int t = 0; t < drv::kMuzzlePovTypes; ++t)
                for (int k = 0; k < 3; ++k) names.push_back(s.pov[t].names[k]);
            for (int t = 0; t < drv::kMuzzleTypes; ++t) names.push_back(s.texture_names[t]);
            if (ok && names != g_tex_names) {
                why = "os nomes das texturas";
                ok = false;
            }
            for (int t = 0; ok && t < drv::kMuzzleTypes; ++t) {
                if (texture_slot_of(e.m.read32(kMfTextures + uint32_t(t) * 4)) != drv::muzzle_texture_slot(s.texture[t]) ||
                    (e.m.read32(kMfTextures + uint32_t(t) * 4 + 0) &&
                     (e.m.read32(e.m.read32(kMfTextures + uint32_t(t) * 4) + 0x10) & 0xf) != 0)) {
                    why = "a textura " + std::to_string(t);
                    ok = false;
                }
            }
            for (int t = 0; ok && t < drv::kMuzzlePovTypes; ++t)
                for (int k = 0; k < 3; ++k)
                    if (texture_slot_of(e.m.read32(kMfPovTypes + uint32_t(t) * 0x18 + 0xc + uint32_t(k) * 4)) !=
                        drv::muzzle_pov_texture_slot(s.pov[t].textures[k])) {
                        why = "a textura do POV " + std::to_string(t);
                        ok = false;
                    }
            if (!ok) ti.fail(why);
        }
        (void)data_pos;
    }
    report("clarões: RMuzzleFlash::Init (InitPOVLayer)", ti, failures);
    const drv::MuzzleFlashState ready = s;
    std::vector<uint8_t> emu_ready(e.m.mem(0x3de460, 0x3de930 - 0x3de460),
                                   e.m.mem(0x3de460, 0x3de930 - 0x3de460) + (0x3de930 - 0x3de460));

    alignas(16) static uint8_t weapon[0x200];
    alignas(16) static float winst[16];
    alignas(16) static uint8_t vehicle[0x400];
    static float player[3];
    static int32_t last_fired = 0;
    drv::MuzzleFlashEnv& env = drv::muzzle_flash_env();
    env = drv::MuzzleFlashEnv();
    env.player_position = [](float out[3]) { std::memcpy(out, player, 12); };
    env.last_fired = []() { return last_fired; };
    env.has_turret = [](uint32_t) { return g_turret; };
    env.is_snowmobile = [](uint32_t) { return g_snow; };
    e.m.write32(kSimCars, kCarList);
    e.m.write32(kCarList, kCar);
    e.m.hook(kFnGetPosition, "PhysicsObject::GetPosition", [](MipsMachine& mm) {
        put(mm, kPos, player, 12);
        mm.set_gpr(R::v0, kPos);
    });
    e.m.write32(kWeaponMgrPtr, kWm);
    e.m.hook(kFnLookupBool, "AttributeSet::LookupBool (HAS_TURRET)", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, g_turret ? 1u : 0u);
    });
    e.m.hook(kFnAttributes, "(os atributos de física)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kAttr); });
    e.m.write32(kVtOwner + 0x1d0, 0);
    e.m.write32(kVtOwner + 0x1d4, kFnAttributes);
    for (int c = 0; c < cases; ++c) {
        const int op = r.range(0, 9);
        // O estado: o mesmo nos dois lados a cada caso (o Draw não muda os globais; o RRandom é conferido).
        s = ready;
        put(e.m, 0x3de460, emu_ready.data(), uint32_t(emu_ready.size()));
        rr.seed = r.next() & 0xffff;
        rr.multiplier = r.chance(0.8) ? 0x075bcd15u : r.next();
        e.m.write32(kRandSeed, rr.seed);
        e.m.write32(kRandMul, rr.multiplier);
        const bool paused = r.chance(0.2);
        e.m.write32(kSimState, paused ? 3u : uint32_t(r.range(0, 2)));
        g_emu_calls.clear();
        g_port_calls.clear();
        std::vector<drv::VolatileDraw> out;
        float m[4][4];
        random_matrix(r, m);
        put(e.m, kScratch, m, 64);
        Tally* t = nullptr;
        if (op < 2) {
            t = &td;
            float pos[4], dir[4];
            random_vec(r, pos, 4, -2.0f, 2.0f);
            random_unit(r, dir);
            dir[3] = r.real(-1, 1);
            if (r.chance(0.05)) dir[0] = dir[2] = 0, dir[1] = 1;  // paralela ao "para cima"
            put(e.m, kP0, pos, 16);
            put(e.m, kP1, dir, 16);
            const int32_t type = r.range(0, 4);
            const float size = r.real(0.0f, 3.0f);
            call_f(e, kFnMuzzleDraw, {kScratch, kP0, kP1, uint32_t(type)}, size);
            drv::RMuzzleFlash_Draw(s, size, m, pos, dir, type, &out);
        } else if (op < 4) {
            t = &tp;
            float pos[4];
            random_vec(r, pos, 4, -2.0f, 2.0f);
            put(e.m, kP0, pos, 16);
            const int32_t type = r.range(0, 7);
            const float size = r.real(0.0f, 3.0f);
            call_f(e, kFnMuzzlePov, {kScratch, kP0, uint32_t(type)}, size);
            drv::RMuzzleFlash_DrawPOV(s, size, m, pos, type, paused, &out);
        } else if (op < 8) {
            // A arma de um ator: o StartMuzzleFlash e o ManualRender, em sequência sobre a mesma imagem.
            if (c % 16 == 0 || op == 4) {
                for (uint8_t& b : weapon) b = uint8_t(r.next());
                for (uint32_t i = 0; i < 8; ++i) {
                    const int32_t end = r.range(-10, 30);
                    std::memcpy(weapon + 0x84 + i * 0x14 + 0xc, &end, 4);
                    const float st = r.real(0.0f, 2.0f);
                    std::memcpy(weapon + 0x84 + i * 0x14 + 0x10, &st, 4);
                }
                const int32_t flags = int32_t(r.next());
                std::memcpy(weapon + 0x138, &flags, 4);
                random_matrix(r, reinterpret_cast<float(*)[4]>(winst));
                put(e.m, kWeapon, weapon, 0x200);
                put(e.m, kWInst, winst, 64);
                e.m.write32(kWeapon + 0x48, kWInst);
                const uint32_t pi = nf::p32_u(winst);
                std::memcpy(weapon + 0x48, &pi, 4);
            }
            random_vec(r, player, 3, -300.0f, 300.0f);
            const int32_t steps = r.range(0, 30);
            e.m.write32(kSimSteps, uint32_t(steps));
            static const int32_t kLast[] = {0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0, 3, 0x2d};
            last_fired = kLast[r.range(0, 7)];
            e.m.write32(kWm + 0x18, uint32_t(last_fired));
            if (op < 6) {
                t = &ta;
                float pos[3];
                random_vec(r, pos, 3, -300.0f, 300.0f);
                if (r.chance(0.3)) std::memcpy(pos, player, 12);
                put(e.m, kP0, pos, 12);
                call_f(e, kFnWeaponStart, {kWeapon, kP0});
                drv::ActWeapon_StartMuzzleFlash(nf::p32_u(weapon), pos, steps);
            } else {
                t = &tm;
                call_f(e, kFnManualRender, {kWeapon});
                drv::ActWeapon_DrawMuzzleFlashes(s, nf::p32_u(weapon), steps, paused, &out);
            }
            if (!take_error(e, *t, "arma")) {
                const uint8_t* a = e.m.mem(kWeapon, 0x200);
                for (uint32_t o = 0; o < 0x200; o += 4) {
                    if (o == 0x48) continue;
                    uint32_t x, y;
                    std::memcpy(&x, a + o, 4);
                    std::memcpy(&y, weapon + o, 4);
                    if (x != y) {
                        t->fail("caso " + std::to_string(c) + ": a arma +0x" + hex(o).substr(5) + ": original " +
                                hex(x) + ", porte " + hex(y));
                        put(e.m, kWeapon, weapon, 0x200);
                        e.m.write32(kWeapon + 0x48, kWInst);
                        break;
                    }
                }
                e.errors = 0;
            }
        } else {
            // O RVehicle: o StartMuzzleFlash e a parte dos clarões do RenderFX.
            if (c % 16 == 0 || op == 8) {
                for (uint8_t& b : vehicle) b = uint8_t(r.next());
                for (uint32_t i = 0; i < 2; ++i) {
                    const uint32_t end = uint32_t(r.range(0, 40));
                    std::memcpy(vehicle + 0x220 + i * 4, &end, 4);
                    const float st = r.real(0.0f, 4.0f);
                    std::memcpy(vehicle + 0x230 + i * 4, &st, 4);
                    const int32_t ty = r.range(2, 4);
                    std::memcpy(vehicle + 0x228 + i * 4, &ty, 4);
                }
                const uint32_t zero = 0;
                std::memcpy(vehicle + 0x358, &zero, 4);
                std::memcpy(vehicle + 0x360, &zero, 4);
                const float neg = -1.0f;
                std::memcpy(vehicle + 0x35c, &neg, 4);
                put(e.m, kCarImg, vehicle, 0x400);
                for (uint8_t& b : g_port_owner) b = uint8_t(r.next());
                const int32_t kind = r.chance(0.7) ? 1 : r.range(0, 4);
                std::memcpy(g_port_owner + 0x40, &kind, 4);
                put(e.m, kOwner, g_port_owner, 0x100);
                e.m.write32(kOwner + 0x68, kVtOwner);
                const uint32_t own = nf::p32_u(g_port_owner);
                std::memcpy(vehicle + 0x24, &own, 4);
                e.m.write32(kCarImg + 0x24, kOwner);
                g_turret = r.chance(0.5);
                g_snow = r.chance(0.5) ? 0 : 1;
                e.m.write32(kAttr + 0xc8, uint32_t(g_snow));
            }
            const uint32_t time = uint32_t(r.range(0, 40));
            e.m.write32(kTime, time);
            drv::rtime_data().time = time;
            if (op == 8) {
                t = &tv;
                float pos[4], dir[4];
                random_vec(r, pos, 4, -3.0f, 3.0f);
                random_unit(r, dir);
                dir[3] = 0;
                put(e.m, kP0, pos, 16);
                put(e.m, kP1, dir, 16);
                const int32_t slot = r.range(0, 1);
                const int32_t wpn = r.chance(0.3) ? 0x2d : r.range(0, 0x40);
                call_f(e, kFnVehicleStart, {kCarImg, kP0, kP1, uint32_t(slot), uint32_t(wpn)});
                drv::RVehicle_StartMuzzleFlash(nf::p32_u(vehicle), pos, dir, slot, wpn);
            } else {
                t = &tf;
                call_f(e, kFnRenderFX, {kCarImg, kScratch});
                drv::RVehicle_DrawMuzzleFlashes(s, nf::p32_u(vehicle), m, time, paused, &out);
            }
            if (!take_error(e, *t, "carro")) {
                if (!same_bytes("o carro (os clarões)", e.m.mem(kCarImg + 0x1e0, 0x58), vehicle + 0x1e0, 0x58, why)) {
                    t->fail("caso " + std::to_string(c) + ": " + why);
                    put(e.m, kCarImg + 0x1e0, vehicle + 0x1e0, 0x58);
                }
            }
        }
        ++t->cases;
        if (take_error(e, *t, "caso " + std::to_string(c))) continue;
        port_draws(out, g_port_calls);
        if (!split_zmsk(g_emu_calls, why) || !same_calls("os desenhos", g_emu_calls, g_port_calls, why)) {
            t->fail("caso " + std::to_string(c) + ": " + why);
            continue;
        }
        if (e.m.read32(kRandSeed) != rr.seed) t->fail("caso " + std::to_string(c) + ": o RRandom");
    }
    report("clarões: RMuzzleFlash::Draw", td, failures);
    report("clarões: RMuzzleFlash::DrawPOV", tp, failures);
    report("clarões: ActWeapon::StartMuzzleFlash", ta, failures);
    report("clarões: ActWeapon::ManualRender (os clarões)", tm, failures);
    report("clarões: RVehicle::StartMuzzleFlash", tv, failures);
    report("clarões: RVehicle::RenderFX (os clarões)", tf, failures);
    s = saved_state;
    env = saved_env;
    rr = saved_rr;
    for (uint32_t fn : {kFnGetContext, kFnFindTexture, kFnVolatileDraw, kFnZmsk, kFnSetNowVisible, kFnPerViewPort,
                        kFnSceneRender, kFnUpdateCar, kFnEmpShoot, kFnRenderEffects, kFnGetPosition, kFnLookupBool,
                        kFnAttributes})
        e.m.unhook(fn);
}

}  // namespace
}  // namespace drvcheck

// d) Os eventos, o SetCollisionArticle e o ScorchWorld.

namespace drvcheck {
namespace {

constexpr uint32_t kFnStimuliReal = 0x1a27a0, kFnSceneFromInstance = 0x230bf0, kFnRedirect = 0x12c608;
constexpr uint32_t kFnRbFind = 0x21a5c0, kFnRbInsert = 0x21a990;
constexpr uint32_t kSimRigidOwners = 0x339400, kSimSimpleOwners = 0x3394c0;
constexpr uint32_t kInstRec = kEffRec + 0x80, kElem = kCtl, kChar = kCarp, kCar2 = kCarImg;
constexpr uint32_t kMapHeader = kMgr + 0x80;
constexpr uint32_t kSynInst = kFake + 0x32000, kSynArt = kFake + 0x32100;

// Os donos das vagas da Simulation: oito imagens, a vaga aponta uma delas ou nenhuma; o dono k tem o objeto k (0-5) ou
// nenhum (6, 7).
int8_t g_slot_owner[2][96];
alignas(16) uint8_t g_ev_rec[0x40], g_ev_inst_rec[0x10], g_ev_weapon[0x200], g_ev_elem[0x100], g_ev_car[0x100];
alignas(16) uint8_t g_ev_char[0x100];
alignas(16) float g_ev_winst[16];
alignas(16) uint32_t g_ev_rinst[16][16];
int32_t g_inst_scene[16];
float g_ev_player[3];

uint32_t ev_sig(uint32_t obj, bool emu) {
    if (obj == (emu ? kCar : 0x7fff0001u)) return 0x1111;
    if (obj == (emu ? kCar2 : nf::p32_u(g_ev_car))) return 0x2222;
    if (obj == (emu ? kChar : nf::p32_u(g_ev_char))) return 0x3333;
    return 0x4444;
}

int32_t scene_of_instance_ptr(uint32_t inst, bool emu) {
    for (int i = 0; i < kObjs; ++i)
        if (inst == (emu ? kInst + uint32_t(i) * 0x40 : nf::p32_u(g_port_inst[i]))) return i;
    return -1;
}

void event_hooks(Emu& e) {
    MipsMachine& m = e.m;
    m.hook(kFnStimuliReal, "RAnimEngine::Handle::ProcessStimuli (gravado)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnStimuliReal, {uint32_t(handle_index(uint32_t(mm.gpr(R::a0)), true)),
                                                uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                                                uint32_t(mm.gpr(R::a3)), uint32_t(mm.gpr(R::t0))}});
    });
    m.hook(kFnInstancePosition, "RSceneObj::GetInstancePosition (sorteada)", [](MipsMachine& mm) {
        float mat[4][4];
        instance_matrix(g_w.scene_index(uint32_t(mm.gpr(R::a0)), true), int32_t(mm.gpr(R::a1)), mm.gpr(R::a3) != 0, mat);
        put(mm, uint32_t(mm.gpr(R::a2)), mat, 64);
        mm.set_gpr(R::v0, 0);
    });
    m.hook(kFnGetPosition, "PhysicsObject::GetPosition", [](MipsMachine& mm) {
        put(mm, kPos, g_ev_player, 12);
        mm.set_gpr(R::v0, kPos);
    });
    m.hook(kFnSceneFromInstance, "WWorld::GetSceneObjFromInstance (a tabela)", [](MipsMachine& mm) {
        const uint32_t k = (uint32_t(mm.gpr(R::a1)) - kRInst) / 0x40;
        const int32_t s = k < 16 ? g_inst_scene[k] : -1;
        mm.set_gpr(R::v0, s >= 0 ? g_w.emu_scene[s] : 0u);
    });
    m.hook(kFnSetCollisionArticle, "WCollisionMgr::SetCollisionArticle (gravado)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnSetCollisionArticle, {uint32_t(scene_of_instance_ptr(uint32_t(mm.gpr(R::a1)), true)),
                                                        uint32_t(mm.gpr(R::a2))}});
        mm.set_gpr(R::v0, 1);
    });
    m.hook(kFnRecTriggerFX, "(a entrada +0x84 do objeto, gravada)", [](MipsMachine& mm) {
        g_emu_calls.push_back({kFnRecTriggerFX, {uint32_t(g_w.scene_index(uint32_t(mm.gpr(R::a0)), true)),
                                                 uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), mm.fpr_bits(12),
                                                 uint32_t(mm.gpr(R::a3)), uint32_t(mm.gpr(R::t0)),
                                                 uint32_t(mm.gpr(R::t1))}});
    });
    m.hook(kFnRedirect, "AIElementController::RedirectCarpElement", [](MipsMachine& mm) { mm.set_gpr(R::v0, kElem); });
    m.hook(kFnPlayerObject, "Simulation::GetPlayerObject", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCar); });
    m.hook(kFnGetSig, "PhysicsObject::GetSig", [](MipsMachine& mm) {
        mm.write32(uint32_t(mm.gpr(R::a0)), ev_sig(uint32_t(mm.gpr(R::a1)), true));
        mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0)));
    });
    for (uint32_t vt : {kVtScene, kVtVehicle}) {
        m.write32(vt + 0x80, 0);
        m.write32(vt + 0x84, kFnRecTriggerFX);
    }
}

void port_event_env() {
    drv::FxEventsEnv& f = drv::fx_events_env();
    f = drv::FxEventsEnv();
    f.physics_scene = [](int32_t simple, int32_t slot) -> uint32_t {
        const int8_t k = g_slot_owner[simple][slot];
        return (k >= 0 && k < kObjs) ? g_w.port_scene[k] : 0u;
    };
    f.instance_position_local = [](uint32_t scene, int32_t inst, float m[4][4]) {
        instance_matrix(g_w.scene_index(scene, false), inst, false, m);
        return true;
    };
    f.stimuli = [](uint32_t h, uint32_t system, uint32_t stimulus, uint32_t time, uint32_t mode) {
        g_port_calls.push_back({kFnStimuliReal, {uint32_t(handle_index(h, false)), system, stimulus, time, mode}});
    };
    f.scene_from_instance = [](uint32_t inst) -> uint32_t {
        for (int k = 0; k < 16; ++k)
            if (inst == nf::p32_u(g_ev_rinst[k])) return g_inst_scene[k] >= 0 ? g_w.port_scene[g_inst_scene[k]] : 0u;
        return 0u;
    };
    f.set_collision_article = [](uint32_t instance, int32_t article) {
        g_port_calls.push_back({kFnSetCollisionArticle, {uint32_t(scene_of_instance_ptr(instance, false)),
                                                         uint32_t(article)}});
    };
    f.steps = []() { return drv::simulation().steps; };
    f.object_sig = [](uint32_t obj) { return ev_sig(obj, false); };
    f.player_sig = []() { return ev_sig(0x7fff0001u, false); };
    f.trigger_fx = [](uint32_t scene, int32_t type, uint32_t mask) {
        g_port_calls.push_back({kFnRecTriggerFX, {uint32_t(g_w.scene_index(scene, false)), uint32_t(type), mask,
                                                  0xbf800000u, 0u, 0x355b80u, 0x355b80u}});
    };
    f.redirect_element = [](uint32_t) { return nf::p32_u(g_ev_elem); };
    drv::MuzzleFlashEnv& mf = drv::muzzle_flash_env();
    mf = drv::MuzzleFlashEnv();
    mf.player_position = [](float out[3]) { std::memcpy(out, g_ev_player, 12); };
}

// O objeto de um 36 ou 173 nas palavras +0xc..+0x18 (a vaga, o registro da instância) e no objeto direto.
void scene_words(Rng& r, uint32_t* ew, uint32_t* pw, int direct_at) {
    const int pick = r.range(0, 3);
    const int s = r.range(0, kObjs - 1);
    // O registro da instância: +8 o objeto (ou nenhum).
    std::memset(g_ev_inst_rec, 0, sizeof g_ev_inst_rec);
    const bool rec_scene = r.chance(0.8);
    const uint32_t ps = rec_scene ? g_w.port_scene[s] : 0u;
    std::memcpy(g_ev_inst_rec + 8, &ps, 4);
    const uint32_t ws[3] = {r.chance(0.5) ? r.next() | 1 : 0u, r.chance(0.7) ? 1u : 0u, 0u};
    ew[3] = pw[3] = ws[0];
    ew[4] = ws[1] ? kInstRec : 0u;
    pw[4] = ws[1] ? nf::p32_u(g_ev_inst_rec) : 0u;
    const bool simple = r.chance(0.5);
    ew[5] = pw[5] = simple ? uint32_t(r.range(1, 3)) : 0u;
    ew[6] = pw[6] = r.chance(0.2) ? 0xffffffffu : uint32_t(r.range(0, simple ? 95 : 47));
    ew[direct_at] = pick == 0 ? g_w.emu_scene[s] : 0u;
    pw[direct_at] = pick == 0 ? g_w.port_scene[s] : 0u;
}

void put_inst_rec(Emu& e) {
    put(e.m, kInstRec, g_ev_inst_rec, 0x10);
    uint32_t p;
    std::memcpy(&p, g_ev_inst_rec + 8, 4);
    const int32_t i = g_w.scene_index(p, false);
    e.m.write32(kInstRec + 8, i >= 0 ? g_w.emu_scene[i] : 0u);
}

void check_events(Emu& e, Rng& r, int cases, int& failures) {
    Tally t36, t39, t130, t173, t252, t279;
    std::string why;
    std::vector<uint8_t> saved_slots(e.m.mem(kSimRigidOwners, 0x240), e.m.mem(kSimRigidOwners, 0x240) + 0x240);
    event_hooks(e);
    const drv::FxEventsEnv saved_env = drv::fx_events_env();
    const drv::MuzzleFlashEnv saved_mf = drv::muzzle_flash_env();
    const int32_t saved_steps = drv::simulation().steps;
    const uint32_t saved_reflect = drv::sim_reflective_object();
    drv::RRandom& rr = drv::render_random();
    const drv::RRandom saved_rr = rr;
    port_event_env();
    e.m.write32(kSimCars, kCarList);
    e.m.write32(kCarList, kCar);
    e.m.write32(kWorldPtr, kWorld);
    for (int c = 0; c < cases; ++c) {
        if (c % 16 == 0) {
            build_objects(e, r);
            // As vagas da Simulation.
            for (uint32_t k = 0; k < 8; ++k) {
                std::memset(e.m.mem(kPhys + k * 0x80, 0x80), 0, 0x80);
                e.m.write32(kPhys + k * 0x80 + 0x48, k < uint32_t(kObjs) ? g_w.emu_scene[k] : 0u);
            }
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < (s ? 96 : 48); ++i) {
                    g_slot_owner[s][i] = int8_t(r.chance(0.3) ? -1 : r.range(0, 7));
                    e.m.write32((s ? kSimSimpleOwners : kSimRigidOwners) + uint32_t(i) * 4,
                                g_slot_owner[s][i] < 0 ? 0u : kPhys + uint32_t(g_slot_owner[s][i]) * 0x80);
                }
            for (int k = 0; k < 16; ++k) g_inst_scene[k] = r.chance(0.3) ? -1 : r.range(0, kObjs - 1);
        }
        const uint32_t time = uint32_t(r.range(0, 40000));
        e.m.write32(kTime, time);
        drv::rtime_data().time = time;
        g_emu_calls.clear();
        g_port_calls.clear();
        uint32_t ew[8] = {}, pw[8] = {};
        static const int32_t kTypes[] = {36, 39, 130, 173, 252, 253, 279};
        const int32_t type = kTypes[r.range(0, 6)];
        drv::PendingEvent ev{type, 0, 0.0f, 0, 0, 0};
        Tally* t = nullptr;
        uint32_t fn = 0;
        if (type == 36) {
            t = &t36;
            fn = kFnSystemAnim;
            ew[0] = pw[0] = 0x370af8;
            ew[1] = pw[1] = uint32_t(r.range(0, 12));
            ew[2] = pw[2] = r.next() & 0xff;
            scene_words(r, ew, pw, 7);
            put_inst_rec(e);
        } else if (type == 39) {
            t = &t39;
            fn = kFnActorFlash;
            ew[0] = pw[0] = 0x371620;
            random_effect(r, g_ev_rec);
            put(e.m, kEffRec, g_ev_rec, 0x40);
            for (uint8_t& b : g_ev_weapon) b = uint8_t(r.next());
            for (uint32_t i = 0; i < 8; ++i) {
                const int32_t end = r.range(-10, 30);
                std::memcpy(g_ev_weapon + 0x84 + i * 0x14 + 0xc, &end, 4);
            }
            random_matrix(r, reinterpret_cast<float(*)[4]>(g_ev_winst));
            put(e.m, kWeapon, g_ev_weapon, 0x200);
            put(e.m, kWInst, g_ev_winst, 64);
            e.m.write32(kWeapon + 0x48, kWInst);
            const uint32_t pi = nf::p32_u(g_ev_winst);
            std::memcpy(g_ev_weapon + 0x48, &pi, 4);
            const bool has_effect = r.chance(0.9), has_scene = r.chance(0.9);
            const int s = r.range(0, kObjs - 1);
            ew[1] = has_effect ? kEffRec : 0u;
            pw[1] = has_effect ? nf::p32_u(g_ev_rec) : 0u;
            ew[2] = kWeapon;
            pw[2] = nf::p32_u(g_ev_weapon);
            ew[3] = has_scene ? g_w.emu_scene[s] : 0u;
            pw[3] = has_scene ? g_w.port_scene[s] : 0u;
            random_vec(r, g_ev_player, 3, -300.0f, 300.0f);
            const int32_t steps = r.range(0, 30);
            e.m.write32(kSimSteps, uint32_t(steps));
            drv::simulation().steps = steps;
            rr.seed = r.next() & 0xffff;
            e.m.write32(kRandSeed, rr.seed);
            e.m.write32(kRandMul, rr.multiplier);
        } else if (type == 130) {
            t = &t130;
            fn = kFnCollisionGeometry;
            ew[0] = pw[0] = 0x370768;
            const int k = r.range(0, 15);
            ew[1] = kRInst + uint32_t(k) * 0x40;
            pw[1] = nf::p32_u(g_ev_rinst[k]);
            ew[2] = pw[2] = r.chance(0.2) ? 0xffffffffu : uint32_t(r.range(0, 12));
            const int s = r.range(0, kObjs - 1);
            const bool direct = r.chance(0.4);
            ew[3] = direct ? g_w.emu_scene[s] : 0u;
            pw[3] = direct ? g_w.port_scene[s] : 0u;
        } else if (type == 173) {
            t = &t173;
            fn = kFnSuppress;
            ew[0] = pw[0] = 0x370300;
            random_effect(r, g_ev_rec);
            put(e.m, kEffRec, g_ev_rec, 0x40);
            const bool has_effect = r.chance(0.5);
            ew[1] = has_effect ? kEffRec : 0u;
            pw[1] = has_effect ? nf::p32_u(g_ev_rec) : 0u;
            scene_words(r, ew, pw, 2);
            put_inst_rec(e);
            ew[7] = pw[7] = uint32_t(r.range(0, 20));
        } else if (type == 252 || type == 253) {
            t = &t252;
            fn = type == 252 ? kFnSwitchOff : kFnSwitchOn;
            ew[0] = pw[0] = type == 252 ? 0x3702e8u : 0x3702d0u;
            for (uint8_t& b : g_ev_elem) b = uint8_t(r.next());
            for (uint8_t& b : g_ev_car) b = uint8_t(r.next());
            const int32_t et = r.chance(0.7) ? 1 : r.range(0, 3);
            std::memcpy(g_ev_elem + 0x44, &et, 4);
            put(e.m, kElem, g_ev_elem, 0x100);
            put(e.m, kCar2, g_ev_car, 0x100);
            e.m.write32(kElem + 0xfc, kCar2);
            const uint32_t pc = nf::p32_u(g_ev_car);
            std::memcpy(g_ev_elem + 0xfc, &pc, 4);
            const bool has = r.chance(0.9);
            ew[1] = pw[1] = has ? (r.next() | 1) : 0u;
            ew[2] = pw[2] = r.chance(0.8) ? uint32_t(r.range(0, 31)) : r.next();
        } else {
            t = &t279;
            fn = kFnReflective;
            ew[0] = pw[0] = 0x370630;
            for (uint8_t& b : g_ev_elem) b = uint8_t(r.next());
            const int32_t et = r.range(0, 2);
            std::memcpy(g_ev_elem + 0x44, &et, 4);
            const bool car = r.chance(0.8);
            put(e.m, kElem, g_ev_elem, 0x100);
            e.m.write32(kElem + 0xfc, car ? kCar2 : 0u);
            e.m.write32(kElem + 0x54, kChar);
            const uint32_t pc = car ? nf::p32_u(g_ev_car) : 0u, ph = nf::p32_u(g_ev_char);
            std::memcpy(g_ev_elem + 0xfc, &pc, 4);
            std::memcpy(g_ev_elem + 0x54, &ph, 4);
            const bool has = r.chance(0.7);
            ew[1] = has ? kElem : 0u;
            pw[1] = has ? nf::p32_u(g_ev_elem) : 0u;
            e.m.write32(kSimReflect, 0x5555);
            drv::sim_reflective_object() = 0x5555;
        }
        put(e.m, kEvent, ew, 32);
        for (int k = 0; k < 8; ++k) ev.obj[k] = pw[k];
        call_f(e, fn, {kEvent, 0});
        drv::fx_event_execute(ev);
        ++t->cases;
        if (take_error(e, *t, "evento " + std::to_string(type))) continue;
        std::string w;
        bool ok = same_calls("as chamadas", g_emu_calls, g_port_calls, w);
        if (ok && type == 39)
            ok = same_bytes("a arma", e.m.mem(kWeapon + 0x84, 0xb8), g_ev_weapon + 0x84, 0xb8, w) &&
                 e.m.read32(kRandSeed) == rr.seed;
        if (ok && (type == 252 || type == 253))
            ok = same_bytes("o carro (+0x6c, +0x70)", e.m.mem(kCar2 + 0x6c, 8), g_ev_car + 0x6c, 8, w);
        if (ok && type == 279 && e.m.read32(kSimReflect) != drv::sim_reflective_object()) {
            w = "o objeto refletido: original " + hex(e.m.read32(kSimReflect)) + ", porte " +
                hex(drv::sim_reflective_object());
            ok = false;
        }
        if (!ok) t->fail("caso " + std::to_string(c) + ": " + (w.empty() ? std::string("o RRandom") : w));
        if (type == 39) rr.seed = e.m.read32(kRandSeed);
    }
    put(e.m, kSimRigidOwners, saved_slots.data(), 0x240);
    drv::fx_events_env() = saved_env;
    drv::muzzle_flash_env() = saved_mf;
    drv::simulation().steps = saved_steps;
    drv::sim_reflective_object() = saved_reflect;
    rr = saved_rr;
    for (uint32_t fn : {kFnStimuliReal, kFnInstancePosition, kFnGetPosition, kFnSceneFromInstance, kFnSetCollisionArticle,
                        kFnRecTriggerFX, kFnRedirect, kFnPlayerObject, kFnGetSig})
        e.m.unhook(fn);
    report("eventos: EPlaySystemAnim (36)", t36, failures);
    report("eventos: EActorMuzzleFlash (39, com o StartMuzzleFlash)", t39, failures);
    report("eventos: ESetCollisionGeometry (130)", t130, failures);
    report("eventos: ESuppressEffect (173)", t173, failures);
    report("eventos: ESwitchEffectOff/On (252/253, MissionEditorSwitch)", t252, failures);
    report("eventos: ESetReflectiveObject (279, SetReflectiveObject)", t279, failures);
}

// O WCollisionMgr::SetCollisionArticle: as instâncias de colisão, as de render com os artigos (UGroups com os dados
// 'ca' de etiqueta curta e outros), o mapa da STL (o _Rb_tree::find e o insert_unique gravados num std::map: a
// biblioteca, não o jogo).
constexpr int kCaInst = 24, kCaGroups = 4;
alignas(16) drv::WCollisionInstance g_ca_inst[kCaInst];
alignas(16) uint32_t g_ca_rinst[16][16];
alignas(16) uint32_t g_ca_arti[kCaGroups], g_ca_ref[kCaGroups];
alignas(16) uint8_t g_ca_ug[kCaGroups][0x200];
std::map<uint32_t, uint32_t> g_emu_map;

void build_ugroup(Rng& r, uint8_t* u) {
    std::memset(u, 0, 0x200);
    const int n = r.range(0, 10);
    const int extra = r.range(0, 2);
    const int total = n + extra;
    const uint32_t hdr[4] = {0x61727469u, 2u, uint32_t(total), 1u};
    std::memcpy(u, hdr, 16);
    // A ordem: os 'ca' com índices sorteados (sem repetir) e os outros no meio.
    std::vector<uint32_t> idx;
    while (int(idx.size()) < n) {
        const uint32_t k = uint32_t(r.range(0, 15));
        if (std::find(idx.begin(), idx.end(), k) == idx.end()) idx.push_back(k);
    }
    int ci = 0;
    for (int i = 0; i < total; ++i) {
        const bool other = ci >= n || (extra && r.chance(0.2));
        uint32_t tag = other ? 0x78780000u | uint32_t(r.range(0, 15)) : 0x63610000u | idx[size_t(ci++)];
        if (!other && ci > n) tag = 0x78780001u;
        const uint32_t entry = 16 + uint32_t(i) * 16;
        const uint32_t data = 16 + uint32_t(total + 1) * 16 + uint32_t(i) * 4;
        const uint32_t d[4] = {tag, 0x403u, 1u, data - entry};
        std::memcpy(u + entry, d, 16);
        const uint32_t v = r.next();
        std::memcpy(u + data, &v, 4);
    }
}

void check_collision_article(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    std::string why;
    e.m.hook(kFnRbFind, "_Rb_tree::find (um std::map)", [](MipsMachine& mm) {
        const uint32_t key = mm.read32(uint32_t(mm.gpr(R::a2)));
        const uint32_t out = uint32_t(mm.gpr(R::a0));
        mm.write32(out, g_emu_map.count(key) ? 0x1u : mm.read32(uint32_t(mm.gpr(R::a1))));
        mm.set_gpr(R::v0, out);
    });
    e.m.hook(kFnRbInsert, "_Rb_tree::insert_unique (um std::map)", [](MipsMachine& mm) {
        const uint32_t v = uint32_t(mm.gpr(R::a2));
        g_emu_map.insert({mm.read32(v), mm.read32(v + 4)});
        const uint32_t out = uint32_t(mm.gpr(R::a0));
        mm.write32(out, 0x1);
        mm.write32(out + 4, 1);
        mm.set_gpr(R::v0, out);
    });
    e.m.write32(kWorldPtr, kWorld);
    e.m.write32(kWorld + 0x20, kRInst);
    static drv::WCollisionMgr pm;
    std::map<uint32_t, uint32_t> saved;
    int n = 0;
    for (int c = 0; c < cases; ++c) {
        if (c % 16 == 0) {
            // As instâncias de colisão e as de render (os índices de render em 0..19: alguns sem instância de render).
            n = r.range(0, kCaInst);
            for (int i = 0; i < kCaInst; ++i) {
                uint8_t* b = reinterpret_cast<uint8_t*>(&g_ca_inst[i]);
                for (int k = 0; k < 0x40; ++k) b[k] = uint8_t(r.next());
                g_ca_inst[i].render_index = uint16_t(r.range(0, 19));
            }
            put(e.m, kCi, g_ca_inst, sizeof g_ca_inst);
            std::memset(&pm, 0, sizeof pm);
            pm.instances = nf::p32_u(g_ca_inst);
            pm.instance_count = uint32_t(n);
            std::memset(e.m.mem(kMgr, 0x80), 0, 0x80);
            e.m.write32(kMgr + 0x08, kCi);
            e.m.write32(kMgr + 0x0c, uint32_t(n));
            e.m.write32(kMgr + 0x20, kMapHeader);
            for (int g = 0; g < kCaGroups; ++g) {
                build_ugroup(r, g_ca_ug[g]);
                put(e.m, kUgca + uint32_t(g) * 0x200, g_ca_ug[g], 0x200);
                g_ca_ref[g] = nf::p32_u(g_ca_ug[g]);
                g_ca_arti[g] = nf::p32_u(&g_ca_ref[g]);
                e.m.write32(kArti + uint32_t(g) * 4, kArti + 0x40 + uint32_t(g) * 4);
                e.m.write32(kArti + 0x40 + uint32_t(g) * 4, kUgca + uint32_t(g) * 0x200);
            }
            for (int k = 0; k < 16; ++k) {
                for (uint32_t& w : g_ca_rinst[k]) w = r.next();
                const int g = r.range(0, kCaGroups - 1);
                put(e.m, kRInst + uint32_t(k) * 0x40, g_ca_rinst[k], 0x40);
                g_ca_rinst[k][7] = nf::p32_u(&g_ca_arti[g]);
                e.m.write32(kRInst + uint32_t(k) * 0x40 + 0x1c, kArti + uint32_t(g) * 4);
            }
            saved.clear();
            g_emu_map.clear();
        }
        const int k = r.range(0, 15);
        const int32_t article = r.chance(0.15) ? -1 : (r.chance(0.05) ? -2 : r.range(0, 16));
        const uint32_t emu = call_f(e, kFnSetCollisionArticle, {kMgr, kRInst + uint32_t(k) * 0x40, uint32_t(article)});
        const uint32_t port = drv::WCollisionMgr_SetCollisionArticle(pm, nf::p32_u(g_ca_rinst[0]),
                                                                     nf::p32_u(g_ca_rinst[k]), article, saved);
        ++t.cases;
        if (take_error(e, t, "SetCollisionArticle")) continue;
        bool ok = emu == port;
        if (!ok) why = "o retorno: original " + std::to_string(emu) + ", porte " + std::to_string(port);
        for (int i = 0; ok && i < kCaInst; ++i) {
            const uint32_t a = e.m.read32(kCi + uint32_t(i) * 0x40 + 0x1c), b = g_ca_inst[i].article;
            bool same = a == b;
            for (int g = 0; !same && g < kCaGroups; ++g)
                if (a >= kUgca + uint32_t(g) * 0x200 && a < kUgca + uint32_t(g + 1) * 0x200)
                    same = (a - kUgca - uint32_t(g) * 0x200) == (b - nf::p32_u(g_ca_ug[g]));
            if (!same) {
                why = "o artigo da instância " + std::to_string(i) + ": original " + hex(a) + ", porte " + hex(b);
                ok = false;
            }
        }
        if (ok) {
            std::map<uint32_t, uint32_t> a, b;
            for (const auto& [key, v] : g_emu_map) a[(key - kCi) / 0x40] = v;
            for (const auto& [key, v] : saved)
                b[uint32_t((nf::p32_ptr<const uint8_t>(key) - reinterpret_cast<const uint8_t*>(g_ca_inst)) / 0x40)] = v;
            if (a != b) {
                why = "o mapa dos artigos originais";
                ok = false;
            }
        }
        if (!ok) t.fail("caso " + std::to_string(c) + ": " + why);
    }
    e.m.unhook(kFnRbFind);
    e.m.unhook(kFnRbInsert);
    report("colisão: WCollisionMgr::SetCollisionArticle (DataLocateTag, DataCountType)", t, failures);
}

// O ScorchWorld sobre um mundo sintético (o da parte 21: células de 2 m, uma face por célula; aqui também o
// CheckHitWorld sintético: o choque, o tipo e o subtipo da face por um número do segmento).
struct SynthWorld {
    uint32_t seed = 1;
    uint32_t hash(int32_t cx, int32_t cz) const {
        uint32_t h = seed * 0x9e3779b1u ^ uint32_t(cx) * 0x85ebca6bu ^ uint32_t(cz) * 0xc2b2ae35u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return h;
    }
    void face(const float pos[3], drv::WWorldPos& wp, uint32_t instance_clear, uint32_t instance_marked) const {
        const int32_t cx = int32_t(std::floor(pos[0] * 0.5f)), cz = int32_t(std::floor(pos[2] * 0.5f));
        const uint32_t h = hash(cx, cz);
        if ((h & 31) == 0) {
            wp.valid = 0;
            return;
        }
        const float base = float(int32_t(h >> 20 & 7) - 3) * 0.02f, sx = float(int32_t(h >> 8 & 15) - 7) * 0.004f,
                    sz = float(int32_t(h >> 12 & 15) - 7) * 0.004f;
        auto y = [&](float x, float z) { return base + sx * (x - float(cx) * 2.0f) + sz * (z - float(cz) * 2.0f); };
        const float x0 = float(cx) * 2.0f, z0 = float(cz) * 2.0f;
        const float v[3][3] = {{x0, y(x0, z0), z0}, {x0 + 2.0f, y(x0 + 2.0f, z0), z0}, {x0, y(x0, z0 + 2.0f), z0 + 2.0f}};
        for (int k = 0; k < 3; ++k) {
            wp.v0[k] = v[0][k];
            wp.v1[k] = v[(h >> 30 & 1) ? 2 : 1][k];
            wp.v2[k] = v[(h >> 30 & 1) ? 1 : 2][k];
        }
        wp.surface[0] = uint8_t((h >> 16 & 15) == 0 ? 6 : (h >> 16 & 15) == 1 ? 9 : 1);
        wp.surface[1] = uint8_t((h >> 24 & 15) == 0 ? 2 : 0);
        wp.valid = 1;
        const uint32_t ik = h >> 27 & 7;
        wp.instance = ik == 0 ? instance_marked : ik == 1 ? instance_clear : 0;
    }
    // O choque de um segmento: 7 em 8 acham algo (1-3), um em 6 é do tipo 6, um em 5 do subtipo 2.
    void hit(const float* seg, uint8_t& type, uint8_t& subtype, uint8_t& hit) const {
        uint32_t h = seed;
        for (int k = 0; k < 8; ++k) h = hash32(h ^ ps2::fbits(seg[k]));
        hit = (h & 7) == 0 ? 0 : uint8_t(1 + (h >> 3) % 3);
        type = (h >> 8) % 6 == 0 ? 6 : uint8_t(1 + (h >> 12 & 3));
        subtype = (h >> 16) % 5 == 0 ? 2 : uint8_t(h >> 20 & 0xfd);
    }
};
SynthWorld g_synth;
alignas(16) drv::WCollisionInstance g_synth_inst[2];
alignas(16) uint8_t g_synth_article[2][0x20];

class SynthCollision : public drv::WorldCollision {
public:
    bool find_closest_face(drv::WWorldPos& wp, const float pos[3], bool) override {
        g_synth.face(pos, wp, nf::p32_u(&g_synth_inst[0]), nf::p32_u(&g_synth_inst[1]));
        return true;
    }
    void make_face_at_point(drv::WWorldPos&, const float*) override {}
    float surface_bump_height(const float*, const uint8_t*) override { return 0.0f; }
};

bool synth_hit(const float* seg, drv::WorldCollisionInfo& info) {
    g_synth.hit(seg, info.face_type, info.face_subtype, info.hit);
    return info.hit != 0;
}

bool compare_decals(Emu& e, const drv::DecalState& s, std::string& why) {
    if (e.m.read32(kDecalMgr + 0x294) != uint32_t(s.m.next)) {
        why = "o índice +0x294: original " + std::to_string(e.m.read32(kDecalMgr + 0x294)) + ", porte " +
              std::to_string(s.m.next);
        return false;
    }
    return same_bytes("as posições (0x3da420)", e.m.mem(kDecalCoords, sizeof s.b.coords), s.b.coords,
                      sizeof s.b.coords, why) &&
           same_bytes("o STQ (0x3d8020)", e.m.mem(kDecalStq, sizeof s.b.stq), s.b.stq, sizeof s.b.stq, why) &&
           same_bytes("as cores (0x3d7720)", e.m.mem(kDecalColours, sizeof s.b.colours), s.b.colours,
                      sizeof s.b.colours, why) &&
           same_bytes("os atrasos (0x3dc820)", e.m.mem(kDecalDelay, 96), s.b.delay, 96, why) &&
           same_bytes("os alfas (0x3dc880)", e.m.mem(kDecalAlpha, 96), s.b.alpha, 96, why);
}

void reset_decals(Emu& e, drv::DecalState& s, const std::vector<uint8_t>& dat) {
    drv::RDecalManager_Construct(s, dat);
    std::memset(e.m.mem(kDecalMgr, 0x298), 0, 0x298);
    e.m.write32(kDecalMgr, 0x0038b840);
    put(e.m, kDecalMgr + 0x10, s.m.types, sizeof s.m.types);
    put(e.m, kDecalCoords, s.b.coords, sizeof s.b.coords);
    put(e.m, kDecalStq, s.b.stq, sizeof s.b.stq);
    put(e.m, kDecalColours, s.b.colours, sizeof s.b.colours);
    put(e.m, kDecalDelay, s.b.delay, 96);
    put(e.m, kDecalAlpha, s.b.alpha, 96);
}

void check_scorch(Emu& e, Rng& r, const nf::DiscSource& disc, int cases, int& failures) {
    Tally t;
    std::string why;
    std::vector<uint8_t> dat;
    {
        drv::Bigf mis;
        if (mis.open(disc, "DRIVING/MIS01.VIV")) {
            const int i = mis.find("data\\render\\decal.dat");
            if (i >= 0) mis.read(i, dat);
        }
    }
    if (dat.size() < 0x280) {
        std::printf("  sem o data\\render\\decal.dat do MIS01.VIV: tipos de marca sorteados\n");
        dat.resize(0x280);
        for (uint8_t& b : dat) b = uint8_t(r.next());
    }
    e.m.write32(kDecalMgrPtr, kDecalMgr);
    e.m.hook(kFnFindClosestFace, "WWorldPos::FindClosestFace (o mundo sintético)", [](MipsMachine& mm) {
        const uint32_t a = uint32_t(mm.gpr(R::a0));
        drv::WWorldPos wp;
        get(mm, a, &wp, 0x40);
        float p[3];
        get(mm, uint32_t(mm.gpr(R::a1)), p, 12);
        g_synth.face(p, wp, kSynInst, kSynInst + 0x40);
        put(mm, a, &wp, 0x40);
        mm.set_gpr(R::v0, 1);
    });
    e.m.hook(kFnCheckHitWorld, "WCollisionMgr::CheckHitWorld (o mundo sintético)", [](MipsMachine& mm) {
        float seg[8];
        get(mm, uint32_t(mm.gpr(R::a1)), seg, 32);
        uint8_t type, sub, hit;
        g_synth.hit(seg, type, sub, hit);
        const uint32_t info = uint32_t(mm.gpr(R::a2));
        *mm.mem(info + 0x50, 1) = type;
        *mm.mem(info + 0x51, 1) = sub;
        *mm.mem(info + 0x53, 1) = hit;
        mm.set_gpr(R::v0, hit != 0 ? 1u : 0u);
    });
    // As duas instâncias do mundo sintético (o byte +0xc do artigo: 0 e 1).
    std::memset(e.m.mem(kSynInst, 0x80), 0, 0x80);
    std::memset(e.m.mem(kSynArt, 0x40), 0, 0x40);
    e.m.write32(kSynInst + 0x1c, kSynArt);
    e.m.write32(kSynInst + 0x40 + 0x1c, kSynArt + 0x20);
    *e.m.mem(kSynArt + 0x20 + 0xc, 1) = 1;
    std::memset(g_synth_inst, 0, sizeof g_synth_inst);
    std::memset(g_synth_article, 0, sizeof g_synth_article);
    g_synth_inst[0].article = nf::p32_u(g_synth_article[0]);
    g_synth_inst[1].article = nf::p32_u(g_synth_article[1]);
    g_synth_article[1][0xc] = 1;
    static drv::DecalState s;
    SynthCollision world;
    uint64_t added = 0, feet = 0;
    reset_decals(e, s, dat);
    for (int c = 0; c < cases; ++c) {
        if ((c & 63) == 0) g_synth.seed = r.next() | 1;
        float pos[4] = {r.real(-60, 60), r.real(-0.5f, 4.0f), r.real(-60, 60), r.chance(0.8) ? 1.0f : r.real(-1, 1)};
        float n[4];
        if (r.chance(0.4)) {
            n[0] = r.real(-0.3f, 0.3f), n[1] = 1.0f, n[2] = r.real(-0.3f, 0.3f);
            const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int k = 0; k < 3; ++k) n[k] /= l;
        } else {
            random_unit(r, n);
            if (r.chance(0.5)) n[1] *= 0.2f;
        }
        n[3] = r.chance(0.7) ? 0.0f : r.real(-1, 1);
        const int32_t type = r.range(0, 7);
        const float size = r.chance(0.3) ? r.real(0.0f, 0.33f) : r.real(0.0f, 9.0f);
        drv::WWorldPos wp{};
        std::memset(e.m.mem(kWp, 0x40), 0, 0x40);
        put(e.m, kScratch, pos, 16);
        put(e.m, kScratch + 0x10, n, 16);
        const uint64_t before = drv::decal_stats().added, feet0 = drv::decal_stats().world_feet;
        call_f(e, kFnScorchWorld, {kWp, kScratch, kScratch + 0x10, uint32_t(type)}, size);
        drv::RScorchWorld_ScorchWorld(s, &world, wp, pos, n, type, size, synth_hit);
        added += drv::decal_stats().added - before;
        feet += drv::decal_stats().world_feet - feet0;
        ++t.cases;
        if (take_error(e, t, "ScorchWorld")) continue;
        if (!compare_decals(e, s, why)) {
            t.fail("caso " + std::to_string(c) + ": " + why);
            reset_decals(e, s, dat);
        }
    }
    e.m.unhook(kFnFindClosestFace);
    e.m.unhook(kFnCheckHitWorld);
    report("marcas: RScorchWorld::ScorchWorld", t, failures);
    std::printf("    (marcas postas pelo ScorchWorld: %" PRIu64 " de %d, %" PRIu64 " descidas ao chão)\n", added, cases,
                feet);
}

}  // namespace
}  // namespace drvcheck

namespace drvcheck {

int run_fx_ped_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size, int cases,
                      uint32_t seed) {
    std::printf("\nparte 28: os pedestres, os efeitos das instâncias, os clarões dos canos, as marcas dos tiros e os "
                "eventos que faltavam em Paris\n");
    int failures = 0;
    static Emu e;
    e.setup(elf, vu0, vu0_size);
    Rng r(seed ^ 0x28);
    check_peds(e, r, disc, cases, failures);
    check_scene_fx(e, r, cases, failures);
    check_lights(e, r, cases, failures);
    check_gallery(e, r, cases, failures);
    check_muzzle(e, r, cases, failures);
    check_events(e, r, cases, failures);
    check_collision_article(e, r, cases, failures);
    check_scorch(e, r, disc, cases, failures);
    return failures;
}

}  // namespace drvcheck
