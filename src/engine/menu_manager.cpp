// O gerente de menus: MenuManager_*, Manager_*, as páginas (Page_*), a janela (Window_*), a lista
// ligada genérica (LinkedList_*) e os processos de cor (Process_*). Ver engine/menu.h.
//
// Divergências (comentadas nas funções):
// - MenuManager_Create: o cursor (CursorInfo) leva no rótulo o endereço 0x0030d610 do PS2, como no
//   original; Txt_BindLabel o recusa e o sprite fica escondido (prioridade -1) do mesmo jeito.
// - Manager_SendMessage 0x44 com a página atual nula não empilha uma entrada (o original leria o foco
//   de uma página nula); 0x47, Process_Destroy e Page_SendMessage 0x47 leem o próximo nó antes de
//   liberar (o original lê depois, do bloco já livre; o heap não muda esses bytes).
// - Handler_HandleMessage com um controle nulo devolve 0 (o original leria o identificador em 0x18).
// - MenuManager_Monitor: psiMultitapPresent é um stub (nenhum multitap); o resto é o do jogo.
// - MenuManager_Update chama Menu_MouseUpdate (só no port, engine/menu_mouse.h) no começo do tick do gerente que
//   recebe a entrada: o mouse põe o cursor do gerente e as ações de escolher e voltar. Logo depois, o gancho
//   Menu_SetPortTick (só no port, nulo por padrão): os pedidos dos menus Modern (engine/menu_modern.h), pelos
//   mesmos caminhos.

#include "engine/menu.h"
#include "engine/menu_mouse.h"

#include <cstdio>
#include <cstring>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/script.h"
#include "engine/sprite.h"
#include "engine/txt.h"
#include "platform/pending.h"
#include "platform/psi.h"

M_MANAGER menu_manager[kMenuManagers];
uint8_t* file_start = nullptr;
uint8_t user_restarted_or_quit = 0;
M_CONTROL* help_control = nullptr;
uint32_t help_table[kHelpEntries * 3];
SpriteInfo CursorInfo;
MenuGuards g_menu_guards;
MenuStats g_menu_stats;
uint32_t g_menu_file = 0;

// Globais de outros módulos que o gerente usa.
extern uint8_t menu_movie;        // 0x002e1c48 a música do menu já tocando (menu_handlers.cpp)
extern uint8_t ls[0x44];          // 0x002e1ad0 o perfil do jogador (menu_handlers.cpp)
extern uint32_t PS2FramesToSkip;  // menu.cpp
extern uint8_t MPSettings[];

namespace {

int16_t player_index = -1;        // 0x0030d608 o controle que sumiu (&_heap_size: nenhum)
uint32_t controller_state = 0;    // 0x0030d604
int32_t counter_152 = 10;
uint8_t was_multitap_inserted_150 = 0;
uint32_t frames_156 = 0;
char buf_155[0x80];               // texto de "controle desconectado"
MenuPortTick g_port_tick = nullptr;  // só no port: Menu_SetPortTick
const char ps2_normal_154[] = "1\0" "2";  // os números das portas (2 bytes cada)
const char ps2_multitap_153[] = "1-A\0" "1-B\0" "1-C\0" "1-D";

template <class T>
T* ptr(int32_t v) {
    return nf::p32_ptr<T>(uint32_t(v));
}
int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }

uint32_t get_dword(uint8_t** p) {
    uint32_t v;
    std::memcpy(&v, *p, 4);
    *p += 4;
    return v;
}
uint16_t get_word(uint8_t** p) {
    uint16_t v;
    std::memcpy(&v, *p, 2);
    *p += 2;
    return v;
}
uint8_t get_byte(uint8_t** p) { return *(*p)++; }

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}

M_PAGE* first_page(M_MANAGER* m) { return static_cast<M_PAGE*>(ll_first(m->pages)); }
M_PAGE* next_page(M_PAGE* p) {
    return p->next != nullptr ? reinterpret_cast<M_PAGE*>(reinterpret_cast<uint8_t*>(nf::raw(p->next)) - 0xa4) : nullptr;
}
M_CONTROL* first_control(M_PAGE* p) { return static_cast<M_CONTROL*>(ll_first(p->controls)); }

// O cursor do gerente no meio do controle c da página.
void cursor_to(M_MANAGER* m, M_PAGE* page, M_CONTROL* c) {
    m->cursor->x = int16_t(page->base.x + c->x + (c->w >> 1));
    m->cursor->y = int16_t(m->page->base.y + c->y + (c->h >> 1));
}

void free_stack(M_MANAGER* m) {
    while (Stack_IsEmpty(&m->stack) == 0) {
        void* entry = ptr<void>(Stack_Top(&m->stack));
        Stack_Pop(&m->stack);
        Menu_Free(&entry, 8);
    }
}

}  // namespace

// ---- lista ligada ----

// [decomp] 0x00219280 LinkedList_Init
void LinkedList_Init(LINKEDLIST* list, uint32_t offset) {
    if (list == nullptr) return;
    list->offset = offset;
    list->head = nullptr;
    list->tail = nullptr;
    list->count = 0;
}

// [decomp] 0x002192a0 LinkedList_ReInit
void LinkedList_ReInit(LINKEDLIST* list) {
    if (list == nullptr) return;
    list->count = 0;
    list->head = nullptr;
    list->tail = nullptr;
}

// [decomp] 0x002192c0 LinkedList_Add
// Põe o nó no fim: node->next = 0, *prev_field = o último.
// Os nós são palavras de ponteiro de 4 bytes (P32: em x64 deslocamentos na janela, platform/ptr32.h).
uint32_t LinkedList_Add(LINKEDLIST* list, void* prev_field, void* node) {
    if (list == nullptr || prev_field == nullptr || node == nullptr) return 0;
    if (list->tail != nullptr) *static_cast<P32<void>*>(nf::raw(list->tail)) = node;
    *static_cast<P32<void>*>(node) = nullptr;
    *static_cast<P32<void>*>(prev_field) = list->tail;
    if (list->count == 0) list->head = node;
    list->tail = node;
    ++list->count;
    return 1;
}

// [decomp] 0x00219318 LinkedList_Insert
// Põe o nó depois de `after` (nulo: no começo).
uint32_t LinkedList_Insert(LINKEDLIST* list, void* after, void* prev_field, void* node) {
    if (list == nullptr || prev_field == nullptr || node == nullptr) return 0;
    P32<void>* n = static_cast<P32<void>*>(node);
    if (after == nullptr) {
        if (list->head != nullptr) static_cast<P32<void>*>(nf::raw(list->head))[1] = node;
        n[0] = list->head;
        *static_cast<P32<void>*>(prev_field) = nullptr;
        list->head = node;
    } else {
        P32<void>* a = static_cast<P32<void>*>(after);
        P32<void>* next = static_cast<P32<void>*>(nf::raw(a[0]));
        if (next != nullptr) next[1] = node;
        a[0] = node;
        n[0] = next;
        *static_cast<P32<void>*>(prev_field) = after;
    }
    if (n[0] == nullptr) list->tail = node;
    ++list->count;
    return 1;
}

// [decomp] 0x002193b0 LinkedList_Remove
uint32_t LinkedList_Remove(LINKEDLIST* list, void* prev_field, void* node) {
    if (list == nullptr || prev_field == nullptr || node == nullptr) return 0;
    P32<void>* prev = static_cast<P32<void>*>(nf::raw(*static_cast<P32<void>*>(prev_field)));
    void* next = *static_cast<P32<void>*>(node);
    if (prev == nullptr) {
        list->head = next;
    } else {
        *prev = next;
    }
    if (next == nullptr) {
        list->tail = prev;
    } else {
        static_cast<P32<void>*>(next)[1] = prev;
    }
    if (--list->count == 0) {
        list->tail = nullptr;
        list->head = nullptr;
    }
    return 1;
}

