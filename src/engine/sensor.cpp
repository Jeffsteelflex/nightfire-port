// O sistema de segurança: câmeras, monitores e as contagens do SSys. Ver sensor.h.
//
// Divergências do original (só no port; também nas funções):
// - Sensor_Create sem memória para o feixe ou o suporte não cria a peça (o original escreveria no objeto nulo; conta em
//   SecurityStats::guards); sem modelo, os flags da câmera ficam 0 (o original leria o endereço 0x44).
// - Sensor_Update no estado 1 sem jogador (glb_players[0] nulo) não segue ninguém (o original leria o objeto nulo em
//   Vec_Track); conta em SecurityStats::guards.
// - Sensor_Update no estado 2 com uma câmera sem caminho: Spline_Interp não escreve nada (o original leria o endereço
//   0) e a orientação de volta é a atual, então a câmera volta logo ao estado 0.
// - Monitor_Update e Monitor_Activate sem a câmera do jogador (glb_viewer nulo) não mexem nela.
// - O quatérnio desalinhado de SENSOR (+0x14) passa por uma cópia alinhada (o PS2 o lê e escreve com lwc1/swc1).
// - SSys_Msg ignora uma fala acima de 6 (as tabelas têm 7; SSys_Monitor só pede de 0 a 6).
// - O w do fim do raio do feixe (lixo da pilha no PS2) é o da posição da câmera.

#include "engine/sensor.h"

#include <cstdio>
#include <cstring>

#include "engine/collide.h"
#include "engine/copter.h"
#include "engine/explode.h"
#include "engine/game.h"
#include "engine/gunimp.h"
#include "engine/hazard.h"
#include "engine/searchlight.h"
#include "engine/space_station.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/spline.h"
#include "engine/vecmath.h"
#include "engine/view.h"

uint8_t SSysItems[40];
float SSysDelayTimer = 0.0f;
SSYSMSG Laser1_134[7];
SSYSMSG Laser2_135[7];
SSYSMSG Cam1_132[7];
SSYSMSG Cam2_133[7];
float SSysMinutes[4];
SecurityStats g_security_stats;

namespace {

SENSOR* sensor_of(obj_tag* obj) { return static_cast<SENSOR*>(obj->data); }
MONITOR_tag* monitor_of(obj_tag* obj) { return static_cast<MONITOR_tag*>(obj->data); }

quaternion_tag get_quat(const SENSOR* s) {
    quaternion_tag q;
    std::memcpy(&q, s->quat, sizeof(q));
    return q;
}
void set_quat(SENSOR* s, const quaternion_tag& q) { std::memcpy(s->quat, &q, sizeof(q)); }

// Item i de SSysItems: o tempo, o canal e o estado.
float& item_timer(int i) { return *reinterpret_cast<float*>(SSysItems + i * 8); }
uint8_t& item_channel(int i) { return SSysItems[i * 8 + 4]; }
uint8_t& item_state(int i) { return SSysItems[i * 8 + 5]; }

bool script_camera_active() {
    const viewer_tag* v = glb_viewer[4];
    return v != nullptr && v->active != 0;
}

// A primeira câmera da lista, a partir do nó "n", que não está desligada e é do grupo; ela fica fora da máscara de
// desenho das câmeras (unknown_f8 = 0: o modelo não aparece na própria imagem).
obj_tag* find_camera(LLNODE_tag* n, uint16_t group) {
    for (; n != nullptr; n = n->next) {
        SENSOR* s = reinterpret_cast<SENSOR*>(n);
        if (s->obj->state == 3) continue;
        if (s->group == group) {
            s->obj->unknown_f8 = 0;
            return s->obj;
        }
    }
    return nullptr;
}

void hud_security_view(BLData* b, bool on) {
    HUD_Enable(b->hud, 0x10, on ? 1 : 0, 0);
    HUD_Enable(b->hud, 0, on ? 0 : 1, 0);
    HUD_Enable(b->hud, 1, on ? 0 : 1, 0);
}

// O jogador volta a ter a câmera dele (fim da imagem do monitor).
void give_back(MONITOR_tag* m, obj_tag* player) {
    Camera_SetUpdator(m->index, player, Player_PositionCamera);
    Player_Enable(player, nullptr, 0);
    hud_security_view(bloke_of(player), false);
}

}  // namespace

