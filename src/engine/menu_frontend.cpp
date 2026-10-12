// O menu principal: o idioma, os filmes de abertura, "Press START", o menu principal, a escolha da missão
// (codinome, dificuldade, mapa), o fim do jogo, e as rodas e o íris que essas páginas usam. Ver
// menu_frontend.h; os codinomes e o cartão estão em menu_codename.cpp.
//
// Divergências (comentadas nas funções):
// - C_GOMULTIPLAYER_Handler: o multijogador não foi portado (a caixa de aviso no lugar da página 0x40000019).
// - P_NFMAP_Handler: depois de uma missão de carro pulada (Menu_PortDrivingSkipped), a caixa de aviso.
// - Menu_UpdateWheel: um item fora da roda (999) conta como bloqueado (o original leria além da tabela).
// - P_NFMAP_Handler: a busca da última missão liberada para em 0 (o original desceria abaixo de sp_level).
// - P_START_Handler: mp_stuff (o multijogador) não é zerado.
// - Menu_WillRunMiniMission: depois das legendas dos perfis padrão, as que o nightfire.ini tem valem por cima
//   (platform/game_options.h, regra 3).

#include "engine/menu_frontend.h"

#include <cstdio>
#include <cstring>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/pc_text.h"
#include "engine/pickup.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/psi.h"

extern uint32_t PS2FramesToSkip;  // menu.cpp

char codename_buf[kCodenames][0x20];
M_ITEM cn_options[7];
M_ITEM difficulty[3];
MENU_IRIS menu_iris;
uint8_t movie_skip = 0;
uint8_t movie_sound = 0;
uint32_t movie_counter = 0;
uint8_t menu_unlock_everything = 0;
uint8_t bDisplayLangMenu = 0;
uint8_t bSkipAttract = 0;
uint8_t RUN_MINI_MISSION = 0;
uint32_t FlagsToDisplay = 0;
uint32_t DefaultFlags = 0x13f;
uint32_t medal_from_cheat = 0;
uint32_t movie_hashcode = 0;

namespace {

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }
template <class T>
T* ptr(int32_t v) {
    return reinterpret_cast<T*>(uintptr_t(uint32_t(v)));
}
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }
int32_t txt(uint32_t label) { return val(Txt_BindLabel(label, 0)); }
M_ITEM* sp_items() { return reinterpret_cast<M_ITEM*>(sp_level); }
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

char kSpace[] = " ";  // 0x0030d240 e 0x0030d380

// Estado das páginas.
uint8_t movie_291 = 0;            // P_ATTRACT_Handler: alterna os dois filmes
uint32_t movie_283 = 0;           // P_INTRO_Handler: o filme da vez
uint8_t once_295 = 0;             // P_START_Handler: o texto legal já esperou
char legal_buf_296[0x200];        // 0x0034d0c8
uint8_t level_chosen_302 = 0;     // P_PARISENUM_Handler (a minimissão)
uint32_t frame_count_287 = 0;     // P_ESTHERO_Handler
uint32_t frame_count_322 = 0;     // P_FMVPLAYER_Handler
uint8_t level_chosen = 0;         // 0x0030d382 P_NFMAP_Handler: missão de carro escolhida (conta até 4)
uint8_t closing_326 = 0;          // P_NFSELECT_Handler
uint8_t select_def_codename_327 = 0;
// Só no port: a caixa do multijogador (C_GOMULTIPLAYER_Handler) e a da missão de carro pulada (P_NFMAP_Handler)
// esperam a resposta para não deixá-la a outra página (Menu_UpdateOptionBox só responde uma vez).
uint8_t port_box_main = 0;
uint8_t port_driving_notice = 0;  // 1 mostrar, 2 mostrada
constexpr uint32_t kPortBoxId = 0x60;  // id das caixas do port (os do jogo vão até 0xd)

// O índice dos quatro jogadores nos controles (PlayerSetting::index), como cada página do menu os deixa.
void reset_player_indices() {
    PlayerSetting[3].index = 3;
    PlayerSetting[2].index = 2;
    PlayerSetting[0].index = 0;
    PlayerSetting[1].index = 1;
}

// A música do menu (os dois sons de fundo), se nenhum filme a trocou.
void menu_music() {
    if (menu_movie != 0) return;
    SFXRemove(0, 0x1d7);
    SFXRemove(0, 0x470);
    SFXStart(0x1d7, 0);
    SFXStart(0x470, 0);
    menu_movie = 1;
}

// Um processo de cor (0xe1) de 15 ticks no controle `id` até `colour` (o fade da página).
void fade(uint8_t index, uint32_t id, uint32_t colour) {
    M_MESSAGE msg{};
    msg.message = 0xe1;
    msg.a = int32_t(colour);
    msg.b = 0;
    Process_Create(ptr<M_CONTROL>(__Menu_Send(index, id, 0x39, 0, 0)), &msg, 0xf, 0);
}

// Começa um filme de página (o som do menu sai, a página congela).
void start_movie(uint32_t movie, uint32_t counter) {
    movie_sound = 1;
    movie_skip = 1;
    movie_counter = counter;
    SFXRemove(0, 0x1d7);
    SFXRemove(0, 0x470);
    menu_movie = 0;
    psiStartBackgroundMovie(movie, 0, SFXGetVolume());
    Manager_SendMessage(&menu_manager[0], 0x68, 1, 0);
}

// O filme acabou? Volta a música do menu e descongela.
bool movie_finished() {
    if (!psiMovieFinished()) return false;
    if (movie_sound != 0 && menu_movie == 0) {
        SFXRemove(0, 0x1d7);
        SFXRemove(0, 0x470);
        SFXStart(0x1d7, 0);
        SFXStart(0x470, 0);
        menu_movie = 1;
    }
    Manager_SendMessage(&menu_manager[0], 0x68, 0, 0);
    return true;
}

// Para o filme pulável (o som volta).
void stop_skippable_movie() {
    if (movie_skip == 0) return;
    if (movie_sound != 0) {
        SFXUnPause();
        SFXUnPauseAllStreams();
    }
    psiStopBackgroundMovie();
    Manager_SendMessage(&menu_manager[0], 0x68, 0, 0);
}

// O texto de ajuda de C_LANGUAGE_Handler e P_LANGUAGE_Handler em cada idioma (strings do ELF, Latin-1).
const char* language_help(int32_t language) {
    switch (language) {
        case 0: return "~A To Select";
        case 1: return "~A pour s\xe9lectionner";
        case 2: return "~A um auszuw\xe4hlen";
        case 3: return "~A Para Seleccionar";
        case 4: return "~A Per Selezionare";
        case 5: return "~A Selecteren";
        case 8: return "~A f\xf6r att v\xe4lja";
        default: return nullptr;
    }
}

