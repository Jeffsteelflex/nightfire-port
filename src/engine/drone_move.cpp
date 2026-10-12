// Port do corpo dos personagens da IA: movimento (NDrone2_Move, o desvio de obstáculos de
// NDrone2_Steer e os DroneMove_*), colisão e gravidade (NDrone2_Collision, Drone_CollisionHandler,
// Drone_FeetOnPoint) e o olhar (cabeça e mira, NDrone2_DoTracking). Ver drone.h.
//
// Como o personagem anda: a animação move a raiz (DroneAnim_PreTransCallback) e NDrone2_Move só gira
// o personagem para o alvo do movimento (Drone_tag::move_angles.y, os ângulos até o ponto seguinte da
// rota), à taxa Drone_tag::unknown_4a0 por tick. NDrone2_Steer prevê, para cada outro personagem a
// menos de 3 m e para o jogador, quando as duas esferas (raio 0,404; 0,475 o jogador) se tocariam
// andando em linha reta (SphereSphereSweep) e soma um giro para longe de cada um, mais urgente
// quanto mais perto no tempo. Os tiros empurram o personagem (Drone_tag::push_offset, somado à
// posição e reduzido a 95% por tick). DroneMove_SetBoundryFlags testa meio metro em cada direção
// com NDrone2_MoveTest para que a animação não atravesse limites da rede de navegação.
//
// Colisão: NDrone2_Collision monta a cápsula (HITTEST_tag 0x800) do personagem em Drone_tag::hit_test
// (raio 0,4, do quadril para baixo até o raio acima dos pés; morto, ao longo do corpo) para o
// Collide_Update. Depois dele, Drone_CollisionHandler (control_funcs[2].collide) procura o chão
// (Drone_FeetOnPoint), aplica o empurrão da cápsula, trata cada acerto (NDrone2_DealWithObjHit, em
// drone_damage.cpp), aplica a gravidade (Drone_tag::fall, WldGravity) fora do chão e, no chão, puxa o
// personagem 1/8 da distância até a altura certa por tick. Raios e esferas (tiros, o raio dos pés)
// acertam as caixas das juntas de AnimGetCollData (módulo de animação, portado aqui), e o "lugar" do
// acerto (HITDATA_tag::unknown_52) é o osso, usado pelo dano.
//
// Peculiaridade mantida: o raio dos pés aceita outro personagem como chão (Drone_FeetOnPoint); dois
// personagens ativos no mesmo lugar sobem um no outro, 1/8 da altura do quadril por tick. No jogo os
// que nascem juntos ficam desligados (NDrone2_Enable(0) no estado WaitSwitch) até o canal ligar.
//
// Divergências intencionais (todas por memória sem valor no original):
// - Os w que o original deixa com lixo da pilha (vetores montados na pilha e somados com vadd.xyzw:
//   a cápsula, o empurrão da gravidade, o alvo do astronauta, a flutuação sem gravidade) ficam como o
//   w da posição ou 0.
// - NDrone2_Steer: as duas ObstacleEval_tag começam zeradas (no original, as flags do obstáculo e o w
//   das velocidades vêm da pilha). Como no original, as flags do obstáculo não voltam a zero entre um
//   obstáculo e o seguinte: depois de um parado, os outros também contam como parados.
// - Drone_FeetOnPoint: Coll_AddHitToList pode devolver nulo (pilha de acertos cheia); o original
//   escreveria nele.
// - DroneMove_NextRouteAngle sem rota (Drone_tag::active_route nulo) devolve 0 em vez de ler o endereço 0.
// - AnimGetCollData para em 16 caixas (o tamanho de HitList2); as tabelas do jogo terminam antes.

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/fmath.h"

_VECTOR DroneReachCheckPos;

namespace {

constexpr float kHalfPi = 1.5707964f;

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}

uint32_t bot_word(const Drone_tag* d, uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(d->bot) + offset, 4);
    return v;
}

_VECTOR sub4(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
// a + v * k em x, y e z (vmulx.xyz seguido de vadd); o w fica o de a.
_VECTOR madd3(const _VECTOR& a, const _VECTOR& v, float k) {
    return {a.x + v.x * k, a.y + v.y * k, a.z + v.z * k, a.w};
}

// A frente do personagem com a virada da animação (bits 8/0x10/0x20 de Drone_tag::anim_flags).
float facing_with_turn(const Drone_tag* d, float yaw) {
    const uint32_t flags = d->anim_flags;
    if ((flags & 8) != 0) yaw = yaw + 1.5707964f;
    if ((flags & 0x10) != 0) yaw = yaw + 3.1415927f;
    if ((flags & 0x20) != 0) yaw = yaw - 1.5707964f;
    return yaw;
}

// Outro personagem que conta como obstáculo: ativo (0x100), vivo e não morto (0x600), não do tipo
// 0x11 nem marcado para remoção.
bool is_obstacle(const obj_tag* o) {
    if (o == nullptr) return false;
    const Drone_tag* od = static_cast<const Drone_tag*>(o->data);
    const uint32_t f = od->flags;
    if ((f & 0x600) != 0 || (f & 0x100) == 0) return false;
    if (od->health <= 0.0f) return false;
    if (o->type == 0x11 || (o->life & 1) != 0) return false;
    return true;
}

}  // namespace

// ---- desvio de obstáculos ----

// [decomp] 0x0014e5b8 SphereSphereSweep
// Duas esferas andando em linha reta no plano (x, z) de p1 a p1_end e de p2 a p2_end: os dois
// instantes (0 a 1 = o trecho) em que a distância entre os centros é a soma dos raios. Já encostadas:
// os dois são 0. Sem encontro (discriminante negativo): 0. Com o primeiro maior que o segundo, os dois
// recebem o segundo.
uint32_t SphereSphereSweep(float r1, const _VECTOR& p1, const _VECTOR& p1_end, float r2, const _VECTOR& p2,
                           const _VECTOR& p2_end, float& t_first, float& t_second) {
    const float rr = (r1 + r2) * (r1 + r2);
    const float ax = p2.x - p1.x, az = p2.z - p1.z;
    const float bx = p1_end.x - p1.x, bz = p1_end.z - p1.z;
    const float cx = p2_end.x - p2.x, cz = p2_end.z - p2.z;
    const float dx = cx - bx, dz = cz - bz;
    const float a = ax * ax + az * az;
    const float b2 = dx * ax + dz * az;
    const float b = b2 + b2;
    const float c = dx * dx + dz * dz;
    if (a <= rr) {
        t_first = 0.0f;
        t_second = 0.0f;
        return 1;
    }
    const float disc = b * b - c * ((a - rr) * 4.0f);
    if (disc == 0.0f) {
        t_first = 0.0f;
        t_second = 0.0f;
    } else if (0.0f < disc) {
        const float s = std::sqrt(disc);
        const float inv = 1.0f / (c + c);
        t_first = (-b + s) * inv;
        t_second = (-b - s) * inv;
    } else {
        return 0;
    }
    if (t_second < t_first) {
        t_first = t_second;
        t_second = t_second;
    }
    return 1;
}

