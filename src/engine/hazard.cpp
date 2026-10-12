// Minas, atiradores e o tremor. Ver hazard.h.
//
// Divergências do original (só no port; também nas funções):
// - Mine_Update chama Explode_Create (com o raio da mina nos dois raios, a própria mina como causadora e material 0),
//   que faz exatamente o que o original faz inline; a única diferença é a contagem do port (g_explode_stats.created).
// - Apocalypse_Create sem modelo deixa os flags 0 (o original leria o endereço 0x44).
// - Os canais dos parâmetros passam por & 0xff ao indexar switch_channels (256 canais; o original usaria o short
//   inteiro; os do disco cabem).

#include "engine/hazard.h"

#include <cstring>

#include "engine/bullet.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/vecmath.h"
#include "engine/view.h"

HazardStats g_hazard_stats;

// [decomp] 0x0017ce28 Mine_Create
// A mina: a posição de casa, o dano (parâmetro 0), o raio (1), o giro (2), o balanço (3 e 4) e o script da explosão (5);
// fica referenciada (Control_ObjectReferenced) até explodir.
obj_tag* Mine_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x30, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_hazard_stats.mines;
    auto* m = static_cast<MINE_tag*>(obj->data);
    m->home = *pos;
    m->damage = float(level->params[0]);
    m->radius = float(level->params[1]);
    m->spin = float(level->params[2]) * 0.0002f;
    m->bob_rate = float(level->params[3]) * 0.0002f;
    m->bob = float(level->params[4]) * 0.1f;
    m->script = level->params[5];
    obj->type = 0x4b;
    obj->glist = glist;
    build_LinkToRoom(obj, 0, glb_world);
    Control_ObjectReferenced(obj, 1);
    return obj;
}

// [decomp] 0x0017c930 Mine_Update
// Gira, balança e, com qualquer objeto na lista de acertos, explode (raio abaixo de 0,5: sem dano) e some.
void Mine_Update(obj_tag* obj) {
    auto* m = static_cast<MINE_tag*>(obj->data);
    obj->rot.y = obj->rot.y + m->spin * FRAME_RATE_MUL;
    const float s = PS2Sinf(float(GameState.frame) * m->bob_rate);
    obj->mode |= 0x20;
    obj->pos.y = m->home.y + s * m->bob;
    HITDATA_tag* h = obj->hits;
    while (h != nullptr && h->obj == nullptr) h = h->next;
    if (h == nullptr) return;
    // O original faz o mesmo que Explode_Create aqui dentro (ver o cabeçalho).
    Explode_Create(obj, &obj->pos, &CONSTANT_UP1, m->radius, m->radius, m->script, m->damage, 0, 0, 0, obj, 0);
    ++g_hazard_stats.mines_blown;
    obj->life |= 1;
}

// [decomp] 0x0017d030 Shooter_Create
// O atirador: o modelo, o canal (parâmetro 0, em obj_tag::state) e a arma (parâmetro 1, em obj_tag::unknown_f6).
obj_tag* Shooter_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_hazard_stats.shooters;
    obj->glist = glist;
    obj->type = 0x4e;
    build_LinkToRoom(obj, 0, glb_world);
    obj->state = uint16_t(level->params[0]);
    obj->unknown_f6 = uint16_t(level->params[1]);
    return obj;
}

// [decomp] 0x0017d0c0 Shooter_Update
// Com o canal ligado, um tiro da arma pela frente do objeto (para baixo se a classe da arma for 0x3b) e some.
void Shooter_Update(obj_tag* obj) {
    if (switch_channels[uint8_t(int16_t(obj->state))] == 0) return;
    weapon_definition_tag* w = weapon_def(int16_t(obj->unknown_f6));
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->matrix);
    if (w->klass == 0x3b) {
        dir.x = 0.0f;
        dir.y = -1.0f;
        dir.z = 0.0f;
    }
    Bullet_init(0, nullptr, obj, w, &obj->pos, &dir, 0xffff);
    ++g_hazard_stats.shots;
    obj->life |= 1;
}

// [decomp] 0x0018dcc0 Apocalypse_Create
// O tremor (não entra em sala nenhuma): o canal (parâmetro 0), a duração em segundos (1, guardada em ticks a 60 Hz), a
// vibração (2), o sacudir (3) e o clarão (4).
obj_tag* Apocalypse_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x10, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_hazard_stats.quakes;
    obj->glist = glist;
    auto* a = static_cast<APOCALYPSE_tag*>(obj->data);
    obj->type = 0x54;
    obj->flags = glist != nullptr ? glist->flags : 0;
    a->channel = uint16_t(level->params[0]);
    a->total = float(level->params[1]) * 60.0f;
    a->left = float(level->params[1]) * 60.0f;
    a->rumble = int16_t(level->params[2]);
    a->shake = uint8_t(level->params[3]);
    a->on = 0;
    a->flash = uint8_t(level->params[4]);
    return obj;
}

// [decomp] 0x0018d4f0 Apocalypse_Update
// Com o canal ligado uma vez, conta o tempo: vibração, sacudir e clarão proporcionais ao que falta; no fim some.
void Apocalypse_Update(obj_tag* obj) {
    auto* a = static_cast<APOCALYPSE_tag*>(obj->data);
    if (switch_channels[uint8_t(int16_t(a->channel))] != 0) {
        if (a->on == 0) ++g_hazard_stats.quakes_on;
        a->on = 1;
    }
    if (a->on == 0) return;
    const float left = a->left - FRAME_RATE_MUL;
    a->left = left;
    if (left <= 0.0f) {
        obj->life |= 1;
        return;
    }
    if (a->rumble != 0) Input_RumbleStart(0, int32_t(left), int32_t(float(a->rumble) * left / a->total));
    if (a->shake != 0 && glb_viewer[0] != nullptr) glb_viewer[0]->shake = (a->left * 5.0f) / a->total;
    if (a->flash != 0) {
        float f = (a->left * 382.0f) / a->total;
        if (255.0f < f) f = 255.0f;  // min.S
        Player_SetFlashBang(glb_blokes[0], a->left, uint8_t(int32_t(f)));
    }
}

void hazard_debug_power_on() { g_hazard_stats = {}; }
