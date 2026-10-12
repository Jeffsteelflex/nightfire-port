#pragma once
// Scripts de cena (módulo Script do ACTION.ELF, 0x001d8088-0x001db9a8) e o "script player" que os toca
// (SP_*, 0x001db9a8-0x001dcc68), mais os objetos que dirigem scripts: interruptores (Switch_*) e caixas
// de fusíveis (FuseBox_*). Ver docs/engine.md, "Script player", e docs/formats.md, "Scripts de cena".
//
// Um script (item tipo 7 de um pacote de fase, hash 06xxxxxx; também o único item tipo 7 de cada
// seção carregável de cena NIS) é uma lista de streams que andam juntos no tempo (quadros a 60 Hz).
// Cada stream é uma fila de comandos: esperar um quadro, criar uma entidade ou uma luz que segue os
// quadros-chave do stream, tocar uma animação, um som, um texto, mover a câmera, ligar canais,
// pausar, chamar um sub-script. Script_Load registra o protótipo na carga (com os dados) e cria uma
// instância (SCRIPTINFO) quando chamado sem dados; Script_Update anda um tick.
//
// O script player é um objeto do tipo 0x1b (posicionamento 0xd9, ou SP_Create por um switch, uma caixa
// de fusíveis ou o filme de abertura) com até dois scripts (A e B) e uma máquina de estados: espera um
// gatilho (canal, tiro, uso, toque do jogador ou de um personagem), toca, troca de script, repete.
//
// Divergências do port (detalhes em cada função):
//  - Script_Load: com mais de 0x3e streams o original chama DList_RemoveFromInUse2Free num bloco que
//    não está na lista (corromperia ScriptList); o port pula a chamada. Nenhum script do disco passa
//    de 53 streams.
//  - Script_EventHandler: os argumentos vão para um buffer de 8 palavras na pilha; o port ignora os
//    que passariam disso (o disco tem no máximo 4).
//  - Script_CreateEntity: os criadores que no ELF são void mas deixam o objeto em v0
//    (Trigger_TouchOnce, Trigger_Touch, Emitter_CreatePlist) devolvem o objeto no port; com o criador
//    devolvendo nulo (stubs), o port não chama control_unlink_object(nulo).
//  - Switch_Create: o laço que roda o script até ele parar tem um limite (o original travaria num script
//    sem pausa).
//  - SSysItems: índices fora da tabela são ignorados (o original escreveria fora dela).
//  - Só no port: Script_ActiveCamera diz ao renderizador quando a câmera de script (a 4) substitui a 0.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/player_stats.h"

struct light_tag;

// Quadro-chave de um stream (0x30 bytes, KEYED_POSROT).
struct alignas(16) KEYED_POSROT {
    _VECTOR pos;            // 0x00 (w = 1)
    quaternion_tag quat;    // 0x10 (x, y, z, w)
    float scale;            // 0x20 entidade: escala; câmera: campo de visão (radianos / proporção)
    float alpha;            // 0x24 entidade: 0..1 (vira obj_tag::unknown_103[3]); luz: bytes em 0x24..0x26
    uint32_t unknown_28;    // 0x28 (1.0 em todo o disco; não lido)
    float frame;            // 0x2c quadro do script
};
static_assert(sizeof(KEYED_POSROT) == 0x30);

// Câmera de um stream de câmera (0x50 bytes, Script_CameraStart).
struct alignas(16) CAM_INFO {
    MATRIX m;               // 0x00 câmera no mundo (linha 2 para a frente, linha 3 posição)
    float fov;              // 0x40 escala do quadro-chave x SCRIPTINFO::scale
    uint16_t first_key;     // 0x44
    uint16_t last_key;      // 0x46 a interpolação usa last_key - 1
    uint16_t interp;        // 0x48 2 linear, 4 spline
    uint8_t pad[6];
};
static_assert(sizeof(CAM_INFO) == 0x50);

