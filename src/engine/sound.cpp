// Os sons do jogo do lado do EE: ver sound.h.

#include "engine/sound.h"

#include <cmath>
#include <cstring>

#include "engine/game.h"
#include "engine/gamestate.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/text.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi_sound.h"
#include "platform/sfx_iop.h"

uint32_t SoundInit = 0;
uint32_t GNumMapSounds = 0;
MAPSOUND_tag GMapSounds[50];
uint8_t GMapSoundActive[50];
uint32_t GMapSoundHandle[50];
_VECTOR GlobalListenerPos;
uint32_t Sound_FrameDelay = 0;
SoundStats g_sound_stats;

namespace {

DLISTINFO_tag DynamicSoundList;     // 0x00349b00
uint8_t FrontEndBankToggle = 0;     // 0x002e1b11 o menu principal alterna os bancos 0xc e 0x38

DYNAMICSOUNDS* first_sound() { return reinterpret_cast<DYNAMICSOUNDS*>(nf::raw(DynamicSoundList.used.first)); }
DYNAMICSOUNDS* as_sound(uint32_t handle) { return nf::p32_ptr<DYNAMICSOUNDS>(handle); }

const uint8_t* out_data(uint32_t sound) { return sound < 0x60d ? SFXOutputData + sound * 0x18 : nullptr; }
uint8_t out_loop(uint32_t sound) { return sound < 0x60d ? SFXOutputData[sound * 0x18 + 0x14] : 0; }
uint8_t out_true3d(uint32_t sound) { return sound < 0x60d ? SFXOutputData[sound * 0x18 + 0x15] : 0; }
float out_float(uint32_t sound, int offset, float fallback) {
    const uint8_t* d = out_data(sound);
    if (d == nullptr) return fallback;
    float f;
    std::memcpy(&f, d + offset, 4);
    return f;
}

uint32_t mp_word(int offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
uint32_t listener_count() { return mp_word(0x180) != 0 ? mp_word(0x1ac) : 1; }

// Só para o contador do port: o som tem fala em Snd2Lbl.
bool is_speech(uint32_t sound) {
    for (const SND2LBL_tag& e : Snd2Lbl) {
        if (e.sound == sound) return true;
    }
    return false;
}


}  // namespace

// [decomp] 0x001ddf88 Sound_Init
void Sound_Init() {
    DList_Init(&DynamicSoundList, 0x60, 0x100);
    SFXInitialise(nullptr);
    std::memset(EventList, 0, sizeof(EventList));
}

// [decomp] 0x001ddfd0 Sound_Kill
void Sound_Kill() {
    if (SoundInit != 0) SFXReset();
    SoundInit = 0;
}

// [decomp] 0x001dceb8 Sound_Ready
// No carregamento da fase (LoaderLoad): limpa o IOP e carrega o banco da fase.
void Sound_Ready() {
    SFXReset();
    SFXSetNumberOfListeners(listener_count());
    switch (GameState.level_file) {
        case 0x7000001: SFXLoadSoundBank(1); break;
        case 0x7000002: SFXLoadSoundBank(2); break;
        case 0x7000003: SFXLoadSoundBank(3); break;
        case 0x7000004: SFXLoadSoundBank(8); break;
        case 0x7000005: SFXLoadSoundBank(5); break;
        case 0x7000006: SFXLoadSoundBank(4); break;
        case 0x7000007: SFXLoadSoundBank(6); break;
        case 0x7000008: SFXLoadSoundBank(7); break;
        case 0x7000009: SFXLoadSoundBank(0x17); break;
        case 0x700000a: SFXLoadSoundBank(0x18); break;
        case 0x700000b: SFXLoadSoundBank(0x19); break;
        case 0x700000c: SFXLoadSoundBank(0x1a); break;
        case 0x700000d: SFXLoadSoundBank(0x37); break;
        case 0x7000011: SFXLoadSoundBank(0x1e); break;
        case 0x7000012: SFXLoadSoundBank(0x1f); break;
        case 0x7000013: SFXLoadSoundBank(0x20); break;
        case 0x7000014: SFXLoadSoundBank(0x27); break;
        case 0x7000015: SFXLoadSoundBank(0x28); break;
        case 0x7000016: SFXLoadSoundBank(0x2b); break;
        case 0x700001a:
        case 0x700001b: SFXLoadSoundBank(0x25); break;
        case 0x7000021:
        case 0x7000022:
        case 0x7000023:
        case 0x7000024:
        case 0x7000025:
        case 0x7000026:
        case 0x7000027:
        case 0x7000028:
        case 0x7000029:
        case 0x700004b:
        case 0x700004c: SFXLoadSoundBank(0x16); break;
        case 0x7000041: SFXLoadSoundBank(0xb); break;
        case 0x7000048:
            SFXLoadSoundBank(FrontEndBankToggle == 0 ? 0xc : 0x38);
            FrontEndBankToggle = uint8_t(1 - FrontEndBankToggle);
            break;
        case 0x700004a: SFXLoadSoundBank(0x21); break;
        default: break;
    }
    SoundInit = 1;
    Music_Event(0, GameState.level_file);
    Music_Event(1, 1);
}

// [decomp] 0x001ddff8 Sound_LoadMapSounds
// Bloco 0x28 do mapa: os sons de ambiente. Divergência: o original copia quantos o bloco disser para os 50 de
// GMapSounds; aqui mais de 50 não são copiados (escreveria além da tabela; nenhum mapa do disco passa de 50).
void Sound_LoadMapSounds(uint8_t* data) {
    int32_t n;
    std::memcpy(&n, data, 4);
    GNumMapSounds = uint32_t(n);
    std::memset(GMapSoundActive, 0, sizeof(GMapSoundActive));
    if (n > 50) {
        GNumMapSounds = 50;
        n = 50;
    }
    if (n > 0) std::memcpy(GMapSounds, data + 4, size_t(n) * sizeof(MAPSOUND_tag));
}

// [decomp] 0x001dd118 HandleMapSoundAllocation
void HandleMapSoundAllocation() {
    if (GNumMapSounds == 0 || int32_t(GNumMapSounds) <= 0) return;
    for (int i = 0; i < int32_t(GNumMapSounds); ++i) {
        MAPSOUND_tag& m = GMapSounds[i];
        _VECTOR pos{};
        pos.x = m.pos[0];
        pos.y = m.pos[1];
        pos.z = m.pos[2];
        const float d2 = Vec_SqDist3D(&GlobalListenerPos, &pos);
        if (GMapSoundActive[i] == 0) {
            if (d2 < m.radius * m.radius) {
                GMapSoundHandle[i] =
                    Sound_Play3D(m.sound, &pos, 100.0f, m.radius, m.min_range, int16_t(int32_t(m.a)), 0, 1);
                GMapSoundActive[i] = 1;
            }
        } else if (m.radius * m.radius * 1.2f < d2) {
            Sound_Stop(GMapSoundHandle[i]);
            GMapSoundActive[i] = 0;
        }
    }
}

// [decomp] 0x001dd278 Sound_UpdateListeners
// O ouvinte é a câmera de cada jogador (ou a câmera 4 durante uma cena); a sala dela dá o ambiente. A velocidade
// (pos - prev_pos) vai zerada ao IOP (SFXSetupListener a zera).
void Sound_UpdateListeners() {
    const uint32_t n = mp_word(0x180) != 0 ? (mp_word(0x1ac) & 0xffff) : 1;
    viewer_tag* last = glb_viewer[0];
    _VECTOR vel{};
    for (uint32_t i = 0; i < n; ++i) {
        viewer_tag* v = i < uint32_t(kViewerCount) ? glb_viewer[i] : nullptr;
        last = v;
        if (v == nullptr) continue;
        vel.x = v->pos.x - v->prev_pos.x;
        vel.y = v->pos.y - v->prev_pos.y;
        vel.z = v->pos.z - v->prev_pos.z;
        _VECTOR up, dir, norm;
        Mat_GetUp(&up, &v->camera);
        Mat_GetDir(&dir, &v->camera);
        Mat_GetNorm(&norm, &v->camera);
        const float pos[3] = {v->camera.m[3][0], v->camera.m[3][1], v->camera.m[3][2]};
        float vv[3] = {vel.x, vel.y, vel.z};
        const float d[3] = {dir.x, dir.y, dir.z}, u[3] = {up.x, up.y, up.z}, nn[3] = {norm.x, norm.y, norm.z};
        SFXSetupListener(pos, vv, d, u, nn, int32_t(i));
    }
    viewer_tag* script = glb_viewer[4];
    if (script != nullptr && script->active != 0) {
        _VECTOR up, dir, norm;
        Mat_GetUp(&up, &script->camera);
        Mat_GetDir(&dir, &script->camera);
        Mat_GetNorm(&norm, &script->camera);
        const float pos[3] = {script->camera.m[3][0], script->camera.m[3][1], script->camera.m[3][2]};
        float vv[3] = {vel.x, vel.y, vel.z};
        const float d[3] = {dir.x, dir.y, dir.z}, u[3] = {up.x, up.y, up.z}, nn[3] = {norm.x, norm.y, norm.z};
        SFXSetupListener(pos, vv, d, u, nn, 0);
        last = script;
    }
    if (last != nullptr) {
        GlobalListenerPos.x = last->camera.m[3][0];
        GlobalListenerPos.y = last->camera.m[3][1];
        GlobalListenerPos.z = last->camera.m[3][2];
        GlobalListenerPos.w = last->camera.m[3][3];
        cel_tag* cel = last->cel;
        if (cel != nullptr) {
            int16_t reverb;
            std::memcpy(&reverb, cel->misc, 2);
            const bool outdoors = (cel->type & 0x10000000) != 0 || reverb >= 0x14;
            SFXSetEnvironment(reverb, outdoors ? 1 : 0);
            HandleMapSoundAllocation();
        }
        _VECTOR lp = GlobalListenerPos;
        Sound_UpdateSounds(&lp);
    }
    SFXUpdate();
}

// [decomp] 0x001dcc68 Sound_ReqestPlaySfx
// Manda o pedido ao IOP. Um som "3D de verdade" (SFXOutputData +0x15) fora do laço e longe (além de 1.1 x o alcance,
// com um ouvinte) não começa. audible (+0x52) decide a legenda: os 2D sempre; os 3D com cena de câmera, sem a flag de
// 3D ou a menos de metade do alcance.
void Sound_ReqestPlaySfx(DYNAMICSOUNDS* s) {
    if (s->is3d == 0) {
        SFXStart(s->sound & 0xfffff, nf::p32_u(s));
        s->audible = 1;
        ++g_sound_stats.played;
        return;
    }
    const float dist = Vec_Dist3D(&GlobalListenerPos, &s->pos);
    const uint32_t listeners = listener_count();
    if (out_loop(s->sound) == 0 && listeners == 1 && out_true3d(s->sound) != 0 && s->max_range * 1.1f < dist) {
        nf::sfx_debug_log("ee som 0x%x longe: %.1f m (alcance %.1f), ouvinte %.2f,%.2f,%.2f\n", s->sound, dist,
                          s->max_range, GlobalListenerPos.x, GlobalListenerPos.y, GlobalListenerPos.z);
        return;
    }
    const float pos[3] = {s->pos.x, s->pos.y, s->pos.z};
    const float vel[3] = {s->vel.x, s->vel.y, s->vel.z};
    s->audible = dist < s->max_range * 0.5f ? 1 : 0;
    if (ScriptCam != 0 || out_true3d(s->sound) == 0) s->audible = 1;
    if (s->has_ranges == 0) {
        SFXStart3D(s->sound & 0xfffff, pos, vel, nf::p32_u(s), int32_t(s->volume), -1, -1);
    } else {
        SFXStart3D(s->sound & 0xfffff, pos, vel, nf::p32_u(s), int32_t(s->volume), int32_t(s->min_range),
                   int32_t(s->max_range));
    }
    ++g_sound_stats.played;
}

// [decomp] 0x001dda70 Sound_UpdateSounds
void Sound_UpdateSounds(_VECTOR* /*listener*/) {
    DYNAMICSOUNDS* s = first_sound();
    if (s == nullptr) return;
    uint8_t age = s->age;
    for (;;) {
        const uint8_t state = s->state;
        DYNAMICSOUNDS* next = s->next;
        s->age = uint8_t(age + 1);
        if (state == 1) {
            const bool playing = out_loop(s->sound) != 0 ? s->state == 1 : SFXIsSFXPlaying(nf::p32_u(s), -1) != 0;
            if (!playing) s->state = 2;
        } else if (state == 0) {
            Sound_ReqestPlaySfx(s);
            s->state = 1;
            if (s->audible != 0) {
                if (is_speech(s->sound)) ++g_sound_stats.speech;
                Sound_DoSubtitle(s->sound);
            }
        } else if (state == 2) {
            SFXRemove(nf::p32_u(s), 0xffffffffu);
            DList_RemoveFromInUse2Free(&DynamicSoundList, reinterpret_cast<LLNODE_tag*>(s));
            s->state = 3;
            s->ref = 0;
        }
        if (s->is3d != 0 && s->state == 1) {
            const float pos[3] = {s->pos.x, s->pos.y, s->pos.z};
            const float vel[3] = {s->vel.x, s->vel.y, s->vel.z};
            SFXSetPositionAndVelocity(pos, vel, nf::p32_u(s), -1);
        }
        s->moved = 0;
        if (next == nullptr) return;
        age = next->age;
        s = next;
    }
}

// [decomp] 0x001dd880 Sound_Play
uint32_t Sound_Play(uint32_t sound, float volume, int16_t a, uint32_t ref) {
    ++g_sound_stats.calls;
    const uint8_t delay = uint8_t(Sound_FrameDelay);
    Sound_FrameDelay = 0;
    const uint32_t id = sound & 0xfffff;
    if (id == 0xffff) return 0;
    auto* s = static_cast<DYNAMICSOUNDS*>(DList_MoveFromFree2InUse(&DynamicSoundList));
    if (s == nullptr) {
        ++g_sound_stats.no_node;
        return 0;
    }
    s->has_ranges = 0;
    s->max_range = out_float(id, 8, 20.0f);
    s->min_range = out_float(id, 4, 10.0f);
    s->volume = volume;
    s->is3d = 0;
    s->a = a;
    s->moved = 1;
    s->state = 0;
    s->sound = sound;
    s->age = 0;
    s->alertness = out_float(id, 0xc, 0.0f);
    s->ref = ref;
    s->audible = 0;
    s->map_sound = 0;
    s->unknown_54 = 0;
    s->pos = CONSTANT_ZERO_VECTOR;
    s->vel = CONSTANT_ZERO_VECTOR;
    s->frame_delay = delay;
    ++g_sound_stats.requested;
    return nf::p32_u(s);
}

// [decomp] 0x001dd548 Sound_Play3D
// Com map_sound os alcances do pedido valem (> 0; senão os da tabela) e o IOP os recebe; sem ele, os da tabela.
uint32_t Sound_Play3D(uint32_t sound, _VECTOR* pos, float volume, float max_range, float min_range, int16_t a,
                      uint32_t ref, int32_t map_sound) {
    ++g_sound_stats.calls;
    nf::sfx_debug_log("ee Sound_Play3D 0x%x em %.2f,%.2f,%.2f\n", sound, pos->x, pos->y, pos->z);
    const uint8_t delay = uint8_t(Sound_FrameDelay);
    const uint32_t id = sound & 0xfffff;
    Sound_FrameDelay = 0;
    if (id == 0xffff) return 0;
    auto* s = static_cast<DYNAMICSOUNDS*>(DList_MoveFromFree2InUse(&DynamicSoundList));
    if (s == nullptr) {
        ++g_sound_stats.no_node;
        return 0;
    }
    if (map_sound == 0) {
        s->has_ranges = 0;
        s->max_range = out_float(id, 8, 20.0f);
        s->min_range = out_float(id, 4, 10.0f);
    } else {
        uint32_t ranges = 0;
        if (min_range <= 0.0f) {
            min_range = out_float(id, 4, 10.0f);
        } else {
            ranges = 1;
        }
        if (max_range <= 0.0f) {
            max_range = out_float(id, 8, 20.0f);
        } else {
            ranges = 1;
        }
        s->has_ranges = ranges;
        s->max_range = max_range;
        s->min_range = min_range;
    }
    s->volume = volume;
    s->is3d = 1;
    s->a = a;
    s->moved = 1;
    s->state = 0;
    s->sound = sound;
    s->age = 0;
    s->alertness = out_float(id, 0xc, 0.0f);
    s->ref = ref;
    s->audible = 0;
    s->unknown_54 = 0;
    s->pos = *pos;
    s->vel = CONSTANT_ZERO_VECTOR;
    s->map_sound = uint8_t(map_sound);
    s->frame_delay = delay;
    ++g_sound_stats.requested;
    return nf::p32_u(s);
}

// [decomp] 0x001de0a8 Sound_Stop
void Sound_Stop(uint32_t handle) {
    if (handle != 0) as_sound(handle)->state = 2;
}

// [decomp] 0x001de0c8 Sound_StopAllWithId
void Sound_StopAllWithId(uint32_t sound) {
    for (DYNAMICSOUNDS* s = first_sound(); s != nullptr; s = s->next) {
        if (s->sound == sound) s->state = 2;
    }
}

// [decomp] 0x001de108 Sound_SetPosition
// O original devolve se o som ainda toca (ninguém no port usa).
void Sound_SetPosition(uint32_t handle, _VECTOR* pos) {
    if (pos == nullptr || handle == 0) return;
    DYNAMICSOUNDS* s = as_sound(handle);
    s->pos = *pos;
    s->moved = 1;
}

// [decomp] 0x001de190 Sound_SetVolume
// O volume vai para o nó e não chega mais ao IOP (que já ignorava o do pedido).
void Sound_SetVolume(uint32_t handle, float volume) {
    if (handle == 0) return;
    DYNAMICSOUNDS* s = as_sound(handle);
    s->volume = volume;
    s->moved = 1;
}

// [decomp] 0x001de210 Sound_SetAlertness
void Sound_SetAlertness(uint32_t handle, float level) {
    if (handle != 0) as_sound(handle)->alertness = level;
}

// [decomp] 0x001de220 Sound_ModAlertness
void Sound_ModAlertness(uint32_t handle, float level) {
    if (handle != 0) as_sound(handle)->alertness *= level;
}

// [decomp] 0x001de240 Sound_IsFinnished
uint32_t Sound_IsFinnished(uint32_t handle) { return handle != 0 ? (as_sound(handle)->state == 2 ? 1 : 0) : 1; }

// [decomp] 0x001de260 Sound_IsLooping
uint32_t Sound_IsLooping(uint32_t handle) {
    if (handle == 0) return 1;
    const uint32_t sound = as_sound(handle)->sound;
    if (sound > 0x60c) return 0;
    return SFXOutputData[sound * 0x18 + 0x14];
}

// [decomp] 0x001de2a8 Sound_ZeroAlertness
void Sound_ZeroAlertness() {
    for (DYNAMICSOUNDS* s = first_sound(); s != nullptr; s = s->next) {
        if (uint8_t(s->state - 2) < 2) continue;
        if (out_loop(s->sound) == 0 && s->alertness != 0.0f) s->alertness = 0.0f;
    }
}

// [decomp] 0x001ddc48 Sound_Alertness
// O que se ouve em pos: a soma, pelos sons não-laço com alerta e em alcance, de alerta x (1 perto do mínimo, caindo
// até 0 no máximo). level recebe o maior valor da soma; sound, o som em que a soma passou dele. Os sons 0x11c e
// 0x11d não contam em 07000009/0a; o 0x248 passa a ter mínimo = máximo e alerta 10. O tipo de volta é float (o
// Ghidra o lia como int).
float Sound_Alertness(_VECTOR* pos, float* level, DYNAMICSOUNDS** sound) {
    float sum = 0.0f;
    float best = 0.0f;
    if (sound != nullptr) *sound = nullptr;
    for (DYNAMICSOUNDS* s = first_sound(); s != nullptr; s = s->next) {
        if (uint8_t(s->state - 2) < 2) continue;
        if (out_loop(s->sound) != 0) continue;
        if (s->alertness == 0.0f) continue;
        const float d2 = Vec_SqDist3D(pos, &s->pos);
        const float max = s->max_range;
        if (max * max < d2) continue;
        const uint32_t id = s->sound & 0xfffff;
        if (id >= 0x11c) {
            if (id < 0x11e) {
                if (GameState.level_file - 0x7000009u < 2u) continue;
            } else if (id == 0x248) {
                s->min_range = max;
                s->alertness = 10.0f;
            }
        }
        const float d = std::sqrt(d2);
        const float factor = d < s->min_range ? 1.0f : (s->max_range - d) / (s->max_range - s->min_range);
        sum += factor * s->alertness;
        if (best < sum) {
            if (sound != nullptr) *sound = s;
            best = sum;
        }
    }
    *level = best;
    return sum > 0.0f ? sum : 0.0f;
}

// [decomp] 0x001de370 Sound_Stop_Ref
void Sound_Stop_Ref(uint32_t handle, uint32_t ref) {
    if (handle == 0) return;
    DYNAMICSOUNDS* s = as_sound(handle);
    if (s->ref == ref && s->state != 3) s->state = 2;
}

// [decomp] 0x001de3a0 Sound_Playing_Ref
uint32_t Sound_Playing_Ref(uint32_t handle, uint32_t ref) {
    if (handle == 0) return 0;
    const DYNAMICSOUNDS* s = as_sound(handle);
    return s->ref == ref && s->state <= 1 ? 1 : 0;
}

namespace {

// Os dados de um gatilho de som (tipo 0x40, 0x14 bytes).
struct SOUNDTRIGGER_tag {
    uint16_t channel;     // 0x00 o canal que o liga (parâmetro 0)
    uint16_t stop;        // 0x02 o canal que o para (parâmetro 4)
    uint16_t once;        // 0x04 1: apagado depois de tocar (parâmetro 1)
    uint16_t sound;       // 0x06 parâmetro 2
    uint16_t is_2d;       // 0x08 != 0: Sound_Play; 0: Sound_Play3D na posição (parâmetro 3)
    uint16_t pad_0a;
    uint32_t handle;      // 0x0c o som tocando (0 nenhum)
    uint8_t exclusive;    // 0x10 o único desses tocando (GlobalVoiceHandle; parâmetro 5)
    uint8_t keep;         // 0x11 continua com o canal desligado (parâmetro 7)
    uint8_t pad_12[2];
};
static_assert(sizeof(SOUNDTRIGGER_tag) == 0x14);

uint32_t GlobalVoiceHandle = 0;  // 0x0030d794 o som do gatilho exclusivo que toca

// Os canais são bytes de switch_channels[256]; um número maior (nenhum no disco) leria além da tabela no original e
// aqui conta como desligado.
bool channel_on(uint32_t ch) { return ch < 256 && switch_channels[ch] != 0; }

}  // namespace

// [decomp] 0x001b2628 SoundTrigger_Create
// Sem som (0 ou 0xffff), sem canal ou fora da dificuldade (parâmetro 6), não cria nada. Zera GlobalVoiceHandle (a
// cada posicionamento desse tipo, criado ou não).
obj_tag* SoundTrigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    GlobalVoiceHandle = 0;
    const uint32_t* p = level->params;
    if (p[2] == 0 || p[2] == 0xffff || p[0] == 0) return nullptr;
    const uint32_t difficulty = GameState.difficulty;
    const uint32_t only = p[6];
    if (only != 0 && only < difficulty) return nullptr;
    if ((only == 5 && difficulty != 1) || (only == 6 && difficulty != 2) || (only == 7 && difficulty != 3)) {
        return nullptr;
    }
    obj_tag* obj = control_create_object(sizeof(SOUNDTRIGGER_tag), pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    SOUNDTRIGGER_tag* d = static_cast<SOUNDTRIGGER_tag*>(obj->data);
    d->channel = uint16_t(p[0]);
    d->once = uint16_t(p[1]);
    d->sound = uint16_t(p[2]);
    d->is_2d = uint16_t(p[3]);
    d->handle = 0;
    d->stop = uint16_t(p[4]);
    d->exclusive = uint8_t(p[5]);
    d->keep = uint8_t(p[7]);
    obj->type = 0x40;
    obj->glist = glist;
    obj->flags = glist != nullptr ? glist->flags : 0;  // guarda do port: o original lê glist->flags sem conferir
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001b2790 SoundTrigger_Update
// Parado: com o canal de parada ligado, apaga-se; com o canal ligado, toca (um exclusivo com outro tocando se apaga).
// Tocando: quando o som acaba, ou quando o canal de parada liga ou (sem "keep") o canal desliga, para; os de uma vez
// só (once = 1) se apagam, os exclusivos também quando parados.
void SoundTrigger_Update(obj_tag* obj) {
    SOUNDTRIGGER_tag* d = static_cast<SOUNDTRIGGER_tag*>(obj->data);
    if (d->handle == 0) {
        if (channel_on(d->stop)) {
            obj->life |= 1;
            return;
        }
        if (!channel_on(d->channel)) return;
        if (d->exclusive != 0 && GlobalVoiceHandle != 0) {
            obj->life |= 1;
            return;
        }
        d->handle = d->is_2d == 0 ? Sound_Play3D(d->sound, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0)
                                  : Sound_Play(d->sound, 100.0f, 0, 0);
        if (d->exclusive != 0) GlobalVoiceHandle = d->handle;
        return;
    }
    if (Sound_IsFinnished(d->handle) == 0) {
        bool stop = d->stop != 0 && channel_on(d->stop);
        if (d->keep == 0 && !channel_on(d->channel)) stop = true;
        if (!stop) return;
        Sound_Stop(d->handle);
        d->handle = 0;
        if (d->exclusive != 0) {
            GlobalVoiceHandle = 0;
            obj->life |= 1;
        }
    } else {
        d->handle = 0;
        if (d->exclusive != 0) GlobalVoiceHandle = 0;
    }
    if (d->once == 1) obj->life |= 1;
}

int sound_nodes_in_use() { return DynamicSoundList.used.count; }

void sound_debug_power_on() {
    GlobalVoiceHandle = 0;
    std::memset(&DynamicSoundList, 0, sizeof(DynamicSoundList));
    SoundInit = 0;
    GNumMapSounds = 0;
    std::memset(GMapSounds, 0, sizeof(GMapSounds));
    std::memset(GMapSoundActive, 0, sizeof(GMapSoundActive));
    std::memset(GMapSoundHandle, 0, sizeof(GMapSoundHandle));
    GlobalListenerPos = _VECTOR{};
    Sound_FrameDelay = 0;
    FrontEndBankToggle = 0;
    music_debug_power_on();
    g_sound_stats = SoundStats{};
}
