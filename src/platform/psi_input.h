#pragma once
// Entrada da plataforma (psiInput_*): no PS2 lê os controles (libpad, multitap) e preenche as
// ações de cada jogador conforme a configuração de botões. No PC o jogador 0 tem o teclado e o mouse
// (pela tabela de teclas, platform/input_bindings.h) e o controle do XInput (pelo controle do PS2 e os
// estilos do jogo, platform/psi_pad.h); os outros jogadores, o segundo controle.
//
// O laço principal entrega o estado do teclado e do mouse a cada quadro de vídeo
// (g_psi_keyboard_mouse); Input_Update (a cada tick de lógica) chama psiInput_PollDevices e
// psiInput_MapInputs, que transformam esse estado e o do controle nas ações.
//
// Teclado e controle juntos (jogador 0): os botões somam (vale o maior), os valores analógicos (andar,
// virar, olhar, ações 0 a 5) são os do último aparelho usado. Se o controle estava em uso e some, o
// jogador fica "desconectado" (PlayerInput_tag::connected = 0) até o controle voltar ou o teclado/mouse
// ser usado: o MenuManager_Monitor do jogo mostra o aviso de controle desconectado.
//
// Esc (no padrão, a pausa e o voltar dos menus: nf::pause_menu_shared): com um menu do jogo aberto, as entradas do
// "pause" que também são de um comando do menu valem só no menu; fechado o menu, a que ainda estiver apertada fica
// fora do "pause" até ser solta.
//
// Só no PC: o mouse gira a visão diretamente (psiInput_TakeMouseLook), em vez de imitar o
// analógico, que no jogo passa por curvas de aceleração feitas para um polegar.

#include <cstdint>

struct PlayerInput_tag;

namespace nf {
struct InputCode;
struct PadState;
}

struct PsiKeyboardMouse {
    bool down[256];  // teclas e botões do mouse pelo código do port (platform/keys.h: os números dos VK_ do Windows)
    float look_dx;   // movimento do mouse acumulado desde o último tick, em contagens
    float look_dy;
    float wheel;     // roda acumulada desde o último tick, em dentes (positivo: para cima)
    bool active;     // false: o jogo não recebe nada (câmera livre, mouse solto)
    // O cursor do sistema para os menus (psiInput_MenuMouse): a posição na área de desenho, em pixels, e o
    // tamanho dela (o 2D vai para a área 4:3 de psi_2d_viewport).
    bool cursor_inside;
    float cursor_x;
    float cursor_y;
    int view_width;
    int view_height;
};
extern PsiKeyboardMouse g_psi_keyboard_mouse;
extern float psiMouseSensitivity;  // radianos por contagem do mouse

// Só no PC: o mouse nos menus (engine/menu_mouse.h), um estado por tick de lógica que psiInput_PollDevices
// calcula a partir de g_psi_keyboard_mouse. Com um menu aberto, os botões do mouse e a roda não vão para a
// tabela de teclas (só para o menu); quando o menu fecha, os botões ainda segurados ficam de fora até serem
// soltos (o clique em CONTINUE não vira um tiro).
struct PsiMenuMouse {
    bool inside = false;   // o cursor está na área de desenho
    float x = 0.0f;        // na tela do PS2 (512 x 448, como os sprites)
    float y = 0.0f;
    bool moved = false;    // andou desde o tick anterior
    bool held = false;     // botão esquerdo segurado
    bool pressed = false;  // botão esquerdo apertado neste tick
    bool back = false;     // botão direito ou Mouse4 apertado neste tick
    int wheel = 0;         // dentes da roda neste tick (positivo: para cima)
    bool active = false;   // o mouse é o aparelho do menu: mexeu ou clicou depois da última tecla ou botão do controle
};
const PsiMenuMouse& psiInput_MenuMouse();

