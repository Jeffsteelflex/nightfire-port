// Os caminhos do CARP e o RPathEngine (driving/render/path_engine.h), do MIPS do DRIVING.ELF. As desmontagens ficam em
// out/agent_drv_heli/ do checkout principal (fora do git).
//
// Divergências (port), também em cada função: a lista do RPathEngine é um vetor de nós (a mesma ordem); o cosf/sinf do
// newlib do VU0_EulerToQuat é o nf::fm::cos/sin; o aviso ao RSceneObj depois de mover uma instância é só contado.

#include "driving/game/target_picker.h"
#include "driving/render/path_engine.h"

#include <algorithm>
#include <cstring>

#include "driving/anim/eagl_anim.h"
#include "driving/real/vu0_math.h"
#include "platform/crt_compat.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// O kIdentityMatrix4 (0x355bb0).
const float kIdentity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
// kPathAxes (0x381f70): x vira -z, z vira x.
const float kPathAxes[4][4] = {{0, 0, -1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 1}};
// A base de Bézier (0x3c1f00).
const float kBezier[4][4] = {{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}};

PathEngineClock g_clock;
PathEngineStats g_stats;
std::vector<RPathHandle*> g_handles;
size_t g_iter = 0;
bool g_list = false;  // o fgPathHandles existe (o primeiro CreatePathHandle/AddInstanceList o cria)

// __fixunssfsi (0x24e340, o fptoui): truncado, com o ramo dos >= 2^31.
uint32_t fptoui(float x) {
    if (ee::lt(x, flt(0x4f000000u))) return uint32_t(ee::cvt_w(x));
    return uint32_t(ee::cvt_w(ee::sub(x, flt(0x4f000000u)))) + 0x80000000u;
}

// [driving] 0x002dbfa0 float * __lower_bound<float *, float, int>(first, last, &valor, int *)
const float* lower_bound(const float* first, const float* last, float v) {
    int32_t len = int32_t(last - first);
    while (len > 0) {
        const int32_t half = len >> 1;
        const float* mid = first + half;
        if (ee::lt(*mid, v)) {
            first = mid + 1;
            len = len - half - 1;
        } else {
            len = half;
        }
    }
    return first;
}

// O TransformPoint (0x002d18e8, a variante xyz) é o de game/target_picker.cpp (conferido na parte 20).

}  // namespace

// [driving] 0x002bfb50 floorf (newlib: pelos bits; o "huge + x > 0" é a soma do PS2)
float newlib_floorf(float x) {
    uint32_t i0 = bits(x);
    const int32_t j0 = int32_t((i0 >> 23) & 0xff) - 0x7f;
    const float huge = flt(0x7149f2cau);  // 1e30
    if (j0 < 23) {
        if (j0 < 0) {
            if (ee::lt(0.0f, ee::add(x, huge))) {
                if (int32_t(i0) >= 0) i0 = 0;
                else if ((i0 & 0x7fffffffu) != 0) i0 = 0xbf800000u;
            }
        } else {
            const uint32_t i = 0x007fffffu >> j0;
            if ((i0 & i) == 0) return x;
            if (ee::lt(0.0f, ee::add(x, huge))) {
                if (int32_t(i0) < 0) i0 += 0x00800000u >> j0;
                i0 &= ~i;
            }
        }
    } else {
        if (j0 == 0x80) return ee::add(x, x);
        return x;
    }
    return flt(i0);
}

// [driving] 0x002dadc8 CARP::PathInfo::FindChannelIndex
uint32_t PathInfo_FindChannelIndex(const PathInfo* p, const char* name) {
    uint32_t i = 0;
    for (; i < p->channel_count; ++i)
        if (nf::str_icmp(name, reinterpret_cast<const char*>(p->at(p->channel(i)->name))) == 0) break;
    return i;
}

