// Corpos rígidos (módulo RB). Ver rigid_body.h.

#include "engine/rigid_body.h"

#include <cmath>
#include <cstring>

#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"

RigidBodyStats g_rigid_body_stats;

namespace {
RIGIDBODY* rb_of(obj_tag* obj) { return static_cast<RIGIDBODY*>(obj->data); }
_VECTOR& row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
const _VECTOR& row3(const MATRIX& m) { return *reinterpret_cast<const _VECTOR*>(&m.m[3][0]); }

// O quatérnio do corpo (em 0x64, sem alinhamento de 16) numa cópia alinhada, e de volta.
quaternion_tag get_quat(const RIGIDBODY* rb) {
    quaternion_tag q;
    std::memcpy(&q, rb->quat, sizeof(q));
    return q;
}
void set_quat(RIGIDBODY* rb, const quaternion_tag& q) { std::memcpy(rb->quat, &q, sizeof(q)); }

// a.xyz += b.xyz * k (vmulx.xyz + vadd: o w do original levava lixo da pilha; aqui não muda).
void madd3(_VECTOR* a, const _VECTOR& b, float k) {
    a->x = a->x + b.x * k;
    a->y = a->y + b.y * k;
    a->z = a->z + b.z * k;
}
void add3(_VECTOR* a, const _VECTOR& b) {
    a->x = a->x + b.x;
    a->y = a->y + b.y;
    a->z = a->z + b.z;
}
// r x f na ordem da VU0 (vopmula.xyz ACC, r, f; vopmsub.xyz out, f, r).
_VECTOR cross_vu(const _VECTOR& r, const _VECTOR& f) {
    return {r.y * f.z - f.y * r.z, r.z * f.x - f.z * r.x, r.x * f.y - f.x * r.y, 0.0f};
}
_VECTOR sub3(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, 0.0f}; }
float dot_vu(const _VECTOR& a, const _VECTOR& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
float hit_damage(const HITDATA_tag* h) {
    float d;
    std::memcpy(&d, &h->unknown_08, 4);
    return d;
}
}  // namespace

