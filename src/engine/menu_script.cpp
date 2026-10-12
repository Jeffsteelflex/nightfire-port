// Os scripts de menu (Script_* de 0x002201c8 a 0x002214f0; não confundir com os scripts de cena de
// script.cpp): cada controle tem uma lista de M_SCRIPT, cada um com quadros-chave (posição e tamanho
// num quadro) e mensagens que disparam quando o quadro-chave passa. Script_RunFrame (de Page_Update)
// interpola o controle entre os quadros-chave (linear ou Catmull-Rom, Script_CalculateSpline) e envia
// as mensagens. Os scripts padrão de cada evento (M_CONTROL::defaults, mensagem 0x21) são tocados
// por Script_PlayDefault: entrar e sair de uma página animam a janela dela. Ver engine/menu.h.

#include "engine/menu.h"

#include <cstring>

#include "platform/psi.h"

namespace {

M_KEYFRAME* first_key(M_SCRIPT* s) { return static_cast<M_KEYFRAME*>(ll_first(s->keys)); }

M_SCRIPT* find_script(M_CONTROL* c, uint32_t id) {
    for (M_SCRIPT* s = static_cast<M_SCRIPT*>(ll_first(c->scripts)); s != nullptr; s = s->next) {
        if (s->id == id) return s;
    }
    return nullptr;
}

// Começa o script do começo.
void start(M_CONTROL* c, M_SCRIPT* s) {
    c->key = first_key(s);
    c->script = s;
    c->loop_key = first_key(s);
    c->step = -1;
    c->loop_step = 0;
    c->frame = 0;
}

// min.S do R5900.
float fmin(float a, float b) { return a < b ? a : b; }

using Calc = float (*)(int32_t, int32_t, int32_t, int32_t, float);

// O corpo comum das duas interpolações: os quatro quadros-chave em volta de t (com as pontas
// repetidas; t antes do primeiro dá a volta para o último, como no original) e os quatro campos.
void interpolate(M_CONTROL* c, M_SCRIPT* script, float t, Calc calc) {
    const int32_t count = int32_t(script->keys.count);
    float f;
    if (0.0f <= t) {
        const uint32_t last = uint32_t(count - 1);
        f = fmin(t, int32_t(last) < 0 ? float(last) : float(int32_t(last)));
    } else {
        f = 0.0f;
    }
    uint32_t i = 0;
    if (0.0f <= f) i = uint32_t(int64_t(fmin(f, float(count - 1))));
    const uint32_t last = uint32_t(count - 1);
    const uint32_t i1 = last < i + 1 ? last : i + 1;
    const uint32_t i2 = last < i + 2 ? last : i + 2;
    const uint32_t i0 = last < i - 1 ? last : i - 1;
    const float frac = f - float(i);
    LINKEDLIST* keys = &script->keys;
    const auto k = [&](uint32_t n) { return static_cast<M_KEYFRAME*>(LinkedList_GetIndex(keys, n)); };
    c->x = int16_t(int32_t(calc(k(i0)->x, k(i)->x, k(i1)->x, k(i2)->x, frac)));
    c->y = int16_t(int32_t(calc(k(i0)->y, k(i)->y, k(i1)->y, k(i2)->y, frac)));
    c->w = int16_t(int32_t(calc(k(i0)->w, k(i)->w, k(i1)->w, k(i2)->w, frac)));
    c->h = int16_t(int32_t(calc(k(i0)->h, k(i)->h, k(i1)->h, k(i2)->h, frac)));
}

}  // namespace

