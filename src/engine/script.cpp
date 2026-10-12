// Port do módulo Script do ACTION.ELF (0x001d8088-0x001db9a8): scripts de cena das fases e das seções
// carregáveis (NIS). Ver script.h e docs/engine.md, "Script player".

#include "engine/script.h"

#include <algorithm>
#include <cstring>

#include "engine/ainetwork.h"
#include "engine/anim.h"
#include "engine/break.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/effect.h"
#include "engine/emitter.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/lights.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/spline.h"
#include "engine/sprite.h"
#include "engine/drone_stubs.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/trigger.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

sprite* Borders = nullptr;
sprite* Borders2 = nullptr;
// A tarja de cinema (0x002c9468): imagem preta 512 x 64 no alto da tela, escondida, câmera 4. Copiada do
// ACTION.ELF na partida (kTables em game/static_init.cpp).
SpriteInfo BorderInfo;
level_tag tptr;
ScriptStats g_script_stats;

namespace {

// Estáticas do original (os sufixos são os dos símbolos, ex.: Keys.235).
uint8_t Cmd_160 = 0;                 // comando em execução (Script_Run)
int32_t Keys_235[4];                 // quadros-chave da última interpolação (Script_GetInterp)
float Weights_236[4];
struct alignas(16) InterpResult {    // Res.237: posição, quatérnio, escala e alfa interpolados
    _VECTOR pos;
    quaternion_tag quat;
    float scale;
    float alpha;
    float pad[2];
};
InterpResult Res_237;
_VECTOR4 TmpV_141[2];                // as duas posições ponderadas (modo linear)
MATRIX TmpM_228;                     // matriz das entidades e das luzes (Script_Interp)
uint8_t* Clr1_238 = nullptr;         // cores dos dois quadros-chave de uma luz
uint8_t* Clr2_239 = nullptr;

// Handles de som e outros valores guardados em SSTREAM::obj.
uint32_t handle_of(const SSTREAM* st) { return nf::p32_u(st->obj); }
void set_handle(SSTREAM* st, uint32_t h) { st->obj = nf::p32_ptr<void>(h); }
obj_tag* obj_of(const SSTREAM* st) { return static_cast<obj_tag*>(st->obj); }
_VECTOR* row3(MATRIX* m) { return reinterpret_cast<_VECTOR*>(&m->m[3][0]); }

// __fixunssfsi (fptoui na libgcc do PS2): float para inteiro sem sinal, truncando.
uint32_t fptoui(float a) {
    if (a >= 2147483648.0f) return uint32_t(int32_t(a - 2147483648.0f)) + 0x80000000u;
    return uint32_t(int32_t(a));
}

// Busca binária dos quadros-chave (o mesmo laço em GetSplineWeights e no modo linear de
// Script_GetInterp): o índice do quadro igual a t ou, sem igual, o último abaixo dele.
int32_t find_key(const KEYED_POSROT* keys, int32_t count, float t) {
    int32_t lo = 0, hi = count;
    for (;;) {
        const int32_t mid = (lo + hi) >> 1;
        const float k = keys[mid].frame;
        if (t < k) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
            if (!(k < t)) return mid;
        }
        if (hi < lo) return hi;
    }
}

void kill_all_streams(SCRIPTINFO* si) {
    si->cur = 0;
    if (si->cur < si->stream_count) {
        for (;;) {
            Script_KillStream(si, &si->streams[si->cur]);
            ++si->cur;
            if (si->stream_count <= si->cur) break;
        }
    }
}

// Interpolação dos pais de um stream (Script_Interp, casos 1, 6 e 7): cada pai soma a sua posição à de
// "out", subindo pela cadeia; ao chegar no fim da cadeia, soma a do script. Devolve se a cadeia foi
// até o fim (o original não soma a do script quando para por índice fora do lugar).
void concat_parents(SCRIPTINFO* si, int32_t parent, MATRIX* out, bool spline_flag_means_4) {
    MATRIX local;
    Mat_IdentityT(&local);
    int32_t i = 0;
    for (;;) {
        if (!(parent < si->stream_count) || !(i < si->stream_count)) return;
        SSTREAM* p = &si->streams[parent];
        float scale_out = 0.0f;
        uint32_t colour_out = 0;
        const bool spline = (p->u22 & 2) != 0;
        const uint16_t mode = spline_flag_means_4 ? (spline ? 4 : 2) : (spline ? 4 : 2);
        Script_GetInterp(si, p, p->first_key, p->last_key, &local, &scale_out, &colour_out, mode, 0, si->scale);
        Concat(&local, out);
        ++i;
        parent = p->parent;
        if (parent == -1 || si->stream_count < i) break;
    }
    Concat(&si->base, out);
}

}  // namespace

// [decomp] 0x001d8088 GetSplineWeights
// Pesos de Catmull-Rom dos quatro quadros em volta do quadro inteiro de "time" (keys[0..3]); a fração
// vai na forma dos quatro pesos (o peso de keys[1] é 1 no começo do trecho). Fora dos quadros, só
// keys[1] (peso 1).
void GetSplineWeights(int32_t* keys, float* weights, float time, KEYED_POSROT* first, int32_t count) {
    const float t_int = float(int32_t(time));
    const int32_t idx = find_key(first, count, t_int + 0.001f);
    keys[1] = idx;
    if (idx < count) {
        if (-1 < idx) {
            const int32_t next = count < idx + 1 ? count : idx + 1;
            const int32_t prev = idx - 1 < 0 ? 0 : idx - 1;
            const int32_t after = count < idx + 2 ? count : idx + 2;
            keys[0] = prev;
            keys[3] = after;
            keys[2] = next;
            const float k = first[idx].frame;
            const float t = (t_int - k) / (first[next].frame - k);
            const float t2 = t * t;
            const float t3 = t * t2;
            weights[3] = (t3 - t2) * 0.5f;
            weights[2] = (t * 0.5f + (t2 + t2)) - t3 * 1.5f;
            weights[0] = t2 - (t + t3) * 0.5f;
            weights[1] = (1.0f - t2 * 2.5f) + t3 * 1.5f;
            return;
        }
        keys[1] = 0;
    } else {
        keys[1] = count;
    }
    weights[1] = 1.0f;
    keys[0] = keys[3] = keys[2] = keys[1];
    weights[0] = 0.0f;
    weights[3] = 0.0f;
    weights[2] = 0.0f;
}

