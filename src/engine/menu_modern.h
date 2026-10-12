#pragma once
// Só do port: a ponte entre os menus no estilo Modern (src/ui: a pausa, o fim de missão, o resultado, o menu principal e
// o aviso de controle desconectado; docs/design.md) e os menus do jogo (engine/menu*.cpp). Ver docs/port.md, "Menus no
// estilo Modern".
//
// O jogo continua dono dos menus: ele os abre (Player_Update a pausa, Mission_Update o fim de missão, o menu
// principal o resultado), os tratadores dele (P_PAUSE_Handler, C_GCPAUSE_Handler, P_ENDMISSION_Handler,
// P_NFRESULTS_Handler, C_LBMSGOPTIONS_Handler...) decidem o que cada escolha faz, e o estilo Modern só troca o
// desenho (o 2D do menu do jogo some; ver render/ui_modern_app.h). Os dois lados da ponte:
//
// - Leitura (MenuModern_Snapshot): o que o menu aberto do gerente 0 mostra, tirado dos controles dele e das tabelas
//   do jogo, em UTF-8: as linhas das listas (os textos com as letras espaçadas do arquivo, "C O N T I N U E", viram
//   "CONTINUE"), os rótulos, a confirmação aberta (a lista tag 0xd da pausa ou a caixa de mensagem 0x4000002f), os
//   objetivos da missão (Mission_*: os visíveis na ordem de Mission_ObjectiveState e os ainda escondidos), o mapa de
//   botões da aba CONTROLS (as células "~A Crouch" da lista tag 10, que Menu_DisplayControllerStyleList monta), o
//   nome e a descrição da missão (sp_level), o resultado e as estatísticas.
// - Escrita (MenuModern_Request): cada pedido vira o que um controle do jogo faria, no começo do tick do gerente
//   (o gancho Menu_SetPortTick, logo depois do mouse, como o mouse dos menus): o foco numa aba da pausa (o cursor
//   do gerente vai ao botão e Page_Update manda 0x4f/0x4e como o direcional), uma linha de uma lista (a mensagem
//   0x1d, como cima/baixo: o evento 0x49 vai ao dono) e um botão do controle do PS2 apertado por um tick
//   (Input_SetAction: Start 0x1e, X/escolher 0x1f, quadrado 0x20, círculo 0x21, triângulo/voltar 0x22), que os
//   updates dos controles leem como um botão de verdade (o som, o 0x4b/0x5d/0x5e ao dono, o 0x5f do gerente e o
//   MenuManager_Monitor fechando a pausa com o Start). Um foco ou um botão encerra o tick: o pedido seguinte espera
//   o efeito dele (o 0x4e mostra a lista da aba antes de ela receber o botão).
//
// Nenhum caso precisou chamar as funções dos tratadores direto: tudo passa pelos eventos que os controles do jogo
// produzem.
//
// O menu principal (07000048, status 5; src/ui/modern_front.h): a leitura também tira as páginas do caminho de um
// jogador (MmFront: "Press START" com o texto legal, o menu principal, as rodas dos codinomes, da dificuldade, das
// missões e do menu do codinome com os itens das tabelas do jogo, o teclado da tela com o nome em codename_entry) e
// a escrita ganha dois pedidos: Focus (o cursor do gerente no meio de um controle, como Tab, para qualquer id: os
// botões do menu principal e as teclas do teclado da tela) e Step (a roda anda um passo por vez até a posição, cada
// passo como o do direcional no Scroll_Update: a posição, o som e o 0x49 ao dono, que refaz os rótulos). O aviso de
// controle desconectado (status 2, a caixa 0x4000002f) é a tela Warning, só desenho: o jogo cuida da volta.
//
// O dossiê e os créditos também são vistas do menu principal. O dossiê (MmDossier): a página da roda das seções
// (0x4000002b) e as quatro seções, com o que os tratadores do jogo escreveram (os recordes, as recompensas mostradas, o
// texto do item com a melhoria); as missões dos recordes e das recompensas são o rádio do jogo, que Step anda como o
// direcional (o 0x49 do rádio refaz a página). Os créditos (MmCredits): as linhas que o P_CREDITS_Handler pôs nos 26
// pares de rótulos, com a posição e a fonte de cada uma; Show mostra a página como o script do "Credits" da AV Options.
// A aba SCORE da pausa vale também com um jogador: as linhas da lista tag 8 que o C_GCPAUSE_Handler monta com o
// PlrStat_GetScore.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct M_MANAGER;

