// Os controles dos menus: rótulo (Label_*), botão (Button_*), caixa de marcar (Checkbox_*), barra de
// rolagem (Scroll_*), lista (List_*), rádio (Radio_*) e memo (Memo_*). Ver engine/menu.h.
//
// Divergências (comentadas nas funções):
// - Label_SendMessage 0x23 não escreve além de M_LABEL::format_buf (16 bytes; o original usa sprintf e
//   escreveria por cima do sprite modelo com um formato longo; os do disco têm até 8 bytes).
// - Scroll_Init com faixa vazia (max = min - 1) divide por 1 (o original para no `break 7` da divisão
//   por zero); conta em g_menu_guards.scroll_zero_range.
// - List_SendMessage 0xf zera M_LIST_ROW::continuation (o original deixa o lixo do heap).
// - Os campos que o original lê antes de escrever (a largura do rótulo em Label_Init, a altura da lista
//   em List_Init e do memo em Memo_Init, vindas do heap) são lidos do mesmo jeito: o próximo update
//   recalcula tudo.
// - Página nula (controle sem dono) onde o original leria o foco dela: nada é atualizado
//   (g_menu_guards.no_page).

#include "engine/menu.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/font.h"
#include "engine/memory.h"
#include "engine/txt.h"
#include "platform/psi.h"

namespace {

char kEmpty[] = "";                                   // 0x0030d5a0
char kSpace[] = " ";                                  // 0x0030d5a8
char kLabelFormat[] = "\xff\x02\xfe\x03\xfc\x64\xfb\x64";  // 0x003096c0 formato padrão do rótulo
char kScale100[] = "\xfc\x64\xfb\x64";                // 0x0030d5b8
char kButtonFormat[] = "\xff\x02\xfe\x03";            // 0x0030d140
char kCheckFormat[] = "\xff\x02";                     // 0x0030d148
char kRadioFormat[] = "\xff\x02\xfe\x03";             // 0x0030d640
char kListFormat1[] = "\xff\x02\xfc\x64\xfb\x64";     // 0x0030d5c8
char kListFormat3[] = "\xff\x03\xfc\x64\xfb\x64";     // 0x0030d5f0
char kListFormat2[] = "\xff\x01\xfc\x64\xfb\x64";     // 0x0030d5f8
char kMemoFormat[] = "\xff\x02";                      // 0x0030d628
char kMemoEmpty[] = "";                               // 0x0030d630
char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }
template <class T>
T* ptr(int32_t v) {
    return reinterpret_cast<T*>(uintptr_t(uint32_t(v)));
}

M_MANAGER* manager_of(M_PAGE* page) { return reinterpret_cast<M_MANAGER*>(page->base.owner); }

// A cor com o pulso dos destaques somado aos três bytes altos.
uint32_t pulse(uint32_t c) {
    const uint32_t a = uint32_t(int32_t(Menu_AlphaGet()));
    return ((c >> 24) + a) << 24 | (((c >> 16) & 0xff) + a) << 16 | (((c >> 8) & 0xff) + a) << 8 | (c & 0xff);
}

// Atualiza já o controle se a página dele tem foco (as mensagens que mudam o texto ou o estado).
void refresh_label(M_LABEL* l) {
    M_PAGE* page = Menu_GetPage(&l->base);
    if (page == nullptr) {
        ++g_menu_guards.no_page;
        return;
    }
    if (page->focus != nullptr) Label_Update(manager_of(page), page, l, 0);
}

// Atualiza a lista ou o memo sem entrada (List_Update/Memo_Update com M_MANAGER::input zerado).
template <class T, class F>
void refresh_quiet(T* c, F update, bool restore_one) {
    M_PAGE* page = Menu_GetPage(&c->base);
    if (page == nullptr) {
        ++g_menu_guards.no_page;
        return;
    }
    M_MANAGER* m = manager_of(page);
    if (page->focus == nullptr) return;
    const uint8_t saved = m->input;
    m->input = 0;
    update(m, page, c);
    m->input = restore_one ? 1 : saved;
}

LIST_ROW* first_row(const LINKEDLIST& rows) { return static_cast<LIST_ROW*>(ll_first(rows)); }

// As três ações de escolha sobre o controle (0x1f, 0x20, 0x21): 1, 2, 3 ou 0.
int32_t pressed_button(M_MANAGER* m) {
    int32_t r = Menu_InputAction(m->player, 0x1f, 4) != 0 ? 1 : 0;
    if (Menu_InputAction(m->player, 0x20, 4) != 0) r = 2;
    if (Menu_InputAction(m->player, 0x21, 4) != 0) r = 3;
    return r;
}

// Os eventos de escolha (botões 1-3) ao dono, com o som.
void send_choice(M_MANAGER* m, M_CONTROL* c, int32_t which) {
    if (m->input == 0 || m->frozen != 0) return;
    if (which == 1) {
        Menu_PlaySound(0);
        __Menu_SendMessage(c->owner, 0x4b, val(c), 0);
    } else if (which == 2) {
        Menu_PlaySound(1);
        __Menu_SendMessage(c->owner, 0x5d, val(c), 0);
    } else if (which == 3) {
        Menu_PlaySound(2);
        __Menu_SendMessage(c->owner, 0x5e, val(c), 0);
    }
}

}  // namespace

// ---- rótulo ----

// [decomp] 0x00218638 Label_Init
// O rótulo: um texto (NEW_CONTROL +8, ou um rótulo de texto pela mensagem 0x18) ou uma imagem (0x24),
// desenhado por um sprite que Label_Update monta a partir do modelo M_LABEL::text. Um rótulo de tela
// cheia (640 x 480) vira a tela do PS2 deslocada (0x40, 0x10).
void Label_Init(M_LABEL* l, NEW_CONTROL* desc, M_CONTROL* owner) {
    if (0x27f < desc->w && 0x1df < desc->h) {
        desc->w = 0x200;
        desc->h = 0x1c0;
        desc->x = 0x40;
        desc->y = 0x10;
    }
    l->base.owner = owner;
    l->base.type = MCT_LABEL;
    l->base.state = 0;
    l->base.unknown_10 = 0;
    l->base.component = 0;
    l->base.id = desc->id;
    l->base.sprites = nullptr;
    l->base.tag = desc->unknown_0c;
    l->colour = 0xff;
    l->base.depth = desc->depth;
    l->colour_off = 0xffffff7fu;
    l->mode = 3;
    l->outline = 1;
    l->faded = 0;
    char* text = reinterpret_cast<char*>(uintptr_t(desc->unknown_08));
    l->label = 0xffffffffu;
    l->colour_throb = 0x808080ffu;
    l->string = text;
    l->sprite_flags = 0;
    l->throb = 0;
    l->keep_focus = 0;
    if (text == nullptr) l->string = kEmpty;
    std::memcpy(l->format_buf, kLabelFormat, 9);
    l->text.text = nullptr;
    l->text.format = desc->format;
    l->text.w = desc->w;
    l->text.priority = int8_t(uint8_t(l->base.depth));
    l->text.h = desc->h;
    l->text.colour = 0x7f7f7fc0u;
    l->text.colour2 = 0x80;
    l->text.viewer = 7;
    l->text.scale.y = 1.0f;
    l->text.format = kLabelFormat;
    l->text.flags = 0;
    l->text.created = 0;
    l->text.scale.x = 1.0f;
    l->spr = nullptr;
    sprite* s = &l->text;
    Sprite_SetText(s, text != nullptr ? text : kEmpty, 0);
    Menu_ClipString(s, desc->w, l->text.format);
    // Como no original, a largura do próprio rótulo ainda não foi escrita aqui (vem do heap).
    if (Font_GetAlignment(l->text.format) == 3) {
        const int32_t v = Menu_ClipString(s, l->base.w, l->text.format);
        l->text.x = int16_t(l->text.x + int16_t(v >> 1));
    }
    if (Font_GetAlignment(l->text.format) == 2) {
        const int16_t v = int16_t(Menu_ClipString(s, l->base.w, l->text.format));
        l->text.x = int16_t(l->text.x + v);
    }
    l->base.x = desc->x;
    l->base.y = desc->y;
    l->base.w = desc->w;
    l->base.h = desc->h;
    Script_PrepareScripting(&l->base);
}

// [decomp] 0x00218890 Label_SendMessage
// 0x18 texto (a: o texto; senão o rótulo b; nada: " "), 0x1a cor (b = 0: as três), 0x1b/0x1c cor com e
// sem foco, 0x23 formato, 0x24 imagem, 0x29 cor do pulso, 0x2b estado, 0x2d recorte da imagem, 0x58
// segunda cor, 0x62 contorno, 0x6c flags do sprite, 0x70 pulso, 0x76 mantém a cor de foco; consultas
// 0x2f-0x32, 0x39, 0x3d-0x3f; 0x47 libera.
int32_t Label_SendMessage(M_LABEL* l, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x14:
            return Script_ExitLoop(&l->base);
        case 0x16:
            return Script_Play(&l->base, uint32_t(a), int8_t(b));
        case 0x18:
            l->mode |= 2;
            if (a == 0) {
                if (b == 0) {
                    l->string = kSpace;
                    l->label = 0xffffffffu;
                } else {
                    l->string = Txt_BindLabel(uint32_t(b), 0);
                    l->label = uint32_t(b);
                }
            } else {
                l->string = ptr<char>(a);
                l->label = 0xffffffffu;
            }
            refresh_label(l);
            return 1;
        case 0x1a:
            l->text.colour = uint32_t(a);
            if (b == 0) {
                const uint32_t lo = uint32_t(a) & 0xff;
                l->mode &= 0xfffffffeu;
                l->text.colour2 = lo < 0x81 ? lo : 0x80;
                l->colour = uint32_t(a);
                l->colour_off = uint32_t(a);
            } else {
                l->mode |= 1;
            }
            return 1;
        case 0x1b:
            l->mode |= 1;
            l->colour = uint32_t(a);
            l->text.colour = uint32_t(a);
            return 1;
        case 0x1c:
            l->mode |= 1;
            l->colour_off = uint32_t(a);
            l->text.colour = uint32_t(a);
            return 1;
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<M_SCRIPT**>(l->base.defaults)[a] = Script_Select(&l->base, uint32_t(b));
            return 1;
        case 0x23:
            // Só no port: limitado a format_buf.
            std::snprintf(l->format_buf, sizeof(l->format_buf), "%s%s", ptr<char>(a), kScale100);
            l->text.format = l->format_buf;
            return 1;
        case 0x24:
            if (a != 0) {
                l->mode &= 0xfffffffdu;
                l->image = uint32_t(a);
                refresh_label(l);
            }
            return 1;
        case 0x29:
            l->colour_throb = uint32_t(a);
            return 1;
        case 0x2b:
            l->base.state = uint8_t(a);
            if ((l->base.state & 1) != 0) {
                l->text.priority = -1;
                if (l->spr != nullptr) l->spr->priority = -1;
            }
            refresh_label(l);
            return 1;
        case 0x2d:
            l->text.u = int16_t(uint32_t(a) >> 16);
            l->text.v = int16_t(a);
            l->text.uw = int16_t(uint32_t(b) >> 16);
            l->text.vh = int16_t(b);
            return 1;
        case 0x2f:
            return val(l->string);
        case 0x30:
            return int32_t(l->text.colour);
        case 0x31:
            return int32_t(l->colour);
        case 0x32:
            return int32_t(l->colour_off);
        case 0x39:
            return val(l);
        case 0x3d:
            return int32_t(l->base.tag);
        case 0x3e:
            return int32_t(uint32_t(uint16_t(l->text.uw)) << 16 | uint16_t(l->text.vh));
        case 0x3f:
            return int32_t(uint32_t(uint16_t(l->text.u)) << 16 | uint16_t(l->text.v));
        case 0x47: {
            Menu_DeleteSprite(l->spr, 2);
            Script_DelAllScripts(&l->base);
            if (l->base.owner->type == MCT_PAGE) {
                void* p = l;
                Menu_Free(&p, 3);
            }
            return 0;
        }
        case 0x58:
            l->base.flags |= 4;
            l->text.colour2 = uint32_t(a);
            return 1;
        case 0x62:
            l->outline = uint8_t(a);
            return 1;
        case 0x6c:
            l->sprite_flags = uint32_t(a);
            return 1;
        case 0x70:
            l->throb = uint8_t(a);
            return 0;
        case 0x76:
            l->keep_focus = uint8_t(a);
            return 0;
        default:
            return 0;
    }
}

