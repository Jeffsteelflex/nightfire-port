#pragma once
// O HUD (módulo HUD_* do ACTION.ELF, 0x00199b70-0x001a16a0): os painéis de cada jogador (munição,
// vida e colete, mensagens da missão, ar, luneta, visão noturna, reflexo do sol, Redeemer, carrinho,
// câmera, a cortina de sangue da morte, raio X, câmera de segurança, OICW, Ronin, laser, estação
// espacial, itens pegos e, no multijogador, placar e radar) e a mira.
//
// BLData::hud aponta para um HUDINFO_tag (0x2c8 bytes, no heap): a mira e 22 painéis. Cada painel
// nasce de uma descrição estática (HUDPANECREATE_tag: posição, funções de criação e atualização e
// os sprites, SpriteInfo) da lista PaneList (MPPaneList no multijogador), que HUD_Init percorre.
// HUD_Update chama a atualização de cada painel por tick; a atualização escreve nos sprites
// (posição, cor, alfa, texto, modo de desenho) que o módulo 2D (Sprite_*, View_DrawSprites) desenha.
// HUD_Reset desliga os painéis que só aparecem em situações especiais (luneta, visão noturna...).
//
// As tabelas estáticas (PaneList, MPPaneList, as descrições dos painéis e dos sprites, CrossHair,
// HUDCrossCoords, BulletImg.189, TexUV.291) vêm do ACTION.ELF na partida (game/hud_tables.cpp),
// depois do construtor estático do HUD, que monta quatro delas.
//
// Os sprites são os da camada 2D (sprite e SpriteInfo de engine/sprite.h, Sprite_* em sprite.cpp): o
// HUD escreve neles e Game_Draw os desenha (Sprite_BuildList e View_DrawSprites na câmera do jogador e
// na 5). sprite::priority é o "modo" de desenho do HUD (-1, 0xff no PS2, esconde).
//
// Divergências e guardas só do port (comentados também nas funções):
// - HUD_CreateDefault pula um sprite que Sprite_Create2 não conseguiu criar (SpriteList cheia); o
//   original escreveria por um ponteiro nulo.
// - Os sprintf com um formato nulo (um rótulo sem texto) escrevem texto vazio, e os textos com %s
//   nulo saem vazios.
// - HUD_UpdateCameraPane cria um objeto marcador por inimigo visível a cada tick (o objeto tipo 0x10
//   se apaga sozinho no original); igual no port.

#include <cstddef>
#include <cstdint>

#include "engine/map_types.h"
#include "engine/sprite.h"
#include "platform/ptr32.h"

struct BLData;
struct obj_tag;
struct HUDPANE_tag;
struct HUDPANECREATE_tag;

using HudCreateFnSig = void(BLData*, HUDPANE_tag*, HUDPANECREATE_tag*, obj_tag*);
using HudUpdateFnSig = void(BLData*, HUDPANE_tag*, obj_tag*);
using HudCreateFn = HudCreateFnSig*;
using HudUpdateFn = HudUpdateFnSig*;

// Descrição estática de um painel (0x1c bytes).
struct HUDPANECREATE_tag {
    int16_t x;               // 0x00 canto da pane (HUD_ValidateXY o ajusta à tela)
    int16_t y;               // 0x02
    uint16_t w;              // 0x04 (HUD_CalcWidthHeight o calcula a partir dos sprites)
    uint16_t h;              // 0x06
    F32<HudCreateFnSig> create;  // 0x08
    F32<HudUpdateFnSig> update;  // 0x0c
    P32<SpriteInfo> sprites;  // 0x10
    uint16_t count;          // 0x14 sprites
    uint16_t extra;          // 0x16 palavras de estado da pane (HUDPANE_tag::extra)
    uint16_t flags;          // 0x18 HUD_ValidateXY: 8 prende x, 0x10 prende y, 0x80/0x100 soma a origem
    uint16_t pad_1a;
};
static_assert(sizeof(HUDPANECREATE_tag) == 0x1c);

// Uma pane em uso (0x20 bytes).
struct HUDPANE_tag {
    P32<HUDPANECREATE_tag> create;  // 0x00
    P32<uint32_t> extra;        // 0x04 estado próprio (mensagens: TXT_MSG*, passo, tipo, velocidade...)
    P32<P32<sprite>> sprites;  // 0x08 os sprites e, logo depois, extra
    F32<HudUpdateFnSig> update;  // 0x0c
    int16_t x;                  // 0x10
    int16_t y;                  // 0x12
    uint16_t w;                 // 0x14
    uint16_t h;                 // 0x16
    uint16_t count;             // 0x18
    uint16_t state;             // 0x1a HUD_State: a cortina de sangue põe 1 quando cobre a tela
    uint8_t enabled;            // 0x1c
    uint8_t unknown_1d;
    uint8_t unknown_1e;
    uint8_t valid;              // 0x1f a pane existe (tem descrição e função de criação)
};
static_assert(sizeof(HUDPANE_tag) == 0x20);

// Índices das panes (HUD_PANE_IND).
enum : uint32_t {
    kHudAmmo = 0,
    kHudHealth = 1,
    kHudMissionStatus = 2,
    kHudObjectiveStatus = 3,
    kHudInfoStatus = 4,
    kHudAir = 5,
    kHudSight = 6,
    kHudNightSight = 7,
    kHudLensFlare = 8,
    kHudRedeemer = 9,
    kHudRCCar = 10,
    kHudCamera = 11,
    kHudBlood = 12,      // cortina de sangue da morte (Player_HandleDeath)
    kHudMPScore = 13,    // só no multijogador
    kHudRadar = 14,      // só no multijogador
    kHudXray = 15,
    kHudSecCam = 16,
    kHudOICW = 17,
    kHudRonin = 18,
    kHudLaser = 19,
    kHudSpace = 20,
    kHudPickupStatus = 21,
    kHudPanes = 22,
};

