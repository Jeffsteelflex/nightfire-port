// nf_driving_check: a física das missões de carro conferida contra o DRIVING.ELF (docs/driving.md, seção 11).
//
//   nf_driving_check <raiz dos dados> [--casos N] [--quadros N] [--semente N] [--consultas N]
//
// 1. Lê os três microprogramas da VU0 do .vutext do DRIVING.ELF e confere que o interpretador (platform/vu_interp.h)
//    conhece todas as instruções deles e dos render methods do EAGL (out/driving/files/MISC.VIV/data/render/*.o, se
//    extraídos com tools/driving_data.py).
// 2. Roda o microcódigo ORIGINAL do VU0_physics no interpretador e o porte à mão (driving/physics/vu0_physics.cpp)
//    a partir do mesmo estado, em milhares de casos (aleatórios em faixas realistas, mais casos de borda), e compara
//    o estado inteiro da VU0 depois (registradores, ACC, VI, Q e a memória de dados): a maior diferença em ULPs por
//    saída e os casos que diferem.
// 3. Roda o passo inteiro da simulação (ESimFrameUpdate) de um cenário de caixas caindo num chão plano duas vezes, uma
//    com o microcódigo no interpretador e outra com o porte, e compara os corpos, os RigidBodyInfo e a VU0 quadro a
//    quadro.
// 4. Roda as funções ORIGINAIS do EE (o código MIPS do DRIVING.ELF, no interpretador MIPS do port com a VU0 ligada)
//    ao lado do porte em C++ e compara a memória (ver run_ee_checks).
//
// Tudo aqui lê dados do jogo: a saída é um relatório, nada é gravado.

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "data/elf.h"
#include "platform/arena.h"
#include "platform/disc_source.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "platform/crash_report.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

#include "driving_check_ee.h"
#include "driving_check_camera.h"
#include "driving_check_input.h"
#include "driving_check_sched.h"
#include "driving_check_car.h"
#include "driving_check_collide.h"
#include "driving_check_world.h"
#include "platform/fmath.h"

