// Auditoria da campanha a pé (ver campaign_audit.h).

#include "campaign_audit.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/anim.h"
#include "engine/break.h"
#include "engine/collide.h"
#include "engine/rigid_body.h"
#include "engine/game.h"
#include "engine/hashtable.h"
#include "engine/gameflow.h"
#include "engine/input.h"
#include "engine/menu.h"
#include "engine/player_traverse.h"
#include "engine/ssys.h"
#include "engine/turret.h"
#include "engine/copter.h"
#include "engine/gunimp.h"
#include "engine/hazard.h"
#include "engine/hud.h"
#include "engine/lights.h"
#include "engine/searchlight.h"
#include "engine/sensor.h"
#include "engine/emitter.h"
#include "engine/space_station.h"
#include "engine/explode.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/gamestate.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/script.h"
#include "engine/trigger.h"
#include "engine/view.h"
#include "platform/pending.h"

namespace campaign {
namespace {

using Counts = std::map<std::string, int>;

Counts g_before;
std::string g_text;  // o que after_load montou (os objetos vivos logo depois da carga)
std::set<std::string> g_blocks;
Counts g_load;  // chamadas na carga
Counts g_game;  // chamadas nos 2 s de jogo
std::map<std::string, std::set<uint32_t>> g_rank;  // bloqueio -> fases

// Inventário inicial (depois de Player_Init): as classes de aparelho que o jogador tem.
std::vector<std::string> g_gadgets;
std::set<std::string> g_gadget_keys;

Counts diff(const Counts& after, const Counts& before) {
    Counts d;
    for (const auto& [name, n] : after) {
        const auto it = before.find(name);
        const int b = it == before.end() ? 0 : it->second;
        if (n != b) d[name] = n - b;
    }
    return d;
}

const MISSIONDATA_tag* mission_of(uint32_t level) {
    for (const MISSIONDATA_tag& m : MissionData) {
        if (m.level == level) return &m;
    }
    return nullptr;
}

// O que um posicionamento provisório pede, pelo *_Create (e pelos parâmetros, no fio).
std::string need_of(const PendingPlacement& p) {
    const std::string c = p.create;
    if (c == "Ladder_Create") return "escada";
    if (c == "Wire_Create") {
        // Wire_Create: o tipo do fio é o parâmetro 1 (0 corda, 1 desligado, 2 tirolesa).
        switch (p.params[1]) {
            case 0: return "corda";
            case 2: return "tirolesa";
            default: return "fio" + std::to_string(p.params[1]);
        }
    }
    if (c == "Grapple_Create") return "gancho";
    if (c == "CreepWall_Create") return "esgueirar";
    if (c == "ThirdIcon_Create") return "icones";
    if (c == "GT_Create") return "torreta";
    if (c == "Lock_Create") return p.params[0] == 1 ? "cadeado_laser" : "cadeado";
    if (c == "Sensor_Create") return "sensor";
    if (c == "Monitor_Create") return "monitor";
    if (c == "PCQWorm_Create") return "qworm";
    if (c == "SS_Create") return "ss";
    if (c == "CamSubject_Create") return "foto";
    if (c == "Copter_Create") return "helicoptero";
    if (c == "Searchlight_Create") return "holofote";
    if (c == "Mine_Create") return "mina";
    if (c == "GunImp_Create") return "gunimp";
    if (c == "InverterTrigger_Create") return "inversor";
    if (c == "Shooter_Create") return "atirador";
    if (c == "Apocalypse_Create") return "apocalypse";
    if (c == "Create_SpaceLaser") return "laser_espacial";
    if (c == "Create_SpaceMissile") return "missil_espacial";
    if (c == "Car_Create") return "carro";
    if (c == "Sub_Create" || c == "MiniSub_Create") return "submarino";
    if (c == "Creature_Create") return "criatura";
    if (c == "RB_Create") return "corpo_rigido";
    return "";  // cosméticos (marolas, chuva, raios, folhas, árvores, nuvens, rotores, câmeras de terceira pessoa)
}

// Parâmetros de canal de saída (que um objeto liga) de cada *_Create, pela decompilação (os já portados ficam para os
// posicionamentos que ainda caiam em pending_create e para a tabela "antes" de docs/engine.md):
// Lock_Update (5), Sensor_Update (3; 8 é o canal da câmera vista no monitor), SS_Update (3), PCQWorm_Update (1),
// CamSubject_Update (0), Copter_Update (7, 8), Searchlight_Update (3, um byte), Create_SpaceLaser (5), Wire (3: a
// tirolesa o liga enquanto anda).
std::vector<int> out_params(const std::string& c) {
    if (c == "Lock_Create") return {5};
    if (c == "Sensor_Create") return {3, 8};
    if (c == "SS_Create") return {3};
    if (c == "PCQWorm_Create") return {1};
    if (c == "CamSubject_Create") return {0};
    if (c == "Copter_Create") return {7, 8};
    if (c == "Searchlight_Create") return {3};
    if (c == "Create_SpaceLaser") return {5};
    if (c == "Wire_Create") return {3};
    return {};
}

bool has_weapon(const BLData* b, int lo, int hi) {
    for (int w = lo; w <= hi && w < kWeaponSlots; ++w) {
        if (b->weapons[w].have != 0) return true;
    }
    return false;
}

// Quem liga o canal entre os objetos portados vivos (e as portas).
std::set<std::string> ported_setters(uint32_t ch) {
    std::set<std::string> s;
    if (ch == 0) return s;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->data == nullptr) continue;
        switch (o->type) {
            case 0x1d: {
                const auto* t = static_cast<const TRIGGER_Tag*>(o->data);
                if (t->mode != 10 && t->mode != 0xd && t->channel == ch) s.insert("gatilho");
                // Troca de fase (modo 10): inputs[0] trava, inputs[1] condição, os outros são canais que liga.
                if (t->mode == 10) {
                    for (int k = 2; k < 8; ++k) {
                        if (t->inputs[k] == ch) s.insert("fim_de_fase");
                    }
                }
                break;
            }
            case 0x39:
                if (static_cast<const SWITCH_tag*>(o->data)->channel == ch) s.insert("interruptor");
                break;
            case 0x3c: {
                const auto* f = static_cast<const FUSEBOX_tag*>(o->data);
                if (f->channel_a[0] == ch || f->channel_a[1] == ch || f->channel_b[0] == ch || f->channel_b[1] == ch) {
                    s.insert("fusiveis");
                }
                break;
            }
            case 0x1b:
                if (static_cast<const SCRIPTPLAYER_tag*>(o->data)->channel == ch) s.insert("cena");
                break;
            case 0x21:
                if (static_cast<const DESTROY_tag*>(o->data)->channel_out == ch) s.insert("destrutivel");
                break;
            case 0x2f:
                if (static_cast<const PICKUPINFO*>(o->data)->channel == ch) s.insert("item");
                break;
            // Os objetos portados nesta auditoria: cadeados (0x3a), Q-Worm (0x49), alvos da câmera (0x48, canal em
            // unknown_f6), tirolesas e paredes de esgueirar (0x2c, 0x2b) e helicópteros (0x3b: fuga e atraso).
            case 0x3a:
                if (static_cast<const LOCK_tag*>(o->data)->channel == ch) s.insert("cadeado");
                break;
            case 0x49:
                if (uint16_t(static_cast<const PCQWORM_tag*>(o->data)->channel) == ch) s.insert("qworm");
                break;
            case 0x48:
                if (o->unknown_f6 == ch) s.insert("foto");
                break;
            case 0x2b:
            case 0x2c:
                if (static_cast<const WIRE_tag*>(o->data)->channel == ch) s.insert(o->type == 0x2c ? "fio" : "esgueirar");
                break;
            case 0x25: {
                const auto* r = static_cast<const SIMPLESCRIPT_tag*>(o->data);
                if ((r->flags & 1) == 0 && r->channel == ch) s.insert("roteiro");
                break;
            }
            case 0x3b: {
                const auto* c = static_cast<const COPTER*>(o->data);
                if (c->done == ch || (c->channel == ch && c->channel != 100)) s.insert("helicoptero");
                break;
            }
            case 0x30: {
                const auto* sp = static_cast<const DroneSpawner_tag*>(o->data);
                if (sp->channel_done == ch) s.insert("gerador");
                // Os modelos do gerador: o canal da morte (parâmetro 12) dos personagens que ele vai criar.
                for (uint16_t e = 0; sp->entries != nullptr && e < sp->entry_count; ++e) {
                    if ((sp->entries[e].vars.params[12] & 0xff) == ch) s.insert("morte_gerado");
                }
                break;
            }
            default:
                break;
        }
    }
    for (const LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
        if (reinterpret_cast<const DOORINFO*>(n)->channel == ch) s.insert("porta");
    }
    for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        if (reinterpret_cast<const Drone_tag*>(n)->param12 == ch) s.insert("morte");
    }
    return s;
}

// Quem espera o canal (portas trancadas ou acionadas por ele, gatilhos, script players, interruptores, personagens,
// geradores, destrutíveis): um canal que só um objeto provisório liga e que alguém espera bloqueia a fase.
std::set<std::string> consumers(uint32_t ch) {
    std::set<std::string> s;
    if (ch == 0) return s;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->data == nullptr) continue;
        switch (o->type) {
            case 0x1d: {
                const auto* t = static_cast<const TRIGGER_Tag*>(o->data);
                for (int k = 0; k < 8; ++k) {
                    if (t->inputs[k] == ch) s.insert("gatilho");
                }
                if (t->lock == ch) s.insert("gatilho");
                break;
            }
            case 0x1b: {
                const auto* sp = static_cast<const SCRIPTPLAYER_tag*>(o->data);
                if (sp->channel == ch || sp->enable == ch) s.insert("cena");
                break;
            }
            case 0x39:
                if (static_cast<const SWITCH_tag*>(o->data)->lock == ch) s.insert("interruptor");
                break;
            case 0x21:
                if (static_cast<const DESTROY_tag*>(o->data)->channel_in == ch) s.insert("destrutivel");
                break;
            case 0x30:
                if (static_cast<const DroneSpawner_tag*>(o->data)->channel_start == ch) s.insert("gerador");
                break;
            // As torretas automáticas que ligam com o canal (0700000a/0b: o alarme das câmeras), os sons de gatilho (a
            // sirene) e os emissores (as luzes do alarme).
            case 0x36:
                if (static_cast<const GUNTURRET*>(o->data)->enable == ch) s.insert("torreta");
                break;
            case 0x40:
                // SOUNDTRIGGER_tag (privado de sound.cpp): o canal que liga é o primeiro u16.
                if (*static_cast<const uint16_t*>(o->data) == ch) s.insert("som");
                break;
            case 0x32:
                if (static_cast<const EMITTER_tag*>(o->data)->switch_on == ch) s.insert("emissor");
                break;
            default:
                break;
        }
    }
    for (const LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
        const auto* d = reinterpret_cast<const DOORINFO*>(n);
        if (d->channel == ch || d->lock_channel == ch) s.insert("porta");
    }
    for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        const auto* d = reinterpret_cast<const Drone_tag*>(n);
        if (d->channel_a == ch || d->channel_b == ch) s.insert("personagem");
    }
    if (const MISSIONDATA_tag* m = mission_of(GameState.level_file); m != nullptr && m->objectives != nullptr) {
        for (uint32_t i = 0; i < m->count; ++i) {
            // Um "não deixe acontecer" (flag 2) não espera o canal: fracassa com ele.
            if ((m->objectives[i].flags & 2) != 0) continue;
            if (m->objectives[i].channel == ch || m->objectives[i].trigger == ch) s.insert("objetivo");
        }
    }
    return s;
}

