// Estados de combate dos personagens da IA (NDrone2_DSTATE_Attack e os estados a que ele leva) e os
// outros estados que não são de bots, com os auxiliares deles (DroneFunc_*/NDrone2_*/DroneAnim_* do
// ACTION.ELF). Ver drone.h e docs/engine.md. Só faltam os estados dos bots (Bot*) e PlayScript (3, que
// precisa do script player).
//
// O ataque. NDrone2_DSTATE_Attack (0x56) é o ponto de entrada: DroneFunc_FirstAttack decide a reação
// à primeira vista (render-se, desafiar, interrogar, um movimento de combate) e liga as flags de
// ataque (0x2000000/0x4000000, alerta 1, alcance de visão ampliado); quem não tem arma vai fugir ou
// sacar (DroneFunc_AttackNoWeapon); os outros vão para o combate (0x58) ou, com a propriedade 0x18
// e o oponente perto, mirar parado (0x67). O combate é uma família de estados que se revezam por
// DroneFunc_CombatState: anda até o oponente pela rota livre (NDrone2_MoveToObject, até 6 m ou o
// alcance range_f0), escolhe a animação do passo pelo ângulo entre a direção do passo e a do
// oponente (DroneAnim_SetCombatMoveAnim) e passa a atirar parado (0x68) ou agachado (0x70) quando o
// vê, a esperar (0x5e), a procurar (0x5c) ou a sair da rota (0x5f). Atirando (DroneWeap_Fire liga
// Drone_tag::firing), escolhe às vezes um movimento (NDrone2_ChooseCombatMove: desviar, andar de lado,
// rolar, um passo, agachar), recarrega (0x69/0x71) quando o pente acaba (Drone_tag::clip) e procura
// cobertura (NDrone2_CoverAvailable/NDrone2_FindCover: o nó de cobertura mais perto pela rede, a menos
// do oponente que ele, livre e com animação para o lado em que o oponente está).
//
// Também aqui: a cobertura (0x8c, 0x93-0x9c), os atiradores de elite (0x29-0x2c), as portas e os nós
// de ação das rotas (0x81-0x84), a procura e as reações a vistas e sons (99-0xa5), os guardas civis e
// o alarme, os ninjas (0xa6-0xb3), o Drake e os astronautas de 0700001b, os impactos, o atordoamento e
// a rendição (0x3f-0x55, 0x7b-0x7d), o motorista, o rapel, a fuga de explosivos, o interrogatório
// (0x39-0x3e), os aliados e a Kiko (0x19-0x27), os reféns (8-0xf), a emboscada (0x37/0x38), o guarda do
// castelo (0x36) e os estados de teste (0xb7-0xba).
//
// Mensagens comuns a quase todos: 6-9 e 0x17-0x19 são impactos (DroneFunc_HandleImpact com o estado
// de reação e a flag: 6 e 8 às vezes sem ela), 0x21 um explosivo à vista (DroneFunc_ConsiderExplosive)
// e 0xc o temporizador 1.
//
// Divergências intencionais (lugares em que o original leria um endereço inválido; contados em
// g_drone_combat_guards, zero nos dados reais):
// - NDrone2_FindCover e NDrone2_FindAmbushCover leem o tipo da cobertura no fim da tabela (a entrada
//   logo depois da última, por um ponteiro que o laço deixou lá); com a tabela cheia (256) essa
//   entrada estaria fora dela, e aqui conta como tipo 0.
// - A animação de um passo de cobertura numa quina com cover_kind 0 (índice fora de CCModes) daria
//   lixo: aqui não há animação (0xffff).
// - Os estados de cobertura com Drone_tag::cover nulo agem como se a cobertura tivesse deixado de
//   servir, em vez de ler o endereço 0.
// - NDrone2_CheckUnsurrender com um oponente sem objeto de animação vê a arma 0; o rapel sem objeto
//   de animação não está no chão; SpecialDeath_Anim sem jogador não toca o script.
// - NDrone2_ReFindMissionPath sem jogador não aponta o alvo da rota para ele.
// - O sequestrador sem refém (unknown_2b0 nulo) nas mensagens 5 e 10 e na conversa
//   (NDrone2_MonitorTalkToMissionObj) não lê o par.
//
// Peculiaridades mantidas (comentadas em cada função): a fala especial da classe 0x10 nunca toca
// (NDrone2_SeenPlayerTalk); NDrone2_EvasiveMove descarta os testes de rolar e andar de lado;
// DroneAnim_SetCombatMoveAnim à direita com as duas rolagens escolhe 0x53; StrafeDodgeRight usa a
// animação 0xd; NDrone2_SetAngleToCover ignora o deslocamento; NDrone2_MoveTarget_IsRouteNode ignora o
// delta; as falas de cobertura só tocam com MPSettings +0x184; NDrone2_MoveToAlertPosition,
// NDrone2_GetAngleDifferenceToAlert e DroneFunc_RecoverTime devolvem o que ficou no registrador (os
// originais são void); GrenadeThrow ignora 0x18/0x19/0x21; PunchImpact ignora 0x17/0x18;
// ValidateCoverAnim devolve outras animações inválidas como vieram; o temporizador de
// TruckDriverMission só religa flags; NDrone2_FindAmbushCover decide quina ou baixa pelo tipo da
// entrada depois da tabela (e devolve 0, já com a cobertura trocada, se ele não é 0 nem 1).

#include <cmath>
#include <cstring>

#include "engine/ainetwork.h"
#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

uint32_t g_drone_combat_guards = 0;

namespace {

constexpr float kHalfPi = 1.5707964f;
constexpr float kPi = 3.1415927f;

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

uint32_t set_state(DCVars_tag* dcv, uint32_t state) { return Drone_SM_SetState(dcv->sm, state, 0); }

bool prop(Drone_tag* d, int32_t p) { return behaviour_util_getProperty(p, d->behaviour) != 0; }

// A arma na mão (ANIMOBJ_tag::weapon), 0 sem objeto de animação.
int8_t weapon_in_hand(obj_tag* obj) {
    if (obj == nullptr || obj->sphere_src == nullptr) return 0;
    return anim_of(obj)->weapon;
}

// Capitão com a arma 0x19 troca para a 0x18 (o que Attack, AimStand e AimStandFire fazem).
void captain_weapon(DCVars_tag* dcv, Drone_tag* drone) {
    if (drone->captain == 0) return;
    obj_tag* obj = dcv->obj;
    if (weapon_in_hand(obj) == 0x19 && obj != nullptr && obj->sphere_src != nullptr) anim_of(obj)->weapon = 0x18;
}

void set_timer1(Drone_tag* d, uint32_t at) {
    d->timer1_time = at;
    d->timer1_data = 0;
    d->timer1_on = 1;
}

// fptoui do libgcc (fp-bit.c): negativo, zero e NaN viram 0; acima de 2^32 - 1, o máximo.
uint32_t fptoui(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 4294967296.0f) return 0xffffffffu;
    return static_cast<uint32_t>(f);
}

// GameState.frame mais "ticks" quadros a 60 Hz (como o original: em float e fptoui).
uint32_t frame_plus(float ticks) { return fptoui(float(GameState.frame) + FRAME_RATE_DIV * ticks); }

bool sees(Drone_tag* d) { return (d->sight_flags & 4) != 0; }

// O personagem só atira se a arma não atravessa uma parede.
void fire_if_clear(Drone_tag* d) { d->firing = d->gun_in_wall == 0 ? 1 : 0; }

// Impactos (6-9, 0x17-0x19) com o estado de reação; com flag68 = 0 as mensagens 6 e 8 vão sem a
// flag, como na maioria dos estados. Explosivo (0x21) com o estado pedido. Devolve false se não é uma
// delas.
bool impact_messages(DCVars_tag* dcv, MsgObject* msg, uint32_t state, int8_t flag68, uint32_t* ret) {
    switch (msg->msg) {
        case 6:
        case 8:
            DroneFunc_HandleImpact(dcv, msg, state, flag68);
            *ret = 1;
            return true;
        case 7: case 9: case 0x17: case 0x18: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, state, 1);
            *ret = 1;
            return true;
        default:
            return false;
    }
}

uint32_t explosive_message(DCVars_tag* dcv, MsgObject* msg, uint32_t state) {
    DroneFunc_ConsiderExplosive(dcv, nf::p32_ptr<obj_tag>(msg->data),
                                static_cast<int16_t>(state));
    return 1;
}

// O teste de passo dos NDrone2_Can*: o ponto "off" no espaço do personagem (girado pela matriz do
// objeto) a partir dos pés deve ser alcançável em linha reta (NDrone2_MoveTest). Bots escalam x e z
// pela velocidade deles.
bool relative_move_ok(Drone_tag* d, obj_tag* obj, float x, float y, float z, bool bot_scale) {
    if (d != nullptr && (d->flags & 0x10) != 0) return false;
    _VECTOR off{x, y, z, 0.0f};
    CelPos_tag to{};
    NDrone2_FeetPos(obj, &to.pos);
    if (bot_scale && d->type == 0x1e) {
        const float s = BOT_getMovementSpeedMul(d);
        Vec_MulR32(&off, &off, s, 1.0f, s);
    }
    ApplyMatrixLVI(&obj->matrix, &off);
    to.pos = {off.x + to.pos.x, off.y + to.pos.y, off.z + to.pos.z, off.w + to.pos.w};
    to.cel = build_FindCel(&to.pos, glb_world);
    if (d != nullptr && (d->flags & 0x10) != 0) return false;
    CelPos_tag from{};
    NDrone2_FeetPos(obj, &from.pos);
    from.cel = obj->cel;
    if (to.cel == nullptr) to.cel = NDrone2_FindCel(&to.pos);
    DroneReachCheckPos = to.pos;
    return NDrone2_MoveTest(&from, &to, nullptr, obj, nullptr, 0, 1) == 1;
}

// A propriedade e a animação de um movimento de combate, sem o bit 0x10 (sem andar).
bool move_allowed(DCVars_tag* dcv, int32_t property, int16_t call) {
    Drone_tag* d = dcv->drone;
    if (behaviour_util_getProperty(property, d->behaviour) == 0) return false;
    if ((d->flags & 0x10) != 0) return false;
    return DroneAnim_CanDoAnimState(dcv, call, 0);
}

// Escolhe um dos dois lados ao acaso (direita primeiro com Rand(2) == 0), cada um só se o teste passa.
template <class R, class L>
uint32_t pick_side(R right, uint32_t right_state, L left, uint32_t left_state) {
    if (Rand_Rand(2) == 0) {
        if (right()) return right_state;
        return left() ? left_state : 0;
    }
    if (left()) return left_state;
    return right() ? right_state : 0;
}

// Agachar para mirar: propriedade 0x11, animação 5, oponente a 6 m ou mais e metade das vezes.
bool crouch_aim_choice(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (behaviour_util_getProperty(0x11, d->behaviour) == 0) return false;
    if (!DroneAnim_CanDoAnimState(dcv, 5, 0)) return false;
    if (!(6.0f <= dcv->drone->opp_dist)) return false;
    return Rand_Rand(2) == 0;
}

}  // namespace

// ---- primeira reação ao oponente ----

// [decomp] 0x00155020 NDrone2_InvalidateAttackRoute
void NDrone2_InvalidateAttackRoute(DCVars_tag* dcv) {
    dcv->drone->active_route = nullptr;
    AINetwork_InvalidateRoute(&dcv->drone->route);
}

// [decomp] 0x0014af80 DroneFunc_AttackNoWeapon
// Sem arma de fogo na mão: com a arma secundária (parâmetro 10) saca (0x66), senão foge com medo
// (0x12). Armado, um civil (lado 3) vira inimigo.
uint32_t DroneFunc_AttackNoWeapon(DCVars_tag* dcv) {
    if (1 < weapon_in_hand(dcv->obj)) {
        if (dcv->drone->side == 3) dcv->drone->side = 1;
        return 0;
    }
    return 1 < static_cast<int8_t>(dcv->drone->weapon2) ? 0x66 : 0x12;
}


// [decomp] 0x00147f58 DroneFunc_FirstAlertState
// Alertado por outro (sem ter visto): em 07000005/07/41, quem soube de um interrogatório (bit 0x200000
// recebido de outro) vai ajudar (0x39).
uint32_t DroneFunc_FirstAlertState(DCVars_tag* dcv) {
    const uint32_t f = dcv->drone->flags2;
    if ((f & (2 | 0x10 | 0x20 | 0x20000 | 0x40000 | 0x80000 | 0x100000)) != 0) return 0;
    if ((f & 0x200000) == 0) return 0;
    const uint32_t level = GameState.level_file;
    if (level == 0x7000007 || level == 0x7000005 || level == 0x7000041) return 0x39;
    return 0;
}

// [decomp] 0x00147c48 DroneFunc_FirstSightState
// Viu o oponente: com a propriedade 0x43, pego de surpresa por ele armado e de frente a menos de 2 m,
// rende-se (0x3f); com a 0x33 avisa todos uma vez (0x11); com a 0x1a desafia (0x3e em 07000009/0a) ou
// interroga (0x3c em 07000007/41); depois a fala de "achei" e, com as propriedades 0x1b/0x19/0x4a, um
// movimento de combate (NDrone2_ChooseCombatMove) ou a granada (0x2d).
uint32_t DroneFunc_FirstSightState(DCVars_tag* dcv) {
    obj_tag* opp = dcv->drone->opponent;
    bool surrender = false;
    if (opp != nullptr && prop(dcv->drone, 0x43)) {
        Drone_tag* d = dcv->drone;
        if (d->alert < 0.66f && d->opp_dist < 2.0f && 2.0943952f < std::fabs(d->opp_bearing) &&
            NDrone2_CanSeeObject(d, opp, 5, 0) != 0 && DroneWeap_OpponentHasWeapon(dcv) != 0 &&
            NDrone2_BondIsFacingMe(dcv, 45.0f) != 0) {
            dcv->drone->alert = 1.0f;
            surrender = true;
        }
    }
    if (surrender) {
        dcv->drone->alert = 1.0f;
        return 0x3f;
    }
    if (prop(dcv->drone, 0x33) && (dcv->drone->flags & 0x200000) == 0) {
        dcv->drone->flags2 |= 1;
        if (dcv->drone->opponent != nullptr) {
            NDrone2_DroneAlertToObject(dcv, 0x11, dcv->drone->opponent, 20.0f, 20.0f, 1.0f);
        }
        dcv->drone->flags |= 0x200000;
    }
    if (prop(dcv->drone, 0x1a)) {
        const uint32_t level = GameState.level_file;
        if (level == 0x7000009 || level == 0x700000a) return 0x3e;
        if (level == 0x7000007 || level == 0x7000041) return 0x3c;
    }
    NDrone2_SeenPlayerTalk(dcv);
    if (prop(dcv->drone, 0x1b)) {
        const uint32_t r = Rand_Rand(2);
        if (r == 0) return 0;
        if (r == 1) return NDrone2_ChooseCombatMove(dcv);
    }
    if (prop(dcv->drone, 0x18)) return 0;
    if (prop(dcv->drone, 0x19)) return NDrone2_ChooseCombatMove(dcv);
    return prop(dcv->drone, 0x4a) ? 0x2d : 0;
}

// [decomp] 0x00148010 DroneFunc_FirstAttack
// O começo do ataque: a reação à primeira vista (ou ao alerta de outro), as flags de ataque, o alcance
// de visão ampliado (fora de 07000009/0a), o aviso aos outros, a fala, a falha de missão (propriedade
// 0x52) e o estado de ataque do tipo (o mesmo que o canal de ataque faz em NDrone2_PreDroneControl).
// 0 deixa NDrone2_DSTATE_Attack decidir.
uint32_t DroneFunc_FirstAttack(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x2000000) != 0) return 0;
    if (switch_BLIND_DRONES != 0) return 0;
    const auto start_attack = [&] {
        d->channel_state = 0;
        d->flags |= 0x2000000;
        d->channel_b = 0;
        if (d->attack_time == 0) d->attack_time = GameState.frame;
        d->flags |= 0x4000000;
        d->alert = 1.0f;
        const uint32_t level = GameState.level_file;
        if (level < 0x700000b && 0x7000008 < level) return false;  // 07000009/0a: mantém o alcance
        if (d->range_e8 < d->range_fc) d->range_e8 = d->range_fc;
        d->view_angle = 0.0f;
        return true;
    };
    if ((d->flags & 0x8000000) != 0) {
        const uint32_t s = DroneFunc_FirstSightState(dcv);
        if (s != 0) {
            start_attack();
            return s;
        }
    } else if ((d->flags & 0x70000000) != 0) {
        const uint32_t s = DroneFunc_FirstAlertState(dcv);
        if (s != 0) return s;
    }
    start_attack();
    Drone_tag* e = dcv->drone;
    if (e->side == 1 && (e->flags & 0x200000) == 0 && e->opponent != nullptr) {
        e->flags2 |= 0x10;
        dcv->drone->flags |= 0x200000;
        NDrone2_DroneAlertToObject(dcv, 0x16, dcv->drone->opponent, 20.0f, 20.0f, 1.0f);
    }
    NDrone2_AttackTalk(dcv);
    if (prop(dcv->drone, 0x52)) DroneFunc_SetMissionFailReason(d, 8);
    Drone_tag* g = dcv->drone;
    if (g->attack_state == 0 || g->channel_b != 0) return DroneFunc_AttackNoWeapon(dcv);
    g->initial_state = 0x56;
    int16_t s = static_cast<int16_t>(g->attack_state);
    if (s != 0) {
        if (g->attack_type != 0) g->type = g->attack_type;
        if (g->type == 2) {
            g->type = 0x19;
            if (DroneTypeSettings[0x19].init != nullptr) DroneTypeSettings[0x19].init(g);
            s = 0x56;
        }
        g->attack_type = 0;
        g->attack_state = 0;
        if (g->divars.params[5] == 100) {
            g->behaviour = g->behaviour_b;
            g->unknown_29 = 1;
        }
        g->unknown_28 = 1;
    }
    return s == 0xb4 ? 0x56 : static_cast<uint32_t>(static_cast<int32_t>(s));
}

// ---- falas ----

// [decomp] 0x001497e8 DroneFunc_ChooseSFX
// Um som ao acaso entre first e last (inclusive).
uint32_t DroneFunc_ChooseSFX(uint32_t first, uint32_t last) { return first + Rand_Rand(last - first + 1); }

// [decomp] 0x0013eac0 NDrone2_AttackTalk
// Grito de ataque pela classe (e pela fase), às vezes; marca a posição vista do oponente.
uint32_t NDrone2_AttackTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    if (Drone_IsTalking(d) != 0) return 0;
    if (d->health <= 0.0f) return 0;
    uint32_t chance = 0;
    uint32_t sfx = 0;
    const uint32_t level = GameState.level_file;
    switch (d->klass) {
        case 1:
            if (level != 0x7000011 && level != (level < 0x7000012 ? 0x700000bu : 0x7000013u)) {
                chance = 2;
                sfx = Rand_Rand(4) + 0x33;
            } else {
                sfx = Rand_Rand(8) + 0xa3;
            }
            break;
        case 2:
            chance = 3;
            sfx = Rand_Rand(9) + 0x37;
            break;
        case 3:
            if ((dcv->drone->sight_flags & 8) != 0) return 0;
            sfx = Rand_Rand(7) + 0xac;
            break;
        case 5:
        case 6:
            if (level - 0x700000cu < 2 && static_cast<uint32_t>(FRAME_RATE_INT * 3) < d->lost_ticks &&
                d->lost_ticks < static_cast<uint32_t>(FRAME_RATE_INT << 2)) {
                sfx = Rand_Rand(3) + 0xd3;
            }
            break;
        case 7:
            if (level == 0x7000014 || level == 0x7000016 || level == 0x7000015) {
                const uint32_t r = Rand_Rand(7);
                sfx = (d->sm.id & 1) != 0 ? r + 0x122 : r + 0x11b;
            }
            break;
        case 0x11:
            if (level == 0x7000007) {
                chance = 3;
                sfx = Rand_Rand(2) + 0x93;
            }
            break;
        default:
            return 0;
    }
    if (sfx == 0) return 0;
    if (chance != 0 && Rand_Rand(chance) != 0) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->seen_opp_pos);
    return 1;
}

// [decomp] 0x0013f1d0 NDrone2_SeenPlayerTalk
// Uma vez em três, a fala de "achei o jogador" da fase (por classe e pelo id da máquina).
// Peculiaridade mantida: a fala especial da classe 0x10 nunca toca (a classe sai antes).
uint32_t NDrone2_SeenPlayerTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (Drone_IsTalking(d) != 0) return 0;
    if (d->health <= 0.0f) return 0;
    if (Rand_Rand(3) != 0) return 0;
    if (d->klass == 0x10) return 0;
    uint32_t sfx = 0;
    const auto pair = [&](uint32_t even, uint32_t odd) {
        const uint32_t r = Rand_Rand(2);
        sfx = (d->sm.id & 1) != 0 ? r + odd : r + even;
    };
    switch (GameState.level_file) {
        case 0x7000005: case 0x7000006: case 0x7000007: case 0x7000008:
            sfx = 0x21;
            if (d->klass == 2) sfx = Rand_Rand(6) + 0x49;
            break;
        case 0x7000009: case 0x700000a:
            sfx = Rand_Rand(9) + 0xc3;
            break;
        case 0x7000012: case 0x700004a:
            sfx = Rand_Rand(7) + 0xac;
            break;
        case 0x7000014:
            if (Rand_Rand(2) != 0) pair(0xf3, 0xf5); else pair(0x10d, 0x10f);
            break;
        case 0x7000015:
            if (Rand_Rand(2) != 0) pair(0xf7, 0xf9); else pair(0x10d, 0x10f);
            break;
        case 0x7000016:
            if (Rand_Rand(2) != 0) pair(0xfb, 0xfd); else pair(0x10d, 0x10f);
            break;
        default:
            break;
    }
    if (sfx == 0) return 0;
    d->sound = NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return 1;
}

// ---- consultas de combate ----

// [decomp] 0x0014ad58 NDrone2_HasOpponent
// Tem oponente, ou o viu há no máximo 30 quadros.
uint32_t NDrone2_HasOpponent(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->opponent != nullptr) return 1;
    return d->seen_time != 0 && GameState.frame <= d->seen_time + 0x1e ? 1 : 0;
}

// [decomp] 0x001549e0 NDrone2_CanAltAttack
// Ataque corpo a corpo (animação 0x3d): oponente a 1,5 m, e só quando as estatísticas do jogador
// contam (ou com bots).
uint32_t NDrone2_CanAltAttack(DCVars_tag* dcv, int16_t variant) {
    if (!bots_on() && !PlrStat_OkToUpdate()) return 0;
    const int16_t v = static_cast<uint16_t>(variant) < 4 ? variant : 0;
    const bool can = DroneAnim_CanDoAnimState(dcv, 0x3d, v);
    return dcv->drone->opp_dist <= 1.5f && can ? 1 : 0;
}

// [decomp] 0x00154960 NDrone2_CanAimCrouch
uint32_t NDrone2_CanAimCrouch(DCVars_tag* dcv) { return crouch_aim_choice(dcv) ? 1 : 0; }



// [decomp] 0x0014f640 NDrone2_CanStepLeft
// Um passo de lado (1,8 m) com mira.
uint32_t NDrone2_CanStepLeft(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 8) != 0) return 0;
    if (!move_allowed(dcv, 0x42, 0x5a)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, 1.8f, 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x0014f808 NDrone2_CanStepRight
uint32_t NDrone2_CanStepRight(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 0x10) != 0) return 0;
    if (!move_allowed(dcv, 0x42, 0x5a) || !DroneAnim_CanDoAnimState(dcv, 0x5b, 0)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, -1.8f, 0.0f, 0.0f, true) ? 1 : 0;
}

namespace {
// Andar de lado: 4 m para as subclasses 6 e 9, 1,8 m para as outras.
float strafe_distance(Drone_tag* d) { return d->subclass == 6 || d->subclass == 9 ? 4.0f : 1.8f; }
}  // namespace

// [decomp] 0x0014f9e8 NDrone2_CanStrafeLeft
uint32_t NDrone2_CanStrafeLeft(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 8) != 0) return 0;
    if (!move_allowed(dcv, 0x41, 0xc)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, strafe_distance(dcv->drone), 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x0014fbe0 NDrone2_CanStrafeRight
uint32_t NDrone2_CanStrafeRight(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 0x10) != 0) return 0;
    if (!move_allowed(dcv, 0x41, 0xc) || !DroneAnim_CanDoAnimState(dcv, 0xd, 0)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, -strafe_distance(dcv->drone), 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x0014fdf8 NDrone2_CanStrafeDodgeLeft
// Desvio de lado (3,6 m).
uint32_t NDrone2_CanStrafeDodgeLeft(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 8) != 0) return 0;
    if (!move_allowed(dcv, 0x13, 0x29)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, 3.6f, 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x0014ffc0 NDrone2_CanStrafeDodgeRight
uint32_t NDrone2_CanStrafeDodgeRight(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 0x10) != 0) return 0;
    if (!move_allowed(dcv, 0x13, 0x29) || !DroneAnim_CanDoAnimState(dcv, 0x2a, 0)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, -3.6f, 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x00150338 NDrone2_CanRollLeft
// Rolar de lado (5,2 m).
uint32_t NDrone2_CanRollLeft(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 8) != 0) return 0;
    if (!move_allowed(dcv, 0x2e, 0x53)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, 5.2f, 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x00150500 NDrone2_CanRollRight
uint32_t NDrone2_CanRollRight(DCVars_tag* dcv) {
    if ((dcv->drone->bunch_flags & 0x10) != 0) return 0;
    if (!move_allowed(dcv, 0x2e, 0x53)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, -5.2f, 0.0f, 0.0f, true) ? 1 : 0;
}

// [decomp] 0x001501a0 NDrone2_CanBackoff
// Recuar mirando (1,8 m para trás; propriedade 6, animação 4).
uint32_t NDrone2_CanBackoff(DCVars_tag* dcv) {
    if (!prop(dcv->drone, 6) || !DroneAnim_CanDoAnimState(dcv, 4, 0)) return 0;
    return relative_move_ok(dcv->drone, dcv->obj, 0.0f, 0.0f, -1.8f, true) ? 1 : 0;
}

// [decomp] 0x00150c18 NDrone2_ChooseCombatMove
// Um movimento de combate ao acaso entre os que o personagem pode fazer: com o oponente mirando nele,
// desviar (0x77/0x78), andar de lado (0x75/0x76) ou rolar (0x79/0x7a); sem ele, agachar (0x6f) ou
// mirar parado (0x67); em qualquer caso, um passo (0x73/0x74). Quando a opção sorteada não dá, tenta
// as seguintes na ordem. 0 se não sai do estado atual.
uint32_t NDrone2_ChooseCombatMove(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const int16_t st = static_cast<int16_t>(d->sm.state);
    uint32_t result = 0;
    int16_t opts[16] = {};
    uint32_t count = 0;
    if ((d->flags & 0x10) != 0 || prop(d, 0x40)) return 0;
    const bool no_bunching = d->no_bunching != 0;
    const bool aiming = DroneWeap_OpponentIsAimingAtMe(dcv, 0.0f) != 0;
    if (7 <= Rand_Rand(0xf) && !aiming && !no_bunching) return 0;
    if (aiming && move_allowed(dcv, 0x13, 0x29)) opts[count++] = 1;
    if ((aiming || no_bunching) && move_allowed(dcv, 0x41, 0xc)) opts[count++] = 2;
    if (aiming && move_allowed(dcv, 0x2e, 0x53)) opts[count++] = 3;
    if (move_allowed(dcv, 0x42, 0x5a)) opts[count++] = 4;
    if (!aiming) {
        if (crouch_aim_choice(dcv)) opts[count++] = 5;
        opts[count++] = 6;
    }
    const auto step = [&]() -> uint32_t {
        if (!move_allowed(dcv, 0x42, 0x5a)) return 0;
        return pick_side([&] { return NDrone2_CanStepRight(dcv) != 0; }, 0x74,
                         [&] { return NDrone2_CanStepLeft(dcv) != 0; }, 0x73);
    };
    const auto crouch_or_stand = [&](bool crouch) -> uint32_t {
        if (crouch && crouch_aim_choice(dcv)) return 0x6f;
        return Rand_Rand(4) != 0 ? 0x67 : 0;
    };
    const int16_t pick = opts[Rand_Rand(count)];
    switch (pick) {
        case 1:
        case 2:
        case 3:
            if (aiming) {
                if (pick == 1 && prop(d, 0x13)) {
                    result = pick_side([&] { return NDrone2_CanStrafeDodgeRight(dcv) != 0; }, 0x78,
                                       [&] { return NDrone2_CanStrafeDodgeLeft(dcv) != 0; }, 0x77);
                }
                if (result == 0 && pick <= 2 && prop(d, 0x41)) {
                    result = pick_side([&] { return NDrone2_CanStrafeRight(dcv) != 0; }, 0x76,
                                       [&] { return NDrone2_CanStrafeLeft(dcv) != 0; }, 0x75);
                }
                if (result == 0 && move_allowed(dcv, 0x2e, 0x53)) {
                    result = pick_side([&] { return NDrone2_CanRollRight(dcv) != 0; }, 0x7a,
                                       [&] { return NDrone2_CanRollLeft(dcv) != 0; }, 0x79);
                }
            }
            if (result == 0) result = step();
            if (result == 0) result = crouch_or_stand(true);
            break;
        case 4:
            result = step();
            if (result == 0) result = crouch_or_stand(true);
            break;
        case 5:
            result = crouch_or_stand(true);
            break;
        case 6:
            result = crouch_or_stand(false);
            break;
        default:
            return 0;
    }
    if (result == 0) return 0;
    if (result == 0x67 && st == 0x68) result = 0;
    if (result == 0x6f && st == 0x70) result = 0;
    return static_cast<int16_t>(result) != st ? result : 0;
}

// [decomp] 0x001506d8 NDrone2_EvasiveMove
// Com o oponente mirando nele de frente (7,5 graus), desvia de lado (0x77/0x78) se der.
// Peculiaridade mantida: as opções sorteadas de rolar e de andar de lado só fazem os testes (os
// resultados são descartados) e caem no desvio; o passo não faz nada.
uint32_t NDrone2_EvasiveMove(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    int16_t opts[16] = {};
    uint32_t count = 0;
    if ((d->flags & 0x10) != 0 || DroneWeap_OpponentIsAimingAtMe(dcv, 7.5f) == 0) return 0;
    if (move_allowed(dcv, 0x2e, 0x53)) opts[count++] = 1;
    if (move_allowed(dcv, 0x41, 0xc)) opts[count++] = 2;
    if (move_allowed(dcv, 0x13, 0x29)) opts[count++] = 3;
    if (move_allowed(dcv, 0x42, 0x5a)) opts[count++] = 4;
    const int16_t pick = opts[Rand_Rand(count)];
    uint32_t result = 0;
    if (pick == 1 || pick == 2 || pick == 3) {
        if (pick == 1 && move_allowed(dcv, 0x2e, 0x53)) {
            pick_side([&] { return NDrone2_CanRollRight(dcv) != 0; }, 0,
                      [&] { return NDrone2_CanRollLeft(dcv) != 0; }, 0);
        }
        if (pick <= 2 && prop(dcv->drone, 0x41)) {
            pick_side([&] { return NDrone2_CanStrafeRight(dcv) != 0; }, 0,
                      [&] { return NDrone2_CanStrafeLeft(dcv) != 0; }, 0);
        }
        if (prop(dcv->drone, 0x13)) {
            result = pick_side([&] { return NDrone2_CanStrafeDodgeRight(dcv) != 0; }, 0x78,
                               [&] { return NDrone2_CanStrafeDodgeLeft(dcv) != 0; }, 0x77);
        }
    }
    if (dcv->drone->type != 0x1e && result != 0) set_state(dcv, result);
    return result;
}

// ---- ir até o oponente ----

// [decomp] 0x001550c8 NDrone2_MoveTarget_IsRouteNode
// Põe move_target no próximo ponto da rota (pelos passos de LinkCreep). Peculiaridade mantida: o
// deslocamento pedido não é usado (o ponto é sempre o do nó atual).
uint32_t NDrone2_MoveTarget_IsRouteNode(int16_t, AIRoute_tag* route, DCVars_tag* dcv, int8_t at_end) {
    if (LinkCreep_Calc(route, at_end) == 0) return 0;
    LinkCreep_Dest(route, 0, &route->next);
    Drone_tag* d = dcv->drone;
    d->move_target.pos = route->next.pos;
    if (route->next.cel == nullptr) route->next.cel = NDrone2_FindCel(&route->next.pos);
    dcv->drone->move_target.cel = route->next.cel;
    dcv->drone->move_arrive = 0.3f;
    _VECTOR feet;
    NDrone2_FeetPos(dcv->obj, &feet);
    dcv->drone->move_dist = Vec_Dist2D(&feet, &dcv->drone->move_target.pos);
    NDrone2_GetAnglesToMoveTarget(dcv, &dcv->drone->move_angles);
    return 1;
}

// [decomp] 0x00151618 NDrone2_MoveToObject
// Vai até obj pela rota livre (route): confere o alvo, refaz a rota se ela não vale e, com `follow`,
// a segue. 3 já chegou (route.arrive), 6 sem objeto, 8 alvo inválido; os outros vêm do cálculo da rota
// (1/2 sem rota boa) ou de NDrone2_FollowRoute (2, 3, 0xc).
uint32_t NDrone2_MoveToObject(DCVars_tag* dcv, obj_tag* obj, float radius, int8_t follow) {
    Drone_tag* d = dcv->drone;
    AIRoute_tag* route = &d->route;
    const uint16_t mask = d->unknown_ba0;
    if (obj == nullptr) return 6;
    d->active_route = route;
    if (!AINetwork_VerifyRouteTarget(dcv->obj, obj, &dcv->drone->goal_target, &dcv->drone->goal_point, radius, route)) {
        return 8;
    }
    uint32_t r = 0;
    if (AINetwork_RouteIsValid(route) == 0) {
        r = AINetwork_CalcRouteToObject(dcv->obj, obj, route, &dcv->drone->goal_target, &dcv->drone->goal_point,
                                        radius, mask);
        if (r < 2) {
            if (follow != 0) {
                route->current = -1;
                NDrone2_MoveTarget_IsRouteNode(0, route, dcv, 0);
            }
        } else if (r != 2) {
            return r;
        }
        dcv->drone->goal_from.pos = route->from.pos;
        dcv->drone->goal_from.cel = route->from.cel;
    }
    dcv->drone->move_dist = Vec_Dist2D(&dcv->obj->pos, &dcv->drone->move_target.pos);
    const float dist = AINetwork_GetRouteDistance(route);
    route->distance = dist;
    if (dist < route->arrive) return 3;
    if (follow == 0) return r;
    r = NDrone2_FollowRoute(route, dcv);
    if (r < 2) return 0;
    if (3 < r && r != 0xc) return 0;
    return r;
}

// [decomp] 0x001543f8 NDrone2_SetGoalToDroneWithLosToOpponent
// Vai até um aliado (um em quatro, ao acaso) que vê o mesmo oponente; 1 se a rota andou.
uint32_t NDrone2_SetGoalToDroneWithLosToOpponent(DCVars_tag* dcv) {
    Drone_tag* me = dcv->drone;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* o = reinterpret_cast<Drone_tag*>(n);
        if (o == me) continue;
        obj_tag* obj = o->obj;
        if (obj == nullptr) continue;
        const Drone_tag* od = static_cast<const Drone_tag*>(obj->data);
        if ((od->flags & 0x600) != 0 || (od->flags & 0x100) == 0 || !(0.0f < od->health) || obj->type == 0x11 ||
            (obj->life & 1) != 0) {
            continue;
        }
        if ((o->flags & 0x4200) != 0) continue;
        if (o->opponent != me->opponent) continue;
        if ((o->sight_flags & 4) == 0) continue;
        if (Rand_Rand(4) != 0) continue;
        CelPos_tag at{};
        at.pos = obj->pos;
        at.cel = obj->cel;
        const uint32_t r = NDrone2_MoveToGoalPosition(dcv, &at, 3.0f, 0);
        if (r == 0 || r == 2) return 1;
    }
    return 0;
}

