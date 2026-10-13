// Regressão do carregador original portado: carrega cada fase com ResetMap_Load, como o
// GameFlow_Main faz no estado GF_LOAD_MAP, e compara o resultado (células, portais, objetos
// dinâmicos) com contagens tiradas dos mesmos dados pelo leitor independente de src/data.
// Não entra no ctest porque precisa dos arquivos do usuário.
//
// Uso: nf_loader_check [pasta com ACTION.ELF e FILES.BIN] [hash da fase]

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "campaign_audit.h"
#include "sights_measure.h"
#include "platform/arena.h"
#include "data/elf.h"
#include "platform/sfx_iop.h"
#include "platform/crash_report.h"
#include "platform/crt_compat.h"
#include "data/files_bin.h"
#include "data/level_pak.h"
#include "data/map_item.h"
#include "data/pack_images.h"
#include "data/texture_pack.h"
#include "engine/sound.h"
#include "engine/ainetwork.h"
#include "engine/anim.h"
#include "engine/break.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/emitter.h"
#include "engine/explode.h"
#include "engine/game.h"
#include "engine/view.h"
#include "engine/weather.h"
#include "engine/gameflow.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/loader.h"
#include "engine/menu.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/player_stats.h"
#include "engine/vecmath.h"
#include "engine/weapons.h"
#include "engine/weapon_wheel.h"
#include "engine/code_resolve.h"
#include "engine/font.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/trigger.h"
#include "engine/pc_text.h"
#include "engine/txt.h"
#include "platform/psi_2d.h"
#include "game/menu_tables.h"
#include "game/hud_tables.h"
#include "game/level_scene.h"
#include "game/static_init.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_file.h"
#include "platform/psi_gfx.h"
#include "platform/psi_shard.h"
#include "platform/fmath.h"

namespace {

struct Counts {
    uint32_t cels = 0;
    uint32_t portals = 0;
    uint32_t dynamic = 0;        // chamadas a parsemap_create_dynamic_objects
    uint32_t entities = 0;       // blocos de parâmetros (celglist_tag criados)
    uint32_t one_past_end = 0;   // posicionamentos estáticos com índice == MapEntityCount
};

struct Placed {
    uint16_t entity;
    uint32_t hash;
    uint32_t flags;
};

bool has_file(const nf::FilesBin& files, const std::string& name) {
    for (const nf::FileEntry& e : files.entries()) {
        if (nf::str_icmp(e.name.c_str(), name.c_str()) == 0) return true;
    }
    return false;
}

// O que o carregador deve produzir, contado direto dos blocos, na ordem de carga.
Counts expected(const nf::FilesBin& files, const std::vector<std::string>& packs) {
    Counts c;
    std::set<uint32_t> registered;
    // Objetos dinâmicos: os itens que não são a fase principal os criam no fim da própria leitura;
    // ResetMap_Load cria os do último item lido (DynamicPtr), que é a fase principal.
    std::vector<Placed> last_dynamic;
    uint32_t last_count = 0;
    const auto count_dynamic = [&](const std::vector<Placed>& list, uint32_t entity_count) {
        for (const Placed& p : list) {
            const bool resolved = p.hash == 0xffffffff ? p.entity < entity_count : registered.count(p.hash) != 0;
            if ((p.flags & 0xa000) == 0 && resolved) ++c.dynamic;
        }
    };
    for (const std::string& pack_name : packs) {
        const nf::Bytes pak = files.load(pack_name);
        for (const nf::PakItem& item : nf::parse_pak(pak)) {
            if (!item.is_map()) continue;
            const nf::ByteView data = item.view(pak);
            uint32_t entity_count = 0;
            std::vector<Placed> dynamic;
            for (const nf::Block& b : nf::read_blocks(data)) {
                const nf::ByteView p = b.payload(data);
                if (b.id == nf::block_id::kMapHeader) entity_count = nf::u32(p, 4);
                if (b.id == nf::block_id::kEntityParams || b.id == nf::block_id::kEntityParamsAlt) {
                    ++c.entities;
                    const uint32_t hash = nf::u32(p, 0);
                    if (hash != 0xffffffff) registered.insert(hash);
                }
                if (b.id == 0x21) c.portals += 2 * nf::u32(p, 0);
                if (b.id == 0x1a) {
                    size_t pos = 0;
                    while (pos + 0x4c <= p.size()) {
                        const uint16_t entity = nf::u16(p, pos);
                        const uint32_t hash = nf::u32(p, pos + 4);
                        const uint32_t flags = nf::u32(p, pos + 8);
                        dynamic.push_back({entity, hash, flags});
                        const bool resolved = hash == 0xffffffff ? entity <= entity_count : true;
                        if ((flags & 0x8000) && resolved) {
                            ++c.cels;
                            if (hash == 0xffffffff && entity == entity_count) ++c.one_past_end;
                        }
                        pos += 0x4c + size_t(nf::u32(p, pos + 0x48)) * 8;
                    }
                }
                if (b.id == nf::block_id::kEnd) break;
            }
            if (item.type != nf::ItemType::MapMain) count_dynamic(dynamic, entity_count);
            last_dynamic = std::move(dynamic);
            last_count = entity_count;
        }
    }
    count_dynamic(last_dynamic, last_count);
    return c;
}

// Como o GameFlow_Main: ResetMap_Load a cada tick em GF_LOAD_MAP; no fim ele troca o estado por
// GF_LOCKS_INIT, que chama Locks_Init e sai da pilha.
int load_level(uint32_t pack) {
    GameState.load_file = pack;
    GameState.difficulty = 2;  // a de bootup_bootup (decide quais personagens a fase cria)
    DrawInfo[3] = 1;           // as dicas ligadas, como bootup_bootup (Hint_Create)
    // O menu e a página que o bootup_bootup deixa (o menu principal os cria ao carregar 07000048).
    GameState.menu_id = 0x80000002;
    GameState.menu_page = 0x40000034;
    GameFlow_PushState(GF_LOAD_MAP, 0.0f, 0);
    int ticks = 0;
    while (GameFlow_GetState() == GF_LOAD_MAP && ticks < 100) {
        ResetMap_Load();
        ++ticks;
    }
    if (GameFlow_GetState() == GF_LOCKS_INIT) {
        Locks_Init();
        GameFlow_PopState();
    }
    return ticks;
}

// Dois segundos de jogo depois da carga (Game_Run e Game_Draw a cada tick, como no estado de jogo),
// com todas as portas acionadas no começo: pega falhas da lógica portada nos dados reais. Devolve
// quantas portas terminaram abertas; *moved recebe quanto o jogador (parado, sem entrada) se mexeu.
int run_game(int ticks, float* moved) {
    // Sem ponto de partida não há jogador para ligar a câmera 0.
    if (glb_players[0] == nullptr) Camera_Enable(0, 1, 1, nullptr);
    const _VECTOR start = glb_players[0] != nullptr ? glb_players[0]->pos : _VECTOR{};
    alignas(16) static obj_tag stand_in;
    for (int t = 0; t < ticks; ++t) {
        if (t == 1) {
            for (LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
                obj_tag* door = reinterpret_cast<DOORINFO*>(n)->obj;
                std::memset(&stand_in, 0, sizeof(stand_in));
                stand_in.sphere = {door->sphere.x, door->sphere.y, door->sphere.z + 2.0f, 0.5f};
                stand_in.matrix.m[3][0] = stand_in.sphere.x;
                stand_in.matrix.m[3][1] = stand_in.sphere.y;
                stand_in.matrix.m[3][2] = stand_in.sphere.z;
                Door_Activate(door, &stand_in, 0);
            }
        }
        ++GameState.frame;
        ++VBlankCount;
        Game_Run();
        Game_Draw();
    }
    *moved = glb_players[0] != nullptr ? Vec_Dist3D(&glb_players[0]->pos, &start) : 0.0f;
    int open = 0;
    for (LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
        if (reinterpret_cast<DOORINFO*>(n)->obj->state == kDoorOpen) ++open;
    }
    return open;
}

// Destruição: quebráveis (tipo 0x20; vidro = tipo 2, com os triângulos de psiCreateBreakableData),
// destrutíveis (0x21) e zonas que machucam (0x27) da fase; depois, com a granada de mão de weapon_data
// (pavio, raio e dano da explosão; a primeira com a flag 0x200 e raio sem ser de luz nem de fumaça),
// Explode_Create como Bullet_DoTrails faria: uma no centro do primeiro quebrável (de vidro, se houver)
// e outra 2 m à frente dos olhos do jogador, e 2 s de jogo (Game_Run + Game_Draw), contando os
// estilhaços criados e o máximo desenhado num quadro (psiDrawShard). vidro_alvo: um ponto a 2,5 m do
// primeiro vidro, numa sala, olhando para ele (para nightfire.exe --at).
struct DestructionStats {
    int breakables = 0, glass = 0, glass_tris = 0, destroyables = 0, hurts = 0;
    uint32_t broken = 0, shards = 0, drawn = 0, explosions = 0, hit = 0, smashed = 0;
    bool target = false;
    _VECTOR aim{};
    float grenade_radius = 0.0f, grenade_damage = 0.0f;
    int grenade_item = -1;  // primeiro item (tipo 0) com uma arma de pavio e explosão
    _VECTOR grenade_at{};
};
DestructionStats destruction_test(int ticks) {
    DestructionStats st;
    obj_tag* first = nullptr;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == kPickupObjectType && st.grenade_item < 0) {
            const PICKUPINFO* info = pickup_of(o);
            if (info->kind == kPickupWeapon && info->item < 115) {
                const weapon_definition_tag* w = weapon_def(info->item);
                if ((w->hit_flags & 0x200) != 0 && (w->hit_flags & 0x22000) == 0 && w->blast_radius > 0.0f) {
                    st.grenade_item = info->item;
                    st.grenade_at = o->sphere;
                }
            }
        }
        if (o->type == 0x21) ++st.destroyables;
        if (o->type == 0x27) ++st.hurts;
        if (o->type != 0x20 || (o->life & 1) != 0) continue;
        ++st.breakables;
        const BREAK_tag* b = static_cast<const BREAK_tag*>(o->data);
        if (b->kind == 2) {
            ++st.glass;
            const TRILIST_tag* list = psiGetTriList(o);
            if (list != nullptr) st.glass_tris += list->tri_count;
            if (list != nullptr && list->tri_count > 0 && (first == nullptr || static_cast<const BREAK_tag*>(first->data)->kind != 2)) {
                first = o;
                // Normal do primeiro triângulo, girada pela matriz do objeto; o lado com sala.
                const SHATTER_VERT* v = list->verts;
                const BREAKTRI_tag& t = list->tris[0];
                const _VECTOR& a = v[t.vert[0]].pos;
                const _VECTOR& b2 = v[t.vert[1]].pos;
                const _VECTOR& c = v[t.vert[2]].pos;
                const float u[3] = {b2.x - a.x, b2.y - a.y, b2.z - a.z}, w[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
                float n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                float nw[3];
                for (int k = 0; k < 3; ++k) nw[k] = n[0] * o->matrix.m[0][k] + n[1] * o->matrix.m[1][k] + n[2] * o->matrix.m[2][k];
                const float len = std::sqrt(nw[0] * nw[0] + nw[2] * nw[2]);
                if (len > 1e-6f) {
                    for (float side : {1.0f, -1.0f}) {
                        const float dx = nw[0] / len * side, dz = nw[2] / len * side;
                        _VECTOR p{o->sphere.x + dx * 2.5f, o->sphere.y, o->sphere.z + dz * 2.5f, 0.0f};
                        if (build_FindCel(&p, glb_world) == nullptr) continue;
                        st.target = true;
                        st.aim = {p.x, p.y, p.z, nf::fm::atan2(-dx, -dz)};
                        break;
                    }
                }
            }
        } else if (first == nullptr) {
            first = o;
        }
    }
    const weapon_definition_tag* grenade = nullptr;
    for (int i = 0; i < 115 && grenade == nullptr; ++i) {
        const weapon_definition_tag* w = weapon_def(i);
        if ((w->hit_flags & 0x200) != 0 && (w->hit_flags & 0x22000) == 0 && w->blast_radius > 0.0f && w->damage > 0.0f) {
            grenade = w;
        }
    }
    st.grenade_radius = grenade != nullptr ? grenade->blast_radius : 5.0f;
    st.grenade_damage = grenade != nullptr ? grenade->damage : 100.0f;
    const BreakStats before = g_break_stats;
    const ExplodeStats boom = g_explode_stats;
    obj_tag* p = glb_players[0];
    if (first != nullptr && first->cel != nullptr) {
        Explode_Create(first, &first->sphere, &CONSTANT_UP1, st.grenade_radius, st.grenade_radius, 0x6000052,
                       st.grenade_damage, 200, 200, 0, p, 0);
    }
    if (p != nullptr && p->cel != nullptr && glb_viewer[0] != nullptr) {
        _VECTOR head;
        Player_GetHeadPos(&head, p, nullptr);
        const MATRIX& cam = glb_viewer[0]->camera;
        _VECTOR at{head.x + cam.m[2][0] * 2.0f, head.y + cam.m[2][1] * 2.0f, head.z + cam.m[2][2] * 2.0f, 0.0f};
        Explode_Create(p, &at, &CONSTANT_UP1, st.grenade_radius, st.grenade_radius, 0x6000052, st.grenade_damage, 200,
                       200, 0, nullptr, 0);
    }
    for (int t = 0; t < ticks; ++t) {
        ++GameState.frame;
        ++VBlankCount;
        Game_Run();
        Game_Draw();
        st.drawn = std::max<uint32_t>(st.drawn, uint32_t(g_psi_frame.tri_vertices.size() / 3));
    }
    st.broken = g_break_stats.broken - before.broken;
    st.shards = g_break_stats.shards - before.shards;
    st.smashed = g_break_stats.smashed - before.smashed;
    st.explosions = g_explode_stats.created - boom.created;
    st.hit = g_explode_stats.hit_objects - boom.hit_objects;
    return st;
}

