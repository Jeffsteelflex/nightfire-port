#pragma once
// O HUD das missões de carro: a parte do GHud do DRIVING.ELF (GHud.obj; docs/driving.md seção 19) que a missão usa
// (as mensagens, o objetivo, a mensagem de botão, o cronômetro, as tarjas de cinema, o escurecer, o resultado da
// missão) e os textos do jogo (GSystem::LOCALE_*, o .loc: data\gfx\locale\IDL_ENG.loc do MISC.VIV).
//
// O GHud do PS2 (o objeto de GHud::TheApp, 0x33ab6c) desenha com as galerias (hudNTSC.gal, GGallery: as fontes, os
// sprites e os quadros do HUD). Aqui ficam o estado do PS2 (GHudData, 0x500 bytes no leiaute do PS2, conferido nas
// partes 15 e 24 do nf_driving_check) e as funções que o mudam, portadas; o desenho de verdade (o GHud::GHud com a
// galeria, o GHud::Render, os Draw*, as miras) está em driving/game/hud_draw.h (seção 28). O hud_view() e o GHud_Tick
// são o HUD mínimo de antes (seção 19: os textos com a fonte de depuração do disco data\render\debug.sfn), que o
// nf_driving só usa sem a galeria ou com --debug-hud.
//
// Divergências: os sons do menu (AMenuSound::Trigger) só anotados; os setters que os outros módulos chamam
// (SetAutoDrive, GadgetOn, Stylin, TriggerDamageFlash) só mudam o GHud com o HUD de verdade carregado (hud_live()):
// sem ele o GHud é o mínimo e as conferências das partes 15, 17 e 18 ficam como eram.

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