// [decomp] 0x00155e10 NDrone2_SetGoalToCloserPosition
// Vai até o ponto mais longe em linha reta na direção do oponente (AINetwork_FurthestPosition).
uint32_t NDrone2_SetGoalToCloserPosition(DCVars_tag* dcv) {
    if (dcv->drone->opponent == nullptr) return 0;
    CelPos_tag at{};
    if (AINetwork_FurthestPosition(&dcv->obj->pos, &dcv->drone->opponent->pos, &at.pos) == 0) return 0;
    at.cel = build_FindCel(&at.pos, glb_world);
    if (at.cel == nullptr) return 0;
    return NDrone2_MoveToGoalPosition(dcv, &at, 3.0f, 0) < 3 ? 1 : 0;
}

// ---- cobertura ----

// [decomp] 0x0013e3f8 DroneAnim_ForceCrouchCover
// Agacha atrás da cobertura: com a propriedade 0x10, ou numa quina sem o lado 8 em pé.
uint32_t DroneAnim_ForceCrouchCover(DCVars_tag* dcv, CoverNode_tag* node) {
    if (prop(dcv->drone, 0x10)) return 1;
    return node->type == 0 && (node->flags & 8) == 0 ? 1 : 0;
}

// [decomp] 0x0013e450 DroneAnim_CanUseCornerAsLowCover
uint32_t DroneAnim_CanUseCornerAsLowCover(DCVars_tag*, CoverNode_tag* node) {
    if (node->type == 0) return (node->flags & 0xc) == 4 ? 1 : 0;
    return 1;
}

namespace {
// Pode agachar atrás desta cobertura (propriedade 0x10 ou 0xf; numa quina, só com o lado 4).
bool can_crouch_at(Drone_tag* d, CoverNode_tag* node) {
    if (!prop(d, 0x10) && !prop(d, 0xf)) return false;
    return node->type != 0 || (node->flags & 4) != 0;
}
// Pode ficar de pé atrás desta cobertura (propriedades 10, 0xc, 0xb ou 0xd; numa quina, lado 8).
bool can_stand_at(Drone_tag* d, CoverNode_tag* node) {
    if (!prop(d, 10) && !prop(d, 0xc) && !prop(d, 0xb) && !prop(d, 0xd)) return false;
    return node->type != 0 || (node->flags & 8) != 0;
}
bool can_lean(Drone_tag* d) { return prop(d, 10) || prop(d, 0xb) || prop(d, 0xd); }
}  // namespace

// [decomp] 0x0013b9e0 DroneAnim_CanCrouchLean
// Inclinar agachado para fora da quina (lado 0x40, animação 0x13).
uint32_t DroneAnim_CanCrouchLean(DCVars_tag* dcv, CoverNode_tag* node) {
    Drone_tag* d = dcv->drone;
    if (!can_crouch_at(d, node)) return 0;
    if (!can_lean(d)) return 0;
    if (node->type != 0) return 0;
    return (node->flags & 0x40) != 0 && DroneAnim_CanDoAnimState(dcv, 0x13, 0) ? 1 : 0;
}

// [decomp] 0x0013bb38 DroneAnim_CanCrouchStepOut
// Sair agachado da quina (propriedade 0xc, lado 0x20, animação 0x15).
uint32_t DroneAnim_CanCrouchStepOut(DCVars_tag* dcv, CoverNode_tag* node) {
    Drone_tag* d = dcv->drone;
    if (!can_crouch_at(d, node)) return 0;
    if (!prop(d, 0xc)) return 0;
    if (node->type != 0) return 0;
    return (node->flags & 0x20) != 0 && DroneAnim_CanDoAnimState(dcv, 0x15, 0) ? 1 : 0;
}

// [decomp] 0x0013bc60 DroneAnim_CanStandLean
// Inclinar de pé (lado 0x40, animação 0x17).
uint32_t DroneAnim_CanStandLean(DCVars_tag* dcv, CoverNode_tag* node) {
    Drone_tag* d = dcv->drone;
    if (!can_lean(d)) return 0;
    if (!can_stand_at(d, node) || node->type != 0 || (node->flags & 0x40) == 0) return 0;
    return DroneAnim_CanDoAnimState(dcv, 0x17, 0) ? 1 : 0;
}

// [decomp] 0x0013bde8 DroneAnim_CanStandStepOut
// Sair de pé (propriedade 0xc, lado 0x20, animação 0x1b).
uint32_t DroneAnim_CanStandStepOut(DCVars_tag* dcv, CoverNode_tag* node) {
    Drone_tag* d = dcv->drone;
    if (!prop(d, 0xc)) return 0;
    if (!can_stand_at(d, node)) return 0;
    if (node->type != 0) return 0;
    return (node->flags & 0x20) != 0 && DroneAnim_CanDoAnimState(dcv, 0x1b, 0) ? 1 : 0;
}

// [decomp] 0x00151d68 NDrone2_CoverNodeOK
// A cobertura serve contra o oponente atual (os bits dela para o lado dele: 2/4 quina pelos dois
// lados, 8 baixa; com `force`, os de 0x70), longe o bastante dele (unknown_f8; com a propriedade 0x44,
// no máximo 1,5 x range_fc), com animação para usá-la e sem outro personagem vivo a 1,5 m. Devolve o
// tipo de uso: 1/2 lado da quina (o pedido em `kind`, ou o primeiro que der), 3 baixa; 0 não serve.
uint32_t NDrone2_CoverNodeOK(DCVars_tag* dcv, CoverNode_tag* node, uint16_t kind, int8_t force) {
    Drone_tag* d = dcv->drone;
    const uint32_t usable = node->usable[d->opponent_player];
    if (d->opponent == nullptr) return 0;
    if ((usable & 0xe) == 0) return 0;
    if (force != 0 && (usable & 0x70) == 0) return 0;
    _VECTOR delta{d->opponent->pos.x - node->pos.x, d->opponent->pos.y - node->pos.y,
                  d->opponent->pos.z - node->pos.z, d->opponent->pos.w - node->pos.w};
    const float dist = Vec_Magnitude(&delta);
    if (prop(d, 0x44) && d->range_fc * 1.5f < dist) return 0;
    if (dist < d->unknown_f8) return 0;
    if (node->type == 0) {
        bool ok;
        if (DroneAnim_ForceCrouchCover(dcv, node) == 0) {
            ok = DroneAnim_CanCrouchLean(dcv, node) != 0 || DroneAnim_CanCrouchStepOut(dcv, node) != 0 ||
                 DroneAnim_CanStandLean(dcv, node) != 0 || DroneAnim_CanStandStepOut(dcv, node) != 0 ||
                 DroneAnim_CanUseCornerAsLowCover(dcv, node) != 0;
        } else {
            ok = DroneAnim_CanCrouchLean(dcv, node) != 0 || DroneAnim_CanCrouchStepOut(dcv, node) != 0 ||
                 DroneAnim_CanUseCornerAsLowCover(dcv, node) != 0;
        }
        if (!ok) return 0;
        if (kind == 1) {
            if ((usable & 2) == 0) return 0;
        } else if (kind == 2) {
            if ((usable & 4) == 0) return 0;
        } else {
            kind = 1;
            if ((usable & 2) == 0) {
                kind = 2;
                if ((usable & 4) == 0) return 0;
            }
        }
    } else {
        if (node->type == 1 && !prop(dcv->drone, 0xf)) return 0;
        kind = 3;
        if ((usable & 8) == 0) return 0;
    }
    if ((dcv->drone->flags & 0x1000) == 0) {
        for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
            Drone_tag* o = reinterpret_cast<Drone_tag*>(n);
            if (o == d || (o->flags & 0x200) != 0) continue;
            if (Vec_Dist3D(&o->obj->pos, &node->pos) < 1.5f) return 0;
        }
    }
    return kind;
}

// [decomp] 0x00152028 NDrone2_GetPosForCornerCover
// Onde ficar numa cobertura, pelo lado em que o oponente está (bits 2/4 quina, 8 baixa): a posição
// dela deslocada (de lado pelo parâmetro da quina e para trás), com `step` a posição de sair para
// atirar. rot recebe a rotação da cobertura (girada 90 graus no lado 2).
uint32_t NDrone2_GetPosForCornerCover(CoverNode_tag* node, uint32_t side, _VECTOR* pos, _VECTOR* rot, int8_t step) {
    const uint32_t f = node->usable[side];
    uint32_t k = (f & 2) != 0 ? 1 : 0;
    if ((f & 4) != 0) k = 2;
    if ((f & 8) != 0) k = 3;
    if (k == 0) return 0;
    pos->x = node->pos.x;
    pos->y = node->pos.y;
    pos->z = node->pos.z;
    if (rot != nullptr) {
        rot->x = node->rot.x;
        rot->y = node->rot.y;
        rot->z = node->rot.z;
    }
    float side_off;
    float fwd;
    if (k == 2) {
        side_off = step != 0 ? 0.7f : -0.1f;
        fwd = float(node->corner_b) * 0.1f;
    } else if (k == 1) {
        fwd = 0.1f;
        side_off = -float(node->corner_a) * 0.1f;
        if (rot != nullptr) rot->y = rot->y - kHalfPi;
        if (step != 0) fwd = -0.7f;
    } else {
        fwd = 0.0f;
        side_off = step != 0 ? -1.1f : -0.3f;
    }
    const float s = PS2Sinf(node->rot.y);
    const float c = PS2Sinf(node->rot.y + kHalfPi);
    pos->x = pos->x + side_off * s + fwd * c;
    const float c2 = PS2Sinf(node->rot.y + kHalfPi);
    const float s2 = PS2Sinf(node->rot.y);
    pos->z = pos->z + (side_off * c2 - fwd * s2);
    return 1;
}

// [decomp] 0x00155858 NDrone2_DisownCover
// Larga a cobertura (fica livre para os outros).
void NDrone2_DisownCover(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->cover != nullptr) {
        d->cover->user = nullptr;
        d->cover->flags &= 0xffffffef;
    }
    d->flags &= 0xfffff7ff;
    dcv->drone->flags &= 0xffffefff;
}

// [decomp] 0x00152548 NDrone2_FindCover
// A cobertura mais perto pela rede (o byte de distância do emissor dela no nó do personagem), não
// mais longe que o oponente, livre e que sirva (NDrone2_CoverNodeOK). Toma posse dela: o alvo das
// rotas vai para a posição de chegada, cover_pos guarda onde ficar. Sem `force`, quem já tem
// cobertura (0x1000) não procura outra.
uint32_t NDrone2_FindCover(DCVars_tag* dcv, int8_t force) {
    NPCGLOBALS_tag& n = NPCGlobals;
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    CoverNode_tag* nodes = n.cover_nodes;
    if (n.cover_count == 0) return 0;
    if (force == 0 && (d->flags & 0x1000) != 0) return 0;
    if (nodes == nullptr) return 0;
    CelPos_tag at{};
    at.pos = obj->pos;
    at.cel = obj->cel;
    AINodeSearch_tag s{};
    s.pos = &at;
    s.goal = nullptr;
    s.path = d->route.path;
    s.to_node = 1;
    s.open = nullptr;
    s.obj_a = obj;
    s.obj_b = nullptr;
    s.mask = 0;
    s.radius_sq = 625.0f;
    s.node = -1;
    s.found = 0;
    s.failed = 0;
    AINetwork_NodeSearch(&s);
    uint32_t node = (s.found < 1 || static_cast<uint16_t>(s.node) == 0xffff) ? 0xffff : static_cast<uint16_t>(s.node);
    if (node == 0xffff) return 0;
    uint32_t best = 0xffffffff;
    uint32_t best_kind = 0;
    uint32_t best_dist = 0xff;
    const float opp_sq = d->opp_dist * d->opp_dist;
    for (uint32_t i = 0; i < n.cover_count; ++i) {
        CoverNode_tag* c = &nodes[i];
        if ((c->flags & 0x10) != 0) continue;
        AIPath_tag* path = c->emitter.path;
        if (path == nullptr || path != d->route.path || c->emitter.dist == nullptr || path->node_count <= node) continue;
        const uint32_t dd = c->emitter.dist[node];
        if (dd == 0xff || best_dist <= dd) continue;
        Vec_AngleDifference(ATAN2_APPROX(c->pos.x - obj->pos.x, c->pos.z - obj->pos.z), d->opp_yaw);  // não usado
        if (!(float(dd) * float(dd) <= opp_sq)) continue;
        const uint32_t kind = NDrone2_CoverNodeOK(dcv, c, 0, force);
        if (kind == 0) continue;
        if ((dcv->drone->flags & 0x800) != 0 && d->cover != nullptr) continue;
        best_kind = kind;
        best = i;
        best_dist = dd;
    }
    if (best == 0xffffffff) return 0;
    if (dcv->drone->cover != nullptr) {
        dcv->drone->cover->user = nullptr;
        dcv->drone->cover->flags &= 0xffffffef;
    }
    dcv->drone->flags &= 0xfffff7ff;
    dcv->drone->flags &= 0xffffefff;
    d->cover = &nodes[best];
    // Peculiaridade mantida: o tipo lido é o da entrada depois da última da tabela (ver o cabeçalho).
    uint8_t end_type = 0;
    if (nodes == CoverNodes && n.cover_count >= static_cast<uint32_t>(kMaxCoverNodes)) {
        ++g_drone_combat_guards;
    } else {
        end_type = nodes[n.cover_count].type;
    }
    const float radius = (end_type == 0 || end_type == 1) ? 0.5f : 1.0f;
    dcv->drone->active_route = nullptr;
    AINetwork_InvalidateRoute(&dcv->drone->route);
    CelPos_tag goal{};
    NDrone2_GetPosForCornerCover(d->cover, d->opponent_player, &goal.pos, &d->goal_point.rot, 1);
    goal.cel = d->cover->cel;
    AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &goal, radius, 0);
    Drone_tag* e = dcv->drone;
    e->cover->user = dcv->obj;
    e->cover_kind = static_cast<uint16_t>((e->cover_kind & 0xfffc) | (best_kind & 3));
    e->cover->flags |= 0x10;
    NDrone2_GetPosForCornerCover(e->cover, e->opponent_player, &e->cover_pos.pos, nullptr, 0);
    e->cover_pos.cel = e->cover->cel;
    dcv->drone->flags |= 0x800;
    dcv->drone->flags |= 0x1000;
    return 1;
}

// [decomp] 0x00155a38 NDrone2_CoverAvailable
// Há cobertura para correr (propriedade 0x30): já tem uma (0x800), ou não vê o oponente (ou ele está a
// 4 m ou mais) e acha uma.
uint32_t NDrone2_CoverAvailable(DCVars_tag* dcv, int8_t force) {
    if (!prop(dcv->drone, 0x30)) return 0;
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x800) != 0) return 1;
    if (sees(d) && d->opp_dist < 4.0f) return 0;
    return NDrone2_FindCover(dcv, force) != 0 ? 1 : 0;
}

// ---- animações de combate ----

// [decomp] 0x0013e3b0 DroneAnim_InTransition
// A animação atual é uma transição (tipo acima de 3, menos o 8).
uint32_t DroneAnim_InTransition(Drone_tag* drone) {
    return drone != nullptr && 3 < drone->anim_kind && drone->anim_kind != 8 ? 1 : 0;
}


// [decomp] 0x0013ce00 DroneAnim_GetMoveQuad
// O quadrante do passo em relação ao oponente (0 frente, 1 esquerda, 2 trás, 3 direita), com uma
// histerese a favor do quadrante anterior.
uint32_t DroneAnim_GetMoveQuad(float angle, uint16_t prev) {
    const float a = std::fabs(angle);
    switch (prev) {
        case 0:
            if (2.3561947f < a) return 2;
            if (0.9250245f < a) return 0.0f < angle ? 3 : 1;
            return 0;
        case 1:
            if (2.495821f < a) return 2;
            if (0.7853982f < angle) return 3;
            return angle < -0.9250245f ? 1 : 0;
        case 2:
            if (2.2165682f < a) return 2;
            if (0.7853982f < a) return 0.0f < angle ? 3 : 1;
            return 0;
        case 3:
            if (2.495821f < a) return 2;
            if (0.9250245f < angle) return 3;
            return angle < -0.7853982f ? 1 : 0;
        default:
            return 0;
    }
}

// [decomp] 0x0013cfb0 DroneAnim_SetCombatMoveAnim
// A animação de andar no combate pelo quadrante entre a direção do passo e a do oponente: correr
// (0x1e), andar mirando de frente (8, 0xf), de lado (0xc/0xd ou o passo 0x5a/0x5b perto de um nó) ou
// para trás (4); de lado, perto de "dist" 2 a 2,5 m, às vezes rolar (0x53-0x56). O deslocamento do giro
// (yaw_offset) vira o corpo para o oponente andando de lado ou para trás; firing diz se pode atirar
// (viu o oponente há menos de 3 s). Peculiaridade mantida: para a direita, com as duas rolagens
// possíveis, sorteia entre 0x56 e 0x53 (a da esquerda).
uint32_t DroneAnim_SetCombatMoveAnim(DCVars_tag* dcv, float dist) {
    Drone_tag* d = dcv->drone;
    bool fire = false;
    uint32_t call = 0x1e;
    float param = 0.0f;
    if ((d->bunch_flags & 2) != 0) {
        set_state(dcv, 0x5e);
        return 1;
    }
    if (d->opponent == nullptr) {
        d->firing = 0;
        DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
        return 1;
    }
    const _VECTOR target{d->opponent->pos.x + d->aim_offset.x, d->opponent->pos.y + d->aim_offset.y,
                         d->opponent->pos.z + d->aim_offset.z, d->opponent->pos.w + d->aim_offset.w};
    const float to_opp = ATAN2_APPROX(target.x - dcv->obj->pos.x, target.z - dcv->obj->pos.z);
    const float to_move = ATAN2_APPROX(d->move_target.pos.x - dcv->obj->pos.x, d->move_target.pos.z - dcv->obj->pos.z);
    if (NDrone2_DoAnimation(dcv->drone) == 0 || dcv->drone->opponent == nullptr) {
        DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
        return 1;
    }
    const bool recent = dcv->drone->lost_ticks < static_cast<uint32_t>(FRAME_RATE_INT * 3);
    const uint32_t quad = DroneAnim_GetMoveQuad(Vec_AngleDifference(to_move, to_opp), d->move_quad);
    d->move_quad = 0;
    const auto done = [&](bool can_fire) {
        d->firing = can_fire ? 1 : 0;
        DroneAnim_CallAnim(param, call, 0, 0, 0, 0, dcv);
        d->yaw_offset = param;
        return 1u;
    };
    const auto no_move = [&] {
        d->firing = 0;
        DroneAnim_CallAnim(param, call, 0, 0, 0, 0, dcv);
        d->yaw_offset = param;
        return 1u;
    };
    // Andar de lado (1 esquerda, 3 direita).
    const auto sideways = [&](bool left) -> uint32_t {
        if ((dcv->drone->bunch_flags & (left ? 8u : 0x10u)) != 0) return no_move();
        const uint16_t strafe = left ? 0xc : 0xd;
        const uint16_t strafe_b = left ? 0x29 : 0x2a;
        const uint16_t step = left ? 0x5a : 0x5b;
        const float turn = left ? kHalfPi : -kHalfPi;
        uint16_t anim;
        if (prop(dcv->drone, 0) || prop(dcv->drone, 0x41)) {
            anim = d->anim_state;
        } else {
            if (!prop(dcv->drone, 0x42)) return done(fire);
            if (!DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(step), 0)) return no_move();
            if (!(DroneMove_DistanceToNextRouteNode(dcv) < 3.0f)) return done(fire);
            const float limit = d->anim_state == step ? 2.792527f : 2.617994f;
            if (limit <= std::fabs(DroneMove_NextRouteAngle(dcv))) return no_move();
            d->move_quad = left ? 1 : 3;
            param = turn;
            call = step;
            return done(false);
        }
        if (anim != strafe && anim != strafe_b) {
            if (!DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(strafe), 0)) return no_move();
            anim = strafe;
        }
        param = turn;
        d->move_quad = left ? 1 : 3;
        call = anim;
        fire = recent;
        if (2.0f < dist && dist < 2.5f && prop(dcv->drone, 0x2e)) {
            const bool a = DroneAnim_CanDoAnimState(dcv, left ? 0x53 : 0x54, 0);
            const bool b = DroneAnim_CanDoAnimState(dcv, left ? 0x55 : 0x56, 0);
            if (a) {
                if (b) {
                    call = Rand_Rand(2) == 0 ? (left ? 0x55 : 0x56) : 0x53;
                } else {
                    call = left ? 0x53 : 0x54;
                }
            } else {
                if (!b) {
                    d->firing = recent ? 1 : 0;
                    DroneAnim_CallAnim(param, call, 0, 0, 0, 0, dcv);
                    d->yaw_offset = param;
                    return 1;
                }
                call = left ? 0x55 : 0x56;
            }
            return done(false);
        }
        return done(fire);
    };
    switch (quad) {
        case 1:
            return sideways(true);
        case 3:
            return sideways(false);
        case 2:
            if ((dcv->drone->bunch_flags & 4) != 0) return no_move();
            if (prop(dcv->drone, 0) || prop(dcv->drone, 6)) {
                if (!DroneAnim_CanDoAnimState(dcv, 4, 0)) return no_move();
                d->move_quad = 2;
                param = kPi;
                fire = recent;
                call = 4;
            }
            return done(fire);
        default: {
            // De frente: andar mirando (8, propriedades 1 ou 0) ou andar (0xf, propriedade 2).
            uint32_t walk = 0;
            if ((prop(dcv->drone, 1) || prop(dcv->drone, 0)) && DroneAnim_CanDoAnimState(dcv, 8, 0)) {
                walk = 8;
            } else if (prop(dcv->drone, 2) && DroneAnim_CanDoAnimState(dcv, 0xf, 0)) {
                walk = 0xf;
            }
            if (walk != 0) {
                d->move_quad = static_cast<uint16_t>(quad);
                fire = recent;
                if ((dcv->drone->bunch_flags & 2) == 0) call = walk;
            }
            return done(fire);
        }
    }
}

// ---- o estado de combate ----

// [decomp] 0x00148338 DroneFunc_CombatState
// O cérebro dos estados de combate (0x58, 0x5a, 0x5c, 0x5e, 0x68, 0x70): anda pela rota até o
// oponente e devolve o estado seguinte (0 fica). Sem oponente há mais de 30 quadros, 0x60.
uint32_t DroneFunc_CombatState(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const int16_t st = static_cast<int16_t>(d->sm.state);
    bool has = d->opponent != nullptr;
    if (!has) has = d->seen_time != 0 && !(d->seen_time + 0x1eu < GameState.frame);
    if (!has) return 0x60;
    if (DroneAnim_InTransition(dcv->drone) != 0) return 0;
    const int16_t k = static_cast<int16_t>(st - 0x58);
    const int8_t follow = (k == 0x10 || k == 0x18) ? 0 : 1;
    const float radius = d->range_f0 < 6.0f ? d->range_f0 : 6.0f;
    const uint32_t r = NDrone2_MoveToObject(dcv, d->opponent, radius, follow);
    const bool close = r < 4 || r == 10;
    const auto stop_flags = [&] { return (dcv->drone->flags & 0x10) != 0 || prop(dcv->drone, 0x40); };
    const auto anim = [&](uint32_t call) {
        DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
        return 0u;
    };
    // Mira no oponente: atira se o vê, senão fica em alerta.
    const auto face = [&]() -> uint32_t {
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (sees(dcv->drone)) {
            d->firing = 1;
            return anim(10);
        }
        return anim(0x21);
    };
    // O que fazer com o resultado da rota (0x5a e 0x5c).
    const auto by_route = [&](bool level_walk) -> uint32_t {
        switch (r) {
            case 0: case 1: case 2:
                if (level_walk) {
                    Drone_tag* e = dcv->drone;
                    if ((e->bunch_flags & 2) != 0) return anim(0x21);
                    if (GameState.level_file - 0x7000005u >= 2) return anim(0x1e);
                }
                DroneAnim_SetCombatMoveAnim(dcv, dcv->drone->active_route->distance);
                return 0;
            case 3: case 9: case 0xb: case 0xc:
                return face();
            case 5:
                return 0x7f;
            case 10:
                return 0;
            default:
                return 0x5f;
        }
    };
    switch (k) {
        case 0:
            if (stop_flags()) return 0x59;
            if (dcv->drone->range_f0 <= dcv->drone->opp_dist) return 0x5a;
            if (!sees(dcv->drone)) return 0xf < dcv->drone->lost_ticks ? 0x5c : 0x5f;
            return 0x5b;
        case 2:
            if (stop_flags()) return 0x59;
            if (NDrone2_CoverAvailable(dcv, 1) != 0) return 0x8c;
            if (dcv->drone->opp_dist < dcv->drone->range_f0) return 0x58;
            return by_route(true);
        case 4:
            if (stop_flags()) return 0x59;
            if (NDrone2_CoverAvailable(dcv, 1) != 0) return 0x8c;
            if (sees(dcv->drone)) return 0x5b;
            if (0xf < dcv->drone->lost_ticks) return by_route(false);
            return face();
        case 6:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (close) return 0x58;
            if (!sees(dcv->drone)) return anim(0x21);
            d->firing = 1;
            return anim(10);
        case 0x10: {
            Drone_tag* e = dcv->drone;
            if (static_cast<int16_t>(e->clip) < 1) return 0x69;
            if (close && e->range_f0 <= e->opp_dist) return 0x5a;
            if (0xf < e->lost_ticks) return 0x5c;
            if (NDrone2_CoverAvailable(dcv, 1) != 0) return 0x8c;
            if (d->anim_kind == 2 && d->anim_state == 0xa) {
                if (DroneWeap_CanThrowGrenade(dcv)) return 0x2d;
                const uint32_t s = NDrone2_ChooseCombatMove(dcv);
                if (s != 0) return s;
            }
            return anim(10);
        }
        case 0x18: {
            Drone_tag* e = dcv->drone;
            if (close && e->range_f0 <= e->opp_dist) return 0x5a;
            if (0xf < e->lost_ticks) return 0x5c;
            if (NDrone2_CoverAvailable(dcv, 1) != 0) return 0x8c;
            if (DroneWeap_CanThrowGrenade(dcv)) return 0x2d;
            if (float(d->sm.entered) + FRAME_RATE_DIV * 120.0f < float(GameState.frame)) {
                const uint32_t s = NDrone2_ChooseCombatMove(dcv);
                if (s != 0) return s;
            }
            return anim(5);
        }
        default:
            return 0;
    }
}

// ---- estados: ataque e combate ----

// [decomp] 0x00164210 NDrone2_DSTATE_Attack
// Entrada no ataque (ver o cabeçalho). Tipos 2 e 0x19 (atiradores de elite) vão mirar (0x2a).
uint32_t NDrone2_DSTATE_Attack(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    dcv->sm->result = DroneFunc_FirstAttack(dcv);
    if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
    dcv->drone->alert = 1.0f;
    Drone_AlertStatusSet(1, dcv);
    dcv->sm->result = DroneFunc_AttackNoWeapon(dcv);
    if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
    captain_weapon(dcv, drone);
    dcv->drone->flags |= 0x400000;
    Drone_tag* d = dcv->drone;
    if (d->type == 2 || d->type == 0x19) return set_state(dcv, 0x2a);
    d->flags |= 0x10000;
    NDrone2_InvalidateAttackRoute(dcv);
    if (prop(dcv->drone, 0x18) && !(dcv->drone->range_f0 <= dcv->drone->opp_dist) &&
        NDrone2_DroneInSights(dcv) == 0) {
        dcv->drone->initial_state = 0x68;
        return set_state(dcv, 0x67);
    }
    set_state(dcv, 0x58);
    return 1;
}

// [decomp] 0x00173178 NDrone2_DSTATE_Alerted1stEncounter
uint32_t NDrone2_DSTATE_Alerted1stEncounter(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    set_state(dcv, 0x5a);
    return 1;
}

