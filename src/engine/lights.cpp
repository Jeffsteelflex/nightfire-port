// Luz ambiente dos objetos. Ver lights.h.
//
// Divergência: sem o jogador (glb_blokes[0] nulo) a visão noturna conta como desligada.

#include "engine/lights.h"

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/door.h"
#include "engine/effect.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/player.h"
#include "engine/sort.h"
#include "engine/vecmath.h"
#include "engine/view.h"

// [decomp] 0x001e8a48 ClrStep
float ClrStep(float from, float to, float step) {
    float f = 0.0f <= step ? (step < 1.0f ? step : 1.0f) : 0.0f;
    if (to - from == 0.0f) return from;
    f = (to - from) * f;
    if (std::fabs(f) < 1.0f) f = f < 0.0f ? -1.0f : 1.0f;
    return from + f;
}

// [decomp] 0x001cb178 Lights_CalcAmbientLight
void Lights_CalcAmbientLight(obj_tag* obj) {
    const cel_tag* cel = obj->cel;
    if (cel == nullptr || (obj->flags & 0x800) == 0) return;
    // A luz da sala depende do canal de switch dela (misc[0xa2]): 0 ou 1 escolhe o par de bytes.
    const uint32_t k = switch_channels[cel->misc[0xa2 - 0x98]];
    uint32_t r = cel->misc[0x9a - 0x98 + k];
    uint32_t g = cel->misc[0x9c - 0x98 + k];
    uint32_t b = cel->misc[0x9e - 0x98 + k];
    if ((obj->flags & 0x10000) != 0 && glb_blokes[0] != nullptr && glb_blokes[0]->night_vision != 0) {
        r = r * 2 < 0x100 ? r * 2 : 0xff;
        g = g * 2 < 0x100 ? g * 2 : 0xff;
        b = b * 2 < 0x100 ? b * 2 : 0xff;
    }
    obj->color[0] = uint8_t(int(ClrStep(float(obj->color[0]), float(r), 0.1f)));
    obj->color[1] = uint8_t(int(ClrStep(float(obj->color[1]), float(g), 0.1f)));
    obj->color[2] = uint8_t(int(ClrStep(float(obj->color[2]), float(b), 0.1f)));
}

// ------------------------------------------------------------------ luzes pontuais

DLISTINFO_tag LightList;

namespace {
P32<light_tag> SortList_150[30];  // ordenada com elementos de 4 bytes (QuickSort)

// Mais 32 luzes livres (0xa00 bytes, zeradas).
void grow_lights() {
    auto* nodes = static_cast<uint8_t*>(Mem_Malloc(0xa00, 0x3704, 0));
    std::memset(nodes, 0, 0xa00);
    for (uint16_t i = 0; i < 0x20; ++i) LList_Add(&LightList.free, reinterpret_cast<LLNODE_tag*>(nodes + i * 0x50));
}
}  // namespace

// [decomp] 0x001cc340 Light_Init
// Lista vazia com 32 luzes livres (a cada carga de fase, depois do Mem_Init).
void Light_Init() {
    DList_Init(&LightList, 0x50, 0);
    grow_lights();
}

// [decomp] 0x001cc3d8 Light_Create
// Uma luz em "pos": cor, raio, intensidade, tipo (ver light_tag::kind), o canal que a acende, a vida
// em ticks (-1: não some) e o pisca-pisca (máximo aceso, metade do sorteio apagado, máscara de
// quadros, som ao acender). Sem luzes livres a lista cresce de 32 em 32. Os nós reaproveitados não
// são zerados (obj, dist ficam do uso anterior), como no original.
light_tag* Light_Create(_VECTOR* pos, uint8_t r, uint8_t g, uint8_t b, float radius, uint16_t channel, int16_t ticks,
                        uint8_t kind, float intensity, uint16_t flicker_max, uint16_t flicker_rand,
                        uint16_t frame_mask, uint32_t sound) {
    auto* l = static_cast<light_tag*>(DList_MoveFromFree2InUse(&LightList));
    if (l == nullptr) {
        grow_lights();
        l = static_cast<light_tag*>(DList_MoveFromFree2InUse(&LightList));
    }
    l->r = r;
    l->g = g;
    l->b = b;
    l->on = 1;
    l->radius = radius;
    l->channel = channel;
    l->life = ticks == -1 ? int16_t(0) : int16_t(ticks + 1);
    l->intensity = intensity;
    l->kind = kind;
    l->flicker_rand = uint16_t(flicker_rand >> 1);
    l->flicker_max = flicker_max;
    l->flicker = uint16_t(flicker_rand + Rand_Rand(flicker_rand));
    l->frame_mask = frame_mask;
    l->sound = uint16_t(sound == 0 ? 0xffffu : sound);
    l->pos = *pos;
    l->handle = psiLight_Create(l);
    return l;
}

