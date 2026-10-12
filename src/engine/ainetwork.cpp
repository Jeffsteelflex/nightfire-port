// Consultas e rotas da rede de navegação da IA (AINetwork_*, LinkCreep_*) e os testes de movimento
// do NDrone2 que elas usam. Ver ainetwork.h (estruturas, peculiaridades preservadas e divergências).
//
// Divergências do port, além das do cabeçalho: AINetwork_UpdateLinksUsedCount não soma o uso da
// "ligação 0xffff" quando dois nós seguidos da rota não têm ligação (o original escreveria além da
// tabela); LinkCreep_AtSpecialNode com a posição -1 na rota e LinkCreep_Handler sem DCVars_tag (o
// original leria antes da lista e desreferenciaria nulo) devolvem sem fazer nada. Contados em
// g_ainetwork_guards.

#include "engine/ainetwork.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "engine/collide.h"
#include "engine/door.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"

namespace {

_VECTOR vsub(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR vadd(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

bool mp_any_target() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

NODE_tag* route_node(const AIRoute_tag* route, int32_t i) { return &route->path->nodes[route->list[i]]; }

// Ligação (índice) entre o nó a e o nó de índice b, procurada na lista do nó a; 0xffff sem ligação.
uint32_t link_between(const AIPath_tag* path, uint16_t a, int16_t b) {
    if (a == 0xffff || b == -1) return 0xffff;
    const NODE_tag& n = path->nodes[a];
    const uint16_t* list = path->node_links + n.first_link;
    for (int32_t k = 0; k < int32_t(n.link_count); ++k) {
        const LINK_tag& l = path->links[list[k]];
        if (int16_t(l.a) == b || int16_t(l.b) == b) return list[k];
    }
    return 0xffff;
}

// Vizinho de n pela ligação s da lista dele.
uint32_t neighbour(const AIPath_tag* path, const NODE_tag* n, int32_t s) {
    const LINK_tag& l = path->links[path->node_links[n->first_link + s]];
    return l.a == uint16_t(n->node->index) ? l.b : l.a;
}

// Ligação s da lista de n.
LINK_tag& slot_link(const AIPath_tag* path, const NODE_tag* n, int32_t s) {
    return path->links[path->node_links[n->first_link + s]];
}

// Dá para passar de `from` para `to`? Só não quando os dois são nós de porta e a porta de `from`
// está trancada.
bool door_passable(const NODE_tag* from, const NODE_tag* to) {
    if (from == nullptr || to == nullptr || from->node == nullptr || to->node == nullptr || from->door == nullptr) {
        return true;
    }
    if ((from->node->flags & kNodeDoor) == 0 || (to->node->flags & kNodeDoor) == 0) return true;
    return !Door_IsLocked(from->door);
}

// Inserção na lista aberta (cabeça falsa `head`, ligada por list_next) em ordem de f crescente,
// depois dos iguais.
void open_insert(NODE_tag* head, NODE_tag* n) {
    NODE_tag* prev = head;
    NODE_tag* cur = head->list_next;
    while (cur != nullptr && !(n->f < cur->f)) {
        prev = cur;
        cur = cur->list_next;
    }
    prev->list_next = n;
    n->list_next = cur;
}

NODE_tag* open_pop(NODE_tag* head) {
    NODE_tag* n = head->list_next;
    head->list_next = n->list_next;
    n->list_next = nullptr;
    n->node->flags &= ~kNodeOpen;
    return n;
}

// Tira n da lista fechada (só roda com o bit 0x80000000, que ninguém põe; ver ainetwork.h).
void closed_remove(NODE_tag* head, NODE_tag* n) {
    NODE_tag* prev = head;
    for (NODE_tag* c = head->list_next; c != nullptr; c = c->list_next) {
        if (c == n) {
            prev->list_next = n->list_next;
            n->list_next = nullptr;
            n->node->flags &= 0x7fffffff;
        }
        prev = c;
    }
}

// Depois de NodeSearchCel sem resultado: repete a busca para cada portal da sala cuja faixa de
// altura (+-2) contém a posição. A sala do teste não muda (ver ainetwork.h).
void search_portals(AINodeSearch_tag* s) {
    if (s->found != 0 || s->pos->cel == nullptr) return;
    for (portal_tag* p = s->pos->cel->portals; p != nullptr; p = p->next) {
        if (p->cel == nullptr) continue;
        float lo = std::fmin(p->verts[0].y, p->verts[1].y);
        lo = std::fmin(lo, p->verts[2].y);
        lo = std::fmin(lo, p->verts[3].y);
        float hi = std::fmax(p->verts[0].y, p->verts[1].y);
        hi = std::fmax(hi, p->verts[2].y);
        hi = std::fmax(hi, p->verts[3].y);
        const float y = s->pos->pos.y;
        if (lo - 2.0f < y && y < hi + 2.0f) AINetwork_NodeSearchCel(s);
        if (s->found != 0) return;
    }
}

void reset_route_fields(AIRoute_tag* route) {
    route->flags &= ~kRouteAltered;
    route->status = kRouteReset;
    route->goal = 0xffff;
    route->current = -1;
    route->creeping = 0;
    route->unknown_05 = 0;
    route->frame = 0;
    route->start = 0xffff;
    route->count = 0;
    route->distance = 0.0f;
}

void fill_goal(AITarget_tag* goal, AIPoint_tag* point) {
    goal->mask = 0x10000000;
    goal->obj = nullptr;
    goal->pos.pos = point->pos.pos;
    goal->frame = 0;
    goal->pos.cel = point->pos.cel;
    AINetwork_NodesForPosition(goal, &goal->pos, nullptr, 0.0f);
}

uint8_t* dcv_drone(DCVars_tag* dcv) {
    uint8_t* drone;
    std::memcpy(&drone, reinterpret_cast<uint8_t*>(dcv) + 4, 4);
    return drone;
}

uint32_t u32_at(const uint8_t* p, int off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

}  // namespace

// ---- rotas: distâncias ----

// [decomp] 0x00156088 AINetwork_AlterDestFor_DROUTE_Nearest
// Rota aproximada (o alvo não é alcançável pela rede): se o caminho em linha reta do último nó ao
// destino cruza um limite, o destino passa a ser o ponto do limite recuado 1 m.
bool AINetwork_AlterDestFor_DROUTE_Nearest(AIRoute_tag* route) {
    MAPNODE_tag* last = route_node(route, route->count - 1)->node;
    _VECTOR hit{};
    const int8_t r = NDrone2_MoveTestFromNode(last, &route->to, nullptr, nullptr, &hit, 0, 1);
    if (r == -1) {
        _VECTOR d = vsub(hit, last->pos);
        Vec_Normalise(&d, &d);
        route->to.pos = vsub(hit, d);
        route->to.cel = NDrone2_FindCel(&route->to.pos);
        route->flags |= kRouteAltered;
    }
    return r == -1;
}

// [decomp] 0x00156160 AINetwork_GetRouteDistanceBetweenNodes
// Soma das ligações entre as posições a e b da rota (negativa se b vem antes de a).
float AINetwork_GetRouteDistanceBetweenNodes(AIRoute_tag* route, int16_t a, int16_t b) {
    if (route->count == 0) return 0.0f;
    const AIPath_tag* path = route->path;
    const int32_t last = route->count - 1;
    int32_t ia = a, ib = b;
    if (path == nullptr || ia > last || ib > last || ia == ib) return 0.0f;
    if (ib < 0) ib = 0;
    if (ia < 0) ia = 0;
    int32_t lo = ia, hi = ib;
    if (ib <= ia) {
        lo = ib;
        hi = ia;
    }
    float total = 0.0f;
    const NODE_tag* prev = route_node(route, lo);
    for (;;) {
        lo = int16_t(lo + 1);
        if (hi < lo) return ib > ia ? total : -total;
        const NODE_tag* cur = route_node(route, lo);
        const uint32_t l = link_between(path, uint16_t(prev->node->index), cur->node->index);
        prev = cur;
        if (l != 0xffff) total += path->links[l].length;
    }
}

namespace {

float route_distance(AIRoute_tag* route, float (*dist)(const _VECTOR*, const _VECTOR*)) {
    _VECTOR feet;
    NDrone2_FeetPos(route->owner, &feet);
    const uint32_t count = route->count;
    if (count != 0 && int32_t(route->current) < int32_t(count) && route->path != nullptr) {
        int32_t i = int16_t(route->current + 1);
        if (i < 0) i = 0;
        if (i <= int32_t(count) - 1) {
            const AIPath_tag* path = route->path;
            const NODE_tag* prev = route_node(route, i);
            float total = dist(&feet, &prev->node->pos);
            for (uint32_t j = uint32_t(i) + 1; j < count; ++j) {
                const NODE_tag* cur = route_node(route, int32_t(j));
                const uint32_t l = link_between(path, uint16_t(prev->node->index), cur->node->index);
                prev = cur;
                if (l != 0xffff) total += path->links[l].length;
            }
            const float end = dist(&route->to.pos, &route_node(route, int32_t(count) - 1)->node->pos);
            route->distance = total + end;
            return total + end;
        }
    }
    return dist(&feet, &route->to.pos);
}

}  // namespace

// [decomp] 0x00156320 AINetwork_GetRouteDistance
// Distância (2D) que falta: dos pés do drone ao próximo nó, as ligações seguintes e do último nó ao
// destino. Guarda em route->distance.
float AINetwork_GetRouteDistance(AIRoute_tag* route) { return route_distance(route, Vec_Dist2D); }

// [decomp] 0x00156540 AINetwork_GetRouteDistance3D
float AINetwork_GetRouteDistance3D(AIRoute_tag* route) { return route_distance(route, Vec_Dist3D); }

namespace {

// Pode pular do nó a direto para o nó b? Não se os dois forem de porta, nem se os dois forem de
// chute (índices fora do caminho valem flags 0).
bool skippable(const AIPath_tag* path, int16_t a, int16_t b) {
    if (a < 0 || a >= int16_t(path->node_count)) return false;
    const MAPNODE_tag* na = path->nodes[a].node;
    const MAPNODE_tag* nb = (b >= 0 && b < int16_t(path->node_count)) ? path->nodes[b].node : nullptr;
    const uint32_t fa = na != nullptr ? na->flags : 0;
    const uint32_t fb = nb != nullptr ? nb->flags : 0;
    if ((fa & kNodeDoor) != 0 && (fb & kNodeDoor) != 0) return false;
    return (fa & kNodeKick) == 0 || (fb & kNodeKick) == 0;
}

}  // namespace

// [decomp] 0x00156760 AINetwork_OptimiseRoute
// Tira o primeiro nó (start) se o drone alcança o segundo direto, e o último (end) se o destino é
// alcançável do penúltimo; recalcula route->distance. A soma usa o comprimento da ligação de índice
// parent_link de cada nó (posição na lista do pai, não o índice da ligação), como no original.
uint32_t AINetwork_OptimiseRoute(AIRoute_tag* route, uint32_t status, int8_t start, int8_t end) {
    AIPath_tag* path = route->path;
    int32_t skip = 0;
    if (start != 0 && route->count > 1) {
        const int16_t n0 = int16_t(route->list[0]);
        const int16_t n1 = int16_t(route->list[1]);
        if (skippable(path, n0, n1) &&
            NDrone2_MoveTestToNode(&route->from, path->nodes[route->list[1]].node, route->target_obj, route->owner,
                                   nullptr, 1, 1) == 1) {
            skip = 1;
            route->start = route->list[1];
        }
    }
    if (end != 0 && uint32_t(skip) + 1 < route->count) {
        const uint32_t n = route->count;
        const int16_t last = int16_t(route->list[n - 1]);
        const int16_t prev = int16_t(route->list[n - 2]);
        if (skippable(path, last, prev) &&
            NDrone2_MoveTestToNode(&route->to, path->nodes[route->list[n - 2]].node, route->target_obj, route->owner,
                                   nullptr, 1, 1) == 1) {
            route->goal = route->list[route->count - 2];
            --route->count;
        }
    }
    if (skip != 0) {
        route->count = uint16_t(route->count - skip);
        for (int32_t i = 0; i < int32_t(route->count); ++i) route->list[i] = route->list[i + skip];
    }
    float dist = Vec_Dist2D(&route->from.pos, &path->nodes[route->list[0]].node->pos);
    for (int32_t i = 1; i < int32_t(route->count); ++i) {
        dist += path->links[path->nodes[route->list[i]].parent_link].length;
    }
    dist += Vec_Dist2D(&route->to.pos, &path->nodes[route->list[route->count - 1]].node->pos);
    route->distance = dist;
    return status;
}

// [decomp] 0x00156ba8 AINetwork_CalcRoute
// Calcula a rota de route->from (os pés do dono) até route->to: em linha reta se der (status 2) ou
// pelo A* da rede (0 exata, 1 aproximada), otimizada e já com o primeiro trecho de LinkCreep.
uint32_t AINetwork_CalcRoute(AIRoute_tag* route, obj_tag* owner, obj_tag* target, uint16_t param) {
    reset_route_fields(route);
    route->owner = owner;
    route->target_obj = target;
    if (route->target == nullptr) return kRouteNoNodes;
    NDrone2_FeetPos(owner, &route->from.pos);
    route->from.cel = owner->cel;
    route->frame = route->target->frame;
    if ((route->flags & kRouteNoDirect) == 0 &&
        NDrone2_MoveTest(&route->from, &route->to, target, owner, nullptr, 1, 1) == 1) {
        reset_route_fields(route);
        route->status = kRouteDirect;
        route->frame = route->target->frame;
        LinkCreep_CalcToRouteEnd(route);
        route->flags |= kRouteValid;
        return route->status;
    }
    if (route->path == nullptr || route->path->node_count == 0) {
        route->status = kRouteNoNodes;
        return kRouteNoNodes;
    }
    uint32_t r = AINetwork_DoAStarPath(route, target, owner, route->target->mask, param);
    route->status = uint8_t(r);
    if (r < 2) {
        r = AINetwork_OptimiseRoute(route, r, 1, 1);
        route->flags |= kRouteValid;
        LinkCreep_Calc(route, 0);
    }
    return r;
}

// ---- busca de nós ----

// [decomp] 0x00156e18 AINetwork_NodeSearchCel
// Nós do caminho na sala da posição, dentro de uma caixa de +-raio (15 m sem raio) e +-2 m de
// altura, ordenados pela distância 2D. Modo to_node: o primeiro que a posição alcança vira o nó
// achado (g = distância, h = distância ao goal) e entra na lista aberta; se o teste cruzar um limite
// (-1), marca mask << 1. Modo nó -> posição: o primeiro que alcança a posição ganha mask; o mais
// perto que não alcança ganha mask << 1. Para no primeiro achado.
uint32_t AINetwork_NodeSearchCel(AINodeSearch_tag* s) {
    CelPos_tag* pos = s->pos;
    if (pos->cel == nullptr || s->path == nullptr) return 0;
    const float half = s->radius_sq != 0.0f ? std::sqrt(s->radius_sq) : 15.0f;
    const _VECTOR lo{pos->pos.x - half, pos->pos.y - 2.0f, pos->pos.z - half, 0.0f};
    const _VECTOR hi{pos->pos.x + half, pos->pos.y + 2.0f, pos->pos.z + half, 0.0f};
    NODE_tag head{};  // cabeça falsa da lista ordenada (sorted_next)
    for (NODE_tag* n = pos->cel->ai_nodes; n != nullptr; n = n->next) {
        if (n->node->path != s->path->index) continue;
        if (Intersect_SphereBox(&n->node->pos, 0.0f, &lo, &hi) == 0) continue;
        const float d = Vec_Dist2D(&n->node->pos, &pos->pos);
        n->dist = d;
        // Compara a distância (não ao quadrado) com radius_sq, como no original.
        if (!(s->radius_sq <= 0.0f) && !(d <= s->radius_sq)) continue;
        NODE_tag* prev = &head;
        NODE_tag* cur = head.sorted_next;
        while (cur != nullptr && !(d < cur->dist)) {
            prev = cur;
            cur = cur->sorted_next;
        }
        prev->sorted_next = n;
        n->sorted_next = cur;
    }
    for (NODE_tag* n = head.sorted_next; n != nullptr; n = n->sorted_next) {
        if (s->to_node != 0) {
            const int8_t r = NDrone2_MoveTestToNode(pos, n->node, s->obj_a, s->obj_b, nullptr, 0, 1);
            if (r == 1) {
                n->g = Vec_Dist2D(&pos->pos, &n->node->pos);
                n->h = s->goal != nullptr ? Vec_Dist2D(&n->node->pos, s->goal) : 0.0f;
                n->list_next = nullptr;
                n->f = n->g + n->h;
                n->parent = nullptr;
                s->node = n->node->index;
                if (s->open != nullptr) {
                    open_insert(s->open, n);
                    n->node->flags |= kNodeOpen;
                }
                ++s->found;
                return 1;
            }
            if (r == -1) {
                n->node->flags |= s->mask << 1;
                ++s->failed;
                return 1;
            }
        } else {
            if (NDrone2_MoveTestFromNode(n->node, pos, s->obj_a, s->obj_b, nullptr, 0, 1) == 1) {
                n->h = n->dist;
                n->node->flags |= s->mask;
                ++s->found;
                return 1;
            }
            if (s->failed == 0) {
                n->h = n->dist;
                n->node->flags |= s->mask << 1;
                ++s->failed;
            }
        }
    }
    return 1;
}

// [decomp] 0x001571b0 AINetwork_DoAStarPath
// A* do nó alcançável mais perto de route->from até um nó com o bit `mask` (posto por
// AINetwork_NodesForPosition no alvo). Sem nó achado no alvo, procura os vistos mas inalcançáveis
// (mask << 1) e devolve 1 (aproximada). Custo de uma ligação: comprimento * (usos + 1); h: distância
// 2D até route->to. Portas trancadas cortam a passagem entre nós de porta. Grava a lista de nós da
// rota (route->list), start, goal e distance.
uint32_t AINetwork_DoAStarPath(AIRoute_tag* route, obj_tag* a, obj_tag* b, uint32_t mask, uint16_t) {
    AIPath_tag* path = route->path;
    for (int32_t i = 0; i < int32_t(path->node_count); ++i) {
        path->nodes[i].node->flags &= 0x3fffffff;
        path->nodes[i].g = 0.0f;
    }
    AITarget_tag* target = route->target;
    if (target->frame == 0) return kRouteNoTarget;
    uint32_t result = kRouteExact;
    if (!(target->found > 0 && (path->found & target->mask) != 0)) {
        mask <<= 1;
        if (target->failed <= 0) return kRouteNoTarget;
        result = kRouteNearest;
    }
    NODE_tag open{};
    NODE_tag closed{};
    _VECTOR* goal = &route->to.pos;
    AINodeSearch_tag s{};
    s.pos = &route->from;
    s.goal = goal;
    s.path = path;
    s.to_node = 1;
    s.open = &open;
    s.obj_a = a;
    s.obj_b = b;
    s.mask = 0;
    s.radius_sq = 625.0f;
    s.failed = 0;
    s.found = 0;
    AINetwork_NodeSearchCel(&s);
    search_portals(&s);
    if (s.found < 1) return kRouteNoStart;
    while (open.list_next != nullptr) {
        NODE_tag* n = open_pop(&open);
        if ((n->node->flags & mask) != 0) {
            route->goal = uint16_t(n->node->index);
            route->distance = n->g + Vec_Dist2D(&n->node->pos, goal);
            uint32_t len = 1;
            NODE_tag* root = n;
            while (root->parent != nullptr) {
                root = root->parent;
                ++len;
            }
            route->prev_start = route->start;
            route->start = uint16_t(root->node->index);
            NODE_tag* c = n;
            for (int32_t i = int32_t(len) - 1; i >= 0; --i) {
                route->list[i] = uint16_t(c->node->index);
                c = c->parent;
            }
            route->count = uint16_t(len);
            if (uint16_t(len) != 0) route->current = 0;
            return result;
        }
        for (int32_t s2 = 0; s2 < int32_t(n->link_count); ++s2) {
            NODE_tag* m = &path->nodes[neighbour(path, n, s2)];
            if (!door_passable(n, m)) continue;
            const LINK_tag& l = slot_link(path, n, s2);
            const float g = n->g + l.length * float(l.used + 1);
            if ((m->node->flags & (kNodeOpen | kNodeClosed)) != 0 && !(g < m->g)) continue;
            m->parent = n;
            m->g = g;
            m->h = Vec_Dist2D(&m->node->pos, goal);
            m->parent_link = uint8_t(s2);
            m->f = m->g + m->h;
            if ((m->node->flags & kNodeClosed) != 0) closed_remove(&closed, m);
            if ((m->node->flags & kNodeOpen) == 0) {
                open_insert(&open, m);
                m->node->flags |= kNodeOpen;
            }
        }
        n->list_next = closed.list_next;
        closed.list_next = n;
        n->node->flags |= kNodeOpen;  // (sic) o bit de aberto, não o de fechado
    }
    return kRouteNoPath;
}

// ---- limites ----

// [decomp] 0x00157780 AINetwork_HitBoundry
// Ponto do segmento de limite mais perto de p (closest), distância 2D ao quadrado (dist_sq) e se p
// toca o limite: dentro do raio, abaixo do topo (altura interpolada) e no máximo 0,4 abaixo dele.
uint32_t AINetwork_HitBoundry(_VECTOR* closest, const BLINK_tag* link, const _VECTOR* p, float radius_sq,
                              float* dist_sq, uint32_t skip) {
    if ((link->flags & skip) != 0) return 0;
    const AIBounds_tag& b = AINetwork.bounds[uint16_t(link->bounds)];
    const BMAPNODE_tag* na = b.nodes[link->a].node;
    const BMAPNODE_tag* nb = b.nodes[link->b].node;
    const _VECTOR d = vsub(nb->pos, na->pos);
    const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
    float t = 0.0f;
    if (1e-20f < std::fabs(dd)) {
        const _VECTOR v = vsub(*p, na->pos);
        t = (d.x * v.x + d.y * v.y + d.z * v.z) / dd;
        if (1.0f < t) {
            *closest = nb->pos;
            t = 1.0f;
        } else if (t < 0.0f) {
            *closest = na->pos;
            t = 0.0f;
        } else {
            // w: o original soma a w do registro da VU0 (lixo); aqui 0.
            *closest = {na->pos.x + d.x * t, na->pos.y + d.y * t, na->pos.z + d.z * t, na->pos.w};
        }
    } else {
        *closest = na->pos;
    }
    *dist_sq = Vec_SqDist2D(p, closest);
    if (radius_sq < *dist_sq) return 0;
    if (closest->y + ((nb->height - na->height) * t + na->height) < p->y) return 0;
    if (p->y < closest->y - 0.4f) return 0;
    return 1;
}

// [decomp] 0x00157990 AINetwork_LineIntersectsBoundry
// A linha from-to cruza o segmento de limite (em planta) dentro da faixa de altura dele (do pé - 2,4
// ao topo)? Com best, só conta se o ponto estiver mais perto de from que *best (e atualiza *best);
// out recebe o ponto (y do nó a do limite com best, senão a altura do limite no ponto). Com adjust e
// a linha mais de 2 m acima do pé do limite, a altura das duas pontas vira a altura da linha - 0,5.
uint32_t AINetwork_LineIntersectsBoundry(const BLINK_tag* link, const _VECTOR* from, const _VECTOR* to, _VECTOR* out,
                                         float* best, int8_t adjust) {
    const AIBounds_tag& b = AINetwork.bounds[uint16_t(link->bounds)];
    BMAPNODE_tag* na = b.nodes[link->a].node;
    BMAPNODE_tag* nb = b.nodes[link->b].node;
    // Port: no caso colinear (retorno 1) o original não escreve o ponto e usa lixo da pilha.
    _VECTOR ix{0.0f, 0.0f, 0.0f, 0.0f};
    if (vecutil_intersect_line_line_2d(from, to, &na->pos, &nb->pos, &ix) == 0) return 0;
    float t = Vec_Dist2D(from, &ix) / Vec_Dist2D(from, to);
    const float line_y = (to->y - from->y) * t + from->y;
    t = Vec_Dist2D(&na->pos, &ix) / Vec_Dist2D(&na->pos, &nb->pos);
    const float base_y = (nb->pos.y - na->pos.y) * t + na->pos.y;
    ix.y = base_y;
    if (base_y + ((nb->height - na->height) * t + na->height) < line_y) return 0;
    if (line_y < base_y - 2.4f) return 0;
    if (adjust != 0 && 2.0f < line_y - base_y) {
        nb->height = line_y - 0.5f;
        na->height = line_y - 0.5f;
    }
    if (best != nullptr) {
        ix.y = from->y;
        const _VECTOR d = vsub(ix, *from);
        const float len = Vec_Magnitude(&d);
        if (!(len < *best)) return 1;
        *best = len;
        ix.y = na->pos.y;
    }
    if (out != nullptr) *out = ix;
    return 1;
}

// [decomp] 0x00157bd0 AINetwork_GetBoundsPushVectorForSphere
// Empurrão para tirar uma esfera dos limites da sala (raio dobrado nos limites de porta, salvo com
// 0x800 em skip). Devolve se algum limite tocou.
uint32_t AINetwork_GetBoundsPushVectorForSphere(const _VECTOR* pos, const cel_tag* cel, float radius, _VECTOR* push,
                                                uint32_t skip) {
    uint32_t hit = 0;
    _VECTOR p = *pos;
    BLINK_tag* l = cel->ai_blinks;
    while (l != nullptr) {
        const AIBounds_tag& b = AINetwork.bounds[uint16_t(l->bounds)];
        const BMAPNODE_tag* na = b.nodes[l->a].node;
        const BMAPNODE_tag* nb = b.nodes[l->b].node;
        float r = radius;
        float r2 = radius * radius;
        if ((skip & kBoundDoor) == 0 && (l->flags & kBoundDoor) != 0) {
            r = radius + radius;
            r2 = r * r;
        }
        _VECTOR closest;
        float dist_sq;
        if (AINetwork_HitBoundry(&closest, l, &p, r2, &dist_sq, skip) != 0) {
            hit = 1;
            _VECTOR d = vsub(p, closest);
            d.y = 0.0f;
            const float len = Vec_NormaliseLen(&d, &d);
            const float amount = r - len;
            // w: o original soma a w de uma posição não escrita da pilha; aqui 0.
            p = {p.x + d.x * amount, p.y + d.y * amount, p.z + d.z * amount, p.w};
        }
        if (na->cel == cel) {
            l = l->next_a;
        } else if (nb->cel == cel) {
            l = l->next_b;
        } else {
            l = nullptr;
        }
    }
    *push = vsub(p, *pos);
    return hit;
}

namespace {

// Salas que um raio de from a to atravessa (Collide_StraddleCels), começando por from_cel.
cel_tag** ray_cels(const _VECTOR& from, const _VECTOR& to, const cel_tag* from_cel, uint16_t* count) {
    HITTEST_tag test{};
    test.start = from;
    test.end = to;
    test.radius = Vec_Dist3D(&from, &to);
    test.type = 0x201;
    test.cel = const_cast<cel_tag*>(from_cel);
    *count = 1;
    return Collide_StraddleCels(&test, count);
}

bool cel_in(cel_tag* const* list, uint16_t count, const cel_tag* cel) {
    for (uint16_t i = 0; i < count; ++i) {
        if (list[i] == cel) return true;
    }
    return false;
}

// Próximo segmento na lista da sala `cel`.
const BLINK_tag* next_in_cel(const BLINK_tag* l, const BMAPNODE_tag* na, const BMAPNODE_tag* nb, const cel_tag* cel) {
    if (na->cel == cel) return l->next_a;
    if (nb->cel == cel) return l->next_b;
    return nullptr;
}

}  // namespace

// [decomp] 0x00157d80 AINetwork_BoundsTest
// O caminho em linha reta de from a to (1 m acima do chão) passa pelas salas até to_cel sem cruzar
// um limite? Sem out: 0 no primeiro cruzamento; com out: -1 e out recebe o cruzamento mais perto.
// Com near_nodes (e sem out), também falha se a linha passa a menos de 0,4 m de uma ponta de limite
// que não esteja a menos de 0,4 m de from ou de to.
int32_t AINetwork_BoundsTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                             _VECTOR* out, int8_t near_nodes, int8_t*) {
    float best = 100000.0f;
    _VECTOR start = *from, end = *to;
    start.y += 1.0f;
    end.y += 1.0f;
    HITTEST_tag test{};
    test.start = start;
    test.end = end;
    test.radius = Vec_Dist3D(from, to);
    test.type = 0x201;
    test.cel = const_cast<cel_tag*>(from_cel);
    uint16_t count = 1;
    if (from_cel == nullptr) return 0;
    cel_tag** cels = Collide_StraddleCels(&test, &count);
    if (!cel_in(cels, count, to_cel)) return 0;
    float* best_ptr = out != nullptr ? &best : nullptr;
    int32_t result = 1;
    for (uint16_t i = 0; i < count; ++i) {
        const cel_tag* cel = cels[i];
        for (const BLINK_tag* l = cel->ai_blinks; l != nullptr;) {
            const AIBounds_tag& b = AINetwork.bounds[uint16_t(l->bounds)];
            const BMAPNODE_tag* na = b.nodes[l->a].node;
            const BMAPNODE_tag* nb = b.nodes[l->b].node;
            if (AINetwork_LineIntersectsBoundry(l, from, to, out, best_ptr, 0) != 0) {
                if (out == nullptr) return 0;
                result = -1;
            }
            if (near_nodes != 0 && out == nullptr && 0.16000001f < Vec_SqDist2D(&na->pos, to) &&
                0.16000001f < Vec_SqDist2D(&nb->pos, to) && 0.16000001f < Vec_SqDist2D(&na->pos, from) &&
                0.16000001f < Vec_SqDist2D(&nb->pos, from)) {
                _VECTOR c;
                vecutil_calculate_closest_point_on_line(&c, from, to, &na->pos);
                if (Vec_SqDist2D(&c, &na->pos) < 0.16000001f && std::fabs(c.y - na->pos.y) < 2.0f) return 0;
                vecutil_calculate_closest_point_on_line(&c, from, to, &nb->pos);
                // Distância (não ao quadrado) e a altura do nó a, como no original.
                if (Vec_Dist2D(&c, &nb->pos) < 0.16000001f && std::fabs(c.y - na->pos.y) < 2.0f) return 0;
            }
            l = next_in_cel(l, na, nb, cel);
        }
    }
    return result;
}

// [decomp] 0x00158140 AINetwork_BoundsNodeTest
// Um passo curto de from a to passa longe das pontas de limite das salas atravessadas? A folga é
// radius ao quadrado, ou metade dela para pontas perto (1,3 x) de from ou de to; a altura comparada
// é sempre a do nó a do segmento.
int8_t AINetwork_BoundsNodeTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                                float radius, _VECTOR*, int8_t*) {
    HITTEST_tag test{};
    test.start = *from;
    test.end = *to;
    test.radius = Vec_Dist3D(from, to);
    test.type = 0x201;
    test.cel = const_cast<cel_tag*>(from_cel);
    uint16_t count = 1;
    if (from_cel == nullptr) return 0;
    cel_tag** cels = Collide_StraddleCels(&test, &count);
    if (to_cel != nullptr && !cel_in(cels, count, to_cel)) return 0;
    const float r2 = radius * radius;
    const float near = r2 * 1.3f;
    const auto clearance = [&](const _VECTOR& p) {
        if (Vec_SqDist2D(&p, to) < near) return r2 * 0.5f;
        if (Vec_SqDist2D(&p, from) < near) return r2 * 0.5f;
        return r2;
    };
    for (uint16_t i = 0; i < count; ++i) {
        const cel_tag* cel = cels[i];
        for (const BLINK_tag* l = cel->ai_blinks; l != nullptr;) {
            const AIBounds_tag& b = AINetwork.bounds[uint16_t(l->bounds)];
            const BMAPNODE_tag* na = b.nodes[l->a].node;
            const BMAPNODE_tag* nb = b.nodes[l->b].node;
            _VECTOR c;
            float thr = clearance(na->pos);
            vecutil_calculate_closest_point_on_line(&c, from, to, &na->pos);
            if (Vec_SqDist2D(&c, &na->pos) < thr && std::fabs(c.y - na->pos.y) < 2.0f) return 0;
            thr = clearance(nb->pos);
            vecutil_calculate_closest_point_on_line(&c, from, to, &nb->pos);
            if (Vec_SqDist2D(&c, &nb->pos) < thr && std::fabs(c.y - na->pos.y) < 2.0f) return 0;
            l = next_in_cel(l, na, nb, cel);
        }
    }
    return 1;
}

// ---- nós de uma posição ----

// [decomp] 0x00158488 AINetwork_NavPathForPosition
// Caminho da rede de navegação do nó mais perto (2D) da posição: primeiro entre os nós da sala (com
// test_move, só os alcançáveis em linha reta), senão entre todos os nós de navegação.
AIPath_tag* AINetwork_NavPathForPosition(_VECTOR* pos, cel_tag* cel, int8_t test_move) {
    if (cel == nullptr) return nullptr;
    uint32_t best_path = 0xffff;
    float best = FLT_MAX;
    for (NODE_tag* n = cel->ai_nodes; n != nullptr; n = n->next) {
        MAPNODE_tag* mn = n->node;
        if ((AINetwork.paths[uint16_t(mn->path)].flags & 0xd) != 0) continue;
        const float d = Vec_Dist2D(pos, &mn->pos);
        if (!(d < best)) continue;
        if (test_move != 0 && NDrone2_MoveTest(pos, cel, &mn->pos, mn->cel, nullptr, nullptr, nullptr, 0, 1) != 1) {
            continue;
        }
        best_path = uint16_t(mn->path);
        best = d;
    }
    if (best_path == 0xffff) {
        for (uint32_t i = 0; i < AINetwork.path_count; i = (i + 1) & 0xffff) {
            const AIPath_tag& path = AINetwork.paths[i];
            if ((path.flags & 0xd) != 0) continue;
            for (uint32_t k = 0; k < path.node_count; k = (k + 1) & 0xffff) {
                const MAPNODE_tag* mn = path.nodes[k].node;
                const float d = Vec_Dist2D(pos, &mn->pos);
                if (d < best) {
                    best_path = uint16_t(mn->path);
                    best = d;
                }
            }
        }
        if (best_path == 0xffff) return nullptr;
    }
    return &AINetwork.paths[best_path];
}

// [decomp] 0x001586a8 AINetwork_NodesForPosition
// Marca, em cada caminho de navegação, o nó mais perto que alcança a posição (target->mask) ou, sem
// ele, o mais perto que não alcança (mask << 1); soma as contagens no alvo, marca o caminho
// (AIPath_tag::found) e guarda a posição e o quadro da busca.
void AINetwork_NodesForPosition(AITarget_tag* target, CelPos_tag* pos, obj_tag* obj, float radius) {
    std::memcpy(&target->last, &target->pos, 0x20);
    target->found = 0;
    target->failed = 0;
    const uint32_t mask = target->mask;
    AINodeSearch_tag s{};
    s.pos = pos;
    s.goal = nullptr;
    s.to_node = 0;
    s.open = nullptr;
    s.obj_a = target->obj;
    s.obj_b = obj;
    s.mask = mask;
    s.radius_sq = radius * radius;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        if ((path.flags & 0xd) != 0) continue;
        s.path = &path;
        for (uint32_t k = 0; k < path.node_count; ++k) path.nodes[k].node->flags &= ~(mask << 1 | mask);
        s.failed = 0;
        s.found = 0;
        AINetwork_NodeSearchCel(&s);
        search_portals(&s);
        target->found = int16_t(target->found + s.found);
        target->failed = int16_t(target->failed + s.failed);
        if (s.found == 0) {
            path.found &= ~mask;
        } else {
            path.found |= mask;
        }
        if (s.failed == 0) {
            path.found &= ~(mask << 1);
        } else {
            path.found |= mask << 1;
        }
    }
    target->frame = GameState.frame;
}

