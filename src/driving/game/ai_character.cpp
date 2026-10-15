// Os personagens da IA das missões de carro (driving/game/ai_character.h): a base AICharacter, o Human e as mãos de
// Bond, do MIPS do DRIVING.ELF. As divergências estão no cabeçalho e em cada função.

#include "driving/game/ai_character.h"

#include <array>
#include <cstring>
#include <map>

#include "driving/ai/sim_random.h"
#include "driving/anim/act_actor.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/camera_math.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/weapons.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/real/vu0_math.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
namespace an = drv::anim;

namespace {

AICharacterStats g_stats;
AICharacterEnv g_env;
HandsGlobals g_hands;
std::map<uint32_t, AIVtable> g_vtables;
std::vector<std::array<uint32_t, 4>> g_trace;
std::vector<Human*> g_humans;  // port: os Human criados (o PS2 os apaga no fim da Simulation)
bool g_trace_on = false;
int32_t g_zoom_button_local = 0;  // o fgZoomButton sem a câmera do port

void note(uint32_t fn, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
    if (g_trace_on) g_trace.push_back({fn, a, b, c});
}

AICharacterData& C(uint32_t self) { return *nf::p32_ptr<AICharacterData>(self); }
AICharacterActorApi& api() { return ai_character_actor_api(); }
AICharacterHandsData& HD(uint32_t self) { return *nf::p32_ptr<AICharacterHandsData>(self); }

// O ActActor de um handle (o +8 do nó); nulo: o PS2 leria o nó nulo.
uint32_t actor_of_handle(uint32_t handle) {
    const uint32_t a = handle ? an::act_actor_of(handle) : 0u;
    if (!a) ++g_stats.guards;
    return a;
}
uint32_t actor_of(uint32_t self) { return actor_of_handle(C(self).actor); }

// As tabelas das mãos (por arma 1..7; o índice 0 não é usado).
constexpr int32_t kArmAnim[8] = {-1, 1, 2, 4, 0, 1, 0, 1};               // 0x3274b0
constexpr int32_t kIdleAnim[8] = {-1, 6, 7, 1, 6, 7, 5, 6};              // 0x3274d0
constexpr int32_t kUnarmAnim[8] = {-1, -1, -1, 3, -1, -1, -1, -1};       // 0x3274f0
constexpr int32_t kReloadAnim[8] = {-1, 8, 9, 5, 8, 9, 7, 8};            // 0x327510
constexpr int32_t kFireAnim[8][2] = {{-1, -1}, {4, 4}, {5, 3}, {0, 0},   // 0x327530 (os dois lados do tiro)
                                     {4, 4},   {5, 3}, {3, 3}, {4, 2}};
constexpr int32_t kZoomInAnim[8] = {-1, -1, -1, 7, -1, -1, -1, -1};      // 0x327570 (a mira entrando)
constexpr int32_t kZoomOutAnim[8] = {-1, -1, -1, 6, -1, -1, -1, -1};     // 0x327590 (a mira saindo)
constexpr int32_t kStartAnim[8] = {-1, 8, 9, 2, 8, 9, 7, 8};             // 0x3275b0
constexpr const char* kHandChars[8] = {nullptr,           "5.a_ssm.b_ssm.110", "5.a_ssm.b_ssm.110",
                                       "6.a_snipe.b_snipe.70", "7.a_ul.b_ul.140", "7.a_ul.b_ul.140",
                                       "8.a_fod.b_fod.100", "8.a_fod.b_fod.100"};  // 0x3275d0
constexpr const char* kHandWeapons[8] = {nullptr, "ssm_mg", "ssm_gr", "sniper", "ul_mg", "ul_gr", "fod_mg",
                                         "fod_gr"};  // 0x3275f0
constexpr const char* kHandPos[8] = {nullptr,          "AIC_HSSNOW_POS", "AIC_HSSNOW_POS", "AIC_HSNIPE_POS",
                                     "AIC_HULTRA_POS", "AIC_HULTRA_POS", "AIC_HFOD_POS",   "AIC_HFOD_POS"};  // 0x327610
constexpr const char* kHandRot[8] = {nullptr,          "AIC_HSSNOW_ROT", "AIC_HSSNOW_ROT", "AIC_HSNIPE_ROT",
                                     "AIC_HULTRA_ROT", "AIC_HULTRA_ROT", "AIC_HFOD_ROT",   "AIC_HFOD_ROT"};  // 0x327630
constexpr int32_t kHandBank[8] = {0, 0xc, 0xc, 0xe, 0x10, 0x10, 0xf, 0xf};  // 0x327650

// Um índice de arma fora de 0..7 (o PS2 leria a memória em volta das tabelas).
bool hand_index_ok(int32_t i) {
    if (i >= 0 && i < 8) return true;
    ++g_stats.guards;
    return false;
}
uint32_t hands_handle(int32_t i) { return hand_index_ok(i) ? g_hands.actor[i] : 0u; }

const uint8_t* camera() { return g_env.camera ? g_env.camera() : nullptr; }

// RPlayerCamState::AimZoom (0x001be570) e AimRelease (0x001be660) sobre o estado da câmera (o +0x354).
void camera_state_call(uint32_t fn) {
    note(fn);
    if (fn == 0x001be570 && g_env.aim_zoom) g_env.aim_zoom();
    else if (fn == 0x001be660 && g_env.aim_release) g_env.aim_release();
    else ++g_stats.camera;
}

// Simulation::GetPlayerObject (o primeiro carro) e o corpo dele (GetRigidBody pela vaga +0x46).
BondCar* player_car() {
    Simulation& sim = simulation();
    if (sim.cars.empty()) {
        ++g_stats.guards;  // o PS2 leria o primeiro de um vetor vazio
        return nullptr;
    }
    return sim.cars[0];
}
RigidBody* player_body() {
    BondCar* c = player_car();
    return c ? c->body() : nullptr;
}

// Simulation::FindPhysicsObjectSignature e PhysicsObject::IsOwnedBy (0x00195c70): o mesmo percurso do missile.cpp,
// que não os expõe.
PhysicsObject* find_signature(Simulation& sim, uint32_t sig) {
    if (sig == 0) return nullptr;
    const int slot = int((sig >> 24) & 0x7f);
    if (sig & 0x80000000u) {
        if (slot < kSimpleBodySlots && sim.simple_sig[slot] == sig) return sim.simple_owners[slot];
        return nullptr;
    }
    if (slot < kRigidBodySlots && sim.rigid_sig[slot] == sig) return sim.owners[slot];
    return nullptr;
}
bool owned_by(Simulation& sim, SimpleObject& self, PhysicsObject* other) {
    if (!other) return false;
    const uint32_t want = PhysicsObject_GetSig(other);
    PhysicsObject* o = find_signature(sim, self.po.owner_sig);
    while (o) {
        if (PhysicsObject_GetSig(o) == want) return true;
        SimpleObject* s = dynamic_cast<SimpleObject*>(o);
        PhysicsObject* n = find_signature(sim, s ? s->po.owner_sig : 0u);
        if (n == o) return false;
        o = n;
    }
    return false;
}

const AIVtable& vt_of(uint32_t self) {
    static const AIVtable empty{};
    auto it = g_vtables.find(C(self).vtable);
    if (it == g_vtables.end()) {
        ++g_stats.guards;  // uma classe que o port não registrou
        return empty;
    }
    return it->second;
}

// A entrada +0x44 + 8 x estado da tabela virtual.
void vcall_state(uint32_t self, int32_t state) {
    const AIVtable& v = vt_of(self);
    if (state >= 0 && state < 23 && v.states[state]) v.states[state](self);
}

void set_visible(uint32_t handle, int32_t v) {
    if (const uint32_t a = actor_of_handle(handle)) nf::p32_ptr<an::ActActorData>(a)->visible = v;
}

// SetNewAnimation sobre o ator do +0x98 com o banco +0x78 e a animação +0x54; `store`: o controlador vai ao +0x9c.
void new_anim(uint32_t self, bool store) {
    AICharacterData& c = C(self);
    const uint32_t a = actor_of(self);
    if (!a) return;
    const uint32_t ctl = api().set_new_animation(a, c.bank, c.anim);
    if (store) c.controller = ctl;
}

}  // namespace

