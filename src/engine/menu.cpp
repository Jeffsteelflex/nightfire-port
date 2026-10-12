// O núcleo dos menus: o despacho de mensagens (__Menu_*), a entrada (Menu_InputAction), o foco e a
// navegação (Menu_GetControl, Menu_FindControl, Menu_CursorOverMe), os sprites e os componentes
// (Component_*). Ver engine/menu.h.
//
// Divergências (comentadas nas funções):
// - Component_GetHandle sem o conjunto do controle devolve um componente zerado (o original devolve
//   nulo e quem chama lê as margens em endereços baixos); conta em g_menu_guards.no_component.
// - Menu_CursorOverMe e Menu_GetControl guardam no máximo 32 controles sob o cursor e
//   Menu_FindControl no máximo 64 candidatos de reserva (o original escreveria além das tabelas da
//   pilha; nenhuma página do disco chega perto).

#include "engine/menu.h"

#include <cmath>
#include <cstring>

#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu_mouse.h"
#include "engine/modules.h"
#include "engine/sort.h"
#include "engine/sprite.h"
#include "engine/vecmath.h"
#include "platform/pending.h"

LINKEDLIST component_list;
uint32_t frame_counter = 0;
uint32_t menu_delay_frame = 0;
float throb = 0.0f;
// 2 quando o PS2 roda a lógica a 30 quadros (os menus dão dois passos por tick). O port roda a 60.
uint32_t PS2FramesToSkip = 0;

namespace {

int16_t alpha = 0;                  // 0x0030d17c
float alpha_counter = 0.0f;         // counter (0x0030d180)
float text_count = 0.0f;            // count (0x0030d18c)
uint32_t enabled_sound_flags = 0;   // 0x0030d184
uint32_t last_action = 0;           // 0x0030d168
int32_t last_controller = -1;       // 0x0030d160
M_CONTROL backup_135;               // o controle de reserva de Menu_GetFirstControl
M_COMPONENT no_component;           // só no port: devolvido por Component_GetHandle sem conjunto

struct DelayedMessage {
    M_CONTROL* control;
    uint32_t frame;
    uint32_t message;
    int32_t a;
    int32_t b;
};
static_assert(sizeof(DelayedMessage) == 0x14);
DelayedMessage menu_delay_msg[0x80];  // 0x002ddb00

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }

M_CONTROL* first_control(M_PAGE* p) { return static_cast<M_CONTROL*>(ll_first(p->controls)); }
M_PAGE* first_page(M_MANAGER* m) { return static_cast<M_PAGE*>(ll_first(m->pages)); }
M_PAGE* next_page(M_PAGE* p) {
    return p->next != nullptr ? reinterpret_cast<M_PAGE*>(reinterpret_cast<uint8_t*>(p->next) - 0xa4) : nullptr;
}

M_COMPONENTSET* find_set(uint32_t component) {
    for (M_COMPONENTSET* s = static_cast<M_COMPONENTSET*>(ll_first(component_list)); s != nullptr; s = s->next) {
        if (s->id == component >> 16) return s;
    }
    return nullptr;
}

// Uma vaga livre na fila de mensagens atrasadas (a primeira com quadro já passado), ou -1.
int32_t free_delay_slot() {
    if (menu_delay_msg[0].frame < menu_delay_frame) return 0;
    for (int32_t i = 1; i < 0x80; ++i) {
        if (menu_delay_msg[i].frame < menu_delay_frame) return i;
    }
    return -1;
}

// Uma ação de um jogador (0-3) ou de todos (-1), como Menu_InputAction lê: o valor x 100 se o
// estado bate com a máscara; guarda quem apertou por último.
uint16_t action_of(int16_t player, int32_t action, uint16_t mask) {
    if (player == -1) {
        uint16_t r = 0;
        for (int16_t i = 0; i < 4; ++i) {
            const uint16_t v = uint16_t(int16_t(r | Input_Action(i, uint32_t(action), mask)));
            if (v != r) {
                last_action = frame_counter;
                last_controller = i;
            }
            r = v;
        }
        return r;
    }
    if (player < 4 && (PlayerSetting[player].state[action] & mask) != 0) {
        const uint16_t v = uint16_t(int32_t(PlayerSetting[player].actions[action] * 100.0f));
        if (v != 0) {
            last_action = frame_counter;
            last_controller = player;
        }
        return v;
    }
    return 0;
}

}  // namespace

// ---- componentes ----

// [decomp] 0x001f1038 Component_InitComponents
void Component_InitComponents() { LinkedList_Init(&component_list, 0); }

// [decomp] 0x001f1060 Component_AddSet
// Um conjunto novo (id 999: o próximo número livre) do gerente `index`.
M_COMPONENTSET* Component_AddSet(uint8_t index, uint16_t id) {
    M_COMPONENTSET* s = static_cast<M_COMPONENTSET*>(Menu_Malloc(0x128, 1));
    s->id = id == 999 ? component_list.count + 1 : id;
    s->manager = index;
    LinkedList_Add(&component_list, &s->prev, s);
    return s;
}

