#pragma once
// As opções do jogo (as das páginas de opções do menu principal, da aba CONTROLS da pausa e do perfil do codinome)
// e a regra delas com o nightfire.ini. Só do port: o PS2 guarda essas opções no perfil do codinome, no cartão.
//
// Uma fonte de verdade por valor: cada opção mora no lugar do jogo (PlayerSetting[0], DrawInfo, o volume e o modo do
// som de platform/psi_sound.h) e é lida e escrita lá: game_option / set_game_option (para as telas do port, como as
// do estilo Modern). O .ini só lembra, entre execuções, as que têm sentido no PC:
//   [controls] vibration, pad_style, pad_invert_y   (as chaves do ControlsConfig, input_bindings.h)
//   [ui]       subtitles
//   [game]     auto_aim, crosshairs, crouch (toggle|hold), manual_aim (toggle|hold), weapon_auto_switch,
//              flashing_objects, hud_always_on, music_volume, effects_volume (0-100)
// As outras só ficam no lugar do jogo e no perfil, sem efeito no PC (decidido com o usuário): widescreen
// (DrawInfo[1]: o PC desenha na proporção da janela, Hor+), tela dividida (DrawInfo[0]), o modo do som e a posição
// da tela (psiSetScreenPos), além das do multijogador.
//
// Precedência:
//  1. Na partida (psiInput_Init, de Input_Init no bootup_bootup) as chaves que o .ini tem valem por cima dos padrões
//     do PS2 (o estilo, a inversão e a vibração sempre, como antes: o ControlsConfig tem padrões próprios).
//  2. Um codinome do cartão lido (LS_LoadPlrSettings, LS_LoadGlobalSettings) põe as opções dele, como no jogo, e o
//     .ini não muda (game_options_rebase).
//  3. Os perfis padrão ("New Codename", "Play without saving", "Default": Menu_MapDefaultCodename; e as legendas que
//     Menu_WillRunMiniMission desliga na versão americana) pegam as opções que o .ini tem por cima das de
//     def_codename (game_options_default_profile): no PC o .ini faz o papel do perfil padrão que o PS2 perde ao
//     desligar.
//  4. Uma opção mudada num menu (qualquer diferença desde o último rebase, vista a cada tick por game_options_sync,
//     no começo de psiInput_MapInputs, e por nf::persist_controls antes de gravar) passa a ser a do .ini e o arquivo
//     é gravado na hora. Só a opção mudada vai para o .ini: as que um codinome trouxe e o jogador não mexeu ficam
//     como estavam no arquivo.
// O arquivo só recebe as chaves que o .ini conhece (as que ele já tinha e as mudadas num menu), também as três de
// [controls]: gravar outra opção (a sensibilidade do mouse, uma tecla) não escreve o estilo, a inversão e a vibração
// em vigor. Antes elas iam sempre, e um pad_style = 0 escrito assim fazia os perfis padrão ("New Codename", "Play
// without saving") começarem no estilo 0 (NightFire) em vez do 7 (Classic Bond) de def_codename. Um .ini que já tem
// a chave continua valendo (compatível com os arquivos antigos).
// Tudo isso só com nf::g_controls_persist (fora das capturas, ou com --config): sem um .ini para gravar (as capturas
// sem --config, os testes) o .ini não lembra nada e o port se comporta como o PS2.
// Opções do PC (vídeo, mouse, teclas, interface HD...) só existem no .ini; o perfil nunca as toca.

#include <cstdint>

namespace nf {

class Settings;

enum class GameOption : uint8_t {
    Vibration,        // PlayerSetting::unknown_09 (0/1)
    AutoAim,          // PlayerSetting::autoaim
    Crosshairs,       // PlayerSetting::unknown_08: a mira aparece sem mirar
    CrouchToggle,     // PlayerSetting::crouch_toggle (1 alterna, 0 segurar)
    AimToggle,        // PlayerSetting::aim_toggle ("Manual Aim": 1 alterna, 0 segurar)
    AutoSwitch,       // PlayerSetting::auto_switch
    FlashingObjects,  // PlayerSetting::unknown_0c (o menu põe 2 ligado, 0 desligado; o boot deixa 1)
    HudAlwaysOn,      // PlayerSetting::unknown_0b == 0
    PadStyle,         // PlayerSetting::style (0 NightFire ... 7 Classic Bond)
    InvertLook,       // PlayerSetting::invert_look
    Subtitles,        // DrawInfo[2]
    MusicVolume,      // SFXMusicGetVolume (0-100)
    EffectsVolume,    // SFXGetVolume (0-100)
    SpeakerMode,      // SFXGetMode (0 mono, 1 estéreo, 4 surround); só guardado
    Widescreen,       // DrawInfo[1]; só guardado (sem efeito no PC)
    SplitScreen,      // DrawInfo[0] (0 horizontal, 1 vertical); só guardado
    Count
};
constexpr int kGameOptionCount = int(GameOption::Count);

// O valor no lugar do jogo, nas unidades do menu (ligar = 1, desligar = 0).
int32_t game_option(GameOption o);
// Escreve no lugar do jogo (FlashingObjects 1 põe 2, como o menu; HudAlwaysOn 1 põe unknown_0b = 0). O .ini segue no
// próximo game_options_sync, como uma mudança feita no menu.
void set_game_option(GameOption o, int32_t value);
// A chave do .ini ("auto_aim"...), ou nulo se a opção não vai para o arquivo.
const char* game_option_key(GameOption o);

// O que o .ini lembra: o valor e se ele está no arquivo (ou foi mudado num menu nesta execução).
struct GameOptionsIni {
    int32_t value[kGameOptionCount] = {};
    bool known[kGameOptionCount] = {};
};
GameOptionsIni& game_options_ini();

// Lê as chaves de `s` (main.cpp, apply_settings, depois de load_controls); as do [controls] vêm de nf::controls().
void load_game_options(const Settings& s, GameOptionsIni* g);
// Grava as chaves conhecidas de [controls] (vibration, pad_style, pad_invert_y), [ui] e [game]; as desconhecidas não
// são escritas. nf::persist_controls a chama (o save_controls só grava as opções do PC).
void save_game_options(Settings& s, const GameOptionsIni& g);

// 1. As opções que o .ini tem nos lugares do jogo, e o rebase (psiInput_Init).
void game_options_apply_ini();
// 2. O estado atual vira a referência, sem tocar o .ini (um codinome lido, as legendas do --level).
void game_options_rebase();
// 3. Um perfil padrão aplicado (Menu_MapDefaultCodename `what`: 1 as opções do jogador e as legendas, 8 os volumes;
//    kGameOptionsSubtitles só as legendas, Menu_WillRunMiniMission): as que o .ini tem por cima, e o rebase.
constexpr uint32_t kGameOptionsSubtitles = 0x100;
void game_options_default_profile(uint32_t what);
// 4. Mudanças desde o rebase vão para o .ini (na memória sempre; o arquivo com g_controls_persist). Devolve se alguma
//    opção do .ini mudou.
bool game_options_sync();
// A primeira metade do 4, sem gravar o arquivo: as mudanças desde o rebase passam a ser as do .ini (só com
// g_controls_persist). nf::persist_controls a chama antes de gravar, então um menu que muda uma opção do jogo e grava
// na mesma hora (a vibração da página de controles) já escreve a chave.
bool game_options_note_changes();

// "opcoes_jogo vibra V mira_auto A ..." (relatório das capturas).
int game_options_describe(char* out, unsigned size);
void game_options_debug_power_on();

}  // namespace nf
