// nf_driving_check, parte 12 (tests/driving_check_vehicle.h): o carro desenhado. O código ORIGINAL do DRIVING.ELF roda
// no interpretador MIPS (com a VU0 em modo macro) ao lado do porte, com estados sorteados (um Vanquish do disco, o
// corpo, o RVehicle e o esqueleto):
//  - RVehicle::ComputeWheels (0x1d2cb0): as rodas, os cubos e, no submarino, as barbatanas e os lemes; o carro tem uma
//    tabela virtual falsa cujas entradas são as funções originais (GetCarWheelSpinAngle, GetCarClass, GetCarSteer,
//    GetCarControl, GetPhysics, IsReversing, GetSuspensionCompression) e a câmera do jogador é a da RAM emulada (o modo e
//    o byte +7 do fHeliCamInfo);
//  - RVehicle::UpdatePosition (0x1d34a8) com o argumento 1 (o do PhysicsObject::Simulate): o RSkeletalObj e o
//    RSceneObj::UpdatePosition (a instância de origem), ComputeWheels, o volante, os dois ponteiros com o
//    PBondCar::CalculateRPM original no carro do jogador;
//  - RVehicle::ComputeDamage, SetPerObjectBuffers e ClearSharedBuffers (GAME::SwitchData e GAME::DamageZones),
//    TriggerFX (0xb, 0xc) e TriggerIlluminate, GetNeedlePos;
//  - a animação procedural das instâncias: RSkeletalObj::SetProcAnimState e GetInstanceMatrix com as funções 0xff, 0xfe
//    e 0xef do gProcAnimFunc (e o BuildRotate);
//  - RShadowMap::DrawSimpleShadow (0x1ec4a0): os quatro cantos, as coordenadas e a cor que chegam ao
//    USimpleTexturedMaterial::Draw;
//  - PBondCar::GlareOn, GlareOff e HandleTurnSignals (com o RVehicle::TriggerFX original no render);
//  - o IFeedback: sequências sorteadas de IFeedback/~IFeedback, ApplyJolt, AddCollisionInfo, SetRumble, SetVibrate,
//    Update e Pause, com as chamadas do PAD_setactuator;
//  - o InputConfigManager: o construtor e o InitAndPreload sobre o Master.def, o FrontEnd.def e os .def das
//    configurações do MIS01.VIV (com os valores do bloco da troca sorteados, dentro e fora das listas), depois
//    SetConfig(tipo, n), GetNumConfigs e GetCurrentConfig.
// Ganchos nativos: memset, memmove, MEM_copy, MEM_fill, a memória (FastAlloc e os new/delete), sinf/cosf (os do port:
// nf::fm), WTree::FindNode e RSceneObj::SetMapNode (registram que a posição andou), RSceneObj::TriggerFX e os outros
// efeitos do RVehicle::TriggerFX (contados), o estado de render da sombra (DisableAlphaWrites, EnableAlphaWrites, o
// construtor do material, atexit, TIMER_removetask) e o USimpleTexturedMaterial::Draw (registra o quadrado), o
// PAD_setactuator e as linhas de execução do IFeedback, UFileLoader::FileLoad/FileFree (os arquivos do disco), as funções
// de texto da libc (strcasecmp, strtok, atoi, strlen, strcpy, strchr, strncpy, strcat, strcmp, qsort), o printf do aviso
// e o InputConfigManager::SetConfig(char *) (registra o texto: o que ele faz é a parte 10).
//
// O que não é comparado: os ponteiros (do carro, do RVehicle e do corpo).

#include "driving_check_vehicle.h"

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
#include "driving/camera/dafi.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/feedback.h"
#include "driving/game/input.h"
#include "driving/game/input_config.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/render/shadow.h"
#include "driving/render/vehicle.h"
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

// As funções originais.
constexpr uint32_t kFnComputeWheels = 0x1d2cb0, kFnUpdatePosition = 0x1d34a8, kFnComputeDamage = 0x1d3138;
constexpr uint32_t kFnSetPerObjectBuffers = 0x1d2b38, kFnClearSharedBuffers = 0x1d2a50;
constexpr uint32_t kFnTriggerFX = 0x1d3c28, kFnTriggerIlluminate = 0x1d2aa0, kFnGetNeedlePos = 0x1d3440;
constexpr uint32_t kFnSetProcAnimState = 0x1cd178, kFnGetInstanceMatrix = 0x1beee0, kFnBuildRotate = 0x2d11c8;
constexpr uint32_t kFnDrawSimpleShadow = 0x1ec4a0;
constexpr uint32_t kFnGlareOn = 0x18b328, kFnGlareOff = 0x18b408, kFnHandleTurnSignals = 0x18b580;
constexpr uint32_t kFnFbCtor = 0x165ed0, kFnFbDtor = 0x166260, kFnFbUpdate = 0x165bc8, kFnFbJolt = 0x165fb0;
constexpr uint32_t kFnFbCollision = 0x1660a0, kFnFbRumble = 0x1660f0, kFnFbVibrate = 0x166180, kFnFbPause = 0x166210;
constexpr uint32_t kFnIcmCtor = 0x166508, kFnIcmInit = 0x1668a0, kFnIcmSetType = 0x166cb0;
constexpr uint32_t kFnIcmNum = 0x166d80, kFnIcmCurrent = 0x166d90;
constexpr uint32_t kCmpByName = 0x2ceae8, kCmpByNumber = 0x2ceb08;

// Os globais do ELF.
constexpr uint32_t kCameraPtr = 0x333ed8, kHeliInfoPtr = 0x3339bc;  // RPlayerCamera do jogador e fHeliCamInfo
constexpr uint32_t kSwitchData = 0x424520, kDamageZones = 0x424620, kTime = 0x334010;
constexpr uint32_t kWRenderPtr = 0x33aac8;
constexpr uint32_t kRendererPtr = 0x333760, kMatIndex = 0x3c82fc, kMatTable = 0x3c8300, kMatInit = 0x3353e0;
constexpr uint32_t kStrength = 0x335388, kSizesPtr = 0x3deb18;
constexpr uint32_t kFbPorts = 0x32c468, kFbEnabled = 0x32c460, kFbThreads = 0x32c538, kFbPaused = 0x32e690;
constexpr uint32_t kFbCounter = 0x3c8770, kUber = 0x100000;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000;
constexpr uint32_t kInfos = 0x01002000;
constexpr uint32_t kCar = 0x01012000;
constexpr uint32_t kPhys = 0x01013000;
constexpr uint32_t kVtable = 0x01014000;
constexpr uint32_t kVeh = 0x01015000;
constexpr uint32_t kVehVt = 0x01015400;
constexpr uint32_t kBones = 0x01016000;
constexpr uint32_t kCam = 0x01017000, kCamObj = 0x01017100, kHeli = 0x01017400, kCarsVec = 0x01017800;
constexpr uint32_t kInst = 0x01018000, kOut = 0x01018100, kStates = 0x01018200;
constexpr uint32_t kShadowMap = 0x01019000, kTexHolder = 0x01019100, kSizes = 0x01019200, kMatState = 0x01019300;
constexpr uint32_t kRenderer = 0x01019400, kWRender = 0x01019800, kWBox = 0x01019900;
constexpr uint32_t kCollInfo = 0x0101a000, kFb = 0x0101a100, kMgr = 0x0101a200;
constexpr uint32_t kFnRenderOffset = 0x0101b000;  // o GetRenderOffset falso (o valor do caso)
constexpr uint32_t kHeapBegin = 0x01020000, kHeapEnd = 0x01400000;

uint32_t body_addr(int slot) { return kBodies + 0x80u * uint32_t(slot); }
uint32_t info_addr(int slot) { return kInfos + 0x500u * uint32_t(slot); }

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1212) {}
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
uint32_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
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

