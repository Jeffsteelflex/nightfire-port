// Port dos geradores de personagens da IA (DroneSpawner_*, objeto 0x30, posicionamento 0xf1). Ver a
// seção "Geradores" de drone.h e docs/engine.md.
//
// Ciclo: DroneSpawner_Create guarda os parâmetros e se registra em NPCGlobals.spawners[grupo]; depois da
// carga, NDrone2_SetupGroups (Drone_PostLoad_Init) chama DroneSpawner_Init, que copia o DIVars_tag de
// cada personagem do grupo para os modelos (entries), tira esses personagens do mapa e marca o grupo
// com 0xffff em NPCGlobals.group_counts. A cada tick, DroneSpawner_Control:
//  - liga killing pelo canal de remoção e running pelo de começo (que então zera);
//  - com o canal de parada ligado, para (e some, se não estiver removendo);
//  - removendo: marca para remoção os gerados (nas fases 07000011-13, 16 e 4a, só os que não foram
//    desenhados há pouco, obj_tag::unknown_fc, e o gerador espera até não sobrar nenhum) e some;
//  - senão conta os vivos, fecha a leva quando gerou max_alive (os modelos voltam a poder gerar), some
//    depois da última leva (ligando channel_done quando todos morreram) e, se é hora de gerar (tipo 2:
//    menos vivos que max_alive; tipo 3: a leva anterior inteira morreu), chama DroneSpawner_SpawnDrone
//    para a primeira vaga livre. DroneSpawner_SpawnDrone sorteia um modelo entre os que ainda não
//    geraram nesta leva e estão a mais de `distance` do jogador e fora da vista dele (sem os dois
//    testes na primeira leva de um gerador já ligado na carga) e cria o personagem como Drone_Create e
//    NDrone2_PostLoad_Init fariam: o mesmo caminho (NDrone2_DefaultInit, máquina de estados) dos
//    personagens da fase. Os gerados não entram no grupo (NDrone2_AddToGroup não é chamada).
// Um gerado que morre (vida <= 0, flags 0x600) ou foi marcado para remoção sai da vaga no tick seguinte;
// quando o objeto é apagado, Drone_Delete chama DroneSpawner_DroneDelete, que solta a vaga também.
//
// Divergências intencionais (o original escreveria ou leria fora do lugar):
// - DroneSpawner_Create, DroneSpawner_Init e DroneSpawner_Delete indexam NPCGlobals.spawners e
//   group_counts pelo grupo sem conferir o limite de 31: aqui um grupo fora da tabela não é registrado
//   (contado em g_drone_spawner_stats.guards; nenhum nos dados do jogo, que vão até o grupo 20).
// - DroneSpawner_Init copia os modelos seguindo a lista do grupo sem limite: aqui para em entry_count
//   (a lista e a contagem só mudam juntas, em NDrone2_AddToGroup).
// - Drone_PositionVisible lê o jogador 0 e a câmera dele sem conferir: sem jogador ou câmera, aqui o
//   ponto conta como fora da vista.
// - A fala de rádio das levas do tipo 3 em 0700000c/0d chama NDrone2_PlaySFX sem DCVars_tag, como o
//   original; o port de NDrone2_PlaySFX não toca o som nesse caso (g_drone_vision_guards). Nenhum
//   gerador do disco chega lá (os dessas fases são do tipo 2).
// Mantido: DroneSpawner_Init deixa NPCGlobals.groups[grupo] apontando para o primeiro modelo, já
// apagado (só NDrone2_AddToGroup lê a tabela, e nenhum caminho portado acrescenta ao grupo depois da
// carga); com a sala dos modelos ainda nula, Drone_PositionVisible nunca vê um modelo.

#include "engine/drone.h"

#include <cstring>

#include "engine/ainetwork.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

DroneSpawnerStats g_drone_spawner_stats;

namespace {

constexpr uint32_t kGroups = 31;  // NPCGlobals.groups, spawners, group_counts

DroneSpawner_tag* spawner_of(obj_tag* obj) { return static_cast<DroneSpawner_tag*>(obj->data); }

// Fases em que o canal de remoção poupa os gerados que estão à vista (07000011-13, 16 e 4a).
bool keeps_visible(uint32_t level) {
    return level == 0x7000016 || level == 0x700004a || (0x7000011 <= level && level <= 0x7000013);
}

}  // namespace

