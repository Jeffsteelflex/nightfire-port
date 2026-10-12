// Interpolação entre ticks (só do port). Ver frame_interp.h.

#include "render/frame_interp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "platform/psi_draw.h"

namespace nf {

namespace {

float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void cross3(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

bool normalize3(float* v) {
    const float l = std::sqrt(dot3(v, v));
    if (!(l > 1e-12f)) return false;
    for (int k = 0; k < 3; ++k) v[k] /= l;
    return true;
}

// Rotação de vetor-linha (linhas = imagens de x, y, z) -> quatérnio (x, y, z, w). A matriz de coluna é a
// transposta: M[i][j] = R[j][i].
void rot_to_quat(const float r[3][3], float q[4]) {
    const float m00 = r[0][0], m11 = r[1][1], m22 = r[2][2];
    const float m01 = r[1][0], m10 = r[0][1], m02 = r[2][0], m20 = r[0][2], m12 = r[2][1], m21 = r[1][2];
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m21 - m12) / s;
        q[1] = (m02 - m20) / s;
        q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s;
        q[0] = 0.25f * s;
        q[1] = (m01 + m10) / s;
        q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s;
        q[0] = (m01 + m10) / s;
        q[1] = 0.25f * s;
        q[2] = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s;
        q[0] = (m02 + m20) / s;
        q[1] = (m12 + m21) / s;
        q[2] = 0.25f * s;
    }
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; ++k) q[k] /= l;
}

