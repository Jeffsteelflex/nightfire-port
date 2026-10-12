// nf_driving_check, parte 4 (tests/driving_check_ee.h): o código ORIGINAL do EE (MIPS do DRIVING.ELF) rodado no
// interpretador MIPS do port (data/mips_interp.h) com a VU0 ligada (o microcódigo original no interpretador das VUs),
// ao lado do porte em C++ (com o microcódigo portado à mão), sobre os mesmos dados. Compara a memória que as funções
// escrevem: os RigidBody, os RigidBodyInfo, a área de rascunho, os WWorldPos, e os registradores da VU0 que o
// microcódigo lê ou escreve.
//
// O que não é comparado, e por quê:
//  - os ponteiros (RigidBody +0x5c, os WWorldPos em RigidBodyInfo +0x380 e +0x4d8, o corpo e o info em voo na área
//    de rascunho, a face em WWorldPos +0x30, e o w de vf25, que leva o ponteiro +0x5c): os endereços da RAM emulada
//    não são os do port;
//  - a linha 3 do tensor inverso no mundo (RigidBodyInfo +0x70): lixo da pilha no PS2 (VU0_MATRIX3x4_mult não a
//    escreve), zero no port; nada a lê;
//  - vf01..vf12, ACC e Q: o código do EE em modo macro os usa como temporários e o porte em C++ usa variáveis locais;
//    os microprogramas carregam ou escrevem esses registradores antes de ler (só as sobras de pista que eles guardam
//    vêm de vf11, vf12 e vf15, que são carregados ou só escritos pelo microcódigo; ver vu0_physics.h).
//
// O que fica fora do código original (ganchos nativos, os mesmos dos dois lados): memset, FastAlloc, o chão
// (WWorldPos::FindClosestFace/MakeFaceAtPoint e WCollisionMgr::SurfaceBumpHeight: um plano, como drv::FlatGround), a
// geometria de colisão do dono (nenhuma), o evento ECollision (só contado), VU0_UploadMicrocode, e o que o porte ainda
// não tem (CollideWithObject, CollideWithWorld, os corpos simples, DetectCollisionsSRB, a visibilidade). Nos choques
// entre dois corpos (ResolveCollision), os donos são falsos dos dois lados: uma tabela virtual na RAM emulada cujas
// entradas +0x9c e +0x18c são ganchos (com o ajuste do this da tabela), PhysicsObject::PlayAnimation registrada (as
// chamadas são comparadas em ordem), a vida pelo ponteiro do dono +0x54 (GetHitPoints é o original) e o
// SMissionManager +0x350.

#include "driving_check_ee.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

// Endereços do DRIVING.ELF.
constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kSim = 0x339190;
constexpr uint32_t kTuning = 0x3390cc;
constexpr uint32_t kScratchPtr = 0x339178;
constexpr uint32_t kTimestepConst = 0x416280;
constexpr uint32_t kUnderwater = 0x416298;
constexpr uint32_t kCollMgrPtr = 0x33a540;

constexpr uint32_t kFnApplyAngularDamping = 0x1f8088;
constexpr uint32_t kFnUpdateZonalInfo = 0x1f8908;
constexpr uint32_t kFnControlSleep = 0x1fcfc0;
constexpr uint32_t kFnGetOrientToGround = 0x1f7c00;
constexpr uint32_t kFnApplyInitialForces = 0x1f7c60;
constexpr uint32_t kFnRigidBodyCtor = 0x1f6e98;
constexpr uint32_t kFnUpdatePosOrient = 0x1f8fd0;
constexpr uint32_t kFnTimestepCollect = 0x1f8cf0;
constexpr uint32_t kFnCollideWithGround = 0x1f9500;
constexpr uint32_t kFnResolveCollect = 0x1f8ff0;
constexpr uint32_t kFnSimulateGame = 0x1ff3d8;
constexpr uint32_t kFnStaticInit = 0x1fd338;
constexpr uint32_t kFnModifyLevers = 0x1f98e8;
constexpr uint32_t kFnScaleObjObjForces = 0x1f9b40;
constexpr uint32_t kFnResolveCollision = 0x1f9d98;
constexpr uint32_t kFnPlayAnimation = 0x195bd0;  // PhysicsObject::PlayAnimation (não virtual)
constexpr uint32_t kMissionMgrPtr = 0x33a024;    // o SMissionManager (ResolveCollision lê +0x350)

// A RAM emulada dos objetos (fora do ELF, abaixo da pilha).
constexpr uint32_t kBodies = 0x01000000;
constexpr uint32_t kInfos = 0x01010000;
constexpr uint32_t kScratch = 0x01020000;
constexpr uint32_t kCollMgr = 0x01030000;
constexpr uint32_t kMap = 0x01031000;
constexpr uint32_t kOwners = 0x01032000;  // 48 donos de 0x100 bytes (o WWorldPos no começo)
constexpr uint32_t kParams = 0x01040000;
constexpr uint32_t kFaceTag = 0x01041000;
// Os donos falsos de ResolveCollision: uma tabela virtual (no dono +0x68) com as entradas +0x98/+0x9c e
// +0x188/+0x18c apontando para dois ganchos (endereços fora do código, nunca executados como MIPS), e o
// SMissionManager falso.
constexpr uint32_t kOwnerVtable = 0x01042000;
constexpr uint32_t kOwnerFn9c = 0x01043000, kOwnerFn18c = 0x01043010;
constexpr uint32_t kDelta18c = 0x40;  // o ajuste do this na entrada +0x18c (o original soma; o gancho desconta)
constexpr uint32_t kMissionMgr = 0x01044000;
constexpr uint32_t kHeapBegin = 0x01100000;
constexpr uint32_t kHeapEnd = 0x01200000;
constexpr uint32_t kDataBegin = 0x326000, kDataEnd = 0x428000;  // .data até o fim do .bss

uint32_t f2u(float f) { return ps2::fbits(f); }

// O que os donos falsos respondem (os dois lados) e o registro das chamadas: (tipo << 24) | (vaga << 16) | argumento,
// tipo 1 = entrada +0x9c, 2 = +0x18c, 4 = PlayAnimation. GetHitPoints é o original (lê o dono +0x54) e não entra no
// registro; has_hp falso: sem o ponteiro (o original dá 1,0, o port o padrão de PhysicsObject::hit_points).
struct OwnerAnswers {
    int32_t v9c = 0;
    int32_t v18c = 0;
    bool has_hp = false;
    float hp = 1.0f;
};
uint32_t owner_call(uint32_t kind, int slot, uint32_t arg = 0) {
    return (kind << 24) | (uint32_t(slot & 0xff) << 16) | (arg & 0xffff);
}

// O dono do lado do port para ResolveCollision.
class CheckOwner : public drv::BasicOwner {
public:
    int slot = 0;
    OwnerAnswers ans;
    std::vector<uint32_t>* calls = nullptr;
    int32_t virtual_9c() override {
        calls->push_back(owner_call(1, slot));
        return ans.v9c;
    }
    int32_t virtual_18c() override {
        calls->push_back(owner_call(2, slot));
        return ans.v18c;
    }
    float hit_points() override { return ans.has_hp ? ans.hp : drv::BasicOwner::hit_points(); }
    void play_animation(uint32_t id) override { calls->push_back(owner_call(4, slot, id)); }
};

