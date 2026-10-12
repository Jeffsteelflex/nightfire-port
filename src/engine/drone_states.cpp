// Port das funções de estado dos personagens da IA (NDrone2_DSTATE_*, chamadas por
// NDrone2_ProcessStateMachine pela tabela NDrone2_StateFuncs). Ver drone.h.
//
// Cada uma recebe (DCVars, Drone_tag, obj, mensagem) e devolve se tratou a mensagem: 1 entrar,
// 2 sair, 3 tick, 4 temporizador (msg_time), 0xc/0xd temporizadores 1/2; os estados de "paz" também
// tratam impactos (6-9, 0x17-0x19), alertas (0x11, 0x12, 0x14-0x16, 0x1e), ataque forçado (0x1f) e
// explosivos (0x21). Portados aqui: os estados de partida, parado, patrulha, civis, festa, aliado que
// guia (início), correr até um ponto e os de morte. O combate (0x56 em diante) ainda não.

#include <cmath>

#include "engine/ainetwork.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/npc.h"
#include "engine/vecmath.h"
#include "engine/view.h"

namespace {

uint32_t set_state(DCVars_tag* dcv, uint32_t state) { return Drone_SM_SetState(dcv->sm, state, 0); }

uint32_t initial(Drone_tag* d) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d->initial_state))); }

bool prop(Drone_tag* d, int32_t p) { return behaviour_util_getProperty(p, d->behaviour) != 0; }

// Mensagens que os estados de paz repartem. Impactos vão para DroneFunc_HandleImpact com o estado de
// reação (6 e 8 sem a flag, os outros com); alertas para DroneVision_EnemyAlerts (troca de estado se
// pedir); 0x1f ataque forçado; 0x21 explosivo (data = o objeto). Devolve false se não é uma delas.
struct Shared {
    uint32_t impact_state;
    uint32_t explosive_state;
    bool alerts = true;
    bool forced = true;
};

bool shared_messages(DCVars_tag* dcv, MsgObject* msg, const Shared& s, uint32_t* ret) {
    switch (msg->msg) {
        case 6:
        case 8:
            DroneFunc_HandleImpact(dcv, msg, s.impact_state, 0);
            *ret = 1;
            return true;
        case 7: case 9: case 0x17: case 0x18: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, s.impact_state, 1);
            *ret = 1;
            return true;
        case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x1e:
            if (!s.alerts) return false;
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            *ret = dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
            return true;
        case 0x1f:
            if (!s.forced) return false;
            DroneFunc_DoForcedAttack(dcv);
            *ret = 1;
            return true;
        case 0x21:
            DroneFunc_ConsiderExplosive(dcv, nf::p32_ptr<obj_tag>(msg->data),
                                        static_cast<int16_t>(s.explosive_state));
            *ret = 1;
            return true;
        default:
            return false;
    }
}

// Civil que viu alguém armado: fica com medo (0x8000000); com a propriedade 0x33 avisa todos uma vez
// (mensagem 0x11 sobre o jogador). A classe 0x10 vira inimiga e ataca.
bool civilian_sees_armed(DCVars_tag* dcv, uint32_t* ret) {
    Drone_tag* d = dcv->drone;
    d->flags |= 0x8000000;
    if (prop(d, 0x33) && (d->flags & 0x200000) == 0) {
        d->flags2 |= 1;
        NDrone2_DroneAlertToObject(dcv, 0x11, NDrone2_Player(), 20.0f, 20.0f, 0.76f);
        d->flags |= 0x200000;
    }
    if (d->klass == 0x10) {
        d->side = 1;
        *ret = set_state(dcv, 0x56);
        return true;
    }
    return false;
}

void set_timer1(Drone_tag* d, uint32_t at) {
    d->timer1_time = at;
    d->timer1_data = 0;
    d->timer1_on = 1;
}

void remove_now(DCVars_tag* dcv) {
    NDrone2_Enable(0, dcv->drone);
    dcv->obj->life |= 1;
}

}  // namespace

