#include "driving/ai/pedestrian.h"

#include <cstring>

#include "driving/ai/road_network.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

PedestrianGlobals g_peds;

// vsub.xyz; vmul.xyz; vmulax.x; vmaddz.x: (p - (x, 0, z)) em x e z ao quadrado.
float dist2_xz(const float p[3], float x, float z) {
    const uint32_t dx = ps2::sub(ps2::fbits(p[0]), ps2::fbits(x)), dz = ps2::sub(ps2::fbits(p[2]), ps2::fbits(z));
    return ps2::ffloat(ps2::madd(ps2::mul(dx, dx), 0x3f800000u, ps2::mul(dz, dz)));
}

}  // namespace

PedestrianGlobals& pedestrian_globals() { return g_peds; }

// [driving] 0x001266a8 AICharacterPedestrian::GetPedBase
int32_t AICharacterPedestrian_GetPedBase(const char* mission) {
    const char* m = mission ? mission : "";
    if (std::strstr(m, "snow1a")) return 1;
    if (std::strstr(m, "snow2a")) return 2;
    if (std::strstr(m, "junglea")) return 3;
    if (std::strstr(m, "jungleb")) return 4;
    if (std::strstr(m, "uw_")) return 5;
    if (std::strstr(m, "junglec")) return 6;
    return 0;
}

// [driving] 0x00125bb8 AICharacterPedestrian::Init(int)
void AICharacterPedestrian_Init(const char* mission, int32_t num_types, bool found) {
    PedestrianGlobals& g = g_peds;
    g.enabled = 0;
    RoadNetworkGlobals& net = road_network();
    if (net.valid == 0) return;
    g.ped_base = AICharacterPedestrian_GetPedBase(mission);
    if (found) g.set_count[g.ped_base] = num_types;
    int32_t roads[32];
    g.num_special = WRoadNetwork_GetSpecialRoads(roads, 32);
    for (int32_t i = 0; i < g.num_special; ++i) {
        PedSpecialRoad& r = g.special[i];
        const WRoadSegment* seg = road_segment(roads[i]);
        const WRoadNode* node = seg ? road_node(seg->node[1]) : nullptr;
        if (!seg || !node) {
            ++g.guards;  // o PS2 leria fora das tabelas da rede
            r = PedSpecialRoad{-1, roads[i], 0, 0, 0, 0};
            continue;
        }
        r.segment = roads[i];
        r.x = node->position[0];
        r.z = node->position[2];
        // A frente x 10 (mul.s) mais o nó (vadd.xyz).
        float ahead[3];
        for (int k = 0; k < 3; ++k) ahead[k] = ee::add(node->position[k], ee::mul(seg->forward[k], 10.0f));
        r.ahead_x = ahead[0];
        r.ahead_z = ahead[2];
        r.ped = -1;
    }
    g.num_peds = (g.set_count[g.ped_base] != 0 || g.set_special[g.ped_base] != 0) ? 16 : 0;
    g.use_last_spawn = 0;
    g.invalidated = -1;
    g.last_spawn_type = 0;
    g.force_special = -1;
    g.voice_delay = 0;
    g.player[0] = g.player[1] = g.player[2] = 0.0f;
    g.chat_allowed = 0;
    // LoadPedTextures (0x001265c0): os personagens dos tipos e dos especiais do conjunto (ActCharacter::LoadCharacter).
    if (g.num_peds > 0) ++g.unported;  // os 16 pedestres: não portados (nenhum em Paris)
    g.force_special = -1;
    g.enabled = 1;
    g.chat_allowed = 1;
}

// [driving] 0x00126818 AICharacterPedestrian::InvalidateFurthestPed
int32_t AICharacterPedestrian_InvalidateFurthestPed(int32_t except) {
    (void)except;
    if (g_peds.num_peds > 0) ++g_peds.unported;  // (os pedestres não existem no port)
    return -1;
}

// [driving] 0x001262f0 AICharacterPedestrian::Manage
void AICharacterPedestrian_Manage(const float player[3]) {
    PedestrianGlobals& g = g_peds;
    if (g.enabled == 0) return;
    ++g.manage_steps;
    if (g.voice_delay != 0) --g.voice_delay;
    else g.voice_delay = 0;
    std::memcpy(g.player, player, 12);
    for (int32_t i = 0; i < g.num_special; ++i) {
        PedSpecialRoad& r = g.special[i];
        if (r.ped == -1) {
            if (g.force_special != -1) continue;
            const float d = dist2_xz(g.player, r.x, r.z);
            if (ee::lt(16875.0f, d) && ee::lt(d, 62500.0f)) {
                AICharacterPedestrian_InvalidateFurthestPed(-1);
                g.force_special = i;
                break;
            }
        } else if (ee::lt(62500.0f, dist2_xz(g.player, r.x, r.z))) {
            ++g.unported;  // KillSelf do pedestre da rua (não há pedestres no port)
            r.ped = -1;
        }
    }
    if (g.num_peds > 0) ++g.unported;  // o laço dos pedestres
}

}  // namespace drv
