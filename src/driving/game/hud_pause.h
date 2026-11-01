#pragma once
// A pausa do HUD das missões de carro (docs/driving.md seção 31): a fila de ações "HUDQ" do GHud (o GHud::Render a
// esvazia a cada quadro desenhado), o GHud::PauseOn/PauseOff com os eventos EPause/EResume, as mensagens do menu de
// pausa (PauseMenuMsgHandler: as páginas MISSÃO, OBJETIVOS, CONTROLES e PONTOS, as perguntas de recomeçar e de sair,
// o menu de fim de missão), as trapaças (CheatMsgHandler, CheckCheatInput: as sequências de botões com o jogo parado),
// o controle desligado (SetControllerUnplugged), o desenho do menu (DrawPauseMenu, DrawPauseErrors e as peças:
// DrawBlinkingArrow, o DrawFont de várias linhas, o DrawFontCentered, o WordWrap), a moça animada do fundo (GGirl: a
// silhueta em RLE do 'fi' da galeria cheia com a tabela de cores 0x0039d918 que corre, o mesmo truque do fundo da
// pausa a pé), o SMissionManager::TallyScores (a página dos pontos) e o InputConfigManager::GetLocaleID (os textos dos
// botões de cada configuração do controle).
//
// Portado do MIPS do DRIVING.ELF (GHud.obj, GFXGallery.obj, SMissionManager.obj, actionqueuemgr.obj) com o leiaute do
// PS2 (o GHud em GHudData, 0x500 bytes) e conferido na parte 27 do nf_driving_check (`--so som_hud`).
//
// Divergências (só do port):
//  - os eventos EPause e EResume (as tabelas virtuais 0x00370be8 e 0x00370900, sem número de 'el') entram na fila dos
//    eventos com os números do port kEventPausePort/kEventResumePort, na mesma ordem dos outros;
//  - o EResume do PS2 espera o controle ficar estável (PAD_isstable, com sceGsSyncV) antes de seguir: o port não espera;
//  - o RPlayerCamera::PauseOff (o SetCameraLookBack do modo de olhar para trás) e a entrada +0xbc da tabela virtual do
//    carro do jogador ficam com quem chama (HudPauseEnv), contados sem eles;
//  - o Bond_SetExitToDriving (as trapaças da snow2a_mis4 e o recomeço em cadeia da CHAIN_RESTART_MISSION, que Paris não
//    tem) fica anotado; o GGirl desenha numa imagem do port (ui_image) atualizada a cada quadro;
//  - um GetButton sem símbolo no texto da dica (o PS2 desenharia o sprite do ponteiro nulo) não desenha o ícone: contado.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace drv {

struct GHudData;
struct UberBondBlock;
struct SMissionManagerData;
struct ActionQueueData;
class InputConfigManager;
struct InputUberBond;
struct InputMap;
struct PendingEvent;

// Os números do port dos eventos EPause e EResume (fora dos 0..0x120 do 'el').
constexpr int32_t kEventPausePort = 0x1000;
constexpr int32_t kEventResumePort = 0x1001;

// O que a pausa usa de fora do GHud.
struct HudPauseEnv {
    InputConfigManager* icm = nullptr;  // InputConfigManager::Get
    InputUberBond* input_uber = nullptr;
    InputMap* input_map = nullptr;
    int32_t loading_stage = 0;          // GLoadingScreen::fLoadingStage (0: nada carregando)
    bool multitap = false;              // IOModule::MultitapConnected (sceMtapGetConnection)
    std::function<void()> camera_pause_off;  // RPlayerCamera::PauseOff da câmera do jogador
    std::function<void()> car_resume;        // a entrada +0xbc da tabela virtual do carro do jogador (o EResume)
    std::function<void(int32_t mission, const char* track)> exit_to_driving;  // Bond_SetExitToDriving(n, pista, 1)
    float* time_scale = nullptr;        // Scheduler::fgScheduler +0x24 (a trapaça da câmera lenta o volta a 1)
    bool accumulate_scores = false;     // AttributeSet::LookupInt(o mundo, "ACCUMULATE_SCORES")
};
HudPauseEnv& hud_pause_env();

