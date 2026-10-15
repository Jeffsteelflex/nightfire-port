// A camada dos atores (actor.h): o esqueleto, o grupo de animações e o poser, do DRIVING.ELF.

#include "driving/anim/actor.h"

#include <cstring>

#include "driving/bigf.h"
#include "driving/camera/camera_math.h"
#include "driving/eagl_loader.h"
#include "driving/real/vu0_math.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv::anim {

namespace {

ActorStats g_stats;

inline uint32_t w32(uint32_t a) { return *nf::p32_ptr<const uint32_t>(a); }
inline float* fp(uint32_t a) { return nf::p32_ptr<float>(a); }

using ps2::ee::add;
using ps2::ee::lt;
using ps2::ee::mul;
using ps2::ee::sub;

// O registrador vf0 da VU0 (0, 0, 0, 1).
constexpr uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};

void identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5) == 0 ? 1.0f : 0.0f;
}

// Nega a primeira linha (x, y, z) da matriz (o espelho do Rotate e do CalcSnap).
void negate_row0(float* m) {
    m[0] = ps2::ee::neg(m[0]);
    m[1] = ps2::ee::neg(m[1]);
    m[2] = ps2::ee::neg(m[2]);
}

}  // namespace

ActorStats& actor_stats() { return g_stats; }
void reset_actor_stats() { g_stats = ActorStats{}; }

EaglObject::EaglObject() = default;
EaglObject::~EaglObject() = default;

bool EaglObject::load(Bigf& arc, const std::string& dat_key, const std::string& rel_key, std::string& error,
                      SymbolResolver resolver) {
    std::vector<uint8_t> dat, rel;
    const int di = arc.find(dat_key);
    if (di < 0) {
        error = "sem " + dat_key;
        return false;
    }
    arc.read(di, dat);
    if (!rel_key.empty()) {
        const int ri = arc.find(rel_key);
        if (ri < 0) {
            error = "sem " + rel_key;
            return false;
        }
        arc.read(ri, rel);
    }
    dat_store_.assign(dat.size() + 16, 0);
    rel_store_.assign(rel.size() + 16, 0);
    dat_ = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(dat_store_.data()) + 15) & ~uintptr_t(15));
    rel_ = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(rel_store_.data()) + 15) & ~uintptr_t(15));
    std::memcpy(dat_, dat.data(), dat.size());
    if (!rel.empty()) std::memcpy(rel_, rel.data(), rel.size());
    dat_size_ = uint32_t(dat.size());
    rel_size_ = uint32_t(rel.size());
    if (rel.empty()) rel_ = nullptr;
    auto none = [](const char*, bool& found) -> uint32_t {
        found = false;
        return 0;
    };
    eagl::Resolver res = none;
    if (resolver) res = [resolver](const char* name, bool& found) { return resolver(name, found); };
    loader_ = rel_key.empty() ? std::make_unique<eagl::DynamicLoader>(dat_, dat_size_, res)
                              : std::make_unique<eagl::DynamicLoader>(dat_, dat_size_, rel_, false, res);
    if (!loader_->ok()) {
        error = "o DynamicLoader recusou " + dat_key;
        loader_.reset();
        return false;
    }
    return true;
}

uint32_t EaglObject::first_of_type(const char* type) const {
    if (!loader_) return 0;
    for (int i = 0; i < loader_->GetCount(); ++i) {
        const eagl::SymbolInfo s = loader_->GetSymbol(i);
        if (s.defined && s.type && std::strcmp(s.type, type) == 0) return s.addr;
    }
    return 0;
}

bool AnimBank::load(Bigf& arc, const std::string& name, std::string& error, SymbolResolver resolver) {
    const std::string base = "data\\actors\\anims\\" + name;
    if (!obj_.load(arc, base + ".dat", base + ".rel", error, resolver)) return false;
    bank_ = obj_.first_of_type("AnimationBank");
    if (!bank_) {
        error = "sem AnimationBank em " + name;
        return false;
    }
    return true;
}

// [driving] 0x0010c788 EAGLAnim::FnDefaultAnimBank::GetNumAnims
int AnimBank::num_anims() const { return bank_ ? int(w32(bank_ + 4)) : 0; }