// O GHud do PS2 (0x500 bytes, o __builtin_new do RRenderHigh::RRenderHigh; só os campos usados têm nome). Os primeiros
// 0x2a0 bytes são os ponteiros (p32) do GHud::GHud (driving/game/hud_draw.h): +0 a textura da cortina de sangue, +8 o
// GGallery, +0xc o UGroup da galeria, +0x10 o canvas hud_base, e os puppets (as chaves 'gk') de cada desenho.
struct GHudData {
    uint8_t unknown000[0x2a0];
    int32_t paused;            // +0x2a0 o menu de pausa aberto
    int32_t unknown2a4;
    int32_t unknown2a8;
    int32_t unknown2ac;
    int32_t letterbox_mode;    // +0x2b0 1 sem tarjas, 0 cheias, 2 entrando, 3 saindo
    int32_t letterbox_count;   // +0x2b4 0..30 (os quadros da animação)
    uint8_t unknown2b8[0x2c8 - 0x2b8];
    int32_t timer_beep_second; // +0x2c8 10 (Reset): o cronômetro regressivo bipa nos últimos segundos
    float health_flash;        // +0x2cc 3 quando a vida ou o escudo mudam (o DrawHealth mostra a vida enquanto > 0)
    float health_fade;         // +0x2d0 o esmaecer depois (0,5)
    float health_low;          // +0x2d4 o piscar com a vida em 25% ou menos
    uint8_t unknown2d8[0x2e8 - 0x2d8];
    float stylin_time;         // +0x2e8 o "stylin" (DrawStylin)
    int32_t stylin_state;      // +0x2ec 0 nada, 1 crescendo, 2 parado
    float stylin_scale;        // +0x2f0
    float stylin_fade;         // +0x2f4
    int32_t button_text;       // +0x2f8 o rótulo da mensagem de botão (ou a cadeia já pronta com +0x2fc <= 0)
    int32_t button_symbol;     // +0x2fc > 0: um rótulo do .loc
    float button_time;         // +0x300 os segundos que faltam
    float button_fade;         // +0x304
    int32_t objective_extra;   // +0x308
    int32_t zooming;           // +0x30c a mira travando (DrawZoomingCrossHairs; o DrawCrossHairs zera)
    int32_t result_text;       // +0x310 o rótulo do resultado (0x18576 cumprida, 0x18577 fracassada; -1 nenhum)
    int32_t result_state;      // +0x314 0 jogando, 5 mostrando o resultado (1: fracassada esperando a cortina)
    int32_t unknown318;
    float result_fade;         // +0x31c (DrawMissionResult, o estado 3)
    float result_wait;         // +0x320 (o estado 4)
    float result_delay;        // +0x324
    uint8_t unknown328[0x34c - 0x328];
    float black_fade;          // +0x34c 0 transparente, 1 preto
    int32_t black_fading;      // +0x350
    int32_t black_fade_start;  // +0x354 o passo da simulação
    float black_fade_from;     // +0x358
    float black_fade_rate;     // +0x35c por passo (1 / passos)
    int32_t message_step;      // +0x360 o passo em que a mensagem da missão começou (o "lastStep" que o GHud::Render lê)
    uint32_t frame_steps;      // +0x364 os passos desde o quadro anterior (GHud::Render)
    float frame_time;          // +0x368 os segundos do quadro (os Draw* descontam das mensagens)
    float unknown36c;          // +0x36c 1 (o DrawHealth: a vida do quadro anterior)
    float shield_last;         // +0x370 (o escudo do quadro anterior)
    uint8_t unknown374[0x380 - 0x374];
    float aim_x;               // +0x380 a mira na tela do HUD (GHud::SetTarget: o WTargetPicker)
    float aim_y;               // +0x384
    int32_t lock;              // +0x388 a trava do alvo (GHud::SetTargetLockState: o +0xa0 do WTargetPicker)
    uint32_t lock_frames;      // +0x38c os quadros da trava (0..7)
    uint32_t lock_frames_seen; // +0x390
    float lock_x;              // +0x394 o alvo travado na tela
    float lock_y;              // +0x398
    uint8_t unknown39c[0x3a8 - 0x39c];
    int32_t objective_frames;  // +0x3a8 os quadros desde o começo da mensagem do objetivo
    float objective_alpha;     // +0x3ac
    float objective_time;      // +0x3b0 os segundos que faltam (5 ao começar)
    int32_t objective_text;    // +0x3b4 o rótulo do objetivo mostrado (-1 nenhum)
    int32_t objective_next;    // +0x3b8 o próximo (a fila de um: -1 nenhum)
    int32_t objective_kind;    // +0x3bc 1 novo, 2 cumprido, 3 fracassado (o título)
    int32_t objective_next_kind;  // +0x3c0
    uint8_t unknown3c4[0x404 - 0x3c4];
    int32_t message_text;      // +0x404 o rótulo da mensagem da missão (EDisplayText; -1 nenhum)
    float message_time;        // +0x408 os segundos que faltam
    float message_fade;        // +0x40c
    int32_t message_long;      // +0x410 o texto passa de 32 caracteres
    int32_t language;          // +0x414
    int32_t aiming;            // +0x418
    uint32_t gadget;           // +0x41c o aparelho mostrado (DrawGadget: 0..5, senão o +0x84)
    float gadget_time;         // +0x420
    float gadget_slide;        // +0x424
    float gadget_blink;        // +0x428
    int32_t gadget_blinks;     // +0x42c
    uint8_t unknown430[4];
    int32_t no_health_fade;    // +0x434 (o UberBond +0x998 não zero: o DrawHealth sempre visível)
    int32_t unknown438;        // +0x438 (o UberBond +0x99c não zero)
    int32_t restart;           // +0x43c o GOrthoHudView::PostRender recomeça (ERestart)
    int32_t exit;              // +0x440 o GOrthoHudView::PostRender sai (EExit): o fim da missão
    uint8_t unknown444[4];
    float blood_curtain;       // +0x448 -1 (Reset); TriggerBloodCurtain: 6
    float screen_width;        // +0x44c 640
    float screen_height;       // +0x450 448
    float screen_cx;           // +0x454
    float screen_cy;           // +0x458
    float zoom_pct;            // +0x45c o zoom da mira (RPlayerCamera::GetZoomPercent)
    int32_t hud_off;           // +0x460 SMissionManager::CheckProgrammerEvent(0x18): sem a vida nem a arma
    int32_t timer;             // +0x464 o cronômetro mostrado (-1 nenhum)
    float view_sx;             // +0x468 a escala e o deslocamento da câmera do HUD (GOrthoHudView::DoRender)
    float view_sy;             // +0x46c
    float view_tx;             // +0x470
    float view_ty;             // +0x474
    uint8_t unknown478[0x480 - 0x478];
    int32_t unknown480;        // +0x480
    uint8_t unknown484[0x48c - 0x484];
    int32_t unknown48c;        // +0x48c
    uint8_t unknown490[0x49c - 0x490];
    int32_t pause_errors;      // +0x49c o menu de pausa mostra os erros (DrawPauseErrors)
    uint8_t unknown4a0[0x4f0 - 0x4a0];
    float frame_rate;          // +0x4f0 30 em NTSC (GHud::GHud; o DrawStylin)
    uint32_t vtable;           // +0x4f4 a tabela virtual do GCC 2.95 (0x003aaec8; o port: 0)
    uint8_t unknown4f8[0x500 - 0x4f8];
};
static_assert(sizeof(GHudData) == 0x500);

// O RSniperZoom::fgThis_RSniperZoom (0x3353ec): +4 ligado, +8 o alfa (o GHud::DrawCrossHairs da luneta do fuzil de
// Bond os escreve, o AimOff desliga; o efeito do zoom no render não tem porte: só o estado).
struct SniperZoomState {
    int32_t on = 0;
    int32_t alpha = 0;
};
SniperZoomState& sniper_zoom();

// O GHud do port: os dados e se ele existe (o GHud::TheApp não nulo).
GHudData& hud();
bool& hud_exists();

