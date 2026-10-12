#pragma once
// Visão: câmeras (viewer_tag), visibilidade por portais (Vision_*) e montagem e desenho das listas
// de cada quadro (View_*, Game_Draw). Ver docs/engine.md, "Câmeras e visibilidade".
//
// Cada câmera (glb_viewer[0..10]) captura a cena em View_CaptureScene: a partir da sala da câmera,
// segue os portais visíveis recortando o frustum (Vision_Recurse), monta a lista de salas visíveis
// e, em View_AddCels/View_AddObjects, põe nas três listas de desenho (0 opaco, 1 transparente,
// 2 especial) as salas, as células penduradas nelas e os objetos, com a distância à câmera como
// chave. View_DrawObjList ordena e desenha cada lista com psiDrawObjectMatrix.

#include <cstddef>
#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

// Anexo desenhado junto com uma célula ou objeto (lista em cel_tag::attach e obj_tag::attach).
// Só os campos lidos pelo View_Draw*Glist; o resto do registro ainda é desconhecido.
// As marcas de tiro e de pé (Effect_tag, engine/effect.h) são anexos da EffectList.
struct alignas(16) ATTACH_tag {
    LLNODE_tag node;        // 0x00 nó da EffectList (marcas)
    P32<ATTACH_tag> next;   // 0x08
    P32<ATTACH_tag> prev;   // 0x0c
    P32<celglist_tag> glist;  // 0x10
    P32<obj_tag> parent;    // 0x14 se houver, a matriz é relativa à de parent (obj_tag::matrix)
    P32<cel_tag> cel;       // 0x18 sala que tem a marca na lista
    uint32_t unknown_1c;
    MATRIX matrix;          // 0x20
};
static_assert(sizeof(ATTACH_tag) == 0x60);
static_assert(offsetof(ATTACH_tag, matrix) == 0x20);

// Item de uma lista de desenho (12 bytes).
struct DRAWENTRY_tag {
    float key;      // distância² à câmera (negativa para trás-para-frente); salas usam -90000 + 10 * n
    P32<obj_tag> obj;
    P32<cel_tag> cel;
};
static_assert(sizeof(DRAWENTRY_tag) == 12);

struct STACKINFO {
    P32<int32_t> data;
    uint16_t count;
    uint16_t size;
};
static_assert(sizeof(STACKINFO) == 8, "STACKINFO: 8 bytes");

struct alignas(16) viewer_tag {
    P32<world_tag> world;        // 0x000
    P32<cel_tag> cel;            // 0x004 sala da câmera
    P32<DRAWENTRY_tag> lists[3];  // 0x008
    uint16_t counts[3];          // 0x014 (a lista é circular: o índice é mascarado por size - 1)
    uint16_t sizes[3];           // 0x01a
    uint8_t sky_visible;         // 0x020 alguma sala visível deixa ver o céu
    uint8_t unknown_21[3];
    uint32_t unknown_24;         // 0x024 recebe BLData::cam_mode do jogador
    uint8_t index;               // 0x028 número da câmera (bit em object_display_mask)
    uint8_t active;              // 0x029
    uint8_t sprites;             // 0x02a desenha sprites e HUD
    uint8_t updated;             // 0x02b
    STACKINFO stack;             // 0x02c
    int32_t stack_data[12];      // 0x034
    P32<obj_tag> follow;         // 0x064 objeto passado à função de atualização
    F32<void(obj_tag*, void*)> updater;  // 0x068
    uint32_t unknown_6c;
    plane_equ_tag planes[4];     // 0x070 lados do frustum (Vision_InView)
    _VECTOR delta;               // 0x0b0 pos - prev_pos
    _VECTOR pos;                 // 0x0c0
    _VECTOR prev_pos;            // 0x0d0
    _VECTOR rot;                 // 0x0e0 ângulos de Euler (Camera_Set)
    float viewport[4];           // 0x0f0 x0, x1, y0, y1 em pixels da tela do PS2
    float proj_x;                // 0x100
    float proj_y;                // 0x104
    float lod_scale;             // 0x108
    float width;                 // 0x10c
    float height;                // 0x110
    float aspect;                // 0x114 largura / altura
    float fov;                   // 0x118 campo de visão vertical, em radianos
    uint32_t unknown_11c;
    MATRIX camera;               // 0x120 câmera no mundo: linha 2 para a frente, linha 3 posição
    MATRIX view;                 // 0x160 Mat_World2ViewMat(camera)
    MATRIX billboard;            // 0x1a0 camera com a linha 0 negada
    uint32_t unknown_1e0[8];
    float shake;                 // 0x200
    float swing;                 // 0x204 fase do balanço da arma (Camera_ApplySwing)
    uint32_t unknown_208[7];
    P32<obj_tag> player;         // 0x224 jogador desta câmera (Player_Init)
    float fade;                  // 0x228
    float fade_speed;            // 0x22c
    uint8_t fade_rgba[4];        // 0x230
    uint8_t unknown_234;
    uint8_t fade_state;          // 0x235
    uint16_t mode;               // 0x236 (2: visão especial que mostra objetos com a flag 0x400)
    uint32_t unknown_238[2];
};
static_assert(sizeof(viewer_tag) == 0x240);
static_assert(offsetof(viewer_tag, planes) == 0x70 && offsetof(viewer_tag, viewport) == 0xf0);
static_assert(offsetof(viewer_tag, camera) == 0x120 && offsetof(viewer_tag, shake) == 0x200);

