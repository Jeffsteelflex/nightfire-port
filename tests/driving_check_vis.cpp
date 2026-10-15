// nf_driving_check, parte 11 (tests/driving_check_vis.h): a visibilidade do mundo do DRIVING.ELF (docs/driving.md
// seção 15).
//
//  a) O microprograma VU0_culling (o terceiro do .vutext) ORIGINAL no interpretador da VU (platform/vu_interp.h) e o
//     porte à mão (driving/vu0_culling.h), a partir do mesmo estado sorteado da VU0, nos três pontos de entrada: 0x000
//     (a cortina vista do olho: os planos ou nada), 0x092 (a esfera contra as cortinas) e 0x0a8 (o ponto). Compara o
//     estado inteiro depois: os 32 VF, ACC, os 16 VI, Q e os 4 KB da memória de dados.
//  b) As funções do EE ORIGINAIS no interpretador MIPS com a VU0 ligada, lado a lado com o porte, com entradas
//     sorteadas: RRenderWorldCulling::Setup2dFrustrum (e o InitializePlaneInfo que ele chama quando o fov ou o alcance
//     mudam), IsInFrustum2d, QuadtreeFrustrumCheck2d, o construtor da WVisCurtain (CalcRadius), sequências de AddCurtain
//     com IsVisibleAgainstCurtains e CheckPositionAgainstActiveCurtains, e o std::sort do DrawPass (__introsort_loop e
//     __final_insertion_sort com os dois comparadores, também com o limite de profundidade baixo, que cai no
//     partial_sort) sobre listas com empates.
//  c) Por pista do disco com árvore de cenas: as instâncias, a árvore ('Wmap'/'wn', com os ponteiros trocados pelos
//     endereços emulados) e as cortinas do porte copiadas para a RAM emulada, o WTree::InitializeTree original sobre a
//     cópia; e, para câmeras andando pela pista (fovs variados, com e sem widescreen), o
//     RPlayerViewCamera::ConfigureView, o WRender::GenerateCurtainsAndNodes, o PrepareForCull e o
//     RRenderWorldCamera::CullModule com o GetNextPoint e o SetCull originais (a parte do mundo do PerformCulling), o
//     DrawPass_sort dos dois tipos e os CopyDrawPasses do DoRender. Compara o fgWorldCulling (os 0xa0 bytes), o
//     multiplicador de LOD, as cortinas ativas e os planos na memória da VU0, a lista do scratchpad (os nós, as
//     instâncias e as distâncias, pelos endereços do porte) e o cursor. Os objetos de cena: o construtor original do
//     RSceneObj (WTree::FindNode, SetMapNode) para cada objeto que o porte criou (o WWorld::Open), comparando os objetos
//     e as listas dos nós; e, a cada quadro, o PrepareSceneObjsForCulling e o CullModule com o
//     GetNextSceneObjCullInfo e o SetSceneObjectCull originais (o SetNowVisible): as listas de visíveis e o cursor.
//  d) As entradas por quadro dos métodos do mundo (driving/light_manager.h): o RTimeData::Update (o passo da simulação,
//     também os negativos: sem sinal) e as direções do bloco de luzes do RLightManager (0x001b0038, com o BuildRotate e o
//     TransformPoint) com ângulos sorteados e os do .tun de cada pista.
//
// Fora do código original (ganchos nativos): o atan2f, o sinf e o cosf do newlib (nf::fm::atan2/sin/cos nos dois
// lados, como na colisão), o memset (as MMI dele), o RPathEngine::GetPathHandle (0: o RPathEngine não foi portado) e o
// VU0_UploadMicrocode (copia o VU0_culling para a memória de microprograma).

#include "driving_check_vis.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/light_manager.h"
#include "driving/physics/vu0_physics.h"
#include "driving/track.h"
#include "driving/vu0_culling.h"
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

// As funções.
constexpr uint32_t kFnCullingStaticCtor = 0x1d4fc0;  // os construtores globais de RViewCamera::fCulling
constexpr uint32_t kFnSetup2d = 0x1c6858, kFnIsInFrustum = 0x1c65b0, kFnQuadtree = 0x1c66c8;
constexpr uint32_t kFnConfigureView = 0x1beba0;
constexpr uint32_t kFnCurtainCtor = 0x22f660, kFnInitActive = 0x22f408, kFnAddCurtain = 0x22f4a0;
constexpr uint32_t kFnIsVisible = 0x22f5a8, kFnCheckPos = 0x22f5e8;
constexpr uint32_t kFnInitTree = 0x22c730, kFnGenerate = 0x21f1d8, kFnPrepare = 0x21f288;
constexpr uint32_t kFnGetNextPoint = 0x21f2b8, kFnSetCull = 0x21f570, kFnCopyPasses = 0x21f5b8, kFnCullModule = 0x1c54a0;
constexpr uint32_t kFnIntrosort = 0x2207c0, kFnFinalInsertion = 0x220b10, kCmpInstance = 0x220bc0, kCmpDistance = 0x220bd0;
constexpr uint32_t kFnAtan2f = 0x2c0618, kFnUploadMicrocode = 0x2cc3f0, kFnSinf = 0x2bfc38, kFnCosf = 0x2bfa48;
constexpr uint32_t kFnTimeUpdate = 0x1d1ef8, kFnLightDirections = 0x1b0038;
constexpr uint32_t kFnSceneObjCtor = 0x1c7098, kFnPathHandle = 0x1b20c8, kFnPrepareSceneObjs = 0x1c80f0;
constexpr uint32_t kFnNextSceneObj = 0x1c8130, kFnSetSceneObjCull = 0x1c8490;
constexpr uint32_t kFnMemset = 0x2a8038;
constexpr uint32_t kSceneNode = 0x333f34, kSceneCurrent = 0x333f3c, kSceneNumVisible = 0x3c8320;
constexpr uint32_t kSceneNumLast = 0x3c8324, kSceneMax = 0x3c8328, kSceneVisible = 0x424840, kSceneLast = 0x425040;
constexpr uint32_t kSimSteps = 0x339f54, kTimeData = 0x334000, kTime = 0x334010, kCausticTime = 0x33403c;

// Os globais.
constexpr uint32_t kCulling = 0x334160, kRendererPtr = 0x333760, kWorldPtr = 0x33aac8, kRenderPtr = 0x33a5d4;
constexpr uint32_t kCurtainCount = 0x33aa9c, kCursorNode = 0x33a5dc, kCursorItem = 0x33a5e0, kCursorList = 0x33a5e4;
constexpr uint32_t kCurrentList = 0x33a5e8;
constexpr uint32_t kScratch = MipsMachine::kScratchpad;

