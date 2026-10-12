// As caixas de marcar da página de depuração (C_CHCH*) e, só do port, os nomes delas, as linhas da página de fim de
// missão por trapaça, o atalho --debug-menu e o perfil --unlock-all. Ver menu_cheats.h.
//
// Divergências (comentadas nas funções):
// - C_CHCHWEAP_Handler, C_CHCHFLY_Handler, C_CHCHZEROG_Handler: sem jogador não fazem nada no jogador (o original o
//   leria nulo); contam em g_menu_cheat_guards.no_player.
// - C_CHCHWS_Handler: no PC quem desenha troca a proporção pela da janela depois de Camera_CalcViewAngles, então a
//   caixa só muda DrawInfo[1] (o que o perfil grava) e o campo de visão das câmeras volta aos 60 graus.
// - C_CHCHMUSIC_Handler: sem módulo de som, SFXMusicSetVolume só guarda o volume (platform/psi_sound.h); o valor
//   da trapaça fica em SoundInfo.

#include "engine/menu_cheats.h"

#include <cstdio>
#include <cstring>

#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gamestate.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/menu_frontend.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/pc_text.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/view.h"

uint8_t hud_state = 1;
uint32_t bDebugLockup = 1;
uint32_t bTweakBrightness = 0;
uint32_t bDebugControls = 1;
uint32_t switch_CONFIG_WIDESCREEN = 0;
MenuCheatGuards g_menu_cheat_guards;

namespace {

uint8_t set_214 = 0;  // Set.214 (0x0030d2fd): a gravidade zero ligada pela caixa

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }
template <class T>
T* ptr(int32_t v) {
    return nf::p32_ptr<T>(uint32_t(v));
}
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
int32_t checked(M_CONTROL* c) { return __Menu_SendMessage(c, 0x40, 0, 0); }
void show(M_CONTROL* c, int32_t v) { __Menu_SendMessage(c, 0x2e, v, 0); }

// Só do port: os nomes das caixas e do botão sem nome da página de depuração (o disco tem "1" em todos).
struct DebugRow {
    uint32_t id;
    PcText text;
};
constexpr DebugRow kDebugRows[] = {
    {0x1000021d, PcText::DbgControls}, {0x10000019, PcText::DbgFly},      {0x100000e7, PcText::DbgWeapons},
    {0x100000e8, PcText::DbgInvincible}, {0x1000001c, PcText::DbgHud},    {0x1000001a, PcText::DbgCoords},
    {0x10000012, PcText::DbgWidescreen}, {0x10000015, PcText::DbgDrones}, {0x10000016, PcText::DbgBlind},
    {0x10000017, PcText::DbgDroneDebug}, {0x100000bd, PcText::DbgDummy},  {0x100000c7, PcText::DbgFreeze},
    {0x1000001b, PcText::DbgMusic},     {0x10000101, PcText::DbgDrawAll}, {0x10000018, PcText::DbgZeroG},
    {0x10000169, PcText::DbgLockup},    {0x100001f7, PcText::DbgBright},  {0x10000216, PcText::DbgUnlock},
    {0x1000021e, PcText::DbgEndMission},
};

// Só do port: a página de depuração não tem tratador no jogo; ao entrar, os nomes.
int32_t debug_page(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        for (const DebugRow& r : kDebugRows) __Menu_Send(index, r.id, 0x18, val(PcText_Get(r.text)), 0);
    }
    return 1;
}

// Só do port: a lista da página 0x4000004c vem vazia no disco (P_CHEATMEDAL_Handler a limpa ao entrar e escolher
// nela dava 0, fracassar); o port põe as seis escolhas do tratador (valor = a escolha).
MenuHandler g_prev_cheat_medal = nullptr;
int32_t cheat_medal_page(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b) {
    const int32_t r = g_prev_cheat_medal != nullptr ? g_prev_cheat_medal(index, c, id, message, a, b) : 0;
    if (message == 0x4c) {
        static const PcText kRows[6] = {PcText::DbgFail,   PcText::DbgComplete, PcText::DbgBronze,
                                        PcText::DbgSilver, PcText::DbgGold,     PcText::DbgPlatinum};
        for (int32_t i = 0; i < 6; ++i) __Menu_Send(index, 0x1000021c, 0x10, val(PcText_Get(kRows[i])), i);
        __Menu_Send(index, 0x1000021c, 0x1d, 0, 0);
    }
    return r;
}

}  // namespace

