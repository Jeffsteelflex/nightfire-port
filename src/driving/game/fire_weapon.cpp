// O tiro das armas do carro e dos personagens: o EFireWeapon (o evento 10 da chave da animação do lançador ou da arma do
// personagem; DTuningFile.obj), do MIPS do DRIVING.ELF (docs/driving.md seções 22 e 24).
//
// Como o PS2 atira: o SWeaponManager::FireWeapon (driving/game/weapons.h) dá o estímulo ao sistema de animação do cano;
// a chave da animação (RAnimEngine::Handle, driving/render/anim_engine.h) cria o EFireWeapon com o RSceneObj, a cópia
// da instância e o seu índice (o EventDynamicData) e, se a animação é a de uma arma de personagem, o ActWeapon (o +0x14:
// o ActWeapon::SetEventDynamicData 0x00116678 guarda o objeto em 0x32e73c, e o RSceneObj do evento é o +0x40 dele). O
// destrutor, no fim da passagem do escalonador, acha onde e para onde (o efeito do evento, RSceneObj::LocateFX, ou a
// cópia da instância, RSceneObj::GetInstancePosition; na arma de Bond (o ActWeapon com o bit 1 de +0x138) a identidade
// pela RSceneObj::TransformMatrixByInstancePosition; a linha 3 é o lugar, a 2 (ou a 0/1 do efeito) a direção), qual arma
// (o sistema da instância, SWeaponManager::GetWeaponFromSystem), quem atira (o dono do RSceneObj, +0x24, ou o do ActWeapon,
// +0x144; o carro do jogador para a arma de Bond), a velocidade dele, o alvo (o WTargetPicker do jogador), a pontaria
// (abaixo), o dano (a tabela das armas) e cria o projétil: o Missile (Simulation::SpawnMissileObject), o RayShell (as
// balas: 0, 0x17, 0x18, 0x1a, 0x1b, 0x1d), o Shell (o canhão 0x10), a mina e a granada (não portadas); depois o som do
// tiro, o tranco no controle de quem atirou e a zona da IA do jogador.
//
// A pontaria: o helicóptero (o dono de tipo 10: a arma 2 mira um WTargetable no lugar dele, +0x134 -> +0x110, a 0 a
// direção dele, +0xf0); a câmera da direção automática e a arma de Bond (RPlayerCamera::GetForwardAimVec4; o lugar da
// câmera, menos nos foguetes, e o foguete que mira o veículo da IA parado sob a câmera: a arma 0x19); os personagens e as
// sentinelas (o alvo do ator, ou o jogador, com o avanço dos foguetes; a precisão do ActWeapon +0x15c e o acréscimo da
// dificuldade, SMissionManager::GetEnemyAccuracyBoost; a direção perturbada pelo Noise::Noise1 do passo e da assinatura,
// Util_PerturbVector, salvo quando o SimRandom acerta); os carros da IA (o AIGroundVehicle: a precisão +0x130, o dano
// +0x131, a mira +0x240/+0x250, o AI_GOODAIM).
//
// Portado do PS2 (0x0014ca60, 7 KB de MIPS); os nomes são do DRIVING.SYM e do nightfire-cxbx (NightfireResearch, a versão
// de Xbox). Conferido nas partes 18 e 20 do nf_driving_check (tests/driving_check_missile.cpp,
// tests/driving_check_integ_shells*.cpp). O que não foi portado é contado (fire_stats()) e anotado (effects_trace()):
//  - a mina (7, Simulation::SpawnMineObject 0x001ffd70) e a granada (0x13, SpawnGrenadeObject 0x001ffea8): os objetos não
//    existem no port;
//  - o render: RSceneObj::LocateFX, GetInstancePosition, TransformMatrixByInstancePosition, TransformToWorldSpace,
//    GetInstanceSystemID e UseInstance vêm de effects_env()/shells_env() (o nf_driving liga o que tem);
//  - o ator do personagem: o +0x140 do ActActor (a entrada +0x24 e a +0x13c do que ela devolve) vem de
//    shells_env().actor_vehicle_value; a Sentry::GetTarget de shells_env().sentry_target; o WTargetable do carro do
//    jogador (a entrada +0x29c) de shells_env().player_targetable;
//  - o AMenuSound (os sons "POV Weapons") e o aviso de depuração das armas sem som ficam anotados;
//  - o BulletStreakCount (a tabela 0x423f38 do RBulletStreak::Init, lida de um banco de dados de depuração) vem de
//    effects_env().streak_count (0 sem ele: o contador +0x64 do dono fica em 0).
// Divergência: nos tiros 1, 2, 0x10, 0x13 e 0x15 de um personagem que não é Bond, o PS2 dá ao AOneShotSound como nome da
// mistura o lixo da pilha (sp+0xb0, a mira do personagem: o sprintf do nome só roda para o jogador); o port usa
// "Henchmen Fire" e conta (guards).

