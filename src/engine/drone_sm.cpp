// Port da máquina de estados dos personagens da IA (Drone_SM_*) e do despacho para as funções dos
// estados (NDrone2_ProcessStateMachine). Ver drone.h.
//
// Mensagens: msg (1 entrar, 2 sair, 3 tick, ...), estado exigido do destino (0 qualquer), quem manda,
// destino (id da máquina; 0 = todos), hora de envio, hora de entrega e um dado. Com hora futura vão
// para a lista ordenada de Drone_SM_LLItems e Drone_SM_SendDelayedMsgs as entrega. A função do estado
// devolve se tratou a mensagem; senão o estado 0 (NDrone2_DSTATE_Global) a recebe. Uma troca de
// estado (Drone_SM_SetState) acontece depois da mensagem: 2 ao estado que sai, 1 ao que entra.
//
// Divergências intencionais:
// - Drone_SM_SendMsg e a troca de estado montam mensagens na pilha deixando campos sem valor (o dado,
//   a hora); aqui ficam zerados.
// - Drone_SM_RouteMsg com a lista de atrasadas cheia escreveria no endereço 0; aqui a mensagem é
//   descartada (conta em g_drone_guards). Um destino que não é personagem (Drone_DCVfromOBJ falha)
//   usaria DCVars sem valor; aqui não recebe nada.
// - Funções de estado ainda não portadas (ponteiro nulo em NDrone2_StateFuncs) não tratam a
//   mensagem e contam em g_drone_state_pending.

#include <cstring>

#include "engine/drone.h"
#include "engine/npc.h"
#include "engine/drone_stubs.h"
#include "engine/gamestate.h"
#include "engine/resetmap.h"
#include "platform/pending.h"

DroneSMLists Drone_SM_LLDelayed;
DroneSMItem Drone_SM_LLItems[0x400];
uint32_t g_drone_state_pending[kDroneStateCount];

namespace {

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

Drone_tag* find_drone(uint32_t id) {
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (d->sm.id == id) return d;
    }
    return nullptr;
}

uint32_t call_handler(DCVars_tag* dcv, uint32_t state, MsgObject* msg) {
    DroneStateHandler h = dcv->sm->handler;
    return h != nullptr ? h(dcv, state, msg) : 0;
}

}  // namespace

// [decomp] 0x001757d0 Drone_SM_Init
uint32_t Drone_SM_Init() {
    Drone_SM_LLDelayed.count = 0x400;
    Drone_SM_LLItems[0].next = nullptr;
    Drone_SM_LLDelayed.free = &Drone_SM_LLItems[1];
    Drone_SM_LLDelayed.items = &Drone_SM_LLItems[0];
    Drone_SM_LLDelayed.pending = &Drone_SM_LLItems[0];
    Drone_SM_LLItems[1].next = nullptr;
    for (uint32_t i = 2; i < 0x400; ++i) {
        DroneSMItem* it = &Drone_SM_LLItems[i];
        it->next = Drone_SM_LLDelayed.free->next;
        Drone_SM_LLDelayed.free->next = it;
    }
    return 1;
}

// [decomp] 0x00175838 Drone_SM_InitObject
// Dá um id à máquina do personagem, começa no estado do objeto e manda "entrar" a si mesma.
bool Drone_SM_InitObject(obj_tag* obj) {
    if (obj == nullptr) return false;
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    const uint32_t id = ++NPCGlobals.sm_next_id;  // 16 bits, como no original
    d->sm.id = id;
    d->sm.change = 0;
    d->sm.change_param = 0;
    const uint32_t state = static_cast<uint32_t>(static_cast<int16_t>(obj->state));
    d->sm.handler = NDrone2_ProcessStateMachine;
    d->sm.next_state = state;
    d->sm.state = state;
    d->sm.return_state = state;
    d->sm.prev_state = state;
    Drone_SM_SendMsg(1, 0, id, id);
    return true;
}

