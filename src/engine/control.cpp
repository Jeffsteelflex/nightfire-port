// Port do núcleo de objetos (control_*, Control_*), da ligação às salas (build_FindCel,
// build_LinkToRoom, Controls_StraddleTest), das matrizes de objeto do módulo View e do registro do
// céu (View_AddSkyObj). Ver object.h.
//
// Dependências ainda não portadas (stubs em modules_stubs.cpp): o módulo Collide (raios e células
// atravessadas), as animações, o script player e o multijogador. Sem colisão, build_FindCel fica
// com a primeira sala que contém o ponto quando há mais de uma.

#include <cmath>
#include <cstring>

#include "engine/collide.h"
#include "engine/door.h"
#include "engine/effect.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/trigger.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"

obj_tag DynamicObjList;
int32_t DynamicObjCount = 0;
LLISTINFO_tag ForcedList;
uint8_t switch_channels[256];
uint32_t switch_channels_time[256];
uint8_t switch_channels_prev[256];
uint8_t switch_channels_hold[256];
uint16_t switch_channels_MusicVars[256];
GFXLIST_tag GfxList;

namespace {
cel_tag* CelList_153[64];
uint16_t CelListCount_154 = 0;

LLNODE_tag* node(obj_tag* obj) { return reinterpret_cast<LLNODE_tag*>(obj); }

// Cor da luz da sala: os pares de bytes 0x9a/0x9b, 0x9c/0x9d e 0x9e/0x9f de cel_tag::misc, escolhidos
// pelo canal de interruptor da sala (misc[0xa2 - 0x98]).
void copy_room_color(obj_tag* obj) {
    const uint8_t* misc = obj->cel->misc;
    const uint8_t k = switch_channels[misc[0xa2 - 0x98]];
    obj->color[0] = misc[0x9a - 0x98 + k];
    obj->color[1] = misc[0x9c - 0x98 + k];
    obj->color[2] = misc[0x9e - 0x98 + k];
}

bool inside_box(const cel_tag* c, const _VECTOR* p) {
    return c->box_min.x <= p->x && p->x <= c->box_max.x && c->box_min.z <= p->z && p->z <= c->box_max.z &&
           c->box_min.y <= p->y && p->y <= c->box_max.y;
}
}  // namespace

void object_debug_power_on() {
    std::memset(&DynamicObjList, 0, sizeof(DynamicObjList));
    DynamicObjCount = 0;
    std::memset(&ForcedList, 0, sizeof(ForcedList));
    std::memset(switch_channels, 0, sizeof(switch_channels));
    std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
    std::memset(switch_channels_prev, 0, sizeof(switch_channels_prev));
    std::memset(switch_channels_hold, 0, sizeof(switch_channels_hold));
    std::memset(switch_channels_MusicVars, 0, sizeof(switch_channels_MusicVars));
    std::memset(&GfxList, 0, sizeof(GfxList));
    CelListCount_154 = 0;
}

// [decomp] 0x00134310 Init_SwitchChannels
// Desliga os 256 canais de switch (e o histórico de dois ticks e a contagem do alarme) e marca a hora
// de todos com o quadro atual. ResetMap_Load chama fora do reinício a quente.
void Init_SwitchChannels() {
    const uint32_t now = GameState.frame;
    for (uint32_t i = 0; i < 0x100; ++i) {
        switch_channels_time[i] = now;
        switch_channels_MusicVars[i] = 0;
        switch_channels[i] = 0;
        switch_channels_prev[i] = 0;
        switch_channels_hold[i] = 0;
    }
}

// [decomp] 0x00134388 control_init_object_lists
void control_init_object_lists() {
    DynamicObjList.prev = nullptr;
    DynamicObjList.next = nullptr;
    DynamicObjCount = 0;
    LList_Init(&ForcedList, 0x110, 0);
}

// [decomp] 0x001344e0 control_create_object
// Aloca o cabeçalho de 0x110 bytes mais data_size bytes do tipo, zera tudo, monta a matriz (pela
// rotação de Euler ou pelo quatérnio) e põe o objeto no começo de DynamicObjList.
obj_tag* control_create_object(uint32_t data_size, _VECTOR* pos, _VECTOR* rot, quaternion_tag* quat) {
    obj_tag* obj = static_cast<obj_tag*>(Mem_Malloc(data_size + 0x110, 0x404, 0));
    if (obj == nullptr) return nullptr;
    std::memset(obj, 0, data_size + 0x110);
    control_init_object(obj);
    if (pos != nullptr) Vec_Copy2(pos, &obj->pos, &obj->old_pos);
    if (rot != nullptr) Vec_Copy2(rot, &obj->rot, &obj->old_rot);
    if (quat == nullptr) {
        View_RotTransMatrix(&obj->rot, &obj->pos, &obj->matrix);
    } else {
        Quat_QuatToMat(quat, &obj->matrix);
        Mat_SetTrans(&obj->pos, &obj->matrix);
    }
    obj->data = reinterpret_cast<uint8_t*>(obj) + 0x110;
    control_add_object_to_list(&DynamicObjList, obj);
    ++DynamicObjCount;
    obj->forced.prev = nullptr;
    obj->forced.next = nullptr;
    return obj;
}

