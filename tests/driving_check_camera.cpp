// nf_driving_check, parte 8 (tests/driving_check_camera.h): a câmera do jogador. O código ORIGINAL do DRIVING.ELF roda
// no interpretador MIPS com a VU0 ao lado do porte (driving/camera):
//  - RCameraIniLoader::LoadFile (0x1a49e0) sobre o data/render/camera.ini do MIS01.VIV (com o IniFiles e o DAFI
//    originais: só a leitura do arquivo é um gancho), para o carro "vanquish" e depois para o "vanquishsub" (os modos
//    com o braço rígido, o ruído e o "para cima" suavizado): as tabelas (modos, Heli com os braços, Bumper, Spline,
//    Ellipse com as alturas, Fixed, Dashboard, os braços do AutoDrive), as contagens, os índices e as constantes;
//  - Noise::Init (0x17bd00): as tabelas do ruído de Perlin;
//  - o construtor RPlayerCamera::RPlayerCamera (0x1b2620, com o RestartCamera e o diretor);
//  - quadro a quadro, RPlayerCamera::UpdateCamera (0x1b29e0) com a âncora (o corpo do Vanquish) andando por Paris perto
//    das paredes e do chão (a colisão da câmera: o WCollider com as listas de faixas, as barreiras e as faces ao longo
//    dos segmentos), com pausas, o olhar para trás, pedidos de outro modo Heli (as transições), reinícios, saltos, o
//    "para cima" do submarino, a máscara de barreiras e o caminho VU0 do gerente.
// Compara a câmera inteira (0x370 bytes), o RPlayerCamState, os globais do anti-vertigem e do tremor e a fila do
// diretor.
//
// O que não é comparado, e por quê:
//  - os ponteiros (a tabela virtual +0xa8, a fila de entrada +0x114, a âncora +0x118, +0x350..+0x36c; no RPlayerCamState
//    a câmera +0x00; os ponteiros das tabelas, comparadas pelo conteúdo);
//  - o w do braço suavizado (+0x2fc): lixo da pilha no PS2 quando o modo não tem ruído;
//  - nos braços Heli (+0x24..+0x2f, +0x3c) e do AutoDrive (+0x3c, +0x6c, +0x74..+0x7b, +0xfc): montados na pilha do
//    LoadFile, lixo no PS2.
// Fora do código original (ganchos nativos): memset, memmove, memcpy, FastAlloc/__builtin_new/operator new (zerados),
// as liberações, UFileLoader::FileLoad e MEM_size (o camera.ini), printf, ActionQueue::ActionQueue, WRoadNav::WRoadNav,
// UGroup::DataLocateTag e GetArray (nenhuma animação 'Cams'), RColorize::SetEnabled, e as entradas do carro na tabela
// virtual (o tipo "vanquish", a classe 0, o tipo secundário e o AIVehicle nulos, GetResetAvailable 0).

#include "driving_check_camera.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "driving_check_world.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kSim = 0x339190;

constexpr uint32_t kFnLoadFile = 0x1a49e0;
constexpr uint32_t kFnNoiseInit = 0x17bd00;
constexpr uint32_t kFnCameraCtor = 0x1b2620;
constexpr uint32_t kFnRestart = 0x1bbc90;
constexpr uint32_t kFnUpdate = 0x1b29e0;
constexpr uint32_t kFnSetMode = 0x1ba418;

// Os globais do original.
constexpr uint32_t kTables = 0x3339b0, kConstants = 0x333610, kModeGlobals = 0x4244b0;
constexpr uint32_t kShakeEye = 0x333a20, kVertigoCount = 0x333a38, kVertigoPoint = 0x333a40;
constexpr uint32_t kNoisePerm = 0x32f4e8, kNoiseGrad = 0x3d08a8;
constexpr uint32_t kOverrideCar = 0x33196c, kWeaponMgrPtr = 0x33a530;