// [driving] 0x002dae58 CARP::PathInfo::GetKnotIndex
uint32_t PathInfo_GetKnotIndex(const PathInfo* p, float t, uint32_t hint, const PathChannel* c) {
    if (c->flags & kPathStepped) return fptoui(newlib_floorf(t));
    const uint32_t n = c->key_count;
    const float* times = reinterpret_cast<const float*>(p->at(c->times));
    if (!(n - 1 < hint)) {
        if (ee::le(times[hint], t) && ee::lt(t, times[hint + 1])) return hint;
        uint32_t h = hint + 1;
        if (h < n - 1 && ee::le(times[h], t) && ee::lt(t, times[h + 1])) return h;
        h = hint - 1;
        if (h < n - 1 && ee::le(times[h], t) && ee::lt(t, times[h + 1])) return h;
    }
    uint32_t i = uint32_t(lower_bound(times, times + n, t) - times);
    if (i == 0) return 0;
    i -= 1;
    if (!(i < c->key_count)) i = c->key_count - 1;
    return i;
}

// [driving] 0x002dafb8 CARP::PathInfo::ComputeUnitTime
float PathInfo_ComputeUnitTime(float t, float a, float b) {
    if (ee::lt(a, b)) return ee::div(ee::sub(t, a), ee::sub(b, a));
    return 0.0f;
}

// [driving] 0x002db028 CARP::PathInfo::ScaleToUnitTime
float PathInfo_ScaleToUnitTime(const PathInfo* p, float t, uint32_t from, uint32_t to, const PathChannel* c) {
    if (c->flags & kPathStepped) return newlib_floorf(t);
    const float* times = reinterpret_cast<const float*>(p->at(c->times));
    return PathInfo_ComputeUnitTime(t, times[from], times[to]);
}

// [driving] 0x002dafe0 CARP::PathInfo::ComputeLinear: a + (b - a) x t na VU0, ou o VU0_fastqslerp (0x002cc018).
void PathInfo_ComputeLinear(const float a[4], const float b[4], float out[4], float t, bool slerp) {
    if (slerp) {
        anim::fast_qslerp(a, b, out, t);
        return;
    }
    Reg va, vb, vt = {bits(t), 0, 0, 0};
    load(va, a);
    load(vb, b);
    sub(vb, vb, va, XYZW);
    mulbc(vb, vb, vt, BX, XYZW);
    add(va, va, vb, XYZW);
    store(out, va);
}

// [driving] 0x002db080 CARP::PathInfo::EvaluateLinear
uint32_t PathInfo_EvaluateLinear(const PathInfo* p, float t, uint32_t hint, const PathChannel* c, float out[4]) {
    const uint32_t k = PathInfo_GetKnotIndex(p, t, hint, c);
    const uint32_t k1 = k + (k < c->key_count - 1 ? 1u : 0u);
    const float u = PathInfo_ScaleToUnitTime(p, t, k, k1, c);
    if (c->flags & kPathScalar) {
        const float* keys = reinterpret_cast<const float*>(p->at(c->keys));
        const float v = ee::add(keys[k], ee::mul(ee::sub(keys[k1], keys[k]), u));
        out[0] = v;
        out[3] = v;
        out[2] = v;
        out[1] = v;
    } else {
        const float* keys = reinterpret_cast<const float*>(p->at(c->keys));
        PathInfo_ComputeLinear(keys + 4 * k, keys + 4 * k1, out, u, ((int32_t(c->flags) >> 1) & 1) != 0);
    }
    return k;
}