// [decomp] 0x0015b830 NDrone2_DSTATE_Global
// O estado 0 recebe o que os outros não tratam. Ao entrar, leva ao estado de partida: bots 0xc3;
// esperando o canal a (parâmetro 3) desligado, 1; o tipo 0x17, 0x32; com script de entrada, 3 (tocar
// script); senão o estado inicial do tipo. A mensagem 10 põe o tipo 4 ativo e vivo no estado 9; a
// 0x1d leva ao estado do dado.
uint32_t NDrone2_DSTATE_Global(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            Drone_tag* d = dcv->drone;
            if (d->type == 0x1e) return set_state(dcv, 0xc3);
            if (d->channel_a != 0 && d->channel_a_state == 0) return set_state(dcv, 1);
            if (drone->type == 0x17) return set_state(dcv, 0x32);
            if (d->entry_script != 0) return set_state(dcv, 3);
            set_state(dcv, initial(d));
            return 1;
        }
        case 0:
            return 1;
        case 10: {
            Drone_tag* d = dcv->drone;
            if ((d->flags & 0x100) != 0 && d->type == 4 && 0.0f < d->health) return set_state(dcv, 9);
            return 1;
        }
        case 0x1d:
            if (msg->data != 0) return set_state(dcv, msg->data);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0015b960 NDrone2_DSTATE_WaitSwitch
// Desligado até o canal a ligar (ou a mensagem 0xe); então vai ao estado de partida.
uint32_t NDrone2_DSTATE_WaitSwitch(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 1:
            NDrone2_Enable(0, d);
            return 1;
        case 0:
            return 1;
        case 3:
            if (switch_channels[d->channel_a] == 0) return 1;
            NDrone2_Enable(1, d);
            if (d->type == 0x17) return set_state(dcv, 0x32);
            if (d->entry_script == 0) return set_state(dcv, initial(d));
            return set_state(dcv, 3);
        case 0xe:
            NDrone2_Enable(1, d);
            if (d->entry_script == 0) {
                set_state(dcv, initial(d));
                return 1;
            }
            return set_state(dcv, 3);
        default:
            return 0;
    }
}

// [decomp] 0x00172440 NDrone2_DSTATE_Disabled
// Espera ficar ativo (0x100); então vai ao estado guardado em state_a (ou ao canal, ao 0x32, ao
// script).
uint32_t NDrone2_DSTATE_Disabled(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 0:
        case 1:
            return 1;
        case 3:
            if ((d->flags & 0x100) == 0) return 1;
            if (d->channel_a != 0 && d->channel_a_state == 0) return set_state(dcv, 1);
            if (drone->type == 0x17) return set_state(dcv, 0x32);
            if (d->entry_script == 0) {
                set_state(dcv, drone->state_a);
                return 1;
            }
            return set_state(dcv, 3);
        default:
            return 0;
    }
}

