#pragma once
// O tempo das fases: a chuva, a neve e as bolhas (InitDrops, UpdateDrops, AddDrop, DrawDrops e as caixas de chuva
// RainBox_*, 0x0018bf68-0x0018d8e0), o vento das salas (Env_Update, Env_Reset; Env_Create e Env_GetWindVel já estavam
// em objects.cpp e effect.cpp), os raios entre dois pontos (Lightning_*; FractalThing em player_weapon.cpp) e as nuvens (Cloud_Create,
// cloud_update). Ver docs/engine.md, "Chuva, neve, raios e nuvens".
//
// - Gotas (DropVector[4096], DropState[4096]): o tipo vem do ambiente da sala (Env_Create, parâmetro 0 = ENV_tag::kind;
//   Env_Reset põe 0xff, nenhum): 0 chuva de partículas (4096, textura 0x3000045), 1 gotas de modelo (0x2000290; 128
//   nas fases 07000009-0b, 64 nas outras; em Paris também o modelo 0x2000039 virado para a câmera, com tweak 0x24),
//   2 partículas que sobem (2048, textura 0x3000087). A cada tick (Env_Update -> UpdateDrops, com DrawSnow ligado, o
//   valor do .sdata) e só com a câmera em uso (a 4 numa cena, senão a 0) vendo o céu (viewer_tag::sky_visible): um cubo
//   de 30 m em volta da câmera (7,5 m à frente do jogador fora de cena) é recortado contra as caixas de chuva
//   (RainBox_Clip); até 21 gotas novas por tick nascem nele (AddDrop: acima da câmera, só dentro de uma caixa, com o
//   chão da caixa ou a câmera - 7,5 m como fim), as vivas andam pelo vento mais uma das 32 velocidades de RandTable2 e
//   morrem ao passar do fim (descem 1024 m, para fora da vista). Sem caixa ativa, Added = 999 e nada é desenhado.
// - DrawDrops (Game_Draw, depois dos estilhaços, câmeras 0-3 e 4): o tipo 0 e o 2 numa lista de partículas
//   (psiDrawParticleList, aditiva, sem cores: o tamanho PS2SnowSize da fase e a cor 0x3f3f3f7f) com todas as CurrentMax
//   gotas, as mortas lá embaixo; o tipo 1 um modelo por gota viva (matriz identidade na posição), nada com a câmera 0
//   debaixo da água da sala (cel_tag::water_y).
// - Raio (posicionamento 0x2c, objeto 0x33): dois posicionamentos com o mesmo parâmetro 1 formam um par; o segundo cria
//   seis objetos e usa o primeiro como o oitavo (que vira tipo 1). Com o céu mostrando o relâmpago com repique
//   (SuperHappyLightningFlag, View_DrawSky), os oito segmentos (modelo 0x200032c, PositionBeam) desenham um raio
//   quebrado entre os dois pontos, refeito por FractalThing (pontos do meio deslocados ao acaso, metade da distância) a
//   cada Reflicker; fora disso ficam escondidos.
// - Nuvem (posicionamento 0xd4, objeto 0x24): um elemento do céu (View_AddSkyObj com o objeto) que anda com o jogador e
//   desliza pelos parâmetros 1 e 2 (x 0,001 por tick), dando a volta no parâmetro 3; a altura é o parâmetro 0 acima da
//   câmera (View_DrawSky).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

constexpr int kMaxDrops = 0x1000;

// Dados de uma caixa de chuva (RainBox_Create, 0x1c bytes; objeto tipo 1 fora das salas, ligado aos outros por
// obj_tag::owner a partir de RainBoxRoot): o recorte do cubo das gotas (RainBox_Clip).
struct RAINBOX_tag {
    float min[3];    // 0x00
    float max[3];    // 0x0c
    uint8_t active;  // 0x18 a interseção tem área em x e z
    uint8_t pad[3];
};
static_assert(sizeof(RAINBOX_tag) == 0x1c);

