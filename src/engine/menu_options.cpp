// As páginas de opções do codinome no menu principal (arquivo 08000002, chegam pela roda de P_CNMENU): "Controller
// Setup" (0x40000022), "Advanced Options" (0x4000002d), "Multiplayer Options" (0x4000002e), "AV Options"
// (0x40000031) com o ajuste de tela (0x40000047), e os créditos (0x40000030, também depois de P_WINGAME). Ver
// menu_frontend.h; onde cada opção mora e a regra com o nightfire.ini em platform/game_options.h.
//
// As páginas seguem o jogo: ao entrar (0x4c) o tratador põe nos controles os valores dos lugares do jogo
// (PlayerSetting[0], DrawInfo, o volume e o modo do som, a posição da tela); escolher (0x4b, "~A Confirm") em
// qualquer controle grava todos de volta, marca o codinome como mudado (cn_modified_flag) e volta; voltar sem
// escolher deixa tudo como estava (na AV Options os volumes, que mudam na hora com as barras, voltam aos de antes).
//
// O que cada opção faz no PC: as do jogador (mira automática, mira, agachar e mirar alternando ou segurando, troca
// automática de arma, itens piscando, HUD sempre visível, estilo e inversão do controle, vibração) e as legendas
// valem como no PS2. Os volumes e o modo do som só são guardados (sem módulo de áudio). Widescreen (DrawInfo[1]), a
// tela dividida (DrawInfo[0]) e o ajuste de tela (psiSetScreenPos) só são guardados, no lugar do jogo e no perfil:
// o PC desenha na proporção da janela (Hor+) e não tem margem de TV (decidido com o usuário).
//
// Peculiaridades mantidas (comentadas nas funções): a roda de widescreen da AV Options não é limpa antes de receber
// as opções (cada visita vinda do menu do codinome acrescenta mais um "On"/"Off"); a escolha de qualquer controle
// das páginas de opções confirma a página.
//
// Divergências: Menu_SetupCredits monta as linhas da tabela que game/menu_tables.cpp tirou do próprio
// Menu_SetupCredits do ELF na partida (os textos do *Txt.dat e as strings do ELF; sem a tabela, nenhuma linha).

#include "engine/menu_frontend.h"

#include "engine/menu_tweaks.h"

#include <cstdio>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/resetmap.h"
#include "engine/txt.h"
#include "platform/psi.h"
#include "platform/psi_sound.h"

uint8_t was_inverted = 0;
int32_t PS2OldX = 0;
int32_t PS2OldY = 0;
CREDIT_SOURCE CreditsSource[kCreditsMax];
uint32_t CreditsSourceCount = 0;
CREDIT_tag* ptr_credits = nullptr;

namespace {

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }
template <class T>
T* ptr(int32_t v) {
    return nf::p32_ptr<T>(uint32_t(v));
}
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }
int32_t txt(uint32_t label) { return val(Txt_BindLabel(label, 0)); }

// Uma roda de duas opções (0x17 limpa, 0x10 texto e valor, 0x1e o valor atual), na ordem do original.
void two_options(uint8_t index, uint32_t id, uint32_t first_label, int32_t first_value, uint32_t second_label,
                 int32_t second_value, int32_t current) {
    __Menu_Send(index, id, 0x17, 0, 0);
    __Menu_Send(index, id, 0x10, txt(first_label), first_value);
    __Menu_Send(index, id, 0x10, txt(second_label), second_value);
    __Menu_Send(index, id, 0x1e, current, 0);
}
// On (0x1b7) com o valor `on`, Off (0x1b6) com o valor `off`.
void on_off(uint8_t index, uint32_t id, int32_t on, int32_t off, int32_t current) {
    two_options(index, id, 0x1b7, on, 0x1b6, off, current);
}
int32_t radio_value(uint8_t index, uint32_t id) { return __Menu_Send(index, id, 0x35, 0, 0); }

