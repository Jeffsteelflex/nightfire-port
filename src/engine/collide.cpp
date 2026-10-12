// Port do núcleo de colisão contra a geometria e os objetos. Ver collide.h.
//
// Convenções das instruções da VU0 reproduzidas aqui: produto escalar = (x + y) + z, e "+ w" do
// plano no fim; produto vetorial a x b vem do par vopmula/vopmsub. Nas somas "a + v * s" o original
// multiplica só xyz e soma xyzw, deixando no w o que estava no registro; nas posições o w não é
// usado, e o port mantém o w do primeiro operando (os empurrões têm w = 0). Os testes de sinal da
// VU0 (vsub seguido de cfc2 do registro de status: bit S ou o S "grudento") viraram "a - b < 0".
//
// Divergências: só onde o original escreveria fora de uma tabela ou seguiria com um ponteiro nulo
// (g_collide_guards). O scratchpad (psiCopyToSP, UseSPadColl) não existe no PC: a geometria é lida
// direto.

#include "engine/collide.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/sort.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"

LLISTINFO_tag HitHeap;
int32_t HitAllocCnt = 0;
_VECTOR DeltaP;
uint8_t Ident = 0;
CollideGuards g_collide_guards;
int32_t TriAllocCnt = 0;
void* TriHeap = nullptr;
uint32_t TriOnCnt = 0;

