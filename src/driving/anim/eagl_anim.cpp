// A libeaglanima da EA portada do DRIVING.ELF (eagl_anim.h; docs/driving.md seção 20). Cada função segue o código do
// PS2 instrução a instrução: a ordem das operações de ponto flutuante (a FPU do EE e a VU0 em modo macro, pelas regras
// de platform/ps2_float.h), os laços com as mesmas contagens e os mesmos ponteiros, os casos de borda dos dados.

#include "driving/anim/eagl_anim.h"

#include <cstring>

#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv::anim {

namespace {

AnimStats g_stats;

inline const uint8_t* bytes(uint32_t a) { return nf::p32_ptr<const uint8_t>(a); }
inline uint8_t u8(uint32_t a) { return *bytes(a); }
inline uint16_t u16(uint32_t a) {
    uint16_t v;
    std::memcpy(&v, bytes(a), 2);
    return v;
}
inline int32_t s32(uint32_t a) {
    int32_t v;
    std::memcpy(&v, bytes(a), 4);
    return v;
}
inline uint32_t w32(uint32_t a) {
    uint32_t v;
    std::memcpy(&v, bytes(a), 4);
    return v;
}
inline float f32(uint32_t a) {
    float v;
    std::memcpy(&v, bytes(a), 4);
    return v;
}

// A FPU do EE com floats (nomes próprios: add/sub/mul do ps2::vu, usados com `using namespace` nas funções da VU0, não
// podem ficar escondidos por estes no escopo do namespace).
inline float fadd(float a, float b) { return ps2::ee::add(a, b); }
inline float fsub(float a, float b) { return ps2::ee::sub(a, b); }
inline float fmul(float a, float b) { return ps2::ee::mul(a, b); }
inline float fdiv(float a, float b) { return ps2::ee::div(a, b); }
inline float fcvt_s(int32_t i) { return ps2::ee::cvt_s(i); }
inline int32_t fcvt_w(float a) { return ps2::ee::cvt_w(a); }

// O registrador vf0 da VU0 (0, 0, 0, 1).
constexpr uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};

// O MemoryPoolManager não é portado: os objetos são C++ (cabeçalho). O "global" 0x003441c0 (a soma dos deltas para
// trás permitida) vale 1 no .data e nada o escreve: os FnDelta* sempre andam para trás subtraindo deltas.
constexpr bool kReverseDeltas = true;

// Escreve x, y, z do quadword alinhado que contém o float `index` da pose (o SQC2 ignora os 4 bits de baixo do
// endereço; a pose do jogo é alinhada a 16). Um índice fora da pose é guardado.
bool pose_quad(float* pose, int pose_floats, int index, float*& q) {
    const int base = index & ~3;
    if (index < 0 || base + 4 > pose_floats) {
        ++g_stats.guards;
        return false;
    }
    if (index & 3) ++g_stats.guards;  // o PS2 escreveria no quadword de baixo (como aqui); não acontece nos dados
    q = pose + base;
    return true;
}
bool pose_span(int pose_floats, int index, int n) {
    if (index < 0 || index + n > pose_floats) {
        ++g_stats.guards;
        return false;
    }
    return true;
}

}  // namespace

AnimStats& anim_stats() { return g_stats; }
void reset_anim_stats() { g_stats = AnimStats{}; }

// ---- o DeltaCompressedData ----------------------------------------------------------------------------------------

// [driving] 0x00276820 EAGLAnim::DeltaCompressedData::DecompressValues
void DecompressValues(uint32_t data, int first, int nchan, int from, int to, const float* cache, float* out) {
    if (to == from) return;
    const uint32_t entries = data + 4 + uint32_t(first) * 12u;  // {deslocamento, escala, base} de cada canal
    if (to < from || to == 0 || from == -1) {
        for (int k = 0; k < nchan; ++k) out[k] = f32(entries + 12u * uint32_t(k) + 8);
        from = 0;
    } else if (out != cache) {
        std::memcpy(out, cache, size_t(uint32_t(nchan) * 4u));
    }
    if (to == 0) return;
    const int total = u16(data);
    const int bits = u8(data + 2);
    const uint32_t deltas = data + 4 + uint32_t(total) * 12u;
    // out += b * escala + deslocamento (mul.s, add.s, add.s).
    auto step = [](float& o, int b, uint32_t e) { o = fadd(o, fadd(fmul(fcvt_s(b), f32(e + 4)), f32(e))); };
    if (bits == 16) {
        // O ponteiro anda só nchan valores por quadro (o original; o jogo só pede um quadro com nchan menor que o total).
        uint32_t p = deltas + 2u * uint32_t(from * total) + 2u * uint32_t(first);
        for (int f = from; f < to; ++f) {
            uint32_t e = entries;
            for (int k = 0; k < nchan; ++k, e += 12, p += 2) step(out[k], u16(p), e);
        }
    } else if (bits == 8) {
        uint32_t p = uint32_t(from * total) + deltas + uint32_t(first);
        for (int f = from; f < to; ++f) {
            uint32_t e = entries;
            for (int k = 0; k < nchan; ++k, e += 12, ++p) step(out[k], u8(p), e);
        }
    } else if (bits == 4) {
        // Dois canais por byte, o de número par no meio byte de baixo; o começo soma `first` em bytes (o original).
        uint32_t p = uint32_t(from * ((total + 1) >> 1)) + deltas + uint32_t(first);
        const int last = nchan - 1;
        if (((first ^ 1) & 1) != 0) {  // first par
            for (int f = from; f < to; ++f) {
                uint32_t e = entries;
                int t3 = 0;
                if (last > 0) {
                    int o = 0;
                    do {
                        const int b = u8(p++);
                        t3 += 2;
                        step(out[o++], b & 0xf, e);
                        e += 12;
                        step(out[o++], b >> 4, e);
                        e += 12;
                    } while (t3 < last);
                }
                if (nchan & 1) step(out[t3], u8(p++) & 0xf, e);
            }
        } else {  // first ímpar: o primeiro canal está no meio byte de cima
            for (int f = from; f < to; ++f) {
                uint32_t e = entries;
                int t3 = 0;
                if (last > 0) {
                    step(out[0], u8(p++) >> 4, e);
                    e += 12;
                    t3 = 1;
                    if (1 < last) {
                        int o = 1;
                        do {
                            const int b = u8(p++);
                            t3 += 2;
                            step(out[o++], b & 0xf, e);
                            e += 12;
                            step(out[o++], b >> 4, e);
                            e += 12;
                        } while (t3 < last);
                    }
                }
                if (last & 1) step(out[t3], u8(p++) & 0xf, e);
            }
        }
    }
}

