// Port das animações dos personagens da IA (DroneAnim_*, DroneData_*, DAnimFunc_*, DAStateFunc_*) e
// dos pequenos ajustes de script e de cabeça do módulo de animação que só elas usam
// (AnimScriptCallOnEvent/OnStop, AnimScriptSetAnimRotation, AnimObjectSetHeadTracking). Ver drone.h.
//
// Uma "DAnim" (índice de Drone_AnimInfo) escolhe o script pela subclasse do personagem
// (Drone_AnimTables), que DroneAnim_SetHTAnim põe para tocar (misturando com o anterior quando
// "blend" não é zero). Uma "chamada" (call_state, pedida pela IA) é traduzida em DAnim pelo estado
// de animação atual (DroneAnimStates -> DASC_*); quando a chamada termina, DroneAnim_CallFullyComplete
// manda o estado e a mensagem de fim (end_state, end_msg).
//
// Divergências intencionais: DroneAnim_OnStartAnim, DroneAnim_SetHTAnim e DAnimFunc_Run usam o script
// atual sem conferir se existe (o original leria o endereço 0 + campo); aqui pulam a escrita.

#include <cstring>

#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

namespace {

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

}  // namespace

// ---- scripts ----

// [decomp] 0x001c71f8 AnimScriptCallOnEvent
void AnimScriptCallOnEvent(ANIMSCRIPT_tag* s, void (*fn)(obj_tag*, void*, ANIMSCRIPT_tag*, uint16_t)) { s->event_fn = fn; }

// [decomp] 0x001c7200 AnimScriptCallOnStop
void AnimScriptCallOnStop(ANIMSCRIPT_tag* s, void (*fn)(obj_tag*, void*, ANIMSCRIPT_tag*)) { s->on_end = fn; }

// [decomp] 0x001c7208 AnimScriptSetAnimRotation
// Liga ou desliga "a raiz gira o objeto" (0x20000); ligar recomeça o ângulo de referência (8).
void AnimScriptSetAnimRotation(ANIMSCRIPT_tag* s, int8_t on) {
    if (on == 0) {
        s->flags &= 0xfffdffff;
        return;
    }
    s->turn[0] = 8.0f;
    s->flags |= 0x20000;
}

// [decomp] 0x001c6748 AnimObjectSetHeadTracking
// A cabeça passa a acompanhar target (nulo: para). delay sorteia um atraso de 1/10 a 1/10 + 1/4 de
// segundo; bone_set escolhe o osso (anim_flags 0x40).
void AnimObjectSetHeadTracking(obj_tag* obj, void* target, float dist, float speed, int8_t delay, int8_t bone_set,
                               uint8_t bone) {
    if (obj == nullptr) return;
    ANIMOBJ_tag* a = anim_of(obj);
    if (a == nullptr) return;
    a->head_speed = speed;
    int8_t wait = 0;
    a->head_dist_sq = dist * dist;
    if (target == nullptr) {
        a->anim_flags &= ~0x10u;
        a->head_target = nullptr;
    } else {
        if (delay != 0) {
            wait = static_cast<int8_t>(Rand_Rand(FRAME_RATE_INT >> 2));
            wait = static_cast<int8_t>(wait + static_cast<int8_t>(static_cast<int32_t>(FRAME_RATE_INT) / 10));
        }
        const uint32_t f = a->anim_flags;
        a->anim_flags = f | 0x10;
        if (bone_set == 0) {
            a->anim_flags = (f & ~0x40u) | 0x10;
            a->head_target = target;
        } else {
            a->anim_flags = f | 0x50;
            a->head_target = target;
            a->head_bone = bone;
        }
    }
    a->head_delay = wait;
}

// ---- DAnims ----

// [decomp] 0x0013ddd0 DroneAnim_SetAnimD
void DroneAnim_SetAnimD(uint16_t danim, uint16_t blend, Drone_tag* drone) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(drone->obj, &dcv);
    dcv.drone->end_state = 0;
    dcv.drone->end_msg = 0;
    if (DroneAnim_SetDAnimInternal(danim, blend, 0, dcv.drone) != 0) dcv.drone->anim_call_done = 1;
}