// A RAM emulada.
constexpr uint32_t kWorld = 0x01000000, kView = 0x01000100, kRenderer = 0x01000200, kCam = 0x01000300;
constexpr uint32_t kRender = 0x01000400, kWmap = 0x01000500, kArgs = 0x01000600;
constexpr uint32_t kTo = 0x01001000;      // uma CachedDrawInfo (0x2c08)
constexpr uint32_t kSortBuf = 0x01004000;  // até 0x400 entradas
constexpr uint32_t kInst = 0x01010000, kCurt = 0x01060000, kTree = 0x01080000, kTreeEnd = 0x01e00000;
constexpr uint32_t kSceneObjs = 0x01e00000, kSceneObjsEnd = 0x01f00000;  // os RSceneObj (0x40 bytes)

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
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

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

void report(const char* what, const Tally& t, int& failures, const char* extra = "") {
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s%s\n", what, t.cases, t.bad, extra,
                t.bad ? ": " : "", t.bad ? t.first.c_str() : "");
    if (t.bad) ++failures;
}

bool same_bytes(const std::string& what, const void* emu, const void* port, uint32_t n, std::string& out) {
    const uint8_t* a = static_cast<const uint8_t*>(emu);
    const uint8_t* b = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
        uint32_t x = 0, y = 0;
        std::memcpy(&x, a + o, std::min(4u, n - o));
        std::memcpy(&y, b + o, std::min(4u, n - o));
        if (x != y) {
            out = what + " +0x" + hex(o).substr(4) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

// Um float sorteado: quase sempre num intervalo do mundo, às vezes um valor especial do PS2.
uint32_t rnd_bits(Rng& r, float lo = -1000.0f, float hi = 1000.0f) {
    switch (r.next() % 64) {
    case 0: return 0;
    case 1: return 0x80000000u;
    case 2: return 0x3f800000u;
    case 3: return 0x7f7fffffu;
    case 4: return r.next();  // qualquer padrão (inclusive o expoente 255, normal no PS2)
    default: return ps2::fbits(r.uni(lo, hi));
    }
}

// ------------------------------------------------------------------------------------------------- a) o microprograma

std::string diff_vu(const ps2::VuMachine& a, const ps2::VuMachine& b) {
    const ps2::VuRegs& x = a.regs();
    const ps2::VuRegs& y = b.regs();
    char buf[128];
    for (int i = 0; i < 32; ++i)
        for (int l = 0; l < 4; ++l)
            if (x.vf[i][l] != y.vf[i][l]) {
                std::snprintf(buf, sizeof buf, "vf%02d.%c: original %08x, porte %08x", i, "xyzw"[l], x.vf[i][l],
                              y.vf[i][l]);
                return buf;
            }
    for (int l = 0; l < 4; ++l)
        if (x.acc[l] != y.acc[l]) {
            std::snprintf(buf, sizeof buf, "ACC.%c: original %08x, porte %08x", "xyzw"[l], x.acc[l], y.acc[l]);
            return buf;
        }
    for (int i = 0; i < 16; ++i)
        if (x.vi[i] != y.vi[i]) {
            std::snprintf(buf, sizeof buf, "vi%02d: original %04x, porte %04x", i, x.vi[i], y.vi[i]);
            return buf;
        }
    if (x.q != y.q) {
        std::snprintf(buf, sizeof buf, "Q: original %08x, porte %08x", x.q, y.q);
        return buf;
    }
    for (uint32_t o = 0; o < a.data_size(); o += 4) {
        uint32_t p, q;
        std::memcpy(&p, a.data() + o, 4);
        std::memcpy(&q, b.data() + o, 4);
        if (p != q) {
            std::snprintf(buf, sizeof buf, "memória +0x%03x: original %08x, porte %08x", o, p, q);
            return buf;
        }
    }
    return {};
}

void random_state(Rng& r, ps2::VuMachine& vu) {
    ps2::VuRegs& g = vu.regs();
    for (int i = 1; i < 32; ++i)
        for (int l = 0; l < 4; ++l) g.vf[i][l] = rnd_bits(r);
    for (int l = 0; l < 4; ++l) g.acc[l] = rnd_bits(r);
    for (int i = 1; i < 16; ++i) g.vi[i] = uint16_t(r.next());
    g.q = rnd_bits(r);
    for (uint32_t o = 0; o < vu.data_size(); o += 4) {
        const uint32_t v = rnd_bits(r, -50.0f, 50.0f);
        std::memcpy(vu.data() + o, &v, 4);
    }
}

// Uma cortina sorteada perto de `c` (o pé e o topo com a mesma ou quase a mesma altura de pé), às vezes degenerada.
void random_curtain(Rng& r, const float c[3], float s[4], float e[4]) {
    const float x = c[0] + r.uni(-200, 200), z = c[2] + r.uni(-200, 200), y = c[1] + r.uni(-20, 5);
    float len = r.uni(1, 120);
    const float ang = r.uni(0, 6.2831853f);
    float h = r.uni(2, 60);
    switch (r.next() % 16) {
    case 0: len = 0; break;  // pé e topo no mesmo x, z
    case 1: h = 0; break;    // sem altura: a normal some
    default: break;
    }
    s[0] = x;
    s[1] = y;
    s[2] = z;
    s[3] = r.chance(0.5) ? 0.0f : r.uni(0, 60);
    e[0] = x + len * std::cos(ang);
    e[1] = y + h;
    e[2] = z + len * std::sin(ang);
    e[3] = r.chance(0.5) ? 0.0f : r.uni(-5, 5);
}

void check_microcode(const std::vector<uint8_t>& program, int cases, uint32_t seed, int& failures) {
    Rng r(seed ^ 0xc0117a1u);
    Tally add, check, pos;
    uint64_t added = 0, hidden = 0, hidden_pos = 0;
    ps2::VuMachine orig(ps2::VuUnit::VU0), port(ps2::VuUnit::VU0);
    orig.load_micro(0, program.data(), uint32_t(program.size()));
    for (int c = 0; c < cases; ++c) {
        const int kind = c % 3;
        random_state(r, orig);
        const float centre[3] = {r.uni(-500, 500), r.uni(-10, 50), r.uni(-500, 500)};
        if (kind != 0) {
            // A memória com cortinas de verdade: o porte do 0x000 (conferido nos casos de kind 0) sobre cortinas
            // sorteadas em volta do centro, com o olho do lado da frente quando dá.
            ps2::VuMachine tmp(ps2::VuUnit::VU0);
            std::memcpy(tmp.data(), orig.data(), orig.data_size());
            const int n = r.range(1, 63);
            int made = 0;
            for (int k = 0; made < n && k < 200; ++k) {
                float s[4], e[4], eye[4] = {centre[0] + r.uni(-50, 50), centre[1] + r.uni(0, 20), centre[2] + r.uni(-50, 50), 1};
                random_curtain(r, centre, s, e);
                std::memcpy(tmp.regs().vf[1], s, 16);
                std::memcpy(tmp.regs().vf[2], e, 16);
                std::memcpy(tmp.regs().vf[3], eye, 16);
                tmp.regs().vi[1] = uint16_t(made);
                drv::VU0MICRO_AddCurtain(tmp);
                if (tmp.regs().vi[2]) ++made;
            }
            std::memcpy(orig.data(), tmp.data(), orig.data_size());
            ps2::VuRegs& g = orig.regs();
            g.vi[1] = uint16_t(std::max(1, made));
            float sp[4] = {centre[0] + r.uni(-250, 250), centre[1] + r.uni(-10, 30), centre[2] + r.uni(-250, 250),
                           r.chance(0.3f) ? 0.0f : r.uni(0, 40)};
            std::memcpy(g.vf[1], sp, 16);
            const float margin = r.chance(0.2f) ? 0.0f : r.uni(-5, 60);
            g.vf[2][0] = ps2::fbits(margin);
        } else {
            ps2::VuRegs& g = orig.regs();
            float s[4], e[4];
            random_curtain(r, centre, s, e);
            float eye[4] = {centre[0] + r.uni(-300, 300), centre[1] + r.uni(-5, 50), centre[2] + r.uni(-300, 300), 1};
            if (r.chance(0.05)) std::memcpy(eye, s, 12);  // o olho no pé da cortina
            std::memcpy(g.vf[1], s, 16);
            std::memcpy(g.vf[2], e, 16);
            std::memcpy(g.vf[3], eye, 16);
            g.vi[1] = uint16_t(r.range(0, 62));
        }
        port = orig;
        const uint32_t entry = kind == 0 ? 0x000 : kind == 1 ? 0x092 : 0x0a8;
        Tally& t = kind == 0 ? add : kind == 1 ? check : pos;
        ++t.cases;
        try {
            orig.run(entry, 2'000'000);
        } catch (const std::exception& ex) {
            t.fail(std::string("o interpretador falhou: ") + ex.what());
            continue;
        }
        drv::vu0_culling_native(port, entry);
        const std::string d = diff_vu(orig, port);
        if (!d.empty()) t.fail("caso " + std::to_string(c) + ": " + d);
        if (kind == 0 && orig.regs().vi[2]) ++added;
        if (kind == 1 && !orig.regs().vi[2]) ++hidden;
        if (kind == 2 && !orig.regs().vi[2]) ++hidden_pos;
    }
    char extra[96];
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " gravadas)", added);
    report("VU0_culling 0x000 (a cortina)", add, failures, extra);
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " escondidas)", hidden);
    report("VU0_culling 0x092 CheckAgainstVisCurtains", check, failures, extra);
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " escondidos)", hidden_pos);
    report("VU0_culling 0x0a8 CheckPositionAgainstVisCurtains", pos, failures, extra);
}

