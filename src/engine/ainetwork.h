#pragma once
// Rede de navegação da IA (módulos AIPath, AIBounds e AINetwork do ACTION.ELF, 0x00156088-0x0015b830
// e 0x001d2598-0x001d30a4) e os objetos de IA posicionados na fase (nós de cobertura, pontos de IA e
// volumes, 0x00136ff0-0x0013a338).
//
// O bloco 0x05 de um item de mapa (AIPath_Parse, formato em docs/formats.md) traz duas coisas:
// - caminhos (AIPath_tag): grafos de nós (MAPNODE_tag) ligados por ligações (LINK_tag). O tipo
//   está nas flags: 0x2 é a rede de navegação (a única que entra nas buscas de rota: flags & 0xd
//   == 0), 0x4 patrulhas e 0x8 rotas de missão (andadas pelos drones nó a nó);
// - limites (AIBounds_tag, flags bit 0): polígonos de nós (BMAPNODE_tag) ligados por segmentos
//   (BLINK_tag) que os drones não podem atravessar (bordas de plataformas, janelas, portas...).
// Os nós ficam na memória do arquivo (com alguns campos reescritos na carga); o jogo aloca os
// NODE_tag (estado da busca de cada nó) e os BNODE_tag. AIPath_BindNodes pendura cada nó na lista
// da sala onde está (cel_tag::ai_nodes / ai_bnodes) e cada segmento de limite na lista das salas
// das suas pontas (cel_tag::ai_blinks).
//
// Buscas: AINetwork_NodeSearch acha os nós de um caminho perto de uma posição (ordenados pela
// distância 2D) que o drone alcança andando em linha reta (NDrone2_MoveTest*: distância, degrau,
// inclinação e os limites); AINetwork_DoAStarPath é um A* com custo comprimento * (usos + 1) e
// estimativa pela distância 2D até o destino; os emissores (AIEmitter_tag) guardam a distância de
// cada nó até um ponto (Dijkstra, AINetwork_EmitPath) para fugir, cercar ou achar cobertura. Uma
// rota (AIRoute_tag) é seguida aos poucos: LinkCreep_* anda pela ligação atual em passos de 0,4 m,
// testando os limites a cada passo.
//
// Peculiaridades preservadas (ver cada função):
// - AINetwork_NodeSearch e as cópias dela repetem a busca na MESMA sala para cada portal cuja
//   faixa de altura contém a posição (o original não troca a sala do teste).
// - AINetwork_NodeSearchCel compara a distância 2D (não ao quadrado) com radius_sq.
// - Os nós fechados do A* e do Dijkstra recebem o bit 0x40000000 (o de "aberto"), não o
//   0x80000000: um nó fechado nunca volta para a lista aberta, mesmo com um custo melhor, e o laço
//   que tiraria nós da lista fechada nunca roda.
// - AINetwork_OptimiseRoute soma o comprimento da ligação de índice NODE_tag::parent_link (a
//   posição na lista do nó, não o índice da ligação) no custo da rota.
// - AINetwork_LineIntersectsBoundry, com adjust, troca a altura das duas pontas do limite pela
//   altura ABSOLUTA da linha menos 0,5.
//
// Divergências do port (todas por endereço inválido ou lixo de registrador/pilha no original):
// - A componente w que o original deixa com lixo do registrador da VU0 (AINetwork_HitBoundry) ou da
//   pilha (AINetwork_GetBoundsPushVectorForSphere) é 0 aqui.
// - Quando vecutil_intersect_line_line_2d acha segmentos colineares (retorno 1, sem escrever o
//   ponto), AINetwork_LineIntersectsBoundry usa o ponto (0, 0, 0) em vez do lixo da pilha.
// - Tabelas cheias (mais de 50 caminhos ou limites, 256 coberturas ou pontos de IA) e tabelas ainda
//   não apontadas (Drone_PreLoad_Init não rodou) são puladas e contadas em g_ainetwork_guards.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct DCVars_tag;  // variáveis de controle de um drone (módulo NDrone2, ainda não portado)
struct Drone_tag;   // dados de um drone (obj_tag::data de um objeto tipo 2; engine/drone.h)

// Posição com a sala onde está.
struct alignas(16) CelPos_tag {
    _VECTOR pos;          // 0x00
    P32<cel_tag> cel;     // 0x10
    uint32_t unknown_14[3];
};
static_assert(sizeof(CelPos_tag) == 0x20);

// ---- caminhos ----