std::set<std::string> pending_setters(uint32_t ch) {
    std::set<std::string> s;
    if (ch == 0) return s;
    for (const PendingPlacement& p : g_pending_placements) {
        for (int k : out_params(p.create)) {
            if (p.params[k] == ch) s.insert(p.create);
        }
    }
    return s;
}

}  // namespace

namespace {

void add(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_text += buf;
}

void inventory(const BLData* b) {
    struct Gadget {
        int lo, hi;
        const char* name;
    };
    // Classes de aparelho de weapon_data (Player_InitWeapon, Player_WeaponFiring): 0x43 dardos, 0x4a-0x4c stunner,
    // 0x4e-0x4f laser, 0x50-0x51 gancho, 0x54-0x55 câmera, 0x56-0x57 decodificador, 0x58 Q-Worm, 0x59-0x5b Q-Spex.
    static const Gadget kGadgets[] = {{0x43, 0x45, "dardos"},  {0x4a, 0x4c, "stunner"}, {0x4e, 0x4f, "laser"},
                                      {0x50, 0x51, "gancho"},  {0x54, 0x55, "camera"},  {0x56, 0x57, "decodificador"},
                                      {0x58, 0x58, "qworm"},   {0x59, 0x5b, "qspex"}};
    for (const Gadget& g : kGadgets) {
        if (!has_weapon(b, g.lo, g.hi)) continue;
        g_gadgets.push_back(g.name);
        g_gadget_keys.insert(g.name);
    }
}

// Monta a parte da linha que depende dos objetos logo depois da carga.
void describe(uint32_t level) {
    g_text.clear();
    g_blocks.clear();
    // Posicionamentos provisórios por função e o que pedem.
    Counts needs;
    for (const PendingPlacement& p : g_pending_placements) {
        const std::string n = need_of(p);
        if (!n.empty()) ++needs[n];
    }
    int water = 0;
    for (const cel_tag* c = glb_world != nullptr ? glb_world->first : nullptr; c != nullptr; c = c->next) {
        const uint16_t t = uint16_t(c->type);
        if ((t == 0xc023 || t == 0xc047) && -400.0f < c->water_y) ++water;
    }
    add(" campanha: provisorios=%zu", g_pending_placements.size());
    // Subestados do jogador que a fase pede.
    add(" subestados=");
    static const char* const kTraversal[] = {"escada", "corda", "tirolesa", "gancho", "esgueirar"};
    for (const char* t : kTraversal) {
        const auto it = needs.find(t);
        if (it == needs.end()) continue;
        add("%s:%d,", t, it->second);
        // O gancho só bloqueia se a fase dá o aparelho.
        if (std::string(t) != "gancho" || g_gadget_keys.count("gancho") != 0) g_blocks.insert(t);
    }
    if (water != 0) add("agua:%d,", water);
    if (needs.count("icones") != 0) add("icones:%d,", needs["icones"]);
    // Aparelhos e os alvos deles ainda provisórios (!).
    add(" aparelhos=");
    for (const std::string& g : g_gadgets) {
        std::string target;
        if (g == "gancho" && (needs.count("gancho") != 0 || needs.count("icones") != 0)) target = "gancho";
        if (g == "camera" && needs.count("foto") != 0) target = "foto";
        if (g == "decodificador" && needs.count("cadeado") != 0) target = "cadeado";
        if (g == "laser" && needs.count("cadeado_laser") != 0) target = "cadeado_laser";
        if (g == "qworm" && needs.count("qworm") != 0) target = "qworm";
        add("%s%s%s%s,", g.c_str(), target.empty() ? "" : "->", target.c_str(), target.empty() ? "" : "!");
        if (!target.empty()) g_blocks.insert(target);
    }
    // Objetivos: canal ("nao" = não deixe acontecer) e quem o liga (! = provisório, ? = nenhum objeto: um evento de
    // script de cena ou a lógica da fase).
    add(" objetivos=");
    if (const MISSIONDATA_tag* m = mission_of(level); m != nullptr && m->objectives != nullptr) {
        for (uint32_t i = 0; i < m->count; ++i) {
            const OBJECTIVE_tag& o = m->objectives[i];
            const std::set<std::string> ported = ported_setters(o.channel);
            const std::set<std::string> pend = pending_setters(o.channel);
            add("%u%s:", o.channel, (o.flags & 2) != 0 ? "(nao)" : "");
            bool first = true;
            for (const std::string& s : ported) {
                add("%s%s", first ? "" : "+", s.c_str());
                first = false;
            }
            for (const std::string& s : pend) {
                add("%s%s!", first ? "" : "+", s.c_str());
                first = false;
            }
            if (first) {
                // Nenhum objeto conhecido liga o canal: os posicionamentos (tipo@parâmetro) com o valor do canal.
                add("?");
                std::set<std::string> seen;
                for (const PendingPlacement& p : g_all_placements) {
                    for (int k = 0; k < 12; ++k) {
                        if (p.params[k] != o.channel) continue;
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%x@%d", p.type, k);
                        seen.insert(buf);
                    }
                }
                if (!seen.empty()) {
                    add("[");
                    bool f = true;
                    for (const std::string& t : seen) {
                        add("%s%s", f ? "" : " ", t.c_str());
                        f = false;
                    }
                    add("]");
                }
            }
            add(",");
            // Um objetivo comum que só objetos provisórios ligam bloqueia a fase.
            if ((o.flags & 2) == 0 && ported.empty() && !pend.empty()) {
                for (const std::string& s : pend) {
                    PendingPlacement probe{};
                    probe.create = s.c_str();
                    g_blocks.insert("objetivo:" + need_of(probe));
                }
            }
        }
    }
    // Objetos de jogo provisórios que não são travessia nem alvo de aparelho.
    static const char* const kObjects[] = {"torreta",  "sensor",      "monitor",         "ss",           "holofote",
                                           "helicoptero", "mina",     "gunimp",          "inversor",     "atirador",
                                           "apocalypse",  "laser_espacial", "missil_espacial", "corpo_rigido", "carro",
                                           "submarino",   "criatura"};
    add(" objetos=");
    for (const char* h : kObjects) {
        const auto it = needs.find(h);
        if (it == needs.end()) continue;
        add("%s:%d,", h, it->second);
    }
    if (needs.count("torreta") != 0) g_blocks.insert("torreta");
    // Canais de saída de objetos provisórios que alguém espera (e que nenhum objeto portado liga).
    add(" canais_provisorios=");
    std::set<std::pair<uint32_t, std::string>> done;
    for (const PendingPlacement& p : g_pending_placements) {
        const std::string c = p.create;
        for (int k : out_params(p.create)) {
            // Os alarmes (o sensor que vê o jogador, o holofote) só atrapalham: sem eles a fase fica mais fácil.
            if ((c == "Sensor_Create" && k == 3) || c == "Searchlight_Create") continue;
            const uint32_t ch = p.params[k];
            if (ch == 0 || ch > 255 || !done.insert({ch, p.create}).second) continue;
            const std::set<std::string> who = consumers(ch);
            if (who.empty() || !ported_setters(ch).empty()) continue;
            add("%u:%s->", ch, need_of(p).c_str());
            bool first = true;
            for (const std::string& w : who) {
                add("%s%s", first ? "" : "+", w.c_str());
                first = false;
            }
            add(",");
            g_blocks.insert("canal:" + need_of(p));
        }
    }
}

bool player_stub(const std::string& n) {
    static const char* const kPrefixes[] = {"Player_",   "GT_",        "Lock_",  "camera_", "Camera_",
                                            "ThirdIcon", "Monitor_",   "Sensor_", "SS_",    "PCQWorm",
                                            "CamSubject", "Ladder",    "Wire",   "Grapple", "CreepWall"};
    for (const char* p : kPrefixes) {
        if (n.rfind(p, 0) == 0) return true;
    }
    return n.find("_Activate") != std::string::npos;
}

}  // namespace

std::string traverse_probe();

bool is_campaign_level(uint32_t level) {
    return (level >> 24) == 7 && mission_of(level) != nullptr;
}

void begin_level() {
    g_record_placements = true;
    g_before = pending_counts();
    g_load.clear();
    g_game.clear();
    g_gadgets.clear();
    g_gadget_keys.clear();
    g_text.clear();
    g_blocks.clear();
}

void after_load(uint32_t level) {
    // NF_CANAL=<n> (0x.. aceito): os posicionamentos com o valor n em algum parâmetro (quem liga ou espera um canal,
    // pelo tipo do posicionamento), para seguir uma cadeia de canais como a do alarme das câmeras.
    if (const char* want = std::getenv("NF_CANAL"); want != nullptr) {
        const uint32_t ch = uint32_t(std::strtoul(want, nullptr, 0));
        for (const PendingPlacement& p : g_all_placements) {
            bool has = false;
            for (int k = 0; k < 12; ++k) has = has || p.params[k] == ch;
            if (!has) continue;
            std::printf("canal %08x tipo=%02x pos=%.2f,%.2f,%.2f parametros=", level, p.type, p.pos.x, p.pos.y, p.pos.z);
            for (int k = 0; k < 12; ++k) std::printf("%x,", p.params[k]);
            std::printf("\n");
        }
    }
    const Counts now = pending_counts();
    g_load = diff(now, g_before);
    g_before = now;
    if (obj_tag* p = glb_players[0]; p != nullptr) inventory(bloke_of(p));
    describe(level);
}

void before_game() { g_before = pending_counts(); }

void after_game() {
    const Counts now = pending_counts();
    g_game = diff(now, g_before);
    g_before = now;
}

void print_line(uint32_t level, bool detail) {
    std::printf("%s", g_text.c_str());
    std::printf("%s", traverse_probe().c_str());
    // Stubs do jogador e dos objetos de jogo chamados na carga e nos 2 s.
    std::printf(" stubs_jogador=");
    Counts all = g_load;
    for (const auto& [n, k] : g_game) all[n] += k;
    for (const auto& [n, k] : all) {
        if (player_stub(n)) std::printf("%s:%d,", n.c_str(), k);
    }
    std::printf(" bloqueios=");
    for (const std::string& b : g_blocks) {
        std::printf("%s,", b.c_str());
        g_rank[b].insert(level);
    }
    if (g_blocks.empty()) std::printf("nenhum");
    if (detail) {
        Counts creates;
        for (const PendingPlacement& p : g_pending_placements) ++creates[p.create];
        std::printf(" provisorios_por_funcao=");
        for (const auto& [n, k] : creates) std::printf("%s:%d,", n.c_str(), k);
        std::printf(" stubs_carga=");
        for (const auto& [n, k] : g_load) std::printf("%s:%d,", n.c_str(), k);
        std::printf(" stubs_jogo=");
        for (const auto& [n, k] : g_game) std::printf("%s:%d,", n.c_str(), k);
    }
}