// P_CNAVOPTIONS_Handler
uint8_t going_to_av_subscreen_498 = 0;
int32_t start_sfx_vol_496 = 0;
int32_t start_music_vol_497 = 0;
int32_t screen_adjust_x_499 = 0;
int32_t screen_adjust_y_500 = 0;
// P_CNMPOPTIONS_Handler: as opções de vantagem de vida (0x0030d390-0x0030d3c8).
char kHandicaps[8][8] = {"-75", "-50", "-25", "0", "+25", "+50", "+75", "+100"};
// P_CREDITS_Handler
CREDIT_tag* credits_516 = nullptr;
uint8_t fading_517 = 0;
uint32_t frame_518 = 0;
uint32_t index_519 = 0;
uint32_t name_520 = 0;
uint32_t count_521 = 0;
int32_t page_from_523 = 0;
uint32_t music_vol_524 = 0;
// Os formatos das linhas (fonte e alinhamento: 0xff fonte, 0xfe alinhamento), 0x0030d560-0x0030d570.
char kCreditFont1[] = "\xff\x01\xfe\x03";
char kCreditFont3[] = "\xff\x03\xfe\x03";
char kCreditFont2[] = "\xff\x02\xfe\x03";
char* credit_format(uint8_t font) { return font == 1 ? kCreditFont1 : font == 2 ? kCreditFont3 : kCreditFont2; }
// O texto das strings do ELF (os créditos e as listas da página de depuração) na partida.
char table_pool[0xa000];
uint32_t table_pool_used = 0;

}  // namespace

// ---- Controller Setup ----

// [decomp] 0x00213cd0 P_CNCONTROLS_Handler
// "Controller Setup" (0x40000022; o jogo a mostra depois de criar um codinome, P_CNMENU_Handler): a roda 0x10000025
// com os oito estilos de botões (os nomes do *Txt.dat) no estilo do jogador, o controle desenhado
// (Menu_DisplayControllerStyle) e o botão 0x100000c9 com a inversão do olhar ("Normal"/"Inverted"), que
// C_RBCONTROL_Handler muda e grava.
int32_t P_CNCONTROLS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x4c) return 1;
    static const uint32_t kStyleNames[8] = {0x82, 0x91, 0xa0, 0xaf, 0xbe, 0xcd, 0x100018b, 0x100019a};
    __Menu_Send(index, 0x10000025, 0x17, 0, 0);
    for (int32_t i = 0; i < 8; ++i) __Menu_Send(index, 0x10000025, 0x10, txt(kStyleNames[i]), i);
    __Menu_Send(index, 0x10000025, 0x1e, int32_t(int16_t(PlayerSetting[0].style)), 0);
    Menu_DisplayControllerStyle(index, uint32_t(int32_t(int16_t(PlayerSetting[0].style))));
    was_inverted = PlayerSetting[0].invert_look;
    __Menu_Send(index, 0x100000c9, 0x18, txt(was_inverted == 0 ? 0x3bd : 0x206), 0);
    return 1;
}

// [decomp] 0x00217908 C_RBCONTROL_Handler
// A roda dos estilos: mudar (0x49) redesenha o controle; escolher (0x4b) grava o estilo (Menu_ChangeControllerStyle)
// e a inversão e volta; o terceiro botão (0x5e, círculo: "~X Invert") alterna a inversão mostrada.
int32_t C_RBCONTROL_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        cn_modified_flag = 1;
        Menu_ChangeControllerStyle(0, uint32_t(__Menu_SendMessage(c, 0x35, 0, 0)));
        PlayerSetting[0].invert_look = was_inverted;
        Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    } else if (message == 0x49) {
        Menu_DisplayControllerStyle(index, uint32_t(__Menu_SendMessage(c, 0x35, 0, 0)));
    } else if (message == 0x5e) {
        was_inverted = was_inverted == 0 ? 1 : 0;
        __Menu_Send(index, 0x100000c9, 0x18, txt(was_inverted != 0 ? 0x206 : 0x3bd), 0);
    }
    return 1;
}

// ---- Advanced Options, Multiplayer Options ----