// Bits de MAPNODE_tag::flags.
constexpr uint32_t kNodeDoor = 0x2;          // nó de porta (NDrone_InitDoorNodes acha a porta tipo 0x1c)
constexpr uint32_t kNodeKick = 0x4;          // nó de chute (objeto 0x1b com o script 060005bb)
constexpr uint32_t kNodeOpen = 0x40000000;   // na lista aberta (e, por engano, nos fechados)
constexpr uint32_t kNodeClosed = 0x80000000; // nunca posto (ver o cabeçalho)

// Nó de um caminho, dentro do arquivo (0x40 bytes).
struct alignas(16) MAPNODE_tag {
    int16_t index;        // 0x00 (reescrito na carga)
    int16_t path;         // 0x02 índice do caminho (reescrito)
    uint32_t flags;       // 0x04: kNode*, bits 16-19 ações do nó, e os bits das buscas: mask de
                          //       AITarget_tag (achado) e mask << 1 (visto mas inalcançável)
    float unknown_08;     // 0x08 lixo do editor
    P32<cel_tag> cel;     // 0x0c sala (AIPath_Link2Cel)
    _VECTOR pos;          // 0x10 (y + 0,4 na carga)
    uint32_t unknown_20[8];
};
static_assert(sizeof(MAPNODE_tag) == 0x40);

// Estado de busca de um nó (0x30 bytes, alocado por AIPath_Parse).
struct NODE_tag {
    P32<NODE_tag> next;       // 0x00 lista da sala (cel_tag::ai_nodes)
    P32<MAPNODE_tag> node;    // 0x04
    P32<NODE_tag> sorted_next;  // 0x08 lista ordenada por dist (AINetwork_NodeSearchCel)
    float dist;               // 0x0c distância 2D até a posição buscada
    uint8_t parent_link;      // 0x10 posição, na lista de ligações do pai, da ligação usada
    uint8_t link_count;       // 0x11 (AIPath_Prepare)
    uint16_t first_link;      // 0x12 primeira entrada em AIPath_tag::node_links
    float g;                  // 0x14 custo desde a origem
    float h;                  // 0x18 estimativa até o destino
    float f;                  // 0x1c g + h: ordem da lista aberta
    P32<NODE_tag> parent;     // 0x20
    P32<NODE_tag> list_next;  // 0x24 lista aberta ou fechada
    P32<obj_tag> door;        // 0x28 porta (nó de porta) ou objeto do chute (nó de chute)
    uint32_t unknown_2c;
};
static_assert(sizeof(NODE_tag) == 0x30);

// Bits de LINK_tag::flags.
constexpr uint16_t kLinkCreeping = 0x10;  // um drone anda por ela (LinkCreep_ForNodes)
constexpr uint16_t kLinkDoor = 0x20;      // cruza um limite de porta (NDrone_InitDoorNodes)
constexpr uint16_t kLinkKick = 0x40;      // cruza um limite de chute (NDrone_InitKickNodes)

// Ligação entre dois nós de um caminho, dentro do arquivo (0x10 bytes).
struct LINK_tag {
    int16_t index;        // 0x00 (no arquivo: (caminho << 16) | índice; reescrito)
    int16_t path;         // 0x02 (reescrito)
    uint16_t flags;       // 0x04 kLink*, e bits de AINetwork_SetLinksFlagInCircle
    uint16_t used;        // 0x06 drones com a ligação na rota: o custo no A* é length * (used + 1)
    uint16_t a;           // 0x08 nós
    uint16_t b;           // 0x0a
    float length;         // 0x0c distância 3D entre os nós (AIPath_Prepare)
};
static_assert(sizeof(LINK_tag) == 0x10);

// Caminho (0x48 bytes; 50 em AINetwork).
struct AIPath_tag {
    int16_t index;            // 0x00
    uint16_t unknown_02;
    P32<uint8_t> header;      // 0x04 no arquivo: nome (0x80 bytes) e cabeçalho
    uint32_t flags;           // 0x08: 0x2 rede de navegação, 0x4 patrulha, 0x8 rota (bit 0: é limite)
    uint32_t unknown_0c[8];   // 0x0c palavras 0x21-0x28 do cabeçalho (zero no disco)
    uint32_t node_count;      // 0x2c
    P32<MAPNODE_tag> map_nodes;  // 0x30 no arquivo
    P32<NODE_tag> nodes;      // 0x34 node_count entradas
    uint32_t link_count;      // 0x38
    P32<LINK_tag> links;      // 0x3c no arquivo
    P32<uint16_t> node_links;  // 0x40 ligações de cada nó, em sequência (AIPath_Prepare)
    uint32_t found;           // 0x44 masks de AITarget_tag com nó achado (bit) ou só visto (bit << 1)
};
static_assert(sizeof(AIPath_tag) == 0x48);