// [decomp] 0x0014e6f0 NDrone2_EvaluateObstacle
// Um obstáculo (eval->other) para o personagem que anda (eval): guarda o personagem mais perto, e se
// estão a até 3 m e as esferas se encontram andando um tick à frente, quando (eval->t, em ticks) e
// para que lado desviar (eval->steer: até pi/2 vezes a parte do desvio que cabe a este, menor quanto
// mais longe no tempo, com o sinal do lado em que o obstáculo está). Devolve 1 se há desvio.
uint32_t NDrone2_EvaluateObstacle(Drone_tag* drone, Drone_tag* other, ObstacleEval_tag* e) {
    constexpr float kHorizon = 7030.0f;
    ObstacleEval_tag* o = e->other;
    _VECTOR d{e->pos->x - o->pos->x, e->pos->y - o->pos->y, e->pos->z - o->pos->z, 0.0f};
    const float dist = Vec_Magnitude(&d);
    if (other != nullptr && dist < drone->nearest_dist) {
        drone->nearest = other;
        drone->nearest_dist = dist;
    }
    const _VECTOR a{e->pos->x + e->vel.x, 0.0f, e->pos->z + e->vel.z, 0.0f};
    if (9.0f < Vec_SqDist3D(e->pos, o->pos)) return 0;
    _VECTOR b;
    if (0.005f < o->speed) {
        o->vel.x = PS2Sinf(o->rot->y) * o->speed;
        o->vel.y = 0.0f;
        o->vel.z = PS2Sinf(o->rot->y + kHalfPi) * o->speed;
        b = {o->pos->x + o->vel.x, 0.0f, o->pos->z + o->vel.z, 0.0f};
    } else {
        b = {o->pos->x, 0.0f, o->pos->z, 0.0f};
        o->flags |= 2;
    }
    float t_second = 0.0f;
    if (SphereSphereSweep(e->radius, *e->pos, a, o->radius, *o->pos, b, e->t, t_second) == 0) return 0;
    if ((o->flags & 2) == 0) {
        e->limit = e->speed - o->speed * PS2Sinf(Vec_AngleDifference(o->rot->y, e->rot->y) + kHalfPi);
    } else {
        e->limit = e->speed;
    }
    const float t = e->t;
    if (t < 0.0f) return 0;
    if (e->limit * kHorizon <= t) return 0;
    float steer;
    float angle;
    if (t == 0.0f) {
        e->contact = *e->pos;
        o->contact = *o->pos;
        e->radius_sum = e->radius + o->radius;
        e->delta = sub4(o->contact, e->contact);
        e->delta.y = 0.0f;
        angle = Vec_AngleDifference(nf::fm::atan2(e->delta.x, e->delta.z), e->rot->y);
        e->dist = 0.0f;
        e->angle = angle;
        const float share = e->speed / (e->speed + o->speed);
        const float weight = 1.0f - std::fabs(angle) * 0.31830987f;
        e->weight = weight;
        e->share = share;
        steer = weight * (share * kHalfPi);
        e->steer = steer;
    } else {
        e->contact = {e->pos->x + e->vel.x * t, e->pos->y + e->vel.y * t, e->pos->z + e->vel.z * t,
                      e->pos->w + e->vel.w};
        if ((o->flags & 2) != 0) {
            o->contact = *o->pos;
        } else {
            o->contact = {o->pos->x + o->vel.x * t, o->pos->y + o->vel.y * t, o->pos->z + o->vel.z * t,
                          o->pos->w + o->vel.w};
        }
        e->radius_sum = e->radius + o->radius;
        e->delta = sub4(o->contact, e->contact);
        e->delta.y = 0.0f;
        e->dist = Vec_Magnitude(&e->delta);
        const float share = e->speed / (e->speed + o->speed);
        const float weight = 1.0f - t / (e->limit * kHorizon);
        e->share = share;
        e->weight = weight;
        steer = weight * (share * kHalfPi);
        e->steer = steer;
        angle = Vec_AngleDifference(nf::fm::atan2(e->delta.x, e->delta.z), e->rot->y);
        e->angle = angle;
    }
    if (angle < 0.0f) e->steer = -steer;
    return 1;
}

// [decomp] 0x0014eba8 NDrone2_Steer
// Gira move_rot.y para o alvo (angle à taxa unknown_4a0) desviando dos outros personagens e do jogador:
// com obstáculos, o giro é a média entre o pedido (com peso 2) e o desvio de cada um. Também acha o
// personagem mais perto (até 3 m) para DroneMove_NoBunching. A classe 0xb e quem não anda (move_speed
// zero) só giram.
void NDrone2_Steer(DCVars_tag* dcv, float angle) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (d->klass == 0xb || d->move_speed == 0.0f) {
        const float turn = angle * d->unknown_4a0;
        d->nearest = nullptr;
        d->nearest_dist = 3.0f;
        d->unknown_318 = nullptr;
        d->move_rot.y = d->move_rot.y + turn;
        for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            Drone_tag* other = reinterpret_cast<Drone_tag*>(n);
            if (other == d || !is_obstacle(other->obj)) continue;
            const float dist = Vec_Dist3D(&obj->pos, &other->obj->pos);
            if (dist < d->nearest_dist) {
                d->nearest_dist = dist;
                d->nearest = other;
            }
        }
        return;
    }
    constexpr float kDroneRadius = 0.40400001f;   // 0x3eced917
    constexpr float kPlayerRadius = 0.47500002f;  // 0x3ef33334
    ObstacleEval_tag self{};
    ObstacleEval_tag other_eval{};
    self.pos = &obj->pos;
    self.rot = &d->move_rot;
    self.speed = d->moved;
    self.radius = kDroneRadius;
    self.vel.x = PS2Sinf(d->move_rot.y) * self.speed;
    self.vel.y = 0.0f;
    self.vel.z = PS2Sinf(d->move_rot.y + kHalfPi) * self.speed;
    self.other = &other_eval;
    d->nearest_dist = 3.0f;
    d->nearest = nullptr;
    d->unknown_318 = nullptr;
    float sum = 0.0f;
    int32_t count = 1;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* other = reinterpret_cast<Drone_tag*>(n);
        if (other == d || !is_obstacle(other->obj)) continue;
        other_eval.rot = &other->move_rot;
        other_eval.pos = &other->obj->pos;
        self.flags = other->sm.id < d->sm.id ? 1 : 0;
        other_eval.speed = other->moved;
        other_eval.radius = kDroneRadius;
        if (NDrone2_EvaluateObstacle(d, other, &self) != 0) {
            ++count;
            sum = sum + self.steer;
        }
    }
    const auto avoid_player = [&](obj_tag* pl) {
        other_eval.rot = &pl->rot;
        other_eval.pos = &pl->pos;
        other_eval.radius = kPlayerRadius;
        self.flags = 0;
        other_eval.speed = 0.0f;
        if (NDrone2_EvaluateObstacle(d, nullptr, &self) != 0) {
            ++count;
            d->avoiding_player = 1;
            sum = sum + self.steer;
        }
    };
    if (!bots_on()) {
        obj_tag* pl = NDrone2_Player();
        if (pl != nullptr) avoid_player(pl);
    } else {
        NDrone2_Player();  // (o resultado não é usado)
        for (uint32_t i = 0; i < mp_word(0x1ac); ++i) {
            obj_tag* pl;
            pl = nf::p32_load<obj_tag>(MPGame + 0x1c + i * 0x30);
            if (pl != nullptr) avoid_player(pl);
        }
    }
    if (count < 2) {
        d->move_rot.y = d->move_rot.y + angle * d->unknown_4a0;
        return;
    }
    d->move_rot.y = d->move_rot.y + ((sum + (angle + angle)) / float(count + 1)) * d->unknown_4a0;
}

