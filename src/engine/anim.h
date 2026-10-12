#pragma once
// Animação esquelética (módulo Anim do ACTION.ELF, 0x001bc6e8-0x001c7a80): esqueletos, peles,
// sequências e scripts de animação carregados do pacote 07F e dos arquivos 04/05/06xxxxxx.bin, e o
// objeto de animação de cada objeto animado (obj_tag::sphere_src): scripts tocando sequências, a
// pose resolvida por quadro (translação e quatérnio por osso), as matrizes dos ossos e a paleta
// que o desenho com skinning usa (psiDrawSkinObjectMatrix).
//
// Formatos em docs/formats.md ("Esqueletos e skins de animação", "Animação esquelética").

#include <cstdint>

#include "engine/collide.h"
#include "engine/object.h"

struct viewer_tag;

// Esqueleto (item SKEL do pacote 07F; AnimSkeletonProcess guarda o ponteiro em pSkeletons[id]).
struct ANIMSKEL_tag {
    uint16_t id;             // 0x00
    uint8_t bone_count;      // 0x02
    uint8_t pad;
    uint32_t trans_mask[3];  // 0x04 bit i: o osso i tem translação animada
    float trans[1][3];       // 0x10 bone_count x translação local padrão, relativa ao pai
};
constexpr int kSkeletonSlots = 128;
extern ANIMSKEL_tag* pSkeletons[kSkeletonSlots];

// Pele em tempo de jogo (0x40 bytes, montada por AnimProcessSkinData no lugar do arquivo 05xxxxxx
// na tabela de hashes).
struct alignas(16) ANIMSKIN_tag {
    uint32_t hash;                // 0x00 (AnimObjectNew)
    celglist_tag** meshes;        // 0x04 n_meshes modelos com skinning
    celglist_tag** discrete;      // 0x08 n_discrete modelos rígidos presos a ossos
    uint8_t* discrete_bones;      // 0x0c osso de cada um
    float scale[3];               // 0x10 (1, 1, 1 nos casos vistos)
    uint32_t unknown_1c;
    uint8_t* extras;              // 0x20 n_extra x 0x24 bytes: +4 osso, +8 translação, +0x14 quatérnio
    uint8_t* parents;             // 0x24 por osso: 0x80 | pai; 0xff na raiz
    MATRIX* inv_pose;             // 0x28 por osso (Quat_QuatTransToMat)
    uint8_t n_meshes;             // 0x2c
    uint8_t n_discrete;           // 0x2d
    uint8_t n_morphs;             // 0x2e alvos de morph (rosto)
    uint8_t n_extra;              // 0x2f
    uint8_t skeleton;             // 0x30 índice em pSkeletons
    uint8_t pad[0x0f];
};
static_assert(sizeof(ANIMSKIN_tag) == 0x40);

inline uint8_t skin_bone_count(const ANIMSKIN_tag* skin) { return pSkeletons[skin->skeleton]->bone_count; }

// Sequência carregada (arquivo 04xxxxxx.bin depois de AnimProcessSeqData: as três primeiras
// palavras viram ponteiro e tamanho dos canais).
struct ANIMRAWSEQ_tag {
    uint32_t* channels;      // 0x00 registros de canal (era o hash)
    uint32_t size;           // 0x04 bytes dos registros
    uint32_t* cursor;        // 0x08 (AnimFrameCopy)
    uint32_t flags;          // 0x0c bits 0-3: 7 = distância (passos)
    float sphere[4];         // 0x10
    float unknown_20[3];     // 0x20
    float unknown_2c[3];     // 0x2c
    int16_t frame_count;     // 0x38
    int16_t channel_count;   // 0x3a
    uint8_t unknown_3c;      // 0x3c
    uint8_t pad_3d;
    uint8_t unknown_3e;      // 0x3e
    uint8_t pad_3f;
};
static_assert(sizeof(ANIMRAWSEQ_tag) == 0x40);

