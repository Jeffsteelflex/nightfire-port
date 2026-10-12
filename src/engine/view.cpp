// Port do módulo de visão: câmeras, visibilidade por portais e listas de desenho. Ver view.h.
//
// Divergências do original (todas para não ler nem escrever fora de uma tabela):
// - ClipPortalWithPlane para em 12 vértices (o tamanho de _PORTAL); o original seguiria escrevendo
//   por cima de NumPortalPlanes.
// - View_AddCels não lê a sala da câmera quando ela é nula (o original leria o endereço 0x3c).
// Só do port, sem mudar o desenho: View_DrawObjList, View_DrawSky e os anexos dizem a psi_draw quem pede cada
// desenho (psi_draw_set_owner), a identidade com que a interpolação entre ticks (render/frame_interp.h) acha o
// mesmo desenho no tick seguinte.

#include "engine/lights.h"
#include "engine/view.h"

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/input.h"
#include "engine/resetmap.h"
#include "engine/sort.h"
#include "engine/sprite.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/fmath.h"

viewer_tag* glb_viewer[kViewerCount];
uint32_t object_display_mask = 0;
_VECTOR CamPos;
uint32_t switch_ForceDrawAll = 0;
float MAX_VIEW_CONE = 2000.0f;
uint16_t OnlyScaleZ = 0;
uint16_t FlipScale = 0;
uint8_t bCharacterLight = 0;
uint8_t bShadowCharacter = 0;
uint8_t GlobalBlur = 0;
uint32_t ScreenBlankerState = 0;
uint8_t LightningType = 0;
uint8_t Reflicker = 0;
uint8_t SuperHappyLightningFlag = 0;
uint32_t Tots[3];

namespace {

// Lista de salas visíveis (VisCelList, 64 entradas de 8 bytes).
struct VISCEL_tag {
    P32<cel_tag> cel;
    uint16_t depth;  // portais atravessados até a sala
    uint16_t pad;
};
VISCEL_tag VisCelList[64];
uint16_t VisCelCount = 0;
int32_t VisionRecurseCount = 0;

constexpr int kPortalMax = 12;
_PORTAL PortalsIn, PortalsOut;
uint8_t PortalPointIn[16];
_VECTOR ViewPoints[5];  // vértice e cantos do frustum (a 200 unidades), no mundo
// Planos do frustum recortado: 10 por profundidade. Um portal recortado com mais de 9 vértices
// escreve no começo da profundidade seguinte, como no original (a tabela é contínua).
plane_equ_tag PortalPlanes[48 * 10];
int32_t NumPortalPlanes[48];
uint32_t rotatosky_168 = 0;
uint32_t port_lightnings = 0;  // só do port: relâmpagos começados no View_DrawSky (o relatório das capturas)
constexpr uint32_t a_318 = 0x4650;
constexpr uint32_t b_319 = 0x78b7;
const uint16_t AllocSizes_147[3] = {0x200, 0x200, 0x80};

// GfxList + 4: primeira sala da lista de visíveis do quadro (ligadas por cel_tag::vis_next).
P32<cel_tag>& vis_head() { return *reinterpret_cast<P32<cel_tag>*>(GfxList.unknown_00 + 4); }

// Distância com sinal ao plano, na ordem de soma da VU0 (vmul.xyz e vaddy/vaddz/vaddw no x).
float dot_plane(const _VECTOR& p, const plane_equ_tag& n) { return ((p.x * n.a + p.y * n.b) + p.z * n.c) + n.d; }

const _VECTOR& cam_row3(const viewer_tag* v) { return *reinterpret_cast<const _VECTOR*>(&v->camera.m[3][0]); }

// Reserva o próximo item da lista (circular: o índice é mascarado pelo tamanho).
DRAWENTRY_tag* push_entry(viewer_tag* viewer, uint32_t list) {
    const uint16_t i = viewer->counts[list];
    viewer->counts[list] = uint16_t((i + 1) & (viewer->sizes[list] - 1u));
    return viewer->lists[list] + i;
}

uint8_t clamp_byte(float f) { return 0.0f <= f ? uint8_t(int(f < 255.0f ? f : 255.0f)) : 0; }

// Vision_AddCelToDraw sem a chamada (o original repete este trecho em linha em três lugares).
void add_vis(cel_tag* cel, uint16_t depth) {
    if (VisCelCount >= 0x40) return;
    for (uint16_t i = 0; i < VisCelCount; ++i) {
        if (VisCelList[i].cel == cel) return;
    }
    cel->misc[0xa0 - 0x98] = 1;
    VisCelList[VisCelCount].cel = cel;
    VisCelList[VisCelCount].depth = depth;
    ++VisCelCount;
}

// vision_generate_display_list, também repetido em linha por Vision_Portal_Recurse.
void link_visible(viewer_tag* viewer) {
    viewer->sky_visible = 0;
    QuickSort(VisCelList, VisCelCount, 8, COMP_DRAWCELS, nullptr);
    cel_tag* prev = vis_head();
    for (uint16_t i = 0; i < VisCelCount; ++i) {
        cel_tag* c = VisCelList[i].cel;
        viewer->sky_visible |= uint8_t((uint8_t(int32_t(c->type) >> 28) ^ 1) & 1);
        if (prev == nullptr) {
            vis_head() = c;
        } else {
            prev->vis_next = c;
        }
        prev = c;
        c->vis_next = nullptr;
    }
}

// Ponto em que a aresta next -> cur cruza o plano, em out->v[out->count]. O w sai como
// next.w + plane.d: o vmulx.xyz escreve num registrador que ainda tem o plano e o vadd é .xyzw.
void clip_point(const plane_equ_tag* plane, const _VECTOR& cur, const _VECTOR& next, _PORTAL* out) {
    const _VECTOR d{cur.x - next.x, cur.y - next.y, cur.z - next.z, cur.w - next.w};
    const float den = (plane->a * d.x + plane->b * d.y) + plane->c * d.z;
    _VECTOR& o = out->v[out->count];
    if (den == 0.0f) {
        Vec_Zero(&o);
    } else {
        const float t = -dot_plane(next, *plane) / den;
        o = {next.x + d.x * t, next.y + d.y * t, next.z + d.z * t, next.w + plane->d};
    }
}

}  // namespace

uint32_t SEED_X = 0x1f123bb5;
uint32_t SEED_Y = 0x159a55e5;

void view_debug_power_on() {
    for (viewer_tag*& v : glb_viewer) v = nullptr;
    object_display_mask = 0;
    VisCelCount = 0;
    VisionRecurseCount = 0;
    SEED_X = 0x1f123bb5;
    SEED_Y = 0x159a55e5;
    rotatosky_168 = 0;
    switch_ForceDrawAll = 0;
    GlobalBlur = 0;
    LightningType = Reflicker = SuperHappyLightningFlag = 0;
    port_lightnings = 0;
    std::memset(DoorGroupsStates, 0, sizeof(DoorGroupsStates));
    vis_head() = nullptr;
}

// ------------------------------------------------------------------ matemática

// [decomp] 0x001e4f10 auxVec_Add
void auxVec_Add(const _VECTOR* a, const _VECTOR* b, _VECTOR* out) {
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
}

// [decomp] 0x001e4a90 Concat
// b = b * a: cada linha de b passa pela parte 3x3 de a (a coluna w de b fica, as máscaras são .xyz)
// e a translação de a é somada à de b.
void Concat(const MATRIX* a, MATRIX* b) {
    for (int r = 0; r < 4; ++r) {
        const float x = b->m[r][0], y = b->m[r][1], z = b->m[r][2];
        for (int c = 0; c < 3; ++c) b->m[r][c] = a->m[0][c] * x + a->m[1][c] * y + a->m[2][c] * z;
    }
    auxVec_Add(reinterpret_cast<const _VECTOR*>(&a->m[3][0]), reinterpret_cast<const _VECTOR*>(&b->m[3][0]),
               reinterpret_cast<_VECTOR*>(&b->m[3][0]));
}

// [decomp] 0x001e4960 ApplyMatrixLVI
void ApplyMatrixLVI(const MATRIX* m, _VECTOR* v) {
    const float x = v->x, y = v->y, z = v->z;
    v->x = m->m[0][0] * x + m->m[1][0] * y + m->m[2][0] * z;
    v->y = m->m[0][1] * x + m->m[1][1] * y + m->m[2][1] * z;
    v->z = m->m[0][2] * x + m->m[1][2] * y + m->m[2][2] * z;
}

