// Os objetos de cena que o carro derruba (driving/game/smackable.h), do MIPS do DRIVING.ELF. As desmontagens ficam em
// out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - o som (ASmackable), os estímulos do RAnimEngine, o TriggerFX e a luz de sombra do RSceneObj (o byte +0x1a pelo
//    GetShadowBrightness) não existem: contados;
//  - o EWakeupSmackable só cria o Smackable (o Newton e o ramo dos raios das armas contados; sem objeto o gatilho volta a
//    valer, como no PS2);
//  - o RSceneObj +0xc (o 'pD') não é gravado no objeto: scene_object_desc o acha como o UseArticle/UseInstanceList o
//    gravaria;
//  - o WTrigger::UpdateRotPos não põe o gatilho na lista dinâmica da grade (o port não a tem): contado;
//  - o mapa de objetos da Simulation (a árvore por tipo) é o vetor smackables() e a ordem do Simulate é a das vagas.

#include "driving/game/smackable.h"

#include <algorithm>
#include <cstring>

#include "driving/carp_runtime.h"
#include "driving/game/attributes.h"
#include "driving/game/explosion.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/world_vis.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

SmackableWorld g_world;
SmackableStats g_stats;
std::vector<Smackable*> g_smackables;
// Os padrões do Smackable sem "PhysicsData" (0x3d0d58: a coleção "smackable"/"Dummy" com MASS 100, HITPOINTS 500).
SmackablePhysicsData g_dummy;

constexpr uint32_t kOne = 0x3f800000u;
uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// __fixunssfsi (0x24e340, o fptoui): truncado, com o ramo dos >= 2^31.
uint32_t fptoui(float x) {
    if (ee::lt(x, flt(0x4f000000u))) return uint32_t(ee::cvt_w(x));
    return uint32_t(ee::cvt_w(ee::sub(x, flt(0x4f000000u)))) + 0x80000000u;
}

// O raio das medidas empacotadas de uma instância (+0x3c): os 10 bits de baixo vezes o passo do bit 30 (0x386b80).
float packed_radius(uint32_t packed) {
    static const uint32_t kSteps[2] = {0x3e800000u, 0x41800000u};  // 0,25 e 16
    return ee::mul(ee::cvt_s(int32_t(packed & 0x3ff)), flt(kSteps[(packed >> 30) & 1]));
}

// UGroup::something (0x001c6fb8): o dado com a etiqueta e a variante (o índice da família curta), ou a variante 0.
UData* locate_variant(UGroup* g, uint32_t tag, uint32_t variant) {
    const uint32_t first = variant == 0xffffffffu ? tag : ((tag & 0xffff0000u) | variant);
    UData* d = UGroup_DataLocateTag(g, first);
    if (d == UGroup_DataEnd(g) && variant != 0) d = UGroup_DataLocateTag(g, tag & 0xffff0000u);
    return d == UGroup_DataEnd(g) ? nullptr : d;
}

uint32_t word_at(uint32_t p32, uint32_t off = 0) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p32) + off, 4);
    return v;
}

// O vmul.xyz + vmadday + vmaddz de um |v|².
uint32_t vu_len_sq(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    return ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
}

}  // namespace

SmackableWorld& smackable_world() { return g_world; }
SmackableStats& smackable_stats() { return g_stats; }
std::vector<Smackable*>& smackables() { return g_smackables; }

// ------------------------------------------------------------------------------------------------- o RSceneObj