// [decomp] 0x0013aee8 DroneAnim_SetDAnimInternal
// Troca para a DAnim: copia Drone_AnimInfo, escolhe o script da subclasse (ou o da subclasse 0),
// encerra a chamada pendente se o estado de animação bate, e toca o script; DAnims sem script (ou
// "anda sem animar" em alguns tipos no chão) passam direto para a seguinte.
uint32_t DroneAnim_SetDAnimInternal(uint16_t danim, uint16_t blend, uint8_t call, Drone_tag* d) {
    if (bots_on() && d->blend_script != nullptr && NDrone2_DoAnimation(d) != 0) return 0;
    uint32_t sub = d->subclass;
    const DroneAnimTableEntry* row = Drone_AnimTables[danim];
    const DroneAnimInfo& info = Drone_AnimInfo[danim];
    const int16_t state = static_cast<int16_t>(info.anim_state);
    d->prev_anim_flags = d->anim_flags;
    d->danim = danim;
    if (static_cast<int16_t>(row[sub].script) == 0) sub = 0;
    d->anim_frame = row[sub].frame;
    d->end_danim = info.next != danim ? info.next : 0;
    d->anim_kind = info.kind;
    d->anim_state_kind = DroneAnimStates[state].kind;
    d->anim_script_hash = row[sub].script + 0x6000000u;
    d->anim_once = 0;
    d->anim_state = info.anim_state;
    d->anim_playing = 0;
    d->anim_flags = info.flags;
    d->move_speed = info.move_speed;
    d->anim_speed = info.speed;
    d->anim_info_10 = info.unknown_10;
    d->end_param = info.end_param;
    d->anim_info_f = info.unknown_0f;
    d->anim_call = call;
    // Chamadas 0x41-0x44, 0x50 e 0x6f contam como 0x40.
    const uint16_t cs = d->call_state;
    uint16_t v = cs;
    if (0x40 < cs && (cs <= 0x44 || cs == 0x50 || cs == 0x6f)) v = 0x40;
    d->call_state = v;
    if (d->anim_state == 0 || d->anim_state == v) {
        d->anim_call_done = 1;
        d->anim_param = d->call_param;
        DroneAnim_ZeroAnimCall(d);
    }
    bool skip;
    if (d->anim_script_hash == 0x6000000) {
        skip = true;
    } else {
        skip = false;
        if (d->obj->unknown_fc == 0 && (d->anim_flags & 4) != 0) {
            switch (d->anim_kind) {
                case 0: case 3: case 4: case 5: case 6: case 7: case 9: skip = true; break;
                default: break;
            }
        }
    }
    if (skip) {
        DroneAnim_SetEndAnim(d, 0x1e);
        return 0;
    }
    if (d->state_func != nullptr) d->state_func(d, 2);
    if (d->anim_func != nullptr) d->anim_func(d, 2);
    DroneAnim_SetHTAnim(d->anim_script_hash, d->anim_flags, d->anim_frame, d, blend, d->anim_speed);
    if (DroneDataFunc* f = DroneData_GetAnimFuncInfo(danim)) d->anim_func = f->fn;
    if (DroneDataFunc* f = DroneData_GetDAStateFuncInfo(d->anim_state)) d->state_func = f->fn;
    DroneAnim_OnStartAnim(d);
    return 1;
}