AICharacterActorApi ai_character_default_actor_api() {
    AICharacterActorApi a;
    a.set_new_animation = an::ActActor_SetNewAnimation;
    a.is_animation_done = an::ActActor_IsAnimationDone;
    a.current_frame = an::AnimController_GetCurrentFrame;
    a.current_time = an::AnimController_GetCurrentTime;
    a.total_time = an::AnimController_GetTotalTime;
    a.remaining_time = an::AnimController_GetRemainingTime;
    a.world_position = an::ActActor_GetActorWorldPosition;
    a.get_origin = an::ActActor_GetAnimationOrigin;
    a.set_origin = an::ActActor_SetAnimationOrigin;
    a.local_pos_ori = an::ActActor_GetActorLocalPosOri;
    a.weapon_position = an::ActActor_GetWeaponPosition;
    a.floor_height = [](uint32_t wpos, const float pos[3], float& h) {
        WorldCollision* w = world();
        if (!w) return false;
        WWorldPos& wp = *nf::p32_ptr<WWorldPos>(wpos);
        w->find_closest_face(wp, pos, true);
        h = WWorldPos_HeightAtPoint(wp, pos);
        return true;
    };
    a.owned_by = [](SimpleObject& self, PhysicsObject* player) { return owned_by(simulation(), self, player); };
    return a;
}

AICharacterActorApi& ai_character_actor_api() {
    static AICharacterActorApi a = ai_character_default_actor_api();
    return a;
}

AICharacterStats& ai_character_stats() { return g_stats; }
AICharacterEnv& ai_character_env() { return g_env; }
HandsGlobals& hands_globals() { return g_hands; }
int32_t& hands_zoom_button() { return g_env.zoom_button ? *g_env.zoom_button : g_zoom_button_local; }
std::vector<std::array<uint32_t, 4>>& ai_character_trace() { return g_trace; }
bool& ai_character_trace_enabled() { return g_trace_on; }

void ai_register_vtable(const AIVtable& vt) { g_vtables[vt.address] = vt; }
const AIVtable* ai_vtable(uint32_t address) {
    auto it = g_vtables.find(address);
    return it == g_vtables.end() ? nullptr : &it->second;
}

// ------------------------------------------------------------------------------------------------------- a base

// [driving] 0x00118720 AICharacter::AICharacter (só as palavras que o PS2 escreve: +0x70, +0xa8, +0xb4.., +0x128 e
// +0x144.. ficam como a memória veio)
void AICharacter_Construct(AICharacterData& c) {
    c.vtable = kVtAICharacter;
    for (int i = 0; i < 16; ++i) c.matrix[i] = (i % 5) == 0 ? 1.0f : 0.0f;  // kIdentityMatrix4 (0x355bb0)
    c.damage_taken = 0.0f;
    c.range_sq = ps2::ffloat(0x45afc800u);  // 5625
    const float zero = c.damage_taken;
    c.clip_size = 0xf;
    c.unknown8c = 0x3c;
    c.counts_hits = 1;
    c.state = c.sub_state = c.anim = c.index = 0;
    c.unknown5c = c.unknown60 = 0;
    c.hit_points = 0;
    c.world_pos = 0;
    c.bank = 0;
    c.clip = 0xf;
    c.target_right = c.unknown88 = c.unknown90 = 0;
    c.sound = c.controller = 0;
    c.unknowna0 = 0;
    c.zone_map = 0;
    c.unknownac = 1;
    c.unknowncc = 0;
    c.falling = c.floor_check = 0;
    std::memset(&c.avoid_distance, 0, 4);
    c.use_avoid = 0;
    c.actor = 0;
    c.avoid_base[2] = c.avoid_base[1] = c.avoid_base[0] = zero;
    // A matriz do corpo: vf0 na linha 3 e (1, 0, 0, 0) com pexew/pexcw nas outras (a identidade).
    for (int i = 0; i < 16; ++i) c.body[i] = (i % 5) == 0 ? 1.0f : 0.0f;
    c.velocity[2] = c.velocity[1] = c.velocity[0] = zero;
    c.avoid_point[2] = c.avoid_point[1] = c.avoid_point[0] = zero;
    c.offset[0] = ps2::ffloat(0xbda3d70au);  // -0,08
    c.offset[1] = zero;
    c.offset[2] = ps2::ffloat(0x3df5c28fu);  // 0,12
    ++g_stats.constructed;
}

// [driving] 0x00118900 AICharacter::~AICharacter (o som, o ator; o +0x98 não é zerado)
void AICharacter_Destruct(AICharacterData& c) {
    c.vtable = kVtAICharacter;
    if (c.sound) ++g_stats.sound;  // a entrada +0xc do ACharacter (não portado)
    if (c.actor) an::ActActorDatabase_KillActorByHandle(c.actor);
}

// [driving] 0x00118988 AICharacter::GetACharacter
uint32_t AICharacter_GetACharacter(uint32_t self) {
    (void)self;
    ++g_stats.sound;  // o ABaseSound/AMix "Character" não foi portado: o +0x94 fica nulo
    return 0;
}

// [driving] 0x00118af0 AICharacter::Execute
void AICharacter_Execute(uint32_t self) {
    ++g_stats.executes;
    AICharacterData& c = C(self);
    {
        const AIVtable& v = vt_of(self);
        if (v.update_rot_pos) v.update_rot_pos(self);        // +0x10c
        if (v.handle_interrupts) v.handle_interrupts(self);  // +0xfc
    }
    if (c.falling != 0) {
        const Simulation& sim = simulation();
        const float half = 0.5f, one = 1.0f;
        c.velocity[0] = ee::mul(c.velocity[0], ee::sub(one, ee::mul(sim.dt, half)));
        c.velocity[1] = ee::sub(c.velocity[1], ee::mul(sim.dt, 13.0f));
        c.velocity[2] = ee::mul(c.velocity[2], ee::sub(one, ee::mul(sim.dt, half)));
        // vmulx.xyz e vadd.xyz: a linha 3 do corpo andando pela velocidade.
        float p[3];
        for (int i = 0; i < 3; ++i) p[i] = ee::add(ee::mul(c.velocity[i], sim.dt), c.body[12 + i]);
        if (c.floor_check != 0) {
            const float* pos = c.matrix + 12;
            float h = 0.0f;
            if (!c.world_pos || !api().floor_height(c.world_pos, pos, h)) {
                ++g_stats.guards;  // o PS2 chamaria o WWorldPos nulo (ou o mundo que o port não tem)
            } else {
                const float floor = ee::sub(h, ps2::ffloat(0x3eb33333u));  // 0,35
                if (ee::lt(p[1], floor)) {
                    const float vx = ee::mul(c.velocity[0], 0.25f);
                    const float vz = ee::mul(c.velocity[2], 0.25f);
                    const float vy = ee::mul(ee::neg(c.velocity[1]), ps2::ffloat(0x3e99999au));
                    c.velocity[0] = vx;
                    c.velocity[1] = vy;
                    c.velocity[2] = vz;
                    p[1] = floor;
                }
            }
        }
        c.body[12] = p[0];
        c.body[13] = p[1];
        c.body[14] = p[2];
    }
    if (uint32_t(c.state) < 0x17) vcall_state(self, c.state);
    // O ACharacter do +0x94 (a posição, ou a entrada +0xc com 3 e o +0x94 zerado): não portado, sempre nulo.
    if (c.sound) ++g_stats.sound;
}