// A RAM emulada.
constexpr uint32_t kBodies = 0x01000000;
constexpr uint32_t kInfos = 0x01002000;
constexpr uint32_t kCar = 0x01012000;
constexpr uint32_t kCarVt = 0x01014000;
constexpr uint32_t kRender = 0x01014400;
constexpr uint32_t kCarsVec = 0x01014500;
constexpr uint32_t kWeaponMgr = 0x01014600;
constexpr uint32_t kWeaponSlots = 0x01014700;
constexpr uint32_t kStrings = 0x01015400;
constexpr uint32_t kCam = 0x01016000;
constexpr uint32_t kIniBuf = 0x01017000;  // (o camera.ini: uns 45 KB)
constexpr uint32_t kScratchpad = 0x01028000;
constexpr uint32_t kFnCarType = 0x0102f000, kFnZero = 0x0102f010;
constexpr uint32_t kMgr = 0x01030000, kGrid = 0x01030100;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 7) {}
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

bool same_words(const std::string& what, const uint8_t* emu, const uint8_t* port, uint32_t n,
                std::initializer_list<std::pair<uint32_t, uint32_t>> skip, std::string& out) {
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

class CameraEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = 0, heap_end = 0;
    uint32_t ini_size = 0;
    uint32_t strtok_last = 0;

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
        // A biblioteca C (as versões da newlib do PS2 usam as instruções MMI de 128 bits que o interpretador não tem):
        // as mesmas regras, nativas.
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
            // strncpy: até o primeiro zero, o resto preenchido com zeros.
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
            // Só os formatos do LoadFile: %s e %d.
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
        // RCamera::RCamera (0x1a43d8) monta as identidades com PADDW/PEXEW/PEXCW (MMI, que o interpretador não tem): as
        // mesmas escritas, nativas (a matriz e a inversa identidade, +0x40 e +0x90 (0, 0, 0, 1), ativa, 33 graus).
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
        // O scratchpad do EE (0x70000000): a área livre que FindFaceInTriStrip usa para os vértices levados ao segmento.
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
        m.hook(kFnZero, "uma entrada do carro que devolve zero (falsa)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        auto entry = [this](uint32_t off, uint32_t fn) {
            m.write32(kCarVt + off, 0);
            m.write32(kCarVt + off + 4, fn);
        };
        entry(0x58, kFnZero);     // o AIVehicle
        entry(0x68, kFnZero);     // GetResetAvailable
        entry(0x80, kFnCarType);  // GetCarType
        entry(0x98, kFnZero);     // GetCarClass
        entry(0x290, kFnZero);    // GetSecondaryType
    }

    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 200'000'000);
    }
};