// [decomp] 0x00218cb8 Label_Update
// Um tick do rótulo: escondido (ele ou o dono), apaga o sprite. Senão a cor (com cores próprias: a de
// foco, a sem foco desbotando em 15 ticks com o processo 0xe1, ou o pulso), a posição (texto: pelo
// alinhamento do formato e centrado na vertical; imagem: o retângulo do controle), o texto do rótulo
// e o corte na largura; os eventos de escolha quando o cursor está nele; e copia o modelo para o
// sprite (mantendo o nó dele).
void Label_Update(M_MANAGER* m, M_PAGE* page, M_LABEL* l, int8_t over) {
    int32_t which = 0;
    if ((l->base.owner->state & 5) != 0 || (l->base.state & 1) != 0) {
        l->base.state |= 4;
        if (l->spr == nullptr) return;
        l->spr->priority = -1;
        Menu_DeleteSprite(l->spr, 2);
        l->spr = nullptr;
        return;
    }
    l->base.state &= 0xfb;
    if (Menu_CursorOverMe(m, &l->base) != 0) which = pressed_button(m);
    const uint32_t lo = l->text.colour & 0xff;
    l->text.priority = int8_t(uint8_t(l->base.depth) - uint8_t(page->base.depth));
    l->text.colour2 = (l->text.colour2 & 0xffffff00u) | (lo < 0x81 ? lo : 0x80);
    if ((l->mode & 1) != 0) {
        bool focused = Menu_CursorOverMe(m, &l->base) != 0 || over != 0;
        if (!focused) {
            M_CONTROL* f = page->focus;
            if (l->base.id == f->id && (l->base.tag == f->tag || l->keep_focus != 0)) focused = true;
        }
        if (focused) {
            Menu_GetNormalTextFmt(&l->text);
            l->faded = 0;
            if (l->throb != 0 && l->base.state == 0) {
                l->text.colour = pulse(l->colour_throb);
            } else {
                l->text.colour = l->colour;
            }
        } else if (l->faded == 0) {
            if (l->base.focus_ticks < 6) {
                l->text.colour = l->colour_off;
            } else {
                M_MESSAGE msg{};
                msg.a = int32_t(l->colour_off);
                msg.b = 1;
                msg.message = 0xe1;
                Process_Create(&l->base, &msg, 0xf, 1);
            }
            Menu_GetNormalTextFmt(&l->text);
            l->faded = 1;
        }
    }
    sprite* s = &l->text;
    GPOINT extent{1.0f, 1.0f};
    Font_GetTextExtent(kAlphabet, l->text.format, &extent, nullptr, -1);
    const float line_h = extent.y;
    if ((l->mode & 2) == 0) {
        const int16_t uw = l->text.uw;
        const int16_t vh = l->text.vh;
        hashtable_set_sprite(s, l->image);
        l->text.flags = 0;
        l->text.uw = uw;
        l->text.vh = vh;
        l->text.text = nullptr;
        l->text.format = nullptr;
        l->text.x = int16_t(l->base.x + page->base.x);
        l->text.w = l->base.w;
        l->text.y = int16_t(l->base.y + page->base.y);
        l->text.h = l->base.h;
        l->text.flags = uint16_t(l->sprite_flags);
    } else {
        const uint8_t align = Font_GetAlignment(l->text.format);
        if (align == 3) {
            l->text.x = int16_t(page->base.x + l->base.x + (l->base.w >> 1));
        } else if (Font_GetAlignment(l->text.format) == 2) {
            l->text.x = int16_t(page->base.x + l->base.x + l->base.w);
        } else {
            l->text.x = int16_t(l->base.x + page->base.x);
        }
        int32_t one_line = 1;
        if (std::strlen(l->string) != 0 && std::strstr(l->string, "\n") != nullptr) one_line = 0;
        l->text.created = 0;
        l->text.tex_w = uint16_t(l->text.x);
        const int16_t y = int16_t(int32_t(float(l->base.y + (l->base.h >> 1)) + line_h * float(one_line) * 0.5f +
                                          float(page->base.y) - 1.0f));
        l->text.tex_h = uint16_t(y);
        l->text.y = y;
        if (l->label != 0xffffffffu) {
            l->string = Txt_BindLabel(l->label, 0);
            if (l->string != nullptr && std::strcmp(l->string, kEmpty) == 0) l->string = kSpace;
        }
        if (l->string != nullptr && std::strlen(l->string) != 0) Sprite_SetText(s, l->string, 0);
        if (l->outline != 0) {
            l->text.flags |= 0x2000;
        } else {
            l->text.flags &= 0xdfff;
        }
        Menu_ClipString(s, l->base.w, l->text.format);
    }
    send_choice(m, &l->base, which);
    if (l->spr == nullptr) {
        l->spr = Menu_CreateSprite(nullptr, 2);
        if (l->spr == nullptr) return;
    }
    const LLNODE_tag node = l->spr->node;
    amemcpy(l->spr, s, 0x44);
    l->spr->node = node;
}

// ---- botão ----

// [decomp] 0x001efc90 Button_Init
// O botão: o componente 0 do conjunto e um rótulo centrado (formato próprio) um nível acima.
void Button_Init(M_BUTTON* b, NEW_CONTROL* desc, M_CONTROL* owner) {
    b->base.type = MCT_BUTTON;
    b->base.state = 0;
    b->base.owner = owner;
    b->base.unknown_10 = 0;
    b->base.component = uint32_t(desc->owner) << 16;
    b->base.id = desc->id;
    b->base.tag = desc->unknown_0c;
    b->base.sprites = nullptr;
    b->base.depth = desc->depth;
    b->unknown_1aa = 0;
    b->base.flags = 0;
    b->unknown_1a8 = int16_t(reinterpret_cast<uintptr_t>(desc->format));
    NEW_CONTROL label;
    amemcpy(&label, desc, sizeof(NEW_CONTROL));
    label.depth = uint16_t(label.depth - 1);
    label.format = kButtonFormat;
    label.unknown_10 = 0;
    Label_Init(&b->label, &label, &b->base);
    b->base.x = desc->x;
    b->base.y = desc->y;
    b->base.w = desc->w;
    b->base.h = desc->h;
    Script_PrepareScripting(&b->base);
}

// [decomp] 0x001ef7a0 Button_SendMessage
int32_t Button_SendMessage(M_BUTTON* b, uint32_t message, int32_t a, int32_t bb) {
    switch (message) {
        case 0x14:
            return Script_ExitLoop(&b->base);
        case 0x16:
            return Script_Play(&b->base, uint32_t(a), int8_t(bb));
        case 0x20:
            b->base.tag = uint32_t(a);
            return 1;
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<M_SCRIPT**>(b->base.defaults)[a] = Script_Select(&b->base, uint32_t(bb));
            return 1;
        case 0x2b:
            b->base.state = uint8_t(a);
            if ((b->base.state & 1) != 0) Component_SetupInstance(&b->base, 0, -1, -1, -1, -1);
            Label_SendMessage(&b->label, 0x2b, a, bb);
            return 1;
        case 0x2f:
            // O texto do sprite do rótulo (o original lê sem conferir se o sprite existe).
            return b->label.spr != nullptr ? val(b->label.spr->text) : 0;
        case 0x39:
            return val(b);
        case 0x3d:
            return int32_t(b->base.tag);
        case 0x47: {
            Label_SendMessage(&b->label, 0x47, 0, 0);
            Component_Free(&b->base);
            Script_DelAllScripts(&b->base);
            void* p = b;
            Menu_Free(&p, 3);
            return 0;
        }
        default:
            return Label_SendMessage(&b->label, message, a, bb);
    }
}

// [decomp] 0x001ef948 Button_Update
// Um tick do botão: o rótulo dentro das margens do componente; o estado do componente (0x10 normal,
// 0x20 com o foco, 0x40 apertado, 0x80 com o foco de outro botão de mesmo id); os eventos de escolha
// (1 apertado, 2 segurado: 0x61, 3/4 os outros botões).
void Button_Update(M_MANAGER* m, M_PAGE* page, M_BUTTON* b) {
    int32_t which = 0;
    if ((b->base.owner->state & 5) != 0 || (b->base.state & 1) != 0) {
        b->base.state |= 4;
        Component_Free(&b->base);
        Label_Update(m, page, &b->label, int8_t(Menu_CursorOverMe(m, &b->base)));
        return;
    }
    b->base.state &= 0xfb;
    const M_COMPONENT* k = Component_GetHandle(&b->base);
    Menu_ResizeControl(&b->label.base, int16_t(b->base.x + k->left), int16_t(b->base.y + k->top),
                       int16_t(b->base.w - k->left - k->right), int16_t(b->base.h - k->top - k->bottom));
    uint32_t flags = 0x10;
    bool check_input = false;
    if (page->focus->id == b->base.id) {
        b->base.flags = 0x20;
        if (Menu_CursorOverMe(m, &b->base) != 0) {
            check_input = true;
        } else if (b->base.id == page->focus->id && b->base.tag == page->focus->tag) {
            check_input = true;
        }
        if (check_input) {
            which = Menu_InputAction(m->player, 0x1f, 4) != 0 ? 1 : 0;
            if (Menu_InputAction(m->player, 0x1f, 8) != 0) which = 2;
            if (Menu_InputAction(m->player, 0x20, 4) != 0) which = 3;
            const bool other = Menu_InputAction(m->player, 0x21, 4) != 0;
            b->base.flags = 0x80;
            if (other) which = 4;
            if (Menu_InputAction(m->player, 0x1f, 1) != 0) b->base.flags = 0x40;
        }
    } else {
        b->base.flags = flags;
    }
    Component_SetupInstance(&b->base, b->base.flags, -1, -1, -1, -1);
    Label_Update(m, page, &b->label, int8_t(Menu_CursorOverMe(m, &b->base)));
    if (m->input == 0 || m->frozen != 0) return;
    if (which == 1) {
        Menu_PlaySound(0);
        __Menu_SendMessage(b->base.owner, 0x4b, val(b), 0);
    }
    if (which == 2) {
        Menu_PlaySound(0);
        __Menu_SendMessage(b->base.owner, 0x61, val(b), 0);
    } else if (which == 3) {
        Menu_PlaySound(1);
        __Menu_SendMessage(b->base.owner, 0x5d, val(b), 0);
    } else if (which == 4) {
        Menu_PlaySound(2);
        __Menu_SendMessage(b->base.owner, 0x5e, val(b), 0);
    }
}