// ---- limites ----

// Bits de BLINK_tag::flags.
constexpr uint32_t kBoundPassable = 0x400;  // uma ligação da rede o atravessa (InitPassableBoundries)
constexpr uint32_t kBoundDoor = 0x800;      // atravessado por uma ligação de porta
constexpr uint32_t kBoundKick = 0x1000;     // atravessado por uma ligação de chute

// Nó de limite, dentro do arquivo (0x20 bytes).
struct alignas(16) BMAPNODE_tag {
    int16_t index;        // 0x00 (reescrito)
    int16_t bounds;       // 0x02 índice do limite (reescrito)
    uint32_t unknown_04;
    float height;         // 0x08 altura do limite acima do nó: teto (raio de 256 para cima), mínimo 2
    P32<cel_tag> cel;     // 0x0c sala (AIBounds_Link2Cel)
    _VECTOR pos;          // 0x10 (y + 0,4 na carga)
};
static_assert(sizeof(BMAPNODE_tag) == 0x20);

// Lista de nós de limite de uma sala (8 bytes, alocado por AIPath_Parse).
struct BNODE_tag {
    P32<BNODE_tag> next;  // 0x00 cel_tag::ai_bnodes
    P32<BMAPNODE_tag> node;  // 0x04
};
static_assert(sizeof(BNODE_tag) == 8);

// Segmento de limite, dentro do arquivo (0x14 bytes).
struct BLINK_tag {
    int16_t index;        // 0x00 (reescrito)
    int16_t bounds;       // 0x02 (reescrito)
    uint32_t flags;       // 0x04 kBound* (zerado na carga)
    uint16_t a;           // 0x08 nós
    uint16_t b;           // 0x0a
    P32<BLINK_tag> next_a;  // 0x0c próximo na lista da sala do nó a (cel_tag::ai_blinks)
    P32<BLINK_tag> next_b;  // 0x10 próximo na lista da sala do nó b
};
static_assert(sizeof(BLINK_tag) == 0x14);

// Limite (0x40 bytes; 50 em AINetwork). Mesmo começo de AIPath_tag.
struct AIBounds_tag {
    int16_t index;            // 0x00
    uint16_t unknown_02;
    P32<uint8_t> header;      // 0x04
    uint32_t flags;           // 0x08 (bit 0)
    uint32_t unknown_0c[8];
    uint32_t node_count;      // 0x2c
    P32<BMAPNODE_tag> map_nodes;  // 0x30
    P32<BNODE_tag> nodes;     // 0x34
    uint32_t link_count;      // 0x38
    P32<BLINK_tag> links;     // 0x3c
};
static_assert(sizeof(AIBounds_tag) == 0x40);

// A rede inteira (global AINetwork, 0x002c7940, 0x1ab8 bytes).
constexpr int kMaxAIPaths = 50;
constexpr int kMaxAIBounds = 50;
struct AINETWORK_tag {
    uint32_t version;         // 0x00: só a versão 8 é lida
    float max_slope;          // 0x04 inclinação máxima (rad) de um passo (Drone_PostLoad_Init)
    float slope_height;       // 0x08 desnível acima do qual a inclinação é testada
    float max_height;         // 0x0c desnível máximo de um passo
    float unknown_10;         // 0x10 (30)
    float max_dist_sq;        // 0x14 distância máxima de um passo, ao quadrado (900)
    uint32_t max_nodes;       // 0x18 maior node_count dos caminhos (tamanho das listas das rotas)
    uint32_t max_links;       // 0x1c maior link_count
    uint32_t path_count;      // 0x20
    AIPath_tag paths[kMaxAIPaths];      // 0x24
    uint32_t bounds_count;    // 0xe34
    AIBounds_tag bounds[kMaxAIBounds];  // 0xe38
};
static_assert(sizeof(AINETWORK_tag) == 0x1ab8);
static_assert(offsetof(AINETWORK_tag, paths) == 0x24 && offsetof(AINETWORK_tag, bounds) == 0xe38);
extern AINETWORK_tag AINetwork;

// ---- buscas, alvos, rotas e emissores ----