// [driving] 0x00276c70 EAGLAnim::DeltaCompressedData::DecompressValuesIndexed
void DecompressValuesIndexed(float t, uint32_t data, int first, int n, int from, int to, const float* cache, float* out,
                             int stride, const uint16_t* index, int out_floats) {
    if (to == from) return;
    const uint32_t entries = data + 4 + uint32_t(first) * 12u;
    if (stride == 0) {  // o DIV por zero (break 7 no PS2)
        ++g_stats.guards;
        return;
    }
    const int groups = n / stride;
    auto at = [&](int g, int k) -> float* {
        const int i = int(index[g]) + k;
        if (i < 0 || i >= out_floats) {
            ++g_stats.guards;
            return nullptr;
        }
        return out + i;
    };
    if (to == 0 || from == -1 || to < from) {
        uint32_t e = entries;
        for (int g = 0; g < groups; ++g)
            for (int k = 0; k < stride; ++k, e += 12)
                if (float* o = at(g, k)) *o = f32(e + 8);
        from = 0;
    } else {
        int c = 0;
        for (int g = 0; g < groups; ++g)
            for (int k = 0; k < stride; ++k, ++c)
                if (float* o = at(g, k)) *o = cache[c];
    }
    if (to == 0) return;
    const int total = u16(data);
    const int bits = u8(data + 2);
    const uint32_t deltas = data + 4 + uint32_t(total) * 12u;
    // out += (b * escala + deslocamento) * t.
    auto step = [&](float* o, int b, uint32_t e) {
        const float d = fmul(fadd(fmul(fcvt_s(b), f32(e + 4)), f32(e)), t);
        if (o) *o = fadd(*o, d);
    };
    if (bits == 16) {
        uint32_t p = deltas + 2u * uint32_t(from * total);  // o primeiro canal não entra no ponteiro (o original)
        for (int f = from; f < to; ++f) {
            uint32_t e = entries;
            for (int g = 0; g < groups; ++g)
                for (int k = 0; k < stride; ++k, e += 12, p += 2) step(at(g, k), u16(p), e);
        }
    } else if (bits == 8) {
        uint32_t p = uint32_t(from * total) + deltas;
        for (int f = from; f < to; ++f) {
            uint32_t e = entries;
            for (int g = 0; g < groups; ++g)
                for (int k = 0; k < stride; ++k, e += 12, ++p) step(at(g, k), u8(p), e);
        }
    } else if (bits == 4) {
        uint32_t p = uint32_t(from * ((total + 1) >> 1)) + deltas;
        bool low = (uint32_t(first) & 0xfffffffeu) == 0;  // começa no meio byte de baixo só com first 0 ou 1
        for (int f = from; f < to; ++f) {
            uint32_t e = entries;
            for (int g = 0; g < groups; ++g)
                for (int k = 0; k < stride; ++k, e += 12) {
                    int b = u8(p);
                    if (low) {
                        b &= 0xf;
                    } else {
                        b >>= 4;
                        ++p;
                    }
                    low = !low;
                    step(at(g, k), b, e);
                }
            if (!low) {  // o quadro acabou no meio de um byte: o próximo começa no byte seguinte
                ++p;
                low = true;
            }
        }
    }
}

// ---- a VU0 ---------------------------------------------------------------------------------------------------------

// [driving] 0x00298478 o slerp rápido (= VU0_fastqslerp 0x002cc018)
void fast_qslerp(const float* q0, const float* q1, float* out, float t) {
    using namespace ps2::vu;
    Reg v1, v2, v3 = {}, v4, v5, v6 = {}, v7, v8, acc = {};
    load(v1, q1);
    load(v2, q0);
    sub(v4, v1, v2, XYZW);
    v3[0] = ps2::fbits(t);
    add(v5, v1, v2, XYZW);
    v6[0] = 0x3f800000;
    subbc(v3, kVf0, v3, BX, W);  // vf3.w = 1 - t
    mulbc(v1, v1, v3, BX, XYZW);
    mulbc(v2, v2, v3, BW, XYZW);
    // |d|^2 e |s|^2: x*x pelo ACC e os quadrados y, z, w (já arredondados pelo VMUL) vezes 1.
    mul(v7, v4, v4, XYZW);
    mulabc(acc, v4, v4, BX, X);
    maddabc(acc, v6, v7, BY, X);
    maddabc(acc, v6, v7, BZ, X);
    maddbc(v7, acc, v6, v7, BW, X);
    mul(v8, v5, v5, XYZW);
    mulabc(acc, v5, v5, BX, X);
    maddabc(acc, v6, v8, BY, X);
    maddabc(acc, v6, v8, BZ, X);
    maddbc(v8, acc, v6, v8, BW, X);
    subbc(v7, v7, v8, BX, X);
    // QMFC2 + DSLL32 + BLEZ: soma se os bits de x, como inteiro, são <= 0 (as pontas perto), senão subtrai.
    if (int32_t(v7[0]) <= 0)
        add(v1, v1, v2, XYZW);
    else
        sub(v1, v1, v2, XYZW);
    mul(v2, v1, v1, XYZW);
    mulabc(acc, v1, v1, BX, X);
    maddabc(acc, v6, v2, BY, X);
    maddabc(acc, v6, v2, BZ, X);
    maddbc(v2, acc, v6, v2, BW, X);
    const uint32_t q = ps2::rsqrt(kVf0[3], v2[0]);
    muls(v1, v1, q, XYZW);
    store(out, v1);
}

// [driving] 0x00297c60 EAGL::VU0_MATRIX4_mult
void MATRIX4_mult(const float* a, const float* b, float* out) {
    using namespace ps2::vu;
    Reg A[4], B[4], R[4], acc = {};
    for (int k = 0; k < 4; ++k) {
        load(A[k], a + 4 * k);
        load(B[k], b + 4 * k);
    }
    for (int k = 0; k < 4; ++k) {
        mulabc(acc, B[0], A[k], BX, XYZW);
        maddabc(acc, B[1], A[k], BY, XYZW);
        maddabc(acc, B[2], A[k], BZ, XYZW);
        maddbc(R[k], acc, B[3], A[k], BW, XYZW);
    }
    for (int k = 0; k < 4; ++k) store(out + 4 * k, R[k]);
}