// Os braços do PS2 montados na pilha do LoadFile: só os campos escritos.
std::string compare_tables(CameraEmu& e, const drv::CameraTables& t) {
    std::string out;
    MipsMachine& m = e.m;
    const drv::CameraTableGlobals& g = t.tables;
    // Contagens e índices.
    if (!same_words("as contagens", m.mem(kTables + 0x3c, 0x1c), reinterpret_cast<const uint8_t*>(&g.heli_count), 0x1c,
                    {}, out))
        return out;
    if (!same_words("os índices", m.mem(kTables + 0x58, 11), reinterpret_cast<const uint8_t*>(&g.tumble_index), 11, {},
                    out))
        return out;
    if (g.autodrive_index >= 0 && m.read32(kTables + 0x30) != uint32_t(g.autodrive_prev_weapon))
        return "fAutoDrivePrevWeapIndex";
    // As constantes.
    const uint8_t* pc = reinterpret_cast<const uint8_t*>(&t.constants);
    if (!same_words("as constantes", m.mem(kConstants, 0x80), pc, 0x80, {{0x34, 0x40}, {0x4c, 0x50}}, out)) return out;
    // Os globais dos modos sem tabela.
    const uint8_t* pm = reinterpret_cast<const uint8_t*>(&t.modes);
    if (g.tumble_index >= 0 && !same_words("Tumble", m.mem(kModeGlobals, 0x20), pm, 0x20, {}, out)) return out;
    if ((g.world_anim_index >= 0 || g.rel_anim_index >= 0 || g.ai_path_anim_index >= 0) &&
        !same_words("a âncora das animações", m.mem(kModeGlobals + 0x20, 0x18), pm + 0x20, 0x18, {}, out))
        return out;
    // As tabelas.
    auto table = [&](const char* what, uint32_t ptr_off, const void* port, uint32_t count, uint32_t stride,
                     std::initializer_list<std::pair<uint32_t, uint32_t>> skip) -> bool {
        const uint32_t base = m.read32(kTables + ptr_off);
        for (uint32_t i = 0; i < count; ++i)
            if (!same_words(std::string(what) + " " + std::to_string(i), m.mem(base + stride * i, stride),
                            static_cast<const uint8_t*>(port) + stride * i, stride, skip, out))
                return false;
        return true;
    };
    if (!table("modo", 0x0c, nf::raw(g.modes), uint32_t(g.mode_count + 1), 0x1c, {})) return out;
    if (!table("Heli", 0x08, nf::raw(g.helis), uint32_t(g.heli_count), 0x4c, {{0x2c, 0x30}})) return out;
    for (int32_t i = 0; i < g.heli_count; ++i) {
        const drv::HeliCamInfo& h = t.heli(i);
        const uint32_t arms = m.read32(m.read32(kTables + 0x08) + 0x4c * uint32_t(i) + 0x2c);
        for (int32_t k = 0; k < h.arm_count; ++k)
            if (!same_words("Heli " + std::to_string(i) + " braço " + std::to_string(k), m.mem(arms + 0x40 * uint32_t(k), 0x40),
                            reinterpret_cast<const uint8_t*>(nf::raw(h.arms) + k), 0x40, {{0x24, 0x30}, {0x3c, 0x40}},
                            out))
                return out;
    }
    if (!table("Bumper", 0x00, nf::raw(g.bumpers), uint32_t(g.bumper_count), 0x40, {})) return out;
    if (!table("Dashboard", 0x04, nf::raw(g.dashboards), uint32_t(g.dashboard_count), 0xc0, {})) return out;
    if (!table("Spline", 0x10, nf::raw(g.splines), uint32_t(g.spline_count), 0x30, {})) return out;
    if (!table("Ellipse", 0x14, nf::raw(g.ellipses), uint32_t(g.ellipse_count), 0x2c, {{0x28, 0x2c}})) return out;
    for (int32_t i = 0; i < g.ellipse_count; ++i) {
        const drv::EllipseCamInfo& el = nf::raw(g.ellipses)[i];
        const uint32_t h = m.read32(m.read32(kTables + 0x14) + 0x2c * uint32_t(i) + 0x28);
        if (el.height_count > 0 &&
            std::memcmp(m.mem(h, uint32_t(el.height_count)), nf::raw(el.heights), size_t(el.height_count)) != 0)
            return "Ellipse " + std::to_string(i) + ": as alturas";
    }
    if (!table("Fixed", 0x18, nf::raw(g.fixeds), uint32_t(g.fixed_count), 0x28, {})) return out;
    if (g.autodrive_index >= 0) {
        if (!same_words("AutoDrive", m.mem(kModeGlobals + 0x38, 0x10), pm + 0x38, 0x10, {}, out)) return out;
        const uint32_t arms = m.read32(kModeGlobals + 0x48);
        for (int32_t k = 0; k < t.modes.autodrive_arm_count; ++k)
            if (!same_words("AutoDrive braço " + std::to_string(k), m.mem(arms + 0x100 * uint32_t(k), 0x100),
                            reinterpret_cast<const uint8_t*>(nf::raw(t.modes.autodrive_arms) + k), 0x100,
                            {{0x3c, 0x40}, {0x6c, 0x70}, {0x74, 0x7c}, {0xfc, 0x100}}, out))
                return out;
    }
    return out;
}