// [decomp] 0x0014eff8 NDrone2_Move
// Com gravidade: gira para o alvo do movimento (a não ser com control_flags 0x8000000). Sem gravidade e
// com float_to_target: acelera para o alvo (unknown_50 m/s² em Drone_tag::fall) e gira o objeto para
// ele. Nos dois casos, troca o empurrão dos tiros do tick anterior pelo atual na posição e reduz o atual
// a 95%.
void NDrone2_Move(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    const float target_yaw = d->move_angles.y;
    d->avoiding_player = 0;
    if (d->zero_g == 0) {
        if ((dcv->drone->control_flags & 0x8000000) == 0) {
            NDrone2_Steer(dcv, Vec_AngleDifference(d->move_rot.y, target_yaw));
        }
    } else if (d->float_to_target != 0) {
        _VECTOR dir = sub4(d->move_target.pos, obj->pos);
        Vec_Normalise(&dir, &dir);
        d->fall = madd3(d->fall, dir, REC_FRAME_RATE * d->unknown_50);
        obj->rot.y = obj->rot.y + Vec_AngleDifference(obj->rot.y, target_yaw) * (0.1f / FRAME_RATE_DIV);
    }
    obj->pos = sub4(obj->pos, d->prev_push_offset);
    obj->pos = add4(obj->pos, d->push_offset);
    d->prev_push_offset = d->push_offset;
    Vec_MulR32(&d->push_offset, &d->push_offset, 0.95f);
}

// [decomp] 0x0014f140 DroneMove_SetBoundryFlags
// Com flags bit 0: testa (NDrone2_MoveTest) um passo de meio metro dos pés para a frente, para trás e
// para os lados no espaço do objeto e marca em boundary_flags os lados bloqueados (2 frente, 4 trás,
// 8 esquerda, 0x10 direita). Com flags 0x10 todos ficam bloqueados.
void DroneMove_SetBoundryFlags(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    d->boundary_flags = 0;
    if ((dcv->drone->flags & 1) == 0) return;
    const auto can_step = [&](float x, float z) {
        if (d != nullptr && (d->flags & 0x10) != 0) return false;
        _VECTOR off{x, 0.0f, z, 0.0f};
        CelPos_tag to{};
        NDrone2_FeetPos(obj, &to.pos);
        if (d->type == 0x1e) {
            const float k = BOT_getMovementSpeedMul(d);
            Vec_MulR32(&off, &off, k, 1.0f, k);
        }
        ApplyMatrixLVI(&obj->matrix, &off);
        to.pos = add4(off, to.pos);
        to.cel = build_FindCel(&to.pos, glb_world);
        if (d != nullptr && (d->flags & 0x10) != 0) return false;
        CelPos_tag from{};
        NDrone2_FeetPos(obj, &from.pos);
        from.cel = obj->cel;
        if (to.cel == nullptr) to.cel = NDrone2_FindCel(&to.pos);
        DroneReachCheckPos = to.pos;
        return NDrone2_MoveTest(&from, &to, nullptr, obj, nullptr, 0, 0) == 1;
    };
    if (!can_step(0.0f, 0.5f)) d->boundary_flags |= 2;
    if (!can_step(0.0f, -0.5f)) d->boundary_flags |= 4;
    if (!can_step(0.5f, 0.0f)) d->boundary_flags |= 8;
    if (!can_step(-0.5f, 0.0f)) d->boundary_flags |= 0x10;
}

// [decomp] 0x00150ad0 DroneMove_NoBunching
// Para que lado está o personagem mais perto (Drone_tag::nearest, de NDrone2_Steer) em relação à frente:
// bunch_flags recebe 0x10 (direita) ou 8 (esquerda) e 2 (a menos de 45 graus da frente) ou 4. Devolve 1
// se havia alguém (que não esteja com flags 0x10).
uint8_t DroneMove_NoBunching(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    Drone_tag* n = d->nearest;
    if (n == nullptr) return 0;
    if ((n->flags & 0x10) != 0) return 0;
    const _VECTOR delta = sub4(n->obj->pos, obj->pos);
    const float yaw = ATAN2_APPROX(delta.x, delta.z);
    const float diff = Vec_AngleDifference(facing_with_turn(d, obj->rot.y), yaw);
    d->bunch_flags |= diff < 0.0f ? 0x10 : 8;
    d->bunch_flags |= std::fabs(diff) < 0.7853982f ? 2 : 4;
    return 1;
}

// ---- rotas e fuga ----

// [decomp] 0x00155ad0 NDrone2_CanReachGoalPosition
// O destino guardado (goal_point) é alcançável: NDrone2_MoveToGoalPosition devolve 0 a 3.
uint32_t NDrone2_CanReachGoalPosition(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    return NDrone2_MoveToGoalPosition(dcv, &d->goal_point.pos, d->goal_point.radius, 0) <= 3 ? 1 : 0;
}

