// Port do script player (SP_*, 0x001db9a8-0x001dcc68 do ACTION.ELF) e dos objetos que dirigem scripts:
// interruptores (Switch_*) e caixas de fusíveis (FuseBox_*). Ver script.h.

#include <cstring>

#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/input.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

LLISTINFO_tag SwitchList;
LLISTINFO_tag CameraList;

namespace {

obj_tag* HitObjList_171[0x40];  // objetos que acertaram as entidades (SP_GetHitDamage)

SCRIPTPLAYER_tag* sp_of(obj_tag* obj) { return static_cast<SCRIPTPLAYER_tag*>(obj->data); }
SCRIPTINFO* current(SCRIPTPLAYER_tag* sp) { return sp->scripts[sp->cur]; }
// O hash do outro script (o que não está em uso).
uint32_t other_hash(const SCRIPTPLAYER_tag* sp) { return sp->hashes[sp->cur != 0 ? 0 : 1]; }

bool multiplayer() {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    return mp != 0;
}

}  // namespace

// [decomp] 0x001db9a8 SP_CreateScriptPlayer
// Objeto do tipo 0x1b com os dois scripts do posicionamento (pelo menos um). Sem gatilhos começa em
// laço solto (estado 6); o modo 1 carrega no primeiro tick, outros modos já carregam (SP_LoadScript).
obj_tag* SP_CreateScriptPlayer(_VECTOR* pos, _VECTOR* rot, level_tag* level, obj_tag* owner,
                               void (*callback)(obj_tag*, uint16_t, void*), void* user) {
    if (level->params[0] == 0 && level->params[1] == 0) return nullptr;
    obj_tag* obj = control_create_object(0x3c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    sp->hashes[0] = level->params[0];
    sp->hashes[1] = level->params[1];
    sp->modes[0] = uint16_t(level->params[2]);
    sp->modes[1] = uint16_t(level->params[3]);
    sp->one_shot = uint8_t(level->params[4]);
    sp->triggers = uint16_t(level->params[5]);
    sp->channel = uint16_t(level->params[6]);
    sp->damage_needed = float(level->params[7]);
    sp->weapon = uint8_t(level->params[8]);
    sp->enable = uint16_t(level->params[9]);
    sp->owner = owner;
    sp->callback = callback;
    sp->user = user;
    sp->state = 0;
    sp->cooldown = 0;
    sp->cur = 0;
    sp->scripts[0] = nullptr;
    sp->scripts[1] = nullptr;
    obj->type = 0x1b;
    switch_channels[sp->channel] = 0;
    sp->load_requested = 0;
    if (sp->triggers == 0) sp->state = 6;
    if (sp->modes[0] == 1) {
        sp->state = 7;
    } else if (sp->modes[0] != 0) {
        SP_LoadScript(obj, sp);
    }
    if (sp->scripts[0] != nullptr) {
        const float scale = level->params[10] == 0 ? 1.0f : float(level->params[10]) * 0.01f;
        sp->scripts[0]->scale = scale;
        if (sp->scripts[1] != nullptr) sp->scripts[1]->scale = scale;
    }
    ++g_script_stats.players;
    return obj;
}

// [decomp] 0x001dbc08 SP_LoadScript
// Carrega a seção da cena (se o script A é de uma seção carregável), cria ou rebobina o script em uso e
// o deixa conforme o modo: 1 tocando (estado 2), 8 em laço (6), outros com um tick e parado (1).
void SP_LoadScript(obj_tag* obj, SCRIPTPLAYER_tag* sp) {
    const uint32_t hash = sp->hashes[0];
    const uint8_t section = isLoadable(hash);
    if (section != 0) {
        Input_RumblePause();
        PS2_SFXWaitTillFinished();
        SFXSuspendFileAccess();
        PS2_SFXWaitTillFinished();
        LoadableLoad(hash, section);
        SFXUnSuspendFileAccess();
        PS2_ResetFrameCount();
        ++g_script_stats.loadables;
    }
    if (sp->scripts[sp->cur] == nullptr) {
        sp->scripts[sp->cur] =
            Script_Load(sp->hashes[sp->cur], &obj->pos, &obj->rot, nullptr, sp, sp->callback, sp->user);
    } else {
        Script_Set2Start(sp->scripts[sp->cur]);
    }
    sp->callback = nullptr;
    if (sp->modes[sp->cur] == 1) {
        Script_Play(current(sp), 0);
        sp->state = 2;
    } else if (sp->modes[sp->cur] == 8) {
        Script_Play(current(sp), 1);
        sp->state = 6;
    } else {
        Script_Play(current(sp), 0);
        Script_Update(current(sp));
        Script_Stop(current(sp));
        sp->state = 1;
    }
    Script_Update(current(sp));
}

// [decomp] 0x001dbda8 SP_Update
// Um tick: o script em uso anda; os gatilhos (SP_Hit, SP_UnPause/SP_Activate) e a máquina de estados
// decidem tocar, trocar de script ou soltar uma pausa; o canal do player é escrito quando dispara; tiros
// nas entidades de um script pausado somam dano até damage_needed.
void SP_Update(obj_tag* obj) {
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    if (sp->hidden != 0) return;
    if (sp->enable != 0 && switch_channels[sp->enable] == 0) return;
    Script_Update(current(sp));
    uint16_t hit = 0;
    if (sp->cooldown == 0) hit = SP_Hit(sp);
    uint8_t value = sp->channel != 0 ? switch_channels[sp->channel] : 0;
    if (sp->cooldown != 0) --sp->cooldown;
    if (sp->unpause != 0 && sp->cooldown == 0) hit |= 4;
    sp->unpause = 0;
    const bool fired = (hit & sp->triggers) != 0;
    switch (sp->state) {
        case 0: {
            if (!fired) break;
            if (!multiplayer()) {
                // Só no port: sem jogador, conta como vivo (o original leria glb_blokes[0] nulo).
                bool ok = glb_blokes[0] == nullptr || !(glb_blokes[0]->health <= 0.0f);
                if (Mission_Status() != 0 && Mission_Status() != 1) ok = false;
                if (sp->hashes[0] == 0x6000994 && Get_Music_Event(2) != 0) ok = false;
                if (!ok) {
                    sp->state = 5;
                    break;
                }
            }
            if (isLoadable(sp->hashes[0]) != 0 && sp->load_requested == 0) {
                sp->load_requested = 1;
                GameFlow_PushState(0xd, 20.0f, 0xff);
                return;
            }
            SP_LoadScript(obj, sp);
            Script_Play(current(sp), 0);
            sp->state = 2;
            break;
        }
        case 1:
            if (!fired) break;
            sp->state = 2;
            sp->cooldown = 5;
            value = 1;
            Script_Play(current(sp), sp->modes[sp->cur] == 8 ? 1 : 0);
            break;
        case 2:
            if ((sp->triggers & 1) != 0 && sp->channel != 0 && switch_channels[sp->channel] != 0) SP_UnPause(obj);
            if (Input_Action(-1, 0x19, 1) != 0 && Script_IsDeathNIS(sp->hashes[0]) == 0) Script_FFwd(current(sp), 1);
            if (Script_IsPlaying(current(sp)) != 0) break;
            if (sp->one_shot != 0) {
                sp->state = 5;
                break;
            }
            if (other_hash(sp) != 0) {
                sp->cur = sp->cur == 0 ? 1 : 0;
                SP_LoadScript(obj, sp);
            }
            sp->state = 3;
            break;
        case 3:
            if (!fired) break;
            sp->state = 4;
            sp->cooldown = 5;
            value = 0;
            Script_Play(current(sp), sp->modes[sp->cur] == 8 ? 1 : 0);
            break;
        case 4:
            if (Script_IsPlaying(current(sp)) != 0) break;
            if (other_hash(sp) != 0) {
                sp->cur = sp->cur == 0 ? 1 : 0;
                SP_LoadScript(obj, sp);
            }
            sp->state = 1;
            break;
        case 5:
            obj->life |= 1;
            break;
        case 6:
            if (!fired) break;
            if (current(sp) != nullptr) current(sp)->wait = 0;
            if (other_hash(sp) == 0) break;
            // Troca para o outro script (o original solta sempre o script A).
            Script_Free(sp->scripts[0]);
            sp->scripts[0] = nullptr;
            sp->cur = sp->cur == 0 ? 1 : 0;
            SP_LoadScript(obj, sp);
            sp->state = 2;
            break;
        case 7:
            SP_LoadScript(obj, sp);
            break;
        default:
            break;
    }
    if (sp->channel != 0 && fired) switch_channels[sp->channel] = value != 0 ? 1 : 0;
    SCRIPTINFO* si = current(sp);
    if (si == nullptr || si->wait == 0 || sp->damage_needed == 0.0f) return;
    int8_t by_player = 0;
    const float damage = SP_GetHitDamage(obj, nullptr, 0, nullptr, 0, &by_player);
    if (damage == 0.0f) return;
    sp->damage += damage;
    if (by_player != 0) {
        const uint32_t h = sp->hashes[0];
        if (h == 0x60007e2 || (0x60007e4 <= h && h <= 0x60007ea)) PlrStat_LogShotHitScenery(0);
    }
    if (sp->damage_needed <= sp->damage) {
        SP_UnPause(obj);
        sp->damage -= sp->damage_needed;
    }
}

// [decomp] 0x001dc338 SP_Activate
// O jogador usou (E) uma entidade de script (obj_tag::script aponta para o player). Interruptores só
// viram pela frente; com o gatilho 4, solta a pausa (com o gatilho 1, só se o canal estiver ligado).
void SP_Activate(obj_tag* obj, obj_tag* by) {
    if (obj == nullptr) return;
    auto* sp = static_cast<SCRIPTPLAYER_tag*>(obj->script);
    if (sp == nullptr) return;
    obj_tag* owner = sp->owner;
    if (owner != nullptr) {
        if (owner->type == 0x36) {
            GT_Activate(owner, by);
        } else if (owner->type == 0x39) {
            _VECTOR dir;
            Mat_GetDir(&dir, &obj->matrix);
            const float dx = by->matrix.m[3][0] - obj->matrix.m[3][0];
            const float dy = by->matrix.m[3][1] - obj->matrix.m[3][1];
            const float dz = by->matrix.m[3][2] - obj->matrix.m[3][2];
            const float dot = dir.x * dx + dir.y * dy + dir.z * dz;
            if (!(0.0f <= dot)) return;
            Switch_Activate(sp->owner, by);
            return;
        }
    }
    if ((sp->triggers & 4) == 0) return;
    if ((sp->triggers & 1) != 0 && sp->channel != 0 && switch_channels[sp->channel] == 0) return;
    sp->unpause = 1;
    if (current(sp) != nullptr) current(sp)->wait = 0;
}

// [decomp] 0x001dc470 SP_Hit
// Bits dos gatilhos deste tick: 1 o canal ligado, 2 tiro (da arma pedida) numa entidade, 8 o jogador e
// 0x10 um personagem encostando numa entidade. Com só o gatilho de tiro, ignora os tiros das armas
// com a flag 0x106 (Collide_FilterBullets).
uint16_t SP_Hit(SCRIPTPLAYER_tag* sp) {
    uint16_t bits = 0;
    SCRIPTINFO* si = current(sp);
    if (sp->channel != 0) bits = switch_channels[sp->channel] != 0 ? 1 : 0;
    if (si == nullptr) return bits;
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        obj_tag* o = Script_GetObj(si, i);
        if (o == nullptr || o->hits == nullptr) continue;
        if (sp->weapon == 0 && sp->triggers == 2) Collide_FilterBullets(&o->hits, 0x106);
        for (HITDATA_tag* h = o->hits; h != nullptr; h = h->next) {
            obj_tag* by = h->obj;
            if (by == nullptr) continue;
            if (by->type == 3) {
                bits |= 8;
            } else if (by->type == 2) {
                bits |= 0x10;
            } else if (by->type == 5) {
                if (sp->weapon == 0 ||
                    uint16_t(static_cast<BU_tag*>(by->data)->weapon->id) == uint16_t(sp->weapon)) {
                    bits |= 2;
                }
            }
        }
    }
    return bits;
}

