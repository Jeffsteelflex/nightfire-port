// Visão e percepção dos personagens da IA (módulo DroneVision e as partes de NDrone2/DroneFunc/Drone
// que escolhem e acompanham o oponente). Ver drone.h.
//
// O que um personagem vê. Uma vez por tick, Drone_InitComms monta os pontos de atenção (projéteis e
// fumaça: Drone_BuildDynamicAwarePoints), atualiza o oponente de cada um (posição, distância, ângulos
// e o fator dos volumes de IA onde ele está: Drone_ProcessOpponents) e chama
// DroneVision_ProcessDroneSight, que:
//  - acha quem alerta quem (DroneVision_FindAlertedDrones: pares de personagens, até 5 por tick, um
//    que sabe algo, flags2 != 0, e outro que ainda não; o segundo é alertado se o vê);
//  - calcula para cada um quanto vê o oponente (DroneVision_OpponentVisibility: 0,7 na distância
//    alcance x (1 + 2 x alerta), caindo com o ângulo fora do campo de visão, vezes os volumes de IA);
//  - testa a linha de visão em rodízio, parando no primeiro teste caro (DroneVision_SeekOpponentLOS:
//    um osso do oponente por tick, de Drone_View_Bones, e a arma atravessando a parede);
//  - conta os ticks vendo ou não o oponente; passado o tempo de reação (DroneFunc_ReactionTime), ele
//    é visto (DroneVision_HaveOpponentSight: bit 4, a última posição vista e, na primeira vez, a
//    mensagem 0xf ao próprio personagem).
// NDrone2_FindOpponent (no controle de cada personagem) escolhe o oponente: o jogador para inimigos e
// civis; para aliados, um helicóptero à frente ou um inimigo visível e mais perto que o atual.
//
// Divergências intencionais:
// - DroneWeap_GetWeaponInfo: o w da direção vem de lixo da pilha no original; aqui é 0.
// - DroneVision_GunThroughWall: o "tipo" passado a DroneWeap_GetWeaponInfo começa zerado (no original,
//   lixo da pilha que AnimDatumGetWeaponInfo sobrescreve).
// - NDrone2_PlaySFX/NDrone2_SpeechSFX com um som fora de Drone_SfxInfo e NDrone2_PlaySFX sem DCVars,
//   DroneFunc_LostSightTalk sem jogador e DroneFunc_HandleSoundAlerts sem o som ouvido seguiriam um
//   ponteiro nulo: aqui não fazem nada (contados em g_drone_vision_guards).
// - DroneVision_ShoutFromOtherDrone devolve sempre 0 como o original, em que o personagem que gritou
//   nunca é achado (as duas saídas do teste dão nulo); o resto da função é código morto e não foi
//   portado.

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

DroneSfxInfo Drone_SfxInfo[kDroneSfxCount];
uint32_t Drone_View_Bones[8];
uint32_t switch_BLIND_DRONES = 0;
uint32_t g_drone_vision_guards = 0;

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kRadToDeg = 57.29578f;

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
bool bots_on() { return mp_word(0x184) != 0; }
// Modos de objetivo do multijogador (proteção e demolição): o alvo fica 1,75 m acima do objeto.
bool objective_mode() { return mp_word(0x1a4) == 0x20000080u || mp_word(0x1a4) == 0x20000040u; }

Drone_tag* drone_of(obj_tag* obj) { return static_cast<Drone_tag*>(obj->data); }

template <class T>
T& at(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
// MPGame: 8 entradas de 0x30 bytes, o objeto do jogador em +0x1c e bits em +0x26.
obj_tag* mp_player(int32_t i) { return at<obj_tag*>(MPGame, uint32_t(i) * 0x30 + 0x1c); }
uint16_t mp_player_bits(int32_t i) { return at<uint16_t>(MPGame, uint32_t(i) * 0x30 + 0x26); }

// O teste "fora de ação" que o original repete em linha: objeto nulo, personagem morto ou morrendo
// (0x600), parado (sem 0x100), sem vida, tipo 0x11 (só em alguns lugares) ou com remoção pedida.
bool out_of_action(obj_tag* obj, bool with_type_11 = true) {
    if (obj == nullptr) return true;
    const Drone_tag* d = drone_of(obj);
    if ((d->flags & 0x600) != 0 || (d->flags & 0x100) == 0 || d->health <= 0.0f) return true;
    if (with_type_11 && obj->type == 0x11) return true;
    return (obj->life & 1) != 0;
}

// fptoui do libgcc (fp-bit.c): negativo, zero e NaN viram 0; acima de 2^32 - 1, o máximo.
uint32_t fptoui(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 4294967296.0f) return 0xffffffffu;
    return static_cast<uint32_t>(f);
}

_VECTOR sub(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }

// O cone de visão de DroneVision_ObjectVisibility e DroneVision_DroneCanSeeDrone: a menos de 2 m vê
// sempre; além de 50 m nunca; senão dentro do ângulo (view_angle 0 = todo lado) e do alcance.
float sight_cone(Drone_tag* drone, obj_tag* obj) {
    const _VECTOR d = sub(obj->pos, drone->obj->pos);
    const float dist = Vec_Magnitude(&d);
    if (dist < 2.0f) return 1.0f;
    const float diff = Vec_AngleDifference(drone->head_yaw, ATAN2_APPROX(d.x, d.z));
    if (50.0f < dist) return 0.0f;
    if (drone->view_angle != 0.0f && drone->view_angle - std::fabs(diff) < 0.0f) return 0.0f;
    return drone->range_e8 < dist ? 0.0f : 1.0f;
}

// Ângulo (graus) entre a frente de `me` e a direção até `obj` (como o original: atan2 de me - obj
// mais pi).
float facing_degrees(obj_tag* me, obj_tag* obj) {
    const _VECTOR d = sub(me->pos, obj->pos);
    const float a = ATAN2_APPROX(d.x, d.z);
    return Vec_AngleDifference(me->rot.y, a + kPi) * kRadToDeg;
}

// Contador de quadros de um canal ligado (switch_channels_MusicVars): começa em 1 e para em 0xffff.
uint16_t tick_channel(uint8_t ch) {
    uint16_t& c = switch_channels_MusicVars[ch];
    if (c == 0) c = 1;
    if (static_cast<uint16_t>(c - 1) < 0xfffe) c = static_cast<uint16_t>(c + 1);
    return c;
}

// Olhos do personagem para os testes de visão: a cabeça, puxada para dentro do cilindro de colisão
// fora do multijogador.
void drone_eyes(Drone_tag* drone, _VECTOR* eye) {
    NDrone2_GetHeadPos(drone->obj, eye, nullptr);
    if (!bots_on()) DroneFunc_ClampPosToCollisionCylinder(drone->obj, eye);
}

}  // namespace

// ---- linha de visão ----

// [decomp] 0x0017a5a0 DroneWeap_GetWeaponInfo
// Osso e posição da boca da arma na mão do personagem e a direção do cano (o -x do osso).
uint32_t DroneWeap_GetWeaponInfo(Drone_tag* drone, int32_t* bone, uint32_t* flash, uint16_t* kind, _VECTOR* pos,
                                 _VECTOR* dir) {
    obj_tag* obj = drone->obj;
    if (obj->sphere_src == nullptr) return 0;
    *bone = AnimDatumGetWeaponInfo(obj, nullptr, flash, kind);
    if (*bone < 0) return 0;
    if (obj->sphere_src == nullptr || (anim_of(obj)->anim_flags & 1) == 0) return 0;
    if (kind != nullptr && *kind == 0) *kind = 0xffff;
    MATRIX m;
    AnimGetBoneWorldTrans(obj, static_cast<uint32_t>(*bone), 1, pos, &m);
    // VU0: (-1, 0, 0) pelas três primeiras linhas mais a quarta (w = 1), só xyz.
    dir->x = m.m[3][0] - m.m[0][0];
    dir->y = m.m[3][1] - m.m[0][1];
    dir->z = m.m[3][2] - m.m[0][2];
    dir->w = 0.0f;  // port: no original, lixo da pilha
    dir->x = dir->x - pos->x;
    dir->y = dir->y - pos->y;
    dir->z = dir->z - pos->z;
    return 1;
}

// [decomp] 0x001499d0 DroneFunc_ClampPosToCollisionCylinder
// Traz pos para no máximo 0,4 m (no plano) do centro do objeto.
void DroneFunc_ClampPosToCollisionCylinder(obj_tag* obj, _VECTOR* pos) {
    _VECTOR d = sub(*pos, obj->pos);
    d.y = 0.0f;
    if (0.4f < Vec_Magnitude(&d)) {
        Vec_Normalise(&d, &d);
        Vec_MulR32(&d, &d, 0.4f);
        pos->x = obj->pos.x + d.x;
        pos->z = obj->pos.z + d.z;
    }
}

// [decomp] 0x00176f18 DroneVision_CanSeeObjectFrom
// Linha de visão de `from` (na sala cel) até o objeto: para personagens e jogadores, o osso `bone`
// (dentro do cilindro de colisão), testado do alvo para o olho; para outros objetos, a posição dele.
// Só testa a geometria se as salas se enxergam (AINetwork_TestRayCels), e conta o teste.
uint32_t DroneVision_CanSeeObjectFrom(Drone_tag* drone, obj_tag* obj, uint32_t bone, _VECTOR* from, cel_tag* cel,
                                      uint32_t mask) {
    CelPos_tag eye{};
    eye.pos = *from;
    eye.cel = cel;
    CelPos_tag target{};
    target.cel = obj->cel;
    if (static_cast<uint8_t>(obj->type - 2) < 2) {
        if (!bots_on()) {
            DroneFunc_GetBonePos(obj, bone, &target.pos);
            DroneFunc_ClampPosToCollisionCylinder(obj, &target.pos);
        } else {
            NDrone2_GetHeadPos(obj, &target.pos, nullptr);
        }
        obj_tag* me = drone->obj;
        if (target.cel != eye.cel && AINetwork_TestRayCels(&target.pos, target.cel, &eye.pos, eye.cel) == 0) return 0;
        ++NPCGlobals.vision_checks;
        return Collide_LineOfSight(&target.pos, &eye.pos, target.cel, obj, me, mask | 0xa27) ? 1 : 0;
    }
    target.pos = obj->pos;
    if (bots_on() && objective_mode()) target.pos.y = target.pos.y + 1.75f;
    obj_tag* me = drone->obj;
    if (eye.cel != target.cel && AINetwork_TestRayCels(&eye.pos, eye.cel, &target.pos, target.cel) == 0) return 0;
    ++NPCGlobals.vision_checks;
    return Collide_LineOfSight(&eye.pos, &target.pos, eye.cel, me, obj, 0xa27) ? 1 : 0;  // (sem mask)
}