// ---- preparar e calcular rotas ----

// [decomp] 0x00158948 AINetwork_SetupRouteToObject
// Prepara a rota até um objeto: o ponto de destino (point) e o alvo (goal, com os nós achados) na
// posição dele; para o jogador, a rota usa o alvo compartilhado NPCGlobals.player_target.
uint32_t AINetwork_SetupRouteToObject(obj_tag* owner, obj_tag* target, AITarget_tag* goal, AIPoint_tag* point,
                                      float radius, AIRoute_tag* route) {
    if (target == nullptr || route == nullptr) return 0;
    const uint16_t flags = route->flags;
    reset_route_fields(route);
    route->owner = owner;
    route->target_obj = target;
    route->flags = flags & 0xffaf;
    route->target = target == NDrone2_Player() ? &NPCGlobals.player_target : goal;
    if (radius == 0.0f) radius = 2.0f;
    point->pos.pos = target->pos;
    point->active = 1;
    point->pos.cel = target->cel;
    point->radius = radius;
    fill_goal(goal, point);
    NDrone2_FeetPos(owner, &route->from.pos);
    route->from.cel = owner->cel;
    NDrone2_FeetPos(target, &route->to.pos);
    route->to.cel = target->cel;
    return 1;
}

// [decomp] 0x00158b10 AINetwork_SetupRouteToPosition
// Como AINetwork_SetupRouteToObject para uma posição (acha a sala dela se faltar; sem sala, 0).
// route->arrive recebe o raio pedido (0 fica 0).
uint32_t AINetwork_SetupRouteToPosition(obj_tag* owner, CelPos_tag* pos, AITarget_tag* goal, AIPoint_tag* point,
                                        float radius, AIRoute_tag* route) {
    if (pos == nullptr || route == nullptr) return 0;
    const uint16_t flags = route->flags;
    reset_route_fields(route);
    route->owner = owner;
    route->target_obj = nullptr;
    route->flags = flags & 0xffaf;
    if (pos->cel == nullptr) {
        pos->cel = NDrone2_FindCel(&pos->pos);
        if (pos->cel == nullptr) return 0;
    }
    route->target = goal;
    const float r = radius == 0.0f ? 2.0f : radius;
    if (&point->pos != pos) {
        point->pos.pos = pos->pos;
        point->pos.cel = pos->cel;
    }
    point->radius = r;
    point->active = 1;
    fill_goal(goal, point);
    NDrone2_FeetPos(owner, &route->from.pos);
    route->from.cel = owner->cel;
    route->to.pos = pos->pos;
    route->arrive = radius;
    route->to.cel = pos->cel;
    return 1;
}