namespace {

using ps2::VuMachine;
using ps2::VuUnit;

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------------------------- os programas

// Um programa da VU0: o código a partir da instrução 0 (pares de 64 bits).
struct VuProgram {
    std::vector<uint8_t> code;
    uint32_t dma_address = 0;
    uint32_t instructions() const { return uint32_t(code.size() / 8); }
};

// Lê as cadeias de DMA do .vutext (uma tag CNT com comandos MPG da VIF e uma END vazia por programa).
std::vector<VuProgram> load_vu0_programs(const nf::Elf& elf) {
    std::vector<VuProgram> out;
    const nf::ElfSection* vt = nullptr;
    for (const auto& s : elf.sections())
        if (s.name == ".vutext") vt = &s;
    if (!vt) return out;
    nf::ByteView d(elf.file());
    const uint32_t off = vt->offset, size = vt->size;
    VuProgram cur;
    uint32_t p = 0;
    while (p + 16 <= size) {
        const uint64_t tag = nf::u64(d, off + p);
        if (tag == 0) {
            p += 16;
            continue;
        }
        const uint32_t qwc = uint32_t(tag & 0xffff), id = uint32_t((tag >> 28) & 7);
        if (cur.code.empty() && cur.dma_address == 0) cur.dma_address = vt->addr + p;
        uint32_t q = p + 8;
        const uint32_t end = p + 16 + qwc * 16;
        while (q < end) {
            const uint32_t w = nf::u32(d, off + q);
            q += 4;
            const uint32_t cmd = (w >> 24) & 0x7f, num = (w >> 16) & 0xff, imm = w & 0xffff;
            if (cmd == 0x4a) {
                const uint32_t n = num ? num : 256;
                if (cur.code.size() < (imm + n) * 8) cur.code.resize((imm + n) * 8);
                std::memcpy(cur.code.data() + imm * 8, d.data() + off + q, n * 8);
                q += n * 8;
            }
        }
        p = end;
        if (id == 7) {
            out.push_back(std::move(cur));
            cur = VuProgram{};
        }
    }
    return out;
}

// O microprograma da VU1 de um render method do EAGL (o tools/vudis.py load_eagl_method em C++).
bool load_eagl_method(const nf::Elf& elf, const std::string& symbol, std::vector<uint8_t>& code) {
    const nf::ElfSection* data = nullptr;
    for (const auto& s : elf.sections())
        if (s.name == ".data") data = &s;
    auto sym = elf.find(symbol);
    if (!data || !sym) return false;
    nf::ByteView d(elf.file());
    const uint32_t base = data->offset;
    const uint32_t count = nf::u32(d, base + sym->value);
    std::vector<std::pair<uint32_t, uint32_t>> chunks;
    uint32_t total = 0;
    for (int64_t a = int64_t(sym->value) - 4; a > std::max<int64_t>(int64_t(sym->value) - 0x1000, 0); a -= 4) {
        const uint32_t w = nf::u32(d, base + uint32_t(a));
        if (((w >> 24) & 0x7f) != 0x4a) continue;
        const uint64_t tag = nf::u64(d, base + uint32_t(a) + 4);
        if (((tag >> 28) & 7) != 3) continue;
        const uint32_t n = ((w >> 16) & 0xff) ? ((w >> 16) & 0xff) : 256;
        chunks.push_back({uint32_t(tag >> 32), n});
        total += n;
        if (total >= count) break;
    }
    if (total != count || chunks.empty()) return false;
    std::reverse(chunks.begin(), chunks.end());
    code.assign(d.data() + base + chunks[0].first, d.data() + base + chunks[0].first + count * 8);
    return true;
}

// ---------------------------------------------------------------------------------------------- comparação

int64_t order(uint32_t b) {
    const int64_t m = b & 0x7fffffff;
    return (b & 0x80000000u) ? -m : m;
}
uint64_t ulps(uint32_t a, uint32_t b) {
    const int64_t d = order(a) - order(b);
    return uint64_t(d < 0 ? -d : d);
}

struct Diff {
    std::map<std::string, uint64_t> max_ulps;  // por saída (só as que diferiram)
    uint64_t cases = 0, mismatched = 0;
    void add_case(bool same) {
        ++cases;
        if (!same) ++mismatched;
    }
};

// Compara dois estados da VU0 (sem os indicadores, que o porte não mantém). Devolve se são iguais bit a bit.
bool compare_vu(const VuMachine& a, const VuMachine& b, Diff& diff) {
    bool same = true;
    const char* lanes = "xyzw";
    const ps2::VuRegs& ra = a.regs();
    const ps2::VuRegs& rb = b.regs();
    for (int r = 0; r < 32; ++r)
        for (int l = 0; l < 4; ++l)
            if (ra.vf[r][l] != rb.vf[r][l]) {
                same = false;
                char name[16];
                std::snprintf(name, sizeof(name), "vf%02d.%c", r, lanes[l]);
                uint64_t& m = diff.max_ulps[name];
                m = std::max(m, ulps(ra.vf[r][l], rb.vf[r][l]));
            }
    for (int l = 0; l < 4; ++l)
        if (ra.acc[l] != rb.acc[l]) {
            same = false;
            char name[16];
            std::snprintf(name, sizeof(name), "ACC.%c", lanes[l]);
            uint64_t& m = diff.max_ulps[name];
            m = std::max(m, ulps(ra.acc[l], rb.acc[l]));
        }
    for (int i = 0; i < 16; ++i)
        if (ra.vi[i] != rb.vi[i]) {
            same = false;
            diff.max_ulps["vi" + std::to_string(i)] = 1;
        }
    if (ra.q != rb.q) {
        same = false;
        uint64_t& m = diff.max_ulps["Q"];
        m = std::max(m, ulps(ra.q, rb.q));
    }
    for (uint32_t n = 0; n < a.data_size() / 16; ++n) {
        const uint32_t* qa = const_cast<VuMachine&>(a).qword(n);
        const uint32_t* qb = const_cast<VuMachine&>(b).qword(n);
        for (int l = 0; l < 4; ++l)
            if (qa[l] != qb[l]) {
                same = false;
                char name[24];
                std::snprintf(name, sizeof(name), "mem[%u].%c", n, lanes[l]);
                uint64_t& m = diff.max_ulps[name];
                m = std::max(m, ulps(qa[l], qb[l]));
            }
    }
    return same;
}

void print_diff(const char* what, const Diff& d) {
    std::printf("  %-44s %6" PRIu64 " casos, %" PRIu64 " diferentes", what, d.cases, d.mismatched);
    if (d.max_ulps.empty()) {
        std::printf(" (bit a bit)\n");
        return;
    }
    std::printf("; maior diferença por saída (ULPs):");
    int shown = 0;
    for (const auto& [k, v] : d.max_ulps) {
        if (shown++ >= 12) {
            std::printf(" ...");
            break;
        }
        std::printf(" %s=%" PRIu64, k.c_str(), v);
    }
    std::printf("\n");
}

// ---------------------------------------------------------------------------------------------- estados aleatórios

struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t seed) : g(seed) {}
    float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g); }
    uint32_t bits() { return g(); }
    int range(int a, int b) { return std::uniform_int_distribution<int>(a, b)(g); }
    bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(g) < p; }
};