// [driving] 0x002985d0 EAGL::VU0_SQTquattom4
void SQTquattom4(const float* scale, const float* quat, const float* trans, float* out) {
    using namespace ps2::vu;
    Reg v1, v11, v12, v4, v5, v6, v7, v8, v9, v10;
    load(v1, quat);
    load(v11, scale);
    load(v12, trans);
    mulbc(v4, v1, v1, BX, XYZW);
    mulbc(v5, v1, v1, BY, XYZW);
    mulbc(v6, v1, v1, BZ, XYZW);
    mulbc(v7, v1, v1, BW, XYZW);
    add(v4, v4, v4, XYZW);
    add(v5, v5, v5, XYZW);
    add(v6, v6, v6, XYZW);
    add(v7, v7, v7, XYZW);
    sub(v8, kVf0, kVf0, XYZW);
    sub(v9, kVf0, kVf0, XYZW);
    sub(v10, kVf0, kVf0, XYZW);
    addbc(v8, kVf0, kVf0, BW, X);
    addbc(v9, kVf0, v5, BX, X);
    addbc(v10, kVf0, v6, BX, X);
    subbc(v8, v8, v5, BY, X);
    subbc(v9, v9, v6, BW, X);
    addbc(v10, v10, v5, BW, X);
    subbc(v8, v8, v6, BZ, X);
    addbc(v9, kVf0, kVf0, BW, Y);
    addbc(v10, kVf0, v5, BZ, Y);
    addbc(v8, kVf0, v5, BX, Y);
    subbc(v9, v9, v4, BX, Y);
    subbc(v10, v10, v7, BX, Y);
    addbc(v8, v8, v6, BW, Y);
    subbc(v9, v9, v6, BZ, Y);
    addbc(v10, kVf0, kVf0, BW, Z);
    addbc(v8, kVf0, v6, BX, Z);
    addbc(v9, kVf0, v6, BY, Z);
    subbc(v10, v10, v4, BX, Z);
    subbc(v8, v8, v5, BW, Z);
    addbc(v9, v9, v7, BX, Z);
    subbc(v10, v10, v5, BY, Z);
    sub(v8, v8, v8, W);
    sub(v9, v9, v9, W);
    sub(v10, v10, v10, W);
    mulbc(v8, v8, v11, BX, XYZ);
    mulbc(v9, v9, v11, BY, XYZ);
    mulbc(v10, v10, v11, BZ, XYZ);
    store(out + 12, v12);
    store(out + 0, v8);
    store(out + 4, v9);
    store(out + 8, v10);
}

// [driving] 0x002627e8 EAGLAnim::Skeleton::PoseSQTToGlobal (o caminho sem máscara: a conta da VU0 em linha)
void Skeleton_PoseSQTToGlobal(uint32_t skel, const float* pose, float* global) {
    using namespace ps2::vu;
    const int n = s32(skel + 8);
    for (int i = 0; i < n; ++i) {
        const float* p = pose + kPoseFloats * i;
        float* out = global + kMatrixFloats * i;
        Reg v1, v11, v12, v4, v5, v6, v7, v8, v9, v10;
        load(v1, p + 4);
        load(v11, p + 0);
        load(v12, p + 8);
        mulbc(v4, v1, v1, BX, XYZW);
        mulbc(v5, v1, v1, BY, XYZW);
        mulbc(v6, v1, v1, BZ, XYZW);
        mulbc(v7, v1, v1, BW, XYZW);
        add(v4, v4, v4, XYZW);
        add(v5, v5, v5, XYZW);
        add(v6, v6, v6, XYZW);
        add(v7, v7, v7, XYZW);
        sub(v8, kVf0, kVf0, XYZW);
        sub(v9, kVf0, kVf0, XYZW);
        sub(v10, kVf0, kVf0, XYZW);
        addbc(v8, kVf0, kVf0, BW, X);
        addbc(v9, kVf0, v5, BX, X);
        addbc(v10, kVf0, v6, BX, X);
        subbc(v8, v8, v5, BY, X);
        subbc(v9, v9, v6, BW, X);
        addbc(v10, v10, v5, BW, X);
        subbc(v8, v8, v6, BZ, X);
        addbc(v9, kVf0, kVf0, BW, Y);
        addbc(v10, kVf0, v5, BZ, Y);
        addbc(v8, kVf0, v5, BX, Y);
        subbc(v9, v9, v4, BX, Y);
        subbc(v10, v10, v7, BX, Y);
        addbc(v8, v8, v6, BW, Y);
        subbc(v9, v9, v6, BZ, Y);
        addbc(v10, kVf0, kVf0, BW, Z);
        addbc(v8, kVf0, v6, BX, Z);
        addbc(v9, kVf0, v6, BY, Z);
        subbc(v10, v10, v4, BX, Z);
        subbc(v8, v8, v5, BW, Z);
        addbc(v9, v9, v7, BX, Z);
        subbc(v10, v10, v5, BY, Z);
        sub(v8, v8, v8, W);
        sub(v9, v9, v9, W);
        sub(v10, v10, v10, W);
        mulbc(v8, v8, v11, BX, XYZ);
        mulbc(v9, v9, v11, BY, XYZ);
        mulbc(v10, v10, v11, BZ, XYZ);
        mulbc(v8, v8, v11, BW, X);
        mulbc(v9, v9, v11, BW, X);
        mulbc(v10, v10, v11, BW, X);
        const int parent = s32(skel + 0x10 + 0x70u * uint32_t(i) + 0xc);
        if (parent < 0) {
            store(out + 0, v8);
            store(out + 4, v9);
            store(out + 8, v10);
            store(out + 12, v12);
            continue;
        }
        Reg r1, r2, r3, r4, acc, o5, o6, o7, o11;
        const float* pm = global + kMatrixFloats * parent;
        load(r1, pm + 0);
        load(r2, pm + 4);
        load(r3, pm + 8);
        load(r4, pm + 12);
        auto row = [&](uint32_t* d, const uint32_t* v) {
            mulabc(acc, r1, v, BX, XYZW);
            maddabc(acc, r2, v, BY, XYZW);
            maddabc(acc, r3, v, BZ, XYZW);
            maddbc(d, acc, r4, v, BW, XYZW);
        };
        row(o5, v8);
        row(o6, v9);
        row(o7, v10);
        row(o11, v12);
        store(out + 0, o5);
        store(out + 4, o6);
        store(out + 8, o7);
        store(out + 12, o11);
    }
}

// ---- o Skeleton ----------------------------------------------------------------------------------------------------

int Skeleton_NumBones(uint32_t skel) { return skel ? s32(skel + 8) : 0; }
int Skeleton_Parent(uint32_t skel, int bone) { return s32(skel + 0x10 + 0x70u * uint32_t(bone) + 0xc); }

// [driving] 0x00262aa8 EAGLAnim::Skeleton::GetStillPose
void Skeleton_GetStillPose(uint32_t skel, float* pose) {
    const int count = Skeleton_NumBones(skel);
    const uint32_t lengths = w32(skel + 0xc);
    uint32_t bone = skel + 0x10;
    for (int i = 0; i < count; ++i, bone += 0x70, pose += kPoseFloats) {
        pose[0] = f32(bone + 0);
        pose[1] = f32(bone + 4);
        pose[2] = f32(bone + 8);
        pose[3] = lengths ? f32(lengths + 4u * uint32_t(i)) : 1.0f;
        for (int k = 0; k < 4; ++k) pose[4 + k] = f32(bone + 0x10 + 4u * uint32_t(k));
        for (int k = 0; k < 3; ++k) pose[8 + k] = f32(bone + 0x20 + 4u * uint32_t(k));
        pose[11] = 1.0f;
    }
}

// [driving] 0x002623c0 EAGLAnim::Skeleton::PoseSQTToLocal
void Skeleton_PoseSQTToLocal(uint32_t skel, const float* pose, float* local) {
    const int count = Skeleton_NumBones(skel);
    for (int i = 0; i < count; ++i, pose += kPoseFloats, local += kMatrixFloats) {
        SQTquattom4(pose + 0, pose + 4, pose + 8, local);
        // A escala do comprimento na primeira coluna das três linhas da rotação (mul.s).
        local[0] = fmul(local[0], pose[3]);
        local[4] = fmul(local[4], pose[3]);
        local[8] = fmul(local[8], pose[3]);
    }
}