// [decomp] 0x00134630 control_init_object
void control_init_object(obj_tag* obj) {
    if (obj == nullptr) return;
    obj->unknown_f8 = 0x1f;
    obj->life |= 2;
    obj->color[2] = 0xff;
    obj->created = GameState.frame;
    obj->unknown_fc = 2;
    obj->scale = 1.0f;
    obj->unknown_103[0] = obj->unknown_103[1] = obj->unknown_103[2] = obj->unknown_103[3] = 0xff;
    obj->color[0] = 0xff;
    obj->color[1] = 0xff;
}

// [decomp] 0x00134698 control_add_object_to_list
// Insere logo depois da cabeça.
void control_add_object_to_list(obj_tag* head, obj_tag* obj) {
    obj_tag* first = head->next;
    obj->prev = head;
    obj->next = first;
    head->next = obj;
    if (first != nullptr) first->prev = obj;
}

// [decomp] 0x001346b8 control_link_object_to_cel
// Sem célula, procura a sala pela posição. A cor da luz da sala só é copiada se ainda não foi
// definida (0xffffff).
void control_link_object_to_cel(obj_tag* obj, cel_tag* cel) {
    if (cel == nullptr) {
        build_LinkToRoom(obj, 0, glb_world);
    } else {
        if (cel->objects != nullptr) cel->objects->cel_prev = obj;
        obj->cel_prev = cel;
        obj->cel_next = cel->objects;
        cel->objects = obj;
        cel->unknown_0c = 0;
        obj->cel = cel;
    }
    if (obj->cel != nullptr && obj->color[0] == 0xff && obj->color[1] == 0xff && obj->color[2] == 0xff) {
        copy_room_color(obj);
    }
}

// [decomp] 0x00134608 control_unlink_object
void control_unlink_object(obj_tag* obj) {
    auto* prev = static_cast<obj_tag*>(obj->cel_prev);
    obj_tag* next = obj->cel_next;
    // O anterior pode ser a célula: cel_tag::objects e obj_tag::cel_next estão no mesmo deslocamento.
    if (prev != nullptr) prev->cel_next = next;
    if (next != nullptr) next->cel_prev = prev;
    obj->cel = nullptr;
    obj->cel_prev = nullptr;
    obj->cel_next = nullptr;
}

// [decomp] 0x00133178 control_delete_object
void control_delete_object(obj_tag* obj) {
    void* mem = obj;
    if (control_funcs[obj->type].remove != nullptr) control_funcs[obj->type].remove(obj);
    Collide_FreeHitList(&obj->hits);
    uint32_t mp_objects;
    std::memcpy(&mp_objects, MPSettings + 0x184, 4);
    if (mp_objects != 0) MP_objectBeingDeleted(obj);
    obj->type = 1;
    if ((obj->flags & 0x30000000) != 0) LList_Remove(&ForcedList, node(obj));
    AnimObjectDelete(obj);
    SP_RemoveObj(obj, obj->script);
    obj_tag* cel_next = obj->cel_next;
    --DynamicObjCount;
    auto* cel_prev = static_cast<obj_tag*>(obj->cel_prev);
    if (cel_prev != nullptr) cel_prev->cel_next = cel_next;
    if (cel_next != nullptr) cel_next->cel_prev = cel_prev;
    obj_tag* prev = obj->prev;
    obj_tag* next = obj->next;
    obj->cel_prev = nullptr;
    obj->cel_next = nullptr;
    obj->cel = nullptr;
    if (prev != nullptr) prev->next = next;
    if (next != nullptr) next->prev = prev;
    obj->next = nullptr;
    obj->prev = nullptr;
    Mem_Free(&mem);
}

// [decomp] 0x00134300 control_first_object
obj_tag* control_first_object() { return DynamicObjList.next; }

// [decomp] 0x00134878 control_init_collision
void control_init_collision(obj_tag*) {}

// [decomp] 0x001341f0 control_AnimUpdate
void control_AnimUpdate(obj_tag* obj) { AnimObjectUpdate(obj); }

// [decomp] 0x00134960 Control_SetGList
// Troca o modelo; as flags passam a ser as do modelo, mantendo os bits de controle 0x30008000.
void Control_SetGList(obj_tag* obj, celglist_tag* glist) {
    if (obj == nullptr || glist == nullptr) return;
    obj->glist = glist;
    obj->flags = glist->flags | (obj->flags & 0x30008000);
    if ((glist->flags & 0x40000) != 0) obj->unknown_f8 &= 0xffef;
}