// [decomp] 0x0013b1c8 DroneAnim_SetHTAnim
// Toca o script. Com blend, o script atual vira o que sai (peso "blend"); se ele ainda não acabou e a
// animação anterior pedia giro (0x100), congela, fica com peso 30 e o giro que faltava vai para
// snap_rotate. frame 999 começa no último quadro, parado. Sem repetição (flags bit 0), avisa no fim.
void DroneAnim_SetHTAnim(uint32_t script, uint32_t flags, uint16_t frame, Drone_tag* d, uint16_t blend, float speed) {
    if (NDrone2_DoAnimation(d) == 0) blend = 0;
    if (blend == 0 || d->script == nullptr) {
        d->script = AnimScriptAdd(d->obj, script);
    } else {
        ANIMSCRIPT_tag* old = d->script;
        d->blend_script = old;
        old->owner = &d->blend_script;
        d->script = AnimScriptAppendBlend(d->obj, script);
        ANIMSCRIPT_tag* b = d->blend_script;
        if (b != nullptr) {
            b->weight = float(blend);
            b->weight_max = float(blend);
            AnimScriptCallOnStop(d->blend_script, nullptr);
            AnimScriptCallOnEvent(d->script, nullptr);
        }
        b = d->blend_script;
        if (b != nullptr && b->frame < float(b->frame_count) && (d->prev_anim_flags & 0x100) != 0) {
            obj_tag* obj = d->obj;
            b->speed = 0.0f;
            d->blend_script->weight = 30.0f;
            d->blend_script->weight_max = 30.0f;
            AnimScriptSetAnimRotation(d->blend_script, 1);
            _VECTOR pos, dir, rot;
            MATRIX m;
            AnimGetBoneWorldTrans(obj, 0x80000000, 0, &pos, &m);
            Mat_GetDir(&dir, &m);
            Vec_DirToRot(&dir, &rot);
            d->snap_rotate = rot.y - obj->rot.y;
            d->blend_script->flags |= 0x8000;
        }
    }
    ANIMSCRIPT_tag* s = d->script;
    if (s == nullptr) return;
    if (frame == 999) {
        s->speed = 0.0f;
        frame = static_cast<uint16_t>(s->frame_count - 1);
    } else if (speed != 1.0f) {
        s->speed = speed;
    }
    if (frame != 0) d->script->frame = float(frame);
    d->script->owner = &d->script;
    AnimScriptCallOnEvent(d->script, DroneAnim_EventFunc);
    d->script->flags |= 0x8000000;
    if ((flags & 1) == 0) {
        d->script->flags &= 0x7fffffff;
        d->anim_playing = 1;
        d->anim_once = 1;
        d->flags |= 0x8000;
        AnimScriptCallOnStop(d->script, DroneAnim_AnimEndCallback);
    } else {
        d->script->flags |= 0x80000000u;
        d->anim_once = 0;
        d->anim_playing = 0;
        d->flags &= 0xffff7fff;
    }
    d->anim_script_hash = script;
    d->state_func = nullptr;
    d->anim_start = GameState.frame;
    d->anim_func = nullptr;
}

// [decomp] 0x0013dc90 DroneAnim_ZeroAnimCall
void DroneAnim_ZeroAnimCall(Drone_tag* drone) { std::memset(&drone->call_state, 0, 0x10); }

// [decomp] 0x0013df38 DroneAnim_SetEndAnim
// Passa à DAnim seguinte (Drone_AnimInfo +0xa), se houver e for outra; sem sucesso, restaura.
uint32_t DroneAnim_SetEndAnim(Drone_tag* d, uint8_t param) {
    if (d->end_danim == 0) return 0;
    const uint16_t next = d->end_danim;
    const uint8_t saved = d->end_param;
    d->end_danim = 0;
    d->end_param = 0;
    if (d->danim == next) return 0;
    const uint8_t blend = DroneAnim_OnEndAnim(d, d->anim_flags, param);
    if (DroneAnim_SetDAnimInternal(next, blend, 0, d) != 0) return 1;
    d->end_param = saved;
    d->end_danim = next;
    return 0;
}

// [decomp] 0x0013e238 DroneAnim_OnStartAnim
void DroneAnim_OnStartAnim(Drone_tag* d) {
    if (d->anim_kind == 6 && d->heavy_hit == 0 && d->script != nullptr) {
        ANIMSCRIPT_tag* s = d->script;
        if (0xf < s->frame_count) s->frame_count = 0xf;
        d->end_param = 5;
    }
    if (d->state_func != nullptr) d->state_func(d, 0);
    if (d->anim_func != nullptr) d->anim_func(d, 0);
}

// [decomp] 0x0013e2c0 DroneAnim_OnEndAnim
// Avisa o fim às funções da animação; as flags 0x100/0x80/0x200 pedem um giro de 180/90/-90 graus
// (aplicado por DroneAnim_SnapRotate) e cortam a mistura.
uint8_t DroneAnim_OnEndAnim(Drone_tag* d, uint32_t flags, uint8_t param) {
    if (d->state_func != nullptr) d->state_func(d, 2);
    if (d->anim_func != nullptr) d->anim_func(d, 2);
    if ((flags & 0x100) != 0) {
        param = 0;
        d->snap_rotate = 3.1415927f;
    }
    if ((flags & 0x80) != 0) {
        param = 0;
        d->snap_rotate = 1.5707964f;
    }
    if ((flags & 0x200) != 0) {
        d->snap_rotate = -1.5707964f;
        param = 0;
    }
    return param;
}