// [decomp] 0x001e48c8 ApplyMatrixLV
// out.xyz = in * M (linhas 0-2); w fica. Cada componente relê "in", como o original (com in == out, as
// seguintes usam o x já escrito).
void ApplyMatrixLV(const MATRIX* m, const _VECTOR* in, _VECTOR* out) {
    out->x = m->m[0][0] * in->x + m->m[1][0] * in->y + m->m[2][0] * in->z;
    out->y = m->m[0][1] * in->x + m->m[1][1] * in->y + m->m[2][1] * in->z;
    out->z = m->m[0][2] * in->x + m->m[1][2] * in->y + m->m[2][2] * in->z;
}

// [decomp] 0x00120510 LimitRadAngle
float LimitRadAngle(float a) {
    while (a < -3.1415927f) a += 6.2831855f;
    while (3.1415927f < a) a -= 6.2831855f;
    return a;
}

// [decomp] 0x001e36f0 Rand_Random
uint32_t Rand_Random() {
    SEED_X = a_318 * (SEED_X & 0xffff) + (SEED_X >> 16);
    SEED_Y = b_319 * (SEED_Y & 0xffff) + (SEED_Y >> 16);
    return SEED_X << 16 | (SEED_Y & 0xffff);
}

// [decomp] 0x001e3738 Rand_Rand
uint32_t Rand_Rand(uint32_t range) {
    SEED_Y = b_319 * (SEED_Y & 0xffff) + (SEED_Y >> 16);
    SEED_X = a_318 * (SEED_X & 0xffff) + (SEED_X >> 16);
    return ((SEED_Y & 0xffff) * range) >> 16;
}

// [decomp] 0x001e3780 Rand_FRand
float Rand_FRand(float range) {
    SEED_X = a_318 * (SEED_X & 0xffff) + (SEED_X >> 16);
    SEED_Y = b_319 * (SEED_Y & 0xffff) + (SEED_Y >> 16);
    return range * float(SEED_X * 0x10000u + (SEED_Y & 0xffff)) * 2.3283064e-10f;
}

// [decomp] 0x001e38b0 Rand_FRand_MVar2
// O mesmo sorteio de Rand_FRand menos "minus": um valor em [-minus, range - minus).
float Rand_FRand_MVar2(float range, float minus) {
    SEED_X = a_318 * (SEED_X & 0xffff) + (SEED_X >> 16);
    SEED_Y = b_319 * (SEED_Y & 0xffff) + (SEED_Y >> 16);
    return range * (float(SEED_X * 0x10000u + (SEED_Y & 0xffff)) * 2.3283064e-10f) - minus;
}

// [decomp] 0x001e3810 Rand_FRandHalf
// Um valor em [-range/2, range/2).
float Rand_FRandHalf(float range) {
    SEED_X = a_318 * (SEED_X & 0xffff) + (SEED_X >> 16);
    SEED_Y = b_319 * (SEED_Y & 0xffff) + (SEED_Y >> 16);
    return range * float(SEED_X * 0x10000u + (SEED_Y & 0xffff)) * 2.3283064e-10f - range * 0.5f;
}

// [decomp] 0x001dfef0 Rand_FRandHalf_Vec
// Rand_FRandHalf em x, y e z (w fica como estava).
void Rand_FRandHalf_Vec(_VECTOR* v, float range) {
    v->x = Rand_FRandHalf(range);
    v->y = Rand_FRandHalf(range);
    v->z = Rand_FRandHalf(range);
}

// ------------------------------------------------------------------ câmeras

// [decomp] 0x001cce38 Stack_Init
uint32_t Stack_Init(STACKINFO* stack, uint16_t size, int32_t* data) {
    if (stack == nullptr) return 0;
    stack->size = size;
    stack->count = 0;
    stack->data = data;
    if (data == nullptr && size != 0) {
        stack->data = static_cast<int32_t*>(Mem_Malloc(uint32_t(size) << 2, 0x1b04, 0));
        std::memset(stack->data, 0, size_t(size) << 2);
    }
    return 1;
}

// [decomp] 0x001bc430 Camera_Shake
// Sacode as câmeras a até 2 x amount de pos: força amount x (1 - (d / 2amount)²), até 10.
void Camera_Shake(_VECTOR* pos, float amount) {
    const float range = amount + amount;
    for (uint32_t i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr) continue;
        _VECTOR eye{v->camera.m[3][0], v->camera.m[3][1], v->camera.m[3][2], v->camera.m[3][3]};
        const float d = Vec_Dist3D(pos, &eye);
        if (range < d) continue;
        const float k = d / range;
        const float shake = range * (1.0f - k * k);
        if (0.0f < shake) v->shake = shake < 10.0f ? shake : 10.0f;
    }
}

// [decomp] 0x001b9af0 Camera_Reset
void Camera_Reset(viewer_tag* v) {
    v->world = nullptr;
    View_RotTransMatrix(&CONSTANT_ZERO_VECTOR, &CONSTANT_ZERO_VECTOR, &v->camera);
    v->rot = CONSTANT_ZERO_VECTOR;
    v->updated = 0;
    v->pos = CONSTANT_ZERO_VECTOR;
    std::memset(v->lists, 0, 0x1c);
    Mat_IdentityT(&v->billboard);
    Vec_Zero(&v->prev_pos);
    v->cel = nullptr;
    v->unknown_24 = 0;
    v->active = 0;
    v->sprites = 0;
    v->unknown_1e0[1] = v->unknown_1e0[2] = 0;
    v->shake = 0.0f;
    v->swing = 0.0f;
    v->player = nullptr;
    v->unknown_208[2] = v->unknown_208[3] = v->unknown_208[4] = v->unknown_208[6] = 0;
    v->unknown_1e0[0] = 0;
    v->mode = 0;
    v->fade = 0.0f;
    v->fade_speed = 0.0f;
    v->fade_rgba[3] = v->fade_rgba[2] = v->fade_rgba[1] = v->fade_rgba[0] = 0;
    v->unknown_234 = 0;
    v->fade_state = 0;
    v->lod_scale = 1.0f;
    const float four = 4.0f;
    std::memcpy(&v->unknown_1e0[4], &four, 4);
    std::memcpy(&v->unknown_1e0[3], &four, 4);
    Stack_Init(&v->stack, 0xc, v->stack_data);
}

// [decomp] 0x001c9378 build_alloc_viewer
viewer_tag* build_alloc_viewer(uint32_t index) {
    auto* v = static_cast<viewer_tag*>(Mem_Malloc(0x240, 0x504, 0));
    v->index = uint8_t(index);
    Camera_Reset(v);
    return v;
}

// [decomp] 0x001c94d8 build_link_world_to_viewer
viewer_tag* build_link_world_to_viewer(viewer_tag* viewer, world_tag* world) {
    if (viewer == nullptr) return nullptr;
    viewer->world = world;
    return viewer;
}

// [decomp] 0x001baf70 Camera_CalcViewAngles
// Proporção 4:3 ou 16:9 (DrawInfo[1]), corrigida para tela dividida; projeção para a tela do PS2.
// No PC quem desenha a câmera troca a proporção pela da janela depois desta chamada.
void Camera_CalcViewAngles(uint16_t index, float fov) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr || v->width == 0.0f || v->height == 0.0f) return;
    v->fov = fov;
    int16_t cameras = 0;
    for (int i = 0; i < 4; ++i) {
        if (glb_viewer[i] != nullptr) ++cameras;
    }
    const uint32_t bits = DrawInfo[1] == 0 ? 0x3faaaaabu : 0x3fe38e39u;
    std::memcpy(&v->aspect, &bits, 4);
    if (cameras != 1 && cameras == 2) v->aspect = DrawInfo[0] == 1 ? v->aspect * 0.5f : v->aspect + v->aspect;
    const float t = nf::fm::tan(fov * 0.5f);
    const float a = 0.2f - MAX_VIEW_CONE;
    const float b = MAX_VIEW_CONE + 0.2f;
    v->lod_scale = 1.0f;
    const float k = -(b / a);
    v->proj_y = k * v->height * 0.5f * (1.0f / t);
    v->proj_x = (k * v->width * 0.5f * (1.0f / t)) / v->aspect;
}

// [decomp] 0x001b9920 Camera_Enable
// Liga ou desliga a câmera. Ligada, ganha as três listas de desenho (512, 512 e 128 itens, metade no
// multijogador) num bloco só; com "pos", vai para lá sem rotação.
void Camera_Enable(uint32_t index, int8_t active, int8_t sprites, _VECTOR* pos) {
    viewer_tag* v = glb_viewer[index & 0xffff];
    if (v == nullptr) return;
    v->active = uint8_t(active);
    v->sprites = uint8_t(sprites);
    if (active == 0) {
        if (v->lists[0] != nullptr) Mem_Free(&v->lists[0]);
        std::memset(v->lists, 0, 0x1c);
    } else if (v->lists[0] == nullptr) {
        std::memset(v->lists, 0, 0x1c);
        uint32_t mp;
        std::memcpy(&mp, MPSettings + 0x180, 4);
        uint32_t total = 0;
        for (int i = 0; i < 3; ++i) {
            v->sizes[i] = mp != 0 ? uint16_t(AllocSizes_147[i] >> 1) : AllocSizes_147[i];
            total += v->sizes[i];
        }
        auto* block = static_cast<DRAWENTRY_tag*>(Mem_Malloc(total * 0xc, 0x504, 0));
        uint32_t at = 0;
        for (int i = 0; i < 3; ++i) {
            v->lists[i] = block + at;
            at += v->sizes[i];
        }
    }
    if (pos != nullptr) {
        v->cel = nullptr;
        View_RotTransMatrix(&CONSTANT_ZERO_VECTOR, pos, &v->camera);
        v->rot = CONSTANT_ZERO_VECTOR;
        v->updated = 0;
        v->pos = *pos;
    }
}