// ------------------------------------------------------------------------------------------------- o emulador

class VisEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(kFnAtan2f, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        m.hook(kFnSinf, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(kFnCosf, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(kFnPathHandle, "RPathEngine::GetPathHandle", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, dst);
        });
        m.hook(kFnUploadMicrocode, "VU0_UploadMicrocode",
               [this](MipsMachine&) { vu.load_micro(0, program.data(), uint32_t(program.size())); });
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args, float f12 = 0, float f13 = 0, uint32_t t0 = 0) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.set_fpr(12, f12);
        m.set_fpr(13, f13);
        m.set_gpr(R::t0, t0);
        m.call(fn, args, 200'000'000);
    }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
    void get(uint32_t a, void* dst, uint32_t n) { std::memcpy(dst, m.mem(a, n), n); }
    uint32_t r32(uint32_t a) { return m.read32(a); }
    void w32(uint32_t a, uint32_t v) { m.write32(a, v); }
};

// Uma câmera: a matriz (linhas: direita, cima, frente, o olho) por giro, inclinação e rolagem.
void camera_matrix(float yaw, float pitch, float roll, const float eye[3], float m[16]) {
    const float f[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    float rgt[3] = {f[2], 0, -f[0]};
    float l = std::sqrt(rgt[0] * rgt[0] + rgt[2] * rgt[2]);
    if (l < 1e-6f) {
        rgt[0] = 1;
        l = 1;
    }
    for (float& v : rgt) v /= l;
    float up[3] = {f[1] * rgt[2] - f[2] * rgt[1], f[2] * rgt[0] - f[0] * rgt[2], f[0] * rgt[1] - f[1] * rgt[0]};
    const float c = std::cos(roll), s = std::sin(roll);
    float r2[3], u2[3];
    for (int k = 0; k < 3; ++k) {
        r2[k] = c * rgt[k] + s * up[k];
        u2[k] = -s * rgt[k] + c * up[k];
    }
    const float* rows[4] = {r2, u2, f, eye};
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) m[i * 4 + k] = rows[i][k];
        m[i * 4 + 3] = i == 3 ? 1.0f : 0.0f;
    }
}

// ------------------------------------------------------------------------------------------------- b) as funções