// [decomp] 0x001f10e8 Component_DestroyAll
void Component_DestroyAll(uint8_t index) {
    for (M_COMPONENTSET* s = static_cast<M_COMPONENTSET*>(ll_first(component_list)); s != nullptr;) {
        M_COMPONENTSET* next = s->next;  // o original lê do bloco já liberado (o heap não muda esses bytes)
        if (s->manager == index) {
            Component_Destroy(s);
            LinkedList_Remove(&component_list, &s->prev, s);
            void* p = s;
            Menu_Free(&p, 1);
        }
        s = next;
    }
}

// [decomp] 0x001f1198 Component_AddComponent
MenuSpriteInfo* Component_AddComponent(M_COMPONENT* c, int16_t left, int16_t top, int16_t right, int16_t bottom,
                                       uint32_t hash, int16_t count) {
    c->left = left;
    c->top = top;
    c->right = right;
    c->bottom = bottom;
    c->hash = hash;
    c->count = uint16_t(count);
    c->pieces = count == 0 ? nullptr : static_cast<MenuSpriteInfo*>(Menu_Malloc(uint32_t(count) * 0x38, 1));
    return c->pieces;
}

// [decomp] 0x001f1200 Component_InitInstance
void Component_InitInstance(MenuSpriteInfo* p, int16_t x, int16_t y, int16_t w, int16_t hh, uint16_t u, uint16_t v,
                            uint16_t uw, uint16_t vh, uint32_t flags, uint32_t colour, float stretch_w,
                            float stretch_h) {
    p->flags = flags;
    p->info.x = x;
    p->info.y = y;
    p->info.w = w;
    p->info.h = hh;
    p->info.u = int16_t(u);
    p->info.v = int16_t(v);
    p->info.uw = int16_t(uw);
    p->info.vh = int16_t(vh);
    p->info.label = 0xffffffffu;
    p->info.priority = 0xffff;
    p->info.colour = colour;
    p->stretch_w = stretch_w;
    p->stretch_h = stretch_h;
    p->info.flags = 0;
    p->info.format = nullptr;
    p->info.hash = 0;
    p->info.viewer = 0;
}

// [decomp] 0x001f1268 Component_GetHandle
// O componente do controle (conjunto em +0x14 >> 16, índice nos 16 bits baixos).
// Só no port: sem o conjunto, um componente zerado (o original devolve nulo e quem chama lê dele).
M_COMPONENT* Component_GetHandle(M_CONTROL* c) {
    M_COMPONENTSET* s = find_set(c->component);
    if (s == nullptr) {
        ++g_menu_guards.no_component;
        no_component = M_COMPONENT{};
        return &no_component;
    }
    return &s->items[c->component & 0xffff];
}

// [decomp] 0x001f0950 Component_SetupInstance
// Monta os sprites do componente do controle no retângulo (x, y, w, h; -1 = o do controle), com as
// peças cujos estados (MenuSpriteInfo::flags) batem com `flags`; as outras ficam escondidas. As peças
// sem esticar ocupam as bordas (à esquerda/em cima, ou à direita/embaixo com as flags 2/1) e as que
// esticam (stretch_w/h) preenchem o que sobra. Com 0x20 (foco) as peças fixas pulsam (Menu_AlphaGet).
void Component_SetupInstance(M_CONTROL* c, uint32_t flags, int32_t x, int32_t y, int32_t w, int32_t hh) {
    M_PAGE* page = Menu_GetPage(c);
    if (page == nullptr) return;
    if (x == -1) x = c->x;
    if (y == -1) y = c->y;
    if (w == -1) w = c->w;
    if (hh == -1) hh = c->h;
    M_COMPONENTSET* set = find_set(c->component);
    if (set == nullptr) return;
    M_COMPONENT* comp = &set->items[c->component & 0xffff];
    int32_t min_x = 0;
    int32_t max_x = w;
    int32_t min_y = 0;
    int32_t max_y = hh;
    if (c->sprites == nullptr) {
        if (comp->count == 0) return;
        c->sprites = static_cast<sprite**>(Menu_Malloc(uint32_t(comp->count) * 4, 1));
        c->sprite_count = comp->count;
        for (int32_t i = 0; i < c->sprite_count; ++i) {
            comp->pieces[i].info.hash = comp->hash;
            c->sprites[i] = Menu_CreateSprite(&comp->pieces[i].info, 1);
        }
    }
    if (comp->pieces == nullptr) return;
    for (int32_t i = 0; i < c->sprite_count; ++i) {
        MenuSpriteInfo* p = &comp->pieces[i];
        if ((p->flags & flags) == 0) {
            if (c->sprites[i] != nullptr) c->sprites[i]->priority = -1;
            continue;
        }
        if ((p->flags & 0x100) != 0) {
            if (p->stretch_h == 0.0f) {
                p->info.w = c->sprites[i]->h;
            } else {
                p->info.w = int16_t(int32_t(float(max_y - min_y) * p->stretch_h));
            }
        }
        int16_t pw;
        if (p->stretch_w == 0.0f) {
            if ((p->flags & 0x400) == 0) {
                if ((p->flags & 2) == 0) {
                    const int32_t v = p->info.x + p->info.w;
                    if (min_x < v) min_x = v;
                } else {
                    const int32_t v = w - p->info.x - p->info.w;
                    if (v < max_x) max_x = v;
                }
            }
            pw = p->info.w;
        } else {
            const float room = float(max_x - min_x);
            const float v = room * p->stretch_w;
            pw = 0.0f <= v ? int16_t(int32_t(v < room ? v : room)) : 0;
        }
        if ((p->flags & 0x200) != 0) {
            if (p->stretch_w == 0.0f) {
                p->info.h = p->info.w;
            } else {
                p->info.h = int16_t(int32_t(float(max_x - min_x) * p->stretch_w));
            }
        }
        int16_t ph;
        if (p->stretch_h == 0.0f) {
            if ((p->flags & 0x800) == 0) {
                if ((p->flags & 1) == 0) {
                    const int32_t v = p->info.y + p->info.h;
                    if (min_y < v) min_y = v;
                } else {
                    const int32_t v = hh - p->info.y - p->info.h;
                    if (v < max_y) max_y = v;
                }
            }
            ph = p->info.h;
        } else {
            const float room = float(max_y - min_y);
            const float v = room * p->stretch_h;
            ph = 0.0f <= v ? int16_t(int32_t(v < room ? v : room)) : 0;
        }
        uint32_t colour = p->info.colour;
        if ((flags & 0x20) != 0 && p->stretch_w == 0.0f && p->stretch_h == 0.0f) {
            const uint32_t a = uint32_t(int32_t(Menu_AlphaGet()));
            colour = ((colour >> 24) + a) << 24 | (((colour >> 16) & 0xff) + a) << 16 | (((colour >> 8) & 0xff) + a) << 8 |
                     (colour & 0xff);
        } else if ((flags & 0xc0) != 0) {
            Menu_AlphaReset();
        }
        const int16_t priority = int16_t(c->depth - page->base.depth);
        int16_t sx;
        if ((p->flags & 2) != 0) {
            sx = int16_t(page->base.x + x + w - pw - p->info.x);
        } else if ((p->flags & 4) != 0) {
            sx = int16_t(page->base.x + x + ((w - pw) >> 1));
        } else {
            sx = int16_t(p->info.x + page->base.x + x);
        }
        int16_t sy;
        if ((p->flags & 1) != 0) {
            sy = int16_t(page->base.y + y + hh - ph - p->info.y);
        } else if ((p->flags & 8) != 0) {
            sy = int16_t(page->base.y + y + ((hh - ph) >> 1));
        } else {
            sy = int16_t(p->info.y + page->base.y + y);
        }
        const uint32_t blend = (p->flags & 0x1000) != 0 ? 0x100 : ((p->flags & 0x2000) != 0 ? 0x200 : 0);
        Menu_InitializeSprite(c->sprites[i], priority, sx, sy, pw, ph, nullptr, comp->hash, colour, 0,
                              uint16_t(p->info.u), uint16_t(p->info.v), uint16_t(p->info.uw), uint16_t(p->info.vh),
                              blend);
    }
}