// Sequência tocando (0xb0 bytes, AnimSeqNew).
struct alignas(16) ANIMSEQ_tag {
    _VECTOR root;            // 0x00 translação da raiz no quadro anterior
    _VECTOR root_delta;      // 0x10 quanto a raiz andou neste tick
    _VECTOR first;           // 0x20 a primeira (AnimFrameCopy, bit 0x100000 de flags)
    _VECTOR offset;          // 0x30 atual - primeira
    _VECTOR obj_pos;         // 0x40 posição do objeto ao começar
    float info_20[3];        // 0x50 da sequência (+0x20)
    float pad_5c;
    float info_2c[3];        // 0x60 da sequência (+0x2c)
    float pad_6c;
    float sphere[4];         // 0x70 da sequência (+0x10)
    void* distance;          // 0x80 tabela de distâncias (AnimDistanceTableGet)
    uint8_t* frame;          // 0x84 quadro decodificado: ossos x translação, ossos x quatérnio
    uint32_t* cursors;       // 0x88 cache de posição nos canais (bit 0x200000)
    void (*callback)(obj_tag*, ANIMSKIN_tag*, ANIMSEQ_tag*, void*);  // 0x8c
    ANIMRAWSEQ_tag* raw;     // 0x90
    uint32_t hash;           // 0x94
    uint32_t flags;          // 0x98
    float frame_pos;         // 0x9c quadro (1 = o primeiro)
    int16_t start;           // 0xa0
    int16_t first_frame;     // 0xa2
    int16_t last_frame;      // 0xa4
    int16_t frame_count;     // 0xa6
    uint8_t unknown_a8;      // 0xa8 (+0x3e da sequência)
    uint8_t skeleton;        // 0xa9
    uint8_t channels;        // 0xaa (+0x3a, modo de canal único)
    uint8_t pad[5];
};
static_assert(sizeof(ANIMSEQ_tag) == 0xb0);

// Script de animação tocando (0xc0 bytes, AnimScriptNew): uma lista ligada por objeto.
struct ANIMSCRIPT_tag;
using AnimScriptFn = void (*)(obj_tag*, void* anim_state, ANIMSCRIPT_tag*);
struct alignas(16) ANIMSCRIPT_tag {
    MATRIX rot;                // 0x00 rotação do objeto guardada (bit 0x20000: a raiz anda nela)
    uint32_t magic;            // 0x40 0x159f5ab2 vivo, 0x983c4837 apagado
    ANIMSCRIPT_tag* prev;      // 0x44
    ANIMSCRIPT_tag* next;      // 0x48
    ANIMSCRIPT_tag* sync;      // 0x4c script de referência (sincronia, AnimListBuild)
    ANIMSEQ_tag* seq[2];       // 0x50 corpo e rosto (morphs)
    void* blend;               // 0x58
    uint8_t* data;             // 0x5c o script (06xxxxxx)
    uint8_t* done_flag;        // 0x60 recebe 1 no fim
    ANIMSCRIPT_tag** owner;    // 0x64 zerado quando o script é apagado
    AnimScriptFn on_delete;    // 0x68
    AnimScriptFn on_end;       // 0x6c
    // 0x70 eventos do script (comando 4, tipo 2): fn(obj, estado, script, parâmetro)
    void (*event_fn)(obj_tag*, void*, ANIMSCRIPT_tag*, uint16_t);
    uint32_t hash;             // 0x74
    uint32_t flags;            // 0x78 0x10000000 terminou, 0x20000000 começou, bit 31 ao contrário
    uint32_t active_cmds;      // 0x7c bit por comando: o quadro estava dentro dele no tick anterior
    uint32_t tag;              // 0x80
    uint32_t stamp;            // 0x84 AnimScriptTimeStamp
    float sync_weight;         // 0x88 peso ao misturar no script de referência
    float unknown_8c;
    float frame;               // 0x90 quadro atual (1 a frame_count)
    float prev_frame;          // 0x94
    float speed;               // 0x98 quadros por tick (negativo: ao contrário)
    float norm_weight;         // 0x9c weight / weight_max
    float turn[2];             // 0xa0 bit 0x20000: ângulo inicial da raiz (>= 8: recomeçar), 0xa4 último
    float weight;              // 0xa8 sobe ou desce (unknown_b6) até weight_max
    float weight_max;          // 0xac
    int16_t frame_count;       // 0xb0
    uint8_t unknown_b2;        // 0xb2
    uint8_t unknown_b3;        // 0xb3
    uint8_t sync_mode;         // 0xb4 0 tempo, 1 distância, 2 fase do script de referência, 3 espelho
    uint8_t end_mode;          // 0xb5 0 para, 1 repete, 2 vai e volta, 3 para e apaga
    int8_t fade;               // 0xb6 > 0 entra, < 0 sai (apagado ao chegar a 0), 0 apaga já
    uint8_t pad[9];
};
static_assert(sizeof(ANIMSCRIPT_tag) == 0xc0);