void check_functions(VisEmu& e, int cases, uint32_t seed, int& failures) {
    Rng r(seed ^ 0xf4057u);
    Tally setup, frustum, quad, ctor, curt, sorts;
    uint64_t inside = 0, quad_kinds[3] = {0, 0, 0}, added = 0, hidden = 0;
    std::string d;
    drv::RRenderWorldCulling& pc = drv::fgWorldCulling();
    float fov = 33, range = 400;
    for (int c = 0; c < cases; ++c) {
        // Setup2dFrustrum (às vezes com o mesmo fov e alcance: o caminho que não refaz o InitializePlaneInfo).
        if (!r.chance(0.3)) {
            fov = r.chance(0.1) ? 33.0f : r.uni(3, 89);
            range = r.chance(0.1) ? 400.0f : r.uni(5, 1500);
        }
        float pos[4] = {r.uni(-2000, 2000), r.uni(-50, 300), r.uni(-2000, 2000), r.chance(0.5) ? 1.0f : r.uni(-2, 2)};
        float m[16];
        camera_matrix(r.uni(-7, 7), r.chance(0.05) ? 1.5707964f : r.uni(-1.2f, 1.2f), r.chance(0.3) ? r.uni(-0.5f, 0.5f) : 0,
                      pos, m);
        if (r.chance(0.02)) m[8] = m[10] = 0;  // a frente vertical: atan2(0, 0)
        e.put(kArgs, pos, 16);
        e.put(kArgs + 0x10, m, 64);
        ++setup.cases;
        e.call(kFnSetup2d, {kCulling, kArgs, kArgs + 0x10}, fov, range);
        drv::Setup2dFrustrum(pc, pos, m, fov, range);
        uint8_t emu[0xa0];
        e.get(kCulling, emu, 0xa0);
        if (!same_bytes("fgWorldCulling", emu, &pc, 0xa0, d)) setup.fail("caso " + std::to_string(c) + ": " + d);
        // IsInFrustum2d e QuadtreeFrustrumCheck2d em volta do olho.
        for (int k = 0; k < 4; ++k) {
            float sph[4] = {pos[0] + r.uni(-1.2f, 1.2f) * range, pos[1] + r.uni(-50, 50), pos[2] + r.uni(-1.2f, 1.2f) * range,
                            r.uni(0, 200)};
            const float radius = r.chance(0.2) ? 0.0f : r.uni(0, 150);
            const int far = r.chance(0.6) ? 1 : 0;
            const float scale = r.chance(0.5) ? 1.0f : r.chance(0.5) ? 0.3f : 2.0f;
            e.put(kArgs + 0x60, sph, 16);
            e.w32(kArgs + 0x80, 0xdeadbeef);
            float pd = ps2::ffloat(0xdeadbeef);
            ++frustum.cases;
            e.call(kFnIsInFrustum, {kCulling, kArgs + 0x60, uint32_t(far), r.chance(0.2) ? 0u : kArgs + 0x80}, radius, scale);
            const uint32_t ev = uint32_t(e.m.gpr(R::v0));
            const bool pv = drv::IsInFrustum2d(pc, sph, radius, far != 0, scale, &pd);
            const uint32_t edist = e.r32(kArgs + 0x80);
            if ((ev != 0) != pv) frustum.fail("caso " + std::to_string(c) + ": o retorno");
            else if (pv && edist != 0xdeadbeef && edist != ps2::fbits(pd))
                frustum.fail("caso " + std::to_string(c) + ": a distância original " + hex(edist) + ", porte " +
                             hex(ps2::fbits(pd)));
            inside += pv;
            float box[4] = {pos[0] + r.uni(-1.5f, 1.5f) * range, pos[1], pos[2] + r.uni(-1.5f, 1.5f) * range, r.uni(1, 600)};
            const float qr = box[3] * 1.414f;
            e.put(kArgs + 0x60, box, 16);
            ++quad.cases;
            e.call(kFnQuadtree, {kCulling, kArgs + 0x60}, qr);
            const int eq = int(e.m.gpr(R::v0));
            const int pq = drv::QuadtreeFrustrumCheck2d(pc, box, qr);
            if (eq != pq) quad.fail("caso " + std::to_string(c) + ": original " + std::to_string(eq) + ", porte " + std::to_string(pq));
            if (pq >= 0 && pq < 3) ++quad_kinds[pq];
        }
        // O construtor da WVisCurtain.
        {
            float s[4], en[4];
            random_curtain(r, pos, s, en);
            e.put(kArgs, s, 16);
            e.put(kArgs + 0x10, en, 16);
            ++ctor.cases;
            e.call(kFnCurtainCtor, {kArgs + 0x40, kArgs, kArgs + 0x10});
            drv::WVisCurtain pcurt;
            drv::WVisCurtain_Construct(pcurt, s, en);
            uint8_t ec[0x20];
            e.get(kArgs + 0x40, ec, 0x20);
            if (!same_bytes("WVisCurtain", ec, &pcurt, 0x20, d)) ctor.fail("caso " + std::to_string(c) + ": " + d);
        }
        // Uma sequência de AddCurtain com testes de esferas e pontos no meio.
        if (c % 4 == 0) {
            e.call(kFnInitActive, {});
            drv::InitActiveCurtainList();
            const float eye[4] = {pos[0], pos[1], pos[2], 1};
            const int n = r.range(1, 90);
            for (int k = 0; k < n; ++k) {
                float s[4], en[4];
                random_curtain(r, pos, s, en);
                drv::WVisCurtain pcurt;
                drv::WVisCurtain_Construct(pcurt, s, en);
                e.put(kArgs, &pcurt, 0x20);
                e.put(kArgs + 0x20, eye, 16);
                ++curt.cases;
                e.call(kFnAddCurtain, {kArgs, kArgs + 0x20});
                drv::AddCurtain(pcurt, eye);
                const uint32_t ec = e.r32(kCurtainCount);
                if (int32_t(ec) != drv::numActiveCurtains()) {
                    curt.fail("caso " + std::to_string(c) + "/" + std::to_string(k) + ": as cortinas ativas original " +
                              std::to_string(ec) + ", porte " + std::to_string(drv::numActiveCurtains()));
                    break;
                }
                if (!same_bytes("os planos na VU0", e.vu.data(), drv::vu0().data(), ec * 64, d)) {
                    curt.fail("caso " + std::to_string(c) + "/" + std::to_string(k) + ": " + d);
                    break;
                }
                float sph[4] = {pos[0] + r.uni(-200, 200), pos[1] + r.uni(-10, 30), pos[2] + r.uni(-200, 200),
                                r.chance(0.3) ? 0.0f : r.uni(0, 30)};
                const float margin = r.uni(0, 50);
                e.put(kArgs + 0x60, sph, 16);
                e.call(kFnIsVisible, {kArgs + 0x60}, margin);
                const bool ev = e.m.gpr(R::v0) != 0;
                const bool pv = drv::IsVisibleAgainstCurtains(sph, margin);
                e.call(kFnCheckPos, {kArgs + 0x60});
                const bool ep = e.m.gpr(R::v0) != 0;
                const bool pp = drv::CheckPositionAgainstActiveCurtains(sph);
                if (ev != pv || ep != pp) {
                    curt.fail("caso " + std::to_string(c) + "/" + std::to_string(k) + ": o teste da esfera ou do ponto");
                    break;
                }
                hidden += !pv;
            }
            added += uint64_t(drv::numActiveCurtains());
        }
        // O std::sort do DrawPass sobre uma lista com empates.
        if (c % 2 == 0) {
            const int n = r.chance(0.1) ? r.range(0, 20) : r.range(0, 0x3ff);
            std::vector<drv::InstanceAndDistance> v(static_cast<size_t>(n));
            const int vals = r.range(1, 40);
            for (auto& x : v) {
                x.instance = kInst + 0x40 * uint32_t(r.range(0, 3000));
                x.distance = r.chance(0.2) ? 0.0f : float(r.range(0, vals)) * 7.25f;
            }
            for (int sort = 1; sort <= 2; ++sort) {
                int lg = 0;
                for (int k = n; k > 1; k >>= 1) ++lg;
                const int depth = r.chance(0.3) ? r.range(0, 3) : 2 * lg;
                std::vector<drv::InstanceAndDistance> p = v;
                e.put(kSortBuf, v.data(), uint32_t(8 * n));
                ++sorts.cases;
                const uint32_t cmp = sort == 1 ? kCmpInstance : kCmpDistance;
                if (n > 0) {
                    e.call(kFnIntrosort, {kSortBuf, kSortBuf + 8u * uint32_t(n), 0, uint32_t(depth)}, 0, 0, cmp);
                    e.call(kFnFinalInsertion, {kSortBuf, kSortBuf + 8u * uint32_t(n), cmp});
                    drv::introsort_loop(p.data(), p.data() + n, depth, sort);
                    drv::final_insertion_sort(p.data(), p.data() + n, sort);
                }
                std::vector<drv::InstanceAndDistance> o(static_cast<size_t>(n));
                if (n > 0) e.get(kSortBuf, o.data(), uint32_t(8 * n));
                if (n > 0 && !same_bytes("a lista ordenada", o.data(), p.data(), uint32_t(8 * n), d))
                    sorts.fail("caso " + std::to_string(c) + " (n " + std::to_string(n) + ", tipo " + std::to_string(sort) +
                               ", profundidade " + std::to_string(depth) + "): " + d);
            }
        }
    }
    report("Setup2dFrustrum (com o InitializePlaneInfo)", setup, failures);
    char extra[96];
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " dentro)", inside);
    report("IsInFrustum2d", frustum, failures, extra);
    std::snprintf(extra, sizeof extra, " (fora %" PRIu64 ", dentro %" PRIu64 ", cruzando %" PRIu64 ")", quad_kinds[0],
                  quad_kinds[1], quad_kinds[2]);
    report("QuadtreeFrustrumCheck2d", quad, failures, extra);
    report("WVisCurtain::WVisCurtain (CalcRadius)", ctor, failures);
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " ativas, %" PRIu64 " esferas escondidas)", added, hidden);
    report("AddCurtain, IsVisibleAgainstCurtains, CheckPosition...", curt, failures, extra);
    report("DrawPass: __introsort_loop + __final_insertion_sort", sorts, failures);
}