void set4(uint32_t* r, float x, float y, float z, float w) {
    r[0] = ps2::fbits(x);
    r[1] = ps2::fbits(y);
    r[2] = ps2::fbits(z);
    r[3] = ps2::fbits(w);
}

// Lixo em todos os registradores e na memória (as pistas que os programas não escrevem precisam sair iguais).
void fill_garbage(VuMachine& m, Rng& rng) {
    ps2::VuRegs& r = m.regs();
    for (int i = 1; i < 32; ++i)
        for (int l = 0; l < 4; ++l) r.vf[i][l] = ps2::fbits(rng.uni(-100, 100));
    for (int l = 0; l < 4; ++l) r.acc[l] = ps2::fbits(rng.uni(-100, 100));
    for (int i = 1; i < 16; ++i) r.vi[i] = uint16_t(rng.bits());
    r.q = ps2::fbits(rng.uni(-10, 10));
    r.i = ps2::fbits(rng.uni(-10, 10));
    for (uint32_t n = 0; n < m.data_size() / 16; ++n)
        for (int l = 0; l < 4; ++l) m.qword(n)[l] = ps2::fbits(rng.uni(-50, 50));
}

void random_quat(Rng& rng, float q[4]) {
    double v[4], n = 0;
    for (double& x : v) {
        x = rng.uni(-1, 1);
        n += x * x;
    }
    n = std::sqrt(n) + 1e-9;
    for (int i = 0; i < 4; ++i) q[i] = float(v[i] / n);
}

// Um corpo rígido realista em vf20..vf27 (e o tensor em três registradores a partir de tensor_reg).
void random_body(VuMachine& m, Rng& rng, int tensor_reg, int mode) {
    ps2::VuRegs& r = m.regs();
    float q[4];
    random_quat(rng, q);
    const float mass = mode == 2 ? 1660.0f : rng.uni(1.0f, 3000.0f);
    set4(r.vf[20], q[0], q[1], q[2], q[3]);
    set4(r.vf[21], rng.uni(-500, 500), rng.uni(-20, 50), rng.uni(-500, 500), rng.uni(1e-4f, 2.0f));
    set4(r.vf[22], rng.uni(-50, 50), rng.uni(-30, 10), rng.uni(-50, 50), rng.uni(1e-4f, 2.0f));
    set4(r.vf[23], rng.uni(-12, 12), rng.uni(-12, 12), rng.uni(-12, 12), rng.uni(1e-4f, 2.0f));
    const float pscale = mode == 1 ? 130.0f : 60.0f;  // modo 1: passa do limite de 100 x massa
    set4(r.vf[24], rng.uni(-pscale, pscale) * mass, rng.uni(-pscale, pscale) * mass, rng.uni(-pscale, pscale) * mass,
         mass);
    set4(r.vf[25], rng.uni(-50, 50), rng.uni(-50, 50), rng.uni(-50, 50), 0);
    r.vf[25][3] = rng.bits();  // o ponteiro do RigidBodyInfo
    set4(r.vf[26], rng.uni(-40, 40) * mass, rng.uni(-40, 10) * mass, rng.uni(-40, 40) * mass, 0);
    r.vf[26][3] = rng.bits();  // os bytes de estado
    set4(r.vf[27], rng.uni(-100, 100), rng.uni(-100, 100), rng.uni(-100, 100), rng.uni(0.5f, 5.0f));
    for (int i = 0; i < 3; ++i)
        set4(r.vf[tensor_reg + i], rng.uni(-0.5f, 2.0f), rng.uni(-0.5f, 2.0f), rng.uni(-0.5f, 2.0f), rng.uni(-1, 1));
}