uint32_t scene_object_desc(const RSceneObj& o) {
    // WWorld::Open: UseArticle(o, o grupo do modelo do 'pD' do artigo da instância, o byte +7 do 'ps  ').
    if (!o.instance) return 0;
    const uint32_t article = word_at(o.instance, 0x1c);
    if (!article) return 0;
    const uint32_t desc = word_at(article);
    if (!desc) return 0;
    UGroup* group = nf::p32_ptr<UGroup>(word_at(desc));
    if (!group) return desc;
    const uint32_t ps = WWorld_GetProcAnimStateFromInstance(o.instance);
    const uint32_t variant = ps ? nf::p32_ptr<const uint8_t>(ps)[7] : 0xffffffffu;
    if (UData* in = locate_variant(group, 0x696e2020u, variant)) {
        // UseInstanceList: o +0xc é o primeiro registro do artigo da primeira instância do grupo (zero sem artigo).
        const uint32_t first_article = word_at(nf::p32_u(UData_Data(in)), 0x1c);
        return first_article ? word_at(first_article) : 0;
    }
    // Sem 'in  ': a instância do mundo (o artigo dela).
    return desc;
}

// [driving] 0x001c9190 RSceneObj::GetTransform
void RSceneObj_GetTransform(const RSceneObj& o, float m[4][4]) {
    std::memcpy(m, nf::p32_ptr<const uint8_t>(o.instance), 64);
    m[3][3] = 1.0f;
    m[0][3] = m[1][3] = m[2][3] = 0.0f;
}

// [driving] 0x001c9778 RSceneObj::GetBoundingDimensions
void RSceneObj_GetBoundingDimensions(const RSceneObj& o, float out[3]) {
    const uint32_t desc = scene_object_desc(o);
    if (!desc) {
        ++g_stats.guards;  // o PS2 leria a caixa a partir do endereço 0
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const CarpBaseDesc& d = *nf::p32_ptr<const CarpBaseDesc>(desc);
    for (int i = 0; i < 3; ++i) out[i] = ee::mul(ee::sub(d.bbox_max[i], d.bbox_min[i]), 0.5f);
}

// [driving] 0x001c9c78 RSceneObj::GetVelocity
const float* RSceneObj_GetVelocity(const RSceneObj& o) {
    if (o.physics) {
        Smackable* s = nf::p32_ptr<Smackable>(o.physics);
        if (s->d.flags & 1) {
            ++g_stats.guards;  // um corpo simples (não portado)
            return nullptr;
        }
        return s->body()->vel;
    }
    if (o.path) ++g_stats.guards;  // o trilho do RPathEngine (+0x88) não foi portado
    return nullptr;
}

// [driving] 0x001c8b78 RSceneObj::Hide e 0x001c8b90 RSceneObj::Show
void RSceneObj_Hide(RSceneObj& o) { o.flags &= ~0x01000000u; }
void RSceneObj_Show(RSceneObj& o) { o.flags |= 0x01000000u; }

// [driving] 0x001c9b28 RSceneObj::SetPhysics
void RSceneObj_SetPhysics(RSceneObj& o, Smackable* physics) {
    const uint32_t p = nf::p32_u(physics);
    if (o.physics == p) return;
    if (o.physics) {
        // A instância na pose do corpo antigo: VU0_SQTquattom4((1, 1, 1, 1), o quatérnio, a posição com w 1).
        Smackable* old = nf::p32_ptr<Smackable>(o.physics);
        if (old->d.flags & 1) {
            ++g_stats.guards;  // um corpo simples (não portado)
        } else {
            const RigidBody* b = old->body();
            float m[4][4];
            VU0_quattom4(b->quat, m);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) m[r][c] = flt(ps2::mul(bits(m[r][c]), kOne));
            uint8_t* inst = nf::p32_ptr<uint8_t>(o.instance);
            for (int r = 0; r < 3; ++r) std::memcpy(inst + 16 * r, m[r], 12);
            std::memcpy(inst + 0x30, b->pos, 12);
            g_world.moved.push_back(o.instance);
        }
    }
    o.physics = p;
    if (o.unknown30[0]) ++g_stats.guards;  // o alvo da mira (DisableTarget/EnableTarget) não foi portado
}