// [decomp] 0x00152250 DroneMove_FindSafetyFromScaryPosition
// Escolhe para onde fugir de pos: com a rede de navegação, o nó a dist (pela distância de rede a pos,
// emissor NPCGlobals.emitter até dist x 1,8) que o personagem alcança; senão (ou se não alcança), o
// ponto a dist de pos na direção oposta, até onde os limites deixam. Grava o destino em goal_point e
// zera a rota principal; o estado inicial vira explosive_state (ou return_state) e return_state guarda
// return_state.
uint32_t DroneMove_FindSafetyFromScaryPosition(DCVars_tag* dcv, _VECTOR* pos, obj_tag* obj, float dist) {
    Drone_tag* d = dcv->drone;
    obj_tag* me = dcv->obj;
    AIEmitter_tag* em = &NPCGlobals.emitter;
    bool network = false;
    if (AINetwork_InitEmitter2(obj, pos, obj != nullptr ? obj->cel : nullptr, em, d->route.path, 0) != 0 &&
        d->route.path == em->path && AINetwork_EmitPath(em, dist * 1.8f) != 0) {
        CelPos_tag here{};
        here.pos = dcv->obj->pos;
        here.cel = dcv->obj->cel;
        const int16_t start = NDrone2_NearestNode(&here, d->route.path, dcv->obj);
        MAPNODE_tag* node = nullptr;
        if (AINetwork_Emitter_GetNodeAtDistance(start, dist, em, &node) != 0 && node != nullptr) {
            CelPos_tag goal{};
            goal.pos = node->pos;
            goal.cel = node->cel;
            AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, &goal, 1.0f, 0);
            AINetwork_ResetRoute(&d->route, me, nullptr);
            AINetwork_InvalidateRoute(&d->route);
            network = NDrone2_CanReachGoalPosition(dcv) != 0;
        }
    }
    if (!network) {
        _VECTOR away = sub4(me->pos, *pos);
        Vec_Normalise(&away, &away);
        CelPos_tag goal{};
        goal.pos = madd3(*pos, away, dist);
        goal.pos.y = me->pos.y + 0.5f;
        goal.cel = build_FindCel(&goal.pos, glb_world);
        AINetwork_FurthestPosition(&me->pos, &goal.pos, &goal.pos);
        goal.pos.y = pos->y + 0.5f;
        AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &goal, 1.0f, 0);
        AINetwork_ResetRoute(&d->route, me, nullptr);
        AINetwork_InvalidateRoute(&d->route);
    }
    if (d->explosive_state != 0) {
        dcv->drone->initial_state = static_cast<uint16_t>(d->explosive_state);
    } else {
        dcv->drone->initial_state = static_cast<uint16_t>(dcv->sm->return_state);
    }
    dcv->drone->return_state = static_cast<uint16_t>(dcv->sm->return_state);
    return 1;
}

// [decomp] 0x001559f8 DroneMove_FindSafetyFromScaryObject
bool DroneMove_FindSafetyFromScaryObject(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->explosive_seen == 0) return false;
    return DroneMove_FindSafetyFromScaryPosition(dcv, &d->explosive_pos, nullptr, d->explosive_radius) != 0;
}

// [decomp] 0x00153f50 DroneMove_ObjectMissionPathDistance
// Distância, pela rota de missão (route2), do nó atual até o nó dela mais perto de obj (dentro de
// radius, alcançável dos pés de obj em linha reta); 0 sem nó.
float DroneMove_ObjectMissionPathDistance(DCVars_tag* dcv, obj_tag* obj, float radius) {
    Drone_tag* d = dcv->drone;
    AIRoute_tag* route = &d->route2;
    float best_sq = radius * radius;
    uint32_t best = 0xffff;
    CelPos_tag from{};
    NDrone2_FeetPos(obj, &from.pos);
    from.cel = obj->cel;
    for (uint32_t i = 0; i < route->count; ++i) {
        MAPNODE_tag* node = &route->path->map_nodes[route->list[i]];
        const float sq = Vec_SqDist2D(&node->pos, &obj->pos);
        if (best != 0xffff && !(sq < best_sq)) continue;
        if (NDrone2_MoveTestToNode(&from, node, nullptr, nullptr, nullptr, 0, 0) == 1) {
            best_sq = sq;
            best = i;
        }
    }
    if (best == 0xffff) return 0.0f;
    return AINetwork_GetRouteDistanceBetweenNodes(route, route->current, static_cast<int16_t>(best));
}

// [decomp] 0x00154568 DroneMove_NextRouteAngle
// Ângulo entre a direção alvo -> personagem e a da ligação em que a rota anda (0 abaixo de ~1 grau).
float DroneMove_NextRouteAngle(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    _VECTOR a = sub4(dcv->obj->pos, d->move_target.pos);
    AIRoute_tag* route = d->active_route;
    if (route == nullptr) {
        ++g_drone_guards;  // port: o original leria o endereço 0
        return 0.0f;
    }
    _VECTOR b = sub4(route->creep_end, route->creep_origin);
    Vec_Normalise(&a, &a);
    Vec_Normalise(&b, &b);
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (0.9998f < dot) return 0.0f;
    return nf::fm::acos(dot);
}

// [decomp] 0x00154cb0 DroneMove_TurnToObj
// Vira o alvo do giro para obj (mais offset) se a diferença passar de tolerance.
void DroneMove_TurnToObj(DCVars_tag* dcv, obj_tag* obj, float offset, float tolerance) {
    Drone_tag* d = dcv->drone;
    if (obj == nullptr || (d->control_flags & 0x8000000) != 0 || (d->anim_flags & 0x380) != 0) return;
    const _VECTOR delta = sub4(obj->pos, dcv->obj->pos);
    const float yaw = ATAN2_APPROX(delta.x, delta.z);
    if (tolerance < std::fabs(Vec_AngleDifference(offset + yaw, d->move_angles.y))) d->move_angles.y = offset + yaw;
}

// [decomp] 0x00155e90 DroneMove_DistanceToNextRouteNode
float DroneMove_DistanceToNextRouteNode(DCVars_tag* dcv) { return dcv->drone->move_dist; }

// [decomp] 0x00154618 DroneMove_AstronautCombat
// Astronauta (sem gravidade) em combate: vira para o oponente e, vencido o prazo, sorteia um ponto a
// 10-20 m dele dentro de um volume de IA do tipo 3 para flutuar até lá (estado 0xc1, novo prazo de 2 a
// 7 segundos); chegando a 3 m do ponto, estado 0xc0.
void DroneMove_AstronautCombat(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (d->opponent == nullptr) return;
    if ((d->control_flags & 0x8000000) == 0) {
        const bool frozen = bots_on() && (bot_word(d, 0x734) & 2) != 0;
        if (!frozen && (d->anim_flags & 0x380) == 0) {
            const _VECTOR delta = sub4(d->opponent->pos, obj->pos);
            dcv->drone->move_angles.y = ATAN2_APPROX(delta.x, delta.z);
        }
    }
    d->firing = static_cast<uint8_t>((dcv->drone->sight_flags >> 2) & 1);
    if (d->state_a + d->state_b < GameState.frame) {
        _VECTOR p{};
        p.x = Rand_FRand(2.0f) - 1.0f;
        p.y = Rand_FRand(2.0f) - 1.0f;
        p.z = Rand_FRand(2.0f) - 1.0f;
        Vec_Normalise(&p, &p);
        Vec_MulR32(&p, &p, Rand_FRand(10.0f) + 10.0f);
        p = add4(d->opponent->pos, p);
        if (Drone_PointInAnyAIBox(&p, 3) != 0) {
            d->float_to_target = 1;
            d->move_target.pos = p;
            const uint32_t range = FRAME_RATE_INT * 5;
            d->state_a = GameState.frame;
            d->state_b = FRAME_RATE_INT * 2 + Rand_Rand(range);
            Drone_SM_SetState(dcv->sm, 0xc1, 0);
        }
    } else if (Vec_Dist3D(&obj->pos, &d->move_target.pos) < 3.0f) {
        Drone_SM_SetState(dcv->sm, 0xc0, 0);
    }
}