// [decomp] 0x001758c0 Drone_SM_SetState
// Pede a troca de estado (feita por Drone_SM_RouteMsgDCV depois da mensagem em curso). Bots podem
// recusar ou trocar o estado pedido.
uint32_t Drone_SM_SetState(StateMachineInfo_tag* sm, uint32_t state, uint32_t param) {
    if (state == 0) return 1;
    Drone_tag* d = find_drone(sm->id);
    obj_tag* obj = d != nullptr ? d->obj : nullptr;
    if (obj != nullptr && static_cast<Drone_tag*>(obj->data)->type == 0x1e) {
        DCVars_tag dcv;
        if (Drone_DCVfromOBJ(obj, &dcv) != 0 && BOT_validateStateChange(&dcv, state) == 0) {
            const int16_t bot_state = *reinterpret_cast<int16_t*>(static_cast<uint8_t*>(dcv.drone->bot) + 0x762);
            if (bot_state == 0) return 0;
            state = static_cast<uint32_t>(static_cast<int32_t>(bot_state));
        }
    }
    sm->change_param = param;
    sm->next_state = state;
    sm->change = 1;
    return 1;
}

// [decomp] 0x0013a630 Drone_SM_SendMsg
void Drone_SM_SendMsg(uint32_t msg, uint32_t state, uint32_t sender, uint32_t dest) {
    if (Drone_bDisableSystem != 0) return;
    MsgObject m{msg, state, sender, dest, GameState.frame, GameState.frame, 0};
    Drone_SM_RouteMsg(&m);
}

// [decomp] 0x00175a38 Drone_SM_SendMsgSelf
void Drone_SM_SendMsgSelf(uint32_t msg, uint32_t data, uint32_t delay, uint32_t state, DCVars_tag* dcv) {
    const uint32_t id = dcv->sm->id;
    MsgObject m{msg, state, id, id, GameState.frame, GameState.frame + delay, data};
    if (delay == 0) {
        Drone_SM_RouteMsgDCV(dcv, &m);
    } else {
        Drone_SM_RouteMsg(&m);
    }
}

// [decomp] 0x00175aa8 Drone_SM_BroadcastMsg
void Drone_SM_BroadcastMsg(uint32_t msg, uint32_t data, uint32_t delay, uint32_t sender) {
    MsgObject m{msg, 0, sender, 0, GameState.frame, GameState.frame + delay, data};
    Drone_SM_RouteMsg(&m);
}

// [decomp] 0x001759a0 Drone_SM_SendDelayedMsgs
void Drone_SM_SendDelayedMsgs() {
    const uint32_t now = GameState.frame;
    DroneSMItem* head = Drone_SM_LLDelayed.pending;
    DroneSMItem* it = head->next;
    while (it != nullptr && it->msg.due <= now) {
        Drone_SM_RouteMsg(&it->msg);
        DroneSMItem* next = it->next;
        head->next = next;
        it->next = Drone_SM_LLDelayed.free->next;
        Drone_SM_LLDelayed.free->next = it;
        it = next;
    }
}

// [decomp] 0x00172128 Drone_SM_RouteMsg
// Mensagem futura: guarda na lista ordenada pela entrega. Senão entrega ao destino, ou a todos os
// personagens (destino 0), menos os que estão nos estados 1, 3, 0xf, 0x47 e 0x48 (0x49 também para um
// destino só).
void Drone_SM_RouteMsg(MsgObject* msg) {
    if (GameState.frame < msg->due) {
        DroneSMItem* it = Drone_SM_LLDelayed.free->next;
        if (it == nullptr) {
            ++g_drone_guards;  // port: o original escreveria no endereço 0
            return;
        }
        Drone_SM_LLDelayed.free->next = it->next;
        it->msg = *msg;
        DroneSMItem* prev = Drone_SM_LLDelayed.pending;
        DroneSMItem* p = prev->next;
        while (p != nullptr && p->msg.due <= it->msg.due) {
            prev = p;
            p = p->next;
        }
        prev->next = it;
        it->next = p;
        return;
    }
    if (msg->dest == 0) {
        for (LLNODE_tag* n = Drone_List()->first; n != nullptr;) {
            Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
            const uint32_t st = d->sm.state;
            if (st != 1 && st != 3 && st != 0xf && st != 0x47 && st != 0x48) {
                DCVars_tag dcv{d->obj, d, d->obj->cel, &d->sm};
                msg->dest = d->sm.id;
                Drone_SM_RouteMsgDCV(&dcv, msg);
            }
            n = n->next;
        }
        return;
    }
    if (!bots_on() || static_cast<int32_t>(msg->dest) >= 0) {
        Drone_tag* d = find_drone(msg->dest);
        if (d == nullptr || d->obj == nullptr) return;
        DCVars_tag dcv;
        if (Drone_DCVfromOBJ(d->obj, &dcv) == 0) return;
        const uint32_t st = dcv.sm->state;
        if (st == 0xf || st == 1 || (0x46 < st && st < 0x4a)) return;
        Drone_SM_RouteMsgDCV(&dcv, msg);
        return;
    }
    // Destino negativo com bots: os jogadores controlados pela IA do multijogador (não portado).
    pending_call("Drone_SM_RouteMsg (bots)");
}

