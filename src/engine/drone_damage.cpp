// Port do dano dos personagens da IA: o que a colisão entrega (NDrone2_DealWithObjHit, chamada por
// Drone_CollisionHandler para cada acerto), os impactos tratados pelos estados (DroneFunc_HandleImpact:
// tiro, soco, explosão, atordoamento), o cálculo do dano (NDrone2_HitDamage) e as falas de dor. Ver
// drone.h.
//
// Caminho de um tiro: o Collide_Update põe o projétil (objeto tipo 5) na lista de acertos do
// personagem; NDrone2_DealWithObjHit filtra fogo amigo, solta fumaça e empurra o personagem
// (push_offset, até 0,175 m) e manda a mensagem 8 (Drone_BulletHit; 6/7/0x19 para socos e choques,
// pela arma) com o HITDATA_tag como dado. O estado atual recebe a mensagem e chama
// DroneFunc_HandleImpact, que leva a NDrone2_BulletImpact: alerta total, NDrone2_DoHitEffects
// (multiplicadores de bots e o som do impacto), NDrone2_HitDamage (lugar do acerto: cabeça x100,
// pernas x0,75; proteções; dificuldade) e, sem vida, o estado de morte (0x44; 0x92 sem gravidade; 0xbf
// tipo 0x1d; 0xf2 bots). Com vida, avisa os outros (DroneFunc_SendHurtMessage, mensagem 0x12 a todos
// em meio segundo) e toca a animação do lugar atingido (DroneAnim_LocationImpactAnim), no máximo uma a
// cada 2 segundos.
//
// Divergências intencionais (memória sem valor ou endereço inválido no original):
// - Drone_BulletHit num objeto que não é personagem usaria um DCVars sem valor: não manda a mensagem.
// - NDrone2_ExplosiveImpact sem impacto (nulo) leria o endereço 0x20; aqui o "objeto que explodiu" é nulo.
// - NDrone2_DoHitEffects com projétil sem arma leria o endereço 0x70: não toca o som.
// - DroneAnim_LocationImpactAnim sem o último acerto (Drone_tag::last_hit nulo) ou sem o jogador animado
//   leria o endereço 0: usa a escolha ao acaso (conta em g_drone_guards).
// - NDrone2_PainTalk compara o som em curso (Drone_tag::sound) lendo o canal de áudio por ele; o módulo
//   de áudio não foi portado e os sons do port não são ponteiros, então um som em curso conta como
//   desconhecido (0xffffffff), como quando não há som.
// - O empurrão do tiro (NDrone2_DealWithObjHit) e o quique do tipo 0x1d somam um w da pilha no
//   original; aqui o w não muda.
// - Gas_Init pode devolver nulo (sem sala); o original escreveria nas flags do nulo.

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

// Valores neutros até a cópia do ELF (game/static_init.cpp).
float DroneDamage_Easy = 1.0f;
float DroneDamage_Normal = 1.0f;
float DroneDamage_Hard = 1.0f;
float DroneDamage_Head = 1.0f;
float DroneDamage_Legs = 1.0f;
float DroneDamage_Arms = 1.0f;
float DroneDamage_Torso = 1.0f;
float DroneArmour_Helmet = 1.0f;
float DroneArmour_Combat = 1.0f;
float DroneArmour_Jacket = 1.0f;
float DroneArmour_Vest = 1.0f;

namespace {

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

const uint8_t* bot_of(const Drone_tag* d) { return static_cast<const uint8_t*>(d->bot); }

uint16_t bot_player(const Drone_tag* d) {
    uint16_t v;
    std::memcpy(&v, bot_of(d) + 0x75c, 2);
    return v;
}

float hit_damage(const HITDATA_tag* hit) {
    float v;
    std::memcpy(&v, &hit->unknown_08, 4);
    return v;
}

void set_hit_damage(HITDATA_tag* hit, float v) { std::memcpy(&hit->unknown_08, &v, 4); }

uint32_t ptr_data(const void* p) { return nf::p32_u(p); }

HITDATA_tag* msg_hit(const MsgObject* msg) { return nf::p32_ptr<HITDATA_tag>(msg->data); }

// Um estado de retorno válido: não nulo e fora dos estados de impacto 0x53 a 0x55.
bool impact_state_ok(uint32_t s) { return s != 0 && (s < 0x53 || 0x56 <= s); }

// __fixunssfsi do GCC 2 (fptoui): acima de 2^31 subtrai e soma; abaixo, a conversão com sinal
// (negativos dão inteiros enormes).
uint32_t fptoui(float a) {
    if (2147483648.0f <= a) return uint32_t(int32_t(a - 2147483648.0f)) + 0x80000000u;
    return uint32_t(int32_t(a));
}

// Fases 07000009 e 0700000a: um impacto tira o personagem do ataque (tipo 9, ataque 7, estado inicial
// 0x14) e ele volta ao estado 0x15; sem ataque, ao 0x14.
void calm_down_on_impact(DCVars_tag* dcv) {
    if (GameState.level_file - 0x7000009u >= 2) return;
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x4000000) == 0) {
        dcv->sm->return_state = 0x14;
        return;
    }
    d->flags &= ~0x2000000u;
    d->attack_state = 0x89;
    d->forced_attack = 0;
    d->flags2 = 0;
    if (d->divars.params[5] == 100) {
        d->unknown_29 = 0;
        d->behaviour = d->behaviour_a;
    }
    d->type = 9;
    d->attack_type = 7;
    d->flags &= ~0x4000000u;
    d->initial_state = 0x14;
    dcv->sm->return_state = 0x15;
}