// [driving] 0x00118eb0 AICharacter::NotifyZoneDamage
void AICharacter_NotifyZoneDamage(uint32_t self, const float* pos) {
    AICharacterData& c = C(self);
    c.zone_last_hit = 0;
    if (!c.zone_map) return;
    const uint8_t* z = nf::p32_ptr<const uint8_t>(c.zone_map);
    auto zf = [&](uint32_t off) {
        float f;
        std::memcpy(&f, z + off, 4);
        return f;
    };
    auto zi = [&](uint32_t off) {
        int32_t i;
        std::memcpy(&i, z + off, 4);
        return i;
    };
    float m[4][4];
    std::memcpy(m, c.matrix, 64);
    OrthoInverse(m);
    using namespace ps2::vu;
    const uint32_t p[4] = {ps2::fbits(pos[0]), ps2::fbits(ee::sub(pos[1], zf(0x10))), ps2::fbits(pos[2]), 0};
    static const uint32_t vf0[4] = {0, 0, 0, 0x3f800000u};
    Reg r0, r1, r2, r3, acc, out;
    load(r0, m[0]);
    load(r1, m[1]);
    load(r2, m[2]);
    load(r3, m[3]);
    mulabc(acc, r0, p, BX, XYZ);
    maddabc(acc, r1, p, BY, XYZ);
    maddabc(acc, r2, p, BZ, XYZ);
    maddbc(out, acc, r3, vf0, BW, XYZ);
    float l[3] = {ps2::ffloat(out[0]), ps2::ffloat(out[1]), ps2::ffloat(out[2])};
    l[1] = ee::div(l[1], zf(0x14));
    l[0] = ee::div(l[0], zf(0xc));
    l[1] = ee::div(l[1], zf(0xc));
    l[2] = ee::div(l[2], zf(0xc));
    float depth, lon;
    Util_VecToPoleDepthAndLong(l, depth, lon);
    float f = ee::add(lon, ps2::ffloat(0xbdcccccdu));  // -0,1
    lon = f;
    if (ee::lt(f, 0.0f)) {
        do {
            f = ee::add(f, 1.0f);
        } while (ee::lt(f, 0.0f));
        lon = f;
    }
    if (ee::lt(1.0f, lon)) {
        f = lon;
        do {
            f = ee::sub(f, 1.0f);
        } while (ee::lt(1.0f, f));
        lon = f;
    }
    depth = ee::max(0.0f, ee::min(1.0f, depth));
    lon = ee::max(0.0f, ee::min(1.0f, lon));
    c.zone_last_hit = Util_CharMapLookup(lon, depth, uint32_t(zi(8)), zi(0), zi(4));
}

uint32_t ai_character_zone_map() {
    // 0x367750: a cabeça (1) nas três primeiras linhas, o tronco (2-4), os braços e as pernas (5-7), a última vazia.
    alignas(4) static const uint8_t kMap[64] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 4, 4, 2, 3, 4, 4, 2, 3, 4,
                                                4, 2, 5, 4, 4, 5, 5, 4, 4, 5, 7, 7, 6, 6, 7, 7, 6, 6, 7, 7, 6, 6,
                                                7, 7, 6, 6, 7, 7, 6, 6, 7, 7, 6, 6, 7, 7, 6, 6, 0, 0, 0, 0};
    struct ZoneMap {
        int32_t w, h;
        uint32_t map;
        float radius, y, scale;
    };
    static const ZoneMap kZone = {4, 0xf, nf::p32_u(kMap), ps2::ffloat(0x3eb33333u), ps2::ffloat(0x3e19999au), 2.0f};
    return nf::p32_u(&kZone);
}

// [driving] 0x00210e08 Util_VecToPoleDepthAndLong
void Util_VecToPoleDepthAndLong(const float* v, float& depth, float& lon) {
    const float one = 1.0f;
    depth = ee::sub(one, ee::mul(ee::add(v[1], one), 0.5f));
    const float a = nf::fm::atan2(v[0], v[2]);  // o atan2f do newlib (a conferência põe o mesmo no original)
    float l = ee::sub(one, ee::mul(a, ps2::ffloat(0x3e22f983u)));  // 1 / 2pi
    lon = l;
    if (ee::lt(l, 0.0f)) {
        do {
            l = ee::add(l, one);
        } while (ee::lt(l, 0.0f));
        lon = l;
    }
    l = lon;
    if (ee::lt(one, l)) {
        do {
            l = ee::sub(l, one);
        } while (ee::lt(one, l));
        lon = l;
    }
}

// [driving] 0x00210ff0 Util_CharMapLookup (o byte com sinal: lb)
int32_t Util_CharMapLookup(float u, float v, uint32_t map, int32_t w, int32_t h) {
    u = ee::mul(u, ee::cvt_s(w - 1));
    const int32_t ix = ee::cvt_w(ee::add(ee::lt(u, 0.0f) ? -0.5f : 0.5f, u));
    v = ee::mul(v, ee::cvt_s(h - 1));
    const int32_t iy = ee::cvt_w(ee::add(ee::lt(v, 0.0f) ? -0.5f : 0.5f, v));
    if (!map) {
        ++g_stats.guards;  // o PS2 leria o mapa nulo
        return 0;
    }
    return int32_t(*nf::p32_ptr<const int8_t>(map + uint32_t(iy * w + ix)));
}

// [driving] 0x00119140 AICharacter::MoveToPoint
void AICharacter_MoveToPoint(uint32_t self, const float* point, bool with_y) {
    const uint32_t a = actor_of(self);
    if (!a) return;
    alignas(16) float pos[4] = {0, 0, 0, 0}, origin[4] = {0, 0, 0, 0};
    api().world_position(a, pos);
    api().get_origin(a, origin);
    const float oy = origin[1];
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(pos[i], origin[i]);
    for (int i = 0; i < 3; ++i) origin[i] = ee::sub(point[i], d[i]);
    if (!with_y) origin[1] = oy;
    api().set_origin(a, origin);
}

// [driving] 0x00119858 AICharacter::UpdateRotPos
void AICharacter_UpdateRotPos(uint32_t self) {
    if (const uint32_t a = actor_of(self)) api().local_pos_ori(a, C(self).matrix);
}