// [decomp] 0x00219428 LinkedList_GetIndex
// O index-ésimo item. Peculiaridade do original: o próximo nó é lido em nó + offset * 4 (certo só com
// offset 0, o único usado).
void* LinkedList_GetIndex(LINKEDLIST* list, uint32_t index) {
    if (list == nullptr || list->count - 1u < index) return nullptr;
    uint8_t* node = static_cast<uint8_t*>(list->head);
    for (uint32_t i = 0;; ++i) {
        if (node == nullptr) return nullptr;
        if (i == index) break;
        node = *reinterpret_cast<P32<uint8_t>*>(node + list->offset * 4);
    }
    return node - list->offset * 4;
}

// ---- gerente ----

// [decomp] 0x0021b570 MenuManager_Init
void MenuManager_Init() {
    std::memset(menu_manager, 0, sizeof(menu_manager));
    for (int i = 0; i < kMenuManagers; ++i) {
        M_MANAGER& m = menu_manager[i];
        m.index = uint8_t(i);
        m.player = 0;
        m.page = nullptr;
        m.previous = nullptr;
        m.overlay = nullptr;
        m.active = 0;
        m.transition = 0;
        m.frozen = 0;
        m.close_action = 0x1e;
    }
    ls[0x3b] = 1;
    player_index = -1;
    controller_state = 0;
    user_restarted_or_quit = 0;
    Component_InitComponents();
}

// [decomp] 0x0021b610 FixupResolution
// Do arquivo (640 x 480) para a tela do PS2 (512 x 448), mantendo o centro: largura x 0.8 (pelo
// menos 2), altura x 0.9333 (pelo menos 2; uma barra de rolagem pelo menos 16).
void FixupResolution(NEW_CONTROL* desc) {
    const int16_t w0 = desc->w;
    int32_t w = int32_t(float(w0) * 0.8f);
    desc->w = int16_t(w);
    if (int16_t(w) < 2) w = 2;
    desc->w = int16_t(w);
    desc->x = int16_t(int32_t((float(desc->x + (w0 >> 1) - 0x140) * 0.8f + 256.0f) - float(int16_t(w) >> 1)));
    const int16_t h0 = desc->h;
    int16_t h = int16_t(int32_t(float(h0) * 0.9333334f));
    desc->h = h;
    const uint8_t type = reinterpret_cast<uint8_t*>(desc)[0x21];
    if (type == MCT_SCROLL) {
        if (h < 0x10) desc->h = 0x10;
    } else if (h < 2) {
        desc->h = 2;
    }
    desc->y = int16_t(int32_t((float(desc->y + (h0 >> 1) - 0xf0) * 0.9333334f + 224.0f) - float(desc->h >> 1)));
}

// [decomp] 0x0021b7a8 FixupResolution
void FixupResolution(M_KEYFRAME* key) {
    const int16_t w0 = key->w;
    int32_t w = int32_t(float(w0) * 0.8f);
    key->w = int16_t(w);
    if (int16_t(w) < 2) w = 2;
    key->w = int16_t(w);
    key->x = int16_t(int32_t((float(key->x + (w0 >> 1) - 0x140) * 0.8f + 256.0f) - float(int16_t(w) >> 1)));
    const int16_t h0 = key->h;
    int32_t h = int32_t(float(h0) * 0.9333334f);
    key->h = int16_t(h);
    if (int16_t(h) < 2) h = 2;
    key->h = int16_t(h);
    key->y = int16_t(int32_t((float(key->y + (h0 >> 1) - 0xf0) * 0.9333334f + 224.0f) - float(int16_t(h) >> 1)));
}

