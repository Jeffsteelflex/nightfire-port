// Os tratadores dos menus de jogo e os do resultado da missão, e as funções do jogo que eles chamam
// (medalhas, bônus, a caixa de mensagem, as linhas das estatísticas). Ver engine/menu.h e
// docs/engine.md, "Menus".
//
// Páginas portadas: 0x4000004b pausa (P_PAUSE_Handler; os controles dela, id 0x10000028, em
// C_GCPAUSE_Handler), 0x40000042 fim de missão (P_ENDMISSION_Handler), 0x40000036 resultado
// (P_NFRESULTS_Handler), 0x40000038 bônus (P_NFBONUS_Handler), 0x40000037 estatísticas
// (P_NFSTATS_Handler) e a caixa de mensagem 0x4000002f (a lista 0x10000120, C_LBMSGOPTIONS_Handler).
// Os do caminho de um jogador do menu principal estão em menu_frontend.cpp e menu_codename.cpp
// (engine/menu_frontend.h), os do dossiê em menu_dossier.cpp e as caixas da página de depuração (C_CHCH*) em
// menu_cheats.cpp. Os outros identificadores com tratador (as opções do codinome, o multijogador, os créditos)
// ficam na tabela como stubs: a mensagem não é tratada (pending_call com o nome).
//
// Divergências (comentadas nas funções):
// - Menu_SetLevelBonus não escreve fora de sp_level (uma fase fora do menu ou a última; o original
//   escreveria antes ou depois da tabela); conta em g_menu_handler_guards.

#include "engine/menu.h"
#include "engine/menu_cheats.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/menu_tweaks.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/loadsave.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/txt.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/game_options.h"
#include "platform/psi_card.h"

uint8_t RewardsTable[kRewardsSize];
uint8_t menu_bonus[0xa8];
uint8_t ls[0x44];
uint8_t def_codename[112];
char VersionString[29];
uint8_t menu_movie = 0;
uint8_t DoneBetter = 0;          // 0x0030cc70 (PlrStat: o jogador fez melhor que o recorde)

namespace {

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }
template <class T>
T* ptr(int32_t v) {
    return nf::p32_ptr<T>(uint32_t(v));
}

M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint64_t* bonus_of(uint8_t player) { return reinterpret_cast<uint64_t*>(menu_bonus) + player; }

uint32_t mp_on() { return rd32(MPSettings + 0x180); }

// Rótulo de texto como ponteiro (Txt_BindLabel) para as mensagens.
int32_t txt(uint32_t label) { return val(Txt_BindLabel(label, 0)); }

// Estado do menu de pausa.
uint8_t state_quit_166 = 0;      // o "sim" da confirmação sai (1) ou recomeça (0)
uint8_t state_ignore_167 = 0;    // ignora o próximo 0x4b da lista principal (voltou do "não")
char buf_162[0x40];              // a versão (página de trapaças)
// A caixa de mensagem.
uint32_t option_return = 0;      // 0x0030d25c
uint32_t mb_id = 0;              // 0x0030d260
uint8_t mb_ok_only = 0;          // 0x0030d264
// O resultado.
uint8_t* score_table = nullptr;  // 0x0030d340 (PlrStat_GetScore)
uint8_t just_completed = 0;      // 0x0030d344 a missão final
uint8_t xbox_save_flag_272 = 0;
char score_buffer_269[0x40];
char target_buffer_270[0x40];
char medal_buffer_271[0x80];
char buf_276[0x80];
uint8_t b_265[0x34];             // Menu_GetLevelBonuses
// Timer_Seconds2String.
uint32_t Cnt_40 = 0;
char Buf_39[4][0x100];

char kNoText[] = "";            // 0x0030d2d8
char kTick[] = "~J";            // 0x0030d2e0
char kCross[] = "~I";           // 0x0030d2e8
char kFormat3[] = "\xff\x03";   // 0x0030d348

std::map<uint32_t, MenuHandlerEntry>& port_handlers() {
    static std::map<uint32_t, MenuHandlerEntry> table;
    return table;
}

}  // namespace

MenuHandlerGuards g_menu_handler_guards;

// ---- pausa ----

// [decomp] 0x0017d178 GS_PauseGame
// Pausa (ou volta) o jogo e os 8 jogadores do multijogador; o cronômetro da missão para junto.
void GS_PauseGame(int8_t pause) {
    GameState.paused = uint8_t(pause);
    for (uint32_t i = 0; i < 8; ++i) wr32(MPGame + i * 0x30, uint32_t(int32_t(pause)));
    if (pause == 0) {
        PlarStat_LogTimerUnpause(0);
    } else {
        PlarStat_LogTimerPause(0);
    }
}

// [decomp] 0x0017d280 GS_PausePlayer
void GS_PausePlayer(int8_t pause, uint16_t player) {
    if (player < 4) wr32(MPGame + player * 0x30, uint32_t(int32_t(pause)));
}

// [decomp] 0x00203c28 P_PAUSE_Handler
// A página de pausa: ao entrar (0x4c) monta as listas (os estilos de controle na 9, a principal 6
// escolhida, as outras escondidas, a mira invertida na 0xe), o texto de ajuda 0x201 e o foco nos
// controles 0x10000028; a cada tick (0x50) a resposta da caixa de mensagem volta a mostrar os
// objetivos; voltar (0x6b) da confirmação mostra a lista principal de novo.
int32_t P_PAUSE_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    const uint8_t player = mgr(index)->player_byte;
    if (message == 0x50) {
        if (Menu_UpdateOptionBox(nullptr) != 0) __Menu_SendEx(index, 0x10000028, 8, 0x2b, 0, 0);
    } else if (message == 0x4c) {
        if (mp_on() != 0) {
            __Menu_SendEx(index, 0x10000028, 1, 0x2b, 1, 0);
            M_CONTROL* b0 = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 0, 0x39, 0, 0));
            M_CONTROL* b3 = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 3, 0x39, 0, 0));
            M_CONTROL* b4 = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 4, 0x39, 0, 0));
            if (b0 != nullptr && b3 != nullptr) {
                const int16_t w = b3->w;
                const int16_t x = int16_t(int32_t(256.0f - float((w + (uint16_t(w) >> 15)) >> 1)));
                b3->x = x;
                b4->x = int16_t(x + w + 10);
                b0->x = int16_t(b3->x - b0->w - 10);
            }
        }
        const uint32_t styles[8] = {0x82, 0x91, 0xa0, 0xaf, 0xbe, 0xcd, 0x100018b, 0x100019a};
        for (int32_t i = 0; i < 8; ++i) __Menu_SendEx(index, 0x10000028, 9, 0x10, txt(styles[i]), i);
        __Menu_SendEx(index, 0x10000028, 6, 0x1d, 0, 0);
        __Menu_SendEx(index, 0x10000028, 9, 0x1e, int32_t(int16_t(PlayerSetting[player].style)), 0);
        __Menu_SendEx(index, 0x10000028, 8, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 9, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 10, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0xb, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0xc, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0xd, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0xd, 0x75, 0xaf, 0);
        __Menu_SendEx(index, 0x10000028, 0xe, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0x10, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 6, 0x59, 0, 3);
        __Menu_SendEx(index, 0x10000028, 6, 0x75, 0xaf, 0);
        __Menu_SendEx(index, 0x10000028, 0xd, 0x59, 0, 3);
        __Menu_Send(index, 0x10000028, 0x29, int32_t(0x806d59ffu), 0);
        __Menu_Send(index, 0x10000028, 0x62, 0, 0);
        __Menu_Send(index, 0x1000006f, 0x62, 0, 0);
        __Menu_Send(index, 0x100000c9, 0x62, 0, 0);
        __Menu_Send(index, 0x100000c9, 0x2b, 1, 0);
        __Menu_Send(index, 0x1000006f, 0x18, txt(0x201), 0);
        if (PlayerSetting[player].invert_look == 0) {
            __Menu_SendEx(index, 0x10000028, 0xe, 0x1a, 0x694646d2, 0);
            __Menu_SendEx(index, 0x10000028, 0xe, 0x18, txt(0x3bd), 0);
        } else {
            __Menu_SendEx(index, 0x10000028, 0xe, 0x1a, 0x645a49d2, 0);
            __Menu_SendEx(index, 0x10000028, 0xe, 0x18, txt(0x206), 0);
        }
        wr32(CheatInfo + 0xc, 0);
        __Menu_Send(index, 0x10000184, 0x6c, 0x100, 0);
        Manager_SendMessage(mgr(index), 0x22, 0x10000028, 0);
        std::snprintf(buf_162, sizeof(buf_162), "%s", VersionString);
        __Menu_Send(index, 0x100000d6, 0x18, val(buf_162), 0);
    } else if (message == 0x6b) {
        M_CONTROL* confirm = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 0xd, 0x39, 0, 0));
        if (MenuManager_GetStatus() == 3 && confirm != nullptr && confirm->state == 0) {
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x201), 0);
            __Menu_SendEx(index, 0x10000028, 0xc, 0x2b, 1, 0);
            __Menu_SendEx(index, 0x10000028, 0xd, 0x2b, 1, 0);
            __Menu_SendEx(index, 0x10000028, 6, 0x2b, 0, 0);
            __Menu_SendEx(index, 0x10000028, 0xf, 0x2b, 2, 0);
        }
    }
    return 1;
}