void print_summary() {
    std::vector<std::pair<std::string, std::set<uint32_t>>> rank(g_rank.begin(), g_rank.end());
    std::stable_sort(rank.begin(), rank.end(),
                     [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    std::printf("bloqueios da campanha (fases):");
    if (rank.empty()) std::printf(" nenhum");
    for (const auto& [name, levels] : rank) {
        std::printf(" %s=%zu[", name.c_str(), levels.size());
        bool first = true;
        for (uint32_t l : levels) {
            std::printf("%s%08x", first ? "" : ",", l);
            first = false;
        }
        std::printf("]");
    }
    std::printf("\n");
}

// ---- a prova da travessia (no fim de cada fase da campanha) ----
//
// Com o jogo da fase (Game_Run + Game_Draw por tick) e a entrada do teclado (g_psi_keyboard_mouse, as teclas padrão:
// W anda, A/D de lado, espaço pula, E usa, mouse 1 atira), o jogador invencível tenta: subir a primeira escada (de pé
// 1,5 m à frente dela, de frente, segurando W); descer a primeira tirolesa (debaixo da ponta de cima, pulando); andar na
// primeira corda (pulando debaixo dela e segurando D); atirar o gancho num ponto de gancho a partir do ícone do tipo 2
// mais perto; e encostar na primeira parede de esgueirar pelo ícone do tipo 6 (E) e andar nela (D). Cada um anota onde
// o jogador começou (para nightfire.exe --at) e o que mudou. O sorteio, GameState, os canais e as contagens de stubs
// voltam como estavam.
namespace {

constexpr int kVkSpace = 0x20;    // VK_SPACE
constexpr int kVkLButton = 0x01;  // VK_LBUTTON
constexpr int kVkRButton = 0x02;  // VK_RBUTTON

void key(int vk, bool down) { g_psi_keyboard_mouse.down[vk] = down; }

void release_keys() {
    std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
}

void game_tick() {
    ++GameState.frame;
    ++VBlankCount;
    Game_Run();
    Game_Draw();
}

// Põe o jogador de pé em "at" virado para "yaw" e deixa a gravidade assentar.
void place(obj_tag* p, const _VECTOR& at, float yaw) {
    _VECTOR pos{at.x, at.y, at.z, 1.0f};
    _VECTOR rot{0.0f, yaw, 0.0f, 0.0f};
    Player_StandAtNewPosition(p, &pos, &rot, kPlrWalk);
    BLData* b = bloke_of(p);
    b->pitch = 0.0f;
    b->traverse_wait = 0;
    release_keys();
    for (int t = 0; t < 20; ++t) game_tick();
}

obj_tag* first_of(uint8_t type, bool (*ok)(const obj_tag*) = nullptr) {
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == type && (ok == nullptr || ok(o))) return o;
    }
    return nullptr;
}

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// Uma escada: o jogador 1,5 m à frente, de frente (giro da escada + 180 graus), na altura da base, segurando W até
// sair por cima (até 15 s). *start recebe onde ele ficou de pé; devolve se saiu por cima, com *climbed o quanto
// subiu preso.
bool climb(obj_tag* p, obj_tag* ladder, _VECTOR* start, float* climbed, bool* held) {
    const float yaw = ladder->rot.y + 3.1415927f;
    const float fx = PS2Sinf(yaw), fz = PS2Sinf(yaw + 1.5707964f);
    const _VECTOR at{ladder->sphere.x - fx * 1.5f, ladder->sphere.y - ladder->sphere.w + 1.2f,
                     ladder->sphere.z - fz * 1.5f, 1.0f};
    place(p, at, yaw);
    *start = p->pos;
    float y0 = 0.0f, top = -1e9f;
    bool left = false;
    *held = false;
    int after = 0;
    key('W', true);
    for (int t = 0; t < 900 && after < 60; ++t) {
        game_tick();
        if (p->unknown_f6 == kPlrClimb) {
            if (!*held) y0 = p->pos.y;
            *held = true;
            top = std::max(top, p->pos.y);
        } else if (*held) {
            left = true;
        }
        if (left) ++after;
    }
    release_keys();
    for (int t = 0; t < 30; ++t) game_tick();
    *climbed = *held ? top - y0 : 0.0f;
    // Saiu por cima: soltou a escada e ficou acima de onde a pegou.
    return left && *held && start->y + 1.0f < p->pos.y;
}

// Escadas: tenta todas as da fase que estão numa sala; anota quantas o jogador subiu até sair por cima e a primeira.
std::string probe_ladder(obj_tag* p) {
    int total = 0, held_n = 0, up = 0, outside = 0;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x2d) continue;
        ++total;
        // Fora de toda sala (build_LinkToRoom não achou a caixa de nenhuma): nenhum teste de colisão a alcança, no
        // original também; em 0700000a e 07000015 são escadas onde a fase não tem geometria (abaixo do piso, num poço
        // sem sala).
        if (o->cel == nullptr) {
            ++outside;
            continue;
        }
        _VECTOR start{};
        float climbed = 0.0f;
        bool held = false;
        const bool ok = climb(p, o, &start, &climbed, &held);
        held_n += held;
        up += ok;
        if (ok && first.empty()) {
            first = fmt("%.2f,%.2f,%.2f,%.3f(subiu=%.2f fim_y=%.2f)", start.x, start.y, start.z,
                        o->rot.y + 3.1415927f, climbed, p->pos.y - start.y);
        }
    }
    if (total == 0) return "";
    return fmt(" escadas=%d/%d/%d", up, held_n, total) + (outside != 0 ? fmt(" escadas_fora_de_sala=%d", outside) : "") +
           (first.empty() ? "" : " escada=" + first);
}

bool is_zipline(const obj_tag* o) { return static_cast<const WIRE_tag*>(o->data)->kind == 2; }
bool is_rope(const obj_tag* o) { return static_cast<const WIRE_tag*>(o->data)->kind == 0; }

void wire_ends(const obj_tag* w, float shrink, _VECTOR* e1, _VECTOR* e2) {
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, w->sphere.w - shrink, w->rot.y + 1.5707964f, w->rot.z);
    *e1 = {w->sphere.x + off.x, w->sphere.y + off.y, w->sphere.z + off.z, 1.0f};
    *e2 = {w->sphere.x - off.x, w->sphere.y - off.y, w->sphere.z - off.z, 1.0f};
}

// O ícone de toque do tipo "kind" mais perto de "at" (nulo se não houver).
obj_tag* nearest_icon(uint32_t kind, const _VECTOR& at) {
    obj_tag* best = nullptr;
    float d = 1e9f;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x42 || static_cast<const THIRDICON_tag*>(o->data)->kind != kind) continue;
        const float k = Vec_Dist3D(&o->sphere, &at);
        if (k < d) {
            d = k;
            best = o;
        }
    }
    return best;
}

// Pegar um fio pulando: no ícone do tipo 3 mais perto do ponto "near" (a ponta de cima da tirolesa, o meio da
// corda), ou, sem ícone, debaixo do ponto; espaço por 3 ticks. Devolve o tick em que o subestado virou "sub" (-1
// nenhum).
// O canal que liga o ícone de toque (parâmetro 1): a fase o liga pela história; a prova liga antes de usar o ícone.
void enable_icon(obj_tag* ic) {
    const auto* d = static_cast<const THIRDICON_tag*>(ic->data);
    if (d->enable != 0 && d->enable < 256) switch_channels[d->enable] = 1;
}

int jump_to_wire(obj_tag* p, const _VECTOR& near, float yaw, uint16_t sub, _VECTOR* start, bool* icon) {
    obj_tag* ic = nearest_icon(3, near);
    *icon = ic != nullptr && Vec_Dist3D(&ic->sphere, &near) < 8.0f;
    if (*icon) enable_icon(ic);
    const _VECTOR at = *icon ? _VECTOR{ic->sphere.x, ic->sphere.y + 0.5f, ic->sphere.z, 1.0f}
                             : _VECTOR{near.x, near.y - 1.1f, near.z, 1.0f};
    place(p, at, yaw);
    *start = p->pos;
    if (p->unknown_f6 == sub) return 0;
    key(kVkSpace, true);
    for (int t = 0; t < 120; ++t) {
        game_tick();
        if (t == 3) key(kVkSpace, false);
        if (p->unknown_f6 == sub) return t + 1;
    }
    return -1;
}

// Tirolesa: do ícone perto da ponta de cima, pulando; depois desce sozinho até soltar (até 20 s).
std::string probe_zipline(obj_tag* p) {
    obj_tag* wire = first_of(0x2c, &is_zipline);
    if (wire == nullptr) return "";
    _VECTOR e1, e2;
    wire_ends(wire, 2.0f, &e1, &e2);
    const _VECTOR hi = e1.y < e2.y ? e2 : e1;
    const _VECTOR lo = e1.y < e2.y ? e1 : e2;
    const float yaw = std::atan2(lo.x - hi.x, lo.z - hi.z);
    _VECTOR start{};
    bool icon = false;
    const uint32_t before = g_traverse_stats.wires;
    const int grabbed = jump_to_wire(p, hi, yaw, kPlrZipline, &start, &icon);
    bool done = false;
    _VECTOR grab = p->pos;
    float lowest = p->pos.y;
    if (grabbed >= 0) {
        for (int t = 0; t < 1200; ++t) {
            game_tick();
            lowest = std::min(lowest, p->pos.y);
            if (p->unknown_f6 != kPlrZipline) {
                done = true;
                break;
            }
        }
    }
    release_keys();
    for (int t = 0; t < 30; ++t) game_tick();
    return fmt(" tirolesa=%.2f,%.2f,%.2f,%.3f(icone=%d agarrou=%u desceu=%.2f andou=%.2f soltou=%d canal=%u)", start.x,
               start.y, start.z, yaw, icon ? 1 : 0, g_traverse_stats.wires - before, grabbed >= 0 ? grab.y - lowest : 0.0f,
               grabbed >= 0 ? Vec_Dist3D(&grab, &p->pos) : 0.0f, done ? 1 : 0,
               static_cast<const WIRE_tag*>(wire->data)->channel);
}

// Corda: do ícone perto do meio dela, pulando; espera subir na corda (2 s) e anda de lado (D, 4 s).
std::string probe_rope(obj_tag* p) {
    obj_tag* wire = first_of(0x2c, &is_rope);
    if (wire == nullptr) return "";
    _VECTOR start{};
    bool icon = false;
    const uint32_t before = g_traverse_stats.wires;
    const int grabbed = jump_to_wire(p, wire->sphere, wire->rot.y, kPlrWire, &start, &icon);
    float moved = 0.0f;
    if (grabbed >= 0) {
        for (int k = 0; k < 120; ++k) game_tick();  // a animação de subir na corda
        const _VECTOR from = p->pos;
        key('D', true);
        for (int k = 0; k < 240 && p->unknown_f6 == kPlrWire; ++k) game_tick();
        moved = Vec_Dist3D(&from, &p->pos);
    }
    release_keys();
    for (int k = 0; k < 30; ++k) game_tick();
    return fmt(" corda=%.2f,%.2f,%.2f,%.3f(icone=%d agarrou=%u andou=%.2f)", start.x, start.y, start.z, wire->rot.y,
               icon ? 1 : 0, g_traverse_stats.wires - before, moved);
}

bool icon_kind(const obj_tag* o, uint32_t kind) { return static_cast<const THIRDICON_tag*>(o->data)->kind == kind; }
bool icon_grapple(const obj_tag* o) { return icon_kind(o, 2); }
bool icon_creep(const obj_tag* o) { return icon_kind(o, 6); }

obj_tag* nearest(uint8_t type, const _VECTOR& at) {
    obj_tag* best = nullptr;
    float d = 1e9f;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != type) continue;
        const float k = Vec_Dist3D(&o->sphere, &at);
        if (k < d) {
            d = k;
            best = o;
        }
    }
    return best;
}

