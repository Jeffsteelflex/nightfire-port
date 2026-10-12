// Armas dos personagens da IA (módulo DroneWeap do ACTION.ELF, 0x001789c0-0x0017a92c), mais as
// funções pequenas que o caminho do tiro usa: DroneAnim_PlayFiringAnim, NDrone2_CanMoveToRelativePos,
// NDrone2_SetAngleToObj, NDrone2_BondIsFacingMe, BOT_getAggressionMul e o dano das balas da IA no
// jogador (Drone_ModPlayerHitDamage/Drone_ModBulletDamage, chamadas por Player_DealWithObjHit; hoje
// não alcançadas porque o stub de MP_RegisterBulletHit devolve falso). Ver drone.h ("Armas da IA").
//
// O tiro. Um estado de combate pede o tiro com DroneWeap_Fire (Drone_tag::firing; com one_burst, uma
// rajada só). A cada tick NDrone2_ControlSTANDARD chama DroneWeap_HandleFiring, que mantém a mira
// (target_pos = oponente + aim_offset; DroneWeap_DoOpponentTargetting nota quando o jogador começa a
// andar ou para) e chama DroneWeap_DoFiring. Este atira (DroneWeap_FireWeapon) quando chega a hora
// (next_shot), o personagem viu o oponente há pouco (lost_ticks < 16, ou a propriedade 0x17) e
// DroneWeap_Ready2Fire concorda: oponente vivo e a animação de mirar (Drone_AnimInfo kind 4, ou 2 sem
// mistura). A rajada tem burst balas (DroneWeap_NextBulletTime: weapon_data +0x2c, o terceiro modo de
// disparo; 2 a 4 ao acaso nas armas 6 e 10 sem rajada, 1 ou 2 nas 0xe-0x10; mais de 3 encolhem pelo
// fator de agressividade de stats[1]), uma a cada fire_delay ticks (no mínimo 60 nas armas 6, 10 e
// 0xe-0x10), e entre rajadas uma pausa (DroneWeap_BurstDelay: 45 ticks vezes 1,66 a 0,33 por
// stats[1] e 0,5 a 1 ao acaso, entre 15 e 60; a pausa mínima a menos de 4 m e a máxima além de
// 30 m). Quando a rajada acaba ou não dá para atirar, burst_over; com one_burst, o pedido se desliga.
// As armas 0x1c e 0x1d (anim_fire) atiram pela animação de tiro (DroneAnim_PlayFiringAnim).
//
// Cada tiro (DroneWeap_FireWeapon): o erro de pontaria (DroneWeap_DoBulletAccuracy: acerta com chance
// 100 - 5 x precisão, vezes a dificuldade e o movimento do alvo, só depois de vê-lo por 2 s, e sempre a
// menos de 3 m; o erro é um desvio de até 1,5 m que oscila com o tempo, girado para o oponente), a
// boca da arma (o osso de AnimDatumGetWeaponInfo e o -x dele, DroneWeap_GetWeaponInfo), uma luz e o
// clarão (um detrito de 2 ticks com o modelo de weapon_data +0x4c), weapon_data +0x18 projéteis com
// Bullet_init (os mesmos do jogador: voam, ricocheteiam, deixam marcas e acertam o jogador ou outros
// personagens, que filtram o fogo amigo pelo lado em NDrone2_DealWithObjHit), a cápsula e o som
// (weapon_data +0x60, ainda stub). Granadas (DroneWeap_ThrowGrenade: a do parâmetro 11, Drone_tag::
// look, contadas em look_variant) saem da mão (osso 0x15) na direção do alvo, 45 graus para baixo
// perto (< 10 m) e para cima longe (> 18 m); golpes (DroneWeap_CloseCombatImpact) são projéteis das
// armas 99-0x66.
//
// Peculiaridades preservadas:
// - DroneWeap_ThrowGrenade inclina a direção em volta do eixo x do mundo (MulMatrix2 depois de
//   RotMatrix), não do eixo lateral do personagem: na direção x a granada sai reta.
// - DroneWeap_FireWeapon: o tom do som do tiro, Rand_Rand(500) - 250, é convertido para float como sem
//   sinal (os negativos viram ~4,29e9).
// - DroneWeap_DoBulletAccuracy aplica o fator de perto (too_close_accuracy) duas vezes.
// - DroneWeap_ThrowGrenade gasta uma granada da contagem mesmo quando o DTYPE 0xd troca a granada.
//
// Divergências intencionais:
// - DroneWeap_FireWeapon: o w da rotação do clarão vem de lixo da pilha no original (o
//   Vec_Cartesian_2_Spherical não o escreve); aqui é 0.
// - DroneWeap_ThrowGrenade e NDrone2_CanMoveToRelativePos: o w dos ângulos e do deslocamento (lixo da
//   pilha no original) é 0.
// - DroneWeap_ChangeWeapon com um personagem sem objeto de animação escreveria no endereço 0x62: aqui
//   não troca a arma (contado em g_drone_weapon_guards; nunca acontece nos dados reais).
// - DroneWeap_DropWeapon de um personagem sem pose: a translação da matriz da mão era lixo da pilha (a arma caía num
//   ponto qualquer, às vezes para sempre); aqui começa na posição do personagem.
// - BOT_getAggressionMul (módulo dos bots) foi portada porque o tiro no modo história também a usa
//   (DroneWeap_NextBulletTime encolhe as rajadas por ela); o stub devolvia sempre 1.

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/lights.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/fmath.h"

DroneFiringSettings DroneFiring;
uint32_t g_drone_weapon_guards = 0;