namespace {

struct BOXENTRY_tag {
    COLLBOX_tag* box;
    float t;  // distância ao longo do raio (ASH_RecurseBoxesRay)
};
BOXENTRY_tag ColBoxs[600];
uint32_t BoxCnt = 0;

cel_tag* CelList_194[128];
P32<HITDATA_tag> SrtList[1100];  // ordenada com elementos de 4 bytes (QuickSort)

const uint16_t Index_189[6] = {0, 1, 2, 2, 3, 0};  // os dois triângulos de um portal
constexpr float kBig = 3.4e38f;                    // DAT_0030c748 e DAT_0030d128

float dot3(const _VECTOR& a, const _VECTOR& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
float plane_eval(const _VECTOR& p, const plane_equ_tag& n) { return ((p.x * n.a + p.y * n.b) + p.z * n.c) + n.d; }
float plane_dot(const plane_equ_tag& n, const _VECTOR& v) { return (n.a * v.x + n.b * v.y) + n.c * v.z; }
_VECTOR sub(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR add(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
// a + v * s com o w de a.
_VECTOR madd(const _VECTOR& a, const _VECTOR& v, float s) { return {a.x + v.x * s, a.y + v.y * s, a.z + v.z * s, a.w}; }
_VECTOR madd(const _VECTOR& a, const plane_equ_tag& n, float s) { return {a.x + n.a * s, a.y + n.b * s, a.z + n.c * s, a.w}; }
// a x n (vopmula a, n; vopmsub n, a).
_VECTOR cross(const _VECTOR& a, const plane_equ_tag& n) {
    return {a.y * n.c - n.b * a.z, a.z * n.a - n.c * a.x, a.x * n.b - n.a * a.y, 0.0f};
}

int16_t rd_s16(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
float rd_f32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

// Normal quantizada: três s16 vezes a escala e o d em float logo depois.
plane_equ_tag decode_normal(const uint8_t* rec, float scale) {
    return {float(rd_s16(rec)) * scale, float(rd_s16(rec + 2)) * scale, float(rd_s16(rec + 4)) * scale, rd_f32(rec + 6)};
}

// Vértice quantizado de uma folha: s16 * scale + offset.
_VECTOR decode_vertex(const uint8_t* rec, const COLLBOX_tag* box) {
    return {float(rd_s16(rec)) * box->scale + box->offset[0], float(rd_s16(rec + 2)) * box->scale + box->offset[1],
            float(rd_s16(rec + 4)) * box->scale + box->offset[2], 0.0f};
}

struct Tri {
    uint16_t v[3];
    uint16_t normal;
};
const Tri* triangles(const colldata_tag* cd) { return reinterpret_cast<const Tri*>(nf::raw(cd->list)); }
COLLBOX_tag* boxes(const colldata_tag* cd) { return reinterpret_cast<COLLBOX_tag*>(nf::raw(cd->boxes)); }

bool sphere_overlaps_box(const _VECTOR& c, float r, const float min[3], const float max[3]) {
    // Mesma ordem de comparações do original: x, z, y.
    if (!(c.x - r <= max[0])) return false;
    if (c.x + r < min[0]) return false;
    if (max[2] < c.z - r) return false;
    if (c.z + r < min[2]) return false;
    if (max[1] < c.y - r) return false;
    return min[1] <= c.y + r;
}

void add_box(COLLBOX_tag* box, float t) {
    if (BoxCnt >= 600) {
        // Divergência: o original escreveria além de ColBoxs.
        ++g_collide_guards.box_list_full;
        return;
    }
    ColBoxs[BoxCnt].box = box;
    ColBoxs[BoxCnt].t = t;
    ++BoxCnt;
}

// Folha achada pelas buscas por caixa (só o ponteiro; o t fica como estava).
void push_box(COLLBOX_tag* box) {
    if (BoxCnt >= 600) {
        // Divergência: o original escreveria além de ColBoxs.
        ++g_collide_guards.box_list_full;
        return;
    }
    ColBoxs[BoxCnt++].box = box;
}

// Intersect_BoxBox(lo, hi, box->min, box->max), como o original chama com o próprio COLLBOX_tag.
bool box_overlaps(const _VECTOR& lo, const _VECTOR& hi, const COLLBOX_tag* box) {
    const _VECTOR bmin{box->min[0], box->min[1], box->min[2], 0.0f};
    const _VECTOR bmax{box->max[0], box->max[1], box->max[2], 0.0f};
    return Intersect_BoxBox(&lo, &hi, &bmin, &bmax) != 0;
}

// Busca por caixa a partir da raiz, comum a Intersect_CylGeom e Intersect_CylTriGeom.
void collect_boxes(colldata_tag* cd, const _VECTOR& lo, const _VECTOR& hi,
                   void (*recurse)(colldata_tag*, COLLBOX_tag*, const _VECTOR*, const _VECTOR*)) {
    COLLBOX_tag* root = boxes(cd);
    if (!box_overlaps(lo, hi, root)) return;
    if (root->child_a < 0) {
        push_box(root);
    } else {
        recurse(cd, boxes(cd) + root->child_a, &lo, &hi);
        recurse(cd, boxes(cd) + root->child_b, &lo, &hi);
    }
}

// Linha a linha: v * M (vetores linha), com a ordem de somas do vmulax/vmadda.
_VECTOR transform_point(const MATRIX& m, const _VECTOR& v) {
    return {((m.m[0][0] * v.x + m.m[1][0] * v.y) + m.m[2][0] * v.z) + m.m[3][0],
            ((m.m[0][1] * v.x + m.m[1][1] * v.y) + m.m[2][1] * v.z) + m.m[3][1],
            ((m.m[0][2] * v.x + m.m[1][2] * v.y) + m.m[2][2] * v.z) + m.m[3][2], v.w};
}
_VECTOR rotate_vector(const MATRIX& m, const _VECTOR& v) {
    return {(m.m[0][0] * v.x + m.m[1][0] * v.y) + m.m[2][0] * v.z,
            (m.m[0][1] * v.x + m.m[1][1] * v.y) + m.m[2][1] * v.z,
            (m.m[0][2] * v.x + m.m[1][2] * v.y) + m.m[2][2] * v.z, v.w};
}

// Corpo comum de Intersect_RayBox e ASH_Intersect_RayBox (só a constante muda, e é a mesma).
uint32_t ray_box(const HITTEST_tag* test, const float* min, const float* max, float* t_out) {
    const _VECTOR& s = test->start;
    const _VECTOR& d = test->dir;
    if (min[0] <= s.x && s.x <= max[0] && min[1] <= s.y && s.y <= max[1] && min[2] <= s.z && s.z <= max[2]) {
        *t_out = 0.0f;
        return 1;
    }
    float near_x, far_x;
    if (d.x == 0.0f) {
        if (s.x < min[0]) return 0;
        if (max[0] < s.x) return 0;
        far_x = kBig;
        near_x = -3.4e37f;
    } else {
        const float a = (min[0] - s.x) / d.x, b = (max[0] - s.x) / d.x;
        far_x = b < a ? a : b;
        near_x = b < a ? b : a;
        if (far_x < 0.0f) return 0;
    }
    float near_z, far_z;
    if (d.z == 0.0f) {
        if (s.z < min[2]) return 0;
        if (max[2] < s.z) return 0;
        far_z = kBig;
        near_z = -3.4e37f;
    } else {
        const float a = (min[2] - s.z) / d.z, b = (max[2] - s.z) / d.z;
        far_z = b < a ? a : b;
        if (far_z < 0.0f) return 0;
        near_z = b < a ? b : a;
    }
    float near_y = -3.4e37f, far_y;
    if (d.y == 0.0f) {
        if (s.y < min[1]) return 0;
        far_y = kBig;
        if (max[1] < s.y) return 0;
    } else {
        const float b = (max[1] - s.y) / d.y, a = (min[1] - s.y) / d.y;
        far_y = b < a ? a : b;
        near_y = b < a ? b : a;
        if (far_y < 0.0f) return 0;
    }
    float t_near = near_y;
    if (t_near < near_x) {
        t_near = near_x;
        if (near_x <= near_z) t_near = near_z;
    } else if (t_near <= near_z) {
        t_near = near_z;
    }
    float t_far = far_y;
    if (far_x < t_far) {
        t_far = far_x;
        if (far_z <= far_x) t_far = far_z;
    } else if (far_z <= t_far) {
        t_far = far_z;
    }
    if (!(t_near <= t_far)) return 0;
    *t_out = t_near;
    return (0.0f <= t_near && t_near <= test->radius) ? 1 : 0;
}

// Fim comum de Collide_SphereIntersect e Collide_RayIntersect: devolve ao HitHeap os acertos
// descartados (dist == 1e8) e ordena os outros pela distância.
void sort_hits(P32<HITDATA_tag>* head) {
    HITDATA_tag* h = *head;
    if (h == nullptr) return;
    uint32_t n = 0;
    while (h != nullptr) {
        HITDATA_tag* next = h->next;
        if (h->dist == kNoHit) {
            LList_Add(&HitHeap, reinterpret_cast<LLNODE_tag*>(h));
        } else if (n < 1100) {
            SrtList[n++] = h;
        }
        h = next;
    }
    if (n == 0) {
        *head = nullptr;
        return;
    }
    QuickSort(SrtList, n, 4, COMP_HITDATA, nullptr);
    HITDATA_tag* prev = SrtList[0];
    *head = prev;
    prev->prev = nullptr;
    for (uint32_t i = 1; i < n; ++i) {
        prev->next = SrtList[i];
        SrtList[i]->prev = prev;
        prev = SrtList[i];
    }
    prev->next = nullptr;
}

// Fase fina sobre a lista do teste, comum às duas buscas.
void narrow_phase(HITTEST_tag* test, obj_tag* tester, int8_t only_cel, uint16_t flags) {
    HITDATA_tag* h = tester->hits;
    while (h != nullptr) {
        if (h->cel == nullptr || h->cel == test->cel || only_cel == 0) {
            const uint32_t r = Collide_Intersect(tester->hit_test, h, flags);
            h = h->next;
            if (r != 0 && (flags & 0x10) != 0) {
                for (; h != nullptr; h = h->next) h->dist = kNoHit;
                break;
            }
        } else {
            h->dist = kNoHit;
            h = h->next;
        }
    }
    sort_hits(&tester->hits);
}

}  // namespace

void collide_debug_power_on() {
    std::memset(&HitHeap, 0, sizeof(HitHeap));
    HitAllocCnt = 0;
    TriAllocCnt = 0;
    TriHeap = nullptr;
    TriOnCnt = 0;
    BoxCnt = 0;
    std::memset(&DeltaP, 0, sizeof(DeltaP));
    Ident = 0;
    g_collide_guards = {};
}

// [decomp] 0x001edda0 Coll_ResetHitHeap
void Coll_ResetHitHeap() {
    HitAllocCnt = 0;
    LList_Init(&HitHeap, 0x60, 0);
    TriAllocCnt = 100;
    TriHeap = Mem_Malloc(0x640, 0x1a04, 0);
    std::memset(TriHeap, 0, size_t(TriAllocCnt) << 4);
}

// [decomp] 0x001ede68 Coll_AddHitToList
// Pega um nó do HitHeap (alocando de 64 em 64, até 1000) e o põe no começo da lista.
HITDATA_tag* Coll_AddHitToList(P32<HITDATA_tag>* list, float dist, cel_tag* cel, obj_tag* obj) {
    P32<HITDATA_tag> hit;
    for (;;) {
        hit = reinterpret_cast<HITDATA_tag*>(LList_Cut(&HitHeap));
        if (hit != nullptr) {
            std::memset(hit, 0, 0x60);
            break;
        }
        if (HitAllocCnt > 1000) {
            // Divergência: o original seguiria com um nó nulo.
            ++g_collide_guards.hit_heap_empty;
            return nullptr;
        }
        HitAllocCnt += 0x40;
        LList_AllocnNodes(&HitHeap, 0x40);
    }
    hit->dist = dist;
    if (*list == nullptr) {
        *list = hit;
    } else {
        hit->next = *list;
        hit->prev = nullptr;
        (*list)->prev = hit;
        *list = hit;
    }
    hit->cel = cel;
    hit->obj = obj;
    return hit;
}

// [decomp] 0x001ede00 Collide_FreeHitList
void Collide_FreeHitList(P32<HITDATA_tag>* list) {
    if (list == nullptr) return;
    for (HITDATA_tag* h = *list; h != nullptr;) {
        HITDATA_tag* next = h->next;
        LList_Add(&HitHeap, reinterpret_cast<LLNODE_tag*>(h));
        h = next;
    }
    *list = nullptr;
}

// [decomp] 0x001eac98 Collide_StraddleCels
// Salas que o volume do teste alcança: busca em largura pelos portais que ele cruza, a partir da
// sala do teste (que entra por último na lista). Cada portal é visitado uma vez por busca.
cel_tag** Collide_StraddleCels(HITTEST_tag* test, uint16_t* count) {
    cel_tag* start = test->cel;
    *count = 0;
    ++GameState.stamp;
    uint32_t n = 0;
    uint32_t next_cel = 0;
    if (n < 0x7e && start != nullptr) {
        portal_tag* p = start->portals;
        for (;;) {
            if (p != nullptr && n < 0x7e) {
                for (;;) {
                    if (p->stamp != GameState.stamp) {
                        p->stamp = GameState.stamp;
                        p->opposite->stamp = GameState.stamp;
                        if (Intersect_Portal(test, p) != 0) {
                            uint16_t k = 0;
                            while (k < *count && CelList_194[k] != p->cel) ++k;
                            if (k == *count) CelList_194[(*count)++] = p->cel;
                        }
                    }
                    p = p->next;
                    n = *count;
                    if (p == nullptr || n > 0x7d) break;
                }
            }
            cel_tag* c = nullptr;
            if (next_cel < n) c = CelList_194[next_cel++];
            if (n > 0x7d || c == nullptr) break;
            p = c->portals;
        }
    }
    CelList_194[(*count)++] = start;
    return CelList_194;
}

// [decomp] 0x001ea7b8 Intersect_Portal
// O volume do teste cruza o portal? Raio: contra os dois triângulos do portal (reta infinita, como
// no original). Esfera: caixa do portal e distância aos triângulos. Cápsula: ponto mais próximo do
// segmento ao plano.
uint32_t Intersect_Portal(HITTEST_tag* test, portal_tag* portal) {
    const uint16_t type = test->type;
    const plane_equ_tag& plane = portal->plane;
    const auto tri = [&](int i, int k) { return &portal->verts[Index_189[i * 3 + k]]; };
    float r;
    if (type < 0x202) {
        if (type >= 0x200) {
            const _VECTOR dir = sub(test->end, test->start);
            for (int i = 0; i < 2; ++i) {
                float t;
                if (Collide_RayTriangle(&test->start, &dir, tri(i, 0), tri(i, 1), tri(i, 2), &t) != 0) return 1;
            }
            return 0;
        }
        if (type > 0x101 || type < 0x100) return 0;
        r = test->radius;
    } else {
        if (type == 0x800) {
            _VECTOR closest;
            const float den = plane_dot(plane, test->dir);
            if (den == 0.0f) {
                vecutil_calculate_closest_point_on_line(&closest, &test->start, &test->end, &portal->center);
            } else {
                const float num = plane_eval(test->start, plane);
                float t = -num / den;
                t = 0.0f <= t ? (t <= 1.0f ? t : 1.0f) : 0.0f;
                closest = madd(test->start, test->dir, t);
            }
            for (int i = 0; i < 2; ++i) {
                uint16_t code;
                const float d = vecutil_Dist2Tri(&closest, tri(i, 0), tri(i, 1), tri(i, 2), &plane, &code, nullptr);
                if (std::fabs(d) < test->radius * 1.5f) return 1;
            }
            return 0;
        }
        if (type < 0x801) {
            if (type != 0x400) return 0;
        } else if (type != 0x1000) {
            return 0;
        }
        r = test->radius;
    }
    const float bmin[3] = {portal->box_min.x, portal->box_min.y, portal->box_min.z};
    const float bmax[3] = {portal->box_max.x, portal->box_max.y, portal->box_max.z};
    if (!sphere_overlaps_box(test->pos, r, bmin, bmax)) return 0;
    for (int i = 0; i < 2; ++i) {
        uint16_t code;
        const float d = vecutil_Dist2Tri(&test->pos, tri(i, 0), tri(i, 1), tri(i, 2), &plane, &code, nullptr);
        if (std::fabs(d) < test->radius) return 1;
    }
    return 0;
}

// [decomp] 0x001eb320 Collide_Pick
// Fase larga: para cada sala alcançada, testa as células dentro dela (máscara 0x80 pula), os
// objetos (0x10 pula) e a própria sala (0x40 pula); no fim, os objetos da ForcedList.
void Collide_Pick(HITTEST_tag* test) {
    obj_tag* tester = test->tester;
    if (test->cel == nullptr) return;
    ++GameState.stamp;
    if ((test->type & 0x1100) != 0) {
        test->sphere = test->pos;
        test->sphere.w = test->radius;
    }
    if ((test->type & 0x200) != 0) {
        test->dir = sub(test->end, test->start);
        const float len = Vec_Magnitude(&test->dir);
        test->radius = 1.0f < len ? len : 1.0f;
    }
    if ((test->type & 0x800) != 0) test->dir = sub(test->end, test->start);
    uint16_t count = 0;
    cel_tag** cels = Collide_StraddleCels(test, &count);
    HITTEST_tag copy;
    amemcpy(&copy, test, 0xa0);
    for (uint16_t i = 0; i < count; ++i) {
        cel_tag* room = cels[i];
        if ((test->mask & 0x80) == 0) {
            for (cel_tag* c = room->children; c != nullptr; c = c->children) {
                celglist_tag* g = c->glist;
                if (g == nullptr || (c->glist_flags & 0x20) != 0 || g->coll == nullptr) continue;
                if ((c->glist_flags & 0x1000) != 0 && (test->mask & 0x20) != 0) continue;
                const uint16_t type = test->type;
                bool sphere_test = false;
                if (type < 0x202) {
                    if (type >= 0x200) {
                        if ((c->type & 0x8000000) == 0) {
                            _VECTOR closest;
                            const _VECTOR center{c->sphere.x, c->sphere.y, c->sphere.z, c->sphere.w};
                            vecutil_calculate_closest_point_on_line(&closest, &copy.start, &copy.end, &center);
                            const float d2 = Vec_SqDist3D(&center, &closest);
                            if (d2 < center.w * center.w) Coll_AddHitToList(&tester->hits, d2, c, nullptr);
                        } else {
                            float t;
                            if (Intersect_RayBox(test, &c->box_min, &c->box_max, &t) != 0) {
                                HITDATA_tag* h = Coll_AddHitToList(&tester->hits, t, c, nullptr);
                                if (h != nullptr) h->dir = test->dir;
                            }
                        }
                        continue;
                    }
                    if (type < 0x100 || type > 0x101) continue;
                    sphere_test = true;
                } else if (type == 0x800) {
                    _VECTOR closest;
                    const _VECTOR center{c->sphere.x, c->sphere.y, c->sphere.z, c->sphere.w * 1.5f};
                    vecutil_calculate_closest_point_on_line(&closest, &copy.start, &copy.end, &center);
                    const float d2 = Vec_SqDist3D(&center, &closest);
                    const float reach = center.w + copy.radius;
                    if (d2 < reach * reach) Coll_AddHitToList(&tester->hits, d2, c, nullptr);
                    continue;
                } else if (type == 0x1000) {
                    sphere_test = true;
                }
                if (!sphere_test) continue;
                if ((c->type & 0x8000000) == 0) {
                    const _VECTOR delta = sub(test->sphere, c->sphere);
                    const float d2 = Vec_SqMagnitude(&delta);
                    const float reach = test->sphere.w + c->sphere.w;
                    if (d2 < reach * reach) Coll_AddHitToList(&tester->hits, d2, c, nullptr);
                } else {
                    const float bmin[3] = {c->box_min.x, c->box_min.y, c->box_min.z};
                    const float bmax[3] = {c->box_max.x, c->box_max.y, c->box_max.z};
                    if (sphere_overlaps_box(test->sphere, test->sphere.w, bmin, bmax)) {
                        Coll_AddHitToList(&tester->hits, 1.0f, c, nullptr);
                    }
                }
            }
        }
        if ((test->mask & 0x10) == 0) {
            for (obj_tag* o = room->objects; o != nullptr; o = o->cel_next) {
                if ((o->flags & 0x30000000) == 0) Collide_PickObj(test, o, tester);
            }
        }
        if ((test->mask & 0x40) == 0 && (room->glist_flags & 0x20) == 0 && room->glist != nullptr) {
            Coll_AddHitToList(&tester->hits, float(i) * 0.1f - 1000.0f, room, nullptr);
        }
    }
    if ((test->mask & 0x10) == 0) {
        for (LLNODE_tag* n = ForcedList.first; n != nullptr && n != n->next; n = n->next) {
            Collide_PickObj(test, reinterpret_cast<obj_tag*>(n), tester);
        }
    }
}

// [decomp] 0x001eae80 Collide_PickObj
// Fase larga contra um objeto. Pula o próprio testador, os dois ignorados, os objetos que a máscara
// do teste exclui (por flags do objeto ou por tipo) e, fora a esfera de um lado (0x100), os que não
// têm colisão (nem modelo com colldata nem o bit 8 de mode, dos articulados). Depois compara a esfera
// do objeto com a esfera, o segmento ou a cápsula do teste e põe o candidato na lista do testador.
//
// Peculiaridade mantida: raios e cápsulas copiam a esfera do objeto para test->sphere (a da cápsula
// com o raio vezes 1,5), e as esferas deixam a diferença dos centros em test->plane.
void Collide_PickObj(HITTEST_tag* test, obj_tag* obj, obj_tag* tester) {
    if (tester == obj || obj == test->ignore_a || obj == test->ignore_b) return;
    if (test->tester != nullptr && (test->tester->unknown_fd & 1) != 0 && (obj->unknown_fd & 2) != 0) return;
    const uint16_t mask = test->mask;
    uint32_t skip = (mask & 0x20) != 0 ? 0x1020u : 0x20u;
    if ((mask & 8) != 0) skip |= 0x40;
    if ((mask & 4) != 0) skip |= 0x10;
    if ((obj->flags & skip) != 0) return;
    if ((mask & 0x400) != 0 && (obj->type == 3 || obj->type == 0x12)) return;
    if ((mask & 0x100) != 0 && (obj->type == 2 || obj->type == 0x11)) return;
    if ((mask & 0x200) != 0 && obj->type == 5) return;
    const uint16_t type = test->type;
    if (type != 0x100 && (obj->mode & 8) == 0 && (obj->glist == nullptr || obj->glist->coll == nullptr)) return;
    if (type < 0x202) {
        if (type >= 0x200) {
            test->sphere = obj->sphere;
            _VECTOR closest;
            vecutil_calculate_closest_point_on_line(&closest, &test->start, &test->end, &test->sphere);
            const float d2 = Vec_SqDist3D(&test->sphere, &closest);
            if (!(d2 < test->sphere.w * test->sphere.w)) return;
            HITDATA_tag* h = Coll_AddHitToList(&tester->hits, d2, nullptr, obj);
            if (h != nullptr) h->dir = test->dir;
            return;
        }
        if (type > 0x101 || type < 0x100) return;
    } else if (type == 0x800) {
        test->sphere = obj->sphere;
        test->sphere.w = obj->sphere.w * 1.5f;
        _VECTOR closest;
        vecutil_calculate_closest_point_on_line(&closest, &test->start, &test->end, &test->sphere);
        const float d2 = Vec_SqDist3D(&test->sphere, &closest);
        const float reach = test->sphere.w + test->radius;
        if (!(d2 < reach * reach)) return;
        HITDATA_tag* h = Coll_AddHitToList(&tester->hits, d2, nullptr, obj);
        if (h != nullptr) {
            h->plane = test->plane;
            h->dir = {test->plane.a, test->plane.b, test->plane.c, test->plane.d};
        }
        return;
    } else if (type != 0x400 && type != 0x1000) {
        return;
    }
    const _VECTOR delta = sub(test->sphere, obj->sphere);
    test->plane = {delta.x, delta.y, delta.z, delta.w};
    const float d2 = Vec_SqMagnitude(&delta);
    const float reach = test->sphere.w + obj->sphere.w;
    if (d2 < reach * reach) Coll_AddHitToList(&tester->hits, d2, nullptr, obj);
}

// [decomp] 0x001e8ec0 Intersect_PointOOBox
// Esfera (p, radius) contra a caixa orientada m (eixos nas linhas 0-2, centro na linha 3) com meias
// medidas half: a projeção do centro em cada eixo tem de caber em half + radius.
uint32_t Intersect_PointOOBox(MATRIX* m, const _VECTOR* half, const _VECTOR* p, float radius) {
    const _VECTOR d{p->x - m->m[3][0], p->y - m->m[3][1], p->z - m->m[3][2], p->w - m->m[3][3]};
    _VECTOR axis;
    Mat_GetNorm(&axis, m);
    if (half->x + radius < std::fabs(dot3(axis, d))) return 0;
    Mat_GetUp(&axis, m);
    if (half->y + radius < std::fabs(dot3(axis, d))) return 0;
    Mat_GetDir(&axis, m);
    if (half->z + radius < std::fabs(dot3(axis, d))) return 0;
    return 1;
}

// [decomp] 0x001ec180 Collide_Jointy
// Fase fina contra um objeto articulado sem colisão própria (personagens). Raios (0x201) e esferas
// (0x101) testam as caixas orientadas das juntas (AnimGetCollData): o raio no espaço de cada junta,
// a esfera pela projeção nos eixos. A cápsula (0x800) só bate na cápsula do outro objeto: pega os
// pontos mais próximos dos dois segmentos e, se estão a menos da soma dos raios, empurra o teste
// para fora (tudo no jogo de um jogador, metade no multijogador, quando o outro também se afasta).
// O acerto não passa pelo espaço local: Collide_Intersect devolve o resultado direto.
//
// Peculiaridades mantidas: no raio, plane recebe a direção do raio no espaço da junta, negada; no
// jogo de um jogador o objeto de tipo 3 não é empurrado por ninguém; entre dois objetos de tipo 3
// (ou de tipo 2) só o mais rápido (ou o de maior valor em data + 0x474) empurra.
uint32_t Collide_Jointy(HITTEST_tag* test, HITDATA_tag* hit) {
    hit->dist = kNoHit;
    uint16_t count = 0;
    float t = kNoHit;
    HITTEST_tag copy;
    amemcpy(&copy, test, 0xa0);
    COLLJOINT_tag* joints = nullptr;
    if ((test->type & 0x300) != 0) joints = AnimGetCollData(hit->obj, &count);
    const uint16_t type = test->type;
    if (type == 0x201) {
        for (uint16_t i = 0; i < count; ++i) {
            COLLJOINT_tag* j = &joints[i];
            MATRIX inv;
            Mat_Inverse(&j->m, &inv);
            RotPreTransVec(&inv, &test->start, &copy.start);
            RotPreTransVec(&inv, &test->end, &copy.end);
            copy.dir = rotate_vector(inv, test->dir);
            _VECTOR lo{0.0f, 0.0f, 0.0f, 0.0f};
            Vec_Negate(&j->half, &lo);
            if (Intersect_RayBox(&copy, &lo, &j->half, &t) == 0) continue;
            hit->surface = 0;
            hit->unknown_52 = j->joint;
            hit->unknown_08 = test->unknown_90;
            if (t == 0.0f) {
                hit->dist = -1.0f;
                hit->pos = test->start;
            } else {
                hit->pos = madd(test->start, test->dir, t);
                hit->dist = test->radius * t;
            }
            Vec_Normalise(&copy.dir, &copy.dir);
            Vec_Negate(&copy.dir, reinterpret_cast<_VECTOR*>(&hit->plane));
            return 1;
        }
        return 0;
    }
    if (type == 0x101) {
        for (uint16_t i = 0; i < count; ++i) {
            COLLJOINT_tag* j = &joints[i];
            if (Intersect_PointOOBox(&j->m, &j->half, &test->pos, test->radius) == 0) continue;
            hit->surface = 0;
            hit->unknown_52 = j->joint;
            hit->dist = test->radius;
            hit->unknown_08 = test->unknown_90;
            hit->pos = test->pos;
            const _VECTOR center{j->m.m[3][0], j->m.m[3][1], j->m.m[3][2], j->m.m[3][3]};
            _VECTOR n = sub(test->pos, center);
            Vec_Normalise(&n, &n);
            hit->plane = {n.x, n.y, n.z, n.w};
            hit->dir = n;
            return 1;
        }
        return 0;
    }
    if (type != 0x800) return 0;
    obj_tag* other = hit->obj;
    const HITTEST_tag* ot = other->hit_test;
    if (ot == nullptr || ot->type != 0x800) return 0;
    const obj_tag* me = test->tester;
    if (me->type == 3 && other->type == 3) {
        const _VECTOR* my_vel = reinterpret_cast<const _VECTOR*>(static_cast<uint8_t*>(me->data) + 0x10);
        const _VECTOR* other_vel = reinterpret_cast<const _VECTOR*>(static_cast<uint8_t*>(other->data) + 0x10);
        if (Vec_Magnitude(my_vel) < Vec_Magnitude(other_vel)) return 0;
    }
    if (me->type == 2 && other->type == 2) {
        float mine, theirs;
        std::memcpy(&mine, static_cast<uint8_t*>(me->data) + 0x474, 4);
        std::memcpy(&theirs, static_cast<uint8_t*>(other->data) + 0x474, 4);
        if (mine < theirs) return 0;
    }
    float share = 0.5f;
    uint32_t multiplayer;
    std::memcpy(&multiplayer, MPSettings + 0x180, 4);
    if (multiplayer == 0) {
        share = 1.0f;
        if (other->type == 3) return 0;
    }
    _VECTOR p_other, p_mine;
    vecutil_calculate_closest_point_on_line(&p_other, &ot->start, &ot->end, &test->start);
    vecutil_calculate_closest_point_on_line(&p_mine, &test->start, &test->end, &p_other);
    vecutil_calculate_closest_point_on_line(&p_other, &ot->start, &ot->end, &p_mine);
    _VECTOR n = sub(p_other, p_mine);
    const float d2 = Vec_SqMagnitude(&n);
    const float reach = test->radius + ot->radius;
    if (reach * reach < d2) return 0;
    const float len = Vec_NormaliseLen(&n, &n);
    const float depth = (test->radius + ot->radius) - len;
    _VECTOR push{0.0f, 0.0f, 0.0f, 0.0f};
    Vec_MulR32(&push, &n, -depth * share);
    test->start = add(test->start, push);
    test->end = add(test->end, push);
    test->push = add(test->push, push);
    hit->plane = {n.x, n.y, n.z, n.w};
    hit->dist = depth;
    hit->pos.x = test->tester->pos.x + n.x * depth * share;
    hit->pos.z = test->tester->pos.z + n.z * depth * share;
    hit->pos.y = test->tester->pos.y + n.y * depth * share;
    return 1;
}

// [decomp] 0x001ebaa8 Collide_Intersect
// Fase fina de um candidato: leva o teste para o espaço local da célula (ou do objeto), testa a
// geometria e traz o contato, a normal e o empurrão de volta para o mundo.
uint32_t Collide_Intersect(HITTEST_tag* test, HITDATA_tag* hit, uint16_t flags) {
    Ident = 0;
    MATRIX m;
    if (hit->cel == nullptr) {
        obj_tag* obj = hit->obj;
        if (obj == nullptr || obj->glist == nullptr) return 0;
        if ((obj->flags & 0x40) != 0) hit->flags |= 2;
        if (test->type != 0x100 && (obj->mode & 8) != 0 && obj->glist->coll == nullptr) {
            return Collide_Jointy(test, hit);
        }
        Mat_Copy(&obj->matrix, &m);
        test->glist = obj->glist;
        if ((obj->unknown_fd & 4) != 0 && test->type == 0x101) {
            // Esfera contra esfera do objeto.
            DeltaP = sub(test->sphere, obj->sphere);
            const float len = Vec_NormaliseLen(reinterpret_cast<_VECTOR*>(&hit->plane), &DeltaP);
            hit->dist = len;
            hit->pos = madd(test->sphere, DeltaP, 0.5f);
            const float d = (test->sphere.w + obj->sphere.w) - len;
            hit->dist = d;
            test->push = madd(test->push, hit->plane, d);
            return 1;
        }
    } else {
        cel_tag* cel = hit->cel;
        Ident = uint8_t((cel->type >> 27) & 1);
        if ((cel->glist_flags & 0x40) != 0) hit->flags |= 2;
        if (Ident == 0) {
            RotTransMatrix(&cel->rot, &cel->pos, &m);
        } else {
            Mat_IdentityT(&m);
        }
        test->glist = cel->glist;
    }
    HITTEST_tag local;
    amemcpy(&local, test, 0xa0);
    local.matrix = &m;  // de novo local -> mundo quando os Intersect_* rodam (Intersect_CylTriGeom)
    MATRIX* pm = &m;
    Mat_Inverse(pm);
    if (Ident == 0) {
        RotPreTransVec(pm, &test->pos, &local.pos);
        RotPreTransVec(pm, &test->sphere, &local.sphere);
    } else {
        local.pos = test->pos;
        local.sphere = test->sphere;
    }
    hit->flags |= uint8_t(flags);
    hit->dist = test->radius;
    const uint16_t type = test->type;
    const auto local_ray = [&]() {
        if (Ident == 0) {
            RotPreTransVec(pm, &test->start, &local.start);
            RotPreTransVec(pm, &test->end, &local.end);
        } else {
            local.start = test->start;
            local.end = test->end;
        }
    };
    if (type == 0x400) {
        hit->dist = kNoHit;
    } else if (type == 0x101) {
        hit->dist = kNoHit;
        Mat_Inverse(pm);
        Intersect_PointGeom(&local, hit);
    } else if (type == 0x201) {
        hit->dist = kNoHit;
        local_ray();
        local.dir = sub(local.end, local.start);
        Mat_Inverse(pm);
        Intersect_RayGeom(&local, hit);
    } else if (type == 0x2000) {
        hit->dist = kNoHit;
        local_ray();
        Mat_Inverse(pm);
        Intersect_CylTriGeom(&local, hit);
        return 1;
    } else if (type == 0x800) {
        hit->dist = kNoHit;
        local_ray();
        Mat_Inverse(pm);
        Intersect_CylGeom(&local, hit);
    } else {
        if (hit->obj != nullptr) {
            hit->pos = hit->obj->sphere;
            const float d = std::sqrt(hit->dist) - (test->radius + hit->obj->sphere.w);
            hit->dist = 0.0f < d ? d : 0.0f;
        }
        return 1;
    }
    const uint8_t surface = hit->surface;
    const uint16_t mask = test->mask;
    if ((surface & 0x40) != 0 && (mask & 0x800) != 0) hit->dist = kNoHit;
    uint8_t f = hit->flags;  // só para o teste abaixo; o original não grava de volta
    if ((surface & 0x40) != 0) f |= 2;
    if ((surface & 0x80) != 0) f |= 2;
    if ((mask & 1) != 0 && uint32_t((surface & 0x3f) - 0xd) < 2) hit->dist = kNoHit;
    if ((mask & 2) != 0 && (surface & 0x3f) == 0x10) hit->dist = kNoHit;
    if ((mask & 8) != 0 && (f & 2) != 0) hit->dist = kNoHit;
    if (hit->dist == kNoHit) return 0;
    hit->unknown_08 = test->unknown_90;
    const MATRIX& w = *pm;
    if (Ident == 0) {
        const _VECTOR p = hit->pos;
        hit->pos.x = w.m[0][0] * p.x + w.m[1][0] * p.y + w.m[2][0] * p.z + w.m[3][0];
        hit->pos.y = w.m[0][1] * p.x + w.m[1][1] * p.y + w.m[2][1] * p.z + w.m[3][1];
        hit->pos.z = w.m[0][2] * p.x + w.m[1][2] * p.y + w.m[2][2] * p.z + w.m[3][2];
    }
    // O empurrão (DeltaP, em espaço local; só a esfera o atualiza) vai para o mundo e move o teste.
    _VECTOR delta = DeltaP;
    if (Ident == 0) {
        delta.x = w.m[0][0] * DeltaP.x + w.m[1][0] * DeltaP.y + w.m[2][0] * DeltaP.z;
        delta.y = w.m[0][1] * DeltaP.x + w.m[1][1] * DeltaP.y + w.m[2][1] * DeltaP.z;
        delta.z = w.m[0][2] * DeltaP.x + w.m[1][2] * DeltaP.y + w.m[2][2] * DeltaP.z;
    }
    test->push = add(delta, test->push);
    test->start = add(test->start, delta);
    test->end = add(test->end, delta);
    test->pos = add(test->pos, delta);
    test->sphere = add(test->sphere, delta);
    if (Ident == 0) {
        const plane_equ_tag n = hit->plane;
        hit->plane.a = w.m[0][0] * n.a + w.m[1][0] * n.b + w.m[2][0] * n.c;
        hit->plane.b = w.m[0][1] * n.a + w.m[1][1] * n.b + w.m[2][1] * n.c;
        hit->plane.c = w.m[0][2] * n.a + w.m[1][2] * n.b + w.m[2][2] * n.c;
    }
    test->found |= local.found;
    if ((test->type & 0x200) == 0) return 1;
    Vec_Normalise(&hit->dir, &test->dir);
    return 1;
}

// [decomp] 0x001edae0 RecurseBoxesSph
void RecurseBoxesSph(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* center, float radius) {
    if (!sphere_overlaps_box(*center, radius, box->min, box->max)) return;
    if (box->child_a < 0) {
        add_box(box, 0.0f);
    } else {
        RecurseBoxesSph(coll, boxes(coll) + box->child_a, center, radius);
        RecurseBoxesSph(coll, boxes(coll) + box->child_b, center, radius);
    }
}

// [decomp] 0x001e92d8 Intersect_PointGeom
// Esfera (sphere, em espaço local) contra os triângulos das folhas que ela toca. Cada triângulo a
// menos de "radius" empurra a esfera para fora pela normal (acumulando em DeltaP); o acerto fica
// com o triângulo mais próximo.
void Intersect_PointGeom(HITTEST_tag* test, HITDATA_tag* hit) {
    float best = test->radius;
    const uint8_t* best_vbase = nullptr;
    float rejected = 0.0f;
    const float min_dist = (test->flags & 8) == 0 ? 0.0f : -best;
    _VECTOR* center = &test->sphere;
    const float r = test->sphere.w;
    colldata_tag* cd = test->glist->coll;
    COLLBOX_tag* root = boxes(cd);
    BoxCnt = 0;
    if (sphere_overlaps_box(*center, r, root->min, root->max)) {
        if (root->child_a < 0) {
            BoxCnt = 1;
            ColBoxs[0].box = root;
        } else {
            RecurseBoxesSph(cd, boxes(cd) + root->child_a, center, r);
            RecurseBoxesSph(cd, boxes(cd) + root->child_b, center, r);
        }
    }
    if (BoxCnt == 0) {
        hit->dist = kNoHit;
        return;
    }
    int32_t best_tri = -1;
    _VECTOR best_pos{};
    Vec_Zero(&DeltaP);
    hit->dist = test->radius;
    const Tri* tris = triangles(cd);
    for (uint32_t b = 0; b < BoxCnt; ++b) {
        const COLLBOX_tag* box = ColBoxs[b].box;
        const uint8_t* vbase = cd->head + uint32_t(box->verts) * 0x40;
        for (int32_t t = box->child_b; t < box->tri_end; ++t) {
            // Peculiaridade mantida: com o filtro ligado, olha o material do melhor triângulo até
            // agora (no começo, o índice -1), não o do triângulo atual.
            if ((test->flags & 0x10) != 0 && (cd->rest[best_tri] & 0xc0) != 0) continue;
            const plane_equ_tag n = decode_normal(vbase + uint32_t(tris[t].normal) * 2, 6.103888e-05f);
            const float dist = plane_eval(*center, n);
            if (test->radius < dist) continue;
            if (!(min_dist < dist)) continue;
            _VECTOR v0 = decode_vertex(vbase + uint32_t(tris[t].v[0]) * 2, box);
            _VECTOR v1 = decode_vertex(vbase + uint32_t(tris[t].v[1]) * 2, box);
            _VECTOR v2 = decode_vertex(vbase + uint32_t(tris[t].v[2]) * 2, box);
            const float d = vecutil_Dist2Tri_PointGeom(center, &v0, &v1, &v2, &n, dist, &hit->pos);
            if (d < min_dist) continue;
            if (!(d <= test->radius)) continue;
            if ((hit->flags & 6) == 0) {
                // Peculiaridade mantida: a esfera anda o DeltaP acumulado, não só o desta vez.
                DeltaP = madd(DeltaP, n, test->radius - std::fabs(d));
                *center = add(*center, DeltaP);
            }
            if (!(d <= best)) continue;
            best_pos = hit->pos;
            best_tri = t;
            best = d;
            best_vbase = vbase;
            if ((test->flags & 0x10) != 0) {
                if (Collide_Filter(cd->rest[t], hit->flags, test->mask, &rejected)) continue;
                // Primeiro triângulo aceito: termina a busca.
                b = BoxCnt;
                break;
            }
        }
    }
    if (best_tri == -1) {
        hit->dist = kNoHit;
        return;
    }
    hit->dist = best;
    hit->surface = cd->rest[best_tri];
    hit->plane = decode_normal(best_vbase + uint32_t(tris[best_tri].normal) * 2, 6.103888e-05f);
    hit->pos = best_pos;
}

// [decomp] 0x00109ad0 ASH_Intersect_RayBox
uint32_t ASH_Intersect_RayBox(const HITTEST_tag* test, const float min[3], const float max[3], float* t) {
    return ray_box(test, min, max, t);
}

// [decomp] 0x0010af30 ASH_RecurseBoxesRay
void ASH_RecurseBoxesRay(colldata_tag* coll, COLLBOX_tag* box, const HITTEST_tag* test) {
    float t;
    if (ASH_Intersect_RayBox(test, box->min, box->max, &t) == 0) return;
    if (box->child_a < 0) {
        add_box(box, t);
    } else {
        ASH_RecurseBoxesRay(coll, boxes(coll) + box->child_a, test);
        ASH_RecurseBoxesRay(coll, boxes(coll) + box->child_b, test);
    }
}

// [decomp] 0x001e9008 Intersect_RayBox
uint32_t Intersect_RayBox(const HITTEST_tag* test, const _VECTOR* min, const _VECTOR* max, float* t) {
    const float a[3] = {min->x, min->y, min->z};
    const float b[3] = {max->x, max->y, max->z};
    return ray_box(test, a, b, t);
}

// [decomp] 0x001ed888 Collide_Filter
// Devolve true quando o material do triângulo descarta o acerto (e marca *dist com 1e8).
bool Collide_Filter(uint16_t surface, uint16_t hit_flags, uint32_t mask, float* dist) {
    if ((surface & 0x40) != 0 && (mask & 0x800) != 0) *dist = kNoHit;
    if ((surface & 0x40) != 0) hit_flags |= 2;
    uint16_t f = (surface & 0x80) != 0 ? uint16_t(hit_flags | 2) : hit_flags;
    if ((mask & 1) != 0 && uint32_t((surface & 0x3f) - 0xd) < 2) *dist = kNoHit;
    if ((mask & 2) != 0 && (surface & 0x3f) == 0x10) *dist = kNoHit;
    if ((mask & 8) != 0 && (f & 2) != 0) *dist = kNoHit;
    return *dist == kNoHit;
}

// [decomp] 0x0010a8c8 Intersect_RayGeom
// Segmento (start + dir * t, t em [0, 1], em espaço local) contra os triângulos das folhas que ele
// cruza; fica com o de menor t que esteja de frente para o raio. A normal aqui é s16 / 16384.
void Intersect_RayGeom(HITTEST_tag* test, HITDATA_tag* hit) {
    int32_t best_tri = -1;
    float best_t = 1.0f;
    colldata_tag* cd = test->glist->coll;
    const uint8_t* best_vbase = nullptr;
    float rejected = 0.0f;
    COLLBOX_tag* root = boxes(cd);
    BoxCnt = 0;
    float t_box;
    if (ASH_Intersect_RayBox(test, root->min, root->max, &t_box) != 0) {
        if (root->child_a < 0) {
            add_box(root, t_box);
        } else {
            ASH_RecurseBoxesRay(cd, boxes(cd) + root->child_a, test);
            ASH_RecurseBoxesRay(cd, boxes(cd) + root->child_b, test);
        }
    }
    if (BoxCnt == 0) {
        hit->dist = kNoHit;
        return;
    }
    const Tri* tris = triangles(cd);
    bool stop = false;
    for (uint32_t b = 0; b < BoxCnt && !stop; ++b) {
        const COLLBOX_tag* box = ColBoxs[b].box;
        // O original copia o bloco de vértices para o scratchpad (UseSPadColl); os dados são os mesmos.
        const uint8_t* vbase = cd->head + uint32_t(box->verts) * 0x40;
        for (int32_t t = box->child_b; t < box->tri_end; ++t) {
            const plane_equ_tag n = decode_normal(vbase + uint32_t(tris[t].normal) * 2, 1.0f / 16384.0f);
            test->plane = n;
            const float num = plane_eval(test->start, n);
            const float den = plane_dot(n, test->dir);
            if (0.0f <= den) continue;
            const float ray_t = -num / den;
            if (ray_t < 0.0f || 1.0f < ray_t) continue;
            const _VECTOR p = madd(test->start, test->dir, ray_t);
            test->pos = p;
            if (best_t <= ray_t) continue;
            const _VECTOR v0 = decode_vertex(vbase + uint32_t(tris[t].v[0]) * 2, box);
            const _VECTOR v1 = decode_vertex(vbase + uint32_t(tris[t].v[1]) * 2, box);
            const _VECTOR v2 = decode_vertex(vbase + uint32_t(tris[t].v[2]) * 2, box);
            if (dot3(sub(p, v0), cross(sub(v0, v2), n)) > 0.0f) continue;
            if (dot3(sub(p, v1), cross(sub(v1, v0), n)) > 0.0f) continue;
            if (dot3(sub(p, v2), cross(sub(v2, v1), n)) > 0.0f) continue;
            hit->pos = test->pos;
            best_tri = t;
            best_t = ray_t;
            best_vbase = vbase;
            if ((test->flags & 0x10) != 0) {
                if (!Collide_Filter(cd->rest[t], hit->flags, test->mask, &rejected) || (cd->rest[t] & 0xc0) != 0) {
                    stop = true;
                    break;
                }
            }
        }
    }
    if (best_tri == -1) {
        hit->dist = kNoHit;
        return;
    }
    const _VECTOR d = sub(test->start, hit->pos);
    hit->dist = std::sqrt(dot3(d, d));
    hit->surface = cd->rest[best_tri];
    hit->plane = decode_normal(best_vbase + uint32_t(tris[best_tri].normal) * 2, 1.0f / 16384.0f);
}

// [decomp] 0x0010ae50 ASH_RecurseBoxesBox
void ASH_RecurseBoxesBox(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* min, const _VECTOR* max) {
    if (!box_overlaps(*min, *max, box)) return;
    if (box->child_a < 0) {
        push_box(box);
    } else {
        ASH_RecurseBoxesBox(coll, boxes(coll) + box->child_a, min, max);
        ASH_RecurseBoxesBox(coll, boxes(coll) + box->child_b, min, max);
    }
}

// [decomp] 0x001edc48 RecurseBoxesBox
// Igual a ASH_RecurseBoxesBox, com o teste de caixas escrito à mão (mesmas comparações).
void RecurseBoxesBox(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* min, const _VECTOR* max) {
    if (max->x < box->min[0] || box->max[0] < min->x || max->z < box->min[2] || box->max[2] < min->z ||
        max->y < box->min[1] || box->max[1] < min->y) {
        return;
    }
    if (box->child_a < 0) {
        push_box(box);
    } else {
        RecurseBoxesBox(coll, boxes(coll) + box->child_a, min, max);
        RecurseBoxesBox(coll, boxes(coll) + box->child_b, min, max);
    }
}

// [decomp] 0x00109770 ASH_vecutil_Dist2Tri
// Versão em VU0 de vecutil_Dist2Tri, com os vetores em registradores de 128 bits: mesmas contas e a
// mesma tabela de casos (código 99 atrás do plano, sem escrever *closest).
float ASH_vecutil_Dist2Tri(const _VECTOR* p, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c,
                           const plane_equ_tag* plane, uint16_t* code, _VECTOR* closest) {
    _VECTOR va = *a, vb = *b, vc = *c;
    return vecutil_Dist2Tri(p, &va, &vb, &vc, plane, code, closest);
}

// [decomp] 0x00109da0 Intersect_CylGeom
// Cápsula (segmento start-end com raio "radius", em espaço local) contra os triângulos das folhas
// que a caixa do segmento toca. Para cada triângulo, acha o ponto do segmento que mais se aproxima
// dele (onde o segmento cruza o plano, preso entre as pontas; perto do paralelo, o mais próximo do
// centroide) e a distância desse ponto ao triângulo. Mais perto que o raio, empurra o segmento para
// fora pela direção do ponto do triângulo ao do segmento (DeltaP acumula, como na esfera) e marca
// found bit 0 quando o contato é um piso para o eixo da cápsula. O acerto fica com o triângulo mais
// próximo; a normal sai em plane e o contrário dela em dir.
//
// Peculiaridades mantidas: a busca nas folhas para no primeiro triângulo cuja primeira coordenada x
// passa da caixa do segmento (o original conta com os triângulos em ordem de x); o teste "0,999 < den"
// usa o segmento sem normalizar; a distância que disputa o melhor acerto é a do ponto do segmento ao
// triângulo depois de ajustado (sem empurrão, a do ponto antes do ajuste).
void Intersect_CylGeom(HITTEST_tag* test, HITDATA_tag* hit) {
    float best = test->radius;
    colldata_tag* cd = test->glist->coll;
    DeltaP = {0.0f, 0.0f, 0.0f, 0.0f};
    const float grow = test->radius + 0.1f;
    const _VECTOR& s0 = test->start;
    const _VECTOR& e0 = test->end;
    _VECTOR lo{std::min(e0.x, s0.x) - grow, std::min(e0.y, s0.y) - grow, std::min(e0.z, s0.z) - grow, 0.0f};
    _VECTOR hi{std::max(e0.x, s0.x) + grow, std::max(e0.y, s0.y) + grow, std::max(e0.z, s0.z) + grow, 0.0f};
    BoxCnt = 0;
    collect_boxes(cd, lo, hi, ASH_RecurseBoxesBox);
    // Eixo da cápsula, do fim para o começo.
    _VECTOR up = sub(test->start, test->end);
    Vec_Normalise(&up, &up);
    if (BoxCnt == 0) {
        hit->dist = kNoHit;
        return;
    }
    const _VECTOR dir = sub(test->end, test->start);
    const Tri* tris = triangles(cd);
    int32_t best_tri = -1;
    const uint8_t* best_vbase = nullptr;
    float rejected = 0.0f;
    float dist = best;
    for (uint32_t b = 0; b < BoxCnt; ++b) {
        const COLLBOX_tag* box = ColBoxs[b].box;
        // O original copia o bloco de vértices para o scratchpad (UseSPadColl); os dados são os mesmos.
        const uint8_t* vbase = cd->head + uint32_t(box->verts) * 0x40;
        for (int32_t t = box->child_b; t < box->tri_end; ++t) {
            const _VECTOR v0 = decode_vertex(vbase + uint32_t(tris[t].v[0]) * 2, box);
            const _VECTOR v1 = decode_vertex(vbase + uint32_t(tris[t].v[1]) * 2, box);
            const _VECTOR v2 = decode_vertex(vbase + uint32_t(tris[t].v[2]) * 2, box);
            if (hi.x - v0.x < 0.0f) break;
            const _VECTOR tmin{std::min(std::min(v0.x, v1.x), v2.x), std::min(std::min(v0.y, v1.y), v2.y),
                               std::min(std::min(v0.z, v1.z), v2.z), 0.0f};
            const _VECTOR tmax{std::max(std::max(v0.x, v1.x), v2.x), std::max(std::max(v0.y, v1.y), v2.y),
                               std::max(std::max(v0.z, v1.z), v2.z), 0.0f};
            if (hi.x - tmin.x < 0.0f || hi.y - tmin.y < 0.0f || hi.z - tmin.z < 0.0f || tmax.x - lo.x < 0.0f ||
                tmax.y - lo.y < 0.0f || tmax.z - lo.z < 0.0f) {
                continue;
            }
            const plane_equ_tag n = decode_normal(vbase + uint32_t(tris[t].normal) * 2, 1.0f / 16384.0f);
            test->plane = n;
            const float den = plane_dot(n, dir);
            _VECTOR seg;  // ponto do segmento
            uint16_t code = 0;
            if (0.0002f < std::fabs(den)) {
                float s = -plane_eval(test->start, n) / den;
                s = 0.0f <= s ? (s <= 1.0f ? s : 1.0f) : 0.0f;
                // Segmento atravessando o plano de trás para a frente (ou o contrário) longe da ponta
                // que conta: o triângulo é ignorado.
                if (0.999f < den && 0.5f < s) continue;
                if (den < -0.999f && s < 0.5f) continue;
                seg = madd(test->start, dir, s);
                dist = ASH_vecutil_Dist2Tri(&seg, &v0, &v1, &v2, &test->plane, &code, &test->pos);
                if (code != 0 && code != 99) {
                    // O ponto do triângulo caiu numa aresta ou num vértice: o ponto do segmento passa a
                    // ser o mais próximo dele.
                    vecutil_calculate_closest_point_on_line(&seg, &test->start, &test->end, &test->pos);
                }
            } else {
                _VECTOR centroid{};
                Vec_Avg3(&v0, &v1, &v2, &centroid);
                vecutil_calculate_closest_point_on_line(&seg, &test->start, &test->end, &centroid);
                dist = ASH_vecutil_Dist2Tri(&seg, &v0, &v1, &v2, &test->plane, &code, &test->pos);
            }
            if (dist < 0.0f || code == 99) continue;
            if ((hit->flags & 6) == 0) {
                const _VECTOR away = sub(seg, test->pos);
                if (plane_dot(test->plane, away) <= 0.0f) continue;
                const float len = std::sqrt(dot3(away, away));
                _VECTOR unit{0.0f, 0.0f, 0.0f, 0.0f};
                if (len != 0.0f) {
                    const float inv = 1.0f / len;
                    unit = {away.x * inv, away.y * inv, away.z * inv, 0.0f};
                }
                dist = len;
                if (test->radius <= len) continue;
                const float depth = test->radius - len;
                const _VECTOR push{unit.x * depth, unit.y * depth, unit.z * depth, 0.0f};
                DeltaP = add(DeltaP, push);
                lo = add(lo, push);
                hi = add(hi, push);
                test->start = add(test->start, push);
                test->end = add(test->end, push);
                if (test->found == 0) {
                    // Piso para a cápsula: a normal segue o eixo (end -> start) e o contato fica além de
                    // end. O segundo teste (< 0,7, já normalizado) sempre passa depois do primeiro.
                    const float facing = plane_dot(test->plane, up);
                    _VECTOR below = sub(test->pos, test->end);
                    if (0.7f < facing && dot3(below, up) < 0.0f) {
                        const float blen = std::sqrt(dot3(below, below));
                        if (blen == 0.0f) {
                            below = {0.0f, 0.0f, 0.0f, 0.0f};
                        } else {
                            const float inv = 1.0f / blen;
                            below = {below.x * inv, below.y * inv, below.z * inv, below.w};
                        }
                        if (dot3(below, up) < 0.7f) test->found |= 1;
                    }
                }
            }
            if (!(dist < best)) continue;
            best = dist;
            hit->pos = test->pos;
            best_vbase = vbase;
            best_tri = t;
            if ((test->flags & 0x10) != 0) {
                if (!Collide_Filter(cd->rest[t], hit->flags, test->mask, &rejected) || (cd->rest[t] & 0xc0) != 0) {
                    goto done;
                }
            }
        }
    }
done:
    if (best_tri == -1) {
        hit->dist = kNoHit;
        return;
    }
    hit->dist = best;
    hit->surface = cd->rest[best_tri];
    hit->plane = decode_normal(best_vbase + uint32_t(tris[best_tri].normal) * 2, 1.0f / 16384.0f);
    hit->dir.x = 0.0f - hit->plane.a;
    hit->dir.y = 0.0f - hit->plane.b;
    hit->dir.z = 0.0f - hit->plane.c;
}

// [decomp] 0x001e9a70 Intersect_CylTriGeom
// Teste 0x2000: junta em TriHeap (no mundo, pela matriz da cópia local) os triângulos a até "radius"
// do segmento, sem empurrar nada e sem acerto (hit->dist fica em 1e8). Usado por
// Collide_PlaceObject. Sem espaço (mais de 500 vértices), para.
//
// Peculiaridade mantida: com o filtro de material ligado, olha o byte antes da tabela (rest[-1]).
void Intersect_CylTriGeom(HITTEST_tag* test, HITDATA_tag* hit) {
    colldata_tag* cd = test->glist->coll;
    Vec_Zero(&DeltaP);
    BoxCnt = 0;
    _VECTOR lo{}, hi{};
    Vec_MinMax(&test->start, &test->end, &lo, &hi);
    const float grow = test->radius + 0.05f;
    lo.x -= grow;
    hi.x += grow;
    lo.y -= grow;
    lo.z -= grow;
    hi.z += grow;
    hi.y += grow;
    collect_boxes(cd, lo, hi, RecurseBoxesBox);
    if (BoxCnt == 0) {
        hit->dist = kNoHit;
        return;
    }
    const _VECTOR dir = sub(test->end, test->start);
    const Tri* tris = triangles(cd);
    for (uint32_t b = 0; b < BoxCnt; ++b) {
        const COLLBOX_tag* box = ColBoxs[b].box;
        const uint8_t* vbase = cd->head + uint32_t(box->verts) * 0x40;
        for (int32_t t = box->child_b; t < box->tri_end; ++t) {
            const uint8_t surface = cd->rest[t];
            if ((test->mask & 1) != 0 && uint32_t((surface & 0x3f) - 0xd) < 2) continue;
            if ((test->mask & 2) != 0 && (surface & 0x3f) == 0x10) continue;
            if ((test->flags & 0x10) != 0 && (cd->rest[-1] & 0xc0) != 0) continue;
            _VECTOR v0 = decode_vertex(vbase + uint32_t(tris[t].v[0]) * 2, box);
            _VECTOR v1 = decode_vertex(vbase + uint32_t(tris[t].v[1]) * 2, box);
            _VECTOR v2 = decode_vertex(vbase + uint32_t(tris[t].v[2]) * 2, box);
            if (std::max(v2.x, std::max(v1.x, v0.x)) < lo.x || hi.x < std::min(v2.x, std::min(v1.x, v0.x)) ||
                std::max(v2.z, std::max(v1.z, v0.z)) < lo.z || hi.z < std::min(v2.z, std::min(v1.z, v0.z)) ||
                std::max(v2.y, std::max(v1.y, v0.y)) < lo.y || hi.y < std::min(v2.y, std::min(v1.y, v0.y))) {
                continue;
            }
            const plane_equ_tag n = decode_normal(vbase + uint32_t(tris[t].normal) * 2, 6.103888e-05f);
            test->plane = n;
            const float den = plane_dot(n, dir);
            _VECTOR seg{};
            if (0.05f < std::fabs(den)) {
                float s = -plane_eval(test->start, n) / den;
                s = 0.0f <= s ? (s <= 1.0f ? s : 1.0f) : 0.0f;
                seg = madd(test->start, dir, s);
            } else {
                if ((test->flags & 1) == 0) continue;
                _VECTOR centroid{};
                Vec_Avg3(&v0, &v1, &v2, &centroid);
                vecutil_calculate_closest_point_on_line(&seg, &test->start, &test->end, &centroid);
            }
            uint16_t code = 0;
            const float d = vecutil_Dist2Tri(&seg, &v0, &v1, &v2, &test->plane, &code, &test->pos);
            if (d < -0.0002f || test->radius < d) continue;
            if (!(TriOnCnt + 3 < uint32_t(TriAllocCnt))) {
                if (uint32_t(TriAllocCnt) > 500) return;
                const uint32_t size = uint32_t(TriAllocCnt + 100) << 4;
                void* grown = Mem_Malloc(size, 0x1a04, 0);
                if (grown == nullptr) {
                    // Divergência: o original seguiria com um ponteiro nulo.
                    ++g_collide_guards.tri_heap_alloc;
                    return;
                }
                std::memset(grown, 0, size);
                amemcpy(grown, TriHeap, uint32_t(TriAllocCnt) << 4);
                Mem_Free(&TriHeap);
                TriHeap = grown;
                TriAllocCnt += 100;
            }
            if (!(TriOnCnt + 3 < 501)) continue;
            uint8_t* out = static_cast<uint8_t*>(TriHeap) + TriOnCnt * 16;
            const _VECTOR* verts[3] = {&v0, &v1, &v2};
            for (int i = 0; i < 3; ++i) {
                const _VECTOR w = Ident == 0 ? transform_point(*test->matrix, *verts[i]) : *verts[i];
                std::memcpy(out + i * 16, &w, 16);
            }
            TriOnCnt += 3;
        }
    }
}

// [decomp] 0x001ec940 Collide_SphereIntersect
// Esfera em *pos. Com only_cel, só a geometria de "cel" (e objetos) entra na fase fina. No fim,
// *pos anda o empurrão acumulado e *hits recebe os acertos ordenados pela distância.
bool Collide_SphereIntersect(_VECTOR* pos, float radius, const cel_tag* cel, const obj_tag* a, const obj_tag* b,
                             P32<HITDATA_tag>* hits, int8_t only_cel, int8_t one_sided, uint32_t mask, uint16_t flags) {
    *hits = nullptr;
    HITTEST_tag test;
    std::memset(&test, 0, sizeof(test));
    obj_tag tester;
    std::memset(&tester, 0, sizeof(tester));
    test.type = one_sided != 0 ? 0x100 : 0x101;
    test.pos = *pos;
    tester.hits = *hits;
    test.flags = flags;
    test.tester = &tester;
    test.ignore_a = const_cast<obj_tag*>(a);
    test.ignore_b = const_cast<obj_tag*>(b);
    test.cel = const_cast<cel_tag*>(cel);
    test.radius = radius;
    test.mask = uint16_t(mask);
    tester.hit_test = &test;
    Collide_Pick(&test);
    narrow_phase(&test, &tester, only_cel, flags);
    *pos = add(*pos, test.push);
    *hits = tester.hits;
    return tester.hits != nullptr;
}

// [decomp] 0x001ec690 Collide_RayIntersect
bool Collide_RayIntersect(const _VECTOR* from, const _VECTOR* to, const cel_tag* cel, const obj_tag* a,
                          const obj_tag* b, P32<HITDATA_tag>* hits, int8_t only_cel, uint32_t mask, uint16_t flags) {
    *hits = nullptr;
    HITTEST_tag test;
    std::memset(&test, 0, sizeof(test));
    obj_tag tester;
    std::memset(&tester, 0, sizeof(tester));
    test.start = *from;
    tester.hits = *hits;
    test.end = *to;
    test.type = 0x201;
    test.radius = -10.0f;
    test.tester = &tester;
    test.ignore_a = const_cast<obj_tag*>(a);
    test.ignore_b = const_cast<obj_tag*>(b);
    test.cel = const_cast<cel_tag*>(cel);
    test.mask = uint16_t(mask);
    tester.hit_test = &test;
    Collide_Pick(&test);
    narrow_phase(&test, &tester, only_cel, flags);
    *hits = tester.hits;
    return tester.hits != nullptr;
}

// [decomp] 0x001ecc00 Collide_CylinderIntersect
// A cápsula do teste dado (start, end e radius já montados por quem chama; o tipo vira 0x800) contra a geometria, com
// um objeto temporário de testador: fase larga (Collide_Pick) e fina como em Collide_SphereIntersect (com only_cel, só
// a sala do teste e os objetos), acertos ordenados pela distância em *hits. O empurrão fica em test->push (os pontos
// andam com ele: Intersect_CylGeom). Só RB_RestingContactRot chama (os pêndulos dos corpos rígidos).
// Divergência: o original deixa test->tester apontando para o testador da pilha depois de voltar; aqui volta ao que
// era (o dono do teste, que Collide_Update põe a cada tick).
bool Collide_CylinderIntersect(HITTEST_tag* test, P32<HITDATA_tag>* hits, int8_t only_cel, uint16_t flags) {
    *hits = nullptr;
    obj_tag tester;
    std::memset(&tester, 0, sizeof(tester));
    obj_tag* const owner = test->tester;
    test->tester = &tester;
    test->type = 0x800;
    tester.hits = *hits;
    tester.hit_test = test;
    Collide_Pick(test);
    narrow_phase(test, &tester, only_cel, flags);
    *hits = tester.hits;
    test->tester = owner;
    return tester.hits != nullptr;
}

// [decomp] 0x001ecfb8 Collide_PlaceObject
// Cabe um objeto de raio "radius" e meia largura "extent" aqui? (GT_DeployMiniGun: a metralhadora 0x52 montada.) Sobe
// pos 5 cm e procura o chão num raio de até 3 m para baixo do "cima" da matriz (máscara 0x1d); sem chão, não. A normal
// do chão vira o novo "cima"; pos vai para o ponto do chão + normal x radius. Uma cápsula de largura extent, de pos até
// 10 cm abaixo dele, junta os triângulos em volta (tipo 0x2000, TriHeap; até 0xa6): se algum plano passa a menos de
// radius - 0,13 de pos, não cabe; os quase paralelos ao chão (normal a 0,96 da do chão) até 0,2 além de radius
// estendem a caixa do chão. A matriz é alinhada ao chão (Mat_Align2Up com a direção dela) e a caixa tem de ir pelo
// menos extent (|lado| + |frente|) do ponto nos dois cantos.
bool Collide_PlaceObject(_VECTOR* pos, MATRIX* m, float radius, float extent, obj_tag* obj) {
    pos->y = pos->y + 0.05f;
    alignas(16) _VECTOR dir, up;
    Mat_GetDir(&dir, m);
    Mat_GetUp(&up, m);
    cel_tag* cel = build_FindCel(pos, glb_world);
    alignas(16) _VECTOR below{pos->x + up.x * -3.0f, pos->y + up.y * -3.0f, pos->z + up.z * -3.0f, pos->w};
    P32<HITDATA_tag> hits = nullptr;
    if (!Collide_RayIntersect(pos, &below, cel, obj, nullptr, &hits, 0, 0x1d, 0)) return false;
    std::memcpy(&up, &hits->plane, sizeof(up));
    Vec_Normalise(&up, &up);
    below = {pos->x + up.x * -0.1f, pos->y + up.y * -0.1f, pos->z + up.z * -0.1f, pos->w};
    *pos = {hits->pos.x + up.x * radius, hits->pos.y + up.y * radius, hits->pos.z + up.z * radius, hits->pos.w};
    alignas(16) _VECTOR floor_n;
    std::memcpy(&floor_n, &hits->plane, sizeof(floor_n));
    Vec_Normalise(&floor_n, &floor_n);
    cel_tag* hit_cel = hits->cel;
    Collide_FreeHitList(&hits);
    alignas(16) _VECTOR lo, hi;
    Vec_Copy2(pos, &lo, &hi);
    HITTEST_tag test;
    std::memset(&test, 0, sizeof(test));
    obj_tag tester;
    std::memset(&tester, 0, sizeof(tester));
    test.type = 0x800;
    test.cel = hit_cel;
    test.radius = extent;
    test.tester = &tester;
    test.mask = 0xd;
    test.flags = 1;
    test.ignore_a = nullptr;
    test.ignore_b = nullptr;
    tester.hit_test = &test;
    tester.hits = nullptr;
    if (test.cel == nullptr) test.cel = build_FindCel(pos, glb_world);
    TriOnCnt = 0;
    test.end = below;
    test.start = *pos;
    Collide_Pick(&test);
    test.type = 0x2000;
    for (HITDATA_tag* h = tester.hits; h != nullptr; h = h->next) Collide_Intersect(tester.hit_test, h, 1);
    Collide_FreeHitList(&tester.hits);
    uint32_t count = TriOnCnt / 3;
    if (0xa6 < count) count = 0xa6;
    const _VECTOR* tri = static_cast<const _VECTOR*>(TriHeap);
    for (uint32_t i = 0; i < count * 3; i += 3) {
        alignas(16) plane_equ_tag pl;
        Plane_PlaneEq(&pl, &tri[i], &tri[i + 1], &tri[i + 2]);
        const float d = (((pos->x * pl.a + pos->y * pl.b) + pos->z * pl.c) + pl.d);
        const float gap = std::fabs(d) - radius;
        if (gap < -0.13f) return false;
        if (0.2f < gap) continue;
        const float c = (pl.a * floor_n.x + pl.b * floor_n.y) + pl.c * floor_n.z;
        if (std::fabs(c) < 0.96f) continue;
        alignas(16) _VECTOR tmin, tmax;
        Vec_MinMax3(&tri[i], &tri[i + 1], &tri[i + 2], &tmin, &tmax);
        Vec_Min(&lo, &tmin, &lo);
        Vec_Max(&hi, &tmax, &hi);
    }
    Mat_Align2Up(m, &up, &dir);
    Mat_GetDir(&dir, m);
    alignas(16) _VECTOR norm;
    Mat_GetNorm(&norm, m);
    const _VECTOR a = sub(lo, *pos);
    const _VECTOR b = sub(hi, *pos);
    const auto reach = [&](const _VECTOR& v) {
        const float s = (v.x * norm.x + v.y * norm.y) + v.z * norm.z;
        const float f = (v.x * dir.x + v.y * dir.y) + v.z * dir.z;
        return std::fabs(s) + std::fabs(f);
    };
    if (reach(a) < extent) return false;
    return !(reach(b) < extent);
}

// [decomp] 0x001edf50 Collide_LineOfSight
// Nada da geometria (filtrada pelo material, mask | 4) entre from e to.
bool Collide_LineOfSight(const _VECTOR* from, const _VECTOR* to, const cel_tag* cel, const obj_tag* a,
                         const obj_tag* b, uint32_t mask) {
    P32<HITDATA_tag> hits = nullptr;
    if (!Collide_RayIntersect(from, to, cel, a, b, &hits, 0, mask | 4, 0x10)) return true;
    for (HITDATA_tag* h = hits; h != nullptr;) {
        HITDATA_tag* next = h->next;
        LList_Add(&HitHeap, reinterpret_cast<LLNODE_tag*>(h));
        h = next;
    }
    return false;
}

// [decomp] 0x001df7e0 vecutil_calculate_closest_point_on_line
// Ponto do segmento a-b mais próximo de p; devolve o parâmetro (0 a 1).
float vecutil_calculate_closest_point_on_line(_VECTOR* out, const _VECTOR* a, const _VECTOR* b, const _VECTOR* p) {
    const _VECTOR d = sub(*b, *a);
    const float len2 = dot3(d, d);
    if (std::fabs(len2) > 1e-20f) {
        const float t = dot3(d, sub(*p, *a)) / len2;
        if (1.0f < t) {
            *out = *b;
            return 1.0f;
        }
        if (0.0f <= t) {
            *out = madd(*a, d, t);
            return t;
        }
    }
    *out = *a;
    return 0.0f;
}

namespace {
// Ponto do triângulo mais próximo de p pelos lados de fora (código de bits por aresta), como em
// vecutil_Dist2Tri e vecutil_Dist2Tri_PointGeom.
_VECTOR closest_on_triangle(const _VECTOR* p, _VECTOR* a, _VECTOR* b, _VECTOR* c, const plane_equ_tag* n,
                            float dist, uint16_t* code_out) {
    _VECTOR* v[3] = {a, b, c};
    uint16_t code = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        const _VECTOR edge = sub(*v[i], *v[(i + 2) % 3]);
        if (dot3(sub(*p, *v[i]), cross(edge, *n)) > 0.0f) code = uint16_t(code | (1u << i));
        if (code > 1) break;
    }
    if (code_out != nullptr) *code_out = code;
    _VECTOR out;
    switch (code) {
        case 1: vecutil_calculate_closest_point_on_line(&out, a, c, p); break;
        case 2: vecutil_calculate_closest_point_on_line(&out, b, a, p); break;
        case 3: out = *a; break;
        case 4: vecutil_calculate_closest_point_on_line(&out, c, b, p); break;
        case 5: out = *c; break;
        case 6: out = *b; break;
        default: out = madd(*p, *n, -dist); break;
    }
    return out;
}
}  // namespace

// [decomp] 0x001dfca8 vecutil_Dist2Tri
// Distância de p ao triângulo. Atrás do plano devolve a distância negativa ao plano (código 99).
float vecutil_Dist2Tri(const _VECTOR* p, _VECTOR* a, _VECTOR* b, _VECTOR* c, const plane_equ_tag* plane, uint16_t* code,
                       _VECTOR* closest) {
    const float dist = plane_eval(*p, *plane);
    if (dist < 0.0f) {
        *code = 99;
        return dist;
    }
    const _VECTOR q = closest_on_triangle(p, a, b, c, plane, dist, code);
    if (closest != nullptr) *closest = q;
    const _VECTOR d = sub(q, *p);
    return std::sqrt(dot3(d, d));
}

// [decomp] 0x001dfac8 vecutil_Dist2Tri_PointGeom
// Como vecutil_Dist2Tri, mas com a distância ao plano já calculada e sem o caso de trás.
float vecutil_Dist2Tri_PointGeom(const _VECTOR* p, _VECTOR* a, _VECTOR* b, _VECTOR* c, const plane_equ_tag* plane,
                                 float dist, _VECTOR* closest) {
    const _VECTOR q = closest_on_triangle(p, a, b, c, plane, dist, nullptr);
    if (closest != nullptr) *closest = q;
    const _VECTOR d = sub(q, *p);
    return std::sqrt(dot3(d, d));
}

// [decomp] 0x001df9b8 vecutil_point_on_poly
// p (no plano) está dentro do triângulo? Mesma regra das arestas de closest_on_triangle.
uint32_t vecutil_point_on_poly(const _VECTOR* p, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c,
                               const plane_equ_tag* plane) {
    if (dot3(sub(*p, *a), cross(sub(*a, *c), *plane)) > 0.0f) return 0;
    if (dot3(sub(*p, *b), cross(sub(*b, *a), *plane)) > 0.0f) return 0;
    if (dot3(sub(*p, *c), cross(sub(*c, *b), *plane)) > 0.0f) return 0;
    return 1;
}

// [decomp] 0x001edfe0 Collide_RayTriangle
// Reta start + dir * t contra o triângulo (sem limitar t).
uint32_t Collide_RayTriangle(const _VECTOR* start, const _VECTOR* dir, const _VECTOR* a, const _VECTOR* b,
                             const _VECTOR* c, float* t) {
    plane_equ_tag plane;
    Plane_PlaneEq(&plane, a, b, c);
    const float den = plane_dot(plane, *dir);
    if (den == 0.0f) return 0;
    *t = -plane_eval(*start, plane) / den;
    const _VECTOR p = madd(*start, *dir, *t);
    return vecutil_point_on_poly(&p, a, b, c, &plane);
}

// [decomp] 0x001ea398 Collide_Update
// A colisão dos objetos no tick: devolve os acertos do tick anterior, testa cada objeto que tem um
// teste ativo (fase larga e fina), ordena os acertos pela distância e copia cada acerto contra outro
// objeto para a lista do outro (marcado com a flag 1), até o primeiro acerto que não deixa passar.
// Depois chama a função de colisão de cada tipo (control_funcs).
void Collide_Update(obj_tag* first) {
    for (obj_tag* o = first; o != nullptr; o = o->next) {
        Collide_FreeHitList(&o->hits);
        if (o->hit_test != nullptr) {
            o->hit_test->tester = o;
            Vec_Zero(&o->hit_test->push);
        }
    }
    for (obj_tag* o = first; o != nullptr; o = o->next) {
        HITTEST_tag* t = o->hit_test;
        if (t == nullptr || t->type == 0) continue;
        t->found = 0;
        Collide_Pick(t);
        for (HITDATA_tag* h = o->hits; h != nullptr;) {
            if ((h->flags & 1) != 0) {
                h = h->next;
                continue;
            }
            const uint32_t r = Collide_Intersect(t, h, t->flags);
            h = h->next;
            if (r != 0 && (t->flags & 0x10) != 0) {
                for (; h != nullptr; h = h->next) h->dist = kNoHit;
                break;
            }
        }
        sort_hits(&o->hits);
        for (HITDATA_tag* h = o->hits; h != nullptr; h = h->next) {
            obj_tag* other = h->obj;
            if (other != nullptr) {
                HITDATA_tag* m = Coll_AddHitToList(&other->hits, h->dist, nullptr, o);
                if (m != nullptr) {
                    m->flags |= 1;
                    m->unknown_08 = t->unknown_90;
                    m->surface = h->surface;
                    m->unknown_52 = h->unknown_52;
                    m->pos = h->pos;
                    m->plane = h->plane;
                    m->dir = h->dir;
                }
            }
            // Para no primeiro acerto sólido: passam os que têm a flag 2, material com o bit 0x40
            // ou, ao acaso, o bit 0x80; com a flag 0x20 do teste, o material 0x10 também passa.
            uint8_t pass = uint8_t(((h->flags >> 1) & 1) | ((h->surface >> 6) & 1));
            if ((h->surface & 0x80) != 0) pass = uint8_t(pass | (Rand_Rand(0x3f) & 1));
            if ((t->flags & 0x20) != 0 && h->surface == 0x10) pass = 1;
            if (pass == 0) break;
        }
    }
    for (obj_tag* o = first; o != nullptr; o = o->next) {
        if (control_funcs[o->type].collide != nullptr) control_funcs[o->type].collide(o);
    }
}

// [decomp] 0x001ece30 Collide_FilterBullets
// Marca como descartados (dist = 1e8) os acertos de projéteis (tipo 5) cuja arma tem, em
// weapon_definition_tag::stats (+0x10), algum bit da máscara; havendo algum, devolve os descartados ao
// HitHeap e ordena o resto pela distância (o mesmo fim de Collide_SphereIntersect, sort_hits).
void Collide_FilterBullets(P32<HITDATA_tag>* list, uint16_t mask) {
    bool any = false;
    for (HITDATA_tag* h = *list; h != nullptr; h = h->next) {
        const obj_tag* o = h->obj;
        if (o == nullptr || o->type != 5) continue;
        uint32_t weapon_addr;  // BU_tag::weapon
        std::memcpy(&weapon_addr, static_cast<const uint8_t*>(o->data) + 0x44, 4);
        const uint8_t* weapon = nf::p32_ptr<const uint8_t>(weapon_addr);
        uint16_t stats;
        std::memcpy(&stats, weapon + 0x10, 2);
        if ((stats & mask) == 0) continue;
        h->dist = kNoHit;
        any = true;
    }
    if (any) sort_hits(list);
}

// [decomp] 0x001ee220 Collide_GetDamageNObjects
// Soma o dano de todos os acertos (HITDATA_tag::unknown_08 como float) e, com objs e count, junta os
// objetos atingidos (até max; count começa em 0).
float Collide_GetDamageNObjects(HITDATA_tag* list, obj_tag** objs, uint16_t* count, uint16_t max) {
    float sum = 0.0f;
    if (count != nullptr) *count = 0;
    for (HITDATA_tag* h = list; h != nullptr; h = h->next) {
        float damage;
        std::memcpy(&damage, &h->unknown_08, 4);
        sum += damage;
        if (h->obj == nullptr || objs == nullptr || count == nullptr) continue;
        if (*count < max) objs[(*count)++] = h->obj;
    }
    return sum;
}

// [decomp] 0x001ed988 Intersect_SphereBox
// A esfera (centro c, raio r) toca a caixa [min, max]? Testa x, z e por último y.
uint32_t Intersect_SphereBox(const _VECTOR* c, float r, const _VECTOR* min, const _VECTOR* max) {
    if (max->x < c->x - r || c->x + r < min->x) return 0;
    if (max->z < c->z - r || c->z + r < min->z) return 0;
    if (max->y < c->y - r || c->y + r < min->y) return 0;
    return 1;
}

// [decomp] 0x001e0aa8 vecutil_intersect_line_line_2d
// Interseção dos segmentos p1-p2 e p3-p4 em planta (x, z), como o "lines_intersect" do Graphics
// Gems: rejeita pelas caixas, depois pelos sinais. 0 sem interseção, 1 colineares (out não é
// escrito), 2 com out.x e out.z no ponto (y e w ficam como estão).
uint32_t vecutil_intersect_line_line_2d(const _VECTOR* p1, const _VECTOR* p2, const _VECTOR* p3, const _VECTOR* p4,
                                        _VECTOR* out) {
    const float ax = p2->x - p1->x;
    const float bx = p3->x - p4->x;
    float x1lo, x1hi;
    if (ax < 0.0f) {
        x1lo = p2->x;
        x1hi = p1->x;
    } else {
        x1hi = p2->x;
        x1lo = p1->x;
    }
    if (0.0f < bx) {
        if (x1hi < p4->x || p3->x < x1lo) return 0;
    } else {
        if (x1hi < p3->x || p4->x < x1lo) return 0;
    }
    const float ay = p2->z - p1->z;
    const float by = p3->z - p4->z;
    float y1lo, y1hi;
    if (ay < 0.0f) {
        y1lo = p2->z;
        y1hi = p1->z;
    } else {
        y1hi = p2->z;
        y1lo = p1->z;
    }
    if (0.0f < by) {
        if (y1hi < p4->z || p3->z < y1lo) return 0;
    } else {
        if (y1hi < p3->z || p4->z < y1lo) return 0;
    }
    const float cx = p1->x - p3->x;
    const float cy = p1->z - p3->z;
    const float f = ay * bx - ax * by;
    const float d = by * cx - bx * cy;
    if (0.0f < f) {
        if (d < 0.0f || f < d) return 0;
    } else {
        if (0.0f < d || d < f) return 0;
    }
    const float e = ax * cy - ay * cx;
    if (0.0f < f) {
        if (e < 0.0f || f < e) return 0;
    } else {
        if (0.0f < e || e < f) return 0;
    }
    if (f <= 0.0002f && -0.0002f <= f) return 1;
    out->x = p1->x + (d * ax) / f;
    out->z = p1->z + (d * ay) / f;
    return 2;
}