// [decomp] 0x00204408 C_GCPAUSE_Handler
// Os controles da pausa (id 0x10000028, distintos pela tag): os quatro botões de cima (0 opções, 1
// objetivos, 3 controles, 4 estatísticas) trocam o painel quando ganham o foco (0x4e); a lista
// principal (tag 6: continuar, recomeçar, sair) e a confirmação (tag 0xd: sim, não); a lista dos
// objetivos (tag 8; escolher no botão 1 abre a descrição numa caixa de mensagem); os estilos de
// controle (tag 9; o terceiro botão inverte a mira).
int32_t C_GCPAUSE_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    const uint8_t player = mgr(index)->player_byte;
    uint32_t tail_tag = 0;
    uint32_t tail_message = 0;
    int32_t tail_value = 0;
    if (message == 0x4b) {
        const uint32_t tag = c->tag;
        if (tag == 1) {
            const uint16_t which = uint16_t(__Menu_SendEx(index, 0x10000028, 8, 0x34, 0, 0));
            OBJ_STATE os{};
            os.text = 0xffffffffu;
            Mission_ObjectiveState(&os, which);
            if (os.text == 0xffffffffu) os.text = 0x31d;
            __Menu_SendEx(index, 0x10000028, 8, 0x2b, 1, 0);
            Menu_CreateOptionBox(index, os.text, 10, 1, 0);
            __Menu_Send(index, 0x10000120, 0x29, 0x645a4cff, 0);
            return 1;
        }
        if (tag == 0xd) {
            const int8_t choice = int8_t(__Menu_SendEx(index, 0x10000028, 0xd, 0x34, 0, 0));
            if (choice == 0) {
                GameState.unknown_51 = 0;
                if (state_quit_166 == 0) {
                    ResetMap_LevelToLoad(Mission_BaseMapHCode(), 0, 1);
                } else {
                    GameState.menu_page = mp_on() == 0 ? 0x4000001c : 0x40000033;
                    ResetMap_LevelToLoad(0x7000048, 0, 0);
                }
                SFXFadeDown(1);
                GameFlow_PushState(GF_FADE_REQUEST, 80.0f, 0xff);
                MenuManager_Delete(index);
                GameState.unknown_24 = 1;
                user_restarted_or_quit = 1;
                return 1;
            }
            if (choice != 1) return 1;
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x201), 0);
            __Menu_SendEx(index, 0x10000028, 0xc, 0x2b, 1, 0);
            __Menu_SendEx(index, 0x10000028, 0xd, 0x2b, 1, 0);
            __Menu_SendEx(index, 0x10000028, 6, 0x2b, 0, 0);
            state_ignore_167 = 1;
            return 1;
        }
        if (tag != 6) return 1;
        if (state_ignore_167 != 0) {
            state_ignore_167 = 0;
            return 1;
        }
        const uint8_t choice = uint8_t(__Menu_SendEx(index, 0x10000028, 6, 0x34, 0, 0));
        if (choice == 0) {
            MenuManager_Delete(index);
            return 1;
        }
        if (choice != 1 && choice != 2) return 1;
        state_quit_166 = choice == 2 ? 1 : 0;
        __Menu_Send(index, 0x1000006f, 0x18, txt(0x1000323), 0);
        __Menu_SendEx(index, 0x10000028, 6, 0x2b, 1, 0);
        __Menu_SendEx(index, 0x10000028, 0xc, 0x2b, 2, 0);
        __Menu_SendEx(index, 0x10000028, 0xc, 0x18, txt(choice == 2 ? 0x200 : 0x1ff), 0);
        __Menu_SendEx(index, 0x10000028, 0xd, 0x1d, 1, 0);
        tail_tag = 0xd;
        tail_message = 0x2b;
        tail_value = 0;
    } else if (message == 0x49) {
        if (c->tag != 9) return 1;
        const uint32_t style = uint32_t(__Menu_SendEx(index, 0x10000028, 9, 0x35, 0, 0));
        Menu_ChangeControllerStyle(player, style);
        Menu_DisplayControllerStyleList(index, style);
        return 1;
    } else if (message == 0x4e) {
        static const uint32_t kAll[] = {6, 8, 9, 10, 0xb, 0xc, 0xd, 0xe, 0x10, 0xf};
        for (uint32_t tag : kAll) __Menu_SendEx(index, 0x10000028, tag, 0x2b, 1, 0);
        __Menu_Send(index, 0x10000028, 0x29, int32_t(0x806d59ffu), 0);
        const uint32_t tag = c->tag;
        if (tag == 1) {
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x202), 0);
            M_LISTBOX* list = ptr<M_LISTBOX>(__Menu_SendEx(index, 0x10000028, 8, 0x39, 0, 0));
            if (list == nullptr) return 1;
            list->wrap = 1;
            M_CONTROL* l = &list->base;
            __Menu_SendMessage(l, 0x75, 100, 0);
            __Menu_SendMessage(l, 0x2b, 0, 0);
            __Menu_SendMessage(l, 0x17, 0, 0);
            __Menu_SendMessage(l, 0x28, 0, 6);
            __Menu_SendMessage(l, 0xe, 6, 0);
            __Menu_SendMessage(l, 0x59, 1, 3);
            __Menu_SendMessage(l, 0xe, 0x58, 0);
            list->scroll.wrap = 0;
            const int32_t n = Mission_NumVisObjectives();
            for (int32_t i = 0; i < n; ++i) {
                OBJ_STATE os{};
                Mission_ObjectiveState(&os, uint16_t(i));
                __Menu_SendMessage(l, 0x10, val(kNoText), 0);
                const uint32_t row = uint32_t(i) << 16;
                __Menu_SendMessage(l, 0x19, int32_t(row | 1), val(os.state == 4 ? kTick : kCross));
                __Menu_SendMessage(l, 0x19, int32_t(row | 2), txt(os.label));
            }
            __Menu_SendMessage(l, 0x1d, n - 1, 0);
            return 1;
        }
        if (tag == 0) {
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x201), 0);
            __Menu_SendEx(index, 0x10000028, 6, 0x2b, 0, 0);
            __Menu_SendEx(index, 0x10000028, 6, 0x75, 0xaf, 0);
            tail_tag = 0xf;
            tail_message = 0x2b;
            tail_value = 2;
        } else if (tag == 3) {
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x204), 0);
            __Menu_SendEx(index, 0x10000028, 9, 0x2b, 0, 0);
            __Menu_SendEx(index, 0x10000028, 0xb, 0x2b, 2, 0);
            __Menu_SendEx(index, 0x10000028, 10, 0x2b, 2, 0);
            __Menu_SendEx(index, 0x10000028, 0xe, 0x2b, 2, 0);
            __Menu_SendEx(index, 0x10000028, 0x10, 0x2b, 2, 0);
            const int32_t style = int16_t(PlayerSetting[player].style);
            Menu_DisplayControllerStyleList(index, uint32_t(style));
            __Menu_SendEx(index, 0x10000028, 9, 0x1e, style, 0);
            const bool inverted = PlayerSetting[player].invert_look != 0;
            __Menu_SendEx(index, 0x10000028, 0xe, 0x1a, inverted ? 0x645a49d2 : 0x694646d2, 0);
            tail_tag = 0xe;
            tail_message = 0x18;
            tail_value = txt(inverted ? 0x206 : 0x3bd);
        } else if (tag == 4) {
            __Menu_Send(index, 0x1000006f, 0x18, txt(0x205), 0);
            if (mp_on() != 0) {
                // Placar do multijogador (Menu_GetMPScore é um stub: o multijogador não foi portado).
                __Menu_SendEx(index, 0x10000028, 8, 0x75, 100, 0);
                __Menu_SendEx(index, 0x10000028, 8, 0x2b, 2, 0);
                __Menu_SendEx(index, 0x10000028, 8, 0x17, 0, 0);
                return 1;
            }
            uint8_t* score = PlrStat_GetScore(1);
            if (score == nullptr) return 1;
            const uint8_t* s = nf::p32_ptr<uint8_t>(rd32(score + 0x18));
            char nums[16][0x10];
            const uint32_t idx[] = {8, 0, 0x1c, 0x14, 0x30, 0x28, 0x44, 0x3c, 0x6c, 100, 0x80};
            for (int32_t i = 0; i < 11; ++i) std::snprintf(nums[i], 0x10, "%d", int32_t(rd32(s + idx[i])));
            std::snprintf(nums[11], 0x10, "%d", 100);
            std::snprintf(nums[12], 0x10, "%s", Timer_Seconds2String(int32_t(rd32(s + 0x94)), 0));
            std::snprintf(nums[13], 0x10, "%s", Timer_Seconds2String(int32_t(rd32(s + 0x8c)) * 100, 0));
            std::snprintf(nums[14], 0x10, "%d", int32_t(rd32(s + 0xa8)));
            std::snprintf(nums[15], 0x10, "%d", int32_t(rd32(s + 0xa0)));
            const auto send = [&](uint32_t m, int32_t a, int32_t b) { __Menu_SendEx(index, 0x10000028, 8, m, a, b); };
            char line[0x40];
            send(0x75, 0x57, 0);
            send(0x2b, 2, 0);
            send(0x17, 0, 0);
            send(0x28, 0, 0x2d);
            send(0xe, 0x1e, 0);
            send(0xe, 0x1e, 0);
            send(0x59, 1, 2);
            send(0x59, 2, 2);
            send(0x10, txt(0x1000204), 0);
            send(0x19, 1, txt(0x10001e1));
            send(0x19, 2, txt(0x10001e2));
            send(0x10, val(kNoText), 0);
            send(0x10, txt(0x165), 0);
            send(0x19, 0x20001, val(nums[0]));
            send(0x19, 0x20002, val(nums[1]));
            send(0x10, txt(0x15a), 0);
            const uint32_t rows[][4] = {{0x15b, 0x40000, 2, 3}, {0x15c, 0x50000, 4, 5}, {0x16e, 0x60000, 6, 7}};
            for (const auto& r : rows) {
                std::snprintf(line, sizeof(line), "  %s", Txt_BindLabel(r[0], 0));
                send(0x10, val(line), 0);
                send(0x19, int32_t(r[1] | 1), val(nums[r[2]]));
                send(0x19, int32_t(r[1] | 2), val(nums[r[3]]));
            }
            send(0x10, txt(0x15e), 0);
            send(0x19, 0x70001, val(nums[8]));
            send(0x19, 0x70002, val(nums[9]));
            send(0x10, txt(0x15f), 0);
            send(0x19, 0x80001, val(nums[10]));
            send(0x19, 0x80002, val(nums[11]));
            send(0x10, txt(0x160), 0);
            send(0x19, 0x90001, val(nums[12]));
            send(0x19, 0x90002, val(nums[13]));
            send(0x10, txt(0x161), 0);
            send(0x19, 0xa0001, val(nums[14]));
            send(0x19, 0xa0002, val(nums[15]));
            return 1;
        } else {
            return 1;
        }
    } else if (message == 0x5e) {
        if (c->tag != 9) return 1;
        uint8_t& invert = PlayerSetting[mgr(index)->player_byte].invert_look;
        invert = invert == 0 ? 1 : 0;
        if (invert != 0) {
            __Menu_SendEx(index, 0x10000028, 0xe, 0x1a, 0x645a49c0, 0);
            tail_value = txt(0x206);
        } else {
            __Menu_SendEx(index, 0x10000028, 0xe, 0x1a, 0x644646c0, 0);
            tail_value = txt(0x3bd);
        }
        tail_tag = 0xe;
        tail_message = 0x18;
    } else {
        return 1;
    }
    __Menu_SendEx(index, 0x10000028, tail_tag, tail_message, tail_value, 0);
    return 1;
}

