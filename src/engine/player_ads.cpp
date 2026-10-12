// A mira moderna (ADS) e o estilo de mira. Ver player_ads.h.

#include "engine/player_ads.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/gameflow.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/vecmath.h"
#include "platform/pc_gameplay.h"
#include "platform/psi_input.h"

namespace {

// A tabela medida (nf_loader_check --miras; tests/sights_measure.cpp): uma linha por pele de arma na mão.
// Gerada da geometria do jogo; só números (sem dados do disco).
const AdsSightEntry kAdsSights[] = {
#include "engine/player_ads_table.inc"
};
constexpr int kAdsSightCount = int(sizeof(kAdsSights) / sizeof(kAdsSights[0]));

// O estado do jogador 0.
struct State {
    const obj_tag* player = nullptr;
    float t = 0.0f;               // 0 quadril, 1 mira (linear no tempo)
    float view_zoom = 1.0f;       // o zoom leve em vigor
    const AdsSightEntry* entry = nullptr;  // a linha da arma na pose (a da saída, ao recarregar ou trocar)
    bool fixed = false;           // a mira presa no centro no último tick
    bool active = false;          // o estilo moderno valia no último tick
    int device_override = -1;
};
State g;

constexpr float kBlendTicks = 10.0f;  // ~0,17 s a 60 Hz
constexpr float kSwayInAds = 0.25f;

// Os braços da arma na mão (os 38 primeiros ossos de todas as peles de arma: 0 o peito, 1-2-3 o braço, o antebraço e a
// mão esquerdos com os dedos 4-18, 19-20-21 os direitos com os dedos 22-36, 37 preso ao 2; a arma é 38 em diante, filha
// da mão direita). Na pose de mira o peito, os braços e um pouco dos antebraços descem e recuam: com a arma no centro, a
// pose do quadril poria o ombro e o braço na frente do olho; assim os antebraços sobem de baixo até as mãos, que ficam
// na arma.
struct ArmDrop {
    uint8_t bone;
    float share;
};
constexpr ArmDrop kAdsArmDrop[] = {{0, 1.0f}, {1, 1.0f}, {19, 1.0f}, {37, 1.0f}, {2, 0.6f}, {20, 0.6f}};
constexpr int kAdsArmBones = int(sizeof(kAdsArmDrop) / sizeof(kAdsArmDrop[0]));
constexpr float kAdsArmDropMetres = 0.3f;
constexpr float kAdsSightHalfWidth = 0.04f;  // a faixa da silhueta de cima, para cada lado do x do cano

float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void norm3(float* v) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 1e-9f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

int current_device() { return g.device_override >= 0 ? g.device_override : psiInput_ActiveDevice(); }

bool modern_now() { return ads_modern_for(ads_style(), current_device()); }

// A pele da arma na mão (nula sem objeto de animação).
const ANIMSKIN_tag* gun_skin(const BLData* b) {
    if (b == nullptr || b->gun == nullptr || b->gun->sphere_src == nullptr) return nullptr;
    return reinterpret_cast<const ANIMOBJ_tag*>(b->gun->sphere_src)->skin;
}

// A arma na mão é de fogo, com modelo e sem luneta (a mira fica presa no centro no estilo moderno).
bool fixed_weapon(obj_tag* player) {
    const BLData* b = static_cast<const BLData*>(player->data);
    if (gun_skin(b) == nullptr) return false;
    const weapon_definition_tag* d = weapon_def(anim_of(player)->weapon);
    return (d->flags & 0x40) == 0 && d->skin != 0;
}

// A arma tem pose de mira: linha medida, sem luneta e sem zoom (as de zoom sem luneta, 0x18/0x19 e 0x2a/0x2b, têm uma
// luneta no modelo, em cima da linha; ficam com a visão de zoom do jogo, como as de luneta).
const AdsSightEntry* entry_for(obj_tag* player) {
    if (!fixed_weapon(player)) return nullptr;
    const weapon_definition_tag* d = weapon_def(anim_of(player)->weapon);
    if (d->zoom_max > 1.0f) return nullptr;
    return ads_sight_entry(gun_skin(static_cast<const BLData*>(player->data))->hash);
}

}  // namespace