// [decomp] 0x001731b8 NDrone2_DSTATE_Combat
// Combate: a cada tick, DroneFunc_CombatState escolhe o próximo estado.
uint32_t NDrone2_DSTATE_Combat(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->alert = 1.0f;
            return 1;
        case 0:
            return 1;
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            dcv->sm->result = DroneFunc_CombatState(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001643e8 NDrone2_DSTATE_CombatNoMove
// Combate sem sair do lugar (flags 0x10 ou propriedade 0x40): gira para o oponente e atira; a
// classe 0xc às vezes se agacha (0x6e). A subclasse 0x13 espera um segundo antes do primeiro tiro.
uint32_t NDrone2_DSTATE_CombatNoMove(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            if (switch_BLIND_DRONES != 0) return set_state(dcv, 4);
            Drone_AlertStatusSet(1, dcv);
            if (dcv->drone->type == 2) return set_state(dcv, 0x2b);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            dcv->drone->flags |= 0x10000;
            NDrone2_AttackTalk(dcv);
            if (drone->subclass != 0x13) {
                DroneWeap_Fire(1, dcv);
                return 1;
            }
            drone->firing = 0;
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT);
            return 1;
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if ((dcv->drone->flags & 0x10) == 0 && !prop(dcv->drone, 0x40)) return set_state(dcv, 0x68);
            DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, 0.5235988f);
            if (NDrone2_CanAltAttack(dcv, 0) != 0) return set_state(dcv, 0x72);
            if (NDrone2_HasOpponent(dcv) == 0) return set_state(dcv, 0x60);
            drone->firing = sees(dcv->drone) ? (drone->gun_in_wall == 0 ? 1 : 0) : 0;
            if (dcv->drone->burst_over == 0) {
                if (drone->firing != 0) {
                    DroneAnim_PlayFiringAnim(dcv);
                    return 1;
                }
            } else if (drone->firing != 0) {
                DroneWeap_Fire(1, dcv);
            }
            if (drone->klass != 0xc) return 1;
            if (Rand_Rand(3) != 0) return 1;
            return set_state(dcv, 0x6e);
        case 0xc:
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00164690 NDrone2_DSTATE_CombatOutOfRange
// O oponente está além do alcance: anda até ele (DroneFunc_CombatState). Com os personagens cegos
// (depuração), vai até onde ele está (99) e depois investiga (0x7e).
uint32_t NDrone2_DSTATE_CombatOutOfRange(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
        case 0x1b:
            return 1;
        case 1:
            if (switch_BLIND_DRONES == 0) {
                Drone_AlertStatusSet(1, dcv);
                dcv->drone->flags |= 0x10000;
                dcv->drone->alert = 1.0f;
                return 1;
            }
            AINetwork_InvalidateRoute(&dcv->drone->route);
            AINetwork_SetupGoalPositionToObj(&dcv->drone->goal_target, &dcv->drone->goal_point, dcv->drone->opponent,
                                             2.0f, 1);
            dcv->drone->initial_state = 0x7e;
            dcv->drone->return_state = 0x5f;
            return set_state(dcv, 99);
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            ++NPCGlobals.unknown_188[0];
            dcv->sm->result = DroneFunc_CombatState(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00173280 NDrone2_DSTATE_CombatNewSighting
// Acabou de ver o oponente: fala e atira de pé (0x68) ou, às vezes, agachado (0x70).
uint32_t NDrone2_DSTATE_CombatNewSighting(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    dcv->drone->flags |= 0x10000;
    NDrone2_SeenPlayerTalk(dcv);
    if (NDrone2_CanAimCrouch(dcv) != 0 && 6.0f <= dcv->drone->opp_dist && Rand_Rand(2) != 0) {
        set_state(dcv, 0x70);
        return 1;
    }
    return set_state(dcv, 0x68);
}

// [decomp] 0x00164838 NDrone2_DSTATE_CombatNoSight
// Perdeu o oponente de vista: anda até onde ele está (DroneFunc_CombatState).
uint32_t NDrone2_DSTATE_CombatNoSight(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->flags |= 0x10000;
            dcv->drone->alert = 1.0f;
            return 1;
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            dcv->sm->result = DroneFunc_CombatState(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00173338 NDrone2_DSTATE_CombatTooClose
uint32_t NDrone2_DSTATE_CombatTooClose(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    dcv->drone->flags |= 0x10000;
    set_state(dcv, 0x67);
    return 1;
}

// [decomp] 0x00164960 NDrone2_DSTATE_CombatWait
// Espera em alerta virado para o oponente, atirando quando o vê; a cada 3 s (temporizador 1)
// DroneFunc_CombatState decide se sai.
uint32_t NDrone2_DSTATE_CombatWait(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->flags |= 0x10000;
            dcv->drone->alert = 1.0f;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
            DroneWeap_Fire(1, dcv);
            drone->firing = 0;
            return 1;
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, 0.5235988f);
            if (!sees(dcv->drone)) {
                drone->firing = 0;
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            fire_if_clear(drone);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            if (dcv->drone->burst_over == 0) {
                if (drone->firing != 0) DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            if (drone->firing != 0) DroneWeap_Fire(1, dcv);
            return 1;
        case 0xc:
            dcv->sm->result = DroneFunc_CombatState(dcv);
            if (dcv->sm->result == 0) {
                set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
                return 1;
            }
            return set_state(dcv, dcv->sm->result);
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00164c08 NDrone2_DSTATE_CombatNoRoute
// Sem rota até o oponente: atira se o vê, senão tenta um movimento de combate; a cada segundo
// (temporizador 1) tenta cobertura, alternadamente a rota até ele (2 m) ou até um aliado que o vê,
// ou chegar mais perto em linha reta.
uint32_t NDrone2_DSTATE_CombatNoRoute(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(60.0f));
            dcv->drone->state_a = 0;
            NDrone2_InvalidateAttackRoute(dcv);
            DroneWeap_Fire(1, dcv);
            drone->firing = 0;
            return 1;
        case 2:
            dcv->drone->flags |= 0x20;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (!sees(dcv->drone)) {
                drone->firing = 0;
                dcv->sm->result = NDrone2_ChooseCombatMove(dcv);
                const uint32_t r = dcv->sm->result;
                if (r != 0 && r != 0x67 && r != 0x6f) return set_state(dcv, r);
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            fire_if_clear(drone);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            if (dcv->drone->burst_over == 0) {
                if (drone->firing != 0) DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            if (drone->firing != 0) DroneWeap_Fire(1, dcv);
            return 1;
        case 0xc: {
            Drone_tag* d = dcv->drone;
            if ((d->flags & 0x10) == 0 && !prop(d, 0x40)) {
                if (NDrone2_CoverAvailable(dcv, 1) != 0) return set_state(dcv, 0x8c);
                dcv->drone->state_a = ~dcv->drone->state_a;
                if (dcv->drone->state_a != 0) {
                    switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
                        case 0: case 1: case 2:
                            return set_state(dcv, 0x58);
                        case 10:
                            return 1;
                        default:
                            break;
                    }
                } else if (NDrone2_SetGoalToDroneWithLosToOpponent(dcv) != 0) {
                    return set_state(dcv, 99);
                }
            }
            if (sees(dcv->drone)) {
                if (Rand_Rand(2) != 0 && dcv->drone->opp_dist < dcv->drone->range_f0) return set_state(dcv, 0x67);
                if (NDrone2_SetGoalToCloserPosition(dcv) != 0) return set_state(dcv, 99);
            }
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        }
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x001650a0 NDrone2_DSTATE_NoOpponent
// Sem oponente: em alerta, volta ao ataque quando acha um (a cada tick fora dos bots, ou a cada
// segundo).
uint32_t NDrone2_DSTATE_NoOpponent(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            Drone_AlertStatusSet(0, dcv);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            if (!bots_on()) set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        case 0:
            return 1;
        case 3:
            if (!bots_on() && dcv->drone->opponent != nullptr) return set_state(dcv, 0x56);
            return 1;
        case 0xc:
            if (dcv->drone->opponent != nullptr) return set_state(dcv, 0x56);
            set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173390 NDrone2_DSTATE_Obstructed
// Caminho bloqueado: espera um segundo em alerta e volta ao estado guardado (return_state).
uint32_t NDrone2_DSTATE_Obstructed(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT);
            NDrone2_InvalidateAttackRoute(dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0xc:
            set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->return_state))));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00165ef0 NDrone2_DSTATE_DroneStuck
// Preso: vai a 0x5f e volta ao ataque quando o alvo das rotas até o jogador for recalculado depois da
// rota dele.
uint32_t NDrone2_DSTATE_DroneStuck(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            set_state(dcv, 0x5f);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (dcv->drone->route.frame < NPCGlobals.player_target.frame) return set_state(dcv, 0x56);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// ---- estados: mirar e atirar ----

// [decomp] 0x00166000 NDrone2_DSTATE_DrawWeapon
// Saca a arma secundária (animação 0x46) e volta ao estado inicial; sem ela, foge com medo (0x12).
uint32_t NDrone2_DSTATE_DrawWeapon(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x66, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            Drone_tag* d = dcv->drone;
            if (static_cast<int8_t>(d->weapon2) < 2) return set_state(dcv, 0x12);
            d->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x46, 0, static_cast<int16_t>(dcv->drone->initial_state), 0, 0, dcv);
            return 1;
        }
        case 2:
            if (weapon_in_hand(dcv->obj) < 2) DroneWeap_ChangeWeapon(dcv, 6);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x66);
        default:
            return 0;
    }
}

// [decomp] 0x00166178 NDrone2_DSTATE_AimStand
// Mira parado (animação 10); quando a mira fica pronta (mensagem 5 no fim da animação) atira (0x68)
// ou, sem ver o oponente há 16 ticks, procura (0x5c). Desarmado saca (0x66); perto, corpo a corpo (0x72).
uint32_t NDrone2_DSTATE_AimStand(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            if (NDrone2_ObjectIsArmed(dcv->obj) == 0) return set_state(dcv, 0x66);
            if (NDrone2_CanAltAttack(dcv, 0) != 0) return set_state(dcv, 0x72);
            captain_weapon(dcv, drone);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 5, 0, dcv);
            drone->firing = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 5:
            if (dcv->drone->lost_ticks < 0x10) {
                set_state(dcv, 0x68);
                return 1;
            }
            return set_state(dcv, 0x5c);
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

// [decomp] 0x00166328 NDrone2_DSTATE_AimStandFire
// Atira parado: gira para o oponente, recua se ele chega a 4 m, ataca corpo a corpo a 1,5 m e consulta
// DroneFunc_CombatState (recarregar, cobertura, granada, movimento). A subclasse 0x13 espera um
// segundo antes do primeiro tiro.
uint32_t NDrone2_DSTATE_AimStandFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1: {
            if (switch_BLIND_DRONES != 0) return set_state(dcv, 4);
            Drone_AlertStatusSet(1, dcv);
            Drone_tag* d = dcv->drone;
            if (d->type == 2) return set_state(dcv, 0x2b);
            if ((d->flags & 0x10) != 0 || prop(d, 0x40)) return set_state(dcv, 0x59);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            captain_weapon(dcv, drone);
            NDrone2_AttackTalk(dcv);
            if (drone->subclass == 0x13) {
                drone->firing = 0;
                set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT);
                return 1;
            }
            if (dcv->drone->lost_ticks < 0x10) {
                DroneWeap_Fire(1, dcv);
                return 1;
            }
            return set_state(dcv, 0x5c);
        }
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 0:
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, 0.5235988f);
            if (NDrone2_CanAltAttack(dcv, 0) != 0) return set_state(dcv, 0x72);
            Drone_tag* d = dcv->drone;
            if ((d->flags & 0x10) == 0 && !prop(d, 0x40) && dcv->drone->opp_dist <= 4.0f &&
                NDrone2_CanBackoff(dcv) != 0) {
                return set_state(dcv, 0x6d);
            }
            if (0xf < dcv->drone->lost_ticks) return set_state(dcv, 0x5c);
            if (NDrone2_HasOpponent(dcv) == 0) return set_state(dcv, 0x60);
            if (drone->anim_kind == 2) drone->no_move = 1;
            if (drone->firing != 0) fire_if_clear(drone);
            if (dcv->drone->burst_over == 0 && drone->firing != 0) {
                DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            dcv->sm->result = DroneFunc_CombatState(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            fire_if_clear(drone);
            if (drone->gun_in_wall == 0) DroneWeap_Fire(1, dcv);
            return 1;
        }
        case 0xc:
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x001666d8 NDrone2_DSTATE_AimStandReload
// Recarrega (animação 0x52; uma vez em 30 a 0x4a) e volta a atirar (0x68); o pente enche.
uint32_t NDrone2_DSTATE_AimStandReload(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->firing = 0;
            if (Rand_Rand(0x1e) == 0 && DroneAnim_CanDoAnimState(dcv, 0x4a, 0)) {
                DroneAnim_CallAnim(0.0f, 0x4a, 0, 0x68, 0, 0, dcv);
            } else {
                DroneAnim_CallAnim(0.0f, 0x52, 0, 0x68, 0, 0, dcv);
            }
            dcv->drone->clip = dcv->drone->clip_size;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// [decomp] 0x00166840 NDrone2_DSTATE_AimStandDiscard
// Joga a arma fora (animação 0x45) e vai sacar a outra (0x66).
uint32_t NDrone2_DSTATE_AimStandDiscard(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    const uint32_t back = dcv->drone->initial_state;
    if (impact_messages(dcv, msg, back, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, 0x45, 0, 0x66, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(back));
        default:
            return 0;
    }
}

// [decomp] 0x00166940 NDrone2_DSTATE_Prone
// Deita (animação 0x1d) e passa a atirar deitado (0x6c).
uint32_t NDrone2_DSTATE_Prone(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x6c, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x1d, 0, 0x6c, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            dcv->drone->no_move = 1;
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x6c);
        default:
            return 0;
    }
}

// [decomp] 0x00166a28 NDrone2_DSTATE_ProneFire
// Atira deitado enquanto vê o oponente.
uint32_t NDrone2_DSTATE_ProneFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (switch_BLIND_DRONES != 0) return set_state(dcv, 4);
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            if (NDrone2_HasOpponent(dcv) == 0) return set_state(dcv, 0x60);
            Drone_tag* d = dcv->drone;
            if (d->burst_over == 0 && drone->firing != 0) return 1;
            d->firing = d->lost_ticks < 0x10 ? (drone->gun_in_wall == 0 ? 1 : 0) : 0;
            if (drone->firing != 0) DroneWeap_Fire(1, dcv);
            return 1;
        }
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00166b98 NDrone2_DSTATE_AimBackoff
// Recua mirando (animação 4) enquanto o oponente está no alcance e à vista; desvia se ele mira de
// frente; ao fim (temporizador 1, 2 a 5 s) ou sem condições, volta a mirar parado (0x67).
uint32_t NDrone2_DSTATE_AimBackoff(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 4, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + (Rand_Rand(4) + 2) * FRAME_RATE_INT);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0:
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            Drone_tag* d = dcv->drone;
            if (d->opp_dist <= d->range_f0 && d->lost_ticks < 0x10 && NDrone2_CanBackoff(dcv) != 0) {
                if (NDrone2_EvasiveMove(dcv) != 0) return 1;
                DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
                if (dcv->drone->burst_over == 0) {
                    if (drone->firing != 0) {
                        DroneAnim_PlayFiringAnim(dcv);
                        return 1;
                    }
                } else if (drone->firing != 0) {
                    DroneWeap_Fire(1, dcv);
                }
                set_state(dcv, NDrone2_ChooseCombatMove(dcv));
                return 1;
            }
            return set_state(dcv, 0x67);
        }
        case 0xc:
            set_state(dcv, 0x67);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

// [decomp] 0x00166d90 NDrone2_DSTATE_CrouchCover
// Agacha e se protege (animação 0x11) por 2 a 3 s e volta a mirar (0x67).
uint32_t NDrone2_DSTATE_CrouchCover(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x67, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(0, dcv);
            DroneAnim_CallAnim(0.0f, 0x11, 0, 0, 0, 0, dcv);
            const uint32_t r = Rand_Rand(FRAME_RATE_INT);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 2 + r);
            return 1;
        }
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (drone->klass != 0xc) DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, 0.5235988f);
            return 1;
        case 0xc:
            set_state(dcv, 0x67);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x67);
        default:
            return 0;
    }
}

// [decomp] 0x00166f28 NDrone2_DSTATE_AimCrouch
// Agacha para mirar (animação 5) e atira agachado (0x70); o tipo 0x11 só se protege (0x6e).
uint32_t NDrone2_DSTATE_AimCrouch(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            if (dcv->drone->type != 0x11) {
                DroneAnim_CallAnim(0.0f, 5, 0, 0x70, 0, 0, dcv);
                return 1;
            }
            return set_state(dcv, 0x6e);
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00167060 NDrone2_DSTATE_AimCrouchFire
// Atira agachado; depois de um segundo no estado consulta DroneFunc_CombatState; sem balas recarrega
// (0x71).
uint32_t NDrone2_DSTATE_AimCrouchFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            if (switch_BLIND_DRONES != 0) return set_state(dcv, 4);
            Drone_AlertStatusSet(1, dcv);
            if (dcv->drone->type == 2) return set_state(dcv, 0x2b);
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            DroneAnim_CallAnim(0.0f, 5, 0, 0, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 2:
            drone->anim_param = 0.0f;
            return 1;
        case 0:
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (NDrone2_CanAltAttack(dcv, 0) != 0) return set_state(dcv, 0x72);
            Drone_tag* d = dcv->drone;
            if ((d->flags & 0x10) == 0 && !prop(d, 0x40) && dcv->drone->opp_dist <= 4.0f &&
                NDrone2_CanBackoff(dcv) != 0) {
                return set_state(dcv, 0x6d);
            }
            if (NDrone2_HasOpponent(dcv) == 0) return set_state(dcv, 0x60);
            if (drone->firing != 0) fire_if_clear(drone);
            if (dcv->drone->burst_over == 0 && drone->firing != 0) return 1;
            if (float(GameState.frame) < float(drone->sm.entered) + FRAME_RATE_DIV * 60.0f) return 1;
            dcv->sm->result = DroneFunc_CombatState(dcv);
            uint32_t next = dcv->sm->result;
            if (next == 0) {
                if (0 < static_cast<int16_t>(dcv->drone->clip)) {
                    fire_if_clear(drone);
                    if (drone->gun_in_wall == 0) DroneWeap_Fire(1, dcv);
                    return 1;
                }
                next = 0x71;
            }
            return set_state(dcv, next);
        }
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00167370 NDrone2_DSTATE_AimCrouchReload
// Recarrega agachado (animação 0x52) e volta a atirar agachado (0x70).
uint32_t NDrone2_DSTATE_AimCrouchReload(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->clip = dcv->drone->clip_size;
            dcv->drone->firing = 0;
            DroneAnim_CallAnim(0.0f, 0x52, 0, 0x70, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// [decomp] 0x00167488 NDrone2_DSTATE_AltAttack
// Ataque corpo a corpo (animação 0x3d, variante sorteada) e volta a atirar (0x68).
uint32_t NDrone2_DSTATE_AltAttack(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x3d, Rand_Rand(4), 0x68, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

namespace {

// StepAimLeft/Right: um passo de lado mirando (animação 0x5a/0x5b) e volta a atirar (0x68).
uint32_t step_aim(DCVars_tag* dcv, MsgObject* msg, uint16_t call) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, call, 0, 0x68, 0, 0, dcv);
            dcv->drone->firing = 0;
            return 1;
        case 3:
            if (!DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(call), 0)) return set_state(dcv, 0x68);
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

// StrafeAimLeft/Right: anda de lado atirando (animação 0xc/0xd) por 2 a 5 s.
uint32_t strafe_aim(DCVars_tag* dcv, Drone_tag* drone, MsgObject* msg, uint16_t call,
                    uint32_t (*can)(DCVars_tag*)) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + (Rand_Rand(4) + 2) * FRAME_RATE_INT);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
            if (drone->anim_kind == 2) {
                if (can(dcv) == 0) return set_state(dcv, 0x67);
                if (0xf < dcv->drone->lost_ticks) return set_state(dcv, 0x67);
            }
            if (dcv->drone->burst_over == 0) {
                if (drone->firing != 0) DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            if (drone->firing != 0) DroneWeap_Fire(1, dcv);
            return 1;
        case 0xc:
            set_state(dcv, 0x67);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

// StrafeDodgeLeft/Right: desvio de lado (animação 0x29 ou 0xd) de 2 a 3 s, sem levar dano
// (hit_damage_mul 0).
uint32_t strafe_dodge(DCVars_tag* dcv, Drone_tag* drone, MsgObject* msg, uint16_t call,
                      uint32_t (*can)(DCVars_tag*)) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + (Rand_Rand(2) + 2) * FRAME_RATE_INT);
            dcv->drone->firing = 0;
            dcv->drone->hit_damage_mul = 0.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
            if (drone->anim_kind != 2) return 1;
            if (can(dcv) != 0 && dcv->drone->lost_ticks < 0x10) return 1;
            return set_state(dcv, 0x67);
        case 0xc:
            set_state(dcv, 0x67);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

// RollLeft/RightCrouch: rola de lado (animação 0x53/0x54) e atira agachado (0x70), com metade do dano.
uint32_t roll_crouch(DCVars_tag* dcv, MsgObject* msg, uint16_t call, uint32_t variant) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x68, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, call, variant, 0x70, 0, 0, dcv);
            dcv->drone->hit_damage_mul = 0.5f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x68);
        default:
            return 0;
    }
}

}  // namespace

// [decomp] 0x00167590 NDrone2_DSTATE_StepAimLeft
uint32_t NDrone2_DSTATE_StepAimLeft(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return step_aim(dcv, msg, 0x5a);
}

// [decomp] 0x001676a0 NDrone2_DSTATE_StepAimRight
uint32_t NDrone2_DSTATE_StepAimRight(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return step_aim(dcv, msg, 0x5b);
}

// [decomp] 0x001677b0 NDrone2_DSTATE_StrafeAimLeft
uint32_t NDrone2_DSTATE_StrafeAimLeft(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return strafe_aim(dcv, drone, msg, 0xc, NDrone2_CanStrafeLeft);
}

// [decomp] 0x00167980 NDrone2_DSTATE_StrafeAimRight
uint32_t NDrone2_DSTATE_StrafeAimRight(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return strafe_aim(dcv, drone, msg, 0xd, NDrone2_CanStrafeRight);
}

// [decomp] 0x00167b50 NDrone2_DSTATE_StrafeDodgeLeft
uint32_t NDrone2_DSTATE_StrafeDodgeLeft(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return strafe_dodge(dcv, drone, msg, 0x29, NDrone2_CanStrafeDodgeLeft);
}

// [decomp] 0x00167cf0 NDrone2_DSTATE_StrafeDodgeRight
// Peculiaridade mantida: usa a animação de andar de lado (0xd), não a de desvio (0x2a).
uint32_t NDrone2_DSTATE_StrafeDodgeRight(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return strafe_dodge(dcv, drone, msg, 0xd, NDrone2_CanStrafeDodgeRight);
}

// [decomp] 0x00167e90 NDrone2_DSTATE_RollLeftCrouch
uint32_t NDrone2_DSTATE_RollLeftCrouch(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return roll_crouch(dcv, msg, 0x53, 0);
}

// [decomp] 0x00167f80 NDrone2_DSTATE_RollRightCrouch
uint32_t NDrone2_DSTATE_RollRightCrouch(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return roll_crouch(dcv, msg, 0x54, 1);
}

// ---- cobertura: animações, giro e falas ----

uint16_t CCModeTables[kCCModeTableSize / 2];
P32<uint16_t> CCModes[10];
uint16_t LCModeCrouch[6];
uint16_t* LCModes = LCModeCrouch;

namespace {

// A animação do passo `step` do modo de cobertura: numa quina (tipo 0) pela tabela do modo e do lado
// (cover_kind 1/2), nas outras pela tabela da cobertura baixa. Um índice fora da tabela (lado 0) daria
// lixo no original: aqui não há animação (0xffff, contado em g_drone_combat_guards).
uint16_t cover_mode_anim(Drone_tag* d, uint32_t mode, uint32_t step) {
    if (d->cover->type != 0) return LCModes[step];
    const int32_t i = static_cast<int32_t>(mode * 2 + (d->cover_kind & 3)) - 1;
    if (i < 0 || 9 < i || 5 < step) {
        ++g_drone_combat_guards;
        return 0xffff;
    }
    return CCModes[i][step];
}

// A animação existe (0 conta como existente): DroneAnimStates abaixo de 0x77 com script na subclasse
// do personagem ou na 0.
bool cover_anim_ok(DCVars_tag* dcv, uint16_t anim) {
    if (anim == 0) return true;
    if (0x76 < anim) return false;
    return DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(anim), 0);
}

// Agacha (não fica de pé) atrás da cobertura: o mesmo teste de DroneAnim_ForceCrouchCover.
bool cover_crouches(DCVars_tag* dcv, CoverNode_tag* node) {
    if (prop(dcv->drone, 0x10)) return true;
    return node->type == 0 && (node->flags & 8) == 0;
}

// Uma fala de cobertura (DroneFunc_UnderCoverTalk e afins). Peculiaridade mantida: só falam com o
// multijogador ligado (MPSettings +0x184), ao contrário das outras falas.
void cover_talk(DCVars_tag* dcv, uint32_t (*pick)(uint32_t level, Drone_tag* d)) {
    if (!bots_on()) return;
    Drone_tag* d = dcv->drone;
    if (Drone_IsTalking(d) != 0) return;
    if (d->health <= 0.0f) return;
    const uint32_t sfx = pick(GameState.level_file, d);
    if (sfx != 0 && Rand_Rand(0) == 0) {
        NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
        NPCGlobals.injured_talk_time = GameState.frame + FRAME_RATE_INT * 5;
    }
}

// A cobertura sumiu (o port evita ler o endereço 0, contado em g_drone_combat_guards).
bool no_cover(Drone_tag* d) {
    if (d->cover != nullptr) return false;
    ++g_drone_combat_guards;
    return true;
}

}  // namespace

// [decomp] 0x0013bf40 DroneAnim_ValidateCoverAnim
// A animação de cobertura existe; se não, a de agachar mirando (5) vira 7 (ou 10). Peculiaridade
// mantida: outras animações inexistentes voltam como estão.
uint16_t DroneAnim_ValidateCoverAnim(DCVars_tag* dcv, uint16_t anim) {
    if (!cover_anim_ok(dcv, anim) && anim == 5) return DroneAnim_CanDoAnimState(dcv, 7, 0) ? 7 : 10;
    return anim;
}

// [decomp] 0x0013c078 DroneAnim_CoverAnim
// A animação de cobertura do passo `step` (0/6 entrar, 1 parado, 2 mirar, 3 atirar, 4 voltar, 5 sair,
// 7 trocar de modo), terminando no estado end_state. Numa quina, entrar sorteia o modo (inclinar ou
// sair, agachado ou de pé, ou usá-la como baixa) entre os que a animação permite; trocar sorteia outro
// (com o oponente a mais de 8 m também os agachados) e passa pela animação de troca. Na baixa, o passo
// 5 vai direto ao estado. 0 se não há animação.
uint32_t DroneAnim_CoverAnim(DCVars_tag* dcv, uint8_t step, int16_t end_state) {
    Drone_tag* d = dcv->drone;
    if (no_cover(d)) return 0;
    uint8_t opts[10] = {};
    uint16_t anim;
    if (d->cover->type != 1) {
        CoverNode_tag* node = d->cover;
        if (step == 6) {
            d->cover_kind &= 3;
            d->cover_mode = 2;
            uint32_t count = 0;
            if (cover_crouches(dcv, node)) {
                if (DroneAnim_CanCrouchLean(dcv, d->cover) != 0) opts[count++] = 0;
                if (DroneAnim_CanCrouchStepOut(dcv, d->cover) != 0) opts[count++] = 1;
            } else {
                if (DroneAnim_CanStandLean(dcv, d->cover) != 0) opts[count++] = 2;
                if (DroneAnim_CanStandStepOut(dcv, d->cover) != 0) opts[count++] = 3;
            }
            if (count == 0) {
                if (DroneAnim_CanUseCornerAsLowCover(dcv, d->cover) == 0) return 0;
                count = 1;
                opts[0] = 4;
            }
            dcv->drone->cover_mode = opts[Rand_Rand(count)];
            dcv->drone->cover_step = 0;
            anim = cover_mode_anim(dcv->drone, dcv->drone->cover_mode, 0);
        } else if (step != 7) {
            d->cover_step = step;
            anim = cover_mode_anim(dcv->drone, dcv->drone->cover_mode, step);
        } else {
            if (Rand_Rand(2) != 0) return 0;
            uint32_t count = 0;
            if (cover_crouches(dcv, node)) {
                if (DroneAnim_CanCrouchLean(dcv, d->cover) != 0) opts[count++] = 0;
                if (DroneAnim_CanCrouchStepOut(dcv, d->cover) != 0) opts[count++] = 1;
            }
            if (count == 0) {
                if (8.0f < dcv->drone->opp_dist) {
                    if (DroneAnim_CanCrouchLean(dcv, d->cover) != 0) {
                        opts[0] = 0;
                        count = 1;
                    }
                    if (DroneAnim_CanCrouchStepOut(dcv, d->cover) != 0) opts[count++] = 1;
                }
                if (DroneAnim_CanStandLean(dcv, d->cover) != 0) opts[count++] = 2;
                if (DroneAnim_CanStandStepOut(dcv, d->cover) != 0) opts[count++] = 3;
                if (count == 0) {
                    if (DroneAnim_CanUseCornerAsLowCover(dcv, d->cover) == 0) return 0;
                    count = 1;
                    opts[0] = 4;
                }
            }
            const uint8_t pick = opts[Rand_Rand(count)];
            if (pick == dcv->drone->cover_mode) return 0;
            dcv->drone->cover_step = 5;
            anim = cover_mode_anim(dcv->drone, dcv->drone->cover_mode, 5);
            if (anim == 0xffff) {
                dcv->drone->cover_step = 0;
                anim = cover_mode_anim(dcv->drone, pick, 0);
                dcv->drone->cover_step = 5;
            }
            dcv->drone->cover_mode = pick;
        }
        if (anim == 0xffff) return 0;
        if (!cover_anim_ok(dcv, anim)) return 0;
    } else {
        switch (step) {
            case 0:
            case 6:
                dcv->drone->cover_kind &= 3;
                dcv->drone->cover_mode = 0;
                dcv->drone->cover_step = 0;
                anim = DroneAnim_ValidateCoverAnim(dcv, d->cover->low_a);
                break;
            case 1:
            case 4:
                dcv->drone->cover_step = step;
                anim = DroneAnim_ValidateCoverAnim(dcv, d->cover->low_a);
                break;
            case 2:
                dcv->drone->cover_step = step;
                anim = DroneAnim_ValidateCoverAnim(dcv, d->cover->low_b);
                break;
            case 3:
                anim = 0x58;
                dcv->drone->cover_step = step;
                break;
            case 5:
                dcv->drone->cover_kind |= 8;
                return set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(end_state)));
            default:
                return 0;
        }
        if (anim == 0xffff) return 0;
    }
    DroneAnim_CallAnim(0.0f, anim, 0, end_state, 0, 0, dcv);
    return 1;
}

// [decomp] 0x00151900 NDrone2_SetAngleToCover
// Vira o alvo do giro para o lado da cobertura (a direção dela, -90 graus no lado 1; na baixa e usando
// a quina como baixa, para o oponente). 1 se virou para a cobertura. Peculiaridade mantida: o
// deslocamento pedido não é usado.
uint32_t NDrone2_SetAngleToCover(DCVars_tag* dcv, float) {
    Drone_tag* d = dcv->drone;
    CoverNode_tag* node = d->cover;
    if (node == nullptr || d->opponent == nullptr) return 0;
    float add = node->type == 1 ? -kPi : 0.0f;
    if ((d->cover_kind & 3) == 1) add = -kHalfPi;
    float yaw = add + node->rot.y;
    if (d->cover_mode == 4) yaw = yaw - kPi;
    const float dx = d->opponent->pos.x - node->pos.x;
    const float dz = d->opponent->pos.z - node->pos.z;
    if (d->cover->type == 1 || d->cover_mode == 4) {
        dcv->drone->move_angles.y = ATAN2_APPROX(dx, dz);
        return 0;
    }
    d->move_angles.y = yaw;
    return 1;
}

// [decomp] 0x00154bd0 NDrone2_SlideToPosition
// Escorrega o objeto uma fração `t` até pos (no plano; com `with_y` também na altura); a menos de 1 cm,
// encosta.
void NDrone2_SlideToPosition(_VECTOR* pos, float t, int8_t with_y, DCVars_tag* dcv) {
    obj_tag* o = dcv->obj;
    if (0.01f < Vec_Dist3D(&o->pos, pos)) {
        o->pos.x = o->pos.x + (pos->x - o->pos.x) * t;
        o->pos.z = o->pos.z + (pos->z - o->pos.z) * t;
        if (with_y != 0) o->pos.y = o->pos.y + (pos->y - o->pos.y) * t;
    } else {
        o->pos = *pos;
    }
}

// [decomp] 0x0013fd08 DroneFunc_RunForCoverTalk
void DroneFunc_RunForCoverTalk(DCVars_tag* dcv) {
    cover_talk(dcv, [](uint32_t level, Drone_tag* d) -> uint32_t {
        switch (level) {
            case 0x700000c: case 0x700000d:
                return Rand_Rand(2) + 0xd0;
            case 0x7000011: case 0x7000012: case 0x7000013: case 0x700004a:
                return 0xbd;
            case 0x7000014: case 0x7000015: case 0x7000016: {
                if (d->klass == 0x10) return 0;
                const uint32_t r = Rand_Rand(7);
                return (d->sm.id & 1) != 0 ? r + 0x122 : r + 0x11b;
            }
            default:
                return 0;
        }
    });
}

// [decomp] 0x0013fe80 DroneFunc_UnderCoverTalk
void DroneFunc_UnderCoverTalk(DCVars_tag* dcv) {
    cover_talk(dcv, [](uint32_t level, Drone_tag*) -> uint32_t {
        if (level - 0x7000011u < 3 || level == 0x700004a) return Rand_Rand(2) + 0xbd;
        if (0x700000b < level && level < 0x700000e) return Rand_Rand(3) + 0xcf;
        return 0;
    });
}

// [decomp] 0x0013fb98 DroneFunc_UnderCoverAimTalk
void DroneFunc_UnderCoverAimTalk(DCVars_tag* dcv) {
    cover_talk(dcv, [](uint32_t level, Drone_tag*) -> uint32_t {
        if (level - 0x7000011u < 3 || level == 0x700004a) return Rand_Rand(2) + 0xba;
        if (0x700000b < level && level < 0x700000e) return Rand_Rand(2) + 0xd0;
        return 0;
    });
}

// ---- portas e nós com ação ----

// [decomp] 0x0014aa38 NDrone2_OpenDoor
// Abre a porta guardada (tipo 0x1c fechada ou fechando: estado 0x12) como se a usasse.
void NDrone2_OpenDoor(DCVars_tag* dcv) {
    obj_tag* door = dcv->drone->door;
    if (door == nullptr || door->type != 0x1c || (door->state & 0x12) == 0) return;
    Door_Activate(door, dcv->obj, 0);
    dcv->drone->door_time = GameState.frame;
}

// [decomp] 0x0014aaa0 NDrone2_DoorIsOpen
// A porta da rota já abriu (estado 4); sem rota ou sem porta, 1.
uint32_t NDrone2_DoorIsOpen(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->active_route != nullptr && d->door != nullptr && d->door->type == 0x1c) return d->door->state == 4 ? 1 : 0;
    return 1;
}

// [decomp] 0x0014a940 NDrone2_FindElevatorDoor
// A porta fechada (estado 2) mais perto, a menos de 2 m, vira a porta do personagem (atiradores do
// elevador de 0700004a).
void NDrone2_FindElevatorDoor(DCVars_tag* dcv) {
    obj_tag* best = nullptr;
    float best_sq = 4.0f;
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 0x1c || (o->state & 2) == 0) continue;
        const float dx = o->pos.x - dcv->obj->pos.x;
        const float dy = o->pos.y - dcv->obj->pos.y;
        const float dz = o->pos.z - dcv->obj->pos.z;
        const float sq = dx * dx + dy * dy + dz * dz;
        if (sq < best_sq) {
            best = o;
            best_sq = sq;
        }
    }
    dcv->drone->door = best;
}

// [decomp] 0x0014aa00 NDrone2_KickObject
// Chuta o objeto do nó de chute (tipo 0x1b): o script dele volta a tocar.
void NDrone2_KickObject(DCVars_tag* dcv) {
    obj_tag* o = dcv->drone->door;
    if (o != nullptr && o->type == 0x1b) SP_UnPause(o);
}

// [decomp] 0x001519e0 NDrone2_MoveTo_RouteNode
// move_target vai para o nó da rota `delta` adiante do atual (antes do primeiro, a origem; depois do
// último, o destino).
void NDrone2_MoveTo_RouteNode(DCVars_tag* dcv, int16_t delta, AIRoute_tag* route) {
    const int32_t i = AINetwork_RouteNodeOffset(route->current, delta, route);
    CelPos_tag target{};
    if (i < 0) {
        target = route->from;
    } else if (i < static_cast<int32_t>(route->count)) {
        const MAPNODE_tag* node = route->path->nodes[route->list[i]].node;
        target.pos = node->pos;
        target.cel = node->cel;
    } else {
        target = route->to;
    }
    Drone_tag* d = dcv->drone;
    d->move_target.pos = target.pos;
    if (target.cel == nullptr) target.cel = NDrone2_FindCel(&target.pos);
    dcv->drone->move_target.cel = target.cel;
    dcv->drone->move_arrive = 0.3f;
    _VECTOR feet;
    NDrone2_FeetPos(dcv->obj, &feet);
    dcv->drone->move_dist = Vec_Dist2D(&feet, &dcv->drone->move_target.pos);
    NDrone2_GetAnglesToMoveTarget(dcv, &dcv->drone->move_angles);
}

// [decomp] 0x00153358 NDrone2_ReachedDestNode
// O personagem chegou a um nó da rota com ação (LinkCreep_Handler): falas do Mayhew (bits 0x10000-
// 0x80000), esperar (bit 1, calmo: a mensagem 4 a si mesmo, uma vez por nó), abrir a porta (2: estado
// 0x81 quando o nó seguinte também é de porta; depois de aberta, esquece a porta), chutar (4: 0x82, uma
// vez por nó), animação de ação (8: 0x83), esperar a missão (0x20, canal 0x99 desligado: 0x21) ou mexer
// em algo (0x40: 0x84). O estado de antes fica em node_return_state. Depois do último nó, para de
// andar pela ligação.
void NDrone2_ReachedDestNode(AIRoute_tag* route, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (route->current < 0) return;
    const uint16_t cur = static_cast<uint16_t>(route->current);
    if (route->count <= cur) {
        std::memset(reinterpret_cast<uint8_t*>(route) + 0xa0, 0, 0x50);
        return;
    }
    const uint32_t next_i = cur + 1u < route->count ? cur + 1u : 0xffff;
    NODE_tag* here = &route->path->nodes[route->list[cur]];
    MAPNODE_tag* node = here->node;
    MAPNODE_tag* next = next_i != 0xffff ? route->path->nodes[route->list[next_i]].node : nullptr;
    if ((node->flags & 0x10000) != 0) Drone_MayhewTalk(0x44);
    if ((node->flags & 0x40000) != 0) Drone_MayhewTalk(0x46);
    if ((node->flags & 0x20000) != 0) Drone_MayhewTalk(0x45);
    if ((node->flags & 0x80000) != 0) Drone_MayhewTalk(0x47);
    if ((node->flags & 1) != 0 && dcv->drone->alert < 0.33f) {
        if (cur != d->wait_node) {
            d->wait_node = cur;
            Drone_SM_SendMsgSelf(4, 0, 1, dcv->drone->sm.state, dcv);
            return;
        }
        d->wait_node = 0xffff;
    }
    const uint32_t f = node->flags;
    StateMachineInfo_tag* sm = dcv->sm;
    const auto go = [&](uint32_t state) {
        if (sm->state != state) d->node_return_state = static_cast<uint16_t>(sm->state);
        Drone_SM_SetState(dcv->sm, state, 0);
    };
    if ((f & 2) != 0) {
        obj_tag* door;
        if (next != nullptr && (next->flags & 2) != 0) {
            door = here->door;
            if (door == d->door) return;
            d->door = door;
            d->action_node = cur;
            go(0x81);
            return;
        }
        door = d->door;
        if (door == nullptr) return;
        if (NDrone2_DoorIsOpen(dcv) == 0) return;
        d->door = nullptr;
        return;
    }
    if ((f & 4) != 0) {
        if (next == nullptr) {
            d->door = nullptr;
            return;
        }
        if ((next->flags & 4) != 0) {
            obj_tag* o = here->door;
            if (o == d->door) return;
            d->door = o;
            d->action_node = cur;
            if ((node->flags & 0x8000) == 0) {
                node->flags |= 0x8000;
                go(0x82);
            }
            return;
        }
        d->door = nullptr;
        return;
    }
    if ((f & 8) != 0) {
        d->action_node = cur;
        go(0x83);
        return;
    }
    if ((f & 0x20) != 0 && switch_channels[0x99] == 0) {
        if (sm->state == 0x21) return;
        d->initial_state = static_cast<uint16_t>(sm->state);
        Drone_SM_SetState(dcv->sm, 0x21, 0);
        return;
    }
    if ((f & 0x40) != 0) {
        d->action_node = cur;
        go(0x84);
    }
}