// ------------------------------------------------------------------------------------------------- c) as pistas

const char* const kTracks[] = {"paris_mis01",    "uw_mis11",    "junglea_mis13a", "jungleb_mis13b",
                               "snow1a_mis3",    "snow2a_mis4", "junglec_mis13c", "snow2a_race"};

struct TreeCopy {
    uint32_t base = 0, size = 0;  // o trecho do port (p32) copiado para kTree
    uint32_t root_emu = 0;
    std::vector<uint32_t> nodes;  // os nós do port (p32)
    uint32_t emu(uint32_t p) const { return kTree + (p - base); }
    uint32_t port(uint32_t a) const { return base + (a - kTree); }
};

bool copy_tree(VisEmu& e, const drv::Track& t, TreeCopy& tc, std::string& error) {
    uint32_t root;
    std::memcpy(&root, nf::p32_ptr<const uint8_t>(t.world_map()), 4);
    if (!root) {
        error = "o 'Wmap' sem raiz";
        return false;
    }
    uint32_t lo = root, hi = root + 0x10;
    std::vector<uint32_t> stack{root};
    while (!stack.empty()) {
        const uint32_t a = stack.back();
        stack.pop_back();
        tc.nodes.push_back(a);
        const drv::MapNode* n = nf::p32_ptr<const drv::MapNode>(a);
        lo = std::min(lo, a);
        hi = std::max(hi, a + 0x10);
        hi = std::max(hi, a + n->list_offset + 2u * (n->instance_count + n->curtain_count));
        if (n->children)
            for (int k = 0; k < 4; ++k) stack.push_back(n->children + 0x10u * uint32_t(k));
    }
    tc.base = lo;
    tc.size = hi - lo;
    if (tc.size > kTreeEnd - kTree) {
        error = "a árvore não cabe na RAM emulada (" + std::to_string(tc.size) + " bytes)";
        return false;
    }
    e.put(kTree, nf::p32_ptr<const uint8_t>(lo), tc.size);
    for (uint32_t a : tc.nodes) {
        const drv::MapNode* n = nf::p32_ptr<const drv::MapNode>(a);
        if (n->children) e.w32(tc.emu(a), tc.emu(n->children));
    }
    tc.root_emu = tc.emu(root);
    return true;
}

