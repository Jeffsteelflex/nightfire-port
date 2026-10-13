// Auxiliares dos personagens da IA usados pelos estados, pela visão, pelo movimento e pelo dano:
// pedidos de animação (DroneAnim_CallAnim), alerta, mensagens (também às redondezas de um objeto:
// Drone_MessageObjVicinity), cabeça e ossos, a falha da missão e quem cai do mapa (Drone_FellOutMap).
// Ver drone.h.
//
// Divergência intencional: Drone_Message com um objeto que não é personagem usaria um DCVars sem
// valor na pilha; aqui ele fica zerado (Drone_SM_SendMsgSelf leria o id da máquina de um ponteiro
// nulo, então a mensagem não é mandada).

#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"

// [decomp] 0x0013dfc8 DroneAnim_CanDoAnimState
// O personagem tem animação para o estado (a DAnim base dele mais a variante): na subclasse dele ou,
// senão, na subclasse 0.
bool DroneAnim_CanDoAnimState(DCVars_tag* dcv, int16_t call, int16_t variant) {
    if (call == 0) return true;
    if (0x76 < static_cast<uint16_t>(call)) return false;
    const uint32_t danim = (static_cast<int32_t>(variant) + DroneAnimStates[call].danim) & 0xffff;
    if (static_cast<int16_t>(Drone_AnimTables[danim][dcv->drone->subclass].script) != 0) return true;
    return static_cast<int16_t>(Drone_AnimTables[danim][0].script) != 0;
}

// [decomp] 0x0013e378 DroneAnim_CurrentMoveState
// O "estado de movimento" da DAnim atual (Drone_AnimInfo +0x10: 3/5 agachado, 4 deitado...).
uint8_t DroneAnim_CurrentMoveState(Drone_tag* drone) {
    if (drone != nullptr && drone->danim != 0) return Drone_AnimInfo[drone->danim].unknown_10;
    return 0;
}

// [decomp] 0x0013ab58 DroneAnim_CallAnim
// Pede uma animação pelo estado de animação "call" (com variante). Estados que a subclasse não tem
// viram um parecido que ela tenha (ou 0x24). Se já está nele, conclui a chamada na hora (com o estado
// e a mensagem de fim); senão guarda o pedido, que DroneAnim_CallHandler atende no tick.
uint32_t DroneAnim_CallAnim(float param, uint32_t call, uint32_t variant, int16_t end_state, uint32_t end_msg,
                            int8_t, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (call == 0x3d && variant == 3) {
        // Variante 3 da chamada 0x3d só para as classes 1 (e 5 a 7) e 0x10; as outras usam a 2.
        const uint16_t k = d->klass;
        if (k < 8) {
            if (k <= 4 && k != 1) variant = 2;
        } else if (k != 0x10) {
            variant = 2;
        }
    }
    if (call == 0 || !DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(call), 0)) {
        switch (call) {
            case 0xb: case 0xc: case 0xd: case 0x29: case 0x2a: case 0x3d: case 0x4a: case 0x4f: case 0x52:
            case 0x53: case 0x54: case 0x55: case 0x56: case 0x5d: case 0x5e: case 0x61:
                call = 10;
                break;
            default:
                NDrone2_ObjectIsArmed(d->obj);  // (o resultado não é usado)
                call = 0x21;
                break;
            case 0xf: case 0x36: call = 0x35; break;
            case 0x1f: case 0x62: call = 0x1e; break;
            case 0x22: case 0x60: call = 0x21; break;
            case 0x25: case 0x26: case 0x27: call = 0x24; break;
            case 0x4c: call = d->anim_state; break;
            case 0x57:
                if (DroneAnim_CanDoAnimState(dcv, 0x11, 0)) {
                    call = 0x11;
                } else {
                    call = DroneAnim_CanDoAnimState(dcv, 0x19, 0) ? 0x19 : 0x21;
                }
                break;
        }
        if (!DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(call), 0)) call = 0x24;
    }
    if (call == d->anim_state) {
        d->call_param = param;
        d->end_state = static_cast<uint16_t>(end_state);
        d->end_msg = end_msg;
        if (end_state == 0 && end_msg == 0) {
            DroneAnim_ZeroAnimCall(d);
        } else {
            DroneAnim_CallFullyComplete(d);
        }
    } else if (call != d->call_state || variant != d->call_variant ||
               end_state != static_cast<int16_t>(d->end_state) || end_msg != d->end_msg || param != d->call_param) {
        std::memset(&d->call_state, 0, 0x10);
        d->call_new = 1;
        d->call_variant = static_cast<uint16_t>(variant);
        d->call_from_state = d->anim_state;
        d->end_msg = end_msg;
        d->end_state = static_cast<uint16_t>(end_state);
        d->call_param = param;
        d->call_state = static_cast<uint16_t>(call);
        d->call_time = GameState.frame;
        d->call_state_b2 = DroneAnimStates[call].unknown_02;
        d->call_state_kind = DroneAnimStates[call].kind;
        d->anim_call_done = 0;
    }
    return 1;
}