// [driving] 0x0022c530 WTree::CheckContainment
bool WTree_CheckContainment(const MapNode& node, const float box[4], const float sphere[4]) {
    static const uint32_t kScale[12] = {0x3f800000, 0x3f000000, 0x3e800000, 0x3e000000, 0x3d800000, 0x3d000000,
                                        0x3c800000, 0x3c000000, 0x3b800000, 0x3b000000, 0x3a800000, 0x3a000000};
    if (node.depth >= 12) {
        ++g_stats.guards;  // a tabela 0x33aa50 tem 12 entradas (o PS2 leria o que vem depois)
        return false;
    }
    const float h = ee::mul(box[3], flt(kScale[node.depth]));
    const float z = ee::mul(ee::cvt_s(int32_t(node.z)), h);
    const float x = ee::mul(ee::cvt_s(int32_t(node.x)), h);
    const float cz = ee::sub(ee::add(ee::add(h, ee::add(z, z)), box[2]), box[3]);
    const float cx = ee::sub(ee::add(ee::add(h, ee::add(x, x)), box[0]), box[3]);
    const float r = sphere[3];
    if (!ee::le(ee::sub(cx, h), ee::sub(sphere[0], r))) return false;
    if (!ee::le(ee::add(sphere[0], r), ee::add(cx, h))) return false;
    if (!ee::le(ee::sub(cz, h), ee::sub(sphere[2], r))) return false;
    return ee::le(ee::add(sphere[2], r), ee::add(cz, h));
}

// [driving] 0x001c8d70 RSceneObj::UpdatePosition
// Divergência: o byte +0x1a (a luz de sombra do chão sob o corpo, RRenderSharedData::GetShadowBrightness) não é refeito.
void RSceneObj_UpdatePosition(RSceneObj& o, bool changed) {
    if (!changed) return;
    float m[4][4];
    Smackable* s = o.physics ? nf::p32_ptr<Smackable>(o.physics) : nullptr;
    if (s) {
        if (s->d.flags & 1) {
            ++g_stats.guards;  // um corpo simples (SimpleRigidBody::RecalcOrientMat, não portado)
            return;
        }
        const RigidBody* b = s->body();
        std::memcpy(m, b->info->orient, 64);
        std::memcpy(m[3], b->pos, 12);
        // GetRenderOffset do RSceneObj: 0 (o ramo que tira a linha 1 x o deslocamento não roda).
        uint8_t* inst = nf::p32_ptr<uint8_t>(o.instance);
        float old[3];
        std::memcpy(old, inst + 0x30, 12);
        float d[3];
        for (int i = 0; i < 3; ++i) d[i] = ee::sub(old[i], m[3][i]);
        const bool same = ee::lt(flt(vu_len_sq(d)), flt(0x3727c5acu));  // 1e-5
        for (int r = 0; r < 4; ++r) std::memcpy(inst + 16 * r, m[r], 12);
        g_world.moved.push_back(o.instance);
        if (same) return;
    } else {
        RSceneObj_GetTransform(o, m);
    }
    if (o.flags & 0x04000000u) return;
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(o.instance);
    uint32_t packed;
    std::memcpy(&packed, inst + 0x3c, 4);
    float sphere[4] = {m[3][0], m[3][1], m[3][2], packed_radius(packed)};
    if (ee::eq(sphere[3], 0.0f)) sphere[3] = 10.0f;
    const uint8_t* wmap = nf::p32_ptr<const uint8_t>(fgWorldMap());
    if (!wmap) {
        ++g_stats.guards;
        return;
    }
    float corner[3], size;
    std::memcpy(corner, wmap + 0x1c, 12);
    std::memcpy(&size, wmap + 0x2c, 4);
    const float half = ee::mul(size, 0.5f);
    const float box[4] = {ee::add(corner[0], half), corner[1], ee::add(corner[2], half), half};
    uint32_t root;
    std::memcpy(&root, wmap, 4);
    MapNode* node = nf::p32_ptr<MapNode>(o.map_node);
    if (!node || !WTree_CheckContainment(*node, box, sphere)) node = WTree_FindNode(nf::p32_ptr<MapNode>(root), box, sphere);
    RSceneObj_SetMapNode(o, node);
    if (s && s->world_pos_.valid && s->world_pos_.instance) ++g_stats.stimuli;  // GetShadowBrightness (contado)
}