void check_tracks(VisEmu& e, const nf::DiscSource& disc, int frames, uint32_t seed, int& failures) {
    const std::vector<uint8_t> clean(e.m.ram());
    Rng r(seed ^ 0x7ee5u);
    Tally tree, frame, sorts, copies, scene, scene_frames;
    uint64_t nodes = 0, curtains = 0, tested = 0, kept = 0, tracks = 0, guards = 0, overflows = 0;
    uint64_t scene_objs = 0, scene_visible = 0;
    std::string d;
    for (const char* name : kTracks) {
        std::memcpy(e.m.ram().data(), clean.data(), clean.size());
        drv::Track track;
        std::string error;
        if (!track.load(disc, name, "", error)) {
            std::printf("  %s: não carregou (%s)\n", name, error.c_str());
            ++failures;
            continue;
        }
        if (!track.has_scene_tree()) {
            std::printf("  %s: sem árvore de cenas ('Wmap')\n", name);
            continue;
        }
        ++tracks;
        TreeCopy tc;
        if (!copy_tree(e, track, tc, error)) {
            std::printf("  %s: %s\n", name, error.c_str());
            ++failures;
            continue;
        }
        // As instâncias, as cortinas e o WRender.
        drv::WRender render = track.world_render();
        const uint32_t count = uint32_t(track.instances().size());
        e.put(kInst, nf::p32_ptr<const uint8_t>(render.instances), 0x40 * count);
        if (!track.curtains().empty())
            e.put(kCurt, track.curtains().data(), uint32_t(0x20 * track.curtains().size()));
        auto inst_emu = [&](uint32_t p) { return kInst + (p - render.instances); };
        auto inst_port = [&](uint32_t a) { return render.instances + (a - kInst); };
        drv::WRender er = render;
        er.instances = kInst;
        for (int p = 0; p < 5; ++p) er.pass_first[p] = inst_emu(render.pass_first[p]);
        er.curtains = render.curtains ? kCurt : 0;
        er.proc_anims = 0;
        e.put(kRender, &er, sizeof er);
        e.w32(kRenderPtr, kRender);
        // O 'Wmap' (o canto e o lado) e o WWorld.
        uint8_t wmap[0x30];
        std::memcpy(wmap, nf::p32_ptr<const uint8_t>(track.world_map()), 0x30);
        std::memcpy(wmap, &tc.root_emu, 4);
        e.put(kWmap, wmap, 0x30);
        e.w32(kWorld + 0x1c, kWmap);
        e.w32(kWorldPtr, kWorld);
        e.w32(kRendererPtr, kRenderer);
        // O WTree::InitializeTree original sobre a cópia (o porte já o rodou no Track::load).
        ++tree.cases;
        e.call(kFnInitTree, {tc.root_emu, 0, 0, 0});
        for (uint32_t a : tc.nodes) {
            uint8_t en[0x10], pn[0x10];
            e.get(tc.emu(a), en, 0x10);
            std::memcpy(pn, nf::p32_ptr<const uint8_t>(a), 0x10);
            std::memcpy(en, pn, 4);  // os filhos (endereços diferentes)
            uint32_t head;
            std::memcpy(&head, en + 4, 4);
            std::memcpy(en + 4, pn + 4, 4);  // o porte já pôs os objetos de cena (abaixo, contra o construtor original)
            if (head != 0 || std::memcmp(en, pn, 0x10) != 0) {
                tree.fail(std::string(name) + ": o nó " + hex(a - tc.base));
                break;
            }
        }
        // As passadas do WRender::WRender contra a passada que o Track::load deu a cada instância.
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t a = render.instances + 0x40 * i;
            int pass = -1;
            for (int p = 0; p < 5; ++p)
                if (render.pass_count[p] && a >= render.pass_first[p] && a < render.pass_first[p] + 0x40u * uint32_t(render.pass_count[p]))
                    pass = p;
            if (pass != track.instances()[i].pass) {
                tree.fail(std::string(name) + ": a passada da instância " + std::to_string(i));
                break;
            }
        }
        // Os objetos de cena: o construtor original do RSceneObj (WTree::FindNode, SetMapNode) para cada instância que o
        // porte escolheu (o WWorld::Open), na mesma ordem, contra os objetos do porte e as listas dos nós.
        const std::vector<drv::RSceneObj>& objs = track.scene_objects();
        auto obj_emu = [&](uint32_t p) -> uint32_t {
            if (!p) return 0;
            return kSceneObjs + (p - nf::p32_u(objs.data()));
        };
        auto node_emu = [&](uint32_t p) -> uint32_t { return p ? tc.emu(p) : 0; };
        if (kSceneObjs + 0x40u * uint32_t(objs.size()) > kSceneObjsEnd) {
            scene.fail(std::string(name) + ": os objetos de cena não cabem");
        } else {
            for (size_t i = 0; i < objs.size(); ++i) {
                ++scene.cases;
                e.call(kFnSceneObjCtor, {kSceneObjs + 0x40u * uint32_t(i), inst_emu(objs[i].instance)});
            }
            for (size_t i = 0; i < objs.size(); ++i) {
                drv::RSceneObj eo, po = objs[i];
                e.get(kSceneObjs + 0x40u * uint32_t(i), &eo, 0x40);
                po.next = obj_emu(po.next);
                po.prev = obj_emu(po.prev);
                po.instance = inst_emu(po.instance);
                po.map_node = node_emu(po.map_node);
                eo.vtable = po.vtable;  // o do PS2 (0x386a88) não existe no porte
                if (!same_bytes("o objeto de cena " + std::to_string(i), &eo, &po, 0x40, d)) {
                    scene.fail(std::string(name) + ": " + d);
                    break;
                }
            }
            for (uint32_t a : tc.nodes) {
                const uint32_t ph = nf::p32_ptr<const drv::MapNode>(a)->scene_objs;
                if (e.r32(tc.emu(a) + 4) != obj_emu(ph)) {
                    scene.fail(std::string(name) + ": a lista do nó " + hex(a - tc.base));
                    break;
                }
            }
            scene_objs += objs.size();
        }
        // A cópia do porte para as funções (Track::cull usa a dele).
        drv::fgRender() = &render;
        drv::fgWorldMap() = track.world_map();
        // As câmeras: um passeio pela pista a partir de instâncias do mundo.
        float eye[3] = {0, 0, 0}, yaw = 0, pitch = 0;
        for (int f = 0; f < frames; ++f) {
            if (f % 40 == 0) {
                const drv::TrackInstance* ti = nullptr;
                for (int tries = 0; tries < 100 && !ti; ++tries) {
                    const drv::TrackInstance& c = track.instances()[size_t(r.range(0, int(count) - 1))];
                    if (c.pass != 4 && c.article >= 0) ti = &c;
                }
                if (!ti) break;
                eye[0] = ti->matrix[12] + r.uni(-10, 10);
                eye[1] = ti->matrix[13] + (r.chance(0.2) ? r.uni(50, 400) : r.uni(1, 25));
                eye[2] = ti->matrix[14] + r.uni(-10, 10);
                yaw = r.uni(-3.14f, 3.14f);
            }
            yaw += r.uni(-0.3f, 0.3f);
            pitch = r.chance(0.1) ? r.uni(-1.4f, 1.4f) : r.uni(-0.4f, 0.2f);
            const float step = r.uni(0, 25);
            eye[0] += step * std::sin(yaw);
            eye[2] += step * std::cos(yaw);
            float cam[44] = {};  // o RCamera (0xb0 bytes): a matriz em +0, o fov em +0xa4
            camera_matrix(yaw, pitch, r.chance(0.1) ? r.uni(-0.3f, 0.3f) : 0.0f, eye, cam);
            const float fovs[] = {33.0f, 20.0f, 31.9f, 45.0f, 60.0f, 89.0f};
            const float fov = r.chance(0.7) ? fovs[r.range(0, 5)] : r.uni(10, 80);
            cam[0xa4 / 4] = fov;
            const int widescreen = r.chance(0.25) ? 1 : 0;
            const float vx0 = r.chance(0.5) ? 0.0f : r.uni(-10, 10), vx1 = r.chance(0.5) ? 640.0f : r.uni(300, 1000);
            e.put(kCam, cam, 0xb0);
            e.w32(kView, kCam);
            e.put(kView + 0x18, &vx0, 4);
            e.put(kView + 0x24, &vx1, 4);
            e.w32(kRenderer + 0x5c, uint32_t(widescreen));
            ++frame.cases;
            // O original: ConfigureView e a parte do mundo do PerformCulling.
            try {
                e.call(kFnConfigureView, {kView});
                e.call(kFnGenerate, {kRender, kCam}, 120.0f);
                e.call(kFnPrepare, {kScratch});
                e.call(kFnCullModule, {kView, kFnGetNextPoint, kFnSetCull, 1});
            } catch (const std::exception& ex) {
                frame.fail(std::string(name) + " quadro " + std::to_string(f) + ": o original falhou: " + ex.what());
                break;
            }
            // O porte.
            const uint64_t g0 = drv::world_vis_guards();
            drv::RPlayerViewCamera_ConfigureView(cam, fov, widescreen != 0);
            const float lod = drv::RefreshLODMultiplier(fov, vx0, vx1);
            drv::CachedDrawInfo* list = drv::GenerateCurtainsAndNodes(render, cam, 120.0f);
            drv::PrepareForCull(list);
            drv::RRenderWorldCamera_CullModule(drv::GetNextPoint, drv::SetCull);
            guards += drv::world_vis_guards() - g0;
            const std::string where = std::string(name) + " quadro " + std::to_string(f) + ": ";
            uint8_t ec[0xa0];
            e.get(kCulling, ec, 0xa0);
            uint32_t elod = e.r32(kView + 0x30);
            if (!same_bytes("fgWorldCulling", ec, &drv::fgWorldCulling(), 0xa0, d)) {
                frame.fail(where + d);
                continue;
            }
            if (elod != ps2::fbits(lod)) {
                frame.fail(where + "o multiplicador de LOD original " + hex(elod) + ", porte " + hex(ps2::fbits(lod)));
                continue;
            }
            const uint32_t ecount = e.r32(kCurtainCount);
            if (int32_t(ecount) != drv::numActiveCurtains()) {
                frame.fail(where + "as cortinas ativas original " + std::to_string(ecount) + ", porte " +
                           std::to_string(drv::numActiveCurtains()));
                continue;
            }
            if (!same_bytes("os planos na VU0", e.vu.data(), drv::vu0().data(), ecount * 64, d)) {
                frame.fail(where + d);
                continue;
            }
            drv::CachedDrawInfo el;
            e.get(kScratch, &el, sizeof el);
            if (el.node_count != list->node_count || el.draw_count != list->draw_count) {
                frame.fail(where + "a lista: nós " + std::to_string(el.node_count) + "/" + std::to_string(list->node_count) +
                           ", desenhos " + std::to_string(el.draw_count) + "/" + std::to_string(list->draw_count));
                continue;
            }
            if (el.node_count > drv::kMaxVisibleNodes) ++overflows;
            bool ok = true;
            for (int i = 0; ok && i < std::min(el.node_count, drv::kMaxVisibleNodes); ++i)
                if (tc.port(el.nodes[i]) != list->nodes[i]) {
                    frame.fail(where + "o nó " + std::to_string(i));
                    ok = false;
                }
            for (int i = 0; ok && i < el.draw_count; ++i)
                if (inst_port(el.draws[i].instance) != list->draws[i].instance ||
                    ps2::fbits(el.draws[i].distance) != ps2::fbits(list->draws[i].distance)) {
                    frame.fail(where + "o desenho " + std::to_string(i));
                    ok = false;
                }
            const drv::WCullCursor& cur = drv::Cursor();
            if (ok && (int32_t(e.r32(kCursorNode)) != cur.node || int32_t(e.r32(kCursorItem)) != cur.item ||
                       e.r32(kCursorList) != kScratch || cur.list != &drv::scratchpad_list() ||
                       e.r32(kCurrentList) != kScratch || drv::CurrentList() != &drv::scratchpad_list())) {
                frame.fail(where + "o cursor");
                ok = false;
            }
            if (!ok) continue;
            nodes += uint64_t(list->node_count);
            curtains += ecount;
            kept += uint64_t(list->draw_count);
            for (int i = 0; i < list->node_count; ++i) tested += nf::p32_ptr<const drv::MapNode>(list->nodes[i])->instance_count;
            // O std::sort do DrawPass (os dois tipos) e os CopyDrawPasses do DoRender.
            for (int sort = 1; sort <= 2; ++sort) {
                const int n = el.draw_count;
                ++sorts.cases;
                if (n == 0) continue;
                e.put(kSortBuf, el.draws, uint32_t(8 * n));
                int lg = 0;
                for (int k = n; k > 1; k >>= 1) ++lg;
                const uint32_t cmp = sort == 1 ? kCmpInstance : kCmpDistance;
                e.call(kFnIntrosort, {kSortBuf, kSortBuf + 8u * uint32_t(n), 0, uint32_t(2 * lg)}, 0, 0, cmp);
                e.call(kFnFinalInsertion, {kSortBuf, kSortBuf + 8u * uint32_t(n), cmp});
                drv::CachedDrawInfo pl = *list;
                drv::DrawPass_sort(pl, sort);
                std::vector<drv::InstanceAndDistance> o(static_cast<size_t>(n));
                e.get(kSortBuf, o.data(), uint32_t(8 * n));
                for (int i = 0; i < n; ++i)
                    if (inst_port(o[size_t(i)].instance) != pl.draws[i].instance ||
                        ps2::fbits(o[size_t(i)].distance) != ps2::fbits(pl.draws[i].distance)) {
                        sorts.fail(where + "tipo " + std::to_string(sort) + ", a entrada " + std::to_string(i));
                        break;
                    }
            }
            // Os objetos de cena: o PrepareSceneObjsForCulling e o CullModule deles (GetNextSceneObjCullInfo,
            // SetSceneObjectCull, SetNowVisible).
            {
                ++scene_frames.cases;
                e.call(kFnPrepareSceneObjs, {kScratch});
                e.call(kFnCullModule, {kView, kFnNextSceneObj, kFnSetSceneObjCull, 0});
                drv::PrepareSceneObjsForCulling(list);
                drv::RRenderWorldCamera_CullModule(drv::GetNextSceneObjCullInfo, drv::SetSceneObjectCull);
                const drv::SceneObjCullState& sc = drv::scene_obj_cull();
                bool same = int32_t(e.r32(kSceneNumVisible)) == sc.num_visible &&
                            int32_t(e.r32(kSceneNumLast)) == sc.num_draw_last &&
                            int32_t(e.r32(kSceneMax)) == sc.max_visible && int32_t(e.r32(kSceneNode)) == sc.node &&
                            e.r32(kSceneCurrent) == obj_emu(nf::p32_u(sc.current));
                for (int i = 0; same && i < sc.num_visible; ++i)
                    same = e.r32(kSceneVisible + 4u * uint32_t(i)) == obj_emu(sc.visible[i]);
                for (int i = 0; same && i < sc.num_draw_last; ++i)
                    same = e.r32(kSceneLast + 4u * uint32_t(i)) == obj_emu(sc.draw_last[i]);
                if (!same)
                    scene_frames.fail(where + "visíveis original " + std::to_string(e.r32(kSceneNumVisible)) +
                                      ", porte " + std::to_string(sc.num_visible));
                scene_visible += uint64_t(sc.num_visible);
            }
            const int ranges[4][2] = {{0, 0}, {1, 1}, {2, 3}, {0, 4}};
            for (const auto& rg : ranges) {
                ++copies.cases;
                e.call(kFnCopyPasses, {kRender, kScratch, kTo, uint32_t(rg[0])}, 0, 0, uint32_t(rg[1]));
                static drv::CachedDrawInfo pt;
                drv::CopyDrawPasses(render, *list, pt, rg[0], rg[1]);
                drv::CachedDrawInfo et;
                e.get(kTo, &et, sizeof et);
                bool same = et.draw_count == pt.draw_count;
                for (int i = 0; same && i < et.draw_count; ++i)
                    same = inst_port(et.draws[i].instance) == pt.draws[i].instance &&
                           ps2::fbits(et.draws[i].distance) == ps2::fbits(pt.draws[i].distance);
                if (!same) copies.fail(where + "passadas " + std::to_string(rg[0]) + "-" + std::to_string(rg[1]));
            }
        }
        drv::fgRender() = nullptr;
    }
    report("WTree::InitializeTree e as passadas do WRender", tree, failures);
    char extra[200];
    std::snprintf(extra, sizeof extra,
                  " (%" PRIu64 " pistas; por quadro: %.1f nós, %.1f cortinas, %.1f instâncias testadas, %.1f guardadas; "
                  "%" PRIu64 " além do limite de nós, %" PRIu64 " guardas)",
                  tracks, frame.cases ? double(nodes) / double(frame.cases) : 0.0,
                  frame.cases ? double(curtains) / double(frame.cases) : 0.0,
                  frame.cases ? double(tested) / double(frame.cases) : 0.0,
                  frame.cases ? double(kept) / double(frame.cases) : 0.0, overflows, guards);
    report("ConfigureView, GenerateCurtainsAndNodes, CullModule", frame, failures, extra);
    report("DrawPass_sort (as listas dos quadros)", sorts, failures);
    report("CopyDrawPasses", copies, failures);
    std::snprintf(extra, sizeof extra, " (%" PRIu64 " objetos nas 8 pistas)", scene_objs);
    report("RSceneObj::RSceneObj (WTree::FindNode, SetMapNode)", scene, failures, extra);
    std::snprintf(extra, sizeof extra, " (%.1f visíveis por quadro)",
                  scene_frames.cases ? double(scene_visible) / double(scene_frames.cases) : 0.0);
    report("o corte dos objetos de cena (GetNextSceneObjCullInfo...)", scene_frames, failures, extra);
}