// O aviso de "ferido" para todos (mensagem 0x12 em 30 quadros, com NPCGlobals.alert como dado): um alerta
// sem objeto, com o ferido como remetente e os três parâmetros zerados, que também vira o alvo de alerta
// (NDrone2_SetupAlertTarget).
void broadcast_hurt(DCVars_tag* dcv) {
    DroneAlert_tag& h = NPCGlobals.alert;
    uint32_t sender = 0;
    h.sender = dcv->obj;
    h.cel = h.sender->cel;
    if (h.sender != nullptr && h.sender->type == 2) sender = static_cast<Drone_tag*>(h.sender->data)->sm.id;
    h.frame = GameState.frame;
    h.msg = 0x12;
    h.obj = nullptr;
    h.pos = h.sender->pos;
    h.param[0] = 0.0f;
    h.param[1] = 0.0f;
    h.param[2] = 0.0f;
    NDrone2_SetupAlertTarget();
    Drone_SM_BroadcastMsg(0x12, ptr_data(&h), 0x1e, sender);
}

// Variante da animação de impacto pelo lado de onde vem a explosão: 1 de frente, 0 de trás, 2/3 dos lados.
uint32_t blast_side(obj_tag* obj, const DroneImpact_tag* impact) {
    if (impact == nullptr) return 1;
    const float dx = impact->pos.x - obj->pos.x;
    const float dz = impact->pos.z - obj->pos.z;
    const float diff = Vec_AngleDifference(obj->rot.y, ATAN2_APPROX(dx, dz));
    if (std::fabs(diff) < 0.7853982f) return 1;
    if (std::fabs(diff) <= 2.3561947f) return diff < 0.0f ? 2 : 3;
    return 0;
}

void log_hit_if_enemy(DCVars_tag* dcv) {
    if (dcv->drone->side == 1) PlrStat_LogShotHitEnemy(0);
}

}  // namespace

// ---- objetos que machucam (módulo Hurt) ----

// [decomp] 0x0017ea18 Hurt_GetDamage
// Dano de um objeto que machuca (tipo 0x27, dados +0x10); 0 para os outros.
float Hurt_GetDamage(obj_tag* obj) {
    if (obj == nullptr || obj->type != 0x27) return 0.0f;
    float v;
    std::memcpy(&v, static_cast<const uint8_t*>(obj->data) + 0x10, 4);
    return v;
}

// [decomp] 0x0017ea50 Hurt_GetType
// Tipo do dano de um objeto que machuca (dados +0x14).
uint16_t Hurt_GetType(obj_tag* obj) {
    if (obj == nullptr || obj->type != 0x27) return 0;
    return static_cast<const uint8_t*>(obj->data)[0x14];
}

// ---- acertos da colisão ----

// [decomp] 0x0013a9f8 Drone_BulletHit
// Um tiro com dano: conta o acerto, guarda quem atirou e manda a mensagem 8 (dado: o acerto) ao
// personagem.
void Drone_BulletHit(obj_tag* obj, BU_tag* bullet, HITDATA_tag* hit) {
    if (obj == nullptr || (obj->life & 1) != 0) return;
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    if (d == nullptr || !(0.0f < hit_damage(hit))) return;
    ++d->hits_taken;
    d->attacker = bullet->from;
    if (Drone_bDisableSystem != 0) return;
    DCVars_tag dcv{};
    if (Drone_DCVfromOBJ(obj, &dcv) == 0) {
        ++g_drone_guards;  // port: o original mandaria com um DCVars sem valor
        return;
    }
    Drone_SM_SendMsgSelf(8, ptr_data(hit), 0, 0, &dcv);
}