void setup_update(VuMachine& m, Rng& rng, int kind) {
    fill_garbage(m, rng);
    ps2::VuRegs& r = m.regs();
    random_body(m, rng, 11, kind % 3);
    const float mass = ps2::ffloat(r.vf[24][3]);
    set4(r.vf[4], ps2::ffloat(0x3c888888u), 0.5f, 1000.0f, 0.0f);
    set4(r.vf[5], ps2::ee::mul(-100.0f, mass), ps2::ee::mul(100.0f, mass), -10.0f, 10.0f);
    for (uint32_t i = 0; i < 16; ++i)
        set4(m.qword(i), rng.uni(-3, 3), rng.uni(-1.5f, 1.5f), rng.uni(-3, 3), ps2::ffloat(rng.bits() & 0x3f7fffff));
    // A orientação, a transposta e a inércia como o EE as monta.
    float q[4];
    for (int l = 0; l < 4; ++l) q[l] = ps2::ffloat(r.vf[20][l]);
    float rot[4][4] = {};
    // Uma rotação aproximada (só precisa ser plausível).
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double mm[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)},
                             {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)},
                             {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) rot[i][j] = float(mm[i][j]);
    for (int i = 0; i < 3; ++i) set4(m.qword(16 + i), rot[0][i], rot[1][i], rot[2][i], 0);
    set4(m.qword(19), ps2::ee::div(1.0f, ps2::ffloat(r.vf[21][3])), ps2::ee::div(1.0f, ps2::ffloat(r.vf[22][3])),
         ps2::ee::div(1.0f, ps2::ffloat(r.vf[23][3])), 0);
    for (int i = 0; i < 3; ++i) set4(m.qword(20 + i), rot[i][0], rot[i][1], rot[i][2], 0);
    if (kind == 3) {  // parado: momento, velocidade angular e torque zerados (dq = 0)
        set4(r.vf[24], 0, 0, 0, mass);
        set4(r.vf[25], 0, 0, 0, 0);
        set4(r.vf[26], 0, 0, 0, 0);
        set4(r.vf[27], 0, 0, 0, 0);
    }
}

void setup_levers(VuMachine& m, Rng& rng, int kind) {
    fill_garbage(m, rng);
    ps2::VuRegs& r = m.regs();
    random_body(m, rng, 17, kind % 3);
    const int count = kind == 4 ? 12 : rng.range(1, 12);
    r.vi[2] = uint16_t(count);
    for (int i = 0; i < count; ++i) {
        set4(m.qword(uint32_t(i)), rng.uni(-2.5f, 2.5f), rng.uni(-1.2f, 0.2f), rng.uni(-2.5f, 2.5f),
             ps2::ffloat(rng.bits() & 0x3f7fffff));
        // Uma normal unitária, quase sempre para cima (kind 5: para baixo, o impulso não é aplicado).
        double n[3] = {rng.uni(-0.3f, 0.3f), kind == 5 ? -1.0 : 1.0, rng.uni(-0.3f, 0.3f)};
        if (kind == 6) n[0] = n[2] = 0;  // vertical exata
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        set4(m.qword(12u + uint32_t(i)), float(n[0] / len), float(n[1] / len), float(n[2] / len),
             rng.uni(0.0f, 0.5f));
    }
    const float mass = ps2::ffloat(r.vf[24][3]);
    set4(m.qword(27), 0.0f, ps2::ee::mul(ps2::ee::div(mass, 8.0f), 0.05f), kind == 7 ? 0.0025f : 0.05f, 10.0f);
    if (kind == 3) {  // tudo parado: a velocidade tangencial é zero (RSQRT de zero)
        set4(r.vf[22], 0, 0, 0, ps2::ffloat(r.vf[22][3]));
        set4(r.vf[23], 0, 0, 0, ps2::ffloat(r.vf[23][3]));
    }
}

void setup_quat(VuMachine& m, Rng& rng, int kind) {
    fill_garbage(m, rng);
    float q[4];
    random_quat(rng, q);
    const float s = kind == 1 ? rng.uni(0.1f, 3.0f) : 1.0f;  // não unitário
    set4(m.regs().vf[1], q[0] * s, q[1] * s, q[2] * s, q[3] * s);
}

void setup_matrix(VuMachine& m, Rng& rng, int) { fill_garbage(m, rng); }

