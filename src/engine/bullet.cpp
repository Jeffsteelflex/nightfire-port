// Projéteis e cápsulas de munição. Ver bullet.h.
//
// Divergências:
// - Onde o original soma lixo da pilha ao w de um vetor (a coluna w de vmulx.xyz não é escrita e o
//   vadd.xyzw seguinte soma o que havia no registrador), o port mantém o w anterior. Vale para dir
//   e pos em Bullet_update, Bullet_CollisionHandler e Bullet_homing, para a velocidade das
//   cápsulas e dos detritos e para os dígitos do satchel; nenhum desses w é lido.
// - Guardas (g_bullet_guards): sem glb_viewer[0] o zunido é pulado; um rastro com período 0 não é
//   atualizado (o original para com break 7); um material >= 23 no quique usa a última entrada da
//   tabela de quique (o original lê além de EffectInfo); um dígito do satchel fora de 0..9 não
//   ganha modelo (o original lê além de SatchelDigits).
// - A função de impacto da arma (weapon_definition_tag::on_hit, +0x108) é chamada como ponteiro
//   nativo: quem preenche weapon_data precisa guardar ponteiros do port, não endereços do PS2.
// - Bullet_init_casing_ex não copia obj->rot do atirador para a pilha: o original copia e ajusta
//   pela inclinação do jogador, mas sobrescreve antes de ler.

#include "engine/bullet.h"

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/door.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/lights.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/fmath.h"

obj_tag* ShellCasings[kShellCasings];
int16_t NextCasing = 0;
BulletGuards g_bullet_guards;

namespace {

// Modelos dos dígitos do satchel (SatchelDigits, 0x002bed90).
constexpr uint32_t kSatchelDigits[10] = {0x2000791, 0x2000792, 0x2000793, 0x2000794, 0x2000795,
                                         0x2000796, 0x2000797, 0x2000798, 0x2000799, 0x200079a};

constexpr float kTwoPi = 6.2831855f;
constexpr float kPi = 3.1415927f;

template <typename T>
T rd(const uint8_t* p, uint32_t offset) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    T v;
    std::memcpy(&v, p + offset, sizeof(T));
    return v;
}

uint32_t mp_word(uint32_t offset) { return rd<uint32_t>(MPSettings, offset); }

_VECTOR* matrix_pos(obj_tag* obj) { return reinterpret_cast<_VECTOR*>(&obj->matrix.m[3][0]); }
_VECTOR* plane_vec(HITDATA_tag* hit) { return reinterpret_cast<_VECTOR*>(&hit->plane); }

void play3d(uint32_t sound, _VECTOR* pos) { Sound_Play3D(sound, pos, 100.0f, -1.0f, -1.0f, 0, 0, 0); }

// Para minas e lasers: quantos projéteis da mesma arma e do mesmo atirador existem (contando este)
// e o mais antigo deles (menor obj_tag::created).
int count_same(const BU_tag* b, obj_tag** oldest) {
    int count = 0;
    *oldest = nullptr;
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 5) continue;
        const BU_tag* other = bullet_of(o);
        if (other->weapon != b->weapon || other->from != b->from) continue;
        ++count;
        if (*oldest == nullptr || o->created < (*oldest)->created) *oldest = o;
    }
    return count;
}

// Dígito do satchel: objeto do tipo 0x10 (Debris_Update o apaga no tick seguinte) no deslocamento
// "offset" do espaço da bala, girado por "rot".
void satchel_digit(obj_tag* obj, const MATRIX& rot, const _VECTOR& offset, int32_t digit) {
    const MATRIX& m = obj->matrix;
    _VECTOR p{obj->pos.x + m.m[0][0] * offset.x + m.m[1][0] * offset.y + m.m[2][0] * offset.z,
              obj->pos.y + m.m[0][1] * offset.x + m.m[1][1] * offset.y + m.m[2][1] * offset.z,
              obj->pos.z + m.m[0][2] * offset.x + m.m[1][2] * offset.y + m.m[2][2] * offset.z, obj->pos.w};
    obj_tag* d = control_create_object(0x40, &p, nullptr, nullptr);
    if (d == nullptr) return;
    MulMatrix0(&obj->matrix, &rot, &d->matrix);
    Mat_SetTrans(&p, &d->matrix);
    uint8_t* data = static_cast<uint8_t*>(d->data);
    d->type = 0x10;
    d->mode |= 4;
    data[0x32] = 0;
    std::memset(data + 0x30, 0, 2);
    if (digit >= 0 && digit < 10) {
        hashtable_set_object_to_entity_gfx(d, kSatchelDigits[digit]);
    } else {
        ++g_bullet_guards.digit_out_of_table;
    }
    d->flags &= 0xffffdfffu;
    control_link_object_to_cel(d, obj->cel);
}

}  // namespace

