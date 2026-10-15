#include "driving/fx/fx_lights.h"

#include <cstring>

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

RLightManagerPositional g_lm;
FxLightStats g_stats;

// 0x00333878: os cinco tipos de explosão (a cor, o K e a intensidade); 0x003338f0: os três de míssil.
const LightTypeEntry kExplosion[5] = {
    {{0.04f, 0.03f, 0.02f, 1.0f}, 1.0f, 45.0f},
    {{0.04f, 0.03f, 0.02f, 1.0f}, 0.8f, 90.0f},
    {{0.04f, 0.03f, 0.02f, 1.0f}, 0.4f, 120.0f},
    {{0.04f, 0.03f, 0.02f, 1.0f}, 0.2f, 180.0f},
    {{0.04f, 0.03f, 0.02f, 1.0f}, 0.1f, 240.0f},
};
const LightTypeEntry kMissile[3] = {
    {{0.0f, 0.0f, 0.1f, 1.0f}, 5.0f, 0.0f},
    {{0.1f, 0.1f, 0.05f, 1.0f}, 5.0f, 0.0f},
    {{0.0f, 0.0f, 0.1f, 1.0f}, 5.0f, 0.0f},
};

}  // namespace

RLightManagerPositional& light_manager_positional() { return g_lm; }
FxLightStats& fx_light_stats() { return g_stats; }
const LightTypeEntry* explosion_light_types() { return kExplosion; }
const LightTypeEntry* missile_light_types() { return kMissile; }

// [driving] 0x001afa88 RHighLevelLightManager::AddExplosion(COORD4 &, float)
void RHighLevelLightManager_AddExplosion(RHighLevelLightManager& m, const float pos[4], float radius, uint32_t time) {
    int32_t type;
    if (ee::lt(800.0f, radius)) type = 4;
    else if (ee::lt(400.0f, radius)) type = 3;
    else if (ee::lt(200.0f, radius)) type = 2;
    else type = ee::lt(100.0f, radius) ? 1 : 0;
    // A vaga: a de menor fTime (sem sinal), parando na primeira livre (0).
    uint32_t best = 0xffffffffu;
    int32_t slot = 0;
    for (int32_t i = 0; i < 4; ++i) {
        if (m.expl_time[i] < best) {
            best = m.expl_time[i];
            slot = i;
        }
        if (best == 0) break;
    }
    if (best == 0xffffffffu) return;
    std::memcpy(m.expl_pos[slot], pos, 16);
    m.expl_type[slot] = type;
    m.expl_time[slot] = time;
    ++g_stats.explosions;
}

// [driving] 0x001afba8 RHighLevelLightManager::AddMissile(COORD4 &, int, float)
void RHighLevelLightManager_AddMissile(RHighLevelLightManager& m, const float pos[4], int32_t type, float value) {
    uint32_t slot;
    if (m.missile_count < 2) {
        slot = uint32_t(m.missile_count);
        ++m.missile_count;
    } else {
        // As duas cheias: a de valor maior (maior que 0); nenhuma: 2, nada.
        float best = 0.0f;
        slot = 2;
        for (uint32_t i = 0; i < 2; ++i)
            if (ee::lt(best, m.missile_value[i])) {
                best = m.missile_value[i];
                slot = i;
            }
    }
    if (slot >= 2) {
        ++g_stats.refused;
        return;
    }
    std::memcpy(m.missile_pos[slot], pos, 16);
    m.missile_type[slot] = type;
    m.missile_value[slot] = value;
    ++g_stats.missiles;
}

// [driving] 0x001afc58 RHighLevelLightManager::AddPositionalLight(COORD4 &, COORD4 &, int)
void RHighLevelLightManager_AddPositionalLight(RHighLevelLightManager& m, const float pos[4], const float colour[3],
                                               int32_t priority) {
    if (m.count < 4) {
        HighLevelLight& l = m.lights[m.count];
        std::memcpy(l.pos, pos, 16);
        std::memcpy(l.colour, colour, 12);
        l.priority = priority;
        ++m.count;
        return;
    }
    // Cheio: a primeira de prioridade menor.
    for (int32_t i = 0; i < 4; ++i) {
        if (m.lights[i].priority < priority) {
            std::memcpy(m.lights[i].pos, pos, 16);
            std::memcpy(m.lights[i].colour, colour, 12);
            m.lights[i].priority = priority;
            return;
        }
    }
}

