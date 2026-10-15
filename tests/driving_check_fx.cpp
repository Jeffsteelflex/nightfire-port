// nf_driving_check, parte 21 (tests/driving_check_fx.h): os efeitos do DRIVING.ELF, o código ORIGINAL no interpretador
// MIPS (com a VU0 em modo macro) lado a lado com o porte (driving/fx). docs/driving.md seção 25.
//
//  a) CalcEmitVal com inteiros sorteados.
//  b) A biblioteca: o RParticleLibrary::RParticleLibrary e o Reload (o 'dflt' do .data), o AddSystem (o MungeData e o
//     MungeDataForPlatform) com tipos sorteados e os das chaves da galeria, o FindSystemViaTag com etiquetas sorteadas.
//  c) RParticle::ResetParticle com tipos, emissores, eixos, velocidades e frações sorteados (a partícula e o RRandom).
//  d) O gerente e o cache: o construtor do RParticleSystemManager, sistemas criados pelo AddOrRefresh (os dois criadores)
//     com emissores sorteados, o SetSystem, o UpdateSpawnAllSystems, o CreateParticles e o UpdateAndRenderAllSystems
//     (o JustUpdate e o JustRender: as listas do RenderBlock) por quadros, comparando o mapa dos sistemas, os sistemas,
//     o cache inteiro, as listas e o RRandom.
//  e) A galeria: o GFXGallery::GFXGallery sobre o gall1.gal do disco, o GFX::LookupReference, o GFX::Trigger de efeitos
//     sorteados (com e sem RSceneObj, grudados, com a velocidade) e quadros do GFX::Update (o FRAME_Draw, o CANVAS_Draw,
//     o PARTICLE_Draw, o PARTICLE_Add, os grudados, o CANVAS_HasStopped) com as partículas, e o recorte das telas
//     (CULL_GetNextCanvas/CULL_SetCanvas), comparando as telas, as listas, os emissores, os sistemas e as partículas.
//  f) As luzes (as tabelas do .data; seqüências de AddExplosion, AddMissile, os dois AddPositionalLight e o
//     FinishLights, com o RColorize nos dois modos), as marcas (AddDecal e o Draw sobre o decal.dat do MIS01.VIV; o
//     ScorchGround sobre um mundo sintético dado pelo WWorldPos::FindClosestFace nos dois lados, o HeightAtPoint do
//     original), o rastro dos mísseis (as tabelas do Init, o Update(COORD4 &), o Update(void), o Draw), o
//     Missile::Render (a luz e o rastro, mísseis sorteados) e os eventos 9, 250, 196 e 257 (os GFX::Trigger, o
//     TriggerFX, o StartNuclearSequence e o alcance do corte que eles pedem).
//
// Fora do código original (ganchos nativos): a memória (FastAlloc, UMemory::Alloc, MEM_fill, MEM_copy, memset), o
// printf/sprintf, os arquivos (GSystem::FILE_*), o GHud, o atan2f (o nf::fm::atan2 dos dois lados), o TIMER, a matriz da
// câmera do giro (0x297480), o EAGL::DrawArray (os Set* do construtor do cache e o Draw: a contagem de cada lista), o
// contexto de texturas, o Upload do cache (a cópia do bloco para o scratchpad: o DMA), o Simulation::GetRigidBody, o
// RSceneObj (GetInstancePosition e TransformMatrixByInstancePosition: a mesma conta nos dois lados), os sons (o ASound
// das trilhas 'gs': nenhum), o RGlareManager e o RDebris (as trilhas não portadas). Na parte f, num interpretador à
// parte: o EAGLMalloc, a lista de desenho do EAGL (0x286018..0x286080), o Simulation::GetSimpleRigidBody, o
// WWorldPos::FindClosestFace (o mundo sintético), o GFX::Trigger e o GFX::Trigger(char *) (anotados), o
// SWeaponManager::StartNuclearSequence, o RSceneObj::GetVelocity e o TriggerFX (a tabela dos objetos falsos).

#include "driving_check_fx.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/ai/sim_random.h"
#include "driving/bigf.h"
#include "driving/fx/decals.h"
#include "driving/fx/fx_events.h"
#include "driving/fx/fx_lights.h"
#include "driving/fx/fx_render.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/missile_fx.h"
#include "driving/fx/particles.h"
#include "driving/game/explosion.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/world_vis.h"
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

// As funções originais.
constexpr uint32_t kFnCalcEmitVal = 0x1e5cd8, kFnSysCtor = 0x1e5e10, kFnSetSystem = 0x1e5fa0;
constexpr uint32_t kFnAddOrRefresh = 0x1e61f8, kFnGetSystem = 0x1e62e0, kFnCreateParticles = 0x1e6330;
constexpr uint32_t kFnSysUpdate = 0x1e65c0, kFnUpdateSpawnAll = 0x1e67e0, kFnUpdateAndRenderAll = 0x1e6e48;
constexpr uint32_t kFnMgrCtor = 0x1e6f78, kFnLibCtor = 0x1e7e10, kFnLibReload = 0x1e7f60, kFnLibAdd = 0x1e8040;
constexpr uint32_t kFnLibFindTag = 0x1e8008, kFnMungePlatform = 0x1f3188, kFnResetParticle = 0x1f29d8;
constexpr uint32_t kFnGalleryCtor = 0x233618, kFnGfxLookup = 0x232f48, kFnGfxTrigger = 0x2330b0;
constexpr uint32_t kFnGfxUpdate = 0x232e80, kFnCullStart = 0x234198, kFnCullNext = 0x2341a8, kFnCullSet = 0x234258;
// Os ganchos.
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnFastFree = 0x2cfe50, kFnUAlloc = 0x2d03b8, kFnUSize = 0x2d04d8;
constexpr uint32_t kFnMemFill = 0x260300, kFnMemCopy = 0x2601f0, kFnMemset = 0x2a8038, kFnPrintf = 0x2a8148;
constexpr uint32_t kFnSprintf = 0x2a9098, kFnStrcasecmp = 0x2a9198, kFnStrcmp = 0x2a9508, kFnAtan2f = 0x2c0618;
constexpr uint32_t kFnTimerRemove = 0x2913f8, kFnTimerFreq = 0x291840, kFnViewMatrix = 0x297480;
constexpr uint32_t kFnTexContext = 0x1cf340, kFnFindTexture = 0x1d0420, kFnDrawArrayDraw = 0x29c4b0;
constexpr uint32_t kFnDrawArraySet[4] = {0x299da8, 0x29c0f8, 0x29c250, 0x29c2e8};
constexpr uint32_t kFn28c318 = 0x28c318, kFnUpload = 0x1f4760, kFnGetRigidBody = 0x1ff088;
constexpr uint32_t kFnFullPath = 0x24b3e8, kFnFileLoad = 0x24b460, kFnTheApp = 0x23c618, kFnDbMessage = 0x245ee0;
constexpr uint32_t kFnGetInstPos = 0x1c9368, kFnTransformByInst = 0x1c95f0, kFnSoundNew = 0x2fca38;
constexpr uint32_t kFnASound = 0x2ed260, kFnAddGlare = 0x1f1f88, kFnDebrisAdd = 0x1d95e8;

// Os globais do PS2.
constexpr uint32_t kManagerPtr = 0x3c82f0, kGalleryPtr = 0x33aad0, kRendererPtr = 0x333760;
constexpr uint32_t kRandSeed = 0x333e60, kRandMul = 0x333e64, kTime = 0x334010, kDensity = 0x425148;
constexpr uint32_t kToggle = 0x3390b0, kLastAdded = 0x33aad8;
// As listas do RenderBlock.
constexpr uint32_t kNormCoords = 0x3e9db0, kNormUv = 0x3f1db0, kNormCol = 0x3e3ff0;
constexpr uint32_t kAddCoords = 0x3f9db0, kAddUv = 0x401db0, kAddCol = 0x3e6070;
constexpr uint32_t kTriCoords = 0x409db0, kTriCol = 0x3e80f0;
constexpr uint32_t kArrNorm = 0x3e9b50, kArrAdd = 0x3e9c50, kArrTri = 0x3e9d50;

// A RAM emulada.
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;
constexpr uint32_t kGalFile = 0x01800000;    // o gall1.gal (até 0x80000)
constexpr uint32_t kFake = 0x01900000;       // o RRenderer, a câmera da vista, o RCamera, o GHud, a matriz
constexpr uint32_t kRenderer = kFake, kViewCam = kFake + 0x100, kRCamera = kFake + 0x200, kApp = kFake + 0x300;
constexpr uint32_t kViewMatrix = kFake + 0x600, kBodies = kFake + 0x700;  // 48 corpos de 0x40
constexpr uint32_t kScratch = kFake + 0x2000;  // os argumentos dos casos (0x1000)
constexpr uint32_t kScenes = 0x01a00000;  // os RSceneObj falsos (só o endereço importa)

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2121) {}
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

void put(MipsMachine& m, uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }
void get(const MipsMachine& m, uint32_t addr, void* dst, uint32_t n) { std::memcpy(dst, m.mem(addr, n), n); }

