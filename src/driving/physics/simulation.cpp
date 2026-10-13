// O passo da simulação dos corpos rígidos (driving/physics/simulation.h), do DRIVING.ELF (Simulation.obj e o
// ESimFrameUpdate de DTuningFile.obj).

#include "driving/physics/simulation.h"

#include <cstring>
#include <memory>

#include "driving/physics/collide.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {
std::unique_ptr<Simulation> g_sim;
BasicOwner g_basic_owners[kRigidBodySlots];
WWorldPos g_basic_owner_pos[kRigidBodySlots];
}  // namespace

Simulation& simulation() {
    if (!g_sim) simulation_reset();
    return *g_sim;
}

// [driving] 0x1feda0 (sem nome: a parte do começo da Simulation que zera o estado e calcula o passo)
void simulation_reset(int frame_rate) {
    if (g_sim) {
        for (int i = 0; i < kRigidBodySlots; ++i)
            if (g_sim->owners[i] && g_sim->body_storage[i].info) RigidBody_Destruct(&g_sim->body_storage[i]);
    }
    // A mesma memória a cada reinício (os endereços dos corpos não mudam entre duas execuções do nf_driving_check).
    if (!g_sim) g_sim = std::make_unique<Simulation>();
    else *g_sim = Simulation{};
    Simulation& s = *g_sim;
    s.bodies = s.body_storage;
    s.scratchpad_bodies = s.scratchpad_storage;
    s.infos = s.info_storage;
    s.frame_rate = frame_rate;                                  // GetFoundationVideoModeRate(): 60 no NTSC
    s.dt = ee::div(1.0f, ee::cvt_s(frame_rate));                // 1 / 60 truncado: 0x3c888888
    s.state = 0;
    s.steps = 0;
    for (int i = 0; i < kRigidBodySlots; ++i) {
        g_basic_owners[i] = BasicOwner{};
        g_basic_owner_pos[i] = WWorldPos{};
        g_basic_owners[i].set_world_pos(&g_basic_owner_pos[i]);
    }
}

// [driving] 0x1ff3c8 Simulation::GetScratchPadFreeZone
RigidBodyScratch* GetScratchPadFreeZone(Simulation& sim) { return &sim.scratch; }

// [driving] 0x1ff0d8 Simulation::GetRigidBodyInfo
RigidBodyInfo* GetRigidBodyInfo(Simulation& sim, int slot) { return &sim.infos[slot]; }

// [driving] 0x1ff088 (o corpo da vaga)
RigidBody* GetRigidBody(Simulation& sim, int slot) {
    return sim.use_scratchpad ? &sim.scratchpad_bodies[slot] : &sim.bodies[slot];
}

// [driving] 0x1ff2e8 Simulation::CopyRigidBodiesToScratchPad
void CopyRigidBodiesToScratchPad(Simulation& sim) {
    ResetRigidBodySP();
    std::memcpy(sim.scratchpad_bodies, sim.bodies, sim.scratchpad_bodies_size);  // DMA para o scratchpad
    sim.use_scratchpad = 1;
}

// [driving] 0x1ff358 Simulation::CopyRigidBodiesFromScratchPad
void CopyRigidBodiesFromScratchPad(Simulation& sim) {
    std::memcpy(sim.bodies, sim.scratchpad_bodies, sim.scratchpad_bodies_size);
    sim.use_scratchpad = 0;
}

// [driving] 0x1fef20 Simulation::AdvanceStep
void AdvanceStep(Simulation& sim) {
    if (sim.state != 3) ++sim.steps;
}

