// nf_driving_check, parte 25 (tests/driving_check_rest.h): os efeitos e os sons que faltavam em Paris, o código ORIGINAL
// do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte (docs/driving.md seção 29).
//
//  a) O banco de ajustes: o DTuningFile::Reader::FindItem e FindIndexedItem originais (o OptionParser, o 0x001440e0, o
//     sprintf e o strstr/strlen/strcpy/strchr/strncpy do newlib) sobre os .tun do disco ("Render:FX" e "Render:Glare"
//     de Paris) e textos sorteados, contra o porte; o ParseData_Colour; as tabelas inteiras do RBulletStreak e do
//     RGlareManager montadas pelas buscas originais.
//  b) O RBulletStreak: o construtor estático (os quatro UVolatileMaterial com o GeoPrimState original), o Init (sem os
//     dbattrib: as formas, os UV, as cores e o material), o Add dos dois tipos com estados sorteados, o Draw (o
//     UVolatileMaterial::Draw original: o material e a camada que o DynamicModel recebe), o Frame.
//  c) Os tiros instantâneos: o RayShell::DrawTracers (com o ClearTracers, o DeleteTracer e o RBulletStreak::Add
//     originais; o RSceneObj::LocateFX e as mãos sorteados) e o DrawFlashes (o RLightManager::AddPositionalLight
//     original).
//  d) Os clarões: o CreateUVsFromTexIDs, o AddGlare, o GlareBlinkBrightness, o AddModelGlare (o TransformPoint, o
//     TransformVector e o VEC3_Dot originais), o DrawGlares (as listas do EAGL::DrawArray), o GLARE_Interpolate, o
//     GLARE_Draw (o tremor), o GET_Key, o GET_Keys, o DataIndex, o GSystem::RANDOM_Startup e o DrawGroupDrawEffects.
//  e) Os destroços: o RDebris e o RDebrisType (o construtor, o Add, o Draw), o GFXGallery::DEBRIS_Draw e os modelos dos
//     tipos 4 e 22 (0x001d93e8).
//  f) O som dos personagens: o ACharacter::Say (as rodas de nomes, os contadores do .data, o alcance, a voz sem laço;
//     o AIndex::Lookup e o SNDbankpatpresent por uma regra fixa nos dois lados, o UMemory::FastAlloc e o AVoice::AVoice
//     gravados) e o ACharacter::Play (o volume ao AVoice::Play e o fim da voz pela máscara das vistas); o
//     GFXGallery::SOUND_Play dos sons 'gs' da galeria (as cópias, a mixagem, o AVoice::Set e os números da chave).
//
// Fora do código original (ganchos nativos): o sprintf (%s e %d), o sscanf (%d e %f, a cor), o operator new e o free,
// o printf, o RTextureContext (as texturas falsas), os dbindex/dbattrib no Init estrutural, o EAGL::DynamicModel::Draw
// (grava a camada), o RSceneObj::LocateFX (uma matriz por objeto e efeito, a mesma nos dois lados), o EAGL::DrawArray::Draw
// e o 0x0028c318 (o ZMSK).

#include "driving_check_rest.h"

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
#include "driving/bigf.h"
#include "driving/ai/sim_random.h"
#include "driving/fx/bullet_streak.h"
#include "driving/fx/debris.h"
#include "driving/fx/fx_lights.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/glare.h"
#include "driving/fx/tuning_db.h"
#include "driving/camera/dafi.h"
#include "driving/game/ai_character.h"
#include "driving/game/bond_car.h"
#include "driving/game/shells.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "platform/crt_compat.h"
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
constexpr uint32_t kFnFindItem = 0x144548, kFnFindIndexed = 0x144648, kFnParseColour = 0x144740;
constexpr uint32_t kFnStreakInit = 0x1d7620, kFnStreakAdd4 = 0x1d7d78, kFnStreakAdd3 = 0x1d8048;
constexpr uint32_t kFnStreakDraw = 0x1d7c18, kFnStreakFrame = 0x1d7d50, kFnStreakStatic = 0x1d82e0;
constexpr uint32_t kFnVolatileCtor = 0x2cb860;
constexpr uint32_t kFnDrawTracers = 0x1993e8, kFnDrawFlashes = 0x199678;
constexpr uint32_t kFnCreateUVs = 0x1f19f0, kFnAddGlare = 0x1f1f88, kFnBlink = 0x1f1b60, kFnAddModelGlare = 0x1f1d18;
constexpr uint32_t kFnDrawGlares = 0x1f2058, kFnGlareInterp = 0x235ea0, kFnGlareDraw = 0x235d50;
constexpr uint32_t kFnGetKey = 0x233dd0, kFnGetKeys = 0x233df8, kFnDataIndex = 0x2339c8, kFnRandomStartup = 0x24b328;
constexpr uint32_t kFnDrawGroupEffects = 0x1ad1a0;
// Os ganchos.
constexpr uint32_t kFnSprintf = 0x2a9098, kFnSscanf = 0x2a9110, kFnOpNew = 0x2d09c8, kFnFree = 0x2d0a50;
constexpr uint32_t kFnPrintf = 0x2a8148, kFnGetContext = 0x1d0420, kFnFindTexture = 0x1cf340;
constexpr uint32_t kFnDbIndex = 0x142b68, kFnDbArgb = 0x143ad8, kFnDbS8 = 0x142088, kFnDbFloat = 0x1426a8;
constexpr uint32_t kFnDbEnd = 0x142be8, kFnDynModelDraw = 0x285388, kFnTimerRemove = 0x2913f8;
constexpr uint32_t kFnLocateFX = 0x1cae00, kFnDrawArrayDraw = 0x29c4b0, kFn28c318 = 0x28c318;
constexpr uint32_t kFnMemset = 0x2a8038;

// Os globais do PS2.
constexpr uint32_t kStreakTable = 0x423f38, kStreakCount = 0x425178, kStreakCoords = 0x425190;
constexpr uint32_t kStreakUv = 0x426490, kStreakColours = 0x3344d0, kStreakMaterial = 0x334290;
constexpr uint32_t kStreakTexture = 0x334990, kLayerData = 0x3c8338, kLayerCur = 0x3c833c, kDynModel = 0x3c83c8;
constexpr uint32_t kCameraHolder = 0x333ed8, kHands = 0x327670, kSim = 0x339190;
constexpr uint32_t kNumTracers = 0x3320b4, kTracers = 0x3320c0, kTracerSide = 0x424180;
constexpr uint32_t kNumFlashes = 0x3328c0, kFlashes = 0x3328d0;
constexpr uint32_t kLmPtr = 0x333830, kColorizePtr = 0x3349e0, kRendererPtr = 0x333760, kTime = 0x334010;
constexpr uint32_t kGlareMgrPtr = 0x339090, kGlareBuffer = 0x3390a0, kGlareCoords = 0x3e1f40, kGlareUvuv = 0x3dff40;
constexpr uint32_t kGlareColours = 0x3df630, kGlareControl = 0x3e3f40, kGlareEnabled = 0x333784;
constexpr uint32_t kJitterSeed = 0x33ba74, kJitterMul = 0x33ba78;

// A RAM emulada.
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;
constexpr uint32_t kFake = 0x01900000;
constexpr uint32_t kText = kFake;                       // o texto de um .tun (até 0x40000)
constexpr uint32_t kReader = kFake + 0x40000;           // o Reader (+0 o texto)
constexpr uint32_t kTextures = kFake + 0x40100;         // as texturas falsas (0x40 bytes cada)
constexpr uint32_t kLayer = kFake + 0x41000;            // a camada do UVolatileMaterial
constexpr uint32_t kCamHolder = kFake + 0x41100, kRCamera = kFake + 0x41200, kViewCam = kFake + 0x41300;
constexpr uint32_t kRenderer = kFake + 0x41400, kHandsObj = kFake + 0x41500, kColorize = kFake + 0x41800;
constexpr uint32_t kLm = kFake + 0x42000, kLmBuild = kFake + 0x42400, kLmPub = kFake + 0x42480;
constexpr uint32_t kGlareMgr = kFake + 0x50000;         // o RGlareManager (0x3438)
constexpr uint32_t kScratch = kFake + 0x60000;          // os argumentos dos casos
constexpr uint32_t kKeys = kFake + 0x61000;             // as chaves sorteadas
constexpr uint32_t kEffects = kFake + 0x62000;          // as listas de efeitos do DrawGroupDrawEffects

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2525) {}
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
    // (até 256 KB: os .tun inteiros)
    const uint32_t n = std::min<uint32_t>(0x40000, MipsMachine::kRamSize - a);
    const char* p = reinterpret_cast<const char*>(m.mem(a, n));
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
    std::printf("  %-56s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    failures += int(t.bad);
    std::fflush(stdout);
}

void put(MipsMachine& m, uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }
void get(const MipsMachine& m, uint32_t addr, void* dst, uint32_t n) { std::memcpy(dst, m.mem(addr, n), n); }

bool same_words(const char* what, const void* emu, const void* port, uint32_t n, std::string& out) {
    const uint8_t* a = static_cast<const uint8_t*>(emu);
    const uint8_t* b = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
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

// Um formatador mínimo (o sprintf do newlib nos formatos que estas funções usam: %s, %d, %u, %x).
std::string format(MipsMachine& m, const std::string& fmt, uint32_t first_arg_reg) {
    std::string out;
    uint32_t reg = first_arg_reg;
    auto arg = [&]() { return uint32_t(m.gpr(R(reg++))); };
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out += fmt[i];
            continue;
        }
        const char c = fmt[++i];
        char b[32];
        if (c == 's') out += cstr(m, arg());
        else if (c == 'd') std::snprintf(b, sizeof b, "%d", int32_t(arg())), out += b;
        else if (c == 'u') std::snprintf(b, sizeof b, "%u", arg()), out += b;
        else if (c == 'x') std::snprintf(b, sizeof b, "%x", arg()), out += b;
        else if (c == '%') out += '%';
    }
    return out;
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::string error;
    uint64_t errors = 0;
    // O EAGL::DynamicModel::Draw: a camada do último desenho.
    struct LayerDraw {
        uint32_t material, count, coords, uv, colours, texture;
    };
    std::vector<LayerDraw> layer_draws;
    // O EAGL::DrawArray::Draw: (a lista, a contagem).
    std::vector<std::pair<uint32_t, uint32_t>> array_draws;
    std::function<void(uint32_t scene, uint32_t effect, float m[4][4])> locate;

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
    float getf(uint32_t a) const { return ps2::ffloat(m.read32(a)); }
    void putf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(kFnSprintf, "sprintf", [](MipsMachine& mm) {
            const std::string s = format(mm, cstr(mm, uint32_t(mm.gpr(R::a1))), uint32_t(R::a2));
            put(mm, uint32_t(mm.gpr(R::a0)), s.c_str(), uint32_t(s.size() + 1));
            mm.set_gpr(R::v0, uint32_t(s.size()));
        });
        m.hook(kFnSscanf, "sscanf", [](MipsMachine& mm) {
            // Os formatos dos dbattrib e do ParseData_Colour: "%d", "%f" e "%d,%d,%d,%d" (os ponteiros em a2..a5).
            const std::string s = cstr(mm, uint32_t(mm.gpr(R::a0)));
            const std::string f = cstr(mm, uint32_t(mm.gpr(R::a1)));
            int n = 0;
            if (f == "%f") {
                float x = 0;
                n = NF_SSCANF(s.c_str(), "%f", &x);
                if (n == 1) mm.write32(uint32_t(mm.gpr(R::a2)), ps2::fbits(x));
            } else if (f == "%d") {
                int x = 0;
                n = NF_SSCANF(s.c_str(), "%d", &x);
                if (n == 1) mm.write32(uint32_t(mm.gpr(R::a2)), uint32_t(x));
            } else if (f == "%d,%d,%d,%d") {
                int x[4] = {0, 0, 0, 0};
                n = NF_SSCANF(s.c_str(), "%d,%d,%d,%d", &x[0], &x[1], &x[2], &x[3]);
                const R regs[4] = {R::a2, R::a3, R::t0, R::t1};
                for (int i = 0; i < n; ++i) mm.write32(uint32_t(mm.gpr(regs[i])), uint32_t(x[i]));
            } else {
                throw nf::MipsError("sscanf com um formato não previsto: " + f, 0, 0);
            }
            mm.set_gpr(R::v0, uint32_t(n));
        });
        m.hook(kFnOpNew, "operator new", [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u))); });
        m.hook(kFnFree, "free", [](MipsMachine&) {});
        // O strlen, strstr, strchr, strcpy e strncpy do newlib (as versões de 128 bits com o MMI): a semântica do C.
        m.hook(0x2a9760, "strlen", [](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(cstr(mm, uint32_t(mm.gpr(R::a0))).size())); });
        m.hook(0x2a9c10, "strstr", [](MipsMachine& mm) {
            const uint32_t h = uint32_t(mm.gpr(R::a0));
            const std::string a = cstr(mm, h), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const size_t p = a.find(b);
            mm.set_gpr(R::v0, p == std::string::npos ? 0u : h + uint32_t(p));
        });
        m.hook(0x2a9378, "strchr", [](MipsMachine& mm) {
            const uint32_t h = uint32_t(mm.gpr(R::a0));
            const std::string a = cstr(mm, h);
            const char c = char(mm.gpr(R::a1) & 0xff);
            const size_t p = c == 0 ? a.size() : a.find(c);
            mm.set_gpr(R::v0, p == std::string::npos ? 0u : h + uint32_t(p));
        });
        m.hook(0x2a964c, "strcpy", [](MipsMachine& mm) {
            const std::string s = cstr(mm, uint32_t(mm.gpr(R::a1)));
            put(mm, uint32_t(mm.gpr(R::a0)), s.c_str(), uint32_t(s.size() + 1));
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2a9a50, "strncpy", [](MipsMachine& mm) {
            const std::string s = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const uint32_t n = uint32_t(mm.gpr(R::a2)), d = uint32_t(mm.gpr(R::a0));
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t w = (d + i) & ~3u, sh = 8 * ((d + i) & 3);
                const uint32_t c = i < s.size() ? uint8_t(s[i]) : 0u;
                mm.write32(w, (mm.read32(w) & ~(0xffu << sh)) | c << sh);
            }
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnPrintf, "printf", [](MipsMachine&) {});
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnGetContext, "RTextureContextManager::GetContext", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnFindTexture, "RTextureContext::FindOrCreateTexture", [](MipsMachine& mm) {
            // Uma textura falsa por etiqueta (as quatro formas e o 'flar').
            const uint32_t tag = uint32_t(mm.gpr(R::a1));
            uint32_t slot = 0;
            for (uint32_t i = 0; i < 16; ++i) {
                const uint32_t t = mm.read32(kTextures + i * 0x40 + 0x3c);
                if (t == tag || t == 0) {
                    slot = i;
                    break;
                }
            }
            mm.write32(kTextures + slot * 0x40 + 0x3c, tag);
            mm.set_gpr(R::v0, kTextures + slot * 0x40);
        });
        m.hook(kFnDynModelDraw, "EAGL::DynamicModel::Draw", [this](MipsMachine& mm) {
            const uint32_t l = mm.read32(kLayerData);
            LayerDraw d{mm.read32(l + 0x38), mm.read32(l + 0x14), mm.read32(l + 0x18), mm.read32(l + 0x20),
                        mm.read32(l + 0x28), mm.read32(l + 0x30)};
            layer_draws.push_back(d);
        });
        m.hook(kFnTimerRemove, "TIMER_removetask", [](MipsMachine&) {});
        m.hook(kFnLocateFX, "RSceneObj::LocateFX", [this](MipsMachine& mm) {
            float mat[4][4];
            locate(uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), mat);
            put(mm, uint32_t(mm.gpr(R::a2)), mat, 64);
            mm.set_gpr(R::v0, 1);
        });
        m.hook(kFnDrawArrayDraw, "EAGL::DrawArray::Draw", [this](MipsMachine& mm) {
            array_draws.push_back({uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1))});
        });
        m.hook(kFn28c318, "(0x28c318: o ZMSK)", [](MipsMachine&) {});
        // A câmera do jogador (0x333ed8 +4 -> o RCamera), o RRenderer (+4 a vista: +0 o RCamera, +0x50 a margem), as
        // mãos, o RLightManager e o RColorize.
        m.write32(kCameraHolder, kCamHolder);
        m.write32(kCamHolder + 4, kRCamera);
        m.write32(kRendererPtr, kRenderer);
        m.write32(kRenderer + 4, kViewCam);
        m.write32(kViewCam, kRCamera);
        putf(kViewCam + 0x50, 4.0f);
        putf(kRCamera + 0xa4, 45.0f);
        m.write32(kHands, kHandsObj);
        m.write32(kLmPtr, kLm);
        m.write32(kLm + 0x2a0, kLmBuild);
        m.write32(kLm + 0x2a4, kLmPub);
        m.write32(kColorizePtr, kColorize);
        m.write32(kGlareMgrPtr, kGlareMgr);
        m.write32(kLayerData, kLayer);
        m.write32(kLayerCur, 0);
        m.write32(kDynModel, kFake + 0x41c00);
        m.write32(kReader, kText);
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
// Uma matriz de rotação e posição sorteada (linhas; o w das três primeiras 0, o da última 1).
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
    random_vec(r, m[3], 3, -500, 500);
    m[3][3] = 1;
}

