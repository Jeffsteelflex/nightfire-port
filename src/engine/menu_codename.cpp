// O menu principal: os codinomes (listar, escolher, criar com o teclado da tela, apagar, gravar) e a caixa do
// cartão de memória (PS2_DoCardFlow) que acompanha cada operação. Ver menu_frontend.h; o cartão em
// platform/psi_card.h e o arquivo do perfil em engine/loadsave.h.
//
// Divergências (comentadas nas funções):
// - PS2_DoCardFlow: sem os controles da caixa 0x4000002a (um arquivo de menu sem ela) não redimensiona (o
//   original escreveria por nulo).
// - C_KEYBOARD_Handler: "Del" com o nome vazio não apaga nada (o original zeraria o byte antes do buffer).
// - C_KEYPAD_Handler: os algarismos ficam dentro de GlobalVars.
// - Menu_MapDefaultCodename: só os dois perfis padrão (o original leria além de def_codename); no fim, as opções que o
//   nightfire.ini tem valem por cima das do perfil padrão (platform/game_options.h, regra 3).

#include "engine/menu_frontend.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/game.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/loadsave.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/sort.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/psi.h"
#include "platform/psi_card.h"

uint8_t cheat_mode = 0;
uint8_t cn_modified_flag = 0;
uint8_t del_when_keypressed = 0;
char codename_entry[0x10];
uint32_t enum_state = 0;

namespace {

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }
template <class T>
T* ptr(int32_t v) {
    return nf::p32_ptr<T>(uint32_t(v));
}
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }
int32_t txt(uint32_t label) { return val(Txt_BindLabel(label, 0)); }
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

char kSpace[] = " ";  // 0x0030d380

// PS2_DoCardFlow
uint32_t frames_125 = 0;
uint8_t dialog_open_124 = 0;
char* message_123 = nullptr;
char buffer_122[0x200];
const char* const kSlotType[2] = {"1", "1-A"};  // slot_type.121: o slot 1, ou o 1-A com o multitap
// P_PS2MEMCARDINIT_Handler
uint32_t state_117 = 0;
// Menu_UpdateMessageBox
uint32_t state_159 = 0;
uint8_t recurse_160 = 0;
uint32_t del_state_161 = 0;
// P_CNSELECT_Handler
uint8_t closing_460 = 0;
uint8_t select_def_codename_461 = 0;
// P_CNMENU_Handler
int32_t screen_adjust_x_468 = 0;
int32_t screen_adjust_y_469 = 0;
char buf_470[0x80];
// P_CNNAME_Handler: os rótulos das teclas 100-103 (Ä, Â, Æ, À; a página americana não as tem).
char buf_477[8];
// C_KEYPAD_Handler
char buf_250[0x10];

// As respostas que PS2CardFlow.options oferece: (bit, rótulo).
const uint32_t kCardOptions[6][2] = {{4, 0x3cd}, {1, 0x3ce}, {2, 0x3cf}, {8, 0x3d0}, {0x10, 0x3d1}, {0x20, 0x3d2}};

}  // namespace

// ---- o cartão ----

// [decomp] 0x00218538 P_PS2MEMCARDINIT_Handler
// O cartão na partida (0x40000048): psiDoBootupCheck e a caixa das mensagens; no fim, os filmes de abertura.
int32_t P_PS2MEMCARDINIT_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        psiDoBootupCheck();
    } else if (message == 0x50) {
        state_117 = psiSavingData();
        PS2_DoCardFlow(mgr(index), state_117, &menu_ls());
        if (menu_ls().done != 0) Manager_SendMessage(mgr(index), 0x44, 0x40000032, 1);
    }
    return 1;
}

// [decomp] 0x002185e8 C_LBERROPTIONS_Handler
// As respostas da caixa do cartão (OK, sim, cancelar...): o valor da linha vai para PS2CardFlow.
int32_t C_LBERROPTIONS_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        PS2CardFlow.answer = uint8_t(__Menu_SendMessage(c, 0x35, 0, 0));
        PS2CardFlow.answered = 1;
    }
    return 1;
}