// [decomp] 0x001dc5f8 SP_GetHitDamage
// Dano dos acertos nas entidades do script em uso (sem os tiros das armas em "filter"); quem atirou
// vai para objs (até max) e by_player diz se foi o jogador.
float SP_GetHitDamage(obj_tag* obj, obj_tag** objs, uint16_t max, uint16_t* count, uint16_t filter,
                      int8_t* by_player) {
    if (count != nullptr) *count = 0;
    if (by_player != nullptr) *by_player = 0;
    float total = 0.0f;
    if (obj == nullptr) return total;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    SCRIPTINFO* si = current(sp);
    if (si == nullptr || si->stream_count == 0) return total;
    for (uint16_t i = 0;; ++i) {
        obj_tag* o = Script_GetObj(current(sp), i);
        if (o != nullptr) {
            Collide_FilterBullets(&o->hits, filter);
            uint16_t n = 0;
            total += Collide_GetDamageNObjects(o->hits, HitObjList_171, &n, 0x40);
            for (uint16_t k = 0; k < n; ++k) {
                obj_tag* hit = HitObjList_171[k];
                if (hit->type != 5) continue;
                obj_tag* shooter = static_cast<BU_tag*>(hit->data)->from;
                if (count != nullptr && objs != nullptr && *count < max) {
                    objs[*count] = shooter;
                    ++*count;
                }
                if (shooter != nullptr && shooter->type == 3 && by_player != nullptr) *by_player = 1;
            }
        }
        if (current(sp)->stream_count <= uint16_t(i + 1)) break;
    }
    return total;
}

