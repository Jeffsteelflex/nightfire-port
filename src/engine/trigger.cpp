// Gatilhos e dicas (Hint_*). Ver trigger.h.
//
// Divergências: canal de gatilho acima de 255 é ignorado (o original escreveria fora de
// switch_channels; vale para todos os canais lidos ou escritos aqui).

#include "engine/trigger.h"

#include <cstring>

#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/spline.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

uint8_t ProcessTouch = 0;

// [decomp] 0x001b1900 Trigger_Calc
// Os canais 0x65-0x6e são os "momentos Bond" (pontos de estatística). Duas fases ligam um canal a
// partir de outro. Depois, os gatilhos de toque (tipo 0x1d no modo 2) desligam o próprio canal e se
// atualizam com ProcessTouch ligado, para religá-lo se alguém ainda os toca.
void Trigger_Calc() {
    for (uint16_t ch = 0x65; ch < 0x6f; ++ch) {
        if (switch_channels[ch] != 0) PlrStat_DoneBondMoment(0, ch);
    }
    if (GameState.level_file == 0x7000005 && switch_channels[0xa4] != 0) switch_channels[0x6f] = 1;
    if (GameState.level_file == 0x700000b && switch_channels[0x34] != 0) switch_channels[0x2d] = 1;
    const auto touch_mode = [](const obj_tag* o) {
        uint16_t mode;
        std::memcpy(&mode, static_cast<const uint8_t*>(o->data) + 0x38, 2);
        return o->type == 0x1d && mode == 2;
    };
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (!touch_mode(o)) continue;
        int32_t ch;
        std::memcpy(&ch, static_cast<const uint8_t*>(o->data) + 0x20, 4);
        if (ch != 0 && uint32_t(ch) < 256) switch_channels[ch] = 0;
    }
    ProcessTouch = 1;
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (touch_mode(o)) Trigger_Update(o);
    }
    ProcessTouch = 0;
}

namespace {

// Parâmetros reorganizados pelos *_Create antes de Trigger_Create (0x00319b88).
uint32_t TmpParam[33];

TRIGGER_Tag* trigger_of(obj_tag* obj) { return static_cast<TRIGGER_Tag*>(obj->data); }

bool on(uint32_t ch) { return ch < 256 && switch_channels[ch] != 0; }

void set_channel(uint32_t ch, uint8_t value) {
    if (ch < 256) switch_channels[ch] = value;
}

void stamp(uint32_t ch) {
    if (ch < 256) switch_channels_time[ch] = GameState.frame;
}

int32_t channel_time(uint32_t ch) { return ch < 256 ? static_cast<int32_t>(switch_channels_time[ch]) : 0; }

// Os gatilhos "multiplex" deslocam os parâmetros 1..6 para 2..7 e põem o modo no 0.
void shift_inputs(level_tag* level, uint32_t mode) {
    amemcpy(TmpParam, level->params, 0x84);
    level->params[0] = mode;
    level->params[1] = TmpParam[0];
    for (int i = 2; i < 8; ++i) level->params[i] = TmpParam[i - 1];
}

// Os de toque: modo, canal (0), quem dispara (1), trava e esconder (6/7 ou 7/8).
void touch_params(level_tag* level, uint32_t mode, int lock_from) {
    amemcpy(TmpParam, level->params, 0x84);
    level->params[0] = mode;
    level->params[1] = TmpParam[0];
    level->params[9] = TmpParam[1];
    level->params[10] = TmpParam[lock_from];
    level->params[11] = TmpParam[lock_from + 1];
    for (int i = 2; i < 8; ++i) level->params[i] = TmpParam[i];
}

}  // namespace