// [decomp] 0x00217bd8 PS2_DoCardFlow
// A caixa do cartão (página 0x4000002a, por cima da atual) durante uma operação: a mensagem de PS2CardFlow
// (com o slot "1"), as respostas oferecidas e o menu congelado enquanto não há o que escolher. No fim (estado
// acima de 3: 4 parou, 7 deu certo, 8 falhou) espera a caixa ter ficado 60 ticks (fora das páginas do nome, do
// codinome e do resultado), fecha a caixa (0x6a à página de baixo, voltar) e mostra ls.next_page; numa falha a
// lista de nomes fica velha e a página do nome volta.
void PS2_DoCardFlow(M_MANAGER* m, uint32_t state, MENU_LS* l) {
    ++frames_125;
    const uint8_t index = m->index;
    if (3 < state) {
        if (state < 9 && 6 < state && m->previous != nullptr) {
            const uint32_t id = m->previous->base.id;
            if (id != 0x40000020) {
                const uint32_t other = id < 0x40000021 ? 0x4000001d : 0x40000036;
                if (id != other && frames_125 < 0x3c && __Menu_Send(index, 0x100000fc, 0x33, 0, 0) == 0) return;
            }
        }
        l->active = 0;
        if (state == 8) l->stale = 1;
        if (dialog_open_124 != 0) {
            if (m->previous != nullptr) Manager_SendMessage(m, 0x6a, val(m->previous), int32_t(state));
            Manager_SendMessage(m, 0x5f, 0, 0);
        }
        if (state == 8) {
            Menu_ClearDelayedMessages();
            l->next_page = 0;
            if (m->page != nullptr && m->page->base.id == 0x40000020 && l->keep == 0) Manager_SendMessage(m, 0x5f, 0, 0);
        } else if (l->next_page != 0) {
            Manager_SendMessage(m, 0x44, int32_t(l->next_page), int32_t(l->next_flags));
        }
        dialog_open_124 = 0;
        message_123 = nullptr;
        frames_125 = 0;
        l->done = 1;
        LS_FlushStates();
        Menu_FlushEnum();
        Manager_SendMessage(m, 0x68, 0, 0);
        return;
    }
    const char* slot = kSlotType[psiMultitapPresent() & 1];
    uint32_t label = 0;
    bool with_slot = true;
    switch (PS2CardFlow.message) {
        case 0: message_123 = nullptr; break;
        case 1: label = 0x26b; break;
        case 2:
            label = 0x269;
            l->keep = 1;
            break;
        case 3:
            std::snprintf(buffer_122, sizeof(buffer_122), Txt_BindLabel(0x26a, 0), slot, 0x52);
            message_123 = buffer_122;
            break;
        case 4:
            label = 0x27d;
            with_slot = false;
            break;
        case 5: label = 0x26d; break;
        case 6:
        case 7:
            std::snprintf(buffer_122, sizeof(buffer_122), Txt_BindLabel(PS2CardFlow.message == 6 ? 0x278 : 0x273, 0), slot);
            message_123 = buffer_122;
            l->next_page = 0;
            break;
        case 8: label = 0x275; break;
        case 9: label = 0x26c; break;
        case 10:
            l->keep = 0;
            label = 0x27a;
            break;
        case 0xb: label = 0x277; break;
        case 0xc: label = 0x3e6; break;
        case 0xd:
            label = 0x3e5;
            with_slot = false;
            break;
        case 0xe: label = 0x3e7; break;
        case 0xf:
            // Em alemão a ordem dos argumentos troca.
            if (LanguageToLoad == 2) {
                std::snprintf(buffer_122, sizeof(buffer_122), Txt_BindLabel(0x268, 0), 0x52, slot);
            } else {
                std::snprintf(buffer_122, sizeof(buffer_122), Txt_BindLabel(0x268, 0), slot, 0x52);
            }
            message_123 = buffer_122;
            break;
        case 0x10:
            std::snprintf(buffer_122, sizeof(buffer_122), Txt_BindLabel(0x267, 0), slot, 0x52);
            message_123 = buffer_122;
            break;
        case 0x11:
            label = 0x27c;
            with_slot = false;
            break;
        default: break;
    }
    if (label != 0) {
        if (with_slot) {
            const char* f = Txt_BindLabel(label, 0);
            std::snprintf(buffer_122, sizeof(buffer_122), f != nullptr ? f : "", slot);
            message_123 = buffer_122;
        } else {
            message_123 = Txt_BindLabel(label, 0);
        }
    }
    if (message_123 == nullptr) {
        __Menu_Send(index, 0x100000fb, 0x2b, 1, 0);
        __Menu_Send(index, 0x100000fd, 0x2b, 1, 0);
    } else if (dialog_open_124 == 0) {
        Manager_SendMessage(m, 0x44, 0x4000002a, 2);
        __Menu_Send(index, 0x100000fb, 0x18, val(message_123), 0);
        __Menu_Send(index, 0x100000fc, 0x17, 0, 0);
        __Menu_Send(index, 0x100000fc, 0x2b, 1, 0);
        frames_125 = 0;
        dialog_open_124 = 1;
    } else {
        __Menu_Send(index, 0x100000fb, 0x2b, 2, 0);
        __Menu_Send(index, 0x100000fb, 0x18, val(message_123), 0);
        __Menu_Send(index, 0x100000fd, 0x2b, 2, 0);
    }
    if (PS2CardFlow.rebuild != 0) {
        __Menu_Send(index, 0x100000fc, 0x2b, 0, 0);
        __Menu_Send(index, 0x100000fc, 0x17, 0, 0);
        for (const auto& o : kCardOptions) {
            if ((PS2CardFlow.options & o[0]) != 0) __Menu_Send(index, 0x100000fc, 0x10, txt(o[1]), int32_t(o[0]));
        }
        __Menu_Send(index, 0x100000fc, 0x59, 0, 3);
        PS2CardFlow.rebuild = 0;
    }
    const uint32_t options = uint32_t(__Menu_Send(index, 0x100000fc, 0x33, 0, 0));
    if (options == 0 || PS2CardFlow.answered != 0) {
        Manager_SendMessage(m, 0x68, 1, 0);
        __Menu_Send(index, 0x100000fc, 0x70, 0, 0);
        __Menu_Send(index, 0x10000244, 0x2b, 1, 0);
    } else {
        Manager_SendMessage(m, 0x68, 0, 0);
        __Menu_Send(index, 0x100000fc, 0x70, options < 2 ? 0 : 1, 0);
        __Menu_Send(index, 0x10000244, 0x2b, 2, 0);
    }
    M_BUTTON* box = ptr<M_BUTTON>(__Menu_Send(index, 0x100000fd, 0x39, 0, 0));
    M_MEMOBOX* memo = ptr<M_MEMOBOX>(__Menu_Send(index, 0x100000fb, 0x39, 0, 0));
    M_LISTBOX* list = ptr<M_LISTBOX>(__Menu_Send(index, 0x100000fc, 0x39, 0, 0));
    if (box == nullptr || memo == nullptr || list == nullptr) return;  // só no port: o original escreveria por nulo
    __Menu_SendMessage(&memo->base, 0x66, 0, 0);
    const uint32_t lines = uint32_t(__Menu_SendMessage(&memo->base, 0x65, 0, 0));
    const uint32_t rows = uint32_t(__Menu_Send(index, 0x100000fc, 0x33, 0, 0));
    const int16_t text_h = int16_t((lines & 0xff) * 0x1f);
    const int16_t total = int16_t(text_h + 0x1e + (rows & 0xff) * 0x15);
    const int16_t height = int16_t((rows & 0xff) == 0 ? total : total + 0x18);
    box->base.h = height;
    const int16_t y = int16_t(int32_t((448.0f - float(height)) * 0.5f));
    box->base.y = y;
    memo->base.y = int16_t(y + 10);
    list->base.y = int16_t(y + text_h + 0x1e);
    M_MANAGER* owner = mgr(index);
    owner->input = 0;
    Memo_Update(owner, reinterpret_cast<M_PAGE*>(nf::raw(memo->base.owner)), memo);
    List_Update(owner, reinterpret_cast<M_PAGE*>(nf::raw(list->base.owner)), list);
    Button_Update(owner, reinterpret_cast<M_PAGE*>(nf::raw(box->base.owner)), box);
    owner->input = 1;
}