bool same_words(const char* what, const void* emu, const void* port, uint32_t n, std::string& out,
                uint32_t skip_from = 0, uint32_t skip_to = 0) {
    const uint8_t* a = static_cast<const uint8_t*>(emu);
    const uint8_t* b = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
        if (o >= skip_from && o < skip_to) continue;
        uint32_t x, y;
        std::memcpy(&x, a + o, 4);
        std::memcpy(&y, b + o, 4);
        if (x != y) {
            out = std::string(what) + " +0x" + hex(o).substr(3) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

// As posições de um mapa da STL da GCC 2.95 (o _Rb_tree: o cabeçalho com o pai em +4, a esquerda em +8, a direita em
// +0xc; o valor do nó em +0x10): os pares (chave, valor) em ordem.
std::vector<std::pair<uint32_t, uint32_t>> emu_map(const MipsMachine& m, uint32_t header) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    if (!header) return out;
    uint32_t x = m.read32(header + 8);
    int guard = 0;
    while (x != header && guard++ < 100000) {
        out.push_back({m.read32(x + 0x10), m.read32(x + 0x14)});
        if (m.read32(x + 0xc)) {
            x = m.read32(x + 0xc);
            while (m.read32(x + 8)) x = m.read32(x + 8);
        } else {
            uint32_t y = m.read32(x + 4);
            while (x == m.read32(y + 0xc)) {
                x = y;
                y = m.read32(y + 4);
            }
            if (m.read32(x + 0xc) != y) x = y;
        }
    }
    return out;
}

// O bloco crescente dos emissores do porte (gfx_emitter_alloc): a ordem dos endereços é a da alocação, como no heap
// emulado, para o mapa dos sistemas andar na mesma ordem nos dois lados.
std::vector<uint8_t>* g_port_pool = nullptr;
size_t g_port_pool_used = 0;
drv::ParticleEmitter* pool_alloc(uint32_t n) {
    const size_t bytes = size_t(n) * sizeof(drv::ParticleEmitter);
    const size_t at = (g_port_pool_used + 63) & ~size_t(63);
    if (!g_port_pool || at + bytes > g_port_pool->size()) return new drv::ParticleEmitter[n]();
    g_port_pool_used = at + bytes;
    auto* p = reinterpret_cast<drv::ParticleEmitter*>(g_port_pool->data() + at);
    std::memset(p, 0, bytes);
    return p;
}
void pool_free(drv::ParticleEmitter*) {}

// As matrizes dos RSceneObj falsos (o mesmo nos dois lados): uma rotação e uma posição por (objeto, instância).
std::map<std::pair<uint32_t, int32_t>, std::array<float, 16>> g_scene_mats;
bool scene_matrix(uint32_t scene, int32_t instance, float m[4][4]) {
    auto it = g_scene_mats.find({scene, instance});
    if (it == g_scene_mats.end()) {
        std::memset(m, 0, 64);
        m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
        return true;
    }
    std::memcpy(m, it->second.data(), 64);
    return true;
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::map<uint32_t, uint32_t> sizes;   // UMemory::Alloc: o tamanho de cada bloco
    std::map<uint32_t, uint32_t> draws;   // EAGL::DrawArray::Draw: a contagem de cada lista no último JustRender
    struct TriDraw {
        uint32_t count;
        std::vector<float> coords;
        std::vector<uint32_t> colours;
    };
    std::vector<TriDraw> tri_draws;       // os riscos de cada bloco (a lista é a mesma memória para os dois blocos)
    std::string error;
    uint64_t errors = 0, glares = 0, debris = 0;

    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > kHeapEnd) throw nf::MipsError("o heap emulado acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        sizes[a] = n;
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
    float getf(uint32_t a) const { return ps2::ffloat(m.read32(a)); }
    void putf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto newf = [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u))); };
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", newf);
        m.hook(kFnUAlloc, "UMemory::Alloc", newf);
        m.hook(kFnUSize, "UMemory (tamanho do bloco)", [this](MipsMachine& mm) {
            auto it = sizes.find(uint32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, it == sizes.end() ? 0 : it->second);
        });
        m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kFnMemFill, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
        });
        m.hook(kFnMemCopy, "MEM_copy", [](MipsMachine& mm) {
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
        m.hook(kFnSprintf, "sprintf", [](MipsMachine& mm) {
            mm.write32(uint32_t(mm.gpr(R::a0)), 0);
            mm.set_gpr(R::v0, 0);
        });
        auto lower = [](std::string s) {
            for (char& c : s) c = char(std::tolower(uint8_t(c)));
            return s;
        };
        m.hook(kFnStrcasecmp, "strcasecmp", [lower](MipsMachine& mm) {
            const std::string a = lower(cstr(mm, uint32_t(mm.gpr(R::a0)))), b = lower(cstr(mm, uint32_t(mm.gpr(R::a1))));
            mm.set_gpr(R::v0, uint32_t(a == b ? 0 : (a < b ? -1 : 1)));
        });
        m.hook(kFnStrcmp, "strcmp", [](MipsMachine& mm) {
            const std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, uint32_t(a == b ? 0 : (a < b ? -1 : 1)));
        });
        m.hook(kFnAtan2f, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(kFnTimerRemove, "TIMER_removetask", [](MipsMachine&) {});
        m.hook(kFnTimerFreq, "TIMER_getfrequency", [](MipsMachine& mm) { mm.set_gpr(R::v0, kApp); });
        m.hook(kFnViewMatrix, "(a matriz da câmera)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kViewMatrix); });
        m.hook(kFnTexContext, "RTextureContextManager::GetContext", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnFindTexture, "RTextureContext::FindOrCreateTexture", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        for (uint32_t f : kFnDrawArraySet) m.hook(f, "EAGL::DrawArray::Set*", [](MipsMachine&) {});
        m.hook(kFnDrawArrayDraw, "EAGL::DrawArray::Draw", [this](MipsMachine& mm) {
            const uint32_t arr = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a1));
            draws[arr] = n;
            if (arr == kArrTri) {
                TriDraw td;
                td.count = n;
                td.coords.resize(size_t(n) * 4);
                td.colours.resize(n);
                std::memcpy(td.coords.data(), mm.mem(kTriCoords, n * 16), size_t(n) * 16);
                std::memcpy(td.colours.data(), mm.mem(kTriCol, n * 4), size_t(n) * 4);
                tri_draws.push_back(std::move(td));
            }
        });
        m.hook(kFn28c318, "(0x28c318)", [](MipsMachine&) {});
        m.hook(kFnUpload, "RParticleParticleCache::Upload", [](MipsMachine& mm) {
            // O DMA do bloco do quadro corrente para o scratchpad (o cacheOp e o 0x2a6528 do original).
            const uint32_t c = uint32_t(mm.gpr(R::a0)), blk = uint32_t(mm.gpr(R::a1));
            const uint32_t cur = mm.read32(c + 0x10040);
            const uint32_t src = c + cur * 0x8000 + 0x40 + blk * 0x4000;
            std::memcpy(mm.mem(MipsMachine::kScratchpad, 0x4000), mm.mem(src, 0x4000), 0x4000);
            mm.write32(c + 8, blk);
        });
        m.hook(kFnGetRigidBody, "Simulation::GetRigidBody", [](MipsMachine& mm) {
            mm.set_gpr(R::v0, kBodies + uint32_t(mm.gpr(R::a1) & 0x3f) * 0x40);
        });
        m.hook(kFnFullPath, "GSystem::FILE_FullPathname", [](MipsMachine& mm) {
            const std::string s = cstr(mm, uint32_t(mm.gpr(R::a0)));
            put(mm, uint32_t(mm.gpr(R::a1)), s.c_str(), uint32_t(s.size() + 1));
        });
        m.hook(kFnFileLoad, "GSystem::FILE_Load", [](MipsMachine& mm) { mm.set_gpr(R::v0, kGalFile); });
        m.hook(kFnTheApp, "GHud::TheApp", [](MipsMachine& mm) { mm.set_gpr(R::v0, kApp); });
        m.hook(kFnDbMessage, "GHud::dbMessage", [](MipsMachine&) {});
        m.hook(kFnGetInstPos, "RSceneObj::GetInstancePosition", [](MipsMachine& mm) {
            float mat[4][4];
            scene_matrix(uint32_t(mm.gpr(R::a0)), int32_t(mm.gpr(R::a1)), mat);
            put(mm, uint32_t(mm.gpr(R::a2)), mat, 64);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(kFnTransformByInst, "RSceneObj::TransformMatrixByInstancePosition", [](MipsMachine& mm) {
            float mat[4][4], inst[4][4], out[4][4];
            const uint32_t a = uint32_t(mm.gpr(R::a2));
            get(mm, a, mat, 64);
            scene_matrix(uint32_t(mm.gpr(R::a0)), int32_t(mm.gpr(R::a1)), inst);
            drv::VU0_MATRIX4_mult(mat, inst, out);
            put(mm, a, out, 64);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(kFnSoundNew, "ABaseSound::operator_new", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnASound, "ASound::ASound", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnAddGlare, "RGlareManager::AddGlare", [this](MipsMachine&) { ++glares; });
        m.hook(kFnDebrisAdd, "RDebris::Add", [this](MipsMachine&) { ++debris; });
        // O RRenderer falso: +4 a vista (o RCamera em +0, a margem 4 em +0x50), +0x40 a posição da câmera, +0x78 o
        // relógio; o RCamera com o fov em +0xa4; o GHud com +0x2a0 = 0.
        m.write32(kRendererPtr, kRenderer);
        m.write32(kRenderer + 4, kViewCam);
        m.write32(kViewCam, kRCamera);
        putf(kViewCam + 0x50, 4.0f);
        putf(kRCamera + 0xa4, 45.0f);
        m.write32(kRenderer + 0x78, kApp);
        m.write32(kApp + 0x2a0, 0);
        m.write32(kDensity, drv::particle_density());
    }

    // Os dois lados com o mesmo RRandom e o mesmo relógio.
    void sync_globals() {
        m.write32(kToggle, uint32_t(drv::particle_render_toggle()));
        m.write32(kRandSeed, drv::render_random().seed);
        m.write32(kRandMul, drv::render_random().multiplier);
        m.write32(kTime, drv::rtime_data().time);
    }
    bool same_random(std::string& why) {
        if (m.read32(kRandSeed) != drv::render_random().seed) {
            why = "RRandom: original " + hex(m.read32(kRandSeed)) + ", porte " + hex(drv::render_random().seed);
            return false;
        }
        return true;
    }
    void view_matrix(const float v[16]) { put(m, kViewMatrix, v, 64); }
    void camera(const float eye[4]) { put(m, kRenderer + 0x40, eye, 16); }
};

void random_vec(Rng& r, float* v, int n, float lo, float hi) {
    for (int i = 0; i < n; ++i) v[i] = r.real(lo, hi);
}

void random_unit(Rng& r, float v[3]) {
    for (;;) {
        random_vec(r, v, 3, -1.0f, 1.0f);
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 0.1f) continue;
        for (int i = 0; i < 3; ++i) v[i] /= l;
        return;
    }
}

void random_quat(Rng& r, float q[4]) {
    for (;;) {
        random_vec(r, q, 4, -1.0f, 1.0f);
        const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (l < 0.1f) continue;
        for (int i = 0; i < 4; ++i) q[i] /= l;
        return;
    }
}

// Um GenericParticleData sorteado (os campos nas faixas dos dados do disco, às vezes nas bordas).
drv::ParticleData random_data(Rng& r) {
    drv::ParticleData d{};
    d.spread_x = r.real(0.0f, 2.0f);
    d.speed = r.real(-0.5f, 1.0f);
    d.spread_z = r.real(0.0f, 2.0f);
    d.count = uint32_t(r.range(0, 120));
    d.side_speed = r.real(0.0f, 0.5f);
    d.speed_spread = r.real(0.0f, 0.3f);
    d.axis_speed = r.real(0.0f, 0.5f);
    d.flags = r.next() & 0x3ff;
    d.drag = r.real(0.5f, 1.0f);
    d.tag = r.next();
    d.alpha_byte = uint8_t(r.chance(0.5) ? 0 : r.next());
    d.unknown2d[0] = uint8_t(r.next());
    d.unknown2d[1] = uint8_t(r.range(-3, 3));
    d.size = r.real(0.0f, 4.0f);
    d.size_change = r.real(0.0f, 6.0f);
    d.life = uint32_t(r.chance(0.1) ? r.range(250, 400) : r.range(1, 200));
    d.on_time = uint32_t(r.range(0, 120));
    d.off_time = uint32_t(r.chance(0.5) ? 0 : r.range(1, 120));
    d.initial = uint32_t(r.chance(0.5) ? 0 : r.range(1, 50));
    d.gravity = r.real(-0.3f, 0.3f);
    d.gravity_spread = r.real(0.0f, 0.2f);
    d.spin = r.chance(0.5) ? 0.0f : r.real(-1.0f, 1.0f);
    d.max_size = r.real(0.0f, 4.0f);
    random_vec(r, d.uv, 4, 0.0f, 1.0f);
    for (auto& c : d.colour) random_vec(r, c, 4, 0.0f, 255.0f);
    d.size_spread = r.chance(0.5) ? 0.0f : r.real(0.0f, 1.0f);
    d.alpha_spread = r.chance(0.5) ? 0.0f : r.real(0.0f, 1.0f);
    d.index = r.next();
    return d;
}

uint32_t call_f(Emu& e, uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0, float f13 = 0) {
    e.m.set_fpr(12, f12);
    e.m.set_fpr(13, f13);
    if (!e.call(fn, args)) return 0xdeadbeefu;
    return uint32_t(e.m.gpr(R::v0));
}

// ----------------------------------------------------------------------------------------------- a) CalcEmitVal

void check_calc_emit(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    for (int i = 0; i < cases; ++i) {
        const int32_t a = r.range(0, 300), b = r.range(1, 300), c = r.range(1, 200), d = r.chance(0.3) ? 0 : r.range(0, 200);
        call_f(e, kFnCalcEmitVal, {uint32_t(a), uint32_t(b), uint32_t(c), uint32_t(d)});
        const uint32_t emu = e.m.fpr_bits(0);
        const uint32_t port = ps2::fbits(drv::CalcEmitVal(a, b, c, d));
        ++t.cases;
        if (emu != port)
            t.fail("CalcEmitVal(" + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," +
                   std::to_string(d) + "): original " + hex(emu) + ", porte " + hex(port));
    }
    report("CalcEmitVal", t, failures);
}

// --------------------------------------------------------------------------------------------- b) a biblioteca

// A biblioteca do original (o RParticleLibrary: +0 os dados) num gerente emulado; os dados em +0, a contagem em +0xb000
// (64 bits), o mapa em +0xb008.
bool compare_library(Emu& e, uint32_t lib, const drv::RParticleLibrary& p, std::string& why) {
    const uint32_t data = e.m.read32(lib);
    const uint32_t n = e.m.read32(data + 0xb000);
    if (n != uint32_t(p.count)) {
        why = "biblioteca: " + std::to_string(n) + " tipos no original, " + std::to_string(p.count) + " no porte";
        return false;
    }
    for (uint32_t i = 0; i < n; ++i) {
        char w[48];
        std::snprintf(w, sizeof w, "tipo %u", i);
        if (!same_words(w, e.m.mem(data + i * 0xb0, 0xb0), &p.systems[i], 0xb0, why)) return false;
    }
    const auto tags = emu_map(e.m, e.m.read32(data + 0xb008));
    if (tags.size() != p.tags.size()) {
        why = "biblioteca: " + std::to_string(tags.size()) + " etiquetas no original, " + std::to_string(p.tags.size());
        return false;
    }
    size_t k = 0;
    for (const auto& kv : p.tags) {
        const auto& et = tags[k++];
        if (et.first != kv.first || et.second != data + kv.second * 0xb0) {
            why = "etiqueta " + hex(kv.first) + ": original " + hex(et.first) + "->" + hex(et.second);
            return false;
        }
    }
    return true;
}

void check_library(Emu& e, Rng& r, const std::vector<drv::ParticleData>& gallery_types, int cases, int& failures) {
    Tally t, tf, tm;
    // A cada 40 casos uma biblioteca nova (o construtor e o Reload), e AddSystem com tipos sorteados e da galeria.
    uint32_t lib = 0;
    drv::RParticleLibrary* p = new drv::RParticleLibrary();
    int in_lib = 0;
    for (int i = 0; i < cases; ++i) {
        if (i % 40 == 0) {
            lib = e.alloc(16);
            if (!e.call(kFnLibCtor, {lib}) || !e.call(kFnLibReload, {lib})) {
                t.fail("RParticleLibrary: " + e.error);
                break;
            }
            *p = drv::RParticleLibrary();
            drv::RParticleLibrary_Reload(*p);
            in_lib = 1;
        }
        drv::ParticleData d = (!gallery_types.empty() && r.chance(0.5))
                                  ? gallery_types[size_t(r.range(0, int(gallery_types.size()) - 1))]
                                  : random_data(r);
        const uint32_t tag = r.chance(0.2) ? 0x64666c74u : uint32_t(r.range(1, 40));
        const uint32_t src = kScratch;
        put(e.m, src, &d, sizeof d);
        const uint32_t ei = call_f(e, kFnLibAdd, {lib, src, tag});
        const int32_t pi = drv::RParticleLibrary_AddSystem(*p, d, tag);
        ++in_lib;
        ++t.cases;
        std::string why;
        if (int32_t(ei) != pi) {
            t.fail("AddSystem: original " + std::to_string(int32_t(ei)) + ", porte " + std::to_string(pi));
        } else if (!same_words("o dado de quem chama", e.m.mem(src, 0xb0), &d, 0xb0, why) ||
                   !compare_library(e, lib, *p, why)) {
            t.fail(why);
        }
        // FindSystemViaTag: etiquetas sorteadas (as que existem e as que não).
        for (int k = 0; k < 3; ++k) {
            const uint32_t q = r.chance(0.3) ? 0x64666c74u : uint32_t(r.range(0, 45));
            const uint32_t er = call_f(e, kFnLibFindTag, {lib, q});
            const drv::ParticleData* pr = drv::RParticleLibrary_FindSystemViaTag(*p, q);
            const uint32_t data = e.m.read32(lib);
            ++tf.cases;
            if (er != data + uint32_t(pr - p->systems) * 0xb0)
                tf.fail("FindSystemViaTag(" + hex(q) + "): original " + hex(er) + ", porte o tipo " +
                        std::to_string(pr - p->systems));
        }
        // MungeDataForPlatform com origem e destino diferentes.
        {
            drv::ParticleData a = random_data(r), b{};
            put(e.m, kScratch, &a, sizeof a);
            std::memset(e.m.mem(kScratch + 0x100, 0xb0), 0, 0xb0);
            call_f(e, kFnMungePlatform, {kScratch, kScratch + 0x100});
            drv::RParticleLibrary_MungeDataForPlatform(a, b);
            ++tm.cases;
            if (!same_words("MungeDataForPlatform", e.m.mem(kScratch + 0x100, 0xb0), &b, 0xb0, why)) tm.fail(why);
        }
        if (in_lib > 250) i = (i / 40 + 1) * 40 - 1;  // a biblioteca enche: uma nova
    }
    delete p;
    report("RParticleLibrary: Reload, AddSystem (MungeData)", t, failures);
    report("RParticleLibrary::FindSystemViaTag", tf, failures);
    report("RParticleLibrary::MungeDataForPlatform", tm, failures);
}

// ------------------------------------------------------------------------------------------- c) ResetParticle

void check_reset_particle(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    for (int i = 0; i < cases; ++i) {
        drv::ParticleData d = random_data(r);
        drv::ParticleData munged{};
        drv::RParticleLibrary_MungeDataForPlatform(d, munged);
        if (r.chance(0.5)) munged = d;  // às vezes os dados sem o munge (as bordas)
        drv::ParticleEmitter em{};
        random_vec(r, em.pos, 4, -100.0f, 100.0f);
        random_unit(r, em.side);
        random_unit(r, em.dir);
        em.sticky = r.chance(0.5) ? -1 : r.range(0, 63);
        float src[3], axis[3], vel[3], delta[3];
        random_vec(r, src, 3, -200.0f, 200.0f);
        random_unit(r, axis);
        random_vec(r, vel, 3, -30.0f, 30.0f);
        random_vec(r, delta, 3, -2.0f, 2.0f);
        const bool with_vel = r.chance(0.5);
        const float a = r.chance(0.2) ? 0.0f : r.real(0.0f, 1.0f), ticks = r.chance(0.2) ? 0.0f : float(r.range(1, 3));
        float view[16];
        random_vec(r, view, 16, -1.0f, 1.0f);
        view[15] = 1.0f;
        drv::particles_set_view_matrix(view);
        e.view_matrix(view);
        drv::Particle pp;
        uint8_t garbage[32];
        for (uint8_t& g : garbage) g = uint8_t(r.next());
        std::memcpy(&pp, garbage, 32);
        const uint32_t ap = kScratch, asrc = kScratch + 0x40, adata = kScratch + 0x80, aem = kScratch + 0x140;
        const uint32_t aaxis = kScratch + 0x180, avel = kScratch + 0x1c0, adelta = kScratch + 0x200;
        put(e.m, ap, garbage, 32);
        put(e.m, asrc, src, 12);
        put(e.m, adata, &munged, 0xb0);
        put(e.m, aem, &em, 0x40);
        put(e.m, aaxis, axis, 12);
        put(e.m, avel, vel, 12);
        put(e.m, adelta, delta, 12);
        drv::render_random().seed = r.next() & 0xffff;
        e.sync_globals();
        call_f(e, kFnResetParticle, {ap, asrc, adata, aem, aaxis, with_vel ? avel : 0u, adelta}, a, ticks);
        drv::ResetParticle(a, ticks, pp, src, munged, em, axis, with_vel ? vel : nullptr, delta);
        ++t.cases;
        std::string why;
        if (e.errors) {
            t.fail("ResetParticle: " + e.error);
            e.errors = 0;
        } else if (!same_words("a partícula", e.m.mem(ap, 32), &pp, 32, why) || !e.same_random(why)) {
            t.fail(why);
        }
    }
    report("RParticle::ResetParticle", t, failures);
}

// -------------------------------------------------------------------------------- d) o gerente e o cache

// Os ponteiros que os dois lados guardam: os emissores (e as cópias dos móveis) e as velocidades.
struct PtrMap {
    std::map<uint32_t, uint32_t> port_to_emu;
    uint32_t emu(uint32_t port) const {
        if (port == 0) return 0;
        auto it = port_to_emu.find(port);
        return it == port_to_emu.end() ? 0xbad0bad0u : it->second;
    }
};

bool compare_system(Emu& e, uint32_t es, const drv::RParticleSystem& ps, uint32_t libdata,
                    const drv::RParticleLibrary& lib, const PtrMap& pm, std::string& why) {
    uint8_t buf[0xb0];
    get(e.m, es, buf, 0xb0);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&ps);
    if (!same_words("sistema", buf, p, 0x30, why)) return false;
    auto w = [&](uint32_t o) {
        uint32_t v;
        std::memcpy(&v, buf + o, 4);
        return v;
    };
    if (w(0x30) != libdata + uint32_t(nf::raw(ps.data) - lib.systems) * 0xb0) {
        why = "sistema +0x30 (o tipo): original " + hex(w(0x30));
        return false;
    }
    const bool movable = ps.vtable == drv::kMovableParticleSystemVtable;
    auto mapped = [&](uint32_t port) {
        if (movable && port == nf::p32_u(&ps.copy)) return es + 0x70;
        return pm.emu(port);
    };
    if (w(0x34) != mapped(nf::p32_u(nf::raw(ps.emitter)))) {
        why = "sistema +0x34 (o emissor): original " + hex(w(0x34)) + ", porte " + hex(mapped(nf::p32_u(nf::raw(ps.emitter))));
        return false;
    }
    if (w(0x40) != mapped(ps.source)) {
        why = "sistema +0x40 (a fonte): original " + hex(w(0x40)) + ", porte " + hex(mapped(ps.source));
        return false;
    }
    if (w(0x44) != mapped(ps.velocity)) {
        why = "sistema +0x44 (a velocidade): original " + hex(w(0x44)) + ", porte " + hex(mapped(ps.velocity));
        return false;
    }
    if (!same_words("sistema", buf + 0x38, p + 0x38, 8, why) || !same_words("sistema", buf + 0x48, p + 0x48, 0x1c, why))
        return false;
    if (movable && !same_words("sistema (a cópia do emissor)", buf + 0x70, p + 0x70, 0x40, why, 0x18, 0x1c)) return false;
    return true;
}

bool compare_manager(Emu& e, uint32_t mgr, const drv::RParticleSystemManager& pm, const PtrMap& map, std::string& why) {
    if (e.m.read32(mgr) != pm.last_tick) {
        why = "gerente +0 (o tique): original " + hex(e.m.read32(mgr)) + ", porte " + hex(pm.last_tick);
        return false;
    }
    const uint32_t lib = mgr + 8, libdata = e.m.read32(lib);
    if (!compare_library(e, lib, pm.library, why)) return false;
    const auto es = emu_map(e.m, e.m.read32(e.m.read32(mgr + 4)));
    if (es.size() != pm.systems.size()) {
        why = "sistemas: " + std::to_string(es.size()) + " no original, " + std::to_string(pm.systems.size());
        return false;
    }
    size_t k = 0;
    for (const auto& kv : pm.systems) {
        const auto& ek = es[k++];
        if (ek.first != map.emu(kv.first)) {
            why = "a chave " + std::to_string(k - 1) + ": original " + hex(ek.first);
            return false;
        }
        if (!compare_system(e, ek.second, *kv.second, libdata, pm.library, map, why)) {
            why = "sistema " + std::to_string(k - 1) + ": " + why;
            return false;
        }
    }
    // O cache inteiro.
    const uint32_t c = mgr + 0x40;
    const drv::RParticleParticleCache& pc = pm.cache;
    if (e.m.read32(c + 4) != uint32_t(pc.count) || e.m.read32(c + 0x10040) != uint32_t(pc.current) ||
        e.m.read32(c + 0x10044) != uint32_t(pc.spawned) || e.m.read32(c + 0xc) != pc.last_time) {
        why = "cache: contagem " + std::to_string(e.m.read32(c + 4)) + "/" + std::to_string(pc.count) + " quadro " +
              std::to_string(e.m.read32(c + 0x10040)) + "/" + std::to_string(pc.current) + " novas " +
              std::to_string(e.m.read32(c + 0x10044)) + "/" + std::to_string(pc.spawned);
        return false;
    }
    if (!same_words("cache (as partículas)", e.m.mem(c + 0x40, 0x10000), pc.particles, 0x10000, why)) {
        // A partícula inteira dos dois lados (o quadro e o índice).
        const uint8_t* a = e.m.mem(c + 0x40, 0x10000);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(pc.particles);
        for (uint32_t o = 0; o < 0x10000; o += 32) {
            if (std::memcmp(a + o, b + o, 32) == 0) continue;
            why += " (quadro " + std::to_string(o / 0x8000) + " partícula " + std::to_string((o % 0x8000) / 32) + ":";
            for (int w = 0; w < 8; ++w) {
                uint32_t x, y;
                std::memcpy(&x, a + o + w * 4, 4);
                std::memcpy(&y, b + o + w * 4, 4);
                why += " " + hex(x) + (x == y ? "" : "/" + hex(y));
            }
            why += ")";
            break;
        }
        return false;
    }
    return true;
}

// As listas do RenderBlock do último JustRender (o quadro duplo que ele usou: o de antes da troca).
bool compare_lists(Emu& e, const drv::RParticleSystemManager& pm, std::string& why) {
    if (e.m.read32(kToggle) != uint32_t(drv::particle_render_toggle())) {
        why = "o quadro duplo: original " + std::to_string(e.m.read32(kToggle)) + ", porte " +
              std::to_string(drv::particle_render_toggle());
        return false;
    }
    const uint32_t t = 1u - e.m.read32(kToggle);
    const int blocks = (pm.cache.count + 0x1ff) / 0x200;
    size_t tri = 0;
    for (int b = 0; b < blocks; ++b) {
        struct L {
            const drv::ParticleDrawLists::Sprites* p;
            uint32_t arr, coords, uv, col;
            const char* name;
        };
        const L ls[2] = {{&pm.lists.normal[b], kArrNorm, kNormCoords, kNormUv, kNormCol, "normais"},
                         {&pm.lists.additive[b], kArrAdd, kAddCoords, kAddUv, kAddCol, "aditivos"}};
        for (const L& l : ls) {
            const uint32_t arr = l.arr + t * 0x80 + uint32_t(b) * 0x40;
            auto it = e.draws.find(arr);
            const uint32_t n = it == e.draws.end() ? 0u : it->second;
            if (n != l.p->colours.size()) {
                why = std::string("retângulos ") + l.name + " do bloco " + std::to_string(b) + ": " + std::to_string(n) +
                      " no original, " + std::to_string(l.p->colours.size());
                return false;
            }
            const uint32_t base = t * 0x400 + uint32_t(b) * 0x200;
            if (n && (!same_words("Coordinates", e.m.mem(l.coords + base * 16, n * 16), l.p->coords.data(), n * 16, why) ||
                      !same_words("UVUVs", e.m.mem(l.uv + base * 16, n * 16), l.p->uvuv.data(), n * 16, why) ||
                      !same_words("Colours", e.m.mem(l.col + base * 4, n * 4), l.p->colours.data(), n * 4, why)))
                return false;
        }
        const size_t pn = pm.lists.tri_colours[b].size();
        if (pn) {
            if (tri >= e.tri_draws.size() || e.tri_draws[tri].count != pn) {
                why = "riscos do bloco " + std::to_string(b) + ": " + std::to_string(pn) + " vértices no porte";
                return false;
            }
            const Emu::TriDraw& td = e.tri_draws[tri++];
            if (!same_words("os riscos", td.coords.data(), pm.lists.tri_coords[b].data(), uint32_t(pn * 16), why) ||
                !same_words("as cores dos riscos", td.colours.data(), pm.lists.tri_colours[b].data(), uint32_t(pn * 4),
                            why))
                return false;
        }
    }
    if (tri != e.tri_draws.size()) {
        why = "riscos: " + std::to_string(e.tri_draws.size()) + " desenhos no original, " + std::to_string(tri);
        return false;
    }
    return true;
}

// Um gerente nos dois lados: o construtor original e o do porte (o mesmo Reload).
// Os sistemas do porte soltos dos emissores (o bloco dos emissores do caso vai ser reaproveitado) e apagados.
void drop_port_systems() {
    if (drv::RParticleSystemManager* pm = drv::particle_manager()) {
        for (auto& kv : pm->systems) kv.second->emitter = nullptr;
        drv::RParticleSystemManager_Reset(*pm);
    }
}

uint32_t new_managers(Emu& e) {
    const uint32_t mgr = e.alloc(0x100c0);
    if (!e.call(kFnMgrCtor, {mgr})) return 0;
    drv::RParticleSystemManager_Init();
    return mgr;
}

// Um sistema sozinho: o construtor (com o InitData), o SetSystem e passos do Update com o cache vazio, num gerente que
// fica (a biblioteca com tipos sorteados).
void check_system(Emu& e, Rng& r, const std::vector<drv::ParticleData>& gallery_types, int cases, int& failures) {
    Tally t;
    e.heap = kHeap;
    e.sizes.clear();
    const uint32_t mgr = new_managers(e);
    if (!mgr) {
        t.fail("RParticleSystemManager: " + e.error);
        report("RParticleSystem: o construtor, o SetSystem, o Update", t, failures);
        return;
    }
    drv::RParticleSystemManager& pm = *drv::particle_manager();
    for (int i = 0; i < 12; ++i) {
        drv::ParticleData d = (!gallery_types.empty() && r.chance(0.7))
                                  ? gallery_types[size_t(r.range(0, int(gallery_types.size()) - 1))]
                                  : random_data(r);
        put(e.m, kScratch, &d, sizeof d);
        call_f(e, kFnLibAdd, {mgr + 8, kScratch, uint32_t(i + 5)});
        drv::RParticleLibrary_AddSystem(pm.library, d, uint32_t(i + 5));
    }
    const uint32_t libdata = e.m.read32(mgr + 8);
    const uint32_t esys = e.alloc(0xb0), eem = e.alloc(0x40), evel = e.alloc(16);
    static drv::RParticleSystem ps;
    static drv::ParticleEmitter pem;
    static float pvel[4];
    PtrMap map;
    map.port_to_emu[nf::p32_u(&pem)] = eem;
    map.port_to_emu[nf::p32_u(pvel)] = evel;
    for (int c = 0; c < cases; ++c) {
        random_vec(r, pem.pos, 3, -50.0f, 50.0f);
        pem.pos[3] = 1.0f;
        random_unit(r, pem.side);
        random_unit(r, pem.dir);
        pem.tag = uint32_t(r.range(4, 17));
        pem.sticky = r.chance(0.7) ? 0xff : r.range(0, 63);
        put(e.m, eem, &pem, 0x40);
        random_vec(r, pvel, 3, -20.0f, 20.0f);
        put(e.m, evel, pvel, 12);
        const float lod = r.chance(0.3) ? 0.0f : r.real(0.0f, 500.0f);
        // Os dois com o mesmo lixo antes do construtor.
        uint8_t garbage[0xb0];
        for (uint8_t& g : garbage) g = uint8_t(r.next());
        put(e.m, esys, garbage, 0xb0);
        std::memcpy(&ps, garbage, 0xb0);
        call_f(e, kFnSysCtor, {esys, eem, 1}, lod);
        drv::RParticleSystem_Construct(ps, lod, &pem, true);
        if (r.chance(0.3)) {
            e.m.write32(esys + 0x44, evel);
            ps.velocity = nf::p32_u(pvel);
        }
        // O cache vazio dos dois lados.
        e.m.write32(mgr + 0x40 + 4, 0);
        e.m.write32(mgr + 0x40 + 0x10044, 0);
        pm.cache.count = 0;
        pm.cache.spawned = 0;
        std::memset(e.m.mem(mgr + 0x40 + 0x40, 0x10000), 0, 0x10000);
        std::memset(pm.cache.particles, 0, sizeof pm.cache.particles);
        e.m.write32(mgr + 0x40 + 0x10040, uint32_t(pm.cache.current));
        drv::render_random().seed = r.next() & 0xffff;
        e.sync_globals();
        std::string why;
        bool bad = false;
        const int steps = r.range(1, 8);
        for (int s = 0; s < steps && !bad; ++s) {
            if (r.chance(0.1)) {
                const uint32_t tag = uint32_t(r.range(4, 17));
                call_f(e, kFnSetSystem, {esys, tag});
                drv::RParticleSystem_SetSystem(ps, tag);
                pem.tag = e.m.read32(eem + 0x2c);  // o SetSystem grava no emissor dos dois lados
            }
            // O emissor anda.
            for (int k = 0; k < 3; ++k) pem.pos[k] += r.real(-1.0f, 1.0f);
            put(e.m, eem, pem.pos, 12);
            const int32_t ticks = r.range(1, 3);
            call_f(e, kFnSysUpdate, {esys, uint32_t(ticks)});
            drv::RParticleSystem_Update(ps, ticks);
            ++t.cases;
            if (e.errors) {
                t.fail("Update: " + e.error);
                e.errors = 0;
                bad = true;
            } else if (!compare_system(e, esys, ps, libdata, pm.library, map, why) || !e.same_random(why)) {
                t.fail("passo " + std::to_string(s) + ": " + why);
                bad = true;
            } else if (e.m.read32(mgr + 0x40 + 0x10044) != uint32_t(pm.cache.spawned) ||
                       !same_words("as partículas novas", e.m.mem(mgr + 0x40 + 0x40 + (1 - pm.cache.current) * 0x8000,
                                                                  uint32_t(pm.cache.spawned) * 32),
                                   pm.cache.particles[1 - pm.cache.current], uint32_t(pm.cache.spawned) * 32, why)) {
                t.fail("passo " + std::to_string(s) + ": " + why + " (novas " +
                       std::to_string(e.m.read32(mgr + 0x40 + 0x10044)) + "/" + std::to_string(pm.cache.spawned) + ")");
                bad = true;
            }
        }
    }
    drop_port_systems();
    report("RParticleSystem: o construtor, o SetSystem, o Update", t, failures);
}

// O cache sozinho: partículas sorteadas no quadro corrente, o JustUpdate (o UpdateBlock e o SaveForNextFrame) e o
// JustRender (o RenderBlock: as listas), num gerente que fica.
void check_cache(Emu& e, Rng& r, const std::vector<drv::ParticleData>& gallery_types, int cases, int& failures) {
    Tally t, tl;
    e.heap = kHeap;
    e.sizes.clear();
    const uint32_t mgr = new_managers(e);
    if (!mgr) {
        t.fail("RParticleSystemManager: " + e.error);
        report("RParticleParticleCache: JustUpdate", t, failures);
        return;
    }
    drv::RParticleSystemManager& pm = *drv::particle_manager();
    const int ntypes = 20;
    for (int i = 0; i < ntypes; ++i) {
        drv::ParticleData d = (!gallery_types.empty() && r.chance(0.7))
                                  ? gallery_types[size_t(r.range(0, int(gallery_types.size()) - 1))]
                                  : random_data(r);
        put(e.m, kScratch, &d, sizeof d);
        call_f(e, kFnLibAdd, {mgr + 8, kScratch, uint32_t(i + 5)});
        drv::RParticleLibrary_AddSystem(pm.library, d, uint32_t(i + 5));
    }
    static float bodies[64][4];
    for (int i = 0; i < 64; ++i) {
        random_vec(r, bodies[i], 3, -20.0f, 20.0f);
        bodies[i][3] = 1.0f;
        put(e.m, kBodies + uint32_t(i) * 0x40 + 0x10, bodies[i], 16);
    }
    drv::particles_rigid_pos() = [](int32_t slot, float out[4]) {
        std::memcpy(out, bodies[slot & 0x3f], 16);
        return true;
    };
    e.m.write32(kGalleryPtr, 0);
    drv::particles_sticky_pos() = nullptr;
    const uint32_t c = mgr + 0x40;
    PtrMap map;
    for (int k = 0; k < std::max(1, cases / 20); ++k) {
        // O cache: um quadro corrente sorteado, as partículas com tipos da biblioteca e vidas curtas.
        const int32_t count = r.chance(0.2) ? r.range(500, 1024) : r.range(0, 600);
        const int32_t cur = r.range(0, 1);
        pm.cache.count = count;
        pm.cache.current = cur;
        pm.cache.spawned = 0;
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < 1024; ++i) {
                drv::Particle& p = pm.cache.particles[b][i];
                random_vec(r, p.pos, 3, -50.0f, 50.0f);
                random_vec(r, p.vel, 3, -1.0f, 1.0f);
                p.life = uint8_t(r.range(1, 6));
                p.flags = uint8_t(r.next());
                p.gravity = int8_t(r.next());
                p.type = uint8_t(r.range(0, ntypes));
                p.nibbles = uint8_t(r.next());
                p.drag = uint8_t(r.next());
                p.sticky = int8_t(r.chance(0.8) ? -1 : r.range(0, 63));
                p.spin = int8_t(r.next());
            }
        e.m.write32(c + 4, uint32_t(count));
        e.m.write32(c + 0x10040, uint32_t(cur));
        e.m.write32(c + 0x10044, 0);
        put(e.m, c + 0x40, pm.cache.particles, 0x10000);
        std::string why;
        const int updates = r.range(1, 3);
        for (int u = 0; u < updates; ++u) {
            e.sync_globals();
            e.call(0x1f4878, {c});  // JustUpdate
            drv::RParticleParticleCache_JustUpdate(pm.cache);
        }
        ++t.cases;
        if (e.errors) {
            t.fail("JustUpdate: " + e.error);
            e.errors = 0;
            continue;
        }
        if (!compare_manager(e, mgr, pm, map, why)) {
            t.fail(why);
            continue;
        }
        e.sync_globals();
        e.draws.clear();
        e.tri_draws.clear();
        e.call(0x1f4920, {c});  // JustRender
        drv::RParticleParticleCache_JustRender(pm);
        ++tl.cases;
        if (e.errors) {
            tl.fail("JustRender: " + e.error);
            e.errors = 0;
        } else if (!compare_lists(e, pm, why)) {
            tl.fail(why);
        }
    }
    drv::particles_rigid_pos() = nullptr;
    report("RParticleParticleCache: JustUpdate (UpdateBlock)", t, failures);
    report("RParticleParticleCache: JustRender (RenderBlock)", tl, failures);
}

void check_manager(Emu& e, Rng& r, const std::vector<drv::ParticleData>& gallery_types, int cases, int& failures) {
    Tally t, tl;
    const int runs = std::max(1, cases / 200);
    std::vector<uint8_t> pool(1u << 20);
    for (int run = 0; run < runs; ++run) {
        e.heap = kHeap;
        e.sizes.clear();
        const uint32_t mgr = new_managers(e);
        if (!mgr) {
            t.fail("RParticleSystemManager::RParticleSystemManager: " + e.error);
            break;
        }
        drv::RParticleSystemManager& pm = *drv::particle_manager();
        // Tipos: os da galeria e sorteados, com etiquetas 5.. como as do PARTICLE_Add.
        const int ntypes = r.range(2, 12);
        for (int i = 0; i < ntypes; ++i) {
            drv::ParticleData d = (!gallery_types.empty() && r.chance(0.7))
                                      ? gallery_types[size_t(r.range(0, int(gallery_types.size()) - 1))]
                                      : random_data(r);
            if (r.chance(0.5)) d.life = uint32_t(r.range(5, 90));  // vidas curtas: o cache se renova
            put(e.m, kScratch, &d, sizeof d);
            call_f(e, kFnLibAdd, {mgr + 8, kScratch, uint32_t(i + 5)});
            drv::RParticleLibrary_AddSystem(pm.library, d, uint32_t(i + 5));
        }
        // Os emissores, em ordem crescente dos dois lados.
        const int nem = r.range(1, 8);
        PtrMap map;
        std::vector<uint32_t> eem(static_cast<size_t>(nem));
        std::vector<drv::ParticleEmitter*> pem(static_cast<size_t>(nem));
        g_port_pool = &pool;
        g_port_pool_used = 0;
        for (int i = 0; i < nem; ++i) {
            eem[size_t(i)] = e.alloc(0x40);
            pem[size_t(i)] = pool_alloc(1);
            map.port_to_emu[nf::p32_u(pem[size_t(i)])] = eem[size_t(i)];
            drv::ParticleEmitter& em = *pem[size_t(i)];
            random_vec(r, em.pos, 3, -50.0f, 50.0f);
            em.pos[3] = 1.0f;
            random_unit(r, em.side);
            random_unit(r, em.dir);
            em.tag = uint32_t(r.range(4, 5 + ntypes));  // às vezes uma que não há (o tipo de menor etiqueta)
            em.sticky = r.chance(0.7) ? 0xff : r.range(0, 63);
            put(e.m, eem[size_t(i)], &em, 0x40);
        }
        // A velocidade de alguns sistemas (SetSourceVelocity).
        static float pvel[3];
        random_vec(r, pvel, 3, -20.0f, 20.0f);
        const uint32_t evel = kScratch + 0x800;
        put(e.m, evel, pvel, 12);
        map.port_to_emu[nf::p32_u(pvel)] = evel;
        float view[16];
        random_vec(r, view, 16, -1.0f, 1.0f);
        drv::particles_set_view_matrix(view);
        e.view_matrix(view);
        // Os corpos rígidos dos riscos grudados (as mesmas posições dos dois lados).
        static float bodies[64][4];
        for (int i = 0; i < 64; ++i) {
            random_vec(r, bodies[i], 3, -20.0f, 20.0f);
            bodies[i][3] = 1.0f;
            put(e.m, kBodies + uint32_t(i) * 0x40 + 0x10, bodies[i], 16);
        }
        drv::particles_rigid_pos() = [](int32_t slot, float out[4]) {
            std::memcpy(out, bodies[slot & 0x3f], 16);
            return true;
        };
        drv::particles_sticky_pos() = nullptr;
        e.m.write32(kGalleryPtr, 0);  // sem galeria: o GET_StickyEffectPos de uma galeria nula não é chamado
        drv::render_random().seed = r.next() & 0xffff;
        drv::rtime_data().time = uint32_t(r.range(0, 1000));
        const int frames = r.range(10, 60);
        std::string why;
        bool bad = false;
        for (int f = 0; f < frames && !bad; ++f) {
            // A cada quadro: alguns AddOrRefresh (os dois criadores), às vezes um SetSystem, uma velocidade.
            for (int i = 0; i < nem; ++i) {
                if (!r.chance(0.6)) continue;
                const bool movable = r.chance(0.3);
                const float lod = r.chance(0.2) ? 0.0f : r.real(0.0f, 300.0f);
                const uint32_t creator = kScratch + 0x900;
                e.m.write32(creator, movable ? 0x390990u : 0x3909d0u);
                const uint32_t er = call_f(e, kFnAddOrRefresh, {mgr, eem[size_t(i)], eem[size_t(i)], creator}, lod);
                const uint32_t esys = e.m.read32(er);
                bool created = false;
                drv::RParticleSystem* ps = drv::RParticleSystemManager_AddOrRefresh(
                    pm, nf::p32_u(pem[size_t(i)]), pem[size_t(i)],
                    movable ? drv::ParticleCreator::kMovable : drv::ParticleCreator::kSystem, lod, &created);
                if (created) {
                    // O móvel: a cópia é nova memória dos dois lados.
                    if (movable) map.port_to_emu[nf::p32_u(&ps->copy)] = esys + 0x70;
                }
                if (r.chance(0.2)) {
                    e.m.write32(esys + 0x44, evel);
                    ps->velocity = nf::p32_u(pvel);
                }
                if (r.chance(0.05)) {
                    const uint32_t tag = uint32_t(r.range(4, 5 + ntypes));
                    call_f(e, kFnSetSystem, {esys, tag});
                    drv::RParticleSystem_SetSystem(*ps, tag);
                }
            }
            if (r.chance(0.2)) {
                // CreateParticles com um tipo da biblioteca.
                const uint32_t ti = uint32_t(r.range(0, int(pm.library.count) - 1));
                float pos[3], dir[3];
                random_vec(r, pos, 3, -50.0f, 50.0f);
                if (r.chance(0.3)) {
                    dir[0] = r.real(-0.05f, 0.05f);
                    dir[1] = r.real(-1.0f, 1.0f);
                    dir[2] = r.real(-0.05f, 0.05f);
                } else {
                    random_vec(r, dir, 3, -2.0f, 2.0f);
                }
                const int32_t n = r.range(0, 20);
                put(e.m, kScratch + 0xa00, pos, 12);
                put(e.m, kScratch + 0xa40, dir, 12);
                const uint32_t libdata = e.m.read32(mgr + 8);
                e.sync_globals();
                call_f(e, kFnCreateParticles, {mgr, libdata + ti * 0xb0, kScratch + 0xa00, kScratch + 0xa40, uint32_t(n)});
                drv::RParticleSystemManager_CreateParticles(pm, pm.library.systems[ti], pos, dir, n);
            }
            drv::rtime_data().time += uint32_t(r.chance(0.1) ? r.range(2, 6) : 1);
            e.sync_globals();
            e.draws.clear();
            e.tri_draws.clear();
            const bool pre = compare_manager(e, mgr, pm, map, why);
            call_f(e, kFnUpdateSpawnAll, {mgr});
            drv::RParticleSystemManager_UpdateSpawnAllSystems(pm);
            const bool mid = pre && compare_manager(e, mgr, pm, map, why);
            call_f(e, kFnUpdateAndRenderAll, {mgr});
            drv::RParticleSystemManager_UpdateAndRenderAllSystems(pm);
            ++t.cases;
            if (!pre || !mid) {
                t.fail(std::string(pre ? "depois do UpdateSpawnAllSystems" : "antes dos passos (AddOrRefresh, "
                                                                            "CreateParticles)") +
                       ", quadro " + std::to_string(f) + ": " + why);
                bad = true;
            } else if (e.errors) {
                t.fail("o gerente: " + e.error);
                e.errors = 0;
                bad = true;
            } else if (!compare_manager(e, mgr, pm, map, why) || !e.same_random(why)) {
                t.fail("quadro " + std::to_string(f) + ": " + why);
                bad = true;
            }
            ++tl.cases;
            if (!bad && !compare_lists(e, pm, why)) {
                tl.fail("quadro " + std::to_string(f) + ": " + why);
                bad = true;
            }
        }
        drop_port_systems();
        g_port_pool = nullptr;
        drv::particles_rigid_pos() = nullptr;
    }
    report("RParticleSystemManager: sistemas, cache (por quadro)", t, failures);
    report("RParticleParticleCache::RenderBlock (as listas)", tl, failures);
}

// ------------------------------------------------------------------------------------------------- e) a galeria

struct GalleryCtx {
    std::vector<uint8_t> file;     // o gall1.gal
    std::vector<std::string> names;  // os efeitos (GANM 'Name')
};

bool compare_gallery(Emu& e, uint32_t eg, const drv::GfxGallery& pg, const PtrMap& map, std::string& why) {
    auto rd = [&](uint32_t o) { return e.m.read32(eg + o); };
    if (rd(0x141c) != uint32_t(pg.active_count) || rd(0x1420) != uint32_t(pg.free_count) ||
        rd(0x1418) != uint32_t(pg.cache_count) || rd(0x1428) != uint32_t(pg.drawn) ||
        rd(0x142c) != uint32_t(pg.culled) || rd(0x1424) != uint32_t(pg.cull_cursor)) {
        why = "galeria: ativas " + std::to_string(rd(0x141c)) + "/" + std::to_string(pg.active_count) + " livres " +
              std::to_string(rd(0x1420)) + "/" + std::to_string(pg.free_count) + " tipos " + std::to_string(rd(0x1418)) +
              "/" + std::to_string(pg.cache_count) + " desenhadas " + std::to_string(rd(0x1428)) + "/" +
              std::to_string(pg.drawn) + " recortadas " + std::to_string(rd(0x142c)) + "/" + std::to_string(pg.culled);
        return false;
    }
    if (!same_words("as ativas", e.m.mem(eg + 0x408, uint32_t(pg.active_count) * 4), pg.active,
                    uint32_t(pg.active_count) * 4, why) ||
        !same_words("as livres", e.m.mem(eg + 0xc08, uint32_t(pg.free_count) * 4), pg.free_list,
                    uint32_t(pg.free_count) * 4, why) ||
        !same_words("os grudados (bits)", e.m.mem(eg + 0x400, 8), pg.sticky_used, 8, why))
        return false;
    for (int i = 0; i < 64; ++i)
        if (pg.sticky_used[i >> 5] & (1u << (i & 31))) {
            if (!same_words("grudado", e.m.mem(eg + uint32_t(i) * 16, 16), &pg.sticky[i], 16, why)) return false;
        }
    const uint32_t ecache = rd(0x1410);
    for (int i = 0; i < pg.cache_count; ++i) {
        const uint32_t et = e.m.read32(ecache + uint32_t(i) * 8), eg2 = e.m.read32(ecache + uint32_t(i) * 8 + 4);
        if (et != map.emu(pg.cache[i].track) || eg2 != pg.cache[i].tag) {
            why = "o tipo " + std::to_string(i) + " da galeria: original " + hex(et) + "/" + hex(eg2);
            return false;
        }
    }
    const uint32_t ecanvas = rd(0x1414);
    for (int i = 0; i < pg.active_count; ++i) {
        const int idx = pg.active[i];
        const uint32_t ec = ecanvas + uint32_t(idx) * 0x90;
        const drv::GfxCanvas& c = pg.canvases[idx];
        uint8_t buf[0x90];
        get(e.m, ec, buf, 0x90);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
        const std::string w = "tela " + std::to_string(idx);
        if (!same_words(w.c_str(), buf, p, 0x1c, why) || !same_words(w.c_str(), buf + 0x20, p + 0x20, 0x2c, why) ||
            !same_words(w.c_str(), buf + 0x58, p + 0x58, 0x16, why) || !same_words(w.c_str(), buf + 0x70, p + 0x70, 0x20, why))
            return false;
        uint32_t v;
        std::memcpy(&v, buf + 0x1c, 4);
        if (v != map.emu(c.ref)) {
            why = w + " +0x1c (o efeito): original " + hex(v) + ", porte " + hex(map.emu(c.ref));
            return false;
        }
        std::memcpy(&v, buf + 0x50, 4);
        if (v != c.scene) {
            why = w + " +0x50 (o RSceneObj)";
            return false;
        }
        std::memcpy(&v, buf + 0x4c, 4);
        if (v != map.emu(c.extra_ptr)) {
            why = w + " +0x4c (a velocidade de quem disparou)";
            return false;
        }
        uint16_t f;
        std::memcpy(&f, buf + 0x6c, 2);
        if (f != c.flags || buf[0x6e] != c.nump || buf[0x6f] != c.sticky) {
            why = w + ": bandeiras " + hex(f) + "/" + hex(c.flags) + " emissores " + std::to_string(buf[0x6e]) + "/" +
                  std::to_string(c.nump) + " grudado " + std::to_string(buf[0x6f]) + "/" + std::to_string(c.sticky);
            return false;
        }
        std::memcpy(&v, buf + 0x54, 4);
        const drv::ParticleEmitter* em = nf::raw(c.emitters);
        if (v != map.emu(nf::p32_u(em))) {
            why = w + " +0x54 (os emissores): original " + hex(v) + ", porte " + hex(map.emu(nf::p32_u(em)));
            return false;
        }
        for (int k = 0; em && k < int8_t(c.nump); ++k) {
            uint8_t eb[0x40];
            get(e.m, v + uint32_t(k) * 0x40, eb, 0x40);
            const uint8_t* pb = reinterpret_cast<const uint8_t*>(&em[k]);
            const std::string ww = w + " emissor " + std::to_string(k);
            if (!same_words(ww.c_str(), eb, pb, 0x18, why, 0x10, 0x18) ||
                !same_words(ww.c_str(), eb + 0x20, pb + 0x20, 0x20, why))
                return false;
        }
    }
    return true;
}

void check_gallery(Emu& e, Rng& r, const GalleryCtx& g, int cases, int& failures) {
    Tally t, tl, tc;
    const int runs = std::max(1, cases / 400);
    std::vector<uint8_t> pool(4u << 20);
    for (int run = 0; run < runs; ++run) {
        e.heap = kHeap;
        e.sizes.clear();
        const uint32_t mgr = new_managers(e);
        if (!mgr) {
            t.fail("RParticleSystemManager: " + e.error);
            break;
        }
        drv::RParticleSystemManager& pm = *drv::particle_manager();
        // A galeria: o arquivo nos dois lados, o construtor original (o GSystem::FILE_Load devolve o arquivo emulado).
        put(e.m, kGalFile, g.file.data(), uint32_t(g.file.size()));
        const uint32_t eg = e.alloc(0x1430);
        const uint32_t name = kScratch + 0xc00;
        put(e.m, name, "gall1.gal", 10);
        if (!e.call(kFnGalleryCtor, {eg, name})) {
            t.fail("GFXGallery::GFXGallery: " + e.error);
            break;
        }
        e.m.write32(kGalleryPtr, eg);
        drv::GFX_Init(g.file);
        drv::GfxGallery& pg = *drv::gfx_gallery();
        g_port_pool = &pool;
        g_port_pool_used = 0;
        drv::gfx_emitter_alloc().alloc = pool_alloc;
        drv::gfx_emitter_alloc().free = pool_free;
        drv::gfx_env().instance_position = scene_matrix;
        drv::particles_sticky_pos() = [](int32_t index, float out[4]) {
            return drv::GFXGallery_GET_StickyEffectPos(*drv::gfx_gallery(), uint32_t(index), out);
        };
        PtrMap map;
        // Os grupos da galeria: o endereço emulado = o arquivo emulado + o deslocamento no do porte.
        const uint32_t pbase = nf::p32_u(pg.data.data());
        for (uint32_t o = 0; o < pg.data.size(); o += 16) map.port_to_emu[pbase + o] = kGalFile + o;
        // A velocidade de quem disparou (o bit 0x10 das bandeiras).
        static float pextra[3];
        random_vec(r, pextra, 3, -30.0f, 30.0f);
        const uint32_t eextra = kScratch + 0xd00;
        put(e.m, eextra, pextra, 12);
        map.port_to_emu[nf::p32_u(pextra)] = eextra;
        // Os RSceneObj falsos: as mesmas matrizes dos dois lados.
        g_scene_mats.clear();
        for (int s = 0; s < 4; ++s)
            for (int inst = -1; inst < 4; ++inst) {
                float q[4];
                random_quat(r, q);
                float m4[4][4];
                drv::VU0_quattom4(q, m4);
                random_vec(r, m4[3], 3, -50.0f, 50.0f);
                m4[3][3] = 1.0f;
                std::array<float, 16> a;
                std::memcpy(a.data(), m4, 64);
                g_scene_mats[{kScenes + uint32_t(s) * 0x100, inst}] = a;
            }
        // A câmera.
        float eye[4] = {r.real(-30, 30), r.real(0, 10), r.real(-30, 30), 1.0f};
        e.camera(eye);
        std::memcpy(drv::gfx_env().camera, eye, 16);
        float view[16];
        random_vec(r, view, 16, -1.0f, 1.0f);
        drv::particles_set_view_matrix(view);
        e.view_matrix(view);
        drv::render_random().seed = r.next() & 0xffff;
        drv::rtime_data().time = uint32_t(r.range(0, 1000));
        const int frames = r.range(60, 240);
        std::string why;
        bool bad = false;
        for (int f = 0; f < frames && !bad; ++f) {
            // Disparos: um efeito sorteado (às vezes um nome que não há), com ou sem RSceneObj.
            if (f == 0 || r.chance(0.08)) {
                const std::string nm = r.chance(0.05) ? std::string("NaoHa") : g.names[size_t(r.range(0, int(g.names.size()) - 1))];
                put(e.m, kScratch + 0xc40, nm.c_str(), uint32_t(nm.size() + 1));
                const uint32_t eref = call_f(e, kFnGfxLookup, {kScratch + 0xc40});
                const uint32_t pref = drv::GFX_LookupReference(nm.c_str());
                ++tc.cases;
                if (eref != map.emu(pref)) {
                    tc.fail("GFX::LookupReference(" + nm + "): original " + hex(eref) + ", porte " + hex(map.emu(pref)));
                    bad = true;
                    break;
                }
                if (eref) {
                    float pos[4] = {r.real(-30, 30), r.real(-1, 5), r.real(-30, 30), 1.0f}, quat[4], pos2[4] = {0, 0, 0, 1},
                          quat2[4] = {0, 0, 0, 1};
                    random_quat(r, quat);
                    uint32_t scene = 0;
                    int32_t inst = 0;
                    if (r.chance(0.4)) {
                        scene = kScenes + uint32_t(r.range(0, 3)) * 0x100;
                        inst = r.range(-1, 3);
                        random_vec(r, pos2, 3, -2.0f, 2.0f);
                        random_quat(r, quat2);
                    }
                    const bool with_extra = r.chance(0.3);
                    put(e.m, kScratch + 0xc80, pos, 16);
                    put(e.m, kScratch + 0xc90, quat, 16);
                    put(e.m, kScratch + 0xca0, pos2, 16);
                    put(e.m, kScratch + 0xcb0, quat2, 16);
                    // Um terço pelo GFX::TriggerIncidental (0x00233108, seção 26: a reserva pela bandeira 1), sem
                    // sortear nada a mais.
                    const bool incidental = (f + frames) % 3 == 0;
                    const uint32_t er = call_f(e, incidental ? 0x233108u : kFnGfxTrigger,
                                               {eref, kScratch + 0xc80, kScratch + 0xc90, kScratch + 0xca0,
                                                kScratch + 0xcb0, scene, uint32_t(inst), with_extra ? eextra : 0u});
                    const int32_t pr =
                        (incidental ? drv::GFX_TriggerIncidental : drv::GFX_Trigger)(
                            pref, pos, quat, pos2, quat2, scene, inst, with_extra ? nf::p32_u(pextra) : 0u);
                    if (int32_t(er) != pr) {
                        tc.fail(std::string(incidental ? "GFX::TriggerIncidental(" : "GFX::Trigger(") + nm +
                                "): original " + std::to_string(int32_t(er)) + ", porte " + std::to_string(pr));
                        bad = true;
                        break;
                    }
                    // Os emissores novos: a última tela ativa.
                    if (pg.active_count > 0) {
                        const drv::GfxCanvas& c = pg.canvases[pg.active[pg.active_count - 1]];
                        if (c.emitters) {
                            const uint32_t ec = e.m.read32(e.m.read32(eg + 0x1414) + uint32_t(pg.active[pg.active_count - 1]) * 0x90 + 0x54);
                            map.port_to_emu[nf::p32_u(nf::raw(c.emitters))] = ec;
                            for (int k = 0; k < c.nump; ++k)
                                map.port_to_emu[nf::p32_u(nf::raw(c.emitters) + k)] = ec + uint32_t(k) * 0x40;
                        }
                    }
                    for (int i = 0; i < pg.active_count; ++i) {
                        const uint32_t ec = e.m.read32(eg + 0x1414) + uint32_t(pg.active[i]) * 0x90;
                        map.port_to_emu[nf::p32_u(&pg.canvases[pg.active[i]].extra)] = ec + 0x40;
                    }
                }
            }
            // O recorte das telas (às vezes), com respostas sorteadas iguais.
            if (r.chance(0.5)) {
                call_f(e, kFnCullStart, {eg});
                drv::GFXGallery_CULL_Start(pg);
                for (;;) {
                    const uint32_t ep = kScratch + 0xe00;
                    const uint32_t ei = call_f(e, kFnCullNext, {eg, ep, ep + 0x10, ep + 0x14, ep + 0x18});
                    float sph[4], h = 0, fs = 0;
                    int32_t cf = 0;
                    const int32_t pi = drv::GFXGallery_CULL_GetNextCanvas(pg, sph, &h, &cf, &fs);
                    ++tc.cases;
                    if (int32_t(ei) != pi || (pi >= 0 && !same_words("CULL_GetNextCanvas", e.m.mem(ep, 16), sph, 16, why))) {
                        tc.fail("CULL_GetNextCanvas: original " + std::to_string(int32_t(ei)) + ", porte " +
                                std::to_string(pi) + " " + why);
                        bad = true;
                        break;
                    }
                    if (pi < 0) break;
                    const int32_t culled = r.chance(0.3) ? 1 : 0;
                    call_f(e, kFnCullSet, {eg, uint32_t(pi), uint32_t(culled)});
                    drv::GFXGallery_CULL_SetCanvas(pg, pi, culled);
                }
                if (bad) break;
            }
            // O quadro: GFX::Update e as partículas.
            drv::rtime_data().time += 1;
            e.sync_globals();
            e.draws.clear();
            e.tri_draws.clear();
            call_f(e, kFnGfxUpdate, {});
            drv::GFX_Update();
            call_f(e, kFnUpdateSpawnAll, {mgr});
            drv::RParticleSystemManager_UpdateSpawnAllSystems(pm);
            call_f(e, kFnUpdateAndRenderAll, {mgr});
            drv::RParticleSystemManager_UpdateAndRenderAllSystems(pm);
            ++t.cases;
            if (e.errors) {
                t.fail("a galeria: " + e.error);
                e.errors = 0;
                bad = true;
            } else if (!compare_gallery(e, eg, pg, map, why)) {
                t.fail("quadro " + std::to_string(f) + ": " + why);
                bad = true;
            } else if (!compare_manager(e, mgr, pm, map, why) || !e.same_random(why)) {
                t.fail("quadro " + std::to_string(f) + " (as partículas): " + why);
                bad = true;
            }
            ++tl.cases;
            if (!bad && !compare_lists(e, pm, why)) {
                tl.fail("quadro " + std::to_string(f) + ": " + why);
                bad = true;
            }
        }
        drop_port_systems();
        drv::GFX_Shutdown();
        drv::gfx_emitter_alloc() = drv::GfxEmitterAlloc();
        g_port_pool = nullptr;
        drv::particles_sticky_pos() = nullptr;
    }
    report("GFX: LookupReference, Trigger, os recortes", tc, failures);
    report("GFXGallery: as telas e as partículas (por quadro)", t, failures);
    report("GFXGallery: as listas do RenderBlock (por quadro)", tl, failures);
}


// ------------------------------------------------------- f) as luzes, as marcas, o rastro dos mísseis e os eventos

// Os endereços do PS2 destas partes.
constexpr uint32_t kLmPtr = 0x333830, kColorizePtr = 0x3349e0, kDecalPtr = 0x334bc0, kSim = 0x339190;
constexpr uint32_t kExplTable = 0x333878, kMissileTable = 0x3338f0, kCullDistance = 0x333ef8, kWeaponMgrPtr = 0x33a530;
constexpr uint32_t kDecCoords = 0x3da420, kDecStq = 0x3d8020, kDecCol = 0x3d7720, kDecDelay = 0x3dc820,
                   kDecAlpha = 0x3dc880;
constexpr uint32_t kStrCol = 0x3de100, kStrStq = 0x3de1b0, kStrArrays = 0x3de1a0, kStrCounts = 0x334de0;
constexpr uint32_t kFnAddExplosion = 0x1afa88, kFnAddMissile = 0x1afba8, kFnHlAddPos = 0x1afc58,
                   kFnLmAddPos = 0x1af520, kFnFinishLights = 0x1aede8;
constexpr uint32_t kFnAddDecal = 0x1da6c8, kFnDecalDraw = 0x1da978, kFnScorchGround = 0x1eb078,
                   kFnFindClosestFace = 0x2322c8;
constexpr uint32_t kFnStreakInit = 0x1e3e28, kFnStreakUpdate = 0x1e4640, kFnStreakUpdateDying = 0x1e43e0,
                   kFnStreakDraw = 0x1e4258, kFnMissileRender = 0x181960, kFnGetSimpleBody = 0x1ff0b0;
constexpr uint32_t kFnEaglMalloc = 0x2871f0, kFn29fd38 = 0x29fd38;
constexpr uint32_t kFnDrawListBegin = 0x286018, kFnDrawListAdd = 0x2851f8, kFnDrawListEnd = 0x285388,
                   kFnDrawListFlush = 0x286080;
constexpr uint32_t kFnPlayEffect = 0x153f80, kFnGiotto = 0x1543e8, kFnNuclear = 0x151408, kFnCullFactor = 0x157ec0;
constexpr uint32_t kFnGfxTriggerName = 0x232f70, kFnStartNuclear = 0x20ea20, kFnGetVelocity = 0x1c9c78,
                   kFnTriggerFx = 0x1caba8;
// Os objetos falsos (a área kFake + 0x3000..).
constexpr uint32_t kLm = kFake + 0x3000, kLmBuild = kFake + 0x3400, kLmPub = kFake + 0x3480, kColorize = kFake + 0x3500;
constexpr uint32_t kDecalMgr = kFake + 0x3600, kMissile = kFake + 0x4000, kBody = kFake + 0x4800, kWp = kFake + 0x4900;
constexpr uint32_t kStrArr = kFake + 0x4a00, kEffVtable = kFake + 0x5000, kEvent = kFake + 0x5100,
                   kEffRec = kFake + 0x5200, kEffMat = kFake + 0x5300, kObjRef = kFake + 0x5400, kVelocities = kFake + 0x5500,
                   kInstances = kFake + 0x5600, kArticles = kFake + 0x5700, kNameBuf = kFake + 0x5800;

// O que os ganchos destas partes guardam.
struct FxHookLog {
    std::vector<uint32_t> drawn_arrays;  // os DrawArray das listas desenhadas (0x2851f8)
    struct Trigger {
        uint32_t ref = 0, scene = 0, instance = 0, extra = 0;
        float pos[3] = {}, quat[4] = {}, pos2[3] = {}, quat2[4] = {};
        std::string name;
    };
    std::vector<Trigger> triggers;
    uint32_t trigger_fx = 0;
    struct Nuclear {
        float pos[3];
        float scale;
    };
    std::vector<Nuclear> nuclear;
    void clear() {
        drawn_arrays.clear();
        triggers.clear();
        trigger_fx = 0;
        nuclear.clear();
    }
};
FxHookLog g_hook;

// O mundo sintético da conferência do ScorchGround (o mesmo nos dois lados): células de 2 m com uma face plana
// inclinada por célula (um pouco), o tipo e o subtipo da face e uma instância (nenhuma, artigo limpo ou marcado), e
// buracos (sem face).
struct SynthWorld {
    uint32_t seed = 1;
    uint32_t hash(int32_t cx, int32_t cz) const {
        uint32_t h = seed * 0x9e3779b1u ^ uint32_t(cx) * 0x85ebca6bu ^ uint32_t(cz) * 0xc2b2ae35u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return h;
    }
    // A face sob (x, z): o WWorldPos de 0x40 bytes (os vértices a meio metro do ponto, alturas pelo plano da célula).
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
};
SynthWorld g_synth;
// As instâncias do porte para o mundo sintético (o artigo com o byte +0xc limpo e marcado).
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

void fx_hooks(Emu& e) {
    MipsMachine& m = e.m;
    m.hook(kFnEaglMalloc, "EAGLInternal::EAGLMalloc", [&e](MipsMachine& mm) {
        mm.set_gpr(R::v0, e.alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u)));
    });
    m.hook(kFn29fd38, "(0x29fd38, a busca de um símbolo do EAGL)", [](MipsMachine& mm) {
        mm.write32(uint32_t(mm.gpr(R::a1)), 0);
        mm.set_gpr(R::v0, 0);
    });
    m.hook(kFnDrawListBegin, "(0x286018, o começo da lista)", [](MipsMachine&) {});
    m.hook(kFnDrawListAdd, "(0x2851f8, um DrawArray na lista)", [](MipsMachine& mm) {
        g_hook.drawn_arrays.push_back(uint32_t(mm.gpr(R::a1)));
    });
    m.hook(kFnDrawListEnd, "(0x285388, o fim da lista)", [](MipsMachine&) {});
    m.hook(kFnDrawListFlush, "(0x286080, o envio)", [](MipsMachine&) {});
    m.hook(kFnGetSimpleBody, "Simulation::GetSimpleRigidBody", [](MipsMachine& mm) { mm.set_gpr(R::v0, kBody); });
    m.hook(kFnFindClosestFace, "WWorldPos::FindClosestFace (o mundo sintético)", [](MipsMachine& mm) {
        const uint32_t a = uint32_t(mm.gpr(R::a0));
        drv::WWorldPos wp;
        get(mm, a, &wp, 0x40);
        float p[3];
        get(mm, uint32_t(mm.gpr(R::a1)), p, 12);
        g_synth.face(p, wp, kInstances, kInstances + 0x40);
        put(mm, a, &wp, 0x40);
        mm.set_gpr(R::v0, 1);
    });
    m.hook(kFnGfxTrigger, "GFX::Trigger (anotado)", [](MipsMachine& mm) {
        FxHookLog::Trigger t;
        t.ref = uint32_t(mm.gpr(R::a0));
        get(mm, uint32_t(mm.gpr(R::a1)), t.pos, 12);
        get(mm, uint32_t(mm.gpr(R::a2)), t.quat, 16);
        get(mm, uint32_t(mm.gpr(R::a3)), t.pos2, 12);
        get(mm, uint32_t(mm.gpr(R::t0)), t.quat2, 16);
        t.scene = uint32_t(mm.gpr(R::t1));
        t.instance = uint32_t(mm.gpr(R::t2));
        t.extra = uint32_t(mm.gpr(R::t3));
        g_hook.triggers.push_back(t);
        mm.set_gpr(R::v0, 0);
    });
    m.hook(kFnGfxTriggerName, "GFX::Trigger(char *) (anotado)", [](MipsMachine& mm) {
        FxHookLog::Trigger t;
        t.name = cstr(mm, uint32_t(mm.gpr(R::a0)));
        get(mm, uint32_t(mm.gpr(R::a1)), t.pos, 12);
        get(mm, uint32_t(mm.gpr(R::a2)), t.quat, 16);
        get(mm, uint32_t(mm.gpr(R::a3)), t.pos2, 12);
        get(mm, uint32_t(mm.gpr(R::t0)), t.quat2, 16);
        t.scene = uint32_t(mm.gpr(R::t1));
        t.instance = uint32_t(mm.gpr(R::t2));
        t.extra = uint32_t(mm.gpr(R::t3));
        g_hook.triggers.push_back(t);
        mm.set_gpr(R::v0, 0);
    });
    m.hook(kFnStartNuclear, "SWeaponManager::StartNuclearSequence (anotado)", [](MipsMachine& mm) {
        FxHookLog::Nuclear n;
        get(mm, uint32_t(mm.gpr(R::a1)), n.pos, 12);
        n.scale = mm.fpr(12);
        g_hook.nuclear.push_back(n);
    });
    m.hook(kFnGetVelocity, "RSceneObj::GetVelocity (a tabela dos falsos)", [](MipsMachine& mm) {
        const uint32_t s = uint32_t(mm.gpr(R::a0));
        const uint32_t k = (s - kScenes) / 0x40;
        mm.set_gpr(R::v0, (k & 1) ? kVelocities + k * 16 : 0);
    });
    m.hook(kFnTriggerFx, "RSceneObj::TriggerFX (anotado)", [](MipsMachine&) { ++g_hook.trigger_fx; });
    // O RLightManager, o RColorize e o RDecalManager falsos; os DrawArray do rastro.
    m.write32(kLmPtr, kLm);
    m.write32(kLm + 0x2a0, kLmBuild);
    m.write32(kLm + 0x2a4, kLmPub);
    m.write32(kColorizePtr, kColorize);
    m.write32(kDecalPtr, kDecalMgr);
    for (uint32_t k = 0; k < 3; ++k) m.write32(kStrArrays + k * 4, kStrArr + k * 0x40);
    m.write32(kWeaponMgrPtr, kFake + 0x5f00);
    // As instâncias e os artigos falsos do mundo sintético (o artigo +0xc: 0 e 1).
    m.write32(kInstances + 0x1c, kArticles);
    m.write32(kInstances + 0x40 + 0x1c, kArticles + 0x20);
    m.write32(kArticles + 0xc, 0);
    m.write32(kArticles + 0x20 + 0xc, 1);
    std::memset(g_synth_inst, 0, sizeof g_synth_inst);
    std::memset(g_synth_article, 0, sizeof g_synth_article);
    g_synth_inst[0].article = nf::p32_u(g_synth_article[0]);
    g_synth_inst[1].article = nf::p32_u(g_synth_article[1]);
    g_synth_article[1][0xc] = 1;
}