// [decomp] 0x0013fff0 NDrone2_DealWithObjHit
// Um acerto da cápsula do personagem. Água (materiais 13 e 14) faz marola nos passos; o tipo 0x1d
// quica (inverte a velocidade) ao encostar. Por objeto: o jogador encostou (mensagem 0x22; guarda a
// direção do contato), um objeto que machuca (0x27) tira vida (estado 0x44 sem ela), 0x2a manda 0x17,
// um projétil (5) de quem pode ferir este personagem solta fumaça, empurra e manda a mensagem do tiro
// (6 soco, 7 choque, 0x19 dardo, 8 os outros, Drone_BulletHit); com o tipo 0x1d, outros objetos o
// repelem.
void NDrone2_DealWithObjHit(obj_tag* obj, Drone_tag* d, HITDATA_tag* hit) {
    if (hit == nullptr) return;
    const uint8_t surface = hit->surface & 0x3f;
    if (surface < 0xf && 0xc < surface) {
        if ((anim_of(d->obj)->flags & 4) != 0) Env_WaterRipple(hit);
    } else if (d->type == 0x1d && (anim_of(d->obj)->flags & 0x10) != 0) {
        Vec_MulR32(&d->fall, &d->fall, -1.0f);
    }
    if (hit->obj == nullptr) return;
    d->last_hit = hit;
    switch (hit->obj->type) {
        case 2: {
            DCVars_tag dcv{};
            Drone_DCVfromOBJ(obj, &dcv);  // (sem efeito, como no original)
            return;
        }
        case 3: {
            d->touching_player = 1;
            Vec_Zero(&d->contact_local);
            Vec_Zero(&d->contact_rot);
            d->face_dir = {hit->pos.x - d->obj->pos.x, hit->pos.y - d->obj->pos.y, hit->pos.z - d->obj->pos.z,
                           hit->pos.w - d->obj->pos.w};
            MATRIX inv;
            Mat_Inverse(&d->obj->matrix, &inv);
            ApplyMatrixLV(&inv, &d->face_dir, &d->contact_local);
            Vec_DirToRot(&d->contact_local, &d->contact_rot);
            Drone_Message(obj, 0x22, 0, 0);
            return;
        }
        case 0x27:
            if (!bots_on()) {
                if (GameState.level_file == 0x7000005 || d->klass == 0x13) return;
                d->health = d->health - Hurt_GetDamage(hit->obj);
                if (d->health <= 0.0f) Drone_SM_SetState(&d->sm, 0x44, 0);
                return;
            }
            if (BOTSTATE_getStateType(d->sm.state) == 10) return;
            if (GameFlow_GetState() != 2) return;
            d->health = d->health - Hurt_GetDamage(hit->obj);
            Hurt_GetType(hit->obj);  // (o resultado não é usado)
            {
                DCVars_tag dcv{};
                Drone_DCVfromOBJ(obj, &dcv);
                BOT_soundEffect(&dcv, 0x1f);
            }
            if (d->health <= 0.0f) Drone_SM_SetState(&d->sm, 0xf2, 0);
            return;
        case 0x29:
        case 0x2b:
        case 0x2c:
        case 0x2d:
            return;
        case 0x2a:
            Drone_Message(obj, 0x17, 0, 0);
            return;
        case 5:
            break;
        default:
            if (d->type != 0x1d) return;
            {
                Vec_MulR32(&d->fall, &d->fall, 0.0f);
                _VECTOR away{d->obj->pos.x - hit->pos.x, d->obj->pos.y - hit->pos.y, d->obj->pos.z - hit->pos.z,
                             d->obj->pos.w - hit->pos.w};
                Vec_Normalise(&away, &away);
                d->fall.x = d->fall.x + away.x * 1.0f;
                d->fall.y = d->fall.y + away.y * 1.0f;
                d->fall.z = d->fall.z + away.z * 1.0f;
            }
            return;
    }
    // Projétil.
    BU_tag* b = static_cast<BU_tag*>(hit->obj->data);
    if (b == nullptr) return;
    const uint16_t id = static_cast<uint16_t>(b->weapon->id);
    switch (id) {
        case 0x1b: case 0x2a: case 0x2b: case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x34: case 0x35:
        case 0x36: case 0x37: case 0x3a: case 0x46: case 0x48: case 0x49: case 0x4e: case 0x50: case 0x51:
        case 0x5f:
            return;
        case 0x6e:
            if (d->klass == 0x10) return;
            break;
        default:
            break;
    }
    obj_tag* shooter = b->from;
    if (shooter != nullptr) {
        // Quem pode ferir: o jogador e as torres (0x36) sempre; outro personagem só de outro lado
        // (inimigos de um lado, aliados e civis do outro); com bots, quem não é do mesmo time.
        bool hurt = false;
        if (bots_on()) {
            if (static_cast<uint8_t>(shooter->type - 2) < 2) {
                if (mp_word(0x198) != 0 || !MP_areObjectsOnSameTeam(shooter, d->obj)) hurt = true;
            } else {
                hurt = true;
            }
        } else if (shooter->type == 3 || shooter->type == 0x36) {
            hurt = true;
        } else if (shooter->type == 2) {
            bool enemy = true;
            if (d->side != 1 && d->side != 0 && d->side <= 3) enemy = false;
            const Drone_tag* sd = static_cast<const Drone_tag*>(shooter->data);
            if (sd != nullptr) {
                if (sd->side == 1) {
                    hurt = !enemy;
                } else if (sd->side != 0 && sd->side <= 3) {
                    hurt = enemy;
                }
            }
        }
        if (d->health <= 0.0f || !hurt) return;
    }
    switch (id) {
        case 0x2b: case 0x34: case 0x35: case 0x36: case 0x37:
            break;
        case 0x32: case 0x33:
            Gas_Init(obj->cel, &hit->pos, nullptr, nullptr, 1.0f, 16.0f, 0x200026e, 5, 0xffffffffu, 0xffffff00u, 0x3c);
            break;
        default:
            if (0.0f < b->weapon->damage) {
                obj_tag* gas = Gas_Init(obj->cel, &hit->pos, nullptr, nullptr, 3.0f, 6.0f, 0x20008bc, 5, 0xffffffffu,
                                        0xffffff00u, 0x3c);
                if (gas != nullptr) gas->flags |= 8;  // port: o original não confere o nulo
            }
            break;
    }
    // O empurrão: 0,175 do sentido do tiro, somado ao atual e limitado a 0,175 m.
    _VECTOR kick{};
    Vec_MulR32(&kick, &hit->dir, 0.175f);
    d->push_offset = {d->push_offset.x + kick.x, d->push_offset.y + kick.y, d->push_offset.z + kick.z,
                      d->push_offset.w + kick.w};
    float len = Vec_NormaliseLen(&d->push_offset, &d->push_offset);
    if (0.175f < len) len = 0.175f;
    Vec_MulR32(&d->push_offset, &d->push_offset, len);
    uint32_t msg = 0;
    if (id == 0x4a || id == 0x4c) {
        msg = 7;
    } else if (id == 1 || id == 0x4b || id == 0x4d) {
        msg = 6;
    } else if (id == 0x43 || id == 0x44) {
        msg = 0x19;
    }
    if (msg == 0) {
        Drone_BulletHit(obj, b, hit);
        return;
    }
    ++d->hits_taken;
    d->attacker = b->from;
    Drone_Message(obj, msg, ptr_data(hit), 0);
}