// [decomp] 0x001f7de8 Menu_UpdateMessageBox
// Um tick da operação do cartão pedida em ls (0 ler, 1 gravar, 2 listar, 3 apagar, 4 listar só o primeiro) e a
// caixa dela. Peculiaridade do original: gravar sem onde ou sem nome desiste e não libera a trava recurse_160,
// e as chamadas seguintes não fazem mais nada.
void Menu_UpdateMessageBox(uint8_t index, uint16_t mask, uint8_t no) {
    MENU_LS& l = menu_ls();
    if (l.active == 0 || recurse_160 != 0) return;
    recurse_160 = 1;
    switch (l.op) {
        case 0:
            state_159 = LS_Load(l.name, no, mask);
            break;
        case 1:
            if (l.can_save == 0 || std::strlen(l.name) == 0) {
                l.active = 0;
                l.done = 1;
                LS_FlushStates();
                return;
            }
            state_159 = LS_Save(l.name, no, mask);
            l.stale = 1;
            break;
        case 2:
            state_159 = Menu_EnumSaves(index, 0);
            break;
        case 3:
            if (del_state_161 == 4 || del_state_161 == 0) {
                psiDeleteData(l.name);
                del_state_161 = 5;
            } else if (del_state_161 != 5) {
                break;
            }
            state_159 = psiDeletingData();
            if (state_159 == 4) del_state_161 = 0;
            break;
        case 4:
            state_159 = Menu_EnumSaves(index, 1);
            break;
        default:
            break;
    }
    PS2_DoCardFlow(mgr(index), state_159, &l);
    recurse_160 = 0;
}

// [decomp] 0x001f7b78 Menu_EnumSaves
// A lista de codinomes em codename_buf: os dois padrões e os perfis do cartão (só o primeiro de até 8 letras
// com `one`), cada um também como linha no controle de ls; no fim ordena os do cartão. Com a lista ainda
// válida (op 2 sem ls.stale) não lista de novo. Devolve 1 listando, 7 pronto.
uint32_t Menu_EnumSaves(uint8_t, int8_t one) {
    MENU_LS& l = menu_ls();
    if (l.op == 2 && l.stale == 0) {
        enum_state = 7;
    } else if (enum_state == 1) {
        for (uint32_t n = 0;; ++n) {
            if (0xff < n) return enum_state;
            const intptr_t r = psiGetNextSave();
            if (r == -1) return enum_state;
            if (r == 0) break;
            const char* name = reinterpret_cast<const char*>(r);
            if (l.count >= uint32_t(kCodenames)) continue;  // só no port: dentro de codename_buf
            if (one == 0) {
                if (std::strlen(name) < 0x21) {
                    std::snprintf(codename_buf[l.count], 0x20, "%s", name);
                    __Menu_SendMessage(l.control, 0x10, val(codename_buf[l.count]), 0);
                    ++l.count;
                }
            } else if (std::strlen(name) < 9) {
                std::snprintf(codename_buf[l.count], 0x20, "%s", name);
                __Menu_SendMessage(l.control, 0x10, val(codename_buf[l.count]), 0);
                enum_state = 7;
                ++l.count;
                psiEndSaveEnum();
                return enum_state;
            }
        }
        enum_state = 7;
        __Menu_SendMessage(l.control, 0x27, 0, int32_t(l.count) - 1);
        psiEndSaveEnum();
        l.stale = 0;
        QuickSort(codename_buf[2], l.count - 2, 0x20, COMP_STRINGS, nullptr);
    } else if (enum_state == 0 || enum_state == 4) {
        l.count = 0;
        for (uint32_t i = 0; i < 2; ++i) {
            const char* name = Txt_BindLabel(rd32(def_codename + i * 0x38), 0);
            std::snprintf(codename_buf[l.count], 0x20, "%s", name != nullptr ? name : "");
            ++l.count;
        }
        psiStartSaveEnum(one);
        enum_state = 1;
    }
    return enum_state;
}