// ---- caixa de marcar ----

// [decomp] 0x001f0110 Checkbox_Init
void Checkbox_Init(M_CHECKBOX* c, NEW_CONTROL* desc, M_CONTROL* owner) {
    c->base.type = MCT_CHECKBOX;
    c->base.state = 0;
    c->base.owner = owner;
    c->base.unknown_10 = 0;
    c->base.component = uint32_t(desc->owner) << 16 | 1;
    c->base.id = desc->id;
    c->base.sprites = nullptr;
    c->base.tag = desc->unknown_0c;
    c->base.depth = desc->depth;
    c->base.flags = 0;
    c->checked = uint8_t(reinterpret_cast<uintptr_t>(desc->format));
    float width = 0.0f;
    char* text = reinterpret_cast<char*>(uintptr_t(desc->unknown_08));
    if (text != nullptr) {
        GPOINT e{};
        Font_GetTextExtent(text, kCheckFormat, &e, nullptr, -1);
        width = e.x;
    }
    NEW_CONTROL label;
    amemcpy(&label, desc, sizeof(NEW_CONTROL));
    const M_COMPONENT* k = Component_GetHandle(&c->base);
    label.format = kCheckFormat;
    label.w = int16_t(int32_t(width) + 1);
    label.x = int16_t(label.x + k->left);
    label.unknown_10 = 0;
    Label_Init(&c->label, &label, &c->base);
    c->base.x = desc->x;
    c->base.y = desc->y;
    c->base.w = desc->w;
    c->base.h = desc->h;
    Script_PrepareScripting(&c->base);
}

// [decomp] 0x001efd68 Checkbox_SendMessage
// 0x2e marca, 0x40 consulta; o resto vai ao rótulo.
int32_t Checkbox_SendMessage(M_CHECKBOX* c, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x14:
            return Script_ExitLoop(&c->base);
        case 0x16:
            return Script_Play(&c->base, uint32_t(a), int8_t(b));
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<M_SCRIPT**>(c->base.defaults)[a] = Script_Select(&c->base, uint32_t(b));
            return 1;
        case 0x2b:
            c->base.state = uint8_t(a);
            if ((c->base.state & 1) != 0) Component_SetupInstance(&c->base, 0, -1, -1, -1, -1);
            Label_SendMessage(&c->label, 0x2b, a, b);
            return 1;
        case 0x2e:
            c->checked = uint8_t(a);
            return 1;
        case 0x39:
            return val(c);
        case 0x3d:
            return int32_t(c->base.tag);
        case 0x40:
            return int32_t(int8_t(c->checked));
        case 0x47: {
            Label_SendMessage(&c->label, 0x47, 0, 0);
            Component_Free(&c->base);
            Script_DelAllScripts(&c->base);
            void* p = c;
            Menu_Free(&p, 3);
            return 0;
        }
        default:
            return Label_SendMessage(&c->label, message, a, b);
    }
}

// [decomp] 0x001efef8 Checkbox_Update
// Com o foco e entrada, a ação de escolher troca a marca e manda 0x4b; o componente mostra 0x40
// (marcada) ou 0x80 (vazia) somado ao estado de foco.
void Checkbox_Update(M_MANAGER* m, M_PAGE* page, M_CHECKBOX* c) {
    if ((c->base.owner->state & 1) != 0 || (c->base.state & 1) != 0) {
        c->base.state |= 4;
        Component_Free(&c->base);
        Label_Update(m, page, &c->label, int8_t(Menu_CursorOverMe(m, &c->base)));
        return;
    }
    bool toggled = false;
    c->base.state &= 0xfb;
    const M_COMPONENT* k = Component_GetHandle(&c->base);
    Menu_ResizeControl(&c->label.base, int16_t(c->base.x + k->left), c->base.y, int16_t(c->base.w - k->left),
                       c->base.h);
    uint32_t flags = 0x10;
    if (page->focus->id == c->base.id && c->base.state == 0 && m->input != 0) {
        if (Menu_InputAction(m->player, 0x1f, 4) != 0) {
            toggled = true;
            c->checked = c->checked == 0 ? 1 : 0;
        }
        flags = 0x20;
    }
    c->base.flags = flags;
    c->base.flags |= c->checked == 0 ? 0x80 : 0x40;
    Label_Update(m, page, &c->label, int8_t(Menu_CursorOverMe(m, &c->base)));
    Component_SetupInstance(&c->base, c->base.flags, -1, -1, -1, -1);
    if (toggled && m->input != 0 && m->frozen == 0) {
        Menu_PlaySound(0);
        __Menu_SendMessage(c->base.owner, 0x4b, val(c), 0);
    }
}

// ---- barra de rolagem ----

// [decomp] 0x002214f0 Scroll_Init
// A barra: valores de NEW_CONTROL +4 (mínimo) a +8 (máximo); horizontal (componente 6) ou vertical (7,
// mais alta que larga); o cursor (componente 8) com o tamanho de um passo. Com +0x10 = 0xff (de
// Scroll_Update, a cada tick) só recalcula a geometria.
void Scroll_Init(M_SCROLL* s, NEW_CONTROL* desc, M_CONTROL* owner) {
    s->base.type = MCT_SCROLL;
    s->base.owner = owner;
    s->base.state = 0;
    s->base.unknown_10 = 0;
    s->base.id = desc->id;
    s->mode = 0;
    s->base.depth = desc->depth;
    const int32_t lo = int32_t(reinterpret_cast<uintptr_t>(desc->format));
    const int32_t hi = int32_t(desc->unknown_08);
    s->range = hi - lo + 1;
    s->min = lo;
    s->base.flags = 0;
    s->max = hi;
    s->range = hi - lo + 1;
    const bool full = desc->unknown_10 != 0xff;
    if (full) std::memset(&s->thumb, 0, sizeof(M_CONTROL));
    s->thumb.type = MCT_SUB;
    s->thumb.owner = &s->base;
    s->thumb.depth = uint16_t(desc->depth - 1);
    s->thumb.component = uint32_t(desc->owner) << 16 | 8;
    if (reinterpret_cast<uint8_t*>(desc)[0x20] == 0) s->mode |= 1;
    if (desc->w < desc->h) s->mode |= 2;
    int32_t range = s->range;
    if (range == 0) {
        ++g_menu_guards.scroll_zero_range;  // o original para na divisão por zero
        range = 1;
    }
    int16_t thumb_extent;
    if ((s->mode & 2) == 0) {
        s->base.component = uint32_t(desc->owner) << 16 | 6;
        s->margin_a = int16_t(desc->h + Component_GetHandle(&s->base)->left);
        s->margin_b = int16_t(desc->h + Component_GetHandle(&s->base)->right);
        s->thumb.x = int16_t(desc->x + s->margin_a);
        s->thumb.y = int16_t(desc->y + Component_GetHandle(&s->thumb)->top);
        const int32_t step = (int32_t(desc->w) - s->margin_a - s->margin_b) / range;
        s->thumb.w = desc->h < step ? int16_t(step) : desc->h;
        const M_COMPONENT* t = Component_GetHandle(&s->thumb);
        thumb_extent = int16_t(desc->h - t->top - Component_GetHandle(&s->thumb)->bottom);
    } else {
        s->base.component = uint32_t(desc->owner) << 16 | 7;
        s->margin_a = int16_t(desc->w + Component_GetHandle(&s->base)->top);
        s->margin_b = int16_t(desc->w + Component_GetHandle(&s->base)->bottom);
        s->thumb.x = int16_t(desc->x + Component_GetHandle(&s->thumb)->left);
        s->thumb.y = int16_t(desc->y + s->margin_a);
        const M_COMPONENT* t = Component_GetHandle(&s->thumb);
        s->thumb.w = int16_t(desc->w - t->left - Component_GetHandle(&s->thumb)->right);
        const int32_t step = (int32_t(desc->h) - s->margin_a - s->margin_b) / range;
        thumb_extent = desc->w < step ? int16_t(step) : desc->w;
    }
    s->thumb.h = thumb_extent;
    s->base.x = desc->x;
    s->base.y = desc->y;
    s->base.w = desc->w;
    s->base.h = desc->h;
    if (full) {
        s->wrap = 1;
        s->pos = s->min;
        s->pressed = 0;
        s->proportional = 0;
        s->base.tag = desc->unknown_0c;
        s->base.sprites = nullptr;
        s->thumb.sprites = nullptr;
        Script_PrepareScripting(&s->base);
    }
}

// [decomp] 0x00221810 Scroll_SendMessage
// 0x12 soma ao máximo, 0x15 sem cursor, 0x25/0x26 máximo/mínimo, 0x27 a faixa, 0x2e a posição (avisa o
// dono com 0x54), 0x69 cursor proporcional; consultas 0x38 (máximo), 0x3a (mínimo), 0x40 (posição na
// faixa).
int32_t Scroll_SendMessage(M_SCROLL* s, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x12:
            s->max += a;
            return 0;
        case 0x14:
            return Script_ExitLoop(&s->base);
        case 0x15:
            s->mode |= 1;
            return 1;
        case 0x16:
            return Script_Play(&s->base, uint32_t(a), int8_t(b));
        case 0x20:
            s->base.tag = uint32_t(a);
            return 1;
        case 0x21:
            if (6 < uint32_t(a)) return 0;
            reinterpret_cast<M_SCRIPT**>(s->base.defaults)[a] = Script_Select(&s->base, uint32_t(b));
            return 1;
        case 0x25:
            if (a < s->min) return 0;
            s->max = a;
            if (s->pos <= a) return 1;
            s->pos = a;
            return 1;
        case 0x26:
            if (s->max < a) return 0;
            s->min = a;
            if (a <= s->pos) return 1;
            s->pos = a;
            return 1;
        case 0x27:
            if (b < a) return 0;
            s->min = a;
            s->max = b;
            s->range = b - a + 1;
            if (s->pos < a) s->pos = a;
            if (s->pos <= b) return 1;
            s->pos = b;
            return 1;
        case 0x2b:
            s->base.state = uint8_t(a);
            if ((s->base.state & 1) != 0) {
                Component_SetupInstance(&s->base, 0, -1, -1, -1, -1);
                Component_SetupInstance(&s->thumb, 0, -1, -1, -1, -1);
            }
            return 1;
        case 0x2e:
            if (s->max < a || a < s->min) return 0;
            s->pos = a;
            __Menu_SendMessage(s->base.owner, 0x54, val(s), 0);
            return 1;
        case 0x38:
            return s->max;
        case 0x39:
            return val(s);
        case 0x3a:
            return s->min;
        case 0x3d:
            return int32_t(s->base.tag);
        case 0x40: {
            if (s->pos < s->min) return s->min;
            if (s->max < s->pos) return s->max;
            return s->pos;
        }
        case 0x47: {
            Component_Free(&s->base);
            Component_Free(&s->thumb);
            Script_DelAllScripts(&s->base);
            if (s->base.owner->type == MCT_PAGE) {
                void* p = s;
                Menu_Free(&p, 3);
            }
            return 0;
        }
        case 0x69:
            s->proportional = 1;
            s->wrap = 0;
            return 1;
        default:
            return 0;
    }
}