// [decomp] 0x00216618 P_ENDMISSION_Handler
// O fim de missão (fracasso): ao entrar, a lista 0x100001a4 (tentar de novo, recomeçar a missão, sair
// para o menu) centrada; ao escolher, o valor da linha: 0 recomeça a fase com o que o jogador tinha
// (Player_RamLoad com o bônus de vida), 1 recomeça a missão do começo, 2 volta ao menu principal (página
// 0x4000001c); depois o fade e o menu fecha.
int32_t P_ENDMISSION_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        GameState.unknown_51 = 0;
        const int32_t choice = __Menu_Send(index, 0x100001a4, 0x35, 0, 0);
        if (choice == 1) {
            ResetMap_LevelToLoad(Mission_BaseMapHCode(), 0, 1);
        } else if (choice == 0) {
            ResetMap_LevelToLoad(GameState.level_file, 1, 1);
            if (glb_players[0] != nullptr) Player_RamLoad(bloke_of(glb_players[0]), 1);
        } else if (choice == 2) {
            GameState.menu_page = 0x4000001c;
            ResetMap_LevelToLoad(0x7000048, 0, 0);
        }
        SFXFadeDown(1);
        GameFlow_PushState(GF_FADE_REQUEST, 80.0f, 0xff);
        MenuManager_Delete(index);
    } else if (message == 0x4c) {
        __Menu_Send(index, 0x10000001, 0x62, 0, 0);
        __Menu_Send(index, 0x1000006f, 0x62, 0, 0);
        __Menu_Send(index, 0x100001a4, 0x62, 0, 0);
        __Menu_Send(index, 0x100001a4, 0x59, 0, 3);
        __Menu_Send(index, 0x100001a4, 0x23, 1, 0);
        __Menu_Send(index, 0x100001a4, 0x75, 200, 0);
        __Menu_Send(index, 0x10000184, 0x6c, 0x100, 0);
    }
    return 1;
}

// ---- resultado ----

