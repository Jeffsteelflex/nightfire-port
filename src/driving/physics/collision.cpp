// O impulso de um choque de corpo rígido contra o mundo e a resposta de um choque entre dois corpos
// (driving/physics/collision.h), da desmontagem do DRIVING.ELF. Os vetores temporários que no PS2 ficam na pilha
// começam zerados aqui: o w deles (que o código não escreve) é lixo da pilha no PS2 e só aparece em
// CollisionInfo::velocity[3] (GenerateImpulse); em ResolveCollision nada lê o w dos braços e dos impulsos.
// Divergência (só para não quebrar): sem dono (o PS2 sempre tem um), as consultas ao dono de ResolveCollision dão o
// valor neutro (vida 1,0, entradas virtuais 0, nenhuma animação) e contam em physics_pending("dono nulo").

#include "driving/physics/collision.h"

#include <cstring>

#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {
const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};

// vmul.xyz + vaddy.x + vaddz.x: o produto escalar no x.
float dot3(const uint32_t* a, const uint32_t* b) {
    Reg t{};
    mul(t, a, b, XYZ);
    addbc(t, t, t, BY, X);
    addbc(t, t, t, BZ, X);
    return f(t, 0);
}

// vmul.xyz vf2 = v * v; vmulax.x ACC = v.x v.x; vmadday.x ACC += 1 * vf2.y; vmaddz.x vf2 = ACC + 1 * vf2.z.
float length_sq(const uint32_t* v) {
    Reg sq{}, acc{};
    const uint32_t one[4] = {0x3f800000, 0, 0, 0};
    mul(sq, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddbc(sq, acc, one, sq, BZ, X);
    return f(sq, 0);
}

void scale_xyz(uint32_t* d, const uint32_t* s, float k) {
    const uint32_t kv[4] = {b(k), 0, 0, 0};
    mulbc(d, s, kv, BX, XYZ);
}

// v * M com as quatro linhas (vmulax/vmadday/vmaddaz/vmaddw com vf0.w): o que as quatro conversões fazem.
void transform4(float v[4], const float m[4][4]) {
    Reg in, r0, r1, r2, r3, out, acc;
    load(in, v);
    load(r0, m[0]);
    load(r1, m[1]);
    load(r2, m[2]);
    load(r3, m[3]);
    load(out, v);
    mulabc(acc, r0, in, BX, XYZ);
    maddabc(acc, r1, in, BY, XYZ);
    maddabc(acc, r2, in, BZ, XYZ);
    maddbc(out, acc, r3, kVf0, BW, XYZ);
    store(v, out);
}

void transposed(const float m[4][4], float t[4][4]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) t[i][j] = m[j][i];
}

// vsqrt Q, vf.x; vwaitq; vaddq.x d, vf0, Q: a raiz pela VU0 (o x de vf0, zero, somado ao Q).
float vu_sqrt(float x) { return ps2::ffloat(ps2::add(0, ps2::sqrt(ps2::fbits(x)))); }

// a x b pelo par vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a (só o xyz de d muda).
void cross(uint32_t* d, const uint32_t* a, const uint32_t* bv) {
    Reg acc{};
    opmula(acc, a, bv);
    opmsub(d, acc, bv, a);
}

// lqc2 vf3, d; vmulx.xyz vf3, s, k; sqc2 vf3, d: d.xyz = s.xyz * k, o w de d fica.
void scaled_into(float d[4], const uint32_t* s, float k) {
    Reg r;
    load(r, d);
    scale_xyz(r, s, k);
    store(d, r);
}
void scale_in_place(float v[4], float k) {
    Reg s;
    load(s, v);
    scaled_into(v, s, k);
}

// As consultas ao dono de um corpo. O PS2 sempre tem um dono; sem ele (port) o valor neutro e a conta em
// physics_pending.
PhysicsObject* owner_of(const RigidBody* rb) {
    PhysicsObject* o = GetOwner(rb);
    if (!o) physics_pending("dono nulo");
    return o;
}
float owner_hit_points(const RigidBody* rb) {
    PhysicsObject* o = owner_of(rb);
    return o ? o->hit_points() : 1.0f;
}
int32_t owner_9c(const RigidBody* rb) {
    PhysicsObject* o = owner_of(rb);
    return o ? o->virtual_9c() : 0;
}
int32_t owner_18c(const RigidBody* rb) {
    PhysicsObject* o = owner_of(rb);
    return o ? o->virtual_18c() : 0;
}
void owner_play_animation(const RigidBody* rb, uint32_t id) {
    if (PhysicsObject* o = owner_of(rb)) o->play_animation(id);
}