// O giro e a elevação (BLData::pitch em unidades de 90 graus; o sinal pela própria mira) que levam a mira a "at".
float aim_at(obj_tag* p, const _VECTOR& at) {
    BLData* b = bloke_of(p);
    _VECTOR head{};
    Player_GetHeadPos(&head, p, nullptr);
    p->rot.y = std::atan2(at.x - head.x, at.z - head.z);
    Control_GetObjMatrix(p, &p->matrix);
    const float dx = at.x - head.x, dz = at.z - head.z;
    const float want = std::atan2(at.y - head.y, std::sqrt(dx * dx + dz * dz)) / 1.5707964f;
    float best_err = 1e9f, best_pitch = 0.0f;
    for (float s : {1.0f, -1.0f}) {
        b->pitch = std::clamp(s * want, -1.0f, 1.0f);
        _VECTOR aim{};
        Player_GetAimingPoint(p, &aim);
        _VECTOR d{aim.x - head.x, aim.y - head.y, aim.z - head.z, 0.0f};
        Vec_Normalise(&d, &d);
        _VECTOR e{at.x - head.x, at.y - head.y, at.z - head.z, 0.0f};
        Vec_Normalise(&e, &e);
        const float err = 1.0f - (d.x * e.x + d.y * e.y + d.z * e.z);
        if (err < best_err) {
            best_err = err;
            best_pitch = b->pitch;
        }
    }
    b->pitch = best_pitch;
    b->pitch_target = best_pitch;
    b->look_state = 1;
    return best_pitch;
}

// Põe a arma "weapon" na mão e espera ela ficar parada (sacar leva mais de um segundo). Devolve se conseguiu (a pele
// da arma precisa estar carregada na fase).
bool draw_weapon(obj_tag* p, int8_t weapon) {
    BLData* b = bloke_of(p);
    ANIMOBJ_tag* anim = anim_of(p);
    // Só as armas cuja pele a fase carregou (Player_CheckWeaponsLoaded tira as outras do inventário).
    const uint32_t skin = weapon_def(weapon)->skin;
    if (skin == 0 || hashtable_getitem(skin) == nullptr) return false;
    b->weapons[uint8_t(weapon)].have = 1;
    // Munição cheia (os testes de antes podem ter gastado a da fase).
    const weapon_definition_tag* d = weapon_def(weapon);
    if (d->ammo_type != 0) {
        b->ammo[d->ammo_type] = ammo_max(d->ammo_type);
        b->weapons[uint8_t(d->klass)].rounds = d->clip;
    }
    anim->next_weapon = weapon;
    for (int t = 0; t < 300 && !(anim->weapon == weapon && b->gun->state == 0); ++t) game_tick();
    return anim->weapon == weapon && b->gun->state == 0;
}

// Gancho: no ícone do tipo 2, com o gancho (arma 0x50) na mão, mirando o ponto de gancho mais perto, um tiro.
// Um gancho: no ícone (ligado), com o gancho na mão, mirando o ponto de gancho mais perto, um tiro. Devolve se puxou.
bool grapple_from(obj_tag* p, obj_tag* icon, obj_tag* point, std::string* out) {
    BLData* b = bloke_of(p);
    enable_icon(icon);
    const _VECTOR at{icon->sphere.x, icon->sphere.y + 0.5f, icon->sphere.z, 1.0f};
    const float yaw = std::atan2(point->sphere.x - at.x, point->sphere.z - at.z);
    place(p, at, yaw);
    const _VECTOR start = p->pos;
    draw_weapon(p, 0x50);
    const float best_pitch = aim_at(p, point->sphere);
    const float d0 = Vec_Dist3D(&p->pos, &point->sphere);
    const uint32_t before = g_traverse_stats.grapples;
    const uint8_t touch = b->touch_type;
    key(kVkLButton, true);
    bool pulled = false;
    float closest = d0;
    for (int t = 0; t < 600; ++t) {
        b->pitch = best_pitch;
        game_tick();
        if (t == 30) key(kVkLButton, false);
        if (p->unknown_f6 == kPlrGrapple) pulled = true;
        closest = std::min(closest, Vec_Dist3D(&p->pos, &point->sphere));
        if (pulled && t > 40 && b->unknown_15c == 2) {
            key(kVkLButton, false);
            break;
        }
    }
    release_keys();
    for (int t = 0; t < 30; ++t) game_tick();
    const uint32_t caught = g_traverse_stats.grapples - before;
    *out = fmt("%.2f,%.2f,%.2f,%.3f(toque=%d preso=%u dist=%.2f->%.2f)", start.x, start.y, start.z, yaw,
               touch == 0xff ? -1 : int(touch), caught, d0, closest);
    return caught != 0;
}

// Gancho: cada ícone do tipo 2 com um ponto de gancho (0x51) a até 30 m; anota quantos puxaram o jogador e o primeiro
// que puxou (ou o último tentado).
std::string probe_grapple(obj_tag* p) {
    int total = 0, ok = 0;
    std::string first, last;
    for (obj_tag* icon = DynamicObjList.next; icon != nullptr; icon = icon->next) {
        if (icon->type != 0x42 || !icon_grapple(icon)) continue;
        obj_tag* point = nearest(0x51, icon->sphere);
        if (point == nullptr || Vec_Dist3D(&point->sphere, &icon->sphere) > 30.0f) continue;
        ++total;
        std::string one;
        const bool pulled = grapple_from(p, icon, point, &one);
        ok += pulled;
        if (pulled && first.empty()) first = one;
        last = one;
    }
    if (total == 0) return first_of(0x42, &icon_grapple) != nullptr ? " gancho=sem_ponto" : "";
    return fmt(" ganchos=%d/%d gancho=", ok, total) + (first.empty() ? last : first);
}

// Parede de esgueirar: no ícone do tipo 6, E, depois D por 3 s.
std::string probe_creep(obj_tag* p) {
    obj_tag* icon = first_of(0x42, &icon_creep);
    if (icon == nullptr) return "";
    obj_tag* wall = nearest(0x2b, icon->sphere);
    if (wall == nullptr) return " parede=sem_parede";
    enable_icon(icon);
    const _VECTOR at{icon->sphere.x, icon->sphere.y + 0.5f, icon->sphere.z, 1.0f};
    const float yaw = wall->rot.y;
    place(p, at, yaw);
    const _VECTOR start = p->pos;
    const uint32_t before = g_traverse_stats.creeps;
    key('E', true);
    bool on = false;
    for (int t = 0; t < 60 && !on; ++t) {
        game_tick();
        if (t == 3) key('E', false);
        on = p->unknown_f6 == kPlrCreep;
    }
    float moved = 0.0f;
    if (on) {
        const _VECTOR from = p->pos;
        key('D', true);
        for (int t = 0; t < 180 && p->unknown_f6 == kPlrCreep; ++t) game_tick();
        moved = Vec_Dist3D(&from, &p->pos);
    }
    release_keys();
    for (int t = 0; t < 30; ++t) game_tick();
    return fmt(" parede=%.2f,%.2f,%.2f,%.3f(encostou=%u andou=%.2f)", start.x, start.y, start.z, yaw,
               g_traverse_stats.creeps - before, moved);
}

// De pé "dist" à frente de um objeto (pela linha 2 da matriz dele), na altura dele (a gravidade assenta), mirando nele.
void stand_before(obj_tag* p, obj_tag* o, float dist) {
    _VECTOR f;
    Mat_GetDir(&f, &o->matrix);
    const _VECTOR at{o->sphere.x + f.x * dist, o->sphere.y, o->sphere.z + f.z * dist, 1.0f};
    place(p, at, std::atan2(-f.x, -f.z));
    aim_at(p, o->sphere);
}

// Segura o tiro mirando o objeto até "done" ou "ticks" acabarem; devolve os ticks usados (-1 não terminou).
int hold_fire(obj_tag* p, obj_tag* o, int ticks, bool (*done)(const obj_tag*)) {
    BLData* b = bloke_of(p);
    const float pitch = b->pitch;
    key(kVkLButton, true);
    int t = 0;
    for (; t < ticks && !done(o); ++t) {
        b->pitch = pitch;
        game_tick();
    }
    release_keys();
    for (int k = 0; k < 10; ++k) game_tick();
    return done(o) ? t : -1;
}

bool lock_open(const obj_tag* o) { return o->state != 0 && o->state != 5; }
bool worm_done(const obj_tag* o) { return o->state != 0; }

// Cadeados: o laser (arma 0x4e) nos do tipo 1, o decodificador (0x56) nos dos tipos 0 e 2, a 2 m, mirando; e, sem o
// decodificador, usar o de tipo 0 abre o teclado. Anota quantos de cada abriram.
std::string probe_locks(obj_tag* p) {
    int total = 0, laser = 0, laser_ok = 0, decrypt = 0, decrypt_ok = 0, keypad = 0;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x3a || o->state != 0) continue;
        const auto* d = static_cast<const LOCK_tag*>(o->data);
        ++total;
        // Perto o bastante para o alcance da arma (o decodificador alcança 2 m, medidos do quadril do jogador).
        const float reach = weapon_def(d->kind == 1 ? 0x4e : 0x56)->range;
        stand_before(p, o, std::min(2.0f, reach * 0.5f));
        const _VECTOR start = p->pos;
        const float yaw = p->rot.y;
        int used = -1;
        if (d->kind == 1) {
            ++laser;
            if (draw_weapon(p, 0x4e)) {
                aim_at(p, o->sphere);
                used = hold_fire(p, o, 600, &lock_open);
            }
            laser_ok += used >= 0;
        } else {
            ++decrypt;
            if (draw_weapon(p, 0x56)) {
                aim_at(p, o->sphere);
                used = hold_fire(p, o, 900, &lock_open);
            }
            decrypt_ok += used >= 0;
            if (used < 0 && d->kind == 0) {
                const uint32_t before = g_ssys_stats.keypads;
                key('E', true);
                for (int t = 0; t < 30; ++t) {
                    game_tick();
                    if (t == 3) key('E', false);
                }
                keypad += g_ssys_stats.keypads != before;
                // Fecha o teclado (o jogador volta a andar: Lock_Update, estado 4).
                for (uint8_t i = 0; i < 5; ++i) {
                    if (MenuManager_Exists(i) != 0) MenuManager_Delete(i);
                }
                for (int t = 0; t < 5; ++t) game_tick();
            }
        }
        if (used >= 0 && first.empty()) {
            first = fmt("%.2f,%.2f,%.2f,%.3f(tipo=%u ticks=%d canal=%u)", start.x, start.y, start.z, yaw, d->kind, used,
                        d->channel);
        }
    }
    if (total == 0) return "";
    return fmt(" cadeados=%d laser=%d/%d decodificador=%d/%d teclado=%d", total, laser_ok, laser, decrypt_ok, decrypt,
               keypad) + (first.empty() ? "" : " cadeado=" + first);
}

// Q-Worm: a arma 0x58, a 2 m do computador, mirando.
std::string probe_worms(obj_tag* p) {
    int total = 0, ok = 0;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x49 || o->state != 0 || static_cast<const PCQWORM_tag*>(o->data)->worm == 0) continue;
        ++total;
        stand_before(p, o, std::min(2.0f, weapon_def(0x58)->range * 0.5f));
        const _VECTOR start = p->pos;
        int used = -1;
        if (draw_weapon(p, 0x58)) {
            aim_at(p, o->sphere);
            used = hold_fire(p, o, 300, &worm_done);
        }
        ok += used >= 0;
        if (used >= 0 && first.empty()) {
            first = fmt("%.2f,%.2f,%.2f,%.3f(canal=%d)", start.x, start.y, start.z, p->rot.y,
                        static_cast<const PCQWORM_tag*>(o->data)->channel);
        }
    }
    if (total == 0) return "";
    return fmt(" qworms=%d/%d", ok, total) + (first.empty() ? "" : " qworm=" + first);
}