// ------------------------------------------------------------------------------------------- a) o banco de ajustes

// As chaves de busca: as dos atributos dos dois sistemas com os índices, mais variações (sem a chave, com espaços,
// maiúsculas trocadas).
std::vector<std::pair<std::string, std::string>> tuning_keys(Rng& r) {
    std::vector<std::pair<std::string, std::string>> k;
    static const char* const kStreak[] = {"BulletStreakColour", "BulletStreakTexture", "BulletStreakWidth",
                                          "BulletStreakLength", "BulletStreakSpeed",   "BulletStreakCount"};
    static const char* const kTypes[] = {"Henchmen", "Vanquish", "SuperSnowmobile", "SUV", "Captain", "Ultralight",
                                         "Sentinel", "Nobody"};
    for (const char* a : kStreak)
        for (const char* t : kTypes) k.push_back({a, t});
    static const char* const kGlare[] = {"Halo Tex",  "Halo Spin",  "Halo Col",  "Halo Size", "Spike Tex",
                                         "Spike Spin", "Spike Col", "Spike Size", "Z Bias",    "ZBias"};
    for (const char* a : kGlare)
        for (int i = 0; i < 82; ++i) k.push_back({a, std::to_string(i)});
    for (int i = 0; i < 64; ++i) {
        std::string a = k[size_t(r.range(0, int(k.size()) - 1))].first;
        if (r.chance(0.5)) a[size_t(r.range(0, int(a.size()) - 1))] ^= 0x20;
        k.push_back({a, std::to_string(r.range(-3, 90))});
    }
    return k;
}

// Um .tun sorteado: linhas "chave{índice}=valor" com espaços, '#', '(', '[', ']' e ')' e chaves que contêm outras.
std::string random_tun(Rng& r) {
    static const char* const kNames[] = {"HaloTex", "HaloSize", "ZBias", "BulletStreakSpeed", "BulletStreakColour"};
    std::string t;
    const int lines = r.range(1, 60);
    for (int i = 0; i < lines; ++i) {
        std::string key = kNames[r.range(0, 4)];
        if (r.chance(0.8)) key += "{" + std::to_string(r.range(0, 9)) + "}";
        if (r.chance(0.1)) key += "X";
        if (r.chance(0.1)) t += " ";
        t += key;
        const int sep = r.range(0, 4);
        t += sep == 0 ? "=" : sep == 1 ? " = " : sep == 2 ? "=(" : sep == 3 ? " [" : "#";
        t += std::to_string(r.range(-100, 300));
        if (r.chance(0.3)) t += "," + std::to_string(r.range(0, 255)) + "," + std::to_string(r.range(0, 255)) + ",7";
        if (r.chance(0.1)) t += "]";
        if (r.chance(0.1)) t += ")";
        t += r.chance(0.3) ? "\r\n" : "\n";
    }
    return t;
}