// [driving] 0x002db188 CARP::PathInfo::EvaluateSpline: as linhas (valor k, saída k, entrada k+1, valor k+1) pela base de
// Bézier (VU0_MATRIX4_mult 0x002cbb88) e o polinômio (t³, t², t, 1) no ACC; a rotação pelo VU0_EulerToQuat.
uint32_t PathInfo_EvaluateSpline(const PathInfo* p, float t, uint32_t hint, const PathChannel* c, float out[4]) {
    const uint32_t k = PathInfo_GetKnotIndex(p, t, hint, c);
    const uint32_t last = c->key_count - 1;
    const uint32_t k1 = k < last ? k + 1 : last;
    const float u = PathInfo_ScaleToUnitTime(p, t, k, k1, c);
    const uint8_t* keys = p->at(c->keys);
    float m[4][4];
    std::memcpy(m[0], keys + k * 0x30 + 0x10, 16);
    std::memcpy(m[1], keys + k * 0x30 + 0x20, 16);
    std::memcpy(m[2], keys + k1 * 0x30, 16);
    std::memcpy(m[3], keys + k1 * 0x30 + 0x10, 16);
    VU0_MATRIX4_mult(kBezier, m, m);
    const float u2 = ee::mul(u, u);
    const float u3 = ee::mul(u2, u);
    const uint32_t poly[4] = {bits(u3), bits(u2), bits(u), 0x3f800000u};
    Reg r0, r1, r2, r3, acc, o;
    load(r0, m[0]);
    load(r1, m[1]);
    load(r2, m[2]);
    load(r3, m[3]);
    mulabc(acc, r0, poly, BX, XYZW);
    maddabc(acc, r1, poly, BY, XYZW);
    maddabc(acc, r2, poly, BZ, XYZW);
    maddbc(o, acc, r3, poly, BW, XYZW);
    store(out, o);
    if (c->flags & kPathRotation) VU0_EulerToQuat(out, out);
    return k;
}

// [driving] 0x002db320 CARP::PathInfo::Evaluate
uint32_t PathInfo_Evaluate(const PathInfo* p, uint32_t channel, float t, uint32_t hint, float out[4]) {
    const PathChannel* c = p->channel(channel);
    if ((c->flags & kPathSpline) == 0) return PathInfo_EvaluateLinear(p, t, hint, c, out);
    return PathInfo_EvaluateSpline(p, t, hint, c, out);
}

// [driving] 0x002db370 CARP::PathInfo::EvaluateMatrix
void PathInfo_EvaluateMatrix(const PathInfo* p, float t, uint32_t& pos_key, uint32_t& rot_key, float m[4][4],
                             float& weight) {
    float pos[4];
    if (p->position_channel != -1) {
        pos_key = PathInfo_Evaluate(p, uint32_t(p->position_channel), t, pos_key, pos);
        weight = pos[3];
        pos[3] = 1.0f;
    } else {
        pos_key = 0;
        std::memcpy(pos, m[3], 16);
        weight = 0.0f;
    }
    if (p->rotation_channel != -1) {
        float q[4];
        rot_key = PathInfo_Evaluate(p, uint32_t(p->rotation_channel), t, rot_key, q);
        VU0_quattom4(q, m);
    } else {
        rot_key = 0;
    }
    std::memcpy(m[3], pos, 16);
}

// [driving] 0x002ccab8 VU0_EulerToQuat. Divergência: o cosf/sinf do newlib (0x002bfa48, 0x002bfc38) é o nf::fm::cos/sin.
void VU0_EulerToQuat(const float euler[4], float q[4]) {
    const float two_pi = flt(0x40c90fdbu);
    const float cx = nf::fm::cos(ee::mul(euler[0], two_pi));
    const float sx = nf::fm::sin(ee::mul(euler[0], two_pi));
    const float cy = nf::fm::cos(ee::mul(euler[1], two_pi));
    const float sy = nf::fm::sin(ee::mul(euler[1], two_pi));
    const float cz = nf::fm::cos(ee::mul(euler[2], two_pi));
    const float sz = nf::fm::sin(ee::mul(euler[2], two_pi));
    // f23 = cx, f24 = sx, f25 = cy, f22 = sy, f20 = cz, f0 = sz.
    const float f5 = ee::mul(sy, sx);
    const float f3 = ee::mul(sy, cx);
    const float f4 = ee::neg(sx);
    const float f1 = ee::neg(cx);
    const float f8 = ee::mul(f3, sz);
    const float f7 = ee::mul(f5, sz);
    const float f6 = ee::mul(f4, cz);
    const float f5b = ee::mul(f5, cz);
    const float f3b = ee::mul(f3, cz);
    const float f2 = ee::mul(cx, cz);
    const float f1b = ee::mul(f1, sz);
    const float f4b = ee::mul(f4, sz);
    float m[4][4] = {};
    m[2][2] = ee::mul(cy, cx);
    m[1][2] = ee::mul(cy, sx);
    m[0][0] = ee::mul(cy, cz);
    m[0][1] = ee::mul(sz, cy);
    m[1][0] = ee::add(f1b, f5b);
    m[2][0] = ee::add(f4b, f3b);
    m[1][1] = ee::add(f2, f7);
    m[2][1] = ee::add(f6, f8);
    m[0][2] = ee::neg(sy);
    VU0_m4toquat(m, q);
}