// Stream de uma instância (0x28 bytes, SSTREAM).
struct SSTREAM {
    float start;            // 0x00 animação: tempo do stream ao começar (Script_AnimStart)
    float anim_frame;       // 0x04 animação: quadro inicial
    float time;             // 0x08 quadros desde o começo (anda FRAME_RATE_MUL por tick)
    float wait;             // 0x0c comandos param até time chegar aqui (comando 5)
    void* obj;              // 0x10 o que o stream controla: obj_tag (1, 2), sprite (3), CAM_INFO (4),
                            //      som (5), light_tag (6), SCRIPTINFO (7)
    uint8_t* cursor;        // 0x14 próximo comando (nulo: acabou)
    uint8_t* commands;      // 0x18 primeiro comando (volta para cá no laço)
    uint16_t first_key;     // 0x1c quadros-chave do stream
    uint16_t last_key;      // 0x1e
    // 0x20 entidade e sub-script: quadro de criação + 1 (1: criado no começo, fica no laço);
    //      som: stream do objeto que leva o som
    uint16_t u20;
    // 0x22 entidade: 2 spline, 1 (evento 14) some no fim do laço; som: quadro de início + 1
    uint16_t u22;
    uint8_t type;           // 0x24 0 vazio, 1 entidade, 2 animação, 3 sprite, 4 câmera, 5 som, 6 luz, 7 sub-script
    int8_t parent;          // 0x25 stream cuja posição soma à deste (-1 nenhum)
    uint8_t pad[2];
};
static_assert(sizeof(SSTREAM) == 0x28);

constexpr int kScriptMaxStreams = 0x3e;

// Instância de um script (0xac0 bytes, SCRIPTINFO; Script_Load sem dados).
struct alignas(16) SCRIPTINFO {
    MATRIX base;            // 0x000 lugar do script (View_RotTransMatrix na carga; Script_SetPosRot)
    MATRIX player;          // 0x040 onde o jogador volta no fim (evento 6)
    void* owner;            // 0x080 dono (o SCRIPTPLAYER_tag); vai para obj_tag::script das entidades
    void (*callback)(obj_tag*, uint16_t, void*);  // 0x084 evento 15: (objeto anterior, argumento, user)
    void* user;             // 0x088
    KEYED_POSROT* keys;     // 0x08c
    uint32_t unknown_90;
    float scale;            // 0x094 escala das posições e da câmera (SP: parâmetro 10, em %)
    uint32_t hash;          // 0x098
    int32_t wait;           // 0x09c ticks de pausa (evento 3: argumento - 1; -1 pausa até SP_UnPause)
    float time;             // 0x0a0 quadros desde o começo
    float fade;             // 0x0a4 avanço rápido (Script_FFwd): ticks de fade decorridos
    SSTREAM streams[kScriptMaxStreams];  // 0x0a8
    uint8_t unknown_a58[0x50];
    // 0xaa8 0x10 entidades ficam no fim, 0x20 pula câmera e sons, 0x40 o script tem câmera,
    // 0x80 religa a IA no fim, 0x100 devolve o jogador no fim (eventos 12, 16, 7, 6)
    uint16_t flags;
    uint16_t stream_count;  // 0xaaa
    uint16_t key_count;     // 0xaac
    uint16_t loop_start;    // 0xaae quadro para onde o laço volta
    uint16_t end;           // 0xab0 último quadro
    uint8_t play;           // 0xab2 1 tocando, 2 terminou, 8 em laço, 0x10 avançando (FFwd)
    uint8_t cur;            // 0xab3 stream em execução (Script_Run)
    uint8_t fade_ticks;     // 0xab4 duração do fade do avanço rápido
    uint8_t state;          // 0xab5 1 avanço rápido pedido, 0x10 câmera ligada, 0x20 fade acabou (pula)
    uint8_t player_flag;    // 0xab6 terceiro argumento de Player_Enable (evento 6)
    uint8_t pad[9];
};
static_assert(sizeof(SCRIPTINFO) == 0xac0);
static_assert(offsetof(SCRIPTINFO, keys) == 0x8c && offsetof(SCRIPTINFO, streams) == 0xa8);
static_assert(offsetof(SCRIPTINFO, flags) == 0xaa8 && offsetof(SCRIPTINFO, play) == 0xab2);