// Estado totalmente aleatório nos bits (denormais, expoente 255): a aritmética precisa coincidir mesmo assim.
void wild(VuMachine& m, Rng& rng, int entry) {
    ps2::VuRegs& r = m.regs();
    for (int i = 1; i < 32; ++i)
        for (int l = 0; l < 4; ++l) r.vf[i][l] = rng.bits();
    for (int l = 0; l < 4; ++l) r.acc[l] = rng.bits();
    for (uint32_t n = 0; n < m.data_size() / 16; ++n)
        for (int l = 0; l < 4; ++l) m.qword(n)[l] = rng.bits();
    if (entry == 0x34) r.vi[2] = uint16_t(rng.range(1, 12));
}

// ---------------------------------------------------------------------------------------------- (2) microcódigo

void check_microcode(const VuProgram& prog, int cases, uint32_t seed) {
    std::printf("\n== microcódigo do VU0_physics: o original no interpretador x o porte à mão ==\n");
    struct Entry {
        uint32_t instr;
        const char* name;
        void (*setup)(VuMachine&, Rng&, int);
        int kinds;
    };
    const Entry entries[] = {
        {0x000, "0x000 quatérnio em matriz", setup_quat, 2},
        {0x022, "0x022 VU0MICRO_MultiplyMatrix", setup_matrix, 1},
        {0x034, "0x034 VU0MICRO_ResolveLeverForces", setup_levers, 8},
        {0x0ad, "0x0ad VU0MICRO_UpdatePositionAndOrientation", setup_update, 4},
    };
    uint64_t total_cases = 0, total_bad = 0;
    for (const Entry& e : entries) {
        Diff realistic, wild_diff;
        Rng rng(seed ^ e.instr);
        for (int c = 0; c < cases; ++c) {
            VuMachine a(VuUnit::VU0);
            a.load_micro(0, prog.code.data(), uint32_t(prog.code.size()));
            const bool is_wild = c % 8 == 7;
            if (is_wild) {
                e.setup(a, rng, 0);
                wild(a, rng, int(e.instr));
            } else {
                e.setup(a, rng, c % e.kinds);
            }
            VuMachine b = a;
            bool ok = true;
            try {
                a.run(e.instr, 2'000'000);
            } catch (const ps2::VuError& err) {
                std::printf("  ERRO no interpretador: %s\n", err.what());
                ok = false;
            }
            drv::vu0_physics_native(b, e.instr);
            const bool same = ok && compare_vu(a, b, is_wild ? wild_diff : realistic);
            (is_wild ? wild_diff : realistic).add_case(same);
        }
        print_diff((std::string(e.name) + " (realistas)").c_str(), realistic);
        print_diff((std::string(e.name) + " (bits aleatórios)").c_str(), wild_diff);
        total_cases += realistic.cases + wild_diff.cases;
        total_bad += realistic.mismatched + wild_diff.mismatched;
    }
    std::printf("  total: %" PRIu64 " casos, %" PRIu64 " diferentes\n", total_cases, total_bad);
}

// ---------------------------------------------------------------------------------------------- (3) o passo inteiro

struct Snapshot {
    std::vector<uint8_t> bytes;
};

// O estado observável de uma execução: os corpos (sem o ponteiro do info), os RigidBodyInfo (sem os ponteiros dos
// WWorldPos), a VU0 (registradores e memória de dados).
Snapshot snapshot(drv::Simulation& sim) {
    Snapshot s;
    for (int i = 0; i < drv::kRigidBodySlots; ++i) {
        if (!sim.owners[i]) continue;
        drv::RigidBody rb = *drv::GetRigidBody(sim, i);
        rb.info = nullptr;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&rb);
        s.bytes.insert(s.bytes.end(), p, p + sizeof(rb));
        drv::RigidBodyInfo info = sim.infos[i];
        for (auto& w : info.lever_pos) w = nullptr;
        info.owner_pos = nullptr;
        const uint8_t* q = reinterpret_cast<const uint8_t*>(&info);
        s.bytes.insert(s.bytes.end(), q, q + sizeof(info));
    }
    const ps2::VuRegs& r = drv::vu0().regs();
    ps2::VuRegs copy = r;
    copy.mac = copy.status = copy.clip = 0;
    for (int i = 0; i < 32; ++i)  // vf25.w e vf26.w guardam ponteiros e bytes de estado; ficam (são iguais)
        (void)i;
    const uint8_t* rr = reinterpret_cast<const uint8_t*>(&copy);
    s.bytes.insert(s.bytes.end(), rr, rr + sizeof(copy));
    const uint8_t* mem = drv::vu0().data();
    s.bytes.insert(s.bytes.end(), mem, mem + drv::vu0().data_size());
    return s;
}