// [decomp] 0x0015bf18 NDrone2_DSTATE_Idle
// Parado olhando em volta: com rota de missão vai a ela (0x1b); em alerta passa a 5; procura o
// oponente a cada tick; de tempos em tempos (propriedade 0x46) uma animação de espera.
uint32_t NDrone2_DSTATE_Idle(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (GameState.level_file == 0x700001b && d->klass == 0x14) return set_state(dcv, 0xc2);
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            if (prop(d, 0x1d)) NDrone2_AssignAIPath(dcv, 8);
            if (d->route2.count != 0) return set_state(dcv, 0x1b);
            behaviour_util_setProperty(0x1d, d->behaviour, 0);
            if (d->alert < 0.66f) {
                DroneAnim_SetStandIdleAnim(dcv, 0);
                if (prop(d, 0x46)) NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
                return 1;
            }
            return set_state(dcv, 5);
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result == 0) {
                DroneAnim_SetStandIdleAnim(dcv, 1);
                return 1;
            }
            return set_state(dcv, dcv->sm->result);
        case 4:
            if (prop(d, 0x46)) {
                if (0.66f <= d->alert) {
                    DroneAnim_CallAnim(0.0f, 0x22, 0, 0, 0, 0, dcv);
                } else {
                    DroneAnim_CallAnim(0.0f, 0x4b, Rand_Rand(3), 0, 0, 0, dcv);
                }
                NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
            }
            return 1;
        case 5:
            if (d->route2.count != 0) return set_state(dcv, 0x1b);
            if (0.66f <= d->alert) {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            DroneAnim_SetStandIdleAnim(dcv, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0015c248 NDrone2_DSTATE_Alert
// Parado em alerta (arma em punho, animação 0x21).
uint32_t NDrone2_DSTATE_Alert(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            if (prop(d, 0x1d)) NDrone2_AssignAIPath(dcv, 8);
            if (d->route2.count != 0) return set_state(dcv, 0x1b);
            behaviour_util_setProperty(0x1d, d->behaviour, 0);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            if (prop(d, 0x46)) NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
            return 1;
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 4:
            if (prop(d, 0x46)) {
                DroneAnim_CallAnim(0.0f, 0x22, 0, 0, 0, 0, dcv);
                NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
            }
            return 1;
        case 5:
            if (d->route2.count == 0) {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            return set_state(dcv, 0x1b);
        default:
            return 0;
    }
}

// [decomp] 0x001724f0 NDrone2_DSTATE_InitPatrol
// Com patrulha (propriedades 0x24/0x25) e um caminho de patrulha sob os pés, patrulha (7); senão
// fica parado (4).
uint32_t NDrone2_DSTATE_InitPatrol(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (!prop(d, 0x24) && !prop(d, 0x25)) return set_state(dcv, 4);
    NDrone2_AssignAIPath(dcv, 4);
    if (d->route2.count == 0) {
        set_state(dcv, 4);
        return 1;
    }
    return set_state(dcv, 7);
}

// [decomp] 0x0015c498 NDrone2_DSTATE_Patrol
// Anda pela patrulha (route2), fala de vez em quando e procura o oponente a cada tick.
uint32_t NDrone2_DSTATE_Patrol(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            if (!prop(d, 0x25) || d->alert < 0.66f) {
                if (prop(d, 0x46)) NDrone2_SetIdleTimeOut(dcv, 0x2d, 10);
            } else if (prop(d, 0x46)) {
                NDrone2_SetIdleTimeOut(dcv, 5, 10);
            }
            return 1;
        case 3:
            NDrone2_PatrolTalk(dcv);
            NDrone2_UpdatePatrolRoute(dcv);
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 4:
            if (prop(d, 0x46)) {
                if (prop(d, 0x25) && 0.66f <= d->alert) {
                    DroneAnim_CallAnim(0.0f, 0x36, 0, 0, 0, 0, dcv);
                    return 1;
                }
                DroneAnim_CallAnim(0.0f, 0x4b, Rand_Rand(3), 7, 0, 0, dcv);
            }
            return 1;
        case 0x1b:
            set_state(dcv, NDrone2_ObstructedPatrolPath(dcv));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172730 NDrone2_DSTATE_CivilianInit
// Civis: com rota de missão (propriedade 0x1d) a seguem (0x16); com patrulha patrulham (0x14); senão
// ficam parados (0x11). A classe 0xd vai para a festa (0x2f).
uint32_t NDrone2_DSTATE_CivilianInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (d->klass == 0xd) return set_state(dcv, 0x2f);
    if (prop(d, 0x1d)) {
        NDrone2_AssignAIPath(dcv, 8);
        if (d->route2.count != 0) return set_state(dcv, 0x16);
    }
    behaviour_util_setProperty(0x1d, d->behaviour, 0);
    NDrone2_AssignAIPath(dcv, 4);
    if (d->route2.count == 0) {
        set_state(dcv, 0x11);
        return 1;
    }
    return set_state(dcv, 0x14);
}

// [decomp] 0x0015d118 NDrone2_DSTATE_Civilian
// Civil parado; se vê alguém armado (state_radius) fica com medo (0x12).
uint32_t NDrone2_DSTATE_Civilian(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x12, 0x12}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (d->klass == 0xd) return set_state(dcv, 0x2f);
            if (d->klass == 0xf) return set_state(dcv, 0x34);
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            DroneAnim_SetStandIdleAnim(dcv, 0);
            d->state_radius = GameState.level_file == 0x7000007 ? 10.0f : 20.0f;
            d->state_a = 0xff;
            d->state_b = 0;
            return 1;
        case 3:
            if (NDrone2_NearArmedPerson(dcv, d->state_radius) == 0) {
                NDrone2_BondTalk(dcv);
                return 1;
            }
            if (civilian_sees_armed(dcv, &ret)) return ret;
            return set_state(dcv, 0x12);
        default:
            return 0;
    }
}