void quat_to_rot(const float q[4], float r[3][3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    // Matriz de coluna M; R = M transposta.
    const float m[3][3] = {{1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
                           {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
                           {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r[i][j] = m[j][i];
    }
}

uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

float dist3(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(dot3(d, d));
}

// Um par de matrizes do mesmo desenho em dois ticks: decompostas (para interpolar) e se o salto passa dos limites.
enum class PairKind { Same, Moves, Jumps };
PairKind classify_pair(const float* a, const float* b, const ItemLimits& limits, float ticks, Xform* xa, Xform* xb) {
    if (std::memcmp(a, b, sizeof(float) * 16) == 0) return PairKind::Same;
    if (!xform_decompose(a, xa) || !xform_decompose(b, xb)) return PairKind::Jumps;
    if ((xa->s[0] < 0.0f) != (xb->s[0] < 0.0f)) return PairKind::Jumps;
    if (dist3(xa->t, xb->t) > limits.distance * ticks) return PairKind::Jumps;
    if (quat_angle(xa->q, xb->q) > limits.angle * ticks) return PairKind::Jumps;
    return PairKind::Moves;
}

}  // namespace

// ---------------------------------------------------------------- matemática

bool xform_decompose(const float m[16], Xform* out) {
    float r[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r[i][j] = m[i * 4 + j];
        out->s[i] = std::sqrt(dot3(r[i], r[i]));
        if (!(out->s[i] > 1e-6f)) return false;
        for (int j = 0; j < 3; ++j) r[i][j] /= out->s[i];
    }
    float c[3];
    cross3(r[1], r[2], c);
    if (dot3(r[0], c) < 0.0f) {
        out->s[0] = -out->s[0];
        for (int j = 0; j < 3; ++j) r[0][j] = -r[0][j];
    }
    // Ortonormal (Gram-Schmidt a partir da linha 0, a 2 pelo produto vetorial: mantém o determinante +1).
    if (!normalize3(r[0])) return false;
    const float d = dot3(r[1], r[0]);
    for (int j = 0; j < 3; ++j) r[1][j] -= d * r[0][j];
    if (!normalize3(r[1])) return false;
    cross3(r[0], r[1], r[2]);
    rot_to_quat(r, out->q);
    for (int j = 0; j < 3; ++j) out->t[j] = m[12 + j];
    return true;
}

void xform_compose(const Xform& x, float m[16]) {
    float r[3][3];
    quat_to_rot(x.q, r);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = r[i][j] * x.s[i];
    }
    for (int j = 0; j < 3; ++j) m[12 + j] = x.t[j];
}

float quat_angle(const float a[4], const float b[4]) {
    const float d = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * std::acos(std::min(1.0f, d));
}

void quat_slerp(const float a[4], const float b[4], float t, float out[4]) {
    float bb[4] = {b[0], b[1], b[2], b[3]};
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0f) {
        d = -d;
        for (float& v : bb) v = -v;
    }
    float wa, wb;
    if (d > 0.9995f) {
        wa = 1.0f - t;
        wb = t;
    } else {
        const float theta = std::acos(d);
        const float s = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / s;
        wb = std::sin(t * theta) / s;
    }
    float l = 0.0f;
    for (int k = 0; k < 4; ++k) {
        out[k] = wa * a[k] + wb * bb[k];
        l += out[k] * out[k];
    }
    l = std::sqrt(l);
    for (int k = 0; k < 4; ++k) out[k] /= l;
}

void xform_lerp(const Xform& a, const Xform& b, float t, Xform* out) {
    for (int k = 0; k < 3; ++k) {
        out->t[k] = a.t[k] + (b.t[k] - a.t[k]) * t;
        out->s[k] = a.s[k] + (b.s[k] - a.s[k]) * t;
    }
    quat_slerp(a.q, b.q, t, out->q);
}

void interp_matrix(const float a[16], const float b[16], float alpha, float out[16]) {
    if (alpha <= 0.0f) {
        std::memcpy(out, a, sizeof(float) * 16);
        return;
    }
    std::memcpy(out, b, sizeof(float) * 16);
    if (alpha >= 1.0f) return;
    Xform xa, xb, x;
    if (!xform_decompose(a, &xa) || !xform_decompose(b, &xb)) return;
    if ((xa.s[0] < 0.0f) != (xb.s[0] < 0.0f)) return;
    xform_lerp(xa, xb, alpha, &x);
    xform_compose(x, out);
}

void interp_camera(const InterpCamera& a, const InterpCamera& b, float alpha, InterpCamera* out) {
    interp_matrix(a.m.data(), b.m.data(), alpha, out->m.data());
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    out->fov_deg = a.fov_deg + (b.fov_deg - a.fov_deg) * t;
    if (alpha >= 1.0f) out->fov_deg = b.fov_deg;
}

float camera_yaw(const float m[16]) { return std::atan2(m[8], m[10]); }

float camera_elevation(const float m[16]) { return std::asin(std::clamp(m[9], -1.0f, 1.0f)); }

void camera_look_delta(float m[16], float dyaw, float dpitch, float max_elev, float W[9]) {
    const float r0[3] = {m[0], m[1], m[2]}, u0[3] = {m[4], m[5], m[6]}, f0[3] = {m[8], m[9], m[10]};
    const float elev = camera_elevation(m);
    const float dp = std::clamp(elev + dpitch, -max_elev, max_elev) - elev;
    const float cp = std::cos(dp), sp = std::sin(dp);
    float rows[3][3];
    for (int k = 0; k < 3; ++k) {
        rows[0][k] = r0[k];
        rows[1][k] = u0[k] * cp - f0[k] * sp;
        rows[2][k] = f0[k] * cp + u0[k] * sp;
    }
    const float cy = std::cos(dyaw), sy = std::sin(dyaw);
    for (auto& v : rows) {
        const float x = v[0], z = v[2];
        v[0] = x * cy + z * sy;
        v[2] = -x * sy + z * cy;
    }
    // W = R^T R' (R ortonormal): as linhas novas são as velhas vezes W.
    const float* R[3] = {r0, u0, f0};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            W[i * 3 + j] = R[0][i] * rows[0][j] + R[1][i] * rows[1][j] + R[2][i] * rows[2][j];
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = rows[i][j];
    }
}

void apply_about_eye(float m[16], const float W[9], const float eye[3]) {
    for (int i = 0; i < 3; ++i) {
        const float v[3] = {m[i * 4 + 0], m[i * 4 + 1], m[i * 4 + 2]};
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = v[0] * W[j] + v[1] * W[3 + j] + v[2] * W[6 + j];
    }
    const float t[3] = {m[12] - eye[0], m[13] - eye[1], m[14] - eye[2]};
    for (int j = 0; j < 3; ++j) m[12 + j] = t[0] * W[j] + t[1] * W[3 + j] + t[2] * W[6 + j] + eye[j];
}

uint64_t draw_identity(const void* owner, uint8_t owner_kind, const void* glist, uint32_t created, uint16_t seq,
                       bool skinned) {
    uint64_t h = mix64(uint64_t(uintptr_t(owner)));
    h = mix64(h ^ (uint64_t(owner_kind) << 56) ^ (uint64_t(skinned ? 1 : 0) << 48) ^ uint64_t(seq));
    h = mix64(h ^ uint64_t(uintptr_t(glist)));
    h = mix64(h ^ uint64_t(created));
    return h == 0 ? 1 : h;
}