// O idioma do som (SFXSetLanguage): 1 francês, 2 alemão, 3 espanhol, o resto inglês.
uint32_t sound_language(int32_t language) {
    if (language == 2) return 2;
    if (language < 3) return language == 1 ? 1 : 0;
    return language == 3 ? 3 : 0;
}

}  // namespace

// ---- idioma, filmes de abertura, Press START ----

// [decomp] 0x00208830 P_LANGUAGE_Handler
// A escolha do idioma (0x40000034, a primeira página do menu principal): sem bDisplayLangMenu (a versão
// americana) carrega LanguageToLoad e vai ao cartão (0x40000048); senão mostra só as bandeiras de
// FlagsToDisplay, centradas na vertical, com o foco no idioma atual.
int32_t P_LANGUAGE_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x4c) return 1;
    __Menu_Send(index, 0x1000015e, 0x70, 1, 0);
    PlayerSetting[3].index = 3;
    menu_ls().done = 0;
    PlayerSetting[1].index = 1;
    PlayerSetting[2].index = 2;
    PlayerSetting[0].index = 0;
    if (bDisplayLangMenu == 0 && LanguageToLoad != 9) {
        Txt_SetLanguage(LanguageToLoad);
        SFXSetLanguage(sound_language(LanguageToLoad));
        Manager_SendMessage(mgr(index), 0x44, 0x40000048, 1);
        return 1;
    }
    // A tag da bandeira de cada idioma (0 inglês, 1 alemão, 2 francês, 3 italiano, 4 holandês, 5 espanhol,
    // 6 sueco).
    const auto tag_of = [](int32_t language) -> int32_t {
        switch (language) {
            case 0: return 0;
            case 1: return 2;
            case 2: return 1;
            case 3: return 5;
            case 4: return 3;
            case 5: return 4;
            case 8: return 6;
            default: return -1;
        }
    };
    const int32_t focus = tag_of(LanguageToLoad);
    Manager_SendMessage(mgr(index), 0x22, 0,
                        __Menu_SendEx(index, 0x1000015e, uint32_t(focus != -1 ? focus : 0), 0x39, 0, 0));
    if (const char* help = language_help(LanguageToLoad)) __Menu_Send(index, 0x1000006f, 0x18, val(help), 0);
    if (FlagsToDisplay == 0) FlagsToDisplay = DefaultFlags;
    M_CONTROL* first = ptr<M_CONTROL>(__Menu_SendEx(index, 0x1000015e, 0, 0x39, 0, 0));
    M_CONTROL* second = ptr<M_CONTROL>(__Menu_SendEx(index, 0x1000015e, 1, 0x39, 0, 0));
    if (first == nullptr || second == nullptr) return 1;  // só no port: o original leria por nulo
    const int16_t top = first->y;
    const int32_t gap = int32_t(second->y) - int32_t(top);
    __Menu_SendEx(index, 0x1000015e, 3, 0x39, 0, 0);
    M_CONTROL* shown[16];
    int32_t count = 0;
    for (uint32_t flag = 0; flag < 16; ++flag) {
        int32_t tag = -1;
        if (flag < 9) {
            switch (flag) {
                case 0: tag = 0; break;
                case 1: tag = 2; break;
                case 2: tag = 1; break;
                case 3: tag = 5; break;
                case 4: tag = 3; break;
                case 5: tag = 4; break;
                case 6:
                case 7: tag = -1; break;
                default: tag = 6; break;
            }
        }
        if (tag == -1) continue;
        M_CONTROL* c = ptr<M_CONTROL>(__Menu_SendEx(index, 0x1000015e, uint32_t(tag), 0x39, 0, 0));
        if ((FlagsToDisplay & (1u << flag)) == 0) {
            __Menu_SendMessage(c, 0x2b, 1, 0);
        } else if (c != nullptr) {
            shown[count++] = c;
        }
    }
    int32_t y = (7 - count) * (gap / 2) + top;
    for (int32_t i = 0; i < count; ++i, y += gap) shown[i]->y = int16_t(y);
    return 1;
}

// [decomp] 0x00208c68 C_LANGUAGE_Handler
// As bandeiras: o foco troca o texto de ajuda e o idioma pedido; escolher carrega o idioma e vai ao cartão.
int32_t C_LANGUAGE_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    static const int32_t kLanguage[7] = {0, 2, 1, 4, 5, 3, 8};  // a língua de cada tag
    if (message == 0x4b) {
        if (c->tag < 7) LanguageToLoad = kLanguage[c->tag];
        Txt_SetLanguage(LanguageToLoad);
        SFXSetLanguage(sound_language(LanguageToLoad));
        Manager_SendMessage(mgr(index), 0x44, 0x40000048, 1);
    } else if (message == 0x4e) {
        if (c->tag >= 7) return 1;
        LanguageToLoad = kLanguage[c->tag];
        __Menu_Send(index, 0x1000006f, 0x18, val(language_help(LanguageToLoad)), 0);
    }
    return 1;
}

// [decomp] 0x00209e88 P_INTRO_Handler
// Os dois filmes de abertura (07370048 e 07380048), um depois do outro; depois "Press START" (0x40000009).
int32_t P_INTRO_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        movie_283 = 0;
    } else if (message == 0x50) {
        if (movie_283 - 1 < 3 && !movie_finished()) return 1;
        ++movie_283;
        if (movie_283 == 1 || movie_283 == 2) {
            movie_sound = 1;
            movie_counter = 0;
            movie_skip = 0;
            SFXRemove(0, 0x1d7);
            SFXRemove(0, 0x470);
            menu_movie = 0;
            psiStartBackgroundMovie(movie_283 == 1 ? 0x7370048 : 0x7380048, 0, SFXGetVolume());
            Manager_SendMessage(&menu_manager[0], 0x68, 1, 0);
            return 1;
        }
        Manager_SendMessage(mgr(index), 0x44, 0x40000009, 1);
    }
    return 1;
}

// [decomp] 0x0020a1f0 P_ATTRACT_Handler
// A demonstração depois de 45 s sem entrada no "Press START" ou no menu principal: um dos dois filmes
// (07390048 do menu principal a cada duas vezes, senão 073a0048... no PS2: 073a0048 / 073b0048); no fim volta.
int32_t P_ATTRACT_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x4c) {
        movie_291 = movie_291 == 0 ? 1 : 0;
        const uint32_t movie = (uint32_t(b) != 0x40000002 || movie_291 != 0) ? 0x73a0048 : 0x73b0048;
        start_movie(movie, 0);
    } else if (message == 0x50) {
        if (movie_finished() || bSkipAttract != 0) {
            stop_skippable_movie();
            Manager_SendMessage(mgr(index), 0x5f, 0, 0);
        }
    }
    return 1;
}