// [decomp] 0x001d8248 Script_Load
// Com dados (na carga do pacote): registra o protótipo na tabela de hashcodes (ou -1 se a versão não é
// 0x1c) e devolve nulo. Sem dados: cria uma instância do protótipo já registrado no lugar pos/rot.
// Divergência: com mais de 0x3e streams o original chama DList_RemoveFromInUse2Free(&ScriptList, def),
// que mexeria numa lista onde o bloco não está; o port não chama (o disco tem no máximo 53).
SCRIPTINFO* Script_Load(uint32_t hash, _VECTOR* pos, _VECTOR* rot, uint8_t* data, void* owner,
                        void (*callback)(obj_tag*, uint16_t, void*), void* user) {
    if ((hash & 0xff000000u) != 0x6000000u) return nullptr;
    auto* def = static_cast<SCRIPTDEF_tag*>(hashtable_getitem(hash));
    if (def == nullptr) {
        if (data == nullptr) return nullptr;
        uint8_t* p = data;
        if (BIN_GetWord(&p) != 0x1c) {
            hashtable_additem(hash, nf::p32_ptr<void>(0xffffffffu));
            return nullptr;
        }
        def = static_cast<SCRIPTDEF_tag*>(Mem_Malloc(0x214, 0x1404, 0));
        hashtable_additem(hash, def);
        def->camera = BIN_GetWord(&p);
        def->stream_count = BIN_GetWord(&p);
        def->loop_start = BIN_GetWord(&p);
        def->end = BIN_GetWord(&p);
        def->hash = hash;
        if (0x3e < def->stream_count) return nullptr;
        for (uint32_t i = 0; i < def->stream_count; ++i) {
            def->streams[i].first_key = BIN_GetWord(&p);
            def->streams[i].last_key = BIN_GetWord(&p);
            const uint32_t size = BIN_GetDWord(&p);
            def->streams[i].commands = p;
            p += size - 4;
        }
        def->key_count = uint16_t(BIN_GetDWord(&p));
        const uint32_t pad = BIN_GetDWord(&p);
        p += pad;
        def->keys = reinterpret_cast<KEYED_POSROT*>(p);
        ++g_script_stats.loaded;
    } else if (def == nf::p32_ptr<SCRIPTDEF_tag>(0xffffffffu)) {
        return nullptr;
    }
    if (data != nullptr) return nullptr;
    auto* si = static_cast<SCRIPTINFO*>(Mem_Malloc(0xac0, 0x1404, 0));
    if (si == nullptr) return nullptr;
    si->play = 0;
    si->hash = def->hash;
    si->stream_count = def->stream_count;
    si->key_count = def->key_count;
    si->end = def->end;
    si->owner = owner;
    si->keys = def->keys;
    si->wait = 0;
    si->fade = 0.0f;
    si->fade_ticks = 0;
    si->state = 0;
    si->time = 0.0f;
    si->loop_start = def->loop_start;
    si->callback = callback;
    si->scale = 1.0f;
    si->user = user;
    si->flags = uint16_t((def->camera != 0 ? 1 : 0) << 6);
    View_RotTransMatrix(rot, pos, &si->base);
    for (uint32_t i = 0; i < def->stream_count; ++i) {
        SSTREAM* st = &si->streams[i];
        std::memset(st, 0, sizeof(SSTREAM));
        st->time = 0.0f;
        st->first_key = def->streams[i].first_key;
        st->last_key = def->streams[i].last_key;
        st->commands = def->streams[i].commands;
        st->parent = -1;
        st->cursor = def->streams[i].commands;
        st->obj = nullptr;
    }
    ++g_script_stats.instances;
    return si;
}

// [decomp] 0x001d8548 Script_Run
// Executa os comandos do stream atual (SCRIPTINFO::cur) até um que espere um quadro futuro. Cada
// comando com tratador termina num byte de fecho (o próprio código; 5 e 6 trocados) que é pulado.
void Script_Run(SCRIPTINFO* si) {
    SSTREAM* st = &si->streams[si->cur];
    if (st->cursor == nullptr) return;
    uint16_t end = si->end;
    for (;;) {
        if (st->time <= float(end)) Script_Interp(si, st);
        if (st->cursor == nullptr) return;
        bool restart = false;
        while (!restart) {
            if (st->time < st->wait) return;
            Cmd_160 = *st->cursor;
            ++st->cursor;
            bool closing = true;
            switch (Cmd_160) {
                case 4:
                    Script_StreamEnd(si, st);
                    if ((si->play & 8) == 0) return;
                    end = si->end;
                    restart = true;
                    closing = false;
                    break;
                case 5:
                    st->wait = float(BIN_GetWord(&st->cursor));
                    break;
                case 6:
                    BIN_GetWord(&st->cursor);
                    ++st->cursor;
                    if (float(si->end) <= st->time) si->play |= 2;
                    closing = false;
                    break;
                case 7: Script_EntityStart(si, st); break;
                case 9: Script_EntityEnd(si, st); break;
                case 10: Script_AnimStart(si, st); break;
                case 0xc: Script_AnimEnd(si, st); break;
                case 0xd: Script_CameraStart(si, st); break;
                case 0xf: Script_CameraEnd(si, st); break;
                case 0x12: Script_EventHandler(si, st); break;
                case 0x13: Script_FadeStart(si, st); break;
                case 0x14: Script_SpriteStart(si, st); break;
                case 0x15: Script_SpriteEnd(si, st); break;
                case 0x16: Script_SoundStart(si, st); break;
                case 0x17: Script_SoundEnd(si, st); break;
                case 0x18: Script_LightStart(si, st); break;
                case 0x1a: Script_LightEnd(si, st); break;
                case 0x1b: Script_SubScriptStart(si, st); break;
                case 0x1d: Script_SubScriptEnd(si, st); break;
                case 0x1e: Script_TextStart(si, st); break;
                default: closing = false; break;
            }
            if (restart) break;
            if (closing) ++st->cursor;
            if (st->cursor == nullptr) return;
        }
    }
}

// [decomp] 0x001d8820 Script_SoundStart
// Som 2D (ou sem objeto) ou preso ao objeto de outro stream (o anterior, se não indicado).
void Script_SoundStart(SCRIPTINFO* si, SSTREAM* st) {
    const uint32_t sound = BIN_GetDWord(&st->cursor);
    uint8_t two_d = BIN_GetByte(&st->cursor);
    const uint8_t own = BIN_GetByte(&st->cursor);
    uint16_t index = BIN_GetByte(&st->cursor);
    st->obj = nullptr;
    if (own == 0) index = si->cur == 0 ? 0 : uint16_t(si->cur - 1);
    st->u20 = index;
    st->u22 = uint16_t(int32_t(st->time) + 1);
    obj_tag* o = Script_GetObj(si, st->u20);
    if (st->obj != nullptr || (si->state & 0x20) != 0) return;
    if (o == nullptr) two_d = 1;
    if (two_d == 1) set_handle(st, Sound_Play(sound, 100.0f, 0, 0));
    if (two_d == 0 && o != nullptr) {
        set_handle(st, Sound_Play3D(sound, row3(&o->matrix), 100.0f, -1.0f, -1.0f, 0, 0, 0));
    }
    st->type = 5;
}