// [decomp] 0x001f12e0 Component_Free
void Component_Free(M_CONTROL* c) {
    if (c->sprites == nullptr) return;
    for (int32_t i = 0; i < c->sprite_count; ++i) Menu_DeleteSprite(c->sprites[i], 1);
    void* p = c->sprites;
    Menu_Free(&p, 1);
    c->sprites = nullptr;
}

// [decomp] 0x001f1368 Component_Destroy
void Component_Destroy(M_COMPONENTSET* set) {
    for (M_COMPONENT& c : set->items) {
        if (c.pieces != nullptr) {
            void* p = c.pieces;
            Menu_Free(&p, 1);
            c.pieces = nullptr;
        }
    }
}

// ---- foco e navegação ----

// [decomp] 0x001f13c8 Menu_CursorOverMe
// O controle é o de menor profundidade entre os habilitados sob o cursor (bordas incluídas).
int32_t Menu_CursorOverMe(M_MANAGER* m, M_CONTROL* c) {
    M_PAGE* page = m->page;
    if (page == nullptr) return 0;
    const int32_t cy = int16_t(uint16_t(m->cursor->y) - uint16_t(m->page->base.y));
    const int32_t cx = int16_t(uint16_t(m->cursor->x) - uint16_t(m->page->base.x));
    M_CONTROL* under[32];
    int32_t n = 0;
    for (M_CONTROL* k = first_control(page); k != nullptr; k = k->next) {
        if (k->type == MCT_WINDOW || k->state != 0) continue;
        if (cx < k->x || k->x + k->w < cx || cy < k->y || k->y + k->h < cy) continue;
        if (n < 32) under[n++] = k;  // só no port: limite da tabela
    }
    if (n == 0) return 0;
    M_CONTROL* best = under[0];
    for (int32_t i = 0; i < n; ++i) {
        if (under[i]->depth < best->depth) best = under[i];
    }
    return best == c ? 1 : 0;
}