bool camera_cut(const InterpCamera& a, const InterpCamera& b, float ticks, const CutLimits& limits) {
    const float n = std::max(1.0f, ticks);
    if (dist3(&a.m[12], &b.m[12]) > limits.distance * n) return true;
    Xform xa, xb;
    if (!xform_decompose(a.m.data(), &xa) || !xform_decompose(b.m.data(), &xb)) return true;
    if (quat_angle(xa.q, xb.q) > limits.angle * n) return true;
    if (a.fov_deg > 0.0f && b.fov_deg > 0.0f) {
        const float r = std::max(a.fov_deg / b.fov_deg, b.fov_deg / a.fov_deg);
        if (r > limits.fov_ratio) return true;
    }
    return false;
}

void interp_weights(const std::vector<float>& a, const std::vector<float>& b, float alpha, std::vector<float>* out) {
    const size_t n = std::max(a.size(), b.size());
    out->assign(n, 0.0f);
    for (size_t k = 0; k < n; ++k) {
        const float wa = k < a.size() ? a[k] : 0.0f;
        const float wb = k < b.size() ? b[k] : 0.0f;
        (*out)[k] = alpha <= 0.0f ? wa : (alpha >= 1.0f ? wb : wa + (wb - wa) * alpha);
    }
}

// ---------------------------------------------------------------- os registros

void FrameInterpolator::reset() {
    prev_ = TickRecord{};
    cur_ = TickRecord{};
    has_prev_ = false;
    has_cur_ = false;
    frame_.clear();
    slots_.clear();
    bone_a_.clear();
    bone_b_.clear();
    bone_moves_.clear();
    prev_ids_.clear();
    fades_.clear();
    stats_ = InterpStats{};
}