// [decomp] 0x001d8968 Script_CreateEntity
// Cria (ou reaproveita, se o stream já tem um objeto do mesmo tipo) o objeto de uma entidade de script:
// o tipo do comando escolhe o criador, com o posicionamento falso tptr; os outros tipos viram um objeto
// simples (tipo 0) com o modelo do hash. O objeto sai de todas as salas.
// Divergência: Trigger_TouchOnce, Trigger_Touch e Emitter_CreatePlist são void no ELF e o original lê
// o objeto que ficou em v0 (o de Trigger_Create ou Emitter_Create); no port elas o devolvem. Criadores
// ainda em stub devolvem nulo e o port não chama control_unlink_object(nulo).
obj_tag* Script_CreateEntity(SSTREAM* st, uint32_t hash, uint8_t type) {
    MATRIX identity;
    Mat_IdentityT(&identity);
    uint8_t obj_type = 0;
    switch (type) {
        case 0x20: obj_type = 0x20; break;
        case 0x22: obj_type = 0x2d; break;
        case 0x29: obj_type = 0x29; break;
        case 0xe1: obj_type = 0xe1; break;
        case 0xea: obj_type = 0xea; break;
        case 0xeb: obj_type = 0xeb; break;
        case 0xf0: obj_type = 0xf0; break;
        case 0xf2: obj_type = 0xf2; break;
        case 0xf5: obj_type = 0x3e; break;
        default: break;
    }
    obj_tag* obj = obj_of(st);
    if (obj != nullptr) {
        if (obj->type != obj_type) {
            obj->life |= 1;
            st->obj = nullptr;
            obj = nullptr;
        }
        if (obj != nullptr) {
            hashtable_set_object_to_entity_gfx(obj, hash);
            control_unlink_object(obj);
            return obj;
        }
    }
    _VECTOR scale{1.0f, 1.0f, 1.0f, 0.0f};
    _VECTOR* zero = const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR);
    switch (obj_type) {
        case 0xe1:
            obj = Destroy_Create(zero, zero, &tptr, hashtable_hashcode_to_celglist(hash), nullptr);
            break;
        case 0x29: obj = Switch_Create(zero, zero, &tptr, hashtable_hashcode_to_celglist(hash)); break;
        case 0x20: obj = Break_Create(nullptr, nullptr, hashtable_hashcode_to_celglist(hash), &tptr); break;
        case 0x2d: obj = Ladder_Create(nullptr, nullptr, &tptr, hashtable_hashcode_to_celglist(hash)); break;
        case 0x3e:
            obj = Drone_AIVolume_Create(zero, zero, &scale, &tptr, hashtable_hashcode_to_celglist(hash));
            break;
        case 0xeb: obj = Trigger_Touch(zero, zero, &tptr, hashtable_hashcode_to_celglist(hash)); break;
        case 0xea: obj = Trigger_TouchOnce(zero, zero, &tptr, hashtable_hashcode_to_celglist(hash)); break;
        case 0xf0: {
            celglist_tag* glist = hashtable_hashcode_to_celglist(hash);
            obj = Pickup_Create(zero, zero, nullptr, glist, uint16_t(tptr.params[0]), uint16_t(tptr.params[1]),
                                tptr.params[2], tptr.params[3], tptr.params[4], 1, uint16_t(tptr.params[5]), 0, 0);
            break;
        }
        case 0xf2:
            obj = Emitter_CreatePlist(&identity, &tptr);
            if (obj != nullptr) control_unlink_object(obj);
            break;
        default:
            obj = control_create_object(0, nullptr, nullptr, nullptr);
            if (hash != 0 && obj != nullptr) hashtable_set_object_to_entity_gfx(obj, hash);
            break;
    }
    if (obj != nullptr) control_unlink_object(obj);
    return obj;
}

// [decomp] 0x001d8d78 Script_EntityStart
// Comando 7: hash, tem pai, pai, spline, tipo, n e n parâmetros (para tptr). O objeto segue os quadros
// do stream (Script_Interp) e passa a apontar para o dono do script (obj_tag::script).
void Script_EntityStart(SCRIPTINFO* si, SSTREAM* st) {
    const uint32_t hash = BIN_GetDWord(&st->cursor);
    const uint8_t has_parent = BIN_GetByte(&st->cursor);
    const uint8_t parent = BIN_GetByte(&st->cursor);
    const uint8_t spline = BIN_GetByte(&st->cursor);
    const uint8_t type = BIN_GetByte(&st->cursor);
    const uint8_t count = BIN_GetByte(&st->cursor);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t v = BIN_GetDWord(&st->cursor);
        if (i < 33) tptr.params[i] = v;  // o original não limita (o disco tem no máximo 6)
    }
    obj_tag* obj = Script_CreateEntity(st, hash, type);
    if (obj == nullptr) return;
    if (st->obj == nullptr) st->u20 = uint16_t(int32_t(st->time) + 1);
    st->parent = has_parent == 0 ? int8_t(-1) : int8_t(parent);
    st->obj = obj;
    st->type = 1;
    st->u22 = spline == 0 ? 0 : 2;
    obj->mode |= 0xe4;
    obj->script = si->owner;
    Script_Interp(si, st);
    Control_BuildWorldSph(obj);
    if (obj->cel == nullptr) build_LinkToRoom(obj, 1, glb_world);
    _VECTOR n;
    Mat_GetNorm(&n, &obj->matrix);
    Vec_NormToRot(&n, &obj->rot);
    Vec_Copy2(row3(&obj->matrix), &obj->old_pos, &obj->pos);
    ++g_script_stats.entities;
}

// [decomp] 0x001d8f40 Script_AnimStart
// Comando 10: script de animação, pele e quadro inicial. Cria um objeto animado (tipo 0) que toca o
// script parado no quadro do stream (Script_Interp acerta o quadro a cada tick).
void Script_AnimStart(SCRIPTINFO* si, SSTREAM* st) {
    const uint32_t script = BIN_GetDWord(&st->cursor);
    const uint32_t skin = BIN_GetDWord(&st->cursor);
    const uint32_t frame = BIN_GetDWord(&st->cursor);
    if (st->obj != nullptr) Script_KillStream(si, st);
    _VECTOR* zero = const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR);
    obj_tag* obj = control_create_object(0, zero, zero, nullptr);
    if (obj == nullptr) return;
    AnimObjectNew(obj, skin);
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim != nullptr) anim->anim_flags |= 0x8000;
    ANIMSCRIPT_tag* s = AnimScriptAdd(obj, script);
    if (s == nullptr) {
        // Só no port: o original seguiria com o script nulo. O objeto fica no stream sem animação.
        st->obj = obj;
        st->type = 2;
        return;
    }
    s->flags |= 0x10000;
    const float f = float(frame);  // conversão sem sinal, como o original
    s->frame = f;
    if (f < 1.0f) {
        s->frame = 1.0f;
    } else if (float(s->frame_count) < f) {
        s->frame = float(s->frame_count);
    }
    st->obj = obj;
    st->type = 2;
    st->start = st->time;
    st->anim_frame = s->frame;
    AnimObjectUpdate(obj);
    AnimGetFirstTrans(s, &obj->pos);
    obj->type = 0;
    obj->data = si->owner;
    obj->mode |= 0x62;
    MATRIX m;
    float scale_out;
    uint32_t colour_out;
    Script_GetInterp(si, st, st->first_key, st->last_key, &m, &scale_out, &colour_out, 2, 1, si->scale);
    if (obj->cel == nullptr) build_LinkToRoom(obj, 0, glb_world);
}