// [decomp] 0x00155ea0 DroneMove_AstronautAnim
// Animação de flutuar conforme o lado do alvo: 0x72 à frente, 0x71 atrás, 0x73/0x74 esquerda/direita.
void DroneMove_AstronautAnim(DCVars_tag* dcv) {
    obj_tag* obj = dcv->obj;
    const _VECTOR delta = sub4(dcv->drone->move_target.pos, obj->pos);
    const float diff = Vec_AngleDifference(obj->rot.y, ATAN2_APPROX(delta.x, delta.z));
    uint32_t call = 0x72;
    if (0.7853982f <= std::fabs(diff)) {
        call = 0x71;
        if (std::fabs(diff) <= 2.3561945f) call = 0.0f < diff ? 0x73 : 0x74;
    }
    DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
}

// [decomp] 0x00155f78 DroneMove_AstronautHitMove
// Atingido sem gravidade: empurra 5 m/s na direção do acerto (ou para longe do oponente).
void DroneMove_AstronautHitMove(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    _VECTOR v;
    if (d->last_hit == nullptr) {
        v = d->opp_delta;
        Vec_Normalise(&v, &v);
        Vec_MulR32(&v, &v, -5.0f);
    } else {
        v = d->last_hit->dir;
        Vec_Normalise(&v, &v);
        Vec_MulR32(&v, &v, 5.0f);
    }
    d->float_to_target = 0;
    d->fall = add4(d->fall, v);
}

// [decomp] 0x00156008 DroneMove_AstronautDeathMove
// Morto sem gravidade: empurra 5 m/s para longe do oponente e desliga o amortecimento (flags 0x40).
void DroneMove_AstronautDeathMove(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    _VECTOR v = d->opp_delta;
    Vec_Normalise(&v, &v);
    Vec_MulR32(&v, &v, -5.0f);
    d->fall = add4(d->fall, v);
    dcv->drone->flags &= ~0x40u;
    d->float_to_target = 0;
}

// ---- colisão e gravidade ----

AnimCollBone HumanCollisionBones[kAnimCollBones];
AnimCollBone AstronautCollisionBones[kAnimCollBones];
COLLJOINT_tag HitList2[kAnimCollBones - 1];

// [decomp] 0x001c4ac8 AnimGetCollData
// As caixas das juntas de um objeto animado de personagem (anim_flags 1 e 2), no mundo, para os raios
// e esferas de Collide_Jointy: cada uma com a orientação do osso other, o centro entre os dois ossos
// (a primeira, a cabeça, fica acima do osso pela distância até o pai) e as meias medidas da tabela
// (a do astronauta para a pele 05000078). Devolve HitList2 e a contagem.
COLLJOINT_tag* AnimGetCollData(obj_tag* obj, uint16_t* count) {
    ANIMOBJ_tag* a = obj != nullptr ? anim_of(obj) : nullptr;
    if (a == nullptr || (a->anim_flags & 1) == 0 || (a->anim_flags & 2) == 0) {
        *count = 0;
        return nullptr;
    }
    psiBuildMatrixPalette(obj, anim_state(a), 0);
    const ANIMSKIN_tag* skin = a->skin;
    const AnimCollBone* t = skin->hash == 0x5000078 ? AstronautCollisionBones : HumanCollisionBones;
    uint16_t n = 0;
    // Port: no máximo as 16 caixas de HitList2 (sem a cópia do ELF a tabela não teria o fim).
    for (; t->bone >= 0 && n < kAnimCollBones - 1; ++t) {
        const int32_t bone = t->bone;
        int32_t other = t->other;
        if (other < 0) other = skin->parents[bone] & 0x7f;
        const MATRIX& mo = a->bones[other];
        const MATRIX& mb = a->bones[bone];
        COLLJOINT_tag& j = HitList2[n];
        Mat_Copy(&mo, &j.m);
        if (n == 0) {
            const float dy = mb.m[3][1] - mo.m[3][1];
            const float dw = mb.m[3][3] - mo.m[3][3];
            j.m.m[3][0] = mb.m[3][0] + 0.0f;
            j.m.m[3][1] = mb.m[3][1] + dy;
            j.m.m[3][2] = mb.m[3][2] + 0.0f;
            j.m.m[3][3] = mb.m[3][3] + dw;
        } else {
            j.m.m[3][0] = (mo.m[3][0] + mb.m[3][0]) * 0.5f;
            j.m.m[3][1] = (mo.m[3][1] + mb.m[3][1]) * 0.5f;
            j.m.m[3][2] = (mo.m[3][2] + mb.m[3][2]) * 0.5f;
        }
        j.half.x = t->half[0] * skin->scale[0];
        j.half.y = t->half[1] * skin->scale[1];
        j.half.z = t->half[2] * skin->scale[2];
        j.joint = static_cast<uint16_t>(bone);
        ++n;
    }
    *count = n;
    return HitList2;
}

// [decomp] 0x0014b190 DroneFunc_NearPlayerCollision
// Algum jogador a menos de 2 m (Drone_tag::player_dist).
uint32_t DroneFunc_NearPlayerCollision(Drone_tag* drone) {
    for (int i = 0; i < 4; ++i) {
        if (glb_players[i] != nullptr && drone->player_dist[i] < 2.0f) return 1;
    }
    return 0;
}

// [decomp] 0x00149ec8 NDrone2_DoCollision
// Se a cápsula colide neste tick: vivo (ou qualquer um em 0700004a) com flags 0x20; a classe 0xc
// sempre; as outras com a classe 0x13, saindo de uma animação, perto de um jogador ou fora do chão
// (obj +0xfc), e não se a DAnim anda sem colidir (anim_flags 2).
uint32_t NDrone2_DoCollision(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x200) != 0 && GameState.level_file != 0x700004a) return 0;
    if ((dcv->drone->flags & 0x20) == 0) return 0;
    if (d->klass != 0xc) {
        if (d->klass == 0x13) return 1;
        if (dcv->drone->blend_script != nullptr) return 1;
        if (DroneFunc_NearPlayerCollision(d) != 0) return 1;
        if (dcv->obj->unknown_fc != 0) return 1;
        if ((dcv->drone->anim_flags & 2) != 0) return 0;
    }
    return 1;
}