// [driving] 0x001191e8 AICharacterEnemy::TargetIsInRange
bool AICharacterEnemy_TargetIsInRange(uint32_t self) {
    RigidBody* rb = player_body();
    if (!rb) return false;
    using namespace ps2::vu;
    const AICharacterData& c = C(self);
    const uint32_t d[4] = {ps2::sub(ps2::fbits(rb->pos[0]), ps2::fbits(c.matrix[12])),
                           ps2::sub(ps2::fbits(rb->pos[1]), ps2::fbits(c.matrix[13])),
                           ps2::sub(ps2::fbits(rb->pos[2]), ps2::fbits(c.matrix[14])), 0};
    static const uint32_t one[4] = {0x3f800000u, 0, 0, 0};
    Reg sq, acc, len;
    mul(sq, d, d, XYZ);
    mulabc(acc, d, d, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddbc(len, acc, one, sq, BZ, X);
    return ee::lt(ps2::ffloat(len[0]), c.range_sq);
}

// [driving] 0x00119270 AICharacterEnemy::NeedsToReload
bool AICharacterEnemy_NeedsToReload(uint32_t self) {
    const AICharacterData& c = C(self);
    if (c.clip_size != 0) return c.clip < 1;
    return false;
}

// [driving] 0x00119290 AICharacterEnemy::TargetIsAcquired
bool AICharacterEnemy_TargetIsAcquired(uint32_t self) {
    AICharacterData& c = C(self);
    const float k = ps2::ffloat(0x3e22f983u);  // 1 / 2pi
    alignas(16) float w[16] = {};
    if (const uint32_t a = actor_of(self)) api().weapon_position(a, w, 0);
    const float* src = c.index == 3 ? c.matrix : w;
    RigidBody* rb = player_body();
    if (!rb) return false;
    const float dx = ee::sub(rb->pos[0], src[12]);
    const float dz = ee::sub(rb->pos[2], src[14]);
    float ang = ee::mul(nf::fm::atan2(dx, dz), k);
    ang = ee::sub(ang, ee::mul(nf::fm::atan2(src[8], src[10]), k));
    if (ee::lt(ang, -0.5f)) ang = ee::add(ang, 1.0f);
    if (ee::lt(0.5f, ang)) ang = ee::sub(ang, 1.0f);
    c.target_right = ee::lt(0.0f, ang) ? 1 : 0;
    const float lim = ps2::ffloat(0x3bb60b61u);  // 2 graus em voltas
    return ee::lt(ps2::ffloat(0xbbb60b61u), ang) && ee::lt(ang, lim);
}

// ------------------------------------------------------------------------------------------------------- o Human

// [driving] 0x0017f4c0 Human::~Human (o personagem pela entrada +0xc da tabela dele com 3; o ~PhysicsObject 0x00195568:
// a vaga do corpo simples)
Human::~Human() {
    if (h.character) {
        const uint32_t ch = h.character;
        const AIVtable* v = ai_vtable(C(ch).vtable);
        if (v && v->destroy) v->destroy(ch);
        else ++g_stats.guards;
        if (g_hands.hands == ch) g_hands.hands = 0;  // port: o global não fica pendurado
    }
    h.character = 0;
    Simulation& sim = simulation();
    if (po.slot >= 0 && po.slot < kSimpleBodySlots && sim.simple_owners[po.slot] == this)
        ReleaseSimpleBodySlot(sim, po.slot);
    for (size_t i = 0; i < g_humans.size(); ++i)
        if (g_humans[i] == this) {
            g_humans.erase(g_humans.begin() + std::ptrdiff_t(i));
            break;
        }
}

void Human::simulate() { Human_Simulate(*this); }

int32_t Human::apply_damage(const float from[3], const float to[3], float amount, float /*split*/, int32_t kind) {
    return Human_ApplyDamage(*this, amount, from, to, kind, damage_source_is_player() ? 1 : 2);
}

float Human::hit_points() { return po.hit_point_loc ? *nf::p32_ptr<const float>(po.hit_point_loc) : 1.0f; }

// [driving] 0x0017f3b0 Human::Human
void Human_Construct(Human& hm, int32_t kind, const float dir[3], const float pos[3], uint32_t sig) {
    Simulation& sim = simulation();
    // PhysicsObject::PhysicsObject("human", "Human", 4, sig, 0, 0) (0x00195470): o corpo simples, a assinatura de quem
    // criou; o AttributeSet não foi portado.
    hm.po = PhysicsObjectWords{};
    hm.po.type = 4;
    hm.po.flags = 1;
    hm.po.slot = int16_t(AssignSimpleBodySlot(sim, &hm));
    hm.po.owner_sig = sig;
    hm.h.kind = kind;
    hm.h.character = 0;
    hm.h.dead = 0;
    hm.h.hit_pos[3] = 0.0f;
    hm.po.vtable = 0x00377f20;
    alignas(16) float orient[4][4];
    Util_GenerateMatrix(orient, dir);
    if (hm.po.slot < 0) {
        ++g_stats.guards;  // o PS2 escreveria o corpo 0x40 bytes antes do vetor
        return;
    }
    static const float zero[3] = {0, 0, 0};  // 0x355b80
    SimpleRigidBody* b = GetSimpleRigidBody(sim, hm.po.slot);
    if (b) SimpleRigidBody_Construct(b, ps2::ffloat(0x3f400000u), 100.0f, hm.po.slot, kSimpleHuman, pos, zero, zero,
                                     orient);
    b = GetSimpleRigidBody(sim, hm.po.slot);
    b->flags = uint16_t(b->flags | kSrbNoCollide);
    ++g_stats.humans;
}

// [driving] 0x0017f540 Human::Activate
void Human_Activate(Human& hm) {
    SimpleRigidBody* b = hm.po.slot >= 0 ? GetSimpleRigidBody(simulation(), hm.po.slot) : nullptr;
    if (!b) {
        ++g_stats.guards;
        return;
    }
    b->flags = uint16_t(b->flags & 0xfffe);
}

// [driving] 0x0017f570 Human::Deactivate
void Human_Deactivate(Human& hm) {
    SimpleRigidBody* b = hm.po.slot >= 0 ? GetSimpleRigidBody(simulation(), hm.po.slot) : nullptr;
    if (!b) {
        ++g_stats.guards;
        return;
    }
    b->flags = uint16_t(b->flags | 1);
}

namespace {

// O corpo simples do Human no lugar do personagem (o fim do SetAICharacter e do Simulate): SetOrientMat pela matriz,
// a posição da linha 3, a aceleração e a velocidade zeradas.
void place_body(Human& hm) {
    SimpleRigidBody* b = hm.po.slot >= 0 ? GetSimpleRigidBody(simulation(), hm.po.slot) : nullptr;
    if (!b) {
        ++g_stats.guards;
        return;
    }
    const AICharacterData& c = C(hm.h.character);
    float m[4][4];
    std::memcpy(m, c.matrix, 64);
    SimpleRigidBody_SetOrientMat(b, m);
    b->pos[0] = c.matrix[12];
    b->pos[1] = c.matrix[13];
    b->pos[2] = c.matrix[14];
    b->accel[0] = b->accel[1] = b->accel[2] = 0.0f;
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
}

}  // namespace

// [driving] 0x0017f5a0 Human::SetAICharacter
void Human_SetAICharacter(Human& hm, uint32_t character) {
    hm.h.character = character;
    const AICharacterData& c = C(character);
    if (c.hit_points != 0) {
        // PhysicsObject::SetHitPointLoc (0x00195a08; o ValidateHitPoints 0x00195a28 é vazio)
        hm.po.hit_point_loc = c.hit_points;
    } else {
        Human_Deactivate(hm);
    }
    // O dono das duas armas do personagem do ator (ActWeapon::SetOwner: o bit 2 se o Human é do jogador).
    if (const uint32_t a = actor_of_handle(c.actor)) {
        const an::ActCharacterData& ch =
            *nf::p32_ptr<const an::ActCharacterData>(nf::p32_ptr<const an::ActActorData>(a)->character);
        for (uint32_t w : ch.weapon) {
            if (!w) continue;
            BondCar* player = player_car();
            an::ActWeapon_SetOwner(w, Human_Image(hm), api().owned_by(hm, player));
        }
    }
    place_body(hm);
}

// [driving] 0x0017f6c0 Human::ApplyDamage
int32_t Human_ApplyDamage(Human& hm, float damage, const float from[3], const float to[3], int32_t kind,
                          int32_t source) {
    if (hm.h.kind == 1) return 0;
    AICharacterData& c = C(hm.h.character);
    const uint32_t a = actor_of_handle(c.actor);
    if (!a || nf::p32_ptr<const an::ActActorData>(a)->visible != 1) return 0;
    if (hm.h.dead != 0) return 0;
    if (ee::lt(damage, 20.0f) && kind == 3) return 0;
    int32_t counts = c.counts_hits;
    // Simulation::FindPhysicsObjectSignature e IsOwnedBy(GetPlayerObject): um objeto que não é do jogador não conta o
    // acerto do tipo 2 (o `source`, ver o cabeçalho).
    if (source == 2 && kind == 2) counts = 0;
    if (counts != 0 && ee::le(5.0f, damage)) {
        if (mission_exists()) SMissionManager_IncShotsHit(mission_manager(), kind == 2);
        else ++g_stats.guards;  // o PS2 leria o gerente nulo
    }
    if (kind == 3 && c.zone_map != 0) {
        using namespace ps2::vu;
        const uint32_t d[4] = {ps2::sub(ps2::fbits(to[0]), ps2::fbits(from[0])),
                               ps2::sub(ps2::fbits(to[1]), ps2::fbits(from[1])),
                               ps2::sub(ps2::fbits(to[2]), ps2::fbits(from[2])), 0};
        static const uint32_t one[4] = {0x3f800000u, 0, 0, 0};
        Reg sq, acc, len, u;
        mul(sq, d, d, XYZ);
        mulabc(acc, d, d, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(len, acc, one, sq, BZ, X);
        muls(u, d, ps2::rsqrt(0x3f800000u, len[0]), XYZ);  // vrsqrt Q, vf0w, vf2x
        float r;
        std::memcpy(&r, nf::p32_ptr<const uint8_t>(c.zone_map) + 0xc, 4);
        for (int i = 0; i < 3; ++i) hm.h.hit_pos[i] = ee::add(from[i], ee::mul(ps2::ffloat(u[i]), r));
    } else {
        hm.h.hit_pos[0] = to[0];
        hm.h.hit_pos[1] = to[1];
        hm.h.hit_pos[2] = to[2];
    }
    if (hm.h.dead == 0 && c.zone_map != 0) {
        const uint32_t self = hm.h.character;
        const AIVtable& v = vt_of(self);
        if (v.notify_zone_damage) v.notify_zone_damage(self, damage, hm.h.hit_pos);  // +0x2c
        const int32_t zone = c.zone_last_hit;  // AICharacter::GetZoneLastHit (0x001190f0)
        const float scale = v.zone_hit_point_scale ? v.zone_hit_point_scale(self, damage, zone) : 1.0f;  // +0x34
        damage = ee::mul(damage, scale);
        if (v.play_zone_injury_anim) v.play_zone_injury_anim(self, damage, zone, kind);  // +0x3c
    }
    // PhysicsObject::LoseHitPoints (0x00195a58: não abaixo de 0) e GetHitPoints (0x00195a30: 1 sem a vida).
    if (float* hp = hm.po.hit_point_loc ? nf::p32_ptr<float>(hm.po.hit_point_loc) : nullptr) {
        float d = damage;
        if (ee::lt(d, 0.0f)) d = 0.0f;
        *hp = ee::sub(*hp, d);
        if (ee::lt(*hp, 0.0f)) *hp = 0.0f;
    }
    hm.h.dead = ee::le(hm.hit_points(), 0.0f) ? 1 : 0;
    if (hm.h.dead) Human_Deactivate(hm);
    return 0x80;
}

// [driving] 0x0017f980 Human::Simulate
void Human_Simulate(Human& hm) {
    if (!hm.h.character) return;
    AICharacter_Execute(hm.h.character);
    place_body(hm);
    PhysicsObject_Simulate(&hm);
}

// [driving] 0x0017fbd0 Human::GetHuman (o +0x6c)
int32_t Human_GetHuman(const Human& hm) { return hm.h.kind; }

// [driving] 0x001ffb90 Simulation::SpawnHumanObject
Human* SpawnHumanObject(Simulation& sim, int32_t kind, const float dir[3], const float pos[3], uint32_t sig) {
    if (!CanSpawnSimpleRigidBody(sim, pos, kind == 0)) return nullptr;
    Human* hm = new Human();  // UMemory::FastAlloc(0x90)
    Human_Construct(*hm, kind, dir, pos, sig);
    g_humans.push_back(hm);
    // Simulation::SpawnPhysicsObject (0x00200cb8): o tipo 4 não tem vetor no port (o Simulate vem do mapa dos corpos
    // simples).
    SpawnPhysicsObject(sim, hm);
    return hm;
}

// [driving] 0x002019c0 Simulation::DeleteHumanObject (o vetor +0xdf0 e o DeletePhysicsObject 0x00200f18)
void DeleteHumanObject(Simulation& sim, Human* human) {
    if (!human) return;
    for (size_t i = 0; i < g_humans.size(); ++i)
        if (g_humans[i] == human) {
            g_humans.erase(g_humans.begin() + std::ptrdiff_t(i));
            break;
        }
    DeletePhysicsObject(sim, human);
}

std::vector<Human*>& simulation_humans() { return g_humans; }

// [driving] 0x00195c30 PhysicsObject::SetOwnerObject
void PhysicsObject_SetOwnerObject(SimpleObject& o, PhysicsObject* owner) {
    o.po.owner_sig = owner ? PhysicsObject_GetSig(owner) : 0u;
}

// ------------------------------------------------------------------------------------------------------- as mãos

namespace {

// [driving] 0x00124238 AICharacterHands::DoInitial
void hands_do_initial(uint32_t) {}

// [driving] 0x00124240 AICharacterHands::DoNeutral
void hands_do_neutral(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (c.sub_state == 8) {
        vcall_state(self, 8);
        return;
    }
    if (c.sub_state != 0xd) return;
    vcall_state(self, 0xd);
    if (c.state == 1) return;
    c.sub_state = 8;
    c.state = 1;
    if (h.hide_on_change) set_visible(c.actor, 0);
    c.actor = hands_handle(c.index);
    if (h.visible) set_visible(c.actor, 1);
    if (!hand_index_ok(c.index)) return;
    c.bank = kHandBank[c.index];
    c.anim = kArmAnim[c.index];
    new_anim(self, false);
}

// [driving] 0x00124348 AICharacterHands::DoIdling
void hands_do_idling(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (h.reload_pending != 0) {
        AICharacterHands_Reload(self);
        return;
    }
    if (g_hands.is_zoomed != hands_zoom_button()) {
        vcall_state(self, 9);
        return;
    }
    const uint32_t a = actor_of(self);
    if (a && api().is_animation_done(a) && hand_index_ok(c.index)) {
        c.anim = kIdleAnim[c.index];
        new_anim(self, false);
    }
}

// [driving] 0x001243f0 AICharacterHands::DoArming
void hands_do_arming(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    const uint32_t a = actor_of(self);
    if (!a || !api().is_animation_done(a)) return;
    h.base.state = 2;
    if (h.pending_weapon != 0) AICharacterHands_ChangeWeapon(self, h.pending_weapon);
    h.pending_weapon = 0;
}

// [driving] 0x00124440 AICharacterHands::DoArmed: a mira entra (fgIsZoomed 0) ou sai.
void hands_do_armed(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (g_hands.is_zoomed != 0) {
        const int32_t anim = hand_index_ok(c.index) ? kZoomOutAnim[c.index] : -1;
        if (anim != -1 && c.state != 0xb && c.state != 0xc) {
            c.state = 0xa;
            c.anim = anim;
            new_anim(self, true);
        }
        camera_state_call(0x001be660);  // RPlayerCamState::AimRelease
        if (h.visible) set_visible(c.actor, 1);
    } else {
        const int32_t anim = hand_index_ok(c.index) ? kZoomInAnim[c.index] : -1;
        if (anim != -1) {
            c.state = 0xa;
            c.anim = anim;
            new_anim(self, true);
        } else {
            camera_state_call(0x001be570);  // RPlayerCamState::AimZoom
        }
    }
    g_hands.is_zoomed = (g_hands.is_zoomed ^ 1) != 0 ? 1 : 0;
}

// [driving] 0x00124570 AICharacterHands::DoAiming
void hands_do_aiming(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    const uint32_t a = actor_of(self);
    if (a && api().is_animation_done(a)) c.state = 2;
    if (!c.controller) {
        ++g_stats.guards;  // o PS2 chamaria o controlador nulo
        return;
    }
    const float frame = api().current_frame(c.controller);
    float t;
    if (g_hands.is_zoomed == 0) {
        t = api().remaining_time(c.controller);
    } else {
        if (ee::lt(ps2::ffloat(0x409ccccdu), frame) && ee::lt(frame, ps2::ffloat(0x40a9999au))) {  // 4,9 e 5,3
            set_visible(c.actor, 0);
            camera_state_call(0x001be570);  // RPlayerCamState::AimZoom
        }
        t = api().current_time(c.controller);
    }
    h.aim_fraction = ee::div(t, api().total_time(c.controller));
}

// [driving] 0x00124650 AICharacterHands::DoFiring
void hands_do_firing(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (!c.controller) {
        ++g_stats.guards;
        return;
    }
    const float frame = api().current_frame(c.controller);
    const bool zoom_in = g_hands.is_zoomed == 0 && hand_index_ok(c.index) && kZoomInAnim[c.index] != -1;
    if (!zoom_in && g_hands.is_zoomed != hands_zoom_button()) {
        vcall_state(self, 9);
        return;
    }
    if (c.index == 3 && ee::lt(ps2::ffloat(0x415e6666u), frame) && ee::lt(frame, ps2::ffloat(0x4161999au))) {
        // O ferrolho do fuzil (13,9 a 14,1): AMenuSound::Trigger(4, "SFX_sniperbolt", "POV Weapons", 0).
        note(0x002f4b10, 4);
        ++g_stats.menu_sounds;
        if (g_env.menu_sound) g_env.menu_sound(4, "SFX_sniperbolt", "POV Weapons");
        return;
    }
    const uint32_t a = actor_of(self);
    if (a && api().is_animation_done(a)) {
        c.state = 2;
        if (h.reload_pending != 0) AICharacterHands_Reload(self);
    }
}

// [driving] 0x00124770 AICharacterHands::DoReloading
void hands_do_reloading(uint32_t self) {
    AICharacterData& c = C(self);
    const bool zoom_in = g_hands.is_zoomed == 0 && hand_index_ok(c.index) && kZoomInAnim[c.index] != -1;
    if (!zoom_in && g_hands.is_zoomed != hands_zoom_button()) {
        vcall_state(self, 9);
        return;
    }
    const uint32_t a = actor_of(self);
    if (a && api().is_animation_done(a)) c.state = 2;
}

// [driving] 0x00124800 AICharacterHands::DoUnarming
void hands_do_unarming(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    const uint32_t a = actor_of(self);
    if (!a || !api().is_animation_done(a)) return;
    h.base.state = 0;
    if (h.pending_weapon != 0) h.base.index = h.pending_weapon;
    h.pending_weapon = 0;
}

// [driving] 0x00119638 AICharacter::IsAlive (a entrada +0x14; o nome é nosso): nem morto (0x16) nem 0xf
int32_t base_is_alive(uint32_t self) {
    const int32_t s = C(self).state;
    if (s == 0x16) return 0;
    return s != 0xf ? 1 : 0;
}
// [driving] 0x00119698 AICharacter::SetActor (a entrada +0x1c; o nome é nosso): o handle no +0x98
void base_set_actor(uint32_t self, uint32_t handle) { C(self).actor = handle; }
// [driving] 0x001196c8 AICharacter::IsDriving (a entrada +0x24; o nome é nosso): 0
int32_t base_slot24(uint32_t) { return 0; }
void base_notify_zone_damage(uint32_t self, float, const float* pos) { AICharacter_NotifyZoneDamage(self, pos); }
// [driving] 0x001190f8 AICharacter::GetZoneHitPointScale(int, float): a zona 0 não leva dano, a 1 seis vezes.
float base_zone_hit_point_scale(uint32_t, float, int32_t zone) {
    if (zone == 0) return 0.0f;
    if (zone != 1) return 1.0f;
    return 6.0f;
}
// [driving] 0x00119138 AICharacter::PlayZoneInjuryAnim(int, float, int)
void base_play_zone_injury_anim(uint32_t, float, int32_t, int32_t) {}
// [driving] 0x00119848 AICharacter::HandleInterrupts (a entrada +0xfc; o nome é nosso): vazia
void base_handle_interrupts(uint32_t) {}
// [driving] 0x00119850 AICharacter::IsInCar (a entrada +0x104; o nome é nosso): 0
int32_t base_slot104(uint32_t) { return 0; }
// Os estados vazios da base (0x00119790..0x00119840: jr ra).
void empty_state(uint32_t) {}

void destroy_base(uint32_t self) {
    AICharacter_Destruct(C(self));
    delete &C(self);
}
void destroy_hands(uint32_t self) {
    AICharacterHands_Destruct(HD(self));
    delete &HD(self);
}

AIVtable base_vtable(uint32_t address) {
    AIVtable v;
    v.address = address;
    v.destroy = destroy_base;
    v.is_alive = base_is_alive;
    v.set_actor = base_set_actor;
    v.slot24 = base_slot24;
    v.notify_zone_damage = base_notify_zone_damage;
    v.zone_hit_point_scale = base_zone_hit_point_scale;
    v.play_zone_injury_anim = base_play_zone_injury_anim;
    for (auto& s : v.states) s = empty_state;
    v.handle_interrupts = base_handle_interrupts;
    v.slot104 = base_slot104;
    v.update_rot_pos = AICharacter_UpdateRotPos;
    return v;
}

// O registro das tabelas da base (0x367930) e das mãos (0x3696e0) e da função da matriz dos atores das mãos.
struct Registrar {
    Registrar() {
        ai_register_vtable(base_vtable(kVtAICharacter));
        AIVtable h = base_vtable(kVtAICharacterHands);
        h.destroy = destroy_hands;
        h.states[0] = hands_do_initial;
        h.states[1] = hands_do_neutral;
        h.states[2] = hands_do_idling;
        h.states[8] = hands_do_arming;
        h.states[9] = hands_do_armed;
        h.states[10] = hands_do_aiming;
        h.states[11] = hands_do_firing;
        h.states[12] = hands_do_reloading;
        h.states[13] = hands_do_unarming;
        h.update_rot_pos = AICharacterHands_UpdateRotPos;
        ai_register_vtable(h);
        an::act_register_callback(0x00123878, [](uint32_t user, float m[16], float vel[3]) {
            AICharacterHands_GetRotPos(user, m, vel);
        });
    }
};
const Registrar g_registrar;

}  // namespace

// [driving] 0x00123e28 AICharacterHands::AICharacterHands
void AICharacterHands_Construct(AICharacterHandsData& h, int32_t weapon) {
    AICharacter_Construct(h.base);
    h.base.index = weapon;
    h.base.vtable = kVtAICharacterHands;
    h.hide_on_change = 1;
    h.reload_pending = 0;
    h.pending_weapon = 0;
    h.shake = 0.0f;
    h.aim_fraction = 0.0f;
    h.visible = 0;
    h.base.bank = hand_index_ok(weapon) ? kHandBank[weapon] : 0;
}

// [driving] 0x00123ea0 AICharacterHands::~AICharacterHands
void AICharacterHands_Destruct(AICharacterHandsData& h) {
    h.base.vtable = kVtAICharacterHands;
    AICharacter_Destruct(h.base);
}

// [driving] 0x00123ef8 AICharacterHands::ShowHands
void AICharacterHands_ShowHands(uint32_t self) {
    set_visible(C(self).actor, 1);
    HD(self).visible = 1;
}

// [driving] 0x00123f10 AICharacterHands::HideHands
void AICharacterHands_HideHands(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (c.state == 1 && c.sub_state == 0xd) {
        c.state = 2;
        if (h.pending_weapon != 0) c.index = h.pending_weapon;
        h.pending_weapon = 0;
        if (h.hide_on_change != 0) set_visible(c.actor, 0);
        c.actor = hands_handle(c.index);
    }
    set_visible(c.actor, 0);
    h.visible = 0;
}

// [driving] 0x00123f90 AICharacterHands::ChangeWeapon
void AICharacterHands_ChangeWeapon(uint32_t self, int32_t weapon) {
    ++g_stats.changes;
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    const int32_t idx = (uint32_t(weapon) - 1u < 7u) ? weapon : 1;
    if (c.state != 2) {
        h.pending_weapon = idx;
        return;
    }
    if (!hand_index_ok(c.index)) return;
    if (kUnarmAnim[c.index] != -1) {
        c.state = 1;
        c.sub_state = 0xd;
        c.anim = kUnarmAnim[c.index];
        new_anim(self, false);
        c.index = idx;
        h.pending_weapon = 0;
        return;
    }
    c.sub_state = 8;
    c.index = idx;
    c.state = 1;
    h.pending_weapon = 0;
    if (h.hide_on_change != 0) set_visible(c.actor, 0);
    c.actor = hands_handle(c.index);
    if (h.visible != 0) set_visible(c.actor, 1);
    c.bank = kHandBank[c.index];
    c.anim = kArmAnim[c.index];
    new_anim(self, false);
}

// [driving] 0x001240c8 AICharacterHands::Fire
void AICharacterHands_Fire(uint32_t self) {
    AICharacterData& c = C(self);
    if (c.state != 2) return;
    if (!hand_index_ok(c.index)) return;
    ++g_stats.hands_fired;
    int32_t& side = g_hands.toggle[c.index];
    c.state = 0xb;
    if (uint32_t(side) > 1u) {
        ++g_stats.guards;  // o PS2 leria a linha de outra arma
        side &= 1;
    }
    c.anim = kFireAnim[c.index][side];
    new_anim(self, true);
    side ^= 1;
}

// [driving] 0x00124170 AICharacterHands::Reload
void AICharacterHands_Reload(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    if (c.state != 2) {
        h.reload_pending = 1;
        return;
    }
    if (!hand_index_ok(c.index)) return;
    ++g_stats.reloads;
    c.state = 0xc;
    c.anim = kReloadAnim[c.index];
    new_anim(self, false);
    h.reload_pending = 0;
}

// [driving] 0x001241e0 AICharacterHands::AbleToFire
bool AICharacterHands_AbleToFire(uint32_t self) {
    const AICharacterHandsData& h = HD(self);
    const int32_t s = h.base.state;
    if (s == 8 || s == 0 || s == 1 || s == 0xc) return false;
    return s == 2 && h.reload_pending == 0;
}

// [driving] 0x001237d0 AICharacterHands::GetAimFraction
float AICharacterHands_GetAimFraction() { return g_hands.hands ? HD(g_hands.hands).aim_fraction : 0.0f; }

// RPlayerCamera::GetAimMatrix4 (0x001bcd10) é o de camera/camera_aim.cpp (conferido na parte 20).

// [driving] 0x00124848 AICharacterHands::UpdateRotPos
void AICharacterHands_UpdateRotPos(uint32_t self) {
    AICharacterHandsData& h = HD(self);
    AICharacterData& c = h.base;
    const uint8_t* cam = camera();
    // A entrada +0x24 da tabela da câmera (a tabela no +0xa8): RPlayerCamera::UpdateCamera.
    note(0x001b29e0);
    if (g_env.camera_update) g_env.camera_update();
    else ++g_stats.camera;
    if (!cam) {
        ++g_stats.guards;  // o PS2 leria a câmera nula
        return;
    }
    const float* m = RPlayerCamera_GetAimMatrix4(
        *reinterpret_cast<PlayerCameraData*>(const_cast<uint8_t*>(cam)), g_hands.aim_yaw, g_hands.aim_pitch);
    std::memcpy(c.matrix, m, 64);
    std::memcpy(c.matrix + 12, cam + 0x30, 16);
    const SMissionManagerData* mm = mission_exists() ? &mission_manager() : nullptr;
    if (uint32_t(c.index - 1) < 2u && !mm) ++g_stats.guards;  // o PS2 leria o gerente nulo
    if (uint32_t(c.index - 1) < 2u && mm && mm->auto_drive != 0) {
        RigidBody* rb = player_body();
        if (rb) {
            const Simulation& sim = simulation();
            float d[3];
            for (int i = 0; i < 3; ++i) d[i] = ee::sub(c.matrix[12 + i], rb->pos[i]);
            const float k1 = ee::mul(sim.dt, 0.5f);
            for (int i = 0; i < 3; ++i)
                g_hands.lag_pos[i] = ee::add(g_hands.lag_pos[i], ee::mul(ee::sub(d[i], g_hands.lag_pos[i]), k1));
            for (int i = 0; i < 3; ++i) d[i] = ee::sub(d[i], g_hands.lag_pos[i]);
            const float k2 = ee::mul(sim.dt, 7.0f);
            for (int i = 0; i < 3; ++i)
                g_hands.lag_vel[i] = ee::add(g_hands.lag_vel[i], ee::mul(ee::sub(d[i], g_hands.lag_vel[i]), k2));
            const float lim = ps2::ffloat(0x3e088888u);  // 0,1333
            if (ee::lt(lim, g_hands.lag_vel[1])) g_hands.lag_vel[1] = lim;
            if (ee::lt(g_hands.lag_vel[1], ps2::ffloat(0xbe088888u))) g_hands.lag_vel[1] = ps2::ffloat(0xbe088888u);
            const float k3 = ps2::ffloat(0x3e19999au);  // 0,15
            for (int i = 0; i < 3; ++i) d[i] = ee::mul(g_hands.lag_vel[i], k3);
            // O lado: a linha 0 do carro (vmul.xyz, vaddy.x, vaddz.x) x 0,2.
            const float* row0 = nf::raw(rb->info)->orient[0];
            float side = ee::add(ee::add(ee::mul(row0[0], d[0]), ee::mul(row0[1], d[1])), ee::mul(row0[2], d[2]));
            side = ee::mul(side, ps2::ffloat(0x3e4ccccdu));
            // A velocidade no plano (vmul.xz, vmulax.x, vmaddz.x, vsqrt, vaddq.x), no máximo 1.
            const uint32_t vx2 = ps2::mul(ps2::fbits(rb->vel[0]), ps2::fbits(rb->vel[0]));
            const uint32_t vz2 = ps2::mul(ps2::fbits(rb->vel[2]), ps2::fbits(rb->vel[2]));
            const float speed = ps2::ffloat(ps2::add(0u, ps2::sqrt(ps2::madd(vx2, 0x3f800000u, vz2))));
            const float dy = ee::mul(d[1], ee::min(1.0f, speed));
            float cam00, cam02;
            std::memcpy(&cam00, cam + 0x0, 4);
            std::memcpy(&cam02, cam + 0x8, 4);
            c.matrix[12] = ee::add(c.matrix[12], ee::mul(side, cam00));
            c.matrix[13] = ee::sub(c.matrix[13], dy);
            c.matrix[14] = ee::add(c.matrix[14], ee::mul(side, cam02));
        }
    }
    if (c.index != 1 && c.index != 4 && c.index != 6) return;
    float fwd[3], pos[3];
    std::memcpy(fwd, cam + 0x20, 12);
    std::memcpy(pos, c.matrix + 12, 12);
    const float k16 = ps2::ffloat(0x37800000u);  // 1 / 65536
    if (c.state == 0xb) {
        const float s = ps2::ffloat(0x3a83126fu), off = ps2::ffloat(0x3a03126fu);  // 0,001 e 0,0005
        float r = ee::cvt_s(int32_t(SimRandom_Generate(sim_random())));
        pos[0] = ee::add(pos[0], ee::sub(ee::mul(ee::mul(r, k16), s), off));
        r = ee::cvt_s(int32_t(SimRandom_Generate(sim_random())));
        pos[2] = ee::add(pos[2], ee::sub(ee::mul(ee::mul(r, k16), s), off));
        if (ee::le(ps2::ffloat(0xbc4ccccdu), h.shake)) h.shake = ps2::ffloat(0xbccccccdu);  // -0,0125: -0,025
    }
    if (ee::lt(h.shake, 0.0f)) {
        for (int i = 0; i < 3; ++i) pos[i] = ee::add(pos[i], ee::mul(fwd[i], h.shake));
        const float r = ee::cvt_s(int32_t(SimRandom_Generate(sim_random())));
        h.shake = ee::min(0.0f, ee::add(h.shake, ee::mul(ee::mul(r, k16), ps2::ffloat(0x3ba3d70au))));
    }
    std::memcpy(c.matrix + 12, pos, 12);
}

// [driving] 0x00123878 AICharacterHands::GetRotPos(int, MATRIX4 &, COORD3 &)
void AICharacterHands_GetRotPos(uint32_t self, float m[16], float vel[3]) {
    std::memcpy(m, C(self).matrix, 64);
    RigidBody* rb = player_body();
    if (!rb) return;
    vel[0] = rb->vel[0];
    vel[1] = rb->vel[1];
    vel[2] = rb->vel[2];
}

// [driving] 0x00123950 AICharacterHands::Init
void AICharacterHands_Init() {
    for (int i = 0; i < 8; ++i) {
        g_hands.actor[i] = 0;
        g_hands.toggle[i] = 0;
    }
    hands_zoom_button() = 0;
    g_hands.is_zoomed = 0;
    SWeaponManagerData* wm = weapon_manager();
    const WeaponEntry* table = wm && wm->weapons ? nf::p32_ptr<const WeaponEntry>(wm->weapons) : nullptr;
    if (!table) {
        ++g_stats.guards;  // o PS2 leria a tabela do gerente nulo
        return;
    }
    int32_t first = 0;
    for (int32_t i = 7; i > 0; --i) {
        if (table[i + 0x17].flags & 1) {
            first = i;
            an::ActCharacter_LoadCharacter(kHandChars[i], kHandWeapons[i], nullptr);
        }
    }
    if (first == 0) return;
    AICharacterHandsData* hands = new AICharacterHandsData{};  // UMemory::FastAlloc(0x170)
    AICharacterHands_Construct(*hands, first);
    const uint32_t self = nf::p32_u(hands);
    Simulation& sim = simulation();
    BondCar* player = player_car();
    const uint32_t sig = player ? PhysicsObject_GetSig(player) : 0u;
    Human* human = SpawnHumanObject(sim, 0, g_hands.init_rot_pos + 8, g_hands.init_rot_pos + 12, sig);
    if (human) PhysicsObject_SetOwnerObject(*human, player);
    else ++g_stats.guards;  // o PS2 chamaria o SetOwnerObject do nulo
    for (int32_t i = 1; i < 8; ++i) {
        if (!(table[i + 0x17].flags & 1)) continue;
        // AttributeSet::LookupVector(o carro do jogador +0x5c, nome, 0) (0x00169e48), a posição e o giro.
        float pos[4] = {0, 0, 0, 0}, rot[4] = {0, 0, 0, 0};
        if (!g_env.lookup_vector || !g_env.lookup_vector(kHandPos[i], pos)) ++g_stats.guards;
        if (!g_env.lookup_vector || !g_env.lookup_vector(kHandRot[i], rot)) ++g_stats.guards;
        float m[16];
        std::memcpy(m, g_hands.init_rot_pos, 64);
        m[12] = ee::add(m[12], pos[0]);
        m[13] = ee::add(m[13], pos[1]);
        m[14] = ee::add(m[14], pos[2]);
        const uint32_t handle =
            an::ActActorDatabase_GetNewActorHandle(self, kHandChars[i], kHandWeapons[i], nullptr, 0x00123878, m, 0);
        g_hands.actor[i] = handle;
        const uint32_t a = actor_of_handle(handle);
        if (!a) continue;
        an::ActActorData& ad = *nf::p32_ptr<an::ActActorData>(a);
        ad.fov_scale = 0.5f;
        api().set_new_animation(a, kHandBank[i], kStartAnim[i]);
        ad.visible = 0;
        ad.pov = 1;
        an::ActActor_RotateActorX(a, rot[0]);
        an::ActActor_RotateActor(a, rot[1]);
    }
    const AIVtable& v = vt_of(self);
    if (v.set_actor) v.set_actor(self, g_hands.actor[first]);  // a entrada +0x1c
    g_hands.hands = self;
    hands->base.state = 8;
    if (human) {
        Human_SetAICharacter(*human, self);
        g_hands.human = nf::p32_u(human);
        Human_Deactivate(*human);
    }
}

// [driving] 0x00123d90 AICharacterHands::Shutdown (o AICharacterHands e o Human ficam: o Human os apaga)
void AICharacterHands_Shutdown() {
    for (int i = 1; i < 8; ++i) {
        if (g_hands.actor[i]) {
            an::ActActorDatabase_KillActorByHandle(g_hands.actor[i]);
            g_hands.actor[i] = 0;
        }
    }
    if (g_hands.hands) {
        C(g_hands.hands).actor = 0;
        g_hands.hands = 0;
    }
    if (g_hands.human) g_hands.human = 0;
}

// --------------------------------------------------------------------------------------------- o Init da base

// [driving] 0x001186b0 AICharacter::Init
void AICharacter_Init() {
    // AICharacterPedestrian::Init(0x10), AICharacterPassenger::Init e AICharacterBond::Init: não portados (contados).
    note(0x00125bb8, 0x10);
    ++g_stats.pedestrians;
    note(0x00125438);
    ++g_stats.passenger;
    note(0x00119e20);
    ++g_stats.bond;
    AICharacterHands_Init();
}

// [driving] 0x001186e8 AICharacter::Shutdown
void AICharacter_Shutdown() {
    AICharacterHands_Shutdown();
    note(0x0011a1a8);  // AICharacterBond::Shutdown
    note(0x00125640);  // AICharacterPassenger::Shutdown
    note(0x00126248);  // AICharacterPedestrian::Destroy
}

void ai_character_reset() {
    // Os Human do port (cada um apaga o seu personagem, que mata o ator do +0x98).
    while (!g_humans.empty()) delete g_humans.back();
    g_stats = AICharacterStats{};
    g_trace.clear();
    g_hands = HandsGlobals{};
    g_zoom_button_local = 0;
}

}  // namespace drv
