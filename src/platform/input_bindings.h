#pragma once
// Teclas e botões dos comandos do jogador (só do port). O PS2 tem oito configurações fixas de botões
// (PlayerSetting::style, psiInput_MapInputs); no PC o teclado e o mouse passam por esta tabela: cada
// comando (andar, atirar, pausa, os do menu...) tem até kMaxBinds entradas entre teclas, botões do
// mouse, a roda e entradas do controle, e põe um valor numa ou em duas das 40 ações do jogo
// (engine/input.h), ou numa ação só do port (PortAction: recarregar e os espaços dos aparelhos, que o PS2 não
// tem; psi_input as entrega ao jogo por psiInput_PortPressed).
//
// Os padrões seguem o costume dos jogos de PC (tests: test_input tem a especificação escrita): andar WASD,
// virar e olhar com as setas, pular Espaço, agachar C ou Ctrl, atirar e mirar com os botões do mouse, usar E
// (usar ou recarregar, como o botão do PS2), recarregar R, modo de tiro botão do meio ou X, visão noturna N,
// armas na roda (para baixo a próxima, para cima a anterior; Z e Q também), aparelhos 1 a 5 (o n-ésimo que o
// jogador tem) e G (o próximo), pausa Enter ou Esc; nos menus setas e WASD, escolher Espaço, círculo Tab,
// voltar Esc ou Backspace; a roda de armas segurando Tab (no controle, L3: o único botão que nenhum dos oito estilos
// usa).
//
// Esc: pausa no jogo e voltar nos menus. Com um menu do jogo aberto, uma entrada do "pause" que também é de um
// comando do menu (Esc, no padrão) vale só no menu (Start e triângulo juntos se anulam em Menu_InputAction) e,
// fechado o menu, fica fora do "pause" até ser solta (não reabre a pausa): psi_input, pause_menu_shared.
//
// O controle do PC segue os estilos do jogo pelo controle do PS2 (platform/psi_pad.h); uma entrada do
// controle posta na tabela vale a mais e sai do controle do PS2 (um botão religado deixa de fazer o que o
// estilo mandava).
//
// Conflitos: uma entrada fica num só comando de cada grupo (jogo ou menu; entre grupos pode repetir,
// como Espaço para pular e escolher no menu). Ligar uma entrada a um comando a tira do outro; se o outro
// é obrigatório (pausa, escolher, voltar, cima e baixo no menu) e ficaria sem tecla, ele recebe a tecla
// que o comando novo tinha (troca), e sem ela a ligação é recusada.
//
// nightfire.ini: [bindings] uma chave por comando (`fire = mouse1, rctrl`, nomes em input_name; vazio:
// nenhuma) e [controls] as opções (ControlsConfig). Ver docs/port.md, "Controles".

#include <cstdint>
#include <string>