// [decomp] 0x0020a408 P_START_Handler
// "Press START" (0x40000009): o texto legal no memo e, na primeira vez, "Press the START button" só depois de
// 240 ticks (a página sem entrada até lá); 45 s sem entrada mostram a demonstração. Escolher (o rótulo de tela
// cheia, script do arquivo) vai a 0x4000004a.
int32_t P_START_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        reset_player_indices();
        menu_music();
        // mp_stuff[0x378..0x379] (o multijogador, não portado) ficam como estão.
        std::snprintf(legal_buf_296, sizeof(legal_buf_296), "%s", Txt_BindLabel(0x375, 0));
        __Menu_Send(index, 0x10000197, 0x18, val(legal_buf_296), 0);
        __Menu_Send(index, 0x10000197, 0x75, 0x5a, 0);
        if (once_295 == 0) {
            menu_ls().legal_once = 1;
            __Menu_Send(index, 0x100000fa, 0x18, 0, 0);
            __Menu_SendMessage(c, 0x6f, 0xf0, 0);
            __Menu_SendDelayed(0xf0, index, 0x100000fa, 0x18, txt(0x10001eb), 0);
            once_295 = 1;
        } else {
            __Menu_SendMessage(c, 0x6f, 0, 0);
            __Menu_Send(index, 0x100000fa, 0x18, txt(0x10001eb), 0);
        }
        Menu_ResetNoInputCount();
    } else if (message == 0x50 && Menu_GetNoInputCount() == 0xa8c) {
        Manager_SendMessage(mgr(index), 0x44, 0x40000035, 0);
    }
    return 1;
}

// [decomp] 0x0020a648 P_PARISENUM_Handler
// Procura o primeiro perfil do cartão (Menu_EnumSaves só o primeiro) e mostra 0x40000043; com a minimissão
// (RUN_MINI_MISSION e um perfil), "LOADING..." três ticks e a fase dela.
int32_t P_PARISENUM_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    MENU_LS& l = menu_ls();
    if (message == 0x4c) {
        l.can_save = 1;
        l.active = 1;
        l.slot = 999;
        l.op = 4;
        l.stale = 1;
        l.keep = 0;
        level_chosen_302 = 0;
        l.next_page = 0;
        l.next_flags = 0;
        l.done = 0;
        l.control = nullptr;
        Menu_UpdateMessageBox(index, 0, 0);
    } else if (message == 0x50) {
        if (level_chosen_302 != 0) {
            ++level_chosen_302;
            if (level_chosen_302 != 0) {
                if (level_chosen_302 < 4) {
                    __Menu_Send(index, 0x1000022c, 0x2b, 2, 0);
                } else if (level_chosen_302 == 4) {
                    Menu_RunMiniMission(index);
                }
            }
        }
        if (l.done == 0) {
            Menu_UpdateMessageBox(index, 4, 0);
        } else {
            l.done = 0;
            if (Menu_WillRunMiniMission() == 0) {
                Manager_SendMessage(mgr(index), 0x44, 0x40000043, 1);
            } else {
                level_chosen_302 = 1;
            }
        }
    }
    return 1;
}

// [decomp] 0x0020a060 P_ESTHERO_Handler
// O filme 073b0048 (pulável depois de 180 ticks) e o menu principal.
int32_t P_ESTHERO_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        frame_count_287 = 0;
    } else if (message == 0x50) {
        if (frame_count_287 == 1) start_movie(0x73b0048, 0xb4);
        ++frame_count_287;
        if (1 < frame_count_287 && movie_finished()) __Menu_SendMessage(&mgr(index)->base, 0x44, 0x40000002, 1);
    }
    return 1;
}

// ---- o menu principal ----

// [decomp] 0x0020a7b0 P_MAIN_Handler
// NightFire / Multiplayer / Codenames: ao entrar, o fade de entrada (o rótulo preto 0x100000ed some em 15
// ticks, com a página congelada) e há onde gravar; 45 s sem entrada mostram a demonstração.
int32_t P_MAIN_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        Menu_ResetNoInputCount();
        reset_player_indices();
        menu_music();
        __Menu_Send(index, 0x100000ed, 0x1a, 0xff, 0);
        __Menu_SendMessage(&mgr(index)->base, 0x68, 1, 0);
        __Menu_SendDelayedMessage(0xf, &mgr(index)->base, 0x68, 0, 0);
        __Menu_Send(index, 0x10000225, 0x75, 0x57, 0);
        fade(index, 0x100000ed, 0);
        menu_ls().can_save = 1;
    } else if (message == 0x50) {
        // Só no port: a resposta da caixa do multijogador (C_GOMULTIPLAYER_Handler) não fica para outra página.
        if (port_box_main != 0) {
            uint32_t id = 0;
            if (Menu_UpdateOptionBox(&id) != 0) port_box_main = 0;
        }
        if (Menu_GetNoInputCount() == 0xa8c) Manager_SendMessage(mgr(index), 0x44, 0x40000035, 0);
    }
    return 1;
}

namespace {
// C_GONIGHTFIRE/GOMULTIPLAYER/GOCODENAMES: fade para preto em 15 ticks e a página `page` 19 ticks depois.
int32_t go_page(uint8_t index, uint32_t message, uint32_t page) {
    if (message == 0x4b) {
        __Menu_SendMessage(&mgr(index)->base, 0x68, 1, 0);
        fade(index, 0x100000ed, 0xff);
        __Menu_SendDelayedMessage(0x13, &mgr(index)->base, 0x44, int32_t(page), 0);
    }
    return 1;
}
}  // namespace

// [decomp] 0x002169b8 C_GONIGHTFIRE_Handler
int32_t C_GONIGHTFIRE_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    return go_page(index, message, 0x40000025);
}

// [decomp] 0x00216a78 C_GOMULTIPLAYER_Handler
// Só no port: o multijogador (0x40000019 e as páginas dele) não foi portado; no lugar do fade e da página, a
// caixa de mensagem do jogo com o aviso.
int32_t C_GOMULTIPLAYER_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        Menu_CreateOptionBox(index, const_cast<char*>(PcText_Get(PcText::FeMultiplayer)), kPortBoxId, 1, 0);
        port_box_main = 1;
    }
    return 1;
}

// [decomp] 0x00216b38 C_GOCODENAMES_Handler
int32_t C_GOCODENAMES_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    return go_page(index, message, 0x4000001b);
}