// ------------------------------------------------------------------------------------------------- d) as entradas

void check_inputs(VisEmu& e, const nf::DiscSource& disc, int cases, uint32_t seed, int& failures) {
    Rng r(seed ^ 0x11647u);
    Tally time, dirs;
    const drv::RTimeData saved = drv::rtime_data();
    std::string d;
    for (int c = 0; c < cases; ++c) {
        const int32_t steps = c % 5 == 0 ? int32_t(r.next()) : c % 5 == 1 ? -int32_t(r.next() & 0x7fffffff) : r.range(0, 1 << 20);
        e.w32(kSimSteps, uint32_t(steps));
        ++time.cases;
        e.call(kFnTimeUpdate, {});
        drv::RTimeData_Update(steps);
        const drv::RTimeData& t = drv::rtime_data();
        if (e.r32(kTimeData) != ps2::fbits(t.time_data[0]) || e.r32(kTime) != t.time ||
            e.r32(kCausticTime) != ps2::fbits(t.caustic_time))
            time.fail("passo " + std::to_string(steps) + ": original " + hex(e.r32(kTimeData)) + ", porte " +
                      hex(ps2::fbits(t.time_data[0])));
    }
    auto check_dirs = [&](const float a[6], const std::string& what) {
        e.put(kArgs, a, 24);
        uint8_t zero[0x70] = {};
        e.put(kArgs + 0x40, zero, 0x70);
        ++dirs.cases;
        e.call(kFnLightDirections, {kArgs + 0x40, kArgs});
        drv::LightBlock b{};
        drv::RLightManager_SetLightDirections(b, a);
        uint8_t eb[0x70];
        e.get(kArgs + 0x40, eb, 0x70);
        if (!same_bytes("as direções", eb, &b, 0x70, d)) dirs.fail(what + ": " + d);
    };
    for (int c = 0; c < cases; ++c) {
        float a[6];
        for (float& v : a) v = r.chance(0.1) ? 0.0f : r.chance(0.2) ? float(r.range(-4, 4)) * 90.0f : r.uni(-400, 400);
        check_dirs(a, "caso " + std::to_string(c));
    }
    // Os ângulos dos quatro conjuntos de cada pista (o .tun de Render\Lighting).
    for (const char* name : kTracks) {
        drv::Bigf arc;
        if (!arc.open(disc, "DRIVING/" + drv::track_viv_name(name))) continue;
        const int idx = arc.find(std::string("data\\tuning\\Render\\Lighting\\") + name + ".tun");
        std::vector<uint8_t> bytes;
        if (idx < 0 || !arc.read(idx, bytes)) continue;
        drv::RLightManagerLights lm;
        drv::RLightManager_Construct(lm, std::string(bytes.begin(), bytes.end()));
        for (int k = 0; k < 4; ++k) check_dirs(lm.angles[k], std::string(name) + " conjunto " + std::to_string(k));
    }
    drv::rtime_data() = saved;
    report("RTimeData::Update", time, failures);
    report("RLightManager: as direções (BuildRotate, TransformPoint)", dirs, failures);
}

}  // namespace

