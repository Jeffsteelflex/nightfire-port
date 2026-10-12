// Portas. Ver door.h.
//
// Divergências (para não ler fora de uma tabela nem seguir um ponteiro nulo):
// - Door_IsClosed trata grupo de portas fora da tabela (104 entradas) como aberto.
// - Door_Activate sem quem ativa (by nulo, o jogador ainda não existe no port) não faz nada nos
//   casos em que precisaria da posição dele.

#include "engine/door.h"

#include <cmath>
#include <cstring>

#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/spline.h"
#include "engine/vecmath.h"
#include "engine/view.h"

LLISTINFO_tag TheDoors;
int16_t DoorGroupsStates[104];

namespace {

DOORINFO* info(obj_tag* door) { return static_cast<DOORINFO*>(door->data); }
const DOORINFO* info(const obj_tag* door) { return static_cast<const DOORINFO*>(door->data); }

_VECTOR* matrix_pos(obj_tag* o) { return reinterpret_cast<_VECTOR*>(&o->matrix.m[3][0]); }

// Ninguém (jogador, IA ou tipo 0x12) encostado na porta? Usado antes de fechá-la sozinha.
bool nobody_near(obj_tag* door) {
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if ((o->flags & 0x10) != 0) continue;
        if (o->type != 0x12 && o->type != 3 && o->type != 2) continue;
        const float d2 = Vec_SqDist3D(&door->sphere, &o->sphere);
        float r = door->sphere.w + o->sphere.w;
        r = r * r;
        if (d2 < r + r) return false;
    }
    return true;
}

}  // namespace

// [decomp] 0x00136848 Door_Init
void Door_Init() {
    LList_Init(&TheDoors, 0x80, 0);
    std::memset(DoorGroupsStates, 0, 0xca);
}

// [decomp] 0x001367b0 Door_IsClosed
// Porta trancada para a visão (unknown_f6 == 1), ou fechada: o estado do grupo de portas, se tiver
// grupo, senão o da própria porta.
bool Door_IsClosed(const obj_tag* door) {
    if (door == nullptr) return false;
    if (door->unknown_f6 == 1) return true;
    const uint16_t group = info(door)->group;
    if (group == 0) return door->state == kDoorClosed;
    if (group >= 104) return false;
    return DoorGroupsStates[group] == kDoorClosed;
}

// [decomp] 0x00136810 Door_IsLocked
bool Door_IsLocked(obj_tag* door) {
    if (door == nullptr) return false;
    const uint16_t lock = info(door)->lock_channel;
    return lock != 0 && switch_channels[lock] == 0;
}

// [decomp] 0x00136788 Door_Link2Portal
uint8_t Door_Link2Portal(obj_tag* door) {
    if (door == nullptr) return 0;
    return (door->unknown_fd >> 3) & 1;
}

