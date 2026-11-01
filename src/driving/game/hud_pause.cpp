// A pausa do HUD das missões de carro (driving/game/hud_pause.h; docs/driving.md seção 31), portada do MIPS do
// DRIVING.ELF. As desmontagens ficam em out/agent_drv_snd/ do checkout principal (fora do git).

#include "driving/game/hud_pause.h"

#include <cstdio>
#include <cstring>

#include "driving/carp_runtime.h"
#include "driving/drv.h"
#include "driving/game/events.h"
#include "driving/game/hud.h"
#include "driving/game/hud_draw.h"
#include "driving/game/input_config.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/render/ggallery.h"
#include "driving/render/hud_gfx.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_pause.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

HudPauseEnv g_env;
HudPauseStats g_stats;
ActionQueueData g_queue;
bool g_queue_ready = false;
int32_t g_running = 1;  // DAT_0032c460
GGirlState g_girl;
std::vector<int8_t> g_colours;
std::function<uint32_t(int32_t, const std::vector<uint32_t>&)> g_sink;

constexpr uint32_t kBright = 0x7fb3dbfau, kDim = 0x7f5c64b0u, kTabDim = 0x7f0e2e40u;
constexpr int32_t kMissionFailed = 0x18577;

uint8_t* bytes(GHudData& h) { return reinterpret_cast<uint8_t*>(&h); }
const uint8_t* bytes(const GHudData& h) { return reinterpret_cast<const uint8_t*>(&h); }
int32_t i32(const GHudData& h, uint32_t off) {
    int32_t v;
    std::memcpy(&v, bytes(h) + off, 4);
    return v;
}
uint32_t u32(const GHudData& h, uint32_t off) { return uint32_t(i32(h, off)); }
float f32(const GHudData& h, uint32_t off) {
    float v;
    std::memcpy(&v, bytes(h) + off, 4);
    return v;
}
void set_i32(GHudData& h, uint32_t off, int32_t v) { std::memcpy(bytes(h) + off, &v, 4); }
void set_f32(GHudData& h, uint32_t off, float v) { std::memcpy(bytes(h) + off, &v, 4); }

GGalleryData& gal(GHudData& h) { return *nf::p32_ptr<GGalleryData>(u32(h, 8)); }
const uint8_t* key(const GHudData& h, uint32_t off) { return nf::p32_ptr<const uint8_t>(u32(h, off)); }
float kx(const GHudData& h, uint32_t off) {
    float v;
    std::memcpy(&v, key(h, off), 4);
    return v;
}
float ky(const GHudData& h, uint32_t off) {
    float v;
    std::memcpy(&v, key(h, off) + 4, 4);
    return v;
}
int32_t klabel(const GHudData& h, uint32_t off) {
    int32_t v;
    std::memcpy(&v, key(h, off) + 0x34, 4);
    return v;
}
// O +0x40 de um sprite (a largura do registro do sprite da chave).
float kwidth(const uint8_t* k) {
    float v;
    std::memcpy(&v, k + 0x40, 4);
    return v;
}

void sound(const char* name) { hud_menu_sound(8, name, "Menu Sounds", 2); }

int32_t objective_count() { return int32_t(mission_lists().objectives.size()); }

// O "%s" do sprintf num buffer de pilha de 0x20 bytes (os textos do .loc cabem; o port não corta).
std::string fmt_s(const char* s) { return std::string(s ? s : ""); }

// O "(o UberBond +0x978): 1 ou 4 ficam, senão 2" do sair.
void quit_exit_code() {
    int32_t& v = uber_bond().word(0x978);
    if (!(v == 1 || v == 4)) v = 2;
}

// Um float pelos bits (as constantes do código: 0x3faaaaaa é o 4/3 da tela larga).
float fl(uint32_t b) {
    float v;
    std::memcpy(&v, &b, 4);
    return v;
}

// A conversão de um unsigned para float do GCC (o bit 31: metade, mais o bit 0, e o dobro).
float cvt_u(uint32_t v) {
    if (int32_t(v) < 0) {
        const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
        return ee::add(f, f);
    }
    return ee::cvt_s(int32_t(v));
}

}  // namespace

HudPauseEnv& hud_pause_env() { return g_env; }
HudPauseStats& hud_pause_stats() { return g_stats; }
int32_t& hud_game_running() { return g_running; }
GGirlState& ggirl() { return g_girl; }
std::vector<int8_t>& ggirl_colour_table() { return g_colours; }
std::function<uint32_t(int32_t, const std::vector<uint32_t>&)>& ggirl_image_sink() { return g_sink; }

// ---------------------------------------------------------------- a fila

ActionQueueData& hud_queue() {
    if (!g_queue_ready) {
        ActionQueue_Construct(g_queue, 0);  // [driving] 0x00165288 ActionQueue::ActionQueue("HUDQ") do GHud::GHud
        g_queue_ready = true;
    }
    return g_queue;
}

void hud_queue_feed(int32_t action, int32_t device, float value) {
    if (!ActionQueueManager_Accepts(action)) return;
    ActionQueue_Push(hud_queue(), {action, device, value});
}

void hud_queue_reset() {
    g_queue_ready = false;
    hud_queue();
}

void GHud_RenderQueue(GHudData& h) {
    ActionQueueData& q = hud_queue();
    while (!ActionQueue_IsEmpty(q)) {
        const ActionDataPS2* a = ActionQueue_GetAction(q);
        const int32_t action = a ? a->action : 0;
        ++g_stats.actions;
        bool to_handlers = false;
        if (action < 0x21) {
            to_handlers = true;
        } else if (action < 0x24) {
            // A arma secundária trocada: a arma aparece (o DrawWeapon) por 3 s.
            if (h.result_state == 0 && h.paused == 0) {
                set_f32(h, 0x2d8, 3.0f);
                set_f32(h, 0x2e4, 0.5f);
                set_i32(h, 0x2dc, 0);
                set_i32(h, 0x2e0, 0);
            }
        } else if (action != 0x47) {
            to_handlers = true;
        } else {
            if (h.result_text != kMissionFailed) {
                if (h.paused == 0) {
                    SMissionManagerData& m = mission_manager();
                    if (m.cinematic_mode == 0) {
                        uber_bond().word(0x5d8) = 0;
                        GHud_PauseOn(h);
                    } else {
                        ++g_stats.skips;
                        SMissionManager_SkipCinematic(m);
                    }
                } else {
                    sound("SFX_Exit");
                    GHud_PauseOff(h);
                }
            }
            if (h.paused != 0 && i32(h, 0x480) == 5) {
                if (i32(h, 0x484) == 1) {
                    SMissionManager_RestoreLives(mission_manager());
                    h.restart = 1;
                    ++g_stats.restarts;
                } else if (i32(h, 0x484) == 2) {
                    sound("SFX_Exit");
                    quit_exit_code();
                    h.exit = 1;
                    ++g_stats.quits;
                }
            }
        }
        if (to_handlers) {
            if (h.paused == 0) {
                if (action == 0x4a) {
                    ++g_stats.skips;
                    SMissionManager_SkipCinematic(mission_manager());
                }
            } else {
                GHud_PauseMenuMsgHandler(h, action);
                GHud_CheatMsgHandler(h, action);
            }
        }
        ActionQueue_PopAction(q);
    }
}

// ---------------------------------------------------------------- a pausa

void GHud_PauseOn(GHudData& h) {
    if (g_env.loading_stage != 0) return;
    set_f32(h, 0x2d8, 3.0f);
    set_f32(h, 0x2b8, 1.0f);
    set_f32(h, 0x2cc, 3.0f);
    set_i32(h, 0x2c4, 0);
    if (h.result_text == kMissionFailed) {
        set_i32(h, 0x480, 5);
        set_i32(h, 0x484, 1);
    } else {
        set_i32(h, 0x480, 0);
        set_i32(h, 0x484, 0);
    }
    set_i32(h, 0x48c, 0);
    set_i32(h, 0x494, 0);
    for (uint32_t o = 0x4ac; o <= 0x4e8; o += 4) set_i32(h, o, 0);
    int32_t i = objective_count();
    for (;;) {
        const int32_t k = i;
        i = k - 1;
        if (k < 0) break;
        const SMissionObjective* o = SMissionManager_GetObjectiveByIndex(mission_manager(), i);
        if (o && o->state == 1) {
            set_i32(h, 0x488, k);
            break;
        }
    }
    h.paused = 1;
    if (g_env.icm)
        set_i32(h, 0x444, InputConfigManager_GetCurrentConfig(*g_env.icm, i32(h, 0x2a8)));
    else
        ++g_stats.no_config;
    ++g_stats.pauses;
    event_create_simple(kEventPausePort);  // Event::operator new(4) + EPause::EPause
}

