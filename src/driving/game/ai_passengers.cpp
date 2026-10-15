// Os passageiros dos carros da IA (driving/game/ai_passengers.h): o AICharacterEnemyWindow, o AICharacterEnemySunroof e
// o AICharacterEnemyDriver, do MIPS do DRIVING.ELF. As divergências estão no cabeçalho e em cada função.

#include "driving/game/ai_passengers.h"

#include <cstdio>
#include <cstring>

#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/anim/act_actor.h"
#include "driving/camera/camera_math.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/target_picker.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/render/anim_engine.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
namespace an = drv::anim;

namespace {

PassengerStats g_stats;
// A guarda (o PS2 leria memória inválida): contada e anotada pela linha.
void guard(int line) {
    ++g_stats.guards;
    ++g_stats.guard_lines[line];
}

AICharacterData& C(uint32_t self) { return *nf::p32_ptr<AICharacterData>(self); }
AICharacterEnemyWindowData& WD(uint32_t self) { return *nf::p32_ptr<AICharacterEnemyWindowData>(self); }
AICharacterEnemySunroofData& SD(uint32_t self) { return *nf::p32_ptr<AICharacterEnemySunroofData>(self); }
AICharacterEnemyDriverData& DD(uint32_t self) { return *nf::p32_ptr<AICharacterEnemyDriverData>(self); }
PassengerApi& api() { return passenger_api(); }
AICharacterActorApi& aapi() { return ai_character_actor_api(); }

// O ActActor de um handle (o +8 do nó); nulo: o PS2 leria o nó nulo.
uint32_t actor_of(uint32_t self) {
    const uint32_t h = C(self).actor;
    const uint32_t a = h ? an::act_actor_of(h) : 0u;
    if (!a) guard(__LINE__);
    return a;
}
// O +0x30 do ActActor (o visível).
int32_t visible(uint32_t actor) { return actor ? nf::p32_ptr<const an::ActActorData>(actor)->visible : 0; }
void set_visible(uint32_t actor, int32_t v) {
    if (actor) nf::p32_ptr<an::ActActorData>(actor)->visible = v;
}

// O carro do +0x150 (a imagem do PS2) como o objeto do port; nulo: o PS2 leria o objeto nulo.
BondCar* car_of(uint32_t car) {
    BondCar* c = car ? ai_car_from_physics(car) : nullptr;
    if (!c) guard(__LINE__);
    return c;
}
uint32_t car_word(uint32_t self) { return *nf::p32_ptr<const uint32_t>(self + 0x150); }

const AIVtable& vt(uint32_t self) {
    static const AIVtable empty{};
    const AIVtable* v = ai_vtable(C(self).vtable);
    if (!v) {
        guard(__LINE__);
        return empty;
    }
    return *v;
}
void state_call(uint32_t self, int32_t s) {
    const AIVtable& v = vt(self);
    if (v.states[s]) v.states[s](self);
}
uint32_t vehicle_ptr(uint32_t self) {  // a entrada +0x24 (GetVehiclePtr)
    const AIVtable& v = vt(self);
    return v.slot24 ? uint32_t(v.slot24(self)) : 0u;
}

// SetNewAnimation com o banco +0x78 e a animação +0x54 (o +0xa0, o controlador do crossfade, vai a 0).
void new_anim(uint32_t self, int32_t anim) {
    AICharacterData& c = C(self);
    c.anim = anim;
    if (const uint32_t a = actor_of(self)) aapi().set_new_animation(a, c.bank, c.anim);
    c.unknowna0 = 0;
}
void time_scale(uint32_t self, float s) {
    if (const uint32_t a = actor_of(self)) api().set_time_scale(a, s);
}
bool anim_done(uint32_t self) {
    const uint32_t a = actor_of(self);
    return a && aapi().is_animation_done(a);
}

// O Human do +0x74 (a imagem do PS2) pelo ganchos de passenger_api().
void human_activate(uint32_t self) { api().human_activate(C(self).world_pos); }
void human_deactivate(uint32_t self) { api().human_deactivate(C(self).world_pos); }

// O corpo do carro do personagem (Simulation::GetRigidBody 0x001ff088 pela vaga +0x46).
RigidBody* car_body(uint32_t self) {
    BondCar* c = car_of(car_word(self));
    if (!c) return nullptr;
    RigidBody* rb = GetRigidBody(simulation(), c->d.slot);
    if (!rb) guard(__LINE__);
    return rb;
}
// O corpo do carro do jogador (o primeiro do vetor dos carros, +0xdcc).
RigidBody* player_body() {
    Simulation& sim = simulation();
    if (sim.cars.empty()) {
        guard(__LINE__);
        return nullptr;
    }
    return GetRigidBody(sim, sim.cars.front()->d.slot);
}

// O alvo dos atiradores: o veículo alvo do AIGroundVehicle do carro deles (+0x1a4, ativo) ou o carro do jogador.
// `aimed` 1 quando é o veículo (o TargetIsAcquired grava o lugar em +0xc0 e 1 em +0xcc).
bool target_pos(uint32_t self, float out[3]) {
    BondCar* car = car_of(vehicle_ptr(self));
    AIGroundVehicle* v = car ? nf::p32_ptr<AIGroundVehicle>(car->d.ai_ground_vehicle) : nullptr;
    if (v && v->primary_target != 0 && nf::p32_ptr<AIGroundVehicle>(v->primary_target)->active != 0) {
        const float* p = AIVehicle_GetPosition(*nf::p32_ptr<AIGroundVehicle>(v->primary_target));
        std::memcpy(out, p, 12);
        return true;
    }
    RigidBody* rb = player_body();
    if (rb) std::memcpy(out, rb->pos, 12);
    else out[0] = out[1] = out[2] = 0.0f;
    return false;
}

// vsub.xyz, vmul.xyz, vmulax.x, vmadday.x, vmaddz.x (o vf3.x = 1): a distância ao quadrado.
float vu_dist_sq(const float a[3], const float b[3]) {
    using namespace ps2::vu;
    const uint32_t d[4] = {ps2::sub(ps2::fbits(a[0]), ps2::fbits(b[0])), ps2::sub(ps2::fbits(a[1]), ps2::fbits(b[1])),
                           ps2::sub(ps2::fbits(a[2]), ps2::fbits(b[2])), 0};
    static const uint32_t one[4] = {0x3f800000u, 0, 0, 0};
    Reg sq, acc, len;
    mul(sq, d, d, XYZ);
    mulabc(acc, d, d, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddbc(len, acc, one, sq, BZ, X);
    return ps2::ffloat(len[0]);
}

// O ângulo (voltas, -0,5..0,5) do alvo à frente (a linha 2) de uma matriz: os dois atan2f da libm do PS2.
float bearing(const float t[3], const float* m) {
    const float k = ps2::ffloat(0x3e22f983u);  // 1 / 2pi
    float a = ee::mul(nf::fm::atan2(ee::sub(t[0], m[12]), ee::sub(t[2], m[14])), k);
    a = ee::sub(a, ee::mul(nf::fm::atan2(m[8], m[10]), k));
    if (ee::lt(a, -0.5f)) a = ee::add(a, 1.0f);
    if (ee::lt(0.5f, a)) a = ee::sub(a, 1.0f);
    return a;
}

// As tabelas das animações (o .data do PS2).
constexpr int32_t kWinIdle = 0xa;                            // 0x327478
constexpr int32_t kWinUnarm = 9;                             // 0x327480
constexpr int32_t kWinFire[5] = {3, 6, 4, 6, 5};             // 0x327488
constexpr int32_t kWinHurt[2] = {7, 8};                      // 0x3274a0
constexpr int32_t kWinDeath[2] = {0, 1};                     // 0x3274a8
constexpr int32_t kSunRise[4] = {-1, 0x13, 0x16, -1};        // 0x326f28 (pela arma)
constexpr int32_t kSunDown[4] = {-1, 0x12, 0x15, -1};        // 0x326f38
constexpr int32_t kSunLean[16] = {-1, -1, -1, -1, 0xc, 0xf, 0xc, 0xf, 9, 0xc, 0xf, 0x12, -1, -1, -1, -1};      // 0x326f48
constexpr int32_t kSunReload[4] = {-1, 0x14, -1, -1};        // 0x326f88
constexpr int32_t kSunFire[24] = {-1, -1, -1, -1, -1, -1, 1, 0, 2, 3, 0, 4,
                                  0,  1,  0,  1,  0,  1,  -1, -1, -1, -1, -1, -1};  // 0x326f98 [arma][6]
constexpr int32_t kSunLeanStay[16] = {-1, -1, -1, -1, 0xd, 0x10, 0xd, 0x10, 0xa, 0xd, 0x10, 0x13, -1, -1, -1, -1};   // 0x326ff8
constexpr int32_t kSunLeanBack[16] = {-1, -1, -1, -1, 0xe, 0x11, 0xe, 0x11, 0xb, 0xe, 0x11, 0x14, -1, -1, -1, -1};  // 0x327038
// 0x327078 (a morte) e, logo depois, 0x327278 (o ferimento): [arma][zona][4] as animações (o banco 9 na morte); uma
// tabela só, como na memória do PS2 (uma zona ou uma arma fora lê a linha vizinha).
constexpr int32_t kSunInjury[256] = {
    // a morte: as armas 0, 1, 2, 3
    -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 1, 2, 0, 0, 6, 0, 0, 0, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 2, 0, 0, 6, 0, 0, 0, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0,
    // o ferimento
    -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0,
    5, 0, 0, 0, 7, 0, 0, 0, 0xb, 0, 0, 0, 8, 9, 0xa, 0, 5, 0, 0, 0, 5, 6, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0,
    2, 0, 0, 0, 4, 0, 0, 0, 8, 0, 0, 0, 5, 6, 7, 0, 2, 0, 0, 0, 2, 3, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0,
    -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0};

// Uma entrada de tabela plana com guarda (o PS2 leria fora: o índice -1 de uma linha lê a anterior, que a tabela plana
// reproduz; fora da tabela inteira, -1 e contado).
int32_t flat(const int32_t* t, int n, int i) {
    if (i < 0 || i >= n) {
        guard(__LINE__);
        return -1;
    }
    return t[i];
}

// O crossfade da animação de ferimento (o +0xa0: o CrossFadeAnimationController).
void injury_anim(uint32_t self, int32_t bank, int32_t anim) {
    AICharacterData& c = C(self);
    c.anim = anim;
    if (c.unknowna0 != 0) {
        api().start_new_crossfade(uint32_t(c.unknowna0), ps2::ffloat(0x3e800000u), bank, anim);
        return;
    }
    const uint32_t a = actor_of(self);
    c.unknowna0 = a ? int32_t(api().set_new_crossfade(a, ps2::ffloat(0x3e800000u), bank, anim)) : 0;
}

// Os personagens do mesmo carro (o DoInitial): o vetor dos Human da Simulation (+0xdf0), os que não são este e cujo
// GetVehiclePtr é o carro dele.
void find_others(uint32_t self, uint32_t* others) {
    for (Human* h : simulation_humans()) {
        const uint32_t ch = h->h.character;
        if (ch == 0 || ch == self) continue;
        if (vehicle_ptr(ch) != car_word(self)) continue;
        if (others[0] == 0) others[0] = ch;
        else if (others[1] == 0) others[1] = ch;
    }
}

// ------------------------------------------------------------------------------------- a matriz (as três classes)

// [driving] 0x00122238 AICharacterEnemyWindow::GetRotPos(int, MATRIX4 &), 0x001208f8 e 0x0011aa30 (os do Sunroof e do
// Driver são iguais), e os de três argumentos 0x00122358, 0x00120a18 e 0x0011ab50 (com a velocidade)
void rot_pos(uint32_t self, float m[16], float vel[3]) {
    RigidBody* rb = car_body(self);
    if (!rb) return;
    std::memcpy(m, nf::raw(rb->info)->orient, 64);
    m[12] = rb->pos[0];
    m[13] = rb->pos[1];
    m[14] = rb->pos[2];
    m[15] = 1.0f;
    if (vel) std::memcpy(vel, rb->vel, 12);
}

// [driving] 0x00122dd0 AICharacterEnemyWindow::UpdateRotPos (0x001217c8 e 0x0011b128 iguais): a pose local do ator
// vezes a matriz do carro.
void update_rot_pos(uint32_t self) {
    alignas(16) float m[16];
    rot_pos(self, m, nullptr);
    AICharacterData& c = C(self);
    if (const uint32_t a = actor_of(self)) aapi().local_pos_ori(a, c.matrix);
    VU0_MATRIX4_mult(reinterpret_cast<const float(*)[4]>(c.matrix), reinterpret_cast<const float(*)[4]>(m),
                     reinterpret_cast<float(*)[4]>(c.matrix));
}

// [driving] 0x00122e18 AICharacterEnemyWindow::GetVehiclePtr e 0x00121810 (o do Sunroof): o +0x150
int32_t get_vehicle_ptr(uint32_t self) { return int32_t(car_word(self)); }

// ------------------------------------------------------------------------------------------------- a janela

// [driving] 0x00122600 AICharacterEnemyWindow::SetActor: o pescoço e o ombro direito sobrepostos no espaço local.
void win_set_actor(uint32_t self, uint32_t handle) {
    C(self).actor = handle;
    int32_t bones[2] = {-1, -1};  // 0x3691c0
    const uint32_t a = actor_of(self);
    if (!a) return;
    bones[0] = api().get_bone_index(a, "Root.Neck");
    if (bones[0] == -1) return;
    bones[1] = api().get_bone_index(a, "Root.RShoulder");
    if (bones[1] != -1) api().create_overrides(a, 2, bones, 1);
}

// [driving] 0x00122698 AICharacterEnemyWindow::DoInitial
void win_do_initial(uint32_t self) {
    if (!anim_done(self)) return;
    C(self).state = 1;
    set_visible(actor_of(self), 0);
    human_deactivate(self);
    find_others(self, WD(self).others);
}

// [driving] 0x00122780 AICharacterEnemyWindow::DoNeutral
void win_do_neutral(uint32_t self) {
    BondCar* car = car_of(car_word(self));
    if (!car || car->d.reset_available != 0) return;  // GetResetAvailable (+0x6c)
    const AIVtable& v = vt(self);
    if (!v.target_in_range || v.target_in_range(self) == 0) return;
    AICharacterData& c = C(self);
    c.state = 2;
    c.sub_state = 8;
    set_visible(actor_of(self), 1);
    human_activate(self);
    WD(self).countdown = simulation().frame_rate * 300;
}

// [driving] 0x00122818 AICharacterEnemyWindow::DoIdling
void win_do_idling(uint32_t self) {
    AICharacterData& c = C(self);
    if (c.sub_state == 8) {
        time_scale(self, 2.0f);
        new_anim(self, kWinIdle);
    } else if (c.sub_state == 1 || c.sub_state == 0x16) {
        set_visible(actor_of(self), 0);
        human_deactivate(self);
    }
    const int32_t next = c.sub_state;
    c.sub_state = 0;
    c.state = next;
}

// [driving] 0x001228d0 AICharacterEnemyWindow::DoArming
void win_do_arming(uint32_t self) {
    if (!anim_done(self)) return;
    C(self).state = 9;
    time_scale(self, 1.0f);
}

// [driving] 0x00122920 AICharacterEnemyWindow::DoArmed: mira e atira (o período +0x8c) até sair do alcance.
void win_do_armed(uint32_t self) {
    AICharacterData& c = C(self);
    BondCar* car = car_of(car_word(self));
    bool unarm = !car || car->d.reset_available != 0;
    if (!unarm && WD(self).countdown < 1) {
        const AIVtable& v = vt(self);
        unarm = !v.target_in_range || v.target_in_range(self) == 0;
    }
    if (unarm) {
        c.state = 0xd;
        time_scale(self, 2.0f);
        new_anim(self, kWinUnarm);
        return;
    }
    state_call(self, 10);  // DoAiming (+0x94)
    if (c.unknownac == 0) return;
    const Simulation& sim = simulation();
    if (sim.steps < c.unknown90) return;
    c.state = 0xb;
    c.clip -= 1;
    c.unknown90 = sim.steps + c.unknown8c;
    const uint32_t r = SimRandom_Generate(sim_random());
    ++g_stats.fires;
    new_anim(self, kWinFire[(int32_t(r) * 5) >> 16]);
}

// [driving] 0x00122a68 AICharacterEnemyWindow::DoAiming: o ombro anda dois graus por passo para o lado do alvo (o
// +0x84 do TargetIsAcquired contra o lado da janela, +0x160); o pescoço segue 55 graus atrás.
void win_do_aiming(uint32_t self) {
    const AIVtable& v = vt(self);
    if (v.target_acquired && v.target_acquired(self) != 0) return;
    ++g_stats.aims;
    AICharacterEnemyWindowData& w = WD(self);
    float delta = 0.0f;
    const uint32_t x = uint32_t(w.base.target_right) ^ uint32_t(w.side);
    if (x == 0) delta = 2.0f;
    else if (x == 1) delta = -2.0f;
    const float aim = ee::add(w.aim, delta);
    w.aim = aim;
    float neck = ee::sub(aim, 55.0f);
    if (ee::lt(neck, -80.0f)) neck = -80.0f;
    if (ee::lt(30.0f, neck)) neck = 30.0f;
    if (ee::lt(aim, -60.0f)) w.aim = -60.0f;
    if (ee::lt(80.0f, w.aim)) w.aim = 80.0f;
    alignas(16) float m[32];
    BuildRotate(m, neck, 0.0f, 1.0f, 0.0f);
    BuildRotate(m + 16, w.aim, 0.0f, 1.0f, 0.0f);
    static const float kWeights[2] = {1.0f, 1.0f};  // 0x3691e8
    if (const uint32_t a = actor_of(self)) api().set_overrides(a, 2, m, kWeights);
}

// [driving] 0x00122be0 AICharacterEnemyWindow::DoFiring
void win_do_firing(uint32_t self) {
    state_call(self, 10);
    if (anim_done(self)) C(self).state = 9;
}

// [driving] 0x00122c30 AICharacterEnemyWindow::DoUnarming
void win_do_unarming(uint32_t self) {
    if (!anim_done(self)) return;
    AICharacterData& c = C(self);
    c.state = 2;
    c.sub_state = 1;
    time_scale(self, 1.0f);
}

// [driving] 0x00122c88 AICharacterEnemyWindow::DoHurting (0x001213c8 o do Sunroof, igual)
void enemy_do_hurting(uint32_t self) {
    if (anim_done(self)) C(self).state = 9;
}

// [driving] 0x00122cc0 AICharacterEnemyWindow::DoDying
void win_do_dying(uint32_t self) {
    AICharacterData& c = C(self);
    if (c.counts_hits != 0) {
        api().inc_kills(1);
        c.counts_hits = 0;
    }
    if (!anim_done(self)) return;
    ++g_stats.deaths;
    c.sub_state = 0x16;
    c.state = 2;
}

// [driving] 0x00122d20 AICharacterEnemyWindow::DoDead (0x00121678 o do Sunroof): vazio
void enemy_do_dead(uint32_t) {}

// [driving] 0x00122d28 AICharacterEnemyWindow::HandleInterrupts: o carro morto mata o atirador; a contagem armada.
void win_handle_interrupts(uint32_t self) {
    AICharacterData& c = C(self);
    BondCar* car = car_of(car_word(self));
    const float hp = car ? car->hit_points() : 1.0f;  // PhysicsObject::GetHitPoints (0x00195a30)
    if (!ee::lt(0.0f, hp) && c.state != 0xf && c.state != 0x16 && c.state != 2) {
        c.state = 0xf;
        if (visible(actor_of(self)) != 1) return;
        new_anim(self, kWinDeath[0]);
        return;
    }
    AICharacterEnemyWindowData& w = WD(self);
    if (0 < w.countdown) w.countdown -= 1;
}

// [driving] 0x00122e20 AICharacterEnemyWindow::TargetIsInRange: o alvo dentro do alcance (+0x64, ao quadrado) e entre
// -90 e 140 graus da frente do carro.
int32_t win_target_in_range(uint32_t self) {
    alignas(16) float m[16] = {};
    rot_pos(self, m, nullptr);
    float t[3];
    target_pos(self, t);
    const float d2 = vu_dist_sq(t, m + 12);
    if (ee::lt(C(self).range_sq, d2)) return 0;
    const float a = bearing(t, m);
    return ee::lt(-0.25f, a) && ee::lt(a, ps2::ffloat(0x3ec71c72u)) ? 1 : 0;
}

// [driving] 0x00123058 AICharacterEnemyWindow::TargetIsAcquired: a arma (o osso dela) dentro de dois graus do alvo; o
// lado em +0x84, o lugar do veículo alvo em +0xc0 (+0xcc 1).
int32_t win_target_acquired(uint32_t self) {
    AICharacterData& c = C(self);
    alignas(16) float w[16] = {};
    if (const uint32_t a = actor_of(self)) aapi().weapon_position(a, w, 0);
    float t[3];
    if (target_pos(self, t)) {
        std::memcpy(c.avoid_base, t, 12);
        c.unknowncc = 1;
    } else {
        c.unknowncc = 0;
    }
    const float a = bearing(t, w);
    c.target_right = ee::lt(0.0f, a) ? 1 : 0;
    const float lim = ps2::ffloat(0x3bb60b61u);
    return ee::lt(ps2::ffloat(0xbbb60b61u), a) && ee::lt(a, lim) ? 1 : 0;
}

// [driving] 0x00123298 AICharacterEnemyWindow::PlayZoneInjuryAnim(int, float, int): a morte (a vida acabou) ou o
// ferimento, com o grito (não portado) e uma das duas animações ao acaso, pelo crossfade.
void win_play_zone_injury_anim(uint32_t self, float damage, int32_t, int32_t) {
    AICharacterData& c = C(self);
    float hp = 1.0f;
    if (c.hit_points != 0) hp = *nf::p32_ptr<const float>(c.hit_points);
    if (ee::le(hp, damage)) {
        if (c.state == 0xf) return;
        if (c.state != 0x16) {
            api().say(self, "HDeath");  // 0x3691f0
            c.state = 0xf;
            const int32_t r = int32_t(SimRandom_Generate(sim_random()) << 1) >> 16;
            injury_anim(self, c.bank, kWinDeath[r]);
            return;
        }
    }
    if (c.state == 0xf || c.state == 0x16) return;
    api().say(self, "HGrunt");  // 0x3691f8
    c.state = 0xe;
    const int32_t r = int32_t(SimRandom_Generate(sim_random()) << 1) >> 16;
    injury_anim(self, c.bank, kWinHurt[r]);
}

// ------------------------------------------------------------------------------------------------- o teto solar

// O sistema 0x13 da animação do carro (o teto solar): o estímulo 8 abre, o 9 fecha (RAnimEngine::Handle::
// ProcessStimuli 0x001a27a0 com o passo da simulação e o modo 2).
void sunroof_stimulus(uint32_t self, uint32_t stimulus) {
    BondCar* car = car_of(car_word(self));
    uint32_t h = 0;
    if (car && car->d.render_object != 0) std::memcpy(&h, nf::p32_ptr<const uint8_t>(car->d.render_object) + 0x28, 4);
    else guard(__LINE__);  // o PS2 leria o RSceneObj nulo
    api().process_stimuli(h, 0x13, stimulus, uint32_t(simulation().steps), 2);
}

// [driving] 0x00120d98 AICharacterEnemySunroof::DoInitial
void sun_do_initial(uint32_t self) {
    if (!anim_done(self)) return;
    C(self).state = 1;
    set_visible(actor_of(self), 0);
    human_deactivate(self);
    find_others(self, SD(self).others);
}

// [driving] 0x00120e80 AICharacterEnemySunroof::DoNeutral: abre o teto e sobe (o alvo da mira passa do carro a ele).
void sun_do_neutral(uint32_t self) {
    BondCar* car = car_of(car_word(self));
    if (!car || car->d.reset_available != 0) return;
    const AIVtable& v = vt(self);
    if (!v.target_in_range || v.target_in_range(self) == 0) return;
    api().disable_beacon(vehicle_ptr(self));  // a entrada +0x7c do carro
    AICharacterEnemySunroofData& s = SD(self);
    if (s.target != 0) api().register_target(s.target);
    s.base.state = 8;
    set_visible(actor_of(self), 1);
    human_activate(self);
    sunroof_stimulus(self, 8);
    new_anim(self, flat(kSunRise, 4, s.base.index));
}

// [driving] 0x00120f90 AICharacterEnemySunroof::DoArming
void sun_do_arming(uint32_t self) {
    state_call(self, 10);
    if (anim_done(self)) C(self).state = 9;
}

// [driving] 0x00120fe0 AICharacterEnemySunroof::DoArmed
void sun_do_armed(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    AICharacterData& c = s.base;
    BondCar* car = car_of(car_word(self));
    const AIVtable& v = vt(self);
    if (car && car->d.reset_available == 0 && v.target_in_range && v.target_in_range(self) != 0) {
        if (!AICharacterEnemySunroof_VehicleIsAccelerating(self)) {
            if (v.needs_to_reload && v.needs_to_reload(self) != 0) {
                c.clip = c.clip_size;
                c.state = 0xc;
                ++g_stats.reloads;
                new_anim(self, flat(kSunReload, 4, c.index));
                return;
            }
            state_call(self, 10);
            if (c.unknownac == 0) return;
            if (!v.target_acquired || v.target_acquired(self) == 0) return;
            const Simulation& sim = simulation();
            if (sim.steps < c.unknown90) return;
            c.state = 0xb;
            c.clip -= 1;
            c.unknown90 = sim.steps + c.unknown8c;
            const uint32_t r = SimRandom_Generate(sim_random());
            ++g_stats.fires;
            new_anim(self, flat(kSunFire, 24, ((int32_t(r) * 6) >> 16) + c.index * 6));
            return;
        }
        c.sub_state = 0x15;
        c.state = 0x10;
        ++g_stats.leans;
        new_anim(self, flat(kSunLean, 16, s.accel - 1 + c.index * 4));
        return;
    }
    api().enable_beacon(vehicle_ptr(self), 0);  // a entrada +0x74 do carro
    if (s.target != 0) api().unregister_target(s.target);
    c.state = 0xd;
    sunroof_stimulus(self, 9);
    new_anim(self, flat(kSunDown, 4, c.index));
}

// [driving] 0x00121250 AICharacterEnemySunroof::DoAiming: o ator gira 0,005 volta por passo para o lado do alvo.
void sun_do_aiming(uint32_t self) {
    const AIVtable& v = vt(self);
    if (v.target_acquired && v.target_acquired(self) != 0) return;
    ++g_stats.aims;
    float turns = 0.0f;
    const int32_t side = C(self).target_right;
    if (side == 0) turns = ps2::ffloat(0xbba3d70au);
    else if (side == 1) turns = ps2::ffloat(0x3ba3d70au);
    if (const uint32_t a = actor_of(self)) api().rotate_actor(a, turns);
}

// [driving] 0x001212d8 AICharacterEnemySunroof::DoFiring e 0x00121328 DoReloading (iguais)
void sun_do_firing(uint32_t self) {
    state_call(self, 10);
    if (anim_done(self)) C(self).state = 9;
}

// [driving] 0x00121378 AICharacterEnemySunroof::DoUnarming
void sun_do_unarming(uint32_t self) {
    if (!anim_done(self)) return;
    C(self).state = 1;
    set_visible(actor_of(self), 0);
    human_deactivate(self);
}

// [driving] 0x00121400 AICharacterEnemySunroof::DoDying
void sun_do_dying(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    if (s.target != 0) api().unregister_target(s.target);
    api().enable_beacon(vehicle_ptr(self), 0);
    if (s.base.counts_hits != 0) {
        api().inc_kills(1);
        s.base.counts_hits = 0;
    }
    if (!anim_done(self)) return;
    ++g_stats.deaths;
    s.base.state = 0x16;
    set_visible(actor_of(self), 0);
    human_deactivate(self);
    sunroof_stimulus(self, 9);
}

// [driving] 0x001214d0 AICharacterEnemySunroof::DoTilting: a inclinação (o estado 0x10 com o 0x14 DoRunning ou o 0x15
// DoLeaning) e a velocidade do carro guardada para o próximo VehicleIsAccelerating.
void sun_do_tilting(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    if (s.base.sub_state == 0x14) state_call(self, 20);
    else if (s.base.sub_state == 0x15) state_call(self, 21);
    if (RigidBody* rb = car_body(self)) std::memcpy(s.vel, rb->vel, 12);
}

// [driving] 0x00121568 AICharacterEnemySunroof::DoRunning (a volta da inclinação)
void sun_do_running(uint32_t self) {
    if (!anim_done(self)) return;
    C(self).sub_state = 0;
    C(self).state = 9;
}

// [driving] 0x001215a8 AICharacterEnemySunroof::DoLeaning
void sun_do_leaning(uint32_t self) {
    if (!anim_done(self)) return;
    AICharacterEnemySunroofData& s = SD(self);
    if (!AICharacterEnemySunroof_VehicleIsAccelerating(self)) {
        s.base.sub_state = 0x14;
        new_anim(self, flat(kSunLeanBack, 16, s.accel - 1 + s.base.index * 4));
        s.accel = 0;
    } else {
        new_anim(self, flat(kSunLeanStay, 16, s.accel - 1 + s.base.index * 4));
    }
}

// [driving] 0x00121680 AICharacterEnemySunroof::HandleInterrupts: o dano tomado; a morte com o carro ou com o fim da
// missão (o estado 3 ou 4 do SMissionManager).
void sun_handle_interrupts(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    AICharacterData& c = s.base;
    BondCar* car = car_of(car_word(self));
    if (visible(actor_of(self)) != 1) {
        const float hp = car ? car->hit_points() : 1.0f;
        if (ee::lt(0.0f, hp)) return;
        if (c.state == 0x16 || c.state == 0xf) return;
        if (c.state != 2) {
            c.state = 0xf;
            return;
        }
        if (c.sub_state == 0x16) return;
        c.state = 0xf;
        return;
    }
    const float* hpp = c.hit_points ? nf::p32_ptr<const float>(c.hit_points) : nullptr;
    const float cur = hpp ? *hpp : 1.0f;
    c.damage_taken = ee::add(c.damage_taken, ee::sub(c.initial_hp, cur));
    c.initial_hp = hpp ? *hpp : 1.0f;
    int32_t mstate = 0;
    if (mission_exists()) mstate = mission_manager().state;
    else guard(__LINE__);  // o PS2 leria o gerente da missão (0x33a024)
    if (mstate != 3 && mstate != 4) {
        const float hp = car ? car->hit_points() : 1.0f;
        if (ee::lt(0.0f, hp)) {
            c.damage_taken = 0.0f;
            return;
        }
    }
    if (c.state != 0xf && c.state != 0x16) {
        c.state = 0xf;
        new_anim(self, flat(kSunDown, 4, c.index));
    }
}

// [driving] 0x00121818 AICharacterEnemySunroof::TargetIsInRange: dentro do alcance, sem outro passageiro do carro de
// fora (visível).
int32_t sun_target_in_range(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    float t[3];
    target_pos(self, t);
    const float d2 = vu_dist_sq(t, s.base.matrix + 12);
    if (ee::lt(s.base.range_sq, d2)) return 0;
    for (uint32_t o : s.others) {
        if (o == 0) continue;
        uint32_t h = C(o).actor;
        const uint32_t a = h ? an::act_actor_of(h) : 0u;
        if (!a) guard(__LINE__);
        if (visible(a) == 1) return 0;
    }
    return 1;
}

// [driving] 0x001219c0 AICharacterEnemySunroof::TargetIsAcquired (a matriz do próprio personagem com o índice 3, a da
// arma nos outros: o AICharacterEnemy::TargetIsAcquired com o alvo do veículo)
int32_t sun_target_acquired(uint32_t self) {
    AICharacterData& c = C(self);
    alignas(16) float w[16] = {};
    if (const uint32_t a = actor_of(self)) aapi().weapon_position(a, w, 0);
    float t[3];
    if (target_pos(self, t)) {
        std::memcpy(c.avoid_base, t, 12);
        c.unknowncc = 1;
    } else {
        c.unknowncc = 0;
    }
    const float a = bearing(t, c.index == 3 ? c.matrix : w);
    c.target_right = ee::lt(0.0f, a) ? 1 : 0;
    const float lim = ps2::ffloat(0x3bb60b61u);
    return ee::lt(ps2::ffloat(0xbbb60b61u), a) && ee::lt(a, lim) ? 1 : 0;
}

// [driving] 0x00121dd8 AICharacterEnemySunroof::PlayZoneInjuryAnim: com o carro sob o EMP (a entrada +0x144) qualquer
// tiro mata; a animação pela arma e pela zona atingida (+0xa8), a da morte no banco 9.
void sun_play_zone_injury_anim(uint32_t self, float damage, int32_t, int32_t) {
    AICharacterData& c = C(self);
    float hp = 1.0f;
    if (c.hit_points != 0) hp = *nf::p32_ptr<const float>(c.hit_points);
    bool death = !ee::lt(damage, hp);
    if (!death) {
        BondCar* car = car_of(car_word(self));
        death = car && car->d.emp_active != 0;  // a entrada +0x144 da tabela do carro
    }
    auto pick = [&](bool d) {
        const int base = (d ? 0 : 128) + c.index * 32 + c.zone_last_hit * 4;
        int32_t n = 0;
        if (flat(kSunInjury, 256, base) != 0) {
            n = 1;
            for (int i = 1; i <= 3 && flat(kSunInjury, 256, base + i) != 0; ++i) ++n;
        }
        const uint32_t r = SimRandom_Generate(sim_random());
        return flat(kSunInjury, 256, base + ((n * int32_t(r)) >> 16));
    };
    if (death) {
        if (c.state == 0xf) return;
        if (c.state != 0x16) {
            api().say(self, "HDeath");  // 0x368ef8
            c.state = 0xf;
            const int32_t anim = pick(true);
            c.bank = 9;
            injury_anim(self, 9, anim);
            return;
        }
    }
    if (c.state == 0xf || c.state == 0x16) return;
    api().say(self, "HGrunt");  // 0x368f00
    c.state = 0xe;
    injury_anim(self, c.bank, pick(false));
}

// ------------------------------------------------------------------------------------------------- o motorista

// O IK e as sobreposições dos pulsos (0x326950: as duas entradas de 0x30 bytes do ActIKSolver::SetSolveInfo, o alvo
// nos primeiros 16 bytes de cada uma; o HandleInterrupts as reescreve).
alignas(16) uint32_t g_driver_ik[24] = {0xbf239581u, 0x3d89374cu, 0x3ea4dd2fu, 0x3f800000u, 0x3f5db22du, 0xbf000000u,
                                        0,           0x3f800000u, 0x3f800000u, 0,           0,           0,
                                        0xbe4fdf3bu, 0x3d48b439u, 0x3ea66666u, 0x3f800000u, 0x3f5db22du, 0xbf000000u,
                                        0,           0x3f800000u, 0x3f800000u, 0,           0,           0};
constexpr int32_t kDriverIdle[3] = {8, 8, 7};  // 0x326940

// [driving] 0x0011ade0 AICharacterEnemyDriver::DoInitial: as mãos no volante pelos efeitos do carro.
void drv_do_initial(uint32_t self) {
    if (!anim_done(self)) return;
    AICharacterEnemyDriverData& d = DD(self);
    d.base.state = 1;
    BondCar* car = car_of(car_word(self));
    const uint32_t scene = car ? car->d.render_object : 0u;
    const uint32_t l = api().lookup_fx(scene, "steerleft");
    const uint32_t r = api().lookup_fx(scene, "steerright");
    if (l == 0 || r == 0) return;
    d.fx_right = r;
    d.fx_left = l;
    const uint32_t a = actor_of(self);
    if (!a) return;
    int32_t bones[2] = {api().get_bone_index(a, "Root.LShoulder"), api().get_bone_index(a, "Root.RShoulder")};
    alignas(16) float va[8] = {0, -1.0f, 0, 0, 0, 1.0f, 0, 0};
    alignas(16) float vb[2] = {0, 0};
    api().create_iks(a, 2, bones, va, vb);
    api().set_ik_info_array(a, 2, reinterpret_cast<const float*>(g_driver_ik));
    bones[0] = api().get_bone_index(a, "Root.LWrist");
    bones[1] = api().get_bone_index(a, "Root.RWrist");
    api().create_overrides(a, 2, bones, 0);
}

// [driving] 0x0011af60 AICharacterEnemyDriver::DoNeutral: uma das três animações de espera, em volta.
void drv_do_neutral(uint32_t self) {
    AICharacterEnemyDriverData& d = DD(self);
    d.base.state = 2;
    uint32_t i = uint32_t(d.idle) + 1;
    d.idle = int32_t(i);
    if (2 < i) d.idle = 0;
    d.base.anim = kDriverIdle[d.idle];
    if (const uint32_t a = actor_of(self)) aapi().set_new_animation(a, d.base.bank, d.base.anim);
}

// [driving] 0x0011afc0 AICharacterEnemyDriver::DoIdling
void drv_do_idling(uint32_t self) {
    if (anim_done(self)) C(self).state = 1;
}

// [driving] 0x0011aff8 AICharacterEnemyDriver::HandleInterrupts: os pulsos nos efeitos do volante (no espaço do carro).
void drv_handle_interrupts(uint32_t self) {
    AICharacterEnemyDriverData& d = DD(self);
    if (d.fx_left == 0 || d.fx_right == 0) return;
    BondCar* car = car_of(car_word(self));
    const uint32_t scene = car ? car->d.render_object : 0u;
    alignas(16) float l[16] = {}, r[16] = {}, m[16] = {}, out[32];
    api().locate_fx(scene, d.fx_left, l);
    api().locate_fx(scene, d.fx_right, r);
    rot_pos(self, m, nullptr);
    OrthoInverse(reinterpret_cast<float(*)[4]>(m));
    VU0_MATRIX4_mult(reinterpret_cast<const float(*)[4]>(l), reinterpret_cast<const float(*)[4]>(m),
                     reinterpret_cast<float(*)[4]>(out));
    VU0_MATRIX4_mult(reinterpret_cast<const float(*)[4]>(r), reinterpret_cast<const float(*)[4]>(m),
                     reinterpret_cast<float(*)[4]>(out + 16));
    std::memcpy(g_driver_ik, out + 12, 16);       // 0x326950
    std::memcpy(g_driver_ik + 12, out + 28, 16);  // 0x326980
    const uint32_t a = actor_of(self);
    if (!a) return;
    api().set_ik_info_array(a, 2, reinterpret_cast<const float*>(g_driver_ik));
    static const float kWeights[2] = {1.0f, 1.0f};  // 0x3269b0
    api().set_overrides(a, 2, out, kWeights);
}

// ------------------------------------------------------------------------------------------------- as tabelas

void destroy_window(uint32_t self) {
    AICharacterEnemyWindow_Destruct(WD(self));
    delete &WD(self);
}
void destroy_sunroof(uint32_t self) {
    AICharacterEnemySunroof_Destruct(SD(self));
    delete &SD(self);
}
void destroy_driver(uint32_t self) {
    AICharacterEnemyDriver_Destruct(DD(self));
    delete &DD(self);
}

// A base das tabelas: as entradas do AICharacter registradas por driving/game/ai_character.cpp.
AIVtable from_base(uint32_t address) {
    const AIVtable* base = ai_vtable(kVtAICharacter);
    AIVtable v = base ? *base : AIVtable{};
    v.address = address;
    return v;
}
int32_t base_needs_to_reload(uint32_t self) { return AICharacterEnemy_NeedsToReload(self) ? 1 : 0; }

struct Registrar {
    Registrar() {
        // 0x369200 a janela
        AIVtable w = from_base(kVtAICharacterEnemyWindow);
        w.destroy = destroy_window;
        w.set_actor = win_set_actor;
        w.slot24 = get_vehicle_ptr;
        w.play_zone_injury_anim = win_play_zone_injury_anim;
        w.states[0] = win_do_initial;
        w.states[1] = win_do_neutral;
        w.states[2] = win_do_idling;
        w.states[8] = win_do_arming;
        w.states[9] = win_do_armed;
        w.states[10] = win_do_aiming;
        w.states[11] = win_do_firing;
        w.states[13] = win_do_unarming;
        w.states[14] = enemy_do_hurting;
        w.states[15] = win_do_dying;
        w.states[22] = enemy_do_dead;
        w.handle_interrupts = win_handle_interrupts;
        w.update_rot_pos = update_rot_pos;
        w.target_in_range = win_target_in_range;
        w.needs_to_reload = base_needs_to_reload;
        w.target_acquired = win_target_acquired;
        ai_register_vtable(w);
        // 0x368f08 o teto
        AIVtable s = from_base(kVtAICharacterEnemySunroof);
        s.destroy = destroy_sunroof;
        s.slot24 = get_vehicle_ptr;
        s.play_zone_injury_anim = sun_play_zone_injury_anim;
        s.states[0] = sun_do_initial;
        s.states[1] = sun_do_neutral;
        s.states[8] = sun_do_arming;
        s.states[9] = sun_do_armed;
        s.states[10] = sun_do_aiming;
        s.states[11] = sun_do_firing;
        s.states[12] = sun_do_firing;  // DoReloading (0x00121328, igual ao DoFiring)
        s.states[13] = sun_do_unarming;
        s.states[14] = enemy_do_hurting;
        s.states[15] = sun_do_dying;
        s.states[16] = sun_do_tilting;
        s.states[20] = sun_do_running;
        s.states[21] = sun_do_leaning;
        s.states[22] = enemy_do_dead;
        s.handle_interrupts = sun_handle_interrupts;
        s.update_rot_pos = update_rot_pos;
        s.target_in_range = sun_target_in_range;
        s.needs_to_reload = base_needs_to_reload;
        s.target_acquired = sun_target_acquired;
        ai_register_vtable(s);
        // 0x3681f0 o motorista (a entrada +0x24 é a da base: 0)
        AIVtable d = from_base(kVtAICharacterEnemyDriver);
        d.destroy = destroy_driver;
        d.states[0] = drv_do_initial;
        d.states[1] = drv_do_neutral;
        d.states[2] = drv_do_idling;
        d.handle_interrupts = drv_handle_interrupts;
        d.update_rot_pos = update_rot_pos;
        ai_register_vtable(d);
        auto cb = [](uint32_t user, float m[16], float vel[3]) { rot_pos(user, m, vel); };
        an::act_register_callback(kFnWindowGetRotPos, cb);
        an::act_register_callback(kFnSunroofGetRotPos, cb);
        an::act_register_callback(kFnDriverGetRotPos, cb);
    }
};
// (a base é registrada por ai_character.cpp; a ordem dos construtores estáticos entre arquivos não é garantida: o
// registro dos passageiros fica para o primeiro uso, em passenger_register())
bool g_registered = false;

}  // namespace

void passenger_register() {
    if (g_registered) return;
    g_registered = true;
    static const Registrar r;
    (void)r;
}

PassengerStats& passenger_stats() { return g_stats; }
uint32_t* passenger_driver_ik() { return g_driver_ik; }
void passenger_reset() { g_stats = PassengerStats{}; }

std::string passenger_report() {
    char b[400];
    std::snprintf(b, sizeof b,
                  "passageiros janela %llu teto %llu motorista %llu | tiros %llu recargas %llu miras %llu mortes %llu "
                  "inclinacoes %llu | gritos %llu crossfades %llu ik %llu guardas %llu |",
                  (unsigned long long)g_stats.windows, (unsigned long long)g_stats.sunroofs,
                  (unsigned long long)g_stats.drivers, (unsigned long long)g_stats.fires,
                  (unsigned long long)g_stats.reloads, (unsigned long long)g_stats.aims,
                  (unsigned long long)g_stats.deaths, (unsigned long long)g_stats.leans,
                  (unsigned long long)g_stats.says, (unsigned long long)g_stats.crossfades,
                  (unsigned long long)g_stats.iks, (unsigned long long)g_stats.guards);
    std::string r = b;
    if (!g_stats.guard_lines.empty()) {
        r += " (linhas";
        for (const auto& kv : g_stats.guard_lines) {
            std::snprintf(b, sizeof b, " %d:%llu", kv.first, (unsigned long long)kv.second);
            r += b;
        }
        r += ")";
    }
    for (Human* h : simulation_humans()) {
        const uint32_t ch = h->h.character;
        if (ch == 0) continue;
        const AICharacterData& c = C(ch);
        const char* kind = c.vtable == kVtAICharacterEnemyWindow    ? "janela"
                           : c.vtable == kVtAICharacterEnemySunroof ? "teto"
                           : c.vtable == kVtAICharacterEnemyDriver  ? "motorista"
                                                                    : nullptr;
        if (!kind) continue;
        const uint32_t a = c.actor ? an::act_actor_of(c.actor) : 0u;
        const float hp = c.hit_points ? *nf::p32_ptr<const float>(c.hit_points) : -1.0f;
        std::snprintf(b, sizeof b, " %s e%d/%d %s pente %d vida %.0f @%.0f,%.0f,%.0f", kind, c.state, c.sub_state,
                      visible(a) ? "fora" : "dentro", c.clip, double(hp), double(c.matrix[12]), double(c.matrix[13]),
                      double(c.matrix[14]));
        r += b;
    }
    // Os vidros (o EHitWindow) e os efeitos dos impactos na galeria (GFX::TriggerIncidental).
    const HitWindowStats& hw = hit_window_stats();
    std::snprintf(b, sizeof b,
                  "\nvidros eventos %llu quebrados %llu alarmes %llu farois %llu efeitos %llu/%llu guardas %llu | "
                  "galeria incidentais %llu",
                  (unsigned long long)hw.events, (unsigned long long)hw.shatter, (unsigned long long)hw.alarm,
                  (unsigned long long)hw.headlight, (unsigned long long)hw.glass, (unsigned long long)hw.smoke,
                  (unsigned long long)hw.guards, (unsigned long long)gfx_stats().incidental);
    r += b;
    // Os tiros dos outros no carro do jogador (os raios e os projéteis) e a vida que tiraram.
    std::snprintf(b, sizeof b, " | jogador baleado %llu dano %.1f explosoes %llu dano %.1f",
                  (unsigned long long)shells_stats().player_hits, shells_stats().player_damage,
                  (unsigned long long)effects_stats().player_hits, effects_stats().player_damage);
    r += b;
    // Os gatilhos que os raios dos tiros tocaram (o índice: as vezes).
    r += " | raios_nos_gatilhos";
    if (trigger_stats().ray_triggers.empty()) r += " -";
    for (const auto& kv : trigger_stats().ray_triggers) {
        std::snprintf(b, sizeof b, " %d:%llu", int(kv.first), (unsigned long long)kv.second);
        r += b;
    }
    return r + "\n";
}

PassengerApi passenger_default_api() {
    PassengerApi a;
    a.set_time_scale = an::ActActor_SetTimeScale;
    a.get_bone_index = an::ActActor_GetBoneIndex;
    a.create_overrides = an::ActActor_CreateGlobalPoseOverrides;
    a.set_overrides = an::ActActor_SetGlobalPoseOverrides;
    a.rotate_actor = an::ActActor_RotateActor;
    // O crossfade (não portado no ActPoser): a animação nova sem a mistura; o controlador nulo.
    a.set_new_crossfade = [](uint32_t actor, float, int32_t bank, int32_t index) -> uint32_t {
        ++g_stats.crossfades;
        an::ActActor_SetNewAnimation(actor, bank, index);
        return 0u;
    };
    a.start_new_crossfade = [](uint32_t, float, int32_t, int32_t) { ++g_stats.crossfades; };
    a.create_iks = [](uint32_t, int32_t, const int32_t*, const float*, const float*) { ++g_stats.iks; };
    a.set_ik_info_array = [](uint32_t, int32_t, const float*) {};
    a.lookup_fx = [](uint32_t, const char*) -> uint32_t { return 0u; };
    a.locate_fx = [](uint32_t, uint32_t, float*) {};
    a.process_stimuli = [](uint32_t h, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode) {
        if (h) AnimHandle_ProcessStimuli(h, system, stimulus, steps, mode);
    };
    a.say = [](uint32_t, const char*) { ++g_stats.says; };
    a.inc_kills = [](int32_t n) {
        if (mission_exists()) SMissionManager_IncKills(mission_manager(), n);
    };
    a.register_target = WTargetPicker_RegisterTarget;
    a.unregister_target = WTargetPicker_UnregisterTarget;
    a.new_character_target = WTargetable_NewCharacter;
    a.remove_reference = WTargetable_RemoveReference;
    a.human_activate = [](uint32_t image) {
        for (Human* h : simulation_humans())
            if (Human_Image(*h) == image) return Human_Activate(*h);
        guard(__LINE__);
    };
    a.human_deactivate = [](uint32_t image) {
        for (Human* h : simulation_humans())
            if (Human_Image(*h) == image) return Human_Deactivate(*h);
        guard(__LINE__);
    };
    a.new_actor_handle = [](uint32_t user, const char* model, const char* weapon, uint32_t callback,
                            const float* m) -> uint32_t {
        return an::ActActorDatabase_GetNewActorHandle(user, model, weapon, nullptr, callback, m, 0);
    };
    a.spawn_human = [](const float dir[3], const float pos[3]) -> Human* {
        return SpawnHumanObject(simulation(), 0, dir, pos, 0);
    };
    a.human_set_character = [](Human& h, uint32_t ch) { Human_SetAICharacter(h, ch); };
    a.human_set_owner = [](Human& h, BondCar& car) { PhysicsObject_SetOwnerObject(h, &car); };
    a.enable_beacon = [](uint32_t car, int32_t create) {
        if (BondCar* c = car ? ai_car_from_physics(car) : nullptr) PBondCar_EnableTargetBeacon(*c, create != 0);
        else guard(__LINE__);
    };
    a.disable_beacon = [](uint32_t car) {
        if (BondCar* c = car ? ai_car_from_physics(car) : nullptr) PBondCar_DisableTargetBeacon(*c);
        else guard(__LINE__);
    };
    return a;
}

PassengerApi& passenger_api() {
    passenger_register();
    static PassengerApi a = passenger_default_api();
    return a;
}

// ------------------------------------------------------------------------------------------------- os construtores

// [driving] 0x001224b0 AICharacterEnemyWindow::AICharacterEnemyWindow
void AICharacterEnemyWindow_Construct(AICharacterEnemyWindowData& c, const float m[16], uint32_t car) {
    passenger_register();
    AICharacter_Construct(c.base);
    c.car = car;
    c.aim = 0.0f;
    c.base.vtable = kVtAICharacterEnemyWindow;
    c.others[0] = c.others[1] = 0;
    c.side = 0;
    c.countdown = 0;
    std::memcpy(c.base.matrix, m, 64);
    c.base.bank = 0xd;
    c.base.zone_map = ai_character_zone_map();
    ++g_stats.windows;
}

// [driving] 0x00120b70 AICharacterEnemySunroof::AICharacterEnemySunroof
void AICharacterEnemySunroof_Construct(AICharacterEnemySunroofData& c, const float m[16], int32_t weapon,
                                       int32_t unknown60, uint32_t car) {
    passenger_register();
    AICharacter_Construct(c.base);
    c.car = car;
    c.base.vtable = kVtAICharacterEnemySunroof;
    if (RigidBody* rb = car_body(nf::p32_u(&c))) std::memcpy(c.vel, rb->vel, 12);
    c.accel = 0;
    c.others[0] = c.others[1] = 0;
    if (weapon < 1) weapon = 1;
    else if (2 < weapon) weapon = 2;
    std::memcpy(c.base.matrix, m, 64);
    c.base.unknown60 = unknown60;
    c.base.index = weapon;
    c.target = api().new_character_target(nf::p32_u(&c));
    if (1 < weapon) c.base.clip_size = 0;
    if (weapon == 1) {
        c.base.clip_size = 0x19;
        c.base.bank = 0xb;
        c.base.clip = 0x19;
    } else {
        c.base.bank = weapon == 2 ? 10 : 0xb;
    }
    c.base.zone_map = ai_character_zone_map();
    ++g_stats.sunroofs;
}

// [driving] 0x0011aca8 AICharacterEnemyDriver::AICharacterEnemyDriver
void AICharacterEnemyDriver_Construct(AICharacterEnemyDriverData& c, const float m[16], uint32_t car) {
    passenger_register();
    AICharacter_Construct(c.base);
    c.car = car;
    c.fx_left = 0;
    c.base.vtable = kVtAICharacterEnemyDriver;
    c.fx_right = 0;
    c.idle = 0;
    std::memcpy(c.base.matrix, m, 64);
    c.base.bank = 0;
    ++g_stats.drivers;
}

// [driving] 0x001225a8 AICharacterEnemyWindow::~AICharacterEnemyWindow
void AICharacterEnemyWindow_Destruct(AICharacterEnemyWindowData& c) {
    c.base.vtable = kVtAICharacterEnemyWindow;
    AICharacter_Destruct(c.base);
}

// [driving] 0x00120d18 AICharacterEnemySunroof::~AICharacterEnemySunroof: o alvo sai da mira (o tipo 0) e perde a
// referência.
void AICharacterEnemySunroof_Destruct(AICharacterEnemySunroofData& c) {
    c.base.vtable = kVtAICharacterEnemySunroof;
    if (c.target != 0) {
        api().unregister_target(c.target);
        nf::p32_ptr<WTargetableData>(c.target)->kind = 0;
        api().remove_reference(c.target);
    }
    AICharacter_Destruct(c.base);
}

// [driving] 0x0011ad88 AICharacterEnemyDriver::~AICharacterEnemyDriver
void AICharacterEnemyDriver_Destruct(AICharacterEnemyDriverData& c) {
    c.base.vtable = kVtAICharacterEnemyDriver;
    AICharacter_Destruct(c.base);
}

void AICharacterEnemy_CarRotPos(uint32_t self, float m[16], float vel[3]) { rot_pos(self, m, vel); }

// [driving] 0x00121c10 AICharacterEnemySunroof::VehicleIsAccelerating: a mudança da velocidade do carro desde o último
// DoTilting na frente (> 45: 1, < -45: 2) ou no lado (> 40: 3, < -40: 4); a primeira direção fica em +0x16c e só ela
// conta até o DoLeaning a zerar.
bool AICharacterEnemySunroof_VehicleIsAccelerating(uint32_t self) {
    AICharacterEnemySunroofData& s = SD(self);
    RigidBody* rb = car_body(self);
    if (!rb) return false;
    const float* ori = &nf::raw(rb->info)->orient[0][0];
    uint32_t d[3];
    for (int i = 0; i < 3; ++i) d[i] = ps2::sub(ps2::fbits(rb->vel[i]), ps2::fbits(s.vel[i]));
    // vmul.xyz, vaddy.x, vaddz.x: (x x' + y y') + z z'
    auto dot = [&](const float* row) {
        const uint32_t x = ps2::mul(ps2::fbits(row[0]), d[0]);
        const uint32_t y = ps2::mul(ps2::fbits(row[1]), d[1]);
        const uint32_t z = ps2::mul(ps2::fbits(row[2]), d[2]);
        return ps2::ffloat(ps2::add(ps2::add(x, y), z));
    };
    int32_t code = 0;
    bool result = true;
    if (ee::lt(45.0f, dot(ori + 8))) code = 1;
    else if (ee::lt(dot(ori + 8), -45.0f)) code = 2;
    else if (ee::lt(40.0f, dot(ori))) code = 3;
    else if (ee::lt(dot(ori), -40.0f)) code = 4;
    else result = false;
    if (s.accel == 0) s.accel = code;
    else if (s.accel != code) result = false;
    return result;
}

// ------------------------------------------------------------------------------------------------- o WakeUp

namespace {
// Os modelos (0x327ba8) e as armas (0x327b90) dos passageiros pelos índices do CARP::AIElement.
const char* const kModels[32] = {
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_pmask.h_pmask.t_paris.95",
    "2.h_pmask.h_pmask.t_paris.95",  "2.h_pmask.h_pmask.t_paris.95",  "2.h_pmask.h_pmask.t_paris.95",
    "2.h_snow.h_snow.t_snow.100",    "2.h_snowt.h_snowt.t_snow.100",  "2.h_snowt.h_snowt1.t_snow.100",
    "2.h_jguya.h_jguya.t_jguya.100", "2.h_jguyb.h_jguyb.t_jguyb.100", "2.h_jguyb.h_jguyc.t_jguyc.100",
    "2.h_julg.h_julg.t_julg.100",    "2.h_julg.h_julg.t_julg.100",    "3.c_domm.h_dommin.t_dommin.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_snow.h_snowh.t_snowh.100"};  // 0x327ba8
const char* const kWeapons[3] = {nullptr, "ssmg", "bazooka"};  // 0x327b90
const char* passenger_model(int32_t i) { return (i >= 0 && i < 32) ? kModels[i] : nullptr; }
const char* passenger_weapon(int32_t i) { return (i >= 0 && i < 3) ? kWeapons[i] : nullptr; }
}  // namespace

// [driving] 0x0012b8b0 AIElementController::AIElementController (a parte dos passageiros: o ActCharacter::LoadCharacter)
void AIElementController_LoadPassenger(uint32_t element, int32_t k) {
    const AIPassenger& p = nf::p32_ptr<const AIElement>(element)->passenger[k];
    const char* model = passenger_model(p.model);
    if (!model) {
        guard(__LINE__);
        return;
    }
    an::ActCharacter_LoadCharacter(model, passenger_weapon(p.weapon), nullptr);
}

// [driving] 0x0012cb08 AIElementController::WakeUp (a parte de um passageiro; a contagem dos personagens acordados e o
// resto ficam em driving/ai/element_controller.cpp)
uint32_t AIElementController_WakeUpPassenger(uint32_t element, int32_t k, BondCar& car) {
    passenger_register();
    AIElement& e = *nf::p32_ptr<AIElement>(element);
    AIPassenger& p = e.passenger[k];
    // A matriz do assento: a identidade (o (1, 0, 0, 0) com pexew e pexcw) com o vetor do atributo na linha 3.
    alignas(16) float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto seat = [&](const char* key) {
        auto it = car.attr_vectors.find(key);
        if (it != car.attr_vectors.end()) std::memcpy(m + 12, it->second.data(), 16);
        else guard(__LINE__);  // (o PS2 daria o vetor da coleção padrão)
    };
    const char* model = passenger_model(p.model);
    const char* weapon = passenger_weapon(p.weapon);
    if (!model) guard(__LINE__);
    const uint32_t car_image = nf::p32_u(&car.d);
    uint32_t self = 0, callback = 0;
    int32_t bank = 0, anim = 0;
    switch (p.type) {
        case 3: {
            seat("AIC_EWIND_POS");
            auto* c = new AICharacterEnemyWindowData{};  // UMemory::FastAlloc(0x170)
            AICharacterEnemyWindow_Construct(*c, m, car_image);
            self = nf::p32_u(c);
            callback = kFnWindowGetRotPos;
            bank = 0xd;
            anim = 2;
            break;
        }
        case 4: {
            seat("AIC_ESUNR_POS");
            auto* c = new AICharacterEnemySunroofData{};  // UMemory::FastAlloc(0x180)
            AICharacterEnemySunroof_Construct(*c, m, p.weapon, 0, car_image);
            self = nf::p32_u(c);
            callback = kFnSunroofGetRotPos;
            bank = 0xb;
            anim = 0x12;
            break;
        }
        case 0x11: {
            seat("AIC_BOND_POS");
            auto* c = new AICharacterEnemyDriverData{};  // UMemory::FastAlloc(0x160)
            AICharacterEnemyDriver_Construct(*c, m, car_image);
            self = nf::p32_u(c);
            callback = kFnDriverGetRotPos;
            weapon = nullptr;
            bank = 0;
            anim = 8;
            break;
        }
        default:
            // 0xe, 0xf, 0x10: os passageiros da neve (AICharacterEnemySnow, AICharacterEnemySSnow), que Paris não tem:
            // não portados (contados).
            guard(__LINE__);
            return 0;
    }
    // ActActorDatabase::GetNewActorHandle (0x0010b298: o 0x0010b3b8 sem a segunda arma) e o SetActor (+0x1c).
    const uint32_t handle = api().new_actor_handle(self, model ? model : "", weapon, callback, m);
    const AIVtable& v = vt(self);
    if (v.set_actor) v.set_actor(self, handle);
    if (const uint32_t a = handle ? an::act_actor_of(handle) : 0u) aapi().set_new_animation(a, bank, anim);
    else guard(__LINE__);
    return self;
}

// [driving] 0x0012cb08 AIElementController::WakeUp (o laço dos passageiros, depois do carro e da contagem dos inimigos)
int32_t AIElementController_WakeUpPassengers(uint32_t element, int32_t& characters_awake, BondCar& car) {
    AIElement& e = *nf::p32_ptr<AIElement>(element);
    int32_t created = 0;
    for (int k = 0; k < e.passengers; ++k) {
        AIPassenger& p = e.passenger[k];
        if (!ee::lt(0.0f, p.health)) continue;
        const uint32_t ch = AIElementController_WakeUpPassenger(element, k, car);
        if (ch != 0) {
            e.characters[k] = ch;
            ++characters_awake;
            ++created;
        }
        if (e.characters[k] == 0) return created;  // (o PS2 sai do WakeUp numa classe que não conhece)
        AICharacterData& cd = C(e.characters[k]);
        // O alcance pela precisão do passageiro (+0x1c): 2/4 o dobro, 3/5 o triplo (ao quadrado: x4, x9).
        if (p.accuracy == 2 || p.accuracy == 4) cd.range_sq = ee::mul(cd.range_sq, 4.0f);
        if (p.accuracy == 3 || p.accuracy == 5) cd.range_sq = ee::mul(cd.range_sq, 9.0f);
        Human* hm = api().spawn_human(e.matrix[2], e.matrix[3]);  // Simulation::SpawnHumanObject(0, dir, pos, 0)
        e.humans[k] = hm ? Human_Image(*hm) : 0u;
        if (hm) {
            cd.hit_points = nf::p32_u(&p.health);
            cd.initial_hp = p.health;
            cd.world_pos = Human_Image(*hm);
            api().human_set_character(*hm, e.characters[k]);  // Human::SetAICharacter
            api().human_set_owner(*hm, car);                  // PhysicsObject::SetOwnerObject
            api().human_activate(Human_Image(*hm));
            const uint32_t one = 1;
            std::memcpy(&hm->h.hit_pos[3], &one, 4);  // o +0x8c do Human: 1 (inteiro)
            // O passageiro na arma do ator (o +0x15c do ActWeapon: a precisão e o dano do EFireWeapon).
            const uint32_t actor = cd.actor ? an::act_actor_of(cd.actor) : 0u;
            const uint32_t chr = actor ? nf::p32_ptr<const an::ActActorData>(actor)->character : 0u;
            const uint32_t w0 = chr ? nf::p32_ptr<const an::ActCharacterData>(chr)->weapon[0] : 0u;
            if (w0) nf::p32_ptr<an::ActWeaponData>(w0)->accuracy = nf::p32_u(&p);
        }
        if (e.flags & kElemNoScore) cd.counts_hits = 0;
    }
    return created;
}

}  // namespace drv
