// nf_driving_check, parte 16 (tests/driving_check_chars.h): os personagens. O código ORIGINAL do DRIVING.ELF roda no
// interpretador MIPS (com a VU0 em modo macro) ao lado do porte de driving/anim, com os dados do disco (os 89 bancos de
// animação e os 72 esqueletos dos nove .VIV, sem os repetidos) e valores sorteados:
//  - a VU0: o slerp rápido (0x00298478 e o VU0_fastqslerp 0x002cc018), VU0_MATRIX4_mult (0x00297c60 e a cópia 0x002cbb88)
//    e VU0_SQTquattom4 (0x002985d0), com quatérnios quase opostos, t fora de [0, 1] e a saída sobre a entrada;
//  - o DeltaCompressedData: DecompressValues (0x00276820) e DecompressValuesIndexed (0x00276c70) sobre os dados dos
//    FnDeltaQuatChan e FnKeyQuatChan dos bancos, do jeito que os canais os chamam (tudo de um quadro a outro, para a
//    frente, para trás, recomeçando; um grupo de quatro de uma chave à seguinte; o indexado com o t);
//  - os canais pela fábrica do jogo (NewFnAnim 0x002647c8 num reservatório na RAM emulada, SetAnimMemoryMap, GetLength,
//    Eval pela tabela virtual): todas as animações de pose dos bancos, cada uma numa sequência de tempos (passos de um
//    quadro, saltos para trás e para a frente, tempos inteiros, antes do começo e depois do fim) numa pose de 128 ossos;
//    a pose inteira comparada depois de cada avaliação;
//  - o Skeleton: GetStillPose (0x00262aa8), PoseSQTToLocal (0x002623c0), PoseLocalToGlobal (0x00262508, fora e no
//    lugar) e PoseGlobalToSkin (0x002634f8) com poses e matrizes sorteadas em cada esqueleto;
//  - o ActPoser: SetNormalAnimation (0x001133d0), DoMainPose (0x00113b48), TransformLocalPoseToWorldPose (0x001141e8),
//    Skin (0x00114218), AdvanceTime (0x001145c0) e ActActor::IsAnimationDone (0x0010ac58), com o ActSkeleton, o
//    ActAnimGroup e o ActPoseMatrices montados na RAM emulada, em cada esqueleto com as animações que cabem nele, a
//    PoseToWorld, a escala do tempo e a supressão da translação sorteadas.
// Os dados vão para a RAM emulada carregados duas vezes pelo EAGL::DynamicLoader do porte: as palavras que diferem pelo
// deslocamento das duas cópias são os ponteiros (as relocações), escritos com os endereços emulados.
// Ganchos nativos: memcpy. O que não é comparado: os ponteiros dos objetos (o reservatório do jogo e os objetos C++).

#include "driving_check_chars.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/anim/actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/bigf.h"
#include "driving/eagl_loader.h"
#include "platform/crt_compat.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;
namespace an = drv::anim;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções originais.
constexpr uint32_t kFnSlerp = 0x298478, kFnFastSlerp = 0x2cc018, kFnMat4 = 0x297c60, kFnMat4b = 0x2cbb88;
constexpr uint32_t kFnSqt = 0x2985d0;
constexpr uint32_t kFnDecompress = 0x276820, kFnDecompressIdx = 0x276c70, kFnNewFnAnim = 0x2647c8;
constexpr uint32_t kFnStillPose = 0x262aa8, kFnSqtToLocal = 0x2623c0, kFnLocalToGlobal = 0x262508;
constexpr uint32_t kFnGlobalToSkin = 0x2634f8;
constexpr uint32_t kFnSetNormal = 0x1133d0, kFnDoMainPose = 0x113b48, kFnToWorld = 0x1141e8, kFnSkin = 0x114218;
constexpr uint32_t kFnAdvance = 0x1145c0, kFnIsDone = 0x10ac58;
constexpr uint32_t kFnMemcpy = 0x2a7e84;
// Os globais: o reservatório do EAGLAnim (a base, o ponteiro do próximo bloco, as listas livres por tipo e por classe de
// tamanho) e o fSkinnedTransforms.
constexpr uint32_t kPoolBase = 0x343d54, kPoolBump = 0x343d58, kFreeByType = 0x343d68, kFreeBySize = 0x343dc0;
constexpr uint32_t kSkinned = 0x326160;

// A RAM emulada.
constexpr uint32_t kData = 0x01000000;     // o .dat do banco (ou o .o do esqueleto)
constexpr uint32_t kRel = 0x01300000;      // o .rel
constexpr uint32_t kPool = 0x01400000, kPoolEnd = 0x01700000;
constexpr uint32_t kScratch = 0x01700000;  // entradas e saídas das funções
constexpr uint32_t kPose = 0x01710000;     // a pose de 128 ossos dos canais
constexpr uint32_t kPoser = 0x01720000;    // o ActPoser, o ActSkeleton, o ActAnimGroup, o ActPoseMatrices, o ator
constexpr uint32_t kSkelBufs = 0x01730000;  // as poses e as matrizes do ActSkeleton, as globais e a pele

constexpr int kPoseBones = 128;
constexpr int kPoseSize = kPoseBones * an::kPoseFloats;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1616) {}
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

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    if (t.bad) ++failures;
}

