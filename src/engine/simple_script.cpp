// Objetos de roteiro simples (módulo SS). Ver simple_script.h.
//
// Nenhuma divergência do original.

#include "engine/simple_script.h"

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/parsemap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/spline.h"

SimpleScriptStats g_simple_script_stats;

namespace {

SIMPLESCRIPT_tag* ss_of(obj_tag* obj) { return static_cast<SIMPLESCRIPT_tag*>(obj->data); }

// O objeto no quadro t do caminho e o passo seguinte (com um quadro só, sem FRAME_RATE_MUL).
void step(obj_tag* obj, SIMPLESCRIPT_tag* ss) {
    if (ss->spline == 0) {
        KeyFrame_Interp(ss->path, 0, ss->frames, ss->t, &obj->matrix);
    } else {
        Spline_Interp(ss->path, 0, ss->frames, ss->t, &obj->matrix, nullptr, SPLINE_INTERP(2));
    }
    obj->mode |= 0x20;
    if (ss->frames == 1) {
        ss->t = ss->t + ss->speed;
    } else {
        ss->t = ss->t + ss->speed * FRAME_RATE_MUL;
    }
}

}  // namespace

// [decomp] 0x001a9c58 SS_Create
// Sem caminho, nada. Um caminho sem duração recebe 0x4b (escrito no próprio posicionamento, como no original).
obj_tag* SS_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    if (level->path_count == 0) return nullptr;
    obj_tag* obj = control_create_object(0x2c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    SIMPLESCRIPT_tag* ss = ss_of(obj);
    if (level->path_unknown == 0) level->path_unknown = 0x4b;
    ss->t = 0.0f;
    ss->path = level->path;
    ss->frames = uint16_t(level->path_count - 1);
    ss->mode = uint16_t(level->params[0]);
    ss->start_mode = uint16_t(level->params[0]);
    ss->once = uint16_t(level->params[1]);
    ss->flags = uint16_t(level->params[2]);
    ss->channel = level->params[3];
    ss->sound = level->params[4] == 0 ? 0xffffu : level->params[4];
    ss->stop = float(level->params[5]);
    ss->spline = uint16_t(level->params[6]);
    ss->sound_handle = 0;
    ss->water = uint16_t(level->params[7]);
    ss->speed = float(ss->frames) / float(level->path_unknown);
    obj->type = 0x25;
    Control_SetGList(obj, glist);
    obj->mode |= 0x44;
    build_LinkToRoom(obj, 1, glb_world);
    // A parada cai num quadro que os passos alcançam.
    ss->stop = ss->speed * float(int32_t(ss->stop / ss->speed));
    ++g_simple_script_stats.created;
    return obj;
}

// [decomp] 0x001a9df8 SS_Operate
// Disparou? Um toque de uma classe pedida em flags (2 tiro, 8 o jogador, 0x10 um personagem) ou o canal ligado.
uint32_t SS_Operate(obj_tag* obj, SIMPLESCRIPT_tag* ss) {
    uint32_t touched = 0;
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        if (h->obj == nullptr) continue;
        switch (h->obj->type) {
            case 3: touched |= 8; break;
            case 2: touched |= 0x10; break;
            case 5: touched |= 2; break;
            default: break;
        }
    }
    if ((ss->flags & touched) != 0) return 1;
    return (ss->channel != 0 && switch_channels[ss->channel] != 0) ? 1 : 0;
}

// [decomp] 0x001a9ea8 SS_Activate
// Usar: com o bit 4 de flags, um objeto que não está parado (modo 0) anda e liga o canal.
void SS_Activate(obj_tag* obj, obj_tag*) {
    SIMPLESCRIPT_tag* ss = ss_of(obj);
    if (ss->mode == 0 || (ss->flags & 4) == 0) return;
    ss->mode = 1;
    if (ss->channel != 0) switch_channels[ss->channel] = 1;
}

// [decomp] 0x001a9948 SS_Update
void SS_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    SIMPLESCRIPT_tag* ss = ss_of(obj);
    const uint32_t fired = SS_Operate(obj, ss);
    if ((ss->flags & 1) == 0 && ss->channel != 0 && switch_channels[ss->channel] == 0) {
        switch_channels[ss->channel] = uint8_t(fired);
    }
    if (ss->moved != 0) obj->mode |= 0x20;
    if (ss->sound_handle != 0) {
        if (Sound_IsFinnished(ss->sound_handle) != 0) ss->sound_handle = 0;
    }
    switch (ss->mode) {
        case 8:
            step(obj, ss);
            if (float(ss->frames) < ss->t) ss->t = 0.0f;
            break;
        case 0x40:
            if (fired == 0) break;
            ss->sound_handle = Sound_Play3D(ss->sound, &obj->sphere, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            ss->mode = 1;
            ++g_simple_script_stats.triggered;
            [[fallthrough]];
        case 1:
            step(obj, ss);
            // A parada do meio do caminho: espera outro disparo dali.
            if (ss->stop < ss->t && std::fabs(ss->t - ss->stop) < 0.0002f) {
                ss->sound_handle = 0;
                ss->mode = 0x40;
            }
            if (int32_t(ss->frames) < int32_t(ss->t)) {
                ss->t = 0.0f;
                ss->mode = ss->once == 0 ? uint16_t(0x40) : uint16_t(0);
                ss->sound_handle = 0;
                ++g_simple_script_stats.finished;
            }
            ss->moved = 1;
            break;
        default:
            break;
    }
    if (ss->water != 0 && obj->cel != nullptr) obj->cel->water_y = obj->sphere.y;
}