// [decomp] 0x00136100 Door_Create
// Monta a porta a partir dos parâmetros do posicionamento (ver DOORINFO) e a põe no lugar do
// caminho que corresponde ao estado inicial (fechada, ou aberta com o bit 2 de params[8] | params[9]).
obj_tag* Door_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x80, pos, rot, nullptr);
    DOORINFO* d = info(obj);
    const uint32_t* p = level->params;
    d->group = uint16_t(p[1]);
    d->channel = uint16_t(p[2]);
    d->lock_channel = uint16_t(p[3]);
    d->snd_open = p[5] != 0 ? uint16_t(p[5]) : 0xffff;
    d->snd_close = p[6] != 0 ? uint16_t(p[6]) : 0xffff;
    d->snd_locked = p[7] != 0 ? uint16_t(p[7]) : 0xffff;
    d->path = level->path;
    d->frames = uint16_t(level->path_count - 1);
    const float traversal = level->path_unknown != 0 ? float(level->path_unknown) : FRAME_RATE;
    d->speed = 1.0f / traversal;
    d->obj = obj;
    d->flags = uint16_t(uint16_t(p[8]) | uint16_t(p[9]));
    const bool open = ((int32_t(p[8] | p[9]) >> 2) & 1) != 0;
    obj->state = open ? kDoorOpen : kDoorClosed;
    obj->unknown_f6 = uint16_t(p[0]);
    obj->type = 0x1c;
    const uint8_t fd = p[4] != 0 ? uint8_t(obj->unknown_fd | 8) : obj->unknown_fd;
    obj->mode |= 0xc4;
    obj->unknown_fd = fd;
    d->one_way = uint8_t(p[10]);
    d->no_touch = uint8_t(p[11]);
    Control_SetGList(obj, glist);
    if (obj->unknown_f6 == 0) {
        Door_SetupSwing(obj, level);
    } else {
        d->pos = (obj->state & kDoorOpen) != 0 ? 1.0f : 0.0f;
        Spline_Interp(d->path, 0, d->frames, 0.0f, &d->hinge, nullptr, 2);
        Mat_Inverse(&d->hinge);
        // Como no original, a inversa é sobrescrita logo em seguida pela matriz do fim do caminho.
        Spline_Interp(d->path, 0, d->frames, float(d->frames), &d->hinge, nullptr, 2);
        Spline_Interp(d->path, 0, d->frames, d->pos * float(d->frames), &obj->matrix, nullptr, 2);
    }
    build_LinkToRoom(obj, 1, glb_world);
    LList_Add(&TheDoors, &d->node);
    Control_BuildWorldSph(obj);
    if (open && d->channel != 0) {
        switch_channels[d->channel] = 1;
        switch_channels_time[d->channel] = GameState.frame;
    }
    Controls_StraddleTest(obj);
    return obj;
}

// [decomp] 0x00135440 Door_SetupSwing
// Porta de girar: fechada no meio do caminho. A velocidade é arredondada para que meio caminho dê um
// número inteiro de quadros; hinge guarda a inversa da matriz fechada (para saber de que lado está
// quem abre) e side o lado para o qual o caminho abre.
void Door_SetupSwing(obj_tag* door, level_tag*) {
    DOORINFO* d = info(door);
    d->speed = 0.5f / float(int(0.5f / (d->speed + d->speed)));
    d->pos = (door->state & kDoorOpen) != 0 ? 1.0f : 0.5f;
    Spline_Interp(d->path, 0, d->frames, float(d->frames) * 0.5f, &d->hinge, nullptr, 2);
    Spline_Interp(d->path, 0, d->frames, d->pos * float(d->frames), &door->matrix, nullptr, 2);
    Mat_Inverse(&d->hinge);
    MATRIX start{}, middle{};
    Spline_Interp(d->path, 0, d->frames, 0.0f, &start, nullptr, 2);
    Spline_Interp(d->path, 0, d->frames, float(d->frames) * 0.5f, &middle, nullptr, 2);
    _VECTOR dir, norm;
    Mat_GetDir(&dir, &start);
    Mat_GetNorm(&norm, &middle);
    d->side = (dir.x * norm.x + dir.y * norm.y) + dir.z * norm.z < 0.0f ? 1.0f : -1.0f;
}

