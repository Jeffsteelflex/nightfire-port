// Os fluxos do jogo de carro (driving/sound/audio_stream.h): AStream, AStreamPriv, AFader, a parte dos fluxos do
// ASoundManager e os eventos de som, portados do MIPS do DRIVING.ELF. Divergências no cabeçalho.

#include "driving/sound/audio_stream.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_mix.h"
#include "driving/sound/snd_stream.h"
#include "driving/sound/stream.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

constexpr int32_t kTimerRate = 100;  // 0x003c80d0: os ticks por segundo do TIMER (o contador 1 a 100 Hz)

int32_t round_ee(float v) { return ee::cvt_w(ee::add(ee::lt(v, 0.0f) ? -0.5f : 0.5f, v)); }

// NF_DRV_STREAM_LOG=<arquivo> (depuração do port): os eventos, os pedidos ao SNDSTRM e os volumes a cada segundo.
std::FILE* stream_log() {
    static std::FILE* f = []() -> std::FILE* {
        std::string path;
        return nf::get_env_narrow("NF_DRV_STREAM_LOG", &path) && !path.empty() ? nf::fopen_narrow(path.c_str(), "w")
                                                                                : nullptr;
    }();
    return f;
}
template <typename... T>
void slog(const char* fmt, T... args) {
    if (std::FILE* f = stream_log()) {
        std::fprintf(f, "%7.2f ", double(TIMER_gettick()) / 100.0);
        std::fprintf(f, fmt, args...);
        std::fputc('\n', f);
        std::fflush(f);
    }
}

// Um evento na fila do AStream (AStreamEntry, 0x4c bytes no PS2).
struct AStreamEntry {
    char name[0x38] = {};       // +0x00 em minúsculas, '/' trocada por '\'
    float fade = 0;             // +0x38 os segundos da transição antes deste (se algo toca)
    int32_t loop = 0;           // +0x3c
    int32_t effect = 0;         // +0x40
    int32_t held = 0;           // +0x44 pedido segurado (o prebuffer), até o EReleaseStream
    int32_t tick = 0;           // +0x48 o AStreamPriv +0x64 quando o evento entrou
};

// AStreamPriv (0x14c no PS2).
struct AStreamPriv {
    std::deque<AStreamEntry> queue;  // +0x00 (a deque do GCC 2.95)
    SndStreamStatus status;     // +0x28
    SndRequestStatus rstatus;   // +0x34
    std::unique_ptr<char[]> file;  // +0x44 o arquivo do pedido em curso (new de 0x40 bytes)
    float filter = 1.0f;        // +0x48 o SetFilter: o passa-baixa |filter| x 24000 Hz abaixo de 1
    float fade = 0;             // +0x4c os segundos de transição já passados
    float volume = 0;           // +0x50 o do último Play
    int32_t mem_size = 0;       // +0x54
    int32_t stream = -1;        // +0x58 o SNDSTRM
    int32_t req = -1;           // +0x5c o pedido em curso
    int32_t event_tick = 0;     // +0x60
    int32_t play_tick = 0;      // +0x64
    int32_t big_added = 0;      // +0x68
    int32_t big_id = 0;         // +0x6c
    int32_t over = 1;           // +0x70 nada toca e a fila está vazia
    int32_t not_found = 0;      // +0x74
    int32_t paused = 0;         // +0x78
    int32_t loop = 0;           // +0x7c
    int32_t effect = 0;         // +0x80
    int32_t held = 0;           // +0x84
    std::vector<uint8_t> mem_buf;  // +0x88 (UMemory::Alloc alinhada a 0x400)
    uint8_t* mem = nullptr;
    char prefix[0x80] = "|";    // +0x8c DAT_003c33a8: os nomes estão dentro do BIG
    char name[0x40] = {};       // +0x10c o arquivo em curso com a primeira letra maiúscula (GetFile)
};

// Os globais da camada (o .bss e o .data de BondAudioPS2).
struct StreamGlobals {
    char path[0x80] = "./";     // AStream::fgPath (0x00355d30)
    int32_t trigger = 0;        // AStream::fTrigger (o prebuffer: 1 esperando, 2 liberado)
    int32_t hold_begin = 0;     // AStream::fHoldBegin
    int32_t mission_over = 0;   // ASoundManager::fMissionOver (0x00355e88)
    int16_t hud_fade = 0;       // ASoundManager::fHUDFade (0x00355e6c)
    int32_t is_paused = 0;      // ASoundManager::fgIsPaused (0x003c82c8)
    std::vector<std::string> failed;  // ASoundManager::fgFailedStreams
    uint64_t events = 0, queued = 0, failed_queue = 0, guards = 0;
};
StreamGlobals& G() {
    static StreamGlobals g;
    return g;
}

class AStream;
std::map<std::string, AStream*, NoCaseLess>& streams() {  // URefCounter<AStream>
    static std::map<std::string, AStream*, NoCaseLess> m;
    return m;
}

