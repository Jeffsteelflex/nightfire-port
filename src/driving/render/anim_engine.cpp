// O motor de animação das instâncias (driving/render/anim_engine.h). Portado do MIPS do PS2 (as desmontagens em
// out/agent_drv_weapons/s_ae do checkout principal); as divergências estão no cabeçalho e em cada função.

#include "driving/render/anim_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#include "driving/carp_runtime.h"
#include "driving/game/events.h"
#include "driving/game/triggers.h"
#include "driving/anim/act_actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

AnimEngineGlobals g_engine;
AnimEngineStats g_stats;
// Os blocos dos Handles (o heap do jogo): o endereço p32 -> o armazenamento.
std::map<uint32_t, std::unique_ptr<uint8_t[]>> g_blocks;

uint8_t* P(uint32_t p32) { return nf::p32_ptr<uint8_t>(p32); }
uint32_t r32(uint32_t p32, uint32_t off) {
    uint32_t v = 0;
    if (const uint8_t* p = P(p32)) std::memcpy(&v, p + off, 4);
    return v;
}
uint16_t r16(uint32_t p32, uint32_t off) {
    uint16_t v = 0;
    if (const uint8_t* p = P(p32)) std::memcpy(&v, p + off, 2);
    return v;
}
uint8_t r8(uint32_t p32, uint32_t off) {
    const uint8_t* p = P(p32);
    return p ? p[off] : 0;
}
void w8(uint32_t p32, uint32_t off, uint8_t v) {
    if (uint8_t* p = P(p32)) p[off] = v;
}

AnimHandleData& H(uint32_t h) { return *nf::p32_ptr<AnimHandleData>(h); }
AnimSystemData& S(uint32_t s) { return *nf::p32_ptr<AnimSystemData>(s); }
uint32_t instance_of(const AnimHandleData& h, uint32_t index) {
    return nf::p32_u(&h) + 0x40 + index * 0x40;
}

// fptoui (0x24e340, o __unpack_f do libgcc): o float positivo truncado para sem sinal; negativo, zero ou NaN 0, infinito
// ou acima de 2^32 0xffffffff.
uint32_t fptoui(float f) {
    const uint32_t b = ps2::fbits(f);
    if (b & 0x80000000u) return 0;
    const uint32_t e = (b >> 23) & 0xff, frac = b & 0x7fffff;
    if (e == 0) return 0;
    if (e == 255) return frac ? 0 : 0xffffffffu;
    const int32_t x = int32_t(e) - 127;
    if (x < 0) return 0;
    if (x >= 32) return 0xffffffffu;
    const uint32_t m = frac | 0x800000u;
    return x >= 23 ? m << (x - 23) : m >> (23 - x);
}

// O float sem sinal do MIPS (cvt.s.w de um inteiro com o bit 31: a metade arredondada, somada a si mesma).
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(h, h);
}

// [driving] 0x001a3348 (sem nome): a animação de (estímulo, estado) entre as `count` primeiras do artigo (a busca
// binária do CARP::AnimInfo por (+0xc, +0xd)); 0 sem.
uint32_t find_anim(uint32_t article, uint32_t count, uint32_t state, uint32_t stimulus) {
    const uint32_t anims = r32(article, 8);
    uint32_t first = anims;
    int32_t len = int32_t(count);
    const uint8_t ks = uint8_t(stimulus), kt = uint8_t(state);
    while (len > 0) {
        const int32_t half = len >> 1;
        const uint32_t mid = first + uint32_t(half) * 0x10;
        const uint8_t s = r8(mid, 0xc);
        const bool less = s != ks ? s < ks : r8(mid, 0xd) < kt;
        if (less) {
            first = mid + 0x10;
            len = len - half - 1;
        } else {
            len = half;
        }
    }
    if (first == anims + count * 0x10) return 0;
    if (r16(first, 0xc) != uint16_t(ks | (kt << 8))) return 0;
    return first;
}

// [driving] 0x001c8b08 RSceneObj::SetEventDynamicData (a entrada +0x60 da tabela virtual; o RVehicle e os outros
// objetos de cena usam esta): o EventDynamicData zerado com o objeto, o -1 e o dono físico (+0x24: a bandeira 1 do +0x44
// e a vaga do +0x46).
void scene_set_event_data(uint32_t scene) {
    EventDynamicData& d = trigger_manager().event_data;
    std::memset(&d, 0, sizeof d);
    d.scene_obj = scene;
    d.instance_index = -1;
    d.index = -1;
    const uint32_t owner = r32(scene, 0x24);
    if (owner != 0) {
        d.flag = r16(owner, 0x44) & 1u;
        d.index = int16_t(r16(owner, 0x46));
    }
}

