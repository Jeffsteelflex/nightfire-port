// Os choques das missões de carro (driving/physics/obb.h, collider.h, collide.h e as barreiras de world_collision.h):
// propriedades independentes (a sobreposição de duas caixas alinhadas, o cruzamento de dois segmentos, o círculo, a
// barreira de uma instância montada à mão e a normal virada para quem bate, a região do WCollider, dois corpos que se
// sobrepõem trocando momento). A conferência bit a bit contra o código original fica no nf_driving_check (parte 7).

#include <cmath>
#include <cstring>
#include <vector>

#include "driving/physics/collide.h"
#include "driving/physics/collider.h"
#include "driving/physics/obb.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "test_framework.h"

using namespace drv;

namespace {
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
const float kIdentity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
}  // namespace

// Duas caixas alinhadas: separadas não se tocam; sobrepostas em x por 0,3 m dão a menor sobreposição -0,3, o eixo x
// virado para a outra caixa e a quina dela que entra.
TEST(driving_obb_overlap_of_aligned_boxes) {
    const float half[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float ca[4] = {0, 0, 0, 0}, far_c[4] = {2.5f, 0, 0, 0}, cb[4] = {1.7f, 0.2f, 0.1f, 0};
    OBB a, b;
    OBB_Reset(a, kIdentity, ca, half);
    OBB_Reset(b, kIdentity, far_c, half);
    CHECK(a.penetration == -100000.0f && a.extent[0][0] == 1.0f && a.half[2] == 1.0f);
    CHECK(!OBB_CheckOBBOverlapAndFindIntersection(a, b));
    OBB_Reset(a, kIdentity, ca, half);
    OBB_Reset(b, kIdentity, cb, half);
    CHECK(OBB_CheckOBBOverlapAndFindIntersection(a, b));
    CHECK(near(a.penetration, -0.3f, 1e-5f));
    // O eixo da menor sobreposição é o x, virado de b para a (o impulso empurra b para +x).
    CHECK(near(std::fabs(a.normal[0]), 1.0f, 1e-6f) && a.normal[1] == 0.0f && a.normal[2] == 0.0f);
    // A quina de b mais funda dentro de a: x = 1,7 - 1.
    CHECK(near(a.contact[0], 0.7f, 1e-5f));
}

// Dois segmentos que se cruzam no plano xz: o ponto no primeiro (com o y dele) e w = 1; paralelos não se cruzam.
TEST(driving_world_segment_intersect_and_circle) {
    const float a[8] = {0, 0, 0, 0, 4, 2, 0, 0}, b[8] = {2, 9, -1, 0, 2, 9, 1, 0};
    float out[4] = {};
    CHECK(WWorldMath_SegmentIntersect(a, b, out));
    CHECK(near(out[0], 2.0f, 1e-6f) && near(out[1], 1.0f, 1e-6f) && near(out[2], 0.0f, 1e-6f) && out[3] == 1.0f);
    const float c[8] = {0, 0, 1, 0, 4, 0, 1, 0};
    CHECK(!WWorldMath_SegmentIntersect(a, c, out));
    const float d[8] = {5, 0, -1, 0, 5, 0, 1, 0};  // depois do fim do primeiro
    CHECK(!WWorldMath_SegmentIntersect(a, d, out));
    // O círculo de raio 1 em (2, 0): o segmento de x = 0 a 4 entra em t = 0,25.
    float t0 = 0, t1 = 0;
    CHECK(WWorldMath_IntersectCircle(t0, t1, 0, 0, 4, 0, 2, 0, 1));
    CHECK(near(t0, 0.25f, 1e-6f) && near(t1, 0.75f, 1e-6f));
    // As duas pontas dentro: zero e verdadeiro; longe: falso.
    CHECK(WWorldMath_IntersectCircle(t0, t1, 1.8f, 0, 2.2f, 0, 2, 0, 1) && t0 == 0.0f && t1 == 0.0f);
    CHECK(!WWorldMath_IntersectCircle(t0, t1, 0, 5, 4, 5, 2, 0, 1));
}

// Uma instância posta em (10, 0, 20) com uma barreira de x = -5 a 5 (z = 0 dela), da altura 0 a 3: a lista perto de
// (10, 1, 18) a tem no mundo; um segmento que a cruza bate nela com a normal virada para o começo dele; a máscara de
// barreiras a esconde.
TEST(driving_world_barrier_of_a_hand_built_instance) {
    struct Art {
        CollisionArticle head;
        CollisionBarrier bar;
    };
    std::vector<Art> art(1);
    std::memset(art.data(), 0, sizeof(Art));
    art[0].head.barrier_offset = 0;
    art[0].head.barrier_count = 1;
    art[0].bar = {-5.0f, 0.0f, 0.0f, 9, 0x20, 0x34, 5.0f, 3.0f, 0.0f, 0.1f};
    std::vector<WCollisionInstance> inst(1);
    std::memset(inst.data(), 0, sizeof(WCollisionInstance));
    inst[0].right[0] = 1.0f;
    inst[0].forward[2] = 1.0f;
    inst[0].half_x = inst[0].half_z = 6.0f;
    inst[0].position[0] = -10.0f;
    inst[0].position[2] = -20.0f;
    inst[0].radius = 8.0f;
    inst[0].flags = 2;
    inst[0].article = nf::p32_u(art.data());
    WCollisionMgr mgr{};
    mgr.barrier_mask = 0x10;
    std::vector<InstanceListEntry> nearby = {{&inst[0], nullptr}};
    std::vector<BarrierListEntry> bars;
    const float centre[3] = {10.0f, 1.0f, 18.0f};
    WCollisionMgr_GetBarrierList(mgr, bars, nearby, centre, 3.0f);
    CHECK(bars.size() == 1);
    CHECK(bars[0].p0[0] == 5.0f && bars[0].p0[2] == 20.0f && bars[0].p1[0] == 15.0f && bars[0].p1[1] == 3.0f);
    CHECK(bars[0].index == 0 && bars[0].instance == nf::p32_u(&inst[0]));
    // Longe demais (no plano) ou acima da barreira: nada.
    std::vector<BarrierListEntry> none;
    const float far_c[3] = {10.0f, 1.0f, 10.0f}, high[3] = {10.0f, 9.0f, 19.0f};
    WCollisionMgr_GetBarrierList(mgr, none, nearby, far_c, 3.0f);
    WCollisionMgr_GetBarrierList(mgr, none, nearby, high, 3.0f);
    CHECK(none.empty());
    // A máscara de barreiras com o bit do subtipo (0x20): escondida.
    mgr.barrier_mask = 0x20;
    WCollisionMgr_GetBarrierList(mgr, none, nearby, centre, 3.0f);
    CHECK(none.empty());
    // O segmento de (10, 1, 18) a (10, 1, 22) cruza a parede em z = 20: a normal (0, 0, -1), para o começo.
    const float seg[8] = {10.0f, 1.0f, 18.0f, 0.0f, 10.0f, 1.0f, 22.0f, 0.0f};
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    CHECK(WCollisionMgr_GetBarrierNormal(mgr, bars, seg, info));
    CHECK(info.hit == 2 && near(info.point[2], 20.0f, 1e-5f) && near(info.point[0], 10.0f, 1e-5f));
    CHECK(info.normal[0] == 0.0f && near(info.normal[2], -1.0f, 1e-6f) && info.normal[1] == 0.0f);
    CHECK(info.face_type == 9 && info.face_subtype == 0x20 && info.flag52 == 1);
    CHECK(info.face_instance == nf::p32_u(&inst[0]));
    // Do outro lado, a normal vira.
    const float back[8] = {10.0f, 1.0f, 22.0f, 0.0f, 10.0f, 1.0f, 18.0f, 0.0f};
    WorldCollisionInfo_Init(info);
    CHECK(WCollisionMgr_GetBarrierNormal(mgr, bars, back, info) && near(info.normal[2], 1.0f, 1e-6f));
    // Acima da parede: passa.
    const float over[8] = {10.0f, 5.0f, 18.0f, 0.0f, 10.0f, 5.0f, 22.0f, 0.0f};
    WorldCollisionInfo_Init(info);
    CHECK(!WCollisionMgr_GetBarrierNormal(mgr, bars, over, info));
}

// A região do WCollider: sem região, a esfera do objeto com o raio x 1,1; andando, o centro vai 1,1 passo à frente e o
// raio cresce do passo (no máximo 1).
TEST(driving_collider_region) {
    const float p[3] = {1.0f, 2.0f, 3.0f}, prev[3] = {1.0f, 2.0f, 2.5f};
    float c[3];
    float r = 0;
    WCollider_FitRegion(false, p, prev, c, r, 2.0f);
    CHECK(c[0] == 1.0f && c[2] == 3.0f && near(r, 2.2f, 1e-5f));
    WCollider_FitRegion(true, p, prev, c, r, 2.0f);
    CHECK(near(c[2], 3.55f, 1e-5f) && near(r, 2.75f, 1e-5f));
    const float jump[3] = {1.0f, 2.0f, 13.0f};
    WCollider_FitRegion(true, jump, prev, c, r, 2.0f);
    CHECK(near(r, 3.3f, 1e-5f));  // o passo conta no máximo 1
    // Sem região montada, nada está dentro; com a região, a esfera inteira dentro dela.
    WCollider col;
    col.mask = WCollider::kBarriers;
    CHECK(!WCollider_InRegion(col, p, 1.0f, WCollider::kBarriers));
    col.region_valid = 1;
    std::memcpy(col.region_centre, p, 12);
    col.region_radius = 2.0f;
    CHECK(WCollider_InRegion(col, p, 1.0f, WCollider::kBarriers));
    CHECK(!WCollider_InRegion(col, jump, 1.0f, WCollider::kBarriers));
    CHECK(!WCollider_InRegion(col, p, 1.0f, WCollider::kInstances));  // a máscara pede o que a região não tem
}

// Dois objetos iguais sobrepostos (vagas 0 e 1), um indo de encontro ao outro a 14,5 m/s: CollideWithObject dá um evento
// e os impulsos opostos (o momento total não muda), e o da frente sai andando.
TEST(driving_collide_with_object_two_boxes) {
    reset_rigid_globals();
    simulation_reset(60);
    Simulation& sim = simulation();
    RigidBodyParams pa;
    pa.mass = 100.0f;
    pa.type = 4;
    pa.half_size[0] = pa.half_size[1] = pa.half_size[2] = 0.5f;
    pa.vel[0] = 14.5f;  // entre dois objetos a força passa de 0,05 (o evento) só acima de ~14,1 m/s
    RigidBodyParams pb = pa;
    pb.pos[0] = 0.6f;
    pb.vel[0] = 0.0f;
    RigidBody* a = simulation_add_body(sim, pa);
    RigidBody* b = simulation_add_body(sim, pb);
    CHECK(a && b && a->slot == 0 && b->slot == 1);
    a->state = b->state = 2;
    const float pa0 = a->momentum[0], pb0 = b->momentum[0];
    std::vector<CollisionInfo> events;
    collide_events().collision = [&events](const CollisionInfo& c) { events.push_back(c); };
    CollideWithObject(a, 0);
    collide_events().collision = nullptr;
    CHECK(events.size() == 1);
    CHECK(events.size() == 1 && events[0].kind == 4 && events[0].type == 4 && events[0].slot == 0 &&
          events[0].unknown_4d == 1);
    CHECK(a->momentum[0] < pa0 && b->momentum[0] > pb0);
    CHECK(near(a->momentum[0] + b->momentum[0], pa0 + pb0, 1e-2f));
    CHECK(b->vel[0] > 0.0f);
    // Longe: nada.
    b->pos[0] = 10.0f;
    const float pa1 = a->momentum[0];
    CollideWithObject(a, 0);
    CHECK(a->momentum[0] == pa1);
    simulation_reset(60);
}

// A caixa de um objeto do mundo parado à frente de um corpo que vem a 4 m/s: o impulso o empurra de volta pela
// normal (o momento ao longo dela cai) e a força vai ao evento.
TEST(driving_resolve_world_obb_collision_pushes_back) {
    reset_rigid_globals();
    simulation_reset(60);
    RigidBodyParams p;
    p.mass = 200.0f;
    p.type = 4;
    p.vel[2] = 4.0f;
    RigidBody* rb = simulation_add_body(simulation(), p);
    rb->state = 2;
    const float normal[4] = {0, 0, 1, 0}, point[4] = {0, 0, 0.5f, 1}, still[4] = {0, 0, 0, 0};
    CollisionInfo out{};
    const float pz = rb->momentum[2];
    CHECK(ResolveWorldOBBCollision(rb, normal, point, 0.1f, &out, still, 6000.0f));
    CHECK(rb->momentum[2] < pz && out.type == 6 && out.kind == 1 && out.strength > 0.0f && out.strength <= 1.0f);
    CHECK(rb->info->flags[1] == 1);
    // Afastando-se rápido de um objeto parado (mais de 5 m/s pela normal): nada.
    rb->vel[2] = -8.0f;
    rb->momentum[2] = -1600.0f;
    CHECK(!ResolveWorldOBBCollision(rb, normal, point, 0.1f, &out, still, 6000.0f));
    CHECK(rb->momentum[2] == -1600.0f);
    simulation_reset(60);
}