// [decomp] 0x00213f08 P_CNOPTIONS_Handler
// "Advanced Options" (0x4000002d): uma roda por byte de PlayerSetting[0]: vibração (0x1000011a, +9), mira automática
// (0x1000011b, +1), mira sem mirar (0x10000124, +8), agachar e mirar alternando ou segurando (0x10000125 +4,
// 0x10000126 +3; "Toggle" 1, "Hold" 0), troca automática de arma (0x100001cb, +0xa), itens piscando (0x10000226, +0xc:
// "On" 2, "Off" 0) e o HUD sempre visível (0x1000023b, +0xb: "On" 0, "Off" 1). Escolher grava todas e volta.
int32_t P_CNOPTIONS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    PlayerInput_tag& p = PlayerSetting[0];
    if (message == 0x4b) {
        cn_modified_flag = 1;
        p.unknown_09 = uint8_t(radio_value(index, 0x1000011a));
        p.autoaim = uint8_t(radio_value(index, 0x1000011b));
        p.unknown_08 = uint8_t(radio_value(index, 0x10000124));
        p.crouch_toggle = uint8_t(radio_value(index, 0x10000125));
        p.aim_toggle = uint8_t(radio_value(index, 0x10000126));
        p.auto_switch = uint8_t(radio_value(index, 0x100001cb));
        p.unknown_0c = uint8_t(radio_value(index, 0x10000226));
        p.unknown_0b = uint8_t(radio_value(index, 0x1000023b));
        Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    } else if (message == 0x4c) {
        on_off(index, 0x1000011a, 1, 0, p.unknown_09);
        on_off(index, 0x1000011b, 1, 0, p.autoaim);
        on_off(index, 0x10000124, 1, 0, p.unknown_08);
        two_options(index, 0x10000125, 0x2a2, 1, 0x2a3, 0, p.crouch_toggle);
        two_options(index, 0x10000126, 0x2a2, 1, 0x2a3, 0, p.aim_toggle);
        on_off(index, 0x100001cb, 1, 0, p.auto_switch);
        on_off(index, 0x10000226, 2, 0, p.unknown_0c);
        on_off(index, 0x1000023b, 0, 1, p.unknown_0b);
    }
    return 1;
}

// [decomp] 0x002144d8 P_CNMPOPTIONS_Handler
// "Multiplayer Options" do codinome (0x4000002e; as opções do jogador no multijogador, gravadas no perfil): o radar
// (0x10000128, MPSettings +0x28), a vantagem de vida (0x1000011c, MPSettings +0x2c: -75 a +100) e a mira automática
// do multijogador (0x10000129, PlayerSetting::autoaim_mp). Não é a configuração de uma partida (P_MPSETUP); o
// multijogador não foi portado, os valores só ficam no perfil.
int32_t P_CNMPOPTIONS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        cn_modified_flag = 1;
        const int32_t radar = radio_value(index, 0x10000128);
        const int32_t handicap = radio_value(index, 0x1000011c);
        std::memcpy(MPSettings + 0x28, &radar, 4);
        std::memcpy(MPSettings + 0x2c, &handicap, 4);
        PlayerSetting[0].autoaim_mp = uint8_t(radio_value(index, 0x10000129));
        Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    } else if (message == 0x4c) {
        int32_t radar = 0, handicap = 0;
        std::memcpy(&radar, MPSettings + 0x28, 4);
        std::memcpy(&handicap, MPSettings + 0x2c, 4);
        on_off(index, 0x10000128, 1, 0, radar);
        __Menu_Send(index, 0x1000011c, 0x17, 0, 0);
        static const int32_t kValues[8] = {-0x4b, -0x32, -0x19, 0, 0x19, 0x32, 0x4b, 100};
        for (int i = 0; i < 8; ++i) __Menu_Send(index, 0x1000011c, 0x10, val(kHandicaps[i]), kValues[i]);
        __Menu_Send(index, 0x1000011c, 0x1e, handicap, 0);
        on_off(index, 0x10000129, 1, 0, PlayerSetting[0].autoaim_mp);
    }
    return 1;
}

// ---- AV Options e o ajuste de tela ----