// [decomp] 0x001dc7e0 SP_Create
// Script player criado por outro objeto: um posicionamento falso com os scripts, o modo nos dois
// scripts, "para no fim" e os gatilhos.
obj_tag* SP_Create(_VECTOR* pos, _VECTOR* rot, uint32_t hash, uint32_t hash_b, uint32_t mode, uint32_t one_shot,
                   obj_tag* owner, void (*callback)(obj_tag*, uint16_t, void*), void* user, uint32_t triggers) {
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    level.params[5] = triggers;
    if (rot == nullptr) rot = const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR);
    level.params[0] = hash;
    level.params[1] = hash_b;
    level.params[2] = mode;
    level.params[3] = mode;
    level.params[4] = one_shot;
    return SP_CreateScriptPlayer(pos, rot, &level, owner, callback, user);
}

// [decomp] 0x001dc8b0 SP_Delete
void SP_Delete(obj_tag* obj) {
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    Script_Free(sp->scripts[0]);
    sp->scripts[0] = nullptr;
    Script_Free(sp->scripts[1]);
    sp->scripts[1] = nullptr;
    obj->life |= 1;
}

// [decomp] 0x001dc900 SP_GetState
// 2 tocou ou acabou (estados 3 e 5); 1 o script em uso está rodando (sem pausa); 0 pausado ou sem script.
uint8_t SP_GetState(obj_tag* obj) {
    if (obj == nullptr) return 0;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    if (sp->state == 5 || sp->state == 3) return 2;
    SCRIPTINFO* si = current(sp);
    if (si == nullptr) return 0;
    return si->wait == 0 ? 1 : 0;
}