// [decomp] 0x001785e0 NDrone2_CanSeeObject
// Linha de visão dos olhos do personagem até o osso `bone` do objeto.
uint32_t NDrone2_CanSeeObject(Drone_tag* drone, obj_tag* obj, uint32_t bone, uint32_t mask) {
    _VECTOR eye;
    drone_eyes(drone, &eye);
    return DroneVision_CanSeeObjectFrom(drone, obj, bone, &eye, drone->obj->cel, mask);
}

// [decomp] 0x00178438 DroneVision_CanSeePosition
// Linha de visão dos olhos de obj (dentro do cilindro de colisão) até pos na sala cel.
uint32_t DroneVision_CanSeePosition(obj_tag* obj, _VECTOR* pos, cel_tag* cel, obj_tag* ignore) {
    _VECTOR eye;
    NDrone2_GetHeadPos(obj, &eye, nullptr);
    DroneFunc_ClampPosToCollisionCylinder(obj, &eye);
    CelPos_tag a{};
    a.pos = eye;
    a.cel = obj->cel;
    CelPos_tag b{};
    b.pos = *pos;
    b.cel = cel;
    if (a.cel != cel && AINetwork_TestRayCels(&a.pos, a.cel, &b.pos, cel) == 0) return 0;
    ++NPCGlobals.vision_checks;
    return Collide_LineOfSight(&a.pos, &b.pos, a.cel, obj, ignore, 0xa27) ? 1 : 0;
}

// [decomp] 0x00178390 DroneVision_LineOfSightToObject
uint32_t DroneVision_LineOfSightToObject(CelPos_tag* from, CelPos_tag* to, obj_tag* a, obj_tag* b, int8_t,
                                         uint32_t mask) {
    if (from->cel != to->cel && AINetwork_TestRayCels(&from->pos, from->cel, &to->pos, to->cel) == 0) return 0;
    ++NPCGlobals.vision_checks;
    return Collide_LineOfSight(&from->pos, &to->pos, from->cel, a, b, mask | 0xa27) ? 1 : 0;
}

// [decomp] 0x00178520 DroneVision_GunThroughWall
// A boca da arma está do outro lado de uma parede: raio do ponto dentro do cilindro de colisão até ela.
uint32_t DroneVision_GunThroughWall(Drone_tag* drone) {
    if (drone->aiming == 0 || NDrone2_ObjectIsArmed(drone->obj) == 0) return 0;
    int32_t bone = 0;
    uint32_t flash = 0;
    uint16_t kind = 0;  // port: no original, lixo da pilha
    _VECTOR pos, dir;
    if (DroneWeap_GetWeaponInfo(drone, &bone, &flash, &kind, &pos, &dir) == 0) return 0;
    const _VECTOR muzzle = pos;
    DroneFunc_ClampPosToCollisionCylinder(drone->obj, &pos);
    HITDATA_tag* hits = nullptr;
    if (!Collide_RayIntersect(&pos, &muzzle, drone->obj->cel, drone->obj, nullptr, &hits, 0, 0x927, 0)) return 0;
    Collide_FreeHitList(&hits);
    return 1;
}

// ---- quanto vê ----

// [decomp] 0x001781a8 DroneVision_ObjectVisibility
// 1 se o objeto está no cone de visão do personagem (sem linha de visão), senão 0.
float DroneVision_ObjectVisibility(Drone_tag* drone, obj_tag* obj) {
    if (drone == nullptr || obj == nullptr) return 0.0f;
    return sight_cone(drone, obj);
}

// [decomp] 0x00175af0 DroneVision_OpponentVisibility
// Quanto o personagem vê o oponente (guardado em visibility; ele é visto de 0,7 em diante): 0,7 na
// distância alcance x (1 + 2 x alerta), mais perto mais; fora do campo de visão (meio ângulo + 22,5
// graus) nada, e na borda cai como um seno; vezes o fator dos volumes de IA no oponente, exceto com
// algo já sabido (flags2) e alerta total. A menos de 2 m basta a linha de visão.
float DroneVision_OpponentVisibility(Drone_tag* drone) {
    const float half = drone->view_angle + 0.3926991f;
    const float yaw = bots_on() ? drone->obj->rot.y : drone->head_yaw;
    const float diff = Vec_AngleDifference(yaw, drone->opp_yaw);
    if (drone->opponent == nullptr) {
        drone->visibility = 0.0f;
        return 0.0f;
    }
    if (drone->opp_dist < 2.0f && 0.0f < drone->range_e8 && NDrone2_CanSeeObject(drone, drone->opponent, 5, 0) != 0) {
        drone->visibility = 1.0f;
        return 1.0f;
    }
    const float range = drone->range_e8 * (drone->alert + drone->alert + 1.0f);
    float vis = 0.7f - (drone->opp_dist - range) / range;
    if (drone->view_angle != 0.0f) {
        const float margin = half - std::fabs(diff);
        if (margin < 0.0f) {
            drone->visibility = 0.0f;
            return 0.0f;
        }
        vis = vis * PS2Sinf((margin / drone->view_angle) * 1.5707964f);
    }
    float mul = 1.0f;
    if ((drone->flags2 == 0 || drone->alert < 1.0f) && drone->visibility_mul != 1.0f) mul = drone->visibility_mul;
    vis = vis * mul;
    if (vis <= 0.0f) vis = 0.0f;
    drone->visibility = vis;
    return vis;
}

// [decomp] 0x00175cb8 DroneVision_SeekOpponentLOS
// Linha de visão até o oponente, se ele está visível o bastante (0,7). Fora do multijogador e para
// quem não é civil, testa um osso por tick em rodízio (Drone_View_Bones), voltando ao primeiro assim
// que o vê, e marca se a arma atravessa a parede. Com eye2_flags 0x30000000, testa também de eye2
// (o resultado não é usado). Guarda o bit 2 (vê) e o anterior em 0x40.
uint32_t DroneVision_SeekOpponentLOS(Drone_tag* drone) {
    uint32_t f = drone->sight_flags;
    f = (f & 2) != 0 ? (f | 0x40) : (f & ~0x40u);
    drone->sight_flags = f & ~2u;
    if (drone->visibility < 0.7f) return 0;
    int8_t los = 0;
    if (drone->opponent != nullptr) {
        bool from_bot = false;
        if (bots_on()) {
            const int32_t idx = Control_Plr2Ind(drone->opponent);
            if (idx != -1) {
                const uint32_t bits = at<uint32_t>(drone->bot, uint32_t(idx) * 0x10 + 0xbc);
                if ((bits & 2) != 0) {
                    from_bot = true;
                    los = int8_t((bits >> 2) & 1);
                }
            }
        }
        if (!from_bot) {
            obj_tag* opp = drone->opponent;
            uint32_t seen = 0;
            if (opp != nullptr) {
                if (bots_on() || drone->side == 3) {
                    seen = NDrone2_CanSeeObject(drone, opp, 5, 0);
                } else {
                    if ((drone->sight_flags & 0x40) == 0) {
                        drone->view_bone = static_cast<uint16_t>(drone->view_bone + 1);
                        if (6 < drone->view_bone) drone->view_bone = 0;
                    } else if (drone->view_bone != 0 && NDrone2_CanSeeObject(drone, opp, Drone_View_Bones[0], 0) != 0) {
                        drone->view_bone = 0;
                        seen = 1;
                    }
                    if (seen == 0) {
                        seen = NDrone2_CanSeeObject(drone, drone->opponent, Drone_View_Bones[drone->view_bone], 0);
                    }
                    if (seen != 0 && drone->aiming != 0) {
                        drone->gun_in_wall = static_cast<uint8_t>(DroneVision_GunThroughWall(drone));
                    } else {
                        drone->gun_in_wall = 0;
                        drone->unknown_1d = 0;
                    }
                }
            }
            los = static_cast<int8_t>(seen);
        }
        if ((drone->eye2_flags & 0x30000000u) == 0x30000000u) {
            DroneVision_CanSeeObjectFrom(drone, drone->opponent, drone->view_bone, &drone->eye2, drone->eye2_cel, 0);
        }
    }
    if (los == 0) return 0;
    drone->sight_flags |= 2;
    return 1;
}

// [decomp] 0x0014ac60 DroneFunc_ReactionTime
// Ticks de linha de visão até o oponente ser visto: nas fases 02, 03, 05, 09 e 0a, 0 depois da primeira
// vez ou com alerta total, senão (1 - alerta) x 0,3 x (200 - estatística 4 do tipo); nas outras, 10.
uint32_t DroneFunc_ReactionTime(Drone_tag* drone) {
    float t;
    switch (GameState.level_file) {
        case 0x7000002: case 0x7000003: case 0x7000005: case 0x7000009: case 0x700000a:
            if ((drone->sight_flags & 8) != 0) return 0;
            if (1.0f <= drone->alert) return 0;
            t = (1.0f - drone->alert) * 30.0f * 0.01f * (FRAME_RATE_DIV * 200.0f - float(drone->stats[4]));
            break;
        default:
            t = FRAME_RATE_DIV * 10.0f;
            break;
    }
    return fptoui(t);
}