// [driving] 0x002e86f8 AStreamPriv::AStreamPriv(char *, char *, unsigned int)
void AStreamPriv_Construct(AStreamPriv& p, const char* file, const char* ext, int32_t buffer) {
    std::string big = G().path;
    big += file;
    if (*ext != 0) {
        big += ".";
        big += ext;
    }
    if (FILESYS_existssync(big.c_str()) != 0) p.big_added = FILESYS_addbigsync(big.c_str(), &p.big_id);
    if (p.big_added == 0) std::fprintf(stderr, "WARNING: Bigfile [%s] not found!\n", big.c_str());
    nf::str_copy_trunc(p.prefix, sizeof p.prefix, "|");
    p.mem_size = buffer + SNDSTRM_overhead(4, 0xf);
    // UMemory::Alloc(tamanho, 0x400, "<arquivo> buffer")
    p.mem_buf.assign(size_t(p.mem_size) + 0x400, 0);
    uintptr_t a = reinterpret_cast<uintptr_t>(p.mem_buf.data());
    a = (a + 0x3ff) & ~uintptr_t(0x3ff);
    p.mem = reinterpret_cast<uint8_t*>(a);
    // Peculiaridade mantida: o SNDplaysetdef escreve sobre o texto "<arquivo> buffer" da pilha (o nome do UMemory::Alloc)
    // e não toca nos bytes +6/+7 (o enchimento das opções), que levam as letras 7 e 8 do texto ao SNDSTRM_create (que
    // os copia para o canal; nada os lê).
    SndPlayOpts opts{};
    {
        char text[64] = {};
        nf::str_copy_trunc(text, sizeof text, file);
        const size_t n = std::strlen(text);
        nf::str_copy_trunc(text + n, sizeof text - n, " buffer");
        std::memcpy(&opts, text, sizeof opts);
    }
    SNDplaysetdef(&opts);
    if (ASystem_heap() == nullptr) {
        p.stream = -1;
    } else {
        p.stream = SNDSTRM_create(&opts, 4, 0xf, p.mem, p.mem_size);
    }
    p.req = -1;
}

// [driving] 0x002e8918 AStreamPriv::~AStreamPriv
void AStreamPriv_Destruct(AStreamPriv& p) {
    if (-1 < p.stream) SNDSTRM_destroy(p.stream);
    p.mem_buf.clear();
    p.mem = nullptr;
    if (p.big_added != 0) FILESYS_delbigsync(p.big_id);
    p.queue.clear();
}

class AStream : public ABaseSound {
public:
    AStreamPriv* priv = nullptr;  // +0xb0
    int32_t hold_timeout = 0;     // +0xb4 dez segundos de ticks do TIMER
    int32_t held_ticks = 0;       // +0xb8
    int32_t release_tick = 0;     // +0xbc
    std::string mix_name;

    // [driving] 0x002e8a90 AStream::AStream(char *, char *, char *, unsigned int)
    AStream(const char* name, const char* file, const char* ext, int32_t buffer) : ABaseSound(name, 4) {
        mix_name = name;
        hold_timeout = kTimerRate * 10;
        held_ticks = 0;
        release_tick = 0;
        priv = new AStreamPriv();
        AStreamPriv_Construct(*priv, file, ext, buffer);
    }
    // [driving] 0x002e8c00 AStream::~AStream
    ~AStream() override {
        Stop();
        if (priv != nullptr) {
            AStreamPriv_Destruct(*priv);
            delete priv;
            priv = nullptr;
        }
        for (auto it = streams().begin(); it != streams().end(); ++it)
            if (it->second == this) {
                streams().erase(it);
                break;
            }
    }

    // [driving] 0x002e9350 AStream::DecodeError(int): avisa e para.
    void DecodeError(int32_t error) {
        std::fprintf(stderr, "AStream: erro %d no fluxo [%s]\n", int(error), priv->file ? priv->file.get() : "");
        Stop();
    }
    // [driving] 0x002e99d0 AStream::Next
    void Next() {
        AStreamPriv& p = *priv;
        if (-1 < p.stream) {
            if (G().trigger == 1) {
                SNDSTRM_modifyhold(p.req, 0);
                G().trigger = 0;
            }
            SNDSTRM_purge(p.stream);
        }
        p.req = -1;
        p.file.reset();
        if (p.queue.empty()) p.over = 1;
    }
    // [driving] 0x002e9ae0 AStream::Last
    void Last() {
        Next();
        while (1 < priv->queue.size()) priv->queue.pop_front();
    }
    // [driving] 0x002e9bc8 AStream::Stop
    void Stop() {
        priv->queue.clear();
        Next();
    }
    // [driving] 0x002e9bf8 AStream::FadeOut: o volume a zero em 3 s (SNDSTRM_autovol).
    void FadeOut() {
        SNDSYS_entercritical();
        SNDSTRM_autovol(priv->stream, 3000, 0);
        SNDSYS_leavecritical();
    }
    // [driving] 0x002e9c48 AStream::GetLatency
    float GetLatency() const {
        const AStreamPriv& p = *priv;
        if (p.loop != 0 || p.req == -1) return 0.0f;
        float t = ee::mul(ee::cvt_s(p.play_tick - p.event_tick), ee::div(1.0f, ee::cvt_s(kTimerRate)));
        if (p.rstatus.state == 2) t = ee::sub(t, ee::mul(ee::cvt_s(p.rstatus.played_ms), 0.001f));
        return t;
    }