struct Emu {
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    bool ground_on = false;
    float ground = 0;
    uint64_t impacts = 0;
    OwnerAnswers answers[48];
    std::vector<uint32_t> owner_calls;

    int owner_slot(uint32_t addr) {
        const uint32_t s = (addr - kOwners) >> 8;
        if (addr < kOwners || s >= 48) throw nf::MipsError("dono falso fora das vagas", addr, 0);
        return int(s);
    }

    std::vector<uint8_t> data_snapshot;

    void mem_write(uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }
    void mem_read(uint32_t addr, void* dst, uint32_t n) { std::memcpy(dst, m.mem(addr, n), n); }
    void w32(uint32_t addr, uint32_t v) { m.write32(addr, v); }
    uint32_t r32(uint32_t addr) { return m.read32(addr); }

    // Um plano em y = ground: o mesmo triângulo de drv::FlatGround.
    bool face(uint32_t wp) {
        if (!ground_on) return false;
        const float v0[3] = {0.0f, ground, 0.0f}, v1[3] = {0.0f, ground, 1.0f}, v2[3] = {1.0f, ground, 0.0f};
        mem_write(wp + 0x00, v0, 12);
        mem_write(wp + 0x10, v1, 12);
        mem_write(wp + 0x20, v2, 12);
        w32(wp + 0x30, kFaceTag);
        return true;
    }

    void setup(const nf::Elf& elf, const std::vector<uint8_t>& prog) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program = prog;
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto ret = [](MipsMachine& mm, uint32_t v) { mm.set_gpr(R::v0, v); };
        m.hook(0x2a8038, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2cfda8, "UMemory::FastAlloc", [this, ret](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            std::memset(mm.mem(heap, n), 0, n);
            ret(mm, heap);
            heap += n;
        });
        m.hook(0x175730, "alocação do evento", [this, ret](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            std::memset(mm.mem(heap, n), 0, n);
            ret(mm, heap);
            heap += n;
        });
        m.hook(0x149d30, "ECollision::ECollision", [this](MipsMachine& mm) {
            ++impacts;
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x232200, "WWorldPos::~WWorldPos", [](MipsMachine&) {});
        m.hook(0x195ae0, "PhysicsObject::GetCollisionGeometry", [ret](MipsMachine& mm) {
            mm.write32(uint32_t(mm.gpr(R::a1)), 0);
            ret(mm, 0);
        });
        m.hook(0x2322c8, "WWorldPos::FindClosestFace", [this, ret](MipsMachine& mm) {
            ret(mm, face(uint32_t(mm.gpr(R::a0))) ? 1 : 0);
        });
        m.hook(0x232228, "WWorldPos::MakeFaceAtPoint", [this](MipsMachine& mm) { face(uint32_t(mm.gpr(R::a0))); });
        m.hook(0x213618, "WCollisionMgr::SurfaceBumpHeight", [](MipsMachine& mm) { mm.set_fpr(0, 0.0f); });
        m.hook(0x2cc3f0, "VU0_UploadMicrocode", [this](MipsMachine&) {
            vu.load_micro(0, program.data(), uint32_t(program.size()));
        });
        for (uint32_t a : {0x1fbbd8u, 0x1fc2f0u, 0x1fd798u, 0x2014b8u, 0x2017b8u})
            m.hook(a, "(não portado)", [](MipsMachine&) {});
        // Os donos de ResolveCollision (só ela chama estes; o resto do código não os alcança).
        m.hook(kOwnerFn9c, "dono: entrada +0x9c", [this](MipsMachine& mm) {
            const int s = owner_slot(uint32_t(mm.gpr(R::a0)));
            owner_calls.push_back(owner_call(1, s));
            mm.set_gpr(R::v0, uint64_t(int64_t(answers[s].v9c)));
        });
        m.hook(kOwnerFn18c, "dono: entrada +0x18c", [this](MipsMachine& mm) {
            const int s = owner_slot(uint32_t(mm.gpr(R::a0)) - kDelta18c);
            owner_calls.push_back(owner_call(2, s));
            mm.set_gpr(R::v0, uint64_t(int64_t(answers[s].v18c)));
        });
        // GetHitPoints roda o original (o dono +0x54: o ponteiro da vida, ou nulo para 1,0; ver own_pair).
        m.hook(kFnPlayAnimation, "PhysicsObject::PlayAnimation", [this](MipsMachine& mm) {
            const int s = owner_slot(uint32_t(mm.gpr(R::a0)));
            owner_calls.push_back(owner_call(4, s, uint32_t(mm.gpr(R::a1))));
        });
        const uint8_t* d = m.mem(kDataBegin, kDataEnd - kDataBegin);
        data_snapshot.assign(d, d + (kDataEnd - kDataBegin));
        reset();
    }

    // Volta ao estado logo depois da carga do ELF (o .data e o .bss, os objetos e a pilha zerados), sem recarregar os
    // 32 MB.
    void reset() {
        std::memcpy(m.mem(kDataBegin, kDataEnd - kDataBegin), data_snapshot.data(), data_snapshot.size());
        std::memset(m.mem(kBodies, kHeapEnd - kBodies), 0, kHeapEnd - kBodies);
        std::memset(m.mem(MipsMachine::kStackTop - 0x10000, 0x10000), 0, 0x10000);
        heap = kHeapBegin;
        impacts = 0;
        ground_on = false;
        vu = ps2::VuMachine(ps2::VuUnit::VU0);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        // Os globais: a área de rascunho, o gerente de colisão, a Simulation.
        w32(kScratchPtr, kScratch);
        w32(kCollMgrPtr, kCollMgr);
        w32(kSim + 0x04, 0);
        w32(kSim + 0x14, 0x1800);
        w32(kSim + 0x1c, kBodies);
        w32(kSim + 0x24, kInfos);
        w32(kSim + 0x28, kBodies + 0x1800);
        w32(kSim + 0xdb4, 0);
        w32(kSim + 0xdc0, f2u(drv::simulation().dt));
        w32(kSim + 0xe68, kMap + 0x10);
        w32(kMap + 0x10, kMap);
        w32(kMap + 0x08, kMap);
        for (int i = 0; i < 48; ++i) w32(kSim + 0x270 + 4 * uint32_t(i), 0);
    }

    void call(uint32_t fn, std::vector<uint32_t> args = {}) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 200'000'000);
    }
};

// ---- o estado do port <-> a RAM emulada --------------------------------------------------------------------------

uint32_t body_addr(int slot) { return kBodies + 0x80 * uint32_t(slot); }
uint32_t info_addr(int slot) { return kInfos + 0x500 * uint32_t(slot); }

void put_body(Emu& e, int slot, const drv::RigidBody& rb) {
    e.mem_write(body_addr(slot), &rb, 0x80);
    e.w32(body_addr(slot) + 0x5c, info_addr(slot));
}

void put_info(Emu& e, int slot, const drv::RigidBodyInfo& info, const uint32_t lever_pos[16], uint32_t owner_pos) {
    e.mem_write(info_addr(slot), &info, 0x500);
    for (int i = 0; i < 16; ++i) e.w32(info_addr(slot) + 0x380 + 4 * uint32_t(i), lever_pos ? lever_pos[i] : 0);
    e.w32(info_addr(slot) + 0x4d8, owner_pos);
}

