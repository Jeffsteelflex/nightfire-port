#pragma once
// O desenho do HUD das missões de carro: o GHud::GHud (as galerias e os puppets), o GHud::Render (o tempo do quadro),
// o GOrthoHudView::DoRender (a câmera do HUD, as tarjas, o escurecer, a cortina de sangue, os golpes na tela) e o
// GHud::Draw com os Draw* que Paris mostra (o objetivo, a mensagem da missão, a mensagem de botão, o cronômetro, a vida e
// o escudo, a arma e a munição, o aparelho, o "stylin", o resultado da missão, as miras); docs/driving.md seção 28.
//
// Portado do MIPS do DRIVING.ELF (GHud.obj) com o leiaute do PS2 (GHudData, 0x500 bytes, driving/game/hud.h; os
// puppets em p32 no começo dele) e conferido na parte 24 do nf_driving_check (os desenhos do material e os pacotes da
// fonte, os campos do GHud e os sons). Os Draw* desenham pela galeria (driving/render/ggallery.h) no quadro do HUD
// (driving/render/hud_gfx.h: hud_frame()).
//
// Divergências (só do port):
//  - o GHud::GHud não cria o RViewCamera nem o GOrthoHudView; a fila "HUDQ" é a do driving/game/hud_pause.h (seção 31:
//    a pausa, as trapaças, o pular cena); as texturas são imagens do port (a galeria, a cortina data\render\blood.ssh);
//  - o GOrthoHudView::DoRender não monta a matriz da vista no EAGL: guarda a escala e o deslocamento no GHud (+0x468..,
//    como o PS2) e o PC os aplica às posições dos materiais (hud_frame_to_batches com hud_view_transform());
//  - o FUN_0023eab0 (a câmera do jogador no modo "fly-by-wire" do míssil guiado) é falso: Paris não tem esse modo;
//  - o AMenuSound::Trigger vai ao som (driving/sound/audio.h) e fica anotado (hud_draw_stats().sounds, na ordem); o
//    DAudio::DrawTargets (depuração) não;
//  - o DrawPauseMenu/DrawPauseErrors estão em driving/game/hud_pause.h (seção 31).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "driving/game/hud.h"
#include "driving/render/ggallery.h"