bool photographed(const obj_tag* o) { return (o->life & 1) != 0; }

// Fotos: a câmera (arma 0x54, que só tira mirando: botão direito), a 3 m do alvo por cada lado.
// Objetos de roteiro simples: quantos andam em laço, quantos esperam; com o canal de cada um que espera ligado por 5 s,
// quantos dispararam e se mexeram (posição ou rotação da matriz).
std::string probe_simple_scripts() {
    int total = 0, looping = 0, waiting = 0, fired = 0, moved = 0;
    struct Waiting {
        obj_tag* obj;
        MATRIX at;
    };
    std::vector<Waiting> list;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x25 || o->data == nullptr) continue;
        ++total;
        const auto* r = static_cast<const SIMPLESCRIPT_tag*>(o->data);
        if (r->mode == 8) ++looping;
        if (r->mode != 0x40) continue;
        ++waiting;
        if (r->channel == 0 || r->channel > 255) continue;
        list.push_back({o, o->matrix});
        switch_channels[r->channel] = 1;
    }
    if (total == 0) return "";
    const uint32_t triggered = g_simple_script_stats.triggered;
    for (int t = 0; t < 300; ++t) game_tick();
    fired = int(g_simple_script_stats.triggered - triggered);
    for (const Waiting& w : list) {
        // Saiu do lugar ou girou (portões e hélices só giram).
        float change = 0.0f;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 3; ++c) change += std::fabs(w.obj->matrix.m[r][c] - w.at.m[r][c]);
        }
        if (change > 0.05f) ++moved;
    }
    return fmt(" roteiros=%d laco=%d esperando=%d com_canal=%zu dispararam=%d andaram=%d", total, looping, waiting,
               list.size(), fired, moved);
}

std::string probe_photos(obj_tag* p) {
    int total = 0, ok = 0;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x48 || photographed(o)) continue;
        ++total;
        int used = -1;
        _VECTOR start{};
        for (float side : {3.0f, -3.0f}) {
            stand_before(p, o, side);
            start = p->pos;
            if (!draw_weapon(p, 0x54)) break;
            aim_at(p, o->sphere);
            key(kVkRButton, true);
            for (int t = 0; t < 60; ++t) game_tick();
            aim_at(p, o->sphere);
            used = hold_fire(p, o, 60, &photographed);
            if (used >= 0) break;
        }
        ok += used >= 0;
        if (used >= 0 && first.empty()) {
            first = fmt("%.2f,%.2f,%.2f,%.3f(canal=%u)", start.x, start.y, start.z, p->rot.y, o->unknown_f6);
        }
    }
    if (total == 0) return "";
    return fmt(" fotos=%d/%d", ok, total) + (first.empty() ? "" : " foto=" + first);
}

// Os canais de objetivo da fase (não os "não deixe acontecer") que as provas ligaram (desligados em "before").
std::string objectives_on(const uint8_t* before) {
    std::string out;
    const MISSIONDATA_tag* m = mission_of(GameState.level_file);
    if (m == nullptr || m->objectives == nullptr) return out;
    for (uint32_t i = 0; i < m->count; ++i) {
        const OBJECTIVE_tag& o = m->objectives[i];
        if ((o.flags & 2) != 0 || o.channel == 0 || switch_channels[o.channel] == 0 || before[o.channel] != 0) continue;
        out += fmt("%s%u", out.empty() ? "" : ",", o.channel);
    }
    return out.empty() ? out : " objetivos_ligados=" + out;
}

// Aperta o tiro (2 ticks) e solta (6) mirando a peça (que se mexe) até "done" ou "ticks" acabarem: as armas de um tiro
// por aperto.
int click_fire(obj_tag* p, const obj_tag* piece, obj_tag* o, int ticks, bool (*done)(const obj_tag*)) {
    BLData* b = bloke_of(p);
    int t = 0;
    for (; t < ticks && !done(o); ++t) {
        if (t % 10 == 0) aim_at(p, piece->sphere);
        key(kVkLButton, t % 8 < 2);
        game_tick();
    }
    release_keys();
    for (int k = 0; k < 10; ++k) game_tick();
    (void)b;
    return done(o) ? t : -1;
}

bool turret_destroyed(const obj_tag* o) { return o->state == 6 || (o->life & 1) != 0; }

// Torretas: com os canais delas ligando, o jogador 4 m à frente por 5 s (abre e atira?) e depois a pistola (arma 6)
// nas peças até destruir (até 15 s). Anota quantas abriram, quantos tiros deram e quantas o jogador destruiu.
std::string probe_turrets(obj_tag* p) {
    int total = 0, automatic = 0, opened = 0, destroyed = 0, damaged = 0;
    const uint32_t shots_before = g_turret_stats.shots;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x36 || (o->life & 1) != 0) continue;
        GUNTURRET* gt = static_cast<GUNTURRET*>(o->data);
        ++total;
        automatic += gt->kind == 1;
        if (gt->enable != 0) switch_channels[gt->enable] = 1;
        if (gt->disable != 0) switch_channels[gt->disable] = 0;
        _VECTOR f;
        Mat_GetDir(&f, &o->matrix);
        const _VECTOR at{o->pos.x + f.x * 4.0f, o->pos.y - 1.0f, o->pos.z + f.z * 4.0f, 1.0f};
        place(p, at, std::atan2(-f.x, -f.z));
        const _VECTOR start = p->pos;
        bool open = false;
        for (int t = 0; t < 300; ++t) {
            game_tick();
            open = open || o->state == 4;
        }
        opened += open;
        int used = -1;
        if (draw_weapon(p, 6)) {
            obj_tag* piece = gt->barrel != nullptr ? gt->barrel : o;
            used = click_fire(p, piece, o, 2400, &turret_destroyed);
        }
        destroyed += used >= 0;
        damaged += gt->health < 100.0f;
        if (first.empty()) {
            first = fmt("%.2f,%.2f,%.2f,%.3f(tipo=%d abriu=%d destruida=%d)", start.x, start.y, start.z, p->rot.y,
                        gt->kind, open ? 1 : 0, used >= 0 ? 1 : 0);
        }
    }
    if (total == 0) return "";
    return fmt(" torretas=%d automaticas=%d abriram=%d tiros=%u danificadas=%d destruidas=%d torreta=", total, automatic,
               opened, g_turret_stats.shots - shots_before, damaged, destroyed) + first;
}

bool copter_alive(const COPTER* c) {
    for (LLNODE_tag* n = CopterList.first; n != nullptr; n = n->next) {
        if (reinterpret_cast<const COPTER*>(n) == c) return c->obj != nullptr && (c->obj->life & 1) == 0;
    }
    return false;
}

// O jogador "pendurado" 15 m à frente do helicóptero e 1 m abaixo do eixo (refeito a cada tick, sem queda): o cone de
// 45 graus e a linha de visão passam como passariam com ele num telhado à vista.
void hold_in_front(obj_tag* p, const COPTER* c) {
    _VECTOR dir;
    Mat_GetDir(&dir, const_cast<MATRIX*>(&c->obj->matrix));
    const float* r = c->obj->matrix.m[3];
    const _VECTOR at{r[0] + dir.x * 15.0f, r[1] + dir.y * 15.0f - 1.0f, r[2] + dir.z * 15.0f, 1.0f};
    p->pos = at;
    p->sphere = {at.x, at.y, at.z, p->sphere.w};
    bloke_of(p)->fall = {};
}

// Helicópteros: com o canal deles ligado, o jogador parado na partida por até 20 s (o helicóptero aparece e ronda; nas
// partidas, dentro de prédios, ele não vê o jogador); depois o jogador à frente dele por até 10 s (ataca e atira?); por
// fim foguetes (Explode_Create no corpo, 60 de dano a cada 0,5 s, como um míssil do jogador) até ele fugir ou explodir:
// o canal de "acabou" (parâmetro 7) liga.
std::string probe_copters(obj_tag* p, const _VECTOR& home, float home_yaw) {
    std::vector<COPTER*> copters;
    for (LLNODE_tag* n = CopterList.first; n != nullptr; n = n->next) {
        COPTER* c = reinterpret_cast<COPTER*>(n);
        if (c->obj != nullptr && (c->obj->life & 1) == 0) copters.push_back(c);
    }
    if (copters.empty()) return "";
    int seen_home = 0, attacked = 0, beaten = 0;
    const uint32_t shots_before = g_copter_stats.shots;
    std::string first;
    for (COPTER* c : copters) {
        if (!copter_alive(c)) continue;
        const uint16_t done = c->done;
        const bool patrol_only = c->channel == 100;
        place(p, home, home_yaw);
        if (c->enable != 0) switch_channels[c->enable] = 1;
        uint32_t attacks = g_copter_stats.attacks;
        for (int t = 0; t < 1200 && g_copter_stats.attacks == attacks && copter_alive(c); ++t) game_tick();
        seen_home += g_copter_stats.attacks != attacks;
        attacks = g_copter_stats.attacks;
        const uint32_t shots = g_copter_stats.shots;
        int in_attack = 0;
        for (int t = 0; t < 600 && copter_alive(c) && !patrol_only; ++t) {
            hold_in_front(p, c);
            game_tick();
            if (c->obj->state == 2 && ++in_attack >= 240) break;
        }
        const bool attacked_here = g_copter_stats.attacks != attacks || in_attack != 0;
        attacked += attacked_here;
        place(p, home, home_yaw);
        const uint32_t fled = g_copter_stats.fled;
        for (int t = 0; t < 1800 && copter_alive(c) && g_copter_stats.fled == fled; ++t) {
            if (t % 30 == 0 && c->parts[2] != nullptr) {
                _VECTOR at = c->parts[2]->sphere;
                Explode_Create(c->parts[2], &at, &CONSTANT_UP1, 3.0f, 3.0f, 0x6000052, 60.0f, 0xff, 0xff, 'd', p, 0);
            }
            game_tick();
        }
        // Fugindo ou explodindo, o canal liga já; o resto da fuga anda sozinho.
        const bool won = g_copter_stats.fled != fled && (done == 0 || switch_channels[done] != 0);
        beaten += won;
        if (first.empty()) {
            first = fmt("(so_ronda=%d atacou=%d tiros=%u venceu=%d foi_embora=%d canal=%u)", patrol_only ? 1 : 0,
                        attacked_here ? 1 : 0, g_copter_stats.shots - shots, won ? 1 : 0,
                        !won && !copter_alive(c) ? 1 : 0, done);
        }
    }
    return fmt(" helicopteros=%zu viram_na_partida=%d atacaram=%d tiros=%u vencidos=%d helicoptero=", copters.size(),
               seen_home, attacked, g_copter_stats.shots - shots_before, beaten) + first;
}

// ---- as provas do sistema de segurança e dos objetos de jogo novos ----

// O jogador "pendurado" em "at" (sem queda): refeito antes de cada tick, como hold_in_front.
void hold_at(obj_tag* p, const _VECTOR& at) {
    p->pos = at;
    p->sphere = {at.x, at.y, at.z, p->sphere.w};
    bloke_of(p)->fall = {};
}