// [decomp] 0x001ad878 SSys_Msg
// A fala "msg" (0 a 6) do item: 1 laser 1, 2 laser 2, 3 câmeras 1, 4 câmeras 2. Em 0700000b, com o canal 0x76
// desligado, só o laser 2 fala; em 0700000a as câmeras 1 não falam.
void SSys_Msg(uint16_t item, uint16_t msg) {
    const bool quiet = GameState.level_file == 0x700000b && switch_channels[0x76] == 0;
    const SSYSMSG* table = nullptr;
    switch (item) {
        case 1:
            if (quiet) return;
            table = Laser1_134;
            break;
        case 2:
            if (quiet) return;
            table = Laser2_135;
            break;
        case 3:
            if (GameState.level_file == 0x700000a || quiet) return;
            table = Cam1_132;
            break;
        case 4:
            if (quiet) return;
            table = Cam2_133;
            break;
        default:
            return;
    }
    if (msg >= 7) return;  // as tabelas têm 7 falas (SSys_Monitor só pede 0 a 6)
    ++g_security_stats.messages;
    Sound_Play(table[msg].sound, 100.0f, 0, 0);
}

// [decomp] 0x001ad998 SSys_Monitor
// Fora das cenas, a cada tick, as quatro contagens: o canal do item ligado começa 240 s (fala 6) e as falas 5, 4, 3 e 2
// aos 180, 120, 60 e 6 s; no fim a fala 1 e o canal desliga. O canal desligado antes para a contagem (fala 0). O laser
// 2 primeiro encerra a contagem do laser 1 (fala 0 dele, o canal dele ligado e esquecido) e espera 8 s.
void SSys_Monitor() {
    if (ScriptCam != 0) return;
    for (int i = 1; i < 5; ++i) {
        const uint8_t channel = item_channel(i);
        if (channel == 0xff) continue;
        if (switch_channels[channel] == 0) {
            if (item_state(i) != 0) {
                item_state(i) = 0;
                SSys_Msg(uint16_t(i), 0);
                item_timer(i) = -1.0f;
            }
            continue;
        }
        if (item_timer(i) <= -1.0f) {
            if (i == 2) {
                if (0.0f < SSysDelayTimer) {
                    SSysDelayTimer = SSysDelayTimer - REC_FRAME_RATE;
                    continue;
                }
                if (item_channel(1) != 0xff) {
                    SSys_Msg(1, 0);
                    item_timer(1) = -1.0f;
                    switch_channels[item_channel(1)] = 1;
                    item_channel(1) = 0xff;
                    SSysDelayTimer = 8.0f;
                    item_state(1) = 0;
                    continue;
                }
            }
            item_timer(i) = 240.0f;
            item_state(i) = 6;
            ++g_security_stats.countdowns;
            SSys_Msg(uint16_t(i), 6);
        } else {
            item_timer(i) = item_timer(i) - REC_FRAME_RATE;
        }
        const float t = item_timer(i);
        switch (item_state(i)) {
            case 2:
                if (t <= 0.0f) {
                    item_state(i) = 1;
                    SSys_Msg(uint16_t(i), 1);
                    if (item_channel(i) != 0) switch_channels[item_channel(i)] = 0;
                    item_state(i) = 0;
                    item_timer(i) = -1.0f;
                }
                break;
            case 3:
                if (t < SSysMinutes[3]) {
                    item_state(i) = 2;
                    SSys_Msg(uint16_t(i), 2);
                }
                break;
            case 4:
                if (t < SSysMinutes[2]) {
                    item_state(i) = 3;
                    SSys_Msg(uint16_t(i), 3);
                }
                break;
            case 5:
                if (t < SSysMinutes[1]) {
                    item_state(i) = 4;
                    SSys_Msg(uint16_t(i), 4);
                }
                break;
            case 6:
                if (t < SSysMinutes[0]) {
                    item_state(i) = 5;
                    SSys_Msg(uint16_t(i), 5);
                }
                break;
            default:
                break;
        }
    }
}

