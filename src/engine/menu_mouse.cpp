// Só no port: o mouse nos menus. Ver menu_mouse.h. Nada aqui vem do ACTION.ELF (o jogo não tem código de
// mouse); as mudanças passam pelos caminhos do próprio sistema de menus: o cursor do gerente (o foco segue ele
// em Page_Update), as ações de escolher e voltar (Input_SetAction por um tick) e o passo das barras de
// Scroll_Update (posição, som e o evento 0x49 ao dono).

#include "engine/menu_mouse.h"

#include <cmath>
#include <cstdio>

#include "engine/input.h"
#include "engine/menu.h"
#include "platform/psi_input.h"

MenuMouseStats g_menu_mouse_stats;

namespace {

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }

M_CONTROL* first_control(M_PAGE* p) { return static_cast<M_CONTROL*>(ll_first(p->controls)); }

// O que o relatório mostra do último tick.
bool g_inside = false;
float g_x = 0.0f, g_y = 0.0f;
bool g_active = false;
uint32_t g_target_id = 0, g_target_tag = 0;
uint8_t g_target_type = 0;
// A roda que nenhum controle usou neste tick.
int32_t g_unused_wheel = 0;
// A barra sendo arrastada (e a página dela, para conferir que ainda existe).
M_SCROLL* g_drag = nullptr;
M_PAGE* g_drag_page = nullptr;

// As margens do componente do controle, sem o contador de Component_GetHandle (o mouse não é o jogo).
struct Margins {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
};
Margins margins_of(const M_CONTROL* c) {
    Margins r;
    for (M_COMPONENTSET* s = static_cast<M_COMPONENTSET*>(ll_first(component_list)); s != nullptr; s = s->next) {
        if (s->id != c->component >> 16) continue;
        const M_COMPONENT& k = s->items[c->component & 0xffff];
        r.left = k.left;
        r.top = k.top;
        r.right = k.right;
        r.bottom = k.bottom;
        break;
    }
    return r;
}

// A página de um controle (pelos donos; sem Menu_Assert, para os sub-controles das listas e rádios).
M_PAGE* page_of(const M_CONTROL* c) {
    for (const M_CONTROL* o = c; o != nullptr; o = o->owner) {
        if (o->type == MCT_PAGE) return reinterpret_cast<M_PAGE*>(const_cast<M_CONTROL*>(o));
    }
    return nullptr;
}

bool inside_strict(const M_CONTROL* c, int32_t cx, int32_t cy) {
    return c->x < cx && cx < c->x + c->w && c->y < cy && cy < c->y + c->h;
}

bool contains(M_PAGE* page, const M_CONTROL* c) {
    for (M_CONTROL* k = first_control(page); k != nullptr; k = k->next) {
        if (k == c) return true;
    }
    return false;
}

// O jogador que lê a entrada do gerente (todos: o 0, o do PC).
int16_t player_of(const M_MANAGER* m) { return m->player < 0 || m->player > 3 ? 0 : m->player; }

// Uma ação do menu apertada neste tick (como se o botão do controle descesse agora).
void press(M_MANAGER* m, uint32_t action) { Input_SetAction(player_of(m), action, kActionDown | kActionPressed); }

// O cursor do gerente sobre o controle: no ponto do mouse se ele está dentro do controle, senão no meio dele.
// Page_Update muda o foco para o controle sob o cursor (Menu_GetControl) com os eventos e o som do jogo.
void cursor_on(M_MANAGER* m, M_CONTROL* c, float x, float y) {
    if (m->cursor == nullptr || m->page == nullptr) return;
    const int32_t cx = int32_t(std::floor(x)), cy = int32_t(std::floor(y));
    if (inside_strict(c, cx - m->page->base.x, cy - m->page->base.y)) {
        m->cursor->x = int16_t(cx);
        m->cursor->y = int16_t(cy);
    } else {
        m->cursor->x = int16_t(m->page->base.x + c->x + (c->w >> 1));
        m->cursor->y = int16_t(m->page->base.y + c->y + (c->h >> 1));
    }
}