// [decomp] 0x001cc588 Light_Delete
// Devolve a luz à lista de livres (1 se ela estava em uso).
uint32_t Light_Delete(light_tag* light) {
    if (light == nullptr) return 0;
    LLNODE_tag* node = LList_Remove(&LightList.used, &light->node);
    if (node == nullptr) return 0;
    psiLight_Delete(light->handle);
    LList_Add(&LightList.free, node);
    return 1;
}

// [decomp] 0x001cc3d0 Light_FindMapLight
// Vazia no jogo: nunca acha.
light_tag* Light_FindMapLight(_VECTOR*, float) { return nullptr; }

// [decomp] 0x001caeb8 Light_SetAmbientRadiators
// As luzes fixas da fase (bloco do mapa: quantidade e registros de 0x20 bytes com posição, raio² e
// cor de 0 a 1): luzes do tipo 0, que só iluminam objetos com a flag 0x80.
void Light_SetAmbientRadiators(uint8_t* data) {
    uint32_t count;
    std::memcpy(&count, data, 4);
    for (uint32_t i = 0; i < count; i = (i + 1) & 0xffff) {
        const uint8_t* e = data + i * 0x20 + 4;
        float f[8];
        std::memcpy(f, e, sizeof(f));
        _VECTOR pos{f[0], f[1], f[2], 0.0f};
        Light_Create(&pos, uint8_t(int32_t(f[5] * 255.0f)), uint8_t(int32_t(f[6] * 255.0f)),
                     uint8_t(int32_t(f[7] * 255.0f)), std::sqrt(f[3]), 0, -1, 0, 1.0f, 0, 0, 0, 0xffff);
    }
}

