// Cadeados e alvos do Q-Worm. Ver ssys.h.
//
// Divergências do original (só no port):
// - PCQWorm_Create sem memória para o objeto: o original escreveria no objeto nulo; o port devolve nulo.
// - Lock_Update no teclado e no decodificador usa o objeto do jogador guardado em GlobalVars (+0) e o BLData (+4):
//   sem eles (o teclado aberto por outro caminho) o port não mexe no BLData nulo.

#include "engine/ssys.h"

#include <cstdio>
#include <cstring>

#include "engine/emitter.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/menu.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/spline.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/view.h"

char KeyCodes[kKeyCodes * 5];
SsysStats g_ssys_stats;

namespace {

LOCK_tag* lock_of(obj_tag* obj) { return static_cast<LOCK_tag*>(obj->data); }

// GlobalVars +0 (o objeto do jogador do teclado) e +4 (o BLData dele).
obj_tag* gv_player() {
    obj_tag* o;
    o = nf::p32_load<obj_tag>(GlobalVars);
    return o;
}
BLData* gv_bloke() {
    BLData* b;
    b = nf::p32_load<BLData>(GlobalVars + 4);
    return b;
}

// A arma do projétil (tipo 5) que acertou, ou -1.
int32_t bullet_weapon(const obj_tag* o) {
    if (o == nullptr || o->type != 5 || o->data == nullptr) return -1;
    const uint8_t* data = static_cast<const uint8_t*>(o->data);
    const weapon_definition_tag* w;
    w = nf::p32_load<const weapon_definition_tag>(data + 0x44);
    if (w == nullptr) return -1;
    return w->id;
}

obj_tag* bullet_shooter(const obj_tag* o) {
    obj_tag* from;
    from = nf::p32_load<obj_tag>(static_cast<const uint8_t*>(o->data) + 0x30);
    return from;
}

uint8_t clamp_byte(int32_t v) { return uint8_t(v < 0 ? 0 : (v < 0x100 ? v : 0xff)); }

void play_at(uint16_t sound, obj_tag* obj) { Sound_Play3D(sound, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0); }

// O código revelado: "Code: %c%c%c%c" (rótulo 0x2000014) na mensagem de informação.
void show_code(uint32_t index) {
    char* text = Txt_GetStringFromHeap(0);
    const char* format = Txt_BindLabel(0x2000014, 0);
    const char* k = KeyCodes + index * 5;
    if (text != nullptr && format != nullptr) {
        std::sprintf(text, format, uint32_t(uint8_t(k[0])), uint32_t(uint8_t(k[1])), uint32_t(uint8_t(k[2])),
                     uint32_t(uint8_t(k[3])));
    }
    Text_AddMsg(0, 0, 1, text, 0, 0xb4);
}

}  // namespace

// [decomp] 0x001ae1f8 Lock_Create
// Cadeado (tipo 0x3a): parâmetros 0-6 (ver LOCK_tag). Com um caminho, já começa no primeiro quadro dele. Fácil
// (parâmetro 6 na dificuldade 1): já aberto.
obj_tag* Lock_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x20, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    LOCK_tag* d = lock_of(obj);
    d->kind = uint16_t(level->params[0]);
    d->key = uint16_t(level->params[1]);
    d->param2 = level->params[2];
    d->sound_open = level->params[3] != 0 ? uint16_t(level->params[3]) : 0xffff;
    d->sound_locked = level->params[4] != 0 ? uint16_t(level->params[4]) : 0xffff;
    d->channel = uint16_t(level->params[5]);
    const int16_t easy = int16_t(level->params[6]);
    d->t = 0.0f;
    d->path = level->path;
    const uint32_t frames = uint16_t(level->path_count) == 0 ? 0 : uint16_t(level->path_count) - 1u;
    d->frames = frames;
    const float speed = float(frames) * REC_FRAME_RATE;
    d->speed = speed;
    d->menu = 0xffff;
    d->speed = speed * (FRAME_RATE / float(level->path_unknown));
    obj->type = 0x3a;
    obj->glist = glist;
    obj->state = 0;
    obj->mode |= 0x44;
    if (d->frames != 0) {
        Spline_Interp(d->path, 0, uint16_t(d->frames), 0.0f, &obj->matrix, nullptr, 2);
        obj->pos = {obj->matrix.m[3][0], obj->matrix.m[3][1], obj->matrix.m[3][2], obj->matrix.m[3][3]};
    }
    build_LinkToRoom(obj, 1, glb_world);
    if (easy != 0 && GameState.difficulty == 1) Lock_Unlock(glb_players[0], obj, 1);
    return obj;
}