// v = P / m (vmulx.xyz com 1 / m) e ω = I⁻¹(mundo) L (vmulax/vmadday/vmaddz pelas linhas do tensor).
void refresh_velocities(RigidBody* rb) {
    Reg p, v, l, w, row0, row1, row2, acc{};
    load(p, &rb->momentum[0]);
    load(v, &rb->vel[0]);
    scale_xyz(v, p, ee::div(1.0f, rb->mass));
    store(&rb->vel[0], v);
    load(l, &rb->ang_momentum[0]);
    load(row0, rb->info->inv_world_tensor[0]);
    load(row1, rb->info->inv_world_tensor[1]);
    load(row2, rb->info->inv_world_tensor[2]);
    load(w, &rb->ang_vel[0]);
    mulabc(acc, row0, l, BX, XYZ);
    maddabc(acc, row1, l, BY, XYZ);
    maddbc(w, acc, row2, l, BZ, XYZ);
    store(&rb->ang_vel[0], w);
}

// O fim de um choque para um corpo que levou impulso: estado 2, RigidBodyInfo +0x4fe e o bit 0 de +0x7c.
void wake(RigidBody* rb) {
    rb->state = 2;
    rb->info->flags[2] = 1;
    rb->radius_flags |= 1u;
}

// O impulso de um objeto comum (tipo >= 4) num choque com algum objeto: acima de 15 m/s, nenhum; senão x e z em
// ±10 m e o y em |y| até 10 m. Num veículo só o y zera.
void limit_object_impulse(const RigidBody* rb, float imp[4]) {
    if (rb->type < 4) {
        imp[1] = 0.0f;
        return;
    }
    Reg v;
    load(v, &rb->vel[0]);
    if (!ee::lt(vu_sqrt(length_sq(v)), 15.0f)) {
        imp[0] = imp[1] = imp[2] = 0.0f;  // sd zero, +0; sw zero, +8
        return;
    }
    const float lo = ee::mul(rb->mass, -10.0f);
    const float hi = ee::mul(rb->mass, 10.0f);
    imp[0] = ee::min(ee::max(imp[0], lo), hi);
    imp[1] = ee::min(ee::abs(imp[1]), hi);
    imp[2] = ee::min(ee::max(imp[2], lo), hi);
}

// O momento angular do braço r com o impulso imp (r x imp; um quarto dele num objeto comum), somado (add_it) ou
// subtraído.
void apply_object_torque(RigidBody* rb, const float r[4], const float imp[4], bool add_it) {
    Reg rr, ii, ang{}, l;
    load(rr, r);
    load(ii, imp);
    cross(ang, rr, ii);
    if (rb->type >= 4) scale_xyz(ang, ang, 0.25f);
    load(l, &rb->ang_momentum[0]);
    if (add_it) add(l, l, ang, XYZ);
    else sub(l, l, ang, XYZ);
    store(&rb->ang_momentum[0], l);
}

// O mesmo nos veículos: o y de r x imp vai a 0,16 no carro do jogador (tipo 1) e 0,5 nos outros; hold: o corpo que
// caía e teve o y do momento cortado não gira.
void apply_vehicle_torque(RigidBody* rb, const float r[4], const float imp[4], bool add_it, bool hold) {
    Reg rr, ii, ang{}, l;
    load(rr, r);
    load(ii, imp);
    cross(ang, rr, ii);
    const float k = rb->type == 1 ? ps2::ffloat(0x3e23d70au) : 0.5f;  // 0,16
    ang[1] = ps2::fbits(ee::mul(f(ang, 1), k));
    if (hold) return;
    load(l, &rb->ang_momentum[0]);
    if (add_it) add(l, l, ang, XYZ);
    else sub(l, l, ang, XYZ);
    store(&rb->ang_momentum[0], l);
}
}  // namespace

// [driving] 0x210510 Util_Bound
float Util_Bound(float v, float a, float b) {
    if (ee::lt(a, v)) return b;
    if (ee::lt(v, ee::neg(a))) return ee::neg(b);
    return v;
}