// Conjunto de animações por velocidade (0x34 bytes, AnimSetAppend): parado ou andando, escolhido e
// misturado por AnimSetUpdate.
struct ANIMSET_tag {
    ANIMSET_tag* prev;            // 0x00
    ANIMSET_tag* next;            // 0x04
    uint32_t stamp_a;             // 0x08 carimbos dos dois scripts de andar em uso
    uint32_t stamp_b;             // 0x0c
    ANIMSCRIPT_tag* walk_script;  // 0x10 o segundo deles (o peso da mistura vai nele)
    ANIMSCRIPT_tag* blend;        // 0x14 passo lateral (multijogador)
    const uint32_t* table;        // 0x18 scripts por velocidade, 0
    const uint32_t* side;         // 0x1c depois: scripts de lado, 0
    uint32_t tag;                 // 0x20 marca dos scripts do conjunto (AnimTagGetUnique)
    float min_speed;              // 0x24
    float stride;                 // 0x28 metros por unidade de velocidade (walk_dir)
    uint16_t idle_time;           // 0x2c
    int8_t current;               // 0x2e velocidade escolhida (-1 nenhuma)
    int8_t strafe;                // 0x2f 0 nenhum, 1 direita, 2 esquerda
    uint8_t hold;                 // 0x30 ticks até poder trocar de novo
    uint8_t speed_count;          // 0x31
    uint8_t side_count;           // 0x32
    uint8_t transition;           // 0x33 1: esperando um script de transição terminar
};
static_assert(sizeof(ANIMSET_tag) == 0x34);