// [driving] 0x001a2700 RAnimEngine::Handle::SetEventDynamicData (a entrada +0x60 da tabela virtual do dono: a do ActWeapon,
// 0x367458, põe também o ActWeapon no EventDynamicData +0x34; driving/anim/act_actor.h)
void handle_set_event_data(uint32_t handle) {
    const uint32_t scene = H(handle).scene_obj;
    if (scene == 0) return;
    if (const uint32_t weapon = anim::ActWeapon_FromScene(scene)) anim::ActWeapon_SetEventDynamicData(weapon);
    else scene_set_event_data(scene);
}

// [driving] 0x001a1af8 (sem nome): o sistema `index` da lista acabou: o estado de chegada; com `next` o primeiro da
// fila começa; o sistema sai da lista. Devolve o índice anterior.
uint32_t finish(uint32_t index, uint32_t ticks, bool next) {
    ++g_stats.finished;
    const uint32_t s = g_engine.active[index];
    AnimSystemData& sys = S(s);
    sys.playing = 0;
    sys.state = sys.next_state;
    if (next && sys.queue[0] >= 0) {
        AnimSystem_ProcessStimuli(s, uint32_t(int32_t(sys.queue[0])), ticks, 0);
        sys.queue[0] = sys.queue[1];
        sys.queue[1] = sys.queue[2];
        sys.queue[2] = sys.queue[3];
        sys.queue[3] = -1;
    }
    // MEM_copy(&lista[index], &lista[index + 1], (contagem - index) x 4): o PS2 copia uma palavra além da última.
    for (uint32_t i = index; i < g_engine.count && i + 1 < 256; ++i) g_engine.active[i] = g_engine.active[i + 1];
    --g_engine.count;
    return index - 1;
}

}  // namespace

void RSceneObj_SetEventDynamicData(uint32_t scene) { scene_set_event_data(scene); }

AnimEngineGlobals& anim_engine() { return g_engine; }
AnimEngineStats& anim_engine_stats() { return g_stats; }

void anim_engine_reset(float video_rate) {
    g_engine = AnimEngineGlobals{};
    g_engine.frequency = ee::div(1.0f, video_rate);  // RTimeData::Init (0x1d1ec0)
    g_blocks.clear();
}

uint32_t anim_ticks(uint32_t steps) {
    return fptoui(ee::mul(ee::mul(u2f(steps), g_engine.frequency), 60.0f));
}

bool AnimHandle_EffectWanted(uint32_t mirrored, uint32_t flags) {
    return (flags & 0x109e) != 0 && (mirrored == 0 || (flags & 0x200) != 0);
}