// O RLightManager do PS2 (o que o porte tem) contra o do porte.
bool compare_lights(Emu& e, const drv::RLightManagerPositional& lm, std::string& why) {
    if (!same_words("RHighLevelLightManager", e.m.mem(kLm + 0x2e0, 0x118), &lm.hl, 0x118, why)) return false;
    if (!same_words("o bloco em construção", e.m.mem(kLmBuild, 0x80), &lm.building, 0x80, why)) return false;
    if (!same_words("o bloco publicado", e.m.mem(kLmPub, 0x80), &lm.published, 0x80, why)) return false;
    const uint32_t c[3] = {e.m.read32(kLm + 0x60), e.m.read32(kLm + 0x64), e.m.read32(kLm + 0x68)};
    const int32_t p[3] = {lm.unknown60, lm.count, lm.last_count};
    if (std::memcmp(c, p, 12) != 0) {
        why = "contagens +0x60/+0x64/+0x68: original " + std::to_string(c[0]) + "/" + std::to_string(c[1]) + "/" +
              std::to_string(c[2]) + ", porte " + std::to_string(p[0]) + "/" + std::to_string(p[1]) + "/" +
              std::to_string(p[2]);
        return false;
    }
    return true;
}

float random_radius(Rng& r) {
    static const float kEdges[] = {100.0f, 200.0f, 400.0f, 800.0f};
    if (r.chance(0.3)) return kEdges[r.range(0, 3)];
    return r.real(0.0f, 1200.0f);
}