namespace {

constexpr float kHalfPi = 1.5707964f;

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
bool bots_on() { return mp_word(0x184) != 0; }

template <class T>
T rd(const void* base, uint32_t offset) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return v;
}

Drone_tag* drone_of(obj_tag* obj) { return static_cast<Drone_tag*>(obj->data); }

// A arma do bot (dados do bot +0x768).
int16_t bot_weapon(Drone_tag* d) { return int16_t(rd<uint8_t>(d->bot, 0x768)); }

// fptoui do libgcc (fp-bit.c): negativo, zero e NaN viram 0; acima de 2^32 - 1, o máximo.
uint32_t fptoui(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 4294967296.0f) return 0xffffffffu;
    return static_cast<uint32_t>(f);
}

_VECTOR sub4(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

// As armas com pausa mínima de 60 ticks entre balas e rajadas sorteadas (6, 10 e 0xe a 0x10).
bool slow_weapon(int8_t w) { return w == 6 || w == 10 || (0xe <= w && w <= 0x10); }

// A mira: a posição do oponente (ou a última conhecida) mais o deslocamento do osso visado.
void aim_at_opponent(Drone_tag* d) {
    const _VECTOR& p = d->opponent != nullptr ? d->opponent->pos : d->opp_pos;
    d->target_pos = add4(p, d->aim_offset);
}

// O teste "fora de ação" que o original repete em linha: personagem morto ou morrendo (0x600),
// parado (sem 0x100), sem vida, tipo 0x11 ou com remoção pedida.
bool out_of_action(obj_tag* obj) {
    if (obj == nullptr) return true;
    const Drone_tag* d = drone_of(obj);
    if ((d->flags & 0x600) != 0 || (d->flags & 0x100) == 0 || d->health <= 0.0f) return true;
    if (obj->type == 0x11) return true;
    return (obj->life & 1) != 0;
}

}  // namespace

// ---- o tiro por tick ----

// [decomp] 0x0017a180 DroneWeap_HandleFiring
// Por tick (NDrone2_ControlSTANDARD): sem pedido de tiro, só a mira; com ele, atira
// (DroneWeap_DoFiring). Com one_burst, a rajada acabada desliga o pedido; sem, recomeça.
void DroneWeap_HandleFiring(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    d->fire_ready = 0;
    if (d->firing == 0) {
        if (d->opponent == nullptr || bots_on()) return;
        aim_at_opponent(dcv->drone);
        if (!bots_on()) DroneWeap_DoOpponentTargetting(dcv);
        return;
    }
    if (d->one_burst == 0) {
        d->burst_over = 0;
    } else if (d->burst_over != 0) {
        d->firing = 0;
        if (d->opponent == nullptr || bots_on()) return;
        aim_at_opponent(dcv->drone);
        return;
    }
    if (!bots_on()) DroneWeap_DoOpponentTargetting(dcv);
    if (DroneWeap_DoFiring(dcv) == 0) d->burst_over = 1;
}

// [decomp] 0x0017a080 DroneWeap_DoFiring
// Uma rajada começa quando burst chega a 0. Sem ver o oponente há 16 ticks (e sem a propriedade
// 0x17), 0: a rajada acaba. Na hora do tiro, se DroneWeap_Ready2Fire, marca fire_ready e (fora das
// armas que atiram pela animação) atira; 0 quando foi a última bala da rajada.
uint32_t DroneWeap_DoFiring(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) BOT_opponentTargetting(dcv);
    if (d->burst == 0) DroneWeap_NextBulletTime(dcv, 1, 0);
    if (0xf < dcv->drone->lost_ticks && behaviour_util_getProperty(0x17, d->behaviour) == 0) return 0;
    if (GameState.frame < d->next_shot) return 1;
    if (DroneWeap_Ready2Fire(dcv) == 0) return 1;
    d->fire_ready = 1;
    if (d->anim_fire != 0) return 1;
    DroneWeap_FireWeapon(dcv);
    Drone_tag* e = dcv->drone;
    e->burst = int16_t(e->burst - 1);
    if (dcv->drone->burst < 1) return 0;
    DroneWeap_NextBulletTime(dcv, 0, 0);
    return 1;
}

// [decomp] 0x0017a8c0 DroneWeap_Fire
// Pede o tiro (firing, sem one_burst). Com restart, e se ainda não atirava (ou com bots), a primeira
// bala espera a pausa de começo (DroneWeap_NextBulletTime com start).
void DroneWeap_Fire(int8_t restart, DCVars_tag* dcv) {
    if (!bots_on() && dcv->drone->firing != 0) restart = 0;
    if (restart != 0) DroneWeap_NextBulletTime(dcv, 0, 1);
    dcv->drone->firing = 1;
    dcv->drone->burst_over = 0;
    dcv->drone->one_burst = 0;
}

// [decomp] 0x0017a870 DroneWeap_ManageBurstCount
// Uma bala a menos na rajada; se ainda há, marca a hora da próxima. 0 quando a rajada acabou.
uint32_t DroneWeap_ManageBurstCount(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    d->burst = int16_t(d->burst - 1);
    if (dcv->drone->burst < 1) return 0;
    DroneWeap_NextBulletTime(dcv, 0, 0);
    return 1;
}