    // [driving] 0x002e9728 AStream::Event(char *, float, bool, bool, bool)
    void Event(float fd, const char* file, bool loop, bool effect, bool held) {
        AStreamEntry e;
        e.tick = priv->play_tick;
        e.fade = fd;
        e.loop = loop ? 1 : 0;
        e.effect = effect ? 1 : 0;
        e.held = held ? 1 : 0;
        nf::str_copy_trunc(e.name, sizeof e.name, file);
        for (size_t i = 0; i < std::strlen(e.name); ++i) {
            char& c = e.name[i];
            if (c == '/') c = '\\';
            else if (uint8_t(c - 0x41) < 0x1a) c = char(c + 0x20);
        }
        priv->queue.push_back(e);
        priv->over = 0;
        priv->not_found = 0;
        ++G().events;
        slog("evento %s [%s] transicao %.2f laco %d efeito %d segurado %d fila %d", mix_name.c_str(), e.name,
             double(fd), e.loop, e.effect, e.held, int(priv->queue.size()));
    }

    // [driving] 0x002e8c70 AStream::Play(APath &)
    void Play(APath& path) override;
};

void AStream::Play(APath& path) {
    AStreamPriv& p = *priv;
    if (p.stream == -1) return;
    p.play_tick = TIMER_gettick();
    if (G().mission_over != 0 && nf::str_icmp(mix->name, "music") == 0) return;
    const float v = ee::mul(ee::mul(volume, AMix_GetVolume(mix)), path.volume);
    p.volume = v;
    if (p.req >= 0 && !p.queue.empty()) {
        const AStreamEntry& front = p.queue.front();
        if (ee::le(0.0f, front.fade)) {
            const float f = ee::add(p.fade, ee::mul(ee::cvt_s(sound_count()), ps2::ffloat(0x3c888889u)));
            p.fade = f;
            if (ee::le(front.fade, f)) {
                if (p.stream >= 0) SNDSTRM_purge(p.stream);
                p.req = -1;
                p.file.reset();
            } else {
                p.volume = ee::mul(v, ee::sub(1.0f, ee::div(f, front.fade)));
            }
        }
    }
    if (p.file && ee::lt(0.0f, p.volume)) {
        mix->count += 1;
        mix->sum = ee::add(mix->sum, ee::add(p.volume, p.volume));
    }
    const float v2 = ee::mul(p.volume, mix->gain);
    p.volume = v2;
    int32_t vol;
    if (ee::lt(v2, 0.0f)) vol = 0;
    else if (ee::lt(1.0f, v2)) vol = 0x7f;
    else vol = round_ee(ee::mul(v2, 127.0f));
    int32_t ipitch = 0;
    if (vol != 0) {
        const float pp = path.pitch;
        if (ee::lt(pp, 0.0f)) ipitch = 0;
        else if (ee::lt(4.0f, pp)) ipitch = 0x4000;
        else ipitch = round_ee(ee::mul(pp, 4096.0f));
    }
    p.paused = ipitch == 0 ? 1 : 0;
    const float az = ee::mul(path.azimuth, 65536.0f);
    const int32_t azr = ee::cvt_w(ee::add(ee::lt(az, 0.0f) ? -0.5f : 0.5f, az));
    if (ipitch == 0) vol = 0;
    const float f = ee::abs(p.filter);  // (o fabs em double do newlib: exato)
    int32_t lp = 0xffff;
    if (ee::lt(f, 1.0f)) lp = round_ee(ee::mul(f, 24000.0f));
    SNDSYS_entercritical();
    SNDSTRM_vol(p.stream, vol);
    SNDSTRM_pitchmult(p.stream, ipitch);
    SNDSTRM_lowpass(p.stream, lp);
    if (ee::lt(0.0f, max_distance)) SNDSTRM_3dpos(p.stream, azr & 0xffff, 0);
    SNDSTRM_status(p.stream, &p.status);
    if (-1 < p.req) SNDSTRM_requeststatus(p.req, &p.rstatus);
    SNDSYS_leavecritical();
    if (p.req < 0) {
        if (p.file) return;
        if (p.queue.empty()) {
            p.over = 1;
            return;
        }
        const AStreamEntry e = p.queue.front();
        nf::str_copy_trunc(p.name, sizeof p.name, e.name);
        if (uint8_t(p.name[0] - 0x61) < 0x1a) p.name[0] = char(p.name[0] - 0x20);
        p.file.reset(new char[0x40]);
        std::snprintf(p.file.get(), 0x40, "%s%s.asf", p.prefix, e.name);
        p.event_tick = e.tick;
        p.loop = e.loop;
        p.effect = e.effect;
        p.held = e.held;
        p.fade = 0;
        if (e.held == 1) {
            p.req = SNDSTRM_queuefile(p.stream, -1, p.file.get(), 0);
            G().trigger = 1;
            G().hold_begin = TIMER_gettick();
            release_tick = 0;
        } else {
            p.req = SNDSTRM_queuefile(p.stream, 0xfa, p.file.get(), 0);
            release_tick = 0;
        }
        ++G().queued;
        slog("pedido %s %s -> %d (segurado %d)", mix_name.c_str(), p.file.get(), p.req, e.held);
        if (p.req < 0) {
            ++G().failed_queue;
            DecodeError(p.req);
        }
        // Guarda do porte: o DecodeError chama o Stop, que esvazia a fila (deque::clear), e o original tira a frente
        // assim mesmo (o pop_front da deque vazia do GCC 2.95 põe o começo depois do fim: a fila passa a "ter" as
        // entradas mortas além do fim, lidas nos quadros seguintes). O porte deixa a fila vazia e conta.
        if (!p.queue.empty()) p.queue.pop_front();
        else ++G().guards;
        if (p.loop != 0) return;
        if (p.queue.empty()) return;
    } else {
        if (p.status.requests != 0) {
            if (p.held == 1) {
                if (G().trigger == 1) {
                    const int32_t t = TIMER_gettick() - G().hold_begin;
                    held_ticks = t;
                    if (t <= hold_timeout) return;
                    slog("segurado demais %s %s: Next", mix_name.c_str(), p.file ? p.file.get() : "");
                    Next();
                    release_tick = TIMER_gettick();
                    return;
                }
                if (G().trigger != 2) return;
                SNDSTRM_modifyhold(p.req, 0);
                slog("liberado %s %s", mix_name.c_str(), p.file ? p.file.get() : "");
                p.event_tick = p.play_tick;
                release_tick = TIMER_gettick();
                G().trigger = 0;
                return;
            }
            if (p.loop == 0) return;
            if (1 < p.status.requests) return;
            p.req = SNDSTRM_queuefile(p.stream, 0xfa, p.file.get(), 0);
            ++G().queued;
            slog("laco %s %s -> %d", mix_name.c_str(), p.file.get(), p.req);
            return;
        }
        if (!p.file) return;
    }
    p.file.reset();
    p.req = -1;
}