// [decomp] 0x001b01b0 SSys_Init
// Na carga fria: os quatro itens parados e sem canal.
void SSys_Init() {
    if (IsWarmReset != 0) return;
    for (int i = 1; i < 5; ++i) {
        item_timer(i) = -1.0f;
        item_channel(i) = 0xff;
        item_state(i) = 0;
    }
}

// [decomp] 0x001b0208 SSys_RamLoad
// O recomeço traz os itens 1-4 da cópia (menos o laser 2 em 0700000a); em 0700000b o laser 1 volta parado e sem canal.
void SSys_RamLoad(uint8_t* items) {
    for (int i = 1; i < 5; ++i) {
        if (GameState.level_file != 0x700000a || i != 2) std::memcpy(SSysItems + i * 8, items + i * 8, 8);
    }
    if (GameState.level_file == 0x700000b) {
        item_timer(1) = -1.0f;
        item_channel(1) = 0xff;
        item_state(1) = 0;
    }
    SSysDelayTimer = 0.0f;
}

// [decomp] 0x001af010 Sensor_Create
// A câmera de segurança: o cone (parâmetro 1, graus), o alcance (2), os canais de alarme (3), de desligar (4) e de
// "sendo vista" (8), o grupo dos monitores (6), fora da lista dos monitores (5), o feixe (entidade do parâmetro 0,
// escalado pelo alcance) e o suporte (entidade do 7, alinhado para cima e para a frente da câmera). A varredura é o
// caminho do posicionamento: quadros - 1 e a velocidade quadros / duração (level_tag +4) por tick a 60 Hz.
obj_tag* Sensor_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(100, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_security_stats.sensors;
    SENSOR* s = sensor_of(obj);
    s->angle = level->params[1];
    s->cos_half = PS2Sinf(float(s->angle) * 0.008726647f + 1.5707964f);
    s->sin_half = PS2Sinf(float(s->angle) * 0.008726647f);
    s->range = float(level->params[2]);
    s->alarm = uint16_t(level->params[3]);
    s->off = uint16_t(level->params[4]);
    s->unlisted = uint8_t(level->params[5]);
    s->group = uint8_t(level->params[6]);
    s->t = 0.0f;
    s->view_channel = uint16_t(level->params[8]);
    s->path = level->path;
    const uint16_t count = uint16_t(level->path_count);
    s->frames = count == 0 ? 0u : uint32_t(count - 1);
    const float frames = float(s->frames) * REC_FRAME_RATE;
    s->speed = frames;
    s->beam = nullptr;
    s->obj = obj;
    s->viewed = 0;
    s->speed = frames * (FRAME_RATE / float(level->path_unknown));
    if (level->params[0] != 0) {
        obj_tag* beam = control_create_object(0, pos, rot, nullptr);
        s->beam = beam;
        if (beam != nullptr) {
            beam->type = 1;
            beam->flags |= 0x20;
            beam->mode |= 5;
            beam->scale = s->range;
            beam->owner = obj;
            Mat_Copy(&obj->matrix, &beam->matrix);
            hashtable_set_object_to_entity_gfx(beam, level->params[0]);
        } else {
            ++g_security_stats.guards;
        }
    }
    if (level->params[7] != 0) {
        _VECTOR dir;
        Mat_GetDir(&dir, &obj->matrix);
        obj_tag* base = control_create_object(0, pos, rot, nullptr);
        s->base = base;
        if (base != nullptr) {
            base->type = 1;
            base->mode |= 4;
            base->owner = obj;
            Mat_Align2Up(&base->matrix, &CONSTANT_UP1, &dir);
            hashtable_set_object_to_entity_gfx(base, level->params[7]);
        } else {
            ++g_security_stats.guards;
        }
    }
    if (s->frames != 0) obj->mode |= 0x44;
    obj->glist = glist;
    obj->type = 0x37;
    obj->flags = glist != nullptr ? glist->flags : 0;
    build_LinkToRoom(obj, 0, glb_world);
    s->glist = glist;
    s->broken = glist != nullptr ? glist->lod : nullptr;
    if (s->unlisted == 0) LList_Add(&CameraList, &s->node);
    return obj;
}