// Balas: "count" tiros em leque saindo dos olhos do jogador (ou da câmera 0, sem jogador) na
// direção do olhar, com Bullet_init e a definição real da arma "weapon" (weapon_data, copiado do
// ELF na partida). Depois "ticks" de jogo; conta as que pararam num acerto (estado 2) e as que
// sumiram sem acertar (alcance ou fora das salas).
struct ShotStats {
    int created = 0;
    int hit = 0;
    int gone = 0;
};
ShotStats fire_bullets(int weapon, int count, int ticks) {
    ShotStats st;
    viewer_tag* v = glb_viewer[0];
    if (v == nullptr) return st;
    weapon_definition_tag* def = weapon_def(weapon);
    obj_tag* p = glb_players[0];
    _VECTOR eye{v->camera.m[3][0], v->camera.m[3][1], v->camera.m[3][2], 1.0f};
    if (p != nullptr) Player_GetHeadPos(&eye, p, nullptr);
    const _VECTOR fwd{v->camera.m[2][0], v->camera.m[2][1], v->camera.m[2][2], 0.0f};
    struct Shot {
        obj_tag* obj;
        bool done;
    };
    std::vector<Shot> shots;
    for (int i = 0; i < count; ++i) {
        const float yaw = (float(i) - float(count - 1) * 0.5f) * 0.1f;
        const float c = nf::fm::cos(yaw), s = nf::fm::sin(yaw);
        _VECTOR dir{fwd.x * c + fwd.z * s, fwd.y - ((i & 1) != 0 ? 0.15f : 0.0f), fwd.z * c - fwd.x * s, 0.0f};
        _VECTOR from = eye;
        // Conta o disparo nas estatísticas como Player_WeaponInitBullet.
        if ((def->stats & 0xf8) != 0) PlrStat_LogShotFired(0);
        obj_tag* b = Bullet_init(0, p, p, def, &from, &dir, 0xffff);
        if (b != nullptr) shots.push_back({b, false});
    }
    st.created = int(shots.size());
    for (int t = 0; t < ticks; ++t) {
        ++GameState.frame;
        ++VBlankCount;
        Game_Run();
        Game_Draw();
        for (Shot& sh : shots) {
            if (sh.done) continue;
            bool alive = false;
            for (const obj_tag* o = DynamicObjList.next; o != nullptr && !alive; o = o->next) alive = o == sh.obj;
            if (!alive) {
                ++st.gone;
                sh.done = true;
            } else if (sh.obj->type == 5 && sh.obj->state == 2) {
                ++st.hit;
                sh.done = true;
            }
        }
    }
    return st;
}

// Tiros da IA: depois do jogo, cada inimigo (lado 1) vivo com uma arma de fogo na mão (com projéteis
// em weapon_data) atira uma rajada no jogador pelas funções do original. Preparação: ligado se
// esperava um canal (NDrone2_Enable), posto no estado quieto WaitForever (para que nenhum outro
// estado troque a animação nem cancele o tiro), com a visão ligada (flags 0x10000, como os estados de
// guarda), o jogador de oponente (NDrone2_SetOpponent), a animação de mirar (DroneAnim_CallAnim 10,
// como o estado AimStandFire) e virando para ele a cada tick (DroneWeap_AimTarget_IsOpponent). Depois
// de "wait" ticks (a pontaria só acerta quem viu o alvo por 2 s, DroneWeap_DoBulletAccuracy),
// DroneWeap_Fire com one_burst. O único atalho é a visão no tiro: lost_ticks volta a 0 a cada tick,
// como se todos vissem o jogador (os que não o veem de fato erram de propósito, aim_error). Roda até
// todas as rajadas acabarem (no máximo "ticks") e conta: quem chegou a mirar (Drone_AnimInfo kind 2
// ou 4), quem tinha linha de visão da boca da arma até a cabeça do jogador, quem o via havia 2 s, as
// balas (pelo pente: um disparo por bala gasta, vezes os projéteis da arma), as rajadas completas e
// os acertos no jogador (a lista de acertos dele a cada tick).
std::map<std::string, int> g_ai_shot_stubs;  // stubs chamados por fire_from_drones (todas as fases)
struct AiShotStats {
    int shooters = -1;  // -1: sem jogador
    int aiming = 0;
    int clear = 0;
    int seeing = 0;
    int bullets = 0;
    int bursts = 0;
    int hits = 0;
};
AiShotStats fire_from_drones(int wait, int ticks) {
    AiShotStats st;
    // O teste não muda as fases seguintes nem os resumos do fim: voltam como estavam o sorteio
    // (Rand_*), GameState, os canais de switch (a carga de uma fase não os zera, e os alarmes da IA
    // ficariam ligados na fase seguinte), as variáveis da música e das falas da IA, e as contagens de
    // stubs, de estados sem porte e de guardas. Os stubs chamados aqui vão para g_ai_shot_stubs.
    struct Restore {
        uint32_t x = SEED_X, y = SEED_Y;
        GAMESTATE_tag state = GameState;
        uint8_t music[sizeof(MusicVars)];
        std::vector<DroneSfxInfo> sfx{Drone_SfxInfo, Drone_SfxInfo + kDroneSfxCount};
        std::vector<uint8_t> channels{switch_channels, switch_channels + 256};
        std::vector<uint8_t> channels_prev{switch_channels_prev, switch_channels_prev + 256};
        std::vector<uint8_t> channels_hold{switch_channels_hold, switch_channels_hold + 256};
        std::vector<uint32_t> channels_time{switch_channels_time, switch_channels_time + 256};
        std::vector<uint16_t> channels_ticks{switch_channels_MusicVars, switch_channels_MusicVars + 256};
        std::map<std::string, int> stubs = pending_counts();
        std::vector<uint32_t> states{g_drone_state_pending, g_drone_state_pending + kDroneStateCount};
        uint32_t guards = g_drone_guards, vision_guards = g_drone_vision_guards;
        uint32_t combat_guards = g_drone_combat_guards;
        Restore() { std::memcpy(music, MusicVars, sizeof(music)); }
        ~Restore() {
            SEED_X = x;
            SEED_Y = y;
            GameState = state;
            std::memcpy(MusicVars, music, sizeof(music));
            std::copy(sfx.begin(), sfx.end(), Drone_SfxInfo);
            std::copy(channels.begin(), channels.end(), switch_channels);
            std::copy(channels_prev.begin(), channels_prev.end(), switch_channels_prev);
            std::copy(channels_hold.begin(), channels_hold.end(), switch_channels_hold);
            std::copy(channels_time.begin(), channels_time.end(), switch_channels_time);
            std::copy(channels_ticks.begin(), channels_ticks.end(), switch_channels_MusicVars);
            for (const auto& [name, n] : pending_counts()) {
                const auto it = stubs.find(name);
                const int before = it == stubs.end() ? 0 : it->second;
                if (n != before) g_ai_shot_stubs[name] += n - before;
            }
            pending_restore(stubs);
            std::copy(states.begin(), states.end(), g_drone_state_pending);
            g_drone_guards = guards;
            g_drone_vision_guards = vision_guards;
            g_drone_combat_guards = combat_guards;
        }
    } restore;
    obj_tag* p = glb_players[0];
    if (p == nullptr) return st;
    st.shooters = 0;
    uint32_t quiet = 0;
    for (int i = 0; i < kDroneStateCount; ++i) {
        if (NDrone2_StateFuncs[i] == &NDrone2_DSTATE_WaitForever) quiet = uint32_t(i);
    }
    struct Shooter {
        Drone_tag* d;
        uint16_t clip;
        uint8_t bullets;
        bool aimed;
    };
    std::vector<Shooter> shooters;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        obj_tag* obj = d->obj;
        const ANIMOBJ_tag* anim = anim_of(obj);
        if (d->side != 1 || anim == nullptr || d->health <= 0.0f || (d->flags & 0x600) != 0 || (obj->life & 1) != 0) {
            continue;
        }
        if (NDrone2_ObjectIsArmed(obj) == 0 || weapon_def(anim->weapon)->bullets == 0) continue;
        if ((d->flags & 0x100) == 0) NDrone2_Enable(1, d);
        if (quiet != 0) Drone_SM_SetState(&d->sm, quiet, 0);
        shooters.push_back({d, 0, weapon_def(anim->weapon)->bullets, false});
    }
    st.shooters = int(shooters.size());
    if (shooters.empty()) return st;
    const auto tick = [&shooters](bool firing) {
        for (Shooter& s : shooters) {
            DCVars_tag dcv{};
            Drone_DCVfromOBJ(s.d->obj, &dcv);
            DroneWeap_AimTarget_IsOpponent(0, 0, &dcv);
            if (firing) s.d->lost_ticks = 0;
        }
        ++GameState.frame;
        ++VBlankCount;
        Game_Run();
        Game_Draw();
    };
    // Um tick para a troca de estado acontecer (ela desliga o tiro).
    ++GameState.frame;
    ++VBlankCount;
    Game_Run();
    Game_Draw();
    for (Shooter& s : shooters) {
        DCVars_tag dcv{};
        Drone_DCVfromOBJ(s.d->obj, &dcv);
        s.d->flags |= 0x10000;
        NDrone2_SetOpponent(s.d, p);
        DroneAnim_CallAnim(0.0f, 10, 0, 0, 0, 0, &dcv);
    }
    for (int t = 0; t < wait; ++t) tick(false);
    for (Shooter& s : shooters) {
        DCVars_tag dcv{};
        Drone_DCVfromOBJ(s.d->obj, &dcv);
        DroneWeap_Fire(1, &dcv);
        s.d->one_burst = 1;
        s.clip = s.d->clip;
        if (DroneFiring.new_sighting_time_to_hit * FRAME_RATE <= float(s.d->seen_ticks)) ++st.seeing;
        int32_t bone = 0;
        uint32_t flash = 0;
        uint16_t kind = 0;
        _VECTOR muzzle, dir, head;
        Player_GetHeadPos(&head, p, nullptr);
        if (DroneWeap_GetWeaponInfo(s.d, &bone, &flash, &kind, &muzzle, &dir) != 0 &&
            Collide_LineOfSight(&muzzle, &head, s.d->obj->cel, s.d->obj, p, 0xa27)) {
            ++st.clear;
        }
    }
    for (int t = 0; t < ticks; ++t) {
        tick(true);
        for (Shooter& s : shooters) s.aimed = s.aimed || s.d->anim_kind == 2 || s.d->anim_kind == 4;
        for (HITDATA_tag* h = p->hits; h != nullptr; h = h->next) {
            if (h->obj == nullptr || h->obj->type != 5) continue;
            const obj_tag* from = bullet_of(h->obj)->from;
            for (const Shooter& s : shooters) st.hits += from == s.d->obj;
        }
        bool busy = false;
        for (const Shooter& s : shooters) busy = busy || s.d->firing != 0;
        if (!busy) break;
    }
    for (const Shooter& s : shooters) {
        st.aiming += s.aimed;
        st.bullets += int(uint16_t(s.clip - s.d->clip)) * s.bullets;
        st.bursts += s.d->firing == 0 && s.d->burst_over != 0;
    }
    return st;
}

// Logo depois da carga, usar cada porta destrancada e fechada como o jogador faria (Player_Activate,
// o caminho da ação de usar): ele é posto de pé 1,3 m à frente dela (pela linha 2 da matriz da
// porta), olhando para ela, com a câmera atualizada; no fim volta ao ponto de partida. Devolve
// quantas abriram; *tried recebe quantas foram tentadas e *first, a posição e o giro do jogador na
// primeira que abriu (para nightfire.exe --at).
int use_doors(int* tried, _VECTOR* first) {
    *tried = 0;
    obj_tag* p = glb_players[0];
    if (p == nullptr || glb_viewer[0] == nullptr) return 0;
    int opened = 0;
    for (LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
        DOORINFO* d = reinterpret_cast<DOORINFO*>(n);
        obj_tag* door = d->obj;
        if ((d->lock_channel != 0 && switch_channels[d->lock_channel] == 0) || d->no_touch != 0 || d->one_way != 0 ||
            door->state != kDoorClosed) {
            continue;
        }
        _VECTOR fwd;
        Mat_GetDir(&fwd, &door->matrix);
        _VECTOR pos{door->sphere.x + fwd.x * 1.3f, door->sphere.y, door->sphere.z + fwd.z * 1.3f, 1.0f};
        _VECTOR rot{0.0f, nf::fm::atan2(-fwd.x, -fwd.z), 0.0f, 0.0f};
        Player_StandAtNewPosition(p, &pos, &rot, 0);
        Control_BuildWorldSph(p);
        Camera_Update(0);
        ++*tried;
        if (Player_Activate(p, bloke_of(p)) != 0 && opened++ == 0) {
            *first = pos;
            first->w = rot.y;
        }
    }
    if (*tried != 0) {
        _VECTOR pos = g_player_init_call.pos;
        _VECTOR rot = g_player_init_call.rot;
        Player_StandAtNewPosition(p, &pos, &rot, p->unknown_f6);
        Camera_Update(0);
    }
    return opened;
}

// Itens pegáveis (objetos 0x2f) vivos e à vista.
std::vector<obj_tag*> live_pickups() {
    std::vector<obj_tag*> out;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == kPickupObjectType && (o->life & 1) == 0) out.push_back(o);
    }
    return out;
}