// Objeto de animação (0x120 bytes em obj_tag::sphere_src, AnimObjectNew). De 0x70 em diante é o
// sAnimObject_tag que as funções do módulo recebem.
struct alignas(16) ANIMOBJ_tag {
    // 0x00 o chão sob o objeto: cópia do acerto de colisão (Player_FeetOnPoint,
    // Player_DealWithObjHit). surface (0x50) é o material; obj (0x58), o objeto pisado.
    HITDATA_tag floor;
    // 0x60 bits: 1 mirando, 4 passo neste tick, 8 no chão, 0x10 encostou em algo, 0x20 algo acima
    // (teto), 0x80 sobre um objeto, 0x100 na água.
    uint16_t flags;
    int8_t weapon;               // 0x62 índice em weapon_data (a arma na mão)
    int8_t next_weapon;          // 0x63 arma pedida (a troca acontece em Player_SetWeaponAnimObj)
    int8_t prev_weapon;          // 0x64 arma anterior (a que está sendo guardada)
    uint8_t unknown_65[0x0b];
    // 0x70 começo do sAnimObject_tag
    void* head_target;           // 0x70 objeto que a cabeça acompanha (AnimObjectSetHeadTracking)
    const void* aim_target;      // 0x74 ponto para onde a mira aponta (AnimObjectSetAiming)
    uint8_t unknown_78[0x08];
    _VECTOR sphere;              // 0x80 esfera local (obj_tag::mode bit 8), da sequência
    void (*root_motion)(obj_tag*, void*, _VECTOR*);  // 0x90 ajusta o quanto a raiz anda no mundo
    ANIMSKIN_tag* skin;          // 0x94
    ANIMSET_tag* sets;           // 0x98
    ANIMSCRIPT_tag* scripts;     // 0x9c
    // 0xa0 depois de resolver a pose: fn(obj, estado, ossos, translações, quatérnios)
    void (*pose_fn)(obj_tag*, void*, uint32_t, uint8_t*, uint8_t*);
    celglist_tag** extra_glists; // 0xa4 n_extra
    celglist_tag* mesh_override; // 0xa8 troca o primeiro modelo da pele
    uint8_t* frame;              // 0xac quadro resolvido: ossos x translação (16), ossos x quatérnio
    float* morph_weights;        // 0xb0 n_morphs
    int8_t* extra_timers;        // 0xb4 n_extra
    MATRIX* bones;               // 0xb8 matriz de cada osso no mundo
    MATRIX* palette;             // 0xbc bones x pose inversa, para o skinning
    obj_tag** attached;          // 0xc0 objetos rígidos presos aos ossos (n_discrete)
    uint32_t tick;               // 0xc4 GameState.ticks da última atualização
    uint32_t anim_flags;         // 0xc8 2 personagem (>= 71 ossos), 4 matrizes prontas, 8 paleta pronta...
    float root_height;           // 0xcc altura da raiz (quadril) sobre os pés, da animação
    float root_offset;           // 0xd0
    float walk_dir;              // 0xd4 sentido e escala do passo (AnimDistanceTableDistanceToFrame2)
    float speed;                 // 0xd8 distância andada no último tick (Player_Update)
    float walk_pos;              // 0xdc distância acumulada no ciclo de passos
    float head_speed;            // 0xe0 rapidez do giro da cabeça
    float aim_speed;             // 0xe4 rapidez da mira (AnimObjectSetAiming)
    float head_dist_sq;          // 0xe8 alcance do acompanhamento ao quadrado
    float aim_dist_sq;           // 0xec alcance da mira ao quadrado
    float head_yaw;              // 0xf0 giro da cabeça em curso, em volta do eixo coluna-cabeça (AnimObjectHeadTrack)
    float aim_yaw;               // 0xf4 (AnimObjectAimAt)
    float head_pitch;            // 0xf8 inclinação da cabeça em curso, em volta do eixo do lado
    float aim_pitch;             // 0xfc (AnimObjectAimAt)
    uint16_t frame_size;         // 0x100 bytes de frame (ossos x 32)
    int16_t face_timer;          // 0x102 ticks até a próxima piscada (AnimFacialUpdate; negativo: pisca)
    uint8_t head_bone;           // 0x104 com anim_flags 0x40
    uint8_t aim_bone;            // 0x105 com anim_flags 0x200 (AnimObjectSetAiming)
    int8_t head_delay;           // 0x106 ticks até começar a acompanhar
    // 0x107 e 0x108: para onde os olhos olham, 0 a 255 (AnimObjectHeadTrack; 127 no meio, 0 sem acompanhar).
    // AnimFacialBlend os leva aos alvos de morph 14/13 (de lado: eye_x / 255 e 1 - eye_x / 255) e 11/12 (para cima e
    // para baixo, com eye_y).
    uint8_t eye_x;
    uint8_t eye_y;
    // 0x109 o estado do rosto (AnimFacialUpdate: 0 nenhum, 1 parado, 2 falando; bit 0x80: a próxima piscada vem
    // logo, a piscada dupla das mulheres); AnimFacialAppend põe 2
    uint8_t face_state;
    uint8_t unknown_10a[6];
    uint8_t closeness[4];        // 0x110 distância por câmera (rosto e morphs só de perto: < 13)
    uint8_t unknown_114[0x0c];
};
static_assert(sizeof(ANIMOBJ_tag) == 0x120);
static_assert(offsetof(ANIMOBJ_tag, flags) == 0x60 && offsetof(ANIMOBJ_tag, skin) == 0x94);
static_assert(offsetof(ANIMOBJ_tag, root_height) == 0xcc && offsetof(ANIMOBJ_tag, speed) == 0xd8);
static_assert(offsetof(ANIMOBJ_tag, frame_size) == 0x100 && offsetof(ANIMOBJ_tag, closeness) == 0x110);
static_assert(offsetof(ANIMOBJ_tag, face_timer) == 0x102 && offsetof(ANIMOBJ_tag, eye_x) == 0x107 &&
              offsetof(ANIMOBJ_tag, face_state) == 0x109);

