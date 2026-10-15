#include "driving/fx/missile_fx.h"

#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

MissileStreakTables g_tables;
MissileFxStats g_stats;

// 0x00334e20 o deslocamento lateral e 0x00334e30 a largura do tremor, por tipo de rastro.
const float kOffset[4] = {ps2::ffloat(0x3eb70a3d), ps2::ffloat(0x3dcccccd), ps2::ffloat(0x3dcccccd), 0.0f};
const float kWidth[4] = {ps2::ffloat(0x3e4ccccd), 0.0f, 0.0f, 0.0f};

// A conversão de um u32 em float do GCC (o cvt.s.w com o bit de cima dividido).
float u32_to_float(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(h, h);
}

}  // namespace

MissileStreakTables& missile_streak_tables() { return g_tables; }
MissileFxStats& missile_fx_stats() { return g_stats; }

// [driving] 0x001e3e28 RMissileStreak::Init
void RMissileStreak_Init(MissileStreakTables& t) {
    for (int32_t i = 0; i < 40; ++i) {
        int32_t a = 0x1e;
        if (i < 13) {
            const float f = ee::div(ee::cvt_s(i), 12.0f);
            a = ee::cvt_w(ee::mul(ee::mul(f, 30.0f), f));
        }
        t.colours[i] = uint32_t(a) << 24 | 0x808080u;
    }
    for (int32_t i = 0; i < 42; ++i) {
        t.stq[i][0] = (i & 1) ? 0.0f : 1.0f;
        t.stq[i][1] = (i & 2) ? 1.0f : 0.0f;
        t.stq[i][2] = 1.0f;
        t.stq[i][3] = 1.0f;
    }
    t.ready = true;
}

// [driving] 0x001e4640 RMissileStreak::Update(COORD4 &)
void RMissileStreak_Update(RMissileStreakData& s, const float pos[4], const float right[3]) {
    if (s.count < 0x60) {
        if ((s.tick & 1) == 0) s.count += 2;
        const int32_t i = s.count - 2;
        if (i < 0) {
            // Um rastro sem pontos num tique ímpar (o jogo não chega aqui: o primeiro Update é no tique 0): o PS2
            // escreveria os dois pontos antes do RMissileStreak (sobre a contagem, o tique e o tipo).
            ++g_stats.guards;
            ++s.tick;
            return;
        }
        const uint32_t kind = uint32_t(s.kind) & 3;  // (o PS2 indexa as tabelas sem limite)
        const float width = kWidth[kind];
        float* a = s.verts[i];
        float* b = s.verts[i + 1];
        std::memcpy(a, pos, 16);
        // O tremor: três números do RRandom (os bits 8-23 do produto) vezes a largura / 65536, menos a largura / 2.
        const float half = ee::neg(ee::mul(width, 0.5f));
        const float scale = ps2::ffloat(0x37800000);  // 1 / 65536
        for (int k = 0; k < 3; ++k) {
            const uint32_t r = RRandom_Generate(render_random());
            const float j = ee::add(half, ee::mul(ee::mul(ee::cvt_s(int32_t(r)), width), scale));
            a[k] = ee::add(a[k], j);
        }
        std::memcpy(b, a, 16);
        const float off = kOffset[kind];
        for (int k = 0; k < 3; ++k) a[k] = ee::add(a[k], ee::mul(right[k], off));
        for (int k = 0; k < 3; ++k) b[k] = ee::sub(b[k], ee::mul(right[k], off));
    }
    ++s.tick;
    ++g_stats.updates;
}

// [driving] 0x001e43e0 RMissileStreak::Update(void)
void RMissileStreak_UpdateDying(RMissileStreakData& s) {
    ++s.tick;
    ++g_stats.dying_updates;
}

// [driving] 0x001e4258 RMissileStreak::Draw
bool RMissileStreak_Draw(const RMissileStreakData& s, StreakDrawRange& out) {
    if (s.count < 4) return false;
    const int32_t end = (s.tick + 1) / 2 * 2;
    const int32_t last = end < s.count ? end : s.count;
    const int32_t start = end - 0x28;
    const int32_t first = 0 < start ? start : 0;
    const int32_t n = last - first;
    if (n < 4) return false;
    out.first = first;
    out.count = n;
    out.stq_first = (first & 2) ? 2 : 0;
    out.kind = s.kind;
    ++g_stats.draws;
    return true;
}

// [driving] 0x00181960 Missile::Render(bool): a luz e o rastro (o modelo fica com o RSceneObj)
void Missile_RenderEffects(MissileData& d, const PhysicsObjectWords& po, const float* body_pos, bool update,
                           const Simulation& sim, const float right[3], RLightManagerPositional& lm,
                           StreakStrip* strip) {
    ++g_stats.renders;
    if (po.render != 0) ++g_stats.guards;  // o RSceneObj::LocateFX não foi portado: o corpo no lugar dele
    if (!body_pos) {
        ++g_stats.guards;
        return;
    }
    alignas(16) float pos[4] = {body_pos[0], body_pos[1], body_pos[2], 1.0f};
    const int32_t weapon = d.weapon;
    if (weapon == 0xe) {
        // O EMP: a luz azul depois do tempo em linha reta.
        if (!(d.straight_time < int32_t(uint32_t(sim.steps) - uint32_t(d.start_step)))) return;
        alignas(16) float p[4] = {pos[0], pos[1], pos[2], 4.0f};
        float c[4] = {ps2::ffloat(0x3e99999a), ps2::ffloat(0x3e99999a), 1.0f, 0.0f};  // 0x378660
        if (d.decaying != 0) {
            const float f = ee::mul(ee::cvt_s(d.decay_time), sim.dt);
            p[3] = 1.0f;
            for (int k = 0; k < 3; ++k) c[k] = ee::mul(c[k], f);
        }
        RLightManager_AddPositionalLight(lm, p, c);
        ++g_stats.emp_lights;
        return;
    }
    if (update) {
        const int32_t type = weapon == 1 ? 1 : (weapon == 0x19 ? 2 : 0);
        if (d.decaying == 0 && weapon != 0x15) {
            const uint32_t age = uint32_t(sim.steps) - uint32_t(d.start_step);
            const uint32_t q = uint32_t(sim.frame_rate) >> 2;
            float v;
            if (age < q) {
                const float t = u32_to_float(q - age);
                const float t2 = ee::add(t, t);
                v = ee::mul(t2, t2);
            } else {
                v = u32_to_float(age);
            }
            RHighLevelLightManager_AddMissile(lm.hl, pos, type, ee::mul(v, sim.dt));
            ++g_stats.lights;
        }
        if (sim.state != 3) {
            if (d.decaying != 0) RMissileStreak_UpdateDying(d.streak);
            else RMissileStreak_Update(d.streak, pos, right);
        }
    }
    if (weapon == 2 || weapon == 0x1e) return;
    StreakDrawRange r;
    if (!RMissileStreak_Draw(d.streak, r) || !strip) return;
    const MissileStreakTables& t = g_tables;
    strip->kind = r.kind;
    strip->pos.clear();
    strip->stq.clear();
    for (int32_t k = 0; k < r.count; ++k) {
        strip->pos.insert(strip->pos.end(), d.streak.verts[r.first + k], d.streak.verts[r.first + k] + 4);
        strip->stq.insert(strip->stq.end(), t.stq[r.stq_first + k], t.stq[r.stq_first + k] + 4);
    }
    strip->colour.assign(t.colours, t.colours + r.count);
}

}  // namespace drv