// Os ids dos menus do jogo que os menus Modern desenham.
constexpr uint32_t kMmPausePage = 0x4000004b;     // a pausa (menu 0x80000002, status 3)
constexpr uint32_t kMmPauseControls = 0x10000028; // os controles da pausa (a tag distingue)
constexpr uint32_t kMmFailPage = 0x40000042;      // o fim de missão (menu 0x80000004, status 4)
constexpr uint32_t kMmFailList = 0x100001a4;      // tentar de novo (0), recomeçar a missão (1), sair (2)
constexpr uint32_t kMmResultsPage = 0x40000036;   // o resultado (menu principal, status 5)
constexpr uint32_t kMmStatsPage = 0x40000037;
constexpr uint32_t kMmBonusPage = 0x40000038;
constexpr uint32_t kMmBoxPage = 0x4000002f;       // a caixa de mensagem (Menu_CreateOptionBox)
constexpr uint32_t kMmBoxList = 0x10000120;       // as respostas da caixa (C_LBMSGOPTIONS_Handler)
constexpr uint32_t kMmCardPage = 0x4000002a;      // a caixa do cartão de memória (PS2_DoCardFlow, menu principal)
constexpr uint32_t kMmCardList = 0x100000fc;      // as respostas do cartão (C_LBERROPTIONS_Handler)
constexpr uint32_t kMmAnyTag = 0xffffffffu;      // Pick: o primeiro controle do id, qualquer tag
// As tags dos controles 0x10000028 da pausa.
constexpr uint32_t kMmTabMission = 0, kMmTabObjectives = 1, kMmTabControls = 3, kMmTabScore = 4;
constexpr uint32_t kMmTagMain = 6;      // continuar, recomeçar, sair (e OPTIONS, valor 3, do port)
constexpr uint32_t kMmTagObjectives = 8;
constexpr uint32_t kMmTagStyles = 9;
constexpr uint32_t kMmTagButtons = 10;
constexpr uint32_t kMmTagQuestion = 0xc;  // "RESTART: ARE YOU SURE?"
constexpr uint32_t kMmTagConfirm = 0xd;   // sim, não
constexpr uint32_t kMmTagAxis = 0xe;      // "Normal" / "Inverted"
// Os botões do controle do PS2 (ações do menu).
constexpr uint32_t kMmStart = 0x1e, kMmSelect = 0x1f, kMmSquare = 0x20, kMmCircle = 0x21, kMmBack = 0x22;
// O menu principal (08000002): as páginas do caminho de um jogador e os controles que a ponte lê ou mexe.
constexpr uint32_t kMmFeLanguage = 0x40000034, kMmFeCardInit = 0x40000048, kMmFeIntro = 0x40000032;
constexpr uint32_t kMmFeStart = 0x40000009, kMmFeAttract = 0x40000035, kMmFeEnum = 0x4000004a, kMmFeHero = 0x40000043;
constexpr uint32_t kMmFeMain = 0x40000002;
constexpr uint32_t kMmFeCampaign = 0x40000025;   // a roda de codinomes da campanha (P_NFSELECT)
constexpr uint32_t kMmFeCodenames = 0x4000001b;  // os codinomes (P_CNSELECT)
constexpr uint32_t kMmFeName = 0x40000020;       // o teclado da tela (P_CNNAME)
constexpr uint32_t kMmFeCnMenu = 0x4000001d;     // o menu do codinome (P_CNMENU)
constexpr uint32_t kMmFeDifficulty = 0x40000023; // P_NFDFCTY
constexpr uint32_t kMmFeMap = 0x4000001c;        // o mapa das missões (P_NFMAP)
constexpr uint32_t kMmFeControls = 0x40000022, kMmFeAdvanced = 0x4000002d, kMmFeAv = 0x40000031;  // opções do codinome
constexpr uint32_t kMmFeMainButtons[3] = {0x10000002, 0x10000003, 0x10000004};  // NightFire, Multiplayer, Codenames
constexpr uint32_t kMmFeStartLabel = 0x10000196;  // o rótulo de tela cheia do "Press START" (escolher: 0x4000004a)
constexpr uint32_t kMmFeKeys = 0x10000074;        // as teclas do teclado da tela (a tag diz qual)
constexpr uint32_t kMmFeKeyDel = 1000, kMmFeKeySpace = 0x3e9, kMmFeKeyEnd = 0x3ea;
constexpr uint32_t kMmFeWheelCampaign = 0x100000e5, kMmFeWheelCodenames = 0x100000eb, kMmFeWheelCnMenu = 0x10000100;
constexpr uint32_t kMmFeWheelDifficulty = 0x100000bf, kMmFeWheelMap = 0x1000000e;
// O dossiê (engine/menu_dossier.h): a página da roda das quatro seções e as quatro seções.
constexpr uint32_t kMmFeDossier = 0x4000002b;  // P_DOSSIER (a roda C_SBDOSSIER sobre ds_options)
constexpr uint32_t kMmFeRecords = 0x4000003a, kMmFeRewards = 0x4000003b, kMmFeGadgets = 0x4000003c, kMmFeWeapons = 0x4000003d;
constexpr uint32_t kMmDsWheel = 0x1000010c;                                 // a roda do dossiê (0 a 3)
constexpr uint32_t kMmDsRecordsRadio = 0x10000174, kMmDsRewardsRadio = 0x1000017a;  // as missões (rádios)
constexpr uint32_t kMmDsGadgetWheel = 0x1000016f, kMmDsWeaponWheel = 0x10000173;
// Os créditos (P_CREDITS: da AV Options, depois do fim do jogo e, no Modern, da linha CREDITS do menu principal).
constexpr uint32_t kMmFeCredits = 0x40000030;
constexpr uint32_t kMmCreditNames = 0x10000213, kMmCreditTitles = 0x10000214, kMmCreditFade = 0x10000215;