struct ScenarioBody {
    float half[3];
    float mass;
    float pos[3];
    float vel[3];
    float ang_vel[3];
    float tilt;  // ângulo em torno de x e z
    int moving;
};

const ScenarioBody kScenario[] = {
    {{0.5f, 0.5f, 0.5f}, 100.0f, {0, 3.0f, 0}, {0, 0, 0}, {0, 0, 0}, 0.0f, 1},              // um cubo caindo reto
    {{1.0f, 0.25f, 2.0f}, 800.0f, {5, 6.0f, 0}, {2, 0, -1}, {0.5f, 1.5f, -0.3f}, 0.4f, 1},   // uma prancha girando
    {{0.3f, 0.3f, 0.3f}, 20.0f, {-4, 10.0f, 3}, {-3, 5, 2}, {4, -2, 6}, 0.9f, 1},            // uma caixa jogada
    {{0.9f, 0.6f, 2.2f}, 1660.0f, {0, 2.0f, 8}, {0, 0, 12}, {0, 0, 0}, 0.05f, 1},            // o tamanho de um carro
    {{0.5f, 0.5f, 0.5f}, 50.0f, {8, 0.49f, 8}, {0, 0, 0}, {0, 0, 0}, 0.0f, 0},               // nasce dormindo no chão
};

void build_scenario(drv::Simulation& sim) {
    for (const ScenarioBody& sb : kScenario) {
        drv::RigidBodyParams p;
        p.mass = sb.mass;
        p.type = 4;
        std::memcpy(p.pos, sb.pos, 12);
        std::memcpy(p.vel, sb.vel, 12);
        std::memcpy(p.ang_vel, sb.ang_vel, 12);
        p.half_size[0] = sb.half[0];
        p.half_size[1] = sb.half[1];
        p.half_size[2] = sb.half[2];
        p.half_size[3] = 0;
        // O tensor inverso de uma caixa: I = m / 3 (b² + c²) com as meias-medidas.
        const float hx2 = sb.half[0] * sb.half[0], hy2 = sb.half[1] * sb.half[1], hz2 = sb.half[2] * sb.half[2];
        p.inv_inertia[0] = 3.0f / (sb.mass * (hy2 + hz2));
        p.inv_inertia[1] = 3.0f / (sb.mass * (hx2 + hz2));
        p.inv_inertia[2] = 3.0f / (sb.mass * (hx2 + hy2));
        const float c = nf::fm::cos(sb.tilt), s = nf::fm::sin(sb.tilt);
        // Rotação em torno de x e depois de z (linhas).
        const float rx[3][3] = {{1, 0, 0}, {0, c, s}, {0, -s, c}};
        const float rz[3][3] = {{c, s, 0}, {-s, c, 0}, {0, 0, 1}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float v = 0;
                for (int k = 0; k < 3; ++k) v += rx[i][k] * rz[k][j];
                p.orient[i][j] = v;
            }
        p.moving = sb.moving;
        drv::simulation_add_body(sim, p);
    }
}

struct RunResult {
    std::vector<Snapshot> frames;
    std::vector<std::vector<float>> final_pos;
    std::string pending;
};

RunResult run_scenario(drv::Vu0Mode mode, int frames, drv::FlatGround& ground) {
    RunResult out;
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    drv::set_vu0_mode(mode);
    drv::simulation_reset(60);
    drv::vu0_reset();
    drv::set_world(&ground);
    drv::InitRigidBodySystem("paris_mis01", nullptr);
    drv::Simulation& sim = drv::simulation();
    build_scenario(sim);
    for (int f = 0; f < frames; ++f) {
        drv::ESimFrameUpdate(sim);
        out.frames.push_back(snapshot(sim));
    }
    for (int i = 0; i < drv::kRigidBodySlots; ++i)
        if (sim.owners[i]) {
            const drv::RigidBody* rb = drv::GetRigidBody(sim, i);
            out.final_pos.push_back({rb->pos[0], rb->pos[1], rb->pos[2], float(rb->state), float(rb->ground_levers)});
        }
    out.pending = drv::physics_pending_report();
    drv::set_world(nullptr);
    return out;
}