// [decomp] 0x0013a910 Drone_AlertStatus
uint8_t* Drone_AlertStatus(obj_tag* obj) {
    if (obj == nullptr || obj->data == nullptr || obj->type != 2) return nullptr;
    return &static_cast<Drone_tag*>(obj->data)->alert_status;
}

// [decomp] 0x0013a8d8 Drone_AlertStatusSet
void Drone_AlertStatusSet(uint8_t status, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (status == d->alert_status) return;
    d->prev_alert_status = d->alert_status;
    d->alert_status = status;
    d->alert_status_changed = 1;
    d->alert_status_time = GameState.frame;
}

// [decomp] 0x0013a678 Drone_Message
// Mensagem a um personagem (a si mesmo, sem estado exigido) ou, sem objeto, a todos.
uint32_t Drone_Message(obj_tag* obj, uint32_t msg, uint32_t data, uint32_t delay) {
    if (Drone_bDisableSystem != 0) return 0;
    if (obj == nullptr) {
        Drone_SM_BroadcastMsg(msg, data, delay, 0);
        return 1;
    }
    DCVars_tag dcv{};
    if (Drone_DCVfromOBJ(obj, &dcv) == 0) return 1;  // port: o original usaria lixo da pilha
    Drone_SM_SendMsgSelf(msg, data, delay, 0, &dcv);
    return 1;
}

// [decomp] 0x0013a438 Drone_MessageObjVicinity
// Manda a mensagem a cada personagem (tipo 2 ou 0x11) a até `radius` metros de obj (Drone_Message). Com a mensagem
// 0x18 (a granada de clarão, Bullet_CollisionHandler) só a quem vê obj pelo osso 5 (NDrone2_CanSeeObject, máscara
// 0x500): os estados StunGrenade*. O original monta um DCVars na pilha só para ler o objeto; nada mais o usa.
void Drone_MessageObjVicinity(uint32_t msg, obj_tag* obj, float radius, uint32_t data, uint32_t delay) {
    if (Drone_bDisableSystem != 0) return;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        obj_tag* o = d->obj;
        if (o == nullptr || (o->type != 2 && o->type != 0x11)) continue;
        if (radius < Vec_Dist3D(&o->pos, &obj->pos)) continue;
        if (msg == 0x18 && NDrone2_CanSeeObject(d, obj, 5, 0x500) == 0) continue;
        Drone_Message(o, msg, data, delay);
    }
}

// [decomp] 0x0013a878 Drone_FellOutMap
// Um personagem saiu de todas as salas (control_handle_cel_change): fora do multijogador, desligado
// (NDrone2_Enable(0)) e marcado para remoção; com os bots, BOT_fellOutMap.
void Drone_FellOutMap(obj_tag* obj) {
    uint32_t bots;
    std::memcpy(&bots, MPSettings + 0x184, 4);
    if (bots != 0) {
        BOT_fellOutMap(obj);
        return;
    }
    if (obj->data != nullptr) NDrone2_Enable(0, static_cast<Drone_tag*>(obj->data));
    obj->life |= 1;
}

// [decomp] 0x00178768 NDrone2_ObjectIsArmed
// A arma na mão (ANIMOBJ_tag::weapon) é de fogo: 2 a 0x46 e 0x6a a 0x71.
uint32_t NDrone2_ObjectIsArmed(obj_tag* obj) {
    if (obj == nullptr || obj->sphere_src == nullptr) return 0;
    const uint8_t w = static_cast<uint8_t>(anim_of(obj)->weapon);
    if (static_cast<uint32_t>(w) - 0x6a < 8) return 1;
    const int8_t s = static_cast<int8_t>(w);
    if (1 < s && w != 0x47 && s < 0x48) return 1;
    return 0;
}

