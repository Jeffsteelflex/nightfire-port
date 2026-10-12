// As subpáginas da página de depuração 0x40000007 do menu de jogo: os ajustes de dano e de tiro, "Previews" (as
// cenas) e a lista das fases. Ver menu_tweaks.h.
//
// Divergências (comentadas nas funções): os valores mostrados vão para os buffers com snprintf de 0x20 bytes (o
// original usa sprintf); os itens das duas listas vêm da tabela tirada do ELF na partida (game/menu_tables.cpp).

#include "engine/menu_tweaks.h"

#include <cstdio>
#include <cstring>

#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/loader.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/game.h"
#include "engine/view.h"

MENU_LIST_ITEM NisListSource[kNisListMax];
uint32_t NisListCount = 0;
MENU_LIST_ITEM LevelListSource[kLevelListMax];
uint32_t LevelListCount = 0;
SCRIPTINFO* FMVScript = nullptr;
uint8_t show_frame_count = 0;

namespace {

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }
int32_t value_of(uint8_t index, uint32_t id) { return __Menu_Send(index, id, 0x40, 0, 0); }

// Os textos do .rodata (0x00308cb0-0x00308f08, 0x0030d320): os rótulos de desenvolvedor das duas páginas.
char kTweaksHelp[] = "~A Save changes ~Y More tweaks ~B Back";
char kTweaks2Help[] = "~A Save changes  ~B Back";
char kFormat[] = "%f";  // 0x0030d328

// Um ajuste: o id da barra (e do rótulo do nome, com o mesmo id), o nome, a escala (barra = valor x escala) e o rótulo
// que mostra o valor.
struct Tweak {
    uint32_t id;
    char name[24];
    float* value;
    float scale;
    uint32_t shown;
};

// P_TWEAKS_Handler, na ordem do original (os nomes ao entrar; o valor mostrado a cada tick, buf_236).
Tweak kDamage[18] = {
    {0x100001a6, "Drone Easy", &DroneDamage_Easy, 2.0f, 0x100001b9},
    {0x100001a7, "Drone Normal", &DroneDamage_Normal, 2.0f, 0x100001ba},
    {0x100001a8, "Drone Hard", &DroneDamage_Hard, 2.0f, 0x100001ca},
    {0x100001a9, "Drone Head", &DroneDamage_Head, 4.0f, 0x100001bb},
    {0x100001aa, "Drone Arms", &DroneDamage_Arms, 4.0f, 0x100001bd},
    {0x100001ab, "Drone Legs", &DroneDamage_Legs, 4.0f, 0x100001bc},
    {0x100001ac, "Drone Torso", &DroneDamage_Torso, 4.0f, 0x100001be},
    {0x100001ad, "Armour Helmet", &DroneArmour_Helmet, 5.0f, 0x100001bf},
    {0x100001ae, "Armour Combat", &DroneArmour_Combat, 5.0f, 0x100001c0},
    {0x100001af, "Armour Jacket", &DroneArmour_Jacket, 5.0f, 0x100001c1},
    {0x100001b0, "Armour Vest", &DroneArmour_Vest, 5.0f, 0x100001c2},
    {0x100001b2, "Multiplayer", &Plr_DMod_Multi, 10.0f, 0x100001c3},
    {0x100001b8, "MP Head", &Plr_DMod_Head, 10.0f, 0x100001c4},
    {0x100001b3, "MP L Limb", &Plr_DMod_LowerLimb, 10.0f, 0x100001c5},
    {0x100001b4, "MP U Limb", &Plr_DMod_UpperLimb, 10.0f, 0x100001c6},
    {0x100001b5, "Diff Easy", &Plr_DMod_Easy, 20.0f, 0x100001c7},
    {0x100001b6, "Diff Normal", &Plr_DMod_Normal, 20.0f, 0x100001c8},
    {0x100001b7, "Diff Hard", &Plr_DMod_Hard, 20.0f, 0x100001c9},
};
// P_TWEAKS2_Handler (buf_240).
Tweak kFiring[14] = {
    {0x100001df, "Drone Delay Min", &DroneFiring.burst_delay_min, 1.0f, 0x100001e4},
    {0x100001e0, "Drone Delay Norm", &DroneFiring.burst_delay_normal, 1.0f, 0x100001e6},
    {0x100001e1, "Drone Delay Max", &DroneFiring.burst_delay_max, 1.0f, 0x100001e7},
    {0x100001e2, "Drone Dist Min", &DroneFiring.burst_delay_min_dist, 1.0f, 0x100001e8},
    {0x100001e3, "Drone Dist Max", &DroneFiring.burst_delay_max_dist, 1.0f, 0x100001e9},
    {0x100001fe, "Firing acc easy", &DroneFiring.accuracy_easy, 4.0f, 0x100001ff},
    {0x10000200, "Firing acc norm", &DroneFiring.accuracy_normal, 4.0f, 0x10000202},
    {0x10000201, "Firing acc hard", &DroneFiring.accuracy_hard, 4.0f, 0x10000203},
    {0x10000205, "New Sight time to acc", &DroneFiring.new_sighting_time_to_hit, 4.0f, 0x10000209},
    {0x10000206, "Move target time to acc", &DroneFiring.target_first_moved_time_to_hit, 4.0f, 0x1000020a},
    {0x10000207, "Move target accuracy", &DroneFiring.target_first_moved_accuracy, 4.0f, 0x1000020b},
    {0x10000208, "Move target acc move", &DroneFiring.target_moving_accuracy, 4.0f, 0x1000020c},
    {0x1000020f, "Too close distance", &DroneFiring.too_close_distance, 4.0f, 0x10000211},
    {0x10000210, "Too close accuracy", &DroneFiring.too_close_accuracy, 4.0f, 0x10000212},
};
char buf_236[18][0x20];  // 0x0034cb28
char buf_240[15][0x20];  // 0x0034cd68 (o último nunca é escrito: o rótulo 0x1000020e fica vazio)