#include <cstdio>
#include <cstring>

#include "driving/ai/ai_vu.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/player_camera.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/mission.h"
#include "driving/game/missile.h"
#include "driving/game/shells.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using aivu::copy3;

namespace {

FireStats g_fire;
// Os contadores do .data do som das metralhadoras (0x32bba0 o nome, 0x32bba4 o dos personagens, 0x32bba8 o dos carros da
// IA).
FireSoundCounters g_snd;

uint32_t rd32(uint32_t p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, 4);
    return v;
}
void wr32(uint32_t p, uint32_t off, uint32_t v) { std::memcpy(nf::p32_ptr<uint8_t>(p) + off, &v, 4); }
float rdf(uint32_t p, uint32_t off) { return ps2::ffloat(rd32(p, off)); }

int32_t lookup_int(uint32_t image, const char* key) {
    if (effects_env().lookup_int) return effects_env().lookup_int(image, key);
    return 0;
}

// O sentido da direção (Float_COORD4toCOORD3: x, y e z).
void to3(float out[3], const float in[4]) { copy3(out, in); }

// A precisão de 0 a 100 (slti 0x65 + movz e -1 < x).
int32_t clamp_percent(int32_t v) {
    if (!(v < 0x65)) v = 100;
    if (!(-1 < v)) v = 0;
    return v;
}

// O dano de 0 a 200 % (a soma com 100 limitada) em fator.
float damage_factor(int32_t pct) {
    int32_t v = pct + 100;
    if (!(v < 0xc9)) v = 200;
    if (!(-1 < v)) v = 0;
    return ee::div(ee::cvt_s(v), 100.0f);
}

// O ruído da pontaria da IA pela dificuldade (0,01 na 1, 0,005 na 2, senão 0,015).
float aim_noise_scale() {
    const int32_t d = mission_exists() ? mission_manager().difficulty : 0;
    if (!mission_exists()) ++g_fire.guards;
    if (d == 1) return ps2::ffloat(0x3c23d70au);
    if (d == 2) return ps2::ffloat(0x3ba3d70au);
    return ps2::ffloat(0x3c75c28fu);
}

// A direção perturbada pelo Noise::Noise1 do passo e do byte de cima da assinatura (sem o bit 31); `exact` fica se o
// sorteio do SimRandom (já feito) acerta.
void noisy_aim(float out[4], const float aim[4], uint32_t sig, int32_t roll, int32_t accuracy, float scale) {
    Simulation& sim = simulation();
    const float t = ee::mul(ee::cvt_s(sim.steps), ps2::ffloat(0x3ca3d70au));  // x 0,02
    const float b = ee::cvt_s(int32_t((sig >> 24) & 0x7f));
    const float n1 = ee::mul(Noise_Noise1(ee::add(t, b)), scale);
    const float yaw = ee::add(n1, n1);
    const float t2 = ee::mul(ee::cvt_s(sim.steps), ps2::ffloat(0x3ca3d70au));
    const float n2 = ee::mul(Noise_Noise1(ee::add(ee::add(t2, b), ps2::ffloat(0x43898000u))), scale);  // + 275
    float p[3];
    Util_PerturbVector(yaw, ee::add(n2, n2), p, aim);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = 1.0f;
    if (roll < accuracy) {
        copy3(out, aim);
        out[3] = 1.0f;
    }
}

// AIGroundVehicle::GetTargetPos (0x0012e7d0): o lugar do alvo primário (+0x1a4) ou o ponto +0x1a8.
const float* ai_target_pos(AIGroundVehicle& v) {
    if (v.primary_target == 0) return v.unknown1a8;
    return AIVehicle_GetPosition(*nf::p32_ptr<AIGroundVehicle>(v.primary_target));
}

// O som de um tiro: um AOneShotSound no lugar com a velocidade de quem atirou.
AOneShotSound* fire_sound(const char* mix, ABank* bank, int32_t patch, const float pos[3], const float vel[3],
                          float min_d, float max_d, float max2) {
    AOneShotSound* s = new AOneShotSound(mix, bank->snd_bank, patch, 1.0f);
    std::memcpy(s->pos, pos, 12);
    std::memcpy(s->vel, vel, 12);
    s->max_distance2 = max2;
    s->min_distance = min_d;
    s->max_distance = max_d;
    ++g_fire.sounds;
    return s;
}