// [decomp] 0x0021b908 MenuManager_Create
// Cria o menu `menu` no gerente `index` (se ele estiver livre) a partir do arquivo carregado: lê os
// registros (docs/formats.md, "Menus (item tipo 8)") e cria as páginas do menu pedido, os controles
// delas (só os de plataforma 0 ou 3), os scripts, os quadros-chave e as mensagens deles, e os
// conjuntos de componentes; depois o cursor, mostra `page` e põe o foco no primeiro controle de id
// 0x10000002. Devolve o índice do gerente (o registro 0xfffffff2 fecha o arquivo) ou 0xffffffff.
uint32_t MenuManager_Create(uint32_t menu, uint32_t page, int16_t player, uint8_t index, uint32_t close_action,
                            int8_t pause_sounds, uint32_t status) {
    if (file_start == nullptr) return 0xffffffffu;
    M_MANAGER* m = &menu_manager[index];
    if (m->active != 0) return 0xffffffffu;
    Menu_StringInit(32000);
    if (pause_sounds != 0) {
        SFXPause();
        SFXPauseAllStreams();
    }
    menu_movie = 0;
    Input_RumbleStop(0);
    Input_RumbleStop(1);
    Input_RumbleStop(2);
    Input_RumbleStop(3);
    LinkedList_Init(&m->pages, 0x29);
    m->base.type = MCT_MANAGER;
    m->active = 1;
    m->close_action = close_action;
    m->player_byte = uint8_t(player);
    m->player = player;
    m->index = index;
    m->status = status;
    m->movie = 0;
    m->pause_sounds = uint8_t(pause_sounds);
    m->transition = 0;
    m->frozen = 0;
    m->first_page = page;
    user_restarted_or_quit = 0;
    if (player == -1) m->player = -1;
    m->page = nullptr;
    m->previous = nullptr;
    m->overlay = nullptr;
    Page_FindHelpControl(nullptr);
    ++g_menu_stats.created;
    M_CONTROL* control = nullptr;  // o último controle criado
    uint32_t result = 0xffffffffu;
    Stack_Init(&m->stack, 0x40, m->stack_data);
    bool skip = true;              // o controle atual não é desta plataforma
    M_COMPONENTSET* set = nullptr;
    MenuSpriteInfo* pieces = nullptr;
    uint8_t* p = file_start;
    get_dword(&p);
    M_SCRIPT* script = nullptr;
    M_KEYFRAME* key = nullptr;
    uint32_t component = 0;
    uint32_t piece = 0;
    bool more = true;
    // Registros montados na pilha (como no original, a mesma área para os três).
    alignas(4) uint8_t record[0x58]{};
    NEW_CONTROL& desc = *reinterpret_cast<NEW_CONTROL*>(record);
    M_KEYFRAME& frame = *reinterpret_cast<M_KEYFRAME*>(record);
    M_MESSAGE& message = *reinterpret_cast<M_MESSAGE*>(record);
    static_assert(sizeof(NEW_CONTROL) <= sizeof(record) && sizeof(M_KEYFRAME) <= sizeof(record));
    do {
        const uint32_t token = get_dword(&p);
        switch (token) {
            case 0xfffffff0: {  // página: id, menu, filme, plataforma, tamanho
                const uint32_t id = get_dword(&p);
                const uint32_t owner = get_dword(&p);
                const uint32_t movie = get_dword(&p);
                const uint8_t kind = get_byte(&p);
                const uint32_t size = get_dword(&p);
                if (owner == menu && (kind == 0 || kind == 3)) {
                    M_PAGE* pg = ptr<M_PAGE>(Manager_SendMessage(m, 0x43, int32_t(id), int32_t(owner)));
                    if (pg != nullptr) pg->movie = movie;
                } else {
                    p += size;
                }
                break;
            }
            case 0xfffffff2:  // fim
                if (control != nullptr) {
                    Manager_SendMessage(m, 0x51, val(control), 0);
                    Page_SendMessage(m->page, 0x51, val(control), 1);
                }
                result = index;
                more = false;
                break;
            case 0xfffffff3: {  // mensagem ao gerente
                message.target = 0;
                message.message = get_byte(&p);
                message.a = int32_t(get_dword(&p));
                message.b = int32_t(get_dword(&p));
                Manager_SendMessage(m, message.message, message.a, message.b);
                break;
            }
            case 0xfffffff4:  // mensagem de quadro-chave
                message.target = get_dword(&p);
                message.message = get_byte(&p);
                message.a = int32_t(get_dword(&p));
                message.b = int32_t(get_dword(&p));
                if (!skip) Script_AddMessage(key, &message);
                break;
            case 0xfffffff5:  // quadro-chave
                frame.frame = get_word(&p);
                frame.x = int16_t(get_word(&p));
                frame.y = int16_t(get_word(&p));
                frame.w = int16_t(get_word(&p));
                frame.h = int16_t(get_word(&p));
                frame.flags = get_dword(&p);
                if (!skip) {
                    FixupResolution(&frame);
                    key = Script_AddKeyFrame(script, &frame);
                }
                break;
            case 0xfffffff6:  // mensagem ao controle
                message.target = 0;
                message.message = get_byte(&p);
                message.a = int32_t(get_dword(&p));
                message.b = int32_t(get_dword(&p));
                // Cores do arquivo trocadas por outra (0x1a-0x1c).
                if (message.message - 0x1au < 3 && uint32_t(message.a) == 0x7d6d59ffu) message.a = int32_t(0x645a49ffu);
                if (!skip && message.message != 0x1d) __Menu_SendMessage(control, message.message, message.a, message.b);
                break;
            case 0xfffffff7:  // script
                if (skip) {
                    get_dword(&p);
                    get_dword(&p);
                    break;
                }
                script = Script_AddScript(control, get_dword(&p));
                script->flags = get_dword(&p);
                break;
            case 0xfffffff8:  // formato do texto (32 bytes)
                if (!skip) {
                    // Na primeira vez o texto está em 16 bits: vira 8 bits no próprio arquivo.
                    if (std::strcmp(reinterpret_cast<char*>(p + 1), "") == 0) {
                        uint8_t* dst = p;
                        const uint8_t* src = p;
                        for (int i = 0; i < 0x1f; ++i) {
                            *dst++ = *src;
                            src += 2;
                        }
                        *dst = 0;
                    }
                    __Menu_SendMessage(control, 0x23, val(p), 0);
                }
                p += 0x20;
                break;
            case 0xfffffff9: {  // controle
                if (control != nullptr) {
                    Manager_SendMessage(m, 0x51, val(control), 0);
                    Page_SendMessage(m->page, 0x51, val(control), 1);
                }
                std::memset(record, 0, sizeof(record));
                desc.id = get_dword(&p);
                record[0x21] = get_byte(&p);
                const uint8_t kind = get_byte(&p);
                desc.x = int16_t(get_word(&p));
                desc.y = int16_t(get_word(&p));
                desc.w = int16_t(get_word(&p));
                desc.h = int16_t(get_word(&p));
                desc.depth = get_word(&p);
                desc.unknown_0c = get_dword(&p);
                desc.owner = get_word(&p);
                desc.format = nf::p32_ptr<char>(get_dword(&p));
                desc.unknown_08 = get_dword(&p);
                const uint8_t type = record[0x21];
                record[0x20] = type == MCT_SCROLL ? 1 : 0;
                if (type == MCT_LABEL && desc.x < 1 && desc.y < 1 && 0x27f < desc.w && 0x1df < desc.h) {
                    desc.w = 0x200;  // rótulo de tela cheia: a tela do PS2
                    desc.h = 0x1c0;
                } else if (desc.id == 0x10000224u) {
                    desc.x = 0x154;
                    desc.y = 0xc6;
                } else if (type != MCT_WINDOW) {
                    FixupResolution(&desc);
                }
                if (kind == 3 || kind == 0) {
                    M_CONTROL* c = ptr<M_CONTROL>(Manager_SendMessage(m, 0x42, val(&desc), 0));
                    control = c;
                    if (c != nullptr) {
                        for (int i = 0; i < 6; ++i) c->defaults[i] = nullptr;
                        skip = false;
                    }
                } else {
                    skip = true;
                }
                break;
            }
            case 0xfffffffb: {  // peça de componente
                const int16_t x = int16_t(get_word(&p));
                const int16_t y = int16_t(get_word(&p));
                const int16_t w = int16_t(get_word(&p));
                const int16_t hh = int16_t(get_word(&p));
                const uint16_t u = get_word(&p);
                const uint16_t v = get_word(&p);
                const uint16_t uw = get_word(&p);
                const uint16_t vh = get_word(&p);
                const uint32_t colour = get_dword(&p);
                const uint32_t flags = get_dword(&p);
                const float sw = float(get_dword(&p));  // inteiros sem sinal (0 ou 1)
                const float sh = float(get_dword(&p));
                Component_InitInstance(pieces + piece, x, y, w, hh, u, v, uw, vh, flags, colour, sw, sh);
                piece = (piece + 1) & 0xff;
                break;
            }
            case 0xfffffffc: {  // componente
                piece = 0;
                const int16_t count = int16_t(get_word(&p));
                const uint32_t hash = get_dword(&p);
                const int16_t left = int16_t(get_word(&p));
                const int16_t top = int16_t(get_word(&p));
                const int16_t right = int16_t(get_word(&p));
                const int16_t bottom = int16_t(get_word(&p));
                pieces = Component_AddComponent(&set->items[component], left, top, right, bottom, hash, count);
                component = (component + 1) & 0xff;
                break;
            }
            case 0xfffffffd:  // conjunto de componentes
                component = 0;
                set = Component_AddSet(index, get_word(&p));
                break;
            case 0xfffffffe:
                get_dword(&p);
                break;
            default:  // fim sem 0xfffffff2: devolve 0xffffffff
                more = false;
                break;
        }
    } while (more);
    // O cursor: o rótulo é o endereço de "" no PS2 (0x0030d610), recusado por Txt_BindLabel; o sprite
    // nasce escondido (prioridade -1 em CursorInfo).
    CursorInfo.label = 0x0030d610u;
    CursorInfo.hash = 0;
    m->unknown_1cf = 7;
    m->cursor = Menu_CreateSprite(&CursorInfo, 4);
    Manager_SendMessage(m, 0x44, int32_t(page), 1);
    Manager_SendMessage(m, 0x22, 0x10000002, 0);
    return result;
}

// [decomp] 0x0021c2b0 MenuManager_Delete
// Fecha o menu: volta o som, devolve a ação de fechar ao jogador (e despausa o jogo se ela é o Start,
// 0x1e), avisa a página que ela saiu e libera tudo.
void MenuManager_Delete(uint8_t index) {
    M_MANAGER* m = &menu_manager[index];
    if (m->active == 0) return;
    if (m->pause_sounds != 0) {
        SFXUnPause();
        SFXUnPauseAllStreams();
        menu_movie = 0;
    }
    if (m->close_action != 0x28) {
        Menu_SetAction(m->player, m->close_action, 1);
        if (m->close_action == 0x1e) GS_PauseGame(0);
    }
    Manager_SendMessage(m, 0x4d, val(m->page), 0);
    Manager_SendMessage(m, 0x47, 1, 0);
    std::memset(m, 0, sizeof(M_MANAGER));
    psiStopBackgroundMovie();
    Menu_StringKill();
    Input_ClearAllActions(-1);
    ++g_menu_stats.deleted;
}