struct PadCall {
    int32_t port, motor;
    uint32_t value;
    bool operator==(const PadCall& o) const { return port == o.port && motor == o.motor && value == o.value; }
};

// O carregador do InitAndPreload (o texto do arquivo, vazio se não há).
struct InputFileLoaderFn {
    drv::InputFileLoader fn;
};

class VehEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    uint64_t find_node = 0, scene_fx = 0, other_fx = 0;
    float render_offset = 0;
    // O quadrado do USimpleTexturedMaterial::Draw.
    bool shadow_drawn = false;
    float shadow_pos[4][4] = {}, shadow_uv[4][4] = {};
    uint32_t shadow_colour[4] = {}, shadow_texture = 0;
    std::vector<PadCall> pad;
    std::vector<std::string> printed;
    std::vector<uint32_t> set_config;  // o texto passado a cada SetConfig(char *)
    std::vector<std::pair<uint32_t, std::string>> files;  // os arquivos carregados (endereço, caminho)
    InputFileLoaderFn* loader = nullptr;
    uint32_t strtok_last = 0;

    const char* cstr(uint32_t a) { return reinterpret_cast<const char*>(m.mem(a, 1)); }
    float f32(uint32_t a) {
        float f;
        std::memcpy(&f, m.mem(a, 4), 4);
        return f;
    }
    void put_f32(uint32_t a, float f) { std::memcpy(m.mem(a, 4), &f, 4); }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size);
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 50'000'000);
    }
};

void VehEmu::setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
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
    m.hook(0x2601f0, "MEM_copy", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
        if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
    });
    m.hook(0x260300, "MEM_fill", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
        if (n) std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
    });
    auto alloc = [this](MipsMachine& mm) {
        const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
        if (heap + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", heap, n);
        if (n) std::memset(mm.mem(heap, n), 0, n);
        mm.set_gpr(R::v0, heap);
        heap += n;
    };
    m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
    m.hook(0x2d0910, "__builtin_new", alloc);
    m.hook(0x2d09c8, "operator_new", alloc);
    m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
    m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
    m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
    m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
    // O mapa da cena (o RSceneObj::UpdatePosition de um objeto que andou).
    m.hook(0x22c6b8, "WTree::FindNode", [this](MipsMachine& mm) {
        ++find_node;
        mm.set_gpr(R::v0, 0);
    });
    m.hook(0x1c8cf8, "RSceneObj::SetMapNode", [](MipsMachine&) {});
    m.hook(0x1adce8, "RRenderSharedData::GetShadowBrightness", [](MipsMachine& mm) { mm.set_fpr(0, 0.5f); });
    m.hook(kFnRenderOffset, "RVehicle::GetRenderOffset (falso)",
           [this](MipsMachine& mm) { mm.set_fpr(0, render_offset); });
    // Os efeitos do TriggerFX.
    m.hook(0x1caba8, "RSceneObj::TriggerFX", [this](MipsMachine&) { ++scene_fx; });
    m.hook(0x1d3b10, "RVehicle::StartColourDissolve", [this](MipsMachine&) { ++other_fx; });
    m.hook(0x1eed18, "RVehicleParticle::TriggerSparks", [this](MipsMachine&) { ++other_fx; });
    m.hook(0x1d38d8, "RVehicle::StartMuzzleFlash", [this](MipsMachine&) { ++other_fx; });
    // A sombra.
    m.hook(0x1ac1b8, "RRenderer::DisableAlphaWrites", [](MipsMachine& mm) { mm.set_gpr(R::v0, 1); });
    m.hook(0x1ac178, "RRenderer::EnableAlphaWrites", [](MipsMachine&) {});
    m.hook(0x2913f8, "TIMER_removetask", [](MipsMachine&) {});
    m.hook(0x2cf8c8, "USimpleTexturedMaterial::USimpleTexturedMaterial", [](MipsMachine&) {});
    m.hook(0x2a71f8, "atexit", [](MipsMachine&) {});
    m.hook(0x2cf9a0, "USimpleTexturedMaterial::Draw", [this](MipsMachine& mm) {
        if (mm.gpr(R::a1) != 5 || mm.gpr(R::a2) != 4) throw nf::MipsError("Draw: argumentos inesperados", 0, 0);
        const uint32_t st = mm.read32(kMatTable + 4u * mm.read32(kMatIndex));
        std::memcpy(shadow_pos, mm.mem(mm.read32(st + 0x18), 64), 64);
        std::memcpy(shadow_uv, mm.mem(mm.read32(st + 0x20), 64), 64);
        std::memcpy(shadow_colour, mm.mem(mm.read32(st + 0x28), 16), 16);
        shadow_texture = mm.read32(st + 0x30);
        shadow_drawn = true;
    });
    // A vibração.
    m.hook(0x25a028, "PAD_setactuator", [this](MipsMachine& mm) {
        pad.push_back({int32_t(mm.gpr(R::a0)), int32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2))});
    });
    for (uint32_t a : {0x261b70u, 0x2611f0u, 0x260ca0u, 0x260d00u, 0x261be0u, 0x261a40u, 0x2612f8u, 0x261bc0u})
        m.hook(a, "linha de execução do IFeedback", [](MipsMachine&) {});
    m.hook(0x2616b0, "THREAD_testexit", [](MipsMachine& mm) { mm.set_gpr(R::v0, 1); });
    // O InputConfigManager.
    m.hook(0x2d73c0, "UFileLoader::FileLoad", [this](MipsMachine& mm) {
        const std::string path = cstr(uint32_t(mm.gpr(R::a0)));
        const std::string text = loader ? loader->fn(path) : std::string();
        if (text.empty()) {
            mm.set_gpr(R::v0, 0);
            return;
        }
        const uint32_t n = (uint32_t(text.size()) + 16) & ~15u;
        if (heap + n > kHeapEnd) throw nf::MipsError("FileLoad: a área acabou", heap, n);
        std::memset(mm.mem(heap, n), 0, n);
        std::memcpy(mm.mem(heap, uint32_t(text.size())), text.data(), text.size());
        files.push_back({heap, path});
        mm.set_gpr(R::v0, heap);
        heap += n;
    });
    m.hook(0x2d7ac8, "UFileLoader::FileFree", [](MipsMachine&) {});
    m.hook(0x2a9198, "strcasecmp", [this](MipsMachine& mm) {
        const int r = drv::dafi_strcasecmp(cstr(uint32_t(mm.gpr(R::a0))), cstr(uint32_t(mm.gpr(R::a1))));
        mm.set_gpr(R::v0, uint64_t(int64_t(r)));
    });
    m.hook(0x2a9508, "strcmp", [this](MipsMachine& mm) {
        const int r = std::strcmp(cstr(uint32_t(mm.gpr(R::a0))), cstr(uint32_t(mm.gpr(R::a1))));
        mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
    });
    m.hook(0x2aabd8, "strtok", [this](MipsMachine& mm) {
        uint32_t s = uint32_t(mm.gpr(R::a0));
        const std::string delims(cstr(uint32_t(mm.gpr(R::a1))));
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
    m.hook(0x2a72b0, "atoi", [this](MipsMachine& mm) {
        mm.set_gpr(R::v0, uint64_t(int64_t(std::atoi(cstr(uint32_t(mm.gpr(R::a0)))))));
    });
    m.hook(0x2a9760, "strlen", [this](MipsMachine& mm) {
        mm.set_gpr(R::v0, std::strlen(cstr(uint32_t(mm.gpr(R::a0)))));
    });
    m.hook(0x2a964c, "strcpy", [this](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0));
        const std::string s = cstr(uint32_t(mm.gpr(R::a1)));
        std::memcpy(mm.mem(dst, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
        mm.set_gpr(R::v0, dst);
    });
    m.hook(0x2a924c, "strcat", [this](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0));
        const std::string a = cstr(dst), b = cstr(uint32_t(mm.gpr(R::a1)));
        std::memcpy(mm.mem(dst + uint32_t(a.size()), uint32_t(b.size() + 1)), b.c_str(), b.size() + 1);
        mm.set_gpr(R::v0, dst);
    });
    m.hook(0x2a9378, "strchr", [this](MipsMachine& mm) {
        const uint32_t a = uint32_t(mm.gpr(R::a0));
        const char* s = cstr(a);
        const char* p = std::strchr(s, int(mm.gpr(R::a1) & 0xff));
        mm.set_gpr(R::v0, p ? a + uint32_t(p - s) : 0);
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
    m.hook(0x2a8198, "qsort", [this](MipsMachine& mm) {
        const uint32_t base = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a1)), size = uint32_t(mm.gpr(R::a2));
        const uint32_t cmp = uint32_t(mm.gpr(R::a3));
        if (size != 8 || (cmp != kCmpByName && cmp != kCmpByNumber))
            throw nf::MipsError("qsort: comparador desconhecido", cmp, size);
        std::vector<std::pair<uint32_t, uint32_t>> s(n);
        for (uint32_t i = 0; i < n; ++i) s[i] = {mm.read32(base + i * 8), mm.read32(base + i * 8 + 4)};
        for (size_t i = 1; i < s.size(); ++i)
            for (size_t j = i; j > 0; --j) {
                const bool less = cmp == kCmpByName ? std::strcmp(cstr(s[j].second), cstr(s[j - 1].second)) < 0
                                                    : int32_t(s[j].first) < int32_t(s[j - 1].first);
                if (!less) break;
                std::swap(s[j], s[j - 1]);
            }
        for (uint32_t i = 0; i < n; ++i) {
            mm.write32(base + i * 8, s[i].first);
            mm.write32(base + i * 8 + 4, s[i].second);
        }
    });
    m.hook(0x10c6c8, "printf", [this](MipsMachine& mm) { printed.push_back(cstr(uint32_t(mm.gpr(R::a0)))); });
    m.hook(0x166f28, "InputConfigManager::SetConfig(char *)",
           [this](MipsMachine& mm) { set_config.push_back(uint32_t(mm.gpr(R::a1))); });

    // A tabela virtual falsa do carro: delta 0 e a função (8 bytes por entrada; o PS2 lê o lh +0 e o lw +4).
    auto entry = [this](uint32_t vt, uint32_t off, uint32_t fn) {
        m.write32(vt + off, 0);
        m.write32(vt + off + 4, fn);
    };
    entry(kVtable, 0x20, 0x18aa78);   // GetDamageZones
    entry(kVtable, 0x98, 0x193008);   // GetCarClass
    entry(kVtable, 0xa8, 0x193110);   // GetCarSpeed
    entry(kVtable, 0xc8, 0x193190);   // GetCarSteer
    entry(kVtable, 0x130, 0x1930c0);  // GetCarControl
    entry(kVtable, 0x170, kFnGlareOn);
    entry(kVtable, 0x190, 0x193178);  // GetCarWheelSpinAngle
    entry(kVtable, 0x1d0, 0x193170);  // GetPhysics
    entry(kVtable, 0x1d8, 0x1931a8);  // IsReversing
    entry(kVtable, 0x1e8, 0x193198);  // GetSuspensionCompression
    entry(kVtable, 0x1f0, 0x1931c0);  // GetWheelRoadHeight
    // A do RVehicle (a do RSceneObj em +0x3c): GetRenderOffset falso e o TriggerFX original.
    entry(kVehVt, 0x50, kFnRenderOffset);
    entry(kVehVt, 0x80, kFnTriggerFX);
    // A câmera do jogador e o fHeliCamInfo.
    m.write32(kCameraPtr, kCam);
    m.write32(kCam + 4, kCamObj);
    m.write32(kHeliInfoPtr, kHeli);
    // O WRender do mapa (o RSceneObj::UpdatePosition lê a caixa do mundo).
    m.write32(kWRenderPtr, kWRender);
    m.write32(kWRender + 0x1c, kWBox);
    // A sombra: o RRenderer, o estado do material, os tamanhos, a textura.
    m.write32(kRendererPtr, kRenderer);
    m.write32(kMatIndex, 0);
    m.write32(kMatTable, kMatState);
    m.write32(kMatInit, 1);
    m.write32(kSizesPtr, kSizes);
    m.write32(kShadowMap + 0x60, kTexHolder);
    m.write32(kTexHolder + 0x4c, 0x7e57);
    // A Simulation.
    m.write32(kSim + 0x04, 0);
    m.write32(kSim + 0x1c, kBodies);
    m.write32(kSim + 0x24, kInfos);
    m.write32(kSim + 0xdcc, kCarsVec);
}