// [decomp] 0x0015d610 NDrone2_DSTATE_CivilianPatrol
// Civil patrulhando; se vê alguém armado foge (0x56).
uint32_t NDrone2_DSTATE_CivilianPatrol(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
        case 2:
            return 1;
        case 1:
            if (d->route2.count == 0) return set_state(dcv, 4);
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x37, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_UpdatePatrolRoute(dcv);
            if (NDrone2_NearArmedPerson(dcv, 6.0f) == 0) return 1;
            if (civilian_sees_armed(dcv, &ret)) return ret;
            if (d->unknown_15 != 0) NDrone2_PlaySFX(dcv, 0x48, nullptr, 1, 1.0f);
            return set_state(dcv, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x00172810 NDrone2_DSTATE_ReturnToPatrolPath
uint32_t NDrone2_DSTATE_ReturnToPatrolPath(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    NDrone2_ReFindMissionPath(dcv);
    dcv->drone->initial_state = 0x14;
    set_state(dcv, 99);
    return 1;
}

// [decomp] 0x0015d860 NDrone2_DSTATE_CivilianMission
// Civil seguindo a rota de missão; no fim fica parado (0x11) ou passa ao ataque; com medo, 0x12.
uint32_t NDrone2_DSTATE_CivilianMission(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (d->klass == 0x13) return set_state(dcv, 0x19);
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x37, 0, 0, 0, 0, dcv);
            d->state_a = 0xff;
            d->state_b = 0;
            return 1;
        case 3:
            if (NDrone2_NearArmedPerson(dcv, 10.0f) == 0) {
                NDrone2_BondTalk(dcv);
                if (NDrone2_UpdateMissionRoute(dcv) != 3) {
                    const uint8_t st = d->route2.status;
                    if (st != 9 && st != 0xc) return 1;
                    if (0.66f <= d->alert) {
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    DroneAnim_SetStandIdleAnim(dcv, 0);
                    return 1;
                }
                behaviour_util_setProperty(0x1d, d->behaviour, 0);
                if (GameState.level_file != 0x7000012 && d->attack_state != 0 && d->channel_b == 0) {
                    return set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(NDrone2_ChangeToAttackMode(d))));
                }
                return set_state(dcv, 0x11);
            }
            if (civilian_sees_armed(dcv, &ret)) return ret;
            return set_state(dcv, 0x12);
        case 0x22:
            if (d->klass != 0x13) return set_state(dcv, 0x17);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0015dbb0 NDrone2_DSTATE_CivilianMissionWait
// Civil esperando o jogador no meio da missão: o temporizador 1 confere a cada 2 s se ele está perto
// (3 m) e à frente; senão volta à missão (0x16).
uint32_t NDrone2_DSTATE_CivilianMissionWait(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x56, 0x56}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            d->flags |= 0x10000;
            d->flags |= 0x2000;
            if (d->alert < 0.66f) {
                DroneAnim_SetStandIdleAnim(dcv, 0);
            } else {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            }
            set_timer1(d, GameState.frame + FRAME_RATE_INT * 2);
            d->state_a = 0xff;
            d->state_b = 0;
            return 1;
        case 3:
            if (NDrone2_NearArmedPerson(dcv, 10.0f) == 0) {
                NDrone2_BondTalk(dcv);
                return 1;
            }
            if (civilian_sees_armed(dcv, &ret)) return ret;
            return set_state(dcv, 0x12);
        case 0xc:
            if (NDrone2_BondWithinAngle(dcv, 0.5235988f) != 0) {
                obj_tag* player = NDrone2_Player();
                if (player != nullptr && Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 3.0f) {
                    set_timer1(d, GameState.frame + FRAME_RATE_INT * 2);
                    return 1;
                }
            }
            return set_state(dcv, 0x16);
        case 0x22:
            set_timer1(d, GameState.frame + FRAME_RATE_INT * 2);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0015e4f0 NDrone2_DSTATE_AllyLeadInit
// Aliado que guia o jogador: com rota de missão espera ele chegar a 3 m e parte (0x20).
uint32_t NDrone2_DSTATE_AllyLeadInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0, 0, false, false}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (prop(d, 0x1d)) NDrone2_AssignAIPath(dcv, 8);
            d->flags |= 0x10000;
            if (d->route2.count == 0) {
                DroneAnim_SetStandIdleAnim(dcv, 0);
                behaviour_util_setProperty(0x1d, d->behaviour, 0);
                return 1;
            }
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 3: {
            if (NDrone2_Player() == nullptr) return 1;
            if (Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 3.0f) {
                if (d->route2.count == 0) return 1;
                return set_state(dcv, 0x20);
            }
            return 1;
        }
        default:
            return 0;
    }
}

