// Rotas dos personagens da IA sobre a rede de navegação (engine/ainetwork.h): o caminho de patrulha
// ou de missão (Drone_tag::route2, NDrone2_AssignAIPath/InitAIPath), o seguir de uma rota
// (NDrone2_FollowRoute, que deixa o próximo ponto em move_target), a ida até um ponto
// (NDrone2_MoveToGoalPosition, pela rota livre route) e a escolha de pontos de IA. Ver drone.h.

#include <cmath>

#include "engine/ainetwork.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/npc.h"
#include "engine/vecmath.h"
#include "engine/view.h"

namespace {

// Aponta move_target para o próximo ponto da rota (sala achada se faltar) e recalcula a distância e a
// direção até ele. É o fim comum de NDrone2_FollowRoute, NDrone2_AssignAIPath e
// NDrone2_ReFindMissionPath.
void aim_at_route_next(DCVars_tag* dcv, AIRoute_tag* route) {
    Drone_tag* d = dcv->drone;
    d->move_target.pos = route->next.pos;
    if (route->next.cel == nullptr) route->next.cel = NDrone2_FindCel(&route->next.pos);
    d->move_target.cel = route->next.cel;
    d->move_arrive = 0.3f;
    _VECTOR feet;
    NDrone2_FeetPos(dcv->obj, &feet);
    d->move_dist = Vec_Dist2D(&feet, &d->move_target.pos);
    NDrone2_GetAnglesToMoveTarget(dcv, &d->move_angles);
}

}  // namespace

// [decomp] 0x00155db8 NDrone2_GetAnglesToMoveTarget
// Só o giro (y) até move_target, no plano.
void NDrone2_GetAnglesToMoveTarget(DCVars_tag* dcv, _VECTOR* out) {
    const _VECTOR& p = dcv->obj->pos;
    const _VECTOR& t = dcv->drone->move_target.pos;
    out->x = 0.0f;
    out->y = ATAN2_APPROX(t.x - p.x, t.z - p.z);
    out->z = 0.0f;
}

// [decomp] 0x0014a3b8 NDrone2_SetIdleTimeOut
// Manda a mensagem 4 ao próprio personagem daqui a base + Rand(variação) segundos.
void NDrone2_SetIdleTimeOut(DCVars_tag* dcv, uint32_t base, uint32_t range) {
    const uint32_t r = Rand_Rand(range);
    dcv->drone->msg_time = GameState.frame + (r + base) * FRAME_RATE_INT;
}

// [decomp] 0x0014ad50 NDrone2_ObstructedPatrolPath
uint32_t NDrone2_ObstructedPatrolPath(DCVars_tag*) { return 0x61; }