// [decomp] 0x0012c5a0 Bullet_init
// Cria o projétil em pos, voando ao longo de dir com a dispersão da arma. O primeiro parâmetro (o
// jogador) não é usado. Com dispersão, a perturbação é um vetor de comprimento até
// (dispersão x precisão) x 0,0014 em direção sorteada; como no original, as componentes y e z dele
// vão para z e y da direção. Precisão: 1, ou a do drone (dados + 0xc0); 0 para drones e para o
// jogador mirando com a flag 0x400000 da arma. O jogador soma à dispersão o acúmulo dos tiros
// seguidos. Devolve o objeto (ou nulo).
obj_tag* Bullet_init(int16_t, obj_tag* from, obj_tag* owner, weapon_definition_tag* weapon, _VECTOR* pos,
                     const _VECTOR* dir, uint16_t sound) {
    int32_t extra = 0;
    float accuracy = 1.0f;
    if (dir == nullptr) return nullptr;
    obj_tag* obj = control_create_object(0x110, pos, nullptr, nullptr);
    if (obj == nullptr) return nullptr;
    obj->type = 5;
    obj->unknown_fd = 2;
    obj->mode |= 2;
    obj->state = 0;
    obj->unknown_f6 = 0;
    obj->scale = 1.0f;
    BU_tag* b = bullet_of(obj);
    b->start = *pos;
    obj->hit_test = &b->hit_test;
    b->timer = 210.0f;
    b->hit_test.type = 0;
    b->occluder = nullptr;
    b->from = from;
    b->owner = owner;
    b->weapon = weapon;
    b->bounces = 0;
    b->dist = 0.0f;
    if (from != nullptr) from->mode |= 0x400;
    if ((b->weapon->flags2 & 0x10000) == 0) {
        b->trail = nullptr;
    } else {
        const uint16_t cls = b->weapon->klass;
        const int16_t width = b->weapon->trail_width;
        if (cls == 0x33 || cls == 0x6a || cls == 0x6e) {
            b->trail = Swoosh_Create(pos, pos, width, 0x20005cd, 0);
        } else {
            b->trail = Swoosh_Create(pos, pos, width, 0x2000918, 1);
        }
    }
    if (from != nullptr) {
        if (owner != nullptr && owner->type == 3) {
            // Jogador: a dispersão cresce com os tiros já dados na rajada (burst do modo de disparo
            // da arma em uso menos os que faltam, BLData::burst), até o tamanho do pente. O modo é
            // BLData::weapons[arma].mode e indexa burst sem limite, como no original (lidos crus).
            const BLData* bl = bloke_of(from);
            const ANIMOBJ_tag* anim = anim_of(from);
            const uint8_t mode = reinterpret_cast<const uint8_t*>(bl->weapons)[int32_t(anim->weapon) * 0xc + 7];
            const uint16_t burst = rd<uint16_t>(reinterpret_cast<const uint8_t*>(weapon->burst), mode * 2u);
            const int16_t shots = int16_t(burst - uint16_t(bl->burst));
            const int16_t n = shots < weapon->clip ? shots : weapon->clip;
            extra = int16_t(int32_t(float(n) * weapon->spread_step));
            if ((weapon->flags & 0x400000) != 0 && (anim->flags & 1) != 0) accuracy = 0.0f;
        } else if (from->type == 2) {
            accuracy = rd<float>(static_cast<const uint8_t*>(from->data), 0xc0);
            if ((weapon->flags & 0x400000) != 0) accuracy = 0.0f;
        }
    }
    const float spread = (weapon->spread + float(extra)) * accuracy;
    const float radius = Rand_FRand_MVar2(spread + spread, spread) * 0.0014f;
    const float azimuth = Rand_FRand(kTwoPi);
    const float elevation = Rand_FRand(kPi);
    _VECTOR jitter;
    Vec_Spherical_2_Cartesian(&jitter, radius, azimuth, elevation);
    b->dir = *dir;
    Vec_Normalise(&b->dir, &b->dir);
    b->dir.x += jitter.x;
    b->dir.z += jitter.y;
    b->dir.y += jitter.z;
    Vec_Normalise(&b->dir, &b->dir);
    Mat_Align2Dir(&obj->matrix, &b->dir, &CONSTANT_UP1, &CONSTANT_UP2);
    build_LinkToRoom(obj, 0, glb_world);
    float speed = weapon->speed;
    if ((b->weapon->flags2 & 0x200) != 0) speed *= 0.25f;
    b->speed = speed;
    if ((b->weapon->hit_flags & 0x10000) != 0) b->timer = 300.0f;
    const weapon_definition_tag* w = b->weapon;
    if (w->klass == 0x3b) {
        // Satchel: 5, 10, 15... segundos pela posição na família (o 0x6c tem 15).
        if (w->id == 0x6c) {
            b->timer = 900.0f;
        } else {
            b->timer = float(((int32_t(w->id) - int32_t(w->klass)) * 5 + 5) * 0x3c);
        }
        b->charge = 100;
    }
    if (w->klass == 0x69) b->timer = 5.0f;
    if (w->klass == 0x37) b->timer = (b->from != nullptr && b->from->type == 3) ? 7777.0f : 420.0f;
    if ((w->flags2 & 4) != 0) {
        // Míssil guiado: o jogador passa a pilotá-lo (subestado 10, câmera 0xc).
        obj_tag* p = b->from;
        if (p != nullptr && p->type == 3 && p->unknown_f6 != 10) {
            const int16_t prev = Player_ChangeSubState(p, 10);
            BLData* bl = bloke_of(b->from);
            bl->ride = obj;
            bl->prev_substate = prev;
            Player_SetCamMode(bl, 0xc);
            bl->ride_time = 0;
        }
    }
    if ((b->weapon->flags2 & 0x4000) != 0 && b->from != nullptr && b->from->type == 3) {
        BLData* bl = bloke_of(b->from);
        bl->move_locked = 1;
        bl->grapple_point = obj->pos;  // o ponto do gancho segue o projétil (a corda, Player_GrappleSetRope)
    }
    b->sound = sound == 0xffff ? 0 : Sound_Play3D(sound, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (weapon->ammo_type == 0xe) b->sound = Sound_Play3D(0x10e, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (weapon->klass == 0x45) b->sound = Sound_Play3D(0x5ef, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    return obj;
}

// [decomp] 0x0012cd98 Bullet_handle_object_destruction
// Um projétil que explode quando atingido (flag 0x4000 da arma, com impacto explosivo ou laser)
// explode e é apagado; o laser leva o feixe junto. O segundo parâmetro não é usado (a explosão é
// creditada a quem disparou o projétil atingido). Devolve 1 se explodiu.
uint32_t Bullet_handle_object_destruction(obj_tag* obj, obj_tag*) {
    if (obj == nullptr || obj->type != 5) return 0;
    BU_tag* b = bullet_of(obj);
    if ((b->weapon->flags & 0x4000) == 0 || (b->weapon->hit_flags & 0x80204) == 0) return 0;
    obj->life |= 1;
    const float radius = b->weapon->blast_radius;
    Explode_Create(obj, &obj->pos, &CONSTANT_UP1, radius, radius, 0x6000052, b->weapon->damage, 0xff, 0xff, 100,
                   b->from, 0);
    if ((b->weapon->hit_flags & 0x80000) != 0 && b->trail != nullptr) b->trail->life |= 1;
    return 1;
}

// [decomp] 0x0012ce80 Bullet_CollisionHandler
// Trata os acertos do tick em ordem de distância. Um acerto "atravessável" (flag 2 do acerto,
// objetos com a flag 0x60, material 0x10 sem a flag 0x800 da arma, materiais 0x40 e, ao acaso, 0x80)
// deixa a bala seguir para o próximo; o primeiro sólido encerra. Em cada um: efeito de impacto,
// explosões, grudar (estado 3, recuada 0,04 da superfície e apontando para dentro dela; personagens
// e o tipo 0x15 não seguram a bala), quicar (refletida, perdendo velocidade pelo material, até 20
// vezes) e ricochetear (até 3). O efeito de impacto acumula as flags entre os acertos.
void Bullet_CollisionHandler(obj_tag* obj) {
    uint16_t effect = 0;
    BU_tag* b = bullet_of(obj);
    HITDATA_tag* hit = obj->hits;
    if (obj->state == 3) return;
    b->resting = 0;
    while (hit != nullptr) {
        cel_tag* cel = hit->cel;
        if (cel == nullptr && hit->obj != nullptr) cel = hit->obj->cel;
        int8_t pass = int8_t((hit->flags >> 1) & 1);
        obj_tag* o = hit->obj;
        if (o != nullptr) {
            if ((o->flags & 0x60) != 0) pass = 1;
            if (o != b->owner && o->type == 0x28 && (b->weapon->hit_flags & 0x100) == 0) pass = 0;
        }
        if ((b->weapon->flags2 & 0x800) == 0 && (hit->surface & 0x3f) == 0x10) pass = 1;
        if (uint16_t(b->weapon->klass - 0x50) >= 2) {
            if ((hit->surface & 0x40) != 0) pass = int8_t(pass | 1);
            if ((hit->surface & 0x80) != 0) pass = int8_t(pass | (Rand_Rand(0x3f) & 1));
        }
        if (pass == 0) {
            if (o != nullptr && (b->weapon->hit_flags & 0x80) != 0) Control_InheritVelocity(obj, o, 10.0f);
            obj->state = 2;
            if (0.0f < b->weapon->damage) Bullet_handle_object_destruction(hit->obj, b->from);
        } else {
            obj->state = 2;
        }
        const weapon_definition_tag* w = b->weapon;
        if ((w->hit_flags & 2) != 0) effect = uint16_t(effect | 0x61);
        if ((w->flags2 & 0x100) != 0) effect = 0x10;
        if ((w->hit_flags & 0x1000) != 0) effect = 0x10;
        Effect_Create(hit, obj, effect, 1.0f);
        if ((b->weapon->hit_flags & 4) != 0 && pass == 0) {
            const float r = b->weapon->blast_radius;
            Explode_Create(obj, &hit->pos, plane_vec(hit), r, r, 0x6000052, b->weapon->damage, 0xff, 0xff, 100,
                           b->from, hit->surface & 0x3f);
        }
        if ((b->weapon->hit_flags & 0x100000) != 0 && pass == 0) {
            Explode_Create(obj, &hit->pos, plane_vec(hit), 1.0f, b->weapon->blast_radius, 0x60007c4, b->weapon->damage,
                           0xff, 0xff, 100, b->from, hit->surface & 0x3f);
        }
        if ((b->weapon->hit_flags & 0x200000) != 0 && pass == 0) {
            Explode_Create(obj, &hit->pos, plane_vec(hit), 1.0f, b->weapon->blast_radius, 0x60007c4, b->weapon->damage,
                           0xff, 0xff, 100, b->from, hit->surface & 0x3f);
        }
        int32_t bounce_off_character = 0;
        if ((b->weapon->hit_flags & 0x100) != 0 && pass == 0) {
            bool stick = true;
            obj_tag* t = hit->obj;
            if (t != nullptr) {
                if (uint8_t(t->type - 2) < 2 || t->type == 0x15) {
                    obj->state = 1;
                    bounce_off_character = 1;
                    stick = false;
                } else {
                    b->target = t;
                    Control_ObjectReferenced(t, 1);
                }
            }
            if (stick) {
                obj->state = 3;
                const _VECTOR in{-hit->plane.a, -hit->plane.b, -hit->plane.c, hit->plane.d};
                Mat_Align2Dir(&obj->matrix, &in, &CONSTANT_UP1, &CONSTANT_UP2);
                float back = 0.04f;
                const uint16_t cls = b->weapon->klass;
                obj_tag* oldest = nullptr;
                if ((b->weapon->hit_flags & 0x80000) != 0) {
                    // Laser de armadilha: arma em 2 s; no máximo 5 do mesmo atirador.
                    play3d(0x446, &obj->pos);
                    back = 0.002f;
                    b->timer = 120.0f;
                    obj->unknown_f6 = 0;
                    if (6 <= count_same(b, &oldest)) oldest->life |= 1;
                } else if (cls == 0x3b) {
                    play3d(0x4a7, &obj->pos);
                } else if (cls == 0x37) {
                    if (6 <= count_same(b, &oldest)) oldest->life |= 1;
                } else if (cls == 0x45) {
                    b->timer = 600.0f;
                    play3d(0x4a7, &obj->pos);
                }
                obj->pos.x = hit->pos.x + hit->plane.a * back;
                obj->pos.y = hit->pos.y + hit->plane.b * back;
                obj->pos.z = hit->pos.z + hit->plane.c * back;
                obj->pos.w = hit->pos.w;
            }
        }
        if (((b->weapon->hit_flags & 0x80) != 0 && pass == 0) || bounce_off_character != 0) {
            const uint32_t f70 = b->weapon->hit_flags;
            if ((f70 & 0x2000) != 0) {
                if (b->bounces == 0) play3d(0x15, &obj->pos);
            } else if ((f70 & 0x20000) != 0) {
                if (b->bounces == 0) play3d(0x13, &obj->pos);
            } else if ((b->bounces & 1) == 0 && b->bounces < 0xf) {
                play3d(0x51c, &obj->pos);
            }
            b->bounces = uint16_t(b->bounces + 1);
            if (b->bounces < 20) {
                const _VECTOR& push = b->hit_test.push;
                obj->pos.x += push.x * FRAME_RATE_DIV;
                obj->pos.y += push.y * FRAME_RATE_DIV;
                obj->pos.z += push.z * FRAME_RATE_DIV;
                if (0.0f < push.y) b->resting = 1;
                const float d = hit->plane.a * b->dir.x + hit->plane.b * b->dir.y + hit->plane.c * b->dir.z;
                const float twice = d + d;
                if (twice <= 0.0f) {
                    b->dir.x += hit->plane.a * -twice;
                    b->dir.y += hit->plane.b * -twice;
                    b->dir.z += hit->plane.c * -twice;
                    Vec_Normalise(&b->dir, &b->dir);
                    uint32_t s = hit->surface & 0x3f;
                    if (22 < s) {
                        ++g_bullet_guards.surface_out_of_table;
                        s = 22;
                    }
                    b->speed *= EffectInfo[s].bounce_keep;
                    if (b->speed < FRAME_RATE_MUL * 0.01f) {
                        Mat_Align2Dir(&obj->matrix, &b->dir, &CONSTANT_UP1, &CONSTANT_UP2);
                        b->speed = 0.0f;
                    }
                }
            } else {
                Mat_Align2Dir(&obj->matrix, &b->dir, &CONSTANT_UP1, &CONSTANT_UP2);
                b->speed = 0.0f;
            }
            obj->state = 1;
        }
        if ((b->weapon->hit_flags & 0x10) != 0) {
            uint16_t mask = 8;
            if (cel != nullptr && (cel->type & 0x400000) != 0) mask = 9;
            if (pass != 0) mask = 0;
            if (b->bounces < 3 && Effect_RicochetProb(hit->surface, mask) != 0) {
                // Ricochete: reflete com o mesmo fator de quique do material.
                const float d = hit->plane.a * b->dir.x + hit->plane.b * b->dir.y + hit->plane.c * b->dir.z;
                uint32_t s = hit->surface & 0x3f;
                if (22 < s) {
                    ++g_bullet_guards.surface_out_of_table;
                    s = 22;
                }
                const float k = -(EffectInfo[s].bounce_keep * d + d);
                b->dir.x += hit->plane.a * k;
                b->dir.y += hit->plane.b * k;
                b->dir.z += hit->plane.c * k;
                Vec_Normalise(&b->dir, &b->dir);
                obj->pos = hit->pos;
                Mat_Align2Dir(&obj->matrix, &b->dir, &CONSTANT_UP1, &CONSTANT_UP2);
                b->bounces = uint16_t(b->bounces + 1);
                obj->state = 1;
            }
        }
        if (b->weapon->on_hit != nullptr) {
            b->weapon->on_hit(obj, &hit->plane, &hit->pos);
            obj->life |= 1;
        }
        if (pass == 0) break;
        obj->state = 1;
        hit = hit->next;
    }
    if (b->occluder != nullptr) {
        b->occluder->pos = obj->pos;
        b->occluder->mode |= 0x20;
    }
    if (obj->state == 2) obj->flags |= 0x10;
}

// [decomp] 0x0012cba8 Bullet_trail_effect_smoke
// Fumaça atrás do projétil: baforadas pequenas a cada 4 ticks (só fora do multijogador); depois que
// o objeto muda de subestado (granada de fumaça soltando), nuvens grandes a cada 15.
void Bullet_trail_effect_smoke(obj_tag* obj) {
    _VECTOR vel{0.0f, 0.0f, 0.0f, 0.0f};
    _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
    const uint32_t phase = GameState.frame + uint8_t(obj->created);
    if (obj->unknown_f6 == 0) {
        if (mp_word(0x180) != 0 || (phase & 3) != 0) return;
        rot.z = Rand_FRand(kTwoPi);
        Gas_Init(obj->cel, &obj->pos, &rot, &vel, 0.1f, 0.5f, 0x200001f, 7, 0x404040ff, 0x40404001, 0x78);
        return;
    }
    if (phase % 0xf != 0) return;
    vel.x = Rand_FRand_MVar2(0.3f, 0.15f) + 0.6f;
    vel.z = Rand_FRand_MVar2(0.3f, 0.15f) + 0.3f;
    vel.y = Rand_FRand_MVar2(0.1f, 0.05f) + 0.5f;
    rot.z = Rand_FRand(kTwoPi);
    Gas_Init(obj->cel, &obj->pos, &rot, &vel, 0.2f, 5.0f, 0x200001f, 0xb, 0x404040ff, 0x404040ff, 0xff);
}

// [decomp] 0x0012d890 Bullet_DoTrails
// O que acompanha o voo: posição para a câmera do jogador, aceleração, fumaça e gás (acima da água,
// pela posição do começo do tick), o rastro (renovado a cada período), o zunido perto da câmera, o
// pavio (granada de luz, de fumaça ou explosão) e a contagem do satchel com os dígitos. O passo
// (último parâmetro) não é usado.
void Bullet_DoTrails(obj_tag* obj, BU_tag* b, _VECTOR* old_pos, float) {
    if (obj == nullptr || b == nullptr || b->weapon == nullptr) return;
    cel_tag* cel = obj->cel;
    if (cel == nullptr) return;
    if (b->from != nullptr && (b->weapon->flags2 & 0x4000) != 0) {
        std::memcpy(static_cast<uint8_t*>(b->from->data) + 0x140, &obj->pos, sizeof(_VECTOR));
    }
    const weapon_definition_tag* w = b->weapon;
    if ((w->flags2 & 0x200) != 0 && b->speed < w->speed) b->speed += w->speed * 0.02f;
    const uint32_t phase = GameState.frame + uint8_t(obj->created);
    if (old_pos != nullptr) {
        const bool above_water = cel->water_y <= old_pos->y;
        if ((b->weapon->flags2 & 2) != 0 && above_water) Bullet_trail_effect_smoke(obj);
        if ((b->weapon->flags2 & 0x20) != 0 && above_water && mp_word(0x180) == 0 && phase % 3 == 0) {
            _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
            rot.z = Rand_FRand(kTwoPi);
            Gas_Init(obj->cel, &obj->pos, &rot, nullptr, 0.1f, 0.4f, 0x2000039, 0, 0x7f7f7f7f, int32_t(0x80808000u),
                     0x40);
        }
        if ((b->weapon->flags2 & 0x80000) != 0 && above_water && mp_word(0x180) == 0 && (phase & 1) == 0) {
            _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
            rot.z = Rand_FRand(kTwoPi);
            Gas_Init(obj->cel, &obj->pos, &rot, nullptr, 0.05f, 0.1f, 0x2000039, 7, 0x7f7f7f7f, int32_t(0x80808000u),
                     0x20);
        }
    }
    w = b->weapon;
    if ((w->flags2 & 0x10000) != 0 && b->trail != nullptr) {
        const int16_t period = w->trail_period;
        auto* trail_point = static_cast<_VECTOR*>(b->trail->data);
        if (period == 0) {
            ++g_bullet_guards.zero_trail_period;
        } else if ((GameState.frame - obj->created) % uint32_t(int32_t(period)) == 0) {
            const uint16_t cls = w->klass;
            if (cls == 0x33 || cls == 0x6a || cls == 0x6e) {
                b->trail = Swoosh_Create(&obj->pos, trail_point, b->weapon->trail_width, 0x20005cd, 0);
            } else {
                b->trail = Swoosh_Create(&obj->pos, trail_point, w->trail_width, 0x2000481, 1);
            }
        } else {
            *trail_point = obj->pos;
        }
    }
    if ((b->weapon->flags2 & 0x40000) != 0 && mp_word(0x180) == 0 && b->from != glb_players[0]) {
        // Zunido: a bala passou a menos de 3 m da câmera neste tick (1 chance em 5).
        if (glb_viewer[0] == nullptr) {
            ++g_bullet_guards.no_viewer;
        } else {
            _VECTOR closest;
            vecutil_calculate_closest_point_on_line(&closest, &obj->pos, &obj->old_pos, &glb_viewer[0]->pos);
            if (Vec_SqDist3D(&closest, &glb_viewer[0]->pos) < 9.0f && Rand_Rand(5) == 0) play3d(0x1dc, &closest);
        }
    }
    if ((b->weapon->hit_flags & 0x200) != 0) {
        const uint16_t cls = b->weapon->klass;
        if (cls == 0x37) {
            if (b->timer != 7777.0f) b->timer -= FRAME_RATE_MUL;  // 7777: mina do jogador, sem pavio
        } else if (cls != 0x59 && cls != 0x5b) {
            b->timer -= FRAME_RATE_MUL;
        }
        if (b->timer < 0.0f) {
            const uint8_t life = obj->life;
            obj->state = 2;
            obj->life = uint8_t(life | 1);
            const uint32_t f70 = b->weapon->hit_flags;
            _VECTOR* pos = &obj->pos;
            if ((f70 & 0x2000) != 0) {
                // Granada de luz: ofusca os jogadores pela distância (5 a 90 m), pelo ângulo com o
                // olhar (30 a ~100 graus) e pela metade sem linha de visão; drones e torretas reagem.
                Gas_Init(obj->cel, pos, &obj->rot, nullptr, 0.5f, 1.67f, 0x200001f, 0xb, 0xffffff7fu,
                         int32_t(0x80808000u), 0x7f);
                Camera_Shake(pos, 2.0f);
                play3d(0x16, pos);
                Light_Create(pos, 0xff, 0xff, 0xff, 10.0f, 0, 10, 1, 5.0f, 0, 0, 0, 0xffff);
                P32<HITDATA_tag> hits = nullptr;
                Collide_SphereIntersect(matrix_pos(obj), 30.0f, obj->cel, obj, nullptr, &hits, 0, 1, 0xca, 4);
                for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
                    obj_tag* o = h->obj;
                    if (o == nullptr) continue;
                    if (o->type == 3) {
                        _VECTOR head;
                        Player_GetHeadPos(&head, o, nullptr);
                        float k = 1.0f - (Vec_Dist3D(&head, pos) - 5.0f) * 0.0117647061f;
                        if (k < 0.0f) {
                            k = 0.0f;
                        } else if (1.0f < k) {
                            k = 1.0f;
                        }
                        if (!(0.0f < k)) continue;
                        if (obj->pos.x == o->pos.x && obj->pos.z == o->pos.z) {
                            k *= 1.0f;
                        } else {
                            const float a = nf::fm::atan2(obj->pos.x - o->pos.x, obj->pos.z - o->pos.z);
                            float facing = 1.0f - (std::fabs(Vec_AngleDifference(a, o->rot.y)) - 0.5235988f) *
                                                      0.818511069f;
                            if (facing < 0.0f) {
                                facing = 0.0f;
                            } else if (1.0f < facing) {
                                facing = 1.0f;
                            }
                            k *= facing;
                        }
                        if (!(0.0f < k)) continue;
                        if (!Collide_LineOfSight(&head, pos, obj->cel, obj, o, 10)) k *= 0.5f;
                        Player_SetFlashBang(bloke_of(o), FRAME_RATE * (k * 10.0f), uint8_t(int32_t(k * 255.0f)));
                    } else if (o->type == 2) {
                        if (b->from == nullptr) continue;
                        if (mp_word(0x184) == 0 && b->from->type != 3) continue;
                        Drone_MessageObjVicinity(0x18, obj, 20.0f, 0, 0);
                    } else if (o->type == 0x36) {
                        GT_Disable(o);
                    }
                }
                Collide_FreeHitList(&hits);
                obj->life |= 1;
            } else if ((f70 & 0x20000) != 0) {
                // Granada de fumaça: no primeiro estouro vira nuvem (Occlude) por 20 s (40 s na classe
                // 0x69) e não é apagada; no segundo, some.
                if (obj->unknown_f6 == 0) {
                    obj->state = 1;
                    obj->life = uint8_t(life & 0xfe);
                    obj->unknown_f6 = 1;
                    b->timer = 1200.0f;
                    if (b->weapon->klass == 0x69) b->timer = 2400.0f;
                    b->occluder = Occlude_Create(pos, int16_t(int32_t(b->timer)), 1);
                    play3d(0x14, pos);
                }
            } else {
                const weapon_definition_tag* ew = b->weapon;
                Explode_Create(obj, pos, &CONSTANT_UP1, ew->blast_radius, ew->blast_radius, 0x6000052, ew->damage, 200,
                               200, 0, b->from, 0);
            }
        }
    }
    if (b->weapon->klass != 0x3b) return;
    // Satchel: bipes cada vez mais rápidos e dois dígitos com os segundos que faltam. Sem carga
    // (desarmado por tiros das classes 0x4e/0x4f), a contagem recomeça de 1 a 91 s ao acaso.
    const int16_t seconds = int16_t(int32_t(b->timer * 0.0166666675f));
    const int16_t quarters = int16_t(int32_t(b->timer * 0.0666666701f));
    if (b->charge < 1) {
        b->timer = float(Rand_Rand(0x1518) + 0x3c);
    } else {
        if (int32_t(quarters) != int32_t(b->bounces)) {
            b->bounces = uint16_t(quarters);
            const uint16_t q = uint16_t(quarters);
            if ((q & 3) == 0 || ((q & 1) == 0 && seconds < 10) || seconds < 5) play3d(0x447, &obj->pos);
        }
        for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
            obj_tag* o = h->obj;
            if (o == nullptr || o->type != 5) continue;
            const uint16_t cls = bullet_of(o)->weapon->klass;
            int16_t amount = 0;
            if (cls == 0x4e) amount = int16_t(int32_t(FRAME_RATE_MUL));
            if (cls == 0x4f) amount = int16_t(int32_t(FRAME_RATE_MUL * 10.0f));
            if (amount == 0) continue;
            b->charge = int16_t(b->charge - amount);
            Emitter_Create(0xc00000a, &h->pos, plane_vec(h), 0.0f, 0, 0, 0, 0.0f);
            if (0 < b->charge) continue;
            play3d(0x5dd, &obj->pos);
        }
    }
    MATRIX rot;
    const _VECTOR euler{1.5707964f, 0.0f, kPi, 0.0f};
    RotMatrix(&euler, &rot);
    const _VECTOR tens{0.0936933011f, -0.00228693010f, -0.122735999f, 0.0f};
    const _VECTOR units{0.0321801007f, -0.00228693010f, -0.122735999f, 0.0f};
    satchel_digit(obj, rot, tens, seconds / 10);
    satchel_digit(obj, rot, units, seconds % 10);
}

// [decomp] 0x0012e660 Bullet_Delete
// Ao apagar: para o som, devolve o controle de um míssil guiado ao jogador, solta o gancho (e,
// parado num ponto de gancho, tipo 0x51, prende o jogador nele) e apaga o feixe do laser.
void Bullet_Delete(obj_tag* obj) {
    BU_tag* b = bullet_of(obj);
    BLData* bl = nullptr;
    if (b->sound != 0) {
        Sound_Stop(b->sound);
        b->sound = 0;
    }
    if (b->from != nullptr && b->from->type == 3) bl = bloke_of(b->from);
    if ((b->weapon->flags2 & 4) != 0 && bl != nullptr) {
        bl->ride = nullptr;
        bl->ride_time = 0xf;
    }
    if ((b->weapon->hit_flags & 0x40) != 0 && bl != nullptr) {
        bl->move_locked = 0;
        if (obj->state == 2) {
            if (obj->hits == nullptr) return;
            obj_tag* point = obj->hits->obj;
            if (point == nullptr || point->type != 0x51) return;
            play3d(0x28d, matrix_pos(obj));
            Player_SetGrapplePoint(bl, matrix_pos(obj), point);
        }
    }
    if ((b->weapon->hit_flags & 0x80000) != 0 && b->trail != nullptr) b->trail->life |= 1;
}

// [decomp] 0x0012e7b0 Bullet_update
// Um tick do projétil. Estado 0: primeiro passo (0,5 a 1 x speed) e o modelo; 1: voando (alcance,
// giro, míssil guiado pelo stick do jogador, gravidade, luz); 2 e 4: apagar; 3: grudado (segue o
// objeto onde grudou; o laser de armadilha arma e projeta o feixe a cada tick). Fora do estado 3
// anda passo x FRAME_RATE_MUL ao longo de dir, cortando o último passo no alcance, e nos estados 0
// e 1 monta o teste de colisão do trecho: um raio de pelo menos 1 m à frente da posição antes de
// andar ou, se o passo for menor que meio raio da bala (mínimo 0,25), uma esfera.
// Peculiaridade mantida: com gravidade, o passo do tick usa a velocidade de antes da gravidade e a
// direção de depois.
void Bullet_update(obj_tag* obj) {
    _VECTOR old_pos = obj->pos;
    BU_tag* b = bullet_of(obj);
    if (b->sound != 0) {
        if (Sound_IsFinnished(b->sound) != 0) {
            b->sound = 0;
        } else {
            Sound_SetPosition(b->sound, matrix_pos(obj));
        }
    }
    b->hit_test.type = 0;
    if ((obj->life & 1) != 0) return;
    float step = 0.0f;
    switch (obj->state) {
        default: {
            step = (Rand_FRand(0.5f) + 0.5f) * b->speed;
            ++obj->state;
            const uint32_t f6c = b->weapon->flags2;
            if ((f6c & 0x1010) != 0) {
                // Traçante: sempre com 0x1000, e só nos tiros dos drones com 0x10.
                if ((b->from != nullptr && b->from->type == 2) || (f6c & 0x1000) != 0) {
                    View_SetDrawInAllViews(obj);
                    hashtable_set_object_to_entity_gfx(obj, 0x2000123);
                    obj->mode |= 1;
                    obj->scale = b->speed;
                }
                break;
            }
            obj->scale = 1.0f;
            const uint32_t entity = b->weapon->projectile;
            if (entity != 0) {
                hashtable_set_object_to_entity_gfx(obj, entity);
                if ((b->weapon->flags2 & 0x100) != 0) {
                    obj->mode |= 1;
                    obj->scale = b->speed;
                }
            }
            if (obj->glist == nullptr) break;
            View_SetDrawInAllViews(obj);
            obj->flags &= 0xffffdfefu;
            break;
        }
        case 1: {
            step = b->speed;
            if (b->weapon->range < b->dist) {
                obj->state = 4;
                obj->life |= 1;
                return;
            }
            if ((b->weapon->flags2 & 0x8000) != 0) {
                // Gira em torno do eixo de voo, mais devagar a cada quique.
                const _VECTOR spin{0.0f, 0.0f, FRAME_RATE_MUL * 0.1f / float(int32_t(b->bounces) + 1), 0.0f};
                MATRIX r, copy;
                RotMatrix(&spin, &r);
                Mat_Copy(&obj->matrix, &copy);
                MulMatrix0(&copy, &r, &obj->matrix);
            }
            if ((b->weapon->flags2 & 4) != 0 && b->from != nullptr && b->from->type == 3 &&
                b->from->unknown_f6 == 10) {
                // Míssil guiado: o stick vira e inclina (sem passar da vertical), com rolagem que
                // acompanha a curva e decai; encostar em algo detona.
                BLData* bl = bloke_of(b->from);
                MATRIX* m = &obj->matrix;
                const float yaw = Input_Actionf(bl->player, 0, 1) * 0.0245436933f * FRAME_RATE_MUL;
                float pitch = -Input_Actionf(bl->player, 5, 1) * 0.0245436933f * FRAME_RATE_MUL;
                _VECTOR fwd;
                Mat_GetDir(&fwd, m);
                if (fwd.y < -0.995f && 0.0f < pitch) {
                    pitch = 0.0f;
                } else if (0.995f < fwd.y && pitch < 0.0f) {
                    pitch = 0.0f;
                }
                _VECTOR trans;
                Mat_GetTrans(&trans, m);
                _VECTOR angles;
                vecutil_cartesian_to_spherical_acc(&angles, fwd.x, fwd.y, fwd.z);
                angles.y = angles.y - yaw;
                angles.z = 0.0f;
                angles.x = pitch - angles.x;
                MATRIX turn, roll;
                RotMatrixZYX(&angles, &turn);
                angles.z = obj->rot.z + yaw;
                angles.x = 0.0f;
                angles.y = 0.0f;
                RotMatrix(&angles, &roll);
                MulMatrix0(&turn, &roll, m);
                Mat_SetTrans(&trans, m);
                obj->rot.z = angles.z * 0.965f;
                Mat_GetDir(&b->dir, m);
                P32<HITDATA_tag> hits = nullptr;
                obj_tag* ignore = 1.0f < b->dist ? nullptr : b->from;
                Collide_SphereIntersect(matrix_pos(obj), 0.1f, obj->cel, obj, ignore, &hits, 0, 0, 0xa0a, 4);
                const bool touched = hits != nullptr;
                Collide_FreeHitList(&hits);
                if (touched) Bullet_handle_object_destruction(obj, b->from);
            }
            if ((b->weapon->flags2 & 8) != 0 && b->resting == 0) {
                // Gravidade: velocidade = dir x speed + g dt^2 (por tick), separada de novo em
                // direção e módulo.
                const float g = FRAME_RATE_DIV * REC_FRAME_RATE * REC_FRAME_RATE;
                b->dir.x = b->dir.x * b->speed + WldGravity.x * g;
                b->dir.y = b->dir.y * b->speed + WldGravity.y * g;
                b->dir.z = b->dir.z * b->speed + WldGravity.z * g;
                b->speed = Vec_NormaliseLen(&b->dir, &b->dir);
            }
            const weapon_definition_tag* w = b->weapon;
            if ((w->flags2 & 0x2000) != 0) {
                Light_Create(&obj->pos, w->light_r, w->light_g, w->light_b, 4.0f, 0, 0, 1, 2.0f, 0, 0, 0, 0xffff);
            }
            break;
        }
        case 2:
        case 4:
            obj->life |= 1;
            return;
        case 3: {
            if (b->sound != 0) {
                Sound_Stop(b->sound);
                b->sound = 0;
            }
            obj_tag* t = b->target;
            if (t != nullptr) {
                if ((t->life & 1) != 0) {
                    // O objeto onde estava grudado sumiu: o laser explode, o resto volta a cair.
                    if ((b->weapon->hit_flags & 0x80000) != 0) {
                        Bullet_handle_object_destruction(obj, b->from);
                        b->target = nullptr;
                        return;
                    }
                    obj->state = 1;
                    b->target = nullptr;
                    return;
                }
                Control_InheritVelocity(obj, t, 30.5f);
            }
            step = 0.0f;
            b->speed = 0.0f;
            Vec_Zero(&b->dir);
            if ((b->weapon->hit_flags & 0x80000) != 0) {
                if (step < b->timer) {
                    b->timer -= FRAME_RATE_MUL;
                    break;
                }
                // Laser de armadilha armado: um feixe de até 200 m para trás do eixo da bala (para
                // fora da parede); um personagem ou carro no caminho detona.
                _VECTOR* pos = &obj->pos;
                if (b->trail == nullptr) {
                    obj_tag* beam = control_create_object(0, pos, nullptr, nullptr);
                    b->trail = beam;
                    hashtable_set_object_to_entity_gfx(beam, 0x2000133);
                    b->trail->type = 1;
                    b->trail->flags |= 0x20;
                    b->trail->owner = obj;
                    b->trail->mode |= 1;
                    play3d(0x27c, pos);
                }
                _VECTOR end;
                Mat_GetDir(&end, &obj->matrix);
                end.x = obj->pos.x + end.x * -200.0f;
                end.y = obj->pos.y + end.y * -200.0f;
                end.z = obj->pos.z + end.z * -200.0f;
                P32<HITDATA_tag> hits = nullptr;
                Collide_RayIntersect(pos, &end, obj->cel, obj, nullptr, &hits, 0, 0x20a, 0);
                if (hits != nullptr) {
                    end = hits->pos;
                    obj_tag* o = hits->obj;
                    if (o != nullptr && (uint8_t(o->type - 2) < 2 || o->type == 0x28)) {
                        Bullet_handle_object_destruction(obj, b->from);
                    }
                }
                Collide_FreeHitList(&hits);
                PositionBeam(b->trail, pos, &end);
            }
            if (b->weapon->klass == 0x45) {
                b->timer -= FRAME_RATE_MUL;
                if (b->timer <= 0.0f) obj->life |= 1;
            }
            break;
        }
    }
    if (obj->state == 3) {
        Bullet_DoTrails(obj, b, &old_pos, step);
        obj->mode |= 0x20;
        return;
    }
    step *= FRAME_RATE_MUL;
    const weapon_definition_tag* w = b->weapon;
    b->dist += step;
    float move = step;
    if (w->range < b->dist) move = step - (b->dist - w->range);
    if (int16_t(obj->state) < 2) {
        HITTEST_tag& t = b->hit_test;
        t.mask = 0;
        t.flags = (w->flags2 & 0x800) == 0 ? 0x20 : 0;
        std::memcpy(&t.unknown_90, &w->damage, 4);  // o dano vai para o acerto do alvo
        if (0.0f < w->blast_radius) {
            switch (w->id) {
                case 0x2a:
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f: {
                    const float one = 1.0f;
                    std::memcpy(&t.unknown_90, &one, 4);
                    break;
                }
                case 0x2b:
                case 0x34:
                case 0x35:
                case 0x36:
                case 0x37:
                case 0x3a:
                case 0x3b:
                case 0x3c:
                case 0x3d:
                case 0x3e:
                case 0x3f:
                case 0x40:
                    t.unknown_90 = 0;
                    break;
                default:
                    break;
            }
        }
        t.ignore_b = nullptr;
        t.ignore_a = b->owner;
        t.cel = obj->cel;
        const uint16_t id = b->weapon->id;
        float radius = obj->sphere.w > 0.25f ? obj->sphere.w : 0.25f;
        if (uint16_t(id - 0x2c) < 6 || id == 0x62 || id == 0x6d) radius = 0.0f;
        if (move < radius * 0.5f) {
            t.radius = radius;
            t.type = 0x101;
            t.pos = obj->pos;
        } else {
            t.type = 0x201;
            t.dir = b->dir;
            const float len = move > 1.0f ? move : 1.0f;
            t.radius = len;
            t.start = obj->pos;
            t.end = obj->pos;
            t.end.x = obj->pos.x + b->dir.x * len;
            t.end.y = obj->pos.y + b->dir.y * len;
            t.end.z = obj->pos.z + b->dir.z * len;
        }
    } else {
        b->hit_test.type = 0;
    }
    obj->pos.x += move * b->dir.x;
    obj->pos.y += move * b->dir.y;
    obj->mode |= 0x20;
    obj->pos.z += move * b->dir.z;
    Bullet_DoTrails(obj, b, &old_pos, step);
    if ((b->weapon->flags2 & 0x20000) != 0) Bullet_homing(b, obj);
    obj->mode |= 0x20;
}

// [decomp] 0x0012f248 Bullet_heatseekable
// Alvos do teleguiado: drones vivos e ativos (não no estado 1), helicópteros, jogadores com vida e
// carros, desde que vistos recentemente (obj_tag::unknown_fc).
bool Bullet_heatseekable(obj_tag* obj) {
    switch (obj->type) {
        case 2:
            if (obj->unknown_fc == 0) return false;
            if (rd<float>(static_cast<const uint8_t*>(obj->data), 0xac) <= 0.0f) return false;
            return obj->state != 1;
        case 0:
            if (obj->unknown_fc == 0) return false;
            for (LLNODE_tag* c = CopterList.first; c != nullptr; c = c->next) {
                if (Copter_GetBody(c) == obj) return true;
            }
            return false;
        case 3:
            return !(bloke_of(obj)->health <= 0.0f);
        case 0x28:
            return obj->unknown_fc != 0;
        default:
            return false;
    }
}

// [decomp] 0x0012f358 Bullet_homing
// Escolhe (ou mantém) o alvo mais próximo a menos de 1000 m, dentro de um cone de ~49 graus à frente
// e com linha de visão, e vira a direção para ele: no máximo 0,01 por componente por tick, antes de
// renormalizar.
void Bullet_homing(BU_tag* b, obj_tag* obj) {
    const _VECTOR* goal = nullptr;
    if (b->target != nullptr && (b->target->life & 1) != 0) b->target = nullptr;
    if (b->target != nullptr) {
        goal = &b->target->pos;
    } else {
        float best = 1000.0f;
        b->target = nullptr;
        for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (!Bullet_heatseekable(o) || o == b->from) continue;
            const float d = Vec_Dist3D(&o->pos, &obj->pos);
            if (!(d < best)) continue;
            _VECTOR fwd;
            Mat_GetDir(&fwd, &obj->matrix);
            _VECTOR to{o->pos.x - obj->pos.x, o->pos.y - obj->pos.y, o->pos.z - obj->pos.z, o->pos.w - obj->pos.w};
            Vec_Normalise(&to, &to);
            if (!(0.66f < fwd.x * to.x + fwd.y * to.y + fwd.z * to.z)) continue;
            if (Collide_LineOfSight(&obj->pos, &o->pos, obj->cel, obj, o, 8)) {
                b->target = o;
                best = d;
            }
        }
        if (b->target != nullptr) goal = &b->target->pos;
    }
    if (goal == nullptr || obj->unknown_f6 != 0) return;
    Mat_GetDir(&b->dir, &obj->matrix);
    _VECTOR to{goal->x - obj->pos.x, goal->y - obj->pos.y, goal->z - obj->pos.z, goal->w - obj->pos.w};
    Vec_Normalise(&to, &to);
    const auto clamp = [](float v) { return v < -0.01f ? -0.01f : (0.01f < v ? 0.01f : v); };
    b->dir.x += clamp((to.x - b->dir.x) * 0.0333f);
    b->dir.y += clamp((to.y - b->dir.y) * 0.0333f);
    b->dir.z += clamp((to.z - b->dir.z) * 0.0333f);
    Vec_Normalise(&b->dir, &b->dir);
    Mat_Align2Dir(&obj->matrix, &b->dir, &CONSTANT_UP1, &CONSTANT_UP2);
}