// [decomp] 0x001f14f8 Menu_GetControl
// O controle sob o cursor (estritamente dentro, habilitado, não janela): o menor (maior 512 - w +
// 448 - h), no empate o de menor profundidade; sem nenhum, Menu_GetFirstControl.
M_CONTROL* Menu_GetControl(M_MANAGER* m, int8_t) {
    M_PAGE* page = m->page;
    if (page == nullptr) return Menu_GetFirstControl(m);
    const int32_t cx = int16_t(uint16_t(m->cursor->x) - uint16_t(page->base.x));
    const int32_t cy = int16_t(uint16_t(m->cursor->y) - uint16_t(page->base.y));
    M_CONTROL* under[32];
    int32_t weight[32];
    int32_t n = 0;
    for (M_CONTROL* k = first_control(page); k != nullptr; k = k->next) {
        if (!(k->x < cx && cx < k->x + k->w && k->y < cy && cy < k->y + k->h)) continue;
        if (k->state != 0 || k->type == MCT_WINDOW) continue;
        if (n >= 32) continue;  // só no port: limite da tabela
        under[n] = k;
        weight[n] = int32_t((512.0f - float(k->w)) + (448.0f - float(k->h)));
        ++n;
    }
    if (n == 0) return Menu_GetFirstControl(m);
    int32_t best = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (weight[best] < weight[i]) {
            best = i;
        } else if (weight[i] == weight[best] && under[i]->depth < under[best]->depth) {
            best = i;
        }
    }
    return under[best];
}

// [decomp] 0x001f16d8 Menu_FindControl
// O vizinho do controle com o foco na direção (1 cima, 2 baixo, 4 esquerda, 8 direita): os que se
// sobrepõem na outra dimensão, pelo menor custo (distância na direção + 0.1 x desvio); sem nenhum,
// dá a volta (o mais distante do lado oposto). Põe o cursor no meio dele e devolve-o.
M_CONTROL* Menu_FindControl(M_MANAGER* m, uint8_t direction) {
    struct Entry {
        M_CONTROL* c;
        int16_t cost;
        int16_t pad;
    };
    static_assert(sizeof(Entry) == 8);
    M_CONTROL* cur = Menu_GetControl(m, 0);
    M_CONTROL* k = first_control(m->page);
    if (cur == nullptr) cur = k;
    if (cur == nullptr) return nullptr;
    Entry near_list[64];
    Entry wrap_list[64];
    near_list[0].c = cur;
    near_list[0].cost = 0;
    wrap_list[0].c = cur;
    wrap_list[0].cost = 0;
    uint32_t n_near = 0;
    uint32_t n_wrap = 0;
    for (; k != nullptr && n_near < 0x3f; k = k->next) {
        if (k == cur || k->state != 0 || k->type == MCT_WINDOW) continue;
        if (int32_t(k->w) * int32_t(k->h) == 0) continue;
        const float kw = float(k->w);
        const float kh = float(k->h);
        const float dx0 = float(cur->x - k->x);
        const float dx1 = float(cur->x + cur->w - k->x);
        const float dy0 = float(cur->y - k->y);
        const float dy1 = float(cur->y + cur->h - k->y);
        const bool over_x = dx0 < kw && 0.0f < dx1;
        const bool over_y = 0.0f < dy1 && dy0 < kh;
        int16_t near_cost = 9999;
        int16_t wrap_cost = 9999;
        switch (direction) {
            case 2:
                if (dy0 < kh && over_x) near_cost = int16_t(int32_t(std::fabs(dy1) + std::fabs(dy1) + std::fabs(dx0) * 0.1f));
                if (kh < dy0 && over_x) wrap_cost = int16_t(int32_t(-dy0 + -dy0 + std::fabs(dx0) * 0.1f));
                break;
            case 1:
                if (kh < dy0 && over_x) near_cost = int16_t(int32_t(std::fabs(dy0) + std::fabs(dy0) + std::fabs(dx0) * 0.1f));
                if (dy1 < 0.0f && over_x) wrap_cost = int16_t(int32_t(dy1 + dy1 + std::fabs(dx0) * 0.1f));
                break;
            case 4:
                if (kw < dx0 && over_y) near_cost = int16_t(int32_t(std::fabs(dx0) + std::fabs(dy0)));
                if (dx1 < 0.0f && over_y) wrap_cost = int16_t(int32_t(dx1 + std::fabs(dy0)));
                break;
            case 8:
                if (dx0 < kw && over_y) near_cost = int16_t(int32_t(std::fabs(dx1) + std::fabs(dy0)));
                if (kw < dx0 && over_y) wrap_cost = int16_t(int32_t(std::fabs(dy0) - dx0));
                break;
            default:
                break;
        }
        if (near_cost != 9999) {
            near_list[n_near].c = k;
            near_list[n_near].cost = near_cost;
            n_near = (n_near + 1) & 0xffff;
        }
        if (wrap_cost != 9999 && n_wrap < 64) {  // só no port: limite da tabela
            wrap_list[n_wrap].c = k;
            wrap_list[n_wrap].cost = wrap_cost;
            n_wrap = (n_wrap + 1) & 0xffff;
        }
    }
    QuickSort(near_list, n_near, 8, COMP_MENUCTRL, nullptr);
    if (near_list[0].c == cur) {
        QuickSort(wrap_list, n_wrap, 8, COMP_MENUCTRL, nullptr);
        near_list[0] = wrap_list[0];
    }
    M_CONTROL* f = near_list[0].c;
    m->cursor->x = int16_t(m->page->base.x + f->x + (f->w >> 1));
    m->cursor->y = int16_t(m->page->base.y + f->y + (f->h >> 1));
    m->cursor->w = int16_t(f->w + 10);
    m->cursor->h = int16_t(f->h + 10);
    m->cursor->priority = int8_t(uint8_t(f->depth) + 1);
    return f;
}