// [decomp] 0x00153720 NDrone2_InitAIPath
// Monta route2 andando pelo caminho a partir do nó start: em cada nó de duas ligações segue a que não
// volta ao nó anterior. Patrulhas (flags 4) dão a volta, rotas de missão (flags 8) param numa ponta.
// Peculiaridade mantida: num nó com outro número de ligações (fora da missão) a rota repete o mesmo
// nó até completar node_count entradas.
uint32_t NDrone2_InitAIPath(DCVars_tag* dcv, AIPath_tag* path, NODE_tag* start) {
    obj_tag* obj = dcv->obj;
    Drone_tag* d = dcv->drone;
    AIRoute_tag* r = &d->route2;
    r->flags = static_cast<uint16_t>((r->flags & 0xfff8) | ((path->flags & 4) == 0 ? 1 : 2));
    if ((path->flags & 0x10) != 0) r->flags |= 0x20;
    r->path = path;
    r->count = 0;
    uint16_t prev = 0xffff;
    bool ended = false;
    if (path->node_count != 0) {
        NODE_tag* n = start;
        for (uint32_t i = 0;; ++i) {
            r->list[i] = static_cast<uint16_t>(n->node->index);
            const uint16_t count = ++r->count;
            const uint8_t lc = n->link_count;
            bool follow = false;
            if (lc == 2) {
                follow = true;
            } else if ((path->flags & 8) != 0) {
                if (lc == 1 && count == 1) {
                    follow = true;
                } else {
                    ended = true;  // rota de missão: termina na ponta
                    break;
                }
            }
            if (follow) {
                uint32_t slot = n->first_link;
                const LINK_tag* link = &path->links[path->node_links[slot]];
                bool second = false;
                if (link->a == prev || link->b == prev) {
                    second = true;
                } else if (lc == 2 && (path->flags & 4) != 0 && prev == 0xffff && (path->flags & 0x10) == 0) {
                    // Patrulha no primeiro nó, sem a flag 0x10: começa pela segunda ligação.
                    second = true;
                }
                prev = static_cast<uint16_t>(n->node->index);
                if (second) link = &path->links[path->node_links[slot + 1]];
                n = &path->nodes[link->a == prev ? link->b : link->a];
            }
            if (path->node_count <= i + 1) break;
        }
    }
    (void)ended;
    d->active_route = r;
    r->current = 0;
    NDrone2_FeetPos(obj, &r->from.pos);
    r->from.cel = obj->cel;
    if (r->count < 2) {
        r->to.pos = start->node->pos;
        r->to.cel = start->node->cel;
    } else {
        const MAPNODE_tag* last = path->nodes[r->list[r->count - 1]].node;
        const MAPNODE_tag* before = path->nodes[r->list[r->count - 2]].node;
        _VECTOR v{last->pos.x - before->pos.x, last->pos.y - before->pos.y, last->pos.z - before->pos.z,
                  last->pos.w - before->pos.w};
        Vec_Normalise(&v, &v);
        Vec_MulR32(&v, &v, 0.5f);
        r->to.cel = last->cel;
        r->to.pos = {last->pos.x + v.x, last->pos.y + v.y, last->pos.z + v.z, last->pos.w + v.w};
        r->arrive = 0.5f;
    }
    r->frame = GameState.frame;
    AINetwork_ValidateRoute(r);
    return 1;
}

// [decomp] 0x00154190 NDrone2_AssignAIPath
// Procura, na lista de nós da sala, o nó mais perto (até 0,3 m no plano) de um caminho do tipo pedido
// (flags & mask: 4 patrulha, 8 missão) e monta route2 por ele. Rotas de missão guardam o ponto de IA
// do tipo 8 mais perto do último nó.
uint32_t NDrone2_AssignAIPath(DCVars_tag* dcv, uint32_t mask) {
    obj_tag* obj = dcv->obj;
    dcv->drone->route2.count = 0;
    cel_tag* cel = obj->cel;
    if (cel == nullptr) return 0;
    int32_t best_path = -1;
    NODE_tag* best_node = nullptr;
    float best = 0.3f;
    for (NODE_tag* n = cel->ai_nodes; n != nullptr; n = n->next) {
        if ((AINetwork.paths[n->node->path].flags & mask) == 0) continue;
        const float dist = Vec_Dist2D(&obj->pos, &n->node->pos);
        if (dist < best) {
            best = dist;
            best_path = n->node->path;
            best_node = n;
        }
    }
    if (best_path < 0) return 0;
    AIPath_tag* path = &AINetwork.paths[best_path];
    if (NDrone2_InitAIPath(dcv, path, best_node) == 0) return 0;
    Drone_tag* d = dcv->drone;
    if ((path->flags & 8) != 0 && 2 <= path->node_count) {
        d->route2.current = 0;
        const MAPNODE_tag* last = d->route2.path->nodes[d->route2.list[d->route2.count - 1]].node;
        _VECTOR p = last->pos;
        d->mission_point = NDrone2_ClosestAIPoint(dcv, 8, &p, last->cel);
    }
    if (LinkCreep_Calc(&d->route2, 0) != 0) {
        LinkCreep_Dest(&d->route2, 0, &d->route2.next);
        aim_at_route_next(dcv, &d->route2);
    }
    return 1;
}