// [decomp] 0x001780a8 DroneVision_LogSeenOpponent
// A primeira vez que um inimigo (ou um civil em 07000009/0a) vê o jogador conta nas estatísticas; em
// 07000007 só se o jogador estiver armado.
void DroneVision_LogSeenOpponent(Drone_tag* drone) {
    if (drone->opponent != glb_players[0] || drone->seen_logged != 0) return;
    const uint8_t side = drone->side;
    if (side == 2) return;
    if (side == 3 && 1 < GameState.level_file - 0x7000009u) return;
    if (GameState.level_file == 0x7000007 && NDrone2_ObjectIsArmed(drone->opponent) == 0) return;
    drone->seen_logged = 1;
    PlrStat_LogEnemyDetectedPlayer(0);
}

// [decomp] 0x00175ef0 DroneVision_HaveOpponentSight
// O oponente é visto (depois do tempo de reação fora do multijogador): bit 4, a hora e a posição e
// rotação dele. Na primeira vez (com a propriedade 0x3c), o bit 8 e a mensagem 0xf ao próprio
// personagem; o tipo 4 também a 10 oito segundos depois e a música; com a propriedade 0x55, a missão
// falha (motivo 10).
void DroneVision_HaveOpponentSight(Drone_tag* drone) {
    const uint32_t now = GameState.frame;
    if (drone->opponent == nullptr) return;
    if (!bots_on() && drone->seen_ticks < DroneFunc_ReactionTime(drone)) return;
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(drone->obj, &dcv);
    drone->sight_flags |= 4;
    DroneVision_LogSeenOpponent(drone);
    if (bots_on() && drone->seen_ticks == 2) {
        BOTSTATE_combatWeaponChangeChoice(drone, drone->opponent, 1, 0, 0);
        if (drone->opponent != nullptr && at<uint8_t>(drone->bot, 0x768) == 0x3b &&
            BOTSTATE_opponentIsMissile(drone, reinterpret_cast<obj_tag*>(uintptr_t(0xffffffffu))) != 0) {
            BOTSTATE_reallyCloseCombatRange(drone);
        } else {
            BOTSTATE_defaultCombatRange(drone);
        }
    }
    obj_tag* opp = drone->opponent;
    if (opp == nullptr) return;
    drone->seen_time = now;
    drone->seen_opp_pos = opp->pos;
    drone->seen_opp_rot = opp->rot;
    if ((drone->sight_flags & 8) != 0 || behaviour_util_getProperty(0x3c, drone->behaviour) == 0) return;
    drone->first_seen = now;
    drone->sight_flags |= 8;
    Drone_SM_SendMsgSelf(0xf, 0, 0, 0, &dcv);
    if (drone->type == 4) {
        Drone_SM_SendMsgSelf(10, 0, FRAME_RATE_INT << 3, 0, &dcv);
        Music_Event(0xf, 1);
    }
    if (behaviour_util_getProperty(0x55, drone->behaviour) != 0) DroneFunc_SetMissionFailReason(drone, 10);
}

// ---- alerta entre personagens ----

// [decomp] 0x001760b0 DroneVision_DroneCanSeeDrone
// O outro personagem está a menos de 50 m, no cone de visão (in_view recebe 1) e à vista.
uint32_t DroneVision_DroneCanSeeDrone(Drone_tag* drone, Drone_tag* other, int8_t* in_view) {
    obj_tag* oo = other->obj;
    if (2500.0f < Vec_SqDist3D(&drone->obj->pos, &oo->pos)) return 0;
    const float vis = oo == nullptr ? 0.0f : sight_cone(drone, oo);
    if (vis < 0.7f) return 0;
    if (in_view != nullptr) *in_view = 1;
    return NDrone2_CanSeeObject(drone, other->obj, 5, 0);
}

// [decomp] 0x00176250 DroneVision_ConsiderAlerted
// `drone` (que pode ser alertado: flag 0x2000, não aliado, ativo, fora de certos estados) é alertado
// pelo que `alerter` sabe (flags2: 1/0x20/0x10000/0x200000 combate, 2 viu o oponente, 4, 8, 0x10
// tudo) se as propriedades dele aceitam esse tipo de alerta e se vê o outro. Ao ser alertado: flag
// 0x20000, flags2 recebe as do outro deslocadas 16 bits, alerted_by e a mensagem 0x1f. Em 0700000c/0d
// as classes 5 e 6 são alertadas sem ver (com a fala 0xd0). Devolve se o outro estava no cone de visão.
int8_t DroneVision_ConsiderAlerted(Drone_tag* drone, Drone_tag* alerter) {
    int8_t in_view = 0;
    if ((drone->flags & 0x2000) == 0 || drone->side == 2) return 0;
    if (drone->type == 5) return 0;
    if (out_of_action(drone->obj)) return 0;
    const uint32_t st = drone->sm.state;
    if (st < 0x3b) {
        if (0x38 < st) return 0;
        if (st == 3) {
            if (1.0f <= drone->alert) return 0;
        } else if (st == 0x1b) {
            if (GameState.level_file == 0x7000014) return 0;
        }
    } else if (0x62 < st) {
        if (st < 0x65 || st == 0x85) return 0;
    }
    if ((drone->flags & 0x20000) != 0) return 0;
    if (float(GameState.frame) < float(alerter->attack_time) + FRAME_RATE_DIV * 60.0f) return 0;
    const uint32_t level = GameState.level_file;
    bool mark = false;
    if (0x700000b < level && level < 0x700000e && static_cast<uint16_t>(drone->klass - 5) < 2) {
        if (Drone_IsTalking(drone) == 0) NDrone2_SpeechSFXd(drone, 0xd0, 0, 0, 0);
        mark = true;
    } else {
        if (level == 0x7000007 && NPCGlobals.interrogating != 0) {
            drone->range_e8 = 20.0f;
            drone->view_angle = 3.1415927f;
        }
        uint32_t* props = drone->behaviour;
        const uint32_t& f = alerter->flags2;
        bool look = false;
        if ((f & 8) != 0) {
            if (behaviour_util_getProperty(0x26, props) != 0 || behaviour_util_getProperty(0x29, props) != 0) {
                if ((f & 2) != 0 && behaviour_util_getProperty(0x27, props) != 0) look = true;
            }
            if (!look && (f & 8) != 0 && behaviour_util_getProperty(0x29, props) != 0) look = true;
        }
        if (!look) {
            if ((f & 0x10) != 0) {
                look = true;
            } else if ((f & 2) != 0 && behaviour_util_getProperty(0x27, props) != 0) {
                look = true;
            } else if ((f & 4) != 0 && behaviour_util_getProperty(0x4f, props) != 0) {
                look = true;
            } else if ((f & 1) != 0 && behaviour_util_getProperty(0x28, props) != 0 && (f & 0x20) != 0) {
                look = true;
            } else if ((f & 0x10000) != 0 && behaviour_util_getProperty(0x28, props) != 0 && (f & 0x200000) != 0) {
                look = true;
            } else {
                if ((f & 1) == 0) return 0;
                if (behaviour_util_getProperty(0x28, props) == 0) return 0;
                look = true;
            }
        }
        if (DroneVision_DroneCanSeeDrone(drone, alerter, &in_view) == 0) return in_view;
        mark = true;
    }
    if (mark) {
        drone->flags |= 0x20000;
        uint32_t bits = alerter->flags2 << 16;
        if (GameState.level_file == 0x7000007) bits |= alerter->flags2 & 0xffff0000u;
        drone->alerted_by = alerter;
        drone->flags2 |= bits;
        Drone_Message(drone->obj, 0x1f, 0, 0);
    }
    return in_view;
}

// [decomp] 0x001766f8 DroneVision_DetermineAlertCheck
// Dos dois, o que sabe algo (flags2, ativo, não aliado, ainda não alertado) pode alertar o outro.
int8_t DroneVision_DetermineAlertCheck(Drone_tag* a, Drone_tag* b) {
    auto knows = [](const Drone_tag* d) {
        if ((d->flags & 0x20000) != 0 || (d->flags & 0x100) == 0 || d->side == 2) return false;
        return d->flags2 != 0;
    };
    const bool ka = knows(a);
    const bool kb = knows(b);
    if (ka) {
        if (kb) return 0;
        return DroneVision_ConsiderAlerted(b, a);
    }
    if (!kb) return 0;
    return DroneVision_ConsiderAlerted(a, b);
}

// [decomp] 0x001767d0 DroneVision_FindAlertedDrones
// Testa os pares de personagens (cada um com os seguintes na lista) até 5 estarem no cone de visão
// do outro, e retoma no tick seguinte do par em que parou (NPCGlobals.alert_cursor). Depois de
// terminar o par guardado, recomeça as linhas pelo primeiro personagem, como o original.
void DroneVision_FindAlertedDrones() {
    NPCGLOBALS_tag* n = &NPCGlobals;
    const uint16_t count = n->drones.count;
    if (count == 0) return;
    Drone_tag* const first = reinterpret_cast<Drone_tag*>(n->drones.first);
    if (first == nullptr) return;
    if (GameState.level_file == 0x7000004 || GameState.level_file == 0x700001b) return;
    uint16_t rows = 0, found = 0;
    bool stop = false;
    Drone_tag* a = first;
    Drone_tag* b = nullptr;
    auto next = [](Drone_tag* d) { return reinterpret_cast<Drone_tag*>(d->node.next); };
    if (n->alert_cursor[0] != 0 || n->alert_cursor[1] != 0) {
        while (a != nullptr && a->sm.id != n->alert_cursor[0]) a = next(a);
        if (a == nullptr) a = first;
        b = next(a);
        if (b == nullptr) b = first;
        while (b != nullptr && b->sm.id != n->alert_cursor[1]) b = next(b);
        if (b == nullptr) b = first;
        // Retoma o par guardado (conta como linha a cada par).
        while (b != nullptr) {
            if (DroneVision_DetermineAlertCheck(a, b) != 0) ++found;
            ++rows;
            b = next(b);
            if (4 < found) stop = true;
            if (b == nullptr || stop) break;
        }
    }
    a = first;
    if (rows >= count) return;
    if (!stop) {
        b = next(a);
        for (;;) {
            if (b != nullptr) {
                do {
                    if (DroneVision_DetermineAlertCheck(a, b) != 0) ++found;
                    b = next(b);
                    if (4 < found) stop = true;
                } while (b != nullptr && !stop);
            }
            if (stop) break;
            ++rows;
            a = next(a);
            if (a == nullptr) break;
            if (rows >= count) return;
            b = next(a);
        }
    }
    if (rows >= count) return;
    n->alert_cursor[0] = a != nullptr ? a->sm.id : 0;
    n->alert_cursor[1] = b != nullptr ? b->sm.id : 0;
}