// [driving] 0x002e9688 AStream::Get(char *)
AStream* AStream_Get(const char* name) {
    auto it = streams().find(name != nullptr ? name : "");
    return it == streams().end() ? nullptr : it->second;
}

// [driving] 0x002e9578 AStream::Create(char *, char *, unsigned int)
AStream* AStream_Create(const char* name, const char* file, int32_t buffer) {
    const char* ext = "";
    if (nf::str_icmp(name, "music") == 0) ext = "mus";  // ASoundManager::fgMusicExt
    else if (nf::str_icmp(name, "speech") == 0 || nf::str_icmp(name, "nis") == 0) ext = "spe";  // fgSpeechExt
    else ext = "viv";
    AStream* s = new AStream(name, file, ext, buffer);
    s->max_distance = 0.0f;
    s->max_distance2 = 0.0f;
    streams()[name] = s;
    return s;
}

// ---- AFader --------------------------------------------------------------------------------------------------------

// AFader::Priv (0x44).
struct AFaderPriv {
    float level = 0;            // +0x00 0 só a secundária .. 1 só a primária
    char second[0x10] = {};     // +0x04 o nome da secundária (o ambiente)
    AStream* stream = nullptr;  // +0x14
    AMix* fader = nullptr;      // +0x18 a mixagem que a primária abaixa ("0:Fader2")
    AMix* fade_effects = nullptr;  // +0x1c "0:Fade Effects"
    AMix* fade_music = nullptr;    // +0x20 "0:Fade Music"
    float filter = 1.0f;        // +0x24
    int32_t primary = 1;        // +0x28 a primária toca ou vai tocar
    int32_t restart = 0;        // +0x2c
    int32_t fading = 0;         // +0x30 a saída em curso
    int32_t call911 = 0;        // +0x34
    int32_t allow_second = 1;   // +0x38
    int32_t second_on = 0;      // +0x3c
    int32_t w40 = 0;            // +0x40
};
std::map<std::string, std::unique_ptr<AFaderPriv>, NoCaseLess>& faders() {  // URefCounter<AFader>
    static std::map<std::string, std::unique_ptr<AFaderPriv>, NoCaseLess> m;
    return m;
}

// [driving] 0x002f9710 AFader::Get(char *)
AFaderPriv* AFader_Get(const char* name) {
    auto it = faders().find(name);
    return it == faders().end() ? nullptr : it->second.get();
}

void set_stream_mix(AStream* s, const char* name) {
    AMix_Remove(s->mix);
    s->mix = AMix_Add(name);
    s->mix_name = name;
}

// [driving] 0x002e9718 AStream::SetFilter(float)
void AStream_SetFilter(float f, AStream* s) { s->priv->filter = f; }

// [driving] 0x002f8e00 AFader::Priv::Event(char *, float, bool, bool)
int32_t AFaderPriv_Event(float fade, AFaderPriv& p, const char* name, bool effect, bool held) {
    if (ee::eq(AMix_GetVolume(AMix_Get("Speech")), 0.0f)) return 0;
    if (ee::eq(AMix_GetVolume(AMix_Get("NIS")), 0.0f)) return 0;
    char file[0x80];
    std::snprintf(file, sizeof file, "|%s.asf", name);
    if (!filesys_find(file, nullptr)) {  // FILESYS_bypassqueuefileinfo
        std::fprintf(stderr, "Unable to find [%s]\n", file);
        return 0;
    }
    AStreamPriv& sp = *p.stream->priv;
    if (effect && sp.over == 0 && sp.loop == 0 && sp.effect == 0) return 0;
    if (sp.over != 0 || ee::le(0.0f, fade)) {
        if (!effect) {
            set_stream_mix(p.stream, "Speech");
            p.stream->volume = 1.0f;
            AStream_SetFilter(1.0f, p.stream);
            const float fx = AMix_GetVolume(p.fade_effects);
            p.fader->saved = p.fader->volume;
            p.fader->volume = fx;
            AStream* music = AStream_Get("Music");
            if (music != nullptr) music->volume = AMix_GetVolume(p.fade_music);
            else ++G().guards;
            p.level = 0.0f;
        } else {
            set_stream_mix(p.stream, "NIS");
            p.stream->volume = 1.0f;
            AStream_SetFilter(1.0f, p.stream);
            p.level = 0.0f;
        }
    } else {
        p.fading = 1;
    }
    p.stream->Event(fade, name, false, effect, held);
    p.restart = 0;
    p.primary = 1;
    return 1;
}