// [decomp] 0x00208e78 P_NFRESULTS_Handler
// O resultado da missão (menu principal, depois de uma missão cumprida): ao entrar (0x4c) a pontuação
// (PlrStat_GetScore), a medalha alcançada e a próxima, e grava a medalha (Menu_SetLevelBonus);
// escolher (0x4b) continua para o bônus (0x40000038, se a medalha deu um), o fim do jogo (0x40000053,
// depois da última missão) ou o mapa das missões (0x4000001c); 0x5d/0x5e o dossiê e as estatísticas;
// 0x50 a caixa de salvar.
int32_t P_NFRESULTS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    M_MANAGER* m = mgr(index);
    if (0x13 < message - 0x4bu) return 1;
    switch (message) {
        case 0x4b: {
            if (PlrStats_DoneBetter() != 0 && 1 < rd32(ls + 0x10)) {
                Menu_CreateOptionBox(index, Txt_BindLabel(0x1000206, 0), 0xb, 0, 0);
                return 1;
            }
            if (PlrStats_DoneBetter() != 0) Menu_UpdateDefaultCodename(0, uint8_t(rd32(ls + 0x10)));
            uint32_t page;
            if (menu_bonus[0xa4] == 0) {
                if (just_completed == 0) {
                    __Menu_Send(index, 0x1000010e, 0x18, txt(0x283), 0);
                    Manager_SendMessage(m, 0x44, 0x4000001c, 1);
                    return 1;
                }
                page = 0x40000053;
            } else {
                page = 0x40000038;
            }
            Manager_SendMessage(m, 0x44, int32_t(page), 1);
            return 1;
        }
        case 0x4c: {
            uint32_t medal = 5;
            PlayerSetting[3].index = 3;
            PlayerSetting[2].index = 2;
            PlayerSetting[0].index = 0;
            PlayerSetting[1].index = 1;
            if (menu_movie == 0) {
                SFXRemove(0, 0x1d7);
                SFXRemove(0, 0x470);
                SFXStart(0x1d7, 0);
                SFXStart(0x470, 0);
                menu_movie = 1;
            }
            ls[0x3a] = 0;
            PS2SaveGameFine = 1;
            xbox_save_flag_272 = 0;
            Manager_SendMessage(m, 0x22, 0x10000165, 0);
            if (b == 0x40000037 || b == 0x4000002b) return 1;
            score_table = PlrStat_GetScore(0);
            if (score_table != nullptr) {
                const auto s32 = [](uint32_t off) { return rd32(score_table + off); };
                std::snprintf(score_buffer_269, sizeof(score_buffer_269), "%d", int32_t(s32(0x34)));
                uint32_t label = 0;
                uint32_t next_label = 0;
                uint32_t image = 0;
                const int32_t status = Mission_Status();
                if (status == 2 || status == 3) {
                    label = status == 2 ? 0x2fd : 0x2fe;
                    medal = 4;
                    next_label = 0x2cf;
                    std::snprintf(target_buffer_270, sizeof(target_buffer_270), "%d", int32_t(s32(4)));
                } else {
                    SeparateNumber(s32(0x34), score_buffer_269);
                    __Menu_Send(index, 0x10000167, 0x18, val(score_buffer_269), 0);
                    const uint32_t score = s32(0x34);
                    if (score != 0) {
                        if (s32(0x10) <= score) {
                            SeparateNumber(s32(0x10), target_buffer_270);
                            medal = 4, label = 0x2c7, next_label = 0x2cc, image = 0x300013f;
                        } else if (s32(0xc) <= score) {
                            SeparateNumber(s32(0x10), target_buffer_270);
                            medal = 3, label = 0x2c8, next_label = 0x2cc, image = 0x300013e;
                        } else if (s32(8) <= score) {
                            SeparateNumber(s32(0xc), target_buffer_270);
                            medal = 2, label = 0x2c9, next_label = 0x2cd, image = 0x3000140;
                        } else if (s32(4) <= score) {
                            SeparateNumber(s32(8), target_buffer_270);
                            medal = 1, label = 0x2ca, next_label = 0x2ce, image = 0x300013d;
                        } else {
                            SeparateNumber(s32(4), target_buffer_270);
                            medal = 5, label = 0x10001fd, next_label = 0x2cf, image = 0x300019a;
                        }
                    }
                }
                if (label == 0x10001fd) {
                    if (rd32(ls + 0x10) < 2) {
                        std::snprintf(medal_buffer_271, sizeof(medal_buffer_271), Txt_BindLabel(0x10001fd, 0),
                                      Txt_BindLabel(0x10001fe, 0));
                    } else {
                        std::snprintf(medal_buffer_271, sizeof(medal_buffer_271), Txt_BindLabel(0x10001fd, 0),
                                      reinterpret_cast<char*>(ls + 0x18));
                    }
                } else {
                    const char* t = Txt_BindLabel(label, 0);
                    std::snprintf(medal_buffer_271, sizeof(medal_buffer_271), "%s", t != nullptr ? t : "");
                }
                __Menu_Send(index, 0x100001d1, 0x23, val(kFormat3), 0);
                __Menu_Send(index, 0x100001d1, 100, 1, 0);
                __Menu_Send(index, 0x100001d1, 0x18, val(medal_buffer_271), 0);
                __Menu_Send(index, 0x10000167, 0x18, val(score_buffer_269), 0);
                __Menu_Send(index, 0x1000002b, 0x18, txt(next_label), 0);
                __Menu_Send(index, 0x10000168, 0x18, val(target_buffer_270), 0);
                __Menu_Send(index, 0x10000166, 0x2b, 2, 0);
                __Menu_Send(index, 0x10000166, 0x24, int32_t(image), 0);
            }
            just_completed = uint8_t(Menu_SetLevelBonus(GameState.unknown_44[1], medal, 0));
            return 1;
        }
        case 0x50: {
            bool no = false;
            const uint32_t r = Menu_UpdateOptionBox(nullptr);
            if (r == 1 || xbox_save_flag_272 != 0) {
                wr32(ls, 1);
                wr32(ls + 0x10, 999);
                ls[0x39] = 1;
                ls[0x42] = 0;
                wr32(ls + 8, 0);
                wr32(ls + 0xc, 0);
                ls[0x3a] = 0;
                wr32(ls + 0x14, 0);
                Menu_UpdateMessageBox(index, 0xff, 0);
            } else {
                no = r == 2;
                if (no) ls[0x3a] = 1;
            }
            if (1 < rd32(ls + 0x10)) Menu_UpdateMessageBox(index, 0xff, 0);
            if (ls[0x3a] == 0) return 1;
            if (PS2SaveGameFine == 0 && !no) {
                Menu_CreateOptionBox(index, Txt_BindLabel(0x1000206, 0), 0xb, 0, 0);
                ls[0x3a] = 0;
                return 1;
            }
            uint32_t page;
            if (menu_bonus[0xa4] == 0) {
                if (just_completed == 0) {
                    __Menu_Send(index, 0x1000010e, 0x18, txt(0x283), 0);
                    Manager_SendMessage(m, 0x44, 0x4000001c, 1);
                    ls[0x3a] = 0;
                    return 1;
                }
                page = 0x40000053;
            } else {
                page = 0x40000038;
            }
            Manager_SendMessage(m, 0x44, int32_t(page), 1);
            ls[0x3a] = 0;
            return 1;
        }
        case 0x5d:
            Manager_SendMessage(m, 0x44, 0x4000002b, 0);
            return 1;
        case 0x5e:
            Manager_SendMessage(m, 0x44, 0x40000037, 0);
            return 1;
        default:
            return 1;
    }
}

// [decomp] 0x00209670 P_NFBONUS_Handler
// O bônus ganho com a medalha: a imagem e os textos dos bônus da fase (Menu_GetLevelBonuses) num memo.
int32_t P_NFBONUS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    M_MANAGER* m = mgr(index);
    if (message == 0x4c) {
        uint8_t* info = Menu_GetLevelBonuses(GameState.unknown_44[1]);
        __Menu_Send(index, 0x100001d0, 0x23, val(kFormat3), 0);
        char* text = nullptr;
        if (menu_bonus[0xa4] != 0) {
            char* buf = reinterpret_cast<char*>(menu_bonus + 0x20);
            buf[0] = kNoText[0];
            text = buf;
            if (info[4] != 0) {
                __Menu_Send(index, 0x100001cf, 0x24, int32_t(rd32(info + 0xc)), 0);
                std::snprintf(buf, 0x84, "%s\n", Txt_BindLabel(rd32(info + 8), 0));
            }
            const uint32_t more[3][2] = {{0x28, 0x2c}, {0x1c, 0x20}, {0x10, 0x14}};
            for (const auto& k : more) {
                if (info[k[0]] == 0) continue;
                std::snprintf(buf_276, sizeof(buf_276), "%s", buf);
                std::snprintf(buf, 0x84, "%s\n%s", buf_276, Txt_BindLabel(rd32(info + k[1]), 0));
            }
        }
        __Menu_Send(index, 0x100001d0, 0x18, val(text), 0);
    } else if (message == 0x4b) {
        if (just_completed == 0) {
            Manager_SendMessage(m, 0x44, 0x4000001c, 1);
        } else {
            Manager_SendMessage(m, 0x44, 0x40000053, 1);
        }
    } else if (message == 0x5d) {
        Manager_SendMessage(m, 0x44, 0x4000002b, 0);
    } else if (message == 0x5e) {
        Manager_SendMessage(m, 0x44, 0x40000037, 0);
    }
    return 1;
}