// [decomp] 0x00202000 Menu_FlushEnum
void Menu_FlushEnum() { enum_state = 0; }

// [decomp] 0x001f79c0 Menu_PutCodenamesIntoControl
// Os nomes na roda (a faixa e a posição) ou na lista (as linhas, sem o "New Codename"); o segundo padrão é
// "Play without saving" na escolha da missão e "Default" nos codinomes.
void Menu_PutCodenamesIntoControl(M_CONTROL* c) {
    if (c == nullptr) return;
    const M_PAGE* page = menu_manager[0].page;
    const uint32_t label = (page == nullptr || page->base.id != 0x40000025) ? 0x211 : 0x10002ff;
    const char* s = Txt_BindLabel(label, 0);
    std::snprintf(codename_buf[1], 0x20, "%s", s != nullptr ? s : "");
    const uint32_t count = menu_ls().count;
    if (c->type == MCT_SCROLL) {
        int32_t last = int32_t(count) - 1;
        if (last < 1) last = 1;
        __Menu_SendMessage(c, 0x27, 0, last);
        __Menu_SendMessage(c, 0x2e, __Menu_SendMessage(c, 0x40, 0, 0), 0);
        return;
    }
    if (c->type != MCT_RADIO && c->type != MCT_LIST) return;
    for (uint32_t i = 1; i < count && i < uint32_t(kCodenames); i = (i + 1) & 0xffff) {
        __Menu_SendMessage(c, 0x10, val(codename_buf[i]), 0);
    }
    __Menu_SendMessage(c, 0x1d, __Menu_SendMessage(c, 0x34, 0, 0), 0);
}

// [decomp] 0x001f7fa8 Menu_MapDefaultCodename
// Aplica um perfil padrão (def_codename `slot`) ao jogador: 1 as opções de controle e as legendas, 2 as
// missões liberadas e o jogo terminado (zera as estatísticas da missão), 4 as do multijogador, 8 o som e a tela
// dividida; com qualquer parte, as medalhas e as trapaças zeradas. Sempre: o perfil em ls, o nome, as missões
// e as medalhas. Só no port, no fim: as opções que o nightfire.ini tem valem por cima das do perfil padrão
// (nf::game_options_default_profile: o .ini faz no PC o papel do perfil padrão, que o PS2 perde ao desligar).
void Menu_MapDefaultCodename(uint8_t player, uint8_t slot, uint32_t what) {
    if (1 < slot) return;  // só no port: def_codename tem dois perfis
    const uint8_t* d = def_codename + slot * 0x38;
    PlayerInput_tag& p = PlayerSetting[player & 3];
    if ((what & 1) != 0) {
        wr32(MPSettings + 0x1bc, d[0x2e]);
        wr32(MPSettings + player * 0x30 + 0x28, d[0x1a]);
        wr32(MPSettings + 0x1c0, d[0x2f]);
        DrawInfo[2] = d[0x18];
        p.unknown_0b = d[0x2c];
        p.aim_toggle = d[0x26];
        p.autoaim = d[0x24];
        p.auto_switch = d[0x2a];
        std::memcpy(&p.style, d + 0x20, 2);
        p.unknown_08 = d[0x29];
        p.crouch_toggle = d[0x27];
        std::memcpy(&p.unknown_10, d + 0x22, 2);
        p.invert_look = d[0x1e];
        p.unknown_09 = d[0x28];
        p.unknown_0c = d[0x2b];
    }
    if ((what & 2) != 0) {
        Menu_SetNightfireStatus(rd32(d + 4));
        PlrStat_ResetForMission();
        GameState.unknown_52 = d[0x19];
    }
    if ((what & 4) != 0) {
        int16_t mp;
        std::memcpy(&mp, d + 0x1c, 2);
        p.autoaim_mp = d[0x25];
        wr32(MPSettings + player * 0x30 + 0x2c, uint32_t(int32_t(mp)));
    }
    if ((what & 8) != 0) {
        DrawInfo[0] = d[0x2d];
        SFXMusicSetVolume(rd32(d + 0x10));
        SFXSetVolume(rd32(d + 0x14));
    }
    uint64_t bonus;
    std::memcpy(&bonus, d + 8, 8);
    if (what != 0) {
        Menu_SetBonus(bonus, player, 0);
        wr32(CheatInfo, 0);
        wr32(CheatInfo + 8, 0);
        wr32(CheatInfo + 4, 0);
    }
    MENU_LS& l = menu_ls();
    l.slot = slot;
    const char* name = Txt_BindLabel(rd32(d), 0);
    std::snprintf(l.name, sizeof(l.name), "%s", name != nullptr ? name : "");
    Menu_SetNightfireStatus(rd32(d + 4));
    Menu_SetBonus(bonus, player, 0);
    if (player == 0) nf::game_options_default_profile(what);
}

