// nf_driving_check, parte 26 (tests/driving_check_cameras.h; docs/driving.md seção 30): a câmera e os gatilhos que
// faltavam a Paris. O código ORIGINAL do DRIVING.ELF roda no interpretador MIPS com a VU0 ao lado do porte
// (driving/camera/player_camera*.cpp, driving/game/triggers.cpp, driving/render/sniper_zoom.cpp):
//  a) as âncoras do RWorldCamera (0x1d5838 IsAnchorValid, 0x1d5ef0 SetAnchor com os 'Cams' do modelo do vanquish,
//     0x1d5b90 GetAnchorMatrix4, 0x1d5cd0 GetAnchorPosition, 0x1d5888 GetAnchorSpeed, 0x1d5d50 GetAnchorLinearVelocity
//     com o primeiro carro num trilho ou não, 0x1d5940 GetAnchorAcceleration, 0x1d5fa8 GetAnchorRenderOffset, 0x1d5e38
//     GetAnchorResetAvailable) com âncoras de corpo rígido, de corpo simples (vagas válidas e não) e inválidas;
//  b) a câmera do vanquish quadro a quadro: o LoadFile (0x1a49e0) e o construtor, e o UpdateCamera (0x1b29e0) com a
//     âncora andando por Paris entre pedidos sorteados: a entrada do carro (CameraInputCallback 0x1bcc00 na
//     configuração do carro: o DriveCamInputHandler 0x1bddc8), NextCameraMode (0x1ba5c0), SetCameraLookBack (0x1bc048),
//     PauseOff (0x1bcb88), SetPauseCamera (0x1bb9e8), SetCameraModeByIndex (0x1ba418) nos modos Heli, Bumper,
//     Dashboard, Tumble, Ellipse e na animação relativa, TriggerCarAnimationCamera (0x1bc9f0) com outra âncora (o
//     DirectorSetAnchor) e as animações 'Cams' do modelo no RAnimEngine ORIGINAL, SetOldCameraMode (0x1bb340, também
//     com transição: o RPlayerCamera da pilha), EndCameraAnim, SetLastSelectableCameraMode, o destrutor do 97
//     EPlayCarCameraAnim (0x153c48), o GetResetAvailable do carro (o Tumble), os outros carros e os corpos simples de
//     tipo 8 perto do olho (CheckObjectCollisions) e o RAnimEngine::Update (0x1a1a10) a cada quadro;
//  c) os gatilhos de Paris tocados pelos corpos simples (0x22eec0 CheckCollide, 0x22dae8 Process com o dono do jogador
//     ou não), pelas instâncias movidas pelos RPathHandle (0x22efb0 e 0x22f030 CheckCollide, 0x22d5a8 Process) e o
//     WTriggerManager::Update (0x22f0c0) com os três;
//  d) o RSniperZoom: o anel (0x1ed7b0) e o SetupVerts (0x1ed8d8) com a tela, o cursor e o alfa sorteados.
// Compara a câmera inteira (0x370 bytes) menos os ponteiros (a âncora e os 'Cams' pelos endereços correspondentes), o
// RPlayerCamState, os globais, a fila do diretor (os registros do SetOldCameraMode pelo conteúdo), o RCameraSpline, a
// cópia do registro no RAnimEngine::Handle (o bloco inteiro quando há animação) e a lista do motor; nos gatilhos, os
// gatilhos, os globais do WTriggerManager, o EventDynamicData e os eventos criados.
// Fora do código original (ganchos nativos), além dos da parte 19: o UGroup::DataLocateTag e o GetArray (o grupo do
// modelo da âncora), as entradas da tabela virtual dos objetos (o AIVehicle do carro, o GetRenderOffset do objeto de
// desenho), o RegisterEvent::LookupEvent (os eventos anotados), o AIElementController::RedirectCarpElement (um elemento
// que não é carro) e o percurso da lista do RPathEngine (GetFirstPathHandle e GetNextPathHandle sobre os nós dados).

#include "driving_check_cameras.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/bigf.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "driving/car_model.h"
#include "driving/carp_runtime.h"
#include "driving/game/bond_car.h"
#include "driving/game/camera_events.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/shells.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/render/anim_engine.h"
#include "driving/render/path_engine.h"
#include "driving/render/sniper_zoom.h"
#include "driving/track.h"
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

// As funções do original.
constexpr uint32_t kFnLoadFile = 0x1a49e0, kFnCameraCtor = 0x1b2620, kFnUpdate = 0x1b29e0;
constexpr uint32_t kFnIsValid = 0x1d5838, kFnSetAnchor = 0x1d5ef0, kFnMatrix = 0x1d5b90, kFnPosition = 0x1d5cd0,
                   kFnSpeed = 0x1d5888, kFnLinVel = 0x1d5d50, kFnAccel = 0x1d5940, kFnRenderOffset = 0x1d5fa8,
                   kFnReset = 0x1d5e38;
constexpr uint32_t kFnInput = 0x1bcc00, kFnNext = 0x1ba5c0, kFnLookBack = 0x1bc048, kFnPauseOff = 0x1bcb88,
                   kFnPauseCam = 0x1bb9e8, kFnModeByIndex = 0x1ba418, kFnCarAnim = 0x1bc9f0, kFnOldMode = 0x1bb340,
                   kFnEndAnim = 0x1bb098, kFnLastSel = 0x1bb1f8, kFnCarAnimDtor = 0x153c48;
constexpr uint32_t kFnAnimUpdate = 0x1a1a10, kFnLookupEvent = 0x1757b8;
constexpr uint32_t kFnTrigProcessSimple = 0x22dae8, kFnTrigProcessInstance = 0x22d5a8, kFnTrigUpdate = 0x22f0c0,
                   kFnTrigCheckSimple = 0x22eec0, kFnTrigCheckPoint = 0x22efb0, kFnTrigCheckPoint3 = 0x22f030;
constexpr uint32_t kFnRingVertex = 0x1ed7b0, kFnSetupVerts = 0x1ed8d8;

// Os globais do original.
constexpr uint32_t kTables = 0x3339b0, kConstants = 0x333610;
constexpr uint32_t kShakeEye = 0x333a20, kPrevRates = 0x333a30, kZoomButton = 0x3276e0;
constexpr uint32_t kNoisePerm = 0x32f4e8, kNoiseGrad = 0x3d08a8;
constexpr uint32_t kWorldCameras = 0x333ed8;
constexpr uint32_t kOverrideCar = 0x33196c, kWeaponMgrPtr = 0x33a530, kMissionPtr = 0x33a024,
                   kRendererPtr = 0x333760, kUber = 0x100000, kRate = 0x333ff0, kFrequency = 0x333ff4,
                   kPathList = 0x3339a8, kTimeData = 0x334010;
constexpr uint32_t kAnimCount = 0x333600, kAnimLastTick = 0x333604, kAnimList = 0x3d1518;
constexpr uint32_t kEventData = 0x32e708;
constexpr uint32_t kTrigManagerPtr = 0x33aa88, kTrigSizeG = 0x33aa8c, kTrigDataG = 0x33aa90, kTrigStamp = 0x33aa98;
constexpr uint32_t kRayCount = 0x332950, kRayIndex = 0x333560;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000, kInfos = 0x01002000;
constexpr uint32_t kCar = 0x01012000, kCarB = 0x01013000, kCarVt = 0x01014000, kCarsVec = 0x01014500,
                   kWeaponMgr = 0x01014600, kWeaponSlots = 0x01014700, kStrings = 0x01015400, kCam = 0x01016000,
                   kIniBuf = 0x01017000, kScratchpad = 0x01028000;
constexpr uint32_t kFnCarType = 0x0102f000, kFnZero = 0x0102f010, kFnAiVehicle = 0x0102f020,
                   kFnRenderOffsetFake = 0x0102f030, kFnEventSink = 0x0102f040, kFnRedirect = 0x0102f050, kFnAudio = 0x0102f060;
constexpr uint32_t kMgr = 0x01030000, kGrid = 0x01030100;
constexpr uint32_t kMission = 0x01031000, kRendererObj = 0x01032000, kIcm = 0x01032100, kAudio = 0x01032200;
constexpr uint32_t kGroup = 0x01034000, kGroupNone = 0x01034040, kCamsUData = 0x01034080, kGroupEnd = 0x01034100,
                   kAiVehicle = 0x01034200, kSplinePathE = 0x01034300, kPathHandleE = 0x01034408,
                   kEvent = 0x01034600, kElem = 0x01034700, kWorldCamList = 0x01034800, kRenderVt = 0x01034900,
                   kRenderObjs = 0x01034a00, kZoom = 0x01035000, kSimpleObjs = 0x01036000, kSrbVec = 0x01039000,
                   kRingPos = 0x0103a800, kRingUv = 0x0103a840;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;
// (o RPathHandle do PS2 vive 8 bytes dentro do nó de 0xdc bytes do FastAlloc, alinhado a 16: as matrizes +0x08/+0x48 e a
// velocidade +0x88 ficam alinhadas para o lq/lqc2; o kPathHandleE repete isso)

constexpr uint32_t kTagCams = 0x43616d73u;  // 'Cams'

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 2626) {}
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

// Um dono de corpo simples do porte: a palavra +0x58 (a assinatura do dono dele: o IsOwnedBy).
class TestOwner : public drv::BasicOwner {
public:
    uint32_t word58 = 0;
    uint32_t raw_word(uint32_t offset) override { return offset == 0x58 ? word58 : 0; }
};

// O AIVehicle do primeiro carro (o GetAnchorLinearVelocity do porte o pergunta por uma função sem contexto).
drv::AIGroundVehicle* g_vehicle = nullptr;
drv::AIGroundVehicle* player_vehicle() { return g_vehicle; }

class CamEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = 0, heap_end = 0;
    uint32_t ini_size = 0;
    uint32_t strtok_last = 0;
    uint32_t ai_vehicle = 0;  // o que a entrada +0x58 do carro devolve
    int32_t pending_type = -1;
    std::vector<std::pair<int32_t, uint32_t>> events;  // os eventos criados (o número e os dados)
    std::vector<uint32_t> path_handles;                // a lista do RPathEngine
    size_t path_iter = 0;

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
        m.hook(0x260300, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
        });
        m.hook(0x2601f0, "MEM_copy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), s = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(d, n), mm.mem(s, n), n);
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
        m.hook(0x10c6c8, "o printf de depuração dos eventos", [](MipsMachine&) {});
        m.hook(0x165288, "ActionQueue::ActionQueue", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        m.hook(0x223338, "WRoadNav::WRoadNav", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
        // O grupo do modelo da âncora (o 'Arti' do vanquish): o DataLocateTag acha os 'Cams' só nele; nos outros (o
        // grupo vazio dos corpos simples, o que o LoadFile pergunta) o fim.
        m.hook(0x2d5a10, "UGroup::DataLocateTag", [](MipsMachine& mm) {
            const uint32_t g = uint32_t(mm.gpr(R::a0)), tag = uint32_t(mm.gpr(R::a1));
            if (g == kGroup) mm.set_gpr(R::v0, tag == kTagCams ? kCamsUData : kGroupEnd);
            else if (g == kGroupNone) mm.set_gpr(R::v0, kGroupEnd);
            else mm.set_gpr(R::v0, 0x5000);
        });
        m.hook(0x2d6940, "UGroup::GetArray", [](MipsMachine& mm) {
            const uint32_t g = uint32_t(mm.gpr(R::a0));
            mm.set_gpr(R::v0, g == kGroup || g == kGroupNone ? kGroupEnd : 0x5000);
        });
        m.hook(0x1d8470, "RColorize::SetEnabled", [](MipsMachine&) {});
        m.hook(kFnCarType, "PBondCar::GetCarType (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kStrings); });
        m.hook(kFnZero, "uma entrada do carro que devolve zero (falsa)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnAiVehicle, "o AIVehicle do carro (falso)", [this](MipsMachine& mm) { mm.set_gpr(R::v0, ai_vehicle); });
        // O RVehicle::GetRenderOffset do objeto de desenho: o float que o objeto guarda em +0x20.
        m.hook(kFnRenderOffsetFake, "RVehicle::GetRenderOffset (falso)", [](MipsMachine& mm) {
            float f;
            std::memcpy(&f, mm.mem(uint32_t(mm.gpr(R::a0)) + 0x20, 4), 4);
            mm.set_fpr(0, f);
        });
        auto entry = [this](uint32_t vt, uint32_t off, uint32_t fn) {
            m.write32(vt + off, 0);
            m.write32(vt + off + 4, fn);
        };
        entry(kCarVt, 0x40, kFnAudio);          // GetAudio (o AVehicle falso: o som da mira +0xc0)
        entry(kCarVt, 0x58, kFnAiVehicle);      // o AIVehicle
        m.write32(kCarVt + 0x68, 0);
        m.write32(kCarVt + 0x6c, 0x192ff0);     // PBondCar::GetResetAvailable (o original: o +0x3e8)
        entry(kCarVt, 0x80, kFnCarType);        // GetCarType
        entry(kCarVt, 0x98, kFnZero);           // GetCarClass
        entry(kCarVt, 0x290, kFnZero);          // GetSecondaryType
        entry(kRenderVt, 0x50, kFnRenderOffsetFake);
        // A libm do jogo: o nf::fm do porte nos dois lados.
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(0x2bfd28, "tanf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::tan(mm.fpr(12))); });
        m.hook(0x2c0618, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(0x2c0418, "acosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::acos(mm.fpr(12))); });
        m.hook(0x2c0518, "asinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::asin(mm.fpr(12))); });
        m.hook(0x2065d0, "SMissionManager::SetCinematicMode (o +0x3cc)", [](MipsMachine& mm) {
            const uint32_t mgr = uint32_t(mm.gpr(R::a0)), mode = uint32_t(mm.gpr(R::a1));
            if (mode != 0) mm.write32(mgr + 0x3cc, mm.read32(mgr + 0x3cc) | mode);
            else mm.write32(mgr + 0x3cc, 0x80000000u);
        });
        m.hook(0x1664c0, "InputConfigManager::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, kIcm); });
        m.hook(kFnAudio, "PBondCar::GetAudio (falso)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kAudio); });
        m.hook(0x23c618, "GHud::TheApp (sem HUD)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(0x22af38, "WTargetPicker::GetSelectedTarget", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(0x12e6f8, "AIGroundVehicle::SetAttackMode", [](MipsMachine&) {});
        m.hook(0x12e468, "AIGroundVehicle::SetNavigateMode", [](MipsMachine&) {});
        m.hook(kFnLookupEvent, "RegisterEvent::LookupEvent", [this](MipsMachine& mm) {
            pending_type = int32_t(mm.gpr(R::a0));
            mm.set_gpr(R::v0, kFnEventSink);
        });
        m.hook(kFnEventSink, "a criadora de um evento", [this](MipsMachine& mm) {
            events.push_back({pending_type, uint32_t(mm.gpr(R::a0))});
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x12c608, "AIElementController::RedirectCarpElement (um elemento que não é carro)",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, kElem); });
        m.hook(0x1b1f28, "RPathEngine::GetFirstPathHandle", [this](MipsMachine& mm) {
            path_iter = 0;
            mm.set_gpr(R::v0, path_handles.empty() ? 0u : path_handles[0]);
        });
        m.hook(0x1b1f68, "RPathEngine::GetNextPathHandle", [this](MipsMachine& mm) {
            ++path_iter;
            mm.set_gpr(R::v0, path_iter < path_handles.size() ? path_handles[path_iter] : 0u);
        });
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0.0f, float f13 = 0.0f, float f14 = 0.0f) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.set_fpr(12, f12);
        m.set_fpr(13, f13);
        m.set_fpr(14, f14);
        m.call(fn, args, 200'000'000);
    }
    void put_f(uint32_t a, float f) { std::memcpy(m.mem(a, 4), &f, 4); }
    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > heap_end) throw nf::MipsError("alocação: a área acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
};

// O CARP do vanquish na RAM emulada: as palavras que apontam para dentro dele viram endereços emulados.
struct CarpCopy {
    uint32_t port = 0, size = 0, emu = 0;
    uint32_t to_emu(uint32_t p) const { return (p >= port && p < port + size) ? emu + (p - port) : p; }
    bool in(uint32_t p) const { return p >= port && p < port + size; }
};

uint32_t rd32(uint32_t p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<uint8_t>(p) + off, 4);
    return v;
}

// Uma pose de corpo andando.
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

void advance(Pose& p, double dt) {
    p.yaw += p.yaw_rate * dt;
    p.pitch = std::clamp(p.pitch + p.pitch_rate * dt, -0.35, 0.35);
    const double cy = std::cos(p.yaw), sy = std::sin(p.yaw), cp = std::cos(p.pitch);
    p.x += sy * cp * p.speed * dt;
    p.y += std::sin(p.pitch) * p.speed * dt;
    p.z += cy * cp * p.speed * dt;
}

void random_quat(Rng& rng, float q[4]) {
    float s = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = rng.uni(-1, 1);
        s += q[i] * q[i];
    }
    s = std::sqrt(std::max(s, 1e-6f));
    for (int i = 0; i < 4; ++i) q[i] /= s;
}

// Todo o mundo da conferência: a emulação e o porte lado a lado.
struct World {
    CamEmu* e = nullptr;
    drv::Track* track = nullptr;
    drv::TrackWorld* tw = nullptr;
    WorldCopy wc;
    drv::CarModel* model = nullptr;
    CarpCopy cc;
    uint32_t cams_port = 0, cams_emu = 0, cams_count = 0;  // os registros 'Cams' do modelo (p32 e emulado)
    std::vector<uint32_t> cams_systems;                     // o sistema da primeira animação de cada registro
    drv::Simulation* sim = nullptr;
    drv::BondCar* car_a = nullptr;
    drv::BondCar* car_b = nullptr;
    drv::CameraAnchor anchor_a, anchor_b;
    std::vector<std::unique_ptr<TestOwner>> simple_owners;  // um por vaga
    std::vector<std::unique_ptr<drv::CameraAnchor>> simple_anchors;
    drv::CameraAnchor invalid_anchor;  // tipo 0 sem a bandeira do corpo simples
    drv::CameraContext ctx;
    // O trilho do primeiro carro (o porte).
    drv::AIGroundVehicle vehicle{};
    alignas(16) drv::AISplinePath spline{};
    alignas(16) drv::RPathHandle path_handle{};
    float render_offset[3] = {};  // A, B, os corpos simples
    int32_t audio_zoom = 0;       // o AVehicle +0xc0 do carro (o som da mira)

    uint32_t emu_of(const drv::CameraAnchor* a) const {
        if (!a) return 0;
        if (a == &anchor_a) return kCar;
        if (a == &anchor_b) return kCarB;
        if (a == &invalid_anchor) return kSimpleObjs + 0x80u * 96u;
        for (size_t i = 0; i < simple_anchors.size(); ++i)
            if (simple_anchors[i].get() == a) return kSimpleObjs + 0x80u * uint32_t(i);
        return 0xdead0000u;
    }
    drv::CameraAnchor* anchor_for(drv::PhysicsObject* o) {
        if (o == car_a) return &anchor_a;
        if (o == car_b) return &anchor_b;
        for (size_t i = 0; i < simple_owners.size(); ++i)
            if (simple_owners[i].get() == o) return simple_anchors[i].get();
        return nullptr;
    }

    // O porte -> o original: os corpos, as vagas, as assinaturas, o passo.
    void put_sim() {
        MipsMachine& m = e->m;
        m.write32(kSim + 0x04, 0);
        m.write32(kSim + 0x1c, kBodies);
        m.write32(kSim + 0x20, kSrbVec);
        m.write32(kSim + 0x24, kInfos);
        m.write32(kSim + 0x28, MipsMachine::kScratchpad);
        m.write32(kSim + 0x2c, MipsMachine::kScratchpad + 0x1800);
        for (int i = 0; i < 2; ++i) {
            drv::RigidBody b = *drv::GetRigidBody(*sim, i);
            const uint32_t info = kInfos + 0x500u * uint32_t(i);
            std::memcpy(m.mem(kBodies + 0x80u * uint32_t(i), 0x80), &b, 0x80);
            m.write32(kBodies + 0x80u * uint32_t(i) + 0x5c, info);
            drv::RigidBodyInfo bi = *nf::raw(b.info);
            std::memcpy(m.mem(info, 0x500), &bi, 0x500);
            for (int k = 0; k < 16; ++k) m.write32(info + 0x380 + 4u * uint32_t(k), 0);
            m.write32(info + 0x4d8, 0);
        }
        for (int i = 0; i < drv::kRigidBodySlots; ++i) {
            m.write32(kSim + 0x30 + 4u * uint32_t(i), sim->rigid_sig[i]);
            m.write32(kSim + 0x270 + 4u * uint32_t(i), i == 0 ? kCar : i == 1 ? kCarB : 0u);
        }
        std::memcpy(m.mem(kSrbVec, 0x40 * drv::kSimpleBodySlots), sim->simple_bodies, 0x40 * drv::kSimpleBodySlots);
        for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
            m.write32(kSim + 0xf0 + 4u * uint32_t(i), sim->simple_sig[i]);
            m.write32(kSim + 0x330 + 4u * uint32_t(i), sim->simple_owners[i] ? kSimpleObjs + 0x80u * uint32_t(i) : 0u);
            m.write32(kSimpleObjs + 0x80u * uint32_t(i) + 0x58, simple_owners[size_t(i)]->word58);
        }
        m.write32(kSim + 0xdb4, uint32_t(sim->state));
        m.write32(kSim + 0xdbc, uint32_t(sim->frame_rate));
        std::memcpy(m.mem(kSim + 0xdc0, 4), &sim->dt, 4);
        m.write32(kSim + 0xdc4, uint32_t(sim->steps));
        m.write32(kCar + 0x3e8, uint32_t(car_a->d.reset_available));
        m.write32(kCarB + 0x3e8, uint32_t(car_b->d.reset_available));
        e->ai_vehicle = g_vehicle ? kAiVehicle : 0u;
        // O trilho: o AIVehicle +0x50, o AISplinePath +0x60, a velocidade do RPathHandle +0x88.
        m.write32(kAiVehicle + 0x50, vehicle.spline_path ? kSplinePathE : 0u);
        m.write32(kSplinePathE + 0x60, kPathHandleE);
        std::memcpy(m.mem(kPathHandleE, sizeof path_handle), &path_handle, sizeof path_handle);
        for (int k = 0; k < 3; ++k) {
            const uint32_t r = kRenderObjs + 0x80u * uint32_t(k);
            e->put_f(r + 0x20, render_offset[k]);
        }
        // O mundo de colisão (o carimbo das consultas).
        const drv::WCollisionMgr& wm = tw->mgr();
        m.write32(wc.mgr + 4, wm.query_stamp);
        m.write32(wc.mgr + 0x2c, wm.barrier_mask);
        m.write32(wc.mgr + 0x30, wm.vu0_path);
        for (uint32_t i = 0; i < wc.count; ++i) m.write32(wc.insts + 0x40 * i + 0x10, tw->instances()[i].query_stamp);
        // O relógio do RTimeData.
        m.write32(kTimeData, drv::rtime_data().time);
    }

    // Um corpo simples de tipo 8 (o helicóptero; os da CheckObjectCollisions) ou outro, perto de um ponto.
    void random_simple(Rng& rng, int i, const float near[3], float spread) {
        drv::SimpleRigidBody& b = *drv::GetSimpleRigidBody(*sim, i);
        random_quat(rng, b.quat);
        for (int k = 0; k < 3; ++k) b.pos[k] = near[k] + rng.uni(-spread, spread);
        b.type = uint8_t(rng.chance(0.6) ? 8 : rng.range(1, 9));
        b.slot = int8_t(i);
        b.flags = uint16_t(rng.next() & 0x1ff);
        for (int k = 0; k < 3; ++k) b.vel[k] = rng.uni(-15, 15);
        b.radius = rng.chance(0.3) ? rng.uni(0.1f, 1.4f) : rng.uni(0.5f, 8.0f);
        for (int k = 0; k < 3; ++k) b.accel[k] = rng.uni(-3, 3);
        b.mass = rng.uni(1, 100);
        const int w = rng.range(0, 3);
        simple_owners[size_t(i)]->word58 =
            w == 0 ? 0u : w == 1 ? sim->rigid_sig[0] : w == 2 ? sim->rigid_sig[1] : (rng.next() & 0x7fffffffu) | 1u;
    }
};

// ---------------------------------------------------------------------------------------------- a) as âncoras

int check_anchors(World& w, drv::PlayerCamera& cam, int cases, Rng& rng) {
    int failures = 0;
    CamEmu& e = *w.e;
    MipsMachine& m = e.m;
    drv::Simulation& sim = *w.sim;
    Tally t[9];
    const char* kNames[9] = {"IsAnchorValid",         "SetAnchor (os 'Cams' do modelo)", "GetAnchorMatrix4",
                             "GetAnchorPosition",     "GetAnchorSpeed",                  "GetAnchorLinearVelocity",
                             "GetAnchorAcceleration", "GetAnchorRenderOffset",           "GetAnchorResetAvailable"};
    uint64_t rail_reads = 0, simple_anchors = 0, invalid = 0;
    Pose pa, pb;
    for (int c = 0; c < cases; ++c) {
        const std::string tag = " (caso " + std::to_string(c) + ")";
        // Os corpos, os corpos simples, o trilho, o reposicionamento.
        for (int k = 0; k < 2; ++k) {
            Pose& p = k == 0 ? pa : pb;
            p.x = rng.uni(-500, 500);
            p.y = rng.uni(-5, 40);
            p.z = rng.uni(-500, 500);
            p.yaw = rng.uni(-3.14159f, 3.14159f);
            p.pitch = rng.uni(-0.3f, 0.3f);
            p.roll = rng.uni(-0.2f, 0.2f);
            p.speed = rng.chance(0.1) ? 0.0 : rng.uni(-10, 50);
            drv::RigidBody* rb = drv::GetRigidBody(sim, k);
            put_pose(p, rb, nf::raw(rb->info));
            for (int i = 0; i < 3; ++i) rb->force[i] = rng.uni(-30000, 30000);
            rb->mass = rng.chance(0.05) ? rng.uni(0.0f, 0.001f) : rng.uni(100, 2500);
            random_quat(rng, rb->quat);
        }
        const float origin[3] = {0, 0, 0};
        for (int k = 0; k < 4; ++k) w.random_simple(rng, rng.range(0, drv::kSimpleBodySlots - 1), origin, 500.0f);
        g_vehicle = rng.chance(0.5) ? &w.vehicle : nullptr;
        w.vehicle.spline_path = rng.chance(0.7) ? nf::p32_u(&w.spline) : 0u;
        for (int k = 0; k < 3; ++k) w.path_handle.velocity[k] = rng.uni(-40, 40);
        w.path_handle.last_time = rng.uni(0, 1000);
        w.car_a->d.reset_available = rng.chance(0.5) ? 0 : rng.range(1, 30);
        w.car_b->d.reset_available = rng.chance(0.5) ? 0 : rng.range(1, 30);
        for (float& r : w.render_offset) r = rng.chance(0.2) ? 0.0f : rng.uni(-1.0f, 1.5f);
        w.anchor_a.render_offset = w.render_offset[0];
        w.anchor_b.render_offset = w.render_offset[1];
        for (auto& a : w.simple_anchors) a->render_offset = w.render_offset[2];
        // A âncora: o carro (com 'Cams'), o outro carro, um corpo simples (vaga qualquer: as de 48 em diante são
        // inválidas), um objeto que não é âncora, nenhuma.
        drv::CameraAnchor* a = nullptr;
        const int pick = rng.range(0, 99);
        if (pick < 30) a = &w.anchor_a;
        else if (pick < 45) a = &w.anchor_b;
        else if (pick < 85) {
            const int s = rng.chance(0.7) ? rng.range(0, 47) : rng.range(48, 95);
            a = w.simple_anchors[size_t(s)].get();
            ++simple_anchors;
        } else if (pick < 95) {
            a = &w.invalid_anchor;
            ++invalid;
        }
        // Os 'Cams' de antes (o SetAnchor sem 'Cams' os deixa).
        if (rng.chance(0.1)) {
            cam.d.camera_anims = 0;
            cam.d.camera_anim_count = 0;
            m.write32(kCam + 0x108, 0);
            m.write32(kCam + 0x10c, 0);
        }
        cam.d.mode_change = rng.next() & 0xf;
        m.write32(kCam + 0xe0, cam.d.mode_change);
        w.put_sim();
        const uint32_t ea = w.emu_of(a);
        e.call(kFnSetAnchor, {kCam, ea});
        cam.SetAnchor(a);
        auto anchor_same = [&](std::string& why) -> bool {
            const uint32_t pe = w.emu_of(cam.anchor_object());
            if (m.read32(kCam + 0x118) != pe) {
                why = "a âncora (+0x118): original " + hex(m.read32(kCam + 0x118)) + ", porte " + hex(pe);
                return false;
            }
            if (m.read32(kCam + 0xe0) != cam.d.mode_change) {
                why = "+0xe0: original " + hex(m.read32(kCam + 0xe0)) + ", porte " + hex(cam.d.mode_change);
                return false;
            }
            const uint32_t anims = cam.d.camera_anims == 0 ? 0u : w.cc.to_emu(cam.d.camera_anims);
            if (m.read32(kCam + 0x108) != anims || m.read32(kCam + 0x10c) != cam.d.camera_anim_count) {
                why = "os 'Cams' (+0x108/+0x10c): original " + hex(m.read32(kCam + 0x108)) + "/" +
                      std::to_string(m.read32(kCam + 0x10c)) + ", porte " + hex(anims) + "/" +
                      std::to_string(cam.d.camera_anim_count);
                return false;
            }
            return true;
        };
        {
            ++t[1].cases;
            std::string why;
            if (!anchor_same(why)) t[1].fail(why + tag);
        }
        // Uma consulta.
        const int q = rng.chance(0.1) ? 0 : rng.range(2, 8);
        ++t[q].cases;
        std::string why;
        switch (q) {
            case 0: {
                e.call(kFnIsValid, {kCam});
                const bool pv = cam.IsAnchorValid();
                if ((m.gpr(R::v0) != 0) != pv) why = "o resultado";
                break;
            }
            case 2: {
                e.call(kFnMatrix, {kCam});
                const float* p = cam.GetAnchorMatrix4(w.ctx);
                same_words("a matriz", m.mem(uint32_t(m.gpr(R::v0)), 64), p, 64, {}, why);
                break;
            }
            case 3: {
                e.call(kFnPosition, {kCam});
                const float* p = cam.GetAnchorPosition(w.ctx);
                same_words("a posição", m.mem(uint32_t(m.gpr(R::v0)), 16), p, 16, {}, why);
                break;
            }
            case 4: {
                e.call(kFnSpeed, {kCam});
                const float pv = cam.GetAnchorSpeed(w.ctx);
                if (ps2::fbits(m.fpr(0)) != ps2::fbits(pv))
                    why = "a velocidade: original " + hex(ps2::fbits(m.fpr(0))) + ", porte " + hex(ps2::fbits(pv));
                break;
            }
            case 5: {
                const uint64_t before = cam.rail_velocity_reads;
                e.call(kFnLinVel, {kCam});
                const float* p = cam.GetAnchorLinearVelocity(w.ctx);
                rail_reads += cam.rail_velocity_reads - before;
                same_words("a velocidade linear", m.mem(uint32_t(m.gpr(R::v0)), 16), p, 16, {}, why);
                break;
            }
            case 6: {
                const uint32_t out = kScratchpad + 0x400;
                float po[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)};
                std::memcpy(m.mem(out, 16), po, 16);
                e.call(kFnAccel, {kCam, out});
                cam.GetAnchorAcceleration(w.ctx, po);
                same_words("a aceleração", m.mem(out, 16), po, 16, {}, why);
                break;
            }
            case 7: {
                e.call(kFnRenderOffset, {kCam});
                const float pv = cam.GetAnchorRenderOffset();
                if (ps2::fbits(m.fpr(0)) != ps2::fbits(pv))
                    why = "o deslocamento: original " + hex(ps2::fbits(m.fpr(0))) + ", porte " + hex(ps2::fbits(pv));
                break;
            }
            default: {
                e.call(kFnReset, {kCam});
                const bool pv = cam.GetAnchorResetAvailable();
                if ((m.gpr(R::v0) != 0) != pv) why = "o resultado";
                break;
            }
        }
        if (why.empty()) anchor_same(why);
        if (!why.empty()) t[q].fail(why + tag);
    }
    for (int i = 0; i < 9; ++i)
        if (i != 1 || t[1].cases) report(kNames[i], t[i], failures);
    std::printf("    (âncoras de corpo simples %" PRIu64 ", inválidas %" PRIu64 ", velocidades do trilho %" PRIu64 ")\n",
                simple_anchors, invalid, rail_reads);
    return failures;
}

// ---------------------------------------------------------------------------------------------- b) a câmera

int check_camera(World& w, drv::CameraTables& tables, drv::PlayerCamera& cam, int frames, Rng& rng) {
    int failures = 0;
    CamEmu& e = *w.e;
    MipsMachine& m = e.m;
    drv::Simulation& sim = *w.sim;
    drv::CameraContext& ctx = w.ctx;
    const drv::CameraTableGlobals& g = tables.tables;

    // Os modos que a conferência pede (os que o vanquish de Paris alcança; o Spline e o Fixed não foram portados).
    std::vector<int32_t> modes, heli_ids;
    std::string list;
    for (int32_t i = 0; i < g.mode_count; ++i) {
        const drv::CameraModeInfo& md = tables.mode(i);
        list += " " + std::to_string(i) + ":" + std::to_string(int(md.type)) + (md.selectable ? "s" : "") + (md.smooth_trans > 0 ? "~" : "") +
                (md.cam_id ? "#" + std::to_string(md.cam_id) : "");
        if (md.type == drv::kCamHeli || md.type == drv::kCamBumper || md.type == drv::kCamDashboard ||
            md.type == drv::kCamTumble || md.type == drv::kCamEllipse)
            modes.push_back(i);
        if (md.cam_id != 0 && md.type == drv::kCamHeli) heli_ids.push_back(md.cam_id);
    }
    if (heli_ids.empty()) heli_ids.push_back(0);
    std::printf("  câmera: os modos do vanquish (número:tipo, s selecionável, #id):%s\n", list.c_str());
    std::printf("    Tumble %d, a pausa %d, a animação relativa %d, a do mundo %d (Bumper %d, Dashboard %d, Ellipse %d)\n",
                int(g.tumble_index), int(g.pause_index), int(g.rel_anim_index), int(g.world_anim_index),
                int(g.bumper_count), int(g.dashboard_count), int(g.ellipse_count));

    // A âncora A anda; a B e os corpos simples perto dela.
    Pose pa, pb;
    auto place = [&](Pose& p) {
        const drv::WCollisionInstance& ci = w.tw->instances()[rng.next() % w.tw->instance_count()];
        float c[3];
        drv::WCollisionInstance_CalcPosition(w.tw->mgr(), ci, c);
        p.x = c[0] + rng.uni(-20, 20);
        p.y = c[1] + rng.uni(2.0f, 12.0f);
        p.z = c[2] + rng.uni(-20, 20);
        p.yaw = rng.uni(-3.14159f, 3.14159f);
        p.pitch = rng.chance(0.5) ? rng.uni(-0.25f, 0.25f) : 0.0;
        p.roll = rng.chance(0.3) ? rng.uni(-0.12f, 0.12f) : 0.0;
        p.speed = rng.uni(0.0f, 30.0f);
        p.yaw_rate = rng.uni(-0.6f, 0.6f);
        p.pitch_rate = rng.uni(-0.05f, 0.05f);
    };
    place(pa);
    pb = pa;
    pb.x += 6;
    pb.yaw_rate = rng.uni(-0.3f, 0.3f);
    auto put_bodies = [&]() {
        for (int k = 0; k < 2; ++k) {
            drv::RigidBody* rb = drv::GetRigidBody(sim, k);
            put_pose(k == 0 ? pa : pb, rb, nf::raw(rb->info));
        }
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
        m.write32(kWeaponMgr + 4, uint32_t(ctx.current_weapon));
    };
    // O RAnimEngine nos dois lados.
    drv::anim_engine_reset(60.0f);
    m.write32(kFrequency, ps2::fbits(drv::anim_engine().frequency));
    m.write32(kRate, ps2::fbits(drv::path_engine_clock().rate));
    m.write32(kAnimCount, 0);
    m.write32(kAnimLastTick, 0);
    drv::rtime_data().time = 0;
    sim.state = 0;
    sim.steps = 0;
    put_bodies();
    w.put_sim();
    put_mission();
    std::memset(m.mem(kCam, 0x370), 0, 0x370);
    e.call(kFnCameraCtor, {kCam});
    cam.construct(tables, ctx);
    m.write32(kWorldCamList + 4, kCam);
    drv::camera_events_env().camera = &cam;
    drv::camera_events_env().ctx = &ctx;
    drv::camera_events_env().anchor_for = [&w](drv::PhysicsObject* o) { return w.anchor_for(o); };
    // A âncora da câmera: o carro A (com os 'Cams' do modelo).
    e.call(kFnSetAnchor, {kCam, kCar});
    cam.SetAnchor(&w.anchor_a);

    auto handle_same = [&](std::string& out) -> bool {
        const uint32_t eh = m.read32(kCam + 0x104);
        if ((eh != 0) != (cam.d.anim_handle != 0)) {
            out = "o RAnimEngine::Handle (+0x104): original " + hex(eh) + ", porte " + std::to_string(cam.d.anim_handle);
            return false;
        }
        const drv::AnimEngineGlobals& ag = drv::anim_engine();
        if (ag.count != m.read32(kAnimCount) || ag.last_tick != m.read32(kAnimLastTick)) {
            out = "o RAnimEngine: a lista " + std::to_string(m.read32(kAnimCount)) + "/" + std::to_string(ag.count) +
                  ", o último tique " + std::to_string(m.read32(kAnimLastTick)) + "/" + std::to_string(ag.last_tick);
            return false;
        }
        if (eh == 0) return true;
        uint8_t* rec = cam.anim_record();
        if (!rec) {
            out = "o registro do Handle (o porte não tem)";
            return false;
        }
        uint8_t prec[0x40];
        std::memcpy(prec, rec, 0x40);
        uint32_t art;
        std::memcpy(&art, prec + 0x1c, 4);
        art = w.cc.to_emu(art);
        std::memcpy(prec + 0x1c, &art, 4);
        if (!same_words("o registro do Handle", m.mem(eh + 0x40, 0x40), prec, 0x40, {}, out)) return false;
        if (cam.engine_handle == 0) {
            // (o porte guarda o registro sem animação num Handle próprio: só o registro conta)
            if (ag.count != 0) {
                out = "o RAnimEngine com sistemas tocando sem Handle no porte";
                return false;
            }
            return true;
        }
        const uint32_t ph = cam.engine_handle;
        const drv::AnimHandleData& h = *nf::p32_ptr<drv::AnimHandleData>(ph);
        const uint32_t total = 0x40 + h.size;
        const uint8_t* p = nf::p32_ptr<uint8_t>(ph);
        for (uint32_t off = 0; off + 4 <= total; off += 4) {
            uint32_t pv, ev = m.read32(eh + off);
            std::memcpy(&pv, p + off, 4);
            const bool header_ptr = off >= 8 && off <= 0x18;
            const bool system_ptr = off >= h.systems - ph && off < h.systems - ph + uint32_t(h.n_systems) * 0x40 &&
                                    (off - (h.systems - ph)) % 0x40 == 0;
            const bool inst_art = off >= 0x40 && off < 0x40 + uint32_t(h.n_instances) * 0x40 && (off - 0x40) % 0x40 == 0x1c;
            if (header_ptr || system_ptr) pv = pv ? eh + (pv - ph) : 0;
            else if (inst_art) pv = w.cc.to_emu(pv);
            if (pv != ev) {
                out = "o Handle +0x" + hex(off).substr(4) + ": original " + hex(ev) + ", porte " + hex(pv);
                return false;
            }
        }
        for (uint32_t i = 0; i < ag.count; ++i)
            if (eh + (ag.active[i] - ph) != m.read32(kAnimList + i * 4)) {
                out = "a lista do RAnimEngine [" + std::to_string(i) + "]";
                return false;
            }
        return true;
    };
    auto compare = [&](std::string& out) -> bool {
        if (!same_words("RPlayerCamera", m.mem(kCam, 0x370), &cam.d, 0x370,
                        {{0xa8, 0xac}, {0xbc, 0xc0}, {0x104, 0x10c}, {0x110, 0x11c}, {0x2fc, 0x300}, {0x350, 0x370}}, out))
            return false;
        const uint32_t anims = cam.d.camera_anims == 0 ? 0u : w.cc.to_emu(cam.d.camera_anims);
        if (m.read32(kCam + 0x108) != anims) {
            out = "os 'Cams' (+0x108): original " + hex(m.read32(kCam + 0x108)) + ", porte " + hex(anims);
            return false;
        }
        if (m.read32(kCam + 0x118) != w.emu_of(cam.anchor_object())) {
            out = "a âncora (+0x118): original " + hex(m.read32(kCam + 0x118)) + ", porte " +
                  hex(w.emu_of(cam.anchor_object()));
            return false;
        }
        const uint32_t st = m.read32(kCam + 0x354);
        if (!same_words("RPlayerCamState", m.mem(st, 0x5c), &cam.state, 0x5c, {{0, 4}}, out)) return false;
        if (!same_words("o olho do tremor (0x333a20)", m.mem(kShakeEye, 16), cam.statics.shake_eye, 16, {}, out))
            return false;
        if (!same_words("os giros anteriores (0x333a30)", m.mem(kPrevRates, 8), cam.statics.prev_rates, 8, {}, out))
            return false;
        if (m.read32(kZoomButton) != uint32_t(cam.statics.zoom_button)) {
            out = "AICharacterHands::fgZoomButton (0x3276e0)";
            return false;
        }
        if (!same_words("os globais 0x3339d0", m.mem(kTables + 0x20, 0x1c), reinterpret_cast<const uint8_t*>(&g) + 0x20,
                        0x1c, {}, out))
            return false;
        // A fila do diretor.
        const uint32_t dq = m.read32(kCam + 0x368);
        const uint32_t head = m.read32(dq);
        std::vector<uint32_t> nodes;
        for (uint32_t n = m.read32(head); n != head && nodes.size() < 4096; n = m.read32(n)) nodes.push_back(n);
        if (nodes.size() != cam.director_queue.size()) {
            out = "a fila do diretor: original " + std::to_string(nodes.size()) + ", porte " +
                  std::to_string(cam.director_queue.size());
            return false;
        }
        for (size_t k = 0; k < nodes.size(); ++k) {
            const drv::DirectorQueueData& q = cam.director_queue[k];
            if (!same_words("a fila do diretor " + std::to_string(k), m.mem(nodes[k] + 8, 0x18), &q, 0x18, {}, out))
                return false;
            const uint32_t e18 = m.read32(nodes[k] + 8 + 0x18), e20 = m.read32(nodes[k] + 8 + 0x20);
            if ((e18 != 0) != (q.data18 != 0)) {
                out = "a fila do diretor " + std::to_string(k) + " +0x18 (o registro)";
                return false;
            }
            if (e18 != 0 && !same_words("o registro da fila " + std::to_string(k), m.mem(e18, 0x40),
                                        nf::p32_ptr<uint8_t>(q.data18), 0x40, {}, out))
                return false;
            if (e20 != w.emu_of(nf::p32_ptr<drv::CameraAnchor>(q.anchor))) {
                out = "a fila do diretor " + std::to_string(k) + " +0x20 (a âncora): original " + hex(e20) + ", porte " +
                      hex(w.emu_of(nf::p32_ptr<drv::CameraAnchor>(q.anchor)));
                return false;
            }
            if (m.read32(nodes[k] + 8 + 0x24) != q.data24 || m.read32(nodes[k] + 8 + 0x1c) != q.data1c) {
                out = "a fila do diretor " + std::to_string(k) + " (+0x1c/+0x24)";
                return false;
            }
        }
        if (m.mem(dq + 8, 1)[0] != cam.director_flags) {
            out = "as bandeiras do diretor";
            return false;
        }
        // O RCameraSpline.
        const uint32_t sp = m.read32(kCam + 0x350);
        if (!same_words("o RCameraSpline", m.mem(sp, 0x64), &cam.spline->d, 0x64, {{0x3c, 0x40}}, out)) return false;
        std::vector<uint32_t> pts;
        const uint32_t lh = m.read32(sp + 0x64);
        for (uint32_t n = m.read32(lh); n != lh && pts.size() < 256; n = m.read32(n)) pts.push_back(n);
        if (pts.size() != cam.spline->points.size()) {
            out = "a lista do RCameraSpline: original " + std::to_string(pts.size()) + ", porte " +
                  std::to_string(cam.spline->points.size());
            return false;
        }
        for (size_t k = 0; k < pts.size(); ++k)
            if (!same_words("o ponto " + std::to_string(k) + " do RCameraSpline", m.mem(pts[k] + 8, 16),
                            cam.spline->points[k].data(), 16, {}, out))
                return false;
        return handle_same(out);
    };
    Tally t_ctor;
    {
        ++t_ctor.cases;
        std::string out;
        if (!compare(out)) t_ctor.fail(out);
    }
    report("RPlayerCamera::RPlayerCamera e SetAnchor (vanquish)", t_ctor, failures);

    Tally t_frames, t_events;
    uint64_t n_input = 0, n_next = 0, n_look = 0, n_pauseoff = 0, n_pausecam = 0, n_mode = 0, n_caranim = 0,
             n_oldmode = 0, n_oldtrans = 0, n_end = 0, n_last = 0, n_reset = 0, n_97 = 0, n_pause = 0, n_simple = 0,
             n_force = 0, n_restart = 0, n_aim = 0;
    std::map<int, uint64_t> by_type;
    uint64_t f_rel = 0, f_relplay = 0, f_anchor_b = 0, f_anchor_s = 0, f_obj = 0;
    const uint64_t pending_before = drv::camera_pending();
    const uint64_t rel_before = cam.rel_anim_updates;
    std::string prev_log;
    int simple_slot = -1;  // a vaga do corpo simples de tipo 8 perto do olho
    // As animações relativas ao carro com transição só saindo de um modo sem transição suave e com a fila vazia (o pedido
    // é atendido neste quadro): com ela, o primeiro quadro do UpdateRelativeAnimationCam multiplica por uma matriz cujas
    // linhas 1 a 3 são o lixo da pilha do PS2 (o porte usa zeros; ver player_camera_views.cpp).
    // (o modo anterior que o DoSmoothModeChange olha é o que o DirectorChangeCameraMode deixa: o corrente, ou o anterior
    // saindo da câmera do míssil)
    auto nonsmooth = [&]() {
        int32_t old = cam.d.mode;
        if (old == g.missile_index) old = cam.d.prev_mode;  // (o +0x44 do RPlayerCamState: o ResetStateForAnimation o zera)
        int32_t prev = cam.d.prev_mode;
        if (old < g.mode_count && (tables.mode(old).type & 0xfedf) != 0) prev = old;
        const bool smooth_prev = prev < g.mode_count && tables.mode(prev).smooth_trans > 0;
        return !smooth_prev && cam.director_queue.empty() && (cam.d.mode_change & 4) == 0;
    };
    for (int f = 0; f < frames; ++f) {
        const double dt = 1.0 / 60.0;
        advance(pa, dt);
        advance(pb, dt);
        if (rng.chance(0.006)) {
            place(pa);
            pb = pa;
            pb.x += rng.uni(-8, 8);
            pb.z += rng.uni(-8, 8);
        }
        if (rng.chance(0.02)) {
            // O outro carro em cima do olho (a elipse em volta dele, CheckObjectCollisions).
            pb = pa;
            pb.x = cam.d.eye[0] + rng.uni(-1.5f, 1.5f);
            pb.y = cam.d.eye[1] + rng.uni(-1.0f, 0.5f);
            pb.z = cam.d.eye[2] + rng.uni(-1.5f, 1.5f);
        }
        put_bodies();
        if (rng.chance(0.97)) ++sim.steps;
        if (sim.state == 3 ? rng.chance(0.2) : rng.chance(0.01)) {
            sim.state = sim.state == 3 ? 0 : 3;
            if (sim.state == 3) ++n_pause;
        }
        if (rng.chance(0.01)) ctx.mission_state = rng.chance(0.2) ? 4 : 1;
        if (rng.chance(0.01)) ctx.mission_cinematic = rng.chance(0.5) ? 1u : 0u;
        // O carro capotado (o Tumble) às vezes, por pouco tempo.
        if (w.car_a->d.reset_available == 0 ? rng.chance(0.003) : rng.chance(0.03)) {
            w.car_a->d.reset_available = w.car_a->d.reset_available == 0 ? rng.range(1, 30) : 0;
            if (w.car_a->d.reset_available) ++n_reset;
        }
        if (rng.chance(0.005)) w.car_b->d.reset_available = rng.chance(0.6) ? 0 : rng.range(1, 30);
        if (rng.chance(0.01)) {
            g_vehicle = rng.chance(0.5) ? &w.vehicle : nullptr;
            w.vehicle.spline_path = rng.chance(0.7) ? nf::p32_u(&w.spline) : 0u;
        }
        if (rng.chance(0.05))
            for (int k = 0; k < 3; ++k) w.path_handle.velocity[k] = float(nf::raw(drv::GetRigidBody(sim, 0)->info) ? 1 : 0) * rng.uni(-30, 30);
        if (rng.chance(0.01))
            for (float& r : w.render_offset) r = rng.chance(0.2) ? 0.0f : rng.uni(-1.0f, 1.5f);
        w.anchor_a.render_offset = w.render_offset[0];
        w.anchor_b.render_offset = w.render_offset[1];
        for (auto& a : w.simple_anchors) a->render_offset = w.render_offset[2];
        // Os corpos simples: um de tipo 8 às vezes perto do olho, os outros parados.
        if (rng.chance(0.02)) {
            if (simple_slot >= 0 && rng.chance(0.3)) {
                sim.simple_owners[simple_slot] = nullptr;
                simple_slot = -1;
            } else {
                simple_slot = rng.range(0, 47);
                sim.simple_owners[simple_slot] = w.simple_owners[size_t(simple_slot)].get();
                ++n_simple;
            }
        }
        if (simple_slot >= 0 && rng.chance(0.3)) {
            w.random_simple(rng, simple_slot, cam.d.eye, 3.0f);
            if (rng.chance(0.7)) drv::GetSimpleRigidBody(sim, simple_slot)->type = 8;
        }
        drv::rtime_data().time = uint32_t(sim.steps);
        w.put_sim();
        put_mission();
        drv::trigger_manager().pending.clear();
        e.events.clear();
        // Os pedidos.
        std::string flog;
        if (prev_log.size() > 600) prev_log.erase(0, prev_log.size() - 600);
        const int k_inputs = rng.chance(0.25) ? rng.range(1, 2) : 0;
        for (int k = 0; k < k_inputs; ++k) {
            static const int32_t kActions[] = {1, 1, 0x1b, 0x1c, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2a, 0x2c, 5, 0x2b};
            const int32_t act = kActions[rng.next() % (sizeof kActions / sizeof kActions[0])];
            const float v = rng.chance(0.2) ? 0.0f : rng.uni(-1.0f, 1.0f);
            m.write32(kIcm + 4, 0);
            e.call(kFnInput, {kCam, uint32_t(act)}, v);
            cam.CameraInputCallback(ctx, act, v, 0);
            flog += " entrada 0x" + hex(uint32_t(act)).substr(6);
            ++n_input;
        }
        if (rng.chance(0.01)) {
            const uint16_t delay = uint16_t(rng.range(0, 4));
            e.call(kFnNext, {kCam, delay});
            cam.NextCameraMode(delay);
            ++n_next;
            flog += " NextCameraMode";
        }
        if (rng.chance(0.01)) {
            const int32_t on = rng.chance(0.5) ? 1 : 0;
            e.call(kFnLookBack, {kCam, uint32_t(on)});
            cam.SetCameraLookBack(on);
            ++n_look;
            flog += " SetCameraLookBack";
        }
        if (rng.chance(0.005)) {
            e.call(kFnPauseOff, {kCam});
            cam.PauseOff();
            ++n_pauseoff;
            flog += " PauseOff";
        }
        if (rng.chance(cam.director_queue.size() > 3 ? 0.02 : 0.004)) {
            e.call(kFnPauseCam, {kCam});
            cam.SetPauseCamera();
            ++n_pausecam;
            flog += " SetPauseCamera";
        }
        if (!modes.empty() && rng.chance(0.03)) {
            const int32_t md = modes[rng.next() % modes.size()];
            const uint16_t delay = uint16_t(rng.chance(0.6) ? 0 : rng.range(1, 6));
            const uint16_t flags = uint16_t(rng.chance(0.5) ? 0 : (rng.chance(0.5) ? 4 : rng.range(0, 3)));
            const uint16_t transition = uint16_t(rng.chance(0.5) ? 0 : rng.range(1, 40));
            e.call(kFnModeByIndex, {kCam, uint32_t(md), delay, flags, transition, 0, 0, 0});
            cam.SetCameraModeByIndex(md, delay, flags, transition, 0, 0, nullptr);
            ++n_mode;
            flog += " modo " + std::to_string(md);
        }
        if (w.cams_count && rng.chance(0.006)) {
            // A animação de um registro do modelo (o sistema da primeira animação; às vezes um que não existe), com a
            // âncora A, a B ou um corpo simples.
            const uint32_t id = rng.chance(0.9) ? w.cams_systems[rng.next() % w.cams_systems.size()]
                                                : uint32_t(rng.range(0, 60));
            const uint16_t transition = uint16_t(rng.chance(0.6) || !nonsmooth() ? 0 : rng.range(1, 40));
            const uint16_t delay = uint16_t(transition || rng.chance(0.6) ? 0 : rng.range(1, 6));
            drv::CameraAnchor* a = &w.anchor_a;
            const int pick = rng.range(0, 9);
            if (pick < 3) a = &w.anchor_b;
            else if (pick < 4) {
                if (simple_slot < 0) {
                    simple_slot = rng.range(0, 47);
                    sim.simple_owners[simple_slot] = w.simple_owners[size_t(simple_slot)].get();
                    w.random_simple(rng, simple_slot, cam.d.eye, 6.0f);
                    w.put_sim();
                }
                a = w.simple_anchors[size_t(simple_slot)].get();
            }
            else if (pick < 5) a = nullptr;
            const uint16_t flags = uint16_t(rng.chance(0.7) ? 0 : rng.range(0, 3));
            e.call(kFnCarAnim, {kCam, id, transition, delay, w.emu_of(a), flags});
            cam.TriggerCarAnimationCamera(id, transition, delay, a, flags);
            ++n_caranim;
            flog += " TriggerCarAnimationCamera(" + std::to_string(id) + ")";
        }
        if (!modes.empty() && rng.chance(0.015)) {
            const int32_t md = modes[rng.next() % modes.size()];
            const uint16_t delay = uint16_t(rng.chance(0.6) ? 0 : rng.range(1, 5));
            const uint32_t transition = uint32_t(rng.chance(0.4) ? rng.range(0, 1) : rng.range(2, 60));
            const uint32_t arg4 = uint32_t(rng.range(0, 1));
            e.call(kFnOldMode, {kCam, uint32_t(md), delay, transition, arg4});
            cam.SetOldCameraMode(md, delay, transition, arg4);
            ++n_oldmode;
            if (transition >= 2) ++n_oldtrans;
            flog += " SetOldCameraMode(" + std::to_string(md) + ", " + std::to_string(transition) + ")";
        }
        if (rng.chance(cam.d.mode == g.rel_anim_index ? 0.03 : 0.006)) {
            const uint16_t delay = uint16_t(rng.range(0, 5));
            const uint32_t transition = uint32_t(rng.chance(0.5) ? rng.range(0, 1) : rng.range(2, 30));
            const int16_t id = rng.chance(0.6) ? int16_t(0) : int16_t(heli_ids[rng.next() % heli_ids.size()]);
            e.call(kFnEndAnim, {kCam, delay, transition, 0, uint32_t(int32_t(id))});
            cam.EndCameraAnim(ctx, delay, transition, 0, id);
            ++n_end;
            flog += " EndCameraAnim";
        }
        // Um ciclo a cada 200 quadros: o RestartCamera (0x1bbc90; a animação do vanquish não acaba sozinha, o estímulo do
        // PlayCurrentAnimation a recomeça, e prende a fila do diretor), depois a animação do modelo com uma âncora sorteada
        // (o DirectorSetAnchor) no quadro 140.
        const int phase = f % 200;
        if (phase == 0 && f > 0) {
            e.call(0x1bbc90, {kCam});
            cam.restart(ctx);
            ++n_restart;
            flog += " RestartCamera (ciclo)";
        }
        if (phase == 140 && w.cams_count) {
            drv::CameraAnchor* a = rng.chance(0.4) ? &w.anchor_a : &w.anchor_b;
            if (rng.chance(0.25)) {
                if (simple_slot < 0) {
                    simple_slot = rng.range(0, 47);
                    sim.simple_owners[simple_slot] = w.simple_owners[size_t(simple_slot)].get();
                    w.random_simple(rng, simple_slot, cam.d.eye, 6.0f);
                    w.put_sim();
                }
                a = w.simple_anchors[size_t(simple_slot)].get();
            }
            const uint32_t id = w.cams_systems[rng.next() % w.cams_systems.size()];
            const uint16_t transition = uint16_t(rng.chance(0.5) || !nonsmooth() ? 0 : rng.range(1, 40));
            e.call(kFnCarAnim, {kCam, id, transition, 0, w.emu_of(a), 0});
            cam.TriggerCarAnimationCamera(id, transition, 0, a, 0);
            ++n_caranim;
            flog += " TriggerCarAnimationCamera (ciclo)";
        }
        // O ForceCameraChange (0x1bd1b8) num modo Heli com id, também durante a animação (o pedido espera).
        if (cam.d.mode == g.rel_anim_index ? rng.chance(0.012) : rng.chance(0.002)) {
            const int16_t id = int16_t(heli_ids[rng.next() % heli_ids.size()]);
            const uint16_t delay = uint16_t(rng.chance(0.6) ? 0 : rng.range(1, 5));
            const uint32_t transition = uint32_t(rng.chance(0.5) ? 0 : rng.range(1, 30));
            e.call(0x1bd1b8, {kCam, uint32_t(int32_t(id)), delay, transition});
            cam.ForceCameraChange(id, delay, transition);
            ++n_force;
            flog += " ForceCameraChange(" + std::to_string(id) + ")";
        }
        if (rng.chance(0.01)) {
            // A mira do fuzil (o AimZoom e o AimRelease do RPlayerCamState: o +0x354), com o míssil guiado às vezes.
            const bool zoom = rng.chance(0.5);
            if (rng.chance(0.1)) ctx.mission_missile = rng.chance(0.5) ? 0x1234u : 0u;
            m.write32(kMission + 0x358, ctx.mission_missile);
            e.call(zoom ? 0x1be570u : 0x1be660u, {m.read32(kCam + 0x354)});
            if (zoom) cam.AimZoom(ctx);
            else cam.AimRelease(ctx);
            if (m.read32(kAudio + 0xc0) != uint32_t(w.audio_zoom))
                t_frames.fail("o som da mira (AVehicle +0xc0) no quadro " + std::to_string(f));
            ++n_aim;
            flog += zoom ? " AimZoom" : " AimRelease";
        }
        if (rng.chance(0.004)) {
            const uint16_t delay = uint16_t(rng.range(0, 5));
            const uint32_t transition = uint32_t(rng.chance(0.5) ? rng.range(0, 1) : rng.range(2, 30));
            const int32_t keep = rng.chance(0.5) ? 1 : 0;
            e.call(kFnLastSel, {kCam, delay, transition, 0, uint32_t(keep)});
            cam.SetLastSelectableCameraMode(delay, transition, 0, keep);
            ++n_last;
            flog += " SetLastSelectable";
        }
        if (w.cams_count && nonsmooth() && rng.chance(0.03)) {
            // O destrutor do 97 EPlayCarCameraAnim: o elemento (nenhum: o carro do jogador; um que não é carro: o dono
            // da vaga +0x18, rígida ou simples pelo +0x14), a transição e a espera em segundos, a animação.
            uint32_t o[8] = {};
            o[0] = 0x370b88u;
            o[1] = rng.chance(0.6) ? 0u : 0x1234u;
            o[2] = rng.chance(0.3) ? uint32_t(rng.range(0, 1)) : uint32_t(rng.range(2, 120));
            o[3] = 0;
            o[4] = rng.chance(0.9) ? w.cams_systems[rng.next() % w.cams_systems.size()] : uint32_t(rng.range(0, 60));
            o[5] = rng.chance(0.7) ? 0u : 1u;
            o[6] = o[5] == 0 ? uint32_t(rng.range(0, 1)) : uint32_t(simple_slot >= 0 ? simple_slot : rng.range(0, 47));
            if (o[5] != 0 && !sim.simple_owners[o[6]]) o[5] = 0, o[6] = 0;
            for (int i = 0; i < 8; ++i) m.write32(kEvent + 4u * uint32_t(i), o[i]);
            e.call(kFnCarAnimDtor, {kEvent, 0});
            drv::PendingEvent pe;
            pe.type = 97;
            for (int i = 0; i < 8; ++i) pe.obj[i] = o[i];
            drv::camera_event_execute(pe);
            ++n_97;
            flog += " evento 97";
        }
        // O RAnimEngine (o EAnimUpdate) e o UpdateCamera.
        if (sim.state != 3) {
            const uint32_t now = uint32_t(sim.steps);
            e.call(kFnAnimUpdate, {now});
            drv::RAnimEngine_Update(now);
        }
        w.put_sim();
        e.call(kFnUpdate, {kCam});
        cam.update(ctx);
        ++t_frames.cases;
        prev_log += " |" + std::to_string(f) + " m" + std::to_string(cam.d.mode) + flog;
        by_type[int(tables.mode(cam.d.mode).type)]++;
        if (cam.d.mode == g.rel_anim_index) {
            ++f_rel;
            if (cam.engine_handle) ++f_relplay;
        }
        if (cam.anchor_object() == &w.anchor_b) ++f_anchor_b;
        if (cam.anchor_object() && (cam.anchor_object()->flags & 1)) ++f_anchor_s;
        f_obj += cam.object_hits;
        std::string out;
        if (out.empty() && e.events.size() != drv::trigger_manager().pending.size())
            out = "os eventos das chaves: original " + std::to_string(e.events.size()) + ", porte " +
                  std::to_string(drv::trigger_manager().pending.size());
        if (!compare(out) || !out.empty()) {
            if (t_frames.bad == 0 && std::getenv("NF_CAM26_DEBUG")) {
                const uint8_t* eb = m.mem(kCam, 0x370);
                const uint8_t* pb2 = reinterpret_cast<const uint8_t*>(&cam.d);
                for (uint32_t o = 0; o < 0x370; o += 4) {
                    uint32_t x, y;
                    std::memcpy(&x, eb + o, 4);
                    std::memcpy(&y, pb2 + o, 4);
                    if (x != y) std::printf("    +0x%03x original %08x porte %08x\n", o, x, y);
                }
                std::printf("    pedidos dos quadros anteriores:%s\n", prev_log.c_str());
                const uint32_t eh = m.read32(kCam + 0x104);
                if (eh && cam.anim_record()) {
                    std::printf("    o registro do Handle (original / porte):\n");
                    for (uint32_t o = 0; o < 0x40; o += 4) {
                        uint32_t y;
                        std::memcpy(&y, cam.anim_record() + o, 4);
                        std::printf("      +0x%02x %08x %08x\n", o, m.read32(eh + 0x40 + o), y);
                    }
                }
            }
            t_frames.fail("quadro " + std::to_string(f) + " (modo " + std::to_string(cam.d.mode) + ";" + flog + "): " + out);
            if (t_frames.bad > 10) break;
        }
        // O w do olho e o do braço suavizado: o lixo da pilha em alguns caminhos do PS2 (o olhar para trás do Bumper e do
        // Dashboard, os modos sem ruído); o do porte nos dois lados.
        e.put_f(kCam + 0xbc, cam.d.eye[3]);
        e.put_f(kCam + 0x2fc, cam.d.offset[3]);
    }
    char what[200];
    std::snprintf(what, sizeof what, "UpdateCamera (o vanquish: %" PRIu64 " entradas, %" PRIu64 " modos pedidos)", n_input,
                  n_mode);
    report(what, t_frames, failures);
    std::string types;
    for (const auto& [ty, n] : by_type) types += " " + std::to_string(ty) + ":" + std::to_string(n);
    std::printf("    quadros por tipo de modo:%s; animação relativa %" PRIu64 " (tocando %" PRIu64
                "), âncora B %" PRIu64 ", âncora de corpo simples %" PRIu64 "\n",
                types.c_str(), f_rel, f_relplay, f_anchor_b, f_anchor_s);
    std::printf("    pedidos: NextCameraMode %" PRIu64 ", SetCameraLookBack %" PRIu64 ", PauseOff %" PRIu64
                ", SetPauseCamera %" PRIu64 ", TriggerCarAnimationCamera %" PRIu64 ", SetOldCameraMode %" PRIu64
                " (com transição %" PRIu64 "), EndCameraAnim %" PRIu64 ", SetLastSelectable %" PRIu64
                ", eventos 97 %" PRIu64 ", ForceCameraChange %" PRIu64 ", RestartCamera %" PRIu64 ", miras %" PRIu64
                ", reposições %" PRIu64
                ", pausas %" PRIu64
                ", corpos simples perto %" PRIu64
                "\n",
                n_next, n_look, n_pauseoff, n_pausecam, n_caranim, n_oldmode, n_oldtrans, n_end, n_last, n_97, n_force, n_restart, n_aim, n_reset,
                n_pause, n_simple);
    std::printf("    (UpdateRelativeAnimationCam %" PRIu64 ", desvios em volta de objetos %" PRIu64
                ", pendentes do port %" PRIu64 ")\n",
                cam.rel_anim_updates - rel_before, cam.ellipse_hits, drv::camera_pending() - pending_before);
    (void)t_events;
    (void)f_obj;
    if (drv::camera_pending() != pending_before) ++failures;
    return failures;
}

// ---------------------------------------------------------------------------------------------- c) os gatilhos

struct TrigCopy {
    drv::UData* trgr = nullptr;
    int count = 0;
    uint32_t data = 0, mgr = 0;
    std::map<uint32_t, uint32_t> lists;  // a lista 'el' do porte (p32) -> o endereço emulado
    uint32_t list_emu(uint32_t p32) const {
        if (!p32) return 0;
        auto it = lists.find(p32);
        return it == lists.end() ? 0xdead0000u : it->second;
    }
    uint32_t trig_emu(int i) const { return data + 0x40u * uint32_t(i); }
    int index_emu(uint32_t a) const { return a ? int((a - data) / 0x40u) : -1; }
    int index_port(uint32_t p32) const { return p32 ? int((p32 - drv::trigger_manager().data) / 0x40u) : -1; }
    uint32_t data_emu(uint32_t p32) const {
        auto it = lists.upper_bound(p32);
        if (it == lists.begin()) return 0xdead0000u;
        --it;
        return it->second + (p32 - it->first);
    }
};

void collect_event_lists(drv::UGroup* g, std::map<uint32_t, uint32_t>& out, int depth) {
    if (depth > 24) return;
    drv::UData* arr = drv::UGroup_GetArray(g);
    const uint32_t groups = drv::UGroup_GroupCount(g);
    for (uint32_t i = 0; i < groups; ++i) collect_event_lists(arr + i, out, depth + 1);
    for (drv::UData* d = arr + groups; d < arr + groups + g->count; ++d)
        if (drv::UData_MatchTag(d) == 0x656c2020u) out[nf::p32_u(drv::UData_Data(d))] = drv::UData_Size(d);
}

int check_triggers(World& w, int cases, Rng& rng) {
    int failures = 0;
    CamEmu& e = *w.e;
    MipsMachine& m = e.m;
    drv::Simulation& sim = *w.sim;
    TrigCopy tc;
    drv::UGroup* root = w.track->root();
    drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
    drv::UData* trgr = drv::UGroup_DataLocateTag(map, 0x54726772u);  // 'Trgr'
    if (trgr == drv::UGroup_DataEnd(map)) {
        std::printf("  os gatilhos: Paris não tem 'Trgr'\n");
        return 1;
    }
    tc.trgr = trgr;
    tc.count = int(trgr->count);
    const uint32_t size = drv::UData_Size(trgr);
    drv::WTriggerManager_Init(trgr);
    drv::TriggerManagerGlobals& g = drv::trigger_manager();
    g.pending.clear();
    {
        std::map<uint32_t, uint32_t> sizes;
        collect_event_lists(root, sizes, 0);
        tc.data = e.alloc(size);
        for (int i = 0; i < tc.count; ++i) {
            const drv::WTrigger* t = drv::trigger_at(i);
            if (!t->events || tc.lists.count(t->events)) continue;
            auto it = sizes.find(t->events);
            if (it == sizes.end()) {
                std::printf("  os gatilhos: a lista de eventos do gatilho %d não é um 'el'\n", i);
                return 1;
            }
            const uint32_t a = e.alloc(it->second + 16) + (t->events & 15u);
            put(m, a, nf::p32_ptr<uint8_t>(t->events), it->second);
            tc.lists[t->events] = a;
        }
        tc.mgr = e.alloc(16);
    }
    std::function<uint32_t(uint32_t)> inst_map;
    auto put_triggers = [&]() {
        for (int i = 0; i < tc.count; ++i) {
            drv::WTrigger t = *drv::trigger_at(i);
            t.events = tc.list_emu(t.events);
            put(m, tc.trig_emu(i), &t, 0x40);
        }
        m.write32(kTrigManagerPtr, tc.mgr);
        m.write32(tc.mgr, uint32_t(g.count));
        m.write32(tc.mgr + 4, tc.data);
        m.write32(tc.mgr + 8, uint32_t(g.active));
        m.write32(kTrigSizeG, g.data_size);
        m.write32(kTrigDataG, tc.data);
        m.write32(kTrigStamp, g.query_stamp);
        drv::EventDynamicData d = g.event_data;
        d.instance = inst_map(d.instance);
        d.trigger = d.trigger ? tc.trig_emu(tc.index_port(d.trigger)) : 0u;
        std::memcpy(m.mem(kEventData, 0x64), &d, 0x64);
        m.write32(kRayCount, uint32_t(drv::rayshell_globals().num_active));
        m.write32(kRayIndex, uint32_t(drv::rayshell_globals().trigger_index));
    };
    // As instâncias movidas pelos RPathHandle: falsas (a bandeira 0x10, o estado de caminho com um 'pt' de Paris), em
    // memória do porte e na emulada.
    struct Inst {
        alignas(16) uint8_t bytes[0x40];
        alignas(16) drv::ProcAnimState state;
        drv::RPathHandle* handle;
        uint32_t emu, emu_handle;
    };
    std::vector<std::unique_ptr<Inst>> insts;
    uint32_t path = 0;
    for (uint32_t i = 1; i < 0x100 && !path; ++i) {
        drv::UData* d = drv::UGroup_DataLocateTag(map, 0x70740000u | i);
        if (d != drv::UGroup_DataEnd(map)) path = nf::p32_u(drv::UData_Data(d));
    }
    drv::RPathEngine_Purge();
    for (int i = 0; path && i < 6; ++i) {
        auto in = std::make_unique<Inst>();
        std::memset(in->bytes, 0, sizeof in->bytes);
        const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(in->bytes, id, 64);
        in->bytes[0xc] = 0x10;
        in->state = drv::ProcAnimState{};
        in->state.type = 2;
        in->state.path = path;
        in->state.speed = 0.5f;
        in->handle = drv::RPathEngine_CreatePathHandle(nf::p32_u(in->bytes), nf::p32_u(&in->state), 0.0f);
        if (!in->handle) break;
        in->emu = e.alloc(0x40);
        in->emu_handle = e.alloc(0xd4);
        insts.push_back(std::move(in));
    }
    auto inst_emu = [&](uint32_t p32) -> uint32_t {
        for (const auto& in : insts)
            if (nf::p32_u(in->bytes) == p32) return in->emu;
        return p32;
    };
    inst_map = inst_emu;
    auto put_insts = [&]() {
        e.path_handles.clear();
        for (const auto& in : insts) {
            put(m, in->emu, in->bytes, 0x40);
            m.write32(in->emu_handle, in->emu);
            m.write32(in->emu_handle + 0xa0, in->handle->master);
            e.path_handles.push_back(in->emu_handle);
        }
    };
    auto compare = [&]() -> std::string {
        std::string out;
        auto eq = [&](const char* what, uint32_t a, uint32_t b) {
            if (out.empty() && a != b) out = std::string(what) + ": original " + hex(a) + ", porte " + hex(b);
        };
        eq("QueryStamp", m.read32(kTrigStamp), g.query_stamp);
        if (!out.empty()) return out;
        for (int i = 0; i < tc.count; ++i)
            if (!same_words("WTrigger", m.mem(tc.trig_emu(i), 0x40), drv::trigger_at(i), 0x40, {{0x18, 0x1c}}, out))
                return out + " (gatilho " + std::to_string(i) + ")";
        drv::EventDynamicData d = g.event_data;
        d.instance = inst_map(d.instance);
        if (!same_words("EventDynamicData", m.mem(kEventData, 0x64), &d, 0x64, {{0x30, 0x34}}, out)) return out;
        if (tc.index_emu(m.read32(kEventData + 0x30)) != tc.index_port(g.event_data.trigger))
            return "EventDynamicData +0x30 (o gatilho)";
        if (e.events.size() != g.pending.size())
            return "eventos criados: " + std::to_string(e.events.size()) + " no original, " +
                   std::to_string(g.pending.size()) + " no porte";
        for (size_t i = 0; i < e.events.size(); ++i)
            if (e.events[i].first != g.pending[i].type || e.events[i].second != tc.data_emu(g.pending[i].data))
                return "o evento " + std::to_string(i) + ": " + std::to_string(e.events[i].first) + " no original, " +
                       std::to_string(g.pending[i].type) + " no porte";
        return out;
    };
    // Um gatilho com as bandeiras dos corpos simples e das instâncias às vezes trocadas.
    auto mutate = [&](drv::WTrigger& t) {
        static const uint16_t kBits[] = {0x0001, 0x0002, 0x0008, 0x0010, 0x0040, 0x0080, 0x0200, 0x0400, 0x0800, 0x2000};
        for (int k = rng.range(0, 3); k > 0; --k) t.flags ^= kBits[rng.next() % (sizeof kBits / sizeof kBits[0])];
        if (rng.chance(0.15)) {
            t.radius = rng.chance(0.3) ? 0.0f : rng.uni(0.5f, 20);
            t.height = rng.uni(0.2f, 10);
        }
        if (rng.chance(0.1)) {
            const float y = rng.uni(-3.14159f, 3.14159f);
            t.forward[0] = std::sin(y);
            t.forward[1] = 0;
            t.forward[2] = std::cos(y);
        }
    };
    auto near = [&](const drv::WTrigger& t, float out[3], float extra) {
        const float reach = std::min(t.radius + extra, 200.0f);
        const float a = rng.uni(-3.14159f, 3.14159f), d = rng.uni(0, reach * 1.2f);
        out[0] = t.position[0] + std::cos(a) * d;
        out[2] = t.position[2] + std::sin(a) * d;
        out[1] = t.position[1] + rng.uni(-3.0f, std::max(3.0f, t.height + 3.0f));
    };
    Tally t_cs, t_cp, t_cp3, t_ps, t_pi, t_up;
    uint64_t fired = 0, owned = 0;
    const drv::TriggerStats before = drv::trigger_stats();
    for (int c = 0; c < cases; ++c) {
        const std::string tag = " (caso " + std::to_string(c) + ")";
        for (int k = rng.range(0, 3); k > 0; --k) mutate(*drv::trigger_at(rng.range(0, tc.count - 1)));
        if (rng.chance(0.1)) g.active = rng.chance(0.5) ? 1 : 0;
        if (rng.chance(0.03)) g.query_stamp = rng.next();
        // Os corpos simples (as vagas dadas aos donos às vezes) perto de um gatilho.
        for (int i = 0; i < drv::kSimpleBodySlots; ++i) sim.simple_owners[i] = nullptr;
        const int n_simple = rng.range(1, 6);
        std::vector<int> slots;
        for (int k = 0; k < n_simple; ++k) {
            const int s = rng.range(0, drv::kSimpleBodySlots - 1);
            float p[3];
            near(*drv::trigger_at(rng.range(0, tc.count - 1)), p, 5.0f);
            w.random_simple(rng, s, p, 1.0f);
            drv::SimpleRigidBody& b = *drv::GetSimpleRigidBody(sim, s);
            if (rng.chance(0.7)) b.flags |= 0x100;
            sim.simple_owners[s] = w.simple_owners[size_t(s)].get();
            slots.push_back(s);
        }
        // Os corpos rígidos (o Update os passa também): longe ou perto, acordados ou não.
        for (int k = 0; k < 2; ++k) {
            drv::RigidBody* rb = drv::GetRigidBody(sim, k);
            float p[3];
            near(*drv::trigger_at(rng.range(0, tc.count - 1)), p, 5.0f);
            for (int i = 0; i < 3; ++i) rb->pos[i] = p[i];
            for (int i = 0; i < 3; ++i) rb->vel[i] = rng.uni(-20, 20);
            rb->state = int8_t(rng.chance(0.6) ? 2 : 1);
        }
        // As instâncias.
        for (const auto& in : insts) {
            float p[3];
            near(*drv::trigger_at(rng.range(0, tc.count - 1)), p, 10.0f);
            std::memcpy(in->bytes + 0x30, p, 12);
            uint32_t word = rng.next();
            if (rng.chance(0.5)) word &= ~0x40000000u;  // os passos de 0,25 (os de 16 cobrem tudo)
            std::memcpy(in->bytes + 0x3c, &word, 4);
            in->handle->master = rng.chance(0.2) ? 0x1234u : 0u;
        }
        w.put_sim();
        put_triggers();
        put_insts();
        e.events.clear();
        g.pending.clear();
        const uint64_t fired0 = drv::trigger_stats().fired;
        const int op = rng.range(0, 99);
        Tally* t = nullptr;
        std::string why;
        if (op < 15) {
            t = &t_cs;
            ++t->cases;
            const int s = slots[rng.next() % slots.size()];
            const int ti = rng.range(0, tc.count - 1);
            e.call(kFnTrigCheckSimple, {tc.mgr, kSrbVec + 0x40u * uint32_t(s), tc.trig_emu(ti)});
            const bool a = (m.gpr(R::v0) & 0xff) != 0;
            const bool b = drv::WTriggerManager_CheckCollideSimple(*drv::GetSimpleRigidBody(sim, s), *drv::trigger_at(ti));
            if (a != b) why = std::string("o resultado: original ") + (a ? "1" : "0") + ", porte " + (b ? "1" : "0");
        } else if (op < 28 && !insts.empty()) {
            t = &t_cp;
            ++t->cases;
            const Inst& in = *insts[rng.next() % insts.size()];
            const int ti = rng.range(0, tc.count - 1);
            const float r = rng.uni(0, 20);
            e.call(kFnTrigCheckPoint, {tc.mgr, in.emu + 0x30, tc.trig_emu(ti)}, r);
            const bool a = (m.gpr(R::v0) & 0xff) != 0;
            float pos[3];
            std::memcpy(pos, in.bytes + 0x30, 12);
            const bool b = drv::WTriggerManager_CheckCollidePoint(r, pos, *drv::trigger_at(ti));
            if (a != b) why = std::string("o resultado: original ") + (a ? "1" : "0") + ", porte " + (b ? "1" : "0");
        } else if (op < 41 && !insts.empty()) {
            t = &t_cp3;
            ++t->cases;
            const Inst& in = *insts[rng.next() % insts.size()];
            const int ti = rng.range(0, tc.count - 1);
            const float r = rng.uni(0, 20), up = rng.uni(0, 10), down = rng.uni(0, 10);
            e.call(kFnTrigCheckPoint3, {tc.mgr, in.emu + 0x30, tc.trig_emu(ti)}, r, up, down);
            const bool a = (m.gpr(R::v0) & 0xff) != 0;
            float pos[3];
            std::memcpy(pos, in.bytes + 0x30, 12);
            const bool b = drv::WTriggerManager_CheckCollidePoint3(r, up, down, pos, *drv::trigger_at(ti));
            if (a != b) why = std::string("o resultado: original ") + (a ? "1" : "0") + ", porte " + (b ? "1" : "0");
        } else if (op < 70) {
            t = &t_ps;
            ++t->cases;
            const int s = slots[rng.next() % slots.size()];
            e.call(kFnTrigProcessSimple, {tc.mgr, uint32_t(s), kSrbVec + 0x40u * uint32_t(s)});
            drv::WTriggerManager_ProcessSimple(s, *drv::GetSimpleRigidBody(sim, s));
            if (w.simple_owners[size_t(s)]->word58 == sim.rigid_sig[0]) ++owned;
        } else if (op < 88 && !insts.empty()) {
            t = &t_pi;
            ++t->cases;
            const Inst& in = *insts[rng.next() % insts.size()];
            e.call(kFnTrigProcessInstance, {tc.mgr, in.emu});
            drv::WTriggerManager_ProcessInstance(nf::p32_u(in.bytes));
        } else {
            t = &t_up;
            ++t->cases;
            e.call(kFnTrigUpdate, {tc.mgr});
            drv::WTriggerManager_Update();
        }
        fired += drv::trigger_stats().fired - fired0;
        if (why.empty()) why = compare();
        if (!why.empty()) t->fail(why + tag);
    }
    g.pending.clear();
    report("WTriggerManager::CheckCollide (corpo simples)", t_cs, failures);
    report("WTriggerManager::CheckCollide (ponto e raio)", t_cp, failures);
    report("WTriggerManager::CheckCollide (ponto, raio, acima, abaixo)", t_cp3, failures);
    report("WTriggerManager::Process (corpo simples)", t_ps, failures);
    report("WTriggerManager::Process (instância de um RPathHandle)", t_pi, failures);
    report("WTriggerManager::Update (rígidos, simples, instâncias)", t_up, failures);
    const drv::TriggerStats& after = drv::trigger_stats();
    std::printf("    (gatilhos disparados %" PRIu64 "; corpos simples conferidos %" PRIu64 ", tocaram %" PRIu64
                ", do jogador %" PRIu64 "; instâncias conferidas %" PRIu64 ", tocaram %" PRIu64 ")\n",
                fired, after.simple_checks - before.simple_checks, after.simple_hits - before.simple_hits, owned,
                after.instance_checks - before.instance_checks, after.instance_hits - before.instance_hits);
    for (int i = 0; i < drv::kSimpleBodySlots; ++i) sim.simple_owners[i] = nullptr;
    drv::RPathEngine_Purge();
    e.path_handles.clear();
    return failures;
}

// ---------------------------------------------------------------------------------------------- d) a luneta

int check_sniper(World& w, int cases, Rng& rng) {
    int failures = 0;
    CamEmu& e = *w.e;
    MipsMachine& m = e.m;
    Tally t_ring, t_verts;
    drv::SniperZoomData& z = *new drv::SniperZoomData();
    drv::RSniperZoom_Construct(z);
    for (int c = 0; c < cases; ++c) {
        const std::string tag = " (caso " + std::to_string(c) + ")";
        drv::SniperZoomView v;
        v.screen_w = rng.chance(0.7) ? 640 : rng.range(320, 1920);
        v.screen_h = rng.chance(0.7) ? 448 : rng.range(200, 1080);
        v.cursor_x = rng.chance(0.2) ? 0.0f : rng.uni(-200, 200);
        v.cursor_y = rng.chance(0.2) ? 0.0f : rng.uni(-150, 150);
        m.write32(kRendererObj + 0x50, uint32_t(v.screen_w));
        m.write32(kRendererObj + 0x54, uint32_t(v.screen_h));
        e.put_f(kCam + 0x1c0, v.cursor_x);
        e.put_f(kCam + 0x1c4, v.cursor_y);
        if (rng.chance(0.3)) {
            ++t_ring.cases;
            const float radius = rng.uni(0, 600), tt = rng.uni(-1, 2);
            e.call(kFnRingVertex, {kRingPos, kRingUv}, radius, tt);
            float pos[4], uv[4];
            drv::RSniperZoom_RingVertex(v, radius, tt, pos, uv);
            std::string why;
            if (!same_words("a posição", m.mem(kRingPos, 16), pos, 16, {}, why) ||
                !same_words("o uv", m.mem(kRingUv, 16), uv, 16, {}, why))
                t_ring.fail(why + tag);
            continue;
        }
        ++t_verts.cases;
        z.alpha = rng.chance(0.3) ? 0xffu : rng.next() & 0x1ff;
        m.write32(kZoom + 8, z.alpha);
        e.call(kFnSetupVerts, {kZoom});
        drv::RSniperZoom_SetupVerts(z, v);
        std::string why;
        if (!same_words("os vértices", m.mem(kZoom + 0x30, 0xc10 - 0x30), reinterpret_cast<const uint8_t*>(&z) + 0x30,
                        0xc10 - 0x30, {}, why))
            t_verts.fail(why + tag);
    }
    report("RSniperZoom (o vértice do anel, 0x1ed7b0)", t_ring, failures);
    report("RSniperZoom::SetupVerts", t_verts, failures);
    delete &z;
    return failures;
}

}  // namespace

int run_cameras_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, int frames, uint32_t seed) {
    std::printf("\nparte 26: as câmeras e os gatilhos de Paris (as âncoras, os modos do carro, as animações relativas, "
                "os gatilhos dos corpos simples e das instâncias, a luneta)\n");
    int failures = 0;
    // (no heap: no x64 o porte guarda endereços dele em palavras de 32 bits)
    std::unique_ptr<World> wp(new World());
    World& w = *wp;
    std::unique_ptr<CamEmu> e(new CamEmu());
    std::unique_ptr<drv::CameraTables> tables(new drv::CameraTables());
    std::unique_ptr<drv::PlayerCamera> cam(new drv::PlayerCamera());
    std::unique_ptr<drv::Track> track(new drv::Track());
    std::unique_ptr<drv::TrackWorld> tw(new drv::TrackWorld());
    std::unique_ptr<drv::CarModel> model(new drv::CarModel());
    w.e = e.get();
    w.track = track.get();
    w.tw = tw.get();
    w.model = model.get();
    const drv::CameraEventsEnv saved_env = drv::camera_events_env();
    try {
        e->setup(elf, vu0_program, vu0_size);
        MipsMachine& m = e->m;
        std::string error;
        drv::Bigf arc;
        std::vector<uint8_t> ini_bytes;
        int idx = -1;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || (idx = arc.find("data\\render\\camera.ini")) < 0 ||
            !arc.read(idx, ini_bytes)) {
            std::printf("  o camera.ini do MIS01.VIV não abriu\n");
            return failures + 1;
        }
        if (!track->load(disc, "paris_mis01", "", error) || !tw->init(track->root(), &error)) {
            std::printf("  Paris não carregou (%s)\n", error.c_str());
            return failures + 1;
        }
        drv::set_world(tw.get());
        if (!copy_world(m, *tw, w.wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  %s\n", error.c_str());
            drv::set_world(nullptr);
            return failures + 1;
        }
        e->heap = (kWorldBegin + w.wc.bytes + 0xfff) & ~0xfffu;
        e->heap_end = kWorldEnd;
        e->ini_size = uint32_t(ini_bytes.size());
        put(m, kIniBuf, ini_bytes.data(), uint32_t(ini_bytes.size()));

        // O modelo do vanquish: o 'Cams' do grupo 'Arti' (o que o SetAnchor acha), o CARP inteiro na RAM emulada com
        // os ponteiros que o RAnimEngine e a câmera seguem trocados.
        model->set_resolve_carp(true);
        if (!model->load(disc, "MIS01.VIV", "vanquish", error)) {
            std::printf("  o vanquish não abriu: %s\n", error.c_str());
            drv::set_world(nullptr);
            return failures + 1;
        }
        drv::UGroup* croot = model->carp_root();
        drv::UGroup* arti = drv::UGroup_GroupLocateTag(croot, 0x41727469u);  // 'Arti'
        uint32_t model_group = 0;
        if (arti != drv::UGroup_GroupEnd(croot)) {
            model_group = nf::p32_u(arti);
            drv::UData* d = drv::UGroup_DataLocateTag(arti, kTagCams);
            if (d != drv::UGroup_DataEnd(arti)) {
                w.cams_port = nf::p32_u(drv::UData_Data(d));
                w.cams_count = d->count;
            }
        }
        w.cc.port = nf::p32_u(model->carp_data());
        w.cc.size = uint32_t(model->carp_size());
        w.cc.emu = e->alloc(w.cc.size + 16);
        {
            std::vector<uint8_t> buf(model->carp_data(), model->carp_data() + w.cc.size);
            std::set<uint32_t> done;
            auto reloc = [&](uint32_t port_addr) {
                if (!w.cc.in(port_addr) || !done.insert(port_addr).second) return;
                uint32_t v;
                std::memcpy(&v, buf.data() + (port_addr - w.cc.port), 4);
                v = w.cc.to_emu(v);
                std::memcpy(buf.data() + (port_addr - w.cc.port), &v, 4);
            };
            for (uint32_t i = 0; i < w.cams_count; ++i) {
                const uint32_t rec = w.cams_port + i * 0x40;
                reloc(rec + 0x1c);
                const uint32_t art = rd32(rec, 0x1c);
                if (!art) {
                    w.cams_systems.push_back(0);
                    continue;
                }
                reloc(art + 4);
                reloc(art + 8);
                const uint32_t anims = rd32(art, 8);
                uint16_t sys = 0;
                if (anims) std::memcpy(&sys, nf::p32_ptr<uint8_t>(anims) + 4, 2);
                w.cams_systems.push_back(sys);
                for (uint32_t a = anims; anims && nf::p32_ptr<uint8_t>(a)[0xf] != 0; a += 0x10) {
                    reloc(a);
                    const uint32_t keys = rd32(a, 0);
                    uint16_t nframes;
                    std::memcpy(&nframes, nf::p32_ptr<uint8_t>(a) + 8, 2);
                    for (uint32_t k = 0; keys && k < nframes; ++k) reloc(keys + k * 0x20 + 0x1c);
                }
            }
            std::memcpy(m.mem(w.cc.emu, w.cc.size), buf.data(), w.cc.size);
        }
        w.cams_emu = w.cams_port ? w.cc.to_emu(w.cams_port) : 0u;
        {
            std::string sys;
            for (uint32_t s : w.cams_systems) sys += " " + std::to_string(s);
            std::printf("  o vanquish: %u registros 'Cams' no 'Arti' (os sistemas:%s), o CARP de %u bytes\n",
                        w.cams_count, sys.c_str(), w.cc.size);
        }
        // O UData dos 'Cams' (sem a bandeira 2: o +0xc é o endereço) e os grupos falsos.
        m.write32(kCamsUData + 0, kTagCams);
        m.write32(kCamsUData + 4, 0);
        m.write32(kCamsUData + 8, w.cams_count);
        m.write32(kCamsUData + 0xc, w.cams_emu);
        for (uint32_t gaddr : {kGroup, kGroupNone}) {
            m.write32(gaddr + 4, 0);
            m.write32(gaddr + 8, 0);
        }
        // Os objetos de desenho (o +0xc aponta para o grupo do modelo; a tabela virtual em +0x3c).
        for (int k = 0; k < 3; ++k) {
            const uint32_t r = kRenderObjs + 0x80u * uint32_t(k);
            std::memset(m.mem(r, 0x80), 0, 0x80);
            m.write32(r + 0xc, r + 0x40);
            m.write32(r + 0x40, k < 2 ? kGroup : kGroupNone);
            m.write32(r + 0x3c, kRenderVt);
        }

        // A Simulation do porte: os carros A e B (BondCar sem construir: só o GetResetAvailable, +0x3e8), os donos dos
        // corpos simples, as assinaturas.
        drv::simulation_reset(60);
        drv::Simulation& sim = drv::simulation();
        w.sim = &sim;
        w.car_a = new drv::BondCar();
        w.car_b = new drv::BondCar();
        for (int k = 0; k < 2; ++k) {
            drv::RigidBody* rb = drv::GetRigidBody(sim, k);
            *rb = drv::RigidBody{};
            rb->info = drv::GetRigidBodyInfo(sim, k);
            *nf::raw(rb->info) = drv::RigidBodyInfo{};
            drv::RigidBodyInfo* info = nf::raw(rb->info);
            info->half_size[0] = 0.95f;
            info->half_size[1] = 0.62f;
            info->half_size[2] = 2.25f;
            rb->slot = int8_t(k);
            rb->state = 2;
            rb->mass = 1500.0f;
            sim.owners[k] = k == 0 ? static_cast<drv::PhysicsObject*>(w.car_a) : w.car_b;
            sim.rigid_sig[k] = (uint32_t(k) << 24) | uint32_t(0x100 + k);
        }
        sim.cars = {w.car_a, w.car_b};
        for (int i = 0; i < drv::kSimpleBodySlots; ++i) {
            w.simple_owners.push_back(std::make_unique<TestOwner>());
            auto a = std::make_unique<drv::CameraAnchor>();
            a->kind = 0;
            a->flags = 1;
            a->body = int16_t(i);
            a->object = w.simple_owners.back().get();
            w.simple_anchors.push_back(std::move(a));
            sim.simple_sig[i] = 0x80000000u | (uint32_t(i) << 24) | uint32_t(0x200 + i);
            const uint32_t o = kSimpleObjs + 0x80u * uint32_t(i);
            std::memset(m.mem(o, 0x80), 0, 0x80);
            m.write32(o + 0x40, 0);
            m.mem(o + 0x44, 2)[0] = 1;
            const int16_t slot = int16_t(i);
            std::memcpy(m.mem(o + 0x46, 2), &slot, 2);
            m.write32(o + 0x48, kRenderObjs + 0x100);
        }
        {
            // O objeto que não é âncora (tipo 0, sem a bandeira).
            const uint32_t o = kSimpleObjs + 0x80u * 96u;
            std::memset(m.mem(o, 0x80), 0, 0x80);
            m.write32(o + 0x48, kRenderObjs + 0x100);
            w.invalid_anchor.kind = 0;
            w.invalid_anchor.flags = 0;
            w.invalid_anchor.body = 3;
            m.mem(o + 0x46, 1)[0] = 3;
        }
        // Os carros na RAM emulada: o tipo 1, a vaga, o objeto de desenho, a tabela virtual; a lista dos carros.
        for (int k = 0; k < 2; ++k) {
            const uint32_t c = k == 0 ? kCar : kCarB;
            std::memset(m.mem(c, 0x400), 0, 0x400);
            m.write32(c + 0x40, 1);
            const int16_t slot = int16_t(k);
            std::memcpy(m.mem(c + 0x46, 2), &slot, 2);
            m.write32(c + 0x48, kRenderObjs + 0x80u * uint32_t(k));
            m.write32(c + 0x68, kCarVt);
            drv::CameraAnchor& a = k == 0 ? w.anchor_a : w.anchor_b;
            a.kind = 1;
            a.flags = 0;
            a.body = int16_t(k);
            a.object = k == 0 ? static_cast<drv::PhysicsObject*>(w.car_a) : w.car_b;
            a.model_group = model_group;
        }
        m.write32(kCarsVec, kCar);
        m.write32(kCarsVec + 4, kCarB);
        m.write32(kSim + 0xdcc, kCarsVec);
        m.write32(kSim + 0xdd0, kCarsVec + 8);
        m.write32(kSim + 0xdd4, kCarsVec + 8);
        m.write32(kSim + 0xe2c, 0);
        m.write32(kSim + 0xe30, 0);
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
        m.write32(kIcm + 4, 0);
        std::memset(m.mem(kAudio, 0x100), 0, 0x100);
        std::memset(m.mem(kUber + 0x4f4, 0x20), 0, 0x20);
        std::memcpy(m.mem(kUber + 0x4f4, 12), "paris_mis01", 12);
        m.write32(kZoomButton, 0);
        m.write32(kPathList, 0);
        m.write32(kWorldCameras, kWorldCamList);
        m.write32(kWorldCamList, 0);
        m.write32(kElem + 0x44, 0);
        std::memset(m.mem(kZoom, 0xca0), 0, 0xca0);
        // O trilho do primeiro carro: o AISplinePath com o RPathHandle.
        w.spline.handle = nf::p32_u(&w.path_handle);
        w.vehicle.spline_path = nf::p32_u(&w.spline);
        // O contexto.
        w.ctx.sim = &sim;
        w.ctx.world = tw.get();
        w.ctx.cars = {&w.anchor_a, &w.anchor_b};
        w.ctx.track = "paris_mis01";
        w.ctx.player_vehicle = &player_vehicle;
        w.ctx.audio_zoom = &w.audio_zoom;
        w.ctx.mission_state = 1;

        // As tabelas: "vanquish".
        std::memset(m.mem(kStrings, 0x80), 0, 0x80);
        std::memcpy(m.mem(kStrings, 0x40), "vanquish", 8);
        m.write32(kTables + 0x0c, 0);
        e->call(kFnLoadFile, {0});
        drv::IniFile ini;
        ini.open(ini_bytes);
        drv::CameraCarInfo car;
        car.type = "vanquish";
        tables->load(ini, car);
        {
            Tally t;
            ++t.cases;
            std::string out;
            const drv::CameraTableGlobals& g = tables->tables;
            if (!same_words("os modos", m.mem(m.read32(kTables + 0x0c), 0x1c * uint32_t(g.mode_count + 1)),
                            nf::raw(g.modes), 0x1c * uint32_t(g.mode_count + 1), {}, out) ||
                !same_words("as constantes", m.mem(kConstants, 0x80), &tables->constants, 0x80,
                            {{0x34, 0x40}, {0x4c, 0x50}}, out) ||
                !same_words("os globais 0x3339d0", m.mem(kTables + 0x20, 0x1c),
                            reinterpret_cast<const uint8_t*>(&g) + 0x20, 0x1c, {}, out))
                t.fail(out);
            report("LoadFile (vanquish)", t, failures);
        }
        // O ruído: as tabelas do porte nos dois lados (a parte 8 confere o Noise::Init).
        (void)drv::Noise_Noise1(0.0f);
        std::memcpy(m.mem(kNoisePerm, 0x2000), drv::noise_perm_table(), 0x2000);
        std::memcpy(m.mem(kNoiseGrad, 0x10b * 4), drv::noise_grad_table(), 0x10b * 4);

        Rng rng(seed ^ 0x2626u);
        failures += check_anchors(w, *cam, cases, rng);
        drv::trigger_manager().pending.clear();
        failures += check_camera(w, *tables, *cam, std::max(frames, cases / 2), rng);
        drv::trigger_manager().pending.clear();
        failures += check_triggers(w, cases, rng);
        failures += check_sniper(w, cases, rng);
        std::printf("    (o interpretador rodou %" PRIu64 " instruções)\n", e->m.steps());
    } catch (const nf::MipsError& err) {
        std::printf("  o interpretador parou: %s\n", err.what());
        ++failures;
    }
    drv::camera_events_env() = saved_env;
    drv::trigger_manager().pending.clear();
    drv::WTriggerManager_Reset();
    drv::RPathEngine_Purge();
    drv::anim_engine_reset();
    g_vehicle = nullptr;
    cam.reset();
    drv::Simulation& sim = drv::simulation();
    sim.cars.clear();
    drv::simulation_reset(60);
    delete w.car_a;
    delete w.car_b;
    drv::set_world(nullptr);
    return failures;
}

}  // namespace drvcheck