void GHud_PauseOff(GHudData& h) {
    if (h.paused == 0) return;
    GGirl_KillGirl();
    set_i32(h, 0x498, 0);
    h.paused = 0;
    if (g_env.icm && g_env.input_uber && g_env.input_map) {
        InputConfigManager_SetConfigType(*g_env.icm, i32(h, 0x2a8), i32(h, 0x444), *g_env.input_uber, *g_env.input_map);
    } else if (g_env.icm) {
        // Sem a tabela da entrada (as conferências): só o tipo e a configuração do InputConfigManager.
        const int32_t type = i32(h, 0x2a8), cfg = i32(h, 0x444);
        g_env.icm->d.type = type;
        if (cfg != -1) g_env.icm->d.current[type & 1] = cfg;
        if (type == 0) uber_bond().word(0xa14) = g_env.icm->d.current[0];
        else if (type == 1) uber_bond().word(0x98c) = g_env.icm->d.current[1];
    } else {
        ++g_stats.no_config;
    }
    ++g_stats.resumes;
    event_create_simple(kEventResumePort);
}

void Simulation_PauseSimState() {
    Simulation& s = simulation();
    if (s.state != 3) {
        s.prev_state = s.state;
        s.state = 3;
    }
}

void Simulation_UnPauseSimState() {
    Simulation& s = simulation();
    if (s.state == 3) s.state = s.prev_state;
}

bool hud_pause_event_execute(const PendingEvent& e) {
    if (e.type == kEventPausePort) {
        if (simulation().state != 3) {
            Simulation_PauseSimState();
            SMissionManager_Stop(mission_manager());
            g_running = 0;
        }
        ASoundManager_Pause();
        return true;
    }
    if (e.type == kEventResumePort) {
        if (simulation().state == 3) {
            Simulation_UnPauseSimState();
            if (mission_manager().state == 2) SMissionManager_Start(mission_manager());  // (e o PAD_isstable)
            ASoundManager_Resume();
            g_running = 1;
            if (g_env.camera_pause_off)
                g_env.camera_pause_off();
            else
                ++g_stats.no_camera;
            if (g_env.car_resume)
                g_env.car_resume();
            else
                ++g_stats.no_car;
        }
        return true;
    }
    return false;
}

bool GHud_CanToggleInvert(const GHudData& h) { return i32(h, 0x2a8) != 0; }

void GHud_SetControllerUnplugged(GHudData& h, int32_t unplugged) {
    if (i32(h, 0x2a4) == unplugged) return;
    if (unplugged == 0) {
        h.pause_errors = 0;
    } else {
        h.pause_errors = 1;
        if (h.paused == 0) {
            GHud_PauseOn(h);
            set_i32(h, 0x2a4, unplugged);
            return;
        }
    }
    set_i32(h, 0x2a4, unplugged);
}

// ---------------------------------------------------------------- as mensagens do menu

namespace {

// O 0x41/0x42 na página dos controles: a configuração anterior ou a seguinte (com mais de uma).
void step_config(GHudData& h, int32_t dir) {
    const int32_t n = g_env.icm ? InputConfigManager_GetNumConfigs(*g_env.icm, i32(h, 0x2a8)) : 0;
    if (!g_env.icm) ++g_stats.no_config;
    int32_t c = i32(h, 0x444);
    if (uint32_t(n) > 1u) {
        set_i32(h, 0x444, c + dir);
        sound("SFX_Toggle");
        c = i32(h, 0x444);
    }
    if (dir < 0) {
        if (c < 0) set_i32(h, 0x444, n - 1);
    } else {
        if (n <= c) set_i32(h, 0x444, 0);
    }
}

void restart_or_chain(GHudData& h) {
    const char* name = nullptr;  // AttributeSet::LookupString(o mundo, "CHAIN_RESTART_MISSION_NAME"): Paris não tem
    if (name) {
        ++g_stats.exits_to_driving;
        quit_exit_code();
        h.exit = 1;
        return;
    }
    SMissionManager_RestoreLives(mission_manager());
    h.restart = 1;
    ++g_stats.restarts;
}

}  // namespace

void GHud_PauseMenuMsgHandler(GHudData& h, int32_t action) {
    const int32_t page = i32(h, 0x480);
    switch (action) {
        case 0x41:  // FRONTENDACTION_UP
        case 0x42: {  // FRONTENDACTION_DOWN
            const bool up = action == 0x41;
            if (page != 0) {
                if (page == 2) {
                    step_config(h, up ? -1 : 1);
                    return;
                }
                if (page == 1) {
                    if (i32(h, 0x498) == 1) return;
                    const int32_t n = objective_count();
                    if (uint32_t(n) <= 1u) return;
                    set_i32(h, 0x488, i32(h, 0x488) + (up ? -1 : 1));
                    sound("SFX_Toggle");
                    if (up) {
                        if (i32(h, 0x488) == 0) set_i32(h, 0x488, objective_count());
                    } else {
                        if (uint32_t(objective_count()) < u32(h, 0x488)) set_i32(h, 0x488, 0);
                    }
                    return;
                }
                if (page != 5) return;
                const int32_t c = i32(h, 0x494);
                if (c == 0) {
                    set_i32(h, 0x484, i32(h, 0x484) + (up ? -1 : 1));
                    sound("SFX_Toggle");
                    if (up) {
                        if (h.result_text != kMissionFailed) return;
                        if (i32(h, 0x484) < 1) set_i32(h, 0x484, 2);
                    } else {
                        if (2 < i32(h, 0x484)) set_i32(h, 0x484, 1);
                    }
                    return;
                }
                if (up) {
                    if (c != 2) return;
                    set_i32(h, 0x494, 1);
                } else {
                    if (c != 1) return;
                    set_i32(h, 0x494, 2);
                }
                break;
            }
            const int32_t c = i32(h, 0x494);
            if (c != 0) {
                if (up) {
                    if (c != 2) return;
                    set_i32(h, 0x494, 1);
                } else {
                    if (c != 1) return;
                    set_i32(h, 0x494, 2);
                }
                break;
            }
            set_i32(h, 0x484, i32(h, 0x484) + (up ? -1 : 1));
            sound("SFX_Toggle");
            if (up) {
                const int32_t v = i32(h, 0x484);
                if (h.hud_off == 0 || h.result_text != kMissionFailed) {
                    if (-1 < v) return;
                } else {
                    if (0 < v) return;
                }
                set_i32(h, 0x484, 2);
            } else {
                if (2 < i32(h, 0x484)) set_i32(h, 0x484, 0);
            }
            return;
        }
        case 0x43:  // FRONTENDACTION_LEFT
        case 0x44: {  // FRONTENDACTION_RIGHT
            if (h.hud_off != 0) return;
            if (h.result_text == kMissionFailed) return;
            if (page == 1 && i32(h, 0x498) == 1) return;
            if (i32(h, 0x494) != 0) return;
            if (action == 0x43) {
                set_i32(h, 0x480, page - 1);
                if (page - 1 < 0) set_i32(h, 0x480, 3);
            } else {
                set_i32(h, 0x480, page + 1);
                if (3 < page + 1) set_i32(h, 0x480, 0);
            }
            break;
        }
        case 0x45: {  // FRONTENDACTION_ACCEPT
            int32_t pg = page;
            if (page == 0) {
                const int32_t item = i32(h, 0x484);
                if (item == 0) {
                    sound("SFX_Exit");
                    GHud_PauseOff(h);
                    pg = i32(h, 0x480);
                } else if (item == 2) {
                    const int32_t c = i32(h, 0x494);
                    if (c == 1) {
                        sound("SFX_Exit");
                        quit_exit_code();
                        h.exit = 1;
                        ++g_stats.quits;
                        pg = i32(h, 0x480);
                        goto page5;
                    }
                    if (c == 2) {
                        set_i32(h, 0x494, 0);
                    } else {
                        if (c != 0) {
                            pg = i32(h, 0x480);
                            goto page5;
                        }
                        set_i32(h, 0x494, 2);
                    }
                    sound("SFX_Toggle");
                    pg = i32(h, 0x480);
                } else if (item == 1) {
                    const int32_t c = i32(h, 0x494);
                    if (c == 1) {
                        restart_or_chain(h);
                        pg = i32(h, 0x480);
                        goto page5;
                    }
                    if (c == 2) {
                        set_i32(h, 0x494, 0);
                    } else {
                        if (c != 0) {
                            pg = i32(h, 0x480);
                            goto page5;
                        }
                        set_i32(h, 0x494, 2);
                    }
                    sound("SFX_Toggle");
                    pg = i32(h, 0x480);
                } else {
                    pg = i32(h, 0x480);
                }
            } else if (page == 1) {
                if (objective_count() != 0) {
                    set_i32(h, 0x498, i32(h, 0x498) == 0 ? 1 : 0);
                    sound("SFX_Toggle");
                }
                pg = i32(h, 0x480);
            }
        page5:
            if (pg != 5) return;
            // (AttributeSet::LookupInt e LookupString do mundo: lidos e não usados)
            {
                const int32_t item = i32(h, 0x484);
                if (item == 1) {
                    SMissionManager_RestoreLives(mission_manager());
                    h.restart = 1;
                    ++g_stats.restarts;
                    return;
                }
                if (item == 2) {
                    sound("SFX_Exit");
                    quit_exit_code();
                    h.exit = 1;
                    ++g_stats.quits;
                    return;
                }
                if (item != 0) return;
                sound("SFX_Exit");
                SMissionManager_RestoreLives(mission_manager());
                h.restart = 1;
                ++g_stats.restarts;
            }
            return;
        }
        case 0x46:  // FRONTENDACTION_CANCEL
            if (page != 0) return;
            if (i32(h, 0x494) == 0) return;
            set_i32(h, 0x494, 0);
            break;
        case 0x49:  // FRONTENDACTION_INVERT
            if (page != 2) return;
            if (!GHud_CanToggleInvert(h)) return;
            if (g_env.icm) {
                InputConfigManager_SetInverted(*g_env.icm, !InputConfigManager_IsInverted(*g_env.icm));
            } else {
                ++g_stats.no_config;
            }
            break;
        default:
            return;
    }
    sound("SFX_Toggle");
}