void check_tuning(Emu& e, Rng& r, const std::vector<std::string>& files, int cases, int& failures) {
    Tally t, tc;
    std::vector<std::string> texts = files;
    for (int i = 0; i < std::max(4, cases / 200); ++i) texts.push_back(random_tun(r));
    for (const std::string& text : texts) {
        if (text.size() + 1 > 0x40000) continue;
        put(e.m, kText, text.c_str(), uint32_t(text.size() + 1));
        drv::TuningReader rd;
        rd.text = text;
        for (const auto& k : tuning_keys(r)) {
            put(e.m, kScratch, k.first.c_str(), uint32_t(k.first.size() + 1));
            put(e.m, kScratch + 0x100, k.second.c_str(), uint32_t(k.second.size() + 1));
            const bool plain = k.second == "-1";
            const uint32_t v = plain ? call_f(e, kFnFindItem, {kReader, kScratch})
                                     : call_f(e, kFnFindIndexed, {kReader, kScratch, kScratch + 0x100});
            ++t.cases;
            if (take_error(e, t, "FindIndexedItem")) continue;
            std::string port;
            const bool found = plain ? drv::DTuningFile_FindItem(rd, k.first.c_str(), port)
                                     : drv::DTuningFile_FindIndexedItem(rd, k.first.c_str(), k.second.c_str(), port);
            const std::string emu = v ? cstr(e.m, v) : std::string();
            if (found != (v != 0) || (found && emu != port)) {
                t.fail(k.first + "{" + k.second + "}: original " + (v ? "\"" + emu + "\"" : "nada") + ", porte " +
                       (found ? "\"" + port + "\"" : "nada"));
                continue;
            }
            if (!found) continue;
            // A cor (o ParseData_Colour original, o sscanf nativo).
            put(e.m, kScratch + 0x200, emu.c_str(), uint32_t(emu.size() + 1));
            e.m.write32(kScratch + 0x300, 0x5a5a5a5au);
            call_f(e, kFnParseColour, {kScratch + 0x200, kScratch + 0x300});
            uint32_t pc = 0x5a5a5a5au;
            drv::DTuningFile_ParseData_Colour(&port, pc);
            ++tc.cases;
            if (take_error(e, tc, "ParseData_Colour")) continue;
            // (o PS2 deixa o lixo da pilha nos números que faltam: só as cores com os quatro números contam)
            int c[4];
            if (NF_SSCANF(port.c_str(), "%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3]) == 4 &&
                e.m.read32(kScratch + 0x300) != pc)
                tc.fail("\"" + port + "\": original " + hex(e.m.read32(kScratch + 0x300)) + ", porte " + hex(pc));
        }
    }
    report("ajustes: FindItem e FindIndexedItem (o OptionParser)", t, failures);
    report("ajustes: ParseData_Colour", tc, failures);
}

// As tabelas inteiras: o porte (RBulletStreak_Init, RGlareManager_Construct) contra as buscas originais com os mesmos
// passos e conversões que os dbattrib fazem.
void check_tables(Emu& e, const std::string& fx, const std::string& glare, int& failures) {
    Tally t;
    auto lookup = [&](const std::string& text, const std::string& name, const std::string& idx, std::string& v) {
        put(e.m, kText, text.c_str(), uint32_t(text.size() + 1));
        put(e.m, kScratch, name.c_str(), uint32_t(name.size() + 1));
        put(e.m, kScratch + 0x100, idx.c_str(), uint32_t(idx.size() + 1));
        const uint32_t a = call_f(e, kFnFindIndexed, {kReader, kScratch, kScratch + 0x100});
        if (!a) return false;
        v = cstr(e.m, a);
        return true;
    };
    {
        drv::BulletStreakState s;
        drv::TuningReader rd;
        rd.text = fx;
        drv::RBulletStreak_Init(s, rd);
        uint8_t want[drv::kBulletStreakTypes * 0x18] = {};
        static const char* const kTypes[] = {"Henchmen", "Vanquish", "SuperSnowmobile", "SUV",
                                             "Captain",  "Ultralight", "Sentinel"};
        static const char* const kAttr[] = {"BulletStreakColour", "BulletStreakTexture", "BulletStreakWidth",
                                            "BulletStreakLength", "BulletStreakSpeed",   "BulletStreakCount"};
        for (int i = 0; i < drv::kBulletStreakTypes; ++i)
            for (int a = 0; a < 6; ++a) {
                std::string v;
                if (!lookup(fx, kAttr[a], kTypes[i], v)) continue;
                uint8_t* dst = want + i * 0x18 + a * 4;
                if (a == 0) {
                    uint32_t c = 0;
                    drv::DTuningFile_ParseData_Colour(&v, c);
                    std::memcpy(dst, &c, 4);
                } else if (a == 1 || a == 5) {
                    int x = 0;
                    NF_SSCANF(v.c_str(), "%d", &x);
                    std::memcpy(dst, &x, 4);
                } else {
                    float x = 0;
                    NF_SSCANF(v.c_str(), "%f", &x);
                    std::memcpy(dst, &x, 4);
                }
            }
        ++t.cases;
        std::string why;
        if (!same_words("a tabela 0x423f38", want, s.types, sizeof want, why)) t.fail(why);
    }
    {
        static drv::RGlareManager g;
        drv::TuningReader rd;
        rd.text = glare;
        drv::RGlareManager_Construct(g, rd);
        static const char* const kAttr[] = {"Halo Tex", "Halo Spin", "Halo Col", "Halo Size", "Spike Tex",
                                            "Spike Spin", "Spike Col", "Spike Size", "Z Bias"};
        static const uint32_t kOff[] = {0x30, 0x0c, 0x20, 0x28, 0x34, 0x1c, 0x24, 0x2c, 0x38};
        for (int i = 0; i < 0x50; ++i) {
            for (int a = 0; a < 9; ++a) {
                std::string v;
                ++t.cases;
                const uint8_t* got = reinterpret_cast<const uint8_t*>(&g.types[i]) + kOff[a];
                uint32_t want;
                std::memcpy(&want, got, 4);
                if (!lookup(glare, kAttr[a], std::to_string(i), v)) {
                    // sem a chave: o valor do construtor (o Z Bias 2, ou 0,3 nos tipos 33, 34 e 52; zero nos outros)
                    const float z = (i == 0x21 || i == 0x22 || i == 0x34) ? ps2::ffloat(0x3e99999a) : 2.0f;
                    const uint32_t def = a == 8 ? ps2::fbits(z) : 0u;
                    if (want != def) t.fail("tipo " + std::to_string(i) + " " + kAttr[a] + " sem chave: " + hex(want));
                    continue;
                }
                uint32_t exp = 0;
                if (a == 2 || a == 6) {
                    drv::DTuningFile_ParseData_Colour(&v, exp);
                } else if (a == 0 || a == 4) {
                    int x = 0;
                    NF_SSCANF(v.c_str(), "%d", &x);
                    exp = uint32_t(x);
                } else {
                    float x = 0;
                    NF_SSCANF(v.c_str(), "%f", &x);
                    exp = ps2::fbits(x);
                }
                if (exp != want)
                    t.fail("tipo " + std::to_string(i) + " " + kAttr[a] + ": esperado " + hex(exp) + ", porte " + hex(want));
            }
        }
    }
    if (e.errors) t.fail("as buscas: " + e.error), e.errors = 0;
    report("ajustes: as tabelas do RBulletStreak e do RGlareManager", t, failures);
}

// ------------------------------------------------------------------------------------------- b) o RBulletStreak

void load_streak(Emu& e, const drv::BulletStreakState& s) {
    put(e.m, kStreakTable, s.types, sizeof s.types);
    put(e.m, kStreakCount, s.count, sizeof s.count);
    put(e.m, kStreakCoords, s.coords, sizeof s.coords);
    put(e.m, kStreakUv, s.uv, sizeof s.uv);
    put(e.m, kStreakColours, s.colours, sizeof s.colours);
    put(e.m, kStreakMaterial, s.material, sizeof s.material);
}

bool compare_streak(Emu& e, const drv::BulletStreakState& s, std::string& why) {
    return same_words("as contagens (0x425178)", e.m.mem(kStreakCount, 16), s.count, 16, why) &&
           same_words("os vértices (0x425190)", e.m.mem(kStreakCoords, sizeof s.coords), s.coords, sizeof s.coords,
                      why) &&
           same_words("os UV (0x426490)", e.m.mem(kStreakUv, sizeof s.uv), s.uv, sizeof s.uv, why) &&
           same_words("as cores (fgColours)", e.m.mem(kStreakColours, sizeof s.colours), s.colours, sizeof s.colours,
                      why) &&
           same_words("os materiais (fgMaterial)", e.m.mem(kStreakMaterial, sizeof s.material), s.material,
                      sizeof s.material, why);
}

void random_streak_state(Rng& r, drv::BulletStreakState& s) {
    for (auto& ty : s.types) {
        ty.colour = r.next();
        ty.shape = r.range(0, 3);
        ty.width = r.real(0.0f, 0.1f);
        ty.length = r.real(0.0f, 8.0f);
        ty.speed = r.real(10.0f, 300.0f);
        ty.count = r.range(0, 4);
    }
    for (int i = 0; i < 4; ++i) s.count[i] = r.chance(0.1) ? r.range(0x40, 0x4c) : r.range(0, 0x40);
    for (auto& sh : s.coords)
        for (auto& v : sh) random_vec(r, v, 4, -500, 500);
}

void put_camera(Emu& e, const float eye[4]) { put(e.m, kRCamera + 0x30, eye, 16); }

void check_streak(Emu& e, Rng& r, const std::string& fx, int cases, int& failures) {
    static drv::BulletStreakState s;
    Tally ts, ti, ta, td;
    // O construtor estático e o Init (sem os dbattrib: ganchos que não fazem nada).
    {
        s = drv::BulletStreakState();
        drv::RBulletStreak_StaticInit(s);
        std::memset(e.m.mem(kStreakMaterial, sizeof s.material), 0xcd, sizeof s.material);
        std::memset(e.m.mem(kStreakColours, sizeof s.colours), 0xcd, sizeof s.colours);
        call_f(e, kFnStreakStatic, {1, 0xffff});
        ++ts.cases;
        std::string why;
        if (!take_error(e, ts, "o construtor estático") &&
            !(same_words("os materiais (fgMaterial)", e.m.mem(kStreakMaterial, sizeof s.material), s.material,
                         sizeof s.material, why) &&
              same_words("as cores (fgColours)", e.m.mem(kStreakColours, sizeof s.colours), s.colours,
                         sizeof s.colours, why)))
            ts.fail(why);
        for (uint32_t f : {kFnDbIndex, kFnDbArgb, kFnDbS8, kFnDbFloat, kFnDbEnd}) e.m.hook(f, "dbindex/dbattrib", [](MipsMachine&) {});
        load_streak(e, s);
        std::memset(e.m.mem(kTextures, 0x400), 0, 0x400);
        drv::TuningReader none;
        drv::RBulletStreak_Init(s, none);
        call_f(e, kFnStreakInit, {});
        ++ti.cases;
        if (!take_error(e, ti, "o Init") && !compare_streak(e, s, why)) ti.fail(why);
        // As texturas: as etiquetas pedidas e o +0x10 escrito.
        static const uint32_t kTags[4] = {0x77747263, 0x79747263, 0x6f747263, 0x62747263};  // 'wtrc' ... (big endian)
        for (int i = 0; i < 4; ++i) {
            const uint32_t tex = e.m.read32(kStreakTexture + uint32_t(i) * 4);
            if (tex != kTextures + uint32_t(i) * 0x40 || e.m.read32(tex + 0x3c) != (kTags[i] >> 24 | (kTags[i] >> 8 & 0xff00) | (kTags[i] << 8 & 0xff0000) | kTags[i] << 24) ||
                e.m.read32(tex + 0x10) != s.texture_flags[i])
                ti.fail("a textura " + std::to_string(i) + ": " + hex(e.m.read32(tex + 0x3c)) + " +0x10 " +
                        hex(e.m.read32(tex + 0x10)) + ", porte " + hex(s.texture_flags[i]));
        }
        for (uint32_t f : {kFnDbIndex, kFnDbArgb, kFnDbS8, kFnDbFloat, kFnDbEnd}) e.m.unhook(f);
        drv::TuningReader rd;
        rd.text = fx;
        drv::RBulletStreak_Init(s, rd);
    }
    report("riscos: o construtor estático (os UVolatileMaterial)", ts, failures);
    report("riscos: RBulletStreak::Init (as formas, sem os ajustes)", ti, failures);
    // O Add dos dois tipos.
    for (int c = 0; c < cases; ++c) {
        if (c % 64 == 0) random_streak_state(r, s);
        load_streak(e, s);
        float eye[4], p[4], d[4];
        random_vec(r, eye, 4, -500, 500);
        random_vec(r, p, 4, -500, 500);
        random_unit(r, d);
        d[3] = r.real(-2, 2);
        if (r.chance(0.05)) std::memcpy(d, eye, 12);  // casos degenerados
        put_camera(e, eye);
        const int32_t type = r.range(0, 6);
        const bool three = r.chance(0.4);
        put(e.m, kScratch, p, 16);
        put(e.m, kScratch + 0x10, d, 16);
        if (three) {
            call_f(e, kFnStreakAdd3, {kScratch, kScratch + 0x10, uint32_t(type)});
            drv::RBulletStreak_Add3(s, eye, p, d, type);
        } else {
            call_f(e, kFnStreakAdd4, {kScratch, kScratch + 0x10, uint32_t(type)});
            drv::RBulletStreak_Add(s, eye, p, d, type);
        }
        ++ta.cases;
        std::string why;
        if (!take_error(e, ta, "Add") && !compare_streak(e, s, why)) ta.fail(std::string(three ? "Add3: " : "Add: ") + why);
        for (int i = 0; i < 4; ++i)
            if (s.count[i] > 0x46) s.count[i] = r.range(0, 0x40);
    }
    report("riscos: RBulletStreak::Add (COORD4 e COORD3)", ta, failures);
    // O Draw (o UVolatileMaterial::Draw original e a camada que o DynamicModel recebe) e o Frame.
    for (int c = 0; c < std::max(50, cases / 20); ++c) {
        random_streak_state(r, s);
        for (int i = 0; i < 4; ++i) s.count[i] = r.chance(0.3) ? 0 : r.range(1, 0x46);
        load_streak(e, s);
        std::memset(e.m.mem(kLayer, 0x40), 0, 0x40);
        e.layer_draws.clear();
        std::vector<drv::VolatileDraw> out;
        const drv::BulletStreakState before = s;
        const bool frame = r.chance(0.2);
        if (frame) {
            call_f(e, kFnStreakFrame, {});
            drv::RBulletStreak_Frame(s);
        } else {
            call_f(e, kFnStreakDraw, {});
            drv::RBulletStreak_Draw(s, &out);
        }
        ++td.cases;
        std::string why;
        if (take_error(e, td, frame ? "Frame" : "Draw")) continue;
        if (!compare_streak(e, s, why)) {
            td.fail(why);
            continue;
        }
        if (frame) continue;
        if (e.layer_draws.size() != out.size()) {
            td.fail("desenhos: original " + std::to_string(e.layer_draws.size()) + ", porte " + std::to_string(out.size()));
            continue;
        }
        for (size_t k = 0; k < out.size(); ++k) {
            const Emu::LayerDraw& l = e.layer_draws[k];
            const drv::VolatileDraw& v = out[k];
            const int sh = v.shape;
            if (l.material != kStreakMaterial + uint32_t(sh) * 0x90 || l.count != uint32_t(v.count) ||
                l.coords != kStreakCoords + uint32_t(sh) * 0x4c0 || l.uv != kStreakUv + uint32_t(sh) * 0x260 ||
                l.colours != kStreakColours + uint32_t(sh) * 0x130 ||
                l.texture != e.m.read32(kStreakTexture + uint32_t(sh) * 4)) {
                td.fail("a camada do desenho " + std::to_string(k));
                break;
            }
            if (std::memcmp(v.coords.data(), before.coords[sh], size_t(v.count) * 16) != 0 ||
                std::memcmp(v.uv.data(), before.uv[sh], size_t(v.count) * 8) != 0 ||
                std::memcmp(v.colours.data(), before.colours[sh], size_t(v.count) * 4) != 0) {
                td.fail("as listas do desenho " + std::to_string(k));
                break;
            }
        }
    }
    report("riscos: RBulletStreak::Draw (UVolatileMaterial::Draw) e Frame", td, failures);
}

// ------------------------------------------------------------------------------------------- c) os tiros instantâneos

uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    return h ^ (h >> 12);
}
// A matriz do LocateFX: uma função do objeto e do efeito (a mesma nos dois lados).
void locate_matrix(uint32_t scene, uint32_t effect, float m[4][4]) {
    Rng r(mix(scene, effect));
    random_matrix(r, m);
}

void check_tracers(Emu& e, Rng& r, int cases, int& failures) {
    static drv::BulletStreakState s;
    drv::RayShellGlobals& g = drv::rayshell_globals();
    drv::Simulation& sim = drv::simulation();
    static drv::AICharacterHandsData hands;
    Tally tt, tf;
    e.locate = locate_matrix;
    drv::shells_env().locate_fx = [](uint32_t scene, uint32_t effect, float m[4][4]) {
        locate_matrix(scene, effect, m);
        return true;
    };
    const uint32_t saved_hands = drv::hands_globals().hands;
    const int32_t saved_steps = sim.steps;
    const float saved_dt = sim.dt;
    for (int c = 0; c < cases; ++c) {
        random_streak_state(r, s);
        for (int i = 0; i < 4; ++i) s.count[i] = r.range(0, 0x30);
        load_streak(e, s);
        float eye[4];
        random_vec(r, eye, 4, -300, 300);
        put_camera(e, eye);
        sim.steps = r.range(0, 100000);
        sim.dt = r.chance(0.8) ? 1.0f / 60.0f : r.real(0.0f, 0.1f);
        e.m.write32(kSim + 0xdc4, uint32_t(sim.steps));
        e.putf(kSim + 0xdc0, sim.dt);
        g.num_tracers = r.range(0, 64);
        for (int i = 0; i < 64; ++i) {
            drv::RayTracer& t = g.tracers[i];
            random_vec(r, t.from, 3, -300, 300);
            random_vec(r, t.to, 3, -300, 300);
            if (r.chance(0.3)) {  // um alvo perto, para a ponta passar dele
                for (int k = 0; k < 3; ++k) t.to[k] = t.from[k] + r.real(-3, 3);
            }
            t.step = sim.steps - r.range(-2, 80);
            t.type = r.range(0, 6);
            g.tracer_side[i][0] = r.chance(0.3) ? r.next() | 1 : 0;
            g.tracer_side[i][1] = r.next() & 0xff;
        }
        hands.visible = r.chance(0.5) ? 1 : 0;
        e.m.write32(kHandsObj + 0x164, uint32_t(hands.visible));
        drv::hands_globals().hands = nf::p32_u(&hands);
        put(e.m, kNumTracers, &g.num_tracers, 4);
        put(e.m, kTracers, g.tracers, sizeof g.tracers);
        put(e.m, kTracerSide, g.tracer_side, sizeof g.tracer_side);
        call_f(e, kFnDrawTracers, {});
        drv::RayShell_DrawTracers(s, eye);
        ++tt.cases;
        std::string why;
        if (take_error(e, tt, "DrawTracers")) continue;
        if (!same_words("fNumTracers", e.m.mem(kNumTracers, 4), &g.num_tracers, 4, why) ||
            !same_words("fTracer", e.m.mem(kTracers, sizeof g.tracers), g.tracers, uint32_t(g.num_tracers) * 0x20, why) ||
            !same_words("os lados (0x424180)", e.m.mem(kTracerSide, sizeof g.tracer_side), g.tracer_side,
                        uint32_t(g.num_tracers) * 8, why) ||
            !compare_streak(e, s, why))
            tt.fail(why);
    }
    report("tiros: RayShell::DrawTracers", tt, failures);
    // O DrawFlashes.
    static drv::RLightManagerPositional lm;
    for (int c = 0; c < std::max(100, cases / 4); ++c) {
        lm = drv::RLightManagerPositional();
        std::memset(e.m.mem(kLm, 0x400), 0, 0x400);
        e.m.write32(kLm + 0x2a0, kLmBuild);
        e.m.write32(kLm + 0x2a4, kLmPub);
        std::memset(e.m.mem(kLmBuild, 0x100), 0, 0x100);
        e.m.write32(kColorize + 0x30, 0);
        lm.count = r.range(0, 5);
        e.m.write32(kLm + 0x64, uint32_t(lm.count));
        g.num_flashes = r.range(0, 8);
        for (auto& f : g.flashes) {
            random_vec(r, f.pos, 3, -300, 300);
            f.sig = r.next();
        }
        put(e.m, kNumFlashes, &g.num_flashes, 4);
        put(e.m, kFlashes, g.flashes, sizeof g.flashes);
        call_f(e, kFnDrawFlashes, {});
        drv::RayShell_DrawFlashes(lm);
        ++tf.cases;
        std::string why;
        if (take_error(e, tf, "DrawFlashes")) continue;
        const int32_t c64 = int32_t(e.m.read32(kLm + 0x64));
        if (!same_words("fNumFlashes", e.m.mem(kNumFlashes, 4), &g.num_flashes, 4, why) ||
            !same_words("fFlash", e.m.mem(kFlashes, sizeof g.flashes), g.flashes, sizeof g.flashes, why) ||
            !same_words("o bloco em construção", e.m.mem(kLmBuild, 0x80), &lm.building, 0x80, why))
            tf.fail(why);
        else if (c64 != lm.count)
            tf.fail("a contagem +0x64: original " + std::to_string(c64) + ", porte " + std::to_string(lm.count));
    }
    report("tiros: RayShell::DrawFlashes (AddPositionalLight)", tf, failures);
    drv::hands_globals().hands = saved_hands;
    sim.steps = saved_steps;
    sim.dt = saved_dt;
    g = drv::RayShellGlobals();
}

// ------------------------------------------------------------------------------------------- d) os clarões