// [driving] 0x00262508 EAGLAnim::Skeleton::PoseLocalToGlobal
void Skeleton_PoseLocalToGlobal(uint32_t skel, const float* local, float* global) {
    const int count = Skeleton_NumBones(skel);
    uint32_t bone = skel + 0x10;
    for (int i = 0; i < count; ++i, bone += 0x70) {
        const int parent = s32(bone + 0xc);
        if (parent >= 0)
            MATRIX4_mult(local + kMatrixFloats * i, global + kMatrixFloats * parent, global + kMatrixFloats * i);
        else if (global != local)
            std::memcpy(global + kMatrixFloats * i, local + kMatrixFloats * i, 64);
    }
}

// [driving] 0x002634f8 EAGLAnim::Skeleton::PoseGlobalToSkin
void Skeleton_PoseGlobalToSkin(uint32_t skel, const float* global, float* skin) {
    // O laço sem máscara é um do ... while: com zero ossos o PS2 faria um (lendo o osso 0); aqui não roda (guarda).
    const int count = Skeleton_NumBones(skel);
    if (count <= 0) {
        ++g_stats.guards;
        return;
    }
    uint32_t bone = skel + 0x10;
    for (int i = 0; i < count; ++i, bone += 0x70) {
        float inv[16];
        std::memcpy(inv, bytes(bone + 0x30), 64);
        MATRIX4_mult(inv, global + kMatrixFloats * i, skin + kMatrixFloats * i);
    }
}

// ---- os canais -----------------------------------------------------------------------------------------------------