// [decomp] 0x00179d40 DroneWeap_NextBulletTime
// next_shot = o maior entre agora (mais um sorteio, com bots) e last_shot + fire_delay da arma na
// mão (os 16 bits baixos, com sinal; no mínimo 60 nas armas lentas; vezes FRAME_RATE_DIV,
// arredondado). Numa rajada nova (new_burst) ou no começo (start), sorteia o tamanho da rajada e
// soma a pausa entre rajadas (DroneWeap_BurstDelay) a last_shot.
uint32_t DroneWeap_NextBulletTime(DCVars_tag* dcv, int8_t new_burst, int8_t start) {
    const uint32_t now = GameState.frame;
    Drone_tag* d = dcv->drone;
    const int8_t w = anim_of(dcv->obj)->weapon;
    int32_t delay = rd<int16_t>(weapon_def(w), 0x40);
    const int32_t floor = slow_weapon(w) ? 0x3c : 0;
    if (delay < floor) delay = floor;
    const int16_t step = int16_t(fptoui(float(delay) * FRAME_RATE_DIV + 0.5f));
    int32_t extra = 0;
    uint32_t pause = 0;
    uint32_t base;
    if (new_burst == 0 && start == 0) {
        base = d->last_shot;
    } else {
        const int8_t w2 = anim_of(dcv->obj)->weapon;
        const uint16_t n = rd<uint16_t>(weapon_def(w2), 0x2c);
        int16_t count;
        if (bots_on()) {
            count = int16_t(n);
        } else if (w2 == 6 || w2 == 10) {
            count = n < 2 ? int16_t(uint16_t(Rand_Rand(3) + 2)) : int16_t(n);
        } else if (0xe <= w2 && w2 <= 0x10) {
            count = int16_t(uint16_t(Rand_Rand(2) + 1));
        } else {
            count = int16_t(n);
        }
        d->burst = count;
        if (count < 1) d->burst = 1;
        if (bots_on()) {
            const float mul = BOT_getAggressionMul(d);
            const uint32_t r = Rand_Rand(0xf);
            const float per = 60.0f / mul;
            float shots = float(d->burst) * mul;
            extra = int32_t(r) + int32_t(per);  // cvt.w.s: trunca
            if (shots <= 1.0f) shots = 1.0f;
            d->burst = int16_t(int32_t(shots));
            NDrone2_SetOpponentAimPos(d);
        } else {
            const float mul = BOT_getAggressionMul(d);
            pause = DroneWeap_BurstDelay(d);
            const int8_t w3 = anim_of(dcv->obj)->weapon;
            if (!slow_weapon(w3) && 3 < d->burst) {
                float shots = float(d->burst) * mul;
                if (shots <= 3.0f) shots = 3.0f;
                d->burst = int16_t(int32_t(shots));
            }
        }
        d->last_shot = d->last_shot + pause;
        base = d->last_shot;
    }
    const uint32_t soonest = now + uint32_t(extra);
    const uint32_t next = base + uint32_t(int32_t(step));
    d->next_shot = next < soonest ? soonest : next;
    return 0;
}

// [decomp] 0x00179ba8 DroneWeap_BurstDelay
// Ticks entre rajadas: DroneFiring.burst_delay_normal vezes o fator de stats[1] (0: 1,66, 1: 1,33,
// 3: 0,66, 4: 0,33) e 0,5 a 1 ao acaso, truncado, entre o mínimo e o máximo; a menos de min_dist o
// mínimo, além de max_dist o máximo. Vezes FRAME_RATE_DIV.
uint32_t DroneWeap_BurstDelay(Drone_tag* drone) {
    float delay = DroneFiring.burst_delay_normal;
    switch (drone->stats[1]) {
        case 0: delay = delay * 1.66f; break;
        case 1: delay = delay * 1.33f; break;
        case 3: delay = delay * 0.66f; break;
        case 4: delay = delay * 0.33f; break;
        default: break;
    }
    const float r = Rand_FRand(0.5f) + 0.5f;
    const float lo = DroneFiring.burst_delay_min;
    float t = float(fptoui(delay * r));
    t = lo < t ? t : lo;  // max.s
    t = float(fptoui(t));
    const float hi = DroneFiring.burst_delay_max;
    const float dist = drone->opp_dist;
    float out;
    if (dist < DroneFiring.burst_delay_min_dist) {
        out = lo;
    } else if (DroneFiring.burst_delay_max_dist < dist) {
        out = hi;
    } else {
        out = t < hi ? t : hi;  // min.s
    }
    return fptoui(out * FRAME_RATE_DIV);
}

// [decomp] 0x00178c48 DroneWeap_Ready2Fire
// Pode atirar agora: o oponente está vivo (e, sendo o jogador, a missão ainda conta), e a animação
// é de mirar (Drone_AnimInfo kind 4; ou 2 sem script saindo); com a mira da animação (anim_flags
// 0x1000), o cano tem de apontar para target_pos (20 graus, 30 no DTYPE 0xd) ou o personagem morreu.
uint32_t DroneWeap_Ready2Fire(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* opp = d->opponent;
    obj_tag* me = dcv->obj;
    if (opp == nullptr) return 0;
    if (opp->type == 2) {
        if (drone_of(opp)->health <= 0.0f) return 0;
    } else if (opp->type == 3 || opp->type == 0x12) {
        if (bloke_of(opp)->health <= 0.0f) return 0;
        if (!bots_on() && !PlrStat_OkToUpdate()) return 0;
    }
    if (d->anim_kind == 4) return 1;
    if (!bots_on()) {
        if (d->anim_kind != 2) return 0;
        if (d->blend_script != nullptr) return 0;
        if (d->opp_dist < 2.0f) return 1;
    }
    if ((anim_of(me)->anim_flags & 0x1000) == 0) return 1;
    if (dcv->drone->type == 0x1e && BOTWEAP_AmmoInGun(d, bot_weapon(d)) < 1) return 0;
    int32_t bone = 0;
    uint32_t flash = 0;
    uint16_t kind = 0;
    _VECTOR pos, dir;
    if (DroneWeap_GetWeaponInfo(d, &bone, &flash, &kind, &pos, &dir) == 0) return 0;
    Vec_Normalise(&dir, &dir);
    _VECTOR to = sub4(d->target_pos, pos);
    Vec_Normalise(&to, &to);
    const float dot = to.x * dir.x + to.y * dir.y + to.z * dir.z;
    if (0.9998f < dot) return 1;
    const float limit = d->type == 0xd ? 0.5235988f : 0.34906587f;
    if (std::fabs(nf::fm::acos(dot)) < limit) return 1;
    if (d->health <= 0.0f) return 1;
    return 0;
}