// [driving] 0x002f9090 (o AFader::SetSecondary(char *) 0x002f97b8 o chama): a secundária troca de nome.
void AFaderPriv_SetSecondary(AFaderPriv& p, const char* name) {
    if (p.allow_second == 0) return;
    if (std::strcmp(p.second, name) == 0) return;
    p.second_on = std::strcmp(name, "Off") != 0 ? 1 : 0;
    nf::str_copy_trunc(p.second, sizeof p.second, name);
    if (p.primary == 0) {
        p.fading = 1;
        p.restart = 1;
    }
    p.primary = 1;
}

// [driving] 0x002f9180 AFader::Priv::Update
void AFaderPriv_Update(AFaderPriv& p) {
    const float dt = ee::mul(ee::cvt_s(sound_count()), ps2::ffloat(0x3c888889u));
    AStreamPriv& sp = *p.stream->priv;
    if (sp.over != 0) {
        if (p.primary == 0) {
            if (p.second_on != 0) {
                set_stream_mix(p.stream, "Ambience");
                p.stream->Event(0.0f, p.second, true, false, false);
                p.level = ps2::ffloat(0x3a83126fu);  // 0,001
            }
        } else {
            p.stream->volume = 0.0f;
            if (p.allow_second != 0) p.primary = 0;
        }
    }
    if (p.primary == 0) {
        float l = ee::add(p.level, ee::add(dt, dt));
        p.level = l;
        if (ee::lt(1.0f, l)) {
            p.level = 1.0f;
            p.restart = 0;
        }
        p.stream->volume = p.level;
        AStream_SetFilter(p.filter, p.stream);
    } else if (p.fading != 0) {
        const float l = ee::sub(p.level, ee::add(dt, dt));
        p.level = l;
        if (ee::le(l, 0.0f)) {
            p.level = 0.0f;
            if (sp.loop != 0 || p.restart != 0) p.stream->Next();
            set_stream_mix(p.stream, sp.effect != 0 ? "NIS" : "Speech");
            p.stream->volume = 1.0f;
            AStream_SetFilter(1.0f, p.stream);
            p.fading = 0;
        } else {
            p.stream->volume = l;
            AStream_SetFilter(p.filter, p.stream);
        }
    } else if (G().hud_fade < 0) {
        p.stream->Stop();
        p.stream->Next();
    } else if (sp.paused == 0) {
        p.stream->volume = 1.0f;
        AStream_SetFilter(1.0f, p.stream);
    } else if (G().is_paused == 0) {
        p.stream->Stop();
    }
    AMix* f0 = AMix_Get("0:Fader0");
    AStream* music = AStream_Get("Music");
    if (G().mission_over == 0) {
        if (p.restart == 0) {
            const float fe = AMix_GetVolume(p.fade_effects);
            const float fm = AMix_GetVolume(p.fade_music);
            f0 = AMix_Get("0:Fader0");
            f0->saved = f0->volume;
            f0->volume = 1.0f;
            if (sp.effect == 0) {
                p.fader->saved = p.fader->volume;
                p.fader->volume = ee::add(ee::mul(p.level, ee::sub(1.0f, fe)), fe);
                music = AStream_Get("Music");
                if (music != nullptr) music->volume = ee::add(ee::mul(p.level, ee::sub(1.0f, fm)), fm);
                else ++G().guards;
            }
        } else {
            p.fader->saved = p.fader->volume;
            p.fader->volume = 1.0f;
            f0 = AMix_Get("0:Fader0");
            f0->saved = f0->volume;
            f0->volume = 1.0f;
            music = AStream_Get("Music");
            if (music != nullptr) music->volume = 1.0f;
            else ++G().guards;
        }
    } else {
        const float fe = AMix_GetVolume(p.fade_effects);
        p.fader->saved = p.fader->volume;
        p.fader->volume = ee::add(ee::mul(p.level, ee::sub(1.0f, fe)), fe);
        f0 = AMix_Get("0:Fader0");
        const float v = ee::sub(f0->volume, ee::mul(dt, 0.25f));
        f0->saved = f0->volume;
        f0->volume = ee::lt(0.0f, v) ? v : 0.0f;
    }
}

// [driving] 0x002f9120 AFader::Priv::Call911 (o AFader::Call911 0x002f9808): no ambiente da cidade, a secundária
// recomeça (as sirenes).
void AFaderPriv_Call911(AFaderPriv& p) {
    if (nf::str_icmp(p.second, "city") != 0) return;
    if (p.primary == 0) {
        p.fading = 1;
        p.restart = 1;
    }
    p.call911 = 1;
    p.primary = 1;
}