// [decomp] 0x001f9930 Menu_IsValidCodename
// O nome digitado sem os espaços das pontas e em maiúsculas: 0 se vale; 0x3bc sem letra ou algarismo, 0x2c0
// se contém o nome de um perfil padrão ("NEW CODENAME", "DEFAULT").
uint32_t Menu_IsValidCodename(char* name) {
    if (std::strlen(name) == 0) return 0x3bc;
    const char* start = nullptr;
    for (size_t i = 0; i < std::strlen(name); ++i) {
        if (name[i] != ' ') {
            start = name + i;
            break;
        }
    }
    if (start == nullptr) return 0x3bc;
    int32_t end = int32_t(std::strlen(name));
    do {
        --end;
    } while (0 <= end && name[end] == ' ');
    name[end + 1] = 0;
    char upper[0x40];
    std::snprintf(upper, sizeof(upper), "%s", start);
    for (char* c = upper; *c != 0; ++c) *c = char(std::toupper(uint8_t(*c)));
    for (uint32_t i = 0; i < 2; ++i) {
        char other[0x40];
        const char* s = Txt_BindLabel(rd32(def_codename + i * 0x38), 0);
        std::snprintf(other, sizeof(other), "%s", s != nullptr ? s : "");
        for (char* c = other; *c != 0; ++c) *c = char(std::toupper(uint8_t(*c)));
        if (std::strstr(upper, other) != nullptr) return 0x2c0;
    }
    std::memcpy(name, upper, std::strlen(upper) + 1);  // o nome só encurta (sem os espaços das pontas)
    return 0;
}

// [decomp] 0x002028d8 Menu_SelectCodenameInControl
// A roda `id` na posição do nome (0 se ele não está na lista).
void Menu_SelectCodenameInControl(uint8_t index, char* name, uint32_t id) {
    uint32_t i = 0;
    if (name != nullptr && std::strlen(name) != 0) {
        for (; i < menu_ls().count && i < uint32_t(kCodenames); ++i) {
            if (std::strcmp(codename_buf[i], name) == 0) break;
        }
        if (i >= menu_ls().count || i >= uint32_t(kCodenames)) {
            __Menu_Send(index, id, 0x2e, 0, 0);
            return;
        }
    }
    __Menu_Send(index, id, 0x2e, int32_t(i), 0);
}

// ---- as páginas dos codinomes ----

// [decomp] 0x00212ad0 P_CNSELECT_Handler
// Os codinomes (0x4000001b): como a escolha da missão, com a roda 0x100000eb; um codinome do cartão lido vai ao
// menu do codinome (0x4000001d); voltar, o menu principal.
int32_t P_CNSELECT_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    MENU_LS& l = menu_ls();
    if (0x22 < message - 0x4c) return 1;
    switch (message) {
        case 0x4c:
            l.stale = 1;
            l.next_page = 0;
            Menu_SelectCodenameInControl(index, l.name, 0x100000eb);
            Menu_StartIris(uint32_t(b) == 0x40000002 ? 0 : 4, index, 0x10000108);
            __Menu_Send(index, 0x100000ec, 0x18, val(kSpace), 0);
            [[fallthrough]];
        case 0x6e: {
            menu_bonus[0xa5] = 0;
            M_CONTROL* wheel = ptr<M_CONTROL>(__Menu_Send(index, 0x100000eb, 0x39, 0, 0));
            if (l.stale == 0) {
                l.done = 1;
            } else {
                l.active = 1;
                l.slot = 999;
                l.op = 2;
                l.done = 1;
                l.keep = 0;
                l.next_page = 0;
                l.next_flags = 0;
                l.control = wheel;
                Menu_UpdateMessageBox(index, 0, 0);
            }
            if (uint32_t(b) != 0x40000002) {
                __Menu_Send(index, 0x10000122, 0x1a, 0, 0);
            } else {
                Menu_StartIris(0, index, 0x10000108);
                __Menu_Send(index, 0x10000122, 0x1a, 0xff, 0);
                __Menu_SendMessage(&mgr(index)->base, 0x68, 1, 0);
                __Menu_SendDelayedMessage(0xf, &mgr(index)->base, 0x68, 0, 0);
                M_MESSAGE msg{};
                msg.message = 0xe1;
                Process_Create(ptr<M_CONTROL>(__Menu_Send(index, 0x10000122, 0x39, 0, 0)), &msg, 0xf, 0);
            }
            select_def_codename_461 = uint32_t(b) == 0x40000002 ? 1 : 0;
            PlayerSetting[1].index = 1;
            PlayerSetting[3].index = 3;
            PlayerSetting[2].index = 2;
            closing_460 = 0;
            PlayerSetting[0].index = 0;
            return 1;
        }
        case 0x50:
            Menu_PlayIris(1, index, 0x10000108);
            if (l.done != 0 && closing_460 == 0) {
                Menu_PutCodenamesIntoControl(ptr<M_CONTROL>(__Menu_Send(index, 0x100000eb, 0x39, 0, 0)));
                l.done = 0;
                if (select_def_codename_461 != 0) __Menu_Send(index, 0x100000eb, 0x2e, 1, 0);
            }
            if (l.active == 0 || 1 < l.slot) {
                Menu_UpdateMessageBox(index, 0xff, 0);
            } else {
                l.active = 0;
                Menu_MapDefaultCodename(0, uint8_t(l.slot), 0xff);
                if (l.next_page != 0) Menu_ChangePageCloseIris(l.next_page, index, 0x10000108);
            }
            return 1;
        case 0x6a:
            if (l.op == 0) {
                Menu_ChangePageCloseIris(0x4000001d, index, 0x10000108);
            } else {
                Menu_UpdateCodenameWheel(index, ptr<M_CONTROL>(__Menu_Send(index, 0x100000eb, 0x39, 0, 0)), 0x100000ec,
                                         0x100001cd, 0x100001f1, 0x10000108, 0);
            }
            closing_460 = 1;
            return 1;
        case 0x6b: {
            M_CONTROL* m = &mgr(index)->base;
            __Menu_SendMessage(m, 0x68, 1, 0);
            __Menu_SendDelayedMessage(0x13, m, 0x44, 0x40000002, 0);
            __Menu_SendDelayedMessage(0x13, m, 0x68, 0, 0);
            M_MESSAGE msg{};
            msg.message = 0xe1;
            msg.a = 0xff;
            Process_Create(ptr<M_CONTROL>(__Menu_Send(index, 0x10000122, 0x39, 0, 0)), &msg, 0xf, 0);
            *ptr<int32_t>(b) = -2;
            return 1;
        }
        default:
            return 1;
    }
}