// [decomp] 0x001ae3d8 Lock_Update
// Estados (obj_tag::state): 0 trancado, 1 abrindo (canal ligado, anda pelo caminho, troca o modelo), 2 aberto, 3 e 4 o
// teclado aberto, 5 o decodificador achando os algarismos. A cor do cadeado (obj_tag::unknown_103[1] e [2], verde e
// azul do tom) cai com o laser e volta devagar.
void Lock_Update(obj_tag* obj) {
    LOCK_tag* d = lock_of(obj);
    uint8_t& green = obj->unknown_103[1];
    uint8_t& blue = obj->unknown_103[2];
    int16_t& count = reinterpret_cast<int16_t&>(obj->unknown_f6);
    switch (obj->state) {
        case 0: {
            if (d->kind == 1) {
                // Laser: cada tiro das armas 0x4e (3) e 0x4f (10) esfria menos o vermelho.
                for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
                    const int32_t w = bullet_weapon(h->obj);
                    const int32_t heat = w == 0x4e ? 3 : (w == 0x4f ? 10 : 0);
                    if (heat == 0) continue;
                    count = 2;
                    Emitter_Create(0xc00000a, &h->pos, reinterpret_cast<_VECTOR*>(&h->plane), 0.0f, 0, 0, 0, 0.0f);
                    green = clamp_byte(int32_t(green) - heat);
                    blue = clamp_byte(int32_t(blue) - heat);
                    if (blue < 4) {
                        play_at(0x249, obj);
                        Lock_Unlock(bullet_shooter(h->obj), obj, 0);
                        return;
                    }
                }
                if (count == 0) {
                    blue = clamp_byte(int32_t(blue) + 1);
                    green = clamp_byte(int32_t(green) + 1);
                    return;
                }
                --count;
                return;
            }
            if (d->kind != 0 && d->kind != 2) return;
            // A chave abre na hora.
            if (d->key != 0 && glb_blokes[0] != nullptr &&
                reinterpret_cast<const uint8_t*>(glb_blokes[0])[0x70c + d->key] != 0) {
                Lock_Unlock(glb_players[0], obj, 0);
                obj->state = 1;
                return;
            }
            // Decodificador (armas 0x56 e 0x57): o primeiro tiro começa a decodificação.
            for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
                const int32_t w = bullet_weapon(h->obj);
                if (uint32_t(w - 0x56) < 2) ++count;
            }
            if (count != 0) {
                std::memcpy(GlobalVars + 8, KeyCodes + uint32_t(d->key) * 5, 5);
                GlobalVars[0x10] = '*';
                GlobalVars[0x12] = 4;
                GlobalVars[0x13] = 0;
                GlobalVars[0xd] = '*';
                GlobalVars[0xe] = '*';
                GlobalVars[0xf] = '*';
                obj->state = 5;
                count = 0;
            }
            return;
        }
        case 1: {
            if (d->channel != 0) {
                switch_channels[d->channel] = 1;
                switch_channels_time[d->channel] = GameState.frame;
            }
            float frames = 0.0f;
            if (d->frames != 0) {
                Spline_Interp(d->path, 0, uint16_t(d->frames), d->t, &obj->matrix, nullptr, 2);
                obj->mode |= 0x20;
                d->t = d->t + d->speed * FRAME_RATE_MUL;
                frames = float(d->frames);
            }
            if (frames <= d->t) {
                Control_NextLOD(obj);
                obj->state = 2;
            }
            return;
        }
        case 2:
            if (int32_t(blue) + 2 < 0x100) blue = uint8_t(blue + 2);
            if (green != 0xff) ++green;
            if (d->kind == 0 && d->channel != 0 && switch_channels[d->channel] == 0) Lock_ReLock(glb_players[0], obj);
            return;
        case 3: {
            // Usado sem a chave (Lock_Activate): o teclado numérico, com o jogador parado.
            std::memcpy(GlobalVars + 8, KeyCodes + uint32_t(d->key) * 5, 5);
            GlobalVars[0x12] = 4;
            GlobalVars[0x13] = 0;
            BLData* b = gv_bloke();
            if (b != nullptr) b->active = 0;
            obj->state = 4;
            d->menu = uint16_t(MenuManager_Create(0x80000003u, 0x40000021, b != nullptr ? b->player : 0, 1, 0x22, 0, 1));
            ++g_ssys_stats.keypads;
            if (KeyCodes[uint32_t(d->key) * 5 + 4] != 0) show_code(d->key);
            return;
        }
        case 4: {
            bool match = true;
            for (uint32_t i = 0; i < GlobalVars[0x12]; ++i) {
                if (GlobalVars[8 + i] != GlobalVars[0xd + i]) match = false;
            }
            if (MenuManager_Exists(0) == 0 && MenuManager_Exists(uint8_t(d->menu)) != 0) {
                MenuManager_Enable(1, uint8_t(d->menu));
            }
            BLData* b = gv_bloke();
            if (match && GlobalVars[0x13] == GlobalVars[0x12]) {
                if (b != nullptr) b->active = 1;
                Lock_Unlock(gv_player(), obj, 0);
                std::memset(GlobalVars, 0, 0x18);
                MenuManager_Delete(uint8_t(d->menu));
                obj->state = 1;
                play_at(d->sound_open, obj);
            } else if (MenuManager_Exists(uint8_t(d->menu)) == 0) {
                if (b != nullptr) b->active = 1;
                obj->state = 0;
            }
            return;
        }
        case 5: {
            // O decodificador: segurando o tiro no cadeado (0x56 +1, 0x57 +3 por tick), um algarismo de cada vez
            // (os errados piscam ao acaso); 4 x 40 ticks o primeiro, 3 x 40 o segundo... Sem tiros, volta 4 por tick.
            GlobalVars[0x14] = 1;
            int32_t hits = 0;
            for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
                const int32_t w = bullet_weapon(h->obj);
                if (w == 0x56) {
                    count = int16_t(count + 1);
                } else if (w == 0x57) {
                    count = int16_t(count + 3);
                } else {
                    continue;
                }
                ++hits;
            }
            if (hits == 0) {
                const int32_t v = int32_t(count) - 4;
                const int16_t c = int16_t(v < 0 ? 0 : (v < 0xbb9 ? v : 3000));
                count = c;
                if (c == 0) {
                    std::memset(GlobalVars, 0, 0x18);
                    GlobalVars[0x14] = 0;
                    obj->state = 0;
                }
                return;
            }
            const uint32_t at = GlobalVars[0x13] < 4 ? GlobalVars[0x13] : 3;
            if ((uint16_t(count) & 1) != 0) {
                const uint8_t digit = uint8_t(uint8_t(Rand_Rand(10)) + 0x30);
                GlobalVars[0xd + at] = digit < 0x30 ? uint8_t(0x30) : (0x39 < digit ? uint8_t(0x39) : digit);
            }
            if (int32_t((4 - at) * 0x28) < int32_t(count)) {
                GlobalVars[0xd + at] = GlobalVars[8 + at];
                ++GlobalVars[0x13];
                count = 5;
                Sound_Play(0x5db, 100.0f, 0, 0);
                ++g_ssys_stats.decrypted;
            }
            if (uint32_t(GlobalVars[0x13]) == uint32_t(GlobalVars[0x12]) + 1) {
                Lock_Unlock(glb_players[0], obj, 0);
                std::memset(GlobalVars, 0, 0x18);
                Sound_Play(0x5da, 100.0f, 0, 0);
            }
            return;
        }
        default:
            return;
    }
}