// ---------------------------------------------------------------- as trapaças

namespace {

// As sequências do .data (0x0033ae58...): a contagem e os botões (0x59 esquerda, 0x5b cima, 0x5d direita, 0x61 o
// gatilho esquerdo, 0x62 soltá-lo; os pares de aperto e soltura).
const CheatSequence kCheats[] = {
    {0x33ae58, {14, 0x61, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x62}},
    {0x33aea0, {10, 0x61, 0x59, 0x5a, 0x5d, 0x5e, 0x5d, 0x5e, 0x59, 0x5a, 0x62}},
    {0x33afc0, {12, 0x61, 0x5d, 0x5e, 0x5d, 0x5e, 0x59, 0x5a, 0x59, 0x5a, 0x5b, 0x5c, 0x62}},
    {0x33b008, {12, 0x61, 0x59, 0x5a, 0x5d, 0x5e, 0x5b, 0x5c, 0x59, 0x5a, 0x5b, 0x5c, 0x62}},
    {0x33b050, {12, 0x61, 0x5d, 0x5e, 0x5b, 0x5c, 0x59, 0x5a, 0x5d, 0x5e, 0x5d, 0x5e, 0x62}},
    {0x33b098, {14, 0x61, 0x5d, 0x5e, 0x5b, 0x5c, 0x59, 0x5a, 0x5d, 0x5e, 0x5d, 0x5e, 0x5d, 0x5e, 0x62}},
    {0x33b0e0, {16, 0x61, 0x5d, 0x5e, 0x5b, 0x5c, 0x59, 0x5a, 0x5d, 0x5e, 0x5d, 0x5e, 0x5d, 0x5e, 0x5d, 0x5e, 0x62}},
    {0x33b128, {14, 0x61, 0x59, 0x5a, 0x5b, 0x5c, 0x5b, 0x5c, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x62}},
    {0x33b170, {10, 0x61, 0x5d, 0x5e, 0x5d, 0x5e, 0x5d, 0x5e, 0x5d, 0x5e, 0x62}},
};

const int32_t* cheat(uint32_t address) {
    for (const CheatSequence& c : kCheats)
        if (c.address == address) return c.words;
    return nullptr;
}

// O 0x00206890 (sem nome; marcado em mission.cpp): o escudo do jogador e o máximo = o máximo das configurações x n.
void set_shield_multiple(SMissionManagerData& m, int32_t n) {
    const MissionSettings* s = nf::p32_ptr<const MissionSettings>(m.settings);
    if (!s) return;
    const float v = ee::cvt_s(s->shield_max * n);
    m.player_shield = v;
    m.player_shield_max = v;
}

}  // namespace

const CheatSequence* hud_cheat_sequences(int32_t* count) {
    if (count) *count = int32_t(sizeof kCheats / sizeof kCheats[0]);
    return kCheats;
}

bool GHud_CheckCheatInput(GHudData& h, const int32_t* seq) {
    const int32_t n = seq[0];
    if (0 < n) {
        uint32_t at = uint32_t(0x4ac + (0x10 - n) * 4);
        for (int32_t i = 0; i < n; ++i, at += 4)
            if (i32(h, at) != seq[1 + i]) return false;
    }
    sound("SFX_Toggle");
    ++g_stats.cheats;
    return true;
}

void GHud_CheatMsgHandler(GHudData& h, int32_t action) {
    if (!(uint32_t(action - 0x51) < 0x14u)) return;
    for (uint32_t o = 0x4ac; o < 0x4e8; o += 4) set_i32(h, o, i32(h, o + 4));
    set_i32(h, 0x4e8, action);
    UberBondBlock& u = uber_bond();
    int32_t& f4 = u.word(0x9a4);
    int32_t& f0 = u.word(0x9a0);
    if (GHud_CheckCheatInput(h, cheat(0x33ae58))) f4 = (f4 & 1) == 0 ? (f4 | 1) : (f4 & ~1);
    if (GHud_CheckCheatInput(h, cheat(0x33aea0))) {
        if ((f4 & 2) == 0) {
            f4 |= 2;
        } else {
            f4 &= ~2;
            if (g_env.time_scale) *g_env.time_scale = 1.0f;  // Scheduler::fgScheduler +0x24
        }
    }
    if (std::strcmp(u.text(0x4f4), "snow2a_mis4") == 0) {
        if (GHud_CheckCheatInput(h, cheat(0x33afc0))) {
            f0 = int32_t((uint32_t(f0) & 0xfffffeffu) | 0x80u);
            ++g_stats.exits_to_driving;
            if (g_env.exit_to_driving) g_env.exit_to_driving(8, "snow2a_race");
            u.word(0x978) = 2;
            h.exit = 1;
        }
        if (GHud_CheckCheatInput(h, cheat(0x33b008))) {
            f0 = int32_t(uint32_t(f0) | 0x100u);
            ++g_stats.exits_to_driving;
            if (g_env.exit_to_driving) g_env.exit_to_driving(u.word(0x974), u.text(0x4f4));
            u.word(0x978) = 2;
            h.exit = 1;
        }
    }
    if (GHud_CheckCheatInput(h, cheat(0x33b050))) set_shield_multiple(mission_manager(), 2);
    if (GHud_CheckCheatInput(h, cheat(0x33b098))) set_shield_multiple(mission_manager(), 3);
    if (GHud_CheckCheatInput(h, cheat(0x33b0e0))) set_shield_multiple(mission_manager(), 4);
    if (GHud_CheckCheatInput(h, cheat(0x33b170))) f0 = (f0 & 0x1000) == 0 ? (f0 | 0x1000) : (f0 & ~0x1000);
    if (GHud_CheckCheatInput(h, cheat(0x33b128))) {
        if ((f0 & 0x2000) == 0) {
            f0 |= 0x3006;
            f4 |= 3;
        } else {
            f0 = int32_t(uint32_t(f0) & 0xffffcff9u);
            f4 = int32_t(uint32_t(f4) & 0xfffffffcu);
        }
    }
}

// ---------------------------------------------------------------- o desenho

void GHud_DrawBlinkingArrow(GHudData& h, float x, float y) {
    const float now = ee::cvt_s(TIMER_gettick());
    const int32_t state = i32(h, 0x2c4);
    if (state == 0) {
        set_i32(h, 0x2c4, 1);
        const float t = ee::cvt_s(TIMER_gettick());
        set_i32(h, 0x2c0, 0);
        set_f32(h, 0x2bc, t);
    } else {
        const float last = f32(h, 0x2bc);
        const float rate = ee::cvt_s(simulation().frame_rate);
        set_f32(h, 0x2bc, now);
        const float acc = ee::add(f32(h, 0x2c0), ee::mul(ee::sub(now, last), rate));
        set_f32(h, 0x2c0, acc);
        if (ee::cvt_w(acc) > 0) {
            set_i32(h, 0x2c0, 0);
            float a = f32(h, 0x2b8);
            if (state == 1) {
                a = ee::sub(a, 0.016666668f);
                set_f32(h, 0x2b8, a);
            } else if (state == 2) {
                a = ee::add(a, 0.016666668f);
                set_f32(h, 0x2b8, a);
            }
            a = f32(h, 0x2b8);
            if (ee::le(1.0f, a)) {
                set_f32(h, 0x2b8, 1.0f);
                set_i32(h, 0x2c4, 1);
            } else if (ee::le(a, 0.0f)) {
                set_f32(h, 0x2b8, 0.0f);
                set_i32(h, 0x2c4, 2);
            }
        }
    }
    const float v = ee::mul(f32(h, 0x2b8), 127.0f);
    const float half = ee::lt(v, 0.0f) ? -0.5f : 0.5f;
    const uint32_t colour = uint32_t(ee::cvt_w(ee::add(half, v))) << 24 | 0xffffffu;
    GGalleryData& g = gal(h);
    GGallery_SPRITE_SetState(g, key(h, 0x120));
    g.colour = colour;
    g.scale[2] = 1.0f;
    g.scale[1] = 1.2f;
    g.scale[0] = 1.2f;
    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x), ee::cvt_w(y), key(h, 0x120), nullptr);
}