bool compare_glares(Emu& e, const drv::RGlareManager& m, std::string& why, bool types = false) {
    const int32_t n = int32_t(e.m.read32(kGlareMgr + 0x2020));
    if (n != m.count) {
        why = "a contagem: original " + std::to_string(n) + ", porte " + std::to_string(m.count);
        return false;
    }
    if (!same_words("os clarões", e.m.mem(kGlareMgr + 0x20, 0x2000), m.glares, uint32_t(std::min(n + 1, 0x80)) * 0x40, why))
        return false;
    return !types || same_words("os tipos", e.m.mem(kGlareMgr + 0x2038, 0x1400), m.types, 0x1400, why);
}

void load_glares(Emu& e, const drv::RGlareManager& m) { put(e.m, kGlareMgr, &m, sizeof m); }

drv::GlareData random_glare(Rng& r) {
    drv::GlareData g{};
    random_vec(r, g.pos, 4, -200, 200);
    g.flags = r.next() & 0x4c35u;
    g.kind = uint8_t(r.range(0, 8));
    g.type = r.range(0, 0x4f);
    g.intensity = r.chance(0.1) ? 0.0f : r.real(0.0f, 3.0f);
    random_unit(r, g.dir);
    g.blink_start = uint32_t(r.range(0, 5000));
    g.blink_rate = r.real(0.0f, 0.2f);
    g.blink_duty = r.real(0.0f, 1.0f);
    g.blink_base = r.real(0.0f, 1.0f);
    g.blink_amp = r.real(-1.0f, 1.0f);
    return g;
}

void random_glare_manager(Rng& r, drv::RGlareManager& m) {
    std::memset(&m, 0, sizeof m);
    m.vtable = 0x394f68;
    m.enabled = r.chance(0.9) ? 1 : 0;
    m.count = r.chance(0.05) ? 0x80 : r.range(0, 0x7f);
    for (auto& g : m.glares) g = random_glare(r);
    for (auto& t : m.types) {
        random_vec(r, t.halo_uv, 4, -2, 2);
        random_vec(r, t.spike_uv, 4, -2, 2);
        t.halo_col = r.next();
        t.spike_col = r.next();
        t.halo_size = r.real(0, 3);
        t.spike_size = r.real(0, 3);
        t.halo_tex = r.range(0, 20);
        t.spike_tex = r.range(0, 20);
        t.zbias = r.real(-1, 3);
    }
}

void check_glares(Emu& e, Rng& r, int cases, int& failures) {
    static drv::RGlareManager m;
    Tally tu, ta, tb, tm, td, ti, tg, tk, tr, te;
    // CreateUVsFromTexIDs
    for (int c = 0; c < std::max(20, cases / 100); ++c) {
        random_glare_manager(r, m);
        load_glares(e, m);
        call_f(e, kFnCreateUVs, {kGlareMgr});
        drv::RGlareManager_CreateUVsFromTexIDs(m);
        ++tu.cases;
        std::string why;
        if (!take_error(e, tu, "CreateUVsFromTexIDs") && !compare_glares(e, m, why, true)) tu.fail(why);
    }
    report("clarões: CreateUVsFromTexIDs", tu, failures);
    // AddGlare e GlareBlinkBrightness
    for (int c = 0; c < cases; ++c) {
        if (c % 100 == 0) random_glare_manager(r, m);
        if (m.count >= 0x80 && r.chance(0.5)) m.count = r.range(0, 0x7f);
        load_glares(e, m);
        const drv::GlareData g = random_glare(r);
        put(e.m, kScratch, &g, sizeof g);
        call_f(e, kFnAddGlare, {kGlareMgr, kScratch}, r.real(0, 9));
        drv::RGlareManager_AddGlare(m, g);
        ++ta.cases;
        std::string why;
        if (!take_error(e, ta, "AddGlare") && !compare_glares(e, m, why)) ta.fail(why);
        const uint32_t time = uint32_t(r.chance(0.1) ? r.next() : uint32_t(r.range(0, 20000)));
        e.m.write32(kTime, time);
        call_f(e, kFnBlink, {kScratch});
        const uint32_t emu = e.m.fpr_bits(0);
        const uint32_t port = ps2::fbits(drv::GlareBlinkBrightness(g, time));
        ++tb.cases;
        if (!take_error(e, tb, "GlareBlinkBrightness") && emu != port)
            tb.fail("original " + hex(emu) + ", porte " + hex(port));
    }
    report("clarões: AddGlare", ta, failures);
    report("clarões: GlareBlinkBrightness", tb, failures);
    // AddModelGlare
    for (int c = 0; c < cases; ++c) {
        if (c % 100 == 0) random_glare_manager(r, m);
        if (m.count >= 0x80 && r.chance(0.5)) m.count = r.range(0, 0x7f);
        load_glares(e, m);
        drv::GlareData g = random_glare(r);
        if (r.chance(0.5)) g.flags |= 0x20;
        float mat[4][4], fwd[4];
        random_matrix(r, mat);
        random_unit(r, fwd);
        fwd[3] = r.real(-1, 1);
        put(e.m, kRCamera + 0x20, fwd, 16);
        const float dist2 = r.chance(0.3) ? 0.0f : r.real(0.0f, 10.0f);
        const uint32_t time = uint32_t(r.range(0, 20000));
        e.m.write32(kTime, time);
        put(e.m, kScratch, &g, sizeof g);
        put(e.m, kScratch + 0x40, mat, 64);
        call_f(e, kFnAddModelGlare, {kGlareMgr, kScratch, kScratch + 0x40}, dist2);
        drv::RGlareManager_AddModelGlare(m, dist2, g, mat, fwd, time);
        ++tm.cases;
        std::string why;
        if (!take_error(e, tm, "AddModelGlare") && !compare_glares(e, m, why)) tm.fail(why);
    }
    report("clarões: AddModelGlare (o pisca, as direções)", tm, failures);
    // DrawGlares
    drv::GlareGlobals& gg = drv::glare_globals();
    for (int c = 0; c < std::max(100, cases / 8); ++c) {
        random_glare_manager(r, m);
        if (r.chance(0.3)) m.count = r.range(0, 8);
        load_glares(e, m);
        float eye[4];
        random_vec(r, eye, 4, -200, 200);
        put(e.m, kRCamera + 0x30, eye, 16);
        const float fov = r.real(10.0f, 90.0f);
        e.putf(kRCamera + 0xa4, fov);
        gg.buffer = r.range(0, 1);
        e.m.write32(kGlareBuffer, uint32_t(gg.buffer));
        for (auto& b : gg.coords)
            for (auto& v : b) random_vec(r, v, 4, -9, 9);
        put(e.m, kGlareCoords, gg.coords, sizeof gg.coords);
        std::memset(e.m.mem(kGlareUvuv, sizeof gg.uvuv), 0, sizeof gg.uvuv);
        std::memset(gg.uvuv, 0, sizeof gg.uvuv);
        std::memset(e.m.mem(kGlareColours, sizeof gg.colours), 0, sizeof gg.colours);
        std::memset(gg.colours, 0, sizeof gg.colours);
        std::memset(e.m.mem(kGlareControl, 16), 0, 16);
        std::memset(gg.control, 0, 16);
        e.array_draws.clear();
        call_f(e, kFnDrawGlares, {kGlareMgr});
        drv::RGlareManager_DrawGlares(m, eye, ps2::ee::div(66.0f, ps2::ee::mul(fov, 4.0f)), nullptr);
        ++td.cases;
        std::string why;
        if (take_error(e, td, "DrawGlares")) continue;
        if (!compare_glares(e, m, why) ||
            !same_words("a metade (0x3390a0)", e.m.mem(kGlareBuffer, 4), &gg.buffer, 4, why) ||
            !same_words("as posições (0x3e1f40)", e.m.mem(kGlareCoords, sizeof gg.coords), gg.coords, sizeof gg.coords, why) ||
            !same_words("os UVUV (0x3dff40)", e.m.mem(kGlareUvuv, sizeof gg.uvuv), gg.uvuv, sizeof gg.uvuv, why) ||
            !same_words("as cores (0x3df630)", e.m.mem(kGlareColours, sizeof gg.colours), gg.colours, sizeof gg.colours, why) ||
            !same_words("o Control (0x3e3f40)", e.m.mem(kGlareControl, 16), gg.control, 16, why))
            td.fail(why);
    }
    report("clarões: DrawGlares", td, failures);
    // GLARE_Interpolate e GLARE_Draw (com o AddGlare e o tremor originais)
    drv::GlareJitter& j = drv::glare_jitter();
    for (int c = 0; c < cases; ++c) {
        if (c % 100 == 0) random_glare_manager(r, m);
        if (m.count >= 0x80 && r.chance(0.5)) m.count = r.range(0, 0x7f);
        load_glares(e, m);
        uint32_t a[3] = {uint32_t(r.range(0, 0x4f)), ps2::fbits(r.real(-2, 2)), r.chance(0.5) ? 0u : r.next()};
        uint32_t b[3] = {uint32_t(r.range(0, 0x4f)), ps2::fbits(r.real(-2, 2)), r.next()};
        uint32_t o[3] = {r.next(), r.next(), r.next()};
        const float t = r.real(-0.5f, 1.5f);
        put(e.m, kScratch, a, 12);
        put(e.m, kScratch + 0x10, b, 12);
        put(e.m, kScratch + 0x20, o, 12);
        call_f(e, kFnGlareInterp, {0, kScratch, kScratch + 0x10, kScratch + 0x20}, t);
        drv::GFXGallery_GLARE_Interpolate(t, a, b, o);
        ++ti.cases;
        std::string why;
        if (!take_error(e, ti, "GLARE_Interpolate") && !same_words("a saída", e.m.mem(kScratch + 0x20, 12), o, 12, why))
            ti.fail(why);
        if (c % 50 == 0) {
            j.seed = r.next() & (r.chance(0.5) ? 0xffffu : 0xffffffffu);
            j.mul = r.chance(0.5) ? 0x075bcd15u : r.next();
            e.m.write32(kJitterSeed, j.seed);
            e.m.write32(kJitterMul, j.mul);
        }
        float pos[4];
        random_vec(r, pos, 4, -300, 300);
        put(e.m, kScratch + 0x40, pos, 16);
        put(e.m, kScratch + 0x20, o, 12);
        call_f(e, kFnGlareDraw, {0, 0, kScratch + 0x20, kScratch + 0x40});
        drv::RGlareManager& saved = drv::glare_manager();
        const drv::RGlareManager keep = saved;
        saved = m;
        drv::GFXGallery_GLARE_Draw(o, pos);
        m = saved;
        saved = keep;
        ++tg.cases;
        if (take_error(e, tg, "GLARE_Draw")) continue;
        // (os 64 bytes do registro na pilha: o PS2 copia o lixo em +0x16 e +0x20..+0x3f; só o que o DrawGlares lê conta)
        const int32_t n = int32_t(e.m.read32(kGlareMgr + 0x2020));
        if (n != m.count) {
            tg.fail("a contagem: original " + std::to_string(n) + ", porte " + std::to_string(m.count));
            continue;
        }
        if (n > 0) {
            const uint32_t at = kGlareMgr + 0x20 + uint32_t(n - 1) * 0x40;
            if (!same_words("o clarão novo (+0..+0x15)", e.m.mem(at, 0x14), &m.glares[n - 1], 0x14, why) ||
                e.m.read32(at + 0x14) % 0x10000 != uint32_t(m.glares[n - 1].kind) ||
                !same_words("o clarão novo (+0x18..+0x1f)", e.m.mem(at + 0x18, 8), &m.glares[n - 1].type, 8, why)) {
                tg.fail(why.empty() ? "o tipo do efeito" : why);
                continue;
            }
        }
        if (e.m.read32(kJitterSeed) != j.seed) tg.fail("a semente: original " + hex(e.m.read32(kJitterSeed)) + ", porte " + hex(j.seed));
    }
    report("clarões: GFXGallery::GLARE_Interpolate", ti, failures);
    report("clarões: GFXGallery::GLARE_Draw (o tremor)", tg, failures);
    // GET_Key, GET_Keys e DataIndex
    for (int c = 0; c < cases; ++c) {
        const int count = r.range(0, 12);
        const uint32_t size = uint32_t(r.range(0, 12)) * 4;  // (as chaves são de palavras: o passo é múltiplo de 4)
        std::vector<uint8_t> keys(0x1000);
        for (auto& b : keys) b = uint8_t(r.next());
        const uint32_t stride = (size + 0x30) > 0x3f ? size + 0x30 : 0x40;
        int32_t t = 0;
        for (int k = 0; k < count; ++k) {
            t += r.range(0, 30);
            std::memcpy(&keys[size_t(k) * stride + 0xc], &t, 4);
        }
        keys[0x21] = uint8_t(size);
        put(e.m, kKeys, keys.data(), uint32_t(keys.size()));
        const int32_t frame = r.chance(0.1) ? 0 : r.range(-5, t + 10);
        call_f(e, kFnGetKeys, {0, kKeys, uint32_t(frame), uint32_t(count), kScratch, kScratch + 4, kScratch + 8, kScratch + 12});
        const uint8_t *k0, *k1;
        int32_t t0, t1;
        drv::GFXGallery_GET_Keys(keys.data(), frame, count, k0, t0, k1, t1);
        ++tk.cases;
        if (take_error(e, tk, "GET_Keys")) continue;
        const uint32_t want[4] = {kKeys + uint32_t(k0 - keys.data()), uint32_t(t0), kKeys + uint32_t(k1 - keys.data()),
                                  uint32_t(t1)};
        std::string why;
        if (!same_words("as chaves", e.m.mem(kScratch, 16), want, 16, why)) tk.fail(why);
        const int32_t i = r.range(0, 20);
        const uint32_t emu = call_f(e, kFnGetKey, {0, kKeys, uint32_t(i)});
        if (emu != kKeys + uint32_t(drv::GFXGallery_GET_Key(keys.data(), i) - keys.data())) tk.fail("GET_Key " + std::to_string(i));
        const uint32_t f = r.next() & 0xff, x = r.next() & 0xfff;
        if (call_f(e, kFnDataIndex, {0, f, x}) != drv::GFXGallery_DataIndex(f, x)) tk.fail("DataIndex");
    }
    report("clarões: GET_Key, GET_Keys, DataIndex", tk, failures);
    // GSystem::RANDOM_Startup
    for (int c = 0; c < std::max(20, cases / 100); ++c) {
        const uint32_t n = c == 0 ? 0x642f2bu : r.next();
        call_f(e, kFnRandomStartup, {n});
        drv::GSystem_RANDOM_Startup(n);
        ++tr.cases;
        if (!take_error(e, tr, "RANDOM_Startup") &&
            (e.m.read32(kJitterSeed) != j.seed || e.m.read32(kJitterMul) != j.mul))
            tr.fail(hex(n) + ": original " + hex(e.m.read32(kJitterSeed)) + "/" + hex(e.m.read32(kJitterMul)) + ", porte " +
                    hex(j.seed) + "/" + hex(j.mul));
    }
    report("clarões: GSystem::RANDOM_Startup", tr, failures);
    // DrawGroupDrawEffects (com o AddModelGlare original)
    for (int c = 0; c < std::max(100, cases / 4); ++c) {
        random_glare_manager(r, m);
        if (r.chance(0.5)) m.count = r.range(0, 0x40);
        load_glares(e, m);
        gg.enabled = r.chance(0.8) ? 1 : 0;
        e.m.write32(kGlareEnabled, uint32_t(gg.enabled));
        const int n = r.range(1, 12);
        std::vector<drv::GlareData> fx(size_t(n) + 1);
        for (int k = 0; k < n; ++k) {
            fx[size_t(k)] = random_glare(r);
            fx[size_t(k)].kind = uint8_t(r.chance(0.6) ? 2 : r.range(1, 8));
            if (r.chance(0.7)) fx[size_t(k)].flags |= 1;
        }
        fx[size_t(n)] = drv::GlareData{};
        put(e.m, kEffects, fx.data(), uint32_t(fx.size() * sizeof(drv::GlareData)));
        e.m.write32(kScratch + 0x104, kEffects);
        float mat[4][4], fwd[4];
        random_matrix(r, mat);
        random_unit(r, fwd);
        fwd[3] = 0;
        put(e.m, kRCamera + 0x20, fwd, 16);
        put(e.m, kScratch + 0x200, mat, 64);
        const float dist = r.chance(0.5) ? 0.0f : r.real(0, 4);
        const uint32_t time = uint32_t(r.range(0, 20000));
        e.m.write32(kTime, time);
        call_f(e, kFnDrawGroupEffects, {kScratch + 0x100, kScratch + 0x200}, dist);
        drv::RGlareManager& saved = drv::glare_manager();
        const drv::RGlareManager keep = saved;
        saved = m;
        drv::DrawGroupDrawEffects(dist, fx.data(), mat, fwd, time);
        m = saved;
        saved = keep;
        ++te.cases;
        std::string why;
        if (!take_error(e, te, "DrawGroupDrawEffects") && !compare_glares(e, m, why)) te.fail(why);
    }
    gg.enabled = 1;
    report("clarões: DrawGroupDrawEffects", te, failures);
}