// ---- filmes ----

// [decomp] 0x002169b0 P_FMV_Handler
int32_t P_FMV_Handler(uint8_t, M_CONTROL*, uint32_t, uint32_t, int32_t, int32_t) { return 1; }

// [decomp] 0x00214f10 P_TRAILER_Handler
// O trailer de "Die Another Day" (07390048, das opções de áudio e vídeo): escolher ou o fim do filme voltam.
int32_t P_TRAILER_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        start_movie(0x7390048, 0);
    } else if (message == 0x4b) {
        Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    } else if (message == 0x50 && movie_finished()) {
        stop_skippable_movie();
        Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    }
    return 1;
}

// [decomp] 0x0020ad30 P_FMVPLAYER_Handler
// Toca movie_hashcode no segundo tick e volta quando ele acaba.
int32_t P_FMVPLAYER_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        frame_count_322 = 0;
    } else if (message == 0x50) {
        if (frame_count_322 == 1 && movie_hashcode != 0) start_movie(movie_hashcode, 0);
        ++frame_count_322;
        if (1 < frame_count_322 && movie_finished()) __Menu_SendMessage(&mgr(index)->base, 0x5f, 0, 0);
    }
    return 1;
}

// [decomp] 0x00215990 Menu_PlayMovie
// O filme de fundo de uma página (ou um filme de página): pulável, em laço, troca a música, congela o menu.
void Menu_PlayMovie(uint32_t movie, int8_t skip, int8_t loop, int8_t sound, int8_t freeze, uint32_t counter) {
    movie_skip = uint8_t(skip);
    movie_sound = uint8_t(sound);
    movie_counter = counter;
    if (sound != 0) {
        SFXRemove(0, 0x1d7);
        SFXRemove(0, 0x470);
        menu_movie = 0;
    }
    psiStartBackgroundMovie(movie, loop, SFXGetVolume());
    Manager_SendMessage(&menu_manager[0], 0x68, freeze, 0);
}

// [decomp] 0x00215a50 Menu_StopMovie
// Pula o filme (MenuManager_Update, com Start ou X): só os puláveis, depois de movie_counter ticks (ou à força).
void Menu_StopMovie(uint8_t force) {
    if (movie_skip == 0 || (force == 0 && movie_counter != 0)) return;
    if (movie_sound != 0) {
        SFXUnPause();
        SFXUnPauseAllStreams();
    }
    psiStopBackgroundMovie();
    Manager_SendMessage(&menu_manager[0], 0x68, 0, 0);
}

// [decomp] 0x00215970 Menu_UpdateMovieCounter
void Menu_UpdateMovieCounter() {
    if (movie_counter != 0) --movie_counter;
}

// ---- o fim do jogo e a trapaça da medalha ----

// [decomp] 0x00215100 P_WINGAME_Handler
// Depois da última missão: o filme 073f0048 e os créditos (0x40000030).
int32_t P_WINGAME_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        start_movie(0x73f0048, 0);
    } else if (message == 0x50 && movie_finished()) {
        stop_skippable_movie();
        Manager_SendMessage(mgr(index), 0x44, 0x40000030, 1);
    }
    return 1;
}

// [decomp] 0x002086b0 P_CHEATMEDAL_Handler
// A página de depuração 0x4000004c (o menu de jogo 08000001): fracassar (canal 99) ou cumprir a missão com uma
// medalha escolhida (canal 100, medal_from_cheat).
int32_t P_CHEATMEDAL_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        __Menu_SendMessage(ptr<M_CONTROL>(__Menu_Send(index, 0x1000021c, 0x39, 0, 0)), 0x17, 0, 0);
        return 1;
    }
    if (message != 0x4b) return 1;
    const uint32_t choice = uint32_t(__Menu_Send(index, 0x1000021c, 0x35, 0, 0));
    MenuManager_Delete(index);
    static const uint32_t kMedal[5] = {999, 1, 2, 3, 4};
    if (choice == 0) {
        switch_channels[99] = 1;
        switch_channels_time[99] = GameState.frame;
    } else if (choice <= 5) {
        medal_from_cheat = kMedal[choice - 1];
        switch_channels[100] = 1;
        switch_channels_time[100] = GameState.frame;
    }
    return 1;
}

// ---- a escolha da missão ----

// [decomp] 0x0020aec0 P_NFSELECT_Handler
// A roda de codinomes da campanha (0x40000025): ao entrar lista o cartão (se a lista está velha) e põe os
// nomes na roda; "Play without saving" mostra o aviso (caixa 9) e, no OK, a dificuldade; um codinome do
// cartão é lido e, no fim (0x6a), a dificuldade. Voltar: o menu principal com o fade.
int32_t P_NFSELECT_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    MENU_LS& l = menu_ls();
    if (0x22 < message - 0x4c) return 1;
    switch (message) {
        case 0x4c:
            l.stale = 1;
            l.next_page = 0;
            // Peculiaridade do original: a roda dos codinomes é a da página 0x4000001b, não a desta.
            Menu_SelectCodenameInControl(index, l.name, 0x100000eb);
            Menu_StartIris(uint32_t(b) == 0x40000002 ? 0 : 4, index, 0x10000103);
            __Menu_Send(index, 0x100000e4, 0x18, val(kSpace), 0);
            [[fallthrough]];
        case 0x6e: {
            menu_bonus[0xa5] = 0;
            M_CONTROL* wheel = ptr<M_CONTROL>(__Menu_Send(index, 0x100000e5, 0x39, 0, 0));
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
                __Menu_Send(index, 0x100000ee, 0x1a, 0, 0);
            } else {
                Menu_StartIris(0, index, 0x10000103);
                __Menu_Send(index, 0x100000ee, 0x1a, 0xff, 0);
                __Menu_SendMessage(&mgr(index)->base, 0x68, 1, 0);
                __Menu_SendDelayedMessage(0xf, &mgr(index)->base, 0x68, 0, 0);
                fade(index, 0x100000ee, 0);
            }
            select_def_codename_327 = uint32_t(b) == 0x40000002 ? 1 : 0;
            reset_player_indices();
            closing_326 = 0;
            return 1;
        }
        case 0x50: {
            Menu_PlayIris(1, index, 0x10000103);
            if (l.done != 0 && closing_326 == 0) {
                Menu_PutCodenamesIntoControl(ptr<M_CONTROL>(__Menu_Send(index, 0x100000e5, 0x39, 0, 0)));
                l.done = 0;
                if (select_def_codename_327 != 0) __Menu_Send(index, 0x100000e5, 0x2e, 1, 0);
            }
            uint32_t id = 0;
            const uint32_t r = Menu_UpdateOptionBox(&id);
            if (id == 9 && r == 3) Menu_ChangePageCloseIris(0x40000023, index, 0x10000103);
            if (l.active != 0 && l.slot < 2) {
                l.active = 0;
                Menu_MapDefaultCodename(0, uint8_t(l.slot), 0xff);
                Menu_ChangePageCloseIris(l.next_page, index, 0x10000103);
                return 1;
            }
            Menu_UpdateMessageBox(index, 0xff, 0);
            return 1;
        }
        case 0x6a:
            if (l.op == 0) {
                Menu_ChangePageCloseIris(0x40000023, index, 0x10000103);
            } else {
                Menu_UpdateCodenameWheel(index, ptr<M_CONTROL>(__Menu_Send(index, 0x100000e5, 0x39, 0, 0)), 0x100000e4,
                                         0x100001cc, 0x100001f2, 0x10000103, 0);
            }
            closing_326 = 1;
            return 1;
        case 0x6b: {
            M_CONTROL* m = &mgr(index)->base;
            __Menu_SendMessage(m, 0x68, 1, 0);
            __Menu_SendDelayedMessage(0x13, m, 0x44, 0x40000002, 0);
            __Menu_SendDelayedMessage(0x13, m, 0x68, 0, 0);
            fade(index, 0x100000ee, 0xff);
            *ptr<int32_t>(b) = -2;
            return 1;
        }
        default:
            return 1;
    }
}