// [decomp] 0x00216168 C_CHCHWS_Handler
// 16:9: DrawInfo[1] e switch_CONFIG_WIDESCREEN, e as câmeras 0-8 refazem a projeção (60 graus).
int32_t C_CHCHWS_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        DrawInfo[1] = uint32_t(checked(c));
        switch_CONFIG_WIDESCREEN = DrawInfo[1];
        for (uint16_t i = 0; i < 9; ++i) Camera_CalcViewAngles(i, 1.0471976f);
    } else if (message == 0x51) {
        show(c, int32_t(switch_CONFIG_WIDESCREEN));
    }
    return 1;
}

// [decomp] 0x00215cf0 C_CHCHDRONES_Handler
// Marcada, os personagens existem; desmarcada liga switch_NO_DRONES (Drone_InitComms os pausa e esconde).
int32_t C_CHCHDRONES_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        switch_NO_DRONES = checked(c) == 0 ? 1 : 0;
    } else if (message == 0x51) {
        show(c, switch_NO_DRONES == 0 ? 1 : 0);
    }
    return 1;
}

// [decomp] 0x00215d60 C_CHCHBLIND_Handler
int32_t C_CHCHBLIND_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        switch_BLIND_DRONES = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(switch_BLIND_DRONES));
    }
    return 1;
}

// [decomp] 0x00216098 C_CHCHDEBUG_Handler
int32_t C_CHCHDEBUG_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        Drone_DebugSpr = uint8_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(Drone_DebugSpr)));
    }
    return 1;
}

// [decomp] 0x00216388 C_CHCHZEROG_Handler
// Gravidade zero: o jogador 0 passa ao subestado 9 (Player_ZeroG) ou volta a andar.
int32_t C_CHCHZEROG_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        set_214 = uint8_t(checked(c));
        // Só do port: sem jogador, nada (o original passaria nulo a Player_ChangeSubState).
        if (glb_players[0] == nullptr) {
            ++g_menu_cheat_guards.no_player;
        } else {
            Player_ChangeSubState(glb_players[0], set_214 != 0 ? 9 : kPlrWalk);
        }
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(set_214)));
    }
    return 1;
}

// [decomp] 0x00215c70 C_CHCHFLY_Handler
// Voo livre: debug_scanmode (e debug_clipmode, que nada lê) e o jogador 0 no subestado 5 (Player_SSScanMode) ou de
// volta a andar.
int32_t C_CHCHFLY_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        const uint8_t v = uint8_t(checked(c));
        debug_scanmode = v;
        debug_clipmode = v;
        // Só do port: sem jogador, nada (o original passaria nulo a Player_ChangeSubState).
        if (glb_players[0] == nullptr) {
            ++g_menu_cheat_guards.no_player;
        } else {
            Player_ChangeSubState(glb_players[0], v != 0 ? kPlrScanMode : kPlrWalk);
        }
    } else if (message == 0x51) {
        show(c, int32_t(debug_scanmode));
    }
    return 1;
}

// [decomp] 0x00216100 C_CHCHCOORDS_Handler
int32_t C_CHCHCOORDS_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        switch_SHOW_COORDS = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(switch_SHOW_COORDS));
    }
    return 1;
}

// [decomp] 0x00215bf0 C_CHCHMUSIC_Handler
// A música: o volume 100 ou 0 (SoundInfo[0], SFXMusicSetVolume).
int32_t C_CHCHMUSIC_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        SoundInfo[0] = checked(c) != 0 ? 100 : 0;
        SFXMusicSetVolume(SoundInfo[0]);
    } else if (message == 0x51) {
        show(c, SoundInfo[0] != 0 ? 1 : 0);
    }
    return 1;
}