// [decomp] 0x00209938 P_NFSTATS_Handler
// As estatísticas da missão (lista 0x10000022: alvo, valor e pontos por linha).
int32_t P_NFSTATS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x4c) return 1;
    int32_t extra = 0;
    score_table = PlrStat_GetScore(0);
    if (score_table == nullptr) return 1;
    const auto s32 = [](uint32_t off) { return rd32(score_table + off); };
    const uint8_t* st = nf::p32_ptr<uint8_t>(s32(0x18));
    const auto f = [&](uint32_t off) {
        float v;
        std::memcpy(&v, st + off, 4);
        return v;
    };
    const auto u = [&](uint32_t off) { return rd32(st + off); };
    __Menu_Send(index, 0x10000022, 0x17, 0, 0);
    __Menu_Send(index, 0x10000022, 0x73, 0xf050580, 0);
    __Menu_Send(index, 0x10000022, 0x28, 0, 0x23);
    __Menu_Send(index, 0x10000022, 0xe, 0x15, 2);
    __Menu_Send(index, 0x10000022, 0xe, 0x15, 2);
    __Menu_Send(index, 0x10000022, 0xe, 0x18, 2);
    __Menu_Send(index, 0x10000022, 0x10, txt(0x1000204), 0);
    __Menu_Send(index, 0x10000022, 0x19, 1, txt(0x10001e1));
    __Menu_Send(index, 0x10000022, 0x19, 2, txt(0x10001e2));
    __Menu_Send(index, 0x10000022, 0x19, 3, txt(0x163));
    Menu_AddRow(index, 0x165, u(8), u(0), uint32_t(f(0x10)), 0, 0);
    __Menu_Send(index, 0x10000022, 0x10, txt(0x15a), 0);
    Menu_AddRow(index, 0x15b, u(0x1c), u(0x14), uint32_t(f(0x24)), 1, 0);
    if (score_table[0x38] != 0) {
        extra = 3;
        Menu_AddRow(index, 0x15c, u(0x30), u(0x28), uint32_t(f(0x38)), 1, 0);
        Menu_AddRow(index, 0x16e, u(0x44), u(0x3c), uint32_t(f(0x4c)), 1, 0);
        Menu_AddRowPercentage(index, 0x15d, uint32_t(f(0x5c) * 100.0f + 0.5f), -1, uint32_t(f(0x60)), 0);
    }
    Menu_AddRowPercentage(index, 0x15e, uint32_t(f(0x70) * 100.0f + 0.5f), int32_t(u(100)), uint32_t(f(0x74)), 0);
    Menu_AddRowPercentage(index, 0x15f, uint32_t(f(0x84) * 100.0f + 0.5f), 100, uint32_t(f(0x88)), 0);
    Menu_AddRow(index, 0x160, s32(0x1c), s32(0x20) * 100, uint32_t(f(0x9c)), 0, 1);
    Menu_AddRow(index, 0x161, u(0xa8), u(0xa0), uint32_t(f(0xb0)), 0, 0);
    __Menu_Send(index, 0x10000022, 0x10, val(kNoText), 0);
    char num[0x40];
    __Menu_Send(index, 0x10000022, 0x10, txt(0x162), 0);
    SeparateNumber(s32(0x2c), num);
    __Menu_Send(index, 0x10000022, 0x19, int32_t(uint32_t(extra + 9) << 16 | 3), val(num));
    __Menu_Send(index, 0x10000022, 0x10, txt(0x164), 0);
    SeparateNumber(s32(0x28), num);
    __Menu_Send(index, 0x10000022, 0x19, int32_t(uint32_t(extra + 10) << 16 | 3), val(num));
    __Menu_Send(index, 0x10000022, 0x10, txt(0x163), 0);
    SeparateNumber(s32(0x34), num);
    __Menu_Send(index, 0x10000022, 0x19, int32_t(uint32_t(extra + 0xb) << 16 | 3), val(num));
    return 1;
}

// ---- caixa de mensagem ----

// [decomp] 0x001f9608 Menu_CreateOptionBox
// Mostra a página 0x4000002f por cima (transição) com o texto e as opções, centrada na vertical pelo
// tamanho do texto, e o foco na lista de opções.
void Menu_CreateOptionBox(uint8_t index, char* text, uint32_t id, uint8_t ok_only, uint8_t with_continue) {
    mb_id = id;
    mb_ok_only = ok_only;
    __Menu_Send(index, 0x1000011f, 0x18, val(text), 0);
    __Menu_Send(index, 0x10000120, 0x17, 0, 0);
    __Menu_Send(index, 0x10000120, 0x59, 0, 3);
    if (ok_only == 0) {
        __Menu_Send(index, 0x10000120, 0x70, 1, 0);
        __Menu_Send(index, 0x10000120, 0x10, txt(0x181), 1);
        __Menu_Send(index, 0x10000120, 0x10, txt(0x182), 2);
    } else {
        __Menu_Send(index, 0x10000120, 0x10, txt(with_continue == 0 ? 0x266 : 0x10001d6), 3);
    }
    M_MANAGER* m = mgr(index);
    Manager_SendMessage(m, 0x44, 0x4000002f, 2);
    M_BUTTON* box = ptr<M_BUTTON>(__Menu_Send(index, 0x1000011e, 0x39, 0, 0));
    M_MEMOBOX* memo = ptr<M_MEMOBOX>(__Menu_Send(index, 0x1000011f, 0x39, 0, 0));
    M_LISTBOX* list = ptr<M_LISTBOX>(__Menu_Send(index, 0x10000120, 0x39, 0, 0));
    if (box == nullptr || memo == nullptr || list == nullptr) {
        ++g_menu_handler_guards.option_box;  // o original leria por nulo
        return;
    }
    __Menu_SendMessage(&memo->base, 0x66, 0, 0);
    __Menu_SendMessage(&memo->base, 0x75, 100, 0);
    const uint32_t lines = uint32_t(__Menu_SendMessage(&memo->base, 0x65, 0, 0));
    const uint32_t options = uint32_t(__Menu_Send(index, 0x10000120, 0x33, 0, 0));
    const int16_t text_h = int16_t((lines & 0xff) * (uint16_t(int32_t(memo->line_height)) & 0xff));
    const int16_t total = int16_t(text_h + (options & 0xff) * 0x15);
    const int16_t height = int16_t((options & 0xff) == 0 ? total + 0x1c : total + 0x36);
    box->base.h = height;
    const int16_t y = int16_t(int32_t((448.0f - float(height)) * 0.5f));
    box->base.y = y;
    memo->base.y = int16_t(y + 10);
    list->base.y = int16_t(y + text_h + 0x1e);
    m->input = 0;
    Memo_Update(m, reinterpret_cast<M_PAGE*>(nf::raw(memo->base.owner)), memo);
    List_Update(m, reinterpret_cast<M_PAGE*>(nf::raw(list->base.owner)), list);
    Button_Update(m, reinterpret_cast<M_PAGE*>(nf::raw(box->base.owner)), box);
    m->input = 1;
    Manager_SendMessage(m, 0x22, 0x10000120, 0);
}

// [decomp] 0x00202218 Menu_CreateOptionBox
void Menu_CreateOptionBox(uint8_t index, uint32_t label, uint32_t id, uint8_t ok_only, uint8_t with_continue) {
    Menu_CreateOptionBox(index, Txt_BindLabel(label, 0), id, ok_only, with_continue);
}

// [decomp] 0x00202280 Menu_UpdateOptionBox
// A resposta da caixa (uma vez; 0 sem resposta) e o id dela.
uint32_t Menu_UpdateOptionBox(uint32_t* id) {
    const uint32_t r = option_return;
    option_return = 0;
    if (id != nullptr) *id = mb_id;
    return r;
}

// [decomp] 0x002022a0 C_LBMSGOPTIONS_Handler
// A lista da caixa de mensagem: escolher devolve o valor da linha; voltar, 2 (ou 4 com só "ok").
int32_t C_LBMSGOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        option_return = uint32_t(__Menu_SendMessage(c, 0x35, 0, 0));
        Manager_SendMessage(mgr(index), 0x5f, 1, 0);
    } else if (message == 0x6d) {
        Manager_SendMessage(mgr(index), 0x5f, 1, 0);
        option_return = mb_ok_only != 0 ? 4 : 2;
    }
    return 1;
}

// ---- medalhas, bônus e perfil ----

// [decomp] 0x002023e8 Menu_GetLevelIndex
// O índice da missão no menu (sp_level +0xc), 0xffffffff fora dele.
uint32_t Menu_GetLevelIndex(uint32_t level) {
    for (uint32_t i = 0; i < 12; ++i) {
        if (rd32(sp_level + i * 0x18 + 0xc) == level) return i;
    }
    return 0xffffffffu;
}

// [decomp] 0x00202360 Menu_GetNightfireStatus
// Um bit por missão concluída (sp_level +0x10).
uint32_t Menu_GetNightfireStatus() {
    uint32_t bits = 0;
    for (uint32_t i = 0; i < 12; ++i) {
        if (sp_level[i * 0x18 + 0x10] != 0) bits |= 1u << i;
    }
    return bits;
}

// [decomp] 0x00202430 Menu_GetBonus
uint64_t Menu_GetBonus(uint8_t player) { return *bonus_of(player); }

// [decomp] 0x001ff898 Menu_SetBonus
// Grava os bits de medalha do jogador (ou soma, com merge e sem a trapaça +0xa5) e refaz as melhorias
// de arma das medalhas de tipo 4.
void Menu_SetBonus(uint64_t bits, uint8_t player, int8_t merge) {
    if (menu_bonus[0xa5] == 0 && merge != 0) {
        *bonus_of(player) |= bits;
    } else {
        *bonus_of(player) = bits;
    }
    ResetUpgrade(player);
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t level = rd32(sp_level + i * 0x18 + 0xc);
        for (uint32_t medal = 1; medal < 5; ++medal) {
            REWARDINFO_tag info{};
            PlrStarts_ProcessRewardCounter(bonus_of(player), level, medal, 0, &info);
            if (info.has != 0 && info.kind == 4) Set_Upgrade(player, int16_t(info.bit), int16_t(info.count));
        }
    }
}