// [driving] 0x002f9688 AFader::Create(char *, AStream *, AMix *)
void AFader_Create(const char* name, AStream* stream, AMix* fader) {
    auto p = std::make_unique<AFaderPriv>();
    p->stream = stream;
    p->fader = fader;
    p->fade_effects = AMix_Get("0:Fade Effects");
    p->fade_music = AMix_Get("0:Fade Music");
    faders()[name] = std::move(p);
}

}  // namespace

// ---- o ASoundManager e o EAudioUpdate --------------------------------------------------------------------------------

// A parte dos fluxos do ASoundManager::Init (0x002eb7a8, em audio.cpp): AStream::SetPath("driving/") (0x002e86a0),
// os dois fluxos com 0x20000 bytes de buffer e o AFader.
void audio_streams_create(const std::string& stream_file) {
    StreamGlobals& g = G();
    g = StreamGlobals{};
    nf::str_copy_trunc(g.path, sizeof g.path, "driving/");  // (DAT_003c7f48 & 1 == 0: o caminho sem o "./")
    std::string music = stream_file;
    // sprintf + strncpy(buf + strlen - 2, "", 1): o nome sem o idioma ("MIS01EN" -> "MIS01")
    if (music.size() >= 2) music.resize(music.size() - 2);
    AStream_Create("Music", music.c_str(), 0x20000);
    AStream* speech = AStream_Create("Speech", stream_file.c_str(), 0x20000);
    AFader_Create("SpeechVsAmbience", speech, AMix_Get("0:Fader2"));
}

void audio_streams_destroy() {
    faders().clear();
    while (!streams().empty()) streams().begin()->second->destroy();
    filesys_reset();
}

// O ambiente sob o carro (o pedaço do EAudioUpdate antes do BuildPaths) e o AFader::Update depois dele.
void audio_streams_enviro(const uint8_t* desc, AListener& l) {
    if (desc == nullptr || desc[8] == 0) return;
    int32_t mode;
    std::memcpy(&mode, desc + 4, 4);
    AFX_SetMode(mode, 0, 0);
    AFaderPriv* f = AFader_Get("SpeechVsAmbience");
    if (f != nullptr) AFaderPriv_SetSecondary(*f, reinterpret_cast<const char*>(desc + 8));  // (DAT_0032e678 = 1)
    float top, height;
    std::memcpy(&top, desc + 0x14, 4);
    std::memcpy(&height, desc + 0x18, 4);
    if (ee::lt(0.0f, height)) {
        l.band_height = height;
        l.band_top = top;
    }
    float filter = 0.0f;
    const float y = l.pos[1];
    if (!ee::lt(y, ee::sub(top, height))) {
        filter = 1.0f;
        if (!ee::lt(top, y)) filter = ee::mul(ee::add(ee::add(y, top), height), ps2::ffloat(0x3dcccccdu));
    }
    if (f != nullptr) f->filter = filter;  // AFader::SetFilter
}

void audio_streams_fader_update() {
    if (AFaderPriv* f = AFader_Get("SpeechVsAmbience")) AFaderPriv_Update(*f);
    static int32_t last = -1000;
    if (stream_log() != nullptr && TIMER_gettick() - last >= 100) {
        last = TIMER_gettick();
        const AudioStreamStats st = audio_stream_stats();
        slog("estado musica %d [%s] vol %.3f | falas %d [%s] vol %.3f mix %s | ambiente [%s] nivel %.3f",
             st.music_req, st.music_file.c_str(), double(st.music_volume), st.speech_req, st.speech_file.c_str(),
             double(st.speech_volume), AStream_Get("Speech") ? AStream_Get("Speech")->mix_name.c_str() : "",
             st.ambience.c_str(), double(st.fader_level));
    }
}

// [driving] 0x00230bb8 WWorld::GetEnviroDesc(WWorldPos &) const
const uint8_t* WWorld_GetEnviroDesc(const WWorldEnviro& w, const WWorldPos& wp) {
    const WCollisionInstance* ci = nf::p32_ptr<const WCollisionInstance>(wp.instance);
    if (ci == nullptr || w.instances == nullptr || w.table == nullptr) return nullptr;
    uint32_t idx = w.instances[ci->render_index].extra & 0xffff;
    if (w.count <= int32_t(idx)) idx = 0;
    return w.table + idx * 0x1c;
}

// [driving] 0x002ec968 ASoundManager::SetMissionOver
void audio_set_mission_over() {
    if (ASystem_heap() != nullptr)
        if (AStream* m = AStream_Get("Music")) m->FadeOut();
    G().mission_over = 1;
}

// ---- os eventos ------------------------------------------------------------------------------------------------------