// [decomp] 0x0021c3d8 MenuManager_Update
// Um tick dos menus (Game_Run): pulsos, mensagens atrasadas, o botão de voltar (0x22: o script 5 do
// controle com o foco ou a mensagem 0x5f), a navegação pelas direções (Menu_FindControl, conforme
// a página: 1 quatro direções, 2 cima/baixo, 3 esquerda/direita) e as páginas (atual, saindo,
// sobreposta). Um gerente só roda se o anterior não está ativo.
void MenuManager_Update() {
    if (MenuManager_GetStatus() == 0) return;
    Menu_AlphaUpdate();
    Menu_UpdateTextFmt();
    Menu_ProcessDelayedMessages();
    if (!psiMovieFinished()) {
        Menu_UpdateMovieCounter();
        if (Menu_InputAction(-1, 0x19, 4) != 0) Menu_StopMovie(0);
    }
    if (GameState.level_file == 0x7000048) PS2PollMemoryCard();
    if (PS2FramesToSkip == 2) {
        if (!psiMovieFinished()) Menu_UpdateMovieCounter();
        Menu_AlphaUpdate();
        Menu_UpdateTextFmt();
        Menu_ProcessDelayedMessages();
    }
    for (int i = 0; i < kMenuManagers; ++i) {
        M_MANAGER* m = &menu_manager[i];
        if (m->active == 0) continue;
        if (0 < i && menu_manager[i - 1].active != 0) continue;
        Menu_MouseUpdate(m);  // só no port: o mouse (engine/menu_mouse.h)
        if (g_port_tick != nullptr) g_port_tick(m);  // só no port: os menus Modern (engine/menu_modern.h)
        M_PAGE* nav_page = m->page;
        M_PAGE* page = m->page;
        if (m->frozen == 0) {
            uint32_t player = 0;
            int16_t p = m->player;
            for (;;) {
                if (p != -1) player = uint8_t(m->player);
                if (Menu_InputAction(int16_t(player), 0x22, 4) == 0) {
                    p = m->player;
                } else {
                    M_PAGE* cur = m->page;
                    bool played = false;
                    if (cur != nullptr) {
                        M_CONTROL* c = cur->player_focus[player];
                        played = Script_PlayDefault(c != nullptr ? c : nf::raw(cur->focus), 0x5b) != 0;
                    }
                    if (played) {
                        page = m->page;
                        break;
                    }
                    if (m->transition == 0) {
                        Menu_PlaySound(8);
                        Manager_SendMessage(m, 0x5f, 0, int32_t(player));
                        return;
                    }
                    p = m->player;
                }
                if (p != -1) break;
                player = (player + 1) & 0xff;
                if (3 < player) break;
                p = m->player;
            }
            page = m->page;
        }
        bool free_nav = true;
        if (page != nullptr) {
            for (int k = 0; k < 4; ++k) {
                if (page->player_focus[k] != nullptr) free_nav = false;
            }
        }
        m->cursor->colour = uint32_t(Menu_AlphaGet() + 0x7f) | 0x201f3400u;
        if (free_nav && m->frozen == 0 && nav_page != nullptr) {
            const uint32_t nav = nav_page->navigation;
            if (nav == 3 || nav == 1) {
                if (Menu_InputAction(m->player, 0x1c, 0xc) != 0) Menu_FindControl(m, 4);
                if (Menu_InputAction(m->player, 0x1d, 0xc) != 0) Menu_FindControl(m, 8);
            }
            if (nav == 2 || nav == 1) {
                if (Menu_InputAction(m->player, 0x1a, 0xc) != 0) Menu_FindControl(m, 1);
                if (Menu_InputAction(m->player, 0x1b, 0xc) != 0) Menu_FindControl(m, 2);
            }
        }
        if (m->page != nullptr) Page_Update(m, m->page, 1);
        if (m->previous != nullptr) Page_Update(m, m->previous, 1);
        if (m->overlay != nullptr) Page_Update(m, m->overlay, 0);
    }
}

void Menu_SetPortTick(MenuPortTick tick) { g_port_tick = tick; }