// [decomp] 0x001ffa10 Menu_SetLevelBonus
// A missão foi cumprida com a medalha `medal` (1-4; 5 nenhuma): marca a missão e libera a seguinte,
// liga os bits das medalhas até ela que faltavam (bonus +0xa4: ganhou algo novo). 1 se é a última.
uint32_t Menu_SetLevelBonus(uint32_t level, uint32_t medal, uint8_t player) {
    menu_bonus[0xa4] = 0;
    uint64_t bits = *bonus_of(0);
    const uint32_t i = Menu_GetLevelIndex(level);
    // Só no port: não escreve fora da tabela (fase fora do menu ou a última missão).
    if (i < 12) {
        sp_level[i * 0x18 + 0x10] = 1;
    } else {
        ++g_menu_handler_guards.level_index;
    }
    if (level != 0x700001b) {
        if (i + 1 < 12) {
            sp_level[(i + 1) * 0x18 + 0x10] = 1;
        } else {
            ++g_menu_handler_guards.level_index;
        }
    } else {
        GameState.unknown_52 = 1;
    }
    REWARDINFO_tag info{};
    if (medal < 5 && 0 < int32_t(medal)) {
        do {
            PlrStarts_ProcessRewardCounter(&bits, level, medal, 0, &info);
            if (info.has == 0) {
                PlrStarts_ProcessRewardCounter(&bits, level, medal, 1, &info);
                menu_bonus[0xa4] = 1;
            }
            --medal;
        } while (0 < int32_t(medal));
    }
    Menu_SetBonus(bits, player, 0);
    return level == 0x700001b ? 1 : 0;
}

// [decomp] 0x001ffe98 Menu_GetLevelBonuses
// As quatro medalhas da fase: ganha (+4, +0x10, +0x1c, +0x28), rótulo e imagem de cada uma.
uint8_t* Menu_GetLevelBonuses(uint32_t level) {
    std::memset(b_265, 0, sizeof(b_265));
    for (uint32_t i = 0; i < PlrStarts_GetRewardsTableSize(); ++i) {
        const uint8_t* e = RewardsTable + i * 0x40;
        if (rd32(e) != level) continue;
        b_265[4] = Menu_HasMedal(level, 1, 0);
        wr32(b_265 + 8, rd32(e + 0x1c));
        wr32(b_265 + 0xc, rd32(e + 0x30));
        b_265[0x10] = Menu_HasMedal(level, 2, 0);
        wr32(b_265 + 0x14, rd32(e + 0x20));
        wr32(b_265 + 0x18, rd32(e + 0x34));
        b_265[0x1c] = Menu_HasMedal(level, 3, 0);
        wr32(b_265 + 0x20, rd32(e + 0x24));
        wr32(b_265 + 0x24, rd32(e + 0x38));
        b_265[0x28] = Menu_HasMedal(level, 4, 0);
        wr32(b_265 + 0x30, rd32(e + 0x3c));
        wr32(b_265 + 0x2c, rd32(e + 0x28));
        break;
    }
    return b_265;
}

// [decomp] 0x00202580 Menu_HasMedal
uint8_t Menu_HasMedal(uint32_t level, uint32_t medal, uint8_t player) {
    REWARDINFO_tag info{};
    PlrStarts_ProcessRewardCounter(bonus_of(player), level, medal, 0, &info);
    return info.has;
}

// [decomp] 0x002024e8 Menu_GetBestMedal
int32_t Menu_GetBestMedal(uint32_t level, uint8_t player) {
    REWARDINFO_tag info{};
    for (int32_t medal = 4; medal != 0; --medal) {
        PlrStarts_ProcessRewardCounter(bonus_of(player), level, uint32_t(medal), 0, &info);
        if (info.has != 0) return medal;
    }
    return 0;
}

// [decomp] 0x001f8218 Menu_UpdateDefaultCodename
// Copia as opções do jogador para o perfil padrão `slot` (def_codename, 0x38 bytes cada).
void Menu_UpdateDefaultCodename(uint8_t player, uint8_t slot) {
    uint8_t* d = def_codename + slot * 0x38;
    const PlayerInput_tag& p = PlayerSetting[player];
    d[0x27] = p.crouch_toggle;
    std::memcpy(d + 0x22, &p.unknown_10, 2);
    std::memcpy(d + 0x1c, MPSettings + player * 0x30 + 0x2c, 2);
    d[0x1e] = p.invert_look;
    d[0x25] = p.autoaim_mp;
    d[0x24] = p.autoaim;
    d[0x2a] = p.auto_switch;
    std::memcpy(d + 0x20, &p.style, 2);
    d[0x29] = p.unknown_08;
    d[0x26] = p.aim_toggle;
    wr32(d + 0x10, SFXMusicGetVolume());
    d[0x2d] = uint8_t(DrawInfo[0]);
    d[0x2e] = MPSettings[0x1bc];
    d[0x1a] = MPSettings[player * 0x30 + 0x28];
    d[0x2f] = MPSettings[0x1c0];
    wr32(d + 0x14, SFXGetVolume());
    d[0x18] = uint8_t(DrawInfo[2]);
    d[0x28] = p.unknown_09;
    d[0x30] = MPSettings[0x1c4];
    wr32(d + 4, Menu_GetNightfireStatus());
    const uint64_t b = Menu_GetBonus(player);
    std::memcpy(d + 8, &b, 8);
    d[0x19] = GameState.unknown_52;
    d[0x2b] = p.unknown_0c;
    d[0x2c] = p.unknown_0b;
}

// [decomp] 0x001f3100 Menu_ChangeControllerStyle
void Menu_ChangeControllerStyle(int16_t player, uint32_t style) {
    if (player == -1) {
        // Peculiaridade do original: o jogador 0 recebe o estilo que estiver no registro (o compilador
        // não passa `style`); o port passa o mesmo.
        Input_ChangeControllerStyle(0, style);
        Input_ChangeControllerStyle(1, style);
        Input_ChangeControllerStyle(2, style);
        Input_ChangeControllerStyle(3, style);
    } else {
        Input_ChangeControllerStyle(player, style);
    }
}

// [decomp] 0x00199210 PlrStarts_ProcessRewardCounter
// A medalha `medal` (1-4) da fase na RewardsTable: o tipo e o bit em `bits` (o tipo 4 é um grupo de
// fichas espalhadas pelas fases, contadas); com `set`, liga o bit.
void PlrStarts_ProcessRewardCounter(uint64_t* bits, uint32_t level, uint32_t medal, int8_t set, REWARDINFO_tag* out) {
    out->kind = 5;
    out->has = 0;
    out->bit = 0;
    out->count = 0;
    if (4 < medal) return;
    uint32_t entry = 0;
    while (entry < 12 && rd32(RewardsTable + entry * 0x40) != level) ++entry;
    if (entry >= 12) return;
    const uint8_t* word = RewardsTable + entry * 0x40 + 4 + medal * 4;
    uint16_t kind;
    uint16_t bit;
    std::memcpy(&bit, word, 2);
    std::memcpy(&kind, word + 2, 2);
    if (4 < kind) return;
    out->kind = kind;
    if (0x3e < bit) return;
    out->bit = bit;
    int32_t mine = 0;
    int32_t seen = 0;
    uint32_t count = 0;
    if (kind == 4) {
        const uint32_t key = rd32(word);
        for (uint32_t e = 0; e < 12; ++e) {
            for (uint32_t k = 0; k < 4; ++k) {
                if (rd32(RewardsTable + e * 0x40 + 8 + k * 4) != key) continue;
                if (e == entry) mine = seen;
                if ((*bits & (uint64_t(1) << (bit + seen))) != 0) ++count;
                ++seen;
            }
        }
    }
    const uint64_t mask = uint64_t(1) << (bit + mine);
    out->has = (*bits & mask) != 0 ? 1 : 0;
    if (set != 0) {
        ++count;
        *bits |= mask;
    }
    if (kind == 4) out->count = count;
}

// [decomp] 0x00199b68 PlrStarts_GetRewardsTableSize
uint32_t PlrStarts_GetRewardsTableSize() { return 0xc; }

// [decomp] 0x00199ac0 PlrStats_DoneBetter
uint8_t PlrStats_DoneBetter() { return DoneBetter; }

// [decomp] 0x001a98a8 Set_Upgrade
void Set_Upgrade(uint8_t player, int16_t slot, int16_t value) {
    if (slot / 3 < 9) Upgrades[slot / 3 + player * 9] = uint8_t(value);
}

// [decomp] 0x001a9900 ResetUpgrade
void ResetUpgrade(uint8_t player) { std::memset(Upgrades + player * 9, 0, 9); }