// O que o jogador carrega: armas, munição guardada mais pentes, colete, chaves e visibilidade.
struct Inventory {
    int weapons = 0, rounds = 0, keys = 0;
    float armour = 0.0f, visibility = 0.0f;
    bool operator!=(const Inventory& o) const {
        return weapons != o.weapons || rounds != o.rounds || keys != o.keys || armour != o.armour ||
               visibility != o.visibility;
    }
};
Inventory inventory_of(const BLData* b) {
    Inventory inv;
    for (const PLRWEAPON_tag& w : b->weapons) {
        inv.weapons += w.have != 0;
        inv.rounds += w.rounds;
    }
    for (int16_t a : b->ammo) inv.rounds += a;
    for (uint8_t k : b->unknown_70c) inv.keys += k;
    inv.armour = b->armour;
    inv.visibility = b->visibility;
    return inv;
}

// Logo depois da carga: o jogador é posto de pé em cada item à vista (no x, z do centro da esfera) e
// o tick do item (Pickup_Update) roda, o caminho do jogo para pegar. Conta os que ele pegou (o item
// some ou espera reaparecer) e os que mudaram o inventário, por tipo. No fim o jogador volta ao ponto
// de partida com o inventário de antes (os itens pegos continuam pegos). first recebe o centro do
// primeiro item de arma ou munição pego (para nightfire.exe --at), o tipo e o item.
struct PickupStats {
    int created = 0, tried = 0, taken = 0, changed = 0;
    std::map<int, int> kinds, taken_kinds;
    _VECTOR first{};
    int first_kind = -1, first_item = -1;
};
PickupStats collect_pickups() {
    PickupStats st;
    const std::vector<obj_tag*> items = live_pickups();
    st.created = int(items.size());
    for (obj_tag* o : items) ++st.kinds[pickup_of(o)->kind];
    obj_tag* p = glb_players[0];
    if (p == nullptr || glb_viewer[0] == nullptr) return st;
    BLData* b = bloke_of(p);
    alignas(16) static BLData saved;
    saved = *b;
    ANIMOBJ_tag* anim = anim_of(p);
    const int8_t next_weapon = anim != nullptr ? anim->next_weapon : 0;
    for (obj_tag* o : items) {
        PICKUPINFO* info = pickup_of(o);
        if ((o->flags & 0x10) != 0 || info->state == kPickupRespawning) continue;
        _VECTOR at{o->sphere.x, o->sphere.y, o->sphere.z, 1.0f};
        _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
        Player_StandAtNewPosition(p, &at, &rot, 0);
        Control_BuildWorldSph(p);
        const Inventory before = inventory_of(b);
        ++st.tried;
        Pickup_Update(o);
        const bool changed = inventory_of(b) != before;
        const bool taken = (o->life & 1) != 0 || info->state == kPickupRespawning || changed;
        st.changed += changed;
        if (!taken) continue;
        ++st.taken;
        ++st.taken_kinds[info->kind];
        if (st.first_kind < 0 && info->kind <= kPickupAmmo) {
            st.first = o->sphere;
            st.first_kind = info->kind;
            st.first_item = info->item;
        }
    }
    *b = saved;
    if (anim != nullptr) anim->next_weapon = next_weapon;
    _VECTOR pos = g_player_init_call.pos;
    _VECTOR rot = g_player_init_call.rot;
    Player_StandAtNewPosition(p, &pos, &rot, p->unknown_f6);
    Camera_Update(0);
    return st;
}

// No fim da fase, um inimigo armado (com o modelo de item da arma carregado) morre como num tiro fatal
// (vida 0 e o estado de morte de NDrone2_HitDamage) e o jogo roda até 4 s: a arma deve cair como item
// (DroneWeap_DropWeapon -> Pickup_CreateSimple) e pousar no chão.
struct DropStats {
    int weapon = -1;  // -1: nenhum inimigo armado com item
    int dropped = 0, resting = 0, ticks = 0;
    float fall = 0.0f;  // quanto o item desceu desde a mão
};
DropStats kill_armed_drone() {
    DropStats st;
    Drone_tag* victim = nullptr;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr && victim == nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        const ANIMOBJ_tag* anim = anim_of(d->obj);
        if (d->side != 1 || anim == nullptr || anim->weapon < 1 || d->health <= 0.0f || (d->flags & 0x600) != 0 ||
            (d->obj->life & 1) != 0) {
            continue;
        }
        uint32_t item;
        std::memcpy(&item, weapon_data + anim->weapon * kWeaponStride + 0x80, 4);
        if (item != 0 && hashtable_hashcode_to_celglist(item) != nullptr) victim = d;
    }
    if (victim == nullptr) return st;
    st.weapon = anim_of(victim->obj)->weapon;
    const std::vector<obj_tag*> before = live_pickups();
    if ((victim->flags & 0x100) == 0) NDrone2_Enable(1, victim);
    victim->health = 0.0f;
    Drone_SM_SetState(&victim->sm, victim->type != 0x1e ? 0x42u : 0xf2u, 0);
    std::vector<std::pair<obj_tag*, float>> dropped;
    for (st.ticks = 0; st.ticks < 240; ++st.ticks) {
        ++GameState.frame;
        ++VBlankCount;
        Game_Run();
        Game_Draw();
        for (obj_tag* o : live_pickups()) {
            if (std::find(before.begin(), before.end(), o) != before.end()) continue;
            bool known = false;
            for (const auto& [k, y] : dropped) known = known || k == o;
            if (!known) dropped.push_back({o, o->matrix.m[3][1]});
        }
        bool settled = !dropped.empty();
        for (const auto& [o, y] : dropped) settled = settled && pickup_of(o)->state != kPickupFalling;
        if (settled) break;
    }
    st.dropped = int(dropped.size());
    for (const auto& [o, y] : dropped) {
        if (pickup_of(o)->state == kPickupResting) ++st.resting;
        st.fall = std::max(st.fall, y - o->matrix.m[3][1]);
    }
    return st;
}

// Quantas texturas da fase (primeiro quadro de cada uma) têm substituta no pacote HD.
int count_hd(const nf::TexturePack& pack, int* total) {
    int found = 0;
    *total = 0;
    for (int32_t i = 0; i < NumPS2TexturesLoaded; ++i) {
        const PS2TEX_tag& t = Tex[i];
        if (t.frame != 0 || t.rec == nullptr || t.rec->pixels == nullptr) continue;
        ++*total;
        if (!pack.find(t.width, t.height, t.colors == 0x100, t.rec->pixels, t.rec->palette).empty()) ++found;
    }
    return found;
}

// Malhas com skinning decodificadas durante a carga (backend de psi_gfx que só decodifica, como o
// RuntimeGfx do nightfire.exe faz antes de criar a malha na GPU).
struct SkinCounts {
    int total = 0;          // blocos 0x2d com a estrutura de skinning
    int ok = 0;             // decodificados com triângulos e sem lotes descartados
    int failed = 0;
    size_t vertices = 0;
    size_t triangles = 0;
    int max_bones = 0;      // maior paleta pedida (maior osso da lista + 1)
    int weight_out = 0;     // pesos fora de [0, 1]
};
SkinCounts g_skin;

// Rostos: as malhas com skinning da fase com o bloco 0x2B (os alvos de morph), decodificadas como no RuntimeGfx
// (nf::decode_morph e a conferência de que os deltas de cada vértice cabem no bloco). Distintas em todas as fases,
// pelo hash da entidade; as peles com morphs (ANIMSKIN_tag::n_morphs) pelo hash da pele.
struct FaceCounts {
    int meshes = 0;              // malhas da fase com os alvos decodificados
    int failed = 0;              // bloco que não se decodifica ou vértice com deltas fora dele
    uint32_t max_targets = 0;
    size_t morph_vertices = 0;   // vértices das malhas com índice de morph
    size_t header_vertices = 0;  // os que leem o cabeçalho (m < primeiro elemento: valem 0)
};
FaceCounts g_face;
std::set<uint32_t> g_face_meshes_all, g_face_skins_all;
FaceCounts g_face_all;  // somados uma vez por malha distinta

// Estado do GS escrito pelas cadeias (pares A+D): partes de malha por {ALPHA_1, TEST_1, ZBUF_1, ABE},
// em todas as malhas de todas as fases (rígidas e com skinning).
struct GsCounts {
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, int>, int> parts;
    int meshes = 0;
    int multi = 0;      // malhas com mais de um estado
    uint32_t inherited = 0;  // lotes sem os três registradores escritos pela própria cadeia
    // Entradas da VU1: partes com a rotina do mapa de ambiente (0x2e0) e malhas que as têm; vértices e
    // os que têm r, g ou b acima de 0x80 (mais claros que a textura) e o maior alfa.
    int env_parts = 0, env_meshes = 0;
    uint64_t vertices = 0, overbright = 0;
    int max_alpha = 0;
};
GsCounts g_gs;

void count_colour(const uint8_t* c) {
    ++g_gs.vertices;
    if (c[0] > 0x80 || c[1] > 0x80 || c[2] > 0x80) ++g_gs.overbright;
    g_gs.max_alpha = std::max(g_gs.max_alpha, int(c[3]));
}

void count_gs(const nf::EntityMesh& mesh) {
    if (mesh.empty()) return;
    ++g_gs.meshes;
    g_gs.inherited += mesh.inherited_gs;
    std::set<std::tuple<uint64_t, uint64_t, uint64_t, int>> states;
    bool env = false;
    for (const nf::Vertex& v : mesh.vertices) count_colour(v.color);
    for (const nf::SkinnedVertex& v : mesh.skin_vertices) count_colour(v.color);
    for (const nf::SubMesh& sm : mesh.submeshes) {
        g_gs.env_parts += sm.env ? 1 : 0;
        env = env || sm.env;
        const auto key = std::make_tuple((sm.gs.has & nf::GsState::kAlpha) ? sm.gs.alpha : ~0ull,
                                         (sm.gs.has & nf::GsState::kTest) ? sm.gs.test : ~0ull,
                                         (sm.gs.has & nf::GsState::kZbuf) ? sm.gs.zbuf : ~0ull, int(sm.gs.abe));
        ++g_gs.parts[key];
        states.insert(key);
    }
    if (states.size() > 1) ++g_gs.multi;
    g_gs.env_meshes += env ? 1 : 0;
}

int decode_entity(void*, const celglist_tag* glist, const map_tag* map) {
    const uint8_t* blk = glist->gfx->ps2_gfx;
    uint32_t header;
    std::memcpy(&header, blk, 4);
    const nf::ByteView block(blk, header & 0xffffff);
    const auto frames_of = [map](int tex) {
        if (map == nullptr || tex < 0 || uint32_t(tex) >= map->bmp_count) return 1;
        return std::max(1, int(map->bmp[tex].frames & 0xff));
    };
    if (!nf::gfx_is_skinned(block)) {
        try {
            const nf::EntityMesh mesh = nf::decode_gfx(block, frames_of);
            count_gs(mesh);
            sights::note_mesh(glist, mesh);
        } catch (const nf::FormatError&) {
        }
        return -1;
    }
    ++g_skin.total;
    try {
        const nf::EntityMesh mesh = nf::decode_gfx(block, frames_of);
        if (mesh.empty() || mesh.bad_batches != 0 || mesh.skin_vertices.empty()) {
            ++g_skin.failed;
            return -1;
        }
        ++g_skin.ok;
        sights::note_mesh(glist, mesh);
        g_skin.vertices += mesh.skin_vertices.size();
        g_skin.triangles += mesh.indices.size() / 3;
        g_skin.max_bones = std::max(g_skin.max_bones, mesh.bone_count);
        count_gs(mesh);
        for (const nf::SkinnedVertex& v : mesh.skin_vertices) {
            if (!(v.weight >= 0.0f && v.weight <= 1.0f)) ++g_skin.weight_out;
        }
        if (glist->gfx->morph != nullptr) {
            uint32_t mh;
            std::memcpy(&mh, glist->gfx->morph - 4, 4);
            const uint32_t hash = hashtable_celglist_to_hashcode(glist);
            const bool first = g_face_meshes_all.insert(hash).second;
            try {
                if ((mh >> 24) != 0x2b || (mh & 0xffffff) < 4) throw nf::FormatError("cabeçalho do bloco de morph");
                const nf::MorphTargets m = nf::decode_morph(nf::ByteView(nf::raw(glist->gfx->morph), (mh & 0xffffff) - 4));
                size_t with = 0, header = 0;
                for (const nf::SkinnedVertex& v : mesh.skin_vertices) {
                    if (v.morph < 0) continue;
                    if (size_t(v.morph) + m.targets > m.elements()) throw nf::FormatError("deltas fora do bloco");
                    ++with;
                    if (uint32_t(v.morph) < m.first) ++header;
                }
                ++g_face.meshes;
                g_face.max_targets = std::max(g_face.max_targets, m.targets);
                g_face.morph_vertices += with;
                g_face.header_vertices += header;
                if (first) {
                    ++g_face_all.meshes;
                    g_face_all.max_targets = std::max(g_face_all.max_targets, m.targets);
                    g_face_all.morph_vertices += with;
                    g_face_all.header_vertices += header;
                }
            } catch (const nf::FormatError&) {
                ++g_face.failed;
                if (first) ++g_face_all.failed;
            }
        }
    } catch (const nf::FormatError&) {
        ++g_skin.failed;
    }
    return -1;
}

// Nomes das funções de estado da IA, para o relatório dos não portados.
std::vector<std::string> g_drone_state_names;