// O valor de volta da barra: a barra / a escala (o original multiplica por 0.5, 0.25, 0.2, 0.1, 0.05; sem escala,
// o inteiro).
float from_bar(int32_t bar, float scale) {
    if (scale == 2.0f) return float(bar) * 0.5f;
    if (scale == 4.0f) return float(bar) * 0.25f;
    if (scale == 5.0f) return float(bar) * 0.2f;
    if (scale == 10.0f) return float(bar) * 0.1f;
    if (scale == 20.0f) return float(bar) * 0.05f;
    return float(bar);
}
// A barra ao entrar: o valor x a escala, truncado (Drone x2 é a soma do valor com ele mesmo, que dá o mesmo).
int32_t to_bar(float v, float scale) { return int32_t(v * scale); }

void tweaks_enter(uint8_t index, Tweak* list, int count, char* help, uint32_t help_id) {
    __Menu_Send(index, help_id, 0x18, val(help), 0);
    for (int i = 0; i < count; ++i) __Menu_Send(index, list[i].id, 0x18, val(list[i].name), 0);
}
void tweaks_values(uint8_t index, Tweak* list, int count) {
    for (int i = 0; i < count; ++i) __Menu_Send(index, list[i].id, 0x2e, to_bar(*list[i].value, list[i].scale), 0);
}
void tweaks_save(uint8_t index, Tweak* list, int count) {
    for (int i = 0; i < count; ++i) *list[i].value = from_bar(value_of(index, list[i].id), list[i].scale);
    Manager_SendMessage(mgr(index), 0x5f, 0, 0);
}
void tweaks_show(uint8_t index, Tweak* list, int count, char (*buf)[0x20]) {
    for (int i = 0; i < count; ++i) {
        std::snprintf(buf[i], 0x20, kFormat, double(from_bar(value_of(index, list[i].id), list[i].scale)));
    }
    for (int i = 0; i < count; ++i) __Menu_Send(index, list[i].shown, 0x18, val(buf[i]), 0);
}

_VECTOR zero_vector{};  // CONSTANT_ZERO_VECTOR (Script_Load recebe o endereço)

}  // namespace

// [decomp] 0x002067c8 P_TWEAKS_Handler
// Os ajustes de dano (0x40000044): ao entrar os nomes nos rótulos e os multiplicadores nas barras (os dos
// personagens pela dificuldade x2, pelo lugar x4, as proteções x5; os do jogador no multijogador x10, pela
// dificuldade x20); a cada tick o valor de cada barra ao lado ("%f"); X grava e volta; Y vai aos ajustes de tiro.
int32_t P_TWEAKS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        tweaks_enter(index, kDamage, 18, kTweaksHelp, 0x100001de);
        tweaks_values(index, kDamage, 18);
    } else if (message == 0x4b) {
        tweaks_save(index, kDamage, 18);
    } else if (message == 0x50) {
        tweaks_show(index, kDamage, 18, buf_236);
    } else if (message == 0x5d) {
        Manager_SendMessage(mgr(index), 0x44, 0x40000046, 0);
    }
    return 1;
}