uint32_t AnimHandle_Create(uint32_t n, uint32_t steps, uint32_t instances, uint32_t damage, uint32_t scene_obj) {
    ++g_stats.handles;
    const uint32_t ticks = anim_ticks(steps);
    std::vector<uint8_t> ids;
    uint32_t n_damage = 0, n_effects = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t inst = instances + i * 0x40;
        const uint32_t mirrored = r8(inst, 0xc) >> 7;
        if (damage != 0 && (r8(inst, 0xc) & 0x10)) ++n_damage;
        const uint32_t art = r32(inst, 0x1c);
        if (art == 0) continue;
        const uint32_t anim = r32(art, 8);
        if (anim != 0) {
            // O PS2 grava os identificadores no próprio artigo (+0x1a, +0x1b: os bytes de baixo do +4 e do +6).
            w8(art, 0x1a, r8(anim, 4));
            w8(art, 0x1b, r8(anim, 6));
            ids.push_back(r8(art, 0x1a + (mirrored ? 1 : 0)));
        }
        const uint32_t eff = r32(art, 4);
        if (eff != 0 && r8(eff, 0x14) != 0) {
            for (uint32_t j = 0; r8(eff + j * 0x40, 0x14) != 0; ++j)
                if (AnimHandle_EffectWanted(mirrored, r32(eff + j * 0x40, 0x10))) ++n_effects;
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const uint32_t n_sys = uint32_t(ids.size());
    const uint32_t size = (n << 6) + (n_damage << 5) + (n_sys << 6) + (n_effects << 6) + (n_effects << 1) + n_effects +
                          n_sys;
    // [driving] 0x001a1f10 RAnimEngine::Handle::operator new(0x40, tamanho): o bloco do cabeçalho mais o resto.
    std::unique_ptr<uint8_t[]> block(new uint8_t[0x40 + size + 16]());
    const uint32_t h = nf::p32_u(block.get());
    g_blocks[h] = std::move(block);

    // [driving] 0x001a1f58 RAnimEngine::Handle::Handle
    AnimHandleData& hd = H(h);
    hd.n_instances = uint8_t(n);
    hd.n_effects = uint8_t(n_effects);
    hd.n_damage = uint8_t(n_damage);
    hd.n_systems = uint8_t(n_sys);
    const uint32_t inst_copies = h + 0x40;
    const uint32_t effects = inst_copies + uint32_t(hd.n_instances) * 0x40;
    const uint32_t dmg = effects + uint32_t(hd.n_effects) * 0x40;
    const uint32_t systems = dmg + uint32_t(hd.n_damage) * 0x20;
    const uint32_t effect_bits = systems + uint32_t(n_sys & 0xff) * 0x40;
    const uint32_t effect_bytes = effect_bits + uint32_t(hd.n_effects) * 2;
    hd.size = size;
    hd.system_ids = effect_bytes + hd.n_effects;
    hd.scene_obj = scene_obj;
    hd.unknown20 = hd.effects_flag8 = hd.effects_type6 = hd.effects_96 = 0;
    hd.effects = effects;
    hd.damage = dmg;
    hd.systems = systems;
    hd.effect_bits = effect_bits;
    for (uint32_t i = 0; i < (n_sys & 0xff); ++i) {
        AnimSystemData& s = S(systems + i * 0x40);
        s.handle = h;
        s.id = ids[i];
        s.start = 0;
        s.zones = 0;
        s.max_ticks = 0;
        s.pending_state = 1;
        s.state = 1;
        s.next_state = 1;
        s.loop = 0;
        s.playing = 0;
        s.n_instances = 0;
        s.n_anims = 100;
        s.unknown1c = 0;
        s.started = 0;
        s.queue[0] = s.queue[1] = s.queue[2] = s.queue[3] = -1;
        std::memset(s.instances, 0, sizeof s.instances);  // MEM_fill(+0x1e, 0, 0x22)
        P(hd.system_ids)[i] = s.id;
    }
    uint32_t eff_index = 0, dmg_index = 0;
    uint32_t eff_dst = hd.effects, bits_dst = hd.effect_bits, bytes_dst = effect_bytes;
    const uint32_t base_desc = scene_obj != 0 ? r32(scene_obj, 0xc) : 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t src = instances + i * 0x40;
        const uint32_t dst = inst_copies + i * 0x40;
        std::memcpy(P(dst), P(src), 0x40);
        const uint32_t mirrored = r8(src, 0xc) >> 7;
        if (damage != 0) {
            if (r8(src, 0xc) & 0x10) {
                const uint16_t k = uint16_t(dmg_index);
                std::memcpy(P(dst) + 0xe, &k, 2);
                std::memcpy(P(hd.damage) + dmg_index * 0x20, P(damage) + uint32_t(r16(src, 0xe)) * 0x20, 0x20);
                ++dmg_index;
            }
        } else if (r8(src, 0xc) & 0x10) {
            w8(dst, 0xd, 0xff);
            const uint16_t z = 0;
            std::memcpy(P(dst) + 0xe, &z, 2);
            w8(dst, 0xc, uint8_t(r8(dst, 0xc) & 0xef));
        }
        const uint32_t art = r32(src, 0x1c);
        if (art == 0) continue;
        const uint32_t anim = r32(art, 8);
        if (anim != 0) {
            uint8_t count = 0;
            for (uint32_t a = anim; r8(a, 0xf) != 0; a += 0x10) ++count;
            const uint32_t id = mirrored ? r16(anim, 6) : r16(anim, 4);
            const uint32_t sys = AnimHandle_FindSystem(h, id);
            if (sys == 0) {
                ++g_stats.guards;  // o PS2 escreveria no endereço 0 (o identificador veio da mesma conta: não acontece)
            } else {
                AnimSystemData& s = S(sys);
                const uint8_t k = s.n_instances++;
                if (k < sizeof s.instances) s.instances[k] = uint8_t(i);
                else ++g_stats.guards;
                if (count < s.n_anims) s.n_anims = count;
                if (base_desc != 0) {
                    float pos[3];
                    std::memcpy(pos, P(src) + 0x30, 12);
                    const uint32_t zone = BaseDesc_CalcDamageZone(base_desc, pos);
                    s.zones = uint16_t(s.zones | (1u << (zone & 31)));
                    const uint32_t packed = r32(src, 0x3c);
                    static const float kScale[2] = {0.25f, 16.0f};  // 0x37d728
                    const float scale = ee::mul(u2f(packed & 0x3ff), kScale[((packed >> 28) & 4) / 4]);
                    if (ee::lt(1.0f, scale)) s.zones = uint16_t(s.zones | BaseDesc_ZoneNeighbours(zone));
                }
            }
        }
        const uint32_t eff = r32(art, 4);
        if (eff == 0) continue;
        for (uint32_t e = eff; r8(e, 0x14) != 0; e += 0x40) {
            if (!AnimHandle_EffectWanted(mirrored, r32(e, 0x10))) continue;
            std::memcpy(P(eff_dst), P(e), 0x40);
            w8(bytes_dst, 0, r8(e, 0x17));
            const uint16_t b = r16(e, 0x12);
            std::memcpy(P(bits_dst), &b, 2);
            const uint32_t flags = r32(e, 0x10);
            const uint64_t bit = uint64_t(1) << (eff_index & 63);
            if (flags & 8) hd.effects_flag8 |= bit;
            if (flags & 0x96) hd.effects_96 |= bit;
            if (r8(e, 0x14) == 6) hd.effects_type6 |= bit;
            if (mirrored) w8(eff_dst, 0x15, uint8_t(i));
            if (r8(eff_dst, 0x15) >= n) w8(eff_dst, 0x15, 0);
            eff_dst += 0x40;
            bits_dst += 2;
            ++eff_index;
            ++bytes_dst;
        }
    }
    // O estímulo 1 em todos (com o tique como tempo: o PS2 converte duas vezes).
    AnimHandle_ProcessStimuliAll(h, 1, ticks, 0);
    return h;
}