// [decomp] 0x002201c8 Script_AddKeyFrame
// Acrescenta um quadro-chave em ordem de quadro (uma cópia); num quadro que já existe, atualiza o
// existente (a não ser que ele seja fixo, flag 0x20).
M_KEYFRAME* Script_AddKeyFrame(M_SCRIPT* script, M_KEYFRAME* key) {
    if (script == nullptr || key == nullptr) return nullptr;
    for (M_KEYFRAME* k = first_key(script); k != nullptr; k = k->next) {
        if (key->frame != k->frame) continue;
        if ((k->flags & 0x20) != 0) return nullptr;
        k->frame = key->frame;
        k->flags = key->flags;
        k->x = key->x;
        k->y = key->y;
        k->w = key->w;
        k->h = key->h;
        return k;
    }
    M_KEYFRAME* n = static_cast<M_KEYFRAME*>(Menu_Malloc(0x28, 6));
    amemcpy(n, key, 0x28);
    M_KEYFRAME* k = first_key(script);
    void* after = nullptr;
    if (k != nullptr) {
        while (k->frame <= n->frame) {
            k = k->next;
            if (k == nullptr) break;
        }
        after = k != nullptr ? static_cast<void*>(k->prev)
                             : (script->keys.tail != nullptr
                                    ? static_cast<void*>(static_cast<uint8_t*>(script->keys.tail) - script->keys.offset * 4)
                                    : nullptr);
        if (after != nullptr) after = static_cast<uint8_t*>(after) + script->keys.offset * 4;
    }
    LinkedList_Insert(&script->keys, after, &n->prev, n);
    LinkedList_Init(&n->messages, 0);
    return n;
}

// [decomp] 0x00220350 Script_PlayDefault
// Toca o script padrão do evento: 0x4c/0x51 entrar (0), 0x4d/0x4e sair/foco (1), 0x4b/99 escolher/
// voltar (2), 0x49 mudar (3), 0x4f perder o foco (4), 0x5b voltar (5). Entrar, sair e voltar de uma
// página tocam o da janela dela. Devolve 1 se havia um script para o evento.
int32_t Script_PlayDefault(M_CONTROL* c, uint32_t event) {
    if (c == nullptr) return 0;
    int32_t slot;
    switch (event) {
        case 0x49: slot = 3; break;
        case 0x4b:
        case 99: slot = 2; break;
        case 0x4c:
        case 0x51: slot = 0; break;
        case 0x4d:
        case 0x4e: slot = 1; break;
        case 0x4f: slot = 4; break;
        case 0x5b: slot = 5; break;
        default: return 0;
    }
    M_CONTROL* target = c;
    if (event - 0x4cu < 2 || event == 99) {
        target = Window_Get(reinterpret_cast<M_PAGE*>(c));
        if (target == nullptr) return 0;
        if (target->defaults[slot] == nullptr) return 0;
    } else {
        if (c->defaults[slot] == nullptr) return 0;
        if (c->type == MCT_WINDOW) return 0;
    }
    M_SCRIPT* s = find_script(target, target->defaults[slot]->id);
    if (s == nullptr) return 1;
    target->script = s;
    start(target, s);
    return 1;
}