// [decomp] 0x0020b390 C_SBNFCN_Handler
// A roda de codinomes da campanha: 0 "New Codename" (com onde gravar: os padrões e a página do nome), 1 "Play
// without saving" (o aviso, caixa 9, e o perfil padrão), 2+ um codinome do cartão (lido). A roda gira com o
// íris; Y (0x5d) cria um codinome.
int32_t C_SBNFCN_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    MENU_LS& l = menu_ls();
    if (message == 0x4b) {
        __Menu_Send(index, 0x1000010e, 0x18, txt(0x208), 0);
        const int32_t v = __Menu_SendMessage(c, 0x40, 0, 0);
        if (v != 0) {
            if (v < 2) {
                l.slot = uint32_t(v);
                Menu_CreateOptionBox(index, 0x10000bb, 9, 1, 1);
                Menu_MapDefaultCodename(0, uint8_t(v), 0xff);
                return 1;
            }
            const int32_t w = __Menu_SendMessage(c, 0x40, 0, 0);
            l.active = 1;
            l.next_page = 0;
            l.next_flags = 0;
            l.op = 0;
            l.done = 0;
            l.keep = 0;
            l.slot = uint32_t(v);
            l.control = c;
            if (0 <= w && w < kCodenames) std::snprintf(l.name, sizeof(l.name), "%s", codename_buf[w]);
            Menu_UpdateMessageBox(index, 0xff, 0);
            return 1;
        }
        l.slot = 0;
        if (l.can_save == 0) return 1;
        Menu_MapDefaultCodename(0, 0, 0xff);
        Menu_ChangePageCloseIris(0x40000020, index, 0x10000103);
        __Menu_Send(index, 0x10000100, 0x20, 0x40000025, 0);
    } else if (message == 0x49 || message == 0x54) {
        Menu_UpdateCodenameWheel(index, c, 0x100000e4, 0x100001cc, 0x100001f2, 0x10000103, message == 0x49 ? 1 : 0);
    } else if (message == 0x5d) {
        l.slot = 0;
        if (l.can_save == 0) return 1;
        __Menu_SendMessage(&mgr(index)->base, 0x44, 0x40000020, 0);
        __Menu_Send(index, 0x10000100, 0x20, 0x40000025, 0);
    }
    return 1;
}

// [decomp] 0x00216bf8 P_NFDFCTY_Handler
int32_t P_NFDFCTY_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x4c) {
        if (uint32_t(b) == 0x40000025) {
            Menu_StartIris(0, index, 0x10000104);
            __Menu_Send(index, 0x100000bf, 0x2e, 0, 0);
        } else {
            Menu_StartIris(4, index, 0x10000104);
        }
    } else if (message == 0x50) {
        Menu_PlayIris(1, index, 0x10000104);
    }
    return 1;
}

// [decomp] 0x00216ca8 C_SBNFDFCTY_Handler
// A roda da dificuldade: escolher uma liberada a põe em GameState.difficulty (1 Operative, 2 Agent, 3 00 Agent)
// e mostra o mapa das missões.
int32_t C_SBNFDFCTY_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        const uint32_t v = uint32_t(__Menu_SendMessage(c, 0x40, 0, 0)) & 0xff;
        if (2 < v) return 1;  // só no port: dentro da tabela (a roda vai de 0 a 2)
        if (menu_unlock_everything == 0 && difficulty[v].enabled == 0) return 1;
        GameState.difficulty = difficulty[v].value;
        // Peculiaridade do original: o íris é o da página do mapa (0x10000102), que esta página não tem.
        Menu_ChangePageCloseIris(0x4000001c, index, 0x10000102);
    } else if (message == 0x51) {
        __Menu_SendMessage(c, 0x27, 0, 2);
    } else if (message == 0x49 || message == 0x54) {
        Menu_UpdateWheel(index, c, difficulty, 0x100000e6, 0x100000be, 0x100001ee, 0x10000104, message == 0x49 ? 1 : 0);
    }
    return 1;
}