// [decomp] 0x0013e198 DroneAnim_SnapRotate
// Aplica o giro pendente de uma vez (com a rotação pela raiz do script desligada durante a troca).
void DroneAnim_SnapRotate(Drone_tag* d) {
    if (d == nullptr) return;
    if (d->script != nullptr) AnimScriptSetAnimRotation(d->script, 0);
    if (d->snap_rotate == 0.0f) return;
    const float y = d->move_angles.y + d->snap_rotate;
    obj_tag* obj = d->obj;
    d->move_angles.y = y;
    obj->rot.y = y;
    Drone_GetOpponentInfo(d, obj);
    if (d->script != nullptr) AnimScriptSetAnimRotation(d->script, 1);
    d->snap_rotate = 0.0f;
}

// [decomp] 0x0013e230 DroneAnim_SnapRotate2
void DroneAnim_SnapRotate2(Drone_tag*) {}

// ---- chamadas ----

// [decomp] 0x0013add0 DroneAnim_CallHandler
// Por tick: termina a animação que acabou (ou a chamada concluída) e, se pode, traduz a chamada
// pendente numa DAnim.
void DroneAnim_CallHandler(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->anim_call != 0 && d->blend_script == nullptr) d->anim_call = 0;
    if (DroneAnim_CheckForAnimComplete(d) != 0) return;
    if (d->anim_call_done != 0 && d->anim_call == 0) {
        const uint8_t k = d->anim_kind;
        if (k == 2 || k == 9 || (k == 1 && d->anim_once == 0)) {
            DroneAnim_CallFullyComplete(d);
            d->anim_call_done = 0;
            return;
        }
    }
    if (DroneAnim_CanSetAnimCall(d) == 0) return;
    uint16_t danim = 0, blend = 8;
    int8_t call = 0;
    if (DroneAnim_GetDAnimForCall(d, d->call_state, d->call_variant, &danim, &blend, &call) == 0) return;
    DroneAnim_SetAnimCall(dcv, danim, blend, static_cast<uint8_t>(call));
}

// [decomp] 0x0013dbf8 DroneAnim_CanSetAnimCall
uint32_t DroneAnim_CanSetAnimCall(Drone_tag* d) {
    const uint16_t st = d->anim_state;
    if (d->call_state == 0 || (bots_on() && d->blend_script != nullptr) ||
        (st != d->call_from_state && d->anim_kind == 5)) {
        return 0;
    }
    if (st < 0xc) return 1;
    if (0xd < st && (0x2a < st || st < 0x29)) return 1;
    // Estados 0xc, 0xd, 0x29 e 0x2a: a chamada 10 não interrompe uma animação de tipo 5.
    if (d->call_state != 10) return 1;
    if (d->anim_kind != 5) return 1;
    return 0;
}

// [decomp] 0x0013dae8 DroneAnim_CheckForAnimComplete
uint32_t DroneAnim_CheckForAnimComplete(Drone_tag* d) {
    if (d->anim_once == 0 || d->anim_playing != 0) return 0;
    d->anim_once = 0;
    return DroneAnim_SetEndAnim(d, d->end_param) != 0 ? 1 : 0;
}

// [decomp] 0x0013de40 DroneAnim_GetDAnimForAnimState
// Procura a chamada (ou a entrada 0, "qualquer") na lista DASC_* do estado de animação: devolve a
// DAnim mais a variante (se a entrada tem tantas), -1 se não houver.
int16_t DroneAnim_GetDAnimForAnimState(uint16_t call, uint16_t anim_state, uint16_t variant, uint16_t* out4,
                                       int8_t* out5) {
    uint32_t st = anim_state;
    if (0x76 < st) st = 0;
    const uint8_t* p = DroneAnimStates[st].calls;
    if (p == nullptr || *p == 0xff) return -1;
    for (uint8_t b = *p;; b = *p) {
        if (b == call || b == 0) {
            int16_t s;
            std::memcpy(&s, p + 2, 2);
            const uint8_t variants = p[1];
            if (out4 != nullptr) *out4 = p[4];
            if (out5 != nullptr) *out5 = static_cast<int8_t>(p[5]);
            if (variants <= variant) variant = 0;
            if (s != 0 && s != -1) return static_cast<int16_t>(s + variant);
            return s;
        }
        p += 6;
        if (*p == 0xff) return -1;
    }
}