// [driving] 0x210938 Util_GenerateCarTensor
void Util_GenerateCarTensor(float out[3], float mass, float dx, float dy, float dz) {
    if (ee::lt(4.0f, dz)) {
        const float y2 = ee::mul(dy, dy);
        const float z2 = ee::mul(dz, dz);
        const float x2 = ee::mul(dx, dx);
        const float m3 = ee::mul(mass, ps2::ffloat(0x3eaaa64cu));  // 0,3333 (não 1/3 exato)
        const float yz = ee::add(y2, z2), xy = ee::add(x2, y2), xz = ee::add(x2, z2);
        out[0] = ee::div(1.0f, ee::mul(m3, yz));
        out[2] = ee::div(1.0f, ee::mul(m3, xy));
        out[1] = ee::div(1.0f, ee::mul(m3, xz));
        return;
    }
    // O ramo de carro: (0,5 / (m/12 (2dz)² + m/4 (dx² + dy²)), o mesmo, 0,5 / (m/2 (dx² + dy²))).
    const float y2 = ee::mul(dy, dy);
    const float dz2 = ee::add(dz, dz);
    const float x2 = ee::mul(dx, dx);
    const float m12 = ee::mul(mass, ps2::ffloat(0x3daaa8ebu));  // 0,08333
    const float zz = ee::mul(dz2, dz2);
    const float xy = ee::add(x2, y2);
    const float m4 = ee::mul(mass, 0.25f);
    const float m2 = ee::mul(mass, 0.5f);
    const float a = ee::add(ee::mul(m12, zz), ee::mul(m4, xy));
    const float c = ee::mul(m2, xy);
    out[2] = ee::div(0.5f, c);
    out[0] = out[1] = ee::div(0.5f, a);
}

// [driving] 0x1f8418 RigidBody::ConvertToLocalAngularMomentum
void ConvertToLocalAngularMomentum(const RigidBody* rb, float v[4]) {
    float t[4][4];
    transposed(rb->info->orient, t);
    transform4(v, t);
}

// [driving] 0x1f8498 RigidBody::ConvertFromLocalAngularMomentum
void ConvertFromLocalAngularMomentum(const RigidBody* rb, float v[4]) { transform4(v, rb->info->orient); }

// [driving] 0x1f8508 RigidBody::ConvertWorldToLocal
void ConvertWorldToLocal(const RigidBody* rb, float v[4]) {
    float t[4][4];
    transposed(rb->info->orient, t);
    transform4(v, t);
}

// [driving] 0x1f84d0 RigidBody::ConvertLocalToWorld
void ConvertLocalToWorld(const RigidBody* rb, float v[4]) { transform4(v, rb->info->orient); }