// [decomp] 0x00176a28 DroneVision_ProcessDroneSight
// A visão de todos por tick (15 quadros depois da carga): alertas entre personagens, a posição dos
// olhos e a visibilidade do oponente de cada um; a linha de visão em rodízio a partir de onde parou,
// até o primeiro teste caro; e os ticks vendo ou não o oponente, com as falas "achei" e "sumiu".
void DroneVision_ProcessDroneSight() {
    NPCGLOBALS_tag* n = &NPCGlobals;
    uint16_t left = n->drones.count;
    if (left == 0) return;
    const uint32_t frame = GameState.frame;
    if (frame < n->postload_frame + 0xfu || frame < n->unknown_1ac + 0xfu) return;
    auto next = [](Drone_tag* d) { return reinterpret_cast<Drone_tag*>(d->node.next); };
    DroneVision_FindAlertedDrones();
    const uint16_t checks = n->vision_checks;
    for (Drone_tag* d = reinterpret_cast<Drone_tag*>(n->drones.first); d != nullptr; d = next(d)) {
        obj_tag* obj = d->obj;
        if (out_of_action(obj)) continue;
        d->visibility_here = Drone_VisibilityForPosition(&obj->pos, 0);
        if (d->obj != nullptr) NDrone2_GetHeadPosOld(d->obj, &d->head_pos, &d->head_yaw);
        if ((d->flags & 0x10000) == 0 || d->opponent == nullptr) continue;
        d->sight_flags &= ~4u;
        if (switch_BLIND_DRONES != 0 || d->health <= 0.0f || NPCGlobals.unknown_1c74 != 0 ||
            DroneVision_OpponentVisibility(d) < 0.7f) {
            d->visibility = 0.0f;
        }
    }
    // Linha de visão em rodízio, a partir do seguinte ao último testado.
    Drone_tag* first = reinterpret_cast<Drone_tag*>(n->drones.first);
    Drone_tag* c = nullptr;
    if (n->drones.count != 0) {
        for (c = first; c != nullptr && c->sm.id != n->sight_cursor; c = next(c)) {
        }
        if (c == nullptr) c = first;
    }
    Drone_tag* d = (c == nullptr || next(c) == nullptr) ? first : next(c);
    for (;;) {
        if (d == nullptr) d = reinterpret_cast<Drone_tag*>(n->drones.first);
        n->sight_cursor = d->sm.id;
        const uint32_t now = GameState.frame;
        if (!out_of_action(d->obj) && (d->flags & 0x10000) != 0 && d->opponent != nullptr) {
            DroneVision_SeekOpponentLOS(d);
            d->unknown_d4 = now;
        }
        --left;
        if (left == 0 || checks < n->vision_checks) break;
        d = next(d);
    }
    // Ticks vendo e sem ver o oponente.
    for (Drone_tag* e = reinterpret_cast<Drone_tag*>(n->drones.first); e != nullptr; e = next(e)) {
        const uint32_t f = e->sight_flags;
        if (!out_of_action(e->obj) && (e->flags & 0x10000) != 0 && e->opponent != nullptr && (f & 2) != 0) {
            e->lost_ticks = 0;
            ++e->seen_ticks;
            DroneVision_HaveOpponentSight(e);
            ++n->unknown_188[2];
            if (FRAME_RATE_INT < e->seen_ticks && n->new_sight_said == 0) {
                DCVars_tag dcv{};
                Drone_DCVfromOBJ(e->obj, &dcv);
                DroneFunc_NewSightTalk(&dcv);
                n->lost_sight_said = 0;
                n->new_sight_said = 1;
            }
            continue;
        }
        const uint32_t lost = e->lost_ticks + 1;
        e->sight_flags = f & ~4u;
        e->lost_ticks = lost;
        e->seen_ticks = 0;
        e->lost_sight = 1;
        if ((f & 8) == 0 || lost <= FRAME_RATE_INT * 2) continue;
        ++n->unknown_188[3];
        if (n->lost_sight_said != 0 || n->prev_counts[5] != 0) continue;
        DCVars_tag dcv{};
        Drone_DCVfromOBJ(e->obj, &dcv);
        DroneFunc_LostSightTalk(&dcv);
        n->lost_sight_said = 1;
        n->new_sight_said = 0;
    }
}

// [decomp] 0x0013f420 DroneFunc_NewSightTalk
// Em 0700000c/0d, guardas das classes 5 e 6 avisam que viram o oponente (fala 0xd6 ou 0xd7).
uint32_t DroneFunc_NewSightTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (NDrone2_Player() == nullptr || bots_on()) return 0;
    if (Drone_IsTalking(d) != 0 || d->health <= 0.0f) return 0;
    if (7 <= d->klass || d->klass < 5) return 0;
    uint32_t sfx = 0;
    if (GameState.level_file - 0x700000cu < 2) sfx = Rand_Rand(2) + 0xd6;
    if (sfx == 0) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->seen_opp_pos);
    return 1;
}

// [decomp] 0x0013f550 DroneFunc_LostSightTalk
// Em 0700000c/0d, guardas das classes 5 e 6 na sala do jogador avisam que o perderam (0xd3 a 0xd5).
uint32_t DroneFunc_LostSightTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    if (Drone_IsTalking(d) != 0 || d->health <= 0.0f) return 0;
    if (7 <= d->klass || d->klass < 5) return 0;
    uint32_t sfx = 0;
    if (GameState.level_file - 0x700000cu < 2) {
        obj_tag* player = NDrone2_Player();
        if (player == nullptr) {
            ++g_drone_vision_guards;  // port: o original leria a sala de um jogador nulo
        } else if (d->obj->cel == player->cel) {
            sfx = Rand_Rand(3) + 0xd3;
        }
    }
    if (sfx == 0) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->seen_opp_pos);
    return 1;
}

// ---- alertas por mensagem ----

// [decomp] 0x00177800 DroneVision_AlertSound
// Reação a um som (mensagem 0x14) de quem ouve (propriedade 0x1f): 0x56 (atacar) para os tipos 9,
// 0x12, 0x13, 0x16 com alerta acima de 0,7 e para as classes 5/6 em 0700000c/0d; 0xa2 (investigar)
// para os outros com som forte (passo de alerta > 0,2) ou alerta acima de 0,66; senão 0. Longe do
// oponente (50 m) ou já atacando, nada.
uint32_t DroneVision_AlertSound(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    bool generic = true;
    bool attack = false;
    NDrone2_Player();
    if (behaviour_util_getProperty(0x1f, dcv->drone->behaviour) == 0) return 0;
    if ((dcv->drone->flags & 0x400000) != 0) return 0;
    if (d->type == 0x1e) return BOT_reactToDroneAlertMsg(0x14, d, nullptr) != 0 ? 0xee : 0;
    const uint32_t level = GameState.level_file;
    if (0x700000b < level) {
        if (level < 0x700000e) {
            attack = static_cast<uint16_t>(d->klass - 5) < 2;
        } else if (level == 0x7000014 && d->sm.state == 0x1b) {
            return 0;
        }
    }
    if (!attack && 50.0f < d->opp_dist) return 0;
    if ((d->flags & 0x4000000) != 0) return 0;
    const uint8_t t = d->type;
    if (t == 9 || t == 0x12 || t == 0x13 || t == 0x16) {
        if (0.7f < d->alert) attack = true;
        generic = false;
    }
    if (attack) {
        d->flags |= 0x4000000;
        dcv->drone->flags |= 0x10000000;
        return 0x56;
    }
    if (!generic) return 0;
    if (0.2f < d->sound_alert_step || 0.66f < d->alert) return 0xa2;
    return 0;
}

// [decomp] 0x00177a30 DroneVision_ShoutFromOtherDrone
// Reação ao grito de outro personagem. No original o personagem que gritou nunca é achado (as duas
// saídas do teste dão nulo) e a função sempre devolve 0; o resto (distância por fase, mesmo time no
// multijogador, ataque) é código morto.
int32_t DroneVision_ShoutFromOtherDrone(DCVars_tag*, DroneAlert_tag*, uint32_t) { return 0; }