// [decomp] 0x0012f678 Bullet_init_casing_ex
// A cápsula sai: do osso 1 (ou 5, em armas com a flag 0x40000 e BLData + 0x958 zerado) da arma na
// mão do jogador, ou do osso do cano de quem atira (mais 2, se existir), com a velocidade de saída
// da arma no espaço do osso, um pouco de acaso e a velocidade do atirador; gira ao acaso e cai até
// um pouco abaixo dos pés dele.
void Bullet_init_casing_ex(obj_tag* obj) {
    if (obj == nullptr) return;
    CASING_tag* c = static_cast<CASING_tag*>(obj->data);
    obj_tag* shooter = c->shooter;
    if (shooter == nullptr) {
        obj->state = 2;
        View_SetDrawInNoViews(obj);
        return;
    }
    const weapon_definition_tag* w = weapon_def(anim_of(shooter)->weapon);
    if (rd<uint32_t>(ammo_data, uint32_t(w->ammo_type) * kAmmoStride + 4) == 0) {
        obj->state = 2;
        View_SetDrawInNoViews(obj);
        return;
    }
    _VECTOR bone_pos;
    MATRIX bone;
    if (shooter->type == 3) {
        const BLData* bl = bloke_of(shooter);
        const uint32_t which = ((w->flags & 0x40000) != 0 && bl->alt_hand == 0) ? 5 : 1;
        AnimGetBoneWorldTrans(bl->gun, which, 0, &bone_pos, &bone);
    } else {
        const int32_t muzzle = AnimDatumGetWeaponInfo(shooter, nullptr, nullptr, nullptr);
        if (muzzle < 0) return;
        if (AnimDatumGetIndex(shooter, muzzle + 2) >= 0) {
            AnimGetBoneWorldTrans(shooter, uint32_t(muzzle), 2, &bone_pos, &bone);
        } else {
            AnimGetBoneWorldTrans(shooter, uint32_t(muzzle), 1, &bone_pos, &bone);
        }
    }
    _VECTOR v{w->casing_vel[0], -w->casing_vel[1], w->casing_vel[2], 0.0f};
    ApplyMatrixLVI(&bone, &v);
    c->vel = v;
    c->spin.x = Rand_FRand_MVar2(0.1f, 0.05f);
    c->spin.y = Rand_FRand_MVar2(0.1f, 0.05f);
    c->spin.z = Rand_FRand_MVar2(0.1f, 0.05f);
    c->vel.x += Rand_FRand_MVar2(0.01f, 0.005f);
    c->vel.y += Rand_FRand_MVar2(0.01f, 0.005f);
    c->vel.z += Rand_FRand_MVar2(0.01f, 0.005f);
    c->vel.x *= FRAME_RATE_MUL;
    c->vel.y *= FRAME_RATE_MUL;
    c->vel.z *= FRAME_RATE_MUL;
    c->spin.x *= FRAME_RATE_MUL;
    c->spin.y *= FRAME_RATE_MUL;
    c->spin.z *= FRAME_RATE_MUL;
    c->vel.x += shooter->pos.x - shooter->old_pos.x;
    c->vel.y += shooter->pos.y - shooter->old_pos.y;
    c->vel.z += shooter->pos.z - shooter->old_pos.z;
    Vec_Copy2(&bone_pos, &obj->pos, &obj->old_pos);
    _VECTOR fwd{0.0f, 0.0f, 1.0f, 0.0f};
    ApplyMatrixLVI(&bone, &fwd);
    vecutil_cartesian_to_spherical_acc(&fwd, fwd.x, fwd.y, fwd.z);
    obj->rot.y = fwd.y;
    obj->rot.x = -fwd.x;
    obj->state = 0;
    obj->rot.z = 0.0f;
    control_unlink_object(obj);
    control_link_object_to_cel(obj, shooter->cel);
    const uint32_t flags = obj->flags;
    obj->flags = flags | 0x900;
    if (shooter->type == 3) obj->flags = flags | 0x2900;
    c->timer = 0.0f;
    c->floor_y = shooter->pos.y - (Rand_FRand(0.5f) + 1.0f);
    c->surface = shooter->sphere_src == nullptr ? 0 : uint16_t(anim_of(shooter)->floor.surface & 0x3f);
    View_SetDrawInAllViews(obj);
}