// [driving] 0x002db478 CARP::RotateSplineAboutBase
void CARP_RotateSplineAboutBase(const float base[4][4], const float evaluated[4][4], const float spline[4][4],
                                float out[4][4]) {
    Reg bp = {}, ep = {}, sp = {}, p = {}, d = {};
    load(bp, base[3], XYZ);
    load(ep, evaluated[3], XYZ);
    load(sp, spline[3], XYZ);
    float rot[4][4];
    std::memcpy(rot, spline, sizeof rot);
    rot[3][0] = 0.0f;
    rot[3][1] = 0.0f;
    rot[3][2] = 0.0f;
    sub(p, bp, ep, XYZ);
    float pt[4];
    store(pt, p);
    TransformPoint3(&rot[0][0], pt, pt);
    load(p, pt);
    add(p, p, ep, XYZ);
    sub(d, sp, ep, XYZ);
    add(p, p, d, XYZ);
    VU0_MATRIX4_mult(base, rot, out);
    out[3][0] = f(p, 0);
    out[3][2] = f(p, 2);
    out[3][1] = f(p, 1);
}

// [driving] 0x002db690 CARP::AISpline::GetApplyTransform. A matriz que o EvaluateMatrix recebe é a da pilha sem valor
// inicial (o caminho com os dois canais a escreve inteira); aqui zerada.
void AISpline_GetApplyTransform(const AISplineRec* s, float out[4][4]) {
    if (s->disabled == 0 && s->path != 0) {
        float m[4][4] = {};
        uint32_t k1 = 0, k2 = 0;
        float w = 0.0f;
        PathInfo_EvaluateMatrix(nf::p32_ptr<const PathInfo>(s->path), 0.0f, k1, k2, m, w);
        CARP_RotateSplineAboutBase(kIdentity, m, s->matrix, out);
        return;
    }
    std::memcpy(out, kIdentity, sizeof kIdentity);
}

// ---- o relógio ----

PathEngineClock& path_engine_clock() {
    if (g_clock.frequency == 0.0f) g_clock.frequency = ee::div(1.0f, g_clock.rate);
    return g_clock;
}

// [driving] 0x001d1ec0 RTimeData::Init
void RTimeData_Init(float video_rate) {
    g_clock.rate = video_rate;
    g_clock.frequency = ee::div(1.0f, video_rate);
}

// [driving] 0x001b0f48 o tempo do RPathEngine: (t x fFrequency) x 60
float path_engine_time(float t) { return ee::mul(ee::mul(t, path_engine_clock().frequency), 60.0f); }

// ---- RPathHandle ----