// [driving] 0x00230c48 WWorld::GetProcAnimStateFromInstance
uint32_t WWorld_GetProcAnimStateFromInstance(uint32_t instance) {
    const SmackableWorld& w = g_world;
    if (instance < w.instances) return 0;
    if (instance >= w.instances + 0x40u * uint32_t(w.instance_count)) return 0;
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(instance);
    if (!(inst[0xc] & 0x10)) return 0;
    uint16_t idx;
    std::memcpy(&idx, inst + 0xe, 2);
    return w.proc_anims + 0x20u * idx;
}

// [driving] 0x00210840 Util_GenerateTensor
void Util_GenerateTensor(float out[3], float mass, float x, float y, float z) {
    if (ee::lt(ee::mul(ee::add(x, z), 5.0f), y)) {
        // Uma barra (o eixo y comprido): o cilindro de raio 2z.
        const float a = ee::mul(ee::add(z, z), ee::add(z, z));
        const float b = ee::add(ee::mul(x, x), ee::mul(y, y));
        const float k12 = ee::mul(mass, flt(0x3daaa8ebu));  // 1/12
        const float k4 = ee::mul(mass, 0.25f);
        const float k2 = ee::mul(mass, 0.5f);
        const float side = ee::add(ee::mul(k12, a), ee::mul(k4, b));
        const float yy = ee::div(1.0f, ee::mul(k2, b));
        const float xx = ee::div(1.0f, side);
        out[2] = yy;
        out[1] = xx;
        out[0] = xx;
        return;
    }
    const float y2 = ee::mul(y, y), z2 = ee::mul(z, z), x2 = ee::mul(x, x);
    const float a = ee::add(y2, z2), b = ee::add(x2, y2), c = ee::add(x2, z2);
    out[0] = ee::div(3.0f, ee::mul(mass, a));
    out[2] = ee::div(3.0f, ee::mul(mass, b));
    out[1] = ee::div(3.0f, ee::mul(mass, c));
}

// [driving] 0x0022cad8 WTrigger::UpdateRotPos
// Divergência: o WGrid::AddGridNodeDynamicElement (a lista dinâmica das células do gatilho movido) não existe no port.
bool WTrigger_UpdateRotPos(WTrigger& t, const float pos[3]) {
    const float h = t.height;
    t.position[0] = pos[0];
    t.position[1] = ee::sub(pos[1], ee::mul(h, 0.5f));
    t.position[2] = pos[2];
    if (t.shape == 1) {
        t.shape = 3;
        if (ee::lt(t.radius, h)) t.radius = ee::mul(h, flt(0x3f0ccccdu));  // 0,55
    }
    ++g_stats.dynamic_grid;
    return true;
}

// ------------------------------------------------------------------------------------------------- o Smackable

RigidBody* Smackable::body() const { return GetRigidBody(simulation(), d.slot); }
RSceneObj* Smackable::scene() const { return nf::p32_ptr<RSceneObj>(d.render); }

// [driving] 0x00195a30 PhysicsObject::GetHitPoints
float Smackable::hit_points() { return d.hit_point_loc ? *nf::p32_ptr<float>(d.hit_point_loc) : 1.0f; }

// [driving] 0x00195ae0 PhysicsObject::GetCollisionGeometry e 0x001c98f8 RSceneObj::GetCollisionGeometry
const CollisionPoint* Smackable::collision_geometry(uint32_t& count, float& y_offset) {
    const RSceneObj* o = scene();
    if (!o) {
        count = 0;
        y_offset = 0;
        return nullptr;
    }
    y_offset = ee::neg(0.0f);  // -GetRenderOffset (0)
    const uint32_t desc = scene_object_desc(*o);
    if (!desc) {
        ++g_stats.guards;
        count = 0;
        return nullptr;
    }
    count = word_at(desc, 0x3c);
    return reinterpret_cast<const CollisionPoint*>(nf::p32_ptr<const uint8_t>(desc) + 0x60);
}