// Construtores estáticos do ACTION.ELF (no PS2, o __main): o que cada um escreve e as tabelas que
// o port copia (weapon_data, ammo_data). Falha alto se o interpretador parar.
bool check_static_constructors(const nf::DiscSource& disc) {
    nf::Bytes elf_bytes;
    if (!disc.read("ACTION.ELF", elf_bytes)) {
        std::printf("não li ACTION.ELF de %s\n", disc.describe().c_str());
        return false;
    }
    const nf::Elf elf(std::move(elf_bytes));
    // Nomes das funções de estado da IA (NDrone2_StateFuncs), para o relatório dos não portados.
    if (const auto table = elf.find("NDrone2_StateFuncs")) {
        const nf::ByteView v = elf.at(table->value, kDroneStateCount * 4);
        for (int i = 0; i < kDroneStateCount; ++i) {
            uint32_t a;
            std::memcpy(&a, v.data() + i * 4, 4);
            const nf::ElfSymbol* s = elf.symbol_at(a, 2);
            std::string name = s != nullptr ? s->name : "?";
            if (const size_t cut = name.find("__"); cut != std::string::npos) name.resize(cut);
            g_drone_state_names.push_back(name);
        }
    }
    for (const nf::ConstructorEffect& e : nf::survey_static_constructors(elf)) {
        std::printf("construtor %08x %s:", e.address, e.name.c_str());
        for (const auto& w : e.writes) std::printf(" %s=%u", w.symbol.c_str(), w.bytes);
        if (!e.error.empty()) std::printf(" ERRO %s", e.error.c_str());
        std::printf("\n");
    }
    std::string error;
    std::vector<std::string> log;
    if (!nf::init_game_tables(elf, &Engine_ResolveCode, &error, &log)) {
        std::printf("ERRO nos construtores estáticos: %s\n", error.c_str());
        return false;
    }
    if (!nf::init_hud_tables(elf, &Engine_ResolveCode, &error, &log)) {
        std::printf("ERRO nas tabelas do HUD: %s\n", error.c_str());
        return false;
    }
    if (!nf::init_menu_tables(elf, &error, &log)) {
        std::printf("ERRO nas tabelas do menu: %s\n", error.c_str());
        return false;
    }
    for (const std::string& line : log) std::printf("  %s\n", line.c_str());
    {
        // Os códigos de "Secret Unlocks": o Menu_SpecialCodenameCheck portado contra o do ELF, código a código.
        std::string diff;
        const int same = nf::check_special_codenames(elf, &diff);
        int codes = 0;
        while (Menu_PortSecretCode(uint32_t(codes)) != nullptr) ++codes;
        std::printf("codinomes especiais: %d de %d conferem com o ELF%s%s\n", same, 3 * (codes + 2),
                    diff.empty() ? "" : " diferenças:", diff.c_str());
    }
    // Fatos independentes: cada entrada guarda o próprio índice em +0 (weapon_definition_tag::id),
    // as armas com modelo têm uma pele 05xxxxxx em +0xdc e o tipo de animação (+0x84) vai de 0 a 6.
    int weapons = 0, wrong = 0, ammo = 0;
    for (uint32_t w = 0; w < kWeaponDataCount; ++w) {
        const uint8_t* d = weapon_data + w * kWeaponStride;
        int16_t id;
        uint32_t skin;
        std::memcpy(&id, d, 2);
        std::memcpy(&skin, d + 0xdc, 4);
        if (skin != 0) ++weapons;
        if (id != int16_t(w) || (skin != 0 && (skin >> 24) != 0x05) || d[0x84] > 6) ++wrong;
    }
    for (uint32_t a = 0; a < kAmmoDataCount; ++a) {
        int16_t max;
        std::memcpy(&max, ammo_data + a * kAmmoStride + 2, 2);
        if (max != 0) ++ammo;
    }
    std::printf("weapon_data: %d armas (pele 05xxxxxx em +0xdc) em %u entradas; ammo_data: %d tipos com máximo\n",
                weapons, kWeaponDataCount, ammo);
    // A arma das balas de teste (fire_bullets) e quantas definições têm a função de impacto.
    const weapon_definition_tag* pistol = weapon_def(2);
    int on_hit = 0;
    for (uint32_t i = 0; i < kWeaponDataCount; ++i) on_hit += weapon_def(int32_t(i))->on_hit != nullptr;
    std::printf("arma 2: velocidade=%g alcance=%g dano=%g dispersão=%g modelo=%08x flags=%x/%x/%x; on_hit em %d\n",
                pistol->speed, pistol->range, pistol->damage, pistol->spread, pistol->projectile, pistol->flags,
                pistol->flags2, pistol->hit_flags, on_hit);
    // As armas das mãos com laser e relógio: a mira laser (flags 8, Player_LaserPointer), o raio laser (flags2
    // 0x100, Draw_LaserBeam), o do taser (flags2 0x80, Draw_TaserBeam), a pele do relógio (0x500004b,
    // Player_SetWatch) e a mira automática de cada uma (weapon_definition_tag::autoaim, Check_AutoAim).
    {
        std::string ptr, beam, taser, watch, aa;
        for (uint32_t i = 0; i < kWeaponDataCount; ++i) {
            const weapon_definition_tag* w = weapon_def(int32_t(i));
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%x,", i);
            if ((w->flags & 8) != 0) ptr += hex;
            if ((w->flags2 & 0x100) != 0) beam += hex;
            if ((w->flags2 & 0x80) != 0) taser += hex;
            if (w->skin == 0x500004b) watch += hex;
            if (w->autoaim != 0.0f && i < 0x30) {
                char t[24];
                std::snprintf(t, sizeof(t), "%x:%g,", i, w->autoaim);
                aa += t;
            }
        }
        std::printf("armas: mira_laser=%s raio_laser=%s taser=%s relogio=%s mira_auto=%s\n", ptr.c_str(), beam.c_str(),
                    taser.c_str(), watch.c_str(), aa.c_str());
    }
    if (wrong != 0) {
        std::printf("ERRO weapon_data: %d entradas sem o próprio índice em +0, com pele inválida ou tipo > 6\n", wrong);
        return false;
    }
    // A pontuação (ScoringTable): fatos independentes das tabelas copiadas. Cada missão aponta as 9 linhas dela,
    // na ordem das entradas; os quatro limites de medalha crescem; a pé (07xxxxxx) é o BaseMap de uma fase da
    // MissionData; as linhas de tempo e de precisão têm alvo. As missões da MissionData sem pontuação vão no fim.
    {
        int on_foot = 0, driving = 0, ordered = 0, rising = 0, known = 0, timed = 0;
        std::string missing;
        for (uint32_t i = 0; i < kScoringTableCount; ++i) {
            const SCORETABLE& e = ScoringTable[i];
            if ((e.level >> 24) == 0x09) ++driving;
            if ((e.level >> 24) == 0x07) ++on_foot;
            if (e.rows == ScoringRows[i].row) ++ordered;
            if (e.medal[0] < e.medal[1] && e.medal[1] < e.medal[2] && e.medal[2] < e.medal[3]) ++rising;
            for (const MISSIONDATA_tag& m : MissionData) known += (e.level >> 24) == 0x07 && m.level == e.level && m.base_map == e.level;
            if (e.rows != nullptr && e.rows[kScoreTime].target != 0 && e.rows[kScoreAccuracy].target == 100) ++timed;
        }
        std::set<uint32_t> bases;
        for (const MISSIONDATA_tag& m : MissionData) {
            if (m.base_map != 0 && PlrStats_GetLevelTotals(m.base_map) == nullptr) bases.insert(m.base_map);
        }
        for (const uint32_t b : bases) {
            char h[16];
            std::snprintf(h, sizeof(h), "%08x,", b);
            missing += h;
        }
        std::printf("pontuação: %u missões (a pé %d, direção %d), linhas na ordem %d, medalhas crescentes %d, a pé na "
                    "MissionData %d, tempo e precisão %d, bônus por dificuldade %g/%g/%g, missões sem pontuação [%s]\n",
                    kScoringTableCount, on_foot, driving, ordered, rising, known, timed, ScoringTableDifMul[0],
                    ScoringTableDifMul[1], ScoringTableDifMul[2], missing.c_str());
        if (ordered != int(kScoringTableCount) || rising != int(kScoringTableCount)) {
            std::printf("ERRO ScoringTable: linhas fora da ordem ou limites de medalha que não crescem\n");
            return false;
        }
    }
    return true;
}

// As estatísticas da fase depois dos testes de jogo (PlrStat_Describe) e a pontuação que a missão daria se fosse
// cumprida agora: PlrStat_GetScore(1) no estado 6, com a entrada, as linhas e o estado restaurados depois.
void stats_report() {
    char line[512];
    PlrStat_Describe(line, sizeof(line));
    std::printf(" estatisticas=[%s]", line);
    SCORETABLE* e = PlrStats_GetLevelTotals(Mission_BaseMapHCode());
    if (e == nullptr || e->rows == nullptr) return;
    const SCORETABLE keep = *e;
    SCORETABLE_ROW rows[kScoreRows];
    std::memcpy(rows, e->rows, sizeof(rows));
    const int32_t status = Mission_Status();
    const uint8_t better = DoneBetter;
    Mission_SetStatus(kMissionComplete);
    if (PlrStat_GetScore(1) != nullptr) {
        const uint32_t total = e->score + e->bonus;
        std::printf(" pontuacao_se_cumprida=[pontos %u bonus %u total %u medalha %u linhas", e->score, e->bonus, total,
                    PlrStat_MedalFor(e, total));
        for (uint32_t r = 0; r < kScoreRows; ++r) std::printf(" %u:%.0f", e->rows[r].actual, double(e->rows[r].earned));
        std::printf("]");
    }
    Mission_SetStatus(uint32_t(status));
    std::memcpy(e->rows, rows, sizeof(rows));
    *e = keep;
    DoneBetter = better;
}

Counts actual() {
    Counts c;
    c.dynamic = g_dynamic_object_calls;
    for (cel_tag* cel = glb_world->first; cel != nullptr; cel = cel->next) {
        ++c.cels;
        for (portal_tag* p = cel->portals; p != nullptr; p = p->next) ++c.portals;
    }
    return c;
}

// Tamanho da rede, e uma rota de teste: do nó da rede de navegação mais perto do ponto de partida do
// jogador até o nó mais longe (2D) entre os ligados a ele (pelas ligações), pelo A* (sem atalho em
// linha reta). "ok" quando a rota é exata, os nós seguidos têm ligação e o que falta andar
// (AINetwork_GetRouteDistance) não é menor que a distância em linha reta. Também um emissor no nó de
// partida (nós alcançados até 255 m).
void ai_report() {
    uint32_t door_nodes = 0, kick_nodes = 0;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        const AIPath_tag& p = AINetwork.paths[i];
        for (uint32_t k = 0; k < p.node_count; ++k) {
            door_nodes += (p.map_nodes[k].flags & kNodeDoor) != 0;
            kick_nodes += (p.map_nodes[k].flags & kNodeKick) != 0;
        }
    }
    // Coberturas e pontos de IA cujo emissor achou um caminho (Drone_PostLoad_Init emite todos).
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < NPCGlobals.cover_count; ++i) emitted += NPCGlobals.cover_nodes[i].emitter.path != nullptr;
    for (uint32_t i = 0; i < NPCGlobals.ai_point_count; ++i) emitted += NPCGlobals.ai_points[i].emitter.path != nullptr;
    uint32_t doors_found = 0, door_links = 0;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        const AIPath_tag& p = AINetwork.paths[i];
        for (uint32_t k = 0; k < p.node_count; ++k) doors_found += (p.map_nodes[k].flags & kNodeDoor) != 0;
        for (uint32_t k = 0; k < p.link_count; ++k) door_links += (p.links[k].flags & kLinkDoor) != 0;
    }
    uint32_t nav = 0, nodes = 0, links = 0, in_room = 0, segs = 0, passable = 0, bound_nodes = 0, bound_in_room = 0;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        const AIPath_tag& p = AINetwork.paths[i];
        nodes += p.node_count;
        links += p.link_count;
        if ((p.flags & 0xd) == 0) nav += p.node_count;
        for (uint32_t k = 0; k < p.node_count; ++k) in_room += p.map_nodes[k].cel != nullptr;
    }
    for (uint32_t i = 0; i < AINetwork.bounds_count; ++i) {
        const AIBounds_tag& b = AINetwork.bounds[i];
        segs += b.link_count;
        bound_nodes += b.node_count;
        for (uint32_t k = 0; k < b.link_count; ++k) passable += (b.links[k].flags & kBoundPassable) != 0;
        for (uint32_t k = 0; k < b.node_count; ++k) bound_in_room += b.map_nodes[k].cel != nullptr;
    }
    std::printf(" ia: caminhos=%u nós=%u/%u em_sala=%u ligações=%u limites=%u segmentos=%u passáveis=%u "
                "nós_limite_em_sala=%u/%u cobertura=%u pontos=%u emitidos=%u volumes=%u portas=%u/%u "
                "ligações_porta=%u chutes=%u",
                AINetwork.path_count, nav, nodes, in_room, links, AINetwork.bounds_count, segs, passable,
                bound_in_room, bound_nodes, NPCGlobals.cover_count, NPCGlobals.ai_point_count, emitted,
                AIBoxList.count, doors_found, door_nodes, door_links, kick_nodes);
    const AINetworkGuards& g = g_ainetwork_guards;
    if (g.paths_full || g.bounds_full || g.bounds_no_cel || g.covers_full || g.cover_table || g.points_full ||
        g.volumes_skipped) {
        std::printf(" guardas_ia: caminhos=%u limites=%u sem_sala=%u coberturas=%u tabela=%u pontos=%u volumes=%u",
                    g.paths_full, g.bounds_full, g.bounds_no_cel, g.covers_full, g.cover_table, g.points_full,
                    g.volumes_skipped);
    }
    if (!g_player_init_call.called) {
        std::printf(" rota_teste=-");
        return;
    }
    // Nó de navegação mais perto (3D) do ponto de partida; o "drone" fica de pé nele.
    AIPath_tag* path = nullptr;
    int16_t start = -1;
    float best = 1e30f;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& p = AINetwork.paths[i];
        if ((p.flags & 0xd) != 0) continue;
        for (uint32_t k = 0; k < p.node_count; ++k) {
            const float d = Vec_Dist3D(&p.map_nodes[k].pos, &g_player_init_call.pos);
            if (p.map_nodes[k].cel != nullptr && d < best) {
                best = d;
                path = &p;
                start = int16_t(k);
            }
        }
    }
    if (start < 0) {
        std::printf(" rota_teste=sem_nó");
        return;
    }
    alignas(16) static obj_tag owner;
    std::memset(&owner, 0, sizeof(owner));
    owner.pos = path->map_nodes[start].pos;
    owner.cel = path->map_nodes[start].cel;
    CelPos_tag from{owner.pos, owner.cel, {}};
    const int16_t nearest = NDrone2_NearestNode(&from, path, &owner);
    // Nós ligados ao de partida (busca em largura pelas ligações; entre dois nós de porta, só com a
    // porta destrancada, como no A*).
    std::vector<int> seen(path->node_count, 0);
    std::vector<int> queue{start};
    seen[start] = 1;
    for (size_t q = 0; q < queue.size(); ++q) {
        const NODE_tag& n = path->nodes[queue[q]];
        for (int s = 0; s < n.link_count; ++s) {
            const LINK_tag& l = path->links[path->node_links[n.first_link + s]];
            const int other = l.a == uint16_t(n.node->index) ? l.b : l.a;
            const bool locked = n.door != nullptr && (n.node->flags & kNodeDoor) != 0 &&
                                (path->map_nodes[other].flags & kNodeDoor) != 0 && Door_IsLocked(n.door);
            if (!seen[other] && !locked) {
                seen[other] = 1;
                queue.push_back(other);
            }
        }
    }
    int far = start;
    float far_d = -1.0f;
    for (int k : queue) {
        const float d = Vec_Dist2D(&path->map_nodes[k].pos, &owner.pos);
        if (d > far_d) {
            far_d = d;
            far = k;
        }
    }
    CelPos_tag dest{path->map_nodes[far].pos, path->map_nodes[far].cel, {}};
    std::vector<uint16_t> list(AINetwork.max_nodes + 1);
    AIRoute_tag route{};
    route.list = list.data();
    route.path = path;
    route.flags = kRouteNoDirect | 1;
    AITarget_tag goal{};
    AIPoint_tag point{};
    const uint32_t r = AINetwork_CalcRouteToPosition(&owner, &dest, &route, &goal, &point, 0.0f, 0);
    bool chained = route.count > 0;
    for (int i = 1; i < route.count; ++i) {
        const NODE_tag& n = path->nodes[list[i - 1]];
        bool found = false;
        for (int s = 0; s < n.link_count && !found; ++s) {
            const LINK_tag& l = path->links[path->node_links[n.first_link + s]];
            found = l.a == list[i] || l.b == list[i];
        }
        chained = chained && found;
    }
    const float straight = Vec_Dist2D(&owner.pos, &dest.pos);
    route.current = -1;
    const float left = r < kRouteDirect ? AINetwork_GetRouteDistance(&route) : 0.0f;
    const bool ok = r == kRouteExact && chained && left + 0.01f >= straight && nearest == start;
    std::printf(" rota_teste=%s(estado=%u,nós=%u/%zu,dist=%.1f,reta=%.1f)", ok ? "ok" : "falhou", r, route.count,
                queue.size(), left, straight);
    AIEmitter_tag em{};
    uint32_t reached = 0;
    if (AINetwork_InitEmitter(nullptr, &owner.pos, owner.cel, &em, path, 1) != 0 && AINetwork_EmitPath(&em, 0.0f) != 0) {
        for (uint32_t k = 0; k < em.size; ++k) reached += em.dist[k] != 0xff;
    }
    std::printf(" emissor=%u/%u", reached, path->node_count);
    AINetwork_FreeEmitter(&em);
}