// [driving] 0x1fb318 RigidBody::GenerateImpulse
void GenerateImpulse(RigidBody* rb, CollisionInfo* out, const float n[4], const float point[4], float depth,
                     bool world, uint16_t tag, float scale) {
    RigidBodyInfo* info = rb->info;
    const RigidTuning& t = rigid_tuning();
    const float(*tensor)[4] = info->inv_world_tensor;
    info->flags[1] = 1;
    Reg nv, pv, pos, vel, omg, mom, acc;
    load(nv, n);
    load(pv, point);
    load(pos, &rb->pos[0]);
    load(vel, &rb->vel[0]);
    load(omg, &rb->ang_vel[0]);
    // As variáveis da pilha (o w delas é lixo no PS2).
    Reg r{}, vp{}, rxn{}, x2{}, fr{}, imp{}, p70{}, ang{};
    sub(r, pv, pos, XYZ);                // sp+0x60 = ponto - posição
    opmula(acc, omg, r);
    opmsub(vp, acc, r, omg);             // sp+0x10 = ω x r
    add(vp, vp, vel, XYZ);               //         + v
    const float vn = ee::mul(dot3(vp, nv), scale);
    auto fill = [&](float strength) {
        out->speed = vn;
        std::memcpy(out->normal, n, 16);
        std::memcpy(out->velocity, vp, 16);
        out->strength = strength;
        out->kind = 1;
        out->flags = 0x10;
        out->type = 6;
        out->slot = rb->slot;
        out->tag = tag;
        std::memcpy(out->point, point, 16);
    };
    if (world && ee::lt(0.0f, vn)) {  // o ponto já se afasta: só informa
        fill(0.0f);
        return;
    }
    opmula(acc, r, nv);
    opmsub(rxn, acc, nv, r);             // sp+0x40 = r x n
    {
        Reg row0, row1, row2, in;
        load(row0, tensor[0]);
        load(row1, tensor[1]);
        load(row2, tensor[2]);
        std::memcpy(in, rxn, 16);
        mulabc(acc, row0, in, BX, XYZ);
        maddabc(acc, row1, in, BY, XYZ);
        maddbc(rxn, acc, row2, in, BZ, XYZ);  // I⁻¹ (r x n)
    }
    opmula(acc, rxn, r);
    opmsub(x2, acc, r, rxn);             // sp+0x50 = (I⁻¹ (r x n)) x r
    // A distância do ponto ao eixo da normal no plano xz, dividida pela meia-largura.
    const float rx = f(r, 0), rz = f(r, 2);
    float axis = ee::abs(ee::add(ee::mul(rx, n[0]), ee::mul(rz, n[2])));
    float side = ee::add(ee::mul(rx, rx), ee::mul(rz, rz));
    side = ee::sub(side, axis);
    side = ee::max(side, 0.0f);
    if (ee::lt(0.0f, side)) side = ee::div(ee::sqrt(side), info->half_size[0]);
    const float denom = ee::add(dot3(x2, nv), side);
    const float push = ee::mul(depth, t.pf_scale);
    float j;
    if (ee::eq(denom, 0.0f)) {
        j = 0.0f;
    } else {
        const float speed = ee::lt(t.building_micro_threshold, ee::abs(vn)) ? t.building_micro_threshold : ee::abs(vn);
        const float rel = ee::sub(vn, push);
        float k = ee::mul(t.building_restitution_scale, speed);
        k = ee::neg(ee::add(k, 1.0f));
        j = ee::div(ee::mul(k, rel), denom);
    }
    float raw = j;
    j = ee::min(raw, t.building_force_limit);
    j = ee::max(j, ee::neg(t.building_force_limit));
    // O atrito: a velocidade do ponto vezes BUILDING_FRICTION_FACTOR, cada componente limitada.
    scale_xyz(fr, vp, t.building_friction_factor);
    for (int i = 0; i < 3; ++i) fr[i] = b(Util_Bound(f(fr, i), t.building_friction_limit, t.building_friction_limit));
    scale_xyz(imp, nv, j);
    if (world) sub(imp, imp, fr, XYZ);
    scale_xyz(imp, imp, rb->mass);
    // O carro do jogador batendo de lado (tipo 1, num prédio): o impulso perde o y.
    bool side_hit = false;
    if (world && rb->type == 1) {
        PhysicsObject* owner = GetOwner(rb);
        const PhysicsAttributes* attr = owner_attributes(owner);
        if (owner && attr && attr->special_mode == 0 && owner->virtual_9c() != 1 && owner->virtual_1dc() != 0) {
            float v70[4] = {};
            owner->virtual_134(v70);
            if (ee::lt(0.75f, ee::abs(v70[0]))) {
                Reg fwd;
                load(fwd, info->orient[2]);
                if (ee::lt(dot3(r, fwd), 0.0f) && ee::lt(ee::abs(dot3(nv, fwd)), ps2::ffloat(0x3f34fdf4u)))
                    side_hit = true;
            }
        }
    }
    if (side_hit) {
        r[1] = 0;
        imp[1] = 0;
    }
    // Um corpo comum batendo no mundo não sai mais rápido do que entrou.
    if (rb->type >= 4 && world) {
        const float v2 = length_sq(vel);
        if (ee::lt(0.0f, v2)) {
            load(mom, &rb->momentum[0]);
            add(p70, mom, imp, XYZ);
            scale_xyz(p70, p70, ee::div(1.0f, rb->mass));
            const float after = length_sq(p70);
            if (ee::lt(v2, after)) scale_xyz(imp, imp, ee::div(v2, after));
        }
    }
    load(mom, &rb->momentum[0]);
    add(mom, mom, imp, XYZ);
    store(&rb->momentum[0], mom);
    opmula(acc, r, imp);
    opmsub(ang, acc, imp, r);            // sp+0x30 = r x J
    float a4[4];
    std::memcpy(a4, ang, 16);
    if (world && rb->type < 4) {
        const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->special_mode == 0) {
            ConvertToLocalAngularMomentum(rb, a4);
            a4[2] = ee::mul(a4[2], 0.2f);
        }
    }
    if (side_hit) {
        a4[1] = ee::mul(a4[1], ps2::ffloat(0x3ed9999au));  // 0,425
        raw = ee::mul(raw, 0.001f);
    }
    if (world && rb->type < 4) {
        const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->special_mode == 0) {
            a4[1] = ee::mul(a4[1], 0.2f);
            ConvertFromLocalAngularMomentum(rb, a4);
        }
    }
    if (rb->type < 4) {
        const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->special_mode == 1) {
            ConvertWorldToLocal(rb, a4);
            a4[2] = ee::mul(a4[2], ps2::ffloat(0x3e19999au));  // 0,15
            ConvertLocalToWorld(rb, a4);
        }
    } else {
        load(ang, a4);
        scale_xyz(ang, ang, 0.1f);
        std::memcpy(a4, ang, 16);
    }
    {
        Reg l, a;
        load(l, &rb->ang_momentum[0]);
        load(a, a4);
        add(l, l, a, XYZ);
        store(&rb->ang_momentum[0], l);
    }
    {
        Reg p, v;
        load(p, &rb->momentum[0]);
        load(v, &rb->vel[0]);
        scale_xyz(v, p, ee::div(1.0f, rb->mass));
        store(&rb->vel[0], v);
    }
    {
        Reg l, row0, row1, row2, w;
        load(l, &rb->ang_momentum[0]);
        load(row0, tensor[0]);
        load(row1, tensor[1]);
        load(row2, tensor[2]);
        load(w, &rb->ang_vel[0]);
        mulabc(acc, row0, l, BX, XYZ);
        maddabc(acc, row1, l, BY, XYZ);
        maddbc(w, acc, row2, l, BZ, XYZ);
        store(&rb->ang_vel[0], w);
    }
    float s = raw;
    if (ee::lt(s, 0.0f)) s = ee::neg(s);
    s = ee::div(ee::mul(s, 0.5f), t.building_force_limit);
    fill(ee::min(s, 1.0f));
}

