// Os eventos da IA (os destrutores do PS2 de DTuningFile.obj; driving/ai/element_controller.h, docs/driving.md seção 21):
// o EAICommand (um comando 'AICo' para um veículo), o EActivateAIElement e o EDeactivateAIElement (o bit 1 das
// bandeiras do elemento), o EKillAIElement (um ApplyDamage de 10^6), o EAIElementFireOn/Off e o
// EAIElementSetAccuracy (os passageiros) e o ESetRecSpeedOffset. As desmontagens ficam em
// out/agent_drv_ai/ do checkout principal (fora do git).
//
// A imagem do objeto do PS2 (PendingEvent::obj, driving/game/events.h): obj[0] a tabela virtual, obj[1..] os campos.
//
// Divergências (cada uma também na função):
//  - os personagens (os passageiros, os a pé) e os helicópteros não existem no port: os ramos deles contam
//    (ai_element_pending) e não fazem nada;
//  - o ESetNextAISpline, o EControlToSpline e o EEndControlToSpline (os trilhos 'AISp', também os dos elementos) são
//    de driving/game/rail.h, que pergunta ao controlador pelo veículo do elemento;
//  - guardas (contadas): o elemento ou o veículo nulos onde o PS2 leria o endereço 0.

#include <cstring>

#include "driving/ai/ai_vu.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "platform/ptr32.h"