// [decomp] 0x00177c18 DroneVision_EnemyAlerts
// Trata as mensagens de alerta (0x11 a 0x16 e 0x1e) que os estados repassam e devolve o estado a
// seguir (0 nenhum). Antes guarda onde estava (estado, pés, sala) para voltar depois.
int32_t DroneVision_EnemyAlerts(DCVars_tag* dcv, MsgObject* msg) {
    auto* alert = reinterpret_cast<DroneAlert_tag*>(uintptr_t(msg->data));
    // (o original calcula o personagem que mandou o alerta, mas o teste usa sempre nulo)
    if (dcv->drone == nullptr) return 0;
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x800000) != 0) return 0;
    if (d->unknown_33[0] == 0 && dcv->drone->explosive_seen == 0) {
        d->return_state = static_cast<uint16_t>(dcv->sm->state);
        NDrone2_FeetPos(dcv->obj, &dcv->drone->return_pos);
        dcv->drone->return_cel = dcv->obj->cel;
    }
    auto shout = [&](int32_t prop, uint32_t m) -> int32_t {
        if (prop >= 0 && behaviour_util_getProperty(prop, dcv->drone->behaviour) == 0) return 0;
        return static_cast<int16_t>(DroneVision_ShoutFromOtherDrone(dcv, alert, m));
    };
    switch (msg->msg) {
        case 0x11: return shout(0x28, 0x11);
        case 0x12: return shout(0x27, 0x12);
        case 0x13: return shout(0x4f, 0x13);
        case 0x14: return static_cast<int32_t>(DroneVision_AlertSound(dcv));
        case 0x15: return shout(0x29, 0x15);
        case 0x16: return shout(-1, 0x16);
        case 0x1e: {
            Drone_tag* s = dcv->drone;
            if ((s->flags & 0x800000) != 0) return 0;
            const uint8_t t = s->type;
            if (t == 0x12 || t == 9) return 0;
            if (t == 0x1e) {
                s->alert = 1.0f;
                if (!MP_areObjectsOnSameTeam(s->obj, nullptr)) return 0;
                return BOT_reactToDroneAlertMsg(0x1e, s, nullptr) != 0 ? 0xcf : 0;
            }
            s->flags |= 0x4000000;
            dcv->drone->flags |= 0x40000000;
            return 0x56;
        }
        default:
            return 0;
    }
}

// [decomp] 0x00177ed0 DroneVision_EnemyLookForOpponent
// Chamada pelos estados de guarda: se o oponente é visto (bit 4) e o personagem pode reagir, passa a
// atacar (0x56; flags 0x4000000 e 0x8000000). O jogador só é atacado com alerta acima de 0,66 ou com
// a propriedade 0x51; civis só em 07000009/0a.
uint32_t DroneVision_EnemyLookForOpponent(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (static_cast<int16_t>(dcv->sm->state) == 3 && 1.0f <= d->alert) return 0;
    if (switch_BLIND_DRONES != 0 || d->opponent == nullptr) return 0;
    if (behaviour_util_getProperty(0x3c, dcv->drone->behaviour) == 0) return 0;
    Drone_tag* e = dcv->drone;
    if ((e->sight_flags & 4) == 0) return 0;
    const uint32_t level = GameState.level_file;
    if ((0x700000a < level || level < 0x7000009) && e->side == 3) return 0;
    if (d->opponent == NDrone2_Player() && d->alert < 0.66f &&
        behaviour_util_getProperty(0x51, dcv->drone->behaviour) == 0) {
        return 0;
    }
    e = dcv->drone;
    if ((e->flags & 0x800) != 0 || (e->flags2 & 0x20) != 0) return 0;
    if (behaviour_util_getProperty(0x57, e->behaviour) == 0) {
        const uint8_t t = dcv->drone->type;
        if (t == 7 || t == 0xe) {
            const uint32_t st = dcv->sm->state;
            if (st == 0x85 || st == 99) return 0;
        }
        e = dcv->drone;
    }
    if ((e->flags & 0x4000000) != 0) return 0x56;
    e->flags |= 0x4000000;
    dcv->drone->flags |= 0x8000000;
    return 0x56;
}

// [decomp] 0x001782a8 DroneVision_BeginInterogate
void DroneVision_BeginInterogate(Drone_tag* drone) {
    drone->flags2 |= 0x20;
    NPCGlobals.interrogating = 1;
}

// [decomp] 0x001782c8 DroneVision_EndInterogate
void DroneVision_EndInterogate(Drone_tag* drone) {
    drone->flags2 = (drone->flags2 & ~0x20u) | 0x10;
    if (GameState.level_file != 0x7000007) NPCGlobals.interrogating = 0;
}

// [decomp] 0x00178308 DroneVision_SetInterogateAssist
void DroneVision_SetInterogateAssist(Drone_tag* drone) { drone->flags2 |= 0x20; }

// [decomp] 0x00178318 DroneVision_EndInterogateAssist
void DroneVision_EndInterogateAssist(Drone_tag* drone) {
    drone->alerted_by = nullptr;
    drone->flags2 = (drone->flags2 & 0xffdfffdfu) | 0x10;
}

// [decomp] 0x00178348 DroneVision_TestEndInterogationAssist
// 0 enquanto o interrogador (alerted_by) só tem os bits de interrogatório (0x20/0x200000, mais o 1).
uint32_t DroneVision_TestEndInterogationAssist(Drone_tag* drone) {
    const Drone_tag* p = drone->alerted_by;
    if (p != nullptr && (p->flags2 & 0x200020u) != 0 && (p->flags2 & 0xffffffdeu) == 0) return 0;
    return 1;
}

// ---- oponentes ----

// [decomp] 0x0013a1f8 Drone_VisibilityForPosition
// Produto dos valores dos volumes de IA do tipo pedido que contêm pos (1 fora deles).
float Drone_VisibilityForPosition(_VECTOR* pos, uint16_t kind) {
    float v = 1.0f;
    for (auto* vol = reinterpret_cast<AIVolume_tag*>(AIBoxList.first); vol != nullptr;
         vol = reinterpret_cast<AIVolume_tag*>(vol->node.next)) {
        if (vol->kind == kind && Intersect_PointOOBox(&vol->obj->matrix, &vol->half, pos, 0.0f) != 0) {
            v = v * vol->value;
        }
    }
    return v;
}

// [decomp] 0x001390c0 Drone_ProcessOpponents
// Fator de visibilidade do oponente de cada personagem (volumes de IA do tipo 0 onde ele está; para o
// jogador, 0,75 a mais no subestado 4) e as informações do oponente (Drone_GetOpponentInfo).
void Drone_ProcessOpponents() {
    obj_tag* player = NDrone2_Player();
    float player_mul = 1.0f;
    if (player != nullptr) {
        player_mul = Drone_VisibilityForPosition(&player->pos, 0);
        if (static_cast<int16_t>(player->unknown_f6) == 4) player_mul = player_mul * 0.75f;
    }
    for (LLNODE_tag* l = NPCGlobals.drones.first; l != nullptr; l = l->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
        obj_tag* obj = d->obj;
        obj_tag* opp = d->opponent;
        if (opp != nullptr) {
            if (opp->type == 2) d->visibility_mul = drone_of(opp)->visibility_here;
            opp = d->opponent;
            d->visibility_mul = opp == player ? player_mul : Drone_VisibilityForPosition(&opp->pos, 0);
        }
        Drone_GetOpponentInfo(d, obj);
    }
}

namespace {

void add_aware_point(obj_tag* obj, uint8_t kind, float radius) {
    NPCGLOBALS_tag* n = &NPCGlobals;
    if (n->aware_count >= kMaxAwarePoints) return;
    DroneAwarePoint_tag& p = n->aware_points[n->aware_count++];
    p.kind = kind;
    p.obj = obj;
    p.pos = obj->pos;
    p.cel = obj->cel;
    p.radius = radius;
}

}  // namespace

// [decomp] 0x00138b10 Drone_BuildDynamicAwarePoints
// Fora do multijogador, refaz os pontos de atenção (até 64) e marca as ligações da rede à volta deles
// (0x100 perigo, 0x200 fumaça): nuvens de fumaça (tipo 0x2a, raio 3) e os projéteis das armas 0x2a,
// 0x2c-0x2f, 0x34, 0x6c (raio 12), 0x36 (raio 3), 0x37 (raio 8, fora do estado 3) e 0x3b-0x40 (raio
// 12, só com o pavio abaixo de 180).
void Drone_BuildDynamicAwarePoints() {
    if (bots_on()) return;
    NPCGLOBALS_tag* n = &NPCGlobals;
    n->aware_count = 0;
    std::memset(n->aware_points, 0, sizeof(n->aware_points));
    for (obj_tag* obj = control_first_object(); obj != nullptr; obj = obj->next) {
        if (obj->type == 0x2a) {
            add_aware_point(obj, 0, 3.0f);
            add_aware_point(obj, 2, 3.0f);
            AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 3.0f, 0x200, 0);
            continue;
        }
        if (obj->type != 5) continue;
        auto* bu = static_cast<BU_tag*>(obj->data);
        if (bu == nullptr || bu->weapon == nullptr) continue;
        switch (static_cast<uint16_t>(bu->weapon->id)) {
            case 0x2a: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
                add_aware_point(obj, 2, 12.0f);
                AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 12.0f, 0x100, 0);
                break;
            case 0x34: case 0x6c:
                add_aware_point(obj, 2, 12.0f);
                AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 12.0f, 0x100, 0);
                [[fallthrough]];
            case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40:
                if (static_cast<BU_tag*>(obj->data)->timer < 180.0f) {
                    add_aware_point(obj, 2, 12.0f);
                    AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 12.0f, 0x100, 0);
                }
                break;
            case 0x36:
                add_aware_point(obj, 1, 3.0f);
                AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 3.0f, 0x200, 0);
                break;
            case 0x37:
                if (static_cast<int16_t>(obj->state) == 3) break;
                add_aware_point(obj, 2, 8.0f);
                AINetwork_SetLinksFlagInCircle(&obj->pos, obj->cel, 8.0f, 0x100, 0);
                break;
            default:
                break;
        }
    }
}

// [decomp] 0x00141f28 DroneFunc_AllocateTargetID
// O lugar do alvo em NPCGlobals.opponents (o que já o tem ou o primeiro livre); só para inimigos.
uint8_t DroneFunc_AllocateTargetID(Drone_tag* drone, obj_tag* obj) {
    if (drone->side != 1) return 0xff;
    NPCOpponent_tag* o = NPCGlobals.opponents;
    for (uint32_t i = 0; i < 8; ++i) {
        if ((o[i].flags & 1) != 0 && o[i].obj == obj) return static_cast<uint8_t>(i);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        if ((o[i].flags & 1) == 0) {
            o[i].obj = obj;
            o[i].flags = 1;
            o[i].pos.z = 0.0f;
            o[i].frame = 0;
            o[i].pos.x = 0.0f;
            o[i].pos.y = 0.0f;
            return static_cast<uint8_t>(i);
        }
    }
    return 0xff;
}