// [decomp] 0x0021c858 Manager_SendMessage
// Mensagens do gerente: 0x22 foco num controle (por id em a ou pelo ponteiro em b), 0x42 cria um
// controle (NEW_CONTROL em a) na página atual, 0x43 cria a página a, 0x44 mostra a página a (b: 1 não
// empilha, 2 transição, 4 com 0x40000002 guarda a pilha, 8 sem scripts e eventos, 0x10 sem 0x6e), 0x45
// sobreposta, 0x47 libera tudo, 0x56 navegação, 0x5a foco de um jogador, 0x5f voltar, 0x68 congela,
// 0x74 a página atual. O resto é um evento: vai ao tratador do controle a; os de interface tocam o
// script padrão; os não tratados sobem para a página.
int32_t Manager_SendMessage(M_MANAGER* m, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x22: {
            M_PAGE* page = m->page;
            if (page == nullptr) return 0;
            for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
                const bool match = a == 0 ? (b != 0 && c == ptr<M_CONTROL>(b)) : c->id == uint32_t(a);
                if (!match || c->state == 2) continue;
                cursor_to(m, page, c);
                m->page->focus = c;
                return 1;
            }
            return 0;
        }
        case 0x42: {
            NEW_CONTROL* desc = ptr<NEW_CONTROL>(a);
            const uint8_t type = reinterpret_cast<uint8_t*>(desc)[0x21];
            M_CONTROL* owner = reinterpret_cast<M_CONTROL*>(nf::raw(m->page));
            M_CONTROL* c = nullptr;
            switch (type) {
                case MCT_RADIO:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x308, 3));
                    Radio_Init(reinterpret_cast<M_RADIO*>(c), desc, owner);
                    break;
                case MCT_COMBO:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x71c, 3));
                    Combo_Init(reinterpret_cast<M_COMBO*>(c), desc, owner);
                    break;
                case MCT_BUTTON:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x1ac, 3));
                    Button_Init(reinterpret_cast<M_BUTTON*>(c), desc, owner);
                    break;
                case MCT_CHECKBOX:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x1ac, 3));
                    Checkbox_Init(reinterpret_cast<M_CHECKBOX*>(c), desc, owner);
                    break;
                case MCT_LABEL:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x114, 3));
                    Label_Init(reinterpret_cast<M_LABEL*>(c), desc, owner);
                    break;
                case MCT_LIST:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x56c, 3));
                    List_Init(reinterpret_cast<M_LISTBOX*>(c), desc, owner);
                    break;
                case MCT_TEXT:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x9b0, 3));
                    Text_Init(reinterpret_cast<M_TEXTBOX*>(c), desc, owner);
                    break;
                case MCT_SCROLL:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x148, 3));
                    Scroll_Init(reinterpret_cast<M_SCROLL*>(c), desc, owner);
                    break;
                case MCT_SPIN:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x318, 3));
                    Spin_Init(reinterpret_cast<M_SPIN*>(c), desc, owner);
                    break;
                case MCT_WINDOW:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x9c, 3));
                    Window_Init(reinterpret_cast<M_WINDOW*>(c), desc, &m->base);
                    break;
                case MCT_MEMO:
                    c = static_cast<M_CONTROL*>(Menu_Malloc(0x284, 3));
                    Memo_Init(reinterpret_cast<M_MEMOBOX*>(c), desc, owner);
                    break;
                default:
                    return 0;
            }
            ++g_menu_stats.controls;
            if (__Menu_SendMessage(owner, 0x42, val(c), 0) == 0) return 0;
            return val(c);
        }
        case 0x43: {
            M_PAGE* page = static_cast<M_PAGE*>(Menu_Malloc(0xd0, 4));
            Page_Init(page, uint32_t(a), &m->base);
            LinkedList_Add(&m->pages, &page->prev, &page->next);
            __Menu_SendMessage(&m->base, 0x44, a, 0x19);
            ++g_menu_stats.pages;
            return val(page);
        }
        case 0x44: {
            M_PAGE* p = first_page(m);
            Input_ClearAllActions(-1);
            if (p != nullptr) {
                const uint32_t transition = uint32_t(b) & 2;
                const uint32_t push = (uint32_t(b) ^ 1) & 1;
                const uint32_t quiet = uint32_t(b) & 8;
                uint32_t old_id = 0;
                for (; p != nullptr; p = next_page(p)) {
                    if (p->base.id == uint32_t(a)) {
                        Menu_EnableSounds(uint32_t(a));
                        if (push != 0) {
                            if (m->page != nullptr) {
                                P32<M_CONTROL>* e = static_cast<P32<M_CONTROL>*>(Menu_Malloc(8, 8));
                                e[0] = &m->page->base;
                                e[1] = m->page->focus;
                                Stack_Push(&m->stack, val(e));
                            } else {
                                ++g_menu_guards.no_page;  // o original leria o foco de uma página nula
                            }
                        }
                        if (uint32_t(a) == 0x40000002u && (uint32_t(b) & 4) == 0) free_stack(m);
                        if (m->previous != nullptr) {
                            m->previous->base.state = 1;
                            Page_Update(m, m->previous, 0);
                        }
                        if (m->page == nullptr) {
                            m->page = p;
                        } else {
                            old_id = m->page->base.id;
                            m->page->base.state = 1;
                            Page_Update(m, m->page, 0);
                            if (transition == 0) {
                                if (quiet == 0) Manager_SendMessage(m, 0x4d, val(m->page), 0);
                                m->previous = nullptr;
                                m->transition = 0;
                                Manager_SendMessage(m, 0x68, 0, 0);
                                m->page = p;
                            } else {
                                m->transition = 1;
                                m->previous = m->page;
                                Menu_ClearDelayedMessages();
                                m->page = p;
                            }
                        }
                        p->base.depth = 5;
                        m->page->base.state = 0;
                        m->page->ticks = 0;
                        const uint32_t movie = m->page->movie;
                        if (m->movie != movie && transition == 0) {
                            if (movie == 0) {
                                psiStopBackgroundMovie();
                            } else {
                                Menu_PlayMovie(movie, 0, 1, 0, 0, 0);
                            }
                            m->movie = m->page->movie;
                        }
                        if (quiet == 0) {
                            Manager_SendMessage(m, 0x4c, val(m->page), int32_t(old_id));
                            if (Script_PlayDefault(&m->page->base, 0x4c) == 0) {
                                M_CONTROL* w = Window_Get(m->page);
                                Script_Stop(w);
                                if (w != nullptr) {
                                    w->y = -16;
                                    w->x = -64;
                                }
                                Page_Update(m, m->page, 0);
                            }
                        } else if ((uint32_t(b) & 0x10) == 0) {
                            Manager_SendMessage(m, 0x6e, val(m->page), int32_t(old_id));
                        }
                        if (m->previous != nullptr) {
                            if (transition == 0) {
                                if (quiet == 0) {
                                    Manager_SendMessage(m, 0x4d, val(m->previous), b);
                                    if (Script_PlayDefault(&m->previous->base, 0x4d) == 0) {
                                        M_CONTROL* w = Window_Get(m->previous);
                                        Script_Stop(w);
                                        if (w != nullptr) {
                                            w->x = 2000;
                                            w->y = 2000;
                                        }
                                        Page_Update(m, m->previous, 0);
                                    }
                                }
                            } else {
                                m->previous->base.depth = 0xfffb;
                                m->previous->base.state = 2;
                                Page_Update(m, m->previous, 0);
                            }
                        }
                    } else if (p == m->page || p == m->previous) {
                        continue;
                    } else if (p != m->overlay) {
                        // Página escondida: libera os sprites dos controles.
                        p->base.depth = 0;
                        p->base.state = 1;
                        if (quiet == 0) Page_Update(m, p, 0);
                        for (M_CONTROL* c = first_control(p); c != nullptr; c = c->next) {
                            Component_Free(c);
                            if (c->type == MCT_LABEL) {
                                M_LABEL* l = reinterpret_cast<M_LABEL*>(c);
                                if (l->spr != nullptr) {
                                    Menu_DeleteSprite(l->spr, 2);
                                    l->spr = nullptr;
                                }
                            }
                        }
                    }
                }
            }
            Page_Update(m, m->page, 0);
            return 1;
        }
        case 0x45: {
            if (a == 0) {
                m->overlay = nullptr;
                return 1;
            }
            for (M_PAGE* p = first_page(m); p != nullptr; p = next_page(p)) {
                if (p->base.id != uint32_t(a)) continue;
                if (m->overlay != nullptr) {
                    m->overlay->base.state = 1;
                    m->overlay->base.depth = 0;
                    // Peculiaridade do original: o evento de saída fala da página nova.
                    Manager_SendMessage(m, 0x4d, val(p), 0);
                    Script_PlayDefault(&p->base, 0x4d);
                    Page_Update(m, m->overlay, 1);
                }
                m->overlay = p;
                p->base.state = 0;
                m->overlay->base.depth = 0xfff6;
                Manager_SendMessage(m, 0x4c, val(p), 0);
                Script_PlayDefault(&p->base, 0x4c);
                return 1;
            }
            return 1;
        }
        case 0x47: {
            for (M_PAGE* p = first_page(m); p != nullptr;) {
                M_PAGE* next = next_page(p);  // só no port: lido antes de liberar
                __Menu_SendMessage(&p->base, 0x47, 0, 0);
                p = next;
            }
            LinkedList_ReInit(&m->pages);
            Menu_DeleteSprite(m->cursor, 4);
            Component_DestroyAll(m->index);
            m->overlay = nullptr;
            m->page = nullptr;
            m->previous = nullptr;
            free_stack(m);
            return 0;
        }
        case 0x56:
            if (m->page == nullptr) return 0;
            if (a == 1) m->page->navigation = uint32_t(b);
            return 1;
        case 0x5a: {
            if (m->page == nullptr) return 0;
            Input_ClearAllActions(-1);
            M_PAGE* pages[2] = {m->page, m->previous};
            for (M_PAGE* pg : pages) {
                if (pg == nullptr) continue;
                for (M_CONTROL* c = first_control(pg); c != nullptr; c = c->next) {
                    if (a == 0 || c->id != uint32_t(a) || c->tag != uint32_t(b)) continue;
                    P32<M_CONTROL>& slot = pg->player_focus[b];
                    if (slot != nullptr) Manager_SendMessage(m, 0x60, val(slot), 0);
                    slot = c;
                    Manager_SendMessage(m, 0x5c, val(c), 0);
                    return 1;
                }
            }
            return 0;
        }
        case 0x5f: {
            M_PAGE* cur = m->page;
            if (a == 0) {
                int32_t v = b;
                if (cur->player_focus[b] != nullptr) {
                    Manager_SendMessage(m, 0x6b, val(cur->player_focus[b]), val(&v));
                    if (v == -2) return 1;
                }
                Manager_SendMessage(m, 0x6b, val(cur), val(&v));
                if (v == -2) return 1;
            }
            if (Stack_IsEmpty(&m->stack) == 0) {
                P32<M_CONTROL>* e = ptr<P32<M_CONTROL>>(Stack_Top(&m->stack));
                bool played = false;
                if (cur != nullptr) played = Script_PlayDefault(&cur->base, 99) != 0;
                if (!played) {
                    __Menu_SendMessage(&m->base, 0x44, int32_t(e[0]->id), m->transition == 0 ? 5 : 0xd);
                    __Menu_SendMessage(&m->base, 0x22, 0, val(e[1]));
                }
                Stack_Pop(&m->stack);
                void* entry = e;
                Menu_Free(&entry, 8);
                return 1;
            }
            Manager_SendMessage(m, 99, val(cur), int32_t(cur->base.id));
            return 1;
        }
        case 0x68:
            m->frozen = uint8_t(a);
            Input_ClearAllActions(-1);
            return 1;
        case 0x74: {
            M_PAGE* p = m->transition == 0 ? m->page : m->previous;
            return p != nullptr ? int32_t(p->base.id) : 0;
        }
        default: {
            if (m->page == nullptr) return 0;
            const int32_t handled = Manager_HandleMessage(m->index, ptr<M_CONTROL>(a), message, a, b);
            switch (message) {
                case 0x49:
                case 0x4a:
                case 0x4b:
                case 0x4e:
                case 0x4f:
                case 0x52:
                case 0x53: {
                    M_CONTROL* c = ptr<M_CONTROL>(a);
                    if (c->type == MCT_PAGE) return 1;
                    return Script_PlayDefault(c, message);
                }
                default:
                    if (handled != 0) return 0;
                    return __Menu_SendMessage(&m->page->base, message, a, 1);
            }
        }
    }
}

