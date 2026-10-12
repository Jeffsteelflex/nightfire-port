// Port do módulo Explode (explosões) e das funções de dano que ele chama. Ver explode.h.

#include "engine/explode.h"

#include <cmath>
#include <cstring>

#include "engine/break.h"
#include "engine/bullet.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

ExplodeStats g_explode_stats;
ExplodeGuards g_explode_guards;

namespace {

// Modelos dos detritos de Explode_Update (MetalHash.132 e StoneHash.133, .data do ACTION.ELF).
uint32_t MetalHash_132[4] = {0x02000414, 0x02000415, 0x02000416, 0x0200041b};
uint32_t StoneHash_133[4] = {0x02000664, 0x02000665, 0x02000666, 0x02000664};

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}

EXPLODE_tag* explode_of(obj_tag* obj) { return static_cast<EXPLODE_tag*>(obj->data); }

uint8_t* script_bytes(SCRIPTINFO* s) { return reinterpret_cast<uint8_t*>(s); }

void set_hit_damage(HITDATA_tag* hit, float damage) { std::memcpy(&hit->unknown_08, &damage, 4); }

}  // namespace

void explode_debug_power_on() {
    g_explode_stats = {};
    g_explode_guards = {};
}

// [decomp] 0x00134880 control_ClosestPlayerPnt
// A posição do jogador mais perto de pos (out não muda sem jogador); devolve a distância ao quadrado
// (1e6 sem jogador mais perto que isso).
float control_ClosestPlayerPnt(_VECTOR* pos, _VECTOR* out) {
    float best = 1e6f;
    for (obj_tag* p : glb_players) {
        if (p == nullptr) continue;
        const float d = Vec_SqDist3D(pos, &p->pos);
        if (d < best) {
            *out = p->pos;
            best = d;
        }
    }
    return best;
}

// [decomp] 0x00196060 Player_Hurt
// Dano de explosão no jogador (Player_HandlePain sem tipo nem lugar); devolve a vida que sobrou.
float Player_Hurt(obj_tag* obj, float damage, _VECTOR*) {
    BLData* b = static_cast<BLData*>(obj->data);
    Player_HandlePain(obj, b, damage, 0, -1);
    return b->health;
}

// [decomp] 0x0013aab8 Drone_ExplosiveHit
// Guarda quem causou a explosão (Drone_tag::attacker) e manda a mensagem 9 ao personagem com o
// impacto; um personagem da classe 0x10 ignora a própria explosão.
// Divergência: num objeto que não é personagem (tipo 2 ou 0x11) o original mandaria a mensagem com um
// DCVars de lixo da pilha; aqui não manda (conta em g_explode_guards). Explode_Propagate só chama com
// personagens.
void Drone_ExplosiveHit(obj_tag* obj, DroneImpact_tag* impact) {
    if (obj == nullptr) return;
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    if (d == nullptr || impact == nullptr) return;
    d->attacker = impact->source;
    if (d->klass == 0x10 && obj == impact->source) return;
    if (Drone_bDisableSystem != 0) return;
    if (obj->type != 2 && obj->type != 0x11) {
        ++g_explode_guards.not_a_drone;
        return;
    }
    DCVars_tag dcv{obj, d, obj->cel, &d->sm};
    Drone_SM_SendMsgSelf(9, uint32_t(reinterpret_cast<uintptr_t>(impact)), 0, 0, &dcv);
}

// [decomp] 0x001816d8 GT_ApplyDamage
// Torreta (tipo 0x36): o dano sai da vida em data + 0xcc.
void GT_ApplyDamage(obj_tag* obj, float damage) {
    float* health = reinterpret_cast<float*>(static_cast<uint8_t*>(obj->data) + 0xcc);
    *health -= damage;
}

// [decomp] 0x001b08a8 Sensor_ApplyDamage
// Vazia no PS2.
void Sensor_ApplyDamage(obj_tag*, float) {}

// [decomp] 0x001b0968 Monitor_ApplyDamage
// Vazia no PS2.
void Monitor_ApplyDamage(obj_tag*, float) {}