// [decomp] 0x00221b20 Scroll_Update
// Um tick da barra: recalcula a geometria (Scroll_Init parcial); com o foco e entrada, as direções da
// barra (esquerda/direita ou cima/baixo, não as duas) mudam a posição (um passo ao apertar, depois de
// 16 ticks um a cada 5, ou 2 depois de 506), dando a volta nas pontas se `wrap`, com o som e o evento
// 0x49 ao dono; o componente e o cursor na posição; e os eventos de escolha (0x22: 0x6d).
void Scroll_Update(M_MANAGER* m, M_PAGE* page, M_SCROLL* s) {
    alignas(4) uint8_t raw[0x24]{};
    NEW_CONTROL& nc = *reinterpret_cast<NEW_CONTROL*>(raw);
    nc.id = s->base.id;
    nc.format = reinterpret_cast<char*>(uintptr_t(uint32_t(s->min)));
    nc.unknown_08 = uint32_t(s->max);
    nc.unknown_10 = 0xff;
    nc.owner = uint16_t(s->base.component >> 16);
    nc.depth = s->base.depth;
    nc.x = s->base.x;
    nc.y = s->base.y;
    nc.w = s->base.w;
    nc.h = s->base.h;
    raw[0x20] = uint8_t(((s->mode & 0xff) ^ 1) & 1);
    const int32_t saved = s->pos;
    Scroll_Init(s, &nc, s->base.owner);
    s->pos = saved;
    const bool up = int8_t(Menu_InputAction(m->player, 0x1a, 1)) != 0;
    const bool down = int8_t(Menu_InputAction(m->player, 0x1b, 1)) != 0;
    const bool left = int8_t(Menu_InputAction(m->player, 0x1c, 1)) != 0;
    const bool right = int8_t(Menu_InputAction(m->player, 0x1d, 1)) != 0;
    const bool vert = up || down;
    const bool horiz = left || right;
    const uint32_t held = s->held;
    const int32_t speed = 0x1f9 < held ? 2 : 5;
    const bool focused = page->focus != nullptr && page->focus->id == s->base.id;
    if (!focused || (m->input != 0 && m->frozen != 0)) {
        if (m->input != 0) {
            if (((s->base.state ^ 1) & 1) != 0) s->base.flags = 0x10;
            s->pressed = 0;
            s->held = 0;
        }
    } else if (m->input != 0) {
        // -1 menos, +1 mais, 0 soltou.
        int32_t dir = 0;
        if ((s->mode & 2) == 0) {
            if (left && !vert) {
                dir = -1;
            } else if (right && !vert) {
                dir = 1;
            }
        } else {
            if (up && !horiz) {
                dir = -1;
            } else if (down && !horiz) {
                dir = 1;
            }
        }
        int32_t next = 0;
        uint32_t sound = 0;
        bool move = false;
        if (dir == 0) {
            if (s->pressed != 0) s->pressed = 0;
        } else if (dir < 0) {
            if (s->pressed == 0) {
                s->pressed = 0x40;
                s->held = 0;
                sound = 3;
                if (s->min < s->pos) {
                    next = s->pos - 1;
                    move = true;
                } else if (s->wrap != 0) {
                    next = s->max;
                    move = true;
                }
            } else if (s->pressed != 0x40 || held < 0x10) {
                s->held = held + 1;
            } else {
                s->held = held + 1;
                if (int32_t(held + 1) % speed == 0) {
                    sound = 5;
                    next = s->pos - 1;
                    if (s->min < s->pos) {
                        move = true;
                    } else if (s->wrap != 0) {
                        next = s->max;
                        move = true;
                    }
                }
            }
        } else {
            if (s->pressed == 0) {
                s->pressed = 0x80;
                s->held = 0;
                sound = 4;
                if (s->pos < s->max) {
                    next = s->pos + 1;
                    move = true;
                } else if (s->wrap != 0) {
                    next = s->min;
                    move = true;
                }
            } else if (s->pressed == 0x80 && 0xf < held) {
                s->held = held + 1;
                if (int32_t(held + 1) % speed == 0) {
                    next = s->pos + 1;
                    move = true;
                    if (s->max <= s->pos) {
                        if (s->wrap == 0) {
                            move = false;
                        } else {
                            next = s->min;
                        }
                    }
                    sound = 6;
                }
            } else {
                s->held = held + 1;
            }
        }
        if (move) {
            s->pos = next;
            Menu_PlaySound(sound);
            __Menu_SendMessage(s->base.owner, 0x49, val(s), 0);
        }
        if (((s->base.state ^ 1) & 1) == 0) {
            s->base.flags = s->pressed;
        } else {
            s->base.flags = s->pressed == 0 ? 0x20 : s->pressed;
        }
    }
    if ((s->base.owner->state & 5) != 0 || (s->base.state & 1) != 0) {
        s->base.state |= 4;
        Component_Free(&s->base);
        Component_Free(&s->thumb);
        return;
    }
    const uint16_t mode = s->mode;
    Component_SetupInstance(&s->base, s->base.flags, -1, -1, -1, -1);
    if (((s->mode ^ 1) & 1) != 0) {
        const int32_t span = s->max - s->min;
        int32_t d = 1;
        if ((mode & 2) == 0) {
            if (0 < span) d = span;
            const int32_t x0 = s->base.x;
            const int32_t lim = x0 + s->base.h;
            const int32_t tx = int16_t(int32_t(
                float(x0 + s->margin_a) +
                (float(s->base.w - s->margin_a - s->margin_b - s->thumb.w) / float(d)) * float(s->pos - s->min)));
            int32_t v;
            if (tx < lim) {
                v = int32_t(uint16_t(s->base.x) + uint16_t(s->base.h));
            } else {
                v = x0 + s->base.w - s->base.h - s->thumb.w;
                if (v <= lim) v = lim;
                if (tx <= v) v = tx;
            }
            s->thumb.x = int16_t(v);
            if (s->proportional != 0) {
                const M_COMPONENT* t = Component_GetHandle(&s->thumb);
                const int32_t x = s->base.x + s->margin_a + t->left;
                const int32_t y = s->thumb.y + Component_GetHandle(&s->thumb)->top;
                const int32_t w = int16_t(int32_t(float(s->base.w - s->margin_a - s->margin_b) *
                                                  (float(s->pos - s->min) / float(s->max - s->min))));
                const int32_t h = s->thumb.h - Component_GetHandle(&s->thumb)->top - Component_GetHandle(&s->thumb)->bottom;
                Component_SetupInstance(&s->thumb, s->base.flags, x, y, w, h);
            } else {
                int32_t tw = s->thumb.w;
                const int32_t avail = s->base.w - s->margin_b - s->margin_a;
                if (avail <= tw) tw = int16_t(avail < 0 ? 0 : avail);
                const M_COMPONENT* t = Component_GetHandle(&s->thumb);
                Component_SetupInstance(&s->thumb, s->base.flags, s->thumb.x + t->left, s->thumb.y + t->top,
                                        tw - t->left - t->right, s->thumb.h - t->top - t->bottom);
            }
        } else {
            if ((s->base.state & 1) < span) d = span;
            const int32_t y0 = s->base.y;
            const int32_t lim = y0 + s->base.w;
            int32_t ty = int16_t(int32_t(
                float(y0 + s->margin_a) +
                (float(s->base.h - s->margin_a - s->margin_b - s->thumb.h) / float(d)) * float(s->pos - s->min)));
            if (ty < lim) {
                ty = int32_t(uint16_t(s->base.y) + uint16_t(s->base.w));
            } else {
                int32_t v = y0 + s->base.h - s->base.w - s->thumb.h;
                if (v <= lim) v = lim;
                if (v < ty) ty = v;
            }
            s->thumb.y = int16_t(ty);
            int32_t th = s->thumb.h;
            const int32_t avail = s->base.h - s->margin_b - s->margin_a;
            if (avail <= th) th = int16_t(avail < 0 ? 0 : avail);
            const M_COMPONENT* t = Component_GetHandle(&s->thumb);
            Component_SetupInstance(&s->thumb, s->base.flags, s->thumb.x + t->left, s->thumb.y + t->top,
                                    s->thumb.w - t->left - t->right, th - t->top - t->bottom);
        }
    }
    s->base.state &= 0xfb;
    if (page->focus == nullptr || page->focus->id != s->base.id || m->input == 0 || m->frozen != 0) return;
    if (Menu_InputAction(m->player, 0x1f, 4) != 0) {
        Menu_PlaySound(0);
        __Menu_SendMessage(s->base.owner, 0x4b, val(s), 0);
    } else if (Menu_InputAction(m->player, 0x20, 4) != 0) {
        Menu_PlaySound(1);
        __Menu_SendMessage(s->base.owner, 0x5d, val(s), 0);
    } else if (Menu_InputAction(m->player, 0x21, 4) != 0) {
        Menu_PlaySound(2);
        __Menu_SendMessage(s->base.owner, 0x5e, val(s), 0);
    } else if (Menu_InputAction(m->player, 0x22, 4) != 0) {
        Menu_PlaySound(9);
        __Menu_SendMessage(s->base.owner, 0x6d, val(s), 0);
    }
}

// ---- lista ----

// [decomp] 0x0021b420 List_GetIndex
int32_t List_GetIndex(M_LISTBOX* l, LINKEDLIST*, LIST_ROW* row) {
    int32_t i = 0;
    for (LIST_ROW* r = first_row(l->rows); r != nullptr; r = r->next, ++i) {
        if (r == row) return i;
    }
    return -1;
}

// [decomp] 0x0021b470 List_GetIndex_RealItem
// O índice da linha sem contar as continuações (linhas quebradas) depois da primeira.
int32_t List_GetIndex_RealItem(M_LISTBOX* l, LINKEDLIST*, LIST_ROW* row) {
    int32_t i = 0;
    LIST_ROW* r = first_row(l->rows);
    for (;;) {
        if (r == nullptr) return -1;
        if (r == row) return i;
        r = r->next;
        if (r != nullptr && r->continuation == 0) ++i;
        if (r == nullptr) return -1;
    }
}

// [decomp] 0x0021b4c8 List_SetIndex
LIST_ROW* List_SetIndex(M_LISTBOX* l, LINKEDLIST*, int32_t index) {
    LIST_ROW* r = first_row(l->rows);
    for (int32_t i = 0; i < index; ++i) {
        if (r == nullptr) return nullptr;
        r = r->next;
    }
    return r;
}

// [decomp] 0x0021b520 List_SetIndex_RealItem
LIST_ROW* List_SetIndex_RealItem(M_LISTBOX* l, LINKEDLIST*, int32_t index) {
    int32_t i = -1;
    LIST_ROW* r = first_row(l->rows);
    if (r == nullptr) return nullptr;  // só no port: o original leria a linha nula
    for (;;) {
        if (r->continuation == 0) ++i;
        if (i == index) return r;
        r = r->next;
        if (r == nullptr) return nullptr;
    }
}