// [decomp] 0x0014a008 DroneFunc_TargetSeen
// O alvo do personagem foi visto agora em pos (NPCGlobals.opponents: bit 2, quadro e posição).
void DroneFunc_TargetSeen(Drone_tag* drone, _VECTOR* pos) {
    const uint8_t id = drone->opponent_player;
    if (id == 0xff) return;
    NPCOpponent_tag& o = NPCGlobals.opponents[id];
    o.frame = GameState.frame;
    o.flags |= 2;
    o.pos = *pos;
}

// [decomp] 0x00141fe8 NDrone2_SetOpponentAimPos
// Onde mirar no oponente, relativo à posição dele (aim_offset): fora do multijogador, o osso que a
// visão está testando (ou o 2) de um personagem ou jogador animado; no multijogador, a cabeça de um
// personagem ou 0,6 m acima (0,2 agachado) de outros, e às vezes (1 em 4 x precisão) o osso 2 puxado
// 2/7 do caminho para a cabeça.
void NDrone2_SetOpponentAimPos(Drone_tag* drone) {
    obj_tag* opp = drone->opponent;
    if (opp == nullptr) return;
    _VECTOR* aim = &drone->aim_offset;
    auto animated = [](obj_tag* o) {
        if (o->sphere_src == nullptr || static_cast<uint8_t>(o->type - 2) >= 2) return false;
        if (o->type == 2 && (drone_of(o)->flags & 0x100000) == 0) return false;
        return (anim_of(o)->anim_flags & 1) != 0;
    };
    if (!bots_on()) {
        const uint32_t bone = (drone->sight_flags & 2) != 0 ? Drone_View_Bones[drone->view_bone] : 2;
        if (animated(opp)) {
            AnimGetBoneWorldTrans(opp, bone | 0x80000000u, 0, aim, nullptr);
        } else {
            *aim = opp->pos;
        }
    } else {
        if (opp->type == 2) {
            *aim = drone_of(opp)->head_pos;
        } else {
            *aim = opp->pos;
            const uint8_t move = opp->type == 2 ? DroneAnim_CurrentMoveState(drone_of(opp)) : 0;
            if (move != 4 && move != 0x1a) aim->y = aim->y + ((move == 3 || move == 5) ? 0.2f : 0.6f);
        }
        uint32_t range = 4;
        if ((at<uint8_t>(drone->bot, 0xac) & 1) == 0) range = static_cast<uint32_t>(drone->stats[0]) << 2;
        const bool skip = Rand_Rand(range) == range - 1;
        if (!skip) {
            obj_tag* o = drone->opponent;
            _VECTOR hip;
            if (o != nullptr && animated(o)) {
                AnimGetBoneWorldTrans(o, 0x80000002u, 0, &hip, nullptr);
                Vec_Swap(&hip, aim);
                aim->y = aim->y - (aim->y - hip.y) * 0.2857143f;
            }
        }
    }
    *aim = sub(*aim, drone->opponent->pos);
}

// [decomp] 0x001422d0 NDrone2_SetOpponent
// Troca o oponente: a mira (aim_offset; 1,75 m acima nos objetivos do multijogador), o alvo (posição +
// mira; com o mesmo oponente perdido de vista há mais de 5 s e a propriedade 0x17, a última posição
// vista) e o lugar dele em NPCGlobals.opponents.
void NDrone2_SetOpponent(Drone_tag* drone, obj_tag* opponent) {
    obj_tag* old = drone->opponent;
    drone->opponent = opponent;
    bool same = old == opponent;
    if (bots_on()) MP_sendBotMessage(drone->obj, 0x45, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(opponent)), 0);
    if (opponent == nullptr) {
        Vec_Zero(&drone->aim_offset);
        drone->opponent_player = 0xff;
        return;
    }
    const uint8_t t = opponent->type;
    if (t == 2 || t == 3) {
        NDrone2_SetOpponentAimPos(drone);
    } else if (t == 0x3b) {
        same = false;
        Vec_Zero(&drone->aim_offset);
    } else if (t != 1) {
        Vec_Zero(&drone->aim_offset);
        if (bots_on() && objective_mode()) drone->aim_offset.y = 1.75f;
    }
    _VECTOR base = opponent->pos;
    if (same && drone->first_seen != 0 && FRAME_RATE_DIV * 5.0f < float(drone->lost_ticks) &&
        behaviour_util_getProperty(0x17, drone->behaviour) != 0) {
        base = drone->seen_opp_pos;
    }
    drone->target_pos = {base.x + drone->aim_offset.x, base.y + drone->aim_offset.y, base.z + drone->aim_offset.z,
                         base.w + drone->aim_offset.w};
    const uint8_t id = DroneFunc_AllocateTargetID(drone, opponent);
    drone->opponent_player = id;
    if (id == 0xff) return;
    NPCOpponent_tag& o = NPCGlobals.opponents[id];
    o.frame = GameState.frame;
    o.flags |= 2;
    o.pos = opponent->pos;
}

namespace {

// O ramo do multijogador de NDrone2_FindOpponent (bots): o mais "barato" dos 8 jogadores segundo o que
// o bot sabe deles (drone->bot + 0xb0 + i * 0x10: +4 distância, +8 ângulo, +0xc bits), e os objetivos
// de proteção e demolição.
uint32_t find_opponent_bots(Drone_tag* drone) {
    obj_tag* me = drone->obj;
    if (out_of_action(me)) {
        if (drone->opponent == nullptr) return 0;
        NDrone2_SetOpponent(drone, nullptr);
        return 0;
    }
    void* bot = drone->bot;
    const uint32_t team = MP_getObjectTeam(me);
    const uint32_t self_idx = at<uint16_t>(bot, 0x75e);
    const float view_deg = drone->view_angle * kRadToDeg;
    float range_sq = drone->range_e8 * drone->range_e8;
    int32_t forced = at<int8_t>(drone->bot, 0x76f);
    const int8_t slot = at<int8_t>(bot, 0x76b);
    obj_tag* target = slot == -1 ? nullptr : mp_player(slot);
    const uint8_t any = at<uint8_t>(drone->bot, 0xac) & 8;
    if (target != nullptr && target->type != 3 && target->type != 2) {
        target = nullptr;
        at<int8_t>(bot, 0x76b) = -1;
    }
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(me, &dcv);
    at<int8_t>(drone->bot, 0x76f) = 0;
    if (any != 0) {
        range_sq = 90000.0f;
        forced = 1;
    }
    obj_tag* opp = drone->opponent;
    if (opp != nullptr) {
        bool clear = opp->type == 1 || (opp->type == 2 && out_of_action(opp, false));
        if (!clear) {
            const float mul = BOT_getAggressionMul(drone);
            clear = fptoui(FRAME_RATE * (mul * 20.0f)) < drone->lost_ticks;
        }
        if (clear && (!objective_mode() || static_cast<uint8_t>(drone->opponent->type - 2) < 2)) {
            NDrone2_SetOpponent(drone, nullptr);
        }
    }
    float cost[8];
    uint8_t distracted[8];
    float best = 640000.0f;
    int32_t best_idx = -1;
    obj_tag* best_obj = nullptr;
    for (int32_t i = 0; i < 8; ++i) {
        cost[i] = 640000.0f;
        obj_tag* o = mp_player(i);
        distracted[i] = 0;
        if (o == nullptr || o == me) continue;
        const uint8_t t = o->type;
        if (t == 2 && out_of_action(o, false)) continue;
        if (t == 0x11 || t == 0x12 || t == 1) continue;
        const uint32_t info = 0xb0 + uint32_t(i) * 0x10;
        const uint32_t bits = at<uint32_t>(bot, info + 0xc);
        if ((bits & 2) == 0) continue;
        float c = at<float>(bot, info + 4);
        if (!(c < range_sq)) continue;
        if ((bits & 4) == 0 && any == 0) continue;
        const uint16_t pbits = mp_player_bits(i);
        if ((bits & 8) != 0) {
            // O original calcula o ângulo até o jogador e não o usa.
            if (any == 0) facing_degrees(me, o);
            continue;
        }
        if (o == target) c = c * 0.04f;
        if ((pbits & 0xf) != 0) c = c * 0.25f;
        const float ang = at<float>(bot, info + 8);
        if (-10.0f < ang && ang < 10.0f) {
            if ((bits & 1) != 0) {
                if (t != 2 || drone_of(o)->opponent == me) {
                    c = c * 0.04f;
                    distracted[i] = 1;
                    BOTSTATE_increaseDistraction(drone, 4.0f);
                }
            } else if ((pbits & 0xf) != 0) {
                c = c * 0.25f;
            }
        }
        if (o != drone->opponent && o != target) {
            const uint8_t bm = at<uint8_t>(bot, 0xab);
            if (bm == 5 || bm == 2) {
                if (i < 4) {
                    for (int32_t k = 4; k < 8; ++k) {
                        if (k == i) continue;
                        obj_tag* p = mp_player(k);
                        if (p != nullptr && drone_of(p)->opponent == o) {
                            c = c * 16.0f;
                            break;
                        }
                    }
                } else if (at<int8_t>(drone_of(o)->bot, 0x770) != 0) {
                    c = c * 16.0f;
                }
            }
        }
        cost[i] = c;
        if (c < best) {
            best = c;
            best_idx = i;
            best_obj = o;
        }
    }
    if (best_idx != -1) {
        if (best_obj == drone->opponent) return BOT_handleOpponentHistory(drone, best_obj);
        while (best_obj != nullptr) {
            bool reject = false;
            if (forced == 0 && distracted[best_idx] == 0) {
                float deg;
                if (best_idx < 4) {
                    deg = facing_degrees(me, best_obj);
                } else {
                    deg = at<float>(drone_of(best_obj)->bot, self_idx * 0x10 + 0xb0 + 8);
                }
                reject = deg < -view_deg || view_deg < deg;
            }
            if (!reject) return BOT_handleOpponentHistory(drone, best_obj);
            cost[best_idx] = 640000.0f;
            best = 640000.0f;
            best_idx = -1;
            best_obj = nullptr;
            for (int32_t j = 0; j < 8; ++j) {
                if (cost[j] < best) {
                    best = cost[j];
                    best_idx = j;
                }
            }
            if (best_idx != -1) best_obj = mp_player(best_idx);
        }
    }
    obj_tag* goal = nullptr;
    const uint32_t mode = mp_word(0x1a4);
    obj_tag* ext = nullptr;
    bool has_ext = false;
    if (mode == 0x20000080u && team == 0) {
        ext = MP_getProtectionObj_NotExt();
        has_ext = true;
    } else if (mode == 0x20000040u && team == 1) {
        ext = MP_getDemolitionObj_NotExt();
        has_ext = true;
    }
    if (has_ext) {
        if (ext == nullptr) return 0;
        goal = static_cast<int16_t>(ext->unknown_f6) != 1 ? ext : nullptr;
    }
    if (goal == nullptr || goal == drone->opponent) return 0;
    if (!(Vec_SqDist3D(&me->pos, &goal->pos) < 16.0f)) return 0;
    const float deg = facing_degrees(me, goal);
    if (!(-view_deg < deg) || !(deg < view_deg)) return 0;
    if (NDrone2_CanSeeObject(drone, goal, 5, 0) == 0) return 0;
    MP_sendBotMessage(drone->obj, 0x3b, 1, 0);
    return BOT_handleOpponentHistory(drone, goal);
}

}  // namespace