// [driving] 0x001b1110 RPathEngine::RPathHandle::RPathHandle(CARP::Instance &, CARP::ProcAnimState &)
void RPathHandle_Construct(RPathHandle& h, uint32_t instance, uint32_t state) {
    std::memset(&h, 0, sizeof h);
    h.instance = instance;
    h.state = state;
    // kZeroCoord3 (0x355b80) e a palavra seguinte (0x355b88, o z dele: 0) no último tempo.
    h.velocity[0] = h.velocity[1] = h.velocity[2] = 0.0f;
    h.last_time = 0.0f;
    h.user = 0;  // 0x381f68 (""): no port a palavra fica 0 (a conferência não a compara)
    h.speed = nf::p32_ptr<const ProcAnimState>(state)->speed;
    std::memcpy(h.base, nf::p32_ptr<const uint8_t>(instance), 64);
    h.base[3][3] = 1.0f;
    h.base[0][3] = 0.0f;
    h.base[1][3] = 0.0f;
    h.base[2][3] = 0.0f;
}

// [driving] 0x001b0f68 RPathEngine::RPathHandle::RPathHandle(const RPathHandle &)
void RPathHandle_Copy(RPathHandle& h, const RPathHandle& from) {
    h = from;
    h.pos_key = 0;
    h.rot_key = 0;
}

// [driving] 0x001b1258 RPathEngine::RPathHandle::Init
void RPathHandle_Init(RPathHandle& h, float now) {
    const float t = path_engine_time(now);
    const ProcAnimState* st = nf::p32_ptr<const ProcAnimState>(h.state);
    h.next = st->path;
    h.current = st->path;
    h.velocity[0] = h.velocity[1] = h.velocity[2] = 0.0f;  // kZeroCoord3
    h.last_time = t;
    h.time = st->start_time;
    h.delta = 0.0f;
    h.speed = st->speed;
    RPathHandle_SetThrottle(h, 1.0f);
    RPathHandle_SetAcceleration(h, 1.0f);
    RPathHandle_SetDesiredThrottle(h, 1.0f);
    RPathHandle_SetAccelDelay(h, 0);
    h.running = int32_t((st->flags >> 15) & 1);
    if (st->master == 0) {
        h.master = 0;
    } else {
        RPathHandle* m = RPathEngine_GetPathHandle(st->master);
        h.master = nf::p32_u(m);
        if (m) h.delta = ee::sub(m->time, h.time);
    }
    if (st->identity_axes != 0) std::memcpy(h.apply, kIdentity, sizeof kIdentity);
    else std::memcpy(h.apply, kPathAxes, sizeof kPathAxes);
    float m[4][4];
    std::memcpy(m, h.base, sizeof m);
    if (h.current != 0) PathInfo_EvaluateMatrix(nf::p32_ptr<const PathInfo>(h.current), h.time, h.pos_key, h.rot_key, m,
                                                h.weight);
    VU0_MATRIX4_mult(h.apply, m, m);
    uint8_t* inst = nf::p32_ptr<uint8_t>(h.instance);
    for (int r = 0; r < 4; ++r) std::memcpy(inst + 16 * r, m[r], 12);
    if (st->scene_obj != 0) ++g_stats.scene_notify;  // a entrada +0x7c da tabela virtual do RSceneObj (1)
}