// ------------------------------------------------------------------------------------------- e) os destroços

constexpr uint32_t kFnDebrisCtor = 0x1d94a8, kFnDebrisDraw = 0x1d9560, kFnDebrisReset = 0x1d95c0;
constexpr uint32_t kFnDebrisAdd = 0x1d95e8, kFnDebrisTypeAdd = 0x1d9bb8, kFnDebrisTypeDraw = 0x1d9920;
constexpr uint32_t kFnDebrisPrep = 0x1d93e8, kFnDebrisGallery = 0x235810, kFnFastAlloc = 0x2cfda8;
constexpr uint32_t kFnDynModelCtor = 0x286018, kFnAddGeoPrim = 0x2851f8, kFnSymbol = 0x29fd38;
constexpr uint32_t kDebrisPtr = 0x334a00, kEaglMallocPtr = 0x3485d4, kRandSeed = 0x333e60, kRandMul = 0x333e64;
constexpr uint32_t kTimeData = 0x334000;
constexpr uint32_t kFnEaglMalloc = 0x01fffff0;  // um endereço fora do código: o gancho do EAGLMalloc

// Os 23 tipos do emulador (os ponteiros do RDebris) contra os do porte.
bool compare_debris(Emu& e, const drv::RDebris& d, std::string& why) {
    const uint32_t rd = e.m.read32(kDebrisPtr);
    for (int i = 0; i < drv::kDebrisTypes; ++i) {
        const uint32_t t = e.m.read32(rd + uint32_t(i) * 4);
        const drv::RDebrisTypeData& p = d.types[i].d;
        const uint32_t n = std::min<uint32_t>(p.count, 64);
        char w[64];
        std::snprintf(w, sizeof w, "tipo %d", i);
        if (!same_words((std::string(w) + " (+0xd00..+0xd1c)").c_str(), e.m.mem(t + 0xd00, 0x20), &p.control, 0x20, why) ||
            !same_words((std::string(w) + " posições").c_str(), e.m.mem(t, n * 16), p.offsets, n * 16, why) ||
            !same_words((std::string(w) + " velocidades").c_str(), e.m.mem(t + 0x400, n * 16), p.velocity, n * 16, why) ||
            !same_words((std::string(w) + " quatérnios").c_str(), e.m.mem(t + 0x800, n * 16), p.quats, n * 16, why) ||
            !same_words((std::string(w) + " cores").c_str(), e.m.mem(t + 0xc00, n * 4), p.colours, n * 4, why))
            return false;
    }
    return true;
}

// Os dados do porte para o emulador (os 23 tipos).
void load_debris(Emu& e, const drv::RDebris& d) {
    const uint32_t rd = e.m.read32(kDebrisPtr);
    for (int i = 0; i < drv::kDebrisTypes; ++i)
        put(e.m, e.m.read32(rd + uint32_t(i) * 4), &d.types[i].d, sizeof(drv::RDebrisTypeData));
}

void random_debris(Rng& r, drv::RDebris& d) {
    for (auto& t : d.types) {
        t.d.count = uint32_t(r.chance(0.1) ? 64 : r.range(0, 63));
        for (int k = 0; k < 64; ++k) {
            random_vec(r, t.d.offsets[k], 4, -100, 100);
            random_vec(r, t.d.velocity[k], 3, -2, 2);
            t.d.velocity[k][3] = float(r.range(0, 3000));
            random_vec(r, t.d.quats[k], 4, -1, 1);
            t.d.colours[k] = r.next();
        }
        t.d.life = r.chance(0.8) ? 0x46u : uint32_t(r.range(0, 200));
        t.d.sim_w = r.real(0, 0.01f);
    }
}

void check_debris(Emu& e, Rng& r, int cases, int& failures) {
    drv::RDebris& d = drv::debris();  // (o DEBRIS_Draw do porte usa o global)
    Tally tc, ta, td, tg, tp;
    // O construtor (com os ganchos do EAGL): os estados e o 0x001d93e8 sobre os modelos do .data.
    std::vector<drv::DebrisGeometry> before;
    {
        auto read = [&](uint32_t addr, void* out, uint32_t n) {
            std::memcpy(out, e.m.mem(addr, n), n);
            return true;
        };
        if (!drv::debris_load_geometry([&](uint32_t a, void* o, uint32_t n) { return read(a, o, n); }, before))
            tc.fail("os modelos do .data");
        // (o debris_load_geometry já aplicou o 0x001d93e8; o emulador o aplica no construtor)
    }
    e.m.hook(kFnFastAlloc, "UMemory::FastAlloc", [&e](MipsMachine& mm) { mm.set_gpr(R::v0, e.alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u))); });
    e.m.hook(kFnEaglMalloc, "EAGLMalloc", [&e](MipsMachine& mm) { mm.set_gpr(R::v0, e.alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u))); });
    e.m.write32(kEaglMallocPtr, kFnEaglMalloc);
    e.m.hook(kFnDynModelCtor, "EAGL::DynamicModel::DynamicModel", [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); });
    e.m.hook(kFnAddGeoPrim, "EAGL::DynamicModel::AddGeoPrim", [](MipsMachine&) {});
    e.m.hook(kFnSymbol, "(a busca de símbolo do EAGL)", [](MipsMachine& mm) { mm.set_gpr(R::v0, kFake + 0x41f00); });
    std::memset(e.m.mem(kTextures, 0x400), 0, 0x400);
    const uint32_t rd = e.alloc(0x60);
    e.m.write32(kDebrisPtr, rd);
    call_f(e, kFnDebrisCtor, {rd});
    drv::RDebris_Construct(d);
    ++tc.cases;
    if (!take_error(e, tc, "RDebris::RDebris")) {
        std::string why;
        for (int i = 0; i < drv::kDebrisTypes && why.empty(); ++i) {
            const uint32_t t = e.m.read32(rd + uint32_t(i) * 4);
            if (!same_words(("o GeoPrimState do tipo " + std::to_string(i)).c_str(), e.m.mem(t + 0xd20, 0x80),
                            d.types[i].state, 0x80, why))
                tc.fail(why);
            else if (e.m.read32(t + 0xd18) != 0)
                tc.fail("a contagem do tipo " + std::to_string(i));
            // A camada: o modelo (+0x24 a contagem, +0x28 as posições, +0x30 o UV) contra o porte lido do .data.
            const uint32_t l = e.m.read32(t + 0xda0);
            const drv::DebrisGeometry& g = before[size_t(i)];
            if (int32_t(e.m.read32(l + 0x24)) != g.count ||
                std::memcmp(e.m.mem(e.m.read32(l + 0x28), uint32_t(g.count) * 16), g.coords.data(), size_t(g.count) * 16) ||
                std::memcmp(e.m.mem(e.m.read32(l + 0x30), uint32_t(g.count) * 8), g.uv.data(), size_t(g.count) * 8))
                tc.fail("o modelo do tipo " + std::to_string(i) + " (a camada +0x24/+0x28/+0x30, o 0x001d93e8)");
        }
    }
    report("destroços: RDebris::RDebris (os 23 tipos, o 0x001d93e8)", tc, failures);
    // RDebris::Add (o RDebrisType::Add: o RRandom do render) e o Reset.
    drv::RRandom& rr = drv::render_random();
    for (int c = 0; c < cases; ++c) {
        if (c % 200 == 0) {
            random_debris(r, d);
            load_debris(e, d);
        }
        if (r.chance(0.01)) {
            call_f(e, kFnDebrisReset, {rd});
            drv::RDebris_Reset(d);
        }
        rr.seed = r.next() & (r.chance(0.5) ? 0xffffu : 0xffffffffu);
        rr.multiplier = r.chance(0.7) ? 0x075bcd15u : r.next();
        e.m.write32(kRandSeed, rr.seed);
        e.m.write32(kRandMul, rr.multiplier);
        float pos[4], vel[4];
        random_vec(r, pos, 4, -100, 100);
        random_vec(r, vel, 4, -2, 2);
        const int32_t type = r.range(0, 22);
        const uint32_t colour = r.next(), enable = r.chance(0.9) ? 1u : 0u;
        const uint32_t time = r.chance(0.05) ? r.next() : uint32_t(r.range(0, 100000));
        const float sim_w = r.real(0, 0.01f);
        e.m.write32(kTime, time);
        put(e.m, kScratch, pos, 16);
        put(e.m, kScratch + 0x10, vel, 16);
        call_f(e, kFnDebrisAdd, {rd, uint32_t(type), kScratch, kScratch + 0x10, colour, enable}, sim_w);
        drv::RDebris_Add(d, sim_w, type, pos, vel, colour, enable, time);
        ++ta.cases;
        std::string why;
        if (take_error(e, ta, "RDebris::Add")) continue;
        if (!compare_debris(e, d, why)) ta.fail(why);
        else if (e.m.read32(kRandSeed) != rr.seed) ta.fail("o RRandom: original " + hex(e.m.read32(kRandSeed)) + ", porte " + hex(rr.seed));
    }
    report("destroços: RDebris::Add (RDebrisType::Add) e Reset", ta, failures);
    // RDebris::Draw (o RDebrisType::Draw de cada tipo menos o 4: o EE e o que vai à camada).
    for (int c = 0; c < std::max(100, cases / 8); ++c) {
        random_debris(r, d);
        load_debris(e, d);
        float td4[4] = {float(r.range(0, 4000)), -0.002f, r.real(-1, 1), r.real(-1, 1)};
        put(e.m, kTimeData, td4, 16);
        e.layer_draws.clear();
        std::vector<drv::DebrisDraw> out;
        call_f(e, kFnDebrisDraw, {rd});
        drv::RDebris_Draw(d, td4, &out);
        ++td.cases;
        std::string why;
        if (take_error(e, td, "RDebris::Draw")) continue;
        if (!compare_debris(e, d, why)) {
            td.fail(why);
            continue;
        }
        if (e.layer_draws.size() != out.size()) {
            td.fail("desenhos: original " + std::to_string(e.layer_draws.size()) + ", porte " + std::to_string(out.size()));
            continue;
        }
        // O w do fTimeData (0x33400c) ficou com o sim_w do último tipo desenhado.
        if (!out.empty() && e.m.read32(kTimeData + 0xc) != ps2::fbits(out.back().simtick[3]))
            td.fail("o w do fTimeData: original " + hex(e.m.read32(kTimeData + 0xc)));
    }
    report("destroços: RDebris::Draw (RDebrisType::Draw)", td, failures);
    // GFXGallery::DEBRIS_Draw (com o RDebris::Add, o gerador do tremor e o RRandom originais).
    drv::GlareJitter& j = drv::glare_jitter();
    for (int c = 0; c < cases; ++c) {
        if (c % 100 == 0) {
            random_debris(r, d);
            for (auto& t : d.types) t.d.count = uint32_t(r.range(0, 20));
            load_debris(e, d);
        }
        std::vector<uint8_t> key(0x80);
        for (auto& b : key) b = uint8_t(r.next());
        float q[4];
        random_unit(r, q);
        q[3] = r.real(-1, 1);
        const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (float& x : q) x /= l;
        std::memcpy(key.data() + 0x10, q, 16);
        auto setf = [&](uint32_t o, float v) { std::memcpy(key.data() + 0x30 + o, &v, 4); };
        auto setu = [&](uint32_t o, uint32_t v) { std::memcpy(key.data() + 0x30 + o, &v, 4); };
        setf(0x00, r.chance(0.2) ? r.real(15, 20) : r.real(0, 8));
        setf(0x04, r.real(-1, 1));
        setf(0x08, r.real(0, 2));
        setf(0x0c, r.real(0, 2));
        setu(0x10, r.next());
        setu(0x14, r.chance(0.2) ? 0u : uint32_t(r.range(1, 0x16)));
        for (uint32_t o : {0x1cu, 0x20u, 0x24u}) setf(o, r.chance(0.5) ? 0.0f : r.real(0, 3));
        setf(0x28, r.real(0, 0.01f));
        setu(0x2c, r.chance(0.9) ? 1u : 0u);
        float pos[4], quat[4], vel[4];
        random_vec(r, pos, 4, -100, 100);
        random_unit(r, quat);
        quat[3] = r.real(-1, 1);
        const float ql = std::sqrt(quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3]);
        for (float& x : quat) x /= ql;
        random_vec(r, vel, 4, -1, 1);
        const bool with_vel = r.chance(0.3);
        j.seed = r.next() & 0xffffu;
        j.mul = r.chance(0.8) ? 0x075bcd15u : r.next();
        e.m.write32(kJitterSeed, j.seed);
        e.m.write32(kJitterMul, j.mul);
        rr.seed = r.next() & 0xffffu;
        rr.multiplier = 0x075bcd15u;
        e.m.write32(kRandSeed, rr.seed);
        e.m.write32(kRandMul, rr.multiplier);
        const uint32_t time = uint32_t(r.range(0, 100000));
        e.m.write32(kTime, time);
        put(e.m, kKeys, key.data(), uint32_t(key.size()));
        put(e.m, kScratch, pos, 16);
        put(e.m, kScratch + 0x10, quat, 16);
        put(e.m, kScratch + 0x20, vel, 16);
        call_f(e, kFnDebrisGallery, {0, kKeys, kScratch, kScratch + 0x10, with_vel ? kScratch + 0x20 : 0u, 0});
        drv::GFXGallery_DEBRIS_Draw(key.data(), pos, quat, with_vel ? vel : nullptr, time);
        ++tg.cases;
        std::string why;
        if (take_error(e, tg, "DEBRIS_Draw")) continue;
        if (!compare_debris(e, d, why)) tg.fail(why);
        else if (e.m.read32(kJitterSeed) != j.seed) tg.fail("a semente do tremor");
        else if (e.m.read32(kRandSeed) != rr.seed) tg.fail("o RRandom");
    }
    report("destroços: GFXGallery::DEBRIS_Draw", tg, failures);
    // O BondDebris do PC: a matriz de um pedaço (bond_debris_instance_matrix) é a do microprograma (o --verify do
    // nf_driving confere os vértices); aqui, a forma do 0x001d93e8 sobre modelos sorteados.
    for (int c = 0; c < std::max(20, cases / 200); ++c) {
        std::vector<drv::DebrisGeometry> g = before;
        float base[26][4], scale = r.real(0, 2);
        for (auto& b : base) random_vec(r, b, 4, -1, 1);
        for (int i = 0; i < 30 && i < g[16].count; ++i) random_vec(r, &g[16].coords[size_t(i) * 4], 4, -1, 1);
        // o emulador: as fontes no .data, o 0x001d93e8, os destinos
        put(e.m, 0x334a10, base, sizeof base);
        e.putf(0x334bb0, scale);
        put(e.m, 0x335f10, g[16].coords.data(), uint32_t(g[16].count) * 16);
        call_f(e, kFnDebrisPrep, {});
        drv::RDebris_PrepareGeometry(g, base, scale);
        ++tp.cases;
        std::string why;
        if (take_error(e, tp, "0x001d93e8")) continue;
        if (!same_words("o tipo 4 (0x336ba0)", e.m.mem(0x336ba0, 26 * 16), g[4].coords.data(), 26 * 16, why) ||
            !same_words("o tipo 22 (0x3360f0)", e.m.mem(0x3360f0, uint32_t(g[22].count) * 16), g[22].coords.data(),
                        uint32_t(g[22].count) * 16, why))
            tp.fail(why);
    }
    report("destroços: 0x001d93e8 (os modelos dos tipos 4 e 22)", tp, failures);
}