// [decomp] 0x0012faa0 Bullet_casing_update
// Estado 3: espera o atraso e sai (Bullet_init_casing_ex); 2: guardada. Senão voa: anda, gira, cai
// (com um décimo da gravidade no estado 1), toca o som do material uma vez ao passar do chão e some
// depois de 4 s.
void Bullet_casing_update(obj_tag* obj) {
    CASING_tag* c = static_cast<CASING_tag*>(obj->data);
    if (obj->state == 3) {
        c->timer -= FRAME_RATE_MUL;
        if (c->timer <= 0.0f) Bullet_init_casing_ex(obj);
        return;
    }
    if (obj->state == 2) return;
    _VECTOR next = obj->pos;
    next.x += c->vel.x;
    next.y += c->vel.y;
    next.z += c->vel.z;
    c->timer += FRAME_RATE_MUL;
    if (5.0f < c->timer) obj->flags &= 0xffffdfffu;
    if (obj->pos.y <= c->floor_y) {
        c->floor_y = -1000000.0f;
        uint32_t sound = 0;
        if (c->entity == 0x2000548 && c->surface != 0xd && c->surface != 0xe) {
            sound = 0x1fd;
        } else {
            switch (c->surface) {
                case 1:
                case 0x14:
                    sound = 0x1ff;
                    break;
                case 3:
                case 4:
                    sound = 0x203;
                    break;
                case 5:
                    sound = 0x204;
                    break;
                case 6:
                    sound = 0x200;
                    break;
                case 10:
                case 0x15:
                    sound = 0x1fe;
                    break;
                case 0xd:
                case 0xe:
                    sound = 0x202;
                    break;
                case 0xf:
                case 0x10:
                case 0x11:
                case 0x12:
                    sound = 0x201;
                    break;
                default:
                    break;
            }
        }
        if (sound != 0) play3d(sound, &obj->pos);
    }
    if (240.0f < c->timer) {
        obj->state = 2;
        View_SetDrawInNoViews(obj);
        c->timer = 32767.0f;
    }
    obj->pos = next;
    obj->rot.x += c->spin.x;
    obj->rot.y += c->spin.y;
    obj->rot.z += c->spin.z;
    obj->rot.w += c->spin.w;
    const float g = obj->state == 1 ? REC_FRAME_RATE * REC_FRAME_RATE * 0.1f : REC_FRAME_RATE * REC_FRAME_RATE;
    c->vel.x += WldGravity.x * g;
    c->vel.y += WldGravity.y * g;
    c->vel.z += WldGravity.z * g;
    obj->mode |= 0x20;
}