// [decomp] 0x001bb0f8 Camera_Create
void Camera_Create(uint32_t index, world_tag* world, int8_t active, uint16_t x, uint16_t y, uint16_t width,
                   uint16_t height) {
    viewer_tag*& slot = glb_viewer[index];
    if (slot == nullptr) slot = build_link_world_to_viewer(build_alloc_viewer(index), world);
    if (active != 0) {
        // Sala de 2000 unidades no mundo da câmera (o tipo 0x10040000 não deixa ver o céu).
        cel_tag* c = build_alloc_cel(slot->world->first);
        slot->world->first = c;
        c->box_max.x = 1000.0f;
        c->box_min.x = -1000.0f;
        c->box_min.z = -1000.0f;
        c->type |= 0x10040000;
        c->box_min.y = -1000.0f;
        c->box_max.z = 1000.0f;
        c->box_max.y = 1000.0f;
    }
    slot->world = world;
    slot->active = uint8_t(active);
    slot->sprites = 1;
    slot->follow = nullptr;
    slot->updater = nullptr;
    Camera_Enable(index & 0xffff, active, 1, nullptr);
    viewer_tag* v = glb_viewer[index & 0xffff];
    if (v != nullptr) {
        v->width = float(width);
        v->viewport[1] = float(width) + float(x);
        v->viewport[3] = float(height) + float(y);
        v->height = float(height);
        v->viewport[0] = float(x);
        v->viewport[2] = float(y);
        Camera_CalcViewAngles(uint16_t(index), 1.0471976f);
    }
}

// [decomp] 0x001bb2b8 Camera_CreateCameras
// Câmeras 0-3: jogadores (só a 0 fora do multijogador). 5 e 7 têm mundo próprio; 4, 6, 8 e 10 são
// especiais. Depois divide a tela do PS2 (512 x 448) entre os jogadores.
void Camera_CreateCameras() {
    uint32_t players = 1;
    ScreenBlankerState = 0;
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    Camera_Create(5, build_alloc_world(), 1, 0, 0, 0x200, 0x1c0);
    Camera_Create(7, build_alloc_world(), 1, 0, 0, 0x200, 0x1c0);
    Camera_Create(4, glb_world, 0, 0, 0, 0x200, 0x1c0);
    Camera_Create(6, nullptr, 0, 0, 0, 0x200, 0x1c0);
    Camera_Create(8, nullptr, 0, 0, 0, 0x200, 0x1c0);
    Camera_Create(10, nullptr, 0, 0, 0, 0x200, 0x1c0);
    Camera_Enable(8, 0, 0, nullptr);
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp != 0) {
        uint16_t n;
        std::memcpy(&n, MPSettings + 0x1ac, 2);
        players = n;
    }
    for (uint32_t i = 0; i < players; ++i) {
        if (glb_viewer[i] == nullptr) Camera_Create(i, glb_world, 0, 0, 0, 0x200, 0x1c0);
    }
    const auto set = [](int i, float x0, float x1, float y0, float y1, float w, float h) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr) return;
        v->viewport[3] = y1;
        v->width = w;
        v->height = h;
        v->viewport[0] = x0;
        v->viewport[1] = x1;
        v->viewport[2] = y0;
        Camera_CalcViewAngles(uint16_t(i), 1.0471976f);
    };
    if (players == 2) {
        float w, h, dx, dy;
        if (DrawInfo[0] == 1) {
            w = 256.0f;
            dy = 0.0f;
            h = 448.0f;
            dx = w;
        } else {
            dy = 224.0f;
            h = 224.0f;
            w = 512.0f;
            dx = 0.0f;
        }
        set(0, 0, w, 0, h, w, h);
        set(1, dx, w + dx, dy, h + dy, w, h);
    } else if (players < 3) {
        if (players == 1) set(0, 0, 512.0f, 0, 448.0f, 512.0f, 448.0f);
    } else if (players < 5) {
        set(0, 0, 256.0f, 0, 224.0f, 256.0f, 224.0f);
        set(1, 256.0f, 512.0f, 0, 224.0f, 256.0f, 224.0f);
        set(2, 0, 256.0f, 224.0f, 448.0f, 256.0f, 224.0f);
        set(3, 256.0f, 512.0f, 224.0f, 448.0f, 256.0f, 224.0f);
    }
}

// [decomp] 0x001bb6d8 Camera_Update
// Chama a função de acompanhamento, aplica o tremor e calcula as matrizes derivadas: a de visão, a
// posição e a de billboards (a câmera com a linha 0 negada).
void Camera_Update(uint16_t index) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr) return;
    if (v->updater != nullptr && v->follow != nullptr) v->updater(v->follow, v->follow->data);
    if (v->cel != nullptr && v->pos.y < v->cel->water_y) Camera_Shear(v);
    if (0.0f < v->shake) {
        Rand_FRand_MVar2_Vec(reinterpret_cast<_VECTOR*>(&v->camera.m[3][0]), v->shake * REC_FRAME_RATE,
                             v->shake * REC_FRAME_RATE);
        const float decay = Rand_FRand(v->shake * 0.25f);
        const float s = v->shake;
        v->shake = s - decay;
        if (s - decay < 0.0f) v->shake = 0.0f;
    }
    Mat_World2ViewMat(&v->camera, &v->view);
    v->pos = cam_row3(v);
    Mat_Copy(&v->camera, &v->billboard);
    Mat_Scale3f(&v->billboard, &v->billboard, -1.0f, 1.0f, 1.0f);
    v->updated = 1;
}

// [decomp] 0x001bbbf8 Camera_Set
// Câmera na posição pos com os ângulos rot (View_RotTransMatrix), guardados em pos e rot.
void Camera_Set(viewer_tag* v, _VECTOR* pos, _VECTOR* rot) {
    if (v == nullptr || pos == nullptr || rot == nullptr) return;
    View_RotTransMatrix(rot, pos, &v->camera);
    const _VECTOR r = *rot;
    const _VECTOR p = *pos;
    v->rot = r;
    v->updated = 0;
    v->pos = p;
}

// [decomp] 0x001bc370 Camera_SetUpdator
// Liga a câmera a um objeto e à função que a posiciona a cada Camera_Update.
obj_tag* Camera_SetUpdator(uint16_t index, obj_tag* obj, void (*updater)(obj_tag*, void*)) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr) return nullptr;
    v->updater = updater;
    v->follow = obj;
    return obj;
}

// [decomp] 0x001bbdd8 Camera_SetFade
// Esmaecimento da câmera para a cor rgba (0xRRGGBBAA, o alfa é o destino) em "time" quadros a 60
// Hz; tempo negativo esmaece de volta (começa cheio). Não começa se já houver um esmaecimento
// travado (fade_state e unknown_234 ligados). flag fica em unknown_234.
uint32_t Camera_SetFade(uint16_t index, float time, uint32_t rgba, int8_t flag) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr || (v->unknown_234 != 0 && v->fade_state != 0)) return 0;
    float speed = (60.0f / time) * 0.016666668f;
    v->fade_speed = speed;
    speed = speed * 0.003921569f * float(rgba & 0xff);
    float fade = 0.0f;
    v->fade_state = 1;
    v->unknown_234 = uint8_t(flag);
    v->fade_speed = speed;
    if (speed < 0.0f) fade = 1.0f;
    v->fade = fade;
    fade = fade * 0.003921569f * float(rgba & 0xff);
    v->fade_rgba[0] = uint8_t(rgba >> 24);
    v->fade_rgba[1] = uint8_t(rgba >> 16);
    v->fade_rgba[2] = uint8_t(rgba >> 8);
    v->sprites = 1;
    v->fade = fade;
    v->fade_rgba[3] = uint8_t(int32_t(fade * 255.0f));
    return 1;
}