// [decomp] 0x001378d0 DroneSpawner_Create
// Gerador do posicionamento 0xf1 (parâmetros em DroneSpawner_tag); tipo menor que 2 não cria nada.
void DroneSpawner_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    if (Drone_bDisableSystem != 0 || level->params[0] < 2) return;
    obj_tag* obj = control_create_object(0x3c, nullptr, nullptr, nullptr);
    if (obj == nullptr) return;
    DroneSpawner_tag* s = spawner_of(obj);
    if (s == nullptr) return;
    control_init_object(obj);
    Vec_Copy2(pos, &obj->pos, &obj->old_pos);
    Vec_Copy2(rot, &obj->rot, &obj->old_rot);
    obj->type = 0x30;
    obj->cel = build_FindCel(&obj->pos, glb_world);
    s->kind = level->params[0];
    s->group = level->params[1];
    s->max_alive = static_cast<uint16_t>(level->params[2]);
    s->waves = static_cast<uint16_t>(level->params[3]);
    s->channel_start = static_cast<uint16_t>(level->params[4]);
    s->channel_stop = static_cast<uint16_t>(level->params[5]);
    s->channel_done = static_cast<uint16_t>(level->params[6]);
    s->channel_kill = static_cast<uint16_t>(level->params[7]);
    s->distance = static_cast<float>(level->params[8]);  // inteiro sem sinal
    if (s->distance == 0.0f) s->distance = 10.0f;
    if (s->waves == 0) s->waves = 32000;
    s->spawn = 0;
    s->entry_count = 0;
    s->wave_spawned = 0;
    s->alive = 0;
    s->wave = 0;
    s->entry_count2 = 0;
    s->unknown_30 = 0;
    s->entries = nullptr;
    s->spawned = nullptr;
    s->ready = 0;
    s->running = 0;
    s->killing = 0;
    ++g_drone_spawner_stats.created;
    if (s->group >= kGroups) {
        ++g_drone_spawner_stats.guards;  // port: o original escreveria além de NPCGlobals.spawners
        return;
    }
    NPCGlobals.spawners[s->group] = obj;
}

// [decomp] 0x00137aa0 DroneSpawner_Init
// Depois da carga (NDrone2_SetupGroups, só para grupos com gerador e personagens): os personagens do
// grupo viram modelos e saem do mapa. Sem personagens no grupo, o gerador some.
void DroneSpawner_Init(obj_tag* spawner) {
    DroneSpawner_tag* s = spawner_of(spawner);
    if (s == nullptr) return;
    NPCGLOBALS_tag* n = &NPCGlobals;
    if (s->group >= kGroups) {
        ++g_drone_spawner_stats.guards;  // port: o original leria além de NPCGlobals.group_counts
        return;
    }
    const uint16_t count = n->group_counts[s->group];
    if (count == 0) {
        spawner->life |= 1;
        n->spawners[s->group] = nullptr;
        return;
    }
    if (s->max_alive == 0 || count < s->max_alive) s->max_alive = count;
    Drone_tag* d = n->groups[s->group];
    if (d != nullptr) {
        s->entry_count = count;
        s->entry_count2 = count;
        if ((s->channel_start == 0 || switch_channels[s->channel_start] != 0) &&
            (s->channel_stop == 0 || switch_channels[s->channel_stop] == 0)) {
            s->anywhere = 1;
        }
        s->spawned = static_cast<obj_tag**>(Mem_Malloc(uint32_t(s->max_alive) << 2, 0x3404, 0));
        s->entries = static_cast<DroneSpawnEntry_tag*>(Mem_Malloc(uint32_t(s->entry_count) * 0xd0, 0x3504, 0));
        std::memset(s->spawned, 0, uint32_t(s->max_alive) << 2);
        std::memset(s->entries, 0, uint32_t(s->entry_count) * 0xd0);
        DroneSpawnEntry_tag* e = s->entries;
        DroneSpawnEntry_tag* const end = s->entries + s->entry_count;
        e->spawned = 0;
        for (;;) {
            amemcpy(&e->vars, &d->divars, 0xc0);
            e->cel = d->obj->cel;
            NDrone2_Enable(0, d);
            ++e;
            d->obj->life |= 1;
            Drone_tag* next = d->group_next;
            control_delete_object(d->obj);  // apaga também o Drone_tag (Drone_Delete)
            ++g_drone_spawner_stats.templates;
            d = next;
            if (d == nullptr) break;
            if (e == end) {
                ++g_drone_spawner_stats.guards;  // port: o original escreveria além de entries
                break;
            }
            e->spawned = 0;
        }
    }
    s->ready = 1;
    s->init_frame = GameState.frame;
    n->group_counts[s->group] = 0xffff;
    if (s->channel_start == 0) s->running = 1;
}