void AnimHandle_Destroy(uint32_t handle) {
    if (handle == 0) return;
    AnimHandle_Stop(handle);
    g_blocks.erase(handle);
}

void AnimHandle_Stop(uint32_t handle) {
    for (uint32_t i = 0; i < g_engine.count; ++i) {
        if (S(g_engine.active[i]).handle == handle) i = finish(i, 0, false);
    }
}

uint32_t AnimHandle_FindSystem(uint32_t handle, uint32_t id) {
    const AnimHandleData& h = H(handle);
    const uint8_t* ids = P(h.system_ids);
    const uint8_t* end = ids + h.n_systems;
    const uint8_t* it = std::lower_bound(ids, end, uint8_t(id));
    // (o byte achado comparado com o identificador inteiro: acima de 255 nunca acha)
    if (it == end || uint32_t(*it) != id) return 0;
    return h.systems + uint32_t(it - ids) * 0x40;
}

void AnimHandle_ProcessStimuli(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode) {
    const uint32_t t = anim_ticks(steps);
    if (const uint32_t s = AnimHandle_FindSystem(handle, system)) AnimSystem_ProcessStimuli(s, stimulus, t, mode);
}

void AnimHandle_ProcessStimuliAll(uint32_t handle, uint32_t stimulus, uint32_t steps, uint32_t mode) {
    const uint32_t t = anim_ticks(steps);
    const AnimHandleData& h = H(handle);
    for (uint32_t i = 0; i < h.n_systems; ++i) AnimSystem_ProcessStimuli(h.systems + i * 0x40, stimulus, t, mode);
}

void AnimHandle_InitAllSystemStates(uint32_t handle, uint8_t state) {
    const AnimHandleData& h = H(handle);
    for (uint32_t i = 0; i < h.n_systems; ++i) S(h.systems + i * 0x40).state = state;
}