// [decomp] 0x00212f58 C_SBCNSELECT_Handler
// A roda dos codinomes: escolher 0 cria (o perfil padrão e a página do nome), 1 o padrão (o menu do codinome),
// 2+ lê do cartão; Y (0x5d) cria; X (0x5e) apaga um do cartão. A barra de ajuda mostra "Delete" só nos do
// cartão.
int32_t C_SBCNSELECT_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    MENU_LS& l = menu_ls();
    switch (message) {
        case 0x49:
        case 0x54:
            Menu_UpdateCodenameWheel(index, c, 0x100000ec, 0x100001cd, 0x100001f1, 0x10000108, message == 0x49 ? 1 : 0);
            __Menu_Send(index, 0x10000220, 0x18, txt(1 < __Menu_SendMessage(c, 0x40, 0, 0) ? 0x10000b4 : 0x10000b9), 0);
            return 1;
        case 0x4b: {
            const uint32_t v = uint32_t(__Menu_SendMessage(c, 0x40, 0, 0));
            if (v == 0) {
                l.slot = 0;
                Menu_MapDefaultCodename(0, 0, 0xff);
                Menu_ChangePageCloseIris(0x40000020, index, 0x10000108);
            } else if (v < 2) {
                l.slot = v;
                Menu_MapDefaultCodename(0, uint8_t(v), 0xff);
                Menu_ChangePageCloseIris(0x4000001d, index, 0x10000108);
            } else {
                const int32_t w = __Menu_SendMessage(c, 0x40, 0, 0);
                l.active = 1;
                l.next_page = 0;
                l.next_flags = 0;
                l.op = 0;
                l.done = 0;
                l.keep = 0;
                l.slot = v;
                l.control = c;
                if (0 <= w && w < kCodenames) std::snprintf(l.name, sizeof(l.name), "%s", codename_buf[w]);
                Menu_UpdateMessageBox(index, 0xff, 0);
            }
            __Menu_Send(index, 0x10000100, 0x20, 0x4000001b, 0);
            return 1;
        }
        case 0x5d:
            l.slot = 0;
            Menu_MapDefaultCodename(0, 0, 0xff);
            __Menu_SendMessage(&mgr(index)->base, 0x44, 0x40000020, 0);
            __Menu_Send(index, 0x10000100, 0x20, 0x4000001b, 0);
            return 1;
        case 0x5e: {
            if (__Menu_SendMessage(c, 0x40, 0, 0) <= 1) return 1;
            const int32_t w = __Menu_SendMessage(c, 0x40, 0, 0);
            l.slot = 999;
            l.active = 1;
            l.stale |= 1;
            l.op = 3;
            l.next_page = 0;
            l.next_flags = 0;
            l.done = 0;
            l.control = nullptr;
            l.keep = 0;
            if (0 <= w && w < kCodenames) std::snprintf(l.name, sizeof(l.name), "%s", codename_buf[w]);
            Menu_UpdateMessageBox(index, 0, 0);
            return 1;
        }
        default:
            return 1;
    }
}