std::string compare_camera(CameraEmu& e, const drv::PlayerCamera& cam) {
    std::string out;
    MipsMachine& m = e.m;
    if (!same_words("RPlayerCamera", m.mem(kCam, 0x370), reinterpret_cast<const uint8_t*>(&cam.d), 0x370,
                    {{0xa8, 0xac}, {0x114, 0x11c}, {0x2fc, 0x300}, {0x350, 0x370}}, out))
        return out;
    // A âncora (no port só a presença).
    if ((m.read32(kCam + 0x118) != 0) != (cam.d.anchor != 0)) return "RPlayerCamera +0x118 (a âncora)";
    const uint32_t st = m.read32(kCam + 0x354);
    if (!same_words("RPlayerCamState", m.mem(st, 0x5c), reinterpret_cast<const uint8_t*>(&cam.state), 0x5c, {{0, 4}},
                    out))
        return out;
    if (!same_words("o olho do tremor (0x333a20)", m.mem(kShakeEye, 16),
                    reinterpret_cast<const uint8_t*>(cam.statics.shake_eye), 16, {}, out))
        return out;
    if (m.read32(kVertigoCount) != uint32_t(cam.statics.vertigo_count))
        return "o anti-vertigem (0x333a38): original " + std::to_string(int32_t(m.read32(kVertigoCount))) + ", porte " +
               std::to_string(cam.statics.vertigo_count);
    if (cam.statics.vertigo_count != 0 &&
        !same_words("o ponto do anti-vertigem (0x333a40)", m.mem(kVertigoPoint, 16),
                    reinterpret_cast<const uint8_t*>(cam.statics.vertigo_point), 16, {}, out))
        return out;
    // A fila do diretor: vazia nos dois e as bandeiras.
    const uint32_t dq = m.read32(kCam + 0x368);
    const uint32_t head = m.read32(dq);
    const bool emu_empty = m.read32(head) == head;
    if (emu_empty != cam.director_queue.empty()) return "a fila do diretor";
    if (m.mem(dq + 8, 1)[0] != cam.director_flags) return "as bandeiras do diretor";
    return out;
}

// A âncora andando: a posição, a velocidade e o referencial (direita, cima, frente) a partir do rumo, da inclinação e da
// rolagem.
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