// [decomp] 0x0013db30 DroneAnim_GetDAnimForCall
// A DAnim da chamada no estado de animação atual; se ele não a conhece, tenta no estado 0 (com mistura
// 8).
uint32_t DroneAnim_GetDAnimForCall(Drone_tag* d, uint16_t call, uint16_t variant, uint16_t* danim, uint16_t* out4,
                                   int8_t* out5) {
    uint16_t r = static_cast<uint16_t>(DroneAnim_GetDAnimForAnimState(call, d->anim_state, variant, out4, out5));
    *danim = r;
    if (r == 0) return 0;
    if (r != 0xffff) return 1;
    if (out4 != nullptr) *out4 = 8;
    if (out5 != nullptr) *out5 = 0;
    r = static_cast<uint16_t>(DroneAnim_GetDAnimForAnimState(d->call_state, 0, d->call_variant, out4, out5));
    *danim = r;
    if (r == 0 || r == 0xffff) return 0;
    return 1;
}

// [decomp] 0x0013def8 DroneAnim_SetAnimCall
bool DroneAnim_SetAnimCall(DCVars_tag* dcv, uint16_t danim, uint16_t blend, uint8_t call) {
    Drone_tag* d = dcv->drone;
    d->anim_param = d->call_param;
    return DroneAnim_SetDAnimInternal(danim, blend, call, d) != 0;
}

// [decomp] 0x0013dcb8 DroneAnim_CallFullyComplete
void DroneAnim_CallFullyComplete(Drone_tag* d) {
    std::memset(&d->call_state, 0, 0x10);
    DroneAnim_SetEndAIState(d, static_cast<int16_t>(d->end_state), d->end_msg);
    d->end_msg = 0;
    d->end_state = 0;
}

// [decomp] 0x0013dd00 DroneAnim_SetEndAIState
void DroneAnim_SetEndAIState(Drone_tag* d, int16_t state, uint32_t msg) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(d->obj, &dcv);
    if (state != 0) Drone_SM_SetState(&d->sm, static_cast<uint32_t>(static_cast<int32_t>(state)), 0);
    if (msg != 0) Drone_SM_SendMsgSelf(msg, 0, 0, 0, &dcv);
}

// ---- callbacks do script ----

// [decomp] 0x0013b490 DroneAnim_PreTransCallback
// Ajusta o quanto a raiz anda no mundo: a altura segue a da animação, scripts de entrada acumulam o
// deslocamento, personagens parados não andam, e os limites (boundary_flags) cortam o movimento no
// espaço do objeto.
void DroneAnim_PreTransCallback(obj_tag* obj, void* anim_state, _VECTOR* delta) {
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    if (obj->type != 2) return;
    float rh;
    std::memcpy(&rh, static_cast<uint8_t*>(anim_state) + 0x5c, 4);  // ANIMOBJ_tag::root_height
    d->root_height = rh;
    d->prev_root_y = d->root_y;
    if (rh != 0.0f) {
        d->root_y = rh - 0.02f;
        if (rh - 0.02f < 0.2f) d->root_y = 0.2f;
        d->root_moved = 1;
    }
    if (d->scripted_move == 0) delta->y = d->root_y - d->prev_root_y;
    if (d->type == 0x1e) {
        const float m = BOT_getMovementSpeedMul(d);
        Vec_MulR32(delta, delta, m, 1.0f, m);
    }
    if (d->scripted_move != 0) {
        d->script_pos = {d->script_pos.x + delta->x, d->script_pos.y + delta->y, d->script_pos.z + delta->z,
                         d->script_pos.w + delta->w};
    }
    bool zero = d->no_move != 0 || d->zero_g != 0;
    if (d->touching_player != 0) {
        _VECTOR rot, local;
        MATRIX m, inv;
        Vec_DirToRot(&d->face_dir, &rot);
        RotMatrix(&rot, &m);
        Mat_Inverse(&m, &inv);
        ApplyMatrixLV(&inv, delta, &local);
        if (0.0f < local.z) {
            local.z = 0.0f;
            ApplyMatrixLVI(&m, &local);
            delta->x = local.x;
            delta->z = local.z;
        }
        zero = false;
    }
    if (zero) {
        Vec_Zero(delta);
    } else if ((d->flags & 1) != 0 && d->door == nullptr) {
        MATRIX inv;
        _VECTOR local;
        Mat_Inverse(&obj->matrix, &inv);
        ApplyMatrixLV(&inv, delta, &local);
        const uint32_t bf = d->boundary_flags;
        if ((bf & 2) != 0 && 0.0f < local.z) local.z = 0.0f;
        if ((bf & 4) != 0 && local.z < 0.0f) local.z = 0.0f;
        if ((bf & 8) != 0 && 0.0f < local.x) local.x = 0.0f;
        if ((bf & 0x10) != 0 && local.x < 0.0f) local.x = 0.0f;
        ApplyMatrixLV(&obj->matrix, &local, delta);
    }
}