// [decomp] 0x001d9140 Script_CameraStart
// Comando 13: spline, primeiro e último quadro-chave (+1). Liga a câmera de script (a 4, com as outras
// desligadas e o estado delas guardado na pilha de cada câmera), as tarjas de cinema e a música.
void Script_CameraStart(SCRIPTINFO* si, SSTREAM* st) {
    ScriptCam = si->hash;
    Mat_IdentityT(&si->base);
    const uint8_t spline = BIN_GetByte(&st->cursor);
    const uint16_t first = BIN_GetWord(&st->cursor);
    const uint16_t last = BIN_GetWord(&st->cursor);
    if ((si->flags & 0x20) != 0) return;
    Input_ClearAllActions(-1);
    auto* cam = static_cast<CAM_INFO*>(st->obj);
    if (cam == nullptr) cam = static_cast<CAM_INFO*>(Mem_Malloc(0x50, 0x1404, 0));
    if (cam == nullptr) return;
    cam->interp = spline != 0 ? 4 : 2;
    cam->last_key = last;
    cam->first_key = first;
    st->type = 4;
    st->obj = cam;
    Script_Interp(si, st);
    Camera_PushStates();
    for (uint32_t i = 0; i < 7; ++i) Camera_Enable(i, 0, 0, nullptr);
    Camera_Enable(4, 1, 1, nullptr);
    si->state |= 0x10;
    Music_Event(0xb, si->hash);
    if (Borders == nullptr) Borders = Sprite_Create2(&BorderInfo);
    if (Borders2 == nullptr) {
        Borders2 = Sprite_Create2(&BorderInfo);
        if (Borders2 != nullptr) Borders2->y = 0x180;
    }
    if (Borders != nullptr) Borders->priority = 1;
    if (Borders2 != nullptr) Borders2->priority = 1;
    Light_Update();
    ++g_script_stats.cameras;
}

// [decomp] 0x001d9300 Script_SpriteStart
// Comando 20: retângulo (x0, y0, x1, y1 na tela de 640 x 480, convertidos para 512 x 448), flags e o
// hash da imagem.
void Script_SpriteStart(SCRIPTINFO*, SSTREAM* st) {
    const int32_t x0 = BIN_GetWord(&st->cursor);
    const int32_t y0 = BIN_GetWord(&st->cursor);
    const int32_t x1 = BIN_GetWord(&st->cursor);
    const int32_t y1 = BIN_GetWord(&st->cursor);
    const uint16_t flags = BIN_GetWord(&st->cursor);
    const uint32_t hash = BIN_GetDWord(&st->cursor);
    auto* s = static_cast<sprite*>(st->obj);
    if (s == nullptr) s = Sprite_CreateLink(4);
    if (s == nullptr) return;
    hashtable_set_sprite(s, hash);
    s->uw = int16_t(s->uw - 1);
    s->vh = int16_t(s->vh - 1);
    s->x = int16_t(int32_t(float(x0) * 0.8f));
    s->w = int16_t(int32_t(float(x1) * 0.8f));
    s->y = int16_t(int32_t(float(y0) * 0.9333334f));
    s->h = int16_t(int32_t(float(y1) * 0.9333334f));
    s->priority = 2;
    s->colour = 0xffffffffu;
    uint16_t& sflags = s->flags;
    if ((flags & 2) != 0) sflags |= 0x40;
    if ((flags & 4) != 0) sflags |= 0x80;
    sflags |= 0x100;
    st->obj = s;
    st->type = 3;
}

// [decomp] 0x001d94c0 Script_EventHandler
// Comando 18: n argumentos e o evento. "Objeto anterior" é o da entidade ou animação do stream
// anterior a este. Divergência: o original lê os argumentos para 8 palavras na pilha sem limite; o
// port ignora os que passariam disso (o disco usa no máximo 4).
void Script_EventHandler(SCRIPTINFO* si, SSTREAM* st) {
    SSTREAM* prev = si->cur == 0 ? nullptr : &si->streams[si->cur - 1];
    const uint8_t count = BIN_GetByte(&st->cursor);
    const uint8_t event = BIN_GetByte(&st->cursor);
    uint32_t a[8] = {};
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t v = BIN_GetDWord(&st->cursor);
        if (i < 8) a[i] = v;
    }
    obj_tag* prev_obj = nullptr;
    if (prev != nullptr && uint8_t(prev->type - 1) < 2) prev_obj = obj_of(prev);
    switch (event) {
        case 3:
            // Pausa: espera a[0] - 1 ticks (0: até SP_UnPause), a não ser no avanço rápido; os objetos
            // ficam onde estão (sem rastro de movimento).
            if ((si->state & 1) == 0) si->wait = int32_t(a[0] - 1);
            for (uint16_t i = 0; i < si->stream_count; ++i) {
                obj_tag* o = Script_GetObj(si, i);
                if (o != nullptr) Vec_Copy2(row3(&o->matrix), &o->pos, &o->old_pos);
            }
            break;
        case 4:
            Player_Disable(glb_players[0], 0);
            PlarStat_LogTimerPause(0);
            break;
        case 5:
            Drone_EnableAll(0, si->hash);
            break;
        case 6:
            si->flags |= 0x100;
            if (prev_obj != nullptr) {
                Mat_Copy(&prev_obj->matrix, &si->player);
            } else if (glb_players[0] != nullptr) {  // só no port: sem jogador o original leria nulo
                Mat_Copy(&glb_players[0]->matrix, &si->player);
            }
            si->player_flag = uint8_t(a[0]);
            break;
        case 7:
            si->flags |= 0x80;
            break;
        case 8:
            if (prev_obj != nullptr) Drone_CoderCreate(row3(&prev_obj->matrix), &prev_obj->rot, a[0], a[1], a[2], a[3], 4);
            break;
        case 9:
            Player_Cam2Mode(0, a[0]);
            break;
        case 10:
            if (prev_obj != nullptr) Break_DoBreak(row3(&prev_obj->matrix));
            break;
        case 11:
            if (prev_obj != nullptr && prev_obj->sphere_src != nullptr) anim_of(prev_obj)->weapon = int8_t(a[0]);
            break;
        case 12:
            si->flags |= 0x10;
            break;
        case 13:
            if (a[0] == 0) return;
            switch_channels[a[0] & 0xff] = uint8_t(a[1]);
            if (a[1] != 0) switch_channels_time[a[0] & 0xff] = GameState.frame;
            break;
        case 14:
            st->u22 |= 1;
            break;
        case 15:
            if (si->callback != nullptr) si->callback(prev_obj, uint16_t(a[0]), si->user);
            break;
        case 16:
            si->flags |= 0x20;
            break;
        case 17: {
            const uint32_t ch = switch_channels[a[0] & 0xff] != 0 ? a[1] : a[2];
            if (ch == 0) return;
            switch_channels[ch & 0xff] = uint8_t(a[3]);
            if (a[3] != 0) switch_channels_time[ch & 0xff] = GameState.frame;
            break;
        }
        case 18:
            if (glb_blokes[0] != nullptr) Player_RamSave(glb_blokes[0]);
            ResetMap_LevelToLoad(a[0], 1, 0);
            break;
        case 19:
            ScriptCam = si->hash;
            break;
        default:
            break;
    }
}