// [decomp] 0x00214818 P_CNAVOPTIONS_Handler
// "AV Options" (0x40000031). Ao entrar vindo do menu do codinome: as barras de volume (0x10000135 música,
// 0x10000134 efeitos: 0 a 20, o volume / 5), as legendas (0x10000136, DrawInfo[2]), a tela dividida (0x1000019a,
// DrawInfo[0]: "Horizontal" 0, "Vertical" 1), o som (0x10000198: Mono 0, Stereo 1, Surround 4) e o widescreen
// (0x10000223, DrawInfo[1]); a posição da tela é guardada. A cada tick os volumes seguem as barras (para ouvir).
// Escolher: "Screen Adjust" (0x10000137) vai ao ajuste de tela, "Credits" (0x10000138) e o trailer (0x10000222) às
// páginas deles (o script do controle troca a página) e voltam com tudo como está; "Restore Defaults" (0x100001a3)
// põe os padrões nos controles (as legendas desligadas nas línguas em que o jogo as desliga) e a posição da tela no
// centro; qualquer outro controle grava tudo e volta. Sair sem gravar (e sem ir a uma das subpáginas) volta os
// volumes e a posição da tela aos de antes.
// Peculiaridade mantida: a roda de widescreen não é limpa (0x17) antes das opções; cada visita vinda do menu do
// codinome acrescenta mais um "On" e um "Off".
int32_t P_CNAVOPTIONS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x4c) {
        going_to_av_subscreen_498 = 0;
        if (uint32_t(b) != 0x4000001d) return 1;
        start_sfx_vol_496 = int32_t(SFXGetVolume()) / 5;
        start_music_vol_497 = int32_t(SFXMusicGetVolume()) / 5;
        psiGetScreenPos(&screen_adjust_x_499, &screen_adjust_y_500);
        __Menu_Send(index, 0x10000135, 0x27, 0, 0x14);
        __Menu_Send(index, 0x10000135, 0x69, 0, 0);
        __Menu_Send(index, 0x10000135, 0x2e, start_music_vol_497, 0);
        __Menu_Send(index, 0x10000134, 0x27, 0, 0x14);
        __Menu_Send(index, 0x10000134, 0x69, 0, 0);
        __Menu_Send(index, 0x10000134, 0x2e, start_sfx_vol_496, 0);
        on_off(index, 0x10000136, 1, 0, int32_t(DrawInfo[2]));
        two_options(index, 0x1000019a, 0x2ba, 0, 0x2b9, 1, int32_t(DrawInfo[0]));
        __Menu_Send(index, 0x10000198, 0x17, 0, 0);
        __Menu_Send(index, 0x10000198, 0x10, txt(0x393), 0);
        __Menu_Send(index, 0x10000198, 0x10, txt(0x394), 1);
        __Menu_Send(index, 0x10000198, 0x10, txt(0x395), 4);
        __Menu_Send(index, 0x10000198, 0x1e, int32_t(SFXGetMode()), 0);
        __Menu_Send(index, 0x10000223, 0x10, txt(0x1b7), 1);
        __Menu_Send(index, 0x10000223, 0x10, txt(0x1b6), 0);
        __Menu_Send(index, 0x10000223, 0x1e, int32_t(DrawInfo[1]), 0);
        return 1;
    }
    if (message == 0x50) {
        SFXMusicSetVolume(uint32_t(__Menu_Send(index, 0x10000135, 0x40, 0, 0) * 5));
        SFXSetVolume(uint32_t(__Menu_Send(index, 0x10000134, 0x40, 0, 0) * 5));
        return 1;
    }
    if (message == 0x4d) {
        if (going_to_av_subscreen_498 == 0) {
            SFXMusicSetVolume(uint32_t(start_music_vol_497 * 5));
            SFXSetVolume(uint32_t(start_sfx_vol_496 * 5));
            psiSetScreenPos(screen_adjust_x_499, screen_adjust_y_500);
        }
        return 1;
    }
    if (message != 0x4b) return 1;
    const M_CONTROL* chosen = ptr<M_CONTROL>(b);
    const uint32_t id = chosen != nullptr ? chosen->id : 0;  // só no port: sem controle, o original leria por nulo
    if (id == 0x10000138 || id == 0x10000222) {
        going_to_av_subscreen_498 = 1;
        return 1;
    }
    if (id == 0x10000137) {
        going_to_av_subscreen_498 = 1;
        __Menu_Send(index, 0x10000137, 0x2b, 1, 0);
        Manager_SendMessage(mgr(index), 0x44, 0x40000047, 0);
        __Menu_Send(index, 0x10000137, 0x2b, 0, 0);
        return 1;
    }
    if (id == 0x100001a3) {
        __Menu_Send(index, 0x10000135, 0x2e, 0xe, 0);
        __Menu_Send(index, 0x10000134, 0x2e, 0x14, 0);
        const bool subtitles = LanguageToLoad < 0 || (3 < LanguageToLoad && LanguageToLoad != 6);
        __Menu_Send(index, 0x10000136, 0x1e, subtitles ? 1 : 0, 0);
        __Menu_Send(index, 0x1000019a, 0x1e, 0, 0);
        __Menu_Send(index, 0x10000198, 0x1e, 1, 0);
        psiRestoreScreenPos();
        __Menu_Send(index, 0x10000223, 0x1e, 0, 0);
        return 1;
    }
    cn_modified_flag = 1;
    start_music_vol_497 = __Menu_Send(index, 0x10000135, 0x40, 0, 0);
    start_sfx_vol_496 = __Menu_Send(index, 0x10000134, 0x40, 0, 0);
    DrawInfo[2] = uint32_t(radio_value(index, 0x10000136));
    DrawInfo[0] = uint32_t(radio_value(index, 0x1000019a));
    SFXSetMode(radio_value(index, 0x10000198));
    DrawInfo[1] = uint32_t(radio_value(index, 0x10000223));
    psiGetScreenPos(&screen_adjust_x_499, &screen_adjust_y_500);
    Manager_SendMessage(mgr(index), 0x5f, 0, 0);
    return 1;
}