// [decomp] 0x00133070 Control_BuildWorldSph
// Recalcula a matriz e leva a esfera do modelo para o mundo; o raio cresce com |scale|.
void Control_BuildWorldSph(obj_tag* obj) {
    Control_GetObjMatrix(obj, &obj->matrix);
    obj->old_sphere = obj->sphere;
    const MATRIX& m = obj->matrix;
    // Como a VU0: o resultado (só xyz) vai para o registro da esfera de origem, então o w gravado
    // é o da origem.
    const auto transform = [&](const _VECTOR& s) {
        obj->sphere.x = m.m[0][0] * s.x + m.m[1][0] * s.y + m.m[2][0] * s.z + m.m[3][0];
        obj->sphere.y = m.m[0][1] * s.x + m.m[1][1] * s.y + m.m[2][1] * s.z + m.m[3][1];
        obj->sphere.z = m.m[0][2] * s.x + m.m[1][2] * s.y + m.m[2][2] * s.z + m.m[3][2];
        obj->sphere.w = s.w;
    };
    if (obj->glist == nullptr) {
        obj->sphere.x = m.m[3][0];
        obj->sphere.y = m.m[3][1];
        obj->sphere.z = m.m[3][2];
        if ((obj->mode & 0x100) != 0) return;
        obj->sphere.w = 0.1f;
        return;
    }
    if ((obj->mode & 8) == 0) {
        transform(obj->glist->sphere);
        if ((obj->mode & 0x100) != 0) return;
        obj->sphere.w = obj->glist->sphere.w * std::fabs(obj->scale);
        return;
    }
    _VECTOR src;
    std::memcpy(&src, obj->sphere_src + 0x80, sizeof(src));
    transform(src);
    obj->sphere.w = src.w * std::fabs(obj->scale);
}

// [decomp] 0x00134210 Control_GetObjMatrix
// Monta a matriz pela posição e rotação (modo 2: só a translação; modo 4: matriz fixa) e aplica a
// escala, normalizando antes.
void Control_GetObjMatrix(obj_tag* obj, MATRIX* m) {
    if ((obj->mode & 4) == 0) {
        if ((obj->mode & 2) == 0) {
            View_RotTransMatrix(&obj->rot, &obj->pos, m);
        } else {
            m->m[3][0] = obj->pos.x;
            m->m[3][1] = obj->pos.y;
            m->m[3][2] = obj->pos.z;
        }
    }
    if (obj->scale != 1.0f && (obj->mode & 0x200) == 0) {
        // Peculiaridade mantida: normaliza e escala obj->matrix, não "m".
        MATRIX* own = &obj->matrix;
        Mat_Normalize(own);
        if ((obj->mode & 1) == 0) {
            Mat_Scale3f(own, own, obj->scale, obj->scale, obj->scale);
        } else {
            Mat_Scale3f(own, own, 1.0f, 1.0f, obj->scale);
        }
    }
}

// [decomp] 0x001343c8 Control_CreateObjEx
obj_tag* Control_CreateObjEx(uint16_t data_size, _VECTOR* pos, _VECTOR* rot, MATRIX* m, celglist_tag* glist,
                             obj_tag* owner, int8_t link, uint16_t mode, float scale, uint16_t flags, uint8_t c0,
                             uint8_t c1, uint8_t c2) {
    obj_tag* obj = control_create_object(data_size, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    if (m != nullptr) {
        Mat_Copy(m, &obj->matrix);
        std::memcpy(&obj->pos, &obj->matrix.m[3][0], sizeof(_VECTOR));
    }
    obj->owner = owner;
    obj->scale = scale;
    obj->mode |= mode;
    obj->glist = glist;
    obj->unknown_103[0] = c0;
    obj->unknown_103[1] = c1;
    obj->unknown_103[2] = c2;
    obj->flags = (glist != nullptr ? glist->flags : 0) | flags;
    if (link != 0) build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001349b0 Control_NextLOD
uint32_t Control_NextLOD(obj_tag* obj) {
    if (obj != nullptr && obj->glist != nullptr && obj->glist->lod != nullptr) {
        obj->glist = obj->glist->lod;
        return 1;
    }
    return 0;
}

// [decomp] 0x00134a38 Control_ReturnNextObjectOfType
obj_tag* Control_ReturnNextObjectOfType(uint8_t type, obj_tag* after) {
    for (obj_tag* o = after != nullptr ? after->next : DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == type) return o;
    }
    return nullptr;
}

// [decomp] 0x001349e8 Control_DeleteAllObjectsOfType
// Só marca o pedido de remoção (bit 0 de life); quem apaga é o laço de atualização.
void Control_DeleteAllObjectsOfType(uint8_t type) {
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == type && (o->life & 1) == 0) o->life |= 1;
    }
}

// [decomp] 0x00134a90 Control_ObjectReferenced
void Control_ObjectReferenced(obj_tag* obj, int8_t referenced) {
    if (referenced != 0) {
        obj->mode |= 0x400;
    } else {
        obj->mode &= 0xfbff;
    }
}

// [decomp] 0x00134940 Control_SetEffectType
void Control_SetEffectType(obj_tag* obj, uint32_t flags) { obj->flags = flags | (obj->flags & 0x30008000); }

namespace {
// Estáticas de Control_InheritVelocity (pt1.185, Delta.183...), globais no original.
_VECTOR InheritPt1, InheritDelta, InheritDeltaV, InheritOldR, InheritNewR, InheritDeltaR;
MATRIX InheritM;
}  // namespace