int run_camera_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                      int frames, uint32_t seed) {
    std::printf("\n== a câmera do jogador: o código original do EE (interpretador MIPS + VU0) x o porte (LoadFile, "
                "RPlayerCamera) ==\n");
    int failures = 0;
    CameraEmu* e = new CameraEmu();
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
        if (!copy_world(e->m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
            std::printf("  %s\n", error.c_str());
            delete e;
            return failures + 1;
        }
        e->heap = (kWorldBegin + wc.bytes + 0xfff) & ~0xfffu;
        e->heap_end = kWorldEnd;
        e->ini_size = uint32_t(ini_bytes.size());
        put(e->m, kIniBuf, ini_bytes.data(), uint32_t(ini_bytes.size()));

        // O carro do jogador na RAM emulada: a âncora (+0x40 tipo 1, +0x44 bandeiras 0, +0x46 vaga 0, +0x48 o modelo, sem
        // animações 'Cams'), a tabela virtual, a lista dos carros da Simulation e o gerente de armas sem arma.
        MipsMachine& m = e->m;
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
        m.write32(kSim + 0xe2c, 0);  // as explosões: vazio
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

        // O ruído.
        Tally t_noise;
        e->call(kFnNoiseInit, {});
        drv::Noise_Init();
        ++t_noise.cases;
        {
            std::string out;
            if (!same_words("as permutações", m.mem(kNoisePerm, 0x2000),
                            reinterpret_cast<const uint8_t*>(drv::noise_perm_table()), 0x2000, {}, out) ||
                !same_words("os gradientes", m.mem(kNoiseGrad, 0x10b * 4),
                            reinterpret_cast<const uint8_t*>(drv::noise_grad_table()), 0x10b * 4, {}, out))
                t_noise.fail(out);
        }
        report("Noise::Init (as tabelas do ruído)", t_noise, failures);

        drv::Simulation& sim = drv::simulation();
        drv::simulation_reset(60);
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

        // As paredes de Paris (para pôr a câmera perto delas).
        struct Wall {
            uint32_t inst, index;
        };
        std::vector<Wall> walls;
        for (uint32_t i = 0; i < tw.instance_count(); ++i) {
            const drv::WCollisionInstance& ci = tw.instances()[i];
            if (!ci.article) continue;
            const auto* a = nf::p32_ptr<const drv::CollisionArticle>(ci.article);
            for (uint32_t k = 0; k < a->barrier_count; ++k) walls.push_back({i, k});
        }
        Rng rng(seed ^ 0xca3e);

        const char* const kCars[] = {"vanquish", "vanquishsub"};
        for (const char* car_type : kCars) {
            // As tabelas.
            Tally t_load;
            std::memset(m.mem(kStrings, 0x40), 0, 0x40);
            std::memcpy(m.mem(kStrings, 0x40), car_type, std::strlen(car_type));
            m.write32(kTables + 0x0c, 0);  // as tabelas do carro anterior: o LoadFile só lê com elas nulas
            e->call(kFnLoadFile, {0});
            tables->reset();
            drv::IniFile ini;
            ini.open(ini_bytes);
            drv::CameraCarInfo car;
            car.type = car_type;
            tables->load(ini, car);
            ++t_load.cases;
            const std::string tout = compare_tables(*e, *tables);
            if (!tout.empty()) t_load.fail(tout);
            int heli_modes = 0;
            std::vector<int32_t> helis;
            for (int32_t i = 0; i < tables->tables.mode_count; ++i)
                if (tables->mode(i).type == drv::kCamHeli) helis.push_back(i);
            heli_modes = int(helis.size());
            const std::string what = std::string("LoadFile (") + car_type + ": " + std::to_string(tables->tables.mode_count) +
                                     " modos, " + std::to_string(heli_modes) + " Heli)";
            report(what.c_str(), t_load, failures);

            // A câmera.
            put_pose(Pose{}, rb, info);
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
                const drv::WCollisionMgr& mgr = tw.mgr();
                m.write32(wc.mgr + 4, mgr.query_stamp);
                m.write32(wc.mgr + 0x2c, mgr.barrier_mask);
                m.write32(wc.mgr + 0x30, mgr.vu0_path);
                for (uint32_t i = 0; i < wc.count; ++i)
                    m.write32(wc.insts + 0x40 * i + 0x10, tw.instances()[i].query_stamp);
            };
            sim.state = 0;
            sim.steps = 0;
            put_body();
            put_sim();
            Tally t_ctor;
            std::memset(m.mem(kCam, 0x370), 0, 0x370);
            e->call(kFnCameraCtor, {kCam});
            const uint32_t heap_mark = e->heap;
            cam->construct(*tables, ctx);
            ++t_ctor.cases;
            {
                const std::string out = compare_camera(*e, *cam);
                if (!out.empty()) t_ctor.fail(out);
            }
            report((std::string("RPlayerCamera::RPlayerCamera (") + car_type + ")").c_str(), t_ctor, failures);

            // Os quadros.
            Tally t_frames;
            uint64_t collisions_before = cam->collisions, restarts = 0, mode_requests = 0, pauses = 0, looks = 0,
                     jumps = 0, pending_before = drv::camera_pending();
            const uint64_t ground_before = cam->ground_hits, barrier_before = cam->barrier_hits;
            uint64_t transition_frames = 0, vertigo_frames = 0, noise_frames = 0, rigid_frames = 0, shake_frames = 0;
            Pose pose;
            auto place = [&]() {
                if (!walls.empty() && rng.chance(0.85)) {
                    const Wall& w = walls[rng.next() % walls.size()];
                    const drv::WCollisionInstance& ci = tw.instances()[w.inst];
                    const auto* a = nf::p32_ptr<const uint8_t>(ci.article);
                    const auto* ah = reinterpret_cast<const drv::CollisionArticle*>(a);
                    const auto* b = reinterpret_cast<const drv::CollisionBarrier*>(a + 0x20 + ah->barrier_offset) + w.index;
                    const float t = rng.uni(0, 1);
                    const float l[3] = {b->x0 + (b->x1 - b->x0) * t, b->y0, b->z0 + (b->z1 - b->z0) * t};
                    float mm[16];
                    drv::WCollisionInstance_MakeMatrix(tw.mgr(), ci, mm, true);
                    const float lt[3] = {l[0] - mm[12], l[1] - mm[13], l[2] - mm[14]};
                    double p[3];
                    for (int c = 0; c < 3; ++c) p[c] = lt[0] * mm[c * 4 + 0] + lt[1] * mm[c * 4 + 1] + lt[2] * mm[c * 4 + 2];
                    const float side = rng.uni(-6.0f, 6.0f);
                    const double nx = (b->z1 - b->z0), nz = -(b->x1 - b->x0);
                    const double nl = std::sqrt(nx * nx + nz * nz);
                    if (nl > 0) {
                        p[0] += nx / nl * side;
                        p[2] += nz / nl * side;
                    }
                    pose.x = p[0];
                    pose.y = p[1];
                    pose.z = p[2];
                } else {
                    const drv::WCollisionInstance& ci = tw.instances()[rng.next() % tw.instance_count()];
                    float c[3];
                    drv::WCollisionInstance_CalcPosition(tw.mgr(), ci, c);
                    pose.x = c[0] + rng.uni(-20, 20);
                    pose.y = c[1];
                    pose.z = c[2] + rng.uni(-20, 20);
                }
                float h = 0;
                const float at[3] = {float(pose.x), float(pose.y) + 3.0f, float(pose.z)};
                if (tw.world_height_at_point(at, h)) pose.y = double(h) + 0.55;
                pose.yaw = rng.uni(-3.14159f, 3.14159f);
                pose.pitch = rng.chance(0.5) ? rng.uni(-0.25f, 0.25f) : 0.0;
                pose.roll = rng.chance(0.3) ? rng.uni(-0.12f, 0.12f) : 0.0;
                pose.speed = rng.chance(0.2) ? 0.0 : rng.uni(-8.0f, 45.0f);
                pose.yaw_rate = rng.uni(-1.0f, 1.0f);
                pose.pitch_rate = rng.uni(-0.05f, 0.05f);
            };
            place();
            for (int f = 0; f < frames; ++f) {
                // A âncora anda (a física do carro não entra: a câmera só lê o corpo).
                const double dt = 1.0 / 60.0;
                pose.yaw += pose.yaw_rate * dt;
                pose.pitch = std::clamp(pose.pitch + pose.pitch_rate * dt, -0.35, 0.35);
                const double cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pose.pitch);
                pose.x += sy * cp * pose.speed * dt;
                pose.y += std::sin(pose.pitch) * pose.speed * dt;
                pose.z += cy * cp * pose.speed * dt;
                if (rng.chance(0.03)) pose.speed = rng.uni(-8.0f, 45.0f);
                if (rng.chance(0.02)) pose.yaw_rate = rng.uni(-1.5f, 1.5f);
                if (rng.chance(0.015)) {
                    place();
                    ++jumps;
                }
                put_pose(pose, rb, info);
                // O resto do mundo da câmera.
                if (rng.chance(0.97)) ++sim.steps;  // às vezes o mesmo passo (a câmera não atualiza)
                if (sim.state == 3 ? rng.chance(0.2) : rng.chance(0.01)) {
                    sim.state = sim.state == 3 ? 0 : 3;
                    if (sim.state == 3) ++pauses;
                }
                if (rng.chance(0.02)) {
                    tw.mgr().barrier_mask = rng.chance(0.7) ? 0x10u : 0xf0u;
                    tw.mgr().vu0_path = rng.chance(0.15) ? 1u : 0u;
                }
                put_body();
                put_sim();
                const uint32_t st = m.read32(kCam + 0x354);
                if (rng.chance(0.02)) {
                    cam->state.look_back ^= 1u;
                    m.write32(st + 0x48, cam->state.look_back);
                    ++looks;
                }
                if (rng.chance(0.02)) {
                    cam->state.up_count = rng.range(0, 30);
                    cam->state.up_hold = rng.chance(0.3) ? 1u : 0u;
                    m.write32(st + 0x58, uint32_t(cam->state.up_count));
                    m.write32(st + 0x54, cam->state.up_hold);
                }
                if (!helis.empty() && rng.chance(0.015)) {
                    const int32_t k = helis[rng.next() % helis.size()];
                    cam->SetCameraModeByIndex(k);
                    for (int r : {R::t0, R::t1, R::t2, R::t3}) m.set_gpr(r, 0);  // os argumentos 5 a 8: zero
                    e->call(kFnSetMode, {kCam, uint32_t(k), 0, 0});
                    ++mode_requests;
                }
                if (rng.chance(0.008)) {
                    e->heap = heap_mark;
                    e->call(kFnRestart, {kCam});
                    cam->restart(ctx);
                    ++restarts;
                }
                e->call(kFnUpdate, {kCam});
                cam->update(ctx);
                ++t_frames.cases;
                {
                    const drv::CameraModeInfo& md = tables->mode(cam->d.mode);
                    if (md.type == drv::kCamHeli) {
                        const drv::HeliCamInfo& hi = tables->heli(md.index);
                        if (hi.noise_amount > 0) ++noise_frames;
                        if (hi.rigid_arm) ++rigid_frames;
                    }
                    if (cam->d.transition > 0) ++transition_frames;
                    if (cam->statics.vertigo_count > 0) ++vertigo_frames;
                    if (md.shake) ++shake_frames;
                }
                const std::string out = compare_camera(*e, *cam);
                if (!out.empty()) {
                    t_frames.fail("quadro " + std::to_string(f) + ": " + out);
                    if (t_frames.bad > 20) break;
                }
            }
            char what2[200];
            std::snprintf(what2, sizeof(what2),
                          "UpdateCamera (%s; %" PRIu64 " colisões, %" PRIu64 " pedidos de modo, %" PRIu64
                          " reinícios, %" PRIu64 " pausas, %" PRIu64 " saltos, %" PRIu64 " olhares)",
                          car_type, cam->collisions - collisions_before, mode_requests, restarts, pauses, jumps, looks);
            report(what2, t_frames, failures);
            std::printf("    colisões da câmera: %" PRIu64 " com o chão (faces das faixas), %" PRIu64 " com barreiras; quadros "
                        "em transição %" PRIu64 ", com o anti-vertigem %" PRIu64 ", modo com ruído %" PRIu64
                        ", braço rígido %" PRIu64 ", com tremor %" PRIu64 "\n",
                        cam->ground_hits - ground_before, cam->barrier_hits - barrier_before, transition_frames,
                        vertigo_frames, noise_frames, rigid_frames, shake_frames);
            if (drv::camera_pending() != pending_before)
                std::printf("    (pendentes do port nestes quadros: %" PRIu64 ")\n", drv::camera_pending() - pending_before);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  o interpretador parou: %s\n", err.what());
        ++failures;
    }
    delete anchor;
    delete cam;
    delete tables;
    delete e;
    return failures;
}

}  // namespace drvcheck