// [decomp] 0x0020b608 P_NFMAP_Handler
// O mapa das missões (0x4000001c): ao entrar a roda na última missão liberada e o texto de ajuda (voltar
// para o menu principal quando a pilha está vazia, como depois do resultado); uma missão de carro escolhida
// mostra "LOADING..." três ticks e pede a fase (psiLaunchDriving).
int32_t P_NFMAP_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x50) {
        Menu_PlayIris(1, index, 0x10000102);
        if (level_chosen != 0) {
            ++level_chosen;
            if (level_chosen != 0) {
                if (level_chosen < 4) {
                    __Menu_Send(index, 0x1000022c, 0x2b, 2, 0);
                } else if (level_chosen == 4) {
                    ResetMap_LevelToLoad(GameState.load_file, 0, 0);
                    GameFlow_PushState(GF_COUNTDOWN_REQUEST, 80.0f, 0);
                    MenuManager_Delete(index);
                    SFXFadeDown(1);
                    return 1;
                }
            }
        }
        // Só no port: a missão de carro pulada (Menu_PortDrivingSkipped) mostra o aviso uma vez e a resposta
        // não fica para outra página.
        if (port_driving_notice == 1 && mgr(index)->transition == 0 && mgr(index)->frozen == 0) {
            Menu_CreateOptionBox(index, const_cast<char*>(PcText_Get(PcText::FeDriving)), kPortBoxId, 1, 0);
            port_driving_notice = 2;
        } else if (port_driving_notice == 2) {
            uint32_t id = 0;
            if (Menu_UpdateOptionBox(&id) != 0) port_driving_notice = 0;
        }
    } else if (message == 0x4c) {
        PlayerSetting[2].index = 2;
        PlayerSetting[3].index = 3;
        PlayerSetting[0].index = 0;
        PlayerSetting[1].index = 1;
        menu_music();
        Menu_StartIris(uint32_t(b) == 0x40000023 ? 0 : 4, index, 0x10000102);
        const uint32_t help = Stack_IsEmpty(&mgr(index)->stack) == 0 ? 0x208 : 0x283;
        __Menu_Send(index, 0x1000010e, 0x18, txt(help), 0);
        M_CONTROL* wheel = ptr<M_CONTROL>(__Menu_Send(index, 0x1000000e, 0x39, 0, 0));
        __Menu_SendMessage(wheel, 0x2e, 0, 0);
        int32_t last = 0xb;
        while (0 < last && sp_items()[last].enabled == 0) --last;  // só no port: para em 0
        __Menu_SendMessage(wheel, 0x2e, last, 0);
    } else if (message == 99) {
        Manager_SendMessage(mgr(index), 0x44, 0x40000002, 1);
    }
    return 1;
}

// [decomp] 0x0020b8d8 C_SBNFMAP_Handler
// A roda das missões (sp_level): escolher uma liberada (ou todas, com a trapaça) põe a fase em
// GameState.load_file, desliga o multijogador e chama ResetMap_LevelToLoad com o filme de abertura e o fade, e
// fecha o menu; uma missão de carro passa por P_NFMAP_Handler. Y (0x5d): o dossiê.
int32_t C_SBNFMAP_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    switch (message) {
        case 0x49:
        case 0x54:
            Menu_UpdateWheel(index, c, sp_items(), 0x100000e3, 0x10000020, 0x1000001f, 0x10000102, message == 0x49 ? 1 : 0);
            return 1;
        case 0x4b: {
            const uint32_t v = uint32_t(__Menu_SendMessage(c, 0x40, 0, 0)) & 0xff;
            if (0xb < v) return 1;  // só no port: o original leria além de sp_level antes de conferir
            if (menu_unlock_everything == 0 && sp_items()[v].enabled == 0) return 1;
            put32(MPSettings + 0x18c, 0);
            put32(MPSettings + 0x180, 0);
            GameState.load_file = sp_items()[v].value;
            put32(MPSettings + 0x1ac, 1);
            put32(MPSettings + 0x194, 1);
            GameState.unknown_44[1] = 0;
            PlayerSetting[0].index = uint8_t(Menu_GetLastController());
            if (Menu_IsDrivingLevel(GameState.load_file) == 0) {
                ResetMap_LevelToLoad(GameState.load_file, 0, 0);
                GameFlow_PushState(GF_FADE_REQUEST, 80.0f, 0xff);
                MenuManager_Delete(index);
                SFXFadeDown(1);
                return 1;
            }
            level_chosen = 1;
            return 1;
        }
        case 0x51:
            __Menu_SendMessage(c, 0x27, 0, 0xb);
            __Menu_SendMessage(c, 0x2e, 0, 0);
            return 1;
        case 0x5d:
            Menu_ChangePageCloseIris(0x4000002b, index, 0x10000102);
            return 1;
        default:
            return 1;
    }
}

// ---- as rodas, os itens e o íris ----

// [decomp] 0x001f8388 Menu_UpdateWheel
// A roda de itens (uma barra de rolagem vertical): a imagem e a descrição do item da vez (a de bloqueado se ele
// não está liberado) e os cinco rótulos com os dois de cada lado, mais claros perto do centro e cinzentos
// quando bloqueados; com animate, a imagem troca 10 ticks depois e o íris gira.
void Menu_UpdateWheel(uint8_t index, M_CONTROL* c, M_ITEM* items, uint32_t labels, uint32_t image, uint32_t text,
                      uint32_t iris, int8_t animate) {
    if (c == nullptr || items == nullptr) return;
    if (c->type == MCT_SCROLL) reinterpret_cast<M_SCROLL*>(c)->wrap = 0;
    const int32_t pos = __Menu_SendMessage(c, 0x40, 0, 0);
    const int32_t max = __Menu_SendMessage(c, 0x38, 0, 0);
    const M_ITEM& item = items[pos];
    if (image != 0) {
        const int32_t colour = item.enabled != 0 ? int32_t(0x808080ffu) : 0x60606060;
        if (animate == 0) {
            __Menu_Send(index, image, 0x24, int32_t(item.image), 0);
            __Menu_Send(index, image, 0x1a, colour, 0);
        } else {
            __Menu_SendDelayed(10, index, image, 0x24, int32_t(item.image), 0);
            __Menu_SendDelayed(10, index, image, 0x1a, colour, 0);
        }
    }
    if (text != 0) {
        const M_PAGE* page = mgr(index)->page;
        if (page != nullptr && page->base.id == 0x4000003f) {
            // Os bots do multijogador: o nome e se ele é bom.
            if (item.enabled == 0) {
                __Menu_Send(index, text, 0x18, txt(item.locked), 0);
            } else {
                static char buf_171[0x100];
                std::snprintf(buf_171, sizeof(buf_171), "%s\n%s : %s", Txt_BindLabel(item.text, 0), Txt_BindLabel(0x1b8, 0),
                              Txt_BindLabel(Menu_IsBotGood(item.value) != 0 ? 0x1c8 : 0x1c7, 0));
                __Menu_Send(index, text, 0x18, val(buf_171), 0);
            }
        } else {
            __Menu_Send(index, text, 0x18, txt(item.enabled == 0 ? item.locked : item.text), 0);
        }
    }
    int32_t idx[5] = {pos - 2, pos - 1, pos, pos + 1, pos + 2};
    if (idx[1] < 0) {
        idx[1] = idx[0] = 999;
    } else if (idx[0] < 0) {
        idx[0] = 999;
    }
    if (max < idx[3]) {
        idx[3] = idx[4] = 999;
    } else if (max < idx[4]) {
        idx[4] = 999;
    }
    for (uint32_t k = 0; k < 5; ++k) {
        const int32_t s = idx[k] == 999 ? val(kSpace) : txt(items[idx[k]].name);
        __Menu_SendEx(index, labels, k, 0x18, s, 0);
    }
    // Só no port: um item fora da roda (999) conta como bloqueado (o original leria items[999]).
    const auto on = [&](int32_t i) { return i != 999 && items[i].enabled != 0; };
    __Menu_SendEx(index, labels, 0, 0x1a, on(idx[0]) ? 0x7d6d5a40 : 0x48484820, 0);
    __Menu_SendEx(index, labels, 1, 0x1a, on(idx[1]) ? int32_t(0x7d6d5a80) : 0x48484840, 0);
    __Menu_SendEx(index, labels, 2, 0x1a, on(idx[2]) ? int32_t(0x7d6d5aff) : int32_t(0x48484880), 0);
    __Menu_SendEx(index, labels, 3, 0x1a, on(idx[3]) ? int32_t(0x7d6d5a80) : 0x48484840, 0);
    __Menu_SendEx(index, labels, 4, 0x1a, on(idx[4]) ? 0x7d6d5a40 : 0x48484820, 0);
    if (animate != 0) Menu_StartIris(2, index, iris);
}