// Parâmetros e resultado de AINetwork_NodeSearch/NodeSearchCel (0x30 bytes, na pilha).
struct AINodeSearch_tag {
    P32<CelPos_tag> pos;      // 0x00 posição buscada (com a sala)
    P32<_VECTOR> goal;        // 0x04 destino para a estimativa h, ou nulo
    P32<AIPath_tag> path;     // 0x08
    int8_t to_node;           // 0x0c 1: testa posição -> nó e põe o nó na lista aberta; 0: nó -> posição
    uint8_t unknown_0d[3];
    P32<NODE_tag> open;       // 0x10 cabeça falsa da lista aberta (só list_next), ou nulo
    P32<obj_tag> obj_a;       // 0x14 repassados aos testes de movimento
    P32<obj_tag> obj_b;       // 0x18
    uint32_t mask;            // 0x1c bits postos nas flags do nó (achado; inalcançável: mask << 1)
    float radius_sq;          // 0x20 raio da busca ao quadrado (0: 15 m e sem filtro)
    int16_t node;             // 0x24 nó achado (modo to_node)
    uint16_t unknown_26;
    int32_t found;            // 0x28
    int32_t failed;           // 0x2c
};
static_assert(sizeof(AINodeSearch_tag) == 0x30);

// Alvo de uma rota: posição e os nós da rede de onde ela é alcançada (0x60 bytes com o alinhamento:
// os dois de NPCGlobals ficam em +0x1c0 e +0x220).
struct alignas(16) AITarget_tag {
    uint32_t mask;            // 0x00 bit das buscas (0x10000000 nas rotas)
    P32<obj_tag> obj;         // 0x04 objeto seguido, ou nulo
    uint32_t unknown_08[2];
    CelPos_tag pos;           // 0x10
    CelPos_tag last;          // 0x30 posição da última busca (AINetwork_BuildAITarget)
    int16_t found;            // 0x50 nós achados (somando os caminhos)
    int16_t failed;           // 0x52 nós vistos mas inalcançáveis
    uint32_t frame;           // 0x54 GameState.frame da busca (0 = refazer)
    uint32_t heard_frame;     // 0x58 Drone_tag::sound_target: GameState.frame do som ouvido
    float heard_level;        // 0x5c e a intensidade dele (DroneFunc_HandleSoundAlerts)
};
static_assert(sizeof(AITarget_tag) == 0x60);
static_assert(offsetof(AITarget_tag, frame) == 0x54);

// Emissor: distância de cada nó de um caminho até um ponto (0x50 bytes).
struct alignas(16) AIEmitter_tag {
    P32<obj_tag> owner;       // 0x00
    uint32_t unknown_04[3];
    CelPos_tag pos;           // 0x10
    P32<AIPath_tag> path;     // 0x30
    uint32_t size;            // 0x34 bytes em dist (node_count do caminho)
    P32<uint8_t> dist;        // 0x38 por nó: distância * 255 / 256 (0xff = além do alcance)
    int16_t start;            // 0x3c nó do caminho mais perto do ponto
    uint16_t unknown_3e;
    uint8_t allocated;        // 0x40
    uint8_t unknown_41[15];
};
static_assert(sizeof(AIEmitter_tag) == 0x50);

// Ponto de IA posicionado na fase (0xa0 bytes; 256 em AIPoints). Também é o destino das rotas
// (AINetwork_SetupRouteTo*).
struct alignas(16) AIPoint_tag {
    uint32_t unknown_00;
    uint8_t active;           // 0x04
    uint8_t unknown_05;
    uint16_t kind;            // 0x06 parâmetro 0
    uint16_t unknown_08;
    uint16_t unknown_0a;
    uint32_t param;           // 0x0c parâmetro 2
    uint32_t unknown_10;
    float radius;             // 0x14 parâmetro 1; nas rotas, a distância de chegada (2 por padrão)
    uint32_t unknown_18[2];
    CelPos_tag pos;           // 0x20
    _VECTOR rot;              // 0x40
    AIEmitter_tag emitter;    // 0x50
};
static_assert(sizeof(AIPoint_tag) == 0xa0);