constexpr int kViewerCount = 11;
extern viewer_tag* glb_viewer[kViewerCount];
extern uint32_t object_display_mask;
extern _VECTOR CamPos;
extern uint32_t switch_ForceDrawAll;
extern float MAX_VIEW_CONE;
extern uint16_t OnlyScaleZ;          // o objeto em desenho só escala em z (mode & 1)
extern uint16_t FlipScale;
extern uint8_t bCharacterLight;      // obj_tag::life bit 2
extern uint8_t bShadowCharacter;     // obj_tag::life bit 3
extern uint8_t GlobalBlur;           // desfoque da tela, decai 16 por quadro (Game_Draw)
extern uint32_t ScreenBlankerState;
extern uint8_t LightningType;        // relâmpago atual: 0 normal, 1 com repique (View_DrawSky)
extern uint8_t Reflicker;
extern uint8_t SuperHappyLightningFlag;  // o céu está mostrando o modelo de relâmpago
// Só do port (o relatório das capturas): o contador do céu que gira da 0700001b (rotatosky_168 do View_DrawSky,
// um passo por Game_Draw fora de pausa).
uint32_t View_PortSkySpin();
// Só do port: quantos relâmpagos do céu (SKYOBJ_tag::glist_flash) o View_DrawSky começou desde a partida.
uint32_t View_PortLightnings();
extern uint32_t Tots[3];             // itens nas três listas, somados entre as câmeras

// ---- câmeras ----
viewer_tag* build_alloc_viewer(uint32_t index);
viewer_tag* build_link_world_to_viewer(viewer_tag* viewer, world_tag* world);
void Camera_Reset(viewer_tag* viewer);
void Camera_Create(uint32_t index, world_tag* world, int8_t active, uint16_t x, uint16_t y, uint16_t width,
                   uint16_t height);
void Camera_CalcViewAngles(uint16_t index, float fov);
void Camera_Enable(uint32_t index, int8_t active, int8_t sprites, _VECTOR* pos);
void Camera_Set(viewer_tag* viewer, _VECTOR* pos, _VECTOR* rot);
obj_tag* Camera_SetUpdator(uint16_t index, obj_tag* obj, void (*updater)(obj_tag*, void*));
uint32_t Camera_SetFade(uint16_t index, float time, uint32_t rgba, int8_t flag);
void Camera_ApplySwing(viewer_tag* viewer, float amount);
void Camera_SetToPlayer(obj_tag* obj, viewer_tag* viewer);
// Máscara de câmeras em que um objeto aparece (obj_tag::unknown_f8).
void View_SetDrawInOtherViewsOnly(obj_tag* obj, uint32_t index);
void View_DrawInThisViewOnly(obj_tag* obj, uint32_t index);
void View_SetDrawInNoViews(obj_tag* obj);
void View_SetDrawInAllViews(obj_tag* obj);
// Ponto do mundo na tela da câmera (pixels do PS2); 1 na frente, -1 atrás, 0 sem câmera.
int32_t View_3DPoint2Screen(const _VECTOR* in, _VECTOR* out, uint16_t index);
void Camera_CreateCameras();
void Camera_Update(uint16_t index);
void Camera_CheckLocation(uint16_t index);
uint32_t Camera_UpdateGlbVars(uint16_t index);
void Camera_UpdateAll();
uint32_t Stack_Init(STACKINFO* stack, uint16_t size, int32_t* data);
uint32_t build_point_in_bounding_box(const _VECTOR* p, const cel_tag* cel);
cel_tag* Cel_ObjectLeftCel(cel_tag* cel, const _VECTOR* from, const _VECTOR* to);
uint32_t Intersect_BoxBox(const _VECTOR* min_a, const _VECTOR* max_a, const _VECTOR* min_b, const _VECTOR* max_b);