// [decomp] 0x00141e58 NDrone2_DoGravity
// Se a gravidade age: flags 0x80; com 4 sempre; morto não (fora de 0700004a); com 0x20, nas mesmas
// condições de NDrone2_DoCollision (mas com anim_flags 2 sim).
uint32_t NDrone2_DoGravity(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const uint32_t flags = d->flags;
    if ((flags & 0x80) == 0) return 0;
    if ((flags & 4) != 0) return 1;
    if ((flags & 0x200) != 0 && GameState.level_file != 0x700004a) return 0;
    if ((dcv->drone->flags & 0x20) != 0) {
        if (d->klass == 0xc || d->klass == 0x13) return 1;
        if (dcv->drone->blend_script != nullptr) return 1;
        if (DroneFunc_NearPlayerCollision(d) != 0) return 1;
        if (dcv->obj->unknown_fc != 0) return 1;
        if ((dcv->drone->anim_flags & 2) == 0) return 1;
    }
    return 0;
}

// [decomp] 0x00140e90 NDrone2_Collision
// Monta a cápsula do personagem para o Collide_Update: vivo, raio 0,4 (0,55 a classe 0xc; 0,3 com
// small_capsule, só no quadril) do quadril + raio até root_y abaixo do quadril + raio, ativa só quando
// NDrone2_DoCollision; morto, raio até 0,4 ao longo do corpo (cabeça menos osso 0x37), sempre ativa.
// Com flags 0x80000000, os limites da rede empurram o personagem para fora.
void NDrone2_Collision(obj_tag* obj) {
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(obj, &dcv);
    HITTEST_tag& t = d->hit_test;
    t.mask = 0;
    const float root = d->root_y;
    t.cel = obj->cel;
    if ((d->flags & 0x200) != 0) {
        t.radius = root < 0.4f ? root : 0.4f;
        _VECTOR head, a, c;
        AnimGetBoneWorldTrans(obj, 0x80000005, 0, &head, nullptr);
        AnimGetBoneWorldTrans(obj, 0x80000037, 0, &a, nullptr);
        AnimGetBoneWorldTrans(obj, 0x80000033, 0, &c, nullptr);
        _VECTOR axis = sub4(head, a);
        const float l1 = Vec_NormaliseLen(&axis, &axis) * 0.5f;
        const float l2 = Vec_Dist3D(&head, &c) * 0.5f;
        t.type = 0x800;
        t.unknown_90 = 0;
        const float m = l1 < l2 ? l2 : l1;
        t.start = madd3(obj->pos, axis, 0.4f);
        t.end = madd3(obj->pos, axis, 0.4f - m);
    } else {
        const float r = d->small_capsule != 0 ? 0.3f : (d->klass == 0xc ? 0.55f : 0.4f);
        t.type = NDrone2_DoCollision(&dcv) != 0 ? 0x800 : 0;
        t.radius = r;
        t.unknown_90 = 0;
        _VECTOR up;
        Mat_GetUp(&up, &obj->matrix);
        if (d->small_capsule != 0) {
            t.start = madd3(obj->pos, up, r - root);
            t.end = t.start;
        } else {
            t.start = madd3(obj->pos, up, r);
            t.end = madd3(obj->pos, up, r - root);
        }
    }
    obj->unknown_fd = 1;
    if ((d->flags & 0x80000000u) != 0) {
        _VECTOR push{};
        AINetwork_GetBoundsPushVectorForSphere(&obj->pos, obj->cel, 0.4f, &push, d->bounds_skip);
        obj->pos = add4(obj->pos, push);
    }
}

// [decomp] 0x00140660 Drone_FeetOnPoint
// Raio da posição até 0,6 abaixo do pé da cápsula. O acerto mais próximo é o chão se os pés (posição
// menos root_y) estão a menos de 0,2 dele (o jogador só conta pelas juntas 1 a 5); *height recebe a
// altura dos pés sobre o primeiro acerto, *normal_y a inclinação. No primeiro objeto pisado que não
// seja personagem, porta ou similar, o personagem herda o movimento dele e o objeto recebe um acerto
// vindo de cima. No chão (ou com a cápsula apoiada), anim flags 8 e o anim copia o acerto. Devolve o
// objeto pisado.
obj_tag* Drone_FeetOnPoint(obj_tag* obj, HITTEST_tag* test, float* normal_y, int8_t, float* height) {
    if (obj == nullptr) return nullptr;
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    P32<HITDATA_tag> hits = nullptr;
    _VECTOR up;
    Mat_GetUp(&up, &obj->matrix);
    HITDATA_tag* stand = nullptr;
    bool done = false;
    if (normal_y != nullptr) *normal_y = 1.0f;
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->floor.surface = 0;
    const _VECTOR to = madd3(test->end, up, -(d->root_y + 0.6f));
    Collide_RayIntersect(&obj->pos, &to, obj->cel, obj, nullptr, &hits, 0, 0xc, 8);
    if (hits != nullptr) {
        const float h = (obj->pos.y - d->root_y) - hits->pos.y;
        if (height != nullptr) *height = h;
        if (h < 0.2f) {
            obj_tag* o = hits->obj;
            if (o == nullptr || o != glb_players[0]) {
                stand = hits;
            } else if (hits->unknown_52 < 6 && hits->unknown_52 != 0) {
                stand = hits;
            }
            if (normal_y != nullptr) *normal_y = hits->plane.b;
        }
    }
    if (stand != nullptr && stand->obj != nullptr) {
        for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
            if (h->cel != nullptr && (h->cel->glist_flags & 0x40) != 0) continue;
            obj_tag* o = h->obj;
            if (o != nullptr && (o->flags & 0x40) != 0) continue;
            if (done) continue;
            if (o != nullptr) {
                switch (o->type) {
                    case 2:
                    case 3:
                    case 5:
                    case 0x11:
                    case 0x12:
                    case 0x1a:
                        break;
                    default:
                        Control_InheritVelocity(obj, h->obj, 3.0f);
                        anim->flags |= 0x80;
                        break;
                }
                HITDATA_tag* n = Coll_AddHitToList(&h->obj->hits, 1.0f, nullptr, obj);
                if (n != nullptr) {  // port: o original escreveria no nulo
                    n->unknown_08 = 0;
                    std::memcpy(&n->plane, &CONSTANT_UP1, sizeof(_VECTOR));
                    Vec_Negate(&CONSTANT_UP1, &n->dir);
                    n->pos = h->pos;
                }
            }
            done = true;
        }
    }
    if (test->found != 0 || stand != nullptr) anim->flags |= 8;
    if (stand != nullptr) amemcpy(anim, stand, 0x60);
    Collide_FreeHitList(&hits);
    return stand != nullptr ? stand->obj : nullptr;
}