// [decomp] 0x001b1720 Trigger_Create
// Gatilho do tipo 0x1d: parâmetros 0 modo, 1 canal, 2-8 entradas, 9 quem dispara (2 se zero), 10
// trava, 11 esconder ao travar; com caminho, anda por ele na velocidade do posicionamento.
obj_tag* Trigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x40, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    TRIGGER_Tag* t = trigger_of(obj);
    std::memset(t, 0, 0x40);
    t->pos = 0.0f;
    t->path = level->path;
    const uint16_t frames = level->path_count == 0 ? 0 : static_cast<uint16_t>(level->path_count - 1);
    t->frames = frames;
    t->mode = static_cast<uint16_t>(level->params[0]);
    t->channel = level->params[1];
    for (int i = 0; i < 7; ++i) t->inputs[i] = static_cast<uint16_t>(level->params[2 + i]);
    t->inputs[7] = 0;
    t->touch = level->params[9] != 0 ? static_cast<uint16_t>(level->params[9]) : 2;
    t->speed = float(t->frames) * REC_FRAME_RATE * (FRAME_RATE / float(level->path_unknown));
    t->lock = static_cast<uint16_t>(level->params[10]);
    t->hide_on_lock = static_cast<uint16_t>(level->params[11]);
    if (frames != 0) obj->mode |= 4;
    obj->glist = glist;
    obj->type = 0x1d;
    obj->flags = glist->flags;
    build_LinkToRoom(obj, 0, glb_world);
    MATRIX m;
    View_RotTransMatrix(rot, pos, &m);
    Mat_GetUp(&t->up, &m);
    if (t->mode == 10) obj->flags &= ~0x10u;
    return obj;
}