// [decomp] 0x0012fd48 Bullet_InitCasingLists
// As 32 cápsulas reaproveitadas, criadas a cada carga de fase (sem tipo até a primeira saída).
void Bullet_InitCasingLists() {
    for (int i = 0; i < kShellCasings; ++i) {
        ShellCasings[i] = control_create_object(0x40, nullptr, nullptr, nullptr);
        View_SetDrawInNoViews(ShellCasings[i]);
        ShellCasings[i]->state = 2;
    }
    NextCasing = 0;
}

// [decomp] 0x0012fde0 Bullet_init_casing
// Pega a próxima das 32 cápsulas (em círculo) para a arma de quem atirou e a põe para sair depois
// do atraso da arma (estado 3, tipo 0xd). Armas cuja munição não tem cápsula não fazem nada.
void Bullet_init_casing(obj_tag* obj) {
    if (obj == nullptr) return;
    const weapon_definition_tag* w = weapon_def(anim_of(obj)->weapon);
    const uint32_t entity = rd<uint32_t>(ammo_data, uint32_t(w->ammo_type) * kAmmoStride + 4);
    if (entity == 0) return;
    obj_tag* casing = ShellCasings[NextCasing];
    NextCasing = int16_t((NextCasing + 1) % kShellCasings);
    if (casing == nullptr) return;
    CASING_tag* c = static_cast<CASING_tag*>(casing->data);
    casing->type = 0xd;
    casing->state = 3;
    c->shooter = obj;
    c->entity = entity;
    c->timer = float(w->casing_delay);
    hashtable_set_object_to_entity_gfx(casing, entity);
    View_SetDrawInNoViews(casing);
}