// [decomp] 0x00178f68 DroneWeap_FireWeapon
// Um disparo: erro de pontaria, luz e clarão na boca da arma, as balas da arma na mão em direção a
// target_pos + aim_error (ou para onde o cano aponta, se o personagem saiu de ação), a cápsula, uma
// bala a menos no pente e o som. Os bots (DTYPE 0x1e) conferem munição, distância e estado; a
// metralhadora fixa ('R') é montada no lugar (GT_DeployMiniGun).
void DroneWeap_FireWeapon(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (d->type == 0x1e) {
        if (BOTWEAP_AmmoInGun(d, bot_weapon(d)) < 1) return;
        if (d->opponent != nullptr && BOTWEAP_tooCloseForWeapon(d, bot_weapon(d), d->opp_dist) != 0) return;
        if ((dcv->drone->sight_flags & 4) == 0) return;
        if (dcv->sm->state - 0xddu < 2) return;
        if (BOTSTATE_getStateType(dcv->sm->state) != 4) return;
        if (bot_weapon(d) == 0x52) {
            const float yaw = obj->rot.y;
            _VECTOR at{};
            at.x = obj->pos.x + PS2Sinf(yaw);
            at.y = obj->pos.y;
            at.z = obj->pos.z + PS2Sinf(yaw + kHalfPi);
            if (GT_DeployMiniGun(&at, &d->obj->matrix, d->obj) == 0) return;
            BOTWEAP_decrRounds(d, 1000);
            BOTSTATE_changeWeapon(static_cast<BOT_vars_t*>(d->bot), 0, 1, 0);
            return;
        }
    } else {
        DroneWeap_DoBulletAccuracy(dcv);
    }
    int32_t bone = 0;
    uint32_t flash = 0;
    uint16_t sound = 0;
    _VECTOR pos, dir;
    if (DroneWeap_GetWeaponInfo(d, &bone, &flash, &sound, &pos, &dir) == 0) return;
    _VECTOR rot;
    Vec_Cartesian_2_Spherical(&rot, dir.x, dir.y, dir.z);
    rot.z = 0.0f;
    rot.w = 0.0f;  // port: no original, lixo da pilha
    rot.x = -rot.x;
    const weapon_definition_tag* def = weapon_def(anim_of(obj)->weapon);
    Light_Create(&pos, def->light_r, def->light_g, def->light_b, 7.0f, 0, 0, 1, 1.0f, 0, 0, 0, 0xffff);
    // O clarão: um detrito (tipo 0x10) de 2 ticks com o modelo da arma, girado ao acaso em volta do cano.
    obj_tag* fx = control_create_object(0x40, &pos, nullptr, nullptr);
    if (fx != nullptr) {
        fx->rot = rot;
        fx->rot.x = fx->rot.x + Rand_FRand(6.2831855f);
        fx->rot.y = fx->rot.y + (Rand_FRandHalf(0.2f) + kHalfPi);
        const float roll = Rand_FRandHalf(0.2f);
        uint8_t* data = static_cast<uint8_t*>(fx->data);
        fx->type = 0x10;
        fx->rot.z = fx->rot.z + roll;
        const int16_t life = 2;
        std::memcpy(data + 0x30, &life, 2);
        data[0x32] = 0;
        if (flash != 0) hashtable_set_object_to_entity_gfx(fx, flash);
        fx->unknown_103[3] = 0xff;
        control_link_object_to_cel(fx, dcv->obj->cel);
    }
    Vec_Normalise(&dir, &dir);
    weapon_definition_tag* w = weapon_def(anim_of(obj)->weapon);
    if (0.0f < d->health && !out_of_action(obj) && d->opponent != nullptr) {
        const _VECTOR target = add4(d->target_pos, d->aim_error);
        dir = sub4(target, pos);
        Vec_Normalise(&dir, &dir);
    }
    for (uint32_t i = 0; i < w->bullets; ++i) Bullet_init(-1, obj, obj, w, &pos, &dir, 0xffff);
    d->fired = 1;
    d->last_shot = GameState.frame;
    d->lost_sight = 0;
    if (!bots_on()) Bullet_init_casing(obj);
    d->clip = uint16_t(d->clip - 1);
    if (dcv->drone->type == 0x1e) BOTWEAP_decrRounds(d, 1);
    if (sound != 0x5ef) {
        const uint32_t pitch = Rand_Rand(500) - 0xfau;  // convertido como sem sinal, como no original
        Sound_Play3D(sound, &obj->pos, 25.0f, 100.0f, float(pitch), 0, 0, 0);
    }
    if (d->anim_fire != 0 && DroneWeap_ManageBurstCount(dcv) == 0) d->burst_over = 1;
}