// [decomp] 0x001f89f0 Menu_UpdateCodenameWheel
// A roda de codinomes: a imagem e a descrição (criar, o padrão, um do cartão) e os cinco nomes de codename_buf.
void Menu_UpdateCodenameWheel(uint8_t index, M_CONTROL* c, uint32_t labels, uint32_t image, uint32_t text, uint32_t iris,
                              int8_t animate) {
    if (c == nullptr) return;
    if (c->type == MCT_SCROLL) reinterpret_cast<M_SCROLL*>(c)->wrap = 0;
    const int32_t pos = __Menu_SendMessage(c, 0x40, 0, 0);
    const int32_t max = __Menu_SendMessage(c, 0x38, 0, 0);
    uint32_t desc = 0x1000005;
    uint32_t picture = 0x3000135;
    if (pos == 0) {
        desc = 0x1000004;
        picture = 0x300013b;
    } else if (pos == 1) {
        desc = 0x1000003;
    }
    if (image != 0) {
        if (animate == 0) {
            __Menu_Send(index, image, 0x24, int32_t(picture), 0);
        } else {
            __Menu_SendDelayed(10, index, image, 0x24, int32_t(picture), 0);
        }
    }
    if (text != 0) __Menu_Send(index, text, 0x18, txt(desc), 0);
    int32_t idx[5] = {pos - 2, pos - 1, pos, pos + 1, pos + 2};
    if (idx[1] < 0) {
        idx[1] = idx[0] = 999;
    } else if (idx[0] < 0) {
        idx[0] = 999;
    }
    if (max < idx[3]) {
        idx[3] = idx[4] = 999;
    } else if (max < idx[4]) {
        idx[4] = 999;
    }
    if (menu_ls().count == 0) idx[0] = idx[1] = idx[2] = idx[3] = idx[4] = 999;
    for (uint32_t k = 0; k < 5; ++k) {
        // Só no port: dentro de codename_buf (o original leria qualquer posição da roda).
        const bool inside = idx[k] != 999 && 0 <= idx[k] && idx[k] < kCodenames;
        __Menu_SendEx(index, labels, k, 0x18, inside ? val(codename_buf[idx[k]]) : val(kSpace), 0);
    }
    if (animate != 0) Menu_StartIris(2, index, iris);
}

// [decomp] 0x001f91a8 Menu_StartIris
// O íris (a imagem ao lado da roda): 0 abre do meio, 1 fecha, 2 gira (se não está girando), 3 fica parado no
// meio, 4 só a imagem parada; desenha o primeiro passo na hora.
void Menu_StartIris(uint32_t op, uint8_t index, uint32_t id) {
    if (op == 2 && menu_iris.busy != 0) return;
    switch (op) {
        case 0:
            menu_iris.step = 5;
            menu_iris.busy = 1;
            menu_iris.hold = 0;
            break;
        case 1:
            menu_iris.step = 1;
            menu_iris.busy = 1;
            menu_iris.hold = 0;
            break;
        case 2:
            menu_iris.step = 1;
            menu_iris.busy = 0;
            menu_iris.hold = 0;
            break;
        case 3:
            menu_iris.hold = 1;
            menu_iris.step = 5;
            menu_iris.busy = 1;
            break;
        case 4:
            menu_iris.step = 10;
            menu_iris.busy = 1;
            menu_iris.hold = 0;
            break;
        default:
            break;
    }
    menu_iris.frame = 0;
    Menu_PlayIris(0, index, id);
    menu_iris.frame = -1;
}

// [decomp] 0x001f9288 Menu_PlayIris
// Um passo do íris a cada dois ticks (a 60 Hz): as imagens 030000bf-030000c3 indo e voltando; o passo 10 é a
// imagem parada.
void Menu_PlayIris(int8_t advance, uint8_t index, uint32_t id) {
    if (10 < menu_iris.step) {
        menu_iris.busy = 0;
        return;
    }
    if (advance != 0) ++menu_iris.frame;
    const int32_t skip = PS2FramesToSkip == 1 ? (menu_iris.frame & 1) : (menu_iris.frame % 2) / 2;
    if (skip != 0) return;
    M_CONTROL* c = ptr<M_CONTROL>(__Menu_Send(index, id, 0x39, 0, 0));
    if (c == nullptr) return;
    static const uint32_t kImages[10] = {0x30000bf, 0x30000c0, 0x30000c1, 0x30000c2, 0x30000c3,
                                         0x30000c2, 0x30000c1, 0x30000c0, 0x30000bf, 0};
    if (1 <= menu_iris.step && menu_iris.step <= 9) {
        __Menu_SendMessage(c, 0x24, int32_t(kImages[menu_iris.step - 1]), 0);
        __Menu_SendMessage(c, 0x2d, 0, 0x800080);
    } else if (menu_iris.step == 10) {
        __Menu_SendMessage(c, 0x24, 0x30000bf, 0);
        __Menu_SendMessage(c, 0x2d, 0x400040, 0x10001);
    }
    if (advance != 0 && menu_iris.hold == 0) ++menu_iris.step;
}

// [decomp] 0x00202190 Menu_ChangePageCloseIris
// Fecha o íris e mostra `page` 10 ticks depois (com as flags de ls.next_flags), o menu congelado no meio.
void Menu_ChangePageCloseIris(uint32_t page, uint8_t index, uint32_t id) {
    Menu_StartIris(1, index, id);
    __Menu_SendDelayedMessage(1, &mgr(index)->base, 0x68, 1, 0);
    __Menu_SendDelayedMessage(10, &mgr(index)->base, 0x44, int32_t(page), int32_t(menu_ls().next_flags));
}