// [decomp] 0x00217a58 P_SCREENADJUST_Handler
// O ajuste de tela (0x40000047, só no PS2: a moldura de cantos e as duas barras de C_SBSCREEN_Handler). Ao entrar
// guarda a posição; escolher volta com a nova; voltar (0x6b) põe a de antes. No PC a posição só é guardada
// (psiSetScreenPos, platform/psi.h): nada se move na janela.
int32_t P_SCREENADJUST_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        psiGetScreenPos(&PS2OldX, &PS2OldY);
    } else if (message == 0x4b) {
        Manager_SendMessage(mgr(index), 0x5f, 1, 0);
    } else if (message == 0x6b) {
        psiSetScreenPos(PS2OldX, PS2OldY);
    }
    return 1;
}

// [decomp] 0x00217b08 C_SBSCREEN_Handler
// As duas barras do ajuste de tela (tag 0 vertical, 1 horizontal; de -1 a 1, paradas no meio): cada passo move a
// imagem (psiAdjustScreenPos) e a barra volta ao meio.
int32_t C_SBSCREEN_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x49) {
        const int32_t step = __Menu_SendMessage(c, 0x40, 0, 0);
        if (c->tag == 0) {
            psiAdjustScreenPos(0, step);
        } else if (c->tag == 1) {
            psiAdjustScreenPos(step, 0);
        }
        __Menu_SendMessage(c, 0x2e, 0, 0);
    } else if (message == 0x51) {
        __Menu_SendMessage(c, 0x27, -1, 1);
        __Menu_SendMessage(c, 0x2e, 0, 0);
    }
    return 1;
}

// ---- créditos ----

// [decomp] 0x00202420 Menu_InitCredits
void Menu_InitCredits() { ptr_credits = nullptr; }

// [decomp] 0x00202428 Menu_KillCredits
// Vazia no original (as linhas ficam no heap do menu até ele fechar).
void Menu_KillCredits() {}