// [decomp] 0x00179740 DroneWeap_DoBulletAccuracy
// Decide se o próximo tiro acerta: chance (em 100) de 100 - 5 x stats[0], vezes os fatores de perto
// (duas vezes), do alvo que começou a andar ou anda, do que acabou de parar (só a subclasse 0x13 em
// 0700000c/0d) e da dificuldade. Longe (>= too_close_distance), só acerta depois de ver o alvo por
// new_sighting_time_to_hit e não logo depois de perdê-lo; a menos de 3 m acerta sempre. Errando,
// aim_error é um desvio (0,5, 1,5, 1,5) que oscila com o tempo, girado para o lado do oponente.
void DroneWeap_DoBulletAccuracy(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->opponent == nullptr) return;
    const DroneFiringSettings& f = DroneFiring;
    float chance = 100.0f - float(int32_t(d->stats[0])) * 5.0f;
    if (d->opp_dist < f.too_close_distance) chance = chance * f.too_close_accuracy;
    if (d->target_started != 0) chance = chance * f.target_first_moved_accuracy;
    if (d->target_stopped != 0 && GameState.level_file - 0x700000cu < 2 && d->subclass == 0x13) {
        chance = chance * f.target_first_stopped_accuracy;
    }
    if (d->target_moving != 0) chance = chance * f.target_moving_accuracy;
    const uint32_t difficulty = GameState.difficulty;
    if (difficulty == 1) {
        chance = chance * f.accuracy_easy;
    } else if (difficulty == 3 || difficulty == 4) {
        chance = chance * f.accuracy_hard;
    } else {
        chance = chance * f.accuracy_normal;
    }
    if (d->opp_dist < f.too_close_distance) chance = chance * f.too_close_accuracy;
    bool hit = Rand_FRand(100.0f) < chance;
    const float dist = d->opp_dist;
    if (f.too_close_distance <= dist) {
        const uint32_t need = fptoui(f.new_sighting_time_to_hit * FRAME_RATE);
        if (d->seen_ticks < need) hit = false;
        if (d->lost_sight != 0) hit = false;
    }
    if (dist < 3.0f) hit = true;
    if (hit) {
        d->aim_error = {0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }
    const float t = float(GameState.frame + dcv->obj->created);
    const float a = t * 0.01f;
    _VECTOR off{};
    off.x = PS2Sinf(a) * 0.5f;
    off.y = PS2Sinf(a + kHalfPi) * 1.5f;
    off.z = PS2Sinf(t * 0.02f) * 1.5f;
    // Os ângulos até o oponente (Drone_GetOpponentInfo: 0, opp_yaw, 0).
    const _VECTOR angles{d->unknown_1c0, d->opp_yaw, d->unknown_1c8, d->unknown_1cc};
    MATRIX m;
    RotMatrix(&angles, &m);
    ApplyMatrixLV(&m, &off, &d->aim_error);
}

// [decomp] 0x00179a48 DroneWeap_DoOpponentTargetting
// Acompanha o jogador: anda (velocidade no plano não nula)? Na troca (começou a andar ou parou),
// marca target_started/target_stopped e a hora; elas valem por target_first_*_time_to_hit.
void DroneWeap_DoOpponentTargetting(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* opp = d->opponent;
    if (opp == nullptr) return;
    if (opp->type == 3) {
        _VECTOR v = bloke_of(opp)->vel;
        v.y = 0.0f;
        d->target_moving = Vec_Magnitude(&v) != 0.0f ? 1 : 0;
    }
    const DroneFiringSettings& f = DroneFiring;
    if (d->target_moving != 0) {
        d->target_stopped = 0;
        d->target_stopped_seen = 0;
        if (d->target_started == 0 && d->target_started_seen == 0) {
            d->target_started_seen = 1;
            d->target_change_time = GameState.frame;
            d->target_started = 1;
        }
        const uint32_t t = fptoui(f.target_first_moved_time_to_hit * FRAME_RATE);
        if (d->target_change_time + t < GameState.frame) d->target_started = 0;
    } else {
        d->target_started = 0;
        d->target_started_seen = 0;
        if (d->target_stopped == 0 && d->target_stopped_seen == 0) {
            d->target_stopped_seen = 1;
            d->target_change_time = GameState.frame;
            d->target_stopped = 1;
        }
        const uint32_t t = fptoui(f.target_first_stopped_time_to_hit * FRAME_RATE);
        if (d->target_change_time + t < GameState.frame) d->target_stopped = 0;
    }
}

// [decomp] 0x0013da48 DroneAnim_PlayFiringAnim
// Armas que atiram pela animação (anim_fire): sem chamada de animação pendente nem script saindo,
// pronto (fire_ready) e com o evento de mira da animação (0x21), toca a DAnim 0x5c e atira.
uint32_t DroneAnim_PlayFiringAnim(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->anim_fire == 0) return 0;
    if (d->call_state != 0) return 0;
    if (d->blend_script != nullptr) return 0;
    if (d->fire_ready == 0) return 0;
    if (d->unknown_21 == 0) return 0;
    d->end_state = 0;
    d->end_msg = 0;
    if (DroneAnim_SetDAnimInternal(0x5c, 0, 0, d) != 0) d->anim_call_done = 1;
    DroneWeap_FireWeapon(dcv);
    d->fired = 0;
    return 1;
}

// ---- mirar e o oponente ----