// ---- dano ----

// [decomp] 0x00145558 NDrone2_HitDamage
// Aplica o dano pelo lugar do acerto (location: a junta; 5 cabeça, 0x14-0x27 braços, 0x31-0x38
// pernas, o resto tronco), pelas proteções e pela dificuldade. Tiro na cabeça com soco ou choque
// (armas 1, 0x4a-0x4d) conta como no tronco. Personagens não vulneráveis só guardam o dano (a não ser
// com force). Com vida, fala de dor.
void NDrone2_HitDamage(DCVars_tag* dcv, float damage, int32_t location, int32_t weapon, int8_t force) {
    if (bots_on()) {
        BOT_handlePain(dcv, damage, 0, static_cast<int16_t>(location));
        return;
    }
    Drone_tag* d = dcv->drone;
    bool log = true;
    if (location < 0) location = 1;
    if (location == 5 && (weapon == 1 || weapon == 0x4a || weapon == 0x4c || weapon == 0x4b || weapon == 0x4d)) {
        location = 1;
        log = false;
    }
    if (GameState.level_file == 0x700001b && location != 5) location = 1;
    if (d->vulnerable == 0 && force == 0) {
        d->last_damage = damage;
        return;
    }
    const uint8_t armour = d->armour;
    // Arma e perna: conta o acerto se veio do oponente ou se o personagem ainda vive.
    const auto log_limb = [&]() {
        if (!log) return;
        Drone_tag* x = dcv->drone;
        if (x->attacker != x->opponent && x->health <= 0.0f) return;
        log_hit_if_enemy(dcv);
    };
    switch (location) {
        case 5: {
            float k;
            if (d->type == 0xd || d->skin == 0x50000ba) {
                k = DroneDamage_Torso + DroneDamage_Torso;
            } else if (d->klass == 0x10 && GameState.level_file == 0x7000014) {
                k = DroneDamage_Torso + DroneDamage_Torso;
            } else {
                dcv->drone->head_shot = 1;
                k = DroneDamage_Head;
            }
            damage = damage * k;
            if ((armour & 4) != 0) damage = damage * DroneArmour_Helmet;
            if (log && dcv->drone->attacker == dcv->drone->opponent) log_hit_if_enemy(dcv);
            break;
        }
        case 0x14: case 0x15: case 0x17: case 0x20: case 0x23: case 0x27:
            damage = damage * DroneDamage_Arms;
            if ((armour & 8) != 0) damage = damage * DroneArmour_Combat;
            if ((armour & 2) != 0) damage = damage * DroneArmour_Jacket;
            log_limb();
            break;
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38:
            damage = damage * DroneDamage_Legs;
            if ((armour & 8) != 0) damage = damage * DroneArmour_Combat;
            log_limb();
            break;
        default:
            damage = damage * DroneDamage_Torso;
            if ((armour & 8) != 0) damage = damage * DroneArmour_Combat;
            if ((armour & 2) != 0) damage = damage * DroneArmour_Jacket;
            if ((armour & 1) != 0) damage = damage * DroneArmour_Vest;
            if (log && dcv->drone->attacker == dcv->drone->opponent) log_hit_if_enemy(dcv);
            break;
    }
    float k = DroneDamage_Normal;
    const uint32_t difficulty = GameState.difficulty;
    if (difficulty == 1) {
        k = DroneDamage_Easy;
    } else if (difficulty == 3 || difficulty == 4) {
        k = DroneDamage_Hard;
    }
    damage = damage * k;
    if (d->vulnerable != 0) dcv->drone->health = dcv->drone->health - damage;
    if (0.0f < dcv->drone->health) NDrone2_PainTalk(dcv);
    d->last_damage = damage;
}