// [decomp] 0x001424e0 NDrone2_FindOpponent
// Escolhe o oponente. Inimigos e civis: o jogador (o falso, se ligado), enquanto vivo e com a missão
// em andamento. Aliados: um helicóptero (tipo 0x3b, estados 1 a 3) entre 30 graus abaixo e 10 acima;
// senão mantêm o atual enquanto o veem; e procuram um inimigo mais perto (no plano) que o atual e à
// vista, retomando a lista de onde pararam e parando no primeiro teste de visão caro. No
// multijogador, o ramo dos bots.
uint32_t NDrone2_FindOpponent(Drone_tag* drone) {
    if (bots_on()) return find_opponent_bots(drone);
    NPCGLOBALS_tag* n = &NPCGlobals;
    const uint16_t checks = n->vision_checks;
    if (drone->side != 2) {
        obj_tag* p = NDrone2_Player();
        if (!PlrStat_OkToUpdate()) p = nullptr;
        if (p != nullptr) {
            if (p->type == 3) {
                if (static_cast<BLData*>(p->data)->health <= 0.0f) p = nullptr;
            } else if (p->type == 0x12) {
                p = nullptr;
            }
        }
        NDrone2_SetOpponent(drone, p);
        return 0;
    }
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        const int16_t st = static_cast<int16_t>(o->state);
        if (o->type != 0x3b || st <= 0 || 3 < st) continue;
        _VECTOR rot;
        Vec_Zero(&rot);
        const _VECTOR dir = sub(o->pos, drone->obj->pos);
        Vec_DirToRot(&dir, &rot);
        if (!(0.17453294f < rot.x) && !(rot.x < -0.5235988f)) {
            NDrone2_SetOpponent(drone, o);
            return 0;
        }
    }
    float best = 640000.0f;
    obj_tag* opp = drone->opponent;
    if (opp != nullptr) {
        bool drop = NDrone2_CanSeeObject(drone, opp, 5, 0) == 0;
        if (!drop) {
            opp = drone->opponent;
            if (opp->type == 2) {
                drop = out_of_action(opp, false);
            } else if (opp->type == 0x3b) {
                drop = 3 < static_cast<int16_t>(opp->state);
            }
        }
        if (drop) NDrone2_SetOpponent(drone, nullptr);
        if (drone->opponent != nullptr) best = drone->opp_dist * drone->opp_dist;
    }
    Drone_tag* cursor = drone->unknown_2c0;
    Drone_tag* d = cursor != nullptr ? reinterpret_cast<Drone_tag*>(cursor->node.next)
                                     : reinterpret_cast<Drone_tag*>(n->drones.first);
    const Drone_tag* current = drone->opponent != nullptr ? static_cast<Drone_tag*>(drone->opponent->data) : nullptr;
    if (d == nullptr || d == cursor) {
        drone->unknown_2c0 = d;
        return 0;
    }
    for (;;) {
        if (d != drone && d != current) {
            obj_tag* o = d->obj;
            uint32_t seen = 0;
            if (!out_of_action(o) && d->side == 1 && Vec_SqDist2D(&drone->obj->pos, &o->pos) < best) {
                seen = NDrone2_CanSeeObject(drone, d->obj, 5, 0);
            }
            if (seen != 0) {
                NDrone2_SetOpponent(drone, d->obj);
                drone->unknown_2c0 = d;
                return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(d->obj));
            }
            if (checks < n->vision_checks) {
                drone->unknown_2c0 = d;
                return 0;
            }
        }
        d = reinterpret_cast<Drone_tag*>(d->node.next);
        if (d == nullptr || d == drone->unknown_2c0) {
            drone->unknown_2c0 = d;
            return 0;
        }
    }
}

// [decomp] 0x0014a800 NDrone2_DroneInSights
uint32_t NDrone2_DroneInSights(DCVars_tag*) { return 0; }

// [decomp] 0x00155d48 NDrone2_SetupAlertTarget
// O alvo das rotas de alerta (NPCGlobals.alert_target) passa a ser a posição do alerta difundido.
void NDrone2_SetupAlertTarget() {
    NPCGLOBALS_tag* n = &NPCGlobals;
    n->alert_target.mask = 0x4000000;
    n->alert_target.pos.pos = n->alert.pos;
    n->alert_target.pos.cel = n->alert.cel;
    n->alert_target.frame = 0;
    n->alert_target.obj = nullptr;
}

// [decomp] 0x0014a538 NDrone2_DroneAlertToObject
// Difunde um alerta sobre um objeto (posição, sala, rotação e deslocamento dele) a todos em 30 quadros.
void NDrone2_DroneAlertToObject(DCVars_tag* dcv, uint32_t msg, obj_tag* obj, float a, float b, float c) {
    if (obj == nullptr) return;
    DroneAlert_tag& al = NPCGlobals.alert;
    al.obj = obj;
    al.frame = GameState.frame;
    al.sender = dcv->obj;
    al.msg = msg;
    al.pos = obj->pos;
    al.cel = obj->cel;
    al.rot = obj->rot;
    al.param[0] = a;
    al.vel = sub(obj->old_pos, obj->pos);
    al.param[1] = b;
    al.param[2] = c;
    NDrone2_SetupAlertTarget();
    Drone_SM_BroadcastMsg(msg, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&al)), 0x1e, dcv->drone->sm.id);
}

// [decomp] 0x0014a498 NDrone2_DroneAlertToPosition
// Difunde um alerta sobre uma posição (sem objeto) a todos em 30 quadros.
void NDrone2_DroneAlertToPosition(obj_tag* sender, uint32_t msg, _VECTOR* pos, cel_tag* cel, float a, float b,
                                  float c) {
    uint32_t id = 0;
    if (sender != nullptr && sender->type == 2) id = drone_of(sender)->sm.id;
    DroneAlert_tag& al = NPCGlobals.alert;
    al.sender = sender;
    al.frame = GameState.frame;
    al.msg = msg;
    al.obj = nullptr;
    al.cel = cel;
    al.pos = *pos;
    al.param[0] = a;
    al.param[1] = b;
    al.param[2] = c;
    NDrone2_SetupAlertTarget();
    Drone_SM_BroadcastMsg(msg, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&al)), 0x1e, id);
}

// [decomp] 0x00177420 NDrone2_NearArmedDrone
// Há um inimigo armado e ativo a até radius que o personagem vê (guardado em near_armed)? Com check,
// só para quem tem a propriedade 0x50.
uint32_t NDrone2_NearArmedDrone(DCVars_tag* dcv, float radius, int8_t check) {
    Drone_tag* me = dcv->drone;
    me->near_armed = nullptr;
    if (check != 0 && behaviour_util_getProperty(0x50, dcv->drone->behaviour) == 0) return 0;
    for (LLNODE_tag* l = NPCGlobals.drones.first; l != nullptr; l = l->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
        if (d == me) continue;
        obj_tag* obj = d->obj;
        if (out_of_action(obj) || (d->flags & 0x4200) != 0) continue;
        if (NDrone2_ObjectIsArmed(obj) == 0 || d->side != 1) continue;
        if (radius < Vec_Dist3D(&dcv->obj->pos, &obj->pos)) continue;
        Drone_tag* self = dcv->drone;
        _VECTOR eye;
        drone_eyes(self, &eye);
        if (DroneVision_CanSeeObjectFrom(self, obj, 5, &eye, self->obj->cel, 0) != 0) {
            me->near_armed = obj;
            return 1;
        }
    }
    return 0;
}

// [decomp] 0x00177640 NDrone2_NotNearArmedDrone
// Nenhum inimigo armado e vivo a até radius está à vista (nem é o de near_armed).
uint32_t NDrone2_NotNearArmedDrone(DCVars_tag* dcv, float radius) {
    for (LLNODE_tag* l = NPCGlobals.drones.first; l != nullptr; l = l->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
        if (d == dcv->drone) continue;
        obj_tag* obj = d->obj;
        if ((d->flags & 0x4200) != 0 || !(0.0f < d->health) || (d->flags & 0x100) == 0) continue;
        if (NDrone2_ObjectIsArmed(obj) == 0 || d->side != 1) continue;
        if (radius < Vec_Dist3D(&obj->pos, &dcv->obj->pos)) continue;
        Drone_tag* self = dcv->drone;
        if (obj == self->near_armed) return 0;
        _VECTOR eye;
        drone_eyes(self, &eye);
        if (DroneVision_CanSeeObjectFrom(self, obj, 5, &eye, self->obj->cel, 0) != 0) return 0;
    }
    return 1;
}