// [decomp] 0x001d6bf0 RB_Create
// O objeto 0x1a com o RIGIDBODY: o teste de colisão dele é o próprio obj_tag::hit_test; inércia de um cubo com as
// medidas da caixa do modelo e a massa do parâmetro 0 (3 sem ela; o valor sem sinal vira float); a gravidade só no tipo
// 0; a orientação na identidade (RB_SetState). O raio: o da esfera do modelo (tipos 0 e 3), o menor lado da caixa em x
// ou z (tipos 2 e 4) ou metade da diagonal em xz (1 e os outros). Só o tipo 0 testa a esfera no Collide_Update (0x101).
obj_tag* RB_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, level_tag* level) {
    obj_tag* obj = control_create_object(0x160, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    RIGIDBODY* rb = rb_of(obj);
    RB_Init(rb, uint16_t(level->params[2]));
    obj->type = kRigidBodyType;
    obj->hit_test = &rb->test;
    obj->mode |= 0x44;
    obj->glist = glist;
    rb->matrix = &obj->matrix;
    const uint32_t m = level->params[0];
    const float mass = m == 0 ? 3.0f : float(m);
    const float sx = glist->box_max.x - glist->box_min.x;
    const float sz = glist->box_max.z - glist->box_min.z;
    const float sy = glist->box_max.y - glist->box_min.y;
    float inertia[3];
    RB_CuboidMomentsOfInertia(inertia, mass, sx, sy, sz);
    rb->half_height = std::fabs(glist->box_min.y);
    RB_Build(rb, rb->kind == 0 ? &WldGravity : &CONSTANT_ZERO_VECTOR, inertia, mass);
    RB_SetState(rb);
    rb->test.ignore_a = obj;
    build_LinkToRoom(obj, 0, glb_world);
    rb->test_type = 0x101;
    rb->test.radius = glist->sphere.w;
    switch (rb->kind) {
        case 0: break;
        case 3: rb->test_type = 0; break;
        case 2:
        case 4:
            rb->test.radius = sz < sx ? sz : sx;  // min.S
            rb->test_type = 0;
            break;
        default:
            rb->test_type = 0;
            rb->test.radius = std::sqrt(sx * sx + sz * sz) * 0.5f;
            break;
    }
    rb->carries_light = uint8_t(level->params[3]);
    rb->sound = level->params[4] == 0 ? 0xffffu : level->params[4];
    ++g_rigid_body_stats.created;
    return obj;
}

// [decomp] 0x001d6e38 RB_Update
// Um tick: o teste de colisão do tipo 0 liga (0x101; os pêndulos ficam sem); abaixo da água da sala, em 15 de cada 16
// quadros, uma bolha (fumaça 0x2000129) e um empuxo para cima de |y - topo da sala| x massa x 0,4 + 0..0,15 (o topo da
// sala, cel_tag::box_max.y, e não a água: peculiaridade mantida); o vento; a integração (RB_EulerEvolve com
// REC_FRAME_RATE). Depois o teste segue a sala e, no tipo 0, a posição; nos pêndulos com o parâmetro 3, a luz da fase
// mais perto do centro (achada uma vez) vai com ele.
void RB_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    RIGIDBODY* rb = rb_of(obj);
    if (rb == nullptr || obj->glist == nullptr) return;
    rb->test.type = 0;
    if (rb->kind == 0) rb->test.type = rb->test_type;
    cel_tag* cel = obj->cel;
    if (cel != nullptr && obj->matrix.m[3][1] < cel->water_y && (GameState.frame & 0xf) != 0) {
        alignas(16) _VECTOR f{0.0f, 0.0f, 0.0f, 0.0f};
        const float depth = std::fabs(obj->matrix.m[3][1] - cel->box_max.y);
        const float r = Rand_FRand(0.15f);
        f.y = depth * (rb->mass * 0.4f) + r;
        Gas_Init(cel, &row3(obj->matrix), nullptr, nullptr, 0.5f, 1.0f, 0x2000129, 1, 0x40008080u, 0x80808008u, 0x10);
        RB_AddImpulsiveForce(rb, &f);
    }
    RB_ApplyWind(rb, obj);
    RB_EulerEvolve(rb, REC_FRAME_RATE, 0);
    rb->test.cel = obj->cel;
    rb->test.type = rb->test_type;
    if (rb->kind == 0) {
        rb->test.pos = row3(obj->matrix);
    } else {
        if (rb->carries_light != 0 && rb->light == nullptr) rb->light = Light_FindMapLight(&rb->test.pos, rb->test.radius);
        if (rb->light != nullptr) rb->light->pos = rb->test.pos;
    }
    obj->mode |= 0x20;
}

// [decomp] 0x001d6ff8 RB_ApplyImpulses
// O impulso angular do tick (fora de quase zero) vai para o corpo, é dividido pela inércia (os eixos y e z trocados:
// ver o cabeçalho), preso a +-4 por eixo e volta ao mundo somado a omega; no tipo 0, o impulso linear / massa soma à
// velocidade.
void RB_ApplyImpulses(RIGIDBODY* rb) {
    if (!Vec_IsEqual(&rb->ang_impulse, &CONSTANT_ZERO_VECTOR, 0.0002f)) {
        alignas(16) _VECTOR b;
        RB_ToBody(&b, &rb->ang_impulse, rb->matrix);
        const float c[3] = {b.x * rb->inv_inertia[0], b.y * rb->inv_inertia[1], b.z * rb->inv_inertia[2]};
        float* out[3] = {&b.x, &b.y, &b.z};
        for (int i = 0; i < 3; ++i) {
            const float v = c[i];
            *out[i] = -4.0f <= v ? (v < 4.0f ? v : 4.0f) : -4.0f;
        }
        alignas(16) _VECTOR w;
        RB_ToWorld(&w, &b, rb->matrix);
        add3(&rb->omega, w);
    }
    if (rb->kind == 0) madd3(&rb->vel, rb->impulse, rb->inv_mass);
}