// [decomp] 0x001d9860 Script_KillStream
// Desfaz o que o stream criou: entidades e animações somem (ou ficam, com a flag 0x10 do script),
// sprite, câmera (volta o estado das câmeras), som, luz e sub-script.
void Script_KillStream(SCRIPTINFO* si, SSTREAM* st) {
    switch (st->type) {
        case 1: {
            obj_tag* o = obj_of(st);
            if (o == nullptr) {
                st->type = 0;
                st->obj = nullptr;
                return;
            }
            o->script = nullptr;
            if ((si->flags & 0x10) == 0) {
                o->life |= 1;
            } else {
                Vec_Copy2(row3(&o->matrix), &o->pos, &o->old_pos);
                o->mode |= 0x20;
            }
            break;
        }
        case 2: {
            obj_tag* o = obj_of(st);
            if (o == nullptr) return;
            if ((si->flags & 0x10) == 0) {
                o->life |= 1;
            } else {
                o->data = nullptr;
            }
            break;
        }
        case 3:
            if (st->obj == nullptr) return;
            Sprite_Delete(static_cast<sprite*>(st->obj));
            st->type = 0;
            st->obj = nullptr;
            return;
        case 4: {
            void* cam = st->obj;
            if (cam == nullptr) return;
            Camera_PopStates();
            Camera_Enable(4, 0, 0, nullptr);
            Mem_Free(&cam);
            Music_Event(0xc, si->hash);
            if (Borders != nullptr) Borders->priority = -1;
            if (Borders2 != nullptr) Borders2->priority = -1;
            break;
        }
        case 5:
            Sound_Stop(handle_of(st));
            break;
        case 6:
            if (st->obj == nullptr) return;
            if ((si->flags & 0x10) == 0) Light_Delete(static_cast<light_tag*>(st->obj));
            st->type = 0;
            st->obj = nullptr;
            return;
        case 7:
            if (st->obj != nullptr) Script_Free(static_cast<SCRIPTINFO*>(st->obj));
            st->type = 0;
            st->obj = nullptr;
            return;
        default:
            break;
    }
    st->type = 0;
    st->obj = nullptr;
}

// [decomp] 0x001d9a08 Script_Interp
// Põe o que o stream controla no lugar do quadro atual: entidade (matriz, escala e alfa, somando os
// pais), animação (quadro do script de animação e passos), câmera, som (volume do fade e posição), luz
// (posição, raio e cor) e sub-script (lugar e um tick).
void Script_Interp(SCRIPTINFO* si, SSTREAM* st) {
    if (st->obj == nullptr) return;
    switch (st->type) {
        case 1: {
            obj_tag* o = obj_of(st);
            o->mode |= 0x20;
            const uint16_t mode = (st->u22 & 2) == 0 ? 2 : 4;
            uint32_t colour = 0;
            Script_GetInterp(si, st, st->first_key, st->last_key, &TmpM_228, &o->scale, &colour, mode,
                             st->parent == -1 ? 1 : 0, si->scale);
            const float s = o->scale;
            o->unknown_103[3] = uint8_t(colour);
            if (s == 1.0f) {
                Mat_Copy(&TmpM_228, &o->matrix);
            } else {
                Mat_Scale3f(&o->matrix, &TmpM_228, s, s, s);
            }
            if (st->parent == -1) return;
            concat_parents(si, st->parent, &o->matrix, true);
            return;
        }
        case 2: {
            obj_tag* o = obj_of(st);
            ANIMOBJ_tag* anim = anim_of(o);
            if ((si->state & 0x20) != 0) return;
            AnimListBuild(o, 1, 0, nullptr, 1);
            for (int32_t i = 0; i < GlbAnimList; ++i) {
                ANIMSCRIPT_tag* s = GlbAnimListItems[i];
                if ((s->flags & 0x10000) == 0) continue;
                const float f = (st->time - st->start) + st->anim_frame;
                s->frame = f;
                if (f < 1.0f) {
                    s->frame = 1.0f;
                } else if (float(s->frame_count) < f) {
                    s->frame = float(s->frame_count);
                }
                s->speed = 0.0f;
            }
            AnimObjectUpdate(o);
            o->mode |= 0x20;
            if (anim != nullptr && anim->skin != nullptr && anim->skin->hash == 0x500001e) NDrone2_CreateNinjaEyes(o);
            anim = anim_of(o);
            if (anim == nullptr || (anim->flags & 4) == 0) return;
            // Passo neste tick: um raio de 4 m para baixo a partir do quadril acha o chão.
            _VECTOR from, to;
            Vec_Copy2(row3(&o->matrix), &from, &to);
            to.y -= 4.0f;
            cel_tag* cel = build_FindCel(&from, glb_world);
            P32<HITDATA_tag> hit = nullptr;
            if (!Collide_RayIntersect(&from, &to, cel, o, nullptr, &hit, 0, 0xf0d, 0)) return;
            if (hit->dist - anim->root_height <= 0.2f) {
                const uint16_t foot = ScriptFootstepFoot != 0 ? 8 : 4;
                Effect_Create(hit, o, uint16_t(foot | 0x42), 1.0f);
            }
            Collide_FreeHitList(&hit);
            return;
        }
        case 4: {
            auto* cam = static_cast<CAM_INFO*>(st->obj);
            if ((si->state & 0x20) != 0) return;
            uint32_t colour = 0;
            Script_GetInterp(si, st, cam->first_key, uint16_t(cam->last_key - 1), &cam->m, &cam->fov, &colour,
                             cam->interp, 1, si->scale);
            Script_UpdateCamera(cam);
            return;
        }
        case 5: {
            const uint32_t h = handle_of(st);
            if (Sound_IsFinnished(h) != 0) {
                Script_KillStream(si, st);
                return;
            }
            if (si->fade != 0.0f) {
                const float n = float(si->fade_ticks);
                Sound_SetVolume(h, ((n - si->fade) / n) * 100.0f);
            }
            obj_tag* o = Script_GetObj(si, st->u20);
            if (o == nullptr) return;
            Sound_SetPosition(h, row3(&o->matrix));
            return;
        }
        case 6: {
            auto* light = static_cast<light_tag*>(st->obj);
            uint32_t colour = 0;
            Script_GetInterp(si, st, st->first_key, st->last_key, &TmpM_228, &light->radius, &colour, 2,
                             st->parent == -1 ? 1 : 0, si->scale);
            light->pos = *row3(&TmpM_228);
            light->r = uint8_t(colour >> 24);
            light->g = uint8_t(colour >> 16);
            light->b = uint8_t(colour >> 8);
            if (st->parent == -1) return;
            concat_parents(si, st->parent, &TmpM_228, true);
            light->pos = *row3(&TmpM_228);
            return;
        }
        case 7: {
            auto* sub = static_cast<SCRIPTINFO*>(st->obj);
            uint32_t colour = 0;
            Script_GetInterp(si, st, st->first_key, st->last_key, &sub->base, &sub->scale, &colour, 2,
                             st->parent == -1 ? 1 : 0, sub->scale);
            if (st->parent != -1) concat_parents(si, st->parent, &sub->base, true);
            Script_Update(sub);
            return;
        }
        default:
            return;
    }
}