// [decomp] 0x00135618 Door_SetState
// Troca o estado: som ao começar a abrir ou a fechar, e o canal de switch da porta fica ligado
// enquanto ela estava aberta ou abrindo.
void Door_SetState(obj_tag* door, uint16_t state) {
    DOORINFO* d = info(door);
    if ((state & kDoorOpening) != 0 && (door->state & kDoorOpening) == 0) {
        Sound_Play3D(d->snd_open, matrix_pos(door), 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    if ((state & kDoorClosing) != 0 && (door->state & kDoorClosing) == 0) {
        Sound_Play3D(d->snd_close, matrix_pos(door), 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    if (d->channel != 0) {
        const bool was_open = (door->state & (kDoorOpen | kDoorOpening)) != 0;
        switch_channels[d->channel] = was_open ? 1 : 0;
        if (was_open) switch_channels_time[d->channel] = GameState.frame;
    }
    door->state = state;
    d->open_time = 0;
}

// [decomp] 0x00135738 Door_Interp
// Põe a porta na posição atual do caminho e, com "advance", anda um quadro. A de girar para em 0 ou
// 1 ao abrir e em 0,5 ao fechar; a de correr para em 0 (fechada) ou 1 (aberta).
void Door_Interp(obj_tag* door, DOORINFO* d, int8_t advance) {
    const uint16_t n = d->frames;
    if ((d->flags & 1) == 0) {
        KeyFrame_Interp(d->path, 0, n, d->pos * float(n), &door->matrix);
    } else {
        Spline_Interp(d->path, 0, n, d->pos * float(n), &door->matrix, nullptr, 2);
    }
    door->mode |= 0x20;
    const float mul = FRAME_RATE_MUL;
    if (advance == 0) return;
    const float p = d->pos + d->speed * d->dir * FRAME_RATE_MUL;
    d->pos = p;
    bool moved = false;
    const int16_t kind = int16_t(door->unknown_f6);
    if (kind == 0) {
        if ((door->state & kDoorOpening) == 0 || (p <= 1.0f && 0.0f <= p)) {
            if ((door->state & kDoorClosing) != 0 && std::fabs(0.5f - d->pos) <= d->speed * mul) {
                d->pos = 0.5f;
                moved = true;
                Door_SetState(door, kDoorClosed);
            }
        } else {
            d->pos = 0.0f <= p ? (p < 1.0f ? p : 1.0f) : 0.0f;
            moved = true;
            Door_SetState(door, kDoorOpen);
        }
    } else if (0 <= kind && kind <= 2) {
        moved = p < 0.0f;
        if (moved) {
            d->pos = 0.0f;
            Door_SetState(door, kDoorClosed);
        }
        if (1.0f < d->pos) {
            d->pos = 1.0f;
            moved = true;
            Door_SetState(door, kDoorOpen);
        }
    }
    // Sempre Spline_Interp aqui, mesmo nas portas de KeyFrame (como no original).
    if (moved) Spline_Interp(d->path, 0, n, d->pos * float(n), &door->matrix, nullptr, 2);
}

// [decomp] 0x00135980 Door_ReactToHit
// Colisão: se quem bateu é a IA (tipo 3) ou o jogador andando (tipo 2), uma porta de girar em
// movimento inverte o sentido quando ia contra ele, e a de correr que fechava volta a abrir.
void Door_ReactToHit(obj_tag* door) {
    DOORINFO* d = info(door);
    if (d->cooldown != 0) return;
    obj_tag* pusher = nullptr;
    for (HITDATA_tag* h = door->hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr) continue;
        if (o->type == 2) {
            const uint8_t* pd = static_cast<const uint8_t*>(o->data);
            uint32_t flags;
            float speed;
            std::memcpy(&flags, pd + 0x4f8, 4);
            std::memcpy(&speed, pd + 0xac, 4);
            const bool idle = (flags & 0x600) != 0 || (flags & 0x100) == 0 || !(0.0f < speed) || (o->life & 1) != 0;
            if (!idle) {
                pusher = o;
                break;
            }
        } else if (o->type == 3) {
            pusher = o;
            break;
        }
    }
    if (pusher == nullptr) return;
    const int16_t kind = int16_t(door->unknown_f6);
    if (kind == 1) return;
    if (kind < 2) {
        if (kind == 0 && (door->state & (kDoorOpening | kDoorClosing)) != 0) {
            MATRIX inv;
            Mat_Copy(&door->matrix, &inv);
            Mat_Inverse(&inv);
            _VECTOR local;
            RotPreTransVec(&inv, &pusher->sphere, &local);
            float z = local.z * d->side;
            if (0.5f < d->pos) z = -z;
            if ((door->state & kDoorClosing) == 0 || -0.0002f <= z) {
                if ((door->state & kDoorOpening) == 0 || z <= 0.0002f) return;
                d->dir = -d->dir;
                Door_SetState(door, kDoorClosing);
            } else {
                d->dir = -d->dir;
                Door_SetState(door, kDoorOpening);
            }
        }
    } else if (kind == 2) {
        if ((door->state & kDoorClosing) == 0) return;
        d->dir = -d->dir;
        Door_SetState(door, kDoorOpening);
        Door_Interp(door, d, 0);
    }
    Door_Interp(door, d, 0);
    d->cooldown = 2;
    d->flags |= 0x10;
}

// [decomp] 0x00135be0 Door_Activate
// Abre ou fecha a porta a pedido de "by" (o jogador, um switch...). Trancada, só toca o som. A de
// girar abre para longe de quem a empurra e não fecha em cima dele. Devolve 1 se a porta mudou.
uint32_t Door_Activate(obj_tag* door, obj_tag* by, uint8_t touch) {
    DOORINFO* d = info(door);
    if (touch != 0 && d->no_touch != 0) return 0;
    const auto locked = [&] {
        Sound_Play3D(d->snd_locked, matrix_pos(door), 100.0f, -1.0f, -1.0f, 0, 0, 0);
        return 0u;
    };
    if (d->lock_channel != 0 && switch_channels[d->lock_channel] == 0 &&
        (door->state & (kDoorClosed | kDoorClosing)) != 0) {
        return locked();
    }
    if ((d->flags & 0x10) != 0) return 0;
    if (d->one_way != 0) {
        if (by == nullptr) return 0;
        const _VECTOR* dp = matrix_pos(door);
        const _VECTOR* bp = matrix_pos(by);
        const _VECTOR diff{dp->x - bp->x, dp->y - bp->y, dp->z - bp->z, dp->w - bp->w};
        _VECTOR fwd;
        Mat_GetDir(&fwd, &door->matrix);
        const float dot = (diff.x * fwd.x + diff.y * fwd.y) + diff.z * fwd.z;
        if (d->one_way == 1 && dot < 0.0f) return locked();
        if (d->one_way == 2 && 0.0f < dot) return locked();
    }
    d->one_way = 0;
    uint32_t changed = 0;
    const int16_t kind = int16_t(door->unknown_f6);
    if (kind == 0) {
        if ((door->state & (kDoorOpen | kDoorOpening)) != 0) {
            // Aberta: não fecha se quem pediu está no caminho da folha.
            if (by == nullptr) return 0;
            _VECTOR local;
            RotPreTransVec(&d->hinge, &by->sphere, &local);
            const float s = d->dir * d->side;
            float v = s < 0.0f ? local.z + 0.3f : local.z - 0.3f;
            v = v * d->dir * d->side;
            if (v < 0.0f && std::fabs(v) < door->sphere.w) {
                d->flags |= 0x10;
                return 0;
            }
        }
        const uint16_t st = door->state;
        if ((st & (kDoorClosed | kDoorClosing)) == 0) {
            if ((st & (kDoorOpen | kDoorOpening)) != 0) {
                d->dir = 0.5f < d->pos ? -1.0f : 1.0f;
                changed = 1;
                Door_SetState(door, kDoorClosing);
            }
        } else {
            if (by == nullptr) return 0;
            _VECTOR local;
            RotPreTransVec(&d->hinge, &by->sphere, &local);
            const float s = local.z <= 0.0f ? -1.0f : 1.0f;
            changed = 1;
            d->dir = s * d->side;
            Door_SetState(door, kDoorOpening);
        }
    } else if (0 <= kind && kind <= 2) {
        changed = 1;
        if ((door->state & (kDoorClosed | kDoorClosing)) != 0) {
            d->dir = 1.0f;
            Door_SetState(door, kDoorOpening);
        } else if ((door->state & (kDoorOpen | kDoorOpening)) != 0) {
            d->dir = -1.0f;
            Door_SetState(door, kDoorClosing);
        }
    }
    d->cooldown = 2;
    d->flags |= 0x10;
    return changed;
}

// [decomp] 0x00135f18 Door_DoMsg
// Texto da porta para o jogador (uma vez por porta, fora do multijogador), se ela não estiver
// trancada nem aberta.
char* Door_DoMsg(obj_tag* door) {
    if (door == nullptr) return nullptr;
    uint32_t mp_flag;
    std::memcpy(&mp_flag, MPSettings + 0x184, 4);
    if (mp_flag != 0) return nullptr;
    DOORINFO* d = info(door);
    if (d->msg_shown != 0) return nullptr;
    d->msg_shown = 1;
    if (d->lock_channel != 0 && switch_channels[d->lock_channel] == 0) return nullptr;
    const uint16_t st = d->group == 0 ? door->state
                                      : (d->group < 104 ? uint16_t(DoorGroupsStates[d->group]) : uint16_t(0));
    if (st == kDoorOpen) return nullptr;
    return Txt_BindLabel(0x200000b, 0);
}

// [decomp] 0x00135fe0 Doors_Calc
// As portas de um grupo copiam o movimento da primeira do grupo que se mexeu neste quadro.
void Doors_Calc() {
    std::memset(DoorGroupsStates, 0, 0xca);
    for (auto* n = TheDoors.first; n != nullptr; n = n->next) {
        auto* d = reinterpret_cast<DOORINFO*>(n);
        if (d->group == 0) continue;
        obj_tag* lead = d->obj;
        for (auto* m = TheDoors.first; m != nullptr; m = m->next) {
            auto* e = reinterpret_cast<DOORINFO*>(m);
            if (e == d || e->group != d->group || (e->flags & 0x10) != 0) continue;
            e->dir = d->dir;
            e->pos = d->pos;
            e->flags |= 0x10;
            e->open_time = d->open_time;
            e->obj->state = lead->state;
            Door_Interp(e->obj, e, 0);
        }
    }
    for (auto* n = TheDoors.first; n != nullptr; n = n->next) {
        reinterpret_cast<DOORINFO*>(n)->flags &= 0xffef;
    }
}

// [decomp] 0x001363c8 Door_Update
// A cada quadro: obedece ao canal de switch da porta, destranca quando o canal de tranca liga
// (mostrando "destrancada", exceto em duas fases), fecha sozinha depois de 6 segundos aberta se não
// houver ninguém perto, anda no caminho e soma o estado no do grupo.
void Door_Update(obj_tag* door) {
    DOORINFO* d = info(door);
    if (d->cooldown != 0) --d->cooldown;
    if ((d->flags & 0x10) != 0) return;
    const uint16_t st = door->state;
    const uint16_t ch = d->channel;
    if ((st & (kDoorClosed | kDoorOpen)) != 0 && ch != 0) {
        if (switch_channels[ch] != 0 && (st & kDoorOpen) == 0) {
            Door_Activate(door, glb_players[0], 0);
        } else if (switch_channels[ch] == 0 && (st & kDoorClosed) == 0) {
            if (nobody_near(door)) Door_Activate(door, glb_players[0], 0);
            d->open_time = 0;
        }
    }
    const uint16_t lock = d->lock_channel;
    if (lock != 0 && switch_channels_prev[lock] == 0 && switch_channels[lock] != 0 && (d->flags & 0x10) == 0) {
        if (GameState.level_file != 0x7000014 && GameState.level_file != 0x700004a) {
            Text_AddMsg(0, 0, 1, Txt_BindLabel(0x2000003, 0), 0, 0xb4);
        }
        Door_Activate(door, glb_players[0], 0);
        d->lock_channel = 0;
        d->flags |= 0x10;
    }
    if ((door->state & kDoorOpen) != 0 && (d->flags & 0x20) == 0) {
        const uint16_t t = d->open_time;
        d->open_time = uint16_t(t + 1);
        if (FRAME_RATE_INT * 6 <= t) {
            if (nobody_near(door)) Door_Activate(door, glb_players[0], 0);
            d->open_time = 0;
        }
    }
    if ((door->state & (kDoorOpening | kDoorClosing)) == 0) {
        door->old_pos = door->pos;
    } else {
        Door_Interp(door, d, 1);
    }
    if (d->group != 0 && d->group < 104) {
        DoorGroupsStates[d->group] = int16_t(uint16_t(DoorGroupsStates[d->group]) | door->state);
    }
}