// [decomp] 0x001f2708 Menu_GetFirstControl
// O foco da página (se for um controle de verdade) ou o primeiro controle habilitado; sem nenhum, um
// controle de reserva fora da tela (id 0x1fffffff).
M_CONTROL* Menu_GetFirstControl(M_MANAGER* m) {
    M_CONTROL* f = m->page->focus;
    if (f != nullptr && f->id != 0x1fffffffu) return f;
    const auto reset = [] {
        backup_135.id = 0x1fffffffu;
        backup_135.x = -100;
        backup_135.y = -100;
        backup_135.w = 0;
        backup_135.h = 0;
        backup_135.type = 0xff;
    };
    std::memset(&backup_135, 0, sizeof(backup_135));
    reset();
    for (M_CONTROL* c = first_control(m->page); c != nullptr; c = c->next) {
        if (c->type == MCT_WINDOW || c->state != 0) continue;
        reset();
        return c;
    }
    return &backup_135;
}

// [decomp] 0x001f27f8 Menu_GetPage
// A página do controle (subindo pelos donos); só para os tipos que passam em Menu_Assert.
M_PAGE* Menu_GetPage(M_CONTROL* c) {
    if (Menu_Assert(c) == 0) return nullptr;
    if (c->type == MCT_WINDOW) return reinterpret_cast<M_WINDOW*>(c)->page;
    for (M_CONTROL* o = c->owner; o != nullptr; o = o->owner) {
        if (o->type == MCT_PAGE) return reinterpret_cast<M_PAGE*>(o);
    }
    return nullptr;
}

// [decomp] 0x001f2f18 Menu_Assert
// 1 para um controle com dono e de um dos tipos com página (1, 2, 3, 5, 6, 9, 10, 11, 12, 200, 0x72).
uint32_t Menu_Assert(M_CONTROL* c) {
    if (c == nullptr || c->owner == nullptr) return 0;
    switch (c->type) {
        case 1:
        case 2:
        case 3:
        case 5:
        case 6:
        case 200:
        case 9:
        case 10:
        case 11:
        case 12:
        case 0x72:
            return 1;
        default:
            return 0;
    }
}

// ---- mensagens ----

// [decomp] 0x001f1c18 __Menu_SendMessage
// Despacha a mensagem pelo tipo do controle (nenhuma a si mesmo: a == c).
int32_t __Menu_SendMessage(M_CONTROL* c, uint32_t message, int32_t a, int32_t b) {
    if (c == nullptr || val(c) == a) return 0;
    switch (c->type) {
        case MCT_PAGE: return Page_SendMessage(reinterpret_cast<M_PAGE*>(c), message, a, b);
        case MCT_COMBO: return Combo_SendMessage(reinterpret_cast<M_COMBO*>(c), message, a, b);
        case MCT_BUTTON: return Button_SendMessage(reinterpret_cast<M_BUTTON*>(c), message, a, b);
        case MCT_CHECKBOX: return Checkbox_SendMessage(reinterpret_cast<M_CHECKBOX*>(c), message, a, b);
        case MCT_LIST: return List_SendMessage(reinterpret_cast<M_LISTBOX*>(c), message, a, b);
        case MCT_LABEL: return Label_SendMessage(reinterpret_cast<M_LABEL*>(c), message, a, b);
        case MCT_MANAGER: return Manager_SendMessage(reinterpret_cast<M_MANAGER*>(c), message, a, b);
        case MCT_SPIN: return Spin_SendMessage(reinterpret_cast<M_SPIN*>(c), message, a, b);
        case MCT_RADIO: return Radio_SendMessage(reinterpret_cast<M_RADIO*>(c), message, a, b);
        case MCT_SCROLL: return Scroll_SendMessage(reinterpret_cast<M_SCROLL*>(c), message, a, b);
        case MCT_WINDOW: return Window_SendMessage(reinterpret_cast<M_WINDOW*>(c), message, a, b);
        case MCT_TEXT: return Text_SendMessage(reinterpret_cast<M_TEXTBOX*>(c), message, a, b);
        case MCT_MEMO: return Memo_SendMessage(reinterpret_cast<M_MEMOBOX*>(c), message, a, b);
        default: return 0;
    }
}

// [decomp] 0x001f1dc0 Menu_Update
void Menu_Update(M_MANAGER* m, M_PAGE* page, M_CONTROL* c) {
    switch (c->type) {
        case MCT_PAGE: Page_Update(m, reinterpret_cast<M_PAGE*>(c), 1); break;
        case MCT_COMBO: Combo_Update(m, page, reinterpret_cast<M_COMBO*>(c)); break;
        case MCT_BUTTON: Button_Update(m, page, reinterpret_cast<M_BUTTON*>(c)); break;
        case MCT_CHECKBOX: Checkbox_Update(m, page, reinterpret_cast<M_CHECKBOX*>(c)); break;
        case MCT_LABEL: Label_Update(m, page, reinterpret_cast<M_LABEL*>(c), 0); break;
        case MCT_LIST: List_Update(m, page, reinterpret_cast<M_LISTBOX*>(c)); break;
        case MCT_SPIN: Spin_Update(m, page, reinterpret_cast<M_SPIN*>(c)); break;
        case MCT_RADIO: Radio_Update(m, page, reinterpret_cast<M_RADIO*>(c)); break;
        case MCT_SCROLL: Scroll_Update(m, page, reinterpret_cast<M_SCROLL*>(c)); break;
        case MCT_WINDOW: Window_Update(m, page, reinterpret_cast<M_WINDOW*>(c)); break;
        case MCT_TEXT: TextBox_Update(m, page, reinterpret_cast<M_TEXTBOX*>(c)); break;
        case MCT_MEMO: Memo_Update(m, page, reinterpret_cast<M_MEMOBOX*>(c)); break;
        default: break;
    }
}