// [decomp] 0x001458e0 NDrone2_DoHitEffects
// O dano de um projétil (HITDATA_tag::unknown_08, vezes mul): com bots, a perícia de quem atirou o
// aumenta e, no modo assassino, o alvo morre com um tiro na cabeça ou nas costas. Toca o som do
// impacto (cabeça 0x601, corpo 0x413) para tiros acima de 0,5 que não quicam nem grudam.
void NDrone2_DoHitEffects(DCVars_tag* dcv, HITDATA_tag* hit, float mul) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (hit == nullptr || hit->obj == nullptr) return;
    BU_tag* b = static_cast<BU_tag*>(hit->obj->data);
    if (b == nullptr) return;
    if (bots_on()) {
        obj_tag* from = b->from;
        if (3 < Control_Plr2Ind(from) && from != nullptr) {
            const uint16_t id = static_cast<uint16_t>(b->weapon->id);
            const uint8_t skill = bot_of(static_cast<Drone_tag*>(from->data))[0xac];
            if (id == 1 || id == 100 || id == 0x65 || id == 0x66) {
                if ((skill & 4) != 0) set_hit_damage(hit, hit_damage(hit) * 1.5f);
            } else if ((skill & 2) != 0) {
                set_hit_damage(hit, hit_damage(hit) * 1.25f);
            }
        }
        if (mp_word(0x1a4) == 0x400 && MP_IsAssasin(b->from) && MP_IsTarget(obj)) {
            const uint16_t joint = hit->unknown_52;
            if (joint != 0 && joint < 5) {
                const float a = ATAN2_APPROX(hit->dir.x, hit->dir.z);
                if (std::fabs(Vec_AngleDifference(a, dcv->obj->rot.y) * 57.295776f) < 70.0f) {
                    set_hit_damage(hit, d->health);
                }
            } else if (joint == 5) {
                set_hit_damage(hit, d->health);
            }
        }
    }
    const int8_t force = (b->from != nullptr && d->klass == 0xc && b->from->type == 3) ? 1 : 0;
    const int32_t weapon = b->weapon != nullptr ? static_cast<uint16_t>(b->weapon->id) : -1;
    NDrone2_HitDamage(dcv, hit_damage(hit) * mul, hit->unknown_52, weapon, force);
    // Port: sem arma, o original leria as flags no endereço 0x70; aqui não toca o som.
    if (0.5f < hit_damage(hit) && b->weapon != nullptr && (b->weapon->hit_flags & 0x180) == 0) {
        Sound_Play3D(hit->unknown_52 == 5 ? 0x601 : 0x413, &hit->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    if (bots_on()) MP_RegisterBulletHit(static_cast<int16_t>(bot_player(d)), hit->obj, d->health);
}

// [decomp] 0x00145e28 DroneFunc_SendHurtMessage
// Com a propriedade 0x32, uma vez (flags 8) e fora do tipo 2: avisa todos que foi ferido.
void DroneFunc_SendHurtMessage(DCVars_tag* dcv) {
    if (behaviour_util_getProperty(0x32, dcv->drone->behaviour) == 0) return;
    Drone_tag* d = dcv->drone;
    if ((d->flags & 8) != 0 || d->type == 2) return;
    d->flags2 |= 2;
    broadcast_hurt(dcv);
    dcv->drone->flags |= 8;
}

// ---- impactos (chamados pelos estados) ----

// [decomp] 0x00145f28 NDrone2_PunchImpact
// Soco: alerta, dano (NDrone2_DoHitEffects) e, sem vida, o estado 0x42 (0xf2 bots). Na fase 07000007,
// bater em quem está desarmado falha a missão; nos outros lados, também (menos em 09/0a). Com anim, toca
// a animação 0x51 e volta a return_state; senão vai a state.
uint32_t NDrone2_PunchImpact(DCVars_tag* dcv, HITDATA_tag* hit, uint32_t state, int8_t anim) {
    Drone_tag* d = dcv->drone;
    if (d->ignore_impacts != 0) return 1;
    const uint32_t current = d->sm.state;
    const uint32_t next = state == current ? 0 : state;
    const uint32_t ret = state != 0 ? state : current;
    if (impact_state_ok(ret)) d->sm.return_state = ret;
    dcv->drone->alert = 1.0f;
    Drone_AlertStatusSet(2, dcv);
    NDrone2_DoHitEffects(dcv, hit, dcv->drone->hit_damage_mul);
    if (GameState.level_file == 0x7000007 && NDrone2_ObjectIsArmed(d->obj) == 0) DroneFunc_SetMissionFailReason(d, 5);
    const auto death_state = [&]() { return dcv->drone->type != 0x1e ? 0x42u : 0xf2u; };
    if (dcv->drone->side == 1) {
        if (d->health <= 0.0f) return Drone_SM_SetState(dcv->sm, death_state(), 0);
        DroneFunc_SendHurtMessage(dcv);
    } else {
        if ((0x700000a < GameState.level_file || GameState.level_file < 0x7000009) &&
            NDrone2_ObjectIsArmed(d->obj) == 0) {
            DroneFunc_SetMissionFailReason(d, 6);
        }
        DroneFunc_SendHurtMessage(dcv);
        if (d->health <= 0.0f) return Drone_SM_SetState(dcv->sm, death_state(), 0);
    }
    if (!bots_on()) NDrone2_PainTalk(dcv);
    if (anim != 0) {
        DroneAnim_CallAnim(0.0f, 0x51, 0, static_cast<int16_t>(d->sm.return_state), 0, 0, dcv);
        NPCGlobals.impact_anim_frame = GameState.frame;
        d->impact_anim_frame = GameState.frame;
        return 1;
    }
    if (!impact_state_ok(next)) return 0;
    return Drone_SM_SetState(dcv->sm, next, 0);
}

// [decomp] 0x001461c8 NDrone2_BulletImpact
// Tiro: alerta total, dano e aviso aos outros; sem vida, o estado de morte (0x44; 0x92 sem gravidade,
// 0xbf o tipo 0x1d, 0xf2 bots). Com vida, fala; com anim, a animação do lugar atingido (no máximo
// uma a cada 2 s por personagem e no total), devolvendo 0; o tipo 0x10 reage em alguns estados; por
// fim vai a state.
uint32_t NDrone2_BulletImpact(DCVars_tag* dcv, HITDATA_tag* hit, uint32_t state, int8_t anim) {
    Drone_tag* d = dcv->drone;
    d->alert = 1.0f;
    const uint32_t ret = state != 0 ? state : d->sm.state;
    if (state == d->sm.state) state = 0;
    Drone_AlertStatusSet(2, dcv);
    NDrone2_DoHitEffects(dcv, hit, dcv->drone->hit_damage_mul);
    DroneFunc_SendHurtMessage(dcv);
    if (d->health <= 0.0f) {
        if (dcv->drone->type == 0x1e) return Drone_SM_SetState(dcv->sm, 0xf2, 0);
        const uint8_t t = d->type;
        uint32_t s = 0x44;
        if (0x1b <= t && t < 0x1d) s = 0x92;
        if (t == 0x1d) s = 0xbf;
        return Drone_SM_SetState(dcv->sm, s, 0);
    }
    DroneFunc_InjuredTalk(dcv);
    if (!bots_on() && anim != 0) {
        Drone_tag* x = dcv->drone;
        x->heavy_hit = (x->max_health * 0.25f < x->last_damage && x->max_health * 0.333f < x->health) ? 1 : 0;
        const uint32_t now = GameState.frame;
        const uint32_t gap = FRAME_RATE_INT * 2;
        if (x->impact_anim_frame + gap <= now && NPCGlobals.impact_anim_frame + gap <= now) {
            if (impact_state_ok(ret)) d->sm.return_state = ret;
            DroneAnim_LocationImpactAnim(dcv);
            NPCGlobals.impact_anim_frame = GameState.frame;
            d->impact_anim_frame = GameState.frame;
            return 0;
        }
    }
    Drone_tag* x = dcv->drone;
    if (x->type == 0x10) {
        if (dcv->sm->state == 0x38) return Drone_SM_SetState(dcv->sm, 0x56, 0);
        if (dcv->sm->state == 0x94) {
            behaviour_util_setProperty(0x10, x->behaviour, 0);
            return Drone_SM_SetState(dcv->sm, 0x9c, 0);
        }
    }
    if (!impact_state_ok(state)) return 1;
    return Drone_SM_SetState(dcv->sm, state, 0);
}

// [decomp] 0x00146480 NDrone2_ExplosiveImpact
// Explosão: a própria explosão de um 0x50000ba não o fere. Alerta, dano (sem lugar), e sem vida o
// estado de morte (0x45, com o impacto como dado; 0x44 tipo 0xd; 0x92 sem gravidade; 0xbf tipo 0x1d;
// 0xf3 bots). Com vida, avisa os outros (sem a exceção do tipo 2 de DroneFunc_SendHurtMessage); com
// anim, a animação 0x47 pelo lado da explosão; senão fala e vai a state.
uint32_t NDrone2_ExplosiveImpact(DCVars_tag* dcv, DroneImpact_tag* impact, uint32_t state, int8_t anim) {
    Drone_tag* d = dcv->drone;
    const uint32_t ret = state != 0 ? state : d->sm.state;
    if (state == d->sm.state) state = 0;
    obj_tag* source = impact != nullptr ? impact->source : nullptr;  // port: o original leria o endereço 0x20
    if (source == d->obj && d->skin == 0x50000ba) {
        if (!impact_state_ok(state)) return 1;
        return Drone_SM_SetState(dcv->sm, state, 0);
    }
    d->alert = 1.0f;
    Drone_AlertStatusSet(2, dcv);
    if (impact != nullptr && 0.0f < impact->damage) {
        NDrone2_HitDamage(dcv, impact->damage, 0, -1, 0);
        Drone_tag* x = dcv->drone;
        if (x->attacker == x->opponent || 0.0f < x->health) log_hit_if_enemy(dcv);
        if (bots_on()) {
            MP_RegisterBulletHit(static_cast<int16_t>(bot_player(dcv->drone)), impact->source, dcv->drone->health);
        }
    }
    if (d->health <= 0.0f) {
        if (bots_on()) return Drone_SM_SetState(dcv->sm, 0xf3, ptr_data(impact));
        const uint8_t t = d->type;
        if (0x1b <= t && t < 0x1d) {
            Drone_SM_SetState(dcv->sm, 0x92, 0);
            return 1;
        }
        if (t == 0xd) {
            Drone_SM_SetState(dcv->sm, 0x44, 0);
            return 1;
        }
        if (t == 0x1d) {
            Drone_SM_SetState(dcv->sm, 0xbf, 0);
            return 1;
        }
        return Drone_SM_SetState(dcv->sm, 0x45, ptr_data(impact));
    }
    if (behaviour_util_getProperty(0x32, dcv->drone->behaviour) != 0 && (dcv->drone->flags & 8) == 0) {
        dcv->drone->flags2 |= 2;
        broadcast_hurt(dcv);
        dcv->drone->flags |= 8;
    }
    if (anim != 0) {
        if (dcv->drone->type == 0x1e) {
            DroneAnim_CallAnim(0.0f, 0x47, blast_side(dcv->obj, impact), 0xcf, 0, 0, dcv);
            return 1;
        }
        if (impact_state_ok(ret)) d->sm.return_state = ret;
        if (d->type == 0x1d || d->klass == 0x14 || d->type == 0xd) return 1;
        DroneAnim_CallAnim(0.0f, 0x47, blast_side(dcv->obj, impact), static_cast<int16_t>(d->sm.return_state), 0, 0,
                           dcv);
        NDrone2_PainTalk(dcv);
        return 1;
    }
    if (!bots_on()) NDrone2_PainTalk(dcv);
    if (!impact_state_ok(state)) return 0;
    return Drone_SM_SetState(dcv->sm, state, 0);
}

// [decomp] 0x001469b8 DroneFunc_HandleImpact
// O que um estado faz com uma mensagem de impacto (state: o estado para onde voltar, 0 o atual; anim:
// o estado toca a animação de impacto). 6 soco (estado 0x53 com anim), 7 choque (atordoa por 2 x vida
// em quadros, estado 0x4a), 8 tiro (NDrone2_BulletImpact; estado 0x55 se ele não decidiu), 9 explosão
// (0x54 com anim), 0x17 (0x7b), 0x18 gás (0x4d; com a propriedade 0x1c, tosse), 0x19 dardo (0x50).
uint32_t DroneFunc_HandleImpact(DCVars_tag* dcv, MsgObject* msg, uint32_t state, int8_t anim) {
    Drone_tag* d = dcv->drone;
    if (d->ignore_impacts != 0) return 1;
    const uint32_t current = d->sm.state;
    const uint32_t next = state != current ? state : 0;
    const uint32_t ret = state != 0 ? state : current;
    const auto keep_return = [&]() {
        if (impact_state_ok(ret)) d->sm.return_state = ret;
    };
    switch (msg->msg) {
        case 6:
            if (anim == 0) {
                NDrone2_PunchImpact(dcv, msg_hit(msg), ret, 0);
                return 1;
            }
            keep_return();
            Drone_SM_SetState(dcv->sm, 0x53, msg->data);
            return 1;
        case 7: {
            if (GameState.level_file == 0x7000007 && NDrone2_ObjectIsArmed(d->obj) == 0) {
                DroneFunc_SetMissionFailReason(d, 7);
            }
            Drone_tag* x = dcv->drone;
            x->stun_time = static_cast<uint16_t>(static_cast<int32_t>(float(x->stun_time) + FRAME_RATE_MUL));
            x = dcv->drone;
            const uint32_t stun = x->stun_time;
            if (stun <= fptoui(x->health) * 2) {
                if (stun != 1) return 1;
                if ((x->flags & 0x4000000) == 0 && next != 0) Drone_SM_SetState(dcv->sm, next, 0);
                return 1;
            }
            keep_return();
            calm_down_on_impact(dcv);
            Drone_SM_SetState(dcv->sm, 0x4a, msg->data);
            return 1;
        }
        case 8: {
            uint32_t s = state;
            if (d->klass == 0xc) s = 0x6e;
            if (impact_state_ok(ret)) s = ret;
            if (NDrone2_BulletImpact(dcv, msg_hit(msg), s, anim) != 0) return 1;
            keep_return();
            Drone_SM_SetState(dcv->sm, 0x55, msg->data);
            return 1;
        }
        case 9:
            if (anim == 0) {
                NDrone2_ExplosiveImpact(dcv, nf::p32_ptr<DroneImpact_tag>(msg->data), state, 0);
                return 1;
            }
            keep_return();
            Drone_SM_SetState(dcv->sm, 0x54, msg->data);
            return 1;
        case 0x17:
            if (behaviour_util_getProperty(0x36, dcv->drone->behaviour) != 0) return 1;
            if (behaviour_util_getProperty(0x34, dcv->drone->behaviour) != 0) return 1;
            keep_return();
            calm_down_on_impact(dcv);
            Drone_SM_SetState(dcv->sm, 0x7b, msg->data);
            return 1;
        case 0x18:
            if (behaviour_util_getProperty(0x1c, dcv->drone->behaviour) != 0) {
                if (d->subclass != 5) return 1;
                uint32_t call = 0x19;
                if (DroneAnim_CanDoAnimState(dcv, 0x57, 0)) {
                    call = 0x57;
                } else if (DroneAnim_CanDoAnimState(dcv, 0x11, 0)) {
                    call = 0x11;
                }
                DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
                return 1;
            }
            keep_return();
            calm_down_on_impact(dcv);
            Drone_SM_SetState(dcv->sm, 0x4d, msg->data);
            return 1;
        case 0x19:
            PlrStat_LogShotHitEnemy(0);
            keep_return();
            calm_down_on_impact(dcv);
            Drone_SM_SetState(dcv->sm, 0x50, 0);
            return 1;
        default:
            return 1;
    }
}

// [decomp] 0x0013c8b8 DroneAnim_LocationImpactAnim
// Animação de impacto (chamada 0x4c) pela junta atingida (last_hit): 5 cabeça, 8 braço, 9 ombro, 0xc e
// 0xd pernas, 6/7 ao acaso no tronco; ao acaso (5 a 12) se o jogador não está animado. Sem gravidade
// (tipos 0x1b/0x1c) a variante passa por uma tabela própria. Ao terminar, volta a return_state.
uint32_t DroneAnim_LocationImpactAnim(DCVars_tag* dcv) {
    // 0x002f48b0, 18 entradas (copiadas para a pilha no original).
    static const int32_t kZeroG[18] = {0, 0, 0, 0, 0, 1, 1, 2, 1, 2, 3, 4, 3, 4, 0, 0, 0, 0};
    Drone_tag* d = dcv->drone;
    const HITDATA_tag* hit = d->last_hit;
    obj_tag* player = glb_players[0];
    uint32_t variant;
    const bool player_animated =
        player != nullptr && player->sphere_src != nullptr && (anim_of(player)->flags & 1) != 0;
    if (!player_animated || hit == nullptr) {
        if (player == nullptr || player->sphere_src == nullptr || (player_animated && hit == nullptr)) {
            ++g_drone_guards;  // port: o original leria o endereço 0
        }
        variant = Rand_Rand(8) + 5;
    } else {
        switch (hit->unknown_52) {
            case 4: case 5:
                variant = 5;
                break;
            case 0x12: case 0x13: case 0x14: case 0x15:
                variant = 8;
                break;
            case 0x1e: case 0x1f: case 0x20: case 0x23:
                variant = 9;
                break;
            case 0x31: case 0x32: case 0x33:
                variant = 0xd;
                break;
            case 0x35: case 0x36: case 0x37:
                variant = 0xc;
                break;
            default:
                variant = Rand_Rand(2) + 6;
                break;
        }
    }
    if (static_cast<uint8_t>(d->type - 0x1b) < 2) variant = static_cast<uint32_t>(kZeroG[variant]);
    DroneAnim_CallAnim(0.0f, 0x4c, variant, static_cast<int16_t>(dcv->sm->return_state), 0, 1, dcv);
    return 1;
}

// ---- falas de dor ----

// [decomp] 0x0013ed38 NDrone2_HitTalkExtra
// Fala extra ao ser atingido (só sem bots, sem outra fala em curso e com vida; logo depois de outra
// fala, 4 em 5 vezes): classes 5 a 7 nas fases 14 a 16 e a classe 0x10.
uint32_t NDrone2_HitTalkExtra(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    if (d->sound != 0) {
        if (Sound_IsFinnished(d->sound) == 0) return 0;
        d->sound = 0;
    }
    if (d->health <= 0.0f) return 0;
    if (GameState.frame < d->sfx_time + FRAME_RATE_INT * 3 && Rand_Rand(5) == 0) return 0;
    uint32_t sfx = 0;
    const uint16_t k = d->klass;
    if (4 < k) {
        if (k < 8) {
            const uint32_t level = GameState.level_file;
            if (level == 0x7000014 || level == 0x7000016 || level == 0x7000015) {
                const uint32_t r = Rand_Rand(5);
                sfx = (d->sm.id & 1) != 0 ? r + 0x138 : r + 0x133;
            }
        } else if (k == 0x10) {
            sfx = Rand_Rand(7) + 0xec;
        }
    }
    if (sfx == 0) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return 1;
}

// [decomp] 0x0013eeb8 NDrone2_PainTalk
// Grito de dor pela classe do personagem (0x10 tenta antes a fala extra), se não for o som em curso.
uint32_t NDrone2_PainTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    uint32_t sfx;
    switch (d->klass) {
        case 1: sfx = 0x12; break;
        case 2: sfx = 0x13; break;
        case 3: sfx = 0x11; break;
        case 7: sfx = 0x14; break;
        case 8: case 10: sfx = 0x18; break;
        case 9: sfx = 0x15; break;
        case 0xc: sfx = 0x1b; break;
        case 0x10:
            if (NDrone2_HitTalkExtra(dcv) != 0) return 0;
            sfx = 0x16;
            break;
        case 0x12: case 0x13: sfx = 0x1c; break;
        case 0x14: sfx = 0x10; break;
        default: sfx = d->unknown_15 != 0 ? 0x17 : 0xf; break;
    }
    // Port: o som em curso seria lido do canal de áudio (som + 0x34); ver o cabeçalho.
    const uint32_t current = 0xffffffffu;
    if (Drone_SfxInfo[sfx].sfx == current) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return 1;
}