// [decomp] 0x00207950 P_TWEAKS2_Handler
// Os ajustes de tiro da IA (0x40000046, DroneFiring): as pausas entre rajadas e as distâncias como estão, as
// precisões e os tempos x4; o rótulo 0x1000020d escondido; o último valor mostrado (0x1000020e) é um buffer nunca
// escrito, vazio. X grava e volta.
int32_t P_TWEAKS2_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        tweaks_enter(index, kFiring, 14, kTweaks2Help, 0x100001ea);
        __Menu_Send(index, 0x1000020d, 0x2b, 1, 0);
        tweaks_values(index, kFiring, 14);
    } else if (message == 0x4b) {
        tweaks_save(index, kFiring, 14);
    } else if (message == 0x50) {
        tweaks_show(index, kFiring, 14, buf_240);
        __Menu_Send(index, 0x1000020e, 0x18, val(buf_240[14]), 0);
    }
    return 1;
}

// [decomp] 0x00216580 P_NIS_Handler
// "Previews" (0x4000000c): enquanto a cena escolhida toca, um passo dela e dos objetos por tick; no fim o jogador de
// volta, as câmeras como estavam e a câmera de script desligada.
int32_t P_NIS_Handler(uint8_t, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x50 || FMVScript == nullptr) return 1;
    Script_Update(FMVScript);
    control_movement_object_handler(0);
    if (Script_IsPlaying(FMVScript) == 0) {
        FMVScript = nullptr;
        Player_Enable(glb_players[0], nullptr, 0);
        Camera_PopStates();
        Camera_Enable(7, 0, 1, nullptr);
        ScriptCam = 0;
    }
    return 1;
}

// [decomp] 0x00205f10 C_NIS_Handler
// A lista das cenas (0x1000003e): ao ser criada, os 70 itens do original (títulos com valor 0 e scripts 06xxxxxx);
// X ou quadrado (que ligam e desligam show_frame_count) num script, se nenhuma cena toca: a seção dele carregada
// (LoadableLoad, se é de uma seção) e, se o script está na fase, a cena tocada com o jogador parado e as câmeras
// guardadas.
int32_t C_NIS_Handler(uint8_t, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x51) {
        for (uint32_t i = 0; i < NisListCount && i < uint32_t(kNisListMax); ++i) {
            __Menu_SendMessage(c, 0x10, val(NisListSource[i].text), NisListSource[i].value);
        }
        FMVScript = nullptr;
    } else if (message == 0x4b) {
        show_frame_count = 1;
    } else if (message == 0x5d) {
        show_frame_count = 0;
    }
    if (message != 0x4b && message != 0x5d) return 1;
    if (Script_IsPlaying(FMVScript) != 0) return 1;
    const uint32_t hash = uint32_t(__Menu_SendMessage(c, 0x35, 0, 0));
    if (hash == 0) return 1;
    const uint32_t section = isLoadable(hash);
    if (section != 0) {
        SFXPauseAllStreams();
        LoadableLoad(hash, section);
        SFXUnPauseAllStreams();
    }
    if (hashtable_getitem(hash) != nullptr) {
        FMVScript = Script_Load(hash, &zero_vector, &zero_vector, nullptr, nullptr, nullptr, nullptr);
        Player_Disable(glb_players[0], 0);
        Script_Play(FMVScript, 0);
        Camera_PushStates();
        Camera_Enable(7, 0, 0, nullptr);
    }
    return 1;
}

// [decomp] 0x00215ac8 C_LBPMMAP_Handler
// A lista das fases da página de depuração (0x10000010): ao ser criada, os itens do original (na versão de venda só
// "Main Menu", 07000048) com a fase atual escolhida; mudar guarda a fase em GameState.load_file e a página de volta
// "Press START"; escolher carrega a fase (um jogador, ResetMap_LevelToLoad, o fade) e fecha o menu.
int32_t C_LBPMMAP_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) {
        const uint32_t one = 1;
        std::memcpy(MPSettings + 0x1ac, &one, 4);
        ResetMap_LevelToLoad(uint32_t(__Menu_SendMessage(c, 0x35, 0, 0)), 0, 0);
        GameFlow_PushState(7, 80.0f, 0xff);
        SFXFadeDown(1);
        MenuManager_Delete(index);
    } else if (message == 0x49) {
        GameState.menu_page = 0x40000009;
        GameState.load_file = uint32_t(__Menu_SendMessage(c, 0x35, 0, 0));
    } else if (message == 0x51) {
        for (uint32_t i = 0; i < LevelListCount && i < uint32_t(kLevelListMax); ++i) {
            __Menu_SendMessage(c, 0x10, val(LevelListSource[i].text), LevelListSource[i].value);
        }
        __Menu_SendMessage(c, 0x1e, int32_t(GameState.level_file), 0);
    }
    return 1;
}

void menu_tweaks_debug_power_on() {
    FMVScript = nullptr;
    show_frame_count = 0;
    std::memset(buf_236, 0, sizeof(buf_236));
    std::memset(buf_240, 0, sizeof(buf_240));
}