bool AnimHandle_AnySystemPlaying(uint32_t handle) {
    if (g_engine.count == 0) return false;
    const AnimHandleData& h = H(handle);
    for (uint32_t i = 0; i < h.n_systems; ++i)
        if (S(h.systems + i * 0x40).playing != 0) return true;
    return false;
}

// [driving] 0x001a38e0 RAnimEngine::System::SetFrame
// Guarda (contada): uma animação de 0 quadros por segundo (o PS2 pararia na divisão por zero, o trap 7).
void AnimSystem_SetFrame(uint32_t s, uint32_t stimulus, uint32_t frame) {
    AnimSystemData& sd = S(s);
    const AnimHandleData& h = H(sd.handle);
    for (uint32_t k = 0; k < sd.n_instances; ++k) {
        const uint32_t inst = instance_of(h, sd.instances[k]);
        const uint32_t article = r32(inst, 0x1c);
        if (article == 0) continue;
        const uint32_t anim = find_anim(article, sd.n_anims, sd.state, stimulus);
        if (anim == 0) continue;
        const uint8_t fps = r8(anim, 0xf);
        if (fps == 0) {
            ++g_stats.guards;
            continue;
        }
        const uint32_t last = (uint32_t(r16(anim, 8)) * 60u) / uint32_t(fps);
        w8(inst, 0xd, uint8_t((anim - r32(article, 8)) >> 4));
        const uint16_t f = uint16_t(frame < last ? frame : last);
        if (uint8_t* p = P(inst)) std::memcpy(p + 0xe, &f, 2);
    }
}

// [driving] 0x001a2630 RAnimEngine::Handle::SetFrame
void AnimHandle_SetFrame(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t frame) {
    const uint32_t s = AnimHandle_FindSystem(handle, system);
    if (s != 0) AnimSystem_SetFrame(s, stimulus, frame);
}

bool AnimHandle_IsSystemPlaying(uint32_t handle, uint32_t system) {
    const uint32_t s = AnimHandle_FindSystem(handle, system);
    return s != 0 && S(s).playing != 0;
}

uint32_t AnimHandle_GetSystemStartTick(uint32_t handle, uint32_t system) {
    const uint32_t s = AnimHandle_FindSystem(handle, system);
    return s ? S(s).start : 0;
}

uint32_t AnimHandle_GetSystemMaxTicks(uint32_t handle, uint32_t system) {
    const uint32_t s = AnimHandle_FindSystem(handle, system);
    return s ? S(s).max_ticks : 0;
}

uint32_t AnimHandle_GetSystemState(uint32_t handle, uint32_t system) {
    const uint32_t s = AnimHandle_FindSystem(handle, system);
    return s ? S(s).state : 0;
}

uint32_t AnimHandle_GetInstanceSystemID(uint32_t handle, uint32_t instance) {
    const uint32_t art = r32(instance_of(H(handle), instance), 0x1c);
    if (art == 0) return 0;
    const uint32_t anim = r32(art, 8);
    return anim ? r16(anim, 4) : 0;
}

bool AnimHandle_GetSystemExists(uint32_t handle, uint32_t system) { return AnimHandle_FindSystem(handle, system) != 0; }

uint32_t AnimHandle_FindEffectByID(uint32_t handle, uint32_t id) {
    const AnimHandleData& h = H(handle);
    const uint32_t bytes = h.effect_bits + uint32_t(h.n_effects) * 2;
    for (uint32_t i = 0; i < h.n_effects; ++i)
        if (r8(bytes, i) == uint8_t(id)) return h.effects + i * 0x40;
    return 0;
}

void AnimHandle_SetEffectBits(uint32_t handle, uint32_t effect, uint16_t bits) {
    std::memcpy(P(H(handle).effect_bits) + effect * 2, &bits, 2);
}