// [decomp] 0x001dc958 SP_RemoveObj
// Chamada por control_delete_object para objetos com obj_tag::script.
void SP_RemoveObj(obj_tag* obj, void* sp) {
    if (obj == nullptr || sp == nullptr) return;
    Script_RemoveObj(obj, current(static_cast<SCRIPTPLAYER_tag*>(sp)));
}

// [decomp] 0x001dc990 SP_SetPos
void SP_SetPos(obj_tag* obj, _VECTOR* pos) {
    if (obj == nullptr) return;
    SCRIPTINFO* si = current(sp_of(obj));
    if (si == nullptr) return;
    std::memcpy(&si->base.m[3][0], pos, sizeof(_VECTOR));
    Script_SetPosRot(si, &si->base);
}

// [decomp] 0x001dc9e8 SP_SetPosRot
void SP_SetPosRot(obj_tag* obj, MATRIX* m) {
    if (obj == nullptr) return;
    SCRIPTINFO* si = current(sp_of(obj));
    if (si == nullptr) return;
    Mat_Copy(m, &si->base);
    Script_SetPosRot(si, &si->base);
}

// [decomp] 0x001dca58 SP_UnPause
// Solta a pausa do script em uso e dispara o gatilho 4 no próximo tick.
void SP_UnPause(obj_tag* obj) {
    if (obj == nullptr) return;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    sp->unpause = 1;
    if (current(sp) != nullptr) current(sp)->wait = 0;
}