// [decomp] 0x00137cb0 DroneSpawner_SpawnDrone
// Sorteia um modelo (o n-ésimo, n ao acaso entre 0 e entry_count - 1, dos que podem gerar; se houver
// menos que n + 1, não gera neste tick) e cria o personagem dele.
obj_tag* DroneSpawner_SpawnDrone(obj_tag* spawner) {
    DroneSpawner_tag* s = spawner_of(spawner);
    if (s == nullptr || s->spawned == nullptr) return nullptr;
    int32_t pick = static_cast<int32_t>(Rand_Rand(static_cast<uint32_t>(static_cast<int16_t>(s->entry_count2))));
    DroneSpawnEntry_tag* e = s->entries;
    obj_tag* player = NDrone2_Player();
    float dist_sq = 1000.0f;
    for (uint32_t i = 0; i < s->entry_count; ++i, ++e) {
        if (player != nullptr) dist_sq = Vec_SqDist3D(&player->pos, &e->vars.pos);
        if (s->anywhere == 0 && !(s->distance * s->distance <= dist_sq)) continue;
        if (e->spawned != 0) continue;
        if (--pick != -1) continue;
        if (s->anywhere == 0 && Drone_PositionVisible(&e->vars.pos, e->cel) != 0) return nullptr;
        obj_tag* obj = NDrone2_CreateObj(&e->vars);
        e->vars.obj = obj;
        if (obj == nullptr) return nullptr;
        Drone_tag* d = static_cast<Drone_tag*>(obj->data);
        obj->created = Rand_Rand(10000);
        d->divars.obj = obj;
        d->divars.pos = e->vars.pos;
        d->divars.rot = e->vars.rot;
        amemcpy(d->divars.params, e->vars.params, 0x84);
        d->group_id = e->vars.params[4];
        d->param7 = e->vars.params[7];
        NDrone2_PostLoad_Init(obj);
        ++e->spawned;
        ++s->wave_spawned;
        ++g_drone_spawner_stats.spawned;
        return obj;
    }
    return nullptr;
}

// [decomp] 0x00137e68 DroneSpawner_Control
// Atualização do objeto 0x30 (control_funcs). Ver o comentário do arquivo.
void DroneSpawner_Control(obj_tag* spawner) {
    NPCGLOBALS_tag* n = &NPCGlobals;
    if (n->enabled == 0 || Drone_bDisableSystem != 0 || spawner == nullptr || (spawner->life & 1) != 0) return;
    DroneSpawner_tag* s = spawner_of(spawner);
    if (s == nullptr || s->group == 0 || s->spawned == nullptr || s->kind < 2 || s->ready == 0) return;
    if (s->channel_kill != 0 && switch_channels[s->channel_kill] != 0) s->killing = 1;
    if (s->running == 0 && s->killing == 0) {
        if (s->channel_start == 0 || switch_channels[s->channel_start] == 0) return;
        s->channel_start = 0;
        s->running = 1;
    }
    if (s->channel_stop != 0 && switch_channels[s->channel_stop] != 0) {
        s->running = 0;
        if (s->killing != 0) return;
        spawner->life |= 1;
        return;
    }

    if (s->killing != 0) {
        const uint32_t level = GameState.level_file;
        const bool all = !keeps_visible(level);
        uint16_t kept = 0;
        for (uint16_t i = 0; i < s->max_alive; ++i) {
            obj_tag* o = s->spawned[i];
            if (o == nullptr) continue;
            if (!all && o->unknown_fc != 0) {
                ++kept;
                continue;
            }
            if (level == 0x7000016) DroneFunc_SetDeathChannel(static_cast<Drone_tag*>(o->data));
            o->life |= 1;
            s->spawned[i] = nullptr;
        }
        if (!all && kept != 0) return;
        spawner->life |= 1;
        s->running = 0;
        return;
    }

    // Vivos: soltam a vaga os que não são mais personagens, foram marcados para remoção ou morreram.
    s->alive = 0;
    for (uint16_t i = 0; i < s->max_alive; ++i) {
        obj_tag*& o = s->spawned[i];
        if (o == nullptr) continue;
        if (o->type != 2 || (o->life & 1) != 0) {
            o = nullptr;
            continue;
        }
        const Drone_tag* d = static_cast<const Drone_tag*>(o->data);
        if (d == nullptr || (d->flags & 0x600) != 0 || d->health <= 0.0f) {
            o = nullptr;
            continue;
        }
        ++s->alive;
    }

    // Depois da última leva: some (com canal de fim ou de remoção, só quando todos morreram).
    if (s->wave >= s->waves) {
        if (s->channel_done != 0 || s->channel_kill != 0) {
            if (s->alive != 0) return;
            if (s->channel_done != 0) {
                switch_channels[s->channel_done] = 1;
                switch_channels_time[s->channel_done] = GameState.frame;
            }
        }
        spawner->life |= 1;
        s->running = 0;
        s->ready = 0;
        return;
    }
    // A leva gerou max_alive: começa a próxima (todos os modelos voltam a poder gerar).
    if (s->wave_spawned >= s->max_alive) {
        s->wave_spawned = 0;
        ++s->wave;
        ++g_drone_spawner_stats.waves;
        s->anywhere = 0;
        DroneSpawner_tag* again = spawner_of(spawner);
        if (again != nullptr && again->spawned != nullptr && again->kind >= 2) {
            for (uint32_t i = 0; i < again->entry_count; ++i) again->entries[i].spawned = 0;
        }
        s->spawn = 0;
        return;
    }
    if (s->kind == 2) {
        s->spawn = s->alive < s->max_alive ? 1 : 0;
    } else if (s->kind == 3) {
        if (s->anywhere != 0) s->spawn = 1;
        if (s->alive == 0) {
            // Nas fases 0700000c e 0700000d, uma fala de rádio anuncia as levas depois da primeira.
            const uint32_t level = GameState.level_file;
            if ((level == 0x700000c || level == 0x700000d) && s->wave != 0) {
                NDrone2_PlaySFX(nullptr, Rand_Rand(2) + 0xd6, nullptr, 1, 1.0f);
            }
            s->spawn = 1;
        }
    }
    if (s->spawn == 0) return;
    // Uma vaga por tick; se o sorteio gastou um teste de visão sem gerar, tenta no próximo tick.
    for (uint16_t i = 0; i < s->max_alive; ++i) {
        if (s->spawned[i] != nullptr) continue;
        const uint16_t checks = n->vision_checks;
        s->spawned[i] = DroneSpawner_SpawnDrone(spawner);
        if (s->spawned[i] != nullptr) return;
        if (checks != n->vision_checks) return;
    }
}