void FrameInterpolator::push(TickRecord&& rec, const AliveFn& alive, const CutLimits& cut, const ItemLimits& item) {
    const uint32_t pushes = stats_.pushes + 1;
    if (has_cur_) {
        prev_ = std::move(cur_);
        has_prev_ = true;
    }
    cur_ = std::move(rec);
    has_cur_ = true;
    stats_ = InterpStats{};
    stats_.pushes = pushes;
    slots_.clear();
    bone_a_.clear();
    bone_b_.clear();
    bone_moves_.clear();
    stats_.cut = !has_prev_ || prev_.scene_camera != cur_.scene_camera ||
                 camera_cut(prev_.camera, cur_.camera, cur_.ticks, cut);
    const bool pair = !stats_.cut;
    const float ticks = std::max(1.0f, cur_.ticks);

    // Os desenhos do tick anterior pela identidade (repetida: -2, sem par).
    prev_ids_.clear();
    if (pair) {
        for (size_t j = 0; j < prev_.items.size(); ++j) {
            const uint64_t id = prev_.items[j].interp.id;
            if (id == 0) continue;
            const auto [it, inserted] = prev_ids_.emplace(id, int32_t(j));
            if (!inserted) it->second = -2;
        }
    }
    std::vector<uint8_t> prev_used(pair ? prev_.items.size() : 0, 0);
    std::vector<Slot> pending;  // um por item do atual que precisa de trabalho por quadro (frame ainda sem índice)
    std::vector<int32_t> slot_of(cur_.items.size(), -1);
    for (size_t i = 0; i < cur_.items.size(); ++i) {
        const DrawItem& c = cur_.items[i];
        Slot s;
        s.cur = uint32_t(i);
        s.overlay = c.interp.list == 2 && c.mesh >= 0;
        if (pair && c.interp.id != 0) {
            const auto it = prev_ids_.find(c.interp.id);
            if (it != prev_ids_.end() && it->second >= 0) {
                const DrawItem& p = prev_.items[size_t(it->second)];
                if (p.mesh == c.mesh && p.palette.size() == c.palette.size()) s.prev = it->second;
            }
        }
        if (s.prev >= 0) {
            ++stats_.matched;
            prev_used[size_t(s.prev)] = 1;
            const DrawItem& p = prev_.items[size_t(s.prev)];
            bool jumps = false;
            const PairKind w = classify_pair(p.world.data(), c.world.data(), item, ticks, &s.wa, &s.wb);
            jumps = w == PairKind::Jumps;
            s.world_moves = w == PairKind::Moves;
            s.bone_first = uint32_t(bone_a_.size());
            s.bone_count = uint32_t(c.palette.size());
            bool bones_move = false;
            for (size_t k = 0; k < c.palette.size() && !jumps; ++k) {
                Xform xa, xb;
                const PairKind b = classify_pair(p.palette[k].data(), c.palette[k].data(), item, ticks, &xa, &xb);
                jumps = b == PairKind::Jumps;
                bone_a_.push_back(xa);
                bone_b_.push_back(xb);
                bone_moves_.push_back(b == PairKind::Moves ? 1 : 0);
                bones_move = bones_move || b == PairKind::Moves;
            }
            // Os pesos dos morphs de um rosto (DrawItem::morph; um lado sem pesos conta como zeros).
            s.morph_moves = !jumps && (!p.morph.empty() || !c.morph.empty()) && p.morph != c.morph;
            if (jumps) {
                ++stats_.snapped;
                bone_a_.resize(s.bone_first);
                bone_b_.resize(s.bone_first);
                bone_moves_.resize(s.bone_first);
                s.bone_count = 0;
                s.world_moves = false;
            } else {
                s.interp = s.world_moves || bones_move || s.morph_moves;
                if (s.morph_moves) ++stats_.morphs;
                if (!s.interp) {
                    bone_a_.resize(s.bone_first);
                    bone_b_.resize(s.bone_first);
                    bone_moves_.resize(s.bone_first);
                    s.bone_count = 0;
                }
            }
            if (s.interp) ++stats_.moving;
        }
        if (s.interp || s.overlay) {
            slot_of[i] = int32_t(pending.size());
            pending.push_back(s);
        }
    }

    // Os do anterior sem par que continuam valendo (a visibilidade é por tick): salas e células sempre, objetos
    // vivos; os da lista 0 no fim dela, os da lista 1 no começo dela.
    std::vector<uint32_t> kept0, kept1;
    if (pair) {
        // Um dono que o tick atual ainda desenha (com outro modelo: o destrutível quebrado, a sala que trocou de
        // modelo pelo canal) não leva o desenho velho junto.
        std::vector<const void*> owners;
        owners.reserve(cur_.items.size());
        for (const DrawItem& c : cur_.items) {
            if (c.interp.owner != nullptr) owners.push_back(c.interp.owner);
        }
        std::sort(owners.begin(), owners.end());
        for (size_t j = 0; j < prev_.items.size(); ++j) {
            if (prev_used[j] != 0) continue;
            const DrawItem& p = prev_.items[j];
            if (p.interp.id == 0 || p.mesh < 0 || (p.interp.list != 0 && p.interp.list != 1)) continue;
            if (std::binary_search(owners.begin(), owners.end(), p.interp.owner)) continue;
            const auto kind = PsiOwnerKind(p.interp.owner_kind);
            bool keep = kind == PsiOwnerKind::Cel;
            if (kind == PsiOwnerKind::Object) keep = alive && alive(p.interp.owner, p.interp.created);
            if (!keep) continue;
            (p.interp.list == 0 ? kept0 : kept1).push_back(uint32_t(j));
        }
    }
    int32_t last0 = -1, first1 = -1;
    for (size_t i = 0; i < cur_.items.size(); ++i) {
        if (cur_.items[i].interp.list == 0) last0 = int32_t(i);
        if (first1 < 0 && cur_.items[i].interp.list == 1) first1 = int32_t(i);
    }
    if (last0 < 0) kept0.clear();
    if (first1 < 0) kept1.clear();

    frame_.clear();
    frame_.reserve(cur_.items.size() + kept0.size() + kept1.size());
    const auto add_kept = [&](const std::vector<uint32_t>& kept) {
        for (const uint32_t j : kept) frame_.push_back(prev_.items[j]);
        stats_.kept += uint32_t(kept.size());
    };
    for (size_t i = 0; i < cur_.items.size(); ++i) {
        if (int32_t(i) == first1) add_kept(kept1);
        if (slot_of[i] >= 0) pending[size_t(slot_of[i])].frame = uint32_t(frame_.size());
        frame_.push_back(cur_.items[i]);
        if (int32_t(i) == last0) add_kept(kept0);
    }
    slots_ = std::move(pending);
    stats_.items = uint32_t(frame_.size());

    // Os esmaecimentos (psiFadeView, DrawItem::fade = a câmera): o alfa entre os dois ticks quando os dois têm o da
    // mesma câmera (também num corte da câmera: o esmaecimento é da tela, não da vista). Um que começa ou acaba fica
    // como o do tick atual (um que começa cheio, a volta do preto, não pisca).
    fades_.clear();
    if (has_prev_) {
        for (size_t i = 0; i < frame_.size(); ++i) {
            const DrawItem& c = frame_[i];
            if (c.fade < 0 || c.quads2d.empty()) continue;
            for (const DrawItem& p : prev_.items) {
                if (p.fade != c.fade || p.quads2d.empty()) continue;
                if (p.quads2d[0].rgba[3] != c.quads2d[0].rgba[3]) {
                    fades_.push_back({uint32_t(i), p.quads2d[0].rgba[3], c.quads2d[0].rgba[3]});
                }
                break;
            }
        }
    }
    stats_.fades = uint32_t(fades_.size());
}