// [decomp] 0x001af398 Sensor_SetBeam
// O suporte gira com a câmera (alinhado para cima, para a frente dela) e o feixe vai até o que o raio da frente
// (alcance da câmera, máscara 8) acerta: devolve 1 se acertou o jogador, 0 se não, -1 sem feixe ou sem jogador.
int32_t Sensor_SetBeam(obj_tag* player, obj_tag* obj, SENSOR* s) {
    _VECTOR dir;
    if (s->base != nullptr) {
        Mat_GetDir(&dir, &obj->matrix);
        Mat_Align2Up(&s->base->matrix, &CONSTANT_UP1, &dir);
        s->base->mode |= 0x20;
    }
    if (s->beam == nullptr || player == nullptr) return -1;
    Mat_Copy(&obj->matrix, &s->beam->matrix);
    s->beam->mode |= 0x20;
    Mat_GetDir(&dir, &obj->matrix);
    const float range = s->range;
    // vmulx.xyz + vadd.xyzw: o w do fim do raio é lixo da pilha no PS2 (não importa ao raio); aqui, o da posição.
    const _VECTOR to{obj->pos.x + dir.x * range, obj->pos.y + dir.y * range, obj->pos.z + dir.z * range, obj->pos.w};
    s->beam->scale = range;
    P32<HITDATA_tag> hits = nullptr;
    int32_t seen = 0;
    if (Collide_RayIntersect(&obj->pos, &to, obj->cel, obj, s->beam, &hits, 0, 8, 0)) {
        s->beam->scale = hits->dist;
        seen = hits->obj == player ? 1 : 0;
        Collide_FreeHitList(&hits);
    }
    return seen;
}

// [decomp] 0x001af4f0 Sensor_InCone
// O jogador visto: pelo feixe (Sensor_SetBeam == 1) ou, a até alcance + o raio dele, dentro do cone (Intersect_ConeSphere
// com a frente da câmera) e com linha de visão da câmera até ele (máscara 10). Com cone de 180 ou 360 graus o original
// ainda multiplica a posição do jogador pela frente num registro que ninguém lê.
uint32_t Sensor_InCone(obj_tag* player, obj_tag* obj, SENSOR* s) {
    const int32_t beam = Sensor_SetBeam(player, obj, s);
    if (beam == 1) return 1;
    if (player == nullptr) return 0;
    const _VECTOR at{obj->matrix.m[3][0], obj->matrix.m[3][1], obj->matrix.m[3][2], obj->matrix.m[3][3]};
    const float reach = s->range + player->sphere.w;
    if (reach * reach < Vec_SqDist3D(&at, &player->sphere)) return 0;
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->matrix);
    if (Intersect_ConeSphere(&at, &dir, s->cos_half, s->sin_half, &player->sphere) == 0) return 0;
    return Collide_LineOfSight(&obj->pos, &player->pos, obj->cel, obj, player, 10) ? 1u : 0u;
}