// [decomp] 0x00220580 Script_RunFrame
// Um tick do script do controle: processos, a posição interpolada entre o quadro-chave anterior e o
// atual e, quando o quadro chega ao quadro-chave, o laço (flags 2/4) e as mensagens dele (0xfffffffd ao
// gerente, 0xfffffffe ao próprio controle, outro valor aos controles com esse id; a = -4 usa o
// resultado da anterior; 0xe1 e acima viram processos até o próximo quadro-chave).
void Script_RunFrame(M_MANAGER* m, M_CONTROL* c) {
    Process_RunFrame(c);
    M_KEYFRAME* key = c->key;
    if (key == nullptr) return;
    M_KEYFRAME* prev = key->prev != nullptr ? nf::raw(key->prev) : key;
    float t;
    if (key->frame == prev->frame) {
        t = float(c->step + 1);
    } else {
        const uint32_t d = uint32_t(c->frame) - prev->frame;
        t = float(c->step) + float(d) / float(key->frame - prev->frame);
    }
    M_SCRIPT* script = c->script;
    if ((script->flags & 1) == 0) {
        Script_InterpolateLine(c, script, t);
    } else {
        Script_InterpolateSpline(c, script, t);
    }
    if (c->frame < int32_t(key->frame)) {
        c->frame = c->frame + 1;
        return;
    }
    if ((key->flags & 2) != 0) {
        c->loop_key = c->key;
        c->loop_step = c->step;
        c->script->flags |= 8;
    }
    if ((key->flags & 4) != 0 && (c->script->flags & 8) != 0) {
        c->script_flags |= 0x10;
        c->step = c->loop_step;
        c->key = c->loop_key;
        c->frame = int32_t(c->loop_key->frame);
        c->frame = c->frame + 1;
        return;
    }
    M_KEYFRAME* next = key->next;
    c->key = next;
    if (next == nullptr) {
        c->step = 0;
    } else {
        ++c->step;
    }
    for (M_MESSAGE* msg = static_cast<M_MESSAGE*>(ll_first(key->messages)); msg != nullptr; msg = msg->next) {
        if (msg->message < 0xe1) {
            int32_t a = msg->a;
            if (a == -4) a = c->result;
            if (msg->target == 0xfffffffdu) {
                c->result = __Menu_SendMessage(&m->base, msg->message, a, msg->b);
            } else if (msg->target == 0xfffffffeu) {
                c->result = __Menu_SendMessage(c, msg->message, a, msg->b);
            } else {
                c->result = __Menu_Send(m->index, msg->target, msg->message, a, msg->b);
            }
        } else {
            uint32_t duration = 0xf;
            if (c->key != nullptr) {
                duration = uint32_t(int32_t(int16_t(uint16_t(c->key->frame) - uint16_t(c->frame) - 1)));
            }
            M_CONTROL* target = c;
            if (msg->target != 0xfffffffeu) {
                target = nf::p32_ptr<M_CONTROL>(uint32_t(__Menu_Send(m->index, msg->target, 0x39, 0, 0)));
            }
            Process_Create(target, msg, duration, 0);
        }
    }
    c->frame = c->frame + 1;
}

// [decomp] 0x002208b8 Script_InterpolateSpline
void Script_InterpolateSpline(M_CONTROL* c, M_SCRIPT* script, float t) {
    interpolate(c, script, t, Script_CalculateSpline);
}

// [decomp] 0x00220bc8 Script_InterpolateLine
void Script_InterpolateLine(M_CONTROL* c, M_SCRIPT* script, float t) {
    interpolate(c, script, t, Script_CalculateLine);
}

// [decomp] 0x00220ed8 Script_PrepareScripting
void Script_PrepareScripting(M_CONTROL* c) {
    if (c == nullptr) return;
    LinkedList_Init(&c->scripts, 0);
    c->key = nullptr;
    c->loop_key = nullptr;
    c->loop_step = 0;
    c->script = nullptr;
    c->frame = -1;
    c->step = -1;
    Process_Init(c);
}

// [decomp] 0x00220f30 Script_DelAllScripts
void Script_DelAllScripts(M_CONTROL* c) {
    if (c == nullptr) return;
    c->script = nullptr;
    c->key = nullptr;
    for (M_SCRIPT* s = static_cast<M_SCRIPT*>(ll_first(c->scripts)); s != nullptr;) {
        M_SCRIPT* next = s->next;
        Script_DelScript(c, s);
        s = next;
    }
    Process_Destroy(c);
}

// [decomp] 0x00220fb0 Script_AddScript
// O script `id` do controle (criado se não existe); vira o atual.
M_SCRIPT* Script_AddScript(M_CONTROL* c, uint32_t id) {
    if (c == nullptr) return nullptr;
    if (M_SCRIPT* s = find_script(c, id)) {
        c->script = s;
        return s;
    }
    M_SCRIPT* s = static_cast<M_SCRIPT*>(Menu_Malloc(0x20, 5));
    LinkedList_Add(&c->scripts, &s->prev, s);
    LinkedList_Init(&s->keys, 0);
    s->id = id;
    c->script = s;
    return s;
}