void check_whole_step(const VuProgram& prog, int frames) {
    std::printf("\n== o passo inteiro (ESimFrameUpdate), %d quadros: microcódigo no interpretador x porte ==\n", frames);
    drv::set_vu0_physics_program(prog.code);
    drv::FlatGround ground_a(0.0f), ground_b(0.0f);
    RunResult native = run_scenario(drv::Vu0Mode::Native, frames, ground_a);
    RunResult interp = run_scenario(drv::Vu0Mode::Interpreted, frames, ground_b);
    drv::set_vu0_mode(drv::Vu0Mode::Native);
    int first_diff = -1;
    for (int f = 0; f < frames; ++f)
        if (native.frames[size_t(f)].bytes != interp.frames[size_t(f)].bytes) {
            first_diff = f;
            break;
        }
    if (first_diff < 0)
        std::printf("  %d quadros x %zu corpos: corpos, RigidBodyInfo e VU0 bit a bit iguais em todos os quadros\n",
                    frames, native.final_pos.size());
    else
        std::printf("  DIFERENTES a partir do quadro %d\n", first_diff);
    std::printf("  consultas ao chão: %u (porte) e %u (interpretador)\n", ground_a.queries, ground_b.queries);
    for (size_t i = 0; i < native.final_pos.size(); ++i) {
        const auto& p = native.final_pos[i];
        std::printf("  corpo %zu: meias-medidas %.2f/%.2f/%.2f, fim em (%.4f, %.4f, %.4f), estado %d, alavancas no "
                    "chão %d\n",
                    i, kScenario[i].half[0], kScenario[i].half[1], kScenario[i].half[2], p[0], p[1], p[2], int(p[3]),
                    int(p[4]));
    }
    std::printf("  choques contra o chão (eventos ECollision não portados): %u\n", drv::ground_impacts().events);
    std::printf("  pendentes (código não portado chamado): %s\n", native.pending.c_str());
}

// ---------------------------------------------------------------------------------------------- (1) cobertura

void check_coverage(const std::vector<VuProgram>& progs, const std::string& root) {
    std::printf("\n== cobertura do interpretador das VUs ==\n");
    const char* names[3] = {"VU0_charAnim", "VU0_physics", "VU0_culling"};
    for (size_t i = 0; i < progs.size(); ++i) {
        uint32_t unknown = 0;
        for (uint32_t k = 0; k < progs[i].instructions(); ++k) {
            uint32_t lo, up;
            std::memcpy(&lo, progs[i].code.data() + k * 8, 4);
            std::memcpy(&up, progs[i].code.data() + k * 8 + 4, 4);
            if (!VuMachine::decodes(lo, up)) ++unknown;
        }
        std::printf("  VU0 programa %zu (%s, DMA 0x%06x): %u instruções, %u desconhecidas\n", i,
                    i < 3 ? names[i] : "?", progs[i].dma_address, progs[i].instructions(), unknown);
    }
    for (const char* obj : {"eaglrm.o", "bondrm.o"}) {
        const std::string path = root + "/out/driving/files/MISC.VIV/data/render/" + obj;
        std::vector<uint8_t> bytes = read_file(path);
        if (bytes.empty()) {
            std::printf("  %s: não extraído (tools/driving_data.py extract MISC.VIV ...): pulado\n", obj);
            continue;
        }
        nf::Elf elf(std::move(bytes));
        uint32_t methods = 0, instrs = 0, unknown = 0, failed = 0;
        for (const auto& s : elf.symbols()) {
            if (s.name.size() < 17 || s.name.compare(s.name.size() - 16, 16, "0__EAGLMicroCode") != 0) continue;
            std::vector<uint8_t> code;
            if (!load_eagl_method(elf, s.name, code)) {
                ++failed;
                continue;
            }
            ++methods;
            for (uint32_t k = 0; k < code.size() / 8; ++k) {
                uint32_t lo, up;
                std::memcpy(&lo, code.data() + k * 8, 4);
                std::memcpy(&up, code.data() + k * 8 + 4, 4);
                ++instrs;
                if (!VuMachine::decodes(lo, up)) ++unknown;
            }
        }
        std::printf("  VU1 %s: %u render methods, %u instruções, %u desconhecidas%s\n", obj, methods, instrs, unknown,
                    failed ? " (alguns métodos não abriram)" : "");
    }
}

}  // namespace