// ---- f) o som dos personagens --------------------------------------------------------------------------------------

constexpr uint32_t kFnSay = 0x2fbe00, kFnCharPlay = 0x2fc130, kFnIndexLookup = 0x2f5880, kFnPatPresent = 0x3085d0;
constexpr uint32_t kFnVoiceCtor = 0x2dd790, kFnMixVolume = 0x2f34d0, kFnVoicePlay = 0x2ddfd0;
constexpr uint32_t kFnViewDtor = 0x2de0b8, kFnFastFree = 0x2cfe50;
constexpr uint32_t kActorsBankPtr = 0x3562bc, kViewMask = 0x355cd0;
constexpr uint32_t kSayCounters[9] = {0x356134, 0x356144, 0x356174, 0x356188, 0x3561b0,
                                      0x3561e0, 0x3561e4, 0x3561fc, 0x356200};
constexpr uint32_t kCharBank = kFake + 0x63000, kCharObj = kFake + 0x63100, kCharVoice = kFake + 0x63200;
constexpr uint32_t kCharPath = kFake + 0x63300, kCharListener = kFake + 0x63400, kCharWhat = kFake + 0x63500;

// O AIndex::Lookup e o SNDbankpatpresent dos dois lados: uma regra fixa sobre o nome (alguns não existem, alguns além
// de 0x80) e sobre o timbre.
int32_t char_lookup(const char* name) {
    if (name == nullptr) return -1;
    uint32_t h = 0x811c9dc5u;
    for (const char* p = name; *p; ++p) h = (h ^ uint8_t(*p)) * 0x01000193u;
    if (h % 7 == 0) return -1;
    return int32_t(h % 0x90);
}
int32_t char_patpresent(int32_t bank, int32_t patch) { return (patch * 7 + bank) % 3 != 0 ? 1 : 0; }