// [driving] 0x0010c798 EAGLAnim::FnDefaultAnimBank::GetAnim(int)
uint32_t AnimBank::anim(int index) const {
    if (index < 0 || index >= num_anims()) return 0;
    return w32(w32(bank_ + 0xc) + 4u * uint32_t(index));
}

// [driving] 0x0010c7b0 EAGLAnim::FnDefaultAnimBank::GetAnimName(int)
const char* AnimBank::anim_name(int index) const {
    if (index < 0 || index >= num_anims()) return "";
    return nf::p32_ptr<const char>(w32(w32(bank_ + 0x10) + 4u * uint32_t(index)));
}

// [driving] 0x0026ddd0 a busca binária do FnDefaultAnimBank::GetAnimIndex(char *) (strcmp nos nomes em ordem)
int AnimBank::find(const char* name) const {
    int hi = num_anims() - 1, lo = 0, mid = hi;
    if (hi < 0) return -1;
    do {
        mid >>= 1;
        const int c = std::strcmp(name, anim_name(mid));
        if (c < 1) {
            if (c > -1) return mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
        mid = lo + hi;
    } while (lo <= hi);
    return -1;
}

ActSkeleton::ActSkeleton() = default;

void ActSkeleton::alloc_buffers() {
    const int n = num_bones();
    still_pose.assign(size_t(n) * kPoseFloats, 0.0f);
    pose.assign(size_t(n) * kPoseFloats, 0.0f);
    matrices.assign(size_t(n) * kMatrixFloats, 0.0f);
    d.still_pose = nf::p32_u(still_pose.data());
    d.pose = nf::p32_u(pose.data());
    d.matrices = nf::p32_u(matrices.data());
}

// [driving] 0x00114760 ActSkeleton::ActSkeleton
bool ActSkeleton::load(Bigf& arc, int index, std::string& error) {
    const std::string key = "data\\actors\\skeleton\\" + std::to_string(index) + "skel.o";
    if (!obj_.load(arc, key, std::string(), error)) return false;
    d = ActSkeletonData{};
    d.file = nf::p32_u(obj_.data());
    d.loader = nf::p32_u(obj_.loader());
    d.skeleton = obj_.first_of_type("Skeleton");
    if (!d.skeleton) {
        error = "sem Skeleton em " + key;
        return false;
    }
    // O fSkinnedTransforms (0x326160) cresce para o maior esqueleto: no port quem desenha dá o seu buffer ao Skin.
    alloc_buffers();
    GetStillPose();
    d.root = bone_index("Root.Root");
    d.weapon = bone_index("Root.Weapon");
    d.clip = bone_index("Root.Clip");
    if (d.weapon == -1) d.weapon = bone_index("Root.RWeapon");
    return true;
}

void ActSkeleton::attach(uint32_t skel, int root) {
    d = ActSkeletonData{};
    d.skeleton = skel;
    alloc_buffers();
    GetStillPose();
    d.root = root;
    d.weapon = -1;
    d.clip = -1;
}

int ActSkeleton::bone_index(const char* name) const { return ActSkeleton_GetBoneIndex(d, name); }

// [driving] 0x00114a08 ActSkeleton::GetBoneIndex
int ActSkeleton_GetBoneIndex(const ActSkeletonData& s, const char* name) {
    uint32_t addr = 0;
    if (!s.loader) return -1;
    const eagl::DynamicLoader* loader = nf::p32_ptr<const eagl::DynamicLoader>(s.loader);
    if (!loader->GetAddr("Bone", name, addr) || !addr) return -1;
    return int(w32(addr));
}

// [driving] 0x0010cac0 ActAnimGroup::~ActAnimGroup
ActAnimGroup::~ActAnimGroup() {
    delete anim_fn();
    delete events_fn();
}

void ActAnimGroup::ChangeAnimationData(uint32_t pose_data, uint32_t event_data) {
    anim_data = pose_data;
    events_data = event_data;
    if (anim_data == 0) {
        length = 0.0f;  // a animação anterior fica (o jogo não a solta)
    } else {
        delete anim_fn();  // o FnAnim anterior volta à lista livre do tipo
        FnAnim* a = NewFnAnim(*nf::p32_ptr<const uint16_t>(anim_data)).release();
        anim = a ? nf::p32_u(a) : 0u;
        if (a) {
            a->SetAnimMemoryMap(anim_data);
            a->GetLength(length);
        } else {
            ++g_stats.guards;  // um tipo que a fábrica do port não tem
        }
    }
    if (events_data == 0) {
        events_length = 0.0f;
    } else {
        delete events_fn();
        FnAnim* a = NewFnAnim(*nf::p32_ptr<const uint16_t>(events_data)).release();
        events = a ? nf::p32_u(a) : 0u;
        if (a) {
            a->SetAnimMemoryMap(events_data);
            a->GetLength(events_length);
        } else {
            ++g_stats.guards;
        }
    }
}

// [driving] 0x00112928 ActPoser::ActPoser
ActPoser::ActPoser(ActSkeleton* skel_, uint32_t events_, int32_t mirrored_, int32_t has_callback, float frame_time_)
    : has_matrix_callback(has_callback),
      mirrored(mirrored_),
      skeleton(skel_ ? nf::p32_u(&skel_->d) : 0u),
      events(events_),
      frame_time(frame_time_) {
    // __builtin_new(0x10) x 2: os uns e os zeros.
    float* ones = new float[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float* zeros = new float[4]{0.0f, 0.0f, 0.0f, 0.0f};
    one4 = nf::p32_u(ones);
    zero4 = nf::p32_u(zeros);
    ActPoseMatrices* m = new ActPoseMatrices{};
    const int n = skel_ ? skel_->num_bones() : 0;
    m->bones = n;
    float* g = new float[size_t(n > 0 ? n : 1) * kMatrixFloats]{};
    m->globals = m->globals2 = nf::p32_u(g);
    // O PS2 não inicia as quatro matrizes (o CalcSnapAndCorrectionMatrices as escreve no primeiro SetNormalAnimation com
    // matriz); o port as deixa identidade (o --actor do nf_driving e os testes posam sem matriz).
    identity(m->initial);
    identity(m->correction);
    identity(m->snap);
    identity(m->pose_to_world);
    matrices = nf::p32_u(m);
    ActIKSolverArrayData* ik_ = new ActIKSolverArrayData{};
    if (skel_) {
        ik_->root = skel_->d.root;
        ik_->skeleton = skel_->d.skeleton;
        ik_->matrices = skel_->d.matrices;
    }
    ik_->globals = m->globals;
    ik = nf::p32_u(ik_);
    ActGlobalPoseOverrideArrayData* ov = new ActGlobalPoseOverrideArrayData{};
    ov->globals = m->globals;
    overrides = nf::p32_u(ov);
}

// [driving] 0x00112a70 ActPoser::~ActPoser
ActPoser::~ActPoser() {
    if (overrides) ActGlobalPoseOverrideArray_DeleteGlobalPoseOverrides(*nf::p32_ptr<ActGlobalPoseOverrideArrayData>(overrides));
    delete nf::p32_ptr<ActGlobalPoseOverrideArrayData>(overrides);
    delete nf::p32_ptr<ActIKSolverArrayData>(ik);
    if (matrices) {
        delete[] nf::p32_ptr<float>(pm().globals2);
        delete nf::p32_ptr<ActPoseMatrices>(matrices);
    }
    delete[] nf::p32_ptr<float>(one4);
    delete[] nf::p32_ptr<float>(zero4);
    if (crossfade) ++g_stats.crossfade;  // o ActCrossFadeBlendData (não portado) nunca é criado no port
}

// [driving] 0x00112b78 ActPoser::GetRootBonePosOri
void ActPoser::GetRootBonePosOri(float* out) const {
    if (skel().root < 0 || skel().root >= pm().bones) {
        ++g_stats.guards;  // o PS2 leria a matriz antes ou depois das globais
        return;
    }
    std::memcpy(out, current() + kMatrixFloats * skel().root, 64);
}

// [driving] 0x00112c18 ActPoser::GetWeaponBonePosOri(int, MATRIX4 &): a arma 0 (+0x1c do esqueleto) ou 1 (+0x20)
void ActPoser::GetWeaponBonePosOri(int weapon, float* out) const {
    const int32_t bone = weapon == 0 ? skel().weapon : (weapon == 1 ? skel().clip : -1);
    if (bone < 0 || bone >= pm().bones) {
        ++g_stats.guards;  // o PS2 leria as matrizes antes ou depois das globais (um osso -1, uma arma além da 1)
        return;
    }
    std::memcpy(out, current() + kMatrixFloats * bone, 64);
}

// [driving] 0x00112cc0 ActPoser::ChangeAnimationOrigin(COORD3 &)
void ActPoser::ChangeAnimationOrigin(const float* origin) { std::memcpy(pm().initial + 12, origin, 12); }

// [driving] 0x00112ce8 ActPoser::GetAnimationOrigin(COORD3 &)
void ActPoser::GetAnimationOrigin(float* out) const { std::memcpy(out, pm().initial + 12, 12); }

// [driving] 0x00112d10 ActPoser::SetAnimationOrigin(COORD3 &)
void ActPoser::SetAnimationOrigin(const float* origin) {
    std::memcpy(pm().initial + 12, origin, 12);
    CalculateFinalCorrectionMatrix();
}

// [driving] 0x00112d50 ActPoser::GetInitialTbOu(MATRIX4 &)
void ActPoser::GetInitialTbOu(float* out) const { std::memcpy(out, pm().initial, 64); }

// [driving] 0x00112d80 ActPoser::SetInitialTbOu(MATRIX4 &)
void ActPoser::SetInitialTbOu(const float* m) {
    std::memcpy(pm().initial, m, 64);
    CalculateFinalCorrectionMatrix();
}

// [driving] 0x00112dc0 ActPoser::Rotate(float voltas): a rotação em y à esquerda da matriz inicial
void ActPoser::Rotate(float turns) {
    // cosf e sinf do newlib: o nf::fm (a parte 20 do nf_driving_check põe o mesmo no original).
    const float a = mul(turns, ps2::ffloat(0x40c90fdbu));
    const float c = nf::fm::cos(a), s = nf::fm::sin(a);
    float r[16] = {c, 0.0f, ps2::ee::neg(s), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, s, 0.0f, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float* m = pm().initial;
    if (mirrored == 1) negate_row0(m);
    MATRIX4_mult(r, m, m);
    if (mirrored == 1) negate_row0(m);
    CalculateFinalCorrectionMatrix();
}

// [driving] 0x00112ef0 ActPoser::RotateX(float voltas)
void ActPoser::RotateX(float turns) {
    const float a = mul(turns, ps2::ffloat(0x40c90fdbu));
    const float c = nf::fm::cos(a), s = nf::fm::sin(a);
    float r[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, s, 0.0f, 0.0f, ps2::ee::neg(s), c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float* m = pm().initial;
    if (mirrored == 1) negate_row0(m);
    MATRIX4_mult(r, m, m);
    if (mirrored == 1) negate_row0(m);
    CalculateFinalCorrectionMatrix();
}

// [driving] 0x00113020 ActPoser::CalculateFinalCorrectionMatrix
void ActPoser::CalculateFinalCorrectionMatrix() {
    ActPoseMatrices& m = pm();
    MATRIX4_mult(m.correction, m.snap, m.pose_to_world);
    MATRIX4_mult(m.pose_to_world, m.initial, m.pose_to_world);
}

// [driving] 0x00113068 ActPoser::SnapMatrixLevelWithGround(MATRIX4 &)
void ActPoser::SnapMatrixLevelWithGround(float* m) {
    using namespace ps2::vu;
    // A linha 2 sem o y, normalizada (vmul.xyz, vmulax.x, vmadday.x, vmaddz.x, vrsqrt, vmulq.xyz).
    m[9] = 0.0f;
    const uint32_t one[4] = {0x3f800000u, 0, 0, 0};
    auto normalize = [&](uint32_t* v) {
        Reg sq, acc, len;
        mul(sq, v, v, XYZ);
        mulabc(acc, v, v, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(len, acc, one, sq, BZ, X);
        const uint32_t q = ps2::rsqrt(kVf0[3], len[0]);
        Reg out;
        muls(out, v, q, XYZ);
        std::memcpy(v, out, 12);  // .xyz: o w fica (o sqc2 grava o w do registrador, que o lqc2 trouxe)
    };
    uint32_t r2[4];
    std::memcpy(r2, m + 8, 16);
    normalize(r2);
    std::memcpy(m + 8, r2, 16);
    m[4] = 0.0f;
    m[5] = 1.0f;
    m[6] = 0.0f;
    // A linha 0 = linha 1 x linha 2 (vopmula.xyz, vopmsub.xyz), normalizada; o w é o da linha 0 (o lqc2 vf3, 0x0).
    uint32_t r1[4], r0[4], acc[4];
    std::memcpy(r1, m + 4, 16);
    std::memcpy(r0, m + 0, 16);
    opmula(acc, r1, r2);
    opmsub(r0, acc, r2, r1);
    normalize(r0);
    std::memcpy(m + 0, r0, 16);
}

// [driving] 0x001130f8 ActPoser::CalcSnapAndCorrectionMatrices(MATRIX4 &)
void ActPoser::CalcSnapAndCorrectionMatrices(const float* m) {
    ActPoseMatrices& p = pm();
    std::memmove(p.initial, m, 64);
    if (has_matrix_callback == 0) {
        SnapMatrixLevelWithGround(p.initial);
        if (has_matrix_callback == 0) p.initial[13] = 0.0f;
    }
    if (mirrored == 1) negate_row0(p.initial);
    ActAnimGroup* g = group();
    FnAnim* a = (g && g->anim_data != 0) ? g->anim_fn() : nullptr;
    if (!a) {
        ++g_stats.guards;  // o PS2 chamaria a tabela virtual do objeto nulo
    } else {
        a->Eval(previous_time, time, fp(skel().still_pose), Skeleton_NumBones(skel().skeleton) * kPoseFloats);
    }
    float* globals = current();
    Skeleton_PoseSQTToGlobal(skel().skeleton, fp(skel().still_pose), globals);
    const float* root = globals + kMatrixFloats * skel().root;
    if (has_matrix_callback == 0) {
        // A correção: a rotação em y que desfaz a direção da raiz (o seno é o +0x20 da raiz) e a translação dela.
        const float s = root[8];
        const float c = ps2::ee::sqrt(sub(1.0f, mul(s, s)));
        float* sn = p.snap;
        std::memset(sn, 0, 64);
        sn[0] = c;
        sn[2] = s;
        sn[5] = 1.0f;
        sn[8] = ps2::ee::neg(s);
        sn[10] = c;
        sn[15] = 1.0f;
        float* co = p.correction;
        std::memset(co, 0, 64);
        co[0] = 1.0f;
        co[5] = 1.0f;
        co[10] = 1.0f;
        co[15] = 1.0f;
        co[12] = ps2::ee::neg(root[12]);
        co[13] = 0.0f;
        co[14] = ps2::ee::neg(root[14]);
    } else {
        identity(p.snap);
        identity(p.correction);
    }
    CalculateFinalCorrectionMatrix();
}

// [driving] 0x001133d0 ActPoser::SetNormalAnimation(ActAnimGroup *, float)
void ActPoser::SetNormalAnimation(ActAnimGroup* g, float t) {
    t = mul(t, fps);
    const float step = mul(mul(frame_time, fps), time_scale);
    mode = 1;
    animation = nf::p32_u(g);
    time = t;
    end_time = g->length;
    length = g->length;
    next_time = t;
    previous_time = sub(t, step);
    if (lt(previous_time, 0.0f)) previous_time = 0.0f;
}

// [driving] 0x00113430 ActPoser::SetNormalAnimation(ActAnimGroup *, float, MATRIX4 &)
void ActPoser::SetNormalAnimation(ActAnimGroup* g, float t, const float* m) {
    SetNormalAnimation(g, t);
    CalcSnapAndCorrectionMatrices(m);
}

// [driving] 0x00113ae8 ActPoser::DoSkeletonPose
void ActPoser::DoSkeletonPose() {
    DoMainPose();
    DoIK();
    ActGlobalPoseOverrideArrayData& ov = *nf::p32_ptr<ActGlobalPoseOverrideArrayData>(overrides);
    if (ov.count > 0) {
        TransformLocalPoseToWorldPose();
        ++g_stats.pose_overrides;
        ActGlobalPoseOverrideArray_DoGlobalOverrides(ov, mirrored, fp(skel().matrices));
    }
    TransformLocalPoseToWorldPose();
}

// [driving] 0x00113b48 ActPoser::DoMainPose (o modo 1; os modos 2 e 3 contam em actor_stats())
void ActPoser::DoMainPose() {
    ActSkeletonData& sk = skel();
    Skeleton_GetStillPose(sk.skeleton, fp(sk.still_pose));
    const float step = mul(mul(frame_time, fps), time_scale);
    time = next_time;
    previous_time = sub(next_time, step);
    if (lt(previous_time, 0.0f)) previous_time = 0.0f;
    if (mode != 1) {
        if (mode == 2 || mode == 3) ++g_stats.crossfade;  // os modos 2 e 3 não são portados
        return;
    }
    ActAnimGroup* g = group();
    if (!g || g->anim_data == 0) return;
    FnAnim* a = g->anim_fn();
    if (!a) {  // um tipo de canal não portado (o PS2 chamaria a tabela virtual de um objeto do tipo)
        ++anim_stats().guards;
        return;
    }
    const int n = Skeleton_NumBones(sk.skeleton);
    a->Eval(previous_time, time, fp(sk.still_pose), n * kPoseFloats);
    Skeleton_PoseSQTToLocal(sk.skeleton, fp(sk.still_pose), fp(sk.matrices));
    if (sk.root < 0 || sk.root >= n) {
        ++anim_stats().guards;
        return;
    }
    float* root = fp(sk.matrices) + kMatrixFloats * sk.root;
    if (suppress_translation) {
        root[14] = 0.0f;  // +0x38
        root[12] = 0.0f;  // +0x30
    }
    MATRIX4_mult(root, pose_to_world(), root);  // 0x002cbb88, a cópia da librealmema
}

// [driving] 0x00114180 ActPoser::DoIK (os solvers não são portados: contados)
void ActPoser::DoIK() {
    const int32_t n = nf::p32_ptr<const ActIKSolverArrayData>(ik)->count;
    for (int32_t i = 0; i < n; ++i) {
        TransformLocalPoseToWorldPose();
        ++g_stats.ik_solvers;  // ActIKSolverArray::Solve (0x0010f0b8, não portado)
    }
}

// [driving] 0x001141e8 ActPoser::TransformLocalPoseToWorldPose
void ActPoser::TransformLocalPoseToWorldPose() {
    Skeleton_PoseLocalToGlobal(skel().skeleton, fp(skel().matrices), current());
}

// [driving] 0x00114218 ActPoser::Skin: o EAGLAnimationBuffer (16 floats por osso) em `skin` (o fSkinnedTransforms).
void ActPoser::Skin(float* skin) const { Skeleton_PoseGlobalToSkin(skel().skeleton, current(), skin); }

// [driving] 0x00114250 ActPoser::DoEventPose (o modo 1 e o 3; o 2 conta em actor_stats())
void ActPoser::DoEventPose() {
    float prev = previous_time;
    if (ps2::ee::eq(prev, time) && ps2::ee::eq(time, 0.0f)) prev = ps2::ffloat(0xb8d1b717u);  // -0,0001
    ActAnimGroup* g = group();
    if (mode == 1) {
        if (!g || g->events_data == 0) return;
        FnAnim* e = g->events_fn();
        if (!e) {
            ++g_stats.guards;
            return;
        }
        e->EvalEvent(prev, time, events, 0);
    } else if (mode == 2) {
        ++g_stats.crossfade;  // o CalcCrossFadeTimes e os eventos das duas animações (não portados)
    } else if (mode == 3) {
        if (!g || g->events_data == 0) return;
        FnAnim* e = g->events_fn();
        if (!e) {
            ++g_stats.guards;
            return;
        }
        float a, b;
        std::memcpy(&a, &manual[5], 4);  // +0x6c
        std::memcpy(&b, &manual[6], 4);  // +0x70
        e->EvalEvent(a, b, events, 0);
    }
}

// [driving] 0x001143b8 ActPoser::DoInitialPoses
void ActPoser::DoInitialPoses() {
    const float saved = next_time;
    next_time = 0.0f;
    DoSkeletonPose();
    next_time = saved;
}

// [driving] 0x001145c0 ActPoser::AdvanceTime (o modo 1; o 2 terminaria o crossfade, contado)
void ActPoser::AdvanceTime() {
    if (mode == 3) return;
    // GetCurrentFrame (0x001146d0) + FrameStep < GetTotalAnimFrames (0x001146d8) - 1
    if (lt(add(time, FrameStep()), sub(end_time, 1.0f))) {
        next_time = add(time, mul(mul(frame_time, fps), time_scale));
    } else if (mode == 2) {
        ++g_stats.crossfade;  // FinishCrossFadeBlend (não portado)
    }
}

// [driving] 0x001146b8 ActPoser::FrameStep
float ActPoser::FrameStep() const { return mul(mul(frame_time, fps), time_scale); }

// [driving] 0x0010ac58 ActActor::IsAnimationDone (sobre o poser do ator)
bool ActPoser::IsAnimationDone() const {
    if (unsigned(mode - 2) <= 1u) return false;
    return ps2::ee::le(sub(end_time, 1.0f), add(time, mul(mul(frame_time, fps), time_scale)));
}

// ----------------------------------------------------------------------- as sobreposições globais (seção 26)

namespace {
float (*m44(float* m))[4] { return reinterpret_cast<float(*)[4]>(m); }
const float (*cm44(const float* m))[4] { return reinterpret_cast<const float(*)[4]>(m); }
// Os globais do BlendMatrices (0x326070 a escala, 0x326080 o quatérnio de a, 0x326090 a translação, 0x3260a0 o de b).
alignas(16) float g_blend_scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
alignas(16) float g_blend_qa[4], g_blend_trans[4], g_blend_qb[4];
}  // namespace

// [driving] 0x0010f140 ActGlobalPoseOverrideArray::CreateGlobalPoseOverrides
void ActGlobalPoseOverrideArray_CreateGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o, int32_t count,
                                                          const int32_t* bones, int32_t local_space) {
    o.count = count;
    // Os três operator new (o port: vetores do heap do C++; o PS2 não zera as matrizes nem os pesos).
    int32_t* b = new int32_t[size_t(count > 0 ? count : 0) + 1]();
    float* m = new float[size_t(count > 0 ? count : 0) * 16 + 4]();
    float* w = new float[size_t(count > 0 ? count : 0) + 1]();
    o.bones = nf::p32_u(b);
    o.matrices = nf::p32_u(m);
    o.weights = nf::p32_u(w);
    for (int32_t i = 0; i < o.count; ++i) b[i] = bones[i];
    o.local_space = local_space;
    int32_t* set = new int32_t[size_t(count > 0 ? count : 0) + 1]();
    o.set = nf::p32_u(set);
    for (int32_t i = 0; i < o.count; ++i) set[i] = 0;
}

// [driving] 0x0010f230 ActGlobalPoseOverrideArray::DeleteGlobalPoseOverrides (o __builtin_delete de cada vetor)
void ActGlobalPoseOverrideArray_DeleteGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o) {
    if (o.count > 0) {
        delete[] nf::p32_ptr<float>(o.weights);
        delete[] nf::p32_ptr<int32_t>(o.bones);
        delete[] nf::p32_ptr<float>(o.matrices);
        delete[] nf::p32_ptr<int32_t>(o.set);
        // (o PS2 não zera os ponteiros: o port zera, para o destrutor do port não soltar duas vezes)
        o.weights = o.bones = o.matrices = o.set = 0;
    }
    o.count = 0;
}

// [driving] 0x0010f2b0 ActGlobalPoseOverrideArray::BlendMatrices (o ActSkeleton::BlendQ 0x00114c58 é o
// VU0_fastqslerp 0x002cc018 com a saída no primeiro; o VU0_SQTquattom4 0x002cc170 é igual ao do EAGL 0x002985d0)
void ActGlobalPoseOverrideArray_BlendMatrices(float t, float* a, const float* b) {
    VU0_m4toquat(cm44(a), g_blend_qa);
    VU0_m4toquat(cm44(b), g_blend_qb);
    g_blend_trans[0] = a[12];
    g_blend_trans[1] = a[13];
    g_blend_trans[2] = a[14];
    g_blend_trans[3] = 1.0f;
    fast_qslerp(g_blend_qa, g_blend_qb, g_blend_qa, t);
    SQTquattom4(g_blend_scale, g_blend_qa, g_blend_trans, a);
}

// [driving] 0x0010f378 ActGlobalPoseOverrideArray::SetGlobalPoseOverride
// Quirk mantido: sem `local_space`, a troca das linhas 0 e 2 vale para a matriz `index`, mas a negação e a troca das
// linhas 1 e 2 que seguem leem a partir do começo do vetor (o índice 0 nega a linha 2 da primeira matriz; o 1, a linha 0
// da segunda).
void ActGlobalPoseOverrideArray_SetGlobalPoseOverride(ActGlobalPoseOverrideArrayData& o, int32_t index,
                                                      const float* m, float weight) {
    float* mats = nf::p32_ptr<float>(o.matrices);
    float* dst = mats + 16 * index;
    std::memcpy(dst, m, 64);
    nf::p32_ptr<int32_t>(o.set)[index] = 1;
    if (o.local_space == 0) {
        float r0[4];
        std::memcpy(r0, dst, 16);
        std::memcpy(dst, dst + 8, 16);
        std::memcpy(dst + 8, r0, 16);
        auto neg4 = [](float* r) {
            for (int i = 0; i < 4; ++i) r[i] = ps2::ee::neg(r[i]);
        };
        auto swap_rows = [](float* x, float* y) {
            float t[4];
            std::memcpy(t, x, 16);
            std::memcpy(x, y, 16);
            std::memcpy(y, t, 16);
        };
        if (index == 0) {
            neg4(mats + 8);                  // +0x20
            swap_rows(mats + 4, mats + 8);   // +0x10 e +0x20
        }
        if (index == 1) {
            neg4(mats + 16);                 // +0x40
            swap_rows(mats + 20, mats + 24); // +0x50 e +0x60
        }
    }
    nf::p32_ptr<float>(o.weights)[index] = weight;
}

// [driving] 0x0010f620 ActGlobalPoseOverrideArray::SetGlobalPoseOverrides
void ActGlobalPoseOverrideArray_SetGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o, const float* m,
                                                       const float* weights) {
    for (int32_t i = 0; i < o.count; ++i) ActGlobalPoseOverrideArray_SetGlobalPoseOverride(o, i, m + 16 * i, weights[i]);
}

// [driving] 0x0010f698 ActGlobalPoseOverrideArray::DoGlobalOverrides
void ActGlobalPoseOverrideArray_DoGlobalOverrides(ActGlobalPoseOverrideArrayData& o, int32_t mirrored, float* local) {
    const int32_t* bones = nf::p32_ptr<const int32_t>(o.bones);
    float* mats = nf::p32_ptr<float>(o.matrices);
    const float* w = nf::p32_ptr<const float>(o.weights);
    const int32_t* set = nf::p32_ptr<const int32_t>(o.set);
    float* glob = nf::p32_ptr<float>(o.globals);
    if (o.local_space == 1) {
        for (int32_t i = 0; i < o.count; ++i) {
            if (set[i] == 0) continue;
            float* l = local + 16 * bones[i];
            float* m = mats + 16 * i;
            std::memcpy(m + 12, l + 12, 16);
            if (lt(0.0f, w[i]) && lt(w[i], 1.0f)) {
                ActGlobalPoseOverrideArray_BlendMatrices(w[i], l, m);
                continue;
            }
            if (ps2::ee::le(1.0f, w[i])) std::memcpy(l, m, 64);
        }
        return;
    }
    for (int32_t i = 0; i < o.count; ++i) {
        if (set[i] == 0) continue;
        float* l = local + 16 * bones[i];
        float* g = glob + 16 * bones[i];
        float* m = mats + 16 * i;
        alignas(16) float t[16];
        std::memcpy(t, l, 64);
        float row3[4];
        std::memcpy(row3, l + 12, 16);
        OrthoInverse(m44(t));
        VU0_MATRIX4_mult(cm44(t), cm44(g), m44(t));
        OrthoInverse(m44(t));
        if (lt(w[i], 1.0f)) {
            if (mirrored) {
                negate_row0(g);
                negate_row0(m);
            }
            ActGlobalPoseOverrideArray_BlendMatrices(w[i], g, m);
            if (mirrored) {
                negate_row0(g);
                negate_row0(m);
            }
        } else {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) g[4 * r + c] = m[4 * r + c];
        }
        VU0_MATRIX4_mult(cm44(g), cm44(t), m44(l));
        std::memcpy(l + 12, row3, 16);
    }
}

}  // namespace drv::anim