// ---- o estilo ----

const char* ads_aim_style_name(AimStyle s) {
    switch (s) {
        case AimStyle::Modern: return "modern";
        case AimStyle::Classic: return "classic";
        default: return "auto";
    }
}

bool ads_parse_aim_style(const char* text, AimStyle* out) {
    if (text == nullptr) return false;
    for (const AimStyle s : {AimStyle::Auto, AimStyle::Modern, AimStyle::Classic}) {
        if (std::strcmp(text, ads_aim_style_name(s)) == 0) {
            *out = s;
            return true;
        }
    }
    return false;
}

bool ads_modern_for(AimStyle s, int device) {
    if (s == AimStyle::Modern) return true;
    if (s == AimStyle::Classic) return false;
    return device == 0;
}

void ads_set_style(AimStyle s) { nf::pc_gameplay().aim_style = uint8_t(s); }
AimStyle ads_style() { return AimStyle(std::min<uint8_t>(nf::pc_gameplay().aim_style, 2)); }
void ads_set_device_override(int device) { g.device_override = device; }

bool ads_arm_skeleton(const uint8_t* parents, uint32_t bones) {
    if (parents == nullptr || bones < 38) return false;
    const auto parent = [&](int i) { return parents[i] == 0xff ? -1 : int(parents[i] & 0x7f); };
    return parent(1) == 0 && parent(2) == 1 && parent(3) == 2 && parent(19) == 0 && parent(20) == 19 &&
           parent(21) == 20 && parent(37) == 2;
}

// ---- a tabela ----

namespace {
const AdsSightEntry* g_extra_entry = nullptr;  // ads_debug_extra_entry
}

void ads_debug_extra_entry(const AdsSightEntry* e) { g_extra_entry = e; }

const AdsSightEntry* ads_sight_entry(uint32_t skin) {
    if (g_extra_entry != nullptr && g_extra_entry->skin == skin) return g_extra_entry;
    for (const AdsSightEntry& e : kAdsSights) {
        if (e.skin == skin) return &e;
    }
    return nullptr;
}
int ads_sight_entry_count() { return kAdsSightCount; }
const AdsSightEntry* ads_sight_entries() { return kAdsSights; }

// ---- o achador ----