// [decomp] 0x0013e560 DroneFunc_SetMissionFailReason
// Liga o canal de falha da missão (0x60 morte de civil/aliado, 99 alarme...) com a mensagem; 0x10
// liga o canal do switch mais perto.
void DroneFunc_SetMissionFailReason(Drone_tag* drone, uint32_t reason) {
    switch (reason) {
        case 1: case 3: case 6: case 9:
            Mission_SetFailLabel(0x4000034);
            switch_channels[0x60] = 1;
            switch_channels_time[0x60] = GameState.frame;
            break;
        case 2: case 4: case 5: case 7: case 8: case 10: case 0xb: case 0xc:
            Mission_SetFailLabel(0x4000033);
            [[fallthrough]];
        default:
            switch_channels[99] = 1;
            switch_channels_time[99] = GameState.frame;
            break;
        case 0x10: {
            obj_tag* sw = Switch_GetNearest(&drone->obj->pos);
            const uint16_t ch = Switch_GetSwitchChannel(sw);
            if (ch != 0) {
                if (ch == 0x61) Mission_SetFailLabel(0x4000032);
                switch_channels[ch] = 1;
                switch_channels_time[ch] = GameState.frame;
            }
            break;
        }
    }
}

namespace {

// Olhos de quem não é personagem: a posição mais 0,6 (0,2 agachado, nada deitado).
uint32_t eyes_from_pos(obj_tag* obj, _VECTOR* pos) {
    *pos = obj->pos;
    const uint8_t move = obj->type == 2 ? DroneAnim_CurrentMoveState(static_cast<Drone_tag*>(obj->data)) : 0;
    if (move == 4 || move == 0x1a) return 0;
    pos->y = pos->y + ((move == 3 || move == 5) ? 0.2f : 0.6f);
    return 0;
}

}  // namespace

// [decomp] 0x00149aa8 NDrone2_GetHeadPos
// Personagem: os olhos guardados (Drone_tag::head_pos) e o giro da cabeça. Outros: a posição mais a
// altura dos olhos.
uint32_t NDrone2_GetHeadPos(obj_tag* obj, _VECTOR* pos, float* yaw) {
    if (obj->type == 2) {
        Drone_tag* d = static_cast<Drone_tag*>(obj->data);
        if (pos != nullptr) *pos = d->head_pos;
        if (yaw != nullptr) *yaw = d->head_yaw;
        return 1;
    }
    return eyes_from_pos(obj, pos);
}

// [decomp] 0x00141150 NDrone2_GetHeadPosOld
// O osso da cabeça (5) de um personagem animado neste tick, fora de 0700001b; senão como acima.
uint32_t NDrone2_GetHeadPosOld(obj_tag* obj, _VECTOR* pos, float* yaw) {
    if (GameState.level_file != 0x700001b && obj != nullptr && obj->sphere_src != nullptr && obj->type == 2 &&
        (static_cast<Drone_tag*>(obj->data)->flags & 0x100000) != 0 && (anim_of(obj)->anim_flags & 1) != 0) {
        MATRIX m;
        AnimGetBoneWorldTrans(obj, 0x80000005, 0, pos, yaw != nullptr ? &m : nullptr);
        if (yaw != nullptr) *yaw = obj->rot.y;
        return 1;
    }
    if (yaw != nullptr) *yaw = obj->rot.y;
    if (pos == nullptr) return 0;
    return eyes_from_pos(obj, pos);
}

// [decomp] 0x00149798 Drone_IsTalking
uint32_t Drone_IsTalking(Drone_tag* drone) {
    if (drone->sound == 0) return 0;
    if (Sound_IsFinnished(drone->sound) == 0) return 1;
    drone->sound = 0;
    return 0;
}

// [decomp] 0x00149bd0 DroneFunc_GetBonePos
// Posição no mundo de um osso de um personagem ou do jogador animado (bit 1 das flags da animação);
// senão a posição do objeto (e devolve 0).
uint32_t DroneFunc_GetBonePos(obj_tag* obj, uint32_t bone, _VECTOR* out) {
    if (obj == nullptr) return 0;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim != nullptr && static_cast<uint32_t>(obj->type) - 2 < 2) {
        bool ok = true;
        if (obj->type == 2) {
            ok = (static_cast<Drone_tag*>(obj->data)->flags & 0x100000) != 0 && GameState.level_file != 0x700001b;
        }
        if (ok && (anim->anim_flags & 1) != 0) {
            AnimGetBoneWorldTrans(obj, bone | 0x80000000, 0, out, nullptr);
            return 1;
        }
    }
    if (out != nullptr) *out = obj->pos;
    return 0;
}
