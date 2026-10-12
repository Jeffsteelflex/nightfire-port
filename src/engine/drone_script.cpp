// Estado 3 da IA (NDrone2_DSTATE_PlayScript): o personagem toca o script de entrada do posicionamento
// (Drone_tag::entry_script, um script de animação 06xxxxxx) e depois volta ao estado inicial. Alguns
// scripts têm tratamento próprio (os de ataque 06000168/06000169 falam e atacam no fim; 060007ac gira e
// some do roteiro de missão). Ver drone.h e docs/engine.md.

#include "engine/drone.h"
#include "engine/gamestate.h"

// [decomp] 0x0015ba68 NDrone2_DSTATE_PlayScript
// state_a guarda o estado para onde ir num impacto (0x56 ataque, 0 nenhum); state_b diz se o
// personagem procura o oponente enquanto toca.
uint32_t NDrone2_DSTATE_PlayScript(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            if (dcv->drone->type == 0x18) return Drone_SM_SetState(dcv->sm, 0x36, 0);
            drone->state_a = 0;
            drone->state_b = 1;
            const uint32_t script = dcv->drone->entry_script;
            if (script == 0x60005e6 || script == 0x60008ae) {
                drone->state_a = 0x56;
                DroneAnim_SetScript(dcv->drone->entry_script, 0x21, 0, 0, 0, 0, 1, dcv);
                return 1;
            }
            if (script == 0x6000168 || script == 0x6000169) {
                drone->state_a = 0;
                if (script == 0x6000168) {
                    DroneAnim_SetScript(dcv->drone->entry_script, 10, 0x32, 8, 0, 5, 0, dcv);
                } else {
                    DroneAnim_SetScript(dcv->drone->entry_script, 5, 0x32, 0x1e, 0, 5, 0, dcv);
                }
                NDrone2_AttackTalk(dcv);
                dcv->drone->flags |= 0x400000;
                return 1;
            }
            if (script == 0x60007ac) {
                drone->state_a = 0;
                drone->state_b = 0;
                DroneAnim_SetScript(dcv->drone->entry_script, 10, 0x32, 0xf, 0, 5, 0, dcv);
                dcv->drone->flags |= 0x400000;
                behaviour_util_setProperty(0x3b, drone->behaviour, 0);
                behaviour_util_setProperty(0x3a, drone->behaviour, 0);
                drone->unknown_ba0 &= 0xfeff;
                return 1;
            }
            drone->state_a = 0x56;
            DroneAnim_SetScript(dcv->drone->entry_script, 0x24, 0x11a, 0x1e, 0, 5, 0, dcv);
            return 1;
        }
        case 3: {
            if (drone->state_b != 0) {
                dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
                if (dcv->sm->result != 0) return Drone_SM_SetState(dcv->sm, dcv->sm->result, 0);
            }
            if (1.0f <= dcv->drone->alert) NDrone2_SeenAndAttacking(dcv);
            if (drone->sm.entered + 0x1e < GameState.frame) {
                const uint32_t script = dcv->drone->entry_script;
                if (0x6000168 <= script && script <= 0x6000169) NDrone2_AttackTalk(dcv);
            }
            return 1;
        }
        case 5: {
            // Fim do script: os de ataque tocam a animação de mirar (0x68) e atacam; os outros voltam ao
            // estado inicial (060007ac antes gira meia volta).
            const uint32_t script = dcv->drone->entry_script;
            if (script == 0x6000169) {
                dcv->drone->flags &= 0xffbfffffu;
                DroneAnim_CallAnim(0.0f, 10, 0, 0x68, 0, 0, dcv);
                dcv->drone->entry_script = 0;
                NDrone2_AttackTalk(dcv);
                return 1;
            }
            if (script == 0x6000168) {
                dcv->drone->flags &= 0xffbfffffu;
                dcv->drone->entry_script = 0;
                DroneAnim_CallAnim(0.0f, 10, 0, 0x68, 0, 0, dcv);
                NDrone2_AttackTalk(dcv);
                return 1;
            }
            if (script == 0x60007ac) {
                dcv->drone->flags &= 0xffbfffffu;
                drone->snap_rotate = 3.1415927f;
                DroneAnim_SetDAnimNow(0xf4, 0, 1, dcv);
            }
            dcv->drone->entry_script = 0;
            Drone_SM_SetState(dcv->sm, uint32_t(int32_t(int16_t(dcv->drone->initial_state))), 0);
            return 1;
        }
        case 6:
        case 8:
            DroneFunc_HandleImpact(dcv, msg, drone->state_a, 0);
            return 1;
        case 7:
        case 9:
        case 0x17:
        case 0x18:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, drone->state_a, 1);
            return 1;
        case 0x11:
        case 0x12:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            if (dcv->sm->result == 0) return 1;
            return Drone_SM_SetState(dcv->sm, dcv->sm->result, 0);
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            DroneFunc_ConsiderExplosive(dcv, reinterpret_cast<obj_tag*>(uintptr_t(msg->data)),
                                        int16_t(drone->state_a));
            return 1;
        default:
            return 0;
    }
}