// O carro de teste (o Vanquish do disco num corpo rígido da Simulation) e o render dele.
struct Rig {
    drv::BondCar* car = nullptr;
    drv::Vehicle veh;
    float instance[16] = {};
};

// O estado do porte na RAM emulada: o corpo, o carro (com a tabela virtual falsa), o CarPhysics, o RVehicle com o
// esqueleto e a instância de origem.
void put_state(VehEmu& e, Rig& r, bool player) {
    drv::BondCar& car = *r.car;
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
    std::memcpy(e.m.mem(kCar, 0x460), &d, 0x460);
    std::memset(e.m.mem(kCar, 0x40), 0, 0x40);  // o WWorldPos (o +0x30 nulo: sem o brilho de sombra)
    e.m.write32(kCar + 0x48, kVeh);
    for (uint32_t o = 0x4c; o < 0x68; o += 4) e.m.write32(kCar + o, 0);
    e.m.write32(kCar + 0x68, kVtable);
    e.m.write32(kCar + 0x23c, kPhys);
    e.m.write32(kCar + 0x26c, 0);
    e.m.write32(kCar + 0x388, 0);
    std::memcpy(e.m.mem(kPhys, sizeof(drv::CarPhysics)), &car.car_physics, sizeof(drv::CarPhysics));
    e.m.write32(kSim + 0x270 + 4u * uint32_t(slot), kCar);
    e.m.write32(kSim + 0xdc4, uint32_t(drv::simulation().steps));
    e.m.write32(kCarsVec, player ? kCar : kCar + 0x1000);
    drv::RVehicleData v = r.veh.d;
    std::memcpy(e.m.mem(kVeh, 0x370), &v, 0x370);
    e.m.write32(kVeh + 0x08, kInst);
    e.m.write32(kVeh + 0x0c, 0);
    e.m.write32(kVeh + 0x10, 0);
    e.m.write32(kVeh + 0x14, 0);
    e.m.write32(kVeh + 0x24, kCar);
    e.m.write32(kVeh + 0x28, 0);
    e.m.write32(kVeh + 0x3c, kVehVt);
    e.m.write32(kVeh + 0x7c, r.veh.bones.empty() ? 0 : kBones);
    for (uint32_t o : {0x348u, 0x34cu, 0x350u, 0x354u, 0x358u, 0x368u}) e.m.write32(kVeh + o, 0);
    if (!r.veh.bones.empty())
        std::memcpy(e.m.mem(kBones, uint32_t(r.veh.bones.size() * 64)), r.veh.bones.data(), r.veh.bones.size() * 64);
    std::memcpy(e.m.mem(kInst, 64), r.instance, 64);
}