// [decomp] 0x00202008 Menu_AddItemsToControl
// Os itens liberados (ou todos) como linhas da lista, com o valor de cada um.
void Menu_AddItemsToControl(M_CONTROL* c, M_ITEM* items, uint16_t count, uint16_t first, uint8_t all) {
    if (items == nullptr) return;
    __Menu_SendMessage(c, 0x17, 0, 0);
    for (uint32_t i = first; i < count; i = (i + 1) & 0xffff) {
        if (all != 0 || menu_unlock_everything != 0 || items[i].enabled != 0) {
            __Menu_SendMessage(c, 0x10, txt(items[i].name), int32_t(items[i].value));
        }
    }
}

// [decomp] 0x002020f8 Menu_SelectItemInControl
// Escolhe na lista ou na roda o item de valor `value` (1); sem ele, a roda na posição 0.
uint32_t Menu_SelectItemInControl(M_CONTROL* c, M_ITEM* items, uint16_t count, uint32_t value) {
    if (items == nullptr || c == nullptr) return 0;
    for (uint32_t i = 0; i < count; i = (i + 1) & 0xffff) {
        if (items[i].value != value) continue;
        if (c->type == MCT_LIST) {
            __Menu_SendMessage(c, 0x1d, int32_t(i), 0);
        } else if (c->type == MCT_SCROLL) {
            __Menu_SendMessage(c, 0x2e, int32_t(i), 0);
        } else {
            return 0;
        }
        return 1;
    }
    __Menu_SendMessage(c, 0x2e, 0, 0);
    return 0;
}

// [decomp] 0x00202718 Menu_GetItemFromHash
M_ITEM* Menu_GetItemFromHash(M_ITEM* items, uint32_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].value == value) return &items[i];
    }
    return nullptr;
}

// [decomp] 0x002023a0 Menu_SetNightfireStatus
// Libera as missões dos bits (0: as duas primeiras).
void Menu_SetNightfireStatus(uint32_t bits) {
    const uint32_t v = bits != 0 ? bits : 3;
    for (uint32_t i = 0; i < 12; ++i) sp_items()[i].enabled = (v & (1u << i)) != 0 ? 1 : 0;
}

// [decomp] 0x002027a8 Menu_IsBotGood
uint32_t Menu_IsBotGood(uint32_t bot) { return BOT_getDefaultStats(bot)[9] == 0 ? 1 : 0; }

// [decomp] 0x00202450 Menu_WillRunMiniMission
// As legendas dos perfis padrão (ligadas fora do inglês, do francês, do alemão, do espanhol e da versão
// americana) e se a minimissão de demonstração roda (RUN_MINI_MISSION e dois nomes na lista). Só no port: as
// legendas que o nightfire.ini tem valem por cima (platform/game_options.h, regra 3).
uint32_t Menu_WillRunMiniMission() {
    if (LanguageToLoad < 0 || (3 < LanguageToLoad && LanguageToLoad != 6)) {
        def_codename[0x38 + 0x18] = 1;
        DrawInfo[2] = 1;
        def_codename[0x18] = 1;
    } else {
        DrawInfo[2] = 0;
        def_codename[0x38 + 0x18] = 0;
        def_codename[0x18] = 0;
    }
    nf::game_options_default_profile(nf::kGameOptionsSubtitles);
    if (RUN_MINI_MISSION == 0) return 0;
    if (menu_ls().count == 2) {
        menu_ls().mini = 1;
        return 1;
    }
    menu_ls().mini = 0;
    return 0;
}

// [decomp] 0x001ffb90 Menu_RunMiniMission
// A minimissão de demonstração (09000001, Paris de carro) com o perfil padrão.
uint32_t Menu_RunMiniMission(uint8_t index) {
    if (RUN_MINI_MISSION == 0) return 0;
    if (menu_ls().count != 2) {
        menu_ls().mini = 0;
        return 0;
    }
    PlayerSetting[0].index = uint8_t(Menu_GetLastController());
    uint32_t label;
    std::memcpy(&label, def_codename + 0x38, 4);
    std::snprintf(menu_ls().name, sizeof(menu_ls().name), "%s", Txt_BindLabel(label, 0));
    menu_ls().mini = 1;
    GameState.load_file = 0x9000001;
    put32(MPSettings + 0x194, 1);
    put32(MPSettings + 0x18c, 0);
    GameState.difficulty = 1;
    put32(MPSettings + 0x180, 0);
    put32(MPSettings + 0x1ac, 1);
    ResetMap_LevelToLoad(0x9000001, 0, 0);
    GameState.unknown_44[1] = 0;
    GameFlow_PushState(GF_COUNTDOWN_REQUEST, 80.0f, 0);
    SFXFadeDown(1);
    MenuManager_Delete(index);
    return 1;
}

// ---- só no port ----

uint32_t Menu_PortDrivingSkipped(uint32_t level) {
    if (Menu_IsDrivingLevel(level) == 0) return 0;
    Menu_SetLevelBonus(level, 5, 0);
    GameState.menu_page = 0x4000001c;
    port_driving_notice = 1;
    ResetMap_LevelToLoad(0x7000048, 0, 0);
    return 1;
}

int Menu_FrontEndDescribe(char* out, size_t size) {
    const MENU_LS& l = menu_ls();
    uint32_t unlocked = 0;
    for (uint32_t i = 0; i < 12; ++i) unlocked |= sp_items()[i].enabled != 0 ? 1u << i : 0;
    return std::snprintf(out, size,
                         "frontend pagina %08x perfil %u codinome \"%s\" op %u ativo %u fim %u nomes %u missoes %03x "
                         "dificuldade %u",
                         Menu_CurrentPage(0), l.slot, l.name, l.op, l.active, l.done, l.count, unlocked,
                         GameState.difficulty);
}

void menu_frontend_debug_power_on() {
    std::memset(codename_buf, 0, sizeof(codename_buf));
    menu_iris = MENU_IRIS{};
    movie_skip = movie_sound = 0;
    movie_counter = 0;
    menu_unlock_everything = 0;
    bDisplayLangMenu = 0;
    bSkipAttract = 0;
    RUN_MINI_MISSION = 0;
    FlagsToDisplay = 0;
    medal_from_cheat = 0;
    movie_hashcode = 0;
    movie_291 = 0;
    movie_283 = 0;
    once_295 = 0;
    level_chosen_302 = 0;
    frame_count_287 = frame_count_322 = 0;
    level_chosen = 0;
    closing_326 = 0;
    select_def_codename_327 = 0;
    port_box_main = 0;
    port_driving_notice = 0;
}