// O HUD de verdade (driving/game/hud_draw.h) ligado: os outros módulos, que só anotam as chamadas ao GHud (para as
// conferências), também mudam o GHud pelos Set* abaixo. Falso por padrão (as conferências das partes 15, 17 e 18 tratam
// essas chamadas como externas); o nf_driving liga com a galeria.
bool& hud_live();
// [driving] 0x00241040 GHud::SetAutoDrive(int) (o +0x2a8: a mira da direção automática)
void GHud_SetAutoDrive(GHudData& h, int32_t on);
// [driving] 0x00244f48 GHud::GadgetOn(float segundos, int aparelho)
void GHud_GadgetOn(GHudData& h, float seconds, uint32_t gadget);
// [driving] 0x0023c820 GHud::Stylin
void GHud_Stylin(GHudData& h);
// [driving] 0x0023e270 GHud::TriggerDamageFlash(float dano)
void GHud_TriggerDamageFlash(GHudData& h, float amount);

// [driving] 0x0023c858 GHud::Reset (os campos do GHudData; os quadros das galerias e a mira não existem no port)
void GHud_Reset(GHudData& h);
// [driving] 0x0023c728 GHud::FadeInFromBlack / 0x0023c780 FadeOutToBlack
void GHud_FadeInFromBlack(GHudData& h, int32_t steps);
void GHud_FadeOutToBlack(GHudData& h, int32_t steps);
// [driving] 0x00244ea0 GHud::LetterBoxOn / 0x00244ec8 LetterBoxOff
void GHud_LetterBoxOn(GHudData& h, bool instant);
void GHud_LetterBoxOff(GHudData& h, bool instant);
// [driving] 0x00244f30 GHud::AimOff (e o RSniperZoom +4)
void GHud_AimOff(GHudData& h);
// [driving] 0x00245eb0 GHud::KillButtonMessage / 0x00245ec0 ClearObjectiveMessage
void GHud_KillButtonMessage(GHudData& h);
void GHud_ClearObjectiveMessage(GHudData& h);
// [driving] 0x00245890 GHud::TriggerMissionMsg(float segundos, int rótulo)
void GHud_TriggerMissionMsg(GHudData& h, float seconds, int32_t text);
// [driving] 0x00245910 GHud::TriggerObjectiveMsg(int tipo, int rótulo, int extra)
void GHud_TriggerObjectiveMsg(GHudData& h, int32_t kind, int32_t text, int32_t extra);
// [driving] 0x002459d0 GHud::TriggerButtonMsg(float segundos, int texto, int símbolo)
void GHud_TriggerButtonMsg(GHudData& h, float seconds, int32_t text, int32_t symbol);
// [driving] 0x0023e568 GHud::TriggerTimerOn / 0x0023e570 TriggerTimerOff
void GHud_TriggerTimerOn(GHudData& h, int32_t timer);
void GHud_TriggerTimerOff(GHudData& h);
// [driving] 0x002456c0 GHud::SetMissionPassed / 0x00245708 SetMissionFailed / 0x0023e558 TriggerBloodCurtain
void GHud_SetMissionPassed(GHudData& h);
void GHud_SetMissionFailed(GHudData& h, bool now);
void GHud_TriggerBloodCurtain(GHudData& h);
// O passar do tempo de um quadro (o que GHud::DrawMessage, DrawObjectiveMessage, DrawButtonMessage e DrawLetterBox
// fazem com os contadores; port: chamado uma vez por quadro desenhado, frame_time em segundos).
void GHud_Tick(GHudData& h, float frame_time);

// ---------------------------------------------------------------- os textos (.loc)

// O arquivo de textos (LOCH; GSystem::LOCALE_LoadFile). Formato: "LOCH", u32 o deslocamento da tabela de rótulos (a
// partir do começo), o byte +8 bit 0 (há tabela), u16 +0xc o número de línguas, u16 +0xe a língua corrente, +0x10 os
// deslocamentos (u32) de cada língua; a tabela ("LOCI", u32 tamanho, u32 contagem, +0x10 os pares {u16 rótulo, u16
// índice} ordenados por rótulo); cada língua: +0xc u32 a contagem, +0x10 os deslocamentos (u32, a partir da língua) das
// cadeias.
struct LocaleFile {
    std::vector<uint8_t> data;
    bool load(std::vector<uint8_t> bytes);
    // [driving] 0x00259360 (sem nome: LOCALE_getstr) com 0x002592e0 (a busca binária do rótulo): nulo sem o rótulo.
    const char* symbol(int32_t id) const;
};
LocaleFile& locale();
// [driving] 0x0024b1a0 GSystem::LOCALE_Symbol: a cadeia ou "MISSING".
const char* LOCALE_Symbol(int32_t id);

// ---------------------------------------------------------------- o desenho do port

// O que desenhar neste quadro (as coordenadas na tela do PS2, 640 x 448; a cor RGBA com o alfa de 0 a 255).
struct HudLine {
    std::string text;
    float x = 0, y = 0;     // o canto de cima, à esquerda (centered: o x é o centro)
    float scale = 1.0f;
    uint32_t colour = 0xffffffffu;
    bool centered = true;
};
struct HudBox {
    float x = 0, y = 0, w = 0, h = 0;
    uint32_t colour = 0x80000000u;
};
struct HudView {
    std::vector<HudBox> boxes;
    std::vector<HudLine> lines;
    float black = 0.0f;     // o escurecer (0..1) por cima de tudo
};
// O HUD deste quadro (o cronômetro lê o gerente da missão).
HudView hud_view(const GHudData& h);

}  // namespace drv