// [decomp] 0x00215f48 C_CHCHHUD_Handler
// O HUD: hud_state em cada jogador que existe (a opção da mira, PlayerSetting +8, e a do multijogador, MPSettings
// +0x28) e os 22 painéis dele ligados ou desligados (HUD_Enable).
int32_t C_CHCHHUD_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        hud_state = uint8_t(checked(c));
        for (uint32_t i = 0; i < 4; ++i) {
            if (glb_blokes[i] == nullptr) continue;
            PlayerSetting[i].unknown_08 = hud_state;
            wr32(MPSettings + i * 0x30 + 0x28, uint32_t(int32_t(int8_t(hud_state))));
            for (uint32_t pane = 0; pane < 0x16; ++pane) HUD_Enable(glb_blokes[i]->hud, pane, int8_t(hud_state), 0);
        }
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(hud_state)));
    }
    return 1;
}

// [decomp] 0x002162b8 C_CHCHDUMMY_Handler
int32_t C_CHCHDUMMY_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        Drone_DummyPlayer = uint8_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(Drone_DummyPlayer)));
    }
    return 1;
}

// [decomp] 0x00216258 C_CHCHALLOWFREEZE_Handler
int32_t C_CHCHALLOWFREEZE_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        switch_AllowFreeze = uint8_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(switch_AllowFreeze)));
    }
    return 1;
}

// [decomp] 0x00215dc8 C_CHCHWEAP_Handler
// Todas as armas: CheatInfo +8 e +4 (o perfil grava os dois no bloco CHET); marcada, o jogador 0 recebe as armas 1 a
// 0x47 e os aparelhos 0x4a a 0x5e com 999 tiros e o colete 50; depois confere as armas carregadas.
int32_t C_CHCHWEAP_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        wr32(CheatInfo + 8, uint32_t(checked(c)));
        const int32_t on = checked(c);
        wr32(CheatInfo + 4, uint32_t(on));
        BLData* b = glb_blokes[0];
        // Só do port: sem jogador, nada (o original leria o jogador nulo).
        if (b == nullptr) {
            ++g_menu_cheat_guards.no_player;
            return 1;
        }
        if (on != 0) {
            for (int16_t w = 1; w < 0x48; ++w) Player_EquipWeapon(b, w, 999);
            for (int16_t w = 0x4a; w < 0x5f; ++w) Player_EquipWeapon(b, w, 999);
            b->armour = 50.0f;
        }
        Player_CheckWeaponsLoaded(b);
    } else if (message == 0x51) {
        show(c, int32_t(rd32(CheatInfo + 4)));
    }
    return 1;
}

// [decomp] 0x00215ee0 C_CHCHHEALTH_Handler
// Invencível: CheatInfo +0 (Player_HandlePain).
int32_t C_CHCHHEALTH_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        wr32(CheatInfo, uint32_t(checked(c)));
    } else if (message == 0x51) {
        show(c, int32_t(rd32(CheatInfo)));
    }
    return 1;
}

// [decomp] 0x00216320 C_CHCHDRAWALL_Handler
int32_t C_CHCHDRAWALL_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        switch_ForceDrawAll = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(switch_ForceDrawAll));
    }
    return 1;
}

// [decomp] 0x00216400 C_CHCHLOCKUP_Handler
int32_t C_CHCHLOCKUP_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        bDebugLockup = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(bDebugLockup));
    }
    return 1;
}

// [decomp] 0x00216460 C_CHCHBRIGHT_Handler
int32_t C_CHCHBRIGHT_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        bTweakBrightness = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(bTweakBrightness));
    }
    return 1;
}

// [decomp] 0x002164c0 C_CHCHUNLOCK_Handler
// Tudo liberado nos menus (menu_unlock_everything: o mapa das missões, as dificuldades, os extras do multijogador).
int32_t C_CHCHUNLOCK_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        menu_unlock_everything = uint8_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(int8_t(menu_unlock_everything)));
    }
    return 1;
}