struct Mismatch {
    uint64_t cases = 0, bad = 0;
    std::map<std::string, uint64_t> fields;  // campo -> casos em que diferiu
    std::string first;
    void note(const std::string& f, const std::string& detail) {
        fields[f]++;
        if (first.empty()) first = detail;
    }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", v);
    return b;
}

// Compara um bloco de bytes com máscara (offsets a ignorar, em palavras de 4 bytes).
bool compare_block(const char* what, const uint8_t* a, const uint8_t* b, uint32_t n,
                   const std::vector<std::pair<uint32_t, uint32_t>>& skip, Mismatch& mm) {
    bool same = true;
    for (uint32_t o = 0; o < n; o += 4) {
        bool skipped = false;
        for (const auto& [s, e] : skip)
            if (o >= s && o < e) skipped = true;
        if (skipped) continue;
        uint32_t x, y;
        std::memcpy(&x, a + o, 4);
        std::memcpy(&y, b + o, 4);
        if (x != y) {
            same = false;
            char f[64];
            std::snprintf(f, sizeof(f), "%s+0x%03x", what, o);
            mm.note(f, std::string(f) + ": original " + hex(x) + ", porte " + hex(y));
        }
    }
    return same;
}

const std::vector<std::pair<uint32_t, uint32_t>> kBodySkip = {{0x5c, 0x60}};
const std::vector<std::pair<uint32_t, uint32_t>> kInfoSkip = {{0x70, 0x80}, {0x380, 0x3c0}, {0x4d8, 0x4dc}};
const std::vector<std::pair<uint32_t, uint32_t>> kScratchSkip = {{0x390, 0x398}};

bool compare_body(Emu& e, int slot, const drv::RigidBody& rb, Mismatch& mm) {
    uint8_t buf[0x80];
    e.mem_read(body_addr(slot), buf, 0x80);
    return compare_block("RigidBody", buf, reinterpret_cast<const uint8_t*>(&rb), 0x80, kBodySkip, mm);
}

bool compare_info(Emu& e, int slot, const drv::RigidBodyInfo& info, Mismatch& mm) {
    std::vector<uint8_t> buf(0x500);
    e.mem_read(info_addr(slot), buf.data(), 0x500);
    bool same = compare_block("RigidBodyInfo", buf.data(), reinterpret_cast<const uint8_t*>(&info), 0x500, kInfoSkip, mm);
    // Os WWorldPos das alavancas.
    for (int i = 0; i < 16; ++i) {
        const uint32_t wp = e.r32(info_addr(slot) + 0x380 + 4 * uint32_t(i));
        if (!wp || !info.lever_pos[i]) {
            if (bool(wp) != bool(info.lever_pos[i])) {
                same = false;
                mm.note("WWorldPos (existe)", "WWorldPos da alavanca " + std::to_string(i));
            }
            continue;
        }
        uint8_t w[0x40];
        e.mem_read(wp, w, 0x40);
        same &= compare_block("WWorldPos", w, reinterpret_cast<const uint8_t*>(nf::raw(info.lever_pos[i])), 0x40,
                              {{0x0c, 0x10}, {0x1c, 0x20}, {0x30, 0x34}}, mm);
    }
    return same;
}

bool compare_scratch(Emu& e, Mismatch& mm) {
    std::vector<uint8_t> buf(sizeof(drv::RigidBodyScratch));
    e.mem_read(kScratch, buf.data(), uint32_t(buf.size()));
    const drv::RigidBodyScratch& s = drv::rigid_scratch();
    bool same = compare_block("rascunho", buf.data(), reinterpret_cast<const uint8_t*>(&s), uint32_t(buf.size()),
                              kScratchSkip, mm);
    const bool a = e.r32(kScratch + 0x390) != 0, b = s.vu0_body != nullptr;
    if (a != b) {
        same = false;
        mm.note("rascunho+0x390 (corpo em voo)", "o corpo em voo na VU0");
    }
    return same;
}

bool compare_vu(Emu& e, Mismatch& mm) {
    bool same = true;
    const ps2::VuRegs& a = e.vu.regs();
    const ps2::VuRegs& b = drv::vu0().regs();
    for (int r = 13; r < 32; ++r)
        for (int l = 0; l < 4; ++l) {
            if (r == 25 && l == 3) continue;
            if (a.vf[r][l] != b.vf[r][l]) {
                same = false;
                char f[32];
                std::snprintf(f, sizeof(f), "vf%02d.%c", r, "xyzw"[l]);
                mm.note(f, std::string(f) + ": original " + hex(a.vf[r][l]) + ", porte " + hex(b.vf[r][l]));
            }
        }
    for (int i = 1; i < 16; ++i)
        if (a.vi[i] != b.vi[i]) {
            same = false;
            mm.note("vi" + std::to_string(i), "vi" + std::to_string(i));
        }
    for (uint32_t n = 0; n < 256; ++n)
        for (int l = 0; l < 4; ++l)
            if (e.vu.qword(n)[l] != drv::vu0().qword(n)[l]) {
                same = false;
                char f[32];
                std::snprintf(f, sizeof(f), "VU0 mem[%u]", n);
                mm.note(f, f);
            }
    return same;
}

void report(const char* what, const Mismatch& mm, int& failures) {
    std::printf("  %-52s %6" PRIu64 " casos, %" PRIu64 " diferentes", what, mm.cases, mm.bad);
    if (mm.bad == 0) {
        std::printf(" (bit a bit)\n");
        return;
    }
    ++failures;
    std::printf("; campos:");
    int n = 0;
    for (const auto& [k, v] : mm.fields) {
        if (n++ >= 10) {
            std::printf(" ...");
            break;
        }
        std::printf(" %s(%" PRIu64 ")", k.c_str(), v);
    }
    std::printf("\n    primeiro: %s\n", mm.first.c_str());
}

// ---- estados aleatórios ------------------------------------------------------------------------------------------

struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t s) : g(s) {}
    float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g); }
    int range(int a, int b) { return std::uniform_int_distribution<int>(a, b)(g); }
    bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(g) < p; }
};

void quat_matrix(Rng& rng, float m[4][4]) {
    double q[4], n = 0;
    for (double& x : q) {
        x = rng.uni(-1, 1);
        n += x * x;
    }
    n = std::sqrt(n) + 1e-9;
    for (double& x : q) x /= n;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)},
                            {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)},
                            {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)}};
    std::memset(m, 0, 64);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i][j] = float(r[i][j]);
    m[3][3] = 1;
}

drv::RigidBodyParams random_params(Rng& rng, int slot, bool near_ground) {
    drv::RigidBodyParams p;
    p.slot = slot;
    p.type = rng.range(4, 7);
    p.mass = rng.chance(0.1) ? 1660.0f : rng.uni(0.5f, 2500.0f);
    p.pos[0] = rng.uni(-100, 100);
    p.pos[1] = near_ground ? rng.uni(0.0f, 1.2f) : rng.uni(-5, 30);
    p.pos[2] = rng.uni(-100, 100);
    for (int i = 0; i < 3; ++i) {
        p.vel[i] = rng.uni(-15, 15);
        p.ang_vel[i] = rng.uni(-6, 6);
    }
    quat_matrix(rng, p.orient);
    for (int i = 0; i < 3; ++i) {
        p.half_size[i] = rng.chance(0.05) ? rng.uni(0.0f, 0.001f) : rng.uni(0.1f, 2.5f);
        p.inv_inertia[i] = rng.uni(0.0005f, 3.0f);
    }
    p.half_size[3] = rng.uni(-1, 1);
    p.moving = rng.chance(0.85) ? 1 : 0;
    p.flag2 = rng.chance(0.5) ? 1 : 0;
    return p;
}