// [driving] 0x0019e100 Smackable::Smackable
RigidBody* Smackable_Construct(Smackable& s, RSceneObj& scene, WTrigger* trigger, const float velocity[3],
                               uint32_t sleep_delay) {
    Simulation& sim = simulation();
    SmackableData& d = s.d;
    const uint32_t desc = scene_object_desc(scene);
    if (!desc) ++g_stats.guards;  // o PS2 leria a PhysicsData do endereço 0xc
    const uint32_t phys = desc ? word_at(desc, 0xc) : 0;
    // PhysicsObject::PhysicsObject(atributos, 2): o WWorldPos, a vaga (AssignRigidBodySlot: a primeira livre), o
    // AttributeSet da PhysicsData.
    s.world_pos_ = WWorldPos{};
    d.type = 2;
    d.flags = 0;
    d.slot = -1;
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (!sim.owners[i]) {
            d.slot = int16_t(i);
            sim.owners[i] = &s;
            break;
        }
    d.attributes = phys;
    d.vtable = 0x37cec0;
    d.sleep_delay = sleep_delay;
    d.awake_timer = fptoui(ee::mul(ee::cvt_s(sim.frame_rate), 10.0f));
    d.trigger = nf::p32_u(trigger);
    d.params = phys ? phys : nf::p32_u(&g_dummy);
    d.proc_anim = WWorld_GetProcAnimStateFromInstance(scene.instance);
    const SmackablePhysicsData& params = *nf::p32_ptr<const SmackablePhysicsData>(d.params);
    d.hit_points = params.hit_points;
    if (d.proc_anim) {
        // A vida menos o dano guardado no 'ps  ' (o byte +4 / 256 da vida).
        const float frac = ee::mul(ee::cvt_s(int32_t(nf::p32_ptr<const uint8_t>(d.proc_anim)[4])), flt(0x3b800000u));
        d.hit_points = ee::sub(d.hit_points, ee::mul(frac, params.hit_points));
    }
    d.hit_point_loc = nf::p32_u(&d.hit_points);  // SetHitPointLoc (ValidateHitPoints: nada)
    ++g_stats.stimuli;                            // o ASmackable (o som) não foi construído
    d.render = nf::p32_u(&scene);                 // SetRenderObject
    RSceneObj_SetPhysics(scene, &s);
    if (d.slot < 0) {
        ++g_stats.guards;  // sem vaga: o PS2 construiria o corpo na vaga -1
        return nullptr;
    }
    float m[4][4];
    RSceneObj_GetTransform(scene, m);
    float dims[3];
    RSceneObj_GetBoundingDimensions(scene, dims);
    float tensor[3];
    Util_GenerateTensor(tensor, params.mass, dims[0], dims[1], dims[2]);
    const bool moving = ee::lt(0.0f, flt(vu_len_sq(velocity)));
    int32_t flag2;
    if (!trigger) flag2 = int32_t(params.force_detach);
    else flag2 = (trigger->flags & 0x100) ? (params.force_detach != 0 ? 1 : 0) : 0;
    RigidBodyParams p;
    p.mass = params.mass;
    p.slot = d.slot;
    p.type = 4;
    std::memcpy(p.pos, m[3], 12);
    std::memcpy(p.vel, velocity, 12);
    std::memset(p.ang_vel, 0, sizeof p.ang_vel);
    std::memcpy(p.orient, m, 64);
    std::memcpy(p.inv_inertia, tensor, 12);
    std::memcpy(p.half_size, dims, 12);
    p.half_size[3] = 0.0f;  // o w do PS2 é o que a pilha tinha em sp+0x7c
    p.owner = &s;
    p.moving = moving ? 1 : 0;
    p.flag2 = flag2;
    RigidBody* rb = GetRigidBody(sim, d.slot);
    RigidBody_Construct(rb, p);
    return rb;
}