// [decomp] 0x0013e088 DroneAnim_SetScript
// Toca um script direto (sem DAnim): a chamada fica no estado de animação anim_state, a DAnim seguinte,
// o parâmetro e o estado e a mensagem de fim. 1 se havia script.
uint32_t DroneAnim_SetScript(uint32_t script, uint16_t anim_state, uint16_t next, uint8_t end_param, int16_t end_state,
                             uint32_t end_msg, uint32_t flags, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    std::memset(&d->call_state, 0, 0x10);
    d->anim_state = anim_state;
    d->anim_state_kind = 0x63;
    d->end_danim = next;
    d->end_param = end_param;
    d->end_state = static_cast<uint16_t>(end_state);
    d->move_speed = 0.0f;
    d->anim_call_done = 1;
    d->anim_param = 0.0f;
    d->call_param = 0.0f;
    d->anim_kind = 1;
    d->danim = 0;
    d->end_msg = end_msg;
    d->anim_info_10 = 0;
    d->anim_info_f = 0;
    d->anim_flags = flags;
    if (script != 0) {
        DroneAnim_SetHTAnim(script, flags, 0, d, 8, 1.0f);
        d->end_msg = end_msg;
        DroneAnim_OnStartAnim(d);
    }
    return script != 0 ? 1 : 0;
}

// ---- estados: cobertura ----

// [decomp] 0x00169150 NDrone2_DSTATE_RunForCover
// Corre até a cobertura achada (goal_point) com a animação de combate, escorregando para o lugar no
// último metro; se ela deixa de servir, ou o oponente aparece a menos de 4 m, volta ao ataque.
uint32_t NDrone2_DSTATE_RunForCover(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            if (dcv->drone->goal_point.active != 0) {
                DroneFunc_RunForCoverTalk(dcv);
                return 1;
            }
            return set_state(dcv, 0x56);
        case 2:
            if (drone->sm.next_state != 0x93) NDrone2_DisownCover(dcv);
            dcv->drone->flags &= 0xfffff7ff;
            drone->anim_param = 0.0f;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            Drone_tag* d = dcv->drone;
            if (d->goal_point.active == 0 || no_cover(d)) return set_state(dcv, 0x56);
            if (NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 0) == 0) return set_state(dcv, 0x56);
            d = dcv->drone;
            if (sees(d) && d->opp_dist < 4.0f) return set_state(dcv, 0x56);
            switch (NDrone2_MoveToGoalPosition(dcv, &d->goal_point.pos, d->goal_point.radius, 0)) {
                case 0:
                case 2:
                    if (drone->active_route->distance < 1.0f) {
                        NDrone2_SlideToPosition(&drone->cover_pos.pos, 0.1f, 1, dcv);
                    }
                    DroneAnim_SetCombatMoveAnim(dcv, dcv->drone->active_route->distance);
                    return 1;
                case 3:
                    return set_state(dcv, 0x93);
                case 10:
                    return 1;
                case 0xc:
                    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                    if (!sees(dcv->drone)) {
                        drone->firing = 0;
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    fire_if_clear(drone);
                    DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                    if (dcv->drone->burst_over != 0) {
                        if (drone->firing != 0) DroneWeap_Fire(1, dcv);
                        return 1;
                    }
                    if (drone->firing != 0) DroneAnim_PlayFiringAnim(dcv);
                    return 1;
                default:
                    set_state(dcv, 0x56);
                    return 1;
            }
        }
        case 0x1b:
            set_state(dcv, 0x56);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

namespace {
// unknown_2e: agachado atrás de uma quina (o tipo da cobertura não é 1).
void mark_corner(DCVars_tag* dcv) { dcv->drone->unknown_2e = dcv->drone->cover->type != 1 ? 1 : 0; }
}  // namespace

// [decomp] 0x00169cc0 NDrone2_DSTATE_UnderCoverInit
// Chegou à cobertura: animação de entrar (sorteia o modo) e fica (0x94), escorregando para o lugar e
// virando para o lado dela.
uint32_t NDrone2_DSTATE_UnderCoverInit(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CoverAnim(dcv, 6, 0x94);
            if (no_cover(dcv->drone)) return 1;
            mark_corner(dcv);
            dcv->drone->flags &= 0xfffff7ff;
            DroneFunc_UnderCoverTalk(dcv);
            drone->anim_param = 0.0f;
            return 1;
        case 0:
            return 1;
        case 2:
            if (drone->sm.next_state != 0x94) NDrone2_DisownCover(dcv);
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            dcv->drone->no_move = 1;
            NDrone2_SlideToPosition(&drone->cover_pos.pos, 0.08f, 0, dcv);
            NDrone2_SetAngleToCover(dcv, kPi);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x00169e58 NDrone2_DSTATE_UnderCoverIdle
// Atrás da cobertura, de pente cheio: de tempos em tempos (até 2,5 s) sai para mirar (0x95) se ela ainda
// serve, procura outra ou, depois de três vezes, volta ao ataque. Com a propriedade 0x10 fica até o
// oponente aparecer ou o jogador chegar a 3 m. Sai (0x9c) se a cobertura deixa de servir ou ficou longe.
uint32_t NDrone2_DSTATE_UnderCoverIdle(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x9c, 0, &ret)) return ret;
    const auto rearm = [&] {
        const float r = Rand_FRand(150.0f);
        set_timer1(dcv->drone, fptoui(float(GameState.frame) + r * FRAME_RATE_DIV));
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            DroneAnim_CoverAnim(dcv, 1, 0);
            if (drone->subclass != 0x13 || 1 < GameState.level_file - 0x700000cu) {
                dcv->drone->clip = dcv->drone->clip_size;
            }
            if (!prop(dcv->drone, 0x10)) rearm();
            if (no_cover(dcv->drone)) return 1;
            mark_corner(dcv);
            if (drone->anim_kind == 2) dcv->drone->no_move = 0;
            dcv->drone->state_a = 0;
            return 1;
        }
        case 2:
            dcv->drone->unknown_2e = 0;
            behaviour_util_setProperty(0x10, dcv->drone->behaviour, 0);
            return 1;
        case 3: {
            if (!prop(dcv->drone, 0x10)) {
                NDrone2_SeenAndAttacking(dcv);
                NDrone2_SlideToPosition(&drone->cover_pos.pos, 0.08f, 0, dcv);
                NDrone2_SetAngleToCover(dcv, kPi);
                if (drone->blend_script != nullptr) return 1;
                Drone_tag* d = dcv->drone;
                if (!no_cover(d) && NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 0) != 0 &&
                    Vec_Dist2D(&dcv->obj->pos, &dcv->drone->goal_point.pos.pos) <= 1.5f) {
                    return 1;
                }
                return set_state(dcv, 0x9c);
            }
            if (!sees(dcv->drone)) {
                if (NDrone2_Player() == nullptr) return 1;
                if (3.0f <= Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos)) return 1;
            }
            behaviour_util_setProperty(0x10, dcv->drone->behaviour, 0);
            return set_state(dcv, 0x9c);
        }
        case 0xc: {
            if (Rand_Rand(5) == 0 && NDrone2_CoverAvailable(dcv, 1) != 0) return set_state(dcv, 0x8c);
            Drone_tag* d = dcv->drone;
            if (!no_cover(d) && NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 1) != 0) {
                return set_state(dcv, 0x95);
            }
            if (NDrone2_CoverAvailable(dcv, 1) != 0) return set_state(dcv, 0x8c);
            const uint32_t n = dcv->drone->state_a;
            dcv->drone->state_a = n + 1;
            if (n < 3) {
                rearm();
                return 1;
            }
            return set_state(dcv, 0x56);
        }
        case 0x21:
            return explosive_message(dcv, msg, 0x9c);
        default:
            return 0;
    }
}

// [decomp] 0x0016a2d0 NDrone2_DSTATE_UnderCoverAim
// Sai da cobertura mirando (animação do passo 2) e atira (0x96).
uint32_t NDrone2_DSTATE_UnderCoverAim(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x9c, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CoverAnim(dcv, 2, 0x96);
            if (no_cover(dcv->drone)) return 1;
            mark_corner(dcv);
            DroneFunc_UnderCoverAimTalk(dcv);
            return 1;
        case 0:
            return 1;
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x9c);
        default:
            return 0;
    }
}

// [decomp] 0x0016a3e0 NDrone2_DSTATE_UnderCoverFire
// Atira da cobertura até o temporizador 1 (posto pelo tiro) ou, com in_sights, às vezes, e volta
// (0x99); se ela deixa de servir, volta (inclinado) ou sai (0x9c). A subclasse 0x13 em 0700000c/0d atira
// como atirador de elite (0x97).
uint32_t NDrone2_DSTATE_UnderCoverFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x9c, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (drone->subclass == 0x13 && GameState.level_file - 0x700000cu < 2) return set_state(dcv, 0x97);
            dcv->drone->flags |= 0x10000;
            DroneWeap_Fire(1, dcv);
            NDrone2_AttackTalk(dcv);
            if (no_cover(dcv->drone)) return 1;
            mark_corner(dcv);
            return 1;
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            Drone_tag* d = dcv->drone;
            if (no_cover(d) || NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 1) == 0) {
                if (drone->cover != nullptr && drone->cover->type == 0 &&
                    (drone->cover_mode == 0 || drone->cover_mode == 2)) {
                    return set_state(dcv, 0x99);
                }
                return set_state(dcv, 0x9c);
            }
            if (dcv->drone->burst_over == 0 && drone->firing != 0) {
                DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            if (drone->in_sights != 0 && Rand_Rand(3) == 0) return set_state(dcv, 0x99);
            if (sees(dcv->drone)) DroneWeap_Fire(1, dcv);
            return 1;
        }
        case 0xc:
            set_state(dcv, 0x99);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x9c);
        default:
            return 0;
    }
}

// [decomp] 0x0016a610 NDrone2_DSTATE_UnderCoverSniperFire
// Atirador de elite na cobertura: atira por 6 s (temporizador 2) e volta; recarrega (0x98) sem balas;
// volta antes se o oponente mira nele ou, de frente, às vezes.
uint32_t NDrone2_DSTATE_UnderCoverSniperFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x9c, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            NDrone2_AttackTalk(dcv);
            if (!no_cover(dcv->drone)) mark_corner(dcv);
            Drone_tag* d = dcv->drone;
            set_timer1(d, GameState.frame + FRAME_RATE_INT);
            d->timer2_time = GameState.frame + FRAME_RATE_INT * 6;
            d->timer2_data = 0;
            d->timer2_on = 1;
            drone->fired = 0;
            drone->firing = 0;
            return 1;
        }
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            if (drone->fired != 0 && static_cast<int16_t>(dcv->drone->clip) <= 0) {
                return set_state(dcv, 0x98);
            }
            if (drone->anim_kind != 2) return 1;
            if (drone->firing == 0) return 1;
            Drone_tag* d = dcv->drone;
            if (no_cover(d) || NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 0) == 0) {
                if (drone->cover != nullptr && drone->cover->type == 0 &&
                    (drone->cover_mode == 0 || drone->cover_mode == 2)) {
                    return set_state(dcv, 0x99);
                }
                return set_state(dcv, 0x9c);
            }
            if (drone->burst_over == 0) return 1;
            if (DroneWeap_OpponentIsAimingAtMe(dcv, 7.5f) == 0) {
                if (NDrone2_BondIsFacingMe(dcv, 45.0f) == 0) return 1;
                if (Rand_Rand(3) != 0) return 1;
            }
            return set_state(dcv, 0x99);
        }
        case 0xc:
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0xd:
            set_state(dcv, 0x99);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x9c);
        default:
            return 0;
    }
}

// [decomp] 0x0016a8b0 NDrone2_DSTATE_UnderCoverSniperReload
// Recarrega na cobertura (animação 0x52) e volta a atirar (0x97).
uint32_t NDrone2_DSTATE_UnderCoverSniperReload(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->firing = 0;
            DroneAnim_CallAnim(0.0f, 0x52, 0, 0x97, 0, 0, dcv);
            dcv->drone->clip = dcv->drone->clip_size;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// [decomp] 0x0016a9c0 NDrone2_DSTATE_UnderCoverReturn
// Volta para trás da cobertura (animação do passo 4) e troca de modo (0x9a); sem animação, sai (0x9b).
// Longe dela ou se ela deixa de servir, marca o bit 4 de cover_kind (a troca vira saída).
uint32_t NDrone2_DSTATE_UnderCoverReturn(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x9c, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            if (DroneAnim_CoverAnim(dcv, 4, 0x9a) == 0) return set_state(dcv, 0x9b);
            mark_corner(dcv);
            DroneFunc_UnderCoverTalk(dcv);
            return 1;
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            Drone_tag* d = dcv->drone;
            if (!no_cover(d) && NDrone2_CoverNodeOK(dcv, d->cover, d->cover_kind & 3, 0) != 0 &&
                Vec_Dist2D(&dcv->obj->pos, &dcv->drone->goal_point.pos.pos) <= 1.5f) {
                return 1;
            }
            dcv->drone->cover_kind |= 4;
            return 1;
        }
        case 0x21:
            return explosive_message(dcv, msg, 0x9c);
        default:
            return 0;
    }
}

// [decomp] 0x001739b8 NDrone2_DSTATE_UnderCoverTypeChange
// Troca o modo de cobertura (metade das vezes) e fica (0x94); procura outra cobertura se esta deixou de
// servir; com o bit 4 de cover_kind, sai (0x9c).
uint32_t NDrone2_DSTATE_UnderCoverTypeChange(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            Drone_tag* d = dcv->drone;
            const uint16_t kind = d->cover_kind;
            if ((kind & 4) != 0) return set_state(dcv, 0x9c);
            if (no_cover(d)) return set_state(dcv, 0x9c);
            if (NDrone2_CoverNodeOK(dcv, d->cover, kind & 3, 1) == 0 && NDrone2_CoverAvailable(dcv, 1) != 0) {
                return set_state(dcv, 0x8c);
            }
            if (DroneAnim_CoverAnim(dcv, 7, 0x94) != 0) {
                mark_corner(dcv);
                return 1;
            }
            return set_state(dcv, 0x94);
        }
        case 0:
            return 1;
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173ac0 NDrone2_DSTATE_UnderCoverLeave
uint32_t NDrone2_DSTATE_UnderCoverLeave(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            set_state(dcv, 0x9c);
            return 1;
        case 0:
            return 1;
        case 2:
            dcv->drone->unknown_2e = 0;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016ab38 NDrone2_DSTATE_UnderCoverLeaveNow
// Larga a cobertura e volta ao ataque mirando (animação 10; saindo das DAnim 0x87/0xab, pelo lado do
// oponente).
uint32_t NDrone2_DSTATE_UnderCoverLeaveNow(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->cover_kind |= 8;
            NDrone2_DisownCover(dcv);
            dcv->drone->unknown_2e = 0;
            if (drone->danim == 0x87 || drone->danim == 0xab) {
                DroneAnim_CallAnim(0.0f, 10, 0.0f < dcv->drone->opp_bearing ? 1 : 0, 0x56, 0, 0, dcv);
            } else {
                drone->anim_state = 0;
                DroneAnim_CallAnim(0.0f, 10, 0, 0x56, 0, 0, dcv);
            }
            return 1;
        case 2:
            NDrone2_DisownCover(dcv);
            dcv->drone->unknown_2e = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// ---- estados: granada, atiradores de elite, portas e nós ----

// [decomp] 0x00173480 NDrone2_DSTATE_GrenadeThrow
// Joga uma granada (animação 0x49) e volta ao ataque. Peculiaridade mantida: os impactos 0x18/0x19 e os
// explosivos (0x21) não são tratados aqui.
uint32_t NDrone2_DSTATE_GrenadeThrow(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x49, 0, 0x56, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        case 6: case 7: case 9: case 0x17:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0015fab0 NDrone2_DSTATE_SniperIdle
// Atirador de elite parado (animação 0xe, sem sair do lugar), procurando o oponente; alertas como os
// estados de guarda; mira (0x2a) quando é atingido.
uint32_t NDrone2_DSTATE_SniperIdle(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x2a, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            dcv->drone->flags |= 0x10;
            DroneAnim_CallAnim(0.0f, 0xe, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x2a);
        default:
            return 0;
    }
}

// [decomp] 0x0015fc18 NDrone2_DSTATE_SniperAim
// Mira de longe (alcance de visão 1000 m) e atira (0x2b) quando vê o oponente; em 0700004a abre a porta
// do elevador mais perto.
uint32_t NDrone2_DSTATE_SniperAim(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x2b, 1, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x10;
            NDrone2_FindElevatorDoor(dcv);
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            dcv->drone->range_fc = 1000.0f;
            dcv->drone->range_e8 = 1000.0f;
            NDrone2_AttackTalk(dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            if (!sees(dcv->drone)) {
                if (GameState.level_file == 0x700004a) NDrone2_OpenDoor(dcv);
                return 1;
            }
            return set_state(dcv, 0x2b);
        case 0x21:
            return explosive_message(dcv, msg, 0x2b);
        default:
            return 0;
    }
}

// [decomp] 0x0015fd88 NDrone2_DSTATE_SniperFire
// Atira de longe enquanto vê o oponente; recarrega (0x2c) sem balas, volta a mirar (0x2a) sem vê-lo.
uint32_t NDrone2_DSTATE_SniperFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 1, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10;
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            dcv->drone->flags |= 0x10000;
            drone->firing = 0;
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT);
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_AttackTalk(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            if (GameState.level_file == 0x700004a) NDrone2_OpenDoor(dcv);
            if (NDrone2_CanAltAttack(dcv, 0) != 0) return set_state(dcv, 0x72);
            Drone_tag* d = dcv->drone;
            if (static_cast<int16_t>(d->clip) < 1) return set_state(dcv, 0x2c);
            if (d->lost_ticks < 0x10) {
                if (d->burst_over != 0 || drone->firing == 0) {
                    DroneWeap_Fire(1, dcv);
                    return 1;
                }
                DroneAnim_PlayFiringAnim(dcv);
                return 1;
            }
            return set_state(dcv, 0x2a);
        }
        case 0xc:
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x0015ff80 NDrone2_DSTATE_SniperReload
// Recarrega depois de 20 quadros (animação 0x52) e volta a atirar (0x2b); o pente enche ao sair.
uint32_t NDrone2_DSTATE_SniperReload(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x2b, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x10;
            set_timer1(dcv->drone, frame_plus(20.0f));
            dcv->drone->firing = 0;
            return 1;
        case 2:
            dcv->drone->clip = dcv->drone->clip_size;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
            NDrone2_OpenDoor(dcv);
            return 1;
        case 0xc:
            DroneAnim_CallAnim(0.0f, 0x52, 0, 0x2b, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x2b);
        default:
            return 0;
    }
}

// [decomp] 0x001681f8 NDrone2_DSTATE_OpenDoor
// Parado diante da porta da rota: abre (empurrando com a animação 0x4e se alerta e com as propriedades
// 0x14-0x16; senão com as mãos) e, aberta, volta ao estado de antes (node_return_state).
uint32_t NDrone2_DSTATE_OpenDoor(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    const auto back = [&] {
        return set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->node_return_state))));
    };
    const auto open_later = [&] {
        NDrone2_OpenDoor(dcv);
        set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 2);
        return 1u;
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            if (NDrone2_DoorIsOpen(dcv) != 0) return back();
            dcv->drone->no_move = 1;
            Drone_tag* d = dcv->drone;
            if (d->door != nullptr && d->door->unknown_f6 == 0 && !(d->alert_base < 0.66f && d->alert < 0.66f) &&
                (prop(d, 0x14) || prop(d, 0x15) || prop(d, 0x16))) {
                NDrone2_MoveTo_RouteNode(dcv, 1, dcv->drone->active_route);
                DroneAnim_CallAnim(0.0f, 0x4e, 0, 0, 0, 0, dcv);
                return open_later();
            }
            if (dcv->drone->alert < 1.0f) {
                DroneAnim_CallAnim(0.0f, 0x24, 1, 0, 0, 0, dcv);
            } else {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            }
            return open_later();
        }
        case 2:
            dcv->drone->no_move = 0;
            return 1;
        case 3:
            dcv->drone->no_move = 1;
            if (NDrone2_DoorIsOpen(dcv) == 0) return 1;
            return back();
        case 0xc:
            return open_later();
        case 0x20:
            NDrone2_PlaySFX(dcv, 0x27, nullptr, 1, 1.0f);
            NDrone2_OpenDoor(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x001684c0 NDrone2_DSTATE_KickObject
// Chuta o objeto do nó (animação 0x4e; no evento 0x20 da animação) e volta ao estado de antes.
uint32_t NDrone2_DSTATE_KickObject(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x4e, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 0xc:
            NDrone2_KickObject(dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 2);
            return 1;
        case 0x20:
            NDrone2_PlaySFX(dcv, 0x28, nullptr, 1, 1.0f);
            NDrone2_KickObject(dcv);
            set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->node_return_state))));
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00168628 NDrone2_DSTATE_ActionAnim
// Toca o script do nó (entry_script) sem ouvir; no fim (mensagem 5) agacha para mirar (0x6f); o evento
// 0x20 volta ao estado de antes.
uint32_t NDrone2_DSTATE_ActionAnim(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_SetScript(dcv->drone->entry_script, 10, 0x32, 0x1e, 0, 5, 0, dcv);
            dcv->drone->flags |= 0x400000;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 5:
            dcv->drone->flags &= 0xffbfffff;
            set_state(dcv, 0x6f);
            return 1;
        case 0xc:
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 2);
            return 1;
        case 0x20:
            set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->node_return_state))));
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x001687c0 NDrone2_DSTATE_StandFiddle
// Mexe em algo parado (animação 0x23) por 5 s e volta ao estado de antes.
uint32_t NDrone2_DSTATE_StandFiddle(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x23, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 5);
            return 1;
        case 3:
            dcv->drone->no_move = 1;
            return 1;
        case 0xc:
            set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->node_return_state))));
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// ---- ir até um ponto, alertas e sons ----

namespace {

// AIPoint_tag +0x10: a distância 2D dos pés até o ponto (NDrone2_DistanceToAIPoint).
float& point_dist(AIPoint_tag& p) { return *reinterpret_cast<float*>(&p.unknown_10); }

uint32_t back_to_initial(DCVars_tag* dcv) {
    return set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->initial_state))));
}

// Os alertas que os estados de guarda repassam a DroneVision_EnemyAlerts (com ou sem o 0x14, o som).
bool alert_message(DCVars_tag* dcv, MsgObject* msg, bool with_sound, uint32_t* ret) {
    switch (msg->msg) {
        case 0x14:
            if (!with_sound) return false;
            [[fallthrough]];
        case 0x11: case 0x12: case 0x15: case 0x16: case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            *ret = dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
            return true;
        default:
            return false;
    }
}

// Mensagem 0x14 (som ouvido) nos estados de procura: forte o bastante (sound_alert_step acima de 0,2) e
// sem ataque ainda, ataca (ouviu: 0x10000000).
bool loud_sound_attacks(DCVars_tag* dcv, Drone_tag* drone, uint32_t* ret) {
    if (0.2f < drone->sound_alert_step && (dcv->drone->flags & 0x4000000) == 0) {
        dcv->drone->flags |= 0x4000000;
        dcv->drone->flags |= 0x10000000;
        *ret = set_state(dcv, 0x56);
        return true;
    }
    return false;
}

}  // namespace

// [decomp] 0x00155d78 NDrone2_DistanceToAIPoint
// Guarda no ponto a distância 2D dos pés até ele.
void NDrone2_DistanceToAIPoint(DCVars_tag* dcv, AIPoint_tag* point) {
    _VECTOR feet;
    NDrone2_FeetPos(dcv->obj, &feet);
    point_dist(*point) = Vec_Dist2D(&feet, &point->pos.pos);
}

// [decomp] 0x0014a5e8 DroneFunc_Using2ndBehaviour
uint32_t DroneFunc_Using2ndBehaviour(Drone_tag* drone) { return drone->unknown_29; }

// [decomp] 0x0014a5f0 DroneFunc_Set1stBehaviour
// Volta à estrutura de comportamento normal (só no modo 100, que tem duas).
uint32_t DroneFunc_Set1stBehaviour(Drone_tag* drone) {
    if (drone->divars.params[5] != 100) return 0;
    drone->unknown_29 = 0;
    drone->behaviour = drone->behaviour_a;
    return 1;
}

// [decomp] 0x0014a618 DroneFunc_Set2ndBehaviour
uint32_t DroneFunc_Set2ndBehaviour(Drone_tag* drone) {
    if (drone->divars.params[5] != 100) return 0;
    drone->unknown_29 = 1;
    drone->behaviour = drone->behaviour_b;
    return 1;
}

// [decomp] 0x0014aae0 NDrone2_IsCivilian
uint32_t NDrone2_IsCivilian(DCVars_tag* dcv) { return dcv->drone->side != 1 ? 1 : 0; }

// [decomp] 0x0013f900 NDrone2_CivilianScaredTalk
// Grito de medo (peles com unknown_15): em 07000009-0b, 11-13 e 4a uma das três falas 0x9f-0xa1, e
// sempre o som 0x48.
void NDrone2_CivilianScaredTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (Drone_IsTalking(d) != 0) return;
    if (d->health <= 0.0f) return;
    if (dcv->drone->unknown_15 == 0) return;
    const uint32_t level = GameState.level_file;
    const bool talks = level < 0x7000014 ? !(level < 0x7000011 && (0x700000b < level || level < 0x7000009))
                                         : level == 0x700004a;
    if (talks) NDrone2_SpeechSFX(dcv, Rand_Rand(3) + 0x9f, 0, 1, 0);
    NDrone2_SpeechSFX(dcv, 0x48, 0, 1, 0);
}

// [decomp] 0x00154ba8 NDrone2_MoveToAlertPosition
// Peculiaridade mantida: não devolve nada (os chamadores leem o valor de NDrone2_MoveToGoalPosition que
// fica no registrador; aqui ele é devolvido).
uint32_t NDrone2_MoveToAlertPosition(DCVars_tag* dcv) {
    return NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0);
}

// [decomp] 0x00154b20 NDrone2_SetDroneAlertRoute
// O alvo das rotas passa a ser o do alerta difundido (NPCGlobals.alert_target), a 2 m.
uint32_t NDrone2_SetDroneAlertRoute(DCVars_tag* dcv) {
    AINetwork_InvalidateRoute(&dcv->drone->route);
    AINetwork_BuildAITarget(&NPCGlobals.alert_target, nullptr, 0.0f);
    amemcpy(&dcv->drone->goal_target, &NPCGlobals.alert_target, 0x60);
    Drone_tag* d = dcv->drone;
    AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, &d->goal_target.pos, 2.0f, 1);
    return 1;
}

// [decomp] 0x00154aa8 NDrone2_SetSoundAlertRoute
// O alvo das rotas passa a ser o último som ouvido (sound_target), a 2 m.
uint32_t NDrone2_SetSoundAlertRoute(DCVars_tag* dcv) {
    amemcpy(&dcv->drone->goal_target, &dcv->drone->sound_target, 0x60);
    AINetwork_InvalidateRoute(&dcv->drone->route);
    AINetwork_BuildAITarget(&dcv->drone->goal_target, nullptr, 0.0f);
    Drone_tag* d = dcv->drone;
    AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, &d->goal_target.pos, 2.0f, 1);
    return 1;
}

// [decomp] 0x00154e90 NDrone2_SetAngleToAlert
// Vira o alvo do giro para o som ouvido (mais `offset`).
void NDrone2_SetAngleToAlert(DCVars_tag* dcv, float offset) {
    if ((dcv->drone->control_flags & 0x8000000) != 0) return;
    const _VECTOR& s = dcv->drone->sound_target.pos.pos;
    dcv->drone->move_angles.y = offset + ATAN2_APPROX(s.x - dcv->obj->pos.x, s.z - dcv->obj->pos.z);
}

// [decomp] 0x00154f00 NDrone2_GetAngleDifferenceToAlert
// Quanto o personagem precisa girar para olhar o som ouvido. Peculiaridade mantida: o original não
// devolve o valor (os chamadores leem o registrador f0, que fica com ele); aqui ele é devolvido.
float NDrone2_GetAngleDifferenceToAlert(DCVars_tag* dcv) {
    const _VECTOR& s = dcv->drone->sound_target.pos.pos;
    const float yaw = ATAN2_APPROX(s.x - dcv->obj->pos.x, s.z - dcv->obj->pos.z);
    return Vec_AngleDifference(dcv->obj->rot.y, yaw);
}

// [decomp] 0x00149920 DroneFunc_HeardNoiseTalk
// "Ouviu isso?" (classe 1), se ninguém via o oponente no tick anterior.
void DroneFunc_HeardNoiseTalk(DCVars_tag* dcv) {
    if (NPCGlobals.prev_counts[5] != 0) return;
    Drone_tag* d = dcv->drone;
    if (Drone_IsTalking(d) != 0) return;
    if (0.0f < d->health && dcv->drone->klass == 1) NDrone2_SpeechSFX(dcv, 0x26, 0, 1, 0);
}

// [decomp] 0x00153008 NDrone2_FindAlarmPoint
// O ponto de alarme (ponto de IA tipo 8): o do fim da rota de missão ou o mais perto; o alvo das rotas
// vai para ele e a direção dele fica em goal_point.rot. 1 se há rota até ele.
uint32_t NDrone2_FindAlarmPoint(DCVars_tag* dcv) {
    AIPoint_tag* p = dcv->drone->mission_point;
    const auto setup = [&](AIPoint_tag* point) {
        const float radius = point->radius;
        AINetwork_InvalidateRoute(&dcv->drone->route);
        AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &point->pos, radius, 0);
        return NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0);
    };
    if (p != nullptr && p->kind == 8) {
        setup(p);
        return 1;
    }
    p = NDrone2_ClosestAIPoint(dcv, 8, nullptr, nullptr);
    if (p == nullptr) return 0;
    dcv->drone->goal_point.rot.y = p->rot.y;
    const uint32_t r = setup(p);
    return r < 4 ? 1 : 0;
}

// [decomp] 0x001e4498 plane_from_two_vectors_and_point
// O plano que contém o ponto p e as direções a e b (normal = a x b, não normalizada).
void plane_from_two_vectors_and_point(_VECTOR* plane, const _VECTOR* a, const _VECTOR* b, const _VECTOR* p) {
    const float nx = a->y * b->z - a->z * b->y;
    plane->x = nx;
    const float ny = a->z * b->x - a->x * b->z;
    plane->y = ny;
    const float nz = a->x * b->y - a->y * b->x;
    plane->z = nz;
    plane->w = (-nx * p->x - ny * p->y) - nz * p->z;
}

namespace {

// O lado da porta guardada (NDrone2_SetDronePartner): o plano vertical pelos dois guardas e os dois
// perpendiculares a ele por cada um.
void build_guard_planes(Drone_tag* owner, obj_tag* here, obj_tag* there, const _VECTOR& dir) {
    const _VECTOR up{0.0f, 1.0f, 0.0f, 1.0f};
    plane_from_two_vectors_and_point(&owner->guard_planes[0], &dir, &up, &here->pos);
    const float a = owner->guard_planes[0].x;
    const float c = owner->guard_planes[0].z;
    owner->guard_planes[2].x = c;
    owner->guard_planes[2].y = owner->guard_planes[0].y;
    owner->guard_planes[2].z = -a;
    const float hx = here->pos.x;
    const float hz = here->pos.z;
    owner->guard_planes[1].x = -c;
    owner->guard_planes[1].z = a;
    owner->guard_planes[1].y = owner->guard_planes[0].y;
    owner->guard_planes[2].w = owner->guard_planes[0].w + (a - c) * hx + (c - -a) * hz;
    owner->guard_planes[1].w = (((owner->guard_planes[0].w + a * here->pos.x) - -c * there->pos.x) + c * here->pos.z) -
                               a * there->pos.z;
}

// x*a + y*b + z*c + d, na ordem da VU0.
float plane_dot(const _VECTOR& plane, const _VECTOR& p) {
    return ((p.x * plane.x + p.y * plane.y) + p.z * plane.z) + plane.w;
}

}  // namespace

// [decomp] 0x00141360 NDrone2_SetDronePartner
// Acha o guarda de tipo `type` sem parceiro mais perto (no plano, a menos de 2 m de altura) e os faz
// parceiros (Drone_tag::unknown_2bc). O da direita guarda os planos da porta entre os dois (guard_planes)
// e o outro fica com guard_zone 0xff (lê a zona do parceiro).
uint32_t NDrone2_SetDronePartner(uint16_t type, DCVars_tag* dcv) {
    obj_tag* me_obj = dcv->obj;
    Drone_tag* me = dcv->drone;
    float best = 1e6f;
    Drone_tag* partner = nullptr;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* o = reinterpret_cast<Drone_tag*>(n);
        if (o == me || o->type != (type & 0xff)) continue;
        obj_tag* obj = o->obj;
        if (obj == nullptr) continue;
        const Drone_tag* od = static_cast<const Drone_tag*>(obj->data);
        if ((od->flags & 0x600) != 0 || (od->flags & 0x100) == 0 || !(0.0f < od->health) || obj->type == 0x11 ||
            (obj->life & 1) != 0) {
            continue;
        }
        if (o->unknown_2bc != nullptr) continue;
        if (!(std::fabs(me_obj->pos.y - obj->pos.y) < 2.0f)) continue;
        const float sq = Vec_SqDist2D(&me_obj->pos, &obj->pos);
        if (best <= sq) continue;
        me->unknown_2bc = o->obj;
        best = sq;
        partner = o;
    }
    if (partner == nullptr) return 1;
    partner->unknown_2bc = me_obj;
    obj_tag* pobj = partner->obj;
    _VECTOR dir{pobj->pos.x - me_obj->pos.x, pobj->pos.y - me_obj->pos.y, pobj->pos.z - me_obj->pos.z,
                pobj->pos.w - me_obj->pos.w};
    Vec_Normalise(&dir, &dir);
    const float diff = Vec_AngleDifference(me_obj->rot.y, ATAN2_APPROX(dir.x, dir.z));
    if (diff < 0.0f) {
        me->guard_zone = 0xff;
        const _VECTOR neg{-dir.x, -dir.y, -dir.z, dir.w};
        build_guard_planes(partner, pobj, me_obj, neg);
    } else {
        partner->guard_zone = 0xff;
        build_guard_planes(me, me_obj, pobj, dir);
    }
    return 1;
}