// Nó de cobertura posicionado na fase (0xb0 bytes; 256 em CoverNodes).
struct alignas(16) CoverNode_tag {
    uint8_t type;             // 0x00: 0 quina, 1 baixa
    uint8_t channel_on;       // 0x01 só vale com este canal ligado (parâmetro 4)
    uint8_t channel_off;      // 0x02 só vale com este canal desligado (parâmetro 5)
    uint8_t low_a;            // 0x03 baixa: animação (tabela por parâmetro 1)
    uint8_t low_b;            // 0x04
    uint8_t corner_a;         // 0x05 quina: parâmetro 6 (8 se zero)
    uint8_t corner_b;         // 0x06 quina: parâmetro 7 (8 se zero)
    uint8_t unknown_07;
    float angle;              // 0x08 quina: abertura (parâmetro 3 em graus; 45 se zero)
    uint32_t flags;           // 0x0c quina: lados (4/8 = parâmetro 0, 0x20/0x40 = 1, 0x80/0x100 = 2);
                              //       baixa: 1, 2; 0x10 em ambos (usado por Drone_IsCoverNodeUsable)
    P32<cel_tag> cel;         // 0x10
    P32<obj_tag> user;        // 0x14 drone que a ocupa
    uint32_t unknown_18[2];
    _VECTOR pos;              // 0x20
    _VECTOR rot;              // 0x30 (rot.y = para onde a cobertura olha)
    AIEmitter_tag emitter;    // 0x40
    uint32_t usable[8];       // 0x90 por lado (oponente): bits de Drone_IsCoverNodeUsable
};
static_assert(sizeof(CoverNode_tag) == 0xb0);
static_assert(offsetof(CoverNode_tag, emitter) == 0x40);

// Volume de IA (dados de um objeto tipo 0x3e, 0x120 bytes; nó da lista AIBoxList).
struct alignas(16) AIVolume_tag {
    LLNODE_tag node;          // 0x00
    P32<obj_tag> obj;         // 0x08 (a matriz do objeto orienta a caixa)
    uint32_t unknown_0c;
    _VECTOR half;             // 0x10 meias medidas: |caixa do modelo| / 2 * escala
    float value;              // 0x20 parâmetro 0 * 0,01
    uint16_t kind;            // 0x24: parâmetro 1 0 -> 0, 1 -> 2, 2 -> 1, 4 -> 3
    uint16_t param4;          // 0x26
    uint32_t param2;          // 0x28
    uint8_t param3;           // 0x2c
    uint8_t unknown_2d[0xf3];
};
static_assert(sizeof(AIVolume_tag) == 0x120);
static_assert(offsetof(AIVolume_tag, kind) == 0x24);

// Bits de AIRoute_tag::flags.
constexpr uint16_t kRouteModeMask = 0x7;   // 1 rota até o fim, 2 circular, 3 vai e volta
constexpr uint16_t kRouteReverse = 0x8;
constexpr uint16_t kRouteValid = 0x10;
constexpr uint16_t kRouteAltered = 0x40;   // destino trocado (AINetwork_AlterDestFor_DROUTE_Nearest)
constexpr uint16_t kRouteNoDirect = 0x80;  // não tenta ir em linha reta (AINetwork_CalcRoute)

// Resultados de AINetwork_CalcRoute/DoAStarPath (AIRoute_tag::status).
enum : uint8_t {
    kRouteExact = 0,       // até um nó do alvo
    kRouteNearest = 1,     // até um nó de onde o alvo é visto mas não alcançado
    kRouteDirect = 2,      // em linha reta, sem a rede
    kRouteArrived = 3,     // AINetwork_FollowRoute: perto o bastante
    kRouteNoTarget = 4,    // o alvo não tem nós
    kRouteNoStart = 5,     // nenhum nó perto da origem
    kRouteNoPath = 6,      // a rede não liga a origem ao alvo
    kRouteNoNetwork = 7,
    kRouteNoNodes = 8,
    kRouteStuck = 9,
    kRouteReset = 0xb,
    kRouteBlocked = 0xc,
};