bool ads_find_sights(const float (*pts)[3], size_t n, const float muzzle[3], float z_min, AdsSights* out,
                     AdsFindStats* stats) {
    AdsFindStats st;
    // O eixo do cano, para quando não há linha: 0,25 m dele até o anexo.
    out->rear[0] = out->front[0] = muzzle[0];
    out->rear[1] = out->front[1] = muzzle[1];
    out->front[2] = muzzle[2];
    out->rear[2] = muzzle[2] - 0.25f;
    if (n == 0) {
        if (stats != nullptr) *stats = st;
        return false;
    }
    float z0 = pts[0][2], z1 = pts[0][2];
    for (size_t i = 0; i < n; ++i) {
        z0 = std::min(z0, pts[i][2]);
        z1 = std::max(z1, pts[i][2]);
    }
    st.length = z1 - z0;
    // A faixa da arma em volta do cano (4 cm para cada lado): a silhueta de cima dela. Uma faixa só do meio perderia o
    // alto do ferrolho, que nos modelos do PS2 é uma face plana com vértices só nas bordas; as orelhas da alça entram, e
    // a linha passa por cima delas (a massa fica alguns milímetros abaixo dela, no entalhe).
    std::vector<int> band;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(pts[i][0] - muzzle[0]) <= kAdsSightHalfWidth && pts[i][2] >= z_min) band.push_back(int(i));
    }
    st.band_points = int(band.size());
    if (band.size() < 2 || st.length <= 1e-4f) {
        if (stats != nullptr) *stats = st;
        return false;
    }
    // O perfil de cima: o y mais alto em cada uma de 64 faixas de z.
    constexpr int kBins = 64;
    float bz[kBins], by[kBins];
    bool have[kBins] = {};
    for (const int i : band) {
        const float z = pts[i][2], y = pts[i][1];
        const int k = std::min(kBins - 1, int((z - z0) / st.length * float(kBins)));
        if (!have[k] || y > by[k]) {
            have[k] = true;
            bz[k] = z;
            by[k] = y;
        }
    }
    // O casco convexo de cima (da esquerda para a direita, tira o ponto do meio quando o próximo fica por cima da linha).
    std::vector<int> hull;
    for (int k = 0; k < kBins; ++k) {
        if (!have[k]) continue;
        while (hull.size() >= 2) {
            const int a = hull[hull.size() - 2], b = hull.back();
            const float cross = (bz[b] - bz[a]) * (by[k] - by[a]) - (by[b] - by[a]) * (bz[k] - bz[a]);
            if (cross >= 0.0f) {
                hull.pop_back();
            } else {
                break;
            }
        }
        hull.push_back(k);
    }
    // A aresta quase horizontal (inclinação até 0,2) mais longa.
    int best = -1;
    float best_span = 0.0f;
    for (size_t i = 0; i + 1 < hull.size(); ++i) {
        const int a = hull[i], b = hull[i + 1];
        const float span = bz[b] - bz[a];
        if (span <= 0.0f) continue;
        const float slope = (by[b] - by[a]) / span;
        if (std::fabs(slope) < 0.2f && span > best_span) {
            best_span = span;
            best = int(i);
        }
    }
    st.span = best_span;
    if (stats != nullptr) *stats = st;
    // Pelo menos 1/4 da arma ou 20 cm (nas armas longas o comprimento inclui a coronha, que vai até o ombro).
    if (best < 0 || best_span < std::min(0.25f * st.length, 0.2f)) return false;
    const int a = hull[size_t(best)], b = hull[size_t(best) + 1];
    out->rear[0] = muzzle[0];
    out->rear[1] = by[a];
    out->rear[2] = bz[a];
    out->front[0] = muzzle[0];
    out->front[1] = by[b];
    out->front[2] = bz[b];
    return true;
}

// ---- a pose ----

AdsPose ads_pose(const AdsSights& s, float depth, float drop) {
    // A base: D da alça para a massa, U o "cima" da arma (y do objeto sem a parte em D), L = U x D.
    float D[3] = {s.front[0] - s.rear[0], s.front[1] - s.rear[1], s.front[2] - s.rear[2]};
    norm3(D);
    if (dot3(D, D) < 0.5f) {
        D[0] = 0.0f;
        D[1] = 0.0f;
        D[2] = 1.0f;
    }
    float U[3] = {-D[1] * D[0], 1.0f - D[1] * D[1], -D[1] * D[2]};
    norm3(U);
    const float L[3] = {U[1] * D[2] - U[2] * D[1], U[2] * D[0] - U[0] * D[2], U[0] * D[1] - U[1] * D[0]};
    // A rotação R com p·R: D vai a z, U a y, L a x (as colunas de R são L, U, D). Os ângulos de RotMatrixZYX:
    // R[0][2] = -sen(y), R[1][2] = cos(y) sen(x), R[2][2] = cos(y) cos(x), R[0][1] = sen(z) cos(y), R[0][0] = cos(z) cos(y).
    AdsPose p{};
    p.rot[0] = std::atan2(D[1], D[2]);
    p.rot[1] = std::asin(std::clamp(-D[0], -1.0f, 1.0f));
    p.rot[2] = std::atan2(U[0], L[0]);
    MATRIX R;
    Mat_Identity(&R);
    const _VECTOR e{p.rot[0], p.rot[1], p.rot[2], 0.0f};
    RotMatrixZYX(&e, &R);
    // pos = (alvo - c)·Rᵀ - alça, com c = (0, -0,2, 0,5) (Player_PositionGun) e o alvo (0, -drop, depth).
    const float t[3] = {0.0f, -drop + 0.2f, depth - 0.5f};
    for (int j = 0; j < 3; ++j) p.pos[j] = t[0] * R.m[j][0] + t[1] * R.m[j][1] + t[2] * R.m[j][2] - s.rear[j];
    return p;
}