// [decomp] 0x001416d8 NDrone2_DoorGuardHandler
// Onde está o oponente em relação à porta guardada: 0 longe ou do outro lado, 1 chegando (até 6 m), 2
// perto (até 2 m), 3 passando. O guarda sem planos lê a zona do parceiro.
uint32_t NDrone2_DoorGuardHandler(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->unknown_2bc == nullptr || d->opponent == nullptr) return 0;
    obj_tag* opp = d->opponent;
    if (d->guard_zone == 0xff) return static_cast<Drone_tag*>(d->unknown_2bc->data)->guard_zone;
    if (2.0f < std::fabs(opp->pos.y - dcv->obj->pos.y)) return 0;
    const float along = plane_dot(d->guard_planes[0], opp->pos);
    const float side_a = plane_dot(d->guard_planes[1], opp->pos);
    const float side_b = plane_dot(d->guard_planes[2], opp->pos);
    uint32_t zone = 0;
    if (0.0f <= side_a && !(side_b < 0.0f) && !(along < -6.0f)) {
        if (along < 0.0f) {
            zone = 3;
        } else if (along < 2.0f) {
            zone = 2;
        } else if (along < 6.0f) {
            zone = 1;
        }
    }
    dcv->drone->guard_zone = zone;
    return zone;
}

// ---- estados: ir até um ponto, procurar, investigar ----

// [decomp] 0x00165640 NDrone2_DSTATE_GoToGoalPosition
// Vai até o alvo das rotas (goal_point) correndo; procura o oponente no caminho (menos em 07000009/0a
// e para quem já sabe dele); armado e vendo o oponente, atira parado quando a rota espera. Ao chegar:
// com a propriedade 0x59 some (0xb4); o tipo 7 alterna entre as duas estruturas de comportamento e
// ataca; os outros vão ao estado inicial.
uint32_t NDrone2_DSTATE_GoToGoalPosition(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x400000;
            return 1;
        case 2:
            dcv->drone->flags &= 0xffbfffff;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            const uint32_t level = GameState.level_file;
            if (!(level < 0x700000b && 0x7000008 < level)) {
                Drone_tag* d = dcv->drone;
                const bool look = sees(d) && !((d->sight_flags & 8) != 0 && d->range_f0 <= d->opp_dist) &&
                                  (d->flags2 & 0x200000) == 0 && drone->forced_attack == 0;
                if (look) {
                    dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
                    if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
                }
            }
            NDrone2_DistanceToAIPoint(dcv, &dcv->drone->goal_point);
            switch (NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0)) {
                case 0: case 1: case 2:
                    DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                    return 1;
                case 9:
                case 0xc:
                    if (drone->side != 1 || NDrone2_ObjectIsArmed(obj) == 0) {
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                    if (!sees(dcv->drone)) {
                        drone->firing = 0;
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    fire_if_clear(drone);
                    DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                    if (dcv->drone->burst_over == 0) {
                        if (drone->firing != 0) DroneAnim_PlayFiringAnim(dcv);
                        return 1;
                    }
                    if (drone->firing != 0) DroneWeap_Fire(1, dcv);
                    return 1;
                case 10:
                    return 1;
                default: {
                    if (prop(dcv->drone, 0x59)) return set_state(dcv, 0xb4);
                    dcv->drone->unknown_33[0] = 0;
                    if (bots_on() || dcv->drone->type != 7) {
                        back_to_initial(dcv);
                        return 1;
                    }
                    if (DroneFunc_Using2ndBehaviour(dcv->drone) != 0) {
                        DroneFunc_Set1stBehaviour(dcv->drone);
                        return 1;
                    }
                    dcv->sm->result = static_cast<uint32_t>(static_cast<int32_t>(NDrone2_ChangeToAttackMode(dcv->drone)));
                    if (dcv->sm->result == 0) {
                        DroneFunc_Set2ndBehaviour(dcv->drone);
                        dcv->drone->type = dcv->drone->base_type;
                        return 1;
                    }
                    return set_state(dcv, dcv->sm->result);
                }
            }
        }
        case 0x1b:
            back_to_initial(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x00165a68 NDrone2_DSTATE_HostageGoToGoalPosition
// Refém ou civil com medo correndo até o ponto (gritando); ao chegar, o estado inicial.
uint32_t NDrone2_DSTATE_HostageGoToGoalPosition(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 100, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 3:
            NDrone2_DistanceToAIPoint(dcv, &dcv->drone->goal_point);
            NDrone2_CivilianScaredTalk(dcv);
            switch (NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0)) {
                case 0: case 1: case 2:
                    DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                    return 1;
                case 3:
                    return back_to_initial(dcv);
                case 9:
                case 0xc:
                    DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                    return 1;
                default:
                    return 1;
            }
        case 0x1b:
            back_to_initial(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 100);
        default:
            return 0;
    }
}

// [decomp] 0x0015f938 NDrone2_DSTATE_AllyGoToGoalPosition
// Aliado correndo até o ponto; ao chegar (ou sem rota), o estado inicial.
uint32_t NDrone2_DSTATE_AllyGoToGoalPosition(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x28, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 3: {
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(2, dcv);
            NDrone2_DistanceToAIPoint(dcv, &dcv->drone->goal_point);
            const uint32_t r =
                NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0);
            if (r == 3) return back_to_initial(dcv);
            if (r < 3) {
                DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                return 1;
            }
            if (r == 10) return 1;
            back_to_initial(dcv);
            return 1;
        }
        case 0x1b:
            back_to_initial(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x28);
        default:
            return 0;
    }
}

// [decomp] 0x00165270 NDrone2_DSTATE_AlertToPosition
// Vai até o lugar de um alerta (o alvo das rotas): corre se está a 10 m ou mais, senão anda mirando;
// procura o oponente no caminho; ao chegar, o estado inicial; sem rota, 0x5f; um som forte faz atacar.
uint32_t NDrone2_DSTATE_AlertToPosition(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0xf, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 5);
            return 1;
        case 3: {
            if (1.0f <= dcv->drone->alert) NDrone2_SeenAndAttacking(dcv);
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            switch (NDrone2_MoveToAlertPosition(dcv)) {
                case 0: case 1: case 2: {
                    if (10.0f <= drone->route.distance) {
                        DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    const uint16_t a = dcv->drone->anim_state;
                    if (a != 0x35 && a != 0xf) DroneAnim_CallAnim(0.0f, 0x35, 0, 0, 0, 0, dcv);
                    return 1;
                }
                case 3:
                    return back_to_initial(dcv);
                case 6:
                    return set_state(dcv, 0x5f);
                case 10:
                    return 1;
                default:
                    dcv->drone->unknown_33[0] = 0;
                    set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(dcv->drone->return_state))));
                    return 1;
            }
        }
        case 0xc:
            if (dcv->drone->goal_point.radius + 8.0f <= point_dist(dcv->drone->goal_point)) {
                DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
            }
            return 1;
        case 0x14:
            if (loud_sound_attacks(dcv, drone, &ret)) return ret;
            [[fallthrough]];
        case 0x11: case 0x12: case 0x15: case 0x16: case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x1b:
            back_to_initial(dcv);
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x00165bf0 NDrone2_DSTATE_SearchArea
// Procura em volta (animações 0x22, depois 0x61 e 0xb em sequência pelas mensagens de fim 0x24/0x25) e
// volta ao lugar onde foi alertado (99, depois o estado de volta), calmo.
uint32_t NDrone2_DSTATE_SearchArea(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    const auto go_back = [&] {
        AINetwork_InvalidateRoute(&dcv->drone->route);
        Drone_tag* d = dcv->drone;
        AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, reinterpret_cast<CelPos_tag*>(&d->return_pos), 2.0f,
                                    1);
        set_state(dcv, 99);
        return 1u;
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x22, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
            return 1;
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0xc:
            DroneAnim_CallAnim(0.0f, 0x61, 0, 0, 0x24, 0, dcv);
            return 1;
        case 0x24:
            DroneAnim_CallAnim(0.0f, 0xb, 0, 0, 0x25, 0, dcv);
            return 1;
        case 0x25:
            Drone_AlertStatusSet(0, dcv);
            dcv->drone->alert = 0.0f;
            dcv->drone->unknown_33[1] = 0;
            dcv->drone->initial_state = dcv->drone->return_state;
            return go_back();
        case 0x14:
            if (loud_sound_attacks(dcv, drone, &ret)) return ret;
            [[fallthrough]];
        case 0x11: case 0x12: case 0x15: case 0x16: case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x1b:
            dcv->drone->initial_state = dcv->drone->return_state;
            return go_back();
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x00168070 NDrone2_DSTATE_Investigate
// Em alerta procurando o oponente; quando o alerta cai abaixo de 0,66, fica parado (4).
uint32_t NDrone2_DSTATE_Investigate(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, true, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            if (dcv->drone->alert < 0.66f) return set_state(dcv, 4);
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// ---- estados: viu ou ouviu algo ----

// [decomp] 0x00173b38 NDrone2_DSTATE_SeenOpponent
uint32_t NDrone2_DSTATE_SeenOpponent(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    set_state(dcv, 0x56);
    return 1;
}

namespace {
// SeenDeadBody/SeenSurrenderedDrone: alerta total, rota até o alerta e, depois da animação 0x21, vai
// até lá (0x62) e então ataca.
uint32_t seen_something(DCVars_tag* dcv, MsgObject* msg, bool may_shout) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->alert = 1.0f;
            dcv->drone->initial_state = 0x56;
            NDrone2_SetDroneAlertRoute(dcv);
            if (may_shout && Rand_Rand(6) == 0) NDrone2_PlaySFX(dcv, 0x23, nullptr, 1, 1.0f);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0x62, 0, 0, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}
}  // namespace

// [decomp] 0x0016acd0 NDrone2_DSTATE_SeenDeadBody
uint32_t NDrone2_DSTATE_SeenDeadBody(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return seen_something(dcv, msg, false);
}

// [decomp] 0x0016ade0 NDrone2_DSTATE_SeenSurrenderedDrone
// Como SeenDeadBody, e às vezes um grito (som 0x23).
uint32_t NDrone2_DSTATE_SeenSurrenderedDrone(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    return seen_something(dcv, msg, true);
}

// [decomp] 0x00173b78 NDrone2_DSTATE_SeenDroneShot
uint32_t NDrone2_DSTATE_SeenDroneShot(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    set_state(dcv, 0x56);
    return 1;
}

// [decomp] 0x00173bb8 NDrone2_DSTATE_SeenExplosive
uint32_t NDrone2_DSTATE_SeenExplosive(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* msg) {
    return msg->msg == 0 || msg->msg == 1 || msg->msg == 3 ? 1 : 0;
}

// [decomp] 0x00173be8 NDrone2_DSTATE_HeardNoise
uint32_t NDrone2_DSTATE_HeardNoise(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    Drone_AlertStatusSet(1, dcv);
    dcv->drone->unknown_33[0] = 1;
    set_state(dcv, 0xa3);
    return 1;
}

// [decomp] 0x0016af20 NDrone2_DSTATE_HeardNoiseAware
// Ouviu algo (nível 2 de unknown_43): olha em volta (animação 0x22) e volta; ouvindo de novo, suspeita
// (0xa4).
uint32_t NDrone2_DSTATE_HeardNoiseAware(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, false, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->unknown_33[0] = 1;
            if (drone->unknown_43 < 2) {
                drone->unknown_43 = 2;
                dcv->drone->flags |= 0x10000;
                dcv->drone->flags |= 0x2000;
                DroneAnim_CallAnim(0.0f, 0x22, 1, static_cast<int16_t>(dcv->drone->return_state), 0, 0, dcv);
                return 1;
            }
            return set_state(dcv, 0xa4);
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x14:
            if (drone->sound_alert_step <= 0.1f) return 1;
            return set_state(dcv, 0xa4);
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x0016b0e0 NDrone2_DSTATE_HeardNoiseSuspect
// Suspeita (nível 3): fala, gira para o som (ou dá meia-volta, animação 0x61, se ele está atrás) e, com
// a propriedade 0x21, vai até lá (0x62, depois procura: 0x65); ouvindo de novo, alerta (0xa5).
uint32_t NDrone2_DSTATE_HeardNoiseSuspect(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, false, &ret)) return ret;
    const auto look_back = [&] {
        dcv->drone->flags |= 0x10000;
        DroneAnim_CallAnim(0.0f, 0x22, 1, static_cast<int16_t>(dcv->drone->return_state), 0, 0, dcv);
        return 1u;
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            if (2 < dcv->drone->unknown_43) return set_state(dcv, 0xa5);
            dcv->drone->unknown_43 = 3;
            Drone_AlertStatusSet(1, dcv);
            DroneFunc_HeardNoiseTalk(dcv);
            dcv->drone->unknown_33[0] = 1;
            dcv->drone->flags |= 0x2000;
            if ((dcv->drone->flags & 0x10) != 0 || prop(dcv->drone, 0x40)) return look_back();
            dcv->drone->unknown_33[0] = 1;
            NDrone2_SetSoundAlertRoute(dcv);
            if (dcv->drone->unknown_33[1] != 0 && prop(dcv->drone, 0x21)) return set_state(dcv, 0x62);
            dcv->drone->flags |= 0x10000;
            if (2.3561945f < std::fabs(NDrone2_GetAngleDifferenceToAlert(dcv))) {
                DroneAnim_CallAnim(0.0f, 0x61, 0, 0, 0x23, 0, dcv);
                return 1;
            }
            dcv->drone->unknown_33[1] = 1;
            NDrone2_SetAngleToAlert(dcv, 0.0f);
            if (!prop(dcv->drone, 0x21)) return look_back();
            dcv->drone->initial_state = 0x65;
            DroneAnim_CallAnim(0.0f, 0x22, 0, 0x62, 0, 0, dcv);
            return 1;
        }
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0x14:
            if (0.1f < drone->sound_alert_step) {
                dcv->drone->flags |= 0x4000000;
                dcv->drone->flags |= 0x10000000;
            }
            set_state(dcv, 0x56);
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        case 0x23:
            if (dcv->drone->unknown_33[1] != 0 && prop(dcv->drone, 0x21)) return set_state(dcv, 0x62);
            dcv->drone->unknown_33[1] = 1;
            NDrone2_SetAngleToAlert(dcv, 0.0f);
            if (!prop(dcv->drone, 0x21)) return look_back();
            dcv->drone->initial_state = 0x65;
            if (dcv->drone->anim_state == 0x21) {
                DroneAnim_CallAnim(0.0f, 0xb, 0, 0x62, 0, 0, dcv);
                return 1;
            }
            return set_state(dcv, 0x62);
        default:
            return 0;
    }
}

// [decomp] 0x0016b540 NDrone2_DSTATE_HeardNoiseAlert
// Alerta pelo som (nível 4): gira para o oponente (ou dá meia-volta) e ataca; depois de um segundo
// (temporizador 1), ataca de qualquer jeito.
uint32_t NDrone2_DSTATE_HeardNoiseAlert(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, false, &ret)) return ret;
    const auto turn_and_attack = [&] {
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(0.0f, 0xb, 0, 0x56, 0, 0, dcv);
        return 1u;
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->unknown_43 = 4;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->unknown_33[0] = 1;
            DroneFunc_HeardNoiseTalk(dcv);
            if ((dcv->drone->flags & 0x10) == 0 && !prop(dcv->drone, 0x40)) {
                dcv->drone->unknown_33[0] = 1;
                dcv->drone->flags |= 0x10000;
                dcv->drone->flags |= 0x2000;
                set_timer1(dcv->drone, frame_plus(60.0f));
                if (std::fabs(dcv->drone->opp_bearing) <= 2.3561945f) {
                    dcv->drone->unknown_33[1] = 1;
                    return turn_and_attack();
                }
                DroneAnim_CallAnim(0.0f, 0x61, 0, 0, 0x23, 0, dcv);
                return 1;
            }
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x22, 1, 0x56, 0, 0, dcv);
            return 1;
        case 3:
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            return dcv->sm->result != 0 ? set_state(dcv, dcv->sm->result) : 1;
        case 0xc:
            dcv->drone->flags |= 0x4000000;
            set_state(dcv, 0x56);
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        case 0x23:
            dcv->drone->unknown_33[1] = 1;
            return turn_and_attack();
        default:
            return 0;
    }
}

// ---- estados: guardas, alarme, civis com medo, missão ----

// [decomp] 0x001603a8 NDrone2_DSTATE_CivilianGuard
// Guarda civil parado: ataca (0x56) quem ele vê armado, ou quando ele mesmo está armado e vê o
// oponente (com a propriedade 0x33, avisando todos uma vez). Ao sair, vira inimigo.
uint32_t NDrone2_DSTATE_CivilianGuard(DCVars_tag* dcv, Drone_tag*, obj_tag* obj, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, true, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            dcv->drone->initial_state = 0x56;
            DroneAnim_CallAnim(0.0f, 0.66f <= dcv->drone->alert ? 0x21 : 0x24, 0, 0, 0, 0, dcv);
            dcv->drone->state_a = 0xff;
            dcv->drone->state_b = 0;
            return 1;
        case 2:
            dcv->drone->side = 1;
            return 1;
        case 3:
            if (NDrone2_NearArmedPerson(dcv, 20.0f) == 0 && (NDrone2_ObjectIsArmed(obj) == 0 || !sees(dcv->drone))) {
                NDrone2_BondTalk(dcv);
                return 1;
            }
            dcv->drone->flags |= 0x8000000;
            if (prop(dcv->drone, 0x33) && (dcv->drone->flags & 0x200000) == 0) {
                dcv->drone->flags2 |= 1;
                NDrone2_DroneAlertToObject(dcv, 0x11, NDrone2_Player(), 20.0f, 20.0f, 0.76f);
                dcv->drone->flags |= 0x200000;
            }
            return set_state(dcv, 0x56);
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x00160648 NDrone2_DSTATE_CivilianDoorGuard
// Um dos dois guardas de uma porta (o parceiro é achado ao entrar): troca de animação de espera de
// tempos em tempos e, pela zona do oponente (NDrone2_DoorGuardHandler), olha para ele, manda parar
// (animação 0x5f e fala) ou, se ele passa, falha a missão e ataca.
uint32_t NDrone2_DSTATE_CivilianDoorGuard(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, true, &ret)) return ret;
    const auto idle = [&](uint32_t call, uint32_t next) {
        dcv->drone->state_a = call;
        DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
        dcv->drone->timer1_time = GameState.frame + (Rand_Rand(0x14) + 5) * FRAME_RATE_INT;
        dcv->drone->timer1_data = next;
    };
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            NDrone2_SetDronePartner(0x16, dcv);
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            dcv->drone->initial_state = 0x56;
            DroneAnim_CallAnim(0.0f, 0x24, 0, 0, 0, 0, dcv);
            dcv->drone->state_a = 0x24;
            dcv->drone->state_b = 4;
            dcv->drone->timer1_time = GameState.frame + Rand_Rand(0x19) * FRAME_RATE_INT;
            dcv->drone->timer1_data = 1;
            dcv->drone->timer1_on = 1;
            return 1;
        case 2:
            dcv->drone->side = 1;
            return 1;
        case 3: {
            if (NDrone2_NearArmedPerson(dcv, 20.0f) != 0) {
                dcv->drone->flags |= 0x8000000;
                if (prop(dcv->drone, 0x33) && (dcv->drone->flags & 0x200000) == 0) {
                    dcv->drone->flags2 |= 1;
                    NDrone2_DroneAlertToObject(dcv, 0x11, NDrone2_Player(), 20.0f, 20.0f, 0.76f);
                    dcv->drone->flags |= 0x200000;
                }
                dcv->drone->side = 1;
                dcv->drone->initial_state = 0x56;
                return set_state(dcv, 0x56);
            }
            const uint32_t zone = NDrone2_DoorGuardHandler(dcv);
            switch (zone) {
                case 0:
                    dcv->drone->move_angles.y = dcv->drone->divars.rot.y;
                    if (dcv->drone->state_b != 0) {
                        dcv->drone->state_b = 0;
                        DroneAnim_CallAnim(0.0f, dcv->drone->state_a, 0, 0, 0, 0, dcv);
                    }
                    return 1;
                case 1:
                    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                    if (dcv->drone->state_b != 1) {
                        dcv->drone->state_b = 1;
                        DroneAnim_CallAnim(0.0f, dcv->drone->state_a, 0, 0, 0, 0, dcv);
                    }
                    return 1;
                case 2:
                    if (dcv->drone->state_b != 2) {
                        dcv->drone->state_b = 2;
                        DroneAnim_CallAnim(0.0f, 0x5f, 1, 0, 0, 0, dcv);
                        if (dcv->drone->guard_zone != 0xff) {
                            NDrone2_SpeechSFX(dcv, DroneFunc_ChooseSFX(0x4f, 0x50), 0, 1, 1);
                        }
                    }
                    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                    return 1;
                case 3:
                    NDrone2_SpeechSFX(dcv, DroneFunc_ChooseSFX(0x8f, 0x92), 0, 1, 1);
                    DroneFunc_SetMissionFailReason(drone, 0xc);
                    return set_state(dcv, 0x56);
                default:
                    return 1;
            }
        }
        case 0xc:
            switch (msg->data) {
                case 1: idle(0x25, 2); break;
                case 0: idle(0x24, 1); break;
                case 2: idle(0x26, 0); break;
                default: return 1;
            }
            dcv->drone->timer1_on = 1;
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// [decomp] 0x001736f8 NDrone2_DSTATE_RunToAlarm
// Corre até o alarme (99, depois aperta: 0x8a); sem ponto de alarme, fica parado (4).
uint32_t NDrone2_DSTATE_RunToAlarm(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            if (NDrone2_FindAlarmPoint(dcv) != 0) {
                dcv->drone->forced_attack = 1;
                dcv->drone->initial_state = 0x8a;
                dcv->drone->return_state = 0x56;
                return set_state(dcv, 99);
            }
            set_state(dcv, 4);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00168ea8 NDrone2_DSTATE_PressAlarm
// Diante do alarme (virado para a direção do ponto): espera 3 s, aperta (animação 0x3c, que termina em
// 0x8b) e meio segundo depois o alarme dispara (o canal do switch mais perto, NDrone2 falha 0x10).
uint32_t NDrone2_DSTATE_PressAlarm(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            drone->state_a = 0;
            const uint32_t level = GameState.level_file;
            DroneAnim_CallAnim(0.0f, level < 0x700000b && 0x7000008 < level ? 0x21 : 0x22, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
            drone->move_angles.y = drone->goal_point.rot.y;
            return 1;
        }
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0xc:
            if (msg->data == 0) {
                drone->state_a = 1;
                DroneAnim_CallAnim(0.0f, 0x3c, 0, 0x8b, 0, 0, dcv);
                dcv->drone->timer1_time = frame_plus(30.0f);
                dcv->drone->timer1_data = 1;
                dcv->drone->timer1_on = 1;
                return 1;
            }
            if (NDrone2_IsCivilian(dcv) == 0) {
                drone->end_danim = 0x32;
                drone->end_param = 0x1e;
            }
            DroneFunc_SetMissionFailReason(drone, 0x10);
            dcv->drone->forced_attack = 0;
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// [decomp] 0x001737d0 NDrone2_DSTATE_DonePressAlarm
// Depois do alarme: a subclasse 0x13 fica de pé (0xb6); armado (ou com arma secundária), ataca; senão
// foge para um ponto de fuga (100, depois com medo: 0x12).
uint32_t NDrone2_DSTATE_DonePressAlarm(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (drone->klass == 0x13) return set_state(dcv, 0xb6);
    if (1 < weapon_in_hand(dcv->obj) || 1 < static_cast<int8_t>(dcv->drone->weapon2)) {
        set_state(dcv, 0x56);
        return 1;
    }
    behaviour_util_setProperty(0x56, dcv->drone->behaviour, 0);
    if (NDrone2_FindRunToPoint(dcv) != 0) {
        dcv->drone->initial_state = 0x12;
        return set_state(dcv, 100);
    }
    return set_state(dcv, 0x12);
}

// [decomp] 0x0015d3a0 NDrone2_DSTATE_CivilianScared
// Civil com medo: armado, ataca (DroneFunc_FirstAttack); senão grita, para de ouvir e se esconde
// (0x13), correndo antes para um ponto de fuga quando o tipo de ataque é 7.
uint32_t NDrone2_DSTATE_CivilianScared(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->sm->result = DroneFunc_FirstAttack(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(2, dcv);
            dcv->drone->flags |= 0x400000;
            dcv->drone->flags |= 0x800000;
            NDrone2_CivilianScaredTalk(dcv);
            if (dcv->drone->attack_type == 7 && NDrone2_FindRunToPoint(dcv) != 0) {
                dcv->drone->initial_state = 0x13;
                return set_state(dcv, 100);
            }
            set_state(dcv, 0x13);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, static_cast<int16_t>(dcv->sm->state));
        default:
            return 0;
    }
}

// [decomp] 0x0015d520 NDrone2_DSTATE_CivilianHiding
// Escondido, agachado com medo (animação 0x19).
uint32_t NDrone2_DSTATE_CivilianHiding(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x12, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(2, dcv);
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x19, 0, 0, 0, 0, dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x12);
        default:
            return 0;
    }
}

// [decomp] 0x0015e1e8 NDrone2_DSTATE_EnemyMission
// Inimigo seguindo a rota de missão (route2) procurando o oponente; parado num nó espera (mirando em
// alerta); no fim, ataca (o estado de ataque do tipo, fora de 07000006) ou fica parado (4). A classe
// 0x13 vai à missão do Kiko (0x19).
uint32_t NDrone2_DSTATE_EnemyMission(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
    if (alert_message(dcv, msg, true, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            Drone_tag* d = dcv->drone;
            if (d->klass == 0x13) return set_state(dcv, 0x19);
            d->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0.66f <= dcv->drone->alert ? 0x35 : 0x37, 0, 0, 0, 0, dcv);
            return 1;
        }
        case 3: {
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            if (NDrone2_UpdateMissionRoute(dcv) != 3) {
                Drone_tag* d = dcv->drone;
                if (d->route2.status != 9 && d->route2.status != 0xc) return 1;
                if (0.66f <= d->alert) {
                    drone->firing = sees(d) ? (drone->gun_in_wall == 0 ? 1 : 0) : 0;
                    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                    DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                    return 1;
                }
                DroneAnim_SetStandIdleAnim(dcv, 1);
                return 1;
            }
            behaviour_util_setProperty(0x1d, dcv->drone->behaviour, 0);
            AINetwork_InvalidateRoute(&drone->route2);
            drone->route2.count = 0;
            Drone_tag* d = dcv->drone;
            if (d->attack_state != 0 && d->channel_b == 0 && GameState.level_file != 0x7000006) {
                return set_state(dcv, static_cast<uint32_t>(static_cast<int32_t>(NDrone2_ChangeToAttackMode(d))));
            }
            return set_state(dcv, 4);
        }
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            return 0;
    }
}

// ---- ninjas ----

namespace {

// Os impactos nos estados dos ninjas: tiro (8) ou soco (6) só dão o efeito (vezes `mul`) e a fala de dor;
// sem vida, morrem (0x44 tiro, 0x42 nocaute); explosões (9) vão a NDrone2_ExplosiveImpact.
bool ninja_hit(DCVars_tag* dcv, MsgObject* msg, float mul, uint32_t* ret) {
    uint32_t death;
    switch (msg->msg) {
        case 6: death = 0x42; break;
        case 8: death = 0x44; break;
        case 9:
            NDrone2_ExplosiveImpact(dcv, nf::p32_ptr<DroneImpact_tag>(msg->data), 0, 1);
            *ret = 1;
            return true;
        default:
            return false;
    }
    NDrone2_DoHitEffects(dcv, nf::p32_ptr<HITDATA_tag>(msg->data), mul);
    if (0.0f < dcv->drone->health) {
        NDrone2_PainTalk(dcv);
        *ret = 1;
    } else {
        *ret = set_state(dcv, death);
    }
    return true;
}

// Salto de lado (3 m para um dos lados, testado à esquerda primeiro).
bool ninja_side_free(DCVars_tag* dcv) {
    return NDrone2_CanMoveToRelativePos(dcv->drone, dcv->obj, 3.0f, 0.0f, 0.0f) != 0 ||
           NDrone2_CanMoveToRelativePos(dcv->drone, dcv->obj, -3.0f, 0.0f, 0.0f) != 0;
}

bool ninja_can(DCVars_tag* dcv, float x, float z) {
    return NDrone2_CanMoveToRelativePos(dcv->drone, dcv->obj, x, 0.0f, z) != 0;
}

// Pulo por cima (5 m para a frente) com o oponente entre 5 e 7 m.
bool ninja_somersault(DCVars_tag* dcv, Drone_tag* drone) {
    return ninja_can(dcv, 0.0f, 5.0f) && 5.0f < drone->opp_dist && drone->opp_dist < 7.0f;
}

// Os acrobáticos (Somersault, Backflip, Sideflip*): no tick atira, menos quando a animação de tiro
// pode tocar e a arma está pronta.
uint32_t ninja_fire_unless_animating(DCVars_tag* dcv, Drone_tag* drone) {
    if (dcv->drone->burst_over != 0 || drone->firing == 0) DroneWeap_Fire(1, dcv);
    return 1;
}

}  // namespace

// [decomp] 0x0016b990 NDrone2_DSTATE_NinjaStand
// Ninja à espera (alcances 3/5 m, a espada na mão: AnimDatumSetEntity); ataca (0xa7) logo ou, com os
// personagens cegos, quando vê o oponente.
uint32_t NDrone2_DSTATE_NinjaStand(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            drone->flags |= 0x80000000;
            dcv->drone->flags |= 0x10000;
            dcv->drone->range_f0 = 3.0f;
            dcv->drone->range_fc = 5.0f;
            AnimDatumSetEntity(dcv->obj, 0, 0, 0xff);
            AnimDatumSetEntity(dcv->obj, 100, 0x2000312, 0xff);
            if (switch_BLIND_DRONES == 0) return set_state(dcv, 0xa7);
            DroneAnim_CallAnim(0.0f, 0x6b, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            if (sees(dcv->drone)) return set_state(dcv, 0xa7);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016bb50 NDrone2_DSTATE_NinjaAttack
// Escolhe o ataque pela distância: longe (> 12 m, 0xa8), meia (6-12, 0xa9), perto (2-6, 0xaa); colado,
// às vezes salta de lado (0xaf) ou para trás (0xae), atira parado (0xb2) ou usa a espada (0xac).
uint32_t NDrone2_DSTATE_NinjaAttack(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            if (switch_BLIND_DRONES != 0) return set_state(dcv, 0xa6);
            drone->flags |= 0x80000000;
            dcv->drone->flags |= 0x10000;
            dcv->drone->alert = 1.0f;
            const float dist = dcv->drone->opp_dist;
            if (12.0f < dist) return set_state(dcv, 0xa8);
            if (6.0f < dist && dist <= 12.0f) return set_state(dcv, 0xa9);
            Drone_tag* d = dcv->drone;
            if (2.0f < d->opp_dist && d->opp_dist <= 6.0f) return set_state(dcv, 0xaa);
            if (d->opp_dist < d->unknown_ec) {
                if (Rand_Rand(10) == 0 && ninja_side_free(dcv)) return set_state(dcv, 0xaf);
                if (ninja_can(dcv, 0.0f, -5.0f)) return set_state(dcv, 0xae);
            }
            if (Rand_Rand(2) != 0) return set_state(dcv, 0xb2);
            if (dcv->drone->opp_dist <= 0.0f) return 1;
            if (2.0f < dcv->drone->opp_dist) return 1;
            return set_state(dcv, 0xac);
        }
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            const float dist = dcv->drone->opp_dist;
            if (12.0f < dist) return set_state(dcv, 0xa8);
            if (6.0f < dist && dist <= 12.0f) return set_state(dcv, 0xa9);
            Drone_tag* d = dcv->drone;
            if (2.0f < d->opp_dist && d->opp_dist <= 6.0f) return set_state(dcv, 0xaa);
            if (d->unknown_ec <= d->opp_dist) return 1;
            if (NDrone2_CanMoveToRelativePos(d, dcv->obj, 0.0f, 0.0f, -5.0f) != 0) return set_state(dcv, 0xae);
            if (Rand_Rand(10) != 0) return 1;
            if (ninja_side_free(dcv)) return set_state(dcv, 0xaf);
            return 1;
        }
        default:
            return 0;
    }
}

// [decomp] 0x0016bfc0 NDrone2_DSTATE_NinjaAttackLongRange
// Longe: corre até o oponente atirando (animação 0x67); a 12 m volta a escolher; às vezes salta de lado.
uint32_t NDrone2_DSTATE_NinjaAttackLongRange(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->alert = 1.0f;
            DroneAnim_CallAnim(0.0f, 0x67, 0, 0, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            if (dcv->drone->opp_dist <= 12.0f) return set_state(dcv, 0xa7);
            const uint32_t r = NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1);
            if (r == 3) return set_state(dcv, 0xb2);
            if (r < 3) {
                DroneAnim_CallAnim(0.0f, 0x67, 0, 0, 0, 0, dcv);
                if (dcv->drone->unknown_2c8 < 0x1f) return 1;
                if (Rand_Rand(2) == 0) return 1;
                if (Rand_Rand(10) != 0) return 1;
                if (ninja_side_free(dcv)) return set_state(dcv, 0xaf);
                return 1;
            }
            if (r == 10) return 1;
            return set_state(dcv, 0xb3);
        }
        default:
            return 0;
    }
}

// [decomp] 0x0016c248 NDrone2_DSTATE_NinjaAttackMidRange
// Meia distância: avança atirando; a cada meio segundo (temporizador 1) às vezes atira parado (0xb2),
// pula por cima (0xad) ou salta de lado (0xaf).
uint32_t NDrone2_DSTATE_NinjaAttackMidRange(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (0xc < msg->msg) return 0;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->alert = 1.0f;
            set_timer1(dcv->drone, frame_plus(60.0f));
            dcv->drone->flags |= 0x10000;
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            const float dist = dcv->drone->opp_dist;
            if (!(6.0f < dist) || !(dist <= 12.0f)) return set_state(dcv, 0xa7);
            switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
                case 0: case 1: case 2:
                    DroneAnim_CallAnim(0.0f, 0x67, 0, 0, 0, 0, dcv);
                    return 1;
                case 3:
                    return set_state(dcv, 0xb2);
                case 10:
                    return 1;
                default:
                    return set_state(dcv, 0xb3);
            }
        }
        case 0xc:
            if (sees(dcv->drone) && Rand_Rand(3) == 0) return set_state(dcv, 0xb2);
            if (0x1e < dcv->drone->unknown_2c8) {
                if (ninja_somersault(dcv, drone)) return set_state(dcv, 0xad);
                if (Rand_Rand(10) == 0 && ninja_side_free(dcv)) return set_state(dcv, 0xaf);
            }
            set_timer1(dcv->drone, frame_plus(30.0f));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016c650 NDrone2_DSTATE_NinjaAttackShortRange