// [decomp] 0x00178ae0 DroneWeap_AimTarget_IsOpponent
// Vira para o oponente. Com check_angle: já de frente (menos de 0,9 rad), 1; fora disso e sem
// keep_turning, 0. Nos estados de animação 0x1e/0x1f gira só metade e não anda se der para ficar 0,4 m
// à frente; nos outros, move_angles aponta para o oponente (NDrone2_SetAngleToObj).
uint32_t DroneWeap_AimTarget_IsOpponent(int8_t check_angle, int8_t keep_turning, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->opponent == nullptr || (d->control_flags & 0x8000000) != 0) return 0;
    obj_tag* me = dcv->obj;
    const _VECTOR delta = sub4(d->opponent->pos, me->pos);
    const float yaw = ATAN2_APPROX(delta.x, delta.z);
    const float diff = Vec_AngleDifference(yaw, me->rot.y);
    if (check_angle != 0) {
        if (std::fabs(diff) < 0.9f) return 1;
        if (keep_turning == 0 && 0.9f < std::fabs(diff)) return 0;
    }
    if (uint32_t(d->anim_state) - 0x1eu < 2) {
        const float half = Vec_AngleDifference(yaw, me->rot.y) * 0.5f;
        d->move_angles.y = me->rot.y - half;
        if (NDrone2_CanMoveToRelativePos(d, me, 0.0f, 0.0f, 0.4f) != 0) d->no_move = 1;
    } else {
        NDrone2_SetAngleToObj(dcv, d->opponent, 0.0f, 0);
    }
    return 1;
}

// [decomp] 0x0017a500 DroneWeap_OpponentIsAimingAtMe
// O oponente (jogador ou personagem) está armado e mira aqui: in_sights ligado ou, com bots, ele
// olha para cá (dentro de "degrees").
uint32_t DroneWeap_OpponentIsAimingAtMe(DCVars_tag* dcv, float degrees) {
    obj_tag* opp = dcv->drone->opponent;
    int8_t armed = 0;
    if (opp != nullptr && uint8_t(opp->type - 2) < 2) armed = int8_t(NDrone2_ObjectIsArmed(opp));
    if (armed == 0) return 0;
    if (dcv->drone->in_sights != 0) return 1;
    if (bots_on() && NDrone2_BondIsFacingMe(dcv, degrees) != 0) return 1;
    return 0;
}

// [decomp] 0x0017a4b8 DroneWeap_OpponentHasWeapon
// O oponente é o jogador ou um personagem com uma arma de fogo na mão.
uint32_t DroneWeap_OpponentHasWeapon(DCVars_tag* dcv) {
    obj_tag* opp = dcv->drone->opponent;
    if (opp == nullptr || 1 < uint8_t(opp->type - 2)) return 0;
    return NDrone2_ObjectIsArmed(opp);
}

// [decomp] 0x00154f60 NDrone2_BondIsFacingMe
// O oponente olha para cá com menos de "degrees" graus de desvio (opp_facing).
uint32_t NDrone2_BondIsFacingMe(DCVars_tag* dcv, float degrees) {
    return std::fabs(dcv->drone->opp_facing) < degrees * 0.017453294f ? 1 : 0;
}

// [decomp] 0x00154d58 NDrone2_SetAngleToObj
// move_angles.y aponta para o objeto (mais offset), exceto sem force quando o giro está travado
// (control_flags 0x8000000, o bot congelado ou as flags 0x380 da DAnim).
void NDrone2_SetAngleToObj(DCVars_tag* dcv, obj_tag* obj, float offset, int8_t force) {
    if (obj == nullptr) return;
    Drone_tag* d = dcv->drone;
    if ((d->control_flags & 0x8000000) != 0 && force == 0) return;
    if (bots_on() && (rd<uint32_t>(d->bot, 0x734) & 2) != 0 && force == 0) return;
    if ((d->anim_flags & 0x380) != 0 && force == 0) return;
    const _VECTOR delta = sub4(obj->pos, dcv->obj->pos);
    dcv->drone->move_angles.y = offset + ATAN2_APPROX(delta.x, delta.z);
}

// [decomp] 0x00154838 NDrone2_CanMoveToRelativePos
// Dá para andar em linha reta dos pés do objeto até o ponto (x, y, z) no espaço dele
// (NDrone2_MoveTest)? Nunca para personagens com a flag 0x10.
uint32_t NDrone2_CanMoveToRelativePos(Drone_tag* drone, obj_tag* obj, float x, float y, float z) {
    if (drone != nullptr && (drone->flags & 0x10) != 0) return 0;
    _VECTOR rel{x, y, z, 0.0f};  // port: w é lixo da pilha no original
    CelPos_tag to{};
    NDrone2_FeetPos(obj, &to.pos);
    ApplyMatrixLVI(&obj->matrix, &rel);
    to.pos = add4(rel, to.pos);
    to.cel = build_FindCel(&to.pos, glb_world);
    if (drone != nullptr && (drone->flags & 0x10) != 0) return 0;
    CelPos_tag from{};
    NDrone2_FeetPos(obj, &from.pos);
    from.cel = obj->cel;
    if (to.cel == nullptr) to.cel = NDrone2_FindCel(&to.pos);
    DroneReachCheckPos = to.pos;
    return NDrone2_MoveTest(&from, &to, nullptr, obj, nullptr, 0, 1) == 1 ? 1 : 0;
}

// ---- granadas, golpes e troca de arma ----