void GHud_DrawFontLines(GHudData& h, float x, float y, float scale, uint32_t k, uint32_t colour,
                        const std::vector<std::string>& lines, int32_t n) {
    GGalleryData& g = gal(h);
    GGallery_FONT_SetState(g, nf::p32_ptr<const uint8_t>(k));
    GGallery_FONT_SetScale(g, scale, scale);
    g.colour = colour;
    const float fh = GGallery_FONT_Height(g, "T");
    for (int32_t i = 0; i < n; ++i) {
        const float next = ee::add(ee::add(y, fh), 5.0f);
        GGallery_FONT_Draw(g, x, y, size_t(i) < lines.size() ? lines[size_t(i)].c_str() : "");
        y = next;
    }
}

void GHud_DrawFontCentered(GHudData& h, float x0, float y0, float x1, float y1, float scale, uint32_t k,
                           uint32_t colour, uint32_t n, const std::vector<std::string>& lines) {
    GGalleryData& g = gal(h);
    GGallery_FONT_SetState(g, nf::p32_ptr<const uint8_t>(k));
    GGallery_FONT_SetScale(g, scale, scale);
    g.colour = colour;
    const float fh = GGallery_FONT_Height(g, "T");
    const float count = cvt_u(n);
    if (n == 0) return;
    float y = ee::add(y0, ee::mul(ee::sub(ee::sub(y1, y0), ee::mul(ee::add(fh, 5.0f), count)), 0.5f));
    for (uint32_t i = 0; i < n; ++i) {
        const char* s = i < lines.size() ? lines[i].c_str() : "";
        const float w = GGallery_FONT_Width(g, s);
        const float next = ee::add(ee::add(y, fh), 5.0f);
        GGallery_FONT_Draw(g, ee::add(x0, ee::mul(ee::sub(ee::sub(x1, x0), w), 0.5f)), y, s);
        y = next;
    }
}

int32_t WordWrap(const char* s, uint32_t max, std::vector<std::string>& out) {
    out.assign(5, std::string());
    uint32_t n = 0;
    while (std::strlen(s) > max && n <= 3) {
        const int32_t m = int32_t(max);
        int32_t k = m - 1;
        if (k >= 0 && s[m - 1] != ' ') {
            k = m - 2;
            while (k >= 0 && s[k] != ' ') --k;
        }
        char c;
        if (k == -1) {
            out[n].assign(s, max);
            c = s[m];
            s += m;
        } else {
            out[n].assign(s, size_t(k));
            c = s[k];
            s += k;
        }
        while (c == ' ') {
            c = s[1];
            ++s;
        }
        ++n;
    }
    size_t len = std::strlen(s);
    if (len > max) len = max;
    out[n].assign(s, len);
    return int32_t(n + 1);
}

void GHud_DrawPauseErrors(GHudData& h) {
    GGalleryData& g = gal(h);
    const uint32_t quad = GLocale_GetLanguage() == 1 ? 0xec : 0xe8;
    GGallery_QUAD_SetState(g, key(h, quad));
    GGallery_QUAD_Draw(g, 1.0f, key(h, quad) + 0x30);
    GGallery_FONT_SetState(g, key(h, 0xd8));
    GGallery_FONT_SetScale(g, 1.0f, 1.0f);
    const int32_t vm = hud_draw_env().video_mode;
    auto centred = [&](uint32_t k, const char* s) {
        const float w = GGallery_FONT_Width(g, s);
        GGallery_FONT_Draw_Colour(g, ee::sub(kx(h, k), ee::mul(w, 0.5f)), ky(h, k), s, g.colour);
    };
    if ((vm == 2 || vm == 3) && GLocale_GetLanguage() == 0)
        centred(0xd8, LOCALE_Symbol(0x138d));
    else
        centred(0xd8, LOCALE_Symbol(klabel(h, 0xd8)));
    GGallery_FONT_SetState(g, key(h, 0xdc));
    GGallery_FONT_SetScale(g, 1.0f, 1.0f);
    centred(0xdc, LOCALE_Symbol(klabel(h, 0xdc)));
    const uint32_t k = g_env.multitap ? 0xe4 : 0xe0;
    GGallery_FONT_SetState(g, key(h, k));
    GGallery_FONT_SetScale(g, 1.0f, 1.0f);
    centred(k, LOCALE_Symbol(klabel(h, k)));
    if (GLocale_GetLanguage() == 6) {
        GGallery_FONT_SetState(g, key(h, 0xe0));
        GGallery_FONT_SetScale(g, 1.0f, 1.0f);
        const char* s = LOCALE_Symbol(0x1392);
        const float w = GGallery_FONT_Width(g, s);
        const float y = ky(h, 0xe0);
        GGallery_FONT_Draw_Colour(g, ee::sub(kx(h, 0xe0), ee::mul(w, 0.5f)), ee::add(y, ee::sub(y, ky(h, 0xdc))), s,
                                  g.colour);
    }
}