// [decomp] 0x00171df8 Drone_SM_RouteMsgDCV
// Entrega ao estado atual (se a mensagem não exige outro) e, sem tratamento, ao estado 0; depois faz
// as trocas de estado pedidas: "sair" (2) ao estado velho, "entrar" (1) ao novo.
void Drone_SM_RouteMsgDCV(DCVars_tag* dcv, MsgObject* msg) {
    StateMachineInfo_tag* sm = dcv->sm;
    if (msg->state != 0 && msg->state != 0xc5 && msg->state != sm->state) return;
    Drone_tag* d = dcv->drone;
    bool bot_tick = false;
    if (d->type == 0x1e && msg->msg == 3) {
        call_handler(dcv, 0xc5, msg);
        bot_tick = true;
    }
    const uint32_t handled = call_handler(dcv, sm->state, msg) & 0xff;
    if (handled == 0 && !bot_tick) call_handler(dcv, dcv->drone->type == 0x1e ? 0xc5 : 0, msg);
    if (sm->change == 0) return;
    for (;;) {
        bool alarm = false, keep = false;
        sm->change = 0;
        const uint32_t next = sm->next_state;
        if (dcv->drone->type == 0x1e) {
            const uint32_t kind = BOTSTATE_getStateType(next);
            if (kind == 7 || kind == 10) {
                alarm = true;
            } else if (kind == 4 && next != 0xd3 && (next < 0xd3 || !(0xdc < next && next < 0xe0))) {
                keep = true;
            }
        }
        if (next == 0x44 || next == 0x46 || next == 0x47 || next == 0x55 || alarm) {
            dcv->drone->one_burst = 1;
        } else if (!keep) {
            dcv->drone->firing = 0;
        }
        MsgObject leave{2, 0, sm->id, sm->id, 0, 0, msg->data};
        call_handler(dcv, sm->state, &leave);
        sm->prev_state = sm->state;
        sm->state = sm->next_state;
        dcv->obj->state = static_cast<uint16_t>(sm->state);
        dcv->drone->flags &= 0xfffeffff;
        dcv->drone->flags &= 0xffffdfff;
        dcv->drone->hit_damage_mul = 1.0f;
        dcv->drone->timer1_on = 0;
        dcv->drone->timer2_on = 0;
        if (dcv->drone->type == 0x1e) {
            MsgObject changed{0x2e, sm->state, sm->id, sm->id, 0, 0, msg->data};
            call_handler(dcv, 0xc5, &changed);
        }
        sm->entered = GameState.frame;
        MsgObject enter{1, 0, sm->id, sm->id, 0, 0, msg->data};
        call_handler(dcv, sm->state, &enter);
        if (sm->change == 0) return;
    }
}

// [decomp] 0x00175778 NDrone2_ProcessStateMachine
uint32_t NDrone2_ProcessStateMachine(DCVars_tag* dcv, uint32_t state, MsgObject* msg) {
    if (state >= static_cast<uint32_t>(kDroneStateCount)) return 0;
    DroneStateFunc f = NDrone2_StateFuncs[state];
    if (f == nullptr) {
        ++g_drone_state_pending[state];
        pending_call("NDrone2_DSTATE_*");
        return 0;
    }
    return f(dcv, dcv->drone, dcv->obj, msg);
}