// As luzes: as tabelas do .data, e seqüências sorteadas de AddExplosion, AddMissile, os dois AddPositionalLight e o
// FinishLights (com o Process), com o RColorize nos modos 0 e 1.
void check_lights(Emu& e, Rng& r, int cases, int& failures) {
    Tally tt, t;
    ++tt.cases;
    std::string why;
    if (!same_words("as explosões (0x333878)", e.m.mem(kExplTable, 0x78), drv::explosion_light_types(), 0x78, why) ||
        !same_words("os mísseis (0x3338f0)", e.m.mem(kMissileTable, 0x48), drv::missile_light_types(), 0x48, why))
        tt.fail(why);
    report("luzes: as tabelas do .data", tt, failures);
    static drv::RLightManagerPositional lm;
    for (int c = 0; c < std::max(1, cases / 8); ++c) {
        lm = drv::RLightManagerPositional();
        lm.colorize_mode = r.chance(0.2) ? 1 : 0;
        std::memset(e.m.mem(kLm, 0x400), 0, 0x400);
        e.m.write32(kLm + 0x2a0, kLmBuild);
        e.m.write32(kLm + 0x2a4, kLmPub);
        std::memset(e.m.mem(kLmBuild, 0x100), 0, 0x100);
        e.m.write32(kColorize + 0x30, uint32_t(lm.colorize_mode));
        uint32_t time = uint32_t(r.range(1, 100000));
        bool bad = false;
        for (int s = 0; s < 30 && !bad; ++s) {
            float pos[4], col[4] = {r.real(0, 1), r.real(0, 1), r.real(0, 1), r.real(0, 1)};
            random_vec(r, pos, 4, -100.0f, 100.0f);
            put(e.m, kScratch, pos, 16);
            put(e.m, kScratch + 0x10, col, 16);
            const int op = r.range(0, 9);
            std::string name;
            if (op <= 2) {
                const float radius = random_radius(r);
                e.m.write32(kTime, time);
                call_f(e, kFnAddExplosion, {kLm + 0x2e0, kScratch}, radius);
                drv::RHighLevelLightManager_AddExplosion(lm.hl, pos, radius, time);
                name = "AddExplosion";
            } else if (op <= 4) {
                const int32_t type = r.range(0, 2);
                const float value = r.chance(0.2) ? 0.0f : r.real(-1.0f, 40.0f);
                call_f(e, kFnAddMissile, {kLm + 0x2e0, kScratch, uint32_t(type)}, value);
                drv::RHighLevelLightManager_AddMissile(lm.hl, pos, type, value);
                name = "AddMissile";
            } else if (op == 5) {
                const int32_t prio = r.range(-5, 20);
                call_f(e, kFnHlAddPos, {kLm + 0x2e0, kScratch, kScratch + 0x10, uint32_t(prio)});
                drv::RHighLevelLightManager_AddPositionalLight(lm.hl, pos, col, prio);
                name = "RHighLevelLightManager::AddPositionalLight";
            } else if (op == 6) {
                call_f(e, kFnLmAddPos, {kLm, kScratch, kScratch + 0x10});
                drv::RLightManager_AddPositionalLight(lm, pos, col);
                name = "RLightManager::AddPositionalLight";
            } else {
                time += uint32_t(r.range(0, 4));
                e.m.write32(kTime, time);
                call_f(e, kFnFinishLights, {kLm});
                drv::RLightManager_FinishLights(lm, time);
                name = "FinishLights";
            }
            ++t.cases;
            if (e.errors) {
                t.fail(name + ": " + e.error);
                e.errors = 0;
                bad = true;
            } else if (!compare_lights(e, lm, why)) {
                t.fail("passo " + std::to_string(s) + " " + name + ": " + why);
                bad = true;
            }
        }
    }
    report("luzes: RHighLevelLightManager e FinishLights", t, failures);
}