// Perto (2-6 m): atira e, às vezes, salta de lado ou para trás; de tempos em tempos (2 a 4 s) se aproxima
// (0xab).
uint32_t NDrone2_DSTATE_NinjaAttackShortRange(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            dcv->drone->alert = 1.0f;
            DroneAnim_CallAnim(0.0f, 0x6b, 0, 0, 0, 0, dcv);
            const float span = float(static_cast<int32_t>(FRAME_RATE_INT << 1));
            const float r = Rand_FRand(span);
            set_timer1(dcv->drone, fptoui(float(GameState.frame + FRAME_RATE_INT * 2) + r));
            DroneWeap_Fire(1, dcv);
            if (Rand_Rand(3) != 0) return 1;
            return set_state(dcv, 0xab);
        }
        case 2:
            dcv->drone->firing = 0;
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            const float dist = dcv->drone->opp_dist;
            if (!(2.0f < dist) || !(dist <= 6.0f)) return set_state(dcv, 0xa7);
            if (dist < dcv->drone->unknown_ec) {
                if (Rand_Rand(10) == 0 && ninja_side_free(dcv)) return set_state(dcv, 0xaf);
                if (ninja_can(dcv, 0.0f, -5.0f)) return set_state(dcv, 0xae);
            }
            if (dcv->drone->burst_over == 0 && drone->firing != 0) return 1;
            if (dcv->drone->unknown_2c8 != 0 && Rand_Rand(2) != 0) {
                if (ninja_side_free(dcv)) return set_state(dcv, 0xaf);
                if (ninja_can(dcv, 0.0f, -5.0f)) return set_state(dcv, 0xae);
            }
            if (dcv->drone->opp_dist <= 0.0f || 2.0f < dcv->drone->opp_dist) {
                DroneWeap_Fire(1, dcv);
                return 1;
            }
            return set_state(dcv, 0xac);
        }
        case 0xc:
            if (Rand_Rand(2) != 0) return set_state(dcv, 0xab);
            if (Rand_Rand(10) == 0 && ninja_side_free(dcv)) return set_state(dcv, 0xaf);
            if (ninja_can(dcv, 0.0f, -5.0f)) return set_state(dcv, 0xae);
            set_state(dcv, 0xab);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016cb70 NDrone2_DSTATE_NinjaGetCloseToPlayer
// Avança até o oponente atirando; colado (até 2 m), a espada (0xac); a 5-7 m pula por cima (0xad); depois
// de 3 s volta ao ataque de perto.
uint32_t NDrone2_DSTATE_NinjaGetCloseToPlayer(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x6b, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
            break;
        case 0:
            return 1;
        case 0xc:
            set_state(dcv, 0xaa);
            return 1;
        case 3: {
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            const float dist = dcv->drone->opp_dist;
            if (0.0f < dist && dist <= 2.0f) return set_state(dcv, 0xac);
            switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
                case 2:
                    if (ninja_somersault(dcv, drone)) return set_state(dcv, 0xad);
                    [[fallthrough]];
                case 0:
                case 1:
                    DroneAnim_CallAnim(0.0f, 0x67, 0, 0, 0, 0, dcv);
                    if (dcv->drone->burst_over == 0 && drone->firing != 0) return 1;
                    break;
                case 10:
                    return 1;
                default:
                    return set_state(dcv, 0xa7);
            }
            break;
        }
        default:
            return 0;
    }
    DroneWeap_Fire(1, dcv);
    return 1;
}