// Compara n bytes palavra a palavra; o primeiro diferente em `out`.
bool same_words(const char* what, const uint8_t* emu, const void* port, uint32_t n, std::string& out) {
    const uint8_t* p = static_cast<const uint8_t*>(port);
    for (uint32_t o = 0; o < n; o += 4) {
        uint32_t x, y;
        std::memcpy(&x, emu + o, 4);
        std::memcpy(&y, p + o, 4);
        if (x != y) {
            out = std::string(what) + " +0x" + hex(o).substr(2) + ": original " + hex(x) + ", porte " + hex(y);
            return false;
        }
    }
    return true;
}

class AnimEmu {
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
        m.hook(kFnMemcpy, "memcpy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 400'000'000);
    }
    // O reservatório do EAGLAnim vazio (o MemoryPoolManager::Init sem o eagl_alloc).
    void reset_pool() {
        m.write32(kPoolBase, kPool);
        m.write32(kPoolBump, kPool);
        for (uint32_t i = 0; i < 22; ++i) m.write32(kFreeByType + 4 * i, 0);
        for (uint32_t i = 0; i < 256; ++i) m.write32(kFreeBySize + 4 * i, 0);
    }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
    void put_f(uint32_t a, float f) { put(a, &f, 4); }
    float get_f(uint32_t a) {
        float f;
        std::memcpy(&f, m.mem(a, 4), 4);
        return f;
    }
    // Uma chamada virtual (a entrada de 8 bytes da tabela do GCC 2.95: o deslocamento em +0, a função em +4).
    void vcall(uint32_t obj, uint32_t slot, std::vector<uint32_t> args) {
        const uint32_t vt = m.read32(obj + 8);
        int16_t delta;
        std::memcpy(&delta, m.mem(vt + slot, 2), 2);
        args.insert(args.begin(), obj + uint32_t(int32_t(delta)));
        call(m.read32(vt + slot + 4), args);
    }
};

// Um objeto do EAGL na RAM emulada: carregado duas vezes pelo DynamicLoader do porte, as palavras que diferem pelo
// deslocamento entre as duas cópias (dentro da .data ou do .rel) são os ponteiros das relocações.
struct ObjCopy {
    uint32_t d1 = 0, r1 = 0, dsz = 0, rsz = 0;  // a primeira cópia do porte (p32) e os tamanhos
    uint32_t emu_d = kData, emu_r = kRel;
    uint32_t to_emu(uint32_t p) const {
        if (p >= d1 && p <= d1 + dsz) return p - d1 + emu_d;
        if (rsz && p >= r1 && p <= r1 + rsz) return p - r1 + emu_r;
        return 0;
    }
};

bool copy_object(AnimEmu& e, const an::EaglObject& a, const an::EaglObject& b, ObjCopy& c, std::string& err) {
    c.d1 = nf::p32_u(a.data());
    c.r1 = a.rel() ? nf::p32_u(a.rel()) : 0;
    c.dsz = a.data_size();
    c.rsz = a.rel_size();
    if (c.dsz > kRel - kData || c.rsz > kPool - kRel || a.data_size() != b.data_size()) {
        err = "objeto grande demais";
        return false;
    }
    const uint32_t d2 = nf::p32_u(b.data()), r2 = b.rel() ? nf::p32_u(b.rel()) : 0;
    auto fix = [&](const uint8_t* p1, const uint8_t* p2, uint32_t n, uint32_t emu) {
        for (uint32_t o = 0; o + 4 <= n; o += 4) {
            uint32_t x, y;
            std::memcpy(&x, p1 + o, 4);
            std::memcpy(&y, p2 + o, 4);
            if (x != y) {
                if (x >= c.d1 && x <= c.d1 + c.dsz && y - x == d2 - c.d1)
                    x = x - c.d1 + c.emu_d;
                else if (c.rsz && x >= c.r1 && x <= c.r1 + c.rsz && y - x == r2 - c.r1)
                    x = x - c.r1 + c.emu_r;
                else {
                    err = "palavra +0x" + hex(o) + " difere entre as cargas sem ser um ponteiro";
                    return false;
                }
            }
            e.m.write32(emu + o, x);
        }
        for (uint32_t o = n & ~3u; o < n; ++o) e.m.mem(emu + o, 1)[0] = p1[o];
        return true;
    };
    if (!fix(a.data(), b.data(), c.dsz, c.emu_d)) return false;
    if (c.rsz && !fix(a.rel(), b.rel(), c.rsz, c.emu_r)) return false;
    return true;
}

// Um quatérnio sorteado (normalizado em double, como os dos dados).
void random_quat(Rng& r, float q[4]) {
    double v[4], l = 0;
    for (double& x : v) {
        x = r.uni(-1.0f, 1.0f);
        l += x * x;
    }
    l = std::sqrt(l > 1e-9 ? l : 1.0);
    for (int k = 0; k < 4; ++k) q[k] = float(v[k] / l);
}

// Os canais de um banco que o DeltaCompressedData lê: os dados do canal, o tipo e o número de quadros ou chaves.
struct DcdUse {
    uint32_t chan, dcd;
    int type, frames, n;
};

int read16(uint32_t a) { return *nf::p32_ptr<const uint16_t>(a); }
uint32_t read32(uint32_t a) { return *nf::p32_ptr<const uint32_t>(a); }

}  // namespace