// O que ficou de fora (para os relatórios e a conferência).
struct HudPauseStats {
    uint64_t pauses = 0, resumes = 0, actions = 0, skips = 0, cheats = 0, restarts = 0, quits = 0;
    uint64_t no_camera = 0, no_car = 0, exits_to_driving = 0, null_icon = 0, no_config = 0;
    uint64_t girl_frames = 0, girl_restarts = 0;
};
HudPauseStats& hud_pause_stats();

// A fila "HUDQ" (o GHud::GHud a cria; o ActionQueueManager::FeedQueue manda todas as ações a todas as filas).
ActionQueueData& hud_queue();
void hud_queue_feed(int32_t action, int32_t device, float value);
void hud_queue_reset();

// [driving] 0x0023cbc0 GHud::Render, a parte da fila (depois do tempo do quadro e do fim da missão): cada ação vai ao
// PauseMenuMsgHandler e ao CheatMsgHandler com o jogo parado, o Start (0x47) liga e desliga a pausa (ou pula a cena
// com o modo de cinema), o 0x4a (FRONTENDACTION_SKIPMOVIE) pula a cena, o 0x21-0x23 (a arma secundária) mostra a arma.
void GHud_RenderQueue(GHudData& h);
// [driving] 0x00241048 GHud::PauseOn / 0x002411a0 GHud::PauseOff
void GHud_PauseOn(GHudData& h);
void GHud_PauseOff(GHudData& h);
// [driving] 0x00241210 GHud::PauseMenuMsgHandler(int)
void GHud_PauseMenuMsgHandler(GHudData& h, int32_t action);
// [driving] 0x00241bb8 GHud::CheatMsgHandler(int)
void GHud_CheatMsgHandler(GHudData& h, int32_t action);
// [driving] 0x00241b38 GHud::CheckCheatInput(int *): a sequência (n, os n botões) no fim do registro +0x4ac..+0x4e8.
bool GHud_CheckCheatInput(GHudData& h, const int32_t* sequence);
// As nove sequências do CheatMsgHandler (0x0033ae58 ...): a contagem e os botões.
struct CheatSequence {
    uint32_t address;
    int32_t words[17];
};
const CheatSequence* hud_cheat_sequences(int32_t* count);
// [driving] 0x00241200 GHud::CanToggleInvert
bool GHud_CanToggleInvert(const GHudData& h);
// [driving] 0x0023cb58 GHud::SetControllerUnplugged(bool)
void GHud_SetControllerUnplugged(GHudData& h, int32_t unplugged);

// O que os eventos do port kEventPausePort/kEventResumePort fazem: [driving] 0x00153488 EPause::~EPause
// (Simulation::PauseSimState, SMissionManager::Stop, DAT_0032c460 = 0, ASoundManager::Pause) e 0x001568f0
// EResume::~EResume (UnPauseSimState, SMissionManager::Start se parado, ASoundManager::Resume, DAT_0032c460 = 1,
// RPlayerCamera::PauseOff, a entrada +0xbc do carro).
bool hud_pause_event_execute(const PendingEvent& e);
// [driving] 0x001fee28 Simulation::PauseSimState / 0x001fee48 UnPauseSimState
void Simulation_PauseSimState();
void Simulation_UnPauseSimState();
// O DAT_0032c460 (o jogo correndo: 0 na pausa).
int32_t& hud_game_running();

// ---- o desenho
// [driving] 0x0023cf88 GHud::Draw chama com o jogo parado: [driving] 0x002426a0 GHud::DrawPauseMenu e
// 0x00241f40 GHud::DrawPauseErrors.
void GHud_DrawPauseMenu(GHudData& h);
void GHud_DrawPauseErrors(GHudData& h);
// [driving] 0x002422b8 GHud::DrawBlinkingArrow(float x, float y)
void GHud_DrawBlinkingArrow(GHudData& h, float x, float y);
// [driving] 0x00244600 GHud::DrawFont(Keys *, x, y, cor, escala, char **linhas, int n)
void GHud_DrawFontLines(GHudData& h, float x, float y, float scale, uint32_t key, uint32_t colour,
                        const std::vector<std::string>& lines, int32_t n);
// [driving] 0x00244490 GHud::DrawFontCentered(Keys *, x0, y0, x1, y1, escala, cor, n, char **linhas)
void GHud_DrawFontCentered(GHudData& h, float x0, float y0, float x1, float y1, float scale, uint32_t key,
                           uint32_t colour, uint32_t n, const std::vector<std::string>& lines);