// O RVehicle, os ossos e a instância: o original contra o porte.
std::string compare_vehicle(VehEmu& e, const Rig& r) {
    std::string out;
    if (!same_words("RVehicle", e.m.mem(kVeh, 0x370), reinterpret_cast<const uint8_t*>(&r.veh.d), 0x370,
                    {{0x08, 0x18}, {0x24, 0x2c}, {0x3c, 0x40}, {0x7c, 0x80}, {0x348, 0x35c}, {0x368, 0x36c}}, out))
        return out;
    if (!r.veh.bones.empty() &&
        !same_words("ossos", e.m.mem(kBones, uint32_t(r.veh.bones.size() * 64)),
                    reinterpret_cast<const uint8_t*>(r.veh.bones.data()), uint32_t(r.veh.bones.size() * 64), {}, out))
        return out;
    if (!same_words("instância", e.m.mem(kInst, 64), reinterpret_cast<const uint8_t*>(r.instance), 64, {}, out))
        return out;
    return out;
}

std::string compare_car(VehEmu& e, const Rig& r) {
    std::string out;
    same_words("PBondCar", e.m.mem(kCar, 0x460), reinterpret_cast<const uint8_t*>(&r.car->d), 0x460,
               {{0x00, 0x40}, {0x48, 0x6c}, {0x74, 0x84}, {0x90, 0x190}, {0x23c, 0x240}, {0x26c, 0x270},
                {0x2cc, 0x2d0}, {0x2e0, 0x2e8}, {0x388, 0x38c}, {0x434, 0x438}},
               out);
    return out;
}

// Um esqueleto sorteado com os ossos das rodas e dos cubos (alguns ausentes) e as alturas das rodas.
void random_vehicle(Rng& rng, Rig& r) {
    drv::Vehicle& v = r.veh;
    drv::RVehicle_Construct(v);
    const uint32_t n = uint32_t(rng.range(4, 12));
    drv::RSkeletalObj_AllocateSkeleton(v, n);
    for (drv::Bone& b : v.bones)
        for (auto& row : b.m)
            for (float& x : row) x = rng.uni(-2, 2);
    for (int i = 0; i < 4; ++i) {
        v.d.wheel_bone[i] = rng.chance(0.1) ? 0 : rng.range(1, int(n));
        v.d.hub_bone[i] = rng.chance(0.2) ? 0 : rng.range(1, int(n));
        v.d.wheel_offset[i] = rng.uni(-0.5f, 0.5f);
    }
    v.d.steer = rng.chance(0.2) ? 0.0f : rng.uni(-0.1f, 0.1f);
    v.d.sub_steer_vertical = rng.uni(-0.3f, 0.3f);
    v.d.sub_brake = rng.uni(0, 1);
    for (int i = 0; i < 12; ++i) v.d.params[i] = rng.uni(-300, 300);
    for (int i = 0; i < 16; ++i) {
        v.d.switch_w[i] = rng.chance(0.5) ? 0.0f : rng.uni(0, 2);
        v.d.switch_time[i] = rng.chance(0.3) ? -10000 : rng.range(-50, 5000);
    }
}

// O corpo e o carro sorteados (o que as funções do render leem).
void random_car(Rng& rng, Rig& r) {
    drv::BondCar& car = *r.car;
    drv::RigidBody* rb = car.body();
    drv::RigidBodyInfo* info = rb->info;
    for (int i = 0; i < 3; ++i) rb->pos[i] = rng.uni(-500, 500);
    float q[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)};
    const float qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (float& x : q) x = qn > 0 ? x / qn : 0.5f;
    if (rng.chance(0.7)) {  // quase de pé
        const float yaw = rng.uni(-3.1f, 3.1f) * 0.5f;
        q[0] = rng.uni(-0.05f, 0.05f);
        q[2] = rng.uni(-0.05f, 0.05f);
        q[1] = std::sin(yaw);
        q[3] = std::cos(yaw);
    }
    drv::SetOrientation(rb, q);
    for (int i = 0; i < info->lever_count; ++i) {
        float l[4];
        std::memcpy(l, info->lever_local[i], 16);
        drv::ConvertLocalToWorld(rb, l);
        std::memcpy(info->lever_world[i], l, 16);
    }
    for (int i = 0; i < 3; ++i) rb->vel[i] = rng.chance(0.1) ? 0.0f : rng.uni(-40, 40);
    info->ground[1] = rng.chance(0.1) ? rng.uni(-1, 0.1f) : rng.uni(0.1f, 1);
    drv::PBondCarData& c = car.d;
    c.type = rng.chance(0.05) ? 2 : 1;
    c.car_class = rng.chance(0.7) ? 0 : rng.range(1, 3);
    c.car_steer = rng.chance(0.2) ? 0.0f : rng.uni(-0.2f, 0.2f);
    c.car_speed = rng.uni(-5, 80);
    c.wheel_spin_angle[0] = rng.uni(-1.5f, 1.5f);
    c.wheel_spin_angle[1] = rng.uni(-1.5f, 1.5f);
    for (int i = 0; i < 4; ++i) {
        c.suspension[i] = rng.chance(0.2) ? 0.0f : rng.uni(-0.1f, 0.5f);
        c.wheel_road_normal[i][3] = rng.chance(0.1) ? rng.uni(-3, -1) : rng.uni(-0.6f, 0.4f);
    }
    c.control.steering = rng.uni(-1, 1);
    c.control.steering_vertical = rng.uni(-1, 1);
    c.control.gas = rng.chance(0.3) ? 0.0f : rng.uni(0, 1);
    c.control.brake = rng.chance(0.5) ? 0.0f : rng.uni(0, 1);
    c.control.gear = uint8_t(rng.chance(0.1));
    c.reversing = uint8_t(rng.chance(0.2));
    c.num_wheels_on_ground = int8_t(rng.range(0, 4));
    c.gear = int8_t(rng.range(0, 5));
    c.previous_gear = int8_t(rng.range(0, 5));
    c.gear_change_timer = uint8_t(rng.chance(0.5) ? 0 : rng.range(1, 15));
    c.wheel_spin_flag = rng.chance(0.8) ? 0.0f : 1.0f;
    for (auto& z : c.damage_zones) {
        z[0] = rng.chance(0.5) ? 0.0f : rng.uni(0, 1);
        z[1] = rng.chance(0.5) ? 0.0f : rng.uni(0, 1);
    }
    car.car_physics.is_sub = rng.chance(0.2) ? 1 : 0;
    car.car_physics.sub_physics = rng.chance(0.1) ? 1 : 0;
    // A instância de origem: a do caso anterior ou uma sorteada (o w de cada linha são as bandeiras: fica).
    if (rng.chance(0.5))
        for (float& x : r.instance) x = rng.uni(-500, 500);
}

}  // namespace