// Constrói um corpo dos dois lados (o construtor original na RAM emulada, o do port na Simulation do port).
void construct_both(Emu& e, const drv::RigidBodyParams& p) {
    const uint32_t a = kParams;
    e.mem_write(a + 0x00, p.pos, 12);
    e.mem_write(a + 0x10, p.vel, 12);
    e.mem_write(a + 0x20, p.ang_vel, 12);
    e.mem_write(a + 0x30, p.inv_inertia, 12);
    e.mem_write(a + 0x40, p.half_size, 16);
    e.mem_write(a + 0x80, p.orient, 64);
    const uint32_t owner = kOwners + 0x100 * uint32_t(p.slot);
    std::memset(e.m.mem(owner, 0x100), 0, 0x100);
    e.w32(kSim + 0x270 + 4 * uint32_t(p.slot), owner);
    e.m.set_gpr(R::t0, a + 0x10);
    e.m.set_gpr(R::t1, a + 0x20);
    e.m.set_gpr(R::t2, a + 0x80);
    e.m.set_gpr(R::t3, a + 0x30);
    e.m.set_fpr(12, p.mass);
    e.w32(MipsMachine::kStackTop + 0x00, a + 0x40);
    e.w32(MipsMachine::kStackTop + 0x08, owner);
    e.w32(MipsMachine::kStackTop + 0x10, uint32_t(p.moving));
    e.w32(MipsMachine::kStackTop + 0x18, uint32_t(p.flag2));
    e.m.set_gpr(R::sp, MipsMachine::kStackTop);
    e.m.call(kFnRigidBodyCtor,
             {body_addr(p.slot), uint32_t(p.slot), uint32_t(p.type), a + 0x00}, 200'000'000);
    drv::simulation_add_body(drv::simulation(), p);
}

// ---- os choques entre dois corpos (ResolveCollision) ------------------------------------------------------------

// Dois corpos perto um do outro (vagas 0 e 1), construídos pelos dois construtores (com tipo >= 4: o construtor de
// um veículo consultaria o dono). Às vezes os dois pesados, de massas iguais, alinhados (a mesma orientação ou a
// virada 180 graus em y) e um à frente do outro.
void build_pair(Emu& e, Rng& rng) {
    drv::RigidBodyParams pa = random_params(rng, 0, false);
    drv::RigidBodyParams pb = random_params(rng, 1, false);
    if (rng.chance(0.2)) {
        pa.mass = rng.uni(1501.0f, 3000.0f);
        pb.mass = rng.uni(1501.0f, 3000.0f);
    }
    if (rng.chance(0.15)) pb.mass = pa.mass;
    for (int i = 0; i < 3; ++i) pb.pos[i] = pa.pos[i] + rng.uni(-3, 3);
    if (rng.chance(0.4)) {
        std::memcpy(pb.orient, pa.orient, sizeof(pa.orient));
        if (rng.chance(0.5))
            for (int j = 0; j < 3; ++j) {
                pb.orient[0][j] = -pb.orient[0][j];
                pb.orient[2][j] = -pb.orient[2][j];
            }
        if (rng.chance(0.6)) {
            const float d = rng.uni(-4, 4);
            for (int i = 0; i < 3; ++i) pb.pos[i] = pa.pos[i] + pa.orient[2][i] * d + rng.uni(-0.3f, 0.3f);
        }
    }
    construct_both(e, pa);
    construct_both(e, pb);
}

// O tipo e um estado variado de um corpo, dos dois lados: parado (velocidade zero exata), devagar ou acima de 15 m/s,
// os bits 0 e 1 de +0x7c, alavancas no chão, os indicadores de RigidBodyInfo +0x4fc..+0x4fe e a idade.
void shake_body(Emu& e, Rng& rng, int slot, drv::RigidBody* rb, int type) {
    rb->type = int8_t(type);
    const bool rest = rng.chance(0.15), fast = rng.chance(0.2);
    for (int i = 0; i < 3; ++i) {
        rb->vel[i] = rest ? 0.0f : fast ? rng.uni(-25, 25) : rng.uni(-8, 8);
        rb->momentum[i] = rb->vel[i] * rb->mass;
        rb->ang_vel[i] = rng.chance(0.3) ? 0.0f : rng.uni(-4, 4);
        rb->ang_momentum[i] = rng.uni(-300, 300);
    }
    rb->radius_flags = (rb->radius_flags & ~3u) | (rng.chance(0.5) ? 2u : 0u) | (rng.chance(0.3) ? 1u : 0u);
    rb->ground_levers = int8_t(rng.chance(0.5) ? 0 : rng.range(1, 4));
    rb->state = int8_t(rng.chance(0.8) ? 2 : 1);
    rb->info->flags[0] = uint8_t(rng.chance(0.04) ? 1 : 0);
    rb->info->flags[1] = uint8_t(rng.chance(0.3) ? 1 : 0);
    rb->info->flags[2] = uint8_t(rng.chance(0.2) ? 1 : 0);
    rb->info->age = uint16_t(rng.range(0, 0xffff));
    put_body(e, slot, *rb);
    e.mem_write(info_addr(slot) + 0x4de, &rb->info->age, 2);
    e.mem_write(info_addr(slot) + 0x4fc, rb->info->flags, 4);
}

OwnerAnswers random_answers(Rng& rng) {
    OwnerAnswers a;
    const int r = rng.range(0, 9);
    a.v9c = r < 4 ? 0 : r < 7 ? 1 : r < 9 ? 2 : 3;
    a.v18c = rng.chance(0.3) ? rng.range(1, 3) : 0;
    a.has_hp = rng.chance(0.7);
    if (a.has_hp) {
        const int h = rng.range(0, 3);
        a.hp = h == 0 ? 0.0f : h == 1 ? -rng.uni(0.1f, 50.0f) : rng.uni(0.01f, 100.0f);
    }
    return a;
}

// Os donos das vagas 0 e 1 dos dois lados (a tabela virtual falsa no dono +0x68, a vida pelo dono +0x54, os
// CheckOwner na Simulation do port) e o SMissionManager +0x350.
void own_pair(Emu& e, CheckOwner owners[2], std::vector<uint32_t>& calls, int32_t mission) {
    e.w32(kOwnerVtable + 0x98, 0);  // o deslocamento do this (s16) da entrada +0x9c
    e.w32(kOwnerVtable + 0x9c, kOwnerFn9c);
    e.w32(kOwnerVtable + 0x188, kDelta18c);
    e.w32(kOwnerVtable + 0x18c, kOwnerFn18c);
    for (int s = 0; s < 2; ++s) {
        const uint32_t owner = kOwners + 0x100 * uint32_t(s);
        e.w32(owner + 0x68, kOwnerVtable);
        const OwnerAnswers& a = e.answers[s];
        if (a.has_hp) {
            e.w32(owner + 0x80, f2u(a.hp));
            e.w32(owner + 0x54, owner + 0x80);
        } else {
            e.w32(owner + 0x54, 0);
        }
        owners[s].slot = s;
        owners[s].ans = a;
        owners[s].calls = &calls;
        drv::simulation().owners[s] = &owners[s];
    }
    e.w32(kMissionMgrPtr, kMissionMgr);
    e.w32(kMissionMgr + 0x350, uint32_t(mission));
    drv::mission_uses_health() = mission;
}

