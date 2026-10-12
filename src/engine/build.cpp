// Port de build_alloc_*, Plane_PlaneEq e BIN_Get*. Ver build.h.

#include "engine/build.h"

#include <cmath>
#include <cstring>

#include "engine/memory.h"
#include "platform/psi.h"

namespace {
float bits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
float fmin2(float a, float b) { return b < a ? b : a; }
float fmax2(float a, float b) { return a < b ? b : a; }
}  // namespace

// [decomp] 0x001c93f8 build_alloc_cel
// Os campos não listados (0x00, 0x04, os w de 0x60/0x70 e 0xa4..0xaf) ficam como o heap os deixou.
cel_tag* build_alloc_cel(cel_tag* next) {
    cel_tag* c = static_cast<cel_tag*>(Mem_Malloc(0xb0, 0x104, 0));
    c->next = next;
    c->water_y = bits(0xc3fa0000);  // -500
    c->misc[0xa3 - 0x98] = 0x10;
    c->box_min.x = -1.0f;
    c->box_max.x = 1.0f;
    c->objects = nullptr;
    c->unknown_0c = 0;
    c->attach = nullptr;
    c->children = c->parent = c->vis_next = nullptr;
    c->ai_nodes = nullptr;
    c->env = nullptr;
    c->ai_bnodes = nullptr;
    c->ai_blinks = nullptr;
    c->portals = nullptr;
    c->glist_flags = 0;
    c->type = 0;
    c->pos.x = c->pos.y = c->pos.z = 0.0f;
    c->rot.x = c->rot.y = c->rot.z = 0.0f;
    c->glist = nullptr;
    c->misc[0xa0 - 0x98] = 0;
    c->misc[0xa1 - 0x98] = 0;
    c->box_min.z = c->box_min.y = -1.0f;
    c->box_max.z = c->box_max.y = 1.0f;
    c->misc[0x9a - 0x98] = c->misc[0x9c - 0x98] = c->misc[0x9e - 0x98] = 0x50;
    return c;
}

// [decomp] 0x001c8928 build_alloc_portal
// Copia plano e vértices do temporário, calcula o centro e a caixa (com folga de 0,1). Os w do
// centro e da caixa não são escritos.
portal_tag* build_alloc_portal(const portal_tag* src, portal_tag* next, cel_tag* cel, uint8_t flag) {
    portal_tag* p = static_cast<portal_tag*>(Mem_Malloc(0xa0, 0x804, 0));
    amemcpy(p->verts, src->verts, 0x40);
    amemcpy(&p->plane, &src->plane, 0x10);
    const _VECTOR* v = p->verts;
    p->next = next;
    p->cel = cel;
    p->flag = flag;
    p->unknown_24 = 0xffff;
    p->center.z = (v[0].z + v[1].z + v[2].z + v[3].z) * 0.25f;
    p->center.x = (v[0].x + v[1].x + v[2].x + v[3].x) * 0.25f;
    p->center.y = (v[0].y + v[1].y + v[2].y + v[3].y) * 0.25f;
    p->box_min.x = fmin2(fmin2(v[0].x, v[1].x), fmin2(v[2].x, v[3].x)) - 0.1f;
    p->box_min.y = fmin2(fmin2(v[0].y, v[1].y), fmin2(v[2].y, v[3].y)) - 0.1f;
    p->box_min.z = fmin2(fmin2(v[0].z, v[1].z), fmin2(v[2].z, v[3].z)) - 0.1f;
    p->box_max.x = fmax2(fmax2(v[0].x, v[1].x), fmax2(v[2].x, v[3].x)) + 0.1f;
    p->box_max.y = fmax2(fmax2(v[0].y, v[1].y), fmax2(v[2].y, v[3].y)) + 0.1f;
    p->box_max.z = fmax2(fmax2(v[0].z, v[1].z), fmax2(v[2].z, v[3].z)) + 0.1f;
    p->unknown_0c = 0;
    p->unknown_26 = 0;
    return p;
}

// [decomp] 0x001c93c8 build_alloc_world
// O Ghidra mostra void, mas o ponteiro alocado continua em v0 no retorno.
world_tag* build_alloc_world() {
    world_tag* w = static_cast<world_tag*>(Mem_Malloc(4, 0x604, 0));
    w->first = nullptr;
    return w;
}

// [decomp] 0x001c94f8 build_MakeCelOverlapBox
// Vazia na versão final.
void build_MakeCelOverlapBox(world_tag*) {}

// [decomp] 0x001e3698 Plane_PutPntOnPlane
// vmul.xyz, a soma (x + y) + z + d na FPU, negada; vmulx.xyz com a normal e vadd.xyzw. Divergência: o w de saída
// é o de p (no original o vadd.xyzw soma o w velho de vf4, que nenhum chamador lê).
void Plane_PutPntOnPlane(_VECTOR* out, const _VECTOR* p, const plane_equ_tag* plane) {
    const float k = -(((plane->a * p->x + plane->b * p->y) + plane->c * p->z) + plane->d);
    *out = {p->x + plane->a * k, p->y + plane->b * k, p->z + plane->c * k, p->w};
}

// [decomp] 0x001df8d0 Plane_PlaneEq
// Normal = (b - a) x (c - b), normalizada; d = -(normal . a). Com normal nula, zera o plano.
bool Plane_PlaneEq(plane_equ_tag* plane, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c) {
    const float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    const float vx = c->x - b->x, vy = c->y - b->y, vz = c->z - b->z;
    const float nx = uy * vz - vy * uz;
    const float ny = uz * vx - vz * ux;
    const float nz = ux * vy - vx * uy;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0.0f) {
        plane->a = plane->b = plane->c = plane->d = 0.0f;
        return false;
    }
    const float inv = 1.0f / len;
    plane->a = nx * inv;
    plane->b = ny * inv;
    plane->c = nz * inv;
    plane->d = -(plane->a * a->x + plane->b * a->y + plane->c * a->z);
    return true;
}

// [decomp] 0x001e8ad8 BIN_GetByte
uint8_t BIN_GetByte(uint8_t** p) { return *(*p)++; }

// [decomp] 0x001e8af0 BIN_GetWord
uint16_t BIN_GetWord(uint8_t** p) {
    uint16_t v;
    std::memcpy(&v, *p, 2);
    *p += 2;
    return v;
}

// [decomp] 0x001e8b10 BIN_GetDWord
// Lê byte a byte (aceita endereço desalinhado).
uint32_t BIN_GetDWord(uint8_t** p) {
    uint32_t v;
    std::memcpy(&v, *p, 4);
    *p += 4;
    return v;
}

// [decomp] 0x001e8b48 BIN_GetFloat
float BIN_GetFloat(uint8_t** p) {
    float v;
    std::memcpy(&v, *p, 4);
    *p += 4;
    return v;
}