// [decomp] 0x001bbf40 Camera_ApplySwing
// Balanço da visão com zoom (lod_scale diferente de 1): os ângulos oscilam em três frequências com
// amplitude (lod_scale - 1) * amount, uma volta a cada 4 segundos.
void Camera_ApplySwing(viewer_tag* v, float amount) {
    if (v == nullptr) return;
    const float phase = v->swing + 6.2831855f / (FRAME_RATE * 4.0f);
    v->swing = phase;
    float s = PS2Sinf(phase + phase);
    v->rot.x = v->rot.x + (v->lod_scale - 1.0f) * amount * s;
    s = PS2Sinf(v->swing);
    v->rot.y = v->rot.y + (v->lod_scale - 1.0f) * amount * s;
    s = PS2Sinf(v->swing * 3.0f);
    v->rot.z = v->rot.z + (v->lod_scale - 1.0f) * amount * s;
    View_RotTransMatrix(&v->rot, &v->pos, &v->camera);
    v->updated = 0;
}

// [decomp] 0x001bc048 Camera_SetToPlayer
// Câmera em primeira pessoa: a rotação do jogador inclinada pelo pitch (BLData::pitch x 90 graus) e
// a posição dos olhos (Player_GetHeadPos).
void Camera_SetToPlayer(obj_tag* obj, viewer_tag* v) {
    if (obj == nullptr || v == nullptr) return;
    MATRIX tilt;
    RotMatrixX(-(bloke_of(obj)->pitch * 1.5707964f), &tilt);
    MulMatrix0(&obj->matrix, &tilt, &v->camera);
    Player_GetHeadPos(reinterpret_cast<_VECTOR*>(&v->camera.m[3][0]), obj, nullptr);
}

// Máscara de câmeras em que o objeto é desenhado (obj_tag::unknown_f8, um bit por câmera, 0x1f
// todas), copiada para os objetos presos aos ossos quando o objeto é animado (mode bit 8).
namespace {
void set_draw_mask(obj_tag* obj, uint16_t mask) {
    if (obj == nullptr) return;
    obj->unknown_f8 = mask;
    if ((obj->mode & 8) == 0) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim == nullptr) return;
    const uint8_t count = anim->skin->n_discrete;
    for (uint32_t i = 0; i < count; ++i) anim->attached[i]->unknown_f8 = mask;
}
}  // namespace

// [decomp] 0x001e7ba0 View_SetDrawInOtherViewsOnly
void View_SetDrawInOtherViewsOnly(obj_tag* obj, uint32_t index) {
    set_draw_mask(obj, uint16_t(~(1u << (index & 0x1f)) & 0x1f));
}

// [decomp] 0x001e7b30 View_DrawInThisViewOnly
void View_DrawInThisViewOnly(obj_tag* obj, uint32_t index) { set_draw_mask(obj, uint16_t(1u << (index & 0x1f))); }

// [decomp] 0x001e7c80 View_SetDrawInNoViews
void View_SetDrawInNoViews(obj_tag* obj) { set_draw_mask(obj, 0); }

// [decomp] 0x001e7c18 View_SetDrawInAllViews
void View_SetDrawInAllViews(obj_tag* obj) { set_draw_mask(obj, 0x1f); }

// [decomp] 0x001e7708 View_3DPoint2Screen
// Um ponto do mundo na tela da câmera "index" (pixels do PS2: x da esquerda, y de baixo para cima a
// partir do centro da altura) e a profundidade em z. 1 na frente da câmera, -1 atrás (z positivo do
// mesmo jeito), 0 sem câmera. in e out podem ser o mesmo vetor; out.w não muda.
int32_t View_3DPoint2Screen(const _VECTOR* in, _VECTOR* out, uint16_t index) {
    if (in == nullptr || out == nullptr) return 0;
    const viewer_tag* v = glb_viewer[index];
    if (v == nullptr) return 0;
    const float dx = in->x - v->pos.x, dy = in->y - v->pos.y, dz = in->z - v->pos.z;
    // vmulax/vmadday/vmaddz.xyz: as linhas 0-2 de view pesadas por d, sem a translação.
    const MATRIX& m = v->view;
    const float rx = m.m[0][0] * dx + m.m[1][0] * dy + m.m[2][0] * dz;
    const float ry = m.m[0][1] * dx + m.m[1][1] * dy + m.m[2][1] * dz;
    float z = m.m[0][2] * dx + m.m[1][2] * dy + m.m[2][2] * dz;
    int32_t side = 1;
    if (z < 0.0f) {
        side = -1;
        z = -z;
    }
    out->x = v->width - (rx * (v->proj_x / z) + v->width * 0.5f);
    out->z = z;
    out->y = ry * (v->proj_y / z) + v->height * 0.5f;
    return side;
}

// [decomp] 0x001e0d58 Cel_ObjectLeftCel
// Se o segmento from-to cruza um portal da sala "cel" (os dois triângulos do quadrilátero), devolve
// a sala do outro lado.
cel_tag* Cel_ObjectLeftCel(cel_tag* cel, const _VECTOR* from, const _VECTOR* to) {
    if (cel == nullptr) return nullptr;
    const _VECTOR d{to->x - from->x, to->y - from->y, to->z - from->z, to->w - from->w};
    const float len = std::sqrt((d.x * d.x + d.y * d.y) + d.z * d.z);
    _VECTOR dir{0, 0, 0, 0};
    if (len != 0.0f) {
        const float inv = 1.0f / len;
        dir = {d.x * inv, d.y * inv, d.z * inv, 0.0f};
    }
    _VECTOR lo, hi;
    Vec_MinMax(from, to, &lo, &hi);
    for (portal_tag* p = cel->portals; p != nullptr; p = p->next) {
        if (Intersect_BoxBox(&lo, &hi, &p->box_min, &p->box_max) == 0) continue;
        float t = 0.0f;
        if (Collide_RayTriangle(from, &dir, &p->verts[0], &p->verts[1], &p->verts[2], &t) != 0 && 0.0f <= t &&
            t <= len) {
            return p->cel;
        }
        if (Collide_RayTriangle(from, &dir, &p->verts[2], &p->verts[3], &p->verts[0], &t) != 0 && 0.0f <= t &&
            t <= len) {
            return p->cel;
        }
    }
    return nullptr;
}

// [decomp] 0x001eda38 Intersect_BoxBox
uint32_t Intersect_BoxBox(const _VECTOR* min_a, const _VECTOR* max_a, const _VECTOR* min_b, const _VECTOR* max_b) {
    if (min_b->x <= max_a->x && min_a->x <= max_b->x && min_b->z <= max_a->z && min_a->z <= max_b->z) {
        return (min_b->y <= max_a->y && min_a->y <= max_b->y) ? 1 : 0;
    }
    return 0;
}

// [decomp] 0x001c9500 build_point_in_bounding_box
uint32_t build_point_in_bounding_box(const _VECTOR* p, const cel_tag* cel) {
    if (cel == nullptr || !(cel->box_min.x <= p->x && p->x <= cel->box_max.x && cel->box_min.z <= p->z &&
                            p->z <= cel->box_max.z)) {
        return 0;
    }
    return (cel->box_min.y <= p->y && p->y <= cel->box_max.y) ? 1 : 0;
}

// [decomp] 0x001bae90 Camera_CheckLocation
// Acompanha a sala da câmera: primeiro pelos portais cruzados desde a posição anterior; se ela saiu
// da caixa da sala sem cruzar portal, procura de novo (build_FindCel). Sem sala nenhuma, fica com a
// anterior ou com a primeira do mundo, e prev_pos não anda.
void Camera_CheckLocation(uint16_t index) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr) return;
    v->delta = {v->pos.x - v->prev_pos.x, v->pos.y - v->prev_pos.y, v->pos.z - v->prev_pos.z,
                v->pos.w - v->prev_pos.w};
    if (v->active == 0) return;
    cel_tag* crossed = Cel_ObjectLeftCel(v->cel, &v->prev_pos, &v->pos);
    if (crossed != nullptr) {
        v->cel = crossed;
        v->prev_pos = v->pos;
        return;
    }
    if (build_point_in_bounding_box(&v->pos, v->cel) == 0) {
        cel_tag* old = v->cel;
        v->cel = build_FindCel(&v->pos, v->world);
        if (v->cel == nullptr) {
            v->cel = old != nullptr ? old : nf::raw(glb_world->first);
            return;
        }
        v->cel->misc[0xa0 - 0x98] = 1;
    }
    v->prev_pos = v->pos;
}

// [decomp] 0x001bada0 Camera_UpdateGlbVars
// Avança o fade da câmera (fade_state 1: em andamento; 2: terminou, volta a 0 no quadro seguinte).
uint32_t Camera_UpdateGlbVars(uint16_t index) {
    viewer_tag* v = glb_viewer[index];
    if (v == nullptr) return 1;
    const auto cel = [v]() { return nf::p32_u(v->cel); };
    if (v->fade_state == 1) {
        const float f = v->fade + v->fade_speed * FRAME_RATE_MUL;
        v->fade = f;
        v->fade_rgba[3] = clamp_byte(f * 255.0f);
        if (0.0f <= v->fade && v->fade <= 1.0f) return cel();
        v->fade_state = 2;
    } else if (v->fade_state < 2) {
        if (v->fade_state != 0) return cel();
        v->fade_rgba[3] = 0;
    } else {
        if (v->fade_state != 2) return cel();
        v->fade_state = 0;
    }
    return cel();
}