inline ANIMOBJ_tag* anim_of(obj_tag* obj) { return reinterpret_cast<ANIMOBJ_tag*>(obj->sphere_src); }
// O sAnimObject_tag (o que vem depois do chão e das flags do jogador).
inline void* anim_state(ANIMOBJ_tag* anim) { return reinterpret_cast<uint8_t*>(anim) + 0x70; }
inline ANIMOBJ_tag* anim_from_state(void* state) {
    return reinterpret_cast<ANIMOBJ_tag*>(static_cast<uint8_t*>(state) - 0x70);
}

// Globais do módulo.
extern float Glb_SkinScale[4];     // escala da pele em atualização (0x00319d80)
extern float Glb_InvSkinScale[4];  // 0x00319d90
extern int32_t GlbAnimList;        // scripts escolhidos por AnimListBuild
extern ANIMSCRIPT_tag* GlbAnimListItems[32];
extern uint8_t Glb_bScriptDeleted;
extern ANIMSCRIPT_tag* Glb_pCurrentScript;
extern uint8_t ReverseFlag;
extern uint8_t ScriptFootstepFoot;  // pé do último passo
extern uint8_t ScriptFootstepType;  // 1 ou 2 neste tick (comando de evento), 0 sem passo
extern uint8_t Morph_Count;         // pesos de morph para o próximo desenho com skinning
extern uint8_t Morph_Index[22];
extern float Morph_Weight[22];

// ---- carga ----
void AnimPreLoadInit(int8_t warm);
void AnimLoadFile(uint32_t hash, int8_t no_process);
void AnimSkeletonProcess(void* data);
void AnimProcessSkinData(uint32_t hash);
void AnimProcessSeqData(uint32_t hash);
void AnimProcessScriptData(uint32_t hash);
void AnimPostLoadInit();
uint32_t AnimSleeveGetEntity(uint32_t index);
void AnimDistanceTableInit();
void* AnimDistanceTableGet(uint32_t seq);
float AnimDistanceTableDistanceToFrame2(obj_tag* obj, void* state, ANIMSCRIPT_tag* s);

// ---- objetos ----
uint32_t AnimObjectNew(obj_tag* obj, uint32_t skin);
void AnimObjectDelete(obj_tag* obj);
uint32_t AnimObjectUpdate(obj_tag* obj);
float AnimObjectInitialPose(obj_tag* obj);
void AnimObjectDraw(obj_tag* obj, viewer_tag* viewer, uint8_t a, uint8_t b, uint8_t c, int8_t d);
void AnimGetBoneWorldTrans(obj_tag* obj, uint32_t bone, int32_t flag, _VECTOR* out, MATRIX* m);
// Como AnimGetBoneWorldTrans, mas da pose da animação (sem a mira e o acompanhamento da cabeça), montando a cadeia do
// osso até a raiz (AnimObjectAimAt).
void AnimGetBoneWorldTrans2(obj_tag* obj, uint32_t bone, int32_t flag, _VECTOR* out, MATRIX* m);
void psiBuildMatrixPalette(obj_tag* obj, void* state, int8_t palette);

// ---- scripts e sequências ----
ANIMSCRIPT_tag* AnimScriptAdd(obj_tag* obj, uint32_t script);
void AnimScriptAddSpeed(obj_tag* obj, uint32_t script, float speed);
ANIMSCRIPT_tag* AnimScriptAddReverse(obj_tag* obj, uint32_t script);
void AnimScriptSetPlayDirection(obj_tag* obj, int32_t dir);
void AnimObjectSetSleeveType(obj_tag* obj, uint32_t type);
ANIMSCRIPT_tag* AnimScriptNew(obj_tag* obj, ANIMSCRIPT_tag* after);
void AnimScriptInit(obj_tag* obj, ANIMSCRIPT_tag* s, uint32_t script);
void AnimScriptDelete(obj_tag* obj, ANIMSCRIPT_tag** s);
bool AnimScriptIsStopped(obj_tag* obj);
void AnimScriptTick(obj_tag* obj, void* state, ANIMSCRIPT_tag* s);
float AnimScriptEnd(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, float frame);
void AnimProcessScriptCmds(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, int32_t from, int32_t to);
ANIMSCRIPT_tag* AnimListBuild(obj_tag* obj, uint32_t criteria, uint32_t value,
                              void (*fn)(obj_tag*, ANIMSCRIPT_tag*), uint32_t mode);