// [decomp] 0x0021d508 MenuManager_Monitor
// Fecha o menu quando o jogador aperta a ação de fechar dele; fora do menu principal, a cada 10
// ticks confere os controles: se o de um jogador sumiu, troca os menus pelo aviso (página 0x4000002f
// do menu 0x80000002) e, quando ele volta, abre o menu de pausa (ou o de fim de missão).
void MenuManager_Monitor() {
    for (int i = 0; i < kMenuManagers; ++i) {
        M_MANAGER* m = &menu_manager[i];
        if (m->active == 0) continue;
        if (i != 0 && menu_manager[i - 1].active != 0) continue;
        if (m->close_action != 0x28 && Menu_InputAction(m->player, int32_t(m->close_action), 4) != 0 &&
            m->base.type == MCT_MANAGER) {
            MenuManager_Delete(uint8_t(i));
        }
    }
    const uint32_t state = GameFlow_GetState();
    if (!(state < 7 || (8 < state && (0xe < state || state < 0xc)))) {
        counter_152 = 10;
        return;
    }
    if (GameState.level_file == 0x7000048) return;
    if (!psiMovieFinished()) {
        counter_152 = 10;
        return;
    }
    if (MenuManager_GetStatus() == 0 && ScriptCam != 0) {
        counter_152 = 10;
        return;
    }
    if (0 < --counter_152) return;
    const uint32_t players = mp_word(0x1ac);
    const auto connected = [](uint32_t i) { return PlayerSetting[i].connected != 0; };
    for (uint32_t i = 0; i < players; ++i) {
        if (player_index != -1) {
            if (!connected(i) && int32_t(i) < player_index) player_index = int16_t(i);
            if (player_index != -1 && player_index != int16_t(i)) continue;
        }
        switch (controller_state) {
            case 0: {
                int16_t who;
                if (was_multitap_inserted_150 == 0 || psiMultitapPresent() != 0) {
                    who = int16_t(i);
                    if (connected(i)) break;
                } else {
                    was_multitap_inserted_150 = 1;
                    player_index = -1;
                    who = player_index;
                }
                player_index = who;
                mf_menu = MenuManager_GetStatus() == 4 ? 1 : 0;
                if (mf_menu == 0 && GameState.unknown_24 != 0) break;
                if (psiMultitapPresent() == 0 && players < 3) {
                    std::snprintf(buf_155, sizeof(buf_155), Txt_BindLabel(0x3c9, 0), ps2_normal_154 + i * 2);
                } else {
                    std::snprintf(buf_155, sizeof(buf_155), Txt_BindLabel(0x3c9, 0), ps2_multitap_153 + i * 4);
                }
                controller_state = 1;
            }
                [[fallthrough]];
            case 1:
                MenuManager_Delete(0);
                MenuManager_Delete(1);
                MenuManager_Delete(2);
                MenuManager_Delete(3);
                GS_PauseGame(1);
                MenuManager_Create(0x80000002u, 0x4000002f, 0, 0, 0x28, 1, 2);
                Manager_SendMessage(&menu_manager[0], 0x68, 1, 0);
                __Menu_Send(0, 0x1000011f, 0x18, val(buf_155), 0);
                if ((was_multitap_inserted_150 == 0 || psiMultitapPresent() != 0) &&
                    (psiMultitapPresent() != 0 || players < 3)) {
                    controller_state = 2;
                } else {
                    controller_state = 4;
                }
                break;
            case 2: {
                const int32_t who = player_index;
                if (was_multitap_inserted_150 == 0 || psiMultitapPresent() == 0) {
                    std::snprintf(buf_155, sizeof(buf_155), Txt_BindLabel(0x3c9, 0), ps2_normal_154 + who * 2);
                } else {
                    std::snprintf(buf_155, sizeof(buf_155), Txt_BindLabel(0x3c9, 0), ps2_multitap_153 + who * 4);
                }
                if (was_multitap_inserted_150 != 0 && psiMultitapPresent() == 0) {
                    controller_state = 4;
                    player_index = -1;
                    break;
                }
                __Menu_Send(0, 0x1000011f, 0x18, val(buf_155), 0);
                M_CONTROL* box = ptr<M_CONTROL>(__Menu_Send(0, 0x1000011e, 0x39, 0, 0));
                M_MEMOBOX* text = ptr<M_MEMOBOX>(__Menu_Send(0, 0x1000011f, 0x39, 0, 0));
                __Menu_SendMessage(&text->base, 0x66, 0, 0);
                __Menu_SendMessage(&text->base, 0x75, 100, 0);
                const uint32_t lines = uint32_t(__Menu_SendMessage(&text->base, 0x65, 0, 0));
                const uint32_t options = uint32_t(__Menu_Send(0, 0x10000120, 0x33, 0, 0));
                const int16_t hh = int16_t((lines & 0xff) * (uint16_t(int32_t(text->line_height)) & 0xff) + (options & 0xff) * 0x15);
                const int16_t height = int16_t((options & 0xff) == 0 ? hh + 0x1c : hh + 0x36);
                box->h = height;
                const int16_t y = int16_t(int32_t((448.0f - float(height)) * 0.5f));
                box->y = y;
                text->base.y = int16_t(y + 10);
                if (connected(uint32_t(who))) {
                    bool found = false;
                    for (uint32_t k = 0; k < players; ++k) {
                        if (!connected(k)) {
                            counter_152 = 0x3c;
                            player_index = int16_t(k);
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        was_multitap_inserted_150 = uint8_t(psiMultitapPresent());
                        return;
                    }
                    controller_state = 3;
                }
                break;
            }
            case 4: {
                M_CONTROL* box = ptr<M_CONTROL>(__Menu_Send(0, 0x1000011e, 0x39, 0, 0));
                M_MEMOBOX* text = ptr<M_MEMOBOX>(__Menu_Send(0, 0x1000011f, 0x39, 0, 0));
                __Menu_SendMessage(&text->base, 0x66, 0, 0);
                __Menu_SendMessage(&text->base, 0x75, 100, 0);
                const uint32_t lines = uint32_t(__Menu_SendMessage(&text->base, 0x65, 0, 0));
                const uint32_t options = uint32_t(__Menu_Send(0, 0x10000120, 0x33, 0, 0));
                const int16_t hh = int16_t((lines & 0xff) * (uint16_t(int32_t(text->line_height)) & 0xff) + (options & 0xff) * 0x15);
                const int16_t height = int16_t((options & 0xff) == 0 ? hh + 0x1c : hh + 0x36);
                box->h = height;
                const int16_t y = int16_t(int32_t((448.0f - float(height)) * 0.5f));
                box->y = y;
                text->base.y = int16_t(y + 10);
                bool wait = false;
                if (players < 3) {
                    std::snprintf(buf_155, sizeof(buf_155), Txt_BindLabel(0x3c9, 0), "1");
                    __Menu_Send(0, 0x1000011f, 0x18, val(buf_155), 0);
                    ++frames_156;
                    if (frames_156 < 0x3c) {
                        wait = psiMultitapPresent() == 0;
                    } else {
                        wait = psiMultitapPresent() == 0 && !connected(0);
                    }
                } else {
                    std::snprintf(buf_155, sizeof(buf_155), "%s", Txt_BindLabel(0x10000ba, 0));
                    __Menu_Send(0, 0x1000011f, 0x18, val(buf_155), 0);
                    wait = psiMultitapPresent() == 0;
                }
                if (wait) break;
                frames_156 = 0;
                for (uint32_t k = 0; k < players; ++k) {
                    if (!connected(k)) {
                        counter_152 = 0x3c;
                        controller_state = 3;
                        was_multitap_inserted_150 = uint8_t(psiMultitapPresent());
                        return;
                    }
                }
                player_index = 0;
                controller_state = 3;
            }
                [[fallthrough]];
            case 3:
                GS_PauseGame(1);
                MenuManager_Delete(0);
                if (player_index == -1) player_index = 0;
                if (mf_menu == 0) {
                    MenuManager_Create(0x80000002u, 0x4000004b, player_index, 0, 0x1e, 1, 3);
                } else {
                    MenuManager_Create(0x80000004u, 0x40000042, player_index, 0, 0x28, 1, 4);
                }
                controller_state = 0;
                player_index = -1;
                break;
            default:
                break;
        }
    }
    was_multitap_inserted_150 = uint8_t(psiMultitapPresent());
}

// [decomp] 0x0021de88 MenuManager_Load
// Guarda o arquivo de menu carregado (item tipo 8; MenuManager_Create o lê).
void MenuManager_Load(uint32_t hash, uint8_t* data) {
    file_start = data;
    g_menu_file = data != nullptr ? hash : 0;  // só no port: para os relatórios
}

// [decomp] 0x0021de90 MenuManager_Exists
uint8_t MenuManager_Exists(uint8_t index) { return menu_manager[index].active; }

// [decomp] 0x0021deb0 MenuManager_GetStatus
// O maior status entre os gerentes (0: nenhum menu).
uint32_t MenuManager_GetStatus() {
    uint32_t s = 0;
    for (const M_MANAGER& m : menu_manager) {
        if (s < m.status) s = m.status;
    }
    return s;
}

// [decomp] 0x0021def0 MenuManager_Enable
void MenuManager_Enable(int8_t on, uint8_t index) { menu_manager[index].active = uint8_t(on); }

// [decomp] 0x0021df10 Manager_HandleMessage
int32_t Manager_HandleMessage(uint8_t index, M_CONTROL* c, uint32_t message, int32_t a, int32_t b) {
    return Handler_HandleMessage(index, c, message, a, b);
}

// [decomp] 0x0021df30 Menu_PlayerHasQuitOrRestarted
// 1 depois que o jogador escolheu sair ou recomeçar no menu de pausa (Player_HandleDeath espera).
uint32_t Menu_PlayerHasQuitOrRestarted() { return user_restarted_or_quit; }

// [decomp] 0x002029e0 Handler_HandleMessage
// Entrega o evento ao tratador do identificador do controle (P_* das páginas, C_* dos controles).
// O original é uma árvore de comparações sobre os 103 identificadores com tratador; o port procura
// numa tabela (Menu_FindHandler). Sem tratador: 0 (não tratado).
int32_t Handler_HandleMessage(uint8_t index, M_CONTROL* c, uint32_t message, int32_t a, int32_t b) {
    if (c == nullptr) {
        ++g_menu_guards.no_focus;  // o original leria o identificador em 0x18
        return 0;
    }
    const MenuHandlerEntry* e = Menu_FindHandler(c->id);
    if (e == nullptr) return 0;
    if (e->handler == nullptr) {
        pending_call(e->name);  // tratador do menu principal ainda não portado
        ++g_menu_guards.handler_stub;
        return 0;
    }
    return e->handler(index, c, c->id, message, a, b);
}

// ---- páginas ----

// [decomp] 0x0021f1c0 Page_Init
void Page_Init(M_PAGE* page, uint32_t id, M_CONTROL* owner) {
    page->base.id = id;
    page->base.depth = 0;
    page->base.tag = 0;
    page->base.x = 0;
    page->base.y = 0;
    page->base.state = 0;
    page->focus = nullptr;
    page->movie = 0;
    page->ticks = 0;
    page->input_delay = 0;
    page->player_focus[0] = nullptr;
    page->player_focus[1] = nullptr;
    page->player_focus[2] = nullptr;
    page->player_focus[3] = nullptr;
    page->base.type = MCT_PAGE;
    page->base.owner = owner;
    page->base.w = 0x280;
    page->base.h = 0x1e0;
    page->navigation = 1;
    LinkedList_Init(&page->controls, 0);
    Script_PrepareScripting(&page->base);
}

// [decomp] 0x0021ec68 Page_SendMessage
// 0x42 acrescenta um controle, 0x47 libera tudo, 0x48 o cursor está no foco, 0x6f atraso da entrada.
// Os eventos tocam o script padrão do controle (b != 0: vindo do gerente) e sobem ao gerente.
int32_t Page_SendMessage(M_PAGE* page, uint32_t message, int32_t a, int32_t b) {
    M_PAGE* self = page;
    switch (message) {
        case 0x42: {
            M_CONTROL* c = nf::p32_ptr<M_CONTROL>(uint32_t(a));
            LinkedList_Add(&page->controls, &c->prev, c);
            return 1;
        }
        case 0x47: {
            if (page->controls.count != 0) {
                for (M_CONTROL* c = first_control(page); c != nullptr;) {
                    M_CONTROL* next = c->next;
                    __Menu_SendMessage(c, 0x47, 0, 0);
                    c = next;
                }
            }
            void* p = self;
            Menu_Free(&p, 4);
            return 1;
        }
        case 0x48:
            return Menu_CursorOverMe(reinterpret_cast<M_MANAGER*>(nf::raw(page->base.owner)), page->focus);
        case 0x49:
        case 0x4e:
        case 0x4f:
        case 0x51:
            Script_PlayDefault(nf::p32_ptr<M_CONTROL>(uint32_t(a)), message);
            break;
        case 0x4a:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x61:
            break;
        case 0x4b:
        case 0x5d:
        case 0x5e:
        case 0x6d:
            if (b == 0) {
                __Menu_SendMessage(page->base.owner, message, val(page), a);
            }
            Script_PlayDefault(nf::p32_ptr<M_CONTROL>(uint32_t(a)), message);
            break;
        case 0x6f:
            page->input_delay = uint32_t(a);
            return 0;
        default:
            return 0;
    }
    if (b != 0) return 0;
    return __Menu_SendMessage(self->base.owner, message, a, 0);
}

// [decomp] 0x0021edc8 Page_Update
// Um tick da página: o foco segue o cursor (Menu_GetControl; ao mudar, eventos 0x4f/0x4e e o som),
// a ajuda, e cada controle (do último para o primeiro): o script, depois o update do tipo com o
// jogador e o foco do multijogador; o controle fixo (+0x10) e o evento 0x50 da página.
void Page_Update(M_MANAGER* m, M_PAGE* page, uint32_t input) {
    if (page == nullptr || m->cursor == nullptr) return;
    m->input = uint8_t(input);
    const uint32_t ticks = page->ticks;
    page->ticks = ticks + 1;
    if (PS2FramesToSkip == 2) page->ticks = ticks + 2;
    M_CONTROL* focus = Menu_GetControl(m, 0);
    M_CONTROL* old = page->focus;
    if (focus != old) {
        page->focus = focus;
        focus->focus_ticks = 0;
        if (m->input != 0) {
            Menu_AlphaReset();
            __Menu_SendMessage(page->base.owner, 0x4f, val(old), 0);
            __Menu_SendMessage(page->base.owner, 0x4e, val(focus), 0);
            if (Menu_GetPage(old) == Menu_GetPage(focus)) {
                Menu_GetPage(focus);
                Menu_PlaySound(7);
            }
        }
    }
    if (page->ticks < 3) Page_FindHelpControl(m->transition == 0 ? page : nullptr);
    Page_SetHelpText(page);
    M_CONTROL* c = static_cast<M_CONTROL*>(page->controls.tail != nullptr
                                               ? static_cast<void*>(static_cast<uint8_t*>(page->controls.tail) -
                                                                    page->controls.offset * 4)
                                               : nullptr);
    if (c != nullptr && m->active != 0) {
        for (;;) {
            if (c->id == page->focus->id) c->focus_ticks = page->focus->focus_ticks;
            Script_RunFrame(m, c);
            if (PS2FramesToSkip == 2) Script_RunFrame(m, c);
            if (((c->state ^ 1) & 1) != 0) {
                const int16_t player = m->player;
                M_CONTROL* saved = page->focus;
                const uint8_t saved_input = m->input;
                for (uint32_t k = 0; k < 4; ++k) {
                    if (page->player_focus[k] == c && page == m->page) {
                        m->player = int16_t(k);
                        m->cursor->x = int16_t(c->x + 1);
                        m->cursor->y = int16_t(c->y + 1);
                        m->cursor->tex_w = 1;
                        m->cursor->tex_h = 1;
                        page->focus = c;
                        break;
                    }
                }
                ++c->focus_ticks;
                if (page->ticks < page->input_delay) m->input = 0;
                Menu_Update(m, page, c);
                m->player = player;
                page->focus = saved;
                m->input = saved_input;
            }
            c = c->prev;
            if (c == nullptr || m->active == 0) break;
        }
    }
    M_CONTROL* f = page->focus;
    if (f != nullptr && m->cursor != nullptr && m->page == page) {
        m->cursor->x = int16_t(page->base.x + f->x + (f->w >> 1));
        m->cursor->y = int16_t(m->page->base.y + page->focus->y + (page->focus->h >> 1));
        m->cursor->w = page->focus->w;
        m->cursor->h = page->focus->h;
        m->cursor->priority = int8_t(page->focus->depth + 1);
    }
    if (page->base.unknown_10 != 0) {
        M_CONTROL* saved = page->focus;
        M_CONTROL* fixed = ptr<M_CONTROL>(__Menu_Send(m->index, page->base.unknown_10, 0x39, 0, 0));
        page->focus = fixed;
        if (fixed != nullptr) Menu_Update(m, page, fixed);
        page->focus = saved;
    }
    if (m->transition != 0 && m->input != 0) {
        if (m->previous == nullptr) return;
        __Menu_SendMessage(m->previous->base.owner, 0x50, val(page), 0);
        return;
    }
    if (m->page == page && m->input != 0) __Menu_SendMessage(page->base.owner, 0x50, val(page), 0);
}

// [decomp] 0x0021f250 Page_FindHelpControl
// O memo de ajuda da página (id 0x10000225), ou nenhum.
void Page_FindHelpControl(M_PAGE* page) {
    if (page != nullptr) {
        for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
            if (c->type == MCT_MEMO && c->id == 0x10000225u) {
                help_control = c;
                return;
            }
        }
    }
    help_control = nullptr;
}