// Protótipo de um script (0x214 bytes, na tabela de hashcodes no lugar do item 06xxxxxx; -1 se o item
// não é um script). Aponta para dentro do arquivo carregado.
struct SCRIPTDEF_tag {
    uint32_t hash;          // 0x000
    struct Stream {
        uint16_t first_key;
        uint16_t last_key;
        uint8_t* commands;
    } streams[kScriptMaxStreams];  // 0x004
    uint8_t unknown_1f4[0x10];
    KEYED_POSROT* keys;     // 0x204
    uint16_t stream_count;  // 0x208
    uint16_t key_count;     // 0x20a
    uint16_t loop_start;    // 0x20c
    uint16_t end;           // 0x20e
    uint16_t camera;        // 0x210
    uint16_t pad;
};
static_assert(sizeof(SCRIPTDEF_tag) == 0x214);
static_assert(offsetof(SCRIPTDEF_tag, keys) == 0x204 && offsetof(SCRIPTDEF_tag, camera) == 0x210);

// Dados de um script player (0x3c bytes em obj->data, tipo 0x1b). Parâmetros do posicionamento 0xd9:
// [0]/[1] scripts A e B, [2]/[3] modo de cada um, [4] para no fim, [5] gatilhos, [6] canal, [7] dano
// para disparar, [8] arma que dispara, [9] canal que liga o player, [10] escala em %.
struct SCRIPTPLAYER_tag {
    obj_tag* owner;         // 0x00 o switch ou a caixa de fusíveis (nulo no posicionamento 0xd9)
    void (*callback)(obj_tag*, uint16_t, void*);  // 0x04 passado ao primeiro Script_Load e zerado
    void* user;             // 0x08
    SCRIPTINFO* scripts[2]; // 0x0c
    float damage_needed;    // 0x14 dano acumulado que dispara (0: tiros não disparam)
    float damage;           // 0x18
    uint32_t hashes[2];     // 0x1c
    // 0x24 0 espera o gatilho e carrega, 1 carregado e parado, 2 tocando, 3 tocou (espera para trocar),
    // 4 tocando o outro, 5 acabou (apaga o objeto), 6 em laço (o gatilho solta as pausas), 7 carrega
    uint16_t state;
    // 0x26 modo de cada script: 0 só carrega no gatilho, 1 toca já, 8 em laço, outros carregam parados
    uint16_t modes[2];
    uint16_t cur;           // 0x2a script em uso (0 ou 1)
    uint16_t unpause;       // 0x2c SP_UnPause / SP_Activate: dispara no próximo tick (bit 4)
    // 0x2e o que dispara: 1 o canal (e só com ele ligado se 4), 2 tiros nas entidades, 4 usar,
    // 8 o jogador encosta, 0x10 um personagem encosta
    uint16_t triggers;
    uint16_t channel;       // 0x30 canal: dispara quando ligado; o player o escreve ao tocar
    uint16_t enable;        // 0x32 só atualiza com este canal ligado (0: sempre)
    uint8_t cooldown;       // 0x34 ticks sem olhar os gatilhos depois de tocar
    uint8_t one_shot;       // 0x35 parâmetro 4: no fim vai para o estado 5 (apaga)
    uint8_t hidden;         // 0x36 SP_HideObj: não atualiza
    uint8_t weapon;         // 0x37 parâmetro 8: só tiros desta arma (0: qualquer)
    uint8_t load_requested; // 0x38 já pediu a tela de carga da seção (GameFlow 0xd)
    uint8_t pad[3];
};
static_assert(sizeof(SCRIPTPLAYER_tag) == 0x3c);
static_assert(offsetof(SCRIPTPLAYER_tag, hashes) == 0x1c && offsetof(SCRIPTPLAYER_tag, state) == 0x24);
static_assert(offsetof(SCRIPTPLAYER_tag, triggers) == 0x2e && offsetof(SCRIPTPLAYER_tag, cooldown) == 0x34);