void AnimListDelete(obj_tag* obj);
int32_t AnimTagGetUnique();
ANIMSEQ_tag* AnimSeqNew(uint32_t frame_size, uint32_t tag);
void AnimSeqDelete(ANIMSEQ_tag** seq);
ANIMRAWSEQ_tag* AnimSeqGetInfo(uint32_t hash, ANIMSEQ_tag* seq, ANIMRAWSEQ_tag* raw);
ANIMSEQ_tag* AnimSeqSet(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, ANIMRAWSEQ_tag* raw, uint32_t hash,
                        int32_t start, int32_t slot);
void AnimSeqTick(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, ANIMSEQ_tag* seq);
void AnimFrameCopy(void* out, ANIMSEQ_tag* seq, ANIMSKIN_tag* skin, float frame);
void AnimFrameSet(obj_tag* obj, ANIMSEQ_tag* seq, ANIMSKIN_tag* skin, float t);
void AnimFrameBlend(obj_tag* obj, void* out, void* a, void* b, uint32_t bones, float t, void* bone_blend);
void AnimFrameResolve(obj_tag* obj, void* state);
float* AnimDistanceTableCreate(uint32_t seq, int32_t axes, int32_t open);
int32_t AnimDatumGetWeaponInfo(obj_tag* obj, uint32_t* muzzle, uint32_t* flash, uint16_t* kind);
int32_t AnimDatumGetIndex(obj_tag* obj, int32_t bone);
void AnimDatumSetEntity(obj_tag* obj, int32_t bone, uint32_t hash, uint32_t ticks);
bool AnimObjectIsClose(obj_tag* obj, uint32_t limit);
void AnimFireWeapon(obj_tag* obj);  // só no port: o comando de tiro de AnimProcessScriptCmds
// Mira e acompanhamento da cabeça (portadas em drone_move.cpp, junto do olhar dos personagens).
bool AnimObjectIsAiming(obj_tag* obj);
bool AnimObjectIsHeadTracking(obj_tag* obj);
void AnimObjectSetAiming(obj_tag* obj, const void* target, float dist, float speed, int8_t use_bone, uint32_t bone);
// ---- rostos (morphs; docs/formats.md "Morphs do rosto", docs/engine.md "Rostos") ----
// Um rosto é uma pele com n_morphs alvos (22 no disco: 0-9 a boca, 10 piscar, 11/12 olhos para cima/baixo, 13/14 olhos
// de lado, 15 sobrancelhas, 16-21 expressões e o queixo). Os pesos vêm da sequência de rosto (o slot 1 de cada script,
// modo de canal único: 22 canais, um peso cada) dos scripts marcados: 0xe0000000 a expressão parada, 0xe0000001 a
// piscada, 0xe0000002 a fala (AnimFacialAppend) ou, numa cena (ScriptCam), todo script novo da pele.
// AnimFacialUpdate escolhe os scripts (o estado em ANIMOBJ_tag::face_state), AnimFacialBlend junta os pesos em
// ANIMOBJ_tag::morph_weights, AnimObjectDraw escolhe os 8 maiores (Morph_Count/Index/Weight) e o desenho com skinning
// os soma às posições da pose de repouso (o SkinIt do PS2; no PC, o shader: render/renderer.h).
constexpr int kMaxMorphs = 22;  // Morph_Index[22], Morph_Weight[22], o quadro de rosto de 0x58 bytes
// Os scripts de rosto por estado de alerta (Drone_AlertStatus: 0 parado ... 3) e, no índice 4, a piscada. Os homens
// (anim_flags 0x400: esqueleto diferente de 1) e as mulheres (esqueleto 1) têm os seus.
extern const uint32_t Male_Facial_State_Anims[5];    // 0x002c6ea8
extern const uint32_t Female_Facial_State_Anims[5];  // 0x002c6ec0
// O script de rosto de uma fala (o som está nele): no fim da lista, marcado 0xe0000002, apaga-se ao acabar.
ANIMSCRIPT_tag* AnimFacialAppend(obj_tag* obj, uint32_t script);
// Um tick do rosto (fora de cena): a expressão do estado de alerta, as piscadas (de 1 a 4 s; as mulheres às vezes
// piscam devagar ou duas vezes) e a volta ao estado parado quando a fala acaba.
void AnimFacialUpdate(obj_tag* obj, void* state);
// Os pesos dos morphs (ANIMOBJ_tag::morph_weights) a partir dos quadros de rosto dos scripts marcados e dos olhos.
void AnimFacialBlend(obj_tag* obj, void* state, ANIMSKIN_tag* skin);
// A cabeça acompanha o alvo de AnimObjectSetHeadTracking (chamada por psiBuildMatrixPalette no osso 5): gira o
// quatérnio e a matriz do osso no mundo e, nos rostos, leva os olhos (eye_x, eye_y) ao que falta girar.
void AnimObjectHeadTrack(obj_tag* obj, void* state, quaternion_tag* world, MATRIX* bones, uint32_t bone);
// A mira do tronco (chamada por psiBuildMatrixPalette no osso 2, com anim_flags 0x80/0x100): gira a coluna para o cano
// da arma apontar para o alvo de AnimObjectSetAiming (aim_yaw, aim_pitch); os braços e a arma seguem.
void AnimObjectAimAt(obj_tag* obj, void* state, quaternion_tag* world, MATRIX* bones, uint32_t bone);
// Só do port: AnimObjectAimAt com mira pedida e alvo nulo (o original leria o endereço 0).
extern uint32_t g_anim_aim_guards;
// Só do port (depuração, --no-torso-aim): AnimObjectAimAt volta sem fazer nada, como o stub de antes do port.
extern bool g_anim_aim_off;
// Só do port: casos em que o original escreveria num script nulo (AnimScriptAppend sem lugar: 32 scripts) ou leria
// um alvo nulo (AnimObjectHeadTrack).
extern uint32_t g_anim_face_guards;