void GHud_DrawPauseMenu(GHudData& h) {
    GGalleryData& g = gal(h);
    uint32_t c_continue = kDim, c_restart = kDim, c_quit = kDim, c_yes = kDim, c_no = kDim;  // s5, s2, s4, s6, s7
    float f25 = ky(h, 0x10c);
    float f27 = ky(h, 0x110);
    float f24 = h.screen_cx;
    float f23 = h.screen_cy;
    // (strcmp(UberBond +0x4f4, "paris_mis01"): o resultado não é usado)
    bool show_scroll_yes_no = false;  // sp+0xb9c (o "scroll": as setas de sim/não)
    bool show_back = false;           // sp+0xba0
    bool show_select = false;         // sp+0xba4
    bool show_continue = false;       // sp+0xba8
    bool show_changepage = false;     // sp+0xbac
    bool show_invert = false;         // sp+0xbb0
    bool show_hint = false;           // sp+0xbb4
    for (uint32_t o = 0xf4; o <= 0xfc; o += 4) {
        GGallery_SPRITE_SetState(g, key(h, o));
        GGallery_SPRITE_Draw2D(g, key(h, o));
    }
    GGirl_DoGirl(h);
    std::string buf;  // o sprintf em sp
    switch (i32(h, 0x480)) {
        case 0: {
            const float f20 = -4.0f;
            f25 = ee::add(f25, f20);
            show_select = true;
            show_continue = true;
            if (h.hud_off != 0) {
                const float s11 = 1.2f;
                const char* t = LOCALE_Symbol(0x134);
                show_changepage = false;
                const float cx = GHud_GetCenter(h, s11, u32(h, 0x1f0), t);
                GHud_DrawFont(h, cx, ee::add(f25, 4.0f), s11, u32(h, 0x1f0), kBright, LOCALE_Symbol(0x134));
                buf = fmt_s(LOCALE_Symbol(0x2201));
            } else {
                buf = fmt_s(LOCALE_Symbol(klabel(h, 0x108)));
            }
            if (i32(h, 0x494) == 0) {
                show_changepage = true;
                const float y0 = ky(h, 0x100), y1 = ky(h, 0x104), y2 = ky(h, 0x108);
                const int32_t item = i32(h, 0x484);
                if (item == 0) {
                    f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x100), nullptr);
                    c_continue = kBright;
                    f23 = y0;
                } else if (item == 1) {
                    f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x104), nullptr);
                    c_restart = kBright;
                    f23 = y1;
                } else if (item == 2) {
                    f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x108), buf.c_str());
                    c_quit = kBright;
                    f23 = y2;
                }
                GHud_DrawBlinkingArrow(h, ee::sub(f24, 30.0f), ee::add(f23, 1.0f));
                if (h.hud_off == 0 || h.result_text != kMissionFailed) {
                    show_back = true;
                    const float cx = GHud_GetCenter(h, 1.0f, u32(h, 0x100), nullptr);
                    GHud_DrawFont(h, cx, y0, 1.0f, u32(h, 0x100), c_continue, nullptr);
                }
                const float cx1 = GHud_GetCenter(h, 1.0f, u32(h, 0x104), nullptr);
                GHud_DrawFont(h, cx1, y1, 1.0f, u32(h, 0x104), c_restart, nullptr);
                const float cx2 = GHud_GetCenter(h, 1.0f, u32(h, 0x108), buf.c_str());
                GHud_DrawFont(h, cx2, y2, 1.0f, u32(h, 0x108), c_quit, buf.c_str());
            } else {
                const float fy = ky(h, 0x1fc);
                const float fn = ky(h, 0x200);
                const int32_t item = i32(h, 0x484);
                if (item == 1 || item == 2) {
                    const uint32_t q = item == 1 ? 0x1f4 : 0x1f8;
                    const float cx = GHud_GetCenter(h, 1.2f, u32(h, q), nullptr);
                    const float y = ky(h, q);
                    GGallery_SPRITE_SetState(g, key(h, q));
                    GHud_DrawFont(h, cx, y, 1.2f, u32(h, q), kBright, nullptr);
                }
                const int32_t c = i32(h, 0x494);
                if (c == 1) {
                    show_scroll_yes_no = true;
                    f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x1fc), nullptr);
                    c_yes = kBright;
                    f23 = fy;
                } else if (c == 2) {
                    show_scroll_yes_no = true;
                    f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x200), nullptr);
                    c_no = kBright;
                    f23 = fn;
                }
                show_back = false;
                GHud_DrawBlinkingArrow(h, ee::sub(f24, 30.0f), ee::add(f23, 1.0f));
                GGallery_SPRITE_SetState(g, key(h, 0x1fc));
                GHud_DrawFont(h, GHud_GetCenter(h, 1.0f, u32(h, 0x1fc), nullptr), fy, 1.0f, u32(h, 0x1fc), c_yes,
                              nullptr);
                GGallery_SPRITE_SetState(g, key(h, 0x200));
                GHud_DrawFont(h, GHud_GetCenter(h, 1.0f, u32(h, 0x200), nullptr), fn, 1.0f, u32(h, 0x200), c_no,
                              nullptr);
            }
            break;
        }
        case 1: {
            if (h.hud_off != 0) break;
            f27 = ee::add(f27, -4.0f);
            show_back = true;
            const float f22 = 100.0f, line = 25.0f;
            float y = ee::add(f27, 39.0f);
            if (i32(h, 0x488) == 0 && objective_count() != 0) set_i32(h, 0x488, 1);
            const int32_t detail = i32(h, 0x498);
            if (detail == 0) {
                show_continue = true;
                show_hint = true;
                show_changepage = true;
                const uint32_t n = uint32_t(objective_count());
                std::vector<int32_t> counts(n);
                for (uint32_t i = 0; i < n; ++i) {
                    const SMissionObjective* o = SMissionManager_GetObjectiveByIndex(mission_manager(), int32_t(i));
                    if (!o) continue;
                    uint32_t colour, k;
                    float sx, sy;
                    if (o->state == 2) {
                        colour = kDim;
                        k = 0x1b4;
                        sx = 100.0f;
                        sy = ee::add(y, 4.0f);
                    } else {
                        sy = ee::add(y, 9.0f);
                        if (o->state == 1) {
                            k = 0x1b8;
                            sx = 100.0f;
                            colour = kBright;
                        } else {
                            k = 0x1b8;
                            sx = ee::add(f22, 3.0f);
                            colour = kDim;
                        }
                    }
                    GGallery_SPRITE_SetState(g, key(h, k));
                    g.colour = colour;
                    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(sx), ee::cvt_w(sy), key(h, k), nullptr);
                    std::vector<std::string> lines;
                    const int32_t lc = WordWrap(LOCALE_Symbol(o->id), 0x3c, lines);
                    counts[i] = lc;
                    GHud_DrawFontLines(h, ee::add(f22, 20.0f), ee::sub(y, 0.0f), 0.9f, u32(h, 0x1f0), colour, lines, lc);
                    y = ee::add(y, ee::mul(line, ee::cvt_s(lc)));
                }
                if (i32(h, 0x488) != 0) {
                    if (!SMissionManager_GetObjectiveByIndex(mission_manager(), i32(h, 0x488) - 1)) goto tail;
                    int32_t sum = 0;
                    for (uint32_t i = 0; i + 1 < u32(h, 0x488); ++i) sum += i < counts.size() ? counts[i] : 0;
                    GHud_DrawBlinkingArrow(h, ee::sub(f22, 30.0f),
                                           ee::add(ee::add(f27, 40.0f), ee::mul(line, ee::cvt_s(sum))));
                }
            } else if (detail == 1) {
                const float x0 = 70.0f, y0 = 135.0f;
                GGallery_QUAD_Draw2D(g, x0, y0, 505.0f, 175.0f, 0x3f000000u);
                GHud_DrawFont(h, GHud_GetCenter(h, 1.0f, u32(h, 0x188), nullptr), ky(h, 0x188), 1.0f, u32(h, 0x188),
                              kBright, nullptr);
                if (const SMissionObjective* o =
                        SMissionManager_GetObjectiveByIndex(mission_manager(), i32(h, 0x488) - 1)) {
                    std::vector<std::string> lines;
                    const int32_t lc = WordWrap(LOCALE_Symbol(o->text), 0x32, lines);
                    GHud_DrawFontCentered(h, x0, y0, 575.0f, 310.0f, 1.0f, u32(h, 0x18c), kBright, uint32_t(lc), lines);
                }
                // A dica do botão: o texto antes e depois do símbolo e o ícone dele entre os dois.
                char before[0x100] = {}, after[0x100] = {};  // (o PS2: 0x20 cada, na pilha; os textos do jogo cabem)
                const char* hint = LOCALE_Symbol(klabel(h, 0x190));
                const float cx = GHud_GetCenter(h, 1.0f, u32(h, 0x190), hint);
                const int32_t b = GHud_GetButton(LOCALE_Symbol(klabel(h, 0x190)), before, after);
                uint32_t icon = 0;
                if (b == 1) icon = u32(h, 0x1e4);
                else if (b == 0) icon = u32(h, 0xd0);
                else if (b == 2) icon = u32(h, 0xc4);
                else if (b == 3) icon = u32(h, 0x1e8);
                const float hy = 280.0f;
                GHud_DrawFont(h, cx, hy, 1.0f, u32(h, 0xc0), kBright, before);
                float w = GGallery_FONT_Width(g, before);
                if (hud_renderer().wide != 0) w = ee::mul(w, fl(0x3faaaaaau));
                float x = ee::add(cx, w);
                x = ee::add(x, GGallery_FONT_Width(g, "s"));
                if (icon == 0) {
                    ++g_stats.null_icon;  // o PS2 desenharia o sprite da chave nula
                    break;
                }
                const uint8_t* ik = nf::p32_ptr<const uint8_t>(icon);
                GGallery_SPRITE_SetState(g, ik);
                g.colour = 0x7fffffffu;
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::add(x, 5.0f)), 0x11b, ik, nullptr);
                x = ee::add(x, kwidth(ik));
                GHud_DrawFont(h, ee::add(x, GGallery_FONT_Width(g, "s")), hy, 1.0f, u32(h, 0xc0), kBright, after);
            }
            break;
        }
        case 2: {
            if (h.hud_off != 0) break;
            show_back = true;
            show_changepage = true;
            for (uint32_t o : {0x12cu, 0x130u, 0x134u}) {
                GGallery_SPRITE_SetState(g, key(h, o));
                GGallery_SPRITE_Draw2D(g, key(h, o));
            }
            const int32_t type = i32(h, 0x2a8);
            const int32_t cfg = i32(h, 0x444);
            const InputConfigManager* icm = g_env.icm;
            if (!icm) ++g_stats.no_config;
            if (icm && uint32_t(InputConfigManager_GetNumConfigs(*icm, type)) > 1u) {
                show_continue = true;
                for (uint32_t o : {0x124u, 0x128u}) {
                    GGallery_BITMAP_SetState(g, key(h, o));
                    GGallery_BITMAP_Draw2D(g, key(h, o));
                }
            }
            auto locale_id = [&](int32_t index) {
                return icm ? InputConfigManager_GetLocaleID(*icm, type, cfg, index) : 0;
            };
            GHud_DrawFont(h, kx(h, 0x138), ky(h, 0x138), 1.0f, u32(h, 0x138), kBright, LOCALE_Symbol(locale_id(0xd)));
            GHud_DrawFont(h, kx(h, 0x13c), ky(h, 0x13c), 0.9f, u32(h, 0x13c), kBright, LOCALE_Symbol(locale_id(0xc)));
            if (GHud_CanToggleInvert(h)) {
                show_invert = true;
                const bool inv = icm && InputConfigManager_IsInverted(*icm);
                if (!icm) ++g_stats.no_config;
                const std::string t = fmt_s(LOCALE_Symbol(inv ? 0x14d : 0x1774));
                const uint32_t colour = inv ? kBright : kDim;
                GHud_DrawFont(h, ee::sub(kx(h, 0x144), 15.0f), ky(h, 0x144), 1.1f, u32(h, 0x144), colour, t.c_str());
                const float w = GGallery_FONT_Width(g, LOCALE_Symbol(klabel(h, 0x140)));
                GHud_DrawFont(h, ee::sub(ee::sub(kx(h, 0x144), w), 50.0f), ky(h, 0x140), 1.1f, u32(h, 0x140), kBright,
                              nullptr);
            }
            for (uint32_t o = 0x158; o <= 0x184; o += 4) {
                GGallery_SPRITE_SetState(g, key(h, o));
                GGallery_SPRITE_Draw2D(g, key(h, o));
            }
            for (int32_t col = 0; col < 2; ++col) {
                const uint32_t k = col == 0 ? 0x158 : 0x170;
                const float x = ee::add(ee::add(kx(h, k), 20.0f), 3.0f);
                float y = ee::sub(ky(h, k), 5.0f);
                for (int32_t i = col * 6; i < col * 6 + 6; ++i) {
                    GHud_DrawFont(h, x, y, 0.9f, u32(h, 0x1f0), kBright, LOCALE_Symbol(locale_id(i)));
                    y = ee::add(y, 21.0f);
                }
            }
            break;
        }
        case 3: {
            if (h.hud_off != 0) break;
            UberBondBlock copy = uber_bond();
            std::memset(copy.bytes + 0xa4c, 0, 4);  // (o PS2 copia 0xa4c bytes para a pilha)
            SMissionManagerData& m = mission_manager();
            if (simulation().state != 2) {
                if (!g_env.accumulate_scores) SMissionManager_ClearScoreBuffer(copy);
                SMissionManager_TallyScores(m, copy, false);
                if (m.state != 3 && m.end_step == 0) {
                    const int32_t rate = simulation().frame_rate;
                    if (rate != 0) copy.word(0x9fc) += simulation().steps / rate;
                }
            }
            const float line = 18.0f;
            float y = 130.0f;
            GHud_DrawFont(h, 85.0f, y, 1.0f, u32(h, 0x1f0), kBright, LOCALE_Symbol(0x220e));
            show_back = true;
            show_changepage = true;
            const char* t1 = LOCALE_Symbol(0x220f);
            GHud_DrawFont(h, ee::sub(445.0f, GGallery_FONT_Width(g, t1)), y, 1.0f, u32(h, 0x1f0), kBright, t1);
            const char* t2 = LOCALE_Symbol(0x2210);
            GHud_DrawFont(h, ee::sub(560.0f, GGallery_FONT_Width(g, t2)), y, 1.0f, u32(h, 0x1f0), kBright, t2);
            y = 160.0f;
            float lx = 85.0f;
            const int32_t diff = uber_bond().word(0xa3c);
            for (int32_t s = 0; s < 10; ++s) {
                const int32_t v = s - 1;
                if (v == 1) lx = 125.0f;
                if (v == 5) lx = 85.0f;
                if (uint32_t(s - 3) < 3u) continue;
                if (v == 8 && diff < 3) continue;
                GHud_DrawFont(h, lx, y, 1.0f, u32(h, 0x1f0), kBright, LOCALE_Symbol(s + 0x6b4));
                y = ee::add(y, line);
            }
            y = 160.0f;
            char a[0x20], b[0x20];
            for (int32_t s = 0; s < 9; ++s) {
                if (s == 1) y = ee::add(y, line);
                if (uint32_t(s - 2) < 3u) continue;
                if (s == 8 && diff < 3) continue;
                if (s == 7) {
                    const uint32_t t = uint32_t(copy.word(0x9fc)), u = uint32_t(copy.word(0xa00));
                    std::snprintf(a, sizeof a, "%u:%02u", t / 60u, t % 60u);
                    std::snprintf(b, sizeof b, "%u:%02u", u / 60u, u % 60u);
                } else if (s == 6) {
                    const float hp = SMissionManager_GetPlayerHealth(m, true);
                    std::snprintf(a, sizeof a, "%d", ee::cvt_w(ee::mul(hp, 100.0f)));
                    std::snprintf(b, sizeof b, "%d", copy.word(0x9f4));
                } else {
                    std::snprintf(a, sizeof a, "%d", copy.word(0x9a8 + uint32_t(s) * 12));
                    std::snprintf(b, sizeof b, "%d", copy.word(0x9ac + uint32_t(s) * 12));
                }
                GHud_DrawFont(h, ee::sub(445.0f, GGallery_FONT_Width(g, a)), y, 1.0f, u32(h, 0x1f0), kBright, a);
                const float w = GGallery_FONT_Width(g, b);
                const float next = ee::add(y, line);
                GHud_DrawFont(h, ee::sub(560.0f, w), y, 1.0f, u32(h, 0x1f0), kBright, b);
                y = next;
            }
            break;
        }
        case 5: {
            f25 = ee::add(f25, -4.0f);
            const float s11 = 1.2f;
            show_select = true;
            const float cx = GHud_GetCenter(h, s11, u32(h, 0x1f0), LOCALE_Symbol(kMissionFailed));
            show_continue = true;
            GHud_DrawFont(h, cx, ee::add(f25, 4.0f), s11, u32(h, 0x1f0), kBright, LOCALE_Symbol(kMissionFailed));
            const float y2 = ky(h, 0x108), y1 = ky(h, 0x104);
            if (h.hud_off != 0) {
                show_changepage = false;
                buf = fmt_s(LOCALE_Symbol(0x2201));
            } else {
                buf = fmt_s(LOCALE_Symbol(klabel(h, 0x108)));
            }
            const char* restart = LOCALE_Symbol(h.hud_off != 0 ? 0x2277 : 0x2268);
            const int32_t item = i32(h, 0x484);
            if (item == 1) {
                f23 = y1;
                f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x104), restart);
                c_restart = kBright;
            } else if (item == 2) {
                f23 = y2;
                f24 = GHud_GetCenter(h, 1.0f, u32(h, 0x108), buf.c_str());
                c_quit = kBright;
            }
            GHud_DrawBlinkingArrow(h, ee::sub(f24, 30.0f), ee::add(f23, 1.0f));
            GHud_DrawFont(h, GHud_GetCenter(h, 1.0f, u32(h, 0x104), restart), y1, 1.0f, u32(h, 0x104), c_restart,
                          restart);
            GHud_DrawFont(h, GHud_GetCenter(h, 1.0f, u32(h, 0x108), buf.c_str()), y2, 1.0f, u32(h, 0x108), c_quit,
                          buf.c_str());
            break;
        }
        default:
            break;
    }
