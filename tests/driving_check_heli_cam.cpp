// nf_driving_check, parte 19 (tests/driving_check_heli.h), a câmera da seção do helicóptero: o código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS com a VU0 ao lado do porte (driving/camera/player_camera_modes.cpp,
// camera_math.cpp), com as tabelas do camera.ini do MIS01.VIV para o "helicopter" com o tipo secundário "vanquish" (o
// LoadFile 0x1a49e0: os modos AutoDrive, WorldAnim e AIPathAnim), o mundo de colisão de Paris (a câmera Heli entre
// eles), os 18 registros 'Cams' e os 'AISp' com os 'pt' de Paris:
//  - o RPlayerCamera::RPlayerCamera e, quadro a quadro, o UpdateCamera (0x1b29e0) com a âncora andando, entre pedidos
//    sorteados: SetAutoDriveCamera (0x1bb7c0), a entrada da mira pelo CameraInputCallback (0x1bcc00, o
//    AutoDriveCamInputHandler: os giros, o centro, o zoom, a troca de braço com a arma do gerente sorteada, o giro de
//    snow1a_mis3), CameraLockOn (0x1bc400, num ponto que se move e de volta), WeaponFired (0x1bd288), o alvo do
//    WTargetPicker e a mira automática (UpdateADTargetAngles), o míssil e o estado da missão; TriggerAnimationCamera
//    (0x1bc510, com o diretor distribuindo a transição), TriggerAIPathAnimationCamera (0x1bc890, o AISplinePath andado
//    pelo RPathEngine::Update 0x1b2058 a cada passo), EndCameraAnim (0x1bb098), SetLastSelectableCameraMode (0x1bb1f8),
//    ForceCameraChange (0x1bd1b8), ShakeCamera (0x1ba2f8), as pausas da simulação (os pedidos por id só para os modos
//    Heli: o Dashboard e o Bumper da tabela não foram portados);
//  - compara a câmera inteira (0x370 bytes), o RPlayerCamState, os globais do módulo (0x3339d0..0x3339eb, 0x333a20,
//    0x333a30, 0x3276e0), os braços do AutoDrive (os campos escritos em jogo), a fila do diretor, o RCameraSpline (os
//    pontos, os quatérnios, o tipo e a lista), os registros 'Cams' (o TriggerAnimationCamera grava a distância em +0x2c)
//    e as bandeiras de som do carro (o AVehicle de GetAudio, +0xb8/+0xbc).
// O que não é comparado: os ponteiros (+0xa8, +0x104 e +0x110 pela presença, +0x114, +0x118, +0x140 pelo alvo, +0x350
// a +0x36c; na fila os +0x18/+0x1c pelo registro e +0x20/+0x24) e o w do braço suavizado (+0x2fc, lixo da pilha).
// Fora do código original (ganchos nativos), além dos da parte 8: o RAnimEngine::Handle::Create (0x1a1bf0) e o
// ~Handle (0x1a2558) para registros sem animação (o cabeçalho com a contagem e a cópia do registro, como o porte),
// tanf/atan2f/acosf/asinf (o nf::fm do porte), InputConfigManager::Get (a configuração do POV),
// WTargetPicker::GetSelectedTarget, PBondCar::GetAudio, o SetAttackMode/SetNavigateMode do AIGroundVehicle.

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "driving/carp_runtime.h"
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
constexpr uint32_t kSim = 0x339190;

constexpr uint32_t kFnLoadFile = 0x1a49e0, kFnCameraCtor = 0x1b2620, kFnUpdate = 0x1b29e0;
constexpr uint32_t kNoisePerm = 0x32f4e8, kNoiseGrad = 0x3d08a8;  // as tabelas do Noise (fRandomTable e os gradientes)
constexpr uint32_t kFnSetAutoDrive = 0x1bb7c0, kFnInput = 0x1bcc00, kFnLockOn = 0x1bc400, kFnTriggerAnim = 0x1bc510,
                   kFnTriggerAIPath = 0x1bc890, kFnEndAnim = 0x1bb098, kFnShake = 0x1ba2f8, kFnForce = 0x1bd1b8,
                   kFnLastSel = 0x1bb1f8, kFnWeaponFired = 0x1bd288, kFnEngineUpdate = 0x1b2058;

// Os globais do original.
constexpr uint32_t kTables = 0x3339b0, kConstants = 0x333610, kModeGlobals = 0x4244b0;
constexpr uint32_t kShakeEye = 0x333a20, kPrevRates = 0x333a30, kVertigoCount = 0x333a38, kZoomButton = 0x3276e0;
constexpr uint32_t kWorldCameras = 0x333ed8;  // a lista das câmeras do mundo ([1]: o RPlayerCamera)
constexpr uint32_t kOverrideCar = 0x33196c, kWeaponMgrPtr = 0x33a530, kMissionPtr = 0x33a024,
                   kRendererPtr = 0x333760, kUber = 0x100000, kRate = 0x333ff0, kFrequency = 0x333ff4,
                   kPathList = 0x3339a8;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000, kInfos = 0x01002000, kCar = 0x01012000, kCarVt = 0x01014000,
                   kRender = 0x01014400, kCarsVec = 0x01014500, kWeaponMgr = 0x01014600, kWeaponSlots = 0x01014700,
                   kStrings = 0x01015400, kStrings2 = 0x01015440, kCam = 0x01016000, kIniBuf = 0x01017000,
                   kScratchpad = 0x01028000;
constexpr uint32_t kFnCarType = 0x0102f000, kFnZero = 0x0102f010, kFnSecondary = 0x0102f020, kFnAudio = 0x0102f030;
constexpr uint32_t kMgr = 0x01030000, kGrid = 0x01030100;
constexpr uint32_t kMission = 0x01031000, kRendererObj = 0x01032000, kIcm = 0x01032100, kAudio = 0x01032200,
                   kTarget = 0x01032300, kLockPoint = 0x01032340, kEvent = 0x01032400, kWorldCamList = 0x01032480,
                   kCams = 0x01033000, kSplines = 0x01034000,
                   kPaths = 0x01040000;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 2323) {}
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