// [decomp] 0x001f1f40 Menu_InputAction
// Uma ação do jogador (-1: qualquer um) com a máscara de estado (1 ligada, 4 apertada, 8 repetição),
// valendo x 100. Peculiaridades: escolher (0x1f) e Start (0x1e) não valem enquanto voltar (0x22) está
// ligado, e voltar não vale com eles; com um menu de status acima de 3 (fim de missão, menu
// principal), Start e escolher valem um pelo outro.
int32_t Menu_InputAction(int16_t player, int32_t action, uint16_t mask) {
    if (uint32_t(action - 0x1e) < 2) {
        if (action_of(player, 0x22, 1) != 0) return 0;
    }
    if (action == 0x22) {
        if (action_of(player, 0x1f, 1) != 0) return 0;
        if (action_of(player, 0x1e, 1) != 0) return 0;
    }
    if (uint32_t(action - 0x1e) < 2 && 3 < MenuManager_GetStatus()) {
        if (action_of(player, 0x1f, mask) != 0) return 1;
        if (action_of(player, 0x1e, mask) != 0) return 1;
    }
    return action_of(player, action, mask);
}

// [decomp] 0x001f29c0 __Menu_Send
// Envia a mensagem a todos os controles de id `id` (-1: todos) de todas as páginas do gerente.
// Devolve o último resultado não nulo. Peculiaridade do original: depois de 1000 passos (controles e
// páginas) desiste e devolve 0.
int32_t __Menu_Send(uint8_t index, uint32_t id, uint32_t message, int32_t a, int32_t b) {
    int32_t result = 0;
    M_PAGE* page = first_page(&menu_manager[index]);
    if (page == nullptr) return 0;
    for (uint32_t n = 1; n < 0x3e9;) {
        for (M_CONTROL* c = first_control(page); c != nullptr;) {
            ++n;
            if (1000 < n) return 0;
            M_CONTROL* next = c->next;
            if (c->id == id || id == 0xffffffffu) {
                const int32_t r = __Menu_SendMessage(c, message, a, b);
                if (r != 0) result = r;
            }
            c = next;
        }
        page = next_page(page);
        ++n;
        if (page == nullptr) return result;
    }
    return 0;
}

// [decomp] 0x001f2b10 __Menu_SendEx
// Como __Menu_Send, mas só para os de tag `tag` da página atual (e, se nada respondeu, da anterior).
int32_t __Menu_SendEx(uint8_t index, uint32_t id, uint32_t tag, uint32_t message, int32_t a, int32_t b) {
    M_MANAGER* m = &menu_manager[index];
    if (m->page == nullptr) return 0;
    int32_t result = 0;
    uint32_t n = 0;
    M_CONTROL* c = first_control(m->page);
    if (c != nullptr) {
        int32_t last = 0;
        for (n = 1;; ++n) {
            if (0x3e8 < n) return 0;
            if ((c->id == id || id == 0xffffffffu) && c->tag == tag) last = __Menu_SendMessage(c, message, a, b);
            c = c->next;
            result = last;
            if (c == nullptr) break;
        }
    }
    if (result != 0) return result;
    if (m->previous == nullptr) return 0;
    for (c = first_control(m->previous); c != nullptr; c = c->next) {
        ++n;
        if (1000 < n) return 0;
        if ((c->id == id || id == 0xffffffffu) && c->tag == tag) result = __Menu_SendMessage(c, message, a, b);
    }
    return result;
}

// [decomp] 0x001f2ce0 __Menu_SendEx2
// Para os controles de id `id` da página atual cuja tag tem o bit `bit` (bits de 0x80 a 1).
int32_t __Menu_SendEx2(uint8_t index, uint32_t id, uint32_t bit, uint32_t message, int32_t a, int32_t b) {
    M_MANAGER* m = &menu_manager[index];
    if (m->page == nullptr) return 0;
    int32_t result = 0;
    uint32_t n = 1;
    for (M_CONTROL* c = first_control(m->page); c != nullptr; c = c->next, ++n) {
        if (1000 < n) return 0;
        if (c->id != id && id != 0xffffffffu) continue;
        int32_t tag = int32_t(c->tag);
        bool found = false;
        for (int32_t k = 7; 0 <= k; --k) {
            const int32_t v = 1 << k;
            if (0 <= tag - v) {
                tag -= v;
                if (uint32_t(v) == bit) {
                    found = true;
                    break;
                }
            }
        }
        if (found) result = __Menu_SendMessage(c, message, a, b);
    }
    return result;
}