// [decomp] 0x001dec78 SeparateNumber
// O número com separadores de milhar ("1,234,567").
int32_t SeparateNumber(uint32_t n, char* out) {
    const int32_t v = int32_t(n);
    const int32_t thousands = (v / 1000) % 1000;
    if ((v / 1000) / 1000 == 0) {
        if (thousands == 0) return std::sprintf(out, "%d", v % 1000);
        return std::sprintf(out, "%d,%.3d", thousands, v % 1000);
    }
    return std::sprintf(out, "%d,%.3d,%.3d", (v / 1000) / 1000, thousands, v % 1000);
}

// [decomp] 0x001deaa8 Timer_Seconds2String
// Centésimos de segundo em "mm:ss" (ou "hh:mm:ss"), num de quatro buffers que se revezam.
char* Timer_Seconds2String(int32_t hundredths, uint32_t format) {
    char* s = Buf_39[Cnt_40];
    Cnt_40 = (Cnt_40 + 1) & 3;
    const int32_t t = int32_t(uint32_t(int64_t(float(hundredths) * 0.01f + 0.5f)));
    int32_t sec = t % 0x3c;
    int32_t min = (t / 0x3c) % 0x3c;
    int32_t hour = (t / 0xe10) % 0x3c;
    if (hundredths < 0) {
        hour = 0;
        min = 0;
        sec = 0;
    }
    switch (format) {
        default:
            if (hour != 0) {
                std::snprintf(s, 0x100, "%.2d:%.2d:%.2d", hour, min, sec);
                return s;
            }
            [[fallthrough]];
        case 2:
            std::snprintf(s, 0x100, "%.2d:%.2d", min, sec);
            break;
        case 1:
            std::snprintf(s, 0x100, "%.2d:%.2d:%.2d", hour, min, sec);
            break;
        case 3:
            std::snprintf(s, 0x100, "%.2d:%.2d:%.2d", min, sec, 0);
            break;
        case 4:
            std::snprintf(s, 0x100, "%.2d:%.2d", sec, 0);
            break;
    }
    return s;
}

namespace {

// Uma linha das estatísticas: "rótulo|a|b|pontos" dividido nas colunas (List_SendMessage 0xf).
void add_split_row(uint8_t index, char* text) {
    alignas(4) uint8_t row[0x2d0]{};
    LIST_ROW* r = reinterpret_cast<LIST_ROW*>(row);
    char* s = text;
    for (uint32_t i = 0; s != nullptr && i < 4; ++i) {
        char* bar = std::strstr(s, "|");
        if (bar != nullptr) *bar = 0;
        std::snprintf(r->cells[i].text, sizeof(r->cells[i].text), "%s", s);
        s = bar != nullptr ? bar + 1 : nullptr;
    }
    __Menu_Send(index, 0x10000022, 0xf, val(row), 0);
}

}  // namespace

// [decomp] 0x00201c10 Menu_AddRow
void Menu_AddRow(uint8_t index, uint32_t label, uint32_t a, uint32_t b, uint32_t score, int8_t indent, int8_t times) {
    char* s = Txt_GetStringFromHeap(0);
    if (s == nullptr) return;
    char points[0x40];
    SeparateNumber(score, points);
    const char* name = Txt_BindLabel(label, 0);
    if (times != 0) {
        const char* ta = Timer_Seconds2String(int32_t(a), 0);
        const char* tb = Timer_Seconds2String(int32_t(b), 0);
        std::snprintf(s, 0x168, "%s|%s|%s|%s", name, ta, tb, points);
    } else {
        std::snprintf(s, 0x168, indent != 0 ? "  %s|%d|%d|%s" : "%s|%d|%d|%s", name, int32_t(a), int32_t(b), points);
    }
    add_split_row(index, s);
}

// [decomp] 0x00201df0 Menu_AddRowPercentage
void Menu_AddRowPercentage(uint8_t index, uint32_t label, uint32_t percent, int32_t target, uint32_t score, int8_t indent) {
    char* s = Txt_GetStringFromHeap(0);
    if (s == nullptr) return;
    char points[0x40];
    char goal[0x40];
    SeparateNumber(score, points);
    if (target == -1) {
        std::snprintf(goal, sizeof(goal), "----");
    } else {
        std::snprintf(goal, sizeof(goal), "%d%%", target);
    }
    std::snprintf(s, 0x168, indent != 0 ? "  %s|%d%%|%s|%s" : "%s|%d%%|%s|%s", Txt_BindLabel(label, 0), int32_t(percent),
                  goal, points);
    add_split_row(index, s);
}

// ---- tabela dos tratadores ----