bool same_words(const std::string& what, const uint8_t* emu, const void* port_v, uint32_t n,
                std::initializer_list<std::pair<uint32_t, uint32_t>> skip, std::string& out) {
    const uint8_t* port = static_cast<const uint8_t*>(port_v);
    for (uint32_t o = 0; o < n; o += 4) {
        bool skipped = false;
        for (const auto& [a, b] : skip)
            if (o >= a && o < b) skipped = true;
        if (skipped) continue;
        uint32_t x = 0, y = 0;
        std::memcpy(&x, emu + o, std::min(4u, n - o));
        std::memcpy(&y, port + o, std::min(4u, n - o));
        if (n - o < 4) {
            const uint32_t mask = (1u << (8 * (n - o))) - 1;
            x &= mask;
            y &= mask;
        }
        if (x != y) {
            out = what + " +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
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

class CamEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = 0, heap_end = 0;
    uint32_t ini_size = 0;
    uint32_t strtok_last = 0;
    uint32_t target = 0;  // o que o WTargetPicker devolve

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
        m.hook(0x2a7f34, "memmove", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2a7e84, "memcpy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > heap_end) throw nf::MipsError("alocação: a área acabou", heap, n);
            if (n) std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x2d0910, "__builtin_new", alloc);
        m.hook(0x2d09c8, "operator_new", alloc);
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x2cad80, "MEM_free", [](MipsMachine&) {});
        auto cstr = [](MipsMachine& mm, uint32_t a) { return reinterpret_cast<const char*>(mm.mem(a, 1)); };
        m.hook(0x2a9760, "strlen", [cstr](MipsMachine& mm) {
            mm.set_gpr(R::v0, std::strlen(cstr(mm, uint32_t(mm.gpr(R::a0)))));
        });
        m.hook(0x2a9c10, "strstr", [cstr](MipsMachine& mm) {
            const uint32_t a = uint32_t(mm.gpr(R::a0));
            const char* h = cstr(mm, a);
            const char* p = std::strstr(h, cstr(mm, uint32_t(mm.gpr(R::a1))));
            mm.set_gpr(R::v0, p ? a + uint32_t(p - h) : 0);
        });
        m.hook(0x2a9198, "strcasecmp", [cstr](MipsMachine& mm) {
            const int r = drv::dafi_strcasecmp(cstr(mm, uint32_t(mm.gpr(R::a0))), cstr(mm, uint32_t(mm.gpr(R::a1))));
            mm.set_gpr(R::v0, uint64_t(int64_t(r)));
        });
        m.hook(0x2a9a50, "strncpy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            bool ended = false;
            for (uint32_t i = 0; i < n; ++i) {
                const uint8_t c = ended ? 0 : mm.mem(src + i, 1)[0];
                if (c == 0) ended = true;
                mm.mem(dst + i, 1)[0] = c;
            }
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2aabd8, "strtok", [this](MipsMachine& mm) {
            uint32_t s = uint32_t(mm.gpr(R::a0));
            const std::string delims(reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1)));
            if (s == 0) s = strtok_last;
            if (s == 0) {
                mm.set_gpr(R::v0, 0);
                return;
            }
            auto at = [&](uint32_t a) { return char(mm.mem(a, 1)[0]); };
            while (at(s) && delims.find(at(s)) != std::string::npos) ++s;
            if (at(s) == 0) {
                strtok_last = 0;
                mm.set_gpr(R::v0, 0);
                return;
            }
            uint32_t e = s;
            while (at(e) && delims.find(at(e)) == std::string::npos) ++e;
            if (at(e)) {
                mm.mem(e, 1)[0] = 0;
                strtok_last = e + 1;
            } else {
                strtok_last = 0;
            }
            mm.set_gpr(R::v0, s);
        });
        m.hook(0x2a7290, "atof", [cstr](MipsMachine& mm) {
            const double d = std::strtod(cstr(mm, uint32_t(mm.gpr(R::a0))), nullptr);
            uint64_t b;
            std::memcpy(&b, &d, 8);
            mm.set_gpr(R::v0, b);
        });
        m.hook(0x2a72b0, "atoi", [cstr](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint64_t(int64_t(drv::dafi_atoi(cstr(mm, uint32_t(mm.gpr(R::a0)))))));
        });
        m.hook(0x2a9098, "sprintf", [cstr](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0));
            const char* fmt = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const int regs[] = {R::a2, R::a3, R::t0, R::t1, R::t2, R::t3};
            int next = 0;
            std::string out;
            for (const char* p = fmt; *p; ++p) {
                if (*p != '%') {
                    out.push_back(*p);
                    continue;
                }
                ++p;
                if (*p == 's') out += cstr(mm, uint32_t(mm.gpr(regs[next++])));
                else if (*p == 'd') out += std::to_string(int32_t(mm.gpr(regs[next++])));
                else throw nf::MipsError("sprintf: formato não suportado", uint32_t(*p), 0);
            }
            std::memcpy(mm.mem(dst, uint32_t(out.size() + 1)), out.c_str(), out.size() + 1);
            mm.set_gpr(R::v0, out.size());
        });
        m.hook(0x1a43d8, "RCamera::RCamera", [](MipsMachine& mm) {
            const uint32_t c = uint32_t(mm.gpr(R::a0));
            const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            const float one[4] = {0, 0, 0, 1};
            mm.write32(c + 0xa8, 0x37d790);
            std::memcpy(mm.mem(c, 64), id, 64);
            std::memcpy(mm.mem(c + 0x60, 64), id, 64);
            std::memcpy(mm.mem(c + 0x40, 16), one, 16);
            const float fov = 33.0f;
            mm.write32(c + 0xa0, 1);
            std::memcpy(mm.mem(c + 0xa4, 4), &fov, 4);
            mm.write32(c + 0x50, 0);
            mm.set_gpr(R::v0, c);
        });
        m.hook(0x1ff3c8, "Simulation::GetScratchPadFreeZone", [](MipsMachine& mm) { mm.set_gpr(R::v0, kScratchpad); });
        m.hook(0x2d73c0, "UFileLoader::FileLoad", [](MipsMachine& mm) { mm.set_gpr(R::v0, kIniBuf); });
        m.hook(0x2cb218, "MEM_size", [this](MipsMachine& mm) { mm.set_gpr(R::v0, ini_size); });
        m.hook(0x2a8148, "printf", [](MipsMachine&) {});
        m.hook(0x165288, "ActionQueue::ActionQueue", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        m.hook(0x223338, "WRoadNav::WRoadNav", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        m.hook(0x2d5a10, "UGroup::DataLocateTag", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x5000); });
        m.hook(0x2d6940, "UGroup::GetArray", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x5000); });
        m.hook(0x1d8470, "RColorize::SetEnabled", [](MipsMachine&) {});
        m.hook(kFnCarType, "PBondCar::GetCarType (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kStrings); });
        m.hook(kFnSecondary, "PBondCar::GetSecondaryType (falso)",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, kStrings2); });
        m.hook(kFnZero, "uma entrada do carro que devolve zero (falsa)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnAudio, "PBondCar::GetAudio (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kAudio); });
        auto entry = [this](uint32_t off, uint32_t fn) {
            m.write32(kCarVt + off, 0);
            m.write32(kCarVt + off + 4, fn);
        };
        entry(0x40, kFnAudio);    // GetAudio
        entry(0x58, kFnZero);     // o AIVehicle
        entry(0x68, kFnZero);     // GetResetAvailable
        entry(0x80, kFnCarType);  // GetCarType
        entry(0x98, kFnZero);     // GetCarClass
        entry(0x290, kFnSecondary);  // GetSecondaryType
        // A libm do jogo: o nf::fm do porte nos dois lados.
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(0x2bfd28, "tanf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::tan(mm.fpr(12))); });
        m.hook(0x2c0618, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(0x2c0418, "acosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::acos(mm.fpr(12))); });
        m.hook(0x2c0518, "asinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::asin(mm.fpr(12))); });
        m.hook(0x10c6c8, "o printf de depuração dos eventos", [](MipsMachine&) {});
        m.hook(0x2065d0, "SMissionManager::SetCinematicMode (o +0x3cc)", [](MipsMachine& mm) {
            const uint32_t mgr = uint32_t(mm.gpr(R::a0)), mode = uint32_t(mm.gpr(R::a1));
            if (mode != 0) mm.write32(mgr + 0x3cc, mm.read32(mgr + 0x3cc) | mode);
            else mm.write32(mgr + 0x3cc, 0x80000000u);  // (o ClearCinematicMode: não pedido por estes eventos)
        });
        m.hook(0x1664c0, "InputConfigManager::Get (o POV)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kIcm); });
        m.hook(0x22af38, "WTargetPicker::GetSelectedTarget", [this](MipsMachine& mm) { mm.set_gpr(R::v0, target); });
        m.hook(0x12e6f8, "AIGroundVehicle::SetAttackMode", [](MipsMachine&) {});
        m.hook(0x12e468, "AIGroundVehicle::SetNavigateMode", [](MipsMachine&) {});
        // O RAnimEngine::Handle de um registro 'Cams' sem animação (o mesmo que o porte monta).
        m.hook(0x1a1bf0, "RAnimEngine::Handle::Create (registro sem animação)", [this](MipsMachine& mm) {
            const uint32_t rec = uint32_t(mm.gpr(R::a2));
            const uint32_t h = heap;
            heap += 0x80;
            std::memset(mm.mem(h, 0x80), 0, 0x80);
            mm.write32(h, 0x40);
            mm.mem(h + 0x1c, 1)[0] = 1;
            std::memcpy(mm.mem(h + 0x40, 0x40), mm.mem(rec, 0x40), 0x40);
            uint8_t* f = mm.mem(h + 0x4c, 4);
            if (f[0] & 0x10) {
                f[1] = 0xff;
                f[2] = 0;
                f[3] = 0;
                f[0] = uint8_t(f[0] & 0xef);
            }
            if (mm.read32(rec + 0x1c) != 0) throw nf::MipsError("um registro 'Cams' com animação", rec, 0);
            mm.set_gpr(R::v0, h);
        });
        m.hook(0x1a2558, "RAnimEngine::Handle::~Handle", [](MipsMachine&) {});
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0.0f, float f13 = 0.0f) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.set_fpr(12, f12);
        m.set_fpr(13, f13);
        m.call(fn, args, 200'000'000);
    }
    void put_f(uint32_t a, float f) { std::memcpy(m.mem(a, 4), &f, 4); }
};

// A âncora andando (como na parte 8).
struct Pose {
    double x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0, speed = 0, yaw_rate = 0, pitch_rate = 0;
};

void put_pose(const Pose& p, drv::RigidBody* rb, drv::RigidBodyInfo* info) {
    const double cy = std::cos(p.yaw), sy = std::sin(p.yaw), cp = std::cos(p.pitch), sp = std::sin(p.pitch);
    const double cr = std::cos(p.roll), sr = std::sin(p.roll);
    const double fwd[3] = {sy * cp, sp, cy * cp};
    const double right0[3] = {cy, 0, -sy};
    const double up0[3] = {-sy * sp, cp, -cy * sp};
    double right[3], up[3];
    for (int i = 0; i < 3; ++i) {
        right[i] = right0[i] * cr + up0[i] * sr;
        up[i] = up0[i] * cr - right0[i] * sr;
    }
    for (int i = 0; i < 3; ++i) {
        info->orient[0][i] = float(right[i]);
        info->orient[1][i] = float(up[i]);
        info->orient[2][i] = float(fwd[i]);
        info->orient[3][i] = 0.0f;
    }
    info->orient[0][3] = info->orient[1][3] = info->orient[2][3] = 0.0f;
    info->orient[3][3] = 1.0f;
    rb->pos[0] = float(p.x);
    rb->pos[1] = float(p.y);
    rb->pos[2] = float(p.z);
    for (int i = 0; i < 3; ++i) rb->vel[i] = float(fwd[i] * p.speed);
    rb->quat[0] = rb->quat[1] = rb->quat[2] = 0.0f;
    rb->quat[3] = 1.0f;
}

}  // namespace

int run_heli_camera_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                           uint32_t vu0_size, int frames, uint32_t seed) {
    int failures = 0;
    CamEmu* e = new CamEmu();
    drv::CameraTables* tables = new drv::CameraTables();
    drv::PlayerCamera* cam = new drv::PlayerCamera();
    drv::CameraAnchor* anchor = new drv::CameraAnchor();
    try {
        e->setup(elf, vu0_program, vu0_size);
        std::string error;
        drv::Bigf arc;
        std::vector<uint8_t> ini_bytes;
        int idx = -1;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || (idx = arc.find("data\\render\\camera.ini")) < 0 ||
            !arc.read(idx, ini_bytes)) {
            std::printf("  o camera.ini do MIS01.VIV não abriu\n");
            delete e;
            return failures + 1;
        }
        drv::Track track;
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
        MipsMachine& m = e->m;
        if (!copy_world(m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  %s\n", error.c_str());
            delete e;
            return failures + 1;
        }
        e->heap = (kWorldBegin + wc.bytes + 0xfff) & ~0xfffu;
        e->heap_end = kWorldEnd;
        e->ini_size = uint32_t(ini_bytes.size());
        put(m, kIniBuf, ini_bytes.data(), uint32_t(ini_bytes.size()));

        // Os 'Cams' e os 'AISp' de Paris (com os 'pt' que os trilhos usam) na RAM emulada.
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        std::vector<uint8_t*> cams_port;
        std::vector<uint32_t> cams_emu;
        {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x43616d73u);  // 'Cams'
            if (d != drv::UGroup_DataEnd(map)) {
                uint8_t* c = reinterpret_cast<uint8_t*>(drv::UData_Data(d));
                const uint32_t n = drv::UData_Size(d) / 0x40;
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t anim = 0;
                    std::memcpy(&anim, c + 0x40 * i + 0x1c, 4);
                    if (anim != 0) continue;  // as animadas (o RAnimEngine): não portadas
                    cams_port.push_back(c + 0x40 * i);
                    cams_emu.push_back(kCams + 0x40 * i);
                    put(m, kCams + 0x40 * i, c + 0x40 * i, 0x40);
                }
            }
        }
        std::map<uint32_t, uint32_t> path_emu;
        std::vector<const drv::AISplineRec*> splines_port;
        std::vector<uint32_t> splines_emu;
        {
            uint32_t at = kPaths;
            for (uint32_t i = 1; i < 0x100; ++i) {
                drv::UData* d = drv::UGroup_DataLocateTag(map, 0x70740000u | i);
                if (d == drv::UGroup_DataEnd(map)) continue;
                const uint8_t* p = reinterpret_cast<const uint8_t*>(drv::UData_Data(d));
                const uint32_t n = drv::UData_Size(d);
                put(m, at, p, n);
                path_emu[nf::p32_u(p)] = at;
                at = (at + n + 15) & ~15u;
            }
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x41495370u);  // 'AISp'
            if (d != drv::UGroup_DataEnd(map)) {
                const auto* s = reinterpret_cast<const drv::AISplineRec*>(drv::UData_Data(d));
                for (uint32_t i = 0; i < d->count; ++i) {
                    if (s[i].path == 0 || !path_emu.count(s[i].path)) continue;
                    drv::AISplineRec se = s[i];
                    se.path = path_emu.at(s[i].path);
                    const uint32_t a = kSplines + uint32_t(sizeof(drv::AISplineRec)) * uint32_t(splines_emu.size());
                    put(m, a, &se, sizeof se);
                    splines_port.push_back(&s[i]);
                    splines_emu.push_back(a);
                }
            }
        }
        std::printf("  câmera: %zu registros 'Cams' sem animação, %zu trilhos ('AISp')\n", cams_port.size(),
                    splines_port.size());
        const drv::PathEngineClock& clk = drv::path_engine_clock();
        e->put_f(kRate, clk.rate);
        e->put_f(kFrequency, clk.frequency);
        drv::RPathEngine_Purge();
        m.write32(kPathList, 0);

        // O carro: a âncora, a tabela virtual, a lista dos carros e o gerente de armas.
        std::memset(m.mem(kCar, 0x100), 0, 0x100);
        m.write32(kCar + 0x40, 1);
        m.write32(kCar + 0x44, 0);
        m.write32(kCar + 0x48, kRender);
        m.write32(kRender + 0x0c, kRender + 0x40);
        m.write32(kRender + 0x40, kRender + 0x80);
        m.write32(kCar + 0x68, kCarVt);
        m.write32(kCarsVec, kCar);
        m.write32(kSim + 0xdcc, kCarsVec);
        m.write32(kSim + 0xdd0, kCarsVec + 4);
        m.write32(kSim + 0xdd4, kCarsVec + 4);
        m.write32(kSim + 0xe2c, 0);
        m.write32(kSim + 0xe30, 0);
        for (int i = 0; i < 96; ++i) m.write32(kSim + 0x330 + 4u * uint32_t(i), 0);
        m.write32(kSim + 0x04, 0);
        m.write32(kSim + 0x1c, kBodies);
        m.write32(kSim + 0x24, kInfos);
        m.write32(kOverrideCar, 0);
        m.write32(kWeaponMgrPtr, kWeaponMgr);
        m.write32(kWeaponMgr + 4, 0xffffffffu);
        m.write32(kWeaponMgr + 0x10, kWeaponSlots);
        std::memset(m.mem(kWeaponSlots, 32 * 0x54), 0, 32 * 0x54);
        std::memset(m.mem(kMission, 0x600), 0, 0x600);
        m.write32(kMissionPtr, kMission);
        m.write32(kRendererPtr, kRendererObj);
        m.write32(kRendererObj + 0x50, 640);
        m.write32(kRendererObj + 0x54, 448);
        m.write32(kIcm + 4, 1);
        std::memset(m.mem(kAudio, 0x100), 0, 0x100);
        std::memset(m.mem(kUber + 0x4f4, 0x20), 0, 0x20);
        std::memcpy(m.mem(kUber + 0x4f4, 12), "paris_mis01", 12);
        m.write32(kUber + 0xa40, 1);
        m.write32(kUber + 0xa44, 0);
        m.write32(kZoomButton, 0);

        // As tabelas: "helicopter" com o tipo secundário "vanquish".
        std::memset(m.mem(kStrings, 0x80), 0, 0x80);
        std::memcpy(m.mem(kStrings, 0x40), "helicopter", 10);
        std::memcpy(m.mem(kStrings2, 0x40), "vanquish", 8);
        m.write32(kTables + 0x0c, 0);
        e->call(kFnLoadFile, {0});
        drv::IniFile ini;
        ini.open(ini_bytes);
        drv::CameraCarInfo car;
        car.type = "helicopter";
        car.secondary = "vanquish";
        tables->load(ini, car);
        const drv::CameraTableGlobals& g = tables->tables;
        if (g.autodrive_index < 0 || g.world_anim_index < 0 || g.ai_path_anim_index < 0) {
            std::printf("  as tabelas do helicóptero não têm os modos (AutoDrive %d, WorldAnim %d, AIPathAnim %d)\n",
                        int(g.autodrive_index), int(g.world_anim_index), int(g.ai_path_anim_index));
            delete e;
            return failures + 1;
        }
        drv::CameraModeGlobals& mg = tables->modes;
        const uint32_t arms_emu = m.read32(kModeGlobals + 0x48);
        uint32_t arms_emu_cur = arms_emu;
        // Os campos dos braços que o LoadFile deixa com o lixo da pilha (escritos depois em jogo): os do porte nos dois.
        for (int32_t k = 0; k < mg.autodrive_arm_count; ++k) {
            drv::AutoDriveArmInfo& a = nf::raw(mg.autodrive_arms)[k];
            std::memcpy(m.mem(arms_emu + 0x100 * uint32_t(k) + 0x6c, 0x10), &a.rest_span, 0x10);
            std::memcpy(m.mem(arms_emu + 0x100 * uint32_t(k) + 0xfc, 4), &a.rest_pitch, 4);
        }
        std::printf("  câmera: LoadFile (helicopter + vanquish): %d modos, AutoDrive %d (%d braços), WorldAnim %d, "
                    "AIPathAnim %d\n",
                    int(g.mode_count), int(g.autodrive_index), int(mg.autodrive_arm_count), int(g.world_anim_index),
                    int(g.ai_path_anim_index));
        {
            Tally t;
            ++t.cases;
            std::string out;
            if (!same_words("os modos", m.mem(m.read32(kTables + 0x0c), 0x1c * uint32_t(g.mode_count + 1)),
                            nf::raw(g.modes), 0x1c * uint32_t(g.mode_count + 1), {}, out) ||
                !same_words("as constantes", m.mem(kConstants, 0x80), &tables->constants, 0x80,
                            {{0x34, 0x40}, {0x4c, 0x50}}, out) ||
                !same_words("os globais do AutoDrive", m.mem(kModeGlobals + 0x38, 0x10),
                            reinterpret_cast<const uint8_t*>(&mg) + 0x38, 0x10, {}, out))
                t.fail(out);
            for (int32_t k = 0; t.bad == 0 && k < mg.autodrive_arm_count; ++k)
                if (!same_words("braço " + std::to_string(k), m.mem(arms_emu + 0x100 * uint32_t(k), 0x100),
                                nf::raw(mg.autodrive_arms) + k, 0x100, {{0x3c, 0x40}}, out))
                    t.fail(out);
            // As tabelas Heli com os braços (como na parte 8).
            const uint32_t helis = m.read32(kTables + 0x08);
            if (t.bad == 0 && m.read32(kTables + 0x3c) != uint32_t(g.heli_count))
                t.fail("a contagem Heli: original " + std::to_string(m.read32(kTables + 0x3c)) + ", porte " +
                       std::to_string(g.heli_count));
            for (int32_t i = 0; t.bad == 0 && i < g.heli_count; ++i) {
                if (!same_words("Heli " + std::to_string(i), m.mem(helis + 0x4c * uint32_t(i), 0x4c),
                                &tables->heli(i), 0x4c, {{0x2c, 0x30}}, out)) {
                    t.fail(out);
                    break;
                }
                const drv::HeliCamInfo& h = tables->heli(i);
                const uint32_t arms = m.read32(helis + 0x4c * uint32_t(i) + 0x2c);
                for (int32_t k = 0; t.bad == 0 && k < h.arm_count; ++k)
                    if (!same_words("Heli " + std::to_string(i) + " braço " + std::to_string(k),
                                    m.mem(arms + 0x40 * uint32_t(k), 0x40), nf::raw(h.arms) + k, 0x40,
                                    {{0x24, 0x30}, {0x3c, 0x40}}, out))
                        t.fail(out);
            }
            report("LoadFile (helicopter + vanquish: os modos e os braços)", t, failures);
        }

        drv::Simulation& sim = drv::simulation();
        drv::simulation_reset(60);
        // O ruído: as tabelas do porte (iniciadas uma vez; a parte 8 as confere contra o Noise::Init original) na RAM
        // emulada, já que um segundo Noise::Init de cada lado partiria de estados diferentes do gerador.
        (void)drv::Noise_Noise1(0.0f);
        std::memcpy(m.mem(kNoisePerm, 0x2000), drv::noise_perm_table(), 0x2000);
        std::memcpy(m.mem(kNoiseGrad, 0x10b * 4), drv::noise_grad_table(), 0x10b * 4);
        drv::RigidBody* rb = drv::GetRigidBody(sim, 0);
        *rb = drv::RigidBody{};
        rb->info = drv::GetRigidBodyInfo(sim, 0);
        drv::RigidBodyInfo* info = nf::raw(rb->info);
        *info = drv::RigidBodyInfo{};
        info->half_size[0] = 0.95f;
        info->half_size[1] = 0.62f;
        info->half_size[2] = 2.25f;
        anchor->kind = 1;
        anchor->flags = 0;
        anchor->body = 0;
        drv::CameraContext ctx;
        ctx.sim = &sim;
        ctx.world = &tw;
        ctx.cars = {anchor};
        ctx.track = "paris_mis01";
        ctx.uber_a40 = 1;
        int32_t audio_port[2] = {0, 0};
        ctx.audio_turn = audio_port;
        // As armas do gerente: os números que as máscaras dos braços pedem (e um que nenhum pede).
        std::vector<int32_t> weapon_numbers;
        for (int32_t k = 0; k < mg.autodrive_arm_count; ++k)
            for (int w = 0; w < 64; ++w)
                if (nf::raw(mg.autodrive_arms)[k].weapons[w >> 5] & (1u << (w & 31))) weapon_numbers.push_back(w);
        weapon_numbers.push_back(63);
        ctx.weapons.resize(4);
        for (size_t i = 0; i < ctx.weapons.size(); ++i) {
            ctx.weapons[i].weapon = weapon_numbers[i % weapon_numbers.size()];
            m.write32(kWeaponSlots + 0x54 * uint32_t(i), uint32_t(ctx.weapons[i].weapon));
        }

        std::vector<int16_t> heli_ids;
        for (int32_t i = 0; i < g.mode_count; ++i)
            if (tables->mode(i).type == drv::kCamHeli && tables->mode(i).cam_id != 0)
                heli_ids.push_back(tables->mode(i).cam_id);
        if (heli_ids.empty()) heli_ids.push_back(0);
        Pose pose;
        Rng rng(seed ^ 0x4e11);
        auto place = [&]() {
            const drv::WCollisionInstance& ci = tw.instances()[rng.next() % tw.instance_count()];
            float c[3];
            drv::WCollisionInstance_CalcPosition(tw.mgr(), ci, c);
            pose.x = c[0] + rng.uni(-20, 20);
            pose.y = c[1] + rng.uni(2.0f, 12.0f);
            pose.z = c[2] + rng.uni(-20, 20);
            pose.yaw = rng.uni(-3.14159f, 3.14159f);
            pose.pitch = rng.chance(0.5) ? rng.uni(-0.25f, 0.25f) : 0.0;
            pose.roll = rng.chance(0.3) ? rng.uni(-0.12f, 0.12f) : 0.0;
            pose.speed = rng.uni(0.0f, 30.0f);
            pose.yaw_rate = rng.uni(-0.6f, 0.6f);
            pose.pitch_rate = rng.uni(-0.05f, 0.05f);
        };
        place();
        auto put_body = [&]() {
            drv::RigidBody b = *rb;
            std::memcpy(m.mem(kBodies, 0x80), &b, 0x80);
            m.write32(kBodies + 0x5c, kInfos);
            drv::RigidBodyInfo bi = *info;
            std::memcpy(m.mem(kInfos, 0x500), &bi, 0x500);
            for (int i = 0; i < 16; ++i) m.write32(kInfos + 0x380 + 4u * uint32_t(i), 0);
            m.write32(kInfos + 0x4d8, 0);
        };
        auto put_sim = [&]() {
            m.write32(kSim + 0xdb4, uint32_t(sim.state));
            m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
            std::memcpy(m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
            m.write32(kSim + 0xdc4, uint32_t(sim.steps));
            const drv::WCollisionMgr& wm = tw.mgr();
            m.write32(wc.mgr + 4, wm.query_stamp);
            m.write32(wc.mgr + 0x2c, wm.barrier_mask);
            m.write32(wc.mgr + 0x30, wm.vu0_path);
            for (uint32_t i = 0; i < wc.count; ++i) m.write32(wc.insts + 0x40 * i + 0x10, tw.instances()[i].query_stamp);
        };
        auto put_mission = [&]() {
            m.write32(kMission + 0x34c, uint32_t(ctx.mission_state));
            m.write32(kMission + 0x350, uint32_t(ctx.mission_auto_drive));
            m.write32(kMission + 0x358, ctx.mission_missile);
            m.write32(kMission + 0x3b8, uint32_t(ctx.mission_difficulty));
            m.write32(kMission + 0x3cc, ctx.mission_cinematic);
            m.write32(kMission + 0x5ec, uint32_t(ctx.mission_5ec));
            m.write32(kUber + 0xa40, uint32_t(ctx.uber_a40));
            m.write32(kUber + 0xa44, uint32_t(ctx.uber_a44));
            std::memset(m.mem(kUber + 0x4f4, 0x20), 0, 0x20);
            std::memcpy(m.mem(kUber + 0x4f4, uint32_t(ctx.track.size())), ctx.track.data(), ctx.track.size());
            m.write32(kWeaponMgr + 4, uint32_t(ctx.current_weapon));
        };
        sim.state = 0;
        sim.steps = 0;
        put_pose(pose, rb, info);
        put_body();
        put_sim();
        put_mission();
        std::memset(m.mem(kCam, 0x370), 0, 0x370);
        e->call(kFnCameraCtor, {kCam});
        cam->construct(*tables, ctx);

        // A comparação de tudo o que a câmera escreve.
        auto compare = [&](std::string& out) -> bool {
            if (!same_words("RPlayerCamera", m.mem(kCam, 0x370), &cam->d, 0x370,
                            {{0xa8, 0xac}, {0x104, 0x108}, {0x110, 0x11c}, {0x140, 0x144}, {0x2fc, 0x300},
                             {0x350, 0x370}},
                            out))
                return false;
            if ((m.read32(kCam + 0x104) != 0) != (cam->d.anim_handle != 0)) {
                out = "RPlayerCamera +0x104 (o RAnimEngine::Handle)";
                return false;
            }
            if ((m.read32(kCam + 0x110) != 0) != (cam->d.ai_spline != 0)) {
                out = "RPlayerCamera +0x110 (o AISplinePath)";
                return false;
            }
            const uint32_t lt = m.read32(kCam + 0x140);
            const uint32_t want = cam->d.lock_target == 0 ? 0 : kLockPoint;
            if (lt != want) {
                out = "RPlayerCamera +0x140 (o ponto preso): original " + hex(lt) + ", porte " + hex(want);
                return false;
            }
            const uint32_t st = m.read32(kCam + 0x354);
            if (!same_words("RPlayerCamState", m.mem(st, 0x5c), &cam->state, 0x5c, {{0, 4}}, out)) return false;
            if (!same_words("o olho do tremor (0x333a20)", m.mem(kShakeEye, 16), cam->statics.shake_eye, 16, {}, out))
                return false;
            if (!same_words("os giros anteriores (0x333a30)", m.mem(kPrevRates, 8), cam->statics.prev_rates, 8, {}, out))
                return false;
            if (m.read32(kZoomButton) != uint32_t(cam->statics.zoom_button)) {
                out = "AICharacterHands::fgZoomButton (0x3276e0)";
                return false;
            }
            if (!same_words("os globais 0x3339d0", m.mem(kTables + 0x20, 0x1c),
                            reinterpret_cast<const uint8_t*>(&g) + 0x20, 0x1c, {}, out))
                return false;
            for (int32_t k = 0; k < mg.autodrive_arm_count; ++k)
                if (!same_words("braço " + std::to_string(k), m.mem(arms_emu_cur + 0x100 * uint32_t(k), 0x100),
                                nf::raw(mg.autodrive_arms) + k, 0x100, {{0x3c, 0x40}}, out))
                    return false;
            if (m.read32(kAudio + 0xb8) != uint32_t(audio_port[0]) || m.read32(kAudio + 0xbc) != uint32_t(audio_port[1])) {
                out = "o som do carro (+0xb8/+0xbc)";
                return false;
            }
            // A fila do diretor.
            const uint32_t dq = m.read32(kCam + 0x368);
            const uint32_t head = m.read32(dq);
            std::vector<uint32_t> nodes;
            for (uint32_t n = m.read32(head); n != head && nodes.size() < 64; n = m.read32(n)) nodes.push_back(n);
            if (nodes.size() != cam->director_queue.size()) {
                out = "a fila do diretor: original " + std::to_string(nodes.size()) + ", porte " +
                      std::to_string(cam->director_queue.size()) + " (original:";
                for (uint32_t n : nodes)
                    out += " modo " + std::to_string(m.read32(n + 8 + 0x10) >> 16) + " espera " +
                           std::to_string(m.read32(n + 8 + 0x10) & 0xffff) + " bandeiras " +
                           std::to_string(m.read32(n + 8 + 0x14) >> 16);
                out += "; bandeiras " + std::to_string(m.mem(dq + 8, 1)[0]) + "/" + std::to_string(cam->director_flags);
                out += "; modo " + std::to_string(m.read32(kCam + 0x120)) + "/" + std::to_string(cam->d.mode) + ")";
                return false;
            }
            for (size_t k = 0; k < nodes.size(); ++k) {
                const drv::DirectorQueueData& q = cam->director_queue[k];
                if (!same_words("a fila do diretor " + std::to_string(k), m.mem(nodes[k] + 8, 0x18), &q, 0x18, {}, out))
                    return false;
                // +0x18 (o registro 'Cams') e +0x1c (o 'AISp'): pelo endereço emulado do mesmo registro.
                auto map_ptr = [&](uint32_t p) -> uint32_t {
                    for (size_t i = 0; i < cams_port.size(); ++i)
                        if (nf::p32_u(cams_port[i]) == p) return cams_emu[i];
                    for (size_t i = 0; i < splines_port.size(); ++i)
                        if (nf::p32_u(splines_port[i]) == p) return splines_emu[i];
                    return p;
                };
                if (m.read32(nodes[k] + 8 + 0x18) != map_ptr(q.data18) ||
                    m.read32(nodes[k] + 8 + 0x1c) != map_ptr(q.data1c)) {
                    out = "a fila do diretor " + std::to_string(k) + " (os registros)";
                    return false;
                }
            }
            if (m.mem(dq + 8, 1)[0] != cam->director_flags) {
                out = "as bandeiras do diretor";
                return false;
            }
            // O RCameraSpline.
            const uint32_t sp = m.read32(kCam + 0x350);
            if (!same_words("o RCameraSpline", m.mem(sp, 0x64), &cam->spline->d, 0x64, {{0x3c, 0x40}}, out)) return false;
            std::vector<uint32_t> pts;
            const uint32_t lh = m.read32(sp + 0x64);
            for (uint32_t n = m.read32(lh); n != lh && pts.size() < 256; n = m.read32(n)) pts.push_back(n);
            if (pts.size() != cam->spline->points.size()) {
                out = "a lista do RCameraSpline: original " + std::to_string(pts.size()) + ", porte " +
                      std::to_string(cam->spline->points.size());
                return false;
            }
            for (size_t k = 0; k < pts.size(); ++k)
                if (!same_words("o ponto " + std::to_string(k) + " do RCameraSpline", m.mem(pts[k] + 8, 16),
                                cam->spline->points[k].data(), 16, {}, out))
                    return false;
            // Os registros 'Cams' (a distância que o TriggerAnimationCamera grava em +0x2c).
            for (size_t i = 0; i < cams_port.size(); ++i)
                if (!same_words("o 'Cams' " + std::to_string(i), m.mem(cams_emu[i], 0x40), cams_port[i], 0x40, {}, out))
                    return false;
            return true;
        };
        Tally t_ctor;
        {
            ++t_ctor.cases;
            std::string out;
            if (!compare(out)) t_ctor.fail(out);
        }
        report("RPlayerCamera::RPlayerCamera (helicopter)", t_ctor, failures);

        // Os quadros (uma passada com os braços do disco, outra com braços sorteados: dois braços com a zona morta, o
        // repouso, a troca de transformação e o braço preso ao carro, que o helicóptero de Paris não liga).
        // O ponto preso e o alvo vivem entre as passadas (a câmera guarda os ponteiros de uma para a outra).
        float lock_point[4] = {0, 0, 0, 1};
        float target_point[4] = {0, 0, 0, 1};
        auto run_pass = [&](const char* label, int nframes) {
        Tally t_frames, t_events;
        uint64_t n_events[6] = {};
        uint64_t n_autodrive = 0, n_input = 0, n_lock = 0, n_anim = 0, n_aipath = 0, n_end = 0, n_shake = 0, n_force = 0,
                 n_last = 0, n_target = 0, n_spin = 0, n_weapon = 0, n_fired = 0, n_pause = 0;
        uint64_t f_autodrive = 0, f_anim = 0, f_aipath = 0, f_limited = 0, f_lock = 0, f_dead = 0, f_weap_change = 0;
        const uint64_t pending_before = drv::camera_pending();
        std::string prev_log;
        for (int f = 0; f < nframes; ++f) {
            // A âncora anda.
            const double dt = 1.0 / 60.0;
            pose.yaw += pose.yaw_rate * dt;
            pose.pitch = std::clamp(pose.pitch + pose.pitch_rate * dt, -0.35, 0.35);
            const double cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pose.pitch);
            pose.x += sy * cp * pose.speed * dt;
            pose.y += std::sin(pose.pitch) * pose.speed * dt;
            pose.z += cy * cp * pose.speed * dt;
            if (rng.chance(0.01)) place();
            put_pose(pose, rb, info);
            if (rng.chance(0.97)) ++sim.steps;
            if (sim.state == 3 ? rng.chance(0.25) : rng.chance(0.01)) {
                sim.state = sim.state == 3 ? 0 : 3;
                if (sim.state == 3) ++n_pause;
            }
            // O mundo de fora: a missão, as opções, o alvo, a arma.
            if (rng.chance(0.01)) ctx.mission_state = rng.chance(0.2) ? 4 : 1;
            if (rng.chance(0.005)) ctx.mission_missile = rng.chance(0.3) ? 0x1234 : 0;
            if (rng.chance(0.02)) ctx.mission_auto_drive = rng.chance(0.8) ? 1 : 0;
            if (rng.chance(0.01)) ctx.mission_difficulty = rng.range(0, 2);
            if (rng.chance(0.01)) ctx.mission_cinematic = rng.chance(0.5) ? 1u : 0u;
            if (rng.chance(0.01)) ctx.mission_5ec = rng.chance(0.5) ? 1 : 0;
            if (rng.chance(0.01)) ctx.uber_a40 = rng.chance(0.7) ? 1 : 0;
            if (rng.chance(0.01)) ctx.uber_a44 = rng.chance(0.5) ? 1 : 0;
            if (rng.chance(0.004)) {
                ctx.track = rng.chance(0.5) ? "snow1a_mis3" : "paris_mis01";
            }
            if (rng.chance(0.02)) {
                ctx.current_weapon = rng.chance(0.2) ? -1 : rng.range(0, int(ctx.weapons.size()) - 1);
                ++n_weapon;
            }
            if (rng.chance(0.015)) {
                // O zoom da mira e o +0x30 do RPlayerCamState (escritos fora da câmera).
                cam->state.zoom_held = rng.chance(0.5) ? 1u : 0u;
                cam->state.unknown30 = rng.chance(0.3) ? 1u : 0u;
                const uint32_t st = m.read32(kCam + 0x354);
                m.write32(st + 0x24, cam->state.zoom_held);
                m.write32(st + 0x30, cam->state.unknown30);
            }
            // O alvo do WTargetPicker: um ponto perto da câmera (dentro do alcance da mira automática, às vezes).
            if (rng.chance(0.03)) {
                if (rng.chance(0.3)) {
                    ctx.target = nullptr;
                    e->target = 0;
                } else {
                    for (int k = 0; k < 3; ++k) target_point[k] = cam->d.eye[k] + rng.uni(-60.0f, 60.0f);
                    target_point[1] = cam->d.eye[1] + rng.uni(-10.0f, 3.0f);
                    std::memcpy(m.mem(kTarget, 16), target_point, 16);
                    ctx.target = target_point;
                    e->target = kTarget;
                    ++n_target;
                }
            }
            if (rng.chance(0.03)) {
                for (int k = 0; k < 3; ++k) lock_point[k] = float(k == 1 ? pose.y : (k == 0 ? pose.x : pose.z)) + rng.uni(-40.0f, 40.0f);
                std::memcpy(m.mem(kLockPoint, 16), lock_point, 16);
            }
            put_body();
            put_sim();
            put_mission();
            m.write32(kAudio + 0xb8, uint32_t(audio_port[0]));
            m.write32(kAudio + 0xbc, uint32_t(audio_port[1]));
            // Os pedidos (o registro do quadro vai para a mensagem de uma diferença).
            std::string flog;
            if (prev_log.size() > 600) prev_log.erase(0, prev_log.size() - 600);
            if (rng.chance(0.02)) {
                const float yaw = rng.uni(-0.5f, 0.5f), pitch = rng.uni(-0.25f, 0.25f);
                const int32_t rel = rng.chance(0.7) ? 1 : 0;
                e->call(kFnSetAutoDrive, {kCam, uint32_t(rel)}, yaw, pitch);
                cam->SetAutoDriveCamera(ctx, yaw, pitch, rel);
                ++n_autodrive;
                flog += " SetAutoDriveCamera";
            }
            const int k_inputs = rng.chance(0.4) ? rng.range(1, 3) : 0;
            for (int k = 0; k < k_inputs; ++k) {
                static const int32_t kActions[] = {0x21, 0x22, 0x23, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
                                                   0x33, 0x34, 0x35, 0x36, 0x38, 0x3a, 0x24, 0x37};
                const int32_t act = kActions[rng.next() % (sizeof kActions / sizeof kActions[0])];
                float v = rng.chance(0.2) ? 0.0f : rng.uni(-1.0f, 1.0f);
                if (rng.chance(0.1)) v = rng.chance(0.5) ? 1.0f : -1.0f;
                e->call(kFnInput, {kCam, uint32_t(act)}, v);
                cam->CameraInputCallback(ctx, act, v, 1);
                flog += " entrada 0x" + hex(uint32_t(act)).substr(6);
                if (act == 0x32) ++n_spin;
                ++n_input;
            }
            if (rng.chance(0.015)) {
                float off[4] = {rng.uni(-3.0f, 3.0f), rng.uni(-2.0f, 2.0f), rng.uni(-3.0f, 3.0f), 0.0f};
                std::memcpy(m.mem(kScratchpad + 0x200, 16), off, 16);
                const uint32_t frames_n = uint32_t(rng.range(0, 40));
                const int32_t keep = rng.chance(0.3) ? 1 : 0;
                const bool on = rng.chance(0.7);
                e->call(kFnLockOn, {kCam, on ? kLockPoint : 0u, kScratchpad + 0x200, frames_n, uint32_t(keep)});
                cam->CameraLockOn(on ? lock_point : nullptr, off, frames_n, keep);
                ++n_lock;
                flog += " CameraLockOn";
            }
            if (rng.chance(0.01)) {
                e->call(kFnWeaponFired, {kCam, 0x1c});
                cam->WeaponFired(ctx, 0x1c);
                ++n_fired;
                flog += " WeaponFired";
            }
            if (!cams_port.empty() && rng.chance(0.012)) {
                const size_t i = rng.next() % cams_port.size();
                const uint16_t transition = uint16_t(rng.range(1, 90));
                const uint16_t delay = uint16_t(rng.chance(0.5) ? 0 : rng.range(1, 20));
                const int32_t distribute = rng.chance(0.5) ? 1 : 0;
                const int32_t fov = rng.chance(0.5) ? 0 : rng.range(10, 70);
                e->call(kFnTriggerAnim, {kCam, cams_emu[i], transition, delay, uint32_t(distribute), uint32_t(fov)});
                cam->TriggerAnimationCamera(ctx, cams_port[i], transition, delay, distribute, fov);
                ++n_anim;
                flog += " TriggerAnimationCamera";
            }
            if (!splines_port.empty() && rng.chance(0.01)) {
                const size_t i = rng.next() % splines_port.size();
                const uint16_t transition = uint16_t(rng.range(1, 60));
                const uint16_t delay = uint16_t(rng.chance(0.5) ? 0 : rng.range(1, 10));
                const int32_t fov = rng.chance(0.4) ? 0 : rng.range(-5, 70);
                e->call(kFnTriggerAIPath, {kCam, splines_emu[i], transition, delay, uint32_t(fov)});
                cam->TriggerAIPathAnimationCamera(ctx, nf::p32_u(splines_port[i]), transition, delay, fov);
                ++n_aipath;
                flog += " TriggerAIPathAnimationCamera";
            }
            if (rng.chance(0.01)) {
                const uint16_t delay = uint16_t(rng.range(0, 5));
                const uint32_t transition = uint32_t(rng.range(0, 1));  // (o SetOldCameraMode com transição: não portado)
                const int16_t id = rng.chance(0.5) ? int16_t(0) : heli_ids[rng.next() % heli_ids.size()];
                e->call(kFnEndAnim, {kCam, delay, transition, 0, uint32_t(int32_t(id))});
                cam->EndCameraAnim(ctx, delay, transition, 0, id);
                ++n_end;
                flog += " EndCameraAnim(" + std::to_string(id) + ")";
            }
            if (rng.chance(0.006)) {
                const uint16_t delay = uint16_t(rng.range(0, 5));
                const uint32_t transition = uint32_t(rng.chance(0.5) ? rng.range(0, 1) : 0);
                const int32_t keep = rng.chance(0.5) ? 1 : 0;
                e->call(kFnLastSel, {kCam, delay, transition, 0, uint32_t(keep)});
                cam->SetLastSelectableCameraMode(delay, transition, 0, keep);
                ++n_last;
                flog += " SetLastSelectable";
            }
            if (rng.chance(0.006)) {
                const int16_t id = rng.chance(0.3) ? int16_t(0) : heli_ids[rng.next() % heli_ids.size()];
                const uint16_t delay = uint16_t(rng.range(0, 5));
                const uint32_t transition = uint32_t(rng.chance(0.7) ? 0 : 1);
                e->call(kFnForce, {kCam, uint32_t(int32_t(id)), delay, transition});
                cam->ForceCameraChange(id, delay, transition);
                ++n_force;
                flog += " ForceCameraChange(" + std::to_string(id) + ")";
            }
            if (rng.chance(0.01)) {
                const float radius = rng.uni(0.0f, 200.0f);
                const int32_t frames_n = rng.range(0, 120);
                const int32_t hold = rng.chance(0.4) ? 1 : 0;
                e->call(kFnShake, {kCam, uint32_t(frames_n), kCam + 0x30, uint32_t(hold)}, radius);
                cam->ShakeCamera(radius, frames_n, cam->d.matrix + 12, hold, sim.frame_rate);
                ++n_shake;
                flog += " ShakeCamera";
            }
            if (rng.chance(0.03)) {
                // Um destrutor de evento de câmera (driving/game/camera_events.cpp): o objeto na RAM emulada (os
                // registros pelos endereços emulados) e o PendingEvent do porte, com os quadros convertidos pela taxa.
                static const int32_t kTypes[6] = {63, 204, 242, 246, 251, 207};
                static const uint32_t kDtors[6] = {0x153700, 0x1539a8, 0x148e30, 0x149338, 0x149118, 0x149528};
                // (as animações, que prendem a câmera até o fim, mais raras)
                const int pick = rng.range(0, 9);
                const int k = pick < 1 ? 0 : pick < 2 ? 1 : 2 + (pick - 2) / 2;
                uint32_t o[10] = {}, oe[10] = {};
                auto frames_arg = [&]() -> uint32_t {
                    return rng.chance(0.2) ? uint32_t(rng.range(0, 1)) : uint32_t(rng.range(2, 240));
                };
                bool ok = true;
                switch (kTypes[k]) {
                    case 63: {
                        if (cams_port.empty()) {
                            ok = false;
                            break;
                        }
                        const size_t i = rng.next() % cams_port.size();
                        o[1] = nf::p32_u(cams_port[i]);
                        oe[1] = cams_emu[i];
                        o[2] = frames_arg();
                        o[3] = rng.chance(0.5) ? 0u : frames_arg();
                        o[4] = rng.chance(0.5) ? 1u : 0u;
                        o[5] = rng.chance(0.5) ? 0u : uint32_t(rng.range(10, 70));
                        break;
                    }
                    case 204: {
                        if (splines_port.empty()) {
                            ok = false;
                            break;
                        }
                        const size_t i = rng.next() % splines_port.size();
                        o[1] = nf::p32_u(splines_port[i]);
                        oe[1] = splines_emu[i];
                        o[2] = frames_arg();
                        o[3] = rng.chance(0.5) ? 0u : frames_arg();
                        o[4] = rng.chance(0.4) ? 0u : uint32_t(rng.range(-5, 70));
                        break;
                    }
                    case 242:
                        if (rng.chance(0.7)) {
                            for (int c = 0; c < 3; ++c)
                                lock_point[c] = float(c == 1 ? pose.y : (c == 0 ? pose.x : pose.z)) + rng.uni(-40.0f, 40.0f);
                            std::memcpy(m.mem(kLockPoint, 16), lock_point, 16);
                            o[2] = nf::p32_u(lock_point);
                            oe[2] = kLockPoint;
                        }
                        o[5] = ps2::fbits(rng.uni(-2.0f, 2.0f));
                        o[6] = ps2::fbits(rng.uni(-3.0f, 3.0f));
                        o[7] = ps2::fbits(rng.uni(-3.0f, 3.0f));
                        o[8] = uint32_t(rng.range(0, 40));
                        o[9] = rng.chance(0.3) ? 1u : 0u;
                        break;
                    case 246:
                        o[1] = uint32_t(rng.range(0, 300));
                        o[2] = frames_arg();
                        break;
                    case 251:
                        o[1] = uint32_t(rng.range(0, 120));
                        o[2] = rng.chance(0.05) ? 0x80000000u + uint32_t(rng.range(0, 1000)) : uint32_t(rng.range(0, 200));
                        o[3] = rng.chance(0.4) ? 1u : 0u;
                        break;
                    default:
                        o[1] = rng.chance(0.3) ? 0u : uint32_t(uint16_t(heli_ids[rng.next() % heli_ids.size()]));
                        // (o id 0 com transição: o SetOldCameraMode com transição, não portado)
                        o[2] = rng.chance(0.7) || o[1] == 0 ? uint32_t(rng.range(0, 1)) : frames_arg();
                        o[3] = rng.chance(0.5) ? 0u : uint32_t(rng.range(0, 5));
                        o[4] = uint32_t(rng.range(0, 3));
                        break;
                }
                if (ok) {
                    for (int i = 0; i < 10; ++i)
                        if (oe[i] == 0) oe[i] = o[i];
                    for (int i = 0; i < 10; ++i) m.write32(kEvent + 4u * uint32_t(i), oe[i]);
                    drv::mission_manager().cinematic_mode = ctx.mission_cinematic;
                    m.write32(kMission + 0x3cc, ctx.mission_cinematic);
                    e->call(kDtors[k], {kEvent, 3});
                    drv::PendingEvent pe;
                    pe.type = kTypes[k];
                    for (int i = 0; i < 10; ++i) pe.obj[i] = o[i];
                    drv::camera_event_execute(pe);
                    if (m.read32(kMission + 0x3cc) != drv::mission_manager().cinematic_mode)
                        t_events.fail("o evento " + std::to_string(kTypes[k]) + ": o modo de cinema " +
                                      hex(m.read32(kMission + 0x3cc)) + " / " + hex(drv::mission_manager().cinematic_mode));
                    ctx.mission_cinematic = drv::mission_manager().cinematic_mode;
                    ++n_events[k];
                    flog += " evento " + std::to_string(kTypes[k]);
                }
            }
            put_body();
            put_sim();
            if (std::getenv("NF_HELI_CAM_DEBUG")) {
                // Depuração: a câmera já diferente depois dos pedidos (antes do UpdateCamera)?
                std::string pre;
                if (!compare(pre)) std::printf("    quadro %d, depois dos pedidos (%s): %s\n", f, flog.c_str(), pre.c_str());
                std::printf("    quadro %d antes: modo %d/%d anterior %d/%d mudanca %x/%x transicao %d/%d fila %zu\n", f,
                            int(m.read32(kCam + 0x120)), int(cam->d.mode), int(m.read32(kCam + 0x124)),
                            int(cam->d.prev_mode), m.read32(kCam + 0xe0), cam->d.mode_change,
                            int(m.read32(kCam + 0x12c)), int(cam->d.transition), cam->director_queue.size());
            }
            // O RPathEngine (o EAnimUpdate) e o UpdateCamera (o ECameraUpdate).
            if (sim.state != 3) {
                const float now = ps2::ee::cvt_s(sim.steps);
                e->call(kFnEngineUpdate, {}, now);
                drv::RPathEngine_Update(now);
            }
            e->call(kFnUpdate, {kCam});
            cam->update(ctx);
            ++t_frames.cases;
            prev_log += " |" + std::to_string(f) + " m" + std::to_string(cam->d.mode) + flog;
            {
                const drv::CameraModeInfo& md = tables->mode(cam->d.mode);
                if (md.type == drv::kCamAutoDrive) ++f_autodrive;
                if (md.type == drv::kCamAnimation) ++f_anim;
                if (md.type == drv::kCamAIPathAnimation) ++f_aipath;
                if (cam->d.limited) ++f_limited;
                if (cam->d.lock_target) ++f_lock;
                if (cam->d.dz_pitch != 0.0f || cam->d.dz_yaw != 0.0f) ++f_dead;
                if (cam->d.weap_change) ++f_weap_change;
            }
            std::string out;
            if (!compare(out)) {
                if (t_frames.bad == 0 && std::getenv("NF_HELI_CAM_DEBUG")) {
                    // Depuração: todas as palavras diferentes da câmera.
                    const uint8_t* eb = m.mem(kCam, 0x370);
                    const uint8_t* pb = reinterpret_cast<const uint8_t*>(&cam->d);
                    for (uint32_t o = 0; o < 0x370; o += 4) {
                        uint32_t x, y;
                        std::memcpy(&x, eb + o, 4);
                        std::memcpy(&y, pb + o, 4);
                        if (x != y) std::printf("    +0x%03x original %08x porte %08x\n", o, x, y);
                    }
                    for (int32_t mi = 0; mi < g.mode_count; ++mi) {
                        const drv::CameraModeInfo& mm = tables->mode(mi);
                        std::printf("    modo %d tipo 0x%x indice %d suave %d selecionavel %d id %d", int(mi),
                                    unsigned(mm.type), int(mm.index), int(mm.smooth_trans), int(mm.selectable),
                                    int(mm.cam_id));
                        if (mm.type == drv::kCamHeli) {
                            const drv::HeliCamInfo& hh = tables->heli(mm.index);
                            std::printf(" rigido %d colisoes %d ruido %g bracos %d", int(hh.rigid_arm),
                                        int(hh.check_collisions), double(hh.noise_amount), int(hh.arm_count));
                        }
                        std::printf("\n");
                    }
                    std::printf("    pedidos dos quadros anteriores:%s\n", prev_log.c_str());
                }
                t_frames.fail("quadro " + std::to_string(f) + " (modo " + std::to_string(cam->d.mode) + ";" + flog +
                              "): " + out);
                if (t_frames.bad > 10) break;
            }
        }
        char what[300];
        std::snprintf(what, sizeof(what),
                      "UpdateCamera (%s: %" PRIu64 " SetAutoDriveCamera, %" PRIu64 " entradas, %" PRIu64
                      " CameraLockOn, %" PRIu64 " TriggerAnimationCamera, %" PRIu64 " TriggerAIPath...)",
                      label, n_autodrive, n_input, n_lock, n_anim, n_aipath);
        report(what, t_frames, failures);
        std::printf("    quadros: AutoDrive %" PRIu64 ", WorldAnim %" PRIu64 ", AIPathAnim %" PRIu64
                    ", presos pelo LimitPitchYaw %" PRIu64 ", presos num ponto %" PRIu64 ", com zona morta %" PRIu64
                    ", trocando de braço %" PRIu64 "; pedidos: EndCameraAnim %" PRIu64 ", SetLastSelectable %" PRIu64
                    ", ForceCameraChange %" PRIu64 ", ShakeCamera %" PRIu64 ", WeaponFired %" PRIu64 ", alvos %" PRIu64
                    ", armas %" PRIu64 ", giros %" PRIu64 ", pausas %" PRIu64 "\n",
                    f_autodrive, f_anim, f_aipath, f_limited, f_lock, f_dead, f_weap_change, n_end, n_last, n_force,
                    n_shake, n_fired, n_target, n_weapon, n_spin, n_pause);
        std::printf("    eventos: 63 %" PRIu64 ", 204 %" PRIu64 ", 242 %" PRIu64 ", 246 %" PRIu64 ", 251 %" PRIu64
                    ", 207 %" PRIu64 " (o que fazem na câmera: nos quadros)\n",
                    n_events[0], n_events[1], n_events[2], n_events[3], n_events[4], n_events[5]);
        t_events.cases = n_events[0] + n_events[1] + n_events[2] + n_events[3] + n_events[4] + n_events[5];
        report("os destrutores dos eventos de câmera (o modo de cinema)", t_events, failures);
        if (drv::camera_pending() != pending_before)
            std::printf("    (pendentes do port nestes quadros: %" PRIu64 ")\n", drv::camera_pending() - pending_before);
        };
        const drv::CameraEventsEnv saved_env = drv::camera_events_env();
        drv::camera_events_env().camera = cam;
        drv::camera_events_env().ctx = &ctx;
        m.write32(kWorldCameras, kWorldCamList);
        m.write32(kWorldCamList, 0);
        m.write32(kWorldCamList + 4, kCam);
        const uint32_t saved_cinematic = drv::mission_manager().cinematic_mode;
        run_pass("braços do disco", frames);
        // Os braços sorteados (na RAM emulada e no porte).
        {
            static std::vector<drv::AutoDriveArmInfo> arms2;
            const drv::AutoDriveArmInfo base = nf::raw(mg.autodrive_arms)[0];
            arms2.assign(2, base);
            for (int k = 0; k < 2; ++k) {
                drv::AutoDriveArmInfo& a = arms2[size_t(k)];
                a.allow_deadzone = 1;
                a.has_rest_pos = k == 0 ? 1u : 0u;
                a.preserve_transform = 1;
                a.lock_arm_to_car = k == 1 ? 1u : 0u;
                a.weapons[0] = 1u << (k == 0 ? 3 : 5);
                a.weapons[1] = 0;
                a.max_deadzone_pitch = rng.uni(0.01f, 0.06f);
                a.max_deadzone_yaw = rng.uni(0.01f, 0.08f);
                a.max_autoaim_pitch = rng.uni(0.01f, 0.05f);
                a.max_autoaim_yaw = rng.uni(0.01f, 0.08f);
                a.autoaim_interpol_speed = rng.uni(0.05f, 0.3f);
                a.rest_interpol_fall_scale = rng.uni(10.0f, 60.0f);
                for (int i = 0; i < 5; ++i) {
                    a.min_yaw[i] = rng.chance(0.3) ? 0.0f : rng.uni(-0.5f, -0.05f);
                    a.max_yaw[i] = rng.chance(0.3) ? 0.0f : rng.uni(0.05f, 0.5f);
                    a.min_pitch[i] = rng.uni(-0.5f, 0.0f);
                    a.max_pitch[i] = rng.uni(0.0f, 0.6f);
                }
            }
            const uint32_t arms_new = e->heap;
            e->heap += 0x200;
            std::memcpy(m.mem(arms_new, 0x200), arms2.data(), 0x200);
            m.write32(kModeGlobals + 0x48, arms_new);
            m.write32(kModeGlobals + 0x44, 2);
            mg.autodrive_arms = arms2.data();
            mg.autodrive_arm_count = 2;
            arms_emu_cur = arms_new;
            ctx.weapons[0].weapon = 3;
            ctx.weapons[1].weapon = 5;
            m.write32(kWeaponSlots + 0x00, 3);
            m.write32(kWeaponSlots + 0x54, 5);
        }
        run_pass("braços sorteados", frames);
        drv::camera_events_env() = saved_env;
        drv::mission_manager().cinematic_mode = saved_cinematic;
        std::printf("    (o interpretador rodou %" PRIu64 " instruções)\n", e->m.steps());
    } catch (const nf::MipsError& err) {
        std::printf("  o interpretador parou: %s\n", err.what());
        ++failures;
    }
    drv::RPathEngine_Purge();
    delete anchor;
    delete cam;
    delete tables;
    delete e;
    return failures;
}

}  // namespace drvcheck