// [driving] 0x1f98e8 RigidBody::ModifyLevers
void ModifyLevers(RigidBody* a, RigidBody* b, float ra[4], float rb[4], const float point[4], float normal[4],
                  bool vehicles) {
    if (!vehicles) {
        bool none = true;  // v1
        if (a->type < 4) {
            ra[1] = 0.0f;  // sw zero
            none = false;
        }
        if (b->type < 4) {
            rb[1] = 0.0f;
            none = false;
        }
        if (none) return;
        // A normal vira (ponto - centro de A) normalizado: vrsqrt Q, vf0w, |n|²x; vmulq.xyz (o w da normal fica).
        Reg p, pos, n;
        load(p, point);
        load(pos, &a->pos[0]);
        load(n, normal);
        sub(n, p, pos, XYZ);
        store(normal, n);
        Reg v, sq, acc{};
        const uint32_t one[4] = {0x3f800000, 0, 0, 0};
        load(v, normal);
        load(sq, normal);
        mul(sq, v, v, XYZ);
        mulabc(acc, v, v, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(sq, acc, one, sq, BZ, X);
        const uint32_t q = ps2::rsqrt(kVf0[3], sq[0]);
        muls(sq, v, q, XYZ);
        store(normal, sq);
        return;
    }
    ConvertWorldToLocal(a, ra);
    ConvertWorldToLocal(b, rb);
    // Os dois de frente um para o outro (ou um atrás do outro) ao longo da frente de A.
    Reg fa, fb, pa, pb, d{};
    load(fa, a->info->orient[2]);
    load(fb, b->info->orient[2]);
    const float facing = ee::abs(dot3(fa, fb));
    load(pa, &a->pos[0]);
    load(pb, &b->pos[0]);
    sub(d, pa, pb, XYZ);
    const float along = ee::abs(dot3(d, fa));
    const float cos30 = ps2::ffloat(0x3f5db22du);  // 0,866
    float k;
    if (ee::lt(cos30, facing) && ee::lt(cos30, along)) {
        k = ps2::ffloat(0x3df5c28fu);  // 0,12
        if (ee::lt(0.0f, ra[1])) ra[1] = ee::neg(ra[1]);
        if (ee::lt(0.0f, rb[1])) rb[1] = ee::neg(rb[1]);
    } else {
        k = ps2::ffloat(0xbeb33333u);  // -0,35
    }
    const RigidTuning& t = rigid_tuning();
    const float ca = a->type == 1 ? t.car_collide_cog_scale : t.unknown_164;
    const float cb = b->type == 1 ? t.car_collide_cog_scale : t.unknown_164;
    const float sa = ee::max(0.0f, ee::add(ca, k));
    const float sb = ee::max(0.0f, ee::add(cb, k));
    ra[1] = ee::mul(ra[1], sa);
    rb[1] = ee::mul(rb[1], sb);
    ConvertLocalToWorld(a, ra);
    ConvertLocalToWorld(b, rb);
}

// [driving] 0x1f9b40 RigidBody::ScaleObjObjForces
void ScaleObjObjForces(const RigidBody* a, const RigidBody* b, float imp_a[4], float imp_b[4], float threshold,
                       float factor, float depth) {
    Reg v;
    load(v, &a->vel[0]);
    const float va = length_sq(v);
    load(v, &b->vel[0]);
    const float vb = length_sq(v);
    const float vmax = ee::max(va, vb);
    if (!ee::lt(threshold, vmax)) return;
    // As velocidades depois do impulso (sp+0x00 e sp+0x10; o w é lixo da pilha no PS2).
    Reg m, i, pa{}, pb{};
    load(m, &a->momentum[0]);
    load(i, imp_a);
    sub(pa, m, i, XYZ);
    load(m, &b->momentum[0]);
    load(i, imp_b);
    add(pb, m, i, XYZ);
    scale_xyz(pa, pa, ee::div(1.0f, a->mass));
    scale_xyz(pb, pb, ee::div(1.0f, b->mass));
    const float sa = length_sq(pa);
    const float sb = length_sq(pb);
    if (ee::lt(threshold, sa)) {
        const float lim = ee::mul(factor, vmax);
        if (ee::lt(lim, sa)) scale_in_place(imp_a, ee::div(lim, sa));
    }
    if (ee::lt(threshold, sb)) {
        const float lim = ee::mul(factor, vmax);
        if (ee::lt(lim, sb)) scale_in_place(imp_b, ee::div(lim, sb));
    }
    if (!ee::lt(0.5f, depth)) return;
    float k = ee::sub(depth, 0.5f);
    k = ee::add(k, k);
    k = ee::mul(k, 1.5f);
    k = ee::add(k, 1.5f);
    if (a->info->flags[1] != 0) {
        scale_in_place(imp_b, k);
        scale_in_place(imp_a, 0.5f);
    }
    if (b->info->flags[1] != 0) {
        scale_in_place(imp_a, k);
        scale_in_place(imp_b, 0.5f);
    }
}

// [driving] 0x1f9d98 RigidBody::ResolveCollision
int ResolveCollision(RigidBody* self, RigidBody* a, RigidBody* b, float normal[4], const float point[4],
                     CollisionInfo* out, float depth) {
    const RigidTuning& t = rigid_tuning();
    bool wake_a = false, wake_b = false;  // sp+0x80, sp+0x84
    // Os braços (sp+0x20, sp+0x30) e os impulsos (sp+0x60 sai de A, sp+0x70 entra em B): o w deles é lixo da pilha
    // no PS2 (nada o lê).
    alignas(16) float ra[4] = {}, rb[4] = {}, ia[4] = {}, ib[4] = {};
    {
        Reg p, pos, r;
        load(p, point);
        load(pos, &a->pos[0]);
        load(r, ra);
        sub(r, p, pos, XYZ);
        store(ra, r);
        load(pos, &b->pos[0]);
        load(r, rb);
        sub(r, p, pos, XYZ);
        store(rb, r);
    }
    const bool both_vehicles = a->type < 4 && b->type < 4;      // s2
    const bool both_objects = !(a->type < 4) && !(b->type < 4);  // s3
    ModifyLevers(a, b, ra, rb, point, normal, both_vehicles);
    // A velocidade relativa do ponto ao longo da normal.
    Reg va{}, vb{}, rel{}, w, r, v, n;
    load(w, &a->ang_vel[0]);
    load(r, ra);
    cross(va, w, r);
    load(v, &a->vel[0]);
    add(va, va, v, XYZ);
    load(w, &b->ang_vel[0]);
    load(r, rb);
    cross(vb, w, r);
    load(v, &b->vel[0]);
    add(vb, vb, v, XYZ);
    sub(rel, vb, va, XYZ);
    load(n, normal);
    const float vn = dot3(n, rel);  // f28
    if (ee::lt(10.0f, vn)) return 0;
    if (a->info->flags[0] != 0 || b->info->flags[0] != 0) return 1;
    out->unknown_30[0] = 1.0f;
    out->unknown_30[1] = 1.0f;
    // O tamanho do impulso.
    load(v, &b->vel[0]);
    load(w, &a->vel[0]);
    sub(rel, v, w, XYZ);
    float speed;  // f1
    if (a->type < 4 && b->type < 4) {
        load(n, normal);
        speed = ee::abs(dot3(n, rel));
    } else {
        speed = vu_sqrt(length_sq(rel));
    }
    const float push = ee::mul(t.speed_scale, speed);  // f23
    const float dfs = both_objects ? ee::sub(t.depth_force_scale, 1.0f) : t.depth_force_scale;
    float k = ee::max(push, ee::mul(depth, dfs));  // f24
    k = ee::max(k, 1.0f);
    load(v, &a->vel[0]);
    const float va2 = length_sq(v);  // f22
    load(v, &b->vel[0]);
    const float vb2 = length_sq(v);  // f20
    float lim = ee::div(ee::mul(t.total_force_limit, ee::max(va2, vb2)), t.unknown_120);
    lim = ee::max(lim, t.unknown_124);
    lim = ee::min(lim, t.total_force_limit);
    if (both_objects) lim = ee::mul(lim, ps2::ffloat(0x3ecccccdu));  // 0,4
    k = ee::min(ee::mul(k, k), lim);
    Reg imp{};  // sp+0x50
    load(n, normal);
    scale_xyz(imp, n, k);
    const float mass_sum = ee::add(a->mass, b->mass);  // f25

    if (!both_vehicles) {
        // Algum objeto comum.
        float ka, kb;  // f27, f26
        if (a->type < 4) {
            out->unknown_30[0] = 0.0f;
            ka = ps2::ffloat(0x3dcccccdu);  // 0,1
            ra[0] = ra[1] = ra[2] = 0.0f;
        } else {
            out->unknown_30[0] = 1.0f;
            ka = 1.0f;
        }
        if (b->type < 4) {
            out->unknown_30[1] = 0.0f;
            kb = ps2::ffloat(0x3dcccccdu);
            rb[0] = rb[1] = rb[2] = 0.0f;
        } else {
            out->unknown_30[1] = 1.0f;
            kb = 1.0f;
        }
        const float ma = a->mass, mb = b->mass;
        float share = (ee::lt(1500.0f, ma) && ee::lt(1500.0f, mb)) ? ee::add(ma, mb) : ee::mul(ee::min(ma, mb), 0.75f);
        bool standard = true;  // s2
        const bool counts = ((a->type == 2 || a->type == 3) && ee::lt(0.0f, owner_hit_points(a))) ||
                            ((b->type == 2 || b->type == 3) && ee::lt(0.0f, owner_hit_points(b))) ||
                            ((self->type == 2 || self->type == 3) && owner_9c(self) == 1);
        if (counts) {
            a->radius_flags &= ~2u;
            b->radius_flags &= ~2u;
        } else {
            // Um corpo parado com o bit 1 de +0x7c, atingido por um mais lento que 7 m/s e não mais leve, leva o
            // impulso todo (vezes max(SPEED_SCALE |Δv|, 1) e a massa do outro); senão perde o bit e o dono toca a
            // animação 12.
            if (((a->radius_flags >> 1) & 1u) && ee::eq(va2, 0.0f) && ee::lt(vb2, 49.0f) && ee::le(a->mass, b->mass)) {
                scaled_into(ia, imp, 0.0f);
                scaled_into(ib, imp, ee::mul(b->mass, ee::max(push, 1.0f)));
                out->unknown_30[0] = 0.0f;
                wake_b = true;
                standard = false;
            } else {
                a->radius_flags &= ~2u;
                owner_play_animation(a, 12);
            }
            if (((b->radius_flags >> 1) & 1u) && ee::eq(vb2, 0.0f) && ee::lt(va2, 49.0f) && ee::le(b->mass, a->mass)) {
                scaled_into(ia, imp, ee::mul(a->mass, ee::max(push, 1.0f)));
                scaled_into(ib, imp, 0.0f);
                out->unknown_30[1] = 0.0f;
                wake_a = true;
                standard = false;
            } else {
                b->radius_flags &= ~2u;
                owner_play_animation(b, 12);
            }
        }
        if (standard) {
            if (both_objects) share = ee::mul(share, 0.5f);
            const float cap = ps2::ffloat(0x3f28f5c3u);  // 0,66
            ka = ee::mul(ee::min(cap, ee::mul(ka, ee::div(b->mass, mass_sum))), share);
            kb = ee::mul(ee::min(cap, ee::mul(kb, ee::div(a->mass, mass_sum))), share);
            scaled_into(ia, imp, ka);
            scaled_into(ib, imp, kb);
            wake_b = true;
            wake_a = true;
        }
        limit_object_impulse(a, ia);
        limit_object_impulse(b, ib);
        apply_object_torque(a, ra, ia, false);
        apply_object_torque(b, rb, ib, true);
    } else {
        // Dois veículos: cada um leva a massa do outro / soma vezes min(soma, 3000) (o momento não se conserva).
        const float share = ee::min(mass_sum, 3000.0f);  // f21
        a->info->age = 0;
        float ka = 1.0f, kb = 1.0f;  // f23, f22
        wake_b = true;
        wake_a = true;
        b->info->age = 0;
        if (mission_uses_health() != 0) {
            // Um veículo "com vida" (entrada +0x9c em 1 ou 2, chamada de novo se a primeira não deu 1) e vida acima
            // de zero leva um quarto e o outro quatro vezes.
            bool da = owner_9c(a) == 1 || owner_9c(a) == 2;
            bool db = owner_9c(b) == 1 || owner_9c(b) == 2;
            if (ee::le(owner_hit_points(a), 0.0f)) da = false;
            if (ee::le(owner_hit_points(b), 0.0f)) db = false;
            if (da && !db) {
                ka = 0.25f;
                kb = 4.0f;
            }
            if (!da && db) {
                ka = 4.0f;
                kb = 0.25f;
            }
        }
        // Apoiado: a entrada +0x18c ou alavancas no chão. Um apoiado contra um no ar: o apoiado leva 0,2 e o outro,
        // se cai, tem o y do momento pela metade, o y do impulso que entra nele a 0,1 e não gira.
        const bool ground_a = owner_18c(a) != 0 || a->ground_levers > 0;  // s3
        const bool ground_b = owner_18c(b) != 0 || b->ground_levers > 0;  // s2
        bool hold_a = false, hold_b = false;                              // s5, s6
        const float fifth = ps2::ffloat(0x3e4ccccdu);                    // 0,2
        if (ground_a) {
            if (!ground_b) {
                ka = fifth;
                kb = 1.0f;
                if (ee::lt(b->momentum[1], 0.0f)) {
                    hold_b = true;
                    b->momentum[1] = ee::mul(b->momentum[1], 0.5f);
                }
            }
        } else if (ground_b) {
            ka = 1.0f;
            kb = fifth;
            if (ee::lt(a->momentum[1], 0.0f)) {
                hold_a = true;
                a->momentum[1] = ee::mul(a->momentum[1], 0.5f);
            }
        }
        scaled_into(ia, imp, ee::mul(ee::mul(ee::div(b->mass, mass_sum), ka), share));
        scaled_into(ib, imp, ee::mul(ee::mul(ee::div(a->mass, mass_sum), kb), share));
        // Quirk preservado: o 0,1 do y vai para o impulso do outro corpo (hold_a corta o de B e vice-versa).
        const float tenth = ps2::ffloat(0x3dcccccdu);
        if (hold_a) ib[1] = ee::mul(ib[1], tenth);
        if (hold_b) ia[1] = ee::mul(ia[1], tenth);
        ScaleObjObjForces(a, b, ia, ib, 225.0f, ps2::ffloat(0x3f866666u) /* 1,05 */, depth);
        if (hold_a) {
            if (ee::lt(10.0f, a->vel[1])) ia[0] = ia[1] = ia[2] = 0.0f;
            const float cap = ee::mul(a->mass, 10.0f);
            if (ee::lt(cap, ia[1])) ia[1] = cap;
        }
        if (hold_b) {
            if (ee::lt(10.0f, b->vel[1])) ib[0] = ib[1] = ib[2] = 0.0f;
            const float cap = ee::mul(b->mass, 10.0f);
            if (ee::lt(cap, ib[1])) ib[1] = cap;
        }
        apply_vehicle_torque(a, ra, ia, false, hold_a);
        apply_vehicle_torque(b, rb, ib, true, hold_b);
        ia[1] = 0.0f;
        ib[1] = 0.0f;
    }

    // O momento, depois v e ω de novo.
    {
        Reg m, i;
        load(m, &a->momentum[0]);
        load(i, ia);
        sub(m, m, i, XYZ);
        store(&a->momentum[0], m);
        load(m, &b->momentum[0]);
        load(i, ib);
        add(m, m, i, XYZ);
        store(&b->momentum[0], m);
    }
    refresh_velocities(a);
    refresh_velocities(b);
    std::memcpy(out->normal, normal, 16);
    std::memcpy(out->point, point, 16);
    out->speed = vn;
    float s = k;
    if (ee::lt(s, 0.0f)) s = ee::neg(s);
    out->strength = ee::min(ee::div(s, t.total_force_limit), 1.0f);
    if (wake_a) wake(a);
    if (wake_b) wake(b);
    return 1;
}

}  // namespace drv