// Rota de um drone (0x100 bytes; o drone tem duas, em +0x860 e +0x970).
struct alignas(16) AIRoute_tag {
    uint16_t flags;           // 0x00 kRoute*
    uint8_t unknown_02;       // 0x02 (AINetwork_FollowRoute: 9 ou 0xc)
    uint8_t unknown_03;
    uint8_t status;           // 0x04
    uint8_t unknown_05;
    int16_t at_node;          // 0x06 cópia de current (AINetwork_SetupNextNode)
    uint32_t frame;           // 0x08 AITarget_tag::frame do cálculo
    uint16_t prev_start;      // 0x0c
    uint16_t start;           // 0x0e primeiro nó (0xffff)
    uint16_t goal;            // 0x10 último nó (0xffff)
    uint16_t unknown_12[7];
    CelPos_tag from;          // 0x20 pés do drone
    CelPos_tag to;            // 0x40 destino
    CelPos_tag next;          // 0x60 próximo ponto a andar
    int16_t current;          // 0x80 posição atual em list (-1 antes do primeiro)
    uint16_t count;           // 0x82 nós em list
    P32<AITarget_tag> target;  // 0x84
    float distance;           // 0x88 comprimento da rota
    float arrive;             // 0x8c distância de chegada (AINetwork_SetupRouteToPosition)
    float creep_max;          // 0x90 LinkCreep_Increment: distância máxima ao próximo passo
    uint32_t unknown_94[3];
    int8_t creeping;          // 0xa0 andando por uma ligação
    int8_t special;           // 0xa1 a ligação sai de um nó com ação (ver LinkCreep_ForNodes)
    int8_t unknown_a2;
    int8_t unknown_a3;
    int16_t creep_link;       // 0xa4 ligação em uso (kLinkCreeping)
    uint16_t unknown_a6;
    P32<AIPath_tag> creep_path;  // 0xa8
    int16_t creep_from;       // 0xac posições em list
    int16_t creep_to;         // 0xae
    int16_t creep_step;       // 0xb0
    int16_t creep_steps;      // 0xb2
    uint32_t unknown_b4[3];
    _VECTOR creep_dir;        // 0xc0 passo (0,4 m na direção da ligação)
    _VECTOR creep_origin;     // 0xd0
    _VECTOR creep_end;        // 0xe0
    P32<uint16_t> list;       // 0xf0 nós da rota (alocado pelo drone: AINetwork.max_nodes entradas)
    P32<AIPath_tag> path;     // 0xf4
    P32<obj_tag> owner;       // 0xf8
    P32<obj_tag> target_obj;  // 0xfc
};
static_assert(sizeof(AIRoute_tag) == 0x100);
static_assert(offsetof(AIRoute_tag, target) == 0x84 && offsetof(AIRoute_tag, creeping) == 0xa0);
static_assert(offsetof(AIRoute_tag, list) == 0xf0 && offsetof(AIRoute_tag, path) == 0xf4);

// Tabelas dos objetos de IA (o jogo as aponta em NPCGlobals no Drone_PreLoad_Init).
constexpr int kMaxCoverNodes = 256;
constexpr int kMaxAIPoints = 256;
extern CoverNode_tag CoverNodes[kMaxCoverNodes];  // 0x00283e10
extern AIPoint_tag AIPoints[kMaxAIPoints];        // 0x0028ee10
extern LLISTINFO_tag AIBoxList;                    // 0x002706c8 volumes de IA

// Situações em que o original escreveria fora de uma tabela ou usaria um ponteiro nulo.
struct AINetworkGuards {
    uint32_t paths_full = 0;       // mais de 50 caminhos no bloco 0x05
    uint32_t bounds_full = 0;      // mais de 50 limites
    uint32_t bounds_no_cel = 0;    // segmento de limite com uma ponta fora das salas
    uint32_t covers_full = 0;      // mais de 256 nós de cobertura (ou tabela não apontada)
    uint32_t cover_table = 0;      // índice além da tabela de animações de Drone_CoverLowNode
    uint32_t points_full = 0;      // mais de 256 pontos de IA (ou tabela não apontada)
    uint32_t volumes_skipped = 0;  // volume de IA sem Drone_PreLoad_Init (AIBoxList não reiniciada)
    uint32_t link_not_found = 0;   // nós seguidos da rota sem ligação (UpdateLinksUsedCount)
    uint32_t special_before_route = 0;  // LinkCreep_AtSpecialNode antes do primeiro nó
    uint32_t handler_without_dcv = 0;   // LinkCreep_Handler sem DCVars_tag
};
extern AINetworkGuards g_ainetwork_guards;

// ---- carga (aipath.cpp) ----
void AIPath_Init();
void AIPath_Parse(uint8_t* block);
void AIPath_BindNodes();
void AIPath_Prepare(AIPath_tag* path);
void AIPath_Link2Cel(NODE_tag* node, char* name);
void AIBounds_Link2Cel(BNODE_tag* node, char* name);
void AIBounds_Prepare(AIBounds_tag* bounds);