// [decomp] 0x00133e88 Control_InheritVelocity
// Quem está sobre "from" (uma plataforma) anda o que ela andou neste tick e, se ela girou, gira em
// volta dela (posição e ângulo y; com matriz fixa, a matriz também). O fator não é usado.
void Control_InheritVelocity(obj_tag* obj, obj_tag* from, float) {
    const auto sub = [](const _VECTOR& a, const _VECTOR& b) {
        return _VECTOR{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    };
    const auto add = [](const _VECTOR& a, const _VECTOR& b) {
        return _VECTOR{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    };
    _VECTOR& row3 = *reinterpret_cast<_VECTOR*>(&obj->matrix.m[3][0]);
    Vec_Zero(&InheritOldR);
    Vec_Zero(&InheritNewR);
    InheritPt1 = sub(obj->pos, obj->old_pos);
    InheritDelta = sub(from->pos, from->old_pos);
    InheritDeltaV = sub(InheritPt1, InheritDelta);
    if ((obj->mode & 4) == 0) obj->pos = add(InheritDelta, obj->pos);
    else row3 = add(InheritDelta, row3);
    if ((from->mode & 6) == 0) {
        InheritOldR.y = from->old_rot.y;
        InheritNewR.y = from->rot.y;
    } else {
        _VECTOR dir;
        Mat_GetDir(&dir, &from->matrix);
        const _VECTOR& o = from->old_rot;
        InheritOldR.x = std::atan2(o.y, std::sqrt(o.x * o.x + o.z * o.z));
        InheritOldR.y = std::atan2(o.x, o.z);
        InheritNewR.x = std::atan2(dir.y, std::sqrt(dir.x * dir.x + dir.z * dir.z));
        InheritNewR.y = std::atan2(dir.x, dir.z);
    }
    InheritDeltaR = sub(InheritOldR, InheritNewR);
    if (Vec_IsEqual(&InheritDeltaR, &CONSTANT_ZERO_VECTOR, 0.0002f)) return;
    InheritPt1 = sub(row3, *reinterpret_cast<const _VECTOR*>(&from->matrix.m[3][0]));
    RotMatrix(&InheritDeltaR, &InheritM);
    const MATRIX& m = InheritM;
    const _VECTOR& p = InheritPt1;
    InheritDeltaV = {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z,
                     m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z,
                     m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z, m.m[0][3]};
    InheritPt1 = sub(InheritPt1, InheritDeltaV);
    if ((obj->mode & 4) == 0) obj->pos = add(obj->pos, InheritPt1);
    else row3 = add(row3, InheritPt1);
    obj->rot.y -= InheritDeltaR.y;
    if ((obj->mode & 6) != 0) {
        Mat_Inverse(&InheritM);
        Concat(&InheritM, &obj->matrix);
    }
}

// [decomp] 0x00134118 Control_Plr2Ind
// Índice do jogador (0-3) de um objeto jogador ou personagem (bots no multijogador), ou -1.
int32_t Control_Plr2Ind(obj_tag* obj) {
    if (obj == nullptr) return -1;
    const uint8_t* data = static_cast<const uint8_t*>(obj->data);
    int32_t index;
    switch (obj->type) {
        case 2:
        case 0x11: {
            const uint8_t* drone;
            std::memcpy(&drone, data + 0xd1c, 4);
            int16_t i;
            std::memcpy(&i, drone + 0x75c, 2);
            index = i;
            break;
        }
        case 3:
        case 0x12:
            index = int8_t(data[0x94e]);
            break;
        case 0x47:
            if (obj->state != 1) return -1;
            return int16_t(obj->unknown_f6);
        default:
            return -1;
    }
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x184, 4);
    obj_tag* owner;
    std::memcpy(&owner, MPGame + index * 0x30 + 0x1c, 4);
    if (mp != 0 && owner != obj) index = -1;
    return index;
}

// [decomp] 0x001c8cb0 build_FindCel
// Sala (célula com o bit 0x40000 no tipo) cuja caixa contém o ponto. Com mais de uma, lança raios
// para cima e para baixo e fica com a do piso ou teto mais próximo. No mundo principal, a lista de
// salas é montada na primeira chamada (gbl_room_cels, zerada a cada fase).
cel_tag* build_FindCel(const _VECTOR* point, world_tag* world) {
    CelListCount_154 = 0;
    if (world == glb_world) {
        if (gbl_room_cels == nullptr) {
            int rooms = 0;
            for (cel_tag* c = world->first; c != nullptr; c = c->next) {
                if ((c->type & 0x40000) != 0) ++rooms;
            }
            gbl_room_cels = static_cast<cel_tag**>(Mem_Malloc(uint32_t(rooms + 1) * 4, 0x904, 0));
            cel_tag** out = gbl_room_cels;
            for (cel_tag* c = world->first; c != nullptr; c = c->next) {
                if ((c->type & 0x40000) != 0) *out++ = c;
            }
            *out = nullptr;
        }
        for (cel_tag** c = gbl_room_cels; *c != nullptr && CelListCount_154 < 0x40; ++c) {
            if (inside_box(*c, point)) CelList_153[CelListCount_154++] = *c;
        }
    } else {
        for (cel_tag* c = world->first; c != nullptr && CelListCount_154 < 0x40; c = c->next) {
            if ((c->type & 0x40000) != 0 && inside_box(c, point)) CelList_153[CelListCount_154++] = c;
        }
    }
    if (CelListCount_154 == 1) return CelList_153[0];
    if (CelListCount_154 == 0) return nullptr;
    float best = 256.0f;
    cel_tag* found = nullptr;
    float reach = 256.0f;
    for (uint16_t i = 0; i < CelListCount_154; ++i) {
        HITDATA_tag* up = nullptr;
        HITDATA_tag* down = nullptr;
        _VECTOR to{point->x, point->y + reach, point->z, 0.0f};
        Collide_RayIntersect(point, &to, CelList_153[i], nullptr, nullptr, &up, 1, 0x90, 0);
        if (up != nullptr && up->dist < reach) reach = up->dist;
        to = {point->x, point->y - reach, point->z, 0.0f};
        Collide_RayIntersect(point, &to, CelList_153[i], nullptr, nullptr, &down, 1, 0x90, 0);
        if (down != nullptr && down->dist < reach) reach = down->dist;
        HITDATA_tag* nearest = down;
        if (up != nullptr) {
            nearest = up;
            if (down != nullptr && down->dist <= up->dist) nearest = down;
        }
        if (nearest != nullptr && nearest->dist < best) {
            found = CelList_153[i];
            best = nearest->dist;
        }
        Collide_FreeHitList(&up);
        Collide_FreeHitList(&down);
    }
    return found != nullptr ? found : CelList_153[0];
}

// [decomp] 0x001c9680 build_LinkToRoom
// Objetos com o bit 0x20000000 não têm sala: vão para a ForcedList (desenhados sempre).
uint32_t build_LinkToRoom(obj_tag* obj, int8_t straddle, world_tag* world) {
    if ((obj->flags & 0x20000000) != 0) {
        if ((obj->flags & 0x10000000) == 0) {
            obj->flags |= 0x10008000;
            LList_Add(&ForcedList, node(obj));
        }
        return 0;
    }
    Control_BuildWorldSph(obj);
    const _VECTOR center = obj->sphere;
    cel_tag* cel = build_FindCel(&center, world);
    if (cel == nullptr) return 0;
    control_unlink_object(obj);
    control_link_object_to_cel(obj, cel);
    copy_room_color(obj);
    if (straddle != 0) Controls_StraddleTest(obj);
    return 1;
}

// [decomp] 0x001c95a8 build_PointOnFloor
// Raio de *p ao longo de dir (para baixo, sem dir) por len; se acertar algo, *p vai para o ponto.
bool build_PointOnFloor(cel_tag* cel, obj_tag* obj, _VECTOR* p, float len, _VECTOR* dir) {
    _VECTOR down{0.0f, -1.0f, 0.0f, 0.0f};
    const _VECTOR* d = dir != nullptr ? dir : &down;
    const _VECTOR to{p->x + d->x * len, p->y + d->y * len, p->z + d->z * len, 0.0f};
    HITDATA_tag* hits = nullptr;
    const bool hit = Collide_RayIntersect(p, &to, cel, obj, nullptr, &hits, 0, 0x70c, 0);
    if (hit) {
        *p = hits->pos;
        Collide_FreeHitList(&hits);
    }
    return hit;
}

// [decomp] 0x001c9150 build_link_objects_to_rooms
// Põe cada célula estática dos tipos 0x8017-0x8019 dentro da sala (tipos 0xc023 e 0xc047) cuja
// geometria está mais perto do centro dela: entre as salas maiores que a célula e cuja caixa contém
// o centro, a do acerto mais próximo de uma esfera de raio 4000 (que vai encolhendo).
void build_link_objects_to_rooms(cel_tag* first) {
    for (cel_tag* c = first; c != nullptr; c = c->next) {
        if (uint32_t(uint16_t(c->type)) - 0x8017 >= 3) continue;
        float best = 4000.0f;
        cel_tag* room = nullptr;
        for (cel_tag* r = first; r != nullptr; r = r->next) {
            const uint16_t rt = uint16_t(r->type);
            if (rt != 0xc047 && rt != 0xc023) continue;
            if (!(c->sphere.w <= r->sphere.w)) continue;
            const _VECTOR& p = c->sphere;
            const bool inside = r->box_min.x <= p.x && p.x <= r->box_max.x && r->box_min.z <= p.z &&
                                p.z <= r->box_max.z && r->box_min.y <= p.y && !(r->box_max.y < p.y);
            if (!inside) continue;
            _VECTOR center = c->sphere;
            HITDATA_tag* hits = nullptr;
            if (Collide_SphereIntersect(&center, best + 1.0f, r, nullptr, nullptr, &hits, 1, 0, 0x90, 4)) {
                if (hits->dist < best) {
                    room = hits->cel;
                    best = hits->dist;
                }
                Collide_FreeHitList(&hits);
            }
        }
        if (room == nullptr) continue;
        c->parent = room;
        c->children = room->children;
        room->children = c;
        if (uint16_t(c->type) == 0x8017) std::memcpy(&room->water_y, &c->box_min.y, 4);
    }
}

// [decomp] 0x001c8ac8 build_LinkDoors2Portals
// Liga cada porta marcada para isso (Door_Link2Portal) ao portal que ela cobre em cada sala tocada
// pela esfera dela: o centro a menos de 0,25 do plano do portal e dentro do quadrilátero. A porta
// passa a atravessar salas (flag 0x8000) e o portal e o oposto guardam a porta em unknown_0c, se
// ainda não tinham uma; fechada, ela esconde a sala do outro lado (Vision_Recurse).
void build_LinkDoors2Portals(cel_tag*, obj_tag* objects) {
    for (obj_tag* o = objects; o != nullptr; o = o->next) {
        if (o->type != 0x1c || o->cel == nullptr || Door_Link2Portal(o) == 0) continue;
        const _VECTOR c = o->sphere;
        HITTEST_tag test;
        std::memset(&test, 0, sizeof(test));
        test.pos = o->sphere;
        test.radius = o->sphere.w;
        test.cel = o->cel;
        test.type = 0x101;
        uint16_t count = 0;
        cel_tag** cels = Collide_StraddleCels(&test, &count);
        for (uint16_t i = 0; i < count; ++i) {
            for (portal_tag* p = cels[i]->portals; p != nullptr; p = p->next) {
                const plane_equ_tag& n = p->plane;
                if (!(std::fabs(((c.x * n.a + c.y * n.b) + c.z * n.c) + n.d) < 0.25f)) continue;
                if (vecutil_point_on_poly(&c, &p->verts[0], &p->verts[1], &p->verts[2], &n) == 0 &&
                    vecutil_point_on_poly(&c, &p->verts[2], &p->verts[3], &p->verts[0], &n) == 0) {
                    continue;
                }
                const uint32_t door = uint32_t(reinterpret_cast<uintptr_t>(o));
                o->flags |= 0x8000;
                if (p->unknown_0c == 0) p->unknown_0c = door;
                if (p->opposite->unknown_0c == 0) p->opposite->unknown_0c = door;
                break;
            }
        }
    }
}

// [decomp] 0x00134778 Controls_StraddleTest
// Objeto que atravessa mais de uma sala entra na ForcedList (bit 0x10000000).
uint32_t Controls_StraddleTest(obj_tag* obj) {
    if ((obj->flags & 0x20000000) != 0) {
        obj->flags |= 0x8000;
        return 1;
    }
    HITTEST_tag test;
    std::memset(&test, 0, sizeof(test));
    test.pos = obj->sphere;
    test.radius = obj->sphere.w;
    test.cel = obj->cel;
    test.type = 0x101;
    uint16_t cels = 0;
    Collide_StraddleCels(&test, &cels);
    if (cels < 2) {
        if ((obj->flags & 0x10000000) != 0) LList_Remove(&ForcedList, node(obj));
        obj->forced.prev = nullptr;
        obj->forced.next = nullptr;
        obj->flags &= 0xefff7fff;
    } else {
        if ((obj->flags & 0x10000020) == 0) {
            LList_Add(&ForcedList, node(obj));
            obj->flags |= 0x10000000;
        }
        obj->flags |= 0x8000;
    }
    return 0;
}

// [decomp] 0x001e5168 View_RotTransMatrix
// Rotação de Euler com o seno da VU0 e translação. Não escreve a coluna 3 (w).
void View_RotTransMatrix(const _VECTOR* rot, const _VECTOR* pos, MATRIX* m) {
    float cx, sx, cy, cz, sz, sy;
    PS2Sinf3(rot->x + 1.5707964f, rot->x, rot->y + 1.5707964f, &cx, &sx, &cy);
    PS2Sinf3(rot->z + 1.5707964f, rot->z, rot->y, &cz, &sz, &sy);
    m->m[0][1] = sz;
    m->m[1][2] = cy * sx + cx * sy * sz;
    m->m[2][0] = cx * sy + cy * sx * sz;
    m->m[2][2] = cx * cy - sx * sy * sz;
    m->m[0][2] = -(cz * sy);
    m->m[1][0] = sx * sy - cx * cy * sz;
    m->m[2][1] = -(cz * sx);
    m->m[0][0] = cy * cz;
    m->m[1][1] = cx * cz;
    m->m[3][0] = pos->x;
    m->m[3][1] = pos->y;
    m->m[3][2] = pos->z;
}

// [decomp] 0x001e4ff0 View_RotTransScaleMatrix
void View_RotTransScaleMatrix(const _VECTOR* rot, const _VECTOR* pos, const _VECTOR* scale, MATRIX* m) {
    float cx, sx, cy, cz, sz, sy;
    PS2Sinf3(rot->x + 1.5707964f, rot->x, rot->y + 1.5707964f, &cx, &sx, &cy);
    PS2Sinf3(rot->z + 1.5707964f, rot->z, rot->y, &cz, &sz, &sy);
    m->m[0][2] = -(cz * sy) * scale->x;
    m->m[0][0] = cy * cz * scale->x;
    m->m[0][1] = sz * scale->x;
    m->m[1][0] = (sx * sy - cx * cy * sz) * scale->y;
    m->m[1][2] = (cy * sx + cx * sy * sz) * scale->y;
    m->m[1][1] = cx * cz * scale->y;
    m->m[2][0] = (cx * sy + cy * sx * sz) * scale->z;
    m->m[2][1] = -(cz * sx) * scale->z;
    m->m[2][2] = (cx * cy - sx * sy * sz) * scale->z;
    m->m[3][0] = pos->x;
    m->m[3][1] = pos->y;
    m->m[3][2] = pos->z;
}

// [decomp] 0x001e79a0 View_AddSkyObj
// Registra um elemento do céu (até 21). Com "flash", o modelo é o do relâmpago, que aparece a
// intervalos aleatórios de até "period" segundos por "flash_frames" quadros.
void View_AddSkyObj(uint16_t slot, celglist_tag* glist, const _VECTOR* pos, const _VECTOR* rot, obj_tag* obj,
                    int8_t flash, uint16_t period, uint16_t flash_frames, int8_t layer) {
    if (slot >= 0x15) return;
    SKYOBJ_tag& s = GfxList.sky[slot];
    if (flash == 0) {
        s.glist = glist;
        s.obj = obj;
    } else {
        s.flash_frames = flash_frames;
        s.glist_flash = glist;
        s.obj_flash = obj;
        s.flash_period = static_cast<int16_t>(float(period) * FRAME_RATE);
    }
    s.layer = layer;
    s.flash_left = s.flash_wait = s.thunder_wait = 0;
    if (obj != nullptr) obj->flags |= 0x10;
    if (pos == nullptr) {
        Vec_Zero(&s.pos);
    } else {
        s.pos = *pos;
    }
    if (rot == nullptr) {
        Vec_Zero(&s.rot);
    } else {
        s.rot = *rot;
    }
}

namespace {
// Ângulo de volta para (-pi, pi), como no laço de control_movement_object_handler.
float wrap_angle(float a) {
    if (!(3.1415927f <= std::fabs(a))) return a;
    const float m = std::fmod(a, 6.2831855f);
    if (!(3.1415927f <= std::fabs(m))) return m;
    return 0.0f <= a ? m - 6.2831855f : m + 6.2831855f;
}

// Objetos que se mexeram (mode 0x20): ângulos de volta ao intervalo, esfera, sala e luz; a posição
// e a rotação atuais viram as antigas. Os de mode 0x40 tiram a posição da matriz.
void settle_moved_objects() {
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if ((o->mode & 0x20) != 0) {
            o->rot.x = wrap_angle(o->rot.x);
            o->rot.y = wrap_angle(o->rot.y);
            o->rot.z = wrap_angle(o->rot.z);
            Control_BuildWorldSph(o);
            control_handle_cel_change(o);
            Lights_CalcAmbientLight(o);
            o->old_pos = o->pos;
            if ((o->mode & 6) == 0) o->old_rot = o->rot;
        }
        if ((o->mode & 0x40) != 0) o->pos = *reinterpret_cast<const _VECTOR*>(&o->matrix.m[3][0]);
    }
}
}  // namespace