// As marcas: AddDecal com posições, normais, direções, tipos e lados sorteados e o Draw (os atrasos), sobre o decal.dat
// da missão; o ScorchGround sobre o mundo sintético.
bool compare_decals(Emu& e, const drv::DecalState& s, std::string& why) {
    if (e.m.read32(kDecalMgr + 0x294) != uint32_t(s.m.next)) {
        why = "o índice +0x294: original " + std::to_string(e.m.read32(kDecalMgr + 0x294)) + ", porte " +
              std::to_string(s.m.next);
        return false;
    }
    return same_words("as posições (0x3da420)", e.m.mem(kDecCoords, sizeof s.b.coords), s.b.coords, sizeof s.b.coords,
                      why) &&
           same_words("o STQ (0x3d8020)", e.m.mem(kDecStq, sizeof s.b.stq), s.b.stq, sizeof s.b.stq, why) &&
           same_words("as cores (0x3d7720)", e.m.mem(kDecCol, sizeof s.b.colours), s.b.colours, sizeof s.b.colours,
                      why) &&
           same_words("os atrasos (0x3dc820)", e.m.mem(kDecDelay, 96), s.b.delay, 96, why) &&
           same_words("os alfas (0x3dc880)", e.m.mem(kDecAlpha, 96), s.b.alpha, 96, why);
}