// [decomp] 0x001af618 Sensor_Update
// A câmera por tick: esconde as peças enquanto um monitor a mostra, o zumbido (ou o desligar pelo canal) e os estados
// (ver sensor.h).
void Sensor_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    obj_tag* player = glb_players[0];
    SENSOR* s = sensor_of(obj);
    uint32_t seen = 0;
    if (s->viewed == 1) {
        obj->flags |= 0x10;
        if (s->beam != nullptr) s->beam->flags |= 0x10;
        if (s->base != nullptr) s->base->flags |= 0x10;
        s->viewed = 2;
    } else if (s->viewed == 2) {
        obj->flags &= 0xffffffefu;
        if (s->beam != nullptr) s->beam->flags &= 0xffffffefu;
        if (s->base != nullptr) s->base->flags &= 0xffffffefu;
        s->viewed = 0;
    }
    if (s->off != 0 && switch_channels[s->off] != 0) {
        if (s->hum != 0) {
            Sound_Stop(s->hum);
            s->hum = 0;
        }
        if (obj->state != 3) ++g_security_stats.disabled;
        obj->state = 3;
    } else if (s->hum == 0) {
        s->hum = Sound_Play3D(0x4aa, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    const bool can_look = !script_camera_active();
    switch (int16_t(obj->state)) {
        case 1: {
            if (player == nullptr) {
                // Só no port: sem jogador o original leria o objeto nulo (Vec_Track).
                ++g_security_stats.guards;
                break;
            }
            if (s->alarm != 0) {
                s->beep = s->beep + REC_FRAME_RATE;
                float period = 0.5f;
                if (2.0f <= s->seen) period = 0.25f;
                if (3.0f <= s->seen) period = 0.125f;
                if (period <= s->beep) {
                    s->beep = 0.0f;
                    Sound_Play3D(0x4a9, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                }
            }
            quaternion_tag q = get_quat(s);
            Vec_Track(0.05f, &player->sphere, &obj->pos, &obj->matrix, &q);
            set_quat(s, q);
            if (can_look) seen = Sensor_InCone(player, obj, s);
            if (seen == 0) {
                obj->unknown_f6 = uint16_t(obj->unknown_f6 + 1);
            } else {
                obj->unknown_f6 = 0;
            }
            if (int32_t(FRAME_RATE_INT) < int32_t(int16_t(obj->unknown_f6))) {
                obj->state = 2;
                ++g_security_stats.lost;
            } else {
                s->seen = s->seen + REC_FRAME_RATE;
                if (2.0f <= s->seen && s->alarm != 0) {
                    switch_channels[s->alarm] = 1;
                    switch_channels_time[s->alarm] = GameState.frame;
                    ++g_security_stats.alarms;
                }
            }
            break;
        }
        case 0: {
            if (s->beam != nullptr) View_SetDrawInAllViews(s->beam);
            if (s->alarm != 0) switch_channels[s->alarm] = 0;
            s->seen = 0.0f;
            if (s->frames != 0) {
                Spline_Interp(s->path, 0, uint16_t(s->frames), s->t, &obj->matrix, nullptr, 2);
                obj->mode |= 0x20;
                s->t = s->t + s->speed * FRAME_RATE_MUL;
                if (float(s->frames) * 0.5f < s->t && s->beep == 0.0f) {
                    Sound_Play3D(0x4dd, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                    s->beep = 1.0f;
                }
                if (float(s->frames) < s->t) {
                    s->t = 0.0f;
                    Sound_Play3D(0x4dd, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                    s->beep = 0.0f;
                }
            }
            if (can_look) seen = Sensor_InCone(player, obj, s);
            if (seen == 0) {
                obj->unknown_f6 = 0;
            } else {
                const int16_t before = int16_t(obj->unknown_f6);
                obj->unknown_f6 = uint16_t(before + 1);
                if (0x1e < before) {
                    s->seen = 0.0f;
                    s->beep = 0.0f;
                    obj->state = 1;
                    ++g_security_stats.spotted;
                    if (s->alarm != 0) Sound_Play3D(0x4a9, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                }
            }
            break;
        }
        case 2: {
            s->beep = 0.0f;
            if (s->alarm != 0) switch_channels[s->alarm] = 0;
            obj->unknown_f6 = 0;
            // Só no port: sem caminho, Spline_Interp não escreve e a volta é para a orientação atual.
            MATRIX m = obj->matrix;
            Spline_Interp(s->path, 0, uint16_t(s->frames), s->t, &m, nullptr, 2);
            quaternion_tag target, q = get_quat(s);
            Quat_MatToQuat(&target, &m);
            Quat_Slerp_Acc(0.1f, &q, &target, &q);
            Quat_QuatToMat(&q, &obj->matrix);
            set_quat(s, q);
            Sensor_SetBeam(player, obj, s);
            if (Quat_IsEqual(&q, &target, 0.01f)) obj->state = 0;
            break;
        }
        case 3:
            if (s->broken != nullptr) obj->glist = s->broken;
            if (s->beam != nullptr) s->beam->unknown_f8 = 0;
            if (s->hum != 0) {
                Sound_Stop(s->hum);
                s->hum = 0;
            }
            if (s->off != 0 && switch_channels[s->off] == 0) {
                obj->state = 0;
                obj->glist = s->glist;
            }
            break;
        default:
            break;
    }
}

// [decomp] 0x001b08b0 Monitor_Create
// O monitor: o grupo das câmeras que ele mostra (parâmetro 0, em obj_tag::unknown_f6) e, com o parâmetro 1 = 0, 1 de
// vida (senão é indestrutível). O quinto argumento (o modelo seguinte) não é usado.
obj_tag* Monitor_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag*) {
    obj_tag* obj = control_create_object(0x14, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_security_stats.monitors;
    MONITOR_tag* m = monitor_of(obj);
    m->health = level->params[1] == 0 ? 1.0f : 0.0f;
    obj->state = 0;
    obj->type = 0x38;
    obj->unknown_f6 = uint16_t(level->params[0]);
    Control_SetGList(obj, glist);
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001b0058 Monitor_Activate
// O jogador usa o monitor parado: a primeira câmera do grupo que não está desligada; com ela, a câmera do jogador fica
// sem atualização (Monitor_Update a põe na câmera de segurança), o "usar" fica marcado como já tratado, o jogador
// parado e o HUD mostra o painel 16 no lugar do 0 e do 1.
void Monitor_Activate(obj_tag* obj, obj_tag* by) {
    if (obj->state != 0 || by->type != 3) return;
    BLData* b = bloke_of(by);
    MONITOR_tag* m = monitor_of(obj);
    m->viewer = glb_viewer[b->player];
    obj_tag* cam = find_camera(CameraList.first, obj->unknown_f6);
    m->camera = cam;
    if (cam == nullptr) return;
    obj->state = 2;
    m->player = by;
    m->index = uint16_t(int16_t(b->player));
    if (m->viewer != nullptr) m->viewer->cel = nullptr;
    Camera_SetUpdator(m->index, by, nullptr);
    Input_SetAction(int16_t(b->player), 0xe, 1);
    Player_Disable(by, 0);
    hud_security_view(b, true);
    ++g_security_stats.viewed;
}

// [decomp] 0x001afcc8 Monitor_Update
// O dano dos tiros quebra o monitor destrutível (LOD seguinte, a explosão 0x6000027 e o jogador de volta). Estado 2
// (mostrando): uma câmera desligada passa à próxima do grupo (dando a volta na lista); usar passa à seguinte (sem dar a
// volta); sem câmera, o jogador volta (estado 9, uma espera de 6 ticks até poder usar de novo); com câmera, o canal de
// "sendo vista" liga e a matriz dela vai para a câmera do jogador.
void Monitor_Update(obj_tag* obj) {
    MONITOR_tag* m = monitor_of(obj);
    if (m->health != 0.0f) {
        const float damage = Collide_GetDamageNObjects(obj->hits, nullptr, nullptr, 0);
        const float before = m->health;
        m->health = before - damage;
        if (before - damage < 0.0f && obj->state != 3) {
            m->health = 0.0f;
            Control_NextLOD(obj);
            obj->state = 3;
            ++g_security_stats.broken;
            if (m->player != nullptr) give_back(m, m->player);
            Explode_Create(obj, &obj->sphere, &CONSTANT_UP1, obj->sphere.w * 6.0f, obj->sphere.w, 0x6000027, 0.5f, 200,
                           200, 0, nullptr, 0);
        }
    }
    switch (int16_t(obj->state)) {
        case 0:
        case 3:
            return;
        case 2:
            break;
        case 4:
            obj->state = 0;
            return;
        default:
            obj->state = uint16_t(obj->state - 1);
            return;
    }
    const uint16_t group = obj->unknown_f6;
    if (obj_tag* cam = m->camera; cam != nullptr && cam->state == 3) {
        cam->unknown_f8 = 1;
        obj_tag* next = nullptr;
        if (cam->data != nullptr) next = find_camera(sensor_of(cam)->node.next, group);
        if (next == nullptr) next = find_camera(CameraList.first, group);
        m->camera = next;
    }
    if (Input_Action(int16_t(m->index), 0xe, 4) != 0) {
        obj_tag* cam = m->camera;
        obj_tag* next = nullptr;
        if (cam != nullptr) {
            cam->unknown_f8 = 1;
            if (cam->data != nullptr) next = find_camera(sensor_of(cam)->node.next, group);
        }
        m->camera = next;
        if (m->viewer != nullptr) m->viewer->cel = nullptr;
        if (next != nullptr) ++g_security_stats.viewed;
    }
    if (m->camera == nullptr) {
        give_back(m, m->player);
        m->player = nullptr;
        obj->state = 9;
        return;
    }
    SENSOR* s = sensor_of(m->camera);
    if (s->view_channel != 0) switch_channels[s->view_channel] = 1;
    if (m->viewer != nullptr) Mat_Copy(&m->camera->matrix, &m->viewer->camera);
    s->viewed = 1;
}

int Security_Describe(char* out, size_t size) {
    int cams[4] = {}, alarms = 0, monitors = 0, lamps[4] = {}, mines = 0;
    const obj_tag* shown = nullptr;
    const obj_tag* showing = nullptr;
    const obj_tag* gun = nullptr;
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if ((o->life & 1) != 0) continue;
        switch (o->type) {
            case 0x37: {
                const SENSOR* s = static_cast<const SENSOR*>(o->data);
                if (o->state < 4) ++cams[o->state];
                alarms += s->alarm != 0 && switch_channels[s->alarm] != 0;
                break;
            }
            case 0x38:
                ++monitors;
                if (o->state == 2) {
                    showing = o;
                    shown = static_cast<const MONITOR_tag*>(o->data)->camera;
                }
                break;
            case 0x22:
                if (static_cast<const SEARCHLIGHT_tag*>(o->data)->kind == 1 && o->state < 4) ++lamps[o->state];
                break;
            case 0x4b:
                ++mines;
                break;
            case 0x47:
                if (o->state == 1) gun = o;
                break;
            default:
                break;
        }
    }
    char view[96] = "-";
    if (showing != nullptr && shown != nullptr) {
        std::snprintf(view, sizeof(view), "%.2f,%.2f,%.2f(grupo_%u)", shown->matrix.m[3][0], shown->matrix.m[3][1],
                      shown->matrix.m[3][2], showing->unknown_f6);
    }
    char mounted[64] = "-";
    if (gun != nullptr) {
        const GUNIMP_tag* g = static_cast<const GUNIMP_tag*>(gun->data);
        std::snprintf(mounted, sizeof(mounted), "calor_%.0f_quente_%u_tiros_%u", g->heat, g->overheated,
                      g_gunimp_stats.shots);
    }
    int deploy[4] = {};
    for (uint8_t m : MissileDeploy) {
        if (m < 4) ++deploy[m];
    }
    return std::snprintf(out, size,
                         "seguranca cameras %d/%d/%d/%d alarmes %d monitores %d mostrando %s holofotes %d/%d/%d/%d "
                         "minas %d explodidas %u canhao %s misseis %d/%d/%d",
                         cams[0], cams[1], cams[2], cams[3], alarms, monitors, view, lamps[0], lamps[1], lamps[2],
                         lamps[3], mines, g_hazard_stats.mines_blown, mounted, deploy[1], deploy[2], deploy[3]);
}

void security_debug_power_on() {
    std::memset(SSysItems, 0, sizeof(SSysItems));
    SSysDelayTimer = 0.0f;
    g_security_stats = {};
}