// ---- emissores ----

// [decomp] 0x00158cf8 AINetwork_EmitPath
// Dijkstra a partir do nó do caminho mais perto do emissor (alcançável), até max_dist (255 se 0):
// em->dist recebe, por nó, a distância * 255 / 256 (até 254) ou 0xff fora do alcance.
uint32_t AINetwork_EmitPath(AIEmitter_tag* em, float max_dist) {
    if (em == nullptr || em->dist == nullptr || em->path == nullptr) return 0;
    AIPath_tag* path = em->path;
    if (max_dist == 0.0f) max_dist = 255.0f;
    for (int32_t i = 0; i < int32_t(path->node_count); ++i) {
        path->nodes[i].node->flags &= 0x3fffffff;
        path->nodes[i].g = 255.0f;
        em->dist[i] = 0xff;
    }
    NODE_tag open{};
    NODE_tag closed{};
    AINodeSearch_tag s{};
    s.pos = &em->pos;
    s.goal = nullptr;
    s.path = path;
    s.to_node = 1;
    s.open = &open;
    s.obj_a = em->owner;
    s.obj_b = nullptr;
    s.mask = 0;
    s.radius_sq = 625.0f;
    s.failed = 0;
    s.found = 0;
    AINetwork_NodeSearchCel(&s);
    search_portals(&s);
    if (s.found < 1) return 0;
    em->start = s.node;
    while (open.list_next != nullptr) {
        NODE_tag* n = open_pop(&open);
        for (int32_t s2 = 0; s2 < int32_t(n->link_count); ++s2) {
            NODE_tag* m = &path->nodes[neighbour(path, n, s2)];
            if (!door_passable(n, m)) continue;
            const LINK_tag& l = slot_link(path, n, s2);
            const float g = n->g + l.length * float(l.used + 1);
            if (max_dist < g) continue;
            if ((m->node->flags & (kNodeOpen | kNodeClosed)) != 0 && !(g < m->g)) continue;
            m->f = g;
            m->parent = n;
            m->g = g;
            m->h = 0.0f;
            m->parent_link = uint8_t(s2);
            if ((m->node->flags & kNodeClosed) != 0) closed_remove(&closed, m);
            if ((m->node->flags & kNodeOpen) == 0) {
                open_insert(&open, m);
                m->node->flags |= kNodeOpen;
            }
        }
        n->list_next = closed.list_next;
        closed.list_next = n;
        n->node->flags |= kNodeOpen;  // (sic)
    }
    for (int32_t i = 0; i < int32_t(path->node_count); ++i) {
        const float g = path->nodes[i].g;
        uint32_t v = 0xff;
        if (g != 255.0f) {
            v = uint32_t(std::fabs(g * 0.00390625f) * 255.0f);
            if (!(v < 0xff)) v = 0xfe;
        }
        em->dist[i] = uint8_t(v);
    }
    return 1;
}