// [decomp] 0x00213288 P_CNMENU_Handler
// O menu do codinome (0x4000001d): "Edit <nome>", a roda das opções (a última, "Save Codename", vira
// "Codenames" nos perfis padrão); vindo da página do nome com um codinome novo, mostra a configuração do
// controle (0x40000022). Voltar com mudanças pergunta (caixa 0xd) e, no "continuar", volta sem elas (a posição
// da tela de antes).
int32_t P_CNMENU_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    MENU_LS& l = menu_ls();
    if (message == 0x50) {
        Menu_PlayIris(1, index, 0x10000109);
        Menu_UpdateMessageBox(index, 0xff, 0);
        if (Menu_UpdateOptionBox(nullptr) == 3) {
            psiSetScreenPos(screen_adjust_x_468, screen_adjust_y_469);
            Manager_SendMessage(mgr(index), 0x5f, 1, 0);
        }
    } else if (message == 0x4c) {
        if (uint32_t(b) == 0x4000001b || (uint32_t(b) == 0x40000020 && cheat_mode == 0)) {
            psiGetScreenPos(&screen_adjust_x_468, &screen_adjust_y_469);
            cn_modified_flag = 0;
            Menu_SelectItemInControl(ptr<M_CONTROL>(__Menu_Send(index, 0x10000100, 0x39, 0, 0)), cn_options, 7, 1);
            Menu_StartIris(0, index, 0x10000109);
            const char* f = Txt_BindLabel(0x10001e4, 0);
            std::snprintf(buf_470, sizeof(buf_470), f != nullptr ? f : "%s", l.name);
            __Menu_Send(index, 0x100001f9, 0x18, val(buf_470), 0);
            cn_options[6].name = l.slot < 2 ? 0x14b : 500;
            if (uint32_t(b) == 0x40000020 && cheat_mode == 0) Manager_SendMessage(mgr(index), 0x44, 0x40000022, 0);
        } else {
            Menu_StartIris(4, index, 0x10000109);
        }
    } else if (message == 0x6a) {
        if (l.next_page != 0) Menu_ChangePageCloseIris(l.next_page, index, 0x10000108);
    } else if (message == 0x6b && cn_modified_flag != 0) {
        Menu_CreateOptionBox(index, 0x10002d3, 0xd, 1, 1);
        *ptr<int32_t>(b) = -2;
    }
    return 1;
}

// [decomp] 0x00213528 C_SBCNOPTIONS_Handler
// A roda do menu do codinome: 0 "Secret Unlocks" (o teclado em modo de código), 1 configuração do controle,
// 2 controle das missões de carro, 3 opções avançadas, 4 do multijogador, 5 áudio e vídeo, 6 grava o codinome do
// cartão (ou guarda as opções no perfil padrão) e volta à página que trouxe aqui (a tag da roda).
int32_t C_SBCNOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    MENU_LS& l = menu_ls();
    if (message == 0x4b) {
        const uint8_t v = uint8_t(__Menu_SendMessage(c, 0x40, 0, 0));
        static const uint32_t kPages[6] = {0x40000020, 0x40000022, 0x4000003e, 0x4000002d, 0x4000002e, 0x40000031};
        if (v < 6) {
            Manager_SendMessage(mgr(index), 0x44, int32_t(kPages[v]), 0);
        } else if (v == 6) {
            if (1 < l.slot) {
                l.next_page = c->tag;
                l.active = 1;
                l.slot = 999;
                l.op = 1;
                l.keep = 0;
                l.next_flags = 1;
                l.done = 0;
                l.control = c;
                Menu_UpdateMessageBox(index, 0xff, 0);
                return 1;
            }
            Menu_UpdateDefaultCodename(0, uint8_t(l.slot));
            l.next_page = c->tag;
            Menu_ChangePageCloseIris(l.next_page, index, 0x10000109);
        }
    } else if (message == 0x51) {
        __Menu_SendMessage(c, 0x27, 0, 6);
    } else if (message == 0x49 || message == 0x54) {
        Menu_UpdateWheel(index, c, cn_options, 0x100000ff, 0x100001a5, 0x100001ec, 0x10000109, message == 0x49 ? 1 : 0);
    }
    return 1;
}

// [decomp] 0x00213790 P_CNNAME_Handler
// O nome no teclado da tela (0x40000020): o nome sugerido "BOND" (ou vazio no modo de código, vindo do menu do
// codinome), o foco no "End"; a caixa 4 (sim) grava um perfil novo; a 0xc (o código aceito) volta.
int32_t P_CNNAME_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    MENU_LS& l = menu_ls();
    if (message == 0x4c) {
        l.active = 0;
        cheat_mode = uint32_t(b) == 0x4000001d ? 1 : 0;
        del_when_keypressed = 1;
        if (cheat_mode != 0) {
            codename_entry[0] = 0;
            __Menu_Send(index, 0x10000242, 0x18, txt(0x10002d4), 0);
        } else {
            __Menu_Send(index, 0x10000242, 0x18, txt(0x1f5), 0);
            std::memset(codename_entry, 0, 8);
            std::memcpy(codename_entry, "BOND", 4);  // 0x0030d530
        }
        __Menu_Send(index, 0x10000075, 0x18, val(codename_entry), 0);
        Manager_SendMessage(mgr(index), 0x22, 0, __Menu_SendEx(index, 0x10000074, 0x3ea, 0x39, 0, 0));
        static const uint8_t kAccents[8] = {0xc4, 0, 0xc2, 0, 0xc6, 0, 0xc0, 0};
        std::memcpy(buf_477, kAccents, sizeof(buf_477));
        __Menu_SendEx(index, 0x10000074, 100, 0x18, val(buf_477), 0);
        __Menu_SendEx(index, 0x10000074, 0x65, 0x18, val(buf_477 + 2), 0);
        __Menu_SendEx(index, 0x10000074, 0x66, 0x18, val(buf_477 + 4), 0);
        __Menu_SendEx(index, 0x10000074, 0x67, 0x18, val(buf_477 + 6), 0);
    } else if (message == 0x50) {
        Menu_UpdateMessageBox(index, 0xff, 0);
        uint32_t id = 0;
        const uint32_t r = Menu_UpdateOptionBox(&id);
        if (id == 4) {
            if (r == 1) {
                PlrStats_ResetScoring();
                l.active = 1;
                l.slot = 999;
                l.next_page = 0x4000001d;
                l.op = 1;
                l.keep = 0;
                l.next_flags = 1;
                l.done = 0;
                l.control = nullptr;
                Menu_UpdateMessageBox(index, 0xff, 0);
            }
        } else if (id == 0xc && r != 0) {
            Manager_SendMessage(mgr(index), 0x5f, 0, 0);
        }
    }
    return 1;
}