// [decomp] 0x001aed00 Lock_Activate
// Usar o cadeado (Player_Activate, pela frente), sem outro teclado aberto: trancado ou decodificando, com a chave
// abre; sem ela, o teclado (tipo 0: estado 3, guardando o jogador em GlobalVars) ou o som de trancado.
void Lock_Activate(obj_tag* lock, obj_tag* by) {
    if (lock == nullptr || by == nullptr || MenuManager_Exists(1) != 0 || by->type != 3) return;
    LOCK_tag* d = lock_of(lock);
    BLData* b = bloke_of(by);
    if (lock->state != 0 && lock->state != 5) return;
    const bool has_key = reinterpret_cast<const uint8_t*>(b)[0x70c + d->key] != 0;
    if (d->kind == 0) {
        if (!has_key) {
            lock->state = 3;
            nf::p32_store(GlobalVars, by);
            nf::p32_store(GlobalVars + 4, b);
            return;
        }
    } else if (!has_key) {
        play_at(d->sound_locked, lock);
        return;
    }
    lock->state = 1;
    play_at(d->sound_open, lock);
}

// [decomp] 0x001b0650 Lock_Unlock
// Abre o cadeado: o jogador (by) fica com a chave; trancado, passa a abrir com o som; decodificando, fica com a chave
// de novo e o abre como se usasse (Lock_Activate); troca o modelo. "silent" (a carga fácil): só passa a abrir.
void Lock_Unlock(obj_tag* by, obj_tag* lock, uint8_t silent) {
    if (silent == 0) {
        if (lock == nullptr || by == nullptr || by->type != 3) return;
        LOCK_tag* d = lock_of(lock);
        Player_EquipKey(bloke_of(by), d->key);
        if (lock->state == 0) {
            lock->state = 1;
            play_at(d->sound_open, lock);
        } else if (lock->state == 5) {
            Player_EquipKey(bloke_of(by), d->key);
            Lock_Activate(lock, by);
        }
        Control_NextLOD(lock);
        ++g_ssys_stats.unlocked;
    } else if (lock->state == 0 || lock->state == 5) {
        lock->state = 1;
    }
}