// [decomp] 0x001b2b88 Trigger_TouchOnce
// Devolve o objeto de Trigger_Create (o original o deixa em v0; Script_CreateEntity o lê).
obj_tag* Trigger_TouchOnce(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    touch_params(level, 1, 7);
    return Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2aa8 Trigger_Touch
// Devolve o objeto de Trigger_Create (como Trigger_TouchOnce).
obj_tag* Trigger_Touch(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    touch_params(level, 2, 6);
    return Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2c68 Trigger_MultiplexIn
void Trigger_MultiplexIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    shift_inputs(level, 3);
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2d38 Trigger_MultiplexOrIn
void Trigger_MultiplexOrIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    shift_inputs(level, 0xc);
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2e08 Trigger_MultiplexSIn
void Trigger_MultiplexSIn(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    shift_inputs(level, 4);
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2ed8 Trigger_MultiplexOut
void Trigger_MultiplexOut(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    shift_inputs(level, 5);
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2950 Trigger_LoadLevelCreate
// Troca de fase: parâmetro 0 a fase, 1-4 trava, condição e os canais a ligar; quem dispara 0xe.
void Trigger_LoadLevelCreate(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    amemcpy(TmpParam, level->params, 0x84);
    level->params[0] = 10;
    level->params[1] = TmpParam[0];
    level->params[2] = TmpParam[1];
    level->params[3] = TmpParam[2];
    level->params[4] = TmpParam[3];
    level->params[9] = 0xe;
    level->params[5] = TmpParam[4];
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2a08 Trigger_MoviePlayer
void Trigger_MoviePlayer(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    amemcpy(TmpParam, level->params, 0x84);
    level->params[0] = 0xd;
    level->params[1] = TmpParam[0];
    level->params[2] = TmpParam[2];
    level->params[9] = TmpParam[1];
    Trigger_Create(pos, rot, level, glist);
}

// [decomp] 0x001b2fa8 Trigger_Type
uint16_t Trigger_Type(obj_tag* trigger) { return trigger != nullptr ? trigger_of(trigger)->mode : 0; }

// [decomp] 0x001b2fc8 Trigger_Locked
// Troca de fase: travado enquanto a trava (inputs[0]) está desligada; os outros: com a trava ligada.
uint32_t Trigger_Locked(obj_tag* trigger) {
    if (trigger == nullptr) return 0;
    const TRIGGER_Tag* t = trigger_of(trigger);
    if (t->mode == 10) {
        if (t->inputs[0] != 0) return on(t->inputs[0]) ? 0 : 1;
    } else if (t->lock != 0 && on(t->lock)) {
        return 1;
    }
    return 0;
}

// [decomp] 0x001b3040 InverterTrigger_Create
// O inversor (posicionamento 0x41, objeto 0x53 sem modelo nem sala, na origem): o canal de entrada (parâmetro 0, em
// obj_tag::state) e o que ele desliga (parâmetro 1, em obj_tag::unknown_f6). O original devolve 0.
obj_tag* InverterTrigger_Create(level_tag* level) {
    obj_tag* obj = control_create_object(4, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR),
                                         const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), nullptr);
    if (obj == nullptr) return nullptr;
    obj->type = 0x53;
    obj->state = uint16_t(level->params[0]);
    obj->unknown_f6 = uint16_t(level->params[1]);
    return obj;
}

// [decomp] 0x001b30a8 InverterTrigger_Update
// Enquanto o canal de entrada está ligado, desliga o de saída (07000006: o fim do alarme dos holofotes).
void InverterTrigger_Update(obj_tag* obj) {
    if (switch_channels[uint8_t(int16_t(obj->state))] != 0 && obj->unknown_f6 != 0) {
        switch_channels[uint8_t(int16_t(obj->unknown_f6))] = 0;
    }
}

// [decomp] 0x001b1fc8 Trigger_Hit
// Quem está tocando o gatilho (a lista de acertos do objeto): 8 o jogador, 0x10 personagens (tipos 2
// e 0x4a), 2 balas. Com jogador/personagem e modo de toque, as entradas filtram: 0xb só personagens,
// 0xe só o jogador, outros valores a pele (16 bits baixos do hash) de um personagem que toca; com 1 em touch, exige o canal inputs[4]. Com balas, a entrada é o tipo de
// munição (weapon_definition_tag + 2) de uma bala que toca.
uint32_t Trigger_Hit(obj_tag* obj, TRIGGER_Tag* t) {
    uint32_t who = 0;
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr) continue;
        switch (o->type) {
            case 3: who |= 8; break;
            case 2: case 0x4a: who |= 0x10; break;
            case 5: who |= 2; break;
            default: break;
        }
    }
    const uint16_t touch = t->touch;
    if ((touch & 0x18) != 0 && (who & 0x18) != 0 && static_cast<uint32_t>(t->mode) - 1 < 2) {
        if (touch == 8) return (who >> 3) & 1;
        if (touch == 0x10 && (who & 0x10) == 0) return 0;
        uint8_t ok = (touch & 1) == 0 ? 1 : (on(t->inputs[4]) ? 1 : 0);
        for (int k = 0; k < 8 && t->inputs[k] != 0; ++k) {
            const uint16_t in = t->inputs[k];
            uint8_t v;
            if (in == 0xb) {
                v = (ok & 1) != 0 && (who & 0x10) != 0 ? 1 : 0;
            } else if (in == 0xe) {
                v = (ok & 1) != 0 && (who & 8) != 0 ? 1 : 0;
            } else {
                // Outro valor: um personagem com essa pele (16 bits baixos do hash) está tocando.
                v = 0;
                for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
                    obj_tag* o = h->obj;
                    if (o != nullptr && o->type == 2 &&
                        static_cast<int16_t>(in) == static_cast<int16_t>(static_cast<Drone_tag*>(o->data)->skin)) {
                        v = ok & 1;
                        break;
                    }
                }
            }
            ok = v;
        }
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(ok)));
    }
    if ((touch & 2) != 0 && (who & 2) != 0 && static_cast<uint32_t>(t->mode) - 1 < 2) {
        int8_t ok = 1;
        for (int k = 0; k < 8 && t->inputs[k] != 0; ++k) {
            const int8_t prev = ok;
            ok = 0;
            for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
                obj_tag* o = h->obj;
                if (o == nullptr || o->type != 5) continue;
                const uint8_t* weapon = reinterpret_cast<const uint8_t*>(static_cast<const BU_tag*>(o->data)->weapon);
                int16_t ammo;
                std::memcpy(&ammo, weapon + 2, 2);
                ok = prev;
                if (static_cast<int16_t>(t->inputs[k]) == ammo) break;
                ok = 0;
            }
        }
        return static_cast<uint32_t>(static_cast<int32_t>(ok));
    }
    return (touch & who) != 0 ? 1 : 0;
}