// [decomp] 0x00159268 AINetwork_Emitter_GetNodeAtDistance
// A partir do nó start (o do emissor se negativo), busca em largura (na ordem do f que os nós
// guardam da última busca) o primeiro vizinho cujo byte de distância do emissor é >= dist; os já
// vistos com distância menor que a do início não voltam à lista.
uint32_t AINetwork_Emitter_GetNodeAtDistance(int16_t start, float dist, AIEmitter_tag* em, MAPNODE_tag** out) {
    int32_t idx = start;
    if (idx < 0) {
        idx = em->start;
        if (idx < 0) {
            idx = NDrone2_NearestNode(&em->pos, em->path, em->owner);
            if (idx < 0) return 0;
        }
    }
    AIPath_tag* path = em->path;
    const uint8_t start_dist = em->dist[idx];
    const uint32_t want = uint32_t(int32_t(dist)) & 0xff;
    NODE_tag open{};
    NODE_tag closed{};
    for (int32_t i = 0; i < int32_t(path->node_count); ++i) path->nodes[i].node->flags &= 0x3fffffff;
    NODE_tag* first = &path->nodes[idx];
    open_insert(&open, first);
    first->node->flags |= kNodeOpen;
    while (open.list_next != nullptr) {
        NODE_tag* n = open_pop(&open);
        for (int32_t s2 = 0; s2 < int32_t(n->link_count); ++s2) {
            const uint32_t k = neighbour(path, n, s2);
            const uint8_t d = em->dist[k];
            NODE_tag* m = &path->nodes[k];
            if (!door_passable(n, m)) continue;
            if (want <= d) {
                if (out != nullptr) *out = m->node;
                return 1;
            }
            if ((m->node->flags & (kNodeOpen | kNodeClosed)) != 0 && d < start_dist) continue;
            if ((m->node->flags & kNodeClosed) != 0) closed_remove(&closed, m);
            if ((m->node->flags & kNodeOpen) != 0) continue;
            open_insert(&open, m);
            m->node->flags |= kNodeOpen;
        }
        n->list_next = closed.list_next;
        closed.list_next = n;
        n->node->flags |= kNodeOpen;  // (sic)
    }
    return 0;
}