// [decomp] 0x0013a338 DroneSpawner_Delete
// Remoção do objeto 0x30: solta as vagas e os modelos e tira o gerador de NPCGlobals.spawners.
void DroneSpawner_Delete(obj_tag* spawner) {
    if (spawner == nullptr) return;
    DroneSpawner_tag* s = spawner_of(spawner);
    if (s == nullptr) return;
    if (s->spawned != nullptr) Mem_Free(reinterpret_cast<void**>(&s->spawned));
    if (s->entries != nullptr) Mem_Free(reinterpret_cast<void**>(&s->entries));
    s->spawned = nullptr;
    s->entries = nullptr;
    if (s->group >= kGroups) {
        ++g_drone_spawner_stats.guards;  // port: o original escreveria além de NPCGlobals.spawners
        return;
    }
    NPCGlobals.spawners[s->group] = nullptr;
}

// [decomp] 0x0013a3b8 DroneSpawner_DroneDelete
// Um personagem do grupo do gerador foi apagado (Drone_Delete): solta a vaga dele.
void DroneSpawner_DroneDelete(obj_tag* spawner, obj_tag* drone) {
    if (spawner == nullptr || drone == nullptr || (spawner->life & 1) != 0) return;
    DroneSpawner_tag* s = spawner_of(spawner);
    if (s == nullptr || s->spawned == nullptr || s->max_alive == 0) return;
    for (uint32_t i = 0; i < s->max_alive; ++i) {
        if (s->spawned[i] == drone) {
            s->spawned[i] = nullptr;
            ++g_drone_spawner_stats.released;
        }
    }
}

// [decomp] 0x0013a7a0 Drone_PositionVisible
// O ponto está no campo de visão (raio 2) da câmera do jogador 0, numa sala visível no último quadro
// (sala nula: não confere) e com linha de visão da câmera até ele?
uint32_t Drone_PositionVisible(_VECTOR* pos, cel_tag* cel) {
    obj_tag* player = glb_players[0];
    if (player == nullptr || player->data == nullptr) {
        ++g_drone_spawner_stats.guards;  // port: o original leria os dados de um jogador nulo
        return 0;
    }
    const int8_t index = bloke_of(player)->player;
    viewer_tag* v = 0 <= index && index < kViewerCount ? glb_viewer[index] : nullptr;
    if (v == nullptr) {
        ++g_drone_spawner_stats.guards;  // port: o original leria os planos de uma câmera nula
        return 0;
    }
    if (Vision_InView(pos, 2.0f, v) == 0) return 0;
    if (cel != nullptr && cel->misc[0xa0 - 0x98] == 0) return 0;
    CelPos_tag from{};
    from.pos = v->pos;
    from.cel = v->cel;
    CelPos_tag to{};
    to.pos = *pos;
    to.cel = cel;
    return DroneVision_LineOfSightToObject(&from, &to, player, nullptr, 1, 0) != 0 ? 1 : 0;
}