// [decomp] 0x00179558 DroneWeap_ThrowGrenade
// Joga a granada (Drone_tag::look; o DTYPE 0xd joga 0x35 ou, uma vez em três, 0x36) da mão (osso
// 0x15) na direção de target_pos: a menos de 10 m 45 graus para baixo, além de 18 m 45 para cima
// (em volta do eixo x do mundo, como no original). Gasta uma de look_variant (mesmo sem granada).
void DroneWeap_ThrowGrenade(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    const uint16_t left = d->look_variant;
    uint32_t kind = d->look;
    if (left != 0) d->look_variant = uint16_t(left - 1);
    if (dcv->drone->type == 0xd) {
        kind = 0x35;
        if (Rand_Rand(3) == 0) kind = 0x36;
    }
    if (kind == 0) return;
    _VECTOR pos;
    MATRIX m;
    AnimGetBoneWorldTrans(dcv->obj, 0x80000015, 0, &pos, &m);
    Mat_Copy(&obj->matrix, &m);
    _VECTOR tilt{0.0f, 0.0f, 0.0f, 0.0f};
    const _VECTOR delta = sub4(d->target_pos, obj->pos);
    const float dist = Vec_Magnitude(&delta);
    const _VECTOR heading{0.0f, nf::fm::atan2(delta.x, delta.z), 0.0f, 0.0f};  // port: w é lixo da pilha no original
    RotMatrix(&heading, &m);
    MATRIX r;
    if (dist < 10.0f) {
        tilt.x = 0.7853982f;
        Mat_Rotate(&tilt, &r);
        MulMatrix2(&r, &m);
    } else if (18.0f < dist) {
        tilt.x = -0.7853982f;
        Mat_Rotate(&tilt, &r);
        MulMatrix2(&r, &m);
    }
    _VECTOR dir;
    Mat_GetDir(&dir, &m);
    Bullet_init(-1, dcv->obj, dcv->obj, weapon_def(int32_t(kind)), &pos, &dir, 0xffff);
}

// [decomp] 0x0017a848 DroneWeap_PlaceTertiary
void DroneWeap_PlaceTertiary(DCVars_tag* dcv) { DroneWeap_ThrowGrenade(dcv); }

// [decomp] 0x0017a798 DroneWeap_CanThrowGrenade
// Tem granada e ainda sobra, sorte (uma vez em duas), oponente entre 8 e 20 m e a animação de
// arremesso (estado 0x49) possível.
uint32_t DroneWeap_CanThrowGrenade(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->look == 0 || d->look_variant == 0) return 0;
    if (Rand_Rand(2) != 0) return 0;
    if (d->opponent == nullptr) return 0;
    if (d->opp_dist < 8.0f || 20.0f < d->opp_dist) return 0;
    return DroneAnim_CanDoAnimState(dcv, 0x49, 0) ? 1 : 0;
}

// [decomp] 0x001789c0 DroneWeap_CloseCombatImpact
// Golpe (eventos 5, 10 e 0xb da animação): um projétil da arma 100 (0x65 com kind 1, 0x66 com kind 2)
// do personagem até o oponente, se ele está a até 1,75 m e dentro de 60 graus da frente; a
// subclasse 7 golpeia a até 2 m em qualquer direção com a arma 99. A direção é opp_delta (sem
// normalizar).
void DroneWeap_CloseCombatImpact(DCVars_tag* dcv, int32_t kind) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    int32_t weapon = 100;
    if (d->subclass == 7) {
        if (2.0f < d->opp_dist) return;
        weapon = 99;
    } else {
        if (1.75f < d->opp_dist) return;
        if (1.0471976f < std::fabs(d->opp_bearing)) return;
        if (kind == 1) {
            weapon = 0x65;
        } else if (kind == 2) {
            weapon = 0x66;
        }
    }
    _VECTOR pos = obj->pos;
    const _VECTOR dir = d->opp_delta;
    Bullet_init(-1, dcv->obj, dcv->obj, weapon_def(weapon), &pos, &dir, 0xffff);
}

// [decomp] 0x0017a6e0 DroneWeap_ChangeWeapon
// Troca a arma na mão (eventos da animação, estados de choque e rendição) e a subclasse de animação
// que combina, com o pente cheio: 1 e 0x36 desarmado (subclasse 0xc), 6 (4, 16 balas), 0x12 (6, 32),
// 0x18/0x19 (5, 32). Outras não fazem nada.
void DroneWeap_ChangeWeapon(DCVars_tag* dcv, int32_t weapon) {
    Drone_tag* d = dcv->drone;
    uint16_t subclass;
    uint16_t clip;
    switch (weapon) {
        case 1:
        case 0x36: subclass = 0xc; clip = 0; break;
        case 6: subclass = 4; clip = 0x10; break;
        case 0x12: subclass = 6; clip = 0x20; break;
        case 0x18:
        case 0x19: subclass = 5; clip = 0x20; break;
        default: return;
    }
    d->subclass = subclass;
    d->clip = clip;
    d->clip_size = clip;
    ANIMOBJ_tag* anim = anim_of(dcv->obj);
    if (anim == nullptr) {
        ++g_drone_weapon_guards;  // port: o original escreveria no endereço 0x62
        return;
    }
    anim->weapon = int8_t(weapon);
}