// [driving] 0x0019e718 Smackable::Simulate (com o PhysicsObject::Simulate 0x00195aa8)
void Smackable::simulate() {
    Simulation& sim = simulation();
    if (d.actor) ++g_stats.guards;  // o ator (+0x4c) não existe no port
    const RigidBody* b = body();
    // vsqrt de |v|² (vmadday): acima de 0,1 m/s o relógio de 10 s volta ao começo.
    const float speed = flt(ps2::add(0, ps2::sqrt(vu_len_sq(b->vel))));
    if (ee::lt(flt(0x3dcccccdu), speed)) d.awake_timer = fptoui(ee::mul(ee::cvt_s(sim.frame_rate), 10.0f));
    if (d.awake_timer != 0) d.awake_timer -= 1;
    // A animação do RAnimEngine (+0x48 -> +0x28) e o ator: nenhum no port.
    if (b->state != 2) {
        if (d.sleep_delay != 0) d.sleep_delay -= 1;
        else Smackable_GoToSleep(*this);
    }
    if (RSceneObj* o = scene()) RSceneObj_UpdatePosition(*o, true);  // PhysicsObject::Simulate: o render +0x78
}

// [driving] 0x0019e610 Smackable::GoToSleep
void Smackable_GoToSleep(Smackable& s) {
    Simulation& sim = simulation();
    SmackableData& d = s.d;
    RSceneObj* o = s.scene();
    ++g_stats.asleep;
    if (d.trigger) {
        WTrigger* t = nf::p32_ptr<WTrigger>(d.trigger);
        const RigidBody* b = s.body();
        bool back;
        if ((b->radius_flags >> 1) & 1) {
            t->flags = uint16_t(t->flags | 0x100);
            back = true;
        } else {
            t->flags = uint16_t(t->flags & ~0x100);
            back = WTrigger_UpdateRotPos(*t, b->pos);
        }
        if (back) {
            t->flags = uint16_t(t->flags | kTrigEnabled);
            // O dano no 'ps  ': -(((vida + máxima - 1) << 8) / máxima), em bytes (divu; máxima 0: break 7).
            const SmackablePhysicsData& params = *nf::p32_ptr<const SmackablePhysicsData>(d.params);
            const uint32_t max_hp = fptoui(params.hit_points);
            const uint32_t cur = fptoui(d.hit_points);
            if (max_hp == 0) {
                ++g_stats.guards;
            } else if (d.proc_anim) {
                const uint32_t v = ((cur + max_hp - 1u) << 8) / max_hp;
                nf::p32_ptr<uint8_t>(d.proc_anim)[4] = uint8_t(0u - v);
            } else {
                ++g_stats.guards;  // o PS2 escreveria no endereço 4
            }
        } else if (o) {
            RSceneObj_Hide(*o);
            ++g_stats.hidden;
        }
    }
    DeleteSmackableObject(sim, s);
}