// ---- seguir a rota ----

// [decomp] 0x00159668 AINetwork_SetupNextNode
// Avança step posições na rota (ao contrário com kRouteReverse) e acerta o próximo ponto: dentro da
// rota, o primeiro passo da ligação (LinkCreep_Calc); depois do fim, o destino (modo 1 e 0), a volta
// ao começo (modo 2) ou a inversão (modo 3).
uint32_t AINetwork_SetupNextNode(int16_t step, AIRoute_tag* route, DCVars_tag*) {
    int32_t cur;
    if (step < 0 && route->current < -1) {
        cur = route->current;
    } else {
        const int16_t s = (route->flags & kRouteReverse) != 0 ? int16_t(-step) : step;
        route->current = int16_t(route->current + s);
        cur = route->current;
    }
    bool creep = cur < int32_t(route->count);
    bool to_dest_at_end = false;  // destino e current = count (LAB_0015973c)
    if (!creep) {
        const uint16_t flags = route->flags;
        const uint32_t mode = flags & kRouteModeMask;
        if (mode == 2) {
            route->current = 0;
            creep = true;
        } else if (mode == 3) {
            const int16_t d = (flags & kRouteReverse) != 0 ? 2 : -2;
            route->current = int16_t(cur + d);
            route->flags = (flags & kRouteReverse) != 0 ? uint16_t(flags & 0xfff7) : uint16_t(flags | kRouteReverse);
            creep = true;
        } else {
            to_dest_at_end = cur != int32_t(route->count);
        }
    }
    if (creep) {
        if (LinkCreep_Calc(route, step < 1 ? 1 : 0) == 0) return 1;
        CelPos_tag dest{};
        LinkCreep_Dest(route, 0, &dest);
        route->next.pos = dest.pos;
        if (dest.cel == nullptr) dest.cel = NDrone2_FindCel(&dest.pos);
        route->next.cel = dest.cel;
        return 1;
    }
    if (to_dest_at_end) route->current = int16_t(route->count);
    route->next.pos = route->to.pos;
    if (route->to.cel == nullptr) route->to.cel = NDrone2_FindCel(&route->to.pos);
    route->next.cel = route->to.cel;
    if (to_dest_at_end) route->at_node = route->current;
    return 1;
}

// [decomp] 0x0015a490 AINetwork_UpdateLinksUsedCount
// Soma 1 ao uso de cada ligação que falta percorrer na rota (a partir da anterior à posição atual);
// AINetwork_ClearLinkFlags zera. Só na rede de navegação.
void AINetwork_UpdateLinksUsedCount(AIRoute_tag* route) {
    AIPath_tag* path = route->path;
    if (path == nullptr || (path->flags & 0xc) != 0) return;
    int32_t i = int16_t(route->current - 1);
    if (i < 0) i = 0;
    uint16_t prev = route->list[i];
    for (uint32_t k = uint32_t(i + 1) & 0xffff; k < route->count; k = (k + 1) & 0xffff) {
        const uint16_t cur = route->list[k];
        uint32_t l = 0xffff;
        if (cur != 0xffff && prev != 0xffff) {
            const NODE_tag& n = path->nodes[cur];
            for (int32_t s = 0; s < int32_t(n.link_count); ++s) {
                const uint16_t li = path->node_links[n.first_link + s];
                if (path->links[li].a == prev || path->links[li].b == prev) {
                    l = li;
                    break;
                }
            }
        }
        if (l == 0xffff) {
            ++g_ainetwork_guards.link_not_found;  // port: o original somaria em links[0xffff]
        } else {
            ++path->links[l].used;
        }
        prev = cur;
    }
}

// [decomp] 0x0015a5c8 AINetwork_InitPassableBoundries
// Limites atravessados por alguma ligação de qualquer caminho viram passáveis (kBoundPassable), menos
// os de porta e de chute; ajusta a altura deles à das ligações (adjust).
uint32_t AINetwork_InitPassableBoundries() {
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.link_count; ++k) {
            const LINK_tag& l = path.links[k];
            const MAPNODE_tag* a = path.nodes[l.a].node;
            const MAPNODE_tag* b = path.nodes[l.b].node;
            for (uint32_t j = 0; j < AINetwork.bounds_count; ++j) {
                AIBounds_tag& bounds = AINetwork.bounds[j];
                for (uint32_t m = 0; m < bounds.link_count; ++m) {
                    BLINK_tag& bl = bounds.links[m];
                    if ((bl.flags & (kBoundDoor | kBoundKick)) != 0) continue;
                    if ((bl.flags & kBoundPassable) == kBoundPassable) continue;
                    if (AINetwork_LineIntersectsBoundry(&bl, &a->pos, &b->pos, nullptr, nullptr, 1) != 0) {
                        bl.flags |= kBoundPassable;
                    }
                }
            }
        }
    }
    return 1;
}

// [decomp] 0x0015a7c0 AINetwork_RouteIsValid
uint8_t AINetwork_RouteIsValid(AIRoute_tag* route) {
    if (route == nullptr) return 0;
    return uint8_t(route->flags & kRouteValid);
}

// [decomp] 0x0015a7e0 AINetwork_InvalidateRoute
void AINetwork_InvalidateRoute(AIRoute_tag* route) {
    if (route != nullptr) route->flags &= 0xffef;
}

// [decomp] 0x0015a800 AINetwork_ValidateRoute
void AINetwork_ValidateRoute(AIRoute_tag* route) {
    if (route != nullptr) route->flags |= kRouteValid;
}

// [decomp] 0x0015a820 AINetwork_RouteNodeOffset
// Posição current + delta na rota, dando a volta (modo 2), refletindo (modo 3) ou presa entre -1 e
// a última (outros modos).
int32_t AINetwork_RouteNodeOffset(int16_t current, int16_t delta, AIRoute_tag* route) {
    const int16_t v = int16_t(current + delta);
    if (v >= 0 && int32_t(v) < int32_t(route->count)) return v;
    const uint32_t mode = route->flags & kRouteModeMask;
    if (mode == 2) return int16_t(v < 0 ? route->count + v : v - route->count);
    if (mode == 3) return int16_t(v < 0 ? -v : route->count * 2 - v);
    return v < 0 ? -1 : int16_t(route->count - 1);
}