// Interruptor (0x1c bytes em obj->data, tipo 0x39). O modelo é a entidade do script.
struct SWITCH_tag {
    LLNODE_tag node;        // 0x00 SwitchList
    obj_tag* obj;           // 0x08
    obj_tag* player;        // 0x0c script player (SP_Create com o script do parâmetro 1, modo 8)
    uint16_t timer;         // 0x10 parâmetro 2: ticks até voltar sozinho (0: não volta)
    uint16_t channel;       // 0x12 parâmetro 0
    uint16_t lock;          // 0x14 parâmetro 5: com este canal ligado o switch não vira
    uint16_t initial;       // 0x16 parâmetro 3: estado inicial (obj_tag::state)
    uint8_t uses;           // 0x18 vezes que virou
    uint8_t once;           // 0x19 parâmetro 4: só vira uma vez
    uint8_t pad[2];
};
static_assert(sizeof(SWITCH_tag) == 0x1c);

// Caixa de fusíveis (0x60 bytes em obj->data, tipo 0x3c). O script registra os fusíveis (evento 15,
// FuseBox_Register) em duas listas; quando todos os de uma lista levam tiro, liga os canais dela.
struct FUSEBOX_tag {
    obj_tag* player;        // 0x00 script player (SP_Create com o script do parâmetro 0, modo 0x40)
    obj_tag* fuses[2][10];  // 0x04
    uint16_t channel_a[2];  // 0x54 parâmetros 1 e 2 (o da lista 1 também: 0 = quebrar fere o jogador)
    uint16_t count[2];      // 0x58 fusíveis registrados em cada lista
    uint16_t channel_b[2];  // 0x5c parâmetros 3 e 4
};
static_assert(sizeof(FUSEBOX_tag) == 0x60);

// ---- módulo Script (script.cpp) ----
struct sprite;
struct SpriteInfo;
extern sprite* Borders;             // 0x0030d068 tarja de cinema de cima
extern sprite* Borders2;            // 0x0030d06c a de baixo
extern SpriteInfo BorderInfo;       // 0x002c9468 a descrição delas (copiada do ACTION.ELF)
extern level_tag tptr;              // 0x00349948 posicionamento falso das entidades de script

void GetSplineWeights(int32_t* keys, float* weights, float time, KEYED_POSROT* first, int32_t count);
SCRIPTINFO* Script_Load(uint32_t hash, _VECTOR* pos, _VECTOR* rot, uint8_t* data, void* owner,
                        void (*callback)(obj_tag*, uint16_t, void*), void* user);
void Script_Run(SCRIPTINFO* si);
void Script_SoundStart(SCRIPTINFO* si, SSTREAM* st);
obj_tag* Script_CreateEntity(SSTREAM* st, uint32_t hash, uint8_t type);
void Script_EntityStart(SCRIPTINFO* si, SSTREAM* st);
void Script_AnimStart(SCRIPTINFO* si, SSTREAM* st);
void Script_CameraStart(SCRIPTINFO* si, SSTREAM* st);
void Script_SpriteStart(SCRIPTINFO* si, SSTREAM* st);
void Script_EventHandler(SCRIPTINFO* si, SSTREAM* st);
void Script_KillStream(SCRIPTINFO* si, SSTREAM* st);
void Script_Interp(SCRIPTINFO* si, SSTREAM* st);
void Script_GetInterp(SCRIPTINFO* si, SSTREAM* st, uint16_t first, uint16_t last, MATRIX* out, float* scale,
                      uint32_t* colour, uint16_t mode, int8_t concat, float pos_scale);