// O HUD de um jogador (0x2c8 bytes; BLData::hud).
struct HUDINFO_tag {
    P32<sprite> crosshair;      // 0x000
    uint32_t unknown_004;
    HUDPANE_tag panes[kHudPanes];  // 0x008
};
static_assert(sizeof(HUDINFO_tag) == 0x2c8);
// O byte que Player_HandleDeath lê: a cortina de sangue (pane 0xc) ligada.
static_assert(offsetof(HUDINFO_tag, panes) + kHudBlood * sizeof(HUDPANE_tag) + offsetof(HUDPANE_tag, enabled) == 0x1a4);

// ---- tabelas estáticas (copiadas do ELF por game/hud_tables.cpp) ----
extern HUDPANECREATE_tag* PaneList[kHudPanes];
extern HUDPANECREATE_tag* MPPaneList[kHudPanes];
extern SpriteInfo CrossHair;
// As panes de mensagem da missão e dos objetivos (HUD_UpdateStatusPane as reconhece pelo endereço).
extern HUDPANECREATE_tag* HudMsgMissionStatusPane;
extern HUDPANECREATE_tag* HudMsgObjectiveStatusPane;
// Quadros da mira (HUDCrossCoords, 9 x 9 int16: u, v, largura, altura...).
extern int16_t HUDCrossCoords[9][9];
// Recorte do ícone de bala de cada tipo de munição (BulletImg.189: u, v, largura, altura por bala).
extern float BulletImg_189[33][4];
// Recortes dos ícones do radar (TexUV.291: 8 x 0x14 bytes; u em +0, v em +4, largura +8, altura +0xc).
extern uint8_t TexUV_291[0xa0];

// Armazém das cópias (game/hud_tables.cpp preenche; tamanhos com folga).
constexpr uint32_t kHudPanePool = 32;
constexpr uint32_t kHudSprInfoPool = 224;
constexpr uint32_t kHudFormatPool = 16;
extern HUDPANECREATE_tag HudPanePool[kHudPanePool];
extern SpriteInfo HudSprInfoPool[kHudSprInfoPool];
extern char HudFormatPool[kHudFormatPool][16];

// ---- globais do módulo ----
extern int16_t ttimer;
extern int16_t ctimer;
extern int16_t ThirdIconTimer;
extern int16_t CrouchIconTimer;
extern int16_t OICW_timer;
extern int16_t OICW_mode;
extern uint8_t FuckOffSprite3;
extern uint8_t MissileDeploy[8];  // estado dos mísseis da estação espacial (engine/space_station.h)
extern uint32_t switch_SHOW_COORDS;
extern uint32_t RealFrameRate;

// ---- funções portadas ----
void HUD_CreateDefault(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_Init(BLData* b, obj_tag* obj);
void HUD_Update(BLData* b, obj_tag* obj);
void HUD_ValidateXY(int16_t* x, int16_t* y, uint16_t w, uint16_t h, uint16_t player, uint16_t flags);
void HUD_CalcWidthHeight(HUDPANECREATE_tag* create, int16_t* w, int16_t* h);
void HUD_UpdateBloodPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateAmmoPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_FixMP(sprite* s, int16_t player);
void HUD_CreateMPHealthPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_UpdateMPHealthPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateHealthPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateStatusPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_MPUpdateStatusPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_CreateOICWPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_UpdateOICWPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateNightSightPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateSpacePane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateLensFlarePane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_CreateRedeemer(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_UpdateRedeemerPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateCrossHair(BLData* b, sprite* s);
void HUD_MonitorNightSight(BLData* b);
void HUD_CreateMPScorePane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_MPUpdatePane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_CreateRadar(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_RadarUpdate(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_CreateShrink(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_DisableAll(BLData* b);
void HUD_Reset(BLData* b);
void HUD_Enable(void* hud, uint32_t pane, int8_t on, uint16_t state);
uint32_t HUD_State(void* hud, uint32_t pane);
void HUD_CreateAmmoPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_CreateHealthPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_CreateMissionStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_CreateObjectiveStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_CreateInfoStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_CreatePickupStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj);
void HUD_UpdateAirPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateSightPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateCameraPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateXRayPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateSecCamPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);
void HUD_UpdateCarPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj);

// Ponteiros de código das tabelas do HUD (nome com mangling do ELF -> função portada; 0 se não for
// do HUD). Usado por Engine_ResolveCode.
uint32_t Hud_ResolveCode(const char* symbol_name, uint32_t ps2_address);

// ---- só no port ----
// O que as panes mostram, numa linha para o relatório de --screenshot e para o nf_loader_check:
// "paineis 0,1,2 vida 72 (6/7) colete 0 (0/8) municao 12-41 sangue 128 mira 1".
int HUD_Describe(const BLData* b, char* out, size_t size);
// Quantas panes e sprites o HUD de b tem (nf_loader_check).
uint32_t HUD_PaneCount(const BLData* b);
// Estado inicial das globais do módulo (testes).
void hud_debug_power_on();