// [decomp] 0x001da158 Script_GetInterp
// Interpola os quadros-chave first..last do stream no quadro inteiro atual: modo 2 linear (slerp na
// rotação), modo 4 Catmull-Rom. Peculiaridades mantidas: no modo 4 a fração passada a Spline_Eval3D e
// ao slerp é o peso de keys[1] (1 no começo do trecho, 0 no fim), e a cor das luzes interpola pelo peso
// do primeiro quadro (Weights[0]). A matriz sai em "out" (com a posição vezes pos_scale), a escala do
// quadro vezes SCRIPTINFO::scale em *scale e o alfa (0..255) ou a cor em *colour; com concat, soma o
// lugar do script.
void Script_GetInterp(SCRIPTINFO* si, SSTREAM* st, uint16_t first, uint16_t last, MATRIX* out, float* scale,
                      uint32_t* colour, uint16_t mode, int8_t concat, float pos_scale) {
    KEYED_POSROT* base = si->keys + first;
    if (mode == 2) {
        const int32_t n = int32_t(last) - int32_t(first);
        Keys_235[1] = n;
        const float t_int = float(int32_t(st->time));
        const int32_t idx = find_key(base, n, t_int + 0.001f);
        Keys_235[0] = idx;
        if (idx < n) {
            if (idx < 0) {
                Keys_235[1] = 0;
                Weights_236[0] = 1.0f;
                Weights_236[1] = 0.0f;
                Keys_235[0] = 0;
            } else {
                Keys_235[1] = idx + 1;
                const float k = base[idx].frame;
                Weights_236[1] = (t_int - k) / (base[idx + 1].frame - k);
                Weights_236[0] = 1.0f - Weights_236[1];
            }
        } else {
            Keys_235[1] = n;
            Weights_236[0] = 1.0f;
            Weights_236[1] = 0.0f;
            Keys_235[0] = n;
        }
        KEYED_POSROT* k0 = base + Keys_235[0];
        KEYED_POSROT* k1 = base + Keys_235[1];
        Vec_MulR32(&TmpV_141[0], reinterpret_cast<_VECTOR4*>(&k0->pos), Weights_236[0]);
        Vec_MulR32(&TmpV_141[1], reinterpret_cast<_VECTOR4*>(&k1->pos), Weights_236[1]);
        Res_237.pos = {TmpV_141[0].x + TmpV_141[1].x, TmpV_141[0].y + TmpV_141[1].y, TmpV_141[0].z + TmpV_141[1].z,
                       TmpV_141[0].w + TmpV_141[1].w};
        Quat_Slerp_Acc(Weights_236[1], &k0->quat, &k1->quat, &Res_237.quat);
        Res_237.scale = k0->scale * Weights_236[0] + k1->scale * Weights_236[1];
        Res_237.alpha = k0->alpha * Weights_236[0] + k1->alpha * Weights_236[1];
    } else {
        GetSplineWeights(Keys_235, Weights_236, st->time, base, int32_t(last) - int32_t(first));
        KEYED_POSROT* k0 = base + Keys_235[0];
        KEYED_POSROT* k1 = base + Keys_235[1];
        KEYED_POSROT* k2 = base + Keys_235[2];
        KEYED_POSROT* k3 = base + Keys_235[3];
        Spline_Eval3D(&k0->pos, &k1->pos, &k2->pos, &k3->pos, Weights_236[1], &Res_237.pos);
        Quat_Slerp_Acc(Weights_236[1], &k1->quat, &k2->quat, &Res_237.quat);
        Res_237.scale = k0->scale * Weights_236[0] + k1->scale * Weights_236[1] + k2->scale * Weights_236[2] +
                        k3->scale * Weights_236[3];
        Res_237.alpha = k0->alpha * Weights_236[0] + k1->alpha * Weights_236[1] + k2->alpha * Weights_236[2] +
                        k3->alpha * Weights_236[3];
    }
    bool write_alpha = true;
    switch (st->type) {
        case 0:
        case 1:
        case 4:
        case 7:
            Quat_QuatToMat(&Res_237.quat, out);
            *row3(out) = Res_237.pos;
            if (scale != nullptr) *scale = Res_237.scale * si->scale;
            break;
        case 2:
            if (concat == 0) {
                obj_tag* o = AnimGetNDiscreteObj(obj_of(st), 0);
                if (o != nullptr) Mat_Copy(&o->matrix, out);
            }
            if (scale != nullptr) *scale = Res_237.scale * si->scale;
            break;
        case 6: {
            Clr2_239 = reinterpret_cast<uint8_t*>(&si->keys[first + Keys_235[1]].alpha);
            Clr1_238 = reinterpret_cast<uint8_t*>(&si->keys[first + Keys_235[0]].alpha);
            const auto channel = [](int b) {
                const float c1 = float(Clr1_238[b]);
                const float c2 = float(Clr2_239[b]);
                const float d = c2 - c1;
                float v = c1 + d * Weights_236[0];
                if (0.0f <= d ? c2 < v : v < c2) v = c2;
                return int32_t(v);
            };
            const int32_t c2 = channel(2);
            const int32_t c1 = channel(1);
            const int32_t c0 = channel(0);
            *colour = uint32_t(c2) << 24 | uint32_t(c1) << 16 | uint32_t(c0) << 8 | 0xff;
            *scale = Res_237.scale * si->scale;
            *row3(out) = Res_237.pos;
            write_alpha = false;
            break;
        }
        default:
            write_alpha = false;
            break;
    }
    if (write_alpha && colour != nullptr) *colour = fptoui(Res_237.alpha * 255.0f + 0.5f);
    if (pos_scale != 1.0f) Vec_MulR32(row3(out), row3(out), pos_scale, pos_scale, pos_scale);
    if (concat != 0) Concat(&si->base, out);
}

// [decomp] 0x001da880 Script_Free
// Desfaz todos os streams, devolve as animações da fase (seção carregável), religa a IA e o jogador
// se o script pediu e solta a instância.
void Script_Free(SCRIPTINFO* si) {
    if (si == nullptr) return;
    kill_all_streams(si);
    const uint32_t hash = si->hash;
    if (isLoadable(hash) != 0) {
        SFXSuspendFileAccess();
        LoadableReload(hash);
        SFXUnSuspendFileAccess();
    }
    if ((si->flags & 0x80) != 0) Drone_EnableAll(1, si->hash);
    if ((si->flags & 0x100) != 0) {
        Player_Enable(glb_players[0], &si->player, si->player_flag);
        PlarStat_LogTimerUnpause(0);
    }
    if (ScriptCam == hash) ScriptCam = 0;
    void* p = si;
    Mem_Free(&p);
}