// Um passo da barra como em Scroll_Update: a posição (dando a volta só com `wrap` da barra e `allow_wrap`),
// o som do passo e o evento 0x49 ao dono (a lista ou o rádio escolhem a linha da posição).
bool scroll_step(M_SCROLL* s, int32_t dir, bool allow_wrap) {
    int32_t next = s->pos + (dir < 0 ? -1 : 1);
    const bool wrap = allow_wrap && s->wrap != 0;
    if (next < s->min) {
        if (!wrap) return false;
        next = s->max;
    } else if (next > s->max) {
        if (!wrap) return false;
        next = s->min;
    }
    if (next == s->pos) return false;
    s->pos = next;
    Menu_PlaySound(dir < 0 ? 3 : 4);
    __Menu_SendMessage(s->base.owner, 0x49, val(s), 0);
    ++g_menu_mouse_stats.steps;
    return true;
}

// As rodas do menu principal (codinomes, dificuldade, missões, opções do codinome): a barra de rolagem que tem o foco
// e os cinco rótulos dos itens (tags 0-4; o 2 é o item da vez). A barra é minúscula (16 x 32 no arquivo), então o
// mouse mexe na roda pelos rótulos e pela roda do mouse em qualquer lugar da página.
struct WheelIds {
    uint32_t bar;
    uint32_t labels;
};
constexpr WheelIds kWheels[] = {{0x100000e5, 0x100000e4}, {0x100000eb, 0x100000ec}, {0x10000100, 0x100000ff},
                                {0x100000bf, 0x100000e6}, {0x1000000e, 0x100000e3}};

// A barra da roda se ela é o foco; nulo senão.
M_SCROLL* focused_wheel(M_PAGE* page) {
    M_CONTROL* f = page->focus;
    if (f == nullptr || f->type != MCT_SCROLL) return nullptr;
    for (const WheelIds& w : kWheels) {
        if (f->id == w.bar) return reinterpret_cast<M_SCROLL*>(f);
    }
    return nullptr;
}

// O rótulo da roda (tag 0-4) sob o ponto, -1 fora deles.
int32_t wheel_label_at(M_PAGE* page, const M_SCROLL* bar, int32_t cx, int32_t cy) {
    uint32_t labels = 0;
    for (const WheelIds& w : kWheels) {
        if (bar->base.id == w.bar) labels = w.labels;
    }
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->id != labels || c->tag > 4 || (c->state & 1) != 0) continue;
        if (inside_strict(c, cx - page->base.x, cy - page->base.y)) return int32_t(c->tag);
    }
    return -1;
}

// A barra no valor (o trilho sob o mouse).
void scroll_set(M_SCROLL* s, int32_t v) {
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    if (v == s->pos) return;
    const int32_t dir = v < s->pos ? -1 : 1;
    s->pos = v;
    Menu_PlaySound(dir < 0 ? 3 : 4);
    __Menu_SendMessage(s->base.owner, 0x49, val(s), 0);
    ++g_menu_mouse_stats.sets;
}

// A linha `row` (índice real) escolhida, como cima/baixo: o evento 0x49 vai ao dono.
void list_pick(M_LISTBOX* l, int32_t row) {
    if (row < 0 || List_SendMessage(l, 0x34, 0, 0) == row) return;
    List_SendMessage(l, 0x1d, row, 0);
    ++g_menu_mouse_stats.rows;
}

bool list_has_bar(const M_LISTBOX* l) { return 1 < l->visible && l->visible < int32_t(l->rows.count); }

// O ponto está na barra da direita da lista (com mais linhas do que cabem).
bool on_list_bar(M_LISTBOX* l, float x) {
    M_PAGE* page = page_of(&l->base);
    if (page == nullptr || !list_has_bar(l)) return false;
    return float(page->base.x + l->base.x + l->base.w - 0x10) <= x;
}

// Um clique na barra da lista: as setas andam uma linha; o meio leva a escolha à altura proporcional.
void list_bar_click(M_LISTBOX* l, float y) {
    M_PAGE* page = page_of(&l->base);
    if (page == nullptr) return;
    const float y0 = float(page->base.y + l->base.y);
    const float h = float(l->base.h);
    if (y < y0 + 16.0f) {
        scroll_step(&l->scroll, -1, false);
    } else if (y >= y0 + h - 16.0f) {
        scroll_step(&l->scroll, 1, false);
    } else {
        const float t = (y - y0 - 16.0f) / std::fmax(1.0f, h - 32.0f);
        const int32_t last = int32_t(l->rows.count) - 1;
        LIST_ROW* r = List_SetIndex(l, &l->rows, int32_t(std::lround(t * float(last))));
        if (r != nullptr) list_pick(l, List_GetIndex_RealItem(l, &l->rows, r));
    }
}

}  // namespace