// [decomp] 0x001f9a88 Menu_SetupCredits
// As linhas dos créditos (0x242 de 12 bytes: nome, cargo, a fonte de cada um, uma linha só): os rótulos do *Txt.dat
// lidos agora (Txt_BindLabel), as strings do ELF da tabela; na primeira vez desde Menu_InitCredits, copiadas para o
// heap do menu (ptr_credits). Devolve as linhas e põe a contagem em *count.
// Divergência: o original monta as 578 linhas com o código (22 KB de instruções); o port as monta da tabela que
// game/menu_tables.cpp tirou desse código na partida (CreditsSource), e a contagem é a da tabela (0 sem ela).
CREDIT_tag* Menu_SetupCredits(uint32_t* count) {
    static CREDIT_tag lines[kCreditsMax];
    std::memset(lines, 0, sizeof(lines));
    for (uint32_t i = 0; i < CreditsSourceCount && i < uint32_t(kCreditsMax); ++i) {
        const CREDIT_SOURCE& s = CreditsSource[i];
        lines[i].name = s.name_label != 0 ? Txt_BindLabel(s.name_label, 0) : s.name_text;
        lines[i].title = s.title_label != 0 ? Txt_BindLabel(s.title_label, 0) : s.title_text;
        lines[i].name_font = s.name_font;
        lines[i].title_font = s.title_font;
        lines[i].single = s.single;
    }
    if (ptr_credits == nullptr) {
        ptr_credits = static_cast<CREDIT_tag*>(Menu_Malloc(sizeof(lines), 0xc));
        if (ptr_credits != nullptr) amemcpy(ptr_credits, lines, sizeof(lines));
    }
    *count = CreditsSourceCount;
    return ptr_credits;
}