// [decomp] 0x0013b750 DroneAnim_EventFunc
// Eventos dos scripts dos personagens: granada, troca de arma, a pistola presa ao osso 0x28 ou 100,
// golpes, avisos à máquina de estados, explosivo, e flags lidas pela IA.
void DroneAnim_EventFunc(obj_tag* obj, void*, ANIMSCRIPT_tag* script, uint16_t event) {
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(obj, &dcv);
    Drone_tag* d = dcv.drone;
    const bool alive_current = [&] { return 0.0f < d->health && script == d->script; }();
    switch (event) {
        case 1:
            if (alive_current) DroneWeap_ThrowGrenade(&dcv);
            break;
        case 2: DroneWeap_ChangeWeapon(&dcv, static_cast<int8_t>(d->weapon2)); break;
        case 3:
            AnimDatumSetEntity(obj, 0x28, 0x2000312, 0xff);
            AnimDatumSetEntity(obj, 100, 0, 0xff);
            break;
        case 4:
            AnimDatumSetEntity(obj, 0x28, 0, 0xff);
            AnimDatumSetEntity(obj, 100, 0x2000312, 0xff);
            break;
        case 5:
        case 10:
            if (alive_current) DroneWeap_CloseCombatImpact(&dcv, 1);
            break;
        case 6:
            if (alive_current) Drone_SM_SendMsg(0x20, 0, dcv.sm->id, dcv.sm->id);
            break;
        case 7: d->anim_playing = 0; break;
        case 9:
            if (alive_current) DroneWeap_PlaceTertiary(&dcv);
            break;
        case 0xb:
            if (alive_current) DroneWeap_CloseCombatImpact(&dcv, 2);
            break;
        case 0xe:
            if (d->unknown_2b0 != nullptr && alive_current) {
                Drone_SM_SendMsg(0xb, 10, dcv.sm->id, static_cast<Drone_tag*>(d->unknown_2b0->data)->sm.id);
            }
            break;
        case 0xf: d->aim_event = 1; break;
        case 0x10:
            d->unknown_21 = 0;
            d->aim_event = 0;
            break;
        case 0x12:
            d->unknown_21 = 1;
            d->aim_event = 1;
            break;
        default: break;
    }
}

// [decomp] 0x0013e3e8 DroneAnim_AnimEndCallback
void DroneAnim_AnimEndCallback(obj_tag* obj, void*, ANIMSCRIPT_tag*) {
    static_cast<Drone_tag*>(obj->data)->anim_playing = 0;
}

// ---- funções por DAnim e por estado de animação ----

namespace {
DroneDataFunc* find_func(DroneDataFunc* table, uint16_t id) {
    for (DroneDataFunc* p = table; p->id != 0xffff; ++p) {
        if (p->id == id) return p;
    }
    return nullptr;
}
}  // namespace

// [decomp] 0x0013e4d8 DroneData_GetAnimFuncInfo
DroneDataFunc* DroneData_GetAnimFuncInfo(uint16_t id) { return find_func(DroneData_AnimFunc, id); }

// [decomp] 0x0013e520 DroneData_GetDAStateFuncInfo
DroneDataFunc* DroneData_GetDAStateFuncInfo(uint16_t id) { return find_func(DroneData_DAStateFunc, id); }

// [decomp] 0x0013e488 DAnimFunc_Run
// Nas fases 07000009 e 0700000a a corrida (DAnim 0xdc) toca a 70%.
void DAnimFunc_Run(Drone_tag* d, uint16_t phase) {
    if (phase == 0 && GameState.level_file - 0x7000009u < 2 && d->script != nullptr) d->script->speed = 0.7f;
}

// [decomp] 0x0013e518 DAStateFunc_AimStand
void DAStateFunc_AimStand(Drone_tag*, uint16_t) {}