// Os menus da fase (o arquivo tipo 8 carregado): o que ficou aberto depois dos testes de jogo (o fim de
// missão de uma missão fracassada; menu_close_game_menus o fecha e despausa antes dos outros testes), a
// pausa aberta como no Player_Update e fechada pelo Start (MenuManager_Monitor) com o jogo despausando, e
// o menu de fim de missão aberto e fechado. O teste roda por último e devolve o sorteio (Rand_*),
// GameState e as contagens de stubs, para não mudar os outros resumos.
struct MenuCheck {
    uint32_t file = 0;           // o item tipo 8 da fase (0: nenhum)
    uint32_t open_in_game = 0;   // status do menu aberto pelos testes de jogo (4: fim de missão)
    uint32_t pause_pages = 0, pause_controls = 0;
    bool paused = false, closed = false, unpaused = false;
    uint32_t pause_focus = 0;
    uint32_t end_pages = 0, end_controls = 0;
    uint32_t end_rows = 0;
    bool end_closed = false;
    uint32_t front_page = 0, front_pages = 0;  // o menu principal (status 5): página atual e páginas
};

void menu_close_game_menus(MenuCheck* c) {
    c->file = g_menu_file;
    c->open_in_game = MenuManager_GetStatus();
    if (c->open_in_game == 5) {
        // O pacote do menu principal (07000048): o ResetMap_Load criou o menu inteiro; sem pausa nem fim.
        c->front_page = Menu_CurrentPage(0);
        c->front_pages = menu_manager[0].pages.count;
    }
    for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
    GS_PauseGame(0);
}

void menu_check(MenuCheck* out) {
    MenuCheck& c = *out;
    if (file_start == nullptr || c.open_in_game == 5) return;
    for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);  // um aberto pelos testes seguintes
    GS_PauseGame(0);
    const uint32_t seed_x = SEED_X, seed_y = SEED_Y;
    const GAMESTATE_tag state = GameState;
    const std::map<std::string, int> stubs = pending_counts();
    MenuStats before = g_menu_stats;
    MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
    GS_PauseGame(1);
    c.pause_pages = g_menu_stats.pages - before.pages;
    c.pause_controls = g_menu_stats.controls - before.controls;
    for (int t = 0; t < 30; ++t) Game_Run();
    c.paused = GS_IsPaused(-1) && Menu_CurrentPage(0) == 0x4000004b;
    uint32_t tag = 0;
    Menu_Focus(0, &c.pause_focus, &tag);
    // Start (0x1e) apertado: o MenuManager_Monitor fecha o menu e o MenuManager_Delete despausa.
    PlayerSetting[0].actions[0x1e] = 1.0f;
    PlayerSetting[0].state[0x1e] = kActionDown | kActionPressed;
    MenuManager_Monitor();
    c.closed = MenuManager_Exists(0) == 0;
    c.unpaused = !GS_IsPaused(-1);
    PlayerSetting[0].actions[0x1e] = 0.0f;
    PlayerSetting[0].state[0x1e] = 0;
    before = g_menu_stats;
    MenuManager_Create(0x80000004u, 0x40000042, 0, 0, 0x28, 0, 4);
    c.end_pages = g_menu_stats.pages - before.pages;
    c.end_controls = g_menu_stats.controls - before.controls;
    for (int t = 0; t < 10; ++t) MenuManager_Update();
    c.end_rows = uint32_t(__Menu_Send(0, 0x100001a4, 0x33, 0, 0));
    MenuManager_Delete(0);
    c.end_closed = MenuManager_Exists(0) == 0;
    GS_PauseGame(0);
    SEED_X = seed_x;
    SEED_Y = seed_y;
    GameState = state;
    pending_restore(stubs);
}

// ---- geradores de personagens (DroneSpawner_*) ----

// Os geradores no mapa (objetos 0x30 com grupo e vagas; o 0x30 do "jogador falso" não tem grupo).
std::vector<obj_tag*> live_spawners() {
    std::vector<obj_tag*> out;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        const auto* s = static_cast<const DroneSpawner_tag*>(o->data);
        if (o->type == 0x30 && (o->life & 1) == 0 && s != nullptr && s->group != 0 && s->spawned != nullptr) {
            out.push_back(o);
        }
    }
    return out;
}

// Personagens vivos dos grupos com gerador: DroneSpawner_Init tirou do mapa todos os modelos desses
// grupos, então qualquer personagem com o grupo foi gerado (mesmo que o gerador já tenha sumido).
std::vector<Drone_tag*> spawned_alive(const std::set<uint32_t>& groups) {
    std::vector<Drone_tag*> out;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (groups.count(d->group_id) == 0 || (d->obj->life & 1) != 0) continue;
        if (d->health > 0.0f && (d->flags & 0x600) == 0) out.push_back(d);
    }
    return out;
}

std::map<std::string, int> g_spawner_run_stubs;  // stubs chamados por forced_spawners (todas as fases)

// Geradores forçados: liga o canal de começo de cada gerador que espera um, deixa o jogador invencível
// e roda `ticks` de jogo (Game_Run + Game_Draw), contando os gerados, os vivos no fim e as levas
// fechadas; depois mata todos os gerados vivos (vida 0 e o estado de morte, como kill_armed_drone: o
// corpo e a remoção são os da máquina de estados) e roda mais `after` ticks, contando os que nasceram
// no lugar, as vagas soltas por DroneSpawner_DroneDelete e os mortos que já foram apagados. Como
// fire_from_drones, devolve o estado global como estava (sorteio, GameState, canais, música, falas,
// trapaças, contagens de stubs e guardas); os stubs vão para g_spawner_run_stubs.
struct ForcedSpawnStats {
    int channels = 0, alive = 0, waves = 0, killed = 0, deleted = 0, left = 0;
    uint32_t spawned = 0, replaced = 0, released = 0;
};
ForcedSpawnStats forced_spawners(const std::set<uint32_t>& groups, int ticks, int after) {
    struct Restore {
        uint32_t x = SEED_X, y = SEED_Y;
        GAMESTATE_tag state = GameState;
        uint8_t music[sizeof(MusicVars)];
        uint8_t cheats[sizeof(CheatInfo)];
        std::vector<DroneSfxInfo> sfx{Drone_SfxInfo, Drone_SfxInfo + kDroneSfxCount};
        std::vector<uint8_t> channels{switch_channels, switch_channels + 256};
        std::vector<uint8_t> channels_prev{switch_channels_prev, switch_channels_prev + 256};
        std::vector<uint8_t> channels_hold{switch_channels_hold, switch_channels_hold + 256};
        std::vector<uint32_t> channels_time{switch_channels_time, switch_channels_time + 256};
        std::vector<uint16_t> channels_ticks{switch_channels_MusicVars, switch_channels_MusicVars + 256};
        std::map<std::string, int> stubs = pending_counts();
        std::vector<uint32_t> states{g_drone_state_pending, g_drone_state_pending + kDroneStateCount};
        uint32_t guards = g_drone_guards, vision_guards = g_drone_vision_guards;
        uint32_t combat_guards = g_drone_combat_guards, weapon_guards = g_drone_weapon_guards;
        Restore() {
            std::memcpy(music, MusicVars, sizeof(music));
            std::memcpy(cheats, CheatInfo, sizeof(cheats));
        }
        ~Restore() {
            SEED_X = x;
            SEED_Y = y;
            GameState = state;
            std::memcpy(MusicVars, music, sizeof(music));
            std::memcpy(CheatInfo, cheats, sizeof(cheats));
            std::copy(sfx.begin(), sfx.end(), Drone_SfxInfo);
            std::copy(channels.begin(), channels.end(), switch_channels);
            std::copy(channels_prev.begin(), channels_prev.end(), switch_channels_prev);
            std::copy(channels_hold.begin(), channels_hold.end(), switch_channels_hold);
            std::copy(channels_time.begin(), channels_time.end(), switch_channels_time);
            std::copy(channels_ticks.begin(), channels_ticks.end(), switch_channels_MusicVars);
            for (const auto& [name, n] : pending_counts()) {
                const auto it = stubs.find(name);
                const int before = it == stubs.end() ? 0 : it->second;
                if (n != before) g_spawner_run_stubs[name] += n - before;
            }
            pending_restore(stubs);
            std::copy(states.begin(), states.end(), g_drone_state_pending);
            g_drone_guards = guards;
            g_drone_vision_guards = vision_guards;
            g_drone_combat_guards = combat_guards;
            g_drone_weapon_guards = weapon_guards;
        }
    } restore;
    ForcedSpawnStats st;
    for (obj_tag* o : live_spawners()) {
        const auto* s = static_cast<const DroneSpawner_tag*>(o->data);
        if (s->running == 0 && s->channel_start != 0 && switch_channels[s->channel_start] == 0) {
            switch_channels[s->channel_start] = 1;
            ++st.channels;
        }
    }
    const uint32_t god = 1;
    std::memcpy(CheatInfo, &god, 4);
    if (glb_players[0] == nullptr) Camera_Enable(0, 1, 1, nullptr);
    const auto run = [](int n) {
        for (int t = 0; t < n; ++t) {
            ++GameState.frame;
            ++VBlankCount;
            Game_Run();
            Game_Draw();
            // O menu de fim de missão (uma missão fracassada pelas mortes, 07000011) pausa o jogo: fecha
            // e segue, como se o jogador escolhesse continuar.
            if (MenuManager_GetStatus() != 0) {
                for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
                GS_PauseGame(0);
            }
        }
    };
    const uint32_t before = g_drone_spawner_stats.spawned, waves_before = g_drone_spawner_stats.waves;
    run(ticks);
    st.spawned = g_drone_spawner_stats.spawned - before;
    st.waves = int(g_drone_spawner_stats.waves - waves_before);
    const std::vector<Drone_tag*> victims = spawned_alive(groups);
    st.alive = int(victims.size());
    for (Drone_tag* d : victims) {
        if ((d->flags & 0x100) == 0) NDrone2_Enable(1, d);
        d->health = 0.0f;
        Drone_SM_SetState(&d->sm, d->type != 0x1e ? 0x42u : 0xf2u, 0);
    }
    st.killed = int(victims.size());
    const uint32_t spawned_before = g_drone_spawner_stats.spawned, released_before = g_drone_spawner_stats.released;
    run(after);
    st.replaced = g_drone_spawner_stats.spawned - spawned_before;
    st.released = g_drone_spawner_stats.released - released_before;
    // Os mortos já apagados (Drone_Delete): os mortos dos grupos que sobram na lista são os que ainda não
    // foram (os que nasceram no lugar estão vivos; o jogador invencível não atira).
    int dead_left = 0;
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
        if (groups.count(d->group_id) != 0 && (d->health <= 0.0f || (d->flags & 0x600) != 0)) ++dead_left;
    }
    st.deleted = st.killed - dead_left;
    st.left = int(live_spawners().size());
    return st;
}

}  // namespace