void Script_Free(SCRIPTINFO* si);
void Script_Update(SCRIPTINFO* si);
void Script_FFwd(SCRIPTINFO* si, int8_t on);
uint32_t Script_IsDeathNIS(uint32_t hash);
void Script_Init();
void Script_FreeFromHashTable(uint32_t hash);
void Script_Set2Start(SCRIPTINFO* si);
void Script_SoundEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_EntityEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_SubScriptStart(SCRIPTINFO* si, SSTREAM* st);
void Script_SubScriptEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_LightStart(SCRIPTINFO* si, SSTREAM* st);
void Script_LightEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_TextStart(SCRIPTINFO* si, SSTREAM* st);
void Script_AnimEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_CameraEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_SpriteEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_FadeStart(SCRIPTINFO* si, SSTREAM* st);
void Script_StreamEnd(SCRIPTINFO* si, SSTREAM* st);
void Script_UpdateCamera(CAM_INFO* cam);
void Script_Play(SCRIPTINFO* si, uint8_t loop);
uint32_t Script_IsPlaying(SCRIPTINFO* si);
void Script_Stop(SCRIPTINFO* si);
obj_tag* Script_GetObj(SCRIPTINFO* si, uint16_t stream);
void Script_RemoveObj(obj_tag* obj, SCRIPTINFO* si);
void Script_SetColour(SCRIPTINFO* si, uint8_t r, uint8_t g, uint8_t b);
void Script_HideObj(SCRIPTINFO* si, int8_t hide);
void Script_SetPosRot(SCRIPTINFO* si, MATRIX* m);

// ---- script player (script_player.cpp) ----
obj_tag* SP_CreateScriptPlayer(_VECTOR* pos, _VECTOR* rot, level_tag* level, obj_tag* owner,
                               void (*callback)(obj_tag*, uint16_t, void*), void* user);
void SP_LoadScript(obj_tag* obj, SCRIPTPLAYER_tag* sp);
void SP_Update(obj_tag* obj);
void SP_Activate(obj_tag* obj, obj_tag* by);
uint16_t SP_Hit(SCRIPTPLAYER_tag* sp);
float SP_GetHitDamage(obj_tag* obj, obj_tag** objs, uint16_t max, uint16_t* count, uint16_t filter,
                      int8_t* by_player);
obj_tag* SP_Create(_VECTOR* pos, _VECTOR* rot, uint32_t hash, uint32_t hash_b, uint32_t mode, uint32_t one_shot,
                   obj_tag* owner, void (*callback)(obj_tag*, uint16_t, void*), void* user, uint32_t triggers);
void SP_Delete(obj_tag* obj);
uint8_t SP_GetState(obj_tag* obj);
void SP_RemoveObj(obj_tag* obj, void* sp);
void SP_SetPos(obj_tag* obj, _VECTOR* pos);
void SP_SetPosRot(obj_tag* obj, MATRIX* m);
void SP_UnPause(obj_tag* obj);
void SP_SetColour(obj_tag* obj, uint8_t r, uint8_t g, uint8_t b);
void SP_SwitchToScript(obj_tag* obj, uint16_t mode, void (*callback)(obj_tag*, uint16_t, void*));
void SP_HideObj(obj_tag* obj, int8_t hide);
// O original devolve o SCRIPTINFO em uso; void* para casar com a declaração em ainetwork.h.
void* SP_getScriptInfo(obj_tag* obj);

// ---- interruptores e caixas de fusíveis (script_player.cpp) ----
extern LLISTINFO_tag SwitchList;    // 0x002bf0f8
extern LLISTINFO_tag CameraList;    // câmeras de vigilância (Sensor_*, ainda não portado)
void Sensor_Init();
obj_tag* Switch_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Switch_Update(obj_tag* obj);
void Switch_Activate(obj_tag* obj, obj_tag* by);
obj_tag* Switch_GetNearest(_VECTOR* pos);
uint16_t Switch_GetSwitchChannel(obj_tag* obj);
obj_tag* FuseBox_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void FuseBox_Update(obj_tag* obj);
void FuseBox_Register(obj_tag* fuse, uint16_t list, void* box);