// [decomp] 0x0015a8d8 AINetwork_FurthestPosition
// Até onde se vai de from para to sem cruzar limite: out recebe o cruzamento mais perto (ou to).
uint32_t AINetwork_FurthestPosition(_VECTOR* from, _VECTOR* to, _VECTOR* out) {
    float best = FLT_MAX;
    if (AINetwork.bounds_count == 0) return 0;
    *out = *to;
    uint32_t r = 0;
    for (uint32_t i = 0; i < AINetwork.bounds_count; ++i) {
        AIBounds_tag& b = AINetwork.bounds[i];
        for (uint32_t k = 0; k < b.link_count; ++k) {
            if (AINetwork_LineIntersectsBoundry(&b.links[k], from, to, out, &best, 0) != 0) r = 1;
        }
    }
    return r;
}

// [decomp] 0x0015aa00 AINetwork_ResetRoute
void AINetwork_ResetRoute(AIRoute_tag* route, obj_tag* owner, obj_tag* target) {
    route->target_obj = target;
    route->owner = owner;
    reset_route_fields(route);
}

// [decomp] 0x0015aa88 AINetwork_NodeSearch
// AINetwork_NodeSearchCel na sala da posição e, sem resultado, de novo para cada portal (ver
// search_portals). 0 só quando a posição não tem sala.
uint32_t AINetwork_NodeSearch(AINodeSearch_tag* search) {
    search->failed = 0;
    search->found = 0;
    AINetwork_NodeSearchCel(search);
    if (search->found != 0) return 1;
    if (search->pos->cel == nullptr) return 0;
    search_portals(search);
    return 1;
}

// [decomp] 0x0015ab78 AINetwork_TestRayCels
// O raio de from a to passa pelas salas até chegar em to_cel?
uint32_t AINetwork_TestRayCels(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel) {
    if (from_cel == nullptr || to_cel == nullptr) return 0;
    uint16_t count;
    cel_tag** cels = ray_cels(*from, *to, from_cel, &count);
    return cel_in(cels, count, to_cel) ? 1 : 0;
}

// [decomp] 0x0015ac78 AINetwork_BuildAITarget
// Refaz os nós do alvo se ele nunca foi buscado ou andou 2 m desde a última busca.
void AINetwork_BuildAITarget(AITarget_tag* target, obj_tag* obj, float radius) {
    if (target->frame == 0 || !(Vec_Dist3D(&target->last.pos, &target->pos.pos) < 2.0f)) {
        AINetwork_NodesForPosition(target, &target->pos, obj, radius);
    }
}

// [decomp] 0x0015acf8 AINetwork_SetupGoalPosition
void AINetwork_SetupGoalPosition(AITarget_tag* target, AIPoint_tag* point, CelPos_tag* pos, float radius, int8_t) {
    if (radius == 0.0f) radius = 2.0f;
    if (pos->cel == nullptr) pos->cel = NDrone2_FindCel(&pos->pos);
    if (&point->pos != pos) {
        point->pos.pos = pos->pos;
        point->pos.cel = pos->cel;
    }
    point->radius = radius;
    point->active = 1;
    fill_goal(target, point);
}

// [decomp] 0x0015add0 AINetwork_SetupGoalPositionToObj
bool AINetwork_SetupGoalPositionToObj(AITarget_tag* target, AIPoint_tag* point, obj_tag* obj, float radius, int8_t) {
    if (radius == 0.0f) radius = 2.0f;
    if (obj != nullptr) {
        point->pos.pos = obj->pos;
        point->active = 1;
        point->pos.cel = obj->cel;
        point->radius = radius;
        fill_goal(target, point);
    }
    return obj != nullptr;
}

namespace {

// A rota ainda vale para o alvo: o mesmo objeto (só fora do multijogador), alvo não buscado de novo
// depois do cálculo e rota válida.
bool route_still_valid(const AIRoute_tag* route, const obj_tag* target) {
    const AITarget_tag* t = route->target;
    if (t == nullptr) return false;
    if (!mp_any_target() && target != t->obj) return false;
    if (!(t->frame <= route->frame)) return false;
    return (route->flags & kRouteValid) != 0;
}

}  // namespace

// [decomp] 0x0015ae68 AINetwork_VerifyRouteTarget
bool AINetwork_VerifyRouteTarget(obj_tag* owner, obj_tag* target, AITarget_tag* goal, AIPoint_tag* point, float radius,
                                 AIRoute_tag* route) {
    if (route_still_valid(route, target)) return true;
    return AINetwork_SetupRouteToObject(owner, target, goal, point, radius, route) != 0;
}

// [decomp] 0x0015aee8 AINetwork_CalcRouteToObject
// Prepara (se preciso) e calcula a rota até o objeto; rota aproximada recua o destino do limite.
uint32_t AINetwork_CalcRouteToObject(obj_tag* owner, obj_tag* target, AIRoute_tag* route, AITarget_tag* goal,
                                     AIPoint_tag* point, float radius, uint16_t param) {
    uint32_t r = kRouteNoNodes;
    bool ok = route_still_valid(route, target);
    if (!ok) ok = AINetwork_SetupRouteToObject(owner, target, goal, point, radius, route) != 0;
    if (ok) {
        r = AINetwork_CalcRoute(route, owner, target, param);
        if (r == kRouteNearest) AINetwork_AlterDestFor_DROUTE_Nearest(route);
    }
    return r;
}

// [decomp] 0x0015afe0 AINetwork_CalcRouteToPosition
// Como AINetwork_CalcRouteToObject para uma posição: a rota é reaproveitada se o alvo atual é uma
// posição (sem objeto) a menos de 1 m dela e a rota é válida.
uint32_t AINetwork_CalcRouteToPosition(obj_tag* owner, CelPos_tag* pos, AIRoute_tag* route, AITarget_tag* goal,
                                       AIPoint_tag* point, float radius, uint16_t param) {
    uint32_t r = kRouteNoNodes;
    bool ok = false;
    if (pos != nullptr && route != nullptr) {
        const AITarget_tag* t = route->target;
        if (t != nullptr && t->obj == nullptr && Vec_SqDist2D(&pos->pos, &t->pos.pos) <= 1.0f &&
            (route->flags & kRouteValid) != 0) {
            ok = true;
        } else {
            ok = AINetwork_SetupRouteToPosition(owner, pos, goal, point, radius, route) != 0;
        }
    }
    if (ok) {
        r = AINetwork_CalcRoute(route, owner, nullptr, param);
        if (r == kRouteNearest) AINetwork_AlterDestFor_DROUTE_Nearest(route);
    }
    return r;
}

// [decomp] 0x0015b110 AINetwork_FollowRoute
// Um tick de rota: distância restante, uso das ligações e um passo de LinkCreep. 3 = chegou (a menos
// de route->arrive do destino depois do último nó), 0 = andando, 9/0xc = travado.
uint32_t AINetwork_FollowRoute(AIRoute_tag* route, DCVars_tag* dcv) {
    if (route->path == nullptr) return kRouteNoNetwork;
    route->distance = AINetwork_GetRouteDistance(route);
    AINetwork_UpdateLinksUsedCount(route);
    if (route->current < int32_t(route->count) || (route->flags & kRouteModeMask) == 2 ||
        route->arrive <= route->distance) {
        if (LinkCreep_Handler(route, dcv) == 0) {
            route->status = route->unknown_02 != 0 ? kRouteBlocked : kRouteStuck;
            return route->status;
        }
        route->status = kRouteExact;
        return 0;
    }
    route->status = kRouteArrived;
    return kRouteArrived;
}

// [decomp] 0x0015b1e8 AINetwork_InitEmitter
// Emissor no ponto: sala (procurada se faltar; sem sala, 0), caminho (o de navegação mais perto se
// nulo) e a tabela de distâncias alocada (1 se alocou).
uint32_t AINetwork_InitEmitter(obj_tag* owner, _VECTOR* pos, cel_tag* cel, AIEmitter_tag* em, AIPath_tag* path,
                               int8_t) {
    if (em == nullptr) return 0;
    if (cel == nullptr) {
        cel = build_FindCel(pos, glb_world);
        if (cel == nullptr) return 0;
    }
    em->owner = owner;
    em->dist = nullptr;
    em->pos.cel = cel;
    em->pos.pos = *pos;
    em->path = path != nullptr ? path : AINetwork_NavPathForPosition(&em->pos.pos, cel, 0);
    if (em->path == nullptr || em->path->node_count == 0) return 0;
    AINetwork_AllocEmitter(em, em->path->node_count);
    return 1;
}

// [decomp] 0x0015b2b8 AINetwork_InitEmitter2
// Como AINetwork_InitEmitter, reaproveitando a tabela já alocada.
uint32_t AINetwork_InitEmitter2(obj_tag* owner, _VECTOR* pos, cel_tag* cel, AIEmitter_tag* em, AIPath_tag* path,
                                int8_t) {
    if (em == nullptr) return 0;
    if (cel == nullptr) {
        cel = build_FindCel(pos, glb_world);
        if (cel == nullptr) return 0;
    }
    em->owner = owner;
    em->pos.cel = cel;
    em->pos.pos = *pos;
    em->path = path != nullptr ? path : AINetwork_NavPathForPosition(&em->pos.pos, cel, 0);
    if (em->path == nullptr || em->path->node_count == 0) return 0;
    if (em->allocated == 0) AINetwork_AllocEmitter(em, em->path->node_count);
    return 1;
}

// [decomp] 0x0015b390 AINetwork_AllocEmitter
void AINetwork_AllocEmitter(AIEmitter_tag* em, uint32_t size) {
    em->size = size;
    em->allocated = 1;
    em->dist = static_cast<uint8_t*>(Mem_Malloc(size, 0x4e08, 0));
}

// [decomp] 0x0015b3d8 AINetwork_FreeEmitter
void AINetwork_FreeEmitter(AIEmitter_tag* em) {
    if (em == nullptr || em->dist == nullptr) return;
    void* p = em->dist;
    Mem_Free(&p);
    std::memset(em, 0, sizeof(*em));
}

// [decomp] 0x0015b498 AINetwork_ClearLinkFlags
// Tira os bits `flags` de todas as ligações e zera o uso.
void AINetwork_ClearLinkFlags(uint16_t flags) {
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.link_count; ++k) {
            path.links[k].flags &= ~flags;
            path.links[k].used = 0;
        }
    }
}