// [decomp] 0x001dca90 SP_SetColour
void SP_SetColour(obj_tag* obj, uint8_t r, uint8_t g, uint8_t b) {
    if (obj == nullptr) return;
    SCRIPTINFO* si = current(sp_of(obj));
    if (si != nullptr) Script_SetColour(si, r, g, b);
}

// [decomp] 0x001dcad8 SP_SwitchToScript
// Troca para o outro script (se houver) com outro modo e callback; o atual é solto e esquecido.
void SP_SwitchToScript(obj_tag* obj, uint16_t mode, void (*callback)(obj_tag*, uint16_t, void*)) {
    if (obj == nullptr) return;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    if (other_hash(sp) == 0) return;
    Script_Free(current(sp));
    sp->scripts[sp->cur] = nullptr;
    sp->hashes[sp->cur] = 0;
    const uint16_t next = sp->cur == 0 ? 1 : 0;
    sp->cur = next;
    sp->modes[next] = mode;
    sp->callback = callback;
    SP_LoadScript(obj, sp);
    if (sp->one_shot == 0) sp->state = sp->modes[sp->cur] == 1 ? 2 : 3;
}

// [decomp] 0x001dcbf0 SP_HideObj
void SP_HideObj(obj_tag* obj, int8_t hide) {
    if (obj == nullptr) return;
    SCRIPTPLAYER_tag* sp = sp_of(obj);
    sp->hidden = uint8_t(hide);
    Script_HideObj(current(sp), hide);
}

// [decomp] 0x001dcc30 SP_getScriptInfo
void* SP_getScriptInfo(obj_tag* obj) {
    if (obj == nullptr || obj->data == nullptr) return nullptr;
    return current(sp_of(obj));
}

// ---- interruptores e caixas de fusíveis ----

// [decomp] 0x001b0868 Sensor_Init
void Sensor_Init() {
    LList_Init(&CameraList, 100, 0);
    LList_Init(&SwitchList, 0x1c, 0);
}