// ---- visibilidade ----
// Polígono de portal em recorte (_PORTAL, 0xd0 bytes): contador e até 12 vértices.
struct alignas(16) _PORTAL {
    int8_t count;
    uint8_t pad[15];
    _VECTOR v[12];
};
static_assert(sizeof(_PORTAL) == 0xd0);
uint32_t ClipPortalWithPlane(const plane_equ_tag* plane, _PORTAL* in, _PORTAL* out);
void View_CaptureScene(viewer_tag* viewer);
void Vision_Init_Portal_Recurse(viewer_tag* viewer);
void Vision_Calculate_View_Fustrum(viewer_tag* viewer);
void Vision_Portal_Recurse(viewer_tag* viewer);
void Vision_Recurse(cel_tag* cel, int32_t depth);
void Vision_AddCelToDraw(cel_tag* cel, uint32_t depth);
void vision_generate_display_list(viewer_tag* viewer);
void vision_GetCamPos(_VECTOR* out);
uint32_t Vision_InView(const _VECTOR* center, float radius, const viewer_tag* viewer);
void View_AddCels(viewer_tag* viewer);
void View_AddObjects(cel_tag* cel, viewer_tag* viewer, float unused);
void View_AddForcedObjects(viewer_tag* viewer);

// ---- desenho ----
void View_DrawObjList(viewer_tag* viewer, uint16_t list);
void View_DrawCelGlist(cel_tag* cel, viewer_tag* viewer);
void View_DrawObjectGlist(obj_tag* obj, viewer_tag* viewer);
void View_DrawSky(viewer_tag* viewer, uint8_t layer);
// O objeto (e as partes de um objeto animado) não aparece em câmera nenhuma.
void View_SetDrawInNoViews(obj_tag* obj);
void Game_Draw();

// ---- chamados pelo desenho e ainda não portados (stubs em modules_stubs.cpp) ----
struct light_tag;
struct sphere_tag;
struct sprite;
void View_SetupRenderModes_Tweak(const viewer_tag* viewer, obj_tag* obj);
void View_SetupRenderModes(viewer_tag* viewer, obj_tag* obj);
void View_SetupRenderModes2(viewer_tag* viewer, obj_tag* obj);
void Emitter_Draw(obj_tag* obj, viewer_tag* viewer);
void Camera_Shear(viewer_tag* viewer);
void Rand_FRand_MVar2_Vec(_VECTOR* v, float a, float b);
uint32_t Sound_Play(uint32_t sound, float volume, int16_t a, uint32_t b);  // stub: 0
void DrawAllShards();
void DrawDrops();
P32<sprite>* Sprite_BuildList(uint16_t camera, uint32_t* count_a, uint32_t* count_b, uint32_t* count_c);
void View_DrawSprites(viewer_tag* viewer, P32<sprite>* list, uint32_t a, uint32_t b);

// ---- matemática usada pela visão ----
void Concat(const MATRIX* a, MATRIX* b);
void ApplyMatrixLVI(const MATRIX* m, _VECTOR* v);
void ApplyMatrixLV(const MATRIX* m, const _VECTOR* in, _VECTOR* out);
void auxVec_Add(const _VECTOR* a, const _VECTOR* b, _VECTOR* out);
float LimitRadAngle(float a);
uint32_t Rand_Random();
uint32_t Rand_Rand(uint32_t range);
void Camera_Shake(_VECTOR* pos, float amount);  // sacode as câmeras perto de pos (view.cpp)
float Rand_FRand(float range);
float Rand_FRandHalf(float range);
void Rand_FRandHalf_Vec(_VECTOR* v, float range);
float Rand_FRand_MVar2(float range, float minus);
extern uint32_t SEED_X, SEED_Y;  // o estado do sorteio (Rand_*)

// Só para testes: estado inicial das globais do módulo.
void view_debug_power_on();