void ads_view_point(const float rot[3], const float pos[3], const float p[3], float out[3]) {
    MATRIX R;
    Mat_Identity(&R);
    const _VECTOR e{rot[0], rot[1], rot[2], 0.0f};
    RotMatrixZYX(&e, &R);
    const float q[3] = {p[0] + pos[0], p[1] + pos[1], p[2] + pos[2]};
    const float c[3] = {0.0f, -0.2f, 0.5f};
    for (int j = 0; j < 3; ++j) out[j] = q[0] * R.m[0][j] + q[1] * R.m[1][j] + q[2] * R.m[2][j] + c[j];
}

float ads_step_weight(float t, bool want, float frame_rate_mul) {
    const float step = std::max(frame_rate_mul, 0.0f) / kBlendTicks;
    // Com uma folga para o arredondamento das somas (10 x 0,1 não dá 1 em float).
    if (want) return t + step > 1.0f - 1e-4f ? 1.0f : t + step;
    return t - step < 1e-4f ? 0.0f : t - step;
}

float ads_ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---- o estado ----

float ads_weight() { return ads_ease(g.t); }
float ads_view_zoom() { return g.view_zoom; }

float ads_effective_zoom(const BLData* b) {
    const float game = b != nullptr ? b->zoom : 1.0f;
    return game * g.view_zoom;
}

bool ads_fixed_crosshair(obj_tag* obj) {
    if (obj == nullptr || obj->data == nullptr) return false;
    const BLData* b = static_cast<const BLData*>(obj->data);
    if (b->player != 0 || !modern_now()) return false;
    return fixed_weapon(obj);
}

bool ads_hide_crosshair(const BLData* b) { return b != nullptr && b->player == 0 && ads_weight() > 0.5f; }

bool ads_move_while_aiming(obj_tag* obj) {
    if (obj == nullptr || obj->data == nullptr) return false;
    const BLData* b = static_cast<const BLData*>(obj->data);
    return b->player == 0 && modern_now();
}