tail:
    // As abas (MISSÃO, OBJETIVOS, CONTROLES, PONTOS) espalhadas entre 80 e 565.
    const float wide = hud_renderer().wide != 0 ? fl(0x3faaaaaau) : 1.0f;
    if (h.hud_off == 0 && h.result_text != kMissionFailed) {
        const char* names[4] = {LOCALE_Symbol(klabel(h, 0x10c)), LOCALE_Symbol(klabel(h, 0x110)),
                                LOCALE_Symbol(klabel(h, 0x118)), LOCALE_Symbol(klabel(h, 0x11c))};
        GGallery_FONT_SetState(g, key(h, 0x10c));
        GGallery_FONT_SetScale(g, 1.0f, 1.0f);
        float xs[4];
        float total = 0.0f, prev = 0.0f;
        xs[0] = 80.0f;
        for (int32_t i = 0; i < 4; ++i) {
            if (i > 0) xs[i] = ee::add(xs[i - 1], prev);
            prev = ee::mul(GGallery_FONT_Width(g, names[i]), wide);
            total = ee::add(total, prev);
        }
        const float gap = ee::div(ee::sub(485.0f, total), 3.0f);
        for (int32_t i = 1; i < 4; ++i) xs[i] = ee::add(xs[i], ee::mul(gap, ee::cvt_s(i)));
        for (int32_t i = 0; i < 4; ++i)
            GHud_DrawFont(h, xs[i], ee::add(ky(h, 0x10c), 4.0f), 1.0f, u32(h, 0x10c),
                          i == i32(h, 0x480) ? kBright : kTabDim, names[i]);
    }
    // A barra de ajuda: os ícones dos botões e os textos, centrada.
    uint32_t icons[8];
    const char* texts[8];
    int32_t n = 0;
    auto add = [&](bool on, uint32_t icon, uint32_t label) {
        if (!on) return;
        icons[n] = u32(h, icon);
        texts[n] = LOCALE_Symbol(klabel(h, label));
        ++n;
    };
    add(show_hint, 0x1e8, 0x1cc);
    add(show_select, 0x1e8, 0x1c8);
    add(show_invert, 0x1e4, 0x1c4);
    add(show_scroll_yes_no, 0x1e0, 0x1c0);
    add(show_back, 0x1dc, 0x1bc);
    add(show_changepage, 0x1ec, 0x1d0);
    add(show_continue, 0x1d8, 0x1d4);
    float iw[8], tw[8], total = 0.0f;
    for (int32_t i = 0; i < n; ++i) {
        iw[i] = kwidth(nf::p32_ptr<const uint8_t>(icons[i]));
        tw[i] = ee::mul(GGallery_FONT_Width(g, texts[i]), wide);
        total = ee::add(total, ee::add(ee::add(iw[i], tw[i]), 5.0f));
    }
    total = ee::add(total, ee::mul(ee::cvt_s(n - 1), 20.0f));
    const float y = ky(h, 0x1cc);
    float x = ee::mul(ee::sub(h.screen_width, total), 0.5f);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* ik = nf::p32_ptr<const uint8_t>(icons[i]);
        GGallery_SPRITE_SetState(g, ik);
        float iy;
        std::memcpy(&iy, ik + 4, 4);
        GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x), ee::cvt_w(iy), ik, nullptr);
        x = ee::add(x, ee::add(iw[i], 5.0f));
        GHud_DrawFont(h, x, y, 1.0f, u32(h, 0x1f0), 0x7f000000u, texts[i]);
        x = ee::add(x, ee::add(tw[i], 20.0f));
    }
}