// [decomp] 0x001336f8 control_movement_object_handler
// Um tick dos objetos: portas e gatilhos, salas que trocam de modelo com um canal de switch,
// atualização de cada objeto (control_funcs), remoção dos marcados (corrigindo quem apontava para
// eles), acomodação dos que se mexeram, colisão, de novo a acomodação, e por fim os objetos
// presos a um dono (owner) seguem a sala dele.
// Divergência: salas com canal de switch e sem modelo são puladas (o original leria o endereço 0x40).
void control_movement_object_handler(int8_t) {
    Drone_InitComms();
    MP_DebugDraw();
    Doors_Calc();
    Trigger_Calc();
    for (cel_tag* c = glb_world->first; c != nullptr; c = c->next) {
        const uint8_t ch = c->misc[0xa2 - 0x98];
        if (ch == 0 || c->glist == nullptr) continue;
        celglist_tag* alt = c->glist->lod;
        if (alt == nullptr || uint16_t(c->type) != 0xc047) continue;
        if (switch_channels[ch] != switch_channels_hold[ch]) c->glist = alt;
    }
    amemcpy(switch_channels_prev, switch_channels_hold, 0x100);
    amemcpy(switch_channels_hold, switch_channels, 0x100);
    for (obj_tag* o = DynamicObjList.next; o != nullptr;) {
        obj_tag* next = o->next;
        if ((o->mode & 6) != 0) Mat_GetDir(&o->old_rot, &o->matrix);
        if (control_funcs[o->type].update != nullptr) control_funcs[o->type].update(o);
        o = next;
    }
    bool again;
    do {
        again = false;
        for (obj_tag* o = DynamicObjList.next; o != nullptr;) {
            obj_tag* next = o->next;
            if ((o->life & 1) == 0) {
                if ((o->life & 2) != 0) {
                    Control_BuildWorldSph(o);
                    Lights_CalcAmbientLight(o);
                    o->life = uint8_t(o->life & 0xfd);
                }
            } else {
                const uint16_t mode = o->mode;
                Effect_RemoveSObjs(o);
                control_delete_object(o);
                if ((mode & 0x400) != 0) {
                    // Outros objetos podiam apontar para o removido: os tipos 5 e 0xf nos dados, e
                    // qualquer um na lista de acertos.
                    for (obj_tag* p = DynamicObjList.next; p != nullptr; p = p->next) {
                        auto* data = static_cast<uint8_t*>(p->data);
                        const auto clear = [&](uint32_t offset) {
                            obj_tag* ref;
                            std::memcpy(&ref, data + offset, 4);
                            if (ref == o) std::memset(data + offset, 0, 4);
                        };
                        if (p->type == 5) {
                            clear(0x30);
                            clear(0x40);
                        } else if (p->type == 0xf) {
                            clear(0x24);
                        }
                        for (HITDATA_tag* h = p->hits; h != nullptr; h = h->next) {
                            if (h->obj == o) h->obj = nullptr;
                        }
                    }
                }
                again = true;
            }
            o = next;
        }
    } while (again);
    settle_moved_objects();
    Collide_Update(DynamicObjList.next);
    settle_moved_objects();
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->unknown_fc != 0) --o->unknown_fc;
        if (o->sphere_src != nullptr && GameState.skip_draw == 0) std::memset(o->sphere_src + 0x110, 0xff, 4);
        obj_tag* owner = o->owner;
        if (owner == nullptr || o->cel == owner->cel) continue;
        control_unlink_object(o);
        control_link_object_to_cel(o, owner->cel);
        Control_BuildWorldSph(o);
    }
}