void ads_tick(obj_tag* player, const _VECTOR* sway) {
    if (player == nullptr || player->data == nullptr) return;
    BLData* b = static_cast<BLData*>(player->data);
    if (b->player != 0 || b->gun == nullptr) return;
    if (g.player != player) {
        g = State{player, 0.0f, 1.0f, nullptr, false, false, g.device_override};
    }
    ANIMOBJ_tag* anim = anim_of(player);
    obj_tag* gun = b->gun;
    const ANIMSKIN_tag* skin = gun_skin(b);
    g.active = modern_now();
    const AdsSightEntry* e = g.active ? entry_for(player) : nullptr;
    g.fixed = g.active && fixed_weapon(player);
    const uint16_t gs = gun->state;
    const bool want = e != nullptr && (anim->flags & 1) != 0 && (gs == 0 || gs == 9) && b->cam_mode == 0 &&
                      int16_t(player->state) == 1;
    if (want) g.entry = e;
    // Outra pele na mão (a troca já passou do modelo): a pose da anterior não serve mais.
    if (g.entry != nullptr && (skin == nullptr || skin->hash != g.entry->skin)) {
        g.t = 0.0f;
        g.entry = want ? e : nullptr;
    }
    g.t = ads_step_weight(g.t, want, FRAME_RATE_MUL);
    const float w = ads_ease(g.t);
    if (g.entry == nullptr || w <= 0.0f) {
        g.view_zoom = 1.0f;
        if (g.t <= 0.0f) g.entry = nullptr;
        return;
    }
    const AdsSightEntry& en = *g.entry;
    g.view_zoom = 1.0f + (en.zoom - 1.0f) * w;
    // A alça na profundidade em que ela fica no quadril (um pouco mais perto), vezes o zoom total: com a visão
    // aumentada Z vezes, a arma Z vezes mais longe fica do mesmo tamanho na tela.
    const weapon_definition_tag* d = weapon_def(anim->weapon);
    const float hip_depth = en.rear[2] + d->offset[0][2];
    const float zoom = std::max(1.0f, b->zoom) * g.view_zoom;
    const float depth = std::clamp(hip_depth * 0.85f, 0.3f, 0.8f) * zoom;
    AdsSights s;
    std::memcpy(s.rear, en.rear, sizeof(s.rear));
    std::memcpy(s.front, en.front, sizeof(s.front));
    const AdsPose pose = ads_pose(s, depth, en.drop * zoom);
    const float sx = sway != nullptr ? sway->x : 0.0f, sy = sway != nullptr ? sway->y : 0.0f,
                sz = sway != nullptr ? sway->z : 0.0f;
    const float target[3] = {pose.pos[0] + sx * kSwayInAds, pose.pos[1] + sy * kSwayInAds, pose.pos[2] + sz * kSwayInAds};
    gun->pos.x += (target[0] - gun->pos.x) * w;
    gun->pos.y += (target[1] - gun->pos.y) * w;
    gun->pos.z += (target[2] - gun->pos.z) * w;
    // O giro do jogo (o coice em y; a mira suavizada vai a 0 mirando) por cima do da pose.
    gun->rot.x += pose.rot[0] * w;
    gun->rot.y += pose.rot[1] * w;
    gun->rot.z += pose.rot[2] * w;
}

void ads_pose_arms(obj_tag* player) {
    if (player == nullptr || player->data == nullptr || player != g.player) return;
    BLData* b = static_cast<BLData*>(player->data);
    const float w = ads_weight();
    if (w <= 0.0f || b->gun == nullptr || b->gun->sphere_src == nullptr) return;
    ANIMOBJ_tag* ga = anim_of(b->gun);
    const ANIMSKIN_tag* skin = ga->skin;
    if (skin == nullptr || ga->bones == nullptr || (ga->anim_flags & 4) == 0) return;
    if (!ads_arm_skeleton(skin->parents, skin_bone_count(skin))) return;
    // O referencial do olhar (Player_PositionGun): o giro do jogador inclinado pelo pitch.
    MATRIX look;
    RotMatrixX(-(b->pitch * 1.5707964f), &look);
    MulMatrix0(&player->matrix, &look, &look);
    const float zoom = std::max(1.0f, b->zoom) * g.view_zoom;
    for (int i = 0; i < kAdsArmBones; ++i) {
        const float k = kAdsArmDrop[i].share * w * kAdsArmDropMetres * zoom;
        const float down = -k, back = -k * 0.5f;
        MATRIX& m = ga->bones[kAdsArmDrop[i].bone];
        for (int c = 0; c < 3; ++c) m.m[3][c] += down * look.m[1][c] + back * look.m[2][c];
    }
}

int ads_describe(char* out, unsigned size) {
    const int w = g.entry != nullptr ? int(g.entry->skin & 0xffff) : -1;
    return std::snprintf(out, size, "ads estilo %s aparelho %d moderno %d fixa %d peso %.3f zoom %.3f pele %x linha %d",
                         ads_aim_style_name(ads_style()), current_device(), g.active ? 1 : 0, g.fixed ? 1 : 0,
                         double(ads_weight()), double(g.view_zoom), w,
                         g.entry != nullptr ? int(g.entry->quality) : 0);
}

void ads_reset() {
    const int dev = g.device_override;
    g = State{};
    g.device_override = dev;
}

void ads_debug_power_on() {
    g = State{};
    g_extra_entry = nullptr;
}