// ---- de outros módulos, usados pelos scripts (script_support.cpp) ----
struct STACKINFO;
uint32_t Stack_IsEmpty(STACKINFO* stack);
int32_t Stack_Top(STACKINFO* stack);
uint32_t Stack_Push(STACKINFO* stack, int32_t value);
int32_t Stack_Pop(STACKINFO* stack);
void Camera_PushStates();
void Camera_PopStates();
struct ANIMSCRIPT_tag;
uint32_t AnimGetFirstTrans(ANIMSCRIPT_tag* s, _VECTOR* out);
obj_tag* AnimGetNDiscreteObj(obj_tag* obj, uint32_t index);
void Player_Disable(obj_tag* obj, int8_t freeze);
void Player_Enable(obj_tag* obj, MATRIX* m, uint32_t check);
void Player_Cam2Mode(uint16_t player, uint32_t mode);
// Player_Hurt: engine/explode.h.
void Drone_EnableAll(int8_t on, uint32_t script);
obj_tag* Drone_CoderCreate(_VECTOR* pos, _VECTOR* rot, uint32_t p0, uint32_t p1, uint32_t unused, uint32_t p9,
                           uint16_t state);
void Input_RumblePause();
void Input_ClearAllActions(int16_t player);
void PS2_ResetFrameCount();
extern uint32_t GameCount;     // quadros de VBlank já entregues à lógica (o laço principal os conta)
extern uint32_t PS2SkipCount;
// Collide_FilterBullets e Collide_GetDamageNObjects: engine/collide.h.

// Chamadas do script player ainda não portadas (stubs em modules_stubs.cpp, na seção de mesmo nome).
// Assinaturas dos nomes com mangling do ELF.
struct sprite;
struct SpriteInfo;
uint32_t Sound_IsLooping(uint32_t handle);                  // stub: 0
void Sound_SetVolume(uint32_t handle, float volume);
// Portadas (engine/sprite.h, engine/text.h):
sprite* Sprite_Create2(SpriteInfo* info);
sprite* Sprite_CreateLink(uint16_t list);
void Sprite_Delete(sprite* s);
uint32_t hashtable_set_sprite(sprite* s, uint32_t hash);  // 0: textura não achada
void Text_FlushAllSubtitles();
void SFXSuspendFileAccess();
void SFXUnSuspendFileAccess();
void psiInput_RumbleStop(uint16_t pad);
void psiFlushAllInput();
uint32_t Get_Music_Event(uint32_t event);                   // stub: 0
obj_tag* Ladder_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);  // player_traverse.cpp
// Break_DoBreak e Destroy_Create: engine/break.h.
// Pickup_Create: engine/pickup.h.

// Só no port: contadores para o nf_loader_check e as capturas.
struct ScriptStats {
    uint32_t loaded = 0;          // protótipos registrados (Script_Load com dados)
    uint32_t instances = 0;       // SCRIPTINFO criados
    uint32_t players = 0;         // SP_CreateScriptPlayer
    uint32_t switches = 0;        // Switch_Create
    uint32_t fuseboxes = 0;       // FuseBox_Create
    uint32_t entities = 0;        // Script_EntityStart que criaram um objeto
    uint32_t cameras = 0;         // Script_CameraStart que ligaram a câmera
    uint32_t loadables = 0;       // seções carregáveis tocadas (LoadableLoad por SP_LoadScript)
    uint32_t switch_loop_guard = 0;  // Switch_Create parou o laço do script no limite
};
extern ScriptStats g_script_stats;
// A câmera de script em uso (glb_viewer[4] ligada por Script_CameraStart), ou nulo.
struct viewer_tag;
viewer_tag* Script_ActiveCamera();
void script_debug_power_on();