bool level_is(const char* name) {
    const char* a = effects_env().level ? effects_env().level : "";
    for (;; ++a, ++name) {
        const int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, cb = (*name >= 'A' && *name <= 'Z') ? *name + 32 : *name;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

}  // namespace

FireStats& fire_stats() { return g_fire; }
FireSoundCounters& fire_sound_counters() { return g_snd; }

// [driving] 0x0014ca60 EFireWeapon::~EFireWeapon
void EFireWeapon_Execute(const PendingEvent& e) {
    const uint32_t* o = e.obj;
    Simulation& sim = simulation();
    EffectsEnv& env = effects_env();
    ShellsEnv& senv = shells_env();
    ++g_fire.events;
    const uint32_t effect = o[1], scene = o[2], aw = o[5];
    uint32_t index = o[4];           // sp+0xf4
    uint32_t fx_effect = 0;          // sp+0xf8 o efeito de tipo 6 (vai ao RayShell)
    uint32_t fx_scene = 0;           // sp+0xfc
    const bool aw_player = aw != 0 && ((rd32(aw, 0x138) >> 1) & 1) != 0;  // sp+0x100 a arma de Bond
    if (scene == 0) {
        ++g_fire.guards;  // o PS2 leria o RSceneObj nulo
        return;
    }
    if (aw) ++g_fire.ai_vehicle;  // o tiro de um ActWeapon (um personagem)
    alignas(16) float m[4][4] = {};  // sp+0x10
    int row = 2;                     // s3: a linha da direção
    if (effect != 0) {
        // RSceneObj::LocateFX (0x001cae00, o render): o lugar do efeito.
        ++g_fire.locate_fx;
        effects_note(0x001cae00, scene, effect);
        if (!senv.locate_fx || !senv.locate_fx(scene, effect, m)) ++g_fire.guards;
        const uint8_t* fx = nf::p32_ptr<const uint8_t>(effect);
        if (fx[0x14] == 6) {
            fx_effect = effect;
            index = fx[0x15];
            fx_scene = scene;
        } else if (fx[0x14] == 2) {
            index = fx[0x15];
        }
        row = aw_player ? 0 : 1;
    } else {
        // RSceneObj::GetInstancePosition(índice, m, true) (0x001c9368, o render).
        if (!env.instance_position || !env.instance_position(scene, int32_t(index), m)) {
            ++g_fire.guards;
            return;
        }
        effects_note(0x001c9368, scene, index);
    }
    if (aw_player) {
        // A arma de Bond: a identidade (0x3ad6a8) levada pela RSceneObj::TransformMatrixByInstancePosition (0x001c95f0).
        std::memset(m, 0, sizeof m);
        m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
        effects_note(0x001c95f0, scene, index);
        if (senv.transform_by_instance) senv.transform_by_instance(scene, index, m);
        else ++g_fire.guards;
    } else if (senv.to_world) {
        senv.to_world(scene, m);  // a entrada +0x3c (TransformToWorldSpace: vazia no RSceneObj e no RVehicle)
    }
    uint32_t s6 = rd32(scene, 0x24);
    if (s6 == 0 && aw != 0) s6 = rd32(aw, 0x144);
    PhysicsObject* owner = s6 ? object_of_image(sim, s6) : nullptr;
    if (s6 && !owner) ++g_fire.guards;  // um dono que o port não acha pela imagem
    PhysicsObject* player_obj = sim.cars.empty() ? nullptr : sim.cars[0];
    uint32_t sig = 0;  // sp+0x50
    if (aw_player) sig = PhysicsObject_GetSig(GetPlayerObject(sim));
    else if (owner) sig = PhysicsObject_GetSig(owner);
    // RSceneObj::GetInstanceSystemID (0x001c9328) e SWeaponManager::GetWeaponFromSystem.
    const uint32_t system = env.instance_system ? env.instance_system(scene, o[4]) : 0;  // sp+0x108
    SWeaponManagerData* wm = weapon_manager();
    const int32_t weapon = SWeaponManager_GetWeaponFromSystem(int32_t(system));  // s5
    const int32_t type = owner ? owner->kind() : 0;
    const bool heli = owner && type == 10;                     // sp+0x10c
    const bool car = owner && type == 1;                       // sp+0x110
    const bool player = car && owner == player_obj;            // sp+0x114
    const bool cam_aim = (mission_exists() && mission_manager().auto_drive != 0 && player) || aw_player;  // sp+0x118
    const bool sentry = owner && type == 11;                   // s2
    bool flying = false;                                       // sp+0x11c
    if (car && lookup_int(s6, "IS_FLYING") == 1) flying = !player && !heli;
    uint32_t target = 0;  // sp+0x104
    if (player || aw_player) {
        // WTargetPicker::GetSelectedTarget (0x0022af38) e CycleToNextTarget (0x0022b6b8) do 0x33a970
        target = weapons_env().selected_target ? weapons_env().selected_target() : 0;
        effects_note(0x0022af38, target);
        effects_note(0x0022b6b8);
    }
    // A velocidade de quem atira (f22) e o vetor (sp+0x70).
    float speed = 0.0f;
    alignas(16) float vel[4] = {};
    if (owner) {
        if (SimpleObject* so = dynamic_cast<SimpleObject*>(owner)) {
            const SimpleRigidBody* b = GetSimpleRigidBody(sim, so->po.slot);
            copy3(vel, b->vel);
            speed = SimpleRigidBody_GetScalarVelocity(b);
        } else {
            int slot = -1;
            for (int i = 0; i < kRigidBodySlots; ++i)
                if (sim.owners[i] == owner) slot = i;
            if (slot < 0) {
                ++g_fire.guards;
                return;
            }
            const RigidBody* rb = GetRigidBody(sim, slot);
            speed = aivu::len(rb->vel);
            copy3(vel, rb->vel);
        }
    } else if (aw_player) {
        BondCar* pc = sim.cars.empty() ? nullptr : sim.cars[0];
        if (!pc) {
            ++g_fire.guards;
            return;
        }
        const RigidBody* rb = GetRigidBody(sim, pc->d.slot);
        copy3(vel, rb->vel);
        speed = aivu::len(rb->vel);
    }
    // O contador dos rastros das balas (o +0x64 de quem atira, ou o +0x160 do ActWeapon): a cada BulletStreakCount tiros
    // um rastro; o tipo (sp+0x130) vai às balas, -1 nos outros tiros.
    int32_t streak = 0;  // sp+0x130
    auto count_streak = [&](int32_t st, uint32_t old, auto&& store) {
        const int32_t count = env.streak_count ? env.streak_count(st) : 0;
        const uint32_t next = old + 1;
        store(int32_t(next) < count ? next : 0u);
        return old != 0 ? -1 : st;
    };
    if (aw_player) {
        if (weapon == 0x1a) {
            streak = -1;
        } else if (player_obj) {
            const int32_t st = lookup_int(object_image(player_obj), "BULLET_STREAK_TYPE");
            streak = count_streak(st, object_word(player_obj, 0x64),
                                  [&](uint32_t v) { object_set_word(player_obj, 0x64, v); });
        } else {
            ++g_fire.guards;
        }
    } else if (owner) {
        const int32_t st = lookup_int(s6, "BULLET_STREAK_TYPE");
        streak = count_streak(st, object_word(owner, 0x64), [&](uint32_t v) { object_set_word(owner, 0x64, v); });
    } else if (aw) {
        const int32_t st = int32_t(rd32(aw, 0x164));
        streak = count_streak(st, rd32(aw, 0x160), [&](uint32_t v) { wr32(aw, 0x160, v); });
    }
    if (!cam_aim) {
        fx_effect = 0;
        fx_scene = 0;
    }
    float* dirp = m[row];
    float* pos = m[3];
    alignas(16) float muzzle[4];  // sp+0x80
    std::memcpy(muzzle, m[3], 16);
    float factor = 1.0f;  // f23
    if (heli) {
        // O helicóptero: a arma 2 mira o WTargetable no lugar dele com metade do y da direção; a 0, a direção dele.
        ++g_fire.helicopter;
        const uint32_t hd = object_word(owner, 0x134);
        if (weapon == 2) {
            if (hd) {
                const float p[3] = {rdf(hd, 0x110), rdf(hd, 0x114), rdf(hd, 0x118)};
                target = WTargetable_NewAtPoint(p);
                nf::p32_ptr<WTargetableData>(target)->fixed = 1;
            } else {
                ++g_fire.guards;
            }
            dirp[1] = ee::mul(dirp[1], 0.5f);
        } else if (weapon == 0) {
            if (hd) std::memcpy(dirp, nf::p32_ptr<const uint8_t>(hd) + 0xf0, 16);
            else ++g_fire.guards;
        }
    } else if (cam_aim) {
        // RPlayerCamera::GetForwardAimVec4(1) (0x001bcca0, driving/camera/camera_aim.h): a frente da câmera; menos
        // nos foguetes, o lugar dela também.
        ++g_fire.camera_aim;
        PlayerCamera* cam = current_player_camera();
        if (cam) {
            std::memcpy(dirp, RPlayerCamera_GetForwardAimVec4(*cam, 1.0f), 16);
            if (weapon != 1 && weapon != 0x19 && weapon != 0x1c && weapon != 0x1e) std::memcpy(pos, &cam->d.matrix[12], 16);
        } else {
            ++g_fire.guards;  // o PS2 leria a câmera nula
        }
    } else if ((aw != 0 && !aw_player) || sentry) {
        // Um personagem ou uma sentinela.
        ++g_fire.ai_aim;
        uint32_t acc_info = 0, actor = 0;  // s3, s1
        if (aw) {
            const uint32_t holder = rd32(aw, 0x144);
            acc_info = rd32(aw, 0x15c);
            PhysicsObject* hobj = holder ? object_of_image(sim, holder) : nullptr;
            if (hobj) actor = object_word(hobj, 0x70);
            else ++g_fire.guards;  // o PS2 leria o +0x70 do dono nulo
            if (acc_info) factor = damage_factor(int32_t(rd32(acc_info, 0xc)));
        }
        alignas(16) float aim[4] = {};  // sp+0x90
        if (actor && rd32(actor, 0xcc) == 1) {
            std::memcpy(aim, nf::p32_ptr<const uint8_t>(actor) + 0xc0, 12);
        } else {
            const float* pp = player_obj ? PhysicsObject_GetPosition(player_obj) : nullptr;
            if (pp) copy3(aim, pp);
            else ++g_fire.guards;
            if (weapon == 1) {
                // O foguete leva em conta a velocidade do jogador: o tempo do voo é 3/4 da distância (pelo menos 20).
                const float* pv = PhysicsObject_GetLinearVelocity(player_obj);
                float d[3];
                aivu::sub3(d, aim, pos);
                const float lead = ee::max(20.0f, ee::mul(aivu::len(d), 0.75f));
                float add[3];
                aivu::scale3(add, pv, ee::mul(sim.dt, lead));
                aivu::add3(aim, aim, add);
            } else if (weapon == 2 && sentry) {
                // Sentry::GetTarget (0x0019bea0): a sentinela não foi portada.
                effects_note(0x0019bea0);
                target = senv.sentry_target ? senv.sentry_target(owner) : 0;
            }
        }
        alignas(16) float dir[4] = {};  // sp+0xb0
        float d0[3];
        aivu::sub3(d0, aim, pos);
        aivu::unit(dir, d0);
        if (target == 0) {
            int32_t acc = acc_info ? int32_t(rd32(acc_info, 0x10)) : 100;
            const float scale = aim_noise_scale();
            acc += SMissionManager_GetEnemyAccuracyBoost();
            if (actor) {
                int32_t value = 0;
                if (senv.actor_vehicle_value && senv.actor_vehicle_value(actor, value)) acc -= (4 - value) * 30;
                if (weapon == 1 && ee::lt(dir[1], ps2::ffloat(0xbe4ccccdu))) {
                    dir[1] = ps2::ffloat(0xbe4ccccdu);  // -0,2: o foguete não desce mais que isso
                    aivu::unit(dir, dir);
                    acc = 0;
                }
            }
            acc = clamp_percent(acc);
            const uint32_t r = SimRandom_Generate(sim_random());
            const int32_t roll = int32_t(r * 100) >> 16;
            noisy_aim(dirp, dir, sig, roll, acc, scale);
        }
    } else if (car && !player) {
        // Um carro da IA (o AIGroundVehicle pela entrada +0x5c da tabela virtual).
        ++g_fire.ai_aim;
        AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(static_cast<BondCar*>(owner)->d.ai_ground_vehicle);
        if (!v) {
            ++g_fire.guards;
        } else {
            factor = damage_factor(v->damage);
            if (weapon == 0x15) {
                target = senv.player_targetable ? senv.player_targetable() : 0;
                effects_note(0x0018029c, target);
            } else if (weapon == 2) {
                const int32_t acc = clamp_percent(int32_t(int8_t(v->accuracy)) + SMissionManager_GetEnemyAccuracyBoost());
                const uint32_t r = SimRandom_Generate(sim_random());
                if ((int32_t(r * 100) >> 16) < acc) {
                    target = senv.player_targetable ? senv.player_targetable() : 0;
                    effects_note(0x0018029c, target);
                } else {
                    target = WTargetable_NewAtPoint(ai_target_pos(*v));
                    nf::p32_ptr<WTargetableData>(target)->fixed = 1;
                    dirp[1] = ee::mul(dirp[1], 0.5f);
                }
            } else if (uint32_t(weapon) < 2 || weapon == 0x10) {
                const int32_t acc = clamp_percent(int32_t(int8_t(v->accuracy)) + SMissionManager_GetEnemyAccuracyBoost());
                const uint32_t r = SimRandom_Generate(sim_random());
                const int32_t roll = int32_t(r * 100) >> 16;
                alignas(16) float aim[4] = {};  // sp+0x90
                if (weapon == 0) {
                    if (lookup_int(s6, "AI_GOODAIM") == 1) {
                        const float* pp = player_obj ? PhysicsObject_GetPosition(player_obj) : nullptr;
                        float d[3] = {};
                        if (pp) aivu::sub3(d, pp, pos);
                        else ++g_fire.guards;
                        aivu::unit(aim, d);
                    } else {
                        copy3(aim, v->unknown240);
                    }
                } else if (weapon == 1) {
                    copy3(aim, v->unknown250);
                } else {
                    copy3(aim, v->unknown240);
                }
                noisy_aim(dirp, aim, sig, roll, acc, aim_noise_scale());
            }
        }
    }
    if (!wm) {
        ++g_fire.guards;  // o PS2 leria a tabela do gerente nulo
        return;
    }
    // O dano (f20): o da arma vezes o fator; RPlayerCamera::WeaponFired no carro do jogador.
    float damage = 0.0f;
    if (uint32_t(weapon) < 0x20) damage = ee::mul(ee::cvt_s(weapon_entry(*wm, weapon)->damage), factor);
    if (player) {
        if (senv.weapon_fired) {
            senv.weapon_fired(weapon);
        } else if (mission_exists() && mission_manager().auto_drive != 0 && weapon == 0x1c && env.camera_1f4) {
            // RPlayerCamera::WeaponFired (0x001bd288): na direção automática, o 0x1c marca o passo na câmera.
            *env.camera_1f4 = sim.steps;
        }
    }
    if (uint32_t(weapon) >= 0x20) return;  // -1: nenhuma arma no sistema (o sltiu do PS2: nem som nem tranco)
    WeaponEntry* entry = weapon_entry(*wm, weapon);
    auto ray = [&](float range, float dmg) {
        RayShellData r{};
        RayShell_Construct(r, range, dmg, 0.0f, weapon, sig, pos, dirp, muzzle, fx_scene, fx_effect, 1, streak);
    };
    auto missile = [&](int32_t w, const float* dir, const float* at) {
        // Simulation::SpawnMissileObject e RSceneObj::UseInstance(o RSceneObj do míssil, o do carro, o índice, a
        // identidade, 0xfb, 0x40): o port guarda a instância para o desenho.
        Missile* mis = SpawnMissileObject(sim, speed, damage, w, dir, at, sig, target, int32_t(system));
        if (mis) {
            mis->source_scene = scene;
            mis->source_instance = int32_t(index);
            effects_note(0x001c7b60, scene, index);
            ++g_fire.missiles;
        }
        return mis;
    };
    // O switch das armas (a tabela 0x0036f520).
    switch (weapon) {
        case 0:
        case 0x17: {
            if (player && !aw_player && target != 0 && wm->turret_on_target != 0 && senv.lookup_bool &&
                senv.lookup_bool(object_image(player_obj), "HAS_TURRET")) {
                // A metralhadora de torre do jogador mira o alvo (vsub.xyz e a unidade com o w: vmul.xyzw, vmulq.xyzw).
                const float* tp = nf::p32_ptr<const float>(target);
                dirp[0] = ee::sub(tp[0], pos[0]);
                dirp[1] = ee::sub(tp[1], pos[1]);
                dirp[2] = ee::sub(tp[2], pos[2]);
                const uint32_t one = 0x3f800000u;
                uint32_t s = ps2::mul(ps2::fbits(dirp[0]), ps2::fbits(dirp[0]));
                s = ps2::madd(s, one, ps2::mul(ps2::fbits(dirp[1]), ps2::fbits(dirp[1])));
                s = ps2::madd(s, one, ps2::mul(ps2::fbits(dirp[2]), ps2::fbits(dirp[2])));
                s = ps2::madd(s, one, ps2::mul(ps2::fbits(dirp[3]), ps2::fbits(dirp[3])));
                const uint32_t q = ps2::rsqrt(one, s);
                for (int i = 0; i < 4; ++i) dirp[i] = ps2::ffloat(ps2::mul(ps2::fbits(dirp[i]), q));
            }
            float dmg;
            if (heli) dmg = 5.0f;
            else if (cam_aim) dmg = ee::cvt_s(weapon_entry(*wm, 0x18)->damage);
            else dmg = ee::cvt_s(weapon_entry(*wm, 0)->damage);
            dmg = ee::mul(dmg, factor);
            if (rayshell_globals().enable != 0) {
                ray(220.0f, dmg);
            } else {
                if (cam_aim) {
                    float p[3];
                    Util_PerturbVectorGauss(ps2::ffloat(0x3b03126fu), p, dirp);  // 0,002
                    copy3(dirp, p);
                    dirp[3] = 1.0f;
                }
                SpawnShellObject(sim, dmg, 0.0f, weapon, dirp, pos, sig, streak);
            }
            break;
        }
        case 1:
        case 0x19:
        case 0x1c:
        case 0x1e:
            if (cam_aim && !heli) {
                // O foguete pela câmera: sem alvo e sem a mira da câmera, o veículo da IA escolhido no WTargetPicker, se
                // está parado (abaixo de 2 m/s) e vivo, vira o alvo e a arma vira 0x19.
                ++g_fire.camera_rocket;
                float at[3], dir[3];
                to3(at, pos);
                to3(dir, dirp);
                int32_t w = weapon;
                PlayerCamera* cam = current_player_camera();
                if (target == 0 && !cam) ++g_fire.guards;  // o PS2 leria o RPlayerCamera nulo
                if (target == 0 && !(cam && RPlayerCamera_CameraAiming(*cam) != 0)) {
                    const uint32_t sel = weapons_env().selected_target ? weapons_env().selected_target() : 0;
                    effects_note(0x0022af38, sel);
                    if (sel != 0) {
                        const WTargetableData& t = *nf::p32_ptr<const WTargetableData>(sel);
                        const uint32_t av = t.kind == 3 ? t.object : 0u;
                        if (av != 0 && rd32(av, 0) == 2) {
                            PhysicsObject* po = object_of_image(sim, rd32(av, 4));
                            if (!po) {
                                ++g_fire.guards;
                            } else if (ee::lt(aivu::len(PhysicsObject_GetLinearVelocity(po)), 2.0f) &&
                                       ee::lt(0.0f, po->hit_points())) {
                                target = rd32(av, 0x88);
                                if (target != 0) {
                                    WTargetable_AddRef(target);
                                    w = 0x19;
                                }
                            }
                        }
                    }
                }
                Missile* mis = missile(w, dir, at);
                if (mis) {
                    SimpleRigidBody* b = GetSimpleRigidBody(sim, mis->po.slot);
                    aivu::add3(b->vel, b->vel, vel);
                }
                break;
            }
            [[fallthrough]];
        case 2:
        case 0xe:
        case 0x15:
            missile(weapon, dirp, pos);
            break;
        case 0x16:
        case 0x1f:
            if (Missile* mis = missile(weapon, dirp, pos)) Missile_DriveMissile(nf::p32_u(mis));
            break;
        case 7:
            // Simulation::SpawnMineObject (0x001ffd70): a mina não foi portada.
            ++g_fire.other_projectiles;
            effects_note(0x001ffd70, uint32_t(weapon));
            break;
        case 0x13:
            // Simulation::SpawnGrenadeObject (0x001ffea8): a granada não foi portada.
            ++g_fire.other_projectiles;
            effects_note(0x001ffea8, uint32_t(weapon));
            break;
        case 0x10:
            SpawnShellObject(sim, ee::mul(damage, 0.75f), ee::mul(damage, 0.25f), weapon, dirp, pos, sig, streak);
            break;
        case 0x18:
        case 0x1b:
        case 0x1d:
            ray(220.0f, ee::cvt_s(weapon_entry(*wm, 0x1b)->damage));
            break;
        case 0x1a:
            ray(400.0f, ee::cvt_s(weapon_entry(*wm, 0x1a)->damage));
            break;
        default:
            return;  // 0x0014ecf4: nada (nem som nem tranco)
    }
    // O som do tiro: "SFX_<nome>Fire" no banco das armas (ABank::fgBanks[4]), procurado para toda arma que chega aqui;
    // o switch dos sons (a tabela 0x0036f5a0) só abaixo de 0x1f.
    ABank* bank = ABank_Category(4);
    int32_t patch = -1;
    if (bank) {
        char name[64];
        std::snprintf(name, sizeof name, "SFX_%sFire", entry->sound_name ? nf::p32_ptr<const char>(entry->sound_name) : "");
        patch = bank->index.Lookup(name);
        effects_note(0x002f5880, uint32_t(patch));
    } else {
        ++g_fire.guards;  // o PS2 leria o banco nulo
    }
    if (weapon < 0x1f) {
        if (bank) {
            switch (weapon) {
                case 1:
                case 2:
                case 5:
                case 0x10:
                case 0x13:
                case 0x15: {
                    // 0x0014e9a8: o AOneShotSound no lugar do tiro com a velocidade de quem atirou.
                    const char* mix = (aw ? aw_player : player) ? "Weapons Fire" : "Henchmen Fire";
                    if (aw && !aw_player) ++g_fire.guards;  // o lixo da pilha como nome (ver o cabeçalho)
                    if (weapon == 0x15 && car && !player) {
                        patch = bank->index.Lookup("SFX_htorpedofire");
                        effects_note(0x002f5880, uint32_t(patch));
                    }
                    fire_sound(mix, bank, patch, pos, vel, 50.0f, 200.0f, 40000.0f);
                    break;
                }
                case 3: case 0xa: case 0xb: case 0xe: case 0x12: case 0x16: case 0x19: case 0x1a: case 0x1c:
                case 0x1e:
                    // 0x0014e4f8: [driving] 0x002f4a20 AMenuSound::Trigger(o banco da arma, o timbre, "POV Weapons", 0)
                    ++g_fire.menu_sounds;
                    effects_note(0x002f4a20, uint32_t(entry->sound_bank), uint32_t(patch));
                    break;
                case 0:
                    // 0x0014e520: as metralhadoras dos personagens (o nome alternado; em snow1a_mis3 um tiro sim, outro
                    // não) e dos carros da IA (o nome do voador e o tom 0,75).
                    if (aw) {
                        if (aw_player) break;
                        g_snd.actor = level_is("snow1a_mis3") ? (g_snd.actor + 1) & 1 : 0;
                        if (g_snd.actor != 0) break;
                        const char* nm = (g_snd.name & 1) ? "SFX_HMachGun2" : "SFX_HMachGun1";
                        ++g_snd.name;
                        const int32_t p = bank->index.Lookup(nm);
                        effects_note(0x002f5880, uint32_t(p));
                        fire_sound("Henchmen Fire", bank, p, pos, vel, 20.0f, 200.0f, 40000.0f);
                    } else if (car && !player) {
                        uint32_t c = g_snd.car + 1;
                        if (!(int32_t(c) < 2)) c = 0;
                        if (!level_is("snow1a_mis3")) c = 0;
                        g_snd.car = c;
                        if (c != 0) break;
                        const int32_t p = bank->index.Lookup(flying ? "SFX_HumntmachFire" : "SFX_hmntmachfire");
                        effects_note(0x002f5880, uint32_t(p));
                        AOneShotSound* s = fire_sound("Henchmen Fire", bank, p, pos, vel, 20.0f, 300.0f, 90000.0f);
                        if (flying) s->pitch = 0.75f;
                    }
                    break;
                case 8: case 9: case 0xc: case 0xd: case 0xf: case 0x11: case 0x14: case 0x17:
                    effects_note(0x0010c6c8);  // "WARNING: Audio not playing WEAPONTYPE!"
                    break;
                default:
                    break;  // 4, 6, 7, 0x18, 0x1b, 0x1d: sem som
            }
        }
    } else {
        effects_note(0x0010c6c8);
    }
    // O tranco (0x0014ebec): as balas vibram, o resto dá um tranco 1,0 no controle de quem atirou (o do jogador na mira
    // pela câmera).
    BondCar* pc = sim.cars.empty() ? nullptr : sim.cars[0];
    const BondCar* who = cam_aim ? pc : dynamic_cast<BondCar*>(owner);
    const bool has_feedback = cam_aim ? true : (owner && object_word(owner, 0x50) != 0);
    if (has_feedback) {
        if (weapon == 0 || weapon == 0x1d || weapon == 0x1b || weapon == 0x18) {
            effects_note(0x001660f0, ps2::fbits(0.51f), ps2::fbits(0.2f));
            if (who) IFeedback_SetRumble(who->feedback, 0.51f, 0.2f);
            else ++g_fire.guards;
        } else {
            effects_note(0x00165fb0, ps2::fbits(1.0f));
            if (who) IFeedback_ApplyJolt(who->feedback, 1.0f);
            else ++g_fire.guards;
        }
        ++g_fire.jolts;
    }
    if (player) {
        // AIZoneController::RegisterZone(2, o lugar do tiro) (driving/ai/ground_vehicle.h).
        effects_note(0x001402f8, 2, ps2::fbits(pos[0]), ps2::fbits(pos[1]), ps2::fbits(pos[2]));
        AIZoneController_RegisterZone(2, pos);
    }
}

}  // namespace drv