int main_body(int argc, char** argv) {
    nf::install_crash_report();
    if (argc < 2) {
        std::printf("uso: nf_driving_check <raiz dos dados> [--casos N] [--quadros N] [--semente N] [--consultas N] "
                    "[--so camera|agenda|entrada]\n");
        return 1;
    }
    const std::string root = argv[1];
    int cases = 4000, frames = 600, queries = 0;
    uint32_t seed = 20261011;
    std::string only;  // --so camera: só a parte 8; --so agenda: só a parte 9; --so entrada: só a parte 10
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--casos") == 0) cases = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--quadros") == 0) frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--semente") == 0) seed = uint32_t(std::strtoul(argv[++i], nullptr, 0));
        else if (std::strcmp(argv[i], "--consultas") == 0) queries = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--so") == 0) only = argv[++i];
    }
    // O disco do jogador (a pasta, uma imagem .iso, uma unidade); os objetos extraídos de out/ ficam na pasta dele.
    std::string disc_error;
    const std::shared_ptr<nf::DiscSource> disc = nf::open_disc(std::wstring(root.begin(), root.end()), &disc_error);
    std::vector<uint8_t> bytes;
    if (disc == nullptr || !disc->read("DRIVING.ELF", bytes) || bytes.empty()) {
        std::printf("sem %s/DRIVING.ELF\n", root.c_str());
        return 1;
    }
    nf::Elf elf(bytes);
    std::vector<VuProgram> progs = load_vu0_programs(elf);
    if (progs.size() != 3) {
        std::printf("o .vutext tem %zu programas (esperados 3)\n", progs.size());
        return 1;
    }
    if (only == "entrada") {
        const int f = drvcheck::run_input_checks(elf, *disc, frames * 4, seed);
        std::printf("\nfim\n");
        return f ? 2 : 0;
    }
    if (only == "agenda") {
        const int f = drvcheck::run_sched_checks(elf, cases, seed);
        std::printf("\nfim\n");
        return f ? 2 : 0;
    }
    if (only == "camera") {
        const int f = drvcheck::run_camera_checks(elf, *disc, progs[1].code.data(), uint32_t(progs[1].code.size()),
                                                  frames * 2, seed);
        std::printf("\nfim\n");
        return f ? 2 : 0;
    }
    {
        const std::wstring folder = disc->folder();
        std::string base;
        for (wchar_t c : folder) base.push_back(c < 128 ? char(c) : '?');
        check_coverage(progs, disc->kind() == nf::DiscKind::Folder ? root : base);
    }
    check_microcode(progs[1], cases, seed);
    check_whole_step(progs[1], frames);
    const int ee_failures = drvcheck::run_ee_checks(elf, progs[1].code, cases, seed);
    // O chão das pistas: `--consultas N` por pista (o padrão, um quarto dos casos).
    const int world_failures =
        drvcheck::run_world_checks(elf, *disc, queries > 0 ? queries : std::max(50, cases / 4), seed);
    // O carro sobre o chão de Paris: tantos casos quanto as consultas por pista.
    const int car_failures = drvcheck::run_car_checks(elf, *disc, progs[1].code.data(), uint32_t(progs[1].code.size()),
                                                      queries > 0 ? queries : std::max(50, cases / 4), seed);
    // Os choques (a colisão com os prédios de Paris e entre dois corpos): tantos casos quanto as consultas por pista.
    const int collide_failures =
        drvcheck::run_collide_checks(elf, *disc, progs[1].code.data(), uint32_t(progs[1].code.size()),
                                     queries > 0 ? queries : std::max(50, cases / 4), seed);
    // A câmera do jogador: as tabelas do camera.ini, o construtor e `--quadros` x 2 quadros por carro.
    const int camera_failures = drvcheck::run_camera_checks(elf, *disc, progs[1].code.data(),
                                                            uint32_t(progs[1].code.size()), frames * 2, seed);
    // O escalonador: `--casos` operações sorteadas.
    const int sched_failures = drvcheck::run_sched_checks(elf, cases, seed);
    // A entrada: os .def do controle e `--quadros` x 4 quadros de controle por tabela.
    const int input_failures = drvcheck::run_input_checks(elf, *disc, frames * 4, seed);
    std::printf("\nfim\n");
    return ee_failures || world_failures || car_failures || collide_failures || camera_failures || sched_failures ||
                   input_failures
               ? 2
               : 0;
}

int main(int argc, char** argv) {
    // 64 bits: na pilha da arena (platform/arena.h); 32 bits: direto.
    return nf::run_main([&] { return main_body(argc, argv); });
}