// [decomp] 0x00216520 C_CHCHCONTROLS_Handler
int32_t C_CHCHCONTROLS_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        bDebugControls = uint32_t(checked(c));
    } else if (message == 0x51) {
        show(c, int32_t(bDebugControls));
    }
    return 1;
}

// ---- só do port ----

void MenuCheats_Install() {
    Menu_PortRegisterHandler(kCheatsPage, "P_DEBUG (PC)", debug_page);
    const MenuHandlerEntry* e = Menu_FindHandler(kCheatMedalPage);
    if (e == nullptr || e->handler != cheat_medal_page) {
        g_prev_cheat_medal = e != nullptr ? e->handler : nullptr;
        Menu_PortRegisterHandler(kCheatMedalPage, "P_CHEATMEDAL_Handler (PC)", cheat_medal_page);
    }
}

bool Menu_PortShowCheats(uint8_t player) {
    if (3 < player || glb_players[player] == nullptr || glb_blokes[player] == nullptr) return false;
    const uint32_t status = MenuManager_GetStatus();
    if (status == 0) {
        if (GameState.unknown_24 != 0 || ScriptCam != 0) return false;
        BLData* b = glb_blokes[player];
        b->menu = int16_t(MenuManager_Create(0x80000002, 0x4000004b, b->player, uint8_t(b->player), 0x1e, 1, 3));
        GS_PauseGame(1);
    } else if (status != 3) {
        return false;
    }
    M_MANAGER* m = &menu_manager[player];
    if (m->active == 0 || m->page == nullptr) return false;
    if (m->page->base.id == kCheatsPage) return true;
    Manager_SendMessage(m, 0x44, int32_t(kCheatsPage), 0);
    return Menu_CurrentPage(player) == kCheatsPage;
}

void Menu_PortUnlockAll() {
    for (uint32_t i = 0; i < 12; ++i) {
        uint32_t level;
        std::memcpy(&level, sp_level + i * 0x18 + 0xc, 4);
        Menu_SetLevelBonus(level, 4, 0);
    }
    menu_bonus[0xa4] = 0;
    const uint32_t missions = Menu_GetNightfireStatus();
    const uint64_t bonus = Menu_GetBonus(0);
    for (uint32_t slot = 0; slot < 2; ++slot) {
        uint8_t* d = def_codename + slot * 0x38;
        wr32(d + 4, missions);
        std::memcpy(d + 8, &bonus, 8);
        d[0x19] = GameState.unknown_52;
    }
}

int MenuCheats_Describe(char* out, size_t size) {
    return std::snprintf(out, size,
                         "trapacas pagina %08x hud %d invencivel %u armas %u/%u voo %u zero_g %u subestado %d coords %u "
                         "tela_larga %u sem_personagens %u cegos %u falso %u depura_ia %u congelar %u musica %u "
                         "todas_salas %u liberar %u travas %u brilho %u controles %u | guardas %u",
                         Menu_CurrentPage(0), int(int8_t(hud_state)), rd32(CheatInfo), rd32(CheatInfo + 4),
                         rd32(CheatInfo + 8), debug_scanmode, set_214,
                         glb_players[0] != nullptr ? int(int16_t(glb_players[0]->unknown_f6)) : -1, switch_SHOW_COORDS,
                         switch_CONFIG_WIDESCREEN, switch_NO_DRONES, switch_BLIND_DRONES, Drone_DummyPlayer,
                         Drone_DebugSpr, switch_AllowFreeze, SoundInfo[0], switch_ForceDrawAll, menu_unlock_everything,
                         bDebugLockup, bTweakBrightness, bDebugControls, g_menu_cheat_guards.no_player);
}

void menu_cheats_debug_power_on() {
    hud_state = 1;
    bDebugLockup = 1;
    bTweakBrightness = 0;
    bDebugControls = 1;
    switch_CONFIG_WIDESCREEN = 0;
    set_214 = 0;
    Drone_DebugSpr = 0;
    g_prev_cheat_medal = nullptr;
    g_menu_cheat_guards = MenuCheatGuards{};
}