// [decomp] 0x0013fa20 DroneFunc_InjuredTalk
// Fala de ferido, só com bots (como no original): pela fase, se não há outra fala em curso.
void DroneFunc_InjuredTalk(DCVars_tag* dcv) {
    if (!bots_on()) return;
    Drone_tag* d = dcv->drone;
    if (d->sound != 0) {
        if (Sound_IsFinnished(d->sound) == 0) return;
        d->sound = 0;
    }
    if (!(0.0f < d->health)) return;
    uint32_t sfx = 0;
    switch (GameState.level_file) {
        case 0x700000c:
        case 0x700000d:
            sfx = Rand_Rand(6) + 0xcc;
            break;
        case 0x7000011:
        case 0x7000012:
        case 0x7000013:
        case 0x700004a:
            sfx = 0xb9;
            break;
        case 0x7000014:
        case 0x7000015:
        case 0x7000016:
            if (d->klass != 0x10) {
                const uint32_t r = Rand_Rand(5);
                sfx = (d->sm.id & 1) != 0 ? r + 0x138 : r + 0x133;
            }
            break;
        default:
            break;
    }
    if (sfx != 0 && Rand_Rand(0) == 0) {
        NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
        NPCGlobals.injured_talk_time = GameState.frame + FRAME_RATE_INT * 5;
    }
}