// [decomp] 0x001ad570 Sub_ApplyDammage
// Submarino (tipo 0x4a): uma vez por tick, o dano sai da vida (data + 0x10); sem vida, troca as
// animações pela de afundar (0x600075e, sem tocar ao contrário), guarda a altura de onde afunda
// (data + 0xc: a altura atual, depois o chão 25 m abaixo da esfera mais |data + 0x18|, pulando o
// próprio submarino e os tipos 0x3d) e passa ao estado 1.
void Sub_ApplyDammage(obj_tag* obj, float damage) {
    uint8_t* data = static_cast<uint8_t*>(obj->data);
    uint32_t last;
    std::memcpy(&last, data + 0x38, 4);
    if (last == GameState.frame) return;
    float health;
    std::memcpy(&health, data + 0x10, 4);
    health -= damage;
    std::memcpy(data + 0x10, &health, 4);
    std::memcpy(data + 0x38, &GameState.frame, 4);
    if (!(health < 0.0f)) return;
    AnimSetDeleteAll(obj);
    ANIMSCRIPT_tag* s = AnimScriptAdd(obj, 0x600075e);
    if (s != nullptr) s->flags &= 0x7fffffffu;
    std::memcpy(data + 0xc, &obj->matrix.m[3][1], 4);
    _VECTOR end = obj->sphere;
    end.y -= 25.0f;
    HITDATA_tag* hits = nullptr;
    Collide_RayIntersect(&obj->sphere, &end, obj->cel, obj, nullptr, &hits, 0, 0, 0);
    for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
        if (h->obj != nullptr && (h->obj->type == 0x4a || h->obj->type == 0x3d)) continue;
        float depth;
        std::memcpy(&depth, data + 0x18, 4);
        const float y = h->pos.y + std::fabs(depth);
        std::memcpy(data + 0xc, &y, 4);
        break;
    }
    Collide_FreeHitList(&hits);
    obj->state = 1;
}

// [decomp] 0x0017cbd0 Explode_Create
// A explosão em pos: raio2 (o raio do dano) abaixo de 0,5 não fere; senão sacode as câmeras
// (Camera_Shake com raio2). A direção da explosão aponta para o jogador mais perto
// (control_ClosestPlayerPnt). O objeto (tipo 0xf, flags 0x30, escala raio2 / 15) guarda o dano, o
// raio, quem causou (referenciado: Control_ObjectReferenced), a sala de obj e o material; o script do
// efeito (Script_Load com o hash script) recebe o raio em +0x94, a matriz alinhada à direção
// (Mat_Align2Dir) e dá a duração (+0xab0) e o eixo dos detritos; sem script a duração é 15. normal, a, b
// e c não são usados.
// Divergências: sem jogador, a direção parte de um ponto zerado (o original usa lixo da pilha); devolve
// o objeto da explosão (o original devolve o resultado de Script_Play).
obj_tag* Explode_Create(obj_tag* obj, _VECTOR* pos, const _VECTOR*, float radius, float radius2, uint32_t script,
                        float damage, uint8_t, uint8_t, uint8_t, obj_tag* from, uint16_t surface) {
    ++g_explode_stats.created;
    if (radius2 < 0.5f) {
        damage = 0.0f;
    } else {
        Camera_Shake(pos, radius2);
    }
    _VECTOR dir{};
    control_ClosestPlayerPnt(pos, &dir);
    dir = {dir.x - pos->x, dir.y - pos->y, dir.z - pos->z, dir.w - pos->w};
    Vec_Normalise(&dir, &dir);
    obj_tag* e = control_create_object(0x40, pos, nullptr, nullptr);
    if (e == nullptr) return nullptr;
    EXPLODE_tag* x = explode_of(e);
    x->damage = damage;
    x->from = from;
    x->cel = obj->cel;
    x->script = Script_Load(script, pos, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), nullptr, nullptr, nullptr,
                            nullptr);
    x->radius = radius2;
    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &dir);
    x->surface = surface;
    x->side = {u.x + v.x, u.y + v.y, u.z + v.z, u.w + v.w};
    x->propagated = 0;
    if (x->script == nullptr) {
        x->duration = 0xf;
    } else {
        std::memcpy(script_bytes(x->script) + 0x94, &radius, 4);
        uint16_t frames;
        std::memcpy(&frames, script_bytes(x->script) + 0xab0, 2);
        x->duration = frames;
        MATRIX* m = reinterpret_cast<MATRIX*>(x->script);
        Mat_Align2Dir(m, &dir, &CONSTANT_UP1, &CONSTANT_UP2);
        Mat_GetUp(&x->up, m);
    }
    e->type = 0xf;
    e->flags = 0x30;
    e->scale = radius2 * 0.06666667f;
    e->pos = *pos;
    if (from != nullptr) Control_ObjectReferenced(from, 1);
    Script_Play(x->script, 0);
    return e;
}