// [decomp] 0x001d7168 RB_RestingContact
// A colisão do tipo 0 (os acertos do Collide_Update): cada acerto dá torque (RB_CalcTorque, fora do teste 0x400); um
// tiro (objeto 5) toca o som da batida e empurra no ponto do acerto (direção x dano x 0,5) e encerra a lista; outro
// objeto que não seja o jogador nem outro corpo e que andou menos de 0,707 m no tick carrega o corpo junto. No fim, com
// o empurrão da geometria (HITTEST_tag::push): v += n x -(r d + d), d = v.n: a parte contra a normal fica -r d (com r
// = -0,5, metade, no mesmo sentido: o corpo não quica, assenta) e o corpo anda o empurrão.
void RB_RestingContact(obj_tag* obj, RIGIDBODY* rb, HITDATA_tag* hit) {
    for (HITDATA_tag* h = hit; h != nullptr;) {
        if (rb->test_type != 0x400) RB_CalcTorque(rb, h);
        obj_tag* other = h->obj;
        if (other == nullptr) {
            h = h->next;
            continue;
        }
        const uint8_t type = other->type;
        if (type == 5) {
            Sound_Play3D(rb->sound, &row3(obj->matrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
            alignas(16) _VECTOR f;
            Vec_MulR32(&f, &h->dir, hit_damage(h) * 0.5f);
            const _VECTOR r = sub3(h->pos, row3(*rb->matrix));
            add3(&rb->impulse, f);
            add3(&rb->ang_impulse, cross_vu(r, f));
            ++g_rigid_body_stats.bullet_hits;
            break;
        }
        if (type != 3 && type != kRigidBodyType) {
            const _VECTOR d = sub3(row3(other->matrix), other->old_pos);
            if (Vec_SqMagnitude(&d) < 0.5f) {
                add3(&row3(*rb->matrix), d);
                ++g_rigid_body_stats.carried;
            }
        }
        h = h->next;
    }
    const _VECTOR& push = rb->test.push;
    if (Vec_IsEqual(&push, &CONSTANT_ZERO_VECTOR, 0.0f)) return;
    alignas(16) _VECTOR n{0.0f, 0.0f, 0.0f, 0.0f};
    Vec_Normalise(&n, &push);
    const float d = dot_vu(rb->vel, n);
    const float k = -(rb->restitution * d + d);
    madd3(&rb->vel, n, k);
    add3(&row3(*rb->matrix), push);
    ++g_rigid_body_stats.bounces;
}

// [decomp] 0x001d73a0 RB_RestingContactRot
// A colisão dos pêndulos (tipos 1-4): o teste vai para o centro + cima x (raio - meia altura) e, fora do tipo 3, uma
// cápsula do ponto 1,8 raio abaixo da posição até ele testa a geometria (Collide_CylinderIntersect, sem objetos:
// máscara 0x10). Cada tiro toca o som e dá o torque do impulso (direção x dano x 0,5) no ponto do acerto, só nos eixos
// do tipo (1 sem y; 2 só x; 3 só y; 4 só z). Com acerto da cápsula e empurrão, o torque do empurrão x 2,5 no ponto do
// primeiro acerto, sem y.
void RB_RestingContactRot(obj_tag* obj, RIGIDBODY* rb, HITDATA_tag* hit) {
    HITDATA_tag* hits = nullptr;
    alignas(16) _VECTOR up;
    Mat_GetUp(&up, &obj->matrix);
    const _VECTOR& pos = row3(obj->matrix);
    const float k = rb->test.radius - rb->half_height;
    rb->test.pos = {pos.x + up.x * k, pos.y + up.y * k, pos.z + up.z * k, pos.w};
    if (rb->kind != 3) {
        const float below = -(rb->test.radius * 1.8f);
        rb->test.start = {pos.x + up.x * below, pos.y + up.y * below, pos.z + up.z * below, pos.w};
        rb->test.end = {pos.x + up.x * k, pos.y + up.y * k, pos.z + up.z * k, pos.w};
        rb->test.mask = 0x10;
        if (Collide_CylinderIntersect(&rb->test, &hits, 0, 0)) ++g_rigid_body_stats.cylinder_hits;
    }
    for (HITDATA_tag* h = hit; h != nullptr; h = h->next) {
        if (h->obj == nullptr || h->obj->type != 5) continue;
        Sound_Play3D(rb->sound, &row3(obj->matrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
        alignas(16) _VECTOR f;
        Vec_MulR32(&f, &h->dir, hit_damage(h) * 0.5f);
        const _VECTOR r = sub3(h->pos, row3(*rb->matrix));
        _VECTOR t = cross_vu(r, f);
        if (rb->kind == 2) t.y = t.z = 0.0f;
        if (rb->kind == 3) t.z = t.x = 0.0f;
        if (rb->kind == 4) t.y = t.x = 0.0f;
        if (rb->kind == 1) t.y = 0.0f;
        add3(&rb->ang_impulse, t);
        ++g_rigid_body_stats.bullet_hits;
    }
    if (hits != nullptr && !Vec_IsEqual(&rb->test.push, &CONSTANT_ZERO_VECTOR, 0.0f)) {
        alignas(16) _VECTOR p;
        Vec_MulR32(&p, &rb->test.push, 2.5f);
        const _VECTOR r = sub3(hits->pos, row3(*rb->matrix));
        _VECTOR t = cross_vu(r, p);
        t.y = 0.0f;
        add3(&rb->ang_impulse, t);
    }
    Collide_FreeHitList(&hits);
}

// [decomp] 0x001d7658 RB_CalcTorque
// O atrito de um contato: na base (u, v, n) da normal do acerto, a velocidade em cada eixo acima de 0,05 vira um
// torque (r x eixo) x (componente x -0,01; -0,2 na normal), com r do centro ao ponto do acerto.
void RB_CalcTorque(RIGIDBODY* rb, HITDATA_tag* hit) {
    alignas(16) _VECTOR n;
    std::memcpy(&n, &hit->plane, sizeof(n));
    const _VECTOR r = sub3(hit->pos, row3(*rb->matrix));
    alignas(16) _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &n);
    const float du = dot_vu(u, rb->vel);
    const float dn = dot_vu(n, rb->vel);
    const float dv = dot_vu(v, rb->vel);
    if (0.05f < std::fabs(du)) madd3(&rb->ang_impulse, cross_vu(r, u), du * -0.01f);
    if (0.05f < std::fabs(dv)) madd3(&rb->ang_impulse, cross_vu(r, v), dv * -0.01f);
    if (0.05f < std::fabs(dn)) madd3(&rb->ang_impulse, cross_vu(r, n), dn * -0.2f);
}

// [decomp] 0x001d7810 RB_EulerEvolve
// Integra um passo de dt (sem skip_impulses, aplica e zera os impulsos do tick): arrasto angular exponencial
// (omega += omega x k, k = -(1 - e^(-dt / ang_damp)) / dt, sem multiplicar de novo por dt: a forma do original), no
// tipo 0 a aceleração (gravidade com arrasto linear, ou a gravidade pura sem ele), omega += ang_accel x
// dt, no tipo 0 velocidade e posição; omega fora de quase zero gira o quatérnio (RB_apply_omega) e a matriz é refeita.
// Nos pêndulos, um impulso angular que leva o "cima" do corpo de volta à vertical: (-cima.z, 0, cima.x) x I1 x
// FRAME_RATE_MUL, para o próximo tick.
void RB_EulerEvolve(RIGIDBODY* rb, float dt, int32_t skip_impulses) {
    if (skip_impulses == 0) {
        RB_ApplyImpulses(rb);
        Vec_Zero(&rb->impulse);
        Vec_Zero(&rb->ang_impulse);
    }
    if (dt != 0.0f) {
        if (0.0f < rb->ang_damp) {
            const float avg = ((rb->inertia[0] + rb->inertia[1]) + rb->inertia[2]) * 0.333f;
            const float e = std::exp((-(avg / rb->ang_damp) / avg) * dt);
            const float k = -((1.0f - e) / dt);
            madd3(&rb->omega, rb->omega, k);
        }
        if (rb->kind == 0) {
            if (0.0f < rb->lin_damp) {
                const float e = std::exp(-dt / rb->lin_damp);
                RB_VecSpecial(&rb->accel, (e - 1.0f) / dt, &rb->vel, rb->gravity, rb->lin_damp);
            } else {
                rb->accel = WldGravity;
            }
        }
        madd3(&rb->omega, rb->ang_accel, dt);
        if (rb->kind == 0) {
            madd3(&rb->vel, rb->accel, dt);
            madd3(&row3(*rb->matrix), rb->vel, dt);
        }
        if (!Vec_IsEqual(&rb->omega, &CONSTANT_ZERO_VECTOR, 0.0002f)) {
            quaternion_tag q = get_quat(rb);
            RB_apply_omega(&q, &rb->omega, dt);
            set_quat(rb, q);
        }
    }
    const quaternion_tag q = get_quat(rb);
    Quat_QuaternionToMatrix(rb->matrix, &q);
    if (rb->kind != 0) {
        alignas(16) _VECTOR up;
        Mat_GetUp(&up, rb->matrix);
        const float i1 = rb->inertia[1];
        alignas(16) const _VECTOR t{-up.z * i1 * FRAME_RATE_MUL, 0.0f, up.x * i1 * FRAME_RATE_MUL, 0.0f};
        add3(&rb->ang_impulse, t);
        Quat_QuaternionToMatrix(rb->matrix, &q);
    }
}

// [decomp] 0x001d7a80 RB_CollisionHandler
void RB_CollisionHandler(obj_tag* obj) {
    if (obj == nullptr) return;
    RIGIDBODY* rb = rb_of(obj);
    if (rb->kind == 0) {
        RB_RestingContact(obj, rb, obj->hits);
    } else {
        RB_RestingContactRot(obj, rb, obj->hits);
    }
    obj->mode |= 0x20;
}

// [decomp] 0x001d7ad8 RB_Init
// Zera tudo, orientação identidade (1, 0, 0, 0), e as constantes do tipo: 0 com o coeficiente -0,5, arrasto linear de
// 1 s e angular de 31; os outros com -0,75, sem arrasto linear, angular de 25 (250 no tipo 3).
void RB_Init(RIGIDBODY* rb, uint16_t kind) {
    std::memset(rb, 0, sizeof(RIGIDBODY));
    rb->kind = kind;
    rb->quat[0] = 1.0f;
    rb->quat[1] = rb->quat[2] = rb->quat[3] = 0.0f;
    if (kind == 0) {
        rb->restitution = -0.5f;
        rb->ang_damp = 31.0f;
        rb->lin_damp = 1.0f;
    } else {
        rb->restitution = -0.75f;
        rb->ang_damp = kind == 3 ? 250.0f : 25.0f;
        rb->lin_damp = 0.0f;
    }
}

// [decomp] 0x001d7ba0 RB_Build
// Só aceita momentos de inércia não negativos que formem um triângulo (cada um até a soma dos outros dois).
uint32_t RB_Build(RIGIDBODY* rb, const _VECTOR* gravity, const float* inertia, float mass) {
    const float a = inertia[0];
    if (!(0.0f <= a)) return 0;
    const float b = inertia[1];
    if (!(0.0f <= b)) return 0;
    const float c = inertia[2];
    if (!(0.0f <= c) || !(c <= a + b) || !(a <= b + c) || !(b <= a + c)) return 0;
    rb->inertia[0] = a;
    rb->inertia[1] = inertia[1];
    rb->inertia[2] = inertia[2];
    rb->inv_inertia[0] = 1.0f / inertia[0];
    rb->inv_inertia[2] = 1.0f / inertia[1];
    rb->gravity = gravity;
    rb->inv_mass = 1.0f / mass;
    rb->mass = mass;
    rb->inv_inertia[1] = 1.0f / inertia[2];
    return 1;
}

// [decomp] 0x001d7c78 RB_ApplyWind
// O vento da sala (Env_GetWindVel) / massa x REC_FRAME_RATE: no tipo 0 no centro; nos pêndulos, no ponto do eixo a
// (meia altura - raio) acima da posição, o que dá giro.
void RB_ApplyWind(RIGIDBODY* rb, obj_tag* obj) {
    alignas(16) _VECTOR w;
    Env_GetWindVel(obj->cel, &row3(obj->matrix), &w);
    const float k = rb->inv_mass * REC_FRAME_RATE;
    w.z = w.z * k;
    w.x = w.x * k;
    w.y = w.y * k;
    if (rb->kind == 0) {
        RB_AddImpulsiveForce(rb, &w);
        return;
    }
    alignas(16) const _VECTOR p{obj->matrix.m[3][0], (obj->matrix.m[3][1] + rb->half_height) - rb->test.radius,
                                obj->matrix.m[3][2], 0.0f};
    RB_AddImpulsiveForceAtPoint(rb, &w, &p);
}

// [decomp] 0x001d7d40 RB_AddImpulsiveForce
void RB_AddImpulsiveForce(RIGIDBODY* rb, const _VECTOR* force) { add3(&rb->impulse, *force); }

// [decomp] 0x001d7d58 RB_AddImpulsiveForceAtPoint
// A força e o torque dela no ponto: (ponto - centro) x força.
void RB_AddImpulsiveForceAtPoint(RIGIDBODY* rb, const _VECTOR* force, const _VECTOR* point) {
    const _VECTOR r = sub3(*point, row3(*rb->matrix));
    const _VECTOR t = cross_vu(r, *force);
    add3(&rb->impulse, *force);
    add3(&rb->ang_impulse, t);
}

// [decomp] 0x001d7da0 RB_SetState
void RB_SetState(RIGIDBODY* rb) {
    const quaternion_tag q = get_quat(rb);
    Quat_QuaternionToMatrix(rb->matrix, &q);
}

// [decomp] 0x001d7dc8 RB_CuboidMomentsOfInertia
// Paralelepípedo de massa m e lados sx, sy, sz: Ix = m (sy² + sz²) / 12, Iy = m (sx² + sz²) / 12, Iz = m (sx² + sy²) / 12.
void RB_CuboidMomentsOfInertia(float* out, float mass, float sx, float sy, float sz) {
    out[0] = mass * (sy * sy + sz * sz) * 0.083333336f;
    out[2] = mass * (sx * sx + sy * sy) * 0.083333336f;
    out[1] = mass * (sx * sx + sz * sz) * 0.083333336f;
}

// [decomp] 0x001d7e18 RB_ToWorld
// v x M (só a rotação: vetor-linha).
void RB_ToWorld(_VECTOR* out, const _VECTOR* v, const MATRIX* m) {
    const float x = v->x, y = v->y, z = v->z;
    out->x = m->m[0][0] * x + m->m[1][0] * y + m->m[2][0] * z;
    out->y = m->m[0][1] * x + m->m[1][1] * y + m->m[2][1] * z;
    out->z = m->m[0][2] * x + m->m[1][2] * y + m->m[2][2] * z;
}

// [decomp] 0x001d7eb8 RB_ToBody
// M x v (a transposta da rotação: do mundo para o corpo).
void RB_ToBody(_VECTOR* out, const _VECTOR* v, const MATRIX* m) {
    const float x = v->x, y = v->y, z = v->z;
    out->x = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z;
    out->y = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z;
    out->z = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z;
}

// [decomp] 0x001d7f50 RB_apply_omega
// Gira q (escalar primeiro) pela velocidade angular durante dt: q = (cos h, sen h / |omega| x omega) * q, com
// h = |omega| dt / 2 (PS2Sinf, o seno da VU0).
void RB_apply_omega(quaternion_tag* q, const _VECTOR* omega, float dt) {
    const float mag = Vec_Magnitude(omega);
    if (mag == 0.0f) return;
    const float h = mag * dt * 0.5f;
    quaternion_tag d;
    d.x = PS2Sinf(h + 1.5707964f);
    const float s = PS2Sinf(h) / mag;
    d.w = s * omega->z;
    d.y = s * omega->x;
    d.z = s * omega->y;
    quaternion_tag old;
    Quat_Copy(&old, q);
    Quat_QuaternionMultiply(&d, &old, q);
}

// [decomp] 0x001d8038 RB_VecSpecial
// out = k (a - b s), em x, y e z.
void RB_VecSpecial(_VECTOR* out, float k, const _VECTOR* a, const _VECTOR* b, float s) {
    out->x = k * (a->x - b->x * s);
    out->y = k * (a->y - b->y * s);
    out->z = k * (a->z - b->z * s);
}