namespace {

// O tipo 1: os eventos. O Eval chamaria o EvalEvent com a pose como tabela de tratadores (ver o cabeçalho).
class FnRawEventChannel final : public FnAnim {
public:
    FnRawEventChannel() : FnAnim(kFnRawEvent) {}
    // [driving] 0x00264bf8 EAGLAnim::FnRawEventChannel::SetAnimMemoryMap: os dados, o cursor e o último tempo (+0xc,
    // +0x10, +0x14).
    void SetAnimMemoryMap(uint32_t data) override {
        data_ = data;
        last_ = 0.0f;
        cursor_ = 0;
    }
    // [driving] 0x00264c40 EAGLAnim::FnRawEventChannel::Eval
    void Eval(float, float, float*, int) override { ++g_stats.events; }
    bool GetLength(float&) const override { return false; }  // o padrão 0x0026dfd0: não escreve, devolve 0
    // [driving] 0x00264c08 EAGLAnim::FnRawEventChannel::EvalEvent
    bool EvalEvent(float prev, float time, uint32_t handlers, uint32_t user) override {
        RawEventChannel_Eval(data_, cursor_, last_, prev, time, handlers, user);
        return true;
    }

private:
    int32_t cursor_ = 0;  // +0x10
    float last_ = 0.0f;   // +0x14
};

// O tipo 11.
class FnDeltaQuatChan final : public FnAnim {
public:
    FnDeltaQuatChan() : FnAnim(kFnDeltaQuat) {}
    // [driving] 0x00276288 EAGLAnim::FnDeltaQuatChan::SetAnimMemoryMap
    void SetAnimMemoryMap(uint32_t data) override {
        const size_t n = u16(w32(data + 4));
        if (values_.size() < n || values_.empty()) values_.resize(n ? n : 1);
        data_ = data;
        cached_ = -1;
    }
    // [driving] 0x00274d30 EAGLAnim::FnDeltaQuatChan::Eval (o corpo do EvalSQT 0x00274e88 com o tempo de f13)
    void Eval(float, float time, float* pose, int pose_floats) override {
        const int it = fcvt_w(time);
        const int frames = u16(data_ + 8);
        const uint32_t dcd = w32(data_ + 4);
        int key;
        if (it < frames)
            key = it > -1 ? it : 0;
        else
            key = frames - 1;
        DecompressValues(dcd, 0, u16(dcd), cached_, key, values_.data(), values_.data());
        cached_ = key;
        const float fit = fcvt_s(it);
        const int n = u16(dcd);
        const uint16_t* index = nf::p32_ptr<const uint16_t>(data_ + 0xa);
        if (!ps2::ee::eq(time, fit) && it + 1 < frames) {
            DecompressValuesIndexed(fsub(time, fit), dcd, 0, n, it, it + 1, values_.data(), pose, 4, index, pose_floats);
            return;
        }
        for (int c = 0, g = 0; c < n; c += 4, ++g)
            if (pose_span(pose_floats, index[g], 4)) std::memcpy(pose + index[g], values_.data() + c, 16);
    }
    // [driving] 0x002761e0 EAGLAnim::FnDeltaQuatChan::GetLength
    bool GetLength(float& out) const override {
        out = fcvt_s(u16(data_ + 8));
        return true;
    }

private:
    int cached_ = -1;            // +0x10
    std::vector<float> values_;  // +0x14
};

// A busca da chave dos canais com quadros-chave (o FnKeyQuatChan e os FnDelta*): o índice da primeira chave depois
// do tempo inteiro `it`, a partir da chave em cache (para a frente ou para trás); 0 antes da primeira.
int find_key(uint32_t keys, int num_keys, int it, int cached) {
    int a3 = cached > 0 ? cached - 1 : 0;
    const int last = num_keys - 2;
    if (it < u16(keys + 2u * uint32_t(a3))) {
        if (a3 <= 0) return a3 + 1;
        --a3;
        while (a3 > 0 && it < u16(keys + 2u * uint32_t(a3))) --a3;
        return a3 + 1;
    }
    if (!(a3 < last)) return a3 + 1;
    if (it < u16(keys + 2u * uint32_t(a3 + 1))) return a3 + 1;
    ++a3;
    while (a3 < last && !(it < u16(keys + 2u * uint32_t(a3 + 1)))) ++a3;
    return a3 + 1;
}

// O tipo 13.
class FnKeyQuatChan final : public FnAnim {
public:
    FnKeyQuatChan() : FnAnim(kFnKeyQuat) {}
    // [driving] 0x002765c0 EAGLAnim::FnKeyQuatChan::SetAnimMemoryMap
    void SetAnimMemoryMap(uint32_t data) override {
        const size_t n = u16(w32(data + 4));
        if (values_.size() < n || values_.empty()) values_.resize(n ? n : 1);
        data_ = data;
        cached_ = -1;
    }
    // [driving] 0x002767f0 EAGLAnim::FnKeyQuatChan::Eval -> [driving] 0x00275228 EAGLAnim::FnKeyQuatChan::EvalSQT
    void Eval(float, float time, float* pose, int pose_floats) override {
        const uint32_t dcd = w32(data_ + 4);
        const uint32_t keys = w32(data_ + 8);
        const int num_keys = u16(data_ + 0xc);
        const uint16_t* index = nf::p32_ptr<const uint16_t>(data_ + 0xe);
        const int n = u16(dcd);
        const int it = fcvt_w(time);
        int key;
        if (ps2::ee::lt(time, fcvt_s(u16(keys))))
            key = 0;
        else
            key = find_key(keys, num_keys, it, cached_);
        DecompressValues(dcd, 0, n, cached_, key, values_.data(), values_.data());
        cached_ = key;
        const int k0 = key == 0 ? 0 : u16(keys + 2u * uint32_t(key - 1));
        const float f0 = fcvt_s(k0);
        bool copy = ps2::ee::eq(f0, time);
        if (!copy && key == num_keys - 1 && ps2::ee::lt(fcvt_s(u16(keys + 2u * uint32_t(num_keys - 2))), time))
            copy = true;
        if (!copy && key == 0 && ps2::ee::lt(time, 0.0f)) copy = true;
        if (copy) {
            for (int c = 0, g = 0; c < n; c += 4, ++g)
                if (pose_span(pose_floats, index[g], 4)) std::memcpy(pose + index[g], values_.data() + c, 16);
            return;
        }
        const float t = fdiv(fsub(time, f0), fcvt_s(int(u16(keys + 2u * uint32_t(key))) - k0));
        for (int c = 0, g = 0; c < n; c += 4, ++g) {
            alignas(16) float next[4], cur[4];
            DecompressValues(dcd, c, 4, key, key + 1, values_.data() + c, next);
            std::memcpy(cur, values_.data() + c, 16);
            float* q;
            if (pose_quad(pose, pose_floats, index[g], q)) fast_qslerp(cur, next, q, t);
        }
    }
    // [driving] 0x00276658 EAGLAnim::FnKeyDeltaChan::GetLength
    bool GetLength(float& out) const override {
        const uint32_t keys = w32(data_ + 8);
        out = fcvt_s(u16(keys + 2u * uint32_t(u16(data_ + 0xc)) - 4) + 1);
        return true;
    }

private:
    int cached_ = -1;
    std::vector<float> values_;
};

// O que o FnDeltaF3 e o FnDeltaF1 têm em comum: os dados (+4 a lista de índices, +8 as chaves (ou nulo), +0xc o
// número de chaves (ou de quadros), +0xe o número de canais, +0x10 o deslocamento dos blocos, +0x11 o número de
// constantes, +0x14 os cabeçalhos dos canais, depois os blocos e as constantes) e o objeto (+0x10 o quadro em cache,
// +0x18 o buffer 1 (o quadro em cache), +0x1c o quadro do buffer 2, +0x24 o buffer 2 (o quadro seguinte), +0x28 um
// pedido de recomeço, +0x2c a tabela de dequantização).
struct DeltaLayout {
    uint32_t data;
    int num_keys, n, shift, nconst, hdr_size, comps;  // comps: 3 (F3) ou 1 (F1)
    uint32_t keys, index, headers, blocks;
    int block_bytes;
};
DeltaLayout delta_layout(uint32_t data, int comps) {
    DeltaLayout l{};
    l.data = data;
    l.comps = comps;
    l.index = w32(data + 4);
    l.keys = w32(data + 8);
    l.num_keys = u16(data + 0xc);
    l.n = u16(data + 0xe);
    l.shift = u8(data + 0x10);
    l.nconst = u8(data + 0x11);
    l.hdr_size = comps == 3 ? 0x24 : 0xc;
    l.headers = data + 0x14;
    l.blocks = l.headers + uint32_t(l.n * l.hdr_size);
    // Os bytes de um bloco: os valores inteiros (u16) e os deltas (u8) de 2^n - 1 quadros, arredondados a par.
    const int full = 2 * comps * l.n, delta = comps * l.n;
    l.block_bytes = ((full + ((1 << l.shift) - 1) * delta + 1) >> 1) * 2;
    return l;
}
// O endereço das constantes (a lista de índices; os valores vêm depois, alinhados a 4).
uint32_t delta_constants(const DeltaLayout& l) {
    const int full = 2 * l.comps * l.n, delta = l.comps * l.n;
    const int mask = int(0x7fffffffu >> (31 - l.shift));
    const int rest = l.num_keys & mask;
    uint32_t a = l.blocks + uint32_t(l.block_bytes * (l.num_keys >> l.shift));
    if (rest > 0) a = ((a + uint32_t(full) + uint32_t((rest - 1) * delta) + 1) >> 1) << 1;
    if (l.n == 0) a = ((a + 1) >> 1) << 1;
    return a;
}

class FnDeltaBase : public FnAnim {
public:
    FnDeltaBase(uint16_t type, int comps) : FnAnim(type), comps_(comps) {}
    // [driving] 0x002700d0 EAGLAnim::FnDeltaF3::SetAnimMemoryMap (e 0x002717d8 o do FnDeltaF1)
    void SetAnimMemoryMap(uint32_t data) override {
        data_ = data;
        key2_ = -1;
        cached_ = -1;
    }
    // [driving] 0x002700e8 EAGLAnim::FnDeltaF3::GetLength (e 0x002717f0 o do FnDeltaF1)
    bool GetLength(float& out) const override {
        const uint32_t keys = w32(data_ + 8);
        uint32_t frames;
        if (keys)
            frames = (u16(keys + 2u * uint32_t(u16(data_ + 0xc)) - 4) + 1u) & 0xffffu;
        else
            frames = u16(data_ + 0xc);
        out = fcvt_s(int32_t(frames));
        return true;
    }

protected:
    // A chave pedida, o quadro dentro do bloco e o t da interpolação (a parte comum dos dois EvalSQT).
    struct Step {
        int it, key, in_block;
        bool interpolate;
        float t;
    };
    float* buf1() { return buf1_.data(); }
    float* buf2() { return buf2_.data(); }

    int comps_;
    int cached_ = -1;              // +0x10
    int key2_ = -1;                // +0x1c
    bool reset_ = false;           // +0x28 (nada no código portado o liga)
    std::vector<float> buf1_, buf2_;  // +0x18 e +0x24 (troca de lugar quando o quadro seguinte vira o atual)
    std::vector<float> table_;        // +0x2c
};

// O quadro pedido (a busca das chaves) e o t, como os dois EvalSQT fazem.
int delta_key(const DeltaLayout& l, int it, int cached) {
    if (!l.keys) {
        if (it < 0) return 0;
        if (it < l.num_keys) return it;
        return l.num_keys - 1;
    }
    if (it < u16(l.keys)) return 0;
    return find_key(l.keys, l.num_keys, it, cached);
}
// a = 0: sem interpolação (a cópia); t em `t`.
bool delta_t(const DeltaLayout& l, float time, int it, int key, float& t) {
    t = 1.0f;
    if (!l.keys) {
        const float f0 = fcvt_s(it);
        if (ps2::ee::eq(time, f0)) return false;
        t = fsub(time, f0);
        return true;
    }
    if (key == 0) {
        if (ps2::ee::eq(time, 0.0f)) return false;
        t = fdiv(time, fcvt_s(u16(l.keys)));
        return true;
    }
    const float f2 = fcvt_s(u16(l.keys + 2u * uint32_t(key - 1)));
    if (ps2::ee::eq(time, f2)) return false;
    t = fdiv(fsub(time, f2), fsub(fcvt_s(u16(l.keys + 2u * uint32_t(key))), f2));
    return true;
}

// O tipo 20.
class FnDeltaF3 final : public FnDeltaBase {
public:
    FnDeltaF3() : FnDeltaBase(kFnDeltaF3, 3) {}

