#pragma once
// Objetos do jogo (obj_tag) e o núcleo que os cria, liga às salas e apaga (control_*, Control_*,
// 0x00133070-0x00134a90 no ACTION.ELF), mais as listas ligadas genéricas (LList_*).
//
// Todo objeto tem um cabeçalho comum de 0x110 bytes seguido dos dados do tipo (obj->data). Os
// objetos ficam numa lista duplamente ligada a partir de DynamicObjList (um obj_tag vazio usado como
// cabeça) e, ao mesmo tempo, na lista da célula (sala) onde estão. O byte "type" indexa
// control_funcs, a tabela de {atualização, colisão, remoção} de cada tipo.

#include <cstddef>
#include <cstdint>

#include "engine/map_types.h"

struct HITDATA_tag;  // lista de colisões (módulo Collide, ainda não portado)

// Nó das listas LList_*: os dois primeiros campos de qualquer estrutura que entre numa delas.
struct LLNODE_tag {
    LLNODE_tag* prev;  // +0 em direção a "first"
    LLNODE_tag* next;  // +4 em direção a "last"
};

struct LLISTINFO_tag {
    LLNODE_tag* first;
    LLNODE_tag* last;
    uint16_t count;
    uint16_t node_size;
};
static_assert(sizeof(LLISTINFO_tag) == 12);

struct alignas(16) obj_tag {
    LLNODE_tag forced;          // 0x00 nó da ForcedList
    obj_tag* cel_next;          // 0x08 lista da célula (a célula guarda a cabeça em cel_tag::objects)
    void* cel_prev;             // 0x0c objeto anterior, ou a própria célula no primeiro
    ATTACH_tag* attach;         // 0x10 anexos desenhados junto (View_DrawObjectGlist)
    obj_tag* next;              // 0x14 DynamicObjList
    obj_tag* prev;              // 0x18
    obj_tag* owner;             // 0x1c (Control_CreateObjEx)
    cel_tag* cel;               // 0x20 sala onde o objeto está
    uint32_t unknown_24[3];
    _VECTOR pos;                // 0x30
    _VECTOR old_pos;            // 0x40
    _VECTOR rot;                // 0x50 ângulos de Euler
    _VECTOR old_rot;            // 0x60
    _VECTOR old_sphere;         // 0x70
    _VECTOR sphere;             // 0x80 esfera no mundo (centro e raio)
    MATRIX matrix;              // 0x90 (linha 3 em 0xc0 = posição)
    HITDATA_tag* hits;          // 0xd0
    struct HITTEST_tag* hit_test;  // 0xd4 teste de colisão em andamento (objeto temporário do Collide)
    celglist_tag* glist;        // 0xd8 modelo
    uint8_t* sphere_src;        // 0xdc com o bit 8 de mode: esfera tirada de sphere_src + 0x80
    void* data;                 // 0xe0 dados do tipo (logo depois do cabeçalho)
    void* script;               // 0xe4 (SP_RemoveObj)
    float scale;                // 0xe8
    uint32_t created;           // 0xec GameState.frame no momento da criação
    uint32_t flags;             // 0xf0 (celglist_tag::flags + bits de controle, ex. 0x10000000 forçado)
    uint16_t state;             // 0xf4
    uint16_t unknown_f6;
    uint16_t unknown_f8;        // 0x1f
    uint16_t mode;              // 0xfa: 2 = só translação, 4 = matriz fixa, 8 = esfera externa...
    uint8_t unknown_fc;         // 2
    uint8_t unknown_fd;
    uint8_t life;               // 0xfe bit 0: remoção pedida; bit 1: ligado na criação
    uint8_t type;               // 0xff índice em control_funcs (1 depois de apagado)
    uint8_t color[3];           // 0x100 cor da luz da sala (0xff = ainda não definida)
    uint8_t unknown_103[4];     // 0xff
    uint8_t unknown_107[9];
};
static_assert(sizeof(obj_tag) == 0x110);
static_assert(offsetof(obj_tag, matrix) == 0x90 && offsetof(obj_tag, glist) == 0xd8);
static_assert(offsetof(obj_tag, flags) == 0xf0 && offsetof(obj_tag, type) == 0xff);

// Entrada de control_funcs (0x0026f890, 85 entradas).
struct CONTROLFUNCS_tag {
    void (*update)(obj_tag*);
    void (*collide)(obj_tag*);
    void (*remove)(obj_tag*);
};
constexpr int kControlFuncCount = 85;
extern const CONTROLFUNCS_tag control_funcs[kControlFuncCount];

extern obj_tag DynamicObjList;  // cabeça da lista (só next é usado)
extern int32_t DynamicObjCount;
extern LLISTINFO_tag ForcedList;   // objetos que atravessam salas ou sempre desenhados
extern uint8_t switch_channels[256];
extern uint32_t switch_channels_time[256];
extern uint8_t switch_channels_prev[256];  // switch_channels de dois ticks atrás (o hold anterior)
extern uint8_t switch_channels_hold[256];  // switch_channels do tick anterior
extern uint16_t switch_channels_MusicVars[256];  // ticks com o canal ligado (DroneFunc_CheckAlarmRaised)
void Init_SwitchChannels();  // zera os canais na carga de uma fase (control.cpp)