// O que mudou entre os personagens e os canais desde a construção: personagens com o alerta ou o estado da máquina
// mudados (os que estão no mapa, os nascidos dos geradores contam à parte) e os canais ligados, cada um com quem o
// espera.
struct Reaction {
    std::vector<uint8_t> alert;
    std::vector<uint16_t> state;
    std::vector<std::pair<const obj_tag*, uint16_t>> turrets;
    uint32_t spawned = 0;
    uint8_t channels[256];
    Reaction() {
        for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type == 0x36) turrets.push_back({o, o->state});
        }
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
            const uint8_t* st = Drone_AlertStatus(d->obj);
            alert.push_back(st != nullptr ? st[0] : 0);
            state.push_back(uint16_t(d->sm.state));
        }
        spawned = g_drone_spawner_stats.spawned;
        std::memcpy(channels, switch_channels, sizeof(channels));
    }
    // Torretas que saíram do repouso (abrindo ou atirando).
    int turrets_moved() const {
        int n = 0;
        for (const auto& [o, st] : turrets) n += st == 0 && o->type == 0x36 && o->state != 0;
        return n;
    }
    std::string diff(int* drones, uint32_t* born, int* opened) const {
        int d = 0, k = 0, c = 0;
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr && k < int(alert.size()); n = n->next, ++k) {
            const Drone_tag* dr = reinterpret_cast<const Drone_tag*>(n);
            const uint8_t* st = Drone_AlertStatus(dr->obj);
            const uint8_t now = st != nullptr ? st[0] : 0;
            if (now != alert[size_t(k)] || uint16_t(dr->sm.state) != state[size_t(k)]) ++d;
        }
        std::string list;
        for (int ch = 1; ch < 256; ++ch) {
            if (switch_channels[ch] != 0 && channels[ch] == 0) {
                ++c;
                std::string who;
                for (const std::string& n : consumers(uint32_t(ch))) who += (who.empty() ? "" : "/") + n;
                list += fmt("%s%d(%s)", list.empty() ? "" : "+", ch, who.empty() ? "-" : who.c_str());
            }
        }
        *drones = d;
        *born = g_drone_spawner_stats.spawned - spawned;
        *opened = c;
        return list;
    }
};

std::string names(const std::set<std::string>& s) {
    std::string out;
    for (const std::string& n : s) out += (out.empty() ? "" : "/") + n;
    return out.empty() ? "-" : out;
}

// Câmeras de segurança: o jogador parado na frente de cada câmera ligada com modelo (a metade do alcance, no máximo 4
// m, pela frente dela, refeito a cada tick), 3 s; anota as que o viram (estado 1) e as que ligaram o alarme. Para as
// que ligaram, mais 5 s com ele ali: quantos personagens mudaram de alerta, quantos os geradores criaram e que canais
// ligaram (o alarme e o que os gatilhos que o esperam fazem).
std::string probe_sensors(obj_tag* p) {
    int total = 0, armed = 0, saw = 0, alarms = 0, drones = 0, opened = 0, turrets = 0;
    uint32_t born = 0;
    std::set<std::string> waiting;
    std::string first, chain;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x37 || (o->life & 1) != 0 || o->state == 3 || o->glist == nullptr) continue;
        SENSOR* s = static_cast<SENSOR*>(o->data);
        ++total;
        armed += s->alarm != 0;
        const Reaction before;
        bool seen = false, alarm = false;
        _VECTOR at{};
        for (int t = 0; t < 180; ++t) {
            _VECTOR dir;
            Mat_GetDir(&dir, &o->matrix);
            const float d = std::min(s->range * 0.5f, 4.0f);
            at = {o->matrix.m[3][0] + dir.x * d, o->matrix.m[3][1] + dir.y * d, o->matrix.m[3][2] + dir.z * d, 1.0f};
            hold_at(p, at);
            game_tick();
            seen = seen || o->state == 1;
            alarm = alarm || (s->alarm != 0 && switch_channels[s->alarm] != 0);
        }
        saw += seen;
        alarms += alarm;
        if (alarm) {
            for (int t = 0; t < 300; ++t) {
                hold_at(p, at);
                game_tick();
            }
            int dd = 0, cc = 0;
            uint32_t bb = 0;
            const std::string here = before.diff(&dd, &bb, &cc);
            drones += dd;
            born += bb;
            opened += cc;
            turrets += before.turrets_moved();
            for (const std::string& n : consumers(s->alarm)) waiting.insert(n);
            if (chain.empty()) chain = here;
        }
        if (first.empty() || (alarm && first.find("alarme=1") == std::string::npos)) {
            _VECTOR dir;
            Mat_GetDir(&dir, &o->matrix);
            first = fmt("%.2f,%.2f,%.2f,%.3f(viu=%d alarme=%d canal=%u)", at.x, at.y, at.z, std::atan2(-dir.x, -dir.z),
                        seen ? 1 : 0, alarm ? 1 : 0, s->alarm);
        }
    }
    if (total == 0) return "";
    return fmt(" cameras=%d com_alarme=%d viram=%d alarmes=%d reagiram=personagens:%d,gerados:%u,torretas:%d,"
               "canais:%d espera=%s ligou=%s camera=",
               total, armed, saw, alarms, drones, born, turrets, opened, names(waiting).c_str(),
               chain.empty() ? "-" : chain.c_str()) +
           first;
}

// Monitores: usar cada um (Monitor_Activate, como o E de Player_Activate): a câmera do jogador vira a da câmera de
// segurança (a matriz igual) com o painel 16 do HUD no lugar do 0; E passa às outras do grupo até o jogador voltar.
std::string probe_monitors(obj_tag* p, const _VECTOR& home, float home_yaw) {
    int total = 0, shown = 0, matched = 0, hud = 0, back = 0;
    uint32_t cameras = 0;
    BLData* b = bloke_of(p);
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x38 || (o->life & 1) != 0 || o->state != 0) continue;
        ++total;
        place(p, home, home_yaw);
        Monitor_Activate(o, p);
        if (o->state != 2) continue;
        ++shown;
        const auto* m = static_cast<const MONITOR_tag*>(o->data);
        for (int t = 0; t < 20; ++t) game_tick();
        // A câmera do jogador na câmera de segurança (as que varrem andam depois da cópia no mesmo tick: até 10 cm).
        if (m->camera != nullptr) {
            const MATRIX& v = glb_viewer[0]->camera;
            const MATRIX& c = m->camera->matrix;
            const float dx = v.m[3][0] - c.m[3][0], dy = v.m[3][1] - c.m[3][1], dz = v.m[3][2] - c.m[3][2];
            const float dot = v.m[2][0] * c.m[2][0] + v.m[2][1] * c.m[2][1] + v.m[2][2] * c.m[2][2];
            matched += dx * dx + dy * dy + dz * dz < 0.01f && 0.99f < dot;
        }
        const auto* h = static_cast<const HUDINFO_tag*>(b->hud);
        hud += h != nullptr && h->panes[16].enabled != 0 && h->panes[0].enabled == 0;
        uint32_t seen = 1;
        for (int k = 0; k < 20 && o->state == 2; ++k) {
            key('E', true);
            game_tick();
            game_tick();
            key('E', false);
            for (int t = 0; t < 10; ++t) game_tick();
            seen += o->state == 2;
        }
        cameras += seen;
        back += o->state != 2 && b->active != 0 && glb_viewer[0]->updater == Player_PositionCamera;
        release_keys();
        for (int t = 0; t < 10; ++t) game_tick();
    }
    if (total == 0) return "";
    return fmt(" monitores=%d mostraram=%d imagem=%d painel16=%d cameras_vistas=%u voltou=%d", total, shown, matched, hud,
               cameras, back);
}

// Holofotes: cada lâmpada (acesa à força, se espera um canal), o jogador 1 m acima de onde a mancha está, até 10 s:
// anota as que o acharam (estado 1), o alarme, se seguem o jogador 2 s e se a luz da mancha ilumina um objeto (flag
// 0x80) ali (Lights_CalcClosestLights).
std::string probe_searchlights(obj_tag* p) {
    int lamps = 0, forced = 0, lit = 0, placed = 0, found = 0, alarms = 0, follow = 0, light = 0, drones = 0;
    uint32_t born = 0;
    std::set<std::string> waiting;
    std::string first, chain;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x22 || (o->life & 1) != 0) continue;
        SEARCHLIGHT_tag* d = static_cast<SEARCHLIGHT_tag*>(o->data);
        if (d->kind != 1 || d->parts[2] == nullptr) continue;
        ++lamps;
        // Apagada esperando o canal de acender: acesa à força (o canal pode ser a saída de um gatilho que Trigger_Calc
        // refaz a cada tick, como o 0xa4 de 07000005, o alarme da fase).
        if (o->state == 3 && d->start != 0) {
            o->state = 0;
            ++forced;
        }
        obj_tag* spot = d->parts[2];
        bool got = false, placed_here = false;
        _VECTOR at{};
        const Reaction before;
        for (int t = 0; t < 600 && !got; ++t) {
            if (-5000.0f < spot->pos.y) {
                at = {spot->pos.x, spot->pos.y + 1.0f, spot->pos.z, 1.0f};
                hold_at(p, at);
            }
            game_tick();
            lit += t == 0 && o->state == 0;
            placed_here = placed_here || -5000.0f < spot->pos.y;
            got = o->state == 1;
        }
        found += got;
        placed += placed_here;
        if (!got) {
            if (first.empty()) {
                first = fmt("%.2f,%.2f,%.2f(nao_achou estado=%d acende=%u mancha=%d giro=%.2f incl=%.2f)", o->pos.x,
                            o->pos.y, o->pos.z, int16_t(o->state), d->start, placed_here ? 1 : 0, o->rot.y, o->rot.x);
            }
            continue;
        }
        alarms += d->alarm != 0 && switch_channels[d->alarm] != 0;
        if (d->alarm != 0) {
            for (const std::string& n : consumers(d->alarm)) waiting.insert(n);
        }
        int still = 0;
        for (int t = 0; t < 120; ++t) {
            hold_at(p, at);
            game_tick();
            still += o->state == 1;
        }
        follow += still == 120;
        {
            int dd = 0, cc = 0;
            uint32_t bb = 0;
            const std::string here = before.diff(&dd, &bb, &cc);
            drones += dd;
            born += bb;
            if (chain.empty()) chain = here;
        }
        uint16_t count = 0;
        light_tag** lights = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&spot->pos), 0x80, &count);
        bool mine = false;
        for (uint16_t k = 0; k < count; ++k) mine = mine || lights[k] == Searchlight_Light(spot);
        light += mine;
        if (first.empty() || first.find("nao_achou") != std::string::npos) {
            first = fmt("%.2f,%.2f,%.2f(alarme=%u)", at.x, at.y - 1.0f, at.z, d->alarm);
        }
    }
    if (lamps == 0) return "";
    return fmt(" holofotes=%d acesos_a_forca=%d varrendo=%d manchas=%d acharam=%d alarmes=%d seguiram=%d iluminam=%d "
               "reagiram=personagens:%d,gerados:%u espera=%s ligou=%s holofote=",
               lamps, forced, lit, placed, found, alarms, follow, light, drones, born, names(waiting).c_str(),
               chain.empty() ? "-" : chain.c_str()) +
           first;
}