// [driving] 0x0019e8c0 Smackable::ApplyDamage
// Divergência: os estímulos do RAnimEngine, o TriggerFX do render (+0x80) e o som não existem (contados).
int32_t Smackable::apply_damage(const float from[3], const float to[3], float amount, float /*split*/, int32_t kind) {
    ++g_stats.damage;
    bool destroyed = false;
    if (ee::lt(0.0f, hit_points())) {
        // PhysicsObject::LoseHitPoints (0x00195a58)
        if (d.hit_point_loc) {
            const float dmg = ee::lt(amount, 0.0f) ? 0.0f : amount;
            float& hp = *nf::p32_ptr<float>(d.hit_point_loc);
            hp = ee::sub(hp, dmg);
            if (ee::lt(hp, 0.0f)) hp = 0.0f;
        }
        if (ee::le(hit_points(), 0.0f)) destroyed = true;
    } else {
        destroyed = true;
    }
    if (kind == 2 && ee::le(amount, 10.0f)) {
        // Um tiro: a força ao longo de de -> para, vezes dano x 1600, o y pelo menos dano x 160, em dobro; sem torque
        // (PhysicsObject::ApplyForces 0x00195728: ResolveForce, ResolveTorque e o bit 0 de +0x7c).
        const float k = ee::mul(amount, 1600.0f);
        Reg a, b, dir{};
        load(a, to);
        load(b, from);
        sub(dir, a, b, XYZ);
        const float dv[3] = {f(dir, 0), f(dir, 1), f(dir, 2)};
        const uint32_t q = ps2::rsqrt(kOne, vu_len_sq(dv));
        float force[4] = {0, 0, 0, 0};
        for (int i = 0; i < 3; ++i) force[i] = flt(ps2::mul(ps2::mul(bits(dv[i]), q), bits(k)));
        const float y = ee::max(force[1], ee::mul(k, flt(0x3dcccccdu)));
        force[1] = ee::add(y, y);
        const float torque[4] = {0, 0, 0, 0};
        if (d.flags & 1) {
            ++g_stats.guards;  // um corpo simples (SimpleRigidBody::Accelerate, não portado)
        } else {
            RigidBody* rb = body();
            ResolveForce(rb, force);
            ResolveTorque(rb, torque);
            rb->radius_flags |= 1u;
        }
    }
    ++g_stats.stimuli;  // os estímulos (destruído ou pela fração de vida) e o TriggerFX
    (void)destroyed;
    return 0x10;
}

// [driving] 0x0019e590 Smackable::~Smackable e 0x00195568 PhysicsObject::~PhysicsObject
Smackable::~Smackable() {
    RSceneObj* o = scene();
    if (d.trigger) {
        if (o) RSceneObj_SetPhysics(*o, nullptr);  // SetRenderObject(0)
        d.render = 0;
    } else if (o) {
        // Sem gatilho o PhysicsObject apagaria o RSceneObj (a entrada +0x8): o port não apaga um objeto do mundo.
        RSceneObj_SetPhysics(*o, nullptr);
        ++g_stats.guards;
    }
    // Simulation::ReleaseRigidBodySlot
    Simulation& sim = simulation();
    if (d.slot >= 0 && d.slot < kRigidBodySlots && sim.owners[d.slot] == this) {
        RigidBody* rb = GetRigidBody(sim, d.slot);
        if (rb->info) RigidBody_Destruct(rb);
        sim.owners[d.slot] = nullptr;
    }
    ++g_stats.deleted;
}

// ------------------------------------------------------------------------------------------------- a Simulation

int32_t RigidBodiesInUse(const Simulation& sim) {
    int32_t n = 0;
    for (int i = 0; i < kRigidBodySlots; ++i) n += sim.owners[i] != nullptr;
    return n;
}

// [driving] 0x002012d0 Simulation::CanSpawnRigidBody
bool CanSpawnRigidBody(Simulation& sim, const float pos[3], bool more) {
    const int32_t limit = more ? 0x30 : 0x24;
    if (!(limit < RigidBodiesInUse(sim))) return RigidBodiesInUse(sim) < limit;
    Smackable* far = nullptr;
    float best = 0.0f;
    for (Smackable* s : g_smackables) {
        const RigidBody* b = s->body();
        if (b->state == 2) continue;
        const float dx = ee::sub(b->pos[0], pos[0]), dy = ee::sub(b->pos[1], pos[1]), dz = ee::sub(b->pos[2], pos[2]);
        const float d2 = ee::add(ee::add(ee::mul(dx, dx), ee::mul(dy, dy)), ee::mul(dz, dz));
        if (ee::lt(best, d2)) {
            far = s;
            best = d2;
        }
    }
    if (far) Smackable_GoToSleep(*far);
    return RigidBodiesInUse(sim) < limit;
}