// [driving] 0x002424a0 WordWrap(char *, unsigned int, char **&): as linhas (no máximo 5) e a contagem.
int32_t WordWrap(const char* text, uint32_t max, std::vector<std::string>& lines);

// ---- o resto
// [driving] 0x00205c30 SMissionManager::TallyScores(UberBond &, bool)
void SMissionManager_TallyScores(SMissionManagerData& m, UberBondBlock& uber, bool final_tally);
// [driving] 0x00166dc0 InputConfigManager::GetLocaleID(tipo, configuração, índice): o número de texto de um botão
// (com as trocas de Paris, das pistas submarinas e da snow1a_mis3).
int32_t InputConfigManager_GetLocaleID(const InputConfigManager& m, int32_t type, int32_t config, int32_t index);
// [driving] 0x00166d78 InputConfigManager::IsInverted / 0x00166d60 SetInverted (também o UberBond +0xa24)
bool InputConfigManager_IsInverted(const InputConfigManager& m);
void InputConfigManager_SetInverted(InputConfigManager& m, bool inverted);
// [driving] 0x00249f98 GLocale::GetLanguage (o UberBond +0x988)
int32_t GLocale_GetLanguage();

// ---- a moça do fundo da pausa (GGirl, GFXGallery.obj)
struct GGirlState {
    const uint8_t* data = nullptr;  // 0x0033aaec o 'fi' da galeria (GET_SomeThing(0))
    int32_t init = 0;               // 0x0033aaf4
    int32_t unknown_aaf8 = 0, unknown_aafc = 0, unknown_ab00 = 0;
    int32_t fps = 0;                // 0x0033ab04 30
    float scroll_x = 0, scroll_y = 0;   // 0x0033ab08, 0x0033ab0c
    float speed_x = 0, speed_y = 1.0f;  // 0x0033ab10, 0x0033ab14 (escritos, não lidos)
    float offset_x = 0, offset_y = 0;   // 0x0033ab18, 0x0033ab1c (do .data: 0)
    float unknown_ab20 = 0, unknown_ab24 = -1.0f;
    const uint8_t* frames = nullptr;  // 0x0033ab28 a animação em curso (o 'fi' da última volta; nulo: recomeça)
    int64_t saved = 0;              // 0x0033ab30 o começo do quadro mostrado
    int64_t cursor = 0;             // 0x0033ab38
    int32_t last_tick = 0;          // 0x0033ab40
    float warmup = 0;               // 0x0033ab44
    int32_t frame_count = 0;        // 0x0033ab48
    int32_t frame = 0;              // 0x0033ab4c
    int32_t normal = 0;             // 0x0033ab50 (1: sem espelhar; 0 no .data)
    int32_t buffer = 0;             // 0x0033aae8 o buffer em uso (0/1)
    // O RLE carrega o estado entre os quadros (s1, s2, t7 do UpdateTexture começam em 0 a cada chamada).
    // As duas texturas 128 x 128 x 32 (o SHAPE_create do GGirlieMaterial) e as imagens do port delas.
    std::vector<uint32_t> pixels[2];
    uint32_t image[2] = {};
};
GGirlState& ggirl();
// A tabela de cores (0x0039d918, 128 x 128 x 3 bytes com sinal, do .data do DRIVING.ELF do jogador).
std::vector<int8_t>& ggirl_colour_table();
// [driving] 0x00239718 GGirl::InitGirl / 0x002397c0 KillGirl / 0x00239848 DoGirl / 0x002398f8 ClearTexture /
// 0x00239988 UpdateTexture / 0x0023a368 DrawTexture / 0x0023a3d0 DrawPoint
void GGirl_InitGirl(GHudData& h);
void GGirl_KillGirl();
void GGirl_DoGirl(GHudData& h);
void GGirl_ClearTexture(int32_t i);
bool GGirl_UpdateTexture(int32_t& x, int32_t& y);
void GGirl_DrawTexture(int32_t x, int32_t y);
void GGirl_DrawPoint(int32_t x, int32_t y, uint32_t colour);
// O port: a imagem do buffer (o ui_image) e a função que a cria (vazio: nenhuma, só os pixels).
std::function<uint32_t(int32_t buffer, const std::vector<uint32_t>& pixels)>& ggirl_image_sink();

}  // namespace drv