// [driving] 0x001b14b8 RPathEngine::RPathHandle::Update
void RPathHandle_Update(RPathHandle& h, float now) {
    const float t = path_engine_time(now);
    const float dt = ee::sub(t, h.last_time);
    if (ee::le(dt, 0.0f)) return;
    if (h.running == 0) {
        h.last_time = t;
        return;
    }
    if (h.current == 0) {
        h.current = h.next;
        h.last_time = t;
        return;
    }
    if (h.master != 0) {
        h.time = ee::sub(nf::p32_ptr<const RPathHandle>(h.master)->time, h.delta);
    } else {
        if (h.accel_delay != 0) {
            h.accel_delay -= 1;
        } else {
            const float th = h.throttle, want = h.desired;
            if (ee::eq(th, want)) {
                if (ee::eq(th, 0.0f)) h.running = 0;
            } else if (ee::lt(th, want)) {
                if (ee::lt(ee::sub(want, th), h.accel)) h.throttle = want;
                else h.throttle = ee::add(th, h.accel);
            } else if (ee::lt(want, th)) {
                if (ee::lt(ee::sub(th, want), h.accel)) h.throttle = want;
                else h.throttle = ee::sub(th, h.accel);
            }
        }
        const float step = ee::mul(ee::mul(dt, h.throttle), h.speed);
        h.delta = step;
        h.time = ee::add(h.time, step);
    }
    bool wrapped = false;
    const PathInfo* cur = nf::p32_ptr<const PathInfo>(h.current);
    if (ee::lt(cur->duration, h.time)) {
        wrapped = true;
        h.time = ee::sub(h.time, cur->duration);
    } else if (ee::lt(h.time, 0.0f)) {
        wrapped = true;
    }
    if (wrapped) {
        ++g_stats.wraps;
        if (h.next == 0) {
            if (ee::lt(h.time, 0.0f)) {
                h.throttle = 0.0f;
                h.time = 0.0f;
            } else {
                h.throttle = 0.0f;
                h.time = cur->duration;
            }
            h.running = 0;
        } else {
            h.current = h.next;
            const PathInfo* nx = nf::p32_ptr<const PathInfo>(h.next);
            if (ee::lt(h.time, 0.0f)) h.time = ee::add(h.time, nx->duration);
            else if (ee::lt(nx->duration, h.time)) h.time = nx->duration;
        }
    }
    float m[4][4];
    std::memcpy(m, h.base, sizeof m);
    PathInfo_EvaluateMatrix(nf::p32_ptr<const PathInfo>(h.current), h.time, h.pos_key, h.rot_key, m, h.weight);
    VU0_MATRIX4_mult(h.apply, m, m);
    uint8_t* inst = nf::p32_ptr<uint8_t>(h.instance);
    if (!wrapped) {
        const float rate = path_engine_clock().rate;
        float old[3];
        std::memcpy(old, inst + 0x30, 12);
        h.velocity[0] = ee::mul(ee::sub(m[3][0], old[0]), rate);
        h.velocity[1] = ee::mul(ee::sub(m[3][1], old[1]), rate);
        h.velocity[2] = ee::mul(ee::sub(m[3][2], old[2]), rate);
    }
    for (int r = 0; r < 4; ++r) std::memcpy(inst + 16 * r, m[r], 12);
    ++g_stats.updates;
    if (nf::p32_ptr<const ProcAnimState>(h.state)->scene_obj != 0) ++g_stats.scene_notify;
    h.last_time = t;
}

// [driving] 0x001b18a8 RPathEngine::RPathHandle::GetOrientMat
void RPathHandle_GetOrientMat(const RPathHandle& h, float out[4][4]) {
    std::memcpy(out, nf::p32_ptr<const uint8_t>(h.instance), 64);
    out[3][3] = 1.0f;
    out[0][3] = 0.0f;
    out[1][3] = 0.0f;
    out[2][3] = 0.0f;
    out[3][0] = 0.0f;
    out[3][1] = 0.0f;
    out[3][2] = 0.0f;
}

// [driving] 0x001b1898 RPathEngine::RPathHandle::GetPosition
const float* RPathHandle_GetPosition(const RPathHandle& h) {
    return reinterpret_cast<const float*>(nf::p32_ptr<const uint8_t>(h.instance) + 0x30);
}

// ---- RPathEngine ----

namespace {

RPathHandle* push_back(const RPathHandle& h) {
    RPathHandle* node = new RPathHandle;  // o nó de 0xdc bytes do FastAlloc (os elos ficam no vetor)
    RPathHandle_Copy(*node, h);
    g_handles.push_back(node);
    g_list = true;
    ++g_stats.created;
    return node;
}

bool path_state(const uint8_t* inst, const ProcAnimState* st) {
    return (inst[0xc] & 0x10) != 0 && st->type == 2 && st->path != 0;
}

}  // namespace