    // [driving] 0x0026fc80 EAGLAnim::FnDeltaF3::InitBuffersAsRequired
    void InitBuffersAsRequired() {
        const int n = u16(data_ + 0xe);
        if (n == 0) return;
        buf1_.assign(size_t(n) * 4, 0.0f);
        buf2_.assign(size_t(n) * 4, 0.0f);
        table_.assign(size_t(n) * 12, 0.0f);
        const float c16 = ps2::ffloat(0x37800080);   // 1/65535
        const float c255 = ps2::ffloat(0x3b808081);  // 1/255
        uint32_t h = data_ + 0x14;
        for (int ch = 0; ch < n; ++ch, h += 0x24) {
            float* tb = table_.data() + 12 * ch;
            for (int k = 0; k < 3; ++k) {
                const float range = f32(h + 0xc + 4u * uint32_t(k));
                tb[k] = f32(h + 4u * uint32_t(k));
                tb[3 + k] = fmul(range, c16);
                float a = fmul(fcvt_s(u16(h + 0x18 + 2u * uint32_t(k))), c16);
                a = fadd(a, a);
                tb[6 + k] = fsub(fmul(a, range), range);
                float b = fmul(fcvt_s(u16(h + 0x1e + 2u * uint32_t(k))), c16);
                b = fadd(b, b);
                tb[9 + k] = fmul(fmul(b, range), c255);
            }
        }
    }

    void Eval(float, float time, float* pose, int pose_floats) override { EvalSQT(time, pose, pose_floats); }

    // [driving] 0x0026e4e0 EAGLAnim::FnDeltaF3::EvalSQT (sem máscara)
    void EvalSQT(float time, float* pose, int pose_floats) {
        using namespace ps2::vu;
        // Com zero canais (só constantes) os buffers ficam nulos no PS2 e o InitBuffersAsRequired roda a cada chamada,
        // sem fazer nada; os laços dos canais não rodam e as constantes são escritas.
        if (buf1_.empty()) InitBuffersAsRequired();
        if (reset_) {
            reset_ = false;
            key2_ = -1;
            cached_ = -1;
        }
        const DeltaLayout l = delta_layout(data_, 3);
        const int it = fcvt_w(time);
        const int key = delta_key(l, it, cached_);
        const int cached = cached_;
        const int mask = int(0x7fffffffu >> (31 - l.shift));
        const int in_block = key & mask;
        const int block = key >> l.shift;
        const uint32_t block_addr = l.blocks + uint32_t(block * l.block_bytes);
        const bool backward = key < cached && !kReverseDeltas;
        const float* tb = table_.data();
        int from;
        if (key == key2_) {
            std::swap(buf1_, buf2_);
            key2_ = -1;
            cached_ = key;
            from = in_block;
        } else if (cached == -1 || block != (cached >> l.shift) || in_block == 0 || backward) {
            // Do começo do bloco: os valores inteiros (u16 x escala + base).
            uint32_t p = block_addr;
            for (int ch = 0; ch < l.n; ++ch, p += 6)
                for (int k = 0; k < 3; ++k)
                    buf1_[size_t(4 * ch + k)] = fadd(fmul(fcvt_s(u16(p + 2u * uint32_t(k))), tb[12 * ch + 3 + k]), tb[12 * ch + k]);
            from = 0;
        } else {
            from = cached & mask;
        }
        const uint32_t deltas = block_addr + uint32_t(6 * l.n);
        // Um delta de um quadro (b x escala + deslocamento, por componente) na pilha (o w é o que a pilha tinha:
        // só xyz entram nas contas).
        auto delta = [&](uint32_t p, int ch, Reg& d) {
            for (int k = 0; k < 3; ++k)
                d[k] = ps2::fbits(fadd(fmul(fcvt_s(u8(p + uint32_t(k))), tb[12 * ch + 9 + k]), tb[12 * ch + 6 + k]));
        };
        if (from < in_block) {
            for (int f = from; f < in_block; ++f) {
                uint32_t p = deltas + uint32_t(f * 3 * l.n);
                for (int ch = 0; ch < l.n; ++ch, p += 3) {
                    Reg cur, d = {};
                    load(cur, buf1() + 4 * ch);
                    delta(p, ch, d);
                    add(cur, cur, d, XYZ);
                    store(buf1() + 4 * ch, cur);
                }
            }
            cached_ = key;
        } else if (in_block < from) {
            for (int f = from - 1; f >= in_block; --f) {
                uint32_t p = deltas + uint32_t(f * 3 * l.n);
                for (int ch = 0; ch < l.n; ++ch, p += 3) {
                    Reg cur, d = {};
                    load(cur, buf1() + 4 * ch);
                    delta(p, ch, d);
                    sub(cur, cur, d, XYZ);
                    store(buf1() + 4 * ch, cur);
                }
            }
            cached_ = key;
        } else {
            cached_ = key;
        }
        float t;
        const bool interp = delta_t(l, time, it, key, t) && key < l.num_keys - 1;
        const uint16_t* index = nf::p32_ptr<const uint16_t>(l.index);
        Reg tv = {};
        tv[0] = ps2::fbits(t);
        if (!interp) {
            for (int ch = 0; ch < l.n; ++ch)
                if (pose_span(pose_floats, index[ch], 3)) std::memcpy(pose + index[ch], buf1() + 4 * ch, 12);
        } else {
            const int next = key + 1;
            const int next_block = next >> l.shift;
            // out.xyz = buf1 + d * t (o w da pose fica).
            auto lerp = [&](int ch, const Reg& d) {
                float* q;
                if (!pose_quad(pose, pose_floats, index[ch], q)) return;
                Reg o, b1, m;
                load(o, q);
                load(b1, buf1() + 4 * ch);
                mulbc(m, d, tv, BX, XYZ);
                move(o, m, XYZ);
                add(o, b1, o, XYZ);
                store(q, o);
            };
            if (next == key2_) {
                for (int ch = 0; ch < l.n; ++ch) {
                    Reg b2, b1, d = {};
                    load(b2, buf2() + 4 * ch);
                    load(b1, buf1() + 4 * ch);
                    sub(d, b2, b1, XYZ);
                    lerp(ch, d);
                }
            } else if (next_block == block) {
                uint32_t p = deltas + uint32_t(in_block * 3 * l.n);
                for (int ch = 0; ch < l.n; ++ch, p += 3) {
                    Reg d = {};
                    float dv[3];
                    for (int k = 0; k < 3; ++k) {
                        dv[k] = fadd(fmul(fcvt_s(u8(p + uint32_t(k))), tb[12 * ch + 9 + k]), tb[12 * ch + 6 + k]);
                        d[k] = ps2::fbits(dv[k]);
                    }
                    for (int k = 0; k < 3; ++k) buf2_[size_t(4 * ch + k)] = fadd(buf1_[size_t(4 * ch + k)], dv[k]);
                    lerp(ch, d);
                }
                key2_ = next;
            } else {
                uint32_t p = l.blocks + uint32_t(next_block * l.block_bytes);
                for (int ch = 0; ch < l.n; ++ch, p += 6) {
                    for (int k = 0; k < 3; ++k)
                        buf2_[size_t(4 * ch + k)] =
                            fadd(fmul(fcvt_s(u16(p + 2u * uint32_t(k))), tb[12 * ch + 3 + k]), tb[12 * ch + k]);
                    Reg b2, b1, d = {};
                    load(b2, buf2() + 4 * ch);
                    load(b1, buf1() + 4 * ch);
                    sub(d, b2, b1, XYZ);
                    lerp(ch, d);
                }
                key2_ = next;
            }
        }
        // As constantes: três floats por entrada.
        if (l.nconst) {
            uint32_t ip = delta_constants(l);
            uint32_t vp = ((ip + 2u * uint32_t(l.nconst) + 3) >> 2) << 2;
            for (int i = 0; i < l.nconst; ++i, ip += 2, vp += 12) {
                const int idx = u16(ip);
                if (!pose_span(pose_floats, idx, 3)) continue;
                for (int k = 0; k < 3; ++k) pose[idx + k] = f32(vp + 4u * uint32_t(k));
            }
        }
    }
};

// O tipo 21.
class FnDeltaF1 final : public FnDeltaBase {
public:
    FnDeltaF1() : FnDeltaBase(kFnDeltaF1, 1) {}