// [decomp] 0x0017c350 Explode_Update
// Roda o script; quando ele acaba, libera e pede a remoção. Fora do multijogador, enquanto o script
// 0x6000052 toca e no primeiro quarto da duração, de vez em quando (quadro múltiplo de 10 a 14) solta
// três detritos do material atingido: metal (5, 6) ou, em 07000008 com o canal 0x53, pedra (8, 10, 11),
// do centro da matriz ao longo do eixo do script, a raio x 2, vivendo 3 s.
void Explode_Update(obj_tag* obj) {
    EXPLODE_tag* x = explode_of(obj);
    Script_Update(x->script);
    if (Script_IsPlaying(x->script) == 0) {
        Script_Free(x->script);
        x->script = nullptr;
        obj->life |= 1;
        return;
    }
    if (mp_word(0x180) == 1 || x->surface == 0) return;
    if (x->script != nullptr) {
        uint32_t hash;
        std::memcpy(&hash, script_bytes(x->script) + 0x98, 4);
        if (hash != 0x6000052) return;
    }
    if ((x->duration >> 2) < GameState.frame - obj->created) return;
    const uint32_t every = Rand_Rand(5) + 10;
    if (GameState.frame % every != 0) return;
    uint32_t* entities = nullptr;
    switch (x->surface) {
        case 5:
        case 6:
            entities = MetalHash_132;
            break;
        case 8:
        case 10:
        case 11:
            if (GameState.level_file != 0x7000008 || switch_channels[0x53] == 0) return;
            entities = StoneHash_133;
            break;
        default:
            return;
    }
    Debris_CreateEx(reinterpret_cast<_VECTOR*>(&obj->matrix.m[3][0]), &x->up, &x->side, x->radius + x->radius,
                    uint32_t(FRAME_RATE * 3.0f), 3, obj->cel, entities, 4);
}