// [decomp] 0x001bbd68 Camera_UpdateAll
void Camera_UpdateAll() {
    for (uint16_t i = 0; i < kViewerCount; ++i) {
        if (glb_viewer[i] == nullptr) continue;
        Camera_Update(i);
        Camera_CheckLocation(i);
        Camera_UpdateGlbVars(i);
    }
}

// ------------------------------------------------------------------ visibilidade

// [decomp] 0x001e7df8 Vision_Calculate_View_Fustrum
// Pirâmide da câmera até 200 unidades: o vértice e os quatro cantos (a matriz só escreve xyz; o w
// fica o do ponto local), os cinco planos da profundidade 0 e os quatro lados copiados para a
// câmera (Vision_InView).
void Vision_Calculate_View_Fustrum(viewer_tag* v) {
    const float t = nf::fm::tan(v->fov * 0.5f);
    const float y = t * 200.0f;
    const float x = t * v->aspect * 200.0f;
    const _VECTOR local[5] = {{0, 0, 0, 0}, {-x, -y, 200, 0}, {x, -y, 200, 0}, {-x, y, 200, 0}, {x, y, 200, 0}};
    const MATRIX& m = v->camera;
    for (int i = 0; i < 5; ++i) {
        const _VECTOR& p = local[i];
        ViewPoints[i].x = ((m.m[0][0] * p.x + m.m[1][0] * p.y) + m.m[2][0] * p.z) + m.m[3][0];
        ViewPoints[i].y = ((m.m[0][1] * p.x + m.m[1][1] * p.y) + m.m[2][1] * p.z) + m.m[3][1];
        ViewPoints[i].z = ((m.m[0][2] * p.x + m.m[1][2] * p.y) + m.m[2][2] * p.z) + m.m[3][2];
        ViewPoints[i].w = p.w;
    }
    NumPortalPlanes[0] = 5;
    Plane_PlaneEq(&PortalPlanes[0], &ViewPoints[0], &ViewPoints[1], &ViewPoints[2]);
    Plane_PlaneEq(&PortalPlanes[1], &ViewPoints[0], &ViewPoints[3], &ViewPoints[1]);
    Plane_PlaneEq(&PortalPlanes[2], &ViewPoints[0], &ViewPoints[4], &ViewPoints[3]);
    Plane_PlaneEq(&PortalPlanes[3], &ViewPoints[0], &ViewPoints[2], &ViewPoints[4]);
    Plane_PlaneEq(&PortalPlanes[4], &ViewPoints[1], &ViewPoints[3], &ViewPoints[2]);
    amemcpy(v->planes, PortalPlanes, 0x40);
}

// [decomp] 0x001e89a8 Vision_Init_Portal_Recurse
void Vision_Init_Portal_Recurse(viewer_tag* v) {
    VisCelCount = 0;
    _VECTOR dir;
    Mat_GetDir(&dir, &v->camera);  // não usado
    Vision_Calculate_View_Fustrum(v);
}

// [decomp] 0x001e7fa0 ClipPortalWithPlane
// Recorta o polígono "in" pelo plano, guardando o lado positivo em "out". Devolve 0 (com "out"
// vazio) se nenhum vértice está do lado negativo.
uint32_t ClipPortalWithPlane(const plane_equ_tag* plane, _PORTAL* in, _PORTAL* out) {
    out->count = 0;
    bool outside = false;
    for (int i = 0; i < in->count; ++i) {
        if (0.0f <= dot_plane(in->v[i], *plane)) {
            PortalPointIn[i] = 1;
        } else {
            PortalPointIn[i] = 0;
            outside = true;
        }
    }
    if (!outside) return 0;
    for (int i = 1; i < in->count + 1; ++i) {
        const int j = i == in->count ? 0 : i;
        const _VECTOR& cur = in->v[i - 1];
        if (out->count >= kPortalMax) break;
        if (PortalPointIn[j] == 1) {
            if (PortalPointIn[i - 1] == 1) {
                out->v[out->count++] = in->v[j];
            } else if (PortalPointIn[i - 1] == 0) {
                clip_point(plane, cur, in->v[j], out);
                ++out->count;
                if (out->count < kPortalMax) out->v[out->count++] = in->v[j];
            }
        } else if (PortalPointIn[j] == 0 && PortalPointIn[i - 1] == 1) {
            clip_point(plane, cur, in->v[j], out);
            ++out->count;
        }
    }
    return 1;
}

// [decomp] 0x001e82e0 Vision_Recurse
// Para cada portal da sala visto de frente e não fechado por uma porta: recorta o quadrilátero pelos
// planos da profundidade atual (descartando vértices repetidos). Sobrando algo, a sala do outro lado
// fica visível; com três vértices ou mais, os planos da câmera às arestas mais o plano do portal
// formam o frustum da próxima profundidade. Até 46 níveis e 100 chamadas por quadro.
void Vision_Recurse(cel_tag* cel, int32_t depth) {
    portal_tag* p = cel->portals;
    if (100 < VisionRecurseCount) return;
    ++VisionRecurseCount;
    if (0x2d < depth || p == nullptr) return;
    for (; p != nullptr; p = p->next) {
        if (p->flag == 0) continue;
        if (!(dot_plane(ViewPoints[0], p->plane) < 0.0f)) continue;
        if (Door_IsClosed(nf::p32_ptr<const obj_tag>(p->unknown_0c))) continue;
        PortalsIn.count = 4;
        for (int i = 0; i < 4; ++i) PortalsIn.v[i] = p->verts[i];
        for (int i = 0; i < NumPortalPlanes[depth]; ++i) {
            if (ClipPortalWithPlane(&PortalPlanes[depth * 10 + i], &PortalsIn, &PortalsOut) == 0) continue;
            PortalsIn.count = 0;
            for (int k = 0; k < PortalsOut.count; ++k) {
                const int next = k + 1 == PortalsOut.count ? 0 : k + 1;
                if (1e-07f < Vec_SqDist3D(&PortalsOut.v[k], &PortalsOut.v[next])) {
                    PortalsIn.v[PortalsIn.count++] = PortalsOut.v[k];
                }
            }
        }
        if (PortalsIn.count == 0) continue;
        p->unknown_26 = 1;
        p->opposite->unknown_26 = 1;
        add_vis(p->cel, uint16_t(depth + 1));
        if (PortalsIn.count < 3) continue;
        plane_equ_tag* planes = &PortalPlanes[(depth + 1) * 10];
        for (int k = 0; k < PortalsIn.count; ++k) {
            const int next = k + 1 == PortalsIn.count ? 0 : k + 1;
            Plane_PlaneEq(&planes[k], &ViewPoints[0], &PortalsIn.v[k], &PortalsIn.v[next]);
        }
        planes[PortalsIn.count] = p->plane;
        NumPortalPlanes[depth + 1] = PortalsIn.count + 1;
        Vision_Recurse(p->cel, depth + 1);
        if (100 < VisionRecurseCount) return;
    }
}

// [decomp] 0x001e86e8 Vision_Portal_Recurse
// A sala da câmera e as que os portais deixam ver, em ordem de profundidade.
void Vision_Portal_Recurse(viewer_tag* v) {
    cel_tag* cel = v->cel;
    if (cel == nullptr) return;
    add_vis(cel, 0);
    VisionRecurseCount = 0;
    Vision_Recurse(cel, 0);
    link_visible(v);
}

// [decomp] 0x001e8908 Vision_AddCelToDraw
void Vision_AddCelToDraw(cel_tag* cel, uint32_t depth) { add_vis(cel, uint16_t(depth)); }

// [decomp] 0x001e8848 vision_generate_display_list
void vision_generate_display_list(viewer_tag* v) { link_visible(v); }

// [decomp] 0x001e8990 vision_GetCamPos
void vision_GetCamPos(_VECTOR* out) { *out = ViewPoints[0]; }

// [decomp] 0x001e89e0 Vision_InView
// A esfera está dentro dos quatro lados do frustum (ou a menos de "radius" de cada um)?
uint32_t Vision_InView(const _VECTOR* center, float radius, const viewer_tag* v) {
    for (int i = 0; i < 4; ++i) {
        if (dot_plane(*center, v->planes[i]) < -radius) return 0;
    }
    return 1;
}