void AnimSystem_ProcessStimuli(uint32_t system, uint32_t stimulus, uint32_t ticks, uint32_t mode) {
    ++g_stats.stimuli;
    AnimSystemData& s = S(system);
    const AnimHandleData& h = H(s.handle);
    if (s.playing == 0 && g_engine.count < 0x100) {
        uint32_t n = 0, max_frames = 0, to_state = 0;
        uint8_t loop = 0;
        for (uint32_t k = 0; k < s.n_instances; ++k) {
            const uint32_t inst = instance_of(h, s.instances[k]);
            const uint32_t art = r32(inst, 0x1c);
            const uint32_t a = find_anim(art, s.n_anims, s.state, stimulus);
            if (a == 0 || r32(a, 0) == 0) continue;
            ++n;
            const uint32_t frames = r16(a, 8);
            w8(inst, 0xd, uint8_t((a - r32(art, 8)) >> 4));
            const uint16_t z = 0;
            std::memcpy(P(inst) + 0xe, &z, 2);
            if (max_frames < frames) max_frames = frames;
            if (r16(a, 0xa) & 1) loop = 1;
            to_state = r8(a, 0xe);
        }
        if (n == 0) return;
        ++g_stats.started;
        g_engine.active[g_engine.count++] = system;
        s.playing = uint8_t(n);
        s.loop = loop;
        s.last = ticks - 1;
        s.max_ticks = uint16_t(max_frames << 2);
        s.next_state = uint8_t(to_state);
        s.start = ticks;
        s.started = 0;
        if (s.queue[0] < 0) s.pending_state = uint8_t(to_state);
        return;
    }
    if (mode == 0) return;
    const uint32_t art = r32(instance_of(h, s.instances[0]), 0x1c);
    const uint32_t a = find_anim(art, s.n_anims, s.pending_state, stimulus);
    if (a == 0) return;
    const int32_t slots = mode == 2 ? 4 : 1;
    for (int32_t i = 0; i < slots; ++i) {
        if (s.queue[i] < 0) {
            s.queue[i] = int8_t(stimulus);
            s.pending_state = r8(a, 0xe);
            return;
        }
    }
}

uint32_t AnimSystem_Update(uint32_t system, uint32_t index, uint32_t ticks) {
    ++g_stats.updates;
    AnimSystemData& s = S(system);
    handle_set_event_data(s.handle);
    uint32_t dt = ticks - s.start;
    uint32_t prev;
    if (s.started != 0) {
        prev = (s.last - s.start) >> 2;
    } else {
        s.started = 1;
        prev = 0xffffffffu;
    }
    s.last = ticks;
    if (s.loop != 0) {
        if (!(dt < uint32_t(s.max_ticks) - 4u)) {
            if (s.queue[0] >= 0) {
                index = finish(index, ticks, true);
            } else {
                const uint32_t m = s.max_ticks;
                const uint32_t period = m - 4;
                s.playing = s.n_instances;
                // O PS2 divide sem conferir (break 7 com o período 0: max_ticks 4); aqui guarda.
                if (period == 0) {
                    ++g_stats.guards;
                } else {
                    s.start = s.start - 5 + m;
                    dt = dt % period;
                }
            }
        }
    } else if (!(dt < s.max_ticks)) {
        if (--s.playing == 0) {
            index = finish(index, ticks, true);
            dt = s.max_ticks;
        }
    }
    const uint32_t frame = dt >> 2;
    const AnimHandleData& h = H(s.handle);
    for (uint32_t k = 0; k < s.n_instances; ++k) {
        const uint32_t idx = s.instances[k];
        const uint32_t inst = instance_of(h, idx);
        if (!(r8(inst, 0xd) < s.n_anims)) continue;
        uint16_t cur = uint16_t(dt);
        std::memcpy(P(inst) + 0xe, &cur, 2);
        const uint32_t anim = r32(r32(inst, 0x1c), 8) + uint32_t(r8(inst, 0xd)) * 0x10;
        const uint32_t frames = r16(anim, 8);
        if (!(uint32_t(cur) < frames * 4)) {
            cur = uint16_t(frames * 4 - 1);
            std::memcpy(P(inst) + 0xe, &cur, 2);
        }
        if (frames == 0) {
            ++g_stats.guards;  // o PS2 andaria por todas as chaves até 0xffffffff (nenhuma animação do disco tem 0)
            continue;
        }
        const uint32_t last = frames - 1;
        const uint32_t upto = int32_t(last) < int32_t(frame) ? last : frame;
        uint32_t key = prev + 1;
        if (upto < key) continue;
        for (;; ++key) {
            const uint32_t el = r32(r32(anim, 0) + key * 0x20, 0x1c);
            if (el != 0) {
                EventDynamicData& d = trigger_manager().event_data;
                d.instance = inst;
                d.unknown04 = 0;
                d.instance_index = int32_t(idx);
                const TriggerEvents* list = nf::p32_ptr<const TriggerEvents>(el);
                const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(list + 1);
                for (int32_t i = 0; i < list->count; ++i) {
                    // RegisterEvent::LookupEvent (0x1757b8): os números sem criadora não criam nada.
                    if (!event_has_creator(e[i].type)) continue;
                    ++g_stats.events;
                    event_create(e[i].type, reinterpret_cast<const uint8_t*>(&e[i]) + e[i].data_offset);
                }
            }
            if (upto < key + 1) break;
        }
    }
    return index;
}