void psiInput_Init();
void psiInput_PollDevices();
void psiInput_MapInputs(PlayerInput_tag* players, uint16_t count);
// Giro pedido pelo mouse neste tick, em radianos (yaw positivo para a esquerda, pitch positivo para
// cima), e zera. Só o jogador 0 tem mouse. Mirando, a sensibilidade vezes mouse_aim_sensitivity e
// dividida pelo zoom (como o analógico em Player_Aiming).
void psiInput_TakeMouseLook(int player, float* yaw, float* pitch, bool aiming = false, float zoom = 1.0f);
// Só no PC (o olhar do mouse sem o atraso da interpolação entre ticks, main.cpp): o giro que o mouse pediria pelo
// movimento que chegou depois do último tick (g_psi_keyboard_mouse, ainda não lido por psiInput_PollDevices), com as
// contas de psiInput_TakeMouseLook, sem consumir nada.
void psiInput_PeekMouseLook(int player, float* yaw, float* pitch, bool aiming = false, float zoom = 1.0f);

// ---- só no PC ----
// As ações só do port do jogador 0 (platform/input_bindings.h, PortAction: recarregar, os espaços dos aparelhos):
// se o comando acabou de ser apertado neste tick (o teclado/mouse e o controle da tabela de teclas; nada com a
// entrada parada, como as ações do jogo). Os outros jogadores não têm.
bool psiInput_PortPressed(int player, int action);
// A roda do mouse mirando com zoom (só no PC): o jogador (Player_Zoom) diz a cada tick se a roda dá zoom (mirando
// com uma arma de zoom); no tick seguinte os dentes da roda não vão para a tabela de teclas (nada de trocar de arma)
// e psiInput_TakeWheelZoom os devolve, somados (positivo: para cima, aproximar), e zera. Sem o pedido de um tick, a
// roda volta à tabela.
void psiInput_SetWheelZoom(int player, bool on);
// A roda de armas aberta (engine/weapon_wheel.h; o main.cpp a liga e desliga a cada quadro de vídeo): o jogador 0 não
// recebe girar, olhar, mirar, atirar, usar, recarregar, os aparelhos, o modo de tiro, a visão nem a troca de arma, e o
// mouse não gira a visão (o main dá o movimento à roda); andar, pular e agachar continuam. Ao fechar, um botão do mouse
// ainda apertado fica fora até ser solto.
void psiInput_SetWheelOpen(bool on);
bool psiInput_WheelOpen();
float psiInput_TakeWheelZoom(int player);
// O aparelho dos valores analógicos do jogador 0: 0 teclado e mouse, 1 o controle.
int psiInput_ActiveDevice();
// O controle do XInput do jogador 0 (-1: nenhum).
int psiInput_PadIndex();

// Captura de uma entrada para a página de teclas: espera tudo ser solto e devolve a primeira tecla,
// botão do mouse, dente da roda ou entrada do controle apertada depois. Enquanto captura (e até a entrada
// capturada ser solta), o jogador 0 não recebe ações. Esc cancela; Delete pede para limpar.
enum class PsiCapture : uint8_t { Idle, Waiting, Got, Cancelled, Clear };
void psiInput_BeginCapture();
void psiInput_EndCapture();
PsiCapture psiInput_CaptureState();
// A entrada capturada (com o estado Got).
bool psiInput_CaptureResult(nf::InputCode* out);

// Um menu do port por cima do jogo (o estilo Modern, src/ui): enquanto ligado, o jogador 0 não recebe ações
// (nem os menus do jogo, nem o mouse deles: o menu do port lê o teclado, o mouse e o controle sozinho); ao
// desligar, o que ainda estiver apertado fica de fora até ser solto (a tecla que fechou o menu não chega à
// pausa). A captura de tecla continua funcionando por baixo.
void psiInput_SetModal(bool on);
bool psiInput_Modal();
// O controle do XInput do jogador 0 lido no último tick (nulo sem controle).
const nf::PadState* psiInput_PlayerPad();