// [decomp] 0x001728c0 NDrone2_DSTATE_PartyGirlInit
uint32_t NDrone2_DSTATE_PartyGirlInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (prop(d, 0x1d)) {
        NDrone2_AssignAIPath(dcv, 8);
        if (d->route2.count != 0) return set_state(dcv, 0x16);
    }
    behaviour_util_setProperty(0x1d, d->behaviour, 0);
    set_state(dcv, 0x2f);
    return 1;
}

// [decomp] 0x00160140 NDrone2_DSTATE_PartyGirl
// Convidada da festa: patrulha se tiver caminho (0x14); com alguém armado por perto fica com medo
// (0x12, e a propriedade 0x52 falha a missão).
uint32_t NDrone2_DSTATE_PartyGirl(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    uint32_t ret;
    if (shared_messages(dcv, msg, {0x12, 0x12}, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (d->klass == 0x12) return set_state(dcv, 0x11);
            NDrone2_AssignAIPath(dcv, 4);
            if (d->route2.count == 0) {
                d->flags |= 0x10000;
                d->flags |= 0x2000;
                d->state_a = 0xff;
                d->state_b = 0;
                return 1;
            }
            return set_state(dcv, 0x14);
        case 3:
            if (NDrone2_NearArmedPerson(dcv, 10.0f) == 0) {
                DroneAnim_SetStandIdleAnim(dcv, 1);
                NDrone2_BondTalk(dcv);
                return 1;
            }
            d->flags |= 0x8000000;
            if (prop(d, 0x33) && (d->flags & 0x200000) == 0) {
                d->flags2 |= 1;
                NDrone2_DroneAlertToObject(dcv, 0x11, NDrone2_Player(), 20.0f, 20.0f, 0.76f);
                d->flags |= 0x200000;
            }
            if (prop(d, 0x52)) DroneFunc_SetMissionFailReason(drone, 0xb);
            return set_state(dcv, 0x12);
        default:
            return 0;
    }
}

// [decomp] 0x00168910 NDrone2_DSTATE_EnemyRunToPoint
// Corre até o ponto de IA do seu lado (ou ao alarme, 0x89) e depois ataca ou espera; sem ponto vai
// direto ao estado de ataque.
uint32_t NDrone2_DSTATE_EnemyRunToPoint(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 1: {
            d->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            if (prop(d, 0x56)) return set_state(dcv, 0x89);
            d->initial_state = 0x56;
            const uint32_t level = GameState.level_file;
            if ((level == 0x7000041 || level == 0x7000011 || level == 0x7000012 || level == 0x7000013) &&
                (d->flags & 0x4000000) == 0) {
                if (d->alert < 0.66f) {
                    d->initial_state = 4;
                } else if (d->alert < 1.0f) {
                    d->initial_state = 5;
                }
            }
            if (NDrone2_FindRunToPoint(dcv) == 0) {
                set_state(dcv, initial(d));
                return 1;
            }
            d->return_state = d->initial_state;
            return set_state(dcv, d->side == 1 ? 99 : 100);
        }
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001637f8 NDrone2_DSTATE_Death_Anim
// Morrendo: falhas, fala, animação pela posição do tiro (termina em 0x47, morto), larga a arma; o
// canal de morte (parâmetro 12) liga ao sair ou no temporizador.
uint32_t NDrone2_DSTATE_Death_Anim(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1: {
            obj->flags |= 0x20;
            if (GameState.level_file == 0x700004a) d->flags |= 4;
            const uint8_t t = d->type;
            if (t < 0x1b) {
                d->state_a = 0;
            } else if (t < 0x1d) {
                return set_state(dcv, 0x92);
            } else if (t == 0x1d) {
                return set_state(dcv, 0xbf);
            } else {
                d->state_a = 0;
            }
            Drone_AlertStatusSet(3, dcv);
            if (d->enter_script != 0) return set_state(dcv, 0x46);
            DroneFunc_OnInitDeath(dcv);
            if (GameState.level_file == 0x7000002) {
                set_timer1(d, static_cast<uint32_t>(float(GameState.frame) + FRAME_RATE_DIV * 90.0f));
            }
            drone->state_a = 1;
            DroneAnim_LocationDeathAnim(dcv, 0x47);
            return 1;
        }
        case 2:
            if (d->state_a != 0) DroneFunc_SetDeathChannel(drone);
            return 1;
        case 3:
            if (drone->firing == 0 || drone->subclass == 4 || drone->subclass == 9) DroneWeap_DropWeapon(dcv);
            if (switch_channels[0x35] != 0) {
                const uint8_t p = drone->param12;
                if (p != 0x88 && p != 0x29 && p != 0x2a) return 1;
                if (drone->state_a != 0) {
                    DroneFunc_SetDeathChannel(drone);
                    drone->state_a = 0;
                }
                remove_now(dcv);
            }
            return 1;
        case 0xc:
            if (drone->state_a != 0) {
                DroneFunc_SetDeathChannel(drone);
                drone->state_a = 0;
            }
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163a80 NDrone2_DSTATE_DeathByExplosion
// Como Death_Anim, com a animação pelo lado da explosão (o impacto vem no parâmetro da troca).
uint32_t NDrone2_DSTATE_DeathByExplosion(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            obj->flags |= 0x20;
            Drone_AlertStatusSet(3, dcv);
            if (d->enter_script != 0) return set_state(dcv, 0x46);
            DroneFunc_OnInitDeath(dcv);
            if (GameState.level_file == 0x7000002) {
                set_timer1(d, static_cast<uint32_t>(float(GameState.frame) + FRAME_RATE_DIV * 90.0f));
            }
            drone->state_a = 1;
            DroneAnim_CallAnim(0.0f, 0x44,
                               NDrone2_ExplosionDir(dcv, nf::p32_ptr<const void>(dcv->sm->change_param)),
                               0x47, 0, 0, dcv);
            return 1;
        case 2:
            if (d->state_a != 0) DroneFunc_SetDeathChannel(drone);
            return 1;
        case 3:
            if (d->firing != 0 && drone->subclass != 4 && drone->subclass != 9) return 1;
            DroneWeap_DropWeapon(dcv);
            return 1;
        case 0xc:
            if (drone->state_a != 0) {
                DroneFunc_SetDeathChannel(drone);
                drone->state_a = 0;
            }
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163fe8 NDrone2_DSTATE_Dead
// Morto: larga tudo, fica no chão pelo tempo dos corpos e então some (0x48); com a propriedade 0x31
// avisa os outros (0x15) ao entrar e de novo no temporizador 2.
uint32_t NDrone2_DSTATE_Dead(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            obj->flags |= 0x20;
            NDrone2_SetAsDead(dcv);
            Drone_AlertStatusSet(3, dcv);
            set_timer1(d, GameState.frame + NPCGlobals.corpse_time);
            if (!prop(d, 0x31) || d->head_shot != 0) return 1;
            d->flags2 |= 8;
            NDrone2_DroneAlertToObject(dcv, 0x15, dcv->obj, 0.0f, 0.0f, 1.0f);
            return 1;
        case 3:
            if (obj->unknown_fc == 0) remove_now(dcv);
            DroneFunc_DeadDrone(dcv);
            return 1;
        case 0xc:
            set_state(dcv, 0x48);
            return 1;
        case 0xd:
            if (!prop(d, 0x31)) return 1;
            if (drone->head_shot == 0) {
                d->flags2 |= 8;
                NDrone2_DroneAlertToObject(dcv, 0x15, dcv->obj, 0.0f, 0.0f, 1.0f);
            }
            d->timer2_time = static_cast<uint32_t>(float(GameState.frame) + FRAME_RATE_DIV * 60.0f);
            d->timer2_data = 0;
            d->timer2_on = 1;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172cd8 NDrone2_DSTATE_Fade
// O corpo some aos poucos (NDrone2_FadeOut) e é apagado.
uint32_t NDrone2_DSTATE_Fade(DCVars_tag* dcv, Drone_tag*, obj_tag* obj, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 1:
            if (GameState.level_file != 0x700004a) {
                d->flags &= ~0x20u;
                d->flags &= ~0x80u;
            }
            d->fade = float(dcv->obj->unknown_103[3]);
            return 1;
        case 0:
        case 10:
            return 1;
        case 3:
            if (obj->unknown_fc == 0 || NDrone2_FadeOut(dcv) != 0) remove_now(dcv);
            DroneFunc_DeadDrone(dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172dd8 NDrone2_DSTATE_FadeFast
// Some depressa (corpos demais); vindo de Fade, continua do alfa que tinha.
uint32_t NDrone2_DSTATE_FadeFast(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 1:
            if (GameState.level_file != 0x700004a) {
                d->flags &= ~0x20u;
                d->flags &= ~0x80u;
            }
            if (drone->sm.prev_state != 0x48) d->fade = float(dcv->obj->unknown_103[3]);
            return 1;
        case 0:
        case 10:
            return 1;
        case 3:
            if (obj->unknown_fc != 0 && NDrone2_FadeOut(dcv) == 0) return 1;
            remove_now(dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173f00 NDrone2_DSTATE_DeleteMe
uint32_t NDrone2_DSTATE_DeleteMe(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    remove_now(dcv);
    return 1;
}

// [decomp] 0x00173fc0 NDrone2_DSTATE_JustStand
// Teste: fica de pé girando devagar e balançando num círculo de 1 cm.
uint32_t NDrone2_DSTATE_JustStand(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    switch (msg->msg) {
        case 3: {
            drone->no_move = 1;
            drone->move_angles.y = drone->move_angles.y + 0.01f;
            const float t = float(GameState.frame) * 0.005f;
            obj->pos.x = obj->pos.x + PS2Sinf(t) * 0.01f;
            obj->pos.z = obj->pos.z + PS2Sinf(t + 1.5707964f) * 0.01f;
            return 1;
        }
        case 8:
            NDrone2_BulletImpact(dcv, nf::p32_ptr<HITDATA_tag>(msg->data), 0xb6, 1);
            return 1;
        case 0:
        case 1:
        case 5:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00174430 NDrone2_DSTATE_WaitForever
uint32_t NDrone2_DSTATE_WaitForever(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* msg) {
    return msg->msg == 0 || msg->msg == 1 ? 1 : 0;
}