int run_vis_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* program, uint32_t program_size,
                   int cases, int frames, uint32_t seed) {
    std::printf("\n== a visibilidade do mundo: o código original (VU0_culling e o EE) x o porte (parte 11) ==\n");
    int failures = 0;
    const drv::Vu0Mode mode = drv::vu0_mode();
    drv::set_vu0_mode(drv::Vu0Mode::Native);
    const std::vector<uint8_t> prog(program, program + program_size);
    check_microcode(prog, cases, seed, failures);
    VisEmu* e = new VisEmu();  // 32 MB de RAM
    try {
        e->setup(elf, program, program_size);
        // Os construtores globais de RViewCamera::fCulling (RRenderWorldCulling(33, 400)): o estado da partida.
        e->call(kFnCullingStaticCtor, {1, 0xffff});
        uint8_t ec[0xa0];
        e->get(kCulling, ec, 0xa0);
        std::string d;
        Tally boot;
        ++boot.cases;
        drv::RRenderWorldCulling fresh{};
        drv::RRenderWorldCulling_Construct(fresh, 33.0f, 400.0f);
        if (!same_bytes("o fgWorldCulling da partida", ec, &fresh, 0xa0, d)) boot.fail(d);
        report("RRenderWorldCulling(33, 400) da partida", boot, failures);
        drv::fgWorldCulling() = fresh;
        check_functions(*e, cases, seed, failures);
        check_tracks(*e, disc, frames, seed, failures);
        check_inputs(*e, disc, cases, seed, failures);
    } catch (const std::exception& ex) {
        std::printf("  falhou: %s\n", ex.what());
        ++failures;
    }
    delete e;
    drv::set_vu0_mode(mode);
    return failures;
}

}  // namespace drvcheck