// [decomp] 0x0017c518 Explode_Propagate
// O dano da explosão: uma esfera de raio radius (só objetos e salas pela máscara 0xc0, de um lado, sem
// empurrar) a partir de cel; cada objeto não apagado recebe damage x (1 - distância da explosão à
// esfera dele / radius), se for ao menos 0.0002, pelo tipo: 0 helicóptero (CopterList: o dano soma em
// +0x64 e +0x6c do helicóptero que tem o objeto entre as 9 peças em +0x30), 2 personagem (mensagem 9
// com o DroneImpact_tag: dano, raio, centro, quem causou), 3 jogador (Player_Hurt; morto no
// multijogador, o acerto 9999 de quem causou e MP_RegisterBulletHit), 5 projéteis explosivos (armas
// 0x2b, 0x34-0x37 e 0x3a explodem junto), 0x20 quebrável (o dano no acerto, Break_ApplyDamage), 0x21
// destrutível (dano de ao menos 1, sem a máscara 0x20: Destroy_Smash), 0x28 (soma em data + 0xf8),
// 0x35 multijogador, 0x36 torreta (dano e GT_Disable), 0x37 sensor, 0x38 monitor, 0x3d (o submarino
// em data), 0x4b (um acerto de quem causou com o dano).
// Divergência: Coll_AddHitToList sem acerto livre (o original escreveria no endereço 8) não é escrito.
void Explode_Propagate(_VECTOR* pos, float radius, cel_tag* cel, float damage, obj_tag* from) {
    if (cel == nullptr || radius < 0.0002f) return;
    ++g_explode_stats.propagated;
    _VECTOR center = *pos;
    HITDATA_tag* hits = nullptr;
    if (!Collide_SphereIntersect(&center, radius, cel, nullptr, nullptr, &hits, 0, 1, 0xc0, 4)) return;
    for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr || (o->life & 1) != 0) continue;
        const float d = Vec_Dist3D(&o->sphere, pos) - o->sphere.w;
        const float k = damage * (1.0f - std::fmax(d, 0.0f) / radius);
        if (k < 0.0002f) continue;
        ++g_explode_stats.hit_objects;
        switch (o->type) {
            case 0:
                for (uint8_t* c = reinterpret_cast<uint8_t*>(CopterList.first); c != nullptr;) {
                    for (int i = 0; i < 9; ++i) {
                        obj_tag* part;
                        std::memcpy(&part, c + 0x30 + i * 4, 4);
                        if (part != o) continue;
                        float* a = reinterpret_cast<float*>(c + 0x6c);
                        float* b = reinterpret_cast<float*>(c + 0x64);
                        *a += k;
                        *b += k;
                    }
                    std::memcpy(&c, c + 4, 4);
                }
                break;
            case 2: {
                DroneImpact_tag impact{};
                impact.damage = k;
                impact.radius = radius;
                impact.pos = *pos;
                impact.source = from;
                Drone_ExplosiveHit(o, &impact);
                break;
            }
            case 3: {
                const float health = Player_Hurt(o, k, pos);
                if (health <= 0.0f && from != nullptr && mp_word(0x180) != 0) {
                    if (Coll_AddHitToList(&o->hits, 9999.0f, nullptr, from) == nullptr) {
                        ++g_explode_guards.hit_heap_empty;
                    }
                    if (mp_word(0x180) != 0) {
                        const BLData* b = static_cast<const BLData*>(o->data);
                        MP_RegisterBulletHit(int16_t(b->player), from, b->health);
                    }
                }
                break;
            }
            case 5: {
                const uint16_t id = uint16_t(bullet_of(o)->weapon->id);
                if (id == 0x2b || (0x34 <= id && id < 0x38) || id == 0x3a) Bullet_handle_object_destruction(o, from);
                break;
            }
            case 0x20:
                set_hit_damage(h, k);
                Break_ApplyDamage(o, h);
                break;
            case 0x21:
                if (1.0f <= k && (static_cast<DESTROY_tag*>(o->data)->mask & 0x20) == 0) Destroy_Smash(o, from, 0x20);
                break;
            case 0x28: {
                float* v = reinterpret_cast<float*>(static_cast<uint8_t*>(o->data) + 0xf8);
                *v += k;
                break;
            }
            case 0x35:
                MP_ApplyDamage(o, k, from);
                break;
            case 0x36:
                GT_ApplyDamage(o, k);
                GT_Disable(o);
                break;
            case 0x37:
                Sensor_ApplyDamage(o, k);
                break;
            case 0x38:
                Monitor_ApplyDamage(o, k);
                break;
            case 0x3d: {
                obj_tag* sub = static_cast<obj_tag*>(o->data);
                if (sub != nullptr && sub->type == 0x4a) Sub_ApplyDammage(sub, k);
                break;
            }
            case 0x4b: {
                HITDATA_tag* n = Coll_AddHitToList(&o->hits, 9999.0f, nullptr, from);
                if (n == nullptr) {
                    ++g_explode_guards.hit_heap_empty;
                } else {
                    set_hit_damage(n, k);
                }
                break;
            }
            default:
                break;
        }
    }
    Collide_FreeHitList(&hits);
}

// [decomp] 0x0017cdc0 Explode_Delete
void Explode_Delete(obj_tag* obj) { Script_Free(explode_of(obj)->script); }

// [decomp] 0x0017cde0 Explode_CollisionHandler
// O dano se espalha uma vez, no primeiro tick de colisão depois da criação.
void Explode_CollisionHandler(obj_tag* obj) {
    EXPLODE_tag* x = explode_of(obj);
    if (x->propagated != 0) return;
    x->propagated = 1;
    Explode_Propagate(&obj->pos, x->radius, x->cel, x->damage, x->from);
}