// [decomp] 0x001da998 Script_Update
// Um tick: roda todos os streams e anda o tempo. Perto do fim de uma cena com câmera, escurece a tela.
// Durante o fade do avanço rápido (Script_FFwd) o script roda vários ticks por chamada até acabar.
void Script_Update(SCRIPTINFO* si) {
    if (si == nullptr || (si->play & 1) == 0) return;
    if (si->wait != 0) {
        if (0 < si->wait) --si->wait;
        return;
    }
    bool finish = false;
    for (;;) {
        if (Script_IsPlaying(si) != 0) {
            si->cur = 0;
            if (si->stream_count != 0) {
                do {
                    Script_Run(si);
                    ++si->cur;
                } while (si->cur < si->stream_count);
            }
            si->time += FRAME_RATE_MUL;
            si->cur = 0;
            if (si->stream_count != 0) {
                do {
                    si->streams[si->cur].time += FRAME_RATE_MUL;
                    ++si->cur;
                } while (si->cur < si->stream_count);
            }
            viewer_tag* v4 = glb_viewer[4];
            if (ScriptCam != 0 && (si->flags & 0x40) != 0 && float(int32_t(si->end) - 0x14) <= si->time &&
                v4 != nullptr && v4->fade_state != 1) {
                if (Script_IsDeathNIS(si->hash) != 0) {
                    finish = true;
                    break;
                }
                const uint32_t h = si->hash;
                if (h != 0x60004fa && h != 0x6000080 && h != 0x60008c8 && h != 0x6000994) {
                    Camera_SetFade(4, 40.0f, 0xff, 0);
                }
            }
        }
        if (si->fade != 0.0f) {
            if (si->fade <= float(si->fade_ticks)) {
                si->fade += FRAME_RATE_MUL;
            } else {
                si->state |= 0x20;
            }
        }
        if (!(float(si->fade_ticks) < si->fade)) return;
        if (Script_IsPlaying(si) == 0) {
            finish = true;
            break;
        }
    }
    if (finish && float(si->fade_ticks) < si->fade && Script_IsPlaying(si) == 0) {
        Camera_SetFade(0, -22.0f, 0xff, 1);
        si->fade = 0.0f;
        si->play &= 0xef;
        si->state &= 0xfe;
    }
}

// [decomp] 0x001dac78 Script_FFwd
// Pular a cena (com a câmera de script ligada): sem câmera própria (flag 0x20), só desliga a câmera;
// senão começa um fade de até 45 ticks durante o qual Script_Update corre o script até o fim.
void Script_FFwd(SCRIPTINFO* si, int8_t on) {
    if (si == nullptr) return;
    if (on == 0 || (si->state & 0x10) == 0 || (si->state & 1) != 0) {
        si->play &= 0xef;
        return;
    }
    if (ScriptCam != 0) {
        Input_ClearAllActions(-1);
        Text_FlushAllSubtitles();
    }
    if ((si->flags & 0x20) != 0) {
        for (uint16_t i = 0; i < si->stream_count; ++i) {
            if (si->streams[i].type == 4) {
                Script_KillStream(si, &si->streams[i]);
                return;
            }
        }
        return;
    }
    const float left = std::min(float(si->end) - si->time, 45.0f);
    si->fade_ticks = uint8_t(int32_t(left));
    si->play |= 0x10;
    si->fade = FRAME_RATE_MUL;
    Music_Event(5, si->hash);
    si->state |= 1;
    Camera_SetFade(4, float(si->fade_ticks), 0xff, 1);
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        SSTREAM* st = &si->streams[i];
        if (st->type != 2 || st->obj == nullptr) continue;
        ANIMOBJ_tag* anim = anim_of(obj_of(st));
        if (reinterpret_cast<uint8_t*>(anim) + 0x70 != nullptr) anim->anim_flags |= 0x40000;
    }
}

// [decomp] 0x001dae60 Script_IsDeathNIS
// As cenas de morte (não podem ser puladas).
uint32_t Script_IsDeathNIS(uint32_t hash) {
    return (hash == 0x600098a || hash == 0x6000952 || hash == 0x6000991) ? 1 : 0;
}

// [decomp] 0x001daea8 Script_Init
void Script_Init() {
    Borders = nullptr;
    Borders2 = nullptr;
    ScriptCam = 0;
}

// [decomp] 0x001daec0 Script_FreeFromHashTable
// Solta o protótipo (as seções carregáveis o tiram ao voltar às animações da fase). Só no port: a marca
// -1 de "não é script" não é solta (o original passaria -1 a Mem_Free).
void Script_FreeFromHashTable(uint32_t hash) {
    void* p = hashtable_getitem(hash);
    if (p == nullptr) return;
    hashtable_deleteitem(hash);
    if (p != nf::p32_ptr<void>(0xffffffffu)) Mem_Free(&p);
}

// [decomp] 0x001daf00 Script_Set2Start
void Script_Set2Start(SCRIPTINFO* si) {
    if (si == nullptr) return;
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        si->streams[i].time = 0.0f;
        si->streams[i].cursor = si->streams[i].commands;
    }
    si->play &= 0xfd;
}

// [decomp] 0x001daf60 Script_SoundEnd
// Comando 23: só para sons em laço (os outros acabam sozinhos); num script em laço, o som que começou no
// quadro 0 continua no fim.
void Script_SoundEnd(SCRIPTINFO* si, SSTREAM* st) {
    if (Sound_IsLooping(handle_of(st)) == 0) return;
    if (st->time < float(si->end) || (si->play & 8) == 0 || st->u22 != 1) Script_KillStream(si, st);
}

// [decomp] 0x001dafe8 Script_EntityEnd
void Script_EntityEnd(SCRIPTINFO* si, SSTREAM* st) {
    if (st->obj == nullptr) return;
    if (st->time < float(si->end) || (si->play & 8) == 0 || st->u20 != 1) Script_KillStream(si, st);
}

// [decomp] 0x001db050 Script_SubScriptStart
// Comando 27: hash, tem pai, pai. O sub-script toca em laço preso aos quadros do stream.
void Script_SubScriptStart(SCRIPTINFO* si, SSTREAM* st) {
    const uint32_t hash = BIN_GetDWord(&st->cursor);
    const uint8_t has_parent = BIN_GetByte(&st->cursor);
    const uint8_t parent = BIN_GetByte(&st->cursor);
    if (st->obj == nullptr) st->u20 = uint16_t(int32_t(st->time) + 1);
    st->parent = has_parent == 0 ? int8_t(-1) : int8_t(parent);
    if (st->obj == nullptr) {
        _VECTOR* zero = const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR);
        st->obj = Script_Load(hash, zero, zero, nullptr, nullptr, nullptr, nullptr);
    }
    st->u22 = 0;
    st->type = 7;
    Script_Play(static_cast<SCRIPTINFO*>(st->obj), 1);
    Script_Interp(si, st);
}

// [decomp] 0x001db148 Script_SubScriptEnd
void Script_SubScriptEnd(SCRIPTINFO* si, SSTREAM* st) {
    if (st->obj == nullptr) return;
    if (st->time < float(si->end) || (si->play & 8) == 0 || st->u20 != 1) Script_KillStream(si, st);
}

// [decomp] 0x001db1b0 Script_LightStart
// Comando 24: tem pai, pai, tipo (1: luz que não ilumina objetos com a flag 0x100). Branca, raio 255
// até a primeira interpolação.
void Script_LightStart(SCRIPTINFO* si, SSTREAM* st) {
    const uint8_t has_parent = BIN_GetByte(&st->cursor);
    const uint8_t parent = BIN_GetByte(&st->cursor);
    const uint8_t kind = BIN_GetByte(&st->cursor);
    st->parent = has_parent == 0 ? int8_t(-1) : int8_t(parent);
    st->obj = Light_Create(const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), 0xff, 2, 3, 255.0f, 0, -1, kind == 1 ? 1 : 0,
                           1.0f, 0, 0, 0, 0xffff);
    st->type = 6;
    Script_Interp(si, st);
}

// [decomp] 0x001db298 Script_LightEnd
void Script_LightEnd(SCRIPTINFO* si, SSTREAM* st) { Script_KillStream(si, st); }