// A tela Modern que o menu aberto pede.
enum class MmScreen : uint8_t { None, Pause, Fail, Results, Front, Warning };

// A vista do menu principal (MmScreen::Front) pela página do jogo; None nas que ficam no estilo Original (as opções do
// multijogador, o ajuste de tela, o controle das missões de carro).
enum class MmFrontView : uint8_t {
    None,
    Title,         // idioma, cartão na partida, filmes de abertura, "Press START", a procura do perfil, o filme do herói
    Main,          // NightFire / Multiplayer / Codenames
    Campaign,      // a roda de codinomes da campanha
    Codenames,     // os codinomes (editar, criar, apagar)
    Name,          // o teclado da tela
    CodenameMenu,  // o menu do codinome
    Difficulty,
    Map,
    Options,       // uma página de opções do codinome do jogo (Controller Setup, Advanced, AV): as opções Modern
    Dossier,       // o dossiê: a página da roda e as quatro seções (MmDossier)
    Credits,       // os créditos (MmCredits)
};
MmFrontView MenuModern_FrontView(uint32_t page);

struct MmItem {
    std::string text;  // UTF-8
    int32_t data = 0;  // o valor da linha (LIST_ROW::data)
};

// O estado de um objetivo para os menus (OBJECTIVE_tag::state): 0 e 1 escondido (a fase dele não chegou, ou
// espera o canal que o revela), 2 e 3 em andamento ("In Progress" com a flag 2, "Incomplete"), 4 cumprido, 5
// fracassado.
enum class MmObjState : uint8_t { Hidden, Active, Done, Failed };
MmObjState MenuModern_ObjectiveState(int32_t state);

struct MmObjective {
    MmObjState state = MmObjState::Hidden;
    int32_t raw = 0;          // OBJECTIVE_tag::state
    bool guard = false;       // flag 2: "não deixe acontecer"
    std::string label;        // o texto do objetivo (vazio nos escondidos)
    std::string text;         // a descrição (a dica da pausa)
    std::string status;       // "In Progress", "Incomplete", "Complete", "Fail" (o rótulo do jogo; vazio nos escondidos)
};

// Uma célula do mapa de botões: o símbolo da fonte ('A' cruz, 'B' triângulo, 'Y' quadrado, 'X' círculo, 'C' L2,
// 'D' R2, 'L' L1, 'R' R1, 'F' analógico esquerdo, 'E' direito, 'H' e 'V' o direcional, 'T' Select) e o que ele faz.
struct MmButton {
    char symbol = 0;
    std::string label;
};

// Uma confirmação aberta: a lista tag 0xd da pausa (com a pergunta da tag 0xc) ou a caixa de mensagem do jogo.
struct MmDialog {
    bool open = false;
    std::string title;
    std::string body;
    std::vector<MmItem> options;
    int focus = 0;               // a linha escolhida da lista
    uint32_t list = 0, tag = 0;  // a lista das respostas (para o pedido Pick)
    bool box = false;            // a caixa de mensagem (página 0x4000002f)
};