// [decomp] 0x001e6df0 View_CaptureScene
// Monta as três listas de desenho da câmera para o quadro.
void View_CaptureScene(viewer_tag* v) {
    std::memset(v->counts, 0, 6);
    if (v->active == 0 || v->lists[0] == nullptr) return;
    vis_head() = nullptr;
    object_display_mask = 1u << (v->index & 0x1f);
    for (cel_tag* c = v->world->first; c != nullptr; c = c->next) c->misc[0xa0 - 0x98] = 0;
    Vision_Init_Portal_Recurse(v);
    if (switch_ForceDrawAll == 0) {
        Vision_Portal_Recurse(v);
    } else {
        for (cel_tag* c = v->world->first; c != nullptr; c = c->next) Vision_AddCelToDraw(c, 0);
        vision_generate_display_list(v);
    }
    vision_GetCamPos(&CamPos);
    View_AddCels(v);
    View_AddForcedObjects(v);
    Tots[0] += v->counts[0];
    Tots[1] += v->counts[1];
    Tots[2] += v->counts[2];
}

// [decomp] 0x001e6ba0 View_AddCels
// As salas visíveis vão para a lista 0 na ordem dos portais (chaves -90000, -89990...). As células
// dentro delas, se estiverem no frustum, entram pela distância² à câmera: na lista 0, na 1 (de trás
// para a frente, chave negativa) com a flag 1 ou na 2 com a flag 0x2000. Depois os objetos de cada
// uma.
void View_AddCels(viewer_tag* v) {
    float room_key = -90000.0f;
    for (cel_tag* room = vis_head(); room != nullptr; room = room->vis_next) {
        if (room->glist != nullptr && (room->glist->flags & 0x10) == 0) {
            DRAWENTRY_tag* e = push_entry(v, 0);
            e->key = room_key;
            room_key += 10.0f;
            e->obj = nullptr;
            e->cel = room;
        }
        View_AddObjects(room, v, 0.0f);
        for (cel_tag* c = room->children; c != nullptr; c = c->children) {
            if (c->glist == nullptr || (c->glist->flags & 0x10) != 0) continue;
            if (uint16_t(c->type) == 0x8018 && v->cel != nullptr && uint16_t(v->cel->type) == 0xc023) {
                continue;
            }
            if ((c->glist_flags & 0x40000) != 0 && object_display_mask == 0x10) continue;
            if (Vision_InView(&c->sphere, c->sphere.w, v) == 0) continue;
            float sign = (c->glist_flags & 1) == 0 ? 1.0f : -1.0f;
            uint32_t list = (c->glist_flags & 1) == 0 ? 0u : 1u;
            if ((c->glist_flags & 0x2000) != 0) {
                sign = 1.0f;
                list = 2;
            }
            DRAWENTRY_tag* e = push_entry(v, list);
            e->obj = nullptr;
            e->cel = c;
            e->key = sign * Vec_SqDist3D(&c->sphere, &CamPos);
            View_AddObjects(c, v, 0.0f);
        }
    }
}

namespace {
// Parte comum de View_AddObjects e View_AddForcedObjects: o item na lista certa, a chave e o nível de
// detalhe por câmera em sphere_src + 0x110 (distância² / lod_scale / 4, até 255).
void add_object(obj_tag* o, viewer_tag* v, bool particles_last) {
    OnlyScaleZ = uint16_t((o->mode & 1) != 0);
    float sign = (o->flags & 1) == 0 ? 1.0f : -1.0f;
    uint32_t list = (o->flags & 1) == 0 ? 0u : 1u;
    if ((o->flags & 0x2000) != 0) {
        sign = 1.0f;
        list = 2;
    }
    if ((o->mode & 8) != 0) sign *= 50.0f;
    DRAWENTRY_tag* e = push_entry(v, list);
    e->obj = o;
    e->cel = nullptr;
    o->unknown_fc = 2;
    const float d2 = Vec_SqDist3D(&o->sphere, &CamPos);
    e->key = sign * d2;
    if (particles_last && (o->mode & 0x10) != 0) e->key = sign * d2 + 2000.0f;
    if (o->sphere_src == nullptr) return;
    const uint32_t slot = v->index < 3 ? v->index : 3;
    o->sphere_src[0x110 + slot] = clamp_byte((d2 / v->lod_scale) * 0.25f);
}
}  // namespace

// [decomp] 0x001e62d8 View_AddObjects
// Objetos da sala com modelo (ou partículas/emissores, mode 0x110) visíveis nesta câmera e no
// frustum. Os de partículas (mode 0x10) vão 2000 unidades² para trás na ordem.
void View_AddObjects(cel_tag* cel, viewer_tag* v, float) {
    for (obj_tag* o = cel->objects; o != nullptr; o = o->cel_next) {
        OnlyScaleZ = 0;
        FlipScale = 0;
        if ((o->mode & 0x110) == 0 && o->glist == nullptr) continue;
        if ((o->unknown_f8 & object_display_mask) == 0) continue;
        if ((o->flags & 0x8010) != 0) continue;
        if (Vision_InView(&o->sphere, o->sphere.w, v) == 0) continue;
        if ((o->flags & 0x400) != 0 && v->mode != 2) continue;
        add_object(o, v, true);
    }
}

// [decomp] 0x001e5fb8 View_AddForcedObjects
// Objetos que atravessam salas (flag 0x8000): entram se estiverem no frustum e, os do tipo 2 com
// raio maior que 0,5, só se alguma das salas que a esfera toca estiver visível.
void View_AddForcedObjects(viewer_tag* v) {
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if ((o->unknown_f8 & object_display_mask) == 0 || o->glist == nullptr) continue;
        if ((o->flags & 0x8000) == 0 || (o->flags & 0x10) != 0) continue;
        if (Vision_InView(&o->sphere, o->sphere.w, v) == 0) continue;
        if ((o->flags & 0x400) != 0 && v->mode != 2) continue;
        if (o->type == 2 && 0.5f < o->sphere.w) {
            HITTEST_tag test;
            std::memset(&test, 0, sizeof(test));
            test.type = 0x101;
            test.pos = o->sphere;
            test.radius = o->sphere.w;
            test.cel = o->cel;
            uint16_t count = 0;
            cel_tag** cels = Collide_StraddleCels(&test, &count);
            bool seen = false;
            for (cel_tag* room = vis_head(); room != nullptr && !seen; room = room->vis_next) {
                for (uint16_t k = 0; k < count; ++k) {
                    if (cels[k] == room) seen = true;
                }
            }
            if (!seen) continue;
        }
        add_object(o, v, false);
    }
}

// ------------------------------------------------------------------ desenho

// [decomp] 0x001e78b0 View_SetupRenderModes
// O estado de desenho de um objeto animado: o tweak dele e as luzes mais próximas da esfera dele.
void View_SetupRenderModes(viewer_tag* v, obj_tag* obj) {
    if (obj == nullptr) return;
    View_SetupRenderModes_Tweak(v, obj);
    uint16_t count = 0;
    P32<light_tag>* lights = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&obj->sphere), uint16_t(obj->flags), &count);
    psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
}

// [decomp] 0x001e7908 View_SetupRenderModes2
// O mesmo para a arma na mão (pele com modelos e anexos): o tweak do jogador desta câmera e as luzes
// perto dos olhos (raio 2).
void View_SetupRenderModes2(viewer_tag* v, obj_tag* obj) {
    if (obj == nullptr || v->player == nullptr) return;
    View_SetupRenderModes_Tweak(v, v->player);
    _VECTOR eye{v->pos.x, v->pos.y, v->pos.z, 2.0f};
    uint16_t count = 0;
    P32<light_tag>* lights = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&eye), uint16_t(obj->flags), &count);
    psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
}

// [decomp] 0x001e5888 View_SetupRenderModes_Tweak
// A cor do objeto (obj_tag::unknown_103: r, g, b, a) vira o "tweak" do desenho; com a flag 0x800,
// multiplicada pela cor da luz da sala (obj_tag::color). Objetos com a flag 0x20000 ficam com 1/4
// do alfa na visão do modo 1. Com a opção de clarões ligada, objetos marcados com 0x80000 a menos
// de 4 m da câmera piscam com FlashMod.
void View_SetupRenderModes_Tweak(const viewer_tag* v, obj_tag* obj) {
    uint8_t a = obj->unknown_103[3];
    if ((obj->flags & 0x20000) != 0 && v->mode == 1) a = uint8_t(a >> 2);
    bool flash = false;
    if (((obj->flags & 0x80000) != 0 || (obj->glist != nullptr && (obj->glist->flags & 0x80000) != 0)) &&
        PlayerSetting[0].unknown_0c != 0 && Vec_SqDist3D(&v->pos, &obj->sphere) < 16.0f) {
        flash = true;
    }
    const uint8_t* c = obj->unknown_103;
    const uint8_t* room = obj->color;
    if (flash) {
        if ((obj->flags & 0x800) == 0) {
            psiSetTweakARGB(a, uint8_t(int32_t(float(c[0]) * FlashMod)), uint8_t(int32_t(float(c[1]) * FlashMod)),
                            uint8_t(int32_t(float(c[2]) * FlashMod)));
        } else {
            psiSetTweakARGB(a, uint8_t(int32_t(float(uint32_t(c[0]) * room[0]) * FlashMod * 0.003921569f)),
                            uint8_t(int32_t(float(uint32_t(c[1]) * room[1]) * FlashMod * 0.003921569f)),
                            uint8_t(int32_t(float(uint32_t(c[2]) * room[2]) * FlashMod * 0.003921569f)));
        }
    } else if ((obj->flags & 0x800) == 0) {
        psiSetTweakARGB(a, c[0], c[1], c[2]);
    } else {
        psiSetTweakARGB(a, uint8_t((uint32_t(c[0]) * room[0]) >> 8), uint8_t((uint32_t(c[1]) * room[1]) >> 8),
                        uint8_t((uint32_t(c[2]) * room[2]) >> 8));
    }
}