M_CONTROL* Menu_MouseHitTest(M_MANAGER* m, float x, float y, bool* via_label) {
    if (via_label != nullptr) *via_label = false;
    M_PAGE* page = m != nullptr ? m->page : nullptr;
    if (page == nullptr) return nullptr;
    const int32_t cx = int32_t(std::floor(x)) - page->base.x;
    const int32_t cy = int32_t(std::floor(y)) - page->base.y;
    // A regra de Menu_GetControl: o menor (maior 512 - w + 448 - h), no empate o de menor profundidade.
    M_CONTROL* best = nullptr;
    int32_t best_weight = 0;
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->state != 0 || c->type == MCT_WINDOW || !inside_strict(c, cx, cy)) continue;
        const int32_t weight = int32_t((512.0f - float(c->w)) + (448.0f - float(c->h)));
        if (best == nullptr || best_weight < weight || (weight == best_weight && c->depth < best->depth)) {
            best = c;
            best_weight = weight;
        }
    }
    if (best != nullptr) return best;
    // O nome de uma linha: um rótulo desabilitado com o id e a tag de um controle habilitado na mesma altura.
    for (M_CONTROL* l = first_control(page); l != nullptr; l = l->next) {
        if (l->type != MCT_LABEL || (l->state & 1) != 0 || (l->state & 2) == 0 || !inside_strict(l, cx, cy)) continue;
        for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
            if (c == l || c->id != l->id || c->tag != l->tag || c->state != 0) continue;
            if (c->type == MCT_WINDOW || c->type == MCT_LABEL) continue;
            if (!(c->y < l->y + l->h && l->y < c->y + c->h)) continue;
            if (via_label != nullptr) *via_label = true;
            return c;
        }
    }
    return nullptr;
}

int32_t Menu_MouseScrollPart(const M_SCROLL* s, float x, float y) {
    const M_PAGE* page = page_of(&s->base);
    if (page == nullptr) return 0;
    if ((s->mode & 2) == 0) {
        const float x0 = float(page->base.x + s->base.x);
        if (x < x0 + float(s->margin_a)) return -1;
        if (x >= x0 + float(s->base.w - s->margin_b)) return 1;
    } else {
        const float y0 = float(page->base.y + s->base.y);
        if (y < y0 + float(s->margin_a)) return -1;
        if (y >= y0 + float(s->base.h - s->margin_b)) return 1;
    }
    return 0;
}

int32_t Menu_MouseScrollValue(const M_SCROLL* s, float x, float y) {
    const M_PAGE* page = page_of(&s->base);
    if (page == nullptr || s->max <= s->min) return s->min;
    const bool vertical = (s->mode & 2) != 0;
    const float origin = vertical ? float(page->base.y + s->base.y) : float(page->base.x + s->base.x);
    const float extent = vertical ? float(s->base.h) : float(s->base.w);
    const float p = vertical ? y : x;
    const float span = float(s->max - s->min);
    const float track0 = origin + float(s->margin_a);
    float track = extent - float(s->margin_a) - float(s->margin_b);
    float t = 0.0f;
    if (s->proportional != 0 || (s->mode & 1) != 0) {
        // A barra cheia até o valor (o volume do jogo): o fim dela fica sob o mouse.
        t = (p - track0) / std::fmax(1.0f, track);
    } else {
        // O cursor centrado no mouse (Scroll_Update: o passo é o trilho menos o cursor, dividido pela faixa).
        const float thumb = float(vertical ? s->thumb.h : s->thumb.w);
        track -= thumb;
        t = (p - track0 - thumb * 0.5f) / std::fmax(1.0f, track);
    }
    int32_t v = s->min + int32_t(std::lround(t * span));
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    return v;
}

int32_t Menu_MouseListRow(M_LISTBOX* l, float y) {
    M_PAGE* page = page_of(&l->base);
    if (page == nullptr || l->row_height <= 0.0f) return -1;
    const float band = float(page->base.y + l->base.y + margins_of(&l->base).top);
    if (y < band) return -1;
    const int32_t line = l->top + int32_t(std::floor((y - band) / l->row_height));
    if (line >= l->top + l->visible || line >= int32_t(l->rows.count)) return -1;
    LIST_ROW* r = List_SetIndex(l, &l->rows, line);
    return r != nullptr ? List_GetIndex_RealItem(l, &l->rows, r) : -1;
}