// [decomp] 0x001f24e0 __Menu_SendDelayed
// Agenda a mensagem para daqui a `delay` ticks ao (último) controle de id `id` do gerente.
uint32_t __Menu_SendDelayed(uint32_t delay, uint8_t index, uint32_t id, uint32_t message, int32_t a, int32_t b) {
    int32_t found = 0;
    M_PAGE* page = first_page(&menu_manager[index]);
    if (page != nullptr) {
        int32_t last = 0;
        bool give_up = false;
        for (uint32_t n = 1; n < 0x3e9; ++n) {
            for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
                ++n;
                if (1000 < n) {
                    give_up = true;
                    break;
                }
                if (c->id == id || id == 0xffffffffu) {
                    const int32_t r = __Menu_SendMessage(c, 0x39, 0, 0);
                    if (r != 0) last = r;
                }
            }
            if (give_up) {
                found = 0;
                break;
            }
            found = last;
            page = next_page(page);
            if (page == nullptr) break;
            found = 0;
        }
    }
    if (found == 0) return 0;
    const int32_t slot = free_delay_slot();
    if (slot == -1) return 0;
    DelayedMessage& d = menu_delay_msg[slot];
    d.control = reinterpret_cast<M_CONTROL*>(uintptr_t(uint32_t(found)));
    d.frame = menu_delay_frame + delay;
    d.message = message;
    d.a = a;
    d.b = b;
    return 1;
}

// [decomp] 0x001f33c0 __Menu_SendDelayedMessage
uint32_t __Menu_SendDelayedMessage(uint32_t delay, M_CONTROL* c, uint32_t message, int32_t a, int32_t b) {
    const int32_t slot = free_delay_slot();
    if (slot == -1) return 0;
    DelayedMessage& d = menu_delay_msg[slot];
    d.control = c;
    d.frame = menu_delay_frame + delay;
    d.message = message;
    d.a = a;
    d.b = b;
    return 1;
}

// [decomp] 0x001f3490 Menu_ProcessDelayedMessages
void Menu_ProcessDelayedMessages() {
    ++menu_delay_frame;
    for (DelayedMessage& d : menu_delay_msg) {
        if (d.frame == menu_delay_frame) __Menu_SendMessage(d.control, d.message, d.a, d.b);
    }
}

// [decomp] 0x001f3508 Menu_ClearDelayedMessages
void Menu_ClearDelayedMessages() {
    for (DelayedMessage& d : menu_delay_msg) d.frame = 0;
}

// ---- sprites, memória e utilidades ----

// [decomp] 0x001f2870 Menu_InitializeSprite
void Menu_InitializeSprite(sprite* s, int16_t priority, int16_t x, int16_t y, int16_t w, int16_t hh, char* text,
                           uint32_t hash, uint32_t colour, uint32_t colour2, uint16_t u, uint16_t v, uint16_t uw,
                           uint16_t vh, uint32_t flags) {
    if (s == nullptr) return;
    if (hash != 0) hashtable_set_sprite(s, hash);
    s->priority = int8_t(priority);
    s->x = x;
    s->y = y;
    s->w = w;
    s->viewer = 7;
    s->h = hh;
    s->format = nullptr;
    s->flags = uint16_t(flags);
    if (u != 0xffff) {
        s->u = int16_t(u);
        s->v = int16_t(v);
        s->uw = int16_t(uw);
        s->vh = int16_t(vh);
    }
    if (text == nullptr) {
        s->text = nullptr;
    } else {
        static char kFormat[] = "\xff\x02\xfc\x64\xfb\x64";  // 0x0030d170
        s->text = text;
        s->format = kFormat;
        s->length = uint16_t(std::strlen(text));
    }
    s->colour = colour != 0 ? colour : 0x7f7f7fc0u;
    s->colour2 = colour2;
}

// [decomp] 0x001f2e30 Menu_ResizeControl
void Menu_ResizeControl(M_CONTROL* c, int16_t x, int16_t y, int16_t w, int16_t hh) {
    c->h = hh;
    c->x = x;
    c->y = y;
    c->w = w;
}

// [decomp] 0x001f2e48 Menu_ResizeControl
void Menu_ResizeControl(M_CONTROL* c, M_CONTROL* from) {
    c->x = from->x;
    c->y = from->y;
    c->w = from->w;
    c->h = from->h;
}

// [decomp] 0x001f2e70 Menu_ClipString
// Corta o texto do sprite ao que cabe em `width` pixels e mede o que sobrou (uw, vh); devolve a largura.
int32_t Menu_ClipString(sprite* s, int32_t width, char* format) {
    if (s == nullptr || s->text == nullptr) return 0;
    const uint32_t n = Font_GetTextCountExtent(s->text, format, &s->scale, float(width));
    s->length = uint16_t(n);
    GPOINT extent{};
    Font_GetTextExtent(s->text, format, &extent, &s->scale, int32_t(uint16_t(n)));
    s->uw = int16_t(int32_t(extent.x));
    s->vh = int16_t(int32_t(extent.y));
    return int32_t(extent.x);
}

// [decomp] 0x001f2fa8 Menu_CreateSprite
// Um sprite da câmera 7 (a dos menus) a partir de info, ou um vazio.
sprite* Menu_CreateSprite(SpriteInfo* info, uint32_t) {
    if (info == nullptr) return Sprite_Create();
    info->viewer = 7;
    return Sprite_Create2(info);
}

// [decomp] 0x001f2fe8 Menu_DeleteSprite
void Menu_DeleteSprite(sprite* s, uint32_t) {
    if (s != nullptr) Sprite_Delete(s);
}

// [decomp] 0x001f3008 Menu_Malloc
void* Menu_Malloc(uint32_t size, uint32_t) { return Mem_Malloc(size, 0x3104, 0); }