// [decomp] 0x00219488 List_Init
// A lista: linhas de até 5 colunas (largura em % e alinhamento), uma barra vertical na direita
// (componente da barra do conjunto) e o destaque da linha escolhida (componente 4).
void List_Init(M_LISTBOX* l, NEW_CONTROL* desc, M_CONTROL* owner) {
    l->format = kListFormat1;
    std::strcpy(l->format_sel, kListFormat1);
    GPOINT e{};
    Font_GetTextExtent(kAlphabet, l->format, &e, nullptr, -1);
    l->base.state = 0;
    l->base.type = MCT_LIST;
    l->base.owner = owner;
    l->base.unknown_10 = 0;
    l->top = 0;
    l->base.component = uint32_t(desc->owner) << 16 | 3;
    l->selected = nullptr;
    l->base.id = desc->id;
    l->base.sprites = nullptr;
    l->base.tag = desc->unknown_0c;
    l->base.depth = desc->depth;
    l->row_height = e.y * 1.1f;
    l->highlight.depth = uint16_t(desc->depth - 1);
    l->columns = 1;
    l->visible = int32_t(float(desc->h) / (e.y * 1.1f));
    l->throb = 0;
    l->stripe = 0;
    l->wrap = 0;
    l->wrapped = 0;
    l->colour = 0xff;
    l->colour2 = 0x80;
    l->colour_off = 0x404040ffu;
    l->highlight_mode = uint8_t(reinterpret_cast<uintptr_t>(desc->format));
    l->colour_sel = 0x7f7f7fffu;
    LinkedList_Init(&l->rows, 0);
    std::memset(l->cells, 0, sizeof(l->cells));
    std::memset(l->widths, 0, sizeof(l->widths));
    l->align[0] = 1;
    l->widths[0] = 100;
    NEW_CONTROL bar;
    amemcpy(&bar, desc, sizeof(NEW_CONTROL));
    const int16_t w = bar.w;
    bar.h = l->base.h;  // como no original: a altura da lista ainda não foi escrita (vem do heap)
    bar.depth = uint16_t(desc->depth - 1);
    bar.w = 0x10;
    bar.x = int16_t(w + bar.x - 0x10);
    reinterpret_cast<uint8_t*>(&bar)[0x20] = 1;
    reinterpret_cast<uint8_t*>(&bar)[0x21] = 0;
    bar.format = nullptr;
    bar.unknown_08 = 0;
    bar.unknown_10 = 0;
    Scroll_Init(&l->scroll, &bar, &l->base);
    std::memset(&l->highlight, 0, sizeof(M_CONTROL));
    l->highlight.owner = &l->base;
    l->highlight.type = MCT_SUB;
    l->highlight.sprites = nullptr;
    l->highlight.component = uint32_t(desc->owner) << 16 | 4;
    Component_SetupInstance(&l->highlight, 0, -1, -1, -1, -1);
    l->base.x = desc->x;
    l->base.y = desc->y;
    l->base.w = desc->w;
    l->base.h = desc->h;
    Script_PrepareScripting(&l->base);
}

namespace {

// Uma linha nova no fim (List_SendMessage 0x10/0x11).
LIST_ROW* add_row(M_LISTBOX* l, int32_t data) {
    LIST_ROW* row = static_cast<LIST_ROW*>(Menu_Malloc(0x2d0, 2));
    for (LIST_CELL& c : row->cells) {
        c.image = 0;
        c.text[0] = 0;
    }
    row->continuation = 0;
    row->data = data;
    row->stripe = nullptr;
    LinkedList_Add(&l->rows, &row->prev, row);
    if (l->selected == nullptr) l->selected = row;
    if (1 < l->rows.count) Scroll_SendMessage(&l->scroll, 0x12, 1, 0);
    return row;
}

void refresh_list(M_LISTBOX* l) {
    refresh_quiet(l, [](M_MANAGER* m, M_PAGE* p, M_LISTBOX* x) { List_Update(m, p, x); }, true);
}

}  // namespace

// [decomp] 0x002196e8 List_SendMessage
// 0xe acrescenta uma coluna (largura, alinhamento), 0xf uma linha copiada, 0x10/0x11 uma linha com o
// texto (ou rótulo) na coluna 0 e o valor b, 0x13 apaga uma linha, 0x17 limpa, 0x19 texto da célula
// (linha << 16 | coluna; com `wrap`, quebra em linhas de continuação), 0x1d/0x1e escolhe por índice/
// valor, 0x23 formato, 0x28/0x59 largura/alinhamento, 0x2c primeira linha visível, 0x75 altura da linha
// em %; consultas 0x31-0x35, 0x3c, 0x41. A barra (0x49/0x52/0x54) move a escolha; os eventos sobem.
int32_t List_SendMessage(M_LISTBOX* l, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0xe:
            if (l->columns >= 5) return 0;
            l->widths[l->columns] = a;
            l->align[l->columns] = b;
            ++l->columns;
            return 1;
        case 0xf: {
            LIST_ROW* row = static_cast<LIST_ROW*>(Menu_Malloc(0x2d0, 2));
            for (LIST_CELL& c : row->cells) {
                c.text[0] = 0;
                c.image = 0;
                c.vh = 0;
                c.u = 0;
                c.v = 0;
                c.uw = 0;
            }
            row->data = 0;
            row->stripe = nullptr;
            row->continuation = 0;  // só no port: o original deixa o lixo do heap
            const LIST_ROW* from = ptr<LIST_ROW>(a);
            for (int32_t i = 0; i < l->columns; i = (i + 1) & 0xff) std::strcpy(row->cells[i].text, from->cells[i].text);
            row->data = b;
            LinkedList_Add(&l->rows, &row->prev, row);
            if (l->selected == nullptr) l->selected = row;
            if (1 < l->rows.count) Scroll_SendMessage(&l->scroll, 0x12, 1, 0);
            refresh_list(l);
            return 1;
        }
        case 0x10:
        case 0x11: {
            if (message == 0x11) a = val(Txt_BindLabel(uint32_t(a), 0));
            add_row(l, b);
            if (a != 0) List_SendMessage(l, 0x19, int32_t((l->rows.count - 1) << 16), a);
            refresh_list(l);
            return int32_t(l->rows.count - 1);
        }
        case 0x13: {
            LIST_ROW* row = List_SetIndex(l, &l->rows, a);
            if (row == nullptr) return 0;
            if (row == l->selected) l->selected = nullptr;
            LinkedList_Remove(&l->rows, &row->prev, row);
            void* p = row;
            Menu_Free(&p, 2);
            Scroll_SendMessage(&l->scroll, 0x2e, 0, 0);
            Scroll_SendMessage(&l->scroll, 0x12, -1, 0);
            return 1;
        }
        case 0x14:
            return Script_ExitLoop(&l->base);
        case 0x16:
            return Script_Play(&l->base, uint32_t(a), int8_t(b));
        case 0x17: {
            for (LIST_ROW* r = first_row(l->rows); r != nullptr; r = r->next) {
                if (r->stripe != nullptr) Menu_DeleteSprite(r->stripe, 3);
                r->stripe = nullptr;
            }
            for (uint32_t i = 0;; i = (i + 1) & 0xffff) {
                uint32_t n = uint16_t(l->rows.count);
                if (uint16_t(l->visible) < n) n = uint16_t(l->visible);
                if (n <= i) break;
                for (int32_t k = 0; k < 5; ++k) {
                    if (i < 32 && l->cells[i][k] != nullptr) Menu_DeleteSprite(l->cells[i][k], 3);
                }
            }
            std::memset(l->cells, 0, sizeof(l->cells));
            if (l->rows.count != 0) {
                for (LIST_ROW* r = first_row(l->rows); r != nullptr;) {
                    LIST_ROW* next = r->next;
                    LinkedList_Remove(&l->rows, &r->prev, r);
                    void* p = r;
                    Menu_Free(&p, 2);
                    r = next;
                }
            }
            std::memset(&l->rows, 0, sizeof(l->rows));
            l->selected = nullptr;
            l->top = 0;
            l->visible = 0;
            l->columns = 1;
            l->widths[0] = 100;
            l->align[0] = 1;
            l->wrapped = 0;
            l->selected_index = 0;
            Scroll_SendMessage(&l->scroll, 0x2e, 0, 0);
            Scroll_SendMessage(&l->scroll, 0x25, 0, 0);
            return 1;
        }
        case 0x19: {
            const uint32_t line = (uint32_t(a) >> 16) & 0xffff;
            LIST_ROW* row = List_SetIndex(l, &l->rows, int32_t(line + l->wrapped));
            if (row == nullptr) return 0;
            const uint32_t col = uint32_t(a) & 0xffff;
            if (l->columns <= int32_t(col)) return 0;
            char* text = ptr<char>(b);
            GPOINT scale{1.0f, 1.0f};
            const int32_t width = int32_t(float(l->widths[col]) * (float(l->base.w) * 0.009f));
            uint32_t fit = Font_GetTextCountExtent(text, l->format, &scale, float(width));
            uint32_t len = uint32_t(std::strlen(text));
            if (std::strstr(text, "~") != nullptr) --len;
            if (fit < len && l->wrap != 0) {
                uint32_t cut = fit;
                if (fit != 0 && text[fit] != ' ') {
                    for (uint32_t k = fit - 1; k != 0; --k) {
                        if (text[k] == ' ') {
                            cut = k;
                            break;
                        }
                    }
                }
                if (len != 0) {
                    amemcpy(row->cells[col].text, text, cut);
                    row->cells[col].text[cut] = 0;
                    char space[] = " ";  // 0x0030d5e8
                    List_SendMessage(l, 0x10, val(space), 0);
                    LIST_ROW* next = List_SetIndex(l, &l->rows, int32_t(line + l->wrapped + 1));
                    next->continuation = 1;
                    List_SendMessage(l, 0x19, int32_t((line + 1) << 16 | col), val(text + cut + 1));
                    ++l->wrapped;
                }
            } else if (len != 0) {
                std::strcpy(row->cells[col].text, text);
            }
            return 1;
        }
        case 0x1b:
            l->colour = uint32_t(a);
            return 1;
        case 0x1c:
            l->colour_off = uint32_t(a);
            return 1;
        case 0x1d: {
            const int32_t index = List_GetIndex(l, &l->rows, l->selected);
            if (Scroll_SendMessage(&l->scroll, 0x2e, index, 0) == 0) return 0;
            l->selected = List_SetIndex_RealItem(l, &l->rows, a);
            l->selected_index = uint8_t(List_GetIndex(l, &l->rows, l->selected));
            LIST_ROW* next = List_SetIndex(l, &l->rows, l->selected_index + 1);
            if (next != nullptr && next->continuation != 0 &&
                l->visible < int32_t(uint32_t(l->selected_index) - (l->top - 2))) {
                List_SendMessage(l, 0x2c, int32_t(uint32_t(l->selected_index) - (l->visible - 2)), 0);
            }
            Scroll_SendMessage(&l->scroll, 0x2e, l->selected_index, 0);
            return __Menu_SendMessage(l->base.owner, 0x49, val(l), 0);
        }
        case 0x1e: {
            LIST_ROW* r = first_row(l->rows);
            if (r == nullptr) return 0;
            while (r->data != a) {
                r = r->next;
                if (r == nullptr) return 0;
            }
            l->selected = r;
            Scroll_SendMessage(&l->scroll, 0x2e, List_GetIndex(l, &l->rows, l->selected), 0);
            if (b == 0) return __Menu_SendMessage(l->base.owner, 0x49, val(l), 0);
            return 1;
        }
        case 0x1f:
            if (l->selected == nullptr) return 0;
            l->selected->data = a;
            return 1;
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<M_SCRIPT**>(l->base.defaults)[a] = Script_Select(&l->base, uint32_t(b));
            return 1;
        case 0x23: {
            if (a == 1) {
                l->format = kListFormat1;
            } else if (a == 3) {
                l->format = kListFormat3;
            } else if (a == 2) {
                l->format = kListFormat2;
            }
            GPOINT e{};
            Font_GetTextExtent(kAlphabet, l->format, &e, nullptr, -1);
            l->row_height = e.y * 1.1f;
            std::strcpy(l->format_sel, l->format);
            return 1;
        }
        case 0x28:
            if (l->columns <= a) return 0;
            l->widths[a] = 100 < b ? 100 : b;
            return 1;
        case 0x29:
            l->colour_sel = uint32_t(a);
            return 1;
        case 0x2a:
            l->highlight_mode = uint16_t(a) & 0xff;
            return 0;
        case 0x2b:
            l->base.state = uint8_t(a);
            Scroll_SendMessage(&l->scroll, 0x2b, a, b);
            if ((l->base.state & 1) != 0) {
                Component_SetupInstance(&l->base, 0, -1, -1, -1, -1);
                Component_SetupInstance(&l->highlight, 0, -1, -1, -1, -1);
            }
            refresh_list(l);
            return 1;
        case 0x2c:
            if (uint32_t(a) <= l->rows.count - uint32_t(l->visible) && -1 < a) l->top = a;
            return 1;
        case 0x31:
            return int32_t(l->colour);
        case 0x32:
            return int32_t(l->colour_off);
        case 0x33:
            return int32_t(l->rows.count);
        case 0x34:
            return List_GetIndex_RealItem(l, &l->rows, l->selected);
        case 0x35:
            return l->selected == nullptr ? 0 : l->selected->data;
        case 0x39:
            return val(l);
        case 0x3c:
            return l->top;
        case 0x3d:
            return int32_t(l->base.tag);
        case 0x41:
            return l->visible;
        case 0x47: {
            Component_Free(&l->base);
            Component_Free(&l->highlight);
            Scroll_SendMessage(&l->scroll, 0x47, 0, b);
            List_SendMessage(l, 0x17, 0, 0);
            Script_DelAllScripts(&l->base);
            if (l->base.owner->type == MCT_PAGE) {
                void* p = l;
                Menu_Free(&p, 3);
            }
            return 0;
        }
        case 0x49:
        case 0x52: {
            int32_t pos = Scroll_SendMessage(&l->scroll, 0x40, 0, 0);
            const int32_t top = Scroll_SendMessage(&l->scroll, 0x38, 0, 0);
            l->selected = nullptr;
            if (l->rows.count != 0) {
                int32_t step = int32_t(l->selected_index) < pos ? 1 : -1;
                for (;;) {
                    LIST_ROW* r = List_SetIndex(l, &l->rows, pos);
                    l->selected = r;
                    if (r == nullptr || r->continuation == 0) break;
                    if (top < pos + step) step = -(step < 0 ? -step : step);
                    pos += step;
                }
            }
            l->selected_index = uint8_t(pos);
            LIST_ROW* next = List_SetIndex(l, &l->rows, l->selected_index + 1);
            if (next != nullptr && next->continuation != 0 &&
                l->visible < int32_t(uint32_t(l->selected_index) - (l->top - 2))) {
                List_SendMessage(l, 0x2c, int32_t(uint32_t(l->selected_index) - (l->visible - 2)), 0);
            }
            Scroll_SendMessage(&l->scroll, 0x2e, l->selected_index, 0);
            __Menu_SendMessage(l->base.owner, message, val(l), 0);
            return 1;
        }
        case 0x4b:
        case 0x5d:
        case 0x5e:
        case 0x6d:
            return __Menu_SendMessage(l->base.owner, message, val(l), 0);
        case 0x54: {
            const int32_t pos = Scroll_SendMessage(&l->scroll, 0x40, 0, 0);
            if (-1 < pos) List_SetIndex(l, &l->rows, pos);
            return 1;
        }
        case 0x58:
            l->colour2 = uint32_t(a);
            return 1;
        case 0x59:
            if (l->columns <= a) return 0;
            l->align[a] = b;
            return 1;
        case 0x62:
            l->outline = uint8_t(a);
            return 1;
        case 0x70:
            l->throb = uint8_t(a);
            return 0;
        case 0x73:
            l->stripe = uint32_t(a);
            return 0;
        case 0x75: {
            GPOINT e{};
            Font_GetTextExtent(kAlphabet, l->format, &e, nullptr, -1);
            l->row_height = e.y * 0.01f * float(a);
            // Peculiaridade do original: a conta está invertida (List_Update refaz a cada tick).
            l->visible = int32_t(l->row_height / float(l->base.h));
            return 0;
        }
        default:
            return 0;
    }
}