void reset_decals(Emu& e, drv::DecalState& s, const std::vector<uint8_t>& dat) {
    drv::RDecalManager_Construct(s, dat);
    std::memset(e.m.mem(kDecalMgr, 0x298), 0, 0x298);
    e.m.write32(kDecalMgr, 0x0038b840);
    put(e.m, kDecalMgr + 0x10, s.m.types, sizeof s.m.types);
    put(e.m, kDecCoords, s.b.coords, sizeof s.b.coords);
    put(e.m, kDecStq, s.b.stq, sizeof s.b.stq);
    put(e.m, kDecCol, s.b.colours, sizeof s.b.colours);
    put(e.m, kDecDelay, s.b.delay, 96);
    put(e.m, kDecAlpha, s.b.alpha, 96);
}

void check_decals(Emu& e, Rng& r, const std::vector<uint8_t>& dat, int cases, int& failures) {
    Tally t, ts;
    static drv::DecalState s;
    std::string why;
    for (int c = 0; c < std::max(1, cases / 20); ++c) {
        reset_decals(e, s, dat);
        bool bad = false;
        for (int k = 0; k < 40 && !bad; ++k) {
            std::string name;
            if (r.chance(0.6)) {
                float pos[4], n[4], d[4] = {0, 0, 1, 0};
                random_vec(r, pos, 3, -200.0f, 200.0f);
                pos[3] = r.chance(0.5) ? 1.0f : r.real(-2, 2);
                random_unit(r, n);
                n[3] = r.chance(0.5) ? 0.0f : r.real(-1, 1);
                if (r.chance(0.3)) {
                    random_unit(r, d);
                    d[3] = r.real(-1, 1);
                }
                const int32_t type = r.range(0, 7);
                const float w = r.real(0.0f, 12.0f), h = r.chance(0.5) ? w : r.real(0.0f, 12.0f);
                put(e.m, kScratch, pos, 16);
                put(e.m, kScratch + 0x10, n, 16);
                put(e.m, kScratch + 0x20, d, 16);
                call_f(e, kFnAddDecal, {kDecalMgr, kScratch, kScratch + 0x10, kScratch + 0x20, uint32_t(type)}, w, h);
                drv::RDecalManager_AddDecal(s, pos, n, d, type, w, h);
                name = "AddDecal";
            } else {
                call_f(e, kFnDecalDraw, {kDecalMgr});
                drv::RDecalManager_Draw(s);
                name = "Draw";
            }
            ++t.cases;
            if (e.errors) {
                t.fail(name + ": " + e.error);
                e.errors = 0;
                bad = true;
            } else if (!compare_decals(e, s, why)) {
                t.fail(name + " (passo " + std::to_string(k) + "): " + why);
                bad = true;
            }
        }
    }
    report("marcas: RDecalManager::AddDecal e Draw", t, failures);
    // O ScorchGround: pontos sorteados sobre o mundo sintético, os tipos 4 e 5 e outros, tamanhos até 16.
    SynthCollision world;
    uint64_t added = 0;
    reset_decals(e, s, dat);
    for (int c = 0; c < cases; ++c) {
        if ((c & 63) == 0) g_synth.seed = r.next() | 1;
        float pos[4] = {r.real(-60, 60), r.real(-0.5f, 4.0f), r.real(-60, 60), r.chance(0.8) ? 1.0f : r.real(-1, 1)};
        const int32_t type = r.chance(0.4) ? 5 : (r.chance(0.3) ? 4 : r.range(0, 7));
        const float size = r.chance(0.5) ? 8.0f : r.real(0.0f, 16.0f);
        drv::WWorldPos wp{};
        std::memset(e.m.mem(kWp, 0x40), 0, 0x40);
        put(e.m, kScratch, pos, 16);
        const uint64_t before = drv::decal_stats().added;
        call_f(e, kFnScorchGround, {kWp, kScratch, uint32_t(type)}, size);
        drv::RScorchWorld_ScorchGround(s, &world, wp, pos, type, size);
        added += drv::decal_stats().added - before;
        ++ts.cases;
        if (e.errors) {
            ts.fail("ScorchGround: " + e.error);
            e.errors = 0;
        } else if (!compare_decals(e, s, why)) {
            ts.fail("caso " + std::to_string(c) + ": " + why);
            reset_decals(e, s, dat);
        }
    }
    report("marcas: RScorchWorld::ScorchGround", ts, failures);
    std::printf("    (marcas postas pelo ScorchGround: %" PRIu64 " de %d)\n", added, cases);
}