// ---- objetos de IA da fase (aipath.cpp) ----
void Drone_CoverCornerNode(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void Drone_CoverLowNode(_VECTOR* pos, _VECTOR* rot, level_tag* level);
void Drone_AIPoint(_VECTOR* pos, _VECTOR* rot, level_tag* level);
obj_tag* Drone_AIVolume_Create(_VECTOR* pos, _VECTOR* rot, _VECTOR* scale, level_tag* level, celglist_tag* glist);
void Drone_AIVolume_Delete(obj_tag* obj);
uint32_t Drone_PointInAnyAIBox(_VECTOR* p, uint16_t kind);
bool Drone_IsCoverNodeUsable(uint32_t side, CoverNode_tag* node);
void Drone_ProcessCoverNodes();
void NDrone_InitDoorNodes();
void NDrone_InitKickNodes();

// ---- consultas e rotas (ainetwork.cpp) ----
AIPath_tag* AINetwork_NavPathForPosition(_VECTOR* pos, cel_tag* cel, int8_t test_move);
uint32_t AINetwork_NodeSearchCel(AINodeSearch_tag* search);
uint32_t AINetwork_NodeSearch(AINodeSearch_tag* search);
void AINetwork_NodesForPosition(AITarget_tag* target, CelPos_tag* pos, obj_tag* obj, float radius);
void AINetwork_BuildAITarget(AITarget_tag* target, obj_tag* obj, float radius);
void AINetwork_SetupGoalPosition(AITarget_tag* target, AIPoint_tag* point, CelPos_tag* pos, float radius, int8_t unused);
bool AINetwork_SetupGoalPositionToObj(AITarget_tag* target, AIPoint_tag* point, obj_tag* obj, float radius,
                                      int8_t unused);
uint32_t AINetwork_DoAStarPath(AIRoute_tag* route, obj_tag* a, obj_tag* b, uint32_t mask, uint16_t unused);
uint32_t AINetwork_OptimiseRoute(AIRoute_tag* route, uint32_t status, int8_t start, int8_t end);
uint32_t AINetwork_CalcRoute(AIRoute_tag* route, obj_tag* owner, obj_tag* target, uint16_t param);
bool AINetwork_AlterDestFor_DROUTE_Nearest(AIRoute_tag* route);
uint32_t AINetwork_SetupRouteToObject(obj_tag* owner, obj_tag* target, AITarget_tag* goal, AIPoint_tag* point,
                                      float radius, AIRoute_tag* route);
uint32_t AINetwork_SetupRouteToPosition(obj_tag* owner, CelPos_tag* pos, AITarget_tag* goal, AIPoint_tag* point,
                                        float radius, AIRoute_tag* route);
bool AINetwork_VerifyRouteTarget(obj_tag* owner, obj_tag* target, AITarget_tag* goal, AIPoint_tag* point,
                                 float radius, AIRoute_tag* route);
uint32_t AINetwork_CalcRouteToObject(obj_tag* owner, obj_tag* target, AIRoute_tag* route, AITarget_tag* goal,
                                     AIPoint_tag* point, float radius, uint16_t param);
uint32_t AINetwork_CalcRouteToPosition(obj_tag* owner, CelPos_tag* pos, AIRoute_tag* route, AITarget_tag* goal,
                                       AIPoint_tag* point, float radius, uint16_t param);
void AINetwork_ResetRoute(AIRoute_tag* route, obj_tag* owner, obj_tag* target);
uint8_t AINetwork_RouteIsValid(AIRoute_tag* route);
void AINetwork_InvalidateRoute(AIRoute_tag* route);
void AINetwork_ValidateRoute(AIRoute_tag* route);
int32_t AINetwork_RouteNodeOffset(int16_t current, int16_t delta, AIRoute_tag* route);
float AINetwork_GetRouteDistanceBetweenNodes(AIRoute_tag* route, int16_t a, int16_t b);
float AINetwork_GetRouteDistance(AIRoute_tag* route);
float AINetwork_GetRouteDistance3D(AIRoute_tag* route);
void AINetwork_UpdateLinksUsedCount(AIRoute_tag* route);
uint32_t AINetwork_SetupNextNode(int16_t step, AIRoute_tag* route, DCVars_tag* dcv);
uint32_t AINetwork_FollowRoute(AIRoute_tag* route, DCVars_tag* dcv);

// Emissores.
void AINetwork_AllocEmitter(AIEmitter_tag* em, uint32_t size);
void AINetwork_FreeEmitter(AIEmitter_tag* em);
uint32_t AINetwork_InitEmitter(obj_tag* owner, _VECTOR* pos, cel_tag* cel, AIEmitter_tag* em, AIPath_tag* path,
                               int8_t unused);
uint32_t AINetwork_InitEmitter2(obj_tag* owner, _VECTOR* pos, cel_tag* cel, AIEmitter_tag* em, AIPath_tag* path,
                                int8_t unused);
uint32_t AINetwork_EmitPath(AIEmitter_tag* em, float max_dist);
uint32_t AINetwork_Emitter_GetNodeAtDistance(int16_t start, float dist, AIEmitter_tag* em, MAPNODE_tag** out);

// Limites e ligações.
uint32_t AINetwork_HitBoundry(_VECTOR* closest, const BLINK_tag* link, const _VECTOR* p, float radius_sq,
                              float* dist_sq, uint32_t skip);
uint32_t AINetwork_LineIntersectsBoundry(const BLINK_tag* link, const _VECTOR* from, const _VECTOR* to,
                                         _VECTOR* out, float* best, int8_t adjust);
uint32_t AINetwork_GetBoundsPushVectorForSphere(const _VECTOR* pos, const cel_tag* cel, float radius, _VECTOR* push,
                                                uint32_t skip);
int32_t AINetwork_BoundsTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                             _VECTOR* out, int8_t near_nodes, int8_t* unused);
int8_t AINetwork_BoundsNodeTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                                float radius, _VECTOR* unused, int8_t* unused2);
uint32_t AINetwork_InitPassableBoundries();
uint32_t AINetwork_FurthestPosition(_VECTOR* from, _VECTOR* to, _VECTOR* out);
uint32_t AINetwork_TestRayCels(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel);
void AINetwork_ClearLinkFlags(uint16_t flags);
void AINetwork_SetLinksFlagInCircle(_VECTOR* center, cel_tag* cel, float radius, uint16_t set, uint16_t clear);
uint32_t AINetwork_ModIntersectedLinkFlags_Bounds(_VECTOR* from, _VECTOR* to, uint32_t set, uint32_t skip,
                                                  _VECTOR* out, int8_t adjust);