// [decomp] 0x0021a598 List_Update
// Um tick da lista: a barra na direita (só se não cabem todas as linhas), a primeira linha visível
// seguindo a escolhida, e para cada linha visível os sprites das células (texto no formato da lista, o
// da escolhida no formato próprio; largura e alinhamento da coluna), as listras de fundo, a cor da
// escolhida (com o pulso) e o destaque sobre ela.
void List_Update(M_MANAGER* m, M_PAGE* page, M_LISTBOX* l) {
    M_PAGE* own = Menu_GetPage(&l->base);
    if (own == nullptr) {
        ++g_menu_guards.no_page;
        return;
    }
    if ((l->base.owner->state & 5) != 0 || (l->base.state & 1) != 0) {
        l->base.state |= 4;
        Component_Free(&l->base);
        Component_Free(&l->highlight);
        Scroll_Update(m, page, &l->scroll);
        for (LIST_ROW* r = first_row(l->rows); r != nullptr; r = r->next) {
            if (r->stripe != nullptr) r->stripe->priority = -1;
        }
        for (uint32_t i = 0;; i = (i + 1) & 0xffff) {
            uint32_t n = uint16_t(l->rows.count);
            if (uint16_t(l->visible) < n) n = uint16_t(l->visible);
            if (n <= i || 32 <= i) return;
            for (sprite* s : l->cells[i]) {
                if (s != nullptr) s->priority = -1;
            }
        }
    }
    l->base.state &= 0xfb;
    if (l->base.w < 0x11) Component_SetupInstance(&l->highlight, 0, -1, -1, -1, -1);
    Menu_ResizeControl(&l->scroll.base, int16_t(l->base.x + l->base.w - 0x10), l->base.y, 0x10, l->base.h);
    const uint32_t count = l->rows.count;
    const uint32_t visible = uint32_t(float(l->base.h) / l->row_height);
    l->visible = int32_t(visible);
    if (int32_t(count) < int32_t(l->top + visible) && visible < count) l->top = int32_t(count - visible);
    l->base.flags = page->focus->id == l->base.id ? 0x20 : 0x10;
    if ((l->base.state & 2) == 0 && 1 < l->visible && (l->scroll.base.state & 1) != 0) {
        l->scroll.base.w = 0x10;
        Component_SetupInstance(&l->base, l->base.flags, -1, -1, l->base.w - 0x10, l->base.h);
    } else if ((l->base.state & 2) != 0 || l->visible == 1) {
        if (((l->scroll.base.state ^ 1) & 1) != 0) {
            l->scroll.base.w = -1;
            Component_SetupInstance(&l->base, l->base.flags, -1, -1, l->base.w, l->base.h);
        }
    }
    if (l->selected != nullptr) {
        const int32_t index = List_GetIndex(l, &l->rows, l->selected);
        if (index < l->top + l->visible) {
            if (l->top > List_GetIndex(l, &l->rows, l->selected)) l->top = List_GetIndex(l, &l->rows, l->selected);
        } else {
            l->top = List_GetIndex(l, &l->rows, l->selected) - l->visible + 1;
        }
    }
    int32_t vis = l->visible;
    if (l->rows.count != 0) {
        uint32_t line = 0;      // índice da linha
        int32_t slot = 0;       // linha visível (cells)
        LIST_ROW* last_real = nullptr;
        for (LIST_ROW* r = first_row(l->rows); r != nullptr; r = r->next) {
            const uint32_t next_line = line + 1;
            if (int32_t(line) < l->top || l->top + l->visible <= int32_t(line)) {
                // Fora da janela: esconde a linha visível de agora (o original não avança `slot`).
                if (slot < 32) {
                    for (int32_t k = 0; k < l->columns; ++k) {
                        if (l->cells[slot][k] != nullptr) l->cells[slot][k]->priority = -1;
                    }
                }
            } else {
                const float row_y = float(int32_t(line - uint32_t(l->top))) * l->row_height;
                if (l->stripe != 0 && (line & 1) == 0) {
                    if (r->stripe == nullptr) r->stripe = Menu_CreateSprite(nullptr, 3);
                    const M_COMPONENT* k = Component_GetHandle(&l->base);
                    const int16_t y = int16_t(int32_t(row_y + float(l->base.y) + float(k->top) + 3.0f) +
                                              uint16_t(own->base.y));
                    Menu_InitializeSprite(r->stripe, 0x28, int16_t(l->base.x + own->base.x), y, l->base.w,
                                          int16_t(int32_t(l->row_height)), nullptr, 0x3000042, l->stripe, l->stripe,
                                          0x26, 0x13, 1, 1, 0);
                }
                const int32_t this_slot = slot;
                for (int32_t col = 0; col < l->columns && this_slot < 32; ++col) {
                    uint32_t offset = 0;
                    for (int32_t k = 0; k < col; ++k) {
                        offset = uint32_t(int32_t(float(offset) + float(l->widths[k]) * float(l->base.w - 0x10) * 0.01f)) & 0xffff;
                    }
                    sprite*& cell = l->cells[this_slot][col];
                    if (cell == nullptr) cell = Menu_CreateSprite(nullptr, 3);
                    char* text = r->cells[col].text;  // nunca nulo (a imagem é um caminho morto no original)
                    GPOINT ext{};
                    GPOINT scale{};
                    Font_GetTextExtent(text, l->format, &ext, &scale, int32_t(std::strlen(text)));
                    const int16_t th = int16_t(int32_t(ext.y));
                    const int32_t yoff = int16_t(int32_t(l->row_height));
                    cell->format = l->format;
                    Menu_ClipString(cell, int32_t(float(l->widths[col]) * float(l->base.w) * 0.01f - 16.0f), l->format);
                    const M_COMPONENT* k = Component_GetHandle(&l->base);
                    const int16_t x = int16_t(int16_t(offset) + own->base.x + l->base.x + k->left + 2);
                    const M_COMPONENT* k2 = Component_GetHandle(&l->base);
                    const uint32_t colour = page->focus->id == l->base.id ? l->colour : l->colour_off;
                    const uint32_t flags = l->outline != 0 ? 0x2000 : 0;
                    const int16_t y = int16_t(int32_t(row_y + float(l->base.y) + float(k2->top) + float(yoff)) +
                                              own->base.y);
                    const int16_t w = int16_t(int32_t(float(l->base.w) * 0.01f * float(l->widths[col])));
                    Menu_InitializeSprite(cell, int16_t(l->base.depth - 7), x, y, w, th, text, 0, colour, l->colour2, 0,
                                          0, 0, 0, flags);
                    cell->format = l->format;
                    GPOINT size{};
                    if (r == l->selected && (l->base.state & 2) == 0) {
                        Font_GetTextExtent(text, l->format_sel, &size, &cell->scale, -1);
                    } else {
                        Font_GetTextExtent(text, l->format, &size, &cell->scale, -1);
                    }
                    cell->w = int16_t(int32_t(size.x));
                    if (l->align[col] == 2) {
                        cell->x = int16_t(int32_t(float(cell->x + w) - size.x));
                    } else if (l->align[col] == 3) {
                        cell->x = int16_t(int32_t(float(cell->x) + (float(w) - size.x) * 0.5f));
                    }
                }
                ++slot;
                bool selected = r == l->selected && (l->base.state & 2) == 0;
                if (!selected && last_real == l->selected && r->continuation != 0) selected = true;
                if (selected) {
                    if (l->highlight_mode - 1u < 2 && this_slot < 32) {
                        for (int32_t col = 0; col < l->columns; ++col) {
                            sprite* cell = l->cells[this_slot][col];
                            cell->colour = l->throb != 0 ? pulse(l->colour_sel) : l->colour_sel;
                        }
                    }
                    if (l->highlight_mode == 1 && r->continuation == 0) {
                        M_CONTROL* hl = &l->highlight;
                        const M_COMPONENT* k = Component_GetHandle(hl);
                        const int32_t x = l->base.x + k->left;
                        const float y = float(int32_t(line - uint32_t(l->top))) * l->row_height + float(l->base.y) +
                                        float(Component_GetHandle(hl)->top);
                        int32_t right = Component_GetHandle(hl)->left;
                        if (1 < l->visible) right += 0x10;
                        const int32_t w = l->base.w - right - Component_GetHandle(hl)->right;
                        const int32_t h = int32_t(l->row_height + float(Component_GetHandle(hl)->bottom));
                        Component_SetupInstance(hl, (l->base.state & 1) == 0 ? l->base.flags : 0, x, int32_t(y), w, h);
                    }
                }
            }
            line = next_line;
            if (r->continuation == 0) last_real = r;
        }
        vis = l->visible;
    }
    const int32_t w = 1 < vis && (l->base.state & 2) == 0 ? l->base.w - 0x10 : l->base.w;
    Component_SetupInstance(&l->base, l->base.flags, -1, -1, w, l->base.h);
    Scroll_Update(m, page, &l->scroll);
}