// [decomp] 0x0015b548 AINetwork_SetLinksFlagInCircle
// Liga `set` nas ligações que passam (em planta) a até `radius` do centro, numa faixa de altura de
// +-2 m; tira `clear` de todas.
void AINetwork_SetLinksFlagInCircle(_VECTOR* center, cel_tag*, float radius, uint16_t set, uint16_t clear) {
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.link_count; ++k) {
            LINK_tag& l = path.links[k];
            if (clear != 0) l.flags &= ~clear;
            const MAPNODE_tag* b = path.nodes[l.b].node;
            const MAPNODE_tag* a = path.nodes[l.a].node;
            const float lo = b->pos.y < a->pos.y ? b->pos.y : a->pos.y;
            const float hi = a->pos.y < b->pos.y ? b->pos.y : a->pos.y;
            if (!(lo <= center->y + 2.0f) || !(center->y - 2.0f <= hi)) continue;
            _VECTOR c;
            vecutil_calculate_closest_point_on_line(&c, &a->pos, &b->pos, center);
            if (Vec_SqDist2D(&c, center) <= radius * radius) l.flags |= set;
        }
    }
}

// [decomp] 0x0015b6f0 AINetwork_ModIntersectedLinkFlags_Bounds
// Liga `set` nos segmentos de limite (sem `skip` e sem `set` completo) que a linha from-to cruza.
uint32_t AINetwork_ModIntersectedLinkFlags_Bounds(_VECTOR* from, _VECTOR* to, uint32_t set, uint32_t skip, _VECTOR* out,
                                                  int8_t adjust) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < AINetwork.bounds_count; ++i) {
        AIBounds_tag& b = AINetwork.bounds[i];
        for (uint32_t k = 0; k < b.link_count; ++k) {
            BLINK_tag& l = b.links[k];
            if ((l.flags & skip) != 0 || (l.flags & set) == set) continue;
            if (AINetwork_LineIntersectsBoundry(&l, from, to, out, nullptr, adjust) != 0) {
                r = 1;
                l.flags |= set;
            }
        }
    }
    return r;
}

// ---- LinkCreep: andar pela ligação em passos de 0,4 m ----

namespace {

// Passos de 0,4 m de origin a end: creep_steps = int(int(comprimento) * 2,5) (1 se até 0,4 m) e
// creep_dir = um passo.
void creep_setup(AIRoute_tag* route, const _VECTOR& origin, const _VECTOR& end) {
    route->creep_origin = origin;
    route->creep_end = end;
    route->creep_dir = vsub(end, origin);
    const float len = Vec_Magnitude(&route->creep_dir);
    if (0.4f < len) {
        route->creep_steps = int16_t(int32_t(float(int32_t(len) & 0xffff) * 2.5f));
        Vec_MulR32(&route->creep_dir, &route->creep_dir, 0.4f / len);
    } else {
        route->creep_steps = 1;
    }
}

// Ponto do passo `step` da ligação atual.
_VECTOR creep_point(const AIRoute_tag* route, int32_t step) {
    _VECTOR p;
    Vec_MulR32(&p, &route->creep_dir, float(step));
    return vadd(route->creep_origin, p);
}

}  // namespace

// [decomp] 0x00159810 LinkCreep_ForNodes
// Começa a andar da posição `from` à `to` da rota (from < 0: dos pés de onde a rota começou; to < 0:
// até o destino; from == to: do nó ao destino, salvo no modo 2). Marca a ligação usada
// (kLinkCreeping) e `special` quando o nó de saída tem ações (flags 0xf007f) ou no fim do modo 1.
// Com at_end, começa do último passo.
int8_t LinkCreep_ForNodes(AIRoute_tag* route, int32_t from, int32_t to, int8_t at_end) {
    route->unknown_a2 = 0;
    route->special = 0;
    const _VECTOR* a;
    const _VECTOR* b;
    if (from < 0) {
        a = &route->from.pos;
        b = to >= 0 ? &route_node(route, to)->node->pos : &route->to.pos;
    } else {
        const MAPNODE_tag* na = route_node(route, from)->node;
        const MAPNODE_tag* nb = route_node(route, to)->node;
        if ((na->flags & 0xf007f) != 0) route->special = 1;
        const uint32_t mode = route->flags & kRouteModeMask;
        if (from == to && mode != 2) {
            if (mode == 1) route->special = 1;
            a = &na->pos;
            b = &route->to.pos;
        } else {
            const uint32_t l = link_between(route->path, route->list[from], int16_t(route->list[to]));
            a = &na->pos;
            b = &nb->pos;
            route->creep_link = int16_t(l);
            if (int16_t(l) >= 0) route->path->links[int16_t(l)].flags |= kLinkCreeping;
        }
    }
    route->creep_from = int16_t(from);
    route->creep_to = int16_t(to);
    route->unknown_a3 = 0;
    creep_setup(route, *a, *b);
    route->creeping = 1;
    route->creep_path = route->path;
    route->creep_step = 0;
    if (at_end != 0) route->creep_step = int16_t(route->creep_steps - 1);
    LinkCreep_Dest(route, 0, &route->next);
    return route->creeping;
}

// [decomp] 0x00159b10 LinkCreep_Calc
// Solta a ligação anterior (exceto a de índice 0, que o original pula) e começa a andar da posição
// atual da rota à seguinte. Depois do fim da rota, zera o estado de LinkCreep (0xa0-0xef) e devolve 0.
int8_t LinkCreep_Calc(AIRoute_tag* route, int8_t at_end) {
    if (int32_t(route->count) < int32_t(route->current)) {
        std::memset(reinterpret_cast<uint8_t*>(route) + 0xa0, 0, 0x50);
        return 0;
    }
    if (route->creep_link > 0) route->path->links[route->creep_link].flags &= ~kLinkCreeping;
    const int32_t from = int16_t(AINetwork_RouteNodeOffset(route->current, 0, route));
    const int32_t to = int16_t(AINetwork_RouteNodeOffset(route->current, 1, route));
    return LinkCreep_ForNodes(route, from, to, at_end);
}

// [decomp] 0x00159cc8 LinkCreep_CalcToRouteEnd
// Rota direta: anda dos pés ao destino.
bool LinkCreep_CalcToRouteEnd(AIRoute_tag* route) {
    creep_setup(route, route->from.pos, route->to.pos);
    route->creep_step = 0;
    route->creeping = 1;
    route->creep_path = route->path;
    LinkCreep_Dest(route, 0, &route->next);
    if (route->creeping != 0) {
        route->unknown_a3 = 1;
        route->creep_to = -1;
        route->special = 0;
        route->unknown_a2 = 1;
        route->creep_from = -1;
    }
    return route->creeping != 0;
}

// [decomp] 0x00159dd0 LinkCreep_AtSpecialNode
// O dono chegou ao nó atual (a menos de 0,4 m), ou está a menos de 1 m e há alguém (o jogador ou
// outro drone) a 0,4 m do nó.
uint32_t LinkCreep_AtSpecialNode(AIRoute_tag* route) {
    const int32_t idx = int16_t(AINetwork_RouteNodeOffset(route->current, 0, route));
    if (idx < 0) {
        ++g_ainetwork_guards.special_before_route;  // port: o original leria list[-1]
        return 0;
    }
    _VECTOR* p = &route_node(route, idx)->node->pos;
    const float d = Vec_Dist2D(p, &route->owner->pos);
    if (d < 0.4f) return 1;
    if (d < 1.0f) return NDrone2_DroneNearPos(p, 0.4f, route->owner) != 0 ? 1 : 0;
    return 0;
}

// [decomp] 0x00159f28 LinkCreep_Increment
// Próximo passo da ligação: no fim dela, o próximo nó (AINetwork_SetupNextNode). O passo é aceito
// (vira route->next) se estiver a menos de 0,4 m dos pés ou longe das pontas de limite; longe demais
// (creep_max) espera. blocked (zerado aqui) é repassado a AINetwork_BoundsNodeTest.
uint32_t LinkCreep_Increment(AIRoute_tag* route, DCVars_tag* dcv, int8_t* blocked) {
    obj_tag* owner = route->owner;
    if (route->creeping == 0) return 1;
    if (route->creep_step >= route->creep_steps) return AINetwork_SetupNextNode(1, route, dcv);
    _VECTOR feet;
    NDrone2_FeetPos(owner, &feet);
    cel_tag* feet_cel = owner->cel;
    const _VECTOR next = creep_point(route, route->creep_step + 1);
    cel_tag* next_cel = build_FindCel(&next, glb_world);
    const float d = Vec_Dist2D(&feet, &next);
    if (blocked != nullptr) *blocked = 0;
    int8_t r;
    if (AINetwork.bounds_count == 0 || Vec_SqDist2D(&feet, &next) < 1.0f) {
        r = 1;
    } else {
        r = AINetwork_BoundsNodeTest(&feet, feet_cel, &next, next_cel, 0.4f, nullptr, blocked);
    }
    if (blocked != nullptr && *blocked != 0) return 0;
    if (!(d < route->creep_max)) return 1;
    if (d < 0.4f || r == 1) {
        route->next.pos = next;
        if (next_cel == nullptr) next_cel = NDrone2_FindCel(const_cast<_VECTOR*>(&next));
        ++route->creep_step;
        route->next.cel = next_cel;
        return 1;
    }
    return d < 0.3f ? 0 : 1;
}