// Dados do raio (Lightning_Create, 0x50 bytes, tipo 0x33).
struct LIGHTNING_tag {
    obj_tag* seg[8];      // 0x00 os segmentos (seg[0] o próprio raio, seg[7] o par); seg[0] nulo: sem par
    _VECTOR start;        // 0x20 a posição deste
    _VECTOR end;          // 0x30 a do par
    uint32_t param0;      // 0x40 parâmetro 0
    int16_t pair;         // 0x44 parâmetro 1: o par
    int16_t param2;       // 0x46 parâmetro 2
    uint8_t pad[8];
};
static_assert(sizeof(LIGHTNING_tag) == 0x50 && offsetof(LIGHTNING_tag, pair) == 0x44);

// Dados da nuvem (Cloud_Create, 0x18 bytes, tipo 0x24).
struct CLOUD_tag {
    float speed_x;   // 0x00 parâmetro 1 x 0,001
    float speed_z;   // 0x04 parâmetro 2 x 0,001
    float off_x;     // 0x08
    float off_z;     // 0x0c
    int32_t height;  // 0x10 parâmetro 0 (lido por View_DrawSky)
    int32_t wrap;    // 0x14 parâmetro 3
};
static_assert(sizeof(CLOUD_tag) == 0x18);

extern uint8_t DropType;          // 0x0030d788: 0 chuva, 1 gotas de modelo, 2 sobem, 0xff nenhum
extern int16_t CurrentMax;        // 0x0030d784
extern int16_t Added;             // 0x0030d786: gotas criadas neste tick; 999 sem céu ou sem caixa
extern uint8_t InitBodge;         // 0x0030d789: 07000005-06 começam com todas as gotas no ar
extern obj_tag* RainBoxRoot;      // 0x0030d780
extern _VECTOR DropVector[kMaxDrops];  // 0x002a8100: posição, w = o fim (y)
extern uint8_t DropState[kMaxDrops];   // 0x002b8100: 1 viva
extern _VECTOR RandTable1[32];    // 0x003190f0: velocidades por tick (InitDrops)
extern _VECTOR RandTable2[32];    // 0x003192f0: as de RandTable1 com o vento, por tick
extern _VECTOR WindVector;        // 0x003194f0
extern _VECTOR CubeCentre;        // 0x00319500
extern _VECTOR PrevPos;           // 0x00319510
extern _VECTOR WindVector2;       // 0x00319520

void InitDrops(uint8_t kind);
void AddDrop(int16_t index, int32_t base, uint8_t* state, _VECTOR* drops);
void UpdateDrops();
void DrawDrops();
uint32_t RainBox_Clip();
obj_tag* RainBox_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void InitRainBox();
void Env_Update();
void Env_Reset();

obj_tag* Lightning_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Lightning_Update(obj_tag* obj);

obj_tag* Cloud_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void cloud_update(obj_tag* obj);

// Só no port: contagens para o relatório das capturas e o nf_loader_check.
struct WeatherStats {
    uint32_t rainboxes = 0;   // RainBox_Create
    uint32_t lightnings = 0;  // Lightning_Create
    uint32_t pairs = 0;       // raios que acharam o par
    uint32_t bolts = 0;       // Lightning_Update desenhando (SuperHappyLightningFlag)
    uint32_t clouds = 0;      // Cloud_Create
    uint32_t added = 0;       // AddDrop que acharam caixa
    uint32_t missed = 0;      // AddDrop fora das caixas
};
extern WeatherStats g_weather_stats;
// Gotas vivas agora.
uint32_t Weather_LiveDrops();
// Uma linha para o relatório: "tempo tipo T max M vivas V criadas C fora F caixas B ativas A ...".
void Weather_Describe(char* out, size_t size);

// Só do port (depuração, --no-env no nightfire.exe e no nf_loader_check): InitDrops não faz nada (nem os sorteios
// dela), o objects.cpp deixa os posicionamentos do tempo e dos enfeites provisórios, e quem liga também zera DrawSnow e
// pula a tela de carga, como antes do porte deles; para comparar capturas e provas com as de antes.
extern uint8_t g_port_no_env;

// Só para testes: o estado inicial (DropType 0xff, sem caixas).
void weather_debug_power_on();