// Minas: o jogador encostado em cada uma (o centro dele no da mina) até 1 s; a que não explodir, tiros de pistola a 4
// m.
std::string probe_mines(obj_tag* p) {
    int total = 0, touched = 0, shot = 0;
    std::string first;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x4b || (o->life & 1) != 0) continue;
        ++total;
        const uint32_t blown = g_hazard_stats.mines_blown;
        const _VECTOR at{o->pos.x, o->pos.y, o->pos.z, 1.0f};
        for (int t = 0; t < 60 && g_hazard_stats.mines_blown == blown; ++t) {
            hold_at(p, at);
            game_tick();
        }
        if (g_hazard_stats.mines_blown != blown) {
            ++touched;
        } else if (draw_weapon(p, 6)) {
            place(p, {o->pos.x, o->pos.y, o->pos.z - 4.0f, 1.0f}, 0.0f);
            for (int t = 0; t < 240 && g_hazard_stats.mines_blown == blown; ++t) {
                if (t % 10 == 0) aim_at(p, o->pos);
                key(kVkLButton, t % 8 < 2);
                game_tick();
            }
            release_keys();
            shot += g_hazard_stats.mines_blown != blown;
        }
        if (first.empty()) first = fmt("%.2f,%.2f,%.2f", at.x, at.y, at.z);
        for (int t = 0; t < 30; ++t) game_tick();
    }
    if (total == 0) return "";
    return fmt(" minas=%d(de %u) explodiram_tocadas=%d explodiram_baleadas=%d mina=", total, g_hazard_stats.mines, touched,
               shot) +
           first;
}

// O canhão montado (0700000d): o jogador o usa (GunImp_Activate, como o E de Player_Activate), atira 5 s segurando o
// gatilho e desmonta com E.
std::string probe_gunimp(obj_tag* p, const _VECTOR& home, float home_yaw) {
    obj_tag* best = nullptr;
    float farthest = -1.0f;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != 0x47 || (o->life & 1) != 0) continue;
        const float d = o->pos.x * o->pos.x + o->pos.y * o->pos.y + o->pos.z * o->pos.z;
        if (farthest < d) {
            farthest = d;
            best = o;
        }
    }
    if (best == nullptr) return "";
    place(p, {best->pos.x, best->pos.y + 0.5f, best->pos.z - 1.5f, 1.0f}, 0.0f);
    const int16_t sub = int16_t(p->unknown_f6);
    GunImp_Activate(best, p);
    const bool mounted = best->state == 1;
    const uint32_t shots = g_gunimp_stats.shots, heats = g_gunimp_stats.overheats;
    key(kVkLButton, true);
    for (int t = 0; t < 300; ++t) game_tick();
    key(kVkLButton, false);
    game_tick();
    key('E', true);
    game_tick();
    game_tick();
    key('E', false);
    for (int t = 0; t < 10; ++t) game_tick();
    const bool left = best->state == 0 && bloke_of(p)->ride == nullptr && int16_t(p->unknown_f6) == sub;
    place(p, home, home_yaw);
    return fmt(" canhao=montou:%d,tiros:%u,superaqueceu:%u,desmontou:%d(%.2f,%.2f,%.2f)", mounted ? 1 : 0,
               g_gunimp_stats.shots - shots, g_gunimp_stats.overheats - heats, left ? 1 : 0, best->pos.x, best->pos.y,
               best->pos.z);
}

// Os objetos de canal: atiradores, inversores, tremores, o laser e os mísseis da estação. Os canais de entrada ligados
// à força; anota quantos atiraram e tremeram e, na estação, 40 s depois: cargas e tiros do laser, mísseis armados,
// lançados ou sabotados.
std::string probe_channel_objects() {
    int shooters = 0, inverters = 0, quakes = 0, lasers = 0, missiles = 0;
    std::vector<uint16_t> enable;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if ((o->life & 1) != 0) continue;
        switch (o->type) {
            case 0x4e:
                ++shooters;
                enable.push_back(o->state);
                break;
            case 0x53:
                ++inverters;
                break;
            case 0x54:
                ++quakes;
                enable.push_back(static_cast<const APOCALYPSE_tag*>(o->data)->channel);
                break;
            // O canal de ligar da estação (0x13 em 0700001b) é a saída de um gatilho que Trigger_Calc refaz a cada
            // tick: a prova zera o canal no objeto (0: sempre ligado).
            case 0x50:
                ++lasers;
                static_cast<SPACELASER_tag*>(o->data)->enable = 0;
                break;
            case 0x4f:
                ++missiles;
                static_cast<SPACEMISSILE_tag*>(o->data)->enable = 0;
                break;
            default:
                break;
        }
    }
    if (shooters + inverters + quakes + lasers + missiles == 0) return "";
    const HazardStats h = g_hazard_stats;
    const SpaceStats st = g_space_stats;
    for (uint16_t c : enable) {
        if (c != 0) switch_channels[uint8_t(c)] = 1;
    }
    const int ticks = lasers + missiles != 0 ? 40 * 60 : 120;
    for (int t = 0; t < ticks; ++t) game_tick();
    std::string out = fmt(" atiradores=%d tiros=%u inversores=%d tremores=%d tremeram=%u", shooters,
                          g_hazard_stats.shots - h.shots, inverters, quakes, g_hazard_stats.quakes_on - h.quakes_on);
    if (lasers + missiles != 0) {
        out += fmt(" estacao: lasers=%d cargas=%u tiros=%u misseis=%d armados=%u lancados=%u sabotados=%u", lasers,
                   g_space_stats.charges - st.charges, g_space_stats.shots - st.shots, missiles,
                   g_space_stats.armed - st.armed, g_space_stats.launched - st.launched,
                   g_space_stats.sabotaged - st.sabotaged);
    }
    return out;
}

// ---- o polimento da jogabilidade: corpos rígidos, o laser e o relógio, a mira automática, a água, a metralhadora ----

const _VECTOR& row3_of(const obj_tag* o) { return *reinterpret_cast<const _VECTOR*>(&o->matrix.m[3][0]); }

// O jogador a "dist" do ponto, numa das quatro direções em xz de onde o vê (Collide_LineOfSight dos olhos), mirando
// nele. Devolve se achou.
bool stand_seeing(obj_tag* p, const _VECTOR& target, float dist, const obj_tag* ignore) {
    static const float kDirs[4][2] = {{0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f}};
    for (const auto& d : kDirs) {
        const _VECTOR at{target.x + d[0] * dist, target.y, target.z + d[1] * dist, 1.0f};
        place(p, at, std::atan2(-d[0], -d[1]));
        aim_at(p, target);
        _VECTOR head{};
        Player_GetHeadPos(&head, p, nullptr);
        if (Collide_LineOfSight(&head, &target, p->cel, p, ignore, 8) && Vec_Dist3D(&head, &target) < dist * 2.0f) {
            return true;
        }
    }
    return false;
}

// Uma arma de fogo comum carregada na fase (as pistolas 2 e 6, a 0x10).
bool draw_any_gun(obj_tag* p) { return draw_weapon(p, 2) || draw_weapon(p, 6) || draw_weapon(p, 0x10); }

// Corpos rígidos: quantos de cada tipo; um pêndulo e um solto levam tiros de pistola por 2 s, mirando o centro a 4 m:
// quanto o pêndulo inclinou (o "cima" contra a vertical, o máximo) e quanto o solto andou.
std::string probe_rigid_bodies(obj_tag* p) {
    int total = 0, with_coll = 0;
    int kinds[5] = {};
    obj_tag* pendulum = nullptr;
    obj_tag* loose = nullptr;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type != kRigidBodyType || (o->life & 1) != 0) continue;
        const RIGIDBODY* rb = static_cast<const RIGIDBODY*>(o->data);
        ++total;
        with_coll += o->glist != nullptr && o->glist->coll != nullptr;
        ++kinds[std::min<uint16_t>(rb->kind, 4)];
        if (rb->kind == 0 && loose == nullptr) loose = o;
        if (rb->kind != 0 && pendulum == nullptr) pendulum = o;
    }
    if (total == 0) return "";
    std::string out = fmt(" corpos=%d tipos=%d,%d,%d,%d,%d com_colisao=%d", total, kinds[0], kinds[1], kinds[2], kinds[3],
                          kinds[4], with_coll);
    for (obj_tag* o : {pendulum, loose}) {
        if (o == nullptr) continue;
        const RIGIDBODY* rb = static_cast<const RIGIDBODY*>(o->data);
        const _VECTOR start = row3_of(o);
        const char* name = rb->kind == 0 ? "solto" : "pendulo";
        if (!stand_seeing(p, start, 4.0f, o) || !draw_any_gun(p)) {
            out += fmt(" %s=sem_visada", name);
            continue;
        }
        const _VECTOR at = p->pos;
        const float yaw = p->rot.y;
        const uint32_t hits = g_rigid_body_stats.bullet_hits;
        float tilt = 0.0f, moved = 0.0f;
        for (int t = 0; t < 120; ++t) {
            if (t % 10 == 0) aim_at(p, row3_of(o));
            key(kVkLButton, t % 12 < 2);
            game_tick();
            if ((o->life & 1) != 0) break;
            tilt = std::max(tilt, std::acos(std::clamp(o->matrix.m[1][1], -1.0f, 1.0f)) * 57.29578f);
            moved = std::max(moved, Vec_Dist3D(&start, &row3_of(o)));
        }
        release_keys();
        for (int k = 0; k < 10; ++k) game_tick();
        out += fmt(" %s=%.2f,%.2f,%.2f,%.3f(tipo=%u tiros=%u inclinou=%.1f andou=%.2f)", name, at.x, at.y, at.z, yaw, rb->kind,
                   g_rigid_body_stats.bullet_hits - hits, tilt, moved);
    }
    return out;
}

// O laser do relógio (arma 0x4e: o raio de Draw_LaserBeam, o mostrador e os ponteiros de Player_SetWatch) e a mira
// laser das pistolas com a flag 8 (arma 6: o feixe e o ponto de Player_LaserPointer), cada um 1 s mirando à frente.
std::string probe_lasers(obj_tag* p, const _VECTOR& home, float home_yaw) {
    BLData* b = bloke_of(p);
    std::string out;
    place(p, home, home_yaw);
    if (draw_weapon(p, 0x4e)) {
        b->weapons[0x4e].rounds = weapon_def(0x4e)->clip;  // as provas dos cadeados gastam o laser
        key(kVkLButton, true);
        float len = 0.0f;
        uint32_t model = 0;
        int seen = 0;
        for (int t = 0; t < 60; ++t) {
            game_tick();
            const obj_tag* beam = b->fx[0];
            if (beam != nullptr && beam->glist != nullptr && beam->unknown_f8 != 0) {
                ++seen;
                len = beam->scale;
                model = hashtable_celglist_to_hashcode(beam->glist);
            }
        }
        release_keys();
        for (int k = 0; k < 10; ++k) game_tick();
        out += fmt(" relogio=ponteiros:%d raio:%08x quadros:%d comprimento:%.2f",
                   (Hours != nullptr) + (Minutes != nullptr) + (Seconds != nullptr), model, seen, len);
    }
    place(p, home, home_yaw);
    if (draw_weapon(p, 6)) {
        float len = 0.0f, dot = -1.0f;
        for (int t = 0; t < 60; ++t) {
            game_tick();
            if (b->gun_flag8 != 0 && b->fx[0] != nullptr && b->fx[0]->glist != nullptr) len = b->fx[0]->scale;
            if (b->glow != nullptr && b->glow->unknown_f8 != 0) {
                _VECTOR head{};
                Player_GetHeadPos(&head, p, nullptr);
                dot = Vec_Dist3D(&head, &b->glow->pos);
            }
        }
        out += fmt(" mira_laser=ligada:%u feixe:%.2f ponto:%.2f", b->gun_flag8, len, dot);
    }
    return out;
}