int run_vehicle_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    std::printf("\n== parte 12: o carro desenhado (RVehicle, a sombra, as luzes, a vibração, as configurações do controle): "
                "o código original do EE (interpretador MIPS + VU0) x o porte ==\n");
    int failures = 0;
    VehEmu* e = new VehEmu();
    Rig rig;
    try {
        e->setup(elf, vu0_program, vu0_size);
        drv::Bigf arc;
        drv::AttributeCollection vanquish;
        std::string error;
        if (!arc.open(disc, "DRIVING/MIS01.VIV") || !vanquish.load(arc, "pvehicle", "vanquish", "paris_mis01", &error)) {
            std::printf("  o vanquish.atr não abriu (%s)\n", error.c_str());
            delete e;
            return failures + 1;
        }
        drv::reset_rigid_globals();
        drv::physics_pending_reset();
        drv::reset_bond_car_tuning();
        drv::simulation_reset(60);
        drv::BondCar* car = new drv::BondCar();
        rig.car = car;
        car->name_ = "vanquish";
        car->car_physics = vanquish.car_physics();
        car->d.physics = nf::p32_u(&car->car_physics);
        car->vehicle = &rig.veh;
        car->lookup_mass = vanquish.lookup_float("MASS");
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
        car->d.type = 1;
        Rng rng(seed ^ 0x5eed12);

        // RVehicle::ComputeWheels.
        {
            Tally t;
            uint64_t subs = 0, inside = 0;
            for (int c = 0; c < cases; ++c) {
                random_vehicle(rng, rig);
                random_car(rng, rig);
                const int mode = rng.range(0, 21);
                const uint8_t interior = uint8_t(rng.chance(0.3) ? 1 : 0);
                e->m.write32(kCamObj + 0x120, uint32_t(mode));
                e->m.mem(kHeli + uint32_t(mode) * 0x1c + 7, 1)[0] = interior;
                put_state(*e, rig, false);
                e->call(kFnComputeWheels, {kVeh});
                const float emu = e->m.fpr(0);
                const float port = drv::RVehicle_ComputeWheels(rig.veh, car, interior != 0);
                subs += car->car_physics.is_sub != 0;
                inside += interior;
                ++t.cases;
                std::string d = compare_vehicle(*e, rig);
                if (d.empty() && fbits(emu) != fbits(port))
                    d = "o retorno: original " + hex(fbits(emu)) + ", porte " + hex(fbits(port));
                if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
            }
            std::printf("    submarinos %" PRIu64 ", vista de dentro %" PRIu64 "\n", subs, inside);
            report("RVehicle::ComputeWheels (as rodas, os cubos, as barbatanas)", t, failures);
        }

        // RVehicle::UpdatePosition (o argumento 1: o PhysicsObject::Simulate).
        {
            Tally t;
            uint64_t moved = 0, players = 0;
            for (int c = 0; c < cases; ++c) {
                random_vehicle(rng, rig);
                random_car(rng, rig);
                drv::simulation().steps = rng.range(0, 100000);
                const bool player = rng.chance(0.7);
                const int mode = rng.range(0, 21);
                const uint8_t interior = uint8_t(rng.chance(0.3) ? 1 : 0);
                e->m.write32(kCamObj + 0x120, uint32_t(mode));
                e->m.mem(kHeli + uint32_t(mode) * 0x1c + 7, 1)[0] = interior;
                e->render_offset = rng.chance(0.2) ? 0.0f : rng.uni(-0.5f, 0.5f);
                if (rng.chance(0.2)) {  // a instância já no lugar (não anda)
                    float m[4][4];
                    std::memcpy(m, rb->info->orient, 64);
                    std::memcpy(m[3], rb->pos, 12);
                    for (int row = 0; row < 4; ++row) std::memcpy(rig.instance + row * 4, m[row], 12);
                    e->render_offset = 0.0f;
                }
                put_state(*e, rig, player);
                const uint64_t f0 = e->find_node;
                e->call(kFnUpdatePosition, {kVeh, 1});
                const bool emu_moved = e->find_node != f0;
                const bool port_moved = drv::RVehicle_UpdatePosition(rig.veh, car, player, interior != 0, rig.instance,
                                                                     e->render_offset);
                moved += port_moved;
                players += player;
                ++t.cases;
                std::string d = compare_vehicle(*e, rig);
                if (d.empty()) d = compare_car(*e, rig);
                if (d.empty() && emu_moved != port_moved) d = "andou: original " + std::to_string(emu_moved);
                if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
            }
            std::printf("    jogador %" PRIu64 ", andou %" PRIu64 "\n", players, moved);
            report("RVehicle::UpdatePosition (instância, rodas, ponteiros)", t, failures);
        }

        // ComputeDamage, SetPerObjectBuffers, ClearSharedBuffers, TriggerFX, TriggerIlluminate.
        {
            Tally t_dmg, t_buf, t_fx;
            for (int c = 0; c < cases / 2; ++c) {
                random_vehicle(rng, rig);
                random_car(rng, rig);
                const bool none = rng.chance(0.1);
                put_state(*e, rig, false);
                if (none) e->m.write32(kVeh + 0x24, 0);
                e->call(kFnComputeDamage, {kVeh});
                drv::RVehicle_ComputeDamage(rig.veh, none ? nullptr : car);
                ++t_dmg.cases;
                std::string d = compare_vehicle(*e, rig);
                if (!d.empty()) t_dmg.fail("caso " + std::to_string(c) + ": " + d);
                // Os buffers: o x, y e z das lâmpadas sorteados dos dois lados (o InitSharedBuffers lê o banco).
                drv::VehicleSharedBuffers& sh = drv::vehicle_shared_buffers();
                for (auto& s : sh.switch_data)
                    for (float& x : s) x = rng.uni(-2, 2);
                for (auto& z : sh.damage_zones)
                    for (float& x : z) x = rng.uni(-2, 2);
                std::memcpy(e->m.mem(kSwitchData, 0x100), sh.switch_data, 0x100);
                std::memcpy(e->m.mem(kDamageZones, 0x100), sh.damage_zones, 0x100);
                const int32_t time = rng.range(-100, 6000);
                e->m.write32(kTime, uint32_t(time));
                const bool clear = rng.chance(0.2);
                if (clear) {
                    e->call(kFnClearSharedBuffers, {});
                    drv::RVehicle_ClearSharedBuffers();
                } else {
                    if (rng.chance(0.5)) {
                        rig.veh.d.switch_time[9] = time - rng.range(-5, 40);
                        rig.veh.d.switch_time[10] = time - rng.range(-5, 40);
                    }
                    put_state(*e, rig, false);
                    e->call(kFnSetPerObjectBuffers, {kVeh});
                    drv::RVehicle_SetPerObjectBuffers(rig.veh, time);
                }
                ++t_buf.cases;
                d = compare_vehicle(*e, rig);
                if (d.empty())
                    same_words("SwitchData", e->m.mem(kSwitchData, 0x100),
                               reinterpret_cast<const uint8_t*>(sh.switch_data), 0x100, {}, d);
                if (d.empty())
                    same_words("DamageZones", e->m.mem(kDamageZones, 0x100),
                               reinterpret_cast<const uint8_t*>(sh.damage_zones), 0x100, {}, d);
                if (!d.empty()) t_buf.fail("caso " + std::to_string(c) + (clear ? " (Clear): " : ": ") + d);
                // TriggerFX 0xb/0xc e TriggerIlluminate.
                put_state(*e, rig, false);
                for (int k = 0; k < 6; ++k) {
                    const int32_t which = rng.range(0, 15);
                    if (rng.chance(0.3)) {
                        const int32_t when = rng.range(0, 6000);
                        e->call(kFnTriggerIlluminate, {kVeh, uint32_t(which), uint32_t(when)});
                        drv::RVehicle_TriggerIlluminate(rig.veh, which, when);
                    } else {
                        const int32_t type = rng.chance(0.5) ? 0xb : 0xc;
                        e->call(kFnTriggerFX, {kVeh, uint32_t(type), uint32_t(which), 0});
                        drv::RVehicle_TriggerFX(rig.veh, type, which);
                    }
                }
                ++t_fx.cases;
                d = compare_vehicle(*e, rig);
                if (!d.empty()) t_fx.fail("caso " + std::to_string(c) + ": " + d);
            }
            report("RVehicle::ComputeDamage", t_dmg, failures);
            report("RVehicle::SetPerObjectBuffers e ClearSharedBuffers", t_buf, failures);
            report("RVehicle::TriggerFX (0xb, 0xc) e TriggerIlluminate", t_fx, failures);
        }

        // GetNeedlePos.
        {
            Tally t;
            for (int c = 0; c < cases; ++c) {
                const int needle = rng.range(0, 3);
                const float value = rng.chance(0.1) ? rng.uni(-100, 0) : rng.uni(-10, needle == 2 ? 9000.0f : 400.0f);
                e->m.set_fpr(12, value);
                e->call(kFnGetNeedlePos, {kVeh, uint32_t(needle)});
                const float emu = e->m.fpr(0), port = drv::RVehicle_GetNeedlePos(value, needle);
                ++t.cases;
                if (fbits(emu) != fbits(port))
                    t.fail("caso " + std::to_string(c) + ": original " + hex(fbits(emu)) + ", porte " + hex(fbits(port)));
            }
            report("RVehicle::GetNeedlePos", t, failures);
        }

        // A animação procedural: SetProcAnimState e GetInstanceMatrix (0xff, 0xfe, 0xef com o BuildRotate).
        {
            Tally t, t_rot;
            uint64_t kinds[3] = {0, 0, 0};
            for (int c = 0; c < cases; ++c) {
                random_vehicle(rng, rig);
                if (rng.chance(0.1)) {  // sem esqueleto: a identidade
                    rig.veh.bones.clear();
                    rig.veh.d.bone_count = 0;
                    rig.veh.d.bones = 0;
                }
                for (int i = 0; i < 12; ++i)
                    rig.veh.d.params[i] = rng.chance(0.2) ? 0.0f : rng.uni(-400, 400);
                put_state(*e, rig, false);
                e->call(kFnSetProcAnimState, {kVeh});
                drv::ProcAnimContext ctx = drv::RSkeletalObj_SetProcAnimState(rig.veh);
                // Os estados (CARP::ProcAnimState, 0x20 bytes) com o índice do parâmetro no u16 +2.
                std::vector<uint8_t> states(16 * 0x20);
                for (size_t i = 0; i < states.size(); i += 2) {
                    const uint16_t v = uint16_t(rng.range(0, 14));
                    std::memcpy(states.data() + i, &v, 2);
                }
                std::memcpy(e->m.mem(kStates, uint32_t(states.size())), states.data(), states.size());
                ctx.states = states.data();
                for (int k = 0; k < 4; ++k) {
                    uint32_t inst[16];
                    float f[16];
                    for (float& x : f) x = rng.uni(-3, 3);
                    std::memcpy(inst, f, 64);
                    uint8_t* b = reinterpret_cast<uint8_t*>(inst);
                    const int kind = rng.range(0, 2);
                    b[0xd] = kind == 0 ? 0xff : kind == 1 ? 0xfe : 0xef;
                    b[0xc] = uint8_t(rng.next());
                    const uint16_t idx = uint16_t(kind == 1 ? rng.range(0, int(rig.veh.bones.size()) + 2)
                                                            : rng.range(0, kind == 2 ? 15 : 3));
                    std::memcpy(b + 0xe, &idx, 2);
                    ++kinds[kind];
                    std::memcpy(e->m.mem(kInst, 64), inst, 64);
                    std::memset(e->m.mem(kOut, 64), 0xcd, 64);
                    e->call(kFnGetInstanceMatrix, {kInst, 0, kOut, kStates});
                    float out[4][4];
                    drv::GetInstanceMatrix(inst, ctx, out);
                    ++t.cases;
                    std::string d;
                    if (!same_words("matriz", e->m.mem(kOut, 64), reinterpret_cast<const uint8_t*>(out), 64, {}, d))
                        t.fail("caso " + std::to_string(c) + " (função " + hex(b[0xd]).substr(6) + "): " + d);
                }
                // BuildRotate sozinho (eixos quaisquer).
                const float deg = rng.chance(0.2) ? 0.0f : rng.uni(-720, 720);
                const float ax = rng.uni(-2, 2), ay = rng.uni(-2, 2), az = rng.chance(0.1) ? 0.0f : rng.uni(-2, 2);
                e->m.set_fpr(12, deg);
                e->m.set_fpr(13, ax);
                e->m.set_fpr(14, ay);
                e->m.set_fpr(15, az);
                std::memset(e->m.mem(kOut, 64), 0xcd, 64);
                e->call(kFnBuildRotate, {kOut});
                float rot[4][4];
                std::memset(rot, 0xcd, 64);
                drv::BuildRotate(rot, deg, ax, ay, az);
                ++t_rot.cases;
                std::string d;
                if (!same_words("BuildRotate", e->m.mem(kOut, 64), reinterpret_cast<const uint8_t*>(rot), 64, {}, d))
                    t_rot.fail("caso " + std::to_string(c) + ": " + d);
            }
            std::printf("    instâncias 0xff %" PRIu64 ", 0xfe %" PRIu64 ", 0xef %" PRIu64 "\n", kinds[0], kinds[1],
                        kinds[2]);
            report("GetInstanceMatrix (SetProcAnimState, gProcAnimFunc)", t, failures);
            report("BuildRotate", t_rot, failures);
        }

        // RShadowMap::DrawSimpleShadow.
        {
            Tally t;
            uint64_t drawn = 0, reasons[6] = {};
            for (int c = 0; c < cases; ++c) {
                random_vehicle(rng, rig);
                random_car(rng, rig);
                if (rng.chance(0.1)) {  // a lata virada: os eixos inclinados
                    for (int i = 0; i < 4; ++i) rb->info->lever_world[i][1] = rng.uni(-3, 3);
                }
                const int32_t strength = rng.chance(0.7) ? 0x18 : rng.range(0, 80);
                const uint8_t type = uint8_t(rng.range(0, 3));
                const float w = rng.chance(0.5) ? 0.8f : rng.uni(0.2f, 1.5f), l = rng.uni(0.2f, 1.5f);
                rig.veh.d.render_type = type;
                put_state(*e, rig, false);
                e->m.write32(kStrength, uint32_t(strength));
                e->put_f32(kSizes + 8u * type, w);
                e->put_f32(kSizes + 8u * type + 4, l);
                e->shadow_drawn = false;
                e->call(kFnDrawSimpleShadow, {kShadowMap, kVeh});
                drv::ShadowQuad q;
                drv::RShadowMap_DrawSimpleShadow(*car, w, l, strength, q);
                drawn += q.drawn;
                ++reasons[q.reason];
                ++t.cases;
                std::string d;
                if (e->shadow_drawn != q.drawn) {
                    d = std::string("desenhada: original ") + (e->shadow_drawn ? "sim" : "não") + " (motivo do porte " +
                        std::to_string(q.reason) + ")";
                } else if (q.drawn) {
                    if (same_words("cantos", reinterpret_cast<const uint8_t*>(e->shadow_pos),
                                   reinterpret_cast<const uint8_t*>(q.corner), 64, {}, d) &&
                        same_words("coordenadas", reinterpret_cast<const uint8_t*>(e->shadow_uv),
                                   reinterpret_cast<const uint8_t*>(q.uv), 64, {}, d) &&
                        same_words("cor", reinterpret_cast<const uint8_t*>(e->shadow_colour),
                                   reinterpret_cast<const uint8_t*>(q.colour), 16, {}, d) &&
                        e->shadow_texture != 0x7e57)
                        d = "a textura";
                }
                if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
            }
            std::printf("    desenhadas %" PRIu64 "; não: veículo %" PRIu64 ", chão %" PRIu64 ", nível %" PRIu64
                        ", eixos %" PRIu64 "/%" PRIu64 "\n",
                        drawn, reasons[1], reasons[2], reasons[3], reasons[4], reasons[5]);
            report("RShadowMap::DrawSimpleShadow", t, failures);
        }

        // As luzes do PBondCar: GlareOn, GlareOff, HandleTurnSignals (o render com o TriggerFX original).
        {
            Tally t;
            uint64_t on = 0, signals = 0;
            for (int c = 0; c < cases / 2; ++c) {
                random_vehicle(rng, rig);
                random_car(rng, rig);
                drv::PBondCarData& cd = car->d;
                for (int i = 0; i < 16; ++i) cd.glare_on[i] = rng.chance(0.3) ? 1 : rng.chance(0.1) ? 2 : 0;
                cd.turn_signal_mode = rng.range(0, 3);
                cd.turn_signal_start = rng.range(0, 50000);
                cd.turn_signal_period = uint32_t(rng.range(1, 120));
                cd.turn_signal_lit = rng.uni(0, 1);
                cd.turn_signal_cancel = rng.uni(-1, 1);
                drv::simulation().steps = rng.range(0, 100000);
                put_state(*e, rig, false);
                for (int k = 0; k < 4; ++k) {
                    const int op = rng.range(0, 2);
                    if (op == 2) {
                        e->call(kFnHandleTurnSignals, {kCar});
                        drv::PBondCar_HandleTurnSignals(*car);
                        ++signals;
                    } else {
                        const int32_t g = rng.range(0, 15);
                        e->call(op == 0 ? kFnGlareOn : kFnGlareOff, {kCar, uint32_t(g)});
                        if (op == 0) drv::PBondCar_GlareOn(*car, g);
                        else drv::PBondCar_GlareOff(*car, g);
                        on += op == 0;
                    }
                }
                ++t.cases;
                std::string d = compare_car(*e, rig);
                if (d.empty()) d = compare_vehicle(*e, rig);
                if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
            }
            std::printf("    GlareOn %" PRIu64 ", HandleTurnSignals %" PRIu64 "; efeitos do RSceneObj %" PRIu64
                        " (porte %" PRIu64 ")\n",
                        on, signals, e->scene_fx, car->render_fx);
            report("PBondCar::GlareOn, GlareOff, HandleTurnSignals", t, failures);
        }

        // O IFeedback: a tabela do pontilhado e sequências de operações.
        {
            Tally t_dither;
            for (uint32_t i = 0; i < 256; ++i) {
                ++t_dither.cases;
                const uint8_t a = e->m.mem(0x3738d0 + i, 1)[0], b = drv::feedback_dither(i);
                if (a != b)
                    t_dither.fail("entrada " + std::to_string(i) + ": .data " + std::to_string(a) + ", porte " +
                                  std::to_string(b));
            }
            report("IFeedback: a tabela do pontilhado (0x3738d0)", t_dither, failures);
            Tally t;
            uint64_t ops = 0, pad_calls = 0;
            drv::FeedbackState& fs = drv::feedback_state();
            std::vector<PadCall> port_pad;
            fs.actuator = [&port_pad](int32_t port, int32_t motor, uint8_t value) {
                port_pad.push_back({port, motor, value});
            };
            for (int c = 0; c < std::max(1, cases / 8); ++c) {
                // O começo: as portas zeradas dos dois lados, as chaves sorteadas.
                drv::feedback_reset();
                std::memset(e->m.mem(kFbPorts, 4 * 0x34), 0, 4 * 0x34);
                fs.enabled = rng.chance(0.9) ? 1 : 0;
                fs.uber_vibration = rng.chance(0.9) ? 1 : 0;
                fs.paused = rng.chance(0.1) ? 1 : 0;
                fs.counter = rng.next();
                fs.threads = 0;
                e->m.write32(kFbEnabled, uint32_t(fs.enabled));
                e->m.write32(kUber + 0x994, uint32_t(fs.uber_vibration));
                e->m.write32(kFbPaused, uint32_t(fs.paused));
                e->m.write32(kFbCounter, fs.counter);
                e->m.write32(kFbThreads, 0);
                std::vector<drv::IFeedback> objs;
                std::vector<bool> alive;
                std::string d;
                for (int k = 0; k < 60 && d.empty(); ++k) {
                    e->pad.clear();
                    port_pad.clear();
                    const int op = rng.range(0, 9);
                    if (op == 0 || objs.empty()) {
                        const int32_t port = rng.range(0, 3);
                        const uint32_t a = kFb + 4u * uint32_t(objs.size());
                        e->call(kFnFbCtor, {a, uint32_t(port)});
                        objs.push_back(drv::IFeedback());
                        alive.push_back(true);
                        drv::IFeedback_Construct(objs.back(), port);
                    } else {
                        const size_t i = rng.next() % objs.size();
                        const uint32_t a = kFb + 4u * uint32_t(i);
                        if (op == 1) {
                            if (alive[i]) {
                                e->call(kFnFbDtor, {a, 0});
                                drv::IFeedback_Destruct(objs[i]);
                                alive[i] = false;
                            }
                        } else if (op == 2) {
                            const float s = rng.chance(0.5) ? rng.uni(0, 0.25f) : rng.uni(0.25f, 1.2f);
                            e->m.set_fpr(12, s);
                            e->call(kFnFbJolt, {a});
                            drv::IFeedback_ApplyJolt(objs[i], s);
                        } else if (op == 3) {
                            drv::CollisionInfo ci{};
                            ci.strength = rng.uni(-1.5f, 1.5f);
                            ci.flags = uint16_t(rng.chance(0.2) ? 0xe : rng.range(0, 20));
                            ci.tag = uint16_t(rng.chance(0.2) ? 0xe : rng.range(0, 20));
                            std::memcpy(e->m.mem(kCollInfo, sizeof ci), &ci, sizeof ci);
                            e->call(kFnFbCollision, {a, kCollInfo});
                            drv::IFeedback_AddCollisionInfo(objs[i], ci);
                        } else if (op == 4 || op == 5) {
                            const float amount = rng.uni(-0.2f, 1.2f), secs = rng.uni(-0.1f, 2);
                            e->m.set_fpr(12, amount);
                            e->m.set_fpr(13, secs);
                            e->call(op == 4 ? kFnFbRumble : kFnFbVibrate, {a});
                            if (op == 4) drv::IFeedback_SetRumble(objs[i], amount, secs);
                            else drv::IFeedback_SetVibrate(objs[i], amount, secs);
                        } else if (op == 6) {
                            // As bases dos motores (o que o jogo grava por fora: sorteado dos dois lados).
                            const int32_t port = objs[i].port;
                            const float sb = rng.uni(0, 1.5f), bb = rng.uni(0, 1.5f);
                            fs.ports[port].small_base = sb;
                            fs.ports[port].big_base = bb;
                            e->put_f32(kFbPorts + 0x34u * uint32_t(port) + 0x2c, sb);
                            e->put_f32(kFbPorts + 0x34u * uint32_t(port) + 0x30, bb);
                        } else if (op == 9 && rng.chance(0.2)) {
                            e->call(kFnFbPause, {});
                            drv::IFeedback_Pause();
                        } else {
                            e->call(kFnFbUpdate, {});
                            drv::IFeedback_Update();
                        }
                    }
                    ++ops;
                    pad_calls += e->pad.size();
                    if (!same_words("as portas", e->m.mem(kFbPorts, 4 * 0x34),
                                    reinterpret_cast<const uint8_t*>(fs.ports), 4 * 0x34, {}, d))
                        break;
                    if (e->m.read32(kFbCounter) != fs.counter) d = "o contador";
                    else if (int32_t(e->m.read32(kFbThreads)) != fs.threads) d = "as linhas de execução";
                    else if (e->pad != port_pad)
                        d = "as chamadas do PAD_setactuator (operação " + std::to_string(op) + ")";
                }
                ++t.cases;
                if (!d.empty()) t.fail("sequência " + std::to_string(c) + ": " + d);
            }
            fs.actuator = nullptr;
            drv::feedback_reset();
            std::printf("    operações %" PRIu64 ", PAD_setactuator %" PRIu64 ", guardas %" PRIu64 "\n", ops, pad_calls,
                        fs.guards);
            report("IFeedback (Update, ApplyJolt, AddCollisionInfo, SetRumble...)", t, failures);
        }

        // O InputConfigManager.
        {
            Tally t;
            drv::Bigf misc;
            const bool have_misc = misc.open(disc, "DRIVING/MISC.VIV");
            auto read_key = [](drv::Bigf& a, const std::string& key) {
                const int idx = a.find(key);
                if (idx < 0) return std::string();
                std::vector<uint8_t> b;
                a.read(idx, b);
                return std::string(b.begin(), b.end());
            };
            InputFileLoaderFn loader;
            loader.fn = [&](const std::string& path) {
                std::string key = path;
                for (char& ch : key)
                    if (ch == '/') ch = '\\';
                std::string s = read_key(arc, key);
                if (s.empty() && have_misc) s = read_key(misc, key);
                return s;
            };
            e->loader = &loader;
            int configs = 0;
            for (int c = 0; c < 12; ++c) {
                e->heap = kHeapBegin;
                e->files.clear();
                e->printed.clear();
                e->set_config.clear();
                drv::InputUberBond uber;
                // Dentro e fora das listas (o aviso); um valor negativo leria antes da lista no PS2 (o porte guarda).
                uber.driving_config = c < 2 ? 0 : rng.range(0, 3);
                uber.pov_config = c < 2 ? 0 : rng.range(0, 10);
                uber.inverted = rng.chance(0.3) ? rng.range(1, 5) : 0;
                e->m.write32(kUber + 0xa14, uint32_t(uber.driving_config));
                e->m.write32(kUber + 0x98c, uint32_t(uber.pov_config));
                e->m.write32(kUber + 0xa24, uint32_t(uber.inverted));
                std::memset(e->m.mem(kMgr, 0x30), 0xcd, 0x30);
                e->call(kFnIcmCtor, {kMgr});
                e->call(kFnIcmInit, {kMgr});
                drv::InputConfigManager* icm = new drv::InputConfigManager();
                drv::InputMap map;
                drv::InputConfigManager_InitAndPreload(*icm, loader.fn, uber, map);
                ++t.cases;
                std::string d;
                const uint8_t* em = e->m.mem(kMgr, 0x30);
                // O gerente (sem os ponteiros: as listas, os nomes, o FrontEnd.def).
                same_words("InputConfigManager", em, reinterpret_cast<const uint8_t*>(&icm->d), 0x30,
                           {{0x18, 0x20}, {0x24, 0x30}}, d);
                auto text_at = [&](uint32_t a) { return a ? std::string(e->cstr(a)) : std::string(); };
                for (int ty = 0; ty < 2 && d.empty(); ++ty) {
                    const uint32_t list = e->m.read32(kMgr + 0x18 + 4u * uint32_t(ty));
                    for (size_t k = 0; k < icm->configs[ty].size() && d.empty(); ++k) {
                        const uint32_t a = list + uint32_t(k) * 0x5c;
                        const drv::InputConfigInfo& ci = icm->configs[ty][k];
                        if (!same_words(("configuração " + std::to_string(ty) + "/" + std::to_string(k)).c_str(),
                                        e->m.mem(a, 0x5c), reinterpret_cast<const uint8_t*>(&ci), 0x5c, {{0x20, 0x24}},
                                        d))
                            break;
                        const std::string pt = ci.file ? icm->texts[ci.file - 1] : std::string();
                        if (text_at(e->m.read32(a + 0x20)) != pt) d = "o texto da configuração " + std::string(ci.name);
                        ++configs;
                    }
                }
                if (d.empty() && text_at(e->m.read32(kMgr + 0x28)) != icm->texts[icm->d.frontend - 1])
                    d = "o FrontEnd.def";
                if (d.empty() && e->printed.size() != icm->warnings.size())
                    d = "os avisos: original " + std::to_string(e->printed.size()) + ", porte " +
                        std::to_string(icm->warnings.size());
                if (d.empty() && (int32_t(e->m.read32(kUber + 0xa14)) != uber.driving_config ||
                                  int32_t(e->m.read32(kUber + 0x98c)) != uber.pov_config))
                    d = "o bloco da troca";
                // O SetConfig do fim: o texto da configuração do carro.
                const drv::InputConfigInfo* cur =
                    icm->d.current[0] >= 0 && size_t(icm->d.current[0]) < icm->configs[0].size()
                        ? &icm->configs[0][size_t(icm->d.current[0])]
                        : nullptr;
                if (d.empty() && (e->set_config.size() != 1 ||
                                  text_at(e->set_config[0]) != (cur && cur->file ? icm->texts[cur->file - 1] : "")))
                    d = "o SetConfig do InitAndPreload";
                // SetConfig(tipo, n), GetNumConfigs, GetCurrentConfig.
                for (int k = 0; k < 20 && d.empty(); ++k) {
                    e->set_config.clear();
                    const int32_t ty = rng.range(0, 1);
                    const int32_t n = icm->d.count[ty];
                    const int32_t cfg = rng.chance(0.3) || n == 0 ? -1 : rng.range(0, n - 1);
                    if (cfg == -1 && (icm->d.current[ty] < 0 || icm->d.current[ty] >= n)) continue;
                    e->call(kFnIcmSetType, {kMgr, uint32_t(ty), uint32_t(cfg)});
                    drv::InputConfigManager_SetConfigType(*icm, ty, cfg, uber, map);
                    const drv::InputConfigInfo& ci = icm->configs[ty][size_t(icm->d.current[ty])];
                    if (e->set_config.size() != 1 || text_at(e->set_config[0]) != (ci.file ? icm->texts[ci.file - 1] : ""))
                        d = "SetConfig(" + std::to_string(ty) + ", " + std::to_string(cfg) + ")";
                    else if (!same_words("InputConfigManager", em, reinterpret_cast<const uint8_t*>(&icm->d), 0x30,
                                         {{0x18, 0x20}, {0x24, 0x30}}, d))
                        break;
                    else if (int32_t(e->m.read32(kUber + 0xa14)) != uber.driving_config ||
                             int32_t(e->m.read32(kUber + 0x98c)) != uber.pov_config)
                        d = "o bloco da troca depois do SetConfig(" + std::to_string(ty) + ")";
                    for (int32_t q = 0; q < 3 && d.empty(); ++q) {
                        if (q < 2) {
                            e->call(kFnIcmNum, {kMgr, uint32_t(q)});
                            if (int32_t(e->m.gpr(R::v0)) != drv::InputConfigManager_GetNumConfigs(*icm, q))
                                d = "GetNumConfigs(" + std::to_string(q) + ")";
                        }
                        e->call(kFnIcmCurrent, {kMgr, uint32_t(q)});
                        if (d.empty() && int32_t(e->m.gpr(R::v0)) != drv::InputConfigManager_GetCurrentConfig(*icm, q))
                            d = "GetCurrentConfig(" + std::to_string(q) + ")";
                    }
                }
                if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
                if (c == 0)
                    std::printf("    Master.def: %d configurações do carro, %d do POV; textos %zu, guardas %" PRIu64 "\n",
                                icm->d.count[0], icm->d.count[1], icm->texts.size(), icm->guards);
                delete icm;
            }
            e->loader = nullptr;
            std::printf("    configurações conferidas %d\n", configs);
            report("InputConfigManager (InitAndPreload, SetConfig, Get...)", t, failures);
        }
        const drv::VehiclePending& vp = drv::vehicle_pending();
        std::printf("    pendentes do porte: mapa %" PRIu64 ", molas %" PRIu64 ", efeitos %" PRIu64 ", procanim %" PRIu64
                    ", guardas %" PRIu64 "\n",
                    vp.map_updates, vp.springs, vp.effects, vp.procanim_other, vp.guards);
        car->vehicle = nullptr;
        drv::simulation_reset(60);
        delete car;
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s (pc %08x, palavra %08x)\n", err.what(), err.pc, err.word);
        ++failures;
    }
    delete e;
    return failures;
}

}  // namespace drvcheck