void RAnimEngine_Update(uint32_t steps) {
    const uint32_t t = anim_ticks(steps);
    if (t == g_engine.last_tick) return;
    for (uint32_t i = 0; i < g_engine.count; ++i) i = AnimSystem_Update(g_engine.active[i], i, t);
    g_engine.last_tick = t;
}

void RAnimEngine_EvaluateInstance(const uint8_t* inst, float out[4][4]) {
    const uint8_t d = inst[0xd];
    uint32_t art = 0, anims = 0, anim = 0;
    std::memcpy(&art, inst + 0x1c, 4);
    if (art) anims = r32(art, 8);
    if (anims) anim = anims + uint32_t(d) * 0x10;
    if (d < 0x64 && art && anims && r32(anim, 0) != 0) {
        uint16_t t;
        std::memcpy(&t, inst + 0xe, 2);
        const int32_t scaled = int32_t(uint32_t(t) * r8(anim, 0xf));
        float f = ee::mul(ee::cvt_s(scaled), ps2::ffloat(0x3c888889u));  // 1/60
        const float fl = std::floor(f);                                // floorf (exata)
        const float half = ee::lt(fl, 0.0f) ? -0.5f : 0.5f;
        uint32_t k0 = uint32_t(ee::cvt_w(ee::add(half, fl)));
        const uint32_t frames = r16(anim, 8);
        if (!(k0 < frames)) k0 = frames - 1;
        uint32_t k1 = k0 + 1;
        if (!(k1 < frames)) k1 = frames - 1;
        f = ee::sub(f, u2f(k0));
        const uint32_t keys = r32(anim, 0);
        const float* q0 = nf::p32_ptr<const float>(keys + k0 * 0x20);
        const float* q1 = nf::p32_ptr<const float>(keys + k1 * 0x20);
        alignas(16) float q[4];
        anim::fast_qslerp(q0, q1, q, f);  // VU0_fastqslerp (0x2cc018)
        VU0_quattom4(q, out);
        out[3][3] = 1.0f;
        for (int c = 0; c < 3; ++c) {
            const float diff = ee::sub(q1[4 + c], q0[4 + c]);
            out[3][c] = ee::add(q0[4 + c], ee::mul(diff, f));
        }
        if (inst[0xc] & 0x80) {
            out[0][0] = ee::neg(out[0][0]);
            out[1][0] = ee::neg(out[1][0]);
            out[2][0] = ee::neg(out[2][0]);
            out[3][0] = ee::neg(out[3][0]);
        }
        return;
    }
    std::memcpy(out, inst, 64);
    out[3][3] = 1.0f;
    out[0][3] = out[1][3] = out[2][3] = 0.0f;
}

// ---------------------------------------------------------------- as zonas de dano