namespace {
// Anexos (cel_tag::attach, obj_tag::attach), com a matriz relativa à do objeto pai quando houver.
// Só do port: cada anexo é o dono do próprio desenho (psi_draw_set_owner, a identidade da interpolação entre
// ticks); depois o dono volta a ser o de antes.
void draw_attachments(ATTACH_tag* a, const viewer_tag* v) {
    const void* owner = psi_draw_owner();
    const PsiOwnerKind kind = psi_draw_owner_kind();
    const int8_t list = psi_draw_list();
    for (; a != nullptr; a = a->next) {
        if (a->glist == nullptr) continue;
        if ((a->glist->flags & 0x400) != 0 && v->mode != 2) continue;
        psi_draw_set_owner(a, PsiOwnerKind::Attach, list);
        if (a->parent != nullptr) {
            MATRIX m;
            Mat_Copy(&a->matrix, &m);
            Concat(&a->parent->matrix, &m);
            psiDrawObjectMatrix(a->glist, &m, 0);
        } else {
            psiDrawObjectMatrix(a->glist, &a->matrix, 0);
        }
    }
    psi_draw_set_owner(owner, kind, list);
}
}  // namespace

// [decomp] 0x001e6538 View_DrawCelGlist
// Célula: RotMatrixZYX(rot) na posição (ou a rotação da câmera, para billboards com a flag 8), escala
// trêmula com a flag 4, alfa piscante pelo byte misc[0xa3] (modo nos bits 0-1, velocidade nos 4-7).
void View_DrawCelGlist(cel_tag* cel, viewer_tag* v) {
    const uint8_t blink = cel->misc[0xa3 - 0x98];
    const uint8_t mode = blink & 3;
    uint8_t alpha = uint8_t(uint8_t(GameState.frame) * (blink >> 4));
    if (mode == 1) {
        alpha = uint8_t(~alpha);
    } else if (mode != 2) {
        alpha = 0xff;
    }
    psiSetTweakARGB(alpha, 0xff, 0xff, 0xff);
    uint16_t nlights = 0;
    P32<light_tag>* lights =
        Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&cel->sphere), uint16_t(cel->glist_flags), &nlights);
    psiLight_SetLights(lights, nlights, 1.0f, 1.0f, 1.0f);
    MATRIX m{};
    if ((cel->glist_flags & 8) == 0) {
        const _VECTOR rot{LimitRadAngle(cel->rot.x), LimitRadAngle(cel->rot.y), LimitRadAngle(cel->rot.z), 0.0f};
        RotMatrixZYX(&rot, &m);
    } else {
        Mat_CopyRot(&v->billboard, &m);
    }
    if ((cel->glist_flags & 4) != 0) {
        const float sx = Rand_FRand(0.05f) + 1.0f;
        const float sy = Rand_FRand(0.05f) + 1.0f;
        const float sz = Rand_FRand(0.05f) + 1.0f;
        Mat_Scale3f(&m, &m, sx, sy, sz);
    }
    Mat_SetTrans(&cel->pos, &m);
    psiDrawObjectMatrix(cel->glist, &m, 0);
    draw_attachments(cel->attach, v);
}

// [decomp] 0x001e6780 View_DrawObjectGlist
// Objeto: a própria matriz, ou (billboard, flag 8) a rotação da câmera girada em z por rot.z, na
// posição do objeto e com a escala dele.
void View_DrawObjectGlist(obj_tag* o, viewer_tag* v) {
    MATRIX m{};
    if ((o->flags & 8) == 0) {
        Mat_Copy(&o->matrix, &m);
    } else {
        if (o->rot.z == 0.0f) {
            Mat_CopyRot(&v->billboard, &m);
        } else {
            o->rot.x = 0.0f;
            o->rot.y = 0.0f;
            RotTransMatrix(&o->rot, &CONSTANT_ZERO_VECTOR, &m);
            Concat(&v->billboard, &m);
        }
        Mat_SetTrans(reinterpret_cast<_VECTOR*>(&o->matrix.m[3][0]), &m);
        if (o->scale != 1.0f) Mat_Scale3f(&m, &m, o->scale, o->scale, o->scale);
    }
    if ((o->flags & 4) != 0) {
        const float a = Rand_FRand(0.05f);
        const float b = Rand_FRand(0.05f);
        const float c = Rand_FRand(0.05f);
        Mat_Scale3f(&m, &m, o->scale + a, o->scale + b, o->scale + c);
    }
    psiDrawObjectMatrix(o->glist, &m, o->created);
    draw_attachments(o->attach, v);
}

// [decomp] 0x001e69a0 View_DrawObjList
// Ordena a lista pela chave e desenha cada item: sala ou célula, objeto animado, partículas, emissor
// ou objeto comum. A lista 1 fica entre psiStartAlpha e psiEndAlpha.
// Só do port: cada item é o dono dos desenhos que pede (psi_draw_set_owner, a identidade da interpolação entre
// ticks, render/frame_interp.h); não muda o que é desenhado.
void View_DrawObjList(viewer_tag* v, uint16_t list) {
    if (list == 1) psiStartAlpha();
    if (v->counts[list] != 0) QuickSort(v->lists[list], v->counts[list], 0xc, COMP_ALPHAOBJ, nullptr);
    for (uint16_t i = 0; i < v->counts[list]; ++i) {
        const DRAWENTRY_tag& e = v->lists[list][i];
        bCharacterLight = 0;
        bShadowCharacter = 0;
        if (e.cel != nullptr) {
            psi_draw_set_owner(e.cel, PsiOwnerKind::Cel, int8_t(list));
            View_DrawCelGlist(e.cel, v);
            continue;
        }
        obj_tag* o = e.obj;
        psi_draw_set_owner(o, PsiOwnerKind::Object, int8_t(list));
        bCharacterLight = (o->life >> 2) & 1;
        bShadowCharacter = (o->life >> 3) & 1;
        if ((o->mode & 8) != 0) {
            AnimObjectDraw(o, v, 0, 0, 0, 0);
            continue;
        }
        View_SetupRenderModes_Tweak(v, o);
        uint16_t nlights = 0;
        P32<light_tag>* lights =
            Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&o->sphere), uint16_t(o->flags), &nlights);
        psiLight_SetLights(lights, nlights, 1.0f, 1.0f, 1.0f);
        if ((o->mode & 0x10) != 0) {
            psiDrawParticles(nf::p32_u(o->data));
        } else if ((o->mode & 0x100) != 0) {
            Emitter_Draw(o, v);
        } else {
            View_DrawObjectGlist(o, v);
        }
    }
    psi_draw_set_owner(nullptr, PsiOwnerKind::None, -1);
    if (list == 1) psiEndAlpha();
    bCharacterLight = 0;
    bShadowCharacter = 0;
}