    // [driving] 0x002713f0 EAGLAnim::FnDeltaF1::InitBuffersAsRequired
    void InitBuffersAsRequired() {
        const int n = u16(data_ + 0xe);
        if (n == 0) return;
        buf1_.assign(size_t(n), 0.0f);
        buf2_.assign(size_t(n), 0.0f);
        table_.assign(size_t(n) * 4, 0.0f);
        const float c16 = ps2::ffloat(0x37800080);
        const float c255 = ps2::ffloat(0x3b808081);
        uint32_t h = data_ + 0x14;
        for (int ch = 0; ch < n; ++ch, h += 0xc) {
            float* tb = table_.data() + 4 * ch;
            const float range = f32(h + 4);
            tb[0] = f32(h);
            tb[1] = fmul(range, c16);
            float a = fmul(fcvt_s(u16(h + 8)), c16);
            a = fadd(a, a);
            tb[2] = fsub(fmul(a, range), range);
            float b = fmul(fcvt_s(u16(h + 0xa)), c16);
            b = fadd(b, b);
            tb[3] = fmul(fmul(b, range), c255);
        }
    }

    void Eval(float, float time, float* pose, int pose_floats) override { EvalSQT(time, pose, pose_floats); }

    // [driving] 0x002701b8 EAGLAnim::FnDeltaF1::EvalSQT (sem máscara)
    void EvalSQT(float time, float* pose, int pose_floats) {
        if (buf1_.empty()) InitBuffersAsRequired();  // com zero canais, a cada chamada (ver o FnDeltaF3)
        if (reset_) {
            reset_ = false;
            key2_ = -1;
            cached_ = -1;
        }
        const DeltaLayout l = delta_layout(data_, 1);
        const int it = fcvt_w(time);
        const int key = delta_key(l, it, cached_);
        const int cached = cached_;
        const int mask = int(0x7fffffffu >> (31 - l.shift));
        const int in_block = key & mask;
        const int block = key >> l.shift;
        const uint32_t block_addr = l.blocks + uint32_t(block * l.block_bytes);
        const bool backward = key < cached && !kReverseDeltas;
        const float* tb = table_.data();
        int from;
        if (key == key2_) {
            std::swap(buf1_, buf2_);
            key2_ = -1;
            cached_ = key;
            from = in_block;
        } else if (cached == -1 || block != (cached >> l.shift) || in_block == 0 || backward) {
            uint32_t p = block_addr;
            for (int ch = 0; ch < l.n; ++ch, p += 2) buf1_[size_t(ch)] = fadd(fmul(fcvt_s(u16(p)), tb[4 * ch + 1]), tb[4 * ch]);
            from = 0;
        } else {
            from = cached & mask;
        }
        const uint32_t deltas = block_addr + uint32_t(2 * l.n);
        auto delta = [&](uint32_t p, int ch) { return fadd(fmul(fcvt_s(u8(p)), tb[4 * ch + 3]), tb[4 * ch + 2]); };
        if (from < in_block) {
            for (int f = from; f < in_block; ++f) {
                const uint32_t p = deltas + uint32_t(f * l.n);
                for (int ch = 0; ch < l.n; ++ch) buf1_[size_t(ch)] = fadd(buf1_[size_t(ch)], delta(p + uint32_t(ch), ch));
            }
        } else if (in_block < from) {
            for (int f = from - 1; f >= in_block; --f) {
                const uint32_t p = deltas + uint32_t(f * l.n);
                for (int ch = 0; ch < l.n; ++ch) buf1_[size_t(ch)] = fsub(buf1_[size_t(ch)], delta(p + uint32_t(ch), ch));
            }
        }
        cached_ = key;
        float t;
        const bool interp = delta_t(l, time, it, key, t) && key < l.num_keys - 1;
        const uint16_t* index = nf::p32_ptr<const uint16_t>(l.index);
        auto out = [&](int ch) -> float* { return pose_span(pose_floats, index[ch], 1) ? pose + index[ch] : nullptr; };
        if (!interp) {
            for (int ch = 0; ch < l.n; ++ch)
                if (float* o = out(ch)) *o = buf1_[size_t(ch)];
        } else {
            const int next = key + 1;
            const int next_block = next >> l.shift;
            if (next == key2_) {
                for (int ch = 0; ch < l.n; ++ch) {
                    const float b1 = buf1_[size_t(ch)];
                    if (float* o = out(ch)) *o = fadd(b1, fmul(fsub(buf2_[size_t(ch)], b1), t));
                }
            } else if (next_block == block) {
                const uint32_t p = deltas + uint32_t(in_block * l.n);
                for (int ch = 0; ch < l.n; ++ch) {
                    const float d = delta(p + uint32_t(ch), ch);
                    const float b1 = buf1_[size_t(ch)];
                    buf2_[size_t(ch)] = fadd(b1, d);
                    if (float* o = out(ch)) *o = fadd(b1, fmul(d, t));
                }
                key2_ = next;
            } else {
                uint32_t p = l.blocks + uint32_t(next_block * l.block_bytes);
                for (int ch = 0; ch < l.n; ++ch, p += 2) {
                    buf2_[size_t(ch)] = fadd(fmul(fcvt_s(u16(p)), tb[4 * ch + 1]), tb[4 * ch]);
                    const float b1 = buf1_[size_t(ch)];
                    if (float* o = out(ch)) *o = fadd(b1, fmul(fsub(buf2_[size_t(ch)], b1), t));
                }
                key2_ = next;
            }
        }
        if (l.nconst) {
            uint32_t ip = delta_constants(l);
            uint32_t vp = ((ip + 2u * uint32_t(l.nconst) + 3) >> 2) << 2;
            for (int i = 0; i < l.nconst; ++i, ip += 2, vp += 4) {
                const int idx = u16(ip);
                if (pose_span(pose_floats, idx, 1)) pose[idx] = f32(vp);
            }
        }
    }
};

}  // namespace