// [decomp] 0x0021f2b8 Page_SetHelpText
// Põe no memo de ajuda o texto do controle com o foco (help_table: id, valor da mensagem 0x35 ou
// 0xf0f0f0f0 para qualquer um, rótulo); sem entrada, texto vazio.
void Page_SetHelpText(M_PAGE* page) {
    M_CONTROL* f = page->focus;
    if (help_control == nullptr || f == nullptr) return;
    const uint32_t value = uint32_t(__Menu_SendMessage(f, 0x35, 0, 0));
    for (int i = 0; i < kHelpEntries; ++i) {
        const uint32_t* e = &help_table[i * 3];
        if (e[0] != f->id) continue;
        if (e[1] == value || e[1] == 0xf0f0f0f0u) {
            __Menu_SendMessage(help_control, 0x18, val(Txt_BindLabel(e[2], 0)), 0);
            return;
        }
    }
    __Menu_SendMessage(help_control, 0x18, 0, 0);
}

// ---- janela ----

// [decomp] 0x00224110 Window_Init
void Window_Init(M_WINDOW* w, NEW_CONTROL* desc, M_CONTROL* owner) {
    w->base.state = 0;
    w->base.type = MCT_WINDOW;
    w->base.component = 0;
    w->base.owner = owner;
    w->base.id = desc->id;
    w->base.tag = desc->unknown_0c;
    w->base.state = 0;
    w->base.depth = desc->depth;
    w->base.sprites = nullptr;
    w->base.flags = 0;
    w->page = reinterpret_cast<M_MANAGER*>(owner)->page;
    w->base.x = desc->x;
    w->base.y = desc->y;
    w->base.w = desc->w;
    w->base.h = desc->h;
    Script_PrepareScripting(&w->base);
}