// [decomp] 0x001adcc0 Switch_Create
// Interruptor: o objeto (tipo 0x39, sem modelo próprio) guarda o canal e o estado; o script player
// (modo 8, gatilho 4) anima a alavanca, parando nas pausas do script. Ligado de início, roda o script até
// a primeira pausa. Divergências: esse laço tem um limite (o original travaria num script sem pausa);
// um índice de SSysItems fora da tabela é ignorado; sem modelo (glist nulo), os flags ficam 0.
obj_tag* Switch_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x1c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* sw = static_cast<SWITCH_tag*>(obj->data);
    sw->obj = obj;
    sw->channel = uint16_t(level->params[0]);
    const uint32_t script = level->params[1];
    sw->timer = uint16_t(level->params[2]);
    sw->initial = uint16_t(level->params[3]);
    sw->once = uint8_t(level->params[4]);
    sw->lock = uint16_t(level->params[5]);
    const uint32_t item = level->params[6];
    if (item != 0 && item * 8 + 4 < sizeof(SSysItems)) SSysItems[item * 8 + 4] = uint8_t(sw->channel);
    obj->type = 0x39;
    obj->state = sw->initial;
    obj->glist = nullptr;
    obj->flags = glist != nullptr ? glist->flags : 0;
    build_LinkToRoom(obj, 0, glb_world);
    if (sw->channel != 0) {
        switch_channels[sw->channel & 0xff] = uint8_t(sw->initial);
        if (sw->initial != 0) switch_channels_time[sw->channel & 0xff] = GameState.frame;
    }
    sw->player = SP_Create(pos, rot, script, 0, 8, 0, obj, nullptr, nullptr, 4);
    if (sw->player == nullptr) {
        control_delete_object(obj);
        return obj;
    }
    SP_Update(sw->player);
    if (int16_t(obj->state) == 1) {
        SP_UnPause(sw->player);
        uint32_t guard = 0;
        while (SP_GetState(sw->player) != 0) {
            if (++guard > 100000) {
                ++g_script_stats.switch_loop_guard;
                break;
            }
            SP_Update(sw->player);
        }
    }
    LList_Add(&SwitchList, &sw->node);
    ++g_script_stats.switches;
    return obj;
}

// [decomp] 0x001adea8 Switch_Update
// O canal mudou por fora: o switch acompanha (anima). Com timer, volta sozinho depois de "timer" ticks
// fora do estado inicial (e inverte o canal).
void Switch_Update(obj_tag* obj) {
    auto* sw = static_cast<SWITCH_tag*>(obj->data);
    if (sw->once != 0 && sw->uses != 0) return;
    if (sw->channel != 0 && switch_channels[sw->channel & 0xff] != uint16_t(int16_t(obj->state))) {
        obj->state ^= 1;
        ++sw->uses;
        SP_UnPause(sw->player);
    }
    if (sw->timer == 0) return;
    if (switch_channels[sw->channel & 0xff] == sw->initial) return;
    const int16_t ticks = int16_t(obj->unknown_f6);
    obj->unknown_f6 = uint16_t(ticks + 1);
    if (!(int32_t(sw->timer) < int32_t(ticks))) return;
    ++sw->uses;
    SP_UnPause(sw->player);
    if (sw->channel != 0) {
        uint8_t& ch = switch_channels[sw->channel & 0xff];
        ch = ch == 0 ? 1 : 0;
        if (ch != 0) {
            obj->unknown_f6 = 0;
            return;
        }
        switch_channels_time[sw->channel & 0xff] = GameState.frame;
    }
    obj->unknown_f6 = 0;
}

// [decomp] 0x001adfe8 FuseBox_Update
// Fusíveis que levam um tiro (sem os das armas com a flag 0xfffd, o soco) quebram (próximo LOD) e saem
// da lista; os da lista 1 ferem o jogador. Sem fusível inteiro numa lista que teve algum, os canais da
// lista ligam (uma vez). Peculiaridade mantida: quebrar um da lista 1 zera o segundo canal da lista 0.
void FuseBox_Update(obj_tag* obj) {
    auto* fb = static_cast<FUSEBOX_tag*>(obj->data);
    for (uint16_t list = 0; list < 2; ++list) {
        uint16_t alive = 0;
        for (uint16_t i = 0; i < 10; ++i) {
            P32<obj_tag>& fuse = fb->fuses[list][i];
            obj_tag* f = fuse;
            if (f == nullptr) continue;
            Collide_FilterBullets(&f->hits, 0xfffd);
            if (f->hits == nullptr) {
                ++alive;
                continue;
            }
            if (Control_NextLOD(f) != 0) Effect_RemoveSObjs(f);
            fuse = nullptr;
            if (list == 1) {
                // Só no port: sem jogador não chama (o original leria nulo).
                if (fb->channel_a[1] == 0 && glb_players[0] != nullptr) Player_Hurt(glb_players[0], 5.0f, &f->sphere);
                fb->channel_b[0] = 0;
            }
        }
        if (alive != 0 || fb->count[list] == 0) continue;
        uint16_t& a = fb->channel_a[list];
        if (a != 0) {
            switch_channels[a & 0xff] = 1;
            switch_channels_time[a & 0xff] = GameState.frame;
            a = 0;
        }
        uint16_t& b = fb->channel_b[list];
        if (b != 0) {
            switch_channels[b & 0xff] = 1;
            switch_channels_time[b & 0xff] = GameState.frame;
            b = 0;
        }
    }
}