namespace drv {

struct AutoDriveArmInfo;
struct CameraModeInfo;
class PlayerCamera;

// Os arquivos do HUD (do MISC.VIV): a galeria hudNTSC.gal, a cortina data\render\blood.ssh e a fonte
// data\loading\loading.sfn; image: a imagem do port de um SHPS (o 'MAIN' da galeria; first: a primeira entrada).
struct HudFiles {
    std::vector<uint8_t> gallery;
    std::vector<uint8_t> blood;
    std::vector<uint8_t> font;
    std::function<uint32_t(const uint8_t* ssh, uint32_t size, bool first)> image;
    std::function<uint32_t(const std::vector<uint8_t>& font)> font_image;
};

// A galeria do GHud do port (o GGallery do +8 e o arquivo dele).
struct HudGalleryStore {
    GGalleryData gallery;
    std::vector<uint8_t> file;
    bool loaded = false;
};
HudGalleryStore& hud_gallery_store();

// [driving] 0x0023ae30 GHud::GHud: a galeria, a cortina, a fonte, os canvases e os puppets, a posição das peças, o
// GHud::Reset. Falso sem a galeria.
bool GHud_Construct(GHudData& h, HudFiles files);
// O puppet (p32 da chave) na palavra `word` do GHud.
uint32_t hud_puppet(const GHudData& h, uint32_t word);

// O que o HUD lê de fora do GHud e da missão.
struct HudDrawEnv {
    bool hud_on = true;          // DAT_0032e670 (o HUD ligado)
    int32_t video_mode = 1;      // GetFoundationVideoMode (1 NTSC)
    int32_t renderer_frames = 3; // RRenderer +0x38 (menos de 3: a tela preta do escurecer)
    bool fly_by_wire = false;    // FUN_0023eab0
    // AStream::Get("Speech"): IsOver, ou a mixagem (+0x84) não é a AMix::Get("Speech") (o DrawMissionResult 4). Vazio:
    // os AStream do port (driving/sound/audio_stream.h).
    std::function<bool()> speech_done;
    // GHud::Render no resultado: AStream::Stop(Get("Music")), e a "Music" acabou (IsNotFound, IsOver, IsPaused ou IsLoop)
    // e a "Speech" também (IsNotFound, IsOver ou outra mixagem). Vazio: os AStream do port.
    std::function<bool()> result_streams_done;
    // RPlayerCamera::GetZoomPercent do RPlayerCamera do jogador (vazio: o current_player_camera()).
    std::function<float()> zoom_percent;
    // AICharacterHands::GetAimFraction (vazio: o do porte, driving/game/ai_character.h).
    std::function<float()> aim_fraction;
    // AttributeSet::LookupBool(o objeto +0x48 do alvo +0x5c, "ONLY_TARGET_WITH_EMP") (vazio: o
    // weapons_env().target_only_emp).
    std::function<bool(uint32_t target)> only_emp;
    // O objeto que a tabela virtual do carro do jogador dá (+0x68: a entrada +0x40/+0x44; o som da arma): 1 o +0xc0
    // (o zoom mudou: 1/0), 2 o AZoomObj::InTheZone(+0x1ac, 1/0), 3 a entrada +0x54 da tabela +0xac (a carga, 0..1).
    // Vazio: o objeto de som do port (audio_player_scene_c0, audio_player_zoom); anotado em hud_draw_stats().car_calls.
    std::function<void(int32_t kind, float value)> car_call;
    // O GHud::PauseOn do DrawMissionResult (o menu de fim de missão). Vazio: o do porte (driving/game/hud_pause.h); a
    // parte 24 do nf_driving_check o troca por uma contagem (lá o original tem o PauseOn enganchado).
    std::function<void(GHudData& h)> pause_on;
    // O GHud::DrawPauseErrors (errors verdadeiro) ou o GHud::DrawPauseMenu do fim do GHud::Draw. Vazio: os do porte; a
    // parte 24 do nf_driving_check os troca por nada (lá o original os tem enganchados; a parte 27 confere os dois).
    std::function<void(GHudData& h, bool errors)> pause_draw;
};
HudDrawEnv& hud_draw_env();

// O AMenuSound::Trigger(categoria, nome, mixagem, AView) dos Draw* (anotado em hud_draw_stats().sounds e tocado).
void hud_menu_sound(int32_t bank, const char* name, const char* mix, int32_t arg);

// Os sons e o que ficou de fora (na ordem).
struct HudSound {
    int32_t bank = 0;
    std::string name, mix;
    int32_t arg = 0;
};
struct HudDrawStats {
    std::vector<HudSound> sounds;  // AMenuSound::Trigger(banco, nome, mixagem, arg)
    int32_t frames = 0, pause_menu = 0, pause_errors = 0, debug_message = 0, null_button_text = 0;
    int32_t guards = 0;            // um puppet nulo (o PS2 lê o endereço 0)
    int32_t exits = 0, restarts = 0;  // os EExit e ERestart do GOrthoHudView::PostRender (um por quadro)
    std::vector<std::pair<int32_t, float>> car_calls;  // HudDrawEnv::car_call
};
HudDrawStats& hud_draw_stats();
// Os globais dos Draw* (o índice do som do golpe DAT_0033b1c8, o GadgetsEnum 0x0033b1cc do DrawFlyByWire).
struct HudDrawGlobals {
    int32_t body_hit = 0;
    int32_t gadgets_enum = 0;
    int32_t auto_aim_blink = 0;  // 0x0033b1d0 (o texto da mira automática na luneta)
};
HudDrawGlobals& hud_draw_globals();

// [driving] 0x0023cbc0 GHud::Render (o tempo do quadro +0x360..+0x368, o +0x440 do fim depois do resultado, a fila de
// ações: GHud_RenderQueue) e, com a galeria, o RViewCamera::Render do GOrthoHudView (o DoRender e o PostRender). O quadro
// do HUD fica em hud_frame() (quem chama a limpa antes).
void GHud_Render(GHudData& h, bool draw = true);
// [driving] 0x0023aad0 GOrthoHudView::DoRender (a lista do quadro: hud_frame())
void GOrthoHudView_DoRender(GHudData& h);
// [driving] 0x0023cf88 GHud::Draw
void GHud_Draw(GHudData& h);

// [driving] 0x00245190 GHud::DrawLetterBox
void GHud_DrawLetterBox(GHudData& h);
// [driving] 0x0023e930 GHud::DrawScreenFades
void GHud_DrawScreenFades(GHudData& h);
// [driving] 0x0023e7b8 GHud::DrawScreenHits
void GHud_DrawScreenHits(GHudData& h);
// [driving] 0x0023e2c0 GHud::DrawBloodCurtain
void GHud_DrawBloodCurtain(GHudData& h);
// [driving] 0x00246c48 GHud::DrawObjectiveMessage
void GHud_DrawObjectiveMessage(GHudData& h);
// [driving] 0x002469b8 GHud::DrawMessage
void GHud_DrawMessage(GHudData& h);
// [driving] 0x00245ab8 GHud::DrawButtonMessage
void GHud_DrawButtonMessage(GHudData& h);
// [driving] 0x002459f8 GHud::GetButton(char *texto, char *antes, char *depois): o símbolo "*A*", "*G*", "*P*" ou "*D*"
// (0..3) ou -1.
int32_t GHud_GetButton(const char* text, char* before, char* after);
// [driving] 0x0023e580 GHud::DrawTimer
void GHud_DrawTimer(GHudData& h);
// [driving] 0x0023da30 GHud::DrawHealth
void GHud_DrawHealth(GHudData& h);
// [driving] 0x00240c98 GHud::DrawWeapon
void GHud_DrawWeapon(GHudData& h);
// [driving] 0x00244f88 GHud::DrawGadget
void GHud_DrawGadget(GHudData& h);
// [driving] 0x002472b0 GHud::DrawStylin
void GHud_DrawStylin(GHudData& h);
// [driving] 0x00245f68 GHud::DrawMissionResult
void GHud_DrawMissionResult(GHudData& h);
// [driving] 0x0023ead8 GHud::DrawFlyByWire
void GHud_DrawFlyByWire(GHudData& h);
// [driving] 0x0023f328 GHud::DrawCrossHairs: os alvos do WTargetPicker (as marcas, as setas fora da tela, a trava),
// a barra de recarga, a mira da arma e a luneta do fuzil de Bond (os traços que giram com o zoom).
void GHud_DrawCrossHairs(GHudData& h);
// [driving] 0x0023ec08 GHud::DrawZoomingCrossHairs(Keys *): a trava fechando no alvo escolhido; a cor da marca.
uint32_t GHud_DrawZoomingCrossHairs(GHudData& h, uint32_t key);
// [driving] 0x0023f080 GHud::DrawFadingCrossHairs(Keys *): a trava abrindo e sumindo.
void GHud_DrawFadingCrossHairs(GHudData& h, uint32_t key);
// [driving] 0x002457a0 GHud::SetTarget(float *): a mira (a tela do PS2 na câmera do HUD).
void GHud_SetTarget(GHudData& h, const float pt[2]);
// [driving] 0x00245800 GHud::SetTargetLockState(int)
void GHud_SetTargetLockState(GHudData& h, int32_t lock);
// [driving] 0x00244ef0 GHud::AimOn (do RPlayerCamState::AimZoom)
void GHud_AimOn(GHudData& h);
// [driving] 0x0022bda0 WTargetPicker::DrawTargetingSystem (do RRenderWorldCamera::DoRender): a trava e a mira no HUD.
void WTargetPicker_DrawTargetingSystem(GHudData& h);
// [driving] 0x001b8960 RPlayerCamera::GetZoomPercent: no modo de tipo 0x200 (o AutoDrive) com o fov mínimo do braço
// (+0x27f) abaixo do padrão do modo, (padrão - fov) / (padrão - mínimo); senão 0.
float RPlayerCamera_GetZoomPercent(PlayerCamera& cam);
float RPlayerCamera_GetZoomPercent(const CameraModeInfo& mode, const AutoDriveArmInfo* arms, int8_t arm, float fov);

// [driving] 0x00241ea8 GHud::GetCenter(float escala, Text *, char *)
float GHud_GetCenter(GHudData& h, float scale, uint32_t key, const char* text);
// [driving] 0x00244398 GHud::DrawFontAndShadow(x, y, escala, Text *, cor da sombra, cor, char *)
void GHud_DrawFontAndShadow(GHudData& h, float x, float y, float scale, uint32_t key, uint32_t shadow, uint32_t colour,
                            const char* text);
// [driving] 0x002446d8 GHud::DrawFont(x, y, escala, Text *, cor, char *)
void GHud_DrawFont(GHudData& h, float x, float y, float scale, uint32_t key, uint32_t colour, const char* text);

// A escala e o deslocamento da câmera do HUD (o +0x468..+0x474) para o PC.
void hud_view_transform(const GHudData& h, float out[4]);

}  // namespace drv