// [decomp] 0x0016d100 NDrone2_DSTATE_NinjaSword
// Golpe de espada (animação 0x6d) a até 2 m e volta ao ataque; os golpes recebidos valem metade.
uint32_t NDrone2_DSTATE_NinjaSword(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 0.5f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            if (0.0f < dcv->drone->opp_dist && dcv->drone->opp_dist <= 2.0f) {
                DroneAnim_CallAnim(0.0f, 0x6d, 0, 0xa7, 0, 0, dcv);
                return 1;
            }
            return set_state(dcv, 0xa7);
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016d298 NDrone2_DSTATE_NinjaSomersault
// Pulo por cima do oponente (animação 0x6a) e a espada (0xac).
uint32_t NDrone2_DSTATE_NinjaSomersault(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 0.5f, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            DroneAnim_CallAnim(0.0f, 0x6a, 0, 0xac, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return ninja_fire_unless_animating(dcv, drone);
        case 0:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016d430 NDrone2_DSTATE_NinjaBackflip
// Salto para trás (animação 0x66, variante 1 em dois terços das vezes) e volta ao ataque.
uint32_t NDrone2_DSTATE_NinjaBackflip(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 0.5f, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            DroneAnim_CallAnim(0.0f, 0x66, Rand_Rand(3) != 0 ? 1 : 0, 0xa7, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return ninja_fire_unless_animating(dcv, drone);
        case 0:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173e08 NDrone2_DSTATE_NinjaSideflip
// Salta para o lado livre (sorteado se os dois estão): 0xb0 esquerda, 0xb1 direita.
uint32_t NDrone2_DSTATE_NinjaSideflip(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    dcv->drone->flags |= 0x10000;
    if (ninja_can(dcv, 3.0f, 0.0f)) {
        if (!ninja_can(dcv, -3.0f, 0.0f)) return set_state(dcv, 0xb0);
        return set_state(dcv, Rand_Rand(2) != 0 ? 0xb1 : 0xb0);
    }
    if (ninja_can(dcv, -3.0f, 0.0f)) return set_state(dcv, 0xb1);
    set_state(dcv, 0xa7);
    return 1;
}

namespace {
uint32_t ninja_sideflip(DCVars_tag* dcv, Drone_tag* drone, MsgObject* msg, uint16_t call, bool variant_when_zero) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 0.5f, &ret)) return ret;
    switch (msg->msg) {
        case 1: {
            dcv->drone->flags |= 0x10000;
            NDrone2_InvalidateAttackRoute(dcv);
            const uint32_t r = Rand_Rand(3);
            DroneAnim_CallAnim(0.0f, call, (r == 0) == variant_when_zero ? 1 : 0, 0xa7, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            return 1;
        }
        case 2:
            dcv->drone->firing = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return ninja_fire_unless_animating(dcv, drone);
        case 0:
            return 1;
        default:
            return 0;
    }
}
}  // namespace

// [decomp] 0x0016cdd0 NDrone2_DSTATE_NinjaSideflipLeft
// Salto para a esquerda (animação 0x68, variante 1 em um terço das vezes) e volta ao ataque.
uint32_t NDrone2_DSTATE_NinjaSideflipLeft(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return ninja_sideflip(dcv, drone, msg, 0x68, true);
}

// [decomp] 0x0016cf68 NDrone2_DSTATE_NinjaSideflipRight
// Salto para a direita (animação 0x69, variante 1 em dois terços das vezes).
uint32_t NDrone2_DSTATE_NinjaSideflipRight(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    return ninja_sideflip(dcv, drone, msg, 0x69, false);
}

// [decomp] 0x0016d5b8 NDrone2_DSTATE_NinjaStandFire
// Atira parado por 3 s (às vezes pulando por cima ou de lado) e volta ao ataque.
uint32_t NDrone2_DSTATE_NinjaStandFire(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x6b, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 3);
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (dcv->drone->burst_over == 0 && drone->firing != 0) return 1;
            if (dcv->drone->unknown_2c8 != 0) {
                if (ninja_somersault(dcv, drone)) return set_state(dcv, 0xad);
                if (Rand_Rand(10) == 0 && ninja_side_free(dcv)) return set_state(dcv, 0xaf);
            }
            DroneWeap_Fire(1, dcv);
            return 1;
        case 0xc:
            set_state(dcv, 0xa7);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016d878 NDrone2_DSTATE_NinjaNoRoute
// Sem rota: atira de onde está e tenta a rota de novo a cada meio segundo.
uint32_t NDrone2_DSTATE_NinjaNoRoute(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (0xc < msg->msg) return 0;
    if (ninja_hit(dcv, msg, 1.0f, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->alert = 1.0f;
            set_timer1(dcv->drone, frame_plus(30.0f));
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x6b, 0, 0, 0, 0, dcv);
            DroneWeap_Fire(1, dcv);
            drone->firing = sees(dcv->drone) ? 1 : 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (dcv->drone->burst_over == 0 && drone->firing != 0) return 1;
            DroneWeap_Fire(1, dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            drone->firing = sees(dcv->drone) ? 1 : 0;
            return 1;
        case 0xc:
            if (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1) < 4) return set_state(dcv, 0xa7);
            set_timer1(dcv->drone, frame_plus(30.0f));
            return 1;
        default:
            return 0;
    }
}

// ---- espaço (0700001b) ----

// [decomp] 0x0016dee0 NDrone2_DSTATE_SpaceDrake
// Drake em 0700001b: parado (vida 10), invulnerável até o canal 10 ligar, quando some (0xb4).
uint32_t NDrone2_DSTATE_SpaceDrake(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->health = 10.0f;
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_SetStandIdleAnim(dcv, 0);
            if (prop(dcv->drone, 0x46)) NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
            return 1;
        case 3:
            if (switch_channels[10] != 0) return set_state(dcv, 0xb4);
            DroneAnim_SetStandIdleAnim(dcv, 1);
            return 1;
        case 4:
            if (!prop(dcv->drone, 0x46)) return 1;
            DroneAnim_CallAnim(0.0f, 0x4b, Rand_Rand(3), 0, 0, 0, dcv);
            NDrone2_SetIdleTimeOut(dcv, 0x2d, 5);
            return 1;
        case 5:
            DroneAnim_SetStandIdleAnim(dcv, 0);
            return 1;
        case 8:
            if (switch_channels[10] != 0) DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0014ab00 NDrone2_LogDroneDeath
// Conta um inimigo abatido pelo jogador (ou, com bots, pelo oponente) nas estatísticas.
void NDrone2_LogDroneDeath(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (!bots_on()) {
        if (d->attacker != NDrone2_Player()) return;
    } else if (d->attacker != d->opponent) {
        return;
    }
    if ((d->flags & 0x1000000) == 0 && d->side == 1) PlrStat_LogEnemyDispatched(0);
}

// [decomp] 0x0016dd28 NDrone2_DSTATE_AstronautLaunch
// Astronauta (sem gravidade): segue a rota de missão atirando (animação 0x72) e, no fim, combate (0xc1).
uint32_t NDrone2_DSTATE_AstronautLaunch(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            dcv->drone->flags &= 0xfffffffd;
            dcv->drone->zero_g = 1;
            NDrone2_AssignAIPath(dcv, 8);
            if (dcv->drone->route2.count != 0) {
                dcv->drone->unknown_50 = 6.9f;
                DroneAnim_CallAnim(0.0f, 0x72, 0, 0, 0, 0, dcv);
                DroneWeap_Fire(1, dcv);
                dcv->drone->firing = 0;
                dcv->drone->state_a = 0;
                dcv->drone->state_b = 0;
                dcv->drone->flags &= 0xffffffdf;
                return 1;
            }
            return set_state(dcv, 0xc1);
        case 2:
            dcv->drone->flags |= 0x20;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (NDrone2_UpdateMissionRoute(dcv) != 3) {
                dcv->drone->firing = sees(dcv->drone) ? 1 : 0;
                return 1;
            }
            return set_state(dcv, 0xc1);
        case 8:
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00174690 NDrone2_DSTATE_AstronautHit
// Astronauta atingido: é empurrado (DroneMove_AstronautHitMove) e volta ao combate em meio segundo.
uint32_t NDrone2_DSTATE_AstronautHit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            set_timer1(dcv->drone, frame_plus(30.0f));
            DroneMove_AstronautHitMove(dcv);
            dcv->drone->firing = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 8:
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        case 0xc:
            set_state(dcv, 0xc0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001747b8 NDrone2_DSTATE_AstronautDeath
// Astronauta morto: canal de morte, estatística, animação (0x6f) e deriva; some no tempo dos corpos.
uint32_t NDrone2_DSTATE_AstronautDeath(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            Drone_AlertStatusSet(3, dcv);
            DroneFunc_SetDeathChannel(drone);
            NDrone2_LogDroneDeath(dcv);
            dcv->drone->firing = 0;
            DroneAnim_CallAnim(0.0f, 0x6f, Rand_Rand(2), 0, 0, 0, dcv);
            DroneMove_AstronautDeathMove(dcv);
            set_timer1(dcv->drone, GameState.frame + NPCGlobals.corpse_time);
            return 1;
        case 0:
            return 1;
        case 3:
            DroneFunc_DeadDrone(dcv);
            return 1;
        case 0xc:
            set_state(dcv, 0x48);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00174458 NDrone2_DSTATE_AstronautCombat
// Astronauta em combate (animação 0x70), flutuando parado (DroneMove_AstronautCombat).
uint32_t NDrone2_DSTATE_AstronautCombat(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->unknown_50 = 0.0f;
            dcv->drone->float_to_target = 0;
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x70, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (drone->opponent != nullptr) DroneMove_AstronautCombat(dcv);
            return 1;
        case 8:
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00174550 NDrone2_DSTATE_AstronautCombatMove
// Astronauta se deslocando no combate por 5 s (DroneMove_AstronautCombat/AstronautAnim).
uint32_t NDrone2_DSTATE_AstronautCombatMove(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    const uint32_t fri = FRAME_RATE_INT;
    switch (msg->msg) {
        case 1:
            dcv->drone->unknown_50 = 2.0f;
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            set_timer1(dcv->drone, GameState.frame + fri * 5);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (drone->opponent == nullptr) return set_state(dcv, 0xc0);
            DroneMove_AstronautCombat(dcv);
            DroneMove_AstronautAnim(dcv);
            return 1;
        case 0:
            return 1;
        case 8:
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0xbe, 0);
            return 1;
        case 0xc:
            set_state(dcv, 0xc0);
            return 1;
        default:
            return 0;
    }
}

// ---- impactos, atordoamento, rendição ----

namespace {

HITDATA_tag* msg_hit(MsgObject* msg) { return nf::p32_ptr<HITDATA_tag>(msg->data); }

// A animação de se recompor (0x76) termina no ataque, ou, para civis, no estado de volta da máquina.
uint32_t recover_end_state(DCVars_tag* dcv) {
    if (NDrone2_IsCivilian(dcv) == 0) return 0x56;
    return static_cast<uint16_t>(dcv->sm->return_state);
}

// Tiro ou explosão em quem está caído ou atordoado: morre (0x44).
uint32_t die_now(DCVars_tag* dcv) {
    set_state(dcv, 0x44);
    return 1;
}

}  // namespace

// [decomp] 0x00147188 DroneFunc_RecoverTime
// Quanto tempo (quadros) o personagem fica atordoado pelo acerto: pela arma do projétil (dardo 0x35
// meio segundo... 0x44 dois minutos; outras armas e acertos sem projétil, um minuto), menos pela
// resistência (stats[5]: 200 - resistência por cento). Peculiaridade mantida: o original não devolve o
// valor (os chamadores leem o registrador); aqui ele é devolvido.
uint32_t DroneFunc_RecoverTime(DCVars_tag* dcv, HITDATA_tag* hit) {
    Drone_tag* d = dcv->drone;
    float t = float(FRAME_RATE_INT) * 60.0f;
    if (hit != nullptr && hit->obj != nullptr && hit->obj->type == 5) {
        const BU_tag* bu = static_cast<const BU_tag*>(hit->obj->data);
        int32_t k = -1;
        switch (*reinterpret_cast<const uint16_t*>(nf::raw(bu->weapon))) {
            case 0x35: k = 0x1e; break;
            case 0x43: k = 0x3c; break;
            case 0x44: k = 0x78; break;
            case 0x4a: case 0x4c: k = 10; break;
            default: break;
        }
        if (k >= 0) t = float(FRAME_RATE_INT * static_cast<uint32_t>(k));
    }
    return fptoui(t * 0.01f * float(static_cast<int32_t>(200 - d->stats[5])));
}

// [decomp] 0x0014a718 DroneFunc_AnyoneNear
// Há um jogador a menos de 2 x radius (e 3 m de altura) ou um personagem ativo a radius.
uint32_t DroneFunc_AnyoneNear(DCVars_tag* dcv, float radius) {
    const float sq = (radius + radius) * (radius + radius);
    for (int i = 0; i < 4; ++i) {
        obj_tag* p = glb_players[i];
        if (p != nullptr && Vec_SqDist3D(&dcv->obj->pos, &p->pos) < sq && std::fabs(dcv->obj->pos.y - p->pos.y) <= 3.0f) {
            return 1;
        }
    }
    return NDrone2_DroneNearPos(&dcv->obj->pos, radius, dcv->obj) != 0 ? 1 : 0;
}

// [decomp] 0x0014a168 NDrone2_CheckUnsurrender
// O rendido desiste de estar rendido: sem oponente, longe dele (12 m), com o oponente desarmado a mais de
// 2 m, ou quando o oponente não olha para ele ou não está à vista.
uint32_t NDrone2_CheckUnsurrender(DCVars_tag* dcv) {
    obj_tag* opp = dcv->drone->opponent;
    if (opp == nullptr) return 1;
    const float dist = dcv->drone->opp_dist;
    if (12.0f < dist) return 1;
    if (2.0f < dist) {
        int8_t w = 0;
        if (opp->sphere_src != nullptr) {
            w = anim_of(opp)->weapon;
        } else {
            ++g_drone_combat_guards;  // port: o original leria o endereço 0 + 0x62
        }
        if (w < 2) return 1;
    }
    Drone_tag* d = dcv->drone;
    if (!(std::fabs(d->opp_facing) <= kHalfPi)) return 1;
    return NDrone2_CanSeeObject(d, opp, 5, 0) == 0 ? 1 : 0;
}

// [decomp] 0x0013dd78 DroneAnim_SetDAnimNow
// Toca a DAnim já, sem estado nem mensagem de fim.
uint32_t DroneAnim_SetDAnimNow(uint16_t danim, uint16_t blend, int8_t call, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    d->end_state = 0;
    d->end_msg = 0;
    const uint32_t ok = DroneAnim_SetDAnimInternal(danim, blend, static_cast<uint8_t>(call), d);
    if (ok != 0) d->anim_call_done = 1;
    return ok != 0 ? 1 : 0;
}

// [decomp] 0x00172ee0 NDrone2_DSTATE_PunchImpact
// Levou um soco: reação (NDrone2_PunchImpact com a animação). Peculiaridade mantida: os impactos 0x17 e
// 0x18 não são tratados.
uint32_t NDrone2_DSTATE_PunchImpact(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            NDrone2_PunchImpact(dcv, msg_hit(msg), 0, 1);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 6:
        case 8:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        case 7: case 9: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172fb0 NDrone2_DSTATE_ExplosiveImpact
// Atingido por uma explosão (NDrone2_ExplosiveImpact com a animação).
uint32_t NDrone2_DSTATE_ExplosiveImpact(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            NDrone2_ExplosiveImpact(dcv, nf::p32_ptr<DroneImpact_tag>(msg->data), 0, 1);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 6: case 7: case 8:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173098 NDrone2_DSTATE_BulletImpact
// Reagindo a um tiro (a animação foi pedida por DroneFunc_HandleImpact); outro tiro conta sem animação;
// uma explosão vai a 0x54.
uint32_t NDrone2_DSTATE_BulletImpact(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            drone->firing = 0;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 6:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        case 7:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
            NDrone2_BulletImpact(dcv, msg_hit(msg), 0, 0);
            return 1;
        case 9:
            Drone_SM_SetState(dcv->sm, 0x54, msg->data);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172a30 NDrone2_DSTATE_KnockedOut_Anim
// Nocauteado: cai (animação 0x50, depois desacordado: 0x47), larga a arma e conta como inimigo
// neutralizado; ao sair liga o canal de morte (fora de 07000009/0a).
uint32_t NDrone2_DSTATE_KnockedOut_Anim(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            obj->flags |= 0x20;
            Drone_AlertStatusSet(3, dcv);
            drone->health = 0.0f;
            DroneAnim_CallAnim(drone->health, 0x50, 0, 0x47, 0, 0, dcv);
            dcv->drone->flags |= 0x1000000;
            DroneWeap_DropWeapon(dcv);
            if (drone->side == 1) PlrStat_LogEnemyDisabled(0);
            return 1;
        case 2:
            if (1 < GameState.level_file - 0x7000009u) DroneFunc_SetDeathChannel(drone);
            return 1;
        case 8:
        case 9:
            set_state(dcv, 0x44);
            return 1;
        case 0: case 3: case 10:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00172b48 NDrone2_DSTATE_Knocked_Out
uint32_t NDrone2_DSTATE_Knocked_Out(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 8:
        case 9:
            set_state(dcv, 0x44);
            return 1;
        case 0:
        case 10:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163c68 NDrone2_DSTATE_SpecialDeath_Anim
// Morte com script de entrada (parâmetro 13): perto do lugar e da direção do script, toca-o (fim em 0x47,
// 0xf para o tipo 5) escorregando para o lugar; senão a morte comum (0x44). O script 0600021f (07000007)
// só vale longe do jogador e toca um som no fim.
uint32_t NDrone2_DSTATE_SpecialDeath_Anim(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            obj->flags |= 0x20;
            Drone_AlertStatusSet(3, dcv);
            bool try_script = true;
            if (drone->enter_script == 0x600021f) {
                obj_tag* player = NDrone2_Player();
                try_script = player != nullptr && 2.0f <= Vec_Dist3D(&dcv->obj->pos, &player->pos);
                if (player == nullptr) ++g_drone_combat_guards;  // port: o original leria o endereço 0
            }
            if (try_script && Vec_Dist2D(&dcv->obj->pos, &dcv->drone->script_pos) < 1.0f &&
                std::fabs(Vec_AngleDifference(dcv->drone->script_rot.y, dcv->obj->rot.y)) < kHalfPi &&
                dcv->drone->enter_script != 0) {
                DroneFunc_OnInitDeath(dcv);
                DroneFunc_SetDeathChannel(drone);
                drone->flags &= 0x7fffffff;
                dcv->drone->move_angles = dcv->drone->script_rot;
                DroneAnim_SetScript(dcv->drone->enter_script, 0x40, 0, 0, 0, 5, 0, dcv);
                drone->scripted_move = 1;
                drone->end_state = dcv->drone->type == 5 ? 0xf : 0x47;
                drone->end_msg = 5;
                drone->end_danim = 0xbf;
                dcv->drone->flags &= 0xffffffdf;
                dcv->drone->flags &= 0xffffff7f;
                drone->state_a = 0;
                set_timer1(dcv->drone, frame_plus(60.0f));
                return 1;
            }
            if (drone->enter_script == 0x600021f) drone->param12 = 0;
            drone->enter_script = 0;
            return set_state(dcv, 0x44);
        }
        case 3:
            NDrone2_SlideToPosition(&drone->script_pos, 0.05f, 1, dcv);
            if (drone->firing == 0 || drone->subclass == 4 || drone->subclass == 9) DroneWeap_DropWeapon(dcv);
            return 1;
        case 5:
            if (drone->enter_script == 0x600021f && glb_players[0] != nullptr) {
                Sound_Play3D(0x148, &glb_players[0]->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            }
            if (dcv->drone->type == 5) return set_state(dcv, 0xf);
            set_state(dcv, 0x47);
            return 1;
        case 0:
        case 10:
            return 1;
        case 0xc:
            drone->state_a = 1;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00162b98 NDrone2_DSTATE_Taser
// Atingido pelo taser: larga a arma (fica com a 1), treme (animação 0x31/0x32) e, meio segundo depois,
// fica atordoado (0x4b); com a propriedade 0x32 avisa os outros uma vez. Os tipos 0x11/0xc falham a
// missão.
uint32_t NDrone2_DSTATE_Taser(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    if (0xc < msg->msg) return 0;
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1: {
            drone->state_c = DroneFunc_RecoverTime(dcv, msg_hit(msg));
            if (NDrone2_IsCivilian(dcv) == 0 && dcv->drone->subclass != 4) {
                DroneWeap_DropWeapon(dcv);
                DroneWeap_ChangeWeapon(dcv, 1);
            }
            if (drone->type == 0x11 || drone->type == 0xc) DroneFunc_SetMissionFailReason(drone, 0xd);
            Drone_AlertStatusSet(2, dcv);
            Drone_tag* d = dcv->drone;
            d->state_a = Rand_Rand(2) != 0 ? 0x31 : 0x32;
            DroneAnim_CallAnim(0.0f, dcv->drone->state_a, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(30.0f));
            if (!prop(dcv->drone, 0x32)) return 1;
            Drone_tag* e = dcv->drone;
            if ((e->flags & 8) != 0) return 1;
            if (e->type != 2) {
                e->flags2 |= 2;
                obj_tag* o = dcv->obj;
                NDrone2_DroneAlertToPosition(o, 0x12, &o->pos, o->cel, 0.0f, 0.0f, 0.0f);
                dcv->drone->flags |= 8;
            }
            return 1;
        }
        case 7:
            set_timer1(dcv->drone, frame_plus(30.0f));
            return 1;
        case 8:
        case 9:
            set_state(dcv, 0x44);
            return 1;
        case 0xc:
            if (static_cast<uint16_t>(drone->anim_state - 0x31) < 2) return set_state(dcv, 0x4b);
            set_timer1(dcv->drone, frame_plus(5.0f));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00162eb0 NDrone2_DSTATE_Stunned
// Atordoado no chão pelo tempo de DroneFunc_RecoverTime (outro choque ou dardo renova); ninguém por
// perto, se recompõe (0x4c).
uint32_t NDrone2_DSTATE_Stunned(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            set_timer1(dcv->drone, GameState.frame + drone->state_c);
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, dcv->drone->state_a == 0x32 ? 0x30 : 0x2f, 0, 0, 0, 0, dcv);
            dcv->drone->small_capsule = 1;
            return 1;
        case 2:
            dcv->drone->small_capsule = 0;
            return 1;
        case 6:
            NDrone2_PainTalk(dcv);
            set_state(dcv, 0x42);
            return 1;
        case 7:
            if (dcv->drone->state_a != 0x32) return set_state(dcv, 0x4a);
            [[fallthrough]];
        case 0x19:
            set_timer1(dcv->drone, GameState.frame + DroneFunc_RecoverTime(dcv, msg_hit(msg)));
            return 1;
        case 8:
        case 9:
            set_state(dcv, 0x44);
            return 1;
        case 0xc:
            if (DroneFunc_AnyoneNear(dcv, 2.0f) != 0) {
                set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 5);
                return 1;
            }
            return set_state(dcv, 0x4c);
        case 0x18:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001630b8 NDrone2_DSTATE_Stunned_Recover
// Levanta (animação 0x76): inimigos pegam a arma de volta e atacam; civis voltam ao estado guardado.
uint32_t NDrone2_DSTATE_Stunned_Recover(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            dcv->drone->flags |= 0x10000;
            if (dcv->drone->klass == 0x10) dcv->drone->side = 1;
            uint32_t end;
            if (NDrone2_IsCivilian(dcv) == 0) {
                DroneWeap_ChangeWeapon(dcv, 6);
                end = 0x56;
            } else {
                end = static_cast<uint16_t>(dcv->sm->return_state);
            }
            DroneAnim_CallAnim(0.0f, 0x76, 0, static_cast<int16_t>(end), 0, 0, dcv);
            return 1;
        }
        case 0:
        case 10:
            return 1;
        case 7:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        default:
            return 0;
    }
}

// [decomp] 0x00172bb0 NDrone2_DSTATE_StunGrenadeImpact
// Atingido por uma granada de atordoar: larga a arma, cai (animação 0x2d/0x2e) e fica (0x4e).
uint32_t NDrone2_DSTATE_StunGrenadeImpact(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            drone->state_c = DroneFunc_RecoverTime(dcv, msg_hit(msg));
            if (dcv->drone->subclass != 4) {
                DroneWeap_DropWeapon(dcv);
                DroneWeap_ChangeWeapon(dcv, 1);
            }
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, Rand_Rand(2) == 0 ? 0x2e : 0x2d, 0, 0x4e, 0, 0, dcv);
            return 1;
        case 7:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        case 0:
        case 10:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001631c0 NDrone2_DSTATE_StunGrenadeLoop
// Atordoado pela granada pelo tempo de recuperação; ninguém por perto, levanta (0x4f).
uint32_t NDrone2_DSTATE_StunGrenadeLoop(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            set_timer1(dcv->drone, GameState.frame + drone->state_c);
            dcv->drone->small_capsule = 1;
            return 1;
        case 2:
            dcv->drone->small_capsule = 0;
            return 1;
        case 6:
            NDrone2_PainTalk(dcv);
            set_state(dcv, 0x42);
            return 1;
        case 7:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        case 0xc:
            if (DroneFunc_AnyoneNear(dcv, 2.0f) == 0) return set_state(dcv, 0x4f);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 5);
            return 1;
        case 0x18:
        case 0x19:
            set_timer1(dcv->drone, GameState.frame + DroneFunc_RecoverTime(dcv, msg_hit(msg)));
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163350 NDrone2_DSTATE_StunGrenadeRecover
uint32_t NDrone2_DSTATE_StunGrenadeRecover(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x76, 0, static_cast<int16_t>(recover_end_state(dcv)), 0, 0, dcv);
            return 1;
        case 0:
        case 10:
            return 1;
        case 7: case 0x18: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        default:
            return 0;
    }
}

// [decomp] 0x00163430 NDrone2_DSTATE_StunDartImpact
// Atingido por um dardo: as subclasses 5, 6 e 9 largam a arma; cai (animação 0x2b/0x2c) e fica (0x51).
// Os tipos 0x11/0xc falham a missão.
uint32_t NDrone2_DSTATE_StunDartImpact(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            const uint32_t t = DroneFunc_RecoverTime(dcv, msg_hit(msg));
            const uint16_t sub = drone->subclass;
            drone->state_c = t;
            if (4 < sub && (sub < 7 || sub == 9)) {
                DroneWeap_DropWeapon(dcv);
                DroneWeap_ChangeWeapon(dcv, 1);
            }
            if (drone->type == 0x11 || drone->type == 0xc) DroneFunc_SetMissionFailReason(drone, 0xe);
            Drone_AlertStatusSet(0, dcv);
            DroneAnim_CallAnim(0.0f, Rand_Rand(2) == 0 ? 0x2c : 0x2b, 0, 0x51, 0, 0, dcv);
            return 1;
        }
        case 8:
        case 9:
            return die_now(dcv);
        case 0:
        case 10:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163570 NDrone2_DSTATE_StunDartLoop
uint32_t NDrone2_DSTATE_StunDartLoop(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            set_timer1(dcv->drone, GameState.frame + drone->state_c);
            dcv->drone->small_capsule = 1;
            return 1;
        case 2:
            dcv->drone->small_capsule = 0;
            return 1;
        case 6:
            NDrone2_PainTalk(dcv);
            set_state(dcv, 0x42);
            return 1;
        case 7:
        case 0x19:
            set_timer1(dcv->drone, GameState.frame + DroneFunc_RecoverTime(dcv, msg_hit(msg)));
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        case 0xc:
            if (DroneFunc_AnyoneNear(dcv, 2.0f) == 0) return set_state(dcv, 0x52);
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 5);
            return 1;
        case 0x18:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 0:
        case 10:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00163700 NDrone2_DSTATE_StunDartRecover
// Levanta (animação 0x76); outro dardo derruba de novo (0x50).
uint32_t NDrone2_DSTATE_StunDartRecover(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x76, 0, static_cast<int16_t>(recover_end_state(dcv)), 0, 0, dcv);
            return 1;
        case 7:
        case 0x18:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            return die_now(dcv);
        case 0x19:
            set_state(dcv, 0x50);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173c50 NDrone2_DSTATE_SmokedOut
// Na fumaça: tosse (animação 0x20) e fica (0x7c).
uint32_t NDrone2_DSTATE_SmokedOut(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, 0x20, 0, 0x7c, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 6: case 7: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
        case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x0016b878 NDrone2_DSTATE_SmokedOut_Loop
// Tossindo enquanto há fumaça (cada 0x17 renova por 5 quadros); depois se recompõe (0x7d).
uint32_t NDrone2_DSTATE_SmokedOut_Loop(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            DroneAnim_CallAnim(0.0f, 0x20, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + 5);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 0:
            return 1;
        case 6: case 8: case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        case 7:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 0xc:
            set_state(dcv, 0x7d);
            return 1;
        case 0x17:
            set_timer1(dcv->drone, GameState.frame + 5);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173d30 NDrone2_DSTATE_SmokedOut_Recover
uint32_t NDrone2_DSTATE_SmokedOut_Recover(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 10, 0, static_cast<int16_t>(static_cast<uint16_t>(dcv->sm->return_state)), 0, 0,
                               dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 6: case 8: case 9:
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
            return 1;
        case 7:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001626a0 NDrone2_DSTATE_Surrender_Anim
// Rende-se: larga a arma (fica com a 1), ergue as mãos (animação 0x5c, depois rendido: 0x40), de costas
// para o oponente; a cada quarto de segundo avisa os outros (0x13).
uint32_t NDrone2_DSTATE_Surrender_Anim(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x5c, 0, 0x40, 0, 0, dcv);
            Drone_AlertStatusSet(2, dcv);
            DroneWeap_DropWeapon(dcv);
            DroneWeap_ChangeWeapon(dcv, 1);
            dcv->drone->timer1_time = frame_plus(15.0f);
            dcv->drone->timer1_data = 1;
            dcv->drone->timer1_on = 1;
            PlrStat_LogEnemySurrender(0);
            if (drone->enter_script == 0x600021f) {
                drone->enter_script = 0;
                drone->param12 = 0;
            }
            return 1;
        case 3:
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, kPi, 1);
            return 1;
        case 0:
            return 1;
        case 6:
            NDrone2_PainTalk(dcv);
            set_state(dcv, 0x42);
            return 1;
        case 7:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
            DroneFunc_HandleImpact(dcv, msg, 0x44, 0);
            return 1;
        case 9:
            return die_now(dcv);
        case 0xc:
            dcv->drone->flags2 |= 4;
            NDrone2_DroneAlertToObject(dcv, 0x13, dcv->obj, 0.0f, 0.0f, 1.0f);
            dcv->drone->timer1_time = frame_plus(15.0f);
            dcv->drone->timer1_data = 1;
            dcv->drone->timer1_on = 1;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00162950 NDrone2_DSTATE_Surrendered
// Rendido, olhando para o oponente; desiste (0x41) quando NDrone2_CheckUnsurrender deixa.
uint32_t NDrone2_DSTATE_Surrendered(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 1);
            return 1;
        case 3:
            if (NDrone2_CheckUnsurrender(dcv) != 0) return set_state(dcv, 0x41);
            return 1;
        case 6:
            NDrone2_PainTalk(dcv);
            set_state(dcv, 0x42);
            return 1;
        case 7:
        case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0, 1);
            return 1;
        case 8:
            DroneFunc_HandleImpact(dcv, msg, 0x44, 0);
            return 1;
        case 9:
            return die_now(dcv);
        default:
            return 0;
    }
}

// [decomp] 0x00162a70 NDrone2_DSTATE_Unsurrender_Anim
// Volta a lutar (animação 10, depois ataque: 0x56).
uint32_t NDrone2_DSTATE_Unsurrender_Anim(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            DroneAnim_CallAnim(0.0f, 10, 0, 0x56, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// ---- estados: motorista, rapel, fuga de explosivos e outros ----

// [decomp] 0x00160bd0 NDrone2_DSTATE_TruckDriverInit
// Motorista: toca o script de entrada (parado ao lado do caminhão) e então segue a rota de missão (0x35)
// ou fica parado (0x34); um alerta o assusta (0x33).
uint32_t NDrone2_DSTATE_TruckDriverInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x33, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            if (prop(dcv->drone, 0x1d)) NDrone2_AssignAIPath(dcv, 8);
            dcv->drone->move_angles.y = dcv->obj->rot.y;
            const uint32_t script = dcv->drone->entry_script;
            if (script != 0) {
                DroneAnim_SetScript(script, 0x24, 0x11a, 0x1e, 0, 5, 0, dcv);
                dcv->drone->entry_script = 0;
                dcv->drone->flags &= 0xffffffdf;
                dcv->drone->flags &= 0xffffff7f;
                return 1;
            }
            behaviour_util_setProperty(0x1d, dcv->drone->behaviour, 0);
            return set_state(dcv, 0x34);
        }
        case 2:
            dcv->drone->flags |= 0x20;
            dcv->drone->flags |= 0x80;
            return 1;
        case 5:
            return set_state(dcv, dcv->drone->route2.count == 0 ? 0x34 : 0x35);
        case 0x11: case 0x12: case 0x13: case 0x16: case 0x1e:
            set_state(dcv, 0x33);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x33);
        default:
            return 0;
    }
}

// [decomp] 0x00160d98 NDrone2_DSTATE_TruckDriverInitAlert
// Assustado no meio do script de entrada: pula para 90 quadros do fim dele e foge com medo (0x12).
uint32_t NDrone2_DSTATE_TruckDriverInitAlert(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags &= 0xffffffdf;
            dcv->drone->flags &= 0xffffff7f;
            ANIMSCRIPT_tag* s = dcv->drone->script;
            if (s == nullptr) return 1;
            s->frame = float(s->frame_count - 0x5a);
            return 1;
        }
        case 2:
            dcv->drone->flags |= 0x20;
            dcv->drone->flags |= 0x80;
            return 1;
        case 5:
            set_state(dcv, 0x12);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            return 0;
    }
}

// [decomp] 0x00160ed0 NDrone2_DSTATE_TruckDriverIdle
// Motorista parado; o jogador armado a menos de 6 m o assusta (0x12).
uint32_t NDrone2_DSTATE_TruckDriverIdle(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x12, 0, &ret)) return ret;
    if (alert_message(dcv, msg, false, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_SetStandIdleAnim(dcv, 0);
            return 1;
        case 3:
            if (NDrone2_NearArmedBond(dcv, 6.0f) != 0) {
                dcv->drone->flags |= 0x8000000;
                return set_state(dcv, 0x12);
            }
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x12);
        default:
            return 0;
    }
}

// [decomp] 0x00161030 NDrone2_DSTATE_TruckDriverMission
// Motorista seguindo a rota de missão; no fim fica parado (0x34). Peculiaridade mantida: o temporizador
// de um segundo só religa as flags 0x20/0x80.
uint32_t NDrone2_DSTATE_TruckDriverMission(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x12, 0, &ret)) return ret;
    if (alert_message(dcv, msg, false, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags &= 0xffffffdf;
            dcv->drone->flags &= 0xffffff7f;
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x37, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        case 2:
        case 0xc:
            dcv->drone->flags |= 0x20;
            dcv->drone->flags |= 0x80;
            return 1;
        case 3: {
            if (NDrone2_UpdateMissionRoute(dcv) != 3) {
                const uint8_t st = dcv->drone->route2.status;
                if (st != 9 && st != 0xc) return 1;
                if (dcv->drone->alert < 0.66f) {
                    DroneAnim_SetStandIdleAnim(dcv, 0);
                    return 1;
                }
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            behaviour_util_setProperty(0x1d, dcv->drone->behaviour, 0);
            return set_state(dcv, 0x34);
        }
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x12);
        default:
            return 0;
    }
}

namespace {
// Leva os impactos do rapel (6, 8 e 9) a DroneFunc_HandleImpact sem estado.
bool abseil_hit(DCVars_tag* dcv, MsgObject* msg, uint32_t* ret) {
    if (msg->msg != 6 && msg->msg != 8 && msg->msg != 9) return false;
    DroneFunc_HandleImpact(dcv, msg, 0, 0);
    *ret = 1;
    return true;
}
// O personagem está no chão (ANIMOBJ_tag::flags bit 8).
bool on_ground(obj_tag* obj) {
    if (obj->sphere_src == nullptr) {
        ++g_drone_combat_guards;  // port: o original leria o endereço 0 + 0x60
        return false;
    }
    return (anim_of(obj)->flags & 8) != 0;
}
// Volta a ser um inimigo comum (a subclasse e o tipo de antes do rapel).
void leave_rope(Drone_tag* d) {
    d->zero_g = 0;
    d->subclass = d->saved_subclass;
    d->type = d->base_type;
}
}  // namespace

// [decomp] 0x00169610 NDrone2_DSTATE_AbseilInit
// Pendurado na corda antes de descer (DAnim 0xb, sem gravidade) por até um segundo.
uint32_t NDrone2_DSTATE_AbseilInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (abseil_hit(dcv, msg, &ret)) return ret;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_SetDAnimNow(0xb, 0, 0, dcv);
            set_timer1(dcv->drone, GameState.frame + Rand_Rand(FRAME_RATE_INT));
            dcv->drone->zero_g = 1;
            dcv->drone->move_target.pos = dcv->obj->pos;
            dcv->drone->state_a = 0;
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        case 0:
            return 1;
        case 0xc:
            set_state(dcv, 0x8f);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00169730 NDrone2_DSTATE_AbseilSlide
// Descendo pela corda (animação 3, descida de 6,9) atirando; no chão, vira um inimigo comum e mira
// (0x67). O tipo 0x1c para (pendura: 0x90) perto da altura do oponente.
uint32_t NDrone2_DSTATE_AbseilSlide(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    uint32_t ret;
    if (abseil_hit(dcv, msg, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 3, 0, 0, 0, 0, dcv);
            drone->zero_g = 1;
            drone->unknown_50 = 6.9f;
            if (drone->type == 0x1b) drone->move_target.pos.y = -1000.0f;
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_AttackTalk(dcv);
            if (on_ground(obj)) {
                leave_rope(drone);
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0x67, 0, 0, dcv);
                drone->state_a = 1;
            }
            drone->firing = sees(dcv->drone) ? (drone->gun_in_wall == 0 ? 1 : 0) : 0;
            if (drone->type == 0x1c) {
                if (drone->opponent == nullptr) return set_state(dcv, 0x90);
                if (drone->opp_pos.y < drone->move_target.pos.y) drone->move_target.pos.y = drone->opp_pos.y;
                if (obj->pos.y < drone->opponent->pos.y + FRAME_RATE_MUL * 3.0f) return set_state(dcv, 0x90);
            }
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00169940 NDrone2_DSTATE_AbseilHang
// Pendurado (animação 2) atirando, a queda freando 2% por tick; se o oponente desce, volta a descer.
uint32_t NDrone2_DSTATE_AbseilHang(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    uint32_t ret;
    if (abseil_hit(dcv, msg, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 2, 0, 0, 0, 0, dcv);
            drone->zero_g = 1;
            dcv->sm->result = static_cast<uint32_t>(static_cast<int32_t>(Rand_FRand(1.0f)));
            drone->unknown_50 = 0.0f;
            DroneWeap_Fire(1, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_AttackTalk(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            Vec_MulR32(&drone->fall, &drone->fall, 1.0f - FRAME_RATE_DIV * 0.02f);
            drone->firing = sees(dcv->drone) ? (drone->gun_in_wall == 0 ? 1 : 0) : 0;
            if (drone->opp_pos.y < drone->move_target.pos.y) drone->move_target.pos.y = drone->opp_pos.y;
            if (drone->opponent != nullptr &&
                drone->opponent->pos.y + FRAME_RATE_MUL * 3.0f + float(static_cast<int32_t>(dcv->sm->result)) <
                    obj->pos.y) {
                return set_state(dcv, 0x8f);
            }
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001738e0 NDrone2_DSTATE_AbseilStepOff
// Larga a corda e mira (0x67) como um inimigo comum.
uint32_t NDrone2_DSTATE_AbseilStepOff(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (abseil_hit(dcv, msg, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            drone->anim_state = 0;
            leave_rope(drone);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0x67, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            drone->firing = 0;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00169b28 NDrone2_DSTATE_AbseilDeath
// Morto na corda: cai (animação 0x40) e, no chão, a DAnim 10 até morto (0x47); em 0700004a some quando o
// corpo para.
uint32_t NDrone2_DSTATE_AbseilDeath(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
        case 10:
            return 1;
        case 1:
            if (GameState.level_file == 0x700004a) {
                dcv->drone->flags &= 0xffffffdf;
                drone->flags |= 4;
            } else {
                obj->flags |= 0x20;
            }
            drone->state_a = 0;
            Drone_AlertStatusSet(3, dcv);
            NDrone2_LogDroneDeath(dcv);
            drone->zero_g = 0;
            drone->state_b = 0;
            drone->state_a = 1;
            DroneAnim_CallAnim(0.0f, 0x40, 0, 0, 0, 0, dcv);
            DroneWeap_DropWeapon(dcv);
            return 1;
        case 2:
            if (drone->state_a != 0) DroneFunc_SetDeathChannel(drone);
            return 1;
        case 3:
            if (GameState.level_file == 0x700004a) {
                if (obj->unknown_fc != 0) return 1;
                NDrone2_Enable(0, dcv->drone);
                dcv->obj->life |= 1;
                return 1;
            }
            if (drone->state_b != 0) return 1;
            if (on_ground(obj)) {
                DroneAnim_SetDAnimNow(10, 0, 0, dcv);
                drone->state_b = 1;
                drone->end_state = 0x47;
            }
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00168aa8 NDrone2_DSTATE_RunAwayFromObject
// Foge de um explosivo (DroneFunc_ConsiderExplosive) até um lugar seguro, gritando; sem lugar, se esconde
// (0x87); passado o tempo, se recompõe (0x88).
uint32_t NDrone2_DSTATE_RunAwayFromObject(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x88, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            drone->alert = 1.0f;
            NDrone2_DeathTalk(dcv);
            dcv->drone->flags |= 0x400000;
            Drone_AlertStatusSet(1, dcv);
            if (DroneMove_FindSafetyFromScaryObject(dcv)) return 1;
            return set_state(dcv, 0x87);
        case 2:
            drone->explosive_seen = 0;
            dcv->drone->flags &= 0xffbfffff;
            return 1;
        case 3: {
            NDrone2_DistanceToAIPoint(dcv, &dcv->drone->goal_point);
            if (drone->explosive_time < GameState.frame) return set_state(dcv, 0x88);
            uint32_t call;
            switch (NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0x300)) {
                case 0: case 1: case 2:
                    call = 0x1e;
                    break;
                case 9:
                case 0xc:
                    if (DroneAnim_CanDoAnimState(dcv, 0x11, 0)) {
                        call = 0x11;
                    } else if (DroneAnim_CanDoAnimState(dcv, 0x19, 0)) {
                        call = 0x19;
                    } else {
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        return 1;
                    }
                    break;
                case 10:
                    return 1;
                default:
                    drone->explosive_seen = 0;
                    back_to_initial(dcv);
                    return 1;
            }
            DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
            return 1;
        }
        case 0x21:
            return explosive_message(dcv, msg, 0x88);
        default:
            return 0;
    }
}

// [decomp] 0x00168d18 NDrone2_DSTATE_HideFromScaryObject
// Agachado com medo do explosivo (animação 0x11 ou 0x19) até o tempo dele passar (0x88).
uint32_t NDrone2_DSTATE_HideFromScaryObject(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret;
    if (impact_messages(dcv, msg, 0x88, 0, &ret)) return ret;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            drone->explosive_seen = 1;
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            uint32_t call;
            if (DroneAnim_CanDoAnimState(dcv, 0x11, 0)) {
                call = 0x11;
            } else if (DroneAnim_CanDoAnimState(dcv, 0x19, 0)) {
                call = 0x19;
            } else {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                return 1;
            }
            DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
            return 1;
        }
        case 2:
            drone->explosive_seen = 0;
            return 1;
        case 3:
            if (GameState.frame <= drone->explosive_time) return 1;
            return set_state(dcv, 0x88);
        case 0x21:
            return explosive_message(dcv, msg, 0x88);
        default:
            return 0;
    }
}

// [decomp] 0x00173658 NDrone2_DSTATE_RecoverFromScaryObject
// Passado o perigo: o tipo 0xc volta à rota de missão (0x20, ou 99 sem ela); quem ia ao alarme volta a
// ele (0x89); os outros atacam.
uint32_t NDrone2_DSTATE_RecoverFromScaryObject(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (drone->type == 0xc) return set_state(dcv, NDrone2_ReFindMissionPath(dcv) == 0 ? 99 : 0x20);
    drone->explosive_seen = 0;
    if (dcv->drone->return_state != 0x89) {
        set_state(dcv, 0x56);
        return 1;
    }
    return set_state(dcv, 0x89);
}

// [decomp] 0x00169488 NDrone2_DSTATE_ElevatorJumper
// Abre a porta do elevador mais perto, corre para fora quando ela abre e ataca meio segundo depois.
uint32_t NDrone2_DSTATE_ElevatorJumper(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            NDrone2_FindElevatorDoor(dcv);
            NDrone2_OpenDoor(dcv);
            dcv->drone->state_a = 0;
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            if (dcv->drone->state_a == 0 && NDrone2_DoorIsOpen(dcv) != 0) {
                DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                dcv->drone->state_a = 1;
                set_timer1(dcv->drone, frame_plus(30.0f));
            }
            return 1;
        case 0xc:
            set_state(dcv, 0x56);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173570 NDrone2_DSTATE_HoldItRightThere
// "Parado aí!": alerta total, mirando no oponente.
uint32_t NDrone2_DSTATE_HoldItRightThere(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            dcv->drone->alert = 1.0f;
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00173f68 NDrone2_DSTATE_FailMission
// Falha a missão (alarme, canal 99) e volta ao estado inicial.
uint32_t NDrone2_DSTATE_FailMission(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    DroneFunc_SetMissionFailReason(drone, 0);
    back_to_initial(dcv);
    return 1;
}

// [decomp] 0x00172870 NDrone2_DSTATE_StandBlind
uint32_t NDrone2_DSTATE_StandBlind(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    DroneAnim_CallAnim(0.0f, 0x24, 0, 0, 0, 0, dcv);
    return 1;
}

// [decomp] 0x001743d8 NDrone2_DSTATE_HangUp
// Congela as animações (velocidade 0 nos dois scripts em curso).
uint32_t NDrone2_DSTATE_HangUp(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 1: {
            if (dcv->drone->script != nullptr) dcv->drone->script->speed = 0.0f;
            ANIMSCRIPT_tag* second;
            second = nf::p32_load<ANIMSCRIPT_tag>(dcv->drone->unknown_534);
            if (second != nullptr) second->speed = 0.0f;
            return 1;
        }
        case 0:
        case 3:
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00178678 NDrone2_NearArmedBond
// O oponente à vista (propriedades 0x3c e 0x51), mais perto que radius, está armado: em 07000009/0a basta
// vê-lo; em 07000007 conta também o próprio personagem armado.
uint32_t NDrone2_NearArmedBond(DCVars_tag* dcv, float radius) {
    if (!prop(dcv->drone, 0x3c)) return 0;
    if (!prop(dcv->drone, 0x51)) return 0;
    if (radius <= dcv->drone->opp_dist) return 0;
    if (!sees(dcv->drone)) return 0;
    if (GameState.level_file - 0x7000009u < 2) return 1;
    if (DroneWeap_OpponentHasWeapon(dcv) != 0) return 1;
    if (GameState.level_file != 0x7000007) return 0;
    return NDrone2_ObjectIsArmed(dcv->obj) != 0 ? 1 : 0;
}

// ---- interrogatório ----

namespace {

// Interrogando: o oponente sacou a arma depois de ter estado desarmado (sight_flags bit 0x10 = armado
// no começo; se ele guarda a arma, o bit cai). Devolve true se agora está armado sem o bit.
bool interrogate_drew_weapon(DCVars_tag* dcv) {
    if ((dcv->drone->sight_flags & 0x10) != 0) {
        if (DroneWeap_OpponentHasWeapon(dcv) == 0) dcv->drone->sight_flags &= ~0x10u;
        if ((dcv->drone->sight_flags & 0x10) != 0) return false;
    }
    return DroneWeap_OpponentHasWeapon(dcv) != 0;
}

// A menor distância do oponente durante o interrogatório.
void interrogate_track_min(Drone_tag* d) {
    if (d->opp_dist < d->interrogate_min) d->interrogate_min = d->opp_dist;
}

// Impactos dos estados de interrogatório: 6 e 8 sem a flag, os outros com ela; 0x21 explosivo; alertas
// (incluindo o som 0x14) e o ataque forçado (0x1f). Devolve false se não é nenhuma dessas.
bool interrogate_messages(DCVars_tag* dcv, MsgObject* msg, uint32_t* ret) {
    if (impact_messages(dcv, msg, 0x56, 0, ret)) return true;
    if (alert_message(dcv, msg, true, ret)) return true;
    if (msg->msg == 0x1f) {
        DroneFunc_DoForcedAttack(dcv);
        *ret = 1;
        return true;
    }
    if (msg->msg == 0x21) {
        *ret = explosive_message(dcv, msg, 0x56);
        return true;
    }
    return false;
}

}  // namespace

// [decomp] 0x001616c0 NDrone2_DSTATE_InterogateAssist
// 0x39: soube que outro está interrogando (DroneFunc_FirstAlertState). Espera até 2 s (sorteados) e
// vai até o oponente (10 m dele, GoToGoalPosition voltando a 0x3a) ou, sem rota, espera ali (0x3a).
uint32_t NDrone2_DSTATE_InterogateAssist(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            DroneVision_SetInterogateAssist(dcv->drone);
            const uint32_t r = Rand_Rand(0x78);
            set_timer1(dcv->drone, fptoui(float(GameState.frame) + float(r) * FRAME_RATE_DIV));
            return 1;
        }
        case 0xc:
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            AINetwork_InvalidateRoute(&dcv->drone->route);
            AINetwork_SetupGoalPositionToObj(&dcv->drone->goal_target, &dcv->drone->goal_point, dcv->drone->opponent,
                                             10.0f, 1);
            if (NDrone2_CanReachGoalPosition(dcv) != 0) {
                dcv->drone->initial_state = 0x3a;
                return set_state(dcv, 99);
            }
            set_state(dcv, 0x3a);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00161860 NDrone2_DSTATE_InterogateAssistWait
// 0x3a: perto do interrogatório, de arma em punho (desarmado, a animação 0x21), olhando o oponente
// até o interrogador terminar (DroneVision_TestEndInterogationAssist); então ataca.
uint32_t NDrone2_DSTATE_InterogateAssistWait(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneVision_SetInterogateAssist(dcv->drone);
            DroneAnim_CallAnim(0.0f, weapon_in_hand(dcv->obj) < 2 ? 0x21 : 10, 0, 0, 0, 0, dcv);
            return 1;
        case 2:
            DroneVision_EndInterogateAssist(dcv->drone);
            return 1;
        case 3:
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            if (dcv->drone->alerted_by == nullptr) return set_state(dcv, 0x56);
            if (DroneVision_TestEndInterogationAssist(dcv->drone) == 0) {
                NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                return 1;
            }
            return set_state(dcv, 0x56);
        case 6: case 7: case 8: case 9: case 0x17: case 0x18: case 0x19:
            DroneFunc_HandleImpact(dcv, msg, 0x56, 1);
            return 1;
        default:
            if (alert_message(dcv, msg, true, &ret)) return ret;
            if (msg->msg == 0x1f) {
                DroneFunc_DoForcedAttack(dcv);
                return 1;
            }
            if (msg->msg == 0x21) return explosive_message(dcv, msg, 0x56);
            return 0;
    }
}

// [decomp] 0x001729f0 NDrone2_DSTATE_Interogator
// 0x3b: só passa para o estado 4.
uint32_t NDrone2_DSTATE_Interogator(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    set_state(dcv, 4);
    return 1;
}

// [decomp] 0x00161a40 NDrone2_DSTATE_Interogate
// 0x3c: o oponente à vista, armado (a propriedade 0x1a em DroneFunc_FirstSightState), é interrogado:
// o personagem aponta a arma, avisa os outros (DroneVision_BeginInterogate), e segue um roteiro de
// falas e animações pelo temporizador 1 (07000005: seis passos com as falas 0x49, 0x4d e 0x4e;
// 07000007: a fala 0x51; nas outras, três animações), ao fim do qual ataca. Ataca antes se o
// oponente saca a arma (tendo estado desarmado) ou se afasta 4 m além do ponto mais perto em que
// esteve. Sem oponente, 0x60; civil ou desarmado, ataca direto.
uint32_t NDrone2_DSTATE_Interogate(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            if (NDrone2_IsCivilian(dcv) != 0) return set_state(dcv, 0x56);
            if (weapon_in_hand(dcv->obj) < 2) return set_state(dcv, 0x56);
            DroneVision_BeginInterogate(dcv->drone);
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            Drone_AlertStatusSet(1, dcv);
            if (DroneWeap_OpponentHasWeapon(dcv) == 0) {
                dcv->drone->sight_flags &= ~0x10u;
            } else {
                dcv->drone->sight_flags |= 0x10;
            }
            Drone_tag* d = dcv->drone;
            d->interrogate_dist = d->opp_dist;
            d->interrogate_min = d->opp_dist;
            d->interrogate_pos = d->opponent->pos;
            d->interrogate_time = GameState.frame;  // o original grava 1.0f antes e sobrescreve
            set_timer1(d, frame_plus(60.0f));
            d->state_a = 0;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        }
        case 2:
            DroneVision_EndInterogate(dcv->drone);
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (interrogate_drew_weapon(dcv)) {
                NDrone2_PlaySFX(dcv, 0x4a, nullptr, 1, 1.0f);
                return set_state(dcv, 0x56);
            }
            interrogate_track_min(dcv->drone);
            if (dcv->drone->interrogate_min - dcv->drone->opp_dist < -4.0f) {
                NDrone2_SpeechSFX(dcv, 0x4a, 0, 1, 1);
                return set_state(dcv, 0x56);
            }
            return 1;
        case 0xc: {
            if (drone->opponent == nullptr) return set_state(dcv, 0x60);
            Drone_tag* d = dcv->drone;
            const uint32_t step = d->state_a++;
            float wait = 0.0f;
            if (GameState.level_file == 0x7000005) {
                switch (step) {
                    case 0:
                        DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                        NDrone2_SpeechSFX(dcv, 0x49, 0, 1, 1);
                        wait = 337.0f;
                        break;
                    case 1:
                        DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                        NDrone2_SpeechSFX(dcv, 0x4d, 0, 1, 1);
                        wait = 60.0f;
                        break;
                    case 2:
                        DroneAnim_CallAnim(0.0f, 0xb, 0, 0, 0, 0, dcv);
                        wait = 120.0f;
                        break;
                    case 3:
                        DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                        wait = 60.0f;
                        break;
                    case 4:
                        NDrone2_SpeechSFX(dcv, 0x4e, 0, 1, 1);
                        wait = 45.0f;
                        break;
                    case 5:
                        return set_state(dcv, 0x56);
                    default:
                        return 1;
                }
            } else if (GameState.level_file == 0x7000007) {
                if (step == 1) return set_state(dcv, 0x56);
                if (step != 0) return 1;
                DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                NDrone2_SpeechSFX(dcv, 0x51, 0, 1, 1);
                wait = 180.0f;
            } else {
                switch (step) {
                    case 0:
                        DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                        wait = 180.0f;
                        break;
                    case 1:
                        DroneAnim_CallAnim(0.0f, 0xb, 0, 0, 0, 0, dcv);
                        wait = 180.0f;
                        break;
                    case 2:
                        DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                        wait = 60.0f;
                        break;
                    case 3:
                        return set_state(dcv, 0x56);
                    default:
                        return 1;
                }
            }
            set_timer1(dcv->drone, fptoui(float(GameState.frame) + FRAME_RATE_DIV * wait));
            return 1;
        }
        default:
            if (interrogate_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x00162338 NDrone2_DSTATE_InterogateWalk
// 0x3d: anda (animação 0x35) olhando o oponente; a menos de 4 m dele, interroga (0x3c). Como no
// interrogatório, ataca se ele saca a arma ou se afasta 4 m além do ponto mais perto (interrogate_min,
// que este estado não reinicia).
uint32_t NDrone2_DSTATE_InterogateWalk(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x35, 0, 0, 0, 0, dcv);
            return 1;
        case 3: {
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (interrogate_drew_weapon(dcv)) return set_state(dcv, 0x56);
            interrogate_track_min(dcv->drone);
            const float dist = dcv->drone->opp_dist;
            if (dist < 4.0f) return set_state(dcv, 0x3c);
            if (-4.0f <= dcv->drone->interrogate_min - dist) return 1;
            return set_state(dcv, 0x56);
        }
        default:
            if (interrogate_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x00162558 NDrone2_DSTATE_CivilianChallenge
// 0x3e: um civil interpela o oponente (07000009/0a, DroneFunc_FirstSightState): animação 0x3e e uma
// das nove falas 0xc3-0xcb, olhando para ele; ao fim da animação, 0x89. Impactos e explosivos levam
// a 0x89; não reage a alertas.
uint32_t NDrone2_DSTATE_CivilianChallenge(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags2 |= 0x20;
            DroneAnim_CallAnim(0.0f, 0x3e, 0, 0x89, 0, 0, dcv);
            dcv->drone->sound = NDrone2_SpeechSFX(dcv, Rand_Rand(9) + 0xc3, 0, 0, 0);
            return 1;
        case 0:
            return 1;
        case 2:
            DroneVision_EndInterogate(dcv->drone);
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x89);
        default:
            if (impact_messages(dcv, msg, 0x89, 0, &ret)) return ret;
            return 0;
    }
}

// ---- aliados e a Kiko ----

namespace {

// Impactos e explosivos dos estados de aliado: a reação volta ao estado atual (6 e 8 sem a flag).
bool ally_messages(DCVars_tag* dcv, MsgObject* msg, uint32_t* ret) {
    if (impact_messages(dcv, msg, dcv->sm->state, 0, ret)) return true;
    if (msg->msg == 0x21) {
        *ret = explosive_message(dcv, msg, dcv->sm->state);
        return true;
    }
    return false;
}

// Um inimigo armado perto (10 m, visto, com a propriedade 0x50): o aliado corre para um ponto de fuga
// (AllyGoToGoalPosition voltando a se esconder, 0x1f) ou se esconde ali mesmo (0x1f).
bool ally_threatened(DCVars_tag* dcv, uint32_t* ret) {
    if (NDrone2_NearArmedDrone(dcv, 10.0f, 1) == 0) return false;
    if (NDrone2_FindRunToPoint(dcv) != 0) {
        dcv->drone->initial_state = 0x1f;
        *ret = set_state(dcv, 0x28);
    } else {
        *ret = set_state(dcv, 0x1f);
    }
    return true;
}

// Entrada dos estados que guiam pela rota de missão (route2).
void ally_lead_enter(DCVars_tag* dcv, Drone_tag* drone, uint8_t alert, uint32_t call) {
    Drone_AlertStatusSet(alert, dcv);
    dcv->drone->flags |= 0x10000;
    drone->active_route = &drone->route2;
    DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
}

// Distância, pela rota de missão, até o nó mais perto do jogador (DroneMove_ObjectMissionPathDistance;
// negativa: o jogador ficou para trás), guardada na máquina de estados.
float ally_path_dist(DCVars_tag* dcv) {
    dcv->sm->path_dist = DroneMove_ObjectMissionPathDistance(dcv, NDrone2_Player(), 5.0f);
    return dcv->sm->path_dist;
}

}  // namespace

// [decomp] 0x0015df28 NDrone2_DSTATE_KikoMission
// 0x19: segue a rota de missão (animação 0x34); no fim, perde a propriedade 0x1d e fica parada (0x18).
// Vendo o oponente, um alerta ou um ataque forçado, corre (0x1a); em 07000016 nada disso a desvia.
uint32_t NDrone2_DSTATE_KikoMission(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(0.0f, 0x34, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            if (GameState.level_file == 0x7000016 || DroneVision_EnemyLookForOpponent(dcv) == 0) {
                if (NDrone2_UpdateMissionRoute(dcv) != 3) return 1;
                behaviour_util_setProperty(0x1d, dcv->drone->behaviour, 0);
                return set_state(dcv, 0x18);
            }
            return set_state(dcv, 0x1a);
        case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x1e:
            if (GameState.level_file == 0x7000016) return 1;
            if (DroneVision_EnemyAlerts(dcv, msg) == 0) return 1;
            return set_state(dcv, 0x1a);
        case 0x1f:
            if (GameState.level_file == 0x7000016) return 1;
            return set_state(dcv, 0x1a);
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015e0d8 NDrone2_DSTATE_KikoMissionRun
// 0x1a: corre pela rota de missão (0x1e) e, no fim, vai ao alarme (0x89).
uint32_t NDrone2_DSTATE_KikoMissionRun(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            if (NDrone2_UpdateMissionRoute(dcv) == 3) return set_state(dcv, 0x89);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015e698 NDrone2_DSTATE_AllyLead
// 0x1d: o aliado guia o jogador pela rota de missão (correndo, 0x1e; parado nos nós de espera, 0x21);
// no fim, 0x23. Com o jogador à frente dele (30 graus) a menos de 3 m, sai do caminho (0x1e); com o
// jogador para trás (mais de 9 m pela rota, ou longe e fora dela), espera (0x20); entre 15 e 25 m do
// fim da rota, espera o jogador também quando ele não está à frente ou há combate (0x22). A mensagem
// 0x1b manda esperar.
uint32_t NDrone2_DSTATE_AllyLead(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            Drone_AlertStatusSet(1, dcv);
            drone->active_route = &drone->route2;
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3: {
            if (ally_threatened(dcv, &ret)) return ret;
            switch (NDrone2_UpdateMissionRoute(dcv)) {
                case 0: case 2:
                    DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
                    break;
                case 3:
                    return set_state(dcv, 0x23);
                case 9: case 0xc:
                    DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
                    break;
                default:
                    break;
            }
            if (NDrone2_BondWithinAngle(dcv, 0.5235988f) != 0 && NDrone2_Player() != nullptr &&
                Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 3.0f) {
                return set_state(dcv, 0x1e);
            }
            const float path = ally_path_dist(dcv);
            if (path == 0.0f) {
                if (NDrone2_Player() == nullptr) return set_state(dcv, 0x20);
                if (5.0f <= Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos)) return set_state(dcv, 0x20);
            }
            if (dcv->sm->path_dist < -9.0f) return set_state(dcv, 0x20);
            if (25.0f <= drone->route2.distance) return 1;
            if (drone->route2.distance <= 15.0f) return 1;
            if (0.0f <= dcv->sm->path_dist && NPCGlobals.prev_counts[4] == 0) return 1;
            return set_state(dcv, 0x22);
        }
        case 0x1b:
            set_state(dcv, 0x20);
            return 1;
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015ea28 NDrone2_DSTATE_AllyLeadPlayerInWay
// 0x1e: o jogador está no caminho: espera (0x21) enquanto ele estiver à frente (60 graus) a menos de
// 3,5 m; depois volta a guiar (0x1d).
uint32_t NDrone2_DSTATE_AllyLeadPlayerInWay(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            Drone_AlertStatusSet(2, dcv);
            dcv->drone->flags |= 0x10000;
            drone->active_route = &drone->route2;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            if (ally_threatened(dcv, &ret)) return ret;
            if (NDrone2_BondWithinAngle(dcv, 1.0471976f) != 0 && NDrone2_Player() != nullptr &&
                Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 3.5f) {
                return 1;
            }
            return set_state(dcv, 0x1d);
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015ec08 NDrone2_DSTATE_AllyLeadHide
// 0x1f: escondido (0x19) até não haver inimigo armado a 14 m; 2 s depois, procura de novo a rota de
// missão (NDrone2_ReFindMissionPath) e espera o jogador nela (0x20).
uint32_t NDrone2_DSTATE_AllyLeadHide(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(2, dcv);
            drone->active_route = &drone->route2;
            DroneAnim_CallAnim(0.0f, 0x19, 0, 0, 0, 0, dcv);
            dcv->drone->state_a = 0;
            return 1;
        case 3:
            if (dcv->drone->state_a != 0) return 1;
            if (NDrone2_NotNearArmedDrone(dcv, 14.0f) == 0) return 1;
            dcv->drone->state_a = 1;
            set_timer1(dcv->drone, GameState.frame + FRAME_RATE_INT * 2);
            return 1;
        case 0xc:
            if (NDrone2_ReFindMissionPath(dcv) == 0) return 1;
            return set_state(dcv, 0x20);
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015eda8 NDrone2_DSTATE_AllyLeadWait
// 0x20: espera o jogador (0x21) até ele chegar a menos de 7 m pela rota (ou a 5 m, fora dela); então
// volta a guiar (0x1d).
uint32_t NDrone2_DSTATE_AllyLeadWait(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            ally_lead_enter(dcv, drone, 2, 0x21);
            return 1;
        case 0:
            return 1;
        case 3: {
            if (ally_threatened(dcv, &ret)) return ret;
            if (ally_path_dist(dcv) <= -7.0f) return 1;
            if (dcv->sm->path_dist == 0.0f) {
                if (NDrone2_Player() == nullptr) return 1;
                if (5.0f <= Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos)) return 1;
            }
            return set_state(dcv, 0x1d);
        }
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015efc8 NDrone2_DSTATE_AllyLeadMissionWait
// 0x21: parado no próximo nó da rota de missão até o canal do switch (o parâmetro da troca de estado,
// 0x99 sem ele) ligar; então volta a guiar (0x1d).
uint32_t NDrone2_DSTATE_AllyLeadMissionWait(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            const uint32_t channel = drone->sm.change_param;
            drone->state_a = channel != 0 ? channel : 0x99;
            drone->active_route = &drone->route2;
            Drone_AlertStatusSet(2, dcv);
            dcv->drone->flags |= 0x10000;
            NDrone2_MoveTo_RouteNode(dcv, 1, &dcv->drone->route2);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        }
        case 3:
            if (switch_channels[drone->state_a] != 0) return set_state(dcv, 0x1d);
            return 1;
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f120 NDrone2_DSTATE_AllyLeadBondCombat
// 0x22: espera enquanto o jogador está para trás pela rota ou há combate; depois volta a guiar (0x1d).
uint32_t NDrone2_DSTATE_AllyLeadBondCombat(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            ally_lead_enter(dcv, drone, 2, 0x21);
            return 1;
        case 3:
            if (ally_threatened(dcv, &ret)) return ret;
            if (ally_path_dist(dcv) < 0.0f) return 1;
            if (NPCGlobals.prev_counts[4] != 0) return 1;
            return set_state(dcv, 0x1d);
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f2e8 NDrone2_DSTATE_AllyLeadDone
// 0x23: chegou ao fim da rota de missão.
uint32_t NDrone2_DSTATE_AllyLeadDone(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f3c8 NDrone2_DSTATE_AllyFollowInit
// 0x24: parado até ver o oponente; então segue o jogador (0x25).
uint32_t NDrone2_DSTATE_AllyFollowInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            if (sees(dcv->drone)) return set_state(dcv, 0x25);
            return 1;
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f4e0 NDrone2_DSTATE_AllyFollow
// 0x25: corre atrás do jogador (até 2 m, NDrone2_MoveToObject) olhando o oponente; a menos de 4 m dele,
// ou sem caminho, espera (0x26).
uint32_t NDrone2_DSTATE_AllyFollow(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x1e, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (NDrone2_Player() != nullptr && Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 4.0f) {
                return set_state(dcv, 0x26);
            }
            switch (NDrone2_MoveToObject(dcv, NDrone2_Player(), 2.0f, 1)) {
                case 0: case 1: case 2: case 10:
                    return 1;
                default:
                    set_state(dcv, 0x26);
                    return 1;
            }
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f6a0 NDrone2_DSTATE_AllyFollowWait
// 0x26: parado olhando o oponente enquanto o jogador está a menos de 8 m; quando ele se afasta e há
// caminho até ele, volta a segui-lo (0x25).
uint32_t NDrone2_DSTATE_AllyFollowWait(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(2, dcv);
            dcv->drone->flags |= 0x10000;
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (NDrone2_Player() != nullptr && Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos) < 8.0f) return 1;
            switch (NDrone2_MoveToObject(dcv, NDrone2_Player(), 2.0f, 1)) {
                case 0: case 1: case 2:
                    return set_state(dcv, 0x25);
                default:
                    return 1;
            }
        default:
            if (ally_messages(dcv, msg, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015f858 NDrone2_DSTATE_AllyFollowDone
// 0x27: parou de seguir.
uint32_t NDrone2_DSTATE_AllyFollowDone(DCVars_tag* dcv, Drone_tag* drone, obj_tag* obj, MsgObject* msg) {
    return NDrone2_DSTATE_AllyLeadDone(dcv, drone, obj, msg);
}

// [decomp] 0x00153a30 NDrone2_ReFindMissionPath
// Volta à rota de missão (route2) depois de sair dela: entre os nós da rota, o mais perto alcançável em
// linha reta dos pés (NDrone2_MoveTest; nunca para quem tem a flag 0x10) vira o atual e o personagem
// anda até ele pela rede (devolve 1). Sem nenhum alcançável, o nó mais perto vira o atual e, se há rota
// pela rede até ele, o personagem vai até lá (GoToGoalPosition, 99; devolve 0); senão o alvo da rota
// vira o jogador (devolve 0). Os dois primeiros casos também apontam move_target para o próximo ponto
// da rota de missão.
uint32_t NDrone2_ReFindMissionPath(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    AIRoute_tag* r2 = &d->route2;
    uint32_t reach_i = 0xffff;
    uint32_t near_i = 0xffff;
    float reach_d2 = 400.0f;
    float near_d2 = 400.0f;
    CelPos_tag reach{};
    CelPos_tag nearest{};
    for (uint32_t i = 0; i < r2->count; ++i) {
        const MAPNODE_tag* node = &r2->path->map_nodes[r2->list[i]];
        const float d2 = Vec_SqDist2D(&node->pos, &obj->pos);
        if (reach_i == 0xffff || d2 < reach_d2) {
            CelPos_tag cand{};
            cand.pos = node->pos;
            cand.cel = node->cel;
            bool ok = false;
            if (d == nullptr || (d->flags & 0x10) == 0) {
                CelPos_tag from{};
                NDrone2_FeetPos(obj, &from.pos);
                from.cel = obj->cel;
                if (cand.cel == nullptr) cand.cel = NDrone2_FindCel(&cand.pos);
                DroneReachCheckPos = cand.pos;
                ok = NDrone2_MoveTest(&from, &cand, nullptr, obj, nullptr, 0, 1) == 1;
            }
            if (ok) {
                reach = cand;
                reach_d2 = d2;
                reach_i = i;
            }
        }
        if (near_i == 0xffff || d2 < near_d2) {
            nearest.pos = node->pos;
            nearest.cel = node->cel;
            near_d2 = d2;
            near_i = i;
        }
    }
    const auto aim_at_next = [&]() {
        if (LinkCreep_Calc(r2, 0) == 0) return;
        LinkCreep_Dest(r2, 0, &r2->next);
        d->move_target.pos = r2->next.pos;
        if (r2->next.cel == nullptr) r2->next.cel = NDrone2_FindCel(&r2->next.pos);
        d->move_target.cel = r2->next.cel;
        d->move_arrive = 0.3f;
        _VECTOR feet;
        NDrone2_FeetPos(obj, &feet);
        d->move_dist = Vec_Dist2D(&feet, &d->move_target.pos);
        NDrone2_GetAnglesToMoveTarget(dcv, &d->move_angles);
    };
    if (reach_i != 0xffff) {
        r2->current = static_cast<int16_t>(reach_i);
        aim_at_next();
        dcv->drone->active_route = nullptr;
        AINetwork_InvalidateRoute(&dcv->drone->route);
        AINetwork_InvalidateRoute(&dcv->drone->route);
        AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &reach, 2.0f, 0);
        NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0);
        return 1;
    }
    if (near_i != 0xffff) {
        r2->current = static_cast<int16_t>(near_i);
        aim_at_next();
        if (d->klass == 9) d->route.path = AINetwork_NavPathForPosition(&obj->pos, obj->cel, 1);
        AINetwork_ResetRoute(&d->route, obj, nullptr);
        AINetwork_InvalidateRoute(&d->route);
        AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, &nearest, 1.0f, 0);
        if (NDrone2_MoveToGoalPosition(dcv, &dcv->drone->goal_point.pos, dcv->drone->goal_point.radius, 0) < 4) {
            set_state(dcv, 99);
            return 0;
        }
    }
    obj_tag* player = NDrone2_Player();
    if (player == nullptr) {
        // port: o original leria a posição do jogador nulo.
        ++g_drone_combat_guards;
        return 0;
    }
    CelPos_tag at{};
    at.pos = player->pos;
    at.cel = player->cel;
    AINetwork_ResetRoute(&d->route, obj, nullptr);
    AINetwork_InvalidateRoute(&d->route);
    AINetwork_SetupGoalPosition(&d->goal_target, &d->goal_point, &at, 2.0f, 0);
    return 0;
}

// ---- reféns ----

// [decomp] 0x0014aaf8 NDrone2_HostageSituationActive
// Vazia no jogo.
void NDrone2_HostageSituationActive(DCVars_tag*) {}

// [decomp] 0x0014a808 NDrone2_SetupHostageExecute
// O sequestrador e o refém (unknown_2b0) se olham: cada um aponta move_target para o outro e
// move_angles (só o giro) na direção dele.
void NDrone2_SetupHostageExecute(DCVars_tag* dcv) {
    obj_tag* me = dcv->obj;
    obj_tag* other = dcv->drone->unknown_2b0;
    if (other == nullptr || me == nullptr) return;
    Drone_tag* md = static_cast<Drone_tag*>(me->data);
    Drone_tag* od = static_cast<Drone_tag*>(other->data);
    md->move_target.pos = other->pos;
    md->move_target.cel = other->cel;
    od->move_target.pos = me->pos;
    od->move_target.cel = me->cel;
    const float dx = me->pos.x - other->pos.x;
    const float dz = me->pos.z - other->pos.z;
    md->move_angles.x = 0.0f;
    md->move_angles.y = ATAN2_APPROX(-dx, -dz);
    md->move_angles.z = 0.0f;
    od->move_angles.x = 0.0f;
    od->move_angles.y = ATAN2_APPROX(dx, dz);
    od->move_angles.z = 0.0f;
}

// [decomp] 0x00144030 NDrone2_SetupHostageKiller
// Sem refém ainda: o refém livre (tipo 5, sem par) mais perto, a menos de 5 m, vira o par deste
// (unknown_2b0 nos dois) e os dois se olham. 0 sem nenhum.
uint32_t NDrone2_SetupHostageKiller(DCVars_tag* dcv) {
    if (dcv->drone->unknown_2b0 != nullptr) return 1;
    Drone_tag* chosen = nullptr;
    float best = 3.4028235e38f;  // FLT_MAX (0x0030cb34)
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* e = reinterpret_cast<Drone_tag*>(n);
        if (e->type != 5 || e->unknown_2b0 != nullptr) continue;
        const float dist = Vec_Dist3D(&e->obj->pos, &dcv->obj->pos);
        if (dist < best && dist < 5.0f) {
            chosen = e;
            best = dist;
        }
    }
    if (chosen == nullptr) return 0;
    dcv->drone->unknown_2b0 = chosen->obj;
    chosen->unknown_2b0 = dcv->obj;
    NDrone2_SetupHostageExecute(dcv);
    return 1;
}

namespace {

// O som que o personagem ainda está tocando (sound); quando acaba, esquece-o.
bool still_talking(Drone_tag* d) {
    if (d->sound == 0) return false;
    if (Sound_IsFinnished(d->sound) == 0) return true;
    d->sound = 0;
    return false;
}

// O par (unknown_2b0) tem vida? (Sem par, o original leria um ponteiro nulo.)
bool partner_alive(Drone_tag* d) {
    if (d->unknown_2b0 == nullptr || d->unknown_2b0->data == nullptr) {
        ++g_drone_combat_guards;
        return false;
    }
    return 0.0f < static_cast<Drone_tag*>(d->unknown_2b0->data)->health;
}

// Mensagem 5 do sequestrador (fim da animação de execução): o refém recebe a mensagem 0xb (morre) e
// o sequestrador ataca.
uint32_t execute_hostage(DCVars_tag* dcv) {
    obj_tag* h = dcv->drone->unknown_2b0;
    if (h == nullptr || h->data == nullptr) {
        ++g_drone_combat_guards;  // port: o original leria o par nulo
    } else {
        Drone_SM_SendMsg(0xb, 10, dcv->sm->id, static_cast<Drone_tag*>(h->data)->sm.id);
    }
    set_state(dcv, 0x56);
    return 1;
}

}  // namespace

// [decomp] 0x00144340 NDrone2_MonitorTalkToMissionObj
// A conversa entre o sequestrador e o refém (unknown_4ce é a vez): 0, o sequestrador fala (mensagem
// 0x1a a si mesmo); 1, quando a fala dele acaba, o refém fala; 2, quando a do refém acaba, de novo o
// sequestrador (volta a 1).
uint32_t NDrone2_MonitorTalkToMissionObj(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    switch (d->unknown_4ce) {
        case 0:
            Drone_Message(dcv->obj, 0x1a, 0, 0);
            ++d->unknown_4ce;
            return 1;
        case 1:
            if (still_talking(d)) return 1;
            Drone_Message(dcv->drone->unknown_2b0, 0x1a, 0, 0);
            ++dcv->drone->unknown_4ce;
            return 1;
        case 2: {
            obj_tag* h = d->unknown_2b0;
            if (h == nullptr || h->data == nullptr) {
                ++g_drone_combat_guards;  // port: o original leria o par nulo
                return 1;
            }
            if (still_talking(static_cast<Drone_tag*>(h->data))) return 1;
            Drone_Message(dcv->obj, 0x1a, 0, 0);
            dcv->drone->unknown_4ce = 1;
            return 1;
        }
        default:
            return 1;
    }
}

// [decomp] 0x0014a8b8 NDrone2_TalkToMissionObj
// Fala da conversa: o sequestrador (tipo 4) uma das seis 0x29-0x2e, o refém (5) uma das quatro
// 0x2f-0x32.
uint32_t NDrone2_TalkToMissionObj(DCVars_tag* dcv) {
    uint32_t sfx;
    if (dcv->drone->type == 4) {
        sfx = Rand_Rand(6) + 0x29;
    } else if (dcv->drone->type == 5) {
        sfx = Rand_Rand(4) + 0x2f;
    } else {
        return 0;
    }
    if (sfx != 0) NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return 1;
}

// [decomp] 0x001441c0 DroneFunc_HostageSaved
// Um refém salvo: conta (NPCGlobals.unknown_19c) e liga o canal do parâmetro 6 (se ainda desligado; ligado,
// não conta). Em 07000002/03 mostra a mensagem do primeiro refém (0x200004f) ou dos seguintes
// (0x2000050).
void DroneFunc_HostageSaved(DCVars_tag* dcv) {
    const uint8_t ch = dcv->drone->channel_b;
    if (switch_channels[ch] != 0) return;
    ++NPCGlobals.unknown_19c;
    if (dcv->drone->channel_b != 0) {
        switch_channels[dcv->drone->channel_b] = 1;
        switch_channels_time[dcv->drone->channel_b] = GameState.frame;
    }
    if (GameState.level_file != 0x7000002 && GameState.level_file != 0x7000003) return;
    const uint32_t label = NPCGlobals.unknown_19c == 1 ? 0x200004f : 0x2000050;
    Text_AddMsg(0, 0, 1, Txt_BindLabel(label, 0), 0, 0xb4);
}

// [decomp] 0x0015c728 NDrone2_DSTATE_HostageKiller
// 8: o sequestrador pega o refém mais perto (NDrone2_SetupHostageKiller; sem refém, 4), aponta a arma
// (9) e conversa com ele; vendo o oponente ou alertado, segue a visão. No fim da animação de execução
// (5) mata o refém e ataca; a mensagem 10 começa a execução (0x58) se o refém vive.
uint32_t NDrone2_DSTATE_HostageKiller(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            if (NDrone2_SetupHostageKiller(dcv) == 0) return set_state(dcv, 4);
            dcv->drone->flags |= 0x10;
            DroneAnim_CallAnim(0.0f, 9, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            NDrone2_MonitorTalkToMissionObj(dcv);
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result == 0) return 1;
            return set_state(dcv, dcv->sm->result);
        case 5:
            return execute_hostage(dcv);
        case 10:
            if (!partner_alive(dcv->drone)) return 1;
            DroneAnim_CallAnim(0.0f, 0x58, 0, 0, 5, 0, dcv);
            return 1;
        case 0x1a:
            NDrone2_TalkToMissionObj(dcv);
            return 1;
        default:
            if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
            if (alert_message(dcv, msg, true, &ret)) return ret;
            if (msg->msg == 0x1f) {
                DroneFunc_DoForcedAttack(dcv);
                return 1;
            }
            if (msg->msg == 0x21) return explosive_message(dcv, msg, 0x56);
            return 0;
    }
}

// [decomp] 0x0015c940 NDrone2_DSTATE_HostageKillerAttack
// 9: com o refém vivo, encara-o (NDrone2_SetupHostageExecute) e 1 s depois (mensagem 10) começa a
// execução (0x58), que termina na mensagem 5 como em 8. Sem refém vivo, ataca (sem a flag 0x10 se veio
// do estado 1 ou não tem script de entrada).
uint32_t NDrone2_DSTATE_HostageKillerAttack(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1: {
            dcv->drone->flags |= 0x10000;
            Drone_AlertStatusSet(1, dcv);
            Drone_tag* d = dcv->drone;
            if (d->unknown_2b0 != nullptr && 0.0f < static_cast<Drone_tag*>(d->unknown_2b0->data)->health) {
                NDrone2_SetupHostageExecute(dcv);
                Drone_SM_SendMsgSelf(10, 0, FRAME_RATE_INT, 0, dcv);
                DroneAnim_CallAnim(0.0f, 9, 0, 0, 0, 0, dcv);
                return 1;
            }
            if (dcv->sm->state == 1 || d->enter_script == 0) d->flags &= ~0x10u;
            return set_state(dcv, 0x56);
        }
        case 3:
            NDrone2_SeenAndAttacking(dcv);
            return 1;
        case 5:
            return execute_hostage(dcv);
        case 10: {
            Drone_tag* d = dcv->drone;
            if (d->unknown_2b0 != nullptr) {
                if (d->enter_script == 0) d->flags &= ~0x10u;
                DroneAnim_CallAnim(0.0f, 0x58, 0, 0, 5, 0, dcv);
            }
            return 1;
        }
        default:
            if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
            if (msg->msg == 0x21) return explosive_message(dcv, msg, dcv->sm->state);
            return 0;
    }
}

// [decomp] 0x0015cb78 NDrone2_DSTATE_Hostage
// 0xa: refém rendido (0x21) enquanto o sequestrador vive; morto ou sem ele, salvo (0xc). A mensagem
// 0xb (a execução) o mata (0xb); 5 toca a DAnim 0xf4; 0x1a, a fala da conversa.
uint32_t NDrone2_DSTATE_Hostage(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3: {
            obj_tag* k = dcv->drone->unknown_2b0;
            if (k != nullptr && 0.0f < static_cast<Drone_tag*>(k->data)->health) {
                NDrone2_HostageSituationActive(dcv);
                return 1;
            }
            return set_state(dcv, 0xc);
        }
        case 5:
            DroneAnim_SetDAnimNow(0xf4, 0, 0, dcv);
            return 1;
        case 0xb:
            set_state(dcv, 0xb);
            return 1;
        case 0x1a:
            NDrone2_TalkToMissionObj(dcv);
            return 1;
        default:
            if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
            if (msg->msg == 0x21) return explosive_message(dcv, msg, dcv->sm->state);
            return 0;
    }
}

// [decomp] 0x001725a0 NDrone2_DSTATE_HostageDie
// 0xb: grita (som 10), cai (0x40, depois 0xf) e conta como morte.
uint32_t NDrone2_DSTATE_HostageDie(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    Drone_AlertStatusSet(3, dcv);
    NDrone2_PlaySFX(dcv, 10, nullptr, 1, 1.0f);
    DroneAnim_CallAnim(0.0f, 0x40, 0, 0xf, 0, 0, dcv);
    NDrone2_LogDroneDeath(dcv);
    return 1;
}

// [decomp] 0x0015ccf0 NDrone2_DSTATE_HostageSaved
// 0xc: salvo (DroneFunc_HostageSaved, música 0x10). Com estado de ataque e sem canal do parâmetro 6,
// fica parado (0xd) ou se esconde se há alguém armado a 10 m (0xe); senão foge para um ponto
// (HostageGoToGoalPosition, 100, e depois 0xe) ou se esconde ali (0xe).
uint32_t NDrone2_DSTATE_HostageSaved(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(0, dcv);
            dcv->drone->unknown_2b0 = nullptr;
            Music_Event(0x10, 1);
            DroneFunc_HostageSaved(dcv);
            if (dcv->drone->attack_state != 0 && dcv->drone->channel_b == 0) {
                return set_state(dcv, NDrone2_NearArmedPerson(dcv, 10.0f) != 0 ? 0xe : 0xd);
            }
            if (NDrone2_FindRunToPoint(dcv) == 0) {
                set_state(dcv, 0xe);
                return 1;
            }
            dcv->drone->initial_state = 0xe;
            return set_state(dcv, 100);
        default:
            if (impact_messages(dcv, msg, dcv->sm->state, 0, &ret)) return ret;
            if (msg->msg == 0x21) return explosive_message(dcv, msg, dcv->sm->state);
            return 0;
    }
}

// [decomp] 0x0015ce60 NDrone2_DSTATE_HostageIdle
// 0xd: calmo (propriedade 0x4c, alerta 0), olhando o oponente; um inimigo armado a 20 m o faz se
// esconder (0xe). Impactos e explosivos também levam a 0xe.
uint32_t NDrone2_DSTATE_HostageIdle(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->sight_flags |= 8;
            behaviour_util_setProperty(0x4c, dcv->drone->behaviour, 1);
            drone->alert = 0.0f;
            drone->flags &= ~0x4000000u;
            Drone_AlertStatusSet(0, dcv);
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            DroneAnim_CallAnim(0.0f, 0x24, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            if (NDrone2_NearArmedDrone(dcv, 20.0f, 0) != 0) return set_state(dcv, 0xe);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0xe);
        default:
            if (impact_messages(dcv, msg, 0xe, 0, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0015cfe0 NDrone2_DSTATE_HostageHide
// 0xe: agachado com medo (0x19, alerta 1) até não haver inimigo armado a 20 m; então 0xd.
uint32_t NDrone2_DSTATE_HostageHide(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 1:
            drone->flags |= 0x4000000;
            drone->alert = 1.0f;
            Drone_AlertStatusSet(2, dcv);
            DroneAnim_CallAnim(0.0f, 0x19, 0, 0, 0, 0, dcv);
            return 1;
        case 0:
            return 1;
        case 3:
            if (NDrone2_NearArmedDrone(dcv, 20.0f, 0) == 0) return set_state(dcv, 0xd);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0);
        default:
            if (impact_messages(dcv, msg, 0, 0, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x00172628 NDrone2_DSTATE_HostageDead
// 0xf: morto; com a propriedade 0x53 a missão falha (9). O corpo some como os outros (0x48 no
// temporizador).
uint32_t NDrone2_DSTATE_HostageDead(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            Drone_AlertStatusSet(3, dcv);
            NDrone2_SetAsDead(dcv);
            if (prop(dcv->drone, 0x53)) DroneFunc_SetMissionFailReason(drone, 9);
            dcv->drone->flags &= ~0x8000u;
            set_timer1(dcv->drone, GameState.frame + NPCGlobals.corpse_time);
            return 1;
        case 3:
            DroneFunc_DeadDrone(dcv);
            return 1;
        case 0xc:
            set_state(dcv, 0x48);
            return 1;
        default:
            return 0;
    }
}

// ---- castelo, emboscada e testes ----

// [decomp] 0x001612e0 NDrone2_DSTATE_CastleChatGuard1
// 0x36: guarda conversando (de arma em punho se o alerta passou de 0,66, senão a animação parada).
// Quando o canal do tipo de conversa liga, toca o script de entrada (uma vez); ao sair do estado com o
// script ainda pendente, liga o canal 0x1a. Alertas sem reação marcam a volta ao estado 4.
uint32_t NDrone2_DSTATE_CastleChatGuard1(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            if (0.66f <= dcv->drone->alert) {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 0, 0, 0, dcv);
            } else {
                DroneAnim_SetStandIdleAnim(dcv, 0);
            }
            return 1;
        case 2:
            if (dcv->drone->entry_script != 0) {
                switch_channels_time[0x1a] = GameState.frame;
                switch_channels[0x1a] = 1;
            }
            return 1;
        case 3: {
            const uint32_t script = dcv->drone->entry_script;
            if (script != 0 && switch_channels[dcv->drone->chat_type] != 0) {
                DroneAnim_SetScript(script, 0, 0x11a, 0, 0, 5, 0, dcv);
                dcv->drone->entry_script = 0;
            }
            dcv->sm->result = DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->sm->result == 0) return 1;
            return set_state(dcv, dcv->sm->result);
        }
        case 5:
            if (0.66f <= dcv->drone->alert) {
                DroneAnim_CallAnim(0.0f, 0x21, 0, 4, 0, 0, dcv);
            } else {
                DroneAnim_SetStandIdleAnim(dcv, 0);
            }
            return 1;
        case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x1e:
            dcv->sm->result = DroneVision_EnemyAlerts(dcv, msg);
            if (dcv->sm->result != 0) return set_state(dcv, dcv->sm->result);
            if (dcv->drone->unknown_33[0] != 0) return 1;
            dcv->drone->return_state = 4;
            return 1;
        case 0x1f:
            DroneFunc_DoForcedAttack(dcv);
            return 1;
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
            return 0;
    }
}

namespace {

// O tipo da entrada logo depois da última da tabela de coberturas (a peculiaridade de
// NDrone2_FindCover e NDrone2_FindAmbushCover; com a tabela cheia, 0).
uint8_t cover_type_after_table() {
    NPCGLOBALS_tag& n = NPCGlobals;
    if (n.cover_nodes == CoverNodes && n.cover_count >= static_cast<uint32_t>(kMaxCoverNodes)) {
        ++g_drone_combat_guards;
        return 0;
    }
    return n.cover_nodes[n.cover_count].type;
}

// Uma quina utilizável na emboscada (com as animações do personagem).
bool ambush_corner_ok(DCVars_tag* dcv, CoverNode_tag* c) {
    if (DroneAnim_ForceCrouchCover(dcv, c) == 0) {
        if (DroneAnim_CanCrouchLean(dcv, c) != 0) return true;
        if (DroneAnim_CanCrouchStepOut(dcv, c) != 0) return true;
        if (DroneAnim_CanStandLean(dcv, c) != 0) return true;
        if (DroneAnim_CanStandStepOut(dcv, c) != 0) return true;
    } else {
        if (DroneAnim_CanCrouchLean(dcv, c) != 0) return true;
        if (DroneAnim_CanCrouchStepOut(dcv, c) != 0) return true;
    }
    return DroneAnim_CanUseCornerAsLowCover(dcv, c) != 0;
}

// Posição na cobertura c (o NDrone2_GetPosForCornerCover embutido): lado 1 (rot - 90 graus, a
// corner_a décimos para o lado), lado 2 (a corner_b décimos para o outro lado), 3 baixa (0,3 m atrás).
void ambush_cover_pos(Drone_tag* d, CoverNode_tag* c, uint32_t side) {
    d->cover_pos.pos.x = c->pos.x;
    d->cover_pos.pos.y = c->pos.y;
    d->cover_pos.pos.z = c->pos.z;
    d->goal_point.rot.x = c->rot.x;
    d->goal_point.rot.y = c->rot.y;
    d->goal_point.rot.z = c->rot.z;
    const float s = PS2Sinf(c->rot.y);
    const float co = PS2Sinf(c->rot.y + kHalfPi);
    if (side == 1) {
        const float a = -float(c->corner_a) * 0.1f;
        d->goal_point.rot.y = d->goal_point.rot.y - kHalfPi;
        d->cover_pos.pos.x = d->cover_pos.pos.x + a * s + co * 0.1f;
        d->cover_pos.pos.z = d->cover_pos.pos.z + (a * co - s * 0.1f);
    } else if (side == 2) {
        const float b = float(c->corner_b) * 0.1f;
        d->cover_pos.pos.x = d->cover_pos.pos.x + s * -0.1f + b * co;
        d->cover_pos.pos.z = d->cover_pos.pos.z + (co * -0.1f - b * s);
    } else {
        d->cover_pos.pos.x = d->cover_pos.pos.x + s * -0.3f;
        d->cover_pos.pos.z = d->cover_pos.pos.z + co * -0.3f;
    }
}

}  // namespace

// [decomp] 0x00152958 NDrone2_FindAmbushCover
// A cobertura livre mais perto, a menos de 2 m, utilizável pelo personagem (quina com animação; baixa
// só com a propriedade 0xf). Toma-a (largando a anterior) e vai até ela: numa quina, do lado mais perto
// dele. Peculiaridade mantida: o tipo que decide quina ou baixa é o da entrada depois da última da
// tabela (como em NDrone2_FindCover), não o da escolhida; se esse tipo não é 0 nem 1, devolve 0 depois
// de já ter trocado de cobertura.
uint32_t NDrone2_FindAmbushCover(DCVars_tag* dcv) {
    NPCGLOBALS_tag& n = NPCGlobals;
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (n.cover_count == 0 || n.cover_nodes == nullptr) return 0;
    uint32_t best = 0xffffffff;
    float best_dist = 2.0f;
    for (uint32_t i = 0; i < n.cover_count; ++i) {
        CoverNode_tag* c = &n.cover_nodes[i];
        if ((c->flags & 0x10) != 0) continue;
        if (c->type == 0 && !ambush_corner_ok(dcv, c)) continue;
        if (c->type == 1 && !prop(dcv->drone, 0xf)) continue;
        const float dist = Vec_Dist3D(&obj->pos, &c->pos);
        if (best_dist <= dist) continue;
        best = i;
        best_dist = dist;
    }
    if (best == 0xffffffff) return 0;
    if (dcv->drone->cover != nullptr) {
        dcv->drone->cover->user = nullptr;
        dcv->drone->cover->flags &= 0xffffffef;
    }
    dcv->drone->flags &= 0xfffff7ff;
    dcv->drone->flags &= 0xffffefff;
    CoverNode_tag* c = &n.cover_nodes[best];
    d->cover = c;
    uint32_t side;
    const uint8_t end_type = cover_type_after_table();
    if (end_type == 0) {
        ambush_cover_pos(d, c, 1);
        const float d1 = Vec_Dist3D(&obj->pos, &d->cover_pos.pos);
        ambush_cover_pos(d, d->cover, 2);
        side = 2;
        if (d1 < Vec_Dist3D(&obj->pos, &d->cover_pos.pos)) {
            ambush_cover_pos(d, d->cover, 1);
            side = 1;
        }
    } else if (end_type == 1) {
        ambush_cover_pos(d, c, 3);
        side = 3;
    } else {
        return 0;
    }
    AINetwork_InvalidateRoute(&dcv->drone->route);
    d->cover_pos.cel = d->cover->cel;
    AINetwork_SetupGoalPosition(&dcv->drone->goal_target, &dcv->drone->goal_point, &d->cover_pos, 0.5f, 0);
    d->cover->user = dcv->obj;
    d->cover_kind = static_cast<uint16_t>((d->cover_kind & 0xfffc) | side);
    d->cover->flags |= 0x10;
    NDrone2_GetPosForCornerCover(d->cover, d->opponent_player, &d->cover_pos.pos, nullptr, 0);
    d->cover_pos.cel = d->cover->cel;
    dcv->drone->flags |= 0x800;
    dcv->drone->flags |= 0x1000;
    return 1;
}

// [decomp] 0x00172960 NDrone2_DSTATE_AmbushInit
// 0x37: com uma cobertura de emboscada, vai até ela (0x93); senão espera agachado (0x11, então 0x38).
uint32_t NDrone2_DSTATE_AmbushInit(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    if (msg->msg == 0) return 1;
    if (msg->msg != 1) return 0;
    if (NDrone2_FindAmbushCover(dcv) != 0) return set_state(dcv, 0x93);
    DroneAnim_CallAnim(0.0f, 0x11, 0, 0, 0, 0, dcv);
    set_state(dcv, 0x38);
    return 1;
}

// [decomp] 0x00161560 NDrone2_DSTATE_AmbushWait
// 0x38: de tocaia, sem ângulo de visão; ataca quando vê o oponente ou quando o jogador chega a 3 m. Ao
// sair, perde a propriedade 0x10.
uint32_t NDrone2_DSTATE_AmbushWait(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    uint32_t ret = 0;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            drone->view_angle = 0.0f;
            dcv->drone->flags |= 0x10000;
            return 1;
        case 2:
            behaviour_util_setProperty(0x10, dcv->drone->behaviour, 0);
            return 1;
        case 3:
            if (!sees(dcv->drone)) {
                if (NDrone2_Player() == nullptr) return 1;
                if (3.0f <= Vec_Dist2D(&dcv->obj->pos, &NDrone2_Player()->pos)) return 1;
            }
            return set_state(dcv, 0x56);
        case 0x21:
            return explosive_message(dcv, msg, 0x56);
        default:
            if (impact_messages(dcv, msg, 0x56, 0, &ret)) return ret;
            return 0;
    }
}

// [decomp] 0x0016db90 NDrone2_DSTATE_Tester1
// 0xb7 (teste): parado, alterna animações pelo temporizador (0x1a 2 s, 0x1b 1/6 s, 10 2 s).
uint32_t NDrone2_DSTATE_Tester1(DCVars_tag* dcv, Drone_tag* drone, obj_tag*, MsgObject* msg) {
    Drone_tag* d = dcv->drone;
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            DroneAnim_CallAnim(0.0f, 0x1a, 0, 0, 0, 0, dcv);
            set_timer1(d, GameState.frame + 0x78);
            return 1;
        case 3:
            drone->no_move = 1;
            return 1;
        case 0xc:
            switch (msg->data) {
                case 1:
                    DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
                    d->timer1_time = GameState.frame + 0x78;
                    d->timer1_data = 2;
                    break;
                case 0:
                    DroneAnim_CallAnim(0.0f, 0x1b, 0, 0, 0, 0, dcv);
                    d->timer1_time = GameState.frame + 10;
                    d->timer1_data = 1;
                    break;
                case 2:
                    DroneAnim_CallAnim(0.0f, 0x1a, 0, 0, 0, 0, dcv);
                    d->timer1_time = GameState.frame + 0x78;
                    d->timer1_data = 0;
                    break;
                default:
                    return 1;
            }
            dcv->drone->timer1_on = 1;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00174150 NDrone2_DSTATE_Tester2
// 0xb8 (teste): animação 0xd e volta a 0xb7.
uint32_t NDrone2_DSTATE_Tester2(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            DroneAnim_CallAnim(0.0f, 0xd, 0, 0, 0, 0, dcv);
            return 1;
        case 3:
            set_state(dcv, 0xb7);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001741c8 NDrone2_DSTATE_Tester3
// 0xb9 (teste): animação 0xd; 1 s depois, 10 e então 0xba.
uint32_t NDrone2_DSTATE_Tester3(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            DroneAnim_CallAnim(0.0f, 0xd, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        case 0xc:
            DroneAnim_CallAnim(0.0f, 10, 0, 0xba, 0, 0, dcv);
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x001742d8 NDrone2_DSTATE_Tester4
// 0xba (teste): animação 10; 1 s depois, 0xb7.
uint32_t NDrone2_DSTATE_Tester4(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* msg) {
    switch (msg->msg) {
        case 0:
            return 1;
        case 1:
            DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, dcv);
            set_timer1(dcv->drone, frame_plus(60.0f));
            return 1;
        case 0xc:
            set_state(dcv, 0xb7);
            return 1;
        default:
            return 0;
    }
}