// [decomp] 0x001e5ad0 View_DrawSky
// Elementos do céu da camada pedida (1 antes do cenário, 0 depois), só se alguma sala visível deixa
// ver o céu. Com a lógica rodando, os elementos com modelo de relâmpago trocam de modelo a intervalos
// aleatórios e tocam o trovão depois. O modelo é girado por rot e fica na posição do elemento, ou na
// da câmera com a flag 2 (o céu acompanha a câmera). Na fase 0700001b o céu gira devagar e leva o
// sol junto.
void View_DrawSky(viewer_tag* v, uint8_t layer) {
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    psiSetUpColourBlend(2);
    psiLight_SetLights(nullptr, 0, 0.0f, 0.0f, 0.0f);
    psiFog(int8_t(layer), 0.0f, 10.0f, 0xff, 0, 0);
    if (v->sky_visible != 0) {
        SuperHappyLightningFlag = 0;
        for (SKYOBJ_tag& s : GfxList.sky) {
            if (uint8_t(s.layer) != layer) continue;
            psi_draw_set_owner(&s, PsiOwnerKind::Sky, -1);  // só do port: a identidade entre ticks
            celglist_tag* glist = s.glist;
            obj_tag* obj = s.obj;
            if (GameState.paused == 0) {
                const int16_t thunder = s.thunder_wait;
                if (0 < thunder) {
                    s.thunder_wait = int16_t(thunder - 1);
                    if (thunder == 1) Sound_Play(LightningType == 0 ? 0x5d : 0x5e, 100.0f, 0, 0);
                }
                if (s.flash_left < 1) {
                    celglist_tag* flash = s.glist_flash;
                    if (flash != nullptr || s.obj_flash != nullptr) {
                        Reflicker = 0;
                        s.flash_wait = int16_t(s.flash_wait - 1);
                        if (s.flash_wait < 1) {
                            ++port_lightnings;
                            s.flash_left = int16_t(s.flash_frames);
                            obj = s.obj_flash;
                            s.flash_wait = int16_t(Rand_Rand(uint16_t(s.flash_period)));
                            float wait;
                            if (Rand_Rand(100) < 0x33) {
                                Reflicker = 1;
                                wait = FRAME_RATE * 0.16666667f;
                                LightningType = 1;
                            } else {
                                LightningType = 0;
                                wait = float(Rand_Rand(uint32_t(FRAME_RATE + FRAME_RATE))) + FRAME_RATE * 0.5f;
                            }
                            s.thunder_wait = int16_t(int(wait));
                            glist = flash;
                        }
                    }
                } else {
                    s.flash_left = int16_t(s.flash_left - 1);
                    if (0x32 < Rand_Rand(100)) {
                        obj = s.obj_flash;
                        glist = s.glist_flash;
                        if (LightningType == 1) SuperHappyLightningFlag = 1;
                    }
                }
            }
            if (glist == nullptr) {
                if (obj == nullptr) continue;
                const _VECTOR scale{obj->scale, obj->scale, obj->scale, 0.0f};
                _VECTOR pos = obj->pos;
                if (obj->type == 0x24) {
                    int32_t height;
                    std::memcpy(&height, static_cast<const uint8_t*>(obj->data) + 0x10, 4);
                    pos.y = float(height) + v->camera.m[3][1];
                }
                MATRIX m{};
                View_RotTransScaleMatrix(&obj->rot, &pos, &scale, &m);
                psiDrawObjectMatrix(obj->glist, &m, obj->created);
                continue;
            }
            MATRIX m{};
            if ((glist->flags & 8) == 0) {
                Mat_CopyRot(&CONSTANT_MAT_IDENT, &m);
                MATRIX r{};
                RotMatrix(&s.rot, &r);
                Concat(&r, &m);
                if (GameState.level_file == 0x700001b) {
                    Vec_Spherical_2_Cartesian(&SunVector, 1000.0f, 4.974189f, -0.6632252f);
                    if (GameState.paused == 0) ++rotatosky_168;
                    const _VECTOR spin{float(rotatosky_168) * 0.00069444446f, 0.0f, 0.0f, 0.0f};
                    MATRIX t{};
                    RotMatrix(&spin, &t);
                    Concat(&t, &m);
                    ApplyMatrixLVI(&m, &SunVector);
                }
            } else {
                Mat_CopyRot(&v->billboard, &m);
            }
            const _VECTOR& at = (glist->flags & 2) == 0 ? s.pos : cam_row3(v);
            m.m[3][0] = at.x;
            m.m[3][1] = at.y;
            m.m[3][2] = at.z;
            m.m[3][3] = at.w;
            psiDrawObjectMatrix(glist, &m, 0);
        }
        psi_draw_set_owner(nullptr, PsiOwnerKind::None, -1);
    }
    psiFog(1, 0.0f, 20.0f, 100, 100, 0x96);
}

uint32_t View_PortSkySpin() { return rotatosky_168; }
uint32_t View_PortLightnings() { return port_lightnings; }

// [decomp] 0x001e6f50 Game_Draw
// O quadro: captura a cena de todas as câmeras e desenha as câmeras 0-3 em camadas (céu de trás,
// lista opaca, névoa, céu da frente, lista transparente com fragmentos e gotas, lista especial,
// sprites e fade); depois as câmeras especiais 4-9, cada uma completa. psiUseCamera só é chamado
// quando a câmera muda.
void Game_Draw() {
    P32<sprite>* sprites = nullptr;
    int current = -1;
    uint32_t count_a = 0, count_b = 0, count_c = 0;
    ++GameState.draws;
    psiSetScreenBlur(GlobalBlur);
    GlobalBlur = GlobalBlur < 0x10 ? 0 : uint8_t(GlobalBlur - 0x10);
    psiPreDraw();
    std::memset(Tots, 0, sizeof(Tots));
    for (viewer_tag* v : glb_viewer) {
        if (v != nullptr) View_CaptureScene(v);
    }
    psiFog(1, 0.0f, 10.0f, 0xff, 0, 0);
    const auto use = [&](int i) {
        if (current != i) psiUseCamera(glb_viewer[i], 1);
        current = i;
    };
    const auto shards_and_drops = [] {
        psiSetShardRenderStates();
        DrawAllShards();
        psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
        psiSetUpColourBlend(2);
        psiLight_SetLights(nullptr, 0, 0.0f, 0.0f, 0.0f);
        DrawDrops();
    };
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || v->active == 0 || v->sky_visible == 0) continue;
        use(i);
        View_DrawSky(v, 1);
    }
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || v->active == 0 || v->counts[0] == 0) continue;
        use(i);
        psiSetUpColourBlend(2);
        View_DrawObjList(v, 0);
    }
    psiPostSolid();
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || v->active == 0 || v->sky_visible == 0) continue;
        use(i);
        View_DrawSky(v, 0);
    }
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr) continue;
        if (v->active != 0 && v->counts[1] != 0) {
            use(i);
            psiSetUpColourBlend(1);
            View_DrawObjList(v, 1);
        }
        if (v->active != 0) {
            use(i);
            shards_and_drops();
        }
    }
    if (Tots[2] != 0) {
        // A lista especial vem depois de limpar o Z (com a câmera 5 em uso).
        if (current != 5) psiUseCamera(glb_viewer[5], 1);
        current = 5;
        psiClearZ();
    }
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || v->active == 0 || v->counts[2] == 0) continue;
        use(i);
        psiSetUpColourBlend(2);
        View_DrawObjList(v, 2);
    }
    for (int i = 0; i < 4; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || v->sprites == 0) continue;
        sprites = Sprite_BuildList(uint16_t(i), &count_a, &count_b, &count_c);
        if (count_b != 0 || count_c != 0) {
            use(i);
            View_DrawSprites(v, sprites, count_b, count_c);
        }
        if (v->fade_state != 0) {
            use(i);
            psiFadeView(v->fade_rgba[0], v->fade_rgba[1], v->fade_rgba[2], v->fade_rgba[3]);
        }
        psiEndUseCamera(v);
    }
    count_a = 0;
    count_b = 0;
    count_c = 0;
    for (int i = 4; i < 10; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr) continue;
        if (v->active != 0 || v->sky_visible != 0) {
            use(i);
            View_DrawSky(v, 1);
        }
        if (v->sprites != 0) {
            sprites = Sprite_BuildList(uint16_t(i), &count_a, &count_b, &count_c);
            if (count_a != 0) {
                use(i);
                View_DrawSprites(v, sprites, count_a, count_b);
            }
        }
        if (v->active != 0 && v->counts[0] != 0) {
            use(i);
            psiSetUpColourBlend(2);
            View_DrawObjList(v, 0);
            psiPostSolid();
        }
        if (v->active != 0 || v->sky_visible != 0) {
            use(i);
            View_DrawSky(v, 0);
        }
        if (v->active != 0) {
            if (v->counts[1] != 0) {
                use(i);
                psiSetUpColourBlend(1);
                View_DrawObjList(v, 1);
            }
            if (i == 4) {
                use(4);
                shards_and_drops();
            }
        }
        if (v->active != 0 && v->counts[2] != 0) {
            use(i);
            psiSetUpColourBlend(2);
            View_DrawObjList(v, 2);
        }
        // Como no original, as contagens podem ser as da última câmera que montou sprites.
        if ((count_b != 0 || count_c != 0) && v->sprites != 0) {
            use(i);
            View_DrawSprites(v, sprites, count_b, count_c);
        }
        if (v->fade_state != 0 && v->sprites != 0) {
            use(i);
            psiFadeView(v->fade_rgba[0], v->fade_rgba[1], v->fade_rgba[2], v->fade_rgba[3]);
        }
        psiEndUseCamera(v);
    }
    SprCnt = 0;
    psiPostDraw();
}