// ---- listas ----
uint32_t LList_Init(LLISTINFO_tag* list, uint16_t node_size, uint16_t count);
uint32_t LList_Add(LLISTINFO_tag* list, LLNODE_tag* node);
LLNODE_tag* LList_Remove(LLISTINFO_tag* list, LLNODE_tag* node);
void LList_Insert(LLISTINFO_tag* list, LLNODE_tag* after, LLNODE_tag* node);
LLNODE_tag* LList_Cut(LLISTINFO_tag* list);
void LList_AllocnNodes(LLISTINFO_tag* list, uint32_t count);
// Insere na ordem que "compare" dá (recebe ponteiros para os ponteiros dos nós, como o qsort). Devolve o
// nó depois do qual "node" entrou (nulo: no começo).
LLNODE_tag* LList_InsertAt(LLISTINFO_tag* list, LLNODE_tag* node, int8_t from_first,
                           int32_t (*compare)(const void*, const void*));

// Lista dupla: os nós livres e os em uso (0x18 bytes; DList_*).
struct DLISTINFO_tag {
    LLISTINFO_tag free;
    LLISTINFO_tag used;
};
static_assert(sizeof(DLISTINFO_tag) == 0x18);
uint32_t DList_Init(DLISTINFO_tag* list, uint16_t node_size, uint16_t count);
void DList_AllocnNodes(DLISTINFO_tag* list, uint32_t count);
void* DList_MoveFromFree2InUse(DLISTINFO_tag* list);
uint32_t DList_RemoveFromInUse2Free(DLISTINFO_tag* list, LLNODE_tag* node);

// ---- objetos ----
void control_init_object_lists();
obj_tag* control_create_object(uint32_t data_size, _VECTOR* pos, _VECTOR* rot, quaternion_tag* quat);
void control_init_object(obj_tag* obj);
void control_add_object_to_list(obj_tag* head, obj_tag* obj);
void control_link_object_to_cel(obj_tag* obj, cel_tag* cel);
void control_unlink_object(obj_tag* obj);
void control_delete_object(obj_tag* obj);
obj_tag* control_first_object();
void control_init_collision(obj_tag* obj);
void control_AnimUpdate(obj_tag* obj);
void control_movement_object_handler(int8_t unused);
void control_handle_cel_change(obj_tag* obj);
void Control_SetGList(obj_tag* obj, celglist_tag* glist);
void Control_BuildWorldSph(obj_tag* obj);
void Control_GetObjMatrix(obj_tag* obj, MATRIX* m);
obj_tag* Control_CreateObjEx(uint16_t data_size, _VECTOR* pos, _VECTOR* rot, MATRIX* m, celglist_tag* glist,
                             obj_tag* owner, int8_t link, uint16_t mode, float scale, uint16_t flags, uint8_t c0,
                             uint8_t c1, uint8_t c2);
uint32_t Control_NextLOD(obj_tag* obj);
obj_tag* Control_ReturnNextObjectOfType(uint8_t type, obj_tag* after);
void Control_DeleteAllObjectsOfType(uint8_t type);
void Control_ObjectReferenced(obj_tag* obj, int8_t referenced);
void Control_SetEffectType(obj_tag* obj, uint32_t flags);
void Control_InheritVelocity(obj_tag* obj, obj_tag* from, float scale);

// ---- salas ----
cel_tag* build_FindCel(const _VECTOR* point, world_tag* world);
void build_link_objects_to_rooms(cel_tag* first);
void build_LinkDoors2Portals(cel_tag* first, obj_tag* objects);
uint32_t build_LinkToRoom(obj_tag* obj, int8_t straddle, world_tag* world);
bool build_PointOnFloor(cel_tag* cel, obj_tag* obj, _VECTOR* p, float len, _VECTOR* dir);
uint32_t Controls_StraddleTest(obj_tag* obj);

// ---- matrizes de objeto (módulo View) ----
void View_RotTransMatrix(const _VECTOR* rot, const _VECTOR* pos, MATRIX* m);
void View_RotTransScaleMatrix(const _VECTOR* rot, const _VECTOR* pos, const _VECTOR* scale, MATRIX* m);

// ---- céu (View_AddSkyObj) ----
// Entrada de GfxList (0x40 bytes; GfxList tem 0x10 bytes antes da primeira e 21 entradas).
// Um elemento pode ter o modelo normal e o de relâmpago (View_DrawSky troca um pelo outro de vez em
// quando e toca o trovão depois).
struct SKYOBJ_tag {
    celglist_tag* glist;        // 0x00 modelo normal
    celglist_tag* glist_flash;  // 0x04 modelo durante o relâmpago
    obj_tag* obj;               // 0x08
    obj_tag* obj_flash;         // 0x0c
    _VECTOR pos;                // 0x10
    _VECTOR rot;                // 0x20
    int16_t flash_period;       // 0x30 intervalo máximo entre relâmpagos, em quadros (param * FRAME_RATE)
    uint16_t flash_frames;      // 0x32 duração de cada relâmpago
    int16_t flash_left;         // 0x34 quadros que faltam do relâmpago atual
    int16_t flash_wait;         // 0x36 quadros até o próximo
    int16_t thunder_wait;       // 0x38 quadros até o trovão
    int8_t layer;               // 0x3a 1: antes do cenário; 0: depois (View_DrawSky)
    uint8_t pad[5];
};
static_assert(sizeof(SKYOBJ_tag) == 0x40);
struct GFXLIST_tag {
    uint8_t unknown_00[0x10];
    SKYOBJ_tag sky[21];
    uint8_t unknown_550[0x10];
};
static_assert(sizeof(GFXLIST_tag) == 1376);
extern GFXLIST_tag GfxList;
void View_AddSkyObj(uint16_t slot, celglist_tag* glist, const _VECTOR* pos, const _VECTOR* rot, obj_tag* obj,
                    int8_t flash, uint16_t period, uint16_t flash_frames, int8_t layer);

// Só para testes: estado inicial das globais do módulo.
void object_debug_power_on();