// [decomp] 0x0017a2a0 DroneWeap_DropWeapon
// Solta a arma na mão (ao morrer, render-se...): se ela tem modelo de pegável (weapon_data +0x80),
// cria a arma no chão no osso da mão (ou na posição do personagem se uma parede está no meio) com
// metade a todo o pente de munição (um quarto a três quartos nos pentes de 10 ou mais); a arma 0x1a
// solta também a munição 0x1b. A mão fica vazia (arma 0).
void DroneWeap_DropWeapon(DCVars_tag* dcv) {
    obj_tag* obj = dcv->obj;
    Drone_tag* d = dcv->drone;
    if (obj == nullptr || obj->sphere_src == nullptr) return;
    const int8_t w = anim_of(obj)->weapon;
    if (w < 1) return;
    const weapon_definition_tag* def = weapon_def(w);
    if (rd<uint32_t>(def, 0x80) != 0) {
        const int32_t bone = AnimDatumGetWeaponInfo(obj, nullptr, nullptr, nullptr);
        _VECTOR hand;
        // Sem pose (anim_flags bit 0), AnimGetBoneWorldTrans só põe a identidade no 3x3 e a translação de m fica o que
        // havia na pilha: o original solta a arma ali, ou na posição do personagem se o raio até lá bater em algo.
        // Divergência: m começa com a posição do personagem (a arma cai aos pés dele, sem depender da pilha).
        MATRIX m{};
        std::memcpy(m.m[3], &d->obj->pos, sizeof(m.m[3]));
        AnimGetBoneWorldTrans(dcv->obj, uint32_t(-1 < bone ? bone : 0), 0, &hand, &m);
        _VECTOR* at = reinterpret_cast<_VECTOR*>(m.m[3]);
        P32<HITDATA_tag> hits = nullptr;
        if (Collide_RayIntersect(&d->obj->pos, at, d->obj->cel, d->obj, nullptr, &hits, 0, 0x125, 0)) {
            Collide_FreeHitList(&hits);
            *at = d->obj->pos;
        }
        const float share = def->clip < 10 ? Rand_FRand(0.5f) + 0.5f : Rand_FRand(0.5f) + 0.25f;
        Pickup_CreateSimple(&m, uint16_t(def->klass), uint16_t(int32_t(float(def->clip) * share)), 0);
        if (uint16_t(def->klass) == 0x1a) {
            const float r = Rand_FRand(0.5f);
            const int16_t clip = weapon_def(0x1b)->clip;
            obj_tag* ammo = Pickup_CreateSimple(&m, 0x1b, uint16_t(int32_t(float(clip) * (r + 0.25f))), 1);
            if (ammo != nullptr) ammo->flags |= 0x10;
        }
    }
    anim_of(dcv->obj)->weapon = 0;
}

// ---- dano no jogador ----

// [decomp] 0x00139f50 Drone_ModBulletDamage
// O dano de uma bala do personagem no jogador: nenhum de um desarmado (classe 0xc) ou sem dano; os
// golpes (armas 0x63-0x66) como estão; senão vezes too_close_damage de perto, vezes damage_mul e, na
// subclasse 0x12 (fora dos DTYPE 2/0x19 e de 0700001b), vezes player_back_shot_damage com o tiro
// vindo pelas costas do jogador (a direção do tiro a menos de 90 graus da frente dele).
float Drone_ModBulletDamage(Drone_tag* drone, obj_tag* player, HITDATA_tag* hit) {
    float damage;
    std::memcpy(&damage, &hit->unknown_08, 4);
    if (damage == 0.0f) return 0.0f;
    if (drone->klass == 0xc) return 0.0f;
    const uint16_t id = uint16_t(bullet_of(hit->obj)->weapon->id);
    if (0x63 <= id && id < 0x67) return damage;
    if (drone->opp_dist < DroneFiring.too_close_distance) damage = damage * DroneFiring.too_close_damage;
    damage = damage * drone->damage_mul;
    bool back = false;
    if (drone->type != 2 && drone->type != 0x19 && drone->subclass == 0x12) {
        back = GameState.level_file != 0x700001b;
    }
    if (back) {
        const float a = ATAN2_APPROX(hit->dir.x, hit->dir.z);
        if (std::fabs(Vec_AngleDifference(a, player->rot.y)) < kHalfPi) {
            damage = damage * DroneFiring.player_back_shot_damage;
        }
    }
    return damage;
}

// [decomp] 0x0013a940 Drone_ModPlayerHitDamage
// Player_DealWithObjHit: um acerto no jogador de uma bala de um personagem (vivos os dois objetos) tem
// o dano trocado pelo de Drone_ModBulletDamage. Fora do multijogador; sempre devolve 1.
bool Drone_ModPlayerHitDamage(obj_tag* obj, HITDATA_tag* hit) {
    if (bots_on() || obj == nullptr || hit == nullptr) return true;
    obj_tag* b = hit->obj;
    if (b == nullptr || (b->life & 1) != 0 || b->type != 5) return true;
    obj_tag* shooter = bullet_of(b)->from;
    if (shooter == nullptr || shooter->type != 2) return true;
    Drone_tag* d = drone_of(shooter);
    if (d == nullptr || (shooter->life & 1) != 0) return true;
    const float damage = Drone_ModBulletDamage(d, obj, hit);
    std::memcpy(&hit->unknown_08, &damage, 4);
    return true;
}

// ---- bots ----

// [decomp] 0x00126468 BOT_getAggressionMul
// Agressividade por stats[1] (0,3, 0,5, 0,7, 0,85, 1); com bots e o oponente a menos de 2,5 m, vezes
// até 3.
float BOT_getAggressionMul(Drone_tag* drone) {
    float mul;
    switch (drone->stats[1]) {
        case 0: mul = 0.3f; break;
        case 1: mul = 0.5f; break;
        case 2: mul = 0.7f; break;
        case 3: mul = 0.85f; break;
        default: mul = 1.0f; break;
    }
    if (bots_on() && drone->opponent != nullptr && drone->opp_dist < 2.5f) {
        float k = (2.5f - drone->opp_dist) + 1.0f;
        if (3.0f < k) k = 3.0f;
        mul = mul * k;
    }
    return mul;
}