// [decomp] 0x00213a98 C_KEYBOARD_Handler
// As teclas da tela (0x10000074, a tag diz qual): letras e algarismos (o texto da tecla) e espaço até 8, Del
// (1000) apaga a última, End (0x3ea) confere o nome (caixa 6 se não vale), e grava o perfil novo e vai ao menu
// do codinome (zera os recordes: PlrStats_ResetScoring); no modo de código, confere o código. A primeira tecla
// apaga o nome sugerido.
int32_t C_KEYBOARD_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x4b && message != 0x61) return 1;
    uint32_t tag = c->tag;
    if (tag != 0x3ea && del_when_keypressed != 0) {
        del_when_keypressed = 0;
        codename_entry[0] = 0;
        if (c->tag == 0x3e9) return 1;
        tag = c->tag;
    }
    char ch;
    if (tag == 0x3e9) {
        if (kCodenameMax - 1 < int32_t(std::strlen(codename_entry))) return 1;
        ch = ' ';
    } else if (tag == 1000) {
        const size_t n = std::strlen(codename_entry);
        if (n != 0) codename_entry[n - 1] = 0;  // só no port: com o nome vazio o original zeraria o byte anterior
        return 1;
    } else if (tag == 0x3ea) {
        MENU_LS& l = menu_ls();
        if (cheat_mode == 0) {
            const uint32_t problem = Menu_IsValidCodename(codename_entry);
            if (problem != 0) {
                Menu_CreateOptionBox(index, problem, 6, 1, 0);
                del_when_keypressed = 1;
                return 1;
            }
            l.slot = 999;
            std::snprintf(l.name, sizeof(l.name), "%s", codename_entry);
            PlrStats_ResetScoring();
            l.active = 1;
            l.slot = 999;
            l.next_page = 0x4000001d;
            l.op = 1;
            l.keep = 0;
            l.next_flags = 1;
            l.done = 0;
            l.control = nullptr;
            Menu_UpdateMessageBox(index, 0xff, 0);
            return 1;
        }
        if (Menu_SpecialCodenameCheck(codename_entry) == 0) return 1;
        Menu_CreateOptionBox(index, 0x100029d, 0xc, 1, 0);
        cn_modified_flag = 1;
        return 1;
    } else {
        if (kCodenameMax - 1 < int32_t(std::strlen(codename_entry))) return 1;
        const char* key = ptr<const char>(__Menu_SendMessage(c, 0x2f, 0, 0));
        if (key == nullptr) return 1;  // só no port: a tecla sem texto (o original leria por nulo)
        ch = key[0];
    }
    const size_t n = std::strlen(codename_entry);
    codename_entry[n + 1] = 0;
    codename_entry[n] = ch;
    return 1;
}

// [decomp] 0x00216810 C_KEYPAD_Handler
// O teclado numérico do PDA (página 0x40000021 do menu de jogo 0x80000003): cada tecla põe o algarismo da tag
// nos algarismos de GlobalVars (+0xd, até o máximo em +0x12) e mostra o código; 0x51 mostra "****".
int32_t C_KEYPAD_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        if (GlobalVars[0x12] <= GlobalVars[0x13]) GlobalVars[0x13] = 0;
        const uint32_t n = GlobalVars[0x13];
        if (0xd + n >= 0x12) return 1;  // só no port: os algarismos ficam dentro de GlobalVars
        GlobalVars[0xd + n] = uint8_t(char(c->tag) + '0');
        for (uint32_t i = 0; i <= n; ++i) buf_250[i] = char(GlobalVars[0xd + i]);
        buf_250[n + 1] = 0;
        __Menu_Send(index, 0x10000075, 0x18, val(buf_250), 0);
        ++GlobalVars[0x13];
    } else if (message == 0x51) {
        std::snprintf(buf_250, sizeof(buf_250), "****");
        __Menu_Send(index, 0x10000075, 0x18, val(buf_250), 0);
        __Menu_Send(index, 0x10000075, 0x62, 0, 0);
    }
    return 1;
}

void menu_codename_debug_power_on() {
    cheat_mode = 0;
    cn_modified_flag = 0;
    del_when_keypressed = 0;
    std::memset(codename_entry, 0, sizeof(codename_entry));
    enum_state = 0;
    frames_125 = 0;
    dialog_open_124 = 0;
    message_123 = nullptr;
    state_117 = 0;
    state_159 = 0;
    recurse_160 = 0;
    del_state_161 = 0;
    closing_460 = 0;
    select_def_codename_461 = 0;
    screen_adjust_x_468 = screen_adjust_y_469 = 0;
}
