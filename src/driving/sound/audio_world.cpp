// Os sons parados do mundo de carro (driving/sound/audio_internal.h, seção 31 do docs/driving.md): AWorldSound (um timbre
// que se repete com intervalos e vezes sorteados, com a voz de aviso de quando algo vai passar perto do carro do
// jogador), WSound (o do mapa) e WSoundGroup (os sons do mapa por número, que o WWorld::Open enche com os registros
// 'Audi' da pista e com os caminhos animados cujo objeto tem um som). Portado do MIPS do DRIVING.ELF (SupportPS2.lib
// UDevCons.obj, WRender.obj no SYM) e conferido na parte 27 do nf_driving_check.
//
// Em Paris o grupo fica vazio: o 'Audi' da pista não tem registros e os sons dos objetos dos caminhos (o DEFAULTSOUND
// da "PhysicsData") são -1, resolvidos na carga da pista antes de o ASoundManager::Init carregar os bancos (ver o
// game/attributes.h).
//
// Divergências (port):
//  - o map<int, WSound *> do PS2 (a árvore da STL com nós do FastAlloc) é um std::map;
//  - o ~AWorldSound do PS2 não apaga a voz de aviso (+0x138): o port também não (ela fica na lista ativa das vistas
//    com o último volume, como no PS2).

#include <cstdio>
#include <cstring>

#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

// AVoice::Rewind (inline no Play): as vistas esquecem o som do SND (sem pará-lo) e voltam a não ter acabado.
void rewind(AVoice& v) {
    for (int i = 2; i >= 0; --i) {
        v.views[i].over = 0;
        v.views[i].handle = -1;
    }
}

// |a - b| pela VU0 (vsub.xyz; vmul.xyz; vmulax.x, vmadday.x, vmaddz.x com 1,0; vsqrt; vaddq.x).
float vu_distance(const float a[3], const float b[3]) {
    const float d[3] = {ee::sub(a[0], b[0]), ee::sub(a[1], b[1]), ee::sub(a[2], b[2])};
    uint32_t acc = ps2::mul(ps2::fbits(d[0]), ps2::fbits(d[0]));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(d[1], d[1])));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(d[2], d[2])));
    return ps2::ffloat(ps2::add(0, ps2::sqrt(acc)));
}

WSoundGroup g_group;
std::vector<AVoice*> g_leaked;

}  // namespace

WSoundGroup& world_sound_group() { return g_group; }
std::vector<AVoice*>& world_leaked_voices() { return g_leaked; }

// [driving] 0x002dd380 AWorldSound::AWorldSound(int banco, int timbre, char *mixagem)
AWorldSound::AWorldSound(int32_t bank, int32_t patch, const char* mix_name)
    : ABaseSound(mix_name, 4), voice(mix, bank, patch) {
    pitch = 1.0f;
    volume = 1.0f;
}

// [driving] 0x002dd178 AWorldSound::~AWorldSound (a voz +0xb0; a de aviso fica, como no PS2: o port a guarda para o
// audio_shutdown)
AWorldSound::~AWorldSound() {
    if (warning != nullptr) g_leaked.push_back(warning);
}

// [driving] 0x002dd500 AWorldSound::SetWarningVoice(int)
void AWorldSound::SetWarningVoice(int32_t id) {
    delete warning;
    warning = new AVoice(mix, id >> 7, id & 0x7f);
}

// [driving] 0x002dd5b8 AWorldSound::SetTiming(float, float): com um intervalo, as vistas tocam uma vez só; um intervalo
// menor que o anterior começa de novo.
void AWorldSound::SetTiming(float min_d, float max_d) {
    const float sum = ee::add(min_d, max_d);
    if (!ee::eq(sum, 0.0f))
        for (int i = 2; i >= 0; --i) voice.views[i].loop = 0;
    if (ee::lt(sum, ee::add(min_delay, max_delay))) next_time = 0.0f;
    max_delay = max_d;
    min_delay = min_d;
}