void check_acharacter(Emu& e, Rng& r, int cases, int& failures) {
    MipsMachine& m = e.m;
    int32_t snd_bank = 3;
    struct VoiceArgs {
        uint32_t self, mix, bank, patch;
    } voice{};
    m.hook(kFnIndexLookup, "AIndex::Lookup", [](MipsMachine& mm) {
        const uint32_t a = uint32_t(mm.gpr(R::a1));
        const std::string s = cstr(mm, a);
        mm.set_gpr(R::v0, uint32_t(char_lookup(a == 0 ? nullptr : s.c_str())));
    });
    m.hook(kFnPatPresent, "SNDbankpatpresent", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, uint32_t(char_patpresent(int32_t(mm.gpr(R::a0)), int32_t(mm.gpr(R::a1)))));
    });
    m.hook(kFnFastAlloc, "UMemory::FastAlloc", [](MipsMachine& mm) {
        std::memset(mm.mem(kCharVoice, 100), 0xcc, 100);
        mm.set_gpr(R::v0, kCharVoice);
    });
    m.hook(kFnVoiceCtor, "AVoice::AVoice", [&voice](MipsMachine& mm) {
        voice = {uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3))};
        mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0)));
    });
    auto present = [&](const char* name) -> int32_t {
        const int32_t patch = char_lookup(name);
        if (snd_bank < 0 || uint32_t(patch) >= 0x80) return 0;
        return char_patpresent(snd_bank, patch) == 1 ? 1 : 0;
    };
    static const char* const kWhat[] = {"HDeath", "HGrunt", "HFall", "HYell", "HScream", "HX", "M", "MPed", "F", "Ped"};
    Tally ts;
    for (int c = 0; c < cases; ++c) {
        // Os contadores sorteados dentro das rodas, o banco, a voz que ainda toca (às vezes).
        drv::ACharacterSayState& st = drv::acharacter_say_state();
        st.fall = r.range(0, 2);
        st.yell = r.range(0, 2);
        st.death = r.range(0, 10);
        st.scream = r.range(0, 3);
        st.grunt = r.range(0, 7);
        st.mped = r.range(0, 9);
        st.mped_wrapped = r.chance(0.5) ? 1 : 0;
        st.fped = r.range(0, 4);
        st.fped_wrapped = 0;
        snd_bank = r.chance(0.1) ? -1 : r.range(0, 12);
        const int32_t* sv = &st.fall;
        for (int i = 0; i < 9; ++i) m.write32(kSayCounters[i], uint32_t(sv[i]));
        const char* what = kWhat[r.range(0, 9)];
        // A roda das mulheres precisa de um timbre presente (o original ficaria no laço).
        bool fped_ok = false;
        for (const char* n : {"SFX_FPed1a", "SFX_FPed1b", "SFX_FPed1c", "SFX_FPed1d", "SFX_FPed1e"}) fped_ok |= present(n) != 0;
        if (!fped_ok && what[0] != 'H' && what[0] != 'M') what = "HGrunt";
        put(m, kCharWhat, what, uint32_t(std::strlen(what) + 1));
        m.write32(kActorsBankPtr, kCharBank);
        m.write32(kCharBank + 8, uint32_t(snd_bank));
        const uint32_t mix = 0x5000 + uint32_t(r.range(0, 100));
        const bool busy = r.chance(0.1);
        m.write32(kCharObj + 0x84, mix);
        m.write32(kCharObj + 0xb0, busy ? 0x1234u : 0u);
        for (uint32_t o = 0x5c; o < 0x68; o += 4) m.write32(kCharObj + o, 0xdeadu);
        voice = {};
        call_f(e, kFnSay, {kCharObj, kCharWhat});
        ++ts.cases;
        if (take_error(e, ts, std::string("Say(") + what + ")")) continue;
        // O porte (o ACharacter_Say sem o objeto: a escolha, o alcance e a voz).
        float range[3] = {};
        const char* name = nullptr;
        if (!busy) name = drv::ACharacter_PickName(what, present, range);
        std::string why;
        for (int i = 0; i < 9 && why.empty(); ++i)
            if (m.read32(kSayCounters[i]) != uint32_t(sv[i])) why = "o contador " + hex(kSayCounters[i]);
        if (why.empty() && busy) {
            if (voice.self != 0 || m.read32(kCharObj + 0xb0) != 0x1234u || m.read32(kCharObj + 0x5c) != 0xdeadu)
                why = "com a voz tocando";
        } else if (why.empty()) {
            const int32_t patch = char_lookup(name);
            if (voice.self != kCharVoice || voice.mix != mix || voice.bank != uint32_t(snd_bank) ||
                voice.patch != uint32_t(patch))
                why = std::string("a voz de ") + what + " (" + (name ? name : "nulo") + ")";
            else if (m.read32(kCharObj + 0xb0) != kCharVoice)
                why = "o +0xb0";
            else if (m.read32(kCharObj + 0x5c) != ps2::fbits(range[0]) ||
                     m.read32(kCharObj + 0x60) != ps2::fbits(range[1]) ||
                     m.read32(kCharObj + 0x64) != ps2::fbits(range[2]))
                why = "o alcance";
            else if (m.read32(kCharVoice + 0x0c) != 0 || m.read32(kCharVoice + 0x2c) != 0 ||
                     m.read32(kCharVoice + 0x4c) != 0)
                why = "as vistas sem laço";
        }
        if (!why.empty()) ts.fail(why);
    }
    report("o som dos personagens: ACharacter::Say", ts, failures);

    // ACharacter::Play: o volume que vai ao AVoice::Play e o fim da voz pelas vistas em uso.
    struct PlayArgs {
        bool called = false;
        uint32_t vol = 0, pitch = 0, az = 0, delay = 0, fx = 0, voice = 0, view = 0;
    } play;
    int freed = 0;
    float mix_volume = 1.0f;
    m.hook(kFnMixVolume, "AMix::GetVolume", [&mix_volume](MipsMachine& mm) { mm.set_fpr(0, mix_volume); });
    m.hook(kFnVoicePlay, "AVoice::Play", [&play](MipsMachine& mm) {
        play = {true,          mm.fpr_bits(12), mm.fpr_bits(13), mm.fpr_bits(14), mm.fpr_bits(15), mm.fpr_bits(16),
                uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1))};
    });
    m.hook(kFnViewDtor, "AVoice::View::~View", [](MipsMachine&) {});
    m.hook(kFnFastFree, "UMemory::FastFree", [&freed](MipsMachine&) { ++freed; });
    Tally tp;
    for (int c = 0; c < cases; ++c) {
        const bool has_voice = !r.chance(0.1);
        const float volume = r.real(0, 2), path_volume = r.real(0, 1.5f), pitch = r.real(0.5f, 2), az = r.real(-0.5f, 0.5f);
        mix_volume = r.real(0, 1);
        const uint32_t mask = uint32_t(r.range(0, 7)), view = uint32_t(r.range(0, 2));
        int32_t over[3];
        for (int32_t& o : over) o = r.chance(0.5) ? 1 : 0;
        e.putf(kCharObj + 0x6c, volume);
        m.write32(kCharObj + 0xb0, has_voice ? kCharVoice : 0u);
        m.write32(kViewMask, mask);
        for (int k = 0; k < 3; ++k) m.write32(kCharVoice + 0x20 + uint32_t(k) * 0x20, uint32_t(over[k]));
        e.putf(kCharPath, path_volume);
        e.putf(kCharPath + 4, pitch);
        e.putf(kCharPath + 8, az);
        m.write32(kCharPath + 0x18, kCharListener);
        m.write32(kCharListener + 0x5c, view);
        play = {};
        freed = 0;
        call_f(e, kFnCharPlay, {kCharObj, kCharPath});
        ++tp.cases;
        if (take_error(e, tp, "ACharacter::Play")) continue;
        std::string why;
        if (!has_voice) {
            if (play.called || freed) why = "sem voz";
        } else {
            const uint32_t vol = ps2::fbits(drv::ACharacter_PlayVolume(volume, mix_volume, path_volume));
            const bool done = drv::ACharacter_VoiceDone(over, mask);
            if (!play.called || play.vol != vol || play.pitch != ps2::fbits(pitch) || play.az != ps2::fbits(az) ||
                play.delay != 0 || play.fx != 0 || play.voice != kCharVoice || play.view != view)
                why = "o AVoice::Play";
            else if ((freed != 0) != done || (m.read32(kCharObj + 0xb0) == 0) != done)
                why = "o fim da voz (máscara " + std::to_string(mask) + ")";
        }
        if (!why.empty()) tp.fail(why);
    }
    report("o som dos personagens: ACharacter::Play", tp, failures);
    drv::acharacter_say_state() = drv::ACharacterSayState();

    // GFXGallery::SOUND_Play (0x002363b0): a posição, a velocidade, a direção (0, 1, 0); com a chave a mixagem "Giotto",
    // o AVoice::Set com o banco da categoria e o timbre do nome, e os números da chave.
    constexpr uint32_t kFnSoundPlay = 0x2363b0, kFnMixRemove = 0x2f2e18, kFnMixAdd = 0x2f2d50, kFnVoiceSet = 0x2dd698;
    constexpr uint32_t kFgBanks = 0x3562a0, kGsSound = kFake + 0x63600, kGsDesc = kFake + 0x63800;
    constexpr uint32_t kGsPos = kFake + 0x63900, kGsVel = kFake + 0x63920, kGsBanks = kFake + 0x63a00;
    struct GsCalls {
        uint32_t removed = 0, set_self = 0, set_bank = 0, set_patch = 0;
        std::string added;
        int adds = 0, sets = 0;
    } gc;
    m.hook(kFnMixRemove, "AMix::Remove", [&gc](MipsMachine& mm) { gc.removed = uint32_t(mm.gpr(R::a0)); });
    m.hook(kFnMixAdd, "AMix::Add", [&gc](MipsMachine& mm) {
        gc.added = cstr(mm, uint32_t(mm.gpr(R::a0)));
        ++gc.adds;
        mm.set_gpr(R::v0, 0x7000u);
    });
    m.hook(kFnVoiceSet, "AVoice::Set", [&gc](MipsMachine& mm) {
        gc.set_self = uint32_t(mm.gpr(R::a0));
        gc.set_bank = uint32_t(mm.gpr(R::a1));
        gc.set_patch = uint32_t(mm.gpr(R::a2));
        ++gc.sets;
    });
    static const char* const kGsNames[] = {"SFX_lamppost3", "SFX_firehydrantLP", "SFX_GlassShatter2", "SFX_Steam",
                                           "SFX_WaterSpray", "SFX_nope", "SFX_chainching", "SFX_rockslide"};
    Tally tg;
    for (int c = 0; c < cases; ++c) {
        const bool with_desc = r.chance(0.6);
        float pos[3], vel[3];
        random_vec(r, pos, 3, -1000, 1000);
        random_vec(r, vel, 3, -50, 50);
        uint8_t desc[0x38] = {};
        const int32_t cat = r.range(0, 9);
        const int32_t w[4] = {r.range(-50, 300), r.range(-50, 400), r.range(0, 200), r.range(0, 2000)};
        std::memcpy(desc + 0x00, &w[0], 4);
        std::memcpy(desc + 0x08, &cat, 4);
        const char* nm = kGsNames[r.range(0, 7)];
        std::memcpy(desc + 0x0c, nm, std::strlen(nm));
        std::memcpy(desc + 0x2c, &w[1], 4);
        std::memcpy(desc + 0x30, &w[2], 4);
        std::memcpy(desc + 0x34, &w[3], 4);
        put(m, kGsDesc, desc, sizeof desc);
        put(m, kGsPos, pos, 12);
        put(m, kGsVel, vel, 12);
        for (uint32_t i = 0; i < 10; ++i) {
            m.write32(kFgBanks + i * 4, kGsBanks + i * 0x20);
            m.write32(kGsBanks + i * 0x20 + 8, uint32_t(int32_t(i) * 3 - 2));
        }
        std::memset(m.mem(kGsSound, 0x120), 0x5a, 0x120);
        const uint32_t old_mix = 0x6000u + uint32_t(r.range(0, 99));
        m.write32(kGsSound + 0x84, old_mix);
        gc = GsCalls();
        call_f(e, kFnSoundPlay, {0, 0, with_desc ? kGsDesc : 0u, kGsSound, kGsPos, kGsVel, 0});
        ++tg.cases;
        if (take_error(e, tg, "SOUND_Play")) continue;
        std::string why;
        const float dir[3] = {0.0f, 1.0f, 0.0f};
        if (!same_words("a posição", m.mem(kGsSound, 12), pos, 12, why) ||
            !same_words("a velocidade", m.mem(kGsSound + 0x10, 12), vel, 12, why) ||
            !same_words("a direção", m.mem(kGsSound + 0x20, 12), dir, 12, why)) {
        } else if (!with_desc) {
            if (gc.adds || gc.sets || m.read32(kGsSound + 0x84) != old_mix || m.read32(kGsSound + 0x6c) != 0x5a5a5a5au)
                why = "sem a chave";
        } else {
            float f[5];
            drv::GFXGallery_SOUND_Fields(desc, f);
            const uint32_t want[5] = {ps2::fbits(f[0]), ps2::fbits(f[1]), ps2::fbits(f[2]), ps2::fbits(f[3]),
                                      ps2::fbits(f[4])};
            const uint32_t got[5] = {m.read32(kGsSound + 0x6c), m.read32(kGsSound + 0x70), m.read32(kGsSound + 0x5c),
                                     m.read32(kGsSound + 0x60), m.read32(kGsSound + 0x64)};
            if (gc.removed != old_mix || gc.added != "Giotto" || m.read32(kGsSound + 0x84) != 0x7000u)
                why = "a mixagem";
            else if (gc.set_self != kGsSound + 0xb0 || gc.set_bank != uint32_t(cat * 3 - 2) ||
                     gc.set_patch != uint32_t(char_lookup(nm)))
                why = "o AVoice::Set";
            else if (std::memcmp(want, got, sizeof want) != 0)
                why = "o volume, o pitch ou o alcance";
        }
        if (!why.empty()) tg.fail(why);
    }
    report("os sons da galeria: GFXGallery::SOUND_Play", tg, failures);

    // Os outros carros: o ATrafficVehicle::Play (0x002e5598) depois do AVehicle::Play (enganchado), com o
    // AVehicle::CalculateRpm (0x002df8a0) e o IsTracked originais pela tabela virtual do ATrafficVehicle (0x003c2e38), as
    // três vozes ao AVoice::Play e a sirene (um ABasic falso com o Play e o Stop enganchados); o AHorn::Play (0x002dea48)
    // com o random original sobre as seis palavras do port.
    constexpr uint32_t kFnTrafficPlay = 0x2e5598, kFnVehiclePlay = 0x2e1768, kFnSnowmobile = 0x2e5968;
    constexpr uint32_t kFnBasicPlay = 0x2fc700, kFnBasicStop = 0x2fc7c8, kFnHornPlay = 0x2dea48;
    constexpr uint32_t kTrafficVt = 0x3c2e38, kSoundCount = 0x355e60, kRandom = 0x343810;
    constexpr uint32_t kTv = kFake + 0x64000, kTvPath = kFake + 0x64400, kTvList = kFake + 0x64500;
    constexpr uint32_t kSiren = kFake + 0x64600, kSirenVt = kFake + 0x64800, kHorn = kFake + 0x64900;
    struct VoiceCall {
        uint32_t vol, pitch, az, delay, fx, voice, view;
    };
    std::vector<VoiceCall> calls;
    int siren_plays = 0, siren_stops = 0, snow = 0, base_plays = 0;
    m.hook(kFnVoicePlay, "AVoice::Play", [&calls](MipsMachine& mm) {
        calls.push_back({mm.fpr_bits(12), mm.fpr_bits(13), mm.fpr_bits(14), mm.fpr_bits(15), mm.fpr_bits(16),
                         uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1))});
    });
    m.hook(kFnMixVolume, "AMix::GetVolume", [&mix_volume](MipsMachine& mm) { mm.set_fpr(0, mix_volume); });
    m.hook(kFnVehiclePlay, "AVehicle::Play", [&base_plays](MipsMachine&) { ++base_plays; });
    m.hook(kFnSnowmobile, "ATrafficVehicle::PlaySnowmobileSpecifics", [&snow](MipsMachine&) { ++snow; });
    m.hook(kFnBasicPlay, "ABasic::Play", [&siren_plays](MipsMachine&) { ++siren_plays; });
    m.hook(kFnBasicStop, "ABasic::Stop", [&siren_stops](MipsMachine&) { ++siren_stops; });
    // A tabela virtual falsa da sirene: os pares (deslocamento, função) do GCC 2.95 com o Play e o Stop.
    for (uint32_t i = 0; i < 0x40; i += 4) m.write32(kSirenVt + i, 0);
    m.write32(kSirenVt + 0x24, kFnBasicPlay);
    m.write32(kSirenVt + 0x2c, kFnBasicStop);
    m.write32(kSiren + 0xac, kSirenVt);
    Tally tt, tr;
    for (int c = 0; c < cases; ++c) {
        drv::TrafficPlayFields f;
        f.surface2 = r.chance(0.15) ? 0 : r.range(1, 12);
        f.surface3 = r.chance(0.15) ? 0 : r.range(1, 12);
        f.alive = r.chance(0.9) ? 1 : 0;
        f.gas = r.chance(0.3) ? 0.0f : r.real(-0.2f, 1.0f);
        f.volume = r.real(0, 1.5f);
        f.speed = r.chance(0.2) ? r.real(0, 2.5f) : r.real(0, 140);
        f.smooth_rpm = r.real(0, 9000);
        f.load = r.real(0, 1);
        f.pitch_offset = r.real(0, 0.2f);
        int32_t vtype = r.chance(0.1) ? 5 : r.range(0, 4);
        // (o tipo 5 acima de 1 m/s usa o logf da libm do PS2: a divergência anotada, fora da conferência)
        if (vtype == 5 && ps2::ee::lt(1.0f, f.speed)) vtype = 4;
        const bool horn = r.chance(0.5);
        const float rpm = r.real(0, 8000), wobble = r.real(-150, 150);
        const int32_t count = r.range(1, 3);
        const bool siren = r.chance(0.3);
        const int32_t siren_on = r.chance(0.5) ? 1 : 0;
        mix_volume = r.real(0, 1);
        drv::APath path;
        path.volume = r.real(0, 1);
        path.pitch = r.real(0.5f, 1.5f);
        path.azimuth = r.real(-0.5f, 0.5f);
        path.facing = r.real(-1, 1);
        const int32_t view = r.range(0, 2);
        std::memset(m.mem(kTv, 0x270), 0, 0x270);
        m.write32(kTv + 0xac, kTrafficVt);
        m.write32(kTv + 0x84, 0x7100u);
        e.putf(kTv + 0x6c, f.volume);
        m.write32(kTv + 0xd0, uint32_t(vtype));
        m.write32(kTv + 0xdc, uint32_t(f.surface2));
        m.write32(kTv + 0xe0, uint32_t(f.surface3));
        e.putf(kTv + 0x118, f.gas);
        e.putf(kTv + 0x11c, rpm);
        e.putf(kTv + 0x120, wobble);
        e.putf(kTv + 0x13c, f.speed);
        m.write32(kTv + 0x164, uint32_t(f.alive));
        m.write32(kTv + 0x1f8, horn ? kHorn : 0u);
        m.write32(kTv + 0x220, 0x7200u);
        m.write32(kTv + 0x224, 0x7300u);
        m.write32(kTv + 0x228, 0x7400u);
        m.write32(kTv + 0x22c, siren ? kSiren : 0u);
        e.putf(kTv + 0x230, f.smooth_rpm);
        e.putf(kTv + 0x234, f.load);
        e.putf(kTv + 0x238, f.pitch_offset);
        m.write32(kTv + 0x244, uint32_t(siren_on));
        m.write32(kSoundCount, uint32_t(count));
        e.putf(kTvPath + 0x00, path.volume);
        e.putf(kTvPath + 0x04, path.pitch);
        e.putf(kTvPath + 0x08, path.azimuth);
        e.putf(kTvPath + 0x0c, path.facing);
        m.write32(kTvPath + 0x18, kTvList);
        m.write32(kTvList + 0x5c, uint32_t(view));
        calls.clear();
        siren_plays = siren_stops = snow = base_plays = 0;
        call_f(e, kFnTrafficPlay, {kTv, kTvPath});
        ++tt.cases;
        if (take_error(e, tt, "ATrafficVehicle::Play")) continue;
        // O porte.
        const float rpm_calc = drv::AVehicle_CalculateRpmValues(vtype == 5, horn, rpm, wobble, f.speed);
        drv::TrafficPlayVoices out;
        drv::ATrafficVehicle_PlayOwn(f, ps2::ee::cvt_s(count), rpm_calc, mix_volume, path, out);
        std::string why;
        if (base_plays != 1 || calls.size() != 3) {
            why = "as chamadas";
        } else {
            const uint32_t voices[3] = {0x7200u, 0x7300u, 0x7400u};
            for (int i = 0; i < 3 && why.empty(); ++i) {
                const VoiceCall& vc = calls[size_t(i)];
                if (vc.vol != ps2::fbits(out.vol[i]) || vc.pitch != ps2::fbits(out.pitch[i]) ||
                    vc.az != ps2::fbits(path.azimuth) || vc.delay != 0 || vc.fx != 0 || vc.voice != voices[i] ||
                    vc.view != uint32_t(view))
                    why = "a voz " + std::to_string(i);
            }
            if (why.empty() && (m.read32(kTv + 0x230) != ps2::fbits(f.smooth_rpm) ||
                                m.read32(kTv + 0x234) != ps2::fbits(f.load)))
                why = "as rotações ou a carga";
            if (why.empty() && (snow != (vtype == 5 ? 1 : 0)))
                why = "o trenó";
            if (why.empty() && siren && (siren_plays != siren_on || siren_stops != 1 - siren_on))
                why = "a sirene";
            if (why.empty() && !siren && (siren_plays || siren_stops))
                why = "a sirene sem sirene";
        }
        if (!why.empty()) tt.fail(why);
    }
    report("os outros carros: ATrafficVehicle::Play (CalculateRpm)", tt, failures);

    // AHorn::Play: o passo da buzina e o volume.
    drv::AudioInternals ai = drv::audio_internals();
    for (int c = 0; c < cases; ++c) {
        int32_t honks = r.range(0, 5), ticks = r.chance(0.4) ? 0 : r.range(1, 70), wait = r.chance(0.5) ? 0 : r.range(1, 300);
        const int32_t request = r.chance(0.6) ? 1 : 0;
        const float vol = r.real(0, 1), pitch = r.real(0.5f, 1.5f);
        mix_volume = r.real(0, 1);
        drv::APath path;
        path.volume = r.real(0, 1);
        path.pitch = r.real(0.5f, 1.5f);
        path.azimuth = r.real(-0.5f, 0.5f);
        const int32_t view = r.range(0, 2);
        for (int i = 0; i < 6; ++i) ai.random[i] = r.next() | (r.chance(0.01) ? 0xffffffffu : 0u);
        put(m, kRandom, ai.random, 24);
        e.putf(kHorn + 0, vol);
        e.putf(kHorn + 4, pitch);
        m.write32(kHorn + 8, 0x7500u);
        m.write32(kHorn + 0x70, uint32_t(honks));
        m.write32(kHorn + 0x74, uint32_t(ticks));
        m.write32(kHorn + 0x78, uint32_t(wait));
        e.putf(kTvPath + 0x00, path.volume);
        e.putf(kTvPath + 0x04, path.pitch);
        e.putf(kTvPath + 0x08, path.azimuth);
        m.write32(kTvPath + 0x18, kTvList);
        m.write32(kTvList + 0x5c, uint32_t(view));
        calls.clear();
        call_f(e, kFnHornPlay, {kHorn, uint32_t(request), kTvPath});
        ++tr.cases;
        if (take_error(e, tr, "AHorn::Play")) continue;
        drv::AHorn_Step(honks, ticks, wait, request);
        const float v = ticks == 0 ? 0.0f : ps2::ee::mul(mix_volume, vol);
        std::string why;
        if (m.read32(kHorn + 0x70) != uint32_t(honks) || m.read32(kHorn + 0x74) != uint32_t(ticks) ||
            m.read32(kHorn + 0x78) != uint32_t(wait))
            why = "os contadores";
        else if (std::memcmp(m.mem(kRandom, 24), ai.random, 24) != 0)
            why = "o random";
        else if (calls.size() != 1 || calls[0].vol != ps2::fbits(ps2::ee::mul(v, path.volume)) ||
                 calls[0].pitch != ps2::fbits(ps2::ee::mul(pitch, path.pitch)) || calls[0].voice != kHorn + 0xc ||
                 calls[0].view != uint32_t(view))
            why = "o AVoice::Play";
        if (!why.empty()) tr.fail(why);
    }
    report("os outros carros: AHorn::Play (o random da libc)", tr, failures);

    // PBondCar::InitAudioObject (0x00186878) das classes 1 a 5: as buscas (AttributeSet::Lookup* enganchados com os
    // mesmos valores dos dois lados), o SimRandom das classes 2 e 4 (o gerador do port nos dois lados, recomeçado), a
    // mixagem e o motor do ATrafficVehicle (ou o AHelicopter), a afinação, o volume, o alcance, a sirene, a buzina.
    constexpr uint32_t kFnIao = 0x186878, kFnLookupStr = 0x169f18, kFnLookupFloat = 0x169d80, kFnLookupInt = 0x169bf0;
    constexpr uint32_t kFnStrcat = 0x2a924c, kFnStrcasecmp = 0x2a9198, kFnSoundNew = 0x2fca38, kFnTrafficCtor = 0x2e5340;
    constexpr uint32_t kFnHeliCtor = 0x2f6718, kFnSimRandom = 0x17dc28, kFnSiren = 0x2e58f8, kFnChooseHorn = 0x2df538;
    constexpr uint32_t kFnSetAudio = 0x1959d8;
    constexpr uint32_t kCar = kFake + 0x65000, kStrings = kFake + 0x66000, kObj = kFake + 0x67000;
    struct IaoCase {
        std::string engine, horn;
        float volume = 0, skid = 0, horn_pitch = 0;
        int32_t siren = 0, heli = 0, flying = 0;
    } ic;
    struct IaoCalls {
        int news = 0, draws = 0, sirens = 0, horns = 0, set_audio = 0, heli = 0, traffic = 0;
        std::string engine, mix, siren_patch, horn_name;
        uint32_t horn_vol = 0, horn_pitch = 0;
    } ia;
    uint32_t string_at = 0;
    m.hook(kFnLookupStr, "AttributeSet::LookupString", [&ic, &string_at](MipsMachine& mm) {
        const std::string k = cstr(mm, uint32_t(mm.gpr(R::a1)));
        const std::string& v = k == "SFX_ENGINE" ? ic.engine : ic.horn;
        const uint32_t a = kStrings + string_at;
        string_at = (string_at + 0x40) & 0x3ff;
        put(mm, a, v.c_str(), uint32_t(v.size() + 1));
        mm.set_gpr(R::v0, a);
    });
    m.hook(kFnLookupFloat, "AttributeSet::LookupFloat", [&ic](MipsMachine& mm) {
        const std::string k = cstr(mm, uint32_t(mm.gpr(R::a1)));
        mm.set_fpr(0, k == "SFX_VOLUME" ? ic.volume : k == "SFX_SKIDMULTIPLE" ? ic.skid : ic.horn_pitch);
    });
    m.hook(kFnLookupInt, "AttributeSet::LookupInt", [&ic](MipsMachine& mm) {
        const std::string k = cstr(mm, uint32_t(mm.gpr(R::a1)));
        mm.set_gpr(R::v0, uint32_t(k == "HAS_SIREN" ? ic.siren : k == "IS_HELI" ? ic.heli : ic.flying));
    });
    m.hook(kFnStrcat, "strcat", [](MipsMachine& mm) {
        const uint32_t d = uint32_t(mm.gpr(R::a0));
        const std::string a = cstr(mm, d), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
        put(mm, d + uint32_t(a.size()), b.c_str(), uint32_t(b.size() + 1));
        mm.set_gpr(R::v0, d);
    });
    m.hook(kFnStrcasecmp, "strcasecmp", [](MipsMachine& mm) {
        const std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
        mm.set_gpr(R::v0, uint32_t(drv::dafi_strcasecmp(a.c_str(), b.c_str())));
    });
    m.hook(kFnSoundNew, "ABaseSound::operator_new", [&ia](MipsMachine& mm) {
        ++ia.news;
        std::memset(mm.mem(kObj, 0x400), 0, 0x400);
        mm.set_gpr(R::v0, kObj);
    });
    m.hook(kFnTrafficCtor, "ATrafficVehicle::ATrafficVehicle", [&ia](MipsMachine& mm) {
        ++ia.traffic;
        ia.engine = cstr(mm, uint32_t(mm.gpr(R::a1)));
        ia.mix = cstr(mm, uint32_t(mm.gpr(R::a2)));
        mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0)));
    });
    m.hook(kFnHeliCtor, "AHelicopter::AHelicopter", [&ia](MipsMachine& mm) {
        ++ia.heli;
        mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0)));
    });
    m.hook(kFnSimRandom, "SimRandom::SimRandom_Generate", [&ia](MipsMachine& mm) {
        ++ia.draws;
        mm.set_gpr(R::v0, drv::SimRandom_Generate(drv::sim_random()));
    });
    m.hook(kFnSiren, "ATrafficVehicle::ActivateSiren", [&ia](MipsMachine& mm) {
        ++ia.sirens;
        ia.siren_patch = cstr(mm, uint32_t(mm.gpr(R::a1)));
    });
    m.hook(kFnChooseHorn, "AVehicle::ChooseHorn", [&ia](MipsMachine& mm) {
        ++ia.horns;
        ia.horn_vol = mm.fpr_bits(12);
        ia.horn_pitch = mm.fpr_bits(13);
        ia.horn_name = cstr(mm, uint32_t(mm.gpr(R::a1)));
    });
    m.hook(kFnSetAudio, "PhysicsObject::SetAudioObject", [&ia](MipsMachine&) { ++ia.set_audio; });
    static const char* const kEngines[] = {"hatchback", "sedan2", "suv1", "750", "Nsub", "nsub", "paris_van", "tank"};
    static const char* const kHorns[] = {"Horn1", "Horn2", "TruckHorn", ""};
    Tally ti;
    for (int c = 0; c < cases; ++c) {
        const int32_t car_class = r.range(1, 5);
        ic = IaoCase();
        ic.engine = kEngines[r.range(0, 7)];
        ic.horn = kHorns[r.range(0, 3)];
        ic.volume = r.real(0, 2);
        ic.skid = r.real(0, 3);
        ic.horn_pitch = r.real(0.5f, 1.5f);
        ic.siren = r.chance(0.3) ? r.range(1, 3) : (r.chance(0.05) ? 0x10000 : 0);
        ic.heli = r.chance(0.2) ? 1 : 0;
        ic.flying = r.chance(0.2) ? 1 : 0;
        // O porte (o gerador guardado e recomeçado para o emulador).
        const drv::SimRandom saved = drv::sim_random();
        drv::AudioCarObjectParams p;
        drv::PBondCar_ReadAudioParams(
            p, car_class,
            [&](const char* k, std::string& v) {
                v = std::string(k) == "SFX_ENGINE" ? ic.engine : ic.horn;
                return true;
            },
            [&](const char* k) {
                const std::string s(k);
                return s == "SFX_VOLUME" ? ic.volume : s == "SFX_SKIDMULTIPLE" ? ic.skid : ic.horn_pitch;
            },
            [&](const char* k) {
                const std::string s(k);
                return s == "HAS_SIREN" ? ic.siren : s == "IS_HELI" ? ic.heli : ic.flying;
            });
        const uint32_t port_draws = drv::sim_random().count - saved.count;
        const drv::AudioCarObjectPlan plan = drv::audio_car_object_plan(p);
        drv::sim_random() = saved;
        // O original.
        std::memset(m.mem(kCar, 0x400), 0, 0x400);
        ia = IaoCalls();
        call_f(e, kFnIao, {kCar, uint32_t(car_class)});
        ++ti.cases;
        if (take_error(e, ti, "InitAudioObject")) continue;
        std::string why;
        const float off_emu = e.getf(kObj + (plan.heli ? 0x344 : 0x238));
        if (uint32_t(ia.draws) != port_draws || ia.news != 1 || ia.set_audio != 1 || m.read32(kCar + 0x388) != kObj)
            why = "o sorteio ou o objeto";
        else if (plan.heli != (ia.heli == 1) || (!plan.heli && (ia.traffic != 1 || ia.engine != p.engine || ia.mix != plan.mix)))
            why = "o objeto (" + ia.mix + " / " + plan.mix + ")";
        else if ((car_class == 2 || car_class == 4) && ps2::fbits(off_emu) != ps2::fbits(p.pitch_offset))
            why = "a afinação";
        else if (m.read32(kObj + 0x6c) != ps2::fbits(p.volume) || m.read32(kObj + 0x5c) != ps2::fbits(plan.min) ||
                 m.read32(kObj + 0x60) != ps2::fbits(plan.max) || m.read32(kObj + 0x64) != ps2::fbits(plan.max2))
            why = "o volume ou o alcance";
        else if (m.read32(kObj + 0x114) != (ps2::ee::le(1.0f, p.skid_multiple) ? ps2::fbits(p.skid_multiple) : 0u))
            why = "as derrapagens";
        else if (plan.siren != (ia.sirens == 1) || (plan.siren && ia.siren_patch != "SFX_copsiren"))
            why = "a sirene";
        else if (plan.horn != (ia.horns == 1) ||
                 (plan.horn && (ia.horn_vol != 0x3f800000u || ia.horn_pitch != ps2::fbits(p.horn_pitch) ||
                                ia.horn_name != p.horn)))
            why = "a buzina";
        if (!why.empty()) ti.fail(why + " (classe " + std::to_string(car_class) + ")");
    }
    report("os outros carros: PBondCar::InitAudioObject (o sorteio)", ti, failures);
    m.hook(kFnMixAdd, "AMix::Add", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x7000u); });
    m.hook(kFnMixRemove, "AMix::Remove", [](MipsMachine&) {});
    m.hook(kFnVoiceSet, "AVoice::Set", [](MipsMachine&) {});
    // Os ganchos que apontavam para as variáveis daqui.
    m.hook(kFnVoiceCtor, "AVoice::AVoice", [](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(mm.gpr(R::a0))); });
    m.hook(kFnMixVolume, "AMix::GetVolume", [](MipsMachine& mm) { mm.set_fpr(0, 1.0f); });
    m.hook(kFnVoicePlay, "AVoice::Play", [](MipsMachine&) {});
    m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
}

}  // namespace