int main_body(int argc, char** argv) {
    nf::install_crash_report();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argc > 1 ? argv[1] : ".";
    // Depois da pasta: uma fase só (opcional) e --campanha (a auditoria da campanha com a lista completa dos
    // provisórios e dos stubs de cada fase).
    std::string only;
    bool campaign_detail = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--campanha") {
            campaign_detail = true;
        } else if (a == "--miras") {
            // Só do port: as miras das armas na mão (tests/sights_measure.h), sem os testes de jogo.
            sights::enable();
        } else if (a == "--no-env") {
            // Só do port (depuração): sem o tempo e os enfeites (DrawSnow = 0, InitDrops vazia, os seus posicionamentos
            // provisórios) nem a tela de carga, como antes do porte deles: as provas sorteiam como no main de antes.
            DrawSnow = 0;
            g_port_no_env = 1;
            g_port_no_load_screen = 1;
        } else if (only.empty()) {
            only = a;
        }
    }
    const std::wstring wdir(dir.begin(), dir.end());
    // O disco do jogador: a pasta com os arquivos extraídos, uma imagem .iso, uma unidade (platform/disc_source.h).
    std::string disc_error;
    const std::shared_ptr<nf::DiscSource> disc = nf::open_disc(wdir, &disc_error);
    if (disc == nullptr) {
        std::printf("não abri os dados em %s: %s\n", dir.c_str(), disc_error.c_str());
        return 2;
    }
    nf::FilesBin files(disc);

    // FileList vem do ACTION.ELF, como no PS2.
    std::memset(FileList, 0, sizeof(FileList));
    for (size_t i = 0; i < files.entries().size() && i < size_t(kFileListCount); ++i) {
        const nf::FileEntry& e = files.entries()[i];
        std::memcpy(FileList[i].name, e.name.c_str(), std::min(e.name.size(), sizeof(FileList[i].name) - 1));
        FileList[i].offset = e.offset;
        FileList[i].size = e.size;
    }
    if (!psiFileSetup(disc->open("FILES.BIN"))) {
        std::printf("não abri FILES.BIN em %s\n", dir.c_str());
        return 2;
    }
    if (!check_static_constructors(*disc)) return 3;
    // O som: o driver do IOP lê os bancos da pasta PS2 (a mistura anda com os VBlanks e é descartada).
    nf::sfx_iop_set_disc(disc);
    // No lugar do menu (como nightfire.exe com --level): o idioma americano e as legendas ligadas. O idioma pedido
    // também, como no main do PS2: o menu principal (07000048) roda P_LANGUAGE_Handler, que carrega LanguageToLoad.
    SelLang = TXTLANG_USA;
    LanguageToLoad = TXTLANG_USA;
    DrawInfo[2] = 1;
    loader_debug_power_on();
    hashtable_debug_power_on();

    pending_reset();
    nf::TexturePack hd_pack;
    hd_pack.open(disc->folder() + L"\\textures\\SLUS-20579\\replacements");
    if (!hd_pack.empty()) std::printf("pacote HD: %zu arquivos\n", hd_pack.files());
    int levels = 0, failures = 0, hd_found = 0, hd_total = 0;
    g_psi_gfx_backend.create_entity = &decode_entity;
    SkinCounts skin_all;
    for (const std::string& level : nf::list_levels(files)) {
        if (!only.empty() && level != only) continue;
        const uint32_t pack = std::stoul(level, nullptr, 16);
        std::string anims = level;
        anims[2] = 'F';
        if (!has_file(files, anims + ".bin")) {
            // ResetMap_Load sempre carrega o 07F; sem ele, psiFileOpen manteria o cursor anterior.
            std::printf("%s ERRO sem pacote %s\n", level.c_str(), anims.c_str());
            ++failures;
            continue;
        }
        const Counts want = expected(files, {anims + ".bin", level + ".bin"});
        g_parsemap_guards = {};
        g_psi_gfx_guards = {};
        g_dynamic_object_calls = 0;
        g_skin = {};
        g_face = {};
        g_emitters_created = 0;
        g_ainetwork_guards = {};
        g_script_stats = ScriptStats{};
        g_drone_spawner_stats = {};
        // Os testes de jogo da fase anterior podem ter terminado a missão (Mission_Update empilha o fade
        // e, no lugar do menu, pede o recomeço quente da fase): cada fase começa limpa, numa carga fria.
        while (GameFlow_Depth() > 0) GameFlow_PopState();
        IsWarmReset = 0;
        const uint32_t restarts_before = g_mission_port.restarts;
        // As estatísticas de cada fase são só dela (no jogo, Mission_Init zera na primeira fase da missão).
        PlrStat_ResetForMission();
        campaign::begin_level();
        sights::begin_level();
        const int ticks = load_level(pack);
        if (sights::enabled()) {
            if (ticks == 2) sights::measure_level(pack);
            continue;
        }
        const bool campaign_level = campaign::is_campaign_level(pack);
        if (campaign_level) campaign::after_load(pack);
        const Counts got = actual();
        const MEMINFO_tag* info = Mem_Info();
        uint32_t used = 0;
        for (uint32_t u : info->used_by_type) used += u;

        const ParsemapGuards& g = g_parsemap_guards;
        const PsiGfxGuards& gg = g_psi_gfx_guards;
        const bool ok = ticks == 2 && GameFlow_Depth() == 0 && got.cels == want.cels &&
                        got.portals + 2 * g.portal_without_cel == want.portals && got.dynamic == want.dynamic &&
                        g.entity_without_gfx == 0 && g.cel_without_glist == 0 && g.param_key_out_of_range == 0 &&
                        g.block_id_out_of_range == 0 && gg.texture_without_palette == 0 && gg.bad_texture_ref == 0 &&
                        gg.table_full == 0 && g_skin.failed == 0 && g_face.failed == 0;
        std::printf("%s %s células=%u/%u portais=%u/%u objetos=%u/%u entidades=%u texturas=%d heap=%.1fMB",
                    level.c_str(), ok ? "ok  " : "ERRO", got.cels, want.cels, got.portals, want.portals, got.dynamic,
                    want.dynamic, want.entities, NumPS2TexturesLoaded, used / 1048576.0);
        // Malhas com skinning decodificadas (todas as do pacote, inclusive as do 07F).
        std::printf(" skin=%d/%d", g_skin.ok, g_skin.total);
        if (g_skin.failed) std::printf(" ERRO skin_falhas=%d", g_skin.failed);
        // Rostos: as peles carregadas com morphs e as malhas com os alvos (bloco 0x2B) decodificados.
        {
            const int n = hashtable_get_hashtype_count(0x5000000);
            std::vector<uint32_t> skins(size_t(std::max(n, 0)));
            if (n > 0) hashtable_get_hashtype_data(0x5000000, skins.data());
            int with_morphs = 0;
            for (const uint32_t h : skins) {
                const auto* sk = static_cast<const ANIMSKIN_tag*>(hashtable_getitem(h));
                if (sk == nullptr || sk->n_morphs == 0) continue;
                ++with_morphs;
                g_face_skins_all.insert(h);
            }
            std::printf(" rostos=%d/%d", g_face.meshes, with_morphs);
            if (g_face.failed) std::printf(" ERRO rostos_falhas=%d", g_face.failed);
        }
        skin_all.total += g_skin.total;
        skin_all.ok += g_skin.ok;
        skin_all.failed += g_skin.failed;
        skin_all.vertices += g_skin.vertices;
        skin_all.triangles += g_skin.triangles;
        skin_all.max_bones = std::max(skin_all.max_bones, g_skin.max_bones);
        skin_all.weight_out += g_skin.weight_out;
        // Células estáticas (tipos 0x8017-0x8019) que build_link_objects_to_rooms pôs numa sala.
        int props = 0, linked = 0, rooms = 0;
        for (const cel_tag* c = glb_world->first; c != nullptr; c = c->next) {
            const uint16_t t = uint16_t(c->type);
            if (t == 0xc023 || t == 0xc047) ++rooms;
            if (uint32_t(t) - 0x8017 >= 3) continue;
            ++props;
            if (c->parent != nullptr) ++linked;
        }
        std::printf(" salas=%d em_sala=%d/%d", rooms, linked, props);
        int listed = 0, sky = 0;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) ++listed;
        for (const SKYOBJ_tag& sk : GfxList.sky) sky += (sk.glist != nullptr) + (sk.glist_flash != nullptr);
        std::printf(" vivos=%d provisórios=%zu céu=%d", listed, g_provisional_objects.size(), sky);
        // Emissores de partículas: definições do bloco 0x30 (hashcodes 0x0c) e os criados na carga
        // (Emitter_CreatePlist).
        std::printf(" emissores=%u defs=%d", g_emitters_created, hashtable_get_hashtype_count(0x0c000000));
        // O tempo (engine/weather.h): o tipo das gotas (-1 nenhum), o máximo, as caixas de chuva, os raios (com par) e as
        // nuvens; os enfeites (engine/scenery.h) que a carga criou.
        {
            uint32_t boxes = 0;
            for (const obj_tag* b = RainBoxRoot; b != nullptr; b = b->owner) ++boxes;
            uint32_t bolts = 0, paired = 0, clouds = 0, ripples = 0, gens = 0, trees = 0, rotors = 0, sides = 0;
            for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
                if (o->type == 0x33) {
                    ++bolts;
                    paired += static_cast<const LIGHTNING_tag*>(o->data)->seg[0] != nullptr ? 1 : 0;
                }
                clouds += o->type == 0x24;
                ripples += o->type == 0x34;
                gens += o->type == 0x31;
                trees += o->type == 0x3f;
                rotors += o->type == 7;
                sides += o->type == 0x4c;
            }
            std::printf(" tempo=%d/%d caixas=%u raios=%u/%u nuvens=%u enfeites=ondulacoes:%u,folhas:%u,arvores:%u,"
                        "helices:%u,lados:%u",
                        DropType == 0xff ? -1 : int(DropType), int(CurrentMax), boxes, paired, bolts, clouds, ripples,
                        gens, trees, rotors, sides);
        }
        // Portas: quantas, quantas cobrem portais (flag 0x8000 de build_LinkDoors2Portals) e fechadas.
        int doors = 0, on_portal = 0, closed = 0;
        for (const LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
            const obj_tag* d = reinterpret_cast<const DOORINFO*>(n)->obj;
            ++doors;
            if ((d->flags & 0x8000) != 0) ++on_portal;
            if (Door_IsClosed(d)) ++closed;
        }
        std::printf(" portas=%d/%d/%d", doors, on_portal, closed);
        std::printf(" partidas=%u", player_start_positions_index);
        // Personagens da IA: quantos, quantos com a pele carregada e com um script tocando.
        int drones = 0, animated = 0, playing = 0;
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
            ++drones;
            if (anim_of(d->obj) != nullptr) ++animated;
            if (d->script != nullptr) ++playing;
        }
        std::printf(" inimigos=%d animados=%d scripts=%d", drones, animated, playing);
        // Grupos com gerador: os personagens deles viraram modelos (DroneSpawner_Init) e saíram do mapa.
        std::set<uint32_t> spawner_groups;
        for (obj_tag* o : live_spawners()) spawner_groups.insert(static_cast<const DroneSpawner_tag*>(o->data)->group);
        // O HUD do jogador (panes criadas por HUD_Init, as ligadas e os sprites substitutos enquanto o
        // módulo 2D é stub) e a missão da fase (Mission_Init: objetivos da missão e o estado).
        {
            const BLData* b = glb_players[0] != nullptr ? bloke_of(glb_players[0]) : nullptr;
            uint32_t on = 0;
            if (b != nullptr && b->hud != nullptr) {
                for (const HUDPANE_tag& pane : static_cast<const HUDINFO_tag*>(b->hud)->panes) on += pane.enabled;
            }
            const MissionSummary ms = Mission_Summary();
            std::printf(" hud: paineis=%u ligados=%u missao: objetivos=%d estado=%d base=%08x ordem=%d",
                        HUD_PaneCount(b), on, ms.total, Mission_Status(), BaseMap,
                        int32_t(ThisOrderNum));
        }
        // Scripts de cena: os itens tipo 7 do pacote da fase registrados como protótipo (SCRIPTDEF_tag
        // com o próprio hash) e os objetos que os tocam, criados na carga.
        const ScriptStats at_load = g_script_stats;
        {
            int items = 0, registered = 0;
            const nf::Bytes pak = files.load(level + ".bin");
            for (const nf::PakItem& item : nf::parse_pak(pak)) {
                if (item.type != nf::ItemType::Script) continue;
                ++items;
                const uint32_t hash = uint32_t(std::stoul(item.name, nullptr, 16));
                const auto* def = static_cast<const SCRIPTDEF_tag*>(hashtable_getitem(hash));
                if (def != nullptr && def != nf::p32_ptr<const SCRIPTDEF_tag>(0xffffffffu) &&
                    def->hash == hash) {
                    ++registered;
                }
            }
            std::printf(" cenas_fase=%d/%d sp=%u switches=%u fusiveis=%u", registered, items, at_load.players,
                        at_load.switches, at_load.fuseboxes);
            // Dicas (Hint_Create, tipo 0x43) criadas na carga, quantas têm som e onde está a primeira.
            int hints = 0, with_sound = 0;
            const obj_tag* first_hint = nullptr;
            for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
                if (o->type != 0x43) continue;
                if (first_hint == nullptr) first_hint = o;
                ++hints;
                if (static_cast<const HINT_tag*>(o->data)->sound != 0) ++with_sound;
            }
            std::printf(" dicas=%d com_som=%d", hints, with_sound);
            if (first_hint != nullptr) {
                const HINT_tag* h = static_cast<const HINT_tag*>(first_hint->data);
                std::printf(" dica=%.2f,%.2f,%.2f(som=0x%x,texto=0x%x)", first_hint->pos.x, first_hint->pos.y,
                            first_hint->pos.z, h->sound, h->label);
            }
        }
        if (g_player_init_call.called) {
            // Ponto escolhido por Player_Start e a direção para a frente (linha 2 de RotMatrix).
            MATRIX m;
            RotMatrix(&g_player_init_call.rot, &m);
            const _VECTOR& p = g_player_init_call.pos;
            std::printf(" jogador=%.2f,%.2f,%.2f frente=%.3f,%.3f,%.3f", p.x, p.y, p.z, m.m[2][0], m.m[2][1],
                        m.m[2][2]);
        }
        if (!hd_pack.empty()) {
            int total = 0;
            const int found = count_hd(hd_pack, &total);
            hd_found += found;
            hd_total += total;
            std::printf(" hd=%d/%d", found, total);
        }
        int tried = 0;
        _VECTOR first{};
        const int used_doors = use_doors(&tried, &first);
        if (glb_players[0] != nullptr) std::printf(" usar=%d/%d", used_doors, tried);
        if (used_doors != 0) std::printf(" porta_usada=%.2f,%.2f,%.2f,%.3f", first.x, first.y, first.z, first.w);
        // Itens: quantos a fase criou (por tipo, tipo:quantos) e quantos o jogador pegou ficando sobre eles.
        {
            const PickupStats ps = collect_pickups();
            std::printf(" itens: criados=%d tipos=", ps.created);
            for (const auto& [k, n] : ps.kinds) std::printf("%d:%d,", k, n);
            std::printf(" pegos=%d/%d inventário_mudou=%d", ps.taken, ps.tried, ps.changed);
            if (!ps.taken_kinds.empty()) {
                std::printf(" pegos_por_tipo=");
                for (const auto& [k, n] : ps.taken_kinds) std::printf("%d:%d,", k, n);
            }
            if (ps.first_kind >= 0) {
                std::printf(" item_pego=%.2f,%.2f,%.2f(tipo=%d,item=%x)", ps.first.x, ps.first.y, ps.first.z,
                            ps.first_kind, ps.first_item);
            }
        }
        // Personagens: a altura antes do jogo, para ver a gravidade e a colisão do corpo agirem.
        std::vector<std::pair<const Drone_tag*, float>> drone_y;
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
            drone_y.push_back({d, d->obj->pos.y});
        }
        float moved = 0.0f;
        g_text_stats = TextStats{};
        g_sound_stats = SoundStats{};
        nf::sfx_iop_reset_stats();
        campaign::before_game();
        const int opened = run_game(120, &moved);
        campaign::after_game();
        std::printf(" jogo: abertas=%d desenhos=%zu", opened, g_psi_frame.draws.size());
        // O som nos 2 s: os pedidos do jogo (Sound_Play/Play3D), os que o driver do IOP começou, as falas (streams) e
        // as legendas que elas deram, os recusados (limite de vozes, distância), o banco e as vozes da SPU agora.
        {
            const nf::SfxIopState st = nf::sfx_iop_state();
            std::printf(" sons: pedidos=%u tocados=%u falas=%u legendas=%u recusados=%u ausentes=%u banco=SB_%d vozes=%d",
                        g_sound_stats.requested, nf::g_sfx_iop_stats.started, nf::g_sfx_iop_stats.speech,
                        g_sound_stats.speech, nf::g_sfx_iop_stats.rejected, nf::g_sfx_iop_stats.not_found, st.bank_slot,
                        st.voices);
            // Os gatilhos de som (tipo 0x40) e de música (0x44) que existem depois dos 2 s, e a música do EE.
            int sound_triggers = 0, music_triggers = 0;
            for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
                sound_triggers += o->type == 0x40;
                music_triggers += o->type == 0x44;
            }
            const MusicState ms = music_state();
            std::printf(" gatilhos_som=%d gatilhos_musica=%d musica=0x%x/%d", sound_triggers, music_triggers,
                        uint32_t(ms.track), ms.last_jump);
            // A guarda de StartSample (amostra fora do banco do item: a fila de um item recusado), só quando disparou.
            if (nf::g_sfx_iop_stats.guards != 0) std::printf(" guardas_iop=%u", nf::g_sfx_iop_stats.guards);
        }
        // O que os scripts fizeram nos 2 s de jogo: instâncias, câmeras de cena, seções carregáveis (NIS)
        // e entidades criadas, e quantos script players ainda existem.
        {
            int players = 0;
            for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) players += o->type == 0x1b;
            std::printf(" cenas_jogo: instancias=%u cameras=%u secoes=%u entidades=%u sp_vivos=%d",
                        g_script_stats.instances - at_load.instances, g_script_stats.cameras - at_load.cameras,
                        g_script_stats.loadables - at_load.loadables, g_script_stats.entities - at_load.entities,
                        players);
        }
        // Camada 2D nos 2 s de jogo: o idioma (strings), as fontes com textura, as mensagens aceitas por
        // tipo (1 informação, 4/5 legendas, 6 itens), os sprites vivos e o que o último quadro desenhou.
        {
            int fonts = 0, textured = 0;
            for (const FONTTABLE_tag& t : FontTable) fonts += t.font != nullptr;
            for (const uint32_t t : FontList) textured += t != 0;
            int glyphs = 0;
            for (const Psi2DQuad& q : g_psi_frame.quads2d) glyphs += q.origin.kind == Psi2DKind::Glyph;
            std::printf(" 2d: idioma=%s textos=%u fontes=%d/%d mensagens=", FNames[SelLang], NumEntries, textured, fonts);
            for (int t = 1; t < kTextMsgTypes; ++t) {
                if (g_text_stats.added[t] != 0) std::printf("%d:%u,", t, g_text_stats.added[t]);
            }
            std::printf(" linhas=%u sprites=%u quadros2d=%zu glifos=%d", g_text_stats.lines, SpriteList.used.count,
                        g_psi_frame.quads2d.size(), glyphs);
            if (g_txt_guards.bad_table || g_font_guards.past_table || g_font_guards.no_glyph || g_font_guards.long_line ||
                g_sprite_guards.active_full || g_sprite_guards.zero_tile || g_sprite_guards.no_format) {
                std::printf(" guardas_2d: rotulo=%u glifo=%u tm=%u linha=%u ativos=%u ladrilho=%u formato=%u",
                            g_txt_guards.bad_table, g_font_guards.past_table, g_font_guards.no_glyph,
                            g_font_guards.long_line, g_sprite_guards.active_full, g_sprite_guards.zero_tile,
                            g_sprite_guards.no_format);
            }
        }
        // Estado de cada personagem depois do jogo (estado:quantos, em hexadecimal).
        {
            std::map<uint32_t, int> states;
            for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
                ++states[reinterpret_cast<const Drone_tag*>(n)->sm.state];
            }
            if (!states.empty()) {
                std::printf(" estados_ia=");
                for (const auto& [st, k] : states) std::printf("%x:%d,", st, k);
            }
        }
        if (!drone_y.empty()) {
            // Quantos desceram mais de 1 m (caíram do chão) e a maior variação de altura.
            int fell = 0;
            float change = 0.0f;
            for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
                const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
                for (const auto& [was, y] : drone_y) {
                    if (was != d) continue;
                    const float drop = y - d->obj->pos.y;
                    if (1.0f < drop) ++fell;
                    change = std::max(change, std::fabs(drop));
                }
            }
            std::printf(" ia_corpo: caíram=%d variação=%.2f", fell, change);
        }
        // Geradores (DroneSpawner_*): criados na carga, modelos tirados do mapa, personagens gerados nos 2 s
        // e os gerados vivos agora.
        if (g_drone_spawner_stats.created != 0) {
            std::printf(" geradores: criados=%u modelos=%u nascidos=%u vivos=%zu", g_drone_spawner_stats.created,
                        g_drone_spawner_stats.templates, g_drone_spawner_stats.spawned,
                        spawned_alive(spawner_groups).size());
        }
        // Partículas no último quadro desenhado (psiDrawParticleList) e emissores vivos.
        int emitters_alive = 0;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type == kEmitterObjectType) ++emitters_alive;
        }
        std::printf(" emissores_vivos=%d sprites=%zu/%zu", emitters_alive, g_psi_frame.particle_lists.size(),
                    g_psi_frame.particles.size());
        if (glb_players[0] != nullptr) {
            const obj_tag* p = glb_players[0];
            std::printf(" jogador_fim=%.2f,%.2f,%.2f andou=%.3f chão=%d", p->pos.x, p->pos.y, p->pos.z, moved,
                        (reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(p->sphere_src))->flags & 8) != 0 ? 1 : 0);
        }
        g_bullet_guards = {};
        // A pistola (arma 2): 8 tiros em leque e um segundo de jogo. em_inimigos: acertos que os
        // personagens receberam (Drone_tag::hits_taken).
        const auto drone_hits = [] {
            int sum = 0;
            for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
                sum += reinterpret_cast<const Drone_tag*>(n)->hits_taken;
            }
            return sum;
        };
        const int hits_before = drone_hits();
        const ShotStats shots = fire_bullets(2, 8, 60);
        std::printf(" balas: criadas=%d acertaram=%d sumiram=%d em_inimigos=%d", shots.created, shots.hit, shots.gone,
                    drone_hits() - hits_before);
        ai_report();
        const BulletGuards& bg = g_bullet_guards;
        if (bg.no_viewer || bg.zero_trail_period || bg.surface_out_of_table || bg.digit_out_of_table) {
            std::printf(" guardas_balas: câmera=%u rastro=%u material=%u dígito=%u", bg.no_viewer,
                        bg.zero_trail_period, bg.surface_out_of_table, bg.digit_out_of_table);
        }
        // Os inimigos armados atiram uma rajada no jogador (fire_from_drones: 2,5 s de mira, até 4 s de tiro).
        {
            const AiShotStats ai = fire_from_drones(150, 240);
            if (ai.shooters < 0) {
                std::printf(" tiros_ia: sem_jogador");
            } else {
                std::printf(" tiros_ia: atiradores=%d mirando=%d visada=%d vendo=%d balas=%d rajadas=%d "
                            "acertos_no_jogador=%d",
                            ai.shooters, ai.aiming, ai.clear, ai.seeing, ai.bullets, ai.bursts, ai.hits);
                // A vida do jogador depois dos tiros (Player_HandlePain) e se ele morreu (tipo 0x12).
                if (const obj_tag* p = glb_players[0]; p != nullptr) {
                    std::printf(" vida_jogador=%.1f morto=%d", static_cast<const BLData*>(p->data)->health,
                                p->type == 0x12 ? 1 : 0);
                    // O que o HUD mostra depois dos tiros.
                    char line[512];
                    HUD_Describe(static_cast<const BLData*>(p->data), line, sizeof(line));
                    std::printf(" hud_fim=[%s]", line);
                }
            }
        }
        // Um inimigo armado morre: a arma cai como item e pousa.
        {
            const DropStats ds = kill_armed_drone();
            if (ds.weapon >= 0) {
                std::printf(" morte_ia: arma=%x itens_soltos=%d pousados=%d queda=%.2f ticks=%d", ds.weapon, ds.dropped,
                            ds.resting, ds.fall, ds.ticks);
            }
        }
        if (g_pickup_guards != 0) std::printf(" guardas_itens=%u", g_pickup_guards);
        stats_report();
        if (g_player_stats_guards.bond_channel != 0 || g_player_stats_guards.bond_player != 0) {
            std::printf(" guardas_estatisticas: canal=%u jogador=%u", g_player_stats_guards.bond_channel,
                        g_player_stats_guards.bond_player);
        }
        // A missão depois de todos os testes de jogo da fase (os alarmes dos tiros podem fracassá-la) e
        // quantas vezes o substituto do menu de fim de missão pediu o recomeço.
        {
            char line[256];
            Mission_Describe(line, sizeof(line));
            std::printf(" missao_fim=[%s] recomecos=%u", line, g_mission_port.restarts - restarts_before);
        }
        // Os menus abertos pelos testes de jogo (o fim de missão pausa o jogo) fecham antes dos outros testes.
        MenuCheck mc;
        menu_close_game_menus(&mc);
        {
            const DestructionStats ds = destruction_test(120);
            std::printf(" destruicao: quebraveis=%d vidros=%d triangulos=%d destrutiveis=%d zonas=%d granada=%.1f/%.0f "
                        "explosoes=%u atingidos=%u quebrados=%u cacos=%u desenhados=%u destruidos=%u",
                        ds.breakables, ds.glass, ds.glass_tris, ds.destroyables, ds.hurts, ds.grenade_radius,
                        ds.grenade_damage, ds.explosions, ds.hit, ds.broken, ds.shards, ds.drawn, ds.smashed);
            if (ds.target) std::printf(" vidro_alvo=%.2f,%.2f,%.2f,%.3f", ds.aim.x, ds.aim.y, ds.aim.z, ds.aim.w);
            if (ds.grenade_item >= 0) {
                std::printf(" item_granada=%.2f,%.2f,%.2f(arma=%x)", ds.grenade_at.x, ds.grenade_at.y, ds.grenade_at.z,
                            ds.grenade_item);
            }
            const ShardGuards& sg = g_shard_guards;
            if (sg.unknown_code || sg.tri_overflow || sg.vert_overflow || g_break_guards.null_hit ||
                g_explode_guards.not_a_drone || g_explode_guards.hit_heap_empty) {
                std::printf(" guardas_destruicao: codigo=%u triangulos=%u vertices=%u sem_acerto=%u nao_drone=%u heap=%u",
                            sg.unknown_code, sg.tri_overflow, sg.vert_overflow, g_break_guards.null_hit,
                            g_explode_guards.not_a_drone, g_explode_guards.hit_heap_empty);
            }
        }
        // Geradores forçados: o canal de começo de cada um ligado, 10 s de jogo, todos os gerados mortos e
        // mais 15 s (o corpo fica 10 s antes de sumir).
        if (!spawner_groups.empty()) {
            const ForcedSpawnStats fs = forced_spawners(spawner_groups, 600, 900);
            std::printf(" geradores_forçados: canais=%d nascidos=%u vivos=%d levas=%d mortos=%d apagados=%d "
                        "repostos=%u vagas_soltas=%u restantes=%d",
                        fs.channels, fs.spawned, fs.alive, fs.waves, fs.killed, fs.deleted, fs.replaced, fs.released,
                        fs.left);
        }
        if (g_drone_spawner_stats.guards != 0) std::printf(" guardas_geradores=%u", g_drone_spawner_stats.guards);
        // Os menus de jogo (pausa e fim de missão) com o arquivo de menu da fase, por último.
        {
            menu_check(&mc);
            if (mc.file == 0) {
                std::printf(" menus: sem_arquivo");
            } else if (mc.open_in_game == 5) {
                std::printf(" menus: arquivo=%08x menu_principal pagina=%08x paginas=%u", mc.file, mc.front_page,
                            mc.front_pages);
            } else {
                std::printf(" menus: arquivo=%08x aberto_no_jogo=%u pausa=%u/%u pausou=%d foco=%08x fechou=%d despausou=%d "
                            "fim=%u/%u linhas=%u fechou=%d",
                            mc.file, mc.open_in_game, mc.pause_pages, mc.pause_controls, mc.paused ? 1 : 0,
                            mc.pause_focus, mc.closed ? 1 : 0, mc.unpaused ? 1 : 0, mc.end_pages, mc.end_controls,
                            mc.end_rows, mc.end_closed ? 1 : 0);
            }
            const MenuGuards& mg = g_menu_guards;
            if (mg.no_component || mg.no_focus || mg.no_page || mg.scroll_zero_range ||
                g_menu_handler_guards.level_index || g_menu_handler_guards.option_box) {
                std::printf(" guardas_menus: componente=%u foco=%u pagina=%u barra=%u fase=%u caixa=%u", mg.no_component,
                            mg.no_focus, mg.no_page, mg.scroll_zero_range, g_menu_handler_guards.level_index,
                            g_menu_handler_guards.option_box);
            }
        }
        // O jogo cria objetos (a arma na mão e as peças presas aos ossos dela): conta de novo.
        int alive = 0;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) ++alive;
        if (alive != DynamicObjCount) {
            std::printf(" ERRO DynamicObjCount=%d lista=%d", DynamicObjCount, alive);
            ++failures;
        }
        if (gg.texture_without_palette || gg.entity_without_gfx || gg.bad_texture_ref || gg.table_full) {
            std::printf(" guardas_gfx: paleta=%u sem_gfx=%u ref=%u tabela=%u", gg.texture_without_palette,
                        gg.entity_without_gfx, gg.bad_texture_ref, gg.table_full);
        }
        if (want.one_past_end) std::printf(" índice_no_limite=%u", want.one_past_end);
        if (g.entity_without_gfx || g.cel_without_glist || g.portal_without_cel || g.param_key_out_of_range ||
            g.dynamic_without_block || g.discard_outside_file || g.block_id_out_of_range) {
            std::printf(" guardas: sem_gfx=%u sem_glist=%u portal=%u chave=%u sem_1a=%u sem_1c=%u id=%u",
                        g.entity_without_gfx, g.cel_without_glist, g.portal_without_cel, g.param_key_out_of_range,
                        g.dynamic_without_block, g.discard_outside_file, g.block_id_out_of_range);
        }
        if (campaign_level) campaign::print_line(pack, campaign_detail);
        std::printf("\n");
        ++levels;
        if (!ok) ++failures;
    }
    if (sights::enabled()) {
        sights::print_table();
        return 0;
    }
    campaign::print_summary();
    std::printf("stubs chamados (todas as fases):");
    for (const auto& [name, n] : pending_counts()) std::printf(" %s=%d", name.c_str(), n);
    std::printf("\nestados da IA sem porte (mensagens):");
    for (int i = 0; i < kDroneStateCount; ++i) {
        if (g_drone_state_pending[i] == 0) continue;
        const char* name = size_t(i) < g_drone_state_names.size() ? g_drone_state_names[i].c_str() : "?";
        std::printf(" %d:%s=%u", i, name, g_drone_state_pending[i]);
    }
    std::printf("\nstubs chamados pelos tiros da IA (tiros_ia, todas as fases):");
    for (const auto& [name, n] : g_ai_shot_stubs) std::printf(" %s=%d", name.c_str(), n);
    std::printf("\nstubs chamados pelos geradores forçados (geradores_forçados, todas as fases):");
    for (const auto& [name, n] : g_spawner_run_stubs) std::printf(" %s=%d", name.c_str(), n);
    if (g_drone_weapon_guards != 0) std::printf("\nguardas das armas da IA: %u", g_drone_weapon_guards);
    if (g_drone_guards != 0) std::printf("\nguardas da IA: %u", g_drone_guards);
    if (g_drone_combat_guards != 0) std::printf("\nguardas do combate da IA: %u", g_drone_combat_guards);
    if (g_drone_vision_guards != 0) std::printf("\nguardas da visão da IA: %u", g_drone_vision_guards);
    if (!hd_pack.empty()) std::printf("\ntexturas com substituta HD: %d de %d", hd_found, hd_total);
    std::printf("\nmalhas com skinning: %d de %d decodificadas (%d falhas), %zu vértices, %zu triângulos, "
                "paleta máxima %d ossos, pesos fora de [0,1]: %d",
                skin_all.ok, skin_all.total, skin_all.failed, skin_all.vertices, skin_all.triangles,
                skin_all.max_bones, skin_all.weight_out);
    // Rostos (alvos de morph, bloco 0x2B): peles com morphs e malhas distintas em todas as fases.
    std::printf("\nrostos: peles=%zu morphs=%d alvos=%u vertices_com_morph=%zu no_cabecalho=%zu falhas=%d guardas=%u",
                g_face_skins_all.size(), g_face_all.meshes, g_face_all.max_targets, g_face_all.morph_vertices,
                g_face_all.header_vertices, g_face_all.failed, g_anim_face_guards);
    // Estado do GS das malhas (pares A+D das cadeias): "-" = registrador não escrito pela cadeia.
    std::printf("\nestado do GS: %d malhas, %d com mais de um estado, %u lotes sem estado próprio; partes:",
                g_gs.meshes, g_gs.multi, g_gs.inherited);
    for (const auto& [key, n] : g_gs.parts) {
        const auto hex = [](uint64_t v) {
            char buf[24];
            if (v == ~0ull) return std::string("-");
            std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
            return std::string(buf);
        };
        std::printf(" [ALPHA=%s TEST=%s ZBUF=%s ABE=%d]=%d", hex(std::get<0>(key)).c_str(),
                    hex(std::get<1>(key)).c_str(), hex(std::get<2>(key)).c_str(), std::get<3>(key), n);
    }
    std::printf("\nVU1: %d partes com mapa de ambiente (rotina 0x2e0) em %d malhas; cores de vértice: %llu, %.1f%% com r, g "
                "ou b acima de 0x80, alfa máximo 0x%x",
                g_gs.env_parts, g_gs.env_meshes, static_cast<unsigned long long>(g_gs.vertices),
                g_gs.vertices ? 100.0 * double(g_gs.overbright) / double(g_gs.vertices) : 0.0, g_gs.max_alpha);
    // As fichas "007 Bonus" (itens tipo 6): Pickup_Create só as cria com a medalha de ouro da missão
    // (PlrStats_HasGoldMedal). Com todas as medalhas no perfil do jogador 0, cada fase de novo e as fichas criadas.
    {
        const uint64_t all = ~uint64_t(0);
        std::memcpy(menu_bonus, &all, 8);
        int total = 0;
        std::printf("\nfichas 007 com a medalha de ouro:");
        for (const std::string& level : nf::list_levels(files)) {
            if (!only.empty() && level != only) continue;
            std::string anims = level;
            anims[2] = 'F';
            if (!has_file(files, anims + ".bin")) continue;
            while (GameFlow_Depth() > 0) GameFlow_PopState();
            IsWarmReset = 0;
            load_level(uint32_t(std::stoul(level, nullptr, 16)));
            int tokens = 0;
            for (obj_tag* o : live_pickups()) tokens += pickup_of(o)->kind == kPickupBonus;
            if (tokens != 0) std::printf(" %s=%d", level.c_str(), tokens);
            total += tokens;
        }
        std::printf(" total=%d", total);
        std::memset(menu_bonus, 0, 8);
    }
    // Só do port: os textos do PC no lugar dos que falam do hardware do PS2 (engine/pc_text.h). Cada rótulo da tabela,
    // no USATxt.dat de verdade, tem o trecho esperado e um formato que aceita o texto do PC (senão fica o do jogo).
    if (SelLang == TXTLANG_USA && Bank != nullptr) {
        int ok = 0;
        std::string bad;
        for (size_t i = 0; i < PcText_LabelOverrideCount(); ++i) {
            const PcLabelOverride& o = PcText_LabelOverrideAt(i);
            const char* game = Bank[FixupTable[o.label >> 24] + (o.label & 0xffffff)];
            if (Txt_BindLabel(o.label, 0) != game && PcText_FormatPrefix(o.en, game) && PcText_FormatPrefix(o.pt, game)) {
                ++ok;
            } else {
                char b[16];
                std::snprintf(b, sizeof(b), " %x", o.label);
                bad += b;
            }
        }
        std::printf("\ntextos do PC: %d de %zu rótulos do %s trocados%s%s", ok, PcText_LabelOverrideCount(),
                    FNames[SelLang], bad.empty() ? "" : "; ficam os do jogo:", bad.c_str());
    }
    // Só do port: as imagens da roda de armas (engine/weapon_wheel.h). Cada classe com imagem: o nome do HUD (weapon_data
    // +0x38) e o do dossiê da imagem dividem uma palavra (a tabela é do port: os nomes não são iguais), e a imagem existe no
    // pacote do menu (07000048), de onde o nightfire.exe a lê ao iniciar.
    if (SelLang == TXTLANG_USA && Bank != nullptr && has_file(files, "07000048.bin")) {
        const auto words = [](const char* s) {
            std::set<std::string> out;
            std::string w;
            for (const char* p = s; p != nullptr; ++p) {
                const unsigned char ch = static_cast<unsigned char>(*p);
                if (std::isalnum(ch) != 0) {
                    w.push_back(char(std::toupper(ch)));
                } else {
                    if (w.size() >= 2 && !(w.size() == 2 && std::isdigit(static_cast<unsigned char>(w[0])) != 0)) out.insert(w);
                    w.clear();
                }
                if (ch == 0) break;
            }
            return out;
        };
        // Os nomes do dossiê das imagens fixas (as pistolas de P_DSWEAPONS_Handler pelo nível, o barbeador).
        const uint32_t pistol_names[4] = {0x33d, 0x3c5, 0x33e, 0x3c7};
        int classes = 0, named = 0;
        std::string bad;
        for (int c = 0; c < 0x5f; ++c) {
            const WheelIconRef r = weapon_wheel_icon_ref(c);
            if (r.table == 0) continue;
            ++classes;
            uint32_t hud = 0;
            std::memcpy(&hud, reinterpret_cast<const uint8_t*>(weapon_def(c)) + 0x38, 4);
            const uint32_t dossier = r.table == 1   ? ds_weapons[r.index].name
                                     : r.table == 2 ? ds_gadgets[r.index].name
                                     : r.index < 4  ? pistol_names[r.index]
                                                    : ds_gadgets[6].name;
            const char* a = hud != 0 && hud != 0xffffffffu ? Txt_BindLabel(hud, 0) : "";
            const char* b = Txt_BindLabel(dossier, 0);
            const std::set<std::string> wa = words(a), wb = words(b);
            bool shared = false;
            for (const std::string& w : wa) shared = shared || wb.count(w) != 0;
            if (shared) {
                ++named;
            } else {
                char buf[160];
                std::snprintf(buf, sizeof(buf), " %02x \"%s\"/\"%s\"", c, a, b != nullptr ? b : "");
                bad += buf;
            }
        }
        uint32_t list[96];
        const int n = weapon_wheel_icon_hashes(list, 96);
        const nf::Bytes menu_pak = files.load("07000048.bin");
        const std::map<uint32_t, nf::Texture> found = nf::decode_pack_images(menu_pak, std::vector<uint32_t>(list, list + n));
        int square = 0;
        for (const auto& [h, t] : found) square += t.width == t.height;
        std::printf("\nroda de armas: %d classes com imagem, nomes batem %d de %d, imagens no 07000048 %zu de %d (%d "
                    "quadradas)%s%s",
                    classes, named, classes, found.size(), n, square, bad.empty() ? "" : "; nomes sem par:", bad.c_str());
    }
    std::printf("\n%d fases carregadas pelo carregador original, %d com diferenças\n", levels, failures);
    psiFileShutdown();
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    // 64 bits: na pilha da arena (platform/arena.h); 32 bits: direto.
    return nf::run_main([&] { return main_body(argc, argv); });
}