// [driving] 0x1ff3d8 Simulation::SimulateGame
void SimulateGame(Simulation& sim) {
    if (sim.state == 3) return;
    upload_vu0_physics();  // VU0_UploadMicrocode(0x324e60, 0, 0)
    for (int slot = 0; slot < kRigidBodySlots; ++slot) {
        if (!sim.owners[slot]) continue;
        RigidBody* rb = GetRigidBody(sim, slot);
        if (rb->state != 3 && rb->state != 0) CollideWithObject(rb, slot);
    }
    // WCollisionMgr +0x30 = 1 durante a colisão com o mundo, 0 na do chão, 1 nos Simulate dos objetos.
    WorldCollision* w = world();
    if (w) w->set_vu0_path(1);
    for (int slot = 0; slot < kRigidBodySlots; ++slot) {
        if (!sim.owners[slot]) continue;
        RigidBody* rb = GetRigidBody(sim, slot);
        if (rb->state == 3) continue;
        RigidBodyInfo* info = rb->info;
        if (info->flags[0] != 0 && info->flags[3] == 0) info->flags[0] = 0;
        if (rb->state == 2) CollideWithWorld(rb);
    }
    if (w) w->set_vu0_path(0);
    for (int slot = 0; slot < kRigidBodySlots; ++slot) {
        if (!sim.owners[slot]) continue;
        RigidBody* rb = GetRigidBody(sim, slot);
        if (rb->state != 3 && rb->state == 2) CollideWithGround(rb);
    }
    Resolve_CollectVU0data();
    for (int slot = 0; slot < kRigidBodySlots; ++slot) {
        if (!sim.owners[slot]) continue;
        RigidBody* rb = GetRigidBody(sim, slot);
        if (rb->state != 3 && rb->state == 2) {
            ApplyAngularDamping(rb);
            UpdatePositionAndOrientation(rb, 1.0f);
        }
    }
    Timestep_CollectVU0data();
    for (int slot = 0; slot < kRigidBodySlots; ++slot) {
        if (!sim.owners[slot]) continue;
        RigidBody* rb = GetRigidBody(sim, slot);
        if (rb->state != 3 && rb->state == 2) ApplyInitialForcesAndTorques(rb);
    }
    for (int slot = 0; slot < kSimpleBodySlots; ++slot)
        if (sim.simple_owners[slot]) physics_pending("SimpleRigidBody::UpdatePosition");
    physics_pending("Simulation::DetectCollisionsSRB");
    // Os Simulate dos objetos (a entrada +0x2c da tabela virtual), pelo mapa da Simulation +0xe68. Port: os donos dos
    // corpos rígidos na ordem das vagas (o mapa do PS2 é uma árvore; com um objeto só a ordem não importa).
    if (w) w->set_vu0_path(1);
    for (int slot = 0; slot < kRigidBodySlots; ++slot)
        if (sim.owners[slot]) sim.owners[slot]->simulate();
    physics_pending("Simulation::UpdateTrackedInstanceVisibility");
}

// [driving] 0x159d10 ESimFrameUpdate::~ESimFrameUpdate
void ESimFrameUpdate(Simulation& sim) {
    physics_pending("Simulation::DeleteDeadObjects");
    CopyRigidBodiesToScratchPad(sim);
    AdvanceStep(sim);
    SimulateGame(sim);
    // O resto do quadro do jogo (atores, missão, câmera, gatilhos, a grade dinâmica) não foi portado.
    physics_pending("ESimFrameUpdate: o resto do jogo");
    if (sim.state != 3) {
        sim.clock = ee::add(sim.clock, 0.016666668f);  // a constante do código (não o dt da Simulation)
        sim.frames += 1;
    }
    CopyRigidBodiesFromScratchPad(sim);
}

RigidBody* simulation_add_body(Simulation& sim, RigidBodyParams params, PhysicsObject* owner_tag) {
    int slot = -1;
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (!sim.owners[i]) {
            slot = i;
            break;
        }
    if (slot < 0) return nullptr;
    PhysicsObject* owner = owner_tag ? owner_tag : params.owner;
    if (!owner) owner = &g_basic_owners[slot];
    sim.owners[slot] = owner;
    params.owner = owner;
    params.slot = slot;
    RigidBody* rb = GetRigidBody(sim, slot);
    RigidBody_Construct(rb, params);
    return rb;
}

}  // namespace drv
