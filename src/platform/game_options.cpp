// As opções do jogo e a regra delas com o nightfire.ini. Ver game_options.h.

#include "platform/game_options.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "engine/input.h"
#include "engine/resetmap.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_sound.h"
#include "platform/settings.h"

namespace nf {

namespace {

GameOptionsIni g_ini;
int32_t g_base[kGameOptionCount] = {};  // os valores do último rebase
bool g_based = false;

enum class Kind : uint8_t { Bool, ToggleHold, Volume, Style, None };
struct Info {
    const char* section;
    const char* key;
    Kind kind;
};
// A ordem de GameOption. As de [controls] têm a chave do ControlsConfig (lida e gravada por input_bindings.cpp).
const Info kInfo[kGameOptionCount] = {
    {"controls", "vibration", Kind::Bool},
    {"game", "auto_aim", Kind::Bool},
    {"game", "crosshairs", Kind::Bool},
    {"game", "crouch", Kind::ToggleHold},
    {"game", "manual_aim", Kind::ToggleHold},
    {"game", "weapon_auto_switch", Kind::Bool},
    {"game", "flashing_objects", Kind::Bool},
    {"game", "hud_always_on", Kind::Bool},
    {"controls", "pad_style", Kind::Style},
    {"controls", "pad_invert_y", Kind::Bool},
    {"ui", "subtitles", Kind::Bool},
    {"game", "music_volume", Kind::Volume},
    {"game", "effects_volume", Kind::Volume},
    {nullptr, nullptr, Kind::None},
    {nullptr, nullptr, Kind::None},
    {nullptr, nullptr, Kind::None},
};

bool in_controls(GameOption o) { return o == GameOption::Vibration || o == GameOption::PadStyle || o == GameOption::InvertLook; }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

int32_t clamp_value(GameOption o, int32_t v) {
    switch (kInfo[int(o)].kind) {
        case Kind::Volume:
            return std::clamp(v, 0, 100);
        case Kind::Style:
            return std::clamp(v, 0, 7);
        case Kind::Bool:
        case Kind::ToggleHold:
            return v != 0 ? 1 : 0;
        default:
            return v;
    }
}

// O valor do .ini nos ControlsConfig (as três de [controls]: os menus do PC as mostram de lá e psiInput_Init as copia
// para o PlayerSetting na partida).
void to_controls(GameOption o, int32_t v) {
    ControlsConfig& c = controls();
    if (o == GameOption::Vibration) c.vibration = v != 0;
    if (o == GameOption::PadStyle) c.pad_style = uint16_t(v);
    if (o == GameOption::InvertLook) c.pad_invert_y = v != 0;
}

}  // namespace

int32_t game_option(GameOption o) {
    const PlayerInput_tag& p = PlayerSetting[0];
    switch (o) {
        case GameOption::Vibration: return p.unknown_09;
        case GameOption::AutoAim: return p.autoaim;
        case GameOption::Crosshairs: return p.unknown_08;
        case GameOption::CrouchToggle: return p.crouch_toggle;
        case GameOption::AimToggle: return p.aim_toggle;
        case GameOption::AutoSwitch: return p.auto_switch;
        case GameOption::FlashingObjects: return p.unknown_0c != 0 ? 1 : 0;
        case GameOption::HudAlwaysOn: return p.unknown_0b == 0 ? 1 : 0;
        case GameOption::PadStyle: return p.style;
        case GameOption::InvertLook: return p.invert_look;
        case GameOption::Subtitles: return int32_t(DrawInfo[2]);
        case GameOption::MusicVolume: return int32_t(SFXMusicGetVolume());
        case GameOption::EffectsVolume: return int32_t(SFXGetVolume());
        case GameOption::SpeakerMode: return int32_t(SFXGetMode());
        case GameOption::Widescreen: return int32_t(DrawInfo[1]);
        case GameOption::SplitScreen: return int32_t(DrawInfo[0]);
        default: return 0;
    }
}

void set_game_option(GameOption o, int32_t v) {
    PlayerInput_tag& p = PlayerSetting[0];
    const uint8_t b = v != 0 ? 1 : 0;
    switch (o) {
        case GameOption::Vibration: p.unknown_09 = b; break;
        case GameOption::AutoAim: p.autoaim = b; break;
        case GameOption::Crosshairs: p.unknown_08 = b; break;
        case GameOption::CrouchToggle: p.crouch_toggle = b; break;
        case GameOption::AimToggle: p.aim_toggle = b; break;
        case GameOption::AutoSwitch: p.auto_switch = b; break;
        case GameOption::FlashingObjects: p.unknown_0c = b != 0 ? 2 : 0; break;
        case GameOption::HudAlwaysOn: p.unknown_0b = b != 0 ? 0 : 1; break;
        case GameOption::PadStyle: Input_ChangeControllerStyle(0, uint32_t(std::clamp(v, 0, 7))); break;
        case GameOption::InvertLook: p.invert_look = b; break;
        case GameOption::Subtitles: DrawInfo[2] = b; break;
        case GameOption::MusicVolume: SFXMusicSetVolume(uint32_t(std::clamp(v, 0, 100))); break;
        case GameOption::EffectsVolume: SFXSetVolume(uint32_t(std::clamp(v, 0, 100))); break;
        case GameOption::SpeakerMode: SFXSetMode(v); break;
        case GameOption::Widescreen: DrawInfo[1] = b; break;
        case GameOption::SplitScreen: DrawInfo[0] = uint32_t(v); break;
        default: break;
    }
}

const char* game_option_key(GameOption o) { return int(o) < kGameOptionCount ? kInfo[int(o)].key : nullptr; }

GameOptionsIni& game_options_ini() { return g_ini; }

void load_game_options(const Settings& s, GameOptionsIni* g) {
    const ControlsConfig& c = controls();
    for (int i = 0; i < kGameOptionCount; ++i) {
        const Info& f = kInfo[i];
        const GameOption o = GameOption(i);
        if (f.key == nullptr) continue;
        if (in_controls(o)) {
            // Lidas por load_controls (o estilo também pelo nome do menu).
            g->known[i] = s.has(f.section, f.key);
            g->value[i] = o == GameOption::Vibration ? (c.vibration ? 1 : 0)
                          : o == GameOption::PadStyle ? int32_t(c.pad_style)
                                                      : (c.pad_invert_y ? 1 : 0);
            continue;
        }
        if (!s.has(f.section, f.key)) continue;
        g->known[i] = true;
        if (f.kind == Kind::ToggleHold) {
            const std::string v = lower(s.get(f.section, f.key));
            g->value[i] = v == "hold" ? 0 : v == "toggle" ? 1 : (s.get_bool(f.section, f.key, true) ? 1 : 0);
        } else if (f.kind == Kind::Volume) {
            g->value[i] = clamp_value(o, s.get_int(f.section, f.key, 100));
        } else {
            g->value[i] = s.get_bool(f.section, f.key, false) ? 1 : 0;
        }
    }
}

void save_game_options(Settings& s, const GameOptionsIni& g) {
    for (int i = 0; i < kGameOptionCount; ++i) {
        const Info& f = kInfo[i];
        // Só as chaves que o .ini conhece: um valor em vigor que o jogador não mudou não vai para o arquivo (com o
        // estilo 0 escrito assim, os perfis padrão deixavam o estilo 7 de def_codename).
        if (f.key == nullptr || !g.known[i]) continue;
        if (f.kind == Kind::Style) {
            s.set_int(f.section, f.key, g.value[i]);
        } else if (f.kind == Kind::ToggleHold) {
            s.set(f.section, f.key, g.value[i] != 0 ? "toggle" : "hold");
        } else if (f.kind == Kind::Volume) {
            s.set_int(f.section, f.key, g.value[i]);
        } else {
            s.set_bool(f.section, f.key, g.value[i] != 0);
        }
    }
}

void game_options_rebase() {
    for (int i = 0; i < kGameOptionCount; ++i) g_base[i] = game_option(GameOption(i));
    g_based = true;
}

void game_options_apply_ini() {
    for (int i = 0; i < kGameOptionCount; ++i) {
        // O estilo, a inversão e a vibração já vieram do ControlsConfig (psiInput_Init).
        if (g_ini.known[i] && !in_controls(GameOption(i))) set_game_option(GameOption(i), g_ini.value[i]);
    }
    game_options_rebase();
}

void game_options_default_profile(uint32_t what) {
    for (int i = 0; i < kGameOptionCount; ++i) {
        const GameOption o = GameOption(i);
        if (!g_ini.known[i]) continue;
        const bool player = o <= GameOption::InvertLook || o == GameOption::Subtitles;
        const bool sound = o == GameOption::MusicVolume || o == GameOption::EffectsVolume;
        const bool apply = ((what & 1) != 0 && player) || ((what & 8) != 0 && sound) ||
                           ((what & kGameOptionsSubtitles) != 0 && o == GameOption::Subtitles);
        if (!apply) continue;
        if (o == GameOption::PadStyle) {
            PlayerSetting[0].style = uint16_t(g_ini.value[i]);  // como o perfil põe o estilo (sem limpar as ações)
        } else {
            set_game_option(o, g_ini.value[i]);
        }
    }
    game_options_rebase();
}

bool game_options_sync() {
    const bool changed = game_options_note_changes();
    if (changed) persist_controls();  // grava [controls], [bindings], [ui] subtitles e [game] (só com g_controls_persist)
    return changed;
}

bool game_options_note_changes() {
    if (!g_based) {
        game_options_rebase();
        return false;
    }
    bool changed = false;
    for (int i = 0; i < kGameOptionCount; ++i) {
        const GameOption o = GameOption(i);
        const int32_t now = game_option(o);
        if (now == g_base[i]) continue;
        g_base[i] = now;
        // Sem um .ini para gravar (as capturas sem --config, os testes) o port é o PS2: o .ini não lembra nada.
        if (kInfo[i].key == nullptr || !g_controls_persist) continue;
        g_ini.value[i] = clamp_value(o, now);
        g_ini.known[i] = true;
        if (in_controls(o)) to_controls(o, g_ini.value[i]);
        changed = true;
    }
    return changed;
}

int game_options_describe(char* out, unsigned size) {
    static const char* const kNames[kGameOptionCount] = {"vibra", "mira_auto", "mira", "agachar", "mirar", "troca",
                                                         "piscar", "hud", "estilo", "inverte", "legendas", "musica",
                                                         "efeitos", "som", "wide", "dividida"};
    int n = std::snprintf(out, size, "opcoes_jogo");
    for (int i = 0; i < kGameOptionCount && 0 <= n && unsigned(n) < size; ++i) {
        n += std::snprintf(out + n, size - unsigned(n), " %s %d%s", kNames[i], game_option(GameOption(i)),
                           g_ini.known[i] ? "*" : "");
    }
    // A posição da tela (o ajuste de tela; só guardada no PC).
    if (0 <= n && unsigned(n) < size) {
        int32_t x = 0, y = 0;
        psiGetScreenPos(&x, &y);
        n += std::snprintf(out + n, size - unsigned(n), " tela %d %d", x, y);
    }
    return n;
}

void game_options_debug_power_on() {
    g_ini = GameOptionsIni{};
    for (int32_t& v : g_base) v = 0;
    g_based = false;
}

}  // namespace nf