// ---------------------------------------------------------------- o resto

void SMissionManager_TallyScores(SMissionManagerData& m, UberBondBlock& u, bool final_tally) {
    if (final_tally) {
        if (m.state != 3) return;
        for (int i = 0; i < 12; ++i) {
            const int32_t r = m.scores[i] % 0x32;
            if (0x2f < r) m.scores[i] = m.scores[i] + 0x32 - r;
        }
        for (int i = 0; i < 12; ++i) u.word(0x504 + 4 * uint32_t(i)) = m.scores[i] + m.uber_state[i];
    }
    const uint8_t* s = nf::p32_ptr<const uint8_t>(m.settings);
    auto sw = [&](uint32_t off) {
        int32_t v;
        std::memcpy(&v, s + off, 4);
        return v;
    };
    auto sf = [&](uint32_t off) {
        float v;
        std::memcpy(&v, s + off, 4);
        return v;
    };
    u.word(0x5e4) += sw(0x24c);
    u.word(0x5e8) += sw(0x250);
    u.word(0x5ec) += sw(0x254);
    const int32_t i5ec = u.word(0x5ec);
    u.word(0x5f0) += sw(0x268);
    const int32_t i5f0 = u.word(0x5f0);
    u.word(0x5f4) += sw(0x258);
    u.word(0x5f8) += sw(0x25c);
    u.word(0x5fc) += sw(0x260);
    u.word(0x600) += sw(0x264);
    u.word(0x604) += sw(0x26c);
    const int32_t i604 = u.word(0x604);
    u.word(0x608) += sw(0x270);
    u.word(0x60c) += sw(0x274);
    u.word(0x610) += sw(0x278);
    u.word(0x534) = sw(0x250) + sw(0x254) + sw(0x268) + sw(0x264) + sw(0x26c) + sw(0x278) + u.word(0x534);
    u.word(0x580) = m.unknown3d8[0] + u.word(0x580);
    u.word(0x588) += m.shots_fired;
    u.word(0x58c) += m.shots_hit;
    u.word(0x590) += m.kills;
    u.word(0x594) += m.unknown3d8[1];
    u.word(0x598) += sw(0x290);
    u.word(0x59c) += sw(0x28c);
    u.word(0x5a0) += m.end_step;
    u.word(0x5a8) += sw(0x2f0);
    u.word(0x5ac) += ee::cvt_w(m.player_damage);
    u.word(0x5b0) += m.stylin;
    u.word(0x5a4) += sw(0x2e4);
    u.word(0x5b4) += sw(0x2e8);
    u.real(0x5b8) = sf(0x2d0);
    u.real(0x5bc) = sf(0x2d4);
    u.real(0x5c0) = sf(0x2d8);
    u.real(0x5c8) = ee::div(ee::cvt_s(u.word(0x510)), ee::cvt_s(i5f0));
    u.real(0x5cc) = ee::div(ee::cvt_s(u.word(0x50c)), ee::cvt_s(i5ec));
    u.real(0x5c4) = ee::sub(1.0f, ee::div(ee::cvt_s(u.word(0x524)), ee::cvt_s(i604)));
    u.real(0x61c) = SMissionManager_GetPlayerHealth(m, true);
    u.real(0x620) = SMissionManager_GetPlayerShield(m, true);
    const int32_t fired = u.word(0x588), hit = u.word(0x58c);
    const int32_t lo = hit < fired ? hit : fired;
    u.word(0x9a8) = u.word(0x5b0);
    u.word(0x9ac) = u.word(0x5b4);
    u.word(0x9b0) = u.word(0x5e8);
    u.word(0x9b4) = u.word(0x590);
    u.word(0x9b8) = u.word(0x594);
    u.word(0x9bc) = u.word(0x610);
    u.word(0x9e4) = fired < 1 ? 100 : (lo * 100) / fired;
    u.word(0x9e8) = 100;
    u.word(0x9ec) = u.word(0x5ec);
    const float target = ee::cvt_s(sw(0x2ec));
    if (ee::lt(ee::mul(SMissionManager_GetPlayerHealth(m, true), 100.0f), target))
        u.word(0x9f0) = int32_t(fptoui(ee::mul(SMissionManager_GetPlayerHealth(m, true), 100.0f)));
    else
        u.word(0x9f0) = int32_t(fptoui(target));
    u.word(0x9f4) = sw(0x2ec);
    u.word(0x9f8) = u.word(0x5f8);
    u.word(0x9fc) = u.word(0x5a0);
    u.word(0xa00) = u.word(0x59c);
    u.word(0xa04) = u.word(0x600);
    u.word(0xa08) = u.word(0x580);
    u.word(0xa0c) = u.word(0x5a4);
    u.word(0xa10) = u.word(0x60c);
    u.word(0x9e0) = 0;
    for (uint32_t o = 0x9c0; o <= 0x9dc; o += 4) u.word(o) = 0;
}

int32_t InputConfigManager_GetLocaleID(const InputConfigManager& m, int32_t type, int32_t config, int32_t index) {
    const std::vector<InputConfigInfo>& list = m.configs[type & 1];
    if (config < 0 || size_t(config) >= list.size() || index < 0 || index >= 14) return 0;
    const uint32_t v = uint32_t(list[size_t(config)].locale[index]);
    const char* track = uber_bond().text(0x4f4);
    auto paris = [&] { return nf::str_icmp(track, "paris_mis01") == 0; };
    if (v == 0xa98 || v == 0xa99) return (paris() && type != 0) ? 0xaa1 : int32_t(v);
    if (v == 0x590) return nf::str_icmp(track, "snow1a_mis3") == 0 ? int32_t(v) : 0xaa1;
    if (v == 0xa94) return std::strstr(track, "uw_") ? 0xaa1 : int32_t(v);
    if (v == 0xa9f) return 0xaa1;
    if (v == 0xaa3) {
        if (!paris() || type == 0) return 0xaa3;
        return uint32_t(config - 6) > 1u ? 0xaa1 : 0x6ad;
    }
    return int32_t(v);
}

bool InputConfigManager_IsInverted(const InputConfigManager& m) { return m.d.inverted != 0; }

void InputConfigManager_SetInverted(InputConfigManager& m, bool inverted) {
    m.d.inverted = inverted ? 1 : 0;
    uber_bond().word(0xa24) = m.d.inverted;
}

int32_t GLocale_GetLanguage() {
    const int32_t v = uber_bond().word(0x988);
    if (uint32_t(v) <= 5u) return v;
    return v == 8 ? 6 : 7;
}

// ---------------------------------------------------------------- a moça

void GGirl_InitGirl(GHudData& h) {
    if (g_girl.init != 0) return;
    // GGallery::GET_SomeThing(galeria, 0): o dado 'fi' 0 do GGAL.
    const GGalleryData& g = gal(h);
    const UGroup* root = nf::p32_ptr<const UGroup>(g.gallery);
    g_girl.data = nullptr;
    if (root) {
        const UData* d = UGroup_DataLocateTag(root, 0x66690000u);
        if (d != UGroup_DataEnd(root)) g_girl.data = UData_Data(d);
    }
    g_girl.init = 1;
    g_girl.fps = 30;
    g_girl.unknown_aaf8 = 1;
    g_girl.unknown_aafc = 0;
    g_girl.unknown_ab00 = 0;
    for (int i = 0; i < 2; ++i) g_girl.pixels[i].assign(128 * 128, 0u);  // GGirlieMaterial: SHAPE_create(128, 128, 32)
}