// [decomp] 0x001cafb8 Light_Update
// Uma vez por tick: as luzes com vida a perdem (e somem ao acabar); as presas a um canal seguem o
// switch; as que piscam alternam entre acesas (pelos quadros da máscara, até flicker_max ticks) e
// apagadas (um intervalo sorteado), tocando o som ao acender.
void Light_Update() {
    light_tag* l = reinterpret_cast<light_tag*>(nf::raw(LightList.used.first));
    if (l == nullptr) return;
    int16_t life = l->life;
    while (true) {
        light_tag* next = reinterpret_cast<light_tag*>(nf::raw(l->node.next));
        const int16_t old = l->life;
        bool keep = life == 0;
        if (!keep) {
            l->life = int16_t(old - 1);
            keep = old != 1;
        }
        if (keep) {
            if (l->channel != 0) l->on = switch_channels[l->channel & 0xff];
            if (l->flicker_max != 0) {
                uint16_t f = l->flicker;
                if ((f & 0xfff) == 0) {
                    l->flicker = 0xf000;
                    Sound_Play3D(l->sound, &l->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                    f = l->flicker;
                }
                if ((f & 0xf000) == 0) {
                    l->on = 1;
                    l->flicker = uint16_t(f - 1);
                } else {
                    l->flicker = uint16_t(f + 1);
                    l->on = uint8_t(l->frame_mask == (GameState.frame & l->frame_mask));
                    if (l->flicker_max < ((f + 1) & 0xfff)) {
                        l->flicker = uint16_t(l->flicker_rand + Rand_Rand(l->flicker_rand));
                    }
                }
            }
        } else if (LLNODE_tag* node = LList_Remove(&LightList.used, &l->node); node != nullptr) {
            psiLight_Delete(l->handle);
            LList_Add(&LightList.free, node);
        }
        if (next == nullptr) break;
        life = next->life;
        l = next;
    }
}

// [decomp] 0x001cb2e0 Lights_CalcClosestLights
// As luzes acesas que alcançam a esfera (distância até o centro <= raio da esfera + raio da luz),
// até 30, da mais próxima para a mais distante pela distância relativa ao raio (dist = d / (raio /
// 2)). Pelo tipo: as de tipo 0 só iluminam objetos com a flag 0x80, as de tipo 1 não iluminam os com
// 0x100, as de tipo 2 (lanternas) só os com 0x80 a até 4 m dela no plano, com a intensidade caindo
// com a distância (50 a 0). Nulo sem luzes.
P32<light_tag>* Lights_CalcClosestLights(sphere_tag* sphere, uint16_t flags, uint16_t* count) {
    if (count == nullptr) return nullptr;
    const auto* s = reinterpret_cast<const _VECTOR*>(sphere);
    uint32_t n = 0;
    for (auto* l = reinterpret_cast<light_tag*>(nf::raw(LightList.used.first)); l != nullptr && n < 0x1e;
         l = reinterpret_cast<light_tag*>(nf::raw(l->node.next))) {
        if (l->on == 0) continue;
        if (l->kind == 0 && (flags & 0x80) == 0) continue;
        if (l->kind == 1 && (flags & 0x100) != 0) continue;
        float d;
        if (l->kind == 2) {
            if ((flags & 0x80) == 0) continue;
            d = Vec_Dist2D(&l->obj->pos, s);
            if (4.0f < d) continue;
            l->intensity = (1.0f - d * 0.25f) * 50.0f;
        } else {
            d = Vec_Dist3D(&l->pos, s);
            if (s->w + l->radius < d) continue;
        }
        l->dist = d / (l->radius * 0.5f);
        SortList_150[n] = l;
        n = (n + 1) & 0xffff;
    }
    *count = uint16_t(n);
    if (n == 0) return nullptr;
    if (n != 1) QuickSort(SortList_150, n, 4, COMP_LIGHT, nullptr);
    return SortList_150;
}

void lights_debug_power_on() {
    std::memset(&LightList, 0, sizeof(LightList));
    std::memset(SortList_150, 0, sizeof(SortList_150));
}

// ------------------------------------------------------------------ objetos de luz

// [decomp] 0x0018da00 Corona_Create
// Halo de luz da fase: um objeto do tipo 1 (sem atualização) com o modelo do halo e as flags dele.
// (Corona_Update, que acenderia o halo pela linha de visada da câmera, não é chamada no jogo.)
obj_tag* Corona_Create(_VECTOR* pos, _VECTOR* rot, level_tag*, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    obj->type = 1;
    obj->flags = glist->flags;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Dados de uma lâmpada que falha (tipo 0x26, 0x30 bytes).
struct FLICKER_tag {
    P32<light_tag> light;  // 0x00 a luz do mapa que ela acompanha (Light_FindMapLight)
    int16_t sparks;     // 0x04 solta faíscas apagada
    uint8_t pad[0x0a];
    _VECTOR up;         // 0x10 direção das faíscas
    uint32_t channel;   // 0x20 canal ligado enquanto ela está apagada
    uint8_t pad2[0x0c];
};
static_assert(sizeof(FLICKER_tag) == 0x30);

// [decomp] 0x001acb00 Flicker_Create
// Lâmpada que acompanha uma luz do mapa: parâmetros 0 (faíscas) e 1 (canal).
obj_tag* Flicker_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag*) {
    obj_tag* obj = control_create_object(0x30, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    obj->type = 0x26;
    obj->flags = glist->flags;
    build_LinkToRoom(obj, 0, glb_world);
    auto* f = static_cast<FLICKER_tag*>(obj->data);
    f->light = nullptr;
    f->sparks = int16_t(level->params[0]);
    f->channel = level->params[1];
    MATRIX m;
    View_RotTransMatrix(rot, pos, &m);
    Mat_GetUp(&f->up, &m);
    return obj;
}

// [decomp] 0x001ab960 Flicker_Update
// Acesa (branca, canal desligado) quando a luz do mapa está acesa, apagada (preta, canal ligado,
// faíscas a cada dois quadros) quando não; a cor anda 10% por tick (r e g ao menos 1). Como
// Light_FindMapLight nunca acha uma luz, no jogo ela fica sempre acesa.
void Flicker_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    if (obj->cel != nullptr && obj->cel->misc[0xa0 - 0x98] == 0) return;
    auto* f = static_cast<FLICKER_tag*>(obj->data);
    bool dark = false;
    if (f->light == nullptr) f->light = Light_FindMapLight(reinterpret_cast<_VECTOR*>(&obj->sphere), obj->sphere.w);
    if (f->light != nullptr && f->light->on == 0) dark = true;
    const int32_t target = dark ? 0 : 0xff;
    if (f->channel != 0) switch_channels[f->channel & 0xff] = dark ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        const int32_t d = target - int32_t(obj->unknown_103[i]);
        if (float(d) == 0.0f) continue;
        float step = float(d) * 0.1f;
        if (i < 2 && std::fabs(step) < 1.0f) step = step < 0.0f ? -1.0f : 1.0f;
        obj->unknown_103[i] = uint8_t(obj->unknown_103[i] + int8_t(int32_t(step)));
    }
    if (dark && f->sparks != 0 && (GameState.frame & 1) == 0) {
        // As faíscas (0x002fcca0, os mesmos modelos das do laser).
        static const uint32_t kSparks[5] = {0x20002d1, 0x20002d2, 0x20002d3, 0x20002d4, 0x20002d5};
        Gas_Init(obj->cel, reinterpret_cast<_VECTOR*>(&obj->sphere), nullptr, &f->up, 1.2f, 0.8f,
                 kSparks[Rand_Rand(5)], 4, 0xffffffffu, 0x0a460003u, 0x55);
        plane_equ_tag down{0.0f, -1.0f, 0.0f, 0.0f};
        Particles_Create(5, reinterpret_cast<_VECTOR*>(&obj->sphere), &down);
    }
}