// Chamadas pelo módulo e ainda não portadas (stubs em modules_stubs.cpp).
void SFXPreCache(uint32_t sound, int32_t frames);  // platform/psi_sound.h
void Sound_SetAlertness(uint32_t handle, float level);
void View_DrawGlist(celglist_tag* glist, _VECTOR* pos, _VECTOR* rot, _VECTOR* scale, uint32_t created);

// ---- conjuntos ----
void AnimSetDeleteAll(obj_tag* obj);
void AnimSetInit(obj_tag* obj, ANIMSET_tag* set, const uint32_t* table, float min_speed, float stride);
ANIMSET_tag* AnimSetAppend(obj_tag* obj, const uint32_t* table, float min_speed, float stride);
void AnimSetUpdate(obj_tag* obj, ANIMSET_tag* set, float speed, float forward, float max_speed, float side,
                   float side_max);
void AnimSet_WalkUpdater(obj_tag* obj, ANIMSKIN_tag* skin, ANIMSEQ_tag* seq, void* script);
ANIMSCRIPT_tag* AnimScriptAppend(obj_tag* obj, uint32_t script);
ANIMSCRIPT_tag* AnimScriptAppendBlend(obj_tag* obj, uint32_t script);
// A translação da raiz de um quadro de uma sequência, sem objeto (script_support.cpp).
void AnimGetFrameTransRaw(uint32_t hash, int32_t frame, _VECTOR* out);
void PlayerAnimSetInit(obj_tag* obj);
void PlayerAnimSetInitNormal(obj_tag* obj);
void PlayerAnimSetInitCrouch(obj_tag* obj);
void PlayerAnimCrouch2Stand(obj_tag* obj);
void PlayerAnimStand2Crouch(obj_tag* obj);

// Só para testes: estado inicial das globais do módulo.
void anim_debug_power_on();