void GGirl_KillGirl() {
    if (g_girl.init == 0) return;
    // (o destrutor virtual de cada GGirlieMaterial)
    for (int i = 0; i < 2; ++i) g_girl.pixels[i].clear();
    g_girl.init = 0;
    g_girl.data = nullptr;
}

void GGirl_ClearTexture(int32_t i) {
    std::vector<uint32_t>& p = g_girl.pixels[i & 1];
    std::fill(p.begin(), p.end(), 0u);
}

void GGirl_DrawPoint(int32_t x, int32_t y, uint32_t colour) {
    std::vector<uint32_t>& p = g_girl.pixels[g_girl.buffer & 1];
    const int64_t at = int64_t(x) + int64_t(y) * 128;
    if (at >= 0 && size_t(at) < p.size()) p[size_t(at)] = colour;
}

bool GGirl_UpdateTexture(int32_t& out_x, int32_t& out_y) {
    GGirlState& s = g_girl;
    int32_t count = 0, value = 0;  // s2, s1
    uint32_t pending = 0;          // t7
    const int32_t now = TIMER_gettick();
    if (s.init == 0) return false;
    if (s.frame_count <= s.frame) s.frames = nullptr;
    if (s.frames == nullptr) {
        if (!s.data) return false;  // (guarda do port: o PS2 leria o endereço 0)
        s.frames = s.data;
        s.warmup = 0.0f;
        s.frame = 0;
        s.cursor = 2;
        s.frame_count = int32_t(s.data[0]) * 0x100 + int32_t(s.data[1]);
        s.saved = 2;
        s.last_tick = now;
        ++g_stats.girl_restarts;
        if (0x400 < s.frame_count) {
            s.frames = nullptr;
            return false;
        }
    }
    const uint8_t* d = s.frames;
    auto byte = [&](int64_t at) { return d[int32_t(at)]; };
    // Um passo do RLE: o valor (0..7) e quantos pixels ainda.
    auto rle = [&]() {
        if (count < 1) {
            const uint8_t b = byte(s.cursor);
            s.cursor += 1;
            value = b;
            if ((b & 0x80) == 0) {
                if ((b & 0x40) == 0) {
                    count = 1;
                    pending = uint32_t(b + 0x40) & 0xffu;
                    value &= 7;
                } else {
                    count = (b >> 3) & 7;
                    value &= 7;
                }
            } else {
                count = b & 0x3f;
                value = (b & 0x40) == 0 ? 0 : 7;
            }
        } else if (pending == 0) {
            count = int32_t(int16_t(count - 1));
        } else {
            value = int32_t(pending >> 3) & 7;
            count = int32_t(int16_t(count - 1));
            pending = 0;
        }
    };
    if (ee::lt(s.warmup, 2.0f)) {
        s.warmup = ee::add(s.warmup, ee::mul(cvt_u(uint32_t(now - s.last_tick)), simulation().dt));
        s.last_tick = now;
    }
    uint32_t steps = fptoui(ee::add(ee::mul(ee::mul(ee::cvt_s(s.fps), cvt_u(uint32_t(now - s.last_tick))), simulation().dt),
                                    0.5f));
    if (uint32_t(s.frame_count / 2) < steps) {
        s.last_tick = now;
        steps = 0;
    }
    if (steps == 0) {
        s.cursor = s.saved;
    } else if (steps == 1) {
        s.frame += 1;
        s.last_tick = now;
        s.saved = s.cursor;
    } else {
        for (uint32_t f = 1; f < steps; ++f) {
            const uint8_t w = byte(s.cursor);
            const uint8_t hgt = byte(s.cursor + 1);
            s.cursor += 4;
            if (hgt == 0) continue;
            for (int32_t r = 0; r < int32_t(hgt); r = int32_t(int16_t(r + 1)))
                for (int32_t c = 0; c < int32_t(w); ++c) rle();
        }
        s.frame += int32_t(steps);
        s.last_tick = now;
        s.saved = s.cursor;
    }
    const uint8_t w = byte(s.cursor), hgt = byte(s.cursor + 1), px = byte(s.cursor + 2), py = byte(s.cursor + 3);
    s.cursor += 4;
    if (!(w < 0x80 && hgt < 0x80 && px < 0x80 && py < 0x80)) {
        s.frames = nullptr;
        return false;
    }
    s.scroll_x = ee::add(s.scroll_x, 0.31f);
    s.unknown_ab20 = -0.71f;
    s.unknown_ab24 = -0.19f;
    s.speed_x = 0.31f;
    s.speed_y = 0.57f;
    if (ee::le(128.0f, s.scroll_x)) s.scroll_x = ee::sub(s.scroll_x, 128.0f);
    if (ee::le(s.scroll_x, 0.0f)) s.scroll_x = ee::add(s.scroll_x, 128.0f);
    s.scroll_y = ee::add(s.scroll_y, 0.57f);
    if (ee::le(128.0f, s.scroll_y)) s.scroll_y = ee::sub(s.scroll_y, 128.0f);
    if (ee::le(s.scroll_y, 0.0f)) s.scroll_y = ee::add(s.scroll_y, 128.0f);
    const std::vector<int8_t>& t = g_colours;
    auto colour = [&](int32_t at) { return at >= 0 && size_t(at) < t.size() ? t[size_t(at)] : int8_t(0); };
    auto wrap = [](int32_t v) {
        while (0x7f < v) v -= 0x80;
        return v;
    };
    const float fx = ee::cvt_s(px), fy = ee::cvt_s(py);
    for (int32_t r = 0; r < int32_t(hgt); r = int32_t(int16_t(r + 1))) {
        const float fr = ee::cvt_s(r);
        for (int32_t c = 0; c < int32_t(w); c = int32_t(int16_t(c + 1))) {
            rle();
            uint32_t pixel = 0;
            if (value != 0) {
                const float fc = ee::cvt_s(c);
                const int32_t a1 = wrap(ee::cvt_w(ee::add(ee::add(fr, s.scroll_y), fy))) * 0x180;
                const int32_t t4 = wrap(ee::cvt_w(ee::add(ee::add(fc, s.scroll_x), fx))) * 3;
                const int32_t a3 = wrap(ee::cvt_w(ee::add(ee::add(fr, s.offset_y), fy))) * 0x180;
                const int32_t t3 = wrap(ee::cvt_w(ee::add(ee::add(fc, s.offset_x), fx))) * 3;
                const int32_t i0 = t4 + a1, i1 = t3 + a3;
                auto chan = [&](int32_t k) {
                    const float p = ee::mul(ee::cvt_s(colour(i0 + k)), 0.00390625f);
                    const float q = ee::mul(ee::cvt_s(colour(i1 + k)), 0.00390625f);
                    return uint32_t(ee::cvt_w(ee::mul(ee::add(p, q), 255.0f))) & 0xffu;
                };
                const uint32_t alpha = (uint32_t(value * 0x1b) & 0xffu) >> 1;
                pixel = alpha << 24 | chan(2) << 16 | chan(1) << 8 | chan(0);
            }
            if (s.normal == 1)
                GGirl_DrawPoint(c, r, pixel);
            else
                GGirl_DrawPoint(0x7f - c, r, pixel);
        }
    }
    if (s.normal == 1)
        out_x = (int32_t(px) + 8) * 2;
    else
        out_x = (0xb8 - int32_t(px)) * 2;
    out_y = (int32_t(py) + 0x3c) * 2;
    ++g_stats.girl_frames;
    return true;
}

void GGirl_DrawTexture(int32_t x, int32_t y) {
    static const float uv[2][4] = {{0, 0, 1, 0}, {1, 1, 1, 0}};
    const int32_t b = g_girl.buffer & 1;
    uint32_t& img = g_girl.image[b];
    if (g_sink) img = g_sink(b, g_girl.pixels[b]);
    Draw_DrawSprite(ee::cvt_s(x), ee::cvt_s(y), 256.0f, 256.0f, 0x7f7f7f7fu, uv, img);
}

void GGirl_DoGirl(GHudData& h) {
    if (g_girl.init == 0) {
        GGirl_InitGirl(h);
        for (int32_t i = 0; i < 2; ++i) GGirl_ClearTexture(i);
    } else {
        GGirl_ClearTexture(g_girl.buffer);
    }
    int32_t x = 0, y = 0;
    if (GGirl_UpdateTexture(x, y)) {
        // GGirl::DownloadTexture: vazia.
        GGirl_DrawTexture(x, y);
        g_girl.buffer += 1;
        if (1 < g_girl.buffer) g_girl.buffer = 0;
    }
}

}  // namespace drv