// [decomp] 0x001aac00 Debris_Update
// Detritos (tipo 0x10, dados: velocidade em 0x10, giro em 0x20, vida em ticks em 0x30, quanto a
// transparência cai por tick em 0x32): vida <= 0 ou transparência esgotada apagam; senão anda, cai
// (estado 0) e gira.
void Debris_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    uint8_t* d = static_cast<uint8_t*>(obj->data);
    int16_t life = rd<int16_t>(d, 0x30);
    if (life < 1) {
        obj->life |= 1;
        return;
    }
    const int32_t alpha = int32_t(obj->unknown_103[3]) - int32_t(d[0x32]);
    if (alpha < 1) {
        obj->life |= 1;
        return;
    }
    obj->unknown_103[3] = uint8_t(alpha);
    if (0 < life) {
        life = int16_t(life - 1);
        std::memcpy(d + 0x30, &life, 2);
    }
    _VECTOR vel = rd<_VECTOR>(d, 0x10);
    const _VECTOR spin = rd<_VECTOR>(d, 0x20);
    obj->pos.x += vel.x;
    obj->pos.y += vel.y;
    obj->pos.z += vel.z;
    obj->pos.w += vel.w;
    if (obj->state == 0) {
        const float g = REC_FRAME_RATE * REC_FRAME_RATE;
        vel.x += WldGravity.x * g;
        vel.y += WldGravity.y * g;
        vel.z += WldGravity.z * g;
        std::memcpy(d + 0x10, &vel, sizeof(vel));
    }
    obj->rot.x += spin.x;
    obj->rot.y += spin.y;
    obj->rot.z += spin.z;
    obj->rot.w += spin.w;
    obj->mode |= 0x20;
}

// [decomp] 0x001cf290 Effect_RicochetProb
// Materiais acima de 22 contam como o 22.
int8_t Effect_RicochetProb(uint8_t surface, uint16_t mask) {
    uint32_t s = surface & 0x3f;
    if (0x16 < s) s = 0x16;
    if (EffectInfo[s].ricochet == 0 || mask == 0) return 0;
    return int8_t(Rand_Rand(EffectInfo[s].ricochet) & mask);
}

void bullet_debug_power_on() {
    std::memset(ShellCasings, 0, sizeof(ShellCasings));
    NextCasing = 0;
    g_bullet_guards = {};
}