// [decomp] 0x00221068 Script_DelScript
void Script_DelScript(M_CONTROL* c, M_SCRIPT* script) {
    if (c == nullptr || script == nullptr) return;
    for (M_KEYFRAME* k = first_key(script); k != nullptr;) {
        M_KEYFRAME* next = k->next;
        Script_DelKeyFrame(script, k);
        k = next;
    }
    LinkedList_Remove(&c->scripts, &script->prev, script);
    void* p = script;
    Menu_Free(&p, 5);
}

// [decomp] 0x00221100 Script_DelKeyFrame
void Script_DelKeyFrame(M_SCRIPT* script, M_KEYFRAME* key) {
    if (script == nullptr || key == nullptr) return;
    for (M_MESSAGE* msg = static_cast<M_MESSAGE*>(ll_first(key->messages)); msg != nullptr;) {
        M_MESSAGE* next = msg->next;
        Script_DelMessage(key, msg);
        msg = next;
    }
    LinkedList_Remove(&script->keys, &key->prev, key);
    void* p = key;
    Menu_Free(&p, 6);
}

// [decomp] 0x00221198 Script_AddMessage
M_MESSAGE* Script_AddMessage(M_KEYFRAME* key, M_MESSAGE* msg) {
    if (key == nullptr || msg == nullptr) return nullptr;
    M_MESSAGE* n = static_cast<M_MESSAGE*>(Menu_Malloc(0x18, 7));
    amemcpy(n, msg, 0x18);
    LinkedList_Add(&key->messages, &n->prev, n);
    return n;
}

// [decomp] 0x00221210 Script_DelMessage
void Script_DelMessage(M_KEYFRAME* key, M_MESSAGE* msg) {
    if (key == nullptr || msg == nullptr) return;
    LinkedList_Remove(&key->messages, &msg->prev, msg);
    void* p = msg;
    Menu_Free(&p, 7);
}

// [decomp] 0x00221250 Script_Select
M_SCRIPT* Script_Select(M_CONTROL* c, uint32_t id) {
    M_SCRIPT* s = find_script(c, id);
    if (s != nullptr) c->script = s;
    return s;
}

// [decomp] 0x002212a8 Script_Play
// Toca o script `id` do começo (from_end 0) ou parado no último quadro-chave.
int32_t Script_Play(M_CONTROL* c, uint32_t id, int8_t from_end) {
    M_SCRIPT* s = find_script(c, id);
    if (s == nullptr) return 0;
    c->script = s;
    if (from_end == 0) {
        start(c, s);
    } else {
        M_KEYFRAME* last = s->keys.tail != nullptr
                               ? reinterpret_cast<M_KEYFRAME*>(static_cast<uint8_t*>(s->keys.tail) - s->keys.offset * 4)
                               : nullptr;
        c->key = last;
        c->loop_key = last;
        c->loop_step = int32_t(s->keys.count);
        c->step = int32_t(s->keys.count);
        c->script = s;
        c->frame = int32_t(c->key->frame);
    }
    return 1;
}

// [decomp] 0x002213d0 Script_ExitLoop
int32_t Script_ExitLoop(M_CONTROL* c) {
    if (c == nullptr) return 0;
    c->script_flags &= 0xfffffff7u;
    return 1;
}

// [decomp] 0x00221400 Script_Stop
void Script_Stop(M_CONTROL* c) {
    if (c != nullptr) c->key = nullptr;
}

// [decomp] 0x00221410 Script_CalculateSpline
// Catmull-Rom entre p1 e p2.
float Script_CalculateSpline(int32_t p0, int32_t p1, int32_t p2, int32_t p3, float t) {
    const float f1 = float(p1);
    const float f0 = float(p0);
    return (f1 + f1 + t * float(p2 - p0) + t * t * (((f0 + f0) - f1 * 5.0f) + float(p2) * 4.0f - float(p3)) +
            t * t * t * (((f1 * 3.0f - float(p2) * 3.0f) + float(p3)) - f0)) *
           0.5f;
}

// [decomp] 0x002214c8 Script_CalculateLine
float Script_CalculateLine(int32_t, int32_t p1, int32_t p2, int32_t, float t) { return float(p1) + float(p2 - p1) * t; }