namespace {

const char* str(uint32_t p32) { return nf::p32_ptr<const char>(p32); }
float fl(uint32_t b) { return ps2::ffloat(b); }

// [driving] 0x0015d000 EStreamEvent::~EStreamEvent: +4 o fluxo ("music", "speech", "nis"), +8 o arquivo, +0xc a
// transição em segundos (int), +0x10 laço, +0x14 só com o jogador vivo, +0x18 segurado.
void EStreamEvent_Execute(const PendingEvent& e) {
    if (e.obj[5] != 0 && mission_exists() && !ee::lt(0.0f, SMissionManager_GetPlayerHealth(mission_manager(), true)))
        return;
    const char* stream = str(e.obj[1]);
    const char* file = str(e.obj[2]);
    const bool nis = stream != nullptr && nf::str_icmp(stream, "nis") == 0;
    const bool skip = mission_exists() && SMissionManager_GoingToSkipCinematic(mission_manager());
    if (stream == nullptr || file == nullptr) {
        std::fprintf(stderr, "WARNING: EStreamEvent called on incomplete trigger!\n");
        return;
    }
    for (const std::string& f : G().failed)
        if (nf::str_icmp(file, f.c_str()) == 0) {
            std::fprintf(stderr, "WARNING: Attempt to play invalid stream [%s]\n", file);
            return;
        }
    const float fade = ee::cvt_s(int32_t(e.obj[3]));
    AFaderPriv* f = AFader_Get("SpeechVsAmbience");
    if (nf::str_icmp(stream, "speech") == 0) {
        if (!skip) {
            if (f != nullptr) AFaderPriv_Event(fade, *f, file, false, e.obj[6] != 0);
            else ++G().guards;
        }
    } else if (nis) {
        if (f != nullptr) AFaderPriv_Event(0.0f, *f, file, true, e.obj[6] != 0);
        else ++G().guards;
    } else {
        AStream* s = AStream_Get(stream);
        if (s == nullptr) {
            ++G().guards;  // o original chamaria o Event num objeto nulo
            return;
        }
        s->Event(fade, file, e.obj[4] != 0, false, e.obj[6] != 0);
    }
}

// [driving] 0x001560d8 EReleaseStream::~EReleaseStream (o AStream::Release 0x002eb708)
void EReleaseStream_Execute() {
    if (G().trigger == 1) G().trigger = 2;
    else if (G().trigger == 0) std::fprintf(stderr, "WARNING! Ignoring command to release a non-prebuffered stream\n");
}

// [driving] 0x00157308 ESetAudioMix::~ESetAudioMix: +4 a mixagem, +8 os quadros, +0xc a mudança em % (float).
void ESetAudioMix_Execute(const PendingEvent& e) {
    AMix* m = AMix_Get(str(e.obj[1]));
    const float pre = AMix_GetPresetVolume(m);
    float v = ee::mul(ee::add(ee::div(fl(e.obj[3]), 100.0f), 1.0f), pre);
    if (ee::lt(1.0f, v)) v = 1.0f;
    if (ee::lt(v, 0.0f)) v = 0.0f;
    AMix_SetTransition(v, m, int32_t(e.obj[2]));
}

// [driving] 0x00154798 EPlaySoundPos::~EPlaySoundPos: um AOneShotSound "World Sounds" parado num ponto. +4 o ponto
// (COORD3), +8 o banco, +0xc o som, +0x10 laço, +0x14 a distância mínima, +0x18 a máxima (floats).
void EPlaySoundPos_Execute(const PendingEvent& e) {
    const float* pos = nf::p32_ptr<const float>(e.obj[1]);
    if (e.obj[2] == 0 || e.obj[3] == 0 || pos == nullptr) return;
    ABank* bank = ABank_Get(str(e.obj[2]));
    if (bank == nullptr) {
        ++G().guards;
        return;
    }
    const int32_t patch = bank->index.Lookup(str(e.obj[3]));
    AOneShotSound* s = new AOneShotSound("World Sounds", bank->snd_bank, patch, 1.0f);
    s->fx = 0.0f;
    s->lifetime = 0.0f;
    for (AVoiceView& v : s->voice.views) v.loop = e.obj[4] != 0 ? 1 : 0;
    s->pos[0] = pos[0];
    s->pos[1] = pos[1];
    s->pos[2] = pos[2];
    s->min_distance = fl(e.obj[5]);
    const float mx = fl(e.obj[6]);
    s->max_distance = mx;
    s->max_distance2 = ee::mul(mx, mx);
}

}  // namespace

bool audio_event_execute(const PendingEvent& e) {
    if (!audio_ready()) {
        // Sem o som (o nf_driving sem --audio-dump nas capturas): os eventos de som existem e não fazem nada.
        return e.type == 20 || e.type == 273 || e.type == 241 || e.type == 266 || e.type == 164;
    }
    switch (e.type) {
        case 20: EStreamEvent_Execute(e); return true;
        case 273: EReleaseStream_Execute(); return true;
        case 241: ESetAudioMix_Execute(e); return true;  // (DAT_0032e668 = 1)
        case 266: AMix_Reset(int32_t(e.obj[1])); return true;  // [driving] 0x001562c8 EResetAudioMix
        case 164: EPlaySoundPos_Execute(e); return true;
        default: return false;
    }
}

// ---- a interface da conferência --------------------------------------------------------------------------------------