// [driving] 0x002647c8 NewFnAnim
std::unique_ptr<FnAnim> NewFnAnim(uint16_t type) {
    std::unique_ptr<FnAnim> a;
    switch (type) {
        case kFnRawEvent: a = std::make_unique<FnRawEventChannel>(); break;
        case kFnDeltaQuat: a = std::make_unique<FnDeltaQuatChan>(); break;
        case kFnKeyQuat: a = std::make_unique<FnKeyQuatChan>(); break;
        case kFnCompound: a = std::make_unique<FnCompoundChannel>(); break;
        case kFnDeltaF3: a = std::make_unique<FnDeltaF3>(); break;
        case kFnDeltaF1: a = std::make_unique<FnDeltaF1>(); break;
        default: ++g_stats.unknown_types; return nullptr;
    }
    ++g_stats.created;
    return a;
}

// [driving] 0x00266518 EAGLAnim::FnCompoundChannel::SetAnimMemoryMap
void FnCompoundChannel::SetAnimMemoryMap(uint32_t data) {
    data_ = data;
    subs_.clear();  // os subcanais voltam ao reservatório; o vetor também
    subs_made_ = false;
}

// O começo comum do Eval e do EvalEvent (sem nome no PS2: o código está repetido nas duas): os subcanais do último para
// o primeiro, cada um com o tipo do u16 dos seus dados.
void FnCompoundChannel::MakeSubChannels() {
    if (subs_made_) return;
    const int count = u16(data_ + 8);
    subs_.clear();
    subs_.resize(size_t(count > 0 ? count : 0));
    for (int i = count - 1; i >= 0; --i) {
        const uint32_t sub_data = w32(data_ + 0xc + 4u * uint32_t(i));
        std::unique_ptr<FnAnim> a = NewFnAnim(u16(sub_data));
        if (a) a->SetAnimMemoryMap(sub_data);
        subs_[size_t(i)] = std::move(a);
    }
    subs_made_ = true;
}

// [driving] 0x002666b8 EAGLAnim::FnCompoundChannel::Eval
void FnCompoundChannel::Eval(float prev, float time, float* pose, int pose_floats) {
    const int count = u16(data_ + 8);
    MakeSubChannels();
    if (use_fps_) {
        const float fps = fcvt_s(fps_);
        time = fmul(time, fps);
        prev = fmul(prev, fps);
    }
    for (int i = count - 1; i >= 0; --i)
        if (subs_[size_t(i)]) subs_[size_t(i)]->Eval(prev, time, pose, pose_floats);
}

// [driving] 0x002663b0 EAGLAnim::FnCompoundChannel::GetLength
bool FnCompoundChannel::GetLength(float& out) const {
    out = fcvt_s(u16(data_ + 0xa));
    if (use_fps_) out = fdiv(out, fcvt_s(fps_));
    return true;
}

// [driving] 0x002659a8 EAGLAnim::FnCompoundChannel::EvalEvent: os subcanais (criados como no Eval), do último para o
// primeiro; devolve 1 se algum devolveu.
bool FnCompoundChannel::EvalEvent(float prev, float time, uint32_t handlers, uint32_t user) {
    const int count = u16(data_ + 8);
    MakeSubChannels();
    if (use_fps_) {
        const float fps = fcvt_s(fps_);
        time = fmul(time, fps);
        prev = fmul(prev, fps);
    }
    bool any = false;
    for (int i = count - 1; i >= 0; --i) {
        FnAnim* a = subs_[size_t(i)].get();
        if (!a) {
            ++g_stats.guards;  // um tipo que a fábrica do port não tem (o PS2 chamaria o objeto dele)
            continue;
        }
        any = a->EvalEvent(prev, time, handlers, user) || any;
    }
    return any;
}

RawEventDispatch& raw_event_dispatch() {
    static RawEventDispatch d = ActEvent_Dispatch;
    return d;
}

// [driving] 0x00274958 EAGLAnim::RawEventChannel::Eval(float anterior, float tempo, int &cursor, float &último, tabela,
// dados): acha o cursor pelos tempos e chama o tratador de cada evento passado (do cursor ao fim e do começo, quando o
// tempo volta; só os do próprio quadro quando anterior == tempo).
void RawEventChannel_Eval(uint32_t data, int32_t& cursor, float& last, float prev, float time, uint32_t handlers,
                          uint32_t user) {
    namespace ee = ps2::ee;
    const uint32_t events = data + 8;
    const int32_t count = s32(data + 4);
    auto ev_time = [&](int32_t i) { return f32(events + 0x10u * uint32_t(i) + 4); };
    auto fire = [&](int32_t i) {
        if (i < 0) {
            ++g_stats.guards;  // o cursor -1 de um canal vazio: o PS2 leria o evento antes do começo
            return;
        }
        const uint32_t ev = events + 0x10u * uint32_t(i);
        const uint32_t h = w32(handlers + 4u * w32(ev));
        if (h) raw_event_dispatch()(h, time, ev, user);
    };
    int32_t s = 0;
    if (ee::lt(prev, last)) {
        s = cursor;
        if (s >= 0 && !ee::le(ev_time(s), prev)) {
            do {
                --s;
            } while (s >= 0 && !ee::le(ev_time(s), prev));
        }
        cursor = s + 1;
    }
    s = cursor;
    if (s < count && !ee::lt(prev, ev_time(s))) {
        do {
            ++s;
        } while (s < count && !ee::lt(prev, ev_time(s)));
    }
    cursor = s;
    if (ee::eq(prev, time)) {
        if (!(s < count)) cursor = count - 1;
        s = cursor;
        if (s >= 0 && !ee::lt(ev_time(s), time)) {
            do {
                --s;
            } while (s >= 0 && !ee::lt(ev_time(s), time));
        }
        cursor = s + 1;
    } else if (ee::lt(time, prev)) {
        // O tempo voltou (o laço): os eventos do cursor ao fim.
        for (int32_t i = s; i < count; ++i) fire(i);
        cursor = 0;
    }
    s = cursor;
    while (s < count && ee::le(ev_time(s), time)) {
        fire(s);
        ++s;
    }
    cursor = s;
    if (!(s < count)) cursor = count - 1;
    last = time;
}

}  // namespace drv::anim