// [decomp] 0x001b1a78 Trigger_Update
// Por tick: anda pelo caminho, vê quem toca e aplica o modo (canal de saída ligado pelo toque, pelo E,
// OU ou sequência das entradas, cópia para as entradas, troca de fase, filme). Travado, para (e se
// esconde com hide_on_lock). Gatilhos de toque só rodam dentro de Trigger_Calc.
void Trigger_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    TRIGGER_Tag* t = trigger_of(obj);
    if (t == nullptr) return;
    if (t->cooldown != 0) --t->cooldown;
    if (t->mode == 2 && ProcessTouch == 0) return;
    Control_SetEffectType(obj, obj->glist != nullptr ? obj->glist->flags : obj->flags);
    if (t->lock != 0 && on(t->lock)) {
        if (t->hide_on_lock != 0) obj->flags |= 0x30;
        return;
    }
    if (t->frames != 0) {
        Spline_Interp(t->path, 0, t->frames, t->pos, &obj->matrix, nullptr, 2);
        obj->mode |= 0x20;
        t->pos = t->pos + t->speed * FRAME_RATE_MUL;
        if (float(t->frames) < t->pos) t->pos = 0.0f;
    }
    const uint32_t hit = Trigger_Hit(obj, t);
    const uint32_t ch = t->channel;
    switch (t->mode) {
        case 1:
            if (on(ch)) {
                if ((obj->flags & 0x10) != 0) obj->life |= 1;
                return;
            }
            [[fallthrough]];
        case 2:
            if (GameState.level_file != 0x7001500 && ch == 0x7d) return;
            if (ch != 0) {
                set_channel(ch, static_cast<uint8_t>(on(ch) || hit != 0));
                if (hit != 0) stamp(ch);
            }
            return;
        case 3: {
            uint8_t all = 1;
            for (int k = 0; k < 8 && t->inputs[k] != 0; ++k) all &= on(t->inputs[k]) ? 1 : 0;
            if (ch != 0) {
                set_channel(ch, all);
                if (all == 0) return;
                stamp(ch);
            }
            if (all == 0 || ch < 0x65 || 0x6e < ch) return;
            if ((obj->flags & 0x10) != 0) obj->life |= 1;  // momento Bond: o gatilho some
            return;
        }
        case 4: {
            uint8_t all = on(t->inputs[0]) ? 1 : 0;
            if (t->inputs[1] != 0 && all != 0) {
                for (int k = 1; k < 8 && t->inputs[k] != 0 && all != 0; ++k) {
                    const bool v = on(t->inputs[k]) && channel_time(t->inputs[k - 1]) - 4 <= channel_time(t->inputs[k]);
                    all &= v ? 1 : 0;
                }
            }
            if (ch == 0) return;
            set_channel(ch, all);
            if (all != 0) stamp(ch);
            return;
        }
        case 5:
            for (int k = 0; k < 8; ++k) {
                const uint16_t out = t->inputs[k];
                if (out == 0) continue;
                set_channel(out, on(ch) ? 1 : 0);
                if (on(ch)) stamp(out);
            }
            return;
        case 10:
            if (t->inputs[1] != 0 && on(t->inputs[1])) Trigger_Activate(obj, glb_players[0]);
            return;
        case 0xc: {
            uint8_t any = 0;
            for (int k = 0; k < 8 && t->inputs[k] != 0; ++k) any |= on(t->inputs[k]) ? 1 : 0;
            if (ch == 0) return;
            set_channel(ch, any);
            if (any != 0) stamp(ch);
            return;
        }
        case 0xd:
            if (t->inputs[0] != 0 && on(t->inputs[0])) {
                set_channel(t->inputs[0], 0);
                GameState.movie_file = ch;
                GameState.unknown_14 = 0;
                GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
                GameFlow_PushState(GF_FADE_REQUEST, 30.0f, 0xff);
            }
            return;
        default:
            return;
    }
}