// Andar pela ligação atual da rota em passos de 0,4 m.
int8_t LinkCreep_ForNodes(AIRoute_tag* route, int32_t from, int32_t to, int8_t at_end);
int8_t LinkCreep_Calc(AIRoute_tag* route, int8_t at_end);
bool LinkCreep_CalcToRouteEnd(AIRoute_tag* route);
uint32_t LinkCreep_AtSpecialNode(AIRoute_tag* route);
uint32_t LinkCreep_Increment(AIRoute_tag* route, DCVars_tag* dcv, int8_t* blocked);
uint32_t LinkCreep_Decrement(AIRoute_tag* route, DCVars_tag* dcv);
uint32_t LinkCreep_Handler(AIRoute_tag* route, DCVars_tag* dcv);
uint32_t LinkCreep_Dest(AIRoute_tag* route, int16_t delta, CelPos_tag* out);

// Testes de movimento e utilidades do NDrone2 usados pela rede (ainetwork.cpp). Os testes devolvem
// 1 (passa), 0 (não passa) ou -1 (cruza um limite e out recebeu o ponto).
int8_t NDrone2_MoveTest(const _VECTOR* from, const cel_tag* from_cel, const _VECTOR* to, const cel_tag* to_cel,
                        obj_tag* a, obj_tag* b, _VECTOR* out, int8_t near_nodes, int8_t check);
int8_t NDrone2_MoveTest(CelPos_tag* from, CelPos_tag* to, obj_tag* a, obj_tag* b, _VECTOR* out, int8_t near_nodes,
                        int8_t check);
int8_t NDrone2_MoveTestFromNode(MAPNODE_tag* node, CelPos_tag* to, obj_tag* a, obj_tag* b, _VECTOR* out,
                                int8_t near_nodes, int8_t check);
int8_t NDrone2_MoveTestToNode(CelPos_tag* from, MAPNODE_tag* node, obj_tag* a, obj_tag* b, _VECTOR* out,
                              int8_t near_nodes, int8_t check);
uint32_t NDrone2_FeetPos(obj_tag* obj, _VECTOR* out);
cel_tag* NDrone2_FindCel(_VECTOR* pos);
int16_t NDrone2_NearestNode(CelPos_tag* pos, AIPath_tag* path, obj_tag* obj);
bool NDrone2_DistanceToEmitter(CelPos_tag* pos, AIPath_tag* path, uint16_t* node, AIEmitter_tag* em, obj_tag* obj,
                               float* dist);
obj_tag* NDrone2_Player();
uint32_t NDrone2_DroneNearPos(_VECTOR* pos, float radius, obj_tag* except);

// ---- visão (drone_vision.cpp; declarada aqui porque a rede não inclui drone.h) ----
uint32_t DroneVision_CanSeePosition(obj_tag* obj, _VECTOR* pos, cel_tag* cel, obj_tag* ignore);

// ---- ainda não portadas (stubs em modules_stubs.cpp) ----
void NDrone2_ReachedDestNode(AIRoute_tag* route, DCVars_tag* dcv);
uint32_t behaviour_util_getProperty(int32_t property, uint32_t* bits);  // 0
void* SP_getScriptInfo(obj_tag* obj);  // nulo