// ---- rádio ----

// [decomp] 0x00220130 Radio_GetIndex
int32_t Radio_GetIndex(M_RADIO* r, LINKEDLIST*, LIST_ROW* row) {
    int32_t i = 0;
    for (LIST_ROW* o = first_row(r->options); o != nullptr; o = o->next, ++i) {
        if (o == row) return i;
    }
    return -1;
}

// [decomp] 0x00220180 Radio_SetIndex
// A opção `index` (parando na última).
LIST_ROW* Radio_SetIndex(M_RADIO* r, LINKEDLIST*, int32_t index) {
    LIST_ROW* o = first_row(r->options);
    int32_t i = 0;
    if (0 < index && o != nullptr) {
        LIST_ROW* next = o->next;
        for (;;) {
            ++i;
            if (next == nullptr) break;
            o = next;
            if (index <= i) break;
            next = next->next;
        }
    }
    return o;
}

// [decomp] 0x0021f8e0 Radio_Init
// O rádio: um rótulo centrado entre as margens do componente 5 e uma barra horizontal por cima de
// tudo, com uma posição por opção.
void Radio_Init(M_RADIO* r, NEW_CONTROL* desc, M_CONTROL* owner) {
    r->base.type = MCT_RADIO;
    r->base.state = 0;
    r->base.owner = owner;
    r->base.unknown_10 = 0;
    r->base.component = uint32_t(desc->owner) << 16 | 5;
    r->base.id = desc->id;
    r->base.sprites = nullptr;
    r->base.tag = desc->unknown_0c;
    r->selected = nullptr;
    r->base.flags = 0;
    r->base.depth = desc->depth;
    LinkedList_Init(&r->options, 0);
    r->base.x = desc->x;
    r->base.y = desc->y;
    r->base.w = desc->w;
    r->base.h = desc->h;
    NEW_CONTROL label;
    amemcpy(&label, desc, sizeof(NEW_CONTROL));
    label.x = int16_t(label.x + Component_GetHandle(&r->base)->left);
    const M_COMPONENT* k = Component_GetHandle(&r->base);
    label.w = int16_t(label.w - (k->left + Component_GetHandle(&r->base)->right));
    label.format = kRadioFormat;
    label.depth = uint16_t(label.depth - 5);
    label.unknown_10 = 0;
    Label_Init(&r->label, &label, &r->base);
    NEW_CONTROL bar;
    amemcpy(&bar, desc, sizeof(NEW_CONTROL));
    bar.format = nullptr;
    bar.unknown_08 = 0;
    reinterpret_cast<uint8_t*>(&bar)[0x20] = 0;
    reinterpret_cast<uint8_t*>(&bar)[0x21] = 0;
    bar.unknown_10 = 0;
    Scroll_Init(&r->scroll, &bar, &r->base);
    Script_PrepareScripting(&r->base);
}

// [decomp] 0x0021fa50 Radio_SendMessage
// 0x10/0x11 acrescenta uma opção (texto ou rótulo, valor b), 0x17 limpa, 0x1d/0x1e escolhe por índice/
// valor, 0x20 valor da última; consultas 0x33-0x35, 0x3b; o resto vai ao rótulo.
int32_t Radio_SendMessage(M_RADIO* r, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x10:
        case 0x11: {
            if (message == 0x11) a = val(Txt_BindLabel(uint32_t(a), 0));
            LIST_ROW* o = static_cast<LIST_ROW*>(Menu_Malloc(0x2d0, 2));
            std::strcpy(o->cells[0].text, ptr<char>(a));
            o->data = b;
            LinkedList_Add(&r->options, &o->prev, o);
            if (1 < r->options.count) {
                const int32_t max = Scroll_SendMessage(&r->scroll, 0x38, 0, 0);
                Scroll_SendMessage(&r->scroll, 0x25, max + 1, 0);
            }
            if (r->options.count == 1) r->selected = first_row(r->options);
            return 1;
        }
        case 0x14:
            return Script_ExitLoop(&r->base);
        case 0x16:
            return Script_Play(&r->base, uint32_t(a), int8_t(b));
        case 0x17: {
            if (r->options.count != 0) {
                for (LIST_ROW* o = first_row(r->options); o != nullptr;) {
                    LIST_ROW* next = o->next;
                    LinkedList_Remove(&r->options, &o->prev, o);
                    void* p = o;
                    Menu_Free(&p, 2);
                    o = next;
                }
            }
            r->selected = nullptr;
            Scroll_SendMessage(&r->scroll, 0x2e, 0, 0);
            Scroll_SendMessage(&r->scroll, 0x25, 0, 0);
            return 0;
        }
        case 0x1d: {
            const int32_t index = Radio_GetIndex(r, &r->options, r->selected);
            if (Scroll_SendMessage(&r->scroll, 0x2e, index, 0) == 0) return 0;
            r->selected = Radio_SetIndex(r, &r->options, a);
            Scroll_SendMessage(&r->scroll, 0x2e, a, b);
            return __Menu_SendMessage(r->base.owner, 0x49, val(r), 0);
        }
        case 0x1e: {
            LIST_ROW* o = first_row(r->options);
            for (; o != nullptr; o = o->next) {
                if (o->data == a) break;
            }
            if (o == nullptr) {
                if (b != 0) return 1;
                return __Menu_SendMessage(r->base.owner, 0x49, val(r), 0);
            }
            r->selected = o;
            Scroll_SendMessage(&r->scroll, 0x2e, Radio_GetIndex(r, &r->options, r->selected), 0);
            if (b != 0) return 1;
            return __Menu_SendMessage(r->base.owner, 0x49, val(r), 0);
        }
        case 0x20: {
            if (r->selected == nullptr) return 0;
            // Peculiaridade do original: muda o valor da última opção, não o da escolhida.
            LIST_ROW* last = r->options.tail != nullptr
                                 ? reinterpret_cast<LIST_ROW*>(static_cast<uint8_t*>(r->options.tail) - r->options.offset * 4)
                                 : nullptr;
            last->data = a;
            return 1;
        }
        case 0x21:
            if (6 < uint32_t(a)) return 0;
            reinterpret_cast<M_SCRIPT**>(r->base.defaults)[a] = Script_Select(&r->base, uint32_t(b));
            return 1;
        case 0x2b:
            r->base.state = uint8_t(a);
            Label_SendMessage(&r->label, 0x2b, a, b);
            Scroll_SendMessage(&r->scroll, 0x2b, a, b);
            return 1;
        case 0x33:
            return int32_t(r->options.count - 1);
        case 0x34:
        case 0x40:
            return r->scroll.pos;
        case 0x35:
            return r->selected == nullptr ? 0 : r->selected->data;
        case 0x39:
            return val(r);
        case 0x3b:
            return r->selected == nullptr ? 0 : val(r->selected->cells[0].text);
        case 0x3d:
            return int32_t(r->base.tag);
        case 0x47: {
            Radio_SendMessage(r, 0x17, 0, 0);
            Label_SendMessage(&r->label, 0x47, 0, 0);
            Scroll_SendMessage(&r->scroll, 0x47, 0, 0);
            Component_Free(&r->base);
            Script_DelAllScripts(&r->base);
            void* p = r;
            Menu_Free(&p, 3);
            return 0;
        }
        case 0x49:
        case 0x4b:
        case 0x52:
        case 0x5d:
        case 0x5e: {
            const int32_t pos = Scroll_SendMessage(&r->scroll, 0x40, 0, 0);
            r->selected = Radio_SetIndex(r, &r->options, pos);
            __Menu_SendMessage(r->base.owner, message, val(r), 0);
            return 1;
        }
        case 0x54: {
            const int32_t pos = Scroll_SendMessage(&r->scroll, 0x40, 0, 0);
            r->selected = Radio_SetIndex(r, &r->options, pos);
            return __Menu_SendMessage(r->base.owner, 0x49, val(r), 1);
        }
        default:
            return Label_SendMessage(&r->label, message, a, b);
    }
}

// [decomp] 0x0021ff80 Radio_Update
void Radio_Update(M_MANAGER* m, M_PAGE* page, M_RADIO* r) {
    if ((r->base.owner->state & 5) != 0 || (r->base.state & 1) != 0) {
        r->base.state |= 4;
        Component_Free(&r->base);
        Scroll_Update(m, page, &r->scroll);
        Label_Update(m, page, &r->label, int8_t(Menu_CursorOverMe(m, &r->base)));
        return;
    }
    r->base.state &= 0xfb;
    Menu_ResizeControl(&r->scroll.base, &r->base);
    const M_COMPONENT* k = Component_GetHandle(&r->base);
    const int16_t left = k->left;
    const M_COMPONENT* k2 = Component_GetHandle(&r->base);
    Menu_ResizeControl(&r->label.base, int16_t(r->base.x + left), int16_t(r->base.y - 1),
                       int16_t(r->base.w - k2->left - Component_GetHandle(&r->base)->right), r->base.h);
    Scroll_Update(m, page, &r->scroll);
    static char kNone[] = " ";  // 0x0030d648
    r->label.string = r->selected != nullptr ? r->selected->cells[0].text : kNone;
    Label_Update(m, page, &r->label, int8_t(Menu_CursorOverMe(m, &r->base)));
}

// ---- memo ----