// [decomp] 0x00224190 Window_SendMessage
int32_t Window_SendMessage(M_WINDOW* w, uint32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x14:
            return Script_ExitLoop(&w->base);
        case 0x16:
            return Script_Play(&w->base, uint32_t(a), int8_t(b));
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            reinterpret_cast<P32<M_SCRIPT>*>(w->base.defaults)[a] = Script_Select(&w->base, uint32_t(b));
            return 1;
        case 0x39:
            return val(w);
        case 0x47: {
            Script_DelAllScripts(&w->base);
            void* p = w;
            Menu_Free(&p, 3);
            return 0;
        }
        default:
            return 0;
    }
}

// [decomp] 0x00224270 Window_Update
void Window_Update(M_MANAGER*, M_PAGE*, M_WINDOW*) {}

// [decomp] 0x00224278 Window_Get
// O primeiro controle da página (a janela).
M_CONTROL* Window_Get(M_PAGE* page) { return first_control(page); }

// ---- processos ----

// [decomp] 0x0021f780 Process_Init
void Process_Init(M_CONTROL* c) {
    if (c != nullptr) LinkedList_Init(&c->processes, 0);
}

// [decomp] 0x0021f7a8 Process_Create
// Começa um processo de `duration` ticks com a mensagem do script; 0xe1 guarda a cor atual (0x30).
void Process_Create(M_CONTROL* c, M_MESSAGE* msg, uint32_t duration, uint32_t) {
    if (c == nullptr || msg == nullptr) return;
    M_PROCESS* p = static_cast<M_PROCESS*>(Menu_Malloc(0x24, 0xb));
    p->duration = duration;
    p->frame = 0;
    p->message = msg->message;
    p->colour = uint32_t(msg->a);
    p->b = msg->b;
    if (msg->message == 0xe1) {
        p->start = uint32_t(__Menu_SendMessage(c, 0x30, 0, 0));
        p->unknown_1c = 0;
    }
    LinkedList_Add(&c->processes, &p->prev, p);
}

// [decomp] 0x0021f860 Process_Destroy
void Process_Destroy(M_CONTROL* c) {
    for (M_PROCESS* p = static_cast<M_PROCESS*>(ll_first(c->processes)); p != nullptr;) {
        M_PROCESS* next = p->next;  // só no port: lido antes de liberar
        LinkedList_Remove(&c->processes, &p->prev, p);
        void* q = p;
        Menu_Free(&q, 0xb);
        p = next;
    }
}

// [decomp] 0x0021f380 Process_RunFrame
// Um tick dos processos do controle (e do rótulo de um botão, caixa ou rádio, da lista de um combo,
// do rótulo de um spin): o 0xe1 leva a cor (0x1a) da inicial à final em `duration` ticks, byte a byte.
void Process_RunFrame(M_CONTROL* c) {
    switch (c->type) {
        case MCT_BUTTON:
        case MCT_CHECKBOX:
        case MCT_RADIO:
            Process_RunFrame(reinterpret_cast<M_CONTROL*>(reinterpret_cast<uint8_t*>(c) + 0x94));
            break;
        case MCT_COMBO:
            Process_RunFrame(reinterpret_cast<M_CONTROL*>(reinterpret_cast<uint8_t*>(c) + 0x600));
            break;
        case MCT_SPIN:
            Process_RunFrame(reinterpret_cast<M_CONTROL*>(reinterpret_cast<uint8_t*>(c) + 0x1dc));
            break;
        default:
            break;
    }
    for (M_PROCESS* p = static_cast<M_PROCESS*>(ll_first(c->processes)); p != nullptr;) {
        M_PROCESS* next = p->next;
        if (p->message == 0xe1) {
            const uint8_t* to = reinterpret_cast<const uint8_t*>(&p->colour);
            const uint8_t* from = reinterpret_cast<const uint8_t*>(&p->start);
            const float frame = float(p->frame);
            const float duration = float(p->duration);
            uint32_t out = 0;
            for (int k = 0; k < 4; ++k) {
                const int32_t v = int32_t(float(from[k]) + (float(int32_t(to[k]) - int32_t(from[k])) / duration) * frame);
                out |= (uint32_t(v) & 0xff) << (k * 8);
            }
            __Menu_SendMessage(c, 0x1a, int32_t(out), 0 < p->b ? 1 : 0);
        }
        const uint32_t f = p->frame + 1;
        p->frame = f;
        if (p->duration <= f) {
            __Menu_SendMessage(c, 0x1a, int32_t(p->colour), 0 < p->b ? 1 : 0);
            LinkedList_Remove(&c->processes, &p->prev, p);
            void* q = p;
            Menu_Free(&q, 0xb);
        }
        p = next;
    }
}