// [driving] 0x002dcd50 AWorldSound::Play(APath &)
void AWorldSound::Play(APath& path) {
    const int32_t view = path.listener->view;
    if (waiting != 0) {
        const float now = sound_time();
        if (!(0.5 < double(repeats))) {
            if (ee::lt(next_time, now)) {
                if (ee::eq(ee::add(min_delay, max_delay), 0.0f)) {
                    waiting = 0;
                } else {
                    if (ee::eq(next_time, 0.0f)) {
                        const float r = rrandom();
                        next_time = ee::add(now, ee::mul(max_delay, r));
                    } else {
                        const float r = rrandom();
                        next_time = ee::add(ee::add(now, min_delay), ee::mul(ee::sub(max_delay, min_delay), r));
                    }
                    const float r = rrandom();
                    repeats = ee::add(min_repeat, ee::mul(ee::sub(max_repeat, min_repeat), r));
                    waiting = 0;
                }
                rewind(voice);
            }
        } else {
            // fptodp + dpsub + dptofp: o -1 em double, arredondado ao mais perto
            repeats = float(double(repeats) - 1.0);
            waiting = 0;
            rewind(voice);
        }
    }
    if (AVoice_Done(voice)) waiting = 1;
    float vol = 0.0f;
    if (waiting == 0) {
        const float mv = AMix_GetVolume(mix);
        vol = ee::mul(ee::mul(volume, mv), path.volume);
    }
    const float p = pan_mode == 1 ? pitch : ee::mul(pitch, path.pitch);
    const float az = pan_mode == 1 ? 0.0f : path.azimuth;
    AVoice_Play(vol, p, az, 0.0f, 0.0f, &voice, view);
    if (warning == nullptr) return;
    // Onde o som vai estar nos próximos cinco passos da velocidade (a cópia leva o w da posição, que o vadd.xyz mantém).
    float ahead[3] = {pos[0], pos[1], pos[2]};
    for (int i = 4; i >= 0; --i) {
        for (int c = 0; c < 3; ++c) ahead[c] = ee::add(vel[c], ahead[c]);
        if (ee::lt(vu_distance(ahead, path.listener->car), 6.0f)) pass_frames = 0xb4;
    }
    float wv = 0.0f;
    if (0 < pass_frames) {
        const float mv = AMix_GetVolume(mix);
        const float v = ee::mul(ee::mul(volume, mv), path.volume);
        wv = ee::add(v, v);
    }
    if (ee::lt(1.0f, wv)) wv = 1.0f;
    const float wp = ee::lt(0.0f, path.pitch) ? ee::add(ee::mul(ee::sub(path.pitch, 1.0f), 0.25f), 1.0f) : 0.0f;
    AVoice_Play(wv, wp, path.azimuth, 0.0f, 0.0f, warning, view);
    if (0 < pass_frames) {
        pass_frames -= sound_count();
        if (pass_frames < 0) pass_frames = 0;
    }
}

// ---- WSoundGroup ---------------------------------------------------------------------------------------------------

// [driving] 0x002278f8 WSoundGroup::Start: nenhum som visto ainda.
void WSoundGroup::Start() {
    for (auto& s : sounds) s.second->added = 0;
}

// [driving] 0x002279d0 WSoundGroup::Add(int, int, int): o som do número (um WSound novo na "World Sounds", ou trocado se
// o timbre mudou), marcado como visto.
WSound* WSoundGroup::Add(int32_t id, int32_t bank, int32_t patch) {
    auto it = sounds.find(id);
    WSound* w;
    if (it == sounds.end()) {
        // ABaseSound::operator new(0x150, "WSound"), WSound::WSound (0x002276d0 inline)
        w = new WSound(bank, patch, "World Sounds");
        sounds.emplace(id, w);
    } else {
        w = it->second;
        if (patch != w->voice.views[0].patch) {
            if (w != nullptr) w->destroy();
            w = new WSound(bank, patch, "World Sounds");
            it->second = w;
        }
    }
    w->added = 1;
    return w;
}