// [decomp] 0x00155be0 NDrone2_FollowRoute
// Avança na rota (AINetwork_FollowRoute); 0 sem nós vira 2. move_target fica no próximo ponto.
uint32_t NDrone2_FollowRoute(AIRoute_tag* route, DCVars_tag* dcv) {
    dcv->drone->active_route = route;
    uint32_t r = AINetwork_FollowRoute(route, dcv);
    if (r == 0) r = route->count != 0 ? 0 : 2;
    aim_at_route_next(dcv, route);
    return r;
}

// [decomp] 0x001514d8 NDrone2_UpdatePatrolRoute
// Segue route2 e escolhe a animação: 7 (esperando) parado; senão andando (ou mirando em alerta com a
// propriedade 0x25).
uint32_t NDrone2_UpdatePatrolRoute(DCVars_tag* dcv) {
    uint32_t r = NDrone2_FollowRoute(&dcv->drone->route2, dcv);
    const auto calm = [&] {
        return behaviour_util_getProperty(0x25, dcv->drone->behaviour) == 0 || dcv->drone->alert < 0.66f;
    };
    if (r == 7) {
        DroneAnim_SetStandIdleAnim(dcv, 1);
    } else if (r == 0) {
        if (calm()) {
            DroneAnim_SetWalkIdleAnim(dcv, 1);
        } else {
            DroneAnim_CallAnim(0.0f, 0x35, 0, 0, 0, 0, dcv);
        }
    } else {
        if (calm()) {
            DroneAnim_SetWalkIdleAnim(dcv, 1);
        } else {
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
        }
        r = 1;
    }
    return r;
}

// [decomp] 0x00154a80 NDrone2_UpdateMissionRoute
// Peculiaridade mantida: não devolve nada (os chamadores leem o valor de NDrone2_FollowRoute que fica
// no registrador; aqui ele é devolvido).
uint32_t NDrone2_UpdateMissionRoute(DCVars_tag* dcv) { return NDrone2_FollowRoute(&dcv->drone->route2, dcv); }

// [decomp] 0x00155958 NDrone2_NearestNodeDCV
// O nó da rede de navegação do personagem que ele alcança mais perto (até 25 m), ou -1.
int16_t NDrone2_NearestNodeDCV(DCVars_tag* dcv) {
    CelPos_tag pos{};
    pos.pos = dcv->obj->pos;
    pos.cel = dcv->obj->cel;
    AINodeSearch_tag s{};
    s.pos = &pos;
    s.goal = nullptr;
    s.path = dcv->drone->route.path;
    s.to_node = 1;
    s.open = nullptr;
    s.obj_a = dcv->obj;
    s.obj_b = nullptr;
    s.mask = 0;
    s.radius_sq = 625.0f;
    s.node = -1;
    s.found = 0;
    s.failed = 0;
    AINetwork_NodeSearch(&s);
    if (s.found < 1 || s.node == -1) return -1;
    return s.node;
}