int run_rest_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size, int cases,
                    uint32_t seed) {
    std::printf("\nparte 25: os efeitos e os sons que faltavam em Paris (os riscos das balas, os clarões, os destroços, o "
                "mixador do IOP, os objetos de som)\n");
    int failures = 0;
    static Emu e;
    e.setup(elf, vu0, vu0_size);
    Rng r(seed ^ 0x25);
    // Os .tun de Paris (o MIS01.VIV).
    std::string fx, glare;
    {
        drv::Bigf mis;
        if (mis.open(disc, "DRIVING/MIS01.VIV")) {
            std::vector<uint8_t> b;
            const int i1 = mis.find("data\\tuning\\Render\\FX\\paris_mis01.tun");
            if (i1 >= 0 && mis.read(i1, b)) fx.assign(b.begin(), b.end());
            const int i2 = mis.find("data\\tuning\\Render\\Glare\\default.tun");
            if (i2 >= 0 && mis.read(i2, b)) glare.assign(b.begin(), b.end());
        }
        if (fx.empty() || glare.empty()) std::printf("  sem os .tun do MIS01.VIV: as tabelas ficam de fora\n");
    }
    std::vector<std::string> files;
    if (!fx.empty()) files.push_back(fx);
    if (!glare.empty()) files.push_back(glare);
    check_tuning(e, r, files, cases, failures);
    if (!fx.empty() && !glare.empty()) check_tables(e, fx, glare, failures);
    check_streak(e, r, fx, cases, failures);
    check_tracers(e, r, cases, failures);
    check_glares(e, r, cases, failures);
    check_debris(e, r, cases, failures);
    check_acharacter(e, r, cases, failures);
    return failures;
}

}  // namespace drvcheck