// [decomp] 0x002152b0 P_CREDITS_Handler
// Os créditos (0x40000030; da AV Options ou depois do fim do jogo, P_WINGAME_Handler): a música 0x27 no lugar da do
// menu; a cada 14 ticks a linha seguinte entra embaixo da tela (os rótulos 0x10000213/0x10000214 de tags 0-25,
// reusados em roda: nome e cargo em duas colunas, ou uma linha larga) e todas sobem 2 pixels por tick; nas últimas
// 50 linhas a música baixa; 18 linhas depois da última a página escurece e volta (ou vai ao menu principal, depois do
// fim do jogo). Sair: a música e o volume do menu de volta.
int32_t P_CREDITS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x4d) {
        Menu_KillCredits();
        SFXStopMusic();
        SFXMusicSetVolume(music_vol_524);
        if (menu_movie == 0) {
            SFXRemove(0, 0x1d7);
            SFXRemove(0, 0x470);
            SFXStart(0x1d7, 0);
            SFXStart(0x470, 0);
            menu_movie = 1;
        }
        PS2_SFXWaitTillFinished();
        return 1;
    }
    if (message == 0x4c) {
        frame_518 = 0;
        credits_516 = Menu_SetupCredits(&count_521);
        index_519 = 0;
        name_520 = 0;
        page_from_523 = b;
        __Menu_Send(index, 0x10000213, 0x62, 0, 0);
        __Menu_Send(index, 0x10000214, 0x62, 0, 0);
        __Menu_Send(index, 0x10000213, 0x2b, 1, 0);
        __Menu_Send(index, 0x10000214, 0x2b, 1, 0);
        __Menu_Send(index, 0x10000215, 0x1a, 0, 0);
        fading_517 = 0;
        __Menu_Send(index, 0x1000023c, 0x1a, GameState.unknown_52 == 0 ? 0xff : 0x404040ff, 0);
        music_vol_524 = SFXMusicGetVolume();
        SFXRemove(0, 0x1d7);
        SFXRemove(0, 0x470);
        menu_movie = 0;
        SFXStartMusic(0x27, 0);
        return 1;
    }
    if (message == 0x51) {
        Menu_InitCredits();
        return 1;
    }
    if (message != 0x50 || credits_516 == nullptr) return 1;
    ++frame_518;
    if (frame_518 % 0xe == 0 && name_520 < count_521) {
        M_CONTROL* name = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000213, index_519, 0x39, 0, 0));
        M_CONTROL* title = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000214, index_519, 0x39, 0, 0));
        if (name == nullptr || title == nullptr) return 1;
        const CREDIT_tag& line = credits_516[name_520];
        name->y = 0x200;
        title->y = 0x200;
        __Menu_SendMessage(name, 0x2b, 2, 0);
        __Menu_SendMessage(name, 0x18, val(line.name), 0);
        __Menu_SendMessage(name, 0x23, val(credit_format(line.name_font)), 0);
        __Menu_SendMessage(title, 0x23, val(credit_format(line.title_font)), 0);
        if (line.single == 0) {
            name->x = 0x10;
            title->x = 0xf0;
            name->w = 0x100;
            title->w = 0x100;
            __Menu_SendMessage(title, 0x2b, 2, 0);
            __Menu_SendMessage(title, 0x18, val(line.title), 0);
        } else {
            name->w = 0x1e0;
            name->x = 0x10;
            __Menu_SendMessage(title, 0x2b, 1, 0);
        }
        index_519 = (index_519 + 1) % 0x1a;
        ++name_520;
    }
    if (count_521 - 0x32 < frame_518 / 0xe) {
        const float v = float(int32_t(count_521 * 0xe - frame_518)) * (float(music_vol_524) / 700.0f);
        const int32_t i = int32_t(v);
        SFXMusicSetVolume(uint32_t(int32_t(float(0 < i ? i : 0))));
    }
    if (fading_517 == 0 && count_521 + 0x12 < frame_518 / 0xe) {
        fading_517 = 1;
        M_CONTROL* m = &mgr(index)->base;
        if (uint32_t(page_from_523) == 0x40000053) {
            __Menu_SendDelayedMessage(0x13, m, 0x44, 0x40000002, 0);
        } else {
            __Menu_SendDelayedMessage(0x13, m, 0x5f, 0, 0);
        }
        __Menu_SendDelayedMessage(0x13, m, 0x68, 0, 0);
        M_MESSAGE msg{};
        msg.message = 0xe1;
        msg.a = 0xff;
        msg.b = 0;
        Process_Create(ptr<M_CONTROL>(__Menu_Send(index, 0x10000215, 0x39, 0, 0)), &msg, 0xf, 0);
    }
    for (uint32_t tag = 0; tag < 0x1a; ++tag) {
        M_CONTROL* name = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000213, tag, 0x39, 0, 0));
        M_CONTROL* title = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000214, tag, 0x39, 0, 0));
        if (name != nullptr && title != nullptr) {
            name->y = int16_t(name->y - 2);
            title->y = int16_t(title->y - 2);
        }
    }
    return 1;
}

// ---- só no port ----

const char* Menu_PortTableString(const char* s) {
    if (s == nullptr) return nullptr;
    const size_t n = std::strlen(s) + 1;
    if (sizeof(table_pool) < table_pool_used + n) return nullptr;
    char* out = table_pool + table_pool_used;
    std::memcpy(out, s, n);
    table_pool_used += uint32_t(n);
    return out;
}

void Menu_PortClearTables() {
    std::memset(CreditsSource, 0, sizeof(CreditsSource));
    CreditsSourceCount = 0;
    std::memset(NisListSource, 0, sizeof(NisListSource));
    NisListCount = 0;
    std::memset(LevelListSource, 0, sizeof(LevelListSource));
    LevelListCount = 0;
    table_pool_used = 0;
}

void menu_options_debug_power_on() {
    was_inverted = 0;
    PS2OldX = PS2OldY = 0;
    ptr_credits = nullptr;
    going_to_av_subscreen_498 = 0;
    start_sfx_vol_496 = start_music_vol_497 = 0;
    screen_adjust_x_499 = screen_adjust_y_500 = 0;
    credits_516 = nullptr;
    fading_517 = 0;
    frame_518 = index_519 = name_520 = count_521 = 0;
    page_from_523 = 0;
    music_vol_524 = 0;
}