// [decomp] 0x001db2b8 Script_TextStart
// Comando 30: rótulo do texto e duração (legendas).
void Script_TextStart(SCRIPTINFO*, SSTREAM* st) {
    const uint32_t label = BIN_GetDWord(&st->cursor);
    const uint16_t time = BIN_GetWord(&st->cursor);
    Text_AddMsg(-1, 0, 4, Txt_BindLabel(label, 0), 0, int16_t(time));
}

// [decomp] 0x001db328 Script_AnimEnd
void Script_AnimEnd(SCRIPTINFO* si, SSTREAM* st) { Script_KillStream(si, st); }

// [decomp] 0x001db348 Script_CameraEnd
void Script_CameraEnd(SCRIPTINFO* si, SSTREAM* st) {
    if (st->time < float(si->end) || (si->play & 8) == 0) {
        Script_KillStream(si, st);
        si->state &= 0xef;
    }
}

// [decomp] 0x001db3b0 Script_SpriteEnd
void Script_SpriteEnd(SCRIPTINFO* si, SSTREAM* st) { Script_KillStream(si, st); }

// [decomp] 0x001db3d0 Script_FadeStart
// Comando 19: duração do fade (a câmera 4 escurece), salvo durante o avanço rápido.
void Script_FadeStart(SCRIPTINFO* si, SSTREAM* st) {
    const float t = BIN_GetFloat(&st->cursor);
    if ((si->state & 1) == 0) Camera_SetFade(4, -t, 0xff, 0);
}

// [decomp] 0x001db420 Script_StreamEnd
// Comando 4: quadro final do stream. Antes dele o comando se repete (cursor volta 3 bytes); no fim,
// um script em laço volta ao loop_start e os outros param.
void Script_StreamEnd(SCRIPTINFO* si, SSTREAM* st) {
    const float w = float(BIN_GetWord(&st->cursor));
    st->wait = w;
    if (st->time <= w && st->time < float(si->end)) {
        st->cursor -= 3;
        return;
    }
    if ((si->play & 8) != 0) {
        st->wait = 0.0f;
        st->cursor = st->commands;
        si->time = float(si->loop_start);
        st->time = float(si->loop_start) - FRAME_RATE_MUL;
    }
    if ((si->play & 8) == 0) si->play &= 0xfe;
    si->play |= 2;
}

// [decomp] 0x001db518 Script_UpdateCamera
// Copia a câmera do stream para a câmera 4 (sem sala: Camera_UpdateAll a acha de novo).
void Script_UpdateCamera(CAM_INFO* cam) {
    viewer_tag* v = glb_viewer[4];
    if (cam == nullptr || v == nullptr) return;
    amemcpy(&v->camera, cam, 0x40);
    Camera_CalcViewAngles(4, cam->fov * v->aspect);
    v->cel = nullptr;
}

// [decomp] 0x001db588 Script_Play
// loop 1: em laço. Um script com câmera vira a câmera de script (e as cenas de morte mudam a música).
// O original devolve 0 (ninguém usa).
void Script_Play(SCRIPTINFO* si, uint8_t loop) {
    if (si == nullptr) return;
    const uint8_t p = si->play;
    si->play = p | 1;
    si->play = loop == 0 ? uint8_t((p & 0xf7) | 1) : uint8_t(p | 9);
    if ((si->flags & 0x40) == 0) return;
    ScriptCam = si->hash;
    if (Script_IsDeathNIS(ScriptCam) != 0) Music_Event(6, 1);
    Text_FlushAllSubtitles();
}

// [decomp] 0x001db630 Script_IsPlaying
// Num script fora de laço, terminar (bit 2) conta como parado.
uint32_t Script_IsPlaying(SCRIPTINFO* si) {
    if (si == nullptr) return 0;
    if ((si->play & 8) != 0 || (si->play & 2) == 0) return si->play & 1u;
    return 0;
}

// [decomp] 0x001db668 Script_Stop
void Script_Stop(SCRIPTINFO* si) {
    if (si != nullptr) si->play &= 0xfe;
}

// [decomp] 0x001db688 Script_GetObj
// O objeto do stream, se for uma entidade ou animação.
obj_tag* Script_GetObj(SCRIPTINFO* si, uint16_t stream) {
    if (si == nullptr || si->stream_count <= stream) return nullptr;
    const uint8_t type = si->streams[stream].type;
    if (type < 3 && type != 0) return obj_of(&si->streams[stream]);
    return nullptr;
}

// [decomp] 0x001db6e0 Script_RemoveObj
// Um objeto do script está sendo apagado: desfaz o stream da entidade.
void Script_RemoveObj(obj_tag* obj, SCRIPTINFO* si) {
    if (obj == nullptr || si == nullptr) return;
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        if (si->streams[i].type == 1 && si->streams[i].obj == obj) {
            Script_KillStream(si, &si->streams[i]);
            return;
        }
    }
}

// [decomp] 0x001db770 Script_SetColour
// Cor do tweak (obj_tag::unknown_103) de todas as entidades e animações.
void Script_SetColour(SCRIPTINFO* si, uint8_t r, uint8_t g, uint8_t b) {
    if (si == nullptr) return;
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        obj_tag* o = Script_GetObj(si, i);
        if (o == nullptr) continue;
        o->unknown_103[2] = b;
        o->unknown_103[0] = r;
        o->unknown_103[1] = g;
    }
}

// [decomp] 0x001db808 Script_HideObj
// Esconde (flag 0x10) ou mostra as entidades e animações; os sons param.
void Script_HideObj(SCRIPTINFO* si, int8_t hide) {
    if (si == nullptr) return;
    for (uint16_t i = 0; i < si->stream_count; ++i) {
        obj_tag* o = Script_GetObj(si, i);
        if (o != nullptr) o->flags = hide == 0 ? (o->flags & 0xffffffefu) : (o->flags | 0x10);
        if (si->streams[i].type == 5) Script_KillStream(si, &si->streams[i]);
    }
}

// [decomp] 0x001db920 Script_SetPosRot
// Muda o lugar do script; parado numa pausa, recoloca os streams na hora.
void Script_SetPosRot(SCRIPTINFO* si, MATRIX* m) {
    Mat_Copy(m, &si->base);
    if (si->wait == 0) return;
    for (uint16_t i = 0; i < si->stream_count; ++i) Script_Interp(si, &si->streams[i]);
}

viewer_tag* Script_ActiveCamera() {
    viewer_tag* v4 = glb_viewer[4];
    viewer_tag* v0 = glb_viewer[0];
    if (ScriptCam == 0 || v4 == nullptr || v4->active == 0) return nullptr;
    if (v0 != nullptr && v0->active != 0) return nullptr;
    return v4;
}

void script_debug_power_on() {
    Borders = nullptr;
    Borders2 = nullptr;
    std::memset(&tptr, 0, sizeof(tptr));
    g_script_stats = ScriptStats{};
    Cmd_160 = 0;
    std::memset(Keys_235, 0, sizeof(Keys_235));
    std::memset(Weights_236, 0, sizeof(Weights_236));
    std::memset(&Res_237, 0, sizeof(Res_237));
    std::memset(TmpV_141, 0, sizeof(TmpV_141));
    std::memset(&TmpM_228, 0, sizeof(TmpM_228));
    Clr1_238 = Clr2_239 = nullptr;
}