// [decomp] 0x001b02a8 Switch_Activate
// O jogador virou o switch (pela frente, SP_Activate): inverte o canal se o switch está parado no
// estado do canal, não travado, e o script está numa pausa. O jogador não vira o canal 0x61.
// Peculiaridade mantida: switch_channels_time só é escrito quando o canal desliga.
void Switch_Activate(obj_tag* obj, obj_tag* by) {
    auto* sw = static_cast<SWITCH_tag*>(obj->data);
    if (sw->once != 0 && sw->uses != 0) return;
    if (by->type == 3 && sw->channel == 0x61) return;
    if (int16_t(obj->state) != switch_channels[sw->channel & 0xff]) return;
    if (sw->lock != 0 && switch_channels[sw->lock & 0xff] != 0) return;
    if (SP_GetState(sw->player) != 0) return;
    if (sw->channel == 0) return;
    uint8_t& ch = switch_channels[sw->channel & 0xff];
    ch = ch == 0 ? 1 : 0;
    if (ch == 0) switch_channels_time[sw->channel & 0xff] = GameState.frame;
}

// [decomp] 0x001b0398 Switch_GetNearest
// O switch mais perto (a menos de sqrt(9999.9)).
obj_tag* Switch_GetNearest(_VECTOR* pos) {
    obj_tag* best = nullptr;
    float best_d = 9999.9f;
    for (LLNODE_tag* n = SwitchList.first; n != nullptr; n = n->next) {
        obj_tag* o = reinterpret_cast<SWITCH_tag*>(n)->obj;
        const float d = Vec_SqDist3D(pos, &o->pos);
        if (d < best_d) {
            best = o;
            best_d = d;
        }
    }
    return best;
}

// [decomp] 0x001b0430 Switch_GetSwitchChannel
uint16_t Switch_GetSwitchChannel(obj_tag* obj) {
    if (obj == nullptr) return 0;
    return static_cast<SWITCH_tag*>(obj->data)->channel;
}

// [decomp] 0x001b0448 FuseBox_Create
// Caixa de fusíveis: o script (parâmetro 0, modo 0x40, sem gatilhos) cria os fusíveis e os registra
// pelo evento 15 (FuseBox_Register).
obj_tag* FuseBox_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x60, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* fb = static_cast<FUSEBOX_tag*>(obj->data);
    fb->channel_a[0] = uint16_t(level->params[1]);
    fb->channel_a[1] = uint16_t(level->params[2]);
    fb->channel_b[0] = uint16_t(level->params[3]);
    fb->channel_b[1] = uint16_t(level->params[4]);
    obj->type = 0x3c;
    obj->state = 0;
    obj->glist = nullptr;
    obj->flags = glist != nullptr ? glist->flags : 0;  // só no port: sem modelo, 0
    build_LinkToRoom(obj, 0, glb_world);
    fb->player = SP_Create(pos, rot, level->params[0], 0, 0x40, 0, obj, FuseBox_Register, fb, 0);
    ++g_script_stats.fuseboxes;
    return obj;
}

// [decomp] 0x001b0970 FuseBox_Register
// Callback do evento 15: põe o fusível (o objeto anterior do script) na lista "list" (0 ou 1).
void FuseBox_Register(obj_tag* fuse, uint16_t list, void* box) {
    if (box == nullptr || 1 < list) return;
    auto* fb = static_cast<FUSEBOX_tag*>(box);
    for (uint16_t i = 0; i < 10; ++i) {
        P32<obj_tag>& slot = fb->fuses[list][i];
        if (slot == fuse) return;
        if (slot == nullptr) {
            slot = fuse;
            ++fb->count[list];
            return;
        }
    }
}