// O rastro: o Init (as tabelas), o Update(COORD4 &), o Update(void) e o Draw com rastros sorteados.
void check_streak(Emu& e, Rng& r, int cases, int& failures) {
    Tally ti, t;
    std::string why;
    ++ti.cases;
    static drv::MissileStreakTables tables;
    drv::RMissileStreak_Init(tables);
    call_f(e, kFnStreakInit, {});
    if (e.errors) {
        ti.fail("Init: " + e.error);
        e.errors = 0;
    } else if (!same_words("as cores (0x3de100)", e.m.mem(kStrCol, 160), tables.colours, 160, why) ||
               !same_words("o STQ (0x3de1b0)", e.m.mem(kStrStq, 42 * 16), tables.stq, 42 * 16, why)) {
        ti.fail(why);
    }
    report("rastro: RMissileStreak::Init (as tabelas)", ti, failures);
    drv::missile_streak_tables() = tables;
    // Os DrawArray falsos (o Init do original escreveu os dele; o Draw só usa +0x18 e +0x20).
    for (uint32_t k = 0; k < 3; ++k) e.m.write32(kStrArrays + k * 4, kStrArr + k * 0x40);
    static drv::RMissileStreakData sd;
    const uint32_t es = kScratch + 0x100;
    for (int c = 0; c < cases; ++c) {
        // Um rastro sorteado: os pontos, a contagem (par), o tique e o tipo.
        for (auto& v : sd.verts) random_vec(r, v, 4, -50.0f, 50.0f);
        sd.count = r.chance(0.1) ? r.range(0, 4) * 2 : r.range(0, 0x30) * 2;
        sd.tick = r.chance(0.1) ? r.range(0, 4) : r.range(0, 260);
        if (sd.count == 0) sd.tick &= ~1;  // (sem pontos num tique ímpar: o jogo não chega lá; o porte guarda)
        sd.kind = r.range(0, 2);
        sd.unknown0c = int32_t(r.next());
        put(e.m, es, &sd, sizeof sd);
        float right[4], pos[4];
        random_unit(r, right);
        right[3] = 0.0f;
        random_vec(r, pos, 3, -100.0f, 100.0f);
        pos[3] = 1.0f;
        put(e.m, kRCamera, right, 16);
        put(e.m, kScratch, pos, 16);
        drv::render_random().seed = r.next() & 0xffff;
        e.sync_globals();
        std::string name;
        const int op = r.range(0, 3);
        if (op <= 1) {
            call_f(e, kFnStreakUpdate, {es, kScratch});
            drv::RMissileStreak_Update(sd, pos, right);
            name = "Update(COORD4 &)";
        } else if (op == 2) {
            call_f(e, kFnStreakUpdateDying, {es});
            drv::RMissileStreak_UpdateDying(sd);
            name = "Update(void)";
        } else {
            g_hook.clear();
            for (uint32_t k = 0; k < 3; ++k) e.m.write32(kStrCounts + k * 16, 0xffffffffu);
            call_f(e, kFnStreakDraw, {es});
            drv::StreakDrawRange dr;
            const bool drew = drv::RMissileStreak_Draw(sd, dr);
            name = "Draw";
            ++t.cases;
            const bool emu_drew = !g_hook.drawn_arrays.empty();
            if (e.errors) {
                t.fail("Draw: " + e.error);
                e.errors = 0;
            } else if (emu_drew != drew) {
                t.fail("Draw: desenhou original " + std::to_string(emu_drew) + ", porte " + std::to_string(drew));
            } else if (drew) {
                const uint32_t arr = kStrArr + uint32_t(sd.kind) * 0x40;
                const uint32_t coords = e.m.read32(arr + 0x18), stq = e.m.read32(arr + 0x20);
                const uint32_t n = e.m.read32(kStrCounts + uint32_t(sd.kind) * 16);
                if (g_hook.drawn_arrays[0] != arr || coords != es + 0x10 + uint32_t(dr.first) * 16 ||
                    stq != kStrStq + uint32_t(dr.stq_first) * 16 || n != uint32_t(dr.count))
                    t.fail("Draw: original pontos " + std::to_string((coords - es - 0x10) / 16) + "+" +
                           std::to_string(n) + " stq " + std::to_string((stq - kStrStq) / 16) + ", porte " +
                           std::to_string(dr.first) + "+" + std::to_string(dr.count) + " stq " +
                           std::to_string(dr.stq_first));
            }
            continue;
        }
        ++t.cases;
        if (e.errors) {
            t.fail(name + ": " + e.error);
            e.errors = 0;
        } else if (!same_words("o RMissileStreak", e.m.mem(es, sizeof sd), &sd, sizeof sd, why) ||
                   !e.same_random(why)) {
            t.fail(name + " (caso " + std::to_string(c) + "): " + why);
        }
    }
    report("rastro: RMissileStreak Update e Draw", t, failures);
}

// O Missile::Render: mísseis sorteados (as armas, morrendo ou não, as idades, a simulação parada), com e sem
// atualizar: a luz (AddMissile ou a do EMP), o rastro e o que o Draw desenha.
void check_missile_render(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    std::string why;
    static drv::RLightManagerPositional lm;
    static drv::Simulation sim;
    static drv::MissileData d;
    static drv::PhysicsObjectWords po;
    static const int32_t kWeapons[] = {1, 2, 0xe, 0x15, 0x16, 0x19, 0x1c, 0x1e, 0x1f, 5};
    for (int c = 0; c < cases; ++c) {
        if ((c % 25) == 0) {
            lm = drv::RLightManagerPositional();
            std::memset(e.m.mem(kLm, 0x400), 0, 0x400);
            e.m.write32(kLm + 0x2a0, kLmBuild);
            e.m.write32(kLm + 0x2a4, kLmPub);
            std::memset(e.m.mem(kLmBuild, 0x100), 0, 0x100);
            e.m.write32(kColorize + 0x30, 0);
        }
        d = drv::MissileData();
        po = drv::PhysicsObjectWords();
        d.weapon = kWeapons[r.range(0, 9)];
        d.decaying = r.chance(0.2) ? 1 : 0;
        d.straight_time = r.range(0, 60);
        d.decay_time = r.range(30, 120);
        sim.steps = r.range(0, 100000);
        d.start_step = sim.steps - r.range(-3, 300);
        sim.frame_rate = r.chance(0.8) ? 60 : r.range(1, 120);
        sim.dt = r.chance(0.8) ? ps2::ee::div(1.0f, 60.0f) : r.real(0.001f, 0.1f);
        sim.state = r.chance(0.1) ? 3 : 1;
        for (auto& v : d.streak.verts) random_vec(r, v, 4, -50.0f, 50.0f);
        d.streak.count = r.range(0, 0x30) * 2;
        d.streak.tick = r.range(0, 200);
        if (d.streak.count == 0) d.streak.tick &= ~1;
        d.streak.kind = r.range(0, 2);
        po.slot = int16_t(r.range(0, 40));
        const bool update = r.chance(0.8);
        float body[4], right[4];
        random_vec(r, body, 3, -200.0f, 200.0f);
        body[3] = 1.0f;
        random_unit(r, right);
        right[3] = 0.0f;
        // O Missile do PS2 (0x730 bytes): as palavras do PhysicsObject em +0x40 e o MissileData em +0x6c.
        std::memset(e.m.mem(kMissile, 0x730), 0, 0x730);
        put(e.m, kMissile + 0x40, &po, sizeof po);
        put(e.m, kMissile + 0x6c, &d, sizeof d);
        put(e.m, kBody + 0x10, body, 16);
        put(e.m, kRCamera, right, 16);
        e.m.write32(kSim + 0xdb4, uint32_t(sim.state));
        e.m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
        e.putf(kSim + 0xdc0, sim.dt);
        e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
        for (uint32_t k = 0; k < 3; ++k) e.m.write32(kStrCounts + k * 16, 0xffffffffu);
        drv::render_random().seed = r.next() & 0xffff;
        e.sync_globals();
        g_hook.clear();
        call_f(e, kFnMissileRender, {kMissile, update ? 1u : 0u, kScratch + 0x200});
        drv::StreakStrip strip;
        drv::Missile_RenderEffects(d, po, body, update, sim, right, lm, &strip);
        ++t.cases;
        if (e.errors) {
            t.fail("Render: " + e.error);
            e.errors = 0;
            continue;
        }
        const bool emu_drew = !g_hook.drawn_arrays.empty();
        if (!same_words("o MissileData", e.m.mem(kMissile + 0x6c, sizeof d), &d, sizeof d, why) ||
            !compare_lights(e, lm, why) || !e.same_random(why)) {
            t.fail("arma " + std::to_string(d.weapon) + " (caso " + std::to_string(c) + "): " + why);
        } else if (emu_drew != !strip.pos.empty()) {
            t.fail("o rastro desenhado: original " + std::to_string(emu_drew) + ", porte " +
                   std::to_string(!strip.pos.empty()));
        } else if (emu_drew && e.m.read32(kStrCounts + uint32_t(d.streak.kind) * 16) != uint32_t(strip.colour.size())) {
            t.fail("o rastro: os pontos diferem");
        }
    }
    report("Missile::Render (a luz e o rastro)", t, failures);
}

// Os eventos: EPlayEffect com registros, matrizes, objetos e instâncias sorteados; EPlayGiottoEffect; ENuclearBlast;
// ESetCullDistanceFactor.
alignas(16) uint8_t g_port_rec[0x40];
alignas(16) float g_port_mat[4][4];
alignas(16) uint32_t g_port_objref[4];
alignas(16) uint8_t g_port_scenes[8][0x40];
alignas(16) float g_port_vel[8][4];
alignas(16) char g_port_name[32];
std::vector<FxHookLog::Trigger> g_port_triggers;
std::vector<FxHookLog::Nuclear> g_port_nuclear;

uint32_t emu_scene(uint32_t port) {
    for (uint32_t k = 0; k < 8; ++k)
        if (port == nf::p32_u(g_port_scenes[k])) return kScenes + k * 0x40;
    return port;
}
uint32_t emu_velocity(uint32_t port) {
    for (uint32_t k = 0; k < 8; ++k)
        if (port == nf::p32_u(g_port_vel[k])) return kVelocities + k * 16;
    return port;
}