// [driving] 0x00227d80 WSoundGroup::Add(int, char *): "banco: timbre" (sscanf "%[^:]: %s").
WSound* WSoundGroup::Add(int32_t id, const char* text) {
    // O sscanf("%[^:]: %s") à mão: o banco até o ':' (pelo menos um caractere), o ':', os brancos e o timbre até o
    // próximo branco (os buffers de 0x20 e 0x40 bytes zerados antes).
    char bank[32] = {};
    char patch[64] = {};
    const char* c = text;
    auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f'; };
    size_t n = 0;
    while (*c != '\0' && *c != ':') {
        if (n + 1 < sizeof bank) bank[n++] = *c;
        ++c;
    }
    if (n != 0 && *c == ':') {
        ++c;
        while (space(*c)) ++c;
        size_t k = 0;
        while (*c != '\0' && !space(*c)) {
            if (k + 1 < sizeof patch) patch[k++] = *c;
            ++c;
        }
    }
    ABank* b = ABank_Get(bank);
    return Add(id, b->snd_bank, b->index.Lookup(patch));
}

// [driving] 0x00227e58 WSoundGroup::End: os que este Open não viu saem.
void WSoundGroup::End() {
    for (auto it = sounds.begin(); it != sounds.end();) {
        if (it->second->added == 0) {
            it->second->destroy();
            it = sounds.erase(it);
        } else {
            ++it;
        }
    }
}

// [driving] 0x00228470 WSoundGroup::Update (o WSound::Update 0x00227538 de cada um, vazio)
void WSoundGroup::Update() {}

// A parte do som do WWorld::Open (0x0022faf8): os registros 'Audi' (0x60 bytes) entre o Start e o End, e depois os
// caminhos animados com som.
void world_sounds_open(const uint8_t* audi, int32_t count, const std::vector<WorldPathSound>& paths) {
    if (!audio_ready()) return;
    WSoundGroup& g = g_group;
    int32_t id = 0;
    if (audi != nullptr) {
        g.Start();
        for (int32_t i = 0; i < count; ++i) {
            const uint8_t* r = audi + 0x60 * i;
            auto f = [r](uint32_t o) {
                float v;
                std::memcpy(&v, r + o, 4);
                return v;
            };
            char name[0x30] = {};
            std::memcpy(name, r + 0x10, 0x2f);
            WSound* w = g.Add(id++, name);
            w->volume = f(0x40);
            w->pitch = f(0x44);
            std::memcpy(w->pos, r, 12);
            w->min_distance = f(0x58);
            const float mx = f(0x5c);
            w->max_distance = mx;
            w->max_distance2 = ee::mul(mx, mx);
            w->SetTiming(f(0x48), f(0x4c));  // (inline no Open)
            w->min_repeat = f(0x50);
            w->max_repeat = f(0x54);
            w->pan_mode = r[0x3f] == 0 ? 1 : 0;
            ++scene_audio_stats().world_sounds;
        }
        g.End();
    }
    for (const WorldPathSound& p : paths) {
        if (p.sound < 0) continue;
        WSound* w = g.Add(id++, p.sound);
        std::memcpy(w->pos, p.pos, 12);
        w->path_handle = p.handle;
        w->max_distance2 = 10000.0f;
        w->min_distance = 10.0f;
        w->max_distance = 100.0f;
        if (-1 < p.warning) w->SetWarningVoice(p.warning);
        ++scene_audio_stats().world_sounds;
    }
}

// O ~WSoundGroup do WWorld::Close (0x00230648).
void world_sounds_close() { g_group.Clear(); }
int32_t world_sounds_count() { return int32_t(g_group.sounds.size()); }

// [driving] 0x00228560 WSoundGroup::Clear
void WSoundGroup::Clear() {
    for (auto it = sounds.begin(); it != sounds.end();) {
        WSound* w = it->second;
        it = sounds.erase(it);
        if (w != nullptr) w->destroy();
    }
}

}  // namespace drv