// [decomp] 0x001b2318 Trigger_Activate
// O jogador usou (E) o gatilho: toque com 4 em touch liga o canal; filme toca; troca de fase (se a
// missão não falhou e a trava está ligada; senão a mensagem 0x2000021) liga os canais pedidos, salva o
// jogador e pede a fase do canal.
void Trigger_Activate(obj_tag* obj, obj_tag* by) {
    if (by == nullptr || obj == nullptr || by->type != 3) return;
    TRIGGER_Tag* t = trigger_of(obj);
    switch (t->mode) {
        case 2:
            if ((t->touch & 4) == 0 || t->channel == 0) return;
            set_channel(t->channel, 1);
            stamp(t->channel);
            return;
        case 1:
            if ((t->touch & 4) == 0) return;
            if (t->channel != 0) {
                set_channel(t->channel, 1);
                stamp(t->channel);
            }
            if ((obj->flags & 0x10) != 0) obj->life |= 1;
            return;
        case 0xd:
            if ((t->touch & 4) == 0) return;
            GameState.movie_file = t->channel;
            GameState.unknown_14 = 0;
            GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
            GameFlow_PushState(GF_FADE_REQUEST, 300.0f, 0xff);
            return;
        case 10:
            break;
        default:
            return;
    }
    if (switch_channels[99] != 0 || switch_channels[0x62] != 0 || switch_channels[0x61] != 0 ||
        switch_channels[0x60] != 0) {
        return;
    }
    if (t->inputs[0] != 0 && !on(t->inputs[0])) {
        if (t->cooldown != 0) return;
        Text_AddMsg(0, 0, 1, Txt_BindLabel(0x2000021, 0), 0, 0xb4);
        t->cooldown = static_cast<int16_t>((FRAME_RATE_INT << 1) + FRAME_RATE_INT);
        return;
    }
    if (t->inputs[1] != 0 && on(t->inputs[1])) {
        if (t->inputs[3] != 0) {
            set_channel(t->inputs[3], 1);
            stamp(t->inputs[3]);
        }
    } else if (t->inputs[2] != 0) {
        set_channel(t->inputs[2], 1);
        stamp(t->inputs[2]);
    }
    Music_Event(3, 1);
    GS_PauseGame(1);
    ResetMap_LevelToLoad(t->channel, 1, 0);
    GameFlow_PushState(GF_FADE_REQUEST, 300.0f, 0xff);
    t->mode = 99;
    if (glb_blokes[0] != nullptr) Player_RamSave(glb_blokes[0]);
}

// [decomp] 0x001b3248 Hint_Create
// A dica do posicionamento 0x33 (ver HINT_tag): parâmetro 2 a dificuldade (0 todas; 1-4 até ela; 5, 6, 7 só a 1, 2,
// 3). Devolve o objeto (ou nulo: dicas desligadas, outra dificuldade, sem objeto livre).
obj_tag* Hint_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    if (DrawInfo[3] == 0) return nullptr;
    const uint32_t difficulty = level->params[2];
    const uint32_t now = GameState.difficulty;
    if (difficulty != 0 && difficulty < now) return nullptr;
    if (difficulty == 5 && now != 1) return nullptr;
    if (difficulty == 6 && now != 2) return nullptr;
    if (difficulty == 7 && now != 3) return nullptr;
    obj_tag* obj = control_create_object(0x10, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->type = 0x43;
    Control_SetGList(obj, glist);
    HINT_tag* h = static_cast<HINT_tag*>(obj->data);
    h->label = level->params[0];
    h->sound = level->params[1];
    h->on = level->params[3];
    h->off = level->params[4];
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001b3380 Hint_Update
// A cada tick (control_funcs 0x43): ligada e não apagada, o primeiro jogador na lista de acertos dela faz o som, a
// mensagem de informação do rótulo (no HUD dele, 180 ticks) e a remoção; apagada, só a remoção. Divergência: canais
// acima de 255 contam como desligados (o original leria além de switch_channels).
void Hint_Update(obj_tag* hint) {
    const HINT_tag* h = static_cast<const HINT_tag*>(hint->data);
    if (h->on != 0 && !on(h->on)) return;
    if (h->off != 0 && on(h->off)) {
        hint->life |= 1;
        return;
    }
    for (HITDATA_tag* hit = hint->hits; hit != nullptr; hit = hit->next) {
        obj_tag* o = hit->obj;
        if (o == nullptr || o->type != 3) continue;
        if (h->sound != 0) Sound_Play(h->sound, 100.0f, 0, 0);
        const int8_t player = static_cast<const BLData*>(o->data)->player;
        Text_AddMsg(player, 0, 1, Txt_BindLabel(h->label, 0), 0, 0xb4);
        hint->life |= 1;
        return;
    }
}