int32_t Menu_MouseTakeWheel() {
    const int32_t w = g_unused_wheel;
    g_unused_wheel = 0;
    return w;
}

void Menu_MouseUpdate(M_MANAGER* m) {
    const PsiMenuMouse& mm = psiInput_MenuMouse();
    g_unused_wheel = 0;
    g_inside = mm.inside;
    g_x = mm.x;
    g_y = mm.y;
    g_active = mm.active;
    g_target_id = g_target_tag = 0;
    g_target_type = 0;
    M_PAGE* page = m->page;
    if (g_drag != nullptr && (!mm.held || page != g_drag_page || page == nullptr || !contains(page, &g_drag->base))) {
        g_drag = nullptr;
        g_drag_page = nullptr;
    }
    // A captura de uma tecla (página de teclas) e o menu sem entrada (0x68) ficam sem o mouse.
    if (!mm.inside || page == nullptr || m->frozen != 0 || psiInput_CaptureState() != PsiCapture::Idle) return;
    if (g_drag != nullptr) {
        scroll_set(g_drag, Menu_MouseScrollValue(g_drag, mm.x, mm.y));
        return;
    }
    bool via_label = false;
    M_CONTROL* target = Menu_MouseHitTest(m, mm.x, mm.y, &via_label);
    M_CONTROL* focus = page->focus;
    if (target != nullptr) {
        g_target_id = target->id;
        g_target_tag = target->tag;
        g_target_type = target->type;
    }
    const bool same_group = target != nullptr && focus != nullptr && target->id == focus->id;
    const bool keeps_focus = same_group && (target->type == MCT_LIST || target->type == MCT_RADIO ||
                                            target->type == MCT_SCROLL || target->type == MCT_MEMO);
    // As abas da pausa (botões 0x10000028, C_GCPAUSE_Handler: a troca de painel é o evento de foco): passar por
    // cima não troca a aba e clicar só leva o foco. Os outros botões de mesmo id (as teclas do teclado da tela do
    // menu principal, 0x10000074) são botões comuns.
    const bool tab_group = same_group && target->type == MCT_BUTTON && target->id == 0x10000028u;
    const bool hover_moves = !same_group || (target->type == MCT_BUTTON && !tab_group);
    // Passar por cima: o foco vai para o controle; numa lista, a linha sob o mouse.
    if (mm.moved && mm.active && target != nullptr) {
        if (hover_moves && target != focus) {
            cursor_on(m, target, mm.x, mm.y);
            ++g_menu_mouse_stats.hovers;
        }
        if (target->type == MCT_LIST && !via_label) {
            M_LISTBOX* l = reinterpret_cast<M_LISTBOX*>(target);
            if (!on_list_bar(l, mm.x)) list_pick(l, Menu_MouseListRow(l, mm.y));
        }
    }
    // Clique num rótulo de uma roda do menu principal: o do meio escolhe, os outros giram a roda até ele.
    M_SCROLL* wheel = focused_wheel(page);
    const int32_t wheel_label =
        wheel != nullptr ? wheel_label_at(page, wheel, int32_t(std::floor(mm.x)), int32_t(std::floor(mm.y))) : -1;
    if (mm.pressed && wheel_label >= 0) {
        ++g_menu_mouse_stats.clicks;
        if (wheel_label == 2) {
            press(m, 0x1f);
            ++g_menu_mouse_stats.selects;
        } else {
            for (int32_t i = 0; i < (wheel_label < 2 ? 2 - wheel_label : wheel_label - 2); ++i) {
                scroll_step(wheel, wheel_label < 2 ? -1 : 1, false);
            }
        }
    } else if (mm.pressed && target != nullptr) {
        ++g_menu_mouse_stats.clicks;
        if (!keeps_focus && target != focus) cursor_on(m, target, mm.x, mm.y);
        switch (target->type) {
            case MCT_LIST: {
                M_LISTBOX* l = reinterpret_cast<M_LISTBOX*>(target);
                if (via_label) {
                    break;
                }
                if (on_list_bar(l, mm.x)) {
                    list_bar_click(l, mm.y);
                    break;
                }
                const int32_t row = Menu_MouseListRow(l, mm.y);
                if (row < 0) break;
                list_pick(l, row);
                press(m, 0x1f);
                ++g_menu_mouse_stats.selects;
                break;
            }
            case MCT_RADIO: {
                M_RADIO* r = reinterpret_cast<M_RADIO*>(target);
                const int32_t part = via_label ? 1 : Menu_MouseScrollPart(&r->scroll, mm.x, mm.y);
                scroll_step(&r->scroll, part < 0 ? -1 : 1, true);
                break;
            }
            case MCT_SCROLL: {
                M_SCROLL* s = reinterpret_cast<M_SCROLL*>(target);
                if (via_label) break;
                const int32_t part = Menu_MouseScrollPart(s, mm.x, mm.y);
                if (part != 0) {
                    scroll_step(s, part, false);
                } else {
                    scroll_set(s, Menu_MouseScrollValue(s, mm.x, mm.y));
                    g_drag = s;
                    g_drag_page = page;
                }
                break;
            }
            case MCT_MEMO:
                break;
            default:
                // Uma aba da pausa só recebe o foco: a troca é o evento 0x4e.
                if (tab_group && target != focus) break;
                press(m, 0x1f);
                ++g_menu_mouse_stats.selects;
                break;
        }
    }
    // A roda: listas uma linha, rádios e barras horizontais um passo; o resto fica para a página.
    if (mm.wheel != 0) {
        bool used = false;
        if (target != nullptr && !via_label) {
            const int32_t notches = mm.wheel < 0 ? -mm.wheel : mm.wheel;
            for (int32_t i = 0; i < notches; ++i) {
                if (target->type == MCT_LIST) {
                    scroll_step(&reinterpret_cast<M_LISTBOX*>(target)->scroll, mm.wheel > 0 ? -1 : 1, false);
                    used = true;
                } else if (target->type == MCT_RADIO) {
                    scroll_step(&reinterpret_cast<M_RADIO*>(target)->scroll, mm.wheel > 0 ? 1 : -1, true);
                    used = true;
                } else if (target->type == MCT_SCROLL) {
                    M_SCROLL* s = reinterpret_cast<M_SCROLL*>(target);
                    const bool vertical = (s->mode & 2) != 0;
                    scroll_step(s, (mm.wheel > 0) != vertical ? 1 : -1, false);
                    used = true;
                }
            }
        }
        // Uma roda do menu principal gira com a roda do mouse em qualquer lugar da página (para cima, o item de cima).
        if (!used && wheel != nullptr) {
            const int32_t notches = mm.wheel < 0 ? -mm.wheel : mm.wheel;
            for (int32_t i = 0; i < notches; ++i) scroll_step(wheel, mm.wheel > 0 ? -1 : 1, false);
            used = true;
        }
        if (!used) g_unused_wheel = mm.wheel;
    }
    // Botão direito ou Mouse4: voltar (a ação do triângulo).
    if (mm.back) {
        press(m, 0x22);
        ++g_menu_mouse_stats.backs;
    }
}

int Menu_MouseDescribe(char* out, size_t size) {
    return std::snprintf(out, size,
                         "mouse dentro %d x %.1f y %.1f ativo %d alvo %08x/%u tipo %u | focos %u cliques %u escolhas %u "
                         "passos %u valores %u linhas %u voltar %u",
                         g_inside ? 1 : 0, double(g_x), double(g_y), g_active ? 1 : 0, g_target_id, g_target_tag,
                         g_target_type, g_menu_mouse_stats.hovers, g_menu_mouse_stats.clicks, g_menu_mouse_stats.selects,
                         g_menu_mouse_stats.steps, g_menu_mouse_stats.sets, g_menu_mouse_stats.rows,
                         g_menu_mouse_stats.backs);
}

void menu_mouse_debug_power_on() {
    g_menu_mouse_stats = MenuMouseStats{};
    g_inside = false;
    g_x = g_y = 0.0f;
    g_active = false;
    g_target_id = g_target_tag = 0;
    g_target_type = 0;
    g_unused_wheel = 0;
    g_drag = nullptr;
    g_drag_page = nullptr;
}