// ---- som ----

// [decomp] 0x00148a30 DroneFunc_HandleSoundAlerts
// O que o personagem (com a propriedade 0x1f) ouve no tick soma ao alerta (1% da intensidade); um som
// novo (mais de um segundo depois do anterior ou mais forte) numa sala conhecida vira o alvo
// sound_target e a mensagem 0x14. Em 07000007 só ouve quem está armado.
void DroneFunc_HandleSoundAlerts(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    float level = 0.0f;
    DYNAMICSOUNDS* sound = nullptr;
    if (behaviour_util_getProperty(0x1f, d->behaviour) == 0 || switch_BLIND_DRONES != 0) return;
    if ((dcv->drone->flags & 0x400000) != 0) return;
    if (GameState.level_file == 0x7000007 && NDrone2_ObjectIsArmed(dcv->obj) == 0) return;
    dcv->drone->sound_alertness = Sound_Alertness(&dcv->obj->pos, &level, &sound);
    if (!(0.0f < dcv->drone->sound_alertness)) return;
    d->sound_alert_step = d->sound_alertness * 0.01f;
    d->alert = d->alert + d->sound_alert_step;
    if (sound == nullptr) {
        ++g_drone_vision_guards;  // port: o original leria a posição de um som nulo
        return;
    }
    uint8_t* s = reinterpret_cast<uint8_t*>(sound);
    _VECTOR spos;
    std::memcpy(&spos, s + 0x10, sizeof(spos));
    cel_tag* cel = build_FindCel(&spos, glb_world);
    if (cel == nullptr) return;
    if (!(float(d->sound_target.heard_frame) + FRAME_RATE_DIV * 60.0f < float(GameState.frame)) &&
        !(d->sound_target.heard_level < level)) {
        return;
    }
    std::memcpy(&d->sound_info, s + 0x48, 4);
    d->sound_target.obj = nullptr;
    d->sound_target.mask = 0x4000000;
    d->sound_target.pos.cel = cel;
    d->sound_target.pos.pos = spos;
    d->sound_target.frame = 0;
    d->sound_target.heard_frame = GameState.frame;
    d->sound_target.heard_level = level;
    Drone_SM_SendMsgSelf(0x14, 0, 0, 0, dcv);
}

// [decomp] 0x001453a8 DroneFunc_CheckAlarmRaised
// Música de alarme: em 07000005 o canal 0xa4 ligado há menos de 1 s dá o evento 2/3; em 07000005/06 o
// canal 0x37 e em todas as fases o 0x61, ligados há menos de 10 s, o evento 2/1. Conta os quadros de
// cada canal ligado em switch_channels_MusicVars.
void DroneFunc_CheckAlarmRaised() {
    const uint32_t level = GameState.level_file;
    if (level == 0x7000005) {
        if (switch_channels[0xa4] != 0 && tick_channel(0xa4) < FRAME_RATE_INT) {
            Music_Event(2, 3);
            return;
        }
    }
    if (level == 0x7000005 || level == 0x7000006) {
        if (switch_channels[0x37] != 0 && tick_channel(0x37) < FRAME_RATE_INT * 10) {
            Music_Event(2, 1);
            return;
        }
    }
    if (switch_channels[0x61] != 0 && tick_channel(0x61) < FRAME_RATE_INT * 10) Music_Event(2, 1);
}

// [decomp] 0x00141848 NDrone2_PlaySFX
// Toca um som de Drone_SfxInfo na posição pedida (ou na do personagem) com o tom sorteado na faixa da
// entrada; não toca nos primeiros 30 quadros da fase nem, com check, antes do intervalo da entrada.
// Guarda o quadro, o som em sfx_time e no registro de NPCGlobals.
uint32_t NDrone2_PlaySFX(DCVars_tag* dcv, uint32_t sfx, _VECTOR* pos, int8_t check, float scale) {
    if (sfx >= kDroneSfxCount) {
        ++g_drone_vision_guards;  // port: o original leria a entrada de um ponteiro nulo
        return 0;
    }
    DroneSfxInfo& info = Drone_SfxInfo[sfx];
    if (info.sfx == 0) return 0;
    const uint32_t now = GameState.frame;
    if (float(now) < float(NPCGlobals.postload_frame) + FRAME_RATE_DIV * 30.0f) return 0;
    if (check != 0 && now < fptoui(float(info.last) + float(info.interval) * FRAME_RATE_DIV)) return 0;
    int16_t pitch = 0;
    if (info.pitch_min != 0 || info.pitch_max != 0) {
        pitch = static_cast<int16_t>(info.pitch_min + static_cast<int32_t>(Rand_Rand(info.pitch_max - info.pitch_min)));
    }
    info.last = now;
    _VECTOR p;
    Vec_Zero(&p);
    if (pos != nullptr) {
        p = *pos;
    } else if (dcv != nullptr) {
        p = dcv->obj->pos;
    }
    if (dcv == nullptr) {
        ++g_drone_vision_guards;  // port: o original leria o id da máquina de um DCVars nulo
        return 0;
    }
    const uint32_t handle =
        Sound_Play3D(info.sfx, &p, info.volume, info.range_a * scale, info.range_b * scale, pitch, dcv->sm->id, 0);
    if (handle != 0) {
        info.last = now;
    } else {
        sfx = 0;
    }
    dcv->drone->sfx_time = now;
    NPCGLOBALS_tag* n = &NPCGlobals;
    n->sfx_log[n->sfx_log_next].sfx = sfx;
    n->sfx_log[n->sfx_log_next].handle = handle;
    if (0xf < ++n->sfx_log_next) n->sfx_log_next = 0;
    return handle;
}

// [decomp] 0x00141b00 NDrone2_SpeechSFX
// Fala do personagem (fora do multijogador, respeitando o intervalo da entrada). Com face e um
// script de rosto na entrada: se o script tem som, guarda-o para tocar em 20 quadros (talk_pending);
// senão toca o script no rosto. Sem rosto (ou sem script e sem need_face): o som, se o personagem
// não estiver falando (ou com force).
uint32_t NDrone2_SpeechSFX(DCVars_tag* dcv, uint32_t sfx, int8_t force, int8_t face, int8_t need_face) {
    if (bots_on()) return 0;
    Drone_tag* d = dcv->drone;
    const uint32_t now_frame = GameState.frame;
    if (float(now_frame) < float(NPCGlobals.postload_frame) + FRAME_RATE_DIV * 30.0f) return 0;
    if (sfx >= kDroneSfxCount) {
        ++g_drone_vision_guards;  // port: o original leria a entrada de um ponteiro nulo
        return 0;
    }
    DroneSfxInfo& info = Drone_SfxInfo[sfx];
    const uint32_t now = GameState.frame;
    if (now < fptoui(float(info.last) + float(info.interval) * FRAME_RATE_DIV)) return 0;
    bool plain = face == 0;
    if (!plain) {
        uint32_t script = info.script;
        if (script == 0) {
            if (need_face == 0) plain = true;
        } else if (hashtable_getitem(script) == nullptr) {
            info.script = 0;
            plain = true;
        } else {
            script = info.script;
        }
        if (!plain) {
            Drone_tag* t = dcv->drone;
            if (script != 0 && t->face_playing == 0 && t->talk_pending == 0) {
                const uint16_t peek = AnimScriptPeekSfx(script);
                if (peek != 0) {
                    t->talk_script = script;
                    t->talk_pending = 1;
                    t->talk_time = GameState.frame + 0x14;
                    SFXPreCache(peek, 0x14);
                } else {
                    Drone_tag* u = dcv->drone;
                    ANIMSCRIPT_tag* fs = AnimFacialAppend(dcv->obj, script);
                    u->face_script = fs;
                    if (fs != nullptr) {
                        u->face_done = 0;
                        u->face_playing = 1;
                        fs->done_flag = &u->face_done;
                        u->face_script->owner = &u->face_script;
                        u->face_script->flags &= 0x7fffffffu;
                        u->face_script->end_mode = 3;
                    }
                    u->talk_pending = 0;
                }
            }
            if (d->face_script != nullptr) info.last = now;
            return 0;
        }
    }
    if (info.sfx == 0 || info.script != 0) return 0;
    if (force == 0 && d->sound != 0) return 0;
    const uint32_t handle = NDrone2_PlaySFX(dcv, sfx, &dcv->obj->pos, 0, 2.0f);
    d->sound = handle;
    return handle;
}

// [decomp] 0x00149d58 NDrone2_SpeechSFXd
void NDrone2_SpeechSFXd(Drone_tag* drone, uint32_t sfx, int8_t force, int8_t face, int8_t need_face) {
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(drone->obj, &dcv);
    NDrone2_SpeechSFX(&dcv, sfx, force, face, need_face);
}

// [decomp] 0x001c6ca8 AnimScriptPeekSfx
// O primeiro som (comando 1) de um script de animação 06xxxxxx, ou 0 (sem script, sem som ou com um
// comando desconhecido antes).
uint16_t AnimScriptPeekSfx(uint32_t script) {
    const auto* data = static_cast<const uint8_t*>(hashtable_getitem(script));
    if (data == nullptr) return 0;
    uint16_t count;
    std::memcpy(&count, data + 6, 2);
    const uint32_t n = count & 0x1ff;
    const uint8_t* p = data + 8;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t cmd;
        std::memcpy(&cmd, p, 2);
        const uint32_t kind = cmd & 0xff;
        const uint32_t len = cmd >> 8;
        if (kind == 1) {
            uint16_t sfx;
            std::memcpy(&sfx, p + 4, 2);
            return sfx;
        }
        uint32_t words;
        if (kind == 0) {
            words = len + 3;
        } else {
            if (4 < kind) return 0;
            words = len + 2;
        }
        p += words * 2 + 2;
    }
    return 0;
}