namespace {
// 0x3c1fe8: as 16 zonas (o código das faces que a posição toca, a máscara das vizinhas).
constexpr uint16_t kZoneCode[16] = {0x0168, 0x0440, 0x0154, 0x0054, 0x0094, 0x0294, 0x0480, 0x02a8,
                                    0x00a8, 0x0068, 0x0049, 0x0045, 0x0085, 0x0089, 0x0042, 0x0082};
constexpr uint16_t kZoneMask[16] = {0x4602, 0x4c05, 0x480a, 0xd816, 0xd868, 0x9050, 0xb0a0, 0xa140,
                                    0xe6c0, 0xe503, 0x2a03, 0x140e, 0x2870, 0x15c0, 0x820f, 0x41f0};
float fl(uint32_t base, uint32_t off) {
    uint32_t v = r32(base, off);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
}  // namespace

uint32_t BaseDesc_CalcDamageZone(uint32_t base, const float pos[3]) {
    // O ponto x (-1, 1, -1) (0x3c1f40, vmul.xyz) contra a caixa do dano (+0x40 o mínimo, +0x50 o máximo).
    const float x = ee::mul(pos[0], -1.0f), y = ee::mul(pos[1], 1.0f), z = ee::mul(pos[2], -1.0f);
    const float min_x = fl(base, 0x40), max_x = fl(base, 0x50);
    uint32_t code;
    if (ee::lt(x, min_x)) code = 0x14;
    else if (ee::lt(x, ee::mul(ee::add(min_x, max_x), 0.5f))) code = 4;
    else if (ee::lt(max_x, x)) code = 0x28;
    else code = 8;
    const float min_z = fl(base, 0x48), max_z = fl(base, 0x58);
    if (ee::lt(z, min_z)) code |= 0x140;
    else if (ee::lt(z, ee::mul(ee::add(min_z, max_z), 0.5f))) code |= 0x40;
    else if (ee::lt(max_z, z)) code |= 0x280;
    else code |= 0x80;
    const float min_y = fl(base, 0x44), max_y = fl(base, 0x54);
    if (ee::lt(y, min_y)) {
        code = (code & 0xf8c3u) | 2u;
    } else if (ee::lt(max_y, y)) {
        code = (code & 0xfccfu) | 1u;
    }
    if ((code & 3) == 0 && (code & 0xc) != 0 && (code & 0x30) == 0) code = (code & 0xfcf3u) | 0x400u;
    for (uint32_t i = 0; i < 16; ++i)
        if (kZoneCode[i] == code) return i;
    return 0;
}

uint16_t BaseDesc_ZoneNeighbours(uint32_t zone) { return zone < 16 ? kZoneMask[zone] : 0; }

std::string AnimHandle_Describe(uint32_t handle) {
    std::string out;
    char b[256];
    const AnimHandleData& h = H(handle);
    std::snprintf(b, sizeof b, "handle instancias %u dano %u sistemas %u efeitos %u\n", h.n_instances, h.n_damage,
                  h.n_systems, h.n_effects);
    out += b;
    for (uint32_t i = 0; i < h.n_systems; ++i) {
        const AnimSystemData& s = S(h.systems + i * 0x40);
        std::snprintf(b, sizeof b, "sistema %u (0x%x): instancias %u animacoes %u estado %u zonas %04x\n", s.id, s.id,
                      s.n_instances, s.n_anims, s.state, s.zones);
        out += b;
        if (s.n_instances == 0) continue;
        const uint32_t inst = instance_of(h, s.instances[0]);
        const uint32_t anims = r32(r32(inst, 0x1c), 8);
        for (uint32_t a = 0; a < s.n_anims; ++a) {
            const uint32_t ai = anims + a * 0x10;
            std::snprintf(b, sizeof b, "  anim %u: estimulo %u de %u para %u quadros %u bandeiras %x:", a, r8(ai, 0xc),
                          r8(ai, 0xd), r8(ai, 0xe), r16(ai, 8), r16(ai, 0xa));
            out += b;
            const uint32_t keys = r32(ai, 0);
            for (uint32_t k = 0; keys && k < r16(ai, 8); ++k) {
                const uint32_t el = r32(keys + k * 0x20, 0x1c);
                if (!el) continue;
                const TriggerEvents* list = nf::p32_ptr<const TriggerEvents>(el);
                const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(list + 1);
                for (int32_t j = 0; j < list->count; ++j) {
                    std::snprintf(b, sizeof b, " q%u:e%d", k, e[j].type);
                    out += b;
                }
            }
            out += "\n";
        }
    }
    return out;
}

bool carp_instance_list(UData* root, uint32_t& instances, uint32_t& count, uint32_t& damage) {
    instances = count = damage = 0;
    if (!root) return false;
    UGroup* arti = UGroup_GroupLocateTag(root, 0x41727469);  // 'Arti'
    if (arti == UGroup_GroupEnd(root)) return false;
    UData* in = UGroup_DataLocateTag(arti, 0x696e0000);  // in#0000
    if (in == UGroup_DataEnd(arti)) return false;
    instances = nf::p32_u(UData_Data(in));
    count = in->count;
    UData* di = UGroup_DataLocateTag(arti, 0x64690000);  // di#0000
    if (di != UGroup_DataEnd(arti)) damage = nf::p32_u(UData_Data(di));
    return true;
}

}  // namespace drv