// [driving] 0x001ffa90 Simulation::SpawnSmackableObject
Smackable* SpawnSmackableObject(Simulation& sim, RSceneObj& scene, WTrigger* trigger, const float velocity[3],
                                uint32_t sleep_delay) {
    float m[4][4];
    RSceneObj_GetTransform(scene, m);
    if (!CanSpawnRigidBody(sim, m[3], false)) {
        ++g_stats.no_room;
        return nullptr;
    }
    if (!scene_obj_body_pos()) {
        scene_obj_body_pos() = [](uint32_t physics, float pos[3]) -> bool {
            const Smackable* sm = nf::p32_ptr<const Smackable>(physics);
            if (sm->d.flags & 1) return false;  // um corpo simples (não portado)
            std::memcpy(pos, sm->body()->pos, 12);
            return true;
        };
    }
    Smackable* s = new Smackable();
    Smackable_Construct(*s, scene, trigger, velocity, sleep_delay);
    g_smackables.push_back(s);  // Simulation::SpawnPhysicsObject (o vetor dos Smackable, +0xde4)
    return s;
}

// [driving] 0x002019a0 Simulation::DeleteSmackableObject: sai do vetor e vai ao DeletePhysicsObject (0x00200f18,
// game/explosion.cpp: o conjunto dos mortos).
void DeleteSmackableObject(Simulation& sim, Smackable& s) {
    auto it = std::find(g_smackables.begin(), g_smackables.end(), &s);
    if (it == g_smackables.end()) return;  // já saiu do mapa
    g_smackables.erase(it);
    DeletePhysicsObject(sim, &s);
}

// ------------------------------------------------------------------------------------------------- o evento

WakeupSmackable WakeupSmackable_FromData(const uint8_t* data, uint32_t trigger, uint32_t has_ray, uint32_t flag,
                                         int32_t index) {
    WakeupSmackable e{};
    std::memcpy(&e.scene, data + 4, 4);
    std::memcpy(&e.instance, data + 8, 4);
    std::memcpy(&e.proc_anim, data + 0xc, 4);
    e.has_ray = has_ray;
    e.trigger = trigger;
    e.flag = flag;
    e.index = index;
    return e;
}

// [driving] 0x0015de48 EWakeupSmackable::~EWakeupSmackable
void EWakeupSmackable_Execute(const WakeupSmackable& e) {
    Simulation& sim = simulation();
    uint32_t scene = e.scene;
    if (!scene) {
        if (!e.proc_anim) {
            ++g_stats.guards;  // o PS2 leria o objeto do endereço 8
            return;
        }
        scene = word_at(e.proc_anim, 8);
        if (!scene) {
            ++g_stats.guards;  // printf("EWakeupSmackable - no scene object")
            return;
        }
    }
    if (!e.proc_anim) {
        ++g_stats.guards;
        return;
    }
    const uint32_t flags = word_at(e.proc_anim, 4) >> 8;
    Smackable* s = nullptr;
    if (flags & 2) {
        RSceneObj& o = *nf::p32_ptr<RSceneObj>(scene);
        float vel[3] = {0.0f, 0.0f, 0.0f};  // 0x355b80
        if (const float* v = RSceneObj_GetVelocity(o)) std::memcpy(vel, v, 12);
        const uint32_t delay = fptoui(ee::mul(ee::cvt_s(sim.frame_rate), flt(0x3dcccccdu)));  // 0,1 s
        s = SpawnSmackableObject(sim, o, nf::p32_ptr<WTrigger>(e.trigger), vel, delay);
        if (s) ++g_stats.woken;
    } else if (flags & 0x40) {
        ++g_stats.newton;  // Simulation::SpawnNewtonObject (não portado): sem objeto
    }
    if (!s) {
        if (WTrigger* t = nf::p32_ptr<WTrigger>(e.trigger)) t->flags = uint16_t(t->flags | kTrigEnabled);
        return;
    }
    if (e.has_ray && !(flags & 0x40)) ++g_stats.rays;  // o raio de uma arma (não portado)
}

}  // namespace drv