// A mira automática: o inimigo à vista mais perto, o jogador a 6 m na frente dele, a mira 4 graus ao lado do peito,
// cinco tiros com a opção desligada e depois ligada (o inimigo de volta ao lugar entre as duas, com vida alta para não
// morrer): quantos tiros o acertaram em cada uma (Check_AutoAim leva a bala ao peito dentro do cone de 10 graus x a
// mira automática da arma x a distância).
std::string probe_autoaim(obj_tag* p) {
    Drone_tag* target = nullptr;
    float best = 1e9f;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (d->obj == nullptr || d->side != 1 || d->health <= 0.0f || (d->obj->life & 1) != 0) continue;
        // Fora os que esperam escondidos um canal (sem colisão: flag 0x10) e os que a mira automática pula.
        if (d->obj->state == 1 || d->obj->unknown_fc == 0 || (d->obj->flags & 0x10) != 0) continue;
        const float k = Vec_Dist3D(&d->obj->pos, &p->pos);
        if (k < best) {
            best = k;
            target = d;
        }
    }
    if (target == nullptr) return "";
    obj_tag* o = target->obj;
    const _VECTOR pos0 = o->pos, rot0 = o->rot;
    const float health0 = target->health;
    BLData* b = bloke_of(p);
    const uint8_t setting = PlayerSetting[0].autoaim;
    int results[2] = {-1, -1};
    int seen = 0;
    std::string where;
    for (int on = 0; on < 2; ++on) {
        o->pos = pos0;
        o->rot = rot0;
        target->health = health0;
        PlayerSetting[0].autoaim = uint8_t(on);
        _VECTOR f{};
        Mat_GetDir(&f, &o->matrix);
        const _VECTOR at{pos0.x + f.x * 6.0f, pos0.y, pos0.z + f.z * 6.0f, 1.0f};
        place(p, at, std::atan2(-f.x, -f.z));
        if (!draw_any_gun(p)) return " mira_auto=sem_arma";
        // Vida alta durante a prova: o inimigo não morre (um tiro que acerta só tira vida).
        target->health = 1000.0f;
        int hits = 0;
        for (int shot = 0; shot < 5; ++shot) {
            // Mira 4 graus ao lado do peito e espera a câmera acompanhar (8 ticks) antes do tiro.
            for (int t = 0; t < 8; ++t) {
                _VECTOR chest{};
                AnimGetBoneWorldTrans(o, 0x80000002u, 0, &chest, nullptr);
                aim_at(p, chest);
                if (t == 0) {
                    _VECTOR head{};
                    Player_GetHeadPos(&head, p, nullptr);
                    seen += Collide_LineOfSight(&head, &chest, p->cel, p, o, 10) ? 1 : 0;
                }
                p->rot.y += 0.07f;
                Control_GetObjMatrix(p, &p->matrix);
                game_tick();
            }
            // Um acerto: um projétil (objeto 5) na lista de acertos do inimigo (Collide_Update), em qualquer estado
            // dele (alguns estados não tiram vida).
            bool hit = false;
            const auto bullet_on_target = [&]() {
                for (const HITDATA_tag* h = o->hits; h != nullptr; h = h->next) {
                    if (h->obj != nullptr && h->obj->type == 5) return true;
                }
                return false;
            };
            key(kVkLButton, true);
            for (int t = 0; t < 12; ++t) {
                if (t == 2) key(kVkLButton, false);
                game_tick();
                hit = hit || bullet_on_target();
            }
            hits += hit;
        }
        results[on] = hits;
        if (where.empty()) where = fmt("%.2f,%.2f,%.2f,%.3f", p->pos.x, p->pos.y, p->pos.z, p->rot.y);
        release_keys();
    }
    PlayerSetting[0].autoaim = setting;
    o->pos = pos0;
    o->rot = rot0;
    target->health = health0;
    (void)b;
    return fmt(" mira_auto=desligada:%d ligada:%d visada:%d dist:%.1f em:", results[0], results[1], seen, best) + where;
}

// A água: salas com água (cel_tag::water_y acima de -500); o jogador posto no meio da primeira, 1,5 m abaixo da
// superfície, por 4 s: se nadou (subestado 3), o menor ar, se ficou submerso (BLData::water_flags 0x10) e se a câmera
// passou por Camera_Shear (GlobalBlur 0x91 no fim do tick).
std::string probe_water(obj_tag* p) {
    int rooms = 0;
    cel_tag* first = nullptr;
    for (cel_tag* c = glb_world != nullptr ? glb_world->first : nullptr; c != nullptr; c = c->next) {
        if (c->water_y <= -499.0f) continue;
        ++rooms;
        if (first == nullptr) first = c;
    }
    if (rooms == 0) return "";
    BLData* b = bloke_of(p);
    const _VECTOR at{(first->box_min.x + first->box_max.x) * 0.5f, first->water_y - 1.5f,
                     (first->box_min.z + first->box_max.z) * 0.5f, 1.0f};
    place(p, at, 0.0f);
    const _VECTOR start = p->pos;
    int swam = 0, under = 0, shear = 0;
    float air = 100.0f, low = p->pos.y;
    // Nadando, olha para baixo e segura W (mergulha: Player_Move leva a velocidade pela inclinação da visão).
    key('W', true);
    for (int t = 0; t < 240; ++t) {
        b->pitch = -0.8f;
        b->pitch_target = -0.8f;
        b->look_state = 1;
        game_tick();
        swam += p->unknown_f6 == kPlrSwim;
        under += (b->water_flags & 0x10) != 0;
        air = std::min(air, b->air);
        low = std::min(low, p->pos.y);
        shear += GlobalBlur >= 0x81;
    }
    release_keys();
    return fmt(" agua=salas:%d nadou:%d submerso:%d ar:%.1f desceu:%.2f ondulou:%d em:%.2f,%.2f,%.2f", rooms, swam,
               under, air, start.y - low, shear, start.x, start.y, start.z);
}

// A metralhadora 0x52 (a que se tira de uma torreta montável): com a pele dela carregada, um tiro monta uma torreta
// no chão à frente (GT_DeployMiniGun, Collide_PlaceObject).
std::string probe_minigun(obj_tag* p, const _VECTOR& home, float home_yaw) {
    const uint32_t skin = weapon_def(0x52)->skin;
    if (skin == 0 || hashtable_getitem(skin) == nullptr) return "";
    place(p, home, home_yaw);
    if (!draw_weapon(p, 0x52)) return " metralhadora=sem_arma";
    int before = 0;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) before += o->type == 0x36 && (o->life & 1) == 0;
    bloke_of(p)->pitch = -0.2f;
    key(kVkLButton, true);
    for (int t = 0; t < 4; ++t) game_tick();
    release_keys();
    for (int t = 0; t < 30; ++t) game_tick();
    int after = 0;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) after += o->type == 0x36 && (o->life & 1) == 0;
    return fmt(" metralhadora=montou:%d em:%.2f,%.2f,%.2f,%.3f", after - before, p->pos.x, p->pos.y, p->pos.z, p->rot.y);
}

}  // namespace

std::string traverse_probe() {
    obj_tag* p = glb_players[0];
    if (p == nullptr) return " travessia=sem_jogador";
    struct Restore {
        uint32_t x = SEED_X, y = SEED_Y;
        GAMESTATE_tag state = GameState;
        std::vector<uint8_t> channels{switch_channels, switch_channels + 256};
        std::map<std::string, int> stubs = pending_counts();
        uint8_t cheats[sizeof(CheatInfo)];
        PsiKeyboardMouse km = g_psi_keyboard_mouse;
        uint8_t players[4];
        Restore() {
            std::memcpy(cheats, CheatInfo, sizeof(cheats));
            std::memcpy(players, MPSettings + 0x1ac, 4);
        }
        ~Restore() {
            SEED_X = x;
            SEED_Y = y;
            GameState = state;
            std::copy(channels.begin(), channels.end(), switch_channels);
            pending_restore(stubs);
            std::memcpy(CheatInfo, cheats, sizeof(cheats));
            g_psi_keyboard_mouse = km;
            std::memcpy(MPSettings + 0x1ac, players, 4);
        }
    } restore;
    // Um jogador, como bootup_bootup (sem isso Input_Update não lê o teclado: o nf_loader_check não roda o boot).
    const uint32_t one = 1;
    std::memcpy(MPSettings + 0x1ac, &one, 4);
    // O jogador pode ter morrido nos testes de antes: volta vivo, invencível e sem menu.
    BLData* b = bloke_of(p);
    if (p->type == 0x12) p->type = 3;
    b->health = 100.0f;
    std::memset(CheatInfo, 0, 4);
    CheatInfo[0] = 1;
    GameState.paused = 0;
    g_psi_keyboard_mouse.active = true;
    uint8_t channels_before[256];
    std::memcpy(channels_before, switch_channels, sizeof(channels_before));
    const _VECTOR home = p->pos;
    const float home_yaw = p->rot.y;
    std::string out = " travessia:";
    out += probe_ladder(p);
    out += probe_zipline(p);
    out += probe_rope(p);
    out += probe_grapple(p);
    out += probe_creep(p);
    out += probe_locks(p);
    out += probe_worms(p);
    out += probe_photos(p);
    out += probe_copters(p, home, home_yaw);
    out += objectives_on(channels_before);
    // Os roteiros simples depois dos objetivos: a prova liga os canais deles.
    out += probe_simple_scripts();
    // As torretas por último: a prova liga os canais delas.
    out += probe_turrets(p);
    // A segurança e os objetos novos: câmeras, monitores, holofotes, minas, o canhão e os objetos de canal.
    std::string sec = probe_sensors(p);
    sec += probe_monitors(p, home, home_yaw);
    sec += probe_searchlights(p);
    sec += probe_mines(p);
    sec += probe_gunimp(p, home, home_yaw);
    sec += probe_channel_objects();
    if (!sec.empty()) out += " seguranca:" + sec;
    // O polimento da jogabilidade: a mira automática primeiro (precisa de um inimigo vivo), depois os corpos rígidos,
    // os lasers, a metralhadora montável e a água.
    // Numa fase cuja cena de abertura ainda toca, o jogador está desligado (Player_Disable: não atira nem colide):
    // religa como no fim da cena.
    if (bloke_of(p)->active != 1 || (p->flags & 0x10) != 0) Player_Enable(p, nullptr, 0);
    std::string pol = probe_autoaim(p);
    pol += probe_rigid_bodies(p);
    pol += probe_lasers(p, home, home_yaw);
    pol += probe_minigun(p, home, home_yaw);
    pol += probe_water(p);
    if (!pol.empty()) out += " polimento:" + pol;
    if (g_anim_aim_guards != 0) out += fmt(" guardas_mira=%u", g_anim_aim_guards);
    const uint32_t guards = g_security_stats.guards + g_searchlight_stats.guards + g_gunimp_stats.guards +
                            g_space_stats.guards;
    if (guards != 0) out += fmt(" guardas_seguranca=%u", guards);
    if (g_traverse_stats.guards != 0) out += fmt(" guardas_travessia=%u", g_traverse_stats.guards);
    return out;
}

}  // namespace campaign