namespace nf {

class Settings;
struct PadState;

enum class InputKind : uint8_t { None = 0, Key = 1, Wheel = 2, Pad = 3 };

// Entradas do controle (InputKind::Pad): os botões pelo número do bit do XInput, os gatilhos e as
// metades dos analógicos.
enum : uint8_t {
    kPadInDpadUp = 0,
    kPadInDpadDown = 1,
    kPadInDpadLeft = 2,
    kPadInDpadRight = 3,
    kPadInStart = 4,
    kPadInBack = 5,
    kPadInLThumb = 6,
    kPadInRThumb = 7,
    kPadInLB = 8,
    kPadInRB = 9,
    kPadInA = 12,
    kPadInB = 13,
    kPadInX = 14,
    kPadInY = 15,
    kPadInLT = 16,
    kPadInRT = 17,
    kPadInLXPos = 18,  // analógico esquerdo para a direita
    kPadInLXNeg = 19,
    kPadInLYPos = 20,  // para cima
    kPadInLYNeg = 21,
    kPadInRXPos = 22,
    kPadInRXNeg = 23,
    kPadInRYPos = 24,
    kPadInRYNeg = 25,
    kPadInCount = 26,
};

struct InputCode {
    InputKind kind = InputKind::None;
    uint8_t code = 0;  // Key: código virtual (VK_*, também os botões do mouse); Wheel: 0 cima, 1 baixo; Pad: kPadIn*
    bool valid() const { return kind != InputKind::None; }
    bool is_pad() const { return kind == InputKind::Pad; }
    friend bool operator==(InputCode a, InputCode b) { return a.kind == b.kind && a.code == b.code; }
    friend bool operator!=(InputCode a, InputCode b) { return !(a == b); }
};
InputCode key_input(uint8_t vk);
InputCode wheel_input(bool down);
InputCode pad_input(uint8_t code);

// Nome no nightfire.ini ("w", "space", "ctrl", "mouse1", "wheel_up", "pad_a", "pad_lx+"...).
std::string input_name(InputCode c);
bool parse_input_name(const std::string& name, InputCode* out);
// Texto curto para os menus (UTF-8; "Espaço", "Mouse 1", "RT"...).
std::string input_label(InputCode c, bool english);

// ---- comandos ----
enum class BindGroup : uint8_t { Game, Menu };
constexpr int kMaxBinds = 4;
constexpr int kMaxKeyBinds = 2;  // teclado/mouse por comando (o resto é do controle)

// Ações só do port: BindTarget::action = kPortActionBase + PortAction. O PS2 recarrega pelo botão de usar
// (Player_WeaponFiring: usa, ou recarrega se nada foi usado) e troca de aparelho só em ciclo; no PC "reload" (R)
// recarrega sem usar nada (Player_WeaponFiring, a recarga do port) e "gadget_1" ... "gadget_5" (1 a 5) escolhem o
// n-ésimo aparelho que o jogador tem (Player_PortSelectGadget). engine/player_weapon.cpp os lê.
enum PortAction : int8_t {
    kPortReload = 0,
    kPortGadget1 = 1,  // ... kPortGadget1 + 4 = o quinto
    // A roda de armas (engine/weapon_wheel.h): o main.cpp a lê segurada a cada quadro de vídeo (command_value); a ação
    // existe para um botão do controle religado nela sair do controle do PS2, como os das outras.
    kPortWeaponWheel = 6,
    kPortActionCount = 7,
};
constexpr int kPortGadgetSlots = 5;
constexpr int kPortActionBase = 40;  // depois das 40 ações do jogo (kActionCount)
constexpr bool is_port_action(int action) { return action >= kPortActionBase && action < kPortActionBase + kPortActionCount; }

struct BindTarget {
    int8_t action;  // ação de engine/input.h (-1: nenhuma; kPortActionBase + PortAction: só do port)
    int8_t sign;    // +1 ou -1
};
struct BindCommand {
    const char* key;  // chave em [bindings]
    BindGroup group;
    bool required;    // não pode ficar sem tecla
    BindTarget targets[2];
    const char* label_pt;
    const char* label_en;
    const char* defaults;  // como no .ini
};
constexpr int kBindCommandCount = 38;
extern const BindCommand kBindCommands[kBindCommandCount];
int find_bind_command(const std::string& key);  // -1 se não existe
// O índice do comando "pause" (Start).
int pause_command();
// A ação do jogo cujos botões do estilo do controle do PS2 fazem o comando (as colunas do controle das páginas de
// teclas, entre parênteses): a do primeiro alvo; "reload" é o usar (14: no PS2 o mesmo botão recarrega); -1 se o
// controle do PS2 não tem (os espaços dos aparelhos, os comandos do menu Modern).
int pad_style_action(int command);

struct Bindings {
    InputCode in[kBindCommandCount][kMaxBinds];
    void reset();  // os padrões
    bool has(int command, InputCode c) const;
    // As entradas do teclado/mouse (pad = false) ou do controle (pad = true) do comando, na ordem.
    int list(int command, bool pad, InputCode* out, int max) const;
};
Bindings default_bindings();

// Resultado de bind().
struct BindResult {
    bool refused = false;  // o comando obrigatório de onde ela sairia ficaria sem tecla
    int moved_from = -1;   // comando que perdeu a entrada (mesmo grupo)
    int swapped_to = -1;   // comando obrigatório que recebeu a tecla antiga deste (troca)
    InputCode swapped;     // essa tecla
};
// Liga `c` ao comando: do teclado/mouse vira a primeira (a segunda anterior sai, até kMaxKeyBinds); do
// controle troca a do controle. Conflitos como no cabeçalho.
BindResult bind(Bindings& b, int command, InputCode c);
// Apaga as entradas do teclado/mouse (pad = false) ou do controle do comando. Falso se o comando é
// obrigatório e ficaria sem teclado.
bool clear_binds(Bindings& b, int command, bool pad);
// A tabela de teclas do menu Modern (colunas PRIMARY, SECONDARY, CONTROLLER = slot 0, 1, 2): slot 1 liga
// `c` como a segunda tecla (a primeira continua; sem primeira, ela vira a primeira), com os conflitos de
// bind(); slot 0 e as do controle são bind(). clear_bind_slot apaga só aquela (2: a do controle); falso se
// o comando é obrigatório e ficaria sem teclado.
BindResult bind_slot(Bindings& b, int command, int slot, InputCode c);
bool clear_bind_slot(Bindings& b, int command, int slot);
// O comando põe valor em alguma ação do jogo ou do port (os do menu Modern não: são lidos com command_value).
bool command_has_action(int command);
// As entradas do "pause" que também são de um comando do menu (Esc no padrão: pausa no jogo, voltar nos menus).
// Devolve quantas (até kMaxBinds).
int pause_menu_shared(const Bindings& b, InputCode* out);

// ---- valores ----
// O estado de entrada de um tick.
struct InputFrame {
    const bool* keys = nullptr;      // 256, pelo código virtual (nulo: nada)
    bool wheel_up = false;           // a roda conta como um aperto de um tick
    bool wheel_down = false;
    const PadState* pad = nullptr;   // nulo: sem controle
    float pad_deadzone = 0.0f;       // zona morta das metades dos analógicos (0 a 1)
};
// Valor de 0 a 1 da entrada (teclas e botões 0 ou 1; gatilhos e analógicos proporcionais).
float input_value(InputCode c, const InputFrame& f);
// Soma nas ações os comandos com entradas da classe pedida (teclado/mouse ou controle), com o sinal de
// cada alvo, limitada a [-1, 1]. Teclado: andar e andar de lado juntos valem 1/sqrt(2) cada, como o
// analógico na diagonal. As ações só do port vão para port_actions (kPortActionCount; nulo: ficam de fora).
void bindings_to_actions(const Bindings& b, const InputFrame& f, bool pad, float actions[40],
                         float* port_actions = nullptr);
// Bits (1 << kPadIn*) das entradas do controle usadas na tabela (fora as dos comandos sem ação do jogo).
uint32_t bindings_pad_mask(const Bindings& b);
// O valor (0 a 1) de um comando: a maior das entradas dele (which: -1 todas, 0 só teclado/mouse, 1 só o
// controle).
float command_value(const Bindings& b, int command, const InputFrame& f, int which = -1);

// ---- [bindings] ----
std::string format_inputs(const InputCode* list, int n);
int parse_inputs(const std::string& text, InputCode* out, int max);  // devolve quantas
// Comandos ausentes ficam no padrão. As entradas de um comando escrito no arquivo valem mais que as padrão: um
// comando ausente perde a entrada que um comando escrito do mesmo grupo usa (um .ini antigo com `fire_mode = r`
// continua com R no modo de tiro, e o "reload" novo fica sem R), a não ser que um obrigatório ficasse sem tecla.
void load_bindings(const Settings& s, Bindings* b);
void save_bindings(Settings& s, const Bindings& b);

// ---- [controls] ----
// As opções de controle do PC. A sensibilidade do mouse é psiMouseSensitivity (platform/psi_input.h,
// `mouse_sensitivity`, também --sensitivity); as três últimas são do jogo (PlayerSetting[0]): psiInput_Init
// as copia para lá e psiInput_MapInputs grava quando a pausa as muda.
struct ControlsConfig {
    Bindings bindings = default_bindings();
    float mouse_aim_scale = 1.0f;  // multiplica o mouse mirando (que também divide pelo zoom)
    bool mouse_invert_y = false;
    float pad_sensitivity = 1.0f;  // multiplica o analógico de olhar antes das curvas do jogo
    float pad_deadzone = 40.0f / 127.0f;  // a de psiInput_PollDevices: 40 de 127 no PS2
    bool pad_triggers = true;      // RT/LT fazem R1/L1 (atirar e mirar no estilo 0); falso: RB/LB
    bool pad_invert_y = false;     // PlayerSetting::invert_look
    bool vibration = true;         // PlayerSetting::unknown_09
    uint16_t pad_style = 0;        // PlayerSetting::style (0 NightFire ... 7 Classic Bond)
};
ControlsConfig& controls();
// A sensibilidade do mouse padrão (radianos por contagem; o --sensitivity do main).
constexpr float kDefaultMouseSensitivity = 0.0022f;
// A barra da sensibilidade do mouse (as duas páginas de opções): logarítmica, kMouseSensStepsPerOctave passos por
// dobro (9% cada), de kMouseSensPosMin a kMouseSensPosMax, com o padrão na posição kMouseSensDefaultPos: de
// 0,0000486 a 0,0088 rad por contagem, 410 a 2,3 cm por volta a 800 DPI (com 400 a 3200 DPI cabem as distâncias de
// 5 a 100 cm). O valor mostrado é x 1000 (o padrão: 2,20).
constexpr int kMouseSensPosMin = 0;
constexpr int kMouseSensPosMax = 60;
constexpr int kMouseSensDefaultPos = 44;
constexpr int kMouseSensStepsPerOctave = 8;
float mouse_sens_from_pos(int pos);
int mouse_sens_to_pos(float sens);
// Centímetros por volta completa (360 graus) do mouse com `dpi` pontos por polegada.
float mouse_cm_per_360(float sens, float dpi);
// Lê/grava [bindings] e [controls] (sem mouse_sensitivity, que o main lê em apply_settings). save_controls só grava
// as opções do PC e só as que o arquivo já tem ou que não estão no padrão (o mesmo para cada comando de
// [bindings]); as três do jogo (vibration, pad_style, pad_invert_y) são gravadas por save_game_options quando o
// .ini as conhece (platform/game_options.h). load_controls lê as três para o ControlsConfig.
void load_controls(const Settings& s, ControlsConfig* c);
void save_controls(Settings& s, const ControlsConfig& c, float mouse_sensitivity);
// Grava nightfire.ini com as opções atuais (também as do jogo que o .ini lembra, platform/game_options.h), se
// g_controls_persist (o main liga fora das capturas ou com --config).
extern bool g_controls_persist;
void persist_controls();

// Nomes dos oito estilos de botões (os do menu do jogo).
extern const char* const kPadStyleNames[8];

}  // namespace nf