// Uma Simulation nova dos dois lados.
void fresh(Emu& e, const nf::Elf& elf, const std::vector<uint8_t>& prog) {
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    drv::set_vu0_mode(drv::Vu0Mode::Native);
    drv::simulation_reset(60);
    drv::vu0_reset();
    (void)elf;
    (void)prog;
    e.reset();
    // O construtor estático original (as razões das constantes) e o do port.
    e.call(kFnStaticInit, {1, 0xffff});
    for (int i = 0; i < 48; ++i) std::memset(e.m.mem(info_addr(i), 0x500), 0, 0x500);
}

}  // namespace

int run_ee_checks(const nf::Elf& elf, const std::vector<uint8_t>& physics_program, int cases, uint32_t seed) {
    std::printf("\n== o código original do EE (interpretador MIPS + VU0) x o porte em C++ ==\n");
    int failures = 0;
    Emu* e = new Emu();  // grande (32 MB de RAM)
    try {
        drv::simulation_reset(60);  // o dt que a Simulation emulada recebe
        e->setup(elf, physics_program);
        // 0. As constantes de ajuste depois do construtor estático.
        {
            fresh(*e, elf, physics_program);
            Mismatch mm;
            mm.cases = 1;
            uint8_t buf[sizeof(drv::RigidTuning)];
            e->mem_read(kTuning, buf, sizeof(buf));
            if (!compare_block("ajuste", buf, reinterpret_cast<const uint8_t*>(&drv::rigid_tuning()), sizeof(buf), {},
                               mm))
                mm.bad = 1;
            report("constantes (0x3390cc..) depois do construtor estático", mm, failures);
        }
        Rng rng(seed);
        // 1. ApplyAngularDamping, UpdateZonalInfo, ControlSleep, GetOrientToGround, ApplyInitialForcesAndTorques:
        //    corpos aleatórios construídos pelos dois construtores (o construtor também é comparado).
        Mismatch ctor_mm, damp_mm, zone_mm, sleep_mm, orient_mm, forces_mm;
        const int n_small = std::max(1, cases / 4);
        for (int c = 0; c < n_small; ++c) {
            fresh(*e, elf, physics_program);
            drv::RigidBodyParams p = random_params(rng, 0, false);
            construct_both(*e, p);
            drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), 0);
            ++ctor_mm.cases;
            bool ok = compare_body(*e, 0, *rb, ctor_mm) & compare_info(*e, 0, *rb->info, ctor_mm);
            if (!ok) ++ctor_mm.bad;

            // Estados variados para as funções pequenas.
            rb->ground_levers = int8_t(rng.range(0, 5));
            for (int i = 0; i < 3; ++i) {
                rb->vel[i] = rng.chance(0.4) ? rng.uni(-0.6f, 0.6f) : rng.uni(-10, 10);
                rb->ang_vel[i] = rng.chance(0.4) ? rng.uni(-0.6f, 0.6f) : rng.uni(-5, 5);
                rb->ang_momentum[i] = rng.uni(-500, 500);
            }
            rb->type = int8_t(rng.range(4, 7));
            rb->state = int8_t(rng.chance(0.9) ? 2 : 3);
            rb->info->flags[1] = uint8_t(rng.chance(0.2));
            rb->info->flags[2] = uint8_t(rng.chance(0.2));
            rb->info->age = uint16_t(rng.chance(0.5) ? 0xffff : rng.range(0, 0xfffe));
            drv::underwater_track() = rng.chance(0.2);
            e->w32(kUnderwater, drv::underwater_track() ? 1 : 0);
            put_body(*e, 0, *rb);
            put_info(*e, 0, *rb->info, nullptr, 0);
            for (int i = 0; i < 16; ++i)
                e->w32(info_addr(0) + 0x380 + 4 * uint32_t(i), 0);  // sem WWorldPos nestes testes

            drv::RigidBody rb0 = *rb;
            drv::RigidBodyInfo info0 = *rb->info;
            auto restore = [&]() {
                *rb = rb0;
                *rb->info = info0;
                put_body(*e, 0, rb0);
                put_info(*e, 0, info0, nullptr, 0);
            };
            auto check = [&](Mismatch& mm) {
                ++mm.cases;
                drv::RigidBodyInfo info_cmp = *rb->info;
                for (auto& w : info_cmp.lever_pos) w = nullptr;
                bool same = compare_body(*e, 0, *rb, mm) & compare_info(*e, 0, info_cmp, mm);
                if (!same) ++mm.bad;
            };
            // As alavancas sem WWorldPos para a comparação.
            drv::WWorldPos* saved[16];
            std::memcpy(saved, rb->info->lever_pos, sizeof(saved));

            e->call(kFnApplyAngularDamping, {body_addr(0)});
            drv::ApplyAngularDamping(rb);
            check(damp_mm);
            restore();

            e->call(kFnUpdateZonalInfo, {body_addr(0)});
            drv::UpdateZonalInfo(rb);
            check(zone_mm);
            restore();

            e->call(kFnControlSleep, {body_addr(0)});
            drv::ControlSleep(rb);
            check(sleep_mm);
            restore();

            e->call(kFnGetOrientToGround, {body_addr(0)});
            ++orient_mm.cases;
            const float o = drv::GetOrientToGround(rb);
            if (ps2::fbits(e->m.fpr(0)) != ps2::fbits(o)) {
                ++orient_mm.bad;
                orient_mm.note("f0", "f0: original " + hex(ps2::fbits(e->m.fpr(0))) + ", porte " + hex(ps2::fbits(o)));
            }

            e->call(kFnApplyInitialForces, {body_addr(0)});
            drv::ApplyInitialForcesAndTorques(rb);
            check(forces_mm);
            restore();
            std::memcpy(rb->info->lever_pos, saved, sizeof(saved));
            drv::underwater_track() = false;
        }
        report("RigidBody::RigidBody (o construtor, com InitLevers)", ctor_mm, failures);
        report("RigidBody::ApplyAngularDamping", damp_mm, failures);
        report("RigidBody::UpdateZonalInfo", zone_mm, failures);
        report("RigidBody::ControlSleep (e ForceToSleep)", sleep_mm, failures);
        report("RigidBody::GetOrientToGround", orient_mm, failures);
        report("RigidBody::ApplyInitialForcesAndTorques", forces_mm, failures);

        // 2. O passo (Timestep_LoadAndStartVU0 + o microprograma + Timestep_CollectVU0data) e o chão
        //    (CollideWithGround: TempGetHeightInformation, ResolveLeverForces + o microprograma, Resolve_CollectVU0data).
        Mismatch step_mm, ground_mm;
        const int n_step = std::max(1, cases / 8);
        drv::FlatGround flat(0.0f);
        for (int c = 0; c < n_step; ++c) {
            fresh(*e, elf, physics_program);
            const int bodies = rng.range(1, 3);
            for (int s = 0; s < bodies; ++s) construct_both(*e, random_params(rng, s, true));
            // Força e torque aleatórios (o passo os integra).
            for (int s = 0; s < bodies; ++s) {
                drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                for (int i = 0; i < 3; ++i) {
                    rb->force[i] = rng.uni(-40, 40) * rb->mass;
                    rb->torque[i] = rng.uni(-100, 100);
                }
                e->mem_write(body_addr(s) + 0x60, rb->force, 12);
                e->mem_write(body_addr(s) + 0x70, rb->torque, 12);
            }
            // O passo de todos e a coleta.
            for (int s = 0; s < bodies; ++s) {
                drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                e->m.set_fpr(12, 1.0f);
                e->call(kFnUpdatePosOrient, {body_addr(s)});
                drv::UpdatePositionAndOrientation(rb, 1.0f);
            }
            e->call(kFnTimestepCollect);
            drv::Timestep_CollectVU0data();
            ++step_mm.cases;
            bool same = compare_scratch(*e, step_mm) & compare_vu(*e, step_mm);
            for (int s = 0; s < bodies; ++s) {
                const drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                same &= compare_body(*e, s, *rb, step_mm) & compare_info(*e, s, *rb->info, step_mm);
            }
            if (!same) ++step_mm.bad;
            if (*reinterpret_cast<const uint32_t*>(&drv::timestep_constants()) != e->r32(kTimestepConst)) {
                ++step_mm.bad;
                step_mm.note("0x416280 (vf04)", "a constante do passo");
            }

            // O chão: um plano numa altura aleatória perto dos corpos.
            const float h = rng.uni(-0.5f, 0.8f);
            flat = drv::FlatGround(h);
            drv::set_world(&flat);
            e->ground_on = true;
            e->ground = h;
            for (int s = 0; s < bodies; ++s) {
                drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                e->call(kFnCollideWithGround, {body_addr(s)});
                drv::CollideWithGround(rb);
            }
            e->call(kFnResolveCollect);
            drv::Resolve_CollectVU0data();
            ++ground_mm.cases;
            same = compare_scratch(*e, ground_mm) & compare_vu(*e, ground_mm);
            for (int s = 0; s < bodies; ++s) {
                const drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                same &= compare_body(*e, s, *rb, ground_mm) & compare_info(*e, s, *rb->info, ground_mm);
            }
            if (!same) ++ground_mm.bad;
            drv::set_world(nullptr);
        }
        report("Timestep_LoadAndStartVU0 + microcódigo + Collect", step_mm, failures);
        report("CollideWithGround + ResolveLeverForces + microcódigo", ground_mm, failures);

        // 2b. GenerateImpulse (o choque contra o mundo), Util_Bound e Util_GenerateCarTensor.
        Mismatch imp_mm, bound_mm, tensor_mm;
        for (int c = 0; c < n_step; ++c) {
            fresh(*e, elf, physics_program);
            drv::RigidBodyParams p = random_params(rng, 0, false);
            construct_both(*e, p);
            drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), 0);
            float n[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
            for (int i = 0; i < 3; ++i) n[i] /= len;
            const float point[4] = {rb->pos[0] + rng.uni(-2, 2), rb->pos[1] + rng.uni(-1, 1),
                                    rb->pos[2] + rng.uni(-2, 2), rng.uni(-1, 1)};
            const float depth = rng.uni(-0.2f, 0.5f), scale = rng.chance(0.5) ? 1.0f : rng.uni(0.2f, 2.0f);
            const bool world = rng.chance(0.7);
            const uint16_t tag = uint16_t(rng.range(0, 0xffff));
            e->mem_write(kParams + 0x100, n, 16);
            e->mem_write(kParams + 0x110, point, 16);
            std::memset(e->m.mem(kParams + 0x200, 0x60), 0, 0x60);
            e->m.set_gpr(R::t0, world ? 1 : 0);
            e->m.set_gpr(R::t1, tag);
            e->m.set_fpr(12, depth);
            e->m.set_fpr(13, scale);
            std::memset(e->m.mem(MipsMachine::kStackTop - 0x1000, 0x1000), 0, 0x1000);
            e->call(0x1fb318, {body_addr(0), kParams + 0x200, kParams + 0x100, kParams + 0x110});
            drv::CollisionInfo ci{};
            drv::GenerateImpulse(rb, &ci, n, point, depth, world, tag, scale);
            ++imp_mm.cases;
            uint8_t buf[sizeof(ci)];
            e->mem_read(kParams + 0x200, buf, sizeof(ci));
            bool same = compare_block("COLLISION_INFO", buf, reinterpret_cast<const uint8_t*>(&ci), sizeof(ci), {}, imp_mm);
            same &= compare_body(*e, 0, *rb, imp_mm) & compare_info(*e, 0, *rb->info, imp_mm);
            if (!same) ++imp_mm.bad;

            const float v = rng.uni(-3, 3), lim = rng.uni(0, 2), lim2 = rng.uni(0, 2);
            e->m.set_fpr(12, v);
            e->m.set_fpr(13, lim);
            e->m.set_fpr(14, lim2);
            e->call(0x210510);
            ++bound_mm.cases;
            if (ps2::fbits(e->m.fpr(0)) != ps2::fbits(drv::Util_Bound(v, lim, lim2))) {
                ++bound_mm.bad;
                bound_mm.note("f0", "Util_Bound");
            }

            const float dims[3] = {rng.uni(0.3f, 3.0f), rng.uni(0.3f, 2.0f), rng.uni(0.5f, 7.0f)};
            const float mass = rng.uni(10, 3000);
            e->m.set_fpr(12, mass);
            e->m.set_fpr(13, dims[0]);
            e->m.set_fpr(14, dims[1]);
            e->m.set_fpr(15, dims[2]);
            e->call(0x210938, {kParams + 0x300});
            float tens[3];
            drv::Util_GenerateCarTensor(tens, mass, dims[0], dims[1], dims[2]);
            ++tensor_mm.cases;
            uint8_t tb[12];
            e->mem_read(kParams + 0x300, tb, 12);
            if (!compare_block("tensor", tb, reinterpret_cast<const uint8_t*>(tens), 12, {}, tensor_mm)) ++tensor_mm.bad;
        }
        report("RigidBody::GenerateImpulse (choque contra o mundo)", imp_mm, failures);
        report("Util_Bound", bound_mm, failures);
        report("Util_GenerateCarTensor", tensor_mm, failures);

        // 2c. O choque entre dois corpos: ModifyLevers e ScaleObjObjForces sozinhas, depois ResolveCollision inteira,
        //     pelos três caminhos (dois objetos, um veículo e um objeto, dois veículos), com donos falsos dos dois
        //     lados (as entradas +0x9c e +0x18c da tabela virtual, a vida, PlayAnimation) e o SMissionManager +0x350.
        {
            const char* names[3] = {"ResolveCollision: dois objetos (tipo >= 4)", "ResolveCollision: veículo x objeto",
                                    "ResolveCollision: dois veículos (tipo 1 a 3)"};
            Mismatch rc_mm[3], ml_mm, so_mm;
            uint64_t separating[3] = {}, frozen[3] = {}, at_rest = 0, plain = 0, anims = 0, health = 0, supported = 0;
            uint64_t aligned = 0, scaled = 0;
            static CheckOwner owners[2];  // a Simulation do port aponta para eles depois do bloco
            std::vector<uint32_t> port_calls;
            for (int c = 0; c < n_step * 3; ++c) {
                const int path = c % 3;
                fresh(*e, elf, physics_program);
                build_pair(*e, rng);
                drv::Simulation& sim = drv::simulation();
                drv::RigidBody* A = drv::GetRigidBody(sim, 0);
                drv::RigidBody* B = drv::GetRigidBody(sim, 1);
                int ta = rng.range(4, 7), tb = rng.range(4, 7);
                if (path == 1) (rng.chance(0.5) ? ta : tb) = rng.range(1, 3);
                if (path == 2) {
                    ta = rng.range(1, 3);
                    tb = rng.range(1, 3);
                }
                shake_body(*e, rng, 0, A, ta);
                shake_body(*e, rng, 1, B, tb);
                for (int s = 0; s < 2; ++s) e->answers[s] = random_answers(rng);
                const int32_t mission = rng.chance(0.5) ? 1 : 0;
                own_pair(*e, owners, port_calls, mission);
                std::memset(e->m.mem(MipsMachine::kStackTop - 0x1000, 0x1000), 0, 0x1000);

                // ModifyLevers sozinha (só lê os corpos): braços, ponto e normal aleatórios.
                {
                    float ra[4], rb[4], nn[4], pt[4];
                    for (int i = 0; i < 3; ++i) {
                        pt[i] = (A->pos[i] + B->pos[i]) * 0.5f + rng.uni(-1, 1);
                        ra[i] = pt[i] - A->pos[i] + rng.uni(-0.2f, 0.2f);
                        rb[i] = pt[i] - B->pos[i] + rng.uni(-0.2f, 0.2f);
                        nn[i] = rng.uni(-1, 1);
                    }
                    if (rng.chance(0.05)) std::memcpy(pt, &A->pos[0], 12);  // o ponto no centro de A: RSQRT de zero
                    ra[3] = rng.uni(-1, 1);
                    rb[3] = rng.uni(-1, 1);
                    nn[3] = rng.uni(-1, 1);
                    pt[3] = rng.uni(-1, 1);
                    const bool vehicles = path == 2 ? rng.chance(0.8) : rng.chance(0.2);
                    e->mem_write(kParams + 0x120, ra, 16);
                    e->mem_write(kParams + 0x130, rb, 16);
                    e->mem_write(kParams + 0x140, nn, 16);
                    e->mem_write(kParams + 0x150, pt, 16);
                    e->m.set_gpr(R::t0, kParams + 0x150);
                    e->m.set_gpr(R::t1, kParams + 0x140);
                    e->m.set_gpr(R::t2, vehicles ? 1 : 0);
                    e->call(kFnModifyLevers, {body_addr(0), body_addr(1), kParams + 0x120, kParams + 0x130});
                    drv::ModifyLevers(A, B, ra, rb, pt, nn, vehicles);
                    ++ml_mm.cases;
                    float port[12];
                    std::memcpy(port, ra, 16);
                    std::memcpy(port + 4, rb, 16);
                    std::memcpy(port + 8, nn, 16);
                    uint8_t buf[48];
                    e->mem_read(kParams + 0x120, buf, 48);
                    if (!compare_block("braços e normal", buf, reinterpret_cast<const uint8_t*>(port), 48, {}, ml_mm))
                        ++ml_mm.bad;
                    if (vehicles) {
                        const float* fa = A->info->orient[2];
                        const float* fb = B->info->orient[2];
                        const double facing = std::fabs(double(fa[0]) * fb[0] + double(fa[1]) * fb[1] + double(fa[2]) * fb[2]);
                        double along = 0;
                        for (int i = 0; i < 3; ++i) along += (double(A->pos[i]) - B->pos[i]) * fa[i];
                        if (facing > 0.866 && std::fabs(along) > 0.866) ++aligned;
                    }
                }

                // ScaleObjObjForces sozinha (só lê os corpos): impulsos de até 30 m/s por massa.
                {
                    float ia[4], ib[4], ia0[4], ib0[4];
                    for (int i = 0; i < 3; ++i) {
                        ia[i] = rng.uni(-30, 30) * A->mass * (rng.chance(0.5) ? 1.0f : 0.1f);
                        ib[i] = rng.uni(-30, 30) * B->mass * (rng.chance(0.5) ? 1.0f : 0.1f);
                    }
                    ia[3] = rng.uni(-1, 1);
                    ib[3] = rng.uni(-1, 1);
                    std::memcpy(ia0, ia, 16);
                    std::memcpy(ib0, ib, 16);
                    const float thr = rng.chance(0.7) ? 225.0f : rng.uni(0.0f, 400.0f);
                    const float fac = rng.chance(0.7) ? ps2::ffloat(0x3f866666u) : rng.uni(0.5f, 2.0f);
                    const float dep = rng.uni(-0.5f, 2.0f);
                    e->mem_write(kParams + 0x160, ia, 16);
                    e->mem_write(kParams + 0x170, ib, 16);
                    e->m.set_fpr(12, thr);
                    e->m.set_fpr(13, fac);
                    e->m.set_fpr(14, dep);
                    e->call(kFnScaleObjObjForces, {body_addr(0), body_addr(1), kParams + 0x160, kParams + 0x170});
                    drv::ScaleObjObjForces(A, B, ia, ib, thr, fac, dep);
                    ++so_mm.cases;
                    float port[8];
                    std::memcpy(port, ia, 16);
                    std::memcpy(port + 4, ib, 16);
                    uint8_t buf[32];
                    e->mem_read(kParams + 0x160, buf, 32);
                    if (!compare_block("impulsos", buf, reinterpret_cast<const uint8_t*>(port), 32, {}, so_mm))
                        ++so_mm.bad;
                    if (std::memcmp(ia, ia0, 16) != 0 || std::memcmp(ib, ib0, 16) != 0) ++scaled;
                }

                // ResolveCollision: a normal de A para B (ou qualquer), o ponto entre os dois, a profundidade.
                float n[4];
                if (rng.chance(0.5)) {
                    for (int i = 0; i < 3; ++i) n[i] = B->pos[i] - A->pos[i] + rng.uni(-0.5f, 0.5f);
                } else {
                    for (int i = 0; i < 3; ++i) n[i] = rng.uni(-1, 1);
                }
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
                for (int i = 0; i < 3; ++i) n[i] /= len;
                n[3] = rng.uni(-1, 1);
                float point[4];
                for (int i = 0; i < 3; ++i) point[i] = (A->pos[i] + B->pos[i]) * 0.5f + rng.uni(-1, 1);
                point[3] = rng.uni(-1, 1);
                const float depth = rng.chance(0.3) ? rng.uni(0.5f, 1.5f) : rng.uni(-1.0f, 0.5f);
                drv::RigidBody* self = rng.chance(0.8) ? A : B;
                const uint32_t self_addr = self == A ? body_addr(0) : body_addr(1);
                drv::CollisionInfo ci;
                for (size_t i = 0; i < sizeof(ci); ++i) reinterpret_cast<uint8_t*>(&ci)[i] = uint8_t(rng.range(0, 255));
                e->mem_write(kParams + 0x100, n, 16);
                e->mem_write(kParams + 0x110, point, 16);
                e->mem_write(kParams + 0x200, &ci, sizeof(ci));
                std::memset(e->m.mem(MipsMachine::kStackTop - 0x1000, 0x1000), 0, 0x1000);
                e->owner_calls.clear();
                port_calls.clear();
                const bool frozen_pair = A->info->flags[0] != 0 || B->info->flags[0] != 0;
                const bool ground_a = owners[0].ans.v18c != 0 || A->ground_levers > 0;
                const bool ground_b = owners[1].ans.v18c != 0 || B->ground_levers > 0;
                auto counts = [](const OwnerAnswers& a) {
                    return (a.v9c == 1 || a.v9c == 2) && (!a.has_hp || a.hp > 0.0f);
                };
                e->m.set_gpr(R::t0, kParams + 0x110);
                e->m.set_gpr(R::t1, kParams + 0x200);
                e->m.set_fpr(12, depth);
                e->call(kFnResolveCollision, {self_addr, body_addr(0), body_addr(1), kParams + 0x100});
                const uint32_t ret_orig = uint32_t(e->m.gpr(R::v0));
                const int ret = drv::ResolveCollision(self, A, B, n, point, &ci, depth);
                Mismatch& mm = rc_mm[path];
                ++mm.cases;
                bool same = true;
                if (ret_orig != uint32_t(ret)) {
                    same = false;
                    mm.note("v0", "v0: original " + hex(ret_orig) + ", porte " + hex(uint32_t(ret)));
                }
                uint8_t buf[sizeof(ci)];
                e->mem_read(kParams + 0x100, buf, 16);
                same &= compare_block("normal", buf, reinterpret_cast<const uint8_t*>(n), 16, {}, mm);
                e->mem_read(kParams + 0x200, buf, sizeof(ci));
                same &= compare_block("COLLISION_INFO", buf, reinterpret_cast<const uint8_t*>(&ci), sizeof(ci), {}, mm);
                for (int s = 0; s < 2; ++s) {
                    const drv::RigidBody* rb = s == 0 ? A : B;
                    same &= compare_body(*e, s, *rb, mm);
                    same &= compare_info(*e, s, *rb->info, mm);
                }
                if (e->owner_calls != port_calls) {
                    same = false;
                    mm.note("chamadas ao dono", "chamadas ao dono: original " + std::to_string(e->owner_calls.size()) +
                                                    ", porte " + std::to_string(port_calls.size()));
                }
                if (!same) ++mm.bad;
                // O que o caso exercitou.
                if (ret == 0) {
                    ++separating[path];
                } else if (frozen_pair) {
                    ++frozen[path];
                } else if (path < 2) {
                    if ((A->type >= 4 && ci.unknown_30[0] == 0.0f) || (B->type >= 4 && ci.unknown_30[1] == 0.0f))
                        ++at_rest;
                    auto alive = [](const drv::RigidBody* rb, const OwnerAnswers& a) {
                        return (rb->type == 2 || rb->type == 3) && (!a.has_hp || a.hp > 0.0f);
                    };
                    const OwnerAnswers& sa = owners[self == A ? 0 : 1].ans;
                    if (alive(A, owners[0].ans) || alive(B, owners[1].ans) ||
                        ((self->type == 2 || self->type == 3) && sa.v9c == 1))
                        ++plain;
                } else {
                    if (mission && counts(owners[0].ans) != counts(owners[1].ans)) ++health;
                    if (ground_a != ground_b) ++supported;
                }
                for (uint32_t call : port_calls)
                    if ((call >> 24) == 4) ++anims;
            }
            report("RigidBody::ModifyLevers", ml_mm, failures);
            std::printf("    veículos alinhados (de frente ou de traseira): %" PRIu64 "\n", aligned);
            report("RigidBody::ScaleObjObjForces", so_mm, failures);
            std::printf("    impulsos reescalados: %" PRIu64 "\n", scaled);
            for (int p = 0; p < 3; ++p) {
                report(names[p], rc_mm[p], failures);
                std::printf("    afastando-se (devolve 0): %" PRIu64 "; RigidBodyInfo +0x4fc (devolve 1): %" PRIu64 "\n",
                            separating[p], frozen[p]);
            }
            std::printf("    com objeto: corpo parado que leva o impulso todo %" PRIu64
                        ", veículo vivo ou +0x9c do this (sem PlayAnimation) %" PRIu64 ", PlayAnimation(12) %" PRIu64
                        "\n    entre veículos: vida (SMissionManager +0x350) %" PRIu64
                        ", um apoiado e outro no ar %" PRIu64 "\n",
                        at_rest, plain, anims, health, supported);
        }

        // 3. Simulation::SimulateGame inteiro, quadro a quadro: cinco corpos caindo num chão plano.
        {
            fresh(*e, elf, physics_program);
            drv::FlatGround ground(0.0f);
            drv::set_world(&ground);
            e->ground_on = true;
            e->ground = 0.0f;
            Rng srng(seed + 7);
            const int bodies = 6;
            for (int s = 0; s < bodies; ++s) {
                drv::RigidBodyParams p = random_params(srng, s, false);
                p.pos[0] = float(s) * 6.0f;
                p.pos[2] = 0;
                p.pos[1] = srng.uni(1.0f, 8.0f);
                p.moving = 1;
                construct_both(*e, p);
            }
            const int frames = 600;
            Mismatch sim_mm;
            int first_bad = -1;
            for (int f = 0; f < frames; ++f) {
                e->call(kFnSimulateGame, {kSim});
                drv::SimulateGame(drv::simulation());
                ++sim_mm.cases;
                bool same = compare_scratch(*e, sim_mm) & compare_vu(*e, sim_mm);
                for (int s = 0; s < bodies; ++s) {
                    const drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                    same &= compare_body(*e, s, *rb, sim_mm) & compare_info(*e, s, *rb->info, sim_mm);
                }
                if (!same) {
                    ++sim_mm.bad;
                    if (first_bad < 0) first_bad = f;
                }
            }
            report("Simulation::SimulateGame (6 corpos, 600 quadros)", sim_mm, failures);
            if (first_bad >= 0) std::printf("    primeiro quadro diferente: %d\n", first_bad);
            std::printf("    choques contra o chão: %" PRIu64 " no original, %u no porte; instruções MIPS: %" PRIu64
                        ", microprogramas: %" PRIu64 "\n",
                        e->impacts, drv::ground_impacts().events, e->m.steps(), e->m.vu0_calls());
            for (int s = 0; s < bodies; ++s) {
                const drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), s);
                std::printf("    corpo %d (tipo %d, %.0f kg): (%.4f, %.4f, %.4f) estado %d\n", s, rb->type, rb->mass,
                            rb->pos[0], rb->pos[1], rb->pos[2], rb->state);
            }
            drv::set_world(nullptr);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  ERRO no interpretador MIPS: %s\n", err.what());
        ++failures;
    }
    delete e;
    return failures;
}

}  // namespace drvcheck