namespace drv {

using namespace aivu;

namespace {

enum : int32_t {
    kEvAICommand = 65,
    kEvActivate = 66,
    kEvDeactivate = 67,
    kEvFireOff = 144,
    kEvFireOn = 145,
    kEvSetAccuracy = 159,
    kEvKill = 161,
    kEvSetRecSpeedOffset = 232,
};

constexpr int32_t kHardLimit = 1;  // gAIElementHardLimit (0x327c2c)

Simulation& sim() { return simulation(); }

BondCar* car_from_p32(uint32_t p) {
    if (p == 0) return nullptr;
    for (BondCar* c : sim().cars)
        if (nf::p32_u(&c->d) == p) return c;
    return nullptr;
}
BondCar* player_car() { return sim().cars.empty() ? nullptr : sim().cars.front(); }
RigidBody* player_body() {
    BondCar* p = player_car();
    return p ? GetRigidBody(sim(), p->d.slot) : nullptr;
}

AIElement* redirect(uint32_t carp) { return AIElementController_RedirectCarpElement(carp); }

// O teste do gatilho comum ao EAICommand e ao EActivateAIElement: quem disparou o gatilho (o corpo da vaga `slot`, ou
// o SimpleRigidBody com `simple`) tem de ser o jogador (sem elemento) ou o veículo do elemento `trigger_element`; o
// gatilho é então desligado (o bit 0). Um gatilho sem o bit dos carros do jogador (kTrigRigidType1) e sem elemento não
// confere quem tocou: o EAICommand o desliga (`clear_other`), o EActivateAIElement não. Devolve falso quando o evento não
// age.
bool trigger_check(uint32_t trigger, uint32_t trigger_element, uint32_t simple, uint32_t slot, bool clear_other) {
    WTrigger* t = nf::p32_ptr<WTrigger>(trigger);
    if (!t) return true;
    if (trigger_element == 0) {
        if (!(t->flags & kTrigRigidType1)) {
            if (clear_other) t->flags = uint16_t(t->flags & 0xfffe);
            return true;
        }
        if (simple != 0) return false;
        if (GetRigidBody(sim(), int(slot)) != player_body()) return false;
        t->flags = uint16_t(t->flags & 0xfffe);
        return true;
    }
    AIElement* el = redirect(trigger_element);
    if (!el) return false;
    if (el->state != 1) return false;
    AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(el->ai);
    if (!v) {
        ai_element_stats().guards++;
        return false;
    }
    if (simple == 0) {
        RigidBody* hit = GetRigidBody(sim(), int(slot));
        if (v->vehicle_type != 1) return true;
        if (v->active == 0) return true;
        BondCar* c = car_from_p32(v->physics);
        if (!c || GetRigidBody(sim(), c->d.slot) != hit) return false;
        t->flags = uint16_t(t->flags & 0xfffe);
        return true;
    }
    // O helicóptero (AIVehicle tipo 2) e o SimpleRigidBody: não portados.
    if (v->vehicle_type == 2 && v->active != 0) {
        ai_element_pending("trigger_check (helicóptero)");
        return false;
    }
    return true;
}

// O que o EAICommand faz num AIGroundVehicle (0x001462ac..0x0014687c).
void apply_command(AIGroundVehicle& v, const uint8_t* cmd, uint32_t cmd_p32) {
    v.ai_command = int32_t(cmd_p32);
    BondCar* car = car_of(v);
    if (!car) {
        ai_element_stats().guards++;
        return;
    }
    alignas(16) float pos[4];
    std::memcpy(pos, cmd + 0x40, 12);
    pos[3] = 0.0f;
    if (cmd[0x7b] != 0) {
        // O teletransporte: os carros do tráfego perto do ponto somem, e o carro vai para o ponto, de frente para a
        // linha 2 da matriz do comando (ResetCar, +0x164).
        RigidBody* vb = GetRigidBody(sim(), car->d.slot);
        for (BondCar* other : std::vector<BondCar*>(sim().cars)) {
            if (other->d.car_class != 3) continue;
            RigidBody* b = GetRigidBody(sim(), other->d.slot);
            const float d = dist(b->pos, pos);
            if (ee::le(d, ee::add(b->info->half_size[2], vb->info->half_size[2]))) {
                if (AIGroundVehicle* o = ai_of(*other)) AIGroundVehicle_UnSpawn(*o);
                else ai_element_stats().guards++;
            }
        }
        alignas(16) float dir[4];
        std::memcpy(dir, cmd + 0x20, 16);
        PBondCar_ResetCar(*car, pos, dir);
    }
    v.coll_avoidance = cmd[0x7c] == 0 ? 1 : 0;
    if (&v != AIVehicleController_GetPlayerGroundVehicle(0))
        AIGroundVehicle_SetTargetMode(v, cmd[0x78], reinterpret_cast<const float*>(cmd + 0x40));
    WRoadNav* nav = nav_of(v);
    nav->unknown04 = cmd[0x7f];
    if (int8_t(cmd[0x7e]) >= 0) nav->next_choice = int8_t(cmd[0x7e]);
    nav->unknownb1 = cmd[0x84];
    nav->unknownb2 = cmd[0x85];
    uint32_t path = 0;
    std::memcpy(&path, cmd + 0x80, 4);
    AIGroundVehicle_SetNavigateMode(v, cmd[0x79], path);
    AIGroundVehicle_SetAttackMode(v, cmd[0x7a]);
    auto f = [cmd](uint32_t off) {
        float x;
        std::memcpy(&x, cmd + off, 4);
        return x;
    };
    v.min_dist_to_target = f(0x4c);
    v.speed_limit = f(0x60);
    v.speed_scaler = f(0x5c);
    v.ram_freq = f(0x68);
    v.lead_scaler = f(0x6c);
    v.rate_of_fire = f(0x64);
    RigidBody* body = GetRigidBody(sim(), car->d.slot);
    float angular = f(0x74);
    if (!ee::eq(f(0x70), 0.0f) && !ee::eq(f(0x70), 1.5f)) {
        // A velocidade de partida: o momento ao longo da frente do carro (unit(linha 2) x velocidade x massa).
        float fwd[3];
        unit(fwd, body->info->orient[2]);
        const float s = ee::mul(f(0x70), body->mass);
        float m[3];
        scale3(m, fwd, s);
        aivu::copy3(body->momentum, m);
        angular = f(0x74);
    }
    if (!ee::eq(angular, 0.0f)) {
        // O giro de partida: o vetor 0x36e380 (0, 1, 0) x giro x massa (SetAngularMomentum).
        alignas(16) float l[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float s = ee::mul(angular, body->mass);
        scale3(l, l, s);
        SetAngularMomentum(body, l);
    }
    uint32_t secondary = 0;
    std::memcpy(&secondary, cmd + 0x50, 4);
    AIGroundVehicle_SetSecondaryTarget(v, secondary);
    v.recorded_glue = cmd[0x7d] != 0 ? 1 : 0;
    v.use_ceiling_floor = cmd[0x86];
    v.ceiling = f(0x88);
    v.floor = f(0x8c);
    if (car->attr_bool("HAS_MACHINEGUNS") || car->attr_bool("HAS_CANNONS"))
        v.has_machine_guns = (cmd[0x87] != 0 || cmd[0x92] != 0) ? 1 : 0;
    if (car->attr_bool("HAS_ROCKETS")) v.has_rockets = cmd[0x90];
    if (car->attr_bool("HAS_MISSLES") || car->attr_bool("HAS_TORPEDOS")) v.has_missiles = cmd[0x91];
    v.damage = int8_t(cmd[0x94]);
    v.force_full_update = 1;
    v.accuracy = cmd[0x93];
    v.valid_command = 1;
}

// [driving] 0x00145ff8 EAICommand::~EAICommand
// obj[1] o comando ('AICo'), obj[2] o elemento que tem de tocar o gatilho, obj[3] o elemento comandado, obj[4] o
// veículo (direto, do WakeUp), obj[5] o gatilho, obj[6] o SimpleRigidBody, obj[7] a vaga de quem tocou.
void EAICommand_Execute(const PendingEvent& e) {
    const uint32_t cmd_p32 = e.obj[1];
    if (cmd_p32 == 0) return;
    if (e.obj[5] != 0 && !trigger_check(e.obj[5], e.obj[2], e.obj[6], e.obj[7], true)) return;
    ++ai_element_stats().commands;
    const uint8_t* cmd = nf::p32_ptr<const uint8_t>(cmd_p32);
    AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(e.obj[4]);
    uint32_t character = 0;
    if (!v) {
        if (e.obj[3] != 0) {
            AIElement* el = redirect(e.obj[3]);
            if (!el) return;
            if (el->state == 1) {
                if (el->type == 1) v = nf::p32_ptr<AIGroundVehicle>(el->ai);
                else if (el->type == 0) character = el->ai;
            }
            // O comando vira o primeiro do elemento (o próximo WakeUp o repete), salvo os modos de ataque 9 e 10.
            if (uint8_t(cmd[0x7a] - 9) > 1) el->command = cmd_p32;
        } else {
            v = AIVehicleController_GetPlayerGroundVehicle(0);
        }
    }
    if (character != 0) {
        ai_element_pending("EAICommand (personagem)");
        return;
    }
    if (!v) return;
    if (v->vehicle_type != 1) {
        if (v->vehicle_type == 2 && v->active != 0) ai_element_pending("AIHelicopter::NewState");
        return;
    }
    if (v->active == 0) return;
    if (v->ai_command == int32_t(cmd_p32)) return;  // o mesmo comando outra vez: nada
    apply_command(*v, cmd, cmd_p32);
}

// [driving] 0x001476b8 EActivateAIElement::~EActivateAIElement
// obj[1] o elemento, obj[2] o elemento que tem de tocar o gatilho, obj[3] o gatilho, obj[4] o SimpleRigidBody, obj[5]
// a vaga.
void EActivateAIElement_Execute(const PendingEvent& e) {
    if (e.obj[3] != 0 && !trigger_check(e.obj[3], e.obj[2], e.obj[4], e.obj[5], false)) return;
    if (e.obj[1] == 0) return;
    AIElement* el = redirect(e.obj[1]);
    if (!el) return;
    bool ok = true;
    if (kHardLimit != 0 && el->type == 1 && !(el->flags & kElemPriority)) ok = AIElementController_OkToSpawnHench();
    if (ok) el->flags = uint16_t(el->flags & 0xfffd);
}

// [driving] 0x0014b510 EDeactivateAIElement::~EDeactivateAIElement
void EDeactivateAIElement_Execute(const PendingEvent& e) {
    if (e.obj[1] == 0) return;
    AIElement* el = redirect(e.obj[1]);
    if (!el) return;
    el->flags = uint16_t(el->flags | kElemInactive);
    if (el->type == 1) AIElementController_ForceVehicleToSleep(el->ai);
}

// [driving] 0x00150748 EKillAIElement::~EKillAIElement: um ApplyDamage de 10^6 (dividido 0,5, tipo 0) da posição do
// acompanhamento rumo a ela menos a do jogador; obj[2] conta a morte para o jogador.
void EKillAIElement_Execute(const PendingEvent& e) {
    if (e.obj[1] == 0) return;
    AIElement* el = redirect(e.obj[1]);
    AIElementTracking* t = AIElementController_GetCarpElementTracking(e.obj[1]);
    if (!el || !t) return;
    alignas(16) float from[4] = {t->pos[0], t->pos[1], t->pos[2], 0};
    alignas(16) float dir[4] = {0, 0, 0, 0};
    if (RigidBody* p = player_body()) sub3(dir, from, p->pos);
    else ai_element_stats().guards++;
    BondCar* car = nullptr;
    if (el->type == 0) {
        ai_element_pending("EKillAIElement (personagem)");
        return;
    }
    if (el->type == 1) {
        car = car_from_p32(el->physics);
        if (el->heli == 0) {
            if (e.obj[2] != 0) {
                if (!car) return;
                // O PS2 olha o +0x138 do PBondCar (um campo das normais das rodas, não o "pontua" +0x440: um engano do
                // original, mantido).
                uint32_t w;
                std::memcpy(&w, reinterpret_cast<const uint8_t*>(&car->d) + 0x138, 4);
                if (w != 0 && mission_exists()) {
                    SMissionManager_IncKills(mission_manager(), 1);
                    std::memset(reinterpret_cast<uint8_t*>(&car->d) + 0x138, 0, 4);
                }
            }
        } else {
            ai_element_pending("EKillAIElement (helicóptero)");
            return;
        }
    }
    if (car) car->apply_damage(from, dir, 1000000.0f, 0.5f, 0);
}

// [driving] 0x001469f8 EAIElementFireOff / 0x00146b48 EAIElementFireOn: o +0xac dos personagens (atirar).
void EAIElementFire_Execute(const PendingEvent& e, bool on) {
    if (e.obj[1] == 0) return;
    AIElement* el = redirect(e.obj[1]);
    if (!el) return;
    if (el->type == 0) {
        if (el->ai != 0) ai_element_pending(on ? "EAIElementFireOn (personagem)" : "EAIElementFireOff (personagem)");
    } else if (el->type == 1) {
        for (int k = 0; k < el->passengers; ++k)
            if (el->characters[k] != 0) ai_element_pending("EAIElementFire (passageiro)");
    }
}

// [driving] 0x00146ca8 EAIElementSetAccuracy: o +0x68 do personagem a pé, ou o +0x10 de cada passageiro do registro.
void EAIElementSetAccuracy_Execute(const PendingEvent& e) {
    if (e.obj[1] == 0) return;
    AIElement* el = redirect(e.obj[1]);
    if (!el) return;
    if (el->type == 0) {
        std::memcpy(reinterpret_cast<uint8_t*>(el) + 0x68, &e.obj[2], 4);
    } else if (el->type == 1) {
        for (int k = 0; k < el->passengers; ++k) el->passenger[k].unknown10 = int32_t(e.obj[2]);
    }
}

// [driving] 0x001587f8 ESetRecSpeedOffset: o +0x134 do veículo do jogador (sem elemento) ou do elemento acordado.
void ESetRecSpeedOffset_Execute(const PendingEvent& e) {
    AIGroundVehicle* v = nullptr;
    if (e.obj[1] == 0) {
        BondCar* p = player_car();
        v = p ? ai_of(*p) : nullptr;
    } else {
        AIElement* el = redirect(e.obj[1]);
        if (!el || el->state != 1 || el->type != 1) return;
        v = nf::p32_ptr<AIGroundVehicle>(el->ai);
    }
    if (!v) {
        ai_element_stats().guards++;
        return;
    }
    std::memcpy(&v->rec_speed_offset, &e.obj[2], 4);
}

}  // namespace

// [driving] 0x00145fc8 EAICommand::EAICommand
void EAICommand_Create(uint32_t command, uint32_t trigger_element, uint32_t element, uint32_t vehicle, uint32_t trigger,
                       uint32_t simple, uint32_t slot) {
    PendingEvent e{kEvAICommand, 0, 0.0f, 0, 0, 0};
    if (const EventInfo* info = event_info(kEvAICommand)) {
        e.size = info->size;
        e.obj[0] = info->vtable;
    }
    e.obj[1] = command;
    e.obj[2] = trigger_element;
    e.obj[3] = element;
    e.obj[4] = vehicle;
    e.obj[5] = trigger;
    e.obj[6] = simple;
    e.obj[7] = slot;
    e.written = 0xff;
    ++event_stats().created[kEvAICommand];
    trigger_manager().pending.push_back(e);
}

bool ai_event_execute(const PendingEvent& e) {
    if (!ai_controller()) return false;
    switch (e.type) {
        case kEvAICommand: EAICommand_Execute(e); return true;
        case kEvActivate: EActivateAIElement_Execute(e); return true;
        case kEvDeactivate: EDeactivateAIElement_Execute(e); return true;
        case kEvKill: EKillAIElement_Execute(e); return true;
        case kEvFireOff: EAIElementFire_Execute(e, false); return true;
        case kEvFireOn: EAIElementFire_Execute(e, true); return true;
        case kEvSetAccuracy: EAIElementSetAccuracy_Execute(e); return true;
        case kEvSetRecSpeedOffset: ESetRecSpeedOffset_Execute(e); return true;
        default: return false;
    }
}

}  // namespace drv