bool same_trigger(const FxHookLog::Trigger& a, const FxHookLog::Trigger& b, std::string& why) {
    const uint32_t sb = emu_scene(b.scene), xb = emu_velocity(b.extra);
    if (a.ref != b.ref || a.scene != sb || a.instance != b.instance || a.extra != xb || a.name != b.name) {
        why = "o GFX::Trigger: original ref " + hex(a.ref) + " cena " + hex(a.scene) + " inst " +
              std::to_string(a.instance) + " extra " + hex(a.extra) + " " + a.name + ", porte ref " + hex(b.ref) +
              " cena " + hex(sb) + " inst " + std::to_string(b.instance) + " extra " + hex(xb) + " " + b.name;
        return false;
    }
    return same_words("a posição", a.pos, b.pos, 12, why) && same_words("o quatérnio", a.quat, b.quat, 16, why) &&
           same_words("a posição 2", a.pos2, b.pos2, 12, why) && same_words("o quatérnio 2", a.quat2, b.quat2, 16, why);
}

void check_events(Emu& e, Rng& r, int cases, int& failures) {
    Tally tp, tg, tn, tc;
    std::string why;
    drv::EffectsEnv& fe = drv::effects_env();
    const drv::EffectsEnv saved = fe;
    const drv::FxEventsEnv saved_fx = drv::fx_events_env();
    fe.instance_position = [](uint32_t scene, int32_t instance, float m[4][4]) {
        return scene_matrix(emu_scene(scene), instance, m);
    };
    fe.gfx_trigger = [](uint32_t ref, const float pos[3], const float quat[4], const float pos2[3], const float quat2[4],
                        uint32_t scene, int32_t instance, uint32_t extra) {
        FxHookLog::Trigger t;
        t.ref = ref;
        std::memcpy(t.pos, pos, 12);
        std::memcpy(t.quat, quat, 16);
        std::memcpy(t.pos2, pos2, 12);
        std::memcpy(t.quat2, quat2, 16);
        t.scene = scene;
        t.instance = uint32_t(instance);
        t.extra = extra;
        g_port_triggers.push_back(t);
        return int32_t(0);
    };
    fe.gfx_lookup = [](const char*) { return 0x12345670u; };
    drv::fx_events_env().scene_velocity = [](uint32_t scene) {
        for (uint32_t k = 0; k < 8; ++k)
            if (scene == nf::p32_u(g_port_scenes[k])) return (k & 1) ? nf::p32_u(g_port_vel[k]) : 0u;
        return 0u;
    };
    drv::fx_events_env().start_nuclear = [](const float pos[3], float scale) {
        FxHookLog::Nuclear n;
        std::memcpy(n.pos, pos, 12);
        n.scale = scale;
        g_port_nuclear.push_back(n);
    };
    // A tabela virtual dos RSceneObj falsos: +0x70/+0x80 sem ajuste, +0x74 GetVelocity, +0x84 TriggerFX.
    e.m.write32(kEffVtable + 0x70, 0);
    e.m.write32(kEffVtable + 0x74, kFnGetVelocity);
    e.m.write32(kEffVtable + 0x80, 0);
    e.m.write32(kEffVtable + 0x84, kFnTriggerFx);
    for (uint32_t k = 0; k < 8; ++k) {
        e.m.write32(kScenes + k * 0x40 + 0x3c, kEffVtable);
        random_vec(r, g_port_vel[k], 4, -20.0f, 20.0f);
        put(e.m, kVelocities + k * 16, g_port_vel[k], 16);
    }
    for (int c = 0; c < cases; ++c) {
        // O registro do efeito: a posição, as bandeiras, o tipo (6 quase sempre), a instância, o GFX, o quatérnio.
        uint8_t rec[0x40];
        for (uint8_t& b : rec) b = uint8_t(r.next());
        float p[4], q[4];
        random_vec(r, p, 4, -100.0f, 100.0f);
        random_quat(r, q);
        std::memcpy(rec, p, 16);
        std::memcpy(rec + 0x20, q, 16);
        uint32_t flags = r.next() & (r.chance(0.5) ? 0x200u : 0xffffu);
        if (r.chance(0.6)) flags &= ~0x82u;
        std::memcpy(rec + 0x10, &flags, 4);
        rec[0x14] = r.chance(0.9) ? 6 : uint8_t(r.range(0, 9));
        rec[0x15] = uint8_t(r.range(0, 4));
        const uint32_t gfx = r.chance(0.9) ? (r.next() | 1) : 0;
        std::memcpy(rec + 0x18, &gfx, 4);
        // A matriz (rotação sorteada e translação), o objeto e a instância.
        float qm[4];
        random_quat(r, qm);
        drv::VU0_quattom4(qm, g_port_mat);
        random_vec(r, g_port_mat[3], 3, -500.0f, 500.0f);
        for (int k = 0; k < 4; ++k) g_port_mat[k][3] = r.real(-1, 1);
        const bool has_mat = r.chance(0.6), has_ref = r.chance(0.5), has_scene = r.chance(0.4);
        const uint32_t sk = uint32_t(r.range(0, 7));
        const uint32_t inst = uint32_t(r.range(0, 4));
        std::array<float, 16> im;
        for (float& f : im) f = r.real(-3, 3);
        g_scene_mats[{kScenes + sk * 0x40, int32_t(inst)}] = im;
        std::memcpy(g_port_rec, rec, 0x40);
        put(e.m, kEffRec, rec, 0x40);
        put(e.m, kEffMat, g_port_mat, 64);
        g_port_objref[2] = nf::p32_u(g_port_scenes[(sk + 1) & 7]);
        e.m.write32(kObjRef + 8, kScenes + ((sk + 1) & 7) * 0x40);
        drv::PendingEvent ev{9, 0, 0.0f, 0, 0, 0};
        ev.obj[0] = 0x00370b70;
        ev.obj[1] = nf::p32_u(g_port_rec);
        ev.obj[2] = has_mat ? nf::p32_u(g_port_mat) : 0;
        ev.obj[3] = has_ref ? nf::p32_u(g_port_objref) : 0;
        ev.obj[4] = has_scene ? nf::p32_u(g_port_scenes[sk]) : 0;
        ev.obj[5] = inst;
        const uint32_t eobj[6] = {0x00370b70, kEffRec, has_mat ? kEffMat : 0, has_ref ? kObjRef : 0,
                                  has_scene ? kScenes + sk * 0x40 : 0, inst};
        put(e.m, kEvent, eobj, 24);
        g_hook.clear();
        g_port_triggers.clear();
        const uint64_t fx0 = drv::fx_event_stats().trigger_fx;
        call_f(e, kFnPlayEffect, {kEvent, 0});
        drv::fx_event_execute(ev);
        ++tp.cases;
        if (e.errors) {
            tp.fail("EPlayEffect: " + e.error);
            e.errors = 0;
        } else if (g_hook.triggers.size() != g_port_triggers.size() ||
                   uint64_t(g_hook.trigger_fx) != drv::fx_event_stats().trigger_fx - fx0) {
            tp.fail("EPlayEffect: disparos original " + std::to_string(g_hook.triggers.size()) + "+" +
                    std::to_string(g_hook.trigger_fx) + ", porte " + std::to_string(g_port_triggers.size()) + "+" +
                    std::to_string(drv::fx_event_stats().trigger_fx - fx0));
        } else if (!g_hook.triggers.empty() && !same_trigger(g_hook.triggers[0], g_port_triggers[0], why)) {
            tp.fail("EPlayEffect (caso " + std::to_string(c) + "): " + why);
        }
        // O EPlayGiottoEffect: o registro (+0 a posição, +0x30 o quatérnio) e o nome.
        std::snprintf(g_port_name, sizeof g_port_name, "Ex_Test%u", r.next() % 100);
        put(e.m, kNameBuf, g_port_name, sizeof g_port_name);
        const uint32_t gobj[3] = {0x00370b58, kEffRec, kNameBuf};
        put(e.m, kEvent, gobj, 12);
        drv::PendingEvent gv{250, 0, 0.0f, 0, 0, 0};
        gv.obj[0] = 0x00370b58;
        gv.obj[1] = nf::p32_u(g_port_rec);
        gv.obj[2] = nf::p32_u(g_port_name);
        g_hook.clear();
        g_port_triggers.clear();
        call_f(e, kFnGiotto, {kEvent, 0});
        drv::fx_event_execute(gv);
        ++tg.cases;
        if (e.errors) {
            tg.fail("EPlayGiottoEffect: " + e.error);
            e.errors = 0;
        } else if (g_hook.triggers.size() != 1 || g_port_triggers.size() != 1) {
            tg.fail("EPlayGiottoEffect: disparos " + std::to_string(g_hook.triggers.size()) + "/" +
                    std::to_string(g_port_triggers.size()));
        } else {
            FxHookLog::Trigger a = g_hook.triggers[0], b = g_port_triggers[0];
            // O original passa o nome (o GFX::Trigger(char *)); o porte, a referência do nome.
            if (a.name != g_port_name) tg.fail("EPlayGiottoEffect: o nome " + a.name);
            a.name.clear();
            a.ref = b.ref;
            if (!same_trigger(a, b, why)) tg.fail("EPlayGiottoEffect: " + why);
        }
        // O ENuclearBlast: a escala (negativa: 1) e a posição.
        const float scale = r.chance(0.3) ? r.real(-5, 0) : r.real(0, 5);
        float np[4];
        random_vec(r, np, 4, -300.0f, 300.0f);
        put(e.m, kScratch + 0x300, np, 16);
        uint32_t nobj[3] = {0x00370e88, ps2::fbits(scale), kScratch + 0x300};
        put(e.m, kEvent, nobj, 12);
        static float port_np[4];
        std::memcpy(port_np, np, 16);
        drv::PendingEvent nv{196, 0, 0.0f, 0, 0, 0};
        nv.obj[0] = 0x00370e88;
        nv.obj[1] = ps2::fbits(scale);
        nv.obj[2] = nf::p32_u(port_np);
        g_hook.clear();
        g_port_nuclear.clear();
        call_f(e, kFnNuclear, {kEvent, 0});
        drv::fx_event_execute(nv);
        ++tn.cases;
        if (e.errors) {
            tn.fail("ENuclearBlast: " + e.error);
            e.errors = 0;
        } else if (g_hook.nuclear.size() != 1 || g_port_nuclear.size() != 1 ||
                   !same_words("a posição", g_hook.nuclear[0].pos, g_port_nuclear[0].pos, 12, why) ||
                   ps2::fbits(g_hook.nuclear[0].scale) != ps2::fbits(g_port_nuclear[0].scale)) {
            tn.fail("ENuclearBlast: " + (why.empty() ? std::string("a chamada difere") : why));
        }
        // O ESetCullDistanceFactor.
        const float factor = r.real(-2, 4);
        const uint32_t cobj[2] = {0x00370738, ps2::fbits(factor)};
        put(e.m, kEvent, cobj, 8);
        drv::PendingEvent cv{257, 0, 0.0f, 0, 0, 0};
        cv.obj[0] = 0x00370738;
        cv.obj[1] = ps2::fbits(factor);
        const float saved_cull = drv::CullDistance();
        call_f(e, kFnCullFactor, {kEvent, 0});
        drv::fx_event_execute(cv);
        ++tc.cases;
        if (e.errors) {
            tc.fail("ESetCullDistanceFactor: " + e.error);
            e.errors = 0;
        } else if (e.m.read32(kCullDistance) != ps2::fbits(drv::CullDistance())) {
            tc.fail("ESetCullDistanceFactor: original " + hex(e.m.read32(kCullDistance)) + ", porte " +
                    hex(ps2::fbits(drv::CullDistance())));
        }
        drv::CullDistance() = saved_cull;
    }
    fe = saved;
    drv::fx_events_env() = saved_fx;
    report("eventos: EPlayEffect (9)", tp, failures);
    report("eventos: EPlayGiottoEffect (250)", tg, failures);
    report("eventos: ENuclearBlast (196)", tn, failures);
    report("eventos: ESetCullDistanceFactor (257)", tc, failures);
}

// A parte f inteira, num interpretador próprio (os ganchos destas partes mudam o GFX::Trigger, que a galeria roda).
void check_lights_decals_events(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                                uint32_t vu0_size, Rng& r, int cases, int& failures) {
    Emu e;
    e.setup(elf, vu0_program, vu0_size);
    fx_hooks(e);
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
    check_lights(e, r, cases, failures);
    check_decals(e, r, dat, cases, failures);
    check_streak(e, r, cases, failures);
    check_missile_render(e, r, cases, failures);
    check_events(e, r, cases, failures);
}

}  // namespace

int run_fx_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                  int cases, uint32_t seed) {
    std::printf("\nparte 21: os efeitos (o sistema de partículas, a galeria) contra o DRIVING.ELF\n");
    int failures = 0;
    Emu e;
    e.setup(elf, vu0_program, vu0_size);
    Rng r(seed ^ 0x21f21u);
    // A galeria do disco (o MISC.VIV) e os tipos de partícula das chaves 'gp' dela.
    GalleryCtx g;
    {
        drv::Bigf misc;
        if (misc.open(disc, "DRIVING/MISC.VIV")) {
            const int i = misc.find("data\\gfx\\data\\gall1.gal");
            if (i >= 0) misc.read(i, g.file);
        }
    }
    std::vector<drv::ParticleData> types;
    if (!g.file.empty()) {
        drv::GFX_Init(g.file);
        drv::GfxGallery& pg = *drv::gfx_gallery();
        drv::UGroup* root = nf::p32_ptr<drv::UGroup>(pg.root);
        drv::UData* a = drv::UGroup_GetArray(root);
        for (uint32_t i = 0; i < drv::UGroup_GroupCount(root); ++i) {
            drv::UData* n = drv::UGroup_DataLocateTag(a + i, 0x4e616d65);
            if (n != drv::UGroup_DataEnd(a + i)) g.names.push_back(reinterpret_cast<const char*>(drv::UData_Data(n)));
            drv::UData* t = drv::UGroup_GetArray(a + i);
            for (uint32_t k = 0; k < drv::UGroup_GroupCount(a + i); ++k) {
                if (drv::UData_MatchTag(t + k) != 0x67702020) continue;
                const uint8_t* key = drv::GFXGallery_GET_FirstIdleKey(nf::p32_u(t + k));
                drv::ParticleData d;
                std::memcpy(&d, key + 0x30, sizeof d);
                types.push_back(d);
            }
        }
        drv::GFX_Shutdown();
        std::printf("  galeria gall1.gal: %zu efeitos, %zu tipos de partícula\n", g.names.size(), types.size());
    } else {
        std::printf("  sem o data\\gfx\\data\\gall1.gal do MISC.VIV: a galeria fica de fora\n");
        ++failures;
    }
    check_calc_emit(e, r, cases, failures);
    check_library(e, r, types, cases, failures);
    check_reset_particle(e, r, cases, failures);
    check_system(e, r, types, cases, failures);
    check_cache(e, r, types, cases, failures);
    check_manager(e, r, types, cases, failures);
    if (!g.file.empty()) check_gallery(e, r, g, cases, failures);
    check_lights_decals_events(elf, disc, vu0_program, vu0_size, r, cases, failures);
    drop_port_systems();
    drv::RParticleSystemManager_Shutdown();
    std::fflush(stdout);
    return failures;
}

}  // namespace drvcheck