// [driving] 0x001b1c20 RPathEngine::CreatePathHandle
RPathHandle* RPathEngine_CreatePathHandle(uint32_t instance, uint32_t state, float now) {
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(instance);
    const ProcAnimState* st = nf::p32_ptr<const ProcAnimState>(state);
    if (!path_state(inst, st)) return nullptr;
    RPathHandle tmp;
    RPathHandle_Construct(tmp, instance, state);
    RPathHandle* node = push_back(tmp);
    RPathHandle_Init(*node, now);
    // O modelo do artigo da instância (+0x1c): a palavra +0x34 dele no user (o AISplinePath não tem).
    uint32_t art;
    std::memcpy(&art, inst + 0x1c, 4);
    if (art != 0) {
        uint32_t model;
        std::memcpy(&model, nf::p32_ptr<const uint8_t>(art), 4);
        if (model != 0) std::memcpy(&node->user, nf::p32_ptr<const uint8_t>(model) + 0x34, 4);
    }
    return node;
}

// [driving] 0x001b1dc0 RPathEngine::DestroyPathHandle
void RPathEngine_DestroyPathHandle(RPathHandle* h) {
    if (!g_list) return;
    auto it = std::find(g_handles.begin(), g_handles.end(), h);
    if (it == g_handles.end()) return;
    const size_t idx = size_t(it - g_handles.begin());
    g_handles.erase(it);
    if (g_iter > idx) --g_iter;
    delete h;
    ++g_stats.destroyed;
}

// [driving] 0x001b1988 RPathEngine::AddInstanceList
void RPathEngine_AddInstanceList(uint32_t instances, uint32_t states, uint32_t count, float now) {
    g_list = true;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t inst = instances + 0x40 * i;
            const uint8_t* ip = nf::p32_ptr<const uint8_t>(inst);
            if ((ip[0xc] & 0x10) == 0) continue;  // o estado só é lido com a bandeira
            uint16_t idx;
            std::memcpy(&idx, ip + 0xe, 2);
            const uint32_t st = states + 0x20u * idx;
            const ProcAnimState* s = nf::p32_ptr<const ProcAnimState>(st);
            if (!path_state(ip, s)) continue;
            if ((pass == 0) != (s->master == 0)) continue;
            RPathHandle tmp;
            RPathHandle_Construct(tmp, inst, st);
            push_back(tmp);
        }
    }
    for (RPathHandle* h : g_handles) RPathHandle_Init(*h, now);
}

// [driving] 0x001b2058 RPathEngine::Update
void RPathEngine_Update(float now) {
    if (!g_list) return;
    for (RPathHandle* h : g_handles) RPathHandle_Update(*h, now);
}

// [driving] 0x001b1e58 RPathEngine::Reset
void RPathEngine_Reset(float now) {
    for (RPathHandle* h : g_handles) RPathHandle_Init(*h, now);
}

// [driving] 0x001b1ec0 RPathEngine::Purge
void RPathEngine_Purge() {
    for (RPathHandle* h : g_handles) delete h;
    g_handles.clear();
    g_iter = 0;
    g_list = false;
}

// [driving] 0x001b20c8 RPathEngine::GetPathHandle
RPathHandle* RPathEngine_GetPathHandle(uint32_t instance) {
    for (RPathHandle* h : g_handles)
        if (h->instance == instance) return h;
    return nullptr;
}

// [driving] 0x001b1f28 RPathEngine::GetFirstPathHandle
RPathHandle* RPathEngine_GetFirstPathHandle() {
    g_iter = 0;
    return g_handles.empty() ? nullptr : g_handles[0];
}

// [driving] 0x001b1f68 RPathEngine::GetNextPathHandle
RPathHandle* RPathEngine_GetNextPathHandle() {
    if (g_iter < g_handles.size()) ++g_iter;
    return g_iter < g_handles.size() ? g_handles[g_iter] : nullptr;
}

const std::vector<RPathHandle*>& path_engine_handles() { return g_handles; }
PathEngineStats& path_engine_stats() { return g_stats; }

}  // namespace drv