AStreamView astream_view(const char* name) {
    AStreamView v;
    AStream* a = AStream_Get(name);
    if (a == nullptr) return v;
    const AStreamPriv& p = *a->priv;
    v.exists = true;
    v.volume = a->volume;
    v.pitch = a->pitch;
    v.max_distance = a->max_distance;
    v.max_distance2 = a->max_distance2;
    v.mix = a->mix != nullptr ? a->mix->name : "";
    v.hold_timeout = a->hold_timeout;
    v.held_ticks = a->held_ticks;
    v.release_tick = a->release_tick;
    for (const AStreamEntry& e : p.queue) v.queue.push_back({e.name, e.fade, e.loop, e.effect, e.held, e.tick});
    v.status[0] = p.status.requests;
    v.status[1] = p.status.current;
    v.status[2] = p.status.buffered_ms;
    v.rstatus[0] = p.rstatus.state;
    v.rstatus[1] = p.rstatus.played_ms;
    v.rstatus[2] = p.rstatus.left_ms;
    v.rstatus[3] = p.rstatus.outstanding_ms;
    v.file = p.file ? p.file.get() : "";
    v.filter = p.filter;
    v.fade = p.fade;
    v.priv_volume = p.volume;
    v.stream = p.stream;
    v.req = p.req;
    v.event_tick = p.event_tick;
    v.play_tick = p.play_tick;
    v.over = p.over;
    v.not_found = p.not_found;
    v.paused = p.paused;
    v.loop = p.loop;
    v.effect = p.effect;
    v.held = p.held;
    v.mem = p.mem;
    v.name = p.name;
    return v;
}

void astream_test_play(const char* name, float vol, float pitch, float azimuth) {
    AStream* a = AStream_Get(name);
    if (a == nullptr) return;
    APath path;
    path.volume = vol;
    path.pitch = pitch;
    path.azimuth = azimuth;
    a->Play(path);
}
void astream_test_event(const char* name, float fade, const char* file, bool loop, bool effect, bool held) {
    if (AStream* a = AStream_Get(name)) a->Event(fade, file, loop, effect, held);
}
void astream_test_next(const char* name) {
    if (AStream* a = AStream_Get(name)) a->Next();
}
void astream_test_last(const char* name) {
    if (AStream* a = AStream_Get(name)) a->Last();
}
void astream_test_stop(const char* name) {
    if (AStream* a = AStream_Get(name)) a->Stop();
}
void astream_test_fadeout(const char* name) {
    if (AStream* a = AStream_Get(name)) a->FadeOut();
}
float astream_test_latency(const char* name) {
    AStream* a = AStream_Get(name);
    return a != nullptr ? a->GetLatency() : 0.0f;
}

AFaderView afader_view(const char* name) {
    AFaderView v;
    AFaderPriv* f = AFader_Get(name);
    if (f == nullptr) return v;
    v.exists = true;
    v.level = f->level;
    v.second = f->second;
    v.filter = f->filter;
    v.primary = f->primary;
    v.restart = f->restart;
    v.fading = f->fading;
    v.call911 = f->call911;
    v.allow_second = f->allow_second;
    v.second_on = f->second_on;
    v.w40 = f->w40;
    return v;
}
void afader_test_update(const char* name) {
    if (AFaderPriv* f = AFader_Get(name)) AFaderPriv_Update(*f);
}
int32_t afader_test_event(const char* name, float fade, const char* file, bool effect, bool held) {
    AFaderPriv* f = AFader_Get(name);
    return f != nullptr ? AFaderPriv_Event(fade, *f, file, effect, held) : 0;
}
void afader_test_set_secondary(const char* name, const char* second) {
    if (AFaderPriv* f = AFader_Get(name)) AFaderPriv_SetSecondary(*f, second);
}
void afader_test_call911(const char* name) {
    if (AFaderPriv* f = AFader_Get(name)) AFaderPriv_Call911(*f);
}

AStreamGlobalsView astream_globals() {
    AStreamGlobalsView v;
    v.trigger = G().trigger;
    v.hold_begin = G().hold_begin;
    v.mission_over = G().mission_over;
    v.hud_fade = G().hud_fade;
    v.is_paused = G().is_paused;
    return v;
}
void astream_set_globals(const AStreamGlobalsView& v) {
    G().trigger = v.trigger;
    G().hold_begin = v.hold_begin;
    G().mission_over = v.mission_over;
    G().hud_fade = int16_t(v.hud_fade);
    G().is_paused = v.is_paused;
}

AudioStreamStats audio_stream_stats() {
    AudioStreamStats r;
    const StreamGlobals& g = G();
    r.events = g.events;
    r.queued = g.queued;
    r.failed = g.failed_queue;
    r.guards = g.guards + mix_guards() + snd_stream_guards();
    r.mix_voices = uint64_t(mix_voices_playing());
    if (AStream* m = AStream_Get("Music")) {
        r.music_req = m->priv->req;
        r.music_file = m->priv->file ? m->priv->file.get() : "";
        r.music_queue = int32_t(m->priv->queue.size());
        r.music_volume = m->priv->volume;
    }
    if (AStream* s = AStream_Get("Speech")) {
        r.speech_req = s->priv->req;
        r.speech_file = s->priv->file ? s->priv->file.get() : "";
        r.speech_queue = int32_t(s->priv->queue.size());
        r.speech_volume = s->priv->volume;
    }
    if (AFaderPriv* f = AFader_Get("SpeechVsAmbience")) {
        r.fader_level = f->level;
        r.ambience = f->second;
    }
    return r;
}

}  // namespace drv