// [decomp] 0x00151b18 NDrone2_ClosestAIPoint
// O ponto de IA ativo do tipo pedido mais perto pela rede (o byte de distância do emissor dele no nó
// do personagem, ou no nó perto de pos). Pontos do mesmo grupo do personagem (AIPoint_tag::param ==
// group_id) ganham dos outros.
AIPoint_tag* NDrone2_ClosestAIPoint(DCVars_tag* dcv, uint16_t kind, _VECTOR* pos, cel_tag* cel) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    const uint32_t group = d->group_id;
    CelPos_tag at{};
    int16_t node;
    if (pos == nullptr) {
        node = NDrone2_NearestNodeDCV(dcv);
        at.pos = obj->pos;
        at.cel = obj->cel;
    } else {
        at.pos = *pos;
        at.cel = cel;
        node = NDrone2_NearestNode(&at, d->route.path, obj);
    }
    if (static_cast<uint16_t>(node) == 0xffff) return nullptr;
    NPCGLOBALS_tag& n = NPCGlobals;
    float best = 255.0f;
    uint32_t best_group = 0;
    AIPoint_tag* result = nullptr;
    for (uint32_t i = 0; i < n.ai_point_count; ++i) {
        AIPoint_tag* p = &n.ai_points[i];
        if (p->active == 0 || p->kind != kind) continue;
        if (p->emitter.path != d->route.path || p->emitter.dist == nullptr) continue;
        const uint8_t b = p->emitter.dist[static_cast<uint16_t>(node)];
        if (b == 0xff) continue;
        const float dist = float(b);
        const bool same_group = p->param != 0 && p->param == group;
        if (best_group == 0) {
            if (!same_group && best <= dist) continue;
            best = dist;
            best_group = same_group ? group : 0;
            result = p;
        } else if (p->param == group && dist < best) {
            best = dist;
            best_group = group;
            result = p;
        }
    }
    return result;
}

// [decomp] 0x001517d8 NDrone2_MoveToGoalPosition
// Vai até pos pela rota livre (route): calcula a rota se ela não vale (guardando de onde saiu) e a
// segue; 3 quando já está a menos de route.arrive, 10 quando a máquina pediu o estado 0x81 (abrir
// porta).
uint32_t NDrone2_MoveToGoalPosition(DCVars_tag* dcv, CelPos_tag* pos, float radius, uint16_t clear) {
    Drone_tag* d = dcv->drone;
    const uint16_t mask = d->unknown_ba0;
    AIRoute_tag* route = &d->route;
    d->active_route = route;
    if (AINetwork_RouteIsValid(route) == 0) {
        const uint32_t r = AINetwork_CalcRouteToPosition(dcv->obj, pos, route, &d->goal_target, &d->goal_point, radius,
                                                         static_cast<uint16_t>(mask & ~clear));
        if (2 < r) return r;
        dcv->drone->goal_from.pos = route->from.pos;
        dcv->drone->goal_from.cel = route->from.cel;
    }
    const float dist = AINetwork_GetRouteDistance(route);
    route->distance = dist;
    if (dist < route->arrive) return 3;
    const uint32_t f = NDrone2_FollowRoute(route, dcv);
    uint32_t r = (f == 3 || f == 0xc) ? f : 0;
    if (dcv->sm->next_state == 0x81) r = 10;
    return r;
}

// [decomp] 0x00153168 NDrone2_FindRunToPoint
// Ponto para onde correr ao ser alertado: o do alarme (tipo 8, propriedade 0x56), o de fuga (1,
// propriedade 0x4e) ou o do lado (0 inimigo, 9 aliado, 2 civil; civis sem ponto 2 tentam o 1). 1 se
// achou um e a rota até ele andou ou chegou.
uint32_t NDrone2_FindRunToPoint(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    uint16_t kind;
    if (behaviour_util_getProperty(0x56, d->behaviour) != 0) {
        kind = 8;
    } else if (behaviour_util_getProperty(0x4e, d->behaviour) != 0) {
        kind = 1;
    } else {
        switch (d->side) {
            case 1: kind = 0; break;
            case 2: kind = 9; break;
            case 3: kind = 2; break;
            default: return 0;
        }
    }
    const auto go = [&](AIPoint_tag* p) -> uint32_t {
        const float radius = p->radius;
        AINetwork_InvalidateRoute(&dcv->drone->route);
        AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &p->pos, radius, 0);
        const uint32_t r = NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0);
        return r < 4 ? 1 : 0;
    };
    if (AIPoint_tag* p = NDrone2_ClosestAIPoint(dcv, kind, nullptr, nullptr)) return go(p);
    if (kind != 2) return 0;
    if (AIPoint_tag* p = NDrone2_ClosestAIPoint(dcv, 1, nullptr, nullptr)) return go(p);
    return 0;
}