// [decomp] 0x001b0750 Lock_ReLock
// Tranca de novo (o canal do cadeado de teclado desligou): o jogador perde a chave e o modelo troca.
void Lock_ReLock(obj_tag* by, obj_tag* lock) {
    if (lock == nullptr || by == nullptr || by->type != 3) return;
    lock->state = 0;
    Player_UnEquipKey(bloke_of(by), lock_of(lock)->key);
    Control_NextLOD(lock);
}

// [decomp] 0x001b0548 Locks_Init
// Depois de cada carga (GF_LOCKS_INIT): anda o sorteio GameState.frame x 10 (& 0x1ff) vezes e sorteia os 51 códigos
// (algarismos de '0' a '8'), nenhum revelado. Bug do original, mantido: o resto é por 9 (Rand_Random() % 9), então o 9
// nunca sai num código (o teclado tem o 9; o jogo só fica com 9^4 códigos possíveis em vez de 10^4). Inofensivo, e
// trocar por % 10 mudaria os códigos sorteados de cada fase.
void Locks_Init() {
    const uint32_t skip = (GameState.frame * 10) & 0x1ff;
    for (uint32_t i = 0; i < skip; ++i) Rand_Random();
    for (uint32_t c = 0; c < uint32_t(kKeyCodes); ++c) {
        for (uint32_t k = 0; k < 4; ++k) KeyCodes[c * 5 + k] = char(Rand_Random() % 9 + '0');
        KeyCodes[c * 5 + 4] = 0;
    }
}

// [decomp] 0x001b07a8 PCQWorm_Create
// O computador do Q-Worm (tipo 0x49): parâmetros 0-3 (ver PCQWORM_tag), ligado à sala.
obj_tag* PCQWorm_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = Control_CreateObjEx(6, pos, rot, nullptr, glist, nullptr, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;
    obj->state = 0;
    obj->type = 0x49;
    auto* d = static_cast<PCQWORM_tag*>(obj->data);
    d->code = int16_t(level->params[0]);
    d->channel = int16_t(level->params[1]);
    d->use = uint8_t(level->params[2]);
    d->worm = uint8_t(level->params[3]);
    return obj;
}

// [decomp] 0x001aee20 PCQWorm_Update
// Esperando (0): com o parâmetro 3, um tiro do Q-Worm (arma 0x58) o ativa. Ativado (1): troca o modelo, revela o
// código (mensagem e KeyCodes +4) e liga o canal; depois (2) nada.
void PCQWorm_Update(obj_tag* obj) {
    auto* d = static_cast<PCQWORM_tag*>(obj->data);
    if (obj->state == 1) {
        if (d->code != 0) {
            Control_NextLOD(obj);
            show_code(uint32_t(int32_t(d->code)));
            KeyCodes[d->code * 5 + 4] = 1;
        }
        if (d->channel != 0) {
            switch_channels[d->channel] = 1;
            switch_channels_time[d->channel] = GameState.frame;
        }
        obj->state = uint16_t(obj->state + 1);
        ++g_ssys_stats.wormed;
        return;
    }
    if (obj->state != 0 || d->worm == 0) return;
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        if (bullet_weapon(h->obj) == 0x58) {
            obj->state = uint16_t(obj->state + 1);
            return;
        }
    }
}

// [decomp] 0x001b0840 PCQWorm_Activate
void PCQWorm_Activate(obj_tag* obj, obj_tag*) {
    const auto* d = static_cast<const PCQWORM_tag*>(obj->data);
    if (d->use != 0 && obj->state == 0) obj->state = 1;
}

// [decomp] 0x001a97b8 Player_UnEquipKey
void Player_UnEquipKey(BLData* b, uint16_t key) { reinterpret_cast<uint8_t*>(b)[0x70c + key] = 0; }

void ssys_debug_power_on() {
    std::memset(KeyCodes, 0, sizeof(KeyCodes));
    g_ssys_stats = SsysStats{};
}