// [decomp] 0x0015a148 LinkCreep_Decrement
// Passo para trás (o passo atual cruzaria um limite): no começo da ligação, volta um nó.
uint32_t LinkCreep_Decrement(AIRoute_tag* route, DCVars_tag* dcv) {
    obj_tag* owner = route->owner;
    if (route->creeping == 0 || route->creep_step < 1) {
        if (route->current < 0) return 0;
        AINetwork_SetupNextNode(-1, route, dcv);
        return 1;
    }
    _VECTOR feet;
    NDrone2_FeetPos(owner, &feet);
    cel_tag* feet_cel = owner->cel;
    const _VECTOR prev = creep_point(route, route->creep_step - 1);
    cel_tag* prev_cel = build_FindCel(&prev, glb_world);
    int8_t r;
    if (AINetwork.bounds_count == 0 || Vec_SqDist2D(&feet, &prev) < 1.0f) {
        r = 1;
    } else {
        r = AINetwork_BoundsNodeTest(&feet, feet_cel, &prev, prev_cel, 0.1f, nullptr, nullptr);
    }
    if (r != 1) return 1;
    route->next.pos = prev;
    if (prev_cel == nullptr) prev_cel = NDrone2_FindCel(const_cast<_VECTOR*>(&prev));
    --route->creep_step;
    route->next.cel = prev_cel;
    return 1;
}

// [decomp] 0x0015a2c8 LinkCreep_Handler
// Um tick de LinkCreep: avisa o drone ao chegar a um nó com ação (NDrone2_ReachedDestNode); se o
// passo atual cruza um limite (folga 0,2), recua; senão avança. Sem a propriedade 0x36 do
// comportamento, um bloqueio no avanço liga 0x1e nas flags do drone (+0x31c).
uint32_t LinkCreep_Handler(AIRoute_tag* route, DCVars_tag* dcv) {
    obj_tag* owner = route->owner;
    if (route->creeping == 0) return 1;
    _VECTOR feet;
    NDrone2_FeetPos(owner, &feet);
    cel_tag* feet_cel = owner->cel;
    const _VECTOR cur = creep_point(route, route->creep_step);
    cel_tag* cur_cel = build_FindCel(&cur, glb_world);
    if (route->special != 0) {
        if (LinkCreep_AtSpecialNode(route) == 0) return 1;
        NDrone2_ReachedDestNode(route, dcv);
        route->special = 0;
    }
    const bool forced = dcv != nullptr && u32_at(dcv_drone(dcv), 0xba8) != 0;
    if (!forced) {
        int8_t r = 1;
        if (AINetwork.bounds_count != 0) {
            r = AINetwork_BoundsNodeTest(&feet, feet_cel, &cur, cur_cel, 0.2f, nullptr, nullptr);
            if (Vec_SqDist2D(&feet, &cur) < 1.0f) r = 1;
        }
        if (r != 1) return LinkCreep_Decrement(route, dcv);
    }
    if (dcv == nullptr) {
        ++g_ainetwork_guards.handler_without_dcv;  // port: o original desreferenciaria nulo
        return LinkCreep_Increment(route, dcv, nullptr);
    }
    uint8_t* drone = dcv_drone(dcv);
    uint32_t* bits;
    std::memcpy(&bits, drone + 0x4d8, 4);
    if (behaviour_util_getProperty(0x36, bits) == 0) {
        int8_t blocked = 0;
        const uint32_t r = LinkCreep_Increment(route, dcv, &blocked);
        if (blocked != 0) {
            const uint32_t f = u32_at(drone, 0x31c) | 0x1e;
            std::memcpy(drone + 0x31c, &f, 4);
        }
        return r;
    }
    return LinkCreep_Increment(route, dcv, nullptr);
}

// [decomp] 0x0015b420 LinkCreep_Dest
// Ponto do passo creep_step + delta e a sala dele.
uint32_t LinkCreep_Dest(AIRoute_tag* route, int16_t delta, CelPos_tag* out) {
    out->pos = creep_point(route, int32_t(route->creep_step) + int32_t(delta));
    out->cel = build_FindCel(&out->pos, glb_world);
    return 1;
}

// ---- testes de movimento e utilidades do NDrone2 ----

namespace {

// Passo em linha reta: distância máxima, desnível máximo, inclinação (acima de slope_height) e,
// havendo limites, AINetwork_BoundsTest.
int8_t move_test(const _VECTOR& from, const cel_tag* from_cel, const _VECTOR& to, const cel_tag* to_cel, _VECTOR* out,
                 int8_t near_nodes, int8_t check) {
    const _VECTOR d = vsub(to, from);
    const float flat = d.x * d.x + d.z * d.z;
    if (!(flat + d.y * d.y <= AINetwork.max_dist_sq)) return 0;
    const float dy = std::fabs(from.y - to.y);
    if (!(dy <= AINetwork.max_height)) return 0;
    if (AINetwork.slope_height < dy && AINetwork.max_slope < std::fabs(-std::atan2(d.y, std::sqrt(flat)))) return 0;
    if (AINetwork.bounds_count == 0) return 1;
    int8_t blocked = 0;
    int32_t r = AINetwork_BoundsTest(&from, from_cel, &to, to_cel, out, near_nodes, check != 0 ? &blocked : nullptr);
    if (blocked != 0) r = 0;
    return int8_t(r);
}

}  // namespace

// [decomp] 0x001551c0 NDrone2_MoveTest
int8_t NDrone2_MoveTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                        obj_tag*, obj_tag*, _VECTOR* out, int8_t near_nodes, int8_t check) {
    return move_test(*from, from_cel, *to, to_cel, out, near_nodes, check);
}

// [decomp] 0x00155330 NDrone2_MoveTest
int8_t NDrone2_MoveTest(CelPos_tag* from, CelPos_tag* to, obj_tag*, obj_tag*, _VECTOR* out, int8_t near_nodes,
                        int8_t check) {
    return move_test(from->pos, from->cel, to->pos, to->cel, out, near_nodes, check);
}

// [decomp] 0x001554a8 NDrone2_MoveTestFromNode
int8_t NDrone2_MoveTestFromNode(MAPNODE_tag* node, CelPos_tag* to, obj_tag*, obj_tag*, _VECTOR* out, int8_t near_nodes,
                                int8_t check) {
    return move_test(node->pos, node->cel, to->pos, to->cel, out, near_nodes, check);
}

// [decomp] 0x00155620 NDrone2_MoveTestToNode
int8_t NDrone2_MoveTestToNode(CelPos_tag* from, MAPNODE_tag* node, obj_tag*, obj_tag*, _VECTOR* out, int8_t near_nodes,
                              int8_t check) {
    return move_test(from->pos, from->cel, node->pos, node->cel, out, near_nodes, check);
}

// [decomp] 0x00149b88 NDrone2_FeetPos
// Posição dos pés: a do objeto, descida da altura do quadril (animação + 0xcc) menos 0,4.
uint32_t NDrone2_FeetPos(obj_tag* obj, _VECTOR* out) {
    *out = obj->pos;
    if (obj->sphere_src == nullptr) return 0;
    float root;
    std::memcpy(&root, obj->sphere_src + 0xcc, 4);
    out->y -= root - 0.4f;
    return 1;
}

// [decomp] 0x00149a80 NDrone2_FindCel
cel_tag* NDrone2_FindCel(_VECTOR* pos) { return build_FindCel(pos, glb_world); }

// [decomp] 0x001558d8 NDrone2_NearestNode
// Nó do caminho mais perto (25 m) que a posição alcança, ou -1.
int16_t NDrone2_NearestNode(CelPos_tag* pos, AIPath_tag* path, obj_tag* obj) {
    AINodeSearch_tag s{};
    s.pos = pos;
    s.goal = nullptr;
    s.path = path;
    s.to_node = 1;
    s.open = nullptr;
    s.obj_a = obj;
    s.obj_b = nullptr;
    s.mask = 0;
    s.radius_sq = 625.0f;
    s.node = -1;
    AINetwork_NodeSearch(&s);
    if (s.found < 1 || s.node == -1) return -1;
    return s.node;
}

// [decomp] 0x00155798 NDrone2_DistanceToEmitter
// Distância (byte do emissor) do nó *node (ou do mais perto da posição, gravado em *node) até o
// emissor; falso se o emissor é de outro caminho ou o nó está fora do alcance.
bool NDrone2_DistanceToEmitter(CelPos_tag* pos, AIPath_tag* path, uint16_t* node, AIEmitter_tag* em, obj_tag* obj,
                               float* dist) {
    if (em->path != path || em->dist == nullptr) {
        if (dist != nullptr) *dist = 255.0f;
        return false;
    }
    uint32_t k = node != nullptr ? *node : 0xffff;
    if (k == 0xffff) k = uint16_t(NDrone2_NearestNode(pos, em->path, obj));
    if (node != nullptr) *node = uint16_t(k);
    const uint8_t d = em->dist[k];
    if (dist != nullptr) *dist = float(d);
    return d != 0xff;
}

// [decomp] 0x00149768 NDrone2_Player
// O jogador que os drones veem: o de verdade ou o falso (NPCGlobals.dummy_player).
obj_tag* NDrone2_Player() {
    if (uint8_t(NPCGlobals.dummy_player_unknown) == 0) return glb_players[0];
    return NPCGlobals.dummy_player;
}

// [decomp] 0x00143e68 NDrone2_DroneNearPos
// Há alguém a `radius` (2D) e 2 m (altura) do ponto: o jogador (0,55 m) ou um drone ativo (bit 0x100
// e sem 0x600 em +0x4f8, vida em +0xac positiva, não tipo 0x11 nem marcado para remoção) que não
// seja `except`?
uint32_t NDrone2_DroneNearPos(_VECTOR* pos, float radius, obj_tag* except) {
    obj_tag* pl = NDrone2_Player();
    if (pl != nullptr && Vec_SqDist2D(pos, &pl->pos) < 0.3025f && std::fabs(pos->y - pl->pos.y) < 2.0f) return 1;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        obj_tag* o;
        std::memcpy(&o, reinterpret_cast<uint8_t*>(n) + 0xc, 4);
        if (o == except) continue;
        if (o == nullptr) continue;
        const auto* drone = static_cast<const uint8_t*>(o->data);
        const uint32_t f = u32_at(drone, 0x4f8);
        float health;
        std::memcpy(&health, drone + 0xac, 4);
        const bool active = (f & 0x600) == 0 && (f & 0x100) != 0 && 0.0f < health && o->type != 0x11 &&
                            (o->life & 1) == 0;
        if (!active) continue;
        if (Vec_SqDist2D(pos, &o->pos) < radius * radius && std::fabs(pos->y - o->pos.y) < 2.0f) return 1;
    }
    return 0;
}