const std::vector<DrawItem>& FrameInterpolator::build(float alpha, InterpCamera* camera) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    stats_.bones = 0;
    for (const Slot& s : slots_) {
        DrawItem& d = frame_[s.frame];
        const DrawItem& c = cur_.items[s.cur];
        if (!s.interp) {
            // Lista 2 sem interpolação: volta ao valor do tick (rotate_overlay a gira depois).
            d.world = c.world;
            continue;
        }
        const DrawItem& p = prev_.items[size_t(s.prev)];
        if (!s.world_moves || alpha >= 1.0f) {
            d.world = c.world;
        } else if (alpha <= 0.0f) {
            d.world = p.world;
        } else {
            Xform x;
            xform_lerp(s.wa, s.wb, alpha, &x);
            d.world = c.world;
            xform_compose(x, d.world.data());
        }
        if (s.morph_moves) {
            if (alpha >= 1.0f) {
                d.morph = c.morph;
            } else if (alpha <= 0.0f) {
                d.morph = p.morph;
            } else {
                interp_weights(p.morph, c.morph, alpha, &d.morph);
            }
        }
        for (uint32_t k = 0; k < s.bone_count; ++k) {
            const uint32_t b = s.bone_first + k;
            if (bone_moves_[b] == 0 || alpha >= 1.0f) {
                d.palette[k] = c.palette[k];
            } else if (alpha <= 0.0f) {
                d.palette[k] = p.palette[k];
            } else {
                Xform x;
                xform_lerp(bone_a_[b], bone_b_[b], alpha, &x);
                d.palette[k] = c.palette[k];
                xform_compose(x, d.palette[k].data());
                ++stats_.bones;
            }
        }
    }
    for (const FadeSlot& f : fades_) {
        const float a = float(f.a) + (float(f.b) - float(f.a)) * alpha;
        frame_[f.frame].quads2d[0].rgba[3] = uint8_t(std::clamp(int(std::lround(a)), 0, 255));
    }
    if (camera != nullptr) {
        if (!has_prev_ || stats_.cut) {
            *camera = cur_.camera;
        } else {
            interp_camera(prev_.camera, cur_.camera, alpha, camera);
        }
    }
    return frame_;
}

void FrameInterpolator::rotate_overlay(const float W[9], const float eye[3]) {
    for (const Slot& s : slots_) {
        if (s.overlay) apply_about_eye(frame_[s.frame].world.data(), W, eye);
    }
}

const DrawItem* FrameInterpolator::find_owner(const void* owner) const {
    for (const DrawItem& d : frame_) {
        if (d.interp.owner == owner && d.mesh >= 0) return &d;
    }
    return nullptr;
}

// ---------------------------------------------------------------- o ritmo

double next_frame_deadline(double prev_deadline, double now, double period) {
    double next = prev_deadline + period;
    if (now - next > period) next = now;
    return next;
}

TickPlan plan_tick(int32_t elapsed, bool force, float slowmo, float* acc) {
    TickPlan p;
    if (!(elapsed >= 1 || force)) return p;
    p.due = true;
    int32_t skip = std::max<int32_t>(elapsed, 1);
    if (skip > 2) {
        skip = 2;
        p.resync = true;
    }
    p.consumed = skip;
    if (!(slowmo < 1.0f)) {
        if (acc != nullptr) *acc = 0.0f;
        p.run = true;
        p.skip = skip;
        return p;
    }
    float a = acc != nullptr ? *acc : 0.0f;
    a += std::max(0.0f, slowmo) * float(skip);
    if (a >= 1.0f) {
        a = std::min(a - 1.0f, 1.0f);
        p.run = true;
        p.skip = 1;
    }
    if (acc != nullptr) *acc = a;
    return p;
}

float tick_alpha(double vblanks, double tick_vblank, double tick_len) {
    if (!(tick_len > 0.0)) return 1.0f;
    return float(std::clamp((vblanks - tick_vblank) / tick_len, 0.0, 1.0));
}

}  // namespace nf