// [decomp] 0x001f3028 Menu_Free
void Menu_Free(void** p, int32_t) { Mem_Free(p); }

// [decomp] 0x001f3048 Menu_SetAction
void Menu_SetAction(int16_t player, uint32_t action, uint8_t state) {
    if (player == -1) {
        Input_SetAction(0, action, state);
        Input_SetAction(1, action, state);
        Input_SetAction(2, action, state);
        Input_SetAction(3, action, state);
    } else {
        Input_SetAction(player, action, state);
    }
}

// [decomp] 0x001f30d8 Menu_GetLastController
uint32_t Menu_GetLastController() { return 0; }

// [decomp] 0x001f30e0 Menu_GetNoInputCount
uint32_t Menu_GetNoInputCount() { return frame_counter - last_action; }

// [decomp] 0x001f30f0 Menu_ResetNoInputCount
void Menu_ResetNoInputCount() { last_action = frame_counter; }

// [decomp] 0x001f3170 Menu_AlphaUpdate
// O pulso dos destaques: alpha = 64 x seno, um passo de 0.2 por tick.
void Menu_AlphaUpdate() {
    alpha_counter = alpha_counter + 0.2f;
    ++frame_counter;
    if (6.2831855f <= alpha_counter) alpha_counter = 0.0f;
    alpha = int16_t(int32_t(PS2Sinf(alpha_counter) * 64.0f));
}

// [decomp] 0x001f31e0 Menu_AlphaReset
void Menu_AlphaReset() { alpha_counter = 0.0f; }

// [decomp] 0x001f31e8 Menu_AlphaGet
int16_t Menu_AlphaGet() { return alpha; }

// [decomp] 0x001f31f0 Menu_EnableSounds
// Os sons de interface de cada página (bits de Menu_PlaySound).
void Menu_EnableSounds(uint32_t page) {
    switch (page) {
        case 0x40000002:
        case 0x40000009:
        case 0x40000015:
            enabled_sound_flags = 1;
            return;
        case 0x40000005:
        case 0x4000001c:
        case 0x40000025:
        case 0x4000004b:
            enabled_sound_flags = 0xb;
            return;
        case 0x40000022:
            enabled_sound_flags = 0xd;
            return;
        case 0x40000033:
            enabled_sound_flags = 3;
            return;
        case 0x40000036:
        case 0x40000038:
            enabled_sound_flags = 7;
            return;
        case 0x40000037:
        case 0x4000003a:
        case 0x4000003b:
        case 0x4000003c:
        case 0x4000003d:
        case 0x4000003e:
            enabled_sound_flags = 8;
            return;
        default:
            enabled_sound_flags = 9;
            return;
    }
}

// [decomp] 0x001f3278 Menu_PlaySound
// Os sons de interface: 0-2 escolher/outros botões (se a página os liga), 3-6 a barra, 7 mudar o foco,
// 8 voltar.
void Menu_PlaySound(uint32_t sound) {
    uint32_t id = 0;
    switch (sound) {
        case 0: id = (enabled_sound_flags & 1) != 0 ? 0x1da : 0; break;
        case 1: id = (enabled_sound_flags & 2) != 0 ? 0x1da : 0; break;
        case 2: id = (enabled_sound_flags & 4) != 0 ? 0x1da : 0; break;
        case 3:
        case 5: id = 0x1d8; break;
        case 4:
        case 6: id = 0x1d9; break;
        case 7: id = 0x1da; break;
        case 8: id = (enabled_sound_flags & 8) != 0 ? 0x1d9 : 0; break;
        default: break;
    }
    if (id != 0) Sound_Play(id, 100.0f, 0, 0);
}

// [decomp] 0x001f3328 Menu_UpdateTextFmt
void Menu_UpdateTextFmt() {
    text_count = text_count + 0.1f;
    if (6.2831855f <= text_count) text_count = 0.0f;
    throb = PS2Sinf(text_count) * 0.03125f + 1.0f;
}

// [decomp] 0x001f3398 Menu_GetNormalTextFmt
void Menu_GetNormalTextFmt(sprite* s) {
    if (s == nullptr) return;
    s->scale.y = 1.0f;
    s->scale.x = 1.0f;
}

// [decomp] 0x001f3540 Menu_StringInit
void Menu_StringInit(int32_t) {}

// [decomp] 0x001f3548 Menu_StringKill
void Menu_StringKill() {}

// ---- só no port ----

void menu_debug_power_on() {
    std::memset(menu_manager, 0, sizeof(menu_manager));
    file_start = nullptr;
    component_list = LINKEDLIST{};
    frame_counter = 0;
    menu_delay_frame = 0;
    throb = 0.0f;
    alpha = 0;
    alpha_counter = 0.0f;
    text_count = 0.0f;
    enabled_sound_flags = 0;
    last_action = 0;
    last_controller = -1;
    std::memset(menu_delay_msg, 0, sizeof(menu_delay_msg));
    std::memset(&backup_135, 0, sizeof(backup_135));
    user_restarted_or_quit = 0;
    help_control = nullptr;
    g_menu_guards = MenuGuards{};
    g_menu_stats = MenuStats{};
    g_menu_file = 0;
    menu_mouse_debug_power_on();
    Menu_SetPortTick(nullptr);
}