// [decomp] 0x00133288 control_handle_cel_change
// Depois que um objeto se mexeu: objetos grandes ou marcados (0x30000000) entram ou saem da
// ForcedList conforme atravessem salas; os outros seguem os portais cruzados (Cel_ObjectLeftCel)
// ou, se saíram da caixa da sala, procuram outra. Sem sala nenhuma, cada tipo reage do seu jeito:
// o jogador cai do mapa, a maioria volta para dentro da caixa da sala antiga, projéteis somem.
void control_handle_cel_change(obj_tag* obj) {
    if (obj == nullptr) return;
    cel_tag* cel = obj->cel;
    obj->mode &= 0xffdf;
    if (cel == nullptr) {
        if (build_LinkToRoom(obj, 0, glb_world) != 0) return;
        if (obj->type == 2) Drone_FellOutMap(obj);
        return;
    }
    if (obj->owner != nullptr) return;
    if (Vec_IsEqual(&obj->sphere, &obj->old_sphere, 0.0f)) return;
    const _VECTOR* from = &obj->old_sphere;
    const _VECTOR* to = &obj->sphere;
    if ((obj->flags & 0x30000000) != 0 || 0.1f < obj->sphere.w) {
        if ((obj->flags & 0x20000000) != 0) {
            obj->flags |= 0x8000;
            return;
        }
        HITTEST_tag test;
        std::memset(&test, 0, sizeof(test));
        test.pos = obj->sphere;
        test.radius = obj->sphere.w;
        test.cel = obj->cel;
        test.type = 0x101;
        uint16_t count = 0;
        Collide_StraddleCels(&test, &count);
        if (count < 2) {
            if ((obj->flags & 0x10000000) != 0) LList_Remove(&ForcedList, node(obj));
            obj->forced.prev = nullptr;
            obj->forced.next = nullptr;
            obj->flags &= 0xefff7fff;
        } else {
            if ((obj->flags & 0x10000020) == 0) {
                LList_Add(&ForcedList, node(obj));
                obj->flags |= 0x10000000;
            }
            obj->flags |= 0x8000;
        }
    }
    if ((obj->mode & 0x80) == 0) {
        from = &obj->old_pos;
        to = &obj->pos;
    }
    cel_tag* crossed = Cel_ObjectLeftCel(obj->cel, from, to);
    if (crossed != nullptr) {
        control_unlink_object(obj);
        control_link_object_to_cel(obj, crossed);
        return;
    }
    if (build_point_in_bounding_box(&obj->pos, cel) != 0) return;
    control_unlink_object(obj);
    if (build_LinkToRoom(obj, 0, glb_world) != 0) return;
    switch (obj->type) {
        case 2: {
            Drone_FellOutMap(obj);
            uint32_t mp;
            std::memcpy(&mp, MPSettings + 0x184, 4);
            if (mp != 0) return;
        }
            [[fallthrough]];
        default:
            if ((obj->flags & 0x20) == 0) {
                // Volta para dentro da caixa da sala antiga, a um raio das paredes.
                const float r = obj->sphere.w;
                obj->pos = obj->old_pos;
                const auto clamp = [r](float v, float lo, float hi) {
                    if (v < lo + r) return lo + r;
                    return v < hi - r ? v : hi - r;
                };
                obj->pos.x = clamp(obj->pos.x, cel->box_min.x, cel->box_max.x);
                obj->pos.y = clamp(obj->pos.y, cel->box_min.y, cel->box_max.y);
                obj->pos.z = clamp(obj->pos.z, cel->box_min.z, cel->box_max.z);
                obj->old_sphere = obj->pos;
                *reinterpret_cast<_VECTOR*>(&obj->matrix.m[3][0]) = obj->pos;
            }
            [[fallthrough]];
        case 0x15:
        case 0x1c:
        case 0x25:
        case 0x3d:
            control_link_object_to_cel(obj, cel);
            break;
        case 5:
        case 6:
        case 0x10:
            obj->life |= 1;
            break;
        case 0xd:
            obj->state = 2;
            View_SetDrawInNoViews(obj);
            break;
        case 0x28:
            Car_Deactivate(obj);
            break;
    }
}