int run_chars_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                     int cases, uint32_t seed) {
    std::printf("\nparte 16: os personagens (a animação: a libeaglanima e o ActPoser contra o DRIVING.ELF)\n");
    int failures = 0;
    AnimEmu e;
    e.setup(elf, vu0_program, vu0_size);
    Rng rng(seed ^ 0x16161616u);

    // ---- a VU0 ----
    Tally t_slerp, t_slerp2, t_m4, t_m4b, t_sqt;
    {
        const uint32_t A = kScratch, B = kScratch + 0x100, C = kScratch + 0x200;
        for (int i = 0; i < cases; ++i) {
            float q0[4], q1[4], out[4];
            random_quat(rng, q0);
            if (rng.chance(0.3)) {
                for (int k = 0; k < 4; ++k) q1[k] = -q0[k] + rng.uni(-0.05f, 0.05f);
            } else if (rng.chance(0.3)) {
                for (int k = 0; k < 4; ++k) q1[k] = q0[k] + rng.uni(-0.05f, 0.05f);
            } else {
                random_quat(rng, q1);
            }
            float t = rng.uni(-0.25f, 1.25f);
            if (rng.chance(0.1)) t = float(rng.range(0, 1));
            for (int which = 0; which < 2; ++which) {
                e.put(A, q0, 16);
                e.put(B, q1, 16);
                e.m.set_fpr(12, t);
                e.call(which ? kFnFastSlerp : kFnSlerp, {A, B, C});
                an::fast_qslerp(q0, q1, out, t);
                Tally& tt = which ? t_slerp2 : t_slerp;
                ++tt.cases;
                std::string why;
                if (!same_words("q", e.m.mem(C, 16), out, 16, why)) tt.fail(why);
            }
            float a[16], b[16], o[16];
            for (float& x : a) x = rng.uni(-2.0f, 2.0f);
            for (float& x : b) x = rng.uni(-2.0f, 2.0f);
            for (int which = 0; which < 2; ++which) {
                const bool in_place = rng.chance(0.3);
                e.put(A, a, 64);
                e.put(B, b, 64);
                e.call(which ? kFnMat4b : kFnMat4, {A, B, in_place ? A : C});
                std::memcpy(o, a, 64);
                an::MATRIX4_mult(o, b, o);
                Tally& tt = which ? t_m4b : t_m4;
                ++tt.cases;
                std::string why;
                if (!same_words("m", e.m.mem(in_place ? A : C, 64), o, 64, why)) tt.fail(why);
            }
            float s[4], q[4], tr[4];
            for (float& x : s) x = rng.uni(0.25f, 2.0f);
            random_quat(rng, q);
            for (float& x : tr) x = rng.uni(-3.0f, 3.0f);
            e.put(A, s, 16);
            e.put(B, q, 16);
            e.put(B + 0x10, tr, 16);
            e.call(kFnSqt, {A, B, B + 0x10, C});
            an::SQTquattom4(s, q, tr, o);
            ++t_sqt.cases;
            std::string why;
            if (!same_words("m", e.m.mem(C, 64), o, 64, why)) t_sqt.fail(why);
        }
    }

    // ---- os bancos e os esqueletos de todos os .VIV (sem os repetidos) ----
    std::vector<std::string> vivs;
    for (const nf::DiscEntry& d : disc.list("DRIVING")) {
        std::string name = d.path.substr(d.path.find_last_of('/') + 1);
        const size_t semi = name.find(';');
        if (semi != std::string::npos) name.resize(semi);
        if (name.size() > 4 && nf::str_icmp(name.c_str() + name.size() - 4, ".VIV") == 0) vivs.push_back(name);
    }
    Tally t_len, t_eval, t_full, t_group, t_idx, t_still, t_local, t_global, t_global_in, t_skin;
    Tally t_set, t_main, t_world, t_skinned, t_adv, t_done;
    int banks = 0, skeletons = 0, pose_anims = 0, poser_pairs = 0;
    std::map<int, int> bits_seen;  // os canais com DeltaCompressedData por bits por delta
    std::set<std::string> seen_banks, seen_skels;
    uint64_t steps0 = e.m.steps();
    for (const std::string& viv : vivs) {
        drv::Bigf arc;
        if (!arc.open(disc, "DRIVING/" + viv)) continue;
        // Os bancos e os esqueletos deste arquivo, pela chave de entrada (o nome e o tamanho: os repetidos entre os
        // arquivos são iguais).
        std::vector<std::string> bank_names;
        std::vector<int> skel_indices;
        for (const drv::BigfEntry& be : arc.entries()) {
            std::string l = be.name;
            for (char& ch : l) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            const std::string base = drv::bigf_base_name(be.name);
            const std::string key = base + ":" + std::to_string(be.size);
            if (l.rfind("data\\actors\\anims\\", 0) == 0 && l.size() > 4 && l.compare(l.size() - 4, 4, ".dat") == 0) {
                if (seen_banks.insert(key).second) bank_names.push_back(base);
            } else if (l.rfind("data\\actors\\skeleton\\", 0) == 0 && l.size() > 2 &&
                       l.compare(l.size() - 2, 2, ".o") == 0) {
                if (seen_skels.insert(key).second) skel_indices.push_back(std::atoi(base.c_str()));
            }
        }
        // Os esqueletos deste arquivo (para o ActPoser) ficam carregados enquanto os bancos rodam.
        std::vector<std::unique_ptr<an::ActSkeleton>> skels;
        std::vector<int> skel_ok;  // o número de cada esqueleto carregado
        for (int si : skel_indices) {
            auto sk = std::make_unique<an::ActSkeleton>();
            std::string err;
            if (sk->load(arc, si, err)) {
                skels.push_back(std::move(sk));
                skel_ok.push_back(si);
            }
        }
        // ---- o Skeleton ----
        for (size_t sidx = 0; sidx < skels.size(); ++sidx) {
            an::ActSkeleton& sk = *skels[sidx];
            an::EaglObject b;
            std::string err;
            ++skeletons;
            // A segunda cópia do mesmo .o.
            if (!b.load(arc, "data\\actors\\skeleton\\" + std::to_string(skel_ok[sidx]) + "skel.o", std::string(),
                        err))
                continue;
            ObjCopy c;
            c.emu_d = kData;
            if (!copy_object(e, sk.object(), b, c, err)) {
                t_still.fail("cópia do esqueleto: " + err);
                continue;
            }
            const uint32_t skel_e = c.to_emu(sk.skeleton());
            const int n = sk.num_bones();
            const uint32_t P = kSkelBufs, L = kSkelBufs + 0x4000, G = kSkelBufs + 0x8000, S = kSkelBufs + 0xc000;
            const int per = std::max(4, cases / 40);
            std::vector<float> pose(size_t(n) * an::kPoseFloats), local(size_t(n) * 16), global(size_t(n) * 16),
                skin(size_t(n) * 16);
            for (int it = 0; it < per; ++it) {
                std::string why;
                // GetStillPose
                e.call(kFnStillPose, {skel_e, P, 0});
                an::Skeleton_GetStillPose(sk.skeleton(), pose.data());
                ++t_still.cases;
                if (!same_words("pose", e.m.mem(P, uint32_t(pose.size() * 4)), pose.data(), uint32_t(pose.size() * 4), why))
                    t_still.fail(why);
                // PoseSQTToLocal numa pose sorteada
                for (int bi = 0; bi < n; ++bi) {
                    float* p = pose.data() + an::kPoseFloats * bi;
                    for (int k = 0; k < 3; ++k) p[k] = rng.chance(0.5) ? 1.0f : rng.uni(0.5f, 1.5f);
                    p[3] = rng.chance(0.5) ? 1.0f : rng.uni(0.5f, 1.5f);
                    random_quat(rng, p + 4);
                    for (int k = 0; k < 3; ++k) p[8 + k] = rng.uni(-1.0f, 1.0f);
                    p[11] = 1.0f;
                }
                e.put(P, pose.data(), uint32_t(pose.size() * 4));
                e.call(kFnSqtToLocal, {skel_e, P, L, 0});
                an::Skeleton_PoseSQTToLocal(sk.skeleton(), pose.data(), local.data());
                ++t_local.cases;
                if (!same_words("local", e.m.mem(L, uint32_t(local.size() * 4)), local.data(),
                                uint32_t(local.size() * 4), why))
                    t_local.fail(why);
                // PoseLocalToGlobal fora do lugar e no lugar
                e.put(L, local.data(), uint32_t(local.size() * 4));
                std::vector<float> gin(local.size());
                for (float& x : gin) x = rng.uni(-1.0f, 1.0f);  // o que a global tinha (a raiz é copiada)
                e.put(G, gin.data(), uint32_t(gin.size() * 4));
                global = gin;
                e.call(kFnLocalToGlobal, {skel_e, L, G, 0});
                an::Skeleton_PoseLocalToGlobal(sk.skeleton(), local.data(), global.data());
                ++t_global.cases;
                if (!same_words("global", e.m.mem(G, uint32_t(global.size() * 4)), global.data(),
                                uint32_t(global.size() * 4), why))
                    t_global.fail(why);
                std::vector<float> inplace = local;
                e.call(kFnLocalToGlobal, {skel_e, L, L, 0});
                an::Skeleton_PoseLocalToGlobal(sk.skeleton(), inplace.data(), inplace.data());
                ++t_global_in.cases;
                if (!same_words("global", e.m.mem(L, uint32_t(inplace.size() * 4)), inplace.data(),
                                uint32_t(inplace.size() * 4), why))
                    t_global_in.fail(why);
                // PoseGlobalToSkin
                e.call(kFnGlobalToSkin, {skel_e, G, S, 0});
                an::Skeleton_PoseGlobalToSkin(sk.skeleton(), global.data(), skin.data());
                ++t_skin.cases;
                if (!same_words("pele", e.m.mem(S, uint32_t(skin.size() * 4)), skin.data(), uint32_t(skin.size() * 4),
                                why))
                    t_skin.fail(why);
            }
        }
        // ---- os bancos ----
        for (const std::string& bn : bank_names) {
            an::AnimBank bank;
            an::EaglObject b2;
            std::string err;
            if (!bank.load(arc, bn, err) ||
                !b2.load(arc, "data\\actors\\anims\\" + bn + ".dat", "data\\actors\\anims\\" + bn + ".rel", err)) {
                t_len.fail("banco " + bn + ": " + err);
                continue;
            }
            ObjCopy c;
            c.emu_d = kData;
            c.emu_r = kRel;
            if (!copy_object(e, bank.object(), b2, c, err)) {
                t_len.fail("cópia do banco " + bn + ": " + err);
                continue;
            }
            ++banks;
            e.reset_pool();
            // As animações de pose (as de eventos só têm canais do tipo 1) e os canais com DeltaCompressedData.
            std::vector<uint32_t> anims;
            std::vector<DcdUse> dcds;
            for (int i = 0; i < bank.num_anims(); ++i) {
                const uint32_t d = bank.anim(i);
                if (!d || read16(d) != an::kFnCompound) continue;
                const int nsub = read16(d + 8);
                bool events = false;
                for (int k = 0; k < nsub; ++k) {
                    const uint32_t sd = read32(d + 0xc + 4u * uint32_t(k));
                    const int ty = read16(sd);
                    if (ty == an::kFnRawEvent) events = true;
                    if (ty == an::kFnDeltaQuat) dcds.push_back({sd, read32(sd + 4), ty, read16(sd + 8), read16(read32(sd + 4))});
                    if (ty == an::kFnKeyQuat) dcds.push_back({sd, read32(sd + 4), ty, read16(sd + 0xc), read16(read32(sd + 4))});
                }
                if (!events) anims.push_back(d);
            }
            for (const DcdUse& u : dcds) bits_seen[*nf::p32_ptr<const uint8_t>(u.dcd + 2)]++;
            // DecompressValues e DecompressValuesIndexed sobre os dados dos canais.
            const int per_bank = std::max(8, cases / 20);
            const uint32_t CA = kScratch, OUT = kScratch + 0x4000, IDX = kScratch + 0x8000;
            for (int it = 0; it < per_bank && !dcds.empty(); ++it) {
                const DcdUse& u = dcds[size_t(rng.range(0, int(dcds.size()) - 1))];
                if (u.frames < 2 || u.n <= 0 || u.n > 0x400) continue;
                const uint32_t dcd_e = c.to_emu(u.dcd);
                std::vector<float> cache(size_t(u.n)), out(size_t(u.n));
                for (float& x : cache) x = rng.uni(-1.0f, 1.0f);
                out = cache;
                // Tudo, de um quadro a outro (o FnDeltaQuatChan e o primeiro do FnKeyQuatChan), em sequência.
                int from = rng.chance(0.3) ? -1 : rng.range(0, u.frames - 1);
                e.put(CA, cache.data(), uint32_t(cache.size() * 4));
                e.put(OUT, cache.data(), uint32_t(cache.size() * 4));
                for (int s = 0; s < 4; ++s) {
                    const int to = rng.range(0, u.frames - 1);
                    const bool same_buf = rng.chance(0.7);  // o canal passa o mesmo buffer (o do valor em cache)
                    if (same_buf) {
                        e.call(kFnDecompress, {dcd_e, 0, uint32_t(u.n), uint32_t(from), uint32_t(to), OUT, OUT});
                        an::DecompressValues(u.dcd, 0, u.n, from, to, out.data(), out.data());
                    } else {
                        e.put(CA, out.data(), uint32_t(out.size() * 4));
                        std::vector<float> src = out;
                        e.call(kFnDecompress, {dcd_e, 0, uint32_t(u.n), uint32_t(from), uint32_t(to), CA, OUT});
                        an::DecompressValues(u.dcd, 0, u.n, from, to, src.data(), out.data());
                    }
                    ++t_full.cases;
                    std::string why;
                    if (!same_words("valores", e.m.mem(OUT, uint32_t(out.size() * 4)), out.data(),
                                    uint32_t(out.size() * 4), why))
                        t_full.fail(why + " (" + std::to_string(from) + " -> " + std::to_string(to) + ")");
                    from = to;
                }
                // Um grupo de quatro de uma chave à seguinte (o FnKeyQuatChan antes do slerp).
                {
                    const int key = rng.range(0, u.frames - 2);
                    const int first = 4 * rng.range(0, std::max(0, u.n / 4 - 1));
                    float q[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)}, o[4];
                    e.put(CA, q, 16);
                    e.call(kFnDecompress, {dcd_e, uint32_t(first), 4, uint32_t(key), uint32_t(key + 1), CA, OUT});
                    an::DecompressValues(u.dcd, first, 4, key, key + 1, q, o);
                    ++t_group.cases;
                    std::string why;
                    if (!same_words("grupo", e.m.mem(OUT, 16), o, 16, why)) t_group.fail(why);
                }
                // O indexado (o FnDeltaQuatChan entre dois quadros), com os índices do canal.
                if (u.type == an::kFnDeltaQuat) {
                    const int k = rng.range(0, u.frames - 2);
                    const float t = rng.uni(0.0f, 1.0f);
                    std::vector<float> pose(kPoseSize);
                    for (float& x : pose) x = rng.uni(-1.0f, 1.0f);
                    e.put(kPose, pose.data(), uint32_t(pose.size() * 4));
                    e.put(CA, cache.data(), uint32_t(cache.size() * 4));
                    e.m.set_fpr(12, t);
                    e.m.write32(MipsMachine::kStackTop, c.to_emu(u.chan + 0xa));
                    e.call(kFnDecompressIdx,
                           {dcd_e, 0, uint32_t(u.n), uint32_t(k), uint32_t(k + 1), CA, kPose, 4});
                    an::DecompressValuesIndexed(t, u.dcd, 0, u.n, k, k + 1, cache.data(), pose.data(), 4,
                                                nf::p32_ptr<const uint16_t>(u.chan + 0xa), kPoseSize);
                    ++t_idx.cases;
                    std::string why;
                    if (!same_words("pose", e.m.mem(kPose, uint32_t(pose.size() * 4)), pose.data(),
                                    uint32_t(pose.size() * 4), why))
                        t_idx.fail(why);
                }
            }
            // Os canais pela fábrica, cada animação de pose numa sequência de tempos.
            const int evals_per = std::max(3, std::min(40, cases / 100));
            for (uint32_t d : anims) {
                ++pose_anims;
                std::unique_ptr<an::FnAnim> pa = an::NewFnAnim(an::kFnCompound);
                pa->SetAnimMemoryMap(d);
                if (uint32_t(e.m.read32(kPoolBump)) >= kPoolEnd - 0x10000) e.reset_pool();
                e.call(kFnNewFnAnim, {uint32_t(an::kFnCompound)});
                const uint32_t obj = uint32_t(e.m.gpr(R::v0));
                e.vcall(obj, 0x80, {c.to_emu(d)});
                float len = 0.0f;
                pa->GetLength(len);
                e.vcall(obj, 0x28, {kScratch});
                ++t_len.cases;
                {
                    std::string why;
                    if (!same_words("comprimento", e.m.mem(kScratch, 4), &len, 4, why)) t_len.fail(why);
                }
                std::vector<float> pose(kPoseSize);
                for (float& x : pose) x = rng.uni(-1.0f, 1.0f);
                e.put(kPose, pose.data(), uint32_t(pose.size() * 4));
                float prev = 0.0f, tm = rng.uni(0.0f, std::max(1.0f, len));
                for (int s = 0; s < evals_per; ++s) {
                    e.m.set_fpr(12, prev);
                    e.m.set_fpr(13, tm);
                    e.vcall(obj, 0x20, {kPose});
                    pa->Eval(prev, tm, pose.data(), kPoseSize);
                    ++t_eval.cases;
                    std::string why;
                    if (!same_words("pose", e.m.mem(kPose, uint32_t(pose.size() * 4)), pose.data(),
                                    uint32_t(pose.size() * 4), why)) {
                        char b[160];
                        int ndiff = 0;
                        for (int k = 0; k < kPoseSize; ++k)
                            if (std::memcmp(e.m.mem(kPose + 4u * uint32_t(k), 4), &pose[size_t(k)], 4) != 0) ++ndiff;
                        std::snprintf(b, sizeof b, " (%s, animação %08x, t %.4f depois de %.4f, %d floats diferentes)",
                                      bn.c_str(), c.to_emu(d), double(tm), double(prev), ndiff);
                        t_eval.fail(why + b);
                        if (nf::get_env_narrow("NF_P16_DEBUG", nullptr)) {
                            const int nsub = read16(d + 8);
                            for (int k = 0; k < nsub; ++k) {
                                const uint32_t sd = read32(d + 0xc + 4u * uint32_t(k));
                                std::printf("    sub %d tipo %d dados %08x\n", k, read16(sd), c.to_emu(sd));
                                if (read16(sd) == an::kFnDeltaF3 || read16(sd) == an::kFnDeltaF1) {
                                    const uint32_t idx = read32(sd + 4), keys = read32(sd + 8);
                                    std::printf("      chaves %d (%s) canais %d bloco 2^%d constantes %d indices",
                                                read16(sd + 0xc), keys ? "lista" : "quadros", read16(sd + 0xe),
                                                *nf::p32_ptr<const uint8_t>(sd + 0x10),
                                                *nf::p32_ptr<const uint8_t>(sd + 0x11));
                                    for (int q = 0; q < read16(sd + 0xe); ++q) std::printf(" %d", read16(idx + 2u * uint32_t(q)));
                                    std::printf("\n");
                                }
                            }
                            for (int k = 0; k < kPoseSize; ++k) {
                                float ev;
                                std::memcpy(&ev, e.m.mem(kPose + 4u * uint32_t(k), 4), 4);
                                if (std::memcmp(&ev, &pose[size_t(k)], 4) != 0)
                                    std::printf("    [%d] original %.6g porte %.6g\n", k, double(ev), double(pose[size_t(k)]));
                            }
                        }
                        break;
                    }
                    prev = tm;
                    const int kind = rng.range(0, 9);
                    if (kind <= 4) tm += rng.chance(0.5) ? 1.0f / 3.0f : 0.5f;          // um passo
                    else if (kind == 5) tm = rng.uni(0.0f, std::max(1.0f, len));        // um salto
                    else if (kind == 6) tm = float(rng.range(0, std::max(1, int(len)))); // um quadro inteiro
                    else if (kind == 7) tm = rng.uni(-2.0f, 0.0f);                       // antes do começo
                    else if (kind == 8) tm = len + rng.uni(-1.0f, 2.0f);                 // o fim
                    else tm = std::max(0.0f, tm - rng.uni(0.0f, 3.0f));                 // para trás
                }
            }
            // ---- o ActPoser: nos esqueletos deste arquivo, as animações do banco que cabem neles ----
            for (size_t sidx = 0; sidx < skels.size(); ++sidx) {
                an::ActSkeleton& sk = *skels[sidx];
                const int n = sk.num_bones();
                if (sk.d.root < 0 || anims.empty()) continue;
                const int index = skel_ok[sidx];
                an::EaglObject b;
                ObjCopy sc;
                sc.emu_d = kRel + 0x40000;  // o esqueleto depois do .rel do banco
                std::string err2;
                if (!b.load(arc, "data\\actors\\skeleton\\" + std::to_string(index) + "skel.o", std::string(), err2) ||
                    sk.object().data_size() > 0x40000 || !copy_object(e, sk.object(), b, sc, err2))
                    continue;
                const int tries = std::max(1, std::min(4, cases / 1000));
                for (int tr = 0; tr < tries; ++tr) {
                    const uint32_t d = anims[size_t(rng.range(0, int(anims.size()) - 1))];
                    // Só as que cabem: uma avaliação no porte com a pose do esqueleto não pode guardar nada.
                    {
                        std::unique_ptr<an::FnAnim> probe = an::NewFnAnim(an::kFnCompound);
                        probe->SetAnimMemoryMap(d);
                        std::vector<float> pp(size_t(n) * an::kPoseFloats);
                        const uint64_t g0 = an::anim_stats().guards;
                        probe->Eval(0.0f, 0.0f, pp.data(), int(pp.size()));
                        if (an::anim_stats().guards != g0) {
                            an::anim_stats().guards = g0;
                            continue;
                        }
                    }
                    ++poser_pairs;
                    // O porte.
                    sk.GetStillPose();
                    std::fill(sk.pose.begin(), sk.pose.end(), 0.0f);
                    std::fill(sk.matrices.begin(), sk.matrices.end(), 0.0f);
                    an::ActAnimGroup group;
                    group.ChangeAnimationData(d, 0);
                    const float frame_time = rng.chance(0.5) ? 1.0f / 60.0f : rng.uni(0.005f, 0.05f);
                    an::ActPoser poser(&sk, frame_time);
                    poser.suppress_translation = rng.chance(0.3) ? 1 : 0;
                    float pw[16] = {};
                    {
                        float q[4], s[4] = {1, 1, 1, 1}, t[4] = {rng.uni(-50, 50), rng.uni(-5, 5), rng.uni(-50, 50), 1};
                        random_quat(rng, q);
                        an::SQTquattom4(s, q, t, pw);
                    }
                    std::memcpy(poser.pose_to_world(), pw, 64);
                    // O original: o ActSkeleton (0x24), o ActAnimGroup (0x20), o ActPoseMatrices (0x10c), o ActPoser
                    // (0x58) e um ator com o poser em +4; os buffers do esqueleto, as globais e a pele.
                    const uint32_t ask = kPoser, agr = kPoser + 0x40, apm = kPoser + 0x80, apo = kPoser + 0x200,
                                   act = kPoser + 0x280;
                    const uint32_t still = kSkelBufs, posebuf = kSkelBufs + 0x2000, mats = kSkelBufs + 0x4000,
                                   cur = kSkelBufs + 0x8000, skin = kSkelBufs + 0xc000;
                    std::memset(e.m.mem(kPoser, 0x400), 0, 0x400);
                    std::memset(e.m.mem(kSkelBufs, 0x10000), 0, 0x10000);
                    e.m.write32(ask + 8, sc.to_emu(sk.skeleton()));
                    e.m.write32(ask + 0xc, still);
                    e.m.write32(ask + 0x10, posebuf);
                    e.m.write32(ask + 0x14, mats);
                    e.m.write32(ask + 0x18, uint32_t(sk.d.root));
                    e.m.write32(ask + 0x1c, uint32_t(sk.d.weapon));
                    e.m.write32(ask + 0x20, uint32_t(sk.d.clip));
                    e.call(kFnStillPose, {sc.to_emu(sk.skeleton()), still, 0});  // o construtor do ActSkeleton
                    if (uint32_t(e.m.read32(kPoolBump)) >= kPoolEnd - 0x10000) e.reset_pool();
                    e.call(kFnNewFnAnim, {uint32_t(an::kFnCompound)});
                    const uint32_t obj = uint32_t(e.m.gpr(R::v0));
                    // A animação do banco na cópia emulada do banco.
                    e.vcall(obj, 0x80, {c.to_emu(d)});
                    e.vcall(obj, 0x28, {agr});  // o ChangeAnimation: GetLength no comprimento do grupo
                    e.m.write32(agr + 8, obj);
                    e.m.write32(agr + 0xc, c.to_emu(d));
                    e.put(apm + 0xc0, pw, 64);
                    e.m.write32(apm + 0x100, cur);
                    e.m.write32(apm + 0x104, cur);
                    e.m.write32(apm + 0x108, uint32_t(n));
                    e.m.write32(apo + 8, ask);
                    e.m.write32(apo + 0x28, apm);
                    e.put_f(apo + 0x2c, 20.0f);
                    e.put_f(apo + 0x30, 0.05f);
                    e.put_f(apo + 0x34, frame_time);
                    e.put_f(apo + 0x38, 1.0f);
                    e.m.write32(apo + 0x50, uint32_t(poser.suppress_translation));
                    e.m.write32(act + 4, apo);
                    e.m.write32(kSkinned, skin);
                    const float t0 = rng.uni(0.0f, std::max(0.05f, group.length / 20.0f));
                    e.m.set_fpr(12, t0);
                    e.call(kFnSetNormal, {apo, agr});
                    poser.SetNormalAnimation(&group, t0);
                    auto times_ok = [&](Tally& tt, const char* where) {
                        const float pt[5] = {poser.time, poser.next_time, poser.previous_time, poser.length,
                                             poser.end_time};
                        std::string why;
                        if (!same_words("tempos", e.m.mem(apo + 0xc, 20), pt, 20, why)) {
                            tt.fail(std::string(where) + ": " + why);
                            return false;
                        }
                        return true;
                    };
                    ++t_set.cases;
                    times_ok(t_set, "SetNormalAnimation");
                    std::vector<float> skin_p(size_t(n) * 16);
                    const int steps = std::max(4, std::min(40, cases / 100));
                    for (int st = 0; st < steps; ++st) {
                        if (rng.chance(0.1)) {
                            const float ts = rng.uni(0.25f, 2.0f);
                            e.put_f(apo + 0x38, ts);
                            poser.time_scale = ts;
                        }
                        e.call(kFnDoMainPose, {apo});
                        poser.DoMainPose();
                        ++t_main.cases;
                        std::string why;
                        if (times_ok(t_main, "DoMainPose") &&
                            (!same_words("pose parada", e.m.mem(still, uint32_t(n * 48)), sk.still_pose.data(),
                                         uint32_t(n * 48), why) ||
                             !same_words("locais", e.m.mem(mats, uint32_t(n * 64)), sk.matrices.data(), uint32_t(n * 64),
                                         why)))
                            t_main.fail(why);
                        e.call(kFnToWorld, {apo});
                        poser.TransformLocalPoseToWorldPose();
                        ++t_world.cases;
                        if (!same_words("globais", e.m.mem(cur, uint32_t(n * 64)), poser.current(),
                                        uint32_t(n * 64), why))
                            t_world.fail(why);
                        e.call(kFnSkin, {apo});
                        poser.Skin(skin_p.data());
                        ++t_skinned.cases;
                        if (!same_words("pele", e.m.mem(skin, uint32_t(n * 64)), skin_p.data(), uint32_t(n * 64), why))
                            t_skinned.fail(why);
                        e.call(kFnAdvance, {apo});
                        poser.AdvanceTime();
                        ++t_adv.cases;
                        times_ok(t_adv, "AdvanceTime");
                        e.call(kFnIsDone, {act});
                        ++t_done.cases;
                        if ((uint32_t(e.m.gpr(R::v0)) != 0) != poser.IsAnimationDone())
                            t_done.fail("IsAnimationDone no passo " + std::to_string(st));
                        if (poser.IsAnimationDone() && rng.chance(0.5)) {
                            // Recomeça (o que o controlador do ator faz no fim de uma animação em laço).
                            const float t1 = rng.uni(0.0f, 0.2f);
                            e.m.set_fpr(12, t1);
                            e.call(kFnSetNormal, {apo, agr});
                            poser.SetNormalAnimation(&group, t1);
                        }
                    }
                }
            }
        }
    }
    std::printf("  %d bancos, %d animações de pose, %d esqueletos, %d pares esqueleto x animação no ActPoser; "
                "%" PRIu64 " instruções do original\n",
                banks, pose_anims, skeletons, poser_pairs, e.m.steps() - steps0);
    std::printf("  canais com DeltaCompressedData por bits:");
    for (const auto& kv : bits_seen) std::printf(" %d=%d", kv.first, kv.second);
    std::printf("\n");
    report("slerp rápido 0x00298478", t_slerp, failures);
    report("VU0_fastqslerp 0x002cc018", t_slerp2, failures);
    report("VU0_MATRIX4_mult 0x00297c60", t_m4, failures);
    report("VU0_MATRIX4_mult 0x002cbb88", t_m4b, failures);
    report("VU0_SQTquattom4", t_sqt, failures);
    report("DecompressValues (todos os canais)", t_full, failures);
    report("DecompressValues (um grupo, de uma chave à seguinte)", t_group, failures);
    report("DecompressValuesIndexed", t_idx, failures);
    report("FnCompoundChannel::GetLength", t_len, failures);
    report("os canais pela fábrica: Eval (11, 13, 15, 20, 21)", t_eval, failures);
    report("Skeleton::GetStillPose", t_still, failures);
    report("Skeleton::PoseSQTToLocal", t_local, failures);
    report("Skeleton::PoseLocalToGlobal", t_global, failures);
    report("Skeleton::PoseLocalToGlobal (no lugar)", t_global_in, failures);
    report("Skeleton::PoseGlobalToSkin", t_skin, failures);
    report("ActPoser::SetNormalAnimation", t_set, failures);
    report("ActPoser::DoMainPose", t_main, failures);
    report("ActPoser::TransformLocalPoseToWorldPose", t_world, failures);
    report("ActPoser::Skin", t_skinned, failures);
    report("ActPoser::AdvanceTime", t_adv, failures);
    report("ActActor::IsAnimationDone", t_done, failures);
    return failures;
}

}  // namespace drvcheck