// [decomp] 0x0021df38 Memo_Init
// O memo: texto com quebra de linha em até 32 sprites, uma barra vertical na direita.
void Memo_Init(M_MEMOBOX* mm, NEW_CONTROL* desc, M_CONTROL* owner) {
    GPOINT e{};
    Font_GetTextExtent(kAlphabet, kMemoFormat, &e, nullptr, -1);
    mm->base.state = 0;
    mm->base.type = MCT_MEMO;
    mm->base.owner = owner;
    mm->top = 0;
    mm->base.component = uint32_t(desc->owner) << 16 | 3;
    mm->base.id = desc->id;
    mm->line_height = e.y * 1.5f;
    mm->colour = 0xff;
    mm->base.tag = desc->unknown_0c;
    mm->colour_off = 0x404040ffu;
    mm->colour2 = 0x80;
    mm->base.sprites = nullptr;
    mm->count = 0;
    mm->visible = int32_t(float(desc->h) / (e.y * 1.5f));
    mm->text = kMemoEmpty;
    mm->base.depth = desc->depth;
    mm->vcentre = 1;
    mm->format = kMemoFormat;
    mm->centre = 1;
    mm->used = 0;
    for (sprite*& s : mm->lines) s = nullptr;
    mm->base.x = desc->x;
    mm->base.y = desc->y;
    mm->base.w = desc->w;
    mm->base.h = desc->h;
    NEW_CONTROL bar;
    amemcpy(&bar, desc, sizeof(NEW_CONTROL));
    const int16_t w = bar.w;
    bar.h = mm->base.h;
    bar.depth = uint16_t(desc->depth - 1);
    bar.w = 0x10;
    bar.x = int16_t(w + bar.x - 0x10);
    bar.unknown_08 = 10;
    bar.format = nullptr;
    reinterpret_cast<uint8_t*>(&bar)[0x20] = 0;
    reinterpret_cast<uint8_t*>(&bar)[0x21] = 0;
    bar.unknown_10 = 0;
    Scroll_Init(&mm->scroll, &bar, &mm->base);
    Script_PrepareScripting(&mm->base);
}

namespace {
void refresh_memo(M_MEMOBOX* mm) {
    refresh_quiet(mm, [](M_MANAGER* m, M_PAGE* p, M_MEMOBOX* x) { Memo_Update(m, p, x); }, false);
}
}  // namespace

// [decomp] 0x0021e108 Memo_SendMessage
// 0x18 o texto, 0x23 o formato, 0x1b/0x1c/0x58 cores, 100 centra as linhas, 0x66 centra na vertical,
// 0x75 altura da linha em %; consultas 0x2f, 0x31, 0x32, 0x39, 0x65 (linhas em uso).
int32_t Memo_SendMessage(M_MEMOBOX* mm, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x16:
            return Script_Play(&mm->base, uint32_t(a), int8_t(b));
        case 0x18:
            mm->text = ptr<char>(a);
            mm->scroll.pos = 0;  // M_MEMOBOX +0x1c4: volta ao começo
            refresh_memo(mm);
            return 1;
        case 0x1b:
            mm->colour = uint32_t(a);
            return 1;
        case 0x1c:
            mm->colour_off = uint32_t(a);
            return 1;
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<M_SCRIPT**>(mm->base.defaults)[a] = Script_Select(&mm->base, uint32_t(b));
            return 1;
        case 0x23:
            mm->format = ptr<char>(a);
            return 1;
        case 0x2b:
            mm->base.state = uint8_t(a);
            Scroll_SendMessage(&mm->scroll, 0x2b, a, b);
            if ((mm->base.state & 1) != 0) {
                Component_SetupInstance(&mm->base, 0, -1, -1, -1, -1);
                for (int32_t i = 0; i < 32 && mm->lines[i] != nullptr; ++i) mm->lines[i]->priority = -1;
            }
            refresh_memo(mm);
            return 1;
        case 0x2f:
            return val(mm->text);
        case 0x31:
            return int32_t(mm->colour);
        case 0x32:
            return int32_t(mm->colour_off);
        case 0x39:
            return val(mm);
        case 0x47: {
            Component_Free(&mm->base);
            Scroll_SendMessage(&mm->scroll, 0x47, 0, b);
            if (uint16_t(mm->count) != 0) {
                for (uint32_t i = 0; mm->lines[i] != nullptr;) {
                    Sprite_Delete(mm->lines[i]);
                    i = (i + 1) & 0xffff;
                    if (uint16_t(mm->count) <= i || 32 <= i) break;
                }
            }
            Script_DelAllScripts(&mm->base);
            if (mm->base.owner->type == MCT_PAGE) {
                void* p = mm;
                Menu_Free(&p, 3);
            }
            return 0;
        }
        case 0x4b:
            return __Menu_SendMessage(mm->base.owner, 0x4b, val(mm), 0);
        case 0x58:
            mm->colour2 = uint32_t(a);
            return 1;
        case 100:
            mm->centre = uint8_t(a);
            return 1;
        case 0x65:
            return mm->used;
        case 0x66:
            mm->vcentre = uint8_t(a);
            return 0;
        case 0x75: {
            GPOINT e{};
            Font_GetTextExtent(kAlphabet, mm->format, &e, nullptr, -1);
            mm->line_height = e.y * 0.01f * float(a);
            // Peculiaridade do original: a conta está invertida (Memo_Update refaz a cada tick).
            mm->visible = int32_t(mm->line_height / float(mm->base.h));
            return 0;
        }
        default:
            return 0;
    }
}

// [decomp] 0x0021e490 Memo_Update
// Um tick do memo: quebra o texto na largura (Font_WordWrapString) e monta um sprite por linha visível
// (centrada se `centre`), a barra quando o texto não cabe, centra o bloco na vertical se `vcentre`, e os
// eventos de escolha (0x22 com o foco: 0x52 ao dono).
void Memo_Update(M_MANAGER* m, M_PAGE* page, M_MEMOBOX* mm) {
    int32_t which = 0;
    if ((mm->base.owner->state & 5) != 0 || (mm->base.state & 1) != 0) {
        mm->base.state |= 4;
        Component_Free(&mm->base);
        Scroll_Update(m, page, &mm->scroll);
        if (uint16_t(mm->count) == 0 || mm->lines[0] == nullptr) return;
        for (uint32_t i = 0;;) {
            Sprite_Delete(mm->lines[i]);
            mm->lines[i] = nullptr;
            i = (i + 1) & 0xffff;
            if (uint16_t(mm->count) <= i || 32 <= i) return;
            if (mm->lines[i] == nullptr) return;
        }
    }
    M_CONTROL* bar = &mm->scroll.base;
    mm->base.state &= 0xfb;
    Menu_ResizeControl(bar, int16_t(mm->base.x + mm->base.w - 0x12), int16_t(mm->base.y + (mm->base.h >> 1) - 8), 0x24,
                       0x10);
    const int32_t count = mm->count;
    const int32_t visible = int32_t(float(mm->base.h) / mm->line_height);
    mm->visible = visible;
    if (count < mm->top + visible && visible < count) mm->top = count - visible;
    mm->base.flags = (page->focus != nullptr && page->focus->id == mm->base.id) ? 0x20 : 0x10;
    if (mm->visible < 2 || (mm->scroll.base.state & 1) == 0) {
        if (mm->visible == 1 && ((mm->scroll.base.state ^ 1) & 1) != 0) {
            Scroll_SendMessage(&mm->scroll, 0x2b, 1, 0);
            Component_SetupInstance(&mm->base, mm->base.flags, -1, -1, mm->base.w, mm->base.h);
        }
    } else {
        Scroll_SendMessage(&mm->scroll, 0x2b, 0, 0);
        Component_SetupInstance(&mm->base, mm->base.flags, -1, -1, mm->base.w - 0x10, mm->base.h);
    }
    mm->used = 0;
    if (mm->text != nullptr) {
        for (int32_t i = 0; i < 32 && mm->lines[i] != nullptr; ++i) mm->lines[i]->priority = -1;
        mm->top = Scroll_SendMessage(&mm->scroll, 0x40, 0, 0);
        char* cursor = mm->text;
        mm->count = 0;
        do {
            const uint16_t len = uint16_t(std::strlen(cursor));
            const M_COMPONENT* k = Component_GetHandle(&mm->base);
            const M_COMPONENT* k2 = Component_GetHandle(&mm->base);
            char* line = Font_WordWrapString(&cursor, len, mm->format,
                                             uint32_t(mm->base.w - (k->left + 0x10) - k2->right), nullptr);
            if (line == nullptr) break;
            GPOINT e{};
            Font_GetTextExtent(line, mm->format, &e, nullptr, -1);
            const int16_t indent = mm->centre == 1 ? int16_t(int32_t((float(mm->base.w) - e.x) * 0.5f)) : 0;
            if (mm->count < mm->top + mm->visible) ++mm->visible;
            if (mm->top <= mm->count && mm->used < 32) {
                sprite*& s = mm->lines[mm->used];
                if (s == nullptr) s = Sprite_Create();
                const M_COMPONENT* kk = Component_GetHandle(&mm->base);
                const int16_t x = int16_t(kk->left + mm->base.x + indent + page->base.x + 2);
                const M_COMPONENT* kk2 = Component_GetHandle(&mm->base);
                const uint32_t colour = page->focus != nullptr && page->focus->id == mm->base.id ? mm->colour : mm->colour_off;
                const int16_t y = int16_t(int32_t(float(mm->count - mm->top) * mm->line_height + float(mm->base.y) +
                                                  float(kk2->top) + float(int32_t(mm->line_height))) +
                                          page->base.y);
                Menu_InitializeSprite(s, int16_t(mm->base.depth - 5), x, y, int16_t(mm->base.w - 0x10), 0xb, line, 0, colour,
                                      mm->colour2, 0, 0, 0, 0, 0x2000);
                s->format = mm->format;
                ++mm->used;
            }
            ++mm->count;
        } while (cursor != nullptr);
    }
    if (mm->vcentre != 0) {
        const uint8_t used = mm->used;
        const int16_t h = mm->base.h;
        const float lh = mm->line_height;
        for (uint32_t i = 0; i < used; i = (i + 1) & 0xff) {
            mm->lines[i]->y = int16_t(mm->lines[i]->y + int16_t(int32_t((float(h) - lh * float(used)) * 0.5f)));
        }
    }
    if (Menu_CursorOverMe(m, &mm->base) != 0) which = pressed_button(m);
    int32_t rest = mm->count - mm->visible;
    if (rest < 0) rest = 0;
    Scroll_SendMessage(&mm->scroll, 0x27, 0, rest);
    rest = mm->count - mm->visible;
    if (rest < 0) rest = 0;
    if (rest < 1 && mm->scroll.base.state == 0) {
        Scroll_SendMessage(&mm->scroll, 0x2b, 1, 0);
    } else if ((mm->scroll.base.state & 1) != 0) {
        Scroll_SendMessage(&mm->scroll, 0x2b, 0, 0);
    }
    Component_SetupInstance(&mm->base, mm->base.flags, -1, -1, mm->base.w, mm->base.h);
    if (((mm->scroll.base.state ^ 1) & 1) != 0) Scroll_Update(m, page, &mm->scroll);
    if (page->focus != nullptr && page->focus->id == mm->base.id && Menu_InputAction(0, 0x22, 4) != 0) {
        __Menu_SendMessage(mm->base.owner, 0x52, val(mm), 0);
    }
    send_choice(m, &mm->base, which);
}