// [driving] 0x001af520 RLightManager::AddPositionalLight(COORD4 &, COORD4 &)
void RLightManager_AddPositionalLight(RLightManagerPositional& lm, const float pos[4], const float colour[3]) {
    const int32_t n = lm.count;
    if (n >= 4) return;
    PositionalLightBlock& b = lm.building;
    if (lm.colorize_mode == 1) {
        b.col[n][0] = 0.0f;
        b.col[n][1] = colour[1];
        b.col[n][2] = ee::mul(colour[2], 0.5f);
    } else {
        b.col[n][0] = colour[0];
        b.col[n][1] = colour[1];
        b.col[n][2] = colour[2];
    }
    for (int k = 0; k < 4; ++k) b.pos[k][n] = pos[k];
    lm.count = n + 1;
    ++g_stats.positional;
}

// [driving] 0x001afd50 RHighLevelLightManager::Process
void RHighLevelLightManager_Process(RLightManagerPositional& lm, uint32_t time) {
    RHighLevelLightManager& m = lm.hl;
    for (int32_t i = 0; i < m.count; ++i) RLightManager_AddPositionalLight(lm, m.lights[i].pos, m.lights[i].colour);
    // As explosões: o fator 1 / (idade x 0,5) - 0,02 (0 com a idade 0); sem fator, a vaga fica livre.
    for (int32_t i = 0; i < 4; ++i) {
        if (m.expl_time[i] == 0) continue;
        float f = 0.0f;
        const int32_t age = int32_t(time - m.expl_time[i]);
        if (age > 0) f = ee::sub(ee::div(1.0f, ee::mul(ee::cvt_s(age), 0.5f)), ps2::ffloat(0x3ca3d70a));
        if (ee::le(f, 0.0f)) {
            m.expl_time[i] = 0;
            continue;
        }
        const LightTypeEntry& e = kExplosion[m.expl_type[i]];
        const float s = ee::min(ee::mul(e.intensity, f), 16.0f);
        float c[4];
        std::memcpy(c, e.colour, 16);
        for (int k = 0; k < 3; ++k) c[k] = ee::mul(c[k], s);  // vmulx.xyz
        m.expl_pos[i][3] = e.k;
        RLightManager_AddPositionalLight(lm, m.expl_pos[i], c);
    }
    // Os mísseis: o K do tipo vezes max(valor, 1).
    for (int32_t i = 0; i < m.missile_count; ++i) {
        const float v = ee::lt(1.0f, m.missile_value[i]) ? m.missile_value[i] : 1.0f;
        const LightTypeEntry& e = kMissile[m.missile_type[i]];
        m.missile_pos[i][3] = ee::mul(e.k, v);
        RLightManager_AddPositionalLight(lm, m.missile_pos[i], e.colour);
    }
}

// [driving] 0x001aede8 RLightManager::FinishLights
void RLightManager_FinishLights(RLightManagerPositional& lm, uint32_t time) {
    RHighLevelLightManager_Process(lm, time);
    lm.hl.count = 0;          // +0x360
    lm.hl.missile_count = 0;  // +0x3c4
    lm.published = lm.building;
    std::memset(&lm.building, 0, sizeof lm.building);
    // [driving] 0x001aedc8 RLightManager::CopyPositionalLightsToPlatformSpecific: vazio no PS2.
    lm.last_count = lm.count;
    lm.count = 0;
    lm.unknown60 = 0;
    ++g_stats.finishes;
    g_stats.published_lights += uint64_t(lm.last_count);
}

void RLightManager_ResetPositional(RLightManagerPositional& lm) {
    lm = RLightManagerPositional();
    g_stats = FxLightStats();
}

}  // namespace drv