// [decomp] 0x00140968 Drone_CollisionHandler
// Depois do Collide_Update (control_funcs[2].collide). Sem gravidade: acha o chão, aplica o empurrão
// e os acertos (NDrone2_DealWithObjHit; o tipo 0x1d quica), marca o contato no anim e anda pela
// velocidade (amortecida a 98% com flags 0x40). Com gravidade: com a cápsula ativa, o mesmo para o
// chão, o empurrão e os acertos; depois, se a gravidade age e a água não cobre (sala +0x90 abaixo da
// posição + 0,3), acelera a queda fora do chão e anda por ela. No chão, a queda vertical zera e o
// personagem desce 1/8 da altura dos pés sobre o chão.
void Drone_CollisionHandler(obj_tag* obj) {
    if (NPCGlobals.enabled == 0 || Drone_bDisableSystem != 0) {
        static_cast<Drone_tag*>(obj->data)->hit_test.type = 0;
        return;
    }
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    HITDATA_tag* best = nullptr;
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(obj, &dcv);
    float height = 0.0f;
    d->flags &= 0xffffbfffu;
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->flags &= 0xffc7;
    anim->flags &= 0xff4f;
    d->touching_player = 0;
    float below_end = 0.0f;
    // Aplica o empurrão da cápsula, trata os acertos e devolve o primeiro que empurra.
    const auto deal_hits = [&]() {
        obj->pos = add4(d->hit_test.push, obj->pos);
        for (HITDATA_tag* h = obj->hits; h != nullptr;) {
            NDrone2_DealWithObjHit(obj, d, h);
            const uint8_t flags = h->flags;
            HITDATA_tag* cand = best != nullptr ? best : h;
            h = h->next;
            if ((flags & 2) == 0) best = cand;
        }
    };
    if (d->zero_g != 0) {
        Drone_FeetOnPoint(obj, &d->hit_test, nullptr, 0, nullptr);
        if (obj->hits != nullptr) {
            deal_hits();
            if (best != nullptr) {
                if (d->type == 0x1d) {
                    anim->flags |= 0x10;
                } else {
                    anim->flags |= 0x10;
                    if (0.0f < best->pos.y - d->hit_test.start.y) anim->flags |= 0x20;
                    if (best->pos.y - d->hit_test.end.y < 0.0f) anim->flags |= 8;
                }
            }
        }
        obj->pos = madd3(obj->pos, d->fall, REC_FRAME_RATE);
        if ((d->flags & 0x40) != 0) Vec_MulR32(&d->fall, &d->fall, 0.98f);
        obj->mode |= 0x20;
        if ((anim->flags & 8) != 0 && d->type != 0x1d) d->fall.y = 0.0f;
        return;
    }
    // Gravidade: a queda acelera fora do chão; a posição anda por ela (a não ser dentro da água).
    const auto gravity = [&]() {
        DCVars_tag g{};
        Drone_DCVfromOBJ(obj, &g);
        Drone_tag* gd = static_cast<Drone_tag*>(obj->data);
        if (NDrone2_DoGravity(&g) == 0) {
            gd->fall.y = 0.0f;
            return;
        }
        if (obj->cel != nullptr && !(obj->cel->water_y <= obj->pos.y + 0.3f)) return;
        if ((anim->flags & 8) == 0) gd->fall = madd3(gd->fall, WldGravity, REC_FRAME_RATE);
        obj->pos = madd3(obj->pos, gd->fall, REC_FRAME_RATE);
    };
    if (NDrone2_DoCollision(&dcv) != 0) {
        Drone_FeetOnPoint(obj, &d->hit_test, nullptr, 0, &height);
        if (obj->hits != nullptr) {
            deal_hits();
            if (best != nullptr) {
                anim->flags |= 0x10;
                if (0.0f < best->pos.y - d->hit_test.start.y) anim->flags |= 0x20;
                below_end = best->pos.y - d->hit_test.end.y;
                if (-0.2f < below_end && below_end < 0.0f) anim->flags |= 8;
            }
        }
        gravity();
    } else if (NDrone2_DoGravity(&dcv) == 0) {
        d->fall.y = 0.0f;
    } else {
        gravity();
    }
    obj->mode |= 0x20;
    if ((anim->flags & 8) != 0) {
        d->fall.y = 0.0f;
        if (-0.1f < below_end) obj->pos.y = obj->pos.y - height * 0.125f;
    }
}

// ---- olhar e mira ----

// [decomp] 0x001c6900 AnimObjectIsAiming
bool AnimObjectIsAiming(obj_tag* obj) {
    if (obj == nullptr || obj->sphere_src == nullptr) return false;
    return (anim_of(obj)->anim_flags & 0x180) != 0;
}

// [decomp] 0x001c6850 AnimObjectIsHeadTracking
bool AnimObjectIsHeadTracking(obj_tag* obj) {
    if (obj == nullptr || obj->sphere_src == nullptr) return false;
    return (anim_of(obj)->anim_flags & 0x30) != 0;
}

// [decomp] 0x001c6878 AnimObjectSetAiming
// A mira passa a apontar para target (nulo: para): alcance dist e rapidez speed; use_bone escolhe o
// osso (anim_flags 0x200).
void AnimObjectSetAiming(obj_tag* obj, const void* target, float dist, float speed, int8_t use_bone, uint32_t bone) {
    if (obj == nullptr || obj->sphere_src == nullptr) return;
    ANIMOBJ_tag* a = anim_of(obj);
    a->aim_speed = speed;
    a->aim_dist_sq = dist * dist;
    if (target == nullptr) {
        a->aim_target = nullptr;
        a->anim_flags &= ~0x80u;
        return;
    }
    const uint32_t f = a->anim_flags;
    a->anim_flags = f | 0x80;
    if (use_bone != 0) {
        a->aim_bone = static_cast<uint8_t>(bone);
        a->anim_flags = f | 0x280;
        a->aim_target = target;
        return;
    }
    a->aim_target = target;
    a->anim_flags = (f & ~0x200u) | 0x80;
}

// [decomp] 0x00147918 NDrone2_AimTest
// Se o personagem mira: nunca sem script; pela DAnim (Drone_AnimInfo +0xf): 2 sim; 3 pelo evento da
// animação (aim_from_events) ou depois da metade do script.
uint8_t NDrone2_AimTest(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->script == nullptr) return 0;
    uint8_t kind;
    if (!bots_on()) {
        kind = d->anim_info_f;
        if (kind == 2) return 1;
        if (kind != 3) return 0;
        if (d->aim_from_events != 0) return d->aim_event;
    } else {
        if ((bot_word(d, 0x734) & 2) != 0) return 0;
        kind = d->anim_info_f;
    }
    if (kind == 2) return 1;
    if (kind != 3) return 0;
    const int32_t frame = static_cast<int32_t>(d->script->frame);
    return (d->script->frame_count >> 1) < frame ? 1 : 0;
}