// Um item de uma roda do menu principal (M_ITEM das tabelas do jogo, ou um nome de codename_buf).
struct MmWheelItem {
    std::string name;        // UTF-8
    std::string text;        // a descrição (a de bloqueado quando ele está bloqueado)
    bool enabled = true;     // liberado (ou tudo liberado pela trapaça, menu_unlock_everything)
    uint32_t value = 0;      // a fase, a dificuldade, a página
    uint32_t image = 0;      // a textura 03xxxxxx do item (0 sem)
    // O mapa das missões: missão de carro (DRIVING.ELF, pulada no port), a melhor medalha do perfil (0 nenhuma, 1
    // bronze, 2 prata, 3 ouro, 4 platina: cada uma só conta com as de baixo, como o dossiê) e o recorde.
    bool driving = false;
    int medal = 0;
    std::string medal_label;
    uint32_t best = 0;
};

// Uma tecla do teclado da tela (os botões 0x10000074): a tag (0-9 algarismos, 10-37 letras, 1000 Del, 0x3e9 espaço,
// 0x3ea End), o texto da tecla e o retângulo no arquivo (para a ordem da grade).
struct MmKey {
    uint32_t tag = 0;
    std::string text;
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// Uma recompensa de uma missão nas recompensas do dossiê (C_RBDSREWARDS_Handler: a imagem 0x100001d6+i e o texto
// 0x100001d2+i, escondidos até a medalha i e todas as de baixo).
struct MmReward {
    bool shown = false;
    uint32_t image = 0;     // a textura que o jogo pôs no rótulo (0 escondida)
    std::string text;       // o texto do jogo ("MP Skin - Oddjob")
    std::string medal;      // a medalha que a libera (os rótulos do jogo: "BRONZE MEDAL"...)
};

// O dossiê (MmFrontView::Dossier; engine/menu_dossier.h). As missões dos recordes e das recompensas (o rádio do jogo) e
// os itens dos aparelhos e das armas (a roda do jogo) vão em MmFront::items, com a posição em MmFront::pos e o controle
// em MmFront::wheel; aqui fica o resto.
struct MmDossier {
    // -1 a página da roda (0x4000002b), 0 os recordes, 1 as recompensas, 2 os aparelhos, 3 as armas.
    int section = -1;
    // As quatro seções (ds_options: o nome e o texto da descrição de cada uma) e a posição da roda do dossiê.
    std::vector<MmWheelItem> sections;
    int section_pos = 0;
    // Recordes: os rótulos e os valores que o C_RBDSRECORDS_Handler escreveu, a melhor medalha e a imagem dela.
    std::string score_label, score, moments_label, moments, upgrades_label, upgrades;
    std::string medal;
    uint32_t medal_image = 0;
    // Recompensas: as quatro da missão (bronze, prata, ouro, platina).
    MmReward rewards[4];
    // Aparelhos e armas: o texto do jogo do item da roda (o memo, com a linha da melhoria) e o nível de melhoria do
    // grupo de cada item (Menu_GetObjectUpgradeLevel).
    std::string memo;
    std::vector<uint32_t> upgrade;
};

// Uma linha visível dos créditos: um rótulo 0x10000213 (o nome, ou a linha larga) ou 0x10000214 (a coluna da direita),
// na posição que o P_CREDITS_Handler deu (tela do PS2) e com a fonte do formato dele: 1 os títulos grandes (as empresas),
// 3 os cargos, 2 os nomes e o texto legal (credit_format).
struct MmCreditLine {
    std::string text;
    int16_t x = 0, y = 0, w = 0;  // na tela do PS2 (512 x 448; a página + o rótulo)
    uint8_t font = 2;
    bool right = false;           // a coluna da direita (0x10000214)
    bool wide = false;            // a linha larga (uma só, no meio)
};
struct MmCredits {
    std::vector<MmCreditLine> lines;
    float fade = 0.0f;  // o escurecer do fim (o alfa do rótulo 0x10000215, 0 a 1)
};

// O que o menu principal mostra (MmScreen::Front).
struct MmFront {
    MmFrontView view = MmFrontView::None;
    std::string title;   // o título da página ("Select Codename", "Edit BOND", "Enter New Codename"...)
    std::string help;    // a barra de ajuda da página ("~A Select  ~Y Create...")
    std::string desc;    // o memo da descrição da página
    std::string profile; // o codinome em uso (ls.name) e o perfil (0/1 os padrões, 2+ do cartão, 999 nenhum)
    uint32_t slot = 999;
    // "Press START": o convite (vazio enquanto o texto legal espera) e o texto legal.
    std::string press, legal;
    // O menu principal: os três botões (o texto e o de ajuda de cada um, help_table) e o que tem o foco.
    std::vector<std::string> buttons, button_help;
    int focus = -1;
    // As rodas: a barra (id, posição e limites) e os itens.
    uint32_t wheel = 0;
    int pos = 0, min = 0, max = 0;
    std::vector<MmWheelItem> items;
    bool listing = false;  // a lista de codinomes ainda está sendo lida do cartão
    // O teclado da tela: o nome (codename_entry), o sugerido que a primeira tecla apaga, o modo de código (Secret
    // Unlocks) e as teclas.
    std::string entry;
    bool suggested = false;
    bool cheat = false;
    int max_len = 8;
    std::vector<MmKey> keys;
    // Opções: a página de opções do codinome do jogo aberta (Controller Setup, Advanced, AV).
    uint32_t option_page = 0;
    MmDossier dossier;
    MmCredits credits;
};

struct MmSnapshot {
    MmScreen screen = MmScreen::None;
    uint32_t status = 0;      // MenuManager_GetStatus
    uint32_t page = 0;        // a página atual do gerente 0
    uint32_t base_page = 0;   // a de baixo quando a caixa de mensagem está por cima
    bool ready = false;       // o gerente recebe entrada (sem congelar)
    // A página de agora já lê os botões: o gerente sem congelar, fora de uma transição de página e passado o
    // input_delay dela (o dossiê só manda o próximo pedido de página assim, para o botão não se perder).
    bool accepts = false;
    bool multiplayer = false;
    MmDialog dialog;
    // ---- pausa ----
    int tab = -1;               // a tag do botão de aba com o foco (0, 1, 3, 4), -1 nenhum
    std::string tab_text[5];    // os textos dos botões das abas, pela tag ("MISSION", "OBJECTIVES"...)
    std::vector<MmItem> rows;   // a lista principal (tag 6)
    int row = 0;
    // A missão (MissionData, sp_level): nome e descrição (vazios fora do menu), a fase dentro dela, a dificuldade.
    std::string mission, mission_text, difficulty, current_label;
    int level = 0, levels = 0;
    std::vector<MmObjective> objectives;  // os visíveis (ordem do jogo) e depois os escondidos
    int done = 0, failed = 0, visible = 0;
    // Controles: os estilos (lista tag 9), o escolhido, o eixo Y (o rótulo "Y-Axis" e o valor da tag 0xe).
    std::vector<std::string> styles;
    int style = 0;
    bool inverted = false;
    std::string axis_label, axis_value;
    std::vector<MmButton> buttons;  // a lista tag 10, célula por célula
    // A aba SCORE (a lista tag 8 com o foco no botão dela): no multijogador o placar; com um jogador as estatísticas da
    // missão (a primeira linha é o cabeçalho Category / Current / Target, as outras as do PlrStat_GetScore).
    std::vector<std::vector<std::string>> score;
    // ---- fim de missão ----
    std::string fail_title, fail_reason;
    bool died = false;  // a missão fracassou pela morte (estado 3): o HUD mostra a cortina de sangue
    std::vector<MmItem> fail_rows;
    int fail_row = 0;
    // ---- resultado ----
    std::string complete_title;               // "Mission Complete"
    std::string results_title;                // "Results"
    std::string score_label, score_value;     // "Total Score:" e o valor
    std::string target_label, target_value;   // "Gold Medal Target:" e o valor
    std::string medal;                        // o texto da medalha (o memo 0x100001d1)
    std::string help;                         // a barra de ajuda da página ("~A Continue  ~X Stats  ~Y Dossier")
    std::vector<std::string> stats_head;      // Category, Current, Target, TOTAL
    std::vector<std::vector<std::string>> stats;  // as linhas da lista 0x10000022
    std::vector<std::string> stats_names;     // as categorias do jogo (para quando a lista está vazia)
    std::string bonus;                        // o memo do bônus (0x100001d0)
    // ---- menu principal ----
    MmFront front;
};

MmSnapshot MenuModern_Snapshot();

enum class MmAction : uint8_t {
    Tab,    // a = a tag do botão de aba da pausa
    Pick,   // a = id, b = tag, c = a linha (índice): a linha escolhida da lista (sem o botão)
    Press,  // a = a ação do menu (kMmStart, kMmSelect, kMmSquare, kMmCircle, kMmBack)
    Focus,  // a = id, b = tag (kMmAnyTag: o primeiro): o cursor do gerente no meio do controle (o foco vai a ele)
    Step,   // a = id da barra (uma roda) ou de um rádio, c = a posição: um passo de cada vez até ela, como o direcional
    Show,   // a = a página: a mensagem 0x44 ao gerente, a que o script de um controle manda para trocar a página (o
            // "Credits" da AV Options do jogo mostra os créditos assim); a página de agora vai à pilha, voltar volta a ela
};
struct MmRequest {
    MmAction kind = MmAction::Press;
    uint32_t a = 0, b = 0;
    int32_t c = 0;
    friend bool operator==(const MmRequest&, const MmRequest&) = default;
};
// Na fila; o gancho do tick do gerente executa (MenuModern_Install).
void MenuModern_Request(const MmRequest& r);
inline void MenuModern_Tab(uint32_t tag) { MenuModern_Request({MmAction::Tab, tag, 0, 0}); }
inline void MenuModern_Pick(uint32_t id, uint32_t tag, int32_t row) { MenuModern_Request({MmAction::Pick, id, tag, row}); }
inline void MenuModern_Press(uint32_t action) { MenuModern_Request({MmAction::Press, action, 0, 0}); }
inline void MenuModern_Focus(uint32_t id, uint32_t tag) { MenuModern_Request({MmAction::Focus, id, tag, 0}); }
inline void MenuModern_Step(uint32_t bar, int32_t pos) { MenuModern_Request({MmAction::Step, bar, 0, pos}); }
inline void MenuModern_Show(uint32_t page) { MenuModern_Request({MmAction::Show, page, 0, 0}); }
// Uma tecla do teclado da tela: o foco nela e o X (C_KEYBOARD_Handler, 0x4b).
inline void MenuModern_Key(uint32_t tag) {
    MenuModern_Focus(kMmFeKeys, tag);
    MenuModern_Press(kMmSelect);
}
// A tecla da tela que digita `c` (maiúscula; o texto da tecla), ou 0xffffffff se não houver.
uint32_t MenuModern_KeyFor(const std::vector<MmKey>& keys, char c);
// As opções do codinome mudadas pelas opções Modern (o menu do codinome abre as opções Modern no lugar das páginas
// do jogo): Begin guarda o que essas páginas gravam (PlayerSetting[0], DrawInfo, os volumes e o modo do som); End
// compara e, se algo mudou, marca o codinome como mudado (cn_modified_flag = 1, como o "escolher" dessas páginas), e
// devolve se mudou.
void MenuModern_CodenameOptionsBegin();
bool MenuModern_CodenameOptionsEnd();
// Escolher a linha `row` da lista: a linha e o X.
inline void MenuModern_Select(uint32_t id, uint32_t tag, int32_t row) {
    MenuModern_Pick(id, tag, row);
    MenuModern_Press(kMmSelect);
}
const std::vector<MmRequest>& MenuModern_Queue();
// Há na fila um pedido desse tipo (o menu principal Modern espera os botões, não os passos e os focos).
bool MenuModern_Queued(MmAction kind);
void MenuModern_Clear();
// Põe o gancho do tick (Menu_SetPortTick). Os testes chamam depois de menu_debug_power_on (que o apaga).
void MenuModern_Install();
// O gancho: executa a fila no tick do gerente que recebe a entrada.
void MenuModern_Tick(M_MANAGER* m);

// Texto do jogo (Windows-1252, as letras espaçadas dos menus) em UTF-8; `collapse` junta "C O N T I N U E".
std::string MenuModern_Utf8(const char* text, bool collapse = false);

struct MenuModernStats {
    uint32_t requests = 0;  // pedidos executados
    uint32_t tabs = 0;
    uint32_t picks = 0;
    uint32_t presses = 0;
    uint32_t focuses = 0;   // Focus
    uint32_t steps = 0;     // passos das rodas (Step)
    uint32_t shows = 0;     // páginas mostradas (Show)
    uint32_t dropped = 0;   // pedidos sem o controle (a página mudou) ou com a fila limpa
};
extern MenuModernStats g_menu_modern_stats;
// "pedidos N abas N linhas N botoes N focos N passos N paginas N perdidos N fila N".
int MenuModern_Describe(char* out, size_t size);
void menu_modern_debug_power_on();