namespace {

// Os 103 identificadores com tratador em Handler_HandleMessage (0x002029e0); handler nulo: stub.
const MenuHandlerEntry kHandlers[] = {
    {0x10000002, "C_GONIGHTFIRE_Handler", C_GONIGHTFIRE_Handler},
    {0x10000003, "C_GOMULTIPLAYER_Handler", C_GOMULTIPLAYER_Handler},
    {0x10000004, "C_GOCODENAMES_Handler", C_GOCODENAMES_Handler},
    {0x10000006, "C_SBMPMAP_Handler", nullptr},
    {0x1000000e, "C_SBNFMAP_Handler", C_SBNFMAP_Handler},
    {0x10000010, "C_LBPMMAP_Handler", C_LBPMMAP_Handler},
    {0x10000012, "C_CHCHWS_Handler", C_CHCHWS_Handler},
    {0x10000015, "C_CHCHDRONES_Handler", C_CHCHDRONES_Handler},
    {0x10000016, "C_CHCHBLIND_Handler", C_CHCHBLIND_Handler},
    {0x10000017, "C_CHCHDEBUG_Handler", C_CHCHDEBUG_Handler},
    {0x10000018, "C_CHCHZEROG_Handler", C_CHCHZEROG_Handler},
    {0x10000019, "C_CHCHFLY_Handler", C_CHCHFLY_Handler},
    {0x1000001a, "C_CHCHCOORDS_Handler", C_CHCHCOORDS_Handler},
    {0x1000001b, "C_CHCHMUSIC_Handler", C_CHCHMUSIC_Handler},
    {0x1000001c, "C_CHCHHUD_Handler", C_CHCHHUD_Handler},
    {0x10000025, "C_RBCONTROL_Handler", C_RBCONTROL_Handler},
    {0x10000028, "C_GCPAUSE_Handler", C_GCPAUSE_Handler},
    {0x1000003e, "C_NIS_Handler", C_NIS_Handler},
    {0x10000074, "C_KEYBOARD_Handler", C_KEYBOARD_Handler},
    {0x1000009c, "C_SBMPSCEN_Handler", nullptr},
    {0x100000ba, "C_KEYPAD_Handler", C_KEYPAD_Handler},
    {0x100000bd, "C_CHCHDUMMY_Handler", C_CHCHDUMMY_Handler},
    {0x100000bf, "C_SBNFDFCTY_Handler", C_SBNFDFCTY_Handler},
    {0x100000c7, "C_CHCHALLOWFREEZE_Handler", C_CHCHALLOWFREEZE_Handler},
    {0x100000e5, "C_SBNFCN_Handler", C_SBNFCN_Handler},
    {0x100000e7, "C_CHCHWEAP_Handler", C_CHCHWEAP_Handler},
    {0x100000e8, "C_CHCHHEALTH_Handler", C_CHCHHEALTH_Handler},
    {0x100000eb, "C_SBCNSELECT_Handler", C_SBCNSELECT_Handler},
    {0x100000f4, "C_SBMPOPTIONS_Handler", nullptr},
    {0x100000fc, "C_LBERROPTIONS_Handler", C_LBERROPTIONS_Handler},
    {0x10000100, "C_SBCNOPTIONS_Handler", C_SBCNOPTIONS_Handler},
    {0x10000101, "C_CHCHDRAWALL_Handler", C_CHCHDRAWALL_Handler},
    {0x1000010c, "C_SBDOSSIER_Handler", C_SBDOSSIER_Handler},
    {0x10000114, "C_SBBOTS_Handler", nullptr},
    {0x10000120, "C_LBMSGOPTIONS_Handler", C_LBMSGOPTIONS_Handler},
    {0x1000015d, "C_MPDBG_Handler", nullptr},
    {0x1000015e, "C_LANGUAGE_Handler", C_LANGUAGE_Handler},
    {0x10000169, "C_CHCHLOCKUP_Handler", C_CHCHLOCKUP_Handler},
    {0x1000016f, "C_SBDSGTSCROLL_Handler", C_SBDSGTSCROLL_Handler},
    {0x10000173, "C_SBDSWPSCROLL_Handler", C_SBDSWPSCROLL_Handler},
    {0x10000174, "C_RBDSRECORDS_Handler", C_RBDSRECORDS_Handler},
    {0x1000017a, "C_RBDSREWARDS_Handler", C_RBDSREWARDS_Handler},
    {0x10000193, "C_SBMPBTCHOOSE_Handler", nullptr},
    {0x1000019f, "C_RBMPSTART_Handler", nullptr},
    {0x100001a1, "C_RBMPSETUP_Handler", nullptr},
    {0x100001eb, "C_SBSCREEN_Handler", C_SBSCREEN_Handler},
    {0x100001f7, "C_CHCHBRIGHT_Handler", C_CHCHBRIGHT_Handler},
    {0x10000216, "C_CHCHUNLOCK_Handler", C_CHCHUNLOCK_Handler},
    {0x1000021d, "C_CHCHCONTROLS_Handler", C_CHCHCONTROLS_Handler},
    {0x1000022a, "C_RBMPFINISH_Handler", nullptr},
    {0x1000022b, "C_RBMPCNAME_Handler", nullptr},
    {0x40000002, "P_MAIN_Handler", P_MAIN_Handler},
    {0x40000009, "P_START_Handler", P_START_Handler},
    {0x4000000c, "P_NIS_Handler", P_NIS_Handler},
    {0x40000012, "P_MPOPTIONS_Handler", nullptr},
    {0x40000013, "P_MPMAP_Handler", nullptr},
    {0x40000014, "P_MPRULES_Handler", nullptr},
    {0x40000017, "P_MPPLAYERMODS_Handler", nullptr},
    {0x40000019, "P_MPJOIN_Handler", nullptr},
    {0x4000001a, "P_MPSCENARIO_Handler", nullptr},
    {0x4000001b, "P_CNSELECT_Handler", P_CNSELECT_Handler},
    {0x4000001c, "P_NFMAP_Handler", P_NFMAP_Handler},
    {0x4000001d, "P_CNMENU_Handler", P_CNMENU_Handler},
    {0x40000020, "P_CNNAME_Handler", P_CNNAME_Handler},
    {0x40000022, "P_CNCONTROLS_Handler", P_CNCONTROLS_Handler},
    {0x40000023, "P_NFDFCTY_Handler", P_NFDFCTY_Handler},
    {0x40000025, "P_NFSELECT_Handler", P_NFSELECT_Handler},
    {0x40000027, "P_MPBOTS_Handler", nullptr},
    {0x40000028, "P_MPENVIROMODS_Handler", nullptr},
    {0x4000002b, "P_DOSSIER_Handler", P_DOSSIER_Handler},
    {0x4000002c, "P_MPBOTSETUP_Handler", nullptr},
    {0x4000002d, "P_CNOPTIONS_Handler", P_CNOPTIONS_Handler},
    {0x4000002e, "P_CNMPOPTIONS_Handler", P_CNMPOPTIONS_Handler},
    {0x40000030, "P_CREDITS_Handler", P_CREDITS_Handler},
    {0x40000031, "P_CNAVOPTIONS_Handler", P_CNAVOPTIONS_Handler},
    {0x40000032, "P_INTRO_Handler", P_INTRO_Handler},
    {0x40000033, "P_MPDEBRIEFING_Handler", nullptr},
    {0x40000034, "P_LANGUAGE_Handler", P_LANGUAGE_Handler},
    {0x40000035, "P_ATTRACT_Handler", P_ATTRACT_Handler},
    {0x40000036, "P_NFRESULTS_Handler", P_NFRESULTS_Handler},
    {0x40000037, "P_NFSTATS_Handler", P_NFSTATS_Handler},
    {0x40000038, "P_NFBONUS_Handler", P_NFBONUS_Handler},
    {0x4000003a, "P_DSRECORDS_Handler", P_DSRECORDS_Handler},
    {0x4000003b, "P_DSREWARDS_Handler", P_DSREWARDS_Handler},
    {0x4000003c, "P_DSGADGETS_Handler", P_DSGADGETS_Handler},
    {0x4000003d, "P_DSWEAPONS_Handler", P_DSWEAPONS_Handler},
    {0x4000003f, "P_MPBOTCHOOSE_Handler", nullptr},
    {0x40000042, "P_ENDMISSION_Handler", P_ENDMISSION_Handler},
    {0x40000043, "P_ESTHERO_Handler", P_ESTHERO_Handler},
    {0x40000044, "P_TWEAKS_Handler", P_TWEAKS_Handler},
    {0x40000046, "P_TWEAKS2_Handler", P_TWEAKS2_Handler},
    {0x40000047, "P_SCREENADJUST_Handler", P_SCREENADJUST_Handler},
    {0x40000048, "P_PS2MEMCARDINIT_Handler", P_PS2MEMCARDINIT_Handler},
    {0x40000049, "P_MPCONFIRM_Handler", nullptr},
    {0x4000004a, "P_PARISENUM_Handler", P_PARISENUM_Handler},
    {0x4000004b, "P_PAUSE_Handler", P_PAUSE_Handler},
    {0x4000004c, "P_CHEATMEDAL_Handler", P_CHEATMEDAL_Handler},
    {0x4000004d, "P_FMV_Handler", P_FMV_Handler},
    {0x4000004e, "P_TRAILER_Handler", P_TRAILER_Handler},
    {0x4000004f, "P_FMVTEST_Handler", nullptr},
    {0x40000050, "P_FMVPLAYER_Handler", P_FMVPLAYER_Handler},
    {0x40000051, "P_MPSETUP_Handler", nullptr},
    {0x40000053, "P_WINGAME_Handler", P_WINGAME_Handler},
};

}  // namespace

const MenuHandlerEntry* Menu_FindHandler(uint32_t id) {
    const auto& extra = port_handlers();
    const auto it = extra.find(id);
    if (it != extra.end()) return &it->second;
    for (const MenuHandlerEntry& e : kHandlers) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void Menu_PortRegisterHandler(uint32_t id, const char* name, MenuHandler handler) {
    auto& extra = port_handlers();
    if (handler == nullptr) {
        extra.erase(id);
    } else {
        extra[id] = MenuHandlerEntry{id, name, handler};
    }
}

// ---- só no port ----

uint32_t Menu_CurrentPage(uint8_t index) {
    const M_MANAGER& m = menu_manager[index];
    return m.active != 0 && m.page != nullptr ? m.page->base.id : 0;
}

bool Menu_Focus(uint8_t index, uint32_t* id, uint32_t* tag) {
    const M_MANAGER& m = menu_manager[index];
    if (m.active == 0 || m.page == nullptr || m.page->focus == nullptr) return false;
    if (id != nullptr) *id = m.page->focus->id;
    if (tag != nullptr) *tag = m.page->focus->tag;
    return true;
}

int Menu_Describe(char* out, size_t size) {
    int n = std::snprintf(out, size, "menus status %u criados %u fechados %u paginas %u controles %u",
                          MenuManager_GetStatus(), g_menu_stats.created, g_menu_stats.deleted, g_menu_stats.pages,
                          g_menu_stats.controls);
    for (int i = 0; i < kMenuManagers && 0 <= n && size_t(n) < size; ++i) {
        const M_MANAGER& m = menu_manager[i];
        if (m.active == 0) continue;
        uint32_t id = 0, tag = 0;
        Menu_Focus(uint8_t(i), &id, &tag);
        n += std::snprintf(out + n, size - size_t(n), " | gerente %d pagina %08x foco %08x/%u", i,
                           Menu_CurrentPage(uint8_t(i)), id, tag);
    }
    return n;
}

void menu_handlers_debug_power_on() {
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    std::memset(ls, 0, sizeof(ls));
    std::memset(def_codename, 0, sizeof(def_codename));
    menu_movie = 0;
    DoneBetter = 0;
    state_quit_166 = 0;
    state_ignore_167 = 0;
    option_return = 0;
    mb_id = 0;
    mb_ok_only = 0;
    score_table = nullptr;
    just_completed = 0;
    xbox_save_flag_272 = 0;
    Cnt_40 = 0;
    g_menu_handler_guards = MenuHandlerGuards{};
    port_handlers().clear();
    // O menu principal, o perfil gravado e o cartão (em memória, vazio).
    menu_frontend_debug_power_on();
    menu_codename_debug_power_on();
    menu_options_debug_power_on();
    menu_tweaks_debug_power_on();
    // O dossiê e as trapaças.
    menu_dossier_debug_power_on();
    menu_cheats_debug_power_on();
    loadsave_debug_power_on();
    psi_card_debug_power_on();
    // Os volumes e o modo do som do disco e as opções que o nightfire.ini lembraria (nenhuma).
    psi_sound_debug_power_on();
    nf::game_options_debug_power_on();
}