// [decomp] 0x001472c8 NDrone2_DoHeadTracking
// Liga (on, com a propriedade 0x4c) ou desliga o acompanhamento da cabeça. Ligar para target: a classe
// 0x11 só olha o jogador; nada muda se já acompanha o mesmo. Rapidez 0,07 (0,14 em alerta acima de
// 0,66) e alcance 3 m; a cabeça fica no alvo por 2 a 7 segundos (5 a 15 ao desligar) antes de
// NDrone2_DoHeadTrackingBehaviour escolher outro.
void NDrone2_DoHeadTracking(int8_t on, obj_tag* target, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    const float speed = 0.66f < d->alert ? 0.14f : 0.07f;
    uint32_t wait;
    if (on == 0 || behaviour_util_getProperty(0x4c, d->behaviour) == 0) {
        if (!AnimObjectIsHeadTracking(obj)) return;
        wait = (Rand_Rand(10) + 5) * FRAME_RATE_INT;
        if (behaviour_util_getProperty(0x4c, dcv->drone->behaviour) == 0) return;
        AnimObjectSetHeadTracking(dcv->obj, nullptr, 3.0f, 0.14f, 1, 1, 5);
        anim_of(dcv->obj)->head_speed = speed == 0.0f ? 0.07f : speed;
        dcv->drone->head_target = nullptr;
        dcv->drone->head_tracking = 0;
    } else {
        if (d->klass == 0x11 && target != NDrone2_Player()) return;
        if (AnimObjectIsHeadTracking(obj) && d->head_target == target) return;
        wait = (Rand_Rand(5) + 2) * FRAME_RATE_INT;
        if (behaviour_util_getProperty(0x4c, dcv->drone->behaviour) == 0) return;
        AnimObjectSetHeadTracking(dcv->obj, target, 3.0f, 0.14f, 1, 1, 5);
        anim_of(dcv->obj)->head_speed = speed == 0.0f ? 0.07f : speed;
        dcv->drone->head_target = target;
        dcv->drone->head_tracking = target != nullptr ? 1 : 0;
    }
    dcv->drone->head_track_until = GameState.frame + static_cast<uint32_t>(float(wait) * FRAME_RATE_DIV);
}

// [decomp] 0x00147578 NDrone2_DoHeadTrackingBehaviour
// Para quem a cabeça olha quando não mira: nada ao atacar ou em DAnims sem olhar (0 e 0xd); o
// oponente se a visão marcou (sight_flags 8); inimigos calmos (lado 1, alerta abaixo de 0,66) e aliados
// e civis (sem oponente perto: range 16, 50 com head_mode 2) olham o personagem mais perto na mesma
// sala (até 30 m), às vezes (1 em 4) mantendo o atual. Cada escolha vale até head_track_until.
void NDrone2_DoHeadTrackingBehaviour(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const float range = d->head_mode == 2 ? 50.0f : 16.0f;
    obj_tag* target = nullptr;
    const uint8_t kind = d->anim_info_f;
    const bool skip = (d->flags & 0x4000000) != 0 || kind == 0 || kind == 0xd;
    if ((d->sight_flags & 8) != 0) target = d->opponent;
    if (skip) {
        NDrone2_DoHeadTracking(0, nullptr, dcv);
        return;
    }
    float dist_sq = 0.0f;
    if (target != nullptr) dist_sq = Vec_SqDist2D(&target->pos, &dcv->obj->pos);
    if (d->head_track_until != 0 && GameState.frame <= d->head_track_until) return;
    // O personagem mais perto na mesma sala (a menos de 30 m), às vezes mantendo o atual.
    const auto nearest_in_room = [&]() -> obj_tag* {
        if (behaviour_util_getProperty(0x4c, dcv->drone->behaviour) == 0) return nullptr;
        Drone_tag* best = nullptr;
        bool keep = false;
        float best_sq = 900.0f;
        for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            Drone_tag* other = reinterpret_cast<Drone_tag*>(n);
            if (other == dcv->drone) continue;
            if (other->obj->cel != dcv->obj->cel) continue;
            const float sq = Vec_SqDist2D(&dcv->obj->pos, &other->obj->pos);
            if (!(sq < best_sq)) continue;
            if (dcv->drone->head_target == other->obj && Rand_Rand(4) == 0) {
                keep = true;
                continue;
            }
            best_sq = sq;
            best = other;
        }
        if (best != nullptr) return best->obj;
        if (keep) return dcv->drone->head_target;
        return nullptr;
    };
    const uint8_t side = d->side;
    if (side == 1) {
        if (0.66f <= d->alert) return;
        if ((dcv->drone->sight_flags & 8) == 0) target = nearest_in_room();
    } else if (side != 0 && side < 4) {
        if (target == nullptr || range < dist_sq || d->head_target == target) target = nearest_in_room();
    }
    if (target == nullptr || target == d->head_target) return;
    NDrone2_DoHeadTracking(1, target, dcv);
    if (d->head_mode != 2) return;
    if (target == d->opponent) {
        d->head_track_until = GameState.frame + (Rand_Rand(5) + 5) * FRAME_RATE_INT;
    } else {
        d->head_track_until = GameState.frame + FRAME_RATE_INT;
    }
}

// [decomp] 0x001479d8 NDrone2_DoTracking
// O olhar do tick: com on e mirando (NDrone2_AimTest), a mira segue target_pos e a cabeça para; sem
// mirar, a mira para e a cabeça segue NDrone2_DoHeadTrackingBehaviour; sem on, para as duas.
void NDrone2_DoTracking(int8_t on, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    d->aiming = 0;
    const auto stop_aiming = [&]() {
        obj_tag* obj = dcv->obj;
        dcv->drone->aiming = 0;
        if (AnimObjectIsAiming(obj)) AnimObjectSetAiming(obj, nullptr, 100.0f, 0.2f, 1, 5);
    };
    if (on == 0) {
        stop_aiming();
        NDrone2_DoHeadTracking(0, nullptr, dcv);
        d->head_track_until = 0;
        return;
    }
    if (NDrone2_AimTest(dcv) == 0) {
        stop_aiming();
        NDrone2_DoHeadTrackingBehaviour(dcv);
        return;
    }
    NDrone2_DoHeadTracking(0, nullptr, dcv);
    d->head_track_until = 0;
    Drone_tag* dd = dcv->drone;
    obj_tag* obj = dcv->obj;
    obj_tag* opp = dd->opponent;
    dd->aiming = opp != nullptr ? 1 : 0;
    if (opp != nullptr) {
        if (anim_of(obj)->aim_target != opp || !AnimObjectIsAiming(obj)) {
            AnimObjectSetAiming(obj, &dd->target_pos, 100.0f, 0.2f, 0, 0);
        }
    } else if (AnimObjectIsAiming(obj)) {
        AnimObjectSetAiming(obj, nullptr, 100.0f, 0.2f, 1, 5);
    }
}
